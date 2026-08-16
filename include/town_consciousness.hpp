#pragma once

#include "world.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>

using json = nlohmann::json;

struct TownEvent {
    uint32_t tick;
    uint32_t day;
    std::string system;      // "weather", "economy", "npc", "player", "horror", "crop", "building", "procgen"
    std::string event_type;  // specific event: "storm", "crop_harvest", "npc_schedule", "player_cmd", etc.
    json payload;            // structured event data
    int player_id = 0;       // 0 = world/system event
};

struct TownMemory {
    // Structured facts about the world
    json player_habits;           // {cmd_frequency, play_times, preferred_crops, explored_regions, social_style}
    json npc_relationships;       // {npc_name: {hearts, trust, fear, personality_drift}}
    json economic_trends;         // {price_history, demand_shifts, market_cycles, player_wealth}
    json ecological_state;        // {soil_health, forest_health, water_table, pest_pressure, biodiversity}
    json discovered_secrets;      // list of discovered narrative elements
    json performance_profile;     // {avg_tick_ms, memory_mb, cpu_percent, hardware_tier}
    json narrative_state;         // {horror_cycle, anchors_discovered, active_threats, player_intent}
    
    uint32_t last_consolidation_day = 0;
    uint32_t consolidation_count = 0;
};

struct Adaptations {
    // Procgen biases
    json procgen = {
        {"biome_preference", json::object()},
        {"ruin_density", 1.0},
        {"resource_richness", 1.0},
        {"chunk_complexity", 1.0}
    };
    
    // NPC personality drifts
    json npc = {
        {"personality_drift", json::object()},    // {npc_name: {trait: delta}}
        {"schedule_bias", json::object()},        // {npc_name: {activity: weight_delta}}
        {"dialogue_topic_weight", json::object()}, // {npc_name: {topic: weight_delta}}
        {"gift_preference_shift", json::object()}  // {npc_name: {item: weight_delta}}
    };
    
    // Economy shifts
    json economy = {
        {"demand_shift", json::object()},       // {commodity: multiplier}
        {"price_elasticity", 1.0},              // global price sensitivity
        {"shop_price_mod", json::object()},     // {shop_name: {item: price_delta}}
        {"market_volatility", 0.0}              // 0=stable, 1=chaotic
    };
    
    // Weather tendencies
    json weather = {
        {"pressure_bias", 0.0},              // -1 to 1 (low=storms, high=clear)
        {"humidity_drift", 0.0},             // -1 to 1
        {"storm_chance", 0.01},              // base daily storm probability
        {"fog_intensity", 0.0},              // 0 to 1
        {"temperature_bias", 0.0},           // -5 to +5 degrees
        {"seasonal_anomaly", 0.0}            // -1 to 1 (early/late season)
    };
    
    // Horror intensity
    json horror = {
        {"intensity", 0.0},                  // 0 to 1
        {"basement_unlock_progress", 0.0},   // 0 to 1
        {"night_event_weight", 1.0},         // multiplier
        {"sanity_drain_multiplier", 1.0},
        {"phantom_sighting_chance", 0.0},
        {"active_threat", ""}                // named threat if any
    };
    
    // Performance tuner (self-optimizing)
    json performance = {
        {"thread_pool", {
            {"world_gen", 2},
            {"npc_ai", 2},
            {"weather", 1},
            {"io", 1}
        }},
        {"tick_budget_ms", 16},
        {"chunk_load_radius", 3},
        {"npc_decision_interval_ticks", 5},
        {"weather_update_interval_ticks", 20},
        {"save_compression", "zstd:3"},
        {"llm_inference_interval_ticks", 10}
    };
    
    uint32_t day = 0;
    uint32_t consolidation_count = 0;
};

class TownConsciousness {
public:
    explicit TownConsciousness(World& world, std::function<std::string(const std::string&, int, float)> llm_callback = {});
    ~TownConsciousness();
    
    // Push event to ring buffer (thread-safe)
    void observe(const TownEvent& event);
    
    // Run consolidation: LoRA inference on buffer + memory -> write town_memory.json, adaptations.json
    void consolidate();
    
    // Aggregate the current event buffer into the structured memory sections
    // (player_habits, npc_relationships, economic_trends, ecological_state,
    // performance_profile). Called during consolidation before the prompt is
    // built so the LLM always sees current facts. Deterministic, no LLM needed.
    void aggregate_memory();
    
    // Get current day's adaptations for a system
    const Adaptations& get_adaptations() const { return current_adaptations_; }
    
    // Get current town memory (read-only)
    const TownMemory& get_memory() const { return memory_; }
    
    // Check if consolidation is due (runs at 04:00 in-game)
    bool is_consolidation_due() const;
    
    // Force consolidation (for testing/dev)
    void force_consolidate();
    
    // Get memory summary for debugging/inspection
    json get_memory_summary() const;
    
    // Get adaptation delta explanation for a system
    json explain_adaptation(const std::string& system) const;
    
    // Reset consciousness (dev only)
    void reset();

private:
    World& world_;
    
    // Ring buffer for events (bounded, thread-safe)
    static constexpr size_t MAX_EVENTS = 10000;
    std::vector<TownEvent> event_buffer_;
    size_t buffer_head_ = 0;
    size_t buffer_count_ = 0;
    mutable std::mutex buffer_mutex_;
    
    // Current state
    TownMemory memory_;
    Adaptations current_adaptations_;
    uint32_t last_consolidation_day_ = 0;
    
    // LLM inference callback
    std::function<std::string(const std::string&, int, float)> llm_callback_;
    
    // Async consolidation
    std::thread consolidation_thread_;
    std::atomic<bool> consolidation_in_progress_{false};
    std::atomic<bool> consolidation_requested_{false};
    std::mutex consolidation_mutex_;
    std::condition_variable consolidation_cv_;
    
    // Consolidation thread
    void consolidation_worker();
    
    // LLM inference
    void run_consolidation_inference();
    std::string build_consolidation_prompt() const;
    void parse_llm_response(const std::string& response);
    
    // Persistence
    void load_memory();
    void save_memory() const;
    void save_adaptations() const;
    void save_log() const;
    
    // Damping for stability
    void apply_damping(Adaptations& new_adaptations, float alpha = 0.3f);
    void dampen_json(json& current, const json& proposed, float alpha);
    
    // Hardware profiling
    json profile_hardware() const;
};