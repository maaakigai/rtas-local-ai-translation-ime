#include <windows.h>
#include <imm.h>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

#include "..\resource_ids.h"

#pragma comment(lib, "Imm32.lib")

// Fallback IME dev definitions if SDK immdev.h is unavailable
#ifndef IME_PROP_UNICODE
#define IME_PROP_UNICODE                0x00080000
#endif
#ifndef IME_PROP_AT_CARET
#define IME_PROP_AT_CARET               0x00010000
#endif
#ifndef SCS_CAP_COMPSTR
#define SCS_CAP_COMPSTR                 0x00000001
#endif
#ifndef SCS_CAP_MAKEREAD
#define SCS_CAP_MAKEREAD                0x00000002
#endif
// IME menu flags (fallbacks)
#ifndef IMFT_RADIOCHECK
#define IMFT_RADIOCHECK 0x00000001
#endif
#ifndef IMFT_SEPARATOR
#define IMFT_SEPARATOR 0x00000002
#endif
#ifndef IMFT_SUBMENU
#define IMFT_SUBMENU 0x00000004
#endif
#ifndef IMFS_CHECKED
#define IMFS_CHECKED 0x00000008
#endif
#ifndef IMFS_ENABLED
#define IMFS_ENABLED 0x00000000
#endif

typedef struct tagIMEINFO {
    DWORD dwPrivateDataSize;
    DWORD fdwProperty;
    DWORD fdwConversionCaps;
    DWORD fdwSentenceCaps;
    DWORD fdwUICaps;
    DWORD fdwSCSCaps;
    DWORD fdwSelectCaps;
} IMEINFO, *LPIMEINFO;

typedef struct tagTRANSMSG {
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
} TRANSMSG, *LPTRANSMSG;

typedef struct tagTRANSMSGLIST {
    UINT     uMsgCount;
    TRANSMSG TransMsg[1];
} TRANSMSGLIST, *LPTRANSMSGLIST;

extern "C" BOOL WINAPI ImmGenerateMessage(HIMC hIMC);

// Minimal IMM32 IME skeleton providing romaji->hiragana conversion and basic composition

struct CtxState {
    std::wstring romaji;
    std::wstring reading;
};

static HINSTANCE g_hInst = nullptr;
static std::unordered_map<HIMC, CtxState> g_states;
static HICON g_iconKana = nullptr;
static HICON g_iconAlpha = nullptr;
static HBITMAP g_bmpKana = nullptr;
static HBITMAP g_bmpAlpha = nullptr;

static bool IsVowel(wchar_t c) { return c==L'a'||c==L'i'||c==L'u'||c==L'e'||c==L'o'; }
static bool IsConsonant(wchar_t c) { return (c>=L'a'&&c<=L'z') && !IsVowel(c); }

static std::wstring ConvertRomaji(std::wstring& buf, bool finalize) {
    std::wstring out;
    auto emit = [&](const wchar_t* s) { out.append(s); };
    auto consume = [&](size_t n) { buf.erase(0, n); };
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

    while (!buf.empty()) {
        if (buf.size() >= 2 && buf[0] == buf[1] && IsConsonant(buf[0]) && buf[0] != L'n') { emit(L"っ"); consume(1); continue; }
        if (buf[0] == L'n') {
            if (buf.size() >= 2) {
                wchar_t n2 = buf[1];
                if (n2 == L'\'') { emit(L"ん"); consume(2); continue; }
                if (!IsVowel(n2) && n2 != L'y') { emit(L"ん"); consume(1); continue; }
                if (n2 == L'n') { emit(L"ん"); consume(1); continue; }
            } else {
                if (finalize) { emit(L"ん"); consume(1); continue; }
                break;
            }
    }
    bool matched = false;
    if (buf.size() >= 4) {
        for (const auto& m : map4) { if (buf.rfind(m.roma, 0) == 0) { emit(m.hira); consume(4); matched = true; break; } }
        if (matched) continue;
    }
    if (buf.size() >= 3) {
        for (const auto& m : map3) { if (buf.rfind(m.roma, 0) == 0) { emit(m.hira); consume(3); matched = true; break; } }
        if (matched) continue;
        }
        if (buf.size() >= 2) {
            for (const auto& m : map2) { if (buf.rfind(m.roma, 0) == 0) { emit(m.hira); consume(2); matched = true; break; } }
            if (matched) continue;
        }
        for (const auto& m : map1) { if (buf.rfind(m.roma, 0) == 0) { emit(m.hira); consume(1); matched = true; break; } }
        if (matched) continue;
        wchar_t c0 = buf[0];
        if (!(c0 >= L'a' && c0 <= L'z')) { wchar_t s[2] = { c0, 0 }; emit(s); consume(1); continue; }
        break;
    }
    return out;
}

