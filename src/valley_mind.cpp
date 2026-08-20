#include "valley_mind.hpp"

#include <algorithm>
#include <fstream>

namespace ashgrove {

namespace {

inline float clamp01(float x) {
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

}  // namespace

ValleyMind::ValleyMind(World* world) : world_(world) {}

void ValleyMind::tick(uint32_t current_day) {
  if (!world_) return;

  // Guard: run exactly once per in-game day. The call site fires every loop
  // iteration during the hour==28 consolidation window (~30 iters/sec for
  // ~33 s). The other minds are mostly idempotent recomputes, but
  // World::tick_valley() is STATEFUL (decays guilt, steps the corruption CA),
  // so without this guard it would collapse guilt ~0.02 * 1000 calls / day.
  if (current_day == last_tick_day_) return;
  last_tick_day_ = current_day;

  // 1) Drive the Valley heartbeat: decay guilt, spread corruption CA, and
  //    recompute awakening (deterministic; lives in World).
  world_->tick_valley();

  // 2) Merge awakening back into the World horror consumers. Only escalate /
  //    dampen — do not hard-override LLM consolidation. Damped merge keeps the
  //    feedback loop stable: awakening is a slow-biasing input, not a spike.
  const float awakening = world_->valley_awakening;
  const float corruption = world_->corruption_density();

  // Horror intensity receives a 40% weight from awakening so the entity's
  // stirring visibly escalates dread over days.
  horror_intensity_pushed_ = clamp01(
      0.6f * world_->horror_intensity + 0.4f * awakening);
  world_->horror_intensity = horror_intensity_pushed_;

  // The deeper the Valley wakes, the faster sanity drains in its presence.
  sanity_drain_pushed_ = std::max(0.5f, std::min(2.0f, 1.0f + 0.5f * awakening));
  world_->horror_sanity_drain_multiplier =
      std::max(0.5f, std::min(2.0f,
          0.5f * world_->horror_sanity_drain_multiplier + 0.5f * sanity_drain_pushed_));

  // Corruption manifests as fog — the genius loci's breath over the land.
  fog_pushed_ = clamp01(std::max(world_->weather_fog_intensity,
                                awakening * 0.6f + corruption * 0.4f));
  world_->weather_fog_intensity = fog_pushed_;

  // Phantoms appear more often where the Valley is awake.
  phantom_pushed_ = clamp01(awakening * 0.3f);
  world_->horror_phantom_sighting_chance =
      std::max(world_->horror_phantom_sighting_chance, phantom_pushed_);

  // 3) Bound the event log.
  if (memory_.size() > 64) memory_.erase(memory_.begin(), memory_.end() - 64);

  (void)current_day;
}

void ValleyMind::record_event(const std::string& event_type,
                              const std::string& detail,
                              float weight) {
  ValleyMemoryRecord rec;
  rec.day = world_ ? world_->day : 0;
  rec.event_type = event_type;
  rec.detail = detail;
  rec.weight = weight;
  memory_.push_back(rec);
  if (memory_.size() > 64) memory_.erase(memory_.begin(), memory_.end() - 64);
}

ValleyMind::Snapshot ValleyMind::get_snapshot() const {
  Snapshot snap{};
  snap.day = world_ ? world_->day : 0;
  snap.collective_guilt = world_ ? world_->collective_guilt : 0.0f;
  snap.valley_awakening = world_ ? world_->valley_awakening : 0.0f;
  snap.corruption_density = world_ ? world_->corruption_density() : 0.0f;
  snap.horror_intensity = world_ ? world_->horror_intensity : 0.0f;
  snap.horror_sanity_drain_multiplier = world_ ? world_->horror_sanity_drain_multiplier : 1.0f;
  snap.weather_fog_intensity = world_ ? world_->weather_fog_intensity : 0.0f;
  snap.horror_phantom_sighting_chance = world_ ? world_->horror_phantom_sighting_chance : 0.0f;
  snap.horror_cycle = world_ ? world_->horror_cycle : 0;
  snap.dread_bias_theme = 0;
  snap.dread_counters = {0, 0, 0, 0};

  // ROADMAP 1.4: surface the first player's dread profile (single-player game).
  if (world_ && !world_->players.empty()) {
    const auto& p0 = world_->players.begin()->second;
    snap.dread_bias_theme = world_->dread_bias(p0);
    snap.dread_counters = p0.dread_counters;
  }

  snap.recent_events.clear();
  std::size_t start = memory_.size() > 10 ? memory_.size() - 10 : 0;
  for (std::size_t i = start; i < memory_.size(); ++i) {
    snap.recent_events.push_back(
        "d" + std::to_string(memory_[i].day) + " " + memory_[i].event_type +
        ": " + memory_[i].detail);
  }
  return snap;
}

// Serialization for ROADMAP 2.10 (Hidden State Persistence)
std::string ValleyMind::to_json() const {
  json j;
  j["horror_intensity_pushed"] = horror_intensity_pushed_;
  j["sanity_drain_pushed"] = sanity_drain_pushed_;
  j["fog_pushed"] = fog_pushed_;
  j["phantom_pushed"] = phantom_pushed_;
  j["last_tick_day"] = last_tick_day_;
  json mem = json::array();
  for (const auto& rec : memory_) {
    mem.push_back({
        {"day", rec.day},
        {"event_type", rec.event_type},
        {"detail", rec.detail},
        {"weight", rec.weight}
    });
  }
  j["memory"] = mem;
  return j.dump();
}

bool ValleyMind::from_json(const std::string& json_str) {
  try {
    json j = json::parse(json_str);
    horror_intensity_pushed_ = j.value("horror_intensity_pushed", 0.0f);
    sanity_drain_pushed_ = j.value("sanity_drain_pushed", 1.0f);
    fog_pushed_ = j.value("fog_pushed", 0.0f);
    phantom_pushed_ = j.value("phantom_pushed", 0.0f);
    last_tick_day_ = j.value("last_tick_day", 0u);
    if (j.contains("memory") && j["memory"].is_array()) {
      memory_.clear();
      for (const auto& rec_json : j["memory"]) {
        ValleyMemoryRecord rec;
        rec.day = rec_json.value("day", 0);
        rec.event_type = rec_json.value("event_type", "");
        rec.detail = rec_json.value("detail", "");
        rec.weight = rec_json.value("weight", 0.0f);
        memory_.push_back(rec);
      }
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void ValleyMind::load(const std::string& path) {
  std::ifstream f(path);
  if (!f) return;
  std::stringstream buf;
  buf << f.rdbuf();
  from_json(buf.str());
}

void ValleyMind::save(const std::string& path) const {
  std::ofstream f(path);
  if (!f) return;
  f << to_json();
}

}  // namespace ashgrove
