# Provider Comparison Harness

`New-ProviderComparisonRun.ps1` prepares and validates JSONL captures for
comparing RTAS kana-kanji backends while `transport=native` remains opt-in.

It does not mutate `config/ime_settings.json`. Inputs are limited to
`tests/samples/provider_comparison/phase0_cases.tsv`.

## Create Capture Template

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode template `
  -Backend bridge `
  -Output tmp_provider_comparison/bridge.jsonl
```

Backends:

- `bridge`
- `server` legacy alias; effective transport is `bridge`
- `imm32`
- `dictionary`
- `native` Phase 3 app-local `mozc_server_client` runtime shape; no fake
  candidates and no silent fallback

## Validate Capture

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/bridge.jsonl
```

Validation checks that each row points to a known corpus id, uses the exact
corpus reading/committed text, uses an allowed backend name, and records a
fallback source when fallback is marked as used. Fallback sources are limited to
`bridge`, `imm32`, and `dictionary`.

Native records are stricter. For the Phase 3 app-local server/client spike they
must use:

```json
{
  "backend": "native",
  "transport": "native",
  "effective_transport": "native",
  "native_backend": "mozc_server_client",
  "protocol_source": "generated Mozc session proto/client boundary",
  "mozc_commit": "Mozc source SHA or artifact provenance",
  "mozc_build_artifact": "local artifact root, MSI/package id, or CI artifact id",
  "native_runtime": "app_local_mozc_server_client",
  "native_wrapper_exe": "wrapper executable provenance or configured path",
  "native_server_exe": "mozc_server executable provenance or configured path",
  "error": "mozc_server_client backend unavailable",
  "fallback_used": false,
  "fallback_source": "",
  "input_scope": "repo_corpus"
}
```

`protocol_source` must describe generated Mozc protocol types or the OSS Mozc
client/session boundary. It must not refer to Google Japanese Input private
named pipes, hand-coded protobuf field numbers, or private protocol scraping.

For the first `mozc_server_client` proof, use the artifact/protocol manifest
shape documented in:

- `docs/dictionary/mozc_server_client_connection_plan.md`
- `docs/dictionary/mozc_native_artifact_protocol_smoke.md`
- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`
- `tests/samples/provider_comparison/native_artifact_server_start_smoke.jsonl`

The non-invasive native probe can create a compatible artifact smoke file
without installing Mozc or changing system-wide IME state:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1 `
  -ServerStartSmoke `
  -Output tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

Nested fields such as `artifact_probe`, `server_probe`,
`server_start_smoke`, `session_lifecycle`, `candidate_extraction`, and
`segment_extraction` are accepted as extra smoke metadata. The validator still
enforces the core native provenance fields listed above.

Ordinary captures should stay under `tmp_provider_comparison/` or another
ignored local directory. Commit a result file only when it is intentionally
reviewed as a baseline fixture.
