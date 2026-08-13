#include "plugin.hpp"
#include "event_bus.hpp"
#include <iostream>

class EchoPlugin : public Plugin {
public:
    void init(EventBus& bus) override {
        // Subscribe to all known topics and just log
        auto log = [](const std::string& topic, const std::string& data) {
            std::cout << "[EchoPlugin] " << topic << ": " << data << "\n";
        };
        bus.subscribe(EventTopic::Tick, "echo_tick", [&](const std::string& d){ log("Tick", d); });
        bus.subscribe(EventTopic::PlayerCmd, "echo_player_cmd", [&](const std::string& d){ log("PlayerCmd", d); });
        bus.subscribe(EventTopic::NpcAction, "echo_npc_action", [&](const std::string& d){ log("NpcAction", d); });
        bus.subscribe(EventTopic::WorldChange, "echo_world_change", [&](const std::string& d){ log("WorldChange", d); });
        bus.subscribe(EventTopic::QuestGenerated, "echo_quest", [&](const std::string& d){ log("QuestGenerated", d); });
        bus.subscribe(EventTopic::HorrorEvent, "echo_horror", [&](const std::string& d){ log("HorrorEvent", d); });
        bus.subscribe(EventTopic::EconomyShift, "echo_economy", [&](const std::string& d){ log("EconomyShift", d); });
        bus.subscribe(EventTopic::SanityChange, "echo_sanity", [&](const std::string& d){ log("SanityChange", d); });
        std::cout << "[EchoPlugin] registered\n";
    }
};

REGISTER_PLUGIN(EchoPlugin)