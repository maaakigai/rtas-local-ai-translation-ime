# 入力・候補選択フローの実装補足

操作の基準は [state_machine.md](state_machine.md)。Layer1 の Enter は文書への確定ではなく、
選択したかな漢字変換結果を Layer2 の翻訳用文章として保持する操作。
既定の Mozc 経路では `FetchLayer2()` は選択済み日本語をそのまま返す。

## レイヤー遷移

- 入力中／Layer1 の Enter は `SwitchToLayer2FromLayer1()` で日本語を Layer2 へ渡す。
- Layer2 の Space は翻訳へ進み、Enter は日本語を確定する。
- Layer2 の Shift+Enter は `MergeLayer2Selection()` で Layer1 へ戻して編集を続ける。
- 統合後の日本語は Space で翻訳、Enter で日本語確定へ進む。
- 翻訳の Enter は翻訳文を確定する。
- 翻訳の Space はキャッシュ済み候補を巡回し、Shift+Space は再問い合わせする。

## 非同期結果の受理

- `CandidateRequest` は TSF スレッドが所有する。ワーカーは `PostMainThreadCallback()` で結果を戻す。
- Layer2 と Translation はそれぞれ一つのリクエストを保持する。
- 応答のレイヤー、リクエスト ID、現在のソースキーが一致し、`pending=false` の結果だけ適用する。
- 不明・キャンセル済み・重複の応答は候補、キャッシュ、待機表示を変更しない。
- `pending=true` の通知ではリクエストを消費しない。
- キャンセルは先に受理用の ID を消し、その後ワーカーへ通知する。
- キャッシュ済み候補への切り替えも、以前のリクエストをキャンセルしてから行う。
- Escape、候補 UI の終了、プロバイダーの再初期化、IME の Deactivate で対象のリクエストを無効化する。
- キューの ID 採番は同一モジュール内で共有し、プロバイダー再生成や別キューとの衝突を防ぐ。

## 残る境界

候補キャッシュと TSF の編集・表示は TextService に残る。今回の抽出対象は Enter の判断と
非同期結果の受理管理であり、全キー処理の独立化ではない。

通信は引き続き同期 WinHTTP をワーカーで実行し、`Shutdown()` はワーカーを join する。
応答の無効化は通信そのものの即時中断を意味しない。IME 切り替え・終了時の待ち時間は実機計測が必要。
