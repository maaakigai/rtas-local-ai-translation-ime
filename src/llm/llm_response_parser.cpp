#include "llm_response_parser.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <sstream>
#include <string_view>
#include <utility>

#include "../../Ime3/rtas_utils.h"

namespace llm {
namespace {

std::wstring NormalizeLineEndings(const std::wstring& input) {
    std::wstring out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        wchar_t ch = input[i];
        if (ch == L'\r') {
            if ((i + 1) < input.size() && input[i + 1] == L'\n') {
                ++i;
            }
            out.push_back(L'\n');
        } else if (ch == L'\t') {
            out.push_back(L' ');
        } else if (ch == L'\u3000') {  // full-width space
            out.push_back(L' ');
        } else {
            out.push_back(ch);
        }
    }
    return TrimWhitespace(out);
}

std::vector<std::string> ExtractJsonObjects(const std::string& jsonArray, std::wstring& error) {
    std::vector<std::string> objects;
    int depth = 0;
    bool inString = false;
    bool escape = false;
    size_t objectStart = std::string::npos;
    for (size_t i = 0; i < jsonArray.size(); ++i) {
        const char ch = jsonArray[i];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                objectStart = i;
            }
            ++depth;
        } else if (ch == '}') {
            if (depth == 0) {
                error = L"Unexpected closing brace in JSON response.";
                return {};
            }
            --depth;
            if (depth == 0 && objectStart != std::string::npos) {
                objects.emplace_back(jsonArray.substr(objectStart, i - objectStart + 1));
                objectStart = std::string::npos;
            }
        }
    }
    if (depth != 0) {
        error = L"JSON response ended with unbalanced braces.";
        return {};
    }
    return objects;
}

std::wstring ToWString(const std::string& value) {
    return TrimWhitespace(Utf8ToWide(value));
}

bool StartsWithCaseInsensitive(const std::wstring& text, std::wstring_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        wint_t lhs = std::towlower(static_cast<wint_t>(text[i]));
        wint_t rhs = std::towlower(static_cast<wint_t>(prefix[i]));
        if (lhs != rhs) return false;
    }
    return true;
}

std::wstring BuildCandidateId(const std::wstring& baseId, size_t index) {
    std::wstringstream ss;
    if (!baseId.empty()) {
        ss << baseId << L":";
    }
    ss << index;
    return ss.str();
}

}  // namespace

ParseResult ParseLayer2Response(const std::wstring& response,
    const ParseContext& ctx) {
    ParseResult result;
    std::wstring normalized = NormalizeLineEndings(response);
    if (normalized.empty()) {
        result.error = L"Ollama returned an empty paraphrase response.";
        return result;
    }

    std::string utf8 = WideToUtf8(normalized);
    size_t arrayStart = utf8.find('[');
    size_t arrayEnd = utf8.rfind(']');
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos || arrayEnd <= arrayStart) {
        result.error = L"Paraphrase response was not a JSON array.";
        return result;
    }
    std::string arraySlice = utf8.substr(arrayStart, arrayEnd - arrayStart + 1);

    std::wstring parseError;
    std::vector<std::string> objects = ExtractJsonObjects(arraySlice, parseError);
    if (!parseError.empty()) {
        result.error = parseError;
        return result;
    }
    if (objects.empty()) {
        result.error = L"Paraphrase response did not contain any entries.";
        return result;
    }

    size_t index = 0;
    for (const auto& object : objects) {
        std::string variantUtf8;
        if (!ExtractJsonString(object, "variant", variantUtf8)) {
            continue;
        }
        std::wstring variant = NormalizeLineEndings(Utf8ToWide(variantUtf8));
        if (variant.empty()) {
            continue;
        }

        std::string toneUtf8;
        ExtractJsonString(object, "tone", toneUtf8);
        std::string deltaUtf8;
        ExtractJsonString(object, "delta", deltaUtf8);

        CandidateEntry entry;
        entry.id = BuildCandidateId(ctx.baseId.empty() ? L"layer2" : ctx.baseId, index);
        entry.layer = CandidateLayer::Layer2;
        entry.source = CandidateSource::Llm;
        entry.reading = ctx.reading;
        entry.displayText = variant;
        entry.commitText = variant;
        entry.metadata.lang = L"ja";
        entry.metadata.llmRequestId = ctx.llmRequestId;
        entry.metadata.tone = Utf8ToWide(toneUtf8);
        entry.metadata.delta = Utf8ToWide(deltaUtf8);

        const std::wstring deltaLower = ToWString(deltaUtf8);
        if (!ctx.committedText.empty()) {
            if (deltaLower == L"suffix" && variant.find(ctx.committedText) == std::wstring::npos) {
                entry.metadata.partial = true;
                entry.commitText = ctx.committedText + variant;
            } else if (variant.rfind(ctx.committedText, 0) == 0) {
                entry.commitText = variant;
            }
        }

        result.entries.emplace_back(std::move(entry));
        ++index;
    }

    if (result.entries.empty()) {
        result.error = L"No valid paraphrase variants were parsed.";
    }
    return result;
}

ParseResult ParseTranslationResponse(const std::wstring& response,
    const ParseContext& ctx) {
    ParseResult result;
    std::wstring normalized = NormalizeLineEndings(response);
    if (normalized.empty()) {
        result.error = L"Ollama returned an empty translation.";
        return result;
    }

    constexpr std::wstring_view kAppendMarker = L"APPEND_MODE=ON";
    bool appendMode = ctx.preferAppendMode;
    if (StartsWithCaseInsensitive(normalized, kAppendMarker)) {
        normalized = NormalizeLineEndings(normalized.substr(kAppendMarker.size()));
        appendMode = true;
    }

    if (normalized.empty()) {
        result.error = L"Ollama translation text was empty after normalization.";
        return result;
    }

    CandidateEntry entry;
    entry.id = BuildCandidateId(ctx.baseId.empty() ? L"translation" : ctx.baseId, 0);
    entry.layer = CandidateLayer::Translation;
    entry.source = CandidateSource::Llm;
    entry.reading = ctx.reading;
    entry.displayText = normalized;
    entry.commitText = normalized;
    entry.metadata.lang = L"en";
    entry.metadata.llmRequestId = ctx.llmRequestId;
    entry.metadata.commitMode = appendMode ? CommitMode::Append : CommitMode::Replace;
    entry.metadata.partial = false;

    result.entries.emplace_back(std::move(entry));
    return result;
}

}  // namespace llm
