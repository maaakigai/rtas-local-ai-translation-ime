# llama.cppでの翻訳

RTASは別プロセスの `llama-server` にHTTPで接続します。翻訳エンジンを設定画面で `llama.cpp` に切り替えて使用します。
かな漢字変換にはMozcを使います。

## 起動と設定

ローカル環境を準備済みの場合は、リポジトリのルートで次を実行できます。

```powershell
.\tools\start_plamo.ps1
```

このヘルパーは `build/llama_cpp/b12110/llama-server.exe` と
`build/models/plamo-3-translate-0.8b-Q4_K_M.gguf` を使い、バックグラウンドで起動します。
モデル名は `plamo-3-translate`、接続先は `127.0.0.1:8080` です。
準備完了まで待機し、起動済みならモデルのパスを確認して再利用します。
ログは `build/runtime/` に保存されます。Windowsの再起動後は再度実行してください。
2BのGGUFも配置した場合は `-Size 2b` を指定できます。
既に異なるサイズのモデルが動いている場合はエラーを表示します。

GUIを単独起動すると、Windowsに登録されているIMEの設定を優先して開きます。
ローカルPLaMoの設定例は `backend=llama_cpp`、`prompt_format=plamo3`、
`model=plamo-3-translate`、`path=/v1/completions`、`timeout_ms=60000` です。
初回生成を含めて待機できるように、ローカル設定例では60秒を指定しています。

