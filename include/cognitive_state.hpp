#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace ashgrove {

// CognitiveState: persistent adaptive cognitive state for one NPC (or Player).
//
// All fields are bounded to [0,1] or [-1,1] floats (after clamping). Online
// updates use small learning rates (see cognitive-architecture.md §6).
//
// This is the Tier 1 brain of the agent -- lightweight, deterministic, runs
// every tick. The LLM (Tier 2) is NOT involved in any field here.
//
// Persistence: serialized to data/npc_cognitive_state/<npc_id>.json by
// src/cognitive_core.cpp::save(); loaded by load() at game start.

struct EmotionalTag {
  // Discrete emotional labels. Stored as floats in [0,1].
  float joy = 0.0f;
  float fear = 0.0f;
  float trust = 0.0f;
  float anger = 0.0f;
  float surprise = 0.0f;
  float anticipation = 0.0f;
  float disgust = 0.0f;
};

struct DriveState {
  // Per-drive satisfaction in [0,1]. 1.0 = fully satisfied; 0.0 = desperate.
  // Bounded adaptation: drive_weights[k] modulates how much drive[k] affects
  // action selection.
  std::array<float, 6> drive_satisfaction{{
      0.5f,  // hunger
      0.5f,  // thirst
      0.5f,  // social
      0.5f,  // safety
      0.5f,  // curiosity
      0.5f,  // rest
  }};
  std::array<float, 6> drive_weights{{
      1.0f, 1.0f, 0.8f, 1.0f, 0.6f, 0.8f,
  }};

  static constexpr std::size_t kHunger = 0;
  static constexpr std::size_t kThirst = 1;
  static constexpr std::size_t kSocial = 2;
  static constexpr std::size_t kSafety = 3;
  static constexpr std::size_t kCuriosity = 4;
  static constexpr std::size_t kRest = 5;
};

struct WorkingMemoryItem {
  uint32_t tick = 0;
  std::string stimulus_ref;     // e.g. "npc:demo", "loc:forest", "event:storm"
  EmotionalTag tag;
  float decay_factor = 1.0f;    // multiplied by 0.995 per tick
  float relevance = 0.0f;       // salience score at insertion
};

struct EpisodicEvent {
  uint32_t tick = 0;
  uint32_t day = 0;
  std::string season;
  std::string event_type;       // e.g. "rain_start", "npc_met", "gift_received"
  std::string payload_json;     // event-specific data
  EmotionalTag tag;
  float confidence = 1.0f;      // decays at 0.998 per tick; reinforced by replay
};

struct SemanticFact {
  std::string subject;          // e.g. "npc:demo", "loc:forest", "self"
  std::string predicate;        // e.g. "prefers", "fears", "trusts"
  std::string object;           // e.g. "gift:wine", "rain", "merchant"
  float confidence = 0.5f;
  uint32_t last_updated_tick = 0;
  std::vector<std::string> source_agent_ids;  // who taught us this
};

struct SocialEdge {
  std::string other_agent_id;   // "npc:demo", "player", etc.
  float trust = 0.5f;           // in [0,1]
  float familiarity = 0.0f;     // in [0,1], grows with interactions
  float emotional_history_sum = 0.0f;  // cumulative valence of past interactions
  float imitation_target = 0.0f;       // how likely to copy their behavior
};

struct SelfModel {
  float self_esteem_estimate = 0.5f;
  float competence_estimate = 0.5f;
  float autonomy_estimate = 0.5f;
};

struct GoalStackEntry {
  std::size_t drive_type = 0;   // index into DriveState
  float urgency = 0.0f;
  std::string target_location;  // e.g. "tavern", "farm"
  uint32_t deadline_tick = 0;
};

struct CognitiveState {
  // Identification
  std::string agent_id;                    // e.g. "npc:demo", "player"
  uint32_t created_tick = 0;
  uint32_t last_tick = 0;

  // Emotional / Drive
  EmotionalTag current_emotion;
  DriveState drives;
  float mean_valence = 0.0f;               // recent valence average (for LLM summarization)
  float mean_arousal = 0.0f;

  // Attention (gating)
  std::array<float, 4> attention_weights{{
      1.0f,  // novelty_weight
      1.0f,  // reward_weight
      0.8f,  // social_weight
      1.0f,  // survival_weight
  }};

  // Memory (bounded)
  std::deque<WorkingMemoryItem> working_memory;  // cap at 7 items
  std::deque<EpisodicEvent> episodic_memory;      // cap at 256 events
  std::map<std::string, SemanticFact> semantic_memory;  // capped at 64 facts

  // Social
  std::map<std::string, SocialEdge> social_graph;  // capped at 32 edges

  // Self
  SelfModel self_model;

  // World-model bias (the only "online" parameter that adapts quickly)
  // 3 floats: weather_shift, social_response, resource_avail.
  std::array<float, 3> world_model_bias{{0.0f, 0.0f, 0.0f}};

  // Goal stack (ordered list of goals)
  std::vector<GoalStackEntry> goal_stack;  // capped at 5 entries

  // Action evaluator prediction cache (scaled by action_evaluator output)
  std::array<float, 6> last_action_scores{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};

  // Constants for bounded adaptation (see docs/cognitive-architecture.md §6)
  static constexpr float kDriveUpdateRate = 0.01f;
  static constexpr float kDriveDecayRate = 0.0002f;  // per tick
  static constexpr float kDriveSatisfactionFloor = 0.05f;
  static constexpr float kWorkingMemoryDecay = 0.995f;
  static constexpr float kWorkingMemoryCap = 7;
  static constexpr float kEpisodicMemoryCap = 256;
  static constexpr float kEpisodicConfidenceDecay = 0.998f;
  static constexpr float kSemanticMemoryCap = 64;
  static constexpr float kSemanticConfidenceDecay = 0.9999f;  // very slow
  static constexpr float kSocialGraphCap = 32;
  static constexpr float kTrustUpdateRate = 0.05f;
  static constexpr float kFamiliarityIncrement = 0.02f;
  static constexpr float kImitationThreshold = 0.5f;
  static constexpr float kSelfModelUpdateRate = 0.005f;
  static constexpr float kWorldModelBiasUpdateRate = 0.01f;
  static constexpr float kAttentionWeightUpdateRate = 0.001f;
  static constexpr float kGoalStackCap = 5;
};

}  // namespace ashgrove
