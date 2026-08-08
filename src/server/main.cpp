#include "server/game_server.h"
#include <spdlog/spdlog.h>
#include <signal.h>
#include <atomic>
#include <thread>
#include <cstdlib>
#include <string>

namespace {
std::atomic<bool> g_stop_requested{false};

void signal_handler(int) {
    g_stop_requested = true;
}
}

int main(int argc, char** argv) {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);
    
    // Parse command line
    ashgrove::GameConfig config;
    
    // Simple argument parsing
    int port = config.port;
    std::string model_path;
    std::string llm_url;
    bool enable_llm = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
            enable_llm = true;
        } else if (arg == "--llm") {
            enable_llm = true;
        } else if (arg == "--llm-url" && i + 1 < argc) {
            llm_url = argv[++i];
            enable_llm = true;
        } else if (arg == "--help") {
            spdlog::info("Usage: ashgrove_server [--port PORT] [--model PATH] [--llm] [--llm-url URL]");
            return 0;
        }
    }
    
    config.port = port;
    config.llm_model_path = model_path;
    config.enable_llm = enable_llm;
    if (!llm_url.empty()) config.llm_url = llm_url;
    
    if (enable_llm) {
        spdlog::info("LLM cognition enabled (llama-server at {})", config.llm_url.c_str());
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    
    spdlog::info("==========================================");
    spdlog::info("        Ashgrove Game Server v0.1.0");
    spdlog::info("==========================================");
    
    ashgrove::GameServer server(config);
    if (!server.initialize()) {
        spdlog::error("Failed to initialize game server");
        return 1;
    }
    
    spdlog::info("Server running. Press Ctrl+C to stop.");
    
    // Run in main thread, handle signals
    server.run_async();
    
    while (!g_stop_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    spdlog::info("Shutting down...");
    server.stop();
    return 0;
}