# `rtas_text_service`のリファクタリング計画

> この文書には完了済みの項目と今後の計画が混在します。個別の状態は本文に明記します。

## 現在の課題

- `TextService`がIMM32互換処理、LLMキュー、overlay、候補UIとの接続をまとめて担当しており、単体テストしにくい状態です。
- 責務を分離しないままレイヤーを追加すると、`OnKeyDown`がさらに肥大化します。

## 提案する構成

1. **`ConversionProvider`抽象化**
   - `TextService`へ`std::unique_ptr<IConversionProvider> m_provider;`を持たせます。
   - `BuildCandidates`を`Imm32ConversionProvider`へ移します。これは`ConversionProvider`リファクタリングとして完了済みで、将来の辞書バックエンドに向けた第一段階です。
2. **`LayerState`管理**
   - `struct LayerState { enum class Stage { Preedit, Layer1, Layer2, Translation, CommitPending }; ... };`を導入します。
   - キー処理を`LayerState::HandleKey(event)`へ集約し、状態遷移を宣言的にします。
3. **非同期結果の配信**
   - `OnTranslationReady`を、キャッシュと確定方式を理解する`LayerResultDispatcher`へ移します。
   - `AsyncWorkQueue`へのアクセスを小さなインターフェースで包み、テストしやすくします。
4. **UI表示層**
   - `CandidateUI`を`CandidatePresenter`で包み、タブ、badge、ハイコントラスト表示を制御します。
   - `OverlayController`には生の文字列ではなく、レイヤーbadgeと処理中状態を渡します。
5. **確定方針**
   - 翻訳の確定方式（既定は`replace`、任意で`append`）を、プロバイダー結果と設定に含め、UIとTextServiceの動作を一致させます。

## 移行手順

- **Step A**：`IConversionProvider`と、IMM32を使う実装、キャッシュ用の接続点を導入する。
- **Step B**：`LayerState`を作成して`OnKeyDown`の流れを移し、SpaceとShift+Spaceの差をここへ集約する。
- **Step C**：LLM連携をプロバイダーへ移し、TextServiceはコールバックを受けて確定方針を適用する。
- **Step D**：`CandidatePresenter`とoverlay更新を追加し、ハイコントラストのbadge描画へ対応する。

## テスト観点

- 各段階の後に、`tests/samples/multi_stage_inputs.txt`の多層入力シナリオを確認します。
- プロバイダーを切り替えても`KanaModeCommand`の動作を変えません。
- `LayerState`の状態遷移、キャッシュ利用、確定方式切り替えに対する単体テストを追加します。
