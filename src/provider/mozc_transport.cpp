#include "mozc_transport.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../../third_party/nlohmann/json.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>

#include "../../tools/mozc_bridge/bridge_api.h"

#pragma comment(lib, "Imm32.lib")

namespace ime::conversion {

namespace {

bool IsBridgeTransport(ime::config::MozcTransport transport) {
  return transport == ime::config::MozcTransport::kBridge;
}

bool IsMozcServerClientNativeBackend(
    ime::config::MozcNativeBackend backend) {
  return backend == ime::config::MozcNativeBackend::kMozcServerClient ||
         backend == ime::config::MozcNativeBackend::kUnset;
}

std::wstring Utf8ToWide(const std::string& input) {
  if (input.empty()) {
    return {};
  }
  int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       input.data(),
                                       static_cast<int>(input.size()),
                                       nullptr, 0);
  if (required <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(required), L'\0');
  ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                        static_cast<int>(input.size()), wide.data(), required);
  return wide;
}

std::string WideToUtf8(const std::wstring& input) {
  if (input.empty()) {
    return {};
  }
  int required = ::WideCharToMultiByte(CP_UTF8, 0, input.data(),
                                       static_cast<int>(input.size()),
                                       nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    return {};
  }
  std::string utf8(static_cast<size_t>(required), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, input.data(),
                        static_cast<int>(input.size()), utf8.data(), required,
                        nullptr, nullptr);
  return utf8;
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

std::wstring LastErrorMessage(const wchar_t* prefix, DWORD errorCode) {
  wchar_t* buffer = nullptr;
  DWORD size = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, errorCode, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::wstring message(prefix ? prefix : L"");
  if (size && buffer) {
    if (!message.empty()) {
      message.append(L": ");
    }
    message.append(buffer, buffer + size);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n')) {
      message.pop_back();
    }
  } else if (!message.empty()) {
    message.append(L": unknown error");
  }
  if (buffer) {
    ::LocalFree(buffer);
  }
  return message;
}

bool ReadAllFromHandle(HANDLE handle, std::string* out) {
  if (!out) {
    return false;
  }
  out->clear();
  std::array<char, 4096> buffer{};
  for (;;) {
    DWORD bytesRead = 0;
    if (!::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &bytesRead, nullptr)) {
      DWORD error = ::GetLastError();
      if (error == ERROR_BROKEN_PIPE) {
        return true;
      }
      return false;
    }
    if (!bytesRead) {
      return true;
    }
    out->append(buffer.data(), buffer.data() + bytesRead);
  }
}

std::wstring QuoteCommandLineArgument(const std::wstring& arg) {
  std::wstring quoted = L"\"";
  size_t backslashes = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++backslashes;
    } else if (ch == L'"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(ch);
      backslashes = 0;
    } else {
      quoted.append(backslashes, L'\\');
      backslashes = 0;
      quoted.push_back(ch);
    }
  }
  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

std::filesystem::path CreateTemporaryUtf8ReadingFile(
    const std::wstring& reading,
    std::wstring* error) {
  wchar_t tempPathBuffer[MAX_PATH + 1]{};
  DWORD tempPathLength = ::GetTempPathW(MAX_PATH, tempPathBuffer);
  if (tempPathLength == 0 || tempPathLength > MAX_PATH) {
    if (error) {
      *error = LastErrorMessage(L"GetTempPath failed", ::GetLastError());
    }
    return {};
  }

  wchar_t tempFileBuffer[MAX_PATH + 1]{};
  if (!::GetTempFileNameW(tempPathBuffer, L"rts", 0, tempFileBuffer)) {
    if (error) {
      *error = LastErrorMessage(L"GetTempFileName failed", ::GetLastError());
    }
    return {};
  }

  const std::filesystem::path tempFile(tempFileBuffer);
  std::ofstream file(tempFile, std::ios::binary | std::ios::trunc);
  if (!file) {
    if (error) {
      *error = L"Failed to open temporary Mozc reading file: " +
               tempFile.wstring();
    }
    ::DeleteFileW(tempFile.c_str());
    return {};
  }
  const std::string utf8 = WideToUtf8(reading);
  file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
  file.close();
  if (!file) {
    if (error) {
      *error = L"Failed to write temporary Mozc reading file: " +
               tempFile.wstring();
    }
    ::DeleteFileW(tempFile.c_str());
    return {};
  }
  return tempFile;
}

