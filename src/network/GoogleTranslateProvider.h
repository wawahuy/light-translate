#pragma once
#include "src/network/BaseHttpTranslateProvider.h"
#include <string>

// Google Translate implementation of BaseHttpTranslateProvider.
// Uses the public translate.googleapis.com single-translation endpoint.
class GoogleTranslateProvider : public BaseHttpTranslateProvider
{
public:
    GoogleTranslateProvider();
    ~GoogleTranslateProvider() override;

    // ITranslateProvider implementation
    std::wstring Translate(const std::wstring& text) override;
};
