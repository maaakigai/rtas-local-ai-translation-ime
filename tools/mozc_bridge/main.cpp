#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bridge_api.h"

#pragma comment(lib, "Imm32.lib")

namespace {

constexpr wchar_t kBridgeWindowClass[] = L"RTAS_MozcBridgeWindowClass";
constexpr wchar_t kSessionPipePattern[] = L"\\\\.\\pipe\\googlejapaneseinput.*.session";
constexpr wchar_t kGoogleImeConverterPath[] =
    L"C:\\Program Files (x86)\\Google\\Google Japanese Input\\GoogleIMEJaConverter.exe";

enum class WireType : uint32_t {
  kVarint = 0,
  kFixed64 = 1,
  kLengthDelimited = 2,
  kStartGroup = 3,
  kEndGroup = 4,
  kFixed32 = 5,
};

bool IsDebugEnabled() {
  static const bool enabled = ([] {
    char value[2] = {};
    DWORD len = GetEnvironmentVariableA("MOZC_BRIDGE_DEBUG", value, static_cast<DWORD>(std::size(value)));
    return len > 0;
  })();
  return enabled;
}

void DebugLog(const std::string& message) {
  if (IsDebugEnabled()) {
    std::cerr << "[mozc_bridge] " << message << std::endl;
  }
}

std::wstring Utf8ToWide(const std::string& input) {
  if (input.empty()) {
    return {};
  }
  int required = ::MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), nullptr, 0);
  if (required <= 0) {
    return {};
  }
  std::wstring out(static_cast<size_t>(required), L'\0');
  ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                        static_cast<int>(input.size()), out.data(), required);
  return out;
}

std::string WideToUtf8(const std::wstring& input) {
  if (input.empty()) {
    return {};
  }
  int required = ::WideCharToMultiByte(
      CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
      nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(required), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                        out.data(), required, nullptr, nullptr);
  return out;
}

std::wstring TrimWhitespace(const std::wstring& input) {
  std::wstring result = input;
  auto is_space = [](wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
  };
  while (!result.empty() && is_space(result.front())) {
    result.erase(result.begin());
  }
  while (!result.empty() && is_space(result.back())) {
    result.pop_back();
  }
  return result;
}

bool IsSentenceDelimiter(wchar_t ch) {
  switch (ch) {
    case L'、':
    case L'。':
    case L',':
    case L'.':
    case L'！':
    case L'!':
    case L'？':
    case L'?':
    case L' ':
    case L'　':
      return true;
    default:
      return false;
  }
}

bool IsClauseParticle(wchar_t ch) {
  switch (ch) {
    case L'は':
    case L'を':
    case L'が':
    case L'に':
    case L'で':
    case L'と':
    case L'も':
    case L'へ':
    case L'や':
    case L'の':
    case L'か':
      return true;
    default:
      return false;
  }
}

struct SegmentBoundary {
  size_t index = 0;
  size_t start = 0;
  size_t length = 0;
  std::wstring surface;
};

bool IsKanaChar(wchar_t ch) {
  return (ch >= 0x3041 && ch <= 0x3096) ||  // hira
         (ch >= 0x30A1 && ch <= 0x30FA) ||  // kata
         ch == 0x30FC;                       // long vowel mark
}

struct RawPreeditSegment {
  std::wstring key;
  std::wstring value;
};

struct CandidateSegmentRecord {
  std::wstring key;
  std::vector<std::wstring> candidates;
  size_t focusedIndex = 0;
};

std::vector<SegmentBoundary> BuildHeuristicSegments(const std::wstring& reading) {
  std::vector<SegmentBoundary> segments;
  if (reading.empty()) return segments;

  size_t begin = 0;
  size_t index = 0;
  bool hasDelimiter = false;
  for (wchar_t ch : reading) {
    if (IsSentenceDelimiter(ch)) {
      hasDelimiter = true;
      break;
    }
  }

  if (hasDelimiter) {
    for (size_t i = 0; i < reading.size(); ++i) {
      if (!IsSentenceDelimiter(reading[i])) continue;
      if (i > begin) {
        SegmentBoundary s;
        s.index = index++;
        s.start = begin;
        s.length = i - begin;
        s.surface = reading.substr(begin, s.length);
        segments.push_back(std::move(s));
      }
      begin = i + 1;
    }
    if (begin < reading.size()) {
      SegmentBoundary s;
      s.index = index++;
      s.start = begin;
      s.length = reading.size() - begin;
      s.surface = reading.substr(begin);
      segments.push_back(std::move(s));
    }
    return segments;
  }

  // No explicit delimiter:
  // Keep segmentation conservative to avoid broken splits like "こんに|ちは".
  // Pick at most one clause boundary.
  size_t bestBoundary = std::wstring::npos;
  const size_t n = reading.size();
  for (size_t i = 1; i + 1 < n; ++i) {
    if (!IsClauseParticle(reading[i - 1])) continue;
    const size_t left = i;
    const size_t right = n - i;
    // Avoid too short segments on either side.
    if (left < 4 || right < 2) continue;
    // Prefer the earliest boundary that satisfies minimum lengths.
    bestBoundary = i;
    break;
  }
  if (bestBoundary != std::wstring::npos) {
    SegmentBoundary s;
    s.index = index++;
    s.start = begin;
    s.length = bestBoundary - begin;
    s.surface = reading.substr(begin, s.length);
    segments.push_back(std::move(s));
    begin = bestBoundary;
  }

  if (begin < reading.size()) {
    SegmentBoundary s;
    s.index = index++;
    s.start = begin;
    s.length = reading.size() - begin;
    s.surface = reading.substr(begin);
    segments.push_back(std::move(s));
  }
  return segments;
}