std::optional<nlohmann::json> ParseJsonObjectFromOutput(
    const std::string& outputUtf8,
    std::wstring* error) {
  const size_t begin = outputUtf8.find('{');
  const size_t end = outputUtf8.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end < begin) {
    if (error) {
      *error = L"Mozc native wrapper did not return a JSON object.";
    }
    return std::nullopt;
  }
  try {
    return nlohmann::json::parse(outputUtf8.substr(begin, end - begin + 1));
  } catch (const std::exception& ex) {
    if (error) {
      *error = L"Failed to parse Mozc native wrapper JSON: " + Utf8ToWide(ex.what());
    }
    return std::nullopt;
  }
}

std::vector<std::wstring> ReadStringArrayField(
    const nlohmann::json& object,
    const char* fieldName) {
  std::vector<std::wstring> values;
  const auto it = object.find(fieldName);
  if (it == object.end() || !it->is_array()) {
    return values;
  }
  for (const auto& node : *it) {
    if (node.is_string()) {
      values.push_back(Utf8ToWide(node.get<std::string>()));
    }
  }
  return values;
}

std::vector<MozcSegmentInfo> BuildSequentialSegments(
    const std::vector<std::wstring>& surfaces) {
  std::vector<MozcSegmentInfo> segments;
  segments.reserve(surfaces.size());
  size_t start = 0;
  for (size_t i = 0; i < surfaces.size(); ++i) {
    MozcSegmentInfo segment;
    segment.index = i;
    segment.start = start;
    segment.length = surfaces[i].size();
    segment.surface = surfaces[i];
    start += segment.length;
    segments.push_back(std::move(segment));
  }
  return segments;
}

std::wstring DescribeNativeServerClientUnavailable(
    const ime::config::MozcSettings& settings) {
  std::wstringstream message;
  message << L"Mozc native backend unavailable: transport=native, "
          << L"native.backend=mozc_server_client requires an app-local OSS "
          << L"Mozc wrapper and server artifact.";
  if (settings.native.wrapper_exe.empty()) {
    message << L" provider.kana.mozc.native.wrapper_exe is empty.";
  } else {
    message << L" wrapper_exe=" << settings.native.wrapper_exe.wstring()
            << L".";
  }
  if (settings.native.server_exe.empty()) {
    message << L" provider.kana.mozc.native.server_exe is empty.";
  } else {
    message << L" server_exe=" << settings.native.server_exe.wstring()
            << L".";
  }
  if (!settings.native.mozc_build_artifact.empty()) {
    message << L" mozc_build_artifact="
            << settings.native.mozc_build_artifact.wstring() << L".";
  }
  message << L" fallback_policy="
          << Utf8ToWide(settings.native.fallback_policy_value)
          << L"; no fallback was used.";
  return message.str();
}

}  // namespace

std::unique_ptr<IMozcTransport> CreateMozcTransport(
    const ime::config::MozcSettings& settings) {
  if (IsBridgeTransport(settings.transport)) {
    return std::make_unique<MozcBridgeTransport>(settings);
  }
  if (settings.transport == ime::config::MozcTransport::kImm32) {
    return std::make_unique<MozcImm32Transport>(settings);
  }
  if (settings.transport == ime::config::MozcTransport::kNative &&
      IsMozcServerClientNativeBackend(settings.native.backend)) {
    return std::make_unique<MozcNativeServerClientTransport>(settings);
  }
  return nullptr;
}

MozcImm32Transport::MozcImm32Transport(
    const ime::config::MozcSettings& settings)
    : settings_(settings) {}

bool MozcImm32Transport::Initialize(std::wstring* error) {
  if (error) {
    error->clear();
  }
  if (settings_.transport != ime::config::MozcTransport::kImm32) {
    if (error) {
      *error = L"Unsupported mozc transport. Supported: imm32";
    }
    return false;
  }
  return true;
}

MozcCandidateResponse MozcImm32Transport::FetchCandidates(
    const MozcCandidateRequest& request) {
  MozcCandidateResponse response;
  if (request.reading.empty()) {
    return response;
  }
  response.candidates = QueryImmCandidates(request.reading);
  if (response.candidates.empty()) {
    response.error = L"No candidates returned from active IME.";
  }
  return response;
}

