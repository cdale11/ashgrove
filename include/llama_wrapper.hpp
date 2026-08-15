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

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
    mutable std::mutex m_mutex;  // serializes all llama decode/sample calls
};