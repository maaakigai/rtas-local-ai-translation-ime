#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../api/conversion_provider.h"
#include "../config/provider_settings.h"
#include "mozc_transport.h"

#include "../../Ime3/rtas_translation.h"

namespace ime::conversion {

class MozcConversionProvider final : public IConversionProvider {
public:
  MozcConversionProvider(const ime::config::MozcSettings& settings,
                         const ime::config::LlmSettings& llmSettings);
  ~MozcConversionProvider() override;

  bool Initialize(std::wstring* error);

  ProviderCapabilities GetCapabilities() const override;
  void SetResultCallback(ProviderCallback callback) override;
  CandidateList FetchLayer1(const LayerRequestContext& ctx) override;
  CandidateList FetchLayer2(const LayerRequestContext& ctx) override;
  CandidateList FetchTranslation(const LayerRequestContext& ctx) override;
  bool Cancel(uint64_t requestId) override;

private:
  static std::wstring TrimWhitespace(const std::wstring& input);
  static std::wstring HiraganaToKatakana(const std::wstring& input);
  static llm::CandidateEntry MakeEntry(const std::wstring& idBase,
                                       const std::wstring& display,
                                       const std::wstring& reading,
                                       llm::CandidateLayer layer);
  void DispatchTranslationResult(uint64_t requestId, TranslationResult result);

  ime::config::MozcSettings settings_;
  std::unique_ptr<IMozcTransport> transport_;
  ProviderCallback callback_;
  std::mutex callbackMutex_;
  AsyncWorkQueue queue_;
  TranslationLlmSettings llmSettings_;
  bool initialized_ = false;
};

}  // namespace ime::conversion
