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

// Farming: Crop plots
enum class CropStage : uint8_t {
    Empty, Planted, Sprouting, Growing, Flowering, Ready, Withered
};

enum class CropType : uint8_t {
    None, Wheat, Carrots, Potatoes, Cabbage, Herbs, Flax, Turnips
};

struct CropPlot {
    EntityID id = INVALID_ENTITY_ID;
    Position position;
    EntityID region_id = INVALID_ENTITY_ID;
    CropType crop = CropType::None;
    CropStage stage = CropStage::Empty;
    float progress = 0.0f;           // 0-1, advances daily
    float quality = 1.0f;            // 0.5-2.0, affects yield
    float water_level = 0.5f;        // 0-1, needs watering
    float fertilizer = 0.0f;         // 0-1, bonus growth
    TimeTick planted_at = 0;
    TimeTick last_tended = 0;
    EntityID owner_id = INVALID_ENTITY_ID; // Who planted it
    
    nlohmann::json serialize() const;
    static CropPlot deserialize(const nlohmann::json& j);
};

// Fishing: Fishing spots
enum class FishType : uint8_t {
    None, Trout, Carp, Perch, Pike, Eel, Salmon, Catfish
};

struct FishingSpot {
    EntityID id = INVALID_ENTITY_ID;
    Position position;
    EntityID region_id = INVALID_ENTITY_ID;
    std::string name;
    float fish_density = 1.0f;       // 0-2, how many fish
    float water_quality = 1.0f;      // 0-1, affects fish quality
    std::vector<FishType> fish_types; // What can be caught here
    float difficulty = 1.0f;         // 0.5-2.0
    bool is_active = true;
    TimeTick last_fished = 0;
    float cooldown = 0.0f;           // Hours until restocked
    
    nlohmann::json serialize() const;
    static FishingSpot deserialize(const nlohmann::json& j);
};

// Jobs/Work: Work opportunities
enum class JobType : uint8_t {
    None, Farmhand, Fisher, Woodcutter, Miner, BlacksmithHelper, InnHelper, Guard, Courier, Herbalist
};

struct JobPosting {
    EntityID id = INVALID_ENTITY_ID;
    JobType type = JobType::None;
    std::string title;
    EntityID employer_id = INVALID_ENTITY_ID; // Building or NPC
    EntityID region_id = INVALID_ENTITY_ID;
    Position work_position;
    float wage_per_hour = 2.0f;      // Base pay
    float hours_per_shift = 8.0f;
    int max_workers = 1;
    std::vector<EntityID> worker_ids;
    std::string requirements;        // Skill requirements
    float reputation_req = 0.0f;     // Min reputation
    bool is_active = true;
    TimeTick posted_at = 0;
    TimeTick expires_at = 0;
    
    nlohmann::json serialize() const;
    static JobPosting deserialize(const nlohmann::json& j);
};

// Player skills
struct PlayerSkills {
    float farming = 0.0f;      // 0-100
    float fishing = 0.0f;
    float woodcutting = 0.0f;
    float mining = 0.0f;
    float smithing = 0.0f;
    float cooking = 0.0f;
    float herbalism = 0.0f;
    float crafting = 0.0f;
    float trading = 0.0f;
    float stealth = 0.0f;
    float perception = 0.0f;
    float combat = 0.0f;
    
    float get(JobType job) const;
    void add_xp(JobType job, float amount);
    nlohmann::json serialize() const;
    static PlayerSkills deserialize(const nlohmann::json& j);
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
    EntityID create_crop_plot(const CropPlot& plot);
    EntityID create_fishing_spot(const FishingSpot& spot);
    EntityID create_job_posting(const JobPosting& posting);
    
    void remove_building(EntityID id);
    void remove_region(EntityID id);
    void remove_item(EntityID id);
    void remove_npc(EntityID id);
    void remove_crop_plot(EntityID id);
    void remove_fishing_spot(EntityID id);
    void remove_job_posting(EntityID id);
    
    // Lookup
    Building* get_building(EntityID id);
    const Building* get_building(EntityID id) const;
    Region* get_region(EntityID id);
    const Region* get_region(EntityID id) const;
    Item* get_item(EntityID id);
    const Item* get_item(EntityID id) const;
    NPCPtr get_npc(EntityID id);
    ResourceDeposit* get_resource_deposit(EntityID id);
    CropPlot* get_crop_plot(EntityID id);
    FishingSpot* get_fishing_spot(EntityID id);
    JobPosting* get_job_posting(EntityID id);
    
    // Iteration accessors
    const std::unordered_map<EntityID, Region>& regions() const { return regions_; }
    
    // Queries
    std::vector<EntityID> get_buildings_in_region(EntityID region_id) const;
    std::vector<EntityID> get_npcs_in_region(EntityID region_id) const;
    std::vector<EntityID> get_items_at_position(const Position& pos, float radius = 1.0f) const;
    std::vector<EntityID> get_npcs_near_position(const Position& pos, float radius = 10.0f) const;
    std::vector<EntityID> get_buildings_near_position(const Position& pos, float radius = 50.0f) const;
    std::vector<EntityID> get_crop_plots_in_region(EntityID region_id) const;
    std::vector<EntityID> get_fishing_spots_in_region(EntityID region_id) const;
    std::vector<EntityID> get_active_jobs_in_region(EntityID region_id) const;
    
    // Full iteration access for simulation systems
    const std::unordered_map<EntityID, NPCPtr>& npcs() const { return npcs_; }
    std::unordered_map<EntityID, NPCPtr>& npcs() { return npcs_; }
    const std::unordered_map<EntityID, Item>& items() const { return items_; }
    std::unordered_map<EntityID, Item>& items() { return items_; }
    const std::unordered_map<EntityID, Building>& buildings() const { return buildings_; }
    std::unordered_map<EntityID, Building>& buildings() { return buildings_; }
    const std::unordered_map<EntityID, CropPlot>& crop_plots() const { return crop_plots_; }
    std::unordered_map<EntityID, CropPlot>& crop_plots() { return crop_plots_; }
    const std::unordered_map<EntityID, FishingSpot>& fishing_spots() const { return fishing_spots_; }
    std::unordered_map<EntityID, FishingSpot>& fishing_spots() { return fishing_spots_; }
    const std::unordered_map<EntityID, JobPosting>& job_postings() const { return job_postings_; }
    std::unordered_map<EntityID, JobPosting>& job_postings() { return job_postings_; }
    
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
    std::unordered_map<EntityID, CropPlot> crop_plots_;
    std::unordered_map<EntityID, FishingSpot> fishing_spots_;
    std::unordered_map<EntityID, JobPosting> job_postings_;
    std::unordered_map<EntityID, NPCPtr> npcs_;
};

} // namespace ashgrove