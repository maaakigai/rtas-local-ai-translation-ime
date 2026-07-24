#include "rtas_translation.h"

#include <chrono>
#include <cwctype>
#include <sstream>
#include <string>
#include <thread>
#include <windows.h>
#include <winhttp.h>

#include "rtas_utils.h"

#pragma comment(lib, "Winhttp.lib")

namespace {

std::wstring ResolveOllamaModel(const TranslationLlmSettings& llmSettings) {
    wchar_t buffer[128];
    DWORD len = GetEnvironmentVariableW(L"IME3_OLLAMA_MODEL", buffer, ARRAYSIZE(buffer));
    if (len > 0 && len < ARRAYSIZE(buffer)) {
        return std::wstring(buffer, len);
    }
    const std::wstring configured = TrimWhitespace(llmSettings.model);
    if (!configured.empty()) {
        return configured;
    }
    return std::wstring(L"llama3.1");
}

std::wstring ResolveOllamaHost(const TranslationLlmSettings& llmSettings) {
    wchar_t buffer[256];
    DWORD len = GetEnvironmentVariableW(L"IME3_OLLAMA_HOST", buffer, ARRAYSIZE(buffer));
    if (len > 0 && len < ARRAYSIZE(buffer)) {
        return TrimWhitespace(std::wstring(buffer, len));
    }
    const std::wstring configured = TrimWhitespace(llmSettings.host);
    if (!configured.empty()) {
        return configured;
    }
    return std::wstring(L"127.0.0.1");
}

INTERNET_PORT ResolveOllamaPort(const TranslationLlmSettings& llmSettings) {
    wchar_t buffer[32];
    DWORD len = GetEnvironmentVariableW(L"IME3_OLLAMA_PORT", buffer, ARRAYSIZE(buffer));
    if (len > 0 && len < ARRAYSIZE(buffer)) {
        int value = _wtoi(buffer);
        if (value > 0 && value <= 65535) {
            return static_cast<INTERNET_PORT>(value);
        }
    }
    if (llmSettings.port > 0) {
        return static_cast<INTERNET_PORT>(llmSettings.port);
    }
    return static_cast<INTERNET_PORT>(11434);
}

std::wstring ResolveOllamaPath(const TranslationLlmSettings& llmSettings) {
    const std::wstring configured = TrimWhitespace(llmSettings.path);
    if (configured.empty()) {
        return std::wstring(L"/api/generate");
    }
    if (configured.front() == L'/') {
        return configured;
    }
    return std::wstring(L"/") + configured;
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

bool ExtractJsonInt64(const std::string& json, const char* fieldName, long long& out) {
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
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') {
        negative = true;
        ++pos;
    }
    if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') {
        return false;
    }
    long long value = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    out = negative ? -value : value;
    return true;
}

std::wstring FormatDurationMs(long long nanos) {
    std::wstringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(2);
    ss << (static_cast<double>(nanos) / 1000000.0) << L"ms";
    return ss.str();
}

void LogOllamaTiming(const wchar_t* operation,
                     const std::wstring& model,
                     const std::string& response,
                     long long wallMs,
                     bool enabled) {
    if (!enabled) {
        return;
    }
    long long loadDuration = -1;
    long long totalDuration = -1;
    ExtractJsonInt64(response, "load_duration", loadDuration);
    ExtractJsonInt64(response, "total_duration", totalDuration);

    std::wstring message = L"Ollama ";
    message += operation ? operation : L"request";
    message += L" model=";
    message += model;
    message += L" wall=";
    message += std::to_wstring(wallMs);
    message += L"ms";
    if (loadDuration >= 0) {
        message += L" load_duration=";
        message += FormatDurationMs(loadDuration);
    }
    if (totalDuration >= 0) {
        message += L" total_duration=";
        message += FormatDurationMs(totalDuration);
    }
    DebugLog(message.c_str());
}

struct WinHttpHandle {
    HINTERNET handle{ nullptr };
    ~WinHttpHandle() { if (handle) WinHttpCloseHandle(handle); }
};

void SetWinHttpError(std::wstring& error, const wchar_t* stage, DWORD err) {
    std::wstring message(stage ? stage : L"WinHTTP");
    message += L" failed: ";
    std::wstring detail = FormatWinHttpError(err);
    if (!detail.empty()) {
        message += detail;
    }
    else {
        message += L"0x";
        wchar_t buf[16];
        swprintf(buf, 16, L"%08X", err);
        message += buf;
    }
    error = std::move(message);
}

