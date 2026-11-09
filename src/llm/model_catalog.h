#pragma once

#include <string>
#include <vector>
#include "../config/provider_settings.h"

namespace llm {

struct ModelCatalog {
    std::vector<std::string> models;
    std::wstring error;
};

ModelCatalog FetchModelCatalog(const ime::config::LlmSettings& settings);

}  // namespace llm
