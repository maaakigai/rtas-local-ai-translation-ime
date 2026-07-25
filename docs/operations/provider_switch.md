# プロバイダー切り替え手順

この文書は、現在の`config/ime_settings.json`にあるプロバイダー設定を説明します。

## 現在の既定値

公開版では、かな漢字変換に元のBridge方式、翻訳にLLM方式を使います。

```json
{
  "provider": {
    "kana": {
      "mode": "mozc",
      "mozc": {
        "enabled": true,
        "transport": "bridge"
      }
    },
    "translation": {
      "mode": "llm"
    }
  }
}
```

## かな漢字変換モード

| モード | 状態 | 補足 |
| --- | --- | --- |
| `mozc` | 現在の既定値 | `provider.kana.mozc.transport`を使う。公開版のtransportは`bridge`。 |
| `dictionary` | 利用可能な試作 | TSV辞書を使う。Mozcと同等の品質ではない。 |
| `llm` | 旧設定との互換経路 | 古い設定ファイルとの互換性のために維持する。 |

## Mozc transport

`provider.kana.mozc.transport`は型付きtransport値へ解析します。

| JSON値 | 型 | 状態 |
| --- | --- | --- |
| `bridge` | `MozcTransport::kBridge` | 現在の既定値。元のGoogle日本語入力による変換経路を維持する。 |
| `server` | `MozcTransport::kBridge` | `bridge`の旧別名。 |
| `imm32` | `MozcTransport::kImm32` | IMM32から直接候補を取得する比較・互換経路。実環境で候補を取得できなかったため既定ではない。 |
| `native` | `MozcTransport::kNative` | Phase 3のアプリ内`mozc_server_client`経路。任意でのみ有効化できる。 |

不正なtransport文字列は設定エラーです。`imm32`、`bridge`、`llm`へ暗黙に切り替えません。

Bridge方式はGoogle日本語入力の非公開セッション境界へ依存します。公開版で既定としているのは、ポートフォリオで元の動作を保つためです。安定した公開APIまたは本番環境への推奨方式ではありません。

ここでの`server`は、現行Bridge方式の旧別名です。将来のOSS Mozc `mozc_server_client` native経路を意味しません。

## nativeの状態

`transport = "native"`は型付き値として認識され、Phase 3の最小限のアプリ内`mozc_server_client`経路があります。ただし任意で有効化する試験機能で、既定バックエンドではありません。

`native`を選ぶと、設定済みラッパー／サーバーを呼ぶか、nativeが利用不能である理由または実行時エラーを返します。偽の候補を返したり、Google日本語入力の非公開pipe処理をRTASへコピーしたりしません。

設定例：

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

`wrapper_exe`と`server_exe`を明示し、ローカルのインストール場所やシステム全体のMozc状態を推測しないようにしています。ラッパーは固定したOSS Mozcソースからビルドし、生成済みプロトコル／クライアント型を使います。Windowsのargvコードページ問題を避けるため、読みは一時UTF-8ファイルで渡します。

成果物不足、起動失敗、変換失敗、timeoutは`fallback_used=false`のプロバイダーエラーとして返します。

サーバー／クライアント方式が品質、文節、ライフサイクル要件を満たさない場合の将来候補として、`linked_converter`経路を文書上に残しています。比較条件を満たすまでnativeを既定にしません。

## 辞書モード

試作バックエンドとして利用できます。

```json
{
  "provider": {
    "kana": {
      "mode": "dictionary",
      "dictionary": {
        "enabled": true,
        "morph_tsv": "data/dictionary/morph_dict.tsv",
        "bilingual_tsv": "data/dictionary/jmdict.tsv"
      }
    }
  }
}
```

現在の内製辞書経路はTSVを読み込み、辞書候補を返せます。ただし、Mozc相当の順位付け、品詞遷移スコア、正式な文節情報、成熟したユーザー学習統合はありません。

## 翻訳プロバイダー

翻訳は`provider.translation`で独立して設定します。

