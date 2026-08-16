#include "town_consciousness.hpp"
#include "world.hpp"
#include "llama_wrapper.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

TownConsciousness::TownConsciousness(World& world, std::function<std::string(const std::string&, int, float)> llm_callback)
    : world_(world), llm_callback_(std::move(llm_callback))
{
    event_buffer_.resize(MAX_EVENTS);
    load_memory();
    
    // Start consolidation worker thread
    consolidation_thread_ = std::thread(&TownConsciousness::consolidation_worker, this);
    
    // Initialize adaptations with defaults if first run
    if (memory_.consolidation_count == 0) {
        current_adaptations_.day = world_.day;
        save_adaptations();
    } else {
        // Load existing adaptations
        std::ifstream f("data/adaptations.json");
        if (f) {
            json j; f >> j;
            current_adaptations_.procgen = j.value("procgen", json::object());
            current_adaptations_.npc = j.value("npc", json::object());
            current_adaptations_.economy = j.value("economy", json::object());
            current_adaptations_.weather = j.value("weather", json::object());
            current_adaptations_.horror = j.value("horror", json::object());
            current_adaptations_.performance = j.value("performance", json::object());
            current_adaptations_.day = j.value("day", world_.day);
            current_adaptations_.consolidation_count = j.value("consolidation_count", 0);
        }
        current_adaptations_.day = world_.day;
    }
}

TownConsciousness::~TownConsciousness() {
    // Signal consolidation thread to exit
    {
        std::lock_guard<std::mutex> lock(consolidation_mutex_);
        consolidation_requested_ = true;
    }
    consolidation_cv_.notify_one();
    if (consolidation_thread_.joinable()) {
        consolidation_thread_.join();
    }
    
    // Save on shutdown
    save_memory();
    save_adaptations();
    save_log();
}

void TownConsciousness::observe(const TownEvent& event) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    event_buffer_[buffer_head_] = event;
    buffer_head_ = (buffer_head_ + 1) % MAX_EVENTS;
    if (buffer_count_ < MAX_EVENTS) buffer_count_++;
}

bool TownConsciousness::is_consolidation_due() const {
    // Consolidate at 04:00 in-game (day_seconds >= 4*3600 = 14400, but DAY_LENGTH_S = 800 = 20 game hours)
    // 04:00 = 4/20 * 800 = 160 seconds into day
    // Actually: 6AM = 0 seconds, so 4AM would be -2 hours = negative. Let's use hour 4 (10:00 AM game time)
    // Game day: 6AM=0s, 12PM=300s, 6PM=600s, 12AM=900s, 2AM=1000s, 4AM=1100s, 6AM=800s (next day)
    // 04:00 AM = 22 hours after 6AM = 22/24 * 800 = 733 seconds
    // But simpler: consolidate once per day at a specific hour
    int hour = hour_of_day(world_);
    // Game time: 6AM=6, 12PM=12, 6PM=18, 12AM=24, 4AM=28, 6AM=30(next day)
    return hour == 28 && world_.day != last_consolidation_day_;
}

void TownConsciousness::consolidate() {
    // Request async consolidation
    if (consolidation_in_progress_.load()) return;
    
    consolidation_requested_.store(true);
    consolidation_cv_.notify_one();
}

