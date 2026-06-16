#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

/// Raw captured frame in BGRA format (matches DXGI_FORMAT_B8G8R8A8_UNORM).
struct Frame
{
    std::vector<uint8_t> data;      ///< BGRA pixels, row-major, top-down
    int       width     = 0;
    int       height    = 0;
    ULONGLONG timestamp = 0;        ///< GetTickCount64() at capture time
};

/// Captures a screen region using the DXGI Desktop Duplication API.
///
/// CPU impact is near-zero: AcquireNextFrame blocks until the GPU presents a
/// new frame (event-driven, no polling).  Only the requested ROI is copied to
/// system memory.
///
/// Thread-safety: Start()/Stop()/GetLatestFrame() may be called from any thread.
class CaptureEngine
{
public:
    CaptureEngine();
    ~CaptureEngine();

    /// Initialise D3D11 + DXGI duplication for the given monitor.
    /// @param monitorIndex  0 = primary monitor, 1 = second, …
    bool Initialize(int monitorIndex = 0);

    /// Release all D3D/DXGI resources.
    void Shutdown();

    /// Set the screen-coordinate region to capture.
    void SetCaptureRect(const RECT& rc) { m_captureRect = rc; }
    RECT GetCaptureRect() const { return m_captureRect; }

    /// Start the internal capture thread.
    bool Start();

    /// Stop the capture thread (blocks until thread exits).
    void Stop();

    /// Copy the most recent frame.  Returns false if no frame has been captured yet.
    bool GetLatestFrame(Frame& outFrame);

    bool         IsRunning()   const { return m_running.load(); }
    std::wstring GetLastError() const { return m_lastError; }

private:
    bool InitDXGI(int monitorIndex);
    void ReleaseResources();
    bool RecreateResources();
    void CaptureLoop();
    bool ProcessFrame(IDXGIResource* resource);
    bool EnsureStagingTexture(int w, int h);

    // ── D3D11 / DXGI objects ──────────────────────────────────────────────────
    ID3D11Device*           m_device      = nullptr;
    ID3D11DeviceContext*    m_context     = nullptr;
    IDXGIOutputDuplication* m_duplication = nullptr;
    ID3D11Texture2D*        m_stagingTex  = nullptr;
    int                     m_stagingW    = 0;
    int                     m_stagingH    = 0;
    int                     m_monitorIndex = 0;

    // ── Capture region ────────────────────────────────────────────────────────
    RECT m_captureRect = { 0, 0, 800, 100 };

    // ── Worker thread ─────────────────────────────────────────────────────────
    std::thread       m_thread;
    std::atomic<bool> m_running   { false };
    std::atomic<bool> m_shouldStop{ false };

    // ── Latest frame (protected by mutex) ────────────────────────────────────
    Frame      m_latestFrame;
    std::mutex m_frameMutex;
    bool       m_hasFrame = false;

    std::wstring m_lastError;
};
