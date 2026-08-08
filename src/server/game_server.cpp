#include "server/game_server.h"
#include "npc/dialogue.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <limits>

namespace ashgrove {

static std::string emotion_label(EmotionType e) {
    switch (e) {
        case EmotionType::Neutral: return "Neutral";
        case EmotionType::Happy: return "Happy";
        case EmotionType::Sad: return "Sad";
        case EmotionType::Angry: return "Angry";
        case EmotionType::Fearful: return "Fearful";
        case EmotionType::Disgusted: return "Disgusted";
        case EmotionType::Surprised: return "Surprised";
        case EmotionType::Anxious: return "Anxious";
        case EmotionType::Content: return "Content";
        case EmotionType::Suspicious: return "Suspicious";
    }
    return "Neutral";
}

static std::string item_description(const Item& item) {
    auto it = item.properties.find("content");
    if (it != item.properties.end()) return it->second;
    return item.name + " — nothing else of note.";
}

// Shop inventory sold by the village shopkeeper (Ingrid). Keys are the goods identifiers.
struct ShopEntry {
    std::string key;        // client-facing goods id, e.g. "wheat_seeds"
    std::string name;       // item name when created
    std::string category;   // "seed", "food", ...
    float price;            // coins to buy
    float weight;           // kg
    std::string crop;       // if seed: crop the seed plants
    std::string description;
};

static const std::vector<ShopEntry> SHOP_CATALOG = {
    {"wheat_seeds", "wheat seeds", "seed", 2.0f, 0.05f, "wheat", "A pouch of wheat kernels for planting."},
    {"carrot_seeds", "carrot seeds", "seed", 2.0f, 0.05f, "carrots", "A pouch of carrot seeds for planting."},
    {"potato_seeds", "potato seeds", "seed", 3.0f, 0.1f, "potatoes", "A sack of seed potatoes."},
    {"cabbage_seeds", "cabbage seeds", "seed", 3.0f, 0.05f, "cabbage", "A pouch of cabbage seeds for planting."},
    {"herb_seeds", "herb seeds", "seed", 2.5f, 0.05f, "herbs", "A pouch of herb seeds for planting."},
    {"turnip_seeds", "turnip seeds", "seed", 1.5f, 0.05f, "turnips", "A pouch of hardy turnip seeds."},
    {"bread", "bread loaf", "food", 4.0f, 0.3f, "", "A dense bread loaf. Restores hunger when eaten."},
    {"ale", "ale flagon", "food", 5.0f, 0.6f, "", "A flagon of village ale. Eases the mind after a hard day."},
};

// Find the nearest shopkeeper NPC the player can trade with (same region, within range).
// Returns INVALID_ENTITY_ID and sets `reason` when out of range.
static EntityID find_trader(World& world, const PlayerCharacter& player, std::string& reason) {
    EntityID best_npc = INVALID_ENTITY_ID;
    float best_dist = 15.0f;
    for (EntityID nid : world.get_npcs_near_position(player.position, 1000.0f)) {
        auto npc = world.get_npc(nid);
        if (!npc || npc->occupation != "Shopkeeper") continue;
        if (npc->position.region_id != player.region_id) continue;
        const float dx = player.position.x - npc->position.x;
        const float dy = player.position.y - npc->position.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best_dist) {
            best_dist = dist;
            best_npc = nid;
        }
    }
    if (best_npc == INVALID_ENTITY_ID) {
        reason = "No trader is close enough. Look for Ingrid's shop in the village.";
        return INVALID_ENTITY_ID;
    }
    return best_npc;
}

GameServer::GameServer(const GameConfig& config)
    : config_(config),
      world_(std::make_shared<World>()),
      simulation_(std::make_unique<Simulation>()),
      investigation_(std::make_unique<InvestigationSystem>()),
      dialogue_(std::make_unique<DialogueSystem>()),
      llm_() {
    transport_ = std::make_shared<SocketTransport>();
    network_ = std::make_unique<NetworkServer>(transport_);
    if (config_.enable_llm) {
        LLMConfig llm_cfg;
        llm_cfg.url = config_.llm_url;
        if (!config_.llm_model_path.empty()) llm_cfg.model = config_.llm_model_path;
        llm_ = std::make_unique<LLMClient>(llm_cfg);
    }
}

GameServer::~GameServer() {
    shutdown();
}

bool GameServer::initialize() {
    simulation_->initialize(world_.get());
    
    // Set up network state provider
    network_->set_state_provider([this]() { return get_world_state(); });
    network_->set_action_handler([this](const nlohmann::json& action) {
        return handle_action(action);
    });
    
    // Create test world
    create_test_world();

    // Place the player at the village square
    player_.position = {0, 0, 0};
    player_.region_id = 1; // first region created = Ashgrove Village

    // Starting kit: a little coin for Ingrid's shop and a few free seeds to start farming.
    player_.money = 15.0f;
    const std::vector<std::string> starter_goods = {"wheat_seeds", "wheat_seeds", "carrot_seeds"};
    for (const auto& goods : starter_goods) {
        const auto it = std::find_if(SHOP_CATALOG.begin(), SHOP_CATALOG.end(),
                                     [&](const ShopEntry& e) { return e.key == goods; });
        if (it == SHOP_CATALOG.end()) continue;
        Item seed;
        seed.name = it->name;
        seed.category = it->category;
        seed.weight = it->weight;
        seed.value = it->price;
        seed.position = player_.position;
        seed.owner_id = 0;
        seed.properties["crop"] = it->crop;
        seed.properties["description"] = it->description;
        player_.add_item(world_->create_item(seed));
    }

    if (llm_) {
        if (llm_->ready()) {
            spdlog::info("LLM cognition connected to {}", config_.llm_url.c_str());
        } else {
            spdlog::warn("LLM enabled but could not connect to {}; dialogue will fall back to simulation text.", config_.llm_url.c_str());
        }
    }

    network_->start(config_.port);
    spdlog::info("Game server initialized on port {}", config_.port);
    return true;
}

void GameServer::shutdown() {
    stop();
    if (simulation_) simulation_->shutdown();
}

