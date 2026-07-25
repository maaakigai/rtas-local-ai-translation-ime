# Mozc native検証ツール

`Invoke-MozcNativeProbe.ps1`は、Phase 2A／2Bの`mozc_server_client`成果物確認用に、プロバイダー比較形式のJSONLを作成します。

このツールはシステムへ影響を与えない設計です。

- Mozcをダウンロードしない。
- `update_deps.py`を実行しない。
- Qtをビルドしない。
- `Mozc64.msi`をインストールしない。
- システム全体のIME状態を変更しない。
- Google日本語入力の非公開pipeを使わない。

マニフェストの成果物がない場合、`segment_source=unavailable`、空の候補、`fallback_used=false`、成果物利用不能エラーを持つnative記録を書きます。偽の候補を返さず、比較スキーマを維持できます。

マニフェストが確認済みの外部成果物を指す場合は、MSIパスと展開済み`mozc_server.exe`を検証し、任意でサーバーを短時間起動できます。この検証だけではMozcセッションを作成せず、native候補も返しません。

## 成果物の起動確認を生成

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1 `
  -ServerStartSmoke `
  -Output tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

既存の比較ツールで検証します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

現在のGitHub Actions成果物について確認済みのテストデータ：

- `tests/samples/provider_comparison/native_artifact_server_start_smoke.jsonl`

## 成果物なしの確認を生成

成果物パスが意図的に存在しない場合、`-ServerStartSmoke`を付けずローカルファイルへ出力します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1 `
  -Output tmp_provider_comparison/native_artifact_unavailable.jsonl
```

## 任意のクライアントラッパー

`rtas_mozc_client_probe.cc`と`rtas_mozc_client_probe.BUILD.bazel`は、固定した外部Mozc checkout内で実クライアント／セッションを簡易検証するための最小ソースです。次へコピーします。

- `src/rtas_probe/rtas_mozc_client_probe.cc`
- `src/rtas_probe/BUILD.bazel`

Mozcの`src`ディレクトリからビルドします。

```powershell
bazelisk --nowindows_enable_symlinks build `
  --config oss_windows `
  --config release_build `
  --noenable_runfiles `
  //rtas_probe:rtas_mozc_client_probe
```

ラッパーはMozc公式の`client::ServerLauncher`を使います。Windowsでは、生の`CreateProcess`ではなくMozcのサンドボックス付き起動経路を通します。サンドボックスを使わない直接起動は、MozcのWindows run-level確認に失敗する想定です。

日本語の読みは、Windowsコマンドラインのコードページ変換へ依存しないよう、`--reading`より`--reading_file`を推奨します。

```powershell
Set-Content -LiteralPath ..\native_probe_reading_utf8.txt `
  -Value 'きょうはいいてんきです' `
  -Encoding utf8

.\bazel-bin\rtas_probe\rtas_mozc_client_probe.exe `
  --server_path=.\bazel-bin\server\mozc_server.exe `
  --reading_file=..\native_probe_reading_utf8.txt `
  --top_n=8 `
  --timeout_ms=10000
```

現在の環境にはVisual Studio ATL／MFCがあり、`//rtas_probe:rtas_mozc_client_probe`と`//server:mozc_server`をビルドできます。ローカルビルドした`mozc_server.exe`に対し、セッション作成、IME有効化、UTF-8読み入力、変換、候補取得、preedit文節取得を確認済みです。

管理展開したMSIの`mozc_server.exe`は、完全なインストール環境なしではこのラッパーから接続できません。

コミットするのは意図的にレビューしたテストデータだけにしてください。通常のローカル検証結果は`tmp_provider_comparison/`へ置きます。
