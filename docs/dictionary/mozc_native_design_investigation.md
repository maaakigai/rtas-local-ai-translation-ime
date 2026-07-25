# Mozcネイティブ方式の設計調査

- 作成日：2026-05-19
- 対象：Mozcネイティブ方式の設計方針と移行状況

この文書は、アーキテクチャの方向性と型付きtransport設定、任意で有効化するネイティブ経路の検証状況を記録します。完成したネイティブ変換エンジンの説明ではありません。

> 履歴上の注意：2026-07-25に、公開版の既定値を一時的に`transport=imm32`へ変更しましたが、実環境で候補を取得できませんでした。現在の既定値は`transport=bridge`へ戻しています。その後、Phase 3として、明示的に設定したアプリ内ラッパー／サーバーを呼び出す`native`経路を実装しました。ただし、必要な外部Mozc成果物は公開リポジトリに同梱していません。

## 結論

現時点で、Bridge方式を内製辞書へ直接置き換えるべきではありません。`bridge`と`imm32`を選択可能な状態で維持し、将来の`transport=native`を独立したMozcバックエンドとして任意で有効化する方針です。

このプロジェクトで「native」と呼べるのは、次のいずれかです。

1. OSS Mozcの変換部品をRTASへリンクまたは埋め込み、型付きデータ構造を直接利用する。
2. 手書きの数値フィールド解析ではなく、生成済みMozcプロトコル定義を使ってOSS Mozcのサーバー／クライアント境界と通信する。
3. 内製辞書デコーダーは別系統として改善し、同等のデコーダー、文節モデル、ランキングモデルへ到達するまでは「Mozc native」と呼ばない。

現在の`mozc_bridge.exe`にあるGoogle日本語入力の非公開pipe解析や固定プロトコルをRTAS DLLへ移すことは、native化に含めません。

## 必須条件

- インストールパス、pipe名、KLID、タイムアウト、プロトコルフィールド番号をnative設計へ固定値として埋め込みません。
- Google日本語入力の非公開named pipeへ依存しません。
- `native`専用の候補UIやTextService分岐を追加せず、既存の`IConversionProvider`と`IMozcTransport`で吸収します。
- 設定で許可し、利用した事実を報告しない限り、nativeからIMM32へ暗黙に切り替えません。
- 正式な動作として、規則による文節の推測を使いません。変換器が返す文節範囲と表層形を利用します。
- 本来のデコーダー／ランカーの代わりに、候補の並べ替えやUIラベル除去で症状だけを隠しません。
- 代替処理、探索パス、スコア設定は、明示・検証・テスト・文書化できる形にします。

## 現在の構成

### プロバイダー境界

安定した入口は`src/api/conversion_provider.h`の`IConversionProvider`です。プロバイダーは`LayerRequestContext`を受け取り、候補、任意の文節情報、任意の非同期要求ID、処理中状態、レイヤーID、エラー文を含む`CandidateList`を返します。

`Ime3/rtas_text_service.h`は、`CreateConversionProvider`でプロバイダーを生成し、対応機能を確認してLayer 1／Layer 2／Translationの要求を送り、返された文字列と文節情報を利用します。UIをバックエンドから分離するこの境界に、native固有処理を閉じ込めます。

主要な接続点：

- `src/api/conversion_provider.h`
- `src/provider/conversion_provider_factory.cpp`
- `Ime3/rtas_text_service.h`
- `docs/api/candidate_schema.md`
- `docs/state_machine.md`
- `docs/ui_spec.md`

### 設定と生成処理

現在の公開設定：

- `provider.kana.mode = "mozc"`
- `provider.kana.mozc.transport = "bridge"`

`src/config/provider_settings.*`はJSON上の文字列を、次の型付き値へ変換します。

- `bridge` → `MozcTransport::kBridge`
- `server` → 互換性のため`MozcTransport::kBridge`として扱う旧別名
- `imm32` → `MozcTransport::kImm32`
- `native` → 明示設定したラッパーとMozcサーバーが存在する場合だけ初期化する、アプリ内配置のサーバー／クライアント境界
- 未対応の文字列 → 設定エラー付きの`MozcTransport::kInvalid`

