#include "cognitive_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace ashgrove {

CognitiveRegistry& CognitiveRegistry::instance() {
  static CognitiveRegistry r;
  return r;
}

CognitiveCore& CognitiveRegistry::get_or_create(const std::string& agent_id) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  auto it = cores_.find(agent_id);
  if (it != cores_.end()) return *it->second;
  auto core = std::make_unique<CognitiveCore>(agent_id);
  CognitiveCore* raw = core.get();
  cores_.emplace(agent_id, std::move(core));
  return *raw;
}

void CognitiveRegistry::remove(const std::string& agent_id) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  cores_.erase(agent_id);
}

bool CognitiveRegistry::save_all(const std::string& dir) const {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  bool ok = true;
  for (const auto& kv : cores_) {
    if (!kv.second->save(dir)) ok = false;
  }
  return ok;
}

std::size_t CognitiveRegistry::load_all(const std::string& dir) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  // Discover files in the dir matching <agent_id>.json.
  // Lightweight scan: list directory contents via std::filesystem (C++17).
  std::size_t loaded = 0;
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(dir, ec)) return 0;
  for (auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) continue;
    auto path = entry.path();
    if (path.extension() != ".json") continue;
    std::string agent_id = path.stem().string();
    auto& core = get_or_create(agent_id);
    if (core.load(dir)) ++loaded;
  }
  return loaded;
}

void CognitiveRegistry::tick_all(uint32_t current_tick,
                                 const std::vector<std::string>& observed_stimuli) {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  for (auto& kv : cores_) {
    // ROADMAP 1.7a: cognitive LOD -- skip ticks for lower-fidelity cores.
    kv.second->set_tick_counter(kv.second->tick_counter() + 1);
    uint32_t interval = kv.second->tick_interval();
    if (kv.second->tick_counter() % interval != 0) continue;
    kv.second->tick(current_tick, observed_stimuli);
  }
}

void CognitiveRegistry::aggregate_stats(float& out_mean_valence,
                                        float& out_mean_arousal,
                                        std::size_t& out_agent_count) const {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  if (cores_.empty()) {
    out_mean_valence = 0.0f;
    out_mean_arousal = 0.0f;
    out_agent_count = 0;
    return;
  }
  float v = 0.0f, a = 0.0f;
  for (const auto& kv : cores_) {
    const CognitiveState& s = kv.second->state();
    v += s.mean_valence;
    a += s.mean_arousal;
  }
  out_mean_valence = v / static_cast<float>(cores_.size());
  out_mean_arousal = a / static_cast<float>(cores_.size());
  out_agent_count = cores_.size();
}

float CognitiveRegistry::average_edge_trust() const {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  float trust_sum = 0.0f;
  std::size_t edge_count = 0;
  for (const auto& kv : cores_) {
    for (const auto& edge_kv : kv.second->state().social_graph) {
      trust_sum += edge_kv.second.trust;
      ++edge_count;
    }
  }
  if (edge_count == 0) return 0.0f;
  return trust_sum / static_cast<float>(edge_count);
}

std::map<std::string, float> CognitiveRegistry::collect_semantic_facts() const {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  std::map<std::string, float> consensus;
  for (const auto& kv : cores_) {
    for (const auto& fact_kv : kv.second->state().semantic_memory) {
      const SemanticFact& f = fact_kv.second;
      // Key on predicate:object so shared beliefs cluster.
      std::string key = f.predicate + ":" + f.object;
      consensus[key] += f.confidence;  // sum of confidences across agents
    }
  }
  return consensus;
}

std::size_t CognitiveRegistry::size() const {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  return cores_.size();
}

}  // namespace ashgrove
