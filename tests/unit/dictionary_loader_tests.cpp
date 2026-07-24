#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../../src/llm/llm_response_parser.h"

#include "../../src/config/provider_settings.h"
#include "../../src/dictionary/bilingual_loader.h"
#include "../../src/dictionary/morph_loader.h"
#include "../../src/provider/mozc_transport.h"
#include "../../src/user_learn/user_learning_store.h"
#include "../../Ime3/rtas_utils.h"

bool RunConversionProviderSmokeTest();

int main() try {
    namespace fs = std::filesystem;
    using ime::config::LoadProviderSettings;
    using ime::config::MozcNativeBackend;
    using ime::config::MozcNativeFallbackPolicy;
    using ime::config::MozcTransport;
    using ime::config::ProviderMode;
    using ime::conversion::CreateMozcTransport;
    using ime::dictionary::BilingualDictionary;
    using ime::dictionary::BilingualDictionaryLoader;
    using ime::dictionary::MorphDictionary;
    using ime::dictionary::MorphDictionaryLoader;
    using ime::learning::CreateFileStore;
    using ime::learning::LearningEvent;

    bool ok = true;
    auto expect = [&](bool cond, const char* message) {
        if (!cond) {
            std::fprintf(stderr, "CHECK FAILED: %s\n", message);
            ok = false;
        }
    };

    const fs::path tempRoot = fs::temp_directory_path() / "ime3_dict_tests";
    fs::create_directories(tempRoot);

    // Validate repository configuration stays on the Mozc bridge path.
    {
        std::wstring error;
        const fs::path repoConfig = fs::path("config") / "ime_settings.json";
        auto defaultSettings = LoadProviderSettings(repoConfig, &error);
        expect(error.empty(), "default config parse");
        expect(defaultSettings.kana.mode == ProviderMode::kMozc, "default kana provider mode is Mozc");
        expect(defaultSettings.translation.llm.keep_alive == "-1",
               "default translation llm keep_alive keeps model resident");
        expect(defaultSettings.translation.llm.warmup_on_activate,
               "default translation llm warmup enabled");
        expect(defaultSettings.translation.llm.warmup_timeout_ms == 60000,
               "default translation llm warmup timeout");
        expect(defaultSettings.translation.llm.unload_on_deactivate,
               "default translation llm unload enabled");
        expect(defaultSettings.translation.llm.unload_delay_ms == 10000,
               "default translation llm unload delay");
        expect(defaultSettings.translation.llm.log_timings,
               "default translation llm timing logs enabled");
        expect(!defaultSettings.logging.debug_file.enabled, "default file logging disabled");
        expect(defaultSettings.logging.debug_file.path == fs::path("logs/rtas-debug.log"),
               "default file logging path");
        expect(defaultSettings.logging.debug_file.max_bytes == 1048576,
               "default file logging max bytes");
        expect(defaultSettings.kana.mozc.has_value(), "default mozc settings populated");
        if (defaultSettings.kana.mozc) {
            expect(defaultSettings.kana.mozc->transport == MozcTransport::kImm32,
                   "default mozc transport is IMM32");
        }
    }

    // Parse optional Ollama residency overrides. Disabling residency is allowed
    // for memory-sensitive environments, but it can reintroduce cold-start delay.
    {
        const fs::path residencyConfig = tempRoot / "llm_residency_overrides.json";
        {
            std::ofstream config(residencyConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"translation\": {\n"
                      "      \"mode\": \"llm\",\n"
                      "      \"llm\": {\n"
                      "        \"keep_alive\": \"30m\",\n"
                      "        \"warmup_on_activate\": false,\n"
                      "        \"warmup_timeout_ms\": 45000,\n"
                      "        \"unload_on_deactivate\": false,\n"
                      "        \"unload_delay_ms\": 0,\n"
                      "        \"log_timings\": false\n"
                      "      }\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(residencyConfig, &error);
        expect(error.empty(), "llm residency config parse");
        expect(settings.translation.llm.keep_alive == "30m",
               "llm keep_alive string parsed");
        expect(!settings.translation.llm.warmup_on_activate,
               "llm warmup can be disabled");
        expect(settings.translation.llm.warmup_timeout_ms == 45000,
               "llm warmup timeout parsed");
        expect(!settings.translation.llm.unload_on_deactivate,
               "llm unload can be disabled");
        expect(settings.translation.llm.unload_delay_ms == 0,
               "llm unload delay parsed");
        expect(!settings.translation.llm.log_timings,
               "llm timing logs can be disabled");
    }

    // Prepare morphological sample TSV.
    const fs::path morphPath = tempRoot / "morph.tsv";
    {
        std::ofstream morph(morphPath, std::ios::binary);
        morph << "surface\treading\tbase_form\tpos\tcost\tfeatures\n";
        morph << "neko\tneko\tneko\tnoun\t-500\tfrequency=high\n";
    }

    MorphDictionary morphDict;
    MorphDictionaryLoader morphLoader;
    auto morphStats = morphLoader.Load(morphPath, morphDict);
    expect(morphStats.parsed_rows == 1, "morph parsed rows");
    expect(morphStats.skipped_bad_columns == 0, "morph skipped columns");
    auto morphHits = morphDict.LookupSurface("neko");
    expect(!morphHits.empty(), "morph lookup hits");
    if (!morphHits.empty()) {
        expect(morphHits.front()->reading == "neko", "morph reading matches");
    }

    // Prepare bilingual sample TSV.
    const fs::path bilingualPath = tempRoot / "bilingual.tsv";
    {
        std::ofstream bilingual(bilingualPath, std::ios::binary);
        bilingual << "headword\tkanji_forms\tkana_forms\tenglish_glosses\tpart_of_speech\tdomains\tmisc\tpriority\n";
        bilingual << "neko\tneko-kanji|cat-graph\tneko-kana\tcat|domestic cat\tnoun\tpets\t\tcommon\n";
    }

    BilingualDictionary bilingualDict;
    BilingualDictionaryLoader bilingualLoader;
    auto bilingualStats = bilingualLoader.Load(bilingualPath, bilingualDict);
    expect(bilingualStats.parsed_rows == 1, "bilingual parsed rows");
    expect(bilingualStats.skipped_bad_columns == 0, "bilingual skipped columns");
    auto headwordHits = bilingualDict.LookupHeadword("neko");
    expect(!headwordHits.empty(), "bilingual headword lookup");
    if (!headwordHits.empty()) {
        expect(!headwordHits.front()->english_glosses.empty(), "bilingual gloss present");
    }
    auto kanaHits = bilingualDict.LookupKana("neko-kana");
    expect(!kanaHits.empty(), "bilingual kana lookup");

    // Parse optional debug file logging configuration.
    {
        const fs::path loggingConfig = tempRoot / "logging_enabled.json";
        {
            std::ofstream config(loggingConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"logging\": {\n"
                      "      \"debug_file\": {\n"
                      "        \"enabled\": true,\n"
                      "        \"path\": \"logs/manual-debug.log\",\n"
                      "        \"max_bytes\": 4096\n"
                      "      }\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(loggingConfig, &error);
        expect(error.empty(), "logging config parse");
        expect(settings.logging.debug_file.enabled, "file logging enabled");
        expect(settings.logging.debug_file.path == fs::path("logs/manual-debug.log"),
               "file logging custom path");
        expect(settings.logging.debug_file.max_bytes == 4096,
               "file logging custom max bytes");
    }
    {
        const fs::path loggingConfig = tempRoot / "logging_empty_path.json";
        {
            std::ofstream config(loggingConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"logging\": {\n"
                      "      \"debug_file\": {\"enabled\": true, \"path\": \"\"}\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(loggingConfig, &error);
        expect(!error.empty(), "logging empty path reports error");
        expect(!settings.logging.debug_file.enabled,
               "logging empty path disables file logging");
    }
    {
        const fs::path debugLogPath = tempRoot / "rtas-debug.log";
        ConfigureDebugLogFile(debugLogPath, true, 4096);
        DebugLog(L"file logging smoke");
        ConfigureDebugLogFile({}, false, 0);

        std::ifstream in(debugLogPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        expect(content.find("file logging smoke") != std::string::npos,
               "debug log file contains message");
        expect(content.find("[TSF-Debug]") != std::string::npos,
               "debug log file contains debug prefix");
    }

    // Parse synthetic Mozc transport configurations.
    {
        const fs::path mozcConfig = tempRoot / "mozc_missing_transport.json";
        {
            std::ofstream config(mozcConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"kana\": {\n"
                      "      \"mode\": \"mozc\",\n"
                      "      \"mozc\": {\"enabled\": true}\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(mozcConfig, &error);
        expect(error.empty(), "missing transport config parse");
        expect(settings.kana.mozc.has_value(), "missing transport mozc settings");
        if (settings.kana.mozc) {
            expect(settings.kana.mozc->transport == MozcTransport::kImm32,
                   "missing transport defaults to imm32");
        }
    }
    {
        const fs::path mozcConfig = tempRoot / "mozc_server_transport.json";
        {
            std::ofstream config(mozcConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"kana\": {\n"
                      "      \"mode\": \"mozc\",\n"
                      "      \"mozc\": {\"enabled\": true, \"transport\": \"server\"}\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(mozcConfig, &error);
        expect(error.empty(), "server transport config parse");
        expect(settings.kana.mozc.has_value(), "server transport mozc settings");
        if (settings.kana.mozc) {
            expect(settings.kana.mozc->transport == MozcTransport::kBridge,
                   "server transport maps to bridge");
            expect(settings.kana.mozc->transport_value == "server",
                   "server transport raw value preserved");
        }
    }
    {
        const fs::path mozcConfig = tempRoot / "mozc_native_transport.json";
        {
            std::ofstream config(mozcConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"kana\": {\n"
                      "      \"mode\": \"mozc\",\n"
                      "      \"mozc\": {\"enabled\": true, \"transport\": \"native\"}\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(mozcConfig, &error);
        expect(error.empty(), "native transport config parse");
        expect(settings.kana.mozc.has_value(), "native transport mozc settings");
        if (settings.kana.mozc) {
            expect(settings.kana.mozc->transport == MozcTransport::kNative,
                   "native transport is parsed as a typed value");
            expect(settings.kana.mozc->native.backend == MozcNativeBackend::kMozcServerClient,
                   "native transport defaults to mozc_server_client backend");
            expect(settings.kana.mozc->native.backend_value == "mozc_server_client",
                   "native backend default raw value");
            expect(settings.kana.mozc->native.fallback_policy == MozcNativeFallbackPolicy::kNone,
                   "native fallback defaults to none");
        }
    }
    {
        const fs::path mozcConfig = tempRoot / "mozc_native_server_client.json";
        {
            std::ofstream config(mozcConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"kana\": {\n"
                      "      \"mode\": \"mozc\",\n"
                      "      \"mozc\": {\n"
                      "        \"enabled\": true,\n"
                      "        \"transport\": \"native\",\n"
                      "        \"native\": {\n"
                      "          \"backend\": \"mozc_server_client\",\n"
                      "          \"root\": \"third_party/mozc\",\n"
                      "          \"mozc_build_artifact\": \"out/mozc/Mozc64.msi\",\n"
                      "          \"wrapper_exe\": \"bin/rtas_mozc_client_probe.exe\",\n"
                      "          \"server_exe\": \"bin/mozc_server.exe\",\n"
                      "          \"timeout_ms\": 7000,\n"
                      "          \"top_n\": 6,\n"
                      "          \"fallback_policy\": \"none\"\n"
                      "        }\n"
                      "      }\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(mozcConfig, &error);
        expect(error.empty(), "native server client config parse");
        expect(settings.kana.mozc.has_value(), "native server client mozc settings");
        if (settings.kana.mozc) {
            expect(settings.kana.mozc->native.backend == MozcNativeBackend::kMozcServerClient,
                   "native backend is mozc_server_client");
            expect(settings.kana.mozc->native.backend_value == "mozc_server_client",
                   "native backend raw value preserved");
            expect(settings.kana.mozc->native.root == fs::path("third_party/mozc"),
                   "native root parsed");
            expect(settings.kana.mozc->native.mozc_build_artifact == fs::path("out/mozc/Mozc64.msi"),
                   "native build artifact parsed");
            expect(settings.kana.mozc->native.wrapper_exe == fs::path("bin/rtas_mozc_client_probe.exe"),
                   "native wrapper executable parsed");
            expect(settings.kana.mozc->native.server_exe == fs::path("bin/mozc_server.exe"),
                   "native server executable parsed");
            expect(settings.kana.mozc->native.timeout_ms == 7000,
                   "native timeout parsed");
            expect(settings.kana.mozc->native.top_n == 6,
                   "native top_n parsed");
            expect(settings.kana.mozc->native.fallback_policy == MozcNativeFallbackPolicy::kNone,
                   "native fallback none parsed");
        }
    }
    {
        const fs::path mozcConfig = tempRoot / "mozc_native_invalid_runtime_values.json";
        {
            std::ofstream config(mozcConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"kana\": {\n"
                      "      \"mode\": \"mozc\",\n"
                      "      \"mozc\": {\n"
                      "        \"enabled\": true,\n"
                      "        \"transport\": \"native\",\n"
                      "        \"native\": {\"timeout_ms\": 0, \"top_n\": 0}\n"
                      "      }\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(mozcConfig, &error);
        expect(!error.empty(), "invalid native runtime values report error");
        expect(settings.kana.mozc.has_value(), "invalid native runtime mozc settings");
        if (settings.kana.mozc) {
            expect(settings.kana.mozc->native.timeout_ms == 0,
                   "invalid native timeout value preserved");
            expect(settings.kana.mozc->native.top_n == 0,
                   "invalid native top_n value preserved");
        }
    }
    {
        const fs::path mozcConfig = tempRoot / "mozc_native_invalid_backend.json";
        {
            std::ofstream config(mozcConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"kana\": {\n"
                      "      \"mode\": \"mozc\",\n"
                      "      \"mozc\": {\n"
                      "        \"enabled\": true,\n"
                      "        \"transport\": \"native\",\n"
                      "        \"native\": {\"backend\": \"private_pipe\"}\n"
                      "      }\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(mozcConfig, &error);
        expect(!error.empty(), "invalid native backend reports error");
        expect(settings.kana.mozc.has_value(), "invalid native backend mozc settings");
        if (settings.kana.mozc) {
            expect(settings.kana.mozc->native.backend == MozcNativeBackend::kInvalid,
                   "invalid native backend does not become server client");
        }
    }
    {
        const fs::path mozcConfig = tempRoot / "mozc_native_bridge_fallback.json";
        {
            std::ofstream config(mozcConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"kana\": {\n"
                      "      \"mode\": \"mozc\",\n"
                      "      \"mozc\": {\n"
                      "        \"enabled\": true,\n"
                      "        \"transport\": \"native\",\n"
                      "        \"native\": {\"backend\": \"mozc_server_client\", \"fallback_policy\": \"bridge\"}\n"
                      "      }\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(mozcConfig, &error);
        expect(!error.empty(), "native bridge fallback reports inactive fallback");
        expect(settings.kana.mozc.has_value(), "native bridge fallback mozc settings");
        if (settings.kana.mozc) {
            expect(settings.kana.mozc->native.fallback_policy == MozcNativeFallbackPolicy::kBridge,
                   "native bridge fallback parsed but inactive");
        }
    }
    {
        const fs::path mozcConfig = tempRoot / "mozc_invalid_transport.json";
        {
            std::ofstream config(mozcConfig, std::ios::binary);
            config << "{\n"
                      "  \"provider\": {\n"
                      "    \"kana\": {\n"
                      "      \"mode\": \"mozc\",\n"
                      "      \"mozc\": {\"enabled\": true, \"transport\": \"bogus\"}\n"
                      "    }\n"
                      "  }\n"
                      "}\n";
        }
        std::wstring error;
        auto settings = LoadProviderSettings(mozcConfig, &error);
        expect(!error.empty(), "invalid transport reports error");
        expect(settings.kana.mozc.has_value(), "invalid transport mozc settings");
        if (settings.kana.mozc) {
            expect(settings.kana.mozc->transport == MozcTransport::kInvalid,
                   "invalid transport does not fall back to imm32");
        }
    }

    {
        ime::config::MozcSettings mozc;
        mozc.transport = MozcTransport::kBridge;
        expect(CreateMozcTransport(mozc) != nullptr, "bridge transport creates transport");
        mozc.transport = MozcTransport::kImm32;
        expect(CreateMozcTransport(mozc) != nullptr, "imm32 transport creates transport");
        mozc.transport = MozcTransport::kNative;
        auto nativeTransport = CreateMozcTransport(mozc);
        expect(nativeTransport != nullptr, "native server client spike creates fail-closed transport");
        std::wstring nativeError;
        expect(!nativeTransport->Initialize(&nativeError),
               "native server client spike does not initialize without artifact");
        expect(nativeError.find(L"mozc_server_client") != std::wstring::npos,
               "native unavailable error names mozc_server_client");
        const fs::path fakeWrapper = tempRoot / "rtas_mozc_client_probe.exe";
        const fs::path fakeServer = tempRoot / "mozc_server.exe";
        {
            std::ofstream wrapper(fakeWrapper, std::ios::binary);
            wrapper << "not a real executable";
            std::ofstream server(fakeServer, std::ios::binary);
            server << "not a real executable";
        }
        mozc.native.wrapper_exe = fakeWrapper;
        mozc.native.server_exe = fakeServer;
        mozc.native.timeout_ms = 5000;
        mozc.native.top_n = 8;
        nativeTransport = CreateMozcTransport(mozc);
        expect(nativeTransport != nullptr, "native app-local transport creates with configured artifacts");
        nativeError.clear();
        expect(nativeTransport->Initialize(&nativeError),
               "native app-local transport initializes when configured artifacts exist");
        expect(nativeError.empty(), "native app-local transport initialize error empty");
        mozc.transport = MozcTransport::kInvalid;
        expect(CreateMozcTransport(mozc) == nullptr, "invalid transport creates no transport");
    }
    {
        auto readEnv = [](const wchar_t* name) {
            DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
            if (required <= 1) {
                return std::wstring();
            }
            std::wstring value(required, L'\0');
            DWORD written = GetEnvironmentVariableW(name, value.data(), required);
            if (written == 0 || written >= required) {
                return std::wstring();
            }
            value.resize(written);
            return value;
        };
        const std::wstring bridgeLive = readEnv(L"RTAS_TEST_MOZC_BRIDGE_LIVE");
        if (bridgeLive == L"1") {
            ime::config::MozcSettings mozc;
            mozc.transport = MozcTransport::kBridge;
            auto bridgeTransport = CreateMozcTransport(mozc);
            expect(bridgeTransport != nullptr, "in-process bridge smoke transport created");
            std::wstring bridgeError;
            expect(bridgeTransport && bridgeTransport->Initialize(&bridgeError),
                   "in-process bridge smoke initializes");
            expect(bridgeError.empty(), "in-process bridge smoke initialize error empty");
            if (bridgeTransport && bridgeError.empty()) {
                ime::conversion::MozcCandidateRequest request;
                request.reading = L"\u306B\u307B\u3093";
                auto response = bridgeTransport->FetchCandidates(request);
                expect(response.error.empty(), "in-process bridge smoke conversion error empty");
                expect(!response.candidates.empty(), "in-process bridge smoke candidates returned");
            }
        }
        const std::wstring wrapperEnv = readEnv(L"RTAS_TEST_MOZC_NATIVE_WRAPPER");
        const std::wstring serverEnv = readEnv(L"RTAS_TEST_MOZC_NATIVE_SERVER");
        if (!wrapperEnv.empty() && !serverEnv.empty()) {
            ime::config::MozcSettings mozc;
            mozc.transport = MozcTransport::kNative;
            mozc.native.backend = MozcNativeBackend::kMozcServerClient;
            mozc.native.backend_value = "mozc_server_client";
            mozc.native.wrapper_exe = wrapperEnv;
            mozc.native.server_exe = serverEnv;
            mozc.native.timeout_ms = 10000;
            mozc.native.top_n = 8;
            auto nativeTransport = CreateMozcTransport(mozc);
            expect(nativeTransport != nullptr, "native external smoke transport created");
            std::wstring nativeError;
            expect(nativeTransport && nativeTransport->Initialize(&nativeError),
                   "native external smoke initializes");
            expect(nativeError.empty(), "native external smoke initialize error empty");
            if (nativeTransport && nativeError.empty()) {
                ime::conversion::MozcCandidateRequest request;
                request.reading = L"\u304D\u3087\u3046\u306F\u3044\u3044\u3066\u3093\u304D\u3067\u3059";
                auto response = nativeTransport->FetchCandidates(request);
                expect(response.error.empty(), "native external smoke conversion error empty");
                expect(!response.candidates.empty(), "native external smoke candidates returned");
                expect(!response.segments.empty(), "native external smoke segments returned");
            }
        }
    }

    // Parse a synthetic dictionary configuration that points at the temp TSVs.
    const fs::path tempConfig = tempRoot / "ime_settings.json";
    {
        std::ofstream config(tempConfig, std::ios::binary);
        config << "{\n"
                  "  \"provider\": {\n"
                  "    \"kana\": {\n"
                  "      \"mode\": \"dictionary\",\n"
                  "      \"llm\": {\"model\": \"default\", \"timeout_ms\": 3000},\n"
                  "      \"dictionary\": {\n"
                  "        \"enabled\": true,\n"
                  "        \"morph_tsv\": \"" << morphPath.generic_string() << "\",\n"
                  "        \"bilingual_tsv\": \"" << bilingualPath.generic_string() << "\",\n"
                  "        \"learning\": {\"enable\": true, \"profile_root\": \"" << (tempRoot / "profiles").generic_string() << "\"}\n"
                  "      }\n"
                  "    }\n"
                  "  }\n"
                  "}\n";
    }

    std::wstring dictError;
    auto dictSettings = LoadProviderSettings(tempConfig, &dictError);
    expect(dictError.empty(), "dictionary config parse");
    expect(dictSettings.kana.mode == ProviderMode::kDictionary, "dictionary provider mode selected");
    expect(dictSettings.kana.dictionary.has_value(), "dictionary settings populated");
    if (dictSettings.kana.dictionary) {
        expect(dictSettings.kana.dictionary->enabled, "dictionary enabled flag set");
        expect(std::filesystem::equivalent(dictSettings.kana.dictionary->morph_tsv, morphPath), "dictionary morph path resolved");
        expect(std::filesystem::equivalent(dictSettings.kana.dictionary->bilingual_tsv, bilingualPath), "dictionary bilingual path resolved");
        expect(dictSettings.kana.dictionary->learning_enable, "dictionary learning enabled");
    }

    // Check dictionary translation candidate shape matches translation layer expectations.
    const std::string query = "neko";
    const auto& matches = !headwordHits.empty() ? headwordHits : kanaHits;
    llm::CandidateEntry entry;
    entry.id = L"dict_translation_test";
    entry.layer = llm::CandidateLayer::Translation;
    entry.source = llm::CandidateSource::Dict;
    entry.reading = std::wstring(query.begin(), query.end());
    entry.metadata.commitMode = llm::CommitMode::Replace;
    entry.metadata.lang = L"ja";
    entry.metadata.partial = false;

    if (!matches.empty() && !matches.front()->english_glosses.empty()) {
        const std::string& gloss = matches.front()->english_glosses.front();
        entry.displayText.assign(gloss.begin(), gloss.end());
        entry.commitText = entry.displayText;
        entry.metadata.lang = L"en";
        entry.confidence = 0.75;
    } else {
        entry.displayText = entry.reading;
        entry.commitText = entry.reading;
        entry.confidence = 0.0;
    }

    expect(entry.layer == llm::CandidateLayer::Translation, "dictionary candidate layer");
    expect(entry.source == llm::CandidateSource::Dict, "dictionary candidate source");
    expect(entry.metadata.commitMode == llm::CommitMode::Replace, "dictionary commit mode");
    expect(entry.displayText == L"cat", "dictionary gloss conversion");
    expect(entry.commitText == entry.displayText, "dictionary commit text sync");
    expect(entry.metadata.lang == L"en", "dictionary target language tag");

    // User learning store smoke test.
    const fs::path learnRoot = tempRoot / "learn_profile";
    auto store = CreateFileStore(learnRoot);
    expect(store != nullptr, "learning store created");
    LearningEvent evt{ "conversion.accepted", "{\"surface\":\"neko\"}" };
    if (store) {
        expect(store->AppendEvent(evt), "learning store append");
        expect(store->Flush(), "learning store flush");
    }

    const fs::path logPath = learnRoot / "events.log";
    std::ifstream logFile(logPath, std::ios::binary);
    std::string line;
    std::getline(logFile, line);
    expect(line.find("conversion.accepted") != std::string::npos, "learning log content");
    logFile.close();

    fs::remove_all(tempRoot);

    bool providerSmoke = RunConversionProviderSmokeTest();
    expect(providerSmoke, "conversion provider smoke test");
    return ok ? 0 : 1;
} catch (const std::exception& ex) {
    std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", ex.what());
    return 1;
} catch (...) {
    std::fprintf(stderr, "UNEXPECTED UNKNOWN FAILURE\n");
    return 1;
}
