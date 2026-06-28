#pragma once
#include "renderer_common.h"
#include <d3d11.h>

class DX11Renderer : public IGraphicsRenderer {
public:
    DX11Renderer();
    ~DX11Renderer() override;

    bool Initialize(HWND hwnd) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    void Resize(int width, int height) override;

private:
    void CleanupDeviceD3D();
    bool CreateDeviceD3D(HWND hwnd);
    void CreateRenderTarget();
    void CleanupRenderTarget();

    HWND m_hwnd = nullptr;
    ID3D11Device* m_d3dDevice = nullptr;
    ID3D11DeviceContext* m_d3dContext = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_renderTargetView = nullptr;
};
