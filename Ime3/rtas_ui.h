#pragma once

#include <msctf.h>
#include <windows.h>

#include <string>
#include <vector>

class TextService;

class OverlayController {
public:
    explicit OverlayController(TextService* owner);
    ~OverlayController();

    void UpdateFromOwnerState();
    void ShowModeToast(const std::wstring& label, UINT durationMs = 800);
    void Hide();
    void Destroy();

private:
    bool EnsureOverlayWindow();
    void RepositionOverlay(int width, int height);
    void ShowOverlayText(const std::wstring& text);
    void ResetToAuto();

    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void RegisterOverlayClass();

    TextService* m_owner;
    HWND m_hwnd = nullptr;
    HFONT m_font = nullptr;
    std::wstring m_text;
    int m_mode = 0; // 0: auto, 1: preedit, 2: toast
    POINT m_lastAnchorPos{ 0, 0 };
    bool m_hasLastAnchorPos = false;

    static const UINT kOverlayToastTimerId = 1;
    static ATOM s_overlayClass;
};

class CandidateUI final : public ITfCandidateListUIElementBehavior {
public:
    CandidateUI(TextService* owner, ITfUIElementMgr* mgr, ITfDocumentMgr* docMgr, const std::vector<std::wstring>& cands);
    ~CandidateUI();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR* pbstr) override;
    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP IsShown(BOOL* pfShow) override;

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD* pdwFlags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** ppdm) override;
    STDMETHODIMP GetCount(UINT* puCount) override;
    STDMETHODIMP GetSelection(UINT* puIndex) override;
    STDMETHODIMP GetString(UINT uIndex, BSTR* pbstr) override;
    STDMETHODIMP GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt) override;
    STDMETHODIMP SetPageIndex(UINT* pIndex, UINT uPageCnt) override;
    STDMETHODIMP GetCurrentPage(UINT* puPage) override;

    // ITfCandidateListUIElementBehavior
    STDMETHODIMP SetSelection(UINT nIndex) override;
    STDMETHODIMP Finalize(void) override;
    STDMETHODIMP Abort(void) override;

    bool Begin();
    void End();
    void NotifyUpdate();
    void MoveSelection(int delta);
    int SelectionIndex() const;
    void SetCandidates(std::vector<std::wstring> candidates, int selection = 0, bool notifyOwner = true);
    bool AddCandidateFront(const std::wstring& candidate);
    const std::wstring& SelectedString() const;

private:
    bool EnsureCandidateWindow();
    void DestroyCandidateWindow();
    void UpdateCandidateWindow();
    void HideCandidateWindow();
    void RepositionCandidateWindow(int width, int height);
    void PaintCandidateWindow(HDC hdc);
    void RefreshCandidateWindowMetrics();
    static void RegisterCandidateWindowClass();
    static LRESULT CALLBACK CandidateWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    LONG m_ref = 1;
    TextService* m_owner = nullptr;
    ITfUIElementMgr* m_mgr = nullptr;
    ITfDocumentMgr* m_docMgr = nullptr;
    std::vector<std::wstring> m_candidates;
    int m_selection = 0;
    BOOL m_shown = TRUE;
    DWORD m_id = static_cast<DWORD>(-1);
    DWORD m_updatedFlags = TF_CLUIE_STRING | TF_CLUIE_SELECTION | TF_CLUIE_DOCUMENTMGR;
    HWND m_hwnd = nullptr;
    HFONT m_font = nullptr;
    int m_rowHeight = 20;
    int m_visibleCount = 0;
    POINT m_lastAnchorPos{ 0, 0 };
    bool m_hasLastAnchorPos = false;

    static ATOM s_candidateClass;
};
