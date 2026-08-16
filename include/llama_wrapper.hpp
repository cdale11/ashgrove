#pragma once
#include <string>
#include <optional>
#include <mutex>
#include <nlohmann/json.hpp>

struct Intent {
    std::string action;          // e.g., "move", "interact", "talk", "craft"
    nlohmann::json parameters;  // arbitrary parameters
};

class LlamaWrapper {
public:
    explicit LlamaWrapper(const std::string& model_path);
    ~LlamaWrapper();

    // Parse raw player text into a structured Intent.
    // Returns nullopt on failure. Serialized internally: the underlying
    // llama_context is not thread-safe, so concurrent callers are serialized.
    std::optional<Intent> parse_command(const std::string& raw_text);

    // General text generation inference. Returns generated text or empty string on failure.
    // max_tokens: maximum tokens to generate (default 256)
    // temp: sampling temperature (default 0.3)
    std::string infer(const std::string& prompt, int max_tokens = 256, float temp = 0.3f);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
    mutable std::mutex m_mutex;  // serializes all llama decode/sample calls
};