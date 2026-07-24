# User Learning Data

This directory stores per-user adaptation artefacts for the IME dictionary pipeline. Files are intentionally simple JSON/TSV blobs so they can be inspected or reset without special tooling.

## Proposed Layout
- `profiles/` – one subdirectory per Windows user SID, allowing multiple accounts on the same machine.
- `profiles/<SID>/events.log` – newline-delimited JSON events captured via the user learning API.
- `profiles/<SID>/summary.json` – periodically compacted aggregates (n-gram counts, correction stats).
- `tmp/` – scratch space for background compaction jobs; safe to purge.

`profiles/` and `tmp/` are created lazily when the learning provider is initialised in dictionary mode. No files are created while LLM mode remains active.

