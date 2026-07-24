#include "rtas_ui.h"

#include <algorithm>
#include <string>
#include <utility>

#include "rtas_globals.h"
#include "rtas_text_service.h"

namespace {
std::wstring CommonPrefixForRange(const std::vector<std::wstring>& items, int start, int count) {
    if (count <= 1 || start < 0 || static_cast<size_t>(start) >= items.size()) {
        return {};
    }
    size_t prefixLen = items[static_cast<size_t>(start)].size();
    for (int i = 1; i < count; ++i) {
        const size_t idx = static_cast<size_t>(start + i);
        if (idx >= items.size()) break;
        const std::wstring& s = items[idx];
        size_t j = 0;
        const size_t limit = (std::min)(prefixLen, s.size());
        while (j < limit && items[static_cast<size_t>(start)][j] == s[j]) {
            ++j;
        }
        prefixLen = j;
        if (prefixLen == 0) break;
    }
    // Avoid aggressive trimming for very short common prefixes.
    if (prefixLen < 2) {
        return {};
    }
    return items[static_cast<size_t>(start)].substr(0, prefixLen);
}

std::wstring CompactCandidateForDisplay(const std::wstring& text, const std::wstring& commonPrefix) {
    if (commonPrefix.empty()) return text;
    if (text.size() >= commonPrefix.size() && text.compare(0, commonPrefix.size(), commonPrefix) == 0) {
        const std::wstring suffix = text.substr(commonPrefix.size());
        if (!suffix.empty()) {
            return L"…" + suffix;
        }
    }
    return text;
}
}  // namespace

ATOM OverlayController::s_overlayClass = 0;
ATOM CandidateUI::s_candidateClass = 0;

OverlayController::OverlayController(TextService* owner)
    : m_owner(owner) {}

OverlayController::~OverlayController() {
    Destroy();
}

void OverlayController::RegisterOverlayClass() {
    if (s_overlayClass) return;
    WNDCLASSW wc{};
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = g_hModule;
    wc.lpszClassName = L"TSF_PreeditOverlay";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    s_overlayClass = RegisterClassW(&wc);
}

