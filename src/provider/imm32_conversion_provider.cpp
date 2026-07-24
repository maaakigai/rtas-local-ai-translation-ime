#include "imm32_conversion_provider.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>

#include "../config/provider_settings.h"
#include "../../Ime3/rtas_utils.h"

#pragma comment(lib, "Imm32.lib")

namespace ime::conversion {

namespace {

std::wstring TrimWhitespace(const std::wstring& input) {
    std::wstring result = input;
    auto is_space = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };
    while (!result.empty() && is_space(result.front())) {
        result.erase(result.begin());
    }
    while (!result.empty() && is_space(result.back())) {
        result.pop_back();
    }
    return result;
}

}  // namespace

Imm32ConversionProvider::Imm32ConversionProvider(const ime::config::ProviderSettings& settings) {
    llmSettings_.model = Utf8ToWide(settings.translation.llm.model);
    llmSettings_.host = Utf8ToWide(settings.translation.llm.host);
    llmSettings_.port = settings.translation.llm.port > 0
                            ? static_cast<uint16_t>(settings.translation.llm.port)
                            : static_cast<uint16_t>(11434);
    llmSettings_.path = Utf8ToWide(settings.translation.llm.path);
    llmSettings_.useTls = settings.translation.llm.use_tls;
    llmSettings_.timeoutMs = settings.translation.llm.timeout_ms;
    llmSettings_.keepAlive = Utf8ToWide(settings.translation.llm.keep_alive);
    llmSettings_.warmupOnActivate = settings.translation.llm.warmup_on_activate;
    llmSettings_.warmupTimeoutMs = settings.translation.llm.warmup_timeout_ms;
    llmSettings_.unloadOnDeactivate = settings.translation.llm.unload_on_deactivate;
    llmSettings_.unloadDelayMs = settings.translation.llm.unload_delay_ms;
    llmSettings_.logTimings = settings.translation.llm.log_timings;
}

Imm32ConversionProvider::~Imm32ConversionProvider() {
    queue_.Shutdown();
}

void Imm32ConversionProvider::SetResultCallback(ProviderCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = std::move(callback);
}

CandidateList Imm32ConversionProvider::FetchLayer1(const LayerRequestContext& ctx) {
    CandidateList list;
    list.layer = ctx.layer ? ctx.layer : 1;

    const std::wstring reading = TrimWhitespace(ctx.reading);
    if (reading.empty()) {
        return list;
    }

    auto candidates = QueryImmConversionCandidates(reading);
    if (candidates.empty()) {
        return list;
    }

    size_t index = 0;
    for (const auto& cand : candidates) {
        std::wstring id = L"imm32_layer1_" + std::to_wstring(index++);
        list.entries.push_back(
            MakeEntry(id, cand, reading, llm::CandidateLayer::Layer1));
    }

    return list;
}

CandidateList Imm32ConversionProvider::FetchLayer2(const LayerRequestContext& ctx) {
    CandidateList list = FetchLayer1(ctx);
    list.layer = ctx.layer ? ctx.layer : 2;
    size_t index = 0;
    for (auto& entry : list.entries) {
        entry.layer = llm::CandidateLayer::Layer2;
        entry.id = L"imm32_layer2_" + std::to_wstring(index++);
        entry.metadata.tone.clear();
        entry.metadata.delta.clear();
    }
    return list;
}

CandidateList Imm32ConversionProvider::FetchTranslation(const LayerRequestContext& ctx) {
    CandidateList list;
    list.layer = ctx.layer ? ctx.layer : 3;
    const std::wstring source = TrimWhitespace(ctx.reading);

    if (source.empty()) {
        list.error = L"No text available for translation.";
        return list;
    }

    ProviderCallback callbackCopy;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbackCopy = callback_;
    }

    if (!ctx.allowAsync || !callbackCopy) {
        auto cancel = std::make_shared<std::atomic_bool>(false);
        TranslationResult result =
            ExecuteTranslationJob(0, source, ctx.committedText, cancel, llmSettings_);
        if (result.cancelled) {
            list.error = L"cancelled";
            return list;
        }
        list.error = result.error;
        if (result.success && !result.translation.empty()) {
            llm::CandidateEntry entry =
                MakeEntry(L"imm32_translation_0",
                          result.translation,
                          ctx.reading,
                          llm::CandidateLayer::Translation);
            entry.metadata.lang = L"en";
            entry.source = llm::CandidateSource::Imm32;
            list.entries.emplace_back(std::move(entry));
        }
        return list;
    }

    LayerRequestContext jobCtx = ctx;
    jobCtx.reading = source;
    uint64_t id = queue_.Enqueue(
        [this, jobCtx](uint64_t requestId, const AsyncWorkQueue::CancelFlag& cancelFlag) {
            TranslationResult result =
                ExecuteTranslationJob(requestId,
                                      jobCtx.reading,
                                      jobCtx.committedText,
                                      cancelFlag,
                                      llmSettings_);
            DispatchTranslationResult(requestId, std::move(result));
        });

    if (!id) {
        auto cancel = std::make_shared<std::atomic_bool>(false);
        TranslationResult result =
            ExecuteTranslationJob(0, source, ctx.committedText, cancel, llmSettings_);
        if (result.cancelled) {
            list.error = L"cancelled";
            return list;
        }
        list.error = result.error;
        if (result.success && !result.translation.empty()) {
            llm::CandidateEntry entry =
                MakeEntry(L"imm32_translation_fallback",
                          result.translation,
                          ctx.reading,
                          llm::CandidateLayer::Translation);
            entry.metadata.lang = L"en";
            entry.source = llm::CandidateSource::Imm32;
            list.entries.emplace_back(std::move(entry));
        }
        return list;
    }

    list.pending = true;
    list.requestId = id;
    return list;
}

