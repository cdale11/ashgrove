#include "npc/npc.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace ashgrove {

nlohmann::json Memory::serialize() const {
    return nlohmann::json{
        {"event_id", event_id},
        {"timestamp", timestamp},
        {"description", description},
        {"participants", participants},
        {"importance", importance},
        {"emotional_valence", emotional_valence},
        {"is_false", is_false},
        {"confidence", confidence},
        {"source", source}
    };
}

Memory Memory::deserialize(const nlohmann::json& j) {
    Memory m;
    m.event_id = j.value("event_id", INVALID_ENTITY_ID);
    m.timestamp = j.value("timestamp", 0);
    m.description = j.value("description", "");
    m.participants = j.value("participants", std::vector<EntityID>{});
    m.importance = j.value("importance", 0.5f);
    m.emotional_valence = j.value("emotional_valence", 0.0f);
    m.is_false = j.value("is_false", false);
    m.confidence = j.value("confidence", 1.0f);
    m.source = j.value("source", "");
    return m;
}

nlohmann::json Belief::serialize() const {
    return nlohmann::json{
        {"proposition", proposition},
        {"confidence", confidence},
        {"formed_at", formed_at},
        {"last_reinforced", last_reinforced},
        {"evidence", evidence},
        {"is_core", is_core}
    };
}

Belief Belief::deserialize(const nlohmann::json& j) {
    Belief b;
    b.proposition = j.value("proposition", "");
    b.confidence = j.value("confidence", 0.5f);
    b.formed_at = j.value("formed_at", 0);
    b.last_reinforced = j.value("last_reinforced", 0);
    b.evidence = j.value("evidence", "");
    b.is_core = j.value("is_core", false);
    return b;
}

nlohmann::json Relationship::serialize() const {
    return nlohmann::json{
        {"target_id", target_id},
        {"affinity", affinity},
        {"trust", trust},
        {"familiarity", familiarity},
        {"type", type},
        {"last_interaction", last_interaction},
        {"history", history}
    };
}

Relationship Relationship::deserialize(const nlohmann::json& j) {
    Relationship r;
    r.target_id = j.value("target_id", INVALID_ENTITY_ID);
    r.affinity = j.value("affinity", 0.0f);
    r.trust = j.value("trust", 0.0f);
    r.familiarity = j.value("familiarity", 0.0f);
    r.type = j.value("type", "stranger");
    r.last_interaction = j.value("last_interaction", 0);
    r.history = j.value("history", std::vector<std::string>{});
    return r;
}

nlohmann::json Goal::serialize() const {
    return nlohmann::json{
        {"description", description},
        {"priority", priority},
        {"progress", progress},
        {"created_at", created_at},
        {"deadline", deadline},
        {"status", status},
        {"subgoals", subgoals}
    };
}

Goal Goal::deserialize(const nlohmann::json& j) {
    Goal g;
    g.description = j.value("description", "");
    g.priority = j.value("priority", 0.5f);
    g.progress = j.value("progress", 0.0f);
    g.created_at = j.value("created_at", 0);
    g.deadline = j.value("deadline", 0);
    g.status = j.value("status", "active");
    g.subgoals = j.value("subgoals", std::vector<std::string>{});
    return g;
}

NPC::NPC(EntityID id_, const std::string& name_, NPCTier tier_)
    : id(id_), name(name_), tier(tier_) {
    // Default personality (Big Five)
    personality = {
        {"openness", 0.5f},
        {"conscientiousness", 0.5f},
        {"extraversion", 0.5f},
        {"agreeableness", 0.5f},
        {"neuroticism", 0.5f}
    };
}