std::vector<SegmentBoundary> MergePreeditWithFallback(
    const std::vector<SegmentBoundary>& preedit,
    const std::vector<SegmentBoundary>& fallback,
    const std::wstring& reading) {
  if (preedit.empty()) return fallback;
  std::vector<SegmentBoundary> out = preedit;
  size_t cursor = 0;
  for (const auto& s : out) {
    const size_t end = s.start + s.length;
    if (end > cursor) cursor = end;
  }
  if (cursor >= reading.size()) {
    return out;
  }
  for (const auto& s : fallback) {
    if (s.start < cursor) continue;
    SegmentBoundary add = s;
    add.index = out.size();
    out.push_back(std::move(add));
  }
  if (out.empty()) return fallback;
  return out;
}

std::vector<std::wstring> ComposeSentenceCandidatesFromSegments(
    const std::vector<std::wstring>& candidates,
    const std::vector<SegmentBoundary>& segments) {
  if (candidates.empty() || segments.size() < 2) {
    return candidates;
  }
  std::vector<SegmentBoundary> sorted = segments;
  std::sort(sorted.begin(), sorted.end(),
            [](const SegmentBoundary& a, const SegmentBoundary& b) {
              return a.start < b.start;
            });
  if (sorted.front().start != 0 || sorted.front().length == 0) {
    return candidates;
  }
  std::wstring tail;
  for (size_t i = 1; i < sorted.size(); ++i) {
    tail.append(sorted[i].surface);
  }
  if (tail.empty()) {
    return candidates;
  }

  std::vector<std::wstring> out;
  out.reserve(candidates.size());
  std::unordered_set<std::wstring> seen;
  for (const auto& cand : candidates) {
    if (cand.empty()) continue;
    std::wstring composed = cand;
    const bool alreadyHasTail =
        composed.size() >= tail.size() &&
        composed.compare(composed.size() - tail.size(), tail.size(), tail) == 0;
    if (!alreadyHasTail) {
      composed.append(tail);
    }
    if (seen.insert(composed).second) {
      out.push_back(std::move(composed));
    }
  }
  if (out.empty()) {
    return candidates;
  }
  return out;
}

bool ReadVarint(const std::string& data, size_t* pos, uint64_t* value) {
  if (!pos || !value) {
    return false;
  }
  uint64_t result = 0;
  int shift = 0;
  while (*pos < data.size() && shift <= 63) {
    const uint8_t byte = static_cast<uint8_t>(data[*pos]);
    ++(*pos);
    result |= static_cast<uint64_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) {
      *value = result;
      return true;
    }
    shift += 7;
  }
  return false;
}

void AppendVarint(uint64_t value, std::string* out) {
  while (value >= 0x80) {
    out->push_back(static_cast<char>((value & 0x7F) | 0x80));
    value >>= 7;
  }
  out->push_back(static_cast<char>(value));
}

void AppendTag(uint32_t field, WireType wire, std::string* out) {
  AppendVarint((static_cast<uint64_t>(field) << 3) | static_cast<uint32_t>(wire), out);
}

void AppendVarintField(uint32_t field, uint64_t value, std::string* out) {
  AppendTag(field, WireType::kVarint, out);
  AppendVarint(value, out);
}

void AppendStringField(uint32_t field, const std::string& value, std::string* out) {
  AppendTag(field, WireType::kLengthDelimited, out);
  AppendVarint(value.size(), out);
  out->append(value);
}

void AppendMessageField(uint32_t field, const std::string& message, std::string* out) {
  AppendStringField(field, message, out);
}

bool SkipField(const std::string& data, size_t* pos, uint32_t field, WireType wire);

bool SkipGroup(const std::string& data, size_t* pos, uint32_t groupField) {
  while (*pos < data.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(data, pos, &tag)) {
      return false;
    }
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (wire == WireType::kEndGroup && field == groupField) {
      return true;
    }
    if (!SkipField(data, pos, field, wire)) {
      return false;
    }
  }
  return false;
}

bool SkipField(const std::string& data, size_t* pos, uint32_t field, WireType wire) {
  uint64_t size = 0;
  switch (wire) {
    case WireType::kVarint:
      return ReadVarint(data, pos, &size);
    case WireType::kFixed64:
      if (*pos + 8 > data.size()) return false;
      *pos += 8;
      return true;
    case WireType::kLengthDelimited:
      if (!ReadVarint(data, pos, &size)) return false;
      if (*pos + size > data.size()) return false;
      *pos += static_cast<size_t>(size);
      return true;
    case WireType::kStartGroup:
      return SkipGroup(data, pos, field);
    case WireType::kEndGroup:
      return true;
    case WireType::kFixed32:
      if (*pos + 4 > data.size()) return false;
      *pos += 4;
      return true;
    default:
      return false;
  }
}

