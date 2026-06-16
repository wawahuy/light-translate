#include "src/network/TranslateProviderFactory.h"
#include "src/network/DeepSeekTranslateProvider.h"

std::unique_ptr<ITranslateProvider> TranslateProviderFactory::CreateProvider(TranslateProvider type)
{
    switch (type)
    {
        case TranslateProvider::DeepSeek:
            return std::make_unique<DeepSeekTranslateProvider>();
        case TranslateProvider::Google:
        case TranslateProvider::OpenAI:
        default:
            // Placeholder for future implementations
            return nullptr;
    }
}