static void UpdateComposition(HIMC hIMC, const std::wstring& comp) {
    LPVOID p = comp.empty() ? nullptr : (LPVOID)comp.c_str();
    DWORD cb = (DWORD)(comp.size() * sizeof(wchar_t));
    ImmSetCompositionStringW(hIMC, SCS_SETSTR, p, cb, nullptr, 0);
    ImmGenerateMessage(hIMC);
}
static void CommitResult(HIMC hIMC, const std::wstring& result) {
    LPVOID pres = result.empty() ? nullptr : (LPVOID)result.c_str();
    DWORD cbres = (DWORD)(result.size() * sizeof(wchar_t));
    ImmSetCompositionStringW(hIMC, SCS_SETSTR, nullptr, 0, pres, cbres);
    ImmGenerateMessage(hIMC);
}
static void CancelComposition(HIMC hIMC) {
    ImmSetCompositionStringW(hIMC, SCS_SETSTR, nullptr, 0, nullptr, 0);
    ImmGenerateMessage(hIMC);
}

extern "C" BOOL WINAPI ImeInquire(LPIMEINFO pii, LPTSTR lpszUIClass, DWORD) {
    if (!pii) return FALSE;
    ZeroMemory(pii, sizeof(*pii));
    pii->dwPrivateDataSize = 0;
    pii->fdwProperty = IME_PROP_UNICODE | IME_PROP_AT_CARET;
    pii->fdwConversionCaps = 0;
    pii->fdwSentenceCaps = 0;
    pii->fdwUICaps = 0; // use default UI
    pii->fdwSCSCaps = SCS_CAP_COMPSTR | SCS_CAP_MAKEREAD;
    pii->fdwSelectCaps = 0;
    if (lpszUIClass) lpszUIClass[0] = 0; // no special UI class
    return TRUE;
}

extern "C" BOOL WINAPI ImeSelect(HIMC hIMC, BOOL fSelect) {
    if (!fSelect) {
        g_states.erase(hIMC);
    }
    return TRUE;
}

extern "C" BOOL WINAPI ImeSetActiveContext(HIMC, BOOL) { return TRUE; }

extern "C" UINT WINAPI ImeProcessKey(HIMC hIMC, UINT vKey, LPARAM, const BYTE*) {
    if (!hIMC) return 0;
    if (!ImmGetOpenStatus(hIMC)) return 0;
    if ((vKey >= 'A' && vKey <= 'Z') || vKey == VK_SPACE || vKey == VK_RETURN || vKey == VK_BACK || vKey == VK_ESCAPE) return 1;
    if (vKey == VK_KANA || vKey == VK_KANJI) return 1;
    return 0;
}

