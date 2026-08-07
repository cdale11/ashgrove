#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <variant>
#include <memory>

namespace ashgrove {

using EntityID = uint64_t;
using TimeTick = uint64_t;
using Temperature = float; // Celsius

constexpr EntityID INVALID_ENTITY_ID = 0;
constexpr EntityID PLAYER_OWNER_ID = 0xFFFFFFFFFFFFFFFFull; // items owned by the player (distinct from INVALID = unowned)
constexpr TimeTick INVALID_TIME = 0;

struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

enum class Season : uint8_t { Spring, Summer, Autumn, Winter };
enum class WeatherType : uint8_t { Clear, Cloudy, Rain, Storm, Snow, Fog };
enum class TimeOfDay : uint8_t { Dawn, Morning, Noon, Afternoon, Evening, Night, Midnight };

struct GameTime {
    TimeTick ticks = 0;           // Ticks since game start (1 tick = 1 minute)
    uint16_t year = 1;
    uint8_t day_of_year = 1;      // 1-360 (12 months * 30 days)
    uint8_t hour = 6;             // 0-23
    uint8_t minute = 0;           // 0-59
    Season season = Season::Spring;
    WeatherType weather = WeatherType::Clear;
    float weather_intensity = 0.0f; // 0.0-1.0
    
    TimeOfDay get_time_of_day() const {
        if (hour >= 5 && hour < 7) return TimeOfDay::Dawn;
        if (hour >= 7 && hour < 12) return TimeOfDay::Morning;
        if (hour >= 12 && hour < 13) return TimeOfDay::Noon;
        if (hour >= 13 && hour < 17) return TimeOfDay::Afternoon;
        if (hour >= 17 && hour < 20) return TimeOfDay::Evening;
        if (hour >= 20 && hour < 23) return TimeOfDay::Night;
        return TimeOfDay::Midnight;
    }
    
    bool is_daytime() const {
        auto tod = get_time_of_day();
        return tod != TimeOfDay::Night && tod != TimeOfDay::Midnight;
    }

    std::string to_string() const;
};

inline constexpr int TICKS_PER_MINUTE = 1;
inline constexpr int TICKS_PER_HOUR = 60;
inline constexpr int TICKS_PER_DAY = 1440;
inline constexpr int DAYS_PER_MONTH = 30;
inline constexpr int MONTHS_PER_YEAR = 12;
inline constexpr int DAYS_PER_YEAR = 360;

struct Position {
    float x, y, z; // World coordinates
    EntityID region_id = INVALID_ENTITY_ID; // Current region/building
    bool operator==(const Position&) const = default;
};

struct Bounds {
    float min_x, min_y, max_x, max_y;
    bool contains(const Position& pos) const {
        return pos.x >= min_x && pos.x <= max_x && pos.y >= min_y && pos.y <= max_y;
    }
};

// Component types for ECS-lite
struct Component {
    virtual ~Component() = default;
};

using ComponentPtr = std::shared_ptr<Component>;
using ComponentMap = std::unordered_map<std::string, ComponentPtr>;

std::string to_string(Season s);
std::string to_string(WeatherType w);
std::string to_string(TimeOfDay t);

} // namespace ashgrove