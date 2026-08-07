#include "world/world.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace ashgrove {

World::World() = default;

EntityID World::next_id() {
    return next_entity_id_++;
}

EntityID World::create_building(const Building& building) {
    EntityID id = next_id();
    buildings_[id] = building;
    buildings_[id].id = id;
    if (building.position.region_id != INVALID_ENTITY_ID) {
        auto* region = get_region(building.position.region_id);
        if (region) region->building_ids.push_back(id);
    }
    return id;
}

EntityID World::create_region(const Region& region) {
    EntityID id = next_id();
    regions_[id] = region;
    regions_[id].id = id;
    return id;
}

EntityID World::create_item(const Item& item) {
    EntityID id = next_id();
    items_[id] = item;
    items_[id].id = id;
    return id;
}

EntityID World::create_resource_deposit(const ResourceDeposit& deposit) {
    EntityID id = next_id();
    resource_deposits_[id] = deposit;
    resource_deposits_[id].id = id;
    return id;
}

EntityID World::create_npc(NPCPtr npc) {
    EntityID id = next_id();
    npc->id = id;
    npcs_[id] = npc;
    if (npc->position.region_id != INVALID_ENTITY_ID) {
        auto* region = get_region(npc->position.region_id);
        if (region) region->npc_ids.push_back(id);
    }
    return id;
}

void World::remove_building(EntityID id) {
    if (auto* b = get_building(id)) {
        for (auto& region : regions_) {
            auto& bid = region.second.building_ids;
            bid.erase(std::remove(bid.begin(), bid.end(), id), bid.end());
        }
    }
    buildings_.erase(id);
}

void World::remove_region(EntityID id) {
    // Remove references from other regions
    for (auto& region : regions_) {
        auto& conn = region.second.connected_region_ids;
        conn.erase(std::remove(conn.begin(), conn.end(), id), conn.end());
    }
    regions_.erase(id);
}

void World::remove_item(EntityID id) {
    items_.erase(id);
}

void World::remove_npc(EntityID id) {
    // Remove NPC from buildings
    for (auto& [bid, b] : buildings_) {
        auto& residents = b.resident_ids;
        residents.erase(std::remove(residents.begin(), residents.end(), id), residents.end());
        auto& workers = b.worker_ids;
        workers.erase(std::remove(workers.begin(), workers.end(), id), workers.end());
    }
    // Remove from regions
    for (auto& [rid, r] : regions_) {
        auto& npcs = r.npc_ids;
        npcs.erase(std::remove(npcs.begin(), npcs.end(), id), npcs.end());
    }
    // Remove relationships referencing this NPC
    for (auto& [nid, npc] : npcs_) {
        if (nid == id) continue;
        auto& rels = npc->relationships;
        rels.erase(std::remove_if(rels.begin(), rels.end(),
            [id](const Relationship& r) { return r.target_id == id; }), rels.end());
    }
    npcs_.erase(id);
}

Building* World::get_building(EntityID id) {
    auto it = buildings_.find(id);
    return it != buildings_.end() ? &it->second : nullptr;
}

const Building* World::get_building(EntityID id) const {
    auto it = buildings_.find(id);
    return it != buildings_.end() ? &it->second : nullptr;
}

Region* World::get_region(EntityID id) {
    auto it = regions_.find(id);
    return it != regions_.end() ? &it->second : nullptr;
}

const Region* World::get_region(EntityID id) const {
    auto it = regions_.find(id);
    return it != regions_.end() ? &it->second : nullptr;
}

Item* World::get_item(EntityID id) {
    auto it = items_.find(id);
    return it != items_.end() ? &it->second : nullptr;
}

const Item* World::get_item(EntityID id) const {
    auto it = items_.find(id);
    return it != items_.end() ? &it->second : nullptr;
}

NPCPtr World::get_npc(EntityID id) {
    auto it = npcs_.find(id);
    return it != npcs_.end() ? it->second : nullptr;
}

ResourceDeposit* World::get_resource_deposit(EntityID id) {
    auto it = resource_deposits_.find(id);
    return it != resource_deposits_.end() ? &it->second : nullptr;
}

std::vector<EntityID> World::get_buildings_in_region(EntityID region_id) const {
    auto it = regions_.find(region_id);
    if (it == regions_.end()) return {};
    return it->second.building_ids;
}

std::vector<EntityID> World::get_npcs_in_region(EntityID region_id) const {
    auto it = regions_.find(region_id);
    if (it == regions_.end()) return {};
    return it->second.npc_ids;
}

std::vector<EntityID> World::get_items_at_position(const Position& pos, float radius) const {
    std::vector<EntityID> result;
    for (const auto& [id, item] : items_) {
        if (item.container_id != INVALID_ENTITY_ID) continue; // Inside container
        float dx = item.position.x - pos.x;
        float dy = item.position.y - pos.y;
        if (std::sqrt(dx * dx + dy * dy) <= radius) result.push_back(id);
    }
    return result;
}

