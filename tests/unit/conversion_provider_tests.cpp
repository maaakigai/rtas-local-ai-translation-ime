#include <cstdio>
#include <filesystem>
#include <string>

#include "../../src/api/conversion_provider.h"
#include "../../src/provider/dictionary_conversion_provider.h"

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
