#include "provider_settings.h"

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <windows.h>

#include "../../third_party/nlohmann/json.hpp"

namespace ime::config {
namespace {

DictionarySettings MakeDefaultDictionarySettings() {
  DictionarySettings dict;
  dict.enabled = false;
  dict.morph_tsv = "data/dictionary/morph_dict.tsv";
  dict.bilingual_tsv = "data/dictionary/jmdict.tsv";
  dict.learning_enable = false;
  dict.learning_profile_root = "data/user_learn/profiles";
  return dict;
}

MozcSettings MakeDefaultMozcSettings() {
  MozcSettings mozc;
  mozc.enabled = false;
  mozc.transport = MozcTransport::kBridge;
  mozc.transport_value = "bridge";
  mozc.timeout_ms = 50;
  mozc.kana_kanji_only_mode = false;
  mozc.native.backend = MozcNativeBackend::kUnset;
  mozc.native.backend_value.clear();
  mozc.native.root.clear();
  mozc.native.mozc_build_artifact.clear();
  mozc.native.wrapper_exe.clear();
  mozc.native.server_exe.clear();
  mozc.native.timeout_ms = 5000;
  mozc.native.top_n = 8;
  mozc.native.fallback_policy = MozcNativeFallbackPolicy::kNone;
  mozc.native.fallback_policy_value = "none";
  return mozc;
}

LlmSettings MakeDefaultLlmSettings() {
  LlmSettings llm;
  llm.model = "default";
  llm.host = "127.0.0.1";
  llm.port = 11434;
  llm.path = "/api/generate";
  llm.use_tls = false;
  llm.timeout_ms = 3000;
  llm.keep_alive = "-1";
  llm.warmup_on_activate = true;
  llm.warmup_timeout_ms = 60000;
  llm.unload_on_deactivate = true;
  llm.unload_delay_ms = 10000;
  llm.log_timings = true;
  return llm;
}

RuntimeLimits MakeDefaultRuntimeLimits() {
  RuntimeLimits limits;
  limits.max_preedit_chars = 512;
  limits.max_romaji_buffer = 64;
  limits.translation_cache.max_entries = 64;
  limits.translation_cache.max_candidates_per_entry = 8;
  limits.candidate_learning_enabled = false;
  return limits;
}

LoggingSettings MakeDefaultLoggingSettings() {
  LoggingSettings logging;
  logging.debug_file.enabled = false;
  logging.debug_file.path = "logs/rtas-debug.log";
  logging.debug_file.max_bytes = 1024 * 1024;
  return logging;
}

KanaProviderSettings DefaultKanaSettings() {
  KanaProviderSettings kana;
  kana.mode = ProviderMode::kLLM;
  kana.llm = MakeDefaultLlmSettings();
  kana.dictionary = MakeDefaultDictionarySettings();
  kana.mozc = MakeDefaultMozcSettings();
  return kana;
}

TranslationProviderSettings DefaultTranslationSettings() {
  TranslationProviderSettings trans;
  trans.mode = TranslationMode::kLLM;
  trans.llm = MakeDefaultLlmSettings();
  trans.dictionary = MakeDefaultDictionarySettings();
  return trans;
}

ProviderSettings DefaultSettings() {
  ProviderSettings settings;
  settings.kana = DefaultKanaSettings();
  settings.translation = DefaultTranslationSettings();
  settings.runtime_limits = MakeDefaultRuntimeLimits();
  settings.logging = MakeDefaultLoggingSettings();
  return settings;
}

std::wstring ToWide(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  int required = ::MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(),
      static_cast<int>(utf8.size()), nullptr, 0);
  if (required <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(required), L'\0');
  ::MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(),
      static_cast<int>(utf8.size()), wide.data(), required);
  return wide;
}

void AppendError(std::wstring* error, std::wstring message) {
  if (!error || message.empty()) {
    return;
  }
  if (!error->empty()) {
    error->append(L"; ");
  }
  error->append(std::move(message));
}

template <class Int>
std::optional<Int> ParseInt(const nlohmann::json& node) {
  if (node.is_number_integer()) {
    return static_cast<Int>(node.get<std::int64_t>());
  }
  if (node.is_number_unsigned()) {
    return static_cast<Int>(node.get<std::uint64_t>());
  }
  return std::nullopt;
}

