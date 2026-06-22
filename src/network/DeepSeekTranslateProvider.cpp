#include "src/network/DeepSeekTranslateProvider.h"
#include "src/utils/StringUtils.h"
#include <windows.h>
#include <sstream>

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

DeepSeekTranslateProvider::DeepSeekTranslateProvider() = default;
DeepSeekTranslateProvider::~DeepSeekTranslateProvider() = default;

std::wstring DeepSeekTranslateProvider::Translate(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }

    std::wstring url = m_apiUrl.empty() ? L"https://api.deepseek.com/chat/completions" : m_apiUrl;

    // Build JSON payload
    std::string utf8Text = WideToUtf8(text);
    std::string escapedText = EscapeJsonString(utf8Text);
    std::string utf8Model = WideToUtf8(m_apiModel);
    std::string escapedModel = EscapeJsonString(utf8Model);
    std::string utf8TargetLang = WideToUtf8(m_targetLanguage);
    std::string escapedTargetLang = EscapeJsonString(utf8TargetLang);

    std::string jsonBody = "{\"model\":\"" + escapedModel + "\",\"messages\":[{\"role\":\"system\",\"content\":\"Translate the input text to " + escapedTargetLang + ". Output ONLY the direct translation without any explanations, notes, or extra conversational text. Keep the original formatting and line breaks.\"},{\"role\":\"user\",\"content\":\"" + escapedText + "\"}],\"stream\":false}";

    // Set authorization headers
    std::wstring authHeader = L"Content-Type: application/json\r\n";
    if (!m_apiKey.empty())
    {
        authHeader += L"Authorization: Bearer " + m_apiKey + L"\r\n";
    }

    std::string responseBody;
    if (!SendHttpRequest(L"POST", url, authHeader, jsonBody, responseBody))
    {
        return {};
    }

    std::wstring result;
    if (!ParseDeepSeekResponse(responseBody, result))
    {
        m_lastError = L"Failed to parse API response: " + Utf8ToWide(responseBody);
        return {};
    }

    m_lastError.clear();
    return result;
}
