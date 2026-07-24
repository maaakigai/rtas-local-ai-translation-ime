#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llm {

struct OllamaRequest {
    std::wstring prompt;                 // user prompt (required)
    std::wstring systemPrompt;           // optional system instructions
    std::wstring modelOverride;          // per-call model override (optional)
    std::wstring keepAlive;              // Ollama keep_alive override (optional)
    double temperature = 0.2;            // sampling temperature
    bool stream = false;                 // streaming not supported yet (false)
    std::vector<std::wstring> stop;      // stop sequences (optional)
};

struct OllamaResponse {
    bool success = false;
    bool cancelled = false;
    unsigned int httpStatus = 0;
    std::wstring modelUsed;
    std::wstring text;
    std::wstring error;
    std::wstring rawPayload;
    uint64_t totalDurationNs = 0;
    uint64_t loadDurationNs = 0;
};

class OllamaClient {
public:
    using CancelFlag = std::shared_ptr<std::atomic_bool>;

    struct Config {
        std::wstring host = L"127.0.0.1";
        uint16_t port = 11434;
        std::wstring defaultModel;           // falls back to env or built-in default
        uint32_t connectTimeoutMs = 5000;
        uint32_t sendTimeoutMs = 5000;
        uint32_t receiveTimeoutMs = 15000;
        uint32_t overallTimeoutMs = 15000;
        uint32_t maxRetries = 2;
        bool useTls = false;
        std::wstring keepAlive = L"-1";
        bool logTimings = true;
    };

    explicit OllamaClient(Config config = {});

    void SetConfig(const Config& config);
    const Config& GetConfig() const noexcept { return m_config; }

    OllamaResponse Generate(const OllamaRequest& request,
        const CancelFlag& cancelFlag = {}) const;

private:
    Config m_config;

    std::wstring ResolveModel(const std::wstring& overrideModel) const;
    std::wstring ResolveHost() const;
    uint16_t ResolvePort() const;
    bool IsCancelled(const CancelFlag& flag) const;
};

}  // namespace llm

