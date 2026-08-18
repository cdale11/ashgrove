#include "village_mind.hpp"

#include <algorithm>
#include <cmath>

namespace ashgrove {

namespace {

inline float clamp01(float x) {
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline float clamp11(float x) {
  return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

}  // namespace

VillageMind::VillageMind(World* world, CognitiveRegistry* registry)
    : world_(world), registry_(registry) {}

void VillageMind::tick(uint32_t current_day) {
  if (!world_ || !registry_) return;

  // 1. Aggregate NPC emotional state from CognitiveRegistry.
  aggregate_emotional_state();

  // 2. Update collective mood from aggregate + memory.
  update_collective_mood();

  // 3. Push biases into World adaptation scalars.
  push_adaptations();

  // 4. Bound memory size.
  decay_memory();

  // Recompute snapshot fields (day etc.) lazily on read.
  (void)current_day;
}

void VillageMind::aggregate_emotional_state() {
  float mean_valence = 0.0f, mean_arousal = 0.0f;
  std::size_t count = 0;
  registry_->aggregate_stats(mean_valence, mean_arousal, count);

  // Collective fear/joy approximated from mean valence + individual emotion
  // tags. Registry has no direct iterator; use mean valence for the mood
  // baseline and derive fear/joy from valence + arousal.
  float fear_base = clamp01(0.5f - mean_valence);      // negative valence -> fear
  float joy_base = clamp01(0.5f + mean_valence);        // positive valence -> joy

  // Arousal amplifies emotional intensity (calm = low arousal).
  collective_fear_ = clamp01(fear_base * (0.5f + mean_arousal * 0.5f));
  collective_joy_ = clamp01(joy_base * (0.5f + mean_arousal * 0.5f));

  // Trust from social graph cohesion.
  float edge_trust = registry_->average_edge_trust();
  collective_trust_ = clamp01(collective_trust_ * 0.95f + edge_trust * 0.05f);

  // Anxiety grows when fear and arousal are both high.
  collective_anxiety_ = clamp01(collective_fear_ * (0.5f + mean_arousal));
}

void VillageMind::update_collective_mood() {
  if (!world_) return;

  // Schedule bias: anxious/fearful village tends to gather together earlier
  // and stay near safety; joyful village is more scattered/active.
  schedule_bias_ = clamp11(collective_anxiety_ * 0.2f - collective_joy_ * 0.1f);
  schedule_bias_ = std::max(-0.2f, std::min(0.2f, schedule_bias_));

  // Market volatility: fear/anxiety -> economic uncertainty -> price swings.
  market_volatility_ = clamp01(0.2f + collective_anxiety_ * 0.8f);

  // Horror night events: collective fear attracts the Valley's attention.
  horror_night_event_weight_ = clamp01(1.0f + collective_fear_ * 1.0f) * 1.0f;
  horror_night_event_weight_ = std::max(0.2f, std::min(2.0f, horror_night_event_weight_));

  // Horror intensity: collective fear feeds the entity, but collective joy
  // and high social trust dampen it (community resilience).
  float damping = 1.0f - (collective_joy_ * 0.4f + collective_trust_ * 0.3f);
  horror_intensity_ = clamp01(collective_fear_ * damping);

  // Optionally consume past panic events from memory to escalate.
  for (const auto& rec : memory_) {
    if (rec.event_type == "panic" || rec.event_type == "disaster") {
      horror_intensity_ = clamp01(horror_intensity_ + rec.emotional_weight * 0.05f);
    }
  }
}

void VillageMind::push_adaptations() {
  if (!world_) return;

  // Directly bias the typed scalars the game systems read.
  // Only escalate/dampen; do not hard override LLM consolidation entirely.
  world_->economy_market_volatility =
      clamp01(0.5f * world_->economy_market_volatility + 0.5f * market_volatility_);
  world_->horror_night_event_weight =
      std::max(0.2f, std::min(2.0f,
              0.6f * world_->horror_night_event_weight + 0.4f * horror_night_event_weight_));
  world_->horror_intensity =
      clamp01(0.6f * world_->horror_intensity + 0.4f * horror_intensity_);
}

void VillageMind::record_event(const std::string& event_type,
                               const std::string& detail,
                               float emotional_weight) {
  VillageMemoryRecord rec;
  rec.day = world_ ? world_->day : 0;
  rec.event_type = event_type;
  rec.detail = detail;
  rec.emotional_weight = clamp01(emotional_weight);
  memory_.push_back(rec);

  // Collective mood reacts immediately to major events.
  if (event_type == "panic" || event_type == "disaster" || event_type == "npc_departed") {
    collective_fear_ = clamp01(collective_fear_ + emotional_weight * 0.3f);
  } else if (event_type == "festival" || event_type == "gift_given") {
    collective_joy_ = clamp01(collective_joy_ + emotional_weight * 0.2f);
  }
}

void VillageMind::decay_memory() {
  if (memory_.size() > 256) {
    memory_.erase(memory_.begin(), memory_.end() - 256);
  }
}

VillageMind::Snapshot VillageMind::get_snapshot() const {
  Snapshot snap;
  snap.day = world_ ? world_->day : 0;
  snap.npc_count = 0;
  snap.mean_valence = 0.0f;
  snap.mean_arousal = 0.0f;
  snap.average_edge_trust = collective_trust_;
  snap.collective_fear = collective_fear_;
  snap.collective_joy = collective_joy_;
  snap.schedule_bias = schedule_bias_;
  snap.market_volatility = market_volatility_;
  snap.horror_night_event_weight = horror_night_event_weight_;
  snap.horror_intensity = horror_intensity_;

  if (registry_) {
    registry_->aggregate_stats(snap.mean_valence, snap.mean_arousal, snap.npc_count);
    snap.average_edge_trust = registry_->average_edge_trust();
  }

  // Last ~10 memory records.
  snap.recent_memory.clear();
  std::size_t start = memory_.size() > 10 ? memory_.size() - 10 : 0;
  for (std::size_t i = start; i < memory_.size(); ++i) {
    snap.recent_memory.push_back(memory_[i]);
  }
  return snap;
}

}  // namespace ashgrove