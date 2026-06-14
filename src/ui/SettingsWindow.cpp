#include "src/ui/SettingsWindow.h"
#include "src/utils/ImageEncoder.h"
#include "resource.h"
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <string>
#include <cwchar>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

constexpr wchar_t SettingsWindow::CLASS_NAME[];

// -----------------------------------------------------------------------------
//  Constructor / Destructor
// -----------------------------------------------------------------------------

SettingsWindow::SettingsWindow() = default;
SettingsWindow::~SettingsWindow()
{
    UnregisterCaptureHotkey();
    UnregisterPauseHotkey();
    RemoveTrayIcon();
}

// -----------------------------------------------------------------------------
//  Create
// -----------------------------------------------------------------------------

bool SettingsWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc)) return false;

    // Compute centred position
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_TOPMOST,
        CLASS_NAME,
        L"Game Translation Overlay — Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sx - WND_W) / 2, (sy - WND_H) / 2,
        WND_W, WND_H,
        nullptr, nullptr, hInstance, this
    );

    return m_hwnd != nullptr;
}

// -----------------------------------------------------------------------------
//  Message loop
// -----------------------------------------------------------------------------

int SettingsWindow::RunMessageLoop()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// -----------------------------------------------------------------------------
//  Window procedure
// -----------------------------------------------------------------------------

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SettingsWindow* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        // -- Creation --------------------------------------------------------------
    case WM_CREATE:
    {
        // Load persisted config
        m_config.Load(GetIniPath());

        // Initialize Capture Helper window
        m_captureHelper.Create(m_hInstance);
        m_captureHelper.SetRect(m_config.captureRect);
        m_captureHelper.OnRectChanged = [&](const RECT& rc)
            {
                m_config.captureRect = rc;
                m_config.captureSet = true;
                UpdateRegionLabel();
            };

        // Initialise Overlay window
        m_overlay.Create(m_hInstance);
        m_overlay.SetPosition(m_config.overlayPos.x, m_config.overlayPos.y);
        m_overlay.SetSize(m_config.overlayWidth, m_config.overlayHeight);
        m_overlay.OnMoved = [&](int x, int y)
            {
                m_config.overlayPos = { x, y };
                int w, h;
                m_overlay.GetSize(w, h);
                m_config.overlayWidth = w;
                m_config.overlayHeight = h;
                // Update edit boxes (post to avoid nested SendMessage)
                PostMessageW(m_hwnd, WM_UPDATE_STATUS,
                    0, reinterpret_cast<LPARAM>(nullptr));
            };

        // Create child controls
        CreateControls();

        // Populate controls with loaded config
        ConfigToUI();

        // Show helper windows if Settings is visible
        SyncHelperWindows();

        // Scheduler status callback
        m_scheduler.OnStatus = [&](const std::wstring& s)
            {
                // Marshal to UI thread
                wchar_t* buf = new wchar_t[s.size() + 1];
                std::wmemcpy(buf, s.c_str(), s.size() + 1);
                PostMessageW(m_hwnd, WM_UPDATE_STATUS, 0, reinterpret_cast<LPARAM>(buf));
            };

        return 0;
    }

    // -- Status update (from any thread) ---------------------------------------
    case WM_UPDATE_STATUS:
    {
        if (lParam)
        {
            auto* buf = reinterpret_cast<wchar_t*>(lParam);
            UpdateStatus(buf);
            delete[] buf;
        }
        else
        {
            // Refresh overlay position label after a drag move
            wchar_t posBuf[64]{};
            swprintf(posBuf, 64, L"X: %ld   Y: %ld",
                m_config.overlayPos.x, m_config.overlayPos.y);
            SetDlgItemTextW(m_hwnd, IDC_OVERLAY_POS_LABEL, posBuf);
        }
        return 0;
    }

    // -- Tab notification ------------------------------------------------------
    case WM_NOTIFY:
    {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->idFrom == IDC_TAB_CTRL && nmhdr->code == TCN_SELCHANGE)
        {
            OnTabChanged();
        }
        break;
    }

    // -- Hotkey ----------------------------------------------------------------
    case WM_HOTKEY:
    {
        OnHotkey(static_cast<int>(wParam));
        return 0;
    }

    // -- Commands (controls + tray menu) ---------------------------------------
    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        switch (id)
        {
        case IDC_START_BTN:        OnStart();                             break;
        case IDC_STOP_BTN:         OnStop();                              break;
        case IDC_SELECT_REGION:    OnSelectRegion();                      break;
        case IDC_TEST_API_BTN:     OnTestApi();                           break;
        case IDC_SAVE_BTN:         OnSave();                              break;
        case IDC_TEXT_COLOR_BTN:   OnTextColorPick();                     break;
        case IDC_SHADOW_COLOR_BTN: OnColorPick(m_config.shadowColor);     break;
        case IDC_STROKE_COLOR_BTN: OnColorPick(m_config.strokeColor);     break;
            // Provider combo - notify when selection changes
        case IDC_PROVIDER_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) OnProviderChanged();
            break;
            // Capture mode combo
        case IDC_CAPTURE_MODE_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) OnCaptureModeChanged();
            break;
            // Tray menu
        case ID_TRAY_SHOW:
            RemoveTrayIcon();
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
            SyncHelperWindows();
            break;
        case ID_TRAY_EXIT:
            SendMessageW(m_hwnd, WM_CLOSE, 0, 0);
            break;
        }
        return 0;
    }

    // -- Tray icon -------------------------------------------------------------
    case WM_TRAYICON:
    {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            ShowTrayMenu();
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            RemoveTrayIcon();
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
            SyncHelperWindows();
        }
        return 0;
    }

    // -- Minimise -> go to tray -------------------------------------------------
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            AddTrayIcon();
            ShowWindow(m_hwnd, SW_HIDE);
        }
        SyncHelperWindows();
        return 0;

        // -- Close -----------------------------------------------------------------
    case WM_CLOSE:
        OnStop();
        UnregisterCaptureHotkey();
        UnregisterPauseHotkey();
        m_captureHelper.Destroy();
        m_overlay.Destroy();
        RemoveTrayIcon();
        m_config.Save(GetIniPath());
        DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Handle tray menu WM_COMMAND (comes as top-level WM_COMMAND after menu pump)
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
//  Control creation helpers
// -----------------------------------------------------------------------------

