#pragma once
#include "src/network/ITranslateProvider.h"
#include <string>
#include <windows.h>
#include <winhttp.h>

// Base class for HTTP-based translation providers.
// Manages WinHTTP session, connections, headers, request sending and retries.
class BaseHttpTranslateProvider : public ITranslateProvider
{
public:
    BaseHttpTranslateProvider();
    ~BaseHttpTranslateProvider() override;

    // ITranslateProvider implementation
    void SetApiUrl(const std::wstring& url) override;
    void SetApiKey(const std::wstring& key) override;
    void SetApiModel(const std::wstring& model) override;
    void SetTargetLanguage(const std::wstring& targetLanguage) override;

    const std::wstring& GetLastError() const override { return m_lastError; }

protected:
    // Execute an HTTP request (GET, POST, etc.) and retrieve response.
    // Handles connection creation/reuse and auto-reconnect retry on failure.
    bool SendHttpRequest(
        const std::wstring& method,
        const std::wstring& url,
        const std::wstring& headers,
        const std::string& body,
        std::string& outResponseBody);

    std::wstring m_apiUrl;
    std::wstring m_apiKey;
    std::wstring m_apiModel;
    std::wstring m_targetLanguage = L"Vietnamese";
    std::wstring m_lastError;

private:
    void CloseConnection();
    bool EnsureConnected(const std::wstring& host, int port);

    HINTERNET m_hSession = nullptr;
    HINTERNET m_hConn = nullptr;
    std::wstring m_lastHost;
    int m_lastPort = 0;
};
