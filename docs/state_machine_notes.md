# State Machine Notes

## Layer transitions
- **Preedit → Layer1Candidates**: finalize the current romaji via `m_reading.append(ConvertRomaji(true))` and call `OpenCandidateUI` when `m_reading` is non-empty.
- **Layer1Candidates → Layer2Refine**: `SwitchToLayer2FromLayer1()` captures the highlighted Japanese candidate into `m_layer2SourceText/key` and invokes `RequestLayer2Alternatives()`. The Candidate UI swaps to the Layer2 tab while Layer1 stays cached.
- **Layer2Refine → Layer1Merged**: `MergeLayer2Selection()` promotes the paraphrase from `m_layer2Cache` back into `m_reading`, refreshes the composition, and sets `m_layer1Merged = true`.
- **Layer1Merged → TranslationSpin**: `SwitchToTranslationTab()` reuses the merged Japanese string (`m_candidateSourceText`) and calls `StartTranslationForCandidate`. Translation is only offered after a Layer1 merge.
- **TranslationSpin → Commit**: by default replace the Japanese sentence with the translation (`CommitComposition` receives the translation string). If the user switches the mode to append, follow up with `InsertText` to add the translation after the original sentence.

## Async handling & cache
- Space in Layer2/Translation first checks the in-memory caches (`m_layer2Cache` / `m_translationCache`, keyed by trimmed source text). If entries exist, cycle locally before re-querying.
- Shift+Space bypasses the cache and re-queries the provider/LLM (`RequestLayer2Alternatives(true)` / `StartTranslationForCandidate(..., true)`).
- Pending requests replace the active tab contents with `? [処理中…]` while the composition preview keeps the last confirmed Layer1 string. `SetLayer2Pending` / `SetTranslationPending` also toggle the overlay spinner.
- When async results are cancelled, the pending flags are cleared and the previous tab contents are restored without disturbing Layer1.
- Normalisation and safety filtering for translation results live in a dedicated helper before they reach the UI.

## UI rules
- Overlay shows badges for each layer (default emojis, `[L1]`, `[L2]`, `[EN]` in high-contrast mode). Pending states add a spinner/clock icon.
- Candidate UI presents three logical tabs (Japanese / Paraphrase / Translation). Space cycles within the current tab; Shift+Space triggers re-query; Ctrl+Tab / Ctrl+Shift+Tab move across tabs. Layer2 can only be entered while the Japanese tab is active, and Translation becomes available once `m_layer1Merged` is true.

## Exception handling
- If Layer2/Translation returns no entries and `pending=false`, the layer is skipped; Enter/Space propagate to the upstream layer.
- Escape unwinds layers in order: TranslationSpin → Layer1Merged → Layer1Candidates → Preedit → Idle → IME OFF.

## TODO
- Expose cache TTL and re-query key bindings (e.g. Shift+Space) through configuration.