HWND SettingsWindow::MakeLabel(int x, int y, int w, int h, const wchar_t* txt)
{
    return CreateWindowExW(0, L"STATIC", txt,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, m_hwnd, nullptr, m_hInstance, nullptr);
}

HWND SettingsWindow::MakeEdit(int x, int y, int w, int h, UINT id, bool multiLine)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    if (multiLine) style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY;
    return CreateWindowExW(0, L"EDIT", L"", style,
        x, y, w, h, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        m_hInstance, nullptr);
}

HWND SettingsWindow::MakeButton(int x, int y, int w, int h, const wchar_t* txt, UINT id)
{
    return CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        m_hInstance, nullptr);
}

HWND SettingsWindow::MakeCheck(int x, int y, int w, int h, const wchar_t* txt, UINT id)
{
    return CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, w, h, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        m_hInstance, nullptr);
}

HWND SettingsWindow::MakeCombo(int x, int y, int w, int h, UINT id)
{
    return CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
        m_hInstance, nullptr);
}

HWND SettingsWindow::MakeGroup(int x, int y, int w, int h, const wchar_t* txt)
{
    return CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, w, h, m_hwnd, nullptr, m_hInstance, nullptr);
}

// -----------------------------------------------------------------------------
//  CreateControls - tabbed layout
// -----------------------------------------------------------------------------

void SettingsWindow::CreateControls()
{
    const int M = 10;   // margin
    const int W = WND_W - M * 2 - 10;  // usable width

    int y = M;

    // -- Tab Control -----------------------------------------------------------
    m_tabCtrl = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
        M, y, W, 370,
        m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_TAB_CTRL)),
        m_hInstance, nullptr);

    // Insert tabs: Realtime first, Translate second
    TCITEMW tie{};
    tie.mask = TCIF_TEXT;

    tie.pszText = const_cast<wchar_t*>(L"Realtime");
    TabCtrl_InsertItem(m_tabCtrl, 0, &tie);

    tie.pszText = const_cast<wchar_t*>(L"Translate");
    TabCtrl_InsertItem(m_tabCtrl, 1, &tie);

    // Get the display area inside the tab control
    RECT tabRect{};
    GetClientRect(m_tabCtrl, &tabRect);
    TabCtrl_AdjustRect(m_tabCtrl, FALSE, &tabRect);

    int tabX = M + tabRect.left;
    int tabY = y + tabRect.top;
    int tabW = tabRect.right - tabRect.left;

    // Create controls for each tab
    CreateRealtimeTab(tabX, tabY, tabW);
    CreateTranslateTab(tabX, tabY, tabW);

    // Show the first tab (Realtime) by default
    ShowTab(0);

    y += 378;   // below tab control

    // -- Start / Stop (always visible) -----------------------------------------
    MakeButton(M, y, 120, 36, L"\u25B6  START", IDC_START_BTN);
    MakeButton(WND_W - 140, y, 120, 36, L"\u25A0  STOP", IDC_STOP_BTN);
    EnableWindow(GetDlgItem(m_hwnd, IDC_STOP_BTN), FALSE);
    y += 44;

    // -- Status log (always visible) -------------------------------------------
    MakeGroup(M, y, W, 175, L"  Output Log  ");
    y += 18;
    MakeEdit(M + 8, y, W - 16, 145, IDC_STATUS_EDIT, true);

    // Default font for all controls
    HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    EnumChildWindows(m_hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, lp, TRUE);
        return TRUE;
        }, reinterpret_cast<LPARAM>(hFont));
}

// -----------------------------------------------------------------------------
//  CreateRealtimeTab — Capture + Overlay settings
// -----------------------------------------------------------------------------

