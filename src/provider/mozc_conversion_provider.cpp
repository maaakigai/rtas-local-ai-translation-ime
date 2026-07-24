#include "mozc_conversion_provider.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include "../../Ime3/rtas_utils.h"

namespace ime::conversion {

namespace {

bool IsSentenceDelimiter(wchar_t ch) {
  switch (ch) {
    case L'、':
    case L'。':
    case L',':
    case L'.':
    case L'！':
    case L'!':
    case L'？':
    case L'?':
    case L'　':
    case L' ':
      return true;
    default:
      return false;
  }
}

bool ContainsDelimiter(const std::wstring& text) {
  for (wchar_t ch : text) {
    if (IsSentenceDelimiter(ch)) {
      return true;
    }
  }
  return false;
}

void PreserveReadingTailForPartialSentence(const std::wstring& reading,
                                           std::vector<std::wstring>* candidates) {
  if (!candidates || candidates->empty()) {
    return;
  }
  size_t splitPos = std::wstring::npos;
  for (size_t i = 0; i < reading.size(); ++i) {
    if (IsSentenceDelimiter(reading[i])) {
      splitPos = i;
      break;
    }
  }
  if (splitPos == std::wstring::npos || splitPos >= reading.size()) {
    return;
  }
  const std::wstring tail = reading.substr(splitPos);
  if (tail.empty()) {
    return;
  }

  for (auto& cand : *candidates) {
    const std::wstring trimmed = cand;
    if (trimmed.empty()) {
      continue;
    }
    // Keep already-complete candidates intact.
    if (trimmed == reading || ContainsDelimiter(trimmed)) {
      continue;
    }
    cand.append(tail);
  }
}

uint32_t DecodeCodePoint(const std::wstring& text, size_t* index) {
  if (!index || *index >= text.size()) {
    return 0;
  }
  const wchar_t lead = text[*index];
  ++(*index);
  if (lead >= 0xD800 && lead <= 0xDBFF && *index < text.size()) {
    const wchar_t trail = text[*index];
    if (trail >= 0xDC00 && trail <= 0xDFFF) {
      ++(*index);
      return 0x10000u + ((static_cast<uint32_t>(lead) - 0xD800u) << 10) +
             (static_cast<uint32_t>(trail) - 0xDC00u);
    }
  }
  return static_cast<uint32_t>(lead);
}

bool IsEmojiCodePoint(uint32_t cp) {
  return (cp >= 0x1F300u && cp <= 0x1FAFFu) ||  // emoji blocks
         (cp >= 0x2600u && cp <= 0x27BFu);      // symbols / dingbats
}

bool ContainsEmoji(const std::wstring& text) {
  size_t i = 0;
  while (i < text.size()) {
    const uint32_t cp = DecodeCodePoint(text, &i);
    if (cp && IsEmojiCodePoint(cp)) {
      return true;
    }
  }
  return false;
}

bool ContainsKanji(const std::wstring& text) {
  for (wchar_t ch : text) {
    if ((ch >= 0x4E00 && ch <= 0x9FFF) ||  // CJK Unified Ideographs
        (ch >= 0x3400 && ch <= 0x4DBF)) {  // CJK Extension A
      return true;
    }
  }
  return false;
}

bool ContainsKana(const std::wstring& text) {
  for (wchar_t ch : text) {
    if ((ch >= 0x3040 && ch <= 0x309F) ||  // Hiragana
        (ch >= 0x30A0 && ch <= 0x30FF) ||  // Katakana
        (ch >= 0xFF66 && ch <= 0xFF9D)) {  // Half-width katakana
      return true;
    }
  }
  return false;
}

std::wstring NativeBackendName(ime::config::MozcNativeBackend backend,
                               const std::string& rawValue) {
  switch (backend) {
    case ime::config::MozcNativeBackend::kMozcServerClient:
    case ime::config::MozcNativeBackend::kUnset:
      return L"mozc_server_client";
    case ime::config::MozcNativeBackend::kLinkedConverter:
      return L"linked_converter";
    case ime::config::MozcNativeBackend::kInvalid:
      return rawValue.empty() ? L"<invalid>" : Utf8ToWide(rawValue);
  }
  return L"<unknown>";
}

int CandidateBiasScore(const std::wstring& candidate, const std::wstring& reading) {
  int score = 0;
  const bool hasKanji = ContainsKanji(candidate);
  const bool hasKana = ContainsKana(candidate);
  if (ContainsEmoji(candidate)) score -= 1000;
  if (hasKanji) {
    // Prefer proper kanji words over kana-mixed variants like "超ぶん".
    score += hasKana ? 20 : 80;
  }
  if (candidate == reading) score -= 20;  // avoid raw reading staying at very top
  return score;
}

void ApplyConservativeReorder(std::vector<std::wstring>* candidates,
                              const std::wstring& reading) {
  if (!candidates || candidates->size() < 2) {
    return;
  }
  struct Row {
    std::wstring text;
    int score = 0;
    size_t original = 0;
  };
  std::vector<Row> rows;
  rows.reserve(candidates->size());
  for (size_t i = 0; i < candidates->size(); ++i) {
    Row row;
    row.text = std::move((*candidates)[i]);
    row.score = CandidateBiasScore(row.text, reading);
    row.original = i;
    rows.push_back(std::move(row));
  }
  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return a.original < b.original;
  });
  candidates->clear();
  candidates->reserve(rows.size());
  for (auto& row : rows) {
    candidates->push_back(std::move(row.text));
  }
}

}  // namespace