bool ReadLengthDelimited(const std::string& data, size_t* pos, std::string* payload) {
  if (!pos || !payload) return false;
  uint64_t len = 0;
  if (!ReadVarint(data, pos, &len)) return false;
  if (*pos + len > data.size()) return false;
  payload->assign(data.data() + *pos, data.data() + *pos + static_cast<size_t>(len));
  *pos += static_cast<size_t>(len);
  return true;
}

std::optional<RawPreeditSegment> ParsePreeditSegmentMessage(const std::string& data) {
  size_t pos = 0;
  std::unordered_map<uint32_t, std::wstring> stringFields;
  while (pos < data.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(data, &pos, &tag)) return std::nullopt;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (wire == WireType::kLengthDelimited) {
      std::string payload;
      if (!ReadLengthDelimited(data, &pos, &payload)) return std::nullopt;
      std::wstring text = Utf8ToWide(payload);
      if (!text.empty()) {
        stringFields[field] = std::move(text);
      }
      continue;
    }
    if (!SkipField(data, &pos, field, wire)) return std::nullopt;
  }

  RawPreeditSegment seg;
  auto itKey = stringFields.find(1);    // typical: key
  auto itValue = stringFields.find(2);  // typical: value
  if (itKey != stringFields.end()) seg.key = itKey->second;
  if (itValue != stringFields.end()) seg.value = itValue->second;

  if (seg.key.empty()) {
    for (const auto& kv : stringFields) {
      bool kanaLike = true;
      for (wchar_t ch : kv.second) {
        if (!IsKanaChar(ch)) {
          kanaLike = false;
          break;
        }
      }
      if (kanaLike) {
        seg.key = kv.second;
        break;
      }
    }
  }
  if (seg.value.empty()) {
    seg.value = seg.key;
  }
  if (seg.key.empty()) return std::nullopt;
  return seg;
}

bool ParsePreeditSegmentGroup(const std::string& data, size_t* pos, RawPreeditSegment* out) {
  if (!pos || !out) return false;
  RawPreeditSegment seg;
  while (*pos < data.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(data, pos, &tag)) return false;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (wire == WireType::kEndGroup && field == 2) {
      break;
    }
    if ((field == 4 || field == 6) && wire == WireType::kLengthDelimited) {
      std::string payload;
      if (!ReadLengthDelimited(data, pos, &payload)) return false;
      std::wstring text = Utf8ToWide(payload);
      if (field == 4) {
        seg.value = std::move(text);
      } else {
        seg.key = std::move(text);
      }
      continue;
    }
    if (!SkipField(data, pos, field, wire)) return false;
  }
  if (seg.key.empty()) seg.key = seg.value;
  if (seg.value.empty()) seg.value = seg.key;
  if (seg.key.empty()) return false;
  *out = std::move(seg);
  return true;
}

std::vector<RawPreeditSegment> ParsePreeditMessage(const std::string& preeditPayload) {
  std::vector<RawPreeditSegment> out;
  size_t pos = 0;
  while (pos < preeditPayload.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(preeditPayload, &pos, &tag)) break;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    // Preedit.segment is repeated group field=2 in commands.proto.
    if (field == 2 && wire == WireType::kStartGroup) {
      RawPreeditSegment seg;
      if (ParsePreeditSegmentGroup(preeditPayload, &pos, &seg)) {
        out.push_back(std::move(seg));
      }
      continue;
    }
    if (!SkipField(preeditPayload, &pos, field, wire)) break;
  }
  return out;
}

std::vector<RawPreeditSegment> ParsePreeditSegmentsFromOutput(const std::string& output,
                                                              std::string* reason) {
  // Use canonical Output.preedit(field=5) only.
  // Generic nested scans can easily pick unrelated messages and corrupt
  // segmentation.
  std::vector<RawPreeditSegment> segments;
  if (reason) *reason = "no_preedit_segments";

  // Expected exact path from commands.proto:
  // Output.preedit is field=5.
  {
    size_t pos = 0;
    while (pos < output.size()) {
      uint64_t tag = 0;
      if (!ReadVarint(output, &pos, &tag)) break;
      const uint32_t field = static_cast<uint32_t>(tag >> 3);
      const WireType wire = static_cast<WireType>(tag & 0x07);
      if (field == 5 && wire == WireType::kLengthDelimited) {
        std::string preeditPayload;
        if (!ReadLengthDelimited(output, &pos, &preeditPayload)) break;
        auto parsed = ParsePreeditMessage(preeditPayload);
        if (!parsed.empty()) {
          if (reason) *reason = "field5_exact";
          return parsed;
        }
        if (reason) *reason = "field5_empty";
        continue;
      }
      if (!SkipField(output, &pos, field, wire)) break;
    }
  }

  return segments;
}

