#define _WIN32_WINNT 0x0601

#include "rtas_text_service.h"
#include <strsafe.h>
#include "..\resource_ids.h"

#ifndef TF_LBI_ATTR_STATUS
#define TF_LBI_ATTR_STATUS 0x00000001u
#endif
#ifndef TF_LBI_ATTR_TEXTCOLORICON
#define TF_LBI_ATTR_TEXTCOLORICON 0x00000002u
#endif
#ifndef TF_LBI_STATUS
#define TF_LBI_STATUS 0x00000001u
#endif
#ifndef TF_LBI_ICON
#define TF_LBI_ICON 0x00000002u
#endif
#ifndef TF_LBI_STYLE_TEXTCOLORICON
#define TF_LBI_STYLE_TEXTCOLORICON 0x00000010u
#endif
#ifndef CONNECT_E_ADVISELIMIT
#define CONNECT_E_ADVISELIMIT ((HRESULT)0x80040201L)
#endif

#pragma comment(lib, "Imm32.lib")


const CLSID CLSID_TsfIme =
{ 0x64ad179e, 0xadbc, 0x4bec, {0xb3, 0xe8, 0x26, 0x09, 0x97, 0x33, 0x3d, 0xe7} };

const GUID GUID_TsfImeProfile =
{ 0xa878c4e2, 0x6c78, 0x4a94, {0x81, 0x32, 0xc0, 0xd5, 0xb4, 0x33, 0xf2, 0x6f} };

const GUID GUID_LangBarButton =
{ 0x36c54a2d, 0x408d, 0x4d35, {0x9a, 0x8f, 0x2e, 0x49, 0x13, 0xd5, 0x64, 0x2f} };

const GUID GUID_DA_PREEDIT =
{ 0x8e294c6e, 0x39f9, 0x4d52, {0x9c, 0x52, 0x74, 0xb9, 0xd6, 0xb4, 0x69, 0x2b} };

const GUID GUID_DA_LAYER2 =
{ 0xb68a1b7b, 0x1c31, 0x4d73, {0x8e, 0x84, 0x8f, 0x93, 0x2f, 0x87, 0x1e, 0x4d} };

const GUID GUID_DA_TRANSLATION =
{ 0x4dd7eab6, 0xdfa3, 0x4f3f, {0x9a, 0x96, 0x7b, 0x14, 0x6f, 0x5a, 0xcd, 0x41} };

const GUID GUID_DA_SEGMENT =
{ 0xe1975112, 0x8868, 0x4cd7, {0x98, 0x6d, 0x58, 0xc5, 0x16, 0xe8, 0x95, 0x3f} };

const GUID GUID_DA_SEGMENT_ACTIVE =
{ 0x2f9a58fd, 0x4a9e, 0x4e20, {0x88, 0x34, 0x4f, 0x3a, 0x5a, 0x31, 0x9b, 0x62} };

const WCHAR kImeDisplayName[] = L"RTAS";
const LANGID kLangId = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
const UINT kToggleVk = 0xF4;

const std::array<PreservedKeyDef, 9> kPreservedKeyDefs = { {
    { &GUID_Preserved_Toggle, kToggleVk, L"Toggle" },
    { &GUID_Preserved_KANA, VK_KANA, L"KANA" },
    { &GUID_Preserved_KANJI, VK_KANJI, L"KANJI" },
    { &GUID_Preserved_DBE_ALPHANUMERIC, VK_DBE_ALPHANUMERIC, L"ALPHANUM" },
    { &GUID_Preserved_DBE_HIRAGANA, VK_DBE_HIRAGANA, L"HIRAGANA" },
    { &GUID_Preserved_DBE_KATAKANA, VK_DBE_KATAKANA, L"KATAKANA" },
    { &GUID_Preserved_DBE_SBCSCHAR, VK_DBE_SBCSCHAR, L"SBCS" },
    { &GUID_Preserved_DBE_DBCSCHAR, VK_DBE_DBCSCHAR, L"DBCS" },
    { &GUID_Preserved_DBE_ROMAN, VK_DBE_ROMAN, L"ROMAN" }
} };


