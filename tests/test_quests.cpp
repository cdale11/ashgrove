#include "test_utils.h"
#include "server/game_server.h"
#include "quest/quest.h"
#include <cmath>
#include <memory>

using namespace ashgrove;

namespace {

// Spin up a real server (no network loop) so quest mechanics, gathering,
// crafting, and save/load exercise the actual handlers.
std::unique_ptr<GameServer> make_server(int port) {
    GameConfig cfg;
    cfg.port = port;
    cfg.enable_llm = false;
    cfg.save_directory = "saves_test";
    auto server = std::make_unique<GameServer>(cfg);
    server->initialize();
    return server;
}

size_t player_item_count(GameServer& server, const std::string& name) {
    auto state = server.get_world_state();
    if (!state.contains("player")) return 0;
    const auto& inv = state["player"]["inventory"];
    size_t count = 0;
    for (const auto& id : inv) {
        for (const auto& it : state["world"]["items"]) {
            if (it["id"] == id && it["name"] == name) count++;
        }
    }
    return count;
}

} // namespace

TEST(quest_serialize_roundtrip) {
    Quest q;
    q.id = 4242;
    q.title = "Test Errand";
    q.kind = QuestKind::Gather;
    q.target = "wood";
    q.required = 6;
    q.progress = 2;
    q.reward_coins = 30.0f;
    q.status = "active";
    q.is_daily = true;
    q.posted_day = 12;

    Quest restored = Quest::deserialize(q.serialize());
    CHECK_EQ(restored.id, 4242);
    CHECK_EQ(restored.title, "Test Errand");
    CHECK_EQ(restored.kind, QuestKind::Gather);
    CHECK_EQ(restored.target, "wood");
    CHECK_EQ(restored.required, 6);
    CHECK_EQ(restored.progress, 2);
    CHECK_NEAR(restored.reward_coins, 30.0f, 0.001f);
    CHECK_EQ(restored.status, "active");
    CHECK(restored.is_daily);
    CHECK_EQ(restored.posted_day, 12);
    CHECK(!restored.is_complete());
    CHECK_NEAR(restored.progress_fraction(), 2.0f / 6.0f, 0.001f);

    Quest done = restored;
    done.progress = 6;
    CHECK(done.is_complete());
}

TEST(quest_accept_claim_flow) {
    auto server = make_server(18101);
    auto state = server->get_world_state();
    CHECK(state.contains("quests"));
    CHECK(state["quests"].size() > 0);

    // Find an available authored gather quest (matches the wood we will collect).
    long long qid = 0;
    for (const auto& q : state["quests"]) {
        if (q["status"] == "available" && !q["is_daily"].get<bool>() &&
            q["kind"] == static_cast<int>(QuestKind::Gather)) {
            qid = q["id"].get<long long>();
            break;
        }
    }
    CHECK(qid != 0);

    // Accept moves it to active.
    nlohmann::json accept = {{"type", "accept_quest"}, {"quest", qid}};
    auto res = server->handle_action(accept);
    CHECK(res["ok"].get<bool>());
    bool found_active = false;
    auto after_accept = server->get_world_state();
    for (const auto& q : after_accept["quests"]) {
        if (q["id"] == qid && q["status"] == "active") found_active = true;
    }
    CHECK(found_active);

    // Duplicate accept is rejected.
    auto second = server->handle_action(accept);
    CHECK(!second["ok"].get<bool>());

    // Claiming an unfinished quest is rejected.
    nlohmann::json claim = {{"type", "claim_quest"}, {"quest", qid}};
    auto early = server->handle_action(claim);
    CHECK(!early["ok"].get<bool>());

    // Complete the gather quest by gathering wood into the player's pack.
    auto wstate = server->get_world_state();
    long long dep_id = 0;
    for (const auto& dep : wstate["world"]["resource_deposits"]) {
        if (dep["resource_name"] == "wood" && dep["amount"].get<float>() > 0) {
            dep_id = dep["id"].get<long long>();
            break;
        }
    }
    CHECK(dep_id != 0);
    float dx = wstate["world"]["resource_deposits"][0]["position"]["x"].get<float>();
    // Find the matching deposit position.
    const nlohmann::json* chosen = nullptr;
    for (const auto& dep : wstate["world"]["resource_deposits"]) {
        if (dep["id"] == dep_id) { chosen = &dep; break; }
    }
    CHECK(chosen != nullptr);
    server->handle_action({{"type", "move"},
                           {"target", {{"x", (*chosen)["position"]["x"].get<float>()},
                                       {"y", (*chosen)["position"]["y"].get<float>()}}}});
    for (int i = 0; i < 8; ++i) {
        server->handle_action({{"type", "gather"}, {"target", dep_id}});
    }

    // Quest progressed toward completion: progress > 0 on the active quest.
    bool progressed = false;
    nlohmann::json redeemable = nlohmann::json();
    auto progressed_state = server->get_world_state();
    for (const auto& q : progressed_state["quests"]) {
        if (q["id"] == qid) {
            progressed = true;
            if (q["status"] == "redeemable") redeemable = q;
        }
    }
    CHECK(progressed);
    CHECK(!redeemable.is_null());

    // Claim pays out coins and marks it completed.
    float money_before = server->get_world_state()["player"]["money"].get<float>();
    auto claimed = server->handle_action(claim);
    CHECK(claimed["ok"].get<bool>());
    float money_after = server->get_world_state()["player"]["money"].get<float>();
    CHECK(money_after > money_before);
    bool completed = false;
    auto after_claim = server->get_world_state();
    for (const auto& q : after_claim["quests"]) {
        if (q["id"] == qid && q["status"] == "completed") completed = true;
    }
    CHECK(completed);
}

