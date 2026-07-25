# キーマップ検証ツール

押したキーの `VirtualKeyCode` と実際の入力文字を対話式で記録し、`txt` に保存します。

## 実行

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\keymap_probe\capture_keymap.ps1
```

出力先を指定する場合:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\keymap_probe\capture_keymap.ps1 -OutputPath .\tmp\jp_keymap.txt
```

Ctrl/Alt 組み合わせも追加で取りたい場合:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\keymap_probe\capture_keymap.ps1 -IncludeCtrlAlt
```

## 使い方

1. 取りたいキーボード配列/IME状態にして実行
2. 画面に出る案内どおりにキーを押す
3. 生成された `txt` を見て、`VK` と `Char` の対応を確認

`Esc` で途中終了できます。
