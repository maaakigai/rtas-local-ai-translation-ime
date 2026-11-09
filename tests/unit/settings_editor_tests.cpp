#include <filesystem>
#include <fstream>
#include <iostream>

#include "../../src/config/settings_editor.h"
#include "../../third_party/nlohmann/json.hpp"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path path = std::filesystem::path(argv[1]) / L"設定.json";
    const std::string initial = R"({"custom":{"preserve":42},"provider":{"llm":{"model":"old"},"kana":{"mode":"mozc","mozc":{"enabled":true,"bridge_exe":"mozc_bridge.exe"}},"runtime_limits":{"max_preedit_chars":512}}})";
    auto write = [&](const std::string& text) { std::ofstream(path, std::ios::binary) << text; };
    auto read = [&]() { std::ifstream file(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); };
    bool ok = true;
    auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ok = false; }
    };
    write(initial);
    ime::config::SettingsDocument doc;
    std::wstring error;
    expect(ime::config::LoadSettingsDocument(path, doc, error), "load existing config");
    auto llm = doc.settings.translation.llm;
    llm.backend = ime::config::LlmBackend::kLlamaCpp;
    llm.device = ime::config::LlmDevice::kCpu;
    llm.local_runtime_root = "../runtime";
    llm.model = "翻訳モデル";
    llm.port = 8080;
    llm.path = "/v1/chat/completions";
    expect(ime::config::SaveSettingsDocument(doc, llm, true, error), "save edited settings");
    auto json = nlohmann::json::parse(read());
    expect(json["custom"]["preserve"] == 42, "preserve unknown fields");
    expect(json["provider"]["runtime_limits"]["max_preedit_chars"] == 512, "preserve unrelated limits");
    expect(json["provider"]["kana"]["mozc"]["bridge_exe"] == "mozc_bridge.exe", "preserve mozc transport settings");
    expect(json["provider"]["translation"]["llm"]["backend"] == "llama_cpp", "persist backend");
    expect(json["provider"]["kana"]["mozc"]["kana_kanji_only_mode"] == true, "persist kana-only default");
    expect(ime::config::LoadSettingsDocument(path, doc, error), "reload saved config");
    expect(doc.settings.translation.llm.model == "翻訳モデル", "Unicode model round trip");
    expect(doc.settings.translation.llm.device == ime::config::LlmDevice::kCpu, "CPU selection round trip");
    expect(doc.settings.translation.llm.local_runtime_root == llm.local_runtime_root, "runtime root round trip");
    const auto saved = read();
    llm.port = 0;
    expect(!ime::config::SaveSettingsDocument(doc, llm, false, error), "reject invalid port");
    expect(read() == saved, "validation failure leaves file intact");
    llm.port = 8080;
    llm.path = "missing-slash";
    expect(!ime::config::SaveSettingsDocument(doc, llm, false, error), "reject invalid API path");
    llm.path = "/v1/chat/completions";
    llm.timeout_ms = -1;
    expect(!ime::config::SaveSettingsDocument(doc, llm, false, error), "reject invalid timeout");
    llm.timeout_ms = 3000;
    write(saved + "\n");
    expect(!ime::config::SaveSettingsDocument(doc, llm, false, error), "reject stale editor snapshot");
    expect(read() == saved + "\n", "concurrent change remains intact");
    write(R"({"provider":{"mode":"dictionary","dictionary":{"enabled":true,"bilingual_tsv":"legacy.tsv","custom":"keep"}}})");
    expect(ime::config::LoadSettingsDocument(path, doc, error), "load legacy dictionary config");
    expect(ime::config::SaveSettingsDocument(doc, llm, false, error), "edit legacy dictionary config");
    expect(ime::config::LoadSettingsDocument(path, doc, error), "reload migrated config");
    expect(doc.settings.kana.mode == ime::config::ProviderMode::kDictionary, "preserve legacy kana mode");
    expect(doc.settings.translation.mode == ime::config::TranslationMode::kDictionary, "preserve legacy translation mode");
    json = nlohmann::json::parse(read());
    expect(json["provider"]["translation"]["dictionary"]["bilingual_tsv"] == "legacy.tsv", "preserve inherited dictionary path");
    expect(json["provider"]["translation"]["dictionary"]["custom"] == "keep", "preserve inherited dictionary custom fields");
    write("not json");
    expect(!ime::config::LoadSettingsDocument(path, doc, error), "reject malformed config");
    expect(read() == "not json", "malformed file remains intact");
    std::filesystem::remove(path);
    if (ok) std::cout << "PASS: settings round trip, validation, preservation and stale-save protection\n";
    return ok ? 0 : 1;
}