std::vector<std::wstring> MozcImm32Transport::QueryImmCandidates(
    const std::wstring& reading) {
  std::vector<std::wstring> out;
  if (reading.empty()) {
    return out;
  }

  HKL hkl = GetKeyboardLayout(0);
  bool ownsHkl = false;
  LANGID lang = LOWORD(reinterpret_cast<ULONG_PTR>(hkl));
  if (!hkl || PRIMARYLANGID(lang) != LANG_JAPANESE) {
    hkl = LoadKeyboardLayoutW(L"00000411", KLF_NOTELLSHELL | KLF_SUBSTITUTE_OK);
    ownsHkl = (hkl != nullptr);
  }
  if (!hkl) {
    return out;
  }

  HIMC himc = ImmCreateContext();
  if (!himc) {
    if (ownsHkl) {
      UnloadKeyboardLayout(hkl);
    }
    return out;
  }

  DWORD required =
      ImmGetConversionListW(hkl, himc, reading.c_str(), nullptr, 0, GCL_CONVERSION);
  if (required) {
    std::vector<BYTE> buffer(required);
    auto* list = reinterpret_cast<LPCANDIDATELIST>(buffer.data());
    DWORD ret =
        ImmGetConversionListW(hkl, himc, reading.c_str(), list, required, GCL_CONVERSION);
    if (ret && ret <= required && list && list->dwCount) {
      for (DWORD i = 0; i < list->dwCount; ++i) {
        DWORD offset = list->dwOffset[i];
        if (offset >= required) {
          continue;
        }
        const wchar_t* cand =
            reinterpret_cast<const wchar_t*>(buffer.data() + offset);
        if (!cand || !*cand) {
          continue;
        }
        std::wstring candidate = cand;
        if (std::find(out.begin(), out.end(), candidate) == out.end()) {
          out.emplace_back(std::move(candidate));
        }
      }
    }
  }

  ImmDestroyContext(himc);
  if (ownsHkl) {
    UnloadKeyboardLayout(hkl);
  }

  return out;
}

MozcBridgeTransport::MozcBridgeTransport(
    const ime::config::MozcSettings& settings)
    : settings_(settings) {}

bool MozcBridgeTransport::Initialize(std::wstring* error) {
  if (error) {
    error->clear();
  }
  if (!IsBridgeTransport(settings_.transport)) {
    if (error) {
      *error = L"Unsupported mozc transport. Supported: bridge/server";
    }
    return false;
  }
  return true;
}

MozcCandidateResponse MozcBridgeTransport::FetchCandidates(
    const MozcCandidateRequest& request) {
  MozcCandidateResponse response;
  if (request.reading.empty()) {
    return response;
  }

  auto bridge = rtas::mozc_bridge::QueryCandidatesInProcess(request.reading);
  response.candidates = std::move(bridge.candidates);
  response.error = std::move(bridge.error);
  response.segments.reserve(bridge.segments.size());
  for (auto& segment : bridge.segments) {
    MozcSegmentInfo out;
    out.index = segment.index;
    out.start = segment.start;
    out.length = segment.length;
    out.surface = std::move(segment.surface);
    response.segments.push_back(std::move(out));
  }
  if (response.candidates.empty() && response.error.empty()) {
    response.error = L"No candidates returned from in-process Mozc bridge.";
  }

  return response;
}

MozcNativeServerClientTransport::MozcNativeServerClientTransport(
    const ime::config::MozcSettings& settings)
    : settings_(settings) {}

bool MozcNativeServerClientTransport::Initialize(std::wstring* error) {
  if (error) {
    error->clear();
  }
  if (settings_.transport != ime::config::MozcTransport::kNative) {
    if (error) {
      *error = L"Unsupported mozc transport. Supported: native";
    }
    return false;
  }
  if (!IsMozcServerClientNativeBackend(settings_.native.backend)) {
    if (error) {
      *error =
          L"Unsupported native Mozc backend for this spike. Supported: "
          L"mozc_server_client";
    }
    return false;
  }
  if (settings_.native.fallback_policy !=
      ime::config::MozcNativeFallbackPolicy::kNone) {
    if (error) {
      *error =
          L"Mozc native fallback is recognized but disabled for the app-local "
          L"runtime spike; "
          L"set provider.kana.mozc.native.fallback_policy to 'none'.";
    }
    return false;
  }
  if (settings_.native.wrapper_exe.empty()) {
    if (error) {
      *error = DescribeNativeServerClientUnavailable(settings_);
    }
    return false;
  }
  if (!std::filesystem::exists(settings_.native.wrapper_exe)) {
    if (error) {
      *error = L"Mozc native wrapper executable not found: " +
               settings_.native.wrapper_exe.wstring() + L"; no fallback was used.";
    }
    return false;
  }
  if (settings_.native.server_exe.empty()) {
    if (error) {
      *error = DescribeNativeServerClientUnavailable(settings_);
    }
    return false;
  }
  if (!std::filesystem::exists(settings_.native.server_exe)) {
    if (error) {
      *error = L"Mozc native server executable not found: " +
               settings_.native.server_exe.wstring() + L"; no fallback was used.";
    }
    return false;
  }
  if (settings_.native.timeout_ms <= 0) {
    if (error) {
      *error = L"Mozc native timeout_ms must be greater than zero.";
    }
    return false;
  }
  if (settings_.native.top_n <= 0) {
    if (error) {
      *error = L"Mozc native top_n must be greater than zero.";
    }
    return false;
  }
  return true;
}