void SettingsWindow::CreateRealtimeTab(int x, int y, int w)
{
    const int LH = 22;
    const int EH = 24;
    const int BH = 28;
    HWND h;

    int cy = y + 4;

    // -- Capture Settings group ------------------------------------------------
    h = MakeGroup(x, cy, w, 155, L"  Capture Settings  ");
    m_realtimeControls.push_back(h);
    cy += 18;

    h = MakeLabel(x + 8, cy, 70, LH, L"Monitor:");
    m_realtimeControls.push_back(h);
    {
        HWND hMonitor = MakeCombo(x + 80, cy, 120, 140, IDC_MONITOR_COMBO);
        SendMessageW(hMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"0 - Primary"));
        SendMessageW(hMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 - Second"));
        SendMessageW(hMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2 - Third"));
        SendMessageW(hMonitor, CB_SETCURSEL, 0, 0);
        m_realtimeControls.push_back(hMonitor);
    }
    cy += EH + 6;

    h = MakeButton(x + 8, cy, 170, BH, L"Reset Capture Region", IDC_SELECT_REGION);
    m_realtimeControls.push_back(h);
    {
        HWND hRegion = CreateWindowExW(0, L"STATIC", L"(not set)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x + 185, cy + 4, w - 192, LH, m_hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_REGION_INFO)),
            m_hInstance, nullptr);
        m_realtimeControls.push_back(hRegion);
    }
    cy += BH + 6;

    // Capture mode selector
    h = MakeLabel(x + 8, cy, 90, LH, L"Capture Mode:");
    m_realtimeControls.push_back(h);
    {
        HWND hMode = MakeCombo(x + 100, cy, 200, 80, IDC_CAPTURE_MODE_COMBO);
        SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto (continuous)"));
        SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Hotkey (single frame)"));
        SendMessageW(hMode, CB_SETCURSEL, 0, 0);
        m_realtimeControls.push_back(hMode);
    }
    cy += EH + 6;

    // Auto mode: interval in ms & pause hotkey
    {
        h = MakeLabel(x + 8, cy, 100, LH, L"Interval (ms):");
        m_realtimeControls.push_back(h);
        m_autoModeControls.push_back(h);

        h = MakeEdit(x + 110, cy, 70, EH, IDC_INTERVAL_EDIT);
        SetWindowTextW(h, L"1000");
        m_realtimeControls.push_back(h);
        m_autoModeControls.push_back(h);

        h = MakeLabel(x + 200, cy, 90, LH, L"Pause Hotkey:");
        m_realtimeControls.push_back(h);
        m_autoModeControls.push_back(h);

        h = MakeEdit(x + 295, cy, 130, EH, IDC_PAUSE_HOTKEY_EDIT);
        SetWindowTextW(h, L"F3");
        SetWindowSubclass(h, HotkeyEditSubclassProc, IDC_PAUSE_HOTKEY_EDIT, reinterpret_cast<DWORD_PTR>(this));
        m_realtimeControls.push_back(h);
        m_autoModeControls.push_back(h);
    }

    // Hotkey mode: hotkey display
    {
        h = MakeLabel(x + 8, cy, 110, LH, L"Hotkey:");
        m_realtimeControls.push_back(h);
        m_hotkeyModeControls.push_back(h);
        h = MakeEdit(x + 120, cy, 80, EH, IDC_HOTKEY_EDIT);
        SetWindowTextW(h, L"F2");
        SetWindowSubclass(h, HotkeyEditSubclassProc, IDC_HOTKEY_EDIT, reinterpret_cast<DWORD_PTR>(this));
        m_realtimeControls.push_back(h);
        m_hotkeyModeControls.push_back(h);
    }
    cy += EH + 10;

    // -- Overlay & Typography group --------------------------------------------
    h = MakeGroup(x, cy, w, 180, L"  Overlay & Typography  ");
    m_realtimeControls.push_back(h);
    cy += 18;

    h = MakeLabel(x + 8, cy, 72, LH, L"Position:");
    m_realtimeControls.push_back(h);
    {
        HWND hPos = CreateWindowExW(0, L"STATIC", L"-",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x + 82, cy, w - 88, LH, m_hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_OVERLAY_POS_LABEL)),
            m_hInstance, nullptr);
        m_realtimeControls.push_back(hPos);
    }
    cy += EH + 6;

    h = MakeLabel(x + 8, cy, 60, LH, L"Font:");
    m_realtimeControls.push_back(h);
    h = MakeEdit(x + 70, cy, 180, EH, IDC_FONT_NAME_EDIT);
    m_realtimeControls.push_back(h);
    h = MakeLabel(x + 260, cy, 40, LH, L"Size:");
    m_realtimeControls.push_back(h);
    h = MakeEdit(x + 302, cy, 55, EH, IDC_FONT_SIZE_EDIT);
    m_realtimeControls.push_back(h);
    cy += EH + 8;

    h = MakeLabel(x + 8, cy, 80, LH, L"Text Color:");
    m_realtimeControls.push_back(h);
    h = MakeButton(x + 90, cy, 90, BH, L"Choose\u2026", IDC_TEXT_COLOR_BTN);
    m_realtimeControls.push_back(h);
    cy += BH + 4;

    h = MakeCheck(x + 8, cy, 80, LH, L"Shadow", IDC_SHADOW_CHECK);
    m_realtimeControls.push_back(h);
    h = MakeButton(x + 90, cy, 90, BH, L"Color\u2026", IDC_SHADOW_COLOR_BTN);
    m_realtimeControls.push_back(h);
    cy += BH + 4;

    h = MakeCheck(x + 8, cy, 80, LH, L"Stroke", IDC_STROKE_CHECK);
    m_realtimeControls.push_back(h);
    h = MakeButton(x + 90, cy, 90, BH, L"Color\u2026", IDC_STROKE_COLOR_BTN);
    m_realtimeControls.push_back(h);
}

// -----------------------------------------------------------------------------
//  CreateTranslateTab — API settings
// -----------------------------------------------------------------------------

