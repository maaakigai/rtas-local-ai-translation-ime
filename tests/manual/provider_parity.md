# プロバイダー互換性の旧検証記録

> この文書は、LLMをかな漢字変換の既定プロバイダーとしていた時点の検証記録です。現在の公開版ではLayer 1の既定値がBridge方式で、ユーザー学習は既定で無効です。現在の設定・試験手順には、READMEと`docs/operations/provider_switch.md`を使用してください。

## 当時の確認範囲

- 設定ローダーがLLMを既定値として扱い、辞書への切り替えを受け付けること。
- 辞書モードがサンプルTSVを読み込み、翻訳パイプラインと互換性のある`CandidateEntry`を生成し、試作中の学習ストアへ書き込めること。

## 環境

- ビルド：`Ime3.sln`（`Debug|x64`）
- テスト：`x64/Debug/Ime3Tests.exe`
- サンプル辞書：`data/dictionary/morph_dict.tsv`、`data/dictionary/jmdict.tsv`（`tests/samples/dictionary/`にも同じ内容を配置）

## 当時の手順と結果

| 手順 | プロバイダー | 操作 | 期待値 | 結果 |
| --- | --- | --- | --- | --- |
| 1 | LLM（当時の既定） | リポジトリの`config/ime_settings.json`で`Ime3Tests.exe`を実行 | `ProviderMode::kLLM`、エラーなし | ✅ test assertionで確認 |
| 2 | Dictionary | テストが辞書モードとサンプルTSVを指定した一時設定を作成 | 絶対パスを解決し、`dictionary.enabled=true`、学習用rootを作成 | ✅ 解析成功。当時は`%TEMP%\ime3_dict_tests`に`profiles`を作成 |
| 3 | Dictionary | `MorphDictionaryLoader`／`BilingualDictionaryLoader`でTSVを読む | `neko`が見つかり、各ファイル1行を解析 | ✅ `parsed_rows == 1`、列不足なし |
| 4 | Dictionary | 対訳結果から翻訳候補を構築 | Layerが`Translation`、sourceが`Dict`、表示が`cat`、確定文一致、langが`en` | ✅ LLM候補との互換性をassertionで確認 |
| 5 | Dictionary | 学習イベントを追記してflush | `events.log`に`conversion.accepted`を記録 | ✅ テストがログを読み確認 |

成功時の`Ime3Tests.exe`終了コードは`0`で、失敗時は中断前に診断を出力します。

## 補足

- 辞書TSVは動作確認用の最小データです。
- この旧試験の学習書き込みは、現在の公開版の既定動作ではありません。
- 現在の辞書モード手動確認では、設定を一時変更し、テキストサービスを再起動してください。確認後は公開既定の`mode = "mozc"`／`transport = "bridge"`へ戻します。
