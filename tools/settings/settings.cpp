#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <memory>
#include <mutex>
#include <thread>

#include "resource.h"
#include "../../src/config/settings_editor.h"
#include "../../src/llm/model_catalog.h"
#include "../../src/llm/local_server.h"

namespace {

struct CatalogRequest {
    std::mutex mutex;
    bool ready = false;
    llm::ModelCatalog result;
};

struct DeviceRequest {
    std::mutex mutex;
    bool ready = false;
    bool success = false;
    std::wstring error;
};

struct DialogState {
    ime::config::SettingsDocument document;
    std::filesystem::path initialPath;
    bool loaded = false;
    bool updating = false;
    bool dirty = false;
    std::shared_ptr<CatalogRequest> catalog;
    std::shared_ptr<DeviceRequest> deviceRequest;
    bool closeAfterDevice = false;
};

std::wstring Wide(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string Utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Text(HWND dialog, int control) {
    HWND item = GetDlgItem(dialog, control);
    std::wstring text(GetWindowTextLengthW(item) + 1, L'\0');
    text.resize(GetWindowTextW(item, text.data(), static_cast<int>(text.size())));
    const auto start = text.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return {};
    return text.substr(start, text.find_last_not_of(L" \t\r\n") - start + 1);
}

void Status(HWND dialog, const wchar_t* text) {
    SetDlgItemTextW(dialog, IDC_STATUS, text);
}

ime::config::LlmSettings ReadLlm(HWND dialog, const DialogState& state) {
    auto llm = state.document.settings.translation.llm;
    llm.backend = SendDlgItemMessageW(dialog, IDC_BACKEND, CB_GETCURSEL, 0, 0) == 0
        ? ime::config::LlmBackend::kLlamaCpp : ime::config::LlmBackend::kOllama;
    const auto format = SendDlgItemMessageW(dialog, IDC_PROMPT_FORMAT, CB_GETCURSEL, 0, 0);
    llm.prompt_format = format >= 0 && format <= 3
        ? static_cast<ime::config::LlmPromptFormat>(format) : ime::config::LlmPromptFormat::kAuto;
    llm.model = Utf8(Text(dialog, IDC_MODEL));
    llm.host = Utf8(Text(dialog, IDC_HOST));
    llm.path = Utf8(Text(dialog, IDC_PATH));
    BOOL validPort = FALSE;
    BOOL validTimeout = FALSE;
    llm.port = static_cast<int>(GetDlgItemInt(dialog, IDC_PORT, &validPort, FALSE));
    llm.timeout_ms = static_cast<int>(GetDlgItemInt(dialog, IDC_TIMEOUT, &validTimeout, FALSE));
    if (!validPort) llm.port = 0;
    if (!validTimeout) llm.timeout_ms = 0;
    llm.use_tls = IsDlgButtonChecked(dialog, IDC_TLS) == BST_CHECKED;
    llm.device = SendDlgItemMessageW(dialog, IDC_DEVICE, CB_GETCURSEL, 0, 0) == 1
        ? ime::config::LlmDevice::kCpu : ime::config::LlmDevice::kGpu;
    return llm;
}

bool IsManagedLocal(const ime::config::LlmSettings& llm) {
    return llm.backend == ime::config::LlmBackend::kLlamaCpp && !llm.use_tls &&
        (llm.host == "127.0.0.1" || llm.host == "localhost") &&
        llm.model == "plamo-3-translate" && !llm.local_runtime_root.empty();
}

void UpdateDeviceControl(HWND dialog, const DialogState& state) {
    EnableWindow(GetDlgItem(dialog, IDC_DEVICE), IsManagedLocal(ReadLlm(dialog, state)));
}

void EnableForm(HWND dialog, const DialogState& state, bool enabled) {
    for (int id : {IDOK, IDC_RELOAD, IDC_BROWSE, IDC_FETCH_MODELS, IDC_BACKEND, IDC_MODEL,
        IDC_HOST, IDC_PORT, IDC_PATH, IDC_TIMEOUT, IDC_TLS, IDC_KANA_ONLY, IDC_PROMPT_FORMAT, IDC_DEVICE}) {
        EnableWindow(GetDlgItem(dialog, id), enabled);
    }
    if (enabled) UpdateDeviceControl(dialog, state);
}

void CancelCatalog(HWND dialog, DialogState& state) {
    state.catalog.reset();
    KillTimer(dialog, IDC_FETCH_MODELS);
    EnableWindow(GetDlgItem(dialog, IDC_FETCH_MODELS), state.loaded && !state.deviceRequest);
}

void FetchModels(HWND dialog, DialogState& state) {
    CancelCatalog(dialog, state);
    const auto settings = ReadLlm(dialog, state);
    auto request = std::make_shared<CatalogRequest>();
    state.catalog = request;
    EnableWindow(GetDlgItem(dialog, IDC_FETCH_MODELS), FALSE);
    Status(dialog, L"サーバーからモデル一覧を取得しています…");
    std::thread([request, settings]() {
        llm::ModelCatalog catalog;
        try {
            catalog = llm::FetchModelCatalog(settings);
        } catch (const std::exception&) {
            catalog.error = L"モデル一覧の取得に失敗しました。";
        }
        std::lock_guard<std::mutex> lock(request->mutex);
        request->result = std::move(catalog);
        request->ready = true;
    }).detach();
    SetTimer(dialog, IDC_FETCH_MODELS, 100, nullptr);
}

void ApplyModels(HWND dialog, DialogState& state) {
    if (!state.catalog) return;
    llm::ModelCatalog catalog;
    {
        std::lock_guard<std::mutex> lock(state.catalog->mutex);
        if (!state.catalog->ready) return;
        catalog = std::move(state.catalog->result);
    }
    CancelCatalog(dialog, state);
    if (!catalog.error.empty()) { Status(dialog, catalog.error.c_str()); return; }
    const auto selected = Text(dialog, IDC_MODEL);
    state.updating = true;
    SendDlgItemMessageW(dialog, IDC_MODEL, CB_RESETCONTENT, 0, 0);
    for (const auto& model : catalog.models) {
        const auto name = Wide(model);
        SendDlgItemMessageW(dialog, IDC_MODEL, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    if (!selected.empty() && SendDlgItemMessageW(dialog, IDC_MODEL, CB_FINDSTRINGEXACT,
        static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(selected.c_str())) == CB_ERR) {
        SendDlgItemMessageW(dialog, IDC_MODEL, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(selected.c_str()));
    }
    SetDlgItemTextW(dialog, IDC_MODEL, selected.c_str());
    state.updating = false;
    Status(dialog, catalog.models.empty() ? L"公開中のモデルがありません。サーバーのモデル設定を確認してください。"
        : L"モデル一覧を取得しました。モデルを選択して保存してください。");
}

void ApplyDevice(HWND dialog, DialogState& state, const ime::config::LlmSettings& llm) {
    CancelCatalog(dialog, state);
    auto request = std::make_shared<DeviceRequest>();
    state.deviceRequest = request;
    EnableForm(dialog, state, false);
    Status(dialog, L"保存しました。ローカルサーバーを再起動して推論先を反映しています…");
    const auto config = state.document.path;
    std::thread([request, config, llm]() {
        const auto result = llm::ApplyLocalDevice(llm, config);
        std::lock_guard<std::mutex> lock(request->mutex);
        request->success = result.success;
        request->error = result.error;
        request->ready = true;
    }).detach();
    SetTimer(dialog, IDC_DEVICE, 100, nullptr);
}

void FinishDevice(HWND dialog, DialogState& state) {
    if (!state.deviceRequest) return;
    bool success;
    std::wstring error;
    {
        std::lock_guard<std::mutex> lock(state.deviceRequest->mutex);
        if (!state.deviceRequest->ready) return;
        success = state.deviceRequest->success;
        error = state.deviceRequest->error;
    }
    state.deviceRequest.reset();
    KillTimer(dialog, IDC_DEVICE);
    EnableForm(dialog, state, true);
    Status(dialog, success ? L"保存した推論先に切り替えました。翻訳を利用できます。"
        : L"設定は保存済みですが、推論先の適用に失敗しました。保存で再試行できます。");
    if (!success) MessageBoxW(dialog, error.c_str(), L"推論先を変更できません", MB_OK | MB_ICONERROR);
    if (success && state.closeAfterDevice) EndDialog(dialog, 0);
    state.closeAfterDevice = false;
}

void Populate(HWND dialog, DialogState& state) {
    CancelCatalog(dialog, state);
    state.updating = true;
    const auto& llm = state.document.settings.translation.llm;
    SendDlgItemMessageW(dialog, IDC_BACKEND, CB_SETCURSEL,
        llm.backend == ime::config::LlmBackend::kLlamaCpp ? 0 : 1, 0);
    SendDlgItemMessageW(dialog, IDC_MODEL, CB_RESETCONTENT, 0, 0);
    const auto model = Wide(llm.model);
    SendDlgItemMessageW(dialog, IDC_MODEL, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(model.c_str()));
    SetDlgItemTextW(dialog, IDC_MODEL, model.c_str());
    SendDlgItemMessageW(dialog, IDC_PROMPT_FORMAT, CB_SETCURSEL, static_cast<WPARAM>(llm.prompt_format), 0);
    SendDlgItemMessageW(dialog, IDC_DEVICE, CB_SETCURSEL, llm.device == ime::config::LlmDevice::kCpu ? 1 : 0, 0);
    SetDlgItemTextW(dialog, IDC_HOST, Wide(llm.host).c_str());
    SetDlgItemInt(dialog, IDC_PORT, llm.port, FALSE);
    SetDlgItemTextW(dialog, IDC_PATH, Wide(llm.path).c_str());
    SetDlgItemInt(dialog, IDC_TIMEOUT, llm.timeout_ms, FALSE);
    CheckDlgButton(dialog, IDC_TLS, llm.use_tls ? BST_CHECKED : BST_UNCHECKED);
    const auto& mozc = state.document.settings.kana.mozc;
    CheckDlgButton(dialog, IDC_KANA_ONLY, mozc && mozc->kana_kanji_only_mode ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(dialog, IDC_CONFIG_PATH, state.document.path.c_str());
    EnableWindow(GetDlgItem(dialog, IDOK), TRUE);
    EnableWindow(GetDlgItem(dialog, IDC_RELOAD), TRUE);
    state.loaded = true;
    UpdateDeviceControl(dialog, state);
    state.dirty = false;
    state.updating = false;
    Status(dialog, L"保存後、IMEを使用するアプリを再起動してください。");
    FetchModels(dialog, state);
}

bool Load(HWND dialog, DialogState& state, const std::filesystem::path& path) {
    if (state.deviceRequest) return false;
    std::wstring error;
    if (!ime::config::LoadSettingsDocument(path, state.document, error)) {
        MessageBoxW(dialog, error.c_str(), L"設定を開けません", MB_OK | MB_ICONERROR);
        return false;
    }
    Populate(dialog, state);
    return true;
}

bool Save(HWND dialog, DialogState& state) {
    if (!state.loaded || state.deviceRequest) return false;
    const auto llm = ReadLlm(dialog, state);
    const bool applyDevice = IsManagedLocal(llm);
    std::error_code launcherError;
    if (applyDevice && !std::filesystem::is_regular_file(
        (state.document.path.parent_path() / Wide(llm.local_runtime_root)) / L"build/llama_cpp/b12110/llama-server.exe", launcherError)) {
        MessageBoxW(dialog, L"ローカルのllama.cpp実行環境が見つかりません。", L"保存できません", MB_OK | MB_ICONERROR);
        return false;
    }
    std::wstring error;
    if (!ime::config::SaveSettingsDocument(state.document, llm,
        IsDlgButtonChecked(dialog, IDC_KANA_ONLY) == BST_CHECKED, error)) {
        MessageBoxW(dialog, error.c_str(), L"保存できません", MB_OK | MB_ICONWARNING);
        return false;
    }
    state.dirty = false;
    if (applyDevice) {
        ApplyDevice(dialog, state, llm);
        return true;
    }
    Status(dialog, L"保存しました。IMEを使用するアプリを再起動してください。");
    return true;
}

bool ConfirmChanges(HWND dialog, DialogState& state) {
    if (!state.dirty) return true;
    const int choice = MessageBoxW(dialog, L"設定の変更を保存しますか？", L"RTAS 設定",
                                   MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return false;
    return choice == IDNO || Save(dialog, state);
}

void CloseDialog(HWND dialog, DialogState& state) {
    if (!state.deviceRequest && !ConfirmChanges(dialog, state)) return;
    if (state.deviceRequest) {
        state.closeAfterDevice = true;
        Status(dialog, L"推論先の切り替えが完了したら設定画面を閉じます。");
    } else {
        EndDialog(dialog, 0);
    }
}

INT_PTR CALLBACK DialogProcedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        SendDlgItemMessageW(dialog, IDC_BACKEND, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"llama.cpp"));
        SendDlgItemMessageW(dialog, IDC_BACKEND, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ollama"));
        SendDlgItemMessageW(dialog, IDC_DEVICE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"GPU"));
        SendDlgItemMessageW(dialog, IDC_DEVICE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"CPU"));
        for (const auto* format : {L"自動（モデル名から判定）", L"汎用チャット", L"PLaMo 2 Translate", L"PLaMo 3 Translate"}) {
            SendDlgItemMessageW(dialog, IDC_PROMPT_FORMAT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(format));
        }
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        EnableWindow(GetDlgItem(dialog, IDC_RELOAD), FALSE);
        SetDlgItemTextW(dialog, IDC_CONFIG_PATH, state->initialPath.c_str());
        Load(dialog, *state, state->initialPath);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_TIMER && wParam == IDC_FETCH_MODELS) { ApplyModels(dialog, *state); return TRUE; }
    if (message == WM_TIMER && wParam == IDC_DEVICE) { FinishDevice(dialog, *state); return TRUE; }
    if (message == WM_DESTROY) { CancelCatalog(dialog, *state); return TRUE; }
    if (message == WM_CLOSE) {
        CloseDialog(dialog, *state);
        return TRUE;
    }
    if (message != WM_COMMAND) return FALSE;
    const int id = LOWORD(wParam);
    const int notification = HIWORD(wParam);
    if (id == IDOK) { Save(dialog, *state); return TRUE; }
    if (id == IDC_FETCH_MODELS) { if (state->loaded) FetchModels(dialog, *state); return TRUE; }
    if (id == IDCANCEL) {
        CloseDialog(dialog, *state);
        return TRUE;
    }
    if (id == IDC_RELOAD) {
        if (ConfirmChanges(dialog, *state) && !state->deviceRequest) Load(dialog, *state, state->document.path);
        return TRUE;
    }
    if (id == IDC_BROWSE) {
        if (!ConfirmChanges(dialog, *state)) return TRUE;
        if (state->deviceRequest) return TRUE;
        wchar_t path[32768]{};
        OPENFILENAMEW file{};
        file.lStructSize = sizeof(file);
        file.hwndOwner = dialog;
        file.lpstrFilter = L"JSON設定ファイル\0*.json\0すべてのファイル\0*.*\0";
        file.lpstrFile = path;
        file.nMaxFile = ARRAYSIZE(path);
        file.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&file)) Load(dialog, *state, path);
        return TRUE;
    }
    if (state->updating || !state->loaded) return FALSE;
    UpdateDeviceControl(dialog, *state);
    if (id == IDC_BACKEND && notification == CBN_SELCHANGE) {
        CancelCatalog(dialog, *state);
        state->updating = true;
        const bool llamaCpp = SendDlgItemMessageW(dialog, IDC_BACKEND, CB_GETCURSEL, 0, 0) == 0;
        const bool plamo = SendDlgItemMessageW(dialog, IDC_PROMPT_FORMAT, CB_GETCURSEL, 0, 0) >= 2;
        SetDlgItemInt(dialog, IDC_PORT, llamaCpp ? 8080 : 11434, FALSE);
        SetDlgItemTextW(dialog, IDC_PATH, llamaCpp
            ? (plamo ? L"/v1/completions" : L"/v1/chat/completions") : L"/api/generate");
        const auto model = Text(dialog, IDC_MODEL);
        if (model.empty() || model == L"default" || model == L"rtas" || model == L"llama3.1") {
            SetDlgItemTextW(dialog, IDC_MODEL, llamaCpp ? L"rtas" : L"llama3.1");
        }
        state->updating = false;
        state->dirty = true;
        SendDlgItemMessageW(dialog, IDC_MODEL, CB_RESETCONTENT, 0, 0);
        SetDlgItemTextW(dialog, IDC_MODEL, (model.empty() || model == L"default" || model == L"rtas" || model == L"llama3.1")
            ? (llamaCpp ? L"rtas" : L"llama3.1") : model.c_str());
        FetchModels(dialog, *state);
    } else if (id == IDC_DEVICE && notification == CBN_SELCHANGE) {
        state->dirty = true;
        Status(dialog, L"推論先を変更しました。保存するとローカルPLaMoサーバーを再起動します。");
    } else if (id == IDC_PROMPT_FORMAT && notification == CBN_SELCHANGE) {
        const auto format = SendDlgItemMessageW(dialog, IDC_PROMPT_FORMAT, CB_GETCURSEL, 0, 0);
        const auto path = Text(dialog, IDC_PATH);
        if (SendDlgItemMessageW(dialog, IDC_BACKEND, CB_GETCURSEL, 0, 0) == 0 &&
            (path == L"/v1/chat/completions" || path == L"/v1/completions")) {
            SetDlgItemTextW(dialog, IDC_PATH, format >= 2 ? L"/v1/completions" : L"/v1/chat/completions");
        }
        state->dirty = true;
        Status(dialog, L"翻訳形式を変更しました。モデルと形式を確認して保存してください。");
    } else if (id == IDC_MODEL && (notification == CBN_SELCHANGE || notification == CBN_EDITCHANGE)) {
        state->dirty = true;
        Status(dialog, L"モデルを変更しました。保存すると反映されます。");
    } else if ((notification == EN_CHANGE && id >= IDC_MODEL && id <= IDC_TIMEOUT) ||
               ((id == IDC_TLS || id == IDC_KANA_ONLY) && notification == BN_CLICKED)) {
        state->dirty = true;
        if (id == IDC_HOST || id == IDC_PORT || id == IDC_TLS) CancelCatalog(dialog, *state);
        Status(dialog, L"変更があります。保存すると設定ファイルに反映されます。");
    }
    return TRUE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t module[32768]{};
        if (!GetModuleFileNameW(nullptr, module, ARRAYSIZE(module))) return 1;
        const auto folder = std::filesystem::path(module).parent_path();
        DialogState state;
        state.initialPath = ime::config::RegisteredSettingsPath();
        if (state.initialPath.empty()) {
            state.initialPath = folder / L"config/ime_settings.json";
            if (!std::filesystem::exists(state.initialPath)) {
                const auto source = folder / L"../../config/ime_settings.json";
                if (std::filesystem::exists(source)) state.initialPath = std::filesystem::canonical(source);
            }
        }
        int count = 0;
        wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (args) {
            if (count == 3 && std::wstring(args[1]) == L"--config") state.initialPath = args[2];
            LocalFree(args);
        }
        return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr,
                               DialogProcedure, reinterpret_cast<LPARAM>(&state)) == -1 ? 1 : 0;
    } catch (const std::exception&) {
        MessageBoxW(nullptr, L"設定画面を開けません。設定ファイルの場所と内容を確認してください。",
                    L"RTAS 設定", MB_OK | MB_ICONERROR);
        return 1;
    }
}
