# Dictionary Preparation Toolkit

This toolkit standardises how we ingest external dictionaries before integrating them into the IME runtime. The scripts are intentionally side-effect free: they only read from the source datasets you provide and emit deterministic `.tsv` artefacts that can be versioned or cached.

## Selected Corpora
- **Morphological (JP)**: UniDic-lite (≤ 200 MB, modern orthography) or mecab-ipadic-NEologd when brand names / neologisms are required. Both ship CSV rows in the MeCab 13-column layout, which the tooling expects.
- **Bilingual (JP ↔ EN)**: JMdict/EDICT2 (XML, maintained by the Electronic Dictionary Research & Development Group). The script supports the official `JMdict_e.xml`.

> ⚠️ Licences: UniDic-lite is distributed under the UniDic licence, mecab-ipadic-NEologd under the BSD licence, and JMdict under the Creative Commons Attribution-ShareAlike (CC BY-SA 4.0). Confirm compatibility with your redistribution policy before bundling the processed output.

> The public portfolio snapshot does not bundle rows converted from those
> corpora. `data/dictionary/*.tsv` contains only a small contributor-authored
> sample. Generated corpora must remain local unless their exact upstream
> version, source hash, generation command, and required license/attribution
> files have been reviewed and included.

## Directory Layout
```
tools/
  dict_prep/
    README.md            # this file
    prepare_morph_dict.py
    prepare_bilingual_dict.py
```

## Morphological Dictionary Workflow
1. Download and extract the source dataset:
   ```powershell
   Invoke-WebRequest -Uri https://unidic.ninjal.ac.jp/unidic_archive/cwj/3.1.1/unidic-cwj-3.1.1.zip -OutFile unidic.zip
   Expand-Archive unidic.zip -DestinationPath data\raw\unidic
   ```
2. Run the converter. The script accepts multiple CSV inputs and merges them:
   ```powershell
   python tools/dict_prep/prepare_morph_dict.py `
     --input data/raw/unidic/mecab-unidic-3.1.1.csv `
     --output data/local-generated/morph_dict.tsv `
     --min-cost -500 `
     --keep-pos 名詞 動詞 形容詞
   ```
3. Inspect the footer printed by the script to verify entry counts and drop ratios. The resulting TSV columns are:
   - `surface`: orthographic form.
   - `reading`: katakana reading.
   - `base_form`: lemma.
   - `pos`: coarse part-of-speech tag.
   - `cost`: MeCab word cost (int).
   - `features`: semicolon-delimited metadata (`key=value`).

## Bilingual Dictionary Workflow
1. Acquire `JMdict_e.xml` from <https://www.edrdg.org/jmdict/j_jmdict.html> and place it under `data/raw/jmdict/JMdict_e.xml`.
2. Convert to TSV:
   ```powershell
   python tools/dict_prep/prepare_bilingual_dict.py `
     --input data/raw/jmdict/JMdict_e.xml `
     --output data/local-generated/jmdict.tsv `
     --only-common `
     --max-glosses 5
   ```
3. The TSV schema:
   - `headword`: primary kanji or reading (fallback).
   - `kanji_forms`: pipe-delimited variants.
   - `kana_forms`: pipe-delimited readings.
   - `english_glosses`: pipe-delimited gloss strings (newline stripped).
   - `part_of_speech`: pipe-delimited JMdict `pos` tags.
   - `domains`: pipe-delimited `field` tags (optional).
   - `misc`: pipe-delimited `misc` tags (optional).
   - `priority`: pipe-delimited priority markers (`news1`, `ichi2`, ...).

## Tips
- Use the `--stats` flag on both scripts to capture counts for regression tracking (`python ... --stats data/local-generated/logs/morph_stats.json`).
- Tweak `--min-cost` and `--only-common` thresholds for mobile-memory profiles; for desktop builds, keep the full datasets and prune at load time.
- Preserve the raw sources under the ignored `data/raw/` directory and generated
  outputs under ignored `data/local-generated/` so the portfolio sample is not
  overwritten or accidentally republished.