nlohmann::json NPC::serialize() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["surname"] = surname;
    j["tier"] = static_cast<int>(tier);
    j["age"] = age;
    j["gender"] = gender;
    j["occupation"] = occupation;
    j["position"] = {{"x", position.x}, {"y", position.y}, {"z", position.z}, {"region_id", position.region_id}};
    j["health"] = health;
    j["hunger"] = hunger;
    j["fatigue"] = fatigue;
    j["temperature"] = temperature;
    j["injuries"] = injuries;
    j["illnesses"] = illnesses;
    j["memories"] = nlohmann::json::array();
    for (const auto& m : memories) j["memories"].push_back(m.serialize());
    j["beliefs"] = nlohmann::json::array();
    for (const auto& b : beliefs) j["beliefs"].push_back(b.serialize());
    j["relationships"] = nlohmann::json::array();
    for (const auto& r : relationships) j["relationships"].push_back(r.serialize());
    j["goals"] = nlohmann::json::array();
    for (const auto& g : goals) j["goals"].push_back(g.serialize());
    j["personality"] = nlohmann::json::array();
    for (const auto& p : personality) j["personality"].push_back({{"name", p.name}, {"value", p.value}});
    j["current_emotion"] = static_cast<int>(current_emotion);
    j["emotion_intensity"] = emotion_intensity;
    j["schedule"] = nlohmann::json::array();
    for (const auto& s : schedule) j["schedule"].push_back({{"start_hour", s.start_hour}, {"duration_hours", s.duration_hours}, {"activity", s.activity}, {"location_id", s.location_id}, {"companion_id", s.companion_id.value_or("")}});
    j["current_activity"] = current_activity;
    j["reputation"] = reputation;
    j["wealth"] = wealth;
    j["home_id"] = home_id;
    j["workplace_id"] = workplace_id;
    j["family_ids"] = family_ids;
    j["llm_context"] = llm_context;
    j["llm_model"] = llm_model;
    j["needs_llm_update"] = needs_llm_update;
    return j;
}

void NPC::deserialize(const nlohmann::json& j) {
    id = j.value("id", INVALID_ENTITY_ID);
    name = j.value("name", "");
    surname = j.value("surname", "");
    tier = static_cast<NPCTier>(j.value("tier", 2));
    age = j.value("age", 25);
    gender = j.value("gender", "");
    occupation = j.value("occupation", "");
    if (j.contains("position")) {
        position.x = j["position"].value("x", 0.0f);
        position.y = j["position"].value("y", 0.0f);
        position.z = j["position"].value("z", 0.0f);
        position.region_id = j["position"].value("region_id", INVALID_ENTITY_ID);
    }
    health = j.value("health", 100.0f);
    hunger = j.value("hunger", 0.0f);
    fatigue = j.value("fatigue", 0.0f);
    temperature = j.value("temperature", 37.0f);
    injuries = j.value("injuries", std::vector<std::string>{});
    illnesses = j.value("illnesses", std::vector<std::string>{});
    memories.clear();
    for (const auto& m : j.value("memories", nlohmann::json::array())) memories.push_back(Memory::deserialize(m));
    beliefs.clear();
    for (const auto& b : j.value("beliefs", nlohmann::json::array())) beliefs.push_back(Belief::deserialize(b));
    relationships.clear();
    for (const auto& r : j.value("relationships", nlohmann::json::array())) relationships.push_back(Relationship::deserialize(r));
    goals.clear();
    for (const auto& g : j.value("goals", nlohmann::json::array())) goals.push_back(Goal::deserialize(g));
    personality.clear();
    for (const auto& p : j.value("personality", nlohmann::json::array())) personality.push_back({p.value("name", ""), p.value("value", 0.5f)});
    current_emotion = static_cast<EmotionType>(j.value("current_emotion", 0));
    emotion_intensity = j.value("emotion_intensity", 0.0f);
    schedule.clear();
    for (const auto& s : j.value("schedule", nlohmann::json::array())) {
        DailyScheduleEntry e;
        e.start_hour = s.value("start_hour", 0);
        e.duration_hours = s.value("duration_hours", 1);
        e.activity = s.value("activity", "idle");
        e.location_id = s.value("location_id", INVALID_ENTITY_ID);
        if (s.contains("companion_id") && !s["companion_id"].is_null() && !s["companion_id"].get<std::string>().empty()) {
            e.companion_id = s["companion_id"].get<std::string>();
        }
        schedule.push_back(e);
    }
    current_activity = j.value("current_activity", "idle");
    reputation = j.value("reputation", 0.0f);
    wealth = j.value("wealth", 0.0f);
    home_id = j.value("home_id", INVALID_ENTITY_ID);
    workplace_id = j.value("workplace_id", INVALID_ENTITY_ID);
    family_ids = j.value("family_ids", std::vector<EntityID>{});
    llm_context = j.value("llm_context", "");
    llm_model = j.value("llm_model", "");
    needs_llm_update = j.value("needs_llm_update", false);
}

void NPC::add_memory(const Memory& mem) {
    memories.push_back(mem);
    prune_memories();
}

std::vector<Memory> NPC::recall(const std::string& query, int limit) const {
    // Simple keyword-based recall (placeholder for semantic search)
    std::vector<Memory> results;
    for (const auto& mem : memories) {
        if (mem.description.find(query) != std::string::npos) {
            results.push_back(mem);
        }
    }
    // Sort by importance * confidence * recency
    std::sort(results.begin(), results.end(), [](const Memory& a, const Memory& b) {
        float score_a = a.importance * a.confidence;
        float score_b = b.importance * b.confidence;
        return score_a > score_b;
    });
    if (results.size() > static_cast<size_t>(limit)) results.resize(limit);
    return results;
}

