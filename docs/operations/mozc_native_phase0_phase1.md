# Mozc native Phase 0／Phase 1計画

- 作成日：2026-05-19
- 対象：現在のRTASの動作を保ちながら、将来の`transport=native`実装へ備える

Phase 1には型付きMozc transportの解析・検証層を含みますが、native変換エンジンの完成を意味しません。

> 履歴上の注意：2026-07-25に公開版の既定値を一時的に`transport=imm32`へ変更しましたが、実環境では候補を取得できませんでした。元のポートフォリオ動作を保つため、現在の既定値は`transport=bridge`です。その後、Phase 3で明示設定したアプリ内ラッパー／サーバーを使う経路を実装しました。外部Mozcビルド成果物は意図的に同梱していません。

## 判断

この段階ではnative変換エンジンを既定化しません。Phase 1で整備したのは、型付きtransport契約、評価コーパス、将来の実装が既定化前に通過すべき条件です。

現在のBridge方式にある回避処理をRTAS DLLへ移しただけの「見せかけのnative実装」は行いません。

## 維持する機能

Phase 0／Phase 1では、次の挙動を変えません。

- `transport=bridge`を公開版の既定経路とする。
- `transport=imm32`は明示的な比較・互換試験で利用できる。
- `transport=native`を既定にしない。
- Layer 1／Layer 2／Translationは、それぞれ`IConversionProvider`の`FetchLayer1`／`FetchLayer2`／`FetchTranslation`を通る。
- 候補UI、Spaceでのキャッシュ順送り、Shift+Spaceでの再取得、処理中表示、翻訳確定方式をバックエンドから独立させる。
- TextServiceへnative固有のUI分岐を追加しない。
- Google日本語入力の非公開named pipe処理をRTAS本体へ移さない。

## 型付きtransport

後方互換性のため、JSONの`provider.kana.mozc.transport`は文字列のまま保存します。`src/config/provider_settings.*`がこれを型付き`MozcTransport`へ解析し、未対応の文字列は暗黙に置換せず、診断用に元の値を保った設定エラーとします。

| 設定値 | 型 | 現在の状態 | 補足 |
| --- | --- | --- | --- |
| `bridge` | `MozcTransport::kBridge` | 公開版の既定値 | DLLへ組み込んだBridge実装を呼ぶ。単体の診断CLIもビルドできる。 |
| `server` | `MozcTransport::kBridge` | 旧別名 | 後方互換性のためだけに維持する。 |
| `imm32` | `MozcTransport::kImm32` | 比較・互換経路 | IMM32から直接候補を取得する。提出用PCの実環境では候補を取得できなかった。 |
| `native` | `MozcTransport::kNative` | Phase 3の任意経路 | アプリ内`mozc_server_client`経路。評価条件を満たすまで既定にしない。 |
| 未対応文字列 | `MozcTransport::kInvalid` | 設定エラー | `imm32`、`bridge`、`llm`へ暗黙に切り替えない。 |

現在の`transport=native`は、常に利用不能なstubではありません。明示したアプリ内ラッパー／サーバーが存在する場合だけ初期化します。成果物不足、起動失敗、変換失敗、timeoutを可視のエラーとして返し、別途設計・記録した試験を除いて代替処理を使いません。

### native設定例

```json
{
  "provider": {
    "kana": {
      "mode": "mozc",
      "mozc": {
        "enabled": true,
        "transport": "native",
        "native": {
          "backend": "mozc_server_client",
          "root": "../rtas-artifacts/mozc/source/src/bazel-bin",
          "mozc_build_artifact": "../rtas-artifacts/mozc/fea1ebace034ade31c611344793f559800e366c9/artifact_zip/Mozc64.msi",
          "wrapper_exe": "../rtas-artifacts/mozc/source/src/bazel-bin/rtas_probe/rtas_mozc_client_probe.exe",
          "server_exe": "../rtas-artifacts/mozc/source/src/bazel-bin/server/mozc_server.exe",
          "timeout_ms": 5000,
          "top_n": 8,
          "fallback_policy": "none"
        }
      }
    }
  }
}
```

| フィールド | 値 | 規則 |
| --- | --- | --- |
| `native.backend` | `mozc_server_client`、`linked_converter` | `mozc_server_client`を最初の検証候補とする。 |
| `native.root` | インストールルート相対または絶対パス | `Program Files`の固定パスを使わない。 |
| `native.mozc_build_artifact` | 相対または絶対パス | 評価に使う固定Mozc成果物の由来を示す。 |
| `native.wrapper_exe` | 相対または絶対パス | Phase 3の初期化に必要な薄いOSS Mozcクライアントラッパー。 |
| `native.server_exe` | 相対または絶対パス | Phase 3の初期化に必要なアプリ内`mozc_server.exe`。 |
| `native.timeout_ms` | 正の整数 | ラッパープロセスとMozc IPCのtimeout。 |
| `native.top_n` | 正の整数 | ラッパーへ要求する候補の最大件数。 |
| `native.fallback_policy` | `none`、`imm32`、`bridge` | 既定は`none`。利用した場合は表示・記録する。 |

`native.trace`などの設定は、同じ変更内のコードが実際に利用する場合だけ追加します。traceは既定で無効とし、後のプライバシー設計で許可しない限り入力文を保存しません。

この設定例を公開版の既定値にはしません。nativeが実装・評価条件を満たすまで、任意の技術検証設定としてだけ利用します。

