# Mozc Native Phase 0 / Phase 1 Plan

Date: 2026-05-19
Scope: preserve all current RTAS behavior while preparing for a future
`transport=native` implementation. Phase 1 now includes the typed Mozc
transport parser and validation layer, but not the native conversion engine.

> Historical decision record: this phase plan predates the 2026-07-25 public
> snapshot work. A temporary hardening change selected `transport=imm32`, but
> live testing returned no candidates. The checked-in default therefore
> restores `transport=bridge` to preserve the original portfolio behavior.
> Later Phase 3 work implemented the opt-in app-local wrapper/server runtime;
> external Mozc build artifacts are intentionally not bundled here.

## Decision

Do not implement the native conversion engine in this phase. The completed
Phase 1 step is the typed transport contract, the evaluation corpus, and the
gate that a future implementation must pass before it can become a default
backend.

This avoids a false native implementation that only moves the current bridge
workarounds into the RTAS DLL.

## Feature Preservation Contract

The following existing behavior must remain unchanged during Phase 0 / Phase 1:

- `transport=bridge` is the active configured path.
- `transport=imm32` remains available only for explicit comparison testing.
- `transport=native` is not the default.
- Layer1 still enters through `IConversionProvider::FetchLayer1`.
- Layer2 still enters through `IConversionProvider::FetchLayer2`.
- Translation still enters through `IConversionProvider::FetchTranslation`.
- Candidate UI, cached Space cycling, Shift+Space refresh, pending placeholders,
  and translation commit policy remain provider-agnostic.
- TextService must not gain native-specific UI branches.
- The Google Japanese Input private named pipe protocol must not be moved into
  RTAS proper.

## Typed Transport Model

The JSON config still stores `provider.kana.mozc.transport` as a string for
backward compatibility, but `src/config/provider_settings.*` parses it into a
typed `MozcTransport` value. Unsupported strings are preserved for diagnostics
and marked invalid instead of being silently downgraded.

| Config value | Type value | Current runtime state | Notes |
| --- | --- | --- | --- |
| `bridge` | `MozcTransport::kBridge` | Public snapshot default | Calls the bridge implementation compiled into the DLL; a standalone diagnostic CLI is also built. |
| `server` | `MozcTransport::kBridge` | Supported legacy alias | Kept only for backward compatibility. |
| `imm32` | `MozcTransport::kImm32` | Experimental comparison path | Direct IMM32 candidate path; live testing returned no candidates on the submission machine. |
| `native` | `MozcTransport::kNative` | Phase 3 app-local `mozc_server_client` runtime path | Must stay opt-in and non-default until the native gate passes. |
| unsupported string | `MozcTransport::kInvalid` | Configuration error | Does not fall back to `imm32`, `bridge`, or `llm`. |

Unsupported values must be configuration errors. They must not silently become
`imm32`, `bridge`, or `llm`.

`transport=native` is no longer a fake or always-unavailable path. Phase 3
initializes only when explicit app-local wrapper/server paths are configured
and present. Missing artifacts, failed startup, failed conversion, and timeout
remain visible errors, and no fallback is used unless a later explicit fallback
test is designed and recorded.

### Future Native Config Shape

The native block is consumed by the Phase 3 app-local server/client runtime:

```json
{
  "provider": {
    "kana": {
      "mode": "mozc",
      "mozc": {
        "enabled": true,
        "transport": "native",
        "native": {
          "backend": "mozc_server_client",
          "root": "../rtas-artifacts/mozc/source/src/bazel-bin",
          "mozc_build_artifact": "../rtas-artifacts/mozc/fea1ebace034ade31c611344793f559800e366c9/artifact_zip/Mozc64.msi",
          "wrapper_exe": "../rtas-artifacts/mozc/source/src/bazel-bin/rtas_probe/rtas_mozc_client_probe.exe",
          "server_exe": "../rtas-artifacts/mozc/source/src/bazel-bin/server/mozc_server.exe",
          "timeout_ms": 5000,
          "top_n": 8,
          "fallback_policy": "none"
        }
      }
    }
  }
}
```