`src/provider/conversion_provider_factory.cpp`は、概ね次の順で選択します。

1. `kana.mode`が`dictionary`で初期化に成功した場合、辞書プロバイダー
2. `kana.mode`が`mozc`で初期化に成功した場合、Mozcプロバイダー
3. IMM32プロバイダー

ただし`transport=native`では、設定した成果物が不足または不正な場合、IMM32へ暗黙に切り替えず、理由を示す利用不能プロバイダーを返します。不正なtransport名も可視の設定エラーとして扱います。

### Mozcプロバイダー

`src/provider/mozc_conversion_provider.cpp`はLayer 1を`IMozcTransport`へ委譲し、`MozcCandidateResponse.segments`を`CandidateList.segments`へコピーして、文字列候補をLayer 1エントリへ変換します。

`src/provider/mozc_transport.cpp`が提供する主な経路：

- `MozcBridgeTransport`
  - RTAS DLLへ組み込んだBridge実装を呼び出します。
  - 候補と、利用可能な場合は文節情報を直接返します。
  - 診断用CLIと実装を共有しますが、通常変換時にCLIプロセスを起動しません。
- `MozcImm32Transport`
  - `ImmGetConversionListW(..., GCL_CONVERSION)`を呼びます。
  - 候補だけを返し、文節情報は返しません。
- native用のアプリ内ラッパー／サーバーtransport
  - 設定した外部成果物が検証できた場合だけ初期化します。
  - 読みを一時UTF-8ファイルで渡し、ラッパーのJSON出力から候補と文節を読み取ります。

`MozcConversionProvider`内では、Bridge失敗時にIMM32へ自動で切り替えません。

### Bridge実装の技術的負債

`tools/mozc_bridge/main.cpp`の元実装は、単純なIMM32ブリッジではありませんでした。

1. `\\.\pipe\googlejapaneseinput.*.session`に一致するGoogle日本語入力のセッションpipeを列挙する。
2. 見つからない場合、固定した`Program Files`パスから`GoogleIMEJaConverter.exe`を起動する。
3. 固定したMozc／Googleプロトコル番号を使い、バイナリメッセージを組み立てる。
4. protobuf風のwireフィールドを走査して候補と文節を解析する。
5. pipeから候補を取得できない場合にIMM32へ切り替える。

さらに、Google TIP、Microsoft IME TIP、旧日本語配列の探索、区切り文字・助詞による文節推測、カーソル位置とキー長による文節復元、文節末尾を付けた候補再構成などの規則を含みます。試作としては有用でしたが、native方式ではこれらの固定依存と推測処理を持ち込みません。

## 内製辞書経路

ビルドとfactoryへ接続済みの実装：

- `src/provider/dictionary_conversion_provider.*`
- `src/dictionary/morph_loader.*`
- `src/dictionary/bilingual_loader.*`
- `data/dictionary/morph_dict.tsv`
- `data/dictionary/jmdict.tsv`

現在動く範囲：

- 形態素TSVを読み込み、表層形と読みで索引化する。
- 対訳TSVを読み込み、見出し語、漢字表記、かな表記で索引化する。
- Layer 1で、生の読み、簡易分割した表層形、少数の代替候補を返す。
- 辞書翻訳モードで最初に一致した英語語釈を返す。一致しない場合は入力を維持してエラーを返す。
- LLM翻訳モードでは翻訳ジョブキューへ委譲する。

未完成の範囲：

- 文節分割は単純化した動的計画法で、`docs/dictionary/analysis_design.md`の完全な設計ではない。
- 品詞遷移行列と特徴量由来のスコアモデルがない。
- Layer 2はLayer 1のタグを付け替えた状態で、ビームサーチによる書き換えではない。
- ユーザー学習設定は解析するが、辞書プロバイダーの順位付けへ利用していない。
- `CandidateList.segments`を出力しない。
- 一部のテストと文書が、現在のコンストラクターや実行時動作に追随していない可能性がある。

