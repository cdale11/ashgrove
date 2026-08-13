#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <memory>

enum class EventTopic {
    Tick,
    PlayerCmd,
    NpcAction,
    WorldChange,
    QuestGenerated,
    HorrorEvent,
    EconomyShift,
    SanityChange
};

class EventBus {
public:
    using Callback = std::function<void(const std::string& data)>;

    EventBus() = default;
    ~EventBus() = default;

    // Subscribe a callback under a unique identifier for a topic.
    void subscribe(EventTopic topic, const std::string& id, Callback cb);

    // Unsubscribe by identifier.
    void unsubscribe(EventTopic topic, const std::string& id);

    // Publish data to all subscribers of a topic.
    void publish(EventTopic topic, const std::string& data);

private:
    std::map<EventTopic, std::map<std::string, Callback>> subscribers_;
    std::mutex mtx_;
};
