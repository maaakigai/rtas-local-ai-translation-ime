# 辞書ベース解析の設計案

関連する移行調査は`docs/dictionary/mozc_native_design_investigation.md`を参照してください。実行時実装へ着手する前に、Bridge方式、Mozcネイティブ方式、内製辞書方式を比較しています。

> この文書は内製辞書方式の設計案です。現在の公開版では、Layer 1の既定経路にBridge方式を使用します。

## 目的

- 多層構成（かな→漢字→翻訳）のインターフェースを維持したまま、LLM中心の処理を決定的な辞書検索へ置き換えられる構造にします。
- `tools/dict_prep`が生成するTSVと互換性を保ちます。
- デコード処理の中心を書き直さず、将来ニューラル再ランキングなどを段階的に追加できるようにします。

## 第1段階：形態素分割（Viterbi）

### グラフ構築

- 入力は、かな文字列`input`と形態素辞書レコード（`MorphRecord`）です。
- `input[i:j)`に一致する辞書項目、または未知語用の代替ノードを格子状に配置します。
- 未知語ノードは、1文字のかな、連続する英数字などの規則で生成し、設定可能なペナルティを与えます。
- 前ノードの終端と次ノードの開始位置が一致する場合に辺を接続します。

### スコアモデル

経路全体のコストは、次の合計です。

1. 辞書に含まれる単語コスト（`record.cost`）
2. 品詞遷移表から取得する遷移コスト`bigram_penalty(prev.pos, current.pos)`
3. `record.features`から求める加点・減点（一般的な活用を優先するなど）
4. 過剰な分割を抑える長さ事前分布

すべて同一のコスト空間で扱い、値が小さいほど良い候補とします。MeCabの意味体系に合わせ、コストは32ビット符号付き整数で保持します。

### 疑似コード

```pseudo
function VITERBI_SEGMENT(input, morph_dict, pos_costs):
    lattice = build_lattice(input, morph_dict)
    best_cost = array(length=lattice.total_nodes, fill=+INF)
    backpointer = array(length=lattice.total_nodes, fill=-1)

    for node in lattice.start_nodes:
        edge_cost = node.word_cost + transition_cost(BOS, node.pos, pos_costs)
        best_cost[node.id] = edge_cost

    for layer in lattice.layers_in_order():
        for node in layer:
            for next_node in node.next_nodes:
                transition = transition_cost(node.pos, next_node.pos, pos_costs)
                feature_bonus = feature_cost(next_node.features)
                candidate = best_cost[node.id] + next_node.word_cost + transition + feature_bonus
                if candidate < best_cost[next_node.id]:
                    best_cost[next_node.id] = candidate
                    backpointer[next_node.id] = node.id

    eos_node = lattice.end_node
    path = reconstruct_path(backpointer, eos_node.id)
    return path_to_tokens(path, lattice)
```

## 第2段階：Layer 2変換（ビームサーチ）

### 課題

- 入力：第1段階で分割したトークン列
- 目的：読みと文脈を保ちながら、自然さが最大になる漢字または漢字かな混じり表記を選ぶ
- 候補元：形態素候補（表層形・基本形）と対訳辞書の語釈

### ビーム設定

- ビーム幅：遅延と精度の均衡を取るため、既定値を8とします。
- 仮説状態：
  - `position`：トークン列内の位置
  - `output`：構築中の漢字かな混じり文字列
  - `score`：コストの合計（小さいほど良い）
  - `translation_buffer`：第3段階用の候補（任意）
- 各トークンの展開候補：
  1. 辞書の表層形との完全一致
  2. 対訳辞書から得た別表記の漢字
  3. 信頼度が低い場合のひらがな維持

### スコア要素

- 軽量なbigram表または頻度規則による言語モデル事前分布
- かなを変換せず残す場合の`kana_penalty`
- `common`または高優先度の対訳辞書項目への加点
- 一定範囲内で矛盾する品詞の混在を避ける文脈整合性

### 疑似コード

```pseudo
function BEAM_REWRITE(tokens, morph_dict, bilingual_dict, beam_width):
    beam = priority_queue(order=lowest_score_first)
    beam.push(state(position=0, output="", score=0, translation_buffer=[]))

    while not beam.empty():
        state = beam.pop()
        if state.position == len(tokens):
            collect_candidate(state)
            continue

        token = tokens[state.position]
        for rewrite in enumerate_rewrites(token, morph_dict, bilingual_dict):
            next_state = state.clone()
            next_state.position += 1
            next_state.output += rewrite.surface
            next_state.score += rewrite.delta_cost
            if rewrite.translation:
                next_state.translation_buffer.append(rewrite.translation)

            beam.push(next_state)

        beam.trim(beam_width)

    return select_best_candidates()
```

### 翻訳バッファ

ビームを展開しながら、優先度付きの英語語釈候補を収集します。第3段階の最終翻訳選択では、対訳辞書を再検索せずこのバッファを再利用できます。

## データインターフェース

- `MorphDictionary`と`BilingualDictionary`のローダーは、繰り返し検索に適したメモリ上のインデックスを返します。
- 後段の処理は辞書レコードを変更せず、返されたポインターを読み取り専用として扱います。
- 将来長文を逐次処理できるよう、デコーダーはイテレーターを受け取る構成にします。

## 未解決事項

- 形態素TSVと同時生成できる、コンパクトな品詞遷移行列ビルダーが必要です。
- 未知語処理にカタカナから漢字を推測する規則が必要か、かなをそのまま通すだけで十分かを評価します。
- 低性能CPUでもビームサーチを5 ms未満に保つ枝刈り方法を調査します。
