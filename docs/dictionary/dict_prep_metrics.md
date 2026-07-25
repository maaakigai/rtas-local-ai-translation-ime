# 辞書生成メトリクス

辞書生成スクリプトは、`--stats data/local-generated/logs/<name>.json`を指定するとJSON形式の統計を出力できます。生成したコーパスとメトリクスは、意図的に公開ポートフォリオ版から除外しています。`data/dictionary/`と`tests/samples/dictionary/`に含まれるのは、コントリビューターが作成した小規模なサンプルだけです。

## メトリクスの定義

形態素TSVの生成時：

- `total_rows`：処理した入力行数
- `kept_rows`：出力した行数
- `keep_ratio`：`kept_rows / total_rows`
- `dropped_by_pos`：品詞条件によって除外した行数
- `dropped_by_cost`：コスト範囲によって除外した行数
- `dropped_by_reading`：必要な読みがないため除外した行数

対訳TSVの生成時：

- `total_entries`：処理したXMLエントリ数
- `kept_entries`：出力した行数
- `keep_ratio`：`kept_entries / total_entries`
- `dropped_no_english`：英語の語釈がないため除外した件数
- `dropped_priority`：一般語フィルターによって除外した件数

`keep_ratio`が±3%を超えて変化した場合は、確認対象とします。生成済みの2つのメトリクスファイルは、次のコマンドで比較できます。

```powershell
python tools/dict_prep/compare_metrics.py <old.json> <new.json> --threshold 0.05
```

生成した辞書を再配布する場合は、元データの正確なバージョン、ソースのハッシュ、生成コマンド、必要なライセンスまたは帰属表示ファイルを必ず保存してください。公開サンプルには、UniDic、JMdict、その他の第三者コーパスの行データを再配布していません。
