#include "npc/dialogue.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace ashgrove {

bool PlayerKnowledge::knows(const std::string& title) const {
    return std::find(knowledge_titles.begin(), knowledge_titles.end(), title) != knowledge_titles.end();
}

bool PlayerKnowledge::knows_evidence(const std::string& title) const {
    return std::find(evidence_titles.begin(), evidence_titles.end(), title) != evidence_titles.end();
}

nlohmann::json DialogueLine::serialize() const {
    return nlohmann::json{
        {"speaker_id", speaker_id},
        {"text", text},
        {"affinity_delta", affinity_delta},
        {"trust_delta", trust_delta},
        {"knowledge_unlocked", knowledge_unlocked},
        {"evidence_gained", evidence_gained},
        {"rumor_spread", rumor_spread}
    };
}

nlohmann::json ConversationTopic::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"label", label},
        {"requires_knowledge", requires_knowledge},
        {"requires_affinity", requires_affinity},
        {"available", available}
    };
}

std::vector<ConversationTopic> DialogueSystem::topics_for(const NPC& npc, const PlayerKnowledge& player) const {
    std::vector<ConversationTopic> topics;

    topics.push_back({"op", "What do you do here?", {}, -1.0f, true});
    topics.push_back({"goals", "How is life in Ashgrove?", {}, -1.0f, true});
    if (!npc.relationships.empty()) {
        topics.push_back({"relationships", "People around here?", {}, -0.2f, true});
    }
    if (!npc.beliefs.empty()) {
        topics.push_back({"beliefs", "What do you believe about this place?", {}, 0.0f, true});
    }

    if (npc.tier == NPCTier::Tier1_Major) {
        topics.push_back({"history", "Tell me about the village's history", {}, -1.0f, false});
        topics.push_back({"disappearance", "About the old disappearance...", {}, -1.0f, false});
    }

    if (player.knows("The Disappearance")) {
        for (auto& t : topics) {
            if (t.id == "disappearance") t.available = true;
        }
    }
    // Evidence unlocks the deeper ask: what happened that night?
    if (player.knows_evidence("Old Miller's Journal") || player.knows_evidence("Rusted Key")) {
        topics.push_back({"miller_night", "What really happened the night the miller vanished?", {"The Disappearance"}, 0.1f, true});
    }
    // With both pieces of evidence in hand, the elders will finally speak plainly.
    if (player.knows_evidence("Old Miller's Journal") && player.knows_evidence("Rusted Key")) {
        topics.push_back({"miller_conclusion", "I have the journal and the key. Tell me the truth.", {"The Disappearance"}, 0.2f, true});
    }
    if (player.knows("The Great Fire")) {
        for (auto& t : topics) {
            if (t.id == "history") t.available = true;
        }
    }

    return topics;
}

DialogueLine DialogueSystem::greeting(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.tier == NPCTier::Tier1_Major) {
        line.text = "\"A visitor asking questions. That's a rare thing in Ashgrove.\" The older one sizes you up. \"Ask what you came for — but mind the answers you truly want.\"";
    } else {
        line.text = npc.occupation.empty()
            ? "\"Morning. Stay careful out here.\""
            : "\"Morning. If you need the " + npc.occupation + ", I'll hear you out.\"";
    }
    return line;
}

DialogueLine DialogueSystem::respond(const NPC& npc, const std::string& topic_id, const PlayerKnowledge&) const {
    if (topic_id == "op") return respond_about_occupation(npc);
    if (topic_id == "goals") return respond_about_goals(npc);
    if (topic_id == "relationships") return respond_about_relationships(npc);
    if (topic_id == "beliefs") return respond_about_beliefs(npc);
    if (topic_id == "disappearance") return respond_about_disappearance(npc);
    if (topic_id == "miller_night") return respond_about_miller_night(npc);
    if (topic_id == "miller_conclusion") return respond_about_miller_conclusion(npc);
    if (topic_id == "history") return respond_about_history(npc);
    if (topic_id == "memories") return respond_about_memories(npc);
    return generic_response(npc, topic_id);
}

