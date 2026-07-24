# CandidateEntry schema

```json
{
  "id": "string",                     // unique inside a layer, e.g. "layer1:0"
  "layer": "layer1|layer2|translation", // the owning layer
  "displayText": "string",            // text shown in UI
  "commitText": "string",             // text applied when committed
  "reading": "string",                // kana reading (Layer1 keeps it, Layer2/translation mirror the source)
  "source": "imm32|dict|llm|cache",   // origin enum used by runtime entries
  "confidence": 0.0,                   // optional 0.0–1.0 score
  "metadata": {
    "altVariants": ["string"],        // optional alternative IDs
    "llmRequestId": 42,                // async tracking id
    "partial": false,                  // true if it only covers a suffix
    "lang": "ja|en",                  // language tag where relevant
    "commitMode": "replace|append",   // how this entry expects to be applied (default replace for translation)
    "timestamp": "ISO8601"            // optional creation time
  }
}
```

## Layer-specific notes
- **layer1**: produced by IMM32/dictionary backends. `commitText` is typically the kanji string; `displayText` may include numbering.
- **layer2**: paraphrase/extension variants that merge back into Layer1. If only a trailing phrase, set `metadata.partial=true`.
- **translation**: always represents the final translated string. `metadata.commitMode` defaults to `replace` to reflect that the Japanese sentence will be substituted unless the user switches to append in settings.

## Provider comparison notes

The runtime `CandidateSource` enum does not currently distinguish Mozc bridge,
server alias, or future native; Mozc Layer1 entries still use the existing
runtime source value. Provider comparison artifacts should therefore record
backend provenance separately with fields such as `backend`, `transport`,
`effective_transport`, and `native_backend`.

## Example
```json
[
  {
    "id": "layer1:0",
    "layer": "layer1",
    "displayText": "今日は",
    "commitText": "今日は",
    "reading": "きょうは",
    "source": "imm32",
    "confidence": 0.82
  },
  {
    "id": "layer2:0",
    "layer": "layer2",
    "displayText": "いいてんきですね",
    "commitText": "今日はいい天気ですね",
    "reading": "きょうは",
    "source": "llm",
    "metadata": {
      "llmRequestId": 42,
      "partial": false
    }
  },
  {
    "id": "translation:0",
    "layer": "translation",
    "displayText": "The weather is nice today",
    "commitText": "The weather is nice today",
    "reading": "きょうは",
    "source": "llm",
    "metadata": {
      "llmRequestId": 43,
      "lang": "en",
      "commitMode": "replace"
    }
  }
]
```
