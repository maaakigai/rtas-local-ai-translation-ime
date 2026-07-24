# Requirements (Section 0)

## Mandatory
- **TSF + IMM32 compatibility**: keep the current `ImmGetConversionListW` + `SendInput` fallback so games (DirectInput) continue to work.
- **Target title**: prioritise Phantasy Star Universe test operations; behaviour must remain stable in that title.
- **Kana toggle consistency**: keep TSF compartments (`GUID_COMPARTMENT_KEYBOARD_*`) in sync with physical toggle keys.
- **Async safety**: ensure `AsyncWorkQueue` tasks cannot freeze the UI; continue using mutex + preview guard semantics.
- **Fallback insert**: retain the `InsertText` → `SendInput` path when edit sessions fail.
- **Logging / masking**: user-provided masking rules must be honoured; avoid writing prompts to disk.
- **Layer cache policy**: Space cycles over cached Layer2/translation entries; Shift+Space forces a re-query. Provide in-memory cache keyed by reading + layer + commit mode.
- **Translation commit policy**: default behaviour replaces the Japanese sentence with the translation (append mode remains opt-in via settings).
- **Provider boundary stability**: Layer1 may move between bridge, IMM32, future Mozc native, or internal dictionary backends only through the provider interfaces. Internal dictionary work remains a parallel decoder track until it reaches measured Mozc parity.

## Optional
- **Candidate enrichment**: merge LLM/user-dictionary outputs with base candidates.
- **Visual tuning**: allow users to configure toast/overlay behaviour (on/off/opacity).
- **Async parallelism**: add knobs for worker counts and prioritisation.

## KPIs
- **Input latency**: ≤ 50 ms from key press to preedit update while in kana mode.
- **Candidate popup**: ≤ 200 ms for `OpenCandidateUI` with readings up to 10 characters.
- **Translation turnaround**: ≤ 1.5 s for standard requests; after 3 s show a timeout message.
- **Error surfacing**: Ollama down/timeouts must display “翻訳不可” and log at WARNING level.

## Open items
- LLM context scope: currently preedit text + most recent committed sentence; adjust later if needed.
- Title-specific constraints beyond PSU remain unknown; gather data once new titles are targeted.
- Log retention and rotation policy still undefined (currently DebugLog only).
- Cache TTL and Shift+Space bindings to be formalised in configuration files.
