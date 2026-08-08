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

EntityID World::create_crop_plot(const CropPlot& plot) {
    EntityID id = next_id();
    crop_plots_[id] = plot;
    crop_plots_[id].id = id;
    return id;
}

EntityID World::create_fishing_spot(const FishingSpot& spot) {
    EntityID id = next_id();
    fishing_spots_[id] = spot;
    fishing_spots_[id].id = id;
    return id;
}

EntityID World::create_job_posting(const JobPosting& posting) {
    EntityID id = next_id();
    job_postings_[id] = posting;
    job_postings_[id].id = id;
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

void World::remove_crop_plot(EntityID id) {
    crop_plots_.erase(id);
}

void World::remove_fishing_spot(EntityID id) {
    fishing_spots_.erase(id);
}

void World::remove_job_posting(EntityID id) {
    job_postings_.erase(id);
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

CropPlot* World::get_crop_plot(EntityID id) {
    auto it = crop_plots_.find(id);
    return it != crop_plots_.end() ? &it->second : nullptr;
}

FishingSpot* World::get_fishing_spot(EntityID id) {
    auto it = fishing_spots_.find(id);
    return it != fishing_spots_.end() ? &it->second : nullptr;
}

JobPosting* World::get_job_posting(EntityID id) {
    auto it = job_postings_.find(id);
    return it != job_postings_.end() ? &it->second : nullptr;
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

std::vector<EntityID> World::get_crop_plots_in_region(EntityID region_id) const {
    std::vector<EntityID> result;
    for (const auto& [id, plot] : crop_plots_) {
        if (plot.region_id == region_id) result.push_back(id);
    }
    return result;
}

std::vector<EntityID> World::get_fishing_spots_in_region(EntityID region_id) const {
    std::vector<EntityID> result;
    for (const auto& [id, spot] : fishing_spots_) {
        if (spot.region_id == region_id && spot.is_active) result.push_back(id);
    }
    return result;
}

std::vector<EntityID> World::get_active_jobs_in_region(EntityID region_id) const {
    std::vector<EntityID> result;
    for (const auto& [id, job] : job_postings_) {
        if (job.region_id == region_id && job.is_active && job.worker_ids.size() < job.max_workers) {
            result.push_back(id);
        }
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
    
    j["crop_plots"] = nlohmann::json::array();
    for (const auto& [id, p] : crop_plots_) j["crop_plots"].push_back(p.serialize());
    
    j["fishing_spots"] = nlohmann::json::array();
    for (const auto& [id, s] : fishing_spots_) j["fishing_spots"].push_back(s.serialize());
    
    j["job_postings"] = nlohmann::json::array();
    for (const auto& [id, jp] : job_postings_) j["job_postings"].push_back(jp.serialize());

    return j;
}

void World::deserialize(const nlohmann::json& j) {
    next_entity_id_ = j.value("next_entity_id", 1);
    buildings_.clear();
    regions_.clear();
    items_.clear();
    resource_deposits_.clear();
    crop_plots_.clear();
    fishing_spots_.clear();
    job_postings_.clear();
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
    for (const auto& pj : j.value("crop_plots", nlohmann::json::array())) {
        auto p = CropPlot::deserialize(pj);
        crop_plots_[p.id] = p;
    }
    for (const auto& fj : j.value("fishing_spots", nlohmann::json::array())) {
        auto f = FishingSpot::deserialize(fj);
        fishing_spots_[f.id] = f;
    }
    for (const auto& jj : j.value("job_postings", nlohmann::json::array())) {
        auto jp = JobPosting::deserialize(jj);
        job_postings_[jp.id] = jp;
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

nlohmann::json CropPlot::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}, {"region_id", region_id}}},
        {"crop", static_cast<int>(crop)},
        {"stage", static_cast<int>(stage)},
        {"progress", progress},
        {"quality", quality},
        {"water_level", water_level},
        {"fertilizer", fertilizer},
        {"planted_at", planted_at},
        {"last_tended", last_tended},
        {"owner_id", owner_id}
    };
}

CropPlot CropPlot::deserialize(const nlohmann::json& j) {
    CropPlot p;
    p.id = j.value("id", INVALID_ENTITY_ID);
    if (j.contains("position")) {
        p.position.x = j["position"].value("x", 0.0f);
        p.position.y = j["position"].value("y", 0.0f);
        p.position.z = j["position"].value("z", 0.0f);
        p.position.region_id = j["position"].value("region_id", INVALID_ENTITY_ID);
    }
    p.crop = static_cast<CropType>(j.value("crop", 0));
    p.stage = static_cast<CropStage>(j.value("stage", 0));
    p.progress = j.value("progress", 0.0f);
    p.quality = j.value("quality", 1.0f);
    p.water_level = j.value("water_level", 0.5f);
    p.fertilizer = j.value("fertilizer", 0.0f);
    p.planted_at = j.value("planted_at", 0);
    p.last_tended = j.value("last_tended", 0);
    p.owner_id = j.value("owner_id", INVALID_ENTITY_ID);
    return p;
}

nlohmann::json FishingSpot::serialize() const {
    std::vector<int> fish_types_int;
    for (auto ft : fish_types) fish_types_int.push_back(static_cast<int>(ft));
    return nlohmann::json{
        {"id", id},
        {"name", name},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}, {"region_id", region_id}}},
        {"fish_density", fish_density},
        {"water_quality", water_quality},
        {"fish_types", fish_types_int},
        {"difficulty", difficulty},
        {"is_active", is_active},
        {"last_fished", last_fished},
        {"cooldown", cooldown}
    };
}

