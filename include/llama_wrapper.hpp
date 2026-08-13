#pragma once
#include <string>
#include <optional>
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
    // Returns nullopt on failure.
    std::optional<Intent> parse_command(const std::string& raw_text) const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};