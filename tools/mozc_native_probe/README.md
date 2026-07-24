# Mozc Native Probe

`Invoke-MozcNativeProbe.ps1` creates provider-comparison-compatible JSONL for
the Phase 2A / 2B `mozc_server_client` artifact proof.

The probe is intentionally non-invasive:

- it does not download Mozc.
- it does not run `update_deps.py`.
- it does not build Qt.
- it does not install `Mozc64.msi`.
- it does not change system-wide IME state.
- it does not use Google Japanese Input private pipes.

When the manifest artifact is absent, the probe writes native records with
`segment_source=unavailable`, empty candidates, `fallback_used=false`, and an
explicit artifact-unavailable error. This preserves the provider comparison
schema without returning fake native candidates.

When the manifest points at a reviewed external artifact, the probe can also
verify the MSI path, verify the extracted `mozc_server.exe`, and optionally
start that server briefly. It still does not create a Mozc session or return
native candidates.

## Generate An Artifact Smoke File

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1 `
  -ServerStartSmoke `
  -Output tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

Validate with the existing provider comparison harness:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

The reviewed fixture for the current GitHub Actions artifact is:

- `tests/samples/provider_comparison/native_artifact_server_start_smoke.jsonl`

## Generate An Unavailable Smoke File

If the artifact path is intentionally absent, omit `-ServerStartSmoke` and
write to a local file:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1 `
  -Output tmp_provider_comparison/native_artifact_unavailable.jsonl
```

## Optional Client Wrapper Source

`rtas_mozc_client_probe.cc` and `rtas_mozc_client_probe.BUILD.bazel` are a
minimal source template for building a real Mozc client/session smoke inside a
pinned external Mozc checkout. Copy them to:

- `src/rtas_probe/rtas_mozc_client_probe.cc`
- `src/rtas_probe/BUILD.bazel`

Then build from the Mozc `src` directory:

```powershell
bazelisk --nowindows_enable_symlinks build `
  --config oss_windows `
  --config release_build `
  --noenable_runfiles `
  //rtas_probe:rtas_mozc_client_probe
```

The wrapper uses Mozc's official `client::ServerLauncher`, so Windows server
startup goes through Mozc's sandboxed launch path instead of a raw
`CreateProcess` call. Direct unsandboxed server startup is expected to fail
Mozc's Windows run-level checks.

For Japanese readings, prefer `--reading_file` over `--reading` so the smoke
does not depend on Windows command-line code page conversion:

```powershell
Set-Content -LiteralPath ..\native_probe_reading_utf8.txt `
  -Value 'きょうはいいてんきです' `
  -Encoding utf8

.\bazel-bin\rtas_probe\rtas_mozc_client_probe.exe `
  --server_path=.\bazel-bin\server\mozc_server.exe `
  --reading_file=..\native_probe_reading_utf8.txt `
  --top_n=8 `
  --timeout_ms=10000
```

The current environment has Visual Studio ATL/MFC installed and can build both
`//rtas_probe:rtas_mozc_client_probe` and `//server:mozc_server`. The wrapper
has proven session creation, IME-on, UTF-8 reading input, conversion, candidate
extraction, and preedit segment extraction against the locally built
`mozc_server.exe`. The administratively extracted MSI `mozc_server.exe` remains
unreachable through this wrapper without a full install context.

Commit only deliberate fixtures. Ordinary local smoke runs should stay under
`tmp_provider_comparison/`.
