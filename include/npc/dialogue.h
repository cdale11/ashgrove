#pragma once

#include "common/types.h"
#include "npc/npc.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ashgrove {

struct DialogueLine {
    EntityID speaker_id = INVALID_ENTITY_ID;
    std::string text;
    // Post-response effects applied by the simulation (validated)
    float affinity_delta = 0.0f;
    float trust_delta = 0.0f;
    std::vector<std::string> knowledge_unlocked; // knowledge titles gained
    std::vector<std::string> evidence_gained;    // evidence names gained
    std::vector<std::string> rumor_spread;       // rumors the NPC now believes

    nlohmann::json serialize() const;
};

// A conversational prompt the player may raise. (Distinct from investigation's
// static topic templates; these are computed from live simulation state.)
struct ConversationTopic {
    std::string id;              // stable id, e.g. "ask_miller"
    std::string label;           // shown to the player
    std::vector<std::string> requires_knowledge; // titles the player must know
    float requires_affinity = -1.0f; // minimum affinity (or -1 = none)
    bool available = true;

    nlohmann::json serialize() const;
};

// Minimal snapshot of what the player knows, used to gate dialogue.
struct PlayerKnowledge {
    std::vector<std::string> knowledge_titles;
    float reputation = 0.0f;

    bool knows(const std::string& title) const;
};

// Generates NPC responses based purely on simulation state.
// (The LLM layer, when enabled, may rewrite the *text* — never the effects.)
class DialogueSystem {
public:
    DialogueSystem() = default;

    // Topics the player can raise with this NPC right now.
    std::vector<ConversationTopic> topics_for(const NPC& npc, const PlayerKnowledge& player) const;

    // Response to a chosen topic. Pure simulation; LLM layer may rephrase text later.
    DialogueLine respond(const NPC& npc, const std::string& topic_id, const PlayerKnowledge& player) const;

    // Opening line when starting a conversation.
    DialogueLine greeting(const NPC& npc) const;

    // Response to an open-ended player question (topic-free).
    DialogueLine generic_response(const NPC& npc, const std::string& question) const;

private:
    // Builders
    DialogueLine respond_about_memories(const NPC& npc) const;
    DialogueLine respond_about_beliefs(const NPC& npc) const;
    DialogueLine respond_about_goals(const NPC& npc) const;
    DialogueLine respond_about_relationships(const NPC& npc) const;
    DialogueLine respond_about_occupation(const NPC& npc) const;
    DialogueLine respond_about_disappearance(const NPC& npc) const;
    DialogueLine respond_about_history(const NPC& npc) const;
};

} // namespace ashgrove