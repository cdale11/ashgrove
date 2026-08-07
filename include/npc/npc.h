#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>

namespace ashgrove {

enum class NPCTier : uint8_t { Tier1_Major, Tier2_Persistent, Tier3_Background };
enum class EmotionType : uint8_t { Neutral, Happy, Sad, Angry, Fearful, Disgusted, Surprised, Anxious, Content, Suspicious };

struct Memory {
    EntityID event_id = INVALID_ENTITY_ID;
    TimeTick timestamp = 0;
    std::string description;        // What happened
    std::vector<EntityID> participants;
    float importance = 0.5f;        // 0-1, affects retention
    float emotional_valence = 0.0f; // -1 to 1 (negative to positive)
    bool is_false = false;          // Misremembered
    float confidence = 1.0f;        // How sure the NPC is
    std::string source;             // "witnessed", "heard", "inferred", "told_by_X"
    
    nlohmann::json serialize() const;
    static Memory deserialize(const nlohmann::json& j);
};

struct Belief {
    std::string proposition;        // e.g., "The mayor is corrupt", "Ghosts exist"
    float confidence = 0.5f;        // 0-1
    TimeTick formed_at = 0;
    TimeTick last_reinforced = 0;
    std::string evidence;           // Supporting memories/observations
    bool is_core = false;           // Core beliefs resist change
    
    nlohmann::json serialize() const;
    static Belief deserialize(const nlohmann::json& j);
};

struct Relationship {
    EntityID target_id = INVALID_ENTITY_ID;
    float affinity = 0.0f;          // -1 to 1 (hate to love)
    float trust = 0.0f;             // -1 to 1
    float familiarity = 0.0f;       // 0-1
    std::string type;               // "family", "friend", "rival", "lover", "stranger", "authority"
    TimeTick last_interaction = 0;
    std::vector<std::string> history; // Key moments
    
    nlohmann::json serialize() const;
    static Relationship deserialize(const nlohmann::json& j);
};

struct Goal {
    std::string description;
    float priority = 0.5f;          // 0-1
    float progress = 0.0f;          // 0-1
    TimeTick created_at = 0;
    TimeTick deadline = 0;          // 0 = no deadline
    std::string status;             // "active", "completed", "failed", "abandoned"
    std::vector<std::string> subgoals;
    
    nlohmann::json serialize() const;
    static Goal deserialize(const nlohmann::json& j);
};

struct PersonalityTrait {
    std::string name;               // "openness", "conscientiousness", "extraversion", "agreeableness", "neuroticism"
    float value = 0.5f;             // 0-1
};

struct DailyScheduleEntry {
    uint8_t start_hour = 0;
    uint8_t duration_hours = 1;
    std::string activity;           // "sleep", "work", "eat", "socialize", "patrol", "pray", "craft"
    EntityID location_id = INVALID_ENTITY_ID;
    std::optional<std::string> companion_id; // Optional NPC to do activity with
};

class NPC {
public:
    NPC() = default;
    NPC(EntityID id, const std::string& name, NPCTier tier);
    virtual ~NPC() = default;
    
    // Identity
    EntityID id = INVALID_ENTITY_ID;
    std::string name;
    std::string surname;
    NPCTier tier = NPCTier::Tier3_Background;
    uint8_t age = 25;
    std::string gender;
    std::string occupation;
    
    // Physical state
    Position position;
    float health = 100.0f;
    float hunger = 0.0f;        // 0-100
    float fatigue = 0.0f;       // 0-100
    float temperature = 37.0f;  // Body temp
    std::vector<std::string> injuries;
    std::vector<std::string> illnesses;
    
    // Cognitive (Tier 1 & 2)
    std::vector<Memory> memories;
    std::vector<Belief> beliefs;
    std::vector<Relationship> relationships;
    std::vector<Goal> goals;
    std::vector<PersonalityTrait> personality;
    EmotionType current_emotion = EmotionType::Neutral;
    float emotion_intensity = 0.0f;
    
    // Routine (Tier 2 & 3)
    std::vector<DailyScheduleEntry> schedule;
    std::string current_activity = "idle";
    
    // Social
    float reputation = 0.0f;    // -100 to 100
    float wealth = 0.0f;
    EntityID home_id = INVALID_ENTITY_ID;
    EntityID workplace_id = INVALID_ENTITY_ID;
    std::vector<EntityID> family_ids;
    
    // LLM integration (Tier 1 only)
    std::string llm_context;    // Serialized context for LLM
    std::string llm_model;      // Model to use
    bool needs_llm_update = false;
    
    // Serialization
    virtual nlohmann::json serialize() const;
    virtual void deserialize(const nlohmann::json& j);
    
    // Memory management
    void add_memory(const Memory& mem);
    std::vector<Memory> recall(const std::string& query, int limit = 5) const;
    void forget(EntityID event_id);
    void reinforce_memory(EntityID event_id, float amount);
    
    // Belief management
    void add_belief(const Belief& belief);
    void update_belief(const std::string& proposition, float confidence_change);
    float get_belief_confidence(const std::string& proposition) const;
    
    // Relationship management
    void modify_relationship(EntityID target, float affinity_delta, float trust_delta);
    Relationship* get_relationship(EntityID target);
    const Relationship* get_relationship(EntityID target) const;
    
    // Goals
    void add_goal(const Goal& goal);
    void update_goal_progress(const std::string& description, float progress);
    
    // Emotion
    void set_emotion(EmotionType emotion, float intensity);
    void decay_emotion(float rate = 0.1f);
    
    // Schedule
    std::string get_scheduled_activity(uint8_t hour) const;
    
protected:
    void prune_memories(size_t max_memories = 1000);
};

using NPCPtr = std::shared_ptr<NPC>;

} // namespace ashgrove