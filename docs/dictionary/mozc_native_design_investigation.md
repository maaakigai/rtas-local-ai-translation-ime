# Mozc Native Design Investigation

Date: 2026-05-19
Scope: design investigation and migration status for Mozc native work. This
document records the architectural direction and the Phase 1 typed transport
state; it does not describe a completed native conversion engine.

> Historical decision record: the recommendation below predates the
> 2026-07-25 public snapshot work. A temporary hardening change selected
> `transport=imm32`, but live testing returned no candidates. The current
> checked-in default restores `transport=bridge`. Later Phase 3 work implemented
> an opt-in app-local wrapper/server boundary for `native`; the public repository
> does not bundle the external Mozc build artifacts it needs.

## Executive Decision

RTAS should not replace the current bridge path with a direct internal
dictionary switch yet. The recommended direction is to keep the existing
`bridge` and `imm32` paths available, then add a future opt-in
`transport=native` path as a first-class Mozc backend.

For this project, "native" must mean one of these principled integrations:

1. RTAS links to or embeds an OSS Mozc conversion component and uses its typed
   data structures directly.
2. RTAS talks to an OSS Mozc server/client boundary using generated Mozc
   protocol definitions, not hand-written numeric field parsing.
3. RTAS improves its internal dictionary decoder separately, but does not call
   that work "Mozc native" until it reaches a comparable decoder model,
   segment model, and ranking model.

It must not mean moving the current Google Japanese Input pipe scraping and
hardcoded protocol fields from `mozc_bridge.exe` into the RTAS DLL.

## Non-Negotiable Constraints

- No hardcoded install paths, pipe names, KLIDs, timeout constants, or protocol
  field numbers in the native design.
- No private Google Japanese Input named-pipe dependency in the native design.
- No candidate UI or TextService special cases for `native`; the existing
  `IConversionProvider` and `IMozcTransport` boundaries should absorb it.
- No silent fallback from native to IMM32 unless an explicit config setting
  allows that fallback and reports it.
- No heuristic segment reconstruction as final behavior. Native should return
  authoritative segment spans and surfaces from the converter.
- No symptomatic candidate reordering or UI-label filtering as a substitute for
  a real decoder/ranker.
- Any fallback, discovery path, or scoring parameter must be explicit,
  validated, testable, and documented.

## Current Architecture

### Provider Boundary

The stable RTAS provider interface is `IConversionProvider` in
`src/api/conversion_provider.h`. Providers receive `LayerRequestContext` and
return `CandidateList` with entries, optional segment metadata, optional async
request id, pending state, layer id, and error text.

`Ime3/rtas_text_service.h` keeps the UI mostly provider-agnostic. It creates a
provider through `CreateConversionProvider`, stores capabilities, sends
Layer1/Layer2/Translation requests, and consumes returned strings and segment
metadata. This boundary is the right place to keep future native work isolated.

Important integration points:

- `src/api/conversion_provider.h`
- `src/provider/conversion_provider_factory.cpp`
- `Ime3/rtas_text_service.h`
- `docs/api/candidate_schema.md`
- `docs/state_machine.md`
- `docs/ui_spec.md`

### Config And Factory Selection

The current public config restores the bridge path:

- `config/ime_settings.json`
  - `provider.kana.mode = "mozc"`
  - `provider.kana.mozc.transport = "bridge"`

The bridge implementation is compiled into the RTAS DLL and is also buildable
as a standalone diagnostic executable. It remains a private compatibility
boundary rather than a stable public API.

`src/config/provider_settings.*` keeps the serialized JSON `transport` value as
a string, but parses it into a typed `MozcTransport` domain:

- `bridge` -> `MozcTransport::kBridge`
- `server` -> `MozcTransport::kBridge` as a legacy alias.
- `imm32` -> `MozcTransport::kImm32`
- `native` -> `MozcTransport::kNative` as an opt-in app-local server/client
  boundary. It initializes only when the configured wrapper and Mozc server
  artifacts exist.
- unsupported strings -> `MozcTransport::kInvalid` with a configuration error.

The factory in `src/provider/conversion_provider_factory.cpp` currently
chooses:

