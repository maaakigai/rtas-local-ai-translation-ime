#include <iostream>

#include "../../Ime3/rtas_translation.h"
#include "../../Ime3/rtas_utils.h"
#include "../../src/config/provider_settings.h"
#include "../../src/llm/model_catalog.h"
#include "../../src/llm/local_server.h"
#include "../../third_party/nlohmann/json.hpp"

// Key bindings belong to the TSF DLL and are unused by this translation probe.
extern const GUID GUID_Preserved_Toggle = {};
extern const GUID GUID_Preserved_KANA = {};
extern const GUID GUID_Preserved_KANJI = {};
extern const GUID GUID_Preserved_DBE_ALPHANUMERIC = {};
extern const GUID GUID_Preserved_DBE_HIRAGANA = {};
extern const GUID GUID_Preserved_DBE_KATAKANA = {};
extern const GUID GUID_Preserved_DBE_SBCSCHAR = {};
extern const GUID GUID_Preserved_DBE_DBCSCHAR = {};
extern const GUID GUID_Preserved_DBE_ROMAN = {};

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    std::wstring error;
    const auto config = ime::config::LoadProviderSettings(argv[1], &error);
    const auto& llm = config.translation.llm;
    if (argc > 2 && std::wstring(argv[2]) == L"device") {
        const auto result = ::llm::ApplyLocalDevice(llm, std::filesystem::absolute(argv[1]));
        std::cout << nlohmann::json{{"success", result.success}, {"error", WideToUtf8(result.error)}}.dump() << '\n';
        return result.success ? 0 : 1;
    }
    if (argc > 2 && std::wstring(argv[2]) == L"models") {
        const auto catalog = llm::FetchModelCatalog(llm);
        std::cout << nlohmann::json{{"models", catalog.models}, {"error", WideToUtf8(catalog.error)}}.dump() << '\n';
        return 0;
    }
    const TranslationLlmSettings settings(llm);
    auto cancel = std::make_shared<std::atomic_bool>(argc > 2);
    const auto result = ExecuteTranslationJob(42, L"こんにちは\"\n世界", L"直前の文章", cancel, settings);
    std::cout << nlohmann::json{
        {"success", result.success}, {"cancelled", result.cancelled},
        {"translation", WideToUtf8(result.translation)},
        {"error", WideToUtf8(result.error)}, {"config_error", WideToUtf8(error)},
        {"port", llm.port}, {"path", llm.path}, {"request_id", result.requestId}
    }.dump() << '\n';
}
