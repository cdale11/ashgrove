#include "event_bus.hpp"
#include <map>
#include <mutex>

void EventBus::subscribe(EventTopic topic, const std::string& id, Callback cb) {
    std::lock_guard<std::mutex> lock(mtx_);
    subscribers_[topic][id] = std::move(cb);
}

void EventBus::unsubscribe(EventTopic topic, const std::string& id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = subscribers_.find(topic);
    if (it != subscribers_.end()) {
        it->second.erase(id);
        if (it->second.empty()) {
            subscribers_.erase(it);
        }
    }
}

void EventBus::publish(EventTopic topic, const std::string& data) {
    std::map<std::string, Callback> copy;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = subscribers_.find(topic);
        if (it == subscribers_.end()) return;
        copy = it->second; // shallow copy of callbacks (function objects are safe to copy)
    }
    for (auto& [id, cb] : copy) {
        cb(data);
    }
}