OllamaLifecycleResult ExecuteOllamaLifecycleRequest(
    const TranslationLlmSettings& llmSettings,
    const std::wstring& keepAlive,
    int timeoutMs,
    const wchar_t* operation,
    const AsyncWorkQueue::CancelFlag& cancelFlag) {
    OllamaLifecycleResult result;
    if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }

    const std::wstring model = TrimWhitespace(ResolveOllamaModel(llmSettings));
    result.model = model;
    if (model.empty()) {
        result.error = L"Ollama model name is empty.";
        return result;
    }

    const std::wstring host = ResolveOllamaHost(llmSettings);
    const INTERNET_PORT port = ResolveOllamaPort(llmSettings);
    const std::wstring path = ResolveOllamaPath(llmSettings);
    const DWORD requestFlags = llmSettings.useTls ? WINHTTP_FLAG_SECURE : 0;
    const int effectiveTimeout = timeoutMs > 0 ? timeoutMs : 15000;

    WinHttpHandle session;
    session.handle = WinHttpOpen(L"RTAS/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.handle) {
        SetWinHttpError(result.error, L"WinHttpOpen", GetLastError());
        return result;
    }
    WinHttpSetTimeouts(session.handle, 5000, 5000, effectiveTimeout, effectiveTimeout);

    WinHttpHandle connection;
    connection.handle = WinHttpConnect(session.handle, host.c_str(), port, 0);
    if (!connection.handle) {
        SetWinHttpError(result.error, L"WinHttpConnect", GetLastError());
        return result;
    }
    if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }

    WinHttpHandle request;
    request.handle = WinHttpOpenRequest(connection.handle, L"POST", path.c_str(),
                                        nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags);
    if (!request.handle) {
        SetWinHttpError(result.error, L"WinHttpOpenRequest", GetLastError());
        return result;
    }

    std::string body = "{";
    body += "\"model\":\"" + EscapeJsonString(WideToUtf8(model)) + "\"";
    AppendKeepAliveJson(body, keepAlive);
    body += "}";

    const auto started = std::chrono::steady_clock::now();
    const wchar_t* headers = L"Content-Type: application/json\r\n";
    if (!WinHttpSendRequest(request.handle, headers, static_cast<DWORD>(-1),
                            (LPVOID)body.data(), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0)) {
        SetWinHttpError(result.error, L"WinHttpSendRequest", GetLastError());
        return result;
    }
    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
        SetWinHttpError(result.error, L"WinHttpReceiveResponse", GetLastError());
        return result;
    }
    DWORD statusCode = 0;
    DWORD statusLen = sizeof(statusCode);
    if (WinHttpQueryHeaders(request.handle,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &statusCode,
                            &statusLen,
                            WINHTTP_NO_HEADER_INDEX) &&
        statusCode >= 400) {
        result.error = L"Ollama lifecycle request returned HTTP ";
        result.error += std::to_wstring(statusCode);
        return result;
    }

    std::string response;
    for (;;) {
        if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            return result;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available)) {
            SetWinHttpError(result.error, L"WinHttpQueryDataAvailable", GetLastError());
            return result;
        }
        if (available == 0) break;
        std::string chunk;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, chunk.data(), available, &read)) {
            SetWinHttpError(result.error, L"WinHttpReadData", GetLastError());
            return result;
        }
        chunk.resize(read);
        response.append(chunk);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    LogOllamaTiming(operation, model, response, static_cast<long long>(elapsed),
                    llmSettings.logTimings);

    result.rawPayload = Utf8ToWide(response);
    result.success = true;
    return result;
}

}  // namespace

AsyncWorkQueue::~AsyncWorkQueue() {
    Shutdown();
}

uint64_t AsyncWorkQueue::Enqueue(Job job) {
    if (!job) return 0;
    std::unique_lock<std::mutex> lock(m_mutex);
    EnsureWorkerLocked(lock);
    const uint64_t id = m_nextId.fetch_add(1, std::memory_order_relaxed) + 1;
    QueueItem item{};
    item.id = id;
    item.cancelFlag = std::make_shared<std::atomic_bool>(false);
    item.job = std::move(job);
    m_pending.emplace(id, item.cancelFlag);
    m_queue.push(std::move(item));
    lock.unlock();
    m_cv.notify_one();
    return id;
}

bool AsyncWorkQueue::Cancel(uint64_t id) {
    CancelFlag flag;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_pending.find(id);
        if (it == m_pending.end()) return false;
        flag = it->second;
    }
    flag->store(true, std::memory_order_relaxed);
    m_cv.notify_all();
    return true;
}

void AsyncWorkQueue::Shutdown() {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_started) {
        std::queue<QueueItem> empty;
        std::swap(m_queue, empty);
        m_pending.clear();
        m_stop = true;
        return;
    }
    m_stop = true;
    for (auto& entry : m_pending) {
        entry.second->store(true, std::memory_order_relaxed);
    }
    lock.unlock();
    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    lock.lock();
    m_started = false;
    std::queue<QueueItem> empty;
    std::swap(m_queue, empty);
    m_pending.clear();
    m_worker = std::thread();
}

void AsyncWorkQueue::EnsureWorkerLocked(std::unique_lock<std::mutex>& lock) {
    if (m_started) return;
    m_stop = false;
    m_started = true;
    std::thread worker([this]() { WorkerLoop(); });
    m_worker.swap(worker);
}

void AsyncWorkQueue::WorkerLoop() {
    for (;;) {
        QueueItem item;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty()) break;
            item = std::move(m_queue.front());
            m_queue.pop();
        }
        if (!item.cancelFlag->load(std::memory_order_relaxed)) {
            item.job(item.id, item.cancelFlag);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending.erase(item.id);
        }
    }
}

