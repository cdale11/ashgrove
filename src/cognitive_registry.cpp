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

std::size_t CognitiveRegistry::size() const {
  std::unique_lock<std::recursive_mutex> lock(mtx_);
  return cores_.size();
}

}  // namespace ashgrove
