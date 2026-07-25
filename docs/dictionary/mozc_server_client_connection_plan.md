# Mozcサーバー／クライアント接続計画

- 更新日：2026-06-12
- 対象：RTASで実際のnative変換を実装する前に、OSS Mozcの`mozc_server_client`成果物、プロトコル、クライアント境界を確認するPhase 2の後続作業

この文書によって`transport=native`を既定にしたり、変換エンジンを公開リポジトリへ同梱したりすることはありません。

## 判断

最初のnative技術検証には`mozc_server_client`を使います。Phase 3の経路は任意で有効化するままですが、単に失敗するstubからは進み、明示設定したアプリ内ラッパー／サーバーの組をRTASから呼び出し、既存のプロバイダー境界を通じて候補またはエラーを返せます。

実装単位は「MozcをRTASへ同梱すること」ではありません。固定した外部Mozc成果物と、RTAS側の小さなnative transportを組み合わせ、次を行います。

- 明示設定から成果物を見つける。
- Mozcソースのコミットとビルドの由来を識別する。
- アプリ内ラッパーを介して、型付きMozcクライアント／セッション境界を呼び出す。
- 生成済みMozc型から候補と文節情報を解析する。
- 暗黙の代替処理を行わず、利用不能・エラー状態を報告する。
- `tools/provider_compare`へ、現在のBridge基準と比較できる結果を渡す。

Mozcソースの取得、依存物のダウンロード、ビルドは`../rtas-artifacts`の下だけで実施しました。このリポジトリ変更では、MSIのインストールや`third_party`の取り込みを行いません。

## ソースの固定

最初の成果物確認に使ったソース：

- リポジトリ：`https://github.com/google/mozc`
- 確認ブランチ：`master`
- 2026-06-12に確認したGitHub Actionsコミット：`fea1ebace034ade31c611344793f559800e366c9`

成果物のビルド時には、ローカルcloneまたはCIジョブで`git rev-parse HEAD`を実行して完全なSHAを記録します。上記と異なる場合は成果物マニフェストを正とし、比較結果にもマニフェストのSHAを記録します。

ソース固定の基準にはGitHubの`master`を使います。Gitilesの`HEAD`表示はGitHub `master`と同じrevisionを表示しない場合があるため、ビルド元のcloneまたはGitHub Actions実行を由来の根拠にします。

## 成果物の形

対象とするWindows成果物：

- 種別：リポジトリ外のMozc Windowsビルド成果物
- 主なパッケージ出力：`bazel-bin/win32/installer/Mozc64.msi`
- ビルドコマンド：`bazelisk build --config oss_windows --config release_build package`
- ソース：固定した`google/mozc` checkout
- RTASから参照する設定：`provider.kana.mozc.native.root`および`provider.kana.mozc.native.mozc_build_artifact`

最初の評価では、ローカルMSIビルドではなく次のGitHub Actions成果物を使用しました。

- ワークフロー：`CI for Windows`
- 実行ID：`27324141219`
- 成果物ID：`7555700414`
- 成果物名：`Mozc64_x64.msi`
- ダイジェスト：`sha256:4e18b4363fc264b2325c8545a08cfea6664e70773afe9b4afb4b0e61fe85cbca`
- 失効日時：`2026-09-09T04:37:19Z`

ワークフロー実行、ソースSHA、成果物ID／名前、ダイジェスト、保持期限をマニフェストへ記録しているため、評価用として利用できます。

`Mozc64.msi`、Mozcビルド出力、Qt出力、MSYS2、LLVM、Ninja、Mozcの`third_party`は、別途人手によるライセンス確認を行わずRTASへコミットしません。

## 人による確認が必要な操作

次の操作を行う前には、利用者へ確認します。

- `build_tools/build_qt.py --release --confirm_license`の実行
- `Mozc64.msi`のインストールまたはシステム全体のIME状態変更
- Mozcソース、生成済みプロトコル、ビルド成果物、`third_party`依存ツリーのRTASリポジトリへの追加
- Mozc成果物をRTASと再配布する判断

小規模な文書更新、比較テンプレート、ローカルマニフェストの検証は、この確認対象に含めません。

## ビルド環境の記録

成果物の確認では次を記録します。

- Windowsのバージョンとアーキテクチャ
- Visual StudioとMSVC toolset
- Windows SDK
- Python
- .NET
- Bazelisk／Bazel
- MozcソースSHA
- ローカルビルドかGitHub Actions成果物か
- 正確なビルドコマンド

公式Windowsビルド文書は、64ビットWindows 10以降、Visual Studio 2022の各コンポーネント、Windows 11 SDK、MSVC v143、ATL、Python 3.12以降、.NET 6以降、Bazeliskを要件としています。公式文書ではWindows ARM64環境上からのARM64ビルドは未対応です。

