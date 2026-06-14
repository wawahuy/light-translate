#pragma once
#include <windows.h>
#include <string>
#include "network/TranslateProvider.h"

/// Capture trigger mode.
enum class CaptureMode : int
{
    Auto   = 0,  ///< Continuous capture at fixed interval
    Hotkey = 1,  ///< Capture one frame when hotkey is pressed
};

/// All user-configurable settings.
/// Persisted as a UTF-16 INI file via the Win32 Profile API.
struct AppConfig
{
    // -- API ------------------------------------------------------------------
    TranslateProvider providerType = TranslateProvider::DeepSeek;
    std::wstring apiUrl = L"https://api.deepseek.com/chat/completions";
    std::wstring apiModel = L"deepseek-chat";
    std::wstring apiKey = L"";
    std::wstring targetLanguage = L"Vietnamese";

    // ── Capture ──────────────────────────────────────────────────────────────
    RECT captureRect  = { 0, 0, 800, 100 }; ///< Screen-coordinate capture region
    bool captureSet   = false;              ///< Whether the user has chosen a region
    int  monitorIndex = 0;                  ///< 0 = primary monitor

    // -- Scheduler / Capture mode ----------------------------------------------
    CaptureMode captureMode     = CaptureMode::Auto;
    int         captureIntervalMs = 1000; ///< Interval between auto-capture frames (ms)
    UINT        hotkeyVk        = VK_F2;  ///< Virtual-key code for hotkey capture
    UINT        hotkeyMod       = 0;      ///< Key modifiers (MOD_CONTROL, MOD_SHIFT, etc.)
    UINT        pauseHotkeyVk   = VK_F3;  ///< Virtual-key code for pause hotkey
    UINT        pauseHotkeyMod  = 0;      ///< Key modifiers for pause hotkey
    UINT        toggleWndVk     = 'H';    ///< Virtual-key code for settings window hotkey
    UINT        toggleWndMod    = MOD_CONTROL | MOD_SHIFT; ///< Key modifiers (Ctrl+Shift)

    /// Returns the configured interval (used by Scheduler in Auto mode).
    [[nodiscard]] int GetIntervalMs() const noexcept
    {
        return (captureIntervalMs > 0) ? captureIntervalMs : 1000;
    }

    // -- Overlay position & size ----------------------------------------------
    POINT overlayPos = { 100, 100 };
    int   overlayWidth = 800;
    int   overlayHeight = 250;

    // ── Typography ────────────────────────────────────────────────────────────
    std::wstring fontName    = L"Arial";
    int          fontSize    = 24;
    COLORREF     textColor   = RGB(255, 255, 255);
    COLORREF     shadowColor = RGB(0, 0, 0);
    COLORREF     strokeColor = RGB(0, 0, 0);
    bool         shadowEnabled = true;
    bool         strokeEnabled = true;
    float        strokeWidth   = 2.0f;

    // ── Persistence ───────────────────────────────────────────────────────────
    void Save(const std::wstring& iniPath) const;
    bool Load(const std::wstring& iniPath);
};