1. dictionary provider, if kana mode is `dictionary` and initialization
   succeeds.
2. Mozc provider, if kana mode is `mozc` and initialization succeeds.
3. IMM32 provider fallback.

For `transport=native`, the factory uses the app-local wrapper/server transport
when its explicit artifacts validate. If they are missing or invalid, the
factory returns an unavailable provider with a clear diagnostic instead of
falling through to IMM32. Invalid transport strings likewise remain visible
configuration errors instead of silently falling back to `imm32`, `bridge`, or
`llm`.

### Mozc Provider And RTAS Bridge Transport

`src/provider/mozc_conversion_provider.cpp` delegates Layer1 to
`IMozcTransport`. It copies `MozcCandidateResponse.segments` into
`CandidateList.segments` and converts candidate strings into Layer1 entries.

`src/provider/mozc_transport.cpp` currently provides:

- `MozcBridgeTransport`
  - calls the bridge implementation compiled into the RTAS DLL.
  - returns candidates and optional segment metadata directly to the provider.
  - shares its implementation with the standalone diagnostic CLI, but does not
    launch that CLI during normal RTAS conversion.
- `MozcImm32Transport`
  - calls `ImmGetConversionListW(..., GCL_CONVERSION)`.
  - returns candidates only.
  - does not return segment metadata.

There is no automatic fallback from a failed `MozcBridgeTransport` call to
`MozcImm32Transport` inside `MozcConversionProvider`.

### `mozc_bridge.exe`

`tools/mozc_bridge/main.cpp` is more than a simple IMM32 bridge. Its preferred
path is:

1. Enumerate Google Japanese Input session pipes matching
   `\\.\pipe\googlejapaneseinput.*.session`.
2. Launch `GoogleIMEJaConverter.exe` from a hardcoded Program Files path when no
   pipe is found.
3. Send hand-built binary messages with hardcoded Mozc/Google protocol field
   numbers.
4. Parse candidate and segment data by walking protobuf-like wire fields.
5. Fall back to IMM32 if the pipe path produces no candidates.

The bridge also contains fallback heuristics:

- hardcoded layout probes such as Google TIP, Microsoft IME TIP, and legacy
  Japanese layout.
- heuristic delimiter/particle segmentation.
- segment reconstruction by cursor position and key length.
- candidate recomposition by appending segment tails.

These are useful as a prototype, but they are exactly the kind of hardcoding
and workaround behavior the native design should remove.

## Internal Dictionary Path

The internal dictionary provider is wired into the build and factory:

- `src/provider/dictionary_conversion_provider.*`
- `src/dictionary/morph_loader.*`
- `src/dictionary/bilingual_loader.*`
- `data/dictionary/morph_dict.tsv`
- `data/dictionary/jmdict.tsv`

What works today:

- morph TSV loading and indexing by surface and reading.
- bilingual TSV loading and indexing by headword, kanji forms, and kana forms.
- Layer1 returns raw reading, a segmented surface, and a small set of
  alternates.
- dictionary translation mode returns the first matching English gloss or a
  passthrough error result.
- LLM translation mode still delegates to the translation job queue.

What is partial:

- Segmentation is simplified DP, not the full design in
  `docs/dictionary/analysis_design.md`.
- There is no POS transition matrix.
- There is no feature-derived scoring model.
- Layer2 is currently Layer1 retagged as Layer2, not a beam-search rewrite.
- User learning settings are parsed but not used by the dictionary provider.
- The provider does not emit `CandidateList.segments`.
- Some tests and docs appear stale against the current provider constructor and
  runtime behavior.

Current hardcoded scoring and heuristic values include unknown-token penalty,
maximum span length, kana-only penalty, length prior, confidence constants, and
alternate-candidate cap. Those must become generated model data, validated
configuration, or measured decoder parameters before the internal dictionary
path can be treated as a principled replacement.

## Option Comparison

