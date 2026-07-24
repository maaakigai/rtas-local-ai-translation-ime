# Provider Parity Check

## Scope
- Confirm the configuration loader keeps LLM as the default provider while accepting dictionary overrides.
- Ensure dictionary mode loads sample TSV assets, produces a `CandidateEntry` compatible with the translation pipeline, and keeps the learning store writable.

## Environment
- Build: `Ime3.sln` (`Debug|x64`).
- Test binary: `x64/Debug/Ime3Tests.exe`.
- Sample dictionaries: `data/dictionary/morph_dict.tsv`, `data/dictionary/jmdict.tsv` (also mirrored under `tests/samples/dictionary/`).

## Procedure & Results
| Step | Provider | Action | Expected | Result |
| --- | --- | --- | --- | --- |
| 1 | LLM (default) | Run `Ime3Tests.exe` with repository `config/ime_settings.json`. | Loader returns `ProviderMode::kLLM`, error string empty. | ✅ Observed via test assertions. |
| 2 | Dictionary | `Ime3Tests.exe` writes a temporary config that enables dictionary mode and points to the sample TSVs. | Loader resolves absolute paths, `dictionary.enabled=true`, learning store root created. | ✅ Config parse succeeded; `profiles` directory created under `%TEMP%\ime3_dict_tests`. |
| 3 | Dictionary | Test loads TSVs through `MorphDictionaryLoader` / `BilingualDictionaryLoader`. | Non-empty lookup for `neko`, stats indicate one parsed row per file. | ✅ `parsed_rows == 1`, zero skipped columns. |
| 4 | Dictionary | Construct translation candidate from bilingual hit. | `CandidateEntry` layer `Translation`, source `Dict`, display text `cat`, commit text matches, lang `en`. | ✅ Assertions in test validated parity with LLM expectations. |
| 5 | Dictionary | Append a learning event and flush. | `events.log` created, contains `conversion.accepted`. | ✅ Verified by test reading the log. |

`Ime3Tests.exe` returns exit code `0` on success; failures print diagnostic messages before aborting.

## Notes
- The dictionary TSVs are minimal smoke-data and ship with the repository to make dictionary mode usable out-of-the-box.
- To exercise the IME manually, copy the same config template to `config/ime_settings.json`, toggle `"mode": "dictionary"`, restart the text service, and type `neko` to surface the dictionary candidate (`cat`). Switch back to `"llm"` to restore the default pipeline; no rebuild required.