DialogueLine DialogueSystem::respond_about_occupation(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.occupation.empty()) {
        line.text = npc.name + " shrugs. \"No official title. I get by.\"";
        return line;
    }
    if (npc.occupation == "Mayor") {
        line.text = "\"I'm the mayor of Ashgrove — a keeper of records and a keeper of peace. The village runs on routine; I keep it that way.\"";
    } else if (npc.occupation == "Pastor") {
        line.text = "\"I tend St. Willow's Chapel. I've buried more years in this valley than I have ahead of me — and heard more than a few unpaid confessions.\"";
    } else if (npc.occupation == "Doctor") {
        line.text = "\"I'm the town doctor. People come to me with fevers, bruises, and the occasional thing they can't explain to anyone else.\"";
    } else if (npc.occupation == "Innkeeper") {
        line.text = "\"I keep The Sleeping Fox — the eaves of the village. Ale, beds, and ears. Lots and ears.\"";
    } else if (npc.occupation == "Blacksmith") {
        line.text = "\"I hammer iron at the forge. What whether the village needs, I shape for them.\"";
    } else {
        line.text = "I'm the " + npc.occupation + " around here.";
    }
    return line;
}

DialogueLine DialogueSystem::respond_about_goals(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.goals.empty()) {
        line.text = npc.name + " sighs. \"Just make it through, like everyone else in this valley.\"";
        return line;
    }
    const auto& g = npc.goals.front();
    line.text = npc.name + " says, \"Mostly, I'm trying to " + g.description + ".\"";
    if (g.progress > 0.5f) line.text += " We're making headway.";
    return line;
}

DialogueLine DialogueSystem::respond_about_memories(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    auto mems = npc.recall("miller", 3);
    if (mems.empty()) mems = npc.recall("disappear", 3);
    if (mems.empty()) {
        line.text = npc.name + " looks off. \"I don't remember much about that, and I'd rather not drag it up.\"";
        return line;
    }
    const auto& mem = mems.front();
    if (mem.is_false) {
        line.text = npc.name + " is uneasy. \"Word is it happened one way, but I recall it differently. Still — folk talk enough about it.\"";
        line.trust_delta = 0.05f;
    } else {
        line.text = npc.name + " recalls: \"" + mem.description + "\"";
        line.trust_delta = 0.05f;
        line.affinity_delta = 0.05f;
    }
    return line;
}

DialogueLine DialogueSystem::respond_about_disappearance(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.occupation == "Mayor") {
        line.text = "\"That case was closed a decade ago. The miller left overnight, we did our due diligence. I'd leave the old dust where it lies.\"";
        line.affinity_delta = -0.1f;
    } else if (npc.occupation == "Pastor") {
        line.text = "\"The records say one thing, the woods say another. The night before the last festival, something was taken from the valley. I said a prayer over an empty casket.\"";
        line.knowledge_unlocked = {"The Disappearance"};
    } else if (npc.occupation == "Doctor") {
        line.text = "\"Three disappearances in a decade: the miller, and two others in 'left town' notices. That's not townsfolk wandering off, that's a pattern being swept. I filed every one.\"";
        line.knowledge_unlocked = {"The Disappearance"};
    } else if (npc.occupation == "Innkeeper") {
        line.text = "\"The night the miller vanished, half the village ended to my bar. Folks went quiet in a way that wasn't grief. It was fear.\"";
        line.knowledge_unlocked = {"The Disappearance"};
        line.affinity_delta = 0.1f;
    } else {
        line.text = npc.name + " shakes their head. \"We really don't discuss the miller. Ask the elders.\"";
    }
    return line;
}

DialogueLine DialogueSystem::respond_about_miller_night(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.occupation == "Mayor") {
        line.text = "\"...He was seen at the river that night. By me. He went in at the ford and never came back up. He'd found the journal page too — about looking at the water. Folks wanted a story, so we gave them a story.\"";
        line.affinity_delta = -0.05f;
        line.trust_delta = 0.15f;
        line.knowledge_unlocked = {"The Disappearance"};
    } else if (npc.occupation == "Pastor") {
        line.text = "\"The river at night does not take kindly to being looked at. The miller learned that; so did the ones who went looking for him. I do not ask what follows the light on the water.\"";
        line.trust_delta = 0.1f;
        line.knowledge_unlocked = {"The Disappearance"};
    } else if (npc.occupation == "Doctor") {
        line.text = "\"His body was never found, but the key to the mill house was. It was dry — river-salted, but dry. Someone carried it back. I never could square that with a drowning.\"";
        line.trust_delta = 0.1f;
        line.knowledge_unlocked = {"The Disappearance"};
    } else if (npc.occupation == "Innkeeper") {
        line.text = "\"The night he vanished, he was in my bar until moonrise, calm as a pond. Next morning the whole village went quiet and stayed quiet. Nothing about a missing man should ever go quiet, and ours did.\"";
        line.affinity_delta = 0.1f;
        line.knowledge_unlocked = {"The Disappearance"};
    } else {
        line.text = npc.name + " shakes their head. \"The river took him, is all I know. Ask the elders — they know the rest.\"";
    }
    return line;
}

