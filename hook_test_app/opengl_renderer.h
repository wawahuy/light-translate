#pragma once
#include "renderer_common.h"
#include <windows.h>

class OpenGLRenderer : public IGraphicsRenderer {
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    bool Initialize(HWND hwnd) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    void Resize(int width, int height) override;

private:
    bool CreateDeviceWGL(HWND hwnd);
    void CleanupDeviceWGL();

    HWND m_hwnd = nullptr;
    HDC m_deviceContext = nullptr;
    HGLRC m_renderContext = nullptr;
};
