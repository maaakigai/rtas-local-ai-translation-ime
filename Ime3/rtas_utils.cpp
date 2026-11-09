#include "rtas_utils.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>

#include "rtas_globals.h"

extern const GUID GUID_Preserved_Toggle;
extern const GUID GUID_Preserved_KANA;
extern const GUID GUID_Preserved_KANJI;
extern const GUID GUID_Preserved_DBE_ALPHANUMERIC;
extern const GUID GUID_Preserved_DBE_HIRAGANA;
extern const GUID GUID_Preserved_DBE_KATAKANA;
extern const GUID GUID_Preserved_DBE_SBCSCHAR;
extern const GUID GUID_Preserved_DBE_DBCSCHAR;
extern const GUID GUID_Preserved_DBE_ROMAN;

namespace {

struct WarframeTerm {
    std::wstring_view reading;
    std::wstring_view english;
};

constexpr WarframeTerm kWarframeTerms[] = {
    // https://www.warframe.com/{en,ja}/news/the-old-blood-8
    {L"くばりっち", L"Kuva Lich"},
    {L"くば・りっち", L"Kuva Lich"},
    {L"くば りっち", L"Kuva Lich"},
    {L"くば　りっち", L"Kuva Lich"},
    // https://www.warframe.com/en/patch-notes/pc/30-5-0
    {L"がるばないずどもっど", L"Galvanized Mod"},
    {L"がるばないずどmod", L"Galvanized Mod"},
    // https://www.warframe.com/{en,ja}/news/the-old-blood-8
    {L"れくいえむもっど", L"Requiem Mod"},
    {L"れくいえむmod", L"Requiem Mod"},
    // https://www.warframe.com/ja/news/lich-sistemi-nedir
    {L"くばへっく", L"Kuva Hek"},
    {L"くばぬこあ", L"Kuva Nukor"},
    {L"くばぬこー", L"Kuva Nukor"},
    {L"くばぶらんま", L"Kuva Bramma"},
};

std::wstring FoldWarframeTerm(std::wstring_view text) {
    std::wstring folded(text);
    for (wchar_t& ch : folded) {
        if (ch >= L'ァ' && ch <= L'ヶ') ch -= L'ァ' - L'ぁ';
        if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
    }
    return folded;
}

bool IsLatinWordChar(wchar_t ch) {
    return (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'_';
}

bool HasWordBoundary(std::wstring_view text, size_t start, size_t length) {
    const size_t end = start + length;
    return !(start && IsLatinWordChar(text[start]) && IsLatinWordChar(text[start - 1])) &&
        !(end < text.size() && IsLatinWordChar(text[end - 1]) && IsLatinWordChar(text[end]));
}

}  // namespace

void WarframeDictionary::Add(std::wstring_view alias, std::wstring_view english, bool translate) {
    if (alias.empty()) return;
    auto folded = FoldWarframeTerm(alias);
    const bool canonicalEnglish = alias == english;
    if (translate || canonicalEnglish) {
        auto& lengths = lengths_[folded.front()];
        const auto pos = std::lower_bound(lengths.begin(), lengths.end(), folded.size(), std::greater<>{});
        if (pos == lengths.end() || *pos != folded.size()) lengths.insert(pos, folded.size());
    }
    auto& entry = aliases_[std::move(folded)];
    if (std::find(entry.names.begin(), entry.names.end(), english) == entry.names.end()) {
        entry.names.emplace_back(english);
    }
    entry.translate |= translate;
    entry.english |= canonicalEnglish;
}

void WarframeDictionary::Load(const std::filesystem::path& path) {
    aliases_.clear();
    lengths_.clear();
    for (const auto& term : kWarframeTerms) {
        Add(term.reading, term.english, true);
        Add(term.english, term.english, true);
    }

    if (path.empty()) return;
    std::ifstream stream(path, std::ios::binary);
    std::string line;
    if (!std::getline(stream, line) || line != "# RTAS Warframe dictionary v1") return;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() == '#') continue;
        const size_t first = line.find('\t');
        const size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
        const size_t third = second == std::string::npos ? second : line.find('\t', second + 1);
        if (third == std::string::npos) continue;
        const auto english = Utf8ToWide(line.substr(0, first));
        if (english.empty()) continue;
        const auto japanese = Utf8ToWide(line.substr(first + 1, second - first - 1));
        const auto reading = Utf8ToWide(line.substr(second + 1, third - second - 1));
        Add(english, english, false);
        // Short official labels often overlap ordinary Japanese words.
        const bool translate = japanese.size() >= 4 && japanese != english &&
            std::any_of(japanese.begin(), japanese.end(), [](wchar_t ch) { return ch > 0x7F; });
        Add(japanese, english, translate);
        Add(reading, english, std::string_view(line).substr(third + 1).starts_with("supplement:"));
    }
}

