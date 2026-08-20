#pragma once

#include "world.hpp"

#include <cstdint>
#include <deque>
#include <string>

namespace ashgrove {

// ValleyMind: aggregate cognition of the Valley Entity (genius loci).
//
// The Valley is a living, cursed consciousness born of collective guilt (witch
// trials, betrayal, massacre). It is NOT an NPC that "acts" — it is a system
// state that emerges from accumulated guilt (deaths, secrets, horror events)
// and a spatial corruption field, and pushes its awakening back into the World
// horror adaptation scalars:
//   - horror_intensity                -> overall horror escalation
//   - horror_sanity_drain_multiplier -> sanity drain scaling
//   - weather_fog_intensity          -> atmospheric fog
//   - horror_phantom_sighting_chance -> phantom sighting probability
//
// Deterministic (Tier 1). The corruption CA + guilt decay live in
// World::tick_valley(); ValleyMind::tick() drives that heartbeat and merges
// the resulting awakening into the game's horror consumers each day.
//
// ROADMAP 1.2 — Valley Entity mechanics.
class ValleyMind {
 public:
  explicit ValleyMind(World* world);
  ~ValleyMind() {}

  // Once per in-game day (04:00), after VillageMind/EconomyMind/CultureMind.
  void tick(uint32_t current_day);

  // Record a Valley-relevant event into the episodic log (deaths, secrets,
  // horror night events, basement descents). The guilt itself is bumped in
  // World (handle_death / find_secret / trigger_basement / roll_night_event);
  // this keeps a bounded narrative memory for diagnostics.
  void record_event(const std::string& event_type,
                    const std::string& detail,
                    float weight);

  // Inspection snapshot for the /valley + /town/valley diagnostic endpoints.
  struct Snapshot {
    uint32_t day;
    float collective_guilt;     // 0..1
    float valley_awakening;    // 0..1
    float corruption_density;   // 0..1 (downsampled spatial average)
    float horror_intensity;     // 0..1 (current World scalar)
    float horror_sanity_drain_multiplier; // 0.5..2.0
    float weather_fog_intensity;          // 0..1
    float horror_phantom_sighting_chance; // 0..0.5
    uint32_t horror_cycle;     // Higurashi-style loop count
    std::vector<std::string> recent_events; // last ~10
    // ROADMAP 1.4 — dread profile (per-player, surfaced for diagnostics).
    uint8_t dread_bias_theme;          // 0..3 dominant theme
    std::array<uint16_t, 4> dread_counters; // per-theme encounter counts
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

  // Per-day guard so the stateful World::tick_valley() runs exactly once per
  // in-game day, even though the call site fires every loop iteration during
  // the hour==28 consolidation window.
  uint32_t last_tick_day_ = 0;

  // Bounded episodic log of Valley-relevant events (last 64).
  struct ValleyMemoryRecord {
    uint32_t day;
    std::string event_type;
    std::string detail;
    float weight;
  };
  std::deque<ValleyMemoryRecord> memory_;

  // Most-recently-pushed adaptation values (for snapshot / diagnostics).
  float horror_intensity_pushed_ = 0.0f;
  float sanity_drain_pushed_ = 1.0f;
  float fog_pushed_ = 0.0f;
  float phantom_pushed_ = 0.0f;
};

}  // namespace ashgrove
