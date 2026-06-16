#pragma once
#include "src/network/ITranslateProvider.h"
#include "src/network/TranslateProvider.h"
#include <memory>

// Factory to create ITranslateProvider instances based on TranslateProvider type.
class TranslateProviderFactory
{
public:
    static std::unique_ptr<ITranslateProvider> CreateProvider(TranslateProvider type);
};