std::vector<SegmentBoundary> BuildSegmentsFromPreedit(
    const std::vector<RawPreeditSegment>& raw,
    const std::wstring& reading) {
  std::vector<SegmentBoundary> out;
  if (raw.empty() || reading.empty()) return out;
  size_t cursor = 0;
  size_t idx = 0;
  for (const auto& seg : raw) {
    if (seg.key.empty()) continue;
    if (cursor >= reading.size()) break;
    size_t length = seg.key.size();
    if (length == 0) continue;
    length = (std::min)(length, reading.size() - cursor);
    SegmentBoundary s;
    s.index = idx++;
    s.start = cursor;
    s.length = length;
    s.surface = seg.value.empty() ? seg.key : seg.value;
    out.push_back(std::move(s));
    cursor += length;
  }
  if (cursor < reading.size()) {
    SegmentBoundary tail;
    tail.index = idx++;
    tail.start = cursor;
    tail.length = reading.size() - cursor;
    tail.surface = reading.substr(cursor);
    out.push_back(std::move(tail));
  }
  return out;
}

std::string BuildCreateSessionInput() {
  std::string input;
  // Input.type = CREATE_SESSION(1)
  AppendVarintField(1, 1, &input);
  return input;
}

std::string BuildConvertReverseInput(uint64_t sessionId, const std::string& readingUtf8) {
  std::string command;
  // SessionCommand.type = CONVERT_REVERSE(8)
  AppendVarintField(1, 8, &command);
  // SessionCommand.text
  AppendStringField(4, readingUtf8, &command);

  std::string input;
  // Input.type = SEND_COMMAND(5)
  AppendVarintField(1, 5, &input);
  // Input.id
  AppendVarintField(2, sessionId, &input);
  // Input.command
  AppendMessageField(4, command, &input);
  return input;
}

bool ParseSessionIdFromOutput(const std::string& output, uint64_t* sessionId) {
  size_t pos = 0;
  while (pos < output.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(output, &pos, &tag)) {
      return false;
    }
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (field == 1 && wire == WireType::kVarint) {
      return ReadVarint(output, &pos, sessionId);
    }
    if (!SkipField(output, &pos, field, wire)) {
      return false;
    }
  }
  return false;
}

std::wstring ParseCandidateWordValue(const std::string& data) {
  size_t pos = 0;
  std::wstring value;
  std::wstring contentValue;
  while (pos < data.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(data, &pos, &tag)) return {};
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if ((field == 5 || field == 6) && wire == WireType::kLengthDelimited) {
      uint64_t len = 0;
      if (!ReadVarint(data, &pos, &len) || pos + len > data.size()) return {};
      std::string utf8(data.data() + pos, data.data() + pos + static_cast<size_t>(len));
      pos += static_cast<size_t>(len);
      std::wstring w = Utf8ToWide(utf8);
      if (field == 5) {
        value = std::move(w);
      } else {
        contentValue = std::move(w);
      }
      continue;
    }
    if (!SkipField(data, &pos, field, wire)) return {};
  }
  if (!value.empty()) return value;
  return contentValue;
}

bool ParseCandidateSegmentMessage(const std::string& data, CandidateSegmentRecord* out) {
  if (!out) return false;
  size_t pos = 0;
  CandidateSegmentRecord seg;
  std::unordered_set<std::wstring> seen;
  while (pos < data.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(data, &pos, &tag)) return false;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (field == 2 && wire == WireType::kLengthDelimited) {
      uint64_t len = 0;
      if (!ReadVarint(data, &pos, &len) || pos + len > data.size()) return false;
      std::wstring value =
          ParseCandidateWordValue(data.substr(pos, static_cast<size_t>(len)));
      if (!value.empty() && seen.insert(value).second) {
        seg.candidates.push_back(std::move(value));
      }
      pos += static_cast<size_t>(len);
      continue;
    }
    if (field == 3 && wire == WireType::kLengthDelimited) {
      std::string payload;
      if (!ReadLengthDelimited(data, &pos, &payload)) return false;
      seg.key = Utf8ToWide(payload);
      continue;
    }
    if (field == 5 && wire == WireType::kVarint) {
      uint64_t focused = 0;
      if (!ReadVarint(data, &pos, &focused)) return false;
      seg.focusedIndex = static_cast<size_t>(focused);
      continue;
    }
    if (!SkipField(data, &pos, field, wire)) return false;
  }
  if (seg.key.empty() && seg.candidates.empty()) return false;
  *out = std::move(seg);
  return true;
}

std::vector<CandidateSegmentRecord> ParseCandidateSegmentsFromCandidateList(
    const std::string& data) {
  std::vector<CandidateSegmentRecord> out;
  size_t pos = 0;
  while (pos < data.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(data, &pos, &tag)) return {};
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (field == 1 && wire == WireType::kLengthDelimited) {
      std::string payload;
      if (!ReadLengthDelimited(data, &pos, &payload)) return {};
      CandidateSegmentRecord seg;
      if (ParseCandidateSegmentMessage(payload, &seg)) {
        out.push_back(std::move(seg));
      }
      continue;
    }
    if (!SkipField(data, &pos, field, wire)) return {};
  }
  return out;
}

