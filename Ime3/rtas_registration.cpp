#include "rtas_registration.h"

#include <msctf.h>
#include <ctfutb.h>
#include <strsafe.h>

#include "rtas_factory.h"
#include "rtas_text_service.h"
#include "rtas_utils.h"
#include "..\resource_ids.h"

HMODULE g_hModule = nullptr;
long g_cDllRef = 0;

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = (HMODULE)hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

extern "C" STDAPI DllCanUnloadNow() {
    return (g_cDllRef == 0) ? S_OK : S_FALSE;
}

extern "C" STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid != CLSID_TsfIme) return CLASS_E_CLASSNOTAVAILABLE;
    return ClassFactory::Create(riid, ppv);
}

static BOOL GetModulePathW(WCHAR* path, DWORD cch) {
    DWORD len = GetModuleFileNameW(g_hModule, path, cch);
    return len > 0 && len < cch;
}

static BOOL SetRegSZ(HKEY root, const WCHAR* subkey, const WCHAR* name, const WCHAR* value) {
    HKEY h;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return FALSE;
    const DWORD cb = (DWORD)((wcslen(value) + 1) * sizeof(WCHAR));
    LONG r = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)value, cb);
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

static BOOL RegisterComClass() {
    WCHAR clsidStr[64];
    StringFromGUID2(CLSID_TsfIme, clsidStr, ARRAYSIZE(clsidStr));
    WCHAR dllPath[MAX_PATH];
    if (!GetModulePathW(dllPath, ARRAYSIZE(dllPath))) return FALSE;
    WCHAR key[256];
    StringCchPrintfW(key, ARRAYSIZE(key), L"CLSID\\%s", clsidStr);
    if (!SetRegSZ(HKEY_CLASSES_ROOT, key, nullptr, kImeDisplayName)) return FALSE;
    StringCchPrintfW(key, ARRAYSIZE(key), L"CLSID\\%s\\InprocServer32", clsidStr);
    if (!SetRegSZ(HKEY_CLASSES_ROOT, key, nullptr, dllPath)) return FALSE;
    if (!SetRegSZ(HKEY_CLASSES_ROOT, key, L"ThreadingModel", L"Both")) return FALSE;
    return TRUE;
}

static BOOL UnregisterComClass() {
    WCHAR clsidStr[64];
    StringFromGUID2(CLSID_TsfIme, clsidStr, ARRAYSIZE(clsidStr));
    WCHAR key[256];
    StringCchPrintfW(key, ARRAYSIZE(key), L"CLSID\\%s", clsidStr);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key);
    return TRUE;
}

static HRESULT RegisterWithTSF() {
    ITfInputProcessorProfiles* prof = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, (void**)&prof);
    if (FAILED(hr)) {
        DebugLog(L"CoCreateInstance for ITfInputProcessorProfiles failed", hr);
        return hr;
    }
    DebugLog(L"ITfInputProcessorProfiles successfully created");

    hr = prof->Register(CLSID_TsfIme);
    if (SUCCEEDED(hr)) {
        const ULONG descLength = static_cast<ULONG>(wcslen(kImeDisplayName) + 1);
        // Do not supply a static icon so the host can render our LangBarItem's text.
        hr = prof->AddLanguageProfile(
            CLSID_TsfIme, kLangId, GUID_TsfImeProfile,
            kImeDisplayName, descLength,
            nullptr, 0, 0);
        if (SUCCEEDED(hr)) {
            DebugLog(L"Language profile successfully added");
            HRESULT hrEnable = prof->EnableLanguageProfile(CLSID_TsfIme, kLangId, GUID_TsfImeProfile, TRUE);
            if (SUCCEEDED(hrEnable)) DebugLog(L"EnableLanguageProfile succeeded");
            else DebugLog(L"EnableLanguageProfile failed", hrEnable);

            HKL hKL = LoadKeyboardLayoutW(L"E0200411", KLF_NOTELLSHELL);
            if (!hKL) hKL = LoadKeyboardLayoutW(L"00000411", KLF_NOTELLSHELL);
            if (hKL) {
                HRESULT hrSub = prof->SubstituteKeyboardLayout(CLSID_TsfIme, kLangId, GUID_TsfImeProfile, hKL);
                if (SUCCEEDED(hrSub)) DebugLog(L"SubstituteKeyboardLayout succeeded");
                else DebugLog(L"SubstituteKeyboardLayout failed", hrSub);
            }
            else {
                DebugLog(L"LoadKeyboardLayout for JP failed; skipping SubstituteKeyboardLayout");
            }

        }
        else {
            DebugLog(L"Failed to add language profile", hr);
        }
    }
    else {
        DebugLog(L"Failed to register TextService CLSID", hr);
    }

    if (prof) prof->Release();
    return hr;
}

