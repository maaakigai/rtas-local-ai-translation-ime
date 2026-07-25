# `CandidateEntry`スキーマ

```json
{
  "id": "string",                       // レイヤー内で一意のID（例："layer1:0"）
  "layer": "layer1|layer2|translation", // この候補を所有するレイヤー
  "displayText": "string",              // UIに表示する文字列
  "commitText": "string",               // 確定時に適用する文字列
  "reading": "string",                  // かな読み（Layer 1は読みを保持し、Layer 2／翻訳は変換元を引き継ぐ）
  "source": "imm32|dict|llm|cache",     // 実行時エントリで使う生成元の列挙値
  "confidence": 0.0,                    // 任意の0.0～1.0スコア
  "metadata": {
    "altVariants": ["string"],          // 任意の代替候補ID
    "llmRequestId": 42,                 // 非同期処理の追跡ID
    "partial": false,                   // 語尾など一部分だけを対象とする場合はtrue
    "lang": "ja|en",                    // 必要に応じて付ける言語タグ
    "commitMode": "replace|append",     // 候補側の適用指定。現行IME確定経路はreplace
    "timestamp": "ISO8601"              // 任意の作成日時
  }
}
```

## レイヤー別の補足

- **layer1**：かな漢字変換バックエンドが生成します。通常、`commitText`は漢字を含む確定文字列で、`displayText`には番号などの表示用情報を含められます。
- **layer2**：Layer 1へ戻って統合する言い換え・拡張候補です。末尾の語句だけを表す場合は`metadata.partial=true`を設定します。
- **translation**：最終的な翻訳文を表します。`metadata.commitMode`は候補スキーマ上で`replace`／`append`を表現できますが、現行IMEの確定経路は`replace`だけを使用します。

## プロバイダー比較時の注意

実行時の`CandidateSource`列挙型は、現在のところMozc Bridge、サーバー別名、将来のネイティブ実装を区別しません。MozcによるLayer 1候補にも、既存の実行時ソース値を使用します。そのため、プロバイダー比較結果では、`backend`、`transport`、`effective_transport`、`native_backend`などのフィールドを使って、バックエンドの由来を別途記録します。

## 例

```json
[
  {
    "id": "layer1:0",
    "layer": "layer1",
    "displayText": "今日は",
    "commitText": "今日は",
    "reading": "きょうは",
    "source": "imm32",
    "confidence": 0.82
  },
  {
    "id": "layer2:0",
    "layer": "layer2",
    "displayText": "いいてんきですね",
    "commitText": "今日はいい天気ですね",
    "reading": "きょうは",
    "source": "llm",
    "metadata": {
      "llmRequestId": 42,
      "partial": false
    }
  },
  {
    "id": "translation:0",
    "layer": "translation",
    "displayText": "The weather is nice today",
    "commitText": "The weather is nice today",
    "reading": "きょうは",
    "source": "llm",
    "metadata": {
      "llmRequestId": 43,
      "lang": "en",
      "commitMode": "replace"
    }
  }
]
```
