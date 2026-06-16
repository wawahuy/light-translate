#pragma once
#include <string>

// Interface for all text translation providers.
class ITranslateProvider
{
public:
    virtual ~ITranslateProvider() = default;

    virtual void SetApiUrl(const std::wstring& url) = 0;
    virtual void SetApiKey(const std::wstring& key) = 0;
    virtual void SetApiModel(const std::wstring& model) = 0;
    virtual void SetTargetLanguage(const std::wstring& targetLanguage) = 0;

    // Translate the given text. Returns empty string on failure.
    virtual std::wstring Translate(const std::wstring& text) = 0;

    // Get the error message of the last failed translation.
    virtual const std::wstring& GetLastError() const = 0;
};
