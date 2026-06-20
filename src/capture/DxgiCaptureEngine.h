#pragma once
#include "src/capture/ICaptureEngine.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

/// Captures a screen region using the DXGI Desktop Duplication API.
class DxgiCaptureEngine : public ICaptureEngine
{
public:
    DxgiCaptureEngine();
    ~DxgiCaptureEngine() override;

    bool Initialize(int monitorIndex = 0) override;
    void Shutdown() override;

    void SetCaptureRect(const RECT& rc) override { m_captureRect = rc; }
    RECT GetCaptureRect() const override { return m_captureRect; }

    bool Start() override;
    void Stop() override;

    bool GetLatestFrame(Frame& outFrame) override;

    bool IsRunning() const override { return m_running.load(); }
    std::wstring GetLastError() const override { return m_lastError; }

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