std::span<const std::wstring> WarframeDictionary::Find(std::wstring_view reading) const {
    if (aliases_.empty()) return {};
    const auto entry = aliases_.find(FoldWarframeTerm(reading));
    if (entry == aliases_.end()) return {};
    return entry->second.names;
}

std::wstring WarframeDictionary::Normalize(std::wstring_view text) const {
    return Rewrite(text, {});
}

std::wstring WarframeDictionary::RestoreEnglishSpelling(
    std::wstring_view source, std::wstring_view translation) const {
    if (source.empty() || lengths_.empty()) return std::wstring(translation);
    return Rewrite(translation, FoldWarframeTerm(source));
}

std::wstring WarframeDictionary::Rewrite(std::wstring_view text, std::wstring_view foldedSource) const {
    if (lengths_.empty()) return std::wstring(text);
    std::wstring normalized;
    const auto folded = FoldWarframeTerm(text);
    for (size_t pos = 0; pos < folded.size();) {
        size_t consumed = 0;
        const auto prefix = lengths_.find(folded[pos]);
        if (prefix != lengths_.end()) {
            for (const size_t length : prefix->second) {
                if (length > folded.size() - pos || !HasWordBoundary(folded, pos, length)) continue;
                const auto alias = std::wstring_view(folded).substr(pos, length);
                const auto entry = aliases_.find(alias);
                if (entry == aliases_.end()) continue;
                if (foldedSource.empty()) {
                    if (!entry->second.translate) continue;
                } else {
                    if (!entry->second.english) continue;
                    size_t match = foldedSource.find(alias);
                    while (match != std::wstring_view::npos && !HasWordBoundary(foldedSource, match, length)) {
                        match = foldedSource.find(alias, match + 1);
                    }
                    if (match == std::wstring_view::npos) continue;
                }
                if (entry->second.names.size() == 1) normalized += entry->second.names.front();
                else normalized.append(text.substr(pos, length));
                consumed = length;
                break;
            }
        }
        if (!consumed) {
            normalized.push_back(text[pos]);
            consumed = 1;
        }
        pos += consumed;
    }
    return normalized;
}

UINT GetImeKeyModifiers() {
    return ((GetKeyState(VK_SHIFT) & 0x8000) ? MOD_SHIFT : 0) |
        ((GetKeyState(VK_CONTROL) & 0x8000) ? MOD_CONTROL : 0) |
        ((GetKeyState(VK_MENU) & 0x8000) ? MOD_ALT : 0) |
        (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ? MOD_WIN : 0);
}