LangBarButton::LangBarButton(TextService* owner) : m_owner(owner), m_text(kImeDisplayName) {}

bool LangBarButton::EnsureResourceIcons() {
    if (m_iconOn && m_iconOff) return true;
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    if (!m_iconOn) {
        m_iconOn = reinterpret_cast<HICON>(LoadImageW(g_hModule, MAKEINTRESOURCEW(IDI_RTAS_MODE_ON), IMAGE_ICON, cx, cy, 0));
    }
    if (!m_iconOff) {
        m_iconOff = reinterpret_cast<HICON>(LoadImageW(g_hModule, MAKEINTRESOURCEW(IDI_RTAS_MODE_OFF), IMAGE_ICON, cx, cy, 0));
    }
    if (!m_iconOn) {
        m_iconOn = reinterpret_cast<HICON>(LoadImageW(g_hModule, MAKEINTRESOURCEW(IDI_RTAS_ICON), IMAGE_ICON, cx, cy, 0));
    }
    if (!m_iconOff) {
        m_iconOff = reinterpret_cast<HICON>(LoadImageW(g_hModule, MAKEINTRESOURCEW(IDI_RTAS_ICON), IMAGE_ICON, cx, cy, 0));
    }
    return (m_iconOn || m_iconOff);
}


void LangBarButton::NotifySink(DWORD dwFlags) {
    if (m_sink) {
        m_sink->OnUpdate(dwFlags);
    }
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, __uuidof(ITfLangBarItem)) ||
        IsEqualIID(riid, __uuidof(ITfLangBarItemButton))) {
        *ppv = static_cast<ITfLangBarItemButton*>(this);
    } else if (IsEqualIID(riid, __uuidof(ITfSource))) {
        *ppv = static_cast<ITfSource*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_ref));
}

