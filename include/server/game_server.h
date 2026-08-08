#pragma once

#include "common/types.h"
#include "simulation/simulation.h"
#include "world/world.h"
#include "npc/npc.h"
#include "npc/dialogue.h"
#include "quest/quest.h"
#include "investigation/investigation.h"
#include "network/network.h"
#include "server/player.h"
#include "ai/llm_client.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace ashgrove {

struct GameConfig {
    int port = 8000;
    bool enable_llm = false;
    std::string llm_url = "http://127.0.0.1:8081"; // llama-server base URL
    std::string llm_model_path;
    std::string save_directory = "saves";
    int tick_rate_ms = 1000;     // Simulation tick interval in ms
    int world_time_scale = 60;   // 1 real second = 60 game seconds (1 game minute = 1 real second)
    float npc_ai_interval_ticks = 15; // How often NPC AI runs (in ticks)
};

class GameServer {
public:
    GameServer(const GameConfig& config);
    ~GameServer();
    
    bool initialize();
    void shutdown();
    
    // Main loop
    void run();
    void run_async();
    void stop();
    
    // Save/Load
    bool save_game(const std::string& filename = "autosave");
    bool load_game(const std::string& filename = "autosave");
    
    // State access for frontend
    nlohmann::json get_world_state() const;
    
    // Handle player actions (from REST/WebSocket)
    nlohmann::json handle_action(const nlohmann::json& action);
    
    // Setup of the vertical slice world
    void create_test_world();
    
private:
    void simulation_step();
    void tick();
    void tick_life_systems(TimeTick elapsed_ticks);
    void tick_npc_schedules(bool snap = false);

    // ---- Gameplay: quests, gathering, farming expansion, crafting ----
    void setup_quests();
    void roll_daily_quests();                 // procedural, seeded per day
    void progress_quest(QuestKind kind, int amount = 1, const std::string& target = "");
    void check_quest_milestones();            // wealth / level / mystery quests
    EntityID grant_reward(const Quest& q);    // coins+item+xp; returns item id or INVALID
    uint32_t next_quest_id_ = 1000;
    std::vector<Quest> quests_;
    std::vector<EntityID> quest_item_ids_;    // items created to seed quest targets
    uint16_t quest_daily_day_ = 0;            // last day daily errands were rolled

    void spawn_resource_deposits();           // procedural deposit placement
    void create_recipe_items();
    nlohmann::json handle_accept_quest(const nlohmann::json& action);
    nlohmann::json handle_claim_quest(const nlohmann::json& action);
    nlohmann::json handle_gather(const nlohmann::json& action);
    nlohmann::json handle_expand_farm(const nlohmann::json& action);
    nlohmann::json handle_craft(const nlohmann::json& action);

    // ---- Recipe table ----
    struct CraftRecipe {
        std::string key;                       // recipe id
        std::string name;                      // resulting item name
        std::string category;                  // "tool" | "food" | "material"
        float value;                           // item value
        std::vector<std::pair<std::string, int>> costs; // item name -> qty
        float skill_req = 0.0f;                // required skill level
        std::string skill;                     // which player skill gates it
        std::string description;
    };
    static const std::vector<CraftRecipe>& recipes();

    // Action handlers
    nlohmann::json handle_move(const nlohmann::json& action);
    nlohmann::json handle_talk(const nlohmann::json& action);
    nlohmann::json handle_dialogue_topic(const nlohmann::json& action);
    nlohmann::json handle_inspect(const nlohmann::json& action);
    nlohmann::json handle_pickup(const nlohmann::json& action);
    nlohmann::json handle_use_item(const nlohmann::json& action);
    nlohmann::json handle_drop_item(const nlohmann::json& action);
    nlohmann::json handle_rest(const nlohmann::json& action);
    nlohmann::json handle_enter(const nlohmann::json& action);
    nlohmann::json handle_exit(const nlohmann::json& action);
    nlohmann::json handle_advance_time(const nlohmann::json& action);
    nlohmann::json handle_plant(const nlohmann::json& action);
    nlohmann::json handle_water(const nlohmann::json& action);
    nlohmann::json handle_harvest(const nlohmann::json& action);
    nlohmann::json handle_fish(const nlohmann::json& action);
    nlohmann::json handle_work(const nlohmann::json& action);
    nlohmann::json handle_give(const nlohmann::json& action);
    nlohmann::json handle_buy(const nlohmann::json& action);
    nlohmann::json handle_sell(const nlohmann::json& action);
    nlohmann::json action_error(const std::string& msg);
    nlohmann::json make_state_response();

    // Helpers
    PlayerKnowledge player_knowledge() const;
    EntityID find_npc_at(const Position& pos, float radius = 5.0f) const;
    EntityID find_item_at(const Position& pos, float radius = 3.0f) const;
    std::string region_name(EntityID region_id) const;
    void build_npc_schedules();
    std::string llm_rephrase(const NPC& npc, const std::string& proposed_text,
                             const std::string& player_line, const std::string& topic) const;
    std::string llm_system_prompt(const NPC& npc, float insecurity) const;

    GameConfig config_;
    std::shared_ptr<World> world_;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<InvestigationSystem> investigation_;
    std::unique_ptr<DialogueSystem> dialogue_;
    std::unique_ptr<LLMClient> llm_;
    PlayerCharacter player_;
    std::shared_ptr<ITransport> transport_;
    std::unique_ptr<NetworkServer> network_;

    bool running_ = false;
    std::thread sim_thread_;
    uint64_t last_tick_ = 0;
    // NPC relationship with the player (NPC id -> Relationship with player id 0)
    std::unordered_map<EntityID, Relationship> player_relations_;
    // NPC home anchors (fallback when a schedule has no valid location)
    std::unordered_map<EntityID, Position> npc_home_;
    // Per-NPC walking speed (world units per tick call; ~1 real second at default tick rate)
    float npc_walk_speed_ = 4.0f;
};

} // namespace ashgrove