bool Imm32ConversionProvider::Cancel(uint64_t requestId) {
    if (!requestId) {
        return false;
    }
    return queue_.Cancel(requestId);
}

std::wstring Imm32ConversionProvider::HiraganaToKatakana(const std::wstring& input) {
    std::wstring result;
    result.reserve(input.size());
    for (wchar_t ch : input) {
        if (ch >= 0x3041 && ch <= 0x3096) {
            result.push_back(static_cast<wchar_t>(ch + 0x60));
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

std::vector<std::wstring> Imm32ConversionProvider::QueryImmConversionCandidates(
    const std::wstring& reading) {
    std::vector<std::wstring> out;
    if (reading.empty()) {
        return out;
    }

    HKL hkl = GetKeyboardLayout(0);
    bool ownsHkl = false;
    LANGID lang = LOWORD(reinterpret_cast<ULONG_PTR>(hkl));
    if (!hkl || PRIMARYLANGID(lang) != LANG_JAPANESE) {
        hkl = LoadKeyboardLayoutW(L"00000411", KLF_NOTELLSHELL | KLF_SUBSTITUTE_OK);
        ownsHkl = (hkl != nullptr);
    }
    if (!hkl) {
        return out;
    }

    HIMC himc = ImmCreateContext();
    if (!himc) {
        if (ownsHkl) {
            UnloadKeyboardLayout(hkl);
        }
        return out;
    }

    DWORD required = ImmGetConversionListW(
        hkl, himc, reading.c_str(), nullptr, 0, GCL_CONVERSION);
    if (required) {
        std::vector<BYTE> buffer(required);
        auto* list = reinterpret_cast<LPCANDIDATELIST>(buffer.data());
        DWORD ret = ImmGetConversionListW(
            hkl, himc, reading.c_str(), list, required, GCL_CONVERSION);
        if (ret && ret <= required && list && list->dwCount) {
            for (DWORD i = 0; i < list->dwCount; ++i) {
                DWORD offset = list->dwOffset[i];
                if (offset >= required) {
                    continue;
                }
                const wchar_t* cand =
                    reinterpret_cast<const wchar_t*>(buffer.data() + offset);
                if (!cand || !*cand) {
                    continue;
                }
                std::wstring candidate = cand;
                if (std::find(out.begin(), out.end(), candidate) == out.end()) {
                    out.emplace_back(std::move(candidate));
                }
            }
        }
    }

    ImmDestroyContext(himc);
    if (ownsHkl) {
        UnloadKeyboardLayout(hkl);
    }

    if (std::find(out.begin(), out.end(), reading) == out.end()) {
        out.push_back(reading);
    }

    std::wstring katakana = HiraganaToKatakana(reading);
    if (!katakana.empty() &&
        std::find(out.begin(), out.end(), katakana) == out.end()) {
        out.push_back(std::move(katakana));
    }

    return out;
}

llm::CandidateEntry Imm32ConversionProvider::MakeEntry(
    const std::wstring& idBase,
    const std::wstring& display,
    const std::wstring& reading,
    llm::CandidateLayer layer) {
    llm::CandidateEntry entry;
    entry.id = idBase;
    entry.layer = layer;
    entry.displayText = display;
    entry.commitText = display;
    entry.reading = reading;
    entry.source = llm::CandidateSource::Imm32;
    entry.metadata.lang = L"ja";
    entry.metadata.commitMode = llm::CommitMode::Replace;
    return entry;
}

void Imm32ConversionProvider::DispatchTranslationResult(
    uint64_t requestId, TranslationResult result) {
    CandidateList list;
    list.layer = 3;
    list.pending = false;
    list.requestId = requestId;
    if (result.cancelled) {
        list.error = L"cancelled";
    } else if (result.success && !result.translation.empty()) {
        llm::CandidateEntry entry =
            MakeEntry(L"imm32_translation_async",
                      result.translation,
                      result.source,
                      llm::CandidateLayer::Translation);
        entry.metadata.lang = L"en";
        entry.source = llm::CandidateSource::Imm32;
        list.entries.emplace_back(std::move(entry));
        list.error = result.error;
    } else {
        list.error = result.error;
    }

    ProviderCallback callbackCopy;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbackCopy = callback_;
    }

    if (callbackCopy) {
        callbackCopy(requestId, std::move(list));
    }
}

}  // namespace ime::conversion
