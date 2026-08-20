#include "culture_mind.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

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

// Serialization for ROADMAP 2.10 (Hidden State Persistence)
std::string CultureMind::to_json() const {
  json j;
  j["cultural_cohesion"] = cultural_cohesion_;
  j["collective_fear"] = collective_fear_;
  j["collective_joy"] = collective_joy_;
  j["schedule_bias"] = schedule_bias_;
  j["dialogue_topic_weight"] = dialogue_topic_weight_;
  j["shared_beliefs"] = shared_beliefs_;
  j["shared_fears"] = shared_fears_;
  json pf = json::object();
  for (const auto& kv : practice_frequency_) pf[kv.first] = kv.second;
  j["practice_frequency"] = pf;
  j["shared_beliefs_vec"] = shared_beliefs_;
  j["shared_fears_vec"] = shared_fears_;
  return j.dump();
}

bool CultureMind::from_json(const std::string& json_str) {
  try {
    json j = json::parse(json_str);
    cultural_cohesion_ = j.value("cultural_cohesion", 0.0f);
    collective_fear_ = j.value("collective_fear", 0.0f);
    collective_joy_ = j.value("collective_joy", 0.0f);
    schedule_bias_ = j.value("schedule_bias", 0.0f);
    dialogue_topic_weight_ = j.value("dialogue_topic_weight", 1.0f);
    if (j.contains("shared_beliefs") && j["shared_beliefs"].is_array()) {
      shared_beliefs_ = j["shared_beliefs"].get<std::vector<std::string>>();
    }
    if (j.contains("shared_fears") && j["shared_fears"].is_array()) {
      shared_fears_ = j["shared_fears"].get<std::vector<std::string>>();
    }
    if (j.contains("practice_frequency") && j["practice_frequency"].is_object()) {
      for (auto it = j["practice_frequency"].begin(); it != j["practice_frequency"].end(); ++it) {
        practice_frequency_[it.key()] = it.value();
      }
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// File-based load (ROADMAP 2.10)
void CultureMind::load(const std::string& path) {
  std::ifstream f(path);
  if (!f) return;
  std::stringstream buf;
  buf << f.rdbuf();
  from_json(buf.str());
}

// File-based save (ROADMAP 2.10)
void CultureMind::save(const std::string& path) const {
  std::ofstream f(path);
  if (!f) return;
  f << to_json();
}

}  // namespace ashgrove