MozcCandidateResponse MozcNativeServerClientTransport::FetchCandidates(
    const MozcCandidateRequest& request) {
  MozcCandidateResponse response;
  if (request.reading.empty()) {
    return response;
  }

  std::wstring tempError;
  const std::filesystem::path readingFile =
      CreateTemporaryUtf8ReadingFile(request.reading, &tempError);
  if (readingFile.empty()) {
    response.error = tempError.empty()
                         ? L"Failed to create Mozc native reading file."
                         : tempError;
    return response;
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE childStdoutRead = nullptr;
  HANDLE childStdoutWrite = nullptr;
  if (!::CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0)) {
    response.error = LastErrorMessage(L"CreatePipe(stdout) failed", ::GetLastError());
    ::DeleteFileW(readingFile.c_str());
    return response;
  }
  if (!::SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
    response.error =
        LastErrorMessage(L"SetHandleInformation(stdout) failed", ::GetLastError());
    ::CloseHandle(childStdoutRead);
    ::CloseHandle(childStdoutWrite);
    ::DeleteFileW(readingFile.c_str());
    return response;
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nullptr;
  si.hStdOutput = childStdoutWrite;
  si.hStdError = childStdoutWrite;

  PROCESS_INFORMATION pi{};
  std::wstring commandLine =
      QuoteCommandLineArgument(settings_.native.wrapper_exe.wstring()) +
      L" --server_path=" +
      QuoteCommandLineArgument(settings_.native.server_exe.wstring()) +
      L" --reading_file=" + QuoteCommandLineArgument(readingFile.wstring()) +
      L" --top_n=" + std::to_wstring(settings_.native.top_n) +
      L" --timeout_ms=" + std::to_wstring(settings_.native.timeout_ms);

  BOOL created = ::CreateProcessW(
      nullptr, commandLine.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

  ::CloseHandle(childStdoutWrite);
  childStdoutWrite = nullptr;

  if (!created) {
    response.error = LastErrorMessage(L"CreateProcess(native wrapper) failed",
                                      ::GetLastError());
    ::CloseHandle(childStdoutRead);
    ::DeleteFileW(readingFile.c_str());
    return response;
  }

  const DWORD timeoutMs =
      static_cast<DWORD>(settings_.native.timeout_ms > 0
                             ? settings_.native.timeout_ms
                             : 5000);
  DWORD waitResult = ::WaitForSingleObject(pi.hProcess, timeoutMs);
  if (waitResult == WAIT_TIMEOUT) {
    ::TerminateProcess(pi.hProcess, 1);
    ::WaitForSingleObject(pi.hProcess, 1000);
    response.error = L"Mozc native wrapper timed out; no fallback was used.";
  } else if (waitResult == WAIT_FAILED) {
    response.error = LastErrorMessage(L"WaitForSingleObject(native wrapper) failed",
                                      ::GetLastError());
  }

  std::string outputUtf8;
  if (!ReadAllFromHandle(childStdoutRead, &outputUtf8) && response.error.empty()) {
    response.error =
        LastErrorMessage(L"ReadFile(native wrapper stdout) failed", ::GetLastError());
  }

  DWORD exitCode = 0;
  ::GetExitCodeProcess(pi.hProcess, &exitCode);
  ::CloseHandle(childStdoutRead);
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);
  ::DeleteFileW(readingFile.c_str());

  std::wstring parseError;
  auto parsed = ParseJsonObjectFromOutput(outputUtf8, &parseError);
  if (!parsed) {
    if (response.error.empty()) {
      response.error = parseError;
      if (exitCode != 0) {
        response.error += L" wrapper_exit_code=" + std::to_wstring(exitCode);
      }
    }
    return response;
  }

  const auto& object = *parsed;
  const bool ok = object.value("ok", false);
  response.candidates = ReadStringArrayField(object, "top_candidates");
  response.segments = BuildSequentialSegments(
      ReadStringArrayField(object, "segments"));

  std::wstring wrapperError;
  if (auto errorIt = object.find("error");
      errorIt != object.end() && errorIt->is_string()) {
    wrapperError = Utf8ToWide(errorIt->get<std::string>());
  }

  if (!response.error.empty()) {
    if (!wrapperError.empty()) {
      response.error += L" " + wrapperError;
    }
  } else if (!ok || exitCode != 0) {
    response.error = wrapperError.empty()
                         ? L"Mozc native wrapper failed; no fallback was used."
                         : wrapperError + L"; no fallback was used.";
    if (exitCode != 0) {
      response.error += L" wrapper_exit_code=" + std::to_wstring(exitCode);
    }
  } else if (response.candidates.empty()) {
    response.error = L"No candidates returned from Mozc native wrapper.";
  }

  return response;
}

}  // namespace ime::conversion
