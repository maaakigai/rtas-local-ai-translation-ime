# プロバイダー比較ツール

`New-ProviderComparisonRun.ps1`は、`transport=native`を任意機能として維持したまま、RTASのかな漢字変換バックエンドを比較するJSONLを作成・検証します。

`config/ime_settings.json`は変更しません。入力は`tests/samples/provider_comparison/phase0_cases.tsv`だけに限定します。

## テンプレートを作る

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode template `
  -Backend bridge `
  -Output tmp_provider_comparison/bridge.jsonl
```

対応バックエンド：

- `bridge`
- `server`：実際のtransportが`bridge`になる旧別名
- `imm32`
- `dictionary`
- `native`：Phase 3のアプリ内`mozc_server_client`形式。偽の候補と暗黙の代替処理を使わない

## 結果を検証する

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/bridge.jsonl
```

各行について、次を確認します。

- 既知のcorpus IDを使っている。
- コーパスと同じ読み・確定文を使っている。
- 対応バックエンド名を使っている。
- `fallback_used=true`の場合に`fallback_source`がある。
- `fallback_source`は`bridge`、`imm32`、`dictionary`のいずれかである。

Phase 3のアプリ内サーバー／クライアントを表すnative記録には、次が必要です。

```json
{
  "backend": "native",
  "transport": "native",
  "effective_transport": "native",
  "native_backend": "mozc_server_client",
  "protocol_source": "generated Mozc session proto/client boundary",
  "mozc_commit": "Mozc source SHA or artifact provenance",
  "mozc_build_artifact": "local artifact root, MSI/package id, or CI artifact id",
  "native_runtime": "app_local_mozc_server_client",
  "native_wrapper_exe": "wrapper executable provenance or configured path",
  "native_server_exe": "mozc_server executable provenance or configured path",
  "error": "mozc_server_client backend unavailable",
  "fallback_used": false,
  "fallback_source": "",
  "input_scope": "repo_corpus"
}
```

`protocol_source`には、生成済みMozcプロトコル型またはOSS Mozcクライアント／セッション境界を記録します。Google日本語入力の非公開named pipe、手書きprotobuf番号、非公開プロトコル解析は指定できません。

最初の`mozc_server_client`確認：

- `docs/dictionary/mozc_server_client_connection_plan.md`
- `docs/dictionary/mozc_native_artifact_protocol_smoke.md`
- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`
- `tests/samples/provider_comparison/native_artifact_server_start_smoke.jsonl`

Mozcのインストールやシステム全体のIME変更をせず、互換形式の成果物確認を生成できます。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1 `
  -ServerStartSmoke `
  -Output tmp_provider_comparison/native_artifact_server_start_smoke.jsonl
```

`artifact_probe`、`server_probe`、`server_start_smoke`、`session_lifecycle`、`candidate_extraction`、`segment_extraction`などの入れ子フィールドは、追加の検証情報として受け付けます。中心となるnativeの由来フィールドは引き続き必須です。

通常の結果は`tmp_provider_comparison/`などの無視対象ローカルディレクトリへ保存してください。基準データとして意図的にレビューした結果だけをコミットします。
