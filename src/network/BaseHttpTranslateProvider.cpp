#include "src/network/BaseHttpTranslateProvider.h"
#include "src/utils/StringUtils.h"
#include <sstream>

#pragma comment(lib, "winhttp.lib")

BaseHttpTranslateProvider::BaseHttpTranslateProvider() = default;

BaseHttpTranslateProvider::~BaseHttpTranslateProvider()
{
    CloseConnection();
}

void BaseHttpTranslateProvider::SetApiUrl(const std::wstring& url) { m_apiUrl = url; }
void BaseHttpTranslateProvider::SetApiKey(const std::wstring& key) { m_apiKey = key; }
void BaseHttpTranslateProvider::SetApiModel(const std::wstring& model) { m_apiModel = model; }
void BaseHttpTranslateProvider::SetTargetLanguage(const std::wstring& targetLanguage) { m_targetLanguage = targetLanguage; }

void BaseHttpTranslateProvider::CloseConnection()
{
    if (m_hConn)
    {
        WinHttpCloseHandle(m_hConn);
        m_hConn = nullptr;
    }
    if (m_hSession)
    {
        WinHttpCloseHandle(m_hSession);
        m_hSession = nullptr;
    }
    m_lastHost.clear();
    m_lastPort = 0;
}

bool BaseHttpTranslateProvider::EnsureConnected(const std::wstring& host, int port)
{
    if (m_hConn && (m_lastHost != host || m_lastPort != port))
    {
        CloseConnection();
    }

    if (!m_hSession)
    {
        m_hSession = WinHttpOpen(
            L"GameTranslate/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (!m_hSession) return false;
    }

    if (!m_hConn)
    {
        m_hConn = WinHttpConnect(m_hSession, host.c_str(), static_cast<INTERNET_PORT>(port), 0);
        if (!m_hConn) return false;
        m_lastHost = host;
        m_lastPort = port;
    }

    return true;
}

bool BaseHttpTranslateProvider::SendHttpRequest(
    const std::wstring& method,
    const std::wstring& url,
    const std::wstring& headers,
    const std::string& body,
    std::string& outResponseBody)
{
    outResponseBody.clear();

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t scheme[16]{}, host[256]{}, path[512]{}, extra[1024]{};
    uc.lpszScheme      = scheme;  uc.dwSchemeLength      = 16;
    uc.lpszHostName    = host;    uc.dwHostNameLength    = 256;
    uc.lpszUrlPath     = path;    uc.dwUrlPathLength     = 512;
    uc.lpszExtraInfo   = extra;   uc.dwExtraInfoLength   = 1024;

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
    {
        m_lastError = L"Invalid API URL: " + url;
        return false;
    }

    const bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    auto PerformRequest = [&]() -> bool
    {
        if (!EnsureConnected(host, uc.nPort))
        {
            m_lastError = L"WinHttp connection failed";
            return false;
        }

        std::wstring fullPath = path;
        if (extra[0]) fullPath += extra;

        HINTERNET hReq = WinHttpOpenRequest(
            m_hConn, method.c_str(), fullPath.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            isHttps ? WINHTTP_FLAG_SECURE : 0
        );
        if (!hReq)
        {
            m_lastError = L"WinHttpOpenRequest failed";
            return false;
        }

        if (!headers.empty())
        {
            WinHttpAddRequestHeaders(hReq, headers.c_str(), static_cast<DWORD>(-1L),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        LPVOID pBody = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
        DWORD bodySize = static_cast<DWORD>(body.size());

        BOOL ok = WinHttpSendRequest(
            hReq,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            pBody, bodySize,
            bodySize,
            0
        );

        if (!ok || !WinHttpReceiveResponse(hReq, nullptr))
        {
            WinHttpCloseHandle(hReq);
            m_lastError = L"HTTP request failed or server closed connection";
            return false;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &statusCode, &statusSize, nullptr);

        // Read response body
        std::string response;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(hReq, &available) && available > 0)
        {
            size_t oldSize = response.size();
            response.resize(oldSize + available);
            DWORD read = 0;
            if (WinHttpReadData(hReq, &response[oldSize], available, &read))
            {
                response.resize(oldSize + read);
            }
            else
            {
                response.resize(oldSize);
                break;
            }
        }

        WinHttpCloseHandle(hReq);

        if (statusCode != 200)
        {
            m_lastError = L"API returned HTTP " + std::to_wstring(statusCode) + L": " + Utf8ToWide(response);
            return false;
        }

        outResponseBody = std::move(response);
        return true;
    };

    // Attempt the request
    if (PerformRequest())
    {
        m_lastError.clear();
        return true;
    }

    // Auto-reconnect retry if failed
    CloseConnection();
    if (PerformRequest())
    {
        m_lastError.clear();
        return true;
    }

    return false;
}
