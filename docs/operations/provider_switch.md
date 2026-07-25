# Provider Switch Operations

This document describes the current `config/ime_settings.json` provider shape.

## Current Default

The checked-in public default restores the original bridge path for
kana-kanji conversion and uses LLM mode for translation:

```json
{
  "provider": {
    "kana": {
      "mode": "mozc",
      "mozc": {
        "enabled": true,
        "transport": "bridge"
      }
    },
    "translation": {
      "mode": "llm"
    }
  }
}
```

## Kana Provider Modes

| Mode | Status | Notes |
| --- | --- | --- |
| `mozc` | Current default | Uses `provider.kana.mozc.transport`; public default is `bridge`. |
| `dictionary` | Available prototype | Uses TSV dictionaries; not Mozc parity. |
| `llm` | Legacy/fallback mode | Kept for compatibility with older configs. |

## Mozc Transport Values

`provider.kana.mozc.transport` is parsed into a typed transport value.

| JSON value | Typed value | Status |
| --- | --- | --- |
| `bridge` | `MozcTransport::kBridge` | Current default; preserves the original Google Japanese Input conversion path. |
| `server` | `MozcTransport::kBridge` | Legacy alias for `bridge`. |
| `imm32` | `MozcTransport::kImm32` | Experimental direct IMM32 path; not the default because it returned no candidates in live testing. |
| `native` | `MozcTransport::kNative` | Phase 3 app-local `mozc_server_client` runtime; opt-in only. |

Invalid transport strings are configuration errors. They must not silently
downgrade to `imm32`, `bridge`, or `llm`.

The bridge depends on a private Google Japanese Input session boundary. It is
the checked-in default only to preserve the working portfolio experience.
It is not a stable public API or a production recommendation.

`server` here is only the legacy RTAS alias for the current bridge transport.
It does not mean the future OSS Mozc `mozc_server_client` native route.

## Native Status

`transport = "native"` is recognized as a typed value and now has a minimal
Phase 3 app-local `mozc_server_client` runtime path. It is still opt-in and is
not a default backend.

Selecting `native` must either call the configured app-local wrapper/server
boundary or report a native backend-unavailable/runtime error. It must not
return fake candidates or copy the current Google Japanese Input private pipe
bridge into RTAS.

Phase 3 consumes the following native block shape. `wrapper_exe` and
`server_exe` are explicit so RTAS does not guess local install paths or rely on
system-wide Mozc state:

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

The Phase 3 runtime calls a thin wrapper built from the pinned OSS Mozc source
tree. The wrapper uses Mozc generated protocol/client types and accepts the
reading through a temporary UTF-8 file to avoid Windows argv code page issues.
Artifact absence, startup failure, conversion failure, and timeout are reported
as provider errors with `fallback_used=false`.

The `linked_converter` route remains documented as a future extension point if
the server/client route cannot meet quality, segment, or lifecycle needs. Native
remains opt-in and must pass the comparison gate before any default change.

## Dictionary Mode

Dictionary mode is available as a prototype backend:

```json
{
  "provider": {
    "kana": {
      "mode": "dictionary",
      "dictionary": {
        "enabled": true,
        "morph_tsv": "data/dictionary/morph_dict.tsv",
        "bilingual_tsv": "data/dictionary/jmdict.tsv"
      }
    }
  }
}
```

The internal dictionary path currently loads TSV assets and can return
dictionary candidates, but it lacks Mozc-grade ranking, POS transition scoring,
authoritative segment metadata, and mature user-learning integration.

## Translation Provider

Translation is configured separately under `provider.translation`:

```json
{
  "provider": {
    "translation": {
      "mode": "llm",
      "llm": {
        "model": "default",
        "host": "127.0.0.1",
        "port": 11434,
        "path": "/api/generate",
        "use_tls": false,
        "timeout_ms": 3000,
        "keep_alive": -1,
        "warmup_on_activate": true,
        "warmup_timeout_ms": 60000,
        "unload_on_deactivate": true,
        "unload_delay_ms": 10000,
        "log_timings": true
      }
    }
  }
}
```

`provider.translation.mode = "dictionary"` is valid only when
`provider.translation.dictionary.enabled = true`; otherwise it falls back to
LLM with a configuration error.

## Ollama Residency

RTAS defaults to a resident Ollama model for LLM translation. The checked-in
configuration sends `keep_alive = -1`, starts a warmup request when RTAS is
activated by TSF, and sends an unload request after RTAS is deactivated and no
other RTAS activation remains active in the process.

The default favors input experience over memory conservation. Disabling
`warmup_on_activate`, clearing/changing `keep_alive`, or disabling
`unload_on_deactivate` is supported for memory-sensitive environments, but it
can reintroduce cold model load latency and timeout risk on the next
translation request.

RTAS uses Ollama's documented `/api/generate` behavior:

- empty generate request with a model loads the model;
- `keep_alive = -1` keeps it resident;
- empty generate request with `keep_alive = 0` unloads it.

`unload_delay_ms` defaults to `10000` so quick TSF reactivation does not unload
and immediately reload the same model. If RTAS is reactivated during the delay,
the pending unload is cancelled. `log_timings = true` records wall time plus
Ollama `load_duration` and `total_duration` when Ollama returns them.

## Debug File Logging

RTAS keeps `DebugLog()` output on `OutputDebugStringW` for DebugView, and can
also mirror the same messages to a UTF-8 file when explicitly enabled:

```json
{
  "provider": {
    "logging": {
      "debug_file": {
        "enabled": true,
        "path": "logs/rtas-debug.log",
        "max_bytes": 1048576
      }
    }
  }
}
```

`path` may be absolute or relative to the RTAS install/output directory. For a
Debug x64 local build, the default relative path resolves to
`x64/Debug/logs/rtas-debug.log`. The logger rotates one previous file to
`rtas-debug.log.1` when `max_bytes` is exceeded. File logging is disabled by
default because debug output can contain operational details from the active
IME process.

## Operational Rules

- Do not commit local provider switches made for manual testing.
- Restart the text service after changing `config/ime_settings.json`.
- Enable `provider.logging.debug_file` only for a targeted investigation, then
  turn it off or archive the log after sharing.
- Keep `bridge` and `imm32` available while native work is in progress.
- Do not make `native` the default until the native implementation gate in
  `docs/operations/mozc_native_phase0_phase1.md` passes.

## Provider Comparison

Use the corpus and JSONL harness when comparing backends:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode template `
  -Backend bridge `
  -Output tmp_provider_comparison/bridge.jsonl

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/bridge.jsonl
```

Comparison backends are recorded as `bridge`, `server`, `imm32`, `dictionary`,
or `native`. `server` remains only the legacy bridge alias, not
`mozc_server_client`.

Native comparison records must include `native_backend`, `protocol_source`,
`mozc_commit`, `mozc_build_artifact`, `fallback_used`, and `fallback_source`.
Phase 3 templates also record `native_runtime`,
`native_wrapper_exe`, and `native_server_exe` for app-local provenance.
Any fallback run must fill both `fallback_used` and `fallback_source`.

For the first `mozc_server_client` proof, use the connection plan and manifest
shape in:

- `docs/dictionary/mozc_server_client_connection_plan.md`
- `docs/dictionary/mozc_native_artifact_protocol_smoke.md`
- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`

Mozc binaries, build output, Qt output, and Mozc `third_party` dependencies
should remain outside the RTAS repository unless a separate human/license
review approves bundling.