void GameServer::create_test_world() {
    spdlog::info("Creating test world...");
    
    // Village region
    Region village;
    village.name = "Ashgrove Village";
    village.type = RegionType::Village;
    village.bounds = {-500, -500, 500, 500};
    village.description = "A small, isolated mountain village. The houses cluster around a central square.";
    village.danger_level = 0.1f;
    EntityID village_id = world_->create_region(village);
    
    // Forest region
    Region forest;
    forest.name = "Whispering Woods";
    forest.type = RegionType::Forest;
    forest.bounds = {-1500, -1500, 1500, 1500};
    forest.description = "Dense forest surrounding the village. Locals say the trees whisper at night.";
    forest.danger_level = 0.4f;
    forest.resources = {{"wood", 500.0f}, {"herbs", 100.0f}, {"wildlife", 50.0f}};
    EntityID forest_id = world_->create_region(forest);
    village.connected_region_ids.push_back(forest_id);
    
    // River region
    Region river;
    river.name = "Silver River";
    river.type = RegionType::River;
    river.bounds = {-100, 100, 500, 150};
    river.description = "A cold, clear river running through the valley. Good for fishing.";
    river.resources = {{"fish", 200.0f}};
    EntityID river_id = world_->create_region(river);
    village.connected_region_ids.push_back(river_id);
    
    // Buildings
    Building tavern;
    tavern.name = "The Sleeping Fox Inn";
    tavern.type = BuildingType::Commercial;
    tavern.position = {0, 0, 0, village_id};
    tavern.bounds = {-5, -3, 5, 3};
    tavern.condition = 0.9f;
    tavern.value = 5000.0f;
    tavern.description = "The village tavern, where rumors are traded alongside ale.";
    EntityID tavern_id = world_->create_building(tavern);
    
    Building general_store;
    general_store.name = "Ashgrove General Store";
    general_store.type = BuildingType::Commercial;
    general_store.position = {15, 10, 0, village_id};
    general_store.bounds = {10, 8, 20, 12};
    general_store.condition = 0.85f;
    general_store.value = 4000.0f;
    general_store.description = "Sells goods and supplies. The owner keeps the town records.";
    world_->create_building(general_store);
    
    Building church;
    church.name = "St. Willow's Chapel";
    church.type = BuildingType::Religious;
    church.position = {-15, -10, 0, village_id};
    church.bounds = {-20, -13, -10, -7};
    church.condition = 0.75f;
    church.value = 8000.0f;
    church.description = "The old stone chapel. The pastor knows the village's deepest secrets.";
    world_->create_building(church);
    
    Building blacksmith;
    blacksmith.name = "Hartman's Forge";
    blacksmith.type = BuildingType::Industrial;
    blacksmith.position = {25, -5, 0, village_id};
    blacksmith.bounds = {22, -7, 28, -3};
    blacksmith.condition = 0.8f;
    blacksmith.value = 3000.0f;
    blacksmith.description = "The blacksmith's workshop. Tools and weapons are forged here.";
    world_->create_building(blacksmith);
    
    Building town_hall;
    town_hall.name = "Ashgrove Town Hall";
    town_hall.type = BuildingType::Civic;
    town_hall.position = {-5, 5, 0, village_id};
    town_hall.bounds = {-8, 3, -2, 7};
    town_hall.condition = 0.9f;
    town_hall.value = 10000.0f;
    town_hall.description = "The center of village administration. Records are kept here.";
    world_->create_building(town_hall);
    
    Building old_miller_house;
    old_miller_house.name = "The Old Mill House";
    old_miller_house.type = BuildingType::Residential;
    old_miller_house.position = {40, 20, 0, village_id};
    old_miller_house.bounds = {37, 18, 43, 22};
    old_miller_house.condition = 0.4f; // Decayed
    old_miller_house.value = 1500.0f;
    old_miller_house.description = "An abandoned mill house. The miller disappeared years ago.";
    world_->create_building(old_miller_house);
    
    // NPCs - the vertical slice core cast
    // Tier 1 NPCs (deeply simulated)
    auto mayor = std::make_shared<NPC>(INVALID_ENTITY_ID, "Elias", NPCTier::Tier1_Major);
    mayor->surname = "Thorne";
    mayor->age = 58;
    mayor->gender = "male";
    mayor->occupation = "Mayor";
    mayor->position = {-5, 5, 0, village_id};
    mayor->personality = {
        {"openness", 0.3f},
        {"conscientiousness", 0.8f},
        {"extraversion", 0.6f},
        {"agreeableness", 0.7f},
        {"neuroticism", 0.2f}
    };
    mayor->set_emotion(EmotionType::Content, 0.4f);
    mayor->add_belief({"The village must stay isolated", 0.9f, 0, 0, "Tradition", true});
    mayor->add_belief({"Outsiders bring trouble", 0.6f, 0, 0, "Experience", false});
    mayor->add_goal({"Keep the village's history hidden", 0.8f, 0.1f, 0, 0, "active", {}});
    mayor->add_memory({1, 0, "The last outsider left quickly after asking about the disappearance.", {INVALID_ENTITY_ID}, 0.7f, -0.3f, false, 1.0f, "witnessed"});
    world_->create_npc(mayor);
    
    auto pastor = std::make_shared<NPC>(INVALID_ENTITY_ID, "Father", NPCTier::Tier1_Major);
    pastor->surname = "Malcolm";
    pastor->age = 64;
    pastor->gender = "male";
    pastor->occupation = "Pastor";
    pastor->position = {-15, -10, 0, village_id};
    pastor->personality = {
        {"openness", 0.5f},
        {"conscientiousness", 0.7f},
        {"extraversion", 0.4f},
        {"agreeableness", 0.8f},
        {"neuroticism", 0.6f}
    };
    pastor->set_emotion(EmotionType::Anxious, 0.3f);
    pastor->add_belief({"The woods are not empty", 0.7f, 0, 0, "Dreams", true});
    pastor->add_goal({"Understand what happened in the woods", 0.6f, 0.2f, 0, 0, "active", {}});
    pastor->add_memory({2, 0, "I saw something moving between the trees on the night of the festival.", {}, 0.8f, -0.7f, false, 0.9f, "witnessed"});
    world_->create_npc(pastor);
    
    auto doctor = std::make_shared<NPC>(INVALID_ENTITY_ID, "Mara", NPCTier::Tier1_Major);
    doctor->surname = "Voss";
    doctor->age = 41;
    doctor->gender = "female";
    doctor->occupation = "Doctor";
    doctor->position = {20, 15, 0, village_id};
    doctor->personality = {
        {"openness", 0.7f},
        {"conscientiousness", 0.6f},
        {"extraversion", 0.5f},
        {"agreeableness", 0.5f},
        {"neuroticism", 0.4f}
    };
    doctor->set_emotion(EmotionType::Neutral, 0.0f);
    doctor->add_belief({"The disappearance was never properly investigated", 0.6f, 0, 0, "Records", false});
    doctor->add_goal({"Find the truth about the missing people", 0.7f, 0.0f, 0, 0, "active", {}});
    doctor->add_memory({3, 0, "Three people have disappeared from Ashgrove in the last decade. Each one left town suddenly.", {}, 0.9f, -0.5f, false, 1.0f, "medical records"});
    world_->create_npc(doctor);
    
    auto innkeeper = std::make_shared<NPC>(INVALID_ENTITY_ID, "Rosalind", NPCTier::Tier1_Major);
    innkeeper->surname = "Baker";
    innkeeper->age = 35;
    innkeeper->gender = "female";
    innkeeper->occupation = "Innkeeper";
    innkeeper->position = {0, 0, 0, village_id};
    innkeeper->personality = {
        {"openness", 0.6f},
        {"conscientiousness", 0.7f},
        {"extraversion", 0.8f},
        {"agreeableness", 0.6f},
        {"neuroticism", 0.3f}
    };
    innkeeper->set_emotion(EmotionType::Happy, 0.5f);
    innkeeper->add_belief({"People talk too much over ale", 0.8f, 0, 0, "Experience", false});
    innkeeper->add_goal({"Keep the inn running", 0.9f, 0.7f, 0, 0, "active", {}});
    world_->create_npc(innkeeper);
    
    // Tier 2 NPCs (moderate simulation)
    auto blacksmith_npc = std::make_shared<NPC>(INVALID_ENTITY_ID, "Tor", NPCTier::Tier2_Persistent);
    blacksmith_npc->surname = "Hartman";
    blacksmith_npc->age = 47;
    blacksmith_npc->gender = "male";
    blacksmith_npc->occupation = "Blacksmith";
    blacksmith_npc->position = {25, -5, 0, village_id};
    blacksmith_npc->personality = {
        {"openness", 0.3f},
        {"conscientiousness", 0.9f},
        {"extraversion", 0.3f},
        {"agreeableness", 0.4f},
        {"neuroticism", 0.5f}
    };
    blacksmith_npc->set_emotion(EmotionType::Neutral, 0.2f);
    blacksmith_npc->add_belief({"Hard work is the only truth", 0.9f, 0, 0, "Life experience", true});
    world_->create_npc(blacksmith_npc);
    
    auto shopkeeper = std::make_shared<NPC>(INVALID_ENTITY_ID, "Ingrid", NPCTier::Tier2_Persistent);
    shopkeeper->surname = "Weiss";
    shopkeeper->age = 52;
    shopkeeper->gender = "female";
    shopkeeper->occupation = "Shopkeeper";
    shopkeeper->position = {15, 10, 0, village_id};
    world_->create_npc(shopkeeper);
    
    // Tier 3 NPCs (background)
    for (int i = 0; i < 12; ++i) {
        auto villager = std::make_shared<NPC>(INVALID_ENTITY_ID, 
            "Villager " + std::to_string(i + 1), NPCTier::Tier3_Background);
        villager->age = 20 + (i * 3);
        villager->occupation = i % 3 == 0 ? "Farmer" : (i % 3 == 1 ? "Laborer" : "Housewife");
        villager->position = {-20 + (float)(i * 5), 30 + (float)(i % 4) * 10, 0, village_id};
        world_->create_npc(villager);
    }
    
    // Items
    Item journal;
    journal.name = "Old Miller's Journal";
    journal.category = "book";
    journal.weight = 0.5f;
    journal.value = 50.0f;
    journal.position = {40, 20, 0, village_id};
    journal.properties = {{"content", "The miller's last entry reads: 'They told me not to look at the river at night. I looked.'"}, {"evidence_tag", "miller_disappearance"}};
    world_->create_item(journal);
    
    Item rusted_key;
    rusted_key.name = "Rusted Key";
    rusted_key.category = "tool";
    rusted_key.weight = 0.1f;
    rusted_key.value = 10.0f;
    rusted_key.position = {15, 10, 0, village_id};
    rusted_key.properties = {{"fits", "old_mill_house"}, {"evidence_tag", "unknown_origin"}};
    world_->create_item(rusted_key);
    
    Item faded_photo;
    faded_photo.name = "Faded Photograph";
    faded_photo.category = "evidence";
    faded_photo.weight = 0.05f;
    faded_photo.value = 25.0f;
    faded_photo.position = {-15, -10, 0, village_id};
    faded_photo.properties = {{"depicts", "A group of villagers standing before a burnt building"}, {"evidence_tag", "miller_disappearance"}};
    world_->create_item(faded_photo);
    
    // Life systems: a crop plot near the mill, two fishing spots on the river, a job at the inn
    CropPlot plot;
    plot.position = {38, 16, 0, village_id};   // near the Old Mill House
    plot.region_id = village_id;
    plot.crop = CropType::Carrots;
    plot.stage = CropStage::Sprouting;
    plot.progress = 0.3f;
    plot.water_level = 0.8f;
    plot.planted_at = 0;
    plot.last_tended = 0;
    plot.owner_id = 0;
    world_->create_crop_plot(plot);

    // Thorne Farm: a proper field of empty plots for the player to farm.
    const struct { float x, y; } field_plots[] = {
        {30, 0}, {30, 4}, {34, 0}, {34, 4}, {30, -4}, {34, -4},
    };
    for (const auto& fp : field_plots) {
        CropPlot fp_plot;
        fp_plot.position = {fp.x, fp.y, 0, village_id};
        fp_plot.region_id = village_id;
        fp_plot.crop = CropType::None;
        fp_plot.stage = CropStage::Empty;
        fp_plot.water_level = 0.5f;
        fp_plot.owner_id = INVALID_ENTITY_ID;
        world_->create_crop_plot(fp_plot);
    }
    
    FishingSpot bend;
    bend.name = "The Old Bend";
    bend.position = {0, 60, 0, river_id};
    bend.region_id = river_id;
    bend.fish_density = 1.2f;
    bend.water_quality = 0.9f;
    bend.fish_types = {FishType::Trout, FishType::Perch, FishType::Pike};
    bend.difficulty = 0.8f;
    bend.cooldown = 2.0f;
    world_->create_fishing_spot(bend);
    
    FishingSpot reeds;
    reeds.name = "The Reeds";
    reeds.position = {30, 80, 0, river_id};
    reeds.region_id = river_id;
    reeds.fish_density = 1.5f;
    reeds.water_quality = 0.7f;
    reeds.fish_types = {FishType::Carp, FishType::Catfish, FishType::Eel};
    reeds.difficulty = 1.2f;
    reeds.cooldown = 3.0f;
    world_->create_fishing_spot(reeds);
    
    JobPosting farmhand;
    farmhand.type = JobType::Farmhand;
    farmhand.title = "Farmhand at Thorne Farm";
    farmhand.employer_id = tavern_id;
    farmhand.region_id = village_id;
    farmhand.work_position = {30, 0, 0};
    farmhand.wage_per_hour = 2.0f;
    farmhand.hours_per_shift = 6.0f;
    farmhand.max_workers = 2;
    farmhand.requirements = "Basic farming knowledge";
    farmhand.reputation_req = 0.0f;
    farmhand.is_active = true;
    farmhand.posted_at = 0;
    farmhand.expires_at = 0;
    world_->create_job_posting(farmhand);
    
    // Seed knowledge
    Knowledge village_history;
    village_history.id = 1;
    village_history.category = KnowledgeCategory::History;
    village_history.title = "The Great Fire";
    village_history.description = "Sixty years ago, a fire destroyed the northern quarter of the village. Official records say it was a chimney accident, but old stories mention something else.";
    village_history.completeness = 0.2f;
    investigation_->add_knowledge(village_history);
    
    Knowledge the_disappearance;
    the_disappearance.id = 2;
    the_disappearance.category = KnowledgeCategory::Mystery;
    the_disappearance.title = "The Disappearance";
    the_disappearance.description = "The old miller vanished without a trace ten years ago. The case was never solved. Villagers avoid the topic.";
    the_disappearance.completeness = 0.3f;
    investigation_->add_knowledge(the_disappearance);

    // Authored mystery chain — The Disappearance of the old miller.
    // Key steps: (1) pick up the miller's journal, (2) read the rusted key,
    // (3) talk to the elders until the picture is complete, (4) open the mill
    // house. Completeness of "The Disappearance" tracks investigation progress.
    Evidence miller_journal;
    miller_journal.id = 1;
    miller_journal.name = "Old Miller's Journal";
    miller_journal.description = "The miller's last entry reads: 'They told me not to look at the river at night. I looked.' His handwriting trails off mid-word.";
    miller_journal.tags = {"miller_disappearance", "miller_journal"};
    miller_journal.reliability = 0.9f;      // In the man's own hand
    miller_journal.related_location_id = 9; // The Old Mill House
    miller_journal.acquired_from = "The Old Mill House";
    investigation_->add_evidence(miller_journal);

    Evidence mill_key;
    mill_key.id = 2;
    mill_key.name = "Rusted Key";
    mill_key.description = "A heavy iron key, tarnished by river water. It fits the wards of the old mill house — and it was never on the miller's ring the night he vanished.";
    mill_key.tags = {"miller_disappearance", "rust_like_river"};
    mill_key.reliability = 0.7f;
    investigation_->add_evidence(mill_key);

    spdlog::info("Test world created with {} NPCs, {} buildings, {} regions", 
        world_->serialize()["npcs"].size(), world_->serialize()["buildings"].size(), world_->serialize()["regions"].size());

    // Assign each NPC a home anchor and a daily routine.
    build_npc_schedules();
}