UINT NormalizeImeShortcut(UINT key, UINT modifiers, bool hasInput) {
    if (modifiers & MOD_WIN) return 0;
    if (modifiers & MOD_ALT) return modifiers == MOD_ALT && key == VK_OEM_3 ? VK_KANJI : 0;
    if (!(modifiers & MOD_CONTROL)) {
        if ((modifiers & MOD_SHIFT) && key >= VK_F6 && key <= VK_F10) return 0;
        return key;
    }
    if (modifiers == MOD_CONTROL && key == VK_CAPITAL) return VK_DBE_HIRAGANA;
    if (!hasInput) return 0;
    if (key == VK_SPACE || key == VK_TAB) return key;
    if (modifiers & MOD_SHIFT) return 0;
    switch (key) {
    case 'U': return VK_F6;
    case 'I': return VK_F7;
    case 'O': return VK_F8;
    case 'P': return VK_F9;
    case 'T': return VK_F10;
    default: return 0;
    }
}

size_t MoveTextOffset(const std::wstring& text, size_t offset, int direction) {
    offset = (std::min)(offset, text.size());
    if (direction < 0 && offset > 0) {
        --offset;
        if (offset > 0 && text[offset] >= 0xDC00 && text[offset] <= 0xDFFF &&
            text[offset - 1] >= 0xD800 && text[offset - 1] <= 0xDBFF) --offset;
    } else if (direction > 0 && offset < text.size()) {
        const wchar_t first = text[offset++];
        if (first >= 0xD800 && first <= 0xDBFF && offset < text.size() &&
            text[offset] >= 0xDC00 && text[offset] <= 0xDFFF) ++offset;
    }
    return offset;
}

int CandidatePageStart(int selection) { return (std::max)(0, selection) / 9 * 9; }

int CandidatePageSelection(int selection, int count, int direction) {
    if (count <= 0) return 0;
    const int pages = (count + 8) / 9;
    const int page = ((selection / 9 + direction) % pages + pages) % pages;
    return (std::min)(page * 9 + selection % 9, count - 1);
}

std::wstring ConvertCharacterForm(const std::wstring& text, UINT functionKey, int letterCase) {
    if (text.empty()) return {};
    DWORD flags = 0;
    switch (functionKey) {
    case VK_F6: flags = LCMAP_HIRAGANA | LCMAP_FULLWIDTH; break;
    case VK_F7: flags = LCMAP_KATAKANA | LCMAP_FULLWIDTH; break;
    case VK_F8: flags = LCMAP_KATAKANA | LCMAP_HALFWIDTH; break;
    case VK_F9: flags = LCMAP_FULLWIDTH; break;
    case VK_F10: flags = LCMAP_HALFWIDTH; break;
    default: return text;
    }
    std::wstring source = text;
    if (functionKey == VK_F9 || functionKey == VK_F10) {
        if (letterCase > 0) {
            bool wordStart = true;
            for (auto& ch : source) {
                const bool letter = iswalpha(ch) != 0;
                ch = letterCase == 1 || (letterCase == 2 && wordStart) ? towupper(ch) : towlower(ch);
                wordStart = !letter;
            }
        }
    }
    const int size = LCMapStringEx(L"ja-JP", flags, source.data(), static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr, 0);
    if (size <= 0) return text;
    std::wstring result(size, L'\0');
    if (!LCMapStringEx(L"ja-JP", flags, source.data(), static_cast<int>(source.size()), result.data(), size, nullptr, nullptr, 0)) return text;
    return result;
}

