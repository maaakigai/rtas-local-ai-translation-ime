#include "settings_editor.h"

#include <fstream>
#include <iterator>
#include <windows.h>

#include "../../third_party/nlohmann/json.hpp"

#pragma comment(lib, "advapi32.lib")

namespace ime::config {
namespace {

bool ReadContents(const std::filesystem::path& path, std::string& text) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;
  text.assign(std::istreambuf_iterator<char>(stream), {});
  return !stream.bad();
}

bool Validate(const LlmSettings& llm, std::wstring& error) {
  if (llm.model.find_first_not_of(" \t\r\n") == std::string::npos) {
    error = L"モデル名を入力してください。";
  } else if (llm.host.empty() || llm.host.find_first_of(" /\\\t\r\n") != std::string::npos) {
    error = L"ホスト名にはURLではなく、127.0.0.1などのホスト名を入力してください。";
  } else if (llm.port < 1 || llm.port > 65535) {
    error = L"ポートは1〜65535で指定してください。";
  } else if (llm.path.empty() || llm.path.front() != L'/' ||
             llm.path.find_first_of(" \t\r\n") != std::string::npos) {
    error = L"APIパスは / で始まる空白のない値を入力してください。";
  } else if (llm.timeout_ms < 1 || llm.timeout_ms > 600000) {
    error = L"タイムアウトは1〜600000ミリ秒で指定してください。";
  } else {
    return true;
  }
  return false;
}

}  // namespace

std::filesystem::path RegisteredSettingsPath() {
  constexpr auto key = L"Software\\Classes\\CLSID\\{64AD179E-ADBC-4BEC-B3E8-260997333DE7}\\InprocServer32";
  for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    wchar_t module[32768]{};
    DWORD bytes = sizeof(module);
    if (RegGetValueW(root, key, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
        nullptr, module, &bytes) != ERROR_SUCCESS || module[0] == L'\0') continue;
    const auto config = std::filesystem::path(module).parent_path() / L"config/ime_settings.json";
    std::error_code error;
    if (std::filesystem::is_regular_file(config, error)) return config;
  }
  return {};
}

bool LoadSettingsDocument(const std::filesystem::path& path,
                          SettingsDocument& document, std::wstring& error) {
  error.clear();
  SettingsDocument loaded;
  loaded.path = std::filesystem::absolute(path);
  if (!ReadContents(loaded.path, loaded.original_contents)) {
    error = L"設定ファイルを読み込めません。ファイルを選択してください。";
    return false;
  }
  loaded.settings = LoadProviderSettings(loaded.path, &error);
  if (!error.empty()) return false;
  std::string latest;
  if (!ReadContents(loaded.path, latest) || latest != loaded.original_contents) {
    error = L"読み込み中に設定が変更されました。もう一度開いてください。";
    return false;
  }
  document = std::move(loaded);
  return true;
}

bool SaveSettingsDocument(SettingsDocument& document, const LlmSettings& llm,
                          bool kana_kanji_only_mode, std::wstring& error) {
  error.clear();
  if (!Validate(llm, error)) return false;

  // Serialize editors in this Windows session. The snapshot check also detects
  // changes made by other tools before this save.
  HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\RTASSettingsEditorSave");
  if (!mutex) { error = L"保存用のロックを作成できません。"; return false; }
  const DWORD wait = WaitForSingleObject(mutex, 0);
  if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
    CloseHandle(mutex);
    error = L"別の設定画面が保存中です。少し待ってから保存してください。";
    return false;
  }
  struct Lock {
    HANDLE handle;
    ~Lock() { ReleaseMutex(handle); CloseHandle(handle); }
  } lock{mutex};

  std::string current;
  if (!ReadContents(document.path, current) || current != document.original_contents) {
    error = L"ファイルが別の場所で変更されました。再読み込みしてから編集してください。";
    return false;
  }

  try {
    auto root = nlohmann::json::parse(current, nullptr, true, true);
    auto& provider = root["provider"];
    if (!provider.contains("kana") || !provider["kana"].is_object()) {
      auto kana = nlohmann::json::object();
      for (const auto* key : {"mode", "llm", "dictionary", "mozc"}) {
        if (provider.contains(key)) kana[key] = provider[key];
      }
      provider["kana"] = std::move(kana);
    }
    const bool explicitTranslation = provider.contains("translation") && provider["translation"].is_object();
    if (!explicitTranslation) {
      provider["translation"] = nlohmann::json::object();
      if (provider["kana"].contains("dictionary")) {
        provider["translation"]["dictionary"] = provider["kana"]["dictionary"];
      }
    }
    auto& translation = provider["translation"];
    if (!translation.contains("mode")) {
      translation["mode"] = document.settings.translation.mode == TranslationMode::kDictionary
          ? "dictionary" : "llm";
    }
    auto& node = translation["llm"];
    node["backend"] = llm.backend == LlmBackend::kLlamaCpp ? "llama_cpp" : "ollama";
    node["device"] = llm.device == LlmDevice::kCpu ? "cpu" : "gpu";
    node["local_runtime_root"] = llm.local_runtime_root;
    switch (llm.prompt_format) {
      case LlmPromptFormat::kAuto: node["prompt_format"] = "auto"; break;
      case LlmPromptFormat::kChat: node["prompt_format"] = "chat"; break;
      case LlmPromptFormat::kPlamo2: node["prompt_format"] = "plamo2"; break;
      case LlmPromptFormat::kPlamo3: node["prompt_format"] = "plamo3"; break;
    }
    node["model"] = llm.model;
    node["host"] = llm.host;
    node["port"] = llm.port;
    node["path"] = llm.path;
    node["use_tls"] = llm.use_tls;
    node["timeout_ms"] = llm.timeout_ms;
    root["provider"]["kana"]["mozc"]["kana_kanji_only_mode"] = kana_kanji_only_mode;
    const std::string output = root.dump(2) + "\n";
    const auto temporary = std::filesystem::path(document.path.wstring() + L"." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp");
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      error = L"保存先に一時ファイルを作成できません。書き込み権限を確認してください。";
      return false;
    }
    DWORD written = 0;
    const bool wrote = WriteFile(file, output.data(), static_cast<DWORD>(output.size()), &written, nullptr)
        && written == output.size() && FlushFileBuffers(file);
    CloseHandle(file);
    // Recheck after writing the temporary file, before replacing the original.
    const bool unchanged = ReadContents(document.path, current) && current == document.original_contents;
    if (!wrote || !unchanged || !ReplaceFileW(document.path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)) {
      DeleteFileW(temporary.c_str());
      error = unchanged ? L"設定を保存できません。書き込み権限を確認してください。"
                        : L"保存中に設定が変更されました。再読み込みしてください。";
      return false;
    }
    document.original_contents = output;
    document.settings.translation.llm = llm;
    if (document.settings.kana.mozc) {
      document.settings.kana.mozc->kana_kanji_only_mode = kana_kanji_only_mode;
    }
    return true;
  } catch (const std::exception&) {
    error = L"設定ファイルの形式が不正なため保存できません。";
    return false;
  }
}

}  // namespace ime::config
