#pragma once

#include "common/types.h"
#include "simulation/simulation.h"
#include "world/world.h"
#include "npc/npc.h"
#include "investigation/investigation.h"
#include "network/network.h"
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace ashgrove {

struct GameConfig {
    int port = 8000;
    bool enable_llm = false;
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
    bool load_game(const std::string& filename = "game_save");
    
    // State access for frontend
    nlohmann::json get_world_state() const;
    
    // Handle player actions (from REST/WebSocket)
    nlohmann::json handle_action(const nlohmann::json& action);
    
    // Setup of the vertical slice world
    void create_test_world();
    
private:
    void simulation_step();
    void tick();
    
    GameConfig config_;
    std::shared_ptr<World> world_;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<InvestigationSystem> investigation_;
    std::shared_ptr<ITransport> transport_;
    std::unique_ptr<NetworkServer> network_;
    
    bool running_ = false;
    std::thread sim_thread_;
    uint64_t last_tick_ = 0;
};

} // namespace ashgrove