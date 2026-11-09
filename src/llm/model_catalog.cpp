#include "model_catalog.h"

#include <algorithm>
#include <windows.h>
#include <winhttp.h>
#include "../../third_party/nlohmann/json.hpp"

#pragma comment(lib, "winhttp.lib")

namespace llm {
namespace {

struct HttpHandle {
    HINTERNET value;
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};

}  // namespace

ModelCatalog FetchModelCatalog(const ime::config::LlmSettings& settings) {
    ModelCatalog result;
    const bool ollama = settings.backend == ime::config::LlmBackend::kOllama;
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, settings.host.data(),
        static_cast<int>(settings.host.size()), nullptr, 0);
    std::wstring host(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, settings.host.data(),
        static_cast<int>(settings.host.size()), host.data(), length);
    int port = settings.port;
    if (ollama) {
        wchar_t value[256]{};
        DWORD size = GetEnvironmentVariableW(L"IME3_OLLAMA_HOST", value, ARRAYSIZE(value));
        if (size > 0 && size < ARRAYSIZE(value)) host.assign(value, size);
        size = GetEnvironmentVariableW(L"IME3_OLLAMA_PORT", value, ARRAYSIZE(value));
        const int overridePort = _wtoi(value);
        if (size > 0 && size < ARRAYSIZE(value) && overridePort > 0 && overridePort <= 65535) port = overridePort;
    }
    if (host.empty() || port < 1 || port > 65535) {
        result.error = L"ホストとポートを確認してください。";
        return result;
    }
    auto failed = [&result]() {
        result.error = L"モデル一覧を取得できません。サーバーの起動と接続先を確認してください。（" +
            std::to_wstring(GetLastError()) + L"）";
        return result;
    };
    HttpHandle session{WinHttpOpen(L"RTAS/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return failed();
    WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 3000);
    HttpHandle connection{WinHttpConnect(session.value, host.c_str(), static_cast<INTERNET_PORT>(port), 0)};
    if (!connection.value) return failed();
    HttpHandle request{WinHttpOpenRequest(connection.value, L"GET", ollama ? L"/api/tags" : L"/v1/models",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, settings.use_tls ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.value) return failed();
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) return failed();
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) return failed();
    if (status != 200) {
        result.error = L"モデル一覧の取得に失敗しました。HTTP " + std::to_wstring(status);
        return result;
    }
    std::string body;
    const ULONGLONG deadline = GetTickCount64() + 10000;
    for (;;) {
        char buffer[8192];
        DWORD count = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &count)) return failed();
        if (count == 0) break;
        body.append(buffer, count);
        if (body.size() > 1024 * 1024 || GetTickCount64() > deadline) {
            result.error = L"モデル一覧の応答が大きすぎるか、取得時間を超えました。";
            return result;
        }
    }
    try {
        const auto json = nlohmann::json::parse(body);
        const auto& models = json.at(ollama ? "models" : "data");
        if (!models.is_array()) throw std::runtime_error("Expected model array");
        for (const auto& model : models) {
            if (!model.is_object()) continue;
            auto id = model.find(ollama ? "name" : "id");
            if (id == model.end() || !id->is_string()) continue;
            const auto name = id->get<std::string>();
            if (!name.empty() && std::find(result.models.begin(), result.models.end(), name) == result.models.end()) {
                result.models.push_back(name);
            }
        }
    } catch (const std::exception&) {
        result.error = L"モデル一覧の応答形式が不正です。";
    }
    return result;
}

}  // namespace llm
