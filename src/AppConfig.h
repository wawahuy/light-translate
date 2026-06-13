#pragma once
#include <windows.h>
#include <string>
#include "network/TranslateProvider.h"

/// All user-configurable settings.
/// Persisted as a UTF-16 INI file via the Win32 Profile API.
struct AppConfig
{
    // -- API ------------------------------------------------------------------
    TranslateProvider providerType = TranslateProvider::DeepSeek;
    std::wstring apiUrl = L"https://api.deepseek.com/chat/completions";
    std::wstring apiModel = L"deepseek-chat";
    std::wstring apiKey = L"";

    // ── Capture ──────────────────────────────────────────────────────────────
    RECT captureRect  = { 0, 0, 800, 100 }; ///< Screen-coordinate capture region
    bool captureSet   = false;              ///< Whether the user has chosen a region
    int  monitorIndex = 0;                  ///< 0 = primary monitor

    // -- Scheduler ------------------------------------------------------------
    double framesPerSecond = 2.0; ///< Accepted values: 0.5, 1.0, 2.0, 4.0

    /// Converts framesPerSecond to millisecond interval.
    [[nodiscard]] int GetIntervalMs() const noexcept
    {
        return (framesPerSecond > 0.0) ? static_cast<int>(1000.0 / framesPerSecond) : 1000;
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
