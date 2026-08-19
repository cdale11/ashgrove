#include "world.hpp"
#include "protocol.hpp"
#include "event_bus.hpp"
#include "llama_wrapper.hpp"
#include "intent_engine.hpp"
#include "command_log.hpp"
#include "town_consciousness.hpp"
#include "cognitive_core.hpp"
#include "cognitive_registry.hpp"
#include "social_cognition.hpp"
#include "nature_mind.hpp"
#include "village_mind.hpp"
#include "economy_mind.hpp"
#include "culture_mind.hpp"
#include "valley_mind.hpp"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <httplib.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <mutex>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <random>
#include <cctype>
#include <ctime>

namespace fs = std::filesystem;
using namespace std::chrono;

std::mutex g_mutex;

// ROADMAP 1.7d: cognitive dialogue generator. Set in main() to the LLM
// callback; the talk handler uses it to produce a cognitive-aware one-line
// reply, falling back to the static template on failure/slow path. Returns an
// empty string when unavailable, in which case the caller keeps the template.
static std::function<std::string(const std::string&, int, float)> g_dialogue_llm;

static uint64_t now_ms() {
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static bool is_water_any(Tile t) {
    return t == Tile::Water || t == Tile::WaterNorth || t == Tile::WaterSouth ||
           t == Tile::WaterEast || t == Tile::WaterWest;
}

static Vec2 facing_cell(Player& p);  // forward declaration

// ROADMAP 2.3 (8.1c) Plant Genetics — forward declarations (defined below).
static SeedGen default_seed_gen();
static SeedGen get_seed_gen(Player& p, Item seed_item);
static uint8_t compute_homozygosity(const std::array<int8_t, 16>& alleles);
static int8_t drift_allele(int8_t v, std::mt19937& rng, int step);
static void grow_morphology(Crop& c, int growth);

static bool save_world(const World& w, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << serialize_world(w);
    return true;
}

// Deserialize a save into an already-generated world (buildings, interiors and
// the house come from generation; saves store cells, players and the clock).
// Resets NPC schedule state so villagers re-route to their current time slot.
static bool load_world(World& w, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz <= 0) return false;
    std::string s(static_cast<size_t>(sz), '\0');
    f.seekg(0, std::ios::beg);
    f.read(&s[0], static_cast<std::streamsize>(sz));
    if (!f) return false;
    if (!deserialize_world(w, s)) return false;
    clear_paths(w);   // fix bridges/doors blocked by pre-fix saves
    for (auto& n : w.npcs) {
        n.sched_slot = -1;
        n.path.clear();
        n.path_i = 0;
        n.next_move_ms = 0;
    }
    return true;
}

static bool load_or_generate(World& w) { return load_world(w, "save.json"); }

// Phase 7.3: adaptation-aware weather name (mirrors weather_of_day_name)
static const char* weather_name_adapted(const World& w, uint32_t day) {
    int wd = w.weather_of_day_adapted(day);
    if (wd == 3) return "Severe Storm";
    if (wd == 2) return "Foggy";
    return wd ? "Rainy" : "Sunny";
}

// ===== ROADMAP 2.4 (8.1d) — Pest / Disease =====
// Pest kinds: 0=aphid, 1=caterpillar, 2=locust. Predators: 128=ladybug (eats aphids),
// 129=lacewing (eats caterpillars + locusts).
// Allele indices in Crop::alleles (see world.hpp): 10=disease_res, 11=pest_res.
static constexpr size_t kAlleleDiseaseRes = 10;
static constexpr size_t kAllelePestRes = 11;

static const char* pest_kind_name(uint8_t kind) {
    switch (kind) {
        case 0: return "aphids";
        case 1: return "caterpillars";
        case 2: return "locusts";
        case 128: return "ladybugs";
        case 129: return "lacewings";
        default: return "pests";
    }
}

// 0..1 susceptibility of a crop variety to each pest kind (transmission-graph edge weight).
static float pest_susceptibility(Item crop, uint8_t kind) {
    switch (kind) {
        case 0:  // aphids: leafy / trellis
            switch (crop) {
                case Item::Tomato: case Item::GreenBean: case Item::Hops:
                case Item::Strawberry: case Item::Blueberry:
                case Item::BokChoy: case Item::Kale: case Item::RedCabbage: return 0.9f;
                case Item::Cauliflower: return 0.7f;
                case Item::Parsnip: case Item::Potato: case Item::Corn: return 0.5f;
                default: return 0.2f;
            }
        case 1:  // caterpillars: brassicas / leafy greens
            switch (crop) {
                case Item::Cauliflower: case Item::RedCabbage:
                case Item::BokChoy: case Item::Kale: return 0.95f;
                case Item::Tomato: case Item::Strawberry: return 0.4f;
                default: return 0.15f;
            }
        case 2:  // locusts: grains / vines
            switch (crop) {
                case Item::Wheat: case Item::Corn: return 0.9f;
                case Item::Pumpkin: case Item::Melon: return 0.7f;
                default: return 0.1f;
            }
        default: return 0.0f;
    }
}

// 0..1 genetic resistance from the pest_res / disease_res allele (positive = resistant).
static float genetic_resistance(const Crop& c, size_t allele_idx) {
    int8_t v = c.alleles[allele_idx];
    if (v <= 0) return 0.0f;
    return std::min(1.0f, static_cast<float>(v) / 128.0f);
}

// Multiplier on pest spawn/spore infection for a crop cell, from companion planting
// and scarecrow protection (the transmission graph's edge weight modifiers).
static float companion_pest_mod(const World& w, int x, int y) {
    float mod = 1.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (!w.in_bounds(nx, ny)) continue;
            const Cell& n = w.at(nx, ny);
            if (n.crop.is_crop() && n.crop.crop == Item::Garlic) mod *= 0.5f;      // garlic repels pests/spores
            if (n.crop.is_crop() && n.crop.crop == Item::Hops) mod *= 1.5f;        // hops are an aphid magnet
            if (n.crop.is_crop() && n.crop.crop == Item::GreenBean) mod *= 1.2f;   // beans draw some pests
            if (n.obj.type == ObjType::Flower) mod *= 0.75f;                       // flowers shelter predators
        }
    // Scarecrow: the existing 17x17 crow protection deters all pests.
    bool covered = false;
    for (int dy = -8; dy <= 8 && !covered; ++dy)
        for (int dx = -8; dx <= 8 && !covered; ++dx) {
            int sx = x + dx, sy = y + dy;
            if (!w.in_bounds(sx, sy)) continue;
            if (w.at(sx, sy).obj.type == ObjType::Scarecrow) covered = true;
        }
    if (covered) mod *= 0.3f;
    return mod;
}

// Daily pest/disease step: spore CA (fungus), pest-agent feeding + reproduction along
// the crop-adjacency transmission graph, and predator hunting. Fully deterministic per day.
static void tick_pest_disease(World& w) {
    std::mt19937 rng(w.day * 104729u + 1234u);
    std::uniform_int_distribution<int> pct(0, 99);
    const int season = season_index(w.day);
    const float season_mod = season == 0 ? 1.0f : season == 1 ? 1.3f : season == 2 ? 0.8f : 0.2f;
    const int dx4[4] = {1, -1, 0, 0};
    const int dy4[4] = {0, 0, 1, -1};

    // ---- 1. Disease spore cellular automaton (simultaneous update) ----
    std::vector<int> delta(w.cells.size(), 0);
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (!c.crop.is_crop()) continue;
            size_t idx = static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x);
            // Dry sunny days help recovery; rain slows it.
            if (c.crop.disease_level > 0) delta[idx] -= w.rain_here(x, y) > 5 ? 4 : 12;
            // New infection: weather + wounded tissue, reduced by companions/resistance.
            float inf = 1.5f + (w.rain_here(x, y) > 5 ? 5.0f : 0.0f) +
                        (static_cast<float>(c.crop.pest_level) / 255.0f) * 4.0f;
            inf *= companion_pest_mod(w, x, y);
            inf *= (1.0f - genetic_resistance(c.crop, kAlleleDiseaseRes) * 0.7f);
            inf *= season_mod * w.pest_bias;
            if (c.crop.disease_level == 0 && pct(rng) < static_cast<int>(inf))
                delta[idx] += 30;
        }
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (c.crop.disease_level == 0) continue;
            int amount = static_cast<int>(c.crop.disease_level) * (w.rain_here(x, y) > 5 ? 40 : 22) / 255;
            if (amount <= 0) continue;
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx4[d], ny = y + dy4[d];
                if (!w.in_bounds(nx, ny)) continue;
                Cell& n = w.at(nx, ny);
                if (!n.crop.is_crop()) continue;
                delta[static_cast<size_t>(ny) * MAP_W + static_cast<size_t>(nx)] += amount;
            }
        }
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (!c.crop.is_crop()) continue;
            size_t idx = static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x);
            int nl = static_cast<int>(c.crop.disease_level) + delta[idx];
            c.crop.disease_level = static_cast<uint8_t>(std::clamp(nl, 0, 255));
            if (c.crop.disease_level > 230 && pct(rng) < 2) c.crop = Crop{};  // severe blight kills the crop
        }

    // ---- 2. Pest agents: feed, reproduce along the transmission graph, age, die ----
    std::vector<PestAgent> born;
    for (auto& pe : w.pests) {
        if (!w.in_bounds(pe.x, pe.y)) continue;
        Cell& c = w.at(pe.x, pe.y);
        if (c.crop.is_crop() && !c.crop.is_fruit_tree) {
            float sus = pest_susceptibility(c.crop.crop, pe.kind);
            int add = static_cast<int>(6.0f * sus * (1.0f - genetic_resistance(c.crop, kAllelePestRes) * 0.7f));
            c.crop.pest_level = static_cast<uint8_t>(
                std::min(255, static_cast<int>(c.crop.pest_level) + std::max(1, add)));
            if (c.crop.biomass > 0.0f) c.crop.biomass = std::max(0.0f, c.crop.biomass - 0.5f);
            // Reproduction: hop to a random adjacent crop (4-neighbour graph edge).
            if (pct(rng) < 30 && static_cast<int>(w.pests.size() + born.size()) < 50) {
                int order[4] = {0, 1, 2, 3};
                for (int i = 3; i > 0; --i) { int j = static_cast<int>(rng() % static_cast<unsigned>(i + 1)); std::swap(order[i], order[j]); }
                for (int k = 0; k < 4; ++k) {
                    int nx = pe.x + dx4[order[k]], ny = pe.y + dy4[order[k]];
                    if (!w.in_bounds(nx, ny)) continue;
                    Cell& n = w.at(nx, ny);
                    if (!n.crop.is_crop() || n.crop.is_fruit_tree || n.crop.pest_level >= 255) continue;
                    born.push_back({pe.kind, static_cast<int16_t>(nx), static_cast<int16_t>(ny), 10, 0});
                    break;
                }
            }
        }
        pe.age++;
    }
    for (auto& b : born) w.pests.push_back(b);
    w.pests.erase(std::remove_if(w.pests.begin(), w.pests.end(),
                                 [](const PestAgent& a) { return a.age >= 10; }),
                  w.pests.end());

    // New infestations arrive from beyond the farm (seeded).
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (!c.crop.is_crop() || c.crop.is_fruit_tree) continue;
            for (uint8_t kind = 0; kind < 3; ++kind) {
                float sus = pest_susceptibility(c.crop.crop, kind);
                if (sus <= 0.0f) continue;
                float chance = 2.0f * sus * season_mod * w.pest_bias *
                               companion_pest_mod(w, x, y) *
                               (1.0f - genetic_resistance(c.crop, kAllelePestRes) * 0.7f);
                if (c.crop.pest_level > 60) chance *= 1.5f;
                if (static_cast<int>(w.pests.size() + w.predators.size()) < 60 &&
                    pct(rng) < static_cast<int>(chance * 100.0f)) {
                    w.pests.push_back({kind, static_cast<int16_t>(x), static_cast<int16_t>(y), 10, 0});
                }
            }
        }

    // ---- 3. Predators: hunt adjacent prey, age, die; immigration when pests are plenty ----
    for (auto& pd : w.predators) {
        bool ate = false;
        for (int dy = -1; dy <= 1 && !ate; ++dy)
            for (int dx = -1; dx <= 1 && !ate; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = pd.x + dx, ny = pd.y + dy;
                if (!w.in_bounds(nx, ny)) continue;
                for (auto& pe : w.pests) {
                    if (pe.hp == 0 || pe.x != nx || pe.y != ny) continue;
                    bool prey = (pd.kind == 128 && pe.kind == 0) ||
                                (pd.kind == 129 && (pe.kind == 1 || pe.kind == 2));
                    if (!prey) continue;
                    if (pct(rng) < 40) { pe.hp = 0; ate = true; break; }
                }
            }
        if (!ate) {
            // Drift one tile toward the nearest pest (radius 14) so released
            // predators roam to infestations instead of staying put.
            int best = 9999, bx = 0, by = 0;
            bool found = false;
            for (auto& pe : w.pests) {
                if (pe.hp == 0) continue;
                int dd = std::abs(static_cast<int>(pe.x) - pd.x) +
                         std::abs(static_cast<int>(pe.y) - pd.y);
                if (dd < best && dd <= 14) { best = dd; bx = pe.x; by = pe.y; found = true; }
            }
            if (found) {
                int nx = pd.x, ny = pd.y;
                if (bx > pd.x) nx++; else if (bx < pd.x) nx--;
                if (by > pd.y) ny++; else if (by < pd.y) ny--;
                if (std::abs(static_cast<int>(bx) - pd.x) > std::abs(static_cast<int>(by) - pd.y))
                    ny = pd.y;
                else if (std::abs(static_cast<int>(by) - pd.y) > std::abs(static_cast<int>(bx) - pd.x))
                    nx = pd.x;
                if (w.in_bounds(nx, ny)) { pd.x = static_cast<int16_t>(nx); pd.y = static_cast<int16_t>(ny); }
            }
        }
        pd.age++;
    }
    w.pests.erase(std::remove_if(w.pests.begin(), w.pests.end(),
                                 [](const PestAgent& a) { return a.hp == 0; }),
                  w.pests.end());
    w.predators.erase(std::remove_if(w.predators.begin(), w.predators.end(),
                                     [](const PestAgent& a) { return a.age >= 12; }),
                      w.predators.end());
    if (static_cast<int>(w.pests.size()) >= 6 && static_cast<int>(w.predators.size()) < 12 &&
        pct(rng) < 8) {
        for (int y = 0; y < MAP_H; ++y)
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                if (c.crop.is_crop() && c.crop.pest_level > 20) {
                    w.predators.push_back({128, static_cast<int16_t>(x), static_cast<int16_t>(y), 10, 0});
                    y = MAP_H; x = MAP_W;  // spawn at most one
                }
            }
    }
}

// ===== ROADMAP 2.5 (8.1e) — Forest Ecology & Tree Evolution =====
// Per-tree individual physiology (carbon/water, L-System morphology), a light
// environment driven by canopy shadow, wind-dispersed seed agents + soil seed
// banks, succession & community assembly, intraspecific evolution (breeder's
// equation on the 2.3 allele architecture), mycorrhizal networks, disturbance
// legacy & old-growth, and player feedback. Fully deterministic per day.

// Ecological traits per tree species. species index == array index; cells store
// this index in seed_bank_species and SeedAgent::species.
struct TreeSpecies {
    ObjType type;
    const char* name;
    float max_height;      // m
    float max_biomass;     // kg dry biomass
    bool pioneer;          // colonizes canopy gaps; shade-intolerant
    float shade_tol;       // 0..1 (1 = thrives in the understory)
    float drought_tol;     // 0..1 (1 = thrives in dry soil)
    float seed_yield;      // seeds/day when mature & healthy (wild trees)
    int maturity_days;     // age to reach sexual maturity
    int old_growth_days;   // age threshold for old-growth status
    uint8_t wood_grade;    // 0=soft, 1=hard, 2=premium (log value scaling)
};
static const TreeSpecies kTreeSpecies[] = {
    {ObjType::Tree,        "tree",      18.0f, 300.0f,  false, 0.40f, 0.35f, 2.0f,  100, 300, 1},
    {ObjType::Pine,        "pine",      22.0f, 200.0f,  true,  0.20f, 0.55f, 3.0f,   60, 250, 0},
    {ObjType::Oak,         "oak",       25.0f, 800.0f,  false, 0.50f, 0.40f, 2.0f,  120, 400, 2},
    {ObjType::Maple,       "maple",     22.0f, 600.0f,  false, 0.60f, 0.30f, 2.0f,  100, 350, 2},
    {ObjType::Birch,       "birch",     18.0f, 250.0f,  true,  0.15f, 0.40f, 4.0f,   50, 200, 0},
    {ObjType::Cedar,       "cedar",     24.0f, 400.0f,  true,  0.30f, 0.60f, 3.0f,   70, 300, 1},
    {ObjType::Redwood,     "redwood",   40.0f, 2000.0f, false, 0.30f, 0.20f, 1.0f,  200, 800, 2},
    {ObjType::Teak,        "teak",      30.0f, 700.0f,  false, 0.40f, 0.50f, 1.0f,  150, 500, 2},
    {ObjType::Mahogany,    "mahogany",  32.0f, 750.0f,  false, 0.50f, 0.30f, 1.0f,  150, 500, 2},
    {ObjType::RubberTree,  "rubber",    25.0f, 350.0f,  false, 0.40f, 0.35f, 2.0f,   90, 300, 1},
    {ObjType::WalnutTree,  "walnut",    20.0f, 500.0f,  false, 0.50f, 0.40f, 2.0f,  100, 350, 2},
    {ObjType::HickoryTree, "hickory",   22.0f, 550.0f,  false, 0.50f, 0.50f, 2.0f,  100, 350, 2},
    {ObjType::ChestnutTree,"chestnut",  24.0f, 600.0f,  false, 0.50f, 0.40f, 2.0f,  100, 350, 2},
    {ObjType::Deodar,      "deodar",    30.0f, 500.0f,  false, 0.35f, 0.50f, 2.0f,  120, 400, 1},
};
static constexpr size_t kNumTreeSpecies = sizeof(kTreeSpecies) / sizeof(kTreeSpecies[0]);

// Species index for a tree ObjType (-1 if not a tree).
static int tree_species_index(ObjType t) {
    for (size_t i = 0; i < kNumTreeSpecies; ++i)
        if (kTreeSpecies[i].type == t) return static_cast<int>(i);
    return -1;
}

// Allele -> trait factor (0 = reference; allele /64 mapped to ~0.5..2.0 range).
static float trait_factor(int8_t allele) {
    return std::max(0.2f, (static_cast<float>(allele) + 64.0f) / 64.0f);
}

// Daily forest ecology step (all 8 roadmap sub-steps). Runs inside advance_day.
static void tick_forest_ecology(World& w) {
    const int season = season_index(w.day);
    const float seasonal_light = season == 3 ? 0.45f : season == 2 ? 0.70f : 1.0f;
    const float temp_factor = season == 3 ? 0.40f : season == 2 ? 0.75f : season == 0 ? 0.90f : 1.10f;

    // ---- (2) Light environment: seasonal irradiance shaded by neighboring canopy.
    // LAI proxy = 8-neighbor tree heights; canopy gaps brighten the floor.
    std::vector<float> light(w.cells.size(), 1.0f);
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            const Cell& c = w.at(x, y);
            if (!is_tree(c.obj.type)) continue;
            float lai = 0.0f;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (!w.in_bounds(nx, ny)) continue;
                    const Cell& n = w.at(nx, ny);
                    if (is_tree(n.obj.type))
                        lai += std::min(n.tree.height, 8.0f) / 8.0f;
                }
            float gap = (c.forest_state != 0 && forest_canopy(c.forest_state) < 3) ? 0.30f : 0.0f;
            float shade = 0.35f + 0.65f * std::exp(-0.5f * lai);
            light[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)] =
                std::min(1.0f, seasonal_light * shade + gap);
        }

    // ---- (1)/(5)/(6) Physiology, evolution, mycorrhiza on each tree cell.
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (!is_tree(c.obj.type)) continue;
            const int sidx = tree_species_index(c.obj.type);
            if (sidx < 0) continue;
            const TreeSpecies& sp = kTreeSpecies[static_cast<size_t>(sidx)];
            TreeState& t = c.tree;
            size_t idx = static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x);

            // Legacy save compatibility: pre-2.5 trees have hp but no TreeState.
            if (t.age == 0 && c.obj.hp >= 5) {
                float frac = static_cast<float>(c.obj.hp) / 255.0f;
                t.biomass = std::max(0.05f, sp.max_biomass * frac * 0.9f);
                t.age = static_cast<uint16_t>(static_cast<float>(sp.old_growth_days) * frac * 0.8f);
                t.height = std::min(sp.max_height,
                                    0.5f + sp.max_height * std::pow(t.biomass / sp.max_biomass, 1.0f / 3.0f));
                t.root_depth = std::min(3.0f, 0.3f + t.biomass * 0.004f);
                t.canopy_area = std::min(sp.max_biomass / 8.0f, 2.0f + t.biomass * 0.05f);
                t.carbon = t.biomass * 0.47f;
                t.player_managed = c.forest_state != 0 && forest_player_managed(c.forest_state);
            }

            t.age++;
            float growth_f = trait_factor(t.alleles[0]);
            float shade_f  = trait_factor(t.alleles[2]);
            float drought_f = trait_factor(t.alleles[3]);
            float root_f   = trait_factor(t.alleles[5]);
            float nutr_f   = trait_factor(t.alleles[14]);
            float cold_f   = trait_factor(t.alleles[12]);
            float heat_f   = trait_factor(t.alleles[13]);

            // Light satisfaction: pioneers need full sun; climax tolerates shade.
            float light_needed = sp.pioneer ? 0.70f : std::max(0.10f, 1.0f - sp.shade_tol * shade_f);
            float light_factor = std::min(1.0f, light[idx] / std::max(0.05f, light_needed));

            // Water balance: uptake from soil saturation (2.2) boosted by roots +
            // mycorrhiza + drought tolerance; loss from transpiration.
            float uptake = (static_cast<float>(c.saturation) / 255.0f) *
                           (0.7f + root_f * 0.4f) * (0.7f + sp.drought_tol * drought_f) *
                           (0.8f + static_cast<float>(t.mycorrhiza) / 255.0f * 0.6f);
            float transp = std::min(10.0f, t.canopy_area * 0.05f * temp_factor);
            float wnew = t.water + uptake * 45.0f - transp * 8.0f;
            // ROADMAP 2.6 (8.2): precipitation is local — trees drink their own tile.
            wnew += static_cast<float>(w.rain_here(x, y)) * 0.10f;
            t.water = std::max(0.0f, std::min(255.0f, wnew));
            float water_factor = t.water < 40.0f ? std::max(0.15f, t.water / 40.0f) : 1.0f;

            // Temperature stress (cold winters, hot summers), modulated by the
            // per-tile microclimate: valley floors are warmer, the northern
            // highlands colder.
            float temp_stress = season == 3 ? std::max(0.30f, cold_f * 0.5f)
                                : season == 1 ? std::max(0.40f, heat_f * 0.6f) : 1.0f;
            temp_stress *= (0.70f + 0.55f * (static_cast<float>(w.temp_here(x, y)) / 255.0f));

            // Nutrient factor from soil chemistry (2.1).
            float n_avail = (static_cast<float>(c.nitrogen) / 255.0f * 0.5f +
                             static_cast<float>(c.phosphorus) / 255.0f * 0.3f +
                             static_cast<float>(c.potassium) / 255.0f * 0.2f) * nutr_f;
            float nutr_factor = 0.60f + n_avail * 0.60f;

            // Carbon economy: gross primary production vs respiration. The
            // coefficients are calibrated so a healthy mature tree (B ~ 0.5-0.6
            // max) roughly breaks even and grows in gaps/edges, while dense
            // canopies (low light_factor) drive mild self-thinning. A smaller
            // gpp coefficient put the break-even point at B~13 kg, so every
            // legacy tree ran a carbon deficit, shrank, and never reproduced.
            float gpp = light_factor * water_factor * temp_stress * nutr_factor * t.canopy_area * 0.25f;
            float resp = t.biomass * 0.010f * std::max(0.4f, temp_stress);
            float npp = gpp - resp;
            t.carbon = std::max(0.0f, t.biomass * 0.47f);

            // Growth allocation (species allometry + growth allele).
            float grow = std::max(0.0f, npp) * (0.4f + growth_f * 0.4f);
            if (grow > 0.0f) {
                t.biomass = std::min(sp.max_biomass, t.biomass + grow);
                t.height = std::min(sp.max_height,
                                    0.5f + sp.max_height * std::pow(t.biomass / sp.max_biomass, 1.0f / 3.0f));
                t.canopy_area = std::min(sp.max_biomass / 8.0f, 2.0f + t.biomass * 0.05f);
                t.root_depth = std::min(3.0f, 0.3f + t.biomass * 0.004f * root_f);
            } else if (npp < -0.05f) {
                // Chronic carbon deficit → senescence (biomass shrinks toward death).
                t.biomass = std::max(0.05f, t.biomass + npp * 0.5f);
                t.height = std::max(0.2f, t.height - 0.05f);
            }

            // Map physiology onto the legacy hp proxy (drives chop/windthrow logic).
            c.obj.hp = static_cast<uint8_t>(std::min(255, std::max(1,
                static_cast<int>(std::round(t.biomass / sp.max_biomass * 255.0f)))));

            // ---- (5) Intraspecific evolution: drift + directional selection.
            {
                std::mt19937 rng_t(w.day * 7919u + static_cast<uint32_t>(idx) * 131u);
                for (auto& a : t.alleles) a = drift_allele(a, rng_t, 1);
                t.homozygosity = compute_homozygosity(t.alleles);
                // Selection differential: chronic stress pushes the relevant
                // tolerance allele up (breeder's equation response).
                if (water_factor < 0.5f && t.alleles[3] < 40) t.alleles[3]++;
                if (light_factor < 0.5f && t.alleles[2] < 40) t.alleles[2]++;
            }

            // ---- (6) Mycorrhiza: develops from soil microbiome; coevolution loop.
            {
                float target_myc = std::min(255.0f, static_cast<float>(c.microbiome) * 1.1f);
                t.mycorrhiza = static_cast<uint8_t>(std::max(0, std::min(255,
                    static_cast<int>(static_cast<float>(t.mycorrhiza) +
                                     (target_myc - static_cast<float>(t.mycorrhiza)) * 0.02f))));
                // Trees feed the soil microbiome back (fungi spread).
                if (t.mycorrhiza > 60 && c.microbiome < 255)
                    c.microbiome = static_cast<uint8_t>(std::min(255, static_cast<int>(c.microbiome) + 1));
            }

            // ---- (7) Old-growth threshold.
            if (!t.old_growth && t.age >= static_cast<uint16_t>(sp.old_growth_days) &&
                t.biomass >= sp.max_biomass * 0.6f) {
                t.old_growth = true;
                if (c.forest_state != 0) forest_set_canopy(c.forest_state, 7);
            }

            // ---- (3) Seed production from mature wild trees.
            if (t.age >= static_cast<uint16_t>(sp.maturity_days) &&
                t.biomass > sp.max_biomass * 0.3f && npp > 0.0f &&
                !t.player_managed && w.seed_agents.size() < 200) {
                float yield = sp.seed_yield * light_factor * water_factor *
                              (t.old_growth ? 1.5f : 1.0f);
                int seeds = static_cast<int>(yield) > 0 ? static_cast<int>(yield) : 0;
                float fitness = light_factor * water_factor * nutr_factor;
                for (int i = 0; i < seeds; ++i) {
                    SeedAgent sa;
                    sa.species = static_cast<uint8_t>(sidx);
                    sa.x = static_cast<int16_t>(x);
                    sa.y = static_cast<int16_t>(y);
                    sa.age = 0;
                    sa.vigor = static_cast<uint8_t>(std::max(0, std::min(255,
                        static_cast<int>(60.0f + fitness * 190.0f))));
                    sa.alleles = t.alleles;
                    // 1% per-locus mutation on the gamete.
                    std::mt19937 rng_s(w.day * 104729u + static_cast<uint32_t>(idx) * 17u +
                                       static_cast<unsigned>(i) * 101u);
                    std::uniform_int_distribution<int> locus(0, 15);
                    sa.alleles[static_cast<size_t>(locus(rng_s))] =
                        drift_allele(sa.alleles[static_cast<size_t>(locus(rng_s))], rng_s, 2);
                    w.seed_agents.push_back(sa);
                }
            }
        }

    // ---- (3b) Mycorrhizal network diffusion: stressed trees borrow from healthy
    // neighbors (second pass so all trees have current mycorrhiza values).
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (!is_tree(c.obj.type)) continue;
            TreeState& t = c.tree;
            if (t.mycorrhiza >= 200) continue;
            uint8_t best = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (!w.in_bounds(nx, ny)) continue;
                    const Cell& n = w.at(nx, ny);
                    if (is_tree(n.obj.type)) best = std::max(best, n.tree.mycorrhiza);
                }
            if (best > t.mycorrhiza + 4) {
                t.mycorrhiza = static_cast<uint8_t>(std::min(255,
                    static_cast<int>(t.mycorrhiza) + ((static_cast<int>(best) - static_cast<int>(t.mycorrhiza)) * 2) / 10 + 1));
            }
        }

    // ---- (3b) Seed dispersal (wind drift) → settle into the soil seed bank.
    {
        std::vector<SeedAgent> survivors;
        for (const SeedAgent& sa : w.seed_agents) {
            SeedAgent a = sa;
            a.age++;
            std::mt19937 rng2(w.day * 31u + static_cast<unsigned>(a.x) * 7u +
                              static_cast<unsigned>(a.y) * 13u + static_cast<unsigned>(a.age));
            std::uniform_int_distribution<int> dir(0, 7);
            int d = dir(rng2);
            // ROADMAP 2.6 (8.2): the wind biases where seeds drift; storms and
            // strong gusts carry them further downwind.
            int wu = 0, wv = 0;
            w.wind_vec_here(a.x, a.y, wu, wv);
            const int bx = (wu > 10) ? 1 : (wu < -10) ? -1 : 0;
            const int by = (wv > 10) ? 1 : (wv < -10) ? -1 : 0;
            int dx = (d % 3) - 1 + bx;
            int dy = (d / 3) - 1 + by;
            const bool stormy = (w.weather_at(a.x, a.y) == 3 || w.wind_here(a.x, a.y) > 30);
            const int dist = stormy ? 3 : 1;
            int nx = std::max(0, std::min(MAP_W - 1, static_cast<int>(a.x) + dx * dist));
            int ny = std::max(0, std::min(MAP_H - 1, static_cast<int>(a.y) + dy * dist));
            a.x = static_cast<int16_t>(nx);
            a.y = static_cast<int16_t>(ny);
            // Airborne vigor decays; banked seeds accumulate age (dormancy).
            if (a.age < 3 && a.vigor > 5) a.vigor = static_cast<uint8_t>(a.vigor - 5);
            Cell& dest = w.at(nx, ny);
            if (a.age >= 3) {
                // Settle into the soil bank (cap per cell).
                if (dest.seed_bank < 255) {
                    int add = std::max(1, std::min(4, static_cast<int>(a.vigor) / 85 + 1));
                    dest.seed_bank = static_cast<uint8_t>(std::min(255,
                        static_cast<int>(dest.seed_bank) + add));
                    dest.seed_bank_species = a.species;   // dominant species wins
                }
                continue;  // seed consumed into the bank
            }
            survivors.push_back(a);
        }
        w.seed_agents = survivors;
        w.forest_seed_agent_count = static_cast<uint16_t>(w.seed_agents.size());
    }

    // ---- (4) Seed bank germination + succession / community assembly.
    // Germination needs open soil; pioneers only in canopy gaps. Player-managed
    // (cultivated) cells never germinate wild trees.
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (c.seed_bank <= 0) continue;
            if (c.obj.type != ObjType::None || c.crop.is_crop()) continue;
            if (c.tile != Tile::Grass && c.tile != Tile::GrassVar && c.tile != Tile::Dirt) continue;
            if (c.forest_state != 0 && forest_player_managed(c.forest_state)) continue;
            const TreeSpecies& sp = kTreeSpecies[static_cast<size_t>(c.seed_bank_species)];
            bool gap = (c.forest_state == 0) ||
                       (forest_canopy(c.forest_state) < 2) ||
                       forest_recent_windthrow(c.forest_state);
            if (!gap && sp.pioneer) continue;  // pioneers need open sky
            float base = gap ? 0.05f : 0.01f;
            if (sp.pioneer) base *= gap ? 1.0f : 0.1f;
            // Per-cell deterministic roll (mix cell index in), so a fraction of
            // the bank germinates each day rather than all-or-nothing per day.
            unsigned roll = ((w.day * 2654435761u) ^ ((static_cast<unsigned>(x) * 7u +
                                                       static_cast<unsigned>(y) * 13u) << 8)) % 1000u;
            if (static_cast<float>(roll) < base * 1000.0f) {
                int type_idx = static_cast<int>(c.seed_bank_species);
                c.obj = {kTreeSpecies[static_cast<size_t>(type_idx)].type, 1, 0};
                c.tree = TreeState{};
                c.tree.age = 0;
                c.tree.height = 0.2f;
                c.tree.biomass = 0.02f;
                c.tree.homozygosity = 255;
                if (c.forest_state != 0) forest_set_canopy(c.forest_state, 1);
                int remaining = static_cast<int>(c.seed_bank) - 5;
                c.seed_bank = static_cast<uint8_t>(remaining > 0 ? remaining : 0);
            }
        }

    // ---- (7) Disturbance legacy decay: windthrow marks clear after ~2 weeks,
    // nurse logs after ~30 days.
    if (w.day % 14 == 0)
        for (int y = 0; y < MAP_H; ++y)
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                if (c.forest_state != 0 && forest_recent_windthrow(c.forest_state))
                    forest_set_windthrow(c.forest_state, false);
            }

    // ---- Aggregate report (for the `ecology` command + NatureMind sync).
    uint32_t n = 0, og = 0;
    float carbon = 0.0f, hsum = 0.0f, my = 0.0f, succ = 0.0f;
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            const Cell& c = w.at(x, y);
            if (!is_tree(c.obj.type)) continue;
            const int sidx = tree_species_index(c.obj.type);
            if (sidx < 0) continue;
            const TreeSpecies& sp = kTreeSpecies[static_cast<size_t>(sidx)];
            n++;
            carbon += c.tree.biomass * 0.47f;
            hsum += c.tree.height;
            my += static_cast<float>(c.tree.mycorrhiza);
            succ += sp.pioneer ? 0.25f : (c.tree.biomass / sp.max_biomass);
            if (c.tree.old_growth) og++;
        }
    w.forest_tree_count = n;
    w.forest_old_growth_count = og;
    w.forest_carbon_stock = carbon;
    w.forest_mean_height = n > 0 ? hsum / static_cast<float>(n) : 0.0f;
    w.forest_succession_index = n > 0 ? succ / static_cast<float>(n) : 0.0f;
    w.forest_mean_mycorrhiza = n > 0 ? static_cast<uint8_t>(my / static_cast<float>(n)) : 0;
}

// Advance to the next day: crops that were watered grow, rain waters everything,
// all farmers get their energy back. Saves immediately.
static void advance_day(World& w) {
    w.day++;
    w.day_seconds = 0;
    w.init_atmosphere();   // ROADMAP 2.6 (8.2): rebuild the synoptic grid for the new day
    int todays_weather = w.weather_of_day_adapted(w.day);
    bool rain = (todays_weather == 1 || todays_weather == 3);
    bool severe_storm = (todays_weather == 3);
    for (auto& cell : w.cells) {
        if (cell.crop.is_crop()) {
            if (cell.crop.watered && cell.crop.days_left > 0) {
                int growth = 1;

                // ROADMAP 2.1 (8.1a) — Soil chemistry effects on crop growth
                // pH effect: optimal range 6.0-7.0 (ph 60-70). Outside this range reduces growth.
                float ph_factor = 1.0f;
                if (cell.ph < 50) ph_factor = 0.5f;        // too acidic (<5.0)
                else if (cell.ph < 60) ph_factor = 0.75f;  // acidic (5.0-6.0)
                else if (cell.ph > 80) ph_factor = 0.6f;   // too alkaline (>8.0)
                else if (cell.ph > 70) ph_factor = 0.85f;  // alkaline (7.0-8.0)
                // else ph 60-70 = optimal (1.0)

                // Nutrient availability based on soil levels (0-255)
                float n_avail = cell.nitrogen / 255.0f;
                float p_avail = cell.phosphorus / 255.0f;
                float k_avail = cell.potassium / 255.0f;

                // Crop-specific nutrient demands (simplified)
                float n_demand = 0.5f, p_demand = 0.3f, k_demand = 0.3f;
                if (cell.crop.crop == Item::Corn || cell.crop.crop == Item::Wheat) {
                    n_demand = 0.8f; p_demand = 0.4f; k_demand = 0.5f; // heavy N feeders
                } else if (cell.crop.crop == Item::Tomato || cell.crop.crop == Item::Pumpkin || cell.crop.crop == Item::Melon) {
                    n_demand = 0.6f; p_demand = 0.6f; k_demand = 0.8f; // heavy P/K for fruiting
                } else if (cell.crop.crop == Item::Potato) {
                    n_demand = 0.4f; p_demand = 0.5f; k_demand = 0.7f; // root crops need P/K
                } else if (cell.crop.crop == Item::GreenBean || cell.crop.crop == Item::Hops) {
                    n_demand = 0.2f; p_demand = 0.4f; k_demand = 0.4f; // legumes fix own N
                }

                // Nutrient limitation factor (Liebig's law of the minimum)
                float n_limit = n_avail / std::max(0.1f, n_demand);
                float p_limit = p_avail / std::max(0.1f, p_demand);
                float k_limit = k_avail / std::max(0.1f, k_demand);
                float nutrient_factor = std::min({n_limit, p_limit, k_limit, 2.0f}); // cap at 2x

                // Organic matter and microbiome boost
                float om_factor = 0.8f + (cell.organic_matter / 255.0f) * 0.4f; // 0.8 to 1.2
                float micro_factor = 0.9f + (cell.microbiome / 255.0f) * 0.2f; // 0.9 to 1.1

                // ROADMAP 2.4 (8.1d) — pest/disease stress reduces growth
                float pest_factor = 1.0f - (static_cast<float>(cell.crop.pest_level) / 255.0f) * 0.8f;
                float disease_factor = 1.0f - (static_cast<float>(cell.crop.disease_level) / 255.0f) * 0.7f;

                // Combined growth factor
                float total_factor = ph_factor * nutrient_factor * om_factor * micro_factor *
                                     pest_factor * disease_factor;
                growth = static_cast<int>(std::round(static_cast<float>(growth) * total_factor));
                growth = std::max(1, std::min(growth, 5)); // clamp 1-5

                // Legacy fertilizer bonus (from obj.ore field)
                if (cell.obj.type == ObjType::None && cell.obj.ore >= 2) {
                    int bonus = (cell.obj.ore - 1);
                    growth += bonus;
                    cell.obj.ore = 0;
                }
                // Moon phase bonus: crops planted on new moon (hp=1) grow 10% faster
                if (cell.obj.type == ObjType::None && cell.obj.hp == 1) {
                    if ((static_cast<int>(w.day) + cell.crop.days_left) % 10 == 0) growth++;
                }
                cell.crop.days_left = static_cast<int8_t>(std::max(0, static_cast<int>(cell.crop.days_left) - growth));

                // ROADMAP 2.1 (8.1a) — Crop nutrient uptake: crops deplete soil nutrients as they grow
                if (growth > 0) {
                    // Uptake proportional to growth and demand
                    int n_uptake = static_cast<int>(static_cast<float>(growth) * n_demand * 2);
                    int p_uptake = static_cast<int>(static_cast<float>(growth) * p_demand * 2);
                    int k_uptake = static_cast<int>(static_cast<float>(growth) * k_demand * 2);
                    cell.nitrogen = static_cast<uint8_t>(std::max(0, static_cast<int>(cell.nitrogen) - n_uptake));
                    cell.phosphorus = static_cast<uint8_t>(std::max(0, static_cast<int>(cell.phosphorus) - p_uptake));
                    cell.potassium = static_cast<uint8_t>(std::max(0, static_cast<int>(cell.potassium) - k_uptake));
                }

                // Recompute stage based on elapsed time vs total days.
                // Stage 0 = just planted, stage 3 = ready to harvest.
                const CropDef* cd = crop_def(cell.crop.crop);
                if (cd) {
                    int total = std::max(1, static_cast<int>(cd->days));
                    int elapsed = total - cell.crop.days_left;
                    cell.crop.stage = std::min<uint8_t>(
                        static_cast<uint8_t>((elapsed * 4) / total), static_cast<uint8_t>(3));
                }

                // ROADMAP 2.3 — allele drift, L-System morphology, giant crops.
                if (growth > 0) {
                    size_t cell_idx = static_cast<size_t>(&cell - &w.cells[0]);
                    std::mt19937 rng(static_cast<unsigned>(w.day) * 7919u +
                                     static_cast<uint32_t>(cell_idx) * 131u);
                    for (auto& a : cell.crop.alleles) a = drift_allele(a, rng, 1);
                    cell.crop.homozygosity = compute_homozygosity(cell.crop.alleles);
                    grow_morphology(cell.crop, growth);
                    // Giant crops: highly homozygous + large biomass unlock a daily roll.
                    if (!cell.crop.is_giant && cell.crop.homozygosity >= 200 &&
                        cell.crop.biomass >= 30.0f) {
                        cell.crop.giant_crop_counter++;
                        std::uniform_int_distribution<int> gc(0, 39);  // 2.5% per day
                        if (gc(rng) == 0) cell.crop.is_giant = true;
                    }
                }
            }
            // ROADMAP 2.6 (8.2): overnight rain waters plots locally, not globally.
            {
                const size_t idx = static_cast<size_t>(&cell - &w.cells[0]);
                const int cx = static_cast<int>(idx % MAP_W);
                const int cy = static_cast<int>(idx / MAP_W);
                cell.crop.watered = w.rain_here(cx, cy) > 5;
            }
        }
    }
    tick_pest_disease(w);   // ROADMAP 2.4 (8.1d) — pests, spores, predators
    tick_forest_ecology(w); // ROADMAP 2.5 (8.1e) — tree physiology, seeds, succession
    w.tick_structural_physics(); // ROADMAP 2.7 (8.3) — rot/erosion, stress, fire
    // sprinklers auto-water adjacent crops overnight
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (c.obj.type != ObjType::Sprinkler) continue;
            int rad = 1;                     // steel sprinkler (tier 2)
            if (c.obj.ore == 3) rad = 2;      // iridium (tier 3) → 5x5
            for (int dy = -rad; dy <= rad; ++dy)
                for (int dx = -rad; dx <= rad; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (!w.in_bounds(nx, ny)) continue;
                    Cell& t = w.at(nx, ny);
                    if (t.crop.is_crop()) t.crop.watered = true;
                }
            c.obj.hp = 255;                  // survive (no wear yet)
        }
    for (auto& [id, p] : w.players) {
        p.energy = p.max_energy;
        p.moving = false;
        p.path.clear();
        p.gifted_today.clear();
        w.tick_sanity(p);
        // ROADMAP 1.4: apply the persistent basement mark's daily sanity malus.
        w.tick_basement_mark(p);
    }

    // Fruit trees: produce once per season after maturing (28 days)
    int season = season_index(w.day);
    for (auto& cell : w.cells) {
        if (cell.crop.is_crop() && cell.crop.is_fruit_tree && cell.crop.days_left == 0) {
            // Fruit tree is mature (days_left == 0)
            // Check if it hasn't produced this season yet
            if (cell.crop.last_harvest_season != season) {
                // Produce fruit
                cell.crop.last_harvest_season = static_cast<int8_t>(season);
                // Fruit is ready to harvest (stage 3)
                cell.crop.stage = 3;
                // ROADMAP 2.6 (8.2): fruit trees drink from their own microclimate
                const size_t fidx = static_cast<size_t>(&cell - &w.cells[0]);
                cell.crop.watered = w.rain_here(static_cast<int>(fidx % MAP_W),
                                                static_cast<int>(fidx / MAP_W)) > 5;
            }
        }
    }

    // Composters: process compost (increment day counter)
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (c.obj.type == ObjType::Composter && c.obj.hp > 0 && c.obj.hp < 4) {
                c.obj.hp++; // advance composting day
            }
        }

    // ROADMAP 2.2 (8.1b) — Water Table / Groundwater Simulation (Darcy→CA)
    // 1. Recharge: rain infiltrates, raising water table & saturation
    // 2. Lateral flow (Darcy CA): water moves from high to low water table (shallow depth)
    // 3. Discharge: wells extract water (cone of depression), baseflow to rivers
    // 4. Capillary rise: shallow water table increases root zone saturation
    // 5. Well drying: if water table drops below well screen, well goes dry
    {
        // Direction arrays for 4-neighbor connectivity
        const int dx[4] = {1, -1, 0, 0};
        const int dy[4] = {0, 0, 1, -1};

        // Temporary array for new water table depths (simultaneous update)
        std::vector<uint8_t> new_water_depth(w.cells.size());
        std::vector<uint8_t> new_saturation(w.cells.size());

        // Pass 1: Recharge from rain + lateral flow (Darcy CA)
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                // Skip water cells, bedrock, built surfaces
                if (c.tile == Tile::Water || c.tile == Tile::WaterNorth || c.tile == Tile::WaterSouth ||
                    c.tile == Tile::WaterEast || c.tile == Tile::WaterWest ||
                    c.tile == Tile::Cobble || c.tile == Tile::Bridge) {
                    new_water_depth[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)] = c.water_table_depth;
                    new_saturation[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)] = c.saturation;
                    continue;
                }

                // Recharge from rain (ROADMAP 2.6 8.2: local precipitation, not a global flag)
                float recharge = 0.0f;
                const float rain_amt = static_cast<float>(w.rain_here(x, y));
                if (rain_amt > 5.0f) {
                    float recharge_rate = c.recharge_capacity / 255.0f * 0.05f; // up to 5% per rain day
                    if (w.weather_at(x, y) == 3) recharge_rate *= 2.0f;
                    recharge = recharge_rate * (rain_amt / 255.0f) *
                               (1.0f - c.saturation / 255.0f); // less recharge if already saturated
                }

                // Lateral flow (Darcy CA): water moves from shallow to deep water table
                // Check 4 neighbors, flow from shallow (low depth) to deep (high depth)
                float lateral_flow = 0.0f;
                const int ndx[4] = {1, -1, 0, 0};
                const int ndy[4] = {0, 0, 1, -1};
                for (int dir = 0; dir < 4; ++dir) {
                    int nx = x + ndx[dir], ny = y + ndy[dir];
                    if (!w.in_bounds(nx, ny)) continue;
                    Cell& n = w.at(nx, ny);
                    if (n.tile == Tile::Water || n.tile == Tile::WaterNorth || n.tile == Tile::WaterSouth ||
                        n.tile == Tile::WaterEast || n.tile == Tile::WaterWest) continue;
                    // Darcy-like: flow proportional to head difference × transmissivity
                    int head_diff = static_cast<int>(c.water_table_depth) - static_cast<int>(n.water_table_depth);
                    if (head_diff > 0) { // neighbor is shallower (water flows TO neighbor)
                        float transmissivity = c.aquifer_transmissivity / 255.0f * 0.02f; // max 2% per tick
                        lateral_flow -= static_cast<float>(head_diff) * transmissivity; // water leaves this cell
                    } else if (head_diff < 0) { // neighbor is deeper (water flows FROM neighbor)
                        float transmissivity = n.aquifer_transmissivity / 255.0f * 0.02f;
                        lateral_flow += static_cast<float>(-head_diff) * transmissivity; // water enters this cell
                    }
                }

                // Discharge to rivers (cells adjacent to water)
                float river_discharge = 0.0f;
                for (int dir = 0; dir < 4; ++dir) {
                    int nx = x + dx[dir], ny = y + dy[dir];
                    if (!w.in_bounds(nx, ny)) continue;
                    if (w.at(nx, ny).tile == Tile::Water || w.at(nx, ny).tile == Tile::WaterNorth ||
                        w.at(nx, ny).tile == Tile::WaterSouth || w.at(nx, ny).tile == Tile::WaterEast ||
                        w.at(nx, ny).tile == Tile::WaterWest) {
                        // River boundary: water table tends toward river level (depth 0)
                        float gradient = c.water_table_depth / 255.0f;
                        river_discharge -= gradient * 0.01f; // 1% per tick toward river level
                    }
                }

                // Well extraction (cone of depression) - handled per-well below

                // Net water table change (in cm equivalent, scaled to 0-255 depth)
                float net_change = (recharge + lateral_flow + river_discharge) * 255.0f;
                int new_depth = static_cast<int>(c.water_table_depth) - static_cast<int>(net_change);
                new_depth = std::clamp(new_depth, 0, 255);

                new_water_depth[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)] = static_cast<uint8_t>(new_depth);

                // Saturation update: capillary rise from water table
                // Shallow water table = higher saturation in root zone
                float capillary_factor = 1.0f - (c.water_table_depth / 255.0f); // 1 at surface, 0 at 2.55m
                float target_saturation = std::clamp(50.0f + capillary_factor * 200.0f, 0.0f, 255.0f);
                // Rain directly increases surface saturation (8.2: local rainfall)
                if (w.rain_here(x, y) > 5) target_saturation = std::min(255.0f, target_saturation + (w.weather_at(x, y) == 3 ? 40.0f : 20.0f));
                // Evapotranspiration (simplified): crops reduce saturation
                // (handled in crop growth section via uptake)

                new_saturation[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)] = static_cast<uint8_t>(std::clamp<float>(target_saturation, 0.0f, 255.0f));
            }
        }

        // Apply water table updates
        for (size_t i = 0; i < w.cells.size(); ++i) {
            w.cells[i].water_table_depth = new_water_depth[i];
            w.cells[i].saturation = new_saturation[i];
        }

        // Pass 2: Well extraction (cone of depression) & well drying
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                if (c.obj.type == ObjType::Well) {
                    // Well extracts water: creates cone of depression
                    uint8_t& water_level = c.obj.hp; // 0-100% water level in well
                    // Extraction rate depends on well depth vs water table
                    int wt_depth = w.at(x, y).water_table_depth;
                    if (wt_depth > 200) { // water table too deep
                        water_level = 0; // well dry
                    } else if (water_level > 0) {
                        // Pumping lowers local water table (cone of depression)
                        float drawdown = 10.0f * static_cast<float>(255 - water_level) / 100.0f; // max 10 depth units
                        // Apply to neighbors (cone of depression)
                        for (int dir = 0; dir < 4; ++dir) {
                            int nx = x + dx[dir], ny = y + dy[dir];
                            if (!w.in_bounds(nx, ny)) continue;
                            float dist_factor = 1.0f / (1.0f + static_cast<float>(std::abs(dx[dir])) + static_cast<float>(std::abs(dy[dir]))); // simple distance decay
                            int dep = static_cast<int>(drawdown * dist_factor);
                            size_t idx = static_cast<size_t>(ny) * MAP_W + static_cast<size_t>(nx);
                            w.cells[idx].water_table_depth = static_cast<uint8_t>(
                                std::min(255, w.cells[idx].water_table_depth + dep));
                        }
                        water_level = std::max<uint8_t>(0, water_level - 5); // well level drops with use
                    } else if (water_level < 100) {
                        // Well recovers slowly when not pumped (rain/recharge; 8.2: local rain)
                        if (w.rain_here(x, y) > 5) water_level = std::min<uint8_t>(100, water_level + 10);
                    }
                    // Well drying check
                    if (water_level == 0) {
                        // Well is dry - could notify player via event system
                    }
                }
            }
        }

        // Capillary rise: update saturation based on water table depth
        for (auto& cell : w.cells) {
            if (cell.tile == Tile::Water || cell.tile == Tile::WaterNorth || cell.tile == Tile::WaterSouth ||
                cell.tile == Tile::WaterEast || cell.tile == Tile::WaterWest) continue;
            // Capillary fringe: saturation increases as water table rises
            float wt_factor = 1.0f - (cell.water_table_depth / 255.0f); // 1 at surface, 0 at deep
            float capillary_sat = std::clamp<float>(80.0f + wt_factor * 150.0f, 0.0f, 255.0f);
            cell.saturation = static_cast<uint8_t>(std::max<float>(static_cast<float>(cell.saturation), capillary_sat));
        }
    }

    // Wind pollination: flowers adjacent to crops boost quality chance at harvest
    // This is applied at harvest time, but we track it here for flavor
    // (Actual quality boost happens in harvest command)

    // Moon phase bonus is set at plant time (see plant command), not retroactively.
    // (Actual quality boost happens in harvest command)

    // Building weathering & maintenance (all buildings)
    bool winter = (season == 3);
    for (auto& [name, bs] : w.building_states) {
        if (rain) bs.roof_leak = std::min<uint8_t>(bs.roof_leak + 1, 100);
        if (severe_storm) bs.roof_leak = std::min<uint8_t>(bs.roof_leak + 10, 100); // severe storm: heavy rain damage
        if (winter) bs.foundation = bs.foundation > 2 ? bs.foundation - 2 : 0;
        if (severe_storm) bs.foundation = bs.foundation > 5 ? bs.foundation - 5 : 0; // wind stress
        // Condition decays based on damage
        if (bs.roof_leak > 50 || bs.foundation < 50) {
            bs.condition = bs.condition > 5 ? bs.condition - 5 : 0;
        } else if (bs.roof_leak > 20 || bs.foundation < 80) {
            bs.condition = bs.condition > 2 ? bs.condition - 2 : 0;
        } else {
            bs.condition = std::min<uint8_t>(bs.condition + 1, 100); // slow recovery if maintained
        }
    }

    // Severe storm effects (ROADMAP 2.6 8.2: storms are local — per-cell weather)
    {
        // 1. Crop damage: 10-30% flattened (recoverable), 5% destroyed
        for (auto& cell : w.cells) {
            const size_t cidx = static_cast<size_t>(&cell - &w.cells[0]);
            const int cx = static_cast<int>(cidx % MAP_W);
            const int cy = static_cast<int>(cidx / MAP_W);
            if (w.weather_at(cx, cy) == 3 && cell.crop.is_crop() && cell.crop.days_left > 0) {
                unsigned roll = ((w.day * 2654435761u) + static_cast<unsigned>(cx) * 31u + static_cast<unsigned>(cy) * 17u) % 100;
                if (roll < 5) {
                    cell.crop = {}; // destroyed
                } else if (roll < 35) {
                    // Flattened: set back one stage (recoverable)
                    if (cell.crop.stage > 0) cell.crop.stage--;
                }
            }
        }
        // 2. Tree windthrow: sophisticated per-tree mechanics (L7)
        // Wind speed scales with the local wind field (severe storms: 20-50 m/s)
        // Soil saturation from recent rain increases uprooting chance
        bool recent_rain = (w.weather_of_day_adapted(w.day - 1) == 1 || w.weather_of_day_adapted(w.day - 2) == 1);
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                if (w.weather_at(x, y) != 3) continue;
                if (is_tree(c.obj.type) && c.obj.hp > 50) {
                    // Tree properties: ROADMAP 2.5 — use the tree's own physiology
                    // (from tick_forest_ecology) when present; fall back to type
                    // defaults for legacy trees that predate per-tree state.
                    float wood_density = 0.6f; // g/cm3 default
                    float root_depth = 1.0f;   // meters default
                    float canopy_area = 10.0f; // m2 default
                    float height = 15.0f;      // meters default
                    if (c.tree.age > 0) {
                        height = c.tree.height;
                        root_depth = c.tree.root_depth;
                        canopy_area = c.tree.canopy_area;
                        wood_density = 0.45f + trait_factor(c.tree.alleles[1]) * 0.2f;
                    } else {
                        switch (c.obj.type) {
                            case ObjType::Pine: wood_density = 0.45f; root_depth = 0.8f; canopy_area = 8.0f; height = 20.0f; break;
                            case ObjType::Oak: wood_density = 0.75f; root_depth = 1.5f; canopy_area = 15.0f; height = 25.0f; break;
                            case ObjType::Maple: wood_density = 0.65f; root_depth = 1.2f; canopy_area = 12.0f; height = 22.0f; break;
                            case ObjType::Birch: wood_density = 0.60f; root_depth = 1.0f; canopy_area = 10.0f; height = 18.0f; break;
                            case ObjType::Cedar: wood_density = 0.50f; root_depth = 1.3f; canopy_area = 11.0f; height = 24.0f; break;
                            case ObjType::Redwood: wood_density = 0.40f; root_depth = 2.0f; canopy_area = 20.0f; height = 40.0f; break;
                            case ObjType::Deodar: wood_density = 0.55f; root_depth = 1.4f; canopy_area = 14.0f; height = 30.0f; break;
                            default: break; // generic tree
                        }
                    }
                    
                    // Wind force on tree (simplified) — speed from the local wind field
                    float wind_speed = 20.0f + static_cast<float>(w.wind_here(x, y)) * 0.3f; // m/s
                    float wind_force = 0.5f * 1.225f * wind_speed * wind_speed * canopy_area; // N
                    
                    // Soil resistance (increases with root depth and wood density, decreases with saturation)
                    float soil_saturation = recent_rain ? 0.8f : 0.3f;
                    float soil_resistance = root_depth * wood_density * (1.0f - soil_saturation * 0.5f) * 10000.0f; // N
                    
                    // Windthrow probability
                    float uproot_prob = wind_force / (soil_resistance + 1.0f);
                    uproot_prob = std::min(uproot_prob, 0.15f); // cap at 15%
                    
                    // Snap probability (trunk failure)
                    float snap_prob = (wind_force * height) / (wood_density * 100000.0f);
                    snap_prob = std::min(snap_prob, 0.10f); // cap at 10%
                    
                    // Lean probability (permanent bend)
                    float lean_prob = 0.05f; // base 5%
                    
                    unsigned roll = ((w.day * 2654435761u) + static_cast<unsigned>(x) * 31u + static_cast<unsigned>(y) * 17u) % 10000u;
                    float r = static_cast<float>(roll) / 10000.0f;
                    
                    if (r < uproot_prob) {
                        // Uprooted: becomes nurse log + canopy gap
                        c.obj = {ObjType::Stump, 1, 0};
                        c.tree = TreeState{};  // individual physiology is gone
                        if (c.tile == Tile::Grass || c.tile == Tile::GrassVar) c.obj.type = ObjType::LeafLitter;
                        // Create canopy gap → light pulse → undergrowth surge
                        if (c.forest_state != 0) {
                            forest_set_canopy(c.forest_state, std::max<uint8_t>(forest_canopy(c.forest_state) - 2, 0));
                            forest_set_undergrowth(c.forest_state, 3); // mushroom bloom in gap
                            forest_set_windthrow(c.forest_state, true);
                        }
                        // Spawn nurse log wildlife
                        w.spawn_wildlife_near(WildlifeType::Deer, x, y, 1); // deer attracted to gaps
                    } else if (r < uproot_prob + snap_prob) {
                        // Snapped: trunk remains as snag
                        c.obj.hp = std::max<uint8_t>(c.obj.hp / 2, 20);
                        // ROADMAP 2.5 — partial canopy loss cuts the tree's leaf area.
                        c.tree.canopy_area = std::max(1.0f, c.tree.canopy_area * 0.5f);
                        c.tree.biomass *= 0.8f;
                        // Partial canopy loss
                        if (c.forest_state != 0) {
                            forest_set_canopy(c.forest_state, std::max<uint8_t>(forest_canopy(c.forest_state) - 1, 0));
                        }
                    } else if (r < uproot_prob + snap_prob + lean_prob) {
                        // Leaned: permanent bend, reduced growth
                        c.obj.hp = std::max<uint8_t>(static_cast<uint8_t>(c.obj.hp * 0.8f), static_cast<uint8_t>(30));
                        c.tree.height *= 0.85f;  // permanent lean shortens effective height
                        // Mark as leaned (could add a flag to forest_state)
                        if (c.forest_state != 0) forest_set_windthrow(c.forest_state, true);
                    }
                }
            }
        }
        // 3. Building damage already applied above (roof_leak +10, foundation -5)
        // 4. NPCs seek shelter (handled in NPC tick via schedule disruption)
        // 5. Player sanity drain if outside (handled in tick_sanity)
    }

    // Crow overnight logic
    // unless protected by a scarecrow within 17x17 tiles.
    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (!c.crop.is_crop() || c.crop.is_fruit_tree || c.crop.stage < 3) continue; // only mature crops (fruit trees exempt)
            // Check if protected by scarecrow (17x17 = radius 8 in all directions)
            bool is_protected = false;
            for (int dy = -8; dy <= 8 && !is_protected; ++dy) {
                for (int dx = -8; dx <= 8 && !is_protected; ++dx) {
                    int sx = x + dx, sy = y + dy;
                    if (!w.in_bounds(sx, sy)) continue;
                    if (w.at(sx, sy).obj.type == ObjType::Scarecrow) is_protected = true;
                }
            }
            if (!is_protected) {
                // 5% chance of crow eating crop
                int roll = (static_cast<int>(w.day) + x * 31 + y * 17) % 100; // deterministic
                if (roll < 5) {
                    c.crop = {}; // eaten by crows
                }
            }
        }
    }

    for (auto& [id, p] : w.players) {
                if (p.inside == "Farmhouse") {
                    // We can't use say() here, so we'll add a notification to a queue
                    // For now, just log to server console
                    std::cerr << "[Farmhouse] The roof groans. A drip lands on the kitchen floor.\n";
                }
            }

    // R9.1: Autumn leaf litter - scatter near deciduous trees in Whisper Wood
    if (season == 2) { // Fall
        std::mt19937 rng(w.day * 12345 + 67890);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (int y = 16; y < 74; ++y) {
            for (int x = 4; x < 22; ++x) {
                if (!w.in_bounds(x, y)) continue;
                Cell& c = w.at(x, y);
                // Only on grass/dirt tiles near deciduous trees (Tree, not Pine)
                if (c.obj.type != ObjType::None) continue;
                if (c.tile != Tile::Grass && c.tile != Tile::GrassVar && c.tile != Tile::Dirt) continue;
                // Check adjacent for deciduous tree
                bool near_tree = false;
                for (int dy = -1; dy <= 1 && !near_tree; ++dy) {
                    for (int dx = -1; dx <= 1 && !near_tree; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx, ny = y + dy;
                        if (!w.in_bounds(nx, ny)) continue;
                        Cell& nc = w.at(nx, ny);
                        if (nc.obj.type == ObjType::Tree) near_tree = true;
                    }
                }
                if (near_tree && dist(rng) < 0.15f) {
                    c.obj = {ObjType::LeafLitter, 1};
                }
            }
        }
    }

    // R9.3: Autumn morning fog - handled in weather system, look command checks this
    // (foggy weather flag stored in world, checked by look command)

    // Snow compaction daily tick (L5)
    // In winter (season 3), snow tiles accumulate compaction from foot traffic
    // Each day: natural decay (-2), temperature-based changes
if (season == 3) { // Winter
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                if (c.tile == Tile::Snow || c.tile == Tile::Ice) {
                    // Natural decay: compaction decreases over time (melting/sublimation)
                    if (c.snow_compaction > 2) c.snow_compaction -= 2;
                    else c.snow_compaction = 0;
                    // Temperature effect: very cold days increase compaction (ice formation)
                    int temp_mod = (static_cast<int>(w.day) * 7 + x * 13 + y * 19) % 10;
                    if (temp_mod < 2) { // 20% chance of freeze
                        if (c.tile == Tile::Snow && c.snow_compaction > 200) {
                            c.tile = Tile::Ice; // packed snow becomes ice
                            c.snow_compaction = 255;
                        } else if (c.tile == Tile::Snow) {
                            c.snow_compaction = std::min<uint8_t>(c.snow_compaction + 10, 255);
                        }
                    }
                    // L9: Track aging - tracks fade over time
                    if (c.track_age > 0) {
                        c.track_age++; // age increases each day
                        if (c.track_age > 48) { // tracks fade after ~48 hours
                            c.track_age = 0;
                            c.track_type = 0;
                            c.track_dir = -1;
                        }
                    }
                }
            }
        }
        }

    // Natural regrowth: weeds, tall grass, flowers, and stump coppicing.
    // Tree growth itself is handled by tick_forest_ecology (ROADMAP 2.5); sap
    // tappers are collected manually via the `tap` command.
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            // Stump regrowth: 2% chance per day to coppice into a sapling (hp=1)
            if (c.obj.type == ObjType::Stump) {
                if ((static_cast<int>(w.day) * 11 + x * 17 + y * 23) % 100 < 2) {
                    c.obj = {ObjType::Tree, 1, 0}; // regrow as generic tree
                    c.tree = TreeState{};          // fresh sapling physiology
                    c.tree.height = 0.2f;
                    c.tree.biomass = 0.02f;
                }
            }
            // Weed regrowth: on grass/dirt tiles, small chance to spawn weed
            else if (c.obj.type == ObjType::None && 
                     (c.tile == Tile::Grass || c.tile == Tile::GrassVar || c.tile == Tile::Dirt)) {
                if ((static_cast<int>(w.day) * 13 + x * 19 + y * 29) % 1000 < 5) { // 0.5% chance per day
                    c.obj = {ObjType::Weed, 1, 0};
                }
            }
            // Tall grass regrowth
            else if (c.obj.type == ObjType::None && c.tile == Tile::Grass) {
                if ((static_cast<int>(w.day) * 17 + x * 23 + y * 31) % 1000 < 3) { // 0.3% chance
                    c.obj = {ObjType::TallGrass, 1, 0};
                }
            }
            // Flower regrowth (spring/summer)
            else if (c.obj.type == ObjType::None && c.tile == Tile::Grass) {
                int flower_season = season_index(w.day);
                if ((flower_season == 0 || flower_season == 1) && (static_cast<int>(w.day) * 19 + x * 29 + y * 37) % 1000 < 2) {
                    c.obj = {ObjType::Flower, 1, 0};
                }
            }
        }

    // L4: Seasonal forest undergrowth updates (at season change)
    // Runs once per day but only changes undergrowth_type at season boundaries
    // 'season' already declared at line 126
    // Detect season change by comparing current season with previous day's season
    int prev_day_season = season_index(w.day - 1);
    if (season != prev_day_season) {
        std::mt19937 rng(w.day * 98765 + 54321);
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                if (c.forest_state == 0) continue; // Not a forest tile
                uint8_t canopy = forest_canopy(c.forest_state);
                // Canopy seasonal change
                if (season == 0) { // Spring: deciduous leaf out
                    if (canopy < 7) canopy = std::min<uint8_t>(canopy + 1, 7);
                } else if (season == 2) { // Autumn: leaf fall
                    if (canopy > 0) canopy = std::max<uint8_t>(canopy - 1, 0);
                } else if (season == 3) { // Winter: minimal canopy for deciduous
                    if (canopy > 2) canopy = std::max<uint8_t>(canopy - 1, 2);
                }
                forest_set_canopy(c.forest_state, canopy);
                // Undergrowth seasonal cycle
                uint8_t ug = 0;
                if (season == 0) { // Spring: fern, berry
                    ug = (rng() & 1) ? 1 : 2; // fern or berry
                } else if (season == 1) { // Summer: berry (ripe)
                    ug = 2; // berry
                } else if (season == 2) { // Autumn: mushroom bloom
                    ug = 3; // mushroom
                } else { // Winter: dormant
                    ug = 0; // none
                }
                forest_set_undergrowth(c.forest_state, ug);
                // Windthrow flag decays yearly (cleared in spring)
                if (season == 0) forest_set_windthrow(c.forest_state, false);
            }
        }
    }

    // L8: Well/Pond rain recharge (8.2: local precipitation)
    {
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                if (w.rain_here(x, y) <= 5) continue;
                Cell& c = w.at(x, y);
                if (c.obj.type == ObjType::Well) {
                    // Wells recharge 15-25% per rainy day
                    int recharge = 15 + (static_cast<int>(w.day) * 13 + x * 7 + y * 11) % 11;
                    c.obj.hp = std::min<uint8_t>(static_cast<uint8_t>(100), static_cast<uint8_t>(c.obj.hp + recharge));
                } else if (c.obj.type == ObjType::Pond) {
                    // Ponds recharge faster (they're larger)
                    int recharge = 25 + (static_cast<int>(w.day) * 11 + x * 5 + y * 13) % 15;
                    c.obj.hp = std::min<uint8_t>(static_cast<uint8_t>(100), static_cast<uint8_t>(c.obj.hp + recharge));
                }
}
        }
    }

    // ROADMAP 2.1 (8.1a) — Root exudates: crops release compounds that affect soil microbiome and nutrient cycling.
    // Each crop type has characteristic exudate profile affecting microbiome diversity and nutrient availability.
    for (auto& cell : w.cells) {
        if (cell.crop.is_crop() && cell.crop.days_left > 0) {
            // Root exudates boost microbiome and slowly release nutrients
            if (cell.microbiome < 255) cell.microbiome = static_cast<uint8_t>(std::min<int>(cell.microbiome + 1, 255));
            // Legumes (peas, beans) fix nitrogen via rhizobia
            if (cell.crop.crop == Item::GreenBean || cell.crop.crop == Item::Hops) {
                if (cell.nitrogen < 255) cell.nitrogen = static_cast<uint8_t>(std::min<int>(cell.nitrogen + 2, 255));
            }
            // Heavy feeders (corn, tomato, pumpkin, melon) deplete N/P/K
            if (cell.crop.crop == Item::Corn || cell.crop.crop == Item::Tomato || 
                cell.crop.crop == Item::Pumpkin || cell.crop.crop == Item::Melon) {
                if (cell.nitrogen > 5) cell.nitrogen -= 1;
                if (cell.phosphorus > 5) cell.phosphorus -= 1;
                if (cell.potassium > 5) cell.potassium -= 1;
            }
        }
    }

    // L6: Update wildlife
    w.tick_wildlife();

    // ROADMAP 2.1 (8.1a) — Rain leaching CA: nutrients move downward with water percolation.
    // Nitrogen (mobile) leaches most; Phosphorus (immobile) leaches least; Potassium intermediate.
    // Rain intensity scales leaching; severe storms cause 2x leaching. (8.2: local rain)
    {
        for (int y = 1; y < MAP_H - 1; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                if (w.rain_here(x, y) <= 5) continue;
                float leach_factor = w.weather_at(x, y) == 3 ? 0.04f : 0.02f; // 2-4% per rain event
                // Skip water, rock, built surfaces
                if (c.tile == Tile::Water || c.tile == Tile::WaterNorth || c.tile == Tile::WaterSouth ||
                    c.tile == Tile::WaterEast || c.tile == Tile::WaterWest ||
                    c.tile == Tile::Cobble || c.tile == Tile::Bridge) continue;
                // Leach nutrients downward (to y+1)
                if (y + 1 < MAP_H) {
                    Cell& below = w.at(x, y + 1);
                    if (below.tile != Tile::Water && below.tile != Tile::WaterNorth && below.tile != Tile::WaterSouth &&
                        below.tile != Tile::WaterEast && below.tile != Tile::WaterWest) {
                        // Nitrogen leaches 3x faster than P, K leaches 1.5x faster than P
                        int n_loss = static_cast<int>(c.nitrogen * leach_factor * 3.0f);
                        int p_loss = static_cast<int>(c.phosphorus * leach_factor);
                        int k_loss = static_cast<int>(c.potassium * leach_factor * 1.5f);
                        n_loss = std::min(n_loss, static_cast<int>(c.nitrogen));
                        p_loss = std::min(p_loss, static_cast<int>(c.phosphorus));
                        k_loss = std::min(k_loss, static_cast<int>(c.potassium));
                        c.nitrogen = static_cast<uint8_t>(c.nitrogen - n_loss);
                        c.phosphorus = static_cast<uint8_t>(c.phosphorus - p_loss);
                        c.potassium = static_cast<uint8_t>(c.potassium - k_loss);
                        below.nitrogen = static_cast<uint8_t>(std::min<int>(below.nitrogen + n_loss, 255));
                        below.phosphorus = static_cast<uint8_t>(std::min<int>(below.phosphorus + p_loss, 255));
                        below.potassium = static_cast<uint8_t>(std::min<int>(below.potassium + k_loss, 255));
                    }
                }
                // Rain slightly lowers pH (acid rain effect) and builds OM slightly
                if (c.ph > 45) c.ph = static_cast<uint8_t>(std::max<int>(c.ph - 1, 45));
                if (c.organic_matter < 200) c.organic_matter = static_cast<uint8_t>(std::min<int>(c.organic_matter + 1, 200));
                // Rain boosts microbiome slightly (moisture favors microbes)
                if (c.microbiome < 255) c.microbiome = static_cast<uint8_t>(std::min<int>(c.microbiome + 2, 255));
            }
        }
    }

    // L6: Update wildlife
    w.tick_wildlife();

    save_world(w, "save.json");
}

// Nudge one NPC toward its current waypoint.
static void step_npc(World& w, NPC& n) {
    Vec2 target = n.way[n.way_idx];
    if (n.pos == target) { n.way_idx ^= 1; return; }
    int16_t dx = target.x > n.pos.x ? 1 : (target.x < n.pos.x ? -1 : 0);
    int16_t dy = target.y > n.pos.y ? 1 : (target.y < n.pos.y ? -1 : 0);
    Vec2 next = n.pos;
    if (dx != 0) next.x += dx;
    if (w.in_bounds(next) && w.walkable(next) && npc_at(w, next.x, next.y) < 0) {
        n.pos = next;
        // L5: NPC foot traffic snow compaction
        int season = season_index(w.day);
        if (season == 3) {
            Cell& stepped = w.at(next);
            if ((stepped.tile == Tile::Snow || stepped.tile == Tile::Ice) && stepped.snow_compaction < 245) {
                stepped.snow_compaction += 5; // NPCs compact less than players
                if (stepped.tile == Tile::Snow && stepped.snow_compaction > 200) {
                    stepped.tile = Tile::Ice;
                    stepped.snow_compaction = 255;
                }
            }
        }
    } else if (dy != 0) {
        next = n.pos;
        next.y += dy;
        if (w.walkable(next) && npc_at(w, next.x, next.y) < 0) {
            n.pos = next;
            // L5: NPC foot traffic snow compaction
            int season = season_index(w.day);
            if (season == 3) {
                Cell& stepped = w.at(next);
                if ((stepped.tile == Tile::Snow || stepped.tile == Tile::Ice) && stepped.snow_compaction < 245) {
                    stepped.snow_compaction += 5;
                    if (stepped.tile == Tile::Snow && stepped.snow_compaction > 200) {
                        stepped.tile = Tile::Ice;
                        stepped.snow_compaction = 255;
                    }
                }
            }
        }
    }
}

// Tool/farming interaction on the cell p.pos + (dx,dy), returns message string.
static std::string act_tool(World& w, Player& p, int tx, int ty) {
    if (!w.in_bounds(tx, ty)) return "";
    Cell& c = w.at(tx, ty);
    Item tool = p.inv[p.sel].item;
    const ItemDef& def = item_def(tool);

    auto spend = [&](uint8_t cost) -> bool {
        if (p.energy < cost) return false;
        p.energy -= cost;
        return true;
    };

    // ---- harvest ready crop (only when no tool is selected: bare-hand gesture) ----
    // Other tools must use the explicit "harvest" command so a Hoe/Axe swing on
    // a ripe tile doesn't double-pay the sell price and then clear the tile.
    if (tool == Item::None && c.crop.is_crop() && !c.crop.is_fruit_tree &&
        c.crop.stage == 3 && c.crop.days_left <= 0) {
        Item produce = c.crop.crop;
        c.crop = Crop{};
        add_item(p, produce, 1);
        p.money += item_def(produce).sell;
        return std::string("Harvested ") + item_def(produce).name + " +" +
               std::to_string(item_def(produce).sell) + "g";
    }

    switch (tool) {
    case Item::Hoe: {
        InvSlot& slot = p.inv[p.sel];
        if (!spend(def.energy)) return "Exhausted";
        // Initialize tool durability if new
        if (slot.max_durability == 0) slot.max_durability = w.tool_max_durability(slot.item);
        Tile t = c.tile;
        if (t == Tile::Grass || t == Tile::GrassVar || t == Tile::Dirt ||
            t == Tile::Sand || t == Tile::Tilled) {
            c.tile = Tile::Tilled;
            c.crop = Crop{};
            // ROADMAP 2.5 — tilling cultivates the soil: dormant wild seeds are destroyed.
            c.seed_bank = 0;
            c.seed_bank_species = 0;
            // ROADMAP 2.7 (8.3) — tool wear: hoeing wears the tool
            w.apply_tool_wear(slot, 2);
            return "";
        }
        return "Can't hoe here";
    }
    case Item::WateringCan: {
        InvSlot& can = p.inv[p.sel];
        if (is_water_any(c.tile)) {            // refill from water
            can.count = 40;
            return "Can refilled";
        }
        // ROADMAP 2.2: refill from well if adjacent or standing on well
        Cell* well_cell = nullptr;
        // Check current cell first (player standing on well)
        Cell& current_cell = w.at(p.pos);
        if (current_cell.obj.type == ObjType::Well) {
            well_cell = &current_cell;
        } else if (c.obj.type == ObjType::Well) {  // facing cell
            well_cell = &c;
        } else {
            Vec2 f = facing_cell(p);
            if (w.in_bounds(f) && w.at(f).obj.type == ObjType::Well) {
                well_cell = &w.at(f);
            }
        }
        if (well_cell && can.count < 40) {
            if (well_cell->obj.hp > 0) {
                int fill = std::min(40 - static_cast<int>(can.count), static_cast<int>(well_cell->obj.hp));
                can.count = static_cast<uint16_t>(can.count + fill);
                well_cell->obj.hp -= static_cast<uint8_t>(fill);
                return "Drew " + std::to_string(fill) + " units from well. Can: " + std::to_string(can.count) + "/40";
            } else {
                return "Well is dry";
            }
        }
        if (can.count == 0) return "Can is empty";
        if (c.tile == Tile::Tilled) {
            if (!spend(def.energy)) return "Exhausted";
            // Initialize tool durability if new
            if (can.max_durability == 0) can.max_durability = w.tool_max_durability(can.item);
            can.count--;
            c.crop.watered = true;
            // ROADMAP 2.2: irrigation slightly raises local saturation
            c.saturation = static_cast<uint8_t>(std::min<int>(c.saturation + 5, 255));
            // ROADMAP 2.7 (8.3) — tool wear: watering wears the can
            w.apply_tool_wear(can, 1);
            return "";
        }
        return "Water the soil";
    }
    case Item::Axe: {
        InvSlot& slot = p.inv[p.sel];
        if (!is_tree(c.obj.type) && c.obj.type != ObjType::Stump) return "";
        if (!spend(def.energy)) return "Exhausted";
        // Initialize tool durability if new
        if (slot.max_durability == 0) slot.max_durability = w.tool_max_durability(slot.item);
        // ROADMAP 2.7 (8.3) — tool wear: chopping wears the axe
        w.apply_tool_wear(slot, 3);
        if (--c.obj.hp > 0) return "";
        ObjType was = c.obj.type;
        bool was_old_growth = c.tree.old_growth;   // ROADMAP 2.5 — legacy value
        bool was_player_managed = c.tree.player_managed;
        c.obj = FarmObj{};
        c.tree = TreeState{};                       // clear individual physiology
        if (was == ObjType::Stump) {
            add_item(p, Item::Wood, 2);
            return "+2 wood";
        }
        c.obj = {ObjType::Stump, 1};
        // Drop appropriate log based on tree type
        Item log = Item::Wood;
        if (is_tree(was)) log = tree_log_item(was);
        int log_amount = 4;
        // Hardwood trees give more/hardwood
        if (was == ObjType::Oak || was == ObjType::Maple || was == ObjType::Cedar || 
            was == ObjType::Redwood || was == ObjType::Teak || was == ObjType::Mahogany ||
            was == ObjType::WalnutTree || was == ObjType::HickoryTree || was == ObjType::ChestnutTree) {
            add_item(p, Item::Hardwood, 1);
            log_amount = 6;
        }
        // Deodar gives hardwood + resin
        if (was == ObjType::Deodar) {
            add_item(p, Item::Hardwood, 1);
            add_item(p, Item::DeodarResin, 1);
            log_amount = 6;
        }
        // ROADMAP 2.5 — old-growth trees: legacy timber + disturbance legacy.
        // Felling a giant opens a canopy gap (windthrow + nurse log) that the
        // ecology tick turns into a pioneer surge.
        if (was_old_growth) {
            add_item(p, Item::Hardwood, 2);
            log_amount += 6;
            if (c.forest_state == 0) c.forest_state = 1;
            forest_set_windthrow(c.forest_state, true);
            forest_set_nurse_log(c.forest_state, true);
            forest_set_canopy(c.forest_state, 1);
        }
        if (was_player_managed && c.forest_state != 0)
            forest_set_player_managed(c.forest_state, false);
        // Sapling drop: mature trees (hp > 100) have 20% chance to drop a sapling
        // Use the tree's hp before it became a stump (stored in was hp, but we need to track it)
        // The tree's hp was in c.obj.hp before we changed it to stump
        // We'll use a deterministic check based on position and day
        int tree_hp_before = (static_cast<int>(w.day) * 7 + tx * 13 + ty * 19) % 255; // deterministic proxy
        if (tree_hp_before > 100 && (static_cast<int>(w.day) * 11 + tx * 17 + ty * 23) % 100 < 20) {
            Item sapling = Item::None;
            switch (was) {
                case ObjType::Tree: sapling = Item::OakSapling; break;
                case ObjType::Pine: sapling = Item::None; break; // pine doesn't drop saplings
                case ObjType::Oak: sapling = Item::OakSapling; break;
                case ObjType::Maple: sapling = Item::MapleSapling; break;
                case ObjType::Birch: sapling = Item::BirchSapling; break;
                case ObjType::Cedar: sapling = Item::CedarSapling; break;
                case ObjType::Redwood: sapling = Item::RedwoodSapling; break;
                case ObjType::Teak: sapling = Item::TeakSapling; break;
                case ObjType::Mahogany: sapling = Item::MahoganySapling; break;
                case ObjType::RubberTree: sapling = Item::RubberTreeSapling; break;
                case ObjType::WalnutTree: sapling = Item::WalnutSapling; break;
                case ObjType::HickoryTree: sapling = Item::HickorySapling; break;
                case ObjType::ChestnutTree: sapling = Item::ChestnutSapling; break;
                case ObjType::Deodar: sapling = Item::DeodarSapling; break;
                default: sapling = Item::None; break;
            }
            if (sapling != Item::None) {
                add_item(p, sapling, 1);
                return "+" + std::to_string(log_amount) + " " + std::string(item_def(log).name) + " + 1 " + std::string(item_def(sapling).name) + 
                       (was == ObjType::Pine ? " (pine resin drips)" : 
                        (was == ObjType::RubberTree ? " (latex sap drips)" : 
                         (was == ObjType::Deodar ? " (deodar resin drips)" : "")));
            }
        }
        // Note: tx, ty are the target coordinates
        (void)tx; (void)ty;
        return "+" + std::to_string(log_amount) + " " + std::string(item_def(log).name) + 
               (was == ObjType::Pine ? " (pine resin drips)" : 
                (was == ObjType::RubberTree ? " (latex sap drips)" : ""));
    }
    case Item::Pickaxe: {
        InvSlot& slot = p.inv[p.sel];
        if (c.obj.type != ObjType::Rock) return "";
        if (!spend(def.energy)) return "Exhausted";
        // Initialize tool durability if new
        if (slot.max_durability == 0) slot.max_durability = w.tool_max_durability(slot.item);
        // ROADMAP 2.7 (8.3) — tool wear: mining wears the pickaxe
        w.apply_tool_wear(slot, 4);
        if (--c.obj.hp > 0) return "";
        if (c.obj.ore) {
            Item ore_item = Item::CopperOre;
            if (c.obj.ore == 2) ore_item = Item::IronOre;
            else if (c.obj.ore == 3) ore_item = Item::GoldOre;
            else if (c.obj.ore == 4) ore_item = Item::IridiumOre;
            c.obj = FarmObj{};
            add_item(p, ore_item, 1);
            return "+1 " + std::string(item_def(ore_item).name);
        }
        c.obj = FarmObj{};
        add_item(p, Item::Stone, 2);
        return "+2 stone";
    }
    case Item::Scythe: {
        InvSlot& slot = p.inv[p.sel];
        if (c.obj.type != ObjType::Weed && c.obj.type != ObjType::TallGrass &&
            c.obj.type != ObjType::Mushroom) return "";
        if (!spend(def.energy)) return "Exhausted";
        // Initialize tool durability if new
        if (slot.max_durability == 0) slot.max_durability = w.tool_max_durability(slot.item);
        // ROADMAP 2.7 (8.3) — tool wear: scything wears the scythe
        w.apply_tool_wear(slot, 2);
        bool tall = c.obj.type == ObjType::TallGrass;
        bool shroom = c.obj.type == ObjType::Mushroom;
        bool is_weed = c.obj.type == ObjType::Weed;
        c.obj = FarmObj{};
        add_item(p, Item::Fiber, 1);
        // Weed seed drops: 15% chance for mixed seeds when cutting weeds
        if (is_weed && (static_cast<int>(w.day) * 7 + tx * 13 + ty * 19) % 100 < 15) {
            static const Item seeds[] = {
                Item::ParsnipSeeds, Item::PotatoSeeds, Item::CauliflowerSeeds,
                Item::CornSeeds, Item::TomatoSeeds, Item::WheatSeeds,
                Item::BlueberrySeeds, Item::GreenBeanSeeds, Item::HopsSeeds,
                Item::StrawberrySeeds, Item::MelonSeeds, Item::PumpkinSeeds,
                Item::RedCabbageSeeds, Item::RhubarbSeeds, Item::GarlicSeeds,
                Item::ArtichokeSeeds, Item::BokChoySeeds, Item::KaleSeeds,
                Item::CranberrySeeds, Item::GrapeSeeds
            };
            Item seed = seeds[(static_cast<int>(w.day) * 11 + tx * 17 + ty * 23) % 20];
            add_item(p, seed, 1);
            return "+1 fiber + 1 " + std::string(item_def(seed).name) + " (mixed seeds!)";
        }
        // Tall grass: 10% chance for hay
        if (tall && (static_cast<int>(w.day) * 13 + tx * 19 + ty * 23) % 100 < 10) {
            add_item(p, Item::Fiber, 1);
            return "+1 fiber + 1 fiber (hay)";
        }
        return tall || shroom ? "+1 fiber" : "+1 fiber";
    }
    default:
        // seeds: plant on tilled soil
        if (def.type == 1 && c.tile == Tile::Tilled && !c.crop.is_crop()) {
            const CropDef* cd = crop_by_seed(tool);
            if (!cd) return "";
            if (!spend(1)) return "Exhausted";
            if (!consume_item(p, tool, 1)) return "";
            c.crop.crop = cd->produce;
            c.crop.stage = 0;
            c.crop.days_left = static_cast<int8_t>(cd->days);
            c.crop.watered = false;
            c.crop.is_trellis = false;
            c.crop.is_fruit_tree = false;
            c.crop.last_harvest_season = -1;
            // Trellis crops: green bean, hops
            if (cd->produce == Item::GreenBean || cd->produce == Item::Hops) {
                c.crop.is_trellis = true;
            }
            // Fruit trees: apple, cherry, peach, pomegranate
            if (cd->produce == Item::Apple || cd->produce == Item::Cherry ||
                cd->produce == Item::Peach || cd->produce == Item::Pomegranate) {
                c.crop.is_fruit_tree = true;
            }
            return "Planted";
        }
    }
    return "";
}

// ---------- text command interpreter ----------

static std::string lower_trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::vector<std::string> split_words(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string w;
    while (iss >> w) out.push_back(w);
    return out;
}

static const char* terrain_name(Tile t) {
    switch (t) {
    case Tile::Grass: case Tile::GrassVar: return "grass";
    case Tile::Water: return "water";
    case Tile::WaterNorth: return "water (north shore)";
    case Tile::WaterSouth: return "water (south shore)";
    case Tile::WaterEast: return "water (east shore)";
    case Tile::WaterWest: return "water (west shore)";
    case Tile::Dirt: return "packed dirt";
    case Tile::Sand: return "sand";
    case Tile::Tilled: return "tilled soil";
    case Tile::Bridge: return "wooden bridge";
    case Tile::Snow: return "snow";
    case Tile::Ice: return "frozen lake ice";
    case Tile::Cobble: return "cobblestone street";
    }
    return "unknown";
}

static const char* obj_name(ObjType o) {
    switch (o) {
    case ObjType::Tree: return "a tall oak tree";
    case ObjType::Pine: return "a towering pine tree";
    case ObjType::Bush: return "a dense berry bush";
    case ObjType::Mushroom: return "a cluster of mushrooms";
    case ObjType::Rock: return "a large gray rock";
    case ObjType::Weed: return "some weeds";
    case ObjType::TallGrass: return "tall grass";
    case ObjType::Flower: return "a patch of wildflowers";
    case ObjType::Building: return "a sturdy building";
    case ObjType::Stump: return "a tree stump";
    case ObjType::FencePost: return "a fence post";
    case ObjType::FenceRail: return "a wooden fence rail";
    case ObjType::Sprinkler:  return "an irrigation sprinkler";
    case ObjType::Statue: return "a carved stone statue";
    case ObjType::LeafLitter: return "a drift of fallen leaves";
    case ObjType::Scarecrow: return "a scarecrow";
    case ObjType::Composter: return "a composter";
    case ObjType::Oak: return "an oak tree";
    case ObjType::Maple: return "a maple tree";
    case ObjType::Birch: return "a birch tree";
    case ObjType::Cedar: return "a cedar tree";
    case ObjType::Redwood: return "a mighty redwood";
    case ObjType::Teak: return "a teak tree";
    case ObjType::Mahogany: return "a mahogany tree";
    case ObjType::RubberTree: return "a rubber tree";
    case ObjType::WalnutTree: return "a walnut tree";
    case ObjType::HickoryTree: return "a hickory tree";
    case ObjType::ChestnutTree: return "a chestnut tree";
    case ObjType::Deodar: return "a deodar cedar tree";
    case ObjType::Well: return "a stone well";
    case ObjType::Pond: return "a tranquil pond";
    default: return "something (unknown type)";
    }
}

static const char* stage_desc(Item c, uint8_t stage) {
    if (stage == 0) return "tiny sprouts have just broken soil";
    if (stage == 1) return "young green shoots";
    if (stage == 2) return "a bushy plant, almost ready";
    if (c == Item::Cauliflower) return "a big white head, ready to harvest";
    if (c == Item::Potato) return "a leafy plant, ready to harvest";
    return "a full grown parsnip, ready to harvest";
}

static Vec2 facing_cell(Player& p) {
    switch (p.dir) {
    case 0: return {p.pos.x, int16_t(p.pos.y + 1)};
    case 1: return {int16_t(p.pos.x - 1), p.pos.y};
    case 2: return {int16_t(p.pos.x + 1), p.pos.y};
    default: return {p.pos.x, int16_t(p.pos.y - 1)};
    }
}

static int find_slot(Player& p, Item it) {
    for (size_t i = 0; i < p.inv.size(); ++i)
        if (p.inv[i].item == it) return static_cast<int>(i);
    return -1;
}
static int count_item(Player& p, Item it) {
    int n = 0;
    for (size_t i = 0; i < p.inv.size(); ++i) if (p.inv[i].item == it) n += p.inv[i].count;
    return n;
}
static bool has_item(Player& p, Item it, int need) { return count_item(p, it) >= need; }
static void consume_item(Player& p, Item it, int need) {
    for (size_t i = 0; i < p.inv.size() && need > 0; ++i) if (p.inv[i].item == it) {
        int t = std::min(need, static_cast<int>(p.inv[i].count));
        p.inv[i].count -= static_cast<uint16_t>(t); need -= t;
        if (p.inv[i].count == 0) p.inv[i].item = Item::None;
    }
}

// gift taste: -2 hate, -1 dislike, 0 neutral, +1 like, +2 love
static int gift_taste(const std::string& npc, Item it) {
    auto eq = [&](Item v) { return v == it; };
    int val = 0;
    if (npc == "Leah") {
        if (eq(Item::Forage) || eq(Item::Fish)) val = 2;
        else if (eq(Item::Parsnip) || eq(Item::Blueberry) || eq(Item::Bread)) val = 1;
        else if (eq(Item::Wheat)) val = -1;
    } else if (npc == "Abigail") {
        if (eq(Item::Bread) || eq(Item::Corn) || eq(Item::Tomato) || eq(Item::Blueberry)) val = 2;
        else if (eq(Item::Fish) || eq(Item::Parsnip)) val = 1;
        else if (eq(Item::Wood) || eq(Item::Stone) || eq(Item::Wheat)) val = -1;
    } else if (npc == "Elliot") {
        if (eq(Item::Fish) || eq(Item::Forage)) val = 2;
        else if (eq(Item::Bread) || eq(Item::Blueberry)) val = 1;
        else if (eq(Item::Stone)) val = -1;
    } else if (npc == "Robin") {
        if (eq(Item::Wood)) val = 2;
        else if (eq(Item::Stone) || eq(Item::Fish) || eq(Item::Forage)) val = 1;
        else if (eq(Item::Bread) || eq(Item::Wheat)) val = -1;
    } else if (npc == "Evelyn") {
        if (eq(Item::Forage) || eq(Item::Blueberry)) val = 2;
        else if (eq(Item::Bread) || eq(Item::Parsnip) || eq(Item::Potato) || eq(Item::Cauliflower)) val = 1;
    }
    return val;
}

static Item item_from_name(const std::string& s) {
    std::string lc = lower_trim(s);
    // exact item name match first (including seeds)
    for (int i = 1; i <= 158; ++i) {
        Item it = static_cast<Item>(i);
        std::string nm = lower_trim(item_def(it).name);
        if (nm == lc) return it;
    }
    // plural / "seeds" suffix
    for (int i = 1; i <= 158; ++i) {
        Item it = static_cast<Item>(i);
        std::string nm = lower_trim(item_def(it).name);
        if (nm + "s" == lc || nm + " seed" == lc || nm + " seeds" == lc) return it;
    }
    return Item::None;
}

static std::string furniture_name(char c) {
    switch (c) {
    case 'B': return "a bed";
    case 'T': return "a table";
    case 'S': return "a stove";
    case 'C': return "a counter";
    case 'M': return "a market stall";
    case 'D': return "a desk";
    case 'E': return "an exhibit case";
    case 'A': return "the anvil";
    case 'F': return "a forge";
    case 'X': return "sacks of grain";
    case 'G': return "a chair";
    case 'N': return "a bench";
    case 'P': return "the platform edge";
    case 'H': return "a hay bale";
    default: return "something";
    }
}

// Starter kit for a brand-new farmer (mirrors the /join handler).
static Player make_fresh_player(uint32_t id, const std::string& name, Vec2 pos) {
    Player p;
    p.id = id;
    p.name = name;
    p.pos = pos;
    p.target = pos;
    p.last_safe_pos = pos;   // fresh farmers wake at the farmhouse door
    p.inv[0] = {Item::Hoe, 1, 0, World::tool_max_durability(Item::Hoe)};
    p.inv[1] = {Item::WateringCan, 40, 0, World::tool_max_durability(Item::WateringCan)};
    p.inv[2] = {Item::Axe, 1, 0, World::tool_max_durability(Item::Axe)};
    p.inv[3] = {Item::Pickaxe, 1, 0, World::tool_max_durability(Item::Pickaxe)};
    p.inv[4] = {Item::Scythe, 1, 0, World::tool_max_durability(Item::Scythe)};
    p.inv[5] = {Item::ParsnipSeeds, 15};
    p.inv[6] = {Item::PotatoSeeds, 15};
    p.inv[7] = {Item::CauliflowerSeeds, 10};
    return p;
}

// Levenshtein distance for fuzzy command matching
static int levenshtein(const std::string& a, const std::string& b) {
    const int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
    if (n == 0) return m;
    if (m == 0) return n;
    std::vector<int> dp(static_cast<size_t>(m) + 1u), prev(static_cast<size_t>(m) + 1u);
    for (int j = 0; j <= m; ++j) prev[static_cast<size_t>(j)] = j;
    for (int i = 1; i <= n; ++i) {
        dp[0] = i;
        for (int j = 1; j <= m; ++j) {
            const size_t ui = static_cast<size_t>(i), uj = static_cast<size_t>(j);
            int cost = (a[ui - 1] == b[uj - 1]) ? 0 : 1;
            dp[uj] = std::min({prev[uj] + 1, dp[uj - 1] + 1, prev[uj - 1] + cost});
        }
        prev.swap(dp);
    }
    return prev[static_cast<size_t>(m)];
}

// Find closest command matches for suggestions
static std::vector<std::string> suggest_commands(const std::string& input, int max_suggestions = 3) {
    static const std::vector<std::string> all_commands = {
        "help", "look", "go", "move", "north", "south", "east", "west",
        "inventory", "inv", "status", "stats", "time", "eat", "drink",
        "hoe", "till", "plant", "water", "harvest", "fertilize",
        "axe", "chop", "cut", "pick", "mine", "pickaxe", "scythe", "clear",
        "fish", "cast", "reel", "forage", "search", "shop", "buy", "sell",
        "craft", "cook", "make", "bake", "place", "build", "construct",
        "repair", "fix", "upgrade", "enter", "inside", "exit", "leave", "out",
        "interact", "use", "train", "bus", "tv", "watch", "talk", "speak",
        "gift", "give", "hearts", "friends", "festival", "fest",
        "sleep", "rest", "bed", "save", "load", "newgame", "plots", "deeds",
        "basement", "horror", "sanity", "dsl", "explore", "map", "travel",
        "region", "fasttravel", "buy plot", "place barn", "place coop", "weather",
        "inspect", "toolrepair", "fire", "structural", "creature", "herd", "disease"
    };
    std::vector<std::pair<int, std::string>> scores;
    for (const auto& cmd : all_commands) {
        int dist = levenshtein(input, cmd);
        if (dist <= 3) scores.push_back({dist, cmd});
    }
    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> result;
    for (size_t i = 0; i < std::min<size_t>(scores.size(), static_cast<size_t>(max_suggestions)); ++i) {
        result.push_back(scores[i].second);
    }
    return result;
}

// ROADMAP 1.7d: produce a cognitive-aware one-line NPC reply. When the NPC has
// a cognitive core, build a prompt from its state (dominant emotion, most-
// needed drive, trust toward the player, most salient memory) and ask the LLM.
// Falls back to the static template on any failure / when no LLM is wired.
static std::string cognitive_dialogue_line(const std::string& npc_name,
                                           const ashgrove::CognitiveCore* core,
                                           int season) {
    if (!core || !g_dialogue_llm) return npc_line(npc_name.c_str(), season);
    const ashgrove::CognitiveState& s = core->state();

    // Dominant emotion tag.
    std::string emotion = "calm";
    float peak = 0.0f;
    {
        const ashgrove::EmotionalTag& e = s.current_emotion;
        struct E { const char* n; float v; };
        E es[] = {{"joy", e.joy}, {"fear", e.fear}, {"trust", e.trust},
                  {"anger", e.anger}, {"surprise", e.surprise},
                  {"anticipation", e.anticipation}, {"disgust", e.disgust}};
        for (auto& x : es) if (x.v > peak) { peak = x.v; emotion = x.n; }
        if (peak < 0.25f) emotion = "calm";
    }

    // Most-needed drive (lowest satisfaction).
    const char* drive_names[] = {"hungry", "thirsty", "lonely", "uneasy",
                                 "curious", "tired"};
    std::string need = "fine";
    float low = 1.0f;
    for (std::size_t i = 0; i < s.drives.drive_satisfaction.size(); ++i) {
        if (s.drives.drive_satisfaction[i] < low) {
            low = s.drives.drive_satisfaction[i];
            need = drive_names[i];
        }
    }

    // Trust toward the player (from the social graph).
    float trust = 0.5f;
    {
        auto it = s.social_graph.find("player");
        if (it != s.social_graph.end()) trust = it->second.trust;
    }

    // Most salient memory (highest-relevance working-memory ref).
    std::string memory = "the valley's quiet routines";
    float best = 0.0f;
    for (const auto& it : s.working_memory)
        if (it.relevance > best) { best = it.relevance; memory = it.stimulus_ref; }

    std::string prompt =
        "You are " + npc_name + ", a villager in Ashgrove Valley. "
        "You are feeling " + emotion + ", mostly " + need + ". "
        "You trust the farmer " + std::to_string(static_cast<int>(trust * 100)) +
        " percent. You are thinking about " + memory + ".\n"
        "Reply with exactly ONE short line of in-character dialogue, in quotes, "
        "about your current mood or the season. Do not add stage directions.";

    std::string raw = g_dialogue_llm(prompt, 40, 0.8f);
    std::cerr << "[cognitive_dialogue] " << npc_name << " raw='" << raw << "'" << std::endl;
    // Trim whitespace and normalize; strip trailing punctuation-only noise.
    raw.erase(0, raw.find_first_not_of(" \t\r\n"));
    while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r' ||
                            raw.back() == ' ')) raw.pop_back();
    // ROADMAP 1.7d: strict validation - reject obvious narrative garbage.
    // Accept only a single short line that looks like direct speech.
    bool ok = raw.size() >= 6 && raw.size() <= 120 &&
              raw.find('"') != std::string::npos &&
              raw.find("said") == std::string::npos &&
              raw.find("I can") == std::string::npos &&
              raw.find("You ") != 0 &&  // don't start with "You "
              std::count(raw.begin(), raw.end(), '.') <= 1 &&
              std::count(raw.begin(), raw.end(), ',') <= 2;
    if (!ok) return npc_line(npc_name.c_str(), season);
    // Keep it a single line.
    auto nl = raw.find('\n');
    if (nl != std::string::npos) raw = raw.substr(0, nl);
    return raw;
}

// ---- ROADMAP 2.3 (8.1c) Plant Genetics helpers ----
// Variety reference = all-zero alleles (0 == reference). Saved/bred seeds carry
// drifted alleles; growing crops converge back toward reference, raising
// homozygosity, which unlocks giant crops.
static SeedGen default_seed_gen() {
    SeedGen g;
    g.homozygosity = 255;  // reference is perfectly homozygous
    return g;
}
static SeedGen get_seed_gen(Player& p, Item seed_item) {
    if (auto it = p.seed_gen.find(seed_item); it != p.seed_gen.end()) return it->second;
    return default_seed_gen();
}
static uint8_t compute_homozygosity(const std::array<int8_t, 16>& alleles) {
    int homo = 0;
    for (int8_t a : alleles) if (a == 0) ++homo;
    return static_cast<uint8_t>((homo * 255) / 16);
}
// Random-walk an allele toward reference (0) with a little noise. A reference
// allele (0) is stable; drifted alleles converge back to 0 over time, so a pure
// (homozygous) line stays pure and a drifted line recovers purity across days.
static int8_t drift_allele(int8_t v, std::mt19937& rng, int step) {
    std::uniform_int_distribution<int> noise(-1, 1);
    int nv = static_cast<int>(v) + noise(rng);
    // pull toward 0
    if (nv > 0) nv = std::max(nv - step, 0);
    else if (nv < 0) nv = std::min(nv + step, 0);
    return static_cast<int8_t>(std::clamp(nv, -40, 40));
}
// Update L-System morphology fields from alleles (called each growing day).
static void grow_morphology(Crop& c, int growth) {
    auto& a = c.alleles;
    float g = static_cast<float>(growth);
    c.height  = std::min(c.height  + g * (0.4f + a[0] / 128.0f), 20.0f);
    c.biomass = std::min(c.biomass + g * (1.2f + (a[0] + a[8]) / 256.0f), 100.0f);
    c.root_depth   = std::min(c.root_depth   + g * (0.3f + a[6] / 128.0f), 10.0f);
    c.canopy_width = std::min(c.canopy_width + g * (0.3f + a[7] / 128.0f), 8.0f);
}

static std::vector<std::string> handle_cmd(World& w, Player& p, const std::string& raw) {
    std::vector<std::string> out;
    auto say = [&](const std::string& s) { out.push_back(s); };
    auto words = split_words(lower_trim(raw));
    if (words.empty()) { say("Type 'help' for a list of commands."); return out; }
    std::string cmd = words[0], arg = "";
    if (words.size() > 1) {
        arg = words[1];
        for (size_t i = 2; i < words.size(); ++i) arg += " " + words[i];
    }
    int season = season_index(w.day);

    // ---------- help ----------
    if (cmd == "help" || cmd == "?") {
        say("=== ESSENTIAL COMMANDS ===");
        say("  go <dir>       move (north/south/east/west, or n/s/e/w)");
        say("  go <dir> <n>   walk n tiles (e.g. 'go 5 north', 'go e 10')");
        say("  go to <name>   path-walk to a known landmark/building");
        say("  look           examine surroundings (alias: l)");
        say("  inventory      list what you carry (alias: inv)");
        say("  status         day, time, energy, money (alias: stats)");
        say("  hoe            till soil in front of you");
        say("  plant <crop>   plant seeds on tilled soil");
        say("  water          water the soil in front of you");
        say("  harvest        pick a ripe crop");
        say("  axe            chop a tree");
        say("  pickaxe        mine rocks (alias: pick, mine)");
        say("  scythe         clear weeds/grass");
        say("  explore <dir>  auto-walk until landmark/obstacle");
        say("  fasttravel <name>  teleport to known landmark (time/energy/sanity cost)");
        say("  fish           cast a line if water is near");
        say("  talk <name>    chat with a nearby villager");
        say("  gift <n> <it>  give a present to adjacent villager");
        say("  eat <item>     snack for energy");
        say("  buy <item>     buy at the shop");
        say("  sell <item>    sell produce/items");
        say("  craft <item>   make: bread, scarecrow, composter");
        say("  place <thing>  build: sprinkler, scarecrow, composter");
        say("  plots          list buyable/owned plots (alias: deeds)");
        say("  buy plot <n>   buy a parcel at the Town Center");
        say("  enter [<name>] step into a building at its door");
        say("  exit           leave current building (alias: leave)");
        say("  sleep          rest until morning (near house door)");
        say("  save [<name>]  write game state (default: save.json)");
        say("  load [<name>]  restore a save file");
        say("");
        say("=== ADVANCED COMMANDS ===");
        say("  planttree <tree>  plant: oak, maple, birch, cedar, redwood, teak,");
        say("                     mahogany, rubber, walnut, hickory, chestnut, deodar");
        say("  tap <tree>     install/collect tapper for sap/syrup/resin/rubber");
        say("  shake <tree>   shake mature trees for saplings (costs 2 energy)");
        say("  breed <s1> <s2>  cross two seeds to recombine genetics (2.3)");
        say("  pest           farm pest & disease report (2.4)");
        say("  spray [all]    clear pests/blight on crops ahead, or the whole farm");
        say("  release [ladybugs|lacewings] [n]  release beneficial insects");
        say("  companion      show companion-planting effects");
        say("  ecology        forest & tree ecology report (2.5)");
        say("  repair <bldg>  fix a building at Carpenter Shop");
        say("  upgrade farmhouse  expand: cottage/house/manor");
        say("  interact       use furniture inside buildings (alias: use)");
        say("  train          ride Zuzu City Express from station");
        say("  bus            take town bus to plaza");
        say("  tv             watch valley news in farmhouse");
        say("  hearts         check friendship with villagers");
        say("  festival       join seasonal festivals");
        say("");
        say("Type 'help <command>' for details. Commands are case-insensitive.");
        return out;
    }

    // ---------- status / inventory ----------
    if (cmd == "status" || cmd == "stats") {
        // ROADMAP 1.4: at fractured sanity, the clock string occasionally lies.
        {
            int ptier = w.perception_tier(p);
            std::string clock = clock_str(w);
            if (ptier >= 3) {
                std::mt19937 rng(p.id * 3797u + w.day * 89u);
                if ((rng() % 100u) < 30u) {
                    // The clock shows a wrong but unsettling time.
                    const char* false_clocks[] = {"3:33 AM", "12:00 AM", "0:00 AM", "13:13 PM"};
                    say(std::string(false_clocks[rng() % 4]) + " (?)");
                } else {
                    say(clock);
                }
            } else {
                say(clock);
            }
        }
        say(std::string(season_name(season_index(w.day))) + " " +
            std::to_string(season_day(w.day)) + " · " + weather_name_adapted(w, w.day));
        say("Energy: " + std::to_string(static_cast<int>(p.energy)) + "/" + std::to_string(p.max_energy) +
            "   Money: " + std::to_string(p.money) + "g");
        say("Health: " + std::to_string(static_cast<int>(p.health)) + "/" + std::to_string(static_cast<int>(p.max_health)) +
            "   Sanity: " + std::to_string(static_cast<int>(p.sanity)) + "/" + std::to_string(static_cast<int>(p.max_sanity)));
        if (p.death_count > 0)
            say("The valley has watched you die " + std::to_string(p.death_count) + " time" + (p.death_count == 1 ? "." : "s."));
        // ROADMAP 1.4: surface the persistent basement mark.
        if (!p.basement_mark.empty() && p.mark_days_left > 0) {
            say("Carrying a " + p.basement_mark + " mark from the cellar (" +
                std::to_string(p.mark_days_left) + " day" + (p.mark_days_left == 1 ? "" : "s") + " left).");
        }
        // Surface any pending death narration from the time-based death check.
        if (!p.pending_death.empty()) {
            say(p.pending_death);
            p.pending_death.clear();
        }
        // Building condition + farmhouse level
        const char* level_names[] = {"", "Starter", "Cottage", "House", "Manor"};
        say("Farmhouse: Level " + std::to_string(w.farmhouse_level) + " (" + level_names[w.farmhouse_level] + ")");
        auto it = w.building_states.find("Farmhouse");
        if (it != w.building_states.end()) {
            say("  Condition: roof " + std::to_string(it->second.roof_leak) +
                " foundation " + std::to_string(it->second.foundation) +
                " condition " + std::to_string(it->second.condition));
        }
        return out;
    }
    if (cmd == "soil" || cmd == "soiltest" || cmd == "testsoil") {
        // ROADMAP 2.1 (8.1a) — Soil test command
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("Nothing to test there."); return out; }
        Cell& c = w.at(f);
        if (c.tile != Tile::Tilled && c.tile != Tile::Grass && c.tile != Tile::GrassVar && c.tile != Tile::Dirt) {
            say("You can only test soil on tilled or natural ground.");
            return out;
        }
        say("=== Soil Test Results ===");
        say("Tile: " + std::string(terrain_name(c.tile)));
        say("Nitrogen (N): " + std::to_string(c.nitrogen) + "/255 " + 
            (c.nitrogen > 180 ? "[HIGH]" : c.nitrogen > 120 ? "[GOOD]" : c.nitrogen > 60 ? "[LOW]" : "[VERY LOW]"));
        say("Phosphorus (P): " + std::to_string(c.phosphorus) + "/255 " + 
            (c.phosphorus > 180 ? "[HIGH]" : c.phosphorus > 120 ? "[GOOD]" : c.phosphorus > 60 ? "[LOW]" : "[VERY LOW]"));
        say("Potassium (K): " + std::to_string(c.potassium) + "/255 " + 
            (c.potassium > 180 ? "[HIGH]" : c.potassium > 120 ? "[GOOD]" : c.potassium > 60 ? "[LOW]" : "[VERY LOW]"));
        float ph_val = c.ph / 10.0f;
        say("pH: " + std::to_string(ph_val) + " " + 
            (c.ph >= 60 && c.ph <= 70 ? "[OPTIMAL]" : c.ph >= 55 && c.ph <= 75 ? "[OK]" : "[NEEDS ADJUSTMENT]"));
        say("Organic Matter: " + std::to_string(c.organic_matter / 2) + "% " + 
            (c.organic_matter > 100 ? "[GOOD]" : c.organic_matter > 50 ? "[OK]" : "[LOW]"));
        say("Microbiome: " + std::to_string(c.microbiome) + "/255 " + 
            (c.microbiome > 180 ? "[RICH]" : c.microbiome > 100 ? "[MODERATE]" : "[POOR]"));
        if (c.crop.is_crop()) {
            say("Current crop: " + std::string(item_def(c.crop.crop).name) + " (stage " + std::to_string(c.crop.stage) + ")");
        }
        say("");
        say("Recommendations:");
        if (c.nitrogen < 80) say("  - Apply nitrogen fertilizer (nitrogen, balanced, or organic)");
        if (c.phosphorus < 80) say("  - Apply phosphorus fertilizer (phosphorus, balanced, or organic)");
        if (c.potassium < 80) say("  - Apply potassium fertilizer (potassium, balanced, or wood ash)");
        if (c.ph < 55) say("  - Soil too acidic: apply lime to raise pH");
        else if (c.ph > 75) say("  - Soil too alkaline: apply sulfur to lower pH");
        if (c.organic_matter < 60) say("  - Low organic matter: add compost or organic fertilizer");
        if (c.microbiome < 100) say("  - Poor microbiome: add organic matter, reduce chemical inputs");
        return out;
    }
    // ---------- ROADMAP 2.4 (8.1d) — pest / disease / predators ----------
    if (cmd == "pest" || cmd == "pests" || cmd == "peststatus") {
        int aphids = 0, caterpillars = 0, locusts = 0;
        for (auto& a : w.pests) {
            if (a.kind == 0) aphids++;
            else if (a.kind == 1) caterpillars++;
            else if (a.kind == 2) locusts++;
        }
        int ladybugs = 0, lacewings = 0;
        for (auto& a : w.predators) {
            if (a.kind == 128) ladybugs++;
            else if (a.kind == 129) lacewings++;
        }
        if (w.pests.empty() && w.predators.empty()) {
            say("The farm is clean — no pests, no predators.");
        } else {
            say("=== Pest Report ===");
            say("  Pests:  " + std::to_string(aphids) + " aphids, " +
                std::to_string(caterpillars) + " caterpillars, " + std::to_string(locusts) + " locusts");
            say("  Predators: " + std::to_string(ladybugs) + " ladybugs, " +
                std::to_string(lacewings) + " lacewings");
            if (w.pest_bias != 1.0f)
                say("  Severity: x" + std::to_string(w.pest_bias));
        }
        // Most-infested crops
        int worst_level = 0, worst_count = 0, worst_disease = 0;
        for (auto& cell : w.cells) {
            if (!cell.crop.is_crop()) continue;
            if (cell.crop.pest_level > 40) worst_count++;
            worst_level = std::max(worst_level, static_cast<int>(cell.crop.pest_level));
            worst_disease = std::max(worst_disease, static_cast<int>(cell.crop.disease_level));
        }
        if (worst_count > 0)
            say("  " + std::to_string(worst_count) + " crop" + (worst_count == 1 ? "" : "s") +
                " under heavy pest pressure (peak " + std::to_string(worst_level) + "/255).");
        if (worst_disease > 0)
            say("  Fungal infection present (peak " + std::to_string(worst_disease) + "/255). 'spray all' to clear.");
        if (worst_count == 0 && worst_disease == 0 && !w.pests.empty())
            say("  Infestation is young — spray soon.");
        return out;
    }
    if (cmd == "spray" || cmd == "spraycrops") {
        // Spray costs 100g + 15 energy; clears pest_level/disease_level.
        const int cost = 100;
        if (p.money < cost) { say("Spray costs " + std::to_string(cost) + "g. You can't afford it."); return out; }
        if (p.energy < 15.0f) { say("You're too tired to spray."); return out; }
        bool all = (arg == "all" || arg == "everything" || arg == "farm");
        int cleared = 0;
        if (all) {
            for (auto& cell : w.cells) {
                if (!cell.crop.is_crop()) continue;
                cell.crop.pest_level = 0;
                cell.crop.disease_level = 0;
                cleared++;
            }
        } else {
            // Spray the crop in front of the player (and its 3x3 neighbourhood).
            int fx = p.pos.x + (p.dir == 2 ? 1 : p.dir == 1 ? -1 : 0);
            int fy = p.pos.y + (p.dir == 0 ? 1 : p.dir == 3 ? -1 : 0);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = fx + dx, ny = fy + dy;
                    if (!w.in_bounds(nx, ny)) continue;
                    Cell& c = w.at(nx, ny);
                    if (!c.crop.is_crop()) continue;
                    c.crop.pest_level = 0;
                    c.crop.disease_level = 0;
                    cleared++;
                }
            if (cleared == 0) { say("No crops to spray there."); return out; }
        }
        p.money -= cost;
        p.energy -= 15.0f;
        say("You spray the crops (" + std::to_string(cleared) + " plot" + (cleared == 1 ? "" : "s") +
            " cleared). -" + std::to_string(cost) + "g");
        return out;
    }
    if (cmd == "release" || cmd == "releasepredators") {
        // Release beneficial insects: 128=ladybug (aphids), 129=lacewing (caterpillars/locusts).
        std::vector<std::string> a = split_words(lower_trim(arg));
        uint8_t kind = 128;
        size_t ki = 0;
        for (; ki < a.size(); ++ki) {   // skip filler words ("some", "a", "the", "of")
            std::string wrd = a[ki];
            if (wrd == "some" || wrd == "a" || wrd == "an" || wrd == "the" || wrd == "of") continue;
            break;
        }
        if (ki < a.size() && (a[ki] == "lacewing" || a[ki] == "lacewings")) kind = 129;
        else if (ki >= a.size() || (a[ki] != "ladybug" && a[ki] != "ladybugs" &&
                 a[ki] != "bug" && a[ki] != "bugs")) {
            say("Usage: release [ladybugs|lacewings] [count]");
            return out;
        }
        int count = 3;
        if (ki + 1 < a.size()) {
            try { count = std::stoi(a[ki + 1]); } catch (...) {}
        }
        count = std::max(1, std::min(count, 10));
        int cost = count * 150;
        if (p.money < cost) {
            say("Releasing " + std::to_string(count) + " " + pest_kind_name(kind) +
                " costs " + std::to_string(cost) + "g. You can't afford it.");
            return out;
        }
        p.money -= cost;
        int placed = 0;
        for (int dy = -1; dy <= 1 && placed < count; ++dy)
            for (int dx = -1; dx <= 1 && placed < count; ++dx) {
                int nx = p.pos.x + dx, ny = p.pos.y + dy;
                if (!w.in_bounds(nx, ny)) continue;
                w.predators.push_back({kind, static_cast<int16_t>(nx), static_cast<int16_t>(ny), 10, 0});
                placed++;
            }
        if (placed < count) p.money += (count - placed) * 150;   // refund unplaced
        say("You release " + std::to_string(placed) + " " + pest_kind_name(kind) +
            " around the farm. -" + std::to_string(placed * 150) + "g");
        return out;
    }
    if (cmd == "companion" || cmd == "companions") {
        say("=== Companion Planting ===");
        int garlic_adj = 0, flower_adj = 0, hops_adj = 0, crops = 0;
        for (int y = 0; y < MAP_H; ++y)
            for (int x = 0; x < MAP_W; ++x) {
                Cell& c = w.at(x, y);
                if (!c.crop.is_crop()) continue;
                crops++;
                bool g = false, f = false, h = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx, ny = y + dy;
                        if (!w.in_bounds(nx, ny)) continue;
                        const Cell& n = w.at(nx, ny);
                        if (n.crop.is_crop() && n.crop.crop == Item::Garlic) g = true;
                        if (n.crop.is_crop() && n.crop.crop == Item::Hops) h = true;
                        if (n.obj.type == ObjType::Flower) f = true;
                    }
                if (g) garlic_adj++;
                if (f) flower_adj++;
                if (h) hops_adj++;
            }
        say("  Garlic next to a crop repels pests and blight (crops near garlic: " +
            std::to_string(garlic_adj) + ").");
        say("  Flowers shelter predators and muffle pest scent (crops near flowers: " +
            std::to_string(flower_adj) + ").");
        say("  Hops are an aphid magnet — keep them apart (crops near hops: " +
            std::to_string(hops_adj) + ").");
        say("  " + std::to_string(crops) + " crop" + (crops == 1 ? "" : "s") + " on the farm.");
        return out;
    }
    if (cmd == "ecology" || cmd == "foreststatus") {
        say("=== Ashgrove Ecology ===");
        say("  " + std::to_string(w.forest_tree_count) + " tree" +
            (w.forest_tree_count == 1 ? "" : "s") + " (" +
            std::to_string(w.forest_old_growth_count) + " old-growth), mean height " +
            std::to_string(static_cast<int>(std::round(w.forest_mean_height))) + " m");
        say("  Carbon stock: " + std::to_string(static_cast<int>(w.forest_carbon_stock)) +
            " kg C | succession index: " +
            std::to_string(static_cast<int>(w.forest_succession_index * 100.0f)) + "/100");
        say("  Seed dispersal: " + std::to_string(w.forest_seed_agent_count) +
            " agent" + (w.forest_seed_agent_count == 1 ? "" : "s") +
            " drifting | mycorrhizal network: " +
            std::to_string(static_cast<int>(w.forest_mean_mycorrhiza)) + "/255");
        return out;
    }
    if (cmd == "inventory" || cmd == "inv") {
        bool any = false;
        for (size_t i = 0; i < p.inv.size(); ++i)
            if (p.inv[i].item != Item::None) {
                any = true;
                say("  " + std::string(item_def(p.inv[i].item).name) +
                    (p.inv[i].count > 1 ? " x" + std::to_string(p.inv[i].count) : ""));
            }
        if (!any) say("You carry nothing.");
        // ROADMAP 1.4: at fractured sanity, a phantom item appears in the listing.
        if (w.perception_tier(p) >= 3) {
            std::mt19937 rng(p.id * 2833u + w.day * 17u);
            if ((rng() % 100u) < 25u) {
                const char* phantoms[] = {"?tarnished locket?", "?a key to no door?", "?a bone button?",
                                          "?a folded note in your own hand?"};
                say("  " + std::string(phantoms[rng() % 4]));
            }
        }
        return out;
    }
    if (cmd == "time") { say(clock_str(w)); return out; }
    if (cmd == "weather" || cmd == "forecast") {
        // ROADMAP 2.6 (8.2): regional synopsis from the atmosphere grid.
        w.init_atmosphere();
        say("━━━━ VALLEY WEATHER OFFICE ━━━━");
        say("  " + std::string(season_name(season_index(w.day))) + " " +
            std::to_string(season_day(w.day)) + " · Day " + std::to_string(w.day));
        int n_storm = 0, n_rain = 0, n_fog = 0, n_clear = 0, pmin = 127, pmax = -127, tsum = 0;
        for (int ay = 0; ay < World::ATMO_H; ++ay)
            for (int ax = 0; ax < World::ATMO_W; ++ax) {
                const int wx = ax * World::ATMO_CELL + 2, wy = ay * World::ATMO_CELL + 2;
                const int wd = w.weather_at(wx, wy);
                if (wd == 3) ++n_storm;
                else if (wd == 1) ++n_rain;
                else if (wd == 2) ++n_fog;
                else ++n_clear;
                const int pr = static_cast<int>(w.atmos_pressure[static_cast<size_t>(ay * World::ATMO_W + ax)]);
                pmin = std::min(pmin, pr);
                pmax = std::max(pmax, pr);
                tsum += w.temp_here(wx, wy);
            }
        const int wet = static_cast<int>(std::lround(
            100.0f * static_cast<float>(n_storm + n_rain) / static_cast<float>(World::ATMO_N)));
        say("  Regional: " + std::to_string(n_storm) + " stormy, " + std::to_string(n_rain) +
            " rainy, " + std::to_string(n_fog) + " foggy, " + std::to_string(n_clear) +
            " clear cells (" + std::to_string(wet) + "% wet)");
        say("  Pressure deviation " + std::to_string(pmin) + ".." + std::to_string(pmax) + " hPa");
        std::string map = "  [";
        for (int ay = 0; ay < World::ATMO_H; ++ay) {
            for (int ax = 0; ax < World::ATMO_W; ++ax) {
                const int wx = ax * World::ATMO_CELL + 2, wy = ay * World::ATMO_CELL + 2;
                const int wd = w.weather_at(wx, wy);
                map += (wd == 3) ? "!" : (wd == 1) ? "~" : (wd == 2) ? "=" : ".";
            }
            if (ay + 1 < World::ATMO_H) map += "\n   ";
        }
        map += "]";
        say(map);
        const int lw = w.weather_at(p.pos.x, p.pos.y);
        const char* lname = lw == 3 ? "severe storm" : lw == 1 ? "rain" : lw == 2 ? "fog" : "clear skies";
        const int lc = static_cast<int>(std::lround(static_cast<float>(w.temp_here(p.pos.x, p.pos.y)) * 40.0f / 255.0f));
        say("  Here (" + std::to_string(p.pos.x) + "," + std::to_string(p.pos.y) + "): " + lname +
            ", " + std::to_string(lc) + " C, wind " + std::to_string(w.wind_here(p.pos.x, p.pos.y)) + "/100");
        say("━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        return out;
    }
    if (cmd == "shop") {
        bool winter = season_index(w.day) == 3;
        say("Pierre's Seed Shop  (" + std::string(season_name(season_index(w.day))) + " prices):");
        say("  parsnip seeds      - 20g");
        say("  potato seeds       - 50g");
        say("  cauliflower seeds  - 80g");
        say("  corn seeds         - 150g (summer, fall)");
        say("  tomato seeds       - 50g  (summer)");
        say("  wheat seeds        - 10g  (spring..winter)");
        say("  blueberry seeds    - 80g  (spring..fall)");
        say("  bread              - 5g   (+30 energy)");
        if (winter) say("  (The ground is frozen. Seeds won't sprout until spring.)");
        say("(buy <item>)");
        return out;
    }

    // ---------- movement ----------
    static const std::unordered_map<std::string, int> dirs = {
        {"n", 3}, {"north", 3}, {"s", 0}, {"south", 0},
        {"e", 2}, {"east", 2}, {"w", 1}, {"west", 1},
    };
    if (cmd == "go" || cmd == "move" || dirs.count(cmd)) {
        std::string d = (cmd == "go" || cmd == "move") ? arg : cmd;
        
        // Check for "go to <landmark>" syntax (d is the raw arg, may start with "to ")
        if (d.rfind("to", 0) == 0 && !arg.empty()) {
            // Parse "go to <landmark>" - the arg contains "to <landmark>"
            std::string landmark = arg;
            size_t pos = landmark.find(' ');
            if (pos != std::string::npos) {
                landmark = landmark.substr(pos + 1);
            } else {
                landmark = "";
            }
            if (landmark.empty()) {
                say("Go to where? Try: go to town center / go to farmhouse / go to saloon");
                return out;
            }
            // Find the building by name (case-insensitive substring match)
            Bldg* target = nullptr;
            for (auto& b : w.buildings) {
                std::string bn = b.name;
                std::transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
                std::string ln = landmark;
                std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
                if (bn.find(ln) != std::string::npos) {
                    target = &b;
                    break;
                }
            }
            // Also check farmhouse
            if (!target && (landmark.find("farm") != std::string::npos || landmark.find("house") != std::string::npos)) {
                // Create a pseudo-building for farmhouse
                Vec2 door = w.door();
                std::vector<Vec2> path;
                if (bfs_path(w, p.pos, door, path)) {
                    p.path = std::move(path);
                    p.moving = !p.path.empty();
                    p.move_start_ms = static_cast<uint32_t>(now_ms());
                    int dx = door.x - p.pos.x, dy = door.y - p.pos.y;
                    p.dir = std::abs(dx) > std::abs(dy) ? (dx > 0 ? 2 : 1) : (dy > 0 ? 0 : 3);
                    // Register landmark
                    p.known_landmarks.insert("Farmhouse");
                    say("You start walking toward the Farmhouse...");
                    return out;
                } else {
                    say("There's no clear path to the Farmhouse.");
                    return out;
                }
            }
            if (!target) {
                // Check if it's a known landmark the player has visited
                if (p.known_landmarks.find(landmark) != p.known_landmarks.end()) {
                    say("You know of '" + landmark + "' but can't locate it from here. Try walking closer first.");
                } else {
                    say("Unknown landmark '" + landmark + "'. Visit a building first to learn its location.");
                }
                return out;
            }
            // Check if player has visited this building before (for fast-travel unlock)
            bool known = p.known_landmarks.find(target->name) != p.known_landmarks.end();
            // Destination is the building's doorstep
            Vec2 door = {int16_t(target->x + (target->w - 1) / 2), int16_t(target->y + target->h)};
            std::vector<Vec2> path;
            if (bfs_path(w, p.pos, door, path)) {
                p.path = std::move(path);
                p.moving = !p.path.empty();
                p.move_start_ms = static_cast<uint32_t>(now_ms());
                int dx = door.x - p.pos.x, dy = door.y - p.pos.y;
                p.dir = std::abs(dx) > std::abs(dy) ? (dx > 0 ? 2 : 1) : (dy > 0 ? 0 : 3);
                // Register landmark
                p.known_landmarks.insert(target->name);
                say("You start walking toward the " + target->name + "...");
                if (!known) say("(You commit the location to memory for future travels.)");
                return out;
            } else {
                say("There's no clear path to the " + target->name + ".");
                return out;
            }
        }
        
        // Check for coordinate-based movement: "go x,y" or "go x y"
        {
            std::string coord_arg = d;
            // Handle "go x,y" or "go x y" format
            std::replace(coord_arg.begin(), coord_arg.end(), ',', ' ');
            std::istringstream iss(coord_arg);
            int tx, ty;
            if (iss >> tx >> ty) {
                if (!w.in_bounds(tx, ty)) { say("Those coordinates are out of bounds."); return out; }
                Vec2 target_pos = {int16_t(tx), int16_t(ty)};
                // If target is not walkable, find nearest walkable tile
                if (!w.walkable(target_pos)) {
                    bool found = false;
                    for (int radius = 1; radius <= 5 && !found; ++radius) {
                        for (int dx = -radius; dx <= radius && !found; ++dx) {
                            for (int dy = -radius; dy <= radius && !found; ++dy) {
                                int nx = tx + dx, ny = ty + dy;
                                if (w.in_bounds(nx, ny) && w.walkable(nx, ny)) {
                                    target_pos = {int16_t(nx), int16_t(ny)};
                                    found = true;
                                    if (nx != tx || ny != ty) {
                                        say("Target blocked; walking to nearest clear spot (" + std::to_string(nx) + ", " + std::to_string(ny) + ").");
                                    }
                                }
                            }
                        }
                    }
                    if (!found) { say("No walkable spot near those coordinates."); return out; }
                }
                std::vector<Vec2> path;
                if (bfs_path(w, p.pos, target_pos, path)) {
                    p.path = std::move(path);
                    p.moving = !p.path.empty();
                    p.move_start_ms = static_cast<uint32_t>(now_ms());
                    int dx = target_pos.x - p.pos.x, dy = target_pos.y - p.pos.y;
                    p.dir = std::abs(dx) > std::abs(dy) ? (dx > 0 ? 2 : 1) : (dy > 0 ? 0 : 3);
                    say("You start walking toward (" + std::to_string(target_pos.x) + ", " + std::to_string(target_pos.y) + ")...");
                    return out;
                } else {
                    say("No path to those coordinates. Terrain may be blocking the way.");
                    return out;
                }
            }
        }
        
        // Check for "go <dir> <count>" syntax (multi-tile walk)
        std::string dir_arg = d;
        int count = 1;
        size_t space_pos = dir_arg.find(' ');
        if (space_pos != std::string::npos) {
            std::string count_str = dir_arg.substr(space_pos + 1);
            dir_arg = dir_arg.substr(0, space_pos);
            try {
                count = std::stoi(count_str);
                if (count < 1) count = 1;
                if (count > 50) count = 50; // cap at 50 tiles
            } catch (...) {
                count = 1;
            }
        }
        
        auto it = dirs.find(dir_arg);
        if (it == dirs.end()) { say("Go where? Try: go north / go south / go east / go west / go 5 north / go to town center"); return out; }
        int16_t dx = 0, dy = 0;
        if (it->second == 3) dy = -1; else if (it->second == 0) dy = 1;
        else if (it->second == 2) dx = 1; else dx = -1;
        p.dir = static_cast<uint8_t>(it->second);
        static const char* dn[] = {"south", "west", "east", "north"};

        // ---------- interior movement ----------
        if (!p.inside.empty()) {
            auto rit = w.interiors.find(p.inside);
            if (rit == w.interiors.end()) { p.inside.clear(); return out; }
            const InteriorRoom& r = rit->second;
            int walked = 0;
            for (int step = 0; step < count; ++step) {
                int nx = p.inx + dx, ny = p.iny + dy;
                if (ny >= r.h - 1) {                       // walk out the door
                    say("You step out the door.");
                    p.inside.clear();
                    p.pos = p.inside_exit;
                    p.target = p.pos;
                    p.path.clear();
                    p.moving = false;
                    return out;
                }
                if (nx < 0 || nx >= r.w || ny < 0 || r.rows[static_cast<size_t>(ny)][static_cast<size_t>(nx)] == '#') {
                    if (walked > 0) say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
                    say("A wall stops you."); return out;
                }
                char ch = r.rows[static_cast<size_t>(ny)][static_cast<size_t>(nx)];
                if (ch != '.' && ch != ' ' && ch != 'P') {
                    if (walked > 0) say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
                    say("Blocked by " + furniture_name(ch) + "."); return out;
                }
                p.inx = static_cast<int16_t>(nx);
                p.iny = static_cast<int16_t>(ny);
                walked++;
            }
            if (walked > 0) say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
            return out;
        }

        // Multi-tile walk on overworld
        // Clear any active pathfinding movement to avoid conflicts
        p.path.clear();
        p.moving = false;
        int walked = 0;
        for (int step = 0; step < count; ++step) {
            Vec2 next = p.pos;
            next.x += dx; next.y += dy;
            if (!w.in_bounds(next)) {
                if (walked > 0) say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
                say("The edge of the world. You can't go that way."); return out;
            }
            if (!w.walkable(next)) {
                if (walked > 0) say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
                Cell& c = w.at(next);
                say("Blocked by " + std::string(obj_name(c.obj.type) ? obj_name(c.obj.type) : terrain_name(c.tile)) + ".");
                return out;
            }
            p.pos = next;
            walked++;
            // L5: Snow compaction from foot traffic
            Cell& stepped = w.at(next);
            if ((stepped.tile == Tile::Snow || stepped.tile == Tile::Ice) && season == 3) {
                // Foot traffic increases compaction
                if (stepped.snow_compaction < 245) stepped.snow_compaction += 10;
                else stepped.snow_compaction = 255;
                if (stepped.tile == Tile::Snow && stepped.snow_compaction > 200) {
                    stepped.tile = Tile::Ice; // packed snow becomes ice
                    stepped.snow_compaction = 255;
                }
                // Energy cost based on compaction: fluffy=2, packed=1.5, ice=1 (slippery)
                float cost_mult = 1.0f;
                if (stepped.tile == Tile::Ice) cost_mult = 1.0f; // slippery, easier
                else if (stepped.snow_compaction > 200) cost_mult = 1.5f; // packed
                else cost_mult = 2.0f; // fluffy
                p.energy = std::max(0.0f, p.energy - cost_mult);
                // L9: Create track
                stepped.track_age = 1; // fresh
                stepped.track_type = 1; // player
                // Direction: 0=S, 1=W, 2=E, 3=N
                stepped.track_dir = static_cast<int8_t>(it->second);
            } else {
                p.energy = std::max(0.0f, p.energy - 1.0f);
            }
            // Check if we entered farmhouse area
            if (w.in_house(next.x, next.y)) {
                say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
                say("You are inside your farmhouse. The door is to the south.");
                return out;
            }
        }
        if (walked > 0) say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
        return out;
    }

    // ---------- explore <dir> (auto-walk until landmark/obstacle) ----------
    if (cmd == "explore") {
        if (arg.empty()) { say("Explore where? Try: explore north / explore east / explore south / explore west"); return out; }
        auto it = dirs.find(arg);
        if (it == dirs.end()) { say("Explore where? Try: explore north / explore east / explore south / explore west"); return out; }
        int16_t dx = 0, dy = 0;
        if (it->second == 3) dy = -1; else if (it->second == 0) dy = 1;
        else if (it->second == 2) dx = 1; else dx = -1;
        p.dir = static_cast<uint8_t>(it->second);
        
        // Clear any active pathfinding
        p.path.clear();
        p.moving = false;
        
        static const char* dn[] = {"south", "west", "east", "north"};
        int walked = 0;
        
        say("You start exploring " + std::string(it->first) + "... (type any command to stop)");
        // Auto-walk loop: move until landmark, obstacle, low energy, or player interrupts
        for (int step = 0; step < 200; ++step) { // max 200 tiles
            Vec2 next = p.pos;
            next.x += dx; next.y += dy;
            if (!w.in_bounds(next)) {
                say("You reach the edge of the world."); break;
            }
            if (!w.walkable(next)) {
                Cell& c = w.at(next);
                say("Blocked by " + std::string(obj_name(c.obj.type) ? obj_name(c.obj.type) : terrain_name(c.tile)) + ".");
                break;
            }
            p.pos = next;
            // L5: Snow compaction
            Cell& stepped = w.at(next);
            if ((stepped.tile == Tile::Snow || stepped.tile == Tile::Ice) && season == 3) {
                if (stepped.snow_compaction < 245) stepped.snow_compaction += 10;
                else stepped.snow_compaction = 255;
                if (stepped.tile == Tile::Snow && stepped.snow_compaction > 200) {
                    stepped.tile = Tile::Ice;
                    stepped.snow_compaction = 255;
                }
                float cost_mult = 1.0f;
                if (stepped.tile == Tile::Ice) cost_mult = 1.0f;
                else if (stepped.snow_compaction > 200) cost_mult = 1.5f;
                else cost_mult = 2.0f;
                p.energy = std::max(0.0f, p.energy - cost_mult);
                stepped.track_age = 1; stepped.track_type = 1; stepped.track_dir = static_cast<int8_t>(it->second);
            } else {
                p.energy = std::max(0.0f, p.energy - 1.0f);
            }
            // Check for landmark (building, region boundary, resource)
            std::string landmark = "";
            for (auto& b : w.buildings) {
                if (p.pos.x >= b.x && p.pos.x < b.x + b.w && p.pos.y >= b.y && p.pos.y < b.y + b.h) {
                    landmark = b.name; break;
                }
            }
            if (w.in_house(p.pos.x, p.pos.y)) landmark = "Farmhouse";
            std::string region = region_at(w, p.pos.x, p.pos.y);
            if (region != "Ashgrove Valley" && region != "Ashgrove Farm") landmark = region;
            // Check for resource nodes
            Cell& here = w.at(p.pos);
            if (here.obj.type != ObjType::None && (is_tree(here.obj.type) || here.obj.type == ObjType::Rock || here.obj.type == ObjType::Bush || here.obj.type == ObjType::Mushroom)) {
                landmark = std::string(obj_name(here.obj.type));
            }
            if (!landmark.empty()) {
                say("You discover: " + landmark + "!");
                break;
            }
            // Low energy check
            if (p.energy < p.max_energy * 0.2f) {
                say("You're too exhausted to continue exploring.");
                break;
            }
        }
        if (walked > 0) say("You explored " + std::to_string(walked) + " tiles " + std::string(dn[p.dir]) + ".");
        return out;
    }

    // ---------- fasttravel <name> (teleport to known landmark) ----------
    if (cmd == "fasttravel" || cmd == "travel") {
        if (arg.empty()) { say("Fast travel where? Try: fasttravel farmhouse / fasttravel town center / fasttravel carpenter shop"); return out; }
        if (!p.inside.empty()) { say("You can't fast travel from inside a building. Exit first."); return out; }
        if (p.energy < p.max_energy * 0.1f) { say("You're too exhausted to travel."); return out; }
        // Sanity check (L7)
        if (w.perception_tier(p) >= 3) { say("Your mind is too fractured to navigate safely."); return out; }
        // Encumbrance check
        int total_weight = 0;
        for (auto& s : p.inv) total_weight += s.count;
        if (total_weight > 80) { say("You're too encumbered to travel quickly."); return out; }
        // Festival check
        if (is_festival_day(w.day)) { say("The roads are too crowded for fast travel today."); return out; }
        
        std::string landmark = arg;
        std::transform(landmark.begin(), landmark.end(), landmark.begin(), ::tolower);
        
        // Find target building
        Bldg* target = nullptr;
        for (auto& b : w.buildings) {
            std::string bn = b.name;
            std::transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
            if (bn.find(landmark) != std::string::npos) { target = &b; break; }
        }
        if (!target && (landmark.find("farm") != std::string::npos || landmark.find("house") != std::string::npos)) {
            target = nullptr; // Farmhouse handled separately
        }
        
        Vec2 destination;
        std::string dest_name;
        if (target) {
            destination = {int16_t(target->x + (target->w - 1) / 2), int16_t(target->y + target->h)};
            dest_name = target->name;
        } else if (landmark.find("farm") != std::string::npos || landmark.find("house") != std::string::npos) {
            destination = w.door();
            dest_name = "Farmhouse";
        } else {
            say("Unknown destination '" + arg + "'. Visit a landmark first to unlock fast travel.");
            return out;
        }
        
        // Check if player has visited this landmark
        if (p.known_landmarks.find(dest_name) == p.known_landmarks.end()) {
            say("You haven't visited " + dest_name + " yet. Walk there first to unlock fast travel.");
            return out;
        }
        
        // Calculate travel cost
        int dx = std::abs(destination.x - p.pos.x);
        int dy = std::abs(destination.y - p.pos.y);
        int dist_chunks = (std::max(dx, dy) + 63) / 64; // CHUNK_SIZE = 128, so 2 tiles per chunk roughly
        float base_hours = static_cast<float>(dist_chunks) * 0.5f;
        
        // Terrain modifier
        std::string region = region_at(w, p.pos.x, p.pos.y);
        std::string dest_region = region_at(w, destination.x, destination.y);
        float terrain_mod = 0.0f;
        if (region == "Whisper Wood" || dest_region == "Whisper Wood") terrain_mod += 0.5f;
        if (region == "Frostveil Tundra" || dest_region == "Frostveil Tundra") terrain_mod += 1.0f;
        if (region == "Mulberry Lane" || dest_region == "Mulberry Lane") terrain_mod -= 0.5f;
        float total_hours = std::max(0.5f, base_hours + terrain_mod);
        
        // Apply costs
        int energy_cost = static_cast<int>(static_cast<float>(p.max_energy) * 0.1f); // 10% energy
        float sanity_cost = 5.0f; // sanity
        
        if (p.energy < static_cast<float>(energy_cost)) { say("Not enough energy for the journey."); return out; }
        if (w.perception_tier(p) >= 3) { say("Your mind is too fractured for safe travel."); return out; }
        
        p.energy -= static_cast<float>(energy_cost);
        w.damage_sanity(p, sanity_cost);
        
        // Advance time
        int hours_to_add = static_cast<int>(total_hours);
        w.day_seconds += static_cast<float>(hours_to_add) * 3600.0f; // 3600 seconds per hour (simplified)
        while (w.day_seconds >= 86400) { // 86400 = DAY_LENGTH_S * 24? Actually DAY_LENGTH_S = 800s = 20 game hours
            w.day_seconds -= 86400;
            advance_day(w);
        }
        
        // Teleport
        p.pos = destination;
        p.target = destination;
        p.path.clear();
        p.moving = false;
        p.known_landmarks.insert(dest_name);
        
        say("You make your way to " + dest_name + ", arriving " + 
            (total_hours < 1 ? "shortly" : (total_hours < 6 ? "by mid-morning" : total_hours < 12 ? "by afternoon" : "by evening")) + 
            ". The journey took " + std::to_string(static_cast<int>(total_hours)) + " hours.");
        say("Energy -" + std::to_string(energy_cost) + " | Sanity -" + std::to_string(static_cast<int>(sanity_cost)) + " | Time advanced " + std::to_string(hours_to_add) + " hours.");
        return out;
    }

    // ---------- look ----------
    if (cmd == "look" || cmd == "l") {
        if (!p.inside.empty()) {
            auto rit = w.interiors.find(p.inside);
            if (rit == w.interiors.end()) { p.inside.clear(); return out; }
            const InteriorRoom& r = rit->second;
            say("You are inside the " + p.inside + ".");
            char here = r.rows[static_cast<size_t>(p.iny)][static_cast<size_t>(p.inx)];
            if (here != '.' && here != ' ' && here != 'P')
                say("You stand beside " + furniture_name(here) + ".");
            std::vector<std::string> around;
            for (auto [ox, oy, di] : std::vector<std::tuple<int,int,const char*>>{{0,-1,"north"},{0,1,"south"},{-1,0,"west"},{1,0,"east"}}) {
                int tx = p.inx + ox, ty = p.iny + oy;
                if (tx < 0 || tx >= r.w || ty < 0 || ty >= r.h) continue;
                char c = r.rows[static_cast<size_t>(ty)][static_cast<size_t>(tx)];
                if (c == '#') around.push_back("a wall to the " + std::string(di));
                else if (c != '.' && c != ' ' && c != 'P')
                    around.push_back(furniture_name(c) + " to the " + std::string(di));
            }
            if (!around.empty()) {
                say("Around you: " + around[0]);
                for (size_t i = 1; i < around.size(); ++i) say("            " + around[i]);
            }
            if (p.inside == "Farmhouse")
                say("The door is to the south. Your bed is here — 'sleep' ends the day.");
            else {
                say("The door is to the south. 'exit' to leave.");
                if (p.inside == "General Store" || p.inside == "Market")
                    say("Pierre nods from behind the counter. 'shop' and 'buy <item>'.");
                else if (p.inside == "Clinic") say("Doc Harvey polishes his stethoscope. 'interact' for treatment (20g).");
                else if (p.inside == "Stardrop Saloon") say("Gus is polishing glasses. 'interact' buys a hot meal (15g).");
                else if (p.inside == "Museum") say("Dusty glass cases hold strange relics. 'interact' to browse.");
                else if (p.inside == "Blacksmith") say("Clint grumbles over his anvil: 'Bring me ore someday...'.");
                else if (p.inside == "Old Mill") say("The millstones grind. Flour dust hangs in the air.");
                else if (p.inside == "Town Center") say("A bulletin board lists this week's events. 'interact' to read it.");
                else if (p.inside == "Railway Station") say("The timetable flutters. 'train' to ride the Zuzu City Express.");
                else if (p.inside == "Bus Stop") say("The shelter bench is cold. 'bus' to take the town bus downtown.");
                else if (p.inside == "Hawthorn Barn") say("Cows low softly in their stalls. Hay smells sweet.");
                else if (p.inside == "Glasshouse") say("Warm, green air. Rows of planters soak up the sun.");
            }
            return out;
        }
        Cell& c = w.at(p.pos);
        int season_local = season_index(w.day);
        int hour = hour_of_day(w);
        const char* part = hour < 9 ? "early morning" : hour < 12 ? "morning" :
                           hour < 17 ? "afternoon" : hour < 21 ? "evening" : "night";
        say("You stand on " + std::string(terrain_name(c.tile)) + " in " +
            std::string(region_at(w, p.pos.x, p.pos.y)) + ".");
        say("It's a " + std::string(weather_name_adapted(w, w.day)) + " " +
            std::string(season_name(season_local)) + " " + std::string(part) + ".");
        // R9.3: Foggy weather reduces visibility
        bool foggy = (w.weather_of_day_adapted(w.day) == 2);
        if (foggy) say("A thick fog clings to the valley. You can barely see a few feet ahead.");
        if (c.tile == Tile::Grass || c.tile == Tile::GrassVar)
            say("Wild grass rustles in the breeze.");
        if (c.tile == Tile::Tilled) say("Freshly tilled soil.");
        if (c.crop.is_crop())
            say("Here grow: " + std::string(stage_desc(c.crop.crop, c.crop.stage)) +
                (c.crop.days_left > 0 ? " (" + std::to_string(c.crop.days_left) + " days left)" : "") +
                (c.crop.watered ? " · watered" : ""));
        if (c.obj.type != ObjType::None)
            say("Also here: " + std::string(obj_name(c.obj.type)) +
                (c.obj.hp > 1 ? " (sturdy, needs " + std::to_string(c.obj.hp) + " more hits)" : ""));
        if (w.in_house(p.pos.x, p.pos.y)) say("Your farmhouse roof is overhead.");
        Vec2 d = w.door();
        if (std::abs(int(d.x) - p.pos.x) + std::abs(int(d.y) - p.pos.y) <= 2)
            say("The house door is to the " +
                std::string(d.y > p.pos.y ? "south" : d.y < p.pos.y ? "north" : d.x > p.pos.x ? "east" : "west") +
                ". 'sleep' to end the day.");
        // region flavor
        std::string reg = region_at(w, p.pos.x, p.pos.y);
        bool in_bldg = false;
        for (auto& b : w.buildings)
            if (p.pos.x >= b.x && p.pos.x < b.x + b.w && p.pos.y >= b.y && p.pos.y < b.y + b.h)
                in_bldg = true;
        if (in_bldg) say("You're standing against the wall of " + reg + ".");
        else if (reg == "Willow River") say("The river murmurs past. Fishing should be good here.");
        else if (reg == "Mirror Lake") say("Mirror Lake lies glassy and still, ringed by reeds and berry bushes.");
        else if (reg == "Seaglass Shore") say("The ocean breath is salty. Shorebirds skitter over the wet sand.");
        else if (reg == "Frozen Lake") say("Blue ice glitters beneath your boots. The lake is frozen solid.");
        else if (reg == "Frostveil Tundra") say("Snow crunches underfoot. Pines lean in close against the cold.");
        else if (reg == "Ashgrove Farm") say("Your fields lie around you, fenced and waiting.");
        else if (reg == "Stardrop Plaza") say("Flagstones underfoot. The village gathers here.");
        else if (reg == "Mulberry Lane") say("The village high street. Dusty wagons, old lanterns.");
        else if (reg == "Whisper Wood") say("Old-growth forest crowds the stream. Forage thrives in the shade.");
        else if (reg == "East Moor") say("Open meadowland. Rabbits dart between the grasses.");
        // Phase 6: perception filters at low sanity
        int ptier = w.perception_tier(p);
        if (ptier >= 1) { std::string f = w.horror_flavor(p); if (!f.empty()) say(f); }
        // ROADMAP 1.2: the corrupted ground itself leaks the Valley's presence
        // into the air — felt even by a sane mind, strongest near the anchors.
        {
            uint8_t cor = w.at(p.pos).corruption;
            if (cor >= 200) say("The air here coils around you, thick and wrong, breath held too long.");
            else if (cor >= 128) say("Something clings to this ground. You taste it on your tongue.");
            else if (cor >= 64) say("The light falls differently here, heavy and slow.");
        }
        // ROADMAP 1.4: hallucinated scene descriptions at low sanity (tagged (?)).
        {
            std::string hs = w.hallucinate_scene(p);
            if (!hs.empty()) say(hs);
        }
        if (ptier >= 2 || w.horror_phantom_sighting_chance > 0.0f) {
            // Distorted vision: a "false" neighbor the player thinks they see.
            // Phase 7.3: horror.phantom_sighting_chance raises sighting odds even at lower tiers.
            bool phantom_seen = (ptier >= 2);
            if (!phantom_seen && w.horror_phantom_sighting_chance > 0.0f) {
                std::mt19937 prng(p.id * 1579u + w.day);
                unsigned pc = static_cast<unsigned>(w.horror_phantom_sighting_chance * 100.0f);
                phantom_seen = (prng() % 100u) < pc;
            }
            if (phantom_seen) {
                static const char* phantom[] = {"a figure standing at the treeline", "a face at a dark window",
                                                "the scarecrow closer than before", "a shape that is not there"};
                std::mt19937 hrng(p.id * 977u + w.day);
                say("You think you see " + std::string(phantom[hrng() % 4]) + ". It is gone when you look again.");
                w.bump_dread(p, 0);  // phantoms -> shadows theme
            }
        }
        if (ptier >= 3) { std::string v = w.internal_voice(p); if (!v.empty()) say(v); }
        // notable neighbors
        std::vector<std::string> near;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = p.pos.x + dx, ny = p.pos.y + dy;
                if (!w.in_bounds(nx, ny) || w.walkable(nx, ny)) continue;
                ObjType o = w.at(nx, ny).obj.type;
                if (o != ObjType::None)
                    near.push_back(std::string(obj_name(o)) + " to the " +
                        std::string(dy == -1 ? "north" : dy == 1 ? "south" : dx == -1 ? "west" : "east"));
                else if (is_water_any(w.at(nx, ny).tile))
                    near.push_back("water to the " +
                        std::string(dy == -1 ? "north" : dy == 1 ? "south" : dx == -1 ? "west" : "east"));
            }
        if (!near.empty()) {
            say("Nearby: " + near[0]);
            for (size_t i = 1; i < near.size(); ++i) say("         " + near[i]);
        }
        // NPCs in earshot - limited in fog
        std::vector<std::string> folks;
        for (auto& n : w.npcs) {
            int dist = std::abs(int(n.pos.x) - p.pos.x) + std::abs(int(n.pos.y) - p.pos.y);
            if (foggy) {
                if (dist <= 1) // Only adjacent in fog
                    folks.push_back(n.name + " (right here)");
            } else if (dist <= 3) {
                folks.push_back(n.name + (dist <= 1 ? " (right here)" :
                    " to the " + std::string(n.pos.y < p.pos.y ? "north" : n.pos.y > p.pos.y ? "south" :
                                                n.pos.x > p.pos.x ? "east" : "west")));
            }
        }
        for (auto& f : folks) say("You see " + f + ". Try 'talk " + f.substr(0, f.find(' ')) + "'.");
        return out;
    }

    // ---------- tool actions (act on facing cell) ----------
    auto grab_tool = [&](Item tool) -> bool {
        int slot = find_slot(p, tool);
        if (slot < 0) { say("You don't have a " + std::string(item_def(tool).name) + "."); return false; }
        p.sel = static_cast<uint8_t>(slot);
        return true;
    };

    if (cmd == "hoe" || cmd == "till") {
        if (!grab_tool(Item::Hoe)) return out;
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("Nothing to hoe."); return out; }
        std::string m = act_tool(w, p, f.x, f.y);
        if (m == "Exhausted") say("Too tired. Rest or sleep.");
        else if (m == "Can't hoe here") say("This ground can't be tilled.");
        else say("You till the soil with your hoe.");
        return out;
    }
    if (cmd == "water") {
        if (!grab_tool(Item::WateringCan)) return out;
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("Nothing to water."); return out; }
        if (is_water_any(w.at(f).tile)) { p.inv[p.sel].count = 40; say("You refill your watering can."); return out; }
        std::string m = act_tool(w, p, f.x, f.y);
        if (m == "Can is empty") say("Your watering can is empty. Stand by water and use 'water' to refill.");
        else if (m == "Water the soil") say("There's nothing to water there.");
        else if (m == "Exhausted") say("Too tired. Rest or sleep.");
        else say("You water the soil.");
        return out;
    }
    if (cmd == "well") {
        Vec2 f = facing_cell(p);
        Cell* c = nullptr;
        if (w.in_bounds(p.pos) && w.at(p.pos).obj.type == ObjType::Well) {
            c = &w.at(p.pos);
        } else if (w.in_bounds(f) && w.at(f).obj.type == ObjType::Well) {
            c = &w.at(f);
        }
        if (!c) { say("There's no well here. Stand on or face a well and try again."); return out; }
        int water_level = c->obj.hp;
        int wt_depth = c->water_table_depth;
        std::string status = "Well water: " + std::to_string(water_level) + "/100. ";
        if (water_level == 0) status += "DRY.";
        else if (water_level < 20) status += "LOW.";
        else if (water_level < 50) status += "Fair.";
        else status += "Good.";
        status += " Water table depth: " + std::to_string(wt_depth) + "cm (saturation: " + std::to_string(c->saturation) + ")";
        if (wt_depth > 200) status += " — AQUIFER DEPLETED";
        else if (wt_depth > 150) status += " — Deep";
        else if (wt_depth > 100) status += " — Moderate";
        else status += " — Shallow";
        say(status);
        return out;
    }
    if (cmd == "axe" || cmd == "chop") {
        if (!grab_tool(Item::Axe)) return out;
        Vec2 f = facing_cell(p);
        auto try_chop = [&](int x, int y) -> std::string {
            if (!w.in_bounds(x, y)) return "";
            Cell& cell = w.at(x, y);
            if (cell.obj.type != ObjType::Tree &&
                cell.obj.type != ObjType::Stump &&
                cell.obj.type != ObjType::Pine) {
                return "";
            }
            for (int attempts = 0; attempts < 10; ++attempts) {
                std::string m = act_tool(w, p, x, y);
                if (m == "Exhausted") return "Exhausted";
                if (!m.empty()) return m;
                cell = w.at(x, y);
                if (cell.obj.type != ObjType::Tree &&
                    cell.obj.type != ObjType::Stump &&
                    cell.obj.type != ObjType::Pine) {
                    break;
                }
            }
            return "";
        };
        std::string m = try_chop(f.x, f.y);
        if (m == "Exhausted") { say("Too tired. Rest or sleep."); return out; }
        if (!m.empty()) { say("You swing your axe. " + m + "."); return out; }
        // Also check current cell if facing cell has no tree
        m = try_chop(p.pos.x, p.pos.y);
        if (m == "Exhausted") { say("Too tired. Rest or sleep."); return out; }
        if (!m.empty()) { say("You swing your axe. " + m + "."); return out; }
        say("Nothing to chop here.");
        return out;
    }
    if (cmd == "pick" || cmd == "mine" || cmd == "pickaxe") {
        if (!grab_tool(Item::Pickaxe)) return out;
        Vec2 f = facing_cell(p);
        auto try_mine = [&](int x, int y) -> std::string {
            if (!w.in_bounds(x, y)) return "";
            Cell& cell = w.at(x, y);
            if (cell.obj.type != ObjType::Rock) return "";
            for (int attempts = 0; attempts < 10; ++attempts) {
                std::string m = act_tool(w, p, x, y);
                if (m == "Exhausted") return "Exhausted";
                if (!m.empty()) return m;
                cell = w.at(x, y);
                if (cell.obj.type != ObjType::Rock) break;
            }
            return "";
        };
        std::string m = try_mine(f.x, f.y);
        if (m == "Exhausted") { say("Too tired. Rest or sleep."); return out; }
        if (!m.empty()) { say("You strike the rock. " + m + "."); return out; }
        // Also check current cell if facing cell has no rock
        m = try_mine(p.pos.x, p.pos.y);
        if (m == "Exhausted") { say("Too tired. Rest or sleep."); return out; }
        if (!m.empty()) { say("You strike the rock. " + m + "."); return out; }
        say("Nothing to mine here.");
        return out;
    }
    if (cmd == "scythe" || cmd == "cut") {
        if (!grab_tool(Item::Scythe)) return out;
        Vec2 f = facing_cell(p);
        auto try_cut = [&](int x, int y) -> std::string {
            if (!w.in_bounds(x, y)) return "";
            Cell& cell = w.at(x, y);
            if (cell.obj.type != ObjType::Weed &&
                cell.obj.type != ObjType::TallGrass &&
                cell.obj.type != ObjType::Mushroom) {
                return "";
            }
            std::string m = act_tool(w, p, x, y);
            if (m == "Exhausted") return "Exhausted";
            return m;
        };
        std::string m = try_cut(f.x, f.y);
        if (m == "Exhausted") { say("Too tired. Rest or sleep."); return out; }
        if (!m.empty()) { say("You sweep your scythe. " + m + "."); return out; }
        // Also check current cell if facing cell has nothing to cut
        m = try_cut(p.pos.x, p.pos.y);
        if (m == "Exhausted") { say("Too tired. Rest or sleep."); return out; }
        if (!m.empty()) { say("You sweep your scythe. " + m + "."); return out; }
        say("Nothing to cut here.");
        return out;
    }

    // ---------- planting ----------
    if (cmd == "plant" || cmd == "planting") {
        const CropDef* crop = crop_def(arg.c_str());
        if (!crop) { say("Plant what? parsnip, potato, cauliflower, corn, tomato, wheat, blueberry, green bean, hops, strawberry, melon, pumpkin, red cabbage, rhubarb, garlic, artichoke, bok choy, kale, cranberry, grape, apple, cherry, peach, pomegranate, apricot, orange, banana, mango, plum, pear, fig, avocado, lemon, lime, grapefruit, persimmon."); return out; }
        int season_local = season_index(w.day);
        if (season_local == 3) { say("The soil is frozen solid. Nothing grows in winter."); return out; }
        if (season_local < crop->min_season || season_local > crop->max_season) {
            say(std::string(crop->name) + " grows best in " +
                std::string(season_name(crop->min_season)) + " through " +
                std::string(season_name(crop->max_season)) + ". It won't thrive now.");
            return out;
        }
        int slot = find_slot(p, crop->seed);
        if (slot < 0) { say("You don't have any " + std::string(item_def(crop->seed).name) + "."); return out; }
        p.sel = static_cast<uint8_t>(slot);
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("Nothing to plant on."); return out; }
        Cell& c = w.at(f);
        if (c.tile != Tile::Tilled) { say("Till the soil first with the hoe."); return out; }
        if (c.crop.is_crop()) { say("Something is already growing there."); return out; }
        if (p.energy < 1) { say("Too tired. Rest or sleep."); return out; }
        p.energy -= 1;
        consume_item(p, crop->seed, 1);
        c.crop.crop = crop->produce;
        c.crop.stage = 0;
        c.crop.days_left = static_cast<int8_t>(crop->days);
        c.crop.watered = false;
        c.crop.is_trellis = false;
        c.crop.is_fruit_tree = false;
        c.crop.last_harvest_season = -1;
        // ROADMAP 2.3 — inherit genetics from saved/bred seed (else reference).
        {
            SeedGen sg = get_seed_gen(p, crop->seed);
            c.crop.alleles = sg.alleles;
            c.crop.homozygosity = sg.homozygosity;
            c.crop.giant_crop_counter = 0;
            c.crop.is_giant = false;
            c.crop.height = c.crop.biomass = c.crop.root_depth = c.crop.canopy_width = 0.0f;
        }
        if (crop->produce == Item::GreenBean || crop->produce == Item::Hops) {
            c.crop.is_trellis = true;
        }
        if (crop->produce == Item::Apple || crop->produce == Item::Cherry ||
            crop->produce == Item::Peach || crop->produce == Item::Pomegranate ||
            crop->produce == Item::Apricot || crop->produce == Item::Orange ||
            crop->produce == Item::Banana || crop->produce == Item::Mango ||
            crop->produce == Item::Plum || crop->produce == Item::Pear ||
            crop->produce == Item::Fig || crop->produce == Item::Avocado ||
            crop->produce == Item::Lemon || crop->produce == Item::Lime ||
            crop->produce == Item::Grapefruit || crop->produce == Item::Persimmon) {
            c.crop.is_fruit_tree = true;
        }
        // Moon phase bonus: new moon (phase 0) = 10% faster growth
        if (w.day % 8 == 0) {
            c.obj.hp = 1; // mark for moon bonus
            say("You plant " + std::string(crop->name) + " seeds. (" +
                std::to_string(crop->days) + " days to harvest) — New Moon blessing!");
        } else {
            say("You plant " + std::string(crop->name) + " seeds. (" +
                std::to_string(crop->days) + " days to harvest)");
        }
        return out;
    }

    // ---------- apply fertilizer ----------
    if (cmd == "fertilize" || cmd == "fertilizer") {
        std::string fert_name = lower_trim(arg);
        Item fert = Item::None;
        // ROADMAP 2.1 (8.1a) — N/P/K specific fertilizers
        if (fert_name == "basic") fert = Item::FertilizerBasic;
        else if (fert_name == "quality") fert = Item::FertilizerQuality;
        else if (fert_name == "premium") fert = Item::FertilizerPremium;
        else if (fert_name == "nitrogen" || fert_name == "n") fert = Item::FertilizerNitrogen;
        else if (fert_name == "phosphorus" || fert_name == "p") fert = Item::FertilizerPhosphorus;
        else if (fert_name == "potassium" || fert_name == "k") fert = Item::FertilizerPotassium;
        else if (fert_name == "balanced" || fert_name == "10-10-10") fert = Item::FertilizerBalanced;
        else if (fert_name == "organic" || fert_name == "compost") fert = Item::FertilizerOrganic;
        else if (fert_name == "lime") fert = Item::SoilLime;
        else if (fert_name == "sulfur" || fert_name == "sulphur") fert = Item::SoilSulfur;
        else if (fert_name == "gypsum") fert = Item::SoilGypsum;
        else {
            say("Apply which fertilizer? 'fertilize nitrogen', 'fertilize phosphorus', 'fertilize potassium', 'fertilize balanced', 'fertilize organic', 'fertilize lime', 'fertilize sulfur', 'fertilize gypsum', or legacy 'fertilize basic/quality/premium'.");
            return out;
        }
        int slot = find_slot(p, fert);
        if (slot < 0) { say("You don't have any " + std::string(item_def(fert).name) + "."); return out; }
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("Nothing to fertilize."); return out; }
        Cell& c = w.at(f);
        if (c.tile != Tile::Tilled) { say("Fertilize tilled soil only."); return out; }
        if (c.crop.is_crop()) { say("There is already a crop growing there."); return out; }
        if (p.energy < 1) { say("Too tired. Rest or sleep."); return out; }
        p.energy -= 1;
        consume_item(p, fert, 1);
        // ROADMAP 2.1 (8.1a) — Apply specific nutrients to soil
        switch (fert) {
            case Item::FertilizerNitrogen:
                c.nitrogen = static_cast<uint8_t>(std::min<int>(c.nitrogen + 40, 255));
                say("Applied Nitrogen Fertilizer. Soil nitrogen increased.");
                break;
            case Item::FertilizerPhosphorus:
                c.phosphorus = static_cast<uint8_t>(std::min<int>(c.phosphorus + 40, 255));
                say("Applied Phosphorus Fertilizer. Soil phosphorus increased.");
                break;
            case Item::FertilizerPotassium:
                c.potassium = static_cast<uint8_t>(std::min<int>(c.potassium + 40, 255));
                say("Applied Potassium Fertilizer. Soil potassium increased.");
                break;
            case Item::FertilizerBalanced:
                c.nitrogen = static_cast<uint8_t>(std::min<int>(c.nitrogen + 25, 255));
                c.phosphorus = static_cast<uint8_t>(std::min<int>(c.phosphorus + 25, 255));
                c.potassium = static_cast<uint8_t>(std::min<int>(c.potassium + 25, 255));
                say("Applied Balanced Fertilizer (10-10-10). Soil NPK increased.");
                break;
            case Item::FertilizerOrganic:
                c.nitrogen = static_cast<uint8_t>(std::min<int>(c.nitrogen + 15, 255));
                c.phosphorus = static_cast<uint8_t>(std::min<int>(c.phosphorus + 15, 255));
                c.potassium = static_cast<uint8_t>(std::min<int>(c.potassium + 15, 255));
                if (c.organic_matter < 200) c.organic_matter = static_cast<uint8_t>(std::min<int>(c.organic_matter + 20, 200));
                if (c.microbiome < 255) c.microbiome = static_cast<uint8_t>(std::min<int>(c.microbiome + 30, 255));
                if (c.ph > 50) c.ph = static_cast<uint8_t>(std::max<int>(c.ph - 2, 50)); // Slightly acidic
                say("Applied Organic Fertilizer. Soil nutrients, organic matter, and microbiome improved.");
                break;
            // ROADMAP 2.1 (8.1a) — Soil pH amendments
            case Item::SoilLime:
                if (c.ph < 85) c.ph = static_cast<uint8_t>(std::min<int>(c.ph + 8, 85)); // Raise pH by ~0.8
                if (c.ph < 70) c.ph = static_cast<uint8_t>(std::min<int>(c.ph + 5, 70)); // Extra boost if very acidic
                say("Applied Agricultural Lime. Soil pH raised.");
                break;
            case Item::SoilSulfur:
                if (c.ph > 50) c.ph = static_cast<uint8_t>(std::max<int>(c.ph - 8, 50)); // Lower pH by ~0.8
                if (c.ph > 65) c.ph = static_cast<uint8_t>(std::max<int>(c.ph - 5, 50)); // Extra boost if very alkaline
                say("Applied Elemental Sulfur. Soil pH lowered.");
                break;
            case Item::SoilGypsum:
                // Gypsum adds calcium without changing pH, helps with soil structure
                say("Applied Gypsum. Soil structure improved (calcium added, pH unchanged).");
                break;
            case Item::FertilizerBasic:
                c.nitrogen = static_cast<uint8_t>(std::min<int>(c.nitrogen + 20, 255));
                say("Applied Basic Fertilizer (legacy). Nitrogen increased.");
                break;
            case Item::FertilizerQuality:
                c.nitrogen = static_cast<uint8_t>(std::min<int>(c.nitrogen + 15, 255));
                c.phosphorus = static_cast<uint8_t>(std::min<int>(c.phosphorus + 15, 255));
                say("Applied Quality Fertilizer (legacy). Nitrogen and phosphorus increased.");
                break;
            case Item::FertilizerPremium:
                c.nitrogen = static_cast<uint8_t>(std::min<int>(c.nitrogen + 15, 255));
                c.phosphorus = static_cast<uint8_t>(std::min<int>(c.phosphorus + 15, 255));
                c.potassium = static_cast<uint8_t>(std::min<int>(c.potassium + 15, 255));
                say("Applied Premium Fertilizer (legacy). NPK increased.");
                break;
            default:
                break;
        }
        p.energy -= 1;
        consume_item(p, fert, 1);
        say("Fertilizer applied to the soil.");
        return out;
    }

    // ---------- plant tree (forestation) ----------
    if (cmd == "planttree" || cmd == "plant tree" || cmd == "forest") {
        std::string tree_name = lower_trim(arg);
        Item sapling = Item::None;
        ObjType tree_type = ObjType::None;
        
        if (tree_name == "oak") { sapling = Item::OakSapling; tree_type = ObjType::Oak; }
        else if (tree_name == "maple") { sapling = Item::MapleSapling; tree_type = ObjType::Maple; }
        else if (tree_name == "birch") { sapling = Item::BirchSapling; tree_type = ObjType::Birch; }
        else if (tree_name == "cedar") { sapling = Item::CedarSapling; tree_type = ObjType::Cedar; }
        else if (tree_name == "redwood") { sapling = Item::RedwoodSapling; tree_type = ObjType::Redwood; }
        else if (tree_name == "teak") { sapling = Item::TeakSapling; tree_type = ObjType::Teak; }
        else if (tree_name == "mahogany") { sapling = Item::MahoganySapling; tree_type = ObjType::Mahogany; }
        else if (tree_name == "rubber" || tree_name == "rubber tree") { sapling = Item::RubberTreeSapling; tree_type = ObjType::RubberTree; }
        else if (tree_name == "walnut") { sapling = Item::WalnutSapling; tree_type = ObjType::WalnutTree; }
        else if (tree_name == "hickory") { sapling = Item::HickorySapling; tree_type = ObjType::HickoryTree; }
        else if (tree_name == "chestnut") { sapling = Item::ChestnutSapling; tree_type = ObjType::ChestnutTree; }
        else if (tree_name == "deodar") { sapling = Item::DeodarSapling; tree_type = ObjType::Deodar; }
        else {
            say("Plant which tree? oak, maple, birch, cedar, redwood, teak, mahogany, rubber, walnut, hickory, chestnut, deodar.");
            return out;
        }
        
        int slot = find_slot(p, sapling);
        if (slot < 0) { say("You don't have a " + std::string(item_def(sapling).name) + "."); return out; }
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("Nothing to plant on."); return out; }
        Cell& c = w.at(f);
        if (c.tile != Tile::Grass && c.tile != Tile::GrassVar && c.tile != Tile::Dirt) {
            say("Plant trees on grass or dirt, not tilled soil.");
            return out;
        }
        if (c.obj.type != ObjType::None) { say("Occupied."); return out; }
        if (p.energy < 2) { say("Too tired. Rest or sleep."); return out; }
        p.energy -= 2;
        consume_item(p, sapling, 1);
        c.obj = {tree_type, 1, 0}; // hp=1 (sapling), ore=0 (not tapped)
        // ROADMAP 2.5 — player-planted saplings get fresh physiology + are marked
        // managed (excluded from wild seeding / succession) for the forest ecology.
        c.tree = TreeState{};
        c.tree.height = 0.2f;
        c.tree.biomass = 0.02f;
        c.tree.homozygosity = 255;
        c.tree.player_managed = true;
        if (c.forest_state == 0) {
            c.forest_state = 1;  // seedling canopy
            forest_set_player_managed(c.forest_state, true);
        } else {
            forest_set_player_managed(c.forest_state, true);
        }
        c.seed_bank = 0;  // planted sapling replaces any dormant wild seeds
        say("Planted a " + std::string(item_def(sapling).name) + ". It will grow over time.");
        say("Mature trees can be tapped for sap/resin/rubber/syrup. Chop with axe for logs.");
        return out;
    }

    // ---------- harvest ----------
    if (cmd == "harvest") {
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("Nothing to harvest."); return out; }
        Cell& c = w.at(f);
        if (!c.crop.is_crop()) { say("No crop there."); return out; }
        if (c.crop.stage < 3 || c.crop.days_left > 0) { say("The crop isn't ready yet."); return out; }
        Item produce = c.crop.crop;
        // Fruit trees: don't remove the crop, just mark as harvested this season
        if (c.crop.is_fruit_tree) {
            int season_local = season_index(w.day);
            // Only harvestable if the tree has produced fruit this season (set by advance_day).
            // Prevents re-harvesting the same tree repeatedly in one season.
            if (c.crop.last_harvest_season != season_local) {
                say("The tree has no ripe fruit right now. It will fruit once per season.");
                return out;
            }
            add_item(p, produce, 1);
            p.money += item_def(produce).sell;
            say("You harvest a " + std::string(item_def(produce).name) + " from the tree! +" +
                std::to_string(item_def(produce).sell) + "g");
            say("The tree will produce again next season.");
            // Mark picked this season: stage<3 blocks re-harvest (checked above), and
            // last_harvest_season stays == season so advance_day won't re-fruit until the
            // season changes (when last_harvest_season != season).
            c.crop.stage = 2;
            return out;
        }
        // Wind pollination: flowers adjacent to crop boost quality (2x sell price chance)
        int flower_bonus = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = f.x + dx, ny = f.y + dy;
                if (!w.in_bounds(nx, ny)) continue;
                Cell& adj = w.at(nx, ny);
                if (adj.obj.type == ObjType::Flower) flower_bonus++;
            }
        }
        int sell_price = item_def(produce).sell;
        std::string quality_msg = "";
        if (flower_bonus > 0 && static_cast<unsigned>((static_cast<int>(w.day) * 7 + f.x * 13 + f.y * 19) % 100) < static_cast<unsigned>(20 * flower_bonus)) {
            // 20% chance per adjacent flower for quality bonus (double price)
            sell_price *= 2;
            quality_msg = " ★ Quality!";
        }
        // Regular crops: remove after harvest
        const CropDef* cd = crop_def(produce);
        bool was_giant = c.crop.is_giant;
        // ROADMAP 2.3 — giant crops sell for 3x.
        if (was_giant) sell_price *= 3;
        // ROADMAP 2.3 — seed saving: the harvested crop's genetics (with a 1%
        // mutation chance per locus) are carried into a saved seed item.
        Item seed_item = Item::None;
        if (cd && cd->seed != Item::None) {
            seed_item = cd->seed;
            SeedGen sg;
            sg.alleles = c.crop.alleles;
            std::mt19937 srng(static_cast<unsigned>(w.day) * 104729u +
                              static_cast<uint32_t>(f.x) * 1009u +
                              static_cast<uint32_t>(f.y) * 2027u);
            std::uniform_int_distribution<int> mroll(0, 99);
            std::uniform_int_distribution<int> mv(-2, 2);
            for (auto& a : sg.alleles) {
                if (mroll(srng) < 1) a = static_cast<int8_t>(std::clamp(static_cast<int>(a) + mv(srng), -40, 40));
            }
            sg.homozygosity = compute_homozygosity(sg.alleles);
            p.seed_gen[seed_item] = sg;
            add_item(p, seed_item, 1);
        }
        c.crop = Crop{};
        add_item(p, produce, 1);
        p.money += sell_price;
        say("You harvest a " + std::string(item_def(produce).name) + "! +" +
            std::to_string(sell_price) + "g" + quality_msg);
        if (was_giant) say("It's a GIANT crop! The valley whispers in awe.");
        if (seed_item != Item::None) {
            say("You saved a seed with the plant's genetics. ('breed " +
                std::string(item_def(seed_item).name) + " ...' to combine).");
        }
        if (flower_bonus > 0 && quality_msg.empty()) {
            say("Nearby flowers swayed in the wind... (" + std::to_string(flower_bonus) + " adjacent)");
        }
        return out;
    }

    // ---------- fishing ----------
    if (cmd == "fish") {
        bool near_water = false;
        for (int dy = -1; dy <= 1 && !near_water; ++dy)
            for (int dx = -1; dx <= 1 && !near_water; ++dx) {
                int nx = p.pos.x + dx, ny = p.pos.y + dy;
                if (w.in_bounds(nx, ny) &&
                    (is_water_any(w.at(nx, ny).tile) || w.at(nx, ny).tile == Tile::Ice))
                    near_water = true;
            }
        if (!near_water) { say("There's no water here to fish in."); return out; }
        if (p.energy < 8) { say("Too tired to fish. Rest or sleep."); return out; }
        p.energy -= 8;
        int season_local = season_index(w.day);
        int count = 0;
        fish_table(season_local, count);
        static const struct { const char* name; int price; int min_h, max_h; } fish[5][5] = {
            {{"Anchovy",30,6,21},{"Sardine",40,6,19},{"Bream",55,6,20},{"Halibut",90,6,11},{"Salmon",100,6,21}},
            {{"Tuna",80,6,19},{"Rainbow Trout",100,6,21},{"Sunfish",80,6,19},{"Catfish",100,12,2},{"Pufferfish",180,12,16}},
            {{"Walleye",120,12,2},{"Eel",145,16,2},{"Salmon",100,6,21},{"Midnight Carp",150,22,2},{"Angler",200,6,21}},
            {{"Perch",90,6,21},{"Squid",100,18,2},{"Sturgeon",150,6,21},{"Ice Pip",200,6,21},{"Glacierfish",260,6,21}},
        };
        int hour = hour_of_day(w);
        bool rainy = w.rain_here(p.pos.x, p.pos.y) > 5;  // 8.2: local conditions
        int roll = rand() % 100;
        int catch_chance = rainy ? 65 : 55;
        if (roll < catch_chance) {
            // pick among fish whose active hours cover now (wrap for 2 AM fish)
            const int seasi = season_local < 0 || season_local > 3 ? 0 : season_local;
            int pool[5];
            int npool = 0;
            for (int i = 0; i < 5; ++i) {
                int a = fish[seasi][i].min_h, b = fish[seasi][i].max_h;
                bool active = (a <= b) ? (hour >= a && hour <= b) : (hour >= a || hour <= b);
                if (active) pool[npool++] = i;
            }
            if (npool > 0) {
                const auto& f = fish[seasi][pool[rand() % npool]];
                add_item(p, Item::Fish, 1);
                say((rainy ? "Rain patters on the water. You catch a " : "You catch a ") +
                    std::string(f.name) + "! Sell it at market for " + std::to_string(f.price) + "g.");
            } else {
                say("The water is quiet. No fish about at this hour.");
            }
        } else if (roll < catch_chance + 18) {
            say("You cast your line... a nibble, and nothing. The fish got away.");
        } else {
            p.money += 10;
            say("You reel in a soggy old boot. +10g for the junk collector.");
        }
        return out;
    }

    // ---------- foraging ----------
    if (cmd == "forage") {
        if (p.energy < 4) { say("Too tired to forage. Rest or sleep."); return out; }
        p.energy -= 4;
        int season_local = season_index(w.day);
        static const struct { const char* name; int price; } forage[4][4] = {
            {{"Dandelion",25},{"Wild Horseradish",40},{"Leek",60},{"Morel Mushroom",90}},
            {{"Spice Berry",80},{"Grape",120},{"Sweet Pea",55},{"Red Mushroom",110}},
            {{"Blackberry",60},{"Hazelnut",90},{"Wild Plum",70},{"Chanterelle",160}},
            {{"Snow Yam",100},{"Crystal Fruit",150},{"Crocus",200},{"Winter Root",90}},
        };
        Cell& c = w.at(p.pos);
        if (is_water_any(c.tile) || c.tile == Tile::Tilled) { say("Nothing grows here."); return out; }
        bool rainy = w.rain_here(p.pos.x, p.pos.y) > 5;  // 8.2: local conditions
        bool in_wood = std::string(region_at(w, p.pos.x, p.pos.y)) == "Whisper Wood";
        
        // L4: Forest state affects forage yields
        uint8_t ug = forest_undergrowth(c.forest_state);
        int ug_bonus = 0;
        if (ug == 1) ug_bonus = 5;     // fern: spring greens
        else if (ug == 2) ug_bonus = 15; // berry: summer berries
        else if (ug == 3) ug_bonus = 10; // mushroom: autumn fungi
        
        int roll = rand() % 100;
        int chance = (rainy ? 45 : 35) + (in_wood ? 15 : 0) + ug_bonus;
        if (roll < chance) {
            int sidx = season_local < 0 || season_local > 3 ? 0 : season_local;
            // L4: Undergrowth type influences forage table selection
            int table_idx = rand() % 4;
            if (ug == 3) table_idx = 3; // mushroom patch -> mushroom
            else if (ug == 2) table_idx = 1; // berry patch -> berry
            else if (ug == 1) table_idx = 0; // fern -> spring greens
            const auto& f = forage[sidx][table_idx];
            add_item(p, Item::Forage, 1);
            say("You found a " + std::string(f.name) + "! Worth " + std::to_string(f.price) + "g.");
        } else {
            say("You search the ground... nothing today.");
        }
        return out;
    }

    // ---------- tap tree ----------
    if (cmd == "tap") {
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("No tree there."); return out; }
        Cell& c = w.at(f);
        if (!is_tree(c.obj.type)) { say("That's not a tree you can tap."); return out; }
        std::string sub = lower_trim(arg);
        if (sub == "collect" || sub == "take" || sub == "harvest") {
            if (c.obj.ore == 0) { say("Tree hasn't been tapped. Use 'tap' first to install a tapper."); return out; }
            if (c.obj.hp <= 50) { say("Tree is too young to produce sap."); return out; }
            // Collect sap
            Item sap = tree_sap_item(c.obj.type);
            int amount = 1 + (c.obj.hp - 50) / 50; // more mature = more sap
            add_item(p, sap, static_cast<uint16_t>(amount));
            c.obj.ore = 0; // tapper removed after collection
            say("Collected " + std::to_string(amount) + "x " + std::string(item_def(sap).name) + " from the " + std::string(obj_type_name(c.obj.type)) + ".");
            return out;
        } else if (sub == "remove" || sub == "untap") {
            if (c.obj.ore == 0) { say("No tapper installed."); return out; }
            c.obj.ore = 0;
            say(std::string("Removed tapper from the ") + obj_type_name(c.obj.type) + ".");
            return out;
        } else {
            // Install tapper
            if (c.obj.ore == 1) { say("Tapper already installed."); return out; }
            if (c.obj.hp < 30) { say("Tree is too young for a tapper (needs 30% growth)."); return out; }
            if (!has_item(p, Item::Wood, 10) && !has_item(p, Item::Hardwood, 1)) {
                say("Need 10 Wood or 1 Hardwood to make a tapper.");
                return out;
            }
            if (has_item(p, Item::Hardwood, 1)) consume_item(p, Item::Hardwood, 1);
            else consume_item(p, Item::Wood, 10);
            c.obj.ore = 1; // mark as tapped
            say(std::string("Installed tapper on the ") + obj_type_name(c.obj.type) + ". Use 'tap collect' to gather sap daily.");
            return out;
        }
    }

    // ---------- shake tree ----------
    if (cmd == "shake") {
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("No tree there."); return out; }
        Cell& c = w.at(f);
        if (!is_tree(c.obj.type)) { say("That's not a tree you can shake."); return out; }
        if (c.obj.hp < 50) { say("Tree is too young to shake for saplings."); return out; }
        if (p.energy < 2) { say("Too tired to shake a tree. Rest or sleep."); return out; }
        p.energy -= 2;
        // Shake: 25% chance for sapling from mature trees, 10% from younger
        int chance = (c.obj.hp > 150) ? 25 : (c.obj.hp > 100) ? 20 : (c.obj.hp > 50) ? 10 : 0;
        if (chance > 0 && static_cast<unsigned>((w.day * 7u + static_cast<unsigned>(f.x) * 13u + static_cast<unsigned>(f.y) * 19u) % 100u) < static_cast<unsigned>(chance)) {
            Item sapling = Item::None;
            switch (c.obj.type) {
                case ObjType::Tree: sapling = Item::OakSapling; break;
                case ObjType::Pine: sapling = Item::None; break; // pine doesn't drop saplings from shaking
                case ObjType::Oak: sapling = Item::OakSapling; break;
                case ObjType::Maple: sapling = Item::MapleSapling; break;
                case ObjType::Birch: sapling = Item::BirchSapling; break;
                case ObjType::Cedar: sapling = Item::CedarSapling; break;
                case ObjType::Redwood: sapling = Item::RedwoodSapling; break;
                case ObjType::Teak: sapling = Item::TeakSapling; break;
                case ObjType::Mahogany: sapling = Item::MahoganySapling; break;
                case ObjType::RubberTree: sapling = Item::RubberTreeSapling; break;
                case ObjType::WalnutTree: sapling = Item::WalnutSapling; break;
                case ObjType::HickoryTree: sapling = Item::HickorySapling; break;
                case ObjType::ChestnutTree: sapling = Item::ChestnutSapling; break;
                case ObjType::Deodar: sapling = Item::DeodarSapling; break;
                default: sapling = Item::None; break;
            }
            if (sapling != Item::None) {
                add_item(p, sapling, 1);
                say("You shake the " + std::string(obj_type_name(c.obj.type)) + " and a " + std::string(item_def(sapling).name) + " falls down!");
            } else {
                say("You shake the tree but nothing falls.");
            }
        } else {
            say("You shake the " + std::string(obj_type_name(c.obj.type)) + " but nothing falls.");
        }
        return out;
    }

    // ---------- shop ----------
 if (cmd == "buy") {
        // R16: buy a parcel of land at the Town Center
        std::string lower_arg = lower_trim(arg);
        if (lower_arg == "plot" || lower_arg.rfind("plot ", 0) == 0) {
            if (p.inside != "Town Center") {
                say("Plots are sold at the Town Center. Step inside and 'buy plot <name>'.");
                return out;
            }
            if (lower_arg == "plot") {
                say("Buyable plots:");
                for (size_t i = 0; i < w.plots.size(); ++i) {
                    const Plot& pl = w.plots[i];
                    std::string owner = pl.owner_id ? " (owned)" : "";
                    say("  " + pl.name + " — " + std::to_string(pl.price) + "g, " + pl.climate + owner);
                }
                return out;
            }
            std::string want = lower_trim(lower_arg.substr(5));
            for (size_t i = 0; i < w.plots.size(); ++i) {
                Plot& pl = w.plots[i];
                if (lower_trim(pl.name) == want) {
                    if (pl.owner_id == p.id) { say("You already own the " + pl.name + "."); return out; }
                    if (pl.owner_id != 0) { say("The " + pl.name + " is already owned."); return out; }
                    if (p.money < pl.price) {
                        say("The " + pl.name + " costs " + std::to_string(pl.price) + "g. You can't afford it.");
                        return out;
                    }
                    p.money -= pl.price;
                    pl.owner_id = p.id;
                    p.owned_plots.insert(i);
                    say("You purchase the " + pl.name + " for " + std::to_string(pl.price) + "g. (" + pl.climate + ")");
                    say("Marked on the deed as plot " + std::to_string(i) + ".");
                    return out;
                }
            }
            say("No plot named '" + want + "'. 'buy plot' to list available plots.");
            return out;
        }
        const CropDef* crop = crop_def(arg.c_str());
        Item seed = crop ? crop->seed : Item::None;
        if (arg == "bread") seed = Item::Bread;
        if (seed == Item::None) { say("The shop sells: parsnip, potato, cauliflower, corn, tomato, wheat, blueberry seeds, bread."); return out; }
        const ItemDef& def = item_def(seed);
        // Phase 7.3: economy adaptations modulate shop price via demand_shift / shop_price_mod
        int price = def.buy;
        if (w.economy_shop_price_mod.is_object()) {
            std::string shop = (p.inside == "General Store") ? "General Store" : "Market";
            if (w.economy_shop_price_mod.contains(shop)) {
                const json& mods = w.economy_shop_price_mod[shop];
                if (mods.is_object() && mods.contains(def.name) && mods[def.name].is_number()) {
                    price += mods[def.name].get<int>();
                }
            }
        }
        if (price < 1) price = 1;
        if (p.money < price) { say("You can't afford " + std::string(def.name) + " (" + std::to_string(price) + "g)."); return out; }
        Vec2 d = w.door();
        bool in_shop = p.inside == "General Store" || p.inside == "Market";
        if (!in_shop && (std::abs(int(d.x) - p.pos.x) > 3 || std::abs(int(d.y) - p.pos.y) > 3)) {
            say("The shop is run from your mailbox near the house door,");
            say("or step inside a shop building to 'buy'.");
            return out;
        }
        p.money -= price;
        add_item(p, seed, 1);
        say("You buy " + std::string(def.name) + " for " + std::to_string(price) + "g.");
        return out;
    }
    if (cmd == "sell") {
        static const std::unordered_map<std::string, Item> items = {
            {"parsnip", Item::Parsnip}, {"potato", Item::Potato}, {"cauliflower", Item::Cauliflower},
            {"corn", Item::Corn}, {"tomato", Item::Tomato}, {"wheat", Item::Wheat},
            {"blueberry", Item::Blueberry},
            {"wood", Item::Wood}, {"stone", Item::Stone}, {"fiber", Item::Fiber},
            {"fish", Item::Fish}, {"forage", Item::Forage},
        };
        auto it = items.find(arg);
        if (it == items.end()) {
            say("Sell what? parsnip, potato, cauliflower, corn, tomato, wheat, blueberry, wood, stone, fiber, fish.");
            return out;
        }
        int slot = find_slot(p, it->second);
        if (slot < 0) { say("You don't have any " + std::string(item_def(it->second).name) + " to sell."); return out; }
        int price = item_def(it->second).sell;
        if (it->second == Item::Fish) price = 45;
        if (it->second == Item::Forage) price = 50;
        // Phase 7.3: demand_shift modulates sell price (high demand = better price)
        if (w.economy_demand_shift.is_object()) {
            std::string name = item_def(it->second).name;
            if (w.economy_demand_shift.contains(name) && w.economy_demand_shift[name].is_number()) {
                float mult = w.economy_demand_shift[name].get<float>();
                price = static_cast<int>(static_cast<float>(price) * mult);
                if (price < 1) price = 1;
            }
        }
        auto idx = static_cast<size_t>(slot);
        p.inv[idx].count--;
        if (p.inv[idx].count == 0) p.inv[idx].item = Item::None;
        p.money += price;
        say("You sell " + std::string(item_def(it->second).name) + " for " + std::to_string(price) + "g.");
        return out;
    }

    // ---------- craft ----------
    if (cmd == "craft") {
        std::string thing = lower_trim(arg);
        // bars: 5 ore + 1 wood (coal stand-in) -> bar
        struct BarRecipe { const char* name; Item ore; Item bar; };
        BarRecipe br[] = {
            {"copper bar", Item::CopperOre, Item::CopperBar},
            {"iron bar",   Item::IronOre,   Item::IronBar},
            {"gold bar",   Item::GoldOre,   Item::GoldBar},
        };
        for (auto& r : br) {
            if (thing == r.name) {
                if (!has_item(p, r.ore, 5) || !has_item(p, Item::Wood, 1)) {
                    say("Recipe: " + std::string(r.name) + " — 5 " + item_def(r.ore).name + " + 1 Wood.");
                    return out;
                }
                consume_item(p, r.ore, 5);
                consume_item(p, Item::Wood, 1);
                add_item(p, r.bar, 1);
                say("Smelted " + std::string(item_def(r.bar).name) + ". (5 ore + 1 wood)");
                return out;
            }
        }
        if (thing == "scarecrow") {
            if (!has_item(p, Item::Wood, 10) || !has_item(p, Item::Fiber, 5)) {
                say("Recipe: scarecrow — 10 Wood + 5 Fiber.");
                return out;
            }
            consume_item(p, Item::Wood, 10);
            consume_item(p, Item::Fiber, 5);
            add_item(p, Item::Scarecrow, 1);
            say("Crafted a Scarecrow. (10 Wood + 5 Fiber)");
            return out;
        }
        if (thing == "bread") {
            if (!has_item(p, Item::Wheat, 3)) {
                say("Recipe: bread — need 3 Wheat."); return out;
            }
            consume_item(p, Item::Wheat, 3);
            add_item(p, Item::Bread, 1);
            say("Crafted Bread (3 Wheat).");
            return out;
        }
        if (thing == "composter") {
            if (!has_item(p, Item::Wood, 50) || !has_item(p, Item::Stone, 10) || !has_item(p, Item::Fiber, 20)) {
                say("Recipe: composter — 50 Wood + 10 Stone + 20 Fiber.");
                return out;
            }
            consume_item(p, Item::Wood, 50);
            consume_item(p, Item::Stone, 10);
            consume_item(p, Item::Fiber, 20);
            add_item(p, Item::Composter, 1);
            say("Crafted a Composter. (50 Wood + 10 Stone + 20 Fiber)");
            say("Place it on your farm. Add weeds/fiber to produce fertilizer in 4 days.");
            return out;
        }
        // --- new crafting recipes (keg, preserves jar, etc.) ---
        if (thing == "wine") {
            if (!has_item(p, Item::Grape, 2)) { say("Recipe: wine — need 2 Grape."); return out; }
            consume_item(p, Item::Grape, 2);
            add_item(p, Item::Wine, 1);
            say("Fermented Wine (2 Grape).");
            return out;
        }
        if (thing == "jam") {
            if (!has_item(p, Item::Strawberry, 2)) { say("Recipe: jam — need 2 Strawberry."); return out; }
            consume_item(p, Item::Strawberry, 2);
            add_item(p, Item::Jam, 1);
            say("Cooked Jam (2 Strawberry).");
            return out;
        }
        if (thing == "mayonnaise") {
            if (!has_item(p, Item::Egg, 1)) { say("Recipe: mayonnaise — need 1 Egg."); return out; }
            consume_item(p, Item::Egg, 1);
            add_item(p, Item::Mayonnaise, 1);
            say("Made Mayonnaise (1 Egg).");
            return out;
        }
        if (thing == "honey") {
            // Flower is not an item; check for Forage
            if (!has_item(p, Item::Forage, 1)) { say("Recipe: honey — need 1 Forage."); return out; }
            consume_item(p, Item::Forage, 1);
            add_item(p, Item::Honey, 1);
            say("Produced Honey (1 Forage).");
            return out;
        }
        if (thing == "cheese") {
            if (!has_item(p, Item::Milk, 1)) { say("Recipe: cheese — need 1 Milk."); return out; }
            consume_item(p, Item::Milk, 1);
            add_item(p, Item::Cheese, 1);
            say("Made Cheese (1 Milk).");
            return out;
        }
        say("Recipes: 'craft copper bar' (5 ore + 1 wood), 'craft iron bar', 'craft gold bar', 'craft bread' (3 wheat), 'craft scarecrow' (10 wood + 5 fiber), 'craft composter' (50 wood + 10 stone + 20 fiber), 'craft wine' (2 grape), 'craft jam' (2 strawberry), 'craft mayonnaise' (1 egg), 'craft honey' (1 forage), 'craft cheese' (1 milk).");
        say("Place machines: 'place sprinkler' (2 Iron Bar + 1 Gold Bar), 'place composter'.");
        return out;
    }

    // ---------- place / build structure ----------
    if (cmd == "place") {
        std::string what = lower_trim(arg);
        // sprinkler: 2 Iron Bar + 1 Gold Bar + 1 Stone
        if (what == "sprinkler" || what == "iridium sprinkler") {
            bool big = (what == "iridium sprinkler");
            int needIron = big ? 2 : 2, needGold = big ? 2 : 1, needIri = big ? 1 : 0;
            Vec2 f = facing_cell(p);
            if (!w.in_bounds(f)) { say("You must face an empty tilled or ground tile."); return out; }
            Cell& c = w.at(f);
            if (c.obj.type != ObjType::None) { say("Occupied. Place on an empty tile."); return out; }
            if (c.crop.is_crop() || !w.walkable(f)) { say("Can't place there."); return out; }
            if (!has_item(p, Item::IronBar, needIron) || !has_item(p, Item::GoldBar, needGold) ||
                (big && !has_item(p, Item::IridiumOre, needIri))) {
                say("Place Sprinkler: 2 Iron Bar + 1 Gold Bar" +
                    std::string(big ? " + 1 Iridium Ore (large)" : "") + ".");
                return out;
            }
            consume_item(p, Item::IronBar, needIron);
            consume_item(p, Item::GoldBar, needGold);
            if (big) consume_item(p, Item::IridiumOre, needIri);
            c.obj = {ObjType::Sprinkler, 255, static_cast<uint8_t>(big ? 3 : 2)}; // ore field = tier
            say("Placed a " + std::string(big ? "Iridium" : "Steel") + " Sprinkler on the farmland.");
            say("     It will water adjacent tiles overnight.");
            return out;
        }
        // scarecrow: 1 Scarecrow item, place on tilled or dirt
        if (what == "scarecrow") {
            if (!has_item(p, Item::Scarecrow, 1)) {
                say("You need a Scarecrow. Craft one first: 'craft scarecrow' (10 Wood + 5 Fiber).");
                return out;
            }
            Vec2 f = facing_cell(p);
            if (!w.in_bounds(f)) { say("You must face an empty tilled or ground tile."); return out; }
            Cell& c = w.at(f);
            if (c.obj.type != ObjType::None) { say("Occupied. Place on an empty tile."); return out; }
            if (c.crop.is_crop()) { say("Can't place on a growing crop."); return out; }
            consume_item(p, Item::Scarecrow, 1);
            c.obj = {ObjType::Scarecrow, 255};
            say("Placed a Scarecrow. It will protect crops in a 17x17 area from crows.");
            return out;
        }
        // composter: 1 Composter item, place on any ground
        if (what == "composter") {
            if (!has_item(p, Item::Composter, 1)) {
                say("You need a Composter. Craft one first: 'craft composter' (50 Wood + 10 Stone + 20 Fiber).");
                return out;
            }
            Vec2 f = facing_cell(p);
            if (!w.in_bounds(f)) { say("You must face an empty ground tile."); return out; }
            Cell& c = w.at(f);
            if (c.obj.type != ObjType::None) { say("Occupied. Place on an empty tile."); return out; }
            if (c.crop.is_crop()) { say("Can't place on a growing crop."); return out; }
            consume_item(p, Item::Composter, 1);
            c.obj = {ObjType::Composter, 0, 0}; // hp = days until ready, ore = fertilizer type (0=none, 1=basic, 2=quality)
            say("Placed a Composter. Add weeds or fiber to start composting (4 days).");
            return out;
        }
        // R16: place a farm structure inside one of your owned plots
        if (what == "barn" || what == "silo" || what == "shed" ||
            what == "well" || what == "windmill" || what == "scarecrow plot") {
            // Must be standing inside an owned plot area
            Plot* plot = nullptr;
            size_t plot_idx = 0;
            for (size_t i = 0; i < w.plots.size(); ++i) {
                Plot& pl = w.plots[i];
                if (p.pos.x >= pl.x && p.pos.x < pl.x + pl.w &&
                    p.pos.y >= pl.y && p.pos.y < pl.y + pl.h) { plot = &pl; plot_idx = i; break; }
            }
            if (!plot) { say("You must stand inside one of your plots to build there."); return out; }
            if (plot->owner_id != p.id) { say("You don't own the " + plot->name + "."); return out; }
            // cost + object type per structure
            struct SRec { const char* n; int wood, stone, fiber, money; ObjType obj; const char* msg; };
            static const SRec recs[] = {
                {"barn", 300, 100, 0, 2000, ObjType::Building, "a sturdy wooden barn with stalls."},
                {"silo", 100, 50, 0, 500,  ObjType::Building, "a grain silo for storing forage."},
                {"shed", 150, 40, 20, 750,  ObjType::Building, "a small toolshed."},
                {"well", 40, 80, 0, 400,    ObjType::Building, "a stone well. Watering costs less energy near it."},
                {"windmill", 200, 150, 0, 1500, ObjType::Building, "a windmill for milling grain."},
            };
            const SRec* rec = nullptr;
            for (auto& r : recs) if (what == r.n) { rec = &r; break; }
            if (!rec) { say("Build: barn, silo, shed, well, windmill."); return out; }
            Vec2 f = facing_cell(p);
            if (!w.in_bounds(f)) { say("Face an open tile inside the plot to build."); return out; }
            Cell& c = w.at(f);
            if (c.obj.type != ObjType::None || c.crop.is_crop()) { say("That tile is occupied. Build on an empty tile."); return out; }
            if (f.x < plot->x || f.x >= plot->x + plot->w || f.y < plot->y || f.y >= plot->y + plot->h) {
                say("Build within your plot's boundaries."); return out;
            }
            if (p.money < rec->money || !has_item(p, Item::Wood, rec->wood) ||
                !has_item(p, Item::Stone, rec->stone) || !has_item(p, Item::Fiber, rec->fiber)) {
                say(std::string("Build " + std::string(rec->n) + ": ") + std::to_string(rec->money) + "g + " +
                    std::to_string(rec->wood) + " wood + " + std::to_string(rec->stone) + " stone" +
                    (rec->fiber ? " + " + std::to_string(rec->fiber) + " fiber" : "") + ".");
                return out;
            }
            p.money -= rec->money;
            consume_item(p, Item::Wood, rec->wood);
            consume_item(p, Item::Stone, rec->stone);
            if (rec->fiber) consume_item(p, Item::Fiber, rec->fiber);
            c.obj = {rec->obj, 255};
            p.placed_structs.push_back({plot_idx, static_cast<uint8_t>(what == "barn" ? 1 : what == "silo" ? 2 : what == "shed" ? 3 : what == "well" ? 4 : 6), f.x, f.y});
            say("You build " + std::string(rec->msg) + " on the " + plot->name + ".");
            return out;
        }
        say("Can place: sprinkler (2 Iron Bar + 1 Gold Bar), scarecrow, composter.");
        say("Build on owned plots: barn, silo, shed, well, windmill.");
        return out;
    }

    // ---------- repair ----------
    if (cmd == "repair") {
        std::string building = lower_trim(arg);
        if (building.empty()) { say("Repair what? Usage: repair <building_name> (e.g., 'repair blacksmith')"); return out; }
        // Find building by name (case-insensitive partial match)
        Bldg* target = nullptr;
        bool target_is_dynamic = false;
        for (auto& b : w.buildings) {
            if (lower_trim(b.name).find(building) != std::string::npos) { target = &b; break; }
        }
        if (!target && building == "farmhouse") {
            target = new Bldg{"Farmhouse", 0, 0, 0, 0}; // special case
            target_is_dynamic = true;
        }
        if (!target) { say("There's no '" + arg + "' to repair."); return out; }
        
        // Must be at Carpenter Shop
        bool at_carpenter = false;
        for (auto& b : w.buildings) {
            if (b.name == "Carpenter Shop" && p.pos.x == b.x + (b.w - 1) / 2 && p.pos.y == b.y + b.h) {
                at_carpenter = true; break;
            }
        }
        if (!at_carpenter) { 
            if (target_is_dynamic) delete target;
            say("Visit the Carpenter Shop to arrange repairs."); 
            return out; 
        }
        
        auto it = w.building_states.find(target->name);
        if (it == w.building_states.end()) { 
            say(std::string(target->name) + " is in good condition."); 
            if (target_is_dynamic) delete target;
            return out; 
        }
        BuildingState& bs = it->second;
        if (bs.condition >= 100) { 
            say(std::string(target->name) + " is already in perfect condition."); 
            if (target_is_dynamic) delete target;
            return out; 
        }
        // Cost: 10 wood + 5 stone per 10 condition points
        int needed = 100 - bs.condition;
        int wood_cost = (needed + 9) / 10 * 10;
        int stone_cost = (needed + 9) / 10 * 5;
        if (!has_item(p, Item::Wood, wood_cost) || !has_item(p, Item::Stone, stone_cost)) {
            say("Repair needs " + std::to_string(wood_cost) + " wood + " + std::to_string(stone_cost) + " stone.");
            say("Condition: " + std::to_string(bs.condition) + "/100 (roof leak " + std::to_string(bs.roof_leak) + ", foundation " + std::to_string(bs.foundation) + ")");
            if (target_is_dynamic) delete target;
            return out;
        }
        consume_item(p, Item::Wood, wood_cost);
        consume_item(p, Item::Stone, stone_cost);
        bs.condition = 100;
        bs.roof_leak = 0;
        bs.foundation = 100;
        bs.last_maintained_day = w.day;
        say(std::string(target->name) + " repaired to perfect condition! (-" + std::to_string(wood_cost) + " wood, -" + std::to_string(stone_cost) + " stone)");
        // ROADMAP 2.7 (8.3) — also reset structural damage
        bs.rot = 0; bs.erosion = 0; bs.stress = 0; bs.fire_risk = 0;
        if (target_is_dynamic) delete target;
        return out;
    }

    // ---------- inspect (structural) ----------
    if (cmd == "inspect") {
        std::string building = lower_trim(arg);
        if (building.empty()) { say("Inspect what? Usage: inspect <building_name>"); return out; }
        Bldg* target = nullptr;
        for (auto& b : w.buildings) {
            if (lower_trim(b.name).find(building) != std::string::npos) { target = &b; break; }
        }
        if (!target && building == "farmhouse") { target = new Bldg{"Farmhouse", 0, 0, 0, 0}; }
        if (!target) { say("There's no '" + arg + "' to inspect."); return out; }
        auto it = w.building_states.find(target->name);
        if (it == w.building_states.end()) { say("No structural data."); if (target && target->name == "Farmhouse") delete target; return out; }
        BuildingState& bs = it->second;
        say("=== " + target->name + " Structural Report ===");
        say("Condition: " + std::to_string(bs.condition) + "/100");
        say("Roof leak: " + std::to_string(bs.roof_leak) + "/100");
        say("Foundation: " + std::to_string(bs.foundation) + "/100");
        say("Rot (wood/metal): " + std::to_string(bs.rot) + "/100");
        say("Erosion (stone): " + std::to_string(bs.erosion) + "/100");
        say("Stress (load): " + std::to_string(bs.stress) + "/100");
        say("Fire risk: " + std::to_string(bs.fire_risk) + "/100");
        say("Fire fuel: " + std::to_string(bs.fire_fuel) + "/100");
        say("Fire intensity: " + std::to_string(bs.fire_intensity) + "/100");
        if (bs.has_basement_hatch) say("Basement hatch: YES (fire escape)");
        if (target && target->name == "Farmhouse") delete target;
        return out;
    }

    // ---------- toolrepair ----------
    if (cmd == "toolrepair") {
        InvSlot& slot = p.inv[p.sel];
        if (slot.item == Item::None) { say("No tool selected."); return out; }
        if (slot.max_durability == 0) { say("That tool has no durability tracking."); return out; }
        if (slot.durability == 0) { say("That tool is already in perfect condition."); return out; }
        // Must be at Blacksmith
        bool at_blacksmith = false;
        for (auto& b : w.buildings) {
            if (b.name == "Blacksmith" && p.pos.x == b.x + (b.w - 1) / 2 && p.pos.y == b.y + b.h) {
                at_blacksmith = true; break;
            }
        }
        if (!at_blacksmith) { say("Visit the Blacksmith to repair tools."); return out; }
        // Cost: 1 metal bar per 50 durability points
        int needed = slot.durability;
        int bars = (needed + 49) / 50;
        Item bar_type = Item::IronBar;
        if (slot.item == Item::Pickaxe || slot.item == Item::Axe) bar_type = Item::GoldBar;
        if (!has_item(p, bar_type, bars)) {
            say("Repair needs " + std::to_string(bars) + " " + item_def(bar_type).name + ".");
            say("Durability: " + std::to_string(slot.max_durability - slot.durability) + "/" + std::to_string(slot.max_durability));
            return out;
        }
        consume_item(p, bar_type, bars);
        slot.durability = 0;
        say("Tool repaired to perfect condition! (-" + std::to_string(bars) + " " + item_def(bar_type).name + ")");
        return out;
    }

    // ---------- fire (manual ignition for testing) ----------
    if (cmd == "fire") {
        Cell& c = w.at(p.pos.x, p.pos.y);
        if (c.fire_fuel == 0) { say("Nothing to burn here."); return out; }
        if (c.fire_intensity > 0) { say("Already burning!"); return out; }
        c.fire_intensity = std::min<uint8_t>(100, c.fire_fuel);
        w.spread_fire(p.pos.x, p.pos.y);
        say("Fire started! Intensity: " + std::to_string(c.fire_intensity));
        return out;
    }

    // ---------- structural (world report) ----------
    if (cmd == "structural") {
        int total_buildings = 0, total_rot = 0, total_erosion = 0, total_stress = 0, total_fire_risk = 0, burning = 0;
        for (auto& [name, bs] : w.building_states) {
            total_buildings++;
            total_rot += bs.rot;
            total_erosion += bs.erosion;
            total_stress += bs.stress;
            total_fire_risk += bs.fire_risk;
            if (bs.fire_intensity > 0) burning++;
        }
        int burning_cells = 0;
        for (int y = 0; y < MAP_H; ++y)
            for (int x = 0; x < MAP_W; ++x)
                if (w.at(x, y).fire_intensity > 0) burning_cells++;
        say("=== Valley Structural Report ===");
        say("Buildings tracked: " + std::to_string(total_buildings));
        say("Avg rot: " + std::to_string(total_buildings ? total_rot / total_buildings : 0) + "/100");
        say("Avg erosion: " + std::to_string(total_buildings ? total_erosion / total_buildings : 0) + "/100");
        say("Avg stress: " + std::to_string(total_buildings ? total_stress / total_buildings : 0) + "/100");
        say("Avg fire risk: " + std::to_string(total_buildings ? total_fire_risk / total_buildings : 0) + "/100");
        say("Buildings burning: " + std::to_string(burning));
        say("Cells burning: " + std::to_string(burning_cells));
        return out;
    }


    // ---------- creature (wildlife report) ----------
    if (cmd == "creature") {
        std::string target = lower_trim(arg);
        if (target.empty()) {
            int counts[7] = {0};
            for (auto& w : w.wildlife) {
                if (w.type != WildlifeType::None) counts[static_cast<int>(w.type)]++;
            }
            say("=== Valley Wildlife Census ===");
            say("Deer: " + std::to_string(counts[1]) + "  Rabbits: " + std::to_string(counts[4]) + "  Owls: " + std::to_string(counts[2]) + "  Fisher-cats: " + std::to_string(counts[3]));
            say("Wolves: " + std::to_string(counts[5]) + "  Bears: " + std::to_string(counts[6]));
            int herd_count = 0, total_members = 0;
            for (auto& h : w.herds) {
                if (!h.members.empty()) { herd_count++; total_members += h.members.size(); }
            }
            say("Herds/Packs: " + std::to_string(herd_count) + " (" + std::to_string(total_members) + " members)");
            int sick = 0, carriers = 0;
            for (auto& w : w.wildlife) {
                if (w.disease_level > 0) { if (w.is_carrier) carriers++; else sick++; }
            }
            say("Sick: " + std::to_string(sick) + "  Carriers: " + std::to_string(carriers));
            return out;
        }
        WildlifeType t = WildlifeType::None;
        if (target == "deer") t = WildlifeType::Deer;
        else if (target == "rabbit") t = WildlifeType::Rabbit;
        else if (target == "owl") t = WildlifeType::Owl;
        else if (target == "fisher-cat") t = WildlifeType::FisherCat;
        else if (target == "wolf") t = WildlifeType::Wolf;
        else if (target == "bear") t = WildlifeType::Bear;
        if (t == WildlifeType::None) { say("Unknown creature. Try: deer, rabbit, owl, fisher-cat, wolf, bear"); return out; }
        for (auto& w : w.wildlife) {
            if (w.type == t) {
                const char* stage_name = w.life_stage == 0 ? "infant" : w.life_stage == 1 ? "juvenile" : w.life_stage == 2 ? "adult" : "senior";
                say("=== " + std::string(wildlife_name(t)) + " Report ===");
                say("Age: " + std::to_string(w.age_ticks) + " ticks (" + stage_name + ")");
                say("Hunger: " + std::to_string(w.hunger) + "/100  Thirst: " + std::to_string(w.thirst) + "/100  Energy: " + std::to_string(w.energy) + "/100");
                say("Body temp: " + std::to_string(w.body_temp) + " C  Circadian: " + std::to_string(w.circadian));
                say("Disease: " + (w.disease_level > 0 ? std::to_string(w.disease_level) + "/100 (type " + std::to_string(w.disease_type) + ")" : "healthy") + (w.is_carrier ? " [carrier]" : ""));
                say("Herd: " + std::string(w.herd_id >= 0 ? std::to_string(w.herd_id) : "solitary") + "  Rank: " + std::to_string(w.social_rank) + "  Bonds: " + std::to_string(w.social_bonds));
                say("Fertility: " + std::to_string(w.fertility) + "/100  Immunity: 0x" + std::to_string(w.immunity_genes));
                return out;
            }
        }
        say("No " + target + " found.");
        return out;
    }

    // ---------- herd (herd/pack details) ----------
    if (cmd == "herd") {
        int herd_id = -1;
        if (!arg.empty()) herd_id = std::stoi(arg);
        if (herd_id >= 0 && herd_id < static_cast<int>(w.herds.size())) {
            World::Herd& h = w.herds[herd_id];
            if (h.members.empty()) { say("Herd " + std::to_string(herd_id) + " is empty."); return out; }
            say("=== Herd/Pack " + std::to_string(herd_id) + " (" + wildlife_name(h.primary_type) + ") ===");
            say("Members: " + std::to_string(h.members.size()) + "  Cohesion: " + std::to_string(h.cohesion) + "/100");
            say("Territory: (" + std::to_string(h.territory_center.x) + "," + std::to_string(h.territory_center.y) + ") radius " + std::to_string(h.territory_radius));
            if (h.alpha_idx >= 0) {
                Wildlife& a = w.wildlife[h.alpha_idx];
                say("Alpha: " + std::string(wildlife_name(a.type)) + " at (" + std::to_string(a.pos.x) + "," + std::to_string(a.pos.y) + ") rank " + std::to_string(a.social_rank));
            }
            say("Formed day " + std::to_string(h.formed_day) + "  Cohesion: " + std::to_string(h.cohesion));
            return out;
        }
        say("=== Valley Herds/Packs ===");
        for (size_t i = 0; i < w.herds.size(); ++i) {
            World::Herd& h = w.herds[i];
            if (h.members.empty()) continue;
            say("Herd " + std::to_string(i) + ": " + wildlife_name(h.primary_type) + "  " + std::to_string(h.members.size()) + " members  cohesion " + std::to_string(h.cohesion) + "/100");
        }
        return out;
    }

    // ---------- disease (wildlife disease status) ----------
    if (cmd == "disease") {
        int sick = 0, carriers = 0, immune = 0;
        for (auto& creature : w.wildlife) {
            if (creature.disease_level > 0) {
                if (creature.is_carrier) carriers++;
                else sick++;
            }
            if (creature.immunity_genes != 0) immune++;
        }
        say("=== Valley Disease Report ===");
        say("Sick: " + std::to_string(sick) + "  Asymptomatic carriers: " + std::to_string(carriers) + "  Immune: " + std::to_string(immune));
        int type_sick[7] = {0}, type_carriers[7] = {0};
        for (auto& creature : w.wildlife) {
            if (creature.disease_level > 0) {
                if (creature.is_carrier) type_carriers[static_cast<int>(creature.type)]++;
                else type_sick[static_cast<int>(creature.type)]++;
            }
        }
        say("Deer: " + std::to_string(type_sick[1]) + " sick, " + std::to_string(type_carriers[1]) + " carriers");
        say("Rabbits: " + std::to_string(type_sick[4]) + " sick, " + std::to_string(type_carriers[4]) + " carriers");
        say("Owls: " + std::to_string(type_sick[2]) + " sick, " + std::to_string(type_carriers[2]) + " carriers");
        say("Fisher-cats: " + std::to_string(type_sick[3]) + " sick, " + std::to_string(type_carriers[3]) + " carriers");
        say("Wolves: " + std::to_string(type_sick[5]) + " sick, " + std::to_string(type_carriers[5]) + " carriers");
        say("Bears: " + std::to_string(type_sick[6]) + " sick, " + std::to_string(type_carriers[6]) + " carriers");
        return out;
    }

    // ---------- upgrade farmhouse ----------
    if (cmd == "upgrade" && lower_trim(arg).find("farmhouse") != std::string::npos) {
        // Must be at Carpenter Shop
        bool at_carpenter = false;
        for (auto& b : w.buildings) {
            if (b.name == "Carpenter Shop" && p.pos.x == b.x + (b.w - 1) / 2 && p.pos.y == b.y + b.h) {
                at_carpenter = true; break;
            }
        }
        if (!at_carpenter) { say("Visit the Carpenter Shop to upgrade your farmhouse."); return out; }
        
        if (w.farmhouse_level >= 4) { say("Your farmhouse is already at the maximum level (Manor)."); return out; }
        
        int next_level = w.farmhouse_level + 1;
        int gold_cost = 0, wood_cost = 0, stone_cost = 0;
        const char* level_name = "";
        
        switch (next_level) {
            case 2: // Cottage
                level_name = "Cottage";
                gold_cost = 10000; wood_cost = 350; stone_cost = 0;
                break;
            case 3: // House
                level_name = "House";
                gold_cost = 50000; wood_cost = 450; stone_cost = 200;
                break;
            case 4: // Manor
                level_name = "Manor";
                gold_cost = 100000; wood_cost = 600; stone_cost = 300;
                break;
        }
        
        if (p.money < gold_cost || !has_item(p, Item::Wood, wood_cost) || !has_item(p, Item::Stone, stone_cost)) {
            say("Upgrade to " + std::string(level_name) + " costs " + std::to_string(gold_cost) + "g, " + 
                std::to_string(wood_cost) + " wood" + (stone_cost > 0 ? ", " + std::to_string(stone_cost) + " stone" : "") + ".");
            say("Current level: " + std::to_string(w.farmhouse_level) + " (Starter=1, Cottage=2, House=3, Manor=4)");
            return out;
        }
        
        p.money -= gold_cost;
        consume_item(p, Item::Wood, wood_cost);
        if (stone_cost > 0) consume_item(p, Item::Stone, stone_cost);
        w.farmhouse_level = static_cast<uint8_t>(next_level);
        
        say("Farmhouse upgraded to " + std::string(level_name) + "! (-" + std::to_string(gold_cost) + "g, -" + 
            std::to_string(wood_cost) + " wood" + (stone_cost > 0 ? ", -" + std::to_string(stone_cost) + " stone" : "") + ")");
        say("New rooms added: " + std::string(next_level == 2 ? "Kitchen + Bedroom" : next_level == 3 ? "Cellar + Study" : "Nursery + Verandah"));
        say("Use 'enter farmhouse' to see the expanded interior.");
        return out;
    }

    // ---------- eat ----------
    if (cmd == "eat") {
        static const std::unordered_map<std::string, std::pair<Item, int>> foods = {
            {"bread", {Item::Bread, 30}},
            {"parsnip", {Item::Parsnip, 20}}, {"potato", {Item::Potato, 25}},
            {"cauliflower", {Item::Cauliflower, 30}}, {"corn", {Item::Corn, 30}},
            {"tomato", {Item::Tomato, 20}}, {"wheat", {Item::Wheat, 10}},
            {"blueberry", {Item::Blueberry, 20}},
            {"green bean", {Item::GreenBean, 15}}, {"hops", {Item::Hops, 10}},
            {"strawberry", {Item::Strawberry, 20}}, {"melon", {Item::Melon, 30}},
            {"pumpkin", {Item::Pumpkin, 40}}, {"red cabbage", {Item::RedCabbage, 25}},
            {"rhubarb", {Item::Rhubarb, 20}}, {"garlic", {Item::Garlic, 15}},
            {"artichoke", {Item::Artichoke, 25}}, {"bok choy", {Item::BokChoy, 20}},
            {"kale", {Item::Kale, 20}}, {"cranberry", {Item::Cranberry, 15}},
            {"grape", {Item::Grape, 15}},
            {"apple", {Item::Apple, 25}}, {"cherry", {Item::Cherry, 20}},
            {"peach", {Item::Peach, 30}}, {"pomegranate", {Item::Pomegranate, 30}},
            {"apricot", {Item::Apricot, 15}}, {"orange", {Item::Orange, 25}},
            {"banana", {Item::Banana, 30}}, {"mango", {Item::Mango, 25}},
            {"plum", {Item::Plum, 20}}, {"pear", {Item::Pear, 25}},
            {"fig", {Item::Fig, 20}}, {"avocado", {Item::Avocado, 30}},
            {"lemon", {Item::Lemon, 10}}, {"lime", {Item::Lime, 10}},
            {"grapefruit", {Item::Grapefruit, 15}}, {"persimmon", {Item::Persimmon, 20}},
            {"sap", {Item::Sap, 10}}, {"resin", {Item::Resin, 15}}, {"rubber", {Item::Rubber, 20}},
            {"bark", {Item::Bark, 5}}, {"hardwood", {Item::Hardwood, 25}},
            {"maple syrup", {Item::MapleSyrup, 50}}, {"oak resin", {Item::OakResin, 30}},
            {"pine tar", {Item::PineTar, 20}},
            {"walnut", {Item::Walnut, 25}}, {"hickory nut", {Item::HickoryNut, 20}},
            {"chestnut", {Item::Chestnut, 20}}, {"acorn", {Item::Acorn, 5}},
            {"forage", {Item::Forage, 25}}, {"fish", {Item::Fish, 20}},
        };
        auto it = foods.find(arg);
        if (it == foods.end()) {
            say("Eat what? bread, parsnip, potato, cauliflower, corn, tomato, wheat, blueberry, green bean, hops, strawberry, melon, pumpkin, red cabbage, rhubarb, garlic, artichoke, bok choy, kale, cranberry, grape, apple, cherry, peach, pomegranate, apricot, orange, banana, mango, plum, pear, fig, avocado, lemon, lime, grapefruit, persimmon, sap, resin, rubber, bark, hardwood, maple syrup, oak resin, pine tar, walnut, hickory nut, chestnut, acorn, forage, fish.");
            return out;
        }
        int slot = find_slot(p, it->second.first);
        if (slot < 0) { say("You don't have any to eat."); return out; }
        if (p.energy >= p.max_energy) { say("You're already full of energy."); return out; }
        p.inv[static_cast<size_t>(slot)].count--;
        if (p.inv[static_cast<size_t>(slot)].count == 0) p.inv[static_cast<size_t>(slot)].item = Item::None;
        p.energy = std::min<float>(p.max_energy, p.energy + static_cast<float>(it->second.second));
        say("You eat " + std::string(item_def(it->second.first).name) +
            ". +" + std::to_string(it->second.second) + " energy.");
        return out;
    }

    // ---------- talk ----------
    if (cmd == "gift") {
        if (arg.empty()) { say("Gift what to whom? 'gift <npc> <item>'"); return out; }
        std::string npc_name = arg;
        std::string item_word;
        { auto sp = arg.find(' '); if (sp != std::string::npos) { npc_name = arg.substr(0, sp); item_word = arg.substr(sp + 1); } }
        std::string lc = lower_trim(npc_name);
        NPC* found = nullptr;
        int best = 1000;
        for (auto& n : w.npcs) {
            if (lower_trim(n.name).find(lc) != std::string::npos) {
                int dist = std::abs(int(n.pos.x) - p.pos.x) + std::abs(int(n.pos.y) - p.pos.y);
                if (dist < best) { best = dist; found = &n; }
            }
        }
        if (!found) { say("No one named '" + npc_name + "' is around."); return out; }
        if (best > 3) { say(found->name + " is too far away. Walk closer first."); return out; }
        if (p.gifted_today.count(found->name)) { say("You already gave " + found->name + " a gift today."); return out; }
        Item gift = item_from_name(item_word);
        if (gift == Item::None) { say("You don't carry anything called '" + item_word + "'."); return out; }
        int slot = find_slot(p, gift);
        if (slot < 0) { say("You don't have any " + std::string(item_def(gift).name) + " to give."); return out; }
        int taste = gift_taste(found->name, gift);
        p.gifted_today.insert(found->name);
        p.inv[static_cast<size_t>(slot)].count--;
        if (p.inv[static_cast<size_t>(slot)].count == 0) p.inv[static_cast<size_t>(slot)].item = Item::None;
        uint8_t h = p.hearts[found->name];
        std::string reaction;
        if (taste >= 2) { h = std::min<uint8_t>(h + 2, 10); reaction = found->name + " beams! \"Oh, this is wonderful!\""; }
        else if (taste == 1) { h = std::min<uint8_t>(h + 1, 10); reaction = found->name + " smiles. \"How thoughtful!\""; }
        else if (taste == 0) { reaction = found->name + " takes it politely. \"Thank you, I suppose.\""; }
        else { h = h >= 1 ? h - 1 : h; reaction = found->name + " grimaces. \"Ugh, really?\""; }
        p.hearts[found->name] = h;
        say("You give " + found->name + " a " + std::string(item_def(gift).name) + ".");
        say(reaction);
        if (h == 10 && taste >= 1) say("(You're really hitting it off with " + found->name + "!)");
        return out;
    }
    if (cmd == "hearts" || cmd == "friends") {
        if (p.hearts.empty()) {
            say("You haven't befriended anyone yet. Gift things to villagers to earn hearts!");
            return out;
        }
        for (auto& [nm, h] : p.hearts) {
            std::string bar;
            for (int i = 0; i < 5; ++i) bar += (i < h / 2 ? "\xE2\x99\xA5" : "\xC2\xB7");
            say(nm + ": " + bar + "  (" + std::to_string(h) + "/10)");
        }
        return out;
    }
    if (cmd == "plots" || cmd == "deeds") {
        bool any = false;
        for (size_t i = 0; i < w.plots.size(); ++i) {
            const Plot& pl = w.plots[i];
            std::string owner = pl.owner_id == p.id ? " — YOU OWN THIS" : (pl.owner_id ? " — owned" : "");
            std::string build;
            if (pl.owner_id == p.id) {
                int cnt = 0;
                for (auto& st : p.placed_structs) if (st.plot_idx == i) ++cnt;
                build = ", " + std::to_string(cnt) + " structure(s)";
            }
            any = true;
            say("[" + std::to_string(i) + "] " + pl.name + " @(" + std::to_string(pl.x) + "," + std::to_string(pl.y) +
                ") " + std::to_string(pl.price) + "g — " + pl.climate + owner + build);
        }
        if (!any) say("There are no plots for sale.");
        return out;
    }
    if (cmd == "talk") {
        // locate nearest npc by name (if given) or by distance
        NPC* found = nullptr;
        int best = 1000;
        for (auto& n : w.npcs) {
            int dist = std::abs(int(n.pos.x) - p.pos.x) + std::abs(int(n.pos.y) - p.pos.y);
            std::string nm = lower_trim(n.name);
            if (!arg.empty() && nm != arg) continue;
            if (dist < best) { best = dist; found = &n; }
        }
        if (!found) {
            if (!arg.empty()) say("No one named '" + arg + "' is around.");
            else say("There's no one to talk to here.");
            return out;
        }
        if (best > 3) { say(found->name + " is too far away. Walk closer first."); return out; }
        int season_local = season_index(w.day);
        p.dir = 0;
        if (found->pos.y < p.pos.y) p.dir = 3;
        else if (found->pos.x > p.pos.x) p.dir = 2;
        else if (found->pos.x < p.pos.x) p.dir = 1;
        // ROADMAP 1.4: at low sanity, NPC dialogue distorts (whispered
        // underlayer, word-swap, hallucinated extra clause).
        {
            // ROADMAP 1.7d: cognitive-aware LLM line when the NPC has a core.
            ashgrove::CognitiveCore* core = nullptr;
            try {
                core = &ashgrove::CognitiveRegistry::instance().get_or_create("npc:" + found->name);
            } catch (...) { core = nullptr; }
            std::string base = cognitive_dialogue_line(found->name, core, season_local);
            std::string distorted = w.distort_dialogue(p, found->name, base);
            say(found->name + ": " + distorted);
        }
        // P2: NPCs remember the player's deaths — survivors of the loops
        // (Mayor, Witch, Traveler, Doctor) reference them directly.
        if (p.death_count > 0) {
            std::string dl = npc_death_line(found->name.c_str(), p.death_count);
            if (!dl.empty()) say(found->name + " adds, quietly: " + dl);
        }
        uint8_t h = p.hearts[found->name];
        if (h >= 6) say(found->name + " has a warm look as they chat — they clearly like you.");
        else if (h >= 3) say(found->name + " chats happily with you. Things are going well.");
        else if (h >= 1) say(found->name + " smiles at you. You're becoming familiar.");
        return out;
    }

    // ---------- enter / exit ----------
    if (cmd == "enter") {
        if (!p.inside.empty()) { say("You're already inside. 'exit' first."); return out; }
        Vec2 d = w.door();
        Bldg* target = nullptr;
        if (arg.empty()) {
            if (p.pos.x == d.x && p.pos.y == d.y) {           // farmhouse door
                // Check farmhouse condition
                auto it = w.building_states.find("Farmhouse");
                if (it != w.building_states.end() && it->second.condition < 20) {
                    say("The farmhouse is too dilapidated to enter safely! (condition: " + std::to_string(it->second.condition) + "%)");
                    say("Repair it at the Carpenter Shop.");
                    return out;
                }
                p.inside = "Farmhouse";
                p.inside_exit = d;
            }
            for (auto& b : w.buildings)
                if (p.pos.x == b.x + (b.w - 1) / 2 && p.pos.y == b.y + b.h) {
                    // Check building condition
                    auto it = w.building_states.find(b.name);
                    if (it != w.building_states.end() && it->second.condition < 20) {
                        say(std::string(b.name) + " is too dilapidated to enter safely! (condition: " + std::to_string(it->second.condition) + "%)");
                        say("Repair it at the Carpenter Shop.");
                        return out;
                    }
                    p.inside = b.name;
                    p.inside_exit = {int16_t(b.x + (b.w - 1) / 2), int16_t(b.y + b.h)};
                    break;
                }
        } else {
            std::string nm = lower_trim(arg);
            if (nm == "farmhouse") {
                if (p.pos.x != d.x || p.pos.y != d.y) {
                    say("Stand on the farmhouse doorstep to enter."); return out;
                }
                // Check farmhouse condition
                auto it = w.building_states.find("Farmhouse");
                if (it != w.building_states.end() && it->second.condition < 20) {
                    say("The farmhouse is too dilapidated to enter safely! (condition: " + std::to_string(it->second.condition) + "%)");
                    say("Repair it at the Carpenter Shop.");
                    return out;
                }
                p.inside = "Farmhouse";
                p.inside_exit = d;
            } else {
                for (auto& b : w.buildings)
                    if (lower_trim(b.name).find(nm) != std::string::npos) { target = &b; break; }
                if (!target) { say("There's no '" + arg + "' around here."); return out; }
                if (p.pos.x != target->x + (target->w - 1) / 2 || p.pos.y != target->y + target->h) {
                    say("Stand at the door of " + std::string(target->name) + " first."); return out;
                }
                // Check building condition
                auto it = w.building_states.find(target->name);
                if (it != w.building_states.end() && it->second.condition < 20) {
                    say(std::string(target->name) + " is too dilapidated to enter safely! (condition: " + std::to_string(it->second.condition) + "%)");
                    say("Repair it at the Carpenter Shop.");
                    return out;
                }
                p.inside = target->name;
                p.inside_exit = {int16_t(target->x + (target->w - 1) / 2),
                                 int16_t(target->y + target->h)};
            }
        }
        auto rit = w.interiors.find(p.inside);
        if (rit == w.interiors.end()) { p.inside.clear(); say("That building has no interior yet."); return out; }
        // Register this building as a known landmark for path-walking
        p.known_landmarks.insert(p.inside);
        const InteriorRoom& r = rit->second;
        p.inx = static_cast<int16_t>(r.w / 2);
        p.iny = static_cast<int16_t>(r.h - 2);
        for (int16_t sy = r.h - 2; sy >= 0; --sy) {
            char ch = r.rows[static_cast<size_t>(sy)][static_cast<size_t>(r.w / 2)];
            if (ch == '.' || ch == ' ' || ch == 'P') { p.iny = sy; break; }
        }
        p.dir = 3;
        say("You step inside the " + p.inside + ".");
        // Horror location structures (ROADMAP 1.1): entering the deep places
        // taxes the mind and surfaces a fragment of the hidden narrative.
        if (p.inside == "Witch's Hut") {
            w.damage_sanity(p, 4.0f);
            say("The air smells of dried herbs and old secrets. A mirror at the table shows you a face that is almost your own.");
        } else if (p.inside == "Abandoned Sanitarium") {
            w.damage_sanity(p, 6.0f);
            say("The corridors hum with a wrongness that has nothing to do with sound. The beds are empty, but they are not unoccupied.");
        } else if (p.inside == "Ritual Circle") {
            w.damage_sanity(p, 5.0f);
            say("The stone at the center is warm to the touch. Something here is still listening.");
        }
        // ROADMAP 1.4: rare 4th-wall meta-break inside deep places at fractured sanity.
        if (p.inside == "Witch's Hut" || p.inside == "Abandoned Sanitarium" ||
            p.inside == "Ritual Circle" || p.inside == "Basement") {
            std::string mb = w.roll_meta_break(p, /*once=*/false);
            if (!mb.empty()) say(mb);
        }
        return out;
    }
    if (cmd == "exit" || cmd == "leave") {
        if (p.inside.empty()) {
            // Check if player is in farmhouse exterior area
            if (w.in_house(p.pos.x, p.pos.y)) {
                Vec2 d = w.door();
                say("You step out the farmhouse door.");
                p.pos = d;
                p.target = p.pos;
                p.path.clear();
                p.moving = false;
                return out;
            }
            say("You're outside already."); return out;
        }
        if (p.inside == "Basement") { w.leave_basement(p); say("You climb back up the stairs into the night air."); return out; }
        say("You step out the door.");
        p.inside.clear();
        p.pos = p.inside_exit;
        p.target = p.pos;
        p.path.clear();
        p.moving = false;
        return out;
    }

    // ---------- basement (Phase 6: hidden under-map, only after midnight) ----------
    if (cmd == "basement") {
        if (p.inside == "Basement") {
            say("You are already in the basement. 'exit' to return above."); return out;
        }
        int hour = hour_of_day(w);
        if (hour < 24) {
            say("A faint draft rises from beneath the floorboards, but the way stays shut.");
            if (!p.secrets_found.empty()) say("(Something remembers the last time you went down. It waits for midnight.)");
            return out;
        }
        // After midnight (24 = 12:00 AM, up to 26 = 2:00 AM) the hatch yields.
        say("The floorboards shift. A cold stairwell opens beneath the farmhouse.");
        say("You descend into the dark.");
        w.trigger_basement(p);
        // ROADMAP 1.4: procedurally-generated room + hazard for this descent,
        // carrying a persistent "mark" the player brings to the surface.
        {
            std::string pg = w.roll_basement_procgen(p);
            std::istringstream pgss(pg);
            std::string ln;
            while (std::getline(pgss, ln)) if (!ln.empty()) say(ln);
        }
        return out;
    }

    // ---------- interact ----------
    if (cmd == "interact" || cmd == "use") {
        // Outdoor: check facing cell for machines (composter)
        if (p.inside.empty()) {
            Vec2 f = facing_cell(p);
            if (w.in_bounds(f)) {
                Cell& c = w.at(f);
                if (c.obj.type == ObjType::Composter) {
                    std::string sub = lower_trim(arg);
                    if (sub == "add" || sub == "put" || sub == "fill") {
                        // Add fiber (from weeds via scythe)
                        if (!has_item(p, Item::Fiber, 1)) { say("You need fiber to add to the composter (use scythe on weeds)."); return out; }
                        if (c.obj.hp > 0) { say("Composter is already working (day " + std::to_string(c.obj.hp) + "/4)."); return out; }
                        consume_item(p, Item::Fiber, 1);
                        c.obj.hp = 1; // day 1 of 4
                        c.obj.ore = 1; // basic fertilizer from fiber
                        say("Added Fiber to composter. Fertilizer ready in 4 days.");
                        return out;
                    } else if (sub == "collect" || sub == "take" || sub == "harvest") {
                        if (c.obj.hp == 0) { say("Composter is empty."); return out; }
                        if (c.obj.hp < 4) { say("Not ready yet. " + std::to_string(4 - c.obj.hp) + " more days."); return out; }
                        // Produce fertilizer
                        Item fert = (c.obj.ore == 2) ? Item::FertilizerQuality : Item::FertilizerBasic;
                        add_item(p, fert, 1);
                        c.obj = {ObjType::Composter, 0, 0};
                        say("Collected " + std::string(item_def(fert).name) + " from composter.");
                        return out;
                    } else {
                        say("Composter: day " + std::to_string(c.obj.hp) + "/4. Use 'interact add' to add weeds/fiber, 'interact collect' when ready.");
                        return out;
                    }
} // Close Composter if block
            // ROADMAP 2.1 (8.1a) — Composter: produce N/P/K specific fertilizers based on input materials
            // Track NPK contributions based on materials added
            // hp = days of composting (0-4), ore = N level, hp2 = P level, hp3 = K level
            if (c.obj.type == ObjType::Composter) {
                std::string arg_trimmed = lower_trim(arg);
                std::string sub = arg_trimmed.substr(0, arg_trimmed.find(' '));
                if (sub == "add" || sub == "put" || sub == "fill") {
                    // Add various organic materials, each contributing different NPK
                    Item material = Item::None;
                    if (arg.find("fiber") != std::string::npos || arg.find("weed") != std::string::npos) material = Item::Fiber;
                    else if (arg.find("crop") != std::string::npos || arg.find("harvest") != std::string::npos) material = Item::Fiber;
                    else if (arg.find("manure") != std::string::npos) material = Item::Milk; // placeholder for manure
                    else if (arg.find("ash") != std::string::npos) material = Item::Wood; // wood ash = potassium
                    else {
                        say("Add what? Try: 'interact add fiber', 'interact add weeds', 'interact add ash'.");
                        return out;
                    }

                    if (!has_item(p, material, 1)) { say("You don't have that material."); return out; }
                    if (c.obj.hp > 0) { say("Composter is already working (day " + std::to_string(c.obj.hp) + "/4)."); return out; }

                    consume_item(p, material, 1);
                    c.obj.hp = 1; // day 1 of 4

                    // Track NPK contributions based on material
                    // hp = days, ore = N level, hp2 = P level, hp3 = K level
                    if (material == Item::Fiber) {
                        c.obj.ore = 1; // N: moderate
                        c.obj.hp2 = 1; // P: low
                        c.obj.hp3 = 1; // K: low
                    } else if (material == Item::Wood) { // wood ash = high potassium
                        c.obj.ore = 0; // N
                        c.obj.hp2 = 1; // P
                        c.obj.hp3 = 3; // K: high
                    } else if (material == Item::Milk) { // manure = high nitrogen
                        c.obj.ore = 3; // N: high
                        c.obj.hp2 = 1; // P
                        c.obj.hp3 = 1; // K
                    }

                    say("Added " + std::string(item_def(material).name) + " to composter. Composting started (4 days).");
                    return out;
                } else if (sub == "collect" || sub == "take" || sub == "harvest") {
                    if (c.obj.hp == 0) { say("Composter is empty."); return out; }
                    if (c.obj.hp < 4) { say("Not ready yet. " + std::to_string(4 - c.obj.hp) + " more days."); return out; }

                    // Produce fertilizer based on accumulated NPK
                    Item fert = Item::FertilizerOrganic; // default
                    uint8_t n_level = c.obj.ore;  // N level
                    uint8_t p_level = c.obj.hp2;  // P level
                    uint8_t k_level = c.obj.hp3;  // K level

                    if (n_level >= 3 && p_level <= 2 && k_level <= 2) fert = Item::FertilizerNitrogen;
                    else if (p_level >= 3 && n_level <= 2 && k_level <= 2) fert = Item::FertilizerPhosphorus;
                    else if (k_level >= 3 && n_level <= 2 && p_level <= 2) fert = Item::FertilizerPotassium;
                    else if (n_level >= 2 && p_level >= 2 && k_level >= 2) fert = Item::FertilizerBalanced;
                    else fert = Item::FertilizerOrganic;

                    add_item(p, fert, 1);
                    c.obj = {ObjType::Composter, 0, 0, 0}; // reset
                    say("Collected " + std::string(item_def(fert).name) + " from composter.");
                    return out;
                } else {
                    say("Composter: day " + std::to_string(c.obj.hp) + "/4. Use 'interact add <material>' to add materials, 'interact collect' when ready.");
                    return out;
                }
            } // Close Composter if block
            // Well interaction
                else if (c.obj.type == ObjType::Well) {
                    std::string sub = lower_trim(arg);
                    uint8_t& water = c.obj.hp; // 0-100 water level
                    if (sub.empty() || sub == "look" || sub == "check") {
                        say("The well has " + std::to_string(water) + "% water remaining.");
                        return out;
                    } else if (sub == "draw" || sub == "fill" || sub == "refill") {
                        // Check if player has watering can
                        int can_slot = find_slot(p, Item::WateringCan);
                        if (can_slot < 0) { say("You need a watering can to draw water."); return out; }
                        if (water <= 0) { say("The well has run dry. Wait for rain."); return out; }
                        int space = 40 - p.inv[p.sel].count;
                        if (space <= 0) { say("Your watering can is already full."); return out; }
                        int draw = std::min(space, static_cast<int>(water));
                        water -= static_cast<uint8_t>(draw);
                        p.inv[p.sel].count += static_cast<uint16_t>(draw);
                        say("You draw " + std::to_string(draw) + " units of water. Well: " + std::to_string(water) + "% remaining.");
                        return out;
                    } else {
                        say("Try: 'interact look' to check water level, 'interact draw' to fill your watering can.");
                        return out;
                    }
                }
                // Pond interaction
                else if (c.obj.type == ObjType::Pond) {
                    std::string sub = lower_trim(arg);
                    if (sub.empty() || sub == "look") {
                        say("A tranquil pond. You can fish here with 'fish'.");
                        return out;
                    } else if (sub == "fill" || sub == "refill") {
                        int can_slot = find_slot(p, Item::WateringCan);
                        if (can_slot < 0) { say("You need a watering can."); return out; }
                        if (p.inv[p.sel].count >= 40) { say("Can is full."); return out; }
                        p.inv[p.sel].count = 40;
                        say("You fill your watering can from the pond.");
                        return out;
                    } else {
                        say("Try 'fish' to cast a line, or 'interact fill' to fill your can.");
                        return out;
                    }
                }
            }
            say("There's nothing to interact with out here. Try 'enter' at a building's door.");
            return out;
        }
        const std::string& b = p.inside;
        auto rit = w.interiors.find(b);
        if (rit != w.interiors.end()) {
            const InteriorRoom& room = rit->second;
            // Check adjacent furniture for context-specific interaction
            std::vector<std::pair<char, std::string>> adjacent;
            for (auto [ox, oy, dir] : std::vector<std::tuple<int,int,std::string>>{{0,-1,"north"},{0,1,"south"},{-1,0,"west"},{1,0,"east"}}) {
                int tx = p.inx + ox, ty = p.iny + oy;
                if (tx < 0 || tx >= room.w || ty < 0 || ty >= room.h) continue;
                char ch = room.rows[static_cast<size_t>(ty)][static_cast<size_t>(tx)];
                if (ch != '.' && ch != ' ' && ch != 'P' && ch != '#') {
                    adjacent.emplace_back(ch, dir);
                }
            }
            // Farmhouse furniture interactions
            if (b == "Farmhouse") {
                for (auto [ch, dir] : adjacent) {
                    if (ch == 'B') { // Bed
                        advance_day(w);
                        say("You crawl into bed and sleep...");
                        say("--- Day " + std::to_string(w.day) + " · " +
                            std::string(season_name(season_index(w.day))) + " ---");
                        say("You wake up refreshed. Energy restored.");
                        return out;
                    } else if (ch == 'V') { // TV
                        say(std::string("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓"));
                        say("    STARDROP VALLEY LOCAL NEWS 68");
                        say(std::string("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛"));
                        say("Anchor: \"Good morning, Valley! This is ..."
                            + std::string(season_name(season_index(w.day)))
                            + " " + std::to_string(season_day(w.day)) + ".\"");
                        int nd = w.weather_of_day_adapted(w.day + 1) == 1;
                        say("Weather: Tomorrow's forecast — " +
                            (nd ? std::string(weather_name_adapted(w, w.day + 1)) + ", bring a coat."
                                : std::string(weather_name_adapted(w, w.day + 1)) + ", skies will clear."));
                        std::vector<std::string> tips = {
                            "SPRING: Blueberry seeds (80g) sell for 100g — plant in Spring for a fat Summer harvest.",
                            "SUMMER: Preserve your blueberries & tomatoes in kegs. Quality stars sell for more.",
                            "FALL: Grapes hang heavy. Harvest before the first frost.",
                            "WINTER: The Travelling Cart visits Thurs–Sun this week. Prices are slashed.",
                        };
                        say("Correspondent: \"" + tips[static_cast<size_t>(season_index(w.day))] + "\"");
                        say("\"And now, the Stardew Valley Fair is coming...\"");
                        return out;
                    } else if (ch == 'S') { // Stove
                        say("The stove is warm. You could cook something here. (Use 'cook <recipe>')");
                        return out;
                    } else if (ch == 'C') { // Counter
                        say("A clean kitchen counter. Good for food prep.");
                        return out;
                    } else if (ch == 'F') { // Fridge
                        say("The fridge hums quietly. Stores perishables. (Not yet implemented)");
                        return out;
                    } else if (ch == 'X') { // Shelf
                        say("Shelves hold jars, preserves, and cookbooks. (Not yet implemented)");
                        return out;
                    } else if (ch == 'T') { // Table
                        say("The dining table is set. A place for meals and journaling.");
                        return out;
                    } else if (ch == 'G') { // Chair
                        say("You pull out a chair and sit. Comfortable.");
                        return out;
                    }
                }
                // Default Farmhouse interaction (sleep)
                advance_day(w);
                say("You crawl into bed and sleep...");
                say("--- Day " + std::to_string(w.day) + " · " +
                    std::string(season_name(season_index(w.day))) + " ---");
                say("You wake up refreshed. Energy restored.");
                return out;
            }

            // Horror location structure interactions (ROADMAP 1.1): each deep
            // place yields a fragment of the hidden narrative on 'interact'.
            if (b == "Witch's Hut") {
                for (auto [ch, dir] : adjacent) {
                    if (ch == 'Y') {
                        w.damage_sanity(p, 2.0f);
                        say("The scrolls are written in a hand you almost recognise. One reads: \"The first death is a door.\"");
                        return out;
                    } else if (ch == 'K') {
                        say("A kettle bubbles with something green. You decide not to ask what's inside.");
                        return out;
                    } else if (ch == 'T') {
                        w.damage_sanity(p, 2.0f);
                        say("You touch the mirror. It stays cold. Your reflection smiles a moment too long.");
                        return out;
                    }
                }
                say("The hut is cluttered with years of the Witch's waiting. Everything here knows something.");
                return out;
            }
            if (b == "Abandoned Sanitarium") {
                for (auto [ch, dir] : adjacent) {
                    if (ch == 'W') {
                        w.find_secret(p, "sanitarium_records");
                        say("A patient ledger lies open. Every name is crossed out except one — and it's yours, written in fresh ink.");
                        return out;
                    } else if (ch == 'D') {
                        say("The beds are stiff with dust. Each one has been slept in recently, despite the years.");
                        return out;
                    } else if (ch == 'I') {
                        say("Strange instruments. You don't recognise a single one, and you're not sure you want to.");
                        return out;
                    }
                }
                say("The sanitarium waits. It has been waiting since before you arrived.");
                return out;
            }
            if (b == "Ritual Circle") {
                for (auto [ch, dir] : adjacent) {
                    if (ch == 'A') {
                        w.find_secret(p, "ritual_altar");
                        say("The altar stone is warm. Symbols circle it that you've only seen in your dreams.");
                        return out;
                    } else if (ch == 'R') {
                        w.damage_sanity(p, 3.0f);
                        say("The candles flicker in a breeze that isn't there. They are counting something.");
                        return out;
                    }
                }
                say("The circle hums faintly, waiting to be completed.");
                return out;
            }
        }
        if (b == "General Store" || b == "Market") {
            say("Pierre's Seed Shop  (" + std::string(season_name(season_index(w.day))) + " prices):");
            say("  parsnip 20g · potato 50g · cauliflower 80g · corn 150g (summer/fall)");
            say("  tomato 50g (summer) · wheat 10g (spring..winter) · blueberry 80g (spring..fall)");
            say("  bread 5g (+30 energy)      'buy <item>' to order.");
        } else if (b == "Clinic") {
            if (p.money < 20) say("Doc Harvey charges 20g. You can't afford it.");
            else {
                p.money -= 20;
                p.energy = p.max_energy;
                say("Doc Harvey patches you up. -20g, energy restored.");
            }
        } else if (b == "Stardrop Saloon") {
            if (p.money < 15) say("Gus wants 15g for a hot meal. You're short.");
            else {
                p.money -= 15;
                p.energy = std::min<float>(p.max_energy, p.energy + 20);
                say("Gus serves a steaming plate of clam chowder. -15g, +20 energy.");
            }
        } else if (b == "Museum") {
            say("You browse the exhibits: a rusty trowel, a bottle of frozen lake water,");
            say("and a framed photo of someone who looks like Leah.");
        } else if (b == "Blacksmith") {
            say("Clint squints at your tools. 'Come back when you've found some ore,' he says.");
        } else if (b == "Old Mill") {
            say("You check the millruns. The gears are greased and turning.");
        } else if (b == "Town Center") {
            say("Bulletin board: 'Lost & Found: 7 red berries by Mirror Lake.'");
            say("              'Friday: potluck at the saloon. Bring a crop!'");
        } else if (b == "Bus Stop") {
            if (p.money < 25) say("The fare is 25g. You're 25g short of a seat.");
            else {
                p.money -= 25;
                say("The town bus rumbles in. You hop aboard and ride to Stardrop Plaza.");
                p.inside.clear();
                p.pos = {47, 17};
                p.target = p.pos;
                p.path.clear();
                p.moving = false;
            }
        } else if (b == "Railway Station") {
            say("You board the Zuzu City Express. The whistle blows, the carriages rock,");
            say("and you doze off in a plush seat as the train thunders through the night...");
            advance_day(w);
            say("--- Day " + std::to_string(w.day) + " · " +
                std::string(season_name(season_index(w.day))) + " ---");
            say("You step off the train back in Ashgrove.");
            p.inside.clear();
            p.pos = p.inside_exit;
            p.target = p.pos;
            p.path.clear();
            p.moving = false;
        } else {
            say("You look around the " + b + ". There's nothing special to do here.");
        }
        return out;
    }

    // ---------- train ----------
    if (cmd == "train") {
        if (p.inside != "Railway Station") {
            say("You're not at the railway station. Find the station platform first.");
            return out;
        }
        say("You board the Zuzu City Express. The whistle blows, the carriages rock,");
        say("and you doze off in a plush seat as the train thunders through the night...");
        advance_day(w);
        say("--- Day " + std::to_string(w.day) + " · " +
            std::string(season_name(season_index(w.day))) + " ---");
        say("You step off the train back in Ashgrove.");
        p.inside.clear();
        p.pos = p.inside_exit;
        p.target = p.pos;
        p.path.clear();
        p.moving = false;
        return out;
    }

    // ---------- bus ----------
    if (cmd == "bus") {
        if (p.inside != "Bus Stop") {
            say("You're not at the bus stop. Find the shelter first.");
            return out;
        }
        if (p.money < 25) { say("The fare is 25g. You're 25g short of a seat."); return out; }
        p.money -= 25;
        say("The town bus rumbles in. You hop aboard and ride to Stardrop Plaza.");
        p.inside.clear();
        p.pos = {47, 17};
        p.target = p.pos;
        p.path.clear();
        p.moving = false;
        return out;
    }

// ---------- TV / watch ----------
    if (cmd == "tv" || cmd == "watch" || cmd == "watch tv") {
        if (p.inside != "Farmhouse") {
            say("You flip the old switch. Nothing but static.");
            say("There's no TV here.");
            return out;
        }
        say(std::string("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓"));
        say("    STARDROP VALLEY LOCAL NEWS 68");
        say(std::string("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛"));
        say("Anchor: \"Good morning, Valley! This is ..."
            + std::string(season_name(season_index(w.day)))
            + " " + std::to_string(season_day(w.day)) + ".\"");
        // next-day weather peek
        int nd = w.weather_of_day_adapted(w.day + 1) == 1;
        say("Weather: Tomorrow's forecast — " +
            (nd ? std::string(weather_name_adapted(w, w.day + 1)) + ", bring a coat."
                : std::string(weather_name_adapted(w, w.day + 1)) + ", skies will clear."));
        // seasonal consequential tip
        std::vector<std::string> tips = {
            "SPRING: Blueberry seeds (80g) sell for 100g — plant in Spring for a fat Summer harvest.",
            "SUMMER: Preserve your blueberries & tomatoes in kegs. Quality stars sell for more.",
            "FALL: Grapes hang heavy. Harvest before the first frost.",
            "WINTER: The Travelling Cart visits Thurs–Sun this week. Prices are slashed.",
        };
        say("Correspondent: \"" + tips[static_cast<size_t>(season_index(w.day))] + "\"");
        say("\"And now, the Stardew Valley Fair is coming...\"");
        return out;
    }

    // ---------- sleep ----------
    if (cmd == "sleep" || cmd == "rest") {
        Vec2 d = w.door();
        if (p.inside != "Farmhouse" &&
            (std::abs(int(d.x) - p.pos.x) > 1 || std::abs(int(d.y) - p.pos.y) > 1)) {
            say("You're not near your bed. Find the house door."); return out;
        }
        std::string old_season = season_name(season_index(w.day));
        int sleep_hour = hour_of_day(w);   // before day rolls over
        advance_day(w);
        std::string new_season = season_name(season_index(w.day));
        int nextWeather = w.weather_of_day_adapted(w.day);
        say("Zzz...");
        // Phase 6: the night has its own story. Every sleep after midnight can
        // surface a chapter of the hidden narrative.
        // Phase 7.3: horror.night_event_weight scales how often the narrative surfaces.
        bool night_event_rolls = (sleep_hour >= 22);
        if (night_event_rolls && w.horror_night_event_weight < 1.0f) {
            int span = static_cast<int>(100.0f / std::max(0.01f, w.horror_night_event_weight));
            night_event_rolls = ((w.day * 9187u + 13u) % static_cast<unsigned>(std::max(1, span))) < 100u;
        }
        if (night_event_rolls) {
            std::string ev = w.roll_night_event();
            std::istringstream evss(ev);
            std::string line;
            while (std::getline(evss, line)) {
                if (!line.empty()) say(line);
            }
            if (p.night_event_log.size() > 12) p.night_event_log.erase(p.night_event_log.begin());
            p.night_event_log.push_back(ev);
            // ROADMAP 1.4: night events tilt the dread profile toward whispers.
            w.bump_dread(p, 2);
        }
        say("--- Day " + std::to_string(w.day) + " · " + std::string(new_season) + " ---");
        say(clock_str(w));
        if (new_season != old_season)
            say("You wake to the first day of " + std::string(new_season) + ". The air has changed.");
        if (nextWeather == 1)
            say("Rain is drumming on the roof. Your fields will be watered by the storm.");
        else
            say("You wake up refreshed. Energy restored.");
        return out;
    }

    // ---------- festival (Egg Festival, Spring 13) ----------
    if (cmd == "festival" || cmd == "fest") {
        if (!is_festival_day(w.day)) {
            say("There's no festival today. The Egg Festival comes every Spring 13.");
            return out;
        }
        if (p.fest_eggs >= 8) {
            say("The festival meadow is picked clean — you found all 8 eggs!");
            return out;
        }
        say("It's the Egg Festival! The whole town has gathered at the Town Center.");
        say("Eight decorated eggs are hidden in the 36 meadow patches.");
        say("Type 'search <patch 1-36>' to peek under one. You have " +
            std::to_string(p.fest_tries) + " searches left (" +
            std::to_string(p.fest_eggs) + "/8 eggs found).");
        return out;
    }
    if (cmd == "search") {
        // L9: Track tracking on snow/ice
        Cell& c = w.at(p.pos);
        if ((c.tile == Tile::Snow || c.tile == Tile::Ice) && c.track_age > 0) {
            if (p.energy < 2) { say("Too tired to examine tracks."); return out; }
            p.energy -= 2;
            static const char* type_names[] = {"", "player", "NPC", "deer", "rabbit", "predator"};
            static const char* dir_names[] = {"south", "west", "east", "north"};
            const char* type_name = (c.track_type < 6) ? type_names[c.track_type] : "creature";
            const char* dir_name = (c.track_dir >= 0 && c.track_dir < 4) ? dir_names[c.track_dir] : "unknown";
            int hours_old = c.track_age;
            say("You crouch and examine the tracks...");
            say("Tracks of a " + std::string(type_name) + " heading " + std::string(dir_name) + " (" + std::to_string(hours_old) + " hours old).");
            // Tracking skill gives bonus info
            int tracking_skill = 0; // could be a player skill later
            if (tracking_skill >= 1) {
                say("The tracks are " + std::string(hours_old < 6 ? "fresh" : hours_old < 24 ? "a day old" : "old") + ".");
            }
            return out;
        }
        
        // Egg Festival search (existing functionality)
        if (!is_festival_day(w.day)) {
            say("There's nothing to search for today. (Egg Festival: Spring 13)");
            return out;
        }
        if (p.fest_eggs >= 8) { say("You already found all 8 eggs!"); return out; }
        int patch = 0;
        try { patch = std::stoi(arg); } catch (...) {}
        if (patch < 1 || patch > 36) { say("Pick a patch 1-36. e.g. 'search 12'"); return out; }
        if (p.fest_tries <= 0) {
            say("You're out of searches. The festival is over for you.");
            return out;
        }
        p.fest_tries--;
        // deterministic layout for this festival day
        unsigned seed = w.day * 2654435761u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 16) & 0x7FFF; };
        bool egg[37] = {};
        int placed = 0;
        while (placed < 8) {
            int n = static_cast<int>(1 + rnd() % 36u);
            if (!egg[n]) { egg[n] = true; ++placed; }
        }
        if (egg[patch]) {
            p.fest_eggs++;
            int reward = 25;
            std::string line = "You lift a tuft of grass... a decorated egg! +" + std::to_string(reward) + "g (" +
                               std::to_string(p.fest_eggs) + "/8).";
            if (p.fest_eggs == 8) {
                reward += 200;
                line += " ALL EIGHT FOUND! The mayor cheers and hands you a bonus +200g!";
            }
            p.money += reward;
            say(line);
        } else {
            say("Just grass. (" + std::to_string(p.fest_eggs) + "/8 eggs so far, " +
                std::to_string(p.fest_tries) + " searches left)");
        }
        return out;
    }

    // ---------- save / load / newgame ----------
    auto name_ok = [](const std::string& n) {
        if (n.empty() || n.size() > 32) return false;
        for (char c : n)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
        return true;
    };
    if (cmd == "save") {
        if (!arg.empty() && !name_ok(arg)) {
            say("Save names: letters, digits, '_' or '-' only (max 32).");
            return out;
        }
        std::string f = arg.empty() ? "save.json" : "save_" + arg + ".json";
        if (save_world(w, f))
            say("Saved to " + f + " — " + std::string(clock_str(w)) + ".");
        else say("Could not write " + f + ".");
        return out;
    }
    if (cmd == "load") {
        if (!arg.empty() && !name_ok(arg)) {
            say("Save names: letters, digits, '_' or '-' only (max 32).");
            return out;
        }
        std::string f = arg.empty() ? "save.json" : "save_" + arg + ".json";
        World fresh;
        generate_world(fresh);           // buildings/interiors/house base
        init_npcs(fresh);
        fresh.init_wildlife();
        if (!load_world(fresh, f)) {
            say("No save file '" + f + "'. Type 'saves' to list them.");
            return out;
        }
        uint32_t pid = p.id;
        std::string pname = p.name;
        bool kept = fresh.players.count(pid) > 0;
        w = std::move(fresh);
        if (!kept)   // save belongs to another farmer: rejoin with a fresh kit
            w.players[pid] = make_fresh_player(pid, pname,
                {int16_t(w.door().x), int16_t(w.door().y + 1)});
        say("Loaded " + f + " — " + std::string(clock_str(w)) + ", " +
            std::string(season_name(season_index(w.day))) + " " +
            std::to_string(season_day(w.day)) + ".");
        return out;
    }
    if (cmd == "newgame") {
        std::time_t t = std::time(nullptr);
        std::tm lt;
        localtime_r(&t, &lt);
        char ts[32];
        std::snprintf(ts, sizeof ts, "%04d%02d%02d_%02d%02d%02d",
                      lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                      lt.tm_hour, lt.tm_min, lt.tm_sec);
        std::string backup = "save_" + std::string(ts) + ".json";
        if (fs::exists("save.json")) {
            fs::copy_file("save.json", backup, fs::copy_options::overwrite_existing);
            say("Previous save kept as " + backup + ".");
        }
        uint32_t pid = p.id;
        std::string pname = p.name;
        World fresh;
        generate_world(fresh);
        init_npcs(fresh);
        fresh.init_wildlife();
        fresh.players[pid] = make_fresh_player(pid, pname,
            {int16_t(fresh.door().x), int16_t(fresh.door().y + 1)});
        w = std::move(fresh);
        save_world(w, "save.json");
        say("New game — Spring 1, Day 1. A rundown farm is yours again.");
        return out;
    }
    if (cmd == "saves") {
        std::error_code ec;
        int n = 0;
        for (auto& e : fs::directory_iterator(".", ec)) {
            if (ec) break;
            if (!e.is_regular_file()) continue;
            std::string fn = e.path().filename().string();
            if (fn.rfind("save", 0) == 0 && fn.size() > 5 &&
                fn.compare(fn.size() - 5, 5, ".json") == 0) {
                say("  " + fn);
                ++n;
            }
        }
        if (!n) say("No save files found.");
        else say(std::to_string(n) + " save file(s). 'load <name>' restores one.");
        return out;
    }

    // ROADMAP 2.3 (8.1c) -- Seed breeding: cross two seeds to create offspring with combined genetics
    if (cmd == "breed") {
        // Try to match known seed names (which may contain spaces)
        static const std::vector<std::string> known_seeds = {
            "parsnip seeds", "potato seeds", "cauliflower seeds", "corn seeds",
            "tomato seeds", "wheat seeds", "blueberry seeds", "green bean seeds",
            "hops seeds", "strawberry seeds", "melon seeds", "pumpkin seeds",
            "red cabbage seeds", "rhubarb seeds"
        };
        std::string rest = lower_trim(arg);
        Item seed1 = Item::None, seed2 = Item::None;
        for (const auto& s : known_seeds) {
            if (rest.rfind(s, 0) == 0) {
                seed1 = item_from_name(s);
                rest = lower_trim(rest.substr(s.length()));
                break;
            }
        }
        if (seed1 != Item::None) {
            for (const auto& s : known_seeds) {
                if (rest.rfind(s, 0) == 0) {
                    seed2 = item_from_name(s);
                    break;
                }
            }
        }
        if (seed1 == Item::None || seed2 == Item::None) { say("Usage: breed <seed1> <seed2> (e.g. 'breed parsnip seeds parsnip seeds')"); return out; }
        int slot1 = find_slot(p, seed1);
        int slot2 = find_slot(p, seed2);
        if (slot1 < 0 || slot2 < 0) { say("You don't have both seeds in your inventory."); return out; }
        if (slot1 == slot2 && p.inv[static_cast<size_t>(slot1)].count < 2) {
            say("You need at least two seeds to breed."); return out;
        }
        // EA recombination: each child locus inherits from one parent (50/50)
        // with a 1% per-locus mutation chance.
        SeedGen g1 = get_seed_gen(p, seed1);
        SeedGen g2 = get_seed_gen(p, seed2);
        std::mt19937 rng(static_cast<unsigned>(w.day) * 224737u +
                         static_cast<uint32_t>(p.pos.x) * 1009u +
                         static_cast<uint32_t>(p.pos.y) * 2027u);
        std::uniform_int_distribution<int> inherit(0, 1);
        std::uniform_int_distribution<int> mroll(0, 99);
        std::uniform_int_distribution<int> mv(-2, 2);
        SeedGen child;
        for (size_t i = 0; i < child.alleles.size(); ++i) {
            child.alleles[i] = (inherit(rng) == 0) ? g1.alleles[i] : g2.alleles[i];
            if (mroll(rng) < 1) child.alleles[i] = static_cast<int8_t>(
                std::clamp(static_cast<int>(child.alleles[i]) + mv(rng), -40, 40));
        }
        child.homozygosity = compute_homozygosity(child.alleles);
        consume_item(p, seed1, 1);
        consume_item(p, seed2, 1);
        p.seed_gen[seed1] = child;
        add_item(p, seed1, 1);
        say("You carefully cross-pollinate two " + std::string(item_def(seed1).name) +
            " lots. A new seed forms with recombined genetics! (homozygosity " +
            std::to_string(child.homozygosity) + "/255)");
        return out;
    }

    // ---------- animal & farming extensions ----------
    if (cmd == "build") {
        if (arg == "barn") {
            // find or create a barn building for player
            say("You construct a barn. It appears near your farmhouse.");
            // simple: add a building named "Barn" if not exists
            bool exists = false;
            for (auto& b : w.buildings) if (b.name == "Barn") { exists = true; break; }
            if (!exists) {
                Bldg barn;
                barn.name = "Barn";
                barn.x = w.house_tl.x - 10;
                barn.y = w.house_tl.y + 5;
                barn.w = 6; barn.h = 4;
                barn.door_x = 2; barn.door_y = 3;
                const int16_t bid = static_cast<int16_t>(w.buildings.size());
                w.buildings.push_back(barn);
                w.building_states["Barn"] = BuildingState{};
                // Mark cells with building_id and initialize fire fuel (wood)
                for (int y = barn.y; y < barn.y + barn.h; ++y)
                    for (int x = barn.x; x < barn.x + barn.w; ++x) {
                        if (!w.in_bounds(x, y)) continue;
                        Cell& c = w.at(x, y);
                        c.building_id = bid;
                        c.fire_fuel = 70;  // wooden barn
                    }
            }
            say("Barn ready. Use 'place chicken Barn' to add livestock.");
        } else if (arg == "coop") {
            say("You build a chicken coop.");
            bool exists = false;
            for (auto& b : w.buildings) if (b.name == "Coop") { exists = true; break; }
            if (!exists) {
                Bldg coop;
                coop.name = "Coop";
                coop.x = w.house_tl.x - 8;
                coop.y = w.house_tl.y + 3;
                coop.w = 4; coop.h = 3;
                coop.door_x = 1; coop.door_y = 1;
                const int16_t bid = static_cast<int16_t>(w.buildings.size());
                w.buildings.push_back(coop);
                w.building_states["Coop"] = BuildingState{};
                // Mark cells with building_id and initialize fire fuel (wood)
                for (int y = coop.y; y < coop.y + coop.h; ++y)
                    for (int x = coop.x; x < coop.x + coop.w; ++x) {
                        if (!w.in_bounds(x, y)) continue;
                        Cell& c = w.at(x, y);
                        c.building_id = bid;
                        c.fire_fuel = 65;  // wooden coop
                    }
            }
            say("Coop ready. Use 'place chicken Coop' to add chickens.");
        } else {
            say("Unknown building. Try: build barn / build coop");
        }
        return out;
    }
    if (cmd == "place") {
        // place <animal> <building>
        std::string animal = arg;
        std::string building_name;
        if (words.size() >= 3) building_name = words[2];
        if (building_name.empty()) { say("Usage: place chicken Barn"); return out; }
        // find building
        Bldg* target = nullptr;
        for (auto& b : w.buildings) if (b.name == building_name) { target = &b; break; }
        if (!target) { say("Building not found."); return out; }
        auto& state = w.building_states[building_name];
        // find empty slot
        bool placed = false;
        for (auto& a : state.animals) {
            if (a.type == AnimalType::None) {
                if (animal == "chicken") a.type = AnimalType::Chicken;
                else if (animal == "cow") a.type = AnimalType::Cow;
                else if (animal == "goat") a.type = AnimalType::Goat;
                else { say("Unknown animal."); return out; }
                a.age = 0; a.hunger = 0; a.days_since_product = 0;
                placed = true;
                break;
            }
        }
        if (placed) say("You place a " + animal + " in the " + building_name + ".");
        else say("No space in " + building_name + ".");
        return out;
    }
    if (cmd == "collect") {
        // collect eggs / milk / goatmilk from all buildings
        int eggs = 0, milk = 0, goatmilk = 0;
        for (auto& [name, state] : w.building_states) {
            for (auto& a : state.animals) {
                if (a.type == AnimalType::Chicken && a.days_since_product >= Animal::interval(AnimalType::Chicken)) {
                    eggs++; a.days_since_product = 0;
                } else if (a.type == AnimalType::Cow && a.days_since_product >= Animal::interval(AnimalType::Cow)) {
                    milk++; a.days_since_product = 0;
                } else if (a.type == AnimalType::Goat && a.days_since_product >= Animal::interval(AnimalType::Goat)) {
                    goatmilk++; a.days_since_product = 0;
                }
            }
        }
        if (eggs) { add_item(p, Item::Egg, static_cast<uint16_t>(eggs)); say("Collected " + std::to_string(eggs) + " egg" + (eggs>1?"s":"") + "."); }
        if (milk) { add_item(p, Item::Milk, static_cast<uint16_t>(milk)); say("Collected " + std::to_string(milk) + " milk."); }
        if (goatmilk) { add_item(p, Item::GoatMilk, static_cast<uint16_t>(goatmilk)); say("Collected " + std::to_string(goatmilk) + " goat milk."); }
        if (!eggs && !milk && !goatmilk) say("Nothing ready to collect yet.");
        return out;
    }
    if (cmd == "feed") {
        // simple: reduce hunger for all animals in all buildings
        for (auto& [name, state] : w.building_states) {
            for (auto& a : state.animals) if (a.type != AnimalType::None) a.hunger = 0;
        }
        say("All animals fed.");
        return out;
    }
    if (cmd == "fish") {
        // simple fishing: if near water tile
        bool near_water = false;
        for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
            int tx = p.pos.x + dx, ty = p.pos.y + dy;
            if (w.in_bounds(tx,ty) && is_water_any(w.at(tx,ty).tile)) near_water = true;
        }
        if (!near_water) { say("No water nearby."); return out; }
        // simple catch chance
        int r = (static_cast<int>(w.day)*7 + p.pos.x*13 + p.pos.y*19) % 100;
        Item catch_item = Item::Fish;
        if (r < 10) catch_item = Item::Fish; // placeholder
        add_item(p, catch_item, 1);
        say("You cast your line and reel in a " + std::string(item_def(catch_item).name) + "!");
        return out;
    }
    if (cmd == "cook") {
        // cook <recipe>
        if (arg.empty()) { say("Usage: cook bread"); return out; }
        if (arg == "bread") {
            if (has_item(p, Item::Wheat, 1)) {
                consume_item(p, Item::Wheat, 1);
                add_item(p, Item::Bread, 1);
                say("You bake a loaf of bread.");
            } else say("You need wheat to bake bread.");
        } else {
            say("Unknown recipe.");
        }
        return out;
    }
    if (cmd == "festival") {
        // simple seasonal festival trigger
        int season_local = season_index(w.day);
        if (season_local == 0) say("🌸 Spring Festival begins! Villagers gather at the plaza.");
        else if (season_local == 1) say("☀️ Summer Luau! Beach party tonight.");
        else if (season_local == 2) say("🍂 Autumn Harvest Festival! Feast and games.");
        else say("❄️ Winter Star Festival! Lights and songs.");
        return out;
    }

    say("I don't understand '" + cmd + "'. Type 'help' for commands.");
    // Suggest similar commands
    auto suggestions = suggest_commands(cmd);
    if (!suggestions.empty()) {
        std::string hint = "Did you mean: ";
        for (size_t i = 0; i < suggestions.size(); ++i) {
            if (i > 0) hint += ", ";
            hint += suggestions[i];
        }
        hint += "?";
        say(hint);
    }
    return out;
}

std::vector<std::string> process_intent(World& w, Player& p, const nlohmann::json& intent) {
    std::string action = intent.value("action", "");
    auto params = intent.value("parameters", nlohmann::json::object());

    // For now, reconstruct a simple command string for fallback handling
    std::string cmd_str = action;
    if (!params.empty()) {
        // naive: append first param value
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (it->is_string()) cmd_str += " " + it->get<std::string>();
            else cmd_str += " " + it->dump();
        }
    }
    return handle_cmd(w, p, cmd_str);
}

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    World world;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        generate_world(world);
        if (load_or_generate(world)) std::cout << "Loaded save\n";
        else std::cout << "New world generated\n";
        init_npcs(world);
        world.init_wildlife();
        world.init_atmosphere();   // ROADMAP 2.6 (8.2): lazily build the synoptic grid
    }

    // ---- locate or download a GGUF model for the local LLM ----
    auto find_model = []() -> std::string {
        // 1) explicit file next to the executable
        std::vector<std::filesystem::path> candidates = {
            // Phase 8: Fine-tuned Ashgrove student (Qwen2.5-0.5B LoRA Q4_K_M)
            std::filesystem::path("/home/umang/ashgrove/data/qwen2.5-0.5b-ashgrove-q4_k_m.gguf"),
            // Preferred Gemma-4b model
            std::filesystem::path("/home/umang/llama.cpp/models/gemma-4-E4B-it-Q4_K_M.gguf"),
            std::filesystem::current_path() / "models" / "model.gguf",
            std::filesystem::current_path() / "model.gguf",
            // local llama.cpp checkout (user may have models there)
            std::filesystem::path("/home/umang/llama.cpp/qwen2.5-coder-3b-instruct-q4_k_m.gguf"),
            std::filesystem::path("/home/umang/llama.cpp/models/qwen2.5-coder-3b-instruct-q4_k_m.gguf"),
            // llama.cpp source fetched by CMake (may contain example models)
            std::filesystem::path(__FILE__).parent_path().parent_path() / "_deps" / "llama_cpp-src" / "models"
        };
        for (auto& p : candidates) {
            if (std::filesystem::is_regular_file(p)) return p.string();
            if (std::filesystem::is_directory(p)) {
                for (auto& entry : std::filesystem::directory_iterator(p)) {
                    if (entry.path().extension() == ".gguf") return entry.path().string();
                }
            }
        }
        return "";
    };

    auto download_model = [](const std::string& url, const std::string& dst) -> bool {
        // Very small download helper using httplib (same as join endpoint)
        std::string host, path;
        std::string u = url;
        if (u.rfind("https://",0)==0) u = u.substr(8);
        else if (u.rfind("http://",0)==0) u = u.substr(7);
        size_t pos = u.find('/');
        if (pos == std::string::npos) return false;
        host = u.substr(0, pos);
        path = u.substr(pos);
        httplib::Client cli(host.c_str());
        cli.set_follow_location(true);
        auto res = cli.Get(path.c_str());
        if (!res || res->status != 200) return false;
        std::ofstream ofs(dst, std::ios::binary);
        if (!ofs) return false;
        ofs.write(res->body.data(), static_cast<std::streamsize>(res->body.size()));
        return true;
    };

    constexpr const char* DEFAULT_MODEL_URL =
        "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-GGUF/resolve/main/tinyllama-1.1b-chat.Q4_K_M.gguf";

    std::string model_path = find_model();
    if (model_path.empty()) {
        std::filesystem::create_directories(std::filesystem::current_path() / "models");
        model_path = (std::filesystem::current_path() / "models" / "model.gguf").string();
        std::cout << "Downloading default LLM model (" << DEFAULT_MODEL_URL << ") …\n";
        if (!download_model(DEFAULT_MODEL_URL, model_path)) {
            std::cerr << "WARNING: model download failed – LLM will run in fallback mode.\n";
            model_path.clear();
        }
    } else {
        std::cout << "Using LLM model: " << model_path << "\n";
    }

    EventBus bus;
    LlamaWrapper llama(model_path);

    // Phase 7: Town Consciousness
    std::function<std::string(const std::string&, int, float)> llm_callback = [&llama](const std::string& prompt, int max_tokens, float temp) -> std::string {
        return llama.infer(prompt, max_tokens, temp);
    };
    // ROADMAP 1.7d: expose the same LLM to the cognitive dialogue path.
    g_dialogue_llm = llm_callback;
    TownConsciousness town_consciousness(world, llm_callback);

// Phase 7.7: Cognitive Core — initialize registry and create cores for important NPCs.
    ashgrove::CognitiveRegistry& cog_registry = ashgrove::CognitiveRegistry::instance();
    const std::vector<std::string> important_npcs = {
        "Mayor", "Witch", "Traveler", "Doctor", "Teacher", "Carpenter", "Farmer"
    };
    for (const auto& name : important_npcs) {
        ashgrove::CognitiveCore& core = cog_registry.get_or_create("npc:" + name);
        core.mutable_state().agent_id = "npc:" + name;
        core.mutable_state().created_tick = static_cast<uint32_t>(now_ms());
        core.set_lod(ashgrove::LodLevel::Full);
        // Load persisted state if exists.
        core.load("data/npc_cognitive_state");
    }
    // ROADMAP 1.7a: Cognitive LOD -- talkable villagers get Lightweight cores
    // (enough state for cognitive dialogue; ticked every ~10 ticks). They must
    // exist so the talk handler can source cognitive state for the LLM line.
    const std::vector<std::string> villager_npcs = {
        "Leah", "Abigail", "Elliot", "Robin", "Evelyn"
    };
    for (const auto& name : villager_npcs) {
        ashgrove::CognitiveCore& core = cog_registry.get_or_create("npc:" + name);
        core.mutable_state().agent_id = "npc:" + name;
        core.mutable_state().created_tick = static_cast<uint32_t>(now_ms());
        core.set_lod(ashgrove::LodLevel::Lightweight);
        core.load("data/npc_cognitive_state");
    }
    // ROADMAP 1.7a: background wildlife (rabbits) are Statistical -- no per-agent
    // cognitive state; the registry simply won't create cores for them.

    // Phase 7.9: Nature Mind — aggregate forest ecology cognition.
    ashgrove::NatureMind nature_mind(&world);

    // Phase 7.9: Village Mind — aggregate NPC collective cognition.
    ashgrove::VillageMind village_mind(&world, &cog_registry);

    // Phase 7.9: Economy Mind — aggregate commodity-cycle cognition.
    ashgrove::EconomyMind economy_mind(&world);

    // Phase 7.9: Culture Mind — aggregate collective-culture cognition.
    ashgrove::CultureMind culture_mind(&world, &cog_registry);

    // ROADMAP 1.2: Valley Mind — aggregate Valley Entity (genius loci)
    // cognition; drives the collective-guilt -> corruption -> horror loop.
    ashgrove::ValleyMind valley_mind(&world);

    // Phase 8: tiered intent engine (rule fast path first, LLM fallback) plus
    // the command log collector that builds the training dataset (data/cmdlog.jsonl).
    IntentEngine intent_engine;
    intent_engine.set_llm_backend([&llama](const std::string& raw) -> std::optional<Intent> {
        return llama.parse_command(raw);
    });
    CommandLog cmdlog(std::filesystem::current_path() / "data" / "cmdlog.jsonl");
    if (cmdlog.enabled()) std::cout << "Command log -> data/cmdlog.jsonl\n";

    // precompute static tile map (never changes after gen)
    std::vector<uint8_t> tile_map(MAP_W * MAP_H);
    for (int i = 0; i < MAP_W * MAP_H; ++i) tile_map[static_cast<size_t>(i)] = static_cast<uint8_t>(world.cells[static_cast<size_t>(i)].tile);

    httplib::Server svr;
    svr.set_base_dir("assets");

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        auto path = fs::path("assets/index.html");
        if (fs::exists(path)) {
            std::ifstream f(path);
            std::ostringstream ss;
            ss << f.rdbuf();
            res.set_content(ss.str(), "text/html");
        } else res.status = 404;
    });

    // ---- join ----
    svr.Post("/join", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        std::string name = j.value("name", "Player");
        std::lock_guard<std::mutex> lock(g_mutex);
        uint32_t pid;
        if (world.players.size() == 1 && world.next_player_id > 1) {
            // single-player reload: reattach to the saved farmer (preserve state)
            pid = world.players.begin()->first;
            world.players[pid].name = name;
            // update only the name; keep saved inventory, money, energy, hearts, etc.
        } else {
            pid = world.next_player_id++;
            world.players[pid] = make_fresh_player(
                pid, name, {int16_t(world.door().x), int16_t(world.door().y + 1)});
        }
        json resp = make_join_ack(pid, tile_map);
        resp["spawn"] = {{"x", world.players[pid].pos.x}, {"y", world.players[pid].pos.y}};
        resp["house"] = {{"x", world.house_tl.x}, {"y", world.house_tl.y}};
        resp["buildings"] = json::array();
        for (auto& b : world.buildings)
            resp["buildings"].push_back({{"name", b.name}, {"x", b.x}, {"y", b.y},
                                         {"w", b.w}, {"h", b.h}});
        resp["day"] = world.day;
        resp["time"] = world.day_seconds;
        resp["season"] = season_name(season_index(world.day));
        resp["season_i"] = season_index(world.day);
        resp["weather"] = weather_of_day(world.day);
        resp["welcome"] = "Welcome to Ashgrove Valley, " + name + "!\n"
            "A rundown farm is yours now. The soil is rich and the river runs clean.\n"
            "Type 'help' for commands. Start by saying 'look'.";
        res.set_content(resp.dump(), "application/json");
    });

    // ---- state ----
svr.Get("/state", [&](const httplib::Request&, httplib::Response& res) {
        // Always ensure we send valid JSON, even on error
        auto send_json = [&](const json& j) {
            std::string body = j.dump();
            if (body.empty()) body = "{}";
            res.set_content(body, "application/json");
        };

        try {
            std::lock_guard<std::mutex> lock(g_mutex);
            std::vector<json> plist;
            for (auto& [id, p] : world.players) {
                json inv = json::array();
                for (int i = 0; i < 12; ++i)
                    inv.push_back({{"item", static_cast<int>(p.inv[static_cast<size_t>(i)].item)},
                                   {"count", p.inv[static_cast<size_t>(i)].count}});
                plist.push_back({
                    {"player_id", id}, {"x", p.pos.x}, {"y", p.pos.y}, {"dir", p.dir},
                    {"moving", p.moving}, {"name", p.name}, {"energy", p.energy},
                    {"max_energy", p.max_energy}, {"money", p.money}, {"sel", p.sel},
                    {"region", region_at(world, p.pos.x, p.pos.y)},
                    {"inside", p.inside}, {"inx", p.inx}, {"iny", p.iny},
                    {"inv", inv},
                    {"sanity", p.sanity}, {"sanity_tier", world.perception_tier(p)},
                    {"health", p.health}, {"max_health", p.max_health},
                    {"death_count", p.death_count},
                    {"pending_death", p.pending_death},
                });
                // Surface pending time-based death narration exactly once.
                if (!p.pending_death.empty()) p.pending_death.clear();
            // attach friendship hearts
            json hearts = json::object();
            for (auto& [nm, h] : p.hearts) hearts[nm] = h;
            plist.back()["hearts"] = hearts;
            }
            json cells = json::array();
            for (int y = 0; y < MAP_H; ++y)
                for (int x = 0; x < MAP_W; ++x) {
                    const Cell& c = world.at(x, y);
                    if (c.obj.type != ObjType::None || c.crop.is_crop() ||
                        c.tile != Tile::Grass) {
                        json cj{{"x", x}, {"y", y},
                                {"tile", static_cast<int>(c.tile)},
                                {"obj", static_cast<int>(c.obj.type)}, {"hp", c.obj.hp},
                                {"ore", c.obj.ore}};
                        // ROADMAP 2.2: expose water table data
                        cj["water_table_depth"] = c.water_table_depth;
                        cj["saturation"] = c.saturation;
                        if (c.crop.is_crop()) {
                            cj["crop"] = static_cast<int>(c.crop.crop);
                            cj["stage"] = c.crop.stage;
                            cj["days_left"] = c.crop.days_left;
                            cj["watered"] = c.crop.watered;
                        }
                        cells.push_back(cj);
                    }
                }
            json resp = make_world_state(plist);
            resp["day"] = world.day;
            resp["time"] = world.day_seconds;
            resp["season"] = season_name(season_index(world.day));
            resp["season_i"] = season_index(world.day);
resp["weather"] = world.weather_of_day_adapted(world.day);
            // ROADMAP 2.6 (8.2): atmosphere grid summary + per-player local weather
            world.init_atmosphere();
            {
                json at = json::object();
                int n_storm = 0, n_rain = 0, n_fog = 0, n_clear = 0, tmin = 255, tmax = 0;
                for (int ay = 0; ay < World::ATMO_H; ++ay)
                    for (int ax = 0; ax < World::ATMO_W; ++ax) {
                        const int wx = ax * World::ATMO_CELL + 2;
                        const int wy = ay * World::ATMO_CELL + 2;
                        const int wd = world.weather_at(wx, wy);
                        if (wd == 3) ++n_storm;
                        else if (wd == 1) ++n_rain;
                        else if (wd == 2) ++n_fog;
                        else ++n_clear;
                        tmin = std::min(tmin, world.temp_here(wx, wy));
                        tmax = std::max(tmax, world.temp_here(wx, wy));
                    }
                at["day"] = world.atmos_day;
                at["storm_cells"] = n_storm;
                at["rain_cells"] = n_rain;
                at["fog_cells"] = n_fog;
                at["clear_cells"] = n_clear;
                at["temp_min"] = tmin;
                at["temp_max"] = tmax;
                at["weather_local"] = json::object();
                for (auto& [id, p] : world.players)
                    at["weather_local"][std::to_string(id)] = world.weather_at(p.pos.x, p.pos.y);
                resp["atmos"] = at;
            }
            resp["festival"] = is_festival_day(world.day) ? "Egg Festival" : "";
            resp["cells"] = cells;
            json npc_list = json::array();
            for (auto& n : world.npcs)
                npc_list.push_back({{"name", n.name}, {"x", n.pos.x}, {"y", n.pos.y},
                                    {"color", n.color}});
            resp["npcs"] = npc_list;
            resp["house"] = {{"x", world.house_tl.x}, {"y", world.house_tl.y}};
            resp["interiors"] = json::object();
            for (auto& [name, ir] : world.interiors)
                resp["interiors"][name] = ir.rows;
            resp["buildings"] = json::array();
            for (auto& b : world.buildings)
                resp["buildings"].push_back({{"name", b.name}, {"x", b.x}, {"y", b.y},
                                             {"w", b.w}, {"h", b.h}});
            
            send_json(resp);
        } catch (const std::exception& e) {
            std::cerr << "[/state] error: " << e.what() << std::endl;
            send_json({{"error", "internal server error"}});
        }
    });

    // ---- town/nature ----
    svr.Get("/town/nature", [&](const httplib::Request&, httplib::Response& res) {
        auto send_json = [&](const json& j) {
            std::string body = j.dump();
            if (body.empty()) body = "{}";
            res.set_content(body, "application/json");
        };
        try {
            auto snap = nature_mind.get_snapshot();
            json resp;
            resp["day"] = snap.day;
            resp["mean_succession_stage"] = snap.mean_succession_stage;
            resp["total_carbon_stock"] = snap.total_carbon_stock;
            resp["biodiversity_shannon"] = snap.biodiversity_shannon;
            resp["mean_disturbance_legacy"] = snap.mean_disturbance_legacy;
            resp["climate_velocity"] = snap.climate_velocity;
            resp["allele_frequencies"] = snap.allele_frequencies;
            resp["procgen_biases"] = snap.procgen_biases;
            resp["weather_storm_bias"] = snap.weather_storm_bias;
            resp["disaster_chance_bias"] = snap.disaster_chance_bias;
            resp["foraging_yield"] = snap.foraging_yield;
            send_json(resp);
        } catch (const std::exception& e) {
            std::cerr << "[/town/nature] error: " << e.what() << std::endl;
            send_json({{"error", "internal server error"}});
        }
    });

    // ---- town/village ----
    svr.Get("/town/village", [&](const httplib::Request&, httplib::Response& res) {
        auto send_json = [&](const json& j) {
            std::string body = j.dump();
            if (body.empty()) body = "{}";
            res.set_content(body, "application/json");
        };
        try {
            auto snap = village_mind.get_snapshot();
            json resp;
            resp["day"] = snap.day;
            resp["npc_count"] = snap.npc_count;
            resp["mean_valence"] = snap.mean_valence;
            resp["mean_arousal"] = snap.mean_arousal;
            resp["average_edge_trust"] = snap.average_edge_trust;
            resp["collective_fear"] = snap.collective_fear;
            resp["collective_joy"] = snap.collective_joy;
            resp["schedule_bias"] = snap.schedule_bias;
            resp["market_volatility"] = snap.market_volatility;
            resp["horror_night_event_weight"] = snap.horror_night_event_weight;
            resp["horror_intensity"] = snap.horror_intensity;
            json mem = json::array();
            for (const auto& rec : snap.recent_memory) {
                mem.push_back({{"day", rec.day},
                               {"event_type", rec.event_type},
                               {"detail", rec.detail},
                               {"emotional_weight", rec.emotional_weight}});
            }
            resp["recent_memory"] = mem;
            send_json(resp);
        } catch (const std::exception& e) {
            std::cerr << "[/town/village] error: " << e.what() << std::endl;
            send_json({{"error", "internal server error"}});
        }
    });

    // ---- /valley (ROADMAP 1.2) — Valley Entity diagnostic endpoint ----
    // Hidden from the normal player loop (not surfaced in /status), but
    // exposed so the mechanic can be verified to actually run end-to-end.
    svr.Get("/valley", [&](const httplib::Request&, httplib::Response& res) {
        auto send_json = [&](const json& j) {
            std::string body = j.dump();
            if (body.empty()) body = "{}";
            res.set_content(body, "application/json");
        };
        try {
            auto snap = valley_mind.get_snapshot();
            json resp;
            resp["day"] = snap.day;
            resp["collective_guilt"] = snap.collective_guilt;
            resp["valley_awakening"] = snap.valley_awakening;
            resp["corruption_density"] = snap.corruption_density;
            resp["horror_intensity"] = snap.horror_intensity;
            resp["horror_sanity_drain_multiplier"] = snap.horror_sanity_drain_multiplier;
            resp["weather_fog_intensity"] = snap.weather_fog_intensity;
            resp["horror_phantom_sighting_chance"] = snap.horror_phantom_sighting_chance;
            resp["horror_cycle"] = snap.horror_cycle;
            // ROADMAP 1.4 — dread profile (per-player diagnostics).
            resp["dread_bias_theme"] = snap.dread_bias_theme;
            json dcs = json::array();
            for (uint8_t t = 0; t < 4; ++t) dcs.push_back(snap.dread_counters[t]);
            resp["dread_counters"] = dcs;
            json ev = json::array();
            for (const auto& s : snap.recent_events) ev.push_back(s);
            resp["recent_events"] = ev;
            send_json(resp);
        } catch (const std::exception& e) {
            std::cerr << "[/valley] error: " << e.what() << std::endl;
            send_json({{"error", "internal server error"}});
        }
    });

    // ---- /cog: ROADMAP 1.7a/c cognitive LOD + causal-trace diagnostics ----
    svr.Get("/cog", [&](const httplib::Request& req, httplib::Response& res) {
        auto send_json = [&](const json& j) {
            std::string body = j.dump();
            if (body.empty()) body = "{}";
            res.set_content(body, "application/json");
        };
        try {
            // Optional ?agent=npc:Leah filter; default lists all cores.
            std::string want = req.has_param("agent") ? req.get_param_value("agent") : "";
            auto& reg = ashgrove::CognitiveRegistry::instance();
            json resp = json::object();
            json agents = json::array();
            // Rebuild from the singleton's cores via get_or_create + state().
            // (Registry has no public iteration; enumerate the known NPC set.)
            const std::vector<std::string> names = {
                "Mayor", "Witch", "Traveler", "Doctor", "Teacher", "Carpenter", "Farmer",
                "Leah", "Abigail", "Elliot", "Robin", "Evelyn"
            };
            for (const auto& nm : names) {
                std::string id = "npc:" + nm;
                if (!want.empty() && want != id && want != nm) continue;
                auto& core = reg.get_or_create(id);
                const ashgrove::CognitiveState& s = core.state();
                json a;
                a["agent_id"] = id;
                a["lod"] = static_cast<int>(core.lod());
                a["mean_valence"] = s.mean_valence;
                a["mean_arousal"] = s.mean_arousal;
                a["episodic_count"] = s.episodic_memory.size();
                a["working_count"] = s.working_memory.size();
                a["semantic_count"] = s.semantic_memory.size();
                a["social_count"] = s.social_graph.size();
                // ROADMAP 1.7c: last causal trace(s).
                json traces = json::array();
                for (const auto& t : core.causal_traces()) {
                    json tr;
                    tr["tick"] = t.tick;
                    tr["action"] = static_cast<int>(t.chosen_action);
                    tr["action_score"] = t.chosen_score;
                    tr["dominant_emotion"] = t.dominant_emotion_name;
                    json urg = json::array();
                    for (float u : t.drive_urgency) urg.push_back(u);
                    tr["drive_urgency"] = urg;
                    json stim = json::array();
                    for (const auto& st : t.top_stimuli) stim.push_back(st);
                    tr["top_stimuli"] = stim;
                    traces.push_back(tr);
                }
                a["causal_traces"] = traces;
                agents.push_back(a);
            }
            resp["agents"] = agents;
            send_json(resp);
        } catch (const std::exception& e) {
            std::cerr << "[/cog] error: " << e.what() << std::endl;
            send_json({{"error", "internal server error"}});
        }
    });

    // ---- town/economy ----
    svr.Get("/town/economy", [&](const httplib::Request&, httplib::Response& res) {
        auto send_json = [&](const json& j) {
            std::string body = j.dump();
            if (body.empty()) body = "{}";
            res.set_content(body, "application/json");
        };
        try {
            auto snap = economy_mind.get_snapshot();
            json resp;
            resp["day"] = snap.day;
            resp["inflation_rate"] = snap.inflation_rate;
            resp["trade_route_health"] = snap.trade_route_health;
            resp["price_elasticity"] = snap.price_elasticity;
            resp["market_volatility"] = snap.market_volatility;
            resp["average_price_ratio"] = snap.average_price_ratio;
            resp["demand_shift"] = snap.demand_shift;
            resp["commodity_volatility"] = snap.commodity_volatility;
            resp["cycle_drivers"] = snap.cycle_drivers;
            send_json(resp);
        } catch (const std::exception& e) {
            std::cerr << "[/town/economy] error: " << e.what() << std::endl;
            send_json({{"error", "internal server error"}});
        }
    });

    // ---- town/culture ----
    svr.Get("/town/culture", [&](const httplib::Request&, httplib::Response& res) {
        auto send_json = [&](const json& j) {
            std::string body = j.dump();
            if (body.empty()) body = "{}";
            res.set_content(body, "application/json");
        };
        try {
            auto snap = culture_mind.get_snapshot();
            json resp;
            resp["day"] = snap.day;
            resp["cultural_cohesion"] = snap.cultural_cohesion;
            resp["collective_fear"] = snap.collective_fear;
            resp["collective_joy"] = snap.collective_joy;
            resp["schedule_bias"] = snap.schedule_bias;
            resp["dialogue_topic_weight"] = snap.dialogue_topic_weight;
            resp["shared_beliefs"] = snap.shared_beliefs;
            resp["shared_fears"] = snap.shared_fears;
            resp["practice_frequency"] = snap.practice_frequency;
            send_json(resp);
        } catch (const std::exception& e) {
            std::cerr << "[/town/culture] error: " << e.what() << std::endl;
            send_json({{"error", "internal server error"}});
        }
    });

    // ---- move (BFS) ----
    svr.Post("/move", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        int16_t tx = static_cast<int16_t>(j.value("target_x", 0));
        int16_t ty = static_cast<int16_t>(j.value("target_y", 0));
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (!p.inside.empty()) { res.set_content("{}", "application/json"); return; }
            std::vector<Vec2> path;
            if (bfs_path(world, p.pos, {tx, ty}, path)) {
                p.path = std::move(path);
                p.moving = !p.path.empty();
                p.move_start_ms = static_cast<uint32_t>(now_ms());
                int dx = tx - p.pos.x, dy = ty - p.pos.y;
                if (std::abs(dx) > std::abs(dy)) p.dir = dx > 0 ? 2 : 1;
                else p.dir = dy > 0 ? 0 : 3;
            }
        }
        res.set_content("{}", "application/json");
    });

    // ---- dev warp ----
    svr.Post("/warp", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        int16_t wx = static_cast<int16_t>(j.value("x", 0)), wy = static_cast<int16_t>(j.value("y", 0));
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            p.inside.clear();
            p.path.clear();
            p.moving = false;
            p.dir = 0;  // reset to facing south (default)
            if (world.walkable(wx, wy)) {
                p.pos = {wx, wy};
                p.target = p.pos;
            }
        }
        res.set_content("{}", "application/json");
    });

    // ---- action (tool use, harvest, plant, slot select) ----
    svr.Post("/action", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        int16_t tx = static_cast<int16_t>(j.value("x", -9999));
        int16_t ty = static_cast<int16_t>(j.value("y", -9999));
        std::string msg;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (j.contains("sel")) p.sel = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(j["sel"]), 0, 11));
            if (tx != -9999 && ty != -9999) {
                int dx = std::abs(int(tx) - p.pos.x), dy = std::abs(int(ty) - p.pos.y);
                if (dx > 1 || dy > 1) { msg = "Too far"; }
                else {
                    if (dx > dy) p.dir = tx > p.pos.x ? 2 : 1;
                    else p.dir = ty > p.pos.y ? 0 : 3;
                    msg = act_tool(world, p, tx, ty);
                }
            }
        }
        res.set_content(make_action_ack(msg).dump(), "application/json");
    });

    // ---- text command ----
    svr.Post("/cmd", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        std::string cmd = j.value("cmd", "");
        // Tiered intent parse: rule fast path first, LLM fallback (Phase 8).
        uint64_t t0 = now_ms();
        std::string tier = "none";
        auto intent = intent_engine.parse(cmd, &tier);
        json intent_json = nlohmann::json::object();
        if (intent) {
            intent_json = {{"action", intent->action}, {"parameters", intent->parameters}};
            bus.publish(EventTopic::PlayerCmd, intent_json.dump());
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        json resp = {{"lines", json::array()}};
        if (auto it = world.players.find(pid); it != world.players.end()) {
            // Tiered execution: rule/LLM intent drives the command when the
            // rule fast path did not already handle it. This lets the LLM
            // understand natural-language commands the whitelist misses.
            std::vector<std::string> lines;
            if (tier == "llm" && intent) {
                lines = process_intent(world, it->second, intent_json);
            } else {
                lines = handle_cmd(world, it->second, cmd);
            }
            // P2 death: if the action drained HP or sanity to zero, apply the
            // "loop" reset and surface the death narration.
            if (world.is_dead(it->second)) {
                lines.push_back(world.handle_death(it->second));
                // ROADMAP 1.4: one-shot 4th-wall meta-break after enough loops.
                std::string mb = world.roll_meta_break(it->second, /*once=*/true);
                if (!mb.empty()) lines.push_back(mb);
            }
            for (auto& l : lines) resp["lines"].push_back(l);
            uint64_t latency = now_ms() - t0;
            cmdlog.record(static_cast<uint32_t>(now_ms()), pid, static_cast<int>(world.day), season_name(season_index(world.day)),
                          hour_of_day(world), cmd, intent_json, tier, latency, lines);
            // Feed the command into Town Consciousness so consolidation sees
            // real player behaviour (Phase 7).
            TownEvent ev;
            ev.tick = static_cast<uint32_t>(now_ms());
            ev.day = world.day;
            ev.system = "player";
            ev.event_type = "player_cmd";
            ev.payload = {{"action", intent_json.value("action", "unknown")},
                          {"tier", tier},
                          {"raw", cmd}};
            ev.player_id = static_cast<int>(pid);
            town_consciousness.observe(ev);
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- sleep: advance a day ----
    svr.Post("/sleep", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        std::string msg;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            Vec2 d = world.door();
            bool in_bed = p.inside == "Farmhouse";
            if (!in_bed &&
                (std::abs(int(d.x) - p.pos.x) > 1 || std::abs(int(d.y) - p.pos.y) > 1)) {
                msg = "Sleep near the door";
            } else {
                advance_day(world);
                msg = "Zzz... Day " + std::to_string(world.day);
            }
        }
        res.set_content(make_action_ack(msg).dump(), "application/json");
    });

    // ---- explore: show current chunk and adjacent chunks ----
    svr.Post("/explore", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            int16_t cx = 0, cy = 0;
            if (p.pos.x >= 0 && p.pos.y >= 0) {
                cx = static_cast<int16_t>(p.pos.x / CHUNK_SIZE);
                cy = static_cast<int16_t>(p.pos.y / CHUNK_SIZE);
            }
            resp["lines"].push_back("Current chunk: (" + std::to_string(cx) + ", " + std::to_string(cy) + ")");
            resp["lines"].push_back("Player at: (" + std::to_string(p.pos.x) + ", " + std::to_string(p.pos.y) + ")");
            
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int16_t acx = static_cast<int16_t>(cx + dx), acy = static_cast<int16_t>(cy + dy);
                    if (std::abs(acx) > MAX_CHUNK_RADIUS || std::abs(acy) > MAX_CHUNK_RADIUS) continue;
                    const Chunk* ch = world.get_chunk_const(acx, acy);
                    if (ch && ch->generated) {
                        int building_count = static_cast<int>(ch->buildings.size());
                        int npc_count = static_cast<int>(ch->npcs.size());
                        resp["lines"].push_back("  Chunk (" + std::to_string(acx) + ", " + std::to_string(acy) + "): generated, " + 
                            std::to_string(building_count) + " buildings, " + std::to_string(npc_count) + " NPCs");
                    } else {
                        resp["lines"].push_back("  Chunk (" + std::to_string(acx) + ", " + std::to_string(acy) + "): unexplored");
                    }
                }
            }
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- travel: fast travel to adjacent explored chunk ----
    svr.Post("/travel", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        int16_t target_cx = static_cast<int16_t>(j.value("chunk_x", 0));
        int16_t target_cy = static_cast<int16_t>(j.value("chunk_y", 0));
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            int16_t current_cx = 0, current_cy = 0;
            if (p.pos.x >= 0 && p.pos.y >= 0) {
                current_cx = static_cast<int16_t>(p.pos.x / CHUNK_SIZE);
                current_cy = static_cast<int16_t>(p.pos.y / CHUNK_SIZE);
            }
            int dx = std::abs(target_cx - current_cx);
            int dy = std::abs(target_cy - current_cy);
            if (dx > 1 || dy > 1) {
                resp["lines"].push_back("Can only travel to adjacent chunks (1 chunk away)");
            } else if (std::abs(target_cx) > MAX_CHUNK_RADIUS || std::abs(target_cy) > MAX_CHUNK_RADIUS) {
                resp["lines"].push_back("Target chunk out of range");
            } else {
                const Chunk* ch = world.get_chunk_const(target_cx, target_cy);
                if (!ch || !ch->generated) {
                    resp["lines"].push_back("Target chunk not explored yet");
                } else {
                    p.pos.x = static_cast<int16_t>(target_cx * CHUNK_SIZE + CHUNK_SIZE / 2);
                    p.pos.y = static_cast<int16_t>(target_cy * CHUNK_SIZE + CHUNK_SIZE / 2);
                    world.current_chunk_cx = target_cx;
                    world.current_chunk_cy = target_cy;
                    resp["lines"].push_back("Fast-traveled to chunk (" + std::to_string(target_cx) + ", " + std::to_string(target_cy) + ")");
                    resp["lines"].push_back("New position: (" + std::to_string(p.pos.x) + ", " + std::to_string(p.pos.y) + ")");
                }
            }
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- region: generate procgen region ----
    svr.Post("/region", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        std::string subcmd = j.value("subcmd", "");
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (subcmd == "add") {
                std::string type_str = j.value("type", "forest");
                int16_t cx = static_cast<int16_t>(j.value("cx", 0));
                int16_t cy = static_cast<int16_t>(j.value("cy", 0));
                int16_t radius = static_cast<int16_t>(j.value("radius", 3));
                uint32_t seed = static_cast<uint32_t>(j.value("seed", p.id + world.day));
                
                RegionType type = RegionType::Forest;
                if (type_str == "hills") type = RegionType::Hills;
                else if (type_str == "mountains") type = RegionType::Mountains;
                else if (type_str == "caves") type = RegionType::Caves;
                else if (type_str == "ruins") type = RegionType::Ruins;
                else if (type_str == "swamp") type = RegionType::Swamp;
                else if (type_str == "ocean") type = RegionType::Ocean;
                
                world.generate_region(type, cx, cy, radius, seed);
                Region reg{type, cx, cy, radius, seed};
                world.regions.push_back(reg);
                resp["lines"].push_back("Generated " + type_str + " region at chunk (" + std::to_string(cx) + ", " + std::to_string(cy) + ") radius " + std::to_string(radius));
            } else {
                resp["lines"].push_back("Usage: region add <type> @ cx,cy radius [seed]");
                resp["lines"].push_back("Types: forest, hills, mountains, caves, ruins, swamp, ocean");
            }
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- dsl: construct structures from DSL string ----
    svr.Post("/dsl", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        std::string dsl_str = j.value("dsl", "");
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (dsl_str.empty()) {
                resp["lines"].push_back("Usage: dsl <structure> @ x,y; <structure> @ x,y");
                resp["lines"].push_back("Example: dsl barn @ 110,30; coop @ 115,30");
            } else {
                std::vector<std::pair<std::string, Vec2>> structs;
                std::string error;
                if (world.parse_dsl(dsl_str, structs, error)) {
                    bool any_success = false;
                    for (auto& [name, pos] : structs) {
                        std::string build_error;
                        if (world.build_dsl_structure(p, name, pos.x, pos.y, build_error)) {
                            resp["lines"].push_back("Built " + name + " at (" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ")");
                            any_success = true;
                        } else {
                            resp["lines"].push_back("Failed to build " + name + " at (" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + "): " + build_error);
                        }
                    }
                    if (!any_success) {
                        resp["lines"].push_back("No structures were built");
                    }
                } else {
                    resp["lines"].push_back("DSL parse error: " + error);
                }
            }
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- quest: view available quests ----
    svr.Post("/quest", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        std::string subcmd = j.value("subcmd", "");
        std::string quest_id = j.value("quest_id", "");
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (subcmd == "list" || subcmd.empty()) {
                world.generate_daily_quests_if_needed(p);
                if (world.active_quests.empty()) {
                    resp["lines"].push_back("No active quests. Check back tomorrow!");
                } else {
                    resp["lines"].push_back("=== Active Quests ===");
                    for (auto& q : world.active_quests) {
                        resp["lines"].push_back("[" + q.id + "] " + q.title);
                        resp["lines"].push_back("  " + q.description);
                        resp["lines"].push_back("  Reward: " + std::to_string(q.reward_money) + "g" + 
                            (q.reward_item != Item::None ? ", " + std::to_string(q.reward_count) + "x " + item_def(q.reward_item).name : ""));
                        resp["lines"].push_back("  Expires: Day " + std::to_string(q.expiry_day));
                    }
                }
            } else if (subcmd == "complete") {
                if (quest_id.empty()) {
                    resp["lines"].push_back("Usage: quest complete <quest_id>");
                } else if (world.complete_quest(p, quest_id)) {
                    resp["lines"].push_back("Quest completed! Rewards received.");
                } else {
                    resp["lines"].push_back("Cannot complete quest: requirements not met or invalid ID.");
                }
            } else if (subcmd == "history") {
                if (world.completed_quests.empty()) {
                    resp["lines"].push_back("No completed quests yet.");
                } else {
                    resp["lines"].push_back("=== Completed Quests ===");
                    for (auto& q : world.completed_quests) {
                        resp["lines"].push_back(q.title + " (Day " + std::to_string(q.expiry_day) + ")");
                    }
                }
            } else {
                resp["lines"].push_back("Usage: quest [list|complete <id>|history]");
            }
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- job: view/do jobs ----
    svr.Post("/job", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        std::string subcmd = j.value("subcmd", "");
        std::string job_id = j.value("job_id", "");
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (subcmd == "list" || subcmd.empty()) {
                world.add_job_board_entries(); // Refresh daily
                resp["lines"].push_back("=== Job Board ===");
                for (auto& jb : world.job_board) {
                    resp["lines"].push_back("[" + jb.id + "] " + jb.title + " (" + jb.type + ")");
                    resp["lines"].push_back("  " + jb.description);
                    resp["lines"].push_back("  Reward: " + std::to_string(jb.reward_money) + "g" + 
                        (jb.reward_item != Item::None ? ", " + std::to_string(jb.reward_count) + "x " + item_def(jb.reward_item).name : ""));
                    if (jb.cooldown_until > world.day) {
                        resp["lines"].push_back("  Cooldown: " + std::to_string(jb.cooldown_until - world.day) + " days");
                    } else {
                        resp["lines"].push_back("  Available now!");
                    }
                }
            } else if (subcmd == "do") {
                if (job_id.empty()) {
                    resp["lines"].push_back("Usage: job do <job_id>");
                } else if (world.start_job(p, job_id)) {
                    resp["lines"].push_back("Job completed! Rewards received.");
                } else {
                    resp["lines"].push_back("Cannot start job: on cooldown or invalid ID.");
                }
            } else {
                resp["lines"].push_back("Usage: job [list|do <id>]");
            }
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- market: view prices ----
    svr.Post("/market", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            world.update_market_prices();
            resp["lines"].push_back("=== Market Prices (Day " + std::to_string(world.day) + ") ===");
            for (auto& mp : world.market_prices) {
                if (mp.current_price > 0) {
                    float ratio = static_cast<float>(mp.current_price) / static_cast<float>(mp.base_price);
                    std::string trend = ratio > 1.1f ? " ▲" : (ratio < 0.9f ? " ▼" : "");
                    std::string line = std::string(item_def(mp.item).name) + ": " + std::to_string(mp.current_price) + "g" + trend + 
                        " (base: " + std::to_string(mp.base_price) + "g, supply: " + std::to_string(mp.supply) + ", demand: " + std::to_string(mp.demand) + ")";
                    resp["lines"].push_back(line);
                }
            }
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- horror: sanity + narrative state (Phase 6) ----
    svr.Post("/horror", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            resp["sanity"] = p.sanity;
            resp["max_sanity"] = p.max_sanity;
            resp["tier"] = world.perception_tier(p);
            resp["basement_unlocked"] = world.basement_unlocked;
            resp["basement_visits"] = world.basement_visits;
            resp["horror_cycle"] = world.horror_cycle;
            resp["active_horror"] = world.active_horror;
            json sec = json::array();
            for (auto& s : p.secrets_found) sec.push_back(s);
            resp["secrets"] = sec;
            json log = json::array();
            for (auto& e : p.night_event_log) log.push_back(e);
            resp["night_log"] = log;
            std::string tiername = world.perception_tier(p) == 0 ? "Sane" :
                                   world.perception_tier(p) == 1 ? "Uneasy" :
                                   world.perception_tier(p) == 2 ? "Strained" : "Fractured";
            resp["lines"].push_back("Sanity: " + std::to_string(static_cast<int>(p.sanity)) + "/" + std::to_string(static_cast<int>(p.max_sanity)) + " (" + tiername + ")");
            resp["lines"].push_back("Basement visits: " + std::to_string(world.basement_visits) + " · Cycle: " + std::to_string(world.horror_cycle));
            if (!world.active_horror.empty()) resp["lines"].push_back("Current narrative: " + world.active_horror);
            resp["lines"].push_back("Known secrets (" + std::to_string(p.secrets_found.size()) + "):");
            if (p.secrets_found.empty()) resp["lines"].push_back("  none yet — the truth is still buried");
            else for (auto& s : p.secrets_found) resp["lines"].push_back("  · " + s);
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- basement: enter/leave the hidden under-map (Phase 6) ----
    svr.Post("/basement", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0u);
        std::string sub = j.value("subcmd", "enter");
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (sub == "leave" || sub == "exit") {
                if (p.inside != "Basement") { resp["lines"].push_back("You're not in the basement."); }
                else { world.leave_basement(p); resp["lines"].push_back("You climb back up into the night air."); }
            } else if (p.inside == "Basement") {
                resp["lines"].push_back("You are already in the basement.");
            } else if (hour_of_day(world) < 24) {
                resp["lines"].push_back("The hatch under the floorboards stays shut until midnight.");
            } else {
                resp["lines"].push_back("The floorboards shift. A cold stairwell opens beneath the farmhouse.");
                resp["lines"].push_back("You descend into the dark.");
                world.trigger_basement(p);
                // ROADMAP 1.4: per-cycle procedurally generated room + hazard + mark.
                std::string pg = world.roll_basement_procgen(p);
                std::istringstream pgss(pg);
                std::string ln;
                while (std::getline(pgss, ln)) if (!ln.empty()) resp["lines"].push_back(ln);
            }
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- autosave thread + game loop ----
    std::thread autosave([&]() {
        auto last = steady_clock::now();
        while (true) {
            std::this_thread::sleep_for(milliseconds(1000));
            if (duration_cast<seconds>(steady_clock::now() - last).count() >= 60) {
                std::lock_guard<std::mutex> lock(g_mutex);
                save_world(world, "save.json");
                last = steady_clock::now();
            }
        }
    });

    std::thread game_loop([&]() {
        auto last = steady_clock::now();
        uint64_t last_town_observe_ms = now_ms();
        uint32_t cog_tick = 0;
        while (true) {
            auto now = steady_clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                world.day_seconds += dt;
                if (world.day_seconds >= World::DAY_LENGTH_S) {
                    // pass out at 2:00 AM
                    world.day_seconds = 0;
                    advance_day(world);
                }
                // Phase 7: Town Consciousness consolidation at 04:00 (hour 28)
                // The async worker (consolidate) may take seconds; is_consolidation_due()
                // stays true for many loop iterations while it runs. Gate the
                // SYNCHRONOUS part (minds + apply_adaptations + valley) behind a
                // local per-day flag so each mind ticks exactly once per day and
                // apply_adaptations can't clobber the Valley's push on re-entry.
                static uint32_t last_sync_consolidation_day = 0;
                if (hour_of_day(world) == 28 && town_consciousness.is_consolidation_due() &&
                    last_sync_consolidation_day != world.day) {
                    last_sync_consolidation_day = world.day;
                    town_consciousness.consolidate();
                    // Phase 7.9: Nature Mind consolidation (runs after Town Consciousness)
                    nature_mind.sync_from_world();   // ROADMAP 2.5 — ground chunks in real trees
                    nature_mind.tick(world.day);
                    // Phase 7.9: Village Mind consolidation (aggregate NPC mood)
                    village_mind.tick(world.day);
                    // Phase 7.9: Economy Mind consolidation (refresh prices, then track)
                    world.update_market_prices();
                    economy_mind.tick(world.day);
                    // Phase 7.9: Culture Mind consolidation (aggregate shared beliefs)
                    culture_mind.tick(world.day);
                    // Phase 7.3: push consolidated adaptations into the world's
                    // consumer scalars (weather, economy, horror, performance).
                    // Runs once per day here (not every loop iter) so the
                    // aggregate minds' pushes below are not clobbered.
                    world.apply_adaptations(town_consciousness.snapshot_adaptations());
                    // ROADMAP 1.2: Valley Mind runs last, AFTER
                    // apply_adaptations, so the Valley's awakening push into
                    // horror_intensity / fog / drain survives the day.
                    valley_mind.tick(world.day);
                }
                // Phase 7: lightweight system heartbeat into the event buffer so
                // consolidation sees the environmental state even with no input.
                if (now_ms() - last_town_observe_ms >= 60000) {
                    last_town_observe_ms = now_ms();
                    TownEvent ev;
                    ev.tick = static_cast<uint32_t>(now_ms());
                    ev.day = world.day;
                    ev.system = "weather";
                    ev.event_type = "state";
                    ev.payload = {{"weather", world.weather_of_day_adapted(world.day)},
                                  {"hour", hour_of_day(world)},
                                  {"players", static_cast<int>(world.players.size())},
                                  {"npcs", static_cast<int>(world.npcs.size())},
                                  {"day", world.day}};
                    town_consciousness.observe(ev);
                }

                // Phase 7.7: Cognitive Core per-tick update for important NPCs.
                ++cog_tick;
                std::vector<std::string> stimuli;
                // Add world-level stimuli that all agents can observe.
                stimuli.push_back("weather:" + std::to_string(world.weather_of_day_adapted(world.day)));
                stimuli.push_back("hour:" + std::to_string(hour_of_day(world)));
                stimuli.push_back("season:" + std::to_string(season_index(world.day)));
                if (world.day > 0) stimuli.push_back("day:" + std::to_string(world.day));
                cog_registry.tick_all(cog_tick, stimuli);
                for (auto& [id, p] : world.players) {
                    if (p.moving && !p.path.empty() &&
                        (now_ms() - p.move_start_ms) >= 110) {
                        Vec2 next = p.path.front();
                        if (world.walkable(next)) p.pos = next;
                        // ROADMAP 1.4: walking on corrupted ground feeds the
                        // dread profile toward the rot theme.
                        {
                            uint8_t cor = world.at(p.pos).corruption;
                            if (cor >= 128) world.bump_dread(p, 3);
                        }
                        p.path.erase(p.path.begin());
                        p.move_start_ms = static_cast<uint32_t>(now_ms());
                        if (p.pos == p.target || p.path.empty()) p.moving = false;
                    }
                    // P2 death: sanity can reach zero from staying up / horror;
                    // apply the "loop" reset. The narration is surfaced by the
                    // next /state or status query via pending_death.
                    if (world.is_dead(p) && p.pending_death.empty()) {
                        p.pending_death = world.handle_death(p);
                        // ROADMAP 1.2: log the death in the Valley's memory.
                        valley_mind.record_event("death",
                            "player " + p.name + " died (loop "
                            + std::to_string(p.death_count) + ")", 0.15f);
                        // ROADMAP 1.4: one-shot 4th-wall meta-break.
                        std::string mb = world.roll_meta_break(p, /*once=*/true);
                        if (!mb.empty()) p.pending_death += "\n" + mb;
                    }
                }
                for (auto& n : world.npcs) {
                    if (now_ms() < n.next_move_ms) continue;
                    n.next_move_ms = now_ms() + static_cast<uint64_t>(400 + rand() % 300);
                    if (hour_of_day(world) >= 21) continue;   // villagers turn in at night
                    Vec2 anchor;
                    int slot = schedule_slot(n.name, world.day, hour_of_day(world), anchor, &world);
                    if (slot == -1) { step_npc(world, n); continue; }
                    if (slot != n.sched_slot) {
                        n.sched_slot = static_cast<int8_t>(slot);
                        n.path.clear();
                        n.path_i = 0;
                        if (anchor != n.pos) bfs_path(world, n.pos, anchor, n.path, 512);
                    }
                    if (!n.path.empty()) {
                        if (n.path_i < n.path.size()) {
                            Vec2 next = n.path[n.path_i];
                            if (world.walkable(next) && npc_at(world, next.x, next.y) < 0) {
                                n.dir = next.x > n.pos.x ? 2 : next.x < n.pos.x ? 1
                                     : next.y > n.pos.y ? 0 : 3;
                                n.pos = next;
                                ++n.path_i;
                                // L9: NPC tracks on snow/ice
                                int season = season_index(world.day);
                                if (season == 3) {
                                    Cell& stepped = world.at(next);
                                    if (stepped.tile == Tile::Snow || stepped.tile == Tile::Ice) {
                                        stepped.track_age = 1;
                                        stepped.track_type = 2; // NPC
                                        stepped.track_dir = static_cast<int8_t>(n.dir);
                                    }
                                }
                            } else {
                                // blocked (e.g. by another villager): recompute
                                n.path.clear();
                                n.path_i = 0;
                                if (anchor != n.pos) bfs_path(world, n.pos, anchor, n.path, 512);
                            }
                        } else n.path.clear();
                    }
                }
            }
            std::this_thread::sleep_for(milliseconds(16));
        }
    });

    svr.set_logger([](const auto& req, const auto& res) { std::cerr << req.method << " " << req.path << " -> " << res.status << std::endl; });
    std::cout << "Ashgrove ~ Stardew-ish farm. Listen on 0.0.0.0:" << port << std::endl;
    int listen_ret = svr.listen("0.0.0.0", port);
    std::cerr << "svr.listen returned: " << listen_ret << std::endl;
    if (listen_ret != 0) {
        std::cerr << "Failed to start server on port " << port << std::endl;
        return 1;
    }
    return 0;
}