DialogueLine DialogueSystem::respond_about_miller_conclusion(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.occupation == "Mayor") {
        line.text = "\"Very well. The truth: the miller wasn't the first, and he wasn't the last. Every decade, the river wants one of us at the ford — and we've always paid. The key kept the mill latched so the empty room couldn't be seen from the square. You hold the evidence of what I've stood guard over for ten years. What will you do with it?\"";
        line.affinity_delta = -0.1f;
        line.trust_delta = 0.2f;
        line.knowledge_unlocked = {"The Disappearance"};
    } else if (npc.occupation == "Pastor") {
        line.text = "\"You've walked the full circle, then — the ink and the iron both. The village feeds the river a sacrifice once a decade and calls it progress. Whatever you decide, say a blessing over the water first.\"";
        line.knowledge_unlocked = {"The Disappearance"};
        line.trust_delta = 0.1f;
    } else {
        line.text = npc.name + " looks at you with something like relief. \"If you can prove what happened to the miller, you'll do what none of us dared: make the village say his name out loud. We'll back you.\"";
        line.affinity_delta = 0.15f;
        line.trust_delta = 0.1f;
        line.knowledge_unlocked = {"The Disappearance"};
    }
    return line;
}

DialogueLine DialogueSystem::respond_about_history(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.occupation == "Mayor") {
        line.text = "\"Sixty years ago the northern quarter burned — chimney fire, on the record. Rebuilt and better for it. That's the history written plainly.\"";
    } else if (npc.occupation == "Pastor") {
        line.text = "\"The chapel sits on the Old Mound, older than the village. They built the town on what came before, then buried it in stone and silence.\"";
        line.knowledge_unlocked = {"The Great Fire"};
    } else if (npc.occupation == "Doctor") {
        line.text = "\"The old fire? Paper says embers died, but careful folk say it burned cold and blue. Nobody checks the church cellar.\"";
        line.knowledge_unlocked = {"The Great Fire"};
    } else {
        line.text = npc.name + " tells a fable about the north quarter burning itself, then goes quiet.";
    }
    return line;
}

DialogueLine DialogueSystem::respond_about_beliefs(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.beliefs.empty()) {
        line.text = npc.name + ": \"I don't put much stock in the unseen. There's enough to tend to around here.\"";
        return line;
    }
    auto it = std::find_if(npc.beliefs.begin(), npc.beliefs.end(),
        [](const Belief& b) { return b.is_core; });
    if (it != npc.beliefs.end()) {
        line.text = npc.name + " believes firmly: \"" + it->proposition + "\"";
    } else {
        const auto& b = npc.beliefs.back();
        line.text = npc.name + " considers: \"" + b.proposition + "\"";
    }
    return line;
}

DialogueLine DialogueSystem::respond_about_relationships(const NPC& npc) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (npc.relationships.empty()) {
        line.text = npc.name + " keeps to themselves. \"I talk when there's a reason.\"";
        return line;
    }
    const auto& rel = *npc.relationships.begin();
    line.text = npc.name + " speaks of a " + rel.type + ": ";
    if (rel.affinity > 0.3f) line.text += "\"close enough to trust.\"";
    else if (rel.affinity < -0.3f) line.text += "\"someone best kept at arm's length.\"";
    else line.text += "\"things are weathered.\"";
    return line;
}

DialogueLine DialogueSystem::generic_response(const NPC& npc, const std::string& question) const {
    DialogueLine line;
    line.speaker_id = npc.id;
    if (question.find("weather") != std::string::npos) {
        line.text = npc.name + ": \"The valley weather shifts quickly. You'd best respect it.\"";
    } else {
        line.text = npc.name + " narrows their eyes. \"Some questions in Ashgrove are closed matters — be careful which doors you nudged.\"";
    }
    return line;
}

} // namespace ashgrove