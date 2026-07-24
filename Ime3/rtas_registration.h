#pragma once

#include <windows.h>

#include "rtas_globals.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved);
extern "C" STDAPI DllCanUnloadNow();
extern "C" STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv);
extern "C" STDAPI DllRegisterServer();
extern "C" STDAPI DllUnregisterServer();
