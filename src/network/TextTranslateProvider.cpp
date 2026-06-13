#include "src/network/TextTranslateProvider.h"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// Helpers for UTF-8/UTF-16 conversion
static std::string WideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string strTo(sizeNeeded - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &strTo[0], sizeNeeded, nullptr, nullptr);
    return strTo;
}

static std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return {};
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (sizeNeeded <= 0) return {};
    std::wstring wstrTo(sizeNeeded - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstrTo[0], sizeNeeded);
    return wstrTo;
}

static std::string EscapeJsonString(const std::string& input)
{
    std::string output;
    output.reserve(input.length());
    for (char c : input)
    {
        switch (c)
        {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 32)
                {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", static_cast<int>(c));
                    output += buf;
                }
                else
                {
                    output += c;
                }
                break;
        }
    }
    return output;
}

static bool ParseDeepSeekResponse(const std::string& json, std::wstring& outText)
{
    size_t choicesPos = json.find("\"choices\"");
    if (choicesPos == std::string::npos) return false;

    size_t messagePos = json.find("\"message\"", choicesPos);
    if (messagePos == std::string::npos) return false;

    size_t contentPos = json.find("\"content\"", messagePos);
    if (contentPos == std::string::npos) return false;

    size_t colonPos = json.find(':', contentPos);
    if (colonPos == std::string::npos) return false;

    size_t openQ = json.find('"', colonPos);
    if (openQ == std::string::npos) return false;

    size_t i = openQ + 1;
    std::string raw;
    raw.reserve(json.length() - i);
    while (i < json.size() && json[i] != '"')
    {
        if (json[i] == '\\' && i + 1 < json.size())
        {
            i++;
            switch (json[i])
            {
                case '"':  raw += '"';  break;
                case '\\': raw += '\\'; break;
                case '/':  raw += '/';  break;
                case 'n':  raw += '\n'; break;
                case 'r':  raw += '\r'; break;
                case 't':  raw += '\t'; break;
                case 'u':
                {
                    if (i + 4 < json.size())
                    {
                        uint32_t cp = 0;
                        for (int k = 1; k <= 4; ++k)
                        {
                            char c = json[i + k];
                            cp <<= 4;
                            if (c >= '0' && c <= '9') cp |= (c - '0');
                            else if (c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
                        }
                        i += 4;
                        if (cp < 0x80u)
                            raw += static_cast<char>(cp);
                        else if (cp < 0x800u) {
                            raw += static_cast<char>(0xC0u | (cp >> 6));
                            raw += static_cast<char>(0x80u | (cp & 0x3Fu));
                        } else {
                            raw += static_cast<char>(0xE0u | (cp >> 12));
                            raw += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                            raw += static_cast<char>(0x80u | (cp & 0x3Fu));
                        }
                    }
                    break;
                }
                default: raw += json[i]; break;
            }
        }
        else
        {
            raw += json[i];
        }
        i++;
    }

    if (raw.empty()) return false;
    outText = Utf8ToWide(raw);
    return true;
}

TextTranslateProvider::TextTranslateProvider() = default;
TextTranslateProvider::~TextTranslateProvider() = default;

void TextTranslateProvider::SetApiUrl(const std::wstring& url) { m_apiUrl = url; }
void TextTranslateProvider::SetApiKey(const std::wstring& key) { m_apiKey = key; }
void TextTranslateProvider::SetApiModel(const std::wstring& model) { m_apiModel = model; }
void TextTranslateProvider::SetProvider(TranslateProvider provider) { m_provider = provider; }

std::wstring TextTranslateProvider::Translate(const std::wstring& text)
{
    if (m_provider == TranslateProvider::DeepSeek)
    {
        return TranslateDeepSeek(text);
    }
    m_lastError = L"Unsupported provider.";
    return {};
}

std::wstring TextTranslateProvider::TranslateDeepSeek(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }

    std::wstring url = L"https://api.deepseek.com/chat/completions";

    // Parse URL using WinHttpCrackUrl
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t scheme[16]{}, host[256]{}, path[512]{}, extra[256]{};
    uc.lpszScheme      = scheme;  uc.dwSchemeLength      = 16;
    uc.lpszHostName    = host;    uc.dwHostNameLength    = 256;
    uc.lpszUrlPath     = path;    uc.dwUrlPathLength     = 512;
    uc.lpszExtraInfo   = extra;   uc.dwExtraInfoLength   = 256;

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
    {
        m_lastError = L"Invalid API URL: " + url;
        return {};
    }

    const bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    // Build JSON payload
    std::string utf8Text = WideToUtf8(text);
    std::string escapedText = EscapeJsonString(utf8Text);
    std::string utf8Model = WideToUtf8(m_apiModel);
    std::string escapedModel = EscapeJsonString(utf8Model);

    std::string jsonBody = "{\"model\":\"" + escapedModel + "\",\"messages\":[{\"role\":\"system\",\"content\":\"Translate the following text to Vietnamese.\"},{\"role\":\"user\",\"content\":\"" + escapedText + "\"}],\"stream\":false}";

    // Setup session/connection/request
    HINTERNET hSession = WinHttpOpen(
        L"GameTranslate/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) { m_lastError = L"WinHttpOpen failed"; return {}; }

    HINTERNET hConn = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConn)
    {
        WinHttpCloseHandle(hSession);
        m_lastError = L"WinHttpConnect failed";
        return {};
    }

    std::wstring fullPath = path;
    if (extra[0]) fullPath += extra;

    HINTERNET hReq = WinHttpOpenRequest(
        hConn, L"POST", fullPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        isHttps ? WINHTTP_FLAG_SECURE : 0
    );
    if (!hReq)
    {
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession);
        m_lastError = L"WinHttpOpenRequest failed";
        return {};
    }

    // Set authorization headers
    std::wstring authHeader = L"Content-Type: application/json\r\n";
    if (!m_apiKey.empty())
    {
        authHeader += L"Authorization: Bearer " + m_apiKey + L"\r\n";
    }

    WinHttpAddRequestHeaders(hReq, authHeader.c_str(), static_cast<DWORD>(-1L),
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    // Send request
    BOOL ok = WinHttpSendRequest(
        hReq,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        const_cast<char*>(jsonBody.data()), static_cast<DWORD>(jsonBody.size()),
        static_cast<DWORD>(jsonBody.size()),
        0
    );

    if (!ok || !WinHttpReceiveResponse(hReq, nullptr))
    {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession);
        m_lastError = L"HTTP request failed";
        return {};
    }

    // Check HTTP status code
    DWORD statusCode  = 0;
    DWORD statusSize  = sizeof(statusCode);
    WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &statusCode, &statusSize, nullptr);

    if (statusCode != 200)
    {
        // Read response body to extract error message if any
        std::string responseBody;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(hReq, &available) && available > 0)
        {
            std::string chunk(available, '\0');
            DWORD read = 0;
            WinHttpReadData(hReq, chunk.data(), available, &read);
            responseBody.append(chunk.data(), read);
        }

        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession);

        std::wstring errStr = Utf8ToWide(responseBody);
        m_lastError = L"API returned HTTP " + std::to_wstring(statusCode) + L": " + errStr;
        return {};
    }

    // Read response body
    std::string responseBody;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(hReq, &available) && available > 0)
    {
        std::string chunk(available, '\0');
        DWORD read = 0;
        WinHttpReadData(hReq, chunk.data(), available, &read);
        responseBody.append(chunk.data(), read);
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);

    std::wstring result;
    if (!ParseDeepSeekResponse(responseBody, result))
    {
        m_lastError = L"Failed to parse API response: " + Utf8ToWide(responseBody);
        return {};
    }

    m_lastError.clear();
    return result;
}
