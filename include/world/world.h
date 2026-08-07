#pragma once

#include "common/types.h"
#include "npc/npc.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <nlohmann/json.hpp>

namespace ashgrove {

enum class BuildingType : uint8_t {
    Residential, Commercial, Industrial, Civic, Religious, Agricultural, Ruin
};

enum class RegionType : uint8_t {
    Village, Forest, River, Lake, Mountain, Cave, Road, Farmland
};

struct Building {
    EntityID id = INVALID_ENTITY_ID;
    std::string name;
    BuildingType type = BuildingType::Residential;
    Position position;
    Bounds bounds;
    EntityID owner_id = INVALID_ENTITY_ID;
    std::vector<EntityID> resident_ids;
    std::vector<EntityID> worker_ids;
    float condition = 1.0f;        // 0-1, decays over time
    float value = 0.0f;
    int level = 1;                 // Upgrades
    std::string description;
    TimeTick constructed_at = 0;
    TimeTick last_maintained = 0;
    
    nlohmann::json serialize() const;
    static Building deserialize(const nlohmann::json& j);
};

struct Region {
    EntityID id = INVALID_ENTITY_ID;
    std::string name;
    RegionType type = RegionType::Village;
    Bounds bounds;
    std::vector<EntityID> building_ids;
    std::vector<EntityID> npc_ids;
    std::vector<EntityID> connected_region_ids; // Adjacent regions
    std::string description;
    // Resources available in this region
    std::unordered_map<std::string, float> resources; // resource_name -> amount
    float danger_level = 0.0f; // 0-1
    
    nlohmann::json serialize() const;
    static Region deserialize(const nlohmann::json& j);
};

struct Item {
    EntityID id = INVALID_ENTITY_ID;
    std::string name;
    std::string category; // "food", "tool", "weapon", "material", "clothing", "book", "evidence"
    float weight = 0.0f;
    float value = 0.0f;
    float condition = 1.0f;
    Position position;    // Where the item is (for world items)
    std::unordered_map<std::string, std::string> properties; // Flexible key-value
    EntityID owner_id = INVALID_ENTITY_ID; // INVALID = on ground / in container
    EntityID container_id = INVALID_ENTITY_ID; // If inside another item (container)
    
    nlohmann::json serialize() const;
    static Item deserialize(const nlohmann::json& j);
};

struct ResourceDeposit {
    EntityID id = INVALID_ENTITY_ID;
    std::string resource_name; // "wood", "stone", "iron", "herbs", "fish"
    Position position;
    float amount = 100.0f;
    float max_amount = 100.0f;
    float regeneration_rate = 0.1f; // Per day
    bool depleted = false;
    EntityID region_id = INVALID_ENTITY_ID;
};

class World {
public:
    World();
    ~World() = default;
    
    // Entity management
    EntityID create_building(const Building& building);
    EntityID create_region(const Region& region);
    EntityID create_item(const Item& item);
    EntityID create_resource_deposit(const ResourceDeposit& deposit);
    EntityID create_npc(NPCPtr npc);
    
    void remove_building(EntityID id);
    void remove_region(EntityID id);
    void remove_item(EntityID id);
    void remove_npc(EntityID id);
    
    // Lookup
    Building* get_building(EntityID id);
    const Building* get_building(EntityID id) const;
    Region* get_region(EntityID id);
    const Region* get_region(EntityID id) const;
    Item* get_item(EntityID id);
    const Item* get_item(EntityID id) const;
    NPCPtr get_npc(EntityID id);
    ResourceDeposit* get_resource_deposit(EntityID id);
    
    // Iteration accessors
    const std::unordered_map<EntityID, Region>& regions() const { return regions_; }
    
    // Queries
    std::vector<EntityID> get_buildings_in_region(EntityID region_id) const;
    std::vector<EntityID> get_npcs_in_region(EntityID region_id) const;
    std::vector<EntityID> get_items_at_position(const Position& pos, float radius = 1.0f) const;
    std::vector<EntityID> get_npcs_near_position(const Position& pos, float radius = 10.0f) const;
    std::vector<EntityID> get_buildings_near_position(const Position& pos, float radius = 50.0f) const;
    
    // Pathfinding (simplified)
    std::vector<Position> find_path(const Position& from, const Position& to) const;
    float estimate_travel_time(const Position& from, const Position& to, const std::string& transport_mode) const;
    
    // Economy
    float get_item_value(const std::string& item_name, EntityID region_id) const;
    void adjust_local_economy(EntityID region_id, const std::string& item_name, float demand_change);
    
    // Serialization
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json& j);
    
    // ID generation
    EntityID next_id();
    
private:
    EntityID next_entity_id_ = 1;
    
    std::unordered_map<EntityID, Building> buildings_;
    std::unordered_map<EntityID, Region> regions_;
    std::unordered_map<EntityID, Item> items_;
    std::unordered_map<EntityID, ResourceDeposit> resource_deposits_;
    std::unordered_map<EntityID, NPCPtr> npcs_;
};

} // namespace ashgrove