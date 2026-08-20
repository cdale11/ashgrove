#pragma once

#include "world.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace ashgrove {

// EconomyMind: aggregate commodity-cycle cognition.
// Tracks persistent economic state across the town:
//   - commodity_cycles   : per-commodity rolling price history (for cycles)
//   - inflation_rate     : slow drift of average prices over time
//   - trade_route_health : 0..1 health of supply network
//   - price_elasticity   : market responsiveness
//   - market_volatility  : chaos of price swings
//   - demand_shift       : per-commodity demand multiplier (player impact)
//
// This is Tier 1 (deterministic). It reads World::market_prices and the
// economy adaptation scalars, accumulates persistent memory, and biases the
// typed scalars that update_market_prices() reads. Does NOT call the LLM.
class EconomyMind {
 public:
  explicit EconomyMind(World* world);
  ~EconomyMind() {}

  // Called once per in-game day (04:00) after VillageMind.
  void tick(uint32_t current_day);

  // Record an economic event (market crash, bumper crop, player hoarding).
  void record_event(const std::string& event_type,
                    const std::string& commodity,
                    float magnitude);

  // Push biases into World economy scalars.
  void push_adaptations();

  // Inspection snapshot for /town/economy.
  struct Snapshot {
    uint32_t day;
    float inflation_rate;          // 0..1 (annualized-ish)
    float trade_route_health;      // 0..1
    float price_elasticity;        // 0.5..2.0
    float market_volatility;       // 0..1
    float average_price_ratio;     // mean(current_price/base_price)
    std::map<std::string, float> demand_shift;      // commodity -> multiplier
    std::map<std::string, float> commodity_volatility; // commodity -> 0..1
    std::vector<std::string> cycle_drivers;         // top volatile commodities
  };
  Snapshot get_snapshot() const;

  // Serialization for ROADMAP 2.10 (Hidden State Persistence)
  std::string to_json() const;
  bool from_json(const std::string& json_str);

  // Load from file (ROADMAP 2.10)
  void load(const std::string& path);

  // Save to file (ROADMAP 2.10)
  void save(const std::string& path) const;

 private:
  World* world_;

  // Rolling price history per commodity (capped at ~60 entries).
  std::map<std::string, std::deque<float>> price_history_;

  // Persistent state
  float inflation_rate_ = 0.0f;
  float trade_route_health_ = 1.0f;
  float price_elasticity_ = 1.0f;
  float market_volatility_ = 0.0f;
  std::map<std::string, float> demand_shift_;
  std::map<std::string, float> commodity_volatility_;

  // Event memory (bounded)
  std::vector<std::pair<std::string, std::string>> recent_events_; // type, commodity

  void capture_price_history();
  void compute_volatility();
  void update_inflation_and_elasticity();
  void apply_player_impact();
  void decay_trade_routes();
};

}  // namespace ashgrove