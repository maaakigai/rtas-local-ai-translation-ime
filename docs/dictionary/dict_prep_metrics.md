# Dictionary preparation metrics

The dictionary preparation scripts can emit JSON statistics with
`--stats data/local-generated/logs/<name>.json`. Generated corpora and metrics are intentionally excluded from
the public portfolio snapshot; only small contributor-authored samples are
included under `data/dictionary/` and `tests/samples/dictionary/`.

## Metric contract

For morphological TSV generation:

- `total_rows`: source rows processed
- `kept_rows`: rows written
- `keep_ratio`: `kept_rows / total_rows`
- `dropped_by_pos`: rows excluded by part of speech
- `dropped_by_cost`: rows excluded by cost bounds
- `dropped_by_reading`: rows missing a required reading

For bilingual TSV generation:

- `total_entries`: XML entries processed
- `kept_entries`: rows written
- `keep_ratio`: `kept_entries / total_entries`
- `dropped_no_english`: entries without an English gloss
- `dropped_priority`: entries excluded by the common-entry filter

Flag a `keep_ratio` change greater than ±3% for review. Compare two generated
metric files with:

```powershell
python tools/dict_prep/compare_metrics.py <old.json> <new.json> --threshold 0.05
```

Before redistributing any generated dictionary, preserve its exact upstream
version, source hash, generation command, and required license or attribution
files. The public sample does not redistribute UniDic, JMdict, or other
third-party corpus rows.
