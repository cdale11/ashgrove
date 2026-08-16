#include "social_cognition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace ashgrove {

namespace {

constexpr float kClamp01(float x) {
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

constexpr float kClamp11(float x) {
  return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace

SocialCognition::SocialCognition(const std::string& agent_id)
    : agent_id_(agent_id) {}

void SocialCognition::tick(uint32_t current_tick) {
  // Slow trust drift toward neutral (0.5) when no interactions
  for (auto& kv : edges_) {
    SocialEdge& edge = kv.second;
    edge.trust = kClamp01(edge.trust * 0.9995f + 0.00025f);
    // Familiarity never decays, only grows
  }
  // Enforce cap
  if (edges_.size() > SocialCognition::kMaxEdges) {
    // Remove lowest familiarity edges
    std::vector<std::pair<std::string, float>> familiarity_list;
    familiarity_list.reserve(edges_.size());
    for (const auto& kv : edges_) {
      familiarity_list.emplace_back(kv.first, kv.second.familiarity);
    }
    std::sort(familiarity_list.begin(), familiarity_list.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    std::size_t to_remove = edges_.size() - SocialCognition::kMaxEdges;
    for (std::size_t i = 0; i < to_remove; ++i) {
      edges_.erase(familiarity_list[i].first);
    }
  }
}

void SocialCognition::update_social(const std::string& other_agent_id,
                                    float valence,
                                    bool observed_positive_action) {
  SocialEdge& edge = edges_[other_agent_id];
  edge.other_agent_id = other_agent_id;
  
  // Trust update (bounded)
  float trust_delta = valence * kTrustUpdateRate;
  edge.trust = kClamp01(edge.trust + trust_delta);
  
  // Familiarity grows
  edge.familiarity = kClamp01(edge.familiarity + kFamiliarityIncrement);
  
  // Emotional history accumulates
  edge.emotional_history_sum = kClamp11(edge.emotional_history_sum + valence * 0.1f);
  
  // Imitation target update
  if (observed_positive_action && edge.trust > 0.5f) {
    edge.imitation_target = kClamp01(edge.imitation_target + kImitationLearningRate);
  } else if (!observed_positive_action) {
    edge.imitation_target = std::max(0.0f, edge.imitation_target - 0.02f);
  }
}

const SocialEdge& SocialCognition::get_edge(const std::string& other_agent_id) const {
  auto it = edges_.find(other_agent_id);
  static const SocialEdge kDefault;
  return it != edges_.end() ? it->second : kDefault;
}

SocialEdge& SocialCognition::get_edge_mutable(const std::string& other_agent_id) {
  return edges_[other_agent_id];
}

std::vector<SocialCognition::TransmittedFact> SocialCognition::propagate_beliefs(
    const std::map<std::string, SemanticFact>& local_semantic_memory) {
  std::vector<TransmittedFact> transmitted;
  transmitted.reserve(local_semantic_memory.size());
  
  for (const auto& kv : local_semantic_memory) {
    const SemanticFact& fact = kv.second;
    // Only propagate high-confidence facts
    if (fact.confidence > 0.6f) {
      transmitted.push_back({fact.subject + "|" + fact.predicate + "|" + fact.object, fact.confidence});
    }
  }
  return transmitted;
}

bool SocialCognition::receive_belief(const std::string& source_agent_id,
                                     const SemanticFact& fact,
                                     float source_confidence) {
  // Find or create edge to source
  auto& edge = edges_[source_agent_id];
  edge.other_agent_id = source_agent_id;
  edge.familiarity = kClamp01(edge.familiarity + kFamiliarityIncrement);
  
  // Weight by source trust * source confidence
  float effective_confidence = source_confidence * edge.trust;
  if (effective_confidence < 0.3f) return false;
  
  // In a full implementation, this would update the agent's semantic memory
  // For now, we track the received belief in the edge
  edge.emotional_history_sum = kClamp11(edge.emotional_history_sum + 0.05f);
  return true;
}

void SocialCognition::observe_action(const std::string& other_agent_id,
                                     std::size_t action_type,
                                     float outcome_valence) {
  // Record observation for imitation learning
  update_social(other_agent_id, outcome_valence, outcome_valence > 0.2f);
  
  // In a full implementation, this would update action preferences
  // based on the observed successful action
  (void)action_type;  // Used in full implementation
}

const std::map<std::string, SocialEdge>& SocialCognition::all_edges() const {
  return edges_;
}

std::string SocialCognition::to_json() const {
  std::ostringstream o;
  o << "{\"agent_id\":\"" << json_escape(agent_id_) << "\",";
  o << "\"edges\":[";
  bool first = true;
  for (const auto& kv : edges_) {
    if (!first) o << ",";
    first = false;
    const SocialEdge& e = kv.second;
    o << "{\"other\":\"" << json_escape(e.other_agent_id) << "\",";
    o << "\"trust\":" << e.trust << ",";
    o << "\"familiarity\":" << e.familiarity << ",";
    o << "\"emotional_history\":" << e.emotional_history_sum << ",";
    o << "\"imitation\":" << e.imitation_target << "}";
  }
  o << "]}";
  return o.str();
}

bool SocialCognition::from_json(const std::string& json_str) {
  // Minimal parser - relies on structure from to_json()
  auto find_float = [&](const std::string& key, float& dst) -> bool {
    auto k = json_str.find("\"" + key + "\":");
    if (k == std::string::npos) return false;
    auto v = json_str.find_first_of("-0123456789", k + key.size() + 3);
    if (v == std::string::npos) return false;
    try { dst = std::stof(json_str.substr(v, 32)); return true; }
    catch (...) { return false; }
  };
  
  // In a real implementation, use nlohmann::json
  // For now, just validate structure
  return json_str.find("\"agent_id\"") != std::string::npos &&
         json_str.find("\"edges\"") != std::string::npos;
}

}  // namespace ashgrove