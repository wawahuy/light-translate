#include "AppConfig.h"
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

// -- Helpers ------------------------------------------------------------------

static void WriteInt(const wchar_t* sec, const wchar_t* key, int v, const wchar_t* f)
{
    WritePrivateProfileStringW(sec, key, std::to_wstring(v).c_str(), f);
}

static void WriteStr(const wchar_t* sec, const wchar_t* key,
                     const std::wstring& v, const wchar_t* f)
{
    WritePrivateProfileStringW(sec, key, v.c_str(), f);
}

static int ReadInt(const wchar_t* sec, const wchar_t* key, int def, const wchar_t* f)
{
    return static_cast<int>(GetPrivateProfileIntW(sec, key, def, f));
}

static std::wstring ReadStr(const wchar_t* sec, const wchar_t* key,
                             const std::wstring& def, const wchar_t* f)
{
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(sec, key, def.c_str(), buf, 1024, f);
    return buf;
}

static void WriteDouble(const wchar_t* sec, const wchar_t* key, double v, const wchar_t* f)
{
    WritePrivateProfileStringW(sec, key, std::to_wstring(v).c_str(), f);
}

static double ReadDouble(const wchar_t* sec, const wchar_t* key, double def, const wchar_t* f)
{
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(sec, key, std::to_wstring(def).c_str(), buf, 64, f);
    return std::wcstod(buf, nullptr);
}

// -- AppConfig::Save -----------------------------------------------------------

void AppConfig::Save(const std::wstring& iniPath) const
{
    const wchar_t* f = iniPath.c_str();

    WriteStr(L"API", L"Url",    apiUrl, f);
    WriteStr(L"API", L"Model",  apiModel, f);
    WriteStr(L"API", L"Key",    apiKey, f);
    WriteStr(L"API", L"TargetLanguage", targetLanguage, f);
    WriteStr(L"API", L"SourceLanguage", sourceLanguage, f);
    WriteInt(L"API", L"Provider", static_cast<int>(providerType), f);
    WriteInt(L"API", L"OcrType", static_cast<int>(ocrType), f);

    WriteInt(L"Capture", L"Left",    captureRect.left,   f);
    WriteInt(L"Capture", L"Top",     captureRect.top,    f);
    WriteInt(L"Capture", L"Right",   captureRect.right,  f);
    WriteInt(L"Capture", L"Bottom",  captureRect.bottom, f);
    WriteInt(L"Capture", L"Set",     captureSet   ? 1 : 0, f);
    WriteInt(L"Capture", L"ScaleRoi", scaleRoi, f);
    WriteInt(L"Capture", L"Monitor", monitorIndex, f);
    WriteInt(L"Capture", L"Mode",    static_cast<int>(captureMode), f);
    WriteInt(L"Capture", L"DisplayMode", static_cast<int>(displayMode), f);
    WriteInt(L"Capture", L"IntervalMs", captureIntervalMs, f);
    WriteInt(L"Capture", L"HotkeyVk",  static_cast<int>(hotkeyVk), f);
    WriteInt(L"Capture", L"HotkeyMod", static_cast<int>(hotkeyMod), f);
    WriteInt(L"Capture", L"PauseHotkeyVk",  static_cast<int>(pauseHotkeyVk), f);
    WriteInt(L"Capture", L"PauseHotkeyMod", static_cast<int>(pauseHotkeyMod), f);
    WriteInt(L"Capture", L"ToggleWndVk",  static_cast<int>(toggleWndVk), f);
    WriteInt(L"Capture", L"ToggleWndMod", static_cast<int>(toggleWndMod), f);
    WriteInt(L"Capture", L"RegionHotkeyVk",  static_cast<int>(regionHotkeyVk), f);
    WriteInt(L"Capture", L"RegionHotkeyMod", static_cast<int>(regionHotkeyMod), f);

    if (displayMode != DisplayMode::InPlace)
    {
        WriteInt(L"Overlay", L"X",       overlayPos.x, f);
        WriteInt(L"Overlay", L"Y",       overlayPos.y, f);
        WriteInt(L"Overlay", L"W",       overlayWidth, f);
        WriteInt(L"Overlay", L"H",       overlayHeight, f);
    }
    WriteInt(L"SettingsWindow", L"X", settingsWndPos.x, f);
    WriteInt(L"SettingsWindow", L"Y", settingsWndPos.y, f);

    WriteStr(L"Font", L"Name",       fontName, f);
    WriteInt(L"Font", L"Size",       fontSize, f);
    WriteInt(L"Font", L"Color",      static_cast<int>(textColor),   f);
    WriteInt(L"Font", L"Shadow",     shadowEnabled ? 1 : 0,         f);
    WriteInt(L"Font", L"ShadowColor",static_cast<int>(shadowColor), f);
    WriteInt(L"Font", L"Stroke",     strokeEnabled ? 1 : 0,         f);
    WriteInt(L"Font", L"StrokeColor",static_cast<int>(strokeColor), f);

    WriteInt(L"ROI", L"Active",    roiActive ? 1 : 0, f);
    WriteInt(L"ROI", L"TimeoutMs", roiTimeoutMs,      f);
    WriteInt(L"ROI", L"Left",      roiRect.left,      f);
    WriteInt(L"ROI", L"Top",       roiRect.top,       f);
    WriteInt(L"ROI", L"Right",     roiRect.right,     f);
    WriteInt(L"ROI", L"Bottom",    roiRect.bottom,    f);
}

