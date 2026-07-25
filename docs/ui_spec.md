# UI仕様

## Preedit／overlay

- **状態badge**：現在のレイヤー（`L1`、`L2`、`EN`）を表示します。`m_translationPendingShown`が`true`の間は、spinnerまたは時計も表示します。
- **Preedit表示**：Layer 1は実線、Layer 2は点線、翻訳previewは薄い灰色の文字を使います。ハイコントラストでは、太線／中線／細線の白い下線へ切り替え、色の代わりに`[L1]`、`[L2]`、`[EN]`を付けます。
- **Toast**：かな入力切り替えは「あ / A」のままです。`TranslationSpin`へ入ったときは「翻訳候補を取得しています…」を約2秒表示します。

## 候補UI

```text
┌ 日本語候補 (Layer1)
│  1. 今日は
│  2. 今日わ
│  3. きょうは
├ 言い換え (Layer2)
│  1. いいてんきですね
│  2. とても良い天気ですね
│  3. 今日は快晴ですね
└ 翻訳 (Translation)
   1. The weather is nice today
   2. It's a beautiful day today
   3. Lovely weather today
```

- **タブ**：日本語、言い換え、翻訳の3つの論理タブです。Layer 1のSpaceは日本語候補を順に選択します。翻訳対応時は、Layer 2のSpaceで選択中の言い換えを翻訳へ渡します。TranslationのSpaceはキャッシュ済み翻訳候補を順に選択し、Layer 2／TranslationのShift+Spaceは再問い合わせです。Ctrl+Tab／Ctrl+Shift+Tabでも利用可能なタブへ移動できます。
- **処理中表示**：Layer 2／Translationの非同期応答を待つ間、タブ見出しにspinnerを表示し、一覧には`? [処理中…]`だけを出します。日本語previewは維持します。

## キー操作とUI

| キー | レイヤー | 動作 |
| --- | --- | --- |
| Space | Layer 1 | 日本語タブを開き、最初のキャッシュ候補を選択する |
| Enter | Layer 1 | Layer 2タブへ移り、最初の候補または`[処理中…]`を選択する |
| Space | Layer 2 | 選択した言い換えをTranslationへ渡す。翻訳非対応時はキャッシュ済み候補を順に表示する |
| Shift+Space | Layer 2 | 言い換え候補を再取得する |
| Enter | Layer 2 | 選択した言い換えを日本語として確定する |
| Shift+Enter | Layer 2 | 選択した言い換えをpreeditへ統合し、Layer 1へ戻る |
| Enter | Layer1Merged | 翻訳せず、統合後の日本語文を確定する |
| Space | Layer1Merged | Translationタブを開く。まずキャッシュを使い、Shift+Spaceで更新する |
| Space／Shift+Space | Translation | キャッシュ済み翻訳候補を順に表示する／翻訳候補を再取得する |
| Enter | Translation | 日本語文を選択した翻訳文へ**置換する**（既定） |

## エラー表示

- Translation：`! 翻訳に失敗しました (Shift+Spaceで再試行)`
- Layer 2：`! 言い換え候補なし (Shift+EnterでLayer1へ戻る)`

## 配色（ハイコントラスト以外）

- Layer 1：`#2563eb`
- Layer 2：`#d97706`
- Translation：`#10b981`
- 処理中：`#facc15`
- エラー：`#dc2626`

## アクセシビリティ

- ハイコントラストでは色の代わりに`[L1]`、`[L2]`、`[EN]`と下線パターンを使います。
- スクリーンリーダーは、たとえば「日本語レイヤーに戻りました」のようにタブ変更を読み上げます。
- Shift+Spaceのtooltipには「再問い合わせ」と表示します。
