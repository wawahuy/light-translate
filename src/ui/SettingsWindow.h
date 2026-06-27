#pragma once
#include <windows.h>
#include <d3d11.h>
#include <memory>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include "src/AppConfig.h"
#include "src/utils/Updater.h"
#include "src/ui/OverlayWindow.h"
#include "src/ui/CaptureHelperWindow.h"
#include "src/ui/RegionSelectWindow.h"
#include "src/ui/RegionResultWindow.h"
#include "src/AppController.h"

/// The main application window that hosts the ImGui settings controls.
/// Also owns the system tray icon when minimised.
class SettingsWindow
{
public:
    SettingsWindow();
    ~SettingsWindow();

    bool Create(HINSTANCE hInstance);

    /// Enter the Windows message loop and render loop.
    int  RunMessageLoop();

    HWND GetHWND() const { return m_hwnd; }

    static constexpr wchar_t CLASS_NAME[] = L"GameTranslate_SettingsWnd";

private:
    // -- Window procedure ------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // -- D3D11 Helpers ---------------------------------------------------------
    bool CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    // -- ImGui Rendering -------------------------------------------------------
    void RenderFrame();
    void RenderUI();
    void ApplyImGuiStyle();
    void LoadFonts();

    // -- Tab Renderers ---------------------------------------------------------
    void RenderAppTab();
    void RenderAdvanceTab();
    void RenderCaptureSettingSubTab();
    void RenderRoiIdleDetectionSubTab();
    void RenderOverlaySubTab();
    void RenderRegionTab();
    void RenderTranslateTab();
    void RenderSystemTab();
    void RenderAboutTab();

    // -- Capture mode & Display changes ----------------------------------------
    void OnCaptureModeChanged();
    void OnDisplayModeChanged();
    void OnRoiActiveChanged();

    // -- Command handlers ------------------------------------------------------
    void OnStart();
    void OnStop();
    void OnSelectRegion();
    void OnTestApi();
    void OnToggleDrag();
    void OnProviderChanged();
    void OnSave();

    // -- Config <-> UI ---------------------------------------------------------
    void ConfigToUI();
    void UIToConfig();
    std::wstring GetRegionInfoText() const;
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

    // D3D11 objects
    ID3D11Device*            m_pd3dDevice = nullptr;
    ID3D11DeviceContext*     m_pd3dDeviceContext = nullptr;
    IDXGISwapChain*          m_pSwapChain = nullptr;
    ID3D11RenderTargetView*  m_mainRenderTargetView = nullptr;
    bool                     m_SwapChainOccluded = false;

    // ImGui state
    int                   m_currentTab = 0;
    int                   m_appMode = 0; // 0: Windows Overlay (Legacy), 1: In-Game Hooking (Advanced)
    int                   m_recordingHotkeyType = 0; // 0: None, 1: Capture, 2: Pause, 3: Toggle, 4: Region
    float                 m_dpiScale = 1.0f;

    // Buffers for text inputs
    char                  m_apiModelBuf[256] = "";
    char                  m_apiKeyBuf[256] = "";
    char                  m_fontNameBuf[256] = "";

    // Color states (temp floats for ColorPicker)
    float                 m_textColorFloat[3] = {1.0f, 1.0f, 1.0f};
    float                 m_shadowColorFloat[3] = {0.0f, 0.0f, 0.0f};
    float                 m_strokeColorFloat[3] = {0.0f, 0.0f, 0.0f};

    // Log list
    std::vector<std::wstring> m_logs;
    bool                  m_scrollToBottom = false;
    std::vector<std::pair<std::wstring, std::wstring>> m_availableOcrLangs;

    AppConfig        m_config;
    OverlayWindow    m_overlay;
    CaptureHelperWindow m_captureHelper;
    RegionSelectWindow  m_regionSelect;
    RegionResultWindow  m_regionResult;
    std::unique_ptr<AppController> m_controller;
    Updater          m_updater;
    std::atomic<bool> m_switchToAboutTab{ false };

    // -- API Connection Test State ---------------------------------------------
    enum class ApiTestState
    {
        Idle,
        Testing,
        Success,
        Failed
    };
    std::atomic<ApiTestState> m_apiTestState{ ApiTestState::Idle };
    std::wstring              m_apiTestMessage;
    std::mutex                m_apiTestMutex;

    static constexpr int WND_W = 720;
    static constexpr int WND_H = 660;
};
