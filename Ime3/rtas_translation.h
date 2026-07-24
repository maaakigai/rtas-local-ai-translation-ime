#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <thread>
#include <utility>

class AsyncWorkQueue {
public:
    using CancelFlag = std::shared_ptr<std::atomic_bool>;
    using Job = std::function<void(uint64_t id, const CancelFlag& cancelFlag)>;

    AsyncWorkQueue() = default;
    ~AsyncWorkQueue();

    uint64_t Enqueue(Job job);
    bool Cancel(uint64_t id);
    void Shutdown();

private:
    struct QueueItem {
        uint64_t id = 0;
        CancelFlag cancelFlag;
        Job job;
    };

    void EnsureWorkerLocked(std::unique_lock<std::mutex>& lock);
    void WorkerLoop();

    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<QueueItem> m_queue;
    std::unordered_map<uint64_t, CancelFlag> m_pending;
    std::atomic_uint64_t m_nextId{ 0 };
    bool m_stop = false;
    bool m_started = false;
};

struct TranslationResult {
    uint64_t requestId = 0;
    bool success = false;
    bool cancelled = false;
    std::wstring source;
    std::wstring translation;
    std::wstring error;
};

struct TranslationLlmSettings {
    std::wstring model = L"default";
    std::wstring host = L"127.0.0.1";
    uint16_t port = 11434;
    std::wstring path = L"/api/generate";
    bool useTls = false;
    int timeoutMs = 15000;
    std::wstring keepAlive = L"-1";
    bool warmupOnActivate = true;
    int warmupTimeoutMs = 60000;
    bool unloadOnDeactivate = true;
    int unloadDelayMs = 10000;
    bool logTimings = true;
};

struct OllamaLifecycleResult {
    bool success = false;
    bool cancelled = false;
    std::wstring model;
    std::wstring error;
    std::wstring rawPayload;
};

TranslationResult ExecuteTranslationJob(uint64_t id,
    const std::wstring& reading,
    const std::wstring& context,
    const AsyncWorkQueue::CancelFlag& cancelFlag,
    const TranslationLlmSettings& llmSettings = {});

OllamaLifecycleResult WarmupOllamaModel(
    const TranslationLlmSettings& llmSettings,
    const AsyncWorkQueue::CancelFlag& cancelFlag = {});

OllamaLifecycleResult UnloadOllamaModel(
    const TranslationLlmSettings& llmSettings,
    const AsyncWorkQueue::CancelFlag& cancelFlag = {});

