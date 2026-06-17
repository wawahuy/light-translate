#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <memory>
#include <string>
#include <vector>
#include "src/AppConfig.h"
#include "src/capture/CaptureEngine.h"
#include "src/ui/OverlayWindow.h"
#include "src/ui/CaptureHelperWindow.h"
#include "src/ui/RegionSelectWindow.h"
#include "src/ui/RegionResultWindow.h"
#include "src/network/ITranslateProvider.h"
#include "src/ocr/IOcrEngine.h"
#include "src/TranslationPipeline.h"

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
    void CreateRealtimeTab(int x, int y, int w);
    void CreateTranslateTab(int x, int y, int w);
    void CreateSystemTab(int x, int y, int w);
    void CreateRegionTab(int x, int y, int w);
    HWND MakeLabel (int x, int y, int w, int h, const wchar_t* txt, UINT id = -1);
    HWND MakeEdit  (int x, int y, int w, int h, UINT id, bool multiLine = false);
    HWND MakeButton(int x, int y, int w, int h, const wchar_t* txt, UINT id);
    HWND MakeCheck (int x, int y, int w, int h, const wchar_t* txt, UINT id);
    HWND MakeCombo (int x, int y, int w, int h, UINT id);
    HWND MakeGroup (int x, int y, int w, int h, const wchar_t* txt);

    // -- Tab management --------------------------------------------------------
    void OnTabChanged();
    void ShowTab(int index);

    // -- Capture mode ----------------------------------------------------------
    void OnCaptureModeChanged();
    void UpdateCaptureModeUI();
    void OnDisplayModeChanged();
    void UpdateDisplayModeUI();
    void UpdateProviderUI();
    void UpdateRoiUI();
    void UpdateRoiLabel();

    // -- Command handlers ------------------------------------------------------
    void OnStart();
    void OnStop();
    void OnSelectRegion();
    void OnTestApi();
    void OnToggleDrag();
    void OnProviderChanged();
    void OnRoiActiveChanged();
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

    // -- Hotkey ----------------------------------------------------------------
    void RegisterCaptureHotkey();
    void UnregisterCaptureHotkey();
    void RegisterPauseHotkey();
    void UnregisterPauseHotkey();
    void RegisterToggleWndHotkey();
    void UnregisterToggleWndHotkey();
    void RegisterRegionHotkey();
    void UnregisterRegionHotkey();
    void OnHotkey(int id);
    static const int HOTKEY_CAPTURE_ID = 1;
    static const int HOTKEY_PAUSE_ID = 2;
    static const int HOTKEY_TOGGLE_WND_ID = 3;
    static const int HOTKEY_REGION_ID = 4;
    static LRESULT CALLBACK HotkeyEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    // -- Region selection ------------------------------------------------------
    void OnRegionHotkeyPressed();
    void PerformRegionCapture(const RECT& region);

    // -- System tray -----------------------------------------------------------
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    // -- Misc ------------------------------------------------------------------
    std::wstring GetIniPath() const;
    static std::wstring VkToName(UINT vk);
    static std::wstring HotkeyToString(UINT vk, UINT mod);

    // -- Data ------------------------------------------------------------------
    HWND        m_hwnd      = nullptr;
    HINSTANCE   m_hInstance = nullptr;
    bool        m_trayAdded = false;
    bool        m_running   = false;
    bool        m_hotkeyRegistered = false;
    bool        m_pauseHotkeyRegistered = false;
    bool        m_toggleWndHotkeyRegistered = false;
    bool        m_regionHotkeyRegistered = false;

    // Tab control
    HWND                  m_tabCtrl = nullptr;
    int                   m_currentTab = 0;
    std::vector<HWND>     m_realtimeControls;    ///< Controls on "Realtime" tab
    std::vector<HWND>     m_translateControls;   ///< Controls on "Translate" tab
    std::vector<HWND>     m_systemControls;      ///< Controls on "System" tab
    std::vector<HWND>     m_regionControls;      ///< Controls on "Region" tab

    // Capture mode sub-controls (conditionally shown within Realtime tab)
    std::vector<HWND>     m_autoModeControls;    ///< Interval edit (Auto mode)
    std::vector<HWND>     m_hotkeyModeControls;  ///< Hotkey edit (Hotkey mode)
    std::vector<HWND>     m_overlayPosControls;  ///< Position controls (Overlay mode)

    AppConfig        m_config;
    CaptureEngine    m_capture;
    OverlayWindow    m_overlay;
    CaptureHelperWindow m_captureHelper;
    RegionSelectWindow  m_regionSelect;
    RegionResultWindow  m_regionResult;
    std::unique_ptr<ITranslateProvider> m_client;
    TranslationPipeline   m_scheduler;
    std::unique_ptr<IOcrEngine> m_regionOcr;   // Separate OCR engine for region capture

    static constexpr wchar_t CLASS_NAME[] = L"GameTranslate_SettingsWnd";
    static constexpr int WND_W = 560;
    static constexpr int WND_H = 850;
};
