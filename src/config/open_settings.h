#pragma once

#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>

#pragma comment(lib, "shell32.lib")

inline bool OpenRtasSettings(HWND owner, HMODULE module) {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module, path, ARRAYSIZE(path));
    if (length == 0 || length >= ARRAYSIZE(path)) return false;
    const auto root = std::filesystem::path(path).parent_path();
    const auto executable = root / L"RTASSettings.exe";
    const auto config = root / L"config/ime_settings.json";
    const std::wstring arguments = L"--config \"" + config.wstring() + L"\"";
    const auto result = ShellExecuteW(owner, L"open", executable.c_str(), arguments.c_str(), root.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(owner, L"設定画面を開けません。IMEと同じフォルダにRTASSettings.exeがあることを確認してください。",
                    L"RTAS 設定", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}
