#include "rtas_factory.h"

HRESULT ClassFactory::Create(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    auto* factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_ref));
}

STDMETHODIMP_(ULONG) ClassFactory::Release() {
    ULONG ref = static_cast<ULONG>(InterlockedDecrement(&m_ref));
    if (!ref) {
        delete this;
    }
    return ref;
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* punkOuter, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    if (punkOuter) return CLASS_E_NOAGGREGATION;

    ComModuleRef moduleRef;
    auto* service = new (std::nothrow) TextService();
    if (!service) return E_OUTOFMEMORY;
    HRESULT hr = service->QueryInterface(riid, ppvObject);
    service->Release();
    return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL fLock) {
    if (fLock) {
        InterlockedIncrement(&g_cDllRef);
    }
    else {
        InterlockedDecrement(&g_cDllRef);
    }
    return S_OK;
}
