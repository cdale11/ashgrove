#include "economy_mind.hpp"

#include <algorithm>
#include <cmath>

namespace ashgrove {

namespace {

inline float clamp01(float x) {
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline float clamp(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// Standard deviation of a float container.
float stddev(const std::deque<float>& v, float& out_mean) {
  if (v.empty()) {
    out_mean = 0.0f;
    return 0.0f;
  }
  float sum = 0.0f;
  for (float x : v) sum += x;
  out_mean = sum / static_cast<float>(v.size());
  float sq = 0.0f;
  for (float x : v) {
    float d = x - out_mean;
    sq += d * d;
  }
  return std::sqrt(sq / static_cast<float>(v.size()));
}

}  // namespace

EconomyMind::EconomyMind(World* world) : world_(world) {}

void EconomyMind::tick(uint32_t current_day) {
  if (!world_) return;

  // 1. Snapshot current market prices into rolling history.
  capture_price_history();

  // 2. Compute per-commodity volatility (cycle detection).
  compute_volatility();

  // 3. Update inflation & elasticity from price trends.
  update_inflation_and_elasticity();

  // 4. Blend player impact into demand shifts.
  apply_player_impact();

  // 5. Trade routes decay/grow slowly.
  decay_trade_routes();

  // 6. Push biases.
  push_adaptations();

  (void)current_day;
}

void EconomyMind::capture_price_history() {
  for (const auto& mp : world_->market_prices) {
    std::string name = item_def(mp.item).name;
    float ratio = mp.base_price > 0
                      ? static_cast<float>(mp.current_price) /
                            static_cast<float>(mp.base_price)
                      : 1.0f;
    auto& hist = price_history_[name];
    hist.push_back(ratio);
    if (hist.size() > 60) hist.pop_front();
  }
}

void EconomyMind::compute_volatility() {
  for (auto& kv : price_history_) {
    float mean = 0.0f;
    float sd = stddev(kv.second, mean);
    // Normalize stddev to ~0..1 (typical price ratio swings < 0.5).
    commodity_volatility_[kv.first] = clamp01(sd * 2.0f);
  }
}

void EconomyMind::update_inflation_and_elasticity() {
  if (price_history_.empty()) return;

  // Inflation: mean of recent price-ratio means vs the neutral 1.0.
  float sum_mean = 0.0f;
  int count = 0;
  for (const auto& kv : price_history_) {
    if (kv.second.empty()) continue;
    float mean = 0.0f;
    stddev(kv.second, mean);
    sum_mean += mean;
    ++count;
  }
  float avg_ratio = count > 0 ? sum_mean / static_cast<float>(count) : 1.0f;

  // Inflation grows when average prices run above base and decline when below.
  inflation_rate_ = clamp01(inflation_rate_ + (avg_ratio - 1.0f) * 0.05f);

  // Elasticity rises when markets are unstable (players react more).
  float vol_sum = 0.0f;
  for (const auto& kv : commodity_volatility_) vol_sum += kv.second;
  float avg_vol = commodity_volatility_.empty()
                      ? 0.0f
                      : vol_sum / static_cast<float>(commodity_volatility_.size());
  market_volatility_ = clamp01(avg_vol);
  price_elasticity_ = clamp(1.0f + market_volatility_ * 0.8f, 0.5f, 2.0f);
}

void EconomyMind::apply_player_impact() {
  if (!world_) return;

  // Blend player-driven demand shifts from recent events (recorded elsewhere).
  // If a commodity was flagged by the player (bumper crop -> oversupply), the
  // demand multiplier drifts toward the recorded magnitude; otherwise it
  // decays back to neutral 1.0 over time.
  for (auto& kv : demand_shift_) {
    kv.second = clamp(kv.second * 0.95f, 0.5f, 2.0f);
  }

  // Incorporate recent event magnitudes into demand shift.
  for (const auto& ev : recent_events_) {
    if (ev.first == "bumper_crop") {
      demand_shift_[ev.second] = clamp(demand_shift_[ev.second] * 0.8f, 0.5f, 2.0f);
    } else if (ev.first == "shortage") {
      demand_shift_[ev.second] = clamp(demand_shift_[ev.second] * 1.2f, 0.5f, 2.0f);
    }
  }
}

void EconomyMind::decay_trade_routes() {
  // Trade route health slowly recovers toward baseline but is knocked down by
  // market volatility (supply disruption) and inflation.
  trade_route_health_ = clamp01(trade_route_health_ * 0.995f + 0.005f * 1.0f);
  trade_route_health_ = clamp01(trade_route_health_ - market_volatility_ * 0.02f);
}

void EconomyMind::record_event(const std::string& event_type,
                               const std::string& commodity,
                               float magnitude) {
  // Store recent events (cap at 32).
  recent_events_.emplace_back(event_type, commodity);
  if (recent_events_.size() > 32) recent_events_.erase(recent_events_.begin());

  // Immediate economic reaction.
  if (event_type == "bumper_crop") {
    demand_shift_[commodity] =
        clamp(demand_shift_[commodity] * (1.0f - magnitude * 0.1f), 0.5f, 2.0f);
  } else if (event_type == "shortage" || event_type == "market_crash") {
    demand_shift_[commodity] =
        clamp(demand_shift_[commodity] * (1.0f + magnitude * 0.1f), 0.5f, 2.0f);
    market_volatility_ = clamp01(market_volatility_ + magnitude * 0.2f);
  }
}

void EconomyMind::push_adaptations() {
  if (!world_) return;

  // Blend EconomyMind's deterministic biases into the typed scalars (they may
  // also be set by TownConsciousness consolidation; we weight toward the
  // measured market behavior).
  world_->economy_price_elasticity =
      clamp(0.5f * world_->economy_price_elasticity + 0.5f * price_elasticity_, 0.5f, 2.0f);
  world_->economy_market_volatility =
      clamp01(0.5f * world_->economy_market_volatility + 0.5f * market_volatility_);

  // Write demand shift map into the world's economy_demand_shift JSON.
  json ds = json::object();
  for (const auto& kv : demand_shift_) {
    ds[kv.first] = kv.second;
  }
  if (world_->economy_demand_shift.is_object()) {
    for (auto it = world_->economy_demand_shift.begin();
         it != world_->economy_demand_shift.end(); ++it) {
      // Keep LLM-provided shifts for commodities we don't track.
      if (!ds.contains(it.key())) ds[it.key()] = it.value();
    }
  }
  world_->economy_demand_shift = ds;
}

EconomyMind::Snapshot EconomyMind::get_snapshot() const {
  Snapshot snap;
  snap.day = world_ ? world_->day : 0;
  snap.inflation_rate = inflation_rate_;
  snap.trade_route_health = trade_route_health_;
  snap.price_elasticity = price_elasticity_;
  snap.market_volatility = market_volatility_;
  snap.demand_shift = demand_shift_;
  snap.commodity_volatility = commodity_volatility_;

  float sum_ratio = 0.0f;
  int count = 0;
  for (const auto& kv : price_history_) {
    if (kv.second.empty()) continue;
    float mean = 0.0f;
    stddev(kv.second, mean);
    sum_ratio += mean;
    ++count;
  }
  snap.average_price_ratio = count > 0 ? sum_ratio / static_cast<float>(count) : 1.0f;

  // Top volatile commodities as cycle drivers.
  std::vector<std::pair<float, std::string>> ranked;
  for (const auto& kv : commodity_volatility_) {
    ranked.emplace_back(kv.second, kv.first);
  }
  std::sort(ranked.rbegin(), ranked.rend());
  snap.cycle_drivers.clear();
  for (const auto& r : ranked) {
    if (r.first < 0.05f) break;
    snap.cycle_drivers.push_back(r.second);
    if (snap.cycle_drivers.size() >= 5) break;
  }

  return snap;
}

}  // namespace ashgrove