#pragma once

#include "cognitive_core.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ashgrove {

// CognitiveRegistry: per-process registry of all agent CognitiveCores.
// Owned by the game loop; consulted whenever an NPC needs to:
//   - tick their cognitive state (every game tick)
//   - record an event
//   - update drives (after eating, sleeping, socializing)
//   - select an action (replaces naive schedule_slot for some NPCs)
//   - persist/load state across sessions
//
// Storage: data/npc_cognitive_state/<agent_id>.json (one file per agent).

class CognitiveRegistry {
 public:
  static CognitiveRegistry& instance();

  // Get or create a CognitiveCore for the given agent_id.
  CognitiveCore& get_or_create(const std::string& agent_id);

  // Remove an agent (e.g. when NPC permanently leaves the world).
  void remove(const std::string& agent_id);

  // Save all agent states to disk.
  bool save_all(const std::string& dir) const;

  // Load all agent states from disk (creating missing ones as empty).
  std::size_t load_all(const std::string& dir);

  // Tick all agents. Called once per game tick from World::tick().
  // `observed_stimuli` is the per-agent list of stimuli observed this tick.
  // For now, all agents see the same stimuli; per-agent filtering happens
  // inside CognitiveCore::tick() via the attention gate.
  void tick_all(uint32_t current_tick,
                const std::vector<std::string>& observed_stimuli);

  // Aggregate stats for LLM summarization (Town Consciousness) and
  // /town/inspect endpoint. Returns mean valence/arousal across all agents.
  void aggregate_stats(float& out_mean_valence,
                       float& out_mean_arousal,
                       std::size_t& out_agent_count) const;

  std::size_t size() const;

 private:
  CognitiveRegistry() = default;
  mutable std::recursive_mutex mtx_;
  std::unordered_map<std::string, std::unique_ptr<CognitiveCore>> cores_;
};

}  // namespace ashgrove
