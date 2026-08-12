#pragma once
#include "world.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

using json = nlohmann::json;

enum class MsgType : uint8_t {
    Join = 0,
    JoinAck = 1,
    WorldState = 2,
    PlayerMove = 3,
    PlayerPos = 4,
    PlayerJoined = 5,
    PlayerLeft = 6,
    Ping = 7,
    Pong = 8,
};

inline json make_join(const std::string& name) {
    return {{"type", static_cast<int>(MsgType::Join)}, {"name", name}};
}

inline json make_join_ack(uint32_t id, const std::vector<uint8_t>& tiles) {
    return {{"type", static_cast<int>(MsgType::JoinAck)},
            {"player_id", id}, {"map_w", MAP_W}, {"map_h", MAP_H}, {"tile_size", 16}, {"tile_map", tiles}};
}

inline json make_player_pos(uint32_t id, int16_t x, int16_t y, uint8_t dir, bool moving) {
    return {{"type", static_cast<int>(MsgType::PlayerPos)},
            {"player_id", id}, {"x", x}, {"y", y}, {"dir", dir}, {"moving", moving}};
}

inline json make_player_joined(uint32_t id, int16_t x, int16_t y, const std::string& name) {
    return {{"type", static_cast<int>(MsgType::PlayerJoined)},
            {"player_id", id}, {"x", x}, {"y", y}, {"name", name}};
}

inline json make_player_left(uint32_t id) {
    return {{"type", static_cast<int>(MsgType::PlayerLeft)}, {"player_id", id}};
}

inline json make_world_state(const std::vector<json>& players) {
    return {{"type", static_cast<int>(MsgType::WorldState)}, {"players", players}};
}

inline json make_action_ack(const std::string& msg) {
    return {{"type", 9}, {"msg", msg}};
}

inline MsgType get_type(const json& j) {
    return static_cast<MsgType>(j.value("type", 0));
}