[0.8B公式モデル](https://huggingface.co/pfnet/plamo-3-translate-0.8b-GGUF)

### 手動でモデルを指定する場合

WindowsとGPUに合うllama.cppのバイナリ、およびチャットテンプレートを含む
対応GGUFモデルを用意してください。次のコマンドでモデルのAPI名を `rtas` にします。
モデルのパスは実際のファイルに置き換えてください。

```powershell
.\llama-server.exe -m "C:\models\translation.gguf" --alias rtas --host 127.0.0.1 --port 8080 -c 4096
```

`GET http://127.0.0.1:8080/health` がHTTP 200を返してからRTASを使用します。
同じサーバーを複数アプリのIMEから共有できます。起動中はモデルのメモリを使用します。
サーバーの起動と終了は利用者が行います。

インストール先の `config/ime_settings.json` の
`provider.translation.llm` に次の設定を指定し、IMEを使用するアプリを再起動してください。
ソース側の設定変更を利用するには、ビルドしたDLLと設定をインストール先へ反映します。

```json
{
  "backend": "llama_cpp",
  "model": "rtas",
  "host": "127.0.0.1",
  "port": 8080,
  "path": "/v1/chat/completions",
  "use_tls": false,
  "timeout_ms": 3000
}
```

`backend` 以外を省略した場合もllama.cppのポートは8080、パスは
`/v1/chat/completions` になります。モデルが空または `default` の場合は `rtas` を使います。
初回のロードなどで3秒を超える場合は `timeout_ms` を増やしてください。
タイムアウトは従来と同じWinHTTPの送受信待ち時間です。

Ollamaへ切り替える場合は `backend` を `ollama`、`port` を11434、
`path` を `/api/generate`、`model` をOllama上のモデル名に変更します。
`backend` のない旧設定はOllamaとして読み込みます。
既存の `IME3_OLLAMA_MODEL/HOST/PORT` はOllama選択時にのみ優先されます。

通信は非ストリーミングです。HTTPエラー、不正な応答、空の翻訳は翻訳失敗として扱います。
ESCによるキャンセルは既存のフラグを使い、送信前と応答読み込み時に確認します。
通信中の待機はWinHTTPのタイムアウトまで続く場合があります。

## GUIでのモデル選択

### CPU／GPUの選択

RTASが管理するローカルPLaMoは、設定画面の「推論先」で `CPU` / `GPU` を選べます。
保存すると設定アプリがサーバーを直接起動・再起動し、準備が完了したら通知します。
サーバーが停止している場合も、保存で起動できます。
CPUは `--device none -ngl 0`、GPUは `--device Vulkan0 -ngl 99` を指定します。
GPUを使うにはVulkan対応ビルドと `Vulkan0` が必要です。推論先だけの変更は、翻訳クライアントの再起動を必要としません。
切り替え中に設定画面を閉じた場合は、処理完了後に閉じます。

この機能を有効にするには、設定ファイルの `provider.translation.llm.local_runtime_root` に、
`build/llama_cpp/b12110/llama-server.exe` と `build/models/plamo-3-translate-0.8b-Q4_K_M.gguf`
を配置したフォルダ（`build` の親）を指定し、GUIを開き直します。設定ファイルからの相対パスも使えます。
モデル名は `plamo-3-translate`、ホストは `127.0.0.1` または `localhost` にします。リモートのサーバーとOllamaでは選択欄が無効になります。
`device` の保存値は `cpu` / `gpu`、旧設定の既定値は `gpu` です。
適用に失敗した場合も希望する推論先は保存されるため、原因を解消してから保存で再試行できます。

起動スクリプトから指定する場合は `-Device cpu -Restart` または `-Device gpu -Restart` を使えます。
GUIからの操作はスクリプトに依存しません。

設定画面を開くと、接続先からモデル一覧を取得します。「一覧取得」で再取得できます。
llama.cppでは `GET /v1/models`、Ollamaでは `GET /api/tags` を使用します。
一覧のモデルを選択して保存してください。現在の設定値は一覧取得後も保持され、
別名の手入力も可能です。取得に失敗した場合は接続先とサーバーの起動を確認します。
Ollamaのホスト・ポート環境変数は一覧取得にも適用されます。

llama.cppで選択できるモデルは、そのサーバーが公開しているものです。
単一モデルで起動したサーバーのモデルを変更するには、別モデルでサーバーを起動します。
一覧取得はモデルのダウンロードを行いません。

## PLaMo Translate

「翻訳形式」で `PLaMo 2 Translate` または `PLaMo 3 Translate` を選べます。
`自動` はモデル名の `plamo-2` / `plamo-3` を判定します。
`rtas` などの別名を使用する場合は形式を明示的に選んでください。

PLaMo 3の起動例です。対応する新しいllama.cppで実行してください。
`-hf` は初回にモデルを取得するため、ディスク容量とダウンロード時間が必要です。

```powershell
.\llama-server.exe -hf pfnet/plamo-3-translate-2b-GGUF:Q4_K_M --alias plamo-3-translate --host 127.0.0.1 --port 8080 -c 4096
```

起動後、GUIで「一覧取得」を押して `plamo-3-translate` を選択します。
翻訳形式を `PLaMo 3 Translate` にすると標準のAPIパスは `/v1/completions` になります。
PLaMoの入力は専用形式を使い、日本語から英語に翻訳します。
PLaMo 3には参照用の文脈も送ります。PLaMo 2は原文を専用形式で送ります。
Ollamaでは同じ専用形式を `/api/generate` の `raw` モードで送ります。
保存される `prompt_format` の値は `auto` / `chat` / `plamo2` / `plamo3` です。

PLaMo 2は、利用するllama.cppのPLaMo 2対応状況も確認してください。
PFNのCLI資料には、特定リビジョンのRoPE読み込みを修正するパッチが説明されています。
このGUIはサーバーに接続するため、モデルと推論ランタイムは利用者側で用意します。
[PLaMo 2の入力形式](https://huggingface.co/pfnet/plamo-2-translate)、
[PLaMo 3のモデルと入力形式](https://huggingface.co/pfnet/plamo-3-translate-2b-GGUF)、
[PFNのランタイム資料](https://github.com/pfnet/plamo-translate-cli#llamacpp-runtime-and-plamo-2-ggufs)

## 比較計測

両サーバーを起動し、同じモデルの重み・量子化・コンテキスト長・GPU条件で実行します。
Ollama側のモデル名は実際の名前に置き換えてください。

```powershell
python tools/benchmark_llm.py --ollama-model llama3.1 --llama-model rtas --runs 5 --text "本日の会議は午後3時から始まります。"
```

翻訳文、初回の完了時間、続く5回の完了時間と中央値をJSON行形式で表示します。
初回の値には、その時点のモデルのロード状態が影響します。
これはRTASと同じ翻訳指示を送るHTTP比較ツールです。
IMEの候補描画時間やメモリ使用量は含まれません。
両モデルの常駐でGPUメモリが足りない場合は、それぞれを単独で計測してください。

## 通信回帰テスト

Visual StudioのC++ビルドツールとPython 3が必要です。

```powershell
.\tests\integration\run_llm_backends.ps1
```

モデルを使わず、ループバックのテストサーバーに対して実際の設定読み込みと
`ExecuteTranslationJob` を呼び出します。Ollama互換性、llama.cppの送受信形式、
Unicode、HTTPエラー、不正な応答、キャンセル、既定値を検証します。

実行ファイルがWindowsのアプリケーション制御で拒否される環境では、
そのポリシーに従って実行可能な開発環境でテストしてください。

サーバーの起動オプションとAPI仕様は[llama.cpp公式資料](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)を参照してください。
