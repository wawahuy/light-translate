#pragma once
#include "src/capture/ICaptureEngine.h"
#include <memory>

/// Types of screen capture engines.
enum class CaptureType : int
{
    DXGI = 0
    // Future types like GDI, WindowsGraphicsCapture can be added here
};

class CaptureEngineFactory
{
public:
    static std::unique_ptr<ICaptureEngine> CreateEngine(CaptureType type);
};
