# Mozc Server/Client Connection Plan

Date: 2026-06-12
Scope: Phase 2 follow-up for proving the OSS Mozc `mozc_server_client`
artifact, protocol, and client boundary before RTAS implements real native
conversion. This document does not make `transport=native` the default and does
not add a conversion engine.

## Decision

Use `mozc_server_client` as the first native spike boundary. Phase 3 keeps it
opt-in, but moves past the fail-closed stub: RTAS can now call an explicitly
configured app-local wrapper/server pair and surface candidate/error results
through the existing provider boundary.

The implementation unit is still not "bundle Mozc into RTAS". It is a pinned
external Mozc artifact plus a small RTAS-side native transport that can:

- discover the artifact from explicit config.
- identify its Mozc source commit and build provenance.
- call a typed Mozc client/session boundary through the app-local wrapper.
- parse candidates and segment metadata from generated Mozc types.
- report unavailable/error states without silent fallback.
- feed `tools/provider_compare` records that can be compared against the
  current bridge baseline.

Mozc source checkout, dependency download, and build attempts were performed
only under `../rtas-artifacts`. No MSI install or vendored `third_party` import
is performed as part of this repo change.

## Source Pin

Selected source pin for the first artifact proof:

- repository: `https://github.com/google/mozc`
- branch observed: `master`
- GitHub Actions commit observed on 2026-06-12:
  `fea1ebace034ade31c611344793f559800e366c9`
- full candidate SHA:
  `fea1ebace034ade31c611344793f559800e366c9`

The artifact build must record the full SHA from the local Mozc clone or CI
job with `git rev-parse HEAD`. If that SHA differs from the value above, the
manifest wins and the comparison run must record the manifest SHA.

Use GitHub `master` as the source of truth for the source pin. A Gitiles `HEAD`
page may not reflect the same visible revision as GitHub `master`, so direct
build and artifact provenance must come from the clone or GitHub Actions run
used to create the artifact.

## Artifact Shape

Target Windows artifact:

- kind: external Mozc Windows build artifact.
- primary package output: `bazel-bin/win32/installer/Mozc64.msi`.
- build command: `bazelisk build --config oss_windows --config release_build package`.
- source tree: pinned `google/mozc` checkout.
- artifact location for RTAS evaluation: outside the RTAS source tree, referenced
  by `provider.kana.mozc.native.root` and
  `provider.kana.mozc.native.mozc_build_artifact`.

The first evaluated artifact came from GitHub Actions rather than a local MSI
build:

- workflow: `CI for Windows`
- run id: `27324141219`
- artifact id: `7555700414`
- artifact name: `Mozc64_x64.msi`
- archive digest:
  `sha256:4e18b4363fc264b2325c8545a08cfea6664e70773afe9b4afb4b0e61fe85cbca`
- artifact expiry: `2026-09-09T04:37:19Z`

That is acceptable for evaluation because the manifest records the workflow
run, source SHA, artifact id/name, digest, and retention risk.

Do not commit `Mozc64.msi`, Mozc build output, Qt output, MSYS2, LLVM, Ninja, or
Mozc `third_party` directories to RTAS without a separate human/license review.

## Human Confirmation Boundaries

Return to the user before performing any of these actions:

- running `build_tools/build_qt.py --release --confirm_license`, because the
  license confirmation must be intentional.
- installing `Mozc64.msi` or changing system-wide IME state.
- adding Mozc source, generated protocol output, build artifacts, or any
  `third_party` dependency tree to the RTAS repository.
- deciding that RTAS may redistribute a bundled Mozc artifact.

Small documentation updates, harness templates, and local manifest validation
do not require confirmation.

## Build Environment Proof

The first artifact proof should record:

- Windows version and architecture.
- Visual Studio version and selected MSVC toolset.
- Windows SDK version.
- Python version.
- .NET version.
- Bazelisk/Bazel version.
- Mozc source SHA.
- whether the artifact came from a local build or GitHub Actions.
- exact build command.

The official Windows build requirements currently include 64-bit Windows 10 or
later, Visual Studio 2022 components, Windows 11 SDK, MSVC v143, ATL, Python
3.12 or later, .NET 6 or later, and Bazelisk. Windows ARM64 build-from-ARM64 is
not yet supported by the official document.

Locale risk must be treated as a validation item, not as a fixed workaround.
Because Mozc build steps involve Python, MSYS2, Qt, and Windows tooling, record
at least:

- system locale.
- user UI language.
- active code page from `chcp`.
- whether the Windows "Beta: Use Unicode UTF-8" setting is enabled.
- whether `PYTHONUTF8` or other encoding-related environment variables were
  set.