static void add_schedule_entry(NPC& npc, uint8_t start_hour, uint8_t duration_hours,
                               const std::string& activity, EntityID location_id) {
    DailyScheduleEntry e;
    e.start_hour = start_hour;
    e.duration_hours = duration_hours;
    e.activity = activity;
    e.location_id = location_id;
    npc.schedule.push_back(e);
}

void GameServer::build_npc_schedules() {
    // Home anchor: where the NPC lives (fallback when a schedule has no location).
    npc_home_.clear();
    for (auto& [nid, npc] : world_->npcs()) {
        npc_home_[nid] = npc->position;
    }

    // Known places around the village.
    EntityID tavern_id = INVALID_ENTITY_ID;
    EntityID store_id = INVALID_ENTITY_ID;
    EntityID church_id = INVALID_ENTITY_ID;
    EntityID club_id = INVALID_ENTITY_ID;
    EntityID city_hall_id = INVALID_ENTITY_ID;
    for (auto& [bid, b] : world_->buildings()) {
        if (b.name.find("Sleeping Fox") != std::string::npos) tavern_id = bid;
        else if (b.name.find("General Store") != std::string::npos) store_id = bid;
        else if (b.name.find("St. Willow") != std::string::npos) church_id = bid;
        else if (b.name.find("Hartman") != std::string::npos) club_id = bid;
        else if (b.name.find("Town Hall") != std::string::npos) city_hall_id = bid;
    }

    for (auto& [nid, npc] : world_->npcs()) {
        const std::string& occ = npc->occupation;
        if (occ == "Mayor") {
            add_schedule_entry(*npc, 8, 8, "work", city_hall_id);      // 08-16 at the town hall
            add_schedule_entry(*npc, 16, 4, "socialize", tavern_id);   // 16-20 at the tavern
            add_schedule_entry(*npc, 20, 11, "sleep", INVALID_ENTITY_ID); // home
        } else if (occ == "Pastor") {
            add_schedule_entry(*npc, 5, 4, "pray", church_id);         // 05-09 at the chapel
            add_schedule_entry(*npc, 9, 6, "work", church_id);         // 09-15 chapel duties
            add_schedule_entry(*npc, 15, 3, "idle", INVALID_ENTITY_ID);// afternoon at home
            add_schedule_entry(*npc, 18, 2, "socialize", tavern_id);   // 18-20 town gossip
            add_schedule_entry(*npc, 20, 9, "sleep", INVALID_ENTITY_ID);
        } else if (occ == "Doctor") {
            add_schedule_entry(*npc, 8, 8, "work", city_hall_id);      // 08-16 clinic at the hall
            add_schedule_entry(*npc, 16, 2, "patrol", INVALID_ENTITY_ID); // house calls
            add_schedule_entry(*npc, 18, 2, "socialize", tavern_id);
            add_schedule_entry(*npc, 20, 12, "sleep", INVALID_ENTITY_ID);
        } else if (occ == "Innkeeper") {
            add_schedule_entry(*npc, 7, 3, "work", tavern_id);         // 07-10 morning prep
            add_schedule_entry(*npc, 10, 10, "work", tavern_id);       // 10-20 behind the bar
            add_schedule_entry(*npc, 20, 11, "sleep", INVALID_ENTITY_ID);
        } else if (occ == "Shopkeeper") {
            add_schedule_entry(*npc, 8, 10, "work", store_id);         // 08-18 at the store
            add_schedule_entry(*npc, 18, 2, "socialize", tavern_id);   // 18-20 evening round
            add_schedule_entry(*npc, 20, 12, "sleep", INVALID_ENTITY_ID);
        } else if (occ == "Blacksmith") {
            add_schedule_entry(*npc, 7, 9, "work", club_id);           // 07-16 at the forge
            add_schedule_entry(*npc, 16, 2, "idle", club_id);
            add_schedule_entry(*npc, 18, 2, "socialize", tavern_id);   // 18-20 ale at the inn
            add_schedule_entry(*npc, 20, 11, "sleep", INVALID_ENTITY_ID);
        } else {
            // Generic villagers: farm/labor all day, some go to the tavern in the evening.
            add_schedule_entry(*npc, 6, 11, "work", INVALID_ENTITY_ID);    // 06-17 the fields
            add_schedule_entry(*npc, 17, 3, "socialize", tavern_id);       // 17-20 inn
            add_schedule_entry(*npc, 20, 10, "sleep", INVALID_ENTITY_ID);
        }
    }
}

// Active schedule entry for `hour`, honoring entries that wrap past midnight.
static const DailyScheduleEntry* active_schedule_entry(const NPC& npc, uint8_t hour) {
    for (const auto& entry : npc.schedule) {
        uint8_t end_hour = (entry.start_hour + entry.duration_hours) % 24;
        bool covers = (entry.start_hour <= end_hour)
                          ? (hour >= entry.start_hour && hour < end_hour)
                          : (hour >= entry.start_hour || hour < end_hour);
        if (covers) return &entry;
    }
    return nullptr;
}

// Anchors that are used when a schedule entry has no location (INVALID).
// - generic villagers "work" at the Thorne Farm field instead of standing at home
// - "patrol" walks the village square
static void anchor_for_activity(const NPC& npc, const Position& home, const std::string& activity,
                                Position& out, bool& out_anchored) {
    if (activity == "work" && npc.occupation != "Doctor") {
        // Farmhands and laborers gather at Thorne Farm (30, 0 site).
        out = {30.0f + static_cast<float>(npc.id % 5) - 2.0f, 8.0f + static_cast<float>(npc.id % 4), 0.0f, home.region_id};
        out_anchored = true;
    } else if (activity == "patrol") {
        // Doctor's rounds: loop around the village square, settled on distinct corners.
        const uint8_t corner = static_cast<uint8_t>(npc.id % 4);
        out = {-3.0f + (corner == 1 || corner == 2 ? 6.0f : 0.0f),
               -2.0f + (corner >= 2 ? 4.0f : 0.0f), 0.0f, home.region_id};
        out_anchored = true;
    }
}