FishingSpot FishingSpot::deserialize(const nlohmann::json& j) {
    FishingSpot s;
    s.id = j.value("id", INVALID_ENTITY_ID);
    s.name = j.value("name", "");
    if (j.contains("position")) {
        s.position.x = j["position"].value("x", 0.0f);
        s.position.y = j["position"].value("y", 0.0f);
        s.position.z = j["position"].value("z", 0.0f);
        s.position.region_id = j["position"].value("region_id", INVALID_ENTITY_ID);
    }
    s.fish_density = j.value("fish_density", 1.0f);
    s.water_quality = j.value("water_quality", 1.0f);
    s.difficulty = j.value("difficulty", 1.0f);
    s.is_active = j.value("is_active", true);
    s.last_fished = j.value("last_fished", 0);
    s.cooldown = j.value("cooldown", 0.0f);
    s.fish_types.clear();
    for (const auto& ft : j.value("fish_types", std::vector<int>{})) {
        s.fish_types.push_back(static_cast<FishType>(ft));
    }
    return s;
}

nlohmann::json JobPosting::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"type", static_cast<int>(type)},
        {"title", title},
        {"employer_id", employer_id},
        {"region_id", region_id},
        {"work_position", {{"x", work_position.x}, {"y", work_position.y}, {"z", work_position.z}}},
        {"wage_per_hour", wage_per_hour},
        {"hours_per_shift", hours_per_shift},
        {"max_workers", max_workers},
        {"worker_ids", worker_ids},
        {"requirements", requirements},
        {"reputation_req", reputation_req},
        {"is_active", is_active},
        {"posted_at", posted_at},
        {"expires_at", expires_at}
    };
}

JobPosting JobPosting::deserialize(const nlohmann::json& j) {
    JobPosting jp;
    jp.id = j.value("id", INVALID_ENTITY_ID);
    jp.type = static_cast<JobType>(j.value("type", 0));
    jp.title = j.value("title", "");
    jp.employer_id = j.value("employer_id", INVALID_ENTITY_ID);
    jp.region_id = j.value("region_id", INVALID_ENTITY_ID);
    if (j.contains("work_position")) {
        jp.work_position.x = j["work_position"].value("x", 0.0f);
        jp.work_position.y = j["work_position"].value("y", 0.0f);
        jp.work_position.z = j["work_position"].value("z", 0.0f);
    }
    jp.wage_per_hour = j.value("wage_per_hour", 2.0f);
    jp.hours_per_shift = j.value("hours_per_shift", 8.0f);
    jp.max_workers = j.value("max_workers", 1);
    jp.worker_ids = j.value("worker_ids", std::vector<EntityID>{});
    jp.requirements = j.value("requirements", "");
    jp.reputation_req = j.value("reputation_req", 0.0f);
    jp.is_active = j.value("is_active", true);
    jp.posted_at = j.value("posted_at", 0);
    jp.expires_at = j.value("expires_at", 0);
    return jp;
}

float PlayerSkills::get(JobType job) const {
    switch (job) {
        case JobType::Farmhand: return farming;
        case JobType::Fisher: return fishing;
        case JobType::Woodcutter: return woodcutting;
        case JobType::Miner: return mining;
        case JobType::BlacksmithHelper: return smithing;
        case JobType::InnHelper: return trading;
        case JobType::Guard: return combat;
        case JobType::Courier: return stealth;
        case JobType::Herbalist: return herbalism;
        default: return 0.0f;
    }
}

void PlayerSkills::add_xp(JobType job, float amount) {
    float* skill = nullptr;
    switch (job) {
        case JobType::Farmhand: skill = &farming; break;
        case JobType::Fisher: skill = &fishing; break;
        case JobType::Woodcutter: skill = &woodcutting; break;
        case JobType::Miner: skill = &mining; break;
        case JobType::BlacksmithHelper: skill = &smithing; break;
        case JobType::InnHelper: skill = &trading; break;
        case JobType::Guard: skill = &combat; break;
        case JobType::Courier: skill = &stealth; break;
        case JobType::Herbalist: skill = &herbalism; break;
        default: return;
    }
    if (skill) {
        *skill = std::min(100.0f, *skill + amount);
    }
}

nlohmann::json PlayerSkills::serialize() const {
    return nlohmann::json{
        {"farming", farming},
        {"fishing", fishing},
        {"woodcutting", woodcutting},
        {"mining", mining},
        {"smithing", smithing},
        {"cooking", cooking},
        {"herbalism", herbalism},
        {"crafting", crafting},
        {"trading", trading},
        {"stealth", stealth},
        {"perception", perception},
        {"combat", combat}
    };
}

PlayerSkills PlayerSkills::deserialize(const nlohmann::json& j) {
    PlayerSkills s;
    s.farming = j.value("farming", 0.0f);
    s.fishing = j.value("fishing", 0.0f);
    s.woodcutting = j.value("woodcutting", 0.0f);
    s.mining = j.value("mining", 0.0f);
    s.smithing = j.value("smithing", 0.0f);
    s.cooking = j.value("cooking", 0.0f);
    s.herbalism = j.value("herbalism", 0.0f);
    s.crafting = j.value("crafting", 0.0f);
    s.trading = j.value("trading", 0.0f);
    s.stealth = j.value("stealth", 0.0f);
    s.perception = j.value("perception", 0.0f);
    s.combat = j.value("combat", 0.0f);
    return s;
}

} // namespace ashgrove