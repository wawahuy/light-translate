#include "src/network/GoogleTranslateProvider.h"
#include "src/utils/StringUtils.h"
#include <sstream>
#include <iomanip>

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
GoogleTranslateProvider::~GoogleTranslateProvider() = default;

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

    std::string responseBody;
    if (!SendHttpRequest(L"GET", url, L"", "", responseBody))
    {
        return {};
    }

    std::wstring result;
    if (!ParseGoogleResponse(responseBody, result))
    {
        m_lastError = L"Failed to parse Google Translate response: " + Utf8ToWide(responseBody);
        return {};
    }

    m_lastError.clear();
    return result;
}