namespace {

std::mutex g_debugLogMutex;
std::filesystem::path g_debugLogPath;
bool g_debugLogFileEnabled = false;
std::size_t g_debugLogMaxBytes = 0;

std::wstring FormatLocalTimestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[40]{};
    swprintf(buffer, 40, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

void RotateDebugLogIfNeeded(const std::filesystem::path& path,
                            std::size_t maxBytes) {
    if (path.empty() || maxBytes == 0) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size <= maxBytes) {
        return;
    }
    std::filesystem::path rotated = path;
    rotated += L".1";
    std::filesystem::remove(rotated, ec);
    ec.clear();
    std::filesystem::rename(path, rotated, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
    }
}

void AppendDebugLogFile(const std::wstring& line) {
    std::lock_guard<std::mutex> lock(g_debugLogMutex);
    if (!g_debugLogFileEnabled || g_debugLogPath.empty()) {
        return;
    }
    try {
        std::error_code ec;
        const auto parent = g_debugLogPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        RotateDebugLogIfNeeded(g_debugLogPath, g_debugLogMaxBytes);
        std::ofstream out(g_debugLogPath, std::ios::binary | std::ios::app);
        if (!out) {
            return;
        }
        std::wstring fullLine = FormatLocalTimestamp();
        fullLine += L" pid=";
        fullLine += std::to_wstring(GetCurrentProcessId());
        fullLine += L" tid=";
        fullLine += std::to_wstring(GetCurrentThreadId());
        fullLine += L" ";
        fullLine += line;
        if (fullLine.empty() || fullLine.back() != L'\n') {
            fullLine += L'\n';
        }
        out << WideToUtf8(fullLine);
    } catch (...) {
    }
}

}  // namespace

void ConfigureDebugLogFile(const std::filesystem::path& path,
                           bool enabled,
                           std::size_t maxBytes) {
    std::lock_guard<std::mutex> lock(g_debugLogMutex);
    g_debugLogPath = path;
    g_debugLogFileEnabled = enabled && !path.empty();
    g_debugLogMaxBytes = maxBytes;
}

void DebugLog(const wchar_t* msg, HRESULT hr) {
    if (!msg) {
        return;
    }
    wchar_t buffer[256];
    if (hr == S_OK) {
        swprintf(buffer, 256, L"[TSF-Debug] %s\n", msg);
    }
    else {
        swprintf(buffer, 256, L"[TSF-Debug] %s (HRESULT=0x%08X)\n", msg, hr);
    }
    OutputDebugStringW(buffer);
    AppendDebugLogFile(buffer);
}

std::string WideToUtf8(const std::wstring& input) {
    if (input.empty()) return {};
    int required = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string output(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), output.data(), required, nullptr, nullptr);
    return output;
}

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) return {};
    int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), static_cast<int>(input.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring output(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), static_cast<int>(input.size()), output.data(), required);
    return output;
}

static void AppendUtf8Codepoint(uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string EscapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char ch : input) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                char buf[7];
                sprintf_s(buf, "\\u%04X", static_cast<unsigned char>(ch));
                out += buf;
            }
            else {
                out.push_back(ch);
            }
            break;
        }
    }
    return out;
}

bool ExtractJsonString(const std::string& json, const char* fieldName, std::string& out) {
    if (!fieldName) return false;
    std::string key = "\"";
    key += fieldName;
    key += "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + key.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    std::string value;
    value.reserve(64);
    bool escape = false;
    uint32_t pendingHighSurrogate = 0;
    while (pos < json.size()) {
        char ch = json[pos++];
        if (escape) {
            if (ch == '"') {
                value.push_back('"');
            }
            else if (ch == '\\') {
                value.push_back('\\');
            }
            else if (ch == '/') {
                value.push_back('/');
            }
            else if (ch == 'b') {
                value.push_back(static_cast<char>(0x08));
            }
            else if (ch == 'f') {
                value.push_back(static_cast<char>(0x0C));
            }
            else if (ch == 'n') {
                value.push_back(static_cast<char>(0x0A));
            }
            else if (ch == 'r') {
                value.push_back(static_cast<char>(0x0D));
            }
            else if (ch == 't') {
                value.push_back(static_cast<char>(0x09));
            }
            else if (ch == 'u') {
                if (pos + 4 > json.size()) return false;
                uint32_t code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = json[pos++];
                    code <<= 4;
                    if (hex >= '0' && hex <= '9') {
                        code |= static_cast<uint32_t>(hex - '0');
                    }
                    else if (hex >= 'A' && hex <= 'F') {
                        code |= static_cast<uint32_t>(hex - 'A' + 10);
                    }
                    else if (hex >= 'a' && hex <= 'f') {
                        code |= static_cast<uint32_t>(hex - 'a' + 10);
                    }
                    else {
                        return false;
                    }
                }
                if (code >= 0xD800 && code <= 0xDBFF) {
                    pendingHighSurrogate = code;
                }
                else if (code >= 0xDC00 && code <= 0xDFFF && pendingHighSurrogate) {
                    const uint32_t full = 0x10000u + ((pendingHighSurrogate - 0xD800u) << 10) + (code - 0xDC00u);
                    AppendUtf8Codepoint(full, value);
                    pendingHighSurrogate = 0;
                }
                else {
                    AppendUtf8Codepoint(code, value);
                    pendingHighSurrogate = 0;
                }
            }
            else {
                value.push_back(ch);
            }
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (ch == '"') {
            out = std::move(value);
            return true;
        }
        value.push_back(ch);
    }
    return false;
}

