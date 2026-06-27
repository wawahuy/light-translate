#include "src/ocr/OcrFactory.h"
#include "src/ocr/PaddleOcrEngine.h"
#include "src/ocr/WindowsOcrEngine.h"

std::unique_ptr<IOcrEngine> OcrFactory::CreateEngine(OcrType type,
                                                    const std::wstring& detModelDir,
                                                    const std::wstring& recModelDir,
                                                    const std::wstring& langTag)
{
    switch (type)
    {
        case OcrType::PaddleOCR:
            return std::make_unique<PaddleOcrEngine>(detModelDir, recModelDir);
        case OcrType::WindowsOCR:
            return std::make_unique<WindowsOcrEngine>(langTag);
        default:
            return nullptr;
    }
}