std::vector<CandidateSegmentRecord> ParseCandidateSegmentsFromOutput(
    const std::string& output) {
  size_t pos = 0;
  while (pos < output.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(output, &pos, &tag)) break;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (field == 14 && wire == WireType::kLengthDelimited) {
      std::string payload;
      if (!ReadLengthDelimited(output, &pos, &payload)) break;
      auto segments = ParseCandidateSegmentsFromCandidateList(payload);
      if (!segments.empty()) {
        return segments;
      }
      continue;
    }
    if (!SkipField(output, &pos, field, wire)) break;
  }
  return {};
}

std::wstring PickSegmentFocusedValue(const CandidateSegmentRecord& seg) {
  if (!seg.candidates.empty()) {
    const size_t idx =
        seg.focusedIndex < seg.candidates.size() ? seg.focusedIndex : 0;
    if (!seg.candidates[idx].empty()) {
      return seg.candidates[idx];
    }
  }
  return seg.key;
}

std::vector<SegmentBoundary> BuildSegmentsFromCandidateList(
    const std::vector<CandidateSegmentRecord>& segments,
    const std::wstring& reading) {
  std::vector<SegmentBoundary> out;
  if (segments.empty() || reading.empty()) {
    return out;
  }
  size_t cursor = 0;
  size_t idx = 0;
  for (const auto& seg : segments) {
    if (cursor >= reading.size()) break;
    size_t length = seg.key.size();
    if (length == 0) continue;
    length = (std::min)(length, reading.size() - cursor);
    SegmentBoundary item;
    item.index = idx++;
    item.start = cursor;
    item.length = length;
    item.surface = PickSegmentFocusedValue(seg);
    if (item.surface.empty()) {
      item.surface = reading.substr(cursor, length);
    }
    out.push_back(std::move(item));
    cursor += length;
  }
  if (cursor < reading.size()) {
    SegmentBoundary tail;
    tail.index = idx++;
    tail.start = cursor;
    tail.length = reading.size() - cursor;
    tail.surface = reading.substr(cursor);
    out.push_back(std::move(tail));
  }
  return out;
}

std::vector<std::wstring> ExtractPrimarySegmentCandidates(
    const std::vector<CandidateSegmentRecord>& segments) {
  std::vector<std::wstring> out;
  if (segments.empty()) return out;
  const auto& first = segments.front();
  std::unordered_set<std::wstring> seen;
  if (!first.candidates.empty()) {
    const size_t focus =
        first.focusedIndex < first.candidates.size() ? first.focusedIndex : 0;
    const std::wstring focused = first.candidates[focus];
    if (!focused.empty() && seen.insert(focused).second) {
      out.push_back(focused);
    }
    for (const auto& cand : first.candidates) {
      if (!cand.empty() && seen.insert(cand).second) {
        out.push_back(cand);
      }
    }
  }
  if (out.empty() && !first.key.empty()) {
    out.push_back(first.key);
  }
  return out;
}

void ParseCandidateGroup(const std::string& data,
                         size_t* pos,
                         std::vector<std::wstring>* out,
                         std::unordered_set<std::wstring>* seen) {
  if (!pos || !out || !seen) return;
  std::wstring value;
  while (*pos < data.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(data, pos, &tag)) return;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (field == 3 && wire == WireType::kEndGroup) {
      if (!value.empty() && seen->insert(value).second) {
        out->push_back(std::move(value));
      }
      return;
    }
    if (field == 5 && wire == WireType::kLengthDelimited) {
      uint64_t len = 0;
      if (!ReadVarint(data, pos, &len) || *pos + len > data.size()) return;
      value = Utf8ToWide(std::string(
          data.data() + *pos, data.data() + *pos + static_cast<size_t>(len)));
      *pos += static_cast<size_t>(len);
      continue;
    }
    if (!SkipField(data, pos, field, wire)) return;
  }
}

void ParseCandidateWindow(const std::string& data,
                          std::vector<std::wstring>* out,
                          std::unordered_set<std::wstring>* seen) {
  if (!out || !seen) return;
  size_t pos = 0;
  while (pos < data.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(data, &pos, &tag)) return;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (field == 3 && wire == WireType::kStartGroup) {
      ParseCandidateGroup(data, &pos, out, seen);
      continue;
    }
    if (field == 8 && wire == WireType::kLengthDelimited) {
      uint64_t len = 0;
      if (!ReadVarint(data, &pos, &len) || pos + len > data.size()) return;
      ParseCandidateWindow(data.substr(pos, static_cast<size_t>(len)), out, seen);
      pos += static_cast<size_t>(len);
      continue;
    }
    if (!SkipField(data, &pos, field, wire)) return;
  }
}

std::vector<std::wstring> ParseCandidatesFromOutput(const std::string& output) {
  std::vector<std::wstring> ordered;
  std::unordered_set<std::wstring> seen;
  size_t pos = 0;
  while (pos < output.size()) {
    uint64_t tag = 0;
    if (!ReadVarint(output, &pos, &tag)) break;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const WireType wire = static_cast<WireType>(tag & 0x07);
    if (field == 6 && wire == WireType::kLengthDelimited) {
      uint64_t len = 0;
      if (!ReadVarint(output, &pos, &len) || pos + len > output.size()) break;
      const std::string payload = output.substr(pos, static_cast<size_t>(len));
      pos += static_cast<size_t>(len);
      ParseCandidateWindow(payload, &ordered, &seen);
      continue;
    }
    if (!SkipField(output, &pos, field, wire)) break;
  }
  return ordered;
}