Do not add RTAS-side hardcoded locale workarounds until a reproducible build
failure has been captured.

Current build status:

- `python src/build_tools/update_deps.py` completed.
- Bazel requires `--nowindows_enable_symlinks --noenable_runfiles` in this
  environment unless Windows Developer Mode or elevated symlink privileges are
  enabled.
- Visual Studio ATL/MFC components were added through an elevated Visual Studio
  Installer run.
- `//rtas_probe:rtas_mozc_client_probe` builds successfully.
- `//server:mozc_server` builds successfully.
- The wrapper proves session creation, IME-on, UTF-8 file reading input,
  conversion, candidate extraction, and preedit segment extraction against the
  locally built `bazel-bin/server/mozc_server.exe`.
- The administratively extracted MSI `mozc_server.exe` remains unreachable
  through the wrapper without a full install context.

The next client/session step should turn the app-local RTAS runtime into a
corpus-driven capture path and then decide whether the evaluated runtime should
use a locally built server artifact, a full installed MSI, or another packaged
server root.

## License And Notices

The artifact proof must collect notices before redistribution is considered:

- Mozc top-level `LICENSE`.
- Mozc `AUTHORS` and `CONTRIBUTORS`, if included in the artifact or
  documentation bundle.
- notices under Mozc `src/third_party`.
- `src/data/dictionary_oss/README.txt`.
- dictionary notices for NAIST/IPAdic and ICOT no-warranty terms.
- Okinawa Dictionary public-domain notice.
- notices for Qt and any other binary dependency shipped with the artifact.

Google-written Mozc code is BSD 3-Clause, but the repository explicitly calls
out third-party code and mixed dictionary data. RTAS must not brand the native
backend as Google Japanese Input and must not imply Google endorsement.

This document is an engineering checklist, not legal advice. A human/legal
review is required before bundling Mozc binaries with RTAS releases.

## Acceptable Protocol Boundary

Acceptable `protocol_source` values for provider comparison records:

- `generated:src/protocol/commands.proto@<mozc_commit>`
- `generated:src/protocol/candidate_window.proto@<mozc_commit>`
- `oss-client:src/client/client.cc@<mozc_commit>`
- `oss-client:src/client/client_interface.h@<mozc_commit>`
- a thin wrapper that uses Mozc generated proto/client types from the pinned
  source tree.

Rejected protocol boundaries:

- Google Japanese Input private named pipe.
- copied byte-walking logic from `tools/mozc_bridge`.
- hand-coded protobuf field numbers.
- raw local `Program Files` probing without artifact provenance.

The official Mozc `commands.proto` describes protocol messages used for Mozc
client/server communication. `candidate_window.proto` defines candidate words,
candidate lists, and candidate window data used by the client/server and
renderer. RTAS should consume generated types from those definitions rather
than reimplementing wire fields.

Concrete generated fields to prove in the spike:

- `commands.Input.CREATE_SESSION` and `commands.Input.DELETE_SESSION` provide
  the session lifecycle commands.
- `commands.Input.SEND_KEY` and `commands.Input.SEND_COMMAND` provide the
  normal key/session command path.
- `commands.KeyEvent.TEXT_INPUT`, `commands.KeyEvent.SPACE`, and
  `commands.KeyEvent.HENKAN` are candidate input/conversion triggers to test
  against the corpus.
- `commands.SessionCommand.TURN_ON_IME` can put the session into a non-direct
  composition mode before conversion.
- `commands.Output.preedit` exposes `Preedit.Segment` records. In conversion
  status, the proto comments state those segments represent conversion
  segments.
- `commands.Output.candidate_window` exposes `CandidateWindow.Candidate.value`
  records for the active candidate window.
- `commands.Output.all_candidate_words` exposes flattened `CandidateList`
  records when populated by the server.

This proves that the OSS protocol has typed places to read candidates and
conversion segments. It does not prove that every RTAS corpus row will populate
all of those fields. The Phase 2 smoke/probe must still measure which fields
are actually returned by the chosen Windows artifact.

## Session Lifecycle

The implementation spike should use this lifecycle:

1. Discover the native root and artifact from typed config.
2. Verify the manifest exists and matches `mozc_commit`.
3. Locate a typed client wrapper or Mozc client binary compiled from that
   source tree.
4. Start or attach to Mozc server through the OSS client boundary.
5. Create a session.
6. Set IME on and a kana composition mode through generated command types.
7. Feed the corpus `reading` using generated key/session command types.
8. Trigger conversion.
9. Parse candidates and segment metadata from generated output structures.
10. Record latency, error state, fallback state, protocol source, commit, and
    artifact provenance in provider comparison JSONL.
