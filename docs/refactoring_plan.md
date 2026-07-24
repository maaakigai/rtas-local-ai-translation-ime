# rtas_text_service refactoring plan (Section 1)

## Current pain points
- `TextService` still hosts IMM32 compatibility, LLM queueing, overlay logic, and candidate UI coupling, making it hard to test.
- Additional layers will further bloat `OnKeyDown` unless responsibilities are separated.

## Proposed restructuring
1. **ConversionProvider abstraction**
   - Add `std::unique_ptr<IConversionProvider> m_provider;` to `TextService`.
   - Move `BuildCandidates` into `Imm32ConversionProvider` (completed via ConversionProvider refactor) (first step toward the future dictionary backend).
2. **LayerState manager**
   - Introduce `struct LayerState { enum class Stage { Preedit, Layer1, Layer2, Translation, CommitPending }; ... };`
   - Route key handling through `LayerState::HandleKey(event)` to keep transitions declarative.
3. **Async result dispatcher**
   - Relocate `OnTranslationReady` into a `LayerResultDispatcher` that understands caching and commit modes.
   - Isolate `AsyncWorkQueue` access behind a small interface to ease testing.
4. **UI presenters**
   - Wrap `CandidateUI` behind `CandidatePresenter` for tab/badge control and high-contrast handling.
   - Let `OverlayController` receive layer badges and pending state rather than raw text.
5. **Commit policy hook**
   - Surface translation commit mode (`replace` default, `append` optional) via provider results + settings so UI and TextService stay consistent.

## Migration steps
- **Step A**: introduce `IConversionProvider` + IMM32-backed implementation with caching hooks.
- **Step B**: create `LayerState`, migrate `OnKeyDown` flow, ensure Space vs Shift+Space logic lives there.
- **Step C**: move LLM integration into provider; TextService listens for callbacks and applies commit policy.
- **Step D**: add `CandidatePresenter`/overlay updates, including high-contrast badge rendering.

## Testing considerations
- After each step, walk through `tests/samples/multi_stage_inputs.txt` scenarios (multi-layer flow).
- Swapping providers must not affect `KanaModeCommand` behaviour.
- Add unit tests for `LayerState` (state transitions, cache usage, commit mode toggles).
