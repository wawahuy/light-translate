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
//  CreateControls - layout
// -----------------------------------------------------------------------------

void SettingsWindow::CreateControls()
{
    const int M = 10;   // margin
    const int LH = 22;   // label height
    const int EH = 24;   // edit height
    const int BH = 28;   // button height
    const int W = WND_W - M * 2 - 10;  // usable width

    int y = M;

    // -- API Settings ----------------------------------------------------------
    MakeGroup(M, y, W, 132, L"  API Settings  ");
    y += 20;
    MakeLabel(M + 8, y, 75, LH, L"Provider:");
    {
        HWND hProv = MakeCombo(M + 85, y, 160, 80, IDC_PROVIDER_COMBO);
        SendMessageW(hProv, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DeepSeek"));
        SendMessageW(hProv, CB_SETCURSEL, 0, 0);
    }
    y += EH + 8;
    MakeLabel(M + 8, y, 75, LH, L"API Model:");
    MakeEdit(M + 85, y, 160, EH, IDC_API_MODEL_EDIT);
    y += EH + 8;
    MakeLabel(M + 8, y, 75, LH, L"API Key:");
    MakeEdit(M + 85, y, W - 95, EH, IDC_API_KEY_EDIT);
    y += EH + 8;
    MakeButton(M + 8, y, 120, BH, L"Test Connection", IDC_TEST_API_BTN);
    y += BH + 14;

    // -- Capture Settings ------------------------------------------------------
    MakeGroup(M, y, W, 120, L"  Capture Settings  ");
    y += 20;
    MakeLabel(M + 8, y, 70, LH, L"Monitor:");
    HWND hMonitor = MakeCombo(M + 80, y, 120, 140, IDC_MONITOR_COMBO);
    SendMessageW(hMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"0 - Primary"));
    SendMessageW(hMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 - Second"));
    SendMessageW(hMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2 - Third"));
    SendMessageW(hMonitor, CB_SETCURSEL, 0, 0);
    y += EH + 8;
    MakeButton(M + 8, y, 170, BH, L"Reset Capture Region", IDC_SELECT_REGION);
    // Region info label - needs an ID so we can update it via SetDlgItemText
    CreateWindowExW(0, L"STATIC", L"(not set)",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        M + 185, y + 4, W - 192, LH, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_REGION_INFO)),
        m_hInstance, nullptr);
    y += BH + 8;
    MakeLabel(M + 8, y, 100, LH, L"Frames/Second:");
    HWND hFps = MakeCombo(M + 110, y, 100, 160, IDC_FPM_COMBO);
    for (const wchar_t* v : { L"0.5", L"1", L"2", L"4" })
        SendMessageW(hFps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v));
    SendMessageW(hFps, CB_SETCURSEL, 2, 0);  // default "2"
    y += EH + 14;

    // -- Overlay & Typography --------------------------------------------------
    MakeGroup(M, y, W, 230, L"  Overlay & Typography  ");
    y += 20;
    MakeLabel(M + 8, y, 72, LH, L"Position:");
    // Read-only label - updated automatically when overlay is moved
    CreateWindowExW(0, L"STATIC", L"-",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        M + 82, y, W - 88, LH, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_OVERLAY_POS_LABEL)),
        m_hInstance, nullptr);
    y += EH + 8;
    MakeLabel(M + 8, y, 60, LH, L"Font:");
    MakeEdit(M + 70, y, 180, EH, IDC_FONT_NAME_EDIT);
    MakeLabel(M + 260, y, 40, LH, L"Size:");
    MakeEdit(M + 302, y, 55, EH, IDC_FONT_SIZE_EDIT);
    y += EH + 10;
    MakeLabel(M + 8, y, 80, LH, L"Text Color:");
    MakeButton(M + 90, y, 90, BH, L"Choose…", IDC_TEXT_COLOR_BTN);
    y += BH + 6;
    MakeCheck(M + 8, y, 80, LH, L"Shadow", IDC_SHADOW_CHECK);
    MakeButton(M + 90, y, 90, BH, L"Color…", IDC_SHADOW_COLOR_BTN);
    y += BH + 6;
    MakeCheck(M + 8, y, 80, LH, L"Stroke", IDC_STROKE_CHECK);
    MakeButton(M + 90, y, 90, BH, L"Color…", IDC_STROKE_COLOR_BTN);
    y += BH + 8;
    MakeButton(M + 8, y, 120, BH, L"Save Settings", IDC_SAVE_BTN);
    y += BH + 14;

    // -- Status log ------------------------------------------------------------
    MakeGroup(M, y, W, 175, L"  Status  ");
    y += 18;
    MakeEdit(M + 8, y, W - 16, 145, IDC_STATUS_EDIT, true);
    y += 150;

    // -- Start / Stop ----------------------------------------------------------
    y += 10;
    MakeButton(M, y, 120, 36, L"▶  START", IDC_START_BTN);
    MakeButton(WND_W - 140, y, 120, 36, L"■  STOP", IDC_STOP_BTN);
    EnableWindow(GetDlgItem(m_hwnd, IDC_STOP_BTN), FALSE);

    // Default font for all controls
    HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    EnumChildWindows(m_hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, lp, TRUE);
        return TRUE;
        }, reinterpret_cast<LPARAM>(hFont));
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

    // FPS combo
    const double fpsValues[] = { 0.5, 1.0, 2.0, 4.0 };
    HWND hFps = GetDlgItem(m_hwnd, IDC_FPM_COMBO);
    for (int i = 0; i < 4; ++i)
        if (fpsValues[i] == m_config.framesPerSecond)
        {
            SendMessageW(hFps, CB_SETCURSEL, i, 0); break;
        }

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

    const double fpsValues[] = { 0.5, 1.0, 2.0, 4.0 };
    HWND hFps = GetDlgItem(m_hwnd, IDC_FPM_COMBO);
    int fpsIdx = static_cast<int>(SendMessageW(hFps, CB_GETCURSEL, 0, 0));
    if (fpsIdx >= 0 && fpsIdx < 4) m_config.framesPerSecond = fpsValues[fpsIdx];

    // Position is read directly from the overlay window (set by drag)
    int ox, oy;
    m_overlay.GetPosition(ox, oy);
    m_config.overlayPos = { static_cast<LONG>(ox), static_cast<LONG>(oy) };

    BOOL ok{};
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
    m_overlay.SetText(L"Starting…");
    m_overlay.Show();

    // Start scheduler
    m_scheduler.SetComponents(&m_capture, &m_client, &m_overlay);
    m_scheduler.Start(m_config.GetIntervalMs());

    m_running = true;
    EnableWindow(GetDlgItem(m_hwnd, IDC_START_BTN), FALSE);
    EnableWindow(GetDlgItem(m_hwnd, IDC_STOP_BTN), TRUE);
    UpdateStatus(L"Started.");
}

void SettingsWindow::OnStop()
{
    if (!m_running) return;

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
    UpdateStatus(newMode ? L"Drag mode ON — drag the overlay, then click again to lock."
        : L"Drag mode OFF — overlay is click-through.");

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
        UpdateStatus(L"Provider: DeepSeek - sử dụng API của DeepSeek qua HTTP REST.");
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
