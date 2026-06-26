#include "src/ui/SettingsWindow.h"
#include "src/utils/ImageEncoder.h"
#include "src/utils/StringUtils.h"
#include "resource.h"
#include "src/ocr/OcrFactory.h"
#include "src/network/TranslateProviderFactory.h"
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <string>
#include <cwchar>
#include <thread>
#include <windowsx.h>
#include <opencv2/imgproc.hpp>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

struct RegionResultData
{
    RECT region;
    std::wstring text;
};

constexpr wchar_t SettingsWindow::CLASS_NAME[];

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    COLORREF FloatToColorref(const float in[3])
    {
        return RGB(
            static_cast<BYTE>(in[0] * 255.0f),
            static_cast<BYTE>(in[1] * 255.0f),
            static_cast<BYTE>(in[2] * 255.0f)
        );
    }

    void ColorrefToFloat(COLORREF cr, float out[3])
    {
        out[0] = static_cast<float>(GetRValue(cr)) / 255.0f;
        out[1] = static_cast<float>(GetGValue(cr)) / 255.0f;
        out[2] = static_cast<float>(GetBValue(cr)) / 255.0f;
    }
}

// -----------------------------------------------------------------------------
//  Constructor / Destructor
// -----------------------------------------------------------------------------

SettingsWindow::SettingsWindow() : m_controller(std::make_unique<AppController>()) {}
SettingsWindow::~SettingsWindow()
{
    UnregisterCaptureHotkey();
    UnregisterPauseHotkey();
    UnregisterToggleWndHotkey();
    UnregisterRegionHotkey();
    RemoveTrayIcon();

    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    CleanupDeviceD3D();
}

// -----------------------------------------------------------------------------
//  Create
// -----------------------------------------------------------------------------

bool SettingsWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc)) return false;

    // Load persisted config
    m_config.Load(GetIniPath());

    // Initialize capture helper window
    m_captureHelper.Create(m_hInstance);
    m_captureHelper.SetRect(m_config.captureRect);
    m_captureHelper.OnRectChanged = [&](const RECT& rc)
        {
            m_config.captureRect = rc;
            m_config.captureSet = true;
        };
    m_captureHelper.OnRoiRectChanged = [&](const RECT& rc)
        {
            m_config.roiRect = rc;
        };

    // Initialise Overlay window
    m_overlay.Create(m_hInstance);
    m_overlay.SetPosition(m_config.overlayPos.x, m_config.overlayPos.y);
    m_overlay.SetSize(m_config.overlayWidth, m_config.overlayHeight);
    m_overlay.OnMoved = [&](int x, int y)
        {
            if (m_config.displayMode != DisplayMode::InPlace)
            {
                m_config.overlayPos = { x, y };
                int w, h;
                m_overlay.GetSize(w, h);
                m_config.overlayWidth = w;
                m_config.overlayHeight = h;
            }
        };

    // Initialize region selection + result windows
    m_regionSelect.Create(m_hInstance);
    m_regionSelect.OnRegionSelected = [&](const RECT& rc)
        {
            m_regionResult.SetFontName(m_config.fontName);
            m_regionResult.SetFontSize(m_config.fontSize);
            m_regionResult.ShowResult(rc, L"");

            std::thread([this, rc]() {
                PerformRegionCapture(rc);
            }).detach();
        };
    m_regionResult.Create(m_hInstance);

    // Controller status callback
    m_controller->OnStatus = [&](const std::wstring& s)
        {
            wchar_t* buf = new wchar_t[s.size() + 1];
            std::wmemcpy(buf, s.c_str(), s.size() + 1);
            PostMessageW(m_hwnd, WM_UPDATE_STATUS, 0, reinterpret_cast<LPARAM>(buf));
        };

    // Calculate DPI scale
    m_dpiScale = 1.0f;
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
        auto pGetDpiForSystem = (UINT(WINAPI*)())GetProcAddress(hUser32, "GetDpiForSystem");
        if (pGetDpiForSystem)
        {
            m_dpiScale = static_cast<float>(pGetDpiForSystem()) / 96.0f;
        }
    }

    int width = static_cast<int>(WND_W * m_dpiScale);
    int height = static_cast<int>(WND_H * m_dpiScale);

    // Compute centred position
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"LIGHT TRANSLATE — Settings",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU,
        (sx - width) / 2, (sy - height) / 2,
        width, height,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    // Restore window position if saved
    if (m_config.settingsWndPos.x != -1 && m_config.settingsWndPos.y != -1)
    {
        SetWindowPos(m_hwnd, nullptr, m_config.settingsWndPos.x, m_config.settingsWndPos.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Initialize D3D11
    if (!CreateDeviceD3D(m_hwnd))
    {
        CleanupDeviceD3D();
        return false;
    }

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ApplyImGuiStyle();
    ImGui::GetStyle().ScaleAllSizes(m_dpiScale);

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

    LoadFonts();

    // Populate buffers
    ConfigToUI();

    // Register toggle & region hotkeys
    RegisterToggleWndHotkey();
    RegisterRegionHotkey();

    // Synchronize window states
    SyncHelperWindows();

    // Automatically check for updates on startup.
    m_updater.CheckForUpdateAsync(APP_VERSION, [this]() {
        if (m_updater.GetStatus() == ::UpdateStatus::UpdateAvailable)
        {
            std::wstring msg = L"A new version (" + Utf8ToWide(m_updater.GetLatestVersion()) + L") is available.\n\nDo you want to download and install it now?";
            int res = MessageBoxW(nullptr, msg.c_str(), L"Light Translate Update", MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND);
            if (res == IDYES)
            {
                m_switchToAboutTab = true;
                m_updater.DownloadUpdateAsync();
                
                // Restore settings window and bring it to front
                ShowWindow(m_hwnd, SW_SHOW);
                ShowWindow(m_hwnd, SW_RESTORE);
                SetForegroundWindow(m_hwnd);
            }
        }
    });

    return true;
}

// -----------------------------------------------------------------------------
//  Message loop
// -----------------------------------------------------------------------------

int SettingsWindow::RunMessageLoop()
{
    MSG msg{};
    while (true)
    {
        bool isVisible = IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd);

        if (isVisible)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT)
                    return static_cast<int>(msg.wParam);
            }

            if (m_SwapChainOccluded && m_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
            {
                ::Sleep(10);
                continue;
            }
            m_SwapChainOccluded = false;

            RenderFrame();
        }
        else
        {
            if (GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT)
                    return static_cast<int>(msg.wParam);
            }
            else
            {
                return static_cast<int>(msg.wParam);
            }
        }
    }
}

