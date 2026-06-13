#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <memory>
#include <string>
#include "src/AppConfig.h"
#include "src/capture/CaptureEngine.h"
#include "src/overlay/OverlayWindow.h"
#include "src/ui/CaptureHelperWindow.h"
#include "src/network/TextTranslateProvider.h"
#include "src/Scheduler.h"

/// The main application window that hosts all settings controls.
/// Also owns the system tray icon when minimised.
class SettingsWindow
{
public:
    SettingsWindow();
    ~SettingsWindow();

    bool Create(HINSTANCE hInstance);

    /// Enter the Windows message loop (returns when WM_QUIT is received).
    int  RunMessageLoop();

    HWND GetHWND() const { return m_hwnd; }

private:
    // -- Window procedure ------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // -- WM_CREATE helpers -----------------------------------------------------
    void CreateControls();
    HWND MakeLabel (int x, int y, int w, int h, const wchar_t* txt);
    HWND MakeEdit  (int x, int y, int w, int h, UINT id, bool multiLine = false);
    HWND MakeButton(int x, int y, int w, int h, const wchar_t* txt, UINT id);
    HWND MakeCheck (int x, int y, int w, int h, const wchar_t* txt, UINT id);
    HWND MakeCombo (int x, int y, int w, int h, UINT id);
    HWND MakeGroup (int x, int y, int w, int h, const wchar_t* txt);

    // -- Command handlers ------------------------------------------------------
    void OnStart();
    void OnStop();
    void OnSelectRegion();
    void OnTestApi();
    void OnToggleDrag();
    void OnProviderChanged();
    void OnSave();
    void OnTextColorPick();
    void OnShadowColorPick(bool shadow);
    void OnColorPick(COLORREF& colorRef);

    // -- Config <-> UI ---------------------------------------------------------
    void ConfigToUI();
    void UIToConfig();
    void UpdateRegionLabel();
    void UpdateStatus(const std::wstring& text);
    void SyncHelperWindows();

    // -- System tray -----------------------------------------------------------
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    // -- Misc ------------------------------------------------------------------
    std::wstring GetIniPath() const;

    // -- Data ------------------------------------------------------------------
    HWND        m_hwnd      = nullptr;
    HINSTANCE   m_hInstance = nullptr;
    bool        m_trayAdded = false;
    bool        m_running   = false;

    AppConfig        m_config;
    CaptureEngine    m_capture;
    OverlayWindow    m_overlay;
    CaptureHelperWindow m_captureHelper;
    TextTranslateProvider  m_client;
    Scheduler        m_scheduler;

    static constexpr wchar_t CLASS_NAME[] = L"GameTranslate_SettingsWnd";
    static constexpr int WND_W = 560;
    static constexpr int WND_H = 804;   // +64 cho provider combo + WS URL
};
