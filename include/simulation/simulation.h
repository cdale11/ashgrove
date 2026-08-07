#pragma once

#include "common/types.h"
#include <functional>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace ashgrove {

class World;
class NPC;

struct SimulationEvent {
    enum class Type : uint8_t {
        TimeAdvanced,
        SeasonChanged,
        WeatherChanged,
        NPCSpawned,
        NPCDied,
        BuildingConstructed,
        BuildingDestroyed,
        RelationshipChanged,
        RumorCreated,
        QuestUpdated
    };
    
    Type type;
    TimeTick tick;
    EntityID entity_id = INVALID_ENTITY_ID;
    std::string data; // JSON payload
};

using EventCallback = std::function<void(const SimulationEvent&)>;

class Simulation {
public:
    Simulation();
    ~Simulation();
    
    void initialize(World* world);
    void shutdown();
    
    // Main simulation step - called each tick
    void step();
    
    // Time control
    void advance_time(TimeTick ticks = 1);
    const GameTime& get_time() const { return time_; }
    TimeTick get_tick() const { return time_.ticks; }
    
    // Event system
    void subscribe(EventCallback cb);
    void emit(const SimulationEvent& event);
    
    // Speed control (for debugging)
    void set_time_scale(float scale) { time_scale_ = scale; }
    float get_time_scale() const { return time_scale_; }
    
    // Pause/resume
    void pause() { paused_ = true; }
    void resume() { paused_ = false; }
    bool is_paused() const { return paused_; }
    
    // Serialization
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json& j);

    // Public for callers that need to run NPC AI explicitly (e.g. game loop)
    void process_npc_ai();
    void process_world_events();

private:
    void update_time();
    void update_season();
    void update_weather();
    
    World* world_ = nullptr;
    GameTime time_;
    float time_scale_ = 1.0f;
    bool paused_ = false;
    std::vector<EventCallback> subscribers_;
    uint64_t event_counter_ = 0;
};

} // namespace ashgrove