std::wstring TrimWhitespace(const std::wstring& input) {
    size_t start = 0;
    while (start < input.size() && iswspace(input[start])) ++start;
    size_t end = input.size();
    while (end > start && iswspace(input[end - 1])) --end;
    return input.substr(start, end - start);
}

std::wstring FormatWinHttpError(DWORD errorCode) {
    if (!errorCode) return {};
    LPWSTR msg = nullptr;
    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring result;
    if (len && msg) {
        result.assign(msg, len);
        LocalFree(msg);
    }
    else {
        result = L"WinHTTP error code: ";
        result += std::to_wstring(static_cast<unsigned long long>(errorCode));
    }
    return TrimWhitespace(result);
}

KanaModeCommand ClassifyKanaKey(UINT vkey, UINT toggleVk) {
    if (vkey == toggleVk || vkey == VK_KANA || vkey == VK_KANJI) {
        return KanaModeCommand::Toggle;
    }
    switch (vkey) {
    case VK_DBE_HIRAGANA:
    case VK_DBE_KATAKANA:
        return KanaModeCommand::ForceOn;
    case VK_DBE_ALPHANUMERIC:
    case VK_DBE_SBCSCHAR:
    case VK_DBE_ROMAN:
        return KanaModeCommand::ForceOff;
    case VK_DBE_DBCSCHAR:
        return (toggleVk == VK_DBE_DBCSCHAR) ? KanaModeCommand::Toggle : KanaModeCommand::ForceOn;
    default:
        return KanaModeCommand::None;
    }
}

KanaModeCommand CommandFromPreservedGuid(REFGUID guid, UINT toggleVk) {
    // Preserved key GUIDs mirror those registered by the text service.
    if (IsEqualGUID(guid, GUID_Preserved_Toggle) || IsEqualGUID(guid, GUID_Preserved_KANA) || IsEqualGUID(guid, GUID_Preserved_KANJI)) {
        return KanaModeCommand::Toggle;
    }
    if (IsEqualGUID(guid, GUID_Preserved_DBE_HIRAGANA) || IsEqualGUID(guid, GUID_Preserved_DBE_KATAKANA)) {
        return KanaModeCommand::ForceOn;
    }
    if (IsEqualGUID(guid, GUID_Preserved_DBE_DBCSCHAR)) {
        return (toggleVk == VK_DBE_DBCSCHAR) ? KanaModeCommand::Toggle : KanaModeCommand::ForceOn;
    }
    if (IsEqualGUID(guid, GUID_Preserved_DBE_ALPHANUMERIC) || IsEqualGUID(guid, GUID_Preserved_DBE_SBCSCHAR) || IsEqualGUID(guid, GUID_Preserved_DBE_ROMAN)) {
        return KanaModeCommand::ForceOff;
    }
    return KanaModeCommand::None;
}

