#pragma once
#include <windows.h>
#include <string>
#include "network/TranslateProvider.h"
#include "ocr/IOcrEngine.h"

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
    TranslateProvider providerType = TranslateProvider::Google;
    OcrType ocrType = OcrType::PaddleOCR; // Default to PaddleOCR
    std::wstring apiUrl = L"https://api.deepseek.com/chat/completions";
    std::wstring apiModel = L"deepseek-chat";
    std::wstring apiKey = L"";
    std::wstring targetLanguage = L"Vietnamese";

    // ── Capture ──────────────────────────────────────────────────────────────
    RECT captureRect  = { 0, 0, 800, 100 }; ///< Screen-coordinate capture region
    bool captureSet   = false;              ///< Whether the user has chosen a region
    int  scaleRoi     = 80;                 ///< Scale ROI in percentage (default 70)
    int  monitorIndex = 0;                  ///< 0 = primary monitor

    // -- Scheduler / Capture mode ----------------------------------------------
    CaptureMode captureMode     = CaptureMode::Auto;
    int         captureIntervalMs = 1000; ///< Interval between auto-capture frames (ms)
    UINT        hotkeyVk        = 'N';    ///< Virtual-key code for hotkey capture (Ctrl + N)
    UINT        hotkeyMod       = MOD_CONTROL;
    UINT        pauseHotkeyVk   = 'P';    ///< Virtual-key code for pause hotkey (Ctrl + P)
    UINT        pauseHotkeyMod  = MOD_CONTROL;
    UINT        toggleWndVk     = 'O';    ///< Virtual-key code for settings window hotkey (Ctrl + Shift + O)
    UINT        toggleWndMod    = MOD_CONTROL | MOD_SHIFT;
    UINT        regionHotkeyVk  = 'M';    ///< Virtual-key code for region selection hotkey (Ctrl + M)
    UINT        regionHotkeyMod = MOD_CONTROL;

    /// Returns the configured interval (used by Scheduler in Auto mode).
    [[nodiscard]] int GetIntervalMs() const noexcept
    {
        return (captureIntervalMs > 0) ? captureIntervalMs : 1000;
    }

    // -- Overlay position & size ----------------------------------------------
    POINT overlayPos = { 100, 100 };
    int   overlayWidth = 800;
    int   overlayHeight = 250;
    POINT settingsWndPos = { -1, -1 };

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
