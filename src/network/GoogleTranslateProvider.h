#pragma once
#include "src/network/ITranslateProvider.h"
#include <string>

// Google Translate implementation of ITranslateProvider.
// Uses the public translate.googleapis.com single-translation endpoint.
class GoogleTranslateProvider : public ITranslateProvider
{
public:
    GoogleTranslateProvider();
    ~GoogleTranslateProvider() override;

    // ITranslateProvider implementation
    void SetApiUrl(const std::wstring& url) override;
    void SetApiKey(const std::wstring& key) override;
    void SetApiModel(const std::wstring& model) override;
    void SetTargetLanguage(const std::wstring& targetLanguage) override;
    std::wstring Translate(const std::wstring& text) override;
    const std::wstring& GetLastError() const override { return m_lastError; }

private:
    std::wstring m_apiUrl;
    std::wstring m_targetLanguage = L"Vietnamese";
    std::wstring m_lastError;
};
