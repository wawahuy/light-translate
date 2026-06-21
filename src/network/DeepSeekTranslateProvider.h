#pragma once
#include "src/network/ITranslateProvider.h"
#include <string>
#include <windows.h>
#include <winhttp.h>

// DeepSeek implementation of ITranslateProvider.
class DeepSeekTranslateProvider : public ITranslateProvider
{
public:
    DeepSeekTranslateProvider();
    ~DeepSeekTranslateProvider() override;

    // ITranslateProvider implementation
    void SetApiUrl(const std::wstring& url) override;
    void SetApiKey(const std::wstring& key) override;
    void SetApiModel(const std::wstring& model) override;
    void SetTargetLanguage(const std::wstring& targetLanguage) override;
    std::wstring Translate(const std::wstring& text) override;
    const std::wstring& GetLastError() const override { return m_lastError; }

private:
    void CloseConnection();
    bool EnsureConnected(const std::wstring& host, int port);

    std::wstring m_apiUrl;
    std::wstring m_apiKey;
    std::wstring m_apiModel;
    std::wstring m_targetLanguage = L"Vietnamese";
    std::wstring m_lastError;

    HINTERNET m_hSession = nullptr;
    HINTERNET m_hConn = nullptr;
    std::wstring m_lastHost;
    int m_lastPort = 0;
};