localeは固定回避策ではなく、検証項目として扱います。少なくとも、システムlocale、表示言語、`chcp`のコードページ、Windowsの「ベータ：ワールドワイド言語サポートでUnicode UTF-8を使用」の状態、`PYTHONUTF8`などの環境変数を記録します。再現可能な失敗を確認するまでは、RTAS側へlocale回避策を固定実装しません。

現在のビルド状況：

- `python src/build_tools/update_deps.py`は完了しました。
- Developer Modeまたは管理者のシンボリックリンク権限がない環境では、Bazelに`--nowindows_enable_symlinks --noenable_runfiles`が必要です。
- Visual Studio InstallerからATL／MFCを追加しました。
- `//rtas_probe:rtas_mozc_client_probe`と`//server:mozc_server`のビルドに成功しました。
- ローカルビルドした`bazel-bin/server/mozc_server.exe`に対し、セッション作成、IME有効化、UTF-8ファイル経由の読み入力、変換、候補取得、preedit文節取得を確認しました。
- 管理展開したMSI内の`mozc_server.exe`は、完全なインストール環境がない状態ではラッパーから接続できません。

次は、評価コーパス全体を通す取得経路を作り、評価用ランタイムにローカルビルドしたサーバー、インストール済みMSI、別のパッケージ化されたサーバールートのどれを使うか判断します。

## ライセンスと通知

再配布を検討する前に、少なくとも次を収集します。

- Mozc最上位の`LICENSE`
- 成果物または文書に含まれるMozcの`AUTHORS`と`CONTRIBUTORS`
- Mozc `src/third_party`配下の通知
- `src/data/dictionary_oss/README.txt`
- NAIST／IPAdicとICOT無保証条項を含む辞書通知
- 沖縄辞書のパブリックドメイン通知
- 成果物と共に配布するQtその他バイナリ依存物の通知

Googleが作成したMozcコードはBSD 3-Clauseですが、リポジトリには第三者コードと複数の条件を持つ辞書データがあります。RTASのnative経路をGoogle日本語入力と表示したり、Googleの推奨を受けているように示したりしません。

この文書は技術チェックリストであり、法的助言ではありません。MozcバイナリをRTASへ同梱する前には、人によるライセンス確認が必要です。

## 利用できるプロトコル境界

比較結果の`protocol_source`に記録できる値：

- `generated:src/protocol/commands.proto@<mozc_commit>`
- `generated:src/protocol/candidate_window.proto@<mozc_commit>`
- `oss-client:src/client/client.cc@<mozc_commit>`
- `oss-client:src/client/client_interface.h@<mozc_commit>`
- 固定したソースツリーのMozc生成済みproto／client型を利用する薄いラッパー

利用しない境界：

- Google日本語入力の非公開named pipe
- `tools/mozc_bridge`からコピーしたバイト走査
- protobufフィールド番号の手書き
- 成果物の由来を示さない`Program Files`の探索

`commands.proto`にはMozcクライアント／サーバー間のメッセージ、`candidate_window.proto`には候補語、候補一覧、候補ウィンドウのデータが定義されています。RTASはwireフィールドを再実装せず、これらから生成した型を利用します。

確認対象となる主な生成済みフィールド：

- `commands.Input.CREATE_SESSION`／`DELETE_SESSION`：セッションの作成・削除
- `commands.Input.SEND_KEY`／`SEND_COMMAND`：通常のキー／セッションコマンド
- `commands.KeyEvent.TEXT_INPUT`／`SPACE`／`HENKAN`：入力・変換の開始
- `commands.SessionCommand.TURN_ON_IME`：変換前に直接入力以外のモードへ切り替える
- `commands.Output.preedit`：変換時の`Preedit.Segment`
- `commands.Output.candidate_window`：現在の候補ウィンドウにある`CandidateWindow.Candidate.value`
- `commands.Output.all_candidate_words`：サーバーが設定した場合の平坦化済み`CandidateList`

型付きの候補・文節格納先があることと、すべてのテスト入力で値が設定されることは別です。選択したWindows成果物を使い、評価コーパス上で実際に返るフィールドを測定します。

## セッションのライフサイクル

技術検証では次の順序を使います。

1. 型付き設定からnativeルートと成果物を解決する。
2. マニフェストの存在と`mozc_commit`の一致を確認する。
3. 同じソースからビルドした型付きクライアントラッパーまたはMozcクライアントバイナリを見つける。
4. OSSクライアント境界からMozcサーバーを起動または接続する。
5. セッションを作成する。
6. 生成済みコマンド型でIMEを有効にし、かな入力モードを設定する。
7. 生成済みキー／セッションコマンドでコーパスの`reading`を入力する。
8. 変換を開始する。
9. 生成済み出力構造から候補と文節情報を解析する。
10. 遅延、エラー、代替処理、プロトコル、コミット、成果物の由来を比較JSONLへ記録する。
11. セッションを削除または初期化する。
12. RTASがこの実行のために起動したサーバーだけを終了する。

