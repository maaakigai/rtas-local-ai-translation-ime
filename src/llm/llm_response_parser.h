#pragma once

#include <optional>
#include <string>
#include <vector>

namespace llm {

enum class CandidateLayer {
    Layer1,
    Layer2,
    Translation
};

enum class CandidateSource {
    Imm32,
    Dict,
    Llm,
    Cache
};

enum class CommitMode {
    Replace,
    Append
};

struct CandidateMetadata {
    std::vector<std::wstring> altVariants;
    std::optional<uint64_t> llmRequestId;
    bool partial = false;
    std::wstring lang;
    std::wstring tone;                    // Layer2: polite/casual etc.
    std::wstring delta;                   // Layer2: full/suffix
    CommitMode commitMode = CommitMode::Replace;
    std::optional<std::wstring> timestamp;
};

struct CandidateEntry {
    std::wstring id;
    CandidateLayer layer = CandidateLayer::Layer1;
    std::wstring displayText;
    std::wstring commitText;
    std::wstring reading;
    CandidateSource source = CandidateSource::Imm32;
    std::optional<double> confidence;
    CandidateMetadata metadata;
};

struct ParseContext {
    std::wstring reading;
    std::wstring committedText;
    std::wstring baseId;                // e.g. "layer2"
    CandidateLayer layer = CandidateLayer::Layer2;
    CandidateSource source = CandidateSource::Llm;
    std::optional<uint64_t> llmRequestId;
    bool preferAppendMode = false;      // user setting
};

struct ParseResult {
    std::vector<CandidateEntry> entries;
    std::wstring error;
};

ParseResult ParseLayer2Response(const std::wstring& response,
    const ParseContext& ctx);

ParseResult ParseTranslationResponse(const std::wstring& response,
    const ParseContext& ctx);

}  // namespace llm