void GameServer::tick_npc_schedules(bool snap) {
    if (simulation_->is_paused()) return;
    const GameTime& gtime = simulation_->get_time();
    const float step = snap ? std::numeric_limits<float>::max() : npc_walk_speed_;

    for (auto& [nid, npc] : world_->npcs()) {
        const DailyScheduleEntry* entry = active_schedule_entry(*npc, gtime.hour);

        // Track what the character is currently doing (shown in the UI).
        npc->current_activity = entry ? entry->activity : "idle";

        Position dest;
        bool anchored = false;

        if (entry && entry->location_id != INVALID_ENTITY_ID) {
            const auto& buildings = world_->buildings();
            auto bit = buildings.find(entry->location_id);
            if (bit != buildings.end()) {
                dest = bit->second.position;
                anchored = true;
            }
        } else {
            Position home = npc->position;
            if (npc_home_.count(nid)) home = npc_home_.at(nid);
            anchor_for_activity(*npc, home, entry ? entry->activity : "idle", dest, anchored);
        }

        if (!anchored) {
            // Fall back to the NPC's home.
            if (npc_home_.count(nid)) dest = npc_home_.at(nid);
            else dest = npc->position; // Never seen a home: stay put.
        }

        // Walk toward the destination, one small step per game minute.
        const float dx = dest.x - npc->position.x;
        const float dy = dest.y - npc->position.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= step) {
            npc->position = dest;
        } else {
            npc->position.x += dx / dist * step;
            npc->position.y += dy / dist * step;
        }
    }
}

void GameServer::run() {
    running_ = true;
    spdlog::info("Game loop started (tick rate: {}ms, world time scale: {}x)", config_.tick_rate_ms, config_.world_time_scale);
    
    auto next_tick = std::chrono::steady_clock::now();
    
    while (running_) {
        std::this_thread::yield();
        
        auto now = std::chrono::steady_clock::now();
        if (now < next_tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        
        tick();
        next_tick += std::chrono::milliseconds(config_.tick_rate_ms);
    }
    
    spdlog::info("Main loop stopped");
}

void GameServer::run_async() {
    running_ = true;
    sim_thread_ = std::thread([this] { run(); });
}

void GameServer::stop() {
    running_ = false;
    if (sim_thread_.joinable()) {
        sim_thread_.join();
    }
    if (network_) network_->stop();
}

void GameServer::tick() {
    // Advance simulation
    simulation_->advance_time(config_.world_time_scale);
    tick_life_systems(config_.world_time_scale);
    tick_npc_schedules();
    
    // Periodic NPC AI updates
    TimeTick interval = static_cast<TimeTick>(config_.npc_ai_interval_ticks);
    if (interval > 0 && simulation_->get_time().ticks % interval == 0) {
        simulation_->process_npc_ai();
    }
    
    // Broadcast state to connected clients (throttled)
    static uint64_t frames = 0;
    frames++;
    if (frames % 10 == 0) {
        transport_->broadcast(get_world_state().dump());
    }
}

void GameServer::tick_life_systems(TimeTick elapsed_ticks) {
    const float days = static_cast<float>(elapsed_ticks) / 86400.0f;
    if (days <= 0.0f) return;

    // Resting: fatigue fades, hunger creeps up
    if (player_.resting) {
        player_.fatigue = std::max(0.0f, player_.fatigue - days * 30.0f);
        player_.hunger = std::min(100.0f, player_.hunger + days * 5.0f);
        if (player_.fatigue <= 0.0f) {
            player_.resting = false;
            player_.current_action = "idle";
            player_.log_action("rest_done", "Finished resting", "Rested, energy restored");
            player_.action_log.back().tick = simulation_->get_time().ticks;
        }
    } else {
        // Activity-based hunger
        player_.hunger = std::min(100.0f, player_.hunger + days * 2.5f);
        if (player_.hunger >= 100.0f) {
            player_.health = std::max(0.0f, player_.health - days * 4.0f);
        } else if (player_.health < 100.0f) {
            player_.health = std::min(100.0f, player_.health + days * 5.0f);
        }
    }
    // A bit of passive fatigue recovery even outside rest (only when idling)
    if (!player_.resting && player_.current_action == "idle") {
        player_.fatigue = std::max(0.0f, player_.fatigue - days * 8.0f);
    }

    // Crop growth stages (days to maturity per crop)
    constexpr float DAYS_TO_GROW[8] = {
        0.0f,  // None
        4.5f,  // Wheat
        3.0f,  // Carrots
        3.5f,  // Potatoes
        4.0f,  // Cabbage
        2.5f,  // Herbs
        3.5f,  // Flax
        3.0f,  // Turnips
    };

    const auto& gtime = simulation_->get_time();

    for (auto& [plot_id, plot] : world_->crop_plots()) {
        if (plot.stage == CropStage::Empty || plot.stage == CropStage::Withered) continue;
        if (plot.crop == CropType::None) continue;
        float grow_days = DAYS_TO_GROW[static_cast<int>(plot.crop)];
        if (grow_days <= 0.0f) continue;

        // Rain fills the soil: rain/~0.8 (or storm) per day, no need to water those days.
        float rain_fill = 0.0f;
        if (gtime.weather == WeatherType::Rain || gtime.weather == WeatherType::Storm) {
            rain_fill = (gtime.weather == WeatherType::Storm ? 0.9f : 0.6f) * gtime.weather_intensity;
        }
        plot.water_level = std::min(1.0f, plot.water_level + rain_fill * days);

        // Process day-by-day so watering, decay and growth interleave correctly.
        int full_days = static_cast<int>(days);
        float partial = days - full_days;
        for (int day = 0; day <= full_days; ++day) {
            float step = (day == full_days) ? partial : 1.0f;
            if (step <= 0.0f) continue;
            if (plot.stage == CropStage::Withered) break;

            plot.water_level = std::max(0.0f, plot.water_level - step * 0.35f);

            if (plot.water_level <= 0.0f) {
                plot.progress = std::max(0.0f, plot.progress - step * 0.06f);
                if (plot.progress <= 0.0f) {
                    plot.stage = CropStage::Withered;
                    break;
                }
                continue;
            }

            float growth_mult = 1.0f + plot.fertilizer * 1.5f + plot.water_level * 0.5f;
            plot.progress = std::min(1.0f, plot.progress + (step * growth_mult) / grow_days);

            if (plot.progress < 0.2f) plot.stage = CropStage::Planted;
            else if (plot.progress < 0.4f) plot.stage = CropStage::Sprouting;
            else if (plot.progress < 0.6f) plot.stage = CropStage::Growing;
            else if (plot.progress < 0.85f) plot.stage = CropStage::Flowering;
            else plot.stage = CropStage::Ready;
        }
    }
}

nlohmann::json GameServer::get_world_state() const {
    nlohmann::json state;
    state["time"] = simulation_->get_time().to_string();
    state["time_data"] = {
        {"ticks", simulation_->get_time().ticks},
        {"year", simulation_->get_time().year},
        {"day_of_year", simulation_->get_time().day_of_year},
        {"hour", simulation_->get_time().hour},
        {"minute", simulation_->get_time().minute},
        {"season", to_string(simulation_->get_time().season)},
        {"weather", to_string(simulation_->get_time().weather)},
        {"weather_intensity", simulation_->get_time().weather_intensity},
        {"insecurity", simulation_->get_insecurity()}
    };
    state["world"] = world_->serialize();
    state["investigation"] = investigation_->serialize();
    state["player"] = player_.serialize();
    nlohmann::json catalog = nlohmann::json::array();
    for (const auto& entry : SHOP_CATALOG) {
        catalog.push_back({{"key", entry.key},
                           {"name", entry.name},
                           {"category", entry.category},
                           {"price", entry.price},
                           {"weight", entry.weight}});
    }
    state["shop_catalog"] = catalog;
    return state;
}

nlohmann::json GameServer::action_error(const std::string& msg) {
    return {{"ok", false}, {"error", msg}};
}

EntityID GameServer::find_npc_at(const Position& pos, float radius) const {
    float best_dist = radius;
    EntityID best = INVALID_ENTITY_ID;
    for (EntityID nid : world_->get_npcs_near_position(pos, radius)) {
        if (auto npc = world_->get_npc(nid)) {
            if (npc->position.region_id != pos.region_id) continue;
            float dx = npc->position.x - pos.x;
            float dy = npc->position.y - pos.y;
            float dz = npc->position.z - pos.z;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < best_dist) { best_dist = dist; best = nid; }
        }
    }
    return best;
}

EntityID GameServer::find_item_at(const Position& pos, float radius) const {
    float best_dist = radius;
    EntityID best = INVALID_ENTITY_ID;
    for (EntityID iid : world_->get_items_at_position(pos, radius)) {
        if (auto item = world_->get_item(iid)) {
            if (item->owner_id != INVALID_ENTITY_ID) continue;
            if (item->position.region_id != pos.region_id) continue;
            float dx = item->position.x - pos.x;
            float dy = item->position.y - pos.y;
            float dz = item->position.z - pos.z;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < best_dist) { best_dist = dist; best = iid; }
        }
    }
    return best;
}

std::string GameServer::region_name(EntityID region_id) const {
    if (auto* r = world_->get_region(region_id)) return r->name;
    return "Unknown";
}

PlayerKnowledge GameServer::player_knowledge() const {
    PlayerKnowledge pk;
    // A knowledge title counts only once the player has actually learned it
    // (via dialogue unlocks etc.), even if the entry exists in the registry.
    for (const auto& k : investigation_->get_all_known()) {
        if (k.discovered_at > 0) {
            pk.knowledge_titles.push_back(k.title);
        }
    }
    // Evidence counts once it has been collected (acquired_at set).
    for (const auto& e : investigation_->get_all_evidence()) {
        if (e.acquired_at > 0) {
            pk.evidence_titles.push_back(e.name);
        }
    }
    pk.reputation = player_.reputation;
    return pk;
}

