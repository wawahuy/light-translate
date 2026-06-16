#include "src/ui/OverlayWindow.h"
#include "resource.h"
#include <objbase.h>   // MinGW-w64: phải include trước gdiplus để có PROPID
#include <gdiplus.h>
#include <cstring>
#include <algorithm>
#pragma comment(lib, "gdiplus.lib")

constexpr wchar_t OverlayWindow::CLASS_NAME[];

struct OverlayInPlaceData
{
    std::wstring text;
    std::vector<std::vector<Point2F>> boxes;
};

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

// ── Constructor / Destructor ──────────────────────────────────────────────────

OverlayWindow::OverlayWindow() = default;

OverlayWindow::~OverlayWindow()
{
    Destroy();
}

// ── Creation ──────────────────────────────────────────────────────────────────

bool OverlayWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;           // Layered window – no GDI background
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);                // OK to fail if already registered

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        CLASS_NAME, L"GameTranslate Overlay",
        WS_POPUP,
        m_posX, m_posY, m_width, m_height,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
    // Exclude the overlay window from screen capture to prevent capture feedback loops
    SetWindowDisplayAffinity(m_hwnd, WDA_EXCLUDEFROMCAPTURE);

    // Default bound rect = entire virtual desktop (all monitors)
    m_boundRect = {
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)
    };

    return true;
}

void OverlayWindow::Destroy()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void OverlayWindow::SetText(const std::wstring& text)
{
    if (!m_hwnd) return;

    // Safe cross-thread: marshal to UI thread via PostMessage.
    // Allocate a heap copy; the WndProc takes ownership and deletes it.
    wchar_t* buf = new wchar_t[text.size() + 1];
    std::wmemcpy(buf, text.c_str(), text.size() + 1);
    PostMessageW(m_hwnd, WM_OVERLAY_SETTEXT, 0, reinterpret_cast<LPARAM>(buf));
}

void OverlayWindow::SetInPlaceText(const std::wstring& text, const std::vector<std::vector<Point2F>>& boxes)
{
    if (!m_hwnd) return;
    OverlayInPlaceData* data = new OverlayInPlaceData{ text, boxes };
    PostMessageW(m_hwnd, WM_OVERLAY_SETINPLACE, 0, reinterpret_cast<LPARAM>(data));
}

void OverlayWindow::SetPosition(int x, int y)
{
    m_posX = x;
    m_posY = y;
    if (m_hwnd)
    {
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        if (IsVisible()) Redraw();
    }
}

void OverlayWindow::SetSize(int w, int h)
{
    m_width = w;
    m_height = h;
    if (m_hwnd)
    {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        if (IsVisible()) Redraw();
    }
}

void OverlayWindow::GetSize(int& w, int& h) const
{
    w = m_width;
    h = m_height;
}