extern "C" UINT WINAPI ImeToAsciiEx(UINT vKey, UINT lKeyData, const BYTE*, LPTRANSMSGLIST, UINT, HIMC hIMC) {
    if (!hIMC) return 0;
    bool open = ImmGetOpenStatus(hIMC) ? true : false;
    auto& st = g_states[hIMC];
    if (vKey == VK_KANA || vKey == VK_KANJI) {
        ImmSetOpenStatus(hIMC, !open);
        if (!open) {
            // turning on: nothing
        } else {
            // turning off: commit remaining
            st.reading.append(ConvertRomaji(st.romaji, true)); st.romaji.clear();
            if (!st.reading.empty()) { CommitResult(hIMC, st.reading); st.reading.clear(); }
        }
        return 0;
    }
    if (!open) return 0;

    if (vKey == VK_ESCAPE) {
        st.romaji.clear(); st.reading.clear(); CancelComposition(hIMC); return 0;
    }
    if (vKey == VK_BACK) {
        if (!st.romaji.empty()) st.romaji.pop_back(); else if (!st.reading.empty()) st.reading.pop_back();
        std::wstring pre = st.reading + st.romaji; UpdateComposition(hIMC, pre); return 0;
    }
    if (vKey == VK_SPACE) {
        st.reading.append(ConvertRomaji(st.romaji, true)); st.romaji.clear();
        if (!st.reading.empty()) { CommitResult(hIMC, st.reading); st.reading.clear(); }
        return 0;
    }
    if (vKey == VK_RETURN) {
        st.reading.append(ConvertRomaji(st.romaji, true)); st.romaji.clear();
        if (!st.reading.empty()) { CommitResult(hIMC, st.reading); st.reading.clear(); }
        return 0;
    }
    if (vKey >= 'A' && vKey <= 'Z') {
        wchar_t ch = (wchar_t)vKey;
        ch = (wchar_t)towlower(ch);
        st.romaji.push_back(ch);
        std::wstring newly = ConvertRomaji(st.romaji, false);
        if (!newly.empty()) st.reading.append(newly);
        std::wstring pre = st.reading + st.romaji;
        UpdateComposition(hIMC, pre);
        return 0;
    }
    return 0;
}

extern "C" BOOL WINAPI ImeSetCompositionString(HIMC, DWORD, LPCVOID, DWORD, LPCVOID, DWORD) { return FALSE; }
extern "C" BOOL WINAPI NotifyIME(HIMC hIMC, DWORD, DWORD, DWORD) {
    if (!hIMC) return FALSE;
    // Minimal: treat as request to cancel/update composition
    CancelComposition(hIMC);
    return TRUE;
}
extern "C" BOOL WINAPI ImeDestroy(UINT) { g_states.clear(); return TRUE; }
extern "C" BOOL WINAPI ImeEscape(HIMC, UINT, LPVOID) { return FALSE; }
extern "C" BOOL WINAPI ImeConfigure(HKL, HWND, DWORD, LPVOID) { return FALSE; }
extern "C" BOOL WINAPI ImeConversionList(HIMC, LPCTSTR, LPCANDIDATELIST, DWORD, UINT) { return FALSE; }
static HICON CreateTextIcon(const wchar_t* text) {
    const int size = 16;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return nullptr;
    HBITMAP color = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) { DeleteDC(hdc); return nullptr; }
    const int maskStride = ((size + 31) / 32) * 4;
    std::vector<BYTE> maskBits(maskStride * size, 0xFF);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    if (!mask) { DeleteObject(color); DeleteDC(hdc); return nullptr; }
    HGDIOBJ oldBmp = SelectObject(hdc, color);
    RECT rc{ 0,0,size,size };
    if (bits) memset(bits, 0, size * size * 4);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ oldFont = nullptr;
    if (font) oldFont = SelectObject(hdc, font);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont) SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBmp);
    if (bits) {
        DWORD* px = static_cast<DWORD*>(bits);
        for (int i = 0; i < size * size; ++i) {
            DWORD rgb = px[i] & 0x00FFFFFF;
            px[i] = rgb | (rgb ? 0xFF000000 : 0x00000000);
        }
    }
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(hdc);
    return icon;
}

