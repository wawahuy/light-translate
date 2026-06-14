#pragma once
#include <string>
#include "TranslateProvider.h"

class TextTranslateProvider
{
public:
    TextTranslateProvider();
    ~TextTranslateProvider();

    void SetApiUrl(const std::wstring& url);
    void SetApiKey(const std::wstring& key);
    void SetApiModel(const std::wstring& model);
    void SetTargetLanguage(const std::wstring& targetLanguage);
    void SetProvider(TranslateProvider provider);

    std::wstring Translate(const std::wstring& text);
    const std::wstring& GetLastError() const { return m_lastError; }

private:
    std::wstring TranslateDeepSeek(const std::wstring& text);

    std::wstring m_apiUrl;
    std::wstring m_apiKey;
    std::wstring m_apiModel;
    std::wstring m_targetLanguage = L"Vietnamese";
    TranslateProvider m_provider = TranslateProvider::DeepSeek;
    std::wstring m_lastError;
};
