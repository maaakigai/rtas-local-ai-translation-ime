#pragma once

#include "../config/provider_settings.h"

namespace llm {

struct DeviceApplyResult {
    bool success = false;
    std::wstring error;
};

DeviceApplyResult ApplyLocalDevice(const ime::config::LlmSettings& settings,
                                  const std::filesystem::path& configPath);

}  // namespace llm