static HBITMAP CreateTextBitmap(const wchar_t* text) {
    const int size = 16;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(hdc); return nullptr; }
    HGDIOBJ old = SelectObject(hdc, bmp);
    RECT rc{ 0,0,size,size };
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ oldFont = nullptr;
    if (font) oldFont = SelectObject(hdc, font);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont) SelectObject(hdc, oldFont);
    SelectObject(hdc, old);
    DeleteDC(hdc);
    return bmp;
}

extern "C" HICON WINAPI ImeGetIcon(void) {
    // Ensure cached icons exist
    if (!g_iconKana) g_iconKana = CreateTextIcon(L"あ");
    if (!g_iconAlpha) g_iconAlpha = CreateTextIcon(L"A");

    // Try to base the icon on the current foreground context's open status.
    HWND fg = GetForegroundWindow();
    bool open = false;
    if (fg) {
        HIMC himc = ImmGetContext(fg);
        if (himc) {
            open = ImmGetOpenStatus(himc) ? true : false;
            ImmReleaseContext(fg, himc);
        }
    }
    return open ? (g_iconKana ? g_iconKana : g_iconAlpha) : (g_iconAlpha ? g_iconAlpha : g_iconKana);
}
extern "C" BOOL WINAPI ImeGetRegisterWordStyle(HIMC, UINT, LPSTYLEBUF) { return FALSE; }

// Provide right-click menu items (hiragana / direct input) for the indicator.
extern "C" UINT WINAPI ImeGetImeMenuItems(HIMC hIMC, DWORD, UINT, LPIMEMENUITEMINFO, LPIMEMENUITEMINFO lpImeMenu, DWORD dwSize) {
    // Two entries: Hiragana (open) and Direct (alpha).
    const UINT kCount = 2;
    if (!lpImeMenu || dwSize < kCount * sizeof(IMEMENUITEMINFO)) {
        return kCount;
    }
    bool open = false;
    if (hIMC) {
        open = ImmGetOpenStatus(hIMC) ? true : false;
    }
    if (!g_iconKana) g_iconKana = CreateTextIcon(L"あ");
    if (!g_iconAlpha) g_iconAlpha = CreateTextIcon(L"A");

    IMEMENUITEMINFO items[kCount] = {};
    // Hiragana
    items[0].cbSize = sizeof(IMEMENUITEMINFO);
    items[0].fType = IMFT_RADIOCHECK;
    items[0].fState = IMFS_ENABLED | (open ? IMFS_CHECKED : 0);
    items[0].wID = 1;
    if (!g_bmpKana) g_bmpKana = CreateTextBitmap(L"あ");
    items[0].hbmpItem = g_bmpKana;
    lstrcpynW(items[0].szString, L"ひらがな", ARRAYSIZE(items[0].szString));
    // Direct input (alpha)
    items[1].cbSize = sizeof(IMEMENUITEMINFO);
    items[1].fType = IMFT_RADIOCHECK;
    items[1].fState = IMFS_ENABLED | (open ? 0 : IMFS_CHECKED);
    items[1].wID = 2;
    if (!g_bmpAlpha) g_bmpAlpha = CreateTextBitmap(L"A");
    items[1].hbmpItem = g_bmpAlpha;
    lstrcpynW(items[1].szString, L"直接入力", ARRAYSIZE(items[1].szString));

    memcpy(lpImeMenu, items, sizeof(items));
    return kCount;
}

BOOL APIENTRY DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { g_hInst = h; DisableThreadLibraryCalls(h); }
    else if (r == DLL_PROCESS_DETACH) {
        if (g_iconKana) { DestroyIcon(g_iconKana); g_iconKana = nullptr; }
        if (g_iconAlpha) { DestroyIcon(g_iconAlpha); g_iconAlpha = nullptr; }
        if (g_bmpKana) { DeleteObject(g_bmpKana); g_bmpKana = nullptr; }
        if (g_bmpAlpha) { DeleteObject(g_bmpAlpha); g_bmpAlpha = nullptr; }
    }
    return TRUE;
}