bool ReadPipeMessage(HANDLE pipe, std::string* response) {
  if (!response) return false;
  response->clear();
  std::vector<char> chunk(4096);
  for (;;) {
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &bytesRead, nullptr);
    if (ok) {
      if (!bytesRead) return true;
      response->append(chunk.data(), chunk.data() + bytesRead);
      return true;
    }
    DWORD err = GetLastError();
    if (err == ERROR_MORE_DATA) {
      response->append(chunk.data(), chunk.data() + bytesRead);
      continue;
    }
    return false;
  }
}

bool TransactSessionPipe(const std::wstring& pipeName,
                         const std::string& request,
                         std::string* response) {
  if (!WaitNamedPipeW(pipeName.c_str(), 500)) {
    return false;
  }
  HANDLE pipe = CreateFileW(
      pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

  DWORD written = 0;
  const BOOL writeOk = WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &written, nullptr);
  if (!writeOk || written != request.size()) {
    CloseHandle(pipe);
    return false;
  }

  const bool readOk = ReadPipeMessage(pipe, response);
  CloseHandle(pipe);  // disconnect also works as ACK for modern server.
  return readOk;
}

std::vector<std::wstring> QueryGoogleSessionCandidates(
    const std::wstring& reading,
    std::vector<SegmentBoundary>* segmentsOut,
    bool* usedMozcSegments,
    std::string* preeditReason) {
  std::vector<std::wstring> out;
  auto collect_pipes = []() {
    std::vector<std::wstring> pipes;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(kSessionPipePattern, &fd);
    if (find == INVALID_HANDLE_VALUE) {
      return pipes;
    }
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        pipes.push_back(std::wstring(L"\\\\.\\pipe\\") + fd.cFileName);
      }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return pipes;
  };

  auto launch_google_converter = []() {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + std::wstring(kGoogleImeConverterPath) + L"\"";
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
      return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
  };

  std::vector<std::wstring> pipes = collect_pipes();
  if (pipes.empty()) {
    if (launch_google_converter()) {
      // Give converter some time to create session pipe.
      for (int i = 0; i < 10; ++i) {
        Sleep(50);
        pipes = collect_pipes();
        if (!pipes.empty()) {
          break;
        }
      }
    }
  }

  DebugLog("session pipes found: " + std::to_string(pipes.size()));
  if (pipes.empty()) {
    return out;
  }

  const std::string readingUtf8 = WideToUtf8(reading);
  if (readingUtf8.empty()) return out;

  for (const auto& pipe : pipes) {
    DebugLog("trying pipe: " + WideToUtf8(pipe));
    std::string createResponse;
    if (!TransactSessionPipe(pipe, BuildCreateSessionInput(), &createResponse)) {
      DebugLog("create_session transact failed");
      continue;
    }
    DebugLog("create_session response bytes: " + std::to_string(createResponse.size()));
    uint64_t sessionId = 0;
    if (!ParseSessionIdFromOutput(createResponse, &sessionId) || sessionId == 0) {
      DebugLog("failed to parse session id");
      continue;
    }
    DebugLog("session id: " + std::to_string(sessionId));
    std::string convertResponse;
    if (!TransactSessionPipe(
            pipe, BuildConvertReverseInput(sessionId, readingUtf8), &convertResponse)) {
      DebugLog("convert_reverse transact failed");
      continue;
    }
    DebugLog("convert_reverse response bytes: " + std::to_string(convertResponse.size()));
    auto candidateSegments = ParseCandidateSegmentsFromOutput(convertResponse);
    if (segmentsOut) {
      std::string reason;
      *segmentsOut = BuildSegmentsFromCandidateList(candidateSegments, reading);
      if (!segmentsOut->empty()) {
        reason = "candidate_list_field14";
      } else {
        auto raw = ParsePreeditSegmentsFromOutput(convertResponse, &reason);
        *segmentsOut = BuildSegmentsFromPreedit(raw, reading);
        if (segmentsOut->empty()) {
          reason = "preedit_empty";
        }
      }
      if (usedMozcSegments) {
        *usedMozcSegments = !segmentsOut->empty();
      }
      if (preeditReason) {
        *preeditReason = reason;
      }
    }
    out = ExtractPrimarySegmentCandidates(candidateSegments);
    if (out.empty()) {
      out = ParseCandidatesFromOutput(convertResponse);
    }
    DebugLog("parsed candidates: " + std::to_string(out.size()));
    if (!out.empty()) {
      return out;
    }
  }
  return out;
}

ATOM EnsureWindowClass(HINSTANCE instance) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = instance;
  wc.lpszClassName = kBridgeWindowClass;
  ATOM atom = RegisterClassW(&wc);
  if (!atom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    return 1;
  }
  return atom;
}

