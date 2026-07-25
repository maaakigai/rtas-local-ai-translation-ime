# 状態遷移の補足

## レイヤー遷移

- **Preedit → Layer1Candidates**：`m_reading.append(ConvertRomaji(true))`で現在のローマ字を確定し、`m_reading`が空でない場合に`OpenCandidateUI`を呼びます。
- **Layer1Candidates → Layer2Refine**：`SwitchToLayer2FromLayer1()`が選択中の日本語候補を`m_layer2SourceText/key`へ保存し、`RequestLayer2Alternatives()`を呼びます。候補UIはLayer 2タブへ移り、Layer 1はキャッシュへ残ります。
- **Layer2Refine → Layer1Merged**：`MergeLayer2Selection()`が`m_layer2Cache`の言い換えを`m_reading`へ戻し、compositionを更新して`m_layer1Merged = true`にします。
- **Layer1Merged → TranslationSpin**：`SwitchToTranslationTab()`が統合後の日本語文（`m_candidateSourceText`）を再利用し、`StartTranslationForCandidate`を呼びます。翻訳はLayer 1へ言い換えを統合した後だけ利用できます。
- **TranslationSpin → Commit**：既定では、翻訳文を`CommitComposition`へ渡して日本語文を置換します。利用者が追記方式へ切り替えた場合は、元の日本語文の後へ`InsertText`で翻訳を追加します。

## 非同期処理とキャッシュ

- Layer 2／TranslationでSpaceを押すと、まず入力文をキーとするメモリキャッシュ（`m_layer2Cache`／`m_translationCache`）を確認し、候補があれば再問い合わせせず順に表示します。
- Shift+Spaceはキャッシュを使わず、プロバイダー／LLMへ再問い合わせします（`RequestLayer2Alternatives(true)`／`StartTranslationForCandidate(..., true)`）。
- 処理中は現在のタブを`? [処理中…]`へ置き換えますが、composition previewには最後に確定したLayer 1文字列を残します。`SetLayer2Pending`／`SetTranslationPending`はoverlayのspinnerも切り替えます。
- 非同期要求をキャンセルした場合、処理中フラグを解除し、Layer 1を変更せず以前のタブ内容へ戻します。
- 翻訳結果の正規化と安全性確認は、UIへ渡す前に専用の補助処理で行います。

## UI規則

- overlayには各レイヤーのbadgeを表示します。通常表示では絵文字、ハイコントラストでは`[L1]`、`[L2]`、`[EN]`を使い、処理中はspinnerまたは時計を追加します。
- 候補UIには、日本語、言い換え、翻訳の3つの論理タブがあります。Spaceは現在タブの候補を順に表示し、Shift+Spaceは再問い合わせ、Ctrl+Tab／Ctrl+Shift+Tabはタブ移動です。Layer 2へは日本語タブからだけ入り、Translationは`m_layer1Merged`が`true`になった後に利用できます。

## 例外処理

- Layer 2／Translationが`pending=false`かつ空の候補を返した場合、そのレイヤーを飛ばし、Enter／Spaceを上流のレイヤーへ渡します。
- Escapeは、TranslationSpin → Layer1Merged → Layer1Candidates → Preedit → Idle → IME OFFの順に1段階ずつ戻ります。

## 未完了項目

- キャッシュの有効期間と、Shift+Spaceなどの再問い合わせキーを設定から変更できるようにする。
