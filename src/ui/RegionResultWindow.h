#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

/// Displays translated text at a specific screen position with a dark
/// semi-transparent background.  Dismisses on any key press.
class RegionResultWindow
{
public:
    RegionResultWindow();
    ~RegionResultWindow();

    bool Create(HINSTANCE hInstance);
    void Destroy();

    /// Show translated text at the given screen rectangle.
    /// The window appears as a topmost overlay with dark background.
    void ShowResult(const RECT& region, const std::wstring& text);

    /// Hide and destroy the result overlay.
    void Hide();

    bool IsVisible() const;

    // Text appearance
    void SetFontName(const std::wstring& n) { m_fontName = n; }
    void SetFontSize(int s)                 { m_fontSize = s; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void PaintResult(HDC hdc);

    /// Low-level keyboard hook to catch any key press and dismiss.
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    void InstallKeyHook();
    void RemoveKeyHook();

    /// Low-level mouse hook to catch any click and dismiss.
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    void InstallMouseHook();
    void RemoveMouseHook();

    HWND      m_hwnd      = nullptr;
    HINSTANCE m_hInstance  = nullptr;
    HHOOK     m_keyHook   = nullptr;
    HHOOK     m_mouseHook = nullptr;

    std::wstring m_text;
    RECT         m_region  = {};

    std::wstring m_fontName = L"Segoe UI";
    int          m_fontSize = 20;

    static RegionResultWindow* s_instance;  ///< For the keyboard hook callback
    static constexpr wchar_t CLASS_NAME[] = L"GameTranslate_RegionResult";
};
