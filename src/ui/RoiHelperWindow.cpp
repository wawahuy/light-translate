#include "src/ui/RoiHelperWindow.h"
#include <windowsx.h>
#include <algorithm>

constexpr wchar_t RoiHelperWindow::CLASS_NAME[];

RoiHelperWindow::RoiHelperWindow() = default;

RoiHelperWindow::~RoiHelperWindow()
{
    Destroy();
}

bool RoiHelperWindow::Create(HWND parentHwnd, HINSTANCE hInstance)
{
    m_parentHwnd = parentHwnd;
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // managed by WM_ERASEBKGND/WM_PAINT
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc); // Ok to fail if already registered

    // Get parent dimensions to center
    RECT parentRc;
    GetClientRect(parentHwnd, &parentRc);
    int pW = parentRc.right - parentRc.left;
    int pH = parentRc.bottom - parentRc.top;

    int myH = std::min(pH - 4, (pW - 4) / 2);
    if (myH < 15) myH = 15;
    int myW = myH * 2;

    int x = (pW - myW) / 2;
    int y = (pH - myH) / 2;

    m_hwnd = CreateWindowExW(
        0,
        CLASS_NAME, L"ROI Selection Window",
        WS_CHILD | WS_CLIPSIBLINGS,
        x, y, myW, myH,
        parentHwnd, nullptr, hInstance, this
    );

    if (m_hwnd)
    {
        m_parentW = pW;
        m_parentH = pH;
    }

    return m_hwnd != nullptr;
}

void RoiHelperWindow::Destroy()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void RoiHelperWindow::Show(bool show)
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, show ? SW_SHOW : SW_HIDE);
        if (show)
        {
            EnsureWithinBounds();
            InvalidateRect(m_hwnd, nullptr, TRUE);
        }
    }
}

bool RoiHelperWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

RECT RoiHelperWindow::GetRect() const
{
    RECT rc{};
    if (m_hwnd)
    {
        GetWindowRect(m_hwnd, &rc);
        // Convert screen coordinates to parent client coordinates
        POINT ptTL = { rc.left, rc.top };
        POINT ptBR = { rc.right, rc.bottom };
        ScreenToClient(m_parentHwnd, &ptTL);
        ScreenToClient(m_parentHwnd, &ptBR);
        rc.left = ptTL.x;
        rc.top = ptTL.y;
        rc.right = ptBR.x;
        rc.bottom = ptBR.y;
    }
    return rc;
}

