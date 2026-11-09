#include "local_server.h"

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <winhttp.h>
#include <fstream>
#include <vector>
#include "../../third_party/nlohmann/json.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace llm {
namespace {
using Json = nlohmann::json;
using Path = std::filesystem::path;

struct Failure { std::wstring message; };

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

struct HttpHandle {
    HINTERNET value;
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};

struct AttributeList {
    LPPROC_THREAD_ATTRIBUTE_LIST value;
    ~AttributeList() { DeleteProcThreadAttributeList(value); }
};

struct MutexLock {
    Handle handle;
    explicit MutexLock(const std::wstring& name, DWORD timeout) {
        handle.value = CreateMutexW(nullptr, FALSE, name.c_str());
        const DWORD result = handle.value ? WaitForSingleObject(handle.value, timeout) : WAIT_FAILED;
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            throw Failure{L"別の起動・設定処理が進行中です。時間をおいて保存してください。"};
        }
    }
    ~MutexLock() { ReleaseMutex(handle.value); }
};

std::wstring Wide(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

Json ReadJson(const Path& path) {
    std::ifstream file(path, std::ios::binary);
    return Json::parse(file, nullptr, true, true);
}

bool SamePath(const Path& left, const Path& right) {
    return _wcsicmp(left.lexically_normal().c_str(), right.lexically_normal().c_str()) == 0;
}

DWORD Listener(int port) {
    DWORD bytes = 0;
    GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    std::vector<BYTE> buffer(bytes);
    if (GetExtendedTcpTable(buffer.data(), &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) {
        throw Failure{L"ローカルサーバーの接続状態を確認できません。"};
    }
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const auto& row = table->table[index];
        const bool local = row.dwLocalAddr == htonl(INADDR_LOOPBACK) || row.dwLocalAddr == INADDR_ANY;
        if (local && ntohs(static_cast<u_short>(row.dwLocalPort)) == port) return row.dwOwningPid;
    }
    return 0;
}

std::string StartTicks(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) return {};
    ULARGE_INTEGER time{};
    time.LowPart = creation.dwLowDateTime;
    time.HighPart = creation.dwHighDateTime;
    return std::to_string(time.QuadPart + 504911232000000000ULL);
}

HANDLE ManagedProcess(const Json& state, const Path& executable, const Path& model, int port) {
    if (!state.is_object() || !state.contains("process_id")) return nullptr;
    const DWORD id = state.at("process_id").get<DWORD>();
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, id)};
    if (!process.value || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) return nullptr;
    wchar_t file[32768]{};
    DWORD length = ARRAYSIZE(file);
    if (!QueryFullProcessImageNameW(process.value, 0, file, &length) || !SamePath(file, executable) ||
        state.value("start_ticks", std::string{}) != StartTicks(process.value) ||
        !SamePath(Wide(state.value("model", std::string{})), model) || state.value("port", 0) != port) return nullptr;
    const DWORD listener = Listener(port);
    if (listener != 0 && listener != id) return nullptr;
    HANDLE result = process.value;
    process.value = nullptr;
    return result;
}

class LocalHttp {
public:
    explicit LocalHttp(int port)
        : session_{WinHttpOpen(L"RTAS/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)},
          connection_{nullptr} {
        if (!session_.value) throw Failure{L"ローカルサーバーへの接続を準備できません。"};
        WinHttpSetTimeouts(session_.value, 1500, 1500, 1500, 1500);
        connection_.value = WinHttpConnect(session_.value, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
        if (!connection_.value) throw Failure{L"ローカルサーバーに接続できません。"};
    }

    Json Get(const wchar_t* path) {
        HttpHandle request{WinHttpOpenRequest(connection_.value, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
        if (!request.value || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr)) return nullptr;
        DWORD status = 0, bytes = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX) || status != 200) return nullptr;
        std::string body;
        for (;;) {
            char buffer[8192];
            DWORD read = 0;
            if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) return nullptr;
            if (read == 0) break;
            body.append(buffer, read);
            if (body.size() > 1024 * 1024) return nullptr;
        }
        return Json::parse(body, nullptr, false);
    }

    bool Ready() {
        const auto health = Get(L"/health");
        return health.is_object() && health.value("status", std::string{}) == "ok";
    }

    void ConfirmModel(const Path& model) {
        const auto props = Get(L"/props");
        const auto models = Get(L"/v1/models");
        if (!props.is_object() || !SamePath(Wide(props.value("model_path", std::string{})), model) ||
            !models.contains("data")) throw Failure{L"実行中のモデルを確認できません。"};
        for (const auto& item : models.at("data")) {
            if (item.value("id", std::string{}) == "plamo-3-translate") return;
        }
        throw Failure{L"この接続先では別のモデルが動いています。"};
    }
private:
    HttpHandle session_;
    HttpHandle connection_;
};

void WaitReady(LocalHttp& http, HANDLE process, const Path& model, const Path& log) {
    const ULONGLONG deadline = GetTickCount64() + 180000;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) break;
        if (http.Ready()) { http.ConfirmModel(model); return; }
        Sleep(500);
    }
    throw Failure{L"サーバーを起動できません。GPUとモデルを確認してください。ログ " + log.wstring()};
}

}  // namespace

