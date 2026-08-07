#include "server/game_server.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace ashgrove {

GameServer::GameServer(const GameConfig& config)
    : config_(config),
      world_(std::make_shared<World>()),
      simulation_(std::make_unique<Simulation>()),
      investigation_(std::make_unique<InvestigationSystem>()) {
    transport_ = std::make_shared<SocketTransport>();
    network_ = std::make_unique<NetworkServer>(transport_);
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
    world_->create_building(tavern);
    
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
    
    spdlog::info("Test world created with {} NPCs, {} buildings, {} regions", 
        world_->serialize()["npcs"].size(), world_->serialize()["buildings"].size(), world_->serialize()["regions"].size());
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
        {"weather_intensity", simulation_->get_time().weather_intensity}
    };
    state["world"] = world_->serialize();
    state["investigation"] = investigation_->serialize();
    return state;
}

nlohmann::json GameServer::handle_action(const nlohmann::json& action) {
    std::string type = action.value("type", "");
    
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
    if (type == "interact") {
        // Player interaction with an NPC or object
        EntityID target = action.value("target", INVALID_ENTITY_ID);
        return get_world_state();
    }
    
    return {{"error", "Unknown action type: " + type}};
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
        
        std::ofstream file(path);
        file << save.dump(2);
        spdlog::info("Game saved to {}", path);
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
            spdlog::error("Load failed: file not found: {}", path);
            return false;
        }
        
        nlohmann::json save;
        file >> save;
        
        world_->deserialize(save["world"]);
        simulation_->deserialize(save["simulation"]);
        investigation_->deserialize(save["investigation"]);
        
        spdlog::info("Game loaded from {}", path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Load failed: {}", e.what());
        return false;
    }
}

} // namespace ashgrove