void RoiHelperWindow::SetRect(const RECT& rc)
{
    if (m_hwnd)
    {
        MoveWindow(m_hwnd, rc.left, rc.top,
                   rc.right - rc.left, rc.bottom - rc.top, TRUE);
        EnsureWithinBounds();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void RoiHelperWindow::CenterAndReset()
{
    if (!m_hwnd || !m_parentHwnd) return;

    RECT parentRc;
    GetClientRect(m_parentHwnd, &parentRc);
    int pW = parentRc.right - parentRc.left;
    int pH = parentRc.bottom - parentRc.top;

    int myH = std::min(pH - 4, (pW - 4) / 2);
    if (myH < 15) myH = 15;
    int myW = myH * 2;

    int myX = (pW - myW) / 2;
    int myY = (pH - myH) / 2;

    MoveWindow(m_hwnd, myX, myY, myW, myH, TRUE);
    InvalidateRect(m_parentHwnd, nullptr, TRUE);

    if (OnRectChanged)
    {
        RECT rc = { myX, myY, myX + myW, myY + myH };
        OnRectChanged(rc);
    }
}

void RoiHelperWindow::EnsureWithinBounds()
{
    if (!m_hwnd || !m_parentHwnd) return;

    RECT parentRc;
    GetClientRect(m_parentHwnd, &parentRc);
    int pW = parentRc.right - parentRc.left;
    int pH = parentRc.bottom - parentRc.top;

    RECT myRc;
    GetWindowRect(m_hwnd, &myRc);
    POINT ptTL = { myRc.left, myRc.top };
    POINT ptBR = { myRc.right, myRc.bottom };
    ScreenToClient(m_parentHwnd, &ptTL);
    ScreenToClient(m_parentHwnd, &ptBR);

    int myW = ptBR.x - ptTL.x;
    int myH = ptBR.y - ptTL.y;
    int myX = ptTL.x;
    int myY = ptTL.y;

    // Clamp size (2px padding, min 30x30 size)
    if (myW > pW - 4) myW = pW - 4;
    if (myH > pH - 4) myH = pH - 4;
    if (myW < 30) myW = 30;
    if (myH < 30) myH = 30;

    // Clamp position with 2px padding
    if (myX < 2) myX = 2;
    if (myY < 2) myY = 2;
    if (myX + myW > pW - 2) myX = pW - 2 - myW;
    if (myY + myH > pH - 2) myY = pH - 2 - myH;

    MoveWindow(m_hwnd, myX, myY, myW, myH, TRUE);
    InvalidateRect(m_parentHwnd, nullptr, TRUE);
}

void RoiHelperWindow::OnParentResize(int newPW, int newPH)
{
    if (!m_hwnd || newPW <= 0 || newPH <= 0) return;

    m_parentW = newPW;
    m_parentH = newPH;
    CenterAndReset();
}

LRESULT CALLBACK RoiHelperWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RoiHelperWindow* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<RoiHelperWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = reinterpret_cast<RoiHelperWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT RoiHelperWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(m_hwnd, &pt);

        RECT rc;
        GetClientRect(m_hwnd, &rc);

        int border = 8;
        bool left = pt.x < border;
        bool right = pt.x > rc.right - border;
        bool top = pt.y < border;
        bool bottom = pt.y > rc.bottom - border;

        if ((top && left) || (bottom && right))
            SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
        else if ((top && right) || (bottom && left))
            SetCursor(LoadCursor(nullptr, IDC_SIZENESW));
        else if (left || right)
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
        else if (top || bottom)
            SetCursor(LoadCursor(nullptr, IDC_SIZENS));
        else
            SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
        return TRUE;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        int border = 8;
        m_dragType = DragType::MOVE;
        if (pt.x < border)
        {
            if (pt.y < border) m_dragType = DragType::RESIZE_TL;
            else if (pt.y > rc.bottom - border) m_dragType = DragType::RESIZE_BL;
            else m_dragType = DragType::RESIZE_L;
        }
        else if (pt.x > rc.right - border)
        {
            if (pt.y < border) m_dragType = DragType::RESIZE_TR;
            else if (pt.y > rc.bottom - border) m_dragType = DragType::RESIZE_BR;
            else m_dragType = DragType::RESIZE_R;
        }
        else if (pt.y < border)
        {
            m_dragType = DragType::RESIZE_T;
        }
        else if (pt.y > rc.bottom - border)
        {
            m_dragType = DragType::RESIZE_B;
        }

        GetCursorPos(&m_startMouse);
        GetWindowRect(m_hwnd, &m_startRect);
        POINT ptTL = { m_startRect.left, m_startRect.top };
        POINT ptBR = { m_startRect.right, m_startRect.bottom };
        ScreenToClient(m_parentHwnd, &ptTL);
        ScreenToClient(m_parentHwnd, &ptBR);
        m_startRect.left = ptTL.x;
        m_startRect.top = ptTL.y;
        m_startRect.right = ptBR.x;
        m_startRect.bottom = ptBR.y;

        SetCapture(m_hwnd);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (GetCapture() == m_hwnd && m_dragType != DragType::NONE)
        {
            POINT pt;
            GetCursorPos(&pt);
            int dx = pt.x - m_startMouse.x;
            int dy = pt.y - m_startMouse.y;

            RECT parentRc;
            GetClientRect(m_parentHwnd, &parentRc);
            int pW = parentRc.right - parentRc.left;
            int pH = parentRc.bottom - parentRc.top;

            int minX = 2;
            int minY = 2;
            int maxX = pW - 2;
            int maxY = pH - 2;

            RECT newRect = m_startRect;
            int w = m_startRect.right - m_startRect.left;
            int h = m_startRect.bottom - m_startRect.top;

            if (m_dragType == DragType::MOVE)
            {
                newRect.left = m_startRect.left + dx;
                newRect.top = m_startRect.top + dy;
                newRect.right = newRect.left + w;
                newRect.bottom = newRect.top + h;

                if (newRect.left < minX)
                {
                    newRect.left = minX;
                    newRect.right = newRect.left + w;
                }
                if (newRect.top < minY)
                {
                    newRect.top = minY;
                    newRect.bottom = newRect.top + h;
                }
                if (newRect.right > maxX)
                {
                    newRect.right = maxX;
                    newRect.left = newRect.right - w;
                }
                if (newRect.bottom > maxY)
                {
                    newRect.bottom = maxY;
                    newRect.top = newRect.bottom - h;
                }
            }
            else
            {
                if (m_dragType == DragType::RESIZE_L || m_dragType == DragType::RESIZE_TL || m_dragType == DragType::RESIZE_BL)
                {
                    newRect.left = m_startRect.left + dx;
                    if (newRect.left < minX) newRect.left = minX;
                    if (newRect.right - newRect.left < 30) newRect.left = newRect.right - 30;
                }
                if (m_dragType == DragType::RESIZE_R || m_dragType == DragType::RESIZE_TR || m_dragType == DragType::RESIZE_BR)
                {
                    newRect.right = m_startRect.right + dx;
                    if (newRect.right > maxX) newRect.right = maxX;
                    if (newRect.right - newRect.left < 30) newRect.right = newRect.left + 30;
                }
                if (m_dragType == DragType::RESIZE_T || m_dragType == DragType::RESIZE_TL || m_dragType == DragType::RESIZE_TR)
                {
                    newRect.top = m_startRect.top + dy;
                    if (newRect.top < minY) newRect.top = minY;
                    if (newRect.bottom - newRect.top < 30) newRect.top = newRect.bottom - 30;
                }
                if (m_dragType == DragType::RESIZE_B || m_dragType == DragType::RESIZE_BL || m_dragType == DragType::RESIZE_BR)
                {
                    newRect.bottom = m_startRect.bottom + dy;
                    if (newRect.bottom > maxY) newRect.bottom = maxY;
                    if (newRect.bottom - newRect.top < 30) newRect.bottom = newRect.top + 30;
                }
            }

            MoveWindow(m_hwnd, newRect.left, newRect.top,
                       newRect.right - newRect.left, newRect.bottom - newRect.top, TRUE);
            InvalidateRect(m_parentHwnd, nullptr, TRUE);

            if (OnRectChanged)
            {
                OnRectChanged(newRect);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (GetCapture() == m_hwnd)
        {
            ReleaseCapture();
            m_dragType = DragType::NONE;
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        // Draw red dashed border (2px)
        HPEN borderPen = CreatePen(PS_DASH, 2, RGB(255, 0, 0));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));

        Rectangle(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);

        // Draw text label
        SetTextColor(hdc, RGB(255, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, hFont));

        const wchar_t* label = L"ROI Detection Area";
        RECT textRc = rc;
        textRc.top += 8;
        DrawTextW(hdc, label, -1, &textRc, DT_CENTER | DT_TOP | DT_SINGLELINE);

        SelectObject(hdc, oldFont);
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
