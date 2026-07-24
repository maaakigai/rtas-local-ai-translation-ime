// Minimal RTAS Mozc native smoke probe.
//
// Build this inside a pinned google/mozc checkout, not inside RTAS:
//   copy to src/rtas_probe/{BUILD.bazel,rtas_mozc_client_probe.cc}
//   bazelisk build --config oss_windows --config release_build //rtas_probe:rtas_mozc_client_probe
//
// The probe uses Mozc's official ClientInterface and generated protocol types.
// It deliberately does not implement or parse the IPC wire format itself.

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/time/time.h"
#include "base/init_mozc.h"
#include "client/client.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"

ABSL_FLAG(std::string, server_path, "", "Path to mozc_server.exe");
ABSL_FLAG(std::string, reading, "kyouhaiitenkidesu",
          "UTF-8 reading to send through TEXT_INPUT");
ABSL_FLAG(std::string, reading_file, "",
          "Path to a UTF-8 file containing the reading to send");
ABSL_FLAG(int, top_n, 8, "Maximum candidates to print");
ABSL_FLAG(int, timeout_ms, 5000, "Server startup/IPC timeout");

namespace {

std::string JsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (ch < 0x20) {
          const char hex[] = "0123456789abcdef";
          escaped += "\\u00";
          escaped.push_back(hex[(ch >> 4) & 0x0f]);
          escaped.push_back(hex[ch & 0x0f]);
        } else {
          escaped.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  return escaped;
}

std::string JsonStringArray(const std::vector<std::string>& values) {
  std::ostringstream stream;
  stream << "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      stream << ",";
    }
    stream << "\"" << JsonEscape(values[i]) << "\"";
  }
  stream << "]";
  return stream.str();
}

std::string JsonBool(bool value) { return value ? "true" : "false"; }

bool ReadUtf8File(const std::string& path, std::string* content,
                  std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    *error = "failed to open reading_file: " + path;
    return false;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  *content = buffer.str();

  constexpr unsigned char kBom[] = {0xef, 0xbb, 0xbf};
  if (content->size() >= 3 &&
      static_cast<unsigned char>((*content)[0]) == kBom[0] &&
      static_cast<unsigned char>((*content)[1]) == kBom[1] &&
      static_cast<unsigned char>((*content)[2]) == kBom[2]) {
    content->erase(0, 3);
  }
  while (!content->empty() &&
         (content->back() == '\r' || content->back() == '\n')) {
    content->pop_back();
  }
  return true;
}

std::vector<std::string> ExtractCandidates(const mozc::commands::Output& output,
                                           int top_n) {
  std::vector<std::string> values;
  if (output.has_candidate_window()) {
    const auto& window = output.candidate_window();
    for (int i = 0; i < window.candidate_size() &&
                    static_cast<int>(values.size()) < top_n;
         ++i) {
      values.push_back(window.candidate(i).value());
    }
  }
  if (output.has_all_candidate_words()) {
    const auto& words = output.all_candidate_words();
    for (int i = 0; i < words.candidates_size() &&
                    static_cast<int>(values.size()) < top_n;
         ++i) {
      values.push_back(words.candidates(i).value());
    }
  }
  return values;
}

std::vector<std::string> ExtractSegments(const mozc::commands::Output& output) {
  std::vector<std::string> values;
  if (!output.has_preedit()) {
    return values;
  }
  const auto& preedit = output.preedit();
  for (int i = 0; i < preedit.segment_size(); ++i) {
    values.push_back(preedit.segment(i).value());
  }
  return values;
}

class ManifestServerLauncher final : public mozc::client::ServerLauncher {
 public:
  explicit ManifestServerLauncher(std::string server_path)
      : server_path_(std::move(server_path)) {}

  bool StartServer(mozc::client::ClientInterface* client) override {
    if (server_path_.empty()) {
      error_ = "server_path is empty";
      return false;
    }
    const bool ok = mozc::client::ServerLauncher::StartServer(client);
    if (!ok && error_.empty()) {
      error_ =
          "official Mozc ServerLauncher could not make mozc_server reachable";
    }
    return ok;
  }

  void OnFatal(ServerErrorType type) override {
    fatal_count_ += 1;
    last_fatal_ = static_cast<int>(type);
  }

  std::string server_program() const override { return server_path_; }

  const std::string& error() const { return error_; }
  int fatal_count() const { return fatal_count_; }
  int last_fatal() const { return last_fatal_; }

 private:
  std::string server_path_;
  std::string error_;
  int fatal_count_ = 0;
  int last_fatal_ = -1;
};

mozc::commands::Output SendTurnOnIme(mozc::client::ClientInterface* client,
                                     bool* ok) {
  mozc::commands::SessionCommand command;
  command.set_type(mozc::commands::SessionCommand::TURN_ON_IME);
  command.set_composition_mode(mozc::commands::HIRAGANA);
  mozc::commands::Output output;
  *ok = client->SendCommand(command, &output);
  return output;
}

mozc::commands::Output SendTextInput(mozc::client::ClientInterface* client,
                                     const std::string& reading, bool* ok) {
  mozc::commands::KeyEvent key;
  key.set_special_key(mozc::commands::KeyEvent::TEXT_INPUT);
  key.set_key_string(reading);
  key.set_input_style(mozc::commands::KeyEvent::AS_IS);
  key.set_activated(true);
  key.set_mode(mozc::commands::HIRAGANA);
  mozc::commands::Output output;
  *ok = client->SendKey(key, &output);
  return output;
}