MozcConversionProvider::MozcConversionProvider(
    const ime::config::MozcSettings& settings,
    const ime::config::LlmSettings& llmSettings)
    : settings_(settings), transport_(CreateMozcTransport(settings)) {
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

MozcConversionProvider::~MozcConversionProvider() {
  queue_.Shutdown();
}

bool MozcConversionProvider::Initialize(std::wstring* error) {
  if (error) {
    error->clear();
  }
  if (!transport_) {
    if (error) {
      if (settings_.transport == ime::config::MozcTransport::kNative) {
        *error =
            L"'provider.kana.mozc.transport=native' selected backend '" +
            NativeBackendName(settings_.native.backend,
                              settings_.native.backend_value) +
            L"', but that OSS Mozc native backend is unavailable in this spike; "
            L"no fallback was used.";
      } else if (settings_.transport == ime::config::MozcTransport::kInvalid) {
        *error = L"Unsupported mozc transport: " +
                 Utf8ToWide(settings_.transport_value);
      } else {
        *error = L"Failed to create mozc transport.";
      }
    }
    initialized_ = false;
    return false;
  }
  initialized_ = transport_->Initialize(error);
  return initialized_;
}

ProviderCapabilities MozcConversionProvider::GetCapabilities() const {
  return {};
}

void MozcConversionProvider::SetResultCallback(ProviderCallback callback) {
  std::lock_guard<std::mutex> lock(callbackMutex_);
  callback_ = std::move(callback);
}

CandidateList MozcConversionProvider::FetchLayer1(const LayerRequestContext& ctx) {
  CandidateList list;
  list.layer = ctx.layer ? ctx.layer : 1;

  const std::wstring reading = TrimWhitespace(ctx.reading);
  if (!initialized_ || reading.empty()) {
    return list;
  }

  MozcCandidateRequest request;
  request.reading = reading;
  MozcCandidateResponse response = transport_->FetchCandidates(request);
  for (const auto& seg : response.segments) {
    ime::conversion::SegmentInfo info;
    info.index = seg.index;
    info.start = seg.start;
    info.length = seg.length;
    info.surface = seg.surface;
    list.segments.push_back(std::move(info));
  }
  if (response.candidates.empty() && !response.error.empty()) {
    list.error = response.error;
  }

  size_t index = 0;
  for (const auto& cand : response.candidates) {
    const std::wstring id = L"mozc_layer1_" + std::to_wstring(index++);
    list.entries.emplace_back(
        MakeEntry(id, cand, reading, llm::CandidateLayer::Layer1));
  }

  return list;
}

CandidateList MozcConversionProvider::FetchLayer2(const LayerRequestContext& ctx) {
  CandidateList list;
  list.layer = ctx.layer ? ctx.layer : 2;

  // Do not re-run kana-kanji conversion at Layer2 in mozc mode.
  // Layer2 keeps the already selected Layer1 result stable.
  std::wstring source = ctx.committedText.empty() ? ctx.reading : ctx.committedText;
  source = TrimWhitespace(source);
  if (source.empty()) {
    return list;
  }

  llm::CandidateEntry entry =
      MakeEntry(L"mozc_layer2_0", source, source, llm::CandidateLayer::Layer2);
  entry.metadata.tone.clear();
  entry.metadata.delta.clear();
  list.entries.emplace_back(std::move(entry));
  return list;
}

CandidateList MozcConversionProvider::FetchTranslation(
    const LayerRequestContext& ctx) {
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
          MakeEntry(L"mozc_translation_0",
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
      [this, jobCtx](uint64_t requestId,
                     const AsyncWorkQueue::CancelFlag& cancelFlag) {
        TranslationResult result = ExecuteTranslationJob(
            requestId, jobCtx.reading, jobCtx.committedText, cancelFlag,
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
          MakeEntry(L"mozc_translation_fallback",
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

bool MozcConversionProvider::Cancel(uint64_t requestId) {
  if (!requestId) {
    return false;
  }
  return queue_.Cancel(requestId);
}

std::wstring MozcConversionProvider::TrimWhitespace(const std::wstring& input) {
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

std::wstring MozcConversionProvider::HiraganaToKatakana(
    const std::wstring& input) {
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

llm::CandidateEntry MozcConversionProvider::MakeEntry(
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

void MozcConversionProvider::DispatchTranslationResult(
    uint64_t requestId, TranslationResult result) {
  CandidateList list;
  list.layer = 3;
  list.pending = false;
  list.requestId = requestId;
  if (result.cancelled) {
    list.error = L"cancelled";
  } else if (result.success && !result.translation.empty()) {
    llm::CandidateEntry entry =
        MakeEntry(L"mozc_translation_async",
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
