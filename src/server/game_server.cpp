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

GameServer::GameServer(const GameConfig& config)
    : config_(config),
      world_(std::make_shared<World>()),
      simulation_(std::make_unique<Simulation>()),
      investigation_(std::make_unique<InvestigationSystem>()),
      dialogue_(std::make_unique<DialogueSystem>()) {
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

    // Place the player at the village square
    player_.position = {0, 0, 0};
    player_.region_id = 1; // first region created = Ashgrove Village

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
    state["player"] = player_.serialize();
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
    if (type == "rest") {
        auto res = handle_rest(action);
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

    item->owner_id = 0; // player
    player_.add_item(target);
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

nlohmann::json GameServer::handle_rest(const nlohmann::json& action) {
    player_.resting = true;
    player_.current_action = "resting";
    player_.rest_start_tick = simulation_->get_time().ticks;
    player_.log_action("rest", "Sat down to rest", "Fatigue slowly fades.");
    player_.action_log.back().tick = simulation_->get_time().ticks;
    return {{"ok", true}, {"message", "You rest. Time passes..."}};
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