nlohmann::json GameServer::handle_action(const nlohmann::json& action) {
    std::string type = action.value("type", "");
    TimeTick tick = simulation_->get_time().ticks;

    if (type == "save") {
        bool ok = save_game();
        return {{"ok", ok}, {"message", ok ? "Game saved" : "Save failed"}};
    }
    if (type == "load") {
        bool ok = load_game();
        return {{"ok", ok}, {"message", ok ? "Game loaded" : "Load failed"}};
    }
    if (type == "get_state") {
        return get_world_state();
    }
    if (type == "move") {
        auto res = handle_move(action);
        res["player"] = player_.serialize();
        if (res["ok"]) {
            player_.log_action("move", "Moved to " + region_name(player_.region_id), "");
            player_.action_log.back().tick = tick;
        }
        return res;
    }
    if (type == "talk") {
        auto res = handle_talk(action);
        if (res.contains("player")) res["player"] = player_.serialize();
        return res;
    }
    if (type == "dialogue_topic") {
        auto res = handle_dialogue_topic(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "inspect") {
        auto res = handle_inspect(action);
        return res;
    }
    if (type == "pickup") {
        auto res = handle_pickup(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "use_item") {
        auto res = handle_use_item(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "drop_item") {
        auto res = handle_drop_item(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "rest") {
        auto res = handle_rest(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "enter") {
        auto res = handle_enter(action);
        res["player"] = player_.serialize();
        if (res["ok"]) {
            player_.log_action("enter", "Entered a building", "");
            player_.action_log.back().tick = tick;
        }
        return res;
    }
    if (type == "exit") {
        auto res = handle_exit(action);
        res["player"] = player_.serialize();
        if (res["ok"]) {
            player_.log_action("exit", "Left the building", "");
            player_.action_log.back().tick = tick;
        }
        return res;
    }
    if (type == "advance_time") {
        auto res = handle_advance_time(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "plant") {
        auto res = handle_plant(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "water") {
        auto res = handle_water(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "harvest") {
        auto res = handle_harvest(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "fish") {
        auto res = handle_fish(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "give") {
        auto res = handle_give(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "buy") {
        auto res = handle_buy(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "sell") {
        auto res = handle_sell(action);
        res["player"] = player_.serialize();
        return res;
    }
    if (type == "work") {
        auto res = handle_work(action);
        res["player"] = player_.serialize();
        return res;
    }

    return {{"error", "Unknown action type: " + type}};
}

nlohmann::json GameServer::handle_move(const nlohmann::json& action) {
    Position target;
    if (!action.contains("target")) {
        return action_error("Move requires a 'target' position");
    }
    const auto& t = action["target"];
    target.x = t.value("x", 0.0f);
    target.y = t.value("y", 0.0f);
    target.z = t.value("z", 0.0f);
    target.region_id = player_.region_id;

    // Region change via explicit region name
    std::string region = action.value("region", "");
    if (!region.empty()) {
        for (const auto& [rid, r] : world_->regions()) {
            if (r.name == region) { target.region_id = rid; break; }
        }
        if (target.region_id == player_.region_id && region != region_name(player_.region_id)) {
            return action_error("Region not found: " + region);
        }
    }

    player_.move_to(target, target.region_id, *world_);
    return {{"ok", true}, {"message", "Moved to " + region_name(player_.region_id)}};
}

nlohmann::json GameServer::handle_talk(const nlohmann::json& action) {
    EntityID target = action.value("target", INVALID_ENTITY_ID);
    auto npc = world_->get_npc(target);
    if (!npc) return action_error("No such NPC");
    if (npc->position.region_id != player_.region_id) {
        return action_error(npc->name + " is not in " + region_name(player_.region_id));
    }

    auto pk = player_knowledge();
    auto greeting = dialogue_->greeting(*npc);
    auto topics = dialogue_->topics_for(*npc, pk);

    // LLM layer rewrites the *text* only; simulation controls all effects.
    greeting.text = llm_rephrase(*npc, greeting.text, "Greetings.", "greeting");

    // Mark relationship familiarity
    if (auto* rel = npc->get_relationship(0)) {
        rel->familiarity = std::min(1.0f, rel->familiarity + 0.05f);
        rel->last_interaction = simulation_->get_time().ticks;
    } else {
        Relationship new_rel;
        new_rel.target_id = 0;
        new_rel.type = "stranger";
        new_rel.last_interaction = simulation_->get_time().ticks;
        new_rel.familiarity = 0.05f;
        npc->relationships.push_back(new_rel);
    }

    player_.current_action = "talking";

    // Insecurity: certain conversations at night
    const auto& time = simulation_->get_time();
    bool is_night = time.hour >= 21 || time.hour < 6;
    if (is_night) {
        // Pastor (id 10) — knows the village's deepest secrets
        if (target == 10) simulation_->add_insecurity(2.0f);
        // Doctor (id 14) — treats the afflicted
        else if (target == 14) simulation_->add_insecurity(1.5f);
        // Blacksmith (id 15) — works with cold iron
        else if (target == 15) simulation_->add_insecurity(1.0f);
    }

    std::vector<nlohmann::json> topic_json;
    for (const auto& tp : topics) topic_json.push_back(tp.serialize());
    return {{"ok", true}, {"speaker", npc->id}, {"speaker_name", npc->name}, {"line", greeting.serialize()}, {"topics", topic_json}};
}

nlohmann::json GameServer::handle_dialogue_topic(const nlohmann::json& action) {
    EntityID target = action.value("target", INVALID_ENTITY_ID);
    std::string topic_id = action.value("topic", "");
    auto npc = world_->get_npc(target);
    if (!npc) return action_error("No such NPC");
    if (npc->position.region_id != player_.region_id) {
        return action_error(npc->name + " is not here.");
    }

    auto pk = player_knowledge();
    auto line = dialogue_->respond(*npc, topic_id, pk);

    // LLM layer rewrites the *text* only; simulation controls all effects.
    line.text = llm_rephrase(*npc, line.text, "Tell me more about " + topic_id + ".", topic_id);

    // Apply validated simulation effects
    if (line.affinity_delta != 0.0f || line.trust_delta != 0.0f) {
        npc->modify_relationship(0, line.affinity_delta, line.trust_delta);
        player_.reputation = std::clamp(player_.reputation + line.affinity_delta * 5.0f, -100.0f, 100.0f);
    }
    for (const auto& title : line.knowledge_unlocked) {
        for (const auto& k : investigation_->get_all_known()) {
            if (k.title == title && investigation_->has_knowledge(k.id)) {
                if (auto* know = investigation_->get_knowledge(k.id)) {
                    know->completeness = std::min(1.0f, know->completeness + 0.2f);
                    if (std::find(know->source_npc_ids.begin(), know->source_npc_ids.end(), npc->id) == know->source_npc_ids.end()) {
                        know->source_npc_ids.push_back(npc->id);
                    }
                    know->discovered_at = simulation_->get_time().ticks;
                }
            }
        }
    }

    player_.log_action("talk", "Asked " + npc->name + " about '" + topic_id + "'", line.text);
    player_.action_log.back().tick = simulation_->get_time().ticks;

    auto topics = dialogue_->topics_for(*npc, player_knowledge());
    std::vector<nlohmann::json> topic_json;
    for (const auto& tp : topics) topic_json.push_back(tp.serialize());

    return {{"ok", true}, {"line", line.serialize()}, {"speaker", npc->id}, {"speaker_name", npc->name}, {"topics", topic_json}};
}

nlohmann::json GameServer::handle_inspect(const nlohmann::json& action) {
    EntityID target = action.value("target", INVALID_ENTITY_ID);
    std::string what = action.value("what", "npc");

    if (what == "item" || action.contains("item_id")) {
        EntityID iid = action.value("item_id", target);
        if (auto* item = world_->get_item(iid)) {
            if (item->position.region_id != player_.region_id && !player_.owns_item(iid)) {
                return action_error("Item not in this region.");
            }
            return {{"ok", true}, {"kind", "item"}, {"name", item->name}, {"category", item->category}, {"description", item_description(*item)}};
        }
        return action_error("No such item");
    }

    if (auto npc = world_->get_npc(target)) {
        if (npc->position.region_id != player_.region_id) {
            return action_error(npc->name + " is not in this region.");
        }
        nlohmann::json detail;
        detail["name"] = npc->name + (npc->surname.empty() ? "" : " " + npc->surname);
        detail["age"] = npc->age;
        detail["occupation"] = npc->occupation;
        detail["tier"] = static_cast<int>(npc->tier);
        detail["emotion"] = emotion_label(npc->current_emotion);
        detail["activity"] = npc->current_activity;
        detail["beliefs"] = std::vector<std::string>();
        for (const auto& b : npc->beliefs) detail["beliefs"].push_back(b.proposition);
        detail["goals"] = std::vector<std::string>();
        for (const auto& g : npc->goals) detail["goals"].push_back(g.description);
        if (auto* rel = npc->get_relationship(0)) {
            detail["affinity"] = rel->affinity;
            detail["trust"] = rel->trust;
            detail["familiarity"] = rel->familiarity;
        }
        player_.log_action("inspect", "Studied " + npc->name, npc->occupation + ", emotion: " + emotion_label(npc->current_emotion));
        return {{"ok", true}, {"kind", "npc"}, {"detail", detail}};
    }

    return action_error("Nothing to inspect there");
}

nlohmann::json GameServer::handle_pickup(const nlohmann::json& action) {
    EntityID target = action.value("target", INVALID_ENTITY_ID);
    auto* item = world_->get_item(target);
    if (!item) return action_error("No such item");
    if (item->owner_id != INVALID_ENTITY_ID) return action_error("That item is already owned.");
    if (item->position.region_id != player_.region_id) {
        return action_error("Item is not in this region.");
    }
    if (player_.owns_item(target)) return action_error("You already have it.");

    float dx = item->position.x - player_.position.x;
    float dy = item->position.y - player_.position.y;
    float dz = item->position.z - player_.position.z;
    if (std::sqrt(dx*dx + dy*dy + dz*dz) > 5.0f) {
        return action_error("Too far away to pick up. Move closer.");
    }

    item->owner_id = PLAYER_OWNER_ID; // player
    player_.add_item(target);

    // Evidence items progress the authored mystery chain.
    // The world item's "evidence_tag" ties it to an Evidence record; owning
    // the journal and key (their exact names) pushes the case forward.
    bool evidence_linked = false;
    for (auto* ev : investigation_->all_evidence()) {
        if (!ev || ev->id == 0) continue;
        if (ev->name == item->name) {
            if (ev->acquired_at == 0) {
                // Fresh find: tag the moment.
                ev->acquired_at = simulation_->get_time().ticks;
                auto* k = investigation_->get_knowledge(2); // The Disappearance
                if (k) {
                    k->completeness = std::min(1.0f, k->completeness + 0.2f);
                    if (std::find(k->source_item_ids.begin(), k->source_item_ids.end(), item->id) == k->source_item_ids.end()) {
                        k->source_item_ids.push_back(item->id);
                    }
                }
                spdlog::info("Player collected evidence: {}", ev->name);
            }
            evidence_linked = true;
            break;
        }
    }
    if (!evidence_linked) {
        auto tag = item->properties.find("evidence_tag");
        if (tag != item->properties.end()) {
            Evidence ev;
            ev.id = 900 + item->id;
            ev.name = item->name;
            ev.tags = {tag->second};
            ev.reliability = 0.6f;
            ev.acquired_from = "Found in " + region_name(player_.region_id);
            ev.acquired_at = simulation_->get_time().ticks;
            investigation_->add_evidence(ev);
            spdlog::info("New evidence tagged: {}", item->name);
        }
    }

    player_.log_action("pickup", "Picked up " + item->name, item_description(*item));
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "Picked up " + item->name}};
}

nlohmann::json GameServer::handle_use_item(const nlohmann::json& action) {
    EntityID target = action.value("target", INVALID_ENTITY_ID);
    if (!player_.owns_item(target)) return action_error("You don't have that item.");
    auto* item = world_->get_item(target);
    if (!item) return action_error("Item no longer exists.");

    if (item->category == "food") {
        player_.hunger = std::max(0.0f, player_.hunger - 30.0f);
        player_.remove_item(target);
        return {{"ok", true}, {"message", "You eat the " + item->name + "."}};
    }
    return {{"ok", true}, {"message", "You examine the " + item->name + ". Nothing else comes to mind."}};
}

nlohmann::json GameServer::handle_drop_item(const nlohmann::json& action) {
    EntityID target = action.value("target", INVALID_ENTITY_ID);
    if (!player_.owns_item(target)) return action_error("You don't have that item.");
    auto* item = world_->get_item(target);
    if (!item) return action_error("Item no longer exists.");

    // Drop at player's feet
    item->owner_id = INVALID_ENTITY_ID;
    item->position = player_.position;
    item->position.z = 0;
    player_.remove_item(target);
    player_.log_action("drop", "Dropped " + item->name, item_description(*item));
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "Dropped " + item->name}};
}

nlohmann::json GameServer::handle_rest(const nlohmann::json& action) {
    player_.resting = true;
    player_.current_action = "resting";
    player_.rest_start_tick = simulation_->get_time().ticks;
    player_.log_action("rest", "Sat down to rest", "Fatigue slowly fades.");
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "You rest. Time passes..."}};
}

nlohmann::json GameServer::handle_enter(const nlohmann::json& action) {
    if (player_.resting) {
        return action_error("You are resting; finish resting first.");
    }
    if (player_.interior_id != INVALID_ENTITY_ID) {
        return action_error("You are already inside a building.");
    }
    if (!action.contains("target")) {
        return action_error("Enter requires a 'target' building id");
    }
    EntityID target = action["target"].value("id", INVALID_ENTITY_ID);
    if (target == INVALID_ENTITY_ID) target = action.value("target", INVALID_ENTITY_ID);

    auto* building = world_->get_building(target);
    if (!building) return action_error("That building does not exist.");

    // Must be in the same region and close to the building's bounds.
    if (building->position.region_id != player_.region_id) {
        return action_error("That building is not in this region.");
    }
    const bool near_x = player_.position.x >= building->bounds.min_x - 15 &&
                        player_.position.x <= building->bounds.max_x + 15;
    const bool near_y = player_.position.y >= building->bounds.min_y - 15 &&
                        player_.position.y <= building->bounds.max_y + 15;
    if (!near_x || !near_y) {
        return action_error("Move closer to the entrance first.");
    }

    player_.interior_id = target;

    // Insecurity spikes: certain places at certain times
    const auto& time = simulation_->get_time();
    bool is_night = time.hour >= 21 || time.hour < 6;
    bool is_twilight = time.hour >= 19 || time.hour < 7;
    
    // The Old Mill House (id 9) — the disappearance site
    if (target == 9) {
        simulation_->add_insecurity(is_night ? 4.0f : is_twilight ? 2.0f : 1.0f);
    }
    // St. Willow's Chapel (id 6) at night — the pastor knows secrets
    else if (target == 6 && (is_night || is_twilight)) {
        simulation_->add_insecurity(2.5f);
    }
    // Hartman's Forge (id 7) at night — the smith works late with strange metals
    else if (target == 7 && is_night) {
        simulation_->add_insecurity(1.5f);
    }
    
    return {{"ok", true},
            {"message", "You step inside " + building->name + "."},
            {"building_id", target}};
}

nlohmann::json GameServer::handle_exit(const nlohmann::json& action) {
    (void)action;
    if (player_.interior_id == INVALID_ENTITY_ID) {
        return action_error("You are not inside a building.");
    }
    player_.interior_id = INVALID_ENTITY_ID;
    return {{"ok", true}, {"message", "You step back outside."}};
}

nlohmann::json GameServer::handle_advance_time(const nlohmann::json& action) {
    double hours = action.value("hours", 1.0);
    if (hours <= 0 || hours > 168) {
        return action_error("Hours must be between 0.1 and 168 (one week).");
    }
    TimeTick ticks = static_cast<TimeTick>(hours * 3600);
    simulation_->advance_time(ticks);
    tick_life_systems(ticks);
    // After a large jump, put everyone where their schedule says (no walking montage).
    if (ticks >= TICKS_PER_HOUR) tick_npc_schedules(true);
    const auto& time = simulation_->get_time();
    std::string msg = "Advanced " + std::to_string(hours) + " hour" + (hours != 1.0 ? "s" : "") + ". Now " +
                      std::to_string(time.hour) + ":" + (time.minute < 10 ? "0" : "") + std::to_string(time.minute) +
                      ", " + to_string(time.season) + " Day " + std::to_string(time.day_of_year) +
                      ", Year " + std::to_string(time.year) + ".";
    return {{"ok", true},
            {"message", msg},
            {"time_data", {
                {"ticks", time.ticks},
                {"year", time.year},
                {"day_of_year", time.day_of_year},
                {"hour", time.hour},
                {"minute", time.minute},
                {"season", to_string(time.season)},
                {"weather", to_string(time.weather)},
                {"weather_intensity", time.weather_intensity}
            }}};
}

nlohmann::json GameServer::handle_plant(const nlohmann::json& action) {
    if (player_.resting) return action_error("You are resting; finish resting first.");
    EntityID plot_id = action.value("target", INVALID_ENTITY_ID);
    auto* plot = world_->get_crop_plot(plot_id);
    if (!plot) return action_error("That crop plot does not exist.");
    if (plot->region_id != player_.region_id) return action_error("That plot is in another region.");
    const float dx = player_.position.x - plot->position.x;
    const float dy = player_.position.y - plot->position.y;
    if (std::sqrt(dx * dx + dy * dy) > 12.0f) return action_error("Move closer to the plot first.");
    if (plot->stage != CropStage::Empty && plot->stage != CropStage::Withered) return action_error("Something is already growing here.");
    std::string crop_str = action.value("crop", "wheat");
    CropType crop = CropType::Wheat;
    if (crop_str == "carrots") crop = CropType::Carrots;
    else if (crop_str == "potatoes") crop = CropType::Potatoes;
    else if (crop_str == "cabbage") crop = CropType::Cabbage;
    else if (crop_str == "herbs") crop = CropType::Herbs;
    else if (crop_str == "flax") crop = CropType::Flax;
    else if (crop_str == "turnips") crop = CropType::Turnips;
    else if (crop_str != "wheat") return action_error("Unknown crop: " + crop_str);

    // Planting requires seeds, which Ingrid sells at her shop.
    EntityID seed_id = INVALID_ENTITY_ID;
    for (EntityID iid : player_.inventory) {
        const Item* it = world_->get_item(iid);
        if (it && it->category == "seed") {
            auto cit = it->properties.find("crop");
            if (cit != it->properties.end() && cit->second == crop_str) {
                seed_id = iid;
                break;
            }
        }
    }
    if (seed_id == INVALID_ENTITY_ID) {
        return action_error("You need " + crop_str + " seeds to plant here. Ingrid sells them at her shop.");
    }
    player_.remove_item(seed_id);
    world_->remove_item(seed_id);

    plot->crop = crop;
    plot->stage = CropStage::Planted;
    plot->progress = 0.0f;
    plot->quality = 1.0f;
    plot->water_level = 0.3f;
    plot->fertilizer = 0.0f;
    plot->planted_at = simulation_->get_time().ticks;
    plot->last_tended = simulation_->get_time().ticks;
    plot->owner_id = 0;
    player_.skills.add_xp(JobType::Farmhand, 2.0f);
    player_.log_action("plant", "Planted " + crop_str + " seeds", "Planted in plot " + std::to_string(plot_id));
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "You plant " + crop_str + " in the plot."}};
}

nlohmann::json GameServer::handle_water(const nlohmann::json& action) {
    if (player_.resting) return action_error("You are resting; finish resting first.");
    EntityID plot_id = action.value("target", INVALID_ENTITY_ID);
    auto* plot = world_->get_crop_plot(plot_id);
    if (!plot) return action_error("That crop plot does not exist.");
    if (plot->region_id != player_.region_id) return action_error("That plot is in another region.");
    const float dx = player_.position.x - plot->position.x;
    const float dy = player_.position.y - plot->position.y;
    if (std::sqrt(dx * dx + dy * dy) > 12.0f) return action_error("Move closer to the plot first.");
    if (plot->stage == CropStage::Empty || plot->stage == CropStage::Withered) {
        return action_error("There is nothing to water here.");
    }
    if (plot->water_level >= 1.0f) return action_error("The soil is already saturated.");
    plot->water_level = std::min(1.0f, plot->water_level + 0.5f);
    plot->last_tended = simulation_->get_time().ticks;
    player_.log_action("water", "Watered a crop plot", "Plot " + std::to_string(plot_id));
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "You water the plot. The soil drinks it down."}};
}

nlohmann::json GameServer::handle_harvest(const nlohmann::json& action) {
    if (player_.resting) return action_error("You are resting; finish resting first.");
    EntityID plot_id = action.value("target", INVALID_ENTITY_ID);
    auto* plot = world_->get_crop_plot(plot_id);
    if (!plot) return action_error("That crop plot does not exist.");
    if (plot->region_id != player_.region_id) return action_error("That plot is in another region.");
    const float dx = player_.position.x - plot->position.x;
    const float dy = player_.position.y - plot->position.y;
    if (std::sqrt(dx * dx + dy * dy) > 12.0f) return action_error("Move closer to the plot first.");
    if (plot->stage != CropStage::Ready) {
        return action_error(plot->stage == CropStage::Empty ? "There is nothing to harvest." : "The crop is not ready yet.");
    }

    std::string crop_str = "produce";
    switch (plot->crop) {
        case CropType::Wheat: crop_str = "wheat"; break;
        case CropType::Carrots: crop_str = "carrots"; break;
        case CropType::Potatoes: crop_str = "potatoes"; break;
        case CropType::Cabbage: crop_str = "cabbage"; break;
        case CropType::Herbs: crop_str = "herbs"; break;
        case CropType::Flax: crop_str = "flax"; break;
        case CropType::Turnips: crop_str = "turnips"; break;
        default: break;
    }

    float skill = player_.skills.farming;
    float yield_mult = 1.0f + skill / 200.0f + plot->quality * 0.25f;
    int count = std::max(1, static_cast<int>(std::round(yield_mult)));

    Item produce;
    produce.name = crop_str + " bundle";
    produce.category = "food";
    produce.weight = 0.5f * count;
    produce.value = 2.0f * count;
    produce.position = player_.position;
    produce.owner_id = 0;
    EntityID produce_id = world_->create_item(produce);
    player_.add_item(produce_id);
    player_.skills.add_xp(JobType::Farmhand, 5.0f);

    plot->crop = CropType::None;
    plot->stage = CropStage::Empty;
    plot->progress = 0.0f;
    plot->water_level = 0.0f;
    plot->last_tended = simulation_->get_time().ticks;

    player_.log_action("harvest", "Harvested " + produce.name, "Gained " + produce.name);
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "You harvest " + crop_str + ". It goes into your pack."}, {"item_id", produce_id}};
}