LRESULT CALLBACK OverlayController::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<OverlayController*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(255, 255, 224));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        if (self) {
            HFONT old = nullptr;
            if (self->m_font) {
                old = (HFONT)SelectObject(hdc, self->m_font);
            }
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            RECT rcText = rc;
            rcText.left += 6;
            rcText.top += 3;
            DrawTextW(hdc, self->m_text.c_str(), static_cast<int>(self->m_text.size()), &rcText, DT_LEFT | DT_TOP | DT_NOPREFIX);
            if (old) {
                SelectObject(hdc, old);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (self && wParam == kOverlayToastTimerId) {
            KillTimer(hwnd, kOverlayToastTimerId);
            self->ResetToAuto();
            self->UpdateFromOwnerState();
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool OverlayController::EnsureOverlayWindow() {
    if (m_hwnd) return true;
    RegisterOverlayClass();
    if (!s_overlayClass) return false;
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"TSF_PreeditOverlay", L"", WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        nullptr, nullptr, g_hModule, this);
    if (!m_hwnd) return false;

    LOGFONTW lf{};
    SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0);
    lf.lfUnderline = TRUE;
    HDC sdc = GetDC(nullptr);
    int dpiY = sdc ? GetDeviceCaps(sdc, LOGPIXELSY) : 96;
    if (sdc) ReleaseDC(nullptr, sdc);
    lf.lfHeight = -MulDiv(14, dpiY, 72);
    m_font = CreateFontIndirectW(&lf);
    return true;
}

void OverlayController::RepositionOverlay(int width, int height) {
    auto isSuspiciousTopLeft = [](const POINT& p) {
        return (p.x <= 8 && p.y <= 8);
    };
    auto isNearWindowClientOrigin = [](HWND hwnd, const POINT& p) {
        if (!hwnd) return false;
        POINT origin{ 0, 0 };
        if (!ClientToScreen(hwnd, &origin)) return false;
        const int dx = p.x - origin.x;
        const int dy = p.y - origin.y;
        return (dx >= -6 && dx <= 24 && dy >= -6 && dy <= 40);
    };
    POINT pt{ 0, 0 };
    bool hasPos = false;
    const wchar_t* anchorSource = L"none";
    bool fromGuiThreadInfo = false;
    bool guiCaretRectWasZero = false;
    if (m_owner && m_owner->TryGetLastKnownCaretScreenPoint(&pt)) {
        hasPos = true;
        anchorSource = L"tsf-cache";
    }
    DWORD guiThreadId = 0;
    if (HWND fg = GetForegroundWindow()) {
        guiThreadId = GetWindowThreadProcessId(fg, nullptr);
    }
    if (!guiThreadId) {
        guiThreadId = GetCurrentThreadId();
    }
    GUITHREADINFO gi{ sizeof(gi) };
    if (!hasPos && GetGUIThreadInfo(guiThreadId, &gi) && (gi.hwndCaret || gi.hwndFocus)) {
        RECT rc = gi.rcCaret;
        guiCaretRectWasZero =
            (rc.left == 0 && rc.top == 0 && rc.right == 0 && rc.bottom == 0);
        pt.x = rc.left;
        pt.y = rc.bottom + 2;
        if (gi.hwndCaret) {
            POINT tmp{ pt.x, pt.y };
            ClientToScreen(gi.hwndCaret, &tmp);
            pt = tmp;
        }
        else if (gi.hwndFocus) {
            POINT tmp{ pt.x, pt.y };
            ClientToScreen(gi.hwndFocus, &tmp);
            pt = tmp;
        }
        hasPos = true;
        fromGuiThreadInfo = true;
        anchorSource = L"gui-thread-info";
    }
    if (hasPos && fromGuiThreadInfo) {
        // Some hosts report a dummy caret rect around (0,0) even when editing
        // is active. Keep the previous stable anchor in that case.
        if ((guiCaretRectWasZero && !gi.hwndCaret) ||
            isSuspiciousTopLeft(pt)) {
            hasPos = false;
        }
    }
    if (hasPos && isNearWindowClientOrigin(gi.hwndFocus, pt)) {
        hasPos = false;
    }
    if (hasPos) {
        m_lastAnchorPos = pt;
        m_hasLastAnchorPos = true;
    }
    if (!hasPos) {
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            POINT c{};
            if (GetCaretPos(&c)) {
                pt = c;
                ClientToScreen(hwnd, &pt);
                pt.y += 20;
                hasPos = true;
                anchorSource = L"win-caret";
            }
        }
    }
    if (hasPos && isSuspiciousTopLeft(pt)) {
        hasPos = false;
    }
    if (hasPos) {
        HWND hwnd = GetForegroundWindow();
        if (isNearWindowClientOrigin(hwnd, pt)) {
            hasPos = false;
        }
    }
    if (hasPos) {
        m_lastAnchorPos = pt;
        m_hasLastAnchorPos = true;
    }
    if (!hasPos) {
        if (m_hasLastAnchorPos) {
            pt = m_lastAnchorPos;
            hasPos = true;
            anchorSource = L"last-anchor";
        } else if (m_mode != 2) {
            // For preedit overlay, keep legacy fallback behavior.
            if (::GetCursorPos(&pt)) {
                hasPos = true;
                anchorSource = L"cursor-fallback";
            } else {
                pt.x = 8;
                pt.y = 8;
                anchorSource = L"hardcoded-fallback";
            }
        } else {
            // For mode toast, never fallback to cursor/top-left.
            // If we cannot resolve a caret anchor, skip showing.
            if (m_owner) {
                m_owner->AppendModeToastDebugLog(
                    L"Overlay.Reposition skipped source=none mode=toast");
            }
            return;
        }
    }

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    RECT work{ 0,0,GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (mon && GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
    }
    if (pt.x + width > work.right) pt.x = std::max(work.left, work.right - width);
    if (pt.y + height > work.bottom) pt.y = std::max(work.top, pt.y - height - 20);
    if (pt.y < work.top) pt.y = work.top;
    if (pt.x < work.left) pt.x = work.left;

    if (m_owner) {
        m_owner->AppendModeToastDebugLog(
            L"Overlay.Reposition source=" + std::wstring(anchorSource) +
            L" x=" + std::to_wstring(pt.x) +
            L" y=" + std::to_wstring(pt.y) +
            L" w=" + std::to_wstring(width) +
            L" h=" + std::to_wstring(height));
    }

    SetWindowPos(m_hwnd, HWND_TOPMOST, pt.x, pt.y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void OverlayController::ShowOverlayText(const std::wstring& text) {
    if (!EnsureOverlayWindow()) return;
    m_text = text;
    HDC hdc = GetDC(m_hwnd);
    HFONT old = nullptr;
    if (m_font) {
        old = (HFONT)SelectObject(hdc, m_font);
    }
    SIZE sz{ 0, 0 };
    GetTextExtentPoint32W(hdc, m_text.c_str(), static_cast<int>(m_text.size()), &sz);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    if (old) {
        SelectObject(hdc, old);
    }
    ReleaseDC(m_hwnd, hdc);
    int width = sz.cx + 12;
    int height = static_cast<int>((sz.cy > static_cast<int>(tm.tmHeight) ? sz.cy : tm.tmHeight) + 6);
    RepositionOverlay(width, height);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void OverlayController::ResetToAuto() {
    if (m_mode == 2) {
        m_mode = 0;
    }
}

void OverlayController::UpdateFromOwnerState() {
    if (!m_owner) return;
    if (m_mode == 2) return;
    if (m_owner->HasActiveComposition()) {
        Hide();
        return;
    }
    std::wstring pre = m_owner->GetPreeditText();
    if (!pre.empty()) {
        ShowOverlayText(pre);
        m_mode = 1;
    }
    else {
        Hide();
        m_mode = 0;
    }
}

void OverlayController::ShowModeToast(const std::wstring& label, UINT durationMs) {
    if (!EnsureOverlayWindow()) return;
    m_mode = 2;
    ShowOverlayText(label);
    SetTimer(m_hwnd, kOverlayToastTimerId, durationMs, nullptr);
}

void OverlayController::Hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void OverlayController::Destroy() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_font) {
        DeleteObject(m_font);
        m_font = nullptr;
    }
    m_text.clear();
    m_mode = 0;
}

CandidateUI::CandidateUI(TextService* owner, ITfUIElementMgr* mgr, ITfDocumentMgr* docMgr, const std::vector<std::wstring>& cands)
    : m_owner(owner), m_mgr(mgr), m_docMgr(docMgr), m_candidates(cands) {
    if (m_mgr) m_mgr->AddRef();
    if (m_docMgr) m_docMgr->AddRef();
}

CandidateUI::~CandidateUI() {
    DestroyCandidateWindow();
    if (m_mgr) m_mgr->Release();
    if (m_docMgr) m_docMgr->Release();
}

STDMETHODIMP CandidateUI::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == __uuidof(ITfUIElement)) {
        *ppv = static_cast<ITfUIElement*>(this);
    }
    else if (riid == __uuidof(ITfCandidateListUIElement)) {
        *ppv = static_cast<ITfCandidateListUIElement*>(this);
    }
    else if (riid == __uuidof(ITfCandidateListUIElementBehavior)) {
        *ppv = static_cast<ITfCandidateListUIElementBehavior*>(this);
    }
    else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CandidateUI::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_ref));
}