未知語ペナルティ、最大span長、かな維持ペナルティ、長さ事前分布、信頼度、代替候補の上限には固定値があります。内製辞書を本格的な置換候補にする前に、生成済みモデル、検証済み設定、または実測したデコーダーパラメーターへ移す必要があります。

## 選択肢の比較

| 選択肢 | 長所 | 短所 | 判断 |
| --- | --- | --- | --- |
| `bridge`を維持 | 現時点で最も確実に候補を返し、一部の文節情報も取得できる。 | 非公開Google pipe、固定パス／プロトコル／KLID、推測と代替処理を含む。 | 公開版の既定値として現行動作を維持しつつ、移行比較の基準とする。 |
| `imm32`を維持 | 単純でローカルな互換経路。 | 有効なIMEへ依存し、文節情報がなく、実環境では候補を返さない構成があった。 | 明示的な比較・互換モードとして残す。 |
| `native`を追加 | 非公開Google pipeを避け、正式な文節情報を取得できる可能性がある。配置とテストを管理しやすい。 | Mozcのビルド、API／プロトコル、依存物の配布、品質評価が必要。 | 推奨する設計経路。既定にはしない。 |
| 内製辞書へ今すぐ切り替え | 完全ローカルで決定的。リポジトリ内に実装がある。 | ランキング、言語モデル、Layer 2、文節情報が試作段階。 | 現時点ではMozcの置換に使わない。 |
| 内製辞書を長期的に育てる | 外部IMEへ依存せず、全体を制御できる。 | モデル生成、スコア、学習、品質評価を含む大規模なデコーダー開発になる。 | native基準を定めた後、独立系統として進める。 |

## 推奨する移行手順

### Phase 0：基準と証拠

- 代替経路が同等の候補動作を証明するまで、公開版の既定値を`transport=bridge`に保ちます。
- `transport=imm32`は比較・互換確認用に残します。
- `docs/operations/mozc_native_phase0_phase1.md`と`tests/samples/provider_comparison/phase0_cases.tsv`を比較基準に使います。
- 短語、節、助詞を含む文、固有名詞、未知語、かな・英数字混在を含むコーパスで、上位候補、文節、エラー、コールド／ウォーム遅延、代替処理の有無を記録します。

### Phase 1：型付き設定

実装済み：

- `transport`を`bridge`、`server`、`imm32`、`native`、不正値へ型付けして解析する。
- 互換性のため`server`をBridgeの別名として残す。
- `native`では成果物を明示設定し、検証できなければ初期化を失敗させる。
- 不正値を暗黙に置換せず、設定エラーとして表示する。

Phase 2で利用する最小設定：

- `native.backend`：`mozc_server_client`または`linked_converter`
- `native.root`：インストールルートを基準に解決する依存物ルート
- `native.mozc_build_artifact`：固定したMozc成果物の由来
- `native.fallback_policy`：`none`、`imm32`、`bridge`。利用時は記録する。

`native.timeout_ms`や`native.trace`などの追加設定は、実装が実際に利用する段階でだけ追加します。traceは既定で無効とし、明示許可がない限り入力文を保存しません。

### Phase 2：ネイティブ・バックエンドの技術検証

比較ツールと接続候補：

- `docs/dictionary/mozc_native_backend_options.md`
- `tools/provider_compare/New-ProviderComparisonRun.ps1`
- `tests/manual/provider_comparison_phase0.md`

`IMozcTransport`の契約を守り、1つの読みを入力として、順序付き候補、利用可能な正式文節情報、失敗時の構造化エラーを返します。UI固有処理、UIラベルを候補として生成する処理、非公開pipe解析、手書きprotobuf番号は含めません。