static HRESULT RegisterCategoriesTSF() {
    ITfCategoryMgr* cat = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, (void**)&cat);
    if (FAILED(hr)) {
        DebugLog(L"CoCreateInstance for CategoryMgr failed", hr);
        return hr;
    }
    cat->RegisterCategory(CLSID_TsfIme, GUID_TFCAT_TIP_KEYBOARD, CLSID_TsfIme);
    cat->RegisterCategory(CLSID_TsfIme, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, CLSID_TsfIme);
    cat->RegisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_UIELEMENTENABLED, CLSID_TsfIme);
    cat->RegisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, CLSID_TsfIme);
#ifdef GUID_TFCAT_TIPCAP_IMEMODE
    // Only available on newer SDKs; wrap to keep older SDKs building.
    cat->RegisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_IMEMODE, CLSID_TsfIme);
#endif
    cat->RegisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_COMLESS, CLSID_TsfIme);
    cat->RegisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_TsfIme);
    cat->RegisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_TsfIme);

    DebugLog(L"RegisterCategoriesTSF succeeded", hr);

    cat->Release();
    return S_OK;
}

static HRESULT UnregisterFromTSF() {
    ITfInputProcessorProfiles* prof = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, (void**)&prof);
    if (FAILED(hr)) {
        DebugLog(L"CoCreateInstance for InputProcessorProfiles failed", hr);
        return hr;
    }

    prof->Unregister(CLSID_TsfIme);
    prof->Release();

    ITfCategoryMgr* cat = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, (void**)&cat))) {
        cat->UnregisterCategory(CLSID_TsfIme, GUID_TFCAT_TIP_KEYBOARD, CLSID_TsfIme);
        cat->UnregisterCategory(CLSID_TsfIme, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, CLSID_TsfIme);
        cat->UnregisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_UIELEMENTENABLED, CLSID_TsfIme);
        cat->UnregisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, CLSID_TsfIme);
#ifdef GUID_TFCAT_TIPCAP_IMEMODE
        cat->UnregisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_IMEMODE, CLSID_TsfIme);
#endif
        cat->UnregisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_COMLESS, CLSID_TsfIme);
        cat->UnregisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_TsfIme);
        cat->UnregisterCategory(CLSID_TsfIme, GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_TsfIme);
        cat->Release();
    }
    DebugLog(L"CoCreateInstance for InputProcessorProfiles succeeded");
    return S_OK;
}

extern "C" STDAPI DllRegisterServer() {
    DebugLog(L"DllRegisterServer called");
    HRESULT hr = CoInitialize(nullptr);
    const bool needCoUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr;

    BOOL ok = RegisterComClass();
    if (ok) {
        hr = RegisterWithTSF();
        if (SUCCEEDED(hr)) hr = RegisterCategoriesTSF();
    }
    else {
        hr = E_FAIL;
    }

    if (needCoUninit) CoUninitialize();
    return hr;
}

extern "C" STDAPI DllUnregisterServer() {
    HRESULT hr = CoInitialize(nullptr);
    const bool needCoUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr;

    UnregisterFromTSF();
    UnregisterComClass();

    if (needCoUninit) CoUninitialize();
    return S_OK;
}