| Option | Summary | Pros | Cons | Recommendation |
| --- | --- | --- | --- | --- |
| Keep `bridge` | Continue using `mozc_bridge.exe` as an opt-in experiment. | Best short-term behavior; already returns candidates and some segment data. | External process per request; private Google pipe; hardcoded converter path/protocol/KLIDs; opaque fallback; terms review needed. | Keep as research baseline only. |
| Keep `imm32` | Use Windows IMM32 conversion list directly. | Simple, local fallback; required by compatibility goals. | Depends on active/installed IME; no segment metadata; limited control. | Keep as explicit fallback. |
| Add `native` | First-class OSS Mozc backend below `IMozcTransport` or `IConversionProvider`. | Removes private GoogleIME pipe; can expose real segment data; allows controlled packaging and tests. | Requires Mozc build, protocol/API decision, dependency packaging, quality evaluation. | Recommended design path. |
| Use internal dictionary now | Switch to RTAS `DictionaryConversionProvider`. | Fully local; deterministic; already in repo. | Prototype-level ranking; no mature language model; hardcoded scoring; no Layer2 beam; no segment metadata. | Do not use as Mozc replacement yet. |
| Build internal dictionary to parity | Treat RTAS dictionary as a long-term independent decoder. | Maximum control; no external IME dependency. | Large decoder project; needs model generation, scoring, learning, evaluation. | Run in parallel after native baseline is defined. |

## Recommended Migration Plan

### Phase 0: Baseline And Evidence

- Keep `transport=bridge` as the portfolio default until a public replacement
  proves equivalent candidate behavior.
- Keep `transport=imm32` available only for controlled research comparisons.
- Use `docs/operations/mozc_native_phase0_phase1.md` for the Phase 0 / Phase 1
  plan and `tests/samples/provider_comparison/phase0_cases.tsv` as the initial
  comparison corpus.
- Add a repeatable candidate comparison corpus before native implementation:
  - short words.
  - clauses.
  - sentences with particles.
  - proper nouns.
  - unknown words.
  - mixed kana/Latin/number input.
- Capture for each backend:
  - top-N candidates.
  - segment spans and surfaces.
  - errors.
  - cold and warm latency.
  - whether fallback was used.

### Phase 1: Typed Config Design

Phase 1 has implemented the first typed config layer for Mozc backends:

- `transport`: `bridge`, `server`, `imm32`, `native`, or invalid.
- `server` is retained as a bridge alias for compatibility.
- `native` constructs the app-local wrapper/server transport when its explicit
  artifacts validate; otherwise initialization fails closed.
- invalid transport values remain visible as configuration errors.

Phase 2 consumes only the minimal native settings needed for the fail-closed
`mozc_server_client` boundary:

- `native.backend`: `mozc_server_client` or `linked_converter`.
- `native.root` or explicit dependency root, resolved against install root.
- `native.mozc_build_artifact`: pinned Mozc artifact provenance.
- `native.fallback_policy`: `none`, `imm32`, or `bridge`, with logging.

Future native-only settings should be added only when a backend consumes them:

- `native.timeout_ms`, if the backend can block.
- `native.trace`: off by default, structured, no prompt/input persistence unless
  explicitly enabled.

### Phase 2: Native Backend Spike

Phase 2 must start with the comparison harness and backend route decision in:

- `docs/dictionary/mozc_native_backend_options.md`
- `tools/provider_compare/New-ProviderComparisonRun.ps1`
- `tests/manual/provider_comparison_phase0.md`

Build an opt-in native backend only after those materials are in place. The
backend must satisfy the existing `IMozcTransport` contract:

- input: one reading string.
- output: ordered candidate strings.
- output: authoritative segment metadata where available.
- output: structured error on backend failure.
- no UI-specific behavior.
- no generated candidates that are UI labels.
- no private pipe scraping.
- no manually encoded numeric protobuf fields.

Acceptable implementation routes:

1. Spawn or connect to OSS `mozc_server` through a client/session boundary that
   uses generated Mozc protocol definitions and a documented session lifecycle.
   This is the recommended first spike route.
2. Link or host an OSS Mozc converter component directly only if its dependency
   shape is proven manageable inside RTAS and the server/client route cannot
   meet latency or segment requirements.

Unacceptable implementation routes:

