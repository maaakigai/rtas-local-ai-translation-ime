#include <cstdio>
#include <future>

#include "../../Ime3/rtas_candidate_request.h"
#include "../../Ime3/rtas_layer_state.h"
#include "../../Ime3/rtas_translation.h"
#include "../../Ime3/rtas_utils.h"
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
    for (UINT key : {'A', 'C', 'V', 'X', 'Z', 'Y', 'S', 'F'}) {
        expect(NormalizeImeShortcut(key, MOD_CONTROL, true) == 0,
               "application Ctrl shortcuts pass through even during composition");
    }
    expect(NormalizeImeShortcut('T', MOD_CONTROL, false) == 0, "Ctrl+T opens an application tab outside composition");
    expect(NormalizeImeShortcut('T', MOD_CONTROL, true) == VK_F10, "Ctrl+T converts active input");
    expect(NormalizeImeShortcut('U', MOD_CONTROL, true) == VK_F6, "Ctrl+U aliases F6");
    expect(NormalizeImeShortcut('I', MOD_CONTROL, true) == VK_F7, "Ctrl+I aliases F7");
    expect(NormalizeImeShortcut('O', MOD_CONTROL, true) == VK_F8, "Ctrl+O aliases F8");
    expect(NormalizeImeShortcut('P', MOD_CONTROL, true) == VK_F9, "Ctrl+P aliases F9");
    expect(NormalizeImeShortcut('T', MOD_CONTROL | MOD_SHIFT, true) == 0, "Ctrl+Shift+T remains an application shortcut");
    expect(NormalizeImeShortcut(VK_SPACE, MOD_CONTROL, true) == VK_SPACE, "Ctrl+Space inserts a literal space");
    expect(NormalizeImeShortcut(VK_TAB, MOD_CONTROL | MOD_SHIFT, true) == VK_TAB, "reverse tab shortcut preserved");
    expect(NormalizeImeShortcut(VK_F4, MOD_ALT, true) == 0, "Alt+F4 is never swallowed");
    expect(NormalizeImeShortcut(VK_F10, MOD_SHIFT, true) == 0, "Shift+F10 preserves the application context menu");
    expect(NormalizeImeShortcut('L', MOD_WIN, true) == 0, "Windows shortcuts pass through");
    expect(NormalizeImeShortcut(VK_OEM_3, MOD_ALT, false) == VK_KANJI, "US keyboard Alt+backtick toggles IME");
    expect(NormalizeImeShortcut(VK_CAPITAL, MOD_CONTROL, false) == VK_DBE_HIRAGANA, "Ctrl+CapsLock enables Japanese input");
    expect(ConvertCharacterForm(L"ｶﾞギＡ", VK_F6) == L"がぎＡ", "F6 handles half-width voiced kana");
    expect(ConvertCharacterForm(L"がぎぐ", VK_F7) == L"ガギグ", "F7 converts to katakana");
    expect(ConvertCharacterForm(L"ぱぴぷ", VK_F8) == L"ﾊﾟﾋﾟﾌﾟ", "F8 handles expanding voiced kana");
    expect(ConvertCharacterForm(L"nihon 123", VK_F9) == L"ｎｉｈｏｎ　１２３", "F9 produces full-width Latin input");
    expect(ConvertCharacterForm(L"ＡＢＣ１２３", VK_F10) == L"ABC123", "F10 produces half-width Latin input");
    expect(ConvertCharacterForm(L"hello world", VK_F10, 1) == L"HELLO WORLD", "repeated F10 can uppercase");
    expect(ConvertCharacterForm(L"HELLO WORLD", VK_F10, 2) == L"Hello World", "repeated F10 can capitalize words");
    expect(ConvertCharacterForm(L"", VK_F7).empty(), "empty form conversion is safe");
    std::wstring roman;
    std::wstring converted;
    for (wchar_t ch : std::wstring(L"konnichiha")) {
        roman += ch;
        converted += ConvertRomajiBuffer(roman, false);
    }
    converted += ConvertRomajiBuffer(roman, true);
    expect(converted == L"こんにちは" && roman.empty(), "shared converter preserves incremental romaji input");
    roman = L"gakkou";
    expect(ConvertRomajiBuffer(roman, true) == L"がっこう", "shared converter preserves doubled consonants");
    roman = RomajiFromKana(L"しゃしん がっこう");
    expect(ConvertRomajiBuffer(roman, true) == L"しゃしん がっこう", "edited kana can regenerate valid romaji");
    const std::wstring emoji = L"a\U0001F600b";
    expect(MoveTextOffset(emoji, 1, 1) == 3 && MoveTextOffset(emoji, 3, -1) == 1,
           "caret and delete never split a UTF-16 surrogate pair");
    expect(MoveTextOffset(emoji, 0, -1) == 0 && MoveTextOffset(emoji, 4, 1) == 4, "caret stays within text boundaries");
    expect(CandidatePageStart(9) == 9 && CandidatePageStart(19) == 18, "last partial page has stable numbering");
    expect(CandidatePageSelection(0, 20, -1) == 18, "PageUp wraps to the last page");
    expect(CandidatePageSelection(17, 20, 1) == 19, "PageDown clamps within the last page");
    expect(CandidatePageSelection(8, 10, 1) == 9 && CandidatePageSelection(9, 10, 1) == 0, "partial pages remain navigable");
    expect(CandidatePageSelection(0, 0, -1) == 0, "empty candidate list is safe");
    expect(state.OnEnter(false, true) == CandidateEnterAction::HoldInLayer2,
           "Layer1 Enter holds Japanese in Layer2 without committing");
    expect(state.OnEnter(true, true) == CandidateEnterAction::HoldInLayer2,
           "Layer1 Shift+Enter also retains the draft");
    expect(state.OnEnter(false, false) == CandidateEnterAction::CommitJapanese,
           "kana-kanji-only mode commits Japanese");
    roman = L"ga";
    expect(ConvertRomajiBuffer(roman, true) == L"が" && roman.empty(),
           "kana-only Enter draft is ga -> が; kanji conversion is not a romaji operation");
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
