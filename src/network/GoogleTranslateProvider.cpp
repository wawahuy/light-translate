#include "src/network/GoogleTranslateProvider.h"
#include "src/utils/StringUtils.h"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// Maps full target language name to ISO 639-1 language code.
static std::wstring MapLanguageToCode(const std::wstring& lang)
{
    if (lang == L"Vietnamese") return L"vi";
    if (lang == L"English") return L"en";
    if (lang == L"Japanese") return L"ja";
    if (lang == L"Chinese (Simplified)") return L"zh-CN";
    if (lang == L"Chinese (Traditional)") return L"zh-TW";
    if (lang == L"Korean") return L"ko";
    if (lang == L"French") return L"fr";
    if (lang == L"German") return L"de";
    if (lang == L"Russian") return L"ru";
    if (lang == L"Spanish") return L"es";
    if (lang == L"Portuguese") return L"pt";
    if (lang == L"Italian") return L"it";
    if (lang == L"Arabic") return L"ar";
    if (lang == L"Thai") return L"th";
    if (lang == L"Indonesian") return L"id";
    if (lang == L"Hindi") return L"hi";
    if (lang == L"Turkish") return L"tr";
    
    return L"en"; // Default fallback
}

// URL encodes a wide string to UTF-8 percent-encoded string.
static std::wstring UrlEncode(const std::wstring& wstr)
{
    std::string str = WideToUtf8(wstr);
    std::ostringstream escaped;
    escaped << std::hex << std::uppercase;
    for (char c : str)
    {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            escaped << c;
        }
        else
        {
            escaped << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return Utf8ToWide(escaped.str());
}

// Custom parser to extract and merge translations from Google Translate JSON arrays.
static bool ParseGoogleResponse(const std::string& json, std::wstring& outText)
{
    if (json.empty() || json[0] != '[') return false;

    std::wstring result;
    size_t pos = json.find("[[[");
    if (pos == std::string::npos)
    {
        return false;
    }

    pos += 3; // Advance past "[[["

    while (pos < json.size())
    {
        if (json[pos] == ']')
        {
            break;
        }

        if (json[pos] == ',')
        {
            pos++;
        }
        if (json[pos] == '[')
        {
            pos++;
        }

        if (json[pos] != '"')
        {
            break;
        }
        pos++; // Skip double quote

        std::string rawSegment;
        while (pos < json.size())
        {
            if (json[pos] == '"')
            {
                pos++;
                break;
            }
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                pos++;
                switch (json[pos])
                {
                    case '"':  rawSegment += '"';  break;
                    case '\\': rawSegment += '\\'; break;
                    case '/':  rawSegment += '/';  break;
                    case 'n':  rawSegment += '\n'; break;
                    case 'r':  rawSegment += '\r'; break;
                    case 't':  rawSegment += '\t'; break;
                    case 'u':
                    {
                        if (pos + 4 < json.size())
                        {
                            uint32_t cp = 0;
                            for (int k = 1; k <= 4; ++k)
                            {
                                char c = json[pos + k];
                                cp <<= 4;
                                if (c >= '0' && c <= '9') cp |= (c - '0');
                                else if (c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
                                else if (c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
                            }
                            pos += 4;
                            if (cp < 0x80u)
                                rawSegment += static_cast<char>(cp);
                            else if (cp < 0x800u) {
                                rawSegment += static_cast<char>(0xC0u | (cp >> 6));
                                rawSegment += static_cast<char>(0x80u | (cp & 0x3Fu));
                            } else {
                                rawSegment += static_cast<char>(0xE0u | (cp >> 12));
                                rawSegment += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                                rawSegment += static_cast<char>(0x80u | (cp & 0x3Fu));
                            }
                        }
                        break;
                    }
                    default: rawSegment += json[pos]; break;
                }
            }
            else
            {
                rawSegment += json[pos];
            }
            pos++;
        }

        result += Utf8ToWide(rawSegment);

        // Skip to end of this segment sub-array
        int bracketDepth = 1;
        while (pos < json.size() && bracketDepth > 0)
        {
            if (json[pos] == '[') bracketDepth++;
            else if (json[pos] == ']') bracketDepth--;
            pos++;
        }
    }

    if (result.empty()) return false;
    outText = result;
    return true;
}

GoogleTranslateProvider::GoogleTranslateProvider() = default;

GoogleTranslateProvider::~GoogleTranslateProvider()
{
    CloseConnection();
}

void GoogleTranslateProvider::CloseConnection()
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

bool GoogleTranslateProvider::EnsureConnected(const std::wstring& host, int port)
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

void GoogleTranslateProvider::SetApiUrl(const std::wstring& url) { m_apiUrl = url; }
void GoogleTranslateProvider::SetApiKey(const std::wstring& /*key*/) {} // Public API doesn't require key
void GoogleTranslateProvider::SetApiModel(const std::wstring& /*model*/) {} // Public API doesn't use model
void GoogleTranslateProvider::SetTargetLanguage(const std::wstring& targetLanguage) { m_targetLanguage = targetLanguage; }

std::wstring GoogleTranslateProvider::Translate(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }

    std::wstring targetLangCode = MapLanguageToCode(m_targetLanguage);
    std::wstring urlEncodedText = UrlEncode(text);

    std::wstring url = m_apiUrl.empty() ? L"https://translate.googleapis.com/translate_a/single" : m_apiUrl;
    url += L"?client=gtx&sl=auto&tl=" + targetLangCode + L"&dt=t&q=" + urlEncodedText;

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
        return {};
    }

    const bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    if (!EnsureConnected(host, uc.nPort))
    {
        m_lastError = L"WinHttp connection failed";
        return {};
    }

    std::wstring fullPath = path;
    if (extra[0]) fullPath += extra;

    HINTERNET hReq = WinHttpOpenRequest(
        m_hConn, L"GET", fullPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        isHttps ? WINHTTP_FLAG_SECURE : 0
    );
    if (!hReq)
    {
        m_lastError = L"WinHttpOpenRequest failed";
        return {};
    }

    // Send HTTP GET request
    BOOL ok = WinHttpSendRequest(
        hReq,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0,
        0,
        0
    );

    // Auto-reconnect retry if connection was reset or closed by server
    if (!ok)
    {
        WinHttpCloseHandle(hReq);
        CloseConnection();
        if (EnsureConnected(host, uc.nPort))
        {
            hReq = WinHttpOpenRequest(
                m_hConn, L"GET", fullPath.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                isHttps ? WINHTTP_FLAG_SECURE : 0
            );
            if (hReq)
            {
                ok = WinHttpSendRequest(
                    hReq,
                    WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0,
                    0,
                    0
                );
            }
        }
    }

    if (!ok || !WinHttpReceiveResponse(hReq, nullptr))
    {
        if (hReq) WinHttpCloseHandle(hReq);
        m_lastError = L"HTTP request failed";
        return {};
    }

    DWORD statusCode  = 0;
    DWORD statusSize  = sizeof(statusCode);
    WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &statusCode, &statusSize, nullptr);

    if (statusCode != 200)
    {
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

        std::wstring errStr = Utf8ToWide(responseBody);
        m_lastError = L"Google Translate returned HTTP " + std::to_wstring(statusCode) + L": " + errStr;
        return {};
    }

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

    std::wstring result;
    if (!ParseGoogleResponse(responseBody, result))
    {
        m_lastError = L"Failed to parse Google Translate response: " + Utf8ToWide(responseBody);
        return {};
    }

    m_lastError.clear();
    return result;
}