TEST(gather_yields_and_skills) {
    auto server = make_server(18102);
    auto state = server->get_world_state();

    long long dep_id = 0;
    const nlohmann::json* dep = nullptr;
    for (const auto& d : state["world"]["resource_deposits"]) {
        if (d["resource_name"] == "stone" && d["amount"].get<float>() > 10) {
            dep = &d;
            dep_id = d["id"].get<long long>();
            break;
        }
    }
    CHECK(dep != nullptr);

    float before = (*dep)["amount"].get<float>();
    float mining_before = state["player"]["skills"]["mining"].get<float>();
    server->handle_action({{"type", "move"},
                           {"target", {{"x", (*dep)["position"]["x"].get<float>()},
                                       {"y", (*dep)["position"]["y"].get<float>()}}}});
    auto res = server->handle_action({{"type", "gather"}, {"target", dep_id}});
    CHECK(res["ok"].get<bool>());
    int gathered = res["count"].get<int>();
    CHECK(gathered >= 1);

    auto state2 = server->get_world_state();
    float after = 0.0f;
    for (const auto& d : state2["world"]["resource_deposits"]) {
        if (d["id"] == dep_id) after = d["amount"].get<float>();
    }
    CHECK_NEAR(before - after, static_cast<float>(gathered), 0.001f);
    CHECK(state2["player"]["skills"]["mining"].get<float>() > mining_before);
}

TEST(craft_requires_and_consumes) {
    auto server = make_server(18103);
    auto state = server->get_world_state();

    // No ingredients yet: crafting must be rejected.
    auto denied = server->handle_action({{"type", "craft"}, {"recipe", "stone_peg"}});
    CHECK(!denied["ok"].get<bool>());

    // Find the forge (crafting bench) and gather from the stone deposit
    // nearest to it, so the distance gate passes.
    float fx = 0.0f, fy = 0.0f;
    bool have_forge = false;
    for (const auto& b : state["world"]["buildings"]) {
        if (b["name"].get<std::string>().find("Hartman") != std::string::npos) {
            fx = b["position"]["x"].get<float>();
            fy = b["position"]["y"].get<float>();
            have_forge = true;
            break;
        }
    }
    CHECK(have_forge);

    long long dep_id = 0;
    float best_dist = 1e9f;
    for (const auto& d : state["world"]["resource_deposits"]) {
        if (d["resource_name"] != "stone" || d["amount"].get<float>() <= 10) continue;
        float dx = d["position"]["x"].get<float>() - fx;
        float dy = d["position"]["y"].get<float>() - fy;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best_dist) { best_dist = dist; dep_id = d["id"].get<long long>(); }
    }
    CHECK(dep_id != 0);
    CHECK(best_dist < 30.0f);
    const nlohmann::json* dep = nullptr;
    for (const auto& d : state["world"]["resource_deposits"]) {
        if (d["id"] == dep_id) { dep = &d; break; }
    }
    server->handle_action({{"type", "move"},
                           {"target", {{"x", (*dep)["position"]["x"].get<float>()},
                                       {"y", (*dep)["position"]["y"].get<float>()}}}});
    int attempts = 0;
    while (player_item_count(*server, "stone") < 2 && attempts < 10) {
        auto g = server->handle_action({{"type", "gather"}, {"target", dep_id}});
        CHECK(g["ok"].get<bool>());
        attempts++;
    }
    CHECK(player_item_count(*server, "stone") >= 2);

    size_t before = player_item_count(*server, "stone");
    auto crafted = server->handle_action({{"type", "craft"}, {"recipe", "stone_peg"}});
    CHECK(crafted["ok"].get<bool>());
    CHECK(player_item_count(*server, "stone") < before);
    CHECK(player_item_count(*server, "stone pegs") >= 1);
}

TEST(quest_save_load_restores_state) {
    auto server = make_server(18104);
    auto state = server->get_world_state();
    size_t quest_count = state["quests"].size();
    CHECK(quest_count > 0);

    // Accept the first available quest, then save.
    long long qid = 0;
    for (const auto& q : state["quests"]) {
        if (q["status"] == "available") { qid = q["id"].get<long long>(); break; }
    }
    CHECK(qid != 0);
    server->handle_action({{"type", "accept_quest"}, {"quest", qid}});

    CHECK(server->save_game());
    CHECK(server->load_game());

    // After reload the accepted quest is still active.
    bool restored = false;
    auto reloaded = server->get_world_state();
    for (const auto& q : reloaded["quests"]) {
        if (q["id"] == qid && q["status"] == "active") restored = true;
    }
    CHECK(restored);
}

TEST(daily_quests_rolled_with_authored) {
    auto server = make_server(18105);
    auto state = server->get_world_state();
    bool has_daily = false;
    bool has_authored = false;
    for (const auto& q : state["quests"]) {
        if (q["is_daily"].get<bool>()) has_daily = true;
        else has_authored = true;
    }
    // Both the authored arc and procedural dailies are on the board.
    CHECK(has_daily);
    CHECK(has_authored);
    // Minimum mix: 4 authored + 3 dailies.
    CHECK(state["quests"].size() >= 7);
}