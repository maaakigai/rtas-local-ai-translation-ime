# User Learning API Specification

## Goals
- Capture confirmations, corrections, and rejected candidates so the dictionary provider can adjust rankings over time.
- Keep the data store transparent and easy to reset (append-only JSONL files plus compact summaries).
- Avoid affecting the existing LLM-first pipeline until dictionary mode is explicitly enabled.

## Data Model

### Event Types
| Event | Description | Payload Fields |
| --- | --- | --- |
| `conversion.accepted` | User confirmed a candidate produced by the dictionary provider. | `reading`, `surface`, `pos`, `provider_version` |
| `conversion.corrected` | User replaced the proposed candidate with manual input. | `reading`, `surface`, `replacement`, `reason` (`typo`, `new_word`, `preference`) |
| `translation.selected` | User cycled to a translation candidate and confirmed it. | `source_text`, `target_text`, `rank`, `confidence_hint` |
| `candidate.reordered` | User promoted or demoted a candidate via UI controls. | `reading`, `surface`, `delta` (+/- integer) |

### Storage Layout
- Events are appended as newline-delimited JSON (`.jsonl`) under `data/user_learn/profiles/<SID>/events.log`.
- Periodic compaction writes `summary.json`:
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
- Compaction is triggered on IME shutdown or after 500 appended events, whichever comes first.

## Compression Workflow Draft
1. **Aggregation trigger**  
   When `events.log` grows beyond 2 MB or more than 1,000 events, enqueue a background compaction task; always trigger on IME shutdown.
2. **Snapshot rotation**  
   Flush buffers, rename `events.log` to `events-<timestamp>.jsonl`, and reopen a fresh log for new events.
3. **Summarisation**  
   Parse the rotated log, fold counts into `summary.json`, and emit a diff file `totals-<timestamp>.json` for diagnostics.
4. **Compression**  
   Gzip the rotated log into `archive/events-<timestamp>.jsonl.gz`. Store the original file size and CRC32 alongside the gzip to support integrity checks, then delete the plain-text copy.
5. **Retention**  
   Keep the latest seven gzip archives; prune older files to cap disk usage. Record the most recent archive timestamp in `summary.json`.
6. **Recovery**  
   On startup, scan `archive/` and `profiles/<SID>/` for orphaned `.jsonl` files; reprocess them if a previous compaction terminated mid-way.

Implementation notes:
- Compression runs on a low-priority worker thread so UI input latency is unaffected.
- All metadata (timestamps, hash, compressed size) lives in `summary.json`, allowing the diagnostics surface to show the last compaction result.
- A future CLI helper can bundle the latest archives for support escalation.

## API Surface (C++)
```cpp
namespace ime::learning {

struct LearningEvent {
  std::string type;           // e.g. "conversion.accepted"
  std::string payload_json;   // Serialized payload for append-only logging.
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

## Integration Plan
1. **Configuration gating** – only instantiate the learning store when `provider.dictionary.learning.enable` is true.
2. **Event plumbing** – extend conversion callbacks to emit `LearningEvent` objects once dictionary mode shipping criteria are met.
3. **Compaction service** – background worker performs the compression workflow above; disabled entirely while in LLM-only mode.
4. **Telemetry reset** – expose a command in the settings UI (or CLI script) that wipes `profiles/<SID>` after confirming with the user.

## Security & Privacy
- Files inherit the user's profile ACLs; no elevation required.
- Events redact control characters and truncate payload strings to a safe length (currently 64 code units).
- Future enhancement: optional DPAPI encryption for enterprise builds.