nlohmann::json GameServer::handle_fish(const nlohmann::json& action) {
    if (player_.resting) return action_error("You are resting; finish resting first.");
    EntityID spot_id = action.value("target", INVALID_ENTITY_ID);
    auto* spot = world_->get_fishing_spot(spot_id);
    if (!spot) return action_error("That fishing spot does not exist.");
    if (spot->region_id != player_.region_id) return action_error("That spot is in another region.");
    const float dx = player_.position.x - spot->position.x;
    const float dy = player_.position.y - spot->position.y;
    if (std::sqrt(dx * dx + dy * dy) > 20.0f) return action_error("Move closer to the water first.");
    if (!spot->is_active) return action_error("The water here is dead and still. No fish remain.");

    const auto& time = simulation_->get_time();
    if (spot->last_fished > 0) {
        double elapsed_hours = (time.ticks - spot->last_fished) / 3600.0;
        if (elapsed_hours < spot->cooldown) {
            return action_error("The fish need time to return here.");
        }
    }
    spot->last_fished = time.ticks;

    float skill = player_.skills.fishing;
    float chance = std::min(0.95f, 0.3f + skill / 250.0f + spot->fish_density * 0.1f - spot->difficulty * 0.05f);
    float roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    if (roll > chance) {
        player_.skills.add_xp(JobType::Fisher, 1.5f);
        player_.log_action("fish", "Fished at " + spot->name, "Nothing biting");
        return {{"ok", true}, {"message", "You fish for a while... nothing bites."}};
    }

    FishType caught = FishType::Trout;
    if (!spot->fish_types.empty()) {
        caught = spot->fish_types[rand() % spot->fish_types.size()];
    }
    std::string fish_str = "fish";
    switch (caught) {
        case FishType::Trout: fish_str = "trout"; break;
        case FishType::Carp: fish_str = "carp"; break;
        case FishType::Perch: fish_str = "perch"; break;
        case FishType::Pike: fish_str = "pike"; break;
        case FishType::Eel: fish_str = "eel"; break;
        case FishType::Salmon: fish_str = "salmon"; break;
        case FishType::Catfish: fish_str = "catfish"; break;
        default: break;
    }

    Item catch_item;
    catch_item.name = fish_str;
    catch_item.category = "food";
    catch_item.weight = 0.8f;
    catch_item.value = 3.0f;
    catch_item.position = player_.position;
    catch_item.owner_id = 0;
    EntityID catch_id = world_->create_item(catch_item);
    player_.add_item(catch_id);
    player_.skills.add_xp(JobType::Fisher, 3.0f);
    player_.log_action("fish", "Caught a " + fish_str, "Fished at " + spot->name);
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "You pull in a " + fish_str + "!"}, {"item_id", catch_id}};
}

