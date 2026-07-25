# ユーザー学習API仕様案

> この文書は、辞書モード向けに検討している設計案です。現在の公開版ではユーザー学習は既定で無効であり、通常のLLMモードでは学習データを保存しません。

## 目的

- 確定、訂正、候補の不採用を記録し、辞書プロバイダーが将来の候補順位を調整できるようにします。
- 追記専用のJSONLと集約済みサマリーを使い、保存内容を確認しやすく、簡単に初期化できるようにします。
- 辞書モードを明示的に有効化するまでは、既存のLLM中心の処理へ影響を与えません。

## データモデル

### イベント種別

| イベント | 説明 | ペイロードのフィールド |
| --- | --- | --- |
| `conversion.accepted` | ユーザーが辞書プロバイダーの候補を確定した。 | `reading`, `surface`, `pos`, `provider_version` |
| `conversion.corrected` | 提示候補をユーザーが手入力で訂正した。 | `reading`, `surface`, `replacement`, `reason`（`typo`、`new_word`、`preference`） |
| `translation.selected` | ユーザーが翻訳候補へ切り替えて確定した。 | `source_text`, `target_text`, `rank`, `confidence_hint` |
| `candidate.reordered` | UI操作で候補の順位を上げ下げした。 | `reading`, `surface`, `delta`（正または負の整数） |

### 保存構成

- イベントは、`data/user_learn/profiles/<SID>/events.log`へ1行1件のJSON（`.jsonl`）として追記します。
- 定期的な集約処理で`summary.json`を書き出します。

  ```json
  {
    "version": 1,
    "updated_at": "2025-10-20T10:05:00Z",
    "surface_stats": {
      "sample_surface": {"accept": 12, "correct": 1},
      "fallback_surface": {"accept": 5, "correct": 3}
    },
    "translation_stats": {
      "sample_source": {"en": {"accept": 4, "reject": 1}}
    }
  }
  ```

- 集約は、IME終了時または500件のイベント追記後のうち、先に到達した時点で開始する想定です。

## 圧縮処理の設計案

1. **集約開始条件**
   `events.log`が2 MBを超えるか、1,000件を超えた場合にバックグラウンド集約を予約します。IME終了時には必ず開始します。
2. **スナップショットのローテーション**
   バッファを書き出し、`events.log`を`events-<timestamp>.jsonl`へ変更して、新しいイベント用の空ログを開き直します。
3. **集約**
   ローテーションしたログを解析して回数を`summary.json`へ反映し、診断用の差分`totals-<timestamp>.json`を出力します。
4. **圧縮**
   ローテーションしたログを`archive/events-<timestamp>.jsonl.gz`へgzip圧縮します。整合性確認用に元ファイルのサイズとCRC32を記録した後、平文のコピーを削除します。
5. **保持期間**
   最新7件のgzipアーカイブを残し、それより古いファイルを削除してディスク使用量を制限します。最新アーカイブの日時は`summary.json`へ記録します。
6. **復旧**
   起動時に`archive/`と`profiles/<SID>/`を走査し、前回の集約が途中終了して孤立した`.jsonl`があれば再処理します。

実装時の注意事項：

- UI入力の遅延を避けるため、圧縮は優先度の低いワーカースレッドで実行します。
- 日時、ハッシュ、圧縮後サイズなどのメタデータを`summary.json`へ保存し、診断画面から直近の集約結果を確認できるようにします。
- 将来、サポート調査用に最新アーカイブをまとめるCLI補助ツールを追加できます。

## C++ API
```cpp
namespace ime::learning {

struct LearningEvent {
  std::string type;           // 例："conversion.accepted"
  std::string payload_json;   // 追記ログ用にシリアライズしたペイロード
};

class UserLearningStore {
 public:
  virtual ~UserLearningStore() = default;
  virtual bool AppendEvent(const LearningEvent& event) = 0;
  virtual bool Flush() = 0;
};

std::unique_ptr<UserLearningStore> CreateFileStore(
    const std::filesystem::path& profile_root);

}  // namespace ime::learning
```

## 統合計画

1. **設定による有効化**：`provider.dictionary.learning.enable`が`true`の場合にだけ学習ストアを生成します。
2. **イベントの接続**：辞書モードが公開基準を満たした段階で変換コールバックを拡張し、`LearningEvent`を送出します。
3. **集約サービス**：バックグラウンドワーカーが前述の圧縮処理を実行します。LLM専用モードでは完全に無効化します。
4. **学習データの初期化**：ユーザーの確認後に`profiles/<SID>`を消去する操作を、設定UIまたはCLIスクリプトから利用できるようにします。

## セキュリティとプライバシー

- ファイルはユーザープロファイルのアクセス制御を継承し、管理者権限を必要としません。
- イベントから制御文字を除去し、ペイロード文字列を安全な長さ（現在の案では64コード単位）に制限します。
- 将来案として、企業向けビルドではDPAPIによる任意の暗号化を検討します。