std::optional<std::size_t> ParseSize(const nlohmann::json& node) {
  if (node.is_number_unsigned()) {
    return static_cast<std::size_t>(node.get<std::uint64_t>());
  }
  if (node.is_number_integer()) {
    const auto value = node.get<std::int64_t>();
    if (value >= 0) {
      return static_cast<std::size_t>(value);
    }
  }
  return std::nullopt;
}

DictionarySettings ParseDictionary(const nlohmann::json& node,
                                   const DictionarySettings& defaults) {
  DictionarySettings dict = defaults;
  dict.enabled = node.value("enabled", dict.enabled);
  if (auto morph_it = node.find("morph_tsv");
      morph_it != node.end() && morph_it->is_string()) {
    dict.morph_tsv = morph_it->get<std::string>();
  }
  if (auto bi_it = node.find("bilingual_tsv");
      bi_it != node.end() && bi_it->is_string()) {
    dict.bilingual_tsv = bi_it->get<std::string>();
  }
  if (auto learning_it = node.find("learning");
      learning_it != node.end() && learning_it->is_object()) {
    dict.learning_enable =
        learning_it->value("enable", dict.learning_enable);
    if (auto root_it = learning_it->find("profile_root");
        root_it != learning_it->end() && root_it->is_string()) {
      dict.learning_profile_root = root_it->get<std::string>();
    }
  }
  return dict;
}

LlmSettings ParseLlm(const nlohmann::json& node, const LlmSettings& defaults) {
  LlmSettings llm = defaults;
  if (auto model_it = node.find("model");
      model_it != node.end() && model_it->is_string()) {
    llm.model = *model_it;
  }
  if (auto host_it = node.find("host");
      host_it != node.end() && host_it->is_string()) {
    llm.host = *host_it;
  }
  if (auto port_it = node.find("port");
      port_it != node.end()) {
    if (auto parsed = ParseInt<int>(*port_it)) {
      if (*parsed > 0 && *parsed <= 65535) {
        llm.port = *parsed;
      }
    }
  }
  if (auto path_it = node.find("path");
      path_it != node.end() && path_it->is_string()) {
    llm.path = *path_it;
  }
  if (auto tls_it = node.find("use_tls");
      tls_it != node.end() && tls_it->is_boolean()) {
    llm.use_tls = tls_it->get<bool>();
  }
  if (auto timeout_it = node.find("timeout_ms");
      timeout_it != node.end()) {
    if (auto parsed = ParseInt<int>(*timeout_it)) {
      llm.timeout_ms = *parsed;
    }
  }
  if (auto keep_alive_it = node.find("keep_alive");
      keep_alive_it != node.end()) {
    if (keep_alive_it->is_string()) {
      llm.keep_alive = keep_alive_it->get<std::string>();
    } else if (auto parsed = ParseInt<int>(*keep_alive_it)) {
      llm.keep_alive = std::to_string(*parsed);
    }
  }
  if (auto warmup_it = node.find("warmup_on_activate");
      warmup_it != node.end() && warmup_it->is_boolean()) {
    llm.warmup_on_activate = warmup_it->get<bool>();
  }
  if (auto warmup_timeout_it = node.find("warmup_timeout_ms");
      warmup_timeout_it != node.end()) {
    if (auto parsed = ParseInt<int>(*warmup_timeout_it)) {
      llm.warmup_timeout_ms = *parsed;
    }
  }
  if (auto unload_it = node.find("unload_on_deactivate");
      unload_it != node.end() && unload_it->is_boolean()) {
    llm.unload_on_deactivate = unload_it->get<bool>();
  }
  if (auto unload_delay_it = node.find("unload_delay_ms");
      unload_delay_it != node.end()) {
    if (auto parsed = ParseInt<int>(*unload_delay_it)) {
      llm.unload_delay_ms = *parsed;
    }
  }
  if (auto timings_it = node.find("log_timings");
      timings_it != node.end() && timings_it->is_boolean()) {
    llm.log_timings = timings_it->get<bool>();
  }
  return llm;
}

