#include "src/capture/DxgiCaptureEngine.h"
#include <algorithm>
#include <cstring>

// ── Constructor / Destructor ──────────────────────────────────────────────────

DxgiCaptureEngine::DxgiCaptureEngine() = default;

DxgiCaptureEngine::~DxgiCaptureEngine()
{
    Stop();
    Shutdown();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool DxgiCaptureEngine::Initialize(int monitorIndex)
{
    m_monitorIndex = monitorIndex;
    return InitDXGI(monitorIndex);
}

void DxgiCaptureEngine::Shutdown()
{
    Stop();
    ReleaseResources();
}

bool DxgiCaptureEngine::Start()
{
    if (m_running.load()) return true;
    if (!m_device)        return false;   // Must call Initialize first

    m_shouldStop.store(false);
    m_running.store(true);
    m_thread = std::thread(&DxgiCaptureEngine::CaptureLoop, this);
    return true;
}

void DxgiCaptureEngine::Stop()
{
    if (!m_running.load()) return;
    m_shouldStop.store(true);
    if (m_thread.joinable())
        m_thread.join();
    m_running.store(false);
}

bool DxgiCaptureEngine::GetLatestFrame(Frame& outFrame)
{
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (!m_hasFrame) return false;
    outFrame = std::move(m_latestFrame); // zero-copy move
    m_hasFrame = false;                  // reset state until next capture
    return true;
}

// ── DXGI Initialisation ───────────────────────────────────────────────────────

bool DxgiCaptureEngine::InitDXGI(int monitorIndex)
{
    // 1. Create D3D11 device (hardware adapter)
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,                       // No D3D11_CREATE_DEVICE_DEBUG in release
        nullptr, 0,
        D3D11_SDK_VERSION,
        &m_device, &featureLevel, &m_context
    );
    if (FAILED(hr))
    {
        m_lastError = L"D3D11CreateDevice failed (hr=" + std::to_wstring(hr) + L")";
        return false;
    }

    // 2. Walk the DXGI chain: Device → IDXGIDevice → Adapter → Output → Output1
    IDXGIDevice* dxgiDev = nullptr;
    hr = m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDev));
    if (FAILED(hr)) { m_lastError = L"QueryInterface(IDXGIDevice) failed"; return false; }

    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDev->GetAdapter(&adapter);
    dxgiDev->Release();
    if (FAILED(hr)) { m_lastError = L"GetAdapter failed"; return false; }

    IDXGIOutput* output = nullptr;
    hr = adapter->EnumOutputs(monitorIndex, &output);
    adapter->Release();
    if (FAILED(hr))
    {
        m_lastError = L"EnumOutputs(" + std::to_wstring(monitorIndex) + L") failed. Invalid monitor index?";
        return false;
    }

    IDXGIOutput1* output1 = nullptr;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
    output->Release();
    if (FAILED(hr)) { m_lastError = L"QueryInterface(IDXGIOutput1) failed"; return false; }

    // 3. Create duplication
    hr = output1->DuplicateOutput(m_device, &m_duplication);
    output1->Release();
    if (FAILED(hr))
    {
        m_lastError = L"DuplicateOutput failed (hr=" + std::to_wstring(hr) +
                      L"). Ensure the app is NOT elevated, or the desktop is not locked.";
        return false;
    }

    return true;
}

void DxgiCaptureEngine::ReleaseResources()
{
    if (m_stagingTex)  { m_stagingTex->Release();  m_stagingTex  = nullptr; m_stagingW = m_stagingH = 0; }
    if (m_duplication) { m_duplication->Release(); m_duplication = nullptr; }
    if (m_context)     { m_context->Release();     m_context     = nullptr; }
    if (m_device)      { m_device->Release();      m_device      = nullptr; }
}

bool DxgiCaptureEngine::RecreateResources()
{
    ReleaseResources();
    return InitDXGI(m_monitorIndex);
}

// ── Staging texture ───────────────────────────────────────────────────────────