void NPC::forget(EntityID event_id) {
    memories.erase(std::remove_if(memories.begin(), memories.end(),
        [event_id](const Memory& m) { return m.event_id == event_id; }), memories.end());
}

void NPC::reinforce_memory(EntityID event_id, float amount) {
    for (auto& mem : memories) {
        if (mem.event_id == event_id) {
            mem.confidence = std::min(1.0f, mem.confidence + amount);
            mem.importance = std::min(1.0f, mem.importance + amount * 0.5f);
            break;
        }
    }
}

void NPC::add_belief(const Belief& belief) {
    // Check if belief already exists
    for (auto& b : beliefs) {
        if (b.proposition == belief.proposition) {
            b.confidence = std::max(b.confidence, belief.confidence);
            b.last_reinforced = belief.last_reinforced;
            return;
        }
    }
    beliefs.push_back(belief);
}

void NPC::update_belief(const std::string& proposition, float confidence_change) {
    for (auto& b : beliefs) {
        if (b.proposition == proposition) {
            b.confidence = std::clamp(b.confidence + confidence_change, 0.0f, 1.0f);
            b.last_reinforced = 0; // Would be current tick
            break;
        }
    }
}

float NPC::get_belief_confidence(const std::string& proposition) const {
    for (const auto& b : beliefs) {
        if (b.proposition == proposition) return b.confidence;
    }
    return 0.0f; // No belief = neutral
}

void NPC::modify_relationship(EntityID target, float affinity_delta, float trust_delta) {
    for (auto& r : relationships) {
        if (r.target_id == target) {
            r.affinity = std::clamp(r.affinity + affinity_delta, -1.0f, 1.0f);
            r.trust = std::clamp(r.trust + trust_delta, -1.0f, 1.0f);
            r.familiarity = std::min(1.0f, r.familiarity + 0.01f);
            return;
        }
    }
    // Create new relationship
    Relationship r;
    r.target_id = target;
    r.affinity = std::clamp(affinity_delta, -1.0f, 1.0f);
    r.trust = std::clamp(trust_delta, -1.0f, 1.0f);
    r.familiarity = 0.1f;
    r.type = "stranger";
    relationships.push_back(r);
}

Relationship* NPC::get_relationship(EntityID target) {
    for (auto& r : relationships) {
        if (r.target_id == target) return &r;
    }
    return nullptr;
}

const Relationship* NPC::get_relationship(EntityID target) const {
    for (const auto& r : relationships) {
        if (r.target_id == target) return &r;
    }
    return nullptr;
}

void NPC::add_goal(const Goal& goal) {
    goals.push_back(goal);
}

void NPC::update_goal_progress(const std::string& description, float progress) {
    for (auto& g : goals) {
        if (g.description == description) {
            g.progress = std::clamp(progress, 0.0f, 1.0f);
            if (g.progress >= 1.0f) g.status = "completed";
            break;
        }
    }
}

void NPC::set_emotion(EmotionType emotion, float intensity) {
    current_emotion = emotion;
    emotion_intensity = std::clamp(intensity, 0.0f, 1.0f);
}

void NPC::decay_emotion(float rate) {
    emotion_intensity = std::max(0.0f, emotion_intensity - rate);
    if (emotion_intensity <= 0.01f) {
        current_emotion = EmotionType::Neutral;
        emotion_intensity = 0.0f;
    }
}

std::string NPC::get_scheduled_activity(uint8_t hour) const {
    for (const auto& entry : schedule) {
        uint8_t end_hour = (entry.start_hour + entry.duration_hours) % 24;
        if (entry.start_hour <= end_hour) {
            if (hour >= entry.start_hour && hour < end_hour) return entry.activity;
        } else {
            // Wraps midnight
            if (hour >= entry.start_hour || hour < end_hour) return entry.activity;
        }
    }
    return "idle";
}

void NPC::prune_memories(size_t max_memories) {
    if (memories.size() <= max_memories) return;
    
    // Sort by importance * confidence (keep most important)
    std::sort(memories.begin(), memories.end(), [](const Memory& a, const Memory& b) {
        return (a.importance * a.confidence) > (b.importance * b.confidence);
    });
    memories.resize(max_memories);
}

} // namespace ashgrove