// -----------------------------------------------------------------------------
//  Window procedure
// -----------------------------------------------------------------------------

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    SettingsWindow* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SHOW_SETTINGS_WND:
    {
        RemoveTrayIcon();
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
        SyncHelperWindows();
        return 0;
    }

    case WM_UPDATE_STATUS:

    {
        if (lParam)
        {
            auto* buf = reinterpret_cast<wchar_t*>(lParam);
            UpdateStatus(buf);
            delete[] buf;
        }
        return 0;
    }

    case WM_SHOW_REGION_RESULT:
    {
        if (lParam)
        {
            auto* data = reinterpret_cast<RegionResultData*>(lParam);
            if (data->text.empty())
            {
                m_regionResult.Hide();
            }
            else
            {
                m_regionResult.SetFontName(m_config.fontName);
                m_regionResult.SetFontSize(m_config.fontSize);
                m_regionResult.ShowResult(data->region, data->text);
            }
            delete data;
        }
        return 0;
    }

    case WM_HOTKEY:
    {
        OnHotkey(static_cast<int>(wParam));
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        if (m_recordingHotkeyType != 0)
        {
            UINT vk = static_cast<UINT>(wParam);
            UINT mod = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
            if (GetKeyState(VK_SHIFT)   & 0x8000) mod |= MOD_SHIFT;
            if (GetKeyState(VK_MENU)    & 0x8000) mod |= MOD_ALT;
            if (GetKeyState(VK_LWIN)    & 0x8000) mod |= MOD_WIN;
            if (GetKeyState(VK_RWIN)    & 0x8000) mod |= MOD_WIN;

            bool isModifierKey = (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                                  vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                                  vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                                  vk == VK_LWIN || vk == VK_RWIN);

            if (!isModifierKey)
            {
                if ((vk == VK_ESCAPE || vk == VK_BACK || vk == VK_DELETE) && mod == 0)
                {
                    vk = 0;
                    mod = 0;
                }

                if (m_recordingHotkeyType == 1)
                {
                    m_config.hotkeyVk = vk;
                    m_config.hotkeyMod = mod;
                    if (m_running) RegisterCaptureHotkey();
                }
                else if (m_recordingHotkeyType == 2)
                {
                    m_config.pauseHotkeyVk = vk;
                    m_config.pauseHotkeyMod = mod;
                    if (m_running) RegisterPauseHotkey();
                }
                else if (m_recordingHotkeyType == 3)
                {
                    m_config.toggleWndVk = vk;
                    m_config.toggleWndMod = mod;
                    RegisterToggleWndHotkey();
                }
                else if (m_recordingHotkeyType == 4)
                {
                    m_config.regionHotkeyVk = vk;
                    m_config.regionHotkeyMod = mod;
                    RegisterRegionHotkey();
                }

                m_recordingHotkeyType = 0;
            }
            return 0;
        }
        break;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        switch (id)
        {
        case ID_TRAY_SHOW:
            RemoveTrayIcon();
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
            SyncHelperWindows();
            break;
        case ID_TRAY_TOGGLE_DRAG:
            OnToggleDrag();
            break;
        case ID_TRAY_EXIT:
            SendMessageW(m_hwnd, WM_CLOSE, 0, 0);
            break;
        }
        return 0;
    }

    case WM_TRAYICON:
    {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            ShowTrayMenu();
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            RemoveTrayIcon();
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
            SyncHelperWindows();
        }
        return 0;
    }

    case WM_NCCALCSIZE:
    {
        if (wParam == TRUE)
        {
            return 0; // Remove standard frame and borders
        }
        break;
    }

    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hwnd, &pt);
        
        int titleHeight = static_cast<int>(40 * m_dpiScale);
        RECT clientRect{};
        GetClientRect(m_hwnd, &clientRect);
        int w = clientRect.right - clientRect.left;
        
        if (pt.y >= 0 && pt.y < titleHeight)
        {
            if (pt.x < w - static_cast<int>(100 * m_dpiScale))
                return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            AddTrayIcon();
            ShowWindow(m_hwnd, SW_HIDE);
        }
        SyncHelperWindows();
        return 0;

    case WM_CLOSE:
        OnStop();
        UnregisterCaptureHotkey();
        UnregisterPauseHotkey();
        UnregisterToggleWndHotkey();
        UnregisterRegionHotkey();
        m_regionResult.Destroy();
        m_regionSelect.Destroy();
        m_captureHelper.Destroy();
        m_overlay.Destroy();
        RemoveTrayIcon();

        // Save position
        {
            WINDOWPLACEMENT wp{};
            wp.length = sizeof(wp);
            if (GetWindowPlacement(m_hwnd, &wp))
            {
                m_config.settingsWndPos.x = wp.rcNormalPosition.left;
                m_config.settingsWndPos.y = wp.rcNormalPosition.top;
            }
        }
        m_config.Save(GetIniPath());
        DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
//  D3D11 Helpers
// -----------------------------------------------------------------------------

bool SettingsWindow::CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void SettingsWindow::CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
    if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
    if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

void SettingsWindow::CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer)
    {
        m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void SettingsWindow::CleanupRenderTarget()
{
    if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
}

// -----------------------------------------------------------------------------
//  ImGui Rendering & Style
// -----------------------------------------------------------------------------

void SettingsWindow::RenderFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RenderUI();

    ImGui::Render();
    const float clear_color_with_alpha[4] = { 0.11f, 0.11f, 0.13f, 1.00f };
    m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
    m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT hr = m_pSwapChain->Present(1, 0);
    m_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
}

