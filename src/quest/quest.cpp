#include "quest/quest.h"
#include <algorithm>

namespace ashgrove {

nlohmann::json Quest::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"title", title},
        {"description", description},
        {"giver", giver},
        {"kind", static_cast<int>(kind)},
        {"kind_hint", quest_kind_hint(kind)},
        {"target", target},
        {"required", required},
        {"progress", progress},
        {"reward_coins", reward_coins},
        {"reward_xp", reward_xp},
        {"reward_item", reward_item},
        {"status", status},
        {"is_daily", is_daily},
        {"posted_day", posted_day},
    };
}

} // namespace ashgrove