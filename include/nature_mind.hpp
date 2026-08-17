#pragma once

#include "cognitive_state.hpp"
#include "world.hpp"

#include <map>
#include <string>
#include <vector>

namespace ashgrove {

// Disturbance types
enum class DisturbanceType {
  Fire,
  Windthrow,
  Flood,
  PlayerClearcut,
  PestOutbreak,
  Disease,
  Drought
};

// Disturbance record for history
struct DisturbanceRecord {
  uint32_t day;
  DisturbanceType type;
  Vec2 center;
  float radius;
  float intensity;
};

// NatureMind: aggregate forest ecology cognition.
// Tracks forest-wide state: succession stages, disturbance legacy, allele frequencies,
// climate velocity, carbon stocks, biodiversity indices.
// Provides feedback to procgen, weather, disaster chance, NPC foraging yields.

class NatureMind {
 public:
  // Per-chunk forest state (public for implementation access)
  struct ForestChunk {
    float succession_stage = 0.0f;        // 0..1
    float carbon_stock = 0.0f;            // Mg C / ha
    float disturbance_legacy = 0.0f;      // 0..1, decays slowly
    float soil_organic_matter = 0.0f;     // Mg C / ha
    std::map<std::string, float> species_comp;  // species -> basal area %
    std::map<std::string, float> alleles;       // locus -> freq
    uint32_t last_disturbance_day = 0;
    float disturbance_intensity = 0.0f;
  };

  explicit NatureMind(World* world);
  ~NatureMind() {}

  // Called once per in-game day (at 04:00, after TownConsciousness consolidation).
  void tick(uint32_t current_day);

  // Disturbance events (fire, windthrow, flood, player clear-cut).
  // Returns true if disturbance created lasting legacy.
  bool apply_disturbance(DisturbanceType type,
                         const Vec2& center,
                         float radius,
                         float intensity);

  // Succession: advances forest patches toward climax community.
  void advance_succession();

  // Allele frequency tracking for tree species (intraspecific evolution).
  void update_allele_frequencies();

  // Carbon cycle: sequestration, respiration, soil organic matter.
  void update_carbon_cycle();

  // Biodiversity indices (Shannon, Simpson, species richness per chunk).
  void update_biodiversity();

  // Climate velocity: shifts biome suitability over decades.
  void update_climate_velocity();

  // Feedback to world systems:
  // - procgen biases for new chunks
  // - weather storm_chance bias
  // - disaster_chance bias
  // - NPC foraging yield multipliers
  void push_adaptations();

  // Serialization
  std::string to_json() const;
  bool from_json(const std::string& json_str);

  // Inspection (for /town/inspect)
  struct Snapshot {
    uint32_t day;
    float mean_succession_stage;      // 0=pioneer .. 1=climax
    float total_carbon_stock;         // Mg C / ha
    float biodiversity_shannon;       // 0..5
    float mean_disturbance_legacy;    // 0..1
    float climate_velocity;           // km/decade
    std::map<std::string, float> allele_frequencies;  // species -> mean freq
    std::vector<float> procgen_biases;  // biome weights
    float weather_storm_bias;
    float disaster_chance_bias;
    std::map<std::string, float> foraging_yield;  // resource -> multiplier
  };
  Snapshot get_snapshot() const;

 private:
  World* world_;

  // Per-chunk forest state
  std::vector<ForestChunk> chunks_;  // indexed by chunk_id

  // Aggregate trackers
  std::map<std::string, float> global_alleles_;   // locus -> global freq
  std::map<std::string, float> species_pools_;    // species -> regional pool

  // Climate
  float climate_velocity_ = 0.0f;      // km/decade
  float temperature_anomaly_ = 0.0f;   // deg C
  float precipitation_anomaly_ = 0.0f; // mm/yr

  // Disturbance history (for legacy calculation)
  std::vector<DisturbanceRecord> disturbance_history_;

  // Helpers
  void decay_disturbance_legacy();
  float chunk_suitability(int chunk_id, const std::string& species) const;
  void migrate_alleles_between_chunks();
  void update_species_pools();
  float calculate_shannon_diversity(const ForestChunk& chunk) const;
};

}  // namespace ashgrove