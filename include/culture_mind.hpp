#pragma once

#include "cognitive_registry.hpp"
#include "world.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ashgrove {

// CultureMind: aggregate collective-culture cognition.
// Aggregates shared beliefs, rituals, collective fears, and preferences from
// all NPCs' semantic memory (via CognitiveRegistry::collect_semantic_facts),
// plus observed cultural practices. Emits biases:
//   - schedule_bias       : rituals/gatherings shift NPC schedules
//   - dialogue_topic_weight : which topics NPCs discuss (fears, joys, traditions)
//
// Tier 1 (deterministic). Accumulates a culture_memory of shared beliefs and
// practice frequencies. Does NOT call the LLM.
class CultureMind {
 public:
  CultureMind(World* world, CognitiveRegistry* registry);
  ~CultureMind() {}

  // Called once per in-game day (04:00), after EconomyMind.
  void tick(uint32_t current_day);

  // Record a cultural practice occurrence (festival attended, ritual performed).
  void record_practice(const std::string& practice, float weight);

  // Push biases into World adaptation scalars / snapshot.
  void push_adaptations();

  // Inspection snapshot for /town/culture.
  struct Snapshot {
    uint32_t day;
    float cultural_cohesion;           // 0..1 how shared beliefs are
    float collective_fear;             // 0..1 (fear predicates consensus)
    float collective_joy;              // 0..1 (joy predicates consensus)
    float schedule_bias;               // -0.2..0.2
    float dialogue_topic_weight;       // 0..2
    std::vector<std::string> shared_beliefs;     // top consensus facts
    std::vector<std::string> shared_fears;       // "fears:*" facts
    std::map<std::string, float> practice_frequency; // practice -> count
  };
  Snapshot get_snapshot() const;

 private:
  World* world_;
  CognitiveRegistry* registry_;

  float cultural_cohesion_ = 0.0f;
  float collective_fear_ = 0.0f;
  float collective_joy_ = 0.0f;
  float schedule_bias_ = 0.0f;
  float dialogue_topic_weight_ = 1.0f;

  std::vector<std::string> shared_beliefs_;
  std::vector<std::string> shared_fears_;
  std::map<std::string, float> practice_frequency_;

  void aggregate_culture();
  void update_biases();
};

}  // namespace ashgrove