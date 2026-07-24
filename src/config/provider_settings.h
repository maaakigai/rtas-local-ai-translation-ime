#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace ime::config {

enum class ProviderMode {
  kLLM,
  kDictionary,
  kMozc,
};

enum class TranslationMode {
  kLLM,
  kDictionary,
};

struct LlmSettings {
  std::string model;
  std::string host = "127.0.0.1";
  int port = 11434;
  std::string path = "/api/generate";
  bool use_tls = false;
  int timeout_ms = 3000;
  std::string keep_alive = "-1";
  bool warmup_on_activate = true;
  int warmup_timeout_ms = 60000;
  bool unload_on_deactivate = true;
  int unload_delay_ms = 10000;
  bool log_timings = true;
};

struct DictionarySettings {
  bool enabled = false;
  std::filesystem::path morph_tsv;
  std::filesystem::path bilingual_tsv;
  bool learning_enable = false;
  std::filesystem::path learning_profile_root;
};

enum class MozcTransport {
  kImm32,
  kBridge,
  kNative,
  kInvalid,
};

enum class MozcNativeBackend {
  kUnset,
  kMozcServerClient,
  kLinkedConverter,
  kInvalid,
};

enum class MozcNativeFallbackPolicy {
  kNone,
  kBridge,
  kImm32,
  kInvalid,
};

struct MozcNativeSettings {
  MozcNativeBackend backend = MozcNativeBackend::kUnset;
  std::string backend_value;
  std::filesystem::path root;
  std::filesystem::path mozc_build_artifact;
  std::filesystem::path wrapper_exe;
  std::filesystem::path server_exe;
  int timeout_ms = 5000;
  int top_n = 8;
  MozcNativeFallbackPolicy fallback_policy = MozcNativeFallbackPolicy::kNone;
  std::string fallback_policy_value = "none";
};

struct MozcSettings {
  bool enabled = false;
  MozcTransport transport = MozcTransport::kImm32;
  std::string transport_value = "imm32";
  int timeout_ms = 50;
  bool kana_kanji_only_mode = false;
  MozcNativeSettings native;
};

struct TranslationCacheLimits {
  std::size_t max_entries = 64;
  std::size_t max_candidates_per_entry = 8;
};

struct RuntimeLimits {
  std::size_t max_preedit_chars = 512;
  std::size_t max_romaji_buffer = 64;
  TranslationCacheLimits translation_cache;
  bool candidate_learning_enabled = false;
};

struct DebugLogFileSettings {
  bool enabled = false;
  std::filesystem::path path = "logs/rtas-debug.log";
  std::size_t max_bytes = 1024 * 1024;
};

struct LoggingSettings {
  DebugLogFileSettings debug_file;
};

struct KanaProviderSettings {
  ProviderMode mode = ProviderMode::kLLM;
  LlmSettings llm;
  std::optional<DictionarySettings> dictionary;
  std::optional<MozcSettings> mozc;
};

struct TranslationProviderSettings {
  TranslationMode mode = TranslationMode::kLLM;
  LlmSettings llm;
  std::optional<DictionarySettings> dictionary;
};

struct ProviderSettings {
  KanaProviderSettings kana;
  TranslationProviderSettings translation;
  RuntimeLimits runtime_limits;
  LoggingSettings logging;
};

ProviderSettings LoadProviderSettings(
    const std::filesystem::path& config_path,
    std::wstring* error = nullptr);

}  // namespace ime::config
