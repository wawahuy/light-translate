#include "src/ui/CaptureHelperWindow.h"
#include <windowsx.h>
#include <algorithm>

static LRESULT HitTestBorder(HWND hwnd, POINT ptCursor, int borderThickness = 10)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);

    bool left = ptCursor.x < (rc.left + borderThickness);
    bool right = ptCursor.x >= (rc.right - borderThickness);
    bool top = ptCursor.y < (rc.top + borderThickness);
    bool bottom = ptCursor.y >= (rc.bottom - borderThickness);

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;

    return HTCAPTION; // Drag by the center area
}

CaptureHelperWindow::CaptureHelperWindow() = default;
CaptureHelperWindow::~CaptureHelperWindow()
{
    Destroy();
}

bool CaptureHelperWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_SIZEALL);
    wc.hbrBackground = nullptr; // managed by WM_ERASEBKGND/WM_PAINT
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    // Initial position
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 800, h = 150;

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME, L"Capture Region Helper",
        WS_POPUP,
        (sw - w) / 2, (sh - h) / 2, w, h,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    // Set transparency (opacity 140/255)
    SetLayeredWindowAttributes(m_hwnd, 0, 140, LWA_ALPHA);

    return true;
}

void CaptureHelperWindow::Destroy()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void CaptureHelperWindow::Show(bool show)
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (show) InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

bool CaptureHelperWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

RECT CaptureHelperWindow::GetRect() const
{
    RECT rc{};
    if (m_hwnd)
    {
        GetWindowRect(m_hwnd, &rc);
    }
    return rc;
}

void CaptureHelperWindow::SetRect(const RECT& rc)
{
    if (m_hwnd)
    {
        SetWindowPos(m_hwnd, HWND_TOPMOST, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

LRESULT CALLBACK CaptureHelperWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CaptureHelperWindow* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<CaptureHelperWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = reinterpret_cast<CaptureHelperWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CaptureHelperWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCHITTEST:
    {
        POINT ptCursor;
        GetCursorPos(&ptCursor);
        return HitTestBorder(m_hwnd, ptCursor, 10);
    }

    case WM_SETCURSOR:
        return DefWindowProcW(m_hwnd, msg, wParam, lParam);

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);

        RECT rc;
        GetClientRect(m_hwnd, &rc);

        // Fill background with a dark gray color
        HBRUSH bgBrush = CreateSolidBrush(RGB(20, 20, 20));
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        // Draw solid green border (4px)
        HPEN borderPen = CreatePen(PS_SOLID, 4, RGB(0, 255, 0));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));

        Rectangle(hdc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);

        // Draw overlay text label
        SetTextColor(hdc, RGB(0, 255, 0));
        SetBkMode(hdc, TRANSPARENT);
        
        HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, hFont));

        const wchar_t* title = L"Capture Region (Adjustable)";
        DrawTextW(hdc, title, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, oldFont);
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_MOVE:
    case WM_SIZE:
    {
        if (OnRectChanged)
        {
            RECT rc{};
            GetWindowRect(m_hwnd, &rc);
            OnRectChanged(rc);
        }
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
