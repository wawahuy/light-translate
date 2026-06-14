#include "src/ui/RegionSelectWindow.h"
#include <windowsx.h>
#include <algorithm>

constexpr wchar_t RegionSelectWindow::CLASS_NAME[];

// -----------------------------------------------------------------------------
//  Construction / Destruction
// -----------------------------------------------------------------------------

RegionSelectWindow::RegionSelectWindow() = default;

RegionSelectWindow::~RegionSelectWindow()
{
    Destroy();
}

// -----------------------------------------------------------------------------
//  Create / Destroy
// -----------------------------------------------------------------------------

bool RegionSelectWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_CROSS);
    wc.hbrBackground = nullptr;  // we paint everything ourselves
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);
    return true;
}

void RegionSelectWindow::Destroy()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_screenBmp)
    {
        DeleteObject(m_screenBmp);
        m_screenBmp = nullptr;
    }
}

// -----------------------------------------------------------------------------
//  Show — capture screen then create fullscreen popup
// -----------------------------------------------------------------------------

void RegionSelectWindow::Show()
{
    // Capture screen before showing the overlay
    CaptureScreen();

    m_selecting = false;
    m_startPt   = {};
    m_currentPt = {};

    // Virtual screen bounds (multi-monitor)
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME,
        L"",
        WS_POPUP,
        vx, vy, vw, vh,
        nullptr, nullptr, m_hInstance, this);

    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetCapture(m_hwnd);
    }
}

// -----------------------------------------------------------------------------
//  CaptureScreen — take a GDI screenshot of the entire virtual screen
// -----------------------------------------------------------------------------

void RegionSelectWindow::CaptureScreen()
{
    if (m_screenBmp)
    {
        DeleteObject(m_screenBmp);
        m_screenBmp = nullptr;
    }

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hScreen = GetDC(nullptr);
    HDC hMem    = CreateCompatibleDC(hScreen);
    m_screenBmp = CreateCompatibleBitmap(hScreen, m_screenW, m_screenH);

    HGDIOBJ hOld = SelectObject(hMem, m_screenBmp);
    BitBlt(hMem, 0, 0, m_screenW, m_screenH, hScreen, vx, vy, SRCCOPY);
    SelectObject(hMem, hOld);

    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);
}

// -----------------------------------------------------------------------------
//  DrawOverlay — dimmed screen + clear selection rectangle
// -----------------------------------------------------------------------------