Planned native value domains:

| Field | Allowed values | Rule |
| --- | --- | --- |
| `native.backend` | `mozc_server_client`, `linked_converter` | Required once `transport=native` is implemented. `mozc_server_client` is the first spike candidate. |
| `native.root` | install-root-relative or absolute path | No hardcoded Program Files path. |
| `native.mozc_build_artifact` | install-root-relative or absolute path | Records the pinned Mozc server/client artifact provenance for evaluation. |
| `native.wrapper_exe` | install-root-relative or absolute path | Thin OSS Mozc client wrapper; required for Phase 3 runtime initialization. |
| `native.server_exe` | install-root-relative or absolute path | App-local `mozc_server.exe`; required for Phase 3 runtime initialization. |
| `native.timeout_ms` | positive integer | Wrapper process and Mozc IPC timeout. |
| `native.top_n` | positive integer | Maximum native candidates to request from the wrapper. |
| `native.fallback_policy` | `none`, `imm32`, `bridge` | Default must be `none`. Fallback must be visible and logged. |

Future native-only fields such as `native.trace` should be added only when code
consumes them in the same change. `native.trace` must remain off by default and
must not persist user input unless a later privacy design permits it.

Do not make this block the checked-in default. It is valid only for opt-in
spike/testing configs until native passes the implementation gate.

## Evaluation Corpus

The Phase 0 corpus lives at:

- `tests/samples/provider_comparison/phase0_cases.tsv`

The corpus intentionally stores inputs and coverage notes, not fixed expected
candidate strings. Candidate quality must be compared across backends instead
of hardcoding a single "correct" output.

Required coverage:

- short words.
- clauses.
- particle-heavy sentences.
- proper nouns.
- unknown words.
- mixed numbers and Latin text.
- longer sentences.
- punctuation and delimiter cases.

## Comparison Procedure

Detailed manual procedure:

- `tests/manual/provider_comparison_phase0.md`

JSONL template and validation harness:

- `tools/provider_compare/New-ProviderComparisonRun.ps1`

The harness creates capture records from the checked-in corpus and validates
that completed result files do not introduce non-corpus input. Store ordinary
comparison runs under `tmp_provider_comparison/` or another ignored/local
directory unless a baseline fixture is deliberately reviewed for commit.

For each corpus row and backend under test:

1. Configure the backend:
   - `provider.kana.mozc.transport = "bridge"`
   - `provider.kana.mozc.transport = "imm32"`
   - `provider.kana.mode = "dictionary"`
   - future: `provider.kana.mozc.transport = "native"`
2. Restart the RTAS text service so config is reloaded.
3. Input the row's `reading`.
4. Capture:
   - top-N Layer1 candidates.
   - segment spans and segment surfaces, if present.
   - provider error text.
   - whether fallback was used.
   - cold latency.
   - warm latency.
   - whether Layer2 can use the selected Layer1 result.
   - whether Translation can use the selected Layer2 or Layer1 result.
5. Record observations in JSONL outside the source tree unless
   it is being added as a deliberate baseline review fixture.

Recommended result schema:

```json
{
  "corpus_id": "short_001",
  "backend": "bridge",
  "top_candidates": ["..."],
  "segments": [
    {"index": 0, "start": 0, "length": 2, "surface": "..."}
  ],
  "error": "",
  "fallback_used": false,
  "cold_latency_ms": 0,
  "warm_latency_ms": 0,
  "layer2_ok": true,
  "translation_ok": true,
  "notes": ""
}
```

## Native Implementation Gate

`transport=native` must not become the default until all of these are true:

- It uses a typed Mozc API or generated Mozc protocol definitions.
- It does not use Google Japanese Input private named pipes.
- It does not manually encode protobuf field numbers.
- It does not hardcode install paths, pipe names, KLIDs, or scoring constants.
- Invalid transport values fail validation visibly.
- Fallback policy is explicit, off by default, and observable.
- It returns ordered candidates through the same `CandidateList` contract as
  existing providers.
