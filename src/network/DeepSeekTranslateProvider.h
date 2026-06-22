#pragma once
#include "src/network/BaseHttpTranslateProvider.h"
#include <string>

// DeepSeek implementation of BaseHttpTranslateProvider.
class DeepSeekTranslateProvider : public BaseHttpTranslateProvider
{
public:
    DeepSeekTranslateProvider();
    ~DeepSeekTranslateProvider() override;

    // ITranslateProvider implementation
    std::wstring Translate(const std::wstring& text) override;
};