std::vector<EntityID> World::get_npcs_near_position(const Position& pos, float radius) const {
    std::vector<EntityID> result;
    for (const auto& [id, npc] : npcs_) {
        float dx = npc->position.x - pos.x;
        float dy = npc->position.y - pos.y;
        if (std::sqrt(dx * dx + dy * dy) <= radius) result.push_back(id);
    }
    return result;
}

std::vector<EntityID> World::get_buildings_near_position(const Position& pos, float radius) const {
    std::vector<EntityID> result;
    for (const auto& [id, b] : buildings_) {
        float dx = b.position.x - pos.x;
        float dy = b.position.y - pos.y;
        if (std::sqrt(dx * dx + dy * dy) <= radius) result.push_back(id);
    }
    return result;
}

std::vector<Position> World::find_path(const Position& from, const Position& to) const {
    // Simplified A* - straight line path for now, will be replaced with actual navigable graph
    std::vector<Position> path;
    path.push_back(from);
    path.push_back(to);
    return path;
}

float World::estimate_travel_time(const Position& from, const Position& to, const std::string& transport_mode) const {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    // Meters per minute approximations
    float speed = 80.0f;  // walking
    if (transport_mode == "bicycle") speed = 250.0f;
    else if (transport_mode == "bus") speed = 400.0f;
    else if (transport_mode == "train") speed = 1000.0f;
    
    return distance / speed;
}

float World::get_item_value(const std::string& item_name, EntityID region_id) const {
    // Base value with regional demand modifiers
    auto region = get_region(region_id);
    if (!region) return 1.0f;
    
    // Placeholder economic model
    float base_value = 10.0f;
    if (item_name.find("food") != std::string::npos) base_value = 5.0f;
    else if (item_name.find("tool") != std::string::npos) base_value = 15.0f;
    else if (item_name.find("weapon") != std::string::npos) base_value = 30.0f;
    else if (item_name.find("clothing") != std::string::npos) base_value = 12.0f;
    
    return base_value;
}

void World::adjust_local_economy(EntityID region_id, const std::string& item_name, float demand_change) {
    auto region = get_region(region_id);
    if (!region) return;
    // Placeholder economy implementation
    spdlog::debug("Economy adjusted in region {} for {} by {}", region_id, item_name, demand_change);
}

nlohmann::json World::serialize() const {
    nlohmann::json j;
    j["next_entity_id"] = next_entity_id_;
    
    j["buildings"] = nlohmann::json::array();
    for (const auto& [id, b] : buildings_) j["buildings"].push_back(b.serialize());
    
    j["regions"] = nlohmann::json::array();
    for (const auto& [id, r] : regions_) j["regions"].push_back(r.serialize());
    
    j["items"] = nlohmann::json::array();
    for (const auto& [id, item] : items_) j["items"].push_back(item.serialize());
    
    j["resource_deposits"] = nlohmann::json::array();
    for (const auto& [id, d] : resource_deposits_) {
        j["resource_deposits"].push_back({
            {"id", d.id},
            {"resource_name", d.resource_name},
            {"position", {{"x", d.position.x}, {"y", d.position.y}, {"z", d.position.z}}},
            {"amount", d.amount},
            {"max_amount", d.max_amount},
            {"regeneration_rate", d.regeneration_rate},
            {"depleted", d.depleted},
            {"region_id", d.region_id}
        });
    }
    
    j["npcs"] = nlohmann::json::array();
    for (const auto& [id, npc] : npcs_) j["npcs"].push_back(npc->serialize());
    
    return j;
}

void World::deserialize(const nlohmann::json& j) {
    next_entity_id_ = j.value("next_entity_id", 1);
    buildings_.clear();
    regions_.clear();
    items_.clear();
    resource_deposits_.clear();
    npcs_.clear();
    
    for (const auto& bj : j.value("buildings", nlohmann::json::array())) {
        auto b = Building::deserialize(bj);
        buildings_[b.id] = b;
    }
    for (const auto& rj : j.value("regions", nlohmann::json::array())) {
        auto r = Region::deserialize(rj);
        regions_[r.id] = r;
    }
    for (const auto& ij : j.value("items", nlohmann::json::array())) {
        auto item = Item::deserialize(ij);
        items_[item.id] = item;
    }
    for (const auto& dj : j.value("resource_deposits", nlohmann::json::array())) {
        ResourceDeposit d;
        d.id = dj.value("id", INVALID_ENTITY_ID);
        d.resource_name = dj.value("resource_name", "");
        d.position.x = dj["position"].value("x", 0.0f);
        d.position.y = dj["position"].value("y", 0.0f);
        d.position.z = dj["position"].value("z", 0.0f);
        d.amount = dj.value("amount", 100.0f);
        d.max_amount = dj.value("max_amount", 100.0f);
        d.regeneration_rate = dj.value("regeneration_rate", 0.1f);
        d.depleted = dj.value("depleted", false);
        d.region_id = dj.value("region_id", INVALID_ENTITY_ID);
        resource_deposits_[d.id] = d;
    }
    for (const auto& nj : j.value("npcs", nlohmann::json::array())) {
        auto npc = std::make_shared<NPC>();
        npc->deserialize(nj);
        npcs_[npc->id] = npc;
    }
}