1. Copy `mozc_bridge` pipe scraping into RTAS.
2. Keep the hardcoded GoogleIME converter path.
3. Keep hardcoded protocol field numbers.
4. Guess segment boundaries when the backend fails to provide them.

### Phase 3: Evaluation Gate

Native should not become default until it passes a comparison gate:

- candidate popup under the project KPI for readings up to 10 characters.
- no worse than bridge for common corpus top-1/top-5 quality by manual review.
- segment metadata is present and coherent for multi-segment inputs.
- backend failures are visible and actionable.
- fallback behavior is explicit and logged.
- no hardcoded path/protocol/layout dependencies remain in the native path.

### Phase 4: Default Switch

Only after Phase 3 should RTAS consider changing the public default from
`imm32` to `native`. Even then, `bridge` should remain a research-only
comparison path and `imm32` a compatibility mode.

### Phase 5: Internal Dictionary Track

Internal dictionary work should continue, but as a separate decoder track:

- generate POS transition costs.
- normalize the dictionary cost space.
- replace hardcoded unknown and confidence constants.
- implement Layer2 beam search.
- emit segment metadata.
- add user learning into ranking.
- compare against bridge/native outputs using the same Phase 0 corpus.

## External Mozc Facts Affecting The Design

The OSS Mozc project is not identical to Google Japanese Input. Official Mozc
documentation states that Mozc is an OSS source release derived from Google
Japanese Input, not an officially supported Google product, and it has no
stable release concept. The branding notes also list differences in system
dictionary, collocation data, reading correction data, suggestion filter, QA,
and update behavior.

The current Windows build document uses Bazel/Bazelisk and shows a package/MSI
build path. It also lists a non-trivial dependency chain including Visual
Studio, Python, .NET, Bazelisk, Qt build steps, LLVM/MSYS2/Ninja downloads, and
GitHub Actions artifacts. Older GYP guidance is deprecated.

References:

- https://github.com/google/mozc
- https://code.googlesource.com/mozc/+/HEAD/doc/about_branding.md
- https://raw.githubusercontent.com/google/mozc/master/docs/build_mozc_in_windows.md
- https://github.com/google/mozc/blob/master/src/protocol/
- https://raw.githubusercontent.com/google/mozc/master/src/converter/converter_interface.h

Implication: native OSS Mozc is likely better engineered than current private
pipe scraping, but it may not match Google Japanese Input candidate quality
without measurement. The migration must compare outputs before changing
defaults.

## Risks And Open Questions

- Which Mozc API boundary is stable enough for RTAS: linked converter, client
  library, or server protocol?
- How should Mozc dependencies be built and stored in this repository without
  mixing build systems unsafely?
- What license and attribution files are required for Mozc source,
  third-party dependencies, and `dictionary_oss` data?
- Can native return segment metadata in exactly the shape RTAS needs, or does
  `CandidateList.segments` need richer fields?
- Which non-silent fallback policy should a real native backend expose, if any?
- Dictionary mode remains a prototype and still needs stronger ranking,
  Layer2, segment metadata, and learning integration before it can be treated
  as a Mozc replacement.
- The Phase 1 typed transport layer does not answer the Mozc build,
  dependency, packaging, or license questions for Phase 2.
- The Phase 2 preflight recommendation is `mozc_server_client` first,
  `linked_converter` second. The artifact pin is now
  `fea1ebace034ade31c611344793f559800e366c9`, with a verified GitHub Actions
  `Mozc64_x64.msi` artifact and `mozc_server.exe` start smoke. A real session
  command sequence still needs an official client/session wrapper build, which
  is blocked in this environment by missing Visual Studio ATL headers.

## Final Recommendation

Proceed with design toward `transport=native`, but treat it as a new first-class
backend, not as a refactor of the current bridge hacks. The typed Mozc
transport config and evaluation corpus are now in place; the next engineering
step is a native backend spike that uses a principled Mozc API/protocol boundary
and remains opt-in.

The internal dictionary path should not be positioned as the immediate Mozc
replacement. It is valuable, but it needs a real decoder model and measured
quality work before it can carry Layer1 conversion as the primary path.