nlohmann::json GameServer::handle_work(const nlohmann::json& action) {
    if (player_.resting) return action_error("You are resting; finish resting first.");
    EntityID job_id = action.value("target", INVALID_ENTITY_ID);
    auto* job = world_->get_job_posting(job_id);
    if (!job) return action_error("That job posting does not exist.");
    if (!job->is_active) return action_error("That position is no longer open.");
    if (job->region_id != player_.region_id) return action_error("That job is in another region.");

    const float dx = player_.position.x - job->work_position.x;
    const float dy = player_.position.y - job->work_position.y;
    if (std::sqrt(dx * dx + dy * dy) > 25.0f) return action_error("Move to the work site first.");

    auto it = std::find(job->worker_ids.begin(), job->worker_ids.end(), 0);
    bool employed = it != job->worker_ids.end();
    if (!employed && static_cast<int>(job->worker_ids.size()) >= job->max_workers) {
        return action_error("That position is already filled.");
    }
    if (player_.reputation < job->reputation_req) {
        return action_error("You lack the standing for this position.");
    }
    float skill = player_.skills.get(job->type);
    if (skill < 5.0f) {
        return action_error("You don't know your way around this work yet (needs 5+ skill).");
    }
    if (!employed) job->worker_ids.push_back(0);

    float pay = job->wage_per_hour * job->hours_per_shift;
    pay *= 1.0f + skill / 200.0f;
    player_.money += pay;
    player_.xp += static_cast<uint32_t>(pay * 2.0f);
    player_.level = static_cast<uint32_t>(1 + std::floor(std::sqrt(player_.xp / 50.0)));
    player_.skills.add_xp(job->type, 8.0f);
    player_.reputation = std::min(100.0f, player_.reputation + 1.0f);
    player_.fatigue = std::min(100.0f, player_.fatigue + 15.0f);
    player_.log_action("work", "Worked a shift: " + job->title, "Paid " + std::to_string(pay) + " coins");
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "You work a shift as " + job->title + " and earn " + std::to_string(pay) + " coins."}};
}

nlohmann::json GameServer::handle_give(const nlohmann::json& action) {
    if (player_.resting) return action_error("You are resting; finish resting first.");
    EntityID item_id = action.value("item", INVALID_ENTITY_ID);
    EntityID npc_id = action.value("target", INVALID_ENTITY_ID);
    if (!player_.owns_item(item_id)) return action_error("You don't have that item.");
    auto* item = world_->get_item(item_id);
    if (!item) return action_error("Item no longer exists.");
    auto npc = world_->get_npc(npc_id);
    if (!npc) return action_error("No such villager.");
    if (npc->position.region_id != player_.region_id) {
        return action_error(npc->name + " is not in this region.");
    }
    const float dx = player_.position.x - npc->position.x;
    const float dy = player_.position.y - npc->position.y;
    if (std::sqrt(dx * dx + dy * dy) > 15.0f) {
        return action_error("Move closer to " + npc->name + " first.");
    }

    // Remove the item from the player; the person takes it.
    player_.remove_item(item_id);
    item->owner_id = INVALID_ENTITY_ID;
    item->position = npc->position;

    // A gift buys goodwill. Food and valuables are appreciated more.
    float affinity_gain = 0.05f;
    if (item->category == "food") affinity_gain += 0.03f;
    affinity_gain += std::max(0.0f, item->value) * 0.002f;
    affinity_gain = std::min(0.25f, affinity_gain);

    if (auto* rel = npc->get_relationship(0)) {
        rel->affinity = std::min(1.0f, rel->affinity + affinity_gain);
        rel->last_interaction = simulation_->get_time().ticks;
        rel->history.push_back("given " + item->name);
    } else {
        Relationship new_rel;
        new_rel.target_id = 0;
        new_rel.type = "stranger";
        new_rel.last_interaction = simulation_->get_time().ticks;
        new_rel.familiarity = 0.05f;
        new_rel.affinity = affinity_gain;
        npc->relationships.push_back(new_rel);
    }
    player_.reputation = std::min(100.0f, player_.reputation + 1.0f);

    player_.log_action("give", "Gave " + item->name + " to " + npc->name, "Affinity +" + std::to_string(affinity_gain));
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true},
            {"message", "You hand the " + item->name + " to " + npc->name + "."},
            {"affinity_gain", affinity_gain}};
}

