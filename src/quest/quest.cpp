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

Quest Quest::deserialize(const nlohmann::json& j) {
    Quest q;
    q.id = j.value("id", INVALID_ENTITY_ID);
    q.title = j.value("title", "");
    q.description = j.value("description", "");
    q.giver = j.value("giver", "");
    q.kind = static_cast<QuestKind>(j.value("kind", static_cast<int>(QuestKind::None)));
    q.target = j.value("target", "");
    q.required = j.value("required", 0);
    q.progress = j.value("progress", 0);
    q.reward_coins = j.value("reward_coins", 0.0f);
    q.reward_xp = j.value("reward_xp", 0.0f);
    q.reward_item = j.value("reward_item", "");
    q.status = j.value("status", "available");
    q.is_daily = j.value("is_daily", false);
    q.posted_day = j.value("posted_day", static_cast<TimeTick>(0));
    return q;
}

} // namespace ashgrove