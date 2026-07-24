# Multi-layer state machine

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Preedit: Kana input / toggle ON
    Preedit --> Layer1Candidates: Space
    Preedit --> Idle: Escape or Backspace buffer empty
    Layer1Candidates --> Preedit: Escape
    Layer1Candidates --> Layer1Candidates: Space (cycle cached)
    Layer1Candidates --> Layer2Refine: Enter (provisional accept)
    Layer2Refine --> Layer1Candidates: Escape (back to Japanese)
    Layer2Refine --> Layer2Refine: Space (cycle cached)
    Layer2Refine --> Layer1Merged: Enter (merge into layer1)
    Layer1Merged --> TranslationSpin: Space (start translation)
    Layer1Merged --> Commit: Enter (commit Japanese only)
    TranslationSpin --> TranslationSpin: Space (cycle cached)
    TranslationSpin --> TranslationSpin: Shift+Space (re-query)
    TranslationSpin --> Commit: Enter (commit translation)
    Commit --> Idle: inserted into document
    %% link styling by layer
    linkStyle 0 stroke:#6b7280,stroke-width:2px
    linkStyle 1 stroke:#2563eb,stroke-width:2px
    linkStyle 2 stroke:#2563eb,stroke-width:2px
    linkStyle 3 stroke:#2563eb,stroke-width:2px
    linkStyle 4 stroke:#2563eb,stroke-width:2px
    linkStyle 5 stroke:#2563eb,stroke-width:2px
    linkStyle 6 stroke:#d97706,stroke-width:2px
    linkStyle 7 stroke:#d97706,stroke-width:2px
    linkStyle 8 stroke:#d97706,stroke-width:2px
    linkStyle 9 stroke:#d97706,stroke-width:2px
    linkStyle 10 stroke:#10b981,stroke-width:2px
    linkStyle 11 stroke:#6b7280,stroke-width:2px
    linkStyle 12 stroke:#10b981,stroke-width:2px
    linkStyle 13 stroke:#10b981,stroke-width:2px
    linkStyle 14 stroke:#10b981,stroke-width:2px
    linkStyle 15 stroke:#6b7280,stroke-width:2px
```

## Layer definitions
- **Preedit**: `m_romajiBuffer + m_reading`; overlay shows kana status.
- **Layer1Candidates**: Japanese candidates from the dictionary/IMM32 provider.
- **Layer2Refine**: paraphrase/extension suggestions (dictionary + LLM hybrid).
- **Layer1Merged**: paraphrase merged back, still uncommitted.
- **TranslationSpin**: translation request to LLM using the confirmed Japanese text only.
- **Commit**: final insertion stage; translation replaces Japanese by default.

## Async events
Layer2Refine and TranslationSpin rely on `QueueTranslationAsync` (or future providers). They return `pending=true` with a request id, and `OnTranslationReady` feeds `PreviewCandidateString` once results arrive.

## Key triggers (summary)
| Key | Stage | Effect |
| --- | --- | --- |
| Space | Preedit | Open layer1 candidates |
| Enter | Layer1 | Move to layer2 (provisional accept) |
| Space | Layer2 | Cycle cached paraphrases |
| Shift+Space | Layer2 | Request new paraphrases |
| Enter | Layer2 | Merge selection into layer1 |
| Space | Layer1Merged | Open translation layer (cached first) |
| Shift+Space | Translation | Re-query translation |
| Enter | Translation | Replace Japanese sentence with translation (default) |
| Enter | Layer1Merged | Commit Japanese without translation |
| Escape | any | Step back one layer (Translation → Layer1Merged → Layer1 → Preedit → Idle) |

> Note: Append mode remains available via settings but defaults to replace; providers carry the desired commit mode in metadata.