const wchar_t* MapFullWidthSymbol(UINT vkey, bool shiftPressed) {
    struct Entry { UINT vkey; const wchar_t* noShift; const wchar_t* withShift; };
    static const std::array<Entry, 22> entries = {
        // Mappings are aligned to common JP keyboard VK captures.
        Entry{ '1',           L"\uFF11", L"\uFF01" }, // １ / ！
        Entry{ '2',           L"\uFF12", L"\uFF02" }, // ２ / ＂
        Entry{ '3',           L"\uFF13", L"\uFF03" }, // ３ / ＃
        Entry{ '4',           L"\uFF14", L"\uFF04" }, // ４ / ＄
        Entry{ '5',           L"\uFF15", L"\uFF05" }, // ５ / ％
        Entry{ '6',           L"\uFF16", L"\uFF06" }, // ６ / ＆
        Entry{ '7',           L"\uFF17", L"\uFF07" }, // ７ / ＇
        Entry{ '8',           L"\uFF18", L"\uFF08" }, // ８ / （
        Entry{ '9',           L"\uFF19", L"\uFF09" }, // ９ / ）
        Entry{ '0',           L"\uFF10", L"\uFF10" }, // ０ / ０ (JISではShift+0が0になる配列もある)
        Entry{ VK_OEM_COMMA,  L"\u3001", L"\uFF1C" }, // 、 / ＜
        Entry{ VK_OEM_PERIOD, L"\u3002", L"\uFF1E" }, // 。 / ＞
        Entry{ VK_OEM_2,      L"\u30FB", L"\uFF1F" }, // ・ / ？
        Entry{ VK_OEM_PLUS,   L"\uFF1B", L"\uFF0B" }, // ； / ＋
        Entry{ VK_OEM_1,      L"\uFF1A", L"\uFF0A" }, // ： / ＊
        Entry{ VK_OEM_4,      L"\uFF3B", L"\uFF5B" }, // ［ / ｛
        Entry{ VK_OEM_6,      L"\uFF3D", L"\uFF5D" }, // ］ / ｝
        Entry{ VK_OEM_MINUS,  L"\u30FC", L"\uFF1D" }, // ー / ＝
        Entry{ VK_OEM_5,      L"\uFFE5", L"\uFF5C" }, // ￥ / ｜
        Entry{ VK_OEM_102,    L"\uFFE5", L"\uFF3F" }, // ￥ / ＿
        Entry{ VK_OEM_3,      L"\uFF20", L"\uFF40" }, // ＠ / ｀
        Entry{ VK_OEM_7,      L"\uFF3E", L"\uFF5E" }  // ＾ / ～
    };
    for (const auto& entry : entries) {
        if (entry.vkey == vkey) {
            return shiftPressed ? entry.withShift : entry.noShift;
        }
    }
    return nullptr;
}

const wchar_t* MapHalfWidthSymbol(UINT vkey, bool shiftPressed) {
    struct Entry { UINT vkey; const wchar_t* noShift; const wchar_t* withShift; };
    static const std::array<Entry, 22> entries = {
        Entry{ '1',           L"1", L"!" },
        Entry{ '2',           L"2", L"\"" },
        Entry{ '3',           L"3", L"#" },
        Entry{ '4',           L"4", L"$" },
        Entry{ '5',           L"5", L"%" },
        Entry{ '6',           L"6", L"&" },
        Entry{ '7',           L"7", L"'" },
        Entry{ '8',           L"8", L"(" },
        Entry{ '9',           L"9", L")" },
        Entry{ '0',           L"0", L"0" },
        Entry{ VK_OEM_COMMA,  L",", L"<" },
        Entry{ VK_OEM_PERIOD, L".", L">" },
        Entry{ VK_OEM_2,      L"/", L"?" },
        Entry{ VK_OEM_PLUS,   L";", L"+" },
        Entry{ VK_OEM_1,      L":", L"*" },
        Entry{ VK_OEM_4,      L"[", L"{" },
        Entry{ VK_OEM_6,      L"]", L"}" },
        Entry{ VK_OEM_MINUS,  L"-", L"=" },
        Entry{ VK_OEM_5,      L"\\", L"|" },
        Entry{ VK_OEM_102,    L"\\", L"_" },
        Entry{ VK_OEM_3,      L"@", L"`" },
        Entry{ VK_OEM_7,      L"^", L"~" }
    };
    for (const auto& entry : entries) {
        if (entry.vkey == vkey) {
            return shiftPressed ? entry.withShift : entry.noShift;
        }
    }
    return nullptr;
}