void SettingsWindow::CreateTranslateTab(int x, int y, int w)
{
    const int LH = 22;
    const int EH = 24;
    const int BH = 28;
    HWND h;

    int cy = y + 4;

    // Provider
    h = MakeLabel(x + 8, cy, 75, LH, L"Provider:");
    m_translateControls.push_back(h);
    {
        HWND hProv = MakeCombo(x + 85, cy, 160, 80, IDC_PROVIDER_COMBO);
        SendMessageW(hProv, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DeepSeek"));
        SendMessageW(hProv, CB_SETCURSEL, 0, 0);
        m_translateControls.push_back(hProv);
    }
    cy += EH + 8;

    // API Model
    h = MakeLabel(x + 8, cy, 75, LH, L"API Model:");
    m_translateControls.push_back(h);
    h = MakeEdit(x + 85, cy, 160, EH, IDC_API_MODEL_EDIT);
    m_translateControls.push_back(h);
    cy += EH + 8;

    // API Key
    h = MakeLabel(x + 8, cy, 75, LH, L"API Key:");
    m_translateControls.push_back(h);
    h = MakeEdit(x + 85, cy, w - 95, EH, IDC_API_KEY_EDIT);
    m_translateControls.push_back(h);
    cy += EH + 8;

    // Test + Save buttons
    h = MakeButton(x + 8, cy, 120, BH, L"Test Connection", IDC_TEST_API_BTN);
    m_translateControls.push_back(h);
    h = MakeButton(x + 138, cy, 120, BH, L"Save Settings", IDC_SAVE_BTN);
    m_translateControls.push_back(h);
}

// -----------------------------------------------------------------------------
//  Tab switching
// -----------------------------------------------------------------------------

void SettingsWindow::OnTabChanged()
{
    int sel = TabCtrl_GetCurSel(m_tabCtrl);
    if (sel >= 0) ShowTab(sel);
}

void SettingsWindow::ShowTab(int index)
{
    m_currentTab = index;

    // Tab 0 = Realtime, Tab 1 = Translate
    int showRealtime  = (index == 0) ? SW_SHOW : SW_HIDE;
    int showTranslate = (index == 1) ? SW_SHOW : SW_HIDE;

    for (HWND h : m_realtimeControls)
        ShowWindow(h, showRealtime);

    for (HWND h : m_translateControls)
        ShowWindow(h, showTranslate);

    // If showing Realtime tab, also apply capture mode visibility
    if (index == 0)
        UpdateCaptureModeUI();
}

// -----------------------------------------------------------------------------
//  Capture mode switching
// -----------------------------------------------------------------------------

void SettingsWindow::OnCaptureModeChanged()
{
    UpdateCaptureModeUI();
}

void SettingsWindow::UpdateCaptureModeUI()
{
    HWND hMode = GetDlgItem(m_hwnd, IDC_CAPTURE_MODE_COMBO);
    int sel = static_cast<int>(SendMessageW(hMode, CB_GETCURSEL, 0, 0));
    bool isAuto = (sel == 0);

    for (HWND h : m_autoModeControls)
        ShowWindow(h, isAuto ? SW_SHOW : SW_HIDE);

    for (HWND h : m_hotkeyModeControls)
        ShowWindow(h, isAuto ? SW_HIDE : SW_SHOW);
}

// -----------------------------------------------------------------------------
//  Config <-> UI
// -----------------------------------------------------------------------------

void SettingsWindow::ConfigToUI()
{
    // Provider
    HWND hProv = GetDlgItem(m_hwnd, IDC_PROVIDER_COMBO);
    SendMessageW(hProv, CB_SETCURSEL,
        m_config.providerType == TranslateProvider::DeepSeek ? 0 : 0, 0);

    SetDlgItemTextW(m_hwnd, IDC_API_MODEL_EDIT, m_config.apiModel.c_str());
    SetDlgItemTextW(m_hwnd, IDC_API_KEY_EDIT, m_config.apiKey.c_str());

    // Monitor combo
    HWND hMon = GetDlgItem(m_hwnd, IDC_MONITOR_COMBO);
    SendMessageW(hMon, CB_SETCURSEL,
        std::min(m_config.monitorIndex, 2), 0);

    // Capture mode
    HWND hMode = GetDlgItem(m_hwnd, IDC_CAPTURE_MODE_COMBO);
    SendMessageW(hMode, CB_SETCURSEL, static_cast<int>(m_config.captureMode), 0);

    // Interval
    SetDlgItemInt(m_hwnd, IDC_INTERVAL_EDIT,
        static_cast<UINT>(m_config.captureIntervalMs), FALSE);

    // Hotkey
    SetDlgItemTextW(m_hwnd, IDC_HOTKEY_EDIT, HotkeyToString(m_config.hotkeyVk, m_config.hotkeyMod).c_str());
    SetDlgItemTextW(m_hwnd, IDC_PAUSE_HOTKEY_EDIT, HotkeyToString(m_config.pauseHotkeyVk, m_config.pauseHotkeyMod).c_str());

    UpdateCaptureModeUI();

    {
        wchar_t posBuf[64]{};
        swprintf(posBuf, 64, L"X: %ld   Y: %ld",
            m_config.overlayPos.x, m_config.overlayPos.y);
        SetDlgItemTextW(m_hwnd, IDC_OVERLAY_POS_LABEL, posBuf);
    }

    SetDlgItemTextW(m_hwnd, IDC_FONT_NAME_EDIT, m_config.fontName.c_str());
    SetDlgItemInt(m_hwnd, IDC_FONT_SIZE_EDIT,
        static_cast<UINT>(m_config.fontSize), FALSE);

    CheckDlgButton(m_hwnd, IDC_SHADOW_CHECK,
        m_config.shadowEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hwnd, IDC_STROKE_CHECK,
        m_config.strokeEnabled ? BST_CHECKED : BST_UNCHECKED);

    UpdateRegionLabel();
}

void SettingsWindow::UIToConfig()
{
    wchar_t buf[1024]{};

    // Provider type
    {
        HWND hProv = GetDlgItem(m_hwnd, IDC_PROVIDER_COMBO);
        int idx = static_cast<int>(SendMessageW(hProv, CB_GETCURSEL, 0, 0));
        m_config.providerType = (idx == 0) ? TranslateProvider::DeepSeek : TranslateProvider::DeepSeek;
    }

    GetDlgItemTextW(m_hwnd, IDC_API_MODEL_EDIT, buf, 1024);
    m_config.apiModel = buf;

    GetDlgItemTextW(m_hwnd, IDC_API_KEY_EDIT, buf, 1024);
    m_config.apiKey = buf;

    HWND hMon = GetDlgItem(m_hwnd, IDC_MONITOR_COMBO);
    m_config.monitorIndex = static_cast<int>(SendMessageW(hMon, CB_GETCURSEL, 0, 0));

    // Capture mode
    HWND hMode = GetDlgItem(m_hwnd, IDC_CAPTURE_MODE_COMBO);
    m_config.captureMode = static_cast<CaptureMode>(SendMessageW(hMode, CB_GETCURSEL, 0, 0));

    // Interval
    BOOL ok{};
    m_config.captureIntervalMs = static_cast<int>(
        GetDlgItemInt(m_hwnd, IDC_INTERVAL_EDIT, &ok, FALSE));
    if (m_config.captureIntervalMs <= 0) m_config.captureIntervalMs = 1000;

    // Hotkey: m_config.hotkeyVk and m_config.hotkeyMod are updated directly in real time by the subclass proc.

    // Position is read directly from the overlay window (set by drag)
    int ox, oy;
    m_overlay.GetPosition(ox, oy);
    m_config.overlayPos = { static_cast<LONG>(ox), static_cast<LONG>(oy) };

    GetDlgItemTextW(m_hwnd, IDC_FONT_NAME_EDIT, buf, 256);
    m_config.fontName = buf;
    m_config.fontSize = static_cast<int>(
        GetDlgItemInt(m_hwnd, IDC_FONT_SIZE_EDIT, &ok, FALSE));
    if (m_config.fontSize <= 0) m_config.fontSize = 24;

    m_config.shadowEnabled =
        (IsDlgButtonChecked(m_hwnd, IDC_SHADOW_CHECK) == BST_CHECKED);
    m_config.strokeEnabled =
        (IsDlgButtonChecked(m_hwnd, IDC_STROKE_CHECK) == BST_CHECKED);
}

void SettingsWindow::UpdateRegionLabel()
{
    wchar_t buf[128]{};
    if (m_config.captureSet)
    {
        swprintf(buf, 128,
            L"(%ld,%ld) \u2013 (%ld,%ld)  [%ldx%ld]",
            m_config.captureRect.left, m_config.captureRect.top,
            m_config.captureRect.right, m_config.captureRect.bottom,
            m_config.captureRect.right - m_config.captureRect.left,
            m_config.captureRect.bottom - m_config.captureRect.top);
    }
    else
    {
        wcscpy(buf, L"(not set)");
    }
    SetDlgItemTextW(m_hwnd, IDC_REGION_INFO, buf);
}

void SettingsWindow::UpdateStatus(const std::wstring& text)
{
    HWND hEdit = GetDlgItem(m_hwnd, IDC_STATUS_EDIT);
    // Append with timestamp
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t line[512]{};
    swprintf_s(line, 512, L"[%02d:%02d:%02d] %ls\r\n",
        st.wHour, st.wMinute, st.wSecond, text.c_str());

    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE,
        reinterpret_cast<LPARAM>(line));
    // Auto-scroll to bottom
    SendMessageW(hEdit, WM_VSCROLL, SB_BOTTOM, 0);
}

