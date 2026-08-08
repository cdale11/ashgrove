#pragma once

#include "common/types.h"
#include "npc/npc.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>

namespace ashgrove {

enum class KnowledgeCategory : uint8_t {
    History,      // Village history
    Folklore,     // Legends, rumors
    Family,       // Family secrets
    Location,     // Hidden places
    Ritual,       // Rituals
    Mystery,      // Active mysteries
    Mechanics,    // Game mechanics knowledge
    Relationship, // NPC relationship info
    Evidence      // Collected evidence
};

struct Knowledge {
    EntityID id = INVALID_ENTITY_ID;
    KnowledgeCategory category = KnowledgeCategory::History;
    std::string title;
    std::string description;
    float completeness = 0.0f;  // 0-1, how much is known
    std::vector<EntityID> source_npc_ids;   // Who told the player
    std::vector<EntityID> source_item_ids;  // What items revealed it
    TimeTick discovered_at = 0;
    bool is_secret = false;                  // Hidden until discovered
    std::string unlock_requirement;          // Knowledge needed to unlock
    
    nlohmann::json serialize() const;
    static Knowledge deserialize(const nlohmann::json& j);
};

struct Evidence {
    EntityID id = INVALID_ENTITY_ID;
    std::string name;
    std::string description;
    std::vector<std::string> tags;          // For matching with dialogue/events
    float reliability = 0.5f;                // 0-1, how trustworthy
    EntityID related_npc_id = INVALID_ENTITY_ID;
    EntityID related_location_id = INVALID_ENTITY_ID;
    std::string acquired_from;               // How it was obtained
    TimeTick acquired_at = 0;
    bool is_contradictory = false;           // Contradicts other evidence
    
    nlohmann::json serialize() const;
    static Evidence deserialize(const nlohmann::json& j);
};

struct DialogueTopic {
    std::string id;
    std::string label;                        // Display text
    std::vector<std::string> required_knowledge; // Knowledge IDs needed
    std::vector<std::string> unlocks;         // Knowledge/evidence IDs gained
    float relationship_requirement = 0.0f;    // NPC affinity needed
    bool is_secret = false;
};

struct DialogueResponse {
    std::string text;
    std::vector<DialogueTopic> followup_topics;
    std::vector<std::string> knowledge_gained;
    std::vector<std::string> evidence_gained;
    float affinity_change = 0.0f;
    float trust_change = 0.0f;
};

class InvestigationSystem {
public:
    InvestigationSystem() = default;
    ~InvestigationSystem() = default;
    
    // Knowledge management
    void add_knowledge(const Knowledge& knowledge);
    Knowledge* get_knowledge(EntityID id);
    std::vector<Knowledge> get_knowledge_by_category(KnowledgeCategory category) const;
    std::vector<Knowledge> get_all_known() const;
    bool has_knowledge(EntityID id) const;
    float get_knowledge_completeness(const std::string& title) const;
    
    // Evidence management
    void add_evidence(const Evidence& evidence);
    Evidence* get_evidence(EntityID id);
    std::vector<Evidence> get_all_evidence() const;
    std::vector<Evidence> get_evidence_by_tag(const std::string& tag) const;
    // Non-const access for mutation by the server (marking discoveries).
    std::vector<Evidence*> all_evidence();
    
    // Deduction - find contradictions between evidence
    std::vector<std::pair<EntityID, EntityID>> find_contradictions() const;
    
    // Dialogue - determine if topics are available
    std::vector<DialogueTopic> get_available_topics(const NPC& npc, const std::vector<EntityID>& known_knowledge_ids) const;
    
    // Serialization
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json& j);
    
private:
    std::unordered_map<EntityID, Knowledge> knowledge_;
    std::unordered_map<EntityID, Evidence> evidence_;
    EntityID next_id_ = 1;
};

} // namespace ashgrove