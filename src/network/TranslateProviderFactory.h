#pragma once
#include "src/network/ITranslateProvider.h"
#include "src/network/TranslateProvider.h"
#include <memory>

// Configuration for UI visibility/behavior based on the selected provider.
struct ProviderUIConfig
{
    bool showApiModel = true;
    bool showApiKey = true;
};

// Factory to create ITranslateProvider instances and query their UI configurations.
class TranslateProviderFactory
{
public:
    static std::unique_ptr<ITranslateProvider> CreateProvider(TranslateProvider type);
    static ProviderUIConfig GetUIConfig(TranslateProvider type);
};