// -----------------------------------------------------------------------------
//  VK to display name helper
// -----------------------------------------------------------------------------

std::wstring SettingsWindow::VkToName(UINT vk)
{
    switch (vk)
    {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_TAB: return L"Tab";
    case VK_ESCAPE: return L"Esc";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_LEFT: return L"Left";
    case VK_UP: return L"Up";
    case VK_RIGHT: return L"Right";
    case VK_DOWN: return L"Down";
    case VK_SNAPSHOT: return L"PrintScreen";
    case VK_SCROLL: return L"ScrollLock";
    case VK_PAUSE: return L"Pause";
    case VK_CAPITAL: return L"CapsLock";
    case VK_NUMLOCK: return L"NumLock";
    }

    if (vk >= VK_F1 && vk <= VK_F24)
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= 'A' && vk <= 'Z')
        return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= '0' && vk <= '9')
        return std::wstring(1, static_cast<wchar_t>(vk));

    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lParam = scanCode << 16;
    switch (vk)
    {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_DIVIDE:
    case VK_NUMLOCK:
        lParam |= 0x01000000;
        break;
    }

    wchar_t name[64]{};
    if (GetKeyNameTextW(lParam, name, 64) > 0)
    {
        return name;
    }

    return L"VK_" + std::to_wstring(vk);
}