void TownConsciousness::consolidation_worker() {
    while (true) {
        std::unique_lock<std::mutex> lock(consolidation_mutex_);
        consolidation_cv_.wait(lock, [this] { return consolidation_requested_.load(); });
        
        if (consolidation_in_progress_.load()) {
            consolidation_requested_.store(false);
            continue;
        }
        
        consolidation_in_progress_.store(true);
        consolidation_requested_.store(false);
        lock.unlock();
        
        // Do the actual consolidation work
        if (world_.day != last_consolidation_day_) {
            std::cout << "[TownConsciousness] Consolidating day " << world_.day << "..." << std::endl;
            
            // Collect events from buffer
            std::vector<TownEvent> events;
            {
                std::lock_guard<std::mutex> buffer_lock(buffer_mutex_);
                events.reserve(buffer_count_);
                size_t idx = (buffer_head_ + MAX_EVENTS - buffer_count_) % MAX_EVENTS;
                for (size_t i = 0; i < buffer_count_; ++i) {
                    events.push_back(event_buffer_[(idx + i) % MAX_EVENTS]);
                }
                // Clear buffer after consolidation (keep last 1000 for continuity)
                if (buffer_count_ > 1000) {
                    buffer_count_ = 1000;
                    buffer_head_ = 1000;
                }
            }
            
            // Summarize observed events into the structured memory sections
            aggregate_memory();
            
            // Run LLM inference
            run_consolidation_inference();
            
            // Update memory with consolidated facts
            memory_.last_consolidation_day = world_.day;
            memory_.consolidation_count++;
            
            // Apply damping to new adaptations
            apply_damping(current_adaptations_);
            
            current_adaptations_.day = world_.day;
            current_adaptations_.consolidation_count = memory_.consolidation_count;
            last_consolidation_day_ = world_.day;
            
            // Persist
            save_memory();
            save_adaptations();
            save_log();
            
            std::cout << "[TownConsciousness] Consolidation complete." << std::endl;
        }
        
        consolidation_in_progress_.store(false);
    }
}

void TownConsciousness::force_consolidate() {
    consolidate();
}

void TownConsciousness::run_consolidation_inference() {
    std::string prompt = build_consolidation_prompt();
    
    // Use the LlamaWrapper callback set by main
    if (llm_callback_) {
        // Use lower temperature for more deterministic JSON output
        // Use fewer max tokens since we only need JSON
        std::string response = llm_callback_(prompt, 1024, 0.1f);
        parse_llm_response(response);
    }
}

void TownConsciousness::aggregate_memory() {
    // Summarize the event buffer into the structured memory sections so the
    // consolidation prompt (and the model) always sees current facts even when
    // the LLM output contains no memory fields. Deterministic and cheap.
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (buffer_count_ == 0) return;

    std::map<std::string, int> cmd_counts;       // action -> count
    std::map<std::string, int> npc_interactions; // npc -> count
    std::map<std::string, double> eco_values;    // commodity -> total spent
    std::map<std::string, int> harvest_counts;   // crop -> count
    int weather_events = 0;
    int horror_events = 0;
    uint32_t earliest_day = UINT32_MAX, latest_day = 0;

    size_t idx = (buffer_head_ + MAX_EVENTS - buffer_count_) % MAX_EVENTS;
    for (size_t i = 0; i < buffer_count_; ++i) {
        const auto& e = event_buffer_[(idx + i) % MAX_EVENTS];
        earliest_day = std::min(earliest_day, e.day);
        latest_day = std::max(latest_day, e.day);

        if (e.system == "player" && e.event_type == "player_cmd") {
            std::string action = e.payload.value("action", "");
            if (!action.empty()) cmd_counts[action]++;
        } else if (e.system == "npc" && e.event_type == "npc_talk") {
            std::string npc = e.payload.value("npc", "");
            if (!npc.empty()) npc_interactions[npc]++;
        } else if (e.system == "economy" && e.event_type == "buy") {
            std::string item = e.payload.value("item", "");
            double cost = e.payload.value("cost", 0.0);
            if (!item.empty()) eco_values[item] += cost;
        } else if (e.system == "crop" && e.event_type == "crop_harvest") {
            std::string crop = e.payload.value("crop", "");
            if (!crop.empty()) harvest_counts[crop]++;
        } else if (e.system == "weather") {
            weather_events++;
        } else if (e.system == "horror") {
            horror_events++;
        }
    }

    if (!cmd_counts.empty()) {
        memory_.player_habits["cmd_frequency"] = cmd_counts;
        memory_.player_habits["day_span"] = latest_day - earliest_day + 1;
    }
    if (!npc_interactions.empty()) {
        for (auto& [npc, n] : npc_interactions) {
            if (!memory_.npc_relationships.contains(npc)) {
                memory_.npc_relationships[npc] = json::object();
            }
            memory_.npc_relationships[npc]["interactions"] = n;
        }
    }
    if (!eco_values.empty()) {
        json history = memory_.economic_trends.value("buy_history", json::object());
        for (auto& [item, total] : eco_values) {
            double prev = history.contains(item) && history[item].is_number()
                              ? history[item].get<double>() : 0.0;
            history[item] = prev + total;
        }
        memory_.economic_trends["buy_history"] = history;
        memory_.economic_trends["last_consolidation_day"] = latest_day;
    }
    if (!harvest_counts.empty()) {
        memory_.ecological_state["harvested"] = harvest_counts;
    }
    memory_.performance_profile["weather_events"] = weather_events;
    memory_.performance_profile["horror_events"] = horror_events;
    memory_.performance_profile["last_consolidation_day"] = latest_day;
}

