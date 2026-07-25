# 辞書生成ツール

外部辞書をIMEへ統合する前に、同じ手順で取り込むためのツールです。各スクリプトは入力データを読み、再現可能な`.tsv`を出力するだけで、入力データを変更しません。生成物はバージョン管理またはローカルキャッシュに利用できます。

## 対象コーパス

- **日本語形態素**：UniDic-lite（200 MB以下、現代の表記）、または固有名詞・新語が必要な場合はmecab-ipadic-NEologd。ツールは両者のMeCab 13列CSVを想定します。
- **日英対訳**：Electronic Dictionary Research and Development Groupが管理するJMdict／EDICT2。公式の`JMdict_e.xml`に対応します。

> ⚠️ ライセンス：UniDic-liteはUniDicライセンス、mecab-ipadic-NEologdはBSDライセンス、JMdictはCreative Commons Attribution-ShareAlike（CC BY-SA 4.0）です。加工済みデータを同梱する前に、配布方針との互換性を確認してください。

> 公開ポートフォリオ版には、これらのコーパスから変換した行を同梱していません。`data/dictionary/*.tsv`にあるのは、コントリビューターが作成した小さなサンプルだけです。元データの正確なバージョン、ソースハッシュ、生成コマンド、必要なライセンス・帰属表示を確認して含めるまでは、生成コーパスをローカルだけに保存してください。

## ディレクトリ構成

```text
tools/
  dict_prep/
    README.md
    prepare_morph_dict.py
    prepare_bilingual_dict.py
```

## 形態素辞書の作成

1. 元データを取得・展開します。

   ```powershell
   Invoke-WebRequest -Uri https://unidic.ninjal.ac.jp/unidic_archive/cwj/3.1.1/unidic-cwj-3.1.1.zip -OutFile unidic.zip
   Expand-Archive unidic.zip -DestinationPath data\raw\unidic
   ```

2. 変換します。複数のCSVを指定すると統合できます。

   ```powershell
   python tools/dict_prep/prepare_morph_dict.py `
     --input data/raw/unidic/mecab-unidic-3.1.1.csv `
     --output data/local-generated/morph_dict.tsv `
     --min-cost -500 `
     --keep-pos 名詞 動詞 形容詞
   ```

3. スクリプト末尾の件数と除外率を確認します。出力TSVの列：

   - `surface`：表層形
   - `reading`：カタカナ読み
   - `base_form`：基本形
   - `pos`：大分類の品詞
   - `cost`：MeCabの単語コスト（整数）
   - `features`：セミコロン区切りの`key=value`

## 対訳辞書の作成

1. <https://www.edrdg.org/jmdict/j_jmdict.html>から`JMdict_e.xml`を取得し、`data/raw/jmdict/JMdict_e.xml`へ置きます。
2. TSVへ変換します。

   ```powershell
   python tools/dict_prep/prepare_bilingual_dict.py `
     --input data/raw/jmdict/JMdict_e.xml `
     --output data/local-generated/jmdict.tsv `
     --only-common `
     --max-glosses 5
   ```

3. 出力TSVの列：

   - `headword`：主な漢字表記。ない場合は読み
   - `kanji_forms`：`|`区切りの漢字表記
   - `kana_forms`：`|`区切りの読み
   - `english_glosses`：`|`区切りの英語語釈（改行除去済み）
   - `part_of_speech`：`|`区切りのJMdict `pos`タグ
   - `domains`：`|`区切りの`field`タグ（任意）
   - `misc`：`|`区切りの`misc`タグ（任意）
   - `priority`：`|`区切りの優先度（`news1`、`ichi2`など）

## 補足

- 両スクリプトで`--stats`を使うと、回帰確認用の件数を保存できます（例：`python ... --stats data/local-generated/logs/morph_stats.json`）。
- 省メモリ構成では`--min-cost`や`--only-common`を調整します。デスクトップ向けでは全データを保持し、読み込み時に絞り込む方法もあります。
- 元データは無視対象の`data/raw/`、生成物は`data/local-generated/`へ置き、公開サンプルを上書きしたり誤って再公開したりしないでください。