11. Delete or clear the session.
12. Terminate the server only if RTAS started it for this run.

The native transport should keep `fallback_policy=none` for the first proof.
If a later explicit fallback test is added, the result must set
`fallback_used=true` and name `fallback_source`.

## Candidate And Segment Proof

Candidate availability is expected to be provable through generated Mozc output
types:

- `commands.Output.candidate_window`, if the active candidate window is
  populated.
- `commands.CandidateWindow.Candidate.value`.
- `commands.CandidateList` or `CandidateWord` fields where the selected client
  boundary exposes them.

Segment availability has a typed protocol path through
`commands.Output.preedit.Segment`, but RTAS must not accept it as available for
the native backend until a smoke run shows those fields are populated for the
conversion command sequence. Candidate rows should record one of:

- `segment_source=preedit`, when generated output contains authoritative
  preedit/conversion segments.
- `segment_source=candidate_list`, when segment spans are explicitly present in
  the chosen generated structures.
- `segment_source=unavailable`, when the server/client boundary cannot provide
  authoritative segment metadata.

Do not infer segment spans from particles, delimiters, cursor position, or
candidate string differences as final native behavior. Heuristic reconstruction
is allowed only as a rejected experiment and must not satisfy the default gate.

## Provider Comparison Integration

Native spike records must include:

```json
{
  "backend": "native",
  "transport": "native",
  "effective_transport": "native",
  "native_backend": "mozc_server_client",
  "protocol_source": "generated:src/protocol/commands.proto@fea1ebace034ade31c611344793f559800e366c9;generated:src/protocol/candidate_window.proto@fea1ebace034ade31c611344793f559800e366c9",
  "mozc_commit": "fea1ebace034ade31c611344793f559800e366c9",
  "mozc_build_artifact": "external:../rtas-artifacts/mozc/fea1ebace034ade31c611344793f559800e366c9/artifact_zip/Mozc64.msi",
  "fallback_used": false,
  "fallback_source": "",
  "input_scope": "repo_corpus"
}
```

The path above is an example manifest path, not a checked-in artifact path.
Actual result files should identify the real local cache path, release asset,
or GitHub Actions artifact id used for the run.

## Minimal Implementation Plan

1. Completed: keep `transport=native` opt-in and fail closed when artifacts are
   missing.
2. Add an artifact manifest file for the selected Mozc commit.
3. Completed: add a native probe that can load the manifest and verify the
   artifact exists without starting conversion.
4. Completed: download the pinned GitHub Actions artifact externally, extract
   the MSI administratively, verify `mozc_server.exe`, and record a
   server-start smoke fixture.
5. Completed for the local build path: build the official client/session
   wrapper and create a real Mozc session through generated protocol types.
6. In progress: call the wrapper from RTAS native transport when explicit
   `native.wrapper_exe` and `native.server_exe` paths are configured.
7. Add corpus-driven candidate capture through the wrapper and record whether
   candidate and segment fields stay populated across the evaluation corpus.
8. Compare against `bridge` baseline before any default change is discussed.

The current probe and reviewed fixtures are documented in:

- `tools/mozc_native_probe/README.md`
- `docs/dictionary/mozc_native_artifact_protocol_smoke.md`
- `tests/samples/provider_comparison/native_artifact_unavailable_smoke.jsonl`
- `tests/samples/provider_comparison/native_artifact_server_start_smoke.jsonl`

## Default Gate

`transport=native` remains non-default until all of these are true:

- Mozc source commit and artifact provenance are pinned in a manifest.
- required notices are collected and reviewed.
- a protocol smoke test creates and deletes a real Mozc session.
- candidate extraction is recorded through generated Mozc types.
- segment metadata is either authoritative or explicitly unavailable.
- provider comparison records validate for `bridge`, `server`, `imm32`,
  `dictionary`, and `native`.
- top-N candidate quality and latency are reviewed against bridge baseline.
- fallback is disabled by default and explicit when tested.

## References

- https://github.com/google/mozc
- https://raw.githubusercontent.com/google/mozc/master/docs/build_mozc_in_windows.md
- https://raw.githubusercontent.com/google/mozc/master/LICENSE
- https://raw.githubusercontent.com/google/mozc/master/src/data/dictionary_oss/README.txt
- https://github.com/google/mozc/blob/master/src/protocol/commands.proto
- https://github.com/google/mozc/blob/master/src/protocol/candidate_window.proto
- https://github.com/google/mozc/blob/master/src/client/client.cc
- https://code.googlesource.com/mozc/+/HEAD/doc/about_branding.md
