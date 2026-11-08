#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
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
std::wstring FormatWinHttpError(DWORD errorCode);
KanaModeCommand ClassifyKanaKey(UINT vkey, UINT toggleVk);
KanaModeCommand CommandFromPreservedGuid(REFGUID guid, UINT toggleVk);
const wchar_t* MapFullWidthSymbol(UINT vkey, bool shiftPressed);
const wchar_t* MapHalfWidthSymbol(UINT vkey, bool shiftPressed);
UINT GetImeKeyModifiers();
// Returns 0 for an application shortcut; normalizes supported IME aliases.
UINT NormalizeImeShortcut(UINT key, UINT modifiers, bool hasInput);
std::wstring ConvertRomajiBuffer(std::wstring& buffer, bool finalize);
std::wstring RomajiFromKana(const std::wstring& text);
std::wstring ConvertCharacterForm(const std::wstring& text, UINT functionKey, int letterCase = 0);
size_t MoveTextOffset(const std::wstring& text, size_t offset, int direction);
int CandidatePageStart(int selection);
int CandidatePageSelection(int selection, int count, int direction);