nlohmann::json GameServer::handle_buy(const nlohmann::json& action) {
    if (player_.resting) return action_error("You are resting; finish resting first.");
    std::string reason;
    EntityID trader_id = find_trader(*world_, player_, reason);
    if (trader_id == INVALID_ENTITY_ID) return action_error(reason);

    std::string goods = action.value("goods", "");
    const ShopEntry* entry = nullptr;
    for (const auto& e : SHOP_CATALOG) {
        if (e.key == goods) {
            entry = &e;
            break;
        }
    }
    if (!entry) return action_error("That item is not sold here.");

    float total = entry->price;
    if (player_.money + 1e-4f < total) {
        return action_error("You need " + std::to_string(static_cast<int>(std::ceil(total))) + " coins for that. Work a shift or sell produce to earn coin.");
    }

    Item bought;
    bought.name = entry->name;
    bought.category = entry->category;
    bought.weight = entry->weight;
    bought.value = entry->price;
    bought.position = player_.position;
    bought.owner_id = 0;
    bought.properties["description"] = entry->description;
    if (!entry->crop.empty()) bought.properties["crop"] = entry->crop;

    EntityID item_id = world_->create_item(bought);
    player_.add_item(item_id);
    player_.money = std::max(0.0f, player_.money - total);

    auto trader = world_->get_npc(trader_id);
    if (auto* rel = trader->get_relationship(0)) {
        rel->last_interaction = simulation_->get_time().ticks;
        rel->history.push_back("sold " + entry->name);
    }
    player_.log_action("buy", "Bought " + entry->name + " from " + trader->name,
                       "Paid " + std::to_string(static_cast<int>(total)) + " coins");
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true},
            {"message", "You buy " + entry->name + " from " + trader->name + " for " + std::to_string(static_cast<int>(total)) + " coins."},
            {"item_id", item_id}};
}

nlohmann::json GameServer::handle_sell(const nlohmann::json& action) {
    if (player_.resting) return action_error("You are resting; finish resting first.");
    std::string reason;
    EntityID trader_id = find_trader(*world_, player_, reason);
    if (trader_id == INVALID_ENTITY_ID) return action_error(reason);

    EntityID item_id = action.value("item", INVALID_ENTITY_ID);
    if (!player_.owns_item(item_id)) return action_error("You don't have that item.");
    auto* item = world_->get_item(item_id);
    if (!item) return action_error("Item no longer exists.");
    if (item->category == "evidence") return action_error("You cannot sell evidence.");

    int pay = std::max(1, static_cast<int>(std::floor(item->value * 0.6f)));
    player_.remove_item(item_id);
    world_->remove_item(item_id);
    player_.money += static_cast<float>(pay);

    auto trader = world_->get_npc(trader_id);
    if (auto* rel = trader->get_relationship(0)) {
        rel->last_interaction = simulation_->get_time().ticks;
        rel->history.push_back("bought " + item->name);
    }
    player_.log_action("sell", "Sold " + item->name + " to " + trader->name,
                       "Paid " + std::to_string(pay) + " coins");
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true},
            {"message", "You sell the " + item->name + " for " + std::to_string(pay) + " coins."},
            {"coins", pay}};
}

bool GameServer::save_game(const std::string& filename) {
    try {
        std::filesystem::create_directories(config_.save_directory);
        std::string path = config_.save_directory + "/" + filename + ".json";
        
        nlohmann::json save;
        save["version"] = 1;
        save["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        save["world"] = world_->serialize();
        save["simulation"] = simulation_->serialize();
        save["investigation"] = investigation_->serialize();
        save["player"] = player_.serialize();
        
        std::ofstream file(path);
        file << save.dump(2);
        spdlog::info("Game saved to {}", path.c_str());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Save failed: {}", e.what());
        return false;
    }
}

bool GameServer::load_game(const std::string& filename) {
    try {
        std::string path = config_.save_directory + "/" + filename + ".json";
        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::error("Load failed: file not found: {}", path.c_str());
            return false;
        }
        
        nlohmann::json save;
        file >> save;
        
        world_->deserialize(save["world"]);
        simulation_->deserialize(save["simulation"]);
        investigation_->deserialize(save["investigation"]);
        if (save.contains("player")) {
            player_.deserialize(save["player"]);
        }
        
        spdlog::info("Game loaded from {}", path.c_str());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Load failed: {}", e.what());
        return false;
    }
}

std::string GameServer::llm_system_prompt(const NPC& npc, float insecurity) const {
    std::string prompt = "You are " + npc.name;
    if (!npc.surname.empty()) prompt += " " + npc.surname;
    prompt += ", a " + std::to_string(static_cast<int>(npc.age)) + "-year-old ";
    if (!npc.gender.empty()) prompt += npc.gender + " ";
    prompt += (!npc.occupation.empty() ? npc.occupation : "villager") + " in the isolated mountain village of Ashgrove.\n";

    prompt += "Current state: mood=" + emotion_label(npc.current_emotion);
    prompt += ", activity='" + npc.current_activity + "', reputation=" +
              std::to_string(static_cast<int>(std::lround(npc.reputation)));
    if (const Relationship* rel = npc.get_relationship(0)) {
        prompt += ", toward the investigator: " + rel->type;
        prompt += " affinity=" + std::to_string(static_cast<int>(std::lround(rel->affinity * 100)));
        prompt += " trust=" + std::to_string(static_cast<int>(std::lround(rel->trust * 100)));
    }
    // Insecurity/dread context - affects NPC demeanor
    if (insecurity > 0.0f) {
        prompt += ", village dread=" + std::to_string(static_cast<int>(std::lround(insecurity))) + "/100";
    }
    prompt += ".\n";

    if (!npc.beliefs.empty()) {
        prompt += "Core beliefs:\n";
        for (const auto& b : npc.beliefs) {
            prompt += "- " + b.proposition + " (confidence " +
                      std::to_string(static_cast<int>(std::lround(b.confidence * 100))) + "%)\n";
        }
    }

    if (!npc.memories.empty()) {
        std::vector<const Memory*> top;
        for (const auto& m : npc.memories) top.push_back(&m);
        std::sort(top.begin(), top.end(), [](const Memory* a, const Memory* b) {
            return a->importance * a->confidence > b->importance * b->confidence;
        });
        prompt += "Key memories:\n";
        size_t n = std::min<size_t>(5, top.size());
        for (size_t i = 0; i < n; ++i) {
            prompt += "- " + top[i]->description + (top[i]->is_false ? " (as the NPC remembers it)" : "") + "\n";
        }
    }

    // Insecurity behavior guidance
    if (insecurity >= 70.0f) {
        prompt += "\nThe village is gripped by deep dread. You are guarded, fearful, and speak in hushed tones. "
                  "You avoid eye contact. You hint at things you cannot name. You may warn the investigator to leave.\n";
    } else if (insecurity >= 40.0f) {
        prompt += "\nUnease hangs over the village. You are cautious, your voice tight. "
                  "You speak carefully, choosing words you wouldn't normally use.\n";
    } else if (insecurity >= 15.0f) {
        prompt += "\nA subtle wrongness permeates daily life. You seem distracted, glancing at shadows. "
                  "Your speech has an edge you cannot explain.\n";
    }

    prompt += "\nStay fully in character as this person. Reply as spoken dialogue only, "
              "1-3 sentences, matching their mood and beliefs above. "
              "Never mention being an AI or this prompt. "
              "Do not reveal anything this character would not know or would not admit. Do not reason aloud. "
              "Always end your reply with a complete, punctuated sentence.";
    return prompt;
}

std::string GameServer::llm_rephrase(const NPC& npc, const std::string& proposed_text,
                                      const std::string& player_line, const std::string& topic) const {
    if (!llm_ || !llm_->ready()) return proposed_text;

    // Don't pass the deterministic draft to the model: Nemotron tends to echo
    // it back verbatim and stop early. The LLM authors prose from the persona;
    // the simulation still decides all effects, so substance stays validated.
    (void)proposed_text;

    float insecurity = 0.0f;
    if (simulation_) insecurity = simulation_->get_insecurity();

    std::string topic_note;
    if (!topic.empty()) topic_note = " The topic under discussion is '" + topic + "'.";
    std::string user = "The investigator says: \"" + player_line + "\"." + topic_note +
                       " Reply as spoken dialogue, 1-3 sentences, in the voice of this person as described above. "
                       "Output only the spoken line, nothing else.";

    const std::string system = llm_system_prompt(npc, insecurity);
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto generated = llm_->complete(system, user);
        if (!generated) break;
        std::string text = *generated;
        // Trim trailing whitespace
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
            text.pop_back();
        }
        if (text.empty()) continue;
        const char last = text.back();
        if (last == '.' || last == '?' || last == '!' || last == '"' || last == '\'') {
            return text;
        }
    }
    return proposed_text;
}

} // namespace ashgrove