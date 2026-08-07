#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ashgrove {

class World;

struct PlayerActionRecord {
    TimeTick tick = 0;
    std::string verb;      // "move", "talk", "inspect", ...
    std::string summary;   // Human-readable description
    std::string result;    // Outcome / world feedback
};

// The player's presence in the world. Owned by GameServer.
class PlayerCharacter {
public:
    PlayerCharacter() = default;

    // Identity
    std::string name = "The Investigator";
    Position position;              // Current world position
    EntityID region_id = INVALID_ENTITY_ID; // Region the player is in

    // Vital stats (lightweight survival, per design)
    float health = 100.0f;
    float hunger = 0.0f;            // 0-100
    float fatigue = 0.0f;           // 0-100

    // Progression
    float reputation = 0.0f;        // Village-wide standing -100..100
    uint32_t level = 1;
    uint32_t xp = 0;

    // Inventory
    std::vector<EntityID> inventory;

    // Activity
    bool resting = false;
    TimeTick rest_start_tick = 0;
    std::string current_action = "idle";

    // Recent log for the UI
    std::vector<PlayerActionRecord> action_log;

    // Serialization
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json& j);

    // Movement
    bool move_to(Position target, EntityID new_region_id, World& world);
    void log_action(const std::string& verb, const std::string& summary, const std::string& result);
    void clear_stale_log(size_t max_entries = 50);

    // Inventory helpers
    bool owns_item(EntityID item_id) const;
    void add_item(EntityID item_id);
    void remove_item(EntityID item_id);
};

} // namespace ashgrove