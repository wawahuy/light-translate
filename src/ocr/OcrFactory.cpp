#include "src/ocr/OcrFactory.h"
#include "src/ocr/PaddleOcrEngine.h"

std::unique_ptr<IOcrEngine> OcrFactory::CreateEngine(OcrType type,
                                                    const std::wstring& detModelDir,
                                                    const std::wstring& recModelDir)
{
    switch (type)
    {
        case OcrType::PaddleOCR:
            return std::make_unique<PaddleOcrEngine>(detModelDir, recModelDir);
        default:
            return nullptr;
    }
}
