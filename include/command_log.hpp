#pragma once
#include <mutex>
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>

// Phase 8 "Model Distillation" – log collector (Phase A).
//
// Appends one JSON object per line to `data/cmdlog.jsonl` for every `/cmd`:
//   {"ts": epoch_ms, "player_id": int, "day": int, "season": str, "hour": int,
//    "raw": "the raw command text",
//    "intent": {"action": str, "parameters": {...}},
//    "tier": "rule"|"llm"|"none",
//    "latency_ms": int,
//    "lines": ["response line", ...]}
//
// This is the ever-growing dataset that Phase 8-B (teacher generation) expands
// into paraphrases, and which the fine-tuned student is eventually evaluated
// against. Written as JSONL (one JSON per line) so it streams cheaply and is
// trivially consumed by Python / HuggingFace tooling.
class CommandLog {
public:
    explicit CommandLog(std::string path);

    // Thread-safe append of a fully-formed record. Missing fields are written
    // verbatim; `append` builds the surrounding envelope.
    void append(const nlohmann::json& record);

    // Convenience wrapper: assemble the standard record and append it.
    void record(uint64_t ts_ms, uint32_t player_id, int day, const std::string& season,
                int hour, const std::string& raw, const nlohmann::json& intent,
                const std::string& tier, uint64_t latency_ms,
                const std::vector<std::string>& lines);

    bool enabled() const { return enabled_; }

private:
    std::mutex m_mutex;
    std::ofstream m_out;
    bool enabled_ = false;
};
