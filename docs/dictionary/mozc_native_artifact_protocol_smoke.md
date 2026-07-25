# Mozcネイティブの成果物・プロトコル簡易検証

- 実施日：2026-06-12
- 対象：OSS Mozcの`mozc_server_client`経路に関するPhase 2A／2Bの成果物確認とプロトコル簡易検証

## 結果

RTASリポジトリには、次の処理を再現できるネイティブ検証ツールがあります。

- 固定したMozc成果物マニフェストの読み込み
- GitHub ActionsのMSI成果物の検証
- 管理展開したファイルの検証
- `mozc_server.exe`の短時間起動
- プロバイダー比較形式と互換性のあるJSONL出力

Phase 3では、アプリ内に配置したラッパーとサーバーのパスを明示した場合に限り、RTASの`transport=native`から外部クライアント／セッションラッパーを呼び出せます。この経路は任意で有効化する技術検証であり、既定バックエンドでも、Mozcバイナリを同梱する決定でもありません。

この検証では、Mozc MSIのインストール、IME登録、レジストリ変更、システム全体のIME変更、MozcバイナリのRTASリポジトリへの取り込みは行っていません。

## 成果物の確認

マニフェスト：

- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`

固定したソースと成果物：

- リポジトリ：`https://github.com/google/mozc`
- コミット：`fea1ebace034ade31c611344793f559800e366c9`
- ワークフロー：`CI for Windows`
- 実行ID：`27324141219`
- 成果物ID：`7555700414`
- 成果物名：`Mozc64_x64.msi`
- アーカイブのダイジェスト：`sha256:4e18b4363fc264b2325c8545a08cfea6664e70773afe9b4afb4b0e61fe85cbca`
- 失効日時：`2026-09-09T04:37:19Z`

リポジトリ外のローカルキャッシュ：

- `../rtas-artifacts/mozc/fea1ebace034ade31c611344793f559800e366c9/`

検証済みファイル：

| ファイル | サイズ | SHA-256 |
| --- | ---: | --- |
| `Mozc64_x64.msi.artifact.zip` | 24884594 | `4e18b4363fc264b2325c8545a08cfea6664e70773afe9b4afb4b0e61fe85cbca` |
| `artifact_zip/Mozc64.msi` | 26578944 | `a7d3113ee44fa096b7bc2cbafbcf7cb36ff444b2e62a84f7bdbb877acceb9fa9` |
| `msi_admin_extract/PFiles/Mozc/mozc_server.exe` | 22582272 | `7ed659bb4ba7a6074a946fe7c5729df08e21fce5e15e15a6ef4af3bf60296a00` |
| `msi_admin_extract/PFiles/Mozc/documents/credits_en.html` | 34395 | `dd7c566382204f448040095e5d3c2f2b5199a1b85d44f427ceb94277dc04af50` |

MSIは`msiexec /a`で外部キャッシュへ管理展開しただけで、インストールしていません。

## サーバー起動の簡易検証

検証ツール：

- `tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1`

確認済みテストデータ：

- `tests/samples/provider_comparison/native_artifact_server_start_smoke.jsonl`

このファイルは`tests/samples/provider_comparison/phase0_cases.tsv`だけから生成し、次の情報を記録します。

- `native_backend=mozc_server_client`
- `src/protocol/commands.proto`と`src/protocol/candidate_window.proto`から生成したMozcプロトコルの由来
- 固定したMozcコミットとGitHub Actions成果物の由来
- `fallback_used=false`
- 成果物と`mozc_server.exe`のハッシュ
- `server_start_smoke.status=ok`
- クライアントラッパーがない段階のセッション・候補・文節状態：`not_run_client_wrapper_missing`

この結果が証明するのは、成果物を検出して実行ファイルを起動できることまでです。候補品質、文節情報、セッションのライフサイクルは証明しません。

## クライアント／セッションの簡易検証

外部ソースのチェックアウト先：

