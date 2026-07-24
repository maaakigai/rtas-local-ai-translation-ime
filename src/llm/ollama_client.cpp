#include "ollama_client.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <utility>
#include <windows.h>
#include <winhttp.h>

#include "../../Ime3/rtas_utils.h"

#pragma comment(lib, "Winhttp.lib")

namespace llm {
namespace {

std::wstring ReadEnvString(const wchar_t* name) {
    if (!name) return {};
    wchar_t buffer[256];
    DWORD written = GetEnvironmentVariableW(name, buffer, ARRAYSIZE(buffer));
    if (written == 0 || written >= ARRAYSIZE(buffer)) {
        if (written > ARRAYSIZE(buffer)) {
            std::wstring large;
            large.resize(written);
            DWORD actual = GetEnvironmentVariableW(name, large.data(), static_cast<DWORD>(large.size()));
            if (actual > 0 && actual < large.size()) {
                large.resize(actual);
                return large;
            }
        }
        return {};
    }
    return std::wstring(buffer, written);
}

struct WinHttpHandle {
    HINTERNET handle{ nullptr };
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET h) : handle(h) {}
    ~WinHttpHandle() {
        if (handle) {
            WinHttpCloseHandle(handle);
        }
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle(WinHttpHandle&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }
    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            if (handle) WinHttpCloseHandle(handle);
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    explicit operator bool() const noexcept { return handle != nullptr; }
};

std::string BuildStopArrayJson(const std::vector<std::wstring>& stop) {
    if (stop.empty()) return {};
    std::string json = ",\"stop\":[";
    bool first = true;
    for (const auto& seq : stop) {
        if (!first) json += ",";
        first = false;
        json += "\"";
        json += EscapeJsonString(WideToUtf8(seq));
        json += "\"";
    }
    json += "]";
    return json;
}

bool IsJsonIntegerLiteral(const std::string& value) {
    if (value.empty()) return false;
    size_t index = 0;
    if (value[index] == '-') {
        ++index;
    }
    if (index >= value.size()) return false;
    for (; index < value.size(); ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    return true;
}

std::string BuildKeepAliveJsonValue(const std::wstring& keepAlive) {
    const std::wstring trimmedWide = TrimWhitespace(keepAlive);
    if (trimmedWide.empty()) {
        return {};
    }
    const std::string utf8 = WideToUtf8(trimmedWide);
    if (IsJsonIntegerLiteral(utf8)) {
        return utf8;
    }
    return "\"" + EscapeJsonString(utf8) + "\"";
}

void AppendKeepAliveJson(std::string& body, const std::wstring& keepAlive) {
    const std::string value = BuildKeepAliveJsonValue(keepAlive);
    if (value.empty()) {
        return;
    }
    body += ",\"keep_alive\":";
    body += value;
}

bool ExtractJsonInt64(const std::string& json, const char* fieldName, uint64_t& out) {
    if (!fieldName || !*fieldName) return false;
    std::string needle = "\"";
    needle += fieldName;
    needle += "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') {
        return false;
    }
    uint64_t value = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + static_cast<uint64_t>(json[pos] - '0');
        ++pos;
    }
    out = value;
    return true;
}

std::wstring FormatDurationMs(uint64_t nanos) {
    std::wstringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(2);
    ss << (static_cast<double>(nanos) / 1000000.0) << L"ms";
    return ss.str();
}

std::wstring FormatHttpStatus(unsigned int status) {
    std::wstringstream ss;
    ss << L"Ollama returned HTTP " << status;
    return ss.str();
}

}  // namespace

OllamaClient::OllamaClient(Config config) {
    SetConfig(config);
}

void OllamaClient::SetConfig(const Config& config) {
    m_config = config;
    if (m_config.port == 0) {
        m_config.port = 11434;
    }
}

