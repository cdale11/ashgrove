#pragma once

#include "cognitive_state.hpp"
#include "social_cognition.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ashgrove {

// CognitiveCore: orchestrator for one agent's persistent cognitive state.
//
// Holds the agent's CognitiveState + the small fixed MLPs (attention,
// action_evaluator, world_model). Provides per-tick update() and saves
// state to disk for persistence across sessions.
//
// IMPORTANT: This is Tier 1 cognition. No LLM calls. All updates run in
// sub-millisecond time and are deterministic given the input events.
//
// Thread-safety: each agent has its own CognitiveCore instance; tick()
// is single-threaded per agent. Save/load acquire an internal mutex.
// Uses std::recursive_mutex to allow subconscious_replay() to be called
// from within tick() which already holds the lock.

class CognitiveCore {
 public:
  explicit CognitiveCore(std::string agent_id);
  ~CognitiveCore() = default;

  // Per-tick update. `events` is the list of events observed this tick
  // (from World, NPC interactions, player actions). Tick number is the
  // current world tick (used for decay / replay timing).
  void tick(uint32_t current_tick,
            const std::vector<std::string>& observed_stimuli);

  // Update drives (called from World tick when agent consumes food, rests,
  // socializes, etc.). All values in [0,1], clamped after update.
  void update_drive(std::size_t drive_idx, float delta);
  void apply_drive_decay();  // hunger/thirst grow over time

  // Record an event into episodic memory. Emotional tag is set by the
  // event_type (e.g. "horror_event" -> fear tag; "gift_received" -> joy tag).
  void record_event(const std::string& event_type,
                    const std::string& payload_json,
                    uint32_t current_tick, uint32_t current_day,
                    const std::string& season);

  // Update social edge after interaction with another agent.
  void update_social(const std::string& other_agent_id,
                     float interaction_valence,  // in [-1, 1]
                     bool observed_positive_action);

  // Subconscious replay: pick 3-5 high-emotional-tag events and reinforce.
  // Called during sleep/rest events.
  void subconscious_replay(uint32_t current_tick);

  // Action selection: returns index of highest-scored action (0..5).
  // Maps to: go, interact, talk, repair, harvest, rest.
  std::size_t select_action() const;

  // Serialize state to JSON for persistence (must hold mtx_).
  std::string to_json_locked() const;
  bool from_json_locked(const std::string& json_str);

  // File-based save/load (data/npc_cognitive_state/<agent_id>.json).
  bool save(const std::string& dir) const;
  bool load(const std::string& dir);

  // Accessors
  const CognitiveState& state() const { return state_; }
  CognitiveState& mutable_state() { return state_; }
  const std::string& agent_id() const { return agent_id_; }
  SocialCognition& social_cognition() { return social_cognition_; }
  const SocialCognition& social_cognition() const { return social_cognition_; }

 private:
  std::string agent_id_;
  CognitiveState state_;
  SocialCognition social_cognition_;
  mutable std::recursive_mutex mtx_;

  // Tiny MLPs (fixed weights at load time, no online updates to weights).
  // - attention_mlp: 4 inputs (salience features) -> 1 output (gate score)
  // - action_evaluator: 10 inputs (state summary) -> 6 outputs (action scores)
  // - world_model: 8 inputs (state + season) -> 3 outputs (predictions)
  // Weights loaded from data/mlp_weights.json at construction.

  void clamp_all();
  void apply_tick_decay();
  void maintain_caps();
  EmotionalTag tag_for_event(const std::string& event_type) const;
  float compute_attention_score(const std::string& stimulus) const;
};

}  // namespace ashgrove
