#include "command_log.hpp"

#include <filesystem>
#include <cstdint>

CommandLog::CommandLog(std::string path) {
    if (path.empty()) { enabled_ = false; return; }
    try {
        std::filesystem::path dir = std::filesystem::path(path).parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        m_out.open(path, std::ios::app);
        enabled_ = m_out.is_open();
    } catch (...) {
        enabled_ = false;
    }
}

void CommandLog::append(const nlohmann::json& record) {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_out << record.dump() << "\n";
    m_out.flush();
}

void CommandLog::record(uint64_t ts_ms, uint32_t player_id, int day, const std::string& season,
                        int hour, const std::string& raw, const nlohmann::json& intent,
                        const std::string& tier, uint64_t latency_ms,
                        const std::vector<std::string>& lines) {
    nlohmann::json j;
    j["ts"] = ts_ms;
    j["player_id"] = player_id;
    j["day"] = day;
    j["season"] = season;
    j["hour"] = hour;
    j["raw"] = raw;
    j["intent"] = intent;
    j["tier"] = tier;
    j["latency_ms"] = latency_ms;
    j["lines"] = lines;
    append(j);
}
