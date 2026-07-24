#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "../api/conversion_provider.h"
#include "../config/provider_settings.h"

namespace ime::conversion {

std::unique_ptr<IConversionProvider> CreateConversionProvider(
    const ime::config::ProviderSettings& settings,
    const std::filesystem::path& installRoot,
    std::wstring* error);

}  // namespace ime::conversion

