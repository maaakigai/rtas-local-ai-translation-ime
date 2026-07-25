# Mozcネイティブ・バックエンドの選択肢

- 作成日：2026-05-19
- 対象：`transport=native`を実装する前に、OSS Mozcと接続する境界を選ぶためのPhase 2事前調査

この文書はネイティブ・バックエンドの候補を比較するもので、ネイティブ変換エンジンそのものを実装するものではありません。

> 履歴上の注意：この比較は`transport=bridge`が既定だった時点で作成しました。2026-07-25に一時的に`transport=imm32`を選ぶ変更を試しましたが、実環境では候補を取得できませんでした。現在の公開版は、既定値を`transport=bridge`へ戻しています。

## 調査した一次資料

- [google/mozc README](https://github.com/google/mozc)
- [MozcのWindowsビルド文書](https://raw.githubusercontent.com/google/mozc/master/docs/build_mozc_in_windows.md)
- [About Branding](https://code.googlesource.com/mozc/+/HEAD/doc/about_branding.md)
- [Mozcクライアント実装](https://code.googlesource.com/mozc/+/HEAD/src/client/client.cc)
- [Mozcセッション用proto](https://code.googlesource.com/mozc/+/HEAD/src/session/)
- [Mozc変換インターフェース](https://raw.githubusercontent.com/google/mozc/master/src/converter/converter_interface.h)
- [Mozcの文節モデル](https://raw.githubusercontent.com/google/mozc/master/src/converter/segments.h)

確認した制約：

- MozcはGoogle日本語入力を基にしたOSSソースですが、Googleが正式にサポートする製品ではなく、安定版の提供も保証されていません。
- MozcとGoogle日本語入力では、辞書、QA、更新方法、一部機能のデータや動作が異なります。候補品質は現在のBridge方式を基準に実測する必要があります。
- 現在のWindowsビルドは、Bazel／Bazelisk、Python、Visual Studio、.NET、Qtの工程を使います。MSIを生成できますが、依存関係が複雑なため、RTASで扱う場合はバージョンを固定する必要があります。
- Mozcは生成済みのセッション・プロトコル型とクライアント／セッション境界を提供します。RTAS側でprotobufのフィールド番号を手書きしたり、Google日本語入力の非公開pipe解析を本体DLLへ移したりしません。

## 接続方法の比較

| 方法 | 概要 | 長所 | リスク | 初期判断 |
| --- | --- | --- | --- | --- |
| `mozc_server_client` | 生成済みMozcプロトコル型、またはそれを利用する薄いラッパーを介し、OSS Mozcのサーバー／クライアント／セッション境界を使う。 | 外部バックエンドとして`IMozcTransport`に収まり、クラッシュと依存関係をDLLから分離できる。サーバーエラーやタイムアウトを明示できる。 | `mozc_server`とクライアント依存物の配置、セッションとIPCの管理が必要。文節・候補の対応付けを検証する必要があり、MozcのクライアントIPCはRTAS向けの安定APIではない。 | 最初の技術検証として推奨。 |
| `linked_converter` | OSS Mozcの変換エンジンを直接リンクし、`Segments`と候補を`CandidateList`へ変換する。 | 理論上の遅延が最小で、正式な`Segments`へ直接アクセスできる。別プロセスが不要。 | `Ime3.dll`の依存範囲が大きい。BazelとMSBuild、ABI／CRT、第三者依存物、クラッシュ分離、配布時の通知に課題がある。 | サーバー方式で遅延または文節要件を満たせない場合の第2候補。 |
| 非公開pipeの複製 | 現在のGoogle日本語入力用pipe処理をRTASへコピーする。 | 現行動作への近道。 | 非公開プロトコル、固定フィールド番号、固定パス、探索処理へ依存し、Bridge方式の脆弱性を繰り返す。 | 不採用。 |
| 見せかけのnative | Bridge／IMM32／辞書候補を返しながらnativeと表示する。 | なし。 | リスクを隠し、評価結果を無効にする。 | 不採用。 |

## 推奨する経路

Phase 2の最初の実装検証には`mozc_server_client`を使います。

- TextServiceや候補UIへnative固有の分岐を追加せず、既存の`IMozcTransport`配下へ配置できます。
- 手書きのwire解析ではなく、生成済みMozcプロトコル型を利用できます。
- Windowsビルド、配置、実行時ライフサイクルを検証している間、Mozcプロセスと依存関係をRTAS DLLの外へ置けます。
- サーバー不足、起動タイムアウト、プロトコル不一致、候補解析失敗、文節情報不足を、暗黙の代替処理なしで記録できます。

`linked_converter`は、サーバー／クライアント方式が候補表示の性能目標を満たさないか、利用可能な文節情報を返せない場合に限り、後続の検証候補とします。

詳細：

- `docs/dictionary/mozc_server_client_connection_plan.md`
- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`

## 比較結果に必要な由来情報

バックエンドを利用できなかった記録も含め、`mozc_server_client`の比較結果には次を含めます。

- `native_backend`：`mozc_server_client`
- `protocol_source`：生成済みMozcセッション・クライアントプロトコル、または型付けされたOSS Mozcクライアント／セッション境界
- `mozc_commit`：固定したMozcソースのSHA、または明示的な成果物の由来
- `mozc_build_artifact`：ローカル成果物のルート、MSI／パッケージID、CI成果物ID
- `fallback_used`と`fallback_source`：代替処理の有無と取得元

由来として認めないもの：

- Google日本語入力の非公開named pipeの読み取り
- protobufフィールド番号の手書き
- 現行Bridgeからコピーした非公開プロトコルのバイト走査
- 成果物の由来を示さない任意の`Program Files`パス

OSS Mozcには安定版の保証がなく、Windows成果物の出力品質をBridge基準と比較する前に、由来を明示する必要があります。

## Phase 2で変更する範囲

変更対象：

- `src/provider/mozc_transport.*`：既存プロバイダー境界の下にネイティブ実装を追加する。
- `src/config/provider_settings.*`：ネイティブ実装と同じ変更内でのみ、対応設定を読み込む。
- `tools/provider_compare/`：実バックエンドを呼び出す比較処理を追加する。
- 固定Mozc成果物を特定するために必要な配置・ビルド文書またはスクリプト。

ネイティブ固有の変更を入れない範囲：

- TextServiceの候補UI
- Layer 1／Layer 2／Translationの制御
- Shift+Spaceによる再問い合わせ
- 候補キャッシュ方針
- Google日本語入力の非公開named pipe解析

## 既定化の評価条件

比較用コーパスで次を確認するまで、`transport=native`は既定にしません。

- 上位N件の候補品質がBridge方式と同等である。
- 節・文のテストで文節情報が一貫しているか、取得不能であることを明示している。
- コールド／ウォーム時の遅延を計測し、短い読みに対する候補表示目標を満たす。
- 初期化・実行時エラーが結果JSONLへ記録される。
- 代替処理を無効にするか、`fallback_used`と`fallback_source`へ明記する。
- Layer 2とTranslationが、native固有のUI分岐なしに選択文字列を利用できる。
- `bridge`、`server`別名、`imm32`、`dictionary`も引き続き選択できる。

## 未解決のリスク

- 技術検証をレビューする前に、正確なMozcソースとWindows成果物の由来をマニフェストへ固定する必要があります。最初の候補は`mozc_server_client_connection_plan.md`に記録しています。
- 選択したMozc成果物と生成済みプロトコルに必要なライセンス・第三者表示を収集する必要があります。
- サーバー／クライアント方式では、かな漢字変換と文節取得の具体的なセッションコマンド列を確認する必要があります。
- 直接リンク方式では依存関係を整理し、TSF DLLへ安全にリンクできるMozcライブラリがあるか判断する必要があります。
- OSS Mozcの候補品質はGoogle日本語入力と異なる可能性があります。実測結果が揃うまでBridge方式を比較基準として維持します。