namespace {
static bool IsVowel(wchar_t c) { return c==L'a'||c==L'i'||c==L'u'||c==L'e'||c==L'o'; }
static bool IsConsonant(wchar_t c) { return (c>=L'a'&&c<=L'z') && !IsVowel(c); }
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

} // namespace

std::wstring_view PreviewRomajiBuffer(std::wstring_view buffer) {
    // Keep both keystrokes available so the second n can start a syllable such as ni.
    return buffer == L"nn" ? std::wstring_view(L"ん") : buffer;
}

std::wstring ConvertRomajiBuffer(std::wstring& buffer, bool finalize) {
    std::wstring out;
    auto emit = [&](const wchar_t* s) { out.append(s); };
    auto consume = [&](size_t n) { buffer.erase(0, n); };
    while (!buffer.empty()) {
        if (buffer.size() >= 2 && buffer[0] == buffer[1] && IsConsonant(buffer[0]) && buffer[0] != L'n') {
            emit(L"っ"); consume(1); continue;
        }
        if (buffer[0] == L'n') {
            if (buffer.size() >= 2) {
                wchar_t n2 = buffer[1];
                if (n2 == L'\'') { emit(L"ん"); consume(2); continue; }
                if (n2 == L'y' && buffer.size() < 3) { break; }
                if (n2 == L'n') {
                    if (buffer.size() == 2 && !finalize) break;
                    if (buffer.size() >= 3) {
                        wchar_t n3 = buffer[2];
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
        if (buffer.size() >= 4) {
            for (const auto& m : map4) { if (buffer.rfind(m.roma, 0) == 0) { emit(m.hira); consume(4); matched = true; break; } }
            if (matched) continue;
        }
        if (buffer.size() >= 3) {
            for (const auto& m : map3) { if (buffer.rfind(m.roma, 0) == 0) { emit(m.hira); consume(3); matched = true; break; } }
            if (matched) continue;
        }
        if (buffer.size() >= 2) {
            for (const auto& m : map2) { if (buffer.rfind(m.roma, 0) == 0) { emit(m.hira); consume(2); matched = true; break; } }
            if (matched) continue;
        }
        for (const auto& m : map1) { if (buffer.rfind(m.roma, 0) == 0) { emit(m.hira); consume(1); matched = true; break; } }
        if (matched) continue;
        wchar_t c0 = buffer[0];
        if (!(c0 >= L'a' && c0 <= L'z')) { wchar_t s[2] = { c0, 0 }; emit(s); consume(1); continue; }
        break;
    }
    if (finalize) {
        out += buffer;
        buffer.clear();
    }
    return out;
}

std::wstring RomajiFromKana(const std::wstring& text) {
    const std::wstring hira = ConvertCharacterForm(text, VK_F6);
    std::wstring result;
    for (size_t pos = 0; pos < hira.size();) {
        const Map* best = nullptr;
        size_t length = 0;
        auto find = [&](const auto& entries) {
            for (const auto& entry : entries) {
                const size_t size = wcslen(entry.hira);
                if (size > length && hira.compare(pos, size, entry.hira) == 0) {
                    best = &entry;
                    length = size;
                }
            }
        };
        find(map3); find(map2); find(map1); find(map4);
        if (best) { result += best->roma; pos += length; }
        else {
            const wchar_t ch = hira[pos++];
            result += ch == L'\u3000' ? L' ' : ch >= 0xFF01 && ch <= 0xFF5E ? static_cast<wchar_t>(ch - 0xFEE0) : ch;
        }
    }
    return result;
}
