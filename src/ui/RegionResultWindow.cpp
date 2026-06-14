#include "src/ui/RegionResultWindow.h"
#include <windowsx.h>

#pragma comment(lib, "msimg32.lib")

constexpr wchar_t RegionResultWindow::CLASS_NAME[];
RegionResultWindow* RegionResultWindow::s_instance = nullptr;

// -----------------------------------------------------------------------------
//  Construction / Destruction
// -----------------------------------------------------------------------------

RegionResultWindow::RegionResultWindow() = default;

RegionResultWindow::~RegionResultWindow()
{
    Destroy();
}

// -----------------------------------------------------------------------------
//  Create / Destroy
// -----------------------------------------------------------------------------

bool RegionResultWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);
    return true;
}

void RegionResultWindow::Destroy()
{
    RemoveKeyHook();
    RemoveMouseHook();
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    s_instance = nullptr;
}

// -----------------------------------------------------------------------------
//  ShowResult — create the popup at the given screen rectangle
// -----------------------------------------------------------------------------

void RegionResultWindow::ShowResult(const RECT& region, const std::wstring& text)
{
    // Clean up any previous result window
    Hide();

    m_region = region;
    m_text   = text;

    int w = region.right - region.left;
    int h = region.bottom - region.top;

    // Minimum size
    if (w < 100) w = 100;
    if (h < 40)  h = 40;

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        CLASS_NAME,
        L"",
        WS_POPUP,
        region.left, region.top, w, h,
        nullptr, nullptr, m_hInstance, this);

    if (!m_hwnd) return;

    // Set semi-transparent background (black with 180/255 alpha)
    SetLayeredWindowAttributes(m_hwnd, 0, 200, LWA_ALPHA);

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(m_hwnd);

    // Install keyboard and mouse hooks to catch any key press or click
    s_instance = this;
    InstallKeyHook();
    InstallMouseHook();
}

// -----------------------------------------------------------------------------
//  Hide
// -----------------------------------------------------------------------------

void RegionResultWindow::Hide()
{
    RemoveKeyHook();
    RemoveMouseHook();
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool RegionResultWindow::IsVisible() const
{
    return m_hwnd != nullptr && IsWindowVisible(m_hwnd);
}

// -----------------------------------------------------------------------------
//  PaintResult — dark background with white text
// -----------------------------------------------------------------------------

void RegionResultWindow::PaintResult(HDC hdc)
{
    RECT rc{};
    GetClientRect(m_hwnd, &rc);

    // Dark background
    HBRUSH hBrush = CreateSolidBrush(RGB(20, 20, 20));
    FillRect(hdc, &rc, hBrush);
    DeleteObject(hBrush);

    // Draw text
    HFONT hFont = CreateFontW(
        m_fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, m_fontName.c_str());
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    RECT textRc = rc;
    textRc.left   += 8;
    textRc.top    += 6;
    textRc.right  -= 8;
    textRc.bottom -= 6;

    DrawTextW(hdc, m_text.c_str(), (int)m_text.size(), &textRc,
              DT_WORDBREAK | DT_LEFT | DT_TOP);

    // Calculate required height and resize if needed
    RECT calcRc = textRc;
    int textH = DrawTextW(hdc, m_text.c_str(), (int)m_text.size(), &calcRc,
                           DT_WORDBREAK | DT_LEFT | DT_TOP | DT_CALCRECT);

    int requiredH = textH + 12;  // 6px top + 6px bottom padding
    int currentH  = rc.bottom - rc.top;

    if (requiredH > currentH)
    {
        // Resize the window to fit the text
        SetWindowPos(m_hwnd, nullptr, 0, 0,
                     rc.right - rc.left, requiredH,
                     SWP_NOMOVE | SWP_NOZORDER);
    }

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// -----------------------------------------------------------------------------
//  Low-level keyboard hook — dismiss on any key
// -----------------------------------------------------------------------------

void RegionResultWindow::InstallKeyHook()
{
    if (m_keyHook) return;
    m_keyHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                   GetModuleHandleW(nullptr), 0);
}

void RegionResultWindow::RemoveKeyHook()
{
    if (m_keyHook)
    {
        UnhookWindowsHookEx(m_keyHook);
        m_keyHook = nullptr;
    }
}

LRESULT CALLBACK RegionResultWindow::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
    {
        if (s_instance && s_instance->m_hwnd)
        {
            // Post a message to hide on the UI thread
            PostMessageW(s_instance->m_hwnd, WM_CLOSE, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// -----------------------------------------------------------------------------
//  Low-level mouse hook — dismiss on any click
// -----------------------------------------------------------------------------

void RegionResultWindow::InstallMouseHook()
{
    if (m_mouseHook) return;
    m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc,
                                     GetModuleHandleW(nullptr), 0);
}

void RegionResultWindow::RemoveMouseHook()
{
    if (m_mouseHook)
    {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }
}

LRESULT CALLBACK RegionResultWindow::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN ||
            wParam == WM_NCLBUTTONDOWN || wParam == WM_NCRBUTTONDOWN || wParam == WM_NCMBUTTONDOWN)
        {
            if (s_instance && s_instance->m_hwnd)
            {
                PostMessageW(s_instance->m_hwnd, WM_CLOSE, 0, 0);
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// -----------------------------------------------------------------------------
//  Window procedure
// -----------------------------------------------------------------------------

LRESULT CALLBACK RegionResultWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RegionResultWindow* pThis = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<RegionResultWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = reinterpret_cast<RegionResultWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT RegionResultWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(m_hwnd, &ps);
        PaintResult(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
    {
        Hide();
        return 0;
    }

    case WM_DESTROY:
    {
        RemoveKeyHook();
        m_hwnd = nullptr;
        return 0;
    }
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
