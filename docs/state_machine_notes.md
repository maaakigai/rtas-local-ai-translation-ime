# 入力・候補選択フローの実装補足

利用者に見える主要経路は[`state_machine.md`](state_machine.md)にまとめています。
この文書では、現行の`rtas_text_service.h`に対応する内部処理だけを補足します。

## レイヤー遷移

- **入力中／Layer 1 → Layer 2**：`SwitchToLayer2FromLayer1()`が変換元の日本語を保存し、言い換え候補を要求します。
- **Layer 2 → Translation**：翻訳対応時にSpaceを押すと、`SwitchToTranslationTab()`が選択中の言い換えを翻訳元として利用します。
- **Layer 2 → Commit**：Enterは選択中の言い換えを日本語として直接確定します。
- **Layer 2 → 統合後の日本語**：Shift+Enterは`MergeLayer2Selection()`で言い換えをLayer 1へ戻し、編集を継続できる状態にします。
- **統合後の日本語 → Translation／Commit**：Spaceは翻訳へ進み、Enterは日本語として確定します。
- **Translation → Commit**：Enterは選択中の翻訳文を確定します。

## 候補と再問い合わせ

- Layer 1のSpaceは日本語候補を順に選択します。
- Layer 2のShift+Spaceはキャッシュを更新するため、言い換え候補を再問い合わせします。
- 翻訳のSpaceはキャッシュ済み候補を順に選択し、Shift+Spaceは翻訳候補を再問い合わせします。
- プロバイダーが翻訳に対応しない場合、Layer 2のSpaceはキャッシュ済みの言い換え候補を順に選択します。

## 非同期処理

Layer 2と翻訳の要求は非同期キューで処理します。処理中は候補UIとoverlayへ待機状態を表示し、
完了後はrequest IDと現在の入力を確認してから結果を反映します。キャンセル済みまたは
現在の入力と一致しない応答は、表示中の候補を置き換えません。

## キャンセル

- TranslationでEscapeを押すとLayer 1へ戻ります。
- Layer 2でEscapeを押すと要求をキャンセルし、Layer 1へ戻ります。
- Layer 1でEscapeを押すと、現在のcompositionと候補UIを閉じます。
