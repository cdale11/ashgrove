#pragma once
#include <string>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

#include "llama_wrapper.hpp"

// Tiered intent parsing (Phase 8 "Model Distillation" runtime plan):
//
//   Tier 0 – deterministic rule/graph fast path. Sub-millisecond, covers the
//            finite command surface so the common commands never touch a model.
//   Tier 1 – an LLM backend. Today this is the local gemma-4-E4B via
//            LlamaWrapper; the plan is to swap in a fine-tuned 0.5B student
//            (same LlamaWrapper / GBNF interface) once trained. A cloud model
//            is used offline only, to generate the training set.
//
// `parse()` tries Tier 0 first and only falls through to Tier 1 when the rule
// path cannot classify the input. The winning tier is reported via `source`
// so the caller can log which backend served each command.
class IntentEngine {
public:
    IntentEngine();

    // Tier 0: deterministic classification of the known command surface.
    // Returns nullopt when the input does not match a known command.
    std::optional<Intent> parse_rule(const std::string& raw);

    // Tier 1: LLM backend. Used as a fallback for anything Tier 0 cannot
    // classify. Slow (serialized, 10-30 s today; <1 s once a student ships).
    std::optional<Intent> parse_llm(const std::string& raw);

    // Combined dispatch: rule first, then LLM. Fills `source` with "rule" or
    // "llm" to record which tier produced the intent.
    std::optional<Intent> parse(const std::string& raw, std::string* source = nullptr);

    // Attach the local LLM backend. If none is set, parse_llm() returns
    // nullopt and parse() degrades to Tier 0 only.
    void set_llm_backend(std::function<std::optional<Intent>(const std::string&)> backend);

private:
    std::function<std::optional<Intent>(const std::string&)> llm_backend_;
};
