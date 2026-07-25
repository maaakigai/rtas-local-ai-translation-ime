# `ConversionProvider`インターフェース

`IConversionProvider`は、TSFテキストサービスおよび候補UIを、実際に使用する変換バックエンドから分離するためのインターフェースです。正式な宣言は`src/api/conversion_provider.h`にあります。

```cpp
struct LayerRequestContext {
    std::wstring reading;
    std::wstring committedText;
    std::wstring hint;
    uint32_t layer = 0;
    bool allowAsync = true;
};

struct CandidateList {
    std::vector<llm::CandidateEntry> entries;
    std::vector<SegmentInfo> segments;
    std::optional<uint64_t> requestId;
    bool pending = false;
    uint32_t layer = 0;
    std::wstring error;
};

using ProviderCallback =
    std::function<void(uint64_t requestId, CandidateList result)>;

class IConversionProvider {
public:
    virtual ~IConversionProvider() = default;
    virtual ProviderCapabilities GetCapabilities() const;
    virtual void SetResultCallback(ProviderCallback callback) = 0;
    virtual CandidateList FetchLayer1(const LayerRequestContext& ctx) = 0;
    virtual CandidateList FetchLayer2(const LayerRequestContext& ctx) = 0;
    virtual CandidateList FetchTranslation(
        const LayerRequestContext& ctx) = 0;
    virtual bool Cancel(uint64_t requestId) = 0;
};
```

## 責務

- `TextService`は、読み、確定済み文字列、対象レイヤー、非同期実行を許可するかどうかを渡します。
- Layer 1は、選択した日本語変換バックエンドからかな漢字変換候補を返します。
- Layer 2は、対応するプロバイダーの場合に日本語の言い換え候補を返します。
- Translationは、選択した日本語文に対する英訳候補を返します。
- `segments`には、バックエンドが提供する任意の文節情報を格納します。
- 同期的に完了できないプロバイダーは、`pending=true`と`requestId`を返し、完了後に`ProviderCallback`を通じて最終的な`CandidateList`を通知します。

## 結果とキャンセルの契約

- UIは`requestId`を使い、入力変更、レイヤー切り替え、キャンセルより前の古い応答を破棄できます。
- `Cancel(requestId)`は処理の中止を要求します。ただし、中止後に届いたコールバックも呼び出し側で古い応答として扱う必要があります。
- `pending=false`かつ`entries`が空の場合は、「処理は完了したが候補がない」ことを表します。失敗によって候補が空になった場合、プロバイダーはユーザー向けの診断情報を`error`へ設定します。
- すべてのバックエンドが全レイヤーを実装しているとは限らないため、対応機能は`GetCapabilities()`で確認します。

## 現在の実装範囲

公開版の既定構成では、Layer 1にBridge方式、Layer 2とTranslationにLLMプロバイダーを使用します。IMM32方式とMozcネイティブ方式は、明示的に選択する調査・試験用の経路です。バックエンドの選択は`src/provider/conversion_provider_factory.cpp`が担当し、UIコードは特定バックエンドの実行ファイルや通信プロトコルへ依存しません。
