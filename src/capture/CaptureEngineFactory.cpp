#include "src/capture/CaptureEngineFactory.h"
#include "src/capture/DxgiCaptureEngine.h"

std::unique_ptr<ICaptureEngine> CaptureEngineFactory::CreateEngine(CaptureType type)
{
    switch (type)
    {
        case CaptureType::DXGI:
        default:
            return std::make_unique<DxgiCaptureEngine>();
    }
}
