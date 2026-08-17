#include "culture_mind.hpp"

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

CultureMind::CultureMind(World* world, CognitiveRegistry* registry)
    : world_(world), registry_(registry) {}

void CultureMind::tick(uint32_t current_day) {
  if (!world_ || !registry_) return;

  aggregate_culture();
  update_biases();
  push_adaptations();

  (void)current_day;
}

void CultureMind::aggregate_culture() {
  // Pull shared semantic facts across all agents.
  std::map<std::string, float> consensus = registry_->collect_semantic_facts();
  if (consensus.empty()) {
    cultural_cohesion_ = 0.0f;
    collective_fear_ = 0.0f;
    collective_joy_ = 0.0f;
    shared_beliefs_.clear();
    shared_fears_.clear();
    return;
  }

  // Total consensus strength (sum of all fact confidences).
  float total = 0.0f;
  float fear_sum = 0.0f;
  float joy_sum = 0.0f;
  std::vector<std::pair<float, std::string>> ranked;
  for (const auto& kv : consensus) {
    total += kv.second;
    if (kv.first.rfind("fears", 0) == 0) fear_sum += kv.second;
    if (kv.first.rfind("prefers", 0) == 0) joy_sum += kv.second;
    ranked.emplace_back(kv.second, kv.first);
  }

  // Cohesion = how concentrated consensus is (largest fact share of total).
  std::sort(ranked.rbegin(), ranked.rend());
  if (!ranked.empty()) {
    cultural_cohesion_ = clamp01(ranked[0].first / total);
  }

  // Collective fear/joy as share of consensus that are fear/preference facts.
  collective_fear_ = clamp01(fear_sum / total);
  collective_joy_ = clamp01(joy_sum / total);

  // Top shared beliefs & fears for the snapshot.
  shared_beliefs_.clear();
  shared_fears_.clear();
  for (const auto& r : ranked) {
    if (r.first < 0.5f) break;
    if (r.second.rfind("fears", 0) == 0) {
      shared_fears_.push_back(r.second);
    } else {
      shared_beliefs_.push_back(r.second);
    }
    if (shared_beliefs_.size() + shared_fears_.size() >= 8) break;
  }
}

void CultureMind::update_biases() {
  // Shared collective fear creates scheduling toward gatherings/safety.
  schedule_bias_ = clamp11(collective_fear_ * 0.15f - collective_joy_ * 0.05f);
  schedule_bias_ = std::max(-0.2f, std::min(0.2f, schedule_bias_));

  // Dialogue topics weighted by collective fear (they discuss what they fear).
  dialogue_topic_weight_ = std::max(0.2f, std::min(2.0f, 1.0f + collective_fear_ * 1.0f));
}

void CultureMind::record_practice(const std::string& practice, float weight) {
  practice_frequency_[practice] += weight;
  // Recompute cohesion slightly (practices reinforce shared culture).
  cultural_cohesion_ = clamp01(cultural_cohesion_ + weight * 0.01f);
}

void CultureMind::push_adaptations() {
  // Expose biases via the world so NPC schedule/dialogue can read them.
  // There is no dedicated world scalar yet; expose via snapshot + store on
  // the world's adaptation JSON for future consumers.
  if (world_) {
    world_->culture_adaptations = {
        {"schedule_bias", schedule_bias_},
        {"dialogue_topic_weight", dialogue_topic_weight_},
        {"cultural_cohesion", cultural_cohesion_},
        {"collective_fear", collective_fear_},
        {"collective_joy", collective_joy_}};
  }
}

CultureMind::Snapshot CultureMind::get_snapshot() const {
  Snapshot snap;
  snap.day = world_ ? world_->day : 0;
  snap.cultural_cohesion = cultural_cohesion_;
  snap.collective_fear = collective_fear_;
  snap.collective_joy = collective_joy_;
  snap.schedule_bias = schedule_bias_;
  snap.dialogue_topic_weight = dialogue_topic_weight_;
  snap.shared_beliefs = shared_beliefs_;
  snap.shared_fears = shared_fears_;
  snap.practice_frequency = practice_frequency_;
  return snap;
}

}  // namespace ashgrove