TranslationResult ExecuteTranslationJob(uint64_t id,
    const std::wstring& reading,
    const std::wstring& context,
    const AsyncWorkQueue::CancelFlag& cancelFlag,
    const TranslationLlmSettings& llmSettings) {
    TranslationResult result;
    result.requestId = id;
    result.source = reading;

    if (cancelFlag->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }

    std::wstring trimmedReading = TrimWhitespace(reading);
    if (trimmedReading.empty()) {
        result.error = L"No text available for translation.";
        return result;
    }
    result.source = trimmedReading;

    const std::wstring host = ResolveOllamaHost(llmSettings);
    const INTERNET_PORT port = ResolveOllamaPort(llmSettings);
    const std::wstring path = ResolveOllamaPath(llmSettings);
    const DWORD requestFlags = llmSettings.useTls ? WINHTTP_FLAG_SECURE : 0;
    const wchar_t* const kUserAgent = L"RTAS/1.0";

    const std::wstring model = TrimWhitespace(ResolveOllamaModel(llmSettings));
    if (model.empty()) {
        result.error = L"Ollama model name is empty.";
        return result;
    }

    WinHttpHandle session;
    session.handle = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.handle) {
        SetWinHttpError(result.error, L"WinHttpOpen", GetLastError());
        return result;
    }
    const int timeoutMs = llmSettings.timeoutMs > 0 ? llmSettings.timeoutMs : 15000;
    WinHttpSetTimeouts(session.handle, 5000, 5000, timeoutMs, timeoutMs);
    if (cancelFlag->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }

    WinHttpHandle connection;
    connection.handle = WinHttpConnect(session.handle, host.c_str(), port, 0);
    if (!connection.handle) {
        SetWinHttpError(result.error, L"WinHttpConnect", GetLastError());
        return result;
    }
    if (cancelFlag->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }

    WinHttpHandle request;
    request.handle = WinHttpOpenRequest(connection.handle, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags);
    if (!request.handle) {
        SetWinHttpError(result.error, L"WinHttpOpenRequest", GetLastError());
        return result;
    }

    const std::string modelUtf8 = WideToUtf8(model);
    std::string prompt = "Translate the following Japanese text into natural English. Reply with English only and avoid extra commentary.\n";
    prompt += WideToUtf8(trimmedReading);
    const std::string contextUtf8 = WideToUtf8(TrimWhitespace(context));
    if (!contextUtf8.empty()) {
        prompt += "\nContext: ";
        prompt += contextUtf8;
    }

    std::string body = "{";
    body += "\"model\":\"" + EscapeJsonString(modelUtf8) + "\"";
    body += ",\"prompt\":\"" + EscapeJsonString(prompt) + "\"";
    body += ",\"stream\":false";
    body += ",\"options\":{\"temperature\":0.2}";
    AppendKeepAliveJson(body, llmSettings.keepAlive);
    body += "}";

    if (cancelFlag->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    const auto started = std::chrono::steady_clock::now();
    if (!WinHttpSendRequest(request.handle, headers, static_cast<DWORD>(-1), (LPVOID)body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
        SetWinHttpError(result.error, L"WinHttpSendRequest", GetLastError());
        return result;
    }
    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
        SetWinHttpError(result.error, L"WinHttpReceiveResponse", GetLastError());
        return result;
    }

    std::string response;
    for (;;) {
        if (cancelFlag->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            return result;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available)) {
            SetWinHttpError(result.error, L"WinHttpQueryDataAvailable", GetLastError());
            return result;
        }
        if (available == 0) break;
        std::string chunk;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, chunk.data(), available, &read)) {
            SetWinHttpError(result.error, L"WinHttpReadData", GetLastError());
            return result;
        }
        chunk.resize(read);
        response.append(chunk);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    LogOllamaTiming(L"translation", model, response, static_cast<long long>(elapsed),
                    llmSettings.logTimings);

    if (cancelFlag->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }

    std::string responseField;
    if (!ExtractJsonString(response, "response", responseField)) {
        result.error = L"Failed to parse Ollama response.";
        return result;
    }

    std::wstring translation = TrimWhitespace(Utf8ToWide(responseField));
    if (translation.empty()) {
        result.error = L"Ollama returned an empty translation.";
        return result;
    }

    result.translation = std::move(translation);
    result.success = true;
    return result;
}

OllamaLifecycleResult WarmupOllamaModel(
    const TranslationLlmSettings& llmSettings,
    const AsyncWorkQueue::CancelFlag& cancelFlag) {
    const std::wstring keepAlive =
        TrimWhitespace(llmSettings.keepAlive).empty() ? L"-1" : llmSettings.keepAlive;
    return ExecuteOllamaLifecycleRequest(
        llmSettings, keepAlive, llmSettings.warmupTimeoutMs, L"warmup", cancelFlag);
}

OllamaLifecycleResult UnloadOllamaModel(
    const TranslationLlmSettings& llmSettings,
    const AsyncWorkQueue::CancelFlag& cancelFlag) {
    return ExecuteOllamaLifecycleRequest(
        llmSettings, L"0", llmSettings.timeoutMs, L"unload", cancelFlag);
}






