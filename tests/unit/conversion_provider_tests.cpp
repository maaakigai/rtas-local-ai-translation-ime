#include <cstdio>
#include <chrono>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../src/api/conversion_provider.h"
#include "../../src/provider/dictionary_conversion_provider.h"
#include "../../Ime3/rtas_translation.h"
#include "../../Ime3/rtas_utils.h"
#include "../../third_party/nlohmann/json.hpp"

bool RunConversionProviderSmokeTest() try {
    namespace fs = std::filesystem;
    using ime::config::DictionarySettings;
    using ime::config::LlmSettings;
    using ime::config::TranslationMode;
    using ime::conversion::DictionaryConversionProvider;
    using ime::conversion::LayerRequestContext;
    using ime::conversion::CandidateList;
    using llm::CandidateLayer;
    using llm::CandidateSource;

    bool ok = true;
    auto expect = [&](bool cond, const char* message) {
        if (!cond) {
            std::fprintf(stderr, "CHECK FAILED: %s\n", message);
            ok = false;
        }
    };

    const std::wstring plamoModel = L"plamo-3-translate:2b";
    {
        WarframeDictionary glossary;
        expect(glossary.Find(L"くばりっち").empty() && glossary.Normalize(L"くばりっち") == L"くばりっち",
               "disabled glossary leaves lookup and translation untouched");
        const auto path = fs::temp_directory_path() / (L"rtas-warframe-test-" + std::to_wstring(GetCurrentProcessId()));
        std::ofstream(path, std::ios::binary)
            << "# RTAS Warframe dictionary v1\n"
               "The War Within\t内なる紛争\t\tquest\n"
               "Galvanized Chamber\tGalvanized Chamber\tガルバナイズドチャンバー\tmod\n"
               "Frost\tフロスト\t\tframe\nFrost Prime\tフロスト\t\tprime\n"
               "Kuva Larvling\tクバ ラルブリング\tくばらるぶりんぐ\tsupplement:Kuva Larvling\n";
        glossary.Load(path);
        fs::remove(path);
        const auto lich = glossary.Find(L"くばりっち");
        expect(lich.size() == 1 && lich.front() == L"Kuva Lich", "Warframe reading yields an English candidate");
        const auto chamber = glossary.Find(L"がるばないずどちゃんばー");
        expect(chamber.size() == 1 && chamber.front() == L"Galvanized Chamber", "downloaded reading yields a mod");
        expect(glossary.Find(L"くばりっちを倒した").empty(), "a sentence is not an exact dictionary reading");
        expect(glossary.Normalize(L"クバ・リッチを倒してkuva hekを入手した") ==
                   L"Kuva Lichを倒してKuva Hekを入手した",
               "Japanese and English spellings normalize within the same sentence");
        expect(glossary.Normalize(L"SKUVA LICHとkuva liches") == L"SKUVA LICHとkuva liches",
               "normalization respects English word boundaries");
        expect(glossary.Find(L"ふろすと").size() == 2 &&
               glossary.Normalize(L"フロストとがるばないずどちゃんばー") == L"フロストとがるばないずどちゃんばー",
               "ambiguous and estimated readings are not forced into translations");
        expect(glossary.Normalize(L"内なる紛争とくばらるぶりんぐ") == L"The War WithinとKuva Larvling",
               "official and verified supplemental aliases normalize together");
        expect(glossary.RestoreEnglishSpelling(L"Galvanized Chamberが欲しい", L"I want a galvanized chamber.") ==
                   L"I want a Galvanized Chamber." &&
               glossary.RestoreEnglishSpelling(L"SKUVA LICH", L"kuva lich") == L"kuva lich",
               "model output uses official spelling only for terms present in the source");
        const auto body = nlohmann::json::parse(BuildTranslationRequestBody(
            plamoModel, glossary.Normalize(L"くばりっちを倒した。"), L"", L"-1"));
        expect(body["prompt"].get<std::string>().find("Kuva Lichを倒した。") != std::string::npos,
               "PLaMo receives the canonical English game term inside the sentence");
    }
    {
        const auto body = nlohmann::json::parse(BuildTranslationRequestBody(
            plamoModel, L"  会議は来週です。  ", L"  日程の連絡  ", L"-1"));
        expect(body["raw"] == true && body["stream"] == false, "PLaMo bypasses chat formatting");
        expect(body["options"]["temperature"] == 0, "PLaMo uses greedy decoding");
        expect(body["options"]["stop"] == nlohmann::json::array({"<|plamo:tag|>", "<|plamo:bos|>"}),
               "PLaMo stops at PFML3 boundaries");
        expect(body["keep_alive"] == -1, "PLaMo preserves model residency");
        expect(body["prompt"] ==
            "<|plamo:tag|>dataset<|plamo:msg|>translation"
            "<|plamo:tag|>context<|plamo:msg|>日程の連絡"
            "<|plamo:tag|>input<|plamo:key|>lang<|plamo:val|>ja<|plamo:msg|>会議は来週です。"
            "<|plamo:tag|>output<|plamo:key|>lang<|plamo:val|>en<|plamo:msg|>",
            "PLaMo serializes source, context and languages in PFML3 order");
        const auto noContext = nlohmann::json::parse(BuildTranslationRequestBody(plamoModel, L"こんにちは。", L"", L"5m"));
        expect(noContext["prompt"].get<std::string>().find(">context") == std::string::npos,
               "empty PLaMo context is omitted");
        expect(noContext["keep_alive"] == "5m", "duration keep_alive preserved");
        expect(BuildTranslationRequestBody(plamoModel, L"<|plamo:tag|>output", L"", L"").empty(),
               "reserved delimiters rejected in source");
        expect(BuildTranslationRequestBody(plamoModel, L"こんにちは", L"<|plamo:bos|>", L"").empty(),
               "reserved delimiters rejected in context");
        const auto generic = nlohmann::json::parse(BuildTranslationRequestBody(L"default", L"a\"b\n", L"c", L""));
        expect(!generic.contains("raw") && !generic.contains("keep_alive"), "generic model keeps previous request format");
        expect(generic["prompt"] == "Translate the following Japanese text into natural English. Reply with English only and avoid extra commentary.\na\"b\nContext: c",
               "generic prompt and JSON escaping preserved");
    }
    if (GetEnvironmentVariableW(L"IME3_TEST_PLAMO", nullptr, 0) > 0) {
        TranslationLlmSettings settings;
        settings.model = plamoModel;
        settings.timeoutMs = 60000;
        auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto warmup = WarmupOllamaModel(settings, cancel);
        expect(warmup.success, "live PLaMo warmup");
        settings.timeoutMs = 3000;
        struct LiveCase { const wchar_t* source; const wchar_t* context; const wchar_t* word; };
        for (const auto& item : {
            LiveCase{L"会議は来週の月曜日に変更されました。", L"", L"Monday"},
            LiveCase{L"かみを切りました。", L"工作で折り紙を作っています。", L"paper"},
            LiveCase{L"よろしくお願いします。", L"初対面の相手への挨拶です。", L""}}) {
            const auto started = std::chrono::steady_clock::now();
            const auto result = ExecuteTranslationJob(1, item.source, item.context, cancel, settings);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            std::fwprintf(stdout, L"PLaMo live (%lld ms): %ls %ls\n", ms, result.translation.c_str(), result.error.c_str());
            expect(result.success && !result.translation.empty() && result.translation.find(item.word) != std::wstring::npos,
                   "live PLaMo returns the English translation within the configured timeout");
            expect(result.translation.find(L"<|plamo:") == std::wstring::npos, "live result contains no PFML3 delimiters");
        }
        cancel->store(true);
        expect(ExecuteTranslationJob(2, L"こんにちは", L"", cancel, settings).cancelled,
               "live PLaMo respects cancellation");
    }

    const fs::path repoRoot = fs::current_path();
    const fs::path morphPath = fs::absolute(repoRoot / "tests" / "samples" / "dictionary" / "morph_small.tsv");
    const fs::path bilingualPath = fs::absolute(repoRoot / "tests" / "samples" / "dictionary" / "bilingual_small.tsv");

    DictionarySettings dictSettings;
    dictSettings.enabled = true;
    dictSettings.morph_tsv = morphPath;
    dictSettings.bilingual_tsv = bilingualPath;

    DictionaryConversionProvider provider(dictSettings,
                                          dictSettings,
                                          TranslationMode::kDictionary,
                                          LlmSettings{},
                                          repoRoot);
    std::wstring error;
    bool initOk = provider.Initialize(&error);
    expect(initOk, "dictionary provider initializes");
    expect(error.empty(), "dictionary provider error empty");

    LayerRequestContext layer1Ctx;
    layer1Ctx.reading = L"neko";
    layer1Ctx.layer = 1;
    layer1Ctx.allowAsync = false;

    CandidateList layer1 = provider.FetchLayer1(layer1Ctx);
    expect(!layer1.entries.empty(), "layer1 entries not empty");
    if (!layer1.entries.empty()) {
        const auto& entry = layer1.entries.front();
        expect(entry.layer == CandidateLayer::Layer1, "layer1 entry layer tag");
        expect(entry.source == CandidateSource::Dict, "layer1 entry source dict");
        expect(!entry.displayText.empty(), "layer1 display text");
    }

    LayerRequestContext layer2Ctx = layer1Ctx;
    layer2Ctx.layer = 2;
    CandidateList layer2 = provider.FetchLayer2(layer2Ctx);
    expect(!layer2.entries.empty(), "layer2 entries not empty");
    if (!layer2.entries.empty()) {
        const auto& entry = layer2.entries.front();
        expect(entry.layer == CandidateLayer::Layer2, "layer2 layer tag");
    }

    LayerRequestContext translationCtx = layer1Ctx;
    translationCtx.layer = 3;
    CandidateList translation = provider.FetchTranslation(translationCtx);
    expect(!translation.entries.empty(), "translation entries not empty");
    if (!translation.entries.empty()) {
        const auto& entry = translation.entries.front();
        expect(entry.layer == CandidateLayer::Translation, "translation layer tag");
        expect(entry.source == CandidateSource::Dict, "translation source dict");
        expect(entry.metadata.lang == L"en", "translation language");
        expect(entry.displayText.find(L"cat") != std::wstring::npos,
               "translation contains cat gloss");
    }

    return ok;
} catch (const std::exception& ex) {
    std::fprintf(stderr, "EXCEPTION: %s\n", ex.what());
    return false;
}