std::string TownConsciousness::build_consolidation_prompt() const {
    std::ostringstream oss;
    
    oss << "You are the Town Consciousness of Ashgrove Valley. Output ONLY valid JSON with updated adaptations.\n";
    oss << "Day: " << world_.day << " | Season: " << season_name(season_index(world_.day)) << "\n";
    oss << "Consolidation #" << (memory_.consolidation_count + 1) << "\n\n";
    
    oss << "=== MEMORY ===\n";
    // Only include non-empty memory sections, truncated
    if (!memory_.player_habits.empty()) oss << "player_habits: " << memory_.player_habits.dump() << "\n";
    if (!memory_.npc_relationships.empty()) oss << "npc_relationships: " << memory_.npc_relationships.dump() << "\n";
    if (!memory_.economic_trends.empty()) oss << "economic_trends: " << memory_.economic_trends.dump() << "\n";
    if (!memory_.ecological_state.empty()) oss << "ecological_state: " << memory_.ecological_state.dump() << "\n";
    if (!memory_.discovered_secrets.empty()) oss << "discovered_secrets: " << memory_.discovered_secrets.dump() << "\n";
    if (!memory_.performance_profile.empty()) oss << "performance_profile: " << memory_.performance_profile.dump() << "\n";
    if (!memory_.narrative_state.empty()) oss << "narrative_state: " << memory_.narrative_state.dump() << "\n\n";
    
    oss << "=== CURRENT ADAPTATIONS ===\n";
    oss << "procgen: " << current_adaptations_.procgen.dump() << "\n";
    oss << "npc: " << current_adaptations_.npc.dump() << "\n";
    oss << "economy: " << current_adaptations_.economy.dump() << "\n";
    oss << "weather: " << current_adaptations_.weather.dump() << "\n";
    oss << "horror: " << current_adaptations_.horror.dump() << "\n";
    oss << "performance: " << current_adaptations_.performance.dump() << "\n\n";
    
    oss << "=== EVENTS (last 24h) ===\n";
    oss << "events: " << buffer_count_ << " entries\n\n";
    
    oss << "=== TASK ===\n";
    oss << "Output ONLY valid JSON with keys: procgen, npc, economy, weather, horror, performance.\n";
    oss << "Each key contains an object with adaptation values (numbers).\n";
    oss << "Apply damping: new = 0.3 * proposed + 0.7 * old.\n";
    oss << "Output ONLY the JSON object, no extra text.\n";
    
    return oss.str();
}

