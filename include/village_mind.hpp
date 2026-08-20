#pragma once

#include "cognitive_registry.hpp"
#include "world.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ashgrove {

// A single aggregate episodic record for the village's collective memory.
struct VillageMemoryRecord {
  uint32_t day = 0;
  std::string event_type;   // "festival", "disaster", "npc_departed", "panic", ...
  std::string detail;       // human-readable summary
  float emotional_weight = 0.0f;  // magnitude of collective emotional impact
};

// VillageMind: aggregate NPC collective cognition.
// Aggregates all important NPCs' emotional/social state into a single
// collective "mood" that biases the town. This is Tier 1 (deterministic),
// reads per-agent CognitiveState via CognitiveRegistry, and pushes biases
// into World adaptation scalars:
//   - schedule_bias            -> npc schedules (via snapshot / schedule)
//   - economy_market_volatility -> economy price swings
//   - horror_night_event_weight -> horror event frequency
//   - horror_intensity         -> overall horror escalation
//
// Not a brain that "acts"; it accumulates memory and emits biases, so the
// town's mood emerges from the aggregate of individual NPC cognition.
class VillageMind {
 public:
  VillageMind(World* world, CognitiveRegistry* registry);
  ~VillageMind() {}

  // Called once per in-game day (04:00), after TownConsciousness consolidation.
  void tick(uint32_t current_day);

  // Record a major collective event into village memory.
  void record_event(const std::string& event_type,
                    const std::string& detail,
                    float emotional_weight);

  // Push aggregate biases into World adaptation scalars.
  void push_adaptations();

  // Inspection snapshot for /town/village.
  struct Snapshot {
    uint32_t day;
    std::size_t npc_count;
    float mean_valence;          // -1..1
    float mean_arousal;          // 0..1
    float average_edge_trust;    // 0..1 social cohesion
    float collective_fear;       // 0..1 (aggregate fear emotion)
    float collective_joy;        // 0..1 (aggregate joy emotion)
    float schedule_bias;         // npc schedule shift, -0.2..0.2
    float market_volatility;     // 0..1
    float horror_night_event_weight; // 0..2
    float horror_intensity;      // 0..1
    std::vector<VillageMemoryRecord> recent_memory; // last ~10
  };
  Snapshot get_snapshot() const;

  // Serialization for ROADMAP 2.10 (Hidden State Persistence)
  std::string to_json() const;
  bool from_json(const std::string& json_str);

  // File-based load/save (ROADMAP 2.10)
  void load(const std::string& path);
  void save(const std::string& path) const;

 private:
  World* world_;
  CognitiveRegistry* registry_;

  // Collective emotional state (bounded, slowly adapting)
  float collective_fear_ = 0.0f;
  float collective_joy_ = 0.0f;
  float collective_trust_ = 0.5f;
  float collective_anxiety_ = 0.0f;

  // Biases emitted to World
  float schedule_bias_ = 0.0f;         // -0.2..0.2
  float market_volatility_ = 0.0f;     // 0..1
  float horror_night_event_weight_ = 1.0f; // 0..2
  float horror_intensity_ = 0.0f;      // 0..1

  // Collective episodic memory (bounded, last 256)
  std::deque<VillageMemoryRecord> memory_;

  void aggregate_emotional_state();
  void update_collective_mood();
  void decay_memory();
};

}  // namespace ashgrove