void SettingsWindow::RenderUI()
{
    RECT rect{};
    GetClientRect(m_hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w), static_cast<float>(h)));

    ImGuiWindowFlags wndFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    
    ImGui::Begin("SettingsWindowImGui", nullptr, wndFlags);

    // Custom Title Bar Area (LIGHT TRANSLATE)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 10.0f));
    ImGui::BeginGroup();
    
    // Icon / Title
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.48f, 0.40f, 0.92f, 1.00f), "  LIGHT TRANSLATE");
    
    // Minimize button
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 125.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.21f, 1.0f)); // Visible frame bg
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.24f, 0.28f, 1.0f));
    
    ImVec2 min_pos = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##minimize", ImVec2(35, 25)))
    {
        ShowWindow(m_hwnd, SW_MINIMIZE);
    }
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(min_pos.x + 11, min_pos.y + 15),
        ImVec2(min_pos.x + 24, min_pos.y + 15),
        ImGui::GetColorU32(ImGuiCol_Text), 1.5f
    );
    ImGui::PopStyleColor(3);
    
    // Close button
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.21f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.0f)); // Red hover
    
    ImVec2 close_pos = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##close", ImVec2(35, 25)))
    {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(
        ImVec2(close_pos.x + 12, close_pos.y + 7),
        ImVec2(close_pos.x + 23, close_pos.y + 18),
        ImGui::GetColorU32(ImGuiCol_Text), 1.5f
    );
    draw_list->AddLine(
        ImVec2(close_pos.x + 23, close_pos.y + 7),
        ImVec2(close_pos.x + 12, close_pos.y + 18),
        ImGui::GetColorU32(ImGuiCol_Text), 1.5f
    );
    ImGui::PopStyleColor(3);
    
    ImGui::EndGroup();
    ImGui::PopStyleVar();
    
    ImGui::Separator();

    // Tab Bar
    if (ImGui::BeginTabBar("Tabs"))
    {
        // ----------------- APP TAB -----------------
        if (ImGui::BeginTabItem("App"))
        {
            ImGui::TextDisabled("Application Mode");
            ImGui::Spacing();

            ImGui::RadioButton("Windows Overlay (Legacy)", &m_appMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("In-Game Hooking (Advanced)", &m_appMode, 1);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            if (m_appMode == 0)
            {
                ImGui::TextWrapped("Description: Windows Overlay (Legacy)\n"
                                   "• Requires target game/application to run in Windowed or Borderless mode.\n"
                                   "• Renders translation text on a transparent top-most system overlay window.\n"
                                   "• Maximum compatibility across various applications and easy setup.");
            }
            else if (m_appMode == 1)
            {
                ImGui::TextWrapped("Description: In-Game Hooking (Advanced)\n"
                                   "• Injects directly into graphics rendering pipeline (DX11/DX12/OpenGL/Vulkan).\n"
                                   "• Renders translation text inside the game frame (works in Exclusive Fullscreen).\n"
                                   "• Maximum rendering performance, zero window positioning lag, and seamless overlay.");
            }
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled("Control Panel");
            ImGui::Spacing();

            if (m_running)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.10f, 0.10f, 1.00f));
                if (ImGui::Button("STOP", ImVec2(200, 45)))
                {
                    OnStop();
                }
                ImGui::PopStyleColor(3);
            }
            else
            {
                if (m_appMode == 1)
                {
                    ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.1f, 1.0f), "This mode will be supported in version 3.0.0.");
                    ImGui::Spacing();
                    ImGui::BeginDisabled();
                }

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.6f, 0.15f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.25f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.5f, 0.10f, 1.00f));
                if (ImGui::Button("START", ImVec2(200, 45)))
                {
                    OnStart();
                }
                ImGui::PopStyleColor(3);

                if (m_appMode == 1)
                {
                    ImGui::EndDisabled();
                }
            }

            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        // ----------------- REALTIME TAB -----------------
        if (ImGui::BeginTabItem("Realtime"))
        {
            ImGui::TextDisabled("Capture Settings");
            
            // Monitor index selection
            const char* monitorOptions[] = { "0 - Primary", "1 - Second", "2 - Third" };
            int currentMon = std::min(m_config.monitorIndex, 2);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::Combo("Monitor", &currentMon, monitorOptions, IM_ARRAYSIZE(monitorOptions)))
            {
                m_config.monitorIndex = currentMon;
            }

            // Capture region display/reset
            ImGui::SameLine();
            if (ImGui::Button("Reset Capture Region"))
            {
                OnSelectRegion();
            }
            ImGui::SameLine();
            ImGui::Text("Region: %s", WideToUtf8(GetRegionInfoText()).c_str());

            // Capture Mode selection
            const char* modeOptions[] = { "Auto (continuous)", "Hotkey (single frame)" };
            int currentMode = static_cast<int>(m_config.captureMode);
            ImGui::SetNextItemWidth(250.0f);
            if (ImGui::Combo("Capture Mode", &currentMode, modeOptions, IM_ARRAYSIZE(modeOptions)))
            {
                m_config.captureMode = static_cast<CaptureMode>(currentMode);
                OnCaptureModeChanged();
            }

            // Scale ROI
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragInt("Scale ROI (%)", &m_config.scaleRoi, 1.0f, 10, 200);
            if (m_config.scaleRoi <= 0) m_config.scaleRoi = 100;

            // Mode-specific configuration
            if (m_config.captureMode == CaptureMode::Auto)
            {
                ImGui::SetNextItemWidth(120.0f);
                ImGui::DragInt("Interval (ms)", &m_config.captureIntervalMs, 50.0f, 100, 10000);
                if (m_config.captureIntervalMs <= 0) m_config.captureIntervalMs = 1000;

                ImGui::SameLine();
                std::string pauseHotkeyStr = m_recordingHotkeyType == 2 ? "Press a key..." : WideToUtf8(HotkeyToString(m_config.pauseHotkeyVk, m_config.pauseHotkeyMod));
                ImGui::Text("Pause Hotkey:"); ImGui::SameLine();
                if (ImGui::Button(pauseHotkeyStr.c_str(), ImVec2(130, 0)))
                {
                    m_recordingHotkeyType = 2;
                }
            }
            else
            {
                std::string hotkeyStr = m_recordingHotkeyType == 1 ? "Press a key..." : WideToUtf8(HotkeyToString(m_config.hotkeyVk, m_config.hotkeyMod));
                ImGui::Text("Capture Hotkey:"); ImGui::SameLine();
                if (ImGui::Button(hotkeyStr.c_str(), ImVec2(130, 0)))
                {
                    m_recordingHotkeyType = 1;
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("ROI Idle Text Detection");

            if (ImGui::Checkbox("Enable ROI Idle Detection", &m_config.roiActive))
            {
                OnRoiActiveChanged();
            }
            if (m_config.roiActive)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                ImGui::DragInt("Idle Timeout (ms)", &m_config.roiTimeoutMs, 100.0f, 500, 30000);
                if (m_config.roiTimeoutMs <= 0) m_config.roiTimeoutMs = 3000;

                ImGui::SameLine();
                RECT roi = m_config.roiRect;
                ImGui::Text("ROI Rect (X,Y,W,H): %ld, %ld, %ld, %ld", roi.left, roi.top, roi.right - roi.left, roi.bottom - roi.top);
            }

            ImGui::Separator();
            ImGui::TextDisabled("Overlay & Typography");

            const char* dispOptions[] = { "In-Place (Default)", "Overlay Window" };
            int currentDisp = (m_config.displayMode == DisplayMode::InPlace) ? 0 : 1;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::Combo("Display Mode", &currentDisp, dispOptions, IM_ARRAYSIZE(dispOptions)))
            {
                m_config.displayMode = (currentDisp == 0) ? DisplayMode::InPlace : DisplayMode::Overlay;
                OnDisplayModeChanged();
            }

            if (m_config.displayMode == DisplayMode::Overlay)
            {
                ImGui::SameLine();
                ImGui::Text("Pos: X: %ld   Y: %ld", m_config.overlayPos.x, m_config.overlayPos.y);

                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputText("Font Name", m_fontNameBuf, sizeof(m_fontNameBuf));
                
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragInt("Size", &m_config.fontSize, 1.0f, 8, 72);
                if (m_config.fontSize <= 0) m_config.fontSize = 24;

                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.0f);
                if (ImGui::ColorEdit3("Text Color", m_textColorFloat, ImGuiColorEditFlags_NoInputs))
                {
                    m_config.textColor = FloatToColorref(m_textColorFloat);
                    SyncHelperWindows();
                }

                ImGui::Checkbox("Shadow", &m_config.shadowEnabled);
                if (m_config.shadowEnabled)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.0f);
                    if (ImGui::ColorEdit3("Shadow Color", m_shadowColorFloat, ImGuiColorEditFlags_NoInputs))
                    {
                        m_config.shadowColor = FloatToColorref(m_shadowColorFloat);
                        SyncHelperWindows();
                    }
                }

                ImGui::SameLine();
                ImGui::Checkbox("Stroke", &m_config.strokeEnabled);
                if (m_config.strokeEnabled)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.0f);
                    if (ImGui::ColorEdit3("Stroke Color", m_strokeColorFloat, ImGuiColorEditFlags_NoInputs))
                    {
                        m_config.strokeColor = FloatToColorref(m_strokeColorFloat);
                        SyncHelperWindows();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::DragFloat("Width", &m_config.strokeWidth, 0.1f, 1.0f, 10.0f, "%.1f");
                    if (m_config.strokeWidth <= 0.0f) m_config.strokeWidth = 1.0f;
                }
            }

            ImGui::EndTabItem();
        }

        // ----------------- REGION TAB -----------------
        if (ImGui::BeginTabItem("Region"))
        {
            ImGui::TextWrapped("Press the hotkey to select a screen region for quick translation.");
            ImGui::Spacing();

            std::string regHotkeyStr = m_recordingHotkeyType == 4 ? "Press a key..." : WideToUtf8(HotkeyToString(m_config.regionHotkeyVk, m_config.regionHotkeyMod));
            ImGui::Text("Selection Hotkey:"); ImGui::SameLine();
            if (ImGui::Button(regHotkeyStr.c_str(), ImVec2(150, 0)))
            {
                m_recordingHotkeyType = 4;
            }

            ImGui::Spacing();
            ImGui::TextWrapped("After selecting a region, the app will OCR and translate the text. Press any key to dismiss the result overlay.");

            ImGui::EndTabItem();
        }

        // ----------------- TRANSLATE TAB -----------------
        if (ImGui::BeginTabItem("Translate"))
        {
            const char* provOptions[] = { "DeepSeek", "Google Translate" };
            int currentProv = (m_config.providerType == TranslateProvider::DeepSeek) ? 0 : 1;
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::Combo("Provider", &currentProv, provOptions, IM_ARRAYSIZE(provOptions)))
            {
                m_config.providerType = (currentProv == 0) ? TranslateProvider::DeepSeek : TranslateProvider::Google;
                OnProviderChanged();
            }

            ImGui::SameLine();

            const char* const commonLangs[] = {
                "Vietnamese", "English", "Japanese", "Chinese (Simplified)", "Chinese (Traditional)",
                "Korean", "French", "German", "Russian", "Spanish", "Portuguese", "Italian",
                "Arabic", "Thai", "Indonesian", "Hindi", "Turkish"
            };
            int langIdx = 0;
            std::string langUtf8 = WideToUtf8(m_config.targetLanguage);
            for (int i = 0; i < IM_ARRAYSIZE(commonLangs); ++i)
            {
                if (langUtf8 == commonLangs[i])
                {
                    langIdx = i;
                    break;
                }
            }
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("Target Lang", &langIdx, commonLangs, IM_ARRAYSIZE(commonLangs)))
            {
                m_config.targetLanguage = Utf8ToWide(commonLangs[langIdx]);
            }

            if (m_config.providerType == TranslateProvider::DeepSeek)
            {
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputText("API Model", m_apiModelBuf, sizeof(m_apiModelBuf));
                
                ImGui::SameLine();
                ImGui::SetNextItemWidth(250.0f);
                ImGui::InputText("API Key", m_apiKeyBuf, sizeof(m_apiKeyBuf), ImGuiInputTextFlags_Password);
            }

            ImGui::Spacing();
            if (ImGui::Button("Test Connection", ImVec2(150, 0)))
            {
                OnTestApi();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Settings", ImVec2(150, 0)))
            {
                OnSave();
            }

            ImGui::EndTabItem();
        }

        // ----------------- SYSTEM TAB -----------------
        if (ImGui::BeginTabItem("System"))
        {
            ImGui::TextDisabled("System Hotkeys");
            
            std::string toggleStr = m_recordingHotkeyType == 3 ? "Press a key..." : WideToUtf8(HotkeyToString(m_config.toggleWndVk, m_config.toggleWndMod));
            ImGui::Text("Toggle Settings Window:");
            if (ImGui::Button(toggleStr.c_str(), ImVec2(150, 0)))
            {
                m_recordingHotkeyType = 3;
            }

            ImGui::Text("OCR Engine:");
            const char* ocrOptions[] = { "PaddleOCR", "Windows OCR (Default)" };
            int currentOcr = (m_config.ocrType == OcrType::PaddleOCR) ? 0 : 1;
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::Combo("OCR Provider", &currentOcr, ocrOptions, IM_ARRAYSIZE(ocrOptions)))
            {
                m_config.ocrType = (currentOcr == 0) ? OcrType::PaddleOCR : OcrType::WindowsOCR;
            }

            ImGui::EndTabItem();
        }

        // ----------------- OUTPUT LOG TAB -----------------
        if (ImGui::BeginTabItem("Output Log"))
        {
            ImGui::TextDisabled("System Output Log");
            ImGui::Spacing();
            
            ImGui::BeginChild("LogArea", ImVec2(0, -10), true);
            for (const auto& log : m_logs)
            {
                ImGui::TextUnformatted(WideToUtf8(log).c_str());
            }
            if (m_scrollToBottom)
            {
                ImGui::SetScrollHereY(1.0f);
                m_scrollToBottom = false;
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // ----------------- ABOUT TAB -----------------
        #ifndef APP_VERSION
        #define APP_VERSION "v0.0.0-dev"
        #endif

        ImGuiTabItemFlags aboutFlags = 0;
        if (m_switchToAboutTab.load())
        {
            aboutFlags |= ImGuiTabItemFlags_SetSelected;
            m_switchToAboutTab = false;
        }

        if (ImGui::BeginTabItem("About", nullptr, aboutFlags))
        {
            ImGui::TextDisabled("Software Information");
            ImGui::Spacing();
            
            std::string verStr = APP_VERSION;
            ImGui::Text("Current Version: %s", verStr.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
            if (ImGui::Selectable("GitHub Repo"))
            {
                ShellExecuteW(nullptr, L"open", L"https://github.com/wawahuy/light-translate", nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();

            // Render UI depending on the current update state.
            ::UpdateStatus status = m_updater.GetStatus();
            
            if (status == ::UpdateStatus::Idle)
            {
                if (ImGui::Button("Check for Update", ImVec2(150, 0)))
                {
                    m_updater.CheckForUpdateAsync(APP_VERSION);
                }
            }
            else if (status == ::UpdateStatus::Checking)
            {
                ImGui::Text("Checking for updates...");
                static float dotTimer = 0.0f;
                dotTimer += ImGui::GetIO().DeltaTime;
                int dots = static_cast<int>(dotTimer * 2.0f) % 4;
                ImGui::SameLine();
                if (dots == 0) ImGui::Text("");
                else if (dots == 1) ImGui::Text(".");
                else if (dots == 2) ImGui::Text("..");
                else if (dots == 3) ImGui::Text("...");
            }
            else if (status == ::UpdateStatus::UpToDate)
            {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Your application is up-to-date!");
                ImGui::Spacing();
                if (ImGui::Button("Check Again", ImVec2(120, 0)))
                {
                    m_updater.CheckForUpdateAsync(APP_VERSION);
                }
            }
            else if (status == ::UpdateStatus::UpdateAvailable)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.1f, 1.0f), "A new version is available: %s", m_updater.GetLatestVersion().c_str());
                ImGui::Spacing();
                
                ImGui::Text("Changelog:");
                ImGui::BeginChild("ChangelogBox", ImVec2(0, 150), true);
                ImGui::TextWrapped("%s", m_updater.GetReleaseNotes().c_str());
                ImGui::EndChild();
                ImGui::Spacing();
                
                if (ImGui::Button("Download and Update", ImVec2(200, 0)))
                {
                    m_updater.DownloadUpdateAsync();
                }
            }
            else if (status == ::UpdateStatus::Downloading)
            {
                float progress = m_updater.GetDownloadProgress();
                ImGui::Text("Downloading update...");
                ImGui::ProgressBar(progress, ImVec2(-1, 25));
                ImGui::Spacing();
                if (ImGui::Button("Cancel", ImVec2(100, 0)))
                {
                    m_updater.CancelDownload();
                }
            }
            else if (status == ::UpdateStatus::DownloadSuccess)
            {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Download complete!");
                ImGui::Text("The application will restart to complete the update.");
                ImGui::Spacing();
                if (ImGui::Button("Restart & Update Now", ImVec2(200, 0)))
                {
                    m_updater.InstallAndRestart();
                }
            }
            else if (status == ::UpdateStatus::DownloadFailed)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Download failed.");
                std::string err = WideToUtf8(m_updater.GetErrorMessage());
                ImGui::TextWrapped("Error: %s", err.c_str());
                ImGui::Spacing();
                if (ImGui::Button("Retry", ImVec2(120, 0)))
                {
                    m_updater.DownloadUpdateAsync();
                }
            }
            else if (status == ::UpdateStatus::Error)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "An error occurred.");
                std::string err = WideToUtf8(m_updater.GetErrorMessage());
                ImGui::TextWrapped("Error: %s", err.c_str());
                ImGui::Spacing();
                if (ImGui::Button("Try Again", ImVec2(120, 0)))
                {
                    m_updater.CheckForUpdateAsync(APP_VERSION);
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void SettingsWindow::ApplyImGuiStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 6.0f;
    
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = ImVec2(12.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.14f, 0.14f, 0.16f, 0.80f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.11f, 0.11f, 0.13f, 0.95f);
    colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    colors[ImGuiCol_FrameBg]                = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    
    colors[ImGuiCol_TitleBg]                = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.38f, 0.38f, 0.44f, 1.00f);
    
    colors[ImGuiCol_CheckMark]              = ImVec4(0.48f, 0.40f, 0.92f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.48f, 0.40f, 0.92f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.60f, 0.52f, 0.98f, 1.00f);
    
    colors[ImGuiCol_Button]                 = ImVec4(0.24f, 0.22f, 0.33f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.48f, 0.40f, 0.92f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.60f, 0.52f, 0.98f, 1.00f);
    
    colors[ImGuiCol_Header]                 = ImVec4(0.24f, 0.22f, 0.33f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.48f, 0.40f, 0.92f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.60f, 0.52f, 0.98f, 1.00f);
    
    colors[ImGuiCol_Separator]              = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.48f, 0.40f, 0.92f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.60f, 0.52f, 0.98f, 1.00f);
    
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.24f, 0.22f, 0.33f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.48f, 0.40f, 0.92f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.60f, 0.52f, 0.98f, 1.00f);
    
    colors[ImGuiCol_Tab]                    = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.48f, 0.40f, 0.92f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.24f, 0.22f, 0.33f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.24f, 0.22f, 0.33f, 1.00f);
}