mozc::commands::Output SendConvert(mozc::client::ClientInterface* client,
                                   bool* ok) {
  mozc::commands::KeyEvent key;
  key.set_special_key(mozc::commands::KeyEvent::SPACE);
  key.set_activated(true);
  key.set_mode(mozc::commands::HIRAGANA);
  mozc::commands::Output output;
  *ok = client->SendKey(key, &output);
  return output;
}

}  // namespace

int main(int argc, char** argv) {
  mozc::InitMozc(argv[0], &argc, &argv);

  const std::string server_path = absl::GetFlag(FLAGS_server_path);
  std::string reading = absl::GetFlag(FLAGS_reading);
  const std::string reading_file = absl::GetFlag(FLAGS_reading_file);
  const int top_n = std::max(1, absl::GetFlag(FLAGS_top_n));
  const int timeout_ms = std::max(1, absl::GetFlag(FLAGS_timeout_ms));

  std::string setup_error;
  if (!reading_file.empty() &&
      !ReadUtf8File(reading_file, &reading, &setup_error)) {
    std::cout << "{"
              << "\"ok\":false"
              << ",\"connection_ok\":false"
              << ",\"session_ok\":false"
              << ",\"turn_on_ime_ok\":false"
              << ",\"text_input_ok\":false"
              << ",\"convert_ok\":false"
              << ",\"elapsed_ms\":0"
              << ",\"reading\":\"\""
              << ",\"top_candidates\":[]"
              << ",\"segments\":[]"
              << ",\"has_candidate_window\":false"
              << ",\"has_all_candidate_words\":false"
              << ",\"has_preedit\":false"
              << ",\"fatal_count\":0"
              << ",\"last_fatal\":-1"
              << ",\"error\":\"" << JsonEscape(setup_error) << "\""
              << "}" << std::endl;
    return 2;
  }

  auto launcher = std::make_unique<ManifestServerLauncher>(server_path);
  ManifestServerLauncher* launcher_raw = launcher.get();

  std::unique_ptr<mozc::client::ClientInterface> client =
      mozc::client::ClientFactory::NewClient();
  client->set_timeout(absl::Milliseconds(timeout_ms));
  client->set_suppress_error_dialog(true);
  client->SetServerLauncher(std::move(launcher));

  const auto started_at = std::chrono::steady_clock::now();
  const bool connection_ok = client->EnsureConnection();
  const bool session_ok = connection_ok && client->EnsureSession();

  bool turn_on_ok = false;
  bool input_ok = false;
  bool convert_ok = false;
  mozc::commands::Output turn_on_output;
  mozc::commands::Output input_output;
  mozc::commands::Output convert_output;

  if (session_ok) {
    turn_on_output = SendTurnOnIme(client.get(), &turn_on_ok);
  }
  if (turn_on_ok) {
    input_output = SendTextInput(client.get(), reading, &input_ok);
  }
  if (input_ok) {
    convert_output = SendConvert(client.get(), &convert_ok);
  }

  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started_at)
          .count();

  const std::vector<std::string> candidates =
      ExtractCandidates(convert_output, top_n);
  const std::vector<std::string> segments = ExtractSegments(convert_output);

  std::ostringstream error;
  if (!connection_ok) {
    error << "EnsureConnection failed";
  } else if (!session_ok) {
    error << "EnsureSession failed";
  } else if (!turn_on_ok) {
    error << "TURN_ON_IME failed";
  } else if (!input_ok) {
    error << "TEXT_INPUT failed";
  } else if (!convert_ok) {
    error << "SPACE conversion failed";
  }
  if (!launcher_raw->error().empty()) {
    if (error.tellp() > 0) {
      error << "; ";
    }
    error << launcher_raw->error();
  }

  std::cout << "{"
            << "\"ok\":" << JsonBool(connection_ok && session_ok && turn_on_ok &&
                                      input_ok && convert_ok)
            << ",\"connection_ok\":" << JsonBool(connection_ok)
            << ",\"session_ok\":" << JsonBool(session_ok)
            << ",\"turn_on_ime_ok\":" << JsonBool(turn_on_ok)
            << ",\"text_input_ok\":" << JsonBool(input_ok)
            << ",\"convert_ok\":" << JsonBool(convert_ok)
            << ",\"elapsed_ms\":" << elapsed_ms
            << ",\"reading\":\"" << JsonEscape(reading) << "\""
            << ",\"top_candidates\":" << JsonStringArray(candidates)
            << ",\"segments\":" << JsonStringArray(segments)
            << ",\"has_candidate_window\":"
            << JsonBool(convert_output.has_candidate_window())
            << ",\"has_all_candidate_words\":"
            << JsonBool(convert_output.has_all_candidate_words())
            << ",\"has_preedit\":" << JsonBool(convert_output.has_preedit())
            << ",\"fatal_count\":" << launcher_raw->fatal_count()
            << ",\"last_fatal\":" << launcher_raw->last_fatal()
            << ",\"error\":\"" << JsonEscape(error.str()) << "\""
            << "}" << std::endl;

  return (connection_ok && session_ok && turn_on_ok && input_ok && convert_ok)
             ? 0
             : 2;
}
