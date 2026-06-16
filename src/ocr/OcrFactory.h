#pragma once
#include "src/ocr/IOcrEngine.h"
#include <memory>
#include <string>

// Factory to create IOcrEngine instances based on OcrType.
class OcrFactory
{
public:
    static std::unique_ptr<IOcrEngine> CreateEngine(OcrType type,
                                                    const std::wstring& detModelDir = L"",
                                                    const std::wstring& recModelDir = L"");
};