std::vector<std::wstring> QueryCandidatesWithHkl(HKL hkl, const std::wstring& reading) {
  std::vector<std::wstring> out;
  if (!hkl || reading.empty()) {
    return out;
  }

  HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!instance || !EnsureWindowClass(instance)) {
    return out;
  }

  HWND hwnd = CreateWindowExW(
      0, kBridgeWindowClass, L"RTAS Mozc Bridge",
      WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
  if (!hwnd) {
    return out;
  }

  HKL prevHkl = ActivateKeyboardLayout(hkl, 0);
  HIMC himc = ImmCreateContext();
  if (!himc) {
    DestroyWindow(hwnd);
    if (prevHkl) {
      ActivateKeyboardLayout(prevHkl, 0);
    }
    return out;
  }
  HIMC oldHimc = ImmAssociateContext(hwnd, himc);
  ImmSetOpenStatus(himc, TRUE);
  ImmSetConversionStatus(himc, IME_CMODE_NATIVE, IME_SMODE_NONE);

  auto append_from_list = [&out](const BYTE* raw, DWORD size) {
    if (!raw || size == 0) {
      return;
    }
    auto* list = reinterpret_cast<const CANDIDATELIST*>(raw);
    if (!list || !list->dwCount) {
      return;
    }
    for (DWORD i = 0; i < list->dwCount; ++i) {
      DWORD offset = list->dwOffset[i];
      if (offset >= size) {
        continue;
      }
      const wchar_t* cand =
          reinterpret_cast<const wchar_t*>(raw + offset);
      if (!cand || !*cand) {
        continue;
      }
      std::wstring candidate = cand;
      if (std::find(out.begin(), out.end(), candidate) == out.end()) {
        out.push_back(std::move(candidate));
      }
    }
  };

  DWORD required =
      ImmGetConversionListW(hkl, himc, reading.c_str(), nullptr, 0, GCL_CONVERSION);
  if (required) {
    std::vector<BYTE> buffer(required);
    DWORD ret = ImmGetConversionListW(
        hkl, himc, reading.c_str(),
        reinterpret_cast<LPCANDIDATELIST>(buffer.data()),
        required, GCL_CONVERSION);
    if (ret && ret <= required) {
      append_from_list(buffer.data(), ret);
    }
  }

  if (out.empty()) {
    ImmSetCompositionStringW(
        himc, SCS_SETSTR, const_cast<wchar_t*>(reading.c_str()),
        static_cast<DWORD>((reading.size() + 1) * sizeof(wchar_t)),
        nullptr, 0);
    DWORD bytes = ImmGetCandidateListW(himc, 0, nullptr, 0);
    if (bytes) {
      std::vector<BYTE> buffer(bytes);
      DWORD ret = ImmGetCandidateListW(
          himc, 0, reinterpret_cast<LPCANDIDATELIST>(buffer.data()), bytes);
      if (ret && ret <= bytes) {
        append_from_list(buffer.data(), ret);
      }
    }
    ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
  }

  if (out.empty()) {
    DWORD listCount = ImmGetCandidateListCountW(himc, nullptr);
    for (DWORD index = 0; index < listCount; ++index) {
      DWORD bytes = ImmGetCandidateListW(himc, index, nullptr, 0);
      if (!bytes) {
        continue;
      }
      std::vector<BYTE> buffer(bytes);
      DWORD ret = ImmGetCandidateListW(
          himc, index, reinterpret_cast<LPCANDIDATELIST>(buffer.data()), bytes);
      if (ret && ret <= bytes) {
        append_from_list(buffer.data(), ret);
      }
      if (!out.empty()) {
        break;
      }
    }
  }

  if (out.empty()) {
    DWORD bytes = ImmGetConversionListW(hkl, himc, reading.c_str(), nullptr, 0, GCL_REVERSECONVERSION);
    if (bytes) {
      std::vector<BYTE> buffer(bytes);
      DWORD ret = ImmGetConversionListW(
          hkl, himc, reading.c_str(),
          reinterpret_cast<LPCANDIDATELIST>(buffer.data()),
          bytes, GCL_REVERSECONVERSION);
      if (ret && ret <= bytes) {
        append_from_list(buffer.data(), ret);
      }
    }
  }

  if (out.empty()) {
    std::wstring hira = reading;
    for (auto& ch : hira) {
      if (ch >= 0x30A1 && ch <= 0x30F6) {
        ch = static_cast<wchar_t>(ch - 0x60);
      }
    }
    if (hira != reading) {
      DWORD bytes = ImmGetConversionListW(hkl, himc, hira.c_str(), nullptr, 0, GCL_CONVERSION);
      if (bytes) {
        std::vector<BYTE> buffer(bytes);
        DWORD ret = ImmGetConversionListW(
            hkl, himc, hira.c_str(),
            reinterpret_cast<LPCANDIDATELIST>(buffer.data()),
            bytes, GCL_CONVERSION);
        if (ret && ret <= bytes) {
          append_from_list(buffer.data(), ret);
        }
      }
    }
  }

  ImmAssociateContext(hwnd, oldHimc);
  ImmDestroyContext(himc);
  DestroyWindow(hwnd);
  if (prevHkl) {
    ActivateKeyboardLayout(prevHkl, 0);
  }
  return out;
}

