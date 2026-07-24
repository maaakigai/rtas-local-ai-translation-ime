#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../config/provider_settings.h"

namespace ime::conversion {

struct MozcCandidateRequest {
  std::wstring reading;
};

struct MozcSegmentInfo {
  size_t index = 0;
  size_t start = 0;
  size_t length = 0;
  std::wstring surface;
};

struct MozcCandidateResponse {
  std::vector<std::wstring> candidates;
  std::vector<MozcSegmentInfo> segments;
  std::wstring error;
};

class IMozcTransport {
public:
  virtual ~IMozcTransport() = default;
  virtual bool Initialize(std::wstring* error) = 0;
  virtual MozcCandidateResponse FetchCandidates(
      const MozcCandidateRequest& request) = 0;
};

std::unique_ptr<IMozcTransport> CreateMozcTransport(
    const ime::config::MozcSettings& settings);

// Stage-1 transport:
// Fetch conversion candidates via IMM32 against the currently active JP IME.
// This allows RTAS to run over Mozc today and keeps a clean seam for future
// in-process Mozc converter integration.
class MozcImm32Transport final : public IMozcTransport {
public:
  explicit MozcImm32Transport(const ime::config::MozcSettings& settings);
  bool Initialize(std::wstring* error) override;
  MozcCandidateResponse FetchCandidates(
      const MozcCandidateRequest& request) override;

private:
  static std::vector<std::wstring> QueryImmCandidates(
      const std::wstring& reading);
  ime::config::MozcSettings settings_;
};

// Stage-2 transport:
// Runs the Google Japanese Input session-pipe bridge in process. The standalone
// mozc_bridge.exe remains available for diagnostics, but normal conversion does
// not depend on launching an unsigned child executable.
class MozcBridgeTransport final : public IMozcTransport {
public:
  explicit MozcBridgeTransport(const ime::config::MozcSettings& settings);
  bool Initialize(std::wstring* error) override;
  MozcCandidateResponse FetchCandidates(
      const MozcCandidateRequest& request) override;

private:
  ime::config::MozcSettings settings_;
};

// Opt-in OSS Mozc server/client transport:
// Initializes only when explicit wrapper and server artifacts exist. Missing
// artifacts and runtime failures are reported without a silent fallback.
class MozcNativeServerClientTransport final : public IMozcTransport {
public:
  explicit MozcNativeServerClientTransport(
      const ime::config::MozcSettings& settings);
  bool Initialize(std::wstring* error) override;
  MozcCandidateResponse FetchCandidates(
      const MozcCandidateRequest& request) override;

private:
  ime::config::MozcSettings settings_;
};

}  // namespace ime::conversion