void RegionSelectWindow::DrawOverlay(HDC hdc)
{
    RECT clientRc{};
    GetClientRect(m_hwnd, &clientRc);
    int cw = clientRc.right;
    int ch = clientRc.bottom;

    // Create back buffer
    HDC hMemDC = CreateCompatibleDC(hdc);
    HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ hOldBmp = SelectObject(hMemDC, hMemBmp);

    // Draw the captured screen
    HDC hSrcDC = CreateCompatibleDC(hdc);
    HGDIOBJ hOldSrc = SelectObject(hSrcDC, m_screenBmp);
    BitBlt(hMemDC, 0, 0, cw, ch, hSrcDC, 0, 0, SRCCOPY);
    SelectObject(hSrcDC, hOldSrc);
    DeleteDC(hSrcDC);

    // Create a dim overlay (semi-transparent black)
    HDC hDimDC = CreateCompatibleDC(hdc);
    HBITMAP hDimBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ hOldDim = SelectObject(hDimDC, hDimBmp);

    // Fill with black
    RECT fillRc = { 0, 0, cw, ch };
    HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hDimDC, &fillRc, hBlack);
    DeleteObject(hBlack);

    // Alpha blend the dim layer
    BLENDFUNCTION bf{};
    bf.BlendOp             = AC_SRC_OVER;
    bf.SourceConstantAlpha = 120;  // ~47% opacity
    bf.AlphaFormat         = 0;
    AlphaBlend(hMemDC, 0, 0, cw, ch, hDimDC, 0, 0, cw, ch, bf);

    SelectObject(hDimDC, hOldDim);
    DeleteObject(hDimBmp);
    DeleteDC(hDimDC);

    // If selecting, draw the clear (undimmed) region
    if (m_selecting)
    {
        RECT selRc;
        selRc.left   = std::min(m_startPt.x, m_currentPt.x);
        selRc.top    = std::min(m_startPt.y, m_currentPt.y);
        selRc.right  = std::max(m_startPt.x, m_currentPt.x);
        selRc.bottom = std::max(m_startPt.y, m_currentPt.y);

        // Restore the original screenshot in the selected area
        HDC hSrcDC2 = CreateCompatibleDC(hdc);
        HGDIOBJ hOldSrc2 = SelectObject(hSrcDC2, m_screenBmp);

        int sw = selRc.right - selRc.left;
        int sh = selRc.bottom - selRc.top;
        if (sw > 0 && sh > 0)
        {
            BitBlt(hMemDC, selRc.left, selRc.top, sw, sh,
                   hSrcDC2, selRc.left, selRc.top, SRCCOPY);
        }
        SelectObject(hSrcDC2, hOldSrc2);
        DeleteDC(hSrcDC2);

        // Draw selection border (white dashed)
        HPEN hPen = CreatePen(PS_DASH, 2, RGB(0, 180, 255));
        HGDIOBJ hOldPen = SelectObject(hMemDC, hPen);
        HGDIOBJ hOldBr  = SelectObject(hMemDC, GetStockObject(NULL_BRUSH));

        Rectangle(hMemDC, selRc.left, selRc.top, selRc.right, selRc.bottom);

        SelectObject(hMemDC, hOldBr);
        SelectObject(hMemDC, hOldPen);
        DeleteObject(hPen);

        // Draw size info text
        wchar_t info[64]{};
        swprintf_s(info, 64, L"%d × %d", sw, sh);
        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, RGB(0, 180, 255));

        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ hOldFont = SelectObject(hMemDC, hFont);

        TextOutW(hMemDC, selRc.left + 4, selRc.top - 20, info, (int)wcslen(info));

        SelectObject(hMemDC, hOldFont);
        DeleteObject(hFont);
    }

    // Flip to screen
    BitBlt(hdc, 0, 0, cw, ch, hMemDC, 0, 0, SRCCOPY);

    SelectObject(hMemDC, hOldBmp);
    DeleteObject(hMemBmp);
    DeleteDC(hMemDC);
}

// -----------------------------------------------------------------------------
//  Window procedure
// -----------------------------------------------------------------------------

LRESULT CALLBACK RegionSelectWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RegionSelectWindow* pThis = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<RegionSelectWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = reinterpret_cast<RegionSelectWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT RegionSelectWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(m_hwnd, &ps);
        DrawOverlay(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        m_selecting = true;
        m_startPt.x = GET_X_LPARAM(lParam);
        m_startPt.y = GET_Y_LPARAM(lParam);
        m_currentPt = m_startPt;
        SetCapture(m_hwnd);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (m_selecting)
        {
            m_currentPt.x = GET_X_LPARAM(lParam);
            m_currentPt.y = GET_Y_LPARAM(lParam);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (m_selecting)
        {
            m_selecting = false;
            ReleaseCapture();

            m_currentPt.x = GET_X_LPARAM(lParam);
            m_currentPt.y = GET_Y_LPARAM(lParam);

            RECT selRc;
            selRc.left   = std::min(m_startPt.x, m_currentPt.x);
            selRc.top    = std::min(m_startPt.y, m_currentPt.y);
            selRc.right  = std::max(m_startPt.x, m_currentPt.x);
            selRc.bottom = std::max(m_startPt.y, m_currentPt.y);

            int sw = selRc.right - selRc.left;
            int sh = selRc.bottom - selRc.top;

            // Minimum selection size (5x5)
            if (sw > 5 && sh > 5)
            {
                // Convert client coords to screen coords
                int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
                RECT screenRc;
                screenRc.left   = selRc.left + vx;
                screenRc.top    = selRc.top + vy;
                screenRc.right  = selRc.right + vx;
                screenRc.bottom = selRc.bottom + vy;

                // Hide the selection window first
                ShowWindow(m_hwnd, SW_HIDE);
                DestroyWindow(m_hwnd);
                m_hwnd = nullptr;

                if (OnRegionSelected)
                    OnRegionSelected(screenRc);
            }
            else
            {
                // Too small — cancel
                ShowWindow(m_hwnd, SW_HIDE);
                DestroyWindow(m_hwnd);
                m_hwnd = nullptr;
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            ReleaseCapture();
            ShowWindow(m_hwnd, SW_HIDE);
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        return 0;
    }

    case WM_DESTROY:
    {
        m_hwnd = nullptr;
        return 0;
    }
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