void SettingsWindow::LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f * m_dpiScale, &config, io.Fonts->GetGlyphRangesVietnamese());
    if (!font)
    {
        font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\arial.ttf", 18.0f * m_dpiScale, &config, io.Fonts->GetGlyphRangesVietnamese());
    }
    if (!font)
    {
        io.Fonts->AddFontDefault();
    }
}

// -----------------------------------------------------------------------------
//  Capture mode switching
// -----------------------------------------------------------------------------

void SettingsWindow::OnCaptureModeChanged()
{
    // No explicit UI updates required for immediate-mode ImGui
}

void SettingsWindow::OnDisplayModeChanged()
{
    UIToConfig();
    SyncHelperWindows();

    if (m_running)
    {
        m_controller->GetPipeline().SetDisplayMode(m_config.displayMode);
        if (m_config.displayMode == DisplayMode::InPlace)
        {
            m_overlay.SetPosition(m_config.captureRect.left, m_config.captureRect.top);
            m_overlay.SetSize(m_config.captureRect.right - m_config.captureRect.left,
                              m_config.captureRect.bottom - m_config.captureRect.top);
            m_overlay.SetInPlaceText(L"", {});
        }
        else
        {
            m_overlay.SetPosition(m_config.overlayPos.x, m_config.overlayPos.y);
            m_overlay.SetSize(m_config.overlayWidth, m_config.overlayHeight);
            m_overlay.SetText(L"");
        }
        m_overlay.Show();
    }
}