std::optional<MozcTransport> ParseMozcTransportValue(
    const std::string& value) {
  if (value == "imm32") return MozcTransport::kImm32;
  if (value == "bridge") return MozcTransport::kBridge;
  if (value == "server") return MozcTransport::kBridge;
  if (value == "native") return MozcTransport::kNative;
  return std::nullopt;
}

std::optional<MozcNativeBackend> ParseMozcNativeBackendValue(
    const std::string& value) {
  if (value == "mozc_server_client") return MozcNativeBackend::kMozcServerClient;
  if (value == "linked_converter") return MozcNativeBackend::kLinkedConverter;
  return std::nullopt;
}

std::optional<MozcNativeFallbackPolicy> ParseMozcNativeFallbackPolicyValue(
    const std::string& value) {
  if (value == "none") return MozcNativeFallbackPolicy::kNone;
  if (value == "bridge") return MozcNativeFallbackPolicy::kBridge;
  if (value == "imm32") return MozcNativeFallbackPolicy::kImm32;
  return std::nullopt;
}

MozcNativeSettings ParseMozcNative(
    const nlohmann::json& node,
    const MozcNativeSettings& defaults) {
  MozcNativeSettings native = defaults;
  if (auto backend_it = node.find("backend");
      backend_it != node.end() && backend_it->is_string()) {
    native.backend_value = backend_it->get<std::string>();
    if (auto parsed = ParseMozcNativeBackendValue(native.backend_value)) {
      native.backend = *parsed;
    } else {
      native.backend = MozcNativeBackend::kInvalid;
    }
  }
  if (auto root_it = node.find("root");
      root_it != node.end() && root_it->is_string()) {
    native.root = root_it->get<std::string>();
  }
  if (auto artifact_it = node.find("mozc_build_artifact");
      artifact_it != node.end() && artifact_it->is_string()) {
    native.mozc_build_artifact = artifact_it->get<std::string>();
  }
  if (auto wrapper_it = node.find("wrapper_exe");
      wrapper_it != node.end() && wrapper_it->is_string()) {
    native.wrapper_exe = wrapper_it->get<std::string>();
  }
  if (auto server_it = node.find("server_exe");
      server_it != node.end() && server_it->is_string()) {
    native.server_exe = server_it->get<std::string>();
  }
  if (auto timeout_it = node.find("timeout_ms");
      timeout_it != node.end()) {
    if (auto parsed = ParseInt<int>(*timeout_it)) {
      native.timeout_ms = *parsed;
    }
  }
  if (auto top_n_it = node.find("top_n");
      top_n_it != node.end()) {
    if (auto parsed = ParseInt<int>(*top_n_it)) {
      native.top_n = *parsed;
    }
  }
  if (auto fallback_it = node.find("fallback_policy");
      fallback_it != node.end() && fallback_it->is_string()) {
    native.fallback_policy_value = fallback_it->get<std::string>();
    if (auto parsed =
            ParseMozcNativeFallbackPolicyValue(native.fallback_policy_value)) {
      native.fallback_policy = *parsed;
    } else {
      native.fallback_policy = MozcNativeFallbackPolicy::kInvalid;
    }
  }
  return native;
}

MozcSettings ParseMozc(const nlohmann::json& node, const MozcSettings& defaults) {
  MozcSettings mozc = defaults;
  mozc.enabled = node.value("enabled", mozc.enabled);
  if (auto transport_it = node.find("transport");
      transport_it != node.end() && transport_it->is_string()) {
    mozc.transport_value = transport_it->get<std::string>();
    if (auto parsed = ParseMozcTransportValue(mozc.transport_value)) {
      mozc.transport = *parsed;
    } else {
      mozc.transport = MozcTransport::kInvalid;
    }
  }
  if (auto timeout_it = node.find("timeout_ms");
      timeout_it != node.end()) {
    if (auto parsed = ParseInt<int>(*timeout_it)) {
      mozc.timeout_ms = *parsed;
    }
  }
  if (auto only_it = node.find("kana_kanji_only_mode");
      only_it != node.end() && only_it->is_boolean()) {
    mozc.kana_kanji_only_mode = only_it->get<bool>();
  }
  if (auto native_it = node.find("native");
      native_it != node.end() && native_it->is_object()) {
    mozc.native = ParseMozcNative(*native_it, mozc.native);
  }
  return mozc;
}

