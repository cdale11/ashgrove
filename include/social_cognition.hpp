#pragma once

#include "cognitive_state.hpp"

#include <map>
#include <string>
#include <vector>

namespace ashgrove {

// SocialCognition: manages an agent's social relationships, imitation, 
// cultural transmission, and belief propagation.
//
// Thread-safety: called from agent's CognitiveCore tick (single-threaded per agent).
// All mutations go through update_social() and propagate_beliefs().

class SocialCognition {
 public:
  explicit SocialCognition(const std::string& agent_id);
  ~SocialCognition() = default;

  // Called each tick: updates familiarity, decays trust, processes imitation.
  void tick(uint32_t current_tick);

  // Record an interaction with another agent.
  // valence: -1.0 (hostile) to 1.0 (positive)
  // observed_positive_action: did the other agent do something worth imitating?
  void update_social(const std::string& other_agent_id,
                     float valence,
                     bool observed_positive_action);

  // Get social edge for another agent (creates if not exists).
  const SocialEdge& get_edge(const std::string& other_agent_id) const;
  SocialEdge& get_edge_mutable(const std::string& other_agent_id);

  // Belief propagation: share semantic facts with connected agents.
  // Returns list of (fact_id, confidence) pairs that were transmitted.
  struct TransmittedFact {
    std::string fact_id;
    float confidence;
  };
  std::vector<TransmittedFact> propagate_beliefs(
      const std::map<std::string, SemanticFact>& local_semantic_memory);

  // Receive a belief from another agent (called by their propagation).
  // Returns true if belief was adopted/updated.
  bool receive_belief(const std::string& source_agent_id,
                      const SemanticFact& fact,
                      float source_confidence);

  // Imitation: observe another agent's successful action.
  // action_type: index into action space (0=go, 1=interact, 2=talk, 3=repair, 4=harvest, 5=rest)
  // outcome_valence: how positive was the outcome (-1 to 1)
  void observe_action(const std::string& other_agent_id,
                      std::size_t action_type,
                      float outcome_valence);

  // Get all social edges (for inspection/debug).
  const std::map<std::string, SocialEdge>& all_edges() const;

  // Serialization for persistence.
  std::string to_json() const;
  bool from_json(const std::string& json_str);

 private:
  std::string agent_id_;
  mutable std::map<std::string, SocialEdge> edges_;
  
  // Imitation learning rates
  static constexpr float kImitationLearningRate = 0.05f;
  static constexpr float kTrustUpdateRate = 0.05f;
  static constexpr float kFamiliarityIncrement = 0.02f;
  
  // Caps
  static constexpr std::size_t kMaxEdges = 32;
};

}  // namespace ashgrove