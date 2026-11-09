#pragma once

#include "provider_settings.h"

namespace ime::config {

struct SettingsDocument {
  std::filesystem::path path;
  std::string original_contents;
  ProviderSettings settings;
};

std::filesystem::path RegisteredSettingsPath();

bool LoadSettingsDocument(const std::filesystem::path& path,
                          SettingsDocument& document, std::wstring& error);
bool SaveSettingsDocument(SettingsDocument& document, const LlmSettings& llm,
                          bool kana_kanji_only_mode, std::wstring& error);

}  // namespace ime::config
