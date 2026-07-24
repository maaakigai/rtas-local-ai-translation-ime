#pragma once

#include <mutex>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>

#include "../api/conversion_provider.h"
#include "../config/provider_settings.h"

#include "../../Ime3/rtas_translation.h"

namespace ime::conversion {

class Imm32ConversionProvider final : public IConversionProvider {
public:
    explicit Imm32ConversionProvider(const ime::config::ProviderSettings& settings);
    ~Imm32ConversionProvider() override;

    void SetResultCallback(ProviderCallback callback) override;
    CandidateList FetchLayer1(const LayerRequestContext& ctx) override;
    CandidateList FetchLayer2(const LayerRequestContext& ctx) override;
    CandidateList FetchTranslation(const LayerRequestContext& ctx) override;
    bool Cancel(uint64_t requestId) override;

private:
    static std::wstring HiraganaToKatakana(const std::wstring& input);
    static std::vector<std::wstring> QueryImmConversionCandidates(const std::wstring& reading);
    static llm::CandidateEntry MakeEntry(const std::wstring& idBase,
        const std::wstring& display,
        const std::wstring& reading,
        llm::CandidateLayer layer);

    void DispatchTranslationResult(uint64_t requestId, TranslationResult result);

    ProviderCallback callback_;
    std::mutex callbackMutex_;
    AsyncWorkQueue queue_;
    TranslationLlmSettings llmSettings_;
};

}  // namespace ime::conversion
