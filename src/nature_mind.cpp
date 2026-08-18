#include "nature_mind.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>

namespace ashgrove {

namespace {

constexpr float kPi = 3.14159265358979323846f;

inline float clamp01(float x) {
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline float clamp11(float x) {
  return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

float gaussian_kernel(float dist, float sigma) {
  return std::exp(-0.5f * (dist * dist) / (sigma * sigma));
}

// Helper to get map value with default (since std::map doesn't have value())
template <typename K, typename V>
V map_value(const std::map<K, V>& m, const K& key, const V& def) {
  auto it = m.find(key);
  return it != m.end() ? it->second : def;
}

}  // namespace

NatureMind::NatureMind(World* world)
    : world_(world) {
  if (!world_) return;

  // Initialize chunks (128x96 / 16 = 8x6 chunks = 48 chunks)
  const int chunk_w = 16;
  const int chunk_h = 16;
  const int n_chunks_x = (MAP_W + chunk_w - 1) / chunk_w;
  const int n_chunks_y = (MAP_H + chunk_h - 1) / chunk_h;
  chunks_.resize(n_chunks_x * n_chunks_y);

  // Initialize species pools (regional species availability)
  species_pools_ = {
    {"oak", 0.30f},
    {"pine", 0.25f},
    {"birch", 0.20f},
    {"maple", 0.15f},
    {"cedar", 0.10f}
  };

  // Initialize global alleles (neutral starting frequencies)
  global_alleles_ = {
    {"drought_tolerance_A", 0.5f},
    {"cold_tolerance_B", 0.5f},
    {"pest_resistance_C", 0.3f},
    {"growth_rate_D", 0.4f},
    {"seed_dispersal_E", 0.5f}
  };

  // Initialize chunks with random but biome-appropriate state
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

  for (int cy = 0; cy < n_chunks_y; ++cy) {
    for (int cx = 0; cx < n_chunks_x; ++cx) {
      int chunk_id = cy * n_chunks_x + cx;
      ForestChunk& chunk = chunks_[static_cast<size_t>(chunk_id)];

      // Sample biome at chunk center
      int gx = cx * 16 + 8;
      int gy = cy * 16 + 8;
      Tile tile = world_->at(gx, gy).tile;

      // Initialize based on biome
      if (tile == Tile::Grass || tile == Tile::GrassVar) {
        chunk.succession_stage = dist01(rng) * 0.5f;  // 0..0.5
        chunk.carbon_stock = 50.0f + dist01(rng) * 100.0f;  // 50..150 Mg C/ha
        chunk.soil_organic_matter = 20.0f + dist01(rng) * 30.0f;

        // Random species composition
        float remaining = 1.0f;
        for (auto& sp : species_pools_) {
          float p = sp.second * (0.5f + dist01(rng) * 0.5f);
          p = std::min(p, remaining);
          chunk.species_comp[sp.first] = p;
          remaining -= p;
          if (remaining <= 0.01f) break;
        }
        // Normalize
        float sum = 0.0f;
        for (auto& kv : chunk.species_comp) sum += kv.second;
        if (sum > 0) for (auto& kv : chunk.species_comp) kv.second /= sum;
      }
    }
  }
}


void NatureMind::tick(uint32_t /*current_day*/) {
  if (!world_) return;

  // Run ecological processes
  decay_disturbance_legacy();
  advance_succession();
  update_allele_frequencies();
  update_carbon_cycle();
  update_biodiversity();
  update_climate_velocity();
  migrate_alleles_between_chunks();
  update_species_pools();

  // Push adaptations to world systems
  push_adaptations();

  // Record disturbance history (keep last 1000)
  if (disturbance_history_.size() > 1000) {
    disturbance_history_.erase(disturbance_history_.begin(),
                               disturbance_history_.end() - 1000);
  }
}

bool NatureMind::apply_disturbance(DisturbanceType type,
                                   const Vec2& center,
                                   float radius,
                                   float intensity) {
  if (!world_) return false;
  intensity = clamp01(intensity);

  const int chunk_w = 16;
  const int chunk_h = 16;
  const int n_chunks_x = (MAP_W + chunk_w - 1) / chunk_w;
  const int n_chunks_y = (MAP_H + chunk_h - 1) / chunk_h;

  int affected = 0;
  for (int cy = 0; cy < n_chunks_y; ++cy) {
    for (int cx = 0; cx < n_chunks_x; ++cx) {
      int chunk_id = cy * n_chunks_x + cx;
      int gx = cx * 16 + 8;
      int gy = cy * 16 + 8;
      float dx = static_cast<float>(gx) - center.x;
      float dy = static_cast<float>(gy) - center.y;
      float dist = std::sqrt(dx * dx + dy * dy);

      if (dist <= radius) {
        ForestChunk& chunk = chunks_[static_cast<size_t>(chunk_id)];
        float falloff = 1.0f - dist / radius;
        float effect = intensity * falloff;

        // Apply disturbance effects
        chunk.disturbance_legacy = clamp01(chunk.disturbance_legacy + effect);
        chunk.last_disturbance_day = 0;  // will be set by world day
        chunk.disturbance_intensity = std::max(chunk.disturbance_intensity, effect);

        // Succession reset (partial)
        chunk.succession_stage *= (1.0f - effect * 0.5f);

        // Carbon loss
        chunk.carbon_stock *= (1.0f - effect * 0.3f);
        chunk.soil_organic_matter *= (1.0f - effect * 0.2f);

        // Species composition shift toward pioneers
        if (type == DisturbanceType::Fire || type == DisturbanceType::Windthrow) {
          chunk.species_comp["birch"] = map_value(chunk.species_comp, std::string("birch"), 0.0f) + effect * 0.1f;
          chunk.species_comp["oak"] = map_value(chunk.species_comp, std::string("oak"), 0.0f) * (1.0f - effect * 0.3f);
        }

        affected++;
      }
    }
  }

  // Record disturbance
  DisturbanceRecord rec;
  rec.day = world_->day;
  rec.type = type;
  rec.center = center;
  rec.radius = radius;
  rec.intensity = intensity;
  disturbance_history_.push_back(rec);

  return affected > 0;
}

void NatureMind::advance_succession() {
  // Succession advances toward climax based on current stage, disturbance, climate
  const int chunk_w = 16;
  const int chunk_h = 16;
  const int n_chunks_x = (MAP_W + chunk_w - 1) / chunk_w;
  const int n_chunks_y = (MAP_H + chunk_h - 1) / chunk_h;

  for (int cy = 0; cy < n_chunks_y; ++cy) {
    for (int cx = 0; cx < n_chunks_x; ++cx) {
      int chunk_id = cy * n_chunks_x + cx;
      ForestChunk& chunk = chunks_[static_cast<size_t>(chunk_id)];

      // Base succession rate (slow: ~0.5% per day toward climax)
      float rate = 0.005f;

      // Disturbance slows succession
      rate *= (1.0f - chunk.disturbance_legacy * 0.8f);

      // Climate velocity can accelerate or decelerate
      rate *= (1.0f + climate_velocity_ * 0.01f);

      // Carbon stock influences rate (more carbon = faster)
      rate *= (1.0f + chunk.carbon_stock * 0.001f);

      // Advance
      chunk.succession_stage = clamp01(chunk.succession_stage + rate);

      // Species composition shifts with succession
      if (chunk.succession_stage > 0.3f) {
        // Mid-succession: oak, maple increase
        chunk.species_comp["oak"] = map_value(chunk.species_comp, std::string("oak"), 0.0f) + 0.001f;
        chunk.species_comp["maple"] = map_value(chunk.species_comp, std::string("maple"), 0.0f) + 0.0005f;
        chunk.species_comp["birch"] = map_value(chunk.species_comp, std::string("birch"), 0.0f) * 0.999f;
      }
      if (chunk.succession_stage > 0.7f) {
        // Late succession: cedar, climax species
        chunk.species_comp["cedar"] = map_value(chunk.species_comp, std::string("cedar"), 0.0f) + 0.0005f;
        chunk.species_comp["pine"] = map_value(chunk.species_comp, std::string("pine"), 0.0f) * 0.999f;
      }

      // Renormalize species composition
      float sum = 0.0f;
      for (auto& kv : chunk.species_comp) sum += kv.second;
      if (sum > 0) for (auto& kv : chunk.species_comp) kv.second /= sum;
    }
  }
}

void NatureMind::update_allele_frequencies() {
  // Allele frequency evolution: selection + drift + migration
  const float selection_strength = 0.01f;
  const float drift_strength = 0.001f;

  std::mt19937 rng(42 + world_->day);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

  // Per-chunk selection
  for (auto& chunk : chunks_) {
    for (auto& locus_freq : chunk.alleles) {
      const std::string& locus = locus_freq.first;
      float& freq = locus_freq.second;

      // Selection pressure based on local conditions
      float selection = 0.0f;
      if (locus == "drought_tolerance_A") {
        selection = temperature_anomaly_ * 0.02f - precipitation_anomaly_ * 0.01f;
      } else if (locus == "cold_tolerance_B") {
        selection = -temperature_anomaly_ * 0.02f;
      } else if (locus == "pest_resistance_C") {
        // Higher pest pressure with warmer temps
        selection = temperature_anomaly_ * 0.015f;
      } else if (locus == "growth_rate_D") {
        selection = (chunk.succession_stage - 0.5f) * 0.01f;
      } else if (locus == "seed_dispersal_E") {
        selection = chunk.disturbance_legacy * 0.02f;
      }

      // Apply selection
      float delta = selection_strength * freq * (1.0f - freq) * selection;
      freq = clamp01(freq + delta);

      // Genetic drift
      float drift = drift_strength * dist(rng) * std::sqrt(freq * (1.0f - freq));
      freq = clamp01(freq + drift);
    }
  }

  // Recalculate global allele frequencies (weighted by chunk carbon stock)
  global_alleles_.clear();
  float total_weight = 0.0f;
  for (const auto& chunk : chunks_) {
    float weight = chunk.carbon_stock + 1.0f;
    total_weight += weight;
    for (const auto& kv : chunk.alleles) {
      global_alleles_[kv.first] += kv.second * weight;
    }
  }
  if (total_weight > 0) {
    for (auto& kv : global_alleles_) {
      kv.second /= total_weight;
    }
  }
}

void NatureMind::update_carbon_cycle() {
  // Photosynthesis - Respiration - Decomposition = Net Ecosystem Exchange
  for (auto& chunk : chunks_) {
    // Gross Primary Production (GPP) - depends on succession, climate, species
    float gpp = 0.0f;
    for (const auto& kv : chunk.species_comp) {
      const std::string& species = kv.first;
      float prop = kv.second;
      float species_gpp = 0.0f;
      if (species == "oak") species_gpp = 12.0f;
      else if (species == "pine") species_gpp = 10.0f;
      else if (species == "birch") species_gpp = 8.0f;
      else if (species == "maple") species_gpp = 9.0f;
      else if (species == "cedar") species_gpp = 7.0f;
      gpp += species_gpp * prop;
    }

    // Climate modifier
    float temp_factor = 1.0f - std::abs(temperature_anomaly_) * 0.05f;
    float precip_factor = 1.0f + precipitation_anomaly_ * 0.001f;
    gpp *= clamp01(temp_factor * precip_factor);

    // Succession modifier (young forests grow faster)
    float succ_factor = 1.0f + (1.0f - chunk.succession_stage) * 0.3f;
    gpp *= succ_factor;

    // Autotrophic respiration (Ra) ~ 50% of GPP
    float ra = gpp * 0.5f;

    // Heterotrophic respiration (Rh) from soil decomposition
    float rh = chunk.soil_organic_matter * 0.02f * (1.0f + temperature_anomaly_ * 0.1f);

    // Net Ecosystem Production
    float nep = gpp - ra - rh;

    // Carbon stock change
    chunk.carbon_stock += nep * 0.01f;  // Mg C/ha per day
    chunk.carbon_stock = std::max(0.0f, chunk.carbon_stock);

    // Soil organic matter update
    float litter_input = gpp * 0.2f;  // 20% to litter
    float decomposition = chunk.soil_organic_matter * 0.005f * (1.0f + temperature_anomaly_ * 0.1f);
    chunk.soil_organic_matter += (litter_input - decomposition) * 0.01f;
    chunk.soil_organic_matter = std::max(0.0f, chunk.soil_organic_matter);
  }
}

void NatureMind::update_biodiversity() {
  // Shannon diversity per chunk is computed in get_snapshot();
  // a persistent global index is deferred.
}

float NatureMind::calculate_shannon_diversity(const ForestChunk& chunk) const {
  float H = 0.0f;
  for (const auto& kv : chunk.species_comp) {
    float p = kv.second;
    if (p > 0) H -= p * std::log(p);
  }
  return H;
}

void NatureMind::update_climate_velocity() {
  // Climate velocity increases over time (simulating climate change)
  // km/decade - starts at 0.5, increases by 0.01 per year
  climate_velocity_ = 0.5f + (static_cast<float>(world_->day) / 365.0f) * 0.01f;

  // Temperature anomaly trends upward
  temperature_anomaly_ = 0.5f + (static_cast<float>(world_->day) / 365.0f) * 0.3f;

  // Precipitation anomaly - more variable
  std::mt19937 rng(123 + world_->day);
  std::normal_distribution<float> dist(0.0f, 50.0f);
  precipitation_anomaly_ = dist(rng) + (static_cast<float>(world_->day) / 365.0f) * 10.0f;
}

void NatureMind::decay_disturbance_legacy() {
  // Disturbance legacy decays exponentially (half-life ~5 years = 1825 days)
  const float decay_rate = 1.0f - std::exp(-std::log(2.0f) / 1825.0f);  // ~0.00038 per day

  for (auto& chunk : chunks_) {
    chunk.disturbance_legacy = std::max(0.0f, chunk.disturbance_legacy - decay_rate);
    chunk.disturbance_intensity = std::max(0.0f, chunk.disturbance_intensity - decay_rate * 0.5f);
  }

  // Remove old disturbance records
  if (!disturbance_history_.empty()) {
    uint32_t cutoff = world_->day > 7300 ? world_->day - 7300 : 0;  // 20 years
    disturbance_history_.erase(
      std::remove_if(disturbance_history_.begin(), disturbance_history_.end(),
        [cutoff](const DisturbanceRecord& r) { return r.day < cutoff; }),
      disturbance_history_.end());
  }
}

void NatureMind::migrate_alleles_between_chunks() {
  // Allele flow between adjacent chunks (isolation by distance)
  const int chunk_w = 16;
  const int chunk_h = 16;
  const int n_chunks_x = (MAP_W + chunk_w - 1) / chunk_w;
  const int n_chunks_y = (MAP_H + chunk_h - 1) / chunk_h;
  const float migration_rate = 0.001f;  // 0.1% per day

  std::vector<ForestChunk> new_chunks = chunks_;

  for (int cy = 0; cy < n_chunks_y; ++cy) {
    for (int cx = 0; cx < n_chunks_x; ++cx) {
      int chunk_id = cy * n_chunks_x + cx;
      ForestChunk& chunk = chunks_[static_cast<size_t>(chunk_id)];

      // Check 4-connected neighbors
      const int dx[4] = {-1, 1, 0, 0};
      const int dy[4] = {0, 0, -1, 1};

      for (int dir = 0; dir < 4; ++dir) {
        int ncx = cx + dx[dir];
        int ncy = cy + dy[dir];
        if (ncx < 0 || ncx >= n_chunks_x || ncy < 0 || ncy >= n_chunks_y) continue;

        int neighbor_id = ncy * n_chunks_x + ncx;
        ForestChunk& neighbor = chunks_[static_cast<size_t>(neighbor_id)];

        // Allele flow proportional to frequency difference
        for (auto& kv : chunk.alleles) {
          const std::string& locus = kv.first;
          float freq = kv.second;
          float neighbor_freq = map_value(neighbor.alleles, locus, 0.5f);

          float diff = neighbor_freq - freq;
          float flow = migration_rate * diff;

          chunk.alleles[locus] = clamp01(freq + flow);
        }
      }
    }
  }
}

void NatureMind::update_species_pools() {
  // Regional species pools shift with climate and succession
  float total_area = 0.0f;
  std::map<std::string, float> area_weighted;

  for (const auto& chunk : chunks_) {
    float weight = chunk.carbon_stock + 1.0f;
    total_area += weight;
    for (const auto& kv : chunk.species_comp) {
      area_weighted[kv.first] += kv.second * weight;
    }
  }

  if (total_area > 0) {
    for (auto& kv : area_weighted) {
      species_pools_[kv.first] = clamp01(species_pools_[kv.first] * 0.99f + (kv.second / total_area) * 0.01f);
    }
  }

  // Renormalize
  float sum = 0.0f;
  for (auto& kv : species_pools_) sum += kv.second;
  if (sum > 0) for (auto& kv : species_pools_) kv.second /= sum;
}

void NatureMind::push_adaptations() {
  if (!world_) return;

  // Procgen biases based on species pools and climate
  // This would be read by procgen when generating new chunks
  // For now, we just calculate the biases

  // Foraging yield multipliers based on species composition
  std::map<std::string, float> foraging_yield;
  foraging_yield["berries"] = map_value(species_pools_, std::string("birch"), 0.0f) * 2.0f + 0.5f;
  foraging_yield["mushrooms"] = map_value(species_pools_, std::string("oak"), 0.0f) * 1.5f + 0.5f;
  foraging_yield["herbs"] = map_value(species_pools_, std::string("maple"), 0.0f) * 1.0f + 0.5f;
  foraging_yield["nuts"] = map_value(species_pools_, std::string("oak"), 0.0f) * 2.0f + map_value(species_pools_, std::string("cedar"), 0.0f) * 1.5f + 0.5f;

  // These would be consumed by NPC foraging system
  // For now, just available via snapshot
}

NatureMind::Snapshot NatureMind::get_snapshot() const {
  Snapshot snap;
  snap.day = world_ ? world_->day : 0;

  // Calculate means
  float total_succ = 0.0f, total_carbon = 0.0f, total_shannon = 0.0f, total_legacy = 0.0f;
  int count = 0;

  for (const auto& chunk : chunks_) {
    if (chunk.carbon_stock > 0) {
      total_succ += chunk.succession_stage;
      total_carbon += chunk.carbon_stock;
      total_shannon += calculate_shannon_diversity(chunk);
      total_legacy += chunk.disturbance_legacy;
      count++;
    }
  }

  if (count > 0) {
    snap.mean_succession_stage = total_succ / static_cast<float>(count);
    snap.total_carbon_stock = total_carbon / static_cast<float>(count);
    snap.biodiversity_shannon = total_shannon / static_cast<float>(count);
    snap.mean_disturbance_legacy = total_legacy / static_cast<float>(count);
  }

  snap.climate_velocity = climate_velocity_;
  snap.allele_frequencies = global_alleles_;

  // Procgen biases (simplified)
  snap.procgen_biases = {map_value(species_pools_, std::string("oak"), 0.0f), map_value(species_pools_, std::string("pine"), 0.0f),
                         map_value(species_pools_, std::string("birch"), 0.0f), map_value(species_pools_, std::string("maple"), 0.0f)};
  snap.weather_storm_bias = climate_velocity_ * 0.1f + temperature_anomaly_ * 0.05f;
  snap.disaster_chance_bias = climate_velocity_ * 0.15f + static_cast<float>(disturbance_history_.size()) * 0.001f;

  // Foraging yields
  snap.foraging_yield["berries"] = map_value(species_pools_, std::string("birch"), 0.0f) * 2.0f + 0.5f;
  snap.foraging_yield["mushrooms"] = map_value(species_pools_, std::string("oak"), 0.0f) * 1.5f + 0.5f;
  snap.foraging_yield["herbs"] = map_value(species_pools_, std::string("maple"), 0.0f) * 1.0f + 0.5f;
  snap.foraging_yield["nuts"] = map_value(species_pools_, std::string("oak"), 0.0f) * 2.0f + map_value(species_pools_, std::string("cedar"), 0.0f) * 1.5f + 0.5f;

  return snap;
}

std::string NatureMind::to_json() const {
  std::ostringstream o;
  o << "{";
  o << "\"day\":" << (world_ ? world_->day : 0) << ",";
  o << "\"climate_velocity\":" << climate_velocity_ << ",";
  o << "\"temperature_anomaly\":" << temperature_anomaly_ << ",";
  o << "\"precipitation_anomaly\":" << precipitation_anomaly_ << ",";

  o << "\"global_alleles\":{";
  bool first = true;
  for (const auto& kv : global_alleles_) {
    if (!first) o << ",";
    first = false;
    o << "\"" << kv.first << "\":" << kv.second;
  }
  o << "},";

  o << "\"species_pools\":{";
  first = true;
  for (const auto& kv : species_pools_) {
    if (!first) o << ",";
    first = false;
    o << "\"" << kv.first << "\":" << kv.second;
  }
  o << "},";

  o << "\"chunks\":[";
  for (size_t i = 0; i < chunks_.size(); ++i) {
    if (i) o << ",";
    const auto& c = chunks_[i];
    if (c.carbon_stock == 0) { o << "null"; continue; }
    o << "{";
    o << "\"succession\":" << c.succession_stage << ",";
    o << "\"carbon\":" << c.carbon_stock << ",";
    o << "\"legacy\":" << c.disturbance_legacy << ",";
    o << "\"soil\":" << c.soil_organic_matter << ",";
    o << "\"species\":{";
    bool fs = true;
    for (const auto& kv : c.species_comp) {
      if (!fs) o << ",";
      fs = false;
      o << "\"" << kv.first << "\":" << kv.second;
    }
    o << "}";
    o << "}";
  }
  o << "]";

  o << "}";
  return o.str();
}

bool NatureMind::from_json(const std::string& json_str) {
  // Minimal parser - in production use nlohmann::json
  (void)json_str;
  return false;
}

}  // namespace ashgrove