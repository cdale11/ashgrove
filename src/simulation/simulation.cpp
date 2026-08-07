#include "simulation/simulation.h"
#include "world/world.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <random>

namespace ashgrove {

Simulation::Simulation() {
    time_.ticks = 0;
    time_.year = 1;
    time_.day_of_year = 1;
    time_.hour = 6;
    time_.minute = 0;
    time_.season = Season::Spring;
    time_.weather = WeatherType::Clear;
    time_.weather_intensity = 0.0f;
}

Simulation::~Simulation() {
    shutdown();
}

void Simulation::initialize(World* world) {
    world_ = world;
    spdlog::info("Simulation initialized at {}", time_.to_string());
}

void Simulation::shutdown() {
    subscribers_.clear();
    world_ = nullptr;
}

void Simulation::step() {
    if (paused_) return;
    
    TimeTick steps = static_cast<TimeTick>(time_scale_);
    if (steps < 1) steps = 1;
    
    for (TimeTick i = 0; i < steps; ++i) {
        advance_time(1);
    }
}

void Simulation::advance_time(TimeTick ticks) {
    if (paused_) return;
    time_.ticks += ticks;

    uint64_t total_minutes = static_cast<uint64_t>(time_.minute) + ticks;
    time_.minute = static_cast<uint8_t>(total_minutes % 60);
    uint64_t total_hours = static_cast<uint64_t>(time_.hour) + total_minutes / 60;
    time_.hour = static_cast<uint8_t>(total_hours % 24);
    uint64_t total_days = static_cast<uint64_t>(time_.day_of_year) + total_hours / 24;
    time_.day_of_year = static_cast<uint16_t>((total_days - 1) % DAYS_PER_YEAR + 1);
    time_.year += static_cast<uint16_t>((total_days - 1) / DAYS_PER_YEAR);

    update_time();
    update_season();
    update_weather();

    emit(SimulationEvent{SimulationEvent::Type::TimeAdvanced, time_.ticks});

    if (world_) {
        process_npc_ai();
        process_world_events();
    }
}

void Simulation::update_time() {
    // TimeOfDay is computed from hour in GameTime::get_time_of_day()
}

void Simulation::update_season() {
    Season old_season = time_.season;
    int month = (time_.day_of_year - 1) / DAYS_PER_MONTH; // 0-11

    if (month >= 0 && month <= 2) time_.season = Season::Spring;      // Jan-Mar
    else if (month >= 3 && month <= 5) time_.season = Season::Summer;  // Apr-Jun
    else if (month >= 6 && month <= 8) time_.season = Season::Autumn; // Jul-Sep
    else time_.season = Season::Winter;                                 // Oct-Dec
    
    if (old_season != time_.season) {
        emit(SimulationEvent{SimulationEvent::Type::SeasonChanged, time_.ticks, INVALID_ENTITY_ID, 
            nlohmann::json{{"season", to_string(time_.season)}}.dump()});
        spdlog::info("Season changed to {}", to_string(time_.season));
    }
}

void Simulation::update_weather() {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    // Simple weather transition probabilities
    float roll = dist(rng);
    WeatherType old_weather = time_.weather;
    
    // Seasonal weather biases
    float rain_chance = 0.1f, snow_chance = 0.0f, storm_chance = 0.02f, fog_chance = 0.05f;
    
    switch (time_.season) {
        case Season::Spring: rain_chance = 0.25f; fog_chance = 0.1f; break;
        case Season::Summer: rain_chance = 0.15f; storm_chance = 0.05f; break;
        case Season::Autumn: rain_chance = 0.2f; fog_chance = 0.15f; break;
        case Season::Winter: snow_chance = 0.3f; storm_chance = 0.03f; break;
    }
    
    if (time_.weather == WeatherType::Clear) {
        if (roll < rain_chance) time_.weather = WeatherType::Rain;
        else if (roll < rain_chance + snow_chance) time_.weather = WeatherType::Snow;
        else if (roll < rain_chance + snow_chance + storm_chance) time_.weather = WeatherType::Storm;
        else if (roll < rain_chance + snow_chance + storm_chance + fog_chance) time_.weather = WeatherType::Fog;
        else time_.weather = WeatherType::Cloudy;
    } else {
        // Weather clearing
        if (roll < 0.1f) time_.weather = WeatherType::Clear;
    }
    
    time_.weather_intensity = dist(rng) * 0.5f + 0.3f;
    
    if (old_weather != time_.weather) {
        emit(SimulationEvent{SimulationEvent::Type::WeatherChanged, time_.ticks, INVALID_ENTITY_ID,
            nlohmann::json{{"weather", to_string(time_.weather)}, {"intensity", time_.weather_intensity}}.dump()});
    }
}

void Simulation::process_npc_ai() {
    // NPC AI processing happens here
    // For now, just a placeholder
}

void Simulation::process_world_events() {
    // World events: building decay, crop growth, etc.
}

void Simulation::subscribe(EventCallback cb) {
    subscribers_.push_back(cb);
}

void Simulation::emit(const SimulationEvent& event) {
    for (auto& cb : subscribers_) {
        try {
            cb(event);
        } catch (const std::exception& e) {
            spdlog::error("Event callback failed: {}", e.what());
        }
    }
}

nlohmann::json Simulation::serialize() const {
    return nlohmann::json{
        {"ticks", time_.ticks},
        {"year", time_.year},
        {"day_of_year", time_.day_of_year},
        {"hour", time_.hour},
        {"minute", time_.minute},
        {"season", static_cast<int>(time_.season)},
        {"weather", static_cast<int>(time_.weather)},
        {"weather_intensity", time_.weather_intensity},
        {"time_scale", time_scale_},
        {"paused", paused_}
    };
}

void Simulation::deserialize(const nlohmann::json& j) {
    time_.ticks = j.value("ticks", 0);
    time_.year = j.value("year", 1);
    time_.day_of_year = j.value("day_of_year", 1);
    time_.hour = j.value("hour", 6);
    time_.minute = j.value("minute", 0);
    time_.season = static_cast<Season>(j.value("season", 0));
    time_.weather = static_cast<WeatherType>(j.value("weather", 0));
    time_.weather_intensity = j.value("weather_intensity", 0.0f);
    time_scale_ = j.value("time_scale", 1.0f);
    paused_ = j.value("paused", false);
}

} // namespace ashgrove