void SettingsWindow::OnRoiActiveChanged()
{
    m_captureHelper.CenterRoi();
    m_captureHelper.ShowRoi(m_config.roiActive && m_captureHelper.IsVisible());
}

// -----------------------------------------------------------------------------
//  Config <-> UI
// -----------------------------------------------------------------------------

void SettingsWindow::ConfigToUI()
{
    strcpy_s(m_apiModelBuf, WideToUtf8(m_config.apiModel).c_str());
    strcpy_s(m_apiKeyBuf, WideToUtf8(m_config.apiKey).c_str());
    strcpy_s(m_fontNameBuf, WideToUtf8(m_config.fontName).c_str());

    ColorrefToFloat(m_config.textColor, m_textColorFloat);
    ColorrefToFloat(m_config.shadowColor, m_shadowColorFloat);
    ColorrefToFloat(m_config.strokeColor, m_strokeColorFloat);

    if ((m_config.roiRect.right - m_config.roiRect.left <= 0) || (m_config.roiRect.bottom - m_config.roiRect.top <= 0))
    {
        m_captureHelper.CenterRoi();
        m_config.roiRect = m_captureHelper.GetRoiRect();
    }
    else
    {
        m_captureHelper.SetRoiRect(m_config.roiRect);
    }
    m_captureHelper.ShowRoi(m_config.roiActive && m_captureHelper.IsVisible());
}