LoggingSettings ParseLogging(const nlohmann::json& node,
                             const LoggingSettings& defaults) {
  LoggingSettings logging = defaults;
  if (auto debug_file_it = node.find("debug_file");
      debug_file_it != node.end() && debug_file_it->is_object()) {
    const auto& debug_file = *debug_file_it;
    logging.debug_file.enabled =
        debug_file.value("enabled", logging.debug_file.enabled);
    if (auto path_it = debug_file.find("path");
        path_it != debug_file.end() && path_it->is_string()) {
      logging.debug_file.path = path_it->get<std::string>();
    }
    if (auto max_bytes_it = debug_file.find("max_bytes");
        max_bytes_it != debug_file.end()) {
      if (auto parsed = ParseSize(*max_bytes_it)) {
        logging.debug_file.max_bytes = *parsed;
      }
    }
  }
  return logging;
}

ProviderMode ParseProviderMode(const nlohmann::json& node,
                               ProviderMode fallback) {
  if (node.is_string()) {
    const std::string mode = node.get<std::string>();
    if (mode == "dictionary") return ProviderMode::kDictionary;
    if (mode == "mozc") return ProviderMode::kMozc;
    if (mode == "llm") return ProviderMode::kLLM;
  }
  return fallback;
}

TranslationMode ParseTranslationMode(const nlohmann::json& node,
                                     TranslationMode fallback) {
  if (node.is_string()) {
    const std::string mode = node.get<std::string>();
    if (mode == "dictionary") return TranslationMode::kDictionary;
    if (mode == "llm") return TranslationMode::kLLM;
  }
  return fallback;
}

}  // namespace

