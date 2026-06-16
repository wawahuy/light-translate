#include "src/network/TranslateProviderFactory.h"
#include "src/network/DeepSeekTranslateProvider.h"
#include "src/network/GoogleTranslateProvider.h"

std::unique_ptr<ITranslateProvider> TranslateProviderFactory::CreateProvider(TranslateProvider type)
{
    switch (type)
    {
        case TranslateProvider::DeepSeek:
            return std::make_unique<DeepSeekTranslateProvider>();
        case TranslateProvider::Google:
            return std::make_unique<GoogleTranslateProvider>();
        case TranslateProvider::OpenAI:
        default:
            // Placeholder for future implementations
            return nullptr;
    }
}

ProviderUIConfig TranslateProviderFactory::GetUIConfig(TranslateProvider type)
{
    ProviderUIConfig config;
    switch (type)
    {
        case TranslateProvider::Google:
            config.showApiModel = false;
            config.showApiKey = false;
            break;
        case TranslateProvider::DeepSeek:
        default:
            config.showApiModel = true;
            config.showApiKey = true;
            break;
    }
    return config;
}