void OverlayWindow::Show()
{
    if (m_hwnd)
    {
        Redraw();
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void OverlayWindow::Hide()
{
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_HIDE);
}

bool OverlayWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void OverlayWindow::EnableDrag(bool enable)
{
    if (!m_hwnd) return;
    m_dragMode = enable;

    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (enable)
    {
        ex &= ~WS_EX_TRANSPARENT;   // Accept mouse events
        SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
    }
    else
    {
        ex |= WS_EX_TRANSPARENT;    // Pass through all mouse events
    }
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);

    if (IsVisible()) Redraw();
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void OverlayWindow::PremultiplyAlpha(void* pvBits, int width, int height) noexcept
{
    // UpdateLayeredWindow with AC_SRC_ALPHA requires premultiplied alpha.
    auto* pixels = static_cast<UINT*>(pvBits);
    const int count = width * height;
    for (int i = 0; i < count; ++i)
    {
        UINT p = pixels[i];
        BYTE a = static_cast<BYTE>(p >> 24);
        if (a == 0)
        {
            pixels[i] = 0;
        }
        else if (a < 255)
        {
            BYTE r = static_cast<BYTE>((static_cast<BYTE>(p >> 16) * a + 127) / 255);
            BYTE g = static_cast<BYTE>((static_cast<BYTE>(p >>  8) * a + 127) / 255);
            BYTE b = static_cast<BYTE>((static_cast<BYTE>(p >>  0) * a + 127) / 255);
            pixels[i] = (static_cast<UINT>(a) << 24) |
                        (static_cast<UINT>(r) << 16) |
                        (static_cast<UINT>(g) <<  8) | b;
        }
    }
}

void OverlayWindow::Redraw()
{
    if (!m_hwnd) return;

    std::lock_guard<std::mutex> lock(m_renderMutex);

    HDC screenDC = GetDC(nullptr);
    HDC memDC    = CreateCompatibleDC(screenDC);

    // 32-bit top-down DIB for per-pixel alpha
    BITMAPINFOHEADER bih{};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = m_width;
    bih.biHeight      = -m_height;    // negative = top-down
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC,
        reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hBmp)
    {
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return;
    }

    HBITMAP hOldBmp = static_cast<HBITMAP>(SelectObject(memDC, hBmp));

    // Clear all pixels to transparent black
    std::memset(pvBits, 0, static_cast<size_t>(m_width) * m_height * 4);

    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        // Drag-mode: draw a semi-transparent dark background + border
        if (m_dragMode)
        {
            Gdiplus::SolidBrush bg(Gdiplus::Color(120, 0, 0, 0));
            g.FillRectangle(&bg, 0, 0, m_width, m_height);

            Gdiplus::Pen border(Gdiplus::Color(220, 0, 180, 255), 2.0f);
            border.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawRectangle(&border, 1.0f, 1.0f,
                            static_cast<float>(m_width - 2),
                            static_cast<float>(m_height - 2));
        }

        if (!m_text.empty())
        {
            Gdiplus::FontFamily family(m_fontName.c_str());
            Gdiplus::StringFormat fmt;
            Gdiplus::RectF layoutRect;
            float shadowOffsetX = 2.0f;
            float shadowOffsetY = 2.0f;

            if (!m_boxes.empty())
            {
                // In-place mode: compute collective bounding box
                float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
                bool hasPoints = false;
                for (const auto& box : m_boxes)
                {
                    for (const auto& pt : box)
                    {
                        if (pt.x < minX) minX = pt.x;
                        if (pt.y < minY) minY = pt.y;
                        if (pt.x > maxX) maxX = pt.x;
                        if (pt.y > maxY) maxY = pt.y;
                        hasPoints = true;
                    }
                }
                if (hasPoints)
                {
                    float w = maxX - minX;
                    float h = maxY - minY;

                    // Inflate the bounding box outward to ensure enough room for translation
                    float padX = std::max(w * 0.15f, 25.0f);
                    float padY = std::max(h * 0.15f, 12.0f);

                    float newMinX = std::max(0.0f, minX - padX);
                    float newMinY = std::max(0.0f, minY - padY);
                    float newMaxX = std::min(static_cast<float>(m_width), maxX + padX);
                    float newMaxY = std::min(static_cast<float>(m_height), maxY + padY);

                    layoutRect = Gdiplus::RectF(newMinX, newMinY, newMaxX - newMinX, newMaxY - newMinY);
                }
                else
                {
                    layoutRect = Gdiplus::RectF(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));
                }

                // Draw solid black background mask over the collective bounding box
                Gdiplus::SolidBrush blackBrush(Gdiplus::Color(255, 0, 0, 0));
                g.FillRectangle(&blackBrush, layoutRect);

                fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            }
            else
            {
                // Standard Overlay Mode
                const float margin = 10.0f;
                layoutRect = Gdiplus::RectF(
                    margin, margin,
                    static_cast<float>(m_width)  - margin * 2,
                    static_cast<float>(m_height) - margin * 2
                );
                fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                fmt.SetLineAlignment(Gdiplus::StringAlignmentFar);
            }

            // Build GraphicsPath to allow stroke (outline) rendering
            const int fontStyle = m_fontBold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
            Gdiplus::GraphicsPath path;
            path.AddString(
                m_text.c_str(),
                static_cast<INT>(m_text.size()),
                &family,
                fontStyle,
                static_cast<float>(m_fontSize),
                layoutRect,
                &fmt
            );

            // 1. Shadow: build shadow path with offset
            if (m_shadowEnabled)
            {
                Gdiplus::RectF shadowRect(
                    layoutRect.X + shadowOffsetX,
                    layoutRect.Y + shadowOffsetY,
                    layoutRect.Width,
                    layoutRect.Height
                );
                Gdiplus::GraphicsPath shadowPath;
                shadowPath.AddString(
                    m_text.c_str(),
                    static_cast<INT>(m_text.size()),
                    &family,
                    fontStyle,
                    static_cast<float>(m_fontSize),
                    shadowRect,
                    &fmt
                );
                Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(
                    180,
                    GetRValue(m_shadowColor),
                    GetGValue(m_shadowColor),
                    GetBValue(m_shadowColor)
                ));
                g.FillPath(&shadowBrush, &shadowPath);
            }

            // 2. Stroke (outline)
            if (m_strokeEnabled)
            {
                Gdiplus::Pen strokePen(Gdiplus::Color(
                    255,
                    GetRValue(m_strokeColor),
                    GetGValue(m_strokeColor),
                    GetBValue(m_strokeColor)
                ), m_strokeWidth);
                strokePen.SetLineJoin(Gdiplus::LineJoinRound);
                g.DrawPath(&strokePen, &path);
            }

            // 3. Fill text
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(
                255,
                GetRValue(m_textColor),
                GetGValue(m_textColor),
                GetBValue(m_textColor)
            ));
            g.FillPath(&textBrush, &path);
        }
    }

    // Premultiply alpha before passing to UpdateLayeredWindow
    PremultiplyAlpha(pvBits, m_width, m_height);

    BLENDFUNCTION blend{};
    blend.BlendOp             = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat         = AC_SRC_ALPHA;

    POINT ptSrc = { 0, 0 };
    POINT ptDst = { m_posX, m_posY };
    SIZE  szWnd = { m_width, m_height };

    UpdateLayeredWindow(m_hwnd, screenDC, &ptDst, &szWnd,
                        memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// ── Window procedure ──────────────────────────────────────────────────────────

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    OverlayWindow* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis)
        return pThis->HandleMessage(msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_OVERLAY_SETTEXT:
    {
        // Ownership of the heap string is transferred here
        auto* buf = reinterpret_cast<wchar_t*>(lParam);
        if (buf)
        {
            m_text = buf;
            delete[] buf;
        }
        m_boxes.clear();
        if (IsVisible()) Redraw();
        return 0;
    }

    case WM_OVERLAY_SETINPLACE:
    {
        auto* data = reinterpret_cast<OverlayInPlaceData*>(lParam);
        if (data)
        {
            m_text = std::move(data->text);
            m_boxes = std::move(data->boxes);
            delete data;
        }
        if (IsVisible()) Redraw();
        return 0;
    }

    case WM_PAINT:
    {
        // Content managed entirely by UpdateLayeredWindow; just validate
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   // Do nothing; avoid clearing our layered content

    case WM_NCHITTEST:
        if (m_dragMode)
        {
            POINT ptCursor;
            GetCursorPos(&ptCursor);
            return HitTestBorder(m_hwnd, ptCursor, 10);
        }
        return HTTRANSPARENT;   // Pass through all mouse events in normal mode

    case WM_MOVING:
    {
        // Clamp the proposed window rectangle within m_boundRect.
        // WM_MOVING fires continuously during drag; lParam = RECT* (screen coords).
        auto* pr = reinterpret_cast<RECT*>(lParam);
        const int w = pr->right  - pr->left;
        const int h = pr->bottom - pr->top;

        if (pr->left < m_boundRect.left)
        { pr->left = m_boundRect.left;  pr->right  = pr->left + w; }
        if (pr->top  < m_boundRect.top)
        { pr->top  = m_boundRect.top;   pr->bottom = pr->top  + h; }
        if (pr->right > m_boundRect.right)
        { pr->right  = m_boundRect.right;  pr->left = pr->right  - w; }
        if (pr->bottom > m_boundRect.bottom)
        { pr->bottom = m_boundRect.bottom; pr->top  = pr->bottom - h; }
        return TRUE;
    }

    case WM_MOVE:
    {
        RECT rc{};
        GetWindowRect(m_hwnd, &rc);
        m_posX = rc.left;
        m_posY = rc.top;
        if (OnMoved) OnMoved(m_posX, m_posY);
        return 0;
    }

    case WM_SIZE:
    {
        m_width = LOWORD(lParam);
        m_height = HIWORD(lParam);
        if (OnMoved) OnMoved(m_posX, m_posY);
        Redraw();
        return 0;
    }

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