void SettingsWindow::UIToConfig()
{
    m_config.apiModel = Utf8ToWide(m_apiModelBuf);
    m_config.apiKey = Utf8ToWide(m_apiKeyBuf);
    m_config.fontName = Utf8ToWide(m_fontNameBuf);

    m_config.textColor = FloatToColorref(m_textColorFloat);
    m_config.shadowColor = FloatToColorref(m_shadowColorFloat);
    m_config.strokeColor = FloatToColorref(m_strokeColorFloat);

    m_config.roiRect = m_captureHelper.GetRoiRect();
}

std::wstring SettingsWindow::GetRegionInfoText() const
{
    if (m_config.captureSet)
    {
        wchar_t buf[128]{};
        swprintf(buf, 128,
            L"(%ld,%ld) - (%ld,%ld)  [%ldx%ld]",
            m_config.captureRect.left, m_config.captureRect.top,
            m_config.captureRect.right, m_config.captureRect.bottom,
            m_config.captureRect.right - m_config.captureRect.left,
            m_config.captureRect.bottom - m_config.captureRect.top);
        return buf;
    }
    return L"(not set)";
}

void SettingsWindow::UpdateStatus(const std::wstring& text)
{
    std::wstring dispText = text;
    if (dispText.size() > 120)
    {
        dispText = dispText.substr(0, 117) + L"...";
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t line[512]{};
    swprintf_s(line, 512, L"[%02d:%02d:%02d] %ls",
        st.wHour, st.wMinute, st.wSecond, dispText.c_str());

    m_logs.push_back(line);
    if (m_logs.size() > 500)
    {
        m_logs.erase(m_logs.begin());
    }
    m_scrollToBottom = true;
}

// -----------------------------------------------------------------------------
//  Command handlers
// -----------------------------------------------------------------------------

void SettingsWindow::OnStart()
{
    if (m_running) return;

    UIToConfig();

    if (!m_config.captureSet)
    {
        MessageBoxW(m_hwnd, L"Please select a capture region first.", L"No Region Set", MB_ICONWARNING);
        return;
    }

    m_controller->GetConfig() = m_config;

    if (!m_controller->Start(m_hwnd, &m_overlay))
    {
        MessageBoxW(m_hwnd, (L"Start failed:\n" + m_controller->GetLastError()).c_str(), L"Error", MB_ICONERROR);
        return;
    }

    m_captureHelper.Show(false);
    m_overlay.EnableDrag(false);

    m_overlay.SetFontName(m_config.fontName);
    m_overlay.SetFontSize(m_config.fontSize);
    m_overlay.SetTextColor(m_config.textColor);
    m_overlay.SetShadowColor(m_config.shadowColor);
    m_overlay.SetShadowEnabled(m_config.shadowEnabled);
    m_overlay.SetStrokeColor(m_config.strokeColor);
    m_overlay.SetStrokeEnabled(m_config.strokeEnabled);
    m_overlay.SetStrokeWidth(m_config.strokeWidth);

    if (m_config.displayMode == DisplayMode::InPlace)
    {
        m_overlay.SetPosition(m_config.captureRect.left, m_config.captureRect.top);
        m_overlay.SetSize(m_config.captureRect.right - m_config.captureRect.left,
                          m_config.captureRect.bottom - m_config.captureRect.top);
        m_overlay.SetInPlaceText(L"", {});
    }
    else
    {
        m_overlay.SetPosition(m_config.overlayPos.x, m_config.overlayPos.y);
        m_overlay.SetSize(m_config.overlayWidth, m_config.overlayHeight);
        m_overlay.SetText(L"");
    }
    m_overlay.Show();

    if (m_config.captureMode == CaptureMode::Auto)
    {
        RegisterPauseHotkey();
        UpdateStatus(L"Started (Auto mode, interval: " + std::to_wstring(m_config.captureIntervalMs) + L"ms).");
    }
    else
    {
        RegisterCaptureHotkey();
        UpdateStatus(L"Started (Hotkey mode: " + HotkeyToString(m_config.hotkeyVk, m_config.hotkeyMod) + L").");
    }

    m_running = true;

    ShowWindow(m_hwnd, SW_MINIMIZE);
}

void SettingsWindow::OnStop()
{
    if (!m_running) return;

    UnregisterCaptureHotkey();
    UnregisterPauseHotkey();
    m_controller->Stop();

    m_running = false;
    UpdateStatus(L"Stopped.");

    SyncHelperWindows();
}

void SettingsWindow::OnSelectRegion()
{
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    RECT rc = { (sw - 800) / 2, (sh - 150) / 2, (sw + 800) / 2, (sh + 150) / 2 };
    m_captureHelper.SetRect(rc);
    m_config.captureRect = rc;
    m_config.captureSet = true;
    UpdateStatus(L"Capture region reset to screen center.");
}

void SettingsWindow::OnTestApi()
{
    UIToConfig();
    UpdateStatus(L"Testing connection...");

    auto tc = TranslateProviderFactory::CreateProvider(m_config.providerType);
    if (!tc)
    {
        UpdateStatus(L"API Test failed: Invalid translate provider.");
        return;
    }
    tc->SetApiKey(m_config.apiKey);
    tc->SetApiModel(m_config.apiModel);
    tc->SetTargetLanguage(m_config.targetLanguage);

    std::wstring result = tc->Translate(L"Hello world! This is a test connection message.");

    if (!result.empty())
        UpdateStatus(L"Connection OK. Response: " + result);
    else
        UpdateStatus(L"Connection error: " + tc->GetLastError());
}

void SettingsWindow::OnToggleDrag()
{
    if (m_config.displayMode == DisplayMode::InPlace)
    {
        MessageBoxW(m_hwnd, L"Dragging is disabled in In-Place mode since the overlay is automatically locked to the capture region.", L"Information", MB_ICONINFORMATION);
        return;
    }

    bool newMode = !m_overlay.IsDragMode();
    m_overlay.EnableDrag(newMode);
    UpdateStatus(newMode ? L"Drag mode ON - drag the overlay, then lock when done." : L"Drag mode OFF - overlay is click-through.");

    int ox, oy;
    m_overlay.GetPosition(ox, oy);
    m_config.overlayPos = { static_cast<LONG>(ox), static_cast<LONG>(oy) };
}

void SettingsWindow::OnProviderChanged()
{
    if (m_config.providerType == TranslateProvider::DeepSeek)
        UpdateStatus(L"Provider: DeepSeek - uses DeepSeek API via HTTP REST.");
    else
        UpdateStatus(L"Provider: Google Translate - uses public Google Translate API.");
}

void SettingsWindow::OnSave()
{
    UIToConfig();
    m_config.Save(GetIniPath());
    UpdateStatus(L"Settings saved.");
    m_controller->ResetRegionOcr();
}

// -----------------------------------------------------------------------------
//  Hotkey registration
// -----------------------------------------------------------------------------

void SettingsWindow::RegisterCaptureHotkey()
{
    UnregisterCaptureHotkey();
    if (m_config.hotkeyVk == 0)
    {
        UpdateStatus(L"No hotkey configured.");
        return;
    }
    if (RegisterHotKey(m_hwnd, HOTKEY_CAPTURE_ID, m_config.hotkeyMod, m_config.hotkeyVk))
    {
        m_hotkeyRegistered = true;
        UpdateStatus(L"Hotkey registered: " + HotkeyToString(m_config.hotkeyVk, m_config.hotkeyMod));
    }
    else
    {
        UpdateStatus(L"Failed to register hotkey: " + HotkeyToString(m_config.hotkeyVk, m_config.hotkeyMod));
    }
}

void SettingsWindow::UnregisterCaptureHotkey()
{
    if (m_hotkeyRegistered)
    {
        UnregisterHotKey(m_hwnd, HOTKEY_CAPTURE_ID);
        m_hotkeyRegistered = false;
    }
}

void SettingsWindow::RegisterPauseHotkey()
{
    UnregisterPauseHotkey();
    if (m_config.pauseHotkeyVk == 0)
    {
        UpdateStatus(L"No pause hotkey configured.");
        return;
    }
    if (RegisterHotKey(m_hwnd, HOTKEY_PAUSE_ID, m_config.pauseHotkeyMod, m_config.pauseHotkeyVk))
    {
        m_pauseHotkeyRegistered = true;
        UpdateStatus(L"Pause Hotkey registered: " + HotkeyToString(m_config.pauseHotkeyVk, m_config.pauseHotkeyMod));
    }
    else
    {
        UpdateStatus(L"Failed to register pause hotkey: " + HotkeyToString(m_config.pauseHotkeyVk, m_config.pauseHotkeyMod));
    }
}

void SettingsWindow::UnregisterPauseHotkey()
{
    if (m_pauseHotkeyRegistered)
    {
        UnregisterHotKey(m_hwnd, HOTKEY_PAUSE_ID);
        m_pauseHotkeyRegistered = false;
    }
}

void SettingsWindow::RegisterToggleWndHotkey()
{
    UnregisterToggleWndHotkey();
    if (m_config.toggleWndVk == 0)
    {
        UpdateStatus(L"No toggle window hotkey configured.");
        return;
    }
    if (RegisterHotKey(m_hwnd, HOTKEY_TOGGLE_WND_ID, m_config.toggleWndMod, m_config.toggleWndVk))
    {
        m_toggleWndHotkeyRegistered = true;
        UpdateStatus(L"Window toggle hotkey registered: " + HotkeyToString(m_config.toggleWndVk, m_config.toggleWndMod));
    }
    else
    {
        UpdateStatus(L"Failed to register window toggle hotkey: " + HotkeyToString(m_config.toggleWndVk, m_config.toggleWndMod));
    }
}

void SettingsWindow::UnregisterToggleWndHotkey()
{
    if (m_toggleWndHotkeyRegistered)
    {
        UnregisterHotKey(m_hwnd, HOTKEY_TOGGLE_WND_ID);
        m_toggleWndHotkeyRegistered = false;
    }
}

void SettingsWindow::RegisterRegionHotkey()
{
    UnregisterRegionHotkey();
    if (m_config.regionHotkeyVk == 0)
    {
        UpdateStatus(L"No region selection hotkey configured.");
        return;
    }
    if (RegisterHotKey(m_hwnd, HOTKEY_REGION_ID, m_config.regionHotkeyMod, m_config.regionHotkeyVk))
    {
        m_regionHotkeyRegistered = true;
        UpdateStatus(L"Region selection hotkey registered: " + HotkeyToString(m_config.regionHotkeyVk, m_config.regionHotkeyMod));
    }
    else
    {
        UpdateStatus(L"Failed to register region selection hotkey: " + HotkeyToString(m_config.regionHotkeyVk, m_config.regionHotkeyMod));
    }
}

void SettingsWindow::UnregisterRegionHotkey()
{
    if (m_regionHotkeyRegistered)
    {
        UnregisterHotKey(m_hwnd, HOTKEY_REGION_ID);
        m_regionHotkeyRegistered = false;
    }
}

void SettingsWindow::OnHotkey(int id)
{
    if (id == HOTKEY_TOGGLE_WND_ID)
    {
        if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd))
        {
            AddTrayIcon();
            ShowWindow(m_hwnd, SW_HIDE);
        }
        else
        {
            RemoveTrayIcon();
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
        }
        SyncHelperWindows();
        return;
    }

    if (id == HOTKEY_REGION_ID)
    {
        OnRegionHotkeyPressed();
        return;
    }

    if (!m_running) return;

    if (id == HOTKEY_CAPTURE_ID)
    {
        UpdateStatus(L"Hotkey pressed - capturing frame...");
        m_controller->TriggerOnce();
    }
    else if (id == HOTKEY_PAUSE_ID)
    {
        bool currentlyPaused = m_controller->IsPaused();
        m_controller->SetPaused(!currentlyPaused);
        if (!currentlyPaused)
        {
            UpdateStatus(L"Capture paused.");
            m_overlay.SetText(L"Suspended (Paused)");
        }
        else
        {
            UpdateStatus(L"Capture resumed.");
            m_overlay.SetText(L"Resuming...");
        }
    }
}

