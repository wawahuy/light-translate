#pragma once
#include <windows.h>
#include <vector>
#include <string>

/// Raw captured frame in BGRA format.
struct Frame
{
    std::vector<uint8_t> data;      ///< BGRA pixels, row-major, top-down
    int       width     = 0;
    int       height    = 0;
    ULONGLONG timestamp = 0;        ///< GetTickCount64() at capture time
};

/// Interface for all screen capture engines.
class ICaptureEngine
{
public:
    virtual ~ICaptureEngine() = default;

    /// Initialise the capture resources.
    virtual bool Initialize(int monitorIndex = 0) = 0;

    /// Release all resources.
    virtual void Shutdown() = 0;

    /// Set the screen-coordinate region to capture.
    virtual void SetCaptureRect(const RECT& rc) = 0;

    /// Get the screen-coordinate region to capture.
    virtual RECT GetCaptureRect() const = 0;

    /// Start the capture engine.
    virtual bool Start() = 0;

    /// Stop the capture engine.
    virtual void Stop() = 0;

    /// Retrieve the latest captured frame.
    virtual bool GetLatestFrame(Frame& outFrame) = 0;

    /// Check if the engine is currently running.
    virtual bool IsRunning() const = 0;

    /// Get the last error description.
    virtual std::wstring GetLastError() const = 0;
};
