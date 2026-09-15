#include <cstdio>
#include <future>

#include "../../Ime3/rtas_candidate_request.h"
#include "../../Ime3/rtas_layer_state.h"
#include "../../Ime3/rtas_translation.h"
#include "../../src/provider/mozc_conversion_provider.h"

bool RunCandidateStateTests() {
    bool ok = true;
    auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "CHECK FAILED: %s\n", message);
            ok = false;
        }
    };

    LayerState state;
    expect(state.OnEnter(false, true) == CandidateEnterAction::HoldInLayer2,
           "Layer1 Enter holds Japanese in Layer2 without committing");
    expect(state.OnEnter(true, true) == CandidateEnterAction::HoldInLayer2,
           "Layer1 Shift+Enter also retains the draft");
    expect(state.OnEnter(false, false) == CandidateEnterAction::CommitJapanese,
           "kana-kanji-only mode commits Japanese");
    state.tab = CandidateTab::Layer2;
    state.SetPendingCarry(true);
    expect(state.HasPendingCarry(), "Layer2 permits continued draft editing");
    expect(state.OnEnter(false, true) == CandidateEnterAction::CommitJapanese,
           "Layer2 Enter retains the existing Japanese commit behavior");
    expect(state.OnEnter(true, true) == CandidateEnterAction::MergeIntoLayer1,
           "Layer2 Shift+Enter merges for continued editing");
    state.SetMerged(true);
    expect(!state.HasPendingCarry(), "merge clears pending carry");
    state.tab = CandidateTab::Layer1;
    expect(state.IsMerged(), "merged draft returns to Layer1");
    expect(state.OnEnter(false, true) == CandidateEnterAction::CommitJapanese,
           "merged draft can commit Japanese");
    state.tab = CandidateTab::Translation;
    expect(state.OnEnter(false, true) == CandidateEnterAction::CommitTranslation,
           "translation Enter commits translation");

    // The default provider's Layer2 must preserve the selected Japanese sentence.
    ime::conversion::MozcConversionProvider provider({}, {});
    ime::conversion::LayerRequestContext context;
    context.reading = L"かなかんじへんかん";
    context.committedText = L"かな漢字変換を試します。";
    context.layer = 2;
    const auto held = provider.FetchLayer2(context);
    expect(held.layer == 2 && !held.pending && held.entries.size() == 1,
           "Layer2 holds one Japanese candidate synchronously");
    if (!held.entries.empty()) {
        expect(held.entries.front().commitText == context.committedText,
               "Layer2 preserves the selected text instead of reconverting its reading");
    }

    // A completes after cancellation and replacement by B, including across queues.
    AsyncWorkQueue oldQueue;
    AsyncWorkQueue newQueue;
    std::promise<void> entered, release;
    auto released = release.get_future();
    std::promise<uint64_t> delivered;
    auto delivery = delivered.get_future();
    const uint64_t a = oldQueue.Enqueue([&](uint64_t id, const auto&) {
        entered.set_value();
        released.wait();
        delivered.set_value(id); // Simulate a result already on its way to the UI.
    });
    entered.get_future().wait();
    CandidateRequest request;
    request.Start(a, L"same text");
    expect(request.Cancel() == a, "cancel returns the invalidated request id");
    oldQueue.Cancel(a);
    const uint64_t b = newQueue.Enqueue([](uint64_t, const auto&) {});
    expect(a != b && a != 0 && b != 0, "IDs are unique across provider and fallback queues");
    request.Start(b, L"same text");
    release.set_value();
    expect(!request.Complete(delivery.get(), L"same text"),
           "late A cannot replace B even for identical source text");
    expect(!request.Complete(b, L"same text", true), "pending notification is not completion");
    expect(!request.Complete(b, L"different text"), "source mismatch cannot update candidates");
    expect(request.Complete(b, L"same text") == std::optional<std::wstring>(L"same text"),
           "B remains valid after stale or pending notifications");
    expect(!request.Complete(b, L"same text"), "duplicate completion is ignored");
    request.Start(b, L"same text");
    request.Cancel(); // Escape, cached selection, composition close, or deactivation.
    expect(!request.Complete(b, L"same text"), "dismissed request cannot repopulate candidates");
    oldQueue.Shutdown();
    newQueue.Shutdown();
    return ok;
}