std::vector<std::wstring> QueryCandidates(
    const std::wstring& reading,
    std::vector<SegmentBoundary>* segmentsOut,
    bool* usedMozcSegments,
    std::string* preeditReason) {
  std::vector<std::wstring> out;
  if (reading.empty()) {
    return out;
  }

  if (segmentsOut) segmentsOut->clear();
  if (usedMozcSegments) *usedMozcSegments = false;
  if (preeditReason) *preeditReason = "not_attempted";
  out = QueryGoogleSessionCandidates(reading, segmentsOut, usedMozcSegments, preeditReason);
  if (!out.empty()) {
    return out;
  }

  struct CandidateLayout {
    const wchar_t* klid;
    bool unload = false;
    HKL hkl = nullptr;
  };

  std::vector<CandidateLayout> layouts;
  layouts.push_back({nullptr, false, GetKeyboardLayout(0)});
  layouts.push_back({L"E0010411", true, nullptr});   // Common Google Japanese Input TIP KLID.
  layouts.push_back({L"E0200411", true, nullptr});   // Microsoft IME TIP KLID.
  layouts.push_back({L"00000411", true, nullptr});   // Legacy JP layout fallback.

  for (auto& layout : layouts) {
    if (layout.klid) {
      layout.hkl = LoadKeyboardLayoutW(layout.klid, KLF_NOTELLSHELL | KLF_SUBSTITUTE_OK);
    }
    if (!layout.hkl) {
      continue;
    }
    auto cands = QueryCandidatesWithHkl(layout.hkl, reading);
    for (auto& cand : cands) {
      if (std::find(out.begin(), out.end(), cand) == out.end()) {
        out.push_back(std::move(cand));
      }
    }
    if (layout.unload) {
      UnloadKeyboardLayout(layout.hkl);
    }
    if (!out.empty()) {
      break;
    }
  }

  return out;
}

}  // namespace

#ifdef RTAS_MOZC_BRIDGE_LIBRARY
namespace rtas::mozc_bridge {

BridgeResponse QueryCandidatesInProcess(const std::wstring& input) {
  BridgeResponse response;
  const std::wstring reading = TrimWhitespace(input);
  if (reading.empty()) {
    response.error = L"empty input";
    return response;
  }

  std::vector<SegmentBoundary> segments;
  bool usedMozcSegments = false;
  std::string preeditReason;
  auto candidates =
      QueryCandidates(reading, &segments, &usedMozcSegments, &preeditReason);
  if (candidates.empty()) {
    response.error = L"no candidates";
    return response;
  }

  if (segments.empty()) {
    usedMozcSegments = false;
    if (preeditReason.empty() || preeditReason == "not_attempted") {
      preeditReason = "no_segments";
    }
  }
  if (usedMozcSegments && !segments.empty()) {
    response.segment_source =
        preeditReason.rfind("candidate_list_", 0) == 0 ? "candidate_list"
                                                       : "preedit";
  } else {
    response.segment_source = "none";
  }
  response.segment_reason = std::move(preeditReason);

  response.segments.reserve(segments.size());
  for (const auto& segment : segments) {
    BridgeSegment out;
    out.index = segment.index;
    out.start = segment.start;
    out.length = segment.length;
    out.surface = segment.surface;
    response.segments.push_back(std::move(out));
  }
  response.candidates =
      ComposeSentenceCandidatesFromSegments(candidates, segments);
  return response;
}

}  // namespace rtas::mozc_bridge
#else
int main() {
  std::ios::sync_with_stdio(false);

  std::string line;
  if (!std::getline(std::cin, line)) {
    std::cout << "ERROR\tfailed to read input" << std::endl;
    return 1;
  }

  std::wstring reading = TrimWhitespace(Utf8ToWide(line));
  if (reading.empty()) {
    std::cout << "ERROR\tempty input" << std::endl;
    return 1;
  }

  std::vector<SegmentBoundary> segments;
  bool usedMozcSegments = false;
  std::string preeditReason;
  auto cands = QueryCandidates(reading, &segments, &usedMozcSegments, &preeditReason);
  if (cands.empty()) {
    std::cout << "ERROR\tno candidates" << std::endl;
    return 0;
  }

  if (segments.empty()) {
    usedMozcSegments = false;
    if (preeditReason.empty() || preeditReason == "not_attempted") {
      preeditReason = "no_segments";
    }
  }
  std::string segmentSource = "none";
  if (usedMozcSegments && !segments.empty()) {
    if (preeditReason.rfind("candidate_list_", 0) == 0) {
      segmentSource = "candidate_list";
    } else {
      segmentSource = "preedit";
    }
  }
  std::cout << "DBG\tSEG_SOURCE\t" << segmentSource << '\n';
  if (!preeditReason.empty()) {
    std::cout << "DBG\tSEG_REASON\t" << preeditReason << '\n';
  }
  for (const auto& seg : segments) {
    std::cout << "SEG\t"
              << seg.index << '\t'
              << seg.start << '\t'
              << seg.length << '\t';
    const std::string surfaceUtf8 = WideToUtf8(seg.surface);
    std::cout << surfaceUtf8 << '\n';
  }

  cands = ComposeSentenceCandidatesFromSegments(cands, segments);

  for (const auto& cand : cands) {
    const std::string utf8 = WideToUtf8(cand);
    if (!utf8.empty()) {
      std::cout << utf8 << '\n';
    }
  }
  return 0;
}
#endif