DeviceApplyResult ApplyLocalDevice(const ime::config::LlmSettings& settings, const Path& configPath) {
    try {
        MutexLock serverLock(L"Local\\RTASPlamoServer-" + std::to_wstring(settings.port), 180000);
        MutexLock settingsLock(L"Local\\RTASSettingsEditorSave", 10000);
        const auto latest = ReadJson(configPath).at("provider").at("translation").at("llm");
        const std::string device = settings.device == ime::config::LlmDevice::kCpu ? "cpu" : "gpu";
        if (latest.value("device", "gpu") != device || latest.value("backend", "") != "llama_cpp" ||
            latest.value("port", 0) != settings.port || latest.value("host", "") != settings.host ||
            latest.value("model", "") != "plamo-3-translate" || latest.value("use_tls", false) ||
            latest.value("local_runtime_root", "") != settings.local_runtime_root) {
            throw Failure{L"別の画面で設定が変更されています。再読み込みしてください。"};
        }
        const Path root = std::filesystem::absolute(configPath.parent_path() / Wide(settings.local_runtime_root)).lexically_normal();
        const Path runtime = (root / L"build/llama_cpp/b12110/llama-server.exe").lexically_normal();
        const Path logs = (root / L"build/runtime").lexically_normal();
        const Path statePath = logs / (L"plamo-" + std::to_wstring(settings.port) + L".state.json");
        Json state = Json::object();
        if (std::filesystem::exists(statePath)) state = ReadJson(statePath);
        const auto legacyModel = Path(Wide(state.value("model", std::string{}))).filename();
        const auto size = state.value("size", legacyModel == L"plamo-3-translate-2b-Q4_K_M.gguf"
            ? std::string("2b") : std::string("0.8b"));
        if (size != "0.8b" && size != "2b") throw Failure{L"モデルのサイズ設定が不正です。"};
        const Path model = (root / (L"build/models/plamo-3-translate-" + Wide(size) + L"-Q4_K_M.gguf")).lexically_normal();
        if (!std::filesystem::is_regular_file(runtime) || !std::filesystem::is_regular_file(model)) {
            throw Failure{L"ローカルのllama.cppまたはPLaMoモデルが見つかりません。"};
        }
        LocalHttp http(settings.port);
        Handle managed{ManagedProcess(state, runtime, model, settings.port)};
        if (managed.value) {
            if (state.value("device", "") == device) {
                WaitReady(http, managed.value, model, logs);
                return {true, {}};
            }
            if (http.Ready()) http.ConfirmModel(model);
            if (!TerminateProcess(managed.value, 0) || WaitForSingleObject(managed.value, 10000) != WAIT_OBJECT_0) {
                throw Failure{L"ローカルサーバーを停止できません。"};
            }
        } else if (Listener(settings.port)) {
            throw Failure{L"このサーバーはRTASの管理対象として確認できないため、推論先を変更できません。"};
        }
        std::filesystem::create_directories(logs);
        const Path log = logs / (L"plamo-native-" + std::to_wstring(GetTickCount64()) + L".log");
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        Handle output{CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        Handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr)};
        if (output.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE) throw Failure{L"起動ログを作成できません。"};
        std::wstring command = L"\"" + runtime.wstring() + L"\" -m \"" + model.wstring() +
            L"\" --alias plamo-3-translate --host 127.0.0.1 --port " + std::to_wstring(settings.port) +
            L" -c 4096 --parallel 1 " + (device == "cpu" ? L"--device none -ngl 0" : L"--device Vulkan0 -ngl 99");
        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        std::vector<BYTE> attributeBuffer(attributeBytes);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)) {
            throw Failure{L"サーバーの起動設定を準備できません。"};
        }
        AttributeList attributeList{attributes};
        HANDLE inherited[] = {input.value, output.value};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr)) throw Failure{L"起動ログの接続を準備できません。"};
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input.value;
        startup.StartupInfo.hStdOutput = output.value;
        startup.StartupInfo.hStdError = output.value;
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION launched{};
        if (!CreateProcessW(runtime.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
            nullptr, root.c_str(), &startup.StartupInfo, &launched)) throw Failure{L"ローカルサーバーを起動できません。"};
        Handle process{launched.hProcess};
        Handle thread{launched.hThread};
        const Path temporary = statePath.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        try {
            const auto encodedPath = model.u8string();
            state = {{"process_id", launched.dwProcessId}, {"start_ticks", StartTicks(process.value)},
                {"model", std::string(encodedPath.begin(), encodedPath.end())}, {"device", device},
                {"port", settings.port}, {"size", size}};
            std::ofstream file(temporary, std::ios::binary);
            file << state.dump(2);
            file.flush();
            if (!file) throw Failure{L"サーバーの管理情報を保存できません。"};
            file.close();
            if (!MoveFileExW(temporary.c_str(), statePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                throw Failure{L"サーバーの管理情報を更新できません。"};
            }
            WaitReady(http, process.value, model, log);
        } catch (...) {
            TerminateProcess(process.value, 1);
            DeleteFileW(temporary.c_str());
            throw;
        }
        return {true, {}};
    } catch (const Failure& failure) {
        return {false, failure.message};
    } catch (const std::exception&) {
        return {false, L"推論先の変更に失敗しました。ローカル環境と設定ファイルを確認してください。"};
    }
}

}  // namespace llm
