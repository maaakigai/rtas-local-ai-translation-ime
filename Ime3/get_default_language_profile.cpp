#include <windows.h>
#include <msctf.h>
#include <iostream>
#include <string>
#include <iterator>

#pragma comment(lib, "ole32.lib")

namespace {
std::wstring GuidToString(REFGUID guid) {
    wchar_t buffer[64];
    if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) == 0) {
        return L"<StringFromGUID2 failed>";
    }
    return buffer;
}

void PrintUsage(const wchar_t* executableName) {
    std::wcout << L"Usage: " << executableName << L" [langid-hex]" << std::endl;
    std::wcout << L"  langid-hex: Optional hexadecimal LANGID (e.g. 0411 for Japanese)."
                << std::endl;
}
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    LANGID langId = GetUserDefaultUILanguage();
    if (argc > 1) {
        wchar_t* endPtr = nullptr;
        unsigned long value = std::wcstoul(argv[1], &endPtr, 16);
        if (endPtr == argv[1] || *endPtr != L'\0' || value > 0xFFFF) {
            PrintUsage(argv[0]);
            return 1;
        }
        langId = static_cast<LANGID>(value);
    }

    std::wcout << L"Querying default language profile for LANGID 0x"
               << std::hex << std::uppercase << static_cast<unsigned int>(langId)
               << std::dec << std::nouppercase << std::endl;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needCoUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::wcerr << L"CoInitializeEx failed: 0x" << std::hex << hr << std::dec << std::endl;
        return 1;
    }

    ITfInputProcessorProfiles* profiles = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
    if (FAILED(hr)) {
        std::wcerr << L"CoCreateInstance(CLSID_TF_InputProcessorProfiles) failed: 0x" << std::hex
                   << hr << std::dec << std::endl;
        if (needCoUninit) CoUninitialize();
        return 1;
    }

    CLSID clsid{};
    GUID profileGuid{};
    hr = profiles->GetDefaultLanguageProfile(langId, GUID_TFCAT_TIP_KEYBOARD, &clsid, &profileGuid);
    if (SUCCEEDED(hr)) {
        std::wcout << L"Default Text Service CLSID : " << GuidToString(clsid) << std::endl;
        std::wcout << L"Default Profile GUID      : " << GuidToString(profileGuid) << std::endl;    } else {
        std::wcerr << L"GetDefaultLanguageProfile failed: 0x" << std::hex << hr << std::dec << std::endl;
#ifdef TF_E_PROFILE_NOT_REGISTERED
        if (hr == TF_E_PROFILE_NOT_REGISTERED) {
            std::wcerr << L"(No default profile registered for this LANGID)" << std::endl;
        }
#endif
    }

    if (profiles) profiles->Release();
    if (needCoUninit) CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}

