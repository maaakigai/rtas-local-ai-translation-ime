#pragma once
#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>
#include <imm.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <locale>
#include <new>
#include <filesystem>
#include <utility>
#include "rtas_globals.h"
#include "rtas_utils.h"
#include "rtas_virtual_keys.h"
#include "rtas_translation.h"
#include "rtas_ui.h"
#include "../src/config/provider_settings.h"
#include "../src/api/conversion_provider.h"
#include "../src/provider/conversion_provider_factory.h"
class ClassFactory;
class CandidateUI;
class OverlayController;
class TextService;
extern const CLSID CLSID_TsfIme;
extern const GUID GUID_TsfImeProfile;
extern const GUID GUID_LangBarButton;
extern const GUID GUID_DA_PREEDIT;
extern const GUID GUID_DA_LAYER2;
extern const GUID GUID_DA_TRANSLATION;
extern const GUID GUID_DA_SEGMENT;
extern const GUID GUID_DA_SEGMENT_ACTIVE;
extern const GUID GUID_Preserved_Toggle;
extern const GUID GUID_Preserved_KANA;
extern const GUID GUID_Preserved_KANJI;
extern const GUID GUID_Preserved_DBE_ALPHANUMERIC;
extern const GUID GUID_Preserved_DBE_HIRAGANA;
extern const GUID GUID_Preserved_DBE_KATAKANA;
extern const GUID GUID_Preserved_DBE_SBCSCHAR;
extern const GUID GUID_Preserved_DBE_DBCSCHAR;
extern const GUID GUID_Preserved_DBE_ROMAN;
extern const WCHAR kImeDisplayName[];
extern const LANGID kLangId;
extern const UINT kToggleVk;
struct PreservedKeyDef {
    const GUID* guid;
    UINT vkey;
    const wchar_t* label;
};
extern const std::array<PreservedKeyDef, 9> kPreservedKeyDefs;
class DisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    DisplayAttributeInfo(const GUID& guid, std::wstring description, COLORREF lineColor, TF_DA_LINESTYLE style)
        : _ref(1), _guid(guid), _description(std::move(description)) {
        ZeroMemory(&_attr, sizeof(_attr));
        _attr.crText.type = TF_CT_NONE;
        _attr.crBk.type = TF_CT_NONE;
        _attr.crLine.type = TF_CT_COLORREF;
        _attr.crLine.cr = lineColor;
        _attr.lsStyle = style;
        _attr.fBoldLine = FALSE;
        _attr.bAttr = TF_ATTR_INPUT;
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(ITfDisplayAttributeInfo)) {
            *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&_ref)); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = static_cast<ULONG>(InterlockedDecrement(&_ref));
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP GetGUID(GUID* pguid) override {
        if (!pguid) return E_POINTER;
        *pguid = _guid;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override {
        if (!pbstrDesc) return E_POINTER;
        *pbstrDesc = SysAllocString(_description.c_str());
        return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override {
        if (!pda) return E_POINTER;
        *pda = _attr;
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
    STDMETHODIMP Reset() override { return S_OK; }
private:
    LONG _ref;
    GUID _guid;
    std::wstring _description;
    TF_DISPLAYATTRIBUTE _attr{};
};
class DisplayAttributeEnumerator final : public IEnumTfDisplayAttributeInfo {
public:
    DisplayAttributeEnumerator(const std::vector<ITfDisplayAttributeInfo*>& infos) : _ref(1), _index(0) {
        _infos.reserve(infos.size());
        for (auto* info : infos) {
            if (info) {
                info->AddRef();
                _infos.push_back(info);
            }
        }
    }
    virtual ~DisplayAttributeEnumerator() {
        for (auto* info : _infos) {
            if (info) info->Release();
        }
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IEnumTfDisplayAttributeInfo)) {
            *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&_ref)); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = static_cast<ULONG>(InterlockedDecrement(&_ref));
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        auto* clone = new (std::nothrow) DisplayAttributeEnumerator(_infos);
        if (!clone) return E_OUTOFMEMORY;
        clone->_index = _index;
        *ppEnum = clone;
        return S_OK;
    }
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override {
        if (!rgInfo) return E_POINTER;
        ULONG fetched = 0;
        while (ulCount-- && _index < _infos.size()) {
            ITfDisplayAttributeInfo* info = _infos[_index++];
            info->AddRef();
            rgInfo[fetched++] = info;
        }
        if (pcFetched) *pcFetched = fetched;
        return fetched ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        _index = 0;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG celt) override {
        size_t remaining = _infos.size() - _index;
        if (celt > remaining) {
            _index = _infos.size();
            return S_FALSE;
        }
        _index += celt;
        return S_OK;
    }
private:
    LONG _ref;
    size_t _index;
    std::vector<ITfDisplayAttributeInfo*> _infos;
};
class LangBarButton final : public ITfLangBarItemButton, public ITfSource {
public:
    explicit LangBarButton(TextService* owner);
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;
    void SetToggled(bool toggled);
    void SetDisplayText(const wchar_t* text);
private:
    void NotifySink(DWORD dwFlags);
    bool EnsureResourceIcons();
    LONG m_ref = 1;
    TextService* m_owner = nullptr;
    ITfLangBarItemSink* m_sink = nullptr;
    DWORD m_sinkCookie = TF_INVALID_COOKIE;
    DWORD m_status = 0;
    std::wstring m_text;
    HICON m_iconOn = nullptr;
    HICON m_iconOff = nullptr;
};
class TextService final : public ITfTextInputProcessorEx, public ITfKeyEventSink, public ITfCompositionSink, public ITfDisplayAttributeProvider, public ITfCompartmentEventSink {
public:
    friend class ClassFactory;
    friend class OverlayController;
    friend class CandidateUI;
    friend class LangBarButton;
    std::wstring GetPreeditText() const { return DraftText() + m_romajiBuffer; }
    bool HasActiveComposition() const { return m_composition != nullptr; }
    bool TryGetLastKnownCaretScreenPoint(POINT* pt) const {
        if (!pt || !m_hasLastCaretScreenPoint) return false;
        *pt = m_lastCaretScreenPoint;
        return true;
    }
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ITfTextInputProcessor)) {
            *ppv = static_cast<ITfTextInputProcessor*>(this);
        }
        else if (riid == __uuidof(ITfTextInputProcessorEx)) {
            *ppv = static_cast<ITfTextInputProcessorEx*>(this);
        }
        else if (riid == __uuidof(ITfKeyEventSink)) {
            *ppv = static_cast<ITfKeyEventSink*>(this);
        }
        else if (riid == __uuidof(ITfCompositionSink)) {
            *ppv = static_cast<ITfCompositionSink*>(this);
        }
        else if (riid == __uuidof(ITfDisplayAttributeProvider)) {
            *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
        }
        else if (riid == __uuidof(ITfCompartmentEventSink)) {
            *ppv = static_cast<ITfCompartmentEventSink*>(this);
        }
        else {
            *ppv = nullptr; return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = (ULONG)InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }
    // ITfTextInputProcessorEx / ITfTextInputProcessor
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptm, TfClientId tid, DWORD /*dwFlags*/) override {
        // For RTAS we behave the same as Activate. dwFlags may include TF_TMF_* hints.
        return Activate(ptm, tid);
    }
    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* ptm, TfClientId tid) override {
        DebugLog(L"ITfThreadMgr passed into Activate");  // ←ここでログ出力
        m_ptm = ptm; m_tid = tid;
        if (m_ptm) m_ptm->AddRef();
        AppendModeToastDebugLog(L"==== Activate ====");
        // Advise as key event sink (optional minimal behavior)
        ComPtr<ITfKeystrokeMgr> km;
        if (SUCCEEDED(m_ptm->QueryInterface(IID_PPV_ARGS(&km)))) {
            km->AdviseKeyEventSink(m_tid, static_cast<ITfKeyEventSink*>(this), TRUE);
            // Register preserved keys so CUAS/IMM32 bridge routes these reliably
            for (const auto& def : kPreservedKeyDefs) {
                if (def.guid == &GUID_Preserved_DBE_DBCSCHAR && kToggleVk == VK_DBE_DBCSCHAR) {
                    continue;
                }
                TF_PRESERVEDKEY pk{};
                pk.uVKey = def.vkey;
                pk.uModifiers = 0;
                km->PreserveKey(m_tid, *def.guid, &pk, def.label, (ULONG)lstrlenW(def.label));
            }
        }
        InitializeCompartmentMonitoring();
        RegisterLangBarButton();
        // Register our display attribute GUID atom
        {
            ComPtr<ITfCategoryMgr> cat;
            if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cat)))) {
                cat->RegisterGUID(GUID_DA_PREEDIT, &m_gaDisplayAttrPreedit);
                cat->RegisterGUID(GUID_DA_LAYER2, &m_gaDisplayAttrLayer2);
                cat->RegisterGUID(GUID_DA_TRANSLATION, &m_gaDisplayAttrTranslation);
                cat->RegisterGUID(GUID_DA_SEGMENT, &m_gaDisplayAttrSegment);
                cat->RegisterGUID(GUID_DA_SEGMENT_ACTIVE, &m_gaDisplayAttrSegmentActive);
            }
        }
        if (!EnsureAsyncDispatchWindow()) { DebugLog(L"EnsureAsyncDispatchWindow failed"); }
        // Prepare overlay window (lazy-created on first use)
        InitializeProviderBackend();
        StartOllamaResidencyOnActivate();
        return S_OK;
    }
    STDMETHODIMP Deactivate() override {
        ShutdownCompartmentMonitoring();
        UnregisterLangBarButton();
        if (m_ptm) {
            ComPtr<ITfKeystrokeMgr> km;
            if (SUCCEEDED(m_ptm->QueryInterface(IID_PPV_ARGS(&km)))) {
                km->UnadviseKeyEventSink(m_tid);
                // Unregister preserved keys
                for (const auto& def : kPreservedKeyDefs) {
                    if (def.guid == &GUID_Preserved_DBE_DBCSCHAR && kToggleVk == VK_DBE_DBCSCHAR) {
                        continue;
                    }
                    TF_PRESERVEDKEY pk{};
                    pk.uVKey = def.vkey;
                    pk.uModifiers = 0;
                    km->UnpreserveKey(*def.guid, &pk);
                }
            }
            m_ptm->Release(); m_ptm = nullptr;
        }
        HideOverlay();
        DestroyOverlay();
        CancelAllTranslations();
        StopOllamaResidencyOnDeactivate();
        m_translationCache.clear();
        m_layer2Cache.clear();
        ClearCandidateSourceText();
        m_conversionProvider.reset();
        m_asyncQueue.Shutdown();
        DestroyAsyncDispatchWindow();
        m_tid = TF_CLIENTID_NULL;
        return S_OK;
    }
    // ITfKeyEventSink (hiragana mode with 0xF4 toggle)
    STDMETHODIMP OnSetFocus(BOOL) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wParam, LPARAM, BOOL* eaten) override {
        if (!eaten) return S_OK;
        const bool shiftPressed = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const UINT key = static_cast<UINT>(wParam);
        if (shiftPressed && (key == kToggleVk || key == VK_KANA || key == VK_KANJI)) {
            *eaten = TRUE;
            return S_OK;
        }
        // Treat only main toggle keys as preserved by us
        KanaModeCommand cmd = ClassifyKanaKey(key, kToggleVk);
        if (cmd != KanaModeCommand::None) { *eaten = TRUE; return S_OK; }
        const bool hasPreeditText = !PreeditText().empty();
        const bool hasUi = (m_candidateUI != nullptr);
        const bool hasActiveInput = hasPreeditText || hasUi;
        // If not in kana mode and no active composition, we don't handle keystrokes
        if (!m_kanaMode && !hasActiveInput) { *eaten = FALSE; return S_OK; }
        if (m_forceHalfWidthInput && hasActiveInput) {
            const bool isLetter = (wParam >= 'A' && wParam <= 'Z');
            const bool isHalfWidthSymbol = (MapHalfWidthSymbol(static_cast<UINT>(wParam), shiftPressed) != nullptr);
            if (isLetter || isHalfWidthSymbol || wParam == VK_SPACE) {
                *eaten = TRUE;
                return S_OK;
            }
        }
        // Letters are always handled in kana mode or during composition
        if ((wParam >= 'A' && wParam <= 'Z')) { *eaten = TRUE; return S_OK; }
        if (MapFullWidthSymbol(static_cast<UINT>(wParam), shiftPressed)) { *eaten = TRUE; return S_OK; }
        // Navigation/confirm keys are handled only when we actually have something in-flight
        if (hasActiveInput) {
            if (wParam == VK_BACK) { *eaten = TRUE; return S_OK; }
            if (wParam == VK_ESCAPE) { *eaten = TRUE; return S_OK; }
            if ((wParam == VK_LEFT || wParam == VK_RIGHT) && m_composition) { *eaten = TRUE; return S_OK; }
            if (wParam == VK_SPACE || wParam == VK_RETURN || wParam == VK_TAB || wParam == VK_UP || wParam == VK_DOWN) { *eaten = TRUE; return S_OK; }
        }
        *eaten = FALSE; return S_OK;
    }
    STDMETHODIMP OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override { if (eaten) *eaten = FALSE; return S_OK; }
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM, BOOL* eaten) override {
        if (eaten) *eaten = FALSE;
        const bool shiftPressed = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const UINT key = static_cast<UINT>(wParam);
        if (shiftPressed && (key == kToggleVk || key == VK_KANA || key == VK_KANJI)) {
            AppendModeToastDebugLog(
                L"OnKeyDown shift-toggle vkey=" + std::to_wstring(key) +
                L" ctx=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(context)));
            ToggleKanaKanjiOnlyMode(context);
            // Preserve-key callbacks may arrive after Shift is released.
            // Mark this key so the companion preserved event is ignored once.
            m_suppressPreservedKanaHandling = true;
            m_suppressedKanaVKey = key;
            if (eaten) *eaten = TRUE;
            return S_OK;
        }
        // Handle toggle keys
        KanaModeCommand cmd = ClassifyKanaKey(key, kToggleVk);
        if (cmd != KanaModeCommand::None) {
            AppendModeToastDebugLog(
                L"OnKeyDown kana-key vkey=" + std::to_wstring(key) +
                L" cmd=" + std::to_wstring(static_cast<int>(cmd)) +
                L" ctx=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(context)) +
                L" (handled-on-keydown)");
            ApplyKanaModeCommand(context, cmd, true, true);
            m_suppressPreservedKanaHandling = true;
            m_suppressedKanaVKey = key;
            if (eaten) *eaten = TRUE;
            return S_OK;
        }
        // From here, handle preedit/candidates
        const bool hasPreeditText = !PreeditText().empty();
        const bool hasUi = (m_candidateUI != nullptr);
        const bool hasActiveInput = hasPreeditText || hasUi;
        const bool ctrlPressed = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (!m_kanaMode && !hasActiveInput) return S_OK;
        auto promoteLayer1ToOrangeBeforeTyping = [&]() {
            if (!m_layer1SpaceTriggered) {
                if (m_candidateUI) {
                    FinalizeLayer2BeforeNewInput(context);
                }
                return;
            }
            m_layer1SpaceTriggered = false;

            std::wstring fallbackSelected;
            if (m_candidateUI) {
                const bool needPromote =
                    (m_activeCandidateTab == CandidateTab::Layer1 &&
                     !IsLayer1MergedFromLayer2());
                if (needPromote) {
                    fallbackSelected =
                        m_segmentMode ? ComposeSegmentReading() : m_candidateUI->SelectedString();
                    if (fallbackSelected.empty()) {
                        fallbackSelected = m_candidateReading;
                    }
                    // Match Enter-like transition when user started conversion by Space.
                    (void)SwitchToLayer2FromLayer1(false);
                }
                FinalizeLayer2BeforeNewInput(context);
            }

            if (!fallbackSelected.empty()) {
                std::wstring locked = ResolveLayer1SourceText(fallbackSelected);
                if (locked.empty()) locked = fallbackSelected;
                if (!HasLayer2LockedCarry() && !locked.empty()) {
                    DraftText() = locked;
                    m_layer2LockedPrefix = locked;
                    m_layer2MergedCarryOpen = true;
                    ClearLayer2TransitionFlags();
                    SetCandidateSourceText(locked);
                }
            } else if (!HasLayer2LockedCarry() && !DraftText().empty()) {
                // Candidate UI may already be closed by host timing; keep
                // space-trigger behavior by locking current preedit.
                m_layer2LockedPrefix = DraftText();
                m_layer2MergedCarryOpen = true;
                ClearLayer2TransitionFlags();
                SetCandidateSourceText(DraftText());
            }
        };
        const bool maybeTextKey =
            ((wParam >= '0' && wParam <= '9') ||
             (wParam >= 'A' && wParam <= 'Z') ||
             (wParam >= VK_OEM_1 && wParam <= VK_OEM_3) ||
             (wParam >= VK_OEM_4 && wParam <= VK_OEM_8) ||
             wParam == VK_OEM_102);
        if (m_layer1SpaceTriggered && hasActiveInput && !ctrlPressed && maybeTextKey) {
            promoteLayer1ToOrangeBeforeTyping();
        }
        auto appendDirectText = [&](const std::wstring& text, bool keepHalfWidthMode) {
            if (text.empty()) return false;
            ResetCompositionCaretToEnd();
            if (m_candidateUI) {
                promoteLayer1ToOrangeBeforeTyping();
                const bool preserveCarry = HasLayer2LockedCarry();
                CloseCandidateUI(preserveCarry);
            }
            std::wstring flushed = ConvertRomaji(true);
            if (!flushed.empty()) DraftText().append(flushed);
            m_romajiBuffer.clear();
            DraftText().append(text);
            if (keepHalfWidthMode) {
                m_forceHalfWidthInput = true;
                m_forceHalfWidthOutstanding += text.size();
            } else {
                ResetForceHalfWidthInput();
            }
            EnsureComposition(context);
            if (m_composition) {
                UpdateCompositionText(context, PreeditText());
                RefreshCandidateSourceFromDraft();
            } else {
                std::wstring chunk = flushed;
                chunk.append(text);
                bool inserted = false;
                if (m_ptm) {
                    ComPtr<ITfDocumentMgr> dm;
                    if (SUCCEEDED(m_ptm->GetFocus(&dm.p)) && dm.p) {
                        ComPtr<ITfContext> top;
                        if (SUCCEEDED(dm->GetTop(&top.p)) && top.p) {
                            if (InsertText(top.p, chunk)) {
                                inserted = true;
                            }
                        }
                    }
                }
                if (!inserted) {
                    InjectUnicodeKeystrokes(chunk);
                }
                DraftText().clear();
                m_romajiBuffer.clear();
                ResetForceHalfWidthInput();
            }
            UpdateOverlayFromState();
            if (eaten) *eaten = TRUE;
            return true;
        };
        if (m_forceHalfWidthInput && hasActiveInput && !ctrlPressed) {
            std::wstring direct;
            if (wParam == VK_SPACE) {
                direct.assign(1, L' ');
            } else if (wParam >= 'A' && wParam <= 'Z') {
                const bool capsOn = (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                const bool upper = (shiftPressed != capsOn);
                const wchar_t base = static_cast<wchar_t>(wParam);
                direct.assign(1, upper ? base : static_cast<wchar_t>(towlower(base)));
            } else if (const wchar_t* mapped = MapHalfWidthSymbol(static_cast<UINT>(wParam), shiftPressed)) {
                direct.assign(mapped);
            }
            if (!direct.empty() && appendDirectText(direct, true)) {
                return S_OK;
            }
        }
        if (wParam == VK_ESCAPE) {
            if (m_candidateUI) {
                if (m_activeCandidateTab == CandidateTab::Translation) {
                    m_activeCandidateTab = CandidateTab::Layer1;
                    ShowLayer1Candidates(false);
                    if (eaten) *eaten = TRUE;
                    return S_OK;
                }
                if (m_activeCandidateTab == CandidateTab::Layer2) {
                    CancelActiveLayer2Request();
                    m_activeCandidateTab = CandidateTab::Layer1;
                    SetLayer1Merged(false);
                    m_layer2SourceText.clear();
                    m_layer2SourceKey.clear();
                    SetPendingLayer2AutoCommit(false);
                    ClearLayer2LockedCarry();
                    ShowLayer1Candidates(true);
                    if (eaten) *eaten = TRUE;
                    return S_OK;
                }
            }
            if (hasActiveInput) {
                CancelComposition(context);
                if (eaten) *eaten = TRUE;
            }
            return S_OK;
        }
        if (wParam == VK_BACK) {
            if (hasPreeditText || hasUi) {
                ResetCompositionCaretToEnd();
                if (m_candidateUI) {
                    const bool inOrangeLayer = IsOrangeEditingState();
                    if (inOrangeLayer) {
                        // Backspace in orange layer edits the orange text directly
                        // instead of cancelling back to blue/kana preview.
                        FinalizeLayer2BeforeNewInput(context);
                        if (m_candidateUI && m_activeCandidateTab == CandidateTab::Layer2) {
                            MergeLayer2Selection(context);
                        }
                        if (IsLayer1MergedFromLayer2() && !m_layer2LockedPrefix.empty()) {
                            m_layer2MergedCarryOpen = true;
                            SetLayer1Merged(false);
                        }
                        const bool preserveCarry = HasLayer2LockedCarry();
                        CloseCandidateUI(preserveCarry);
                        if (!m_romajiBuffer.empty()) {
                            m_romajiBuffer.pop_back();
                        } else if (!DraftText().empty()) {
                            const wchar_t deleted = DraftText().back();
                            DraftText().pop_back();
                            OnReadingCharDeleted(deleted);
                            if (HasLayer2LockedCarry()) {
                                if (m_layer2LockedPrefix.size() > DraftText().size()) {
                                    m_layer2LockedPrefix.resize(DraftText().size());
                                }
                                if (m_layer2LockedPrefix.empty() ||
                                    DraftText().size() < m_layer2LockedPrefix.size() ||
                                    DraftText().compare(0, m_layer2LockedPrefix.size(), m_layer2LockedPrefix) != 0) {
                                    ClearLayer2LockedCarry();
                                }
                            }
                        }
                        const std::wstring preedit = PreeditText();
                        if (preedit.empty()) {
                            CancelComposition(context);
                        } else {
                            EnsureComposition(context);
                            if (m_composition) UpdateCompositionText(context, preedit);
                        }
                        UpdateOverlayFromState();
                        if (eaten) *eaten = TRUE;
                        return S_OK;
                    }
                    const bool preserveCarry = HasLayer2LockedCarry();
                    CloseCandidateUI(preserveCarry);
                    const std::wstring preedit = PreeditText();
                    if (preedit.empty()) {
                        CancelComposition(context);
                    } else {
                        EnsureComposition(context);
                        if (m_composition) UpdateCompositionText(context, preedit);
                    }
                    UpdateOverlayFromState();
                    if (eaten) *eaten = TRUE;
                    return S_OK;
                }
                if (!m_romajiBuffer.empty()) {
                    m_romajiBuffer.pop_back();
                } else if (StepBackLockedPrefixBoundary()) {
                    // Deleted one char from locked Layer2 prefix.
                } else if (!DraftText().empty()) {
                    const wchar_t deleted = DraftText().back();
                    DraftText().pop_back();
                    OnReadingCharDeleted(deleted);
                }
                const std::wstring preedit = PreeditText();
                if (preedit.empty()) {
                    CancelComposition(context);
                } else {
                    EnsureComposition(context);
                    if (m_composition) UpdateCompositionText(context, preedit);
                }
                UpdateOverlayFromState();
                if (eaten) *eaten = TRUE;
            }
            return S_OK;
        }
        if (wParam == VK_TAB && ctrlPressed) {
            if (m_candidateUI) {
                CycleCandidateTab(shiftPressed);
                if (eaten) *eaten = TRUE;
            }
            return S_OK;
        }
        if ((wParam == VK_LEFT || wParam == VK_RIGHT) &&
            m_candidateUI && m_activeCandidateTab == CandidateTab::Layer1 && m_segmentMode) {
            if (SetActiveSegment(wParam == VK_RIGHT ? +1 : -1)) {
                if (eaten) *eaten = TRUE;
                return S_OK;
            }
        }
        if ((wParam == VK_LEFT || wParam == VK_RIGHT) && hasActiveInput && m_composition) {
            if (MoveCompositionCaret(context, wParam == VK_RIGHT ? +1 : -1)) {
                if (eaten) *eaten = TRUE;
                return S_OK;
            }
        }
        if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_TAB) {
            if (m_candidateUI) {
                if (wParam == VK_UP) m_candidateUI->MoveSelection(-1);
                else m_candidateUI->MoveSelection(+1);
                if (eaten) *eaten = TRUE;
                return S_OK;
            }
            // No candidate UI yet: only act if we have something in-flight
            if (!hasActiveInput) return S_OK;
        }
        if (wParam == VK_SPACE) {
            if (hasActiveInput) {
                if (!m_candidateUI) {
                    m_layer1SpaceTriggered = true;
                    DraftText().append(ConvertRomaji(true));
                    m_romajiBuffer.clear();
                    EnsureComposition(context);
                    if (m_composition) UpdateCompositionText(context, DraftText());
                    OpenCandidateUI(context, DraftText());
                    // OpenCandidateUI internally calls CloseCandidateUI(), which
                    // clears state flags. Re-arm the space-trigger for this
                    // preedit so next typing can promote to orange.
                    if (m_candidateUI && m_activeCandidateTab == CandidateTab::Layer1) {
                        m_layer1SpaceTriggered = true;
                    }
                    HideOverlay();
                    if (m_candidateUI) {
                        // For Space on mixed text, prefer raw reading first to
                        // avoid aggressive auto-conversion (e.g. "...なん" -> "...何なん").
                        const bool hasLockedCarry = HasLayer2LockedCarry();
                        const std::wstring rawReading = TrimWhitespace(DraftText());
                        if (!rawReading.empty() && !m_layer1DisplayCache.empty() &&
                            ContainsKanjiChar(rawReading) && !hasLockedCarry) {
                            auto it = std::find(
                                m_layer1DisplayCache.begin(),
                                m_layer1DisplayCache.end(),
                                rawReading);
                            if (it == m_layer1DisplayCache.end() && m_segmentMode) {
                                m_segmentMode = false;
                                m_layer1SegmentRuntime.clear();
                                m_segmentDisplayRanges.clear();
                                m_segmentBaseReading.clear();
                                auto full = CollectLayer1BaseCandidates(rawReading, false, false);
                                if (!full.empty()) {
                                    m_layer1DisplayCache = std::move(full);
                                    m_layer1Selection = 0;
                                    m_candidateUI->SetCandidates(m_layer1DisplayCache, 0, false);
                                    it = std::find(
                                        m_layer1DisplayCache.begin(),
                                        m_layer1DisplayCache.end(),
                                        rawReading);
                                }
                            }
                            if (it != m_layer1DisplayCache.end()) {
                                const int index =
                                    static_cast<int>(it - m_layer1DisplayCache.begin());
                                m_layer1Selection = static_cast<size_t>(index);
                                m_candidateUI->SetSelection(static_cast<UINT>(index));
                            }
                        }
                    }
                    if (eaten) *eaten = TRUE;
                } else {
                    if (m_activeCandidateTab == CandidateTab::Layer1) {
                        m_layer1SpaceTriggered = true;
                    }
                    bool advanceSelection = false;
                    bool handled = false;
                    switch (m_activeCandidateTab) {
                    case CandidateTab::Layer1:
                        if (IsLayer1MergedFromLayer2()) {
                            if (SupportsTranslationFlow()) {
                                handled = SwitchToTranslationTab(shiftPressed);
                            }
                            if (!handled) advanceSelection = true;
                        } else {
                            advanceSelection = true;
                        }
                        break;
                    case CandidateTab::Layer2:
                        if (shiftPressed) {
                            handled = HandleLayer2Space(true);
                        } else {
                            if (SupportsTranslationFlow()) {
                                // Start translation directly from Layer2 selection without Enter.
                                handled = SwitchToTranslationTab(false);
                            } else {
                                handled = HandleLayer2Space(false);
                            }
                        }
                        if (!handled) advanceSelection = true;
                        break;
                    case CandidateTab::Translation:
                        handled = HandleTranslationSpace(shiftPressed);
                        if (!handled) advanceSelection = true;
                        break;
                    }
                    if (advanceSelection) {
                        m_candidateUI->MoveSelection(+1);
                        handled = true;
                    }
                    if (handled && eaten) *eaten = TRUE;
                }
            }
            return S_OK;
        }
        if (wParam == VK_RETURN) {
            if (hasActiveInput) {
                if (m_candidateUI) {
                    HandleTabEnter(context, shiftPressed);
                } else {
                    DraftText().append(ConvertRomaji(true));
                    m_romajiBuffer.clear();
                    EnsureComposition(context);
                    if (m_composition) UpdateCompositionText(context, DraftText());
                    const std::wstring enterSource = TrimWhitespace(DraftText());
                    OpenCandidateUI(context, DraftText());
                    HideOverlay();
                    if (m_candidateUI) {
                        if (SupportsLayer2Flow()) {
                            // Root rule: transition text for Enter is the actual
                            // preedit text at Enter time, not a re-ranked candidate.
                            if (!SwitchToLayer2FromLayer1(false, &enterSource)) {
                                CommitComposition(context, DraftText());
                            }
                        } else {
                            CommitCandidateSelection(context);
                        }
                    } else {
                        // Fallback: no candidates available, commit as-is.
                        CommitComposition(context, DraftText());
                    }
                }
                if (eaten) *eaten = TRUE;
            }
            return S_OK;
        }
        if (const wchar_t* mapped = MapFullWidthSymbol(static_cast<UINT>(wParam), shiftPressed)) {
            appendDirectText(std::wstring(mapped), false);
            return S_OK;
        }
        if (wParam >= 'A' && wParam <= 'Z') {
            if (shiftPressed) {
                const wchar_t alpha = static_cast<wchar_t>(wParam); // half-width uppercase
                appendDirectText(std::wstring(1, alpha), true);
                return S_OK;
            }
            ResetCompositionCaretToEnd();
            if (m_candidateUI) {
                promoteLayer1ToOrangeBeforeTyping();
                const bool preserveCarry = HasLayer2LockedCarry();
                CloseCandidateUI(preserveCarry);
            }
            wchar_t ch = (wchar_t)wParam;
            if (iswalpha(ch)) ch = (wchar_t)towlower(ch);
            if (m_romajiBuffer.size() >= m_maxRomajiBuffer) {
                std::wstring flushed = ConvertRomaji(true);
                if (!flushed.empty()) {
                    DraftText().append(flushed);
                }
                m_romajiBuffer.clear();
            }
            m_romajiBuffer.push_back(ch);
            std::wstring newly = ConvertRomaji(false);
            if (!newly.empty()) DraftText().append(newly);
            DebugLog(L"Key A-Z: updating composition");
            EnsureComposition(context);
            if (m_composition) {
                UpdateCompositionText(context, PreeditText());
                RefreshCandidateSourceFromDraft();
            } else if (!newly.empty()) {
                // As last resort, directly insert the converted chunk into focused context.
                ComPtr<ITfDocumentMgr> dm; ComPtr<ITfContext> top;
                if (m_ptm && SUCCEEDED(m_ptm->GetFocus(&dm.p)) && dm.p && SUCCEEDED(dm->GetTop(&top.p)) && top.p) {
                    DebugLog(L"Fallback: direct insert via InsertText");
                    if (!InsertText(top.p, newly)) {
                        // If even edit session scheduling fails, inject Unicode directly
                        for (wchar_t wc : newly) {
                            INPUT in[2]{};
                            in[0].type = INPUT_KEYBOARD; in[0].ki.wScan = wc; in[0].ki.dwFlags = KEYEVENTF_UNICODE;
                            in[1].type = INPUT_KEYBOARD; in[1].ki.wScan = wc; in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                            SendInput(2, in, sizeof(INPUT));
                        }
                        DebugLog(L"Fallback: SendInput UNICODE injected");
                    }
                    // Keep reading cleared to avoid duplication when composition later succeeds
                    DraftText().clear();
                    m_romajiBuffer.clear();
                }
            }
            UpdateOverlayFromState();
            if (eaten) *eaten = TRUE;
            return S_OK;
        }
        return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* eaten) override {
        if (eaten) *eaten = FALSE;
        const UINT key = static_cast<UINT>(wParam);
        if (m_suppressPreservedKanaHandling && key == m_suppressedKanaVKey) {
            m_suppressPreservedKanaHandling = false;
            m_suppressedKanaVKey = 0;
        }
        return S_OK;
    }
    STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID rguid, BOOL* eaten) override {
        if (eaten) *eaten = FALSE;
        AppendModeToastDebugLog(
            L"OnPreservedKey guidData1=0x" + std::to_wstring(static_cast<unsigned long long>(rguid.Data1)) +
            L" ctx=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(context)));
        const bool shiftPressed = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shiftPressed &&
            (IsEqualGUID(rguid, GUID_Preserved_Toggle) ||
             IsEqualGUID(rguid, GUID_Preserved_KANA) ||
             IsEqualGUID(rguid, GUID_Preserved_KANJI))) {
            // Shift+toggle is reserved for kana-kanji-only mode in OnKeyDown.
            if (eaten) *eaten = TRUE;
            return S_OK;
        }
        if (m_suppressPreservedKanaHandling) {
            const bool stillPressed =
                (m_suppressedKanaVKey != 0) &&
                ((::GetKeyState(static_cast<int>(m_suppressedKanaVKey)) & 0x8000) != 0);
            if (!stillPressed) {
                // Key up may be missed in some hosts. Drop stale suppression.
                m_suppressPreservedKanaHandling = false;
                m_suppressedKanaVKey = 0;
            } else {
            AppendModeToastDebugLog(
                L"OnPreservedKey suppressed after keydown guidData1=0x" +
                std::to_wstring(static_cast<unsigned long long>(rguid.Data1)));
            if (eaten) *eaten = TRUE;
            return S_OK;
            }
        }
        KanaModeCommand cmd = CommandFromPreservedGuid(rguid, kToggleVk);
        if (cmd == KanaModeCommand::ForceOff && m_kanaMode == false) {
            AppendModeToastDebugLog(L"OnPreservedKey ForceOff while already OFF");
        } else if (cmd == KanaModeCommand::ForceOn && m_kanaMode == true) {
            AppendModeToastDebugLog(L"OnPreservedKey ForceOn while already ON");
        }
        if (cmd != KanaModeCommand::None) {
            // Fallback path when hosts do not send a usable OnKeyDown for preserved keys.
            ApplyKanaModeCommand(context, cmd, true, true);
            if (eaten) *eaten = TRUE;
        }
        return S_OK;
    }
    STDMETHODIMP OnChange(REFGUID rguid) override {
        if (rguid == GUID_COMPARTMENT_KEYBOARD_OPENCLOSE || rguid == GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) {
            if (!m_ignoreCompartmentNotifications) {
                SyncKanaModeFromCompartments(false);
            }
        }
        return S_OK;
    }
    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;
    // Async translation entry points (future Ollama integration)
    uint64_t SubmitTranslationTask(const std::wstring& reading, const std::wstring& context) {
        return QueueTranslationAsync(reading, context);
    }
    void CancelTranslationTask(uint64_t requestId) {
        CancelTranslationAsync(requestId);
        if (requestId == m_activeTranslationRequestId) {
            m_activeTranslationRequestId = 0;
        }
    }
    void CancelAllTranslationTasks() {
        CancelAllTranslations();
        m_candidateReading.clear();
        ClearCandidateSourceText();
    }
