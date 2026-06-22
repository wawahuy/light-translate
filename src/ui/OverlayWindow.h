#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <functional>
#include <mutex>
#include <vector>
#include "src/ui/ITranslationOutput.h"

/// Always-on-top, click-through, transparent overlay window.
///
/// Rendering pipeline:
///   1. Create a 32-bit ARGB DIB section (per-pixel alpha).
///   2. Draw text with optional shadow + stroke using GDI+.
///   3. Premultiply alpha (required by UpdateLayeredWindow).
///   4. Call UpdateLayeredWindow — no WM_PAINT redraws ever touch the window.
///
/// Thread-safety:
///   SetText() may be called from any thread; it posts WM_OVERLAY_SETTEXT to
///   the HWND so the actual drawing stays on the UI thread.
class OverlayWindow : public ITranslationOutput
{
public:
    OverlayWindow();
    ~OverlayWindow() override;

    bool Create(HINSTANCE hInstance);
    void Destroy();

    // Set the displayed text.  Safe to call from any thread.
    void SetText(const std::wstring& text) override;

    // Set text and detected text regions for in-place rendering. Safe from any thread.
    void SetInPlaceText(const std::wstring& text, const std::vector<std::vector<Point2F>>& boxes) override;

    const std::wstring& GetText() const { return m_text; }

    /// Move the overlay to (x, y) in screen coordinates.
    void SetPosition(int x, int y) override;
    void GetPosition(int& x, int& y) const { x = m_posX; y = m_posY; }
    void SetSize(int w, int h) override;
    void GetSize(int& w, int& h) const;

    /// Constrain dragging to this screen rectangle (default = virtual screen).
    void SetBoundRect(const RECT& rc) noexcept { m_boundRect = rc; }
    RECT GetBoundRect()               const noexcept { return m_boundRect; }

    void Show() override;
    void Hide() override;
    bool IsVisible() const override;

    /// Toggle drag mode: removes WS_EX_TRANSPARENT so the user can drag.
    void EnableDrag(bool enable);
    bool IsDragMode() const { return m_dragMode; }

    // ── Text appearance setters (call then Redraw if visible) ─────────────────
    void SetFontName     (const std::wstring& n) { m_fontName = n; }
    void SetFontSize     (int  s)                { m_fontSize = s; }
    void SetFontBold     (bool b)                { m_fontBold = b; }
    void SetTextColor    (COLORREF c)            { m_textColor = c; }
    void SetShadowColor  (COLORREF c)            { m_shadowColor = c; }
    void SetStrokeColor  (COLORREF c)            { m_strokeColor = c; }
    void SetShadowEnabled(bool e)                { m_shadowEnabled = e; }
    void SetStrokeEnabled(bool e)                { m_strokeEnabled = e; }
    void SetStrokeWidth  (float w)               { m_strokeWidth = w; }

    /// Force a full repaint (call after changing appearance settings).
    void Redraw();

    /// Called (on the UI thread) when the user drags the overlay.
    std::function<void(int x, int y)> OnMoved;

    HWND GetHWND() const { return m_hwnd; }

private:
    static void PremultiplyAlpha(void* pvBits, int width, int height, const RECT& rect) noexcept;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    static constexpr wchar_t CLASS_NAME[] = L"GameTranslateOverlay_v1";

    HWND      m_hwnd      = nullptr;
    HINSTANCE m_hInstance = nullptr;

    std::wstring m_text;

    int m_posX   = 100;
    int m_posY   = 100;
    int m_width  = 700;
    int m_height = 160;

    // ── GDI Cache ─────────────────────────────────────────────────────────────
    HDC     m_cachedMemDC  = nullptr;
    HBITMAP m_cachedBmp    = nullptr;
    void*   m_cachedBits   = nullptr;
    int     m_cachedWidth  = 0;
    int     m_cachedHeight = 0;

    // ── Appearance ────────────────────────────────────────────────────────────
    std::wstring m_fontName      = L"Segoe UI";
    int          m_fontSize      = 24;
    bool         m_fontBold      = false;  ///< false = Regular
    COLORREF     m_textColor     = RGB(255, 255, 255);
    COLORREF     m_shadowColor   = RGB(0,   0,   0  );
    COLORREF     m_strokeColor   = RGB(0,   0,   0  );
    bool         m_shadowEnabled = true;
    bool         m_strokeEnabled = true;
    float        m_strokeWidth   = 2.0f;

    bool m_dragMode = false;

    /// Drag clamping boundary (screen coordinates, set in Create())
    RECT m_boundRect = {};

    std::mutex m_renderMutex;   // Serialise Redraw() calls
    std::vector<std::vector<Point2F>> m_boxes; // Bounding boxes for in-place text masking
};