std::wstring SettingsWindow::HotkeyToString(UINT vk, UINT mod)
{
    if (vk == 0) return L"None";
    std::wstring s;
    if (mod & MOD_CONTROL) s += L"Ctrl + ";
    if (mod & MOD_SHIFT)   s += L"Shift + ";
    if (mod & MOD_ALT)     s += L"Alt + ";
    if (mod & MOD_WIN)     s += L"Win + ";
    s += VkToName(vk);
    return s;
}

LRESULT CALLBACK SettingsWindow::HotkeyEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    auto* pThis = reinterpret_cast<SettingsWindow*>(dwRefData);
    if (!pThis)
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);

    switch (uMsg)
    {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        UINT vk = static_cast<UINT>(wParam);
        
        // Identify modifiers currently held down
        UINT mod = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
        if (GetKeyState(VK_SHIFT)   & 0x8000) mod |= MOD_SHIFT;
        if (GetKeyState(VK_MENU)    & 0x8000) mod |= MOD_ALT;
        if (GetKeyState(VK_LWIN)    & 0x8000) mod |= MOD_WIN;
        if (GetKeyState(VK_RWIN)    & 0x8000) mod |= MOD_WIN;

        // Check if the key pressed is itself a modifier key
        bool isModifierKey = (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                              vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                              vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                              vk == VK_LWIN || vk == VK_RWIN);

        if (isModifierKey)
        {
            std::wstring s;
            if (mod & MOD_CONTROL) s += L"Ctrl + ";
            if (mod & MOD_SHIFT)   s += L"Shift + ";
            if (mod & MOD_ALT)     s += L"Alt + ";
            if (mod & MOD_WIN)     s += L"Win + ";
            s += L"...";
            SetWindowTextW(hWnd, s.c_str());
        }
        else
        {
            if ((vk == VK_ESCAPE || vk == VK_BACK || vk == VK_DELETE) && mod == 0)
            {
                if (uIdSubclass == IDC_HOTKEY_EDIT)
                {
                    pThis->m_config.hotkeyVk = 0;
                    pThis->m_config.hotkeyMod = 0;
                }
                else if (uIdSubclass == IDC_PAUSE_HOTKEY_EDIT)
                {
                    pThis->m_config.pauseHotkeyVk = 0;
                    pThis->m_config.pauseHotkeyMod = 0;
                }
                SetWindowTextW(hWnd, L"None");
            }
            else
            {
                if (uIdSubclass == IDC_HOTKEY_EDIT)
                {
                    pThis->m_config.hotkeyVk = vk;
                    pThis->m_config.hotkeyMod = mod;
                }
                else if (uIdSubclass == IDC_PAUSE_HOTKEY_EDIT)
                {
                    pThis->m_config.pauseHotkeyVk = vk;
                    pThis->m_config.pauseHotkeyMod = mod;
                }
                SetWindowTextW(hWnd, SettingsWindow::HotkeyToString(vk, mod).c_str());
            }
        }
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        wchar_t buf[64]{};
        GetWindowTextW(hWnd, buf, 64);
        std::wstring text(buf);
        if (text.size() >= 3 && text.compare(text.size() - 3, 3, L"...") == 0)
        {
            UINT mod = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
            if (GetKeyState(VK_SHIFT)   & 0x8000) mod |= MOD_SHIFT;
            if (GetKeyState(VK_MENU)    & 0x8000) mod |= MOD_ALT;
            if (GetKeyState(VK_LWIN)    & 0x8000) mod |= MOD_WIN;
            if (GetKeyState(VK_RWIN)    & 0x8000) mod |= MOD_WIN;

            std::wstring s;
            if (mod & MOD_CONTROL) s += L"Ctrl + ";
            if (mod & MOD_SHIFT)   s += L"Shift + ";
            if (mod & MOD_ALT)     s += L"Alt + ";
            if (mod & MOD_WIN)     s += L"Win + ";
            if (s.empty())
            {
                if (uIdSubclass == IDC_HOTKEY_EDIT)
                {
                    s = SettingsWindow::HotkeyToString(pThis->m_config.hotkeyVk, pThis->m_config.hotkeyMod);
                }
                else if (uIdSubclass == IDC_PAUSE_HOTKEY_EDIT)
                {
                    s = SettingsWindow::HotkeyToString(pThis->m_config.pauseHotkeyVk, pThis->m_config.pauseHotkeyMod);
                }
            }
            else
            {
                s += L"...";
            }
            SetWindowTextW(hWnd, s.c_str());
        }
        return 0;
    }

    case WM_KILLFOCUS:
    {
        if (uIdSubclass == IDC_HOTKEY_EDIT)
        {
            SetWindowTextW(hWnd, SettingsWindow::HotkeyToString(pThis->m_config.hotkeyVk, pThis->m_config.hotkeyMod).c_str());
        }
        else if (uIdSubclass == IDC_PAUSE_HOTKEY_EDIT)
        {
            SetWindowTextW(hWnd, SettingsWindow::HotkeyToString(pThis->m_config.pauseHotkeyVk, pThis->m_config.pauseHotkeyMod).c_str());
        }
        break;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, HotkeyEditSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// -----------------------------------------------------------------------------
//  Hotkey registration
// -----------------------------------------------------------------------------

void SettingsWindow::RegisterCaptureHotkey()
{
    UnregisterCaptureHotkey();
    if (m_config.hotkeyVk == 0)
    {
        UpdateStatus(L"No hotkey configured.");
        return;
    }
    if (RegisterHotKey(m_hwnd, HOTKEY_CAPTURE_ID, m_config.hotkeyMod, m_config.hotkeyVk))
    {
        m_hotkeyRegistered = true;
        UpdateStatus(L"Hotkey registered: " + HotkeyToString(m_config.hotkeyVk, m_config.hotkeyMod));
    }
    else
    {
        UpdateStatus(L"Failed to register hotkey: " + HotkeyToString(m_config.hotkeyVk, m_config.hotkeyMod));
    }
}

void SettingsWindow::UnregisterCaptureHotkey()
{
    if (m_hotkeyRegistered)
    {
        UnregisterHotKey(m_hwnd, HOTKEY_CAPTURE_ID);
        m_hotkeyRegistered = false;
    }
}

void SettingsWindow::RegisterPauseHotkey()
{
    UnregisterPauseHotkey();
    if (m_config.pauseHotkeyVk == 0)
    {
        UpdateStatus(L"No pause hotkey configured.");
        return;
    }
    if (RegisterHotKey(m_hwnd, HOTKEY_PAUSE_ID, m_config.pauseHotkeyMod, m_config.pauseHotkeyVk))
    {
        m_pauseHotkeyRegistered = true;
        UpdateStatus(L"Pause Hotkey registered: " + HotkeyToString(m_config.pauseHotkeyVk, m_config.pauseHotkeyMod));
    }
    else
    {
        UpdateStatus(L"Failed to register pause hotkey: " + HotkeyToString(m_config.pauseHotkeyVk, m_config.pauseHotkeyMod));
    }
}

void SettingsWindow::UnregisterPauseHotkey()
{
    if (m_pauseHotkeyRegistered)
    {
        UnregisterHotKey(m_hwnd, HOTKEY_PAUSE_ID);
        m_pauseHotkeyRegistered = false;
    }
}

void SettingsWindow::OnHotkey(int id)
{
    if (!m_running) return;

    if (id == HOTKEY_CAPTURE_ID)
    {
        UpdateStatus(L"Hotkey pressed — capturing frame...");
        m_scheduler.TriggerOnce();
    }
    else if (id == HOTKEY_PAUSE_ID)
    {
        bool currentlyPaused = m_scheduler.IsPaused();
        m_scheduler.SetPaused(!currentlyPaused);
        if (!currentlyPaused)
        {
            UpdateStatus(L"Capture paused.");
            m_overlay.SetText(L"Suspended (Paused)");
        }
        else
        {
            UpdateStatus(L"Capture resumed.");
            m_overlay.SetText(L"Resuming...");
        }
    }
}

// -----------------------------------------------------------------------------
//  Command handlers
// -----------------------------------------------------------------------------

void SettingsWindow::OnStart()
{
    if (m_running) return;

    UIToConfig();

    if (!m_config.captureSet)
    {
        MessageBoxW(m_hwnd,
            L"Please select a capture region first.",
            L"No Region Set", MB_ICONWARNING);
        return;
    }

    // (Re-)initialise capture engine
    m_capture.Shutdown();
    if (!m_capture.Initialize(m_config.monitorIndex))
    {
        MessageBoxW(m_hwnd,
            (L"CaptureEngine init failed:\n" + m_capture.GetLastError()).c_str(),
            L"Error", MB_ICONERROR);
        return;
    }
    m_capture.SetCaptureRect(m_config.captureRect);
    m_capture.Start();

    // Hide capture helper, set overlay to translation mode
    m_captureHelper.Show(false);
    m_overlay.EnableDrag(false);

    // Configure translate client
    m_client.SetApiKey(m_config.apiKey);
    m_client.SetApiModel(m_config.apiModel);
    m_client.SetProvider(m_config.providerType);

    // Apply appearance to overlay
    m_overlay.SetFontName(m_config.fontName);
    m_overlay.SetFontSize(m_config.fontSize);
    m_overlay.SetTextColor(m_config.textColor);
    m_overlay.SetShadowColor(m_config.shadowColor);
    m_overlay.SetShadowEnabled(m_config.shadowEnabled);
    m_overlay.SetStrokeColor(m_config.strokeColor);
    m_overlay.SetStrokeEnabled(m_config.strokeEnabled);
    m_overlay.SetStrokeWidth(m_config.strokeWidth);
    m_overlay.SetPosition(m_config.overlayPos.x, m_config.overlayPos.y);
    m_overlay.SetSize(m_config.overlayWidth, m_config.overlayHeight);
    m_overlay.SetText(L"Starting\u2026");
    m_overlay.Show();

    // Start based on capture mode
    m_scheduler.SetComponents(&m_capture, &m_client, &m_overlay);

    if (m_config.captureMode == CaptureMode::Auto)
    {
        m_scheduler.Start(m_config.GetIntervalMs());
        RegisterPauseHotkey();
        UpdateStatus(L"Started (Auto mode, interval: " +
            std::to_wstring(m_config.captureIntervalMs) + L"ms).");
    }
    else
    {
        // Hotkey mode: start scheduler in paused/manual mode
        m_scheduler.Start(0);  // 0 = no auto timer
        RegisterCaptureHotkey();
        UpdateStatus(L"Started (Hotkey mode: " + HotkeyToString(m_config.hotkeyVk, m_config.hotkeyMod) + L").");
    }

    m_running = true;
    EnableWindow(GetDlgItem(m_hwnd, IDC_START_BTN), FALSE);
    EnableWindow(GetDlgItem(m_hwnd, IDC_STOP_BTN), TRUE);
}

void SettingsWindow::OnStop()
{
    if (!m_running) return;

    UnregisterCaptureHotkey();
    UnregisterPauseHotkey();
    m_scheduler.Stop();
    m_capture.Stop();

    m_running = false;
    EnableWindow(GetDlgItem(m_hwnd, IDC_START_BTN), TRUE);
    EnableWindow(GetDlgItem(m_hwnd, IDC_STOP_BTN), FALSE);
    UpdateStatus(L"Stopped.");

    SyncHelperWindows();
}

void SettingsWindow::OnSelectRegion()
{
    // Reset capture region to center of primary monitor
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    RECT rc = { (sw - 800) / 2, (sh - 150) / 2, (sw + 800) / 2, (sh + 150) / 2 };
    m_captureHelper.SetRect(rc);
    m_config.captureRect = rc;
    m_config.captureSet = true;
    UpdateRegionLabel();
    UpdateStatus(L"Capture region reset to screen center.");
}

void SettingsWindow::OnTestApi()
{
    UIToConfig();
    UpdateStatus(L"Testing connection...");

    TextTranslateProvider tc;
    tc.SetApiKey(m_config.apiKey);
    tc.SetApiModel(m_config.apiModel);
    tc.SetProvider(m_config.providerType);

    std::wstring result = tc.Translate(L"Hello world! This is a test connection message.");

    if (!result.empty())
        UpdateStatus(L"DeepSeek OK. Response: " + result);
    else
        UpdateStatus(L"DeepSeek error: " + tc.GetLastError());
}

void SettingsWindow::OnToggleDrag()
{
    bool newMode = !m_overlay.IsDragMode();
    m_overlay.EnableDrag(newMode);
    UpdateStatus(newMode ? L"Drag mode ON \u2014 drag the overlay, then click again to lock."
        : L"Drag mode OFF \u2014 overlay is click-through.");

    // Sync position after drag ends (or before entering drag mode)
    int ox, oy;
    m_overlay.GetPosition(ox, oy);
    m_config.overlayPos = { static_cast<LONG>(ox), static_cast<LONG>(oy) };
    wchar_t posBuf[64]{};
    swprintf(posBuf, 64, L"X: %d   Y: %d", ox, oy);
    SetDlgItemTextW(m_hwnd, IDC_OVERLAY_POS_LABEL, posBuf);
}

void SettingsWindow::OnProviderChanged()
{
    HWND hProv = GetDlgItem(m_hwnd, IDC_PROVIDER_COMBO);
    int idx = static_cast<int>(SendMessageW(hProv, CB_GETCURSEL, 0, 0));
    if (idx == 0)
        UpdateStatus(L"Provider: DeepSeek - s\u1EED d\u1EE5ng API c\u1EE7a DeepSeek qua HTTP REST.");
}

void SettingsWindow::OnSave()
{
    UIToConfig();
    m_config.Save(GetIniPath());
    UpdateStatus(L"Settings saved.");
}

void SettingsWindow::OnColorPick(COLORREF& colorRef)
{
    static COLORREF customColors[16]{};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = m_hwnd;
    cc.rgbResult = colorRef;
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc))
    {
        colorRef = cc.rgbResult;
        SyncHelperWindows();
    }
}

