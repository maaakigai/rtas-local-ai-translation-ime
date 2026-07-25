# Mozc Native Backend Options

Date: 2026-05-19
Scope: Phase 2 preflight research for choosing an OSS Mozc native backend
boundary before implementing `transport=native`.

This document compares native backend routes only. It does not implement a
native conversion engine.

> Historical decision record: this comparison was written when
> `transport=bridge` was the default. A temporary 2026-07-25 snapshot change
> selected `transport=imm32`, but live testing returned no candidates.
> The checked-in default now restores `transport=bridge`.

## Source Baseline

Primary references checked for this phase:

- [google/mozc README](https://github.com/google/mozc)
- [Mozc Windows build document](https://raw.githubusercontent.com/google/mozc/master/docs/build_mozc_in_windows.md)
- [About Branding](https://code.googlesource.com/mozc/+/HEAD/doc/about_branding.md)
- [Mozc client implementation](https://code.googlesource.com/mozc/+/HEAD/src/client/client.cc)
- [Mozc session protos](https://code.googlesource.com/mozc/+/HEAD/src/session/)
- [Mozc converter interface](https://raw.githubusercontent.com/google/mozc/master/src/converter/converter_interface.h)
- [Mozc segments model](https://raw.githubusercontent.com/google/mozc/master/src/converter/segments.h)

Observed constraints:

- Mozc is an OSS source release derived from Google Japanese Input, but it is
  not an officially supported Google product and has no stable release
  promise.
- Mozc and Google Japanese Input differ in data and operational behavior,
  including dictionary-related data, QA, update behavior, and some feature
  data. Candidate quality must therefore be measured against the current bridge
  baseline.
- Current Windows builds use Bazel/Bazelisk, Python, Visual Studio, .NET, and
  Qt build steps. Windows artifacts can be built as a package/MSI, but the
  build dependency chain is non-trivial and must be pinned for RTAS.
- Mozc exposes generated session protocol types and client/session boundaries;
  RTAS must not hand-code protobuf field numbers or move Google Japanese Input
  private pipe parsing into the main DLL.

## Candidate Routes

| Route | Summary | Strengths | Risks | Initial decision |
| --- | --- | --- | --- | --- |
| `mozc_server_client` | Use OSS Mozc server/client/session boundary through generated Mozc protocol types or a thin wrapper that uses them. | Matches RTAS `IMozcTransport` as an external backend; isolates crashes and dependency churn; avoids linking converter internals into `Ime3.dll`; can expose server errors/timeouts clearly. | Requires packaging `mozc_server` and client dependencies; session lifecycle and IPC must be owned; segment/candidate mapping must be verified; Mozc client IPC is not an RTAS-stable API. | Recommended first spike route. |
| `linked_converter` | Link an OSS Mozc engine/converter component directly and map `Segments`/candidates into `CandidateList`. | Best theoretical latency; direct access to authoritative `Segments`; no separate server process. | Large dependency surface in `Ime3.dll`; Bazel/MSBuild integration risk; ABI/CRT/third-party dependency risk; harder crash isolation; more license/attribution work in the DLL distribution. | Keep as second route if server/client cannot meet latency or segment requirements. |
| private pipe clone | Copy current Google Japanese Input pipe protocol handling into RTAS. | Short path to current behavior. | Depends on private protocol; requires hardcoded field numbers/path/probing; repeats existing bridge fragility. | Rejected. |
| fake native | Return bridge/IMM32/dictionary candidates while labeling them native. | None. | Masks risk and invalidates evaluation. | Rejected. |

## Recommended Route

Use `mozc_server_client` for the first Phase 2 implementation spike.

Reasoning:

- It fits below the existing `IMozcTransport` boundary without adding
  native-specific branches to TextService or candidate UI.
- It can use generated Mozc protocol types rather than handwritten wire
  parsing.
- It keeps the Mozc process and dependency graph outside the RTAS DLL while the
  team learns the Windows build, packaging, and runtime lifecycle.
- It makes failure modes observable: server missing, startup timeout, protocol
  mismatch, candidate parse failure, and segment unavailability can all be
  recorded without silent fallback.

`linked_converter` should remain a documented fallback route for a later spike
only if the server/client route cannot meet the candidate popup KPI or cannot
return usable segment metadata.

The concrete artifact/protocol/client plan for this route is tracked in:

- `docs/dictionary/mozc_server_client_connection_plan.md`
- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`

## Provider Comparison Provenance

Every `mozc_server_client` comparison record, including backend-unavailable
records, must identify:

- `native_backend`: `mozc_server_client`.
- `protocol_source`: generated Mozc session protocol definitions, generated
  Mozc client protocol definitions, or a typed OSS Mozc client/session boundary.
- `mozc_commit`: pinned Mozc source SHA or explicit artifact provenance.
- `mozc_build_artifact`: local artifact root, MSI/package id, or CI artifact id.
- `fallback_used` and `fallback_source`: explicit fallback state.

Invalid provenance:

- Google Japanese Input private named pipe scraping.
- hand-coded protobuf field numbers.
- private protocol byte walking copied from the current bridge.
- arbitrary local Program Files paths without artifact provenance.

This is required because OSS Mozc has no stable release promise and Windows
build artifacts need explicit provenance before output quality can be compared
to the bridge baseline.

## Phase 2 Spike Boundaries

Touch these boundaries:

- `src/provider/mozc_transport.*`: add the native transport implementation
  below the existing provider boundary.
- `src/config/provider_settings.*`: consume native settings only in the same
  change that implements them.
- `tools/provider_compare/`: extend the comparison harness to execute the real
  native backend once it exists.
- Packaging/build docs or scripts needed to locate a pinned Mozc artifact.

Do not touch these boundaries for native-specific behavior:

- TextService candidate UI.
- Layer1/Layer2/Translation orchestration.
- Shift+Space re-query logic.
- Candidate cache policy.
- Google Japanese Input private named pipe parsing.

## Evaluation Gate

`transport=native` remains non-default until a run against the provider
comparison corpus proves:

- top-N candidate quality is comparable to the bridge baseline.
- segment metadata is present and coherent for clause and sentence rows, or the
  backend reports unavailable metadata explicitly.
- cold and warm latency are measured and meet the candidate popup KPI for short
  readings.
- initialization/runtime errors are visible in the result JSONL.
- fallback is either disabled or explicitly recorded with `fallback_used` and
  `fallback_source`.
- Layer2 and Translation still consume selected source text without
  native-specific UI branches.
- bridge, server alias, imm32, and dictionary behavior remain available.

## Open Risks

- Exact Mozc source revision and Windows artifact provenance must be pinned in
  a manifest before a spike is reviewed. The initial source pin candidate is
  documented in `mozc_server_client_connection_plan.md`.
- License and third-party attribution must be collected for the chosen Mozc
  artifact and any generated protocol sources.
- The server/client route still needs a concrete session command sequence for
  kana-kanji conversion and segment extraction.
- The linked converter route still needs a dependency map and a decision on
  whether any Mozc libraries can be linked safely into the TSF DLL.
- OSS Mozc candidate quality may differ from Google Japanese Input; the bridge
  baseline must remain the comparison target until evidence says otherwise.
