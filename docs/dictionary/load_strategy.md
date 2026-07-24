# Dictionary Load Strategy

This note captures how the TSV dictionaries produced by `tools/dict_prep/` will be consumed at runtime. It focuses on keeping memory predictable, ensuring we can ship through `regsvr32` registration without bloating the RTAS DLL, and outlining the testing path before the loaders are wired into production code.

## Memory Budget
- **Target footprint (desktop default)**: ≤ 120 MB resident for UniDic-lite + JMdict common subset combined.
  - Morphological TSV: ~70 MB (after pruning infrequent POS and costs below -500).
  - Bilingual TSV: ~45 MB when capped at 5 glosses and `--only-common`.
  - Index overhead: < 5 MB (vectors of 32-bit indices + unordered_map tables).
- **Low-memory profile (optional)**: ≤ 32 MB by tightening `--max-entries`, raising `--min-cost`, and using frequency-based gloss pruning. The loader options expose `max_entries` so the caller can enforce this at startup.
- **Allocation model**:
  - Loaders reserve capacity before pushing entries to avoid repeated reallocations.
  - Strings remain `std::string` and share the TSV buffer lifetime; consider interning later if profiling shows pressure.
  - Feature/variant vectors are shrunk-to-fit after load in the integration step (not yet implemented).

## Index Strategy
- `MorphDictionary`
  - Primary key: exact surface form → vector of record indices.
  - Secondary: katakana reading → vector of indices (enables IME conversion by reading).
  - Unknown handling: not in loader scope; decoder will synthesize fallback nodes.
- `BilingualDictionary`
  - Headword index for direct lookups.
  - Kanji and kana indices for reverse search (beam rewrite stage).
  - Indices store `std::size_t` offsets; vectors are intentionally unsorted to allow stable insertion.
  - Future extension: maintain a priority score per entry (derived from TSV) to speed up candidate ranking.

## Build Integration Plan
1. **Static library**: add `src/dictionary/*` to a new `Ime3Dictionary.lib` project, keeping it independent of RTAS to allow unit tests without COM host dependencies.
2. **RTAS hook**: wire the loaders into `rtas_text_service` behind an interface exposed via `docs/api/conversion_provider.md`. Activation occurs only when config specifies `provider = "dictionary"`.
3. **Unit tests**: introduce `tests/unit/dictionary_loader_tests.cpp` (GoogleTest) exercising TSV samples located under `tests/samples/dictionary/`.
4. **CI leverage**: extend the existing build scripts to compile the new static library; the RTAS DLL continues to export the same COM objects, so `regsvr32` workflow remains unchanged.

## regsvr32 Deployment Alignment
- RTAS DLL size increase is expected to stay < 1 MB (loader code only). Heavy data assets (`*.tsv`) are not embedded; they ship under `%ProgramFiles%/Ime3/data/`.
- Registration scripts (`install_rtas_x64.bat`) will copy TSV assets before invoking `regsvr32`, ensuring the COM server can locate them if/when dictionary mode is enabled.
- No additional registry keys are required; provider selection will be read from `config/ime_settings.json` at runtime.

## Integration & Test TODOs
- [ ] Add build target for `Ime3Dictionary.lib` and include loaders.
- [ ] Create seeded TSV fixtures (small) and write loader regression tests.
- [ ] Benchmark load times on representative hardware (< 200 ms target) and update this document with results.
- [ ] Validate memory footprint using Windows Performance Analyzer after integrating into RTAS.