// -- AppConfig::Load -----------------------------------------------------------

bool AppConfig::Load(const std::wstring& iniPath)
{
    if (!PathFileExistsW(iniPath.c_str()))
        return false;

    const wchar_t* f = iniPath.c_str();

    apiUrl = ReadStr(L"API", L"Url", apiUrl, f);
    apiModel = ReadStr(L"API", L"Model", apiModel, f);
    apiKey = ReadStr(L"API", L"Key", apiKey, f);
    targetLanguage = ReadStr(L"API", L"TargetLanguage", targetLanguage, f);
    sourceLanguage = ReadStr(L"API", L"SourceLanguage", sourceLanguage, f);
    providerType = static_cast<TranslateProvider>(ReadInt(L"API", L"Provider", 1, f)); // 1 represents Google
    ocrType = static_cast<OcrType>(ReadInt(L"API", L"OcrType", 1, f)); // 1 represents WindowsOCR

    captureRect.left   = ReadInt(L"Capture", L"Left",    0,   f);
    captureRect.top    = ReadInt(L"Capture", L"Top",     0,   f);
    captureRect.right  = ReadInt(L"Capture", L"Right",   800, f);
    captureRect.bottom = ReadInt(L"Capture", L"Bottom",  100, f);
    captureSet         = ReadInt(L"Capture", L"Set",     0,   f) != 0;
    scaleRoi           = ReadInt(L"Capture", L"ScaleRoi", 80, f);
    monitorIndex       = ReadInt(L"Capture", L"Monitor", 0,   f);
    captureMode        = static_cast<CaptureMode>(ReadInt(L"Capture", L"Mode", 0, f));
    displayMode        = static_cast<DisplayMode>(ReadInt(L"Capture", L"DisplayMode", 1, f)); // Default to InPlace (1)
    captureIntervalMs  = ReadInt(L"Capture", L"IntervalMs", 1000, f);
    hotkeyVk           = static_cast<UINT>(ReadInt(L"Capture", L"HotkeyVk", 'N', f));
    hotkeyMod          = static_cast<UINT>(ReadInt(L"Capture", L"HotkeyMod", MOD_CONTROL, f));
    pauseHotkeyVk      = static_cast<UINT>(ReadInt(L"Capture", L"PauseHotkeyVk", 'P', f));
    pauseHotkeyMod     = static_cast<UINT>(ReadInt(L"Capture", L"PauseHotkeyMod", MOD_CONTROL, f));
    toggleWndVk        = static_cast<UINT>(ReadInt(L"Capture", L"ToggleWndVk", 'O', f));
    toggleWndMod       = static_cast<UINT>(ReadInt(L"Capture", L"ToggleWndMod", MOD_CONTROL | MOD_SHIFT, f));
    regionHotkeyVk     = static_cast<UINT>(ReadInt(L"Capture", L"RegionHotkeyVk", 'M', f));
    regionHotkeyMod    = static_cast<UINT>(ReadInt(L"Capture", L"RegionHotkeyMod", MOD_CONTROL, f));

    overlayPos.x = ReadInt(L"Overlay", L"X", 100, f);
    overlayPos.y = ReadInt(L"Overlay", L"Y", 100, f);
    overlayWidth = ReadInt(L"Overlay", L"W", 800, f);
    overlayHeight = ReadInt(L"Overlay", L"H", 250, f);
    settingsWndPos.x = ReadInt(L"SettingsWindow", L"X", -1, f);
    settingsWndPos.y = ReadInt(L"SettingsWindow", L"Y", -1, f);

    fontName    = ReadStr(L"Font", L"Name",       fontName, f);
    fontSize    = ReadInt(L"Font", L"Size",        24, f);
    textColor   = static_cast<COLORREF>(ReadInt(L"Font", L"Color",       static_cast<int>(textColor),   f));
    shadowEnabled = ReadInt(L"Font", L"Shadow",    1,  f) != 0;
    shadowColor = static_cast<COLORREF>(ReadInt(L"Font", L"ShadowColor", static_cast<int>(shadowColor), f));
    strokeEnabled = ReadInt(L"Font", L"Stroke",    1,  f) != 0;
    strokeColor = static_cast<COLORREF>(ReadInt(L"Font", L"StrokeColor", static_cast<int>(strokeColor), f));

    roiActive    = ReadInt(L"ROI", L"Active",    1,    f) != 0;
    roiTimeoutMs = ReadInt(L"ROI", L"TimeoutMs", 3000, f);
    roiRect.left = ReadInt(L"ROI", L"Left",      0,    f);
    roiRect.top  = ReadInt(L"ROI", L"Top",       0,    f);
    roiRect.right= ReadInt(L"ROI", L"Right",     0,    f);
    roiRect.bottom=ReadInt(L"ROI", L"Bottom",    0,    f);

    return true;
}