// -----------------------------------------------------------------------------
//  Region selection trigger & execution
// -----------------------------------------------------------------------------

void SettingsWindow::OnRegionHotkeyPressed()
{
    UpdateStatus(L"Region hotkey pressed. Drag to select region...");
    m_regionResult.Hide();
    m_regionSelect.Show();
}

void SettingsWindow::PerformRegionCapture(const RECT& region)
{
    int rx = region.left;
    int ry = region.top;
    int rw = region.right - region.left;
    int rh = region.bottom - region.top;

    if (rw <= 0 || rh <= 0) return;

    HDC hScreen = GetDC(nullptr);
    HDC hMem = CreateCompatibleDC(hScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreen, rw, rh);
    HGDIOBJ hOld = SelectObject(hMem, hBmp);

    BitBlt(hMem, 0, 0, rw, rh, hScreen, rx, ry, SRCCOPY);
    SelectObject(hMem, hOld);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);

    cv::Mat mat(rh, rw, CV_8UC4);
    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = rw;
    bih.biHeight = -rh;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    HDC hScreenDC = GetDC(nullptr);
    GetDIBits(hScreenDC, hBmp, 0, rh, mat.data, reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS);
    ReleaseDC(nullptr, hScreenDC);

    DeleteObject(hBmp);

    m_controller->GetConfig() = m_config;
    std::wstring ocrText;
    std::wstring result = m_controller->PerformRegionCaptureAndTranslate(mat, ocrText);

    if (result.empty())
    {
        auto* data = new RegionResultData{ region, L"" };
        PostMessageW(m_hwnd, WM_SHOW_REGION_RESULT, 0, reinterpret_cast<LPARAM>(data));
        return;
    }

    auto* data = new RegionResultData{ region, result };
    PostMessageW(m_hwnd, WM_SHOW_REGION_RESULT, 0, reinterpret_cast<LPARAM>(data));
}