- `../rtas-artifacts/mozc/source`
- detached HEAD：`fea1ebace034ade31c611344793f559800e366c9`

依存関係とビルド：

- `python src/build_tools/update_deps.py`は正常終了しました。
- Developer Modeまたは管理者のシンボリックリンク権限がないため、Bazelの通常のシンボリックリンク方式は失敗しました。
- `--nowindows_enable_symlinks --noenable_runfiles`を付けるとコンパイルまで進みました。
- Visual Studio Installerを管理者として実行し、`Microsoft.VisualStudio.Component.VC.ATL`と`Microsoft.VisualStudio.Component.VC.ATLMFC`を追加しました。
- `//rtas_probe:rtas_mozc_client_probe`のビルドに成功しました。出力は`bazel-bin/rtas_probe/rtas_mozc_client_probe.exe`です。
- `//server:mozc_server`のビルドに成功しました。出力は`bazel-bin/server/mozc_server.exe`です。

最初のラッパーは`CreateProcess`を直接使っていましたが、Windows版`mozc_server`が`RunLevel::SERVER`を確認するため、Mozcのサンドボックス付きランチャー経路を通らず失敗しました。現在のラッパーはMozc公式の`client::ServerLauncher`を継承し、`server_program()`だけを上書きしています。

ローカルで確認したセッション結果：

```json
{"ok":true,"connection_ok":true,"session_ok":true,"turn_on_ime_ok":true,"text_input_ok":true,"convert_ok":true,"reading":"きょうはいいてんきです","top_candidates":["今日は","きょうは","教は","強は","凶は","経は","卿は","興は"],"segments":["今日は","いい天気です"],"has_all_candidate_words":true,"has_preedit":true,"fatal_count":0,"error":""}
```

制限事項：

- この検証では、ローカルビルドした`bazel-bin/server/mozc_server.exe`を使っています。
- 管理展開したMSI内の`mozc_server.exe`には、完全なインストール環境なしではラッパーから接続できていません。
- 日本語の読みは、Windowsコマンドラインのコードページ変換を避けるため`--reading_file`で渡します。ASCIIだけの検証では`--reading`も利用できます。
- RTASのネイティブ経路も同じ方針で、現在の読みを一時UTF-8ファイルへ書き、設定済みラッパーを起動し、標準出力のJSONを読み取った直後に一時ファイルを削除します。
- Visual Studioの英語言語パックがないためBazelの自動設定警告が残ります。ヘッダー枝刈りは無効になりますが、ビルドは妨げませんでした。

## プロトコル境界

この固定コミットで利用を認めるプロトコル由来：

- `generated:src/protocol/commands.proto@fea1ebace034ade31c611344793f559800e366c9`
- `generated:src/protocol/candidate_window.proto@fea1ebace034ade31c611344793f559800e366c9`
- `oss-client:src/client/client.cc@fea1ebace034ade31c611344793f559800e366c9`
- `oss-client:src/client/client_interface.h@fea1ebace034ade31c611344793f559800e366c9`

利用しない境界：

- Google日本語入力の非公開named pipe
- `tools/mozc_bridge`のバイト走査処理のコピー
- protobufフィールド番号の手書き
- 成果物の由来を示さない`Program Files`の探索

## 再検証

サーバー起動検証ファイルを生成します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1 `
  -ServerStartSmoke `
  -Output tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

検証します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

既存の比較ツールは、`bridge`、`server`、`imm32`、`dictionary`、`native`の各テンプレートも引き続き検証できる必要があります。

## 既定化の条件

`transport=native`は既定ではありません。既定値を変更するには、外部ラッパーだけでなくRTAS本体について、次を確認する必要があります。

- ネイティブ経路を通したセッションのライフサイクル
- 評価コーパス全体で、生成済みMozc型から候補を取り出せること
- 正式な文節情報、または文節情報を取得できないことの明示
- Bridge方式を基準にした遅延と品質
- 配布する成果物に必要なライセンス・通知文の確認
