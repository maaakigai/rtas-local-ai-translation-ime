#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include "rtas_virtual_keys.h"

enum class KanaModeCommand {
    None,
    Toggle,
    ForceOn,
    ForceOff
};

void ConfigureDebugLogFile(const std::filesystem::path& path,
                           bool enabled,
                           std::size_t maxBytes = 1024 * 1024);
void DebugLog(const wchar_t* msg, HRESULT hr = S_OK);
std::string WideToUtf8(const std::wstring& input);
std::wstring Utf8ToWide(const std::string& input);
std::string EscapeJsonString(const std::string& input);
bool ExtractJsonString(const std::string& json, const char* fieldName, std::string& out);
std::wstring TrimWhitespace(const std::wstring& input);

class WarframeDictionary {
public:
    // An empty path enables the curated terms without a downloaded snapshot.
    void Load(const std::filesystem::path& path);
    std::span<const std::wstring> Find(std::wstring_view reading) const;
    std::wstring Normalize(std::wstring_view text) const;
    std::wstring RestoreEnglishSpelling(std::wstring_view source, std::wstring_view translation) const;

private:
    struct Entry {
        std::vector<std::wstring> names;
        bool translate = false;
        bool english = false;
    };

    void Add(std::wstring_view alias, std::wstring_view english, bool translate);
    std::wstring Rewrite(std::wstring_view text, std::wstring_view foldedSource) const;

    std::map<std::wstring, Entry, std::less<>> aliases_;
    std::map<wchar_t, std::vector<size_t>> lengths_;
};
std::wstring FormatWinHttpError(DWORD errorCode);
KanaModeCommand ClassifyKanaKey(UINT vkey, UINT toggleVk);
KanaModeCommand CommandFromPreservedGuid(REFGUID guid, UINT toggleVk);
const wchar_t* MapFullWidthSymbol(UINT vkey, bool shiftPressed);
const wchar_t* MapHalfWidthSymbol(UINT vkey, bool shiftPressed);
UINT GetImeKeyModifiers();
// Returns 0 for an application shortcut; normalizes supported IME aliases.
UINT NormalizeImeShortcut(UINT key, UINT modifiers, bool hasInput);
std::wstring_view PreviewRomajiBuffer(std::wstring_view buffer);
std::wstring ConvertRomajiBuffer(std::wstring& buffer, bool finalize);
std::wstring RomajiFromKana(const std::wstring& text);
std::wstring ConvertCharacterForm(const std::wstring& text, UINT functionKey, int letterCase = 0);
size_t MoveTextOffset(const std::wstring& text, size_t offset, int direction);
int CandidatePageStart(int selection);
int CandidatePageSelection(int selection, int count, int direction);