private:
    template<class T> struct ComPtr { T* p = nullptr; ~ComPtr() { if (p) p->Release(); } T** operator&() { return &p; } operator bool()const { return p != nullptr; } T* operator->()const { return p; } };
    TextService() : m_ref(1), m_ptm(nullptr), m_tid(TF_CLIENTID_NULL) {}
    ~TextService() = default;
    // Kana toggle state and romaji buffer
    bool m_kanaMode = false;
    size_t m_maxPreeditChars = 512;
    size_t m_maxRomajiBuffer = 64;
    bool m_forceHalfWidthInput = false;
    size_t m_forceHalfWidthOutstanding = 0;
    bool m_kanaKanjiOnlyMode = false;
    bool m_suppressPreservedKanaHandling = false;
    UINT m_suppressedKanaVKey = 0;
    std::wstring m_romajiBuffer;
    std::wstring m_reading; // current preedit reading (hiragana)
    ITfComposition* m_composition = nullptr; // active composition or null
    TfGuidAtom m_gaDisplayAttrPreedit = 0;    // atom id for layer1 preedit display attribute
    TfGuidAtom m_gaDisplayAttrLayer2 = 0;     // atom id for layer2 underline attribute
    TfGuidAtom m_gaDisplayAttrTranslation = 0; // atom id for translation underline attribute
    TfGuidAtom m_gaDisplayAttrSegment = 0;    // atom id for segment boundary attribute
    TfGuidAtom m_gaDisplayAttrSegmentActive = 0; // atom id for active segment highlight
    std::unique_ptr<OverlayController> m_overlay;
    ComPtr<ITfLangBarItemMgr> m_langBarMgr;
    LangBarButton* m_langBarButton = nullptr;
    // Provider backend state
    std::filesystem::path m_installRoot;
    ime::config::ProviderSettings m_providerSettings;
    std::wstring m_providerLoadError;
    std::unique_ptr<ime::conversion::IConversionProvider> m_conversionProvider;
    ime::conversion::ProviderCapabilities m_providerCapabilities;
    // Async background work (Ollama requests etc.)
    AsyncWorkQueue m_asyncQueue;
    bool m_ollamaResidencyActive = false;
    uint64_t m_ollamaResidencyGeneration = 0;
    HWND m_hwndAsyncDispatch = nullptr;
    std::mutex m_mainThreadMutex;
    std::queue<std::function<void()>> m_mainThreadCallbacks;
    std::mutex m_translationMutex;
    std::unordered_map<uint64_t, std::wstring> m_translationPending;
    uint64_t m_activeTranslationRequestId = 0;
    std::mutex m_layer2Mutex;
    std::unordered_map<uint64_t, std::wstring> m_layer2Pending;
    uint64_t m_activeLayer2RequestId = 0;
    enum class CandidateTab {
        Layer1,
        Layer2,
        Translation
    };
    struct TranslationCacheEntry {
        std::vector<llm::CandidateEntry> candidates;
        size_t index = 0;
        uint64_t lastAccessStamp = 0;
        std::wstring error;
    };
    std::unordered_map<std::wstring, TranslationCacheEntry> m_translationCache;
    size_t m_maxTranslationCacheEntries = 64;
    size_t m_maxTranslationCandidatesPerEntry = 8;
    uint64_t m_translationCacheStamp = 0;
    struct Layer2CacheEntry {
        std::vector<llm::CandidateEntry> candidates;
        size_t index = 0;
        uint64_t lastAccessStamp = 0;
        std::wstring error;
    };
    std::unordered_map<std::wstring, Layer2CacheEntry> m_layer2Cache;
    std::unordered_map<std::wstring, std::unordered_map<std::wstring, uint32_t>> m_candidateUsage;
    bool m_candidateUsageLoaded = false;
    bool m_candidateLearningEnabled = false;
    CandidateTab m_activeCandidateTab = CandidateTab::Layer1;
    bool m_layer1Merged = false;
    bool m_layer1SpaceTriggered = false;
    size_t m_layer1Selection = 0;
    std::vector<std::wstring> m_layer1DisplayCache;
    std::wstring m_layer2SourceText;
    std::wstring m_layer2SourceKey;
    size_t m_maxLayer2CacheEntries = 64;
    size_t m_maxLayer2CandidatesPerEntry = 8;
    uint64_t m_layer2CacheStamp = 0;
    ComPtr<ITfCompartmentMgr> m_compartmentMgr;
    ComPtr<ITfCompartment> m_compartmentOpenClose;
    ComPtr<ITfCompartment> m_compartmentConversion;
    DWORD m_compartmentSinkCookieOpenClose = TF_INVALID_COOKIE;
    DWORD m_compartmentSinkCookieConversion = TF_INVALID_COOKIE;
    bool m_ignoreCompartmentNotifications = false;
    DWORD m_lastModeToastTick = 0;
    std::wstring m_lastModeToastLabel;
    // Build current preedit: converted kana + remaining romaji buffer
    std::wstring PreeditText() const { return DraftText() + m_romajiBuffer; }
    std::wstring& DraftText() { return m_reading; }
    const std::wstring& DraftText() const { return m_reading; }
    std::wstring ActiveDraftText() const {
        return TrimWhitespace(m_candidateReading.empty() ? DraftText() : m_candidateReading);
    }
    void SetCandidateSourceText(const std::wstring& source) {
        m_candidateSourceText = TrimWhitespace(source);
        m_candidateSourceKey = BuildTranslationCacheKey(m_candidateSourceText);
    }
    void ClearCandidateSourceText() {
        SetCandidateSourceText(L"");
    }
    void SyncCandidateSourceFromActiveDraft() {
        const std::wstring draft = ActiveDraftText();
        if (draft.empty()) return;
        SetCandidateSourceText(draft);
    }
    bool HasLayer2LockedCarry() const {
        return m_layer2MergedCarryOpen && !m_layer2LockedPrefix.empty();
    }
    void ClearLayer2LockedCarry() {
        m_layer2MergedCarryOpen = false;
        m_layer2LockedPrefix.clear();
    }
    // Layer1 merged-state: Layer2 candidate was merged back and is being edited on Layer1.
    // Pending auto-commit: Layer2 tab is active and next typing should merge current Layer2 selection first.
    void SetLayer1Merged(bool merged) {
        m_layer1Merged = merged;
        if (merged) {
            m_pendingLayer2AutoCommit = false;
        }
    }
    void SetPendingLayer2AutoCommit(bool pending) {
        m_pendingLayer2AutoCommit = pending;
        if (pending) {
            m_layer1Merged = false;
        }
    }
    bool IsLayer1MergedFromLayer2() const {
        return m_layer1Merged && m_activeCandidateTab == CandidateTab::Layer1;
    }
    bool HasPendingLayer2AutoCommitTransition() const {
        return m_pendingLayer2AutoCommit && m_activeCandidateTab == CandidateTab::Layer2;
    }
    bool IsOrangeEditingState() const {
        return (m_activeCandidateTab == CandidateTab::Layer2) ||
               IsLayer1MergedFromLayer2() ||
               HasPendingLayer2AutoCommitTransition() ||
               HasLayer2LockedCarry();
    }
    void ClearLayer2TransitionFlags() {
        SetLayer1Merged(false);
        SetPendingLayer2AutoCommit(false);
    }
    bool SupportsLayer2Flow() const {
        return m_providerCapabilities.supports_layer2 && !m_kanaKanjiOnlyMode;
    }
    bool SupportsTranslationFlow() const {
        return m_providerCapabilities.supports_translation && !m_kanaKanjiOnlyMode;
    }
    void RefreshCaretAnchorFromContext(ITfContext* context = nullptr) {
        ITfContext* ctx = context;
        ComPtr<ITfContext> focused;
        if (!ctx && m_ptm) {
            ComPtr<ITfDocumentMgr> dm;
            if (SUCCEEDED(m_ptm->GetFocus(&dm.p)) && dm.p) {
                dm->GetTop(&focused.p);
                ctx = focused.p;
            }
        }
        if (!ctx) return;
        struct CaptureCaretAnchorEditSession final : ITfEditSession {
            LONG _ref{ 1 };
            ITfContext* _ctx{ nullptr };
            TextService* _owner{ nullptr };
            CaptureCaretAnchorEditSession(ITfContext* ctx, TextService* owner)
                : _ctx(ctx), _owner(owner) {
                if (_ctx) _ctx->AddRef();
            }
            ~CaptureCaretAnchorEditSession() {
                if (_ctx) _ctx->Release();
            }
            STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
                if (!ppv) return E_POINTER;
                *ppv = nullptr;
                if (riid == IID_IUnknown || riid == __uuidof(ITfEditSession)) {
                    *ppv = static_cast<ITfEditSession*>(this);
                    AddRef();
                    return S_OK;
                }
                return E_NOINTERFACE;
            }
            STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&_ref); }
            STDMETHODIMP_(ULONG) Release() override {
                ULONG r = (ULONG)InterlockedDecrement(&_ref);
                if (!r) delete this;
                return r;
            }
            STDMETHODIMP DoEditSession(TfEditCookie ec) override {
                if (!_ctx || !_owner) return S_OK;
                TF_SELECTION sel{};
                ULONG fetched = 0;
                if (SUCCEEDED(_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) &&
                    fetched == 1 && sel.range) {
                    ComPtr<ITfRange> caret;
                    if (SUCCEEDED(sel.range->Clone(&caret.p)) && caret.p) {
                        caret->Collapse(ec, TF_ANCHOR_END);
                        ComPtr<ITfContextView> view;
                        if (SUCCEEDED(_ctx->GetActiveView(&view.p)) && view.p) {
                            RECT rc{};
                            BOOL clipped = FALSE;
                            if (SUCCEEDED(view->GetTextExt(ec, caret.p, &rc, &clipped))) {
                                const bool isZeroRect =
                                    (rc.left == 0 && rc.top == 0 && rc.right == 0 && rc.bottom == 0);
                                if (!isZeroRect) {
                                    _owner->m_lastCaretScreenPoint.x = rc.left;
                                    _owner->m_lastCaretScreenPoint.y = rc.bottom + 2;
                                    _owner->m_hasLastCaretScreenPoint = true;
                                }
                            }
                        }
                    }
                    sel.range->Release();
                }
                return S_OK;
            }
        };
        auto* edit = new (std::nothrow) CaptureCaretAnchorEditSession(ctx, this);
        if (!edit) return;
        HRESULT hrSession = E_FAIL;
        HRESULT hr = ctx->RequestEditSession(m_tid, edit, TF_ES_SYNC | TF_ES_READ, &hrSession);
        if (FAILED(hr)) {
            HRESULT hr2Session = E_FAIL;
            HRESULT hr2 = ctx->RequestEditSession(m_tid, edit, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hr2Session);
            (void)hr2;
            (void)hr2Session;
        }
        edit->Release();
    }
    bool ShouldShowModeToast(ITfContext* context) const {
        if (!context) {
            AppendModeToastDebugLog(L"ShouldShowModeToast=false reason=no-context");
            return false;
        }
        if (!m_ptm) {
            AppendModeToastDebugLog(L"ShouldShowModeToast=false reason=no-ptm");
            return false;
        }
        ComPtr<ITfDocumentMgr> dm;
        if (FAILED(m_ptm->GetFocus(&dm.p)) || !dm.p) {
            AppendModeToastDebugLog(L"ShouldShowModeToast=false reason=no-focus-docmgr");
            return false;
        }
        ComPtr<ITfContext> top;
        if (FAILED(dm->GetTop(&top.p)) || !top.p) {
            AppendModeToastDebugLog(L"ShouldShowModeToast=false reason=no-top-context");
            return false;
        }
        const bool allow = (top.p == context);
        AppendModeToastDebugLog(
            L"ShouldShowModeToast=" + std::to_wstring(allow ? 1 : 0) +
            L" top=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(top.p)) +
            L" ctx=0x" + std::to_wstring(reinterpret_cast<uintptr_t>(context)));
        return allow;
    }
    void SetKanaKanjiOnlyMode(ITfContext* context, bool enable, bool showToast = true) {
        if (m_kanaKanjiOnlyMode == enable) {
            AppendModeToastDebugLog(
                L"SetKanaKanjiOnlyMode no-op enable=" + std::to_wstring(enable ? 1 : 0) +
                L" (toast suppressed)");
            return;
        }
        m_kanaKanjiOnlyMode = enable;
        PersistKanaKanjiOnlyMode();
        CancelActiveTranslationRequest();
        CancelActiveLayer2Request();
        SetPendingLayer2AutoCommit(false);
        if (m_candidateUI && m_activeCandidateTab != CandidateTab::Layer1) {
            m_activeCandidateTab = CandidateTab::Layer1;
            ShowLayer1Candidates(true);
            std::wstring preview =
                m_segmentMode ? ComposeSegmentReading() : m_candidateUI->SelectedString();
            if (!preview.empty()) {
                PreviewCandidateString(preview);
            }
        }
        if (showToast && ShouldShowModeToast(context)) {
            RefreshCaretAnchorFromContext(context);
            ShowModeToast(enable ? L"\u304B\u306A\u6F22\u5B57" : L"\u901A\u5E38");
        }
    }
    void ToggleKanaKanjiOnlyMode(ITfContext* context) {
        SetKanaKanjiOnlyMode(context, !m_kanaKanjiOnlyMode);
    }
    static bool IsHalfWidthAscii(wchar_t ch) {
        return (ch >= 0x20 && ch <= 0x7E);
    }
    void ResetForceHalfWidthInput() {
        m_forceHalfWidthInput = false;
        m_forceHalfWidthOutstanding = 0;
    }
    void OnReadingCharDeleted(wchar_t deleted) {
        if (!m_forceHalfWidthInput) return;
        if (m_forceHalfWidthOutstanding == 0) {
            m_forceHalfWidthInput = false;
            return;
        }
        if (IsHalfWidthAscii(deleted) && m_forceHalfWidthOutstanding > 0) {
            --m_forceHalfWidthOutstanding;
        }
        if (m_forceHalfWidthOutstanding == 0) {
            m_forceHalfWidthInput = false;
        }
    }
    void SetKanaMode(ITfContext* context, bool enableKana, bool showToast = true, bool updateCompartments = true) {
        if (m_kanaMode == enableKana) {
            DebugLog(enableKana ? L"Kana mode already ON" : L"Kana mode already OFF");
            if (!enableKana) {
                CloseCandidateUI();
                HideOverlay();
            } else {
                UpdateOverlayFromState();
            }
            if (updateCompartments) UpdateSystemInputModeCompartments(enableKana);
            AppendModeToastDebugLog(
                L"SetKanaMode no-op enable=" + std::to_wstring(enableKana ? 1 : 0) +
                L" (toast suppressed)");
            UpdateLangBarButtonToggle();
            return;
        }
        m_kanaMode = enableKana;
        if (!m_kanaMode) {
            ResetForceHalfWidthInput();
        }
        UpdateLangBarButtonToggle();
        DebugLog(m_kanaMode ? L"Kana mode ON" : L"Kana mode OFF");
        if (!m_kanaMode) {
            DraftText().append(ConvertRomaji(true));
            m_romajiBuffer.clear();
            if (!DraftText().empty() || m_composition) {
                CommitComposition(context, DraftText());
            } else {
                CloseCandidateUI();
                HideOverlay();
            }
        } else {
            UpdateOverlayFromState();
        }
        if (updateCompartments) UpdateSystemInputModeCompartments(m_kanaMode);
        if (showToast && ShouldShowModeToast(context)) {
            RefreshCaretAnchorFromContext(context);
            ShowModeToast(m_kanaMode ? L"\u3042" : L"A");
        }
    }
    void ApplyKanaModeCommand(ITfContext* context, KanaModeCommand cmd, bool showToast, bool updateCompartments) {
        if (cmd == KanaModeCommand::Toggle) {
            SetKanaMode(context, !m_kanaMode, showToast, updateCompartments);
        } else if (cmd == KanaModeCommand::ForceOn) {
            SetKanaMode(context, true, showToast, updateCompartments);
        } else if (cmd == KanaModeCommand::ForceOff) {
            SetKanaMode(context, false, showToast, updateCompartments);
        }
    }
    void ToggleKanaMode(ITfContext* context) {
        SetKanaMode(context, !m_kanaMode);
    }
    void RegisterLangBarButton();
    void UnregisterLangBarButton();
    void UpdateLangBarButtonToggle();
    void InitializeCompartmentMonitoring() {
        if (!EnsureCompartmentMgr()) return;
        EnsureCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, m_compartmentOpenClose, m_compartmentSinkCookieOpenClose, true);
        EnsureCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, m_compartmentConversion, m_compartmentSinkCookieConversion, true);
        SyncKanaModeFromCompartments(true);
    }
    void ShutdownCompartmentMonitoring() {
        m_ignoreCompartmentNotifications = false;
        ReleaseCompartment(m_compartmentOpenClose, m_compartmentSinkCookieOpenClose);
        ReleaseCompartment(m_compartmentConversion, m_compartmentSinkCookieConversion);
        if (m_compartmentMgr) { m_compartmentMgr.p->Release(); m_compartmentMgr.p = nullptr; }
    }
    void InitializeProviderBackend() {
        wchar_t modulePath[MAX_PATH]{};
        DWORD len = GetModuleFileNameW(g_hModule, modulePath, ARRAYSIZE(modulePath));
        if (len > 0 && len < ARRAYSIZE(modulePath)) {
            try {
                m_installRoot = std::filesystem::path(modulePath).parent_path();
            } catch (...) {
                m_installRoot.clear();
            }
        }
        CancelAllTranslations();
        m_providerLoadError.clear();
        m_conversionProvider.reset();
        m_providerCapabilities = {};
        m_kanaKanjiOnlyMode = false;
        m_translationCache.clear();
        m_layer2Cache.clear();
        {
            std::lock_guard<std::mutex> lock(m_layer2Mutex);
            m_layer2Pending.clear();
        }
        m_activeLayer2RequestId = 0;
        m_layer2PendingActive = false;
        const std::filesystem::path configPath = ResolveInstallPath(L"config/ime_settings.json");
        m_providerSettings = ime::config::LoadProviderSettings(configPath, &m_providerLoadError);
        ConfigureDebugLoggingFromConfig();
        if (!m_providerLoadError.empty()) {
            DebugLog(m_providerLoadError.c_str());
        }
        if (m_providerSettings.kana.mozc) {
            m_kanaKanjiOnlyMode = m_providerSettings.kana.mozc->kana_kanji_only_mode;
        }
        RestoreKanaKanjiOnlyModeFromPersistentState();
        ApplyRuntimeLimitsFromConfig();
        std::wstring providerError;
        auto provider = ime::conversion::CreateConversionProvider(m_providerSettings, m_installRoot, &providerError);
        if (!providerError.empty()) {
            if (!m_providerLoadError.empty()) {
                m_providerLoadError.append(L"; ");
            }
            m_providerLoadError.append(providerError);
        }
        m_conversionProvider = std::move(provider);
        if (m_conversionProvider) {
            m_providerCapabilities = m_conversionProvider->GetCapabilities();
        }
        if (m_conversionProvider) {
            m_conversionProvider->SetResultCallback(
                [this](uint64_t requestId, ime::conversion::CandidateList result) {
                    PostMainThreadCallback([this, requestId, result = std::move(result)]() mutable {
                        OnProviderResult(requestId, std::move(result));
                    });
                });
        }
        LoadCandidateUsageTable();
    }
    void ConfigureDebugLoggingFromConfig() const {
        const auto& debugFile = m_providerSettings.logging.debug_file;
        if (!debugFile.enabled) {
            ConfigureDebugLogFile({}, false, 0);
            return;
        }
        const std::filesystem::path logPath = ResolveInstallPath(debugFile.path);
        ConfigureDebugLogFile(logPath, true, debugFile.max_bytes);
        std::wstring message = L"Debug file logging enabled: ";
        message += logPath.wstring();
        DebugLog(message.c_str());
    }
    static std::wstring ReadEnvVar(const wchar_t* name) {
        if (!name || !*name) return L"";
        wchar_t* value = nullptr;
        size_t len = 0;
        if (_wdupenv_s(&value, &len, name) != 0 || !value) {
            return L"";
        }
        std::wstring out(value);
        free(value);
        return out;
    }
    std::filesystem::path CandidateUsagePath() const {
        const std::wstring roamingAppData = ReadEnvVar(L"APPDATA");
        if (!roamingAppData.empty()) {
            return std::filesystem::path(roamingAppData) / L"RTAS" / L"user_data" / L"candidate_usage.tsv";
        }
        const std::wstring localAppData = ReadEnvVar(L"LOCALAPPDATA");
        if (!localAppData.empty()) {
            return std::filesystem::path(localAppData) / L"RTAS" / L"user_data" / L"candidate_usage.tsv";
        }
        return ResolveInstallPath(L"runtime/user_data/candidate_usage.tsv");
    }
    std::filesystem::path LegacyCandidateUsagePath() const {
        const std::wstring localAppData = ReadEnvVar(L"LOCALAPPDATA");
        if (!localAppData.empty()) {
            return std::filesystem::path(localAppData) / L"RTAS" / L"candidate_usage.tsv";
        }
        return ResolveInstallPath(L"runtime/candidate_usage.tsv");
    }
    std::filesystem::path KanaKanjiModeStatePath() const {
        const std::wstring roamingAppData = ReadEnvVar(L"APPDATA");
        if (!roamingAppData.empty()) {
            return std::filesystem::path(roamingAppData) / L"RTAS" / L"user_data" / L"kana_kanji_mode_state.txt";
        }
        const std::wstring localAppData = ReadEnvVar(L"LOCALAPPDATA");
        if (!localAppData.empty()) {
            return std::filesystem::path(localAppData) / L"RTAS" / L"user_data" / L"kana_kanji_mode_state.txt";
        }
        return ResolveInstallPath(L"runtime/user_data/kana_kanji_mode_state.txt");
    }
    std::filesystem::path ModeToastDebugLogPath() const {
        const std::wstring roamingAppData = ReadEnvVar(L"APPDATA");
        if (!roamingAppData.empty()) {
            return std::filesystem::path(roamingAppData) / L"RTAS" / L"user_data" / L"mode_toast_debug.log";
        }
        const std::wstring localAppData = ReadEnvVar(L"LOCALAPPDATA");
        if (!localAppData.empty()) {
            return std::filesystem::path(localAppData) / L"RTAS" / L"user_data" / L"mode_toast_debug.log";
        }
        return ResolveInstallPath(L"runtime/user_data/mode_toast_debug.log");
    }
    void AppendModeToastDebugLog(const std::wstring& line) const {
        if (line.empty()) return;
        try {
            const auto path = ModeToastDebugLogPath();
            if (path.empty()) return;
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::wofstream out(path, std::ios::binary | std::ios::app);
            if (!out) return;
            SYSTEMTIME st{};
            GetLocalTime(&st);
            out << st.wYear << L"-";
            if (st.wMonth < 10) out << L"0";
            out << st.wMonth << L"-";
            if (st.wDay < 10) out << L"0";
            out << st.wDay << L" ";
            if (st.wHour < 10) out << L"0";
            out << st.wHour << L":";
            if (st.wMinute < 10) out << L"0";
            out << st.wMinute << L":";
            if (st.wSecond < 10) out << L"0";
            out << st.wSecond << L".";
            if (st.wMilliseconds < 100) out << L"0";
            if (st.wMilliseconds < 10) out << L"0";
            out << st.wMilliseconds
                << L" tid=" << GetCurrentThreadId()
                << L" this=0x" << reinterpret_cast<uintptr_t>(this)
                << L" " << line << L"\n";
        } catch (...) {
        }
    }
    void PersistKanaKanjiOnlyMode() const {
        try {
            const auto path = KanaKanjiModeStatePath();
            if (path.empty()) return;
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::wofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out << (m_kanaKanjiOnlyMode ? L"1" : L"0") << L"\n";
        } catch (...) {
        }
    }
    void RestoreKanaKanjiOnlyModeFromPersistentState() {
        try {
            const auto path = KanaKanjiModeStatePath();
            std::wifstream in(path, std::ios::binary);
            if (!in) return;
            std::wstring token;
            in >> token;
            if (token == L"1" || token == L"true" || token == L"on") {
                m_kanaKanjiOnlyMode = true;
            } else if (token == L"0" || token == L"false" || token == L"off") {
                m_kanaKanjiOnlyMode = false;
            }
        } catch (...) {
        }
    }
    static std::wstring SanitizeTsvField(const std::wstring& value) {
        std::wstring out = value;
        std::replace(out.begin(), out.end(), L'\t', L' ');
        std::replace(out.begin(), out.end(), L'\r', L' ');
        std::replace(out.begin(), out.end(), L'\n', L' ');
        return out;
    }
    void LoadCandidateUsageTable() {
        if (!m_candidateLearningEnabled) {
            m_candidateUsageLoaded = true;
            m_candidateUsage.clear();
            return;
        }
        if (m_candidateUsageLoaded) return;
        m_candidateUsageLoaded = true;
        m_candidateUsage.clear();
        std::filesystem::path path = CandidateUsagePath();
        if (!std::filesystem::exists(path)) {
            const std::filesystem::path legacy = LegacyCandidateUsagePath();
            if (std::filesystem::exists(legacy)) {
                path = legacy;
            } else {
                return;
            }
        }
        std::wifstream in(path, std::ios::in);
        if (!in.is_open()) return;
        try {
            in.imbue(std::locale(""));
        } catch (...) {
        }
        std::wstring line;
        while (std::getline(in, line)) {
            const size_t p1 = line.find(L'\t');
            if (p1 == std::wstring::npos) continue;
            const size_t p2 = line.find(L'\t', p1 + 1);
            if (p2 == std::wstring::npos) continue;
            const std::wstring reading = TrimWhitespace(line.substr(0, p1));
            const std::wstring candidate = TrimWhitespace(line.substr(p1 + 1, p2 - (p1 + 1)));
            if (reading.empty() || candidate.empty()) continue;
            unsigned long count = 0;
            try {
                count = std::stoul(line.substr(p2 + 1));
            } catch (...) {
                continue;
            }
            if (count == 0) continue;
            m_candidateUsage[reading][candidate] = static_cast<uint32_t>(count);
        }
        if (path != CandidateUsagePath()) {
            SaveCandidateUsageTable();
        }
    }
    void SaveCandidateUsageTable() const {
        const std::filesystem::path path = CandidateUsagePath();
        try {
            std::filesystem::create_directories(path.parent_path());
        } catch (...) {
            return;
        }
        std::wofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return;
        try {
            out.imbue(std::locale(""));
        } catch (...) {
        }
        for (const auto& readingPair : m_candidateUsage) {
            const std::wstring reading = SanitizeTsvField(readingPair.first);
            if (reading.empty()) continue;
            for (const auto& candPair : readingPair.second) {
                const uint32_t count = candPair.second;
                if (count == 0) continue;
                const std::wstring candidate = SanitizeTsvField(candPair.first);
                if (candidate.empty()) continue;
                out << reading << L'\t' << candidate << L'\t' << count << L'\n';
            }
        }
    }
    void LearnCandidateSelection(const std::wstring& reading, const std::wstring& candidate) {
        if (!m_candidateLearningEnabled) return;
        const std::wstring key = TrimWhitespace(reading);
        const std::wstring value = TrimWhitespace(candidate);
        if (key.empty() || value.empty()) return;
        LoadCandidateUsageTable();
        auto& bucket = m_candidateUsage[key];
        uint32_t& count = bucket[value];
        if (count < 1000000000u) {
            ++count;
        }
        SaveCandidateUsageTable();
    }
    void ApplyLearnedCandidateOrdering(const std::wstring& reading, std::vector<std::wstring>* cands) {
        if (!m_candidateLearningEnabled) return;
        if (!cands || cands->size() < 2) return;
        LoadCandidateUsageTable();
        const std::wstring key = TrimWhitespace(reading);
        if (key.empty()) return;
        auto itUsage = m_candidateUsage.find(key);
        if (itUsage == m_candidateUsage.end() || itUsage->second.empty()) return;
        bool hasAnyScore = false;
        struct Row {
            size_t index = 0;
            uint32_t score = 0;
            std::wstring text;
        };
        std::vector<Row> rows;
        rows.reserve(cands->size());
        for (size_t i = 0; i < cands->size(); ++i) {
            Row row;
            row.index = i;
            row.text = std::move((*cands)[i]);
            auto it = itUsage->second.find(row.text);
            row.score = (it == itUsage->second.end()) ? 0u : it->second;
            if (row.score > 0u) hasAnyScore = true;
            rows.emplace_back(std::move(row));
        }
        cands->clear();
        if (!hasAnyScore) {
            cands->reserve(rows.size());
            for (auto& row : rows) {
                cands->push_back(std::move(row.text));
            }
            return;
        }
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.index < b.index;
        });
        cands->reserve(rows.size());
        for (auto& row : rows) {
            cands->push_back(std::move(row.text));
        }
    }
    std::filesystem::path ResolveInstallPath(const std::filesystem::path& relative) const {
        if (relative.is_absolute()) {
            return relative;
        }
        std::filesystem::path root = m_installRoot;
        if (root.empty()) {
            wchar_t modulePath[MAX_PATH]{};
            DWORD len = GetModuleFileNameW(g_hModule, modulePath, ARRAYSIZE(modulePath));
            if (len > 0 && len < ARRAYSIZE(modulePath)) {
                try {
                    root = std::filesystem::path(modulePath).parent_path();
                } catch (...) {
                    root.clear();
                }
            }
        }
        if (root.empty()) {
            return relative;
        }
        if (relative.empty()) {
            return root;
        }
        return root / relative;
    }
    void ApplyRuntimeLimitsFromConfig() {
        const auto& limits = m_providerSettings.runtime_limits;
        m_maxPreeditChars = std::max<std::size_t>(1, limits.max_preedit_chars);
        m_maxRomajiBuffer = std::max<std::size_t>(1, limits.max_romaji_buffer);
        m_maxTranslationCacheEntries = std::max<std::size_t>(1, limits.translation_cache.max_entries);
        m_maxTranslationCandidatesPerEntry = std::max<std::size_t>(1, limits.translation_cache.max_candidates_per_entry);
        m_maxLayer2CacheEntries = m_maxTranslationCacheEntries;
        m_maxLayer2CandidatesPerEntry = m_maxTranslationCandidatesPerEntry;
        m_candidateLearningEnabled = limits.candidate_learning_enabled;
        if (!m_candidateLearningEnabled) {
            m_candidateUsage.clear();
            m_candidateUsageLoaded = true;
        } else if (m_candidateUsageLoaded && m_candidateUsage.empty()) {
            m_candidateUsageLoaded = false;
        }
    }
    bool EnsureCompartmentMgr() {
        if (m_compartmentMgr) return true;
        if (!m_ptm) return false;
        ITfCompartmentMgr* mgr = nullptr;
        if (FAILED(m_ptm->GetGlobalCompartment(&mgr)) || !mgr) return false;
        m_compartmentMgr.p = mgr;
        return true;
    }
    ITfCompartment* EnsureCompartment(REFGUID guid, ComPtr<ITfCompartment>& storage, DWORD& cookie, bool adviseSink) {
        if (!EnsureCompartmentMgr()) return nullptr;
        if (!storage) {
            ITfCompartment* comp = nullptr;
            if (FAILED(m_compartmentMgr.p->GetCompartment(guid, &comp)) || !comp) return nullptr;
            storage.p = comp;
            if (adviseSink) {
                ComPtr<ITfSource> source;
                if (SUCCEEDED(storage.p->QueryInterface(IID_PPV_ARGS(&source.p))) && source.p) {
                    DWORD tempCookie = TF_INVALID_COOKIE;
                    if (SUCCEEDED(source.p->AdviseSink(__uuidof(ITfCompartmentEventSink), static_cast<ITfCompartmentEventSink*>(this), &tempCookie))) {
                        cookie = tempCookie;
                    }
                }
            }
        }
        return storage.p;
    }
    void ReleaseCompartment(ComPtr<ITfCompartment>& storage, DWORD& cookie) {
        if (storage) {
            if (cookie != TF_INVALID_COOKIE) {
                ComPtr<ITfSource> source;
                if (SUCCEEDED(storage.p->QueryInterface(IID_PPV_ARGS(&source.p))) && source.p) {
                    source.p->UnadviseSink(cookie);
                }
                cookie = TF_INVALID_COOKIE;
            }
            storage.p->Release();
            storage.p = nullptr;
        } else {
            cookie = TF_INVALID_COOKIE;
        }
    }
    LONG ReadCompartmentLong(ITfCompartment* compartment, LONG fallback) const {
        if (!compartment) return fallback;
        VARIANT var; VariantInit(&var);
        LONG value = fallback;
        if (SUCCEEDED(compartment->GetValue(&var))) {
            if (var.vt == VT_I4) value = var.lVal;
        }
        VariantClear(&var);
        return value;
    }
    void UpdateSystemInputModeCompartments(bool enableKana) {
        if (m_tid == TF_CLIENTID_NULL || !EnsureCompartmentMgr()) return;
        ITfCompartment* openComp = EnsureCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, m_compartmentOpenClose, m_compartmentSinkCookieOpenClose, true);
        ITfCompartment* convComp = EnsureCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, m_compartmentConversion, m_compartmentSinkCookieConversion, true);
        VARIANT var; VariantInit(&var); var.vt = VT_I4;
        if (!openComp && !convComp) {
            VariantClear(&var);
            return;
        }
        m_ignoreCompartmentNotifications = true;
        if (openComp) {
            var.lVal = enableKana ? 1 : 0;
            openComp->SetValue(m_tid, &var);
        }
        if (convComp) {
            if (enableKana) {
                var.lVal = TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE | TF_CONVERSIONMODE_ROMAN;
            } else {
                var.lVal = TF_CONVERSIONMODE_ALPHANUMERIC | TF_CONVERSIONMODE_ROMAN;
            }
            convComp->SetValue(m_tid, &var);
        }
        ITfCompartment* sentenceComp = nullptr;
        if (SUCCEEDED(m_compartmentMgr.p->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, &sentenceComp)) && sentenceComp) {
            var.lVal = TF_SENTENCEMODE_NONE;
            sentenceComp->SetValue(m_tid, &var);
            sentenceComp->Release();
        }
        VariantClear(&var);
        m_ignoreCompartmentNotifications = false;
    }
    void SyncKanaModeFromCompartments(bool initialSync) {
        if (!EnsureCompartmentMgr()) return;
        ITfCompartment* openComp = EnsureCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, m_compartmentOpenClose, m_compartmentSinkCookieOpenClose, true);
        ITfCompartment* convComp = EnsureCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, m_compartmentConversion, m_compartmentSinkCookieConversion, true);
        if (!openComp && !convComp) return;
        const LONG openValue = ReadCompartmentLong(openComp, -1);
        const LONG convValue = ReadCompartmentLong(convComp, -1);
        bool shouldEnable = m_kanaMode;
        if (openValue != -1) {
            shouldEnable = (openValue != 0);
            if (shouldEnable && convValue != -1) {
                shouldEnable = (convValue & TF_CONVERSIONMODE_NATIVE) != 0;
            }
        }
        if (shouldEnable != m_kanaMode) {
            // Compartment-driven sync can be observed by multiple TIP instances
            // across processes. Suppress toast here to avoid duplicate toasts.
            SetKanaMode(nullptr, shouldEnable, false, false);
        } else if (initialSync) {
            UpdateOverlayFromState();
        }
    }
        // --- Simple overlay preedit for non-TSF hosts ---------------------------
    OverlayController* EnsureOverlayController() {
        if (!m_overlay) {
            m_overlay = std::make_unique<OverlayController>(this);
        }
        return m_overlay.get();
    }
    void DestroyOverlay() {
        if (m_overlay) {
            m_overlay->Destroy();
            m_overlay.reset();
        }
    }
    void HideOverlay() {
        if (m_overlay) {
            m_overlay->Hide();
        }
    }
    void UpdateOverlayFromState() {
        if (HasActiveComposition()) {
            HideOverlay();
            return;
        }
        if (!m_overlay && GetPreeditText().empty()) {
            return;
        }
        if (auto* overlay = EnsureOverlayController()) {
            overlay->UpdateFromOwnerState();
        }
    }
    void ShowModeToast(const std::wstring& label, UINT durationMs = 800) {
        const DWORD now = GetTickCount();
        if (!m_lastModeToastLabel.empty() &&
            m_lastModeToastLabel == label &&
            m_lastModeToastTick != 0 &&
            (now - m_lastModeToastTick) <= 250) {
            AppendModeToastDebugLog(
                L"ShowModeToast skipped(debounce) label=" + label +
                L" deltaMs=" + std::to_wstring(now - m_lastModeToastTick));
            return;
        }
        m_lastModeToastLabel = label;
        m_lastModeToastTick = now;
        AppendModeToastDebugLog(
            L"ShowModeToast shown label=" + label +
            L" durationMs=" + std::to_wstring(durationMs));
        if (auto* overlay = EnsureOverlayController()) {
            overlay->ShowModeToast(label, durationMs);
        }
    }
