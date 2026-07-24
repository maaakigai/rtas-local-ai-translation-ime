# Dictionary-Based Analysis Design

Related migration note: `docs/dictionary/mozc_native_design_investigation.md`
compares bridge, native Mozc, and internal dictionary paths before any runtime
implementation work.

## Goals
- Replace the LLM-first pipeline with deterministic dictionary lookup while keeping the multi-layer (kana → kanji → translation) interface intact.
- Maintain compatibility with the TSV artefacts produced by `tools/dict_prep`.
- Support incremental upgrades (e.g., adding neural rescoring) without rewriting the decoding core.

## Stage 1: Morphological Segmentation (Viterbi)

### Graph Construction
- Input: kana string `input` and morph dictionary records (`MorphRecord`).
- Build a lattice where each node represents `input[i:j)` matched by a dictionary entry or a fallback unknown-token node.
- Unknown nodes are generated with heuristics (single kana, extended Latin/number runs) and assigned a configurable penalty.
- Edges connect nodes whose spans touch (i.e., end of the previous node equals start of the next).

### Scoring Model
Total path cost is the sum of:
1. Word cost from the dictionary (`record.cost`).
2. Transition cost: `bigram_penalty(prev.pos, current.pos)` looked up from a POS transition table.
3. Feature bonuses/penalties derived from `record.features` (e.g., favour common conjugations).
4. Length prior that discourages excessive segmentation.

All terms operate in the same cost space (lower is better). Costs are stored as 32-bit signed integers to match MeCab semantics.

### Pseudocode
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

## Stage 2: Layer 2 Conversion (Beam Search)

### Problem Statement
- Input: sequence of segmented tokens from Stage 1.
- Objective: choose kanji or mixed-script forms that maximise fluency while respecting readings and context.
- Sources: morphological candidates (surface vs. base form) and bilingual dictionary glosses.

### Beam Configuration
- Beam width: configurable (default 8) to balance latency against accuracy.
- Hypothesis state:
  - `position`: index in token list.
  - `output`: accumulated kanji string.
  - `score`: sum of costs (lower is better).
  - `translation_buffer`: candidates for Stage 3 (optional).
- Expansion options per token:
  1. Exact dictionary surface form.
  2. Alternate kanji matched via bilingual entry (e.g., same headword).
  3. Hiragana fallback (keeps kana when confidence is low).

### Scoring Components
- Language model prior (lightweight bigram table or heuristic frequency weighting).
- Penalty for leaving kana untouched (`kana_penalty`).
- Bonus for bilingual entries tagged as `common` or high priority.
- Context compatibility: discourage mixing conflicting POS tags within a window.

### Pseudocode
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

### Translation Buffer
- While expanding the beam, we collect English gloss candidates with priority tags.
- Stage 3 (final translation selection) can reuse the buffer without re-querying the bilingual dictionary.

## Data Interfaces
- `MorphDictionary` and `BilingualDictionary` loaders return in-memory indices optimised for repeated lookup.
- Downstream modules must not mutate dictionary records; they should treat returned pointers as read-only.
- Decoders accept iterators so we can stream tokens for long inputs in the future.

## Open Questions
- Need a compact POS transition matrix builder (potentially generated alongside the morph TSV).
- Evaluate whether heuristics for unknown tokens require katakana-to-kanji guesses or if kana passthrough suffices.
- Investigate pruning heuristics to keep beam search latency under 5 ms on low-end CPUs.

