#include "plugin.hpp"
#include "event_bus.hpp"
#include "world.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

class CommandDispatcherPlugin : public Plugin {
public:
    void init(EventBus& bus) override {
        bus.subscribe(EventTopic::PlayerCmd, "dispatch_intent",
            [&](const std::string& data) {
                try {
                    auto j = nlohmann::json::parse(data);
                    std::string action = j.value("action", "");
                    auto params = j.value("parameters", nlohmann::json::object());
                    std::cout << "[CommandDispatcher] intent action=" << action << "\n";
                    // Actual handling will be done by the main thread after the event loop.
                    // For now we just log; the main loop can also listen to this topic.
                } catch (...) {
                    std::cerr << "[CommandDispatcher] failed to parse intent\n";
                }
            });
        std::cout << "[CommandDispatcherPlugin] registered\n";
    }
};

REGISTER_PLUGIN(CommandDispatcherPlugin)