#include <windows.h>
#include <imm.h>
#include <stdio.h>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        wprintf(L"Usage: ImmInstall <path-to-imm32-ime.ime> [DisplayName]\n");
        return 1;
    }
    const wchar_t* path = argv[1];
    const wchar_t* name = (argc >= 3) ? argv[2] : L"My IMM32 IME";
    HKL hkl = ImmInstallIMEW(path, name);
    if (!hkl) {
        DWORD err = GetLastError();
        wprintf(L"ImmInstallIMEW failed (error=%lu)\n", err);
        return 2;
    }
    wprintf(L"ImmInstallIMEW OK (HKL=%p)\n", hkl);
    return 0;
}