bool DxgiCaptureEngine::EnsureStagingTexture(int w, int h)
{
    if (m_stagingTex && m_stagingW == w && m_stagingH == h)
        return true;

    if (m_stagingTex) { m_stagingTex->Release(); m_stagingTex = nullptr; }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width             = static_cast<UINT>(w);
    desc.Height            = static_cast<UINT>(h);
    desc.MipLevels         = 1;
    desc.ArraySize         = 1;
    desc.Format            = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count  = 1;
    desc.Usage             = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags    = D3D11_CPU_ACCESS_READ;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_stagingTex);
    if (FAILED(hr)) { m_stagingTex = nullptr; return false; }

    m_stagingW = w;
    m_stagingH = h;
    return true;
}

// ── Capture loop (runs on worker thread) ─────────────────────────────────────

void DxgiCaptureEngine::CaptureLoop()
{
    while (!m_shouldStop.load())
    {
        if (!m_duplication)
        {
            // Resources were lost; try to recreate
            if (!RecreateResources())
            {
                Sleep(1000);
                continue;
            }
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        IDXGIResource* desktopResource = nullptr;

        // Block up to 100 ms for a new frame (prevents busy-wait)
        HRESULT hr = m_duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            continue;   // No new frame yet, loop again

        if (hr == DXGI_ERROR_ACCESS_LOST ||
            hr == DXGI_ERROR_DEVICE_REMOVED ||
            hr == DXGI_ERROR_DEVICE_RESET)
        {
            // Desktop session switched, UAC prompt, etc. – reinitialise
            m_lastError = L"Desktop Duplication access lost. Reinitialising...";
            ReleaseResources();
            Sleep(500);
            continue;
        }

        if (FAILED(hr))
        {
            if (desktopResource) desktopResource->Release();
            Sleep(50);
            continue;
        }

        // Only process frames that actually contain new pixel data
        if (frameInfo.LastPresentTime.QuadPart != 0 && desktopResource)
            ProcessFrame(desktopResource);

        if (desktopResource) desktopResource->Release();
        m_duplication->ReleaseFrame();
    }
}

bool DxgiCaptureEngine::ProcessFrame(IDXGIResource* resource)
{
    ID3D11Texture2D* srcTex = nullptr;
    HRESULT hr = resource->QueryInterface(
        __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&srcTex));
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcTex->GetDesc(&srcDesc);

    // Clamp capture rect to actual screen bounds
    RECT rc = m_captureRect;
    rc.left   = std::max(0L, std::min(rc.left,   static_cast<LONG>(srcDesc.Width)  - 1));
    rc.top    = std::max(0L, std::min(rc.top,    static_cast<LONG>(srcDesc.Height) - 1));
    rc.right  = std::max(rc.left + 1, std::min(rc.right,  static_cast<LONG>(srcDesc.Width)));
    rc.bottom = std::max(rc.top  + 1, std::min(rc.bottom, static_cast<LONG>(srcDesc.Height)));

    int roiW = rc.right  - rc.left;
    int roiH = rc.bottom - rc.top;

    if (roiW <= 0 || roiH <= 0)
    {
        srcTex->Release();
        return false;
    }

    if (!EnsureStagingTexture(roiW, roiH))
    {
        srcTex->Release();
        return false;
    }

    // Copy just the ROI into the staging texture
    D3D11_BOX srcBox{};
    srcBox.left   = static_cast<UINT>(rc.left);
    srcBox.top    = static_cast<UINT>(rc.top);
    srcBox.right  = static_cast<UINT>(rc.right);
    srcBox.bottom = static_cast<UINT>(rc.bottom);
    srcBox.front  = 0;
    srcBox.back   = 1;

    m_context->CopySubresourceRegion(m_stagingTex, 0, 0, 0, 0, srcTex, 0, &srcBox);
    srcTex->Release();

    // Map the staging texture to read pixel data on CPU
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_context->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    Frame frame;
    frame.width     = roiW;
    frame.height    = roiH;
    frame.timestamp = GetTickCount64();
    frame.data.resize(static_cast<size_t>(roiW) * roiH * 4);

    // Copy rows individually (RowPitch may include padding)
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t*    dst = frame.data.data();
    const size_t rowBytes = static_cast<size_t>(roiW) * 4;
    for (int y = 0; y < roiH; ++y)
        std::memcpy(dst + static_cast<size_t>(y) * rowBytes,
                    src + static_cast<size_t>(y) * mapped.RowPitch,
                    rowBytes);

    m_context->Unmap(m_stagingTex, 0);

    // Publish frame to consumers
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_latestFrame = std::move(frame);
        m_hasFrame    = true;
    }

    return true;
}
