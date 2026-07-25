# プロバイダー比較 Phase 0 手動手順

RTASの既定動作を変えず、現在と将来のかな漢字変換バックエンドを比較するための手順です。

アプリ内nativeラッパー／サーバーの境界は実装済みですが、公開リポジトリには外部Mozcビルド成果物を同梱していません。リポジトリ内の「成果物なし」テストデータは、runtime境界が未実装だと主張するものではなく、成果物がない場合の応答を記録します。

## 入力

- コーパス：`tests/samples/provider_comparison/phase0_cases.tsv`
- バックエンド：
  - `bridge`：`provider.kana.mode = "mozc"`、`provider.kana.mozc.transport = "bridge"`
  - `imm32`：`provider.kana.mode = "mozc"`、`provider.kana.mozc.transport = "imm32"`
  - `dictionary`：`provider.kana.mode = "dictionary"`
  - `native`：任意で有効化するアプリ内`mozc_server_client`。明示設定したラッパーとサーバー成果物が必要。

比較のために変更したローカル設定はコミットしないでください。

評価ログへの入力は、上記コーパスの行だけに限定します。個人的な入力、開いているeditorの文章、DebugViewの全出力、native trace全体、プロンプト、保存済み利用者入力を比較結果へ追加しないでください。

## JSONLツール

`tools/provider_compare/New-ProviderComparisonRun.ps1`を使い、コーパス1行につきJSONL 1件のテンプレートを作成し、記入後の結果を検証します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode template `
  -Backend bridge `
  -Output tmp_provider_comparison/bridge.jsonl
```

`-Backend`は`bridge`、`server`、`imm32`、`dictionary`、`native`に対応します。

- `server`はRTASの旧別名で、`effective_transport = "bridge"`になります。
- `native`はアプリ内`mozc_server_client`経路です。ラッパーとサーバーがあれば変換を行い、なければ暗黙に切り替えず、利用不能エラーを記録します。

記入後に検証します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/bridge.jsonl
```

検証ツールは、次の行を拒否します。

- 未知の`corpus_id`
- コーパスと異なる`reading`または`committed_text`
- 未対応のバックエンド名
- `fallback_used=true`なのに`fallback_source`がない

これはプライバシー保護のための境界です。バックエンドの出力は記録できますが、評価入力はリポジトリ内のコーパスから変更できません。

native行ではさらに、`native_backend=mozc_server_client`、空でない`protocol_source`、`mozc_commit`、`mozc_build_artifact`、明示した代替処理フィールドが必要です。

Phase 2A／2Bの成果物確認では、`tools/mozc_native_probe/Invoke-MozcNativeProbe.ps1`が、Mozcを取得・ビルド・インストールせず、マニフェストとコーパスからnative記録を生成できます。成果物がない場合の期待値は、`fallback_used=false`、`segment_source=unavailable`、成果物利用不能エラーです。

## 取得する境界

可能な限りプロバイダー境界で比較します。

- `CandidateList.entries`
- `CandidateList.segments`
- `CandidateList.error`
- `pending`／`requestId`

ツールではなく実際のUIを使う場合も、候補ウィンドウ、デバッグ出力、Bridge CLIから同じ情報を手動で記録します。

## Bridgeの由来に関する注意

単体の`mozc_bridge.exe`は、文節取得元と理由を`DBG`行へ出力できます。DLL内の`MozcBridgeTransport`はこのテキストを解析せず、`QueryCandidatesInProcess()`から候補と文節構造を直接受け取ります。

追加の代替処理・取得元情報が必要な場合は、単体CLIから収集するか、Bridgeの構造化応答を明示的に拡張します。

## バックエンド別の設定

| 記録名 | 必要な設定 | 期待する状態 |
| --- | --- | --- |
| `bridge` | `kana.mode = "mozc"`、`mozc.transport = "bridge"` | 公開版の既定値。DLL内のBridge実装を呼ぶ。 |
| `server` | `kana.mode = "mozc"`、`mozc.transport = "server"` | 旧別名。`bridge`と同じ動作を期待する。 |
| `imm32` | `kana.mode = "mozc"`、`mozc.transport = "imm32"` | 比較・互換経路。提出用PCの実環境では候補を取得できなかった。 |
| `dictionary` | `kana.mode = "dictionary"`、辞書成果物を有効化 | 試作バックエンド。Mozcと同等ではない。 |
| `native` | `kana.mode = "mozc"`、`mozc.transport = "native"`、`native.backend = "mozc_server_client"` | 任意のアプリ内経路。明示成果物がなければ利用不能。 |

実行中RTASの設定を切り替える場合：

1. `config/ime_settings.json`をローカルへバックアップする。
2. テスト対象のバックエンドを設定する。
3. RTASテキストサービスを再起動する。
4. コーパスの各行について、次を記録する。
   - `reading`を入力する。
   - 上位N件のLayer 1候補を記録する。
   - 利用可能なら文節範囲と表層形を記録する。
   - 表示またはログに出たプロバイダーエラーを記録する。
   - 代替処理の有無を記録する。
   - 最初の実行をコールド遅延として記録する。
   - 同じ入力を繰り返し、ウォーム遅延を記録する。
   - `layer_flow`行では、Layer 2とTranslationが選択した文字列を引き継ぐことを確認する。
5. 終了後に元の設定を戻す。

## 推奨する結果形式

```json
{
  "schema_version": 1,
  "corpus_id": "short_001",
  "category": "short_word",
  "reading": "...",
  "committed_text": "",
  "backend": "bridge",
  "kana_mode": "mozc",
  "transport": "bridge",
  "effective_transport": "bridge",
  "native_backend": "",
  "top_n": 8,
  "top_candidates": ["..."],
  "entries": [],
  "segments": [
    {"index": 0, "start": 0, "length": 2, "surface": "..."}
  ],
  "segment_source": "preedit|candidate_list|imm32|dictionary|unavailable",
  "error": "",
  "pending": false,
  "request_id": null,
  "fallback_used": false,
  "fallback_source": "",
  "cold_latency_ms": 0,
  "warm_latency_ms": 0,
  "layer2_impact": {"checked": true, "result": "ok", "notes": ""},
  "translation_impact": {"checked": true, "result": "ok", "notes": ""},
  "source_provenance": "manual_provider_boundary",
  "protocol_source": "",
  "mozc_commit": "",
  "mozc_build_artifact": "",
  "input_scope": "repo_corpus",
  "notes": ""
}
```

上記は構造例で、期待する候補値ではありません。

nativeの利用不能・技術検証記録では次を守ります。

- `native_backend`は`mozc_server_client`。
- `protocol_source`には生成済みMozcプロトコル、または利用した型付きMozc API境界を記録する。
- `mozc_commit`にはMozcソースまたは成果物のrevisionを記録する。
- `mozc_build_artifact`にはローカル成果物ルートまたはパッケージIDを記録する。
- 別途レビューする代替方針の試験を除き、`fallback_used=false`、`fallback_source`は空にする。

意図的にコミットした利用不能テストデータ：

- `tests/samples/provider_comparison/native_artifact_unavailable_smoke.jsonl`

## この段階の合格条件

- `bridge`の動作が変わらない。
- `imm32`を選択できる。
- `dictionary`をMozc同等ではない試作として比較できる。
- RTAS内で非公開プロトコル解析を追加しなくても比較できる。
- 1つの候補文字列を唯一の正解として固定しない。
- native結果は、成果物とruntime情報から明示的な`mozc_server_client`経路を確認できる場合だけ採用する。
- 代替処理を`fallback_used`と`fallback_source`へ明記する。
- バックエンド判断に利用する前に、JSONL検証へ合格する。