// --- Async dispatch window -----------------------------------------------
    static ATOM s_asyncDispatchClass;
    static constexpr UINT WM_ASYNC_DISPATCH = WM_APP + 0x200;
    static LRESULT CALLBACK AsyncDispatchWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        TextService* self = reinterpret_cast<TextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
        if (msg == WM_NCDESTROY) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        if (msg == WM_ASYNC_DISPATCH) {
            if (self) self->DrainMainThreadCallbacks();
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    static void RegisterAsyncDispatchClass() {
        if (s_asyncDispatchClass) return;
        WNDCLASSW wc{};
        wc.lpfnWndProc = AsyncDispatchWndProc;
        wc.hInstance = g_hModule;
        wc.lpszClassName = L"TSF_AsyncDispatch";
        s_asyncDispatchClass = RegisterClassW(&wc);
    }
    bool EnsureAsyncDispatchWindow() {
        if (m_hwndAsyncDispatch) return true;
        RegisterAsyncDispatchClass();
        if (!s_asyncDispatchClass) return false;
        m_hwndAsyncDispatch = CreateWindowExW(
            0, L"TSF_AsyncDispatch", L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, g_hModule, this);
        return m_hwndAsyncDispatch != nullptr;
    }
    void DestroyAsyncDispatchWindow() {
        if (m_hwndAsyncDispatch) {
            DestroyWindow(m_hwndAsyncDispatch);
            m_hwndAsyncDispatch = nullptr;
        }
        std::lock_guard<std::mutex> lock(m_mainThreadMutex);
        std::queue<std::function<void()>> empty;
        std::swap(m_mainThreadCallbacks, empty);
    }
    void PostMainThreadCallback(std::function<void()> fn) {
        if (!fn) return;
        {
            std::lock_guard<std::mutex> lock(m_mainThreadMutex);
            m_mainThreadCallbacks.push(std::move(fn));
        }
        if (m_hwndAsyncDispatch) {
            PostMessageW(m_hwndAsyncDispatch, WM_ASYNC_DISPATCH, 0, 0);
        }
    }
    void DrainMainThreadCallbacks() {
        std::queue<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(m_mainThreadMutex);
            std::swap(local, m_mainThreadCallbacks);
        }
        while (!local.empty()) {
            auto fn = std::move(local.front());
            local.pop();
            if (fn) fn();
        }
    }
    // --- Translation job plumbing --------------------------------------------
    static constexpr const wchar_t* kTranslationPendingPlaceholder = L"\u231B [\u51E6\u7406\u4E2D\u2026]";
    static constexpr const wchar_t* kLayer2ErrorPlaceholder = L"! \u8A00\u3044\u63DB\u3048\u5019\u88DC\u306A\u3057 (Shift+Enter\u3067Layer1\u3078\u623B\u308B)";
    static constexpr const wchar_t* kTranslationErrorPlaceholder = L"! \u7FFB\u8A33\u306B\u5931\u6557\u3057\u307E\u3057\u305F (Shift+Space\u3067\u518D\u8A66\u884C)";
    struct OllamaResidencyState {
        int activeCount = 0;
        uint64_t generation = 0;
    };
    struct OllamaResidencySharedState {
        LONG activeCount = 0;
        uint64_t generation = 0;
    };
    class OllamaResidencySharedLock {
    public:
        OllamaResidencySharedLock() {
            mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\RTAS_OllamaResidency_v1_mutex");
            if (mutex_ && WaitForSingleObject(mutex_, 5000) == WAIT_OBJECT_0) {
                locked_ = true;
            }
        }
        ~OllamaResidencySharedLock() {
            if (locked_) {
                ReleaseMutex(mutex_);
            }
            if (mutex_) {
                CloseHandle(mutex_);
            }
        }
        bool locked() const { return locked_; }
    private:
        HANDLE mutex_ = nullptr;
        bool locked_ = false;
    };
    static std::atomic_uint64_t& OllamaResidencyFallbackGeneration() {
        static std::atomic_uint64_t generation{ 0 };
        return generation;
    }
    static std::atomic_int& OllamaResidencyFallbackActiveCount() {
        static std::atomic_int activeCount{ 0 };
        return activeCount;
    }
    static OllamaResidencySharedState* OllamaResidencySharedStatePtr() {
        static HANDLE mapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(sizeof(OllamaResidencySharedState)),
            L"Local\\RTAS_OllamaResidency_v1");
        if (!mapping) {
            return nullptr;
        }
        static auto* state = static_cast<OllamaResidencySharedState*>(
            MapViewOfFile(mapping,
                          FILE_MAP_ALL_ACCESS,
                          0,
                          0,
                          sizeof(OllamaResidencySharedState)));
        return state;
    }
    static OllamaResidencyState BeginOllamaResidencyActivation() {
        if (auto* shared = OllamaResidencySharedStatePtr()) {
            OllamaResidencySharedLock lock;
            if (lock.locked()) {
                if (shared->activeCount < 0) {
                    shared->activeCount = 0;
                }
                ++shared->activeCount;
                ++shared->generation;
                return { static_cast<int>(shared->activeCount), shared->generation };
            }
        }
        const int activeCount =
            OllamaResidencyFallbackActiveCount().fetch_add(1, std::memory_order_acq_rel) + 1;
        const uint64_t generation =
            OllamaResidencyFallbackGeneration().fetch_add(1, std::memory_order_acq_rel) + 1;
        return { activeCount, generation };
    }
    static OllamaResidencyState EndOllamaResidencyActivation() {
        if (auto* shared = OllamaResidencySharedStatePtr()) {
            OllamaResidencySharedLock lock;
            if (lock.locked()) {
                if (shared->activeCount <= 0) {
                    shared->activeCount = 0;
                } else {
                    --shared->activeCount;
                }
                return { static_cast<int>(shared->activeCount), shared->generation };
            }
        }
        int previous =
            OllamaResidencyFallbackActiveCount().fetch_sub(1, std::memory_order_acq_rel);
        if (previous <= 0) {
            OllamaResidencyFallbackActiveCount().store(0, std::memory_order_release);
            previous = 0;
        }
        return {
            std::max(0, previous - 1),
            OllamaResidencyFallbackGeneration().load(std::memory_order_acquire)
        };
    }
    static OllamaResidencyState ReadOllamaResidencyState() {
        if (auto* shared = OllamaResidencySharedStatePtr()) {
            OllamaResidencySharedLock lock;
            if (lock.locked()) {
                return { static_cast<int>(std::max<LONG>(0, shared->activeCount)),
                         shared->generation };
            }
        }
        return {
            std::max(0, OllamaResidencyFallbackActiveCount().load(std::memory_order_acquire)),
            OllamaResidencyFallbackGeneration().load(std::memory_order_acquire)
        };
    }
    static bool ShouldUseTranslationLlmResidency(
        const ime::config::ProviderSettings& settings) {
        return settings.translation.mode == ime::config::TranslationMode::kLLM;
    }
    TranslationLlmSettings BuildTranslationLlmSettings() const {
        TranslationLlmSettings llmSettings;
        const auto& configured = m_providerSettings.translation.llm;
        llmSettings.model = Utf8ToWide(configured.model);
        llmSettings.host = Utf8ToWide(configured.host);
        llmSettings.port = configured.port > 0
            ? static_cast<uint16_t>(configured.port)
            : static_cast<uint16_t>(11434);
        llmSettings.path = Utf8ToWide(configured.path);
        llmSettings.useTls = configured.use_tls;
        llmSettings.timeoutMs = configured.timeout_ms;
        llmSettings.keepAlive = Utf8ToWide(configured.keep_alive);
        llmSettings.warmupOnActivate = configured.warmup_on_activate;
        llmSettings.warmupTimeoutMs = configured.warmup_timeout_ms;
        llmSettings.unloadOnDeactivate = configured.unload_on_deactivate;
        llmSettings.unloadDelayMs = configured.unload_delay_ms;
        llmSettings.logTimings = configured.log_timings;
        return llmSettings;
    }
    void StartOllamaResidencyOnActivate() {
        if (m_ollamaResidencyActive) {
            return;
        }
        m_ollamaResidencyActive = true;
        const OllamaResidencyState residency = BeginOllamaResidencyActivation();
        m_ollamaResidencyGeneration = residency.generation;
        if (residency.activeCount > 1) {
            DebugLog(L"Ollama residency warmup skipped: another RTAS activation is active");
            return;
        }
        if (!ShouldUseTranslationLlmResidency(m_providerSettings)) {
            DebugLog(L"Ollama residency warmup skipped: translation mode is not llm");
            return;
        }
        TranslationLlmSettings llmSettings = BuildTranslationLlmSettings();
        if (!llmSettings.warmupOnActivate) {
            DebugLog(L"Ollama residency warmup disabled by config; LLM cold-start latency may increase");
            return;
        }
        const uint64_t generation = m_ollamaResidencyGeneration;
        std::thread([llmSettings, generation]() {
            struct ModuleRef {
                ModuleRef() { InterlockedIncrement(&g_cDllRef); }
                ~ModuleRef() { InterlockedDecrement(&g_cDllRef); }
            } moduleRef;
            const OllamaResidencyState residency = TextService::ReadOllamaResidencyState();
            if (residency.generation != generation || residency.activeCount <= 0) {
                DebugLog(L"Ollama residency warmup cancelled: RTAS deactivated");
                return;
            }
            OllamaLifecycleResult result = WarmupOllamaModel(llmSettings);
            if (result.cancelled) {
                DebugLog(L"Ollama residency warmup cancelled");
                return;
            }
            if (!result.success) {
                std::wstring message = L"Ollama residency warmup failed: ";
                message += result.error.empty() ? L"<unknown>" : result.error;
                DebugLog(message.c_str());
                return;
            }
            std::wstring message = L"Ollama residency warmup complete for model=";
            message += result.model;
            DebugLog(message.c_str());
        }).detach();
    }
    void StopOllamaResidencyOnDeactivate() {
        if (!m_ollamaResidencyActive) {
            return;
        }
        m_ollamaResidencyActive = false;
        const OllamaResidencyState residency = EndOllamaResidencyActivation();
        if (residency.activeCount > 0) {
            DebugLog(L"Ollama residency unload skipped: another RTAS activation remains active");
            return;
        }
        if (!ShouldUseTranslationLlmResidency(m_providerSettings)) {
            DebugLog(L"Ollama residency unload skipped: translation mode is not llm");
            return;
        }
        TranslationLlmSettings llmSettings = BuildTranslationLlmSettings();
        if (!llmSettings.unloadOnDeactivate) {
            DebugLog(L"Ollama residency unload disabled by config; model may remain resident");
            return;
        }
        const uint64_t generation = m_ollamaResidencyGeneration;
        const int delayMs = std::max(0, llmSettings.unloadDelayMs);
        std::thread([llmSettings, generation, delayMs]() {
            struct ModuleRef {
                ModuleRef() { InterlockedIncrement(&g_cDllRef); }
                ~ModuleRef() { InterlockedDecrement(&g_cDllRef); }
            } moduleRef;
            if (delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
            const OllamaResidencyState residency = TextService::ReadOllamaResidencyState();
            if (residency.generation != generation || residency.activeCount > 0) {
                DebugLog(L"Ollama residency unload cancelled: RTAS reactivated");
                return;
            }
            OllamaLifecycleResult result = UnloadOllamaModel(llmSettings);
            if (!result.success) {
                std::wstring message = L"Ollama residency unload failed: ";
                message += result.error.empty() ? L"<unknown>" : result.error;
                DebugLog(message.c_str());
                return;
            }
            std::wstring message = L"Ollama residency unload complete for model=";
            message += result.model;
            DebugLog(message.c_str());
        }).detach();
    }
    static std::wstring CandidateDisplayText(const llm::CandidateEntry& entry) {
        if (!entry.displayText.empty()) return entry.displayText;
        if (!entry.commitText.empty()) return entry.commitText;
        return entry.reading;
    }
    void UpdatePendingVisualFlag() {
        const bool pending = m_layer2PendingActive || m_translationPendingActive;
        if (pending == m_translationPendingShown) {
            return;
        }
        m_translationPendingShown = pending;
        UpdateOverlayFromState();
    }
    void SetLayer2Pending(bool pending) {
        if (m_layer2PendingActive == pending) return;
        m_layer2PendingActive = pending;
        UpdatePendingVisualFlag();
    }
    void SetTranslationPending(bool pending) {
        if (m_translationPendingActive == pending) return;
        m_translationPendingActive = pending;
        UpdatePendingVisualFlag();
    }
    std::wstring BuildTranslationCacheKey(const std::wstring& source) const {
        return TrimWhitespace(source);
    }
    void TouchTranslationCacheEntry(TranslationCacheEntry& entry) {
        entry.lastAccessStamp = ++m_translationCacheStamp;
        if (!entry.candidates.empty()) {
            entry.index %= entry.candidates.size();
        } else {
            entry.index = 0;
        }
    }
    void EnforceTranslationCacheLimits() {
        while (m_translationCache.size() > m_maxTranslationCacheEntries && !m_translationCache.empty()) {
            auto lru = m_translationCache.begin();
            for (auto it = m_translationCache.begin(); it != m_translationCache.end(); ++it) {
                if (it->second.lastAccessStamp < lru->second.lastAccessStamp) {
                    lru = it;
                }
            }
            m_translationCache.erase(lru);
        }
    }
    std::wstring BuildLayer2CacheKey(const std::wstring& source) const {
        return TrimWhitespace(source);
    }
    static bool HasSentenceDelimiter(const std::wstring& text) {
        for (wchar_t ch : text) {
            switch (ch) {
            case L'、':
            case L'。':
            case L',':
            case L'.':
            case L'！':
            case L'!':
            case L'？':
            case L'?':
            case L' ':
            case L'　':
                return true;
            default:
                break;
            }
        }
        return false;
    }
    static bool ContainsKanjiChar(const std::wstring& text) {
        for (wchar_t ch : text) {
            if ((ch >= 0x4E00 && ch <= 0x9FFF) ||  // CJK Unified Ideographs
                (ch >= 0x3400 && ch <= 0x4DBF)) {  // CJK Extension A
                return true;
            }
        }
        return false;
    }
    static size_t FindFallbackClauseBoundary(const std::wstring& reading) {
        // Heuristic boundary for no-delimiter Japanese input.
        // Prefer splits around common particles/end markers.
        // For long readings, avoid early-particle splits because they can
        // duplicate an already-converted prefix ("...変な" + "なんか...").
        if (reading.size() < 4) return std::wstring::npos;
        size_t start = 2;
        if (reading.size() >= 10) {
            start = reading.size() / 2;
            if (start < 2) start = 2;
        }
        for (size_t i = start; i + 1 < reading.size(); ++i) {
            const wchar_t ch = reading[i - 1];
            switch (ch) {
            case L'は':
            case L'を':
            case L'が':
            case L'に':
            case L'で':
            case L'と':
            case L'も':
            case L'へ':
            case L'や':
            case L'の':
            case L'か':
                return i;
            default:
                break;
            }
        }
        return std::wstring::npos;
    }
    static std::wstring MergePartialWithReadingTail(const std::wstring& candidate, const std::wstring& reading) {
        if (candidate.empty() || reading.empty()) return candidate;
        if (candidate.size() + 2 >= reading.size()) return candidate;
        const size_t boundary = FindFallbackClauseBoundary(reading);
        if (boundary == std::wstring::npos || boundary >= reading.size()) {
            // No safe clause boundary found.
            return std::wstring();
        }
        const std::wstring tail = reading.substr(boundary);
        if (tail.empty()) {
            return candidate;
        }
        // Avoid duplicating suffix when candidate already includes it
        // (e.g. "長文打ってると" + "うってると").
        if (candidate.find(tail) != std::wstring::npos) {
            return candidate;
        }
        std::wstring merged = candidate;
        merged.append(tail);
        return merged;
    }
    bool IsLikelyPartialLayer1Candidate(const std::wstring& candidate) const {
        if (m_activeCandidateTab != CandidateTab::Layer1) return false;
        // When appending new text after a Layer2 merge, do not apply
        // partial-candidate tail merge heuristics. It can duplicate the
        // suffix (e.g. "...なんか" -> "...南下なんか").
        if (m_layer2MergedCarryOpen) return false;
        const std::wstring selected = TrimWhitespace(candidate);
        const std::wstring reading = ActiveDraftText();
        if (selected.empty() || reading.empty()) return false;
        // If selected candidate already contains kanji, treat it as a
        // complete conversion result rather than a partial prefix.
        if (ContainsKanjiChar(selected)) return false;
        // Heuristic is only for all-kana input. If reading already contains
        // kanji, tail merging tends to corrupt mixed text.
        if (ContainsKanjiChar(reading)) return false;
        if (HasSentenceDelimiter(reading)) return false;
        // If conversion result is much shorter than source reading, it is likely
        // a first-clause-only candidate from mozc bridge output.
        return (selected.size() + 2 < reading.size());
    }
    std::wstring ResolveLayer1SourceText(const std::wstring& selected) const {
        const std::wstring normalizedSelected = TrimWhitespace(selected);
        std::wstring reading = ActiveDraftText();
        if (reading.empty()) return normalizedSelected;
        // In mozc mode, keep Layer1->Layer2 transition source identical to
        // the selected mozc candidate to preserve pure mozc behavior.
        if (m_providerSettings.kana.mode == ime::config::ProviderMode::kMozc) {
            return normalizedSelected.empty() ? reading : normalizedSelected;
        }
        // In carry-forward mode, never let Layer1 short candidates drop the
        // still-editing suffix. Keep the current full reading as source text.
        if (HasLayer2LockedCarry()) {
            const bool readingHasLockedPrefix =
                reading.size() >= m_layer2LockedPrefix.size() &&
                reading.compare(0, m_layer2LockedPrefix.size(), m_layer2LockedPrefix) == 0;
            if (readingHasLockedPrefix) {
                // Fallback to raw reading only when Layer1 selection is clearly
                // a strict-prefix truncation. Do not use length-only checks,
                // because kanji conversion can legitimately shorten the string
                // (e.g. "げんき" -> "元気").
                const bool selectedHasLockedPrefix =
                    normalizedSelected.size() >= m_layer2LockedPrefix.size() &&
                    normalizedSelected.compare(0, m_layer2LockedPrefix.size(), m_layer2LockedPrefix) == 0;
                if (!selectedHasLockedPrefix) {
                    return reading;
                }
                if (reading.size() > normalizedSelected.size() &&
                    reading.compare(0, normalizedSelected.size(), normalizedSelected) == 0) {
                    return reading;
                }
            }
        }
        if (!IsLikelyPartialLayer1Candidate(normalizedSelected)) {
            return normalizedSelected;
        }
        if (HasSentenceDelimiter(reading)) {
            return reading;
        }
        std::wstring merged = MergePartialWithReadingTail(normalizedSelected, reading);
        if (!merged.empty()) {
            return merged;
        }
        // When no safe boundary exists, keep long/near-full candidates as-is,
        // but avoid truncating clearly short candidates to only one phrase.
        if (normalizedSelected.size() * 2 <= reading.size()) {
            return reading;
        }
        return normalizedSelected;
    }
    void TouchLayer2CacheEntry(Layer2CacheEntry& entry) {
        entry.lastAccessStamp = ++m_layer2CacheStamp;
        if (!entry.candidates.empty()) {
            entry.index %= entry.candidates.size();
        } else {
            entry.index = 0;
        }
    }
    void EnforceLayer2CacheLimits() {
        while (m_layer2Cache.size() > m_maxLayer2CacheEntries && !m_layer2Cache.empty()) {
            auto lru = m_layer2Cache.begin();
            for (auto it = m_layer2Cache.begin(); it != m_layer2Cache.end(); ++it) {
                if (it->second.lastAccessStamp < lru->second.lastAccessStamp) {
                    lru = it;
                }
            }
            m_layer2Cache.erase(lru);
        }
    }
    void ShowPendingPlaceholder(bool notifyOwner = false) {
        if (!m_candidateUI) return;
        std::vector<std::wstring> pending{ kTranslationPendingPlaceholder };
        m_candidateUI->SetCandidates(std::move(pending), 0, notifyOwner);
    }
    void ShowLayer1Candidates(bool notifyOwner = true) {
        if (!m_candidateUI) return;
        if (m_layer1DisplayCache.empty()) {
            std::wstring fallback = m_candidateReading.empty() ? PreeditText() : m_candidateReading;
            if (!fallback.empty()) {
                m_layer1DisplayCache.push_back(std::move(fallback));
            }
        }
        if (m_layer1DisplayCache.empty()) {
            m_layer1DisplayCache.push_back(L"(no candidates)");
            notifyOwner = false;
        }
        if (m_layer1Selection >= m_layer1DisplayCache.size()) {
            m_layer1Selection = m_layer1DisplayCache.empty() ? 0 : m_layer1DisplayCache.size() - 1;
        }
        m_candidateUI->SetCandidates(m_layer1DisplayCache, static_cast<int>(m_layer1Selection), notifyOwner);
    }
    void ShowLayer2CandidatesFromCache(bool notifyOwner = true) {
        if (!m_candidateUI) return;
        std::vector<std::wstring> display;
        int selection = 0;
        auto it = m_layer2Cache.find(m_layer2SourceKey);
        if (it != m_layer2Cache.end() && !it->second.candidates.empty()) {
            display.reserve(it->second.candidates.size());
            for (const auto& entry : it->second.candidates) {
                display.push_back(CandidateDisplayText(entry));
            }
            selection = static_cast<int>(it->second.index % it->second.candidates.size());
        } else {
            std::wstring message = (it != m_layer2Cache.end() && !it->second.error.empty())
                ? it->second.error
                : std::wstring(kLayer2ErrorPlaceholder);
            display.push_back(std::move(message));
            notifyOwner = false;
        }
        m_candidateUI->SetCandidates(std::move(display), selection, notifyOwner);
    }
    void ShowTranslationCandidatesFromCache(bool notifyOwner = true) {
        if (!m_candidateUI) return;
        std::vector<std::wstring> display;
        int selection = 0;
        auto it = m_translationCache.find(m_candidateSourceKey);
        if (it != m_translationCache.end() && !it->second.candidates.empty()) {
            display.reserve(it->second.candidates.size());
            for (const auto& entry : it->second.candidates) {
                display.push_back(CandidateDisplayText(entry));
            }
            selection = static_cast<int>(it->second.index % it->second.candidates.size());
        } else {
            std::wstring message = (it != m_translationCache.end() && !it->second.error.empty())
                ? it->second.error
                : std::wstring(kTranslationErrorPlaceholder);
            display.push_back(std::move(message));
            notifyOwner = false;
        }
        m_candidateUI->SetCandidates(std::move(display), selection, notifyOwner);
    }
    bool RequestLayer2Alternatives(bool forceRefresh = false) {
        if (!m_candidateUI) return false;
        std::wstring source = TrimWhitespace(m_layer2SourceText.empty() ? m_candidateUI->SelectedString() : m_layer2SourceText);
        if (source.empty()) return false;
        m_layer2SourceText = source;
        const std::wstring key = BuildLayer2CacheKey(source);
        m_layer2SourceKey = key;
        if (!forceRefresh) {
            auto it = m_layer2Cache.find(key);
            if (it != m_layer2Cache.end() && (!it->second.candidates.empty() || !it->second.error.empty())) {
                TouchLayer2CacheEntry(it->second);
                EnforceLayer2CacheLimits();
                if (m_activeCandidateTab == CandidateTab::Layer2) {
                    ShowLayer2CandidatesFromCache();
                }
                return true;
            }
        } else {
            m_layer2Cache.erase(key);
        }
        CancelActiveLayer2Request();
        if (!m_conversionProvider) {
            return false;
        }
        ime::conversion::LayerRequestContext ctx;
        ctx.reading = source;
        ctx.committedText = source;
        ctx.layer = 2;
        ctx.allowAsync = true;
        auto list = m_conversionProvider->FetchLayer2(ctx);
        if (!list.pending) {
            ApplyLayer2Candidates(key, std::move(list));
            return true;
        }
        if (list.requestId) {
            {
                std::lock_guard<std::mutex> lock(m_layer2Mutex);
                m_layer2Pending[*list.requestId] = key;
            }
            m_activeLayer2RequestId = *list.requestId;
            SetLayer2Pending(true);
            if (m_activeCandidateTab == CandidateTab::Layer2) {
                ShowPendingPlaceholder(false);
            }
            return true;
        }
        ApplyLayer2Candidates(key, std::move(list));
        return true;
    }
    void ApplyLayer2Candidates(const std::wstring& key, ime::conversion::CandidateList result) {
        if (!result.error.empty()) {
            DebugLog(result.error.c_str());
        }
        if (result.entries.size() > m_maxLayer2CandidatesPerEntry) {
            result.entries.resize(m_maxLayer2CandidatesPerEntry);
        }
        if (!key.empty()) {
            Layer2CacheEntry entry;
            entry.candidates = std::move(result.entries);
            entry.error = result.error;
            entry.index = 0;
            entry.lastAccessStamp = m_layer2CacheStamp;
            m_layer2Cache[key] = std::move(entry);
            auto it = m_layer2Cache.find(key);
            if (it != m_layer2Cache.end()) {
                TouchLayer2CacheEntry(it->second);
                EnforceLayer2CacheLimits();
            }
        }
        if (m_activeCandidateTab == CandidateTab::Layer2 && key == m_layer2SourceKey) {
            ShowLayer2CandidatesFromCache(true);
        }
        SetLayer2Pending(false);
    }
    bool SwitchToLayer2FromLayer1(
        bool forceRefresh = false,
        const std::wstring* explicitSourceText = nullptr) {
        if (!SupportsLayer2Flow()) return false;
        if (!m_candidateUI) return false;
        std::wstring selected = m_segmentMode ? ComposeSegmentReading() : m_candidateUI->SelectedString();
        std::wstring source;
        if (explicitSourceText) {
            source = TrimWhitespace(*explicitSourceText);
        }
        if (source.empty()) {
            if (selected.empty()) return false;
            source = ResolveLayer1SourceText(selected);
        }
        if (source.empty()) {
            source = selected;
        }
        if (source.empty()) return false;
        m_layer2SourceText = std::move(source);
        m_layer2SourceKey = BuildLayer2CacheKey(m_layer2SourceText);
        m_segmentMode = false;
        m_layer1SegmentRuntime.clear();
        m_segmentDisplayRanges.clear();
        m_segmentBaseReading.clear();
        SetLayer1Merged(false);
        m_activeCandidateTab = CandidateTab::Layer2;
        ClearLayer2LockedCarry();
        if (!RequestLayer2Alternatives(forceRefresh)) {
            m_activeCandidateTab = CandidateTab::Layer1;
            ShowLayer1Candidates(true);
            SetPendingLayer2AutoCommit(false);
            return false;
        }
        // Force redraw so display attribute switches to Layer2 color
        // even when selected text stays identical.
        m_candidatePreview.clear();
        PreviewCandidateString(m_layer2SourceText);
        SetPendingLayer2AutoCommit(true);
        // Half-width lock is scoped to blue/red preedit only.
        // Once we enter Layer2 (orange), end that mode.
        ResetForceHalfWidthInput();
        return true;
    }
    bool HandleLayer2Space(bool shiftPressed) {
        if (m_layer2SourceKey.empty()) return false;
        if (shiftPressed) {
            return RequestLayer2Alternatives(true);
        }
        auto it = m_layer2Cache.find(m_layer2SourceKey);
        if (it == m_layer2Cache.end() || it->second.candidates.empty()) {
            return RequestLayer2Alternatives(false);
        }
        if (it->second.candidates.size() <= 1) {
            return false;
        }
        it->second.index = (it->second.index + 1) % it->second.candidates.size();
        it->second.lastAccessStamp = ++m_layer2CacheStamp;
        ShowLayer2CandidatesFromCache(true);
        return true;
    }
    bool ResolveLayer2SelectionText(std::wstring& selection) {
        selection.clear();
        auto it = m_layer2Cache.find(m_layer2SourceKey);
        if (it == m_layer2Cache.end() || it->second.candidates.empty()) {
            return false;
        }
        size_t idx = 0;
        if (m_candidateUI) {
            int sel = m_candidateUI->SelectionIndex();
            if (sel > 0) idx = static_cast<size_t>(sel);
        }
        if (idx >= it->second.candidates.size()) {
            idx = it->second.candidates.size() - 1;
        }
        it->second.index = idx;
        const auto& entry = it->second.candidates[idx];
        selection = entry.commitText.empty() ? entry.displayText : entry.commitText;
        return !selection.empty();
    }
    void MergeLayer2Selection(ITfContext* context) {
        SetPendingLayer2AutoCommit(false);
        std::wstring merged;
        if (!ResolveLayer2SelectionText(merged)) {
            m_activeCandidateTab = CandidateTab::Layer1;
            ShowLayer1Candidates(false);
            return;
        }
        DraftText() = merged;
        m_layer2LockedPrefix = merged;
        m_layer1DisplayCache.assign(1, merged);
        m_layer1Selection = 0;
        SetLayer1Merged(true);
        SetCandidateSourceText(merged);
        m_activeCandidateTab = CandidateTab::Layer1;
        // Layer2 selection was merged (orange state established), so end
        // half-width lock from the prior blue/red preedit.
        ResetForceHalfWidthInput();
        ShowLayer1Candidates(true);
        PreviewCandidateString(merged);
        ITfContext* target = context ? context : m_candidateContext;
        if (target) {
            UpdateCompositionText(target, merged);
        }
    }
    void CommitLayer2Selection(ITfContext* context) {
        SetPendingLayer2AutoCommit(false);
        std::wstring commit;
        if (!ResolveLayer2SelectionText(commit)) {
            m_activeCandidateTab = CandidateTab::Layer1;
            ShowLayer1Candidates(false);
            return;
        }
        // Enter on Layer2 should provide a direct one-step Japanese commit.
        std::wstring source = TrimWhitespace(m_layer2SourceText);
        if (source.empty()) {
            source = ActiveDraftText();
        }
        if (!source.empty()) {
            LearnCandidateSelection(source, commit);
        }
        ITfContext* target = context ? context : m_candidateContext;
        CloseCandidateUI();
        CommitComposition(target, commit);
    }
    void CommitTranslationSelection(ITfContext* context) {
        auto it = m_translationCache.find(m_candidateSourceKey);
        if (it == m_translationCache.end() || it->second.candidates.empty()) return;
        size_t idx = 0;
        if (m_candidateUI) {
            int sel = m_candidateUI->SelectionIndex();
            if (sel > 0) idx = static_cast<size_t>(sel);
        }
        if (idx >= it->second.candidates.size()) {
            idx = it->second.candidates.size() - 1;
        }
        it->second.index = idx;
        const auto& entry = it->second.candidates[idx];
        std::wstring commit = entry.commitText.empty() ? entry.displayText : entry.commitText;
        if (commit.empty()) return;
        ITfContext* target = context ? context : m_candidateContext;
        CloseCandidateUI();
        CommitComposition(target, commit);
    }
    // Auto-commit the active Layer2 paraphrase before we append new input.
    bool FinalizeLayer2BeforeNewInput(ITfContext* context) {
        if (!m_candidateUI) return false;
        if (HasPendingLayer2AutoCommitTransition()) {
            if (m_activeCandidateTab == CandidateTab::Layer2) {
                MergeLayer2Selection(context);
            }
            if (!m_candidateUI) {
                m_layer2MergedCarryOpen = false;
                return false;
            }
            if (m_activeCandidateTab != CandidateTab::Layer1) return false;
            SetPendingLayer2AutoCommit(false);
            m_layer2MergedCarryOpen = true;
        }
        if (!HasLayer2LockedCarry() && !m_layer2LockedPrefix.empty()) {
            m_layer2MergedCarryOpen = true;
        }
        if (m_layer2MergedCarryOpen) {
            SetLayer1Merged(false);
            return true;
        }
        return false;
    }
    bool StepBackLockedPrefixBoundary() {
        if (!HasLayer2LockedCarry()) return false;
        if (!m_romajiBuffer.empty()) return false;
        if (DraftText().size() < m_layer2LockedPrefix.size()) {
            ClearLayer2LockedCarry();
            return false;
        }
        if (DraftText().compare(0, m_layer2LockedPrefix.size(), m_layer2LockedPrefix) != 0) {
            ClearLayer2LockedCarry();
            return false;
        }
        const size_t tailLength = DraftText().size() - m_layer2LockedPrefix.size();
        if (tailLength > 0) {
            return false;
        }
        if (DraftText().empty()) {
            ClearLayer2LockedCarry();
            return false;
        }
        DraftText().pop_back();
        m_layer2LockedPrefix.pop_back();
        if (m_layer2LockedPrefix.empty()) {
            m_layer2MergedCarryOpen = false;
        }
        SetLayer1Merged(false);
        return true;
    }
    void RefreshCandidateSourceFromDraft() {
        if (!m_candidateUI) return;
        std::wstring trimmed = ActiveDraftText();
        if (trimmed.empty()) return;
        auto rebuildSegmentMode = [this]() {
            m_segmentMode = false;
            m_layer1SegmentRuntime.clear();
            m_segmentDisplayRanges.clear();
            m_segmentBaseReading.clear();
            m_activeSegmentIndex = 0;
            m_activeSegmentDisplayStart = 0;
            m_activeSegmentDisplayLength = 0;
            m_hasActiveSegmentDisplayRange = false;
            InitializeSegmentMode();
        };
        if (HasLayer2LockedCarry()) {
            if (trimmed.size() >= m_layer2LockedPrefix.size() &&
                trimmed.compare(0, m_layer2LockedPrefix.size(), m_layer2LockedPrefix) == 0) {
                if (trimmed != m_candidateReading) {
                    std::wstring tail = trimmed.substr(m_layer2LockedPrefix.size());
                    auto cands = CollectLayer1CandidatesWithLockedPrefix(
                        m_layer2LockedPrefix, tail, false, true);
                    if (cands.empty()) {
                        cands.push_back(trimmed);
                    }
                    m_candidateReading = trimmed;
                    m_layer1DisplayCache = std::move(cands);
                    m_layer1Selection = 0;
                    m_candidateUI->SetCandidates(m_layer1DisplayCache, 0, true);
                    PreviewCandidateString(m_candidateUI->SelectedString());
                    rebuildSegmentMode();
                }
                SetCandidateSourceText(trimmed);
                return;
            }
            ClearLayer2LockedCarry();
        }
        if (trimmed == m_candidateReading) {
            if (IsLayer1MergedFromLayer2()) {
                SetCandidateSourceText(trimmed);
            }
            return;
        }
        auto cands = CollectLayer1BaseCandidates(trimmed, false);
        if (cands.empty()) {
            cands.push_back(trimmed);
        }
        m_candidateReading = trimmed;
        m_layer1DisplayCache = std::move(cands);
        m_layer1Selection = 0;
        m_candidateUI->SetCandidates(m_layer1DisplayCache, 0, true);
        PreviewCandidateString(m_candidateUI->SelectedString());
        rebuildSegmentMode();
        SetCandidateSourceText(trimmed);
    }
    void HandleTabEnter(ITfContext* context, bool shiftPressed = false) {
        if (!m_candidateUI) return;
        switch (m_activeCandidateTab) {
        case CandidateTab::Layer1:
            // Enter on Layer1 should only commit when the current reading is
            // already a merged Layer2 result. Otherwise transition to Layer2.
            if (IsLayer1MergedFromLayer2() || !SupportsLayer2Flow()) {
                CommitCandidateSelection(context);
            } else {
                SwitchToLayer2FromLayer1(false);
            }
            break;
        case CandidateTab::Layer2:
            // Enter commits Layer2 selection directly; Shift+Enter keeps the
            // previous merge-back path for continued Layer1 editing.
            if (shiftPressed) {
                MergeLayer2Selection(context);
            } else {
                CommitLayer2Selection(context);
            }
            break;
        case CandidateTab::Translation:
            CommitTranslationSelection(context);
            break;
        }
    }
    void CycleCandidateTab(bool reverse) {
        if (!m_candidateUI) return;
        std::vector<CandidateTab> order;
        order.push_back(CandidateTab::Layer1);
        if (SupportsLayer2Flow()) {
            order.push_back(CandidateTab::Layer2);
        }
        if (SupportsTranslationFlow()) {
            order.push_back(CandidateTab::Translation);
        }
        if (order.size() <= 1) {
            m_activeCandidateTab = CandidateTab::Layer1;
            ShowLayer1Candidates(true);
            return;
        }
        int idx = 0;
        for (int i = 0; i < static_cast<int>(order.size()); ++i) {
            if (order[i] == m_activeCandidateTab) { idx = i; break; }
        }
        for (size_t attempt = 0; attempt < order.size(); ++attempt) {
            idx = reverse ? (idx - 1 + static_cast<int>(order.size())) % static_cast<int>(order.size())
                          : (idx + 1) % static_cast<int>(order.size());
            CandidateTab next = order[idx];
            if (next == CandidateTab::Layer1) {
                m_activeCandidateTab = CandidateTab::Layer1;
                ShowLayer1Candidates(true);
                return;
            }
            if (next == CandidateTab::Layer2) {
                if (m_activeCandidateTab == CandidateTab::Layer1 && SwitchToLayer2FromLayer1(false)) return;
                continue;
            }
            if (next == CandidateTab::Translation) {
                if (IsLayer1MergedFromLayer2() && SwitchToTranslationTab(false)) return;
            }
        }
    }
    uint64_t QueueTranslationAsync(const std::wstring& reading, const std::wstring& context) {
        const std::wstring trimmed = TrimWhitespace(reading);
        TranslationLlmSettings llmSettings = BuildTranslationLlmSettings();
        auto job = [this, trimmed, context, llmSettings](uint64_t id, const AsyncWorkQueue::CancelFlag& cancelFlag) {
            TranslationResult result = ExecuteTranslationJob(id, trimmed, context, cancelFlag, llmSettings);
            PostMainThreadCallback([this, res = std::move(result)]() mutable {
                OnTranslationReady(std::move(res));
            });
        };
        const uint64_t id = m_asyncQueue.Enqueue(std::move(job));
        if (id) {
            std::lock_guard<std::mutex> lock(m_translationMutex);
            m_translationPending[id] = BuildTranslationCacheKey(trimmed);
        }
        return id;
    }
    void CancelTranslationAsync(uint64_t id) {
        if (!id) return;
        m_asyncQueue.Cancel(id);
        if (m_conversionProvider) {
            m_conversionProvider->Cancel(id);
        }
        std::lock_guard<std::mutex> lock(m_translationMutex);
        m_translationPending.erase(id);
    }
    void CancelAllTranslations() {
        std::unordered_map<uint64_t, std::wstring> pending;
        {
            std::lock_guard<std::mutex> lock(m_translationMutex);
            pending.swap(m_translationPending);
        }
        for (const auto& entry : pending) {
            m_asyncQueue.Cancel(entry.first);
            if (m_conversionProvider) {
                m_conversionProvider->Cancel(entry.first);
            }
        }
        m_activeTranslationRequestId = 0;
        SetTranslationPending(false);
    }
    void CancelActiveTranslationRequest() {
        if (!m_activeTranslationRequestId) return;
        CancelTranslationAsync(m_activeTranslationRequestId);
        m_activeTranslationRequestId = 0;
        SetTranslationPending(false);
    }
    void CancelActiveLayer2Request() {
        if (!m_activeLayer2RequestId) return;
        if (m_conversionProvider) {
            m_conversionProvider->Cancel(m_activeLayer2RequestId);
        }
        {
            std::lock_guard<std::mutex> lock(m_layer2Mutex);
            m_layer2Pending.erase(m_activeLayer2RequestId);
        }
        m_activeLayer2RequestId = 0;
        SetLayer2Pending(false);
    }
    void CancelAllLayer2Requests() {
        std::unordered_map<uint64_t, std::wstring> pending;
        {
            std::lock_guard<std::mutex> lock(m_layer2Mutex);
            pending.swap(m_layer2Pending);
        }
        for (const auto& entry : pending) {
            if (m_conversionProvider) {
                m_conversionProvider->Cancel(entry.first);
            }
        }
        m_activeLayer2RequestId = 0;
        SetLayer2Pending(false);
    }
    void ApplyTranslationCandidates(const std::wstring& key, ime::conversion::CandidateList result) {
        if (!result.error.empty()) {
            DebugLog(result.error.c_str());
        }
        if (result.entries.size() > m_maxTranslationCandidatesPerEntry) {
            result.entries.resize(m_maxTranslationCandidatesPerEntry);
        }
        if (!key.empty()) {
            TranslationCacheEntry entry;
            entry.candidates = std::move(result.entries);
            entry.error = result.error;
            entry.index = 0;
            entry.lastAccessStamp = m_translationCacheStamp;
            m_translationCache[key] = std::move(entry);
            auto it = m_translationCache.find(key);
            if (it != m_translationCache.end()) {
                TouchTranslationCacheEntry(it->second);
                EnforceTranslationCacheLimits();
            }
        }
        if (m_activeCandidateTab == CandidateTab::Translation && key == m_candidateSourceKey) {
            ShowTranslationCandidatesFromCache(true);
        }
        SetTranslationPending(false);
    }
    bool StartTranslationForCandidate(const std::wstring& source, bool forceRefresh = false) {
        std::wstring trimmed = TrimWhitespace(source);
        if (trimmed.empty()) return false;
        const std::wstring key = BuildTranslationCacheKey(trimmed);
        SetCandidateSourceText(trimmed);
        if (!forceRefresh) {
            auto it = m_translationCache.find(key);
            if (it != m_translationCache.end() && (!it->second.candidates.empty() || !it->second.error.empty())) {
                TouchTranslationCacheEntry(it->second);
                EnforceTranslationCacheLimits();
                if (m_activeCandidateTab == CandidateTab::Translation) {
                    ShowTranslationCandidatesFromCache(true);
                }
                return true;
            }
        } else {
            m_translationCache.erase(key);
        }
        CancelActiveTranslationRequest();
        if (m_conversionProvider) {
            ime::conversion::LayerRequestContext ctx;
            ctx.reading = trimmed;
            ctx.committedText = trimmed;
            ctx.layer = 3;
            ctx.allowAsync = true;
            auto list = m_conversionProvider->FetchTranslation(ctx);
            if (!list.pending) {
                ApplyTranslationCandidates(key, std::move(list));
                return true;
            }
            if (list.pending && list.requestId) {
                {
                    std::lock_guard<std::mutex> lock(m_translationMutex);
                    m_translationPending[*list.requestId] = key;
                }
                m_activeTranslationRequestId = *list.requestId;
                SetTranslationPending(true);
                if (m_activeCandidateTab == CandidateTab::Translation) {
                    ShowPendingPlaceholder(false);
                }
                return true;
            }
            ApplyTranslationCandidates(key, std::move(list));
            return true;
        }
        const uint64_t id = QueueTranslationAsync(trimmed, L"");
        if (id) {
            m_activeTranslationRequestId = id;
            SetTranslationPending(true);
            if (m_activeCandidateTab == CandidateTab::Translation) {
                ShowPendingPlaceholder(false);
            }
            return true;
        }
        return false;
    }
    bool HandleTranslationSpace(bool shiftPressed) {
        if (m_candidateSourceText.empty()) return false;
        if (shiftPressed) {
            return StartTranslationForCandidate(m_candidateSourceText, true);
        }
        auto it = m_translationCache.find(m_candidateSourceKey);
        if (it == m_translationCache.end() || it->second.candidates.empty()) {
            return StartTranslationForCandidate(m_candidateSourceText);
        }
        if (it->second.candidates.size() <= 1) {
            return false;
        }
        it->second.index = (it->second.index + 1) % it->second.candidates.size();
        it->second.lastAccessStamp = ++m_translationCacheStamp;
        ShowTranslationCandidatesFromCache(true);
        return true;
    }
    bool SwitchToTranslationTab(bool forceRefresh = false) {
        if (!SupportsTranslationFlow()) return false;
        if (!m_candidateUI) return false;
        if (m_activeCandidateTab == CandidateTab::Layer2) {
            // Use current Layer2 selection as translation source without requiring merge/commit.
            std::wstring source;
            if (!ResolveLayer2SelectionText(source)) {
                source = m_candidateUI->SelectedString();
            }
            if (!source.empty()) {
                SetCandidateSourceText(source);
            }
        } else if (m_candidateSourceText.empty()) {
            std::wstring base = ActiveDraftText();
            if (base.empty()) {
                base = TrimWhitespace(m_candidateUI->SelectedString());
            }
            if (base.empty()) return false;
            SetCandidateSourceText(base);
        }
        m_activeCandidateTab = CandidateTab::Translation;
        SetPendingLayer2AutoCommit(false);
        ClearLayer2LockedCarry();
        if (!StartTranslationForCandidate(m_candidateSourceText, forceRefresh)) {
            m_activeCandidateTab = CandidateTab::Layer1;
            ShowLayer1Candidates(true);
            return false;
        }
        return true;
    }
    void OnProviderResult(uint64_t requestId, ime::conversion::CandidateList result) {
        std::wstring layer2Key;
        {
            std::lock_guard<std::mutex> lock(m_layer2Mutex);
            auto it = m_layer2Pending.find(requestId);
            if (it != m_layer2Pending.end()) {
                layer2Key = it->second;
                m_layer2Pending.erase(it);
            }
        }
        if (!layer2Key.empty()) {
            if (requestId == m_activeLayer2RequestId) {
                m_activeLayer2RequestId = 0;
            }
            if (!result.pending) {
                ApplyLayer2Candidates(layer2Key, std::move(result));
            }
            return;
        }
        std::wstring key;
        {
            std::lock_guard<std::mutex> lock(m_translationMutex);
            auto it = m_translationPending.find(requestId);
            if (it != m_translationPending.end()) {
                key = it->second;
                m_translationPending.erase(it);
            }
        }
        if (requestId == m_activeTranslationRequestId) {
            m_activeTranslationRequestId = 0;
        }
        if (result.pending) {
            return;
        }
        if (key.empty()) {
            key = m_candidateSourceKey;
        }
        ApplyTranslationCandidates(key, std::move(result));
    }
    void OnTranslationReady(TranslationResult result) {
        std::wstring key;
        {
            std::lock_guard<std::mutex> lock(m_translationMutex);
            auto it = m_translationPending.find(result.requestId);
            if (it != m_translationPending.end()) {
                key = it->second;
                m_translationPending.erase(it);
            }
        }
        const bool isActiveRequest = (result.requestId == m_activeTranslationRequestId);
        if (isActiveRequest) {
            m_activeTranslationRequestId = 0;
        }
        if (result.cancelled) {
            SetTranslationPending(false);
            return;
        }
        ime::conversion::CandidateList list;
        list.layer = 3;
        if (!result.success) {
            list.error = result.error;
            ApplyTranslationCandidates(key, std::move(list));
            return;
        }
        if (key.empty()) {
            key = BuildTranslationCacheKey(result.source);
        }
        llm::CandidateEntry entry;
        entry.id = L"fallback_translation";
        entry.layer = llm::CandidateLayer::Translation;
        entry.displayText = result.translation;
        entry.commitText = result.translation;
        entry.reading = TrimWhitespace(result.source);
        entry.source = llm::CandidateSource::Llm;
        list.entries.emplace_back(std::move(entry));
        ApplyTranslationCandidates(key, std::move(list));
    }
    CandidateUI* m_candidateUI = nullptr;
    ITfContext* m_candidateContext = nullptr;
    std::wstring m_candidatePreview;
    std::wstring m_candidateReading;
    std::wstring m_candidateSourceText;
    std::wstring m_candidateSourceKey;
    bool m_translationPendingShown = false;
    bool m_layer2PendingActive = false;
    bool m_translationPendingActive = false;
    bool m_pendingLayer2AutoCommit = false;
    bool m_layer2MergedCarryOpen = false;
    std::wstring m_layer2LockedPrefix;
    struct SegmentRuntime {
        size_t start = 0;
        size_t length = 0;
        std::wstring surface;
        std::vector<std::wstring> candidates;
        size_t index = 0;
    };
    std::vector<ime::conversion::SegmentInfo> m_layer1SourceSegments;
    std::vector<SegmentRuntime> m_layer1SegmentRuntime;
    std::vector<std::pair<size_t, size_t>> m_segmentDisplayRanges;
    std::wstring m_segmentBaseReading;
    size_t m_activeSegmentIndex = 0;
    bool m_segmentMode = false;
    size_t m_activeSegmentDisplayStart = 0;
    size_t m_activeSegmentDisplayLength = 0;
    bool m_hasActiveSegmentDisplayRange = false;
    POINT m_lastCaretScreenPoint{ 0, 0 };
    bool m_hasLastCaretScreenPoint = false;
    size_t m_compositionCaretOffset = std::wstring::npos;
    size_t EffectiveCompositionCaretOffset(size_t textLength) const {
        if (m_compositionCaretOffset == std::wstring::npos) return textLength;
        return (std::min)(m_compositionCaretOffset, textLength);
    }
    void ResetCompositionCaretToEnd() {
        m_compositionCaretOffset = std::wstring::npos;
    }
    void ApplyCompositionCaretSelection(TfEditCookie ec, ITfContext* ctx, ITfRange* range, size_t textLength) {
        if (!ctx || !range) return;
        ComPtr<ITfRange> caret;
        if (FAILED(range->Clone(&caret.p)) || !caret.p) return;
        caret->Collapse(ec, TF_ANCHOR_START);
        const size_t offset = EffectiveCompositionCaretOffset(textLength);
        if (offset > 0) {
            LONG shifted = 0;
            caret->ShiftEnd(ec, static_cast<LONG>(offset), &shifted, nullptr);
            caret->Collapse(ec, TF_ANCHOR_END);
        }
        TF_SELECTION sel{};
        sel.range = caret.p;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        ctx->SetSelection(ec, 1, &sel);

        // Cache the caret anchor from TSF view coordinates so UI placement
        // can stay stable even in apps where GUIThreadInfo does not expose
        // a reliable caret rectangle.
        ComPtr<ITfContextView> view;
        if (SUCCEEDED(ctx->GetActiveView(&view.p)) && view.p) {
            RECT rc{};
            BOOL clipped = FALSE;
            if (SUCCEEDED(view->GetTextExt(ec, caret.p, &rc, &clipped))) {
                m_lastCaretScreenPoint.x = rc.left;
                m_lastCaretScreenPoint.y = rc.bottom + 2;
                m_hasLastCaretScreenPoint = true;
            }
        }
    }
    bool MoveCompositionCaret(ITfContext* context, int delta) {
        if (!m_composition || delta == 0) return false;
        std::wstring text = m_candidatePreview;
        if (text.empty()) text = PreeditText();
        const size_t textLength = text.size();
        const size_t current = EffectiveCompositionCaretOffset(textLength);
        size_t next = current;
        if (delta < 0) {
            if (next > 0) --next;
        } else if (next < textLength) {
            ++next;
        }
        if (next == current) return true;
        m_compositionCaretOffset = next;
        UpdateCompositionText(context, text);
        return true;
    }
    void CloseCandidateUI(bool preserveLayer2Carry = false) {
        const bool keepCarry = preserveLayer2Carry && HasLayer2LockedCarry();
        const std::wstring lockedPrefixSnapshot = keepCarry ? m_layer2LockedPrefix : std::wstring();
        CancelActiveTranslationRequest();
        CancelActiveLayer2Request();
        if (m_candidateUI) { m_candidateUI->End(); m_candidateUI->Release(); m_candidateUI = nullptr; }
        if (m_candidateContext) { m_candidateContext->Release(); m_candidateContext = nullptr; }
        m_candidatePreview.clear();
        m_candidateReading.clear();
        ClearCandidateSourceText();
        m_layer2SourceText.clear();
        m_layer2SourceKey.clear();
        m_layer1DisplayCache.clear();
        m_layer1Selection = 0;
        SetLayer1Merged(false);
        m_layer1SpaceTriggered = false;
        m_activeCandidateTab = CandidateTab::Layer1;
        m_layer2PendingActive = false;
        m_translationPendingActive = false;
        SetPendingLayer2AutoCommit(false);
        ClearLayer2LockedCarry();
        m_layer1SourceSegments.clear();
        m_layer1SegmentRuntime.clear();
        m_segmentDisplayRanges.clear();
        m_segmentBaseReading.clear();
        m_activeSegmentIndex = 0;
        m_segmentMode = false;
        m_activeSegmentDisplayStart = 0;
        m_activeSegmentDisplayLength = 0;
        m_hasActiveSegmentDisplayRange = false;
        if (keepCarry) {
            m_layer2MergedCarryOpen = true;
            m_layer2LockedPrefix = lockedPrefixSnapshot;
        }
        UpdatePendingVisualFlag();
    }
    std::wstring ComposeSegmentReading() {
        m_hasActiveSegmentDisplayRange = false;
        m_activeSegmentDisplayStart = 0;
        m_activeSegmentDisplayLength = 0;
        if (!m_segmentMode || m_layer1SegmentRuntime.empty() || m_segmentBaseReading.empty()) {
            m_segmentDisplayRanges.clear();
            return m_candidateUI ? m_candidateUI->SelectedString() : m_candidateReading;
        }
        std::wstring out;
        size_t cursor = 0;
        m_segmentDisplayRanges.clear();
        m_segmentDisplayRanges.reserve(m_layer1SegmentRuntime.size());
        for (size_t i = 0; i < m_layer1SegmentRuntime.size(); ++i) {
            const auto& seg = m_layer1SegmentRuntime[i];
            if (seg.start > m_segmentBaseReading.size()) break;
            if (seg.start > cursor) {
                out.append(m_segmentBaseReading.substr(cursor, seg.start - cursor));
            }
            const size_t segDisplayStart = out.size();
            if (!seg.candidates.empty()) {
                size_t idx = seg.index < seg.candidates.size() ? seg.index : 0;
                out.append(seg.candidates[idx]);
            } else {
                out.append(seg.surface);
            }
            m_segmentDisplayRanges.push_back(
                std::make_pair(segDisplayStart, out.size() - segDisplayStart));
            if (i == m_activeSegmentIndex) {
                m_hasActiveSegmentDisplayRange = true;
                m_activeSegmentDisplayStart = segDisplayStart;
                m_activeSegmentDisplayLength = out.size() - segDisplayStart;
            }
            cursor = seg.start + seg.length;
        }
        if (cursor < m_segmentBaseReading.size()) {
            out.append(m_segmentBaseReading.substr(cursor));
        }
        return out.empty() ? m_segmentBaseReading : out;
    }
    bool SetActiveSegment(int delta) {
        if (!m_segmentMode || m_layer1SegmentRuntime.empty() || !m_candidateUI) return false;
        const int count = static_cast<int>(m_layer1SegmentRuntime.size());
        int idx = static_cast<int>(m_activeSegmentIndex);
        if (delta != 0) {
            idx = (idx + delta + count) % count;
        }
        m_activeSegmentIndex = static_cast<size_t>(idx);
        auto& seg = m_layer1SegmentRuntime[m_activeSegmentIndex];
        m_layer1DisplayCache = seg.candidates.empty() ? std::vector<std::wstring>{ seg.surface } : seg.candidates;
        if (seg.index >= m_layer1DisplayCache.size()) seg.index = 0;
        m_layer1Selection = seg.index;
        m_candidateUI->SetCandidates(m_layer1DisplayCache, static_cast<int>(m_layer1Selection), false);
        m_candidatePreview.clear();
        PreviewCandidateString(ComposeSegmentReading());
        return true;
    }
    bool InitializeSegmentMode() {
        m_segmentMode = false;
        m_layer1SegmentRuntime.clear();
        m_segmentDisplayRanges.clear();
        m_segmentBaseReading.clear();
        m_activeSegmentIndex = 0;
        m_activeSegmentDisplayStart = 0;
        m_activeSegmentDisplayLength = 0;
        m_hasActiveSegmentDisplayRange = false;
        if (!m_candidateUI) {
            return false;
        }

        const std::wstring baseReading = ActiveDraftText();
        if (baseReading.empty() || m_layer1SourceSegments.empty()) {
            return false;
        }

        std::vector<ime::conversion::SegmentInfo> sorted = m_layer1SourceSegments;
        std::sort(sorted.begin(), sorted.end(),
            [](const ime::conversion::SegmentInfo& a,
               const ime::conversion::SegmentInfo& b) {
                if (a.start != b.start) return a.start < b.start;
                return a.length < b.length;
            });

        size_t segmentFloor = 0;
        if (HasLayer2LockedCarry() &&
            baseReading.size() >= m_layer2LockedPrefix.size() &&
            baseReading.compare(0, m_layer2LockedPrefix.size(), m_layer2LockedPrefix) == 0) {
            // Keep merged/locked prefix out of segment-edit scope.
            segmentFloor = m_layer2LockedPrefix.size();
        }

        std::vector<SegmentRuntime> runtime;
        runtime.reserve(sorted.size() + 1);
        size_t cursor = segmentFloor;
        auto appendSegment = [&](size_t start, size_t length, const std::wstring& surfaceHint) {
            if (length == 0 || start >= baseReading.size()) {
                return;
            }
            SegmentRuntime seg;
            seg.start = start;
            seg.length = (std::min)(length, baseReading.size() - start);
            seg.surface = surfaceHint.empty()
                ? baseReading.substr(seg.start, seg.length)
                : surfaceHint;
            runtime.push_back(std::move(seg));
        };

        for (const auto& src : sorted) {
            if (src.length == 0 || src.start >= baseReading.size()) {
                continue;
            }
            const size_t rawStart = (std::max)(segmentFloor, src.start);
            const size_t rawEnd = (std::min)(baseReading.size(), src.start + src.length);
            if (rawEnd <= rawStart) {
                continue;
            }
            const size_t segStart = (std::max)(cursor, rawStart);
            if (segStart > cursor) {
                appendSegment(cursor, segStart - cursor, baseReading.substr(cursor, segStart - cursor));
            }
            if (segStart >= baseReading.size()) {
                break;
            }
            const size_t segEnd = rawEnd;
            if (segEnd <= segStart) {
                continue;
            }
            appendSegment(segStart, segEnd - segStart, src.surface);
            cursor = segEnd;
        }
        if (cursor < baseReading.size()) {
            appendSegment(cursor, baseReading.size() - cursor, baseReading.substr(cursor));
        }
        if (runtime.size() <= 1) {
            return false;
        }

        auto buildSegmentCandidates = [this](const std::wstring& key, const std::wstring& surface) {
            std::vector<std::wstring> raw = CollectLayer1BaseCandidates(key, false, false);
            std::vector<std::wstring> ordered;
            ordered.reserve(raw.size() + 2);
            auto addUnique = [&ordered](const std::wstring& value) {
                if (value.empty()) return;
                if (std::find(ordered.begin(), ordered.end(), value) != ordered.end()) return;
                ordered.push_back(value);
            };
            addUnique(surface);
            addUnique(key);
            for (const auto& cand : raw) {
                addUnique(cand);
            }
            return ordered;
        };

        for (auto& seg : runtime) {
            if (seg.start >= baseReading.size()) {
                continue;
            }
            const std::wstring key = baseReading.substr(seg.start, seg.length);
            if (seg.surface.empty()) {
                seg.surface = key;
            }
            seg.candidates = buildSegmentCandidates(key, seg.surface);
            if (seg.candidates.empty()) {
                seg.candidates.push_back(seg.surface.empty() ? key : seg.surface);
            }
            auto it = std::find(seg.candidates.begin(), seg.candidates.end(), seg.surface);
            seg.index = (it == seg.candidates.end())
                ? 0
                : static_cast<size_t>(it - seg.candidates.begin());
        }

        m_segmentMode = true;
        m_segmentBaseReading = baseReading;
        m_layer1SegmentRuntime = std::move(runtime);
        m_activeSegmentIndex = 0;
        auto& active = m_layer1SegmentRuntime[m_activeSegmentIndex];
        m_layer1DisplayCache = active.candidates;
        if (active.index >= m_layer1DisplayCache.size()) {
            active.index = 0;
        }
        m_layer1Selection = active.index;
        m_candidateUI->SetCandidates(
            m_layer1DisplayCache,
            static_cast<int>(m_layer1Selection),
            false);
        m_candidatePreview.clear();
        PreviewCandidateString(ComposeSegmentReading());
        return true;
    }

    void PreviewCandidateString(const std::wstring& candidate) {
        if (!m_composition) return;
        std::wstring preview = candidate;
        if (IsLikelyPartialLayer1Candidate(preview)) {
            preview = ResolveLayer1SourceText(preview);
        }
        if (preview.empty()) preview = DraftText();
        if (preview.empty()) preview = PreeditText();
        if (preview.empty() || preview == m_candidatePreview) return;
        m_candidatePreview = preview;
        UpdateCompositionText(m_candidateContext, preview);
    }

    void OpenCandidateUI(ITfContext* context, const std::wstring& reading) {
        if (!m_ptm || !context) return;
        ResetCompositionCaretToEnd();
        const bool carryForward = HasLayer2LockedCarry();
        std::wstring lockedPrefixSnapshot = m_layer2LockedPrefix;
        CloseCandidateUI();
        ComPtr<ITfUIElementMgr> mgr; if (FAILED(m_ptm->QueryInterface(IID_PPV_ARGS(&mgr)))) return;
        std::vector<std::wstring> cands;
        const std::wstring trimmedReading = TrimWhitespace(reading);
        if (carryForward && !lockedPrefixSnapshot.empty() && trimmedReading.size() >= lockedPrefixSnapshot.size() &&
            trimmedReading.compare(0, lockedPrefixSnapshot.size(), lockedPrefixSnapshot) == 0) {
            std::wstring tail = trimmedReading.substr(lockedPrefixSnapshot.size());
            cands = CollectLayer1CandidatesWithLockedPrefix(
                lockedPrefixSnapshot, tail, false, true);
            if (!cands.empty()) {
                m_layer2MergedCarryOpen = true;
                m_layer2LockedPrefix = lockedPrefixSnapshot;
            } else {
                ClearLayer2LockedCarry();
            }
        } else {
            ClearLayer2LockedCarry();
        }
        if (cands.empty()) {
            cands = CollectLayer1BaseCandidates(trimmedReading.empty() ? reading : trimmedReading, false);
            if (cands.empty()) return;
        }
        m_candidateContext = context;
        m_candidateContext->AddRef();
        m_candidateReading = trimmedReading;
        ComPtr<ITfDocumentMgr> docMgr;
        context->GetDocumentMgr(&docMgr.p); // docMgr.p stays null on failure
        auto* candidate = new (std::nothrow) CandidateUI(this, mgr.p, docMgr.p, cands);
        if (!candidate) {
            m_candidateContext->Release();
            m_candidateContext = nullptr;
            return;
        }
        if (!candidate->Begin()) {
            candidate->Release();
            m_candidateContext->Release();
            m_candidateContext = nullptr;
            return;
        }
        m_candidateUI = candidate;
        SyncCandidateSourceFromActiveDraft();
        m_layer1DisplayCache = std::move(cands);
        m_layer1Selection = 0;
        SetLayer1Merged(false);
        m_activeCandidateTab = CandidateTab::Layer1;
        ShowLayer1Candidates(true);
        std::wstring initialCandidate = m_candidateUI->SelectedString();
        if (initialCandidate.empty() && !m_layer1DisplayCache.empty()) {
            initialCandidate = m_layer1DisplayCache.front();
        }
        PreviewCandidateString(initialCandidate);
        InitializeSegmentMode();
    }
    void CommitCandidateSelection(ITfContext* context) {
        if (!m_candidateUI) return;
        const std::wstring commit = m_segmentMode ? ComposeSegmentReading() : m_candidateUI->SelectedString();
        const std::wstring source = ActiveDraftText();
        LearnCandidateSelection(source, commit);
        CloseCandidateUI();
        CommitComposition(context, commit);
    }
    // callbacks from CandidateUI
    void OnCandidateSelectionChanged(const std::wstring& candidate) {
        if (!m_candidateUI) return;
        int sel = m_candidateUI->SelectionIndex();
        if (sel < 0) sel = 0;
        switch (m_activeCandidateTab) {
        case CandidateTab::Layer1:
            m_layer1Selection = static_cast<size_t>(sel);
            if (m_segmentMode && !m_layer1SegmentRuntime.empty() &&
                m_activeSegmentIndex < m_layer1SegmentRuntime.size()) {
                auto& seg = m_layer1SegmentRuntime[m_activeSegmentIndex];
                if (!m_layer1DisplayCache.empty()) {
                    const size_t idx = static_cast<size_t>(sel) < m_layer1DisplayCache.size()
                        ? static_cast<size_t>(sel) : 0;
                    seg.index = idx;
                }
                PreviewCandidateString(ComposeSegmentReading());
            } else {
                PreviewCandidateString(candidate);
            }
            break;
        case CandidateTab::Layer2: {
            if (!m_layer2SourceKey.empty()) {
                auto it = m_layer2Cache.find(m_layer2SourceKey);
                if (it != m_layer2Cache.end() && !it->second.candidates.empty()) {
                    size_t idx = sel >= static_cast<int>(it->second.candidates.size())
                        ? it->second.candidates.size() - 1
                        : static_cast<size_t>(sel);
                    it->second.index = idx;
                }
            }
            PreviewCandidateString(candidate);
            break;
        }
        case CandidateTab::Translation: {
            if (!m_candidateSourceKey.empty()) {
                auto it = m_translationCache.find(m_candidateSourceKey);
                if (it != m_translationCache.end() && !it->second.candidates.empty()) {
                    size_t idx = sel >= static_cast<int>(it->second.candidates.size())
                        ? it->second.candidates.size() - 1
                        : static_cast<size_t>(sel);
                    it->second.index = idx;
                }
            }
            PreviewCandidateString(candidate);
            break;
        }
        }
    }

    void OnCandidateFinalize(int sel) {
        if (!m_candidateUI) return;
        if (sel >= 0) m_candidateUI->SetSelection((UINT)sel);
        ITfContext* ctx = m_candidateContext;
        if (ctx) ctx->AddRef();
        const bool shiftPressed = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        HandleTabEnter(ctx, shiftPressed);
        if (ctx) ctx->Release();
    }
    void OnCandidateAbort() {
        ITfContext* ctx = m_candidateContext;
        if (ctx) ctx->AddRef();
        CloseCandidateUI();
        if (ctx) {
            if (m_composition) UpdateCompositionText(ctx, PreeditText());
            ctx->Release();
        } else if (m_composition) {
            UpdateCompositionText(nullptr, PreeditText());
        }
        UpdateOverlayFromState();
    }
    // --- Composition helpers -------------------------------------------------
    struct StartCompositionEditSession : public ITfEditSession {
        LONG _ref{1}; ITfContext* _ctx{ nullptr }; TfClientId _tid{ TF_CLIENTID_NULL }; TextService* _owner{ nullptr };
        StartCompositionEditSession(ITfContext* c, TfClientId t, TextService* o) : _ctx(c), _tid(t), _owner(o) { if (_ctx) _ctx->AddRef(); }
        ~StartCompositionEditSession() { if (_ctx) _ctx->Release(); }
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override { if (!ppv) return E_POINTER; *ppv = nullptr; if (riid == IID_IUnknown || riid == __uuidof(ITfEditSession)) { *ppv = static_cast<ITfEditSession*>(this); AddRef(); return S_OK; } return E_NOINTERFACE; }
        STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&_ref); }
        STDMETHODIMP_(ULONG) Release() override { ULONG r = (ULONG)InterlockedDecrement(&_ref); if (!r) delete this; return r; }
        STDMETHODIMP DoEditSession(TfEditCookie ec) override {
            if (!_ctx || !_owner) return S_OK;
            DebugLog(L"StartComposition DoEditSession");
            TF_SELECTION sel{}; ULONG fetched=0; if (FAILED(_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched!=1 || !sel.range) return S_OK;
            ComPtr<ITfContextComposition> ccc; if (FAILED(_ctx->QueryInterface(IID_PPV_ARGS(&ccc)))) { if (sel.range) sel.range->Release(); return S_OK; }
            if (_owner->m_composition) { if (sel.range) sel.range->Release(); return S_OK; }
            ITfComposition* comp=nullptr; HRESULT hr = ccc->StartComposition(ec, sel.range, static_cast<ITfCompositionSink*>(_owner), &comp);
            if (SUCCEEDED(hr) && comp) {
                _owner->m_composition = comp;
                // Initialize composition text with current preedit (kana + pending romaji)
                std::wstring preedit = _owner->PreeditText();
                if (!preedit.empty()) {
                    ComPtr<ITfRange> rng; _owner->m_composition->GetRange(&rng.p);
                    if (rng.p) {
                        rng->SetText(ec, 0, preedit.c_str(), (LONG)preedit.size());
                        // Apply display attribute to the whole composition
                        _owner->ApplyDisplayAttributes(ec, _ctx, rng.p, preedit);
                        _owner->ApplyCompositionCaretSelection(ec, _ctx, rng.p, preedit.size());
                    }
                }
            }
            if (sel.range) sel.range->Release();
            return S_OK;
        }
    };
    struct UpdateCompositionEditSession : public ITfEditSession {
        LONG _ref{1}; ITfContext* _ctx{ nullptr }; TfClientId _tid{ TF_CLIENTID_NULL }; TextService* _owner{ nullptr }; std::wstring _text;
        UpdateCompositionEditSession(ITfContext* c, TfClientId t, TextService* o, const std::wstring& s) : _ctx(c), _tid(t), _owner(o), _text(s) { if (_ctx) _ctx->AddRef(); }
        ~UpdateCompositionEditSession() { if (_ctx) _ctx->Release(); }
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override { if (!ppv) return E_POINTER; *ppv = nullptr; if (riid == IID_IUnknown || riid == __uuidof(ITfEditSession)) { *ppv = static_cast<ITfEditSession*>(this); AddRef(); return S_OK; } return E_NOINTERFACE; }
        STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&_ref); }
        STDMETHODIMP_(ULONG) Release() override { ULONG r = (ULONG)InterlockedDecrement(&_ref); if (!r) delete this; return r; }
        STDMETHODIMP DoEditSession(TfEditCookie ec) override {
            if (!_ctx || !_owner) return S_OK;
            if (!_owner->m_composition) { DebugLog(L"UpdateComposition DoEditSession without composition"); return S_OK; }
            DebugLog(L"UpdateComposition DoEditSession");
            ComPtr<ITfRange> rng; _owner->m_composition->GetRange(&rng.p); if (!rng.p) return S_OK;
            rng->SetText(ec, 0, _text.c_str(), (LONG)_text.size());
            _owner->ApplyDisplayAttributes(ec, _ctx, rng.p, _text);
            _owner->ApplyCompositionCaretSelection(ec, _ctx, rng.p, _text.size());
            return S_OK;
        }
    };
    struct EndCompositionEditSession : public ITfEditSession {
        LONG _ref{1}; ITfContext* _ctx{ nullptr }; TfClientId _tid{ TF_CLIENTID_NULL }; TextService* _owner{ nullptr }; std::wstring _commit;
        EndCompositionEditSession(ITfContext* c, TfClientId t, TextService* o, const std::wstring& s) : _ctx(c), _tid(t), _owner(o), _commit(s) { if (_ctx) _ctx->AddRef(); }
        ~EndCompositionEditSession() { if (_ctx) _ctx->Release(); }
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override { if (!ppv) return E_POINTER; *ppv = nullptr; if (riid == IID_IUnknown || riid == __uuidof(ITfEditSession)) { *ppv = static_cast<ITfEditSession*>(this); AddRef(); return S_OK; } return E_NOINTERFACE; }
        STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&_ref); }
        STDMETHODIMP_(ULONG) Release() override { ULONG r = (ULONG)InterlockedDecrement(&_ref); if (!r) delete this; return r; }
        STDMETHODIMP DoEditSession(TfEditCookie ec) override {
            if (!_ctx || !_owner) return S_OK;
            DebugLog(L"EndComposition DoEditSession");
            if (_owner->m_composition) {
                ComPtr<ITfRange> rng; _owner->m_composition->GetRange(&rng.p);
                if (rng.p) {
                    if (!_commit.empty()) {
                        // Replace comp text with commit text
                        rng->SetText(ec, 0, _commit.c_str(), (LONG)_commit.size());
                        // Place caret at the end of committed text
                        ComPtr<ITfRange> end; if (SUCCEEDED(rng->Clone(&end.p)) && end.p) {
                            end->Collapse(ec, TF_ANCHOR_END);
                            TF_SELECTION sel{}; sel.range = end.p; sel.style.ase = TF_AE_NONE; sel.style.fInterimChar = FALSE; _ctx->SetSelection(ec, 1, &sel);
                        }
                    } else {
                        // Cancel path: clear composition text before ending composition.
                        rng->SetText(ec, 0, L"", 0);
                    }
                }
                _owner->m_composition->EndComposition(ec);
                _owner->m_composition->Release();
                _owner->m_composition = nullptr;
            } else if (!_commit.empty()) {
                // Fallback: insert directly
                ComPtr<ITfInsertAtSelection> insert; if (SUCCEEDED(_ctx->QueryInterface(IID_PPV_ARGS(&insert)))) {
                    ComPtr<ITfRange> rng;
                    insert->InsertTextAtSelection(ec, 0, _commit.c_str(), (ULONG)_commit.size(), &rng.p);
                    if (rng.p) {
                        rng->Collapse(ec, TF_ANCHOR_END);
                        TF_SELECTION sel{}; sel.range = rng.p; sel.style.ase = TF_AE_NONE; sel.style.fInterimChar = FALSE; _ctx->SetSelection(ec, 1, &sel);
                    }
                }
            }
            return S_OK;
        }
    };
    void EnsureComposition(ITfContext* context) {
        if (m_composition) return;
        ITfContext* ctx = context;
        // Try to get focused top context if none passed
        ComPtr<ITfContext> focused;
        if (!ctx && m_ptm) {
            ComPtr<ITfDocumentMgr> dm; if (SUCCEEDED(m_ptm->GetFocus(&dm.p)) && dm.p) {
                dm->GetTop(&focused.p);
                ctx = focused.p;
            }
        }
        if (!ctx) return;
        auto* edit = new (std::nothrow) StartCompositionEditSession(ctx, m_tid, this);
        if (!edit) return;
        HRESULT hrSession = E_FAIL;
        HRESULT hr = ctx->RequestEditSession(m_tid, edit, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (FAILED(hr)) {
            DebugLog(L"EnsureComposition SYNC failed; fallback ASYNC", hr);
            HRESULT hr2Session = E_FAIL;
            HRESULT hr2 = ctx->RequestEditSession(m_tid, edit, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr2Session);
            (void)hr2; (void)hr2Session;
        }
        edit->Release();
    }
    void UpdateCompositionText(ITfContext* context, const std::wstring& text) {
        if (!m_composition) { DebugLog(L"UpdateCompositionText without composition; ignoring"); return; }
        ITfContext* ctx = context;
        ComPtr<ITfContext> focused;
        if (!ctx && m_ptm) {
            ComPtr<ITfDocumentMgr> dm; if (SUCCEEDED(m_ptm->GetFocus(&dm.p)) && dm.p) { dm->GetTop(&focused.p); ctx = focused.p; }
        }
        if (!ctx) return;
        auto* edit = new (std::nothrow) UpdateCompositionEditSession(ctx, m_tid, this, text);
        if (!edit) return;
        HRESULT hrSession = E_FAIL;
        HRESULT hr = ctx->RequestEditSession(m_tid, edit, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (FAILED(hr)) {
            DebugLog(L"UpdateComposition SYNC failed; fallback ASYNC", hr);
            HRESULT hr2Session = E_FAIL;
            HRESULT hr2 = ctx->RequestEditSession(m_tid, edit, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr2Session);
            (void)hr2; (void)hr2Session;
        }
        edit->Release();
    }
    void CommitComposition(ITfContext* context, const std::wstring& commit) {
        ITfContext* ctx = context;
        ComPtr<ITfContext> focused;
        if (!ctx && m_ptm) {
            ComPtr<ITfDocumentMgr> dm; if (SUCCEEDED(m_ptm->GetFocus(&dm.p)) && dm.p) { dm->GetTop(&focused.p); ctx = focused.p; }
        }
        if (!ctx) {
            // No TSF context (e.g., game chat). Commit via keystroke/clipboard fallback.
            if (!commit.empty()) {
                InjectUnicodeKeystrokes(commit);
                // Best-effort paste as a secondary fallback
                if (!PasteWithClipboard(commit)) {
                    // Nothing else to do
                }
            }
            DraftText().clear(); m_romajiBuffer.clear(); CloseCandidateUI(); HideOverlay();
            ResetForceHalfWidthInput();
            return;
        }
        auto* edit = new (std::nothrow) EndCompositionEditSession(ctx, m_tid, this, commit);
        if (!edit) return;
        HRESULT hrSession = E_FAIL;
        HRESULT hr = ctx->RequestEditSession(m_tid, edit, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (FAILED(hr)) {
            DebugLog(L"CommitComposition SYNC failed; fallback ASYNC", hr);
            HRESULT hr2Session = E_FAIL;
            HRESULT hr2 = ctx->RequestEditSession(m_tid, edit, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr2Session);
            (void)hr2; (void)hr2Session;
        }
        edit->Release();
        DraftText().clear(); m_romajiBuffer.clear(); CloseCandidateUI(); HideOverlay();
        ResetForceHalfWidthInput();
        ResetCompositionCaretToEnd();
    }
    void CancelComposition(ITfContext* context) {
        if (m_composition) { CommitComposition(context, L""); }
        DraftText().clear(); m_romajiBuffer.clear(); CloseCandidateUI(); HideOverlay();
        ResetForceHalfWidthInput();
        ResetCompositionCaretToEnd();
    }
    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie, ITfComposition*) override { if (m_composition) { m_composition->Release(); m_composition = nullptr; } DraftText().clear(); m_romajiBuffer.clear(); ResetForceHalfWidthInput(); CloseCandidateUI(); HideOverlay(); ResetCompositionCaretToEnd(); return S_OK; }
    TfGuidAtom CurrentDisplayAttributeAtom() const {
        if ((m_activeCandidateTab == CandidateTab::Translation) || m_translationPendingActive) {
            if (m_gaDisplayAttrTranslation) return m_gaDisplayAttrTranslation;
        }
        if ((m_activeCandidateTab == CandidateTab::Layer2) || m_layer2PendingActive || IsLayer1MergedFromLayer2()) {
            if (m_gaDisplayAttrLayer2) return m_gaDisplayAttrLayer2;
        }
        return m_gaDisplayAttrPreedit;
    }
    bool SetDisplayAtomOnRange(TfEditCookie ec, ITfContext* ctx, ITfRange* range, TfGuidAtom atom) {
        if (!ctx || !range || atom == 0) return false;
        ComPtr<ITfProperty> prop;
        if (FAILED(ctx->GetProperty(GUID_PROP_ATTRIBUTE, &prop.p)) || !prop.p) return false;
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_I4;
        var.lVal = atom;
        const HRESULT hr = prop->SetValue(ec, range, &var);
        return SUCCEEDED(hr);
    }
    // Apply display attributes to composition:
    // - default: current tab color for whole composition
    // - segment mode: all segments use red dotted underline, active segment
    //   uses solid red underline
    void ApplyDisplayAttributes(TfEditCookie ec, ITfContext* ctx, ITfRange* range, const std::wstring& text) {
        if (!ctx || !range) return;
        const TfGuidAtom baseAtom = CurrentDisplayAttributeAtom();
        if (baseAtom == 0) return;
        SetDisplayAtomOnRange(ec, ctx, range, baseAtom);
        // In carry-forward mode, keep the locked Layer2 prefix visually
        // distinct (orange) while new tail input stays in Layer1 (blue).
        if (HasLayer2LockedCarry() &&
            m_activeCandidateTab == CandidateTab::Layer1 &&
            m_gaDisplayAttrLayer2) {
            const size_t prefixLen = m_layer2LockedPrefix.size();
            if (prefixLen <= text.size() &&
                text.compare(0, prefixLen, m_layer2LockedPrefix) == 0) {
                ComPtr<ITfRange> lockedPrefixRange;
                if (SUCCEEDED(range->Clone(&lockedPrefixRange.p)) && lockedPrefixRange.p) {
                    LONG moved = 0;
                    const LONG shrinkTail = static_cast<LONG>(text.size() - prefixLen);
                    if (shrinkTail > 0) {
                        if (SUCCEEDED(lockedPrefixRange->ShiftEnd(ec, -shrinkTail, &moved, nullptr))) {
                            SetDisplayAtomOnRange(ec, ctx, lockedPrefixRange.p, m_gaDisplayAttrLayer2);
                        }
                    } else {
                        SetDisplayAtomOnRange(ec, ctx, lockedPrefixRange.p, m_gaDisplayAttrLayer2);
                    }
                }
            }
        }
        if (!(m_segmentMode && m_activeCandidateTab == CandidateTab::Layer1)) {
            return;
        }
        if (text.empty()) return;

        auto applyAtomOnSpan = [&](size_t start, size_t length, TfGuidAtom atom) {
            if (atom == 0 || length == 0 || start >= text.size()) return;
            const size_t end = (std::min)(text.size(), start + length);
            if (end <= start) return;
            ComPtr<ITfRange> span;
            if (FAILED(range->Clone(&span.p)) || !span.p) return;
            LONG moved = 0;
            const LONG startShift = static_cast<LONG>(start);
            if (startShift > 0) {
                if (FAILED(span->ShiftStart(ec, startShift, &moved, nullptr)) || moved != startShift) {
                    return;
                }
            }
            const LONG shrinkTail = static_cast<LONG>(text.size() - end);
            if (shrinkTail > 0) {
                if (FAILED(span->ShiftEnd(ec, -shrinkTail, &moved, nullptr)) || moved != -shrinkTail) {
                    return;
                }
            }
            SetDisplayAtomOnRange(ec, ctx, span.p, atom);
        };

        if (m_gaDisplayAttrSegment && !m_segmentDisplayRanges.empty()) {
            for (const auto& item : m_segmentDisplayRanges) {
                applyAtomOnSpan(item.first, item.second, m_gaDisplayAttrSegment);
            }
        }
        if (m_hasActiveSegmentDisplayRange) {
            applyAtomOnSpan(
                m_activeSegmentDisplayStart,
                m_activeSegmentDisplayLength,
                m_gaDisplayAttrSegmentActive);
        }
    }
    // Small built-in dictionary and utilities
    static std::wstring HiraganaToKatakana(const std::wstring& s) {
        std::wstring r; r.reserve(s.size());
        for (wchar_t ch : s) {
            if (ch >= 0x3041 && ch <= 0x3096) r.push_back((wchar_t)(ch + 0x60)); else r.push_back(ch);
        }
        return r;
    }
    static std::vector<std::wstring> QueryImmConversionCandidates(const std::wstring& reading) {
        std::vector<std::wstring> out;
        if (reading.empty()) return out;
        HKL hkl = GetKeyboardLayout(0);
        bool ownsHkl = false;
        LANGID lang = LOWORD(reinterpret_cast<ULONG_PTR>(hkl));
        if (!hkl || PRIMARYLANGID(lang) != LANG_JAPANESE) {
            hkl = LoadKeyboardLayoutW(L"00000411", KLF_NOTELLSHELL | KLF_SUBSTITUTE_OK);
            ownsHkl = (hkl != nullptr);
        }
        if (!hkl) return out;
        HIMC himc = ImmCreateContext();
        if (!himc) { if (ownsHkl) UnloadKeyboardLayout(hkl); return out; }
        DWORD need = ImmGetConversionListW(hkl, himc, reading.c_str(), nullptr, 0, GCL_CONVERSION);
        if (need) {
            std::vector<BYTE> buffer(need);
            auto* list = reinterpret_cast<LPCANDIDATELIST>(buffer.data());
            DWORD ret = ImmGetConversionListW(hkl, himc, reading.c_str(), list, need, GCL_CONVERSION);
            if (ret && ret <= need && list && list->dwCount) {
                for (DWORD i = 0; i < list->dwCount; ++i) {
                    DWORD offset = list->dwOffset[i];
                    if (offset >= need) continue;
                    const wchar_t* cand = reinterpret_cast<const wchar_t*>(buffer.data() + offset);
                    if (!cand || !*cand) continue;
                    if (std::find(out.begin(), out.end(), cand) == out.end()) {
                        out.emplace_back(cand);
                    }
                }
            }
        }
        ImmDestroyContext(himc);
        if (ownsHkl) UnloadKeyboardLayout(hkl);
        return out;
    }
    static std::vector<std::wstring> BuildCandidates(const std::wstring& reading) {
        std::vector<std::wstring> out = QueryImmConversionCandidates(reading);
        if (!reading.empty() && std::find(out.begin(), out.end(), reading) == out.end()) out.push_back(reading);
        std::wstring kata = HiraganaToKatakana(reading);
        if (!kata.empty() && std::find(out.begin(), out.end(), kata) == out.end()) out.push_back(kata);
        return out;
    }
    std::vector<std::wstring> CollectLayer1BaseCandidates(const std::wstring& reading, bool allowAsync = false, bool captureSegments = true) {
        std::vector<std::wstring> cands;
        const std::wstring trimmed = TrimWhitespace(reading);
        if (captureSegments) {
            m_layer1SourceSegments.clear();
        }
        if (m_conversionProvider) {
            ime::conversion::LayerRequestContext req;
            req.reading = trimmed;
            req.layer = 1;
            req.allowAsync = allowAsync;
            auto list = m_conversionProvider->FetchLayer1(req);
            if (captureSegments) {
                m_layer1SourceSegments = list.segments;
            }
            if (!list.error.empty()) {
                DebugLog(list.error.c_str());
            }
            for (const auto& entry : list.entries) {
                std::wstring text = entry.displayText.empty() ? entry.commitText : entry.displayText;
                if (!text.empty()) {
                    if (std::find(cands.begin(), cands.end(), text) == cands.end()) {
                        cands.push_back(std::move(text));
                    }
                }
            }
        }
        if (cands.empty()) {
            cands = BuildCandidates(trimmed.empty() ? reading : trimmed);
        } else if (!trimmed.empty() && std::find(cands.begin(), cands.end(), trimmed) == cands.end()) {
            cands.push_back(trimmed);
        }
        ApplyLearnedCandidateOrdering(trimmed.empty() ? reading : trimmed, &cands);
        return cands;
    }
    static void AppendUniqueCandidate(std::vector<std::wstring>& list, std::wstring value) {
        if (value.empty()) return;
        if (std::find(list.begin(), list.end(), value) != list.end()) return;
        list.push_back(std::move(value));
    }
    std::vector<std::wstring> CollectLayer1CandidatesWithLockedPrefix(
        const std::wstring& prefix,
        const std::wstring& tail,
        bool allowAsync = false,
        bool captureTailSegments = false) {
        std::vector<std::wstring> combined;
        std::wstring trimmedTail = TrimWhitespace(tail);
        std::wstring normalizedTail = trimmedTail.empty() ? tail : trimmedTail;
        if (normalizedTail.empty()) {
            if (captureTailSegments) {
                m_layer1SourceSegments.clear();
                if (!prefix.empty()) {
                    ime::conversion::SegmentInfo seg;
                    seg.index = 0;
                    seg.start = 0;
                    seg.length = prefix.size();
                    seg.surface = prefix;
                    m_layer1SourceSegments.push_back(std::move(seg));
                }
            }
            AppendUniqueCandidate(combined, prefix);
            return combined;
        }
        AppendUniqueCandidate(combined, prefix + normalizedTail);
        auto tailCandidates = CollectLayer1BaseCandidates(normalizedTail, allowAsync, captureTailSegments);
        if (captureTailSegments) {
            std::vector<ime::conversion::SegmentInfo> adjusted;
            adjusted.reserve(m_layer1SourceSegments.size() + 1);
            size_t idx = 0;
            if (!prefix.empty()) {
                ime::conversion::SegmentInfo fixed;
                fixed.index = idx++;
                fixed.start = 0;
                fixed.length = prefix.size();
                fixed.surface = prefix;
                adjusted.push_back(std::move(fixed));
            }
            for (auto seg : m_layer1SourceSegments) {
                seg.start += prefix.size();
                seg.index = idx++;
                adjusted.push_back(std::move(seg));
            }
            if (!normalizedTail.empty() && adjusted.size() <= (prefix.empty() ? 0u : 1u)) {
                ime::conversion::SegmentInfo syntheticTail;
                syntheticTail.index = idx++;
                syntheticTail.start = prefix.size();
                syntheticTail.length = normalizedTail.size();
                syntheticTail.surface = normalizedTail;
                adjusted.push_back(std::move(syntheticTail));
            }
            if (!normalizedTail.empty()) {
                std::sort(adjusted.begin(), adjusted.end(),
                    [](const ime::conversion::SegmentInfo& a, const ime::conversion::SegmentInfo& b) {
                        return a.start < b.start;
                    });
                const size_t tailStart = prefix.size();
                const size_t tailEnd = tailStart + normalizedTail.size();
                size_t covered = tailStart;
                bool missingCoverage = false;
                for (const auto& seg : adjusted) {
                    size_t segStart = seg.start;
                    size_t segEnd = seg.start + seg.length;
                    if (segEnd <= tailStart) continue;
                    if (segStart < tailStart) segStart = tailStart;
                    if (segStart > covered) {
                        missingCoverage = true;
                        break;
                    }
                    if (segEnd > covered) covered = (std::min)(segEnd, tailEnd);
                    if (covered >= tailEnd) break;
                }
                if (covered < tailEnd) {
                    missingCoverage = true;
                }
                if (missingCoverage) {
                    std::vector<ime::conversion::SegmentInfo> repaired;
                    repaired.reserve(prefix.empty() ? 1 : 2);
                    size_t repairIdx = 0;
                    if (!prefix.empty()) {
                        ime::conversion::SegmentInfo fixed;
                        fixed.index = repairIdx++;
                        fixed.start = 0;
                        fixed.length = prefix.size();
                        fixed.surface = prefix;
                        repaired.push_back(std::move(fixed));
                    }
                    ime::conversion::SegmentInfo syntheticTail;
                    syntheticTail.index = repairIdx++;
                    syntheticTail.start = tailStart;
                    syntheticTail.length = normalizedTail.size();
                    syntheticTail.surface = normalizedTail;
                    repaired.push_back(std::move(syntheticTail));
                    adjusted = std::move(repaired);
                } else {
                    for (size_t i = 0; i < adjusted.size(); ++i) {
                        adjusted[i].index = i;
                    }
                }
            }
            m_layer1SourceSegments = std::move(adjusted);
        }
        for (const auto& cand : tailCandidates) {
            AppendUniqueCandidate(combined, prefix + cand);
        }
        return combined;
    }
    // Edit session to insert text at caret
    struct InsertTextEditSession : public ITfEditSession {
        LONG _ref{1};
        ITfContext* _context{ nullptr };
        TfClientId _tid{ TF_CLIENTID_NULL };
        std::wstring _text;
        InsertTextEditSession(ITfContext* ctx, TfClientId tid, const std::wstring& txt) : _context(ctx), _tid(tid), _text(txt) { if (_context) _context->AddRef(); }
        ~InsertTextEditSession() { if (_context) _context->Release(); }
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER; *ppv = nullptr;
            if (riid == IID_IUnknown || riid == __uuidof(ITfEditSession)) { *ppv = static_cast<ITfEditSession*>(this); AddRef(); return S_OK; }
            return E_NOINTERFACE;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&_ref); }
        STDMETHODIMP_(ULONG) Release() override { ULONG r = (ULONG)InterlockedDecrement(&_ref); if (!r) delete this; return r; }
        STDMETHODIMP DoEditSession(TfEditCookie ec) override {
            if (!_context || _text.empty()) return S_OK;
            DebugLog(L"InsertText DoEditSession begin");
            // Try InsertAtSelection first
            ComPtr<ITfInsertAtSelection> insert;
            if (SUCCEEDED(_context->QueryInterface(IID_PPV_ARGS(&insert)))) {
                ComPtr<ITfRange> rng;
                insert->InsertTextAtSelection(ec, 0 /*dwFlags*/, _text.c_str(), (ULONG)_text.size(), &rng.p);
                DebugLog(L"InsertText via ITfInsertAtSelection");
                return S_OK;
            }
            // Fallback: use current selection range directly
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (SUCCEEDED(_context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched == 1 && sel.range) {
                sel.range->SetText(ec, 0, _text.c_str(), (LONG)_text.size());
                sel.range->Collapse(ec, TF_ANCHOR_END);
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                _context->SetSelection(ec, 1, &sel);
                DebugLog(L"InsertText via selection range");
                sel.range->Release();
            }
            return S_OK;
        }
    };
    bool InsertText(ITfContext* context, const std::wstring& text) {
        if (!context || text.empty()) return false;
        auto* edit = new (std::nothrow) InsertTextEditSession(context, m_tid, text);
        if (!edit) return false;
        HRESULT hrSession = E_FAIL;
        HRESULT hr = context->RequestEditSession(m_tid, edit, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (FAILED(hr)) {
            DebugLog(L"InsertText SYNC failed; fallback ASYNC", hr);
            HRESULT hr2Session = E_FAIL;
            hr = context->RequestEditSession(m_tid, edit, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr2Session);
        }
        edit->Release();
        return SUCCEEDED(hr);
    }
    static bool IsVowel(wchar_t c) { return c==L'a'||c==L'i'||c==L'u'||c==L'e'||c==L'o'; }
    static bool IsConsonant(wchar_t c) { return (c>=L'a'&&c<=L'z') && !IsVowel(c); }
    // Convert available part of m_romajiBuffer into Hiragana.
    // If finalize=true, flush any remaining 'n' etc.
    std::wstring ConvertRomaji(bool finalize) {
        std::wstring out;
        auto emit = [&](const wchar_t* s) { out.append(s); };
        auto consume = [&](size_t n) { m_romajiBuffer.erase(0, n); };
        struct Map { const wchar_t* roma; const wchar_t* hira; };
        static const Map map4[] = {
            {L"ltsu",L"っ"},
        };
        static const Map map3[] = {
            {L"kya",L"きゃ"},{L"kyu",L"きゅ"},{L"kyo",L"きょ"},
            {L"kwa",L"くぁ"},{L"kwi",L"くぃ"},{L"kwu",L"くぅ"},{L"kwe",L"くぇ"},{L"kwo",L"くぉ"},
            {L"gya",L"ぎゃ"},{L"gyu",L"ぎゅ"},{L"gyo",L"ぎょ"},
            {L"gwa",L"ぐぁ"},{L"gwi",L"ぐぃ"},{L"gwu",L"ぐぅ"},{L"gwe",L"ぐぇ"},{L"gwo",L"ぐぉ"},
            {L"sha",L"しゃ"},{L"shu",L"しゅ"},{L"sho",L"しょ"},
            {L"she",L"しぇ"},
            {L"sya",L"しゃ"},{L"syu",L"しゅ"},{L"syo",L"しょ"},
            {L"jya",L"じゃ"},{L"jyu",L"じゅ"},{L"jyo",L"じょ"},
            {L"cha",L"ちゃ"},{L"chu",L"ちゅ"},{L"cho",L"ちょ"},
            {L"che",L"ちぇ"},
            {L"tya",L"ちゃ"},{L"tyu",L"ちゅ"},{L"tyo",L"ちょ"},
            {L"thi",L"てぃ"},
            {L"cya",L"ちゃ"},{L"cyu",L"ちゅ"},{L"cyo",L"ちょ"},
            {L"tsa",L"つぁ"},{L"tsi",L"つぃ"},{L"tse",L"つぇ"},{L"tso",L"つぉ"},
            {L"xtu",L"っ"},{L"ltu",L"っ"},
            {L"xya",L"ゃ"},{L"xyu",L"ゅ"},{L"xyo",L"ょ"},
            {L"lya",L"ゃ"},{L"lyu",L"ゅ"},{L"lyo",L"ょ"},
            {L"nya",L"にゃ"},{L"nyu",L"にゅ"},{L"nyo",L"にょ"},
            {L"hya",L"ひゃ"},{L"hyu",L"ひゅ"},{L"hyo",L"ひょ"},
            {L"fya",L"ふゃ"},{L"fyu",L"ふゅ"},{L"fyo",L"ふょ"},
            {L"bya",L"びゃ"},{L"byu",L"びゅ"},{L"byo",L"びょ"},
            {L"pya",L"ぴゃ"},{L"pyu",L"ぴゅ"},{L"pyo",L"ぴょ"},
            {L"mya",L"みゃ"},{L"myu",L"みゅ"},{L"myo",L"みょ"},
            {L"rya",L"りゃ"},{L"ryu",L"りゅ"},{L"ryo",L"りょ"},
            {L"vya",L"ゔゃ"},{L"vyi",L"ゔぃ"},{L"vyu",L"ゔゅ"},{L"vye",L"ゔぇ"},{L"vyo",L"ゔょ"},
            {L"shi",L"し"},{L"chi",L"ち"},{L"tsu",L"つ"},
            {L"dya",L"ぢゃ"},{L"dyu",L"ぢゅ"},{L"dyo",L"ぢょ"},
            {L"dhi",L"でぃ"},
        };
        static const Map map2[] = {
            {L"ka",L"か"},{L"ki",L"き"},{L"ku",L"く"},{L"ke",L"け"},{L"ko",L"こ"},
            {L"ga",L"が"},{L"gi",L"ぎ"},{L"gu",L"ぐ"},{L"ge",L"げ"},{L"go",L"ご"},
            {L"sa",L"さ"},{L"si",L"し"},{L"su",L"す"},{L"se",L"せ"},{L"so",L"そ"},
            {L"ja",L"じゃ"},{L"ji",L"じ"},{L"ju",L"じゅ"},{L"je",L"じぇ"},{L"jo",L"じょ"},
            {L"za",L"ざ"},{L"zi",L"じ"},{L"zu",L"ず"},{L"ze",L"ぜ"},{L"zo",L"ぞ"},
            {L"ta",L"た"},{L"ti",L"ち"},{L"tu",L"つ"},{L"te",L"て"},{L"to",L"と"},
            {L"da",L"だ"},{L"di",L"ぢ"},{L"du",L"づ"},{L"de",L"で"},{L"do",L"ど"},
            {L"na",L"な"},{L"ni",L"に"},{L"nu",L"ぬ"},{L"ne",L"ね"},{L"no",L"の"},
            {L"ha",L"は"},{L"hi",L"ひ"},{L"hu",L"ふ"},{L"he",L"へ"},{L"ho",L"ほ"},
            {L"ba",L"ば"},{L"bi",L"び"},{L"bu",L"ぶ"},{L"be",L"べ"},{L"bo",L"ぼ"},
            {L"pa",L"ぱ"},{L"pi",L"ぴ"},{L"pu",L"ぷ"},{L"pe",L"ぺ"},{L"po",L"ぽ"},
            {L"ma",L"ま"},{L"mi",L"み"},{L"mu",L"む"},{L"me",L"め"},{L"mo",L"も"},
            {L"ya",L"や"},{L"yu",L"ゆ"},{L"yo",L"よ"},
            {L"ye",L"いぇ"},
            {L"ra",L"ら"},{L"ri",L"り"},{L"ru",L"る"},{L"re",L"れ"},{L"ro",L"ろ"},
            {L"wa",L"わ"},{L"wo",L"を"},
            {L"wi",L"うぃ"},{L"wu",L"う"},{L"we",L"うぇ"},
            {L"qa",L"くぁ"},{L"qi",L"くぃ"},{L"qu",L"く"},{L"qe",L"くぇ"},{L"qo",L"くぉ"},
            {L"va",L"ゔぁ"},{L"vi",L"ゔぃ"},{L"vu",L"ゔ"},{L"ve",L"ゔぇ"},{L"vo",L"ゔぉ"},
            {L"la",L"ぁ"},{L"li",L"ぃ"},{L"lu",L"ぅ"},{L"le",L"ぇ"},{L"lo",L"ぉ"},
            {L"xa",L"ぁ"},{L"xi",L"ぃ"},{L"xu",L"ぅ"},{L"xe",L"ぇ"},{L"xo",L"ぉ"},
            {L"nn",L"ん"},
            {L"fa",L"ふぁ"},{L"fi",L"ふぃ"},{L"fu",L"ふ"},{L"fe",L"ふぇ"},{L"fo",L"ふぉ"},
        };
        static const Map map1[] = {
            {L"a",L"あ"},{L"i",L"い"},{L"u",L"う"},{L"e",L"え"},{L"o",L"お"},
            {L"n",L"ん"},
        };
        while (!m_romajiBuffer.empty()) {
            if (m_romajiBuffer.size() >= 2 && m_romajiBuffer[0] == m_romajiBuffer[1] && IsConsonant(m_romajiBuffer[0]) && m_romajiBuffer[0] != L'n') {
                emit(L"っ"); consume(1); continue;
            }
            if (m_romajiBuffer[0] == L'n') {
                if (m_romajiBuffer.size() >= 2) {
                    wchar_t n2 = m_romajiBuffer[1];
                    if (n2 == L'\'') { emit(L"ん"); consume(2); continue; }
                    if (n2 == L'y' && m_romajiBuffer.size() < 3) { break; }
                    if (n2 == L'n') {
                        if (m_romajiBuffer.size() >= 3) {
                            wchar_t n3 = m_romajiBuffer[2];
                            if (IsVowel(n3) || n3 == L'y') { emit(L"ん"); consume(1); continue; }
                        }
                        emit(L"ん"); consume(2); continue;
                    }
                    if (!IsVowel(n2) && n2 != L'y') { emit(L"ん"); consume(1); continue; }
                } else {
                    if (finalize) { emit(L"ん"); consume(1); continue; }
                    break;
                }
            }
            bool matched = false;
            if (m_romajiBuffer.size() >= 4) {
                for (const auto& m : map4) { if (m_romajiBuffer.rfind(m.roma, 0) == 0) { emit(m.hira); consume(4); matched = true; break; } }
                if (matched) continue;
            }
            if (m_romajiBuffer.size() >= 3) {
                for (const auto& m : map3) { if (m_romajiBuffer.rfind(m.roma, 0) == 0) { emit(m.hira); consume(3); matched = true; break; } }
                if (matched) continue;
            }
            if (m_romajiBuffer.size() >= 2) {
                for (const auto& m : map2) { if (m_romajiBuffer.rfind(m.roma, 0) == 0) { emit(m.hira); consume(2); matched = true; break; } }
                if (matched) continue;
            }
            for (const auto& m : map1) { if (m_romajiBuffer.rfind(m.roma, 0) == 0) { emit(m.hira); consume(1); matched = true; break; } }
            if (matched) continue;
            wchar_t c0 = m_romajiBuffer[0];
            if (!(c0 >= L'a' && c0 <= L'z')) { wchar_t s[2] = { c0, 0 }; emit(s); consume(1); continue; }
            break;
        }
        return out;
    }
    LONG m_ref;
    ITfThreadMgr* m_ptm;
    TfClientId m_tid;
    // --- Non-TSF fallback helpers -------------------------------------------
    void InjectUnicodeKeystrokes(const std::wstring& s) {
        if (s.empty()) return;
        std::vector<INPUT> ins; ins.reserve(s.size() * 2);
        for (wchar_t wc : s) {
            INPUT down{}; down.type = INPUT_KEYBOARD; down.ki.wScan = wc; down.ki.dwFlags = KEYEVENTF_UNICODE;
            INPUT up{}; up.type = INPUT_KEYBOARD; up.ki.wScan = wc; up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            ins.push_back(down); ins.push_back(up);
        }
        if (!ins.empty()) SendInput((UINT)ins.size(), ins.data(), sizeof(INPUT));
    }
    static HGLOBAL DupGlobal(HGLOBAL h) {
        if (!h) return nullptr; SIZE_T sz = GlobalSize(h); if (!sz) return nullptr;
        HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, sz); if (!copy) return nullptr;
        void* src = GlobalLock(h); void* dst = GlobalLock(copy);
        if (src && dst) memcpy(dst, src, sz);
        if (dst) GlobalUnlock(copy); if (src) GlobalUnlock(h);
        return copy;
    }
    bool PasteWithClipboard(const std::wstring& s) {
        HWND hwnd = GetForegroundWindow(); if (!hwnd || s.empty()) return false;
        if (!OpenClipboard(hwnd)) return false;
        // Backup existing CF_UNICODETEXT
        HGLOBAL hOld = nullptr; std::wstring oldText;
        HANDLE hClip = GetClipboardData(CF_UNICODETEXT);
        if (hClip) {
            HGLOBAL hMem = (HGLOBAL)hClip; HGLOBAL dup = DupGlobal(hMem);
            if (dup) hOld = dup;
        }
        EmptyClipboard();
        SIZE_T bytes = (s.size() + 1) * sizeof(wchar_t);
        HGLOBAL hNew = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!hNew) { CloseClipboard(); return false; }
        void* p = GlobalLock(hNew); memcpy(p, s.c_str(), bytes); GlobalUnlock(hNew);
        SetClipboardData(CF_UNICODETEXT, hNew);
        CloseClipboard();
        // Send Ctrl+V
        INPUT seq[4]{};
        seq[0].type = INPUT_KEYBOARD; seq[0].ki.wVk = VK_CONTROL;
        seq[1].type = INPUT_KEYBOARD; seq[1].ki.wVk = 'V';
        seq[2].type = INPUT_KEYBOARD; seq[2].ki.wVk = 'V'; seq[2].ki.dwFlags = KEYEVENTF_KEYUP;
        seq[3].type = INPUT_KEYBOARD; seq[3].ki.wVk = VK_CONTROL; seq[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, seq, sizeof(INPUT));
        // Restore clipboard best-effort
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            if (hOld) SetClipboardData(CF_UNICODETEXT, hOld);
            CloseClipboard();
        }
        return true;
    }
};

