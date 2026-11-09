#pragma once

#include <windows.h>
#include <unknwn.h>

#include "rtas_globals.h"
#include "rtas_text_service.h"


class ClassFactory final : public IClassFactory {
public:
    static HRESULT Create(REFIID riid, void** ppv);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* punkOuter, REFIID riid, void** ppvObject) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

private:
    ClassFactory() = default;
    ComModuleRef m_moduleRef;
    LONG m_ref{ 1 };
};
