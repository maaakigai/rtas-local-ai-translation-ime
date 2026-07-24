#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../api/conversion_provider.h"
#include "../config/provider_settings.h"
#include "../dictionary/bilingual_loader.h"
#include "../dictionary/morph_loader.h"
#include "../../Ime3/rtas_translation.h"

namespace ime::conversion {

class DictionaryConversionProvider final : public IConversionProvider {
public:
    DictionaryConversionProvider(const ime::config::DictionarySettings& settings,
                                 std::optional<ime::config::DictionarySettings> translationDictionary,
                                 ime::config::TranslationMode translationMode,
                                 const ime::config::LlmSettings& llmSettings,
                                 const std::filesystem::path& installRoot);

    bool Initialize(std::wstring* error);

    void SetResultCallback(ProviderCallback callback) override;
    CandidateList FetchLayer1(const LayerRequestContext& ctx) override;
    CandidateList FetchLayer2(const LayerRequestContext& ctx) override;
    CandidateList FetchTranslation(const LayerRequestContext& ctx) override;
    bool Cancel(uint64_t requestId) override;

private:
    struct Token {
        std::wstring surface;
        std::wstring reading;
        std::string pos;
        int cost = 0;
        bool unknown = false;
    };

    struct SegmentationResult {
        std::vector<Token> tokens;
        int totalCost = 0;
    };

    SegmentationResult SegmentReading(const std::wstring& reading) const;
    std::filesystem::path ResolveDictionaryPath(const std::filesystem::path& relative) const;
    std::vector<const ime::dictionary::BilingualEntry*> LookupBilingual(const std::wstring& surface) const;
    static llm::CandidateEntry MakeEntry(const std::wstring& idBase,
        const std::wstring& display,
        const std::wstring& reading,
        llm::CandidateLayer layer,
        llm::CandidateSource source);
    void DispatchTranslationResult(uint64_t requestId,
        TranslationResult result,
        const std::wstring& originalReading);
    static std::wstring TrimWhitespace(const std::wstring& input);
    static std::string WideToUtf8(const std::wstring& input);
    static std::wstring Utf8ToWide(const std::string& input);

    ime::config::DictionarySettings settings_;
    std::optional<ime::config::DictionarySettings> translation_dictionary_;
    ime::config::TranslationMode translationMode_;
    std::filesystem::path installRoot_;
    ProviderCallback callback_;
    std::mutex callbackMutex_;
    AsyncWorkQueue queue_;
    TranslationLlmSettings llmSettings_;
    ime::dictionary::MorphDictionary morphDictionary_;
    ime::dictionary::BilingualDictionary bilingualDictionary_;
    bool loaded_ = false;
};

}  // namespace ime::conversion
