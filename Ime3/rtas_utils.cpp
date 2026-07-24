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


