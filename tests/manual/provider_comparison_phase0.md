# Provider Comparison Phase 0 Manual Procedure

Use this procedure to compare current and future kana-kanji backends without
changing RTAS runtime behavior by default.

The app-local native wrapper/server boundary is implemented, but this public
repository intentionally does not bundle its external Mozc build artifacts.
The checked-in unavailable fixture documents the no-artifact smoke case rather
than claiming that the runtime boundary is unimplemented.

## Inputs

- Corpus: `tests/samples/provider_comparison/phase0_cases.tsv`
- Backends:
  - `bridge`: `provider.kana.mode = "mozc"`,
    `provider.kana.mozc.transport = "bridge"`
  - `imm32`: `provider.kana.mode = "mozc"`,
    `provider.kana.mozc.transport = "imm32"`
  - `dictionary`: `provider.kana.mode = "dictionary"`
  - `native`: opt-in app-local `mozc_server_client` path; requires explicitly
    configured wrapper and server artifacts.

Do not commit local config flips used for manual comparison.

Evaluation logs must be limited to rows from the repo corpus above. Do not add
arbitrary personal input, active editor text, full DebugView dumps, native trace
dumps, prompt text, or persisted user input to comparison artifacts.

## JSONL Harness

Use `tools/provider_compare/New-ProviderComparisonRun.ps1` to create one JSONL
record per corpus row and to validate filled comparison results.

Create an empty capture file for a backend:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode template `
  -Backend bridge `
  -Output tmp_provider_comparison/bridge.jsonl
```

Supported `-Backend` values are `bridge`, `server`, `imm32`, `dictionary`, and
`native`. `server` records the legacy RTAS alias and has
`effective_transport = "bridge"`. `native` records the app-local
`mozc_server_client` path. It can perform conversion when the configured wrapper
and server artifacts exist; otherwise it must record an explicit unavailable
error without silently falling back.

Validate a filled capture file before comparing it:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/bridge.jsonl
```

The validator rejects rows whose `corpus_id` is unknown, whose `reading` or
`committed_text` differs from the corpus, whose backend name is unsupported, or
whose `fallback_used=true` has no `fallback_source`. This is the privacy guard:
backend outputs may be recorded, but the inputs under evaluation must remain
the checked-in corpus inputs. Native rows must also include
`native_backend=mozc_server_client`, non-empty `protocol_source`, non-empty
`mozc_commit`, non-empty `mozc_build_artifact`, and explicit fallback fields.

For Phase 2A / 2B artifact smoke, `tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1`
can generate native records from the checked-in manifest and corpus without
downloading, building, or installing Mozc. When no artifact exists, the expected
result is an explicit unavailable error with `fallback_used=false` and
`segment_source=unavailable`.

## Capture Boundary

Compare at the provider boundary where possible:

- `CandidateList.entries`
- `CandidateList.segments`
- `CandidateList.error`
- `pending` / `requestId`

If using the current app UI instead of a harness, capture the same information
manually from the candidate window, debug output, and bridge CLI output.

## Bridge Provenance Caveat

`mozc_bridge.exe` can print `DBG` lines such as segment source and segment
reason for standalone diagnosis. The in-DLL `MozcBridgeTransport` does not parse
that text protocol: it receives candidates and segment structures directly from
`QueryCandidatesInProcess()`. If additional fallback/source provenance matters,
collect it from the standalone CLI or extend the structured bridge response
explicitly.

## Per-Backend Steps

Backend configuration matrix:

| Backend record | Required config shape | Expected state |
| --- | --- | --- |
| `bridge` | `provider.kana.mode = "mozc"` and `provider.kana.mozc.transport = "bridge"` | Opt-in research baseline; the DLL calls the compiled-in bridge implementation. |
| `server` | `provider.kana.mode = "mozc"` and `provider.kana.mozc.transport = "server"` | Legacy alias only; expected to behave as `bridge`. |
| `imm32` | `provider.kana.mode = "mozc"` and `provider.kana.mozc.transport = "imm32"` | Public-snapshot default; segment metadata may be unavailable. |
| `dictionary` | `provider.kana.mode = "dictionary"` and dictionary assets enabled/resolved | Prototype backend; not Mozc parity. |
| `native` | `provider.kana.mode = "mozc"`, `provider.kana.mozc.transport = "native"`, and `provider.kana.mozc.native.backend = "mozc_server_client"` | Opt-in app-local wrapper/server path; unavailable without explicit artifacts. |

If using the running RTAS config instead of a provider-boundary harness:

1. Save a local backup of `config/ime_settings.json`.
2. Set the backend under test.
3. Restart the RTAS text service so the config is reloaded.
4. For each corpus row:
   - enter `reading`.
   - record top-N Layer1 candidates.
   - record segment spans and surfaces if available.
   - record visible or logged provider errors.
   - record whether fallback was observed.
   - record cold latency for the first run.
   - repeat the same input and record warm latency.
   - for `layer_flow` rows, verify Layer2 and Translation still use the
     selected source text.
5. Restore the original config after the run.

## Suggested Result Schema

```json
{
  "schema_version": 1,
  "corpus_id": "short_001",
  "category": "short_word",
  "reading": "...",
  "committed_text": "",
  "backend": "bridge",
  "kana_mode": "mozc",
  "transport": "bridge",
  "effective_transport": "bridge",
  "native_backend": "",
  "top_n": 8,
  "top_candidates": ["..."],
  "entries": [],
  "segments": [
    {"index": 0, "start": 0, "length": 2, "surface": "..."}
  ],
  "segment_source": "preedit|candidate_list|imm32|dictionary|unavailable",
  "error": "",
  "pending": false,
  "request_id": null,
  "fallback_used": false,
  "fallback_source": "",
  "cold_latency_ms": 0,
  "warm_latency_ms": 0,
  "layer2_impact": {"checked": true, "result": "ok", "notes": ""},
  "translation_impact": {"checked": true, "result": "ok", "notes": ""},
  "source_provenance": "manual_provider_boundary",
  "protocol_source": "",
  "mozc_commit": "",
  "mozc_build_artifact": "",
  "input_scope": "repo_corpus",
  "notes": ""
}
```

The example values above show the shape only. Do not treat them as expected
outputs.

For native unavailable/spike captures:

- `native_backend` must be `mozc_server_client`.
- `protocol_source` must name generated Mozc protocol definitions or the
  typed Mozc API boundary used.
- `mozc_commit` must record the Mozc source revision or artifact provenance.
- `mozc_build_artifact` must record the local build artifact root or package
  identifier.
- `fallback_used` must be `false` and `fallback_source` must be empty unless a
  separate explicit fallback-policy test is being reviewed.

The deliberate unavailable fixture for the current workspace is:

- `tests/samples/provider_comparison/native_artifact_unavailable_smoke.jsonl`

## Pass Criteria For This Phase

- `bridge` behavior remains unchanged.
- `imm32` remains available.
- `dictionary` remains comparable as a prototype backend, not as Mozc parity.
- No comparison procedure requires private protocol parsing in RTAS.
- No exact candidate string is hardcoded as the only acceptable result.
- Native results are accepted only when artifact provenance and runtime fields
  identify the explicit app-local `mozc_server_client` path.
- Any fallback is explicit in `fallback_used` and `fallback_source`.
- Result files pass the JSONL harness validator before they are used for
  backend decisions.