STDMETHODIMP_(ULONG) LangBarButton::Release() {
    ULONG ref = static_cast<ULONG>(InterlockedDecrement(&m_ref));
    if (!ref) {
        if (m_sink) {
            m_sink->Release();
            m_sink = nullptr;
        }
        if (m_iconOn) { DestroyIcon(m_iconOn); m_iconOn = nullptr; }
        if (m_iconOff) { DestroyIcon(m_iconOff); m_iconOff = nullptr; }
        delete this;
    }
    return ref;
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
    if (!pInfo) return E_POINTER;
    ZeroMemory(pInfo, sizeof(*pInfo));
    pInfo->clsidService = CLSID_TsfIme;
    pInfo->guidItem = GUID_LangBarButton;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_TOGGLE |
                     TF_LBI_STYLE_SHOWNINTRAY | TF_LBI_STYLE_TEXTCOLORICON;
    pInfo->ulSort = 0;
    StringCchCopyW(pInfo->szDescription, ARRAYSIZE(pInfo->szDescription), kImeDisplayName);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* pdwStatus) {
    if (!pdwStatus) return E_POINTER;
    *pdwStatus = m_status;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL fShow) {
    DWORD newStatus = m_status;
    if (fShow) {
        newStatus &= ~TF_LBI_STATUS_HIDDEN;
    } else {
        newStatus |= TF_LBI_STATUS_HIDDEN;
    }
    if (newStatus != m_status) {
        m_status = newStatus;
        NotifySink(TF_LBI_STATUS | TF_LBI_ICON | TF_LBI_ATTR_STATUS | TF_LBI_ATTR_TEXTCOLORICON);
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* pbstrToolTip) {
    if (!pbstrToolTip) return E_POINTER;
    *pbstrToolTip = SysAllocString(kImeDisplayName);
    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT, const RECT*) {
    if (click != TF_LBI_CLK_LEFT || !m_owner) {
        return S_OK;
    }
    m_owner->ToggleKanaMode(nullptr);
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu*) {
    return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(UINT) {
    return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON* phIcon) {
    if (!phIcon) return E_POINTER;
    HICON icon = nullptr;
    if (EnsureResourceIcons()) {
        const bool on = (m_status & TF_LBI_STATUS_BTN_TOGGLED) != 0;
        icon = on ? m_iconOn : m_iconOff;
        if (!icon) icon = m_iconOn ? m_iconOn : m_iconOff;
    }
    *phIcon = icon;
    return icon ? S_OK : S_FALSE;
}

void LangBarButton::SetDisplayText(const wchar_t* text) {
    std::wstring newText = text ? text : L"";
    if (newText.empty()) {
        newText = kImeDisplayName;
    }
    if (m_text != newText) {
        m_text = newText;
        NotifySink(TF_LBI_ICON | TF_LBI_ATTR_TEXTCOLORICON);
    }
}

STDMETHODIMP LangBarButton::GetText(BSTR* pbstrText) {
    if (!pbstrText) return E_POINTER;
    const wchar_t* text = m_text.empty() ? kImeDisplayName : m_text.c_str();
    *pbstrText = SysAllocString(text);
    return *pbstrText ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) {
    if (!punk || !pdwCookie) return E_POINTER;
    if (!IsEqualIID(riid, __uuidof(ITfLangBarItemSink))) return E_INVALIDARG;
    if (m_sink) return CONNECT_E_ADVISELIMIT;
    HRESULT hr = punk->QueryInterface(IID_PPV_ARGS(&m_sink));
    if (FAILED(hr)) return hr;
    m_sinkCookie = 1;
    *pdwCookie = m_sinkCookie;
    NotifySink(TF_LBI_STATUS | TF_LBI_ICON | TF_LBI_ATTR_STATUS | TF_LBI_ATTR_TEXTCOLORICON);
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD dwCookie) {
    if (dwCookie != m_sinkCookie) return E_INVALIDARG;
    if (m_sink) {
        m_sink->Release();
        m_sink = nullptr;
    }
    m_sinkCookie = TF_INVALID_COOKIE;
    return S_OK;
}

void LangBarButton::SetToggled(bool toggled) {
    DWORD newStatus = toggled ? (m_status | TF_LBI_STATUS_BTN_TOGGLED)
                              : (m_status & ~TF_LBI_STATUS_BTN_TOGGLED);
    if (newStatus != m_status) {
        m_status = newStatus;
        NotifySink(TF_LBI_STATUS | TF_LBI_ICON | TF_LBI_ATTR_STATUS | TF_LBI_ATTR_TEXTCOLORICON);
    }
}

void TextService::RegisterLangBarButton() {
    if (m_langBarButton || !m_ptm) {
        return;
    }
    ITfLangBarItemMgr* mgr = nullptr;
    if (FAILED(m_ptm->QueryInterface(IID_PPV_ARGS(&mgr))) || !mgr) {
        return;
    }
    auto* button = new (std::nothrow) LangBarButton(this);
    if (!button) {
        mgr->Release();
        return;
    }
    HRESULT hr = mgr->AddItem(button);
    if (SUCCEEDED(hr)) {
        if (m_langBarMgr.p) {
            m_langBarMgr.p->Release();
        }
        m_langBarMgr.p = mgr;
        m_langBarButton = button;
        m_langBarButton->AddRef();
        m_langBarButton->SetToggled(m_kanaMode);
        // Use explicit code point to avoid source-encoding issues.
        m_langBarButton->SetDisplayText(m_kanaMode ? L"\u3042" : L"A");
        button->Release();
        return;
    }
    button->Release();
    mgr->Release();
}

void TextService::UnregisterLangBarButton() {
    if (m_langBarMgr.p && m_langBarButton) {
        m_langBarMgr.p->RemoveItem(m_langBarButton);
        m_langBarButton->Release();
        m_langBarButton = nullptr;
    } else if (m_langBarButton) {
        m_langBarButton->Release();
        m_langBarButton = nullptr;
    }
    if (m_langBarMgr.p) {
        m_langBarMgr.p->Release();
        m_langBarMgr.p = nullptr;
    }
}

void TextService::UpdateLangBarButtonToggle() {
    if (m_langBarButton) {
        m_langBarButton->SetToggled(m_kanaMode);
        m_langBarButton->SetDisplayText(m_kanaMode ? L"\u3042" : L"A");
    }
}

ATOM TextService::s_asyncDispatchClass = 0;
STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = nullptr;

    auto* layer1 = new (std::nothrow) DisplayAttributeInfo(GUID_DA_PREEDIT, L"Layer1 preedit", RGB(0x25, 0x63, 0xEB), TF_LS_SOLID);
    auto* layer2 = new (std::nothrow) DisplayAttributeInfo(GUID_DA_LAYER2, L"Layer2 paraphrase", RGB(0xD9, 0x77, 0x06), TF_LS_DOT);
    auto* translation = new (std::nothrow) DisplayAttributeInfo(GUID_DA_TRANSLATION, L"Translation preview", RGB(0x10, 0xB9, 0x81), TF_LS_SOLID);
    auto* segment = new (std::nothrow) DisplayAttributeInfo(GUID_DA_SEGMENT, L"Layer1 segment", RGB(0xEF, 0x44, 0x44), TF_LS_DOT);
    auto* segmentActive = new (std::nothrow) DisplayAttributeInfo(GUID_DA_SEGMENT_ACTIVE, L"Layer1 active segment", RGB(0xDC, 0x26, 0x26), TF_LS_SOLID);
    if (!layer1 || !layer2 || !translation || !segment || !segmentActive) {
        if (layer1) layer1->Release();
        if (layer2) layer2->Release();
        if (translation) translation->Release();
        if (segment) segment->Release();
        if (segmentActive) segmentActive->Release();
        return E_OUTOFMEMORY;
    }

    std::vector<ITfDisplayAttributeInfo*> infos{ layer1, layer2, translation, segment, segmentActive };
    auto* enumerator = new (std::nothrow) DisplayAttributeEnumerator(infos);
    layer1->Release();
    layer2->Release();
    translation->Release();
    segment->Release();
    segmentActive->Release();
    if (!enumerator) return E_OUTOFMEMORY;

    *ppEnum = enumerator;
    return S_OK;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_POINTER;
    *ppInfo = nullptr;

    if (IsEqualGUID(guid, GUID_DA_PREEDIT)) {
        auto* info = new (std::nothrow) DisplayAttributeInfo(GUID_DA_PREEDIT, L"Layer1 preedit", RGB(0x25, 0x63, 0xEB), TF_LS_SOLID);
        if (!info) return E_OUTOFMEMORY;
        *ppInfo = info;
        return S_OK;
    }
    if (IsEqualGUID(guid, GUID_DA_LAYER2)) {
        auto* info = new (std::nothrow) DisplayAttributeInfo(GUID_DA_LAYER2, L"Layer2 paraphrase", RGB(0xD9, 0x77, 0x06), TF_LS_DOT);
        if (!info) return E_OUTOFMEMORY;
        *ppInfo = info;
        return S_OK;
    }
    if (IsEqualGUID(guid, GUID_DA_TRANSLATION)) {
        auto* info = new (std::nothrow) DisplayAttributeInfo(GUID_DA_TRANSLATION, L"Translation preview", RGB(0x10, 0xB9, 0x81), TF_LS_SOLID);
        if (!info) return E_OUTOFMEMORY;
        *ppInfo = info;
        return S_OK;
    }
    if (IsEqualGUID(guid, GUID_DA_SEGMENT)) {
        auto* info = new (std::nothrow) DisplayAttributeInfo(GUID_DA_SEGMENT, L"Layer1 segment", RGB(0xEF, 0x44, 0x44), TF_LS_DOT);
        if (!info) return E_OUTOFMEMORY;
        *ppInfo = info;
        return S_OK;
    }
    if (IsEqualGUID(guid, GUID_DA_SEGMENT_ACTIVE)) {
        auto* info = new (std::nothrow) DisplayAttributeInfo(GUID_DA_SEGMENT_ACTIVE, L"Layer1 active segment", RGB(0xDC, 0x26, 0x26), TF_LS_SOLID);
        if (!info) return E_OUTOFMEMORY;
        *ppInfo = info;
        return S_OK;
    }
    return E_INVALIDARG;
}

