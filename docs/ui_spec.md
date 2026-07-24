# UI Specification (Section 1)

## Preedit / Overlay

- **Status badges**: show the active layer (`L1`, `L2`, `EN`). When
  `m_translationPendingShown` is true, overlay displays a spinner/clock.
- **Preedit styling**: Layer1 uses a solid underline, Layer2 uses a dotted
  underline, translation preview uses ghosted grey text. High-contrast mode
  switches to thick/medium/thin white underlines and adds the `[L1]`, `[L2]`,
  `[EN]` prefixes instead of colour.
- **Toasts**: Kana toggle toast remains “あ / A”. When entering
  `TranslationSpin`, display “翻訳候補を取得しています…” for ~2 s.

## Candidate UI layout

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

- **Tabs**: three logical tabs. Space cycles inside the active tab using cached
  candidates; Shift+Space re-queries the provider/LLM. Ctrl+Tab /
  Ctrl+Shift+Tab move across tabs (Layer2 is only available from Japanese;
  Translation unlocks after Layer1 is merged).
- **Pending state**: while Layer2/Translation is waiting for an async response,
  the tab title shows a spinner and the list displays a single `? [処理中…]`
  row while the Japanese preview stays intact.

## Key events → UI behaviour

| Key | Layer | Behaviour |
| --- | --- | --- |
| Space | Layer1 | Opens Japanese tab and highlights the first cached candidate |
| Enter | Layer1 | Moves focus to Layer2 tab; the first candidate or `[処理中…]` becomes selected |
| Space | Layer2 | Cycles cached paraphrases; Shift+Space fetches new alternatives |
| Enter | Layer2 | Returns to Layer1 with the selected paraphrase merged into preedit |
| Enter | Layer1Merged | Commits the merged Japanese text without translation |
| Space | Layer1Merged | Activates Translation tab (cached first, Shift+Space for refresh) |
| Enter | Translation | **Replaces** the Japanese sentence with the selected translation (default) |

## Error presentation

- Translation tab: show `! 翻訳に失敗しました (Shift+Spaceで再試行)`.
- Paraphrase tab: show `! 言い換え候補なし (Enterでレイヤー1へ戻る)`.

## Colour palette (non high-contrast)

- Layer1: `#2563eb`
- Layer2: `#d97706`
- Translation: `#10b981`
- Pending: `#facc15`
- Error: `#dc2626`

## Accessibility

- High-contrast mode swaps colour cues for badge labels `[L1]`, `[L2]`, `[EN]`
  and underline patterns.
- Screen readers announce tab changes, e.g., “日本語レイヤーに戻りました”。
- Provide tooltip text for Shift+Space indicating “再問い合わせ”.
