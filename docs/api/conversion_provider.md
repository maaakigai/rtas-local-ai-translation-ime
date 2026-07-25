# ConversionProvider interface

`IConversionProvider` separates the TSF text service and candidate UI from the
active conversion backend. The authoritative declarations are in
`src/api/conversion_provider.h`.

```cpp
struct LayerRequestContext {
    std::wstring reading;
    std::wstring committedText;
    std::wstring hint;
    uint32_t layer = 0;
    bool allowAsync = true;
};

struct CandidateList {
    std::vector<llm::CandidateEntry> entries;
    std::vector<SegmentInfo> segments;
    std::optional<uint64_t> requestId;
    bool pending = false;
    uint32_t layer = 0;
    std::wstring error;
};

using ProviderCallback =
    std::function<void(uint64_t requestId, CandidateList result)>;

class IConversionProvider {
public:
    virtual ~IConversionProvider() = default;
    virtual ProviderCapabilities GetCapabilities() const;
    virtual void SetResultCallback(ProviderCallback callback) = 0;
    virtual CandidateList FetchLayer1(const LayerRequestContext& ctx) = 0;
    virtual CandidateList FetchLayer2(const LayerRequestContext& ctx) = 0;
    virtual CandidateList FetchTranslation(
        const LayerRequestContext& ctx) = 0;
    virtual bool Cancel(uint64_t requestId) = 0;
};
```

## Responsibilities

- `TextService` supplies the reading, committed text, target layer, and
  asynchronous-execution preference.
- Layer 1 returns kana-kanji candidates from the selected Japanese conversion
  backend.
- Layer 2 returns Japanese paraphrase candidates when the provider supports
  them.
- Translation returns English candidates for the selected Japanese text.
- `segments` carries optional backend-provided segmentation metadata.
- A provider that cannot complete synchronously returns `pending=true` and a
  `requestId`, then delivers the final `CandidateList` through
  `ProviderCallback`.

## Result and cancellation contract

- `requestId` lets the UI reject a stale response after the user changes text,
  switches layers, or cancels a request.
- `Cancel(requestId)` requests cancellation; consumers must still treat any
  late callback as stale.
- An empty `entries` list with `pending=false` is a completed result with no
  candidates. Providers should put a user-displayable diagnostic in `error`
  when the empty result represents a failure.
- Provider capability checks must happen through `GetCapabilities()` instead
  of assuming every backend implements every layer.

## Current implementation boundary

The checked-in public default uses the bridge transport for Layer 1 and the LLM
provider for Layer 2 and Translation. The IMM32 and native Mozc transports are
opt-in research paths. Backend selection is handled by
`src/provider/conversion_provider_factory.cpp`; UI code should not depend on a
backend-specific executable or protocol.
