#pragma once

enum class CandidateTab { Layer1, Layer2, Translation };
enum class CandidateEnterAction { HoldInLayer2, CommitJapanese, MergeIntoLayer1, CommitTranslation };

// Input decisions only. TSF edits and candidate presentation remain in TextService.
class LayerState {
public:
    CandidateTab tab = CandidateTab::Layer1;

    void SetMerged(bool merged) {
        merged_ = merged;
        if (merged) pendingCarry_ = false;
    }

    void SetPendingCarry(bool pending) {
        pendingCarry_ = pending;
        if (pending) merged_ = false;
    }

    bool IsMerged() const { return merged_ && tab == CandidateTab::Layer1; }
    bool HasPendingCarry() const { return pendingCarry_ && tab == CandidateTab::Layer2; }

    CandidateEnterAction OnEnter(bool shift, bool supportsLayer2) const {
        switch (tab) {
        case CandidateTab::Layer1:
            // Kana-kanji conversion becomes an uncommitted translation draft in Layer2.
            return IsMerged() || !supportsLayer2
                ? CandidateEnterAction::CommitJapanese
                : CandidateEnterAction::HoldInLayer2;
        case CandidateTab::Layer2:
            return shift ? CandidateEnterAction::MergeIntoLayer1
                         : CandidateEnterAction::CommitJapanese;
        case CandidateTab::Translation:
            return CandidateEnterAction::CommitTranslation;
        }
        return CandidateEnterAction::CommitJapanese;
    }

private:
    bool merged_ = false;
    bool pendingCarry_ = false;
};