// -----------------------------------------------------------------------------
//  System tray
// -----------------------------------------------------------------------------

void SettingsWindow::AddTrayIcon()
{
    if (m_trayAdded) return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Game Translation Overlay");

    Shell_NotifyIconW(NIM_ADD, &nid);
    m_trayAdded = true;
}

void SettingsWindow::RemoveTrayIcon()
{
    if (!m_trayAdded) return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = ID_TRAY_ICON;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayAdded = false;
}

void SettingsWindow::ShowTrayMenu()
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Show Settings");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE_DRAG, L"Toggle Overlay Drag");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}

// -----------------------------------------------------------------------------
//  INI path helper
// -----------------------------------------------------------------------------

std::wstring SettingsWindow::GetIniPath() const
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* dot = wcsrchr(path, L'.');
    if (dot) wcscpy_s(dot, 8, L".ini");
    return path;
}

void SettingsWindow::SyncHelperWindows()
{
    bool showHelpers = IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd);

    if (showHelpers && !m_running)
    {
        m_captureHelper.Show(true);
        m_captureHelper.ShowRoi(m_config.roiActive && m_captureHelper.IsVisible());

        if (m_config.displayMode == DisplayMode::InPlace)
        {
            m_overlay.EnableDrag(false);
            m_overlay.Hide();
        }
        else
        {
            m_overlay.EnableDrag(true);
            
            m_overlay.SetFontName(m_config.fontName);
            m_overlay.SetFontSize(m_config.fontSize);
            m_overlay.SetTextColor(m_config.textColor);
            m_overlay.SetShadowColor(m_config.shadowColor);
            m_overlay.SetShadowEnabled(m_config.shadowEnabled);
            m_overlay.SetStrokeColor(m_config.strokeColor);
            m_overlay.SetStrokeEnabled(m_config.strokeEnabled);
            m_overlay.SetStrokeWidth(m_config.strokeWidth);
            m_overlay.SetPosition(m_config.overlayPos.x, m_config.overlayPos.y);
            m_overlay.SetSize(m_config.overlayWidth, m_config.overlayHeight);
            
            m_overlay.SetText(L"Game Translation Overlay (Preview)\nDrag borders to resize, drag center to move.");
            m_overlay.Show();
        }
    }
    else
    {
        m_captureHelper.Show(false);
        if (!m_running)
        {
            m_overlay.Hide();
        }
    }
}

// -----------------------------------------------------------------------------
//  Hotkey to string helpers
// -----------------------------------------------------------------------------

std::wstring SettingsWindow::VkToName(UINT vk)
{
    switch (vk)
    {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_TAB: return L"Tab";
    case VK_ESCAPE: return L"Esc";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_LEFT: return L"Left";
    case VK_UP: return L"Up";
    case VK_RIGHT: return L"Right";
    case VK_DOWN: return L"Down";
    case VK_SNAPSHOT: return L"PrintScreen";
    case VK_SCROLL: return L"ScrollLock";
    case VK_PAUSE: return L"Pause";
    case VK_CAPITAL: return L"CapsLock";
    case VK_NUMLOCK: return L"NumLock";
    }

    if (vk >= VK_F1 && vk <= VK_F24)
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= 'A' && vk <= 'Z')
        return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= '0' && vk <= '9')
        return std::wstring(1, static_cast<wchar_t>(vk));

    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lParam = scanCode << 16;
    switch (vk)
    {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_DIVIDE:
    case VK_NUMLOCK:
        lParam |= 0x01000000;
        break;
    }

    wchar_t name[64]{};
    if (GetKeyNameTextW(lParam, name, 64) > 0)
    {
        return name;
    }

    return L"VK_" + std::to_wstring(vk);
}

std::wstring SettingsWindow::HotkeyToString(UINT vk, UINT mod)
{
    if (vk == 0) return L"None";
    std::wstring s;
    if (mod & MOD_CONTROL) s += L"Ctrl + ";
    if (mod & MOD_SHIFT)   s += L"Shift + ";
    if (mod & MOD_ALT)     s += L"Alt + ";
    if (mod & MOD_WIN)     s += L"Win + ";
    s += VkToName(vk);
    return s;
}
