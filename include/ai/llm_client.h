#pragma once

#include <string>
#include <optional>
#include <mutex>

namespace ashgrove {

struct LLMConfig {
    std::string url = "http://127.0.0.1:8081"; // base URL of a llama-server instance
    std::string model = "local";
    int max_tokens = 256;
    double temperature = 0.9;
    double request_timeout_s = 120.0;
};

// Thin client for llama.cpp's llama-server OpenAI-compatible API.
// The game server uses it to propose NPC *text*; the simulation alone
// decides all effects, so the LLM can never corrupt world state.
class LLMClient {
public:
    explicit LLMClient(LLMConfig config = {});
    ~LLMClient();

    LLMClient(const LLMClient&) = delete;
    LLMClient& operator=(const LLMClient&) = delete;

    bool health() const;
    bool ready() const { return ready_; }

    // Returns generated assistant text, or nullopt on failure.
    std::optional<std::string> complete(const std::string& system_prompt,
                                        const std::string& user_prompt) const;

private:
    LLMConfig config_;
    bool ready_ = false;
    mutable std::mutex init_mutex_;
};

} // namespace ashgrove