ProviderSettings LoadProviderSettings(const std::filesystem::path& config_path,
                                      std::wstring* error) {
  ProviderSettings settings = DefaultSettings();
  if (error) {
    error->clear();
  }

  auto parse_size = [](const nlohmann::json& node, std::size_t fallback) {
    if (node.is_number_unsigned()) {
      return static_cast<std::size_t>(node.get<std::uint64_t>());
    }
    if (node.is_number_integer()) {
      const auto value = node.get<std::int64_t>();
      if (value >= 0) {
        return static_cast<std::size_t>(value);
      }
    }
    return fallback;
  };

  std::ifstream file(config_path, std::ios::binary);
  if (!file) {
    if (error) {
      *error = L"Configuration file not found: " + config_path.wstring();
    }
    return settings;
  }

  try {
    nlohmann::json root =
        nlohmann::json::parse(file, /*callback*/ nullptr, /*allow_exceptions*/ true,
                              /*ignore_comments*/ true);
    if (!root.is_object()) {
      if (error) {
        *error = L"Configuration root must be a JSON object.";
      }
      return settings;
    }

    const auto provider_it = root.find("provider");
    if (provider_it == root.end() || !provider_it->is_object()) {
      if (error) {
        *error = L"Missing 'provider' section in configuration.";
      }
      return settings;
    }

    const auto& provider = *provider_it;

    // Kana (conversion) branch
    if (auto kana_it = provider.find("kana");
        kana_it != provider.end() && kana_it->is_object()) {
      const auto& kana = *kana_it;
      settings.kana.mode = ParseProviderMode(kana.value("mode", ""), settings.kana.mode);
      if (auto llm_it = kana.find("llm");
          llm_it != kana.end() && llm_it->is_object()) {
        settings.kana.llm = ParseLlm(*llm_it, settings.kana.llm);
      }
      if (auto dict_it = kana.find("dictionary");
          dict_it != kana.end() && dict_it->is_object()) {
        settings.kana.dictionary =
            ParseDictionary(*dict_it,
                            settings.kana.dictionary.value_or(MakeDefaultDictionarySettings()));
      }
      if (auto mozc_it = kana.find("mozc");
          mozc_it != kana.end() && mozc_it->is_object()) {
        settings.kana.mozc =
            ParseMozc(*mozc_it,
                      settings.kana.mozc.value_or(MakeDefaultMozcSettings()));
      }
    } else {
      // Backward compatibility
      settings.kana.mode = ParseProviderMode(provider.value("mode", ""), settings.kana.mode);
      if (auto llm_it = provider.find("llm");
          llm_it != provider.end() && llm_it->is_object()) {
        settings.kana.llm = ParseLlm(*llm_it, settings.kana.llm);
      }
      if (auto dict_it = provider.find("dictionary");
          dict_it != provider.end() && dict_it->is_object()) {
        settings.kana.dictionary =
            ParseDictionary(*dict_it,
                            settings.kana.dictionary.value_or(MakeDefaultDictionarySettings()));
      }
      if (auto mozc_it = provider.find("mozc");
          mozc_it != provider.end() && mozc_it->is_object()) {
        settings.kana.mozc =
            ParseMozc(*mozc_it,
                      settings.kana.mozc.value_or(MakeDefaultMozcSettings()));
      }
    }

    // Translation branch
    bool translation_explicit = false;
    if (auto trans_it = provider.find("translation");
        trans_it != provider.end() && trans_it->is_object()) {
      translation_explicit = true;
      const auto& trans = *trans_it;
      settings.translation.mode =
          ParseTranslationMode(trans.value("mode", ""), settings.translation.mode);
      if (auto llm_it = trans.find("llm");
          llm_it != trans.end() && llm_it->is_object()) {
        settings.translation.llm = ParseLlm(*llm_it, settings.translation.llm);
      }
      if (auto dict_it = trans.find("dictionary");
          dict_it != trans.end() && dict_it->is_object()) {
        settings.translation.dictionary =
            ParseDictionary(*dict_it,
                            settings.translation.dictionary.value_or(MakeDefaultDictionarySettings()));
      }
    }

    // Runtime limits
    RuntimeLimits runtime_limits = settings.runtime_limits;
    const auto runtime_it = provider.find("runtime_limits");
    if (runtime_it != provider.end() && runtime_it->is_object()) {
      const auto& runtime = *runtime_it;
      if (auto preedit_it = runtime.find("max_preedit_chars");
          preedit_it != runtime.end()) {
        runtime_limits.max_preedit_chars =
            parse_size(*preedit_it, runtime_limits.max_preedit_chars);
      }
      if (auto buffer_it = runtime.find("max_romaji_buffer");
          buffer_it != runtime.end()) {
        runtime_limits.max_romaji_buffer =
            parse_size(*buffer_it, runtime_limits.max_romaji_buffer);
      }
      if (auto cache_it = runtime.find("translation_cache");
          cache_it != runtime.end() && cache_it->is_object()) {
        const auto& cache = *cache_it;
        if (auto max_entries_it = cache.find("max_entries");
            max_entries_it != cache.end()) {
          runtime_limits.translation_cache.max_entries = parse_size(
              *max_entries_it, runtime_limits.translation_cache.max_entries);
        }
        if (auto max_candidates_it = cache.find("max_candidates_per_entry");
            max_candidates_it != cache.end()) {
          runtime_limits.translation_cache.max_candidates_per_entry =
              parse_size(*max_candidates_it,
                         runtime_limits.translation_cache.max_candidates_per_entry);
        }
      }
      if (auto learning_it = runtime.find("candidate_learning_enabled");
          learning_it != runtime.end() && learning_it->is_boolean()) {
        runtime_limits.candidate_learning_enabled = learning_it->get<bool>();
      }
    }
    settings.runtime_limits = runtime_limits;

    if (auto logging_it = provider.find("logging");
        logging_it != provider.end() && logging_it->is_object()) {
      settings.logging = ParseLogging(*logging_it, settings.logging);
    }

    // If translation not explicitly specified, mirror kana defaults.
    if (!translation_explicit) {
      settings.translation.mode =
          (settings.kana.mode == ProviderMode::kDictionary &&
           settings.kana.dictionary && settings.kana.dictionary->enabled)
              ? TranslationMode::kDictionary
              : TranslationMode::kLLM;
      settings.translation.llm = settings.kana.llm;
      settings.translation.dictionary = settings.kana.dictionary;
    }

    // Validation
    if (settings.kana.mozc &&
        settings.kana.mozc->transport == MozcTransport::kInvalid) {
      AppendError(error,
                  L"Unsupported 'provider.kana.mozc.transport': " +
                      ToWide(settings.kana.mozc->transport_value) +
                      L". Supported: bridge, server, imm32, native.");
    }
    if (settings.kana.mozc &&
        settings.kana.mozc->native.backend == MozcNativeBackend::kInvalid) {
      AppendError(error,
                  L"Unsupported 'provider.kana.mozc.native.backend': " +
                      ToWide(settings.kana.mozc->native.backend_value) +
                      L". Supported: mozc_server_client, linked_converter.");
    }
    if (settings.kana.mozc &&
        settings.kana.mozc->native.fallback_policy ==
            MozcNativeFallbackPolicy::kInvalid) {
      AppendError(error,
                  L"Unsupported 'provider.kana.mozc.native.fallback_policy': " +
                      ToWide(settings.kana.mozc->native.fallback_policy_value) +
                      L". Supported: none, bridge, imm32.");
    }
    if (settings.kana.mozc &&
        settings.kana.mozc->transport == MozcTransport::kNative) {
      if (settings.kana.mozc->native.backend == MozcNativeBackend::kUnset) {
        settings.kana.mozc->native.backend = MozcNativeBackend::kMozcServerClient;
        settings.kana.mozc->native.backend_value = "mozc_server_client";
      }
      if (settings.kana.mozc->native.timeout_ms <= 0) {
        AppendError(error,
                    L"'provider.kana.mozc.native.timeout_ms' must be greater "
                    L"than zero.");
      }
      if (settings.kana.mozc->native.top_n <= 0) {
        AppendError(error,
                    L"'provider.kana.mozc.native.top_n' must be greater than "
                    L"zero.");
      }
      if (settings.kana.mozc->native.fallback_policy ==
          MozcNativeFallbackPolicy::kBridge) {
        AppendError(error,
                    L"'provider.kana.mozc.native.fallback_policy=bridge' is "
                    L"recognized for future native work but not active in this "
                    L"spike; native remains fail-closed.");
      } else if (settings.kana.mozc->native.fallback_policy ==
                 MozcNativeFallbackPolicy::kImm32) {
        AppendError(error,
                    L"'provider.kana.mozc.native.fallback_policy=imm32' is "
                    L"recognized for future native work but not active in this "
                    L"spike; native remains fail-closed.");
      }
    }
    if (settings.logging.debug_file.enabled &&
        settings.logging.debug_file.path.empty()) {
      AppendError(error,
                  L"'provider.logging.debug_file.enabled' is true but "
                  L"'provider.logging.debug_file.path' is empty.");
      settings.logging.debug_file.enabled = false;
    }
    if (settings.kana.mode == ProviderMode::kDictionary) {
      if (!settings.kana.dictionary || !settings.kana.dictionary->enabled) {
        AppendError(error,
                    L"'provider.kana.mode' is 'dictionary' but "
                    L"'provider.kana.dictionary.enabled' is false or missing.");
        settings.kana.mode = ProviderMode::kLLM;
        settings.translation.mode = TranslationMode::kLLM;
      }
    }
    if (settings.kana.mode == ProviderMode::kMozc) {
      if (!settings.kana.mozc || !settings.kana.mozc->enabled) {
        AppendError(error,
                    L"'provider.kana.mode' is 'mozc' but "
                    L"'provider.kana.mozc.enabled' is false or missing.");
        settings.kana.mode = ProviderMode::kLLM;
      }
    }

    if (settings.translation.mode == TranslationMode::kDictionary) {
      if (!settings.translation.dictionary ||
          !settings.translation.dictionary->enabled) {
        AppendError(error,
                    L"'provider.translation.mode' is 'dictionary' but "
                    L"'provider.translation.dictionary.enabled' is false or missing; "
                    L"falling back to 'llm'.");
        settings.translation.mode = TranslationMode::kLLM;
      }
    }
  } catch (const std::exception& ex) {
    if (error) {
      *error = L"Failed to parse provider settings: " + ToWide(ex.what());
    }
    return settings;
  }

  return settings;
}

}  // namespace ime::config