void SettingsWindow::OnTextColorPick()
{
    OnColorPick(m_config.textColor);
}

// -----------------------------------------------------------------------------
//  System tray
// -----------------------------------------------------------------------------

void SettingsWindow::AddTrayIcon()
{
    if (m_trayAdded) return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Game Translation Overlay");

    Shell_NotifyIconW(NIM_ADD, &nid);
    m_trayAdded = true;
}

void SettingsWindow::RemoveTrayIcon()
{
    if (!m_trayAdded) return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = ID_TRAY_ICON;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayAdded = false;
}

void SettingsWindow::ShowTrayMenu()
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Show Settings");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE_DRAG, L"Toggle Overlay Drag");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}

// -----------------------------------------------------------------------------
//  INI path helper
// -----------------------------------------------------------------------------

std::wstring SettingsWindow::GetIniPath() const
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    // Replace .exe extension with .ini
    wchar_t* dot = wcsrchr(path, L'.');
    if (dot) wcscpy_s(dot, 8, L".ini");
    return path;
}

void SettingsWindow::SyncHelperWindows()
{
    bool showHelpers = IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd);

    if (showHelpers && !m_running)
    {
        m_captureHelper.Show(true);
        m_overlay.EnableDrag(true);
        
        // Render preview text so user can see font styles
        m_overlay.SetFontName(m_config.fontName);
        m_overlay.SetFontSize(m_config.fontSize);
        m_overlay.SetTextColor(m_config.textColor);
        m_overlay.SetShadowColor(m_config.shadowColor);
        m_overlay.SetShadowEnabled(m_config.shadowEnabled);
        m_overlay.SetStrokeColor(m_config.strokeColor);
        m_overlay.SetStrokeEnabled(m_config.strokeEnabled);
        m_overlay.SetStrokeWidth(m_config.strokeWidth);
        m_overlay.SetPosition(m_config.overlayPos.x, m_config.overlayPos.y);
        m_overlay.SetSize(m_config.overlayWidth, m_config.overlayHeight);
        
        m_overlay.SetText(L"Game Translation Overlay (Preview)\nDrag borders to resize, drag center to move.");
        m_overlay.Show();
    }
    else
    {
        m_captureHelper.Show(false);
        if (!m_running)
        {
            m_overlay.Hide();
        }
    }
}