OllamaResponse OllamaClient::Generate(const OllamaRequest& request,
    const CancelFlag& cancelFlag) const {
    OllamaResponse last;
    if (request.prompt.empty()) {
        last.error = L"OllamaClient::Generate called with an empty prompt.";
        return last;
    }

    const std::wstring model = ResolveModel(request.modelOverride);
    if (model.empty()) {
        last.error = L"No model specified for Ollama request.";
        return last;
    }

    const std::wstring host = ResolveHost();
    const uint16_t port = ResolvePort();
    const bool useTls = m_config.useTls;

    const std::string modelUtf8 = WideToUtf8(model);
    const std::string promptUtf8 = WideToUtf8(request.prompt);
    const std::string systemUtf8 = WideToUtf8(request.systemPrompt);
    const std::wstring keepAlive =
        TrimWhitespace(request.keepAlive).empty() ? m_config.keepAlive : request.keepAlive;

    std::ostringstream temperature;
    temperature.setf(std::ios::fixed);
    temperature.precision(2);
    temperature << (request.temperature < 0.0 ? 0.0 : request.temperature);

    std::string body = "{";
    body += "\"model\":\"" + EscapeJsonString(modelUtf8) + "\"";
    if (!systemUtf8.empty()) {
        body += ",\"system\":\"" + EscapeJsonString(systemUtf8) + "\"";
    }
    body += ",\"prompt\":\"" + EscapeJsonString(promptUtf8) + "\"";
    body += ",\"stream\":";
    body += request.stream ? "true" : "false";
    body += ",\"options\":{\"temperature\":";
    body += temperature.str();
    body += "}";
    AppendKeepAliveJson(body, keepAlive);
    body += BuildStopArrayJson(request.stop);
    body += "}";

    const wchar_t* headers = L"Content-Type: application/json\r\n";

    const uint32_t maxAttempts = (m_config.maxRetries == 0) ? 1 : (m_config.maxRetries + 1);
    for (uint32_t attempt = 0; attempt < maxAttempts; ++attempt) {
        if (IsCancelled(cancelFlag)) {
            last.cancelled = true;
            return last;
        }

        last.success = false;
        last.cancelled = false;
        last.error.clear();
        last.rawPayload.clear();
        last.httpStatus = 0;
        last.totalDurationNs = 0;
        last.loadDurationNs = 0;

        WinHttpHandle session(WinHttpOpen(L"RTAS/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
        if (!session) {
            last.error = FormatWinHttpError(GetLastError());
            continue;
        }

        WinHttpSetTimeouts(session.handle,
            static_cast<int>(m_config.connectTimeoutMs),
            static_cast<int>(m_config.sendTimeoutMs),
            static_cast<int>(m_config.receiveTimeoutMs),
            static_cast<int>(m_config.overallTimeoutMs));

        if (IsCancelled(cancelFlag)) {
            last.cancelled = true;
            return last;
        }

        WinHttpHandle connection(WinHttpConnect(session.handle,
            host.c_str(),
            static_cast<INTERNET_PORT>(port),
            0));
        if (!connection) {
            last.error = FormatWinHttpError(GetLastError());
            continue;
        }

        DWORD requestFlags = useTls ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle requestHandle(WinHttpOpenRequest(connection.handle,
            L"POST",
            L"/api/generate",
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            requestFlags));
        if (!requestHandle) {
            last.error = FormatWinHttpError(GetLastError());
            continue;
        }

        const auto started = std::chrono::steady_clock::now();
        if (!WinHttpSendRequest(requestHandle.handle,
            headers,
            DWORD(-1),
            (LPVOID)body.data(),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0)) {
            last.error = FormatWinHttpError(GetLastError());
            continue;
        }

        if (IsCancelled(cancelFlag)) {
            last.cancelled = true;
            return last;
        }

        if (!WinHttpReceiveResponse(requestHandle.handle, nullptr)) {
            last.error = FormatWinHttpError(GetLastError());
            continue;
        }

        if (IsCancelled(cancelFlag)) {
            last.cancelled = true;
            return last;
        }

        std::string responseUtf8;
        DWORD statusCode = 0;
        DWORD statusLen = sizeof(statusCode);
        if (WinHttpQueryHeaders(requestHandle.handle,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusLen,
            WINHTTP_NO_HEADER_INDEX)) {
            last.httpStatus = static_cast<unsigned int>(statusCode);
        } else {
            last.httpStatus = 0;
        }

        for (;;) {
            if (IsCancelled(cancelFlag)) {
                last.cancelled = true;
                return last;
            }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(requestHandle.handle, &available)) {
                last.error = FormatWinHttpError(GetLastError());
                break;
            }
            if (available == 0) break;
            std::string chunk;
            chunk.resize(available);
            DWORD read = 0;
            if (!WinHttpReadData(requestHandle.handle, chunk.data(), available, &read)) {
                last.error = FormatWinHttpError(GetLastError());
                break;
            }
            chunk.resize(read);
            responseUtf8.append(chunk);
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();

        if (last.cancelled) {
            return last;
        }

        last.rawPayload = Utf8ToWide(responseUtf8);
        ExtractJsonInt64(responseUtf8, "total_duration", last.totalDurationNs);
        ExtractJsonInt64(responseUtf8, "load_duration", last.loadDurationNs);
        if (m_config.logTimings) {
            std::wstring message = L"Ollama generate model=";
            message += model;
            message += L" wall=";
            message += std::to_wstring(static_cast<long long>(elapsed));
            message += L"ms";
            if (last.loadDurationNs > 0) {
                message += L" load_duration=";
                message += FormatDurationMs(last.loadDurationNs);
            }
            if (last.totalDurationNs > 0) {
                message += L" total_duration=";
                message += FormatDurationMs(last.totalDurationNs);
            }
            DebugLog(message.c_str());
        }
        if (!last.error.empty()) {
            // error occurred while reading, retry if possible
            if (attempt + 1 < maxAttempts) {
                continue;
            }
            return last;
        }

        if (last.httpStatus >= 400) {
            last.success = false;
            last.error = FormatHttpStatus(last.httpStatus);
            if (attempt + 1 < maxAttempts) {
                continue;
            }
            return last;
        }

        std::string textField;
        if (!ExtractJsonString(responseUtf8, "response", textField)) {
            last.success = false;
            last.error = L"Failed to parse Ollama response.";
            if (attempt + 1 < maxAttempts) {
                continue;
            }
            return last;
        }

        last.text = TrimWhitespace(Utf8ToWide(textField));
        last.modelUsed = model;
        if (last.text.empty()) {
            last.success = false;
            last.error = L"Ollama returned an empty response.";
            if (attempt + 1 < maxAttempts) {
                continue;
            }
            return last;
        }

        last.success = true;
        last.error.clear();
        return last;
    }

    return last;
}

std::wstring OllamaClient::ResolveModel(const std::wstring& overrideModel) const {
    if (!overrideModel.empty()) {
        return overrideModel;
    }
    if (!m_config.defaultModel.empty()) {
        return m_config.defaultModel;
    }

    std::wstring envModel = ReadEnvString(L"IME3_OLLAMA_MODEL");
    if (!envModel.empty()) {
        return envModel;
    }

    return L"llama3.1";
}

std::wstring OllamaClient::ResolveHost() const {
    std::wstring envHost = ReadEnvString(L"IME3_OLLAMA_HOST");
    if (!envHost.empty()) {
        return envHost;
    }
    if (!m_config.host.empty()) {
        return m_config.host;
    }
    return L"127.0.0.1";
}

uint16_t OllamaClient::ResolvePort() const {
    std::wstring envPort = ReadEnvString(L"IME3_OLLAMA_PORT");
    if (!envPort.empty()) {
        int value = _wtoi(envPort.c_str());
        if (value > 0 && value <= 65535) {
            return static_cast<uint16_t>(value);
        }
    }
    return m_config.port == 0 ? 11434 : m_config.port;
}

bool OllamaClient::IsCancelled(const CancelFlag& flag) const {
    return flag && flag->load(std::memory_order_relaxed);
}

}  // namespace llm
