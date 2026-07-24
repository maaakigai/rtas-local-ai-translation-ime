#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../llm/llm_response_parser.h"

namespace ime::conversion {

struct ProviderCapabilities {
    bool supports_layer2 = true;
    bool supports_translation = true;
};

struct SegmentInfo {
    std::size_t index = 0;
    std::size_t start = 0;
    std::size_t length = 0;
    std::wstring surface;
};

struct LayerRequestContext {
    std::wstring reading;
    std::wstring committedText;
    std::wstring hint;
    uint32_t layer = 0;
    bool allowAsync = true;
};

struct CandidateList {
    std::vector<llm::CandidateEntry> entries;
    std::vector<SegmentInfo> segments;
    std::optional<uint64_t> requestId;
    bool pending = false;
    uint32_t layer = 0;
    std::wstring error;
};

using ProviderCallback = std::function<void(uint64_t requestId, CandidateList result)>;

class IConversionProvider {
public:
    virtual ~IConversionProvider() = default;

    virtual ProviderCapabilities GetCapabilities() const {
        return {};
    }
    virtual void SetResultCallback(ProviderCallback callback) = 0;
    virtual CandidateList FetchLayer1(const LayerRequestContext& ctx) = 0;
    virtual CandidateList FetchLayer2(const LayerRequestContext& ctx) = 0;
    virtual CandidateList FetchTranslation(const LayerRequestContext& ctx) = 0;
    virtual bool Cancel(uint64_t requestId) = 0;
};

}  // namespace ime::conversion