void TownConsciousness::parse_llm_response(const std::string& response) {
    try {
        // Extract JSON from response (might have extra text) - find the outermost valid JSON object
        size_t start = response.find('{');
        if (start == std::string::npos) {
            std::cerr << "[TownConsciousness] No JSON found in LLM response" << std::endl;
            return;
        }
        
        // Find the matching closing brace by counting braces
        int brace_count = 0;
        size_t end = std::string::npos;
        for (size_t i = start; i < response.length(); ++i) {
            if (response[i] == '{') brace_count++;
            else if (response[i] == '}') {
                brace_count--;
                if (brace_count == 0) {
                    end = i;
                    break;
                }
            }
        }
        
        if (end == std::string::npos) {
            std::cerr << "[TownConsciousness] No matching closing brace found in LLM response" << std::endl;
            return;
        }
        
        std::string json_str = response.substr(start, end - start + 1);
        
        // Try to parse, if it fails, try to clean up common issues
        try {
            json j = json::parse(json_str);
            
            // Parse each section with damping
            if (j.contains("procgen")) {
                for (auto& [key, val] : j["procgen"].items()) {
                    if (current_adaptations_.procgen.contains(key)) {
                        current_adaptations_.procgen[key] = val;
                    }
                }
            }
            if (j.contains("npc")) {
                for (auto& [key, val] : j["npc"].items()) {
                    if (current_adaptations_.npc.contains(key)) {
                        current_adaptations_.npc[key] = val;
                    }
                }
            }
            if (j.contains("economy")) {
                for (auto& [key, val] : j["economy"].items()) {
                    if (current_adaptations_.economy.contains(key)) {
                        current_adaptations_.economy[key] = val;
                    }
                }
            }
            if (j.contains("weather")) {
                for (auto& [key, val] : j["weather"].items()) {
                    if (current_adaptations_.weather.contains(key)) {
                        current_adaptations_.weather[key] = val;
                    }
                }
            }
            if (j.contains("horror")) {
                for (auto& [key, val] : j["horror"].items()) {
                    if (current_adaptations_.horror.contains(key)) {
                        current_adaptations_.horror[key] = val;
                    }
                }
            }
            if (j.contains("performance")) {
                for (auto& [key, val] : j["performance"].items()) {
                    if (current_adaptations_.performance.contains(key)) {
                        current_adaptations_.performance[key] = val;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[TownConsciousness] Failed to parse LLM response JSON: " << e.what() << std::endl;
            std::cerr << "[TownConsciousness] Response was: " << json_str.substr(0, 200) << "..." << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[TownConsciousness] Failed to parse LLM response: " << e.what() << std::endl;
    }
}

void TownConsciousness::dampen_json(json& current, const json& proposed, float alpha) {
    for (auto& [key, val] : proposed.items()) {
        if (current.contains(key)) {
            if (val.is_number() && current[key].is_number()) {
                current[key] = alpha * val.get<float>() + (1.0f - alpha) * current[key].get<float>();
            } else if (val.is_object() && current[key].is_object()) {
                // Recurse for nested objects
                dampen_json(current[key], proposed[key], alpha);
            } else {
                current[key] = val;
            }
        } else {
            current[key] = val;
        }
    }
}

void TownConsciousness::apply_damping(Adaptations& new_adaptations, float alpha) {
    dampen_json(current_adaptations_.procgen, new_adaptations.procgen, alpha);
    dampen_json(current_adaptations_.npc, new_adaptations.npc, alpha);
    dampen_json(current_adaptations_.economy, new_adaptations.economy, alpha);
    dampen_json(current_adaptations_.weather, new_adaptations.weather, alpha);
    dampen_json(current_adaptations_.horror, new_adaptations.horror, alpha);
    dampen_json(current_adaptations_.performance, new_adaptations.performance, alpha);
}

void TownConsciousness::load_memory() {
    std::ifstream f("data/town_memory.json");
    if (f) {
        json j; f >> j;
        memory_.player_habits = j.value("player_habits", json::object());
        memory_.npc_relationships = j.value("npc_relationships", json::object());
        memory_.economic_trends = j.value("economic_trends", json::object());
        memory_.ecological_state = j.value("ecological_state", json::object());
        memory_.discovered_secrets = j.value("discovered_secrets", json::array());
        memory_.performance_profile = j.value("performance_profile", json::object());
        memory_.narrative_state = j.value("narrative_state", json::object());
        memory_.last_consolidation_day = j.value("last_consolidation_day", 0);
        memory_.consolidation_count = j.value("consolidation_count", 0);
    }
}

void TownConsciousness::save_memory() const {
    json j;
    j["player_habits"] = memory_.player_habits;
    j["npc_relationships"] = memory_.npc_relationships;
    j["economic_trends"] = memory_.economic_trends;
    j["ecological_state"] = memory_.ecological_state;
    j["discovered_secrets"] = memory_.discovered_secrets;
    j["performance_profile"] = memory_.performance_profile;
    j["narrative_state"] = memory_.narrative_state;
    j["last_consolidation_day"] = memory_.last_consolidation_day;
    j["consolidation_count"] = memory_.consolidation_count;
    
    std::ofstream f("data/town_memory.json");
    if (f) f << j.dump(2);
}

void TownConsciousness::save_adaptations() const {
    json j;
    j["procgen"] = current_adaptations_.procgen;
    j["npc"] = current_adaptations_.npc;
    j["economy"] = current_adaptations_.economy;
    j["weather"] = current_adaptations_.weather;
    j["horror"] = current_adaptations_.horror;
    j["performance"] = current_adaptations_.performance;
    j["day"] = current_adaptations_.day;
    j["consolidation_count"] = current_adaptations_.consolidation_count;
    
    std::ofstream f("data/adaptations.json");
    if (f) f << j.dump(2);
}

void TownConsciousness::save_log() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    std::ofstream f("data/town_log.jsonl", std::ios::app);
    if (!f) return;
    
    size_t idx = (buffer_head_ + MAX_EVENTS - buffer_count_) % MAX_EVENTS;
    for (size_t i = 0; i < buffer_count_; ++i) {
        const auto& e = event_buffer_[(idx + i) % MAX_EVENTS];
        json j = {
            {"tick", e.tick},
            {"day", e.day},
            {"system", e.system},
            {"event_type", e.event_type},
            {"payload", e.payload},
            {"player_id", e.player_id}
        };
        f << j.dump() << '\n';
    }
}

json TownConsciousness::profile_hardware() const {
    json j;
    // CPU cores
    j["cpu_cores"] = std::thread::hardware_concurrency();
    
    // Memory (approximate)
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo) {
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                size_t kb = std::stoul(line.substr(9));
                j["ram_mb"] = kb / 1024;
                break;
            }
        }
    }
    
    // Current process memory
    std::ifstream statm("/proc/self/statm");
    if (statm) {
        size_t pages;
        statm >> pages;
        j["process_ram_mb"] = (pages * 4096) / (1024 * 1024);
    }
    
    return j;
}