```json
{
  "provider": {
    "translation": {
      "mode": "llm",
      "llm": {
        "model": "default",
        "host": "127.0.0.1",
        "port": 11434,
        "path": "/api/generate",
        "use_tls": false,
        "timeout_ms": 3000,
        "keep_alive": -1,
        "warmup_on_activate": true,
        "warmup_timeout_ms": 60000,
        "unload_on_deactivate": true,
        "unload_delay_ms": 10000,
        "log_timings": true
      }
    }
  }
}
```

`provider.translation.mode = "dictionary"`は、`provider.translation.dictionary.enabled = true`の場合だけ有効です。無効な場合は設定エラーを示したうえでLLMへ戻ります。

## Ollamaモデルの常駐

RTASは既定で、LLM翻訳に使うOllamaモデルをメモリへ常駐させます。公開設定は`keep_alive = -1`を送信し、TSFがRTASを有効化した時点でwarmupを開始します。RTASが無効化され、同じプロセスに有効なRTASが残っていない場合、待機後にunload要求を送ります。

既定値はメモリ節約より入力体験を優先しています。メモリを優先する環境では、`warmup_on_activate`、`keep_alive`、`unload_on_deactivate`を変更できます。ただし、次の翻訳時にモデルのコールドロード遅延やtimeoutが再発する可能性があります。

Ollamaの`/api/generate`を次のように使います。

- モデル名を付けた空のgenerate要求：モデルを読み込む。
- `keep_alive = -1`：モデルを常駐させる。
- `keep_alive = 0`を付けた空のgenerate要求：モデルを解放する。

`unload_delay_ms`の既定値は`10000`です。TSFを短時間で再有効化したときの不要な解放・再読込を防ぎます。待機中に再有効化された場合はunloadを取り消します。`log_timings = true`では、経過時間と、Ollamaが返す場合は`load_duration`／`total_duration`を記録します。

## デバッグファイル

RTASの`DebugLog()`は、DebugView向けに`OutputDebugStringW`へ出力します。明示的に有効化した場合、同じ内容をUTF-8ファイルへ複製できます。

```json
{
  "provider": {
    "logging": {
      "debug_file": {
        "enabled": true,
        "path": "logs/rtas-debug.log",
        "max_bytes": 1048576
      }
    }
  }
}
```

`path`には絶対パス、またはRTASのインストール／出力ディレクトリからの相対パスを指定できます。Debug x64のローカルビルドでは、既定の相対パスは`x64/Debug/logs/rtas-debug.log`になります。`max_bytes`を超えると、前のファイルを`rtas-debug.log.1`へローテーションします。

デバッグ出力には稼働中IMEの動作情報が含まれる可能性があるため、ファイル出力は既定で無効です。

## 運用上の注意

- 手動テスト用に変更したプロバイダー設定をコミットしないでください。
- `config/ime_settings.json`を変更した後は、テキストサービスを再起動してください。
- `provider.logging.debug_file`は調査中だけ有効にし、共有後は無効化またはログを保管してください。
- native作業中も`bridge`と`imm32`を選択可能な状態で維持してください。
- `docs/operations/mozc_native_phase0_phase1.md`の合格条件を満たすまでnativeを既定にしないでください。

## プロバイダー比較

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode template `
  -Backend bridge `
  -Output tmp_provider_comparison/bridge.jsonl

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/provider_compare/New-ProviderComparisonRun.ps1 `
  -Mode validate `
  -Result tmp_provider_comparison/bridge.jsonl
```

比較結果のバックエンド名は`bridge`、`server`、`imm32`、`dictionary`、`native`です。`server`はBridgeの旧別名で、`mozc_server_client`ではありません。

native結果には`native_backend`、`protocol_source`、`mozc_commit`、`mozc_build_artifact`、`fallback_used`、`fallback_source`を含めます。Phase 3ではさらに、アプリ内成果物の由来として`native_runtime`、`native_wrapper_exe`、`native_server_exe`を記録します。代替処理を使った場合は`fallback_used`と`fallback_source`の両方を設定します。

最初の`mozc_server_client`検証：

- `docs/dictionary/mozc_server_client_connection_plan.md`
- `docs/dictionary/mozc_native_artifact_protocol_smoke.md`
- `tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json`

Mozcバイナリ、ビルド出力、Qt出力、Mozcの`third_party`依存物は、人による別途のライセンス確認で同梱を認めるまでRTASリポジトリ外へ置きます。
