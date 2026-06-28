#include "opengl_renderer.h"
#include <GL/gl.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer() {
    Shutdown();
}

bool OpenGLRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    if (!CreateDeviceWGL(hwnd)) {
        CleanupDeviceWGL();
        return false;
    }

    wglMakeCurrent(m_deviceContext, m_renderContext);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void OpenGLRenderer::Shutdown() {
    if (ImGui::GetCurrentContext()) {
        wglMakeCurrent(m_deviceContext, m_renderContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    CleanupDeviceWGL();
}

void OpenGLRenderer::BeginFrame() {
    wglMakeCurrent(m_deviceContext, m_renderContext);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    glViewport(0, 0, 1024, 768);
    RECT rect;
    if (GetClientRect(m_hwnd, &rect)) {
        glViewport(0, 0, rect.right - rect.left, rect.bottom - rect.top);
    }
    
    glClearColor(0.12f, 0.18f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void OpenGLRenderer::EndFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SwapBuffers(m_deviceContext);
}

void OpenGLRenderer::Resize(int width, int height) {
    if (width == 0 || height == 0) return;
    if (m_deviceContext && m_renderContext) {
        wglMakeCurrent(m_deviceContext, m_renderContext);
        glViewport(0, 0, width, height);
    }
}

bool OpenGLRenderer::CreateDeviceWGL(HWND hwnd) {
    m_deviceContext = GetDC(hwnd);
    if (!m_deviceContext) return false;

    PIXELFORMATDESCRIPTOR pfd;
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(m_deviceContext, &pfd);
    if (pf == 0) return false;

    if (!SetPixelFormat(m_deviceContext, pf, &pfd)) return false;

    m_renderContext = wglCreateContext(m_deviceContext);
    if (!m_renderContext) return false;

    return true;
}

void OpenGLRenderer::CleanupDeviceWGL() {
    if (m_renderContext) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_renderContext);
        m_renderContext = nullptr;
    }
    if (m_deviceContext && m_hwnd) {
        ReleaseDC(m_hwnd, m_deviceContext);
        m_deviceContext = nullptr;
    }
}
