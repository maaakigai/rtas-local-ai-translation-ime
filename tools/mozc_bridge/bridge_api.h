#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rtas::mozc_bridge {

struct BridgeSegment {
  size_t index = 0;
  size_t start = 0;
  size_t length = 0;
  std::wstring surface;
};

struct BridgeResponse {
  std::vector<std::wstring> candidates;
  std::vector<BridgeSegment> segments;
  std::wstring error;
  std::string segment_source;
  std::string segment_reason;
};

// Runs the Google Japanese Input session-pipe/IMM32 bridge in the current
// process. This avoids launching an unsigned helper executable on systems
// where Smart App Control blocks unknown child processes.
BridgeResponse QueryCandidatesInProcess(const std::wstring& reading);

}  // namespace rtas::mozc_bridge
