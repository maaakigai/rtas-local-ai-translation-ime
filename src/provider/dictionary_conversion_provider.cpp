#include "dictionary_conversion_provider.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "../../Ime3/rtas_utils.h"

namespace ime::conversion {

namespace {

// Unknown tokens should be much more expensive than known dictionary entries
// (UniDic word costs are typically 5k-15k). Keep this high so known words win.
constexpr int kUnknownPenalty = 40000;

std::wstring Trim(const std::wstring& input) {
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

std::string ToUtf8(const std::wstring& input) {
    if (input.empty()) {
        return {};
    }
    int required = ::WideCharToMultiByte(CP_UTF8, 0, input.c_str(),
                                         static_cast<int>(input.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string utf8(required, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, input.c_str(),
                          static_cast<int>(input.size()),
                          utf8.data(), required,
                          nullptr, nullptr);
    return utf8;
}

std::wstring ToWide(const std::string& input) {
    if (input.empty()) {
        return {};
    }
    int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         input.c_str(),
                                         static_cast<int>(input.size()),
                                         nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(required, L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                          input.c_str(),
                          static_cast<int>(input.size()),
                          wide.data(), required);
    return wide;
}

// Convert hiragana to katakana so lookups expecting katakana still match hiragana input.
std::wstring ToKatakana(const std::wstring& input) {
    std::wstring out = input;
    for (auto& ch : out) {
        if (ch >= L'ぁ' && ch <= L'ゖ') {
            ch = static_cast<wchar_t>(ch + 0x60);
        }
    }
    return out;
}

}  // namespace

DictionaryConversionProvider::DictionaryConversionProvider(
    const ime::config::DictionarySettings& settings,
    std::optional<ime::config::DictionarySettings> translationDictionary,
    ime::config::TranslationMode translationMode,
    const ime::config::LlmSettings& llmSettings,
    const std::filesystem::path& installRoot)
    : settings_(settings),
      translation_dictionary_(std::move(translationDictionary)),
      translationMode_(translationMode),
      installRoot_(installRoot) {
    llmSettings_.model = Utf8ToWide(llmSettings.model);
    llmSettings_.host = Utf8ToWide(llmSettings.host);
    llmSettings_.port = llmSettings.port > 0
                            ? static_cast<uint16_t>(llmSettings.port)
                            : static_cast<uint16_t>(11434);
    llmSettings_.path = Utf8ToWide(llmSettings.path);
    llmSettings_.useTls = llmSettings.use_tls;
    llmSettings_.timeoutMs = llmSettings.timeout_ms;
    llmSettings_.keepAlive = Utf8ToWide(llmSettings.keep_alive);
    llmSettings_.warmupOnActivate = llmSettings.warmup_on_activate;
    llmSettings_.warmupTimeoutMs = llmSettings.warmup_timeout_ms;
    llmSettings_.unloadOnDeactivate = llmSettings.unload_on_deactivate;
    llmSettings_.unloadDelayMs = llmSettings.unload_delay_ms;
    llmSettings_.logTimings = llmSettings.log_timings;
}

bool DictionaryConversionProvider::Initialize(std::wstring* error) {
    if (error) {
        error->clear();
    }

    try {
        std::filesystem::path morphPath =
            ResolveDictionaryPath(settings_.morph_tsv);
        std::filesystem::path bilingualPath =
            ResolveDictionaryPath(settings_.bilingual_tsv);

        if (translationMode_ == ime::config::TranslationMode::kDictionary &&
            translation_dictionary_ && translation_dictionary_->enabled) {
            bilingualPath = ResolveDictionaryPath(translation_dictionary_->bilingual_tsv);
        }

        if (!std::filesystem::exists(morphPath)) {
            if (error) {
                *error = L"Morphological dictionary not found: " + morphPath.wstring();
            }
            return false;
        }
        if (!std::filesystem::exists(bilingualPath)) {
            if (error) {
                *error = L"Bilingual dictionary not found: " + bilingualPath.wstring();
            }
            return false;
        }

        ime::dictionary::MorphDictionaryLoader morphLoader;
        ime::dictionary::BilingualDictionaryLoader bilingualLoader;

        morphLoader.Load(morphPath, morphDictionary_);
        bilingualLoader.Load(bilingualPath, bilingualDictionary_);

        loaded_ = true;
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = L"Dictionary load failure: " + ToWide(ex.what());
        }
        loaded_ = false;
        return false;
    }
}

void DictionaryConversionProvider::SetResultCallback(ProviderCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = std::move(callback);
}

CandidateList DictionaryConversionProvider::FetchLayer1(const LayerRequestContext& ctx) {
    CandidateList list;
    list.layer = ctx.layer ? ctx.layer : 1;

    const std::wstring reading = TrimWhitespace(ctx.reading);
    if (!loaded_ || reading.empty()) {
        return list;
    }

    SegmentationResult segmentation = SegmentReading(reading);
    std::wstring surface;
    for (const auto& token : segmentation.tokens) {
        surface.append(token.surface);
    }
    if (surface.empty()) {
        surface = reading;
    }

    std::vector<llm::CandidateEntry> entries;

    // Prefer raw reading as the default (so Enter keeps kana unchanged).
    llm::CandidateEntry hiraEntry = MakeEntry(
        L"dict_layer1_0", reading, reading, llm::CandidateLayer::Layer1,
        llm::CandidateSource::Dict);
    hiraEntry.metadata.lang = L"ja";
    hiraEntry.confidence = 0.3;
    entries.emplace_back(std::move(hiraEntry));

    // Primary surface from segmentation.
    llm::CandidateEntry surfaceEntry = MakeEntry(
        L"dict_layer1_1", surface, reading, llm::CandidateLayer::Layer1,
        llm::CandidateSource::Dict);
    surfaceEntry.metadata.lang = L"ja";
    surfaceEntry.confidence = segmentation.tokens.empty()
        ? std::optional<double>(0.2)
        : std::optional<double>(0.7);
    entries.emplace_back(std::move(surfaceEntry));

    // Add alternates so users can cycle through major variants.
    std::wstring katakana = ToKatakana(reading);
    std::vector<std::wstring> extras;
    if (!katakana.empty() && katakana != reading && katakana != entries[1].displayText) {
        extras.push_back(katakana);
    }
    // Collect dictionary surfaces for the whole reading to provide more options.
    if (!katakana.empty()) {
        auto matches = morphDictionary_.LookupReading(WideToUtf8(katakana));
        std::unordered_set<std::wstring> seen;
        for (const auto& e : entries) {
            seen.insert(e.displayText);
        }
        if (!katakana.empty()) seen.insert(katakana);
        for (const auto* rec : matches) {
            if (!rec) continue;
            std::wstring cand = rec->surface.empty()
                                    ? Utf8ToWide(rec->base_form)
                                    : Utf8ToWide(rec->surface);
            if (cand.empty()) {
                cand = reading;
            }
            if (seen.insert(cand).second) {
                extras.push_back(std::move(cand));
            }
            if (extras.size() >= 6) {  // limit variants
                break;
            }
        }
    }
    size_t altIndex = entries.size();
    for (const auto& alt : extras) {
        llm::CandidateEntry altEntry = MakeEntry(
            L"dict_layer1_" + std::to_wstring(altIndex++),
            alt, reading, llm::CandidateLayer::Layer1,
            llm::CandidateSource::Dict);
        altEntry.confidence = 0.3;
        entries.emplace_back(std::move(altEntry));
    }

    list.entries = std::move(entries);
    return list;
}

CandidateList DictionaryConversionProvider::FetchLayer2(const LayerRequestContext& ctx) {
    CandidateList list = FetchLayer1(ctx);
    list.layer = ctx.layer ? ctx.layer : 2;
    size_t index = 0;
    for (auto& entry : list.entries) {
        entry.layer = llm::CandidateLayer::Layer2;
        entry.id = L"dict_layer2_" + std::to_wstring(index++);
        entry.metadata.tone = L"";
        entry.metadata.delta = L"";
    }
    return list;
}

CandidateList DictionaryConversionProvider::FetchTranslation(const LayerRequestContext& ctx) {
    CandidateList list;
    list.layer = ctx.layer ? ctx.layer : 3;

    std::wstring source = TrimWhitespace(ctx.reading);
    if (source.empty()) {
        list.error = L"No text available for translation.";
        return list;
    }

    if (translationMode_ == ime::config::TranslationMode::kLLM) {
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
                    MakeEntry(L"dict_translation_0",
                              result.translation,
                              ctx.reading,
                              llm::CandidateLayer::Translation,
                              llm::CandidateSource::Llm);
                entry.metadata.lang = L"en";
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
                DispatchTranslationResult(requestId, std::move(result), jobCtx.reading);
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
                    MakeEntry(L"dict_translation_fallback",
                              result.translation,
                              ctx.reading,
                              llm::CandidateLayer::Translation,
                              llm::CandidateSource::Llm);
                entry.metadata.lang = L"en";
                list.entries.emplace_back(std::move(entry));
            }
            return list;
        }

        list.pending = true;
        list.requestId = id;
        return list;
    }