- It returns authoritative segment metadata, or clearly reports that segment
  metadata is unavailable.
- Candidate popup latency meets the RTAS KPI for readings up to 10 characters.
- The evaluation corpus shows no regression in existing bridge behavior.
- Layer2 and Translation flows continue to work without TextService
  native-specific branches.
- `bridge` and `imm32` remain available.

## Default Switch Blockers

Do not make native the default if any of these remain true:

- Native has lower candidate quality than bridge on common corpus rows.
- Native falls back silently.
- Native requires hardcoded local installation paths.
- Native lacks a reproducible Mozc build/dependency story.
- Native cannot surface meaningful initialization or runtime errors.
- Native requires candidate UI or TextService special cases.
- Native segment metadata is guessed rather than provided by the backend.

## Implementation Sequence

1. Keep this phase document and corpus in place.
2. Completed: introduce a typed `MozcTransport` enum while keeping the existing
   `transport` config string as the serialized input.
3. Completed: add config validation coverage for `bridge`, `server`, `imm32`,
   `native`, and unsupported values.
4. Completed for Phase 3: parse native app-local runtime settings consumed by
   the `mozc_server_client` boundary.
5. Completed: add a JSONL comparison template/validator for
   `bridge`, `server`, `imm32`, `dictionary`, and future `native`.
6. In progress: implement native as an opt-in backend below the provider
   boundary; current state calls an external wrapper when explicit app-local
   artifacts are configured.
7. Run the evaluation corpus before considering any default change.

## Phase 2 Handoff

Phase 2 should start with `mozc_server_client` as the recommended native spike
route and keep `linked_converter` as a second route if server/client cannot meet
latency or segment needs. See:

- `docs/dictionary/mozc_native_backend_options.md`

Before reviewing a native spike:

- pin the Mozc source revision or artifact provenance.
- record license and third-party attribution requirements.
- identify the generated Mozc protocol sources or typed API boundary used.
- extend the provider comparison harness from template/validation into
  execution for the real native backend.
- keep fallback disabled by default and observable when enabled.
- treat native unavailable records as valid only when JSONL includes
  `native_backend=mozc_server_client`, non-empty `protocol_source`, non-empty
  `mozc_commit`, non-empty `mozc_build_artifact`, `fallback_used`, and
  `fallback_source`.

The Phase 2 server/client connection plan is now tracked in:

- `docs/dictionary/mozc_server_client_connection_plan.md`
- `docs/dictionary/mozc_native_artifact_protocol_smoke.md`
- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`

That plan defines the initial Mozc source pin, Windows artifact shape,
license/notices checklist, protocol boundary, session lifecycle, and
candidate/segment proof requirements. It intentionally does not install Mozc,
download large dependencies, vendor Mozc `third_party`, or make native the
default.

The current Phase 2A / 2B probe and Phase 3 transport support these reviewed
states:

- artifact absent: explicit unavailable native smoke.
- artifact present: pinned GitHub Actions MSI, administrative extraction, and
  `mozc_server.exe` start smoke with session/candidate/segment extraction still
  marked `not_run_client_wrapper_missing`.
- local source build present: external `rtas_mozc_client_probe.exe` can use
  Mozc's official `client::ServerLauncher` with a locally built
  `bazel-bin/server/mozc_server.exe` to prove session creation, IME-on,
  conversion, candidates, and preedit segments.
- RTAS native transport present: `transport=native` can call the configured
  app-local wrapper/server pair, parse wrapper JSON candidates and preedit
  segment surfaces, and surface runtime errors without fallback.

It does not return fake candidates and keeps `fallback_used=false`.

The client/session wrapper build now requires Visual Studio ATL/MFC to remain
installed. The next blocker is corpus-level comparison and deciding whether the
evaluated runtime should be packaged from the local Bazel build, a full MSI
install context, or another reviewed app-local artifact layout.
