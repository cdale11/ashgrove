#pragma once
#include "event_bus.hpp"
#include <memory>
#include <vector>

class Plugin {
public:
    virtual ~Plugin() = default;
    virtual void init(EventBus& bus) = 0;
};

class PluginManager {
public:
    static void registerPlugin(std::unique_ptr<Plugin> plugin) {
        plugins().push_back(std::move(plugin));
    }

    static void initAll(EventBus& bus) {
        for (auto& p : plugins()) {
            p->init(bus);
        }
    }

private:
    static std::vector<std::unique_ptr<Plugin>>& plugins() {
        static std::vector<std::unique_ptr<Plugin>> list;
        return list;
    }
};

// Helper macro for automatic registration
#define REGISTER_PLUGIN(ClassName) \
    namespace { \
        struct ClassName##Registrar { \
            ClassName##Registrar() { \
                PluginManager::registerPlugin(std::make_unique<ClassName>()); \
            } \
        }; \
        static ClassName##Registrar registrarInstance; \
    }
