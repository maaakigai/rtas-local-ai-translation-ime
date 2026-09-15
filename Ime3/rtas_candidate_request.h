#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// Owned by the TSF thread. Workers post results to that thread before completion.
// Each layer has at most one current request; cancellation invalidates it first.
class CandidateRequest {
public:
    void Start(uint64_t id, std::wstring key) {
        id_ = id;
        key_ = std::move(key);
    }

    std::optional<std::wstring> Complete(uint64_t id,
                                         const std::wstring& currentKey,
                                         bool pending = false) {
        if (!id || id != id_ || pending || key_ != currentKey) return std::nullopt;
        id_ = 0;
        return std::exchange(key_, {});
    }

    uint64_t Cancel() {
        key_.clear();
        return std::exchange(id_, 0);
    }

private:
    uint64_t id_ = 0;
    std::wstring key_;
};