json TownConsciousness::get_memory_summary() const {
    json j;
    j["consolidation_count"] = memory_.consolidation_count;
    j["last_consolidation_day"] = memory_.last_consolidation_day;
    j["event_buffer_size"] = buffer_count_;
    j["player_habits_keys"] = memory_.player_habits.size();
    j["npc_relationships_count"] = memory_.npc_relationships.size();
    j["secrets_discovered"] = memory_.discovered_secrets.size();
    return j;
}

json TownConsciousness::explain_adaptation(const std::string& system) const {
    json j;
    j["system"] = system;
    j["current"] = current_adaptations_.procgen; // Would filter by system
    j["memory_context"] = {
        {"recent_events", buffer_count_},
        {"consolidations", memory_.consolidation_count}
    };
    j["reasoning"] = "Adaptation based on consolidated patterns from " + std::to_string(memory_.consolidation_count) + " cycles.";
    return j;
}

void TownConsciousness::reset() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    event_buffer_.clear();
    event_buffer_.resize(MAX_EVENTS);
    buffer_head_ = 0;
    buffer_count_ = 0;
    
    memory_ = TownMemory{};
    current_adaptations_ = Adaptations{};
    current_adaptations_.day = world_.day;
    last_consolidation_day_ = 0;
    
    fs::remove("data/town_memory.json");
    fs::remove("data/adaptations.json");
    fs::remove("data/town_log.jsonl");
    
    std::cout << "[TownConsciousness] Reset complete." << std::endl;
}