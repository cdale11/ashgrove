#include "common/types.h"
#include <fmt/format.h>

namespace ashgrove {

std::string to_string(Season s) {
    switch (s) {
        case Season::Spring: return "Spring";
        case Season::Summer: return "Summer";
        case Season::Autumn: return "Autumn";
        case Season::Winter: return "Winter";
    }
    return "Unknown";
}

std::string to_string(WeatherType w) {
    switch (w) {
        case WeatherType::Clear: return "Clear";
        case WeatherType::Cloudy: return "Cloudy";
        case WeatherType::Rain: return "Rain";
        case WeatherType::Storm: return "Storm";
        case WeatherType::Snow: return "Snow";
        case WeatherType::Fog: return "Fog";
    }
    return "Unknown";
}

std::string to_string(TimeOfDay t) {
    switch (t) {
        case TimeOfDay::Dawn: return "Dawn";
        case TimeOfDay::Morning: return "Morning";
        case TimeOfDay::Noon: return "Noon";
        case TimeOfDay::Afternoon: return "Afternoon";
        case TimeOfDay::Evening: return "Evening";
        case TimeOfDay::Night: return "Night";
        case TimeOfDay::Midnight: return "Midnight";
    }
    return "Unknown";
}

std::string GameTime::to_string() const {
    return fmt::format("Year {} Day {} {:02d}:{:02d} {} {}",
        year, day_of_year, hour, minute,
        ashgrove::to_string(season), ashgrove::to_string(weather));
}

} // namespace ashgrove