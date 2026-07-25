# mozc_bridge

このディレクトリの実装は、Google日本語入力との互換性を調査するための研究コードです。
同じ`main.cpp`を2通りにビルドします。

- RTAS DLLでは`RTAS_MOZC_BRIDGE_LIBRARY`を定義し、`transport=bridge`
  （`server`は後方互換エイリアス）からプロセス内で呼び出します。
- `mozc_bridge.vcxproj`では、手動診断用のコンソール実行ファイルを生成します。

応募用スナップショットの既定値は、オリジナルのかな漢字変換を維持するため
`transport=bridge`です。通常動作ではDLLへ組み込んだ同じ実装をin-processで呼び出し、
このコンソール版は手動診断にだけ使います。

> [!WARNING]
> この経路はGoogle日本語入力の非公開セッション境界へ依存します。安定した公開APIではなく、
> Google日本語入力の更新で壊れる可能性があります。製品利用や再配布を前提にせず、
> 利用条件を独立して確認したうえでの互換性調査に限定してください。

## ビルド

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  tools\mozc_bridge\mozc_bridge.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64
```

出力例:

- `tools\mozc_bridge\x64\Release\mozc_bridge.exe`

## 入出力プロトコル

入力（stdin, UTF-8）:

- 1行: `<reading>\n`

出力（stdout, UTF-8）:

- 候補を1行ずつ出力
- エラー時は `ERROR\t<message>` を出力

## 手動テスト

```powershell
cmd /c "echo ねこ|tools\mozc_bridge\x64\Release\mozc_bridge.exe"
```

PowerShell のパイプは文字コードの影響で期待どおり動かないことがあるため、上記の `cmd /c` 形式を推奨します。

## RTAS の既定設定

`config/ime_settings.json` の例:

```json
{
  "provider": {
    "kana": {
      "mode": "mozc",
      "mozc": {
        "enabled": true,
        "transport": "bridge",
        "timeout_ms": 200
      }
    }
  }
}
```

`transport=bridge`はDLLへコンパイルされた実装を呼ぶため、コンソール版のパス設定は
不要です。コンソール版は、同じ候補取得処理をRTAS本体から切り離して観察する場合だけ
使用します。