最初の確認では`fallback_policy=none`を維持します。後で明示的な代替処理テストを追加する場合は、`fallback_used=true`と`fallback_source`を記録します。

## 候補と文節の確認

候補は、主に次の生成済みMozc出力型から取得します。

- `commands.Output.candidate_window`
- `commands.CandidateWindow.Candidate.value`
- 選択したクライアント境界が公開する`commands.CandidateList`または`CandidateWord`

文節には`commands.Output.preedit.Segment`という型付き経路がありますが、実際の変換コマンド列で値が返ることを簡易検証するまでは、利用可能とみなしません。

結果には次のいずれかを記録します。

- `segment_source=preedit`：正式なpreedit／変換文節が返った。
- `segment_source=candidate_list`：選択した生成済み構造に文節範囲が明記された。
- `segment_source=unavailable`：正式な文節情報を取得できない。

助詞、区切り文字、カーソル位置、候補文字列の差分から文節を推測した結果を、nativeの正式動作として扱いません。

## 比較結果の形式

```json
{
  "backend": "native",
  "transport": "native",
  "effective_transport": "native",
  "native_backend": "mozc_server_client",
  "protocol_source": "generated:src/protocol/commands.proto@fea1ebace034ade31c611344793f559800e366c9;generated:src/protocol/candidate_window.proto@fea1ebace034ade31c611344793f559800e366c9",
  "mozc_commit": "fea1ebace034ade31c611344793f559800e366c9",
  "mozc_build_artifact": "external:../rtas-artifacts/mozc/fea1ebace034ade31c611344793f559800e366c9/artifact_zip/Mozc64.msi",
  "fallback_used": false,
  "fallback_source": "",
  "input_scope": "repo_corpus"
}
```

上記パスはマニフェストの例であり、リポジトリ内の成果物パスではありません。実結果には、使用したローカルキャッシュ、release asset、またはGitHub Actions成果物IDを記録します。

## 最小実装計画

1. 完了：`transport=native`を任意有効のままにし、成果物不足時にはエラーを返す。
2. 選択したMozcコミットの成果物マニフェストを追加する。
3. 完了：マニフェストを読み込み、変換を開始せず成果物の存在を検証するツールを追加する。
4. 完了：固定したGitHub Actions成果物を外部へ取得し、MSIを管理展開して`mozc_server.exe`を確認し、起動結果を記録する。
5. ローカルビルド経路で完了：公式クライアント／セッションラッパーをビルドし、生成済み型を使って実セッションを作る。
6. 実装済み：`native.wrapper_exe`と`native.server_exe`を明示した場合にRTASからラッパーを呼ぶ。
7. 進行中：評価コーパス全体の候補取得を追加し、候補・文節フィールドが継続して返るか記録する。
8. 既定値の議論前にBridge基準と比較する。

関連文書・テストデータ：

- `tools/mozc_native_probe/README.md`
- `docs/dictionary/mozc_native_artifact_protocol_smoke.md`
- `tests/samples/provider_comparison/native_artifact_unavailable_smoke.jsonl`
- `tests/samples/provider_comparison/native_artifact_server_start_smoke.jsonl`

## 既定化の条件

次をすべて満たすまで、`transport=native`は既定にしません。

- Mozcソースと成果物の由来をマニフェストへ固定した。
- 必要な通知を収集・確認した。
- プロトコル簡易検証で実Mozcセッションを作成・削除した。
- 生成済みMozc型から候補を取得・記録した。
- 文節情報が正式な値であるか、取得不能と明示されている。
- `bridge`、`server`、`imm32`、`dictionary`、`native`の比較記録を検証できる。
- 上位N件の候補品質と遅延をBridge基準と比較した。
- 代替処理が既定で無効で、試験時には明示される。

## 参考資料

- https://github.com/google/mozc
- https://raw.githubusercontent.com/google/mozc/master/docs/build_mozc_in_windows.md
- https://raw.githubusercontent.com/google/mozc/master/LICENSE
- https://raw.githubusercontent.com/google/mozc/master/src/data/dictionary_oss/README.txt
- https://github.com/google/mozc/blob/master/src/protocol/commands.proto
- https://github.com/google/mozc/blob/master/src/protocol/candidate_window.proto
- https://github.com/google/mozc/blob/master/src/client/client.cc
- https://code.googlesource.com/mozc/+/HEAD/doc/about_branding.md