    if (!loaded_) {
        list.error = L"Dictionary backend not initialised.";
        return list;
    }

    auto matches = LookupBilingual(source);

    llm::CandidateEntry entry = MakeEntry(
        L"dict_translation_0", source, source,
        llm::CandidateLayer::Translation, llm::CandidateSource::Dict);
    entry.metadata.lang = L"en";
    entry.metadata.commitMode = llm::CommitMode::Replace;

    if (!matches.empty() && !matches.front()->english_glosses.empty()) {
        entry.displayText = TrimWhitespace(Utf8ToWide(matches.front()->english_glosses.front()));
        entry.commitText = entry.displayText;
        entry.metadata.lang = L"en";
        entry.confidence = 0.75;
    } else {
        entry.displayText = source;
        entry.commitText = source;
        entry.metadata.lang = L"ja";
        entry.confidence = 0.1;
        list.error = L"No bilingual entry matched; passthrough text.";
    }

    list.entries.emplace_back(std::move(entry));
    return list;
}

bool DictionaryConversionProvider::Cancel(uint64_t requestId) {
    if (translationMode_ != ime::config::TranslationMode::kLLM) {
        return false;
    }
    // Only cancel async translation jobs when LLM translation mode is active.
    if (!requestId) {
        return false;
    }
    return queue_.Cancel(requestId);
}

DictionaryConversionProvider::SegmentationResult
DictionaryConversionProvider::SegmentReading(const std::wstring& reading) const {
    SegmentationResult result;
    const size_t length = reading.size();
    if (length == 0 || !loaded_) {
        return result;
    }

    struct Node {
        bool valid = false;
        int cost = std::numeric_limits<int>::max();
        size_t prev = 0;
        Token token;
    };

    std::vector<Node> dp(length + 1);
    dp[0].valid = true;
    dp[0].cost = 0;

    const std::wstring katakana = ToKatakana(reading);

    auto registerCandidate = [&](size_t end, size_t prev, int cost, Token token) {
        if (!dp[end].valid || cost < dp[end].cost) {
            dp[end].valid = true;
            dp[end].cost = cost;
            dp[end].prev = prev;
            dp[end].token = std::move(token);
        }
    };

    for (size_t i = 0; i < length; ++i) {
        if (!dp[i].valid) {
            continue;
        }
        const int baseCost = dp[i].cost;

        for (size_t len = 1; len <= 6 && i + len <= length; ++len) {
            std::wstring view_norm = katakana.substr(i, len);
            std::string key = WideToUtf8(view_norm);
            auto matches = morphDictionary_.LookupReading(key);
            if (matches.empty()) {
                continue;
            }

            const ime::dictionary::MorphRecord* best = nullptr;
            int bestCost = std::numeric_limits<int>::max();
            for (const auto* rec : matches) {
                if (!rec) {
                    continue;
                }
                int adjustedCost = rec->cost;
                std::wstring candidateSurface =
                    Utf8ToWide(rec->surface.empty() ? rec->base_form : rec->surface);
                // Prefer non-kana surfaces (kanji/latin) to avoid katakana-only outputs.
                bool kanaOnly = true;
                for (wchar_t wc : candidateSurface) {
                    if (!((wc >= L'ぁ' && wc <= L'ゖ') || (wc >= L'ァ' && wc <= L'ヺ') ||
                          wc == L'ー')) {
                        kanaOnly = false;
                        break;
                    }
                }
                if (kanaOnly) {
                    adjustedCost += 2000;
                }
                if (adjustedCost < bestCost) {
                    bestCost = adjustedCost;
                    best = rec;
                }
            }
            if (!best) {
                continue;
            }

            Token token;
            token.surface = best->surface.empty()
                ? Utf8ToWide(best->base_form)
                : Utf8ToWide(best->surface);
            if (token.surface.empty()) {
                token.surface = reading.substr(i, len);
            }
            token.reading = reading.substr(i, len);  // preserve original hiragana for UI/commit
            token.pos = best->pos;
            token.cost = bestCost;
            token.unknown = false;

            int totalCost = baseCost + bestCost + static_cast<int>(len) * 8;
            registerCandidate(i + len, i, totalCost, std::move(token));
        }

        Token unknown;
        unknown.surface = reading.substr(i, 1);
        unknown.reading = unknown.surface;
        unknown.pos = "UNK";
        unknown.cost = kUnknownPenalty;
        unknown.unknown = true;
        registerCandidate(i + 1, i, baseCost + kUnknownPenalty, std::move(unknown));
    }

    if (!dp[length].valid) {
        Token fallback;
        fallback.surface = reading;
        fallback.reading = reading;
        fallback.pos = "UNK";
        fallback.cost = kUnknownPenalty * static_cast<int>(length);
        fallback.unknown = true;
        result.tokens.push_back(std::move(fallback));
        result.totalCost = kUnknownPenalty * static_cast<int>(length);
        return result;
    }

    size_t pos = length;
    while (pos > 0) {
        const Node& node = dp[pos];
        result.tokens.push_back(node.token);
        pos = node.prev;
    }
    std::reverse(result.tokens.begin(), result.tokens.end());
    result.totalCost = dp[length].cost;
    return result;
}

std::filesystem::path DictionaryConversionProvider::ResolveDictionaryPath(
    const std::filesystem::path& relative) const {
    if (relative.is_absolute() || installRoot_.empty()) {
        return relative;
    }
    return installRoot_ / relative;
}

std::vector<const ime::dictionary::BilingualEntry*>
DictionaryConversionProvider::LookupBilingual(const std::wstring& surface) const {
    std::vector<const ime::dictionary::BilingualEntry*> matches;
    if (!loaded_) {
        return matches;
    }
    std::string utf8 = WideToUtf8(surface);
    auto append = [&matches](const std::vector<const ime::dictionary::BilingualEntry*>& items) {
        matches.insert(matches.end(), items.begin(), items.end());
    };
    append(bilingualDictionary_.LookupHeadword(utf8));
    append(bilingualDictionary_.LookupKanji(utf8));
    append(bilingualDictionary_.LookupKana(utf8));
    return matches;
}

llm::CandidateEntry DictionaryConversionProvider::MakeEntry(
    const std::wstring& idBase,
    const std::wstring& display,
    const std::wstring& reading,
    llm::CandidateLayer layer,
    llm::CandidateSource source) {
    llm::CandidateEntry entry;
    entry.id = idBase;
    entry.layer = layer;
    entry.displayText = display;
    entry.commitText = display;
    entry.reading = reading;
    entry.source = source;
    entry.metadata.lang = L"ja";
    entry.metadata.commitMode = llm::CommitMode::Replace;
    entry.metadata.partial = false;
    return entry;
}

void DictionaryConversionProvider::DispatchTranslationResult(
    uint64_t requestId,
    TranslationResult result,
    const std::wstring& originalReading) {
    CandidateList list;
    list.layer = 3;
    list.pending = false;
    list.requestId = requestId;
    if (result.cancelled) {
        list.error = L"cancelled";
    } else if (result.success && !result.translation.empty()) {
        llm::CandidateEntry entry =
            MakeEntry(L"dict_translation_async",
                      result.translation,
                      originalReading,
                      llm::CandidateLayer::Translation,
                      llm::CandidateSource::Llm);
        entry.metadata.lang = L"en";
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

std::wstring DictionaryConversionProvider::TrimWhitespace(const std::wstring& input) {
    return Trim(input);
}

std::string DictionaryConversionProvider::WideToUtf8(const std::wstring& input) {
    return ToUtf8(input);
}

std::wstring DictionaryConversionProvider::Utf8ToWide(const std::string& input) {
    return ToWide(input);
}

}  // namespace ime::conversion
