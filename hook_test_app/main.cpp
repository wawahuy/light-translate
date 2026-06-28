#include <windows.h>
#include <memory>
#include <string>
#include "renderer_common.h"
#include "dx11_renderer.h"
#include "opengl_renderer.h"
#include "imgui.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

HWND g_hwnd = nullptr;
GraphicsAPI g_currentApi = GraphicsAPI::DirectX11;
GraphicsAPI g_nextApi = GraphicsAPI::DirectX11;
bool g_switchApiRequested = false;
bool g_recreatingWindow = false;
std::unique_ptr<IGraphicsRenderer> g_renderer = nullptr;

int g_windowX = 100;
int g_windowY = 100;
int g_windowWidth = 1024;
int g_windowHeight = 768;

int g_subtitleIndex = 0;
DWORD g_lastSubtitleUpdateTime = 0;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_SIZE:
            if (g_renderer && wParam != SIZE_MINIMIZED) {
                g_renderer->Resize((UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
            }
            return 0;
        case WM_MOVE: {
            RECT rect;
            if (GetWindowRect(hWnd, &rect)) {
                g_windowX = rect.left;
                g_windowY = rect.top;
                g_windowWidth = rect.right - rect.left;
                g_windowHeight = rect.bottom - rect.top;
            }
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            if (!g_recreatingWindow) {
                PostQuitMessage(0);
            }
            return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { 
        sizeof(wc), 
        CS_CLASSDC, 
        WndProc, 
        0L, 
        0L, 
        hInstance, 
        nullptr, 
        nullptr, 
        nullptr, 
        nullptr, 
        L"GraphicsHookTestClass", 
        nullptr 
    };
    return ::RegisterClassExW(&wc) != 0;
}

HWND CreateAppWindow(HINSTANCE hInstance, const std::wstring& apiName) {
    std::wstring windowTitle = L"Graphics Hooking Test Application - Rendering: " + apiName;

    HWND hwnd = ::CreateWindowExW(
        0,
        L"GraphicsHookTestClass", 
        windowTitle.c_str(), 
        WS_OVERLAPPEDWINDOW, 
        g_windowX, 
        g_windowY, 
        g_windowWidth, 
        g_windowHeight, 
        nullptr, 
        nullptr, 
        hInstance, 
        nullptr
    );

    return hwnd;
}

bool InitializeRenderer(HWND hwnd, GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::DirectX11:
            g_renderer = std::make_unique<DX11Renderer>();
            break;
        case GraphicsAPI::OpenGL:
            g_renderer = std::make_unique<OpenGLRenderer>();
            break;
    }

    if (!g_renderer->Initialize(hwnd)) {
        g_renderer.reset();
        return false;
    }
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nShowCmd;

    if (!RegisterWindowClass(hInstance)) {
        return 1;
    }

    std::string cmdLine(lpCmdLine);
    if (cmdLine.find("-opengl") != std::string::npos) {
        g_currentApi = GraphicsAPI::OpenGL;
        g_nextApi = GraphicsAPI::OpenGL;
    } else {
        g_currentApi = GraphicsAPI::DirectX11;
        g_nextApi = GraphicsAPI::DirectX11;
    }

    std::wstring apiName = L"DirectX 11";
    if (g_currentApi == GraphicsAPI::OpenGL) {
        apiName = L"OpenGL 3";
    }

    g_hwnd = CreateAppWindow(hInstance, apiName);
    if (g_hwnd == nullptr) {
        return 1;
    }

    if (!InitializeRenderer(g_hwnd, g_currentApi)) {
        ::MessageBoxW(nullptr, L"Failed to initialize graphics backend.", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    g_lastSubtitleUpdateTime = GetTickCount();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }

        if (done) break;

        if (g_switchApiRequested) {
            g_switchApiRequested = false;
            g_recreatingWindow = true;

            g_renderer->Shutdown();
            g_renderer.reset();

            DestroyWindow(g_hwnd);

            g_currentApi = g_nextApi;
            std::wstring nextApiName = L"DirectX 11";
            if (g_currentApi == GraphicsAPI::OpenGL) {
                nextApiName = L"OpenGL 3";
            }

            g_hwnd = CreateAppWindow(hInstance, nextApiName);
            if (g_hwnd == nullptr) {
                g_recreatingWindow = false;
                return 1;
            }

            if (!InitializeRenderer(g_hwnd, g_currentApi)) {
                ::MessageBoxW(nullptr, L"Failed to initialize graphics backend. Reverting to DirectX 11.", L"Error", MB_ICONERROR | MB_OK);
                g_currentApi = GraphicsAPI::DirectX11;
                g_nextApi = GraphicsAPI::DirectX11;
                
                DestroyWindow(g_hwnd);
                
                g_hwnd = CreateAppWindow(hInstance, L"DirectX 11");
                InitializeRenderer(g_hwnd, g_currentApi);
            }

            g_recreatingWindow = false;

            ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
            ::UpdateWindow(g_hwnd);
            
            continue;
        }

        DWORD currentTime = GetTickCount();
        if (currentTime - g_lastSubtitleUpdateTime >= 4000) {
            g_subtitleIndex = (g_subtitleIndex + 1) % g_Subtitles.size();
            g_lastSubtitleUpdateTime = currentTime;
        }

        g_renderer->BeginFrame();

        {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(350.0f, 160.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Graphics API Switcher Control Panel");

            ImGui::Text("Current Renderer API: %s", 
                g_currentApi == GraphicsAPI::DirectX11 ? "DirectX 11" : "OpenGL 3"
            );
            ImGui::Separator();

            ImGui::Text("Select rendering backend to switch:");
            
            if (ImGui::Button("Switch to DirectX 11", ImVec2(250.0f, 30.0f))) {
                if (g_currentApi != GraphicsAPI::DirectX11) {
                    g_nextApi = GraphicsAPI::DirectX11;
                    g_switchApiRequested = true;
                }
            }

            if (ImGui::Button("Switch to OpenGL 3", ImVec2(250.0f, 30.0f))) {
                if (g_currentApi != GraphicsAPI::OpenGL) {
                    g_nextApi = GraphicsAPI::OpenGL;
                    g_switchApiRequested = true;
                }
            }

            ImGui::End();
        }

        {
            RECT rect;
            GetClientRect(g_hwnd, &rect);
            float width = (float)(rect.right - rect.left);
            float height = (float)(rect.bottom - rect.top);

            float subWindowWidth = width * 0.9f;
            float subWindowHeight = 110.0f;
            float subWindowX = (width - subWindowWidth) / 2.0f;
            float subWindowY = height - subWindowHeight - 20.0f;

            ImGui::SetNextWindowPos(ImVec2(subWindowX, subWindowY));
            ImGui::SetNextWindowSize(ImVec2(subWindowWidth, subWindowHeight));

            ImGuiWindowFlags subFlags = ImGuiWindowFlags_NoDecoration | 
                                        ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoSavedSettings | 
                                        ImGuiWindowFlags_NoFocusOnAppearing | 
                                        ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoInputs;

            ImGui::Begin("Subtitles Window", nullptr, subFlags);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 pMin = ImGui::GetWindowPos();
            ImVec2 pMax = ImVec2(pMin.x + subWindowWidth, pMin.y + subWindowHeight);
            drawList->AddRectFilled(pMin, pMax, IM_COL32(0, 0, 0, 166), 10.0f);

            std::string text = g_Subtitles[g_subtitleIndex];
            
            ImGui::SetWindowFontScale(1.5f);
            
            ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            float textX = (subWindowWidth - textSize.x) / 2.0f;
            float textY = (subWindowHeight - textSize.y) / 2.0f;

            ImGui::SetCursorPos(ImVec2(textX, textY));
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "%s", text.c_str());

            ImGui::End();
        }

        g_renderer->EndFrame();
    }

    g_renderer->Shutdown();
    g_renderer.reset();

    DestroyWindow(g_hwnd);
    UnregisterClassW(L"GraphicsHookTestClass", hInstance);

    return 0;
}