## 評価コーパス

Phase 0の入力：

- `tests/samples/provider_comparison/phase0_cases.tsv`

固定した「正解候補」は保存せず、入力と確認観点だけを記録します。候補品質は、特定の出力を正解として埋め込むのではなく、複数バックエンド間で比較します。

対象：

- 短い単語
- 節
- 助詞を多く含む文
- 固有名詞
- 未知語
- 数字と英字の混在
- 長文
- 句読点と区切り文字

## 比較手順

詳細：

- `tests/manual/provider_comparison_phase0.md`

JSONLテンプレートと検証ツール：

- `tools/provider_compare/New-ProviderComparisonRun.ps1`

通常の比較結果は`tmp_provider_comparison/`などの無視対象ローカルディレクトリへ保存します。人が確認して基準ファイルとして採用する場合だけコミットします。

各入力・バックエンドで次を行います。

1. `bridge`、`imm32`、`dictionary`、または将来の`native`を設定する。
2. RTASテキストサービスを再起動して設定を読み直す。
3. `reading`を入力する。
4. 上位N件のLayer 1候補、文節、エラー、代替処理の有無、コールド／ウォーム遅延、Layer 2とTranslationの接続結果を記録する。
5. 明示的に基準データへ採用する場合を除き、結果をソースツリー外へ保存する。

推奨する結果形式：

```json
{
  "corpus_id": "short_001",
  "backend": "bridge",
  "top_candidates": ["..."],
  "segments": [
    {"index": 0, "start": 0, "length": 2, "surface": "..."}
  ],
  "error": "",
  "fallback_used": false,
  "cold_latency_ms": 0,
  "warm_latency_ms": 0,
  "layer2_ok": true,
  "translation_ok": true,
  "notes": ""
}
```

## native実装の合格条件

`transport=native`を既定にする前に、すべて満たす必要があります。

- 型付きMozc APIまたは生成済みMozcプロトコルを使う。
- Google日本語入力の非公開named pipeを使わない。
- protobufフィールド番号を手書きしない。
- インストールパス、pipe名、KLID、スコア定数を固定しない。
- 不正なtransport値を可視のエラーにする。
- 代替方針が明示され、既定で無効で、利用時に確認できる。
- 既存プロバイダーと同じ`CandidateList`で順序付き候補を返す。
- 正式な文節情報を返すか、取得不能であることを明示する。
- 10文字以下の読みで、候補表示の性能目標を満たす。
- 評価コーパスでBridge方式に回帰がない。
- TextServiceへnative固有の分岐を追加せず、Layer 2とTranslationが動く。
- `bridge`と`imm32`を引き続き利用できる。

次のいずれかが残る場合、既定にしません。

- 一般語でBridgeより候補品質が低い。
- 暗黙に代替処理へ切り替わる。
- 固定したローカルインストールパスが必要。
- 再現できるMozcビルド・依存物の説明がない。
- 初期化・実行時エラーを利用者へ示せない。
- 候補UIまたはTextServiceの特例が必要。
- 文節情報をバックエンドから取得せず推測している。

## 実装順序と現在地

1. この計画と評価コーパスを維持する。
2. 完了：JSON文字列を維持したまま、型付き`MozcTransport`を導入する。
3. 完了：`bridge`、`server`、`imm32`、`native`、不正値の設定検証を追加する。
4. 完了：Phase 3の`mozc_server_client`が使うアプリ内設定を解析する。
5. 完了：`bridge`、`server`、`imm32`、`dictionary`、`native`のJSONLテンプレート／検証を追加する。
6. 一部完了：明示した外部ラッパー／成果物を呼ぶnativeバックエンドを、既存プロバイダー境界の下へ実装した。
7. 未完了：既定値の変更前に、評価コーパス全体を実測する。

## Phase 2以降への引き継ぎ

最初のnative経路には`mozc_server_client`を推奨し、サーバー方式が性能または文節要件を満たさない場合の第2候補として`linked_converter`を残します。

関連文書：

- `docs/dictionary/mozc_native_backend_options.md`
- `docs/dictionary/mozc_server_client_connection_plan.md`
- `docs/dictionary/mozc_native_artifact_protocol_smoke.md`
- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`

技術検証のレビュー前に、Mozcソース／成果物、ライセンス・第三者表示、使用する生成済みプロトコルまたは型付きAPI境界を記録し、実nativeを実行できるよう比較ツールを拡張します。代替処理は既定で無効にし、有効時に確認できるようにします。

現在確認済みの状態：

- 成果物がない場合：理由を示すnative利用不能記録を生成できる。
- 成果物がある場合：固定GitHub Actions MSIを管理展開し、`mozc_server.exe`の起動を確認できる。
- ローカルビルドがある場合：外部`rtas_mozc_client_probe.exe`がMozc公式`client::ServerLauncher`を使い、セッション作成、IME有効化、変換、候補、preedit文節を確認できる。
- RTASのnative transport：設定したアプリ内ラッパー／サーバーを呼び、JSON候補とpreedit文節を解析し、代替処理なしで実行時エラーを表示できる。

偽の候補は返さず、`fallback_used=false`を維持します。クライアント／セッションラッパーのビルドにはVisual StudioのATL／MFCが必要です。次の課題は評価コーパス全体の比較と、ローカルBazelビルド、完全なMSIインストール環境、確認済みの別アプリ内成果物構成のどれを評価用に採用するかの判断です。