最初の推奨経路は、生成済みプロトコルと文書化したセッションライフサイクルを使うOSS `mozc_server`のクライアント／セッション境界です。直接リンク方式は、依存関係を安全に管理でき、サーバー方式が性能または文節要件を満たせない場合に限り検討します。

### Phase 3：評価

既定化の前に確認する項目：

- 10文字以下の読みで候補表示がプロジェクトの性能目標を満たす。
- 一般語コーパスの上位1件／5件が、人手確認でBridge方式より劣らない。
- 複数文節の入力で文節情報が存在し、一貫している。
- バックエンドの失敗理由を利用者が確認できる。
- 代替処理が明示され、記録される。
- native経路に固定パス、固定プロトコル、固定配列依存が残っていない。

### Phase 4：既定値の変更

Phase 3を通過した後に限り、公開版の既定値を`bridge`から`native`へ変更するか検討します。その場合も`bridge`は比較・互換経路、`imm32`は互換モードとして残します。

### Phase 5：内製辞書

- 品詞遷移コストを生成する。
- 辞書コスト空間を正規化する。
- 未知語・信頼度の固定値をモデルまたは検証済み設定へ置き換える。
- Layer 2のビームサーチを実装する。
- 文節情報を出力する。
- ユーザー学習を順位付けへ統合する。
- Phase 0と同じコーパスでBridge／nativeと比較する。

## 外部Mozcに関する前提

OSS MozcはGoogle日本語入力と同一ではありません。公式資料では、MozcはGoogle日本語入力を基にしたOSSソースであり、Googleの正式サポート製品ではなく、安定版という概念もないと説明されています。システム辞書、連語データ、読み訂正、候補フィルター、QA、更新動作にも違いがあります。

WindowsビルドはBazel／Bazeliskを使い、Visual Studio、Python、.NET、Qt、LLVM／MSYS2／Ninjaなどの依存物を含みます。旧GYP手順は非推奨です。

参考資料：

- https://github.com/google/mozc
- https://code.googlesource.com/mozc/+/HEAD/doc/about_branding.md
- https://raw.githubusercontent.com/google/mozc/master/docs/build_mozc_in_windows.md
- https://github.com/google/mozc/blob/master/src/protocol/
- https://raw.githubusercontent.com/google/mozc/master/src/converter/converter_interface.h

したがって、OSS Mozcのnative経路は現在の非公開pipe解析より保守しやすい可能性がありますが、Google日本語入力と同じ候補品質になるとは限りません。既定値の変更前に出力を比較します。

## リスクと未解決事項

- RTASに適したMozc API境界が、直接リンク、クライアントライブラリ、サーバープロトコルのどれか。
- BazelとMSBuildの成果物・依存関係を混在させず、どのようにビルド・保存するか。
- Mozcソース、第三者依存物、`dictionary_oss`データに必要なライセンス・帰属表示。
- nativeがRTASの必要な形式で文節情報を返せるか、`CandidateList.segments`の拡張が必要か。
- 実バックエンドが提供すべき、暗黙でない代替処理の方針。
- 内製辞書は、順位付け、Layer 2、文節情報、学習統合を改善するまで試作扱いである。

Phase 2の第一候補は`mozc_server_client`、第二候補は`linked_converter`です。成果物は`fea1ebace034ade31c611344793f559800e366c9`へ固定し、GitHub Actionsの`Mozc64_x64.msi`と`mozc_server.exe`起動を検証済みです。ATL／MFC追加後、公式クライアント／セッションラッパーとローカルビルドしたサーバーによる実セッション作成・変換・候補・文節取得にも成功しています。MSIを管理展開したサーバーは、完全なインストール環境なしではまだ利用できません。

## 最終提案

`transport=native`は、Bridge実装の非公開pipe処理を移植するのではなく、新しい独立バックエンドとして開発し、当面は任意で有効化する経路にします。

内製辞書は価値のある長期経路ですが、実用的なデコーダーモデルと品質測定が揃う前に、Layer 1の既定変換としてMozcを置き換えるべきではありません。
