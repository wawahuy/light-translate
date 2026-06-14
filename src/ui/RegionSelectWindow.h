#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>

/// Fullscreen overlay for selecting a rectangular screen region.
/// Shows a dimmed screen; the user drags to select, Escape cancels.
class RegionSelectWindow
{
public:
    RegionSelectWindow();
    ~RegionSelectWindow();

    bool Create(HINSTANCE hInstance);
    void Destroy();

    /// Show the selection overlay (captures current screen first).
    void Show();

    /// Callback when a region is successfully selected.
    std::function<void(const RECT&)> OnRegionSelected;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CaptureScreen();
    void DrawOverlay(HDC hdc);

    HWND      m_hwnd      = nullptr;
    HINSTANCE m_hInstance  = nullptr;
    HBITMAP   m_screenBmp  = nullptr;  ///< Screenshot bitmap
    int       m_screenW    = 0;
    int       m_screenH    = 0;

    // Selection state
    bool  m_selecting  = false;
    POINT m_startPt    = {};
    POINT m_currentPt  = {};

    static constexpr wchar_t CLASS_NAME[] = L"GameTranslate_RegionSelect";
};
