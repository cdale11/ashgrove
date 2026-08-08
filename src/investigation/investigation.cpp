#include "investigation/investigation.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace ashgrove {

nlohmann::json Knowledge::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"category", static_cast<int>(category)},
        {"title", title},
        {"description", description},
        {"completeness", completeness},
        {"source_npc_ids", source_npc_ids},
        {"source_item_ids", source_item_ids},
        {"discovered_at", discovered_at},
        {"is_secret", is_secret},
        {"unlock_requirement", unlock_requirement}
    };
}

Knowledge Knowledge::deserialize(const nlohmann::json& j) {
    Knowledge k;
    k.id = j.value("id", INVALID_ENTITY_ID);
    k.category = static_cast<KnowledgeCategory>(j.value("category", 0));
    k.title = j.value("title", "");
    k.description = j.value("description", "");
    k.completeness = j.value("completeness", 0.0f);
    k.source_npc_ids = j.value("source_npc_ids", std::vector<EntityID>{});
    k.source_item_ids = j.value("source_item_ids", std::vector<EntityID>{});
    k.discovered_at = j.value("discovered_at", 0);
    k.is_secret = j.value("is_secret", false);
    k.unlock_requirement = j.value("unlock_requirement", "");
    return k;
}

nlohmann::json Evidence::serialize() const {
    return nlohmann::json{
        {"id", id},
        {"name", name},
        {"description", description},
        {"tags", tags},
        {"reliability", reliability},
        {"related_npc_id", related_npc_id},
        {"related_location_id", related_location_id},
        {"acquired_from", acquired_from},
        {"acquired_at", acquired_at},
        {"is_contradictory", is_contradictory}
    };
}

Evidence Evidence::deserialize(const nlohmann::json& j) {
    Evidence e;
    e.id = j.value("id", INVALID_ENTITY_ID);
    e.name = j.value("name", "");
    e.description = j.value("description", "");
    e.tags = j.value("tags", std::vector<std::string>{});
    e.reliability = j.value("reliability", 0.5f);
    e.related_npc_id = j.value("related_npc_id", INVALID_ENTITY_ID);
    e.related_location_id = j.value("related_location_id", INVALID_ENTITY_ID);
    e.acquired_from = j.value("acquired_from", "");
    e.acquired_at = j.value("acquired_at", 0);
    e.is_contradictory = j.value("is_contradictory", false);
    return e;
}

void InvestigationSystem::add_knowledge(const Knowledge& knowledge) {
    knowledge_[knowledge.id] = knowledge;
}

Knowledge* InvestigationSystem::get_knowledge(EntityID id) {
    auto it = knowledge_.find(id);
    return it != knowledge_.end() ? &it->second : nullptr;
}

std::vector<Knowledge> InvestigationSystem::get_knowledge_by_category(KnowledgeCategory category) const {
    std::vector<Knowledge> result;
    for (const auto& [id, k] : knowledge_) {
        if (k.category == category) result.push_back(k);
    }
    return result;
}

std::vector<Knowledge> InvestigationSystem::get_all_known() const {
    std::vector<Knowledge> result;
    for (const auto& [id, k] : knowledge_) {
        if (!k.is_secret || k.completeness > 0.5f) result.push_back(k);
    }
    return result;
}

bool InvestigationSystem::has_knowledge(EntityID id) const {
    return knowledge_.find(id) != knowledge_.end();
}

float InvestigationSystem::get_knowledge_completeness(const std::string& title) const {
    for (const auto& [id, k] : knowledge_) {
        if (k.title == title) return k.completeness;
    }
    return 0.0f;
}

void InvestigationSystem::add_evidence(const Evidence& evidence) {
    // Check for contradictions with existing evidence
    for (auto& [eid, existing] : evidence_) {
        if (existing.tags.size() > 0 && evidence.tags.size() > 0) {
            bool shares_tag = false;
            bool conflicts = false;
            for (const auto& tag : existing.tags) {
                if (std::find(evidence.tags.begin(), evidence.tags.end(), tag) != evidence.tags.end()) {
                    shares_tag = true;
                    // If same tag but very different reliability, likely contradictory
                    if (std::abs(existing.reliability - evidence.reliability) > 0.5f) {
                        conflicts = true;
                    }
                }
            }
            if (shares_tag && conflicts) {
                existing.is_contradictory = true;
                evidence_.begin()->second.is_contradictory = true;
            }
        }
    }
    evidence_[evidence.id] = evidence;
}

Evidence* InvestigationSystem::get_evidence(EntityID id) {
    auto it = evidence_.find(id);
    return it != evidence_.end() ? &it->second : nullptr;
}

std::vector<Evidence> InvestigationSystem::get_all_evidence() const {
    std::vector<Evidence> result;
    for (const auto& [id, e] : evidence_) result.push_back(e);
    return result;
}

std::vector<Evidence> InvestigationSystem::get_evidence_by_tag(const std::string& tag) const {
    std::vector<Evidence> result;
    for (const auto& [id, e] : evidence_) {
        if (std::find(e.tags.begin(), e.tags.end(), tag) != e.tags.end()) {
            result.push_back(e);
        }
    }
    return result;
}

std::vector<Evidence*> InvestigationSystem::all_evidence() {
    std::vector<Evidence*> result;
    for (auto& [id, e] : evidence_) result.push_back(&e);
    return result;
}

std::vector<std::pair<EntityID, EntityID>> InvestigationSystem::find_contradictions() const {
    std::vector<std::pair<EntityID, EntityID>> result;
    std::vector<Evidence> all = get_all_evidence();
    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j) {
            bool shares_tag = false;
            for (const auto& t1 : all[i].tags) {
                if (std::find(all[j].tags.begin(), all[j].tags.end(), t1) != all[j].tags.end()) {
                    shares_tag = true;
                    break;
                }
            }
            if (shares_tag && std::abs(all[i].reliability - all[j].reliability) > 0.5f) {
                result.push_back({all[i].id, all[j].id});
            }
        }
    }
    return result;
}

std::vector<DialogueTopic> InvestigationSystem::get_available_topics(const NPC& npc, const std::vector<EntityID>& known_knowledge_ids) const {
    // Placeholder dialogue system
    // In practice this would check NPC beliefs, memories, relationships
    std::vector<DialogueTopic> result;
    
    return result;
}

nlohmann::json InvestigationSystem::serialize() const {
    nlohmann::json j;
    j["next_id"] = next_id_;
    j["knowledge"] = nlohmann::json::array();
    for (const auto& [id, k] : knowledge_) j["knowledge"].push_back(k.serialize());
    j["evidence"] = nlohmann::json::array();
    for (const auto& [id, e] : evidence_) j["evidence"].push_back(e.serialize());
    return j;
}

void InvestigationSystem::deserialize(const nlohmann::json& j) {
    next_id_ = j.value("next_id", 1);
    knowledge_.clear();
    evidence_.clear();
    for (const auto& kj : j.value("knowledge", nlohmann::json::array())) {
        auto k = Knowledge::deserialize(kj);
        knowledge_[k.id] = k;
    }
    for (const auto& ej : j.value("evidence", nlohmann::json::array())) {
        auto e = Evidence::deserialize(ej);
        evidence_[e.id] = e;
    }
}

} // namespace ashgrove