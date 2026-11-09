#pragma once

#include <windows.h>

extern HMODULE g_hModule;
inline constexpr wchar_t kOverlayClass[] = L"TSF_PreeditOverlay";
inline constexpr wchar_t kCandidateClass[] = L"RTAS_CandidatePopup";
inline constexpr wchar_t kAsyncDispatchClass[] = L"TSF_AsyncDispatch";

class ComModuleRef final {
public:
    ComModuleRef() { Acquire(); }
    ~ComModuleRef() { Release(); }
    ComModuleRef(const ComModuleRef&) = delete;
    ComModuleRef& operator=(const ComModuleRef&) = delete;

    static void Acquire();
    static void Release();
};

