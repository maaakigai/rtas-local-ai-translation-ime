# Mozc Native Artifact / Protocol Smoke

Date: 2026-06-12
Scope: Phase 2A / 2B artifact proof and protocol smoke status for the OSS Mozc
`mozc_server_client` route.

## Result

The RTAS repository now has a repeatable native smoke probe that can load a
pinned Mozc artifact manifest, verify a GitHub Actions MSI artifact, verify an
administrative extraction, start `mozc_server.exe` briefly, and emit
provider-comparison-compatible JSONL.

Phase 3 wires RTAS `transport=native` to the external client/session wrapper
when explicit app-local wrapper/server paths are configured. This remains an
opt-in spike path, not a default backend and not a Mozc binary bundling
decision.

No Mozc MSI install, IME registration, registry change, system-wide IME change,
or Mozc binary vendoring into RTAS was performed.

## Artifact Proof

Manifest:

- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`

Pinned source/artifact:

- repository: `https://github.com/google/mozc`
- commit: `fea1ebace034ade31c611344793f559800e366c9`
- workflow: `CI for Windows`
- run id: `27324141219`
- artifact id: `7555700414`
- artifact name: `Mozc64_x64.msi`
- archive digest:
  `sha256:4e18b4363fc264b2325c8545a08cfea6664e70773afe9b4afb4b0e61fe85cbca`
- expiry: `2026-09-09T04:37:19Z`

Local external cache:

- `../rtas-artifacts/mozc/fea1ebace034ade31c611344793f559800e366c9/`

Verified local files:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `Mozc64_x64.msi.artifact.zip` | 24884594 | `4e18b4363fc264b2325c8545a08cfea6664e70773afe9b4afb4b0e61fe85cbca` |
| `artifact_zip/Mozc64.msi` | 26578944 | `a7d3113ee44fa096b7bc2cbafbcf7cb36ff444b2e62a84f7bdbb877acceb9fa9` |
| `msi_admin_extract/PFiles/Mozc/mozc_server.exe` | 22582272 | `7ed659bb4ba7a6074a946fe7c5729df08e21fce5e15e15a6ef4af3bf60296a00` |
| `msi_admin_extract/PFiles/Mozc/documents/credits_en.html` | 34395 | `dd7c566382204f448040095e5d3c2f2b5199a1b85d44f427ceb94277dc04af50` |

The MSI was extracted with `msiexec /a` into the external cache. It was not
installed.

## Server Start Smoke

Probe:

- `tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1`

Reviewed fixture:

- `tests/samples/provider_comparison/native_artifact_server_start_smoke.jsonl`

The fixture is generated only from `tests/samples/provider_comparison/phase0_cases.tsv`.
It records:

- `native_backend=mozc_server_client`
- generated Mozc protocol provenance from `src/protocol/commands.proto` and
  `src/protocol/candidate_window.proto`.
- pinned Mozc commit and GitHub Actions artifact provenance.
- `fallback_used=false`
- artifact and `mozc_server.exe` hashes.
- `server_start_smoke.status=ok`
- session/candidate/segment statuses as `not_run_client_wrapper_missing`.

This proves artifact discovery and executable startup. It does not prove
candidate quality, segment availability, or session lifecycle.

## Client / Session Smoke

External source checkout:

- `../rtas-artifacts/mozc/source`
- detached at `fea1ebace034ade31c611344793f559800e366c9`

Dependency setup:

- `python src/build_tools/update_deps.py` completed successfully.
- Bazel symlink mode failed without Developer Mode/admin symlink privilege.
- Re-running Bazel with `--nowindows_enable_symlinks --noenable_runfiles`
  reached compilation.
- Visual Studio components `Microsoft.VisualStudio.Component.VC.ATL` and
  `Microsoft.VisualStudio.Component.VC.ATLMFC` were added through an elevated
  Visual Studio Installer run.

Build results:

- `//rtas_probe:rtas_mozc_client_probe`
  - result: build succeeded.
  - output: `bazel-bin/rtas_probe/rtas_mozc_client_probe.exe`
- `//server:mozc_server`
  - result: build succeeded.
  - output: `bazel-bin/server/mozc_server.exe`

The first wrapper version used direct `CreateProcess` and failed because
Windows `mozc_server` requires Mozc's sandboxed launcher path to pass
`RunLevel::SERVER` checks. The wrapper now derives from Mozc's official
`client::ServerLauncher` and overrides only `server_program()` so startup uses
the same sandbox path as Mozc's client.

Verified local client/session smoke:

```json
{"ok":true,"connection_ok":true,"session_ok":true,"turn_on_ime_ok":true,"text_input_ok":true,"convert_ok":true,"reading":"きょうはいいてんきです","top_candidates":["今日は","きょうは","教は","強は","凶は","経は","卿は","興は"],"segments":["今日は","いい天気です"],"has_all_candidate_words":true,"has_preedit":true,"fatal_count":0,"error":""}
```

Important limitation:

- The smoke above uses the locally built `bazel-bin/server/mozc_server.exe`.
- The administratively extracted MSI `mozc_server.exe` is still unreachable
  through the wrapper without a full install context.
- `--reading_file` is used for Japanese readings to avoid Windows command-line
  code page conversion. Plain `--reading` is still suitable for ASCII smoke.
- RTAS follows the same rule: native transport writes the active reading to a
  temporary UTF-8 file, launches the configured wrapper, reads its JSON stdout,
  and deletes the temporary file immediately.
- A Bazel auto-configuration warning remains because Visual Studio English
  language pack is not installed; it disables header pruning but did not block
  the build.

## Protocol Boundary

Acceptable protocol sources for this pin:

- `generated:src/protocol/commands.proto@fea1ebace034ade31c611344793f559800e366c9`
- `generated:src/protocol/candidate_window.proto@fea1ebace034ade31c611344793f559800e366c9`
- `oss-client:src/client/client.cc@fea1ebace034ade31c611344793f559800e366c9`
- `oss-client:src/client/client_interface.h@fea1ebace034ade31c611344793f559800e366c9`

Rejected boundaries remain unchanged:

- Google Japanese Input private named pipe.
- copied `tools/mozc_bridge` byte walking.
- hand-coded protobuf field numbers.
- unproven local `Program Files` probing without artifact provenance.

## Verification

Generate the server-start smoke fixture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1 `
  -ServerStartSmoke `
  -Output tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

Validate it:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

The existing provider comparison harness should also continue to validate
`bridge`, `server`, `imm32`, `dictionary`, and `native` templates.

## Default Gate

`transport=native` remains non-default. A default switch is still blocked until
RTAS itself, not only the external wrapper, proves:

- session lifecycle through the native transport boundary.
- candidate extraction through generated Mozc types for the full corpus.
- authoritative segment metadata or explicit segment unavailability.
- latency and quality against the bridge baseline.
- reviewed license/notices for any distributed artifact.
