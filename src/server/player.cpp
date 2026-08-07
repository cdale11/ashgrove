#include "server/player.h"
#include "world/world.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace ashgrove {

nlohmann::json PlayerCharacter::serialize() const {
    return nlohmann::json{
        {"name", name},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}}},
        {"region_id", region_id},
        {"health", health},
        {"hunger", hunger},
        {"fatigue", fatigue},
        {"reputation", reputation},
        {"level", level},
        {"xp", xp},
        {"inventory", inventory},
        {"resting", resting},
        {"current_action", current_action},
        {"action_log", [this]() {
            std::vector<nlohmann::json> log;
            for (const auto& rec : action_log) {
                log.push_back({{"tick", rec.tick}, {"verb", rec.verb}, {"summary", rec.summary}, {"result", rec.result}});
            }
            return log;
        }()}
    };
}

void PlayerCharacter::deserialize(const nlohmann::json& j) {
    name = j.value("name", "The Investigator");
    if (j.contains("position")) {
        position.x = j["position"].value("x", 0.0f);
        position.y = j["position"].value("y", 0.0f);
        position.z = j["position"].value("z", 0.0f);
    }
    region_id = j.value("region_id", INVALID_ENTITY_ID);
    health = j.value("health", 100.0f);
    hunger = j.value("hunger", 0.0f);
    fatigue = j.value("fatigue", 0.0f);
    reputation = j.value("reputation", 0.0f);
    level = j.value("level", 1);
    xp = j.value("xp", 0);
    inventory = j.value("inventory", std::vector<EntityID>{});
    resting = j.value("resting", false);
    current_action = j.value("current_action", "idle");
    action_log.clear();
    for (const auto& rec : j.value("action_log", std::vector<nlohmann::json>{})) {
        PlayerActionRecord r;
        r.tick = rec.value("tick", 0);
        r.verb = rec.value("verb", "");
        r.summary = rec.value("summary", "");
        r.result = rec.value("result", "");
        action_log.push_back(r);
    }
}

bool PlayerCharacter::move_to(Position target, EntityID new_region_id, World& world) {
    position = target;
    if (new_region_id != INVALID_ENTITY_ID) {
        region_id = new_region_id;
    }
    // Simple fatigue/travel cost
    fatigue = std::min(100.0f, fatigue + 2.0f);
    return true;
}

void PlayerCharacter::log_action(const std::string& verb, const std::string& summary, const std::string& result) {
    PlayerActionRecord rec;
    rec.tick = 0; // filled by caller with current tick
    rec.verb = verb;
    rec.summary = summary;
    rec.result = result;
    action_log.push_back(rec);
    clear_stale_log();
}

void PlayerCharacter::clear_stale_log(size_t max_entries) {
    if (action_log.size() > max_entries) {
        action_log.erase(action_log.begin(), action_log.begin() + (action_log.size() - max_entries));
    }
}

bool PlayerCharacter::owns_item(EntityID item_id) const {
    return std::find(inventory.begin(), inventory.end(), item_id) != inventory.end();
}

void PlayerCharacter::add_item(EntityID item_id) {
    if (!owns_item(item_id)) inventory.push_back(item_id);
}

void PlayerCharacter::remove_item(EntityID item_id) {
    inventory.erase(std::remove(inventory.begin(), inventory.end(), item_id), inventory.end());
}

} // namespace ashgrove