# LLM プロンプト設計（Layer2/Layer3）

多段レイヤーで利用するLLMプロンプトを以下の方針で定義する。Layer1（かな→漢字）は選択したローカル変換プロバイダーで処理し、LLMはLayer2（言い換え）とLayer3（翻訳）で使用する。

## 共通設計指針
- 実行先は既定でローカルOllamaサーバー（`127.0.0.1:11434`）。モデル名はチェックイン済み設定の`default`エイリアスを使用する。
- すべてのリクエストは JSON API `/api/generate` へ POST。`stream=false` でバッチ取得。
- キャッシュキーは `layer + model + promptHash + temperature + commitMode`。Space キーはキャッシュ巡回のみ、Shift+Space 時に `bypass_cache=true` を渡して再問い合わせする。
- 返却フォーマットはプレーンテキスト（制御トークンなし）。応答パーサで JSON 化し、`CandidateEntry` 配列へ変換する。

## Layer2: 言い換えテンプレート
- **目的**: Layer1 が確定した日本語文に対し、自然な追記・リライト案を生成。
- **プロンプト骨子**
  ```
  あなたは日本語の編集アシスタントです。
  入力文を自然な敬体で 3 通り提案してください。
  出力は以下の JSON 配列形式で返してください。
  [
    {"variant": "提案文1", "tone": "polite|casual", "delta": "full|suffix"},
    ...
  ]
  ```
- **制御パラメータ**
  | 項目 | 値 | 備考 |
  | ---- | ---- | ---- |
  | temperature | 0.4 | 多様性確保。Shift+Space 時は 0.6 まで上げる。 |
  | stop | `["```", "###"]` | 不要なマークダウン遮断。 |
  | 最大トークン | 256 | 応答が長文化しないよう制限（API options). |
  | 追加 context | `committedText` の末尾 40 文字 | 文脈維持用。 |

### 想定入力例
- 読み: `きょうは`
- Layer1 確定文: `今日は雨ですが出かけます。`
- 期待出力:
  ```json
  [
    {"variant":"今日は雨ですが出かけます。お気をつけてお過ごしください。","tone":"polite","delta":"suffix"},
    {"variant":"今日は雨ですが、気分転換に出かけてきます。","tone":"casual","delta":"full"},
    {"variant":"雨ですが出社します。何かあればご連絡ください。","tone":"polite","delta":"full"}
  ]
  ```

## Layer3: 翻訳テンプレート
- **目的**: 確定済みの日本語文を自然な英語に翻訳（置換モードが既定）。
- **プロンプト骨子**
  ```
  You are a professional English translator.
  Translate the following Japanese sentence into natural English.
  - Keep sentences concise.
  - Do not include commentary or explanations.
  - Output plain text only.
  SOURCE: {{committedText}}
  ```
- **制御パラメータ**
  | 項目 | 値 | 備考 |
  | ---- | ---- | ---- |
  | temperature | 0.2 | 再現性重視。Shift+Space 時は 0.3。 |
  | stop | `["\n\n", "###"]` | 冗長説明を抑止。 |
  | append モード時追記 | 翻訳前後に `APPEND_MODE=ON` を挿入 | 応答パーサで commitMode を切り替え。 |
  | context | Layer2 で選択された候補（英語化対象のみ） | 意味保持のため定義。 |

### 想定入力例
- 入力文: `今日は雨ですが出かけます。お気をつけてお過ごしください。`
- 期待出力: `It's raining today, but I'm heading out. Please take care.`
- append モード時サンプル: `APPEND_MODE=ON It's raining today, but I'm heading out.`

## API オプションマッピング
- `OllamaClient::Generate` に渡す `OllamaRequest` に温度・停止シーケンス・モデルを設定。
- 再問い合わせ（Shift+Space）は `maxRetries` を 0 に設定し即時返答、パラメータ増分を適用。
- すべてのテンプレートは `docs/prompts/normalization_rules.md` に記載の正規化ルールを踏まえて解析する。