STDMETHODIMP_(ULONG) CandidateUI::Release() {
    ULONG r = static_cast<ULONG>(InterlockedDecrement(&m_ref));
    if (!r) delete this;
    return r;
}

STDMETHODIMP CandidateUI::GetDescription(BSTR* pbstr) {
    if (!pbstr) return E_POINTER;
    *pbstr = SysAllocString(L"Candidates");
    return *pbstr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUI::GetGUID(GUID* pguid) {
    if (!pguid) return E_POINTER;
    static const GUID kCandidateGuid = { 0x7f9c9a40,0x6f17,0x4c30,{0xa5,0x28,0x5b,0x41,0x77,0x9a,0x6b,0x2f} };
    *pguid = kCandidateGuid;
    return S_OK;
}

STDMETHODIMP CandidateUI::Show(BOOL fShow) {
    m_shown = !!fShow;
    UpdateCandidateWindow();
    return S_OK;
}

STDMETHODIMP CandidateUI::IsShown(BOOL* pfShow) {
    if (!pfShow) return E_POINTER;
    *pfShow = m_shown;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetUpdatedFlags(DWORD* pdwFlags) {
    if (!pdwFlags) return E_POINTER;
    *pdwFlags = m_updatedFlags;
    m_updatedFlags = 0;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetDocumentMgr(ITfDocumentMgr** ppdm) {
    if (!ppdm) return E_POINTER;
    *ppdm = nullptr;
    if (m_docMgr) {
        m_docMgr->AddRef();
        *ppdm = m_docMgr;
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::GetCount(UINT* puCount) {
    if (!puCount) return E_POINTER;
    *puCount = static_cast<UINT>(m_candidates.size());
    return S_OK;
}

STDMETHODIMP CandidateUI::GetSelection(UINT* puIndex) {
    if (!puIndex) return E_POINTER;
    *puIndex = static_cast<UINT>(m_selection);
    return S_OK;
}

STDMETHODIMP CandidateUI::GetString(UINT uIndex, BSTR* pbstr) {
    if (!pbstr) return E_POINTER;
    if (uIndex >= m_candidates.size()) return E_INVALIDARG;
    *pbstr = SysAllocString(m_candidates[uIndex].c_str());
    return *pbstr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUI::GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt) {
    const UINT kPageSize = 9;
    UINT count = static_cast<UINT>(m_candidates.size());
    UINT pageCnt = count == 0 ? 1u : ((count + kPageSize - 1) / kPageSize);
    if (puPageCnt) *puPageCnt = pageCnt;
    if (pIndex && uSize) {
        UINT fill = (uSize < pageCnt) ? uSize : pageCnt;
        for (UINT i = 0; i < fill; ++i) {
            pIndex[i] = i * kPageSize;
        }
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::SetPageIndex(UINT*, UINT) {
    return E_NOTIMPL;
}

STDMETHODIMP CandidateUI::GetCurrentPage(UINT* puPage) {
    if (!puPage) return E_POINTER;
    *puPage = (m_selection < 0) ? 0u : static_cast<UINT>(m_selection / 9);
    return S_OK;
}

STDMETHODIMP CandidateUI::SetSelection(UINT nIndex) {
    if (nIndex >= m_candidates.size()) return E_INVALIDARG;
    m_selection = static_cast<int>(nIndex);
    if (m_owner) m_owner->OnCandidateSelectionChanged(SelectedString());
    m_updatedFlags |= TF_CLUIE_SELECTION;
    NotifyUpdate();
    return S_OK;
}

STDMETHODIMP CandidateUI::Finalize(void) {
    if (m_owner) m_owner->OnCandidateFinalize(m_selection);
    return S_OK;
}

STDMETHODIMP CandidateUI::Abort(void) {
    if (m_owner) m_owner->OnCandidateAbort();
    return S_OK;
}

bool CandidateUI::Begin() {
    if (!m_mgr) return false;
    BOOL show = TRUE;
    if (FAILED(m_mgr->BeginUIElement(this, &show, &m_id))) return false;
    m_shown = !!show;
    UpdateCandidateWindow();
    return true;
}

void CandidateUI::End() {
    HideCandidateWindow();
    if (m_mgr && m_id != static_cast<DWORD>(-1)) m_mgr->EndUIElement(m_id);
    m_id = static_cast<DWORD>(-1);
    DestroyCandidateWindow();
}

void CandidateUI::NotifyUpdate() {
    UpdateCandidateWindow();
    if (m_mgr && m_id != static_cast<DWORD>(-1)) m_mgr->UpdateUIElement(m_id);
}

void CandidateUI::MoveSelection(int delta) {
    if (m_candidates.empty()) return;
    int count = static_cast<int>(m_candidates.size());
    int next = (m_selection + delta + count) % count;
    if (next != m_selection && m_owner) {
        m_selection = next;
        m_owner->OnCandidateSelectionChanged(SelectedString());
    }
    else {
        m_selection = next;
    }
    m_updatedFlags |= TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
    NotifyUpdate();
}

int CandidateUI::SelectionIndex() const {
    return m_selection;
}

void CandidateUI::SetCandidates(std::vector<std::wstring> candidates, int selection, bool notifyOwner) {
    m_candidates = std::move(candidates);
    int count = static_cast<int>(m_candidates.size());
    if (count == 0) {
        m_selection = 0;
    }
    else {
        if (selection < 0) selection = 0;
        if (selection >= count) selection = count - 1;
        if (selection != m_selection) {
            m_selection = selection;
        }
    }
    m_updatedFlags |= TF_CLUIE_COUNT | TF_CLUIE_STRING | TF_CLUIE_SELECTION | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    NotifyUpdate();
    if (notifyOwner && m_owner) {
        m_owner->OnCandidateSelectionChanged(SelectedString());
    }
}

bool CandidateUI::AddCandidateFront(const std::wstring& candidate) {
    if (candidate.empty()) return false;
    auto it = std::find(m_candidates.begin(), m_candidates.end(), candidate);
    if (it != m_candidates.end()) {
        int idx = static_cast<int>(it - m_candidates.begin());
        if (m_selection != idx) {
            m_selection = idx;
            m_updatedFlags |= TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
            NotifyUpdate();
            if (m_owner) m_owner->OnCandidateSelectionChanged(SelectedString());
        }
        return false;
    }
    m_candidates.insert(m_candidates.begin(), candidate);
    m_selection = 0;
    m_updatedFlags |= TF_CLUIE_COUNT | TF_CLUIE_STRING | TF_CLUIE_SELECTION | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    NotifyUpdate();
    if (m_owner) m_owner->OnCandidateSelectionChanged(SelectedString());
    return true;
}

const std::wstring& CandidateUI::SelectedString() const {
    static const std::wstring kEmpty;
    return m_candidates.empty() ? kEmpty : m_candidates[static_cast<size_t>(m_selection)];
}

void CandidateUI::RegisterCandidateWindowClass() {
    if (s_candidateClass) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = CandidateWndProc;
    wc.hInstance = g_hModule;
    wc.lpszClassName = L"RTAS_CandidatePopup";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    s_candidateClass = RegisterClassW(&wc);
}

LRESULT CALLBACK CandidateUI::CandidateWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<CandidateUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (self) {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            self->PaintCandidateWindow(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool CandidateUI::EnsureCandidateWindow() {
    if (m_hwnd) return true;
    RegisterCandidateWindowClass();
    if (!s_candidateClass) return false;
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"RTAS_CandidatePopup",
        L"",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        nullptr, nullptr, g_hModule, this);
    if (!m_hwnd) return false;

    LOGFONTW lf{};
    SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0);
    HDC sdc = GetDC(nullptr);
    const int dpiY = sdc ? GetDeviceCaps(sdc, LOGPIXELSY) : 96;
    if (sdc) ReleaseDC(nullptr, sdc);
    lf.lfHeight = -MulDiv(13, dpiY, 72);
    m_font = CreateFontIndirectW(&lf);
    RefreshCandidateWindowMetrics();
    return true;
}

void CandidateUI::DestroyCandidateWindow() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_font) {
        DeleteObject(m_font);
        m_font = nullptr;
    }
}

void CandidateUI::HideCandidateWindow() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void CandidateUI::RefreshCandidateWindowMetrics() {
    if (!m_hwnd) return;
    HDC hdc = GetDC(m_hwnd);
    if (!hdc) return;
    HFONT old = nullptr;
    if (m_font) old = (HFONT)SelectObject(hdc, m_font);
    TEXTMETRICW tm{};
    if (GetTextMetricsW(hdc, &tm)) {
        m_rowHeight = std::max(20, static_cast<int>(tm.tmHeight) + 8);
    }
    if (old) SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);
}

void CandidateUI::RepositionCandidateWindow(int width, int height) {
    if (!m_hwnd) return;

    auto isSuspiciousTopLeft = [](const POINT& p) {
        return (p.x <= 8 && p.y <= 8);
    };
    auto isNearWindowClientOrigin = [](HWND hwnd, const POINT& p) {
        if (!hwnd) return false;
        POINT origin{ 0, 0 };
        if (!ClientToScreen(hwnd, &origin)) return false;
        const int dx = p.x - origin.x;
        const int dy = p.y - origin.y;
        return (dx >= -6 && dx <= 24 && dy >= -6 && dy <= 40);
    };

    POINT pt{ 0, 0 };
    bool hasPos = false;
    bool fromGuiThreadInfo = false;
    bool guiCaretRectWasZero = false;
    if (m_owner && m_owner->TryGetLastKnownCaretScreenPoint(&pt)) {
        hasPos = true;
    }
    DWORD guiThreadId = 0;
    if (HWND fg = GetForegroundWindow()) {
        guiThreadId = GetWindowThreadProcessId(fg, nullptr);
    }
    if (!guiThreadId) {
        guiThreadId = GetCurrentThreadId();
    }
    GUITHREADINFO gi{ sizeof(gi) };
    if (!hasPos && GetGUIThreadInfo(guiThreadId, &gi) && (gi.hwndCaret || gi.hwndFocus)) {
        RECT rc = gi.rcCaret;
        guiCaretRectWasZero =
            (rc.left == 0 && rc.top == 0 && rc.right == 0 && rc.bottom == 0);
        pt.x = rc.left;
        pt.y = rc.bottom + 2;
        if (gi.hwndCaret) {
            POINT tmp{ pt.x, pt.y };
            ClientToScreen(gi.hwndCaret, &tmp);
            pt = tmp;
        }
        else if (gi.hwndFocus) {
            POINT tmp{ pt.x, pt.y };
            ClientToScreen(gi.hwndFocus, &tmp);
            pt = tmp;
        }
        hasPos = true;
        fromGuiThreadInfo = true;
    }
    if (hasPos && fromGuiThreadInfo) {
        // Some hosts report a dummy caret rect around (0,0) even when editing
        // is active. Keep the previous stable anchor in that case.
        if ((guiCaretRectWasZero && !gi.hwndCaret) ||
            isSuspiciousTopLeft(pt)) {
            hasPos = false;
        }
    }
    if (hasPos && isNearWindowClientOrigin(gi.hwndFocus, pt)) {
        hasPos = false;
    }
    if (hasPos) {
        m_lastAnchorPos = pt;
        m_hasLastAnchorPos = true;
    }
    if (!hasPos) {
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            POINT c{};
            if (GetCaretPos(&c)) {
                pt = c;
                ClientToScreen(hwnd, &pt);
                pt.y += 20;
                hasPos = true;
            }
        }
    }
    if (hasPos && isSuspiciousTopLeft(pt)) {
        hasPos = false;
    }
    if (hasPos) {
        HWND hwnd = GetForegroundWindow();
        if (isNearWindowClientOrigin(hwnd, pt)) {
            hasPos = false;
        }
    }
    if (hasPos) {
        m_lastAnchorPos = pt;
        m_hasLastAnchorPos = true;
    }
    if (!hasPos) {
        if (m_hasLastAnchorPos) {
            pt = m_lastAnchorPos;
            hasPos = true;
        } else {
            // Last-resort fallback: current cursor position instead of top-left.
            if (::GetCursorPos(&pt)) {
                hasPos = true;
            } else {
                pt.x = 8;
                pt.y = 8;
            }
        }
    }

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    RECT work{ 0,0,GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (mon && GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
    }
    if (pt.x + width > work.right) pt.x = std::max(work.left, work.right - width);
    if (pt.y + height > work.bottom) pt.y = std::max(work.top, pt.y - height - m_rowHeight);
    if (pt.y < work.top) pt.y = work.top;
    if (pt.x < work.left) pt.x = work.left;

    SetWindowPos(m_hwnd, HWND_TOPMOST, pt.x, pt.y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CandidateUI::PaintCandidateWindow(HDC hdc) {
    RECT rc{};
    GetClientRect(m_hwnd, &rc);

    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    HFONT oldFont = nullptr;
    if (m_font) oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);

    const int padX = 8;
    const int numWidth = 22;
    const int topPad = 4;
    const int count = static_cast<int>(m_candidates.size());
    int start = (m_selection >= 0 ? (m_selection / 9) * 9 : 0);
    if (start < 0) start = 0;
    if (start > std::max(0, count - 9)) start = std::max(0, count - 9);
    const int visible = std::min(count - start, 9);
    const std::wstring commonPrefix = CommonPrefixForRange(m_candidates, start, visible);
    for (int i = 0; i < visible; ++i) {
        const int idx = start + i;
        RECT row{
            rc.left + 1,
            rc.top + topPad + i * m_rowHeight,
            rc.right - 1,
            rc.top + topPad + (i + 1) * m_rowHeight
        };
        if (idx == m_selection) {
            HBRUSH sel = CreateSolidBrush(RGB(225, 239, 255));
            FillRect(hdc, &row, sel);
            DeleteObject(sel);
            SetTextColor(hdc, RGB(0, 51, 153));
        }
        else {
            SetTextColor(hdc, RGB(32, 32, 32));
        }

        wchar_t numBuf[8]{};
        wsprintfW(numBuf, L"%d.", idx + 1);
        RECT rcNum = row;
        rcNum.left += padX;
        rcNum.right = rcNum.left + numWidth;
        DrawTextW(hdc, numBuf, -1, &rcNum, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT rcText = row;
        rcText.left += padX + numWidth;
        rcText.right -= padX;
        const std::wstring& text = m_candidates[static_cast<size_t>(idx)];
        const std::wstring compact = CompactCandidateForDisplay(text, commonPrefix);
        DrawTextW(hdc, compact.c_str(), static_cast<int>(compact.size()), &rcText,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (oldFont) SelectObject(hdc, oldFont);
}

void CandidateUI::UpdateCandidateWindow() {
    const bool shouldShow = (m_shown != FALSE) && !m_candidates.empty();
    if (!shouldShow) {
        HideCandidateWindow();
        return;
    }
    if (!EnsureCandidateWindow()) return;

    RefreshCandidateWindowMetrics();

    int width = 180;
    const int count = static_cast<int>(m_candidates.size());
    int start = (m_selection >= 0 ? (m_selection / 9) * 9 : 0);
    if (start < 0) start = 0;
    if (start > std::max(0, count - 9)) start = std::max(0, count - 9);
    int visible = std::min(count - start, 9);
    m_visibleCount = visible;
    HDC hdc = GetDC(m_hwnd);
    if (hdc) {
        HFONT old = nullptr;
        if (m_font) old = (HFONT)SelectObject(hdc, m_font);
        const std::wstring commonPrefix = CommonPrefixForRange(m_candidates, start, visible);
        for (int i = 0; i < visible; ++i) {
            SIZE sz{};
            const std::wstring& text = m_candidates[static_cast<size_t>(start + i)];
            const std::wstring compact = CompactCandidateForDisplay(text, commonPrefix);
            if (GetTextExtentPoint32W(hdc, compact.c_str(), static_cast<int>(compact.size()), &sz)) {
                width = std::max(width, static_cast<int>(sz.cx) + 46);
            }
        }
        if (old) SelectObject(hdc, old);
        ReleaseDC(m_hwnd, hdc);
    }
    int height = (visible * m_rowHeight) + 8;
    RepositionCandidateWindow(width, height);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}