nlohmann::json Building::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"name", name},
        {"type", static_cast<int>(type)},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}, {"region_id", position.region_id}}},
        {"bounds", {{"min_x", bounds.min_x}, {"min_y", bounds.min_y}, {"max_x", bounds.max_x}, {"max_y", bounds.max_y}}},
        {"owner_id", owner_id},
        {"resident_ids", resident_ids},
        {"worker_ids", worker_ids},
        {"condition", condition},
        {"value", value},
        {"level", level},
        {"description", description},
        {"constructed_at", constructed_at},
        {"last_maintained", last_maintained}
    };
}

Building Building::deserialize(const nlohmann::json& j) {
    Building b;
    b.id = j.value("id", INVALID_ENTITY_ID);
    b.name = j.value("name", "");
    b.type = static_cast<BuildingType>(j.value("type", 0));
    if (j.contains("position")) {
        b.position.x = j["position"].value("x", 0.0f);
        b.position.y = j["position"].value("y", 0.0f);
        b.position.z = j["position"].value("z", 0.0f);
        b.position.region_id = j["position"].value("region_id", INVALID_ENTITY_ID);
    }
    if (j.contains("bounds")) {
        b.bounds.min_x = j["bounds"].value("min_x", 0.0f);
        b.bounds.min_y = j["bounds"].value("min_y", 0.0f);
        b.bounds.max_x = j["bounds"].value("max_x", 0.0f);
        b.bounds.max_y = j["bounds"].value("max_y", 0.0f);
    }
    b.owner_id = j.value("owner_id", INVALID_ENTITY_ID);
    b.resident_ids = j.value("resident_ids", std::vector<EntityID>{});
    b.worker_ids = j.value("worker_ids", std::vector<EntityID>{});
    b.condition = j.value("condition", 1.0f);
    b.value = j.value("value", 0.0f);
    b.level = j.value("level", 1);
    b.description = j.value("description", "");
    b.constructed_at = j.value("constructed_at", 0);
    b.last_maintained = j.value("last_maintained", 0);
    return b;
}

nlohmann::json Region::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"name", name},
        {"type", static_cast<int>(type)},
        {"bounds", {{"min_x", bounds.min_x}, {"min_y", bounds.min_y}, {"max_x", bounds.max_x}, {"max_y", bounds.max_y}}},
        {"building_ids", building_ids},
        {"npc_ids", npc_ids},
        {"connected_region_ids", connected_region_ids},
        {"description", description},
        {"resources", resources},
        {"danger_level", danger_level}
    };
}

Region Region::deserialize(const nlohmann::json& j) {
    Region r;
    r.id = j.value("id", INVALID_ENTITY_ID);
    r.name = j.value("name", "");
    r.type = static_cast<RegionType>(j.value("type", 0));
    if (j.contains("bounds")) {
        r.bounds.min_x = j["bounds"].value("min_x", 0.0f);
        r.bounds.min_y = j["bounds"].value("min_y", 0.0f);
        r.bounds.max_x = j["bounds"].value("max_x", 0.0f);
        r.bounds.max_y = j["bounds"].value("max_y", 0.0f);
    }
    r.building_ids = j.value("building_ids", std::vector<EntityID>{});
    r.npc_ids = j.value("npc_ids", std::vector<EntityID>{});
    r.connected_region_ids = j.value("connected_region_ids", std::vector<EntityID>{});
    r.description = j.value("description", "");
    r.resources = j.value("resources", std::unordered_map<std::string, float>{});
    r.danger_level = j.value("danger_level", 0.0f);
    return r;
}

nlohmann::json Item::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"name", name},
        {"category", category},
        {"weight", weight},
        {"value", value},
        {"condition", condition},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}, {"region_id", position.region_id}}},
        {"properties", properties},
        {"owner_id", owner_id},
        {"container_id", container_id}
    };
}

Item Item::deserialize(const nlohmann::json& j) {
    Item item;
    item.id = j.value("id", INVALID_ENTITY_ID);
    item.name = j.value("name", "");
    item.category = j.value("category", "");
    item.weight = j.value("weight", 0.0f);
    item.value = j.value("value", 0.0f);
    item.condition = j.value("condition", 1.0f);
    if (j.contains("position")) {
        item.position.x = j["position"].value("x", 0.0f);
        item.position.y = j["position"].value("y", 0.0f);
        item.position.z = j["position"].value("z", 0.0f);
        item.position.region_id = j["position"].value("region_id", INVALID_ENTITY_ID);
    }
    item.properties = j.value("properties", std::unordered_map<std::string, std::string>{});
    item.owner_id = j.value("owner_id", INVALID_ENTITY_ID);
    item.container_id = j.value("container_id", INVALID_ENTITY_ID);
    return item;
}

} // namespace ashgrove