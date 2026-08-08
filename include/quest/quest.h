#pragma once

#include "common/types.h"
#include <algorithm>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ashgrove {

// Objectives a quest can track. Each maps to a game action and a counter.
enum class QuestKind : uint8_t {
    None,
    Plant,        // plant N crops
    Harvest,      // harvest N crops
    Fish,         // catch N fish
    Gather,       // gather N raw resources
    Craft,        // craft N items
    Sell,         // earn N coins from selling
    ReachWealth,  // hold N coins at once
    ReachLevel,   // reach player level N
    Mystery,      // progress the authored mystery chain
};

inline const char* quest_kind_hint(QuestKind k) {
    switch (k) {
        case QuestKind::Plant: return "Plant crops";
        case QuestKind::Harvest: return "Harvest crops";
        case QuestKind::Fish: return "Catch fish";
        case QuestKind::Gather: return "Gather resources";
        case QuestKind::Craft: return "Craft items";
        case QuestKind::Sell: return "Earn coins";
        case QuestKind::ReachWealth: return "Hold enough coins";
        case QuestKind::ReachLevel: return "Reach a level";
        case QuestKind::Mystery: return "Uncover the mystery";
        default: return "";
    }
}

// One quest with progress, reward, and giver.
struct Quest {
    EntityID id = INVALID_ENTITY_ID;
    std::string title;
    std::string description;
    std::string giver;          // who posted it (name/occupation)
    QuestKind kind = QuestKind::None;
    std::string target;         // optional: named item / skill / knowledge title
    int required = 0;           // goal amount
    int progress = 0;           // current amount
    float reward_coins = 0.0f;
    float reward_xp = 0.0f;
    std::string reward_item;    // optional item granted on claim
    std::string status;         // "available" | "active" | "redeemable" | "completed"
    bool is_daily = false;      // procedurally generated per-day errand
    TimeTick posted_day = 0;    // day_of_year when created (for dailies expiry)

    float progress_fraction() const {
        if (required <= 0) return 0.0f;
        return std::min(1.0f, static_cast<float>(progress) / static_cast<float>(required));
    }
    bool is_complete() const { return required > 0 && progress >= required; }

    nlohmann::json serialize() const;
};

} // namespace ashgrove