#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <string>
#include <optional>

constexpr uint16_t MAP_W = 128;
constexpr uint16_t MAP_H = 96;
constexpr uint8_t TILE_SIZE = 16;

struct Vec2 {
    int16_t x, y;
    bool operator==(Vec2 const& o) const noexcept { return x == o.x && y == o.y; }
};

// ---- tiles ----
enum class Tile : uint8_t {
    Grass = 0, GrassVar = 1, Water = 2, WaterNorth = 3, WaterSouth = 4,
    WaterEast = 5, WaterWest = 6, Dirt = 7, Sand = 8, Tilled = 9, Bridge = 10,
    Snow = 11, Ice = 12, Cobble = 13,
};

// ---- items ----
enum class Item : uint16_t {
    None = 0,
    Hoe = 1, WateringCan = 2, Axe = 3, Pickaxe = 4, Scythe = 5,
    ParsnipSeeds = 10, PotatoSeeds = 11, CauliflowerSeeds = 12,
    CornSeeds = 13, TomatoSeeds = 14, WheatSeeds = 15, BlueberrySeeds = 16,
    GreenBeanSeeds = 17, HopsSeeds = 18,
    Parsnip = 20, Potato = 21, Cauliflower = 22,
    Corn = 23, Tomato = 24, Wheat = 25, Blueberry = 26,
    GreenBean = 27, Hops = 28,
    CopperOre = 6, IronOre = 7, GoldOre = 8, IridiumOre = 9,
    CopperBar = 17, IronBar = 18, GoldBar = 19,
    Wood = 30, Stone = 31, Fiber = 32,
    Fish = 33, Forage = 34, Bread = 35,
    FertilizerBasic = 36, FertilizerQuality = 37, FertilizerPremium = 38,
    Scarecrow = 39,
    AppleSapling = 40, CherrySapling = 41, PeachSapling = 42, PomegranateSapling = 43,
    Apple = 44, Cherry = 45, Peach = 46, Pomegranate = 47,
};

struct ItemDef {
    const char* name;
    uint8_t type;        // 0 tool, 1 seed, 2 produce, 3 resource
    int buy;             // shop price, -1 = not sold
    int sell;
    uint8_t energy;      // energy cost to use (tools)
};

inline ItemDef const& item_def(Item it) {
    static const ItemDef defs[] = {
        /* 0  */ {"None",0,0,0,0},
        /* 1  */ {"Hoe",0,-1,0,2},{"Watering Can",0,-1,0,2},{"Axe",0,-1,0,5},
        /* 4  */ {"Pickaxe",0,-1,0,5},{"Scythe",0,-1,0,2},
        /* 6-9  */ {"Copper Ore",3,-1,15,0},{"Iron Ore",3,-1,30,0},
                    {"Gold Ore",3,-1,60,0},{"Iridium Ore",3,-1,150,0},
        /* 10 */ {"Parsnip Seeds",1,20,0,0},{"Potato Seeds",1,50,0,0},{"Cauliflower Seeds",1,80,0,0},
        /* 13 */ {"Corn Seeds",1,150,0,0},{"Tomato Seeds",1,50,0,0},{"Wheat Seeds",1,10,0,0},
                {"Blueberry Seeds",1,80,0,0},
        /* 17-19 */ {"Copper Bar",3,-1,2,0},{"Iron Bar",3,-1,4,0},{"Gold Bar",3,-1,8,0},
        /* 20 */ {"Parsnip",2,-1,35,0},{"Potato",2,-1,80,0},{"Cauliflower",2,-1,175,0},
        /* 23 */ {"Corn",2,-1,110,0},{"Tomato",2,-1,120,0},{"Wheat",2,-1,25,0},
                {"Blueberry",2,-1,100,0},
        /* 27-29 */ {"-",0,0,0,0},{"-",0,0,0,0},{"-",0,0,0,0},
        /* 30 */ {"Wood",3,-1,2,0},{"Stone",3,-1,2,0},{"Fiber",3,-1,1,0},
        /* 33 */ {"Fish",3,-1,0,0},{"Forage",3,-1,0,0},
        /* 35 */ {"Bread",2,5,0,0},
        /* 36 */ {"Fertilizer Basic",3,100,0,0},{"Fertilizer Quality",3,200,0,0},{"Fertilizer Premium",3,400,0,0},
        /* 39 */ {"Scarecrow",3,500,0,0},
        /* 40 */ {"Apple Sapling",3,4000,0,0},{"Cherry Sapling",3,3400,0,0},{"Peach Sapling",3,6000,0,0},{"Pomegranate Sapling",3,6000,0,0},
        /* 44 */ {"Apple",2,-1,100,0},{"Cherry",2,-1,80,0},{"Peach",2,-1,140,0},{"Pomegranate",2,-1,140,0},
    };
    uint16_t i = static_cast<uint16_t>(it);
    return i < sizeof(defs)/sizeof(defs[0]) ? defs[i] : defs[0];
}

// ---- crops (seed -> produce) ----
struct CropDef {
    const char* name;
    Item seed;
    Item produce;
    int buy;
    int sell;
    uint8_t days;
    uint8_t min_season;   // 0 spring ...
    uint8_t max_season;   // 3 winter (crops unplantable in winter)
};

inline CropDef const* crop_def(const char* name) {
    static const CropDef crops[] = {
        {"parsnip", Item::ParsnipSeeds, Item::Parsnip, 20, 35, 4, 0, 2},
        {"potato", Item::PotatoSeeds, Item::Potato, 50, 80, 6, 0, 2},
        {"cauliflower", Item::CauliflowerSeeds, Item::Cauliflower, 80, 175, 12, 0, 1},
        {"corn", Item::CornSeeds, Item::Corn, 150, 110, 14, 1, 2},
        {"tomato", Item::TomatoSeeds, Item::Tomato, 50, 120, 10, 1, 1},
        {"wheat", Item::WheatSeeds, Item::Wheat, 10, 25, 4, 0, 3},
        {"blueberry", Item::BlueberrySeeds, Item::Blueberry, 80, 100, 13, 0, 2},
        {"green bean", Item::GreenBeanSeeds, Item::GreenBean, 60, 40, 10, 0, 2},
        {"hops", Item::HopsSeeds, Item::Hops, 60, 25, 11, 1, 1},
        {"apple", Item::AppleSapling, Item::Apple, 4000, 100, 28, 0, 0},
        {"cherry", Item::CherrySapling, Item::Cherry, 3400, 80, 28, 0, 0},
        {"peach", Item::PeachSapling, Item::Peach, 6000, 140, 28, 0, 0},
        {"pomegranate", Item::PomegranateSapling, Item::Pomegranate, 6000, 140, 28, 0, 0},
    };
    std::string nm(name);
    // normalize: "parsnip seeds" -> "parsnip", "parsnips" -> "parsnip"
    if (nm.size() > 6 && nm.substr(nm.size() - 6) == " seeds") nm = nm.substr(0, nm.size() - 6);
    for (auto& c : crops)
        if (nm == c.name || nm == std::string(c.name) + "s" ||
            nm == std::string(c.name) + " seed" || nm == std::string(c.name) + " seeds")
            return &c;
    return nullptr;
}

inline CropDef const* crop_def(Item produce) {
    static const CropDef crops[] = {
        {"parsnip", Item::ParsnipSeeds, Item::Parsnip, 20, 35, 4, 0, 2},
        {"potato", Item::PotatoSeeds, Item::Potato, 50, 80, 6, 0, 2},
        {"cauliflower", Item::CauliflowerSeeds, Item::Cauliflower, 80, 175, 12, 0, 1},
        {"corn", Item::CornSeeds, Item::Corn, 150, 110, 14, 1, 2},
        {"tomato", Item::TomatoSeeds, Item::Tomato, 50, 120, 10, 1, 1},
        {"wheat", Item::WheatSeeds, Item::Wheat, 10, 25, 4, 0, 3},
        {"blueberry", Item::BlueberrySeeds, Item::Blueberry, 80, 100, 13, 0, 2},
        {"green bean", Item::GreenBeanSeeds, Item::GreenBean, 60, 40, 10, 0, 2},
        {"hops", Item::HopsSeeds, Item::Hops, 60, 25, 11, 1, 1},
        {"apple", Item::AppleSapling, Item::Apple, 4000, 100, 28, 0, 0},
        {"cherry", Item::CherrySapling, Item::Cherry, 3400, 80, 28, 0, 0},
        {"peach", Item::PeachSapling, Item::Peach, 6000, 140, 28, 0, 0},
        {"pomegranate", Item::PomegranateSapling, Item::Pomegranate, 6000, 140, 28, 0, 0},
    };
    for (auto& c : crops)
        if (c.produce == produce) return &c;
    return nullptr;
}

inline CropDef const* crop_by_seed(Item seed) {
    static const CropDef crops[] = {
        {"parsnip", Item::ParsnipSeeds, Item::Parsnip, 20, 35, 4, 0, 2},
        {"potato", Item::PotatoSeeds, Item::Potato, 50, 80, 6, 0, 2},
        {"cauliflower", Item::CauliflowerSeeds, Item::Cauliflower, 80, 175, 12, 0, 1},
        {"corn", Item::CornSeeds, Item::Corn, 150, 110, 14, 1, 2},
        {"tomato", Item::TomatoSeeds, Item::Tomato, 50, 120, 10, 1, 1},
        {"wheat", Item::WheatSeeds, Item::Wheat, 10, 25, 4, 0, 3},
        {"blueberry", Item::BlueberrySeeds, Item::Blueberry, 80, 100, 13, 0, 2},
        {"green bean", Item::GreenBeanSeeds, Item::GreenBean, 60, 40, 10, 0, 2},
        {"hops", Item::HopsSeeds, Item::Hops, 60, 25, 11, 1, 1},
        {"apple", Item::AppleSapling, Item::Apple, 4000, 100, 28, 0, 0},
        {"cherry", Item::CherrySapling, Item::Cherry, 3400, 80, 28, 0, 0},
        {"peach", Item::PeachSapling, Item::Peach, 6000, 140, 28, 0, 0},
        {"pomegranate", Item::PomegranateSapling, Item::Pomegranate, 6000, 140, 28, 0, 0},
    };
    for (auto& c : crops)
        if (c.seed == seed) return &c;
    return nullptr;
}

// ---- NPCs ----
struct NPC {
    std::string name;
    std::string kind = "villager"; // "villager", "rabbit", etc.
    uint8_t color = 1;         // 1..5 sprite color (index into NPC list; color >palette clamps via %)
    Vec2 pos;
    Vec2 way[2];
    uint8_t way_idx = 0;
    uint64_t next_move_ms = 0;
    uint8_t dir = 0;
    // daily schedule: current time slot + BFS path toward its anchor
    int8_t sched_slot = -1;
    std::vector<Vec2> path;
    size_t path_i = 0;
};

// ---- world objects ----
enum class ObjType : uint8_t {
    None = 0, Tree, Rock, Weed, TallGrass, Flower, Stump,
    FencePost, FenceRail, Pine, Bush, Mushroom, Building,
    Sprinkler = 13, Statue, LeafLitter, Scarecrow,
};
static inline bool is_machine(ObjType t) { return t == ObjType::Sprinkler; }

struct Bldg {
    std::string name;
    int16_t x, y, w, h;
};

// Building condition state for weathering/maintenance
struct BuildingState {
    uint8_t condition = 100;     // 0..100, decays over time
    uint8_t roof_leak = 0;        // rain damage accrued
    uint8_t foundation = 100;     // winter frost damage
    uint32_t last_maintained_day = 0;
};

// ---- interiors ----
struct InteriorRoom {
    std::string building;              // matches Bldg::name; "" = none
    std::vector<std::string> rows;     // '#' wall, '.' floor, letters = furniture
    int16_t w = 0, h = 0;
};

struct FarmObj { ObjType type = ObjType::None; uint8_t hp = 0; uint8_t ore = 0; };

struct Crop {
    Item crop = Item::None;   // Parsnip / Potato / Cauliflower
    uint8_t stage = 0;        // 0..3 (sprite index)
    int8_t days_left = 0;     // days until mature
    bool watered = false;
    // Trellis crops (green bean, hops) are impassable when growing
    // Fruit trees (apple, cherry, peach, pomegranate) produce seasonally
    bool is_trellis = false;
    bool is_fruit_tree = false;
    int8_t last_harvest_season = -1; // for fruit trees: season when last harvested
    bool is_crop() const { return crop != Item::None; }
};

// ---- cell ----
struct Cell {
    Tile tile = Tile::Grass;
    FarmObj obj;
    Crop crop;
};

// ---- player ----
struct InvSlot { Item item = Item::None; uint16_t count = 0; };

struct Player {
    uint32_t id = 0;
    Vec2 pos, target;
    uint8_t dir = 0;           // 0 down, 1 left, 2 right, 3 up
    bool moving = false;
    uint32_t move_start_ms = 0;
    std::string name;
    float energy = 270.0f;
    uint16_t max_energy = 270;
    int money = 500;
    uint8_t sel = 0;           // selected inventory slot
    std::array<InvSlot, 12> inv; // tools + items
    std::vector<Vec2> path;    // waypoints (mutex-guarded)
    std::string inside;        // building interior name, or "" for overworld
    int16_t inx = 0, iny = 0;  // position inside the interior
    bool train_used = false;   // rode the Zuzu express today
    Vec2 inside_exit;          // overworld tile to return to on exit
    // friendship: npc name -> hearts (0..10). one gift per NPC per day.
    std::map<std::string, uint8_t> hearts;
    std::set<std::string> gifted_today;
    // Egg Festival (Spring 13): eggs found this festival, searches remaining.
    uint8_t fest_eggs = 0;
    uint8_t fest_tries = 8;
    // Known landmarks for fast-travel / path-walking (building names player has entered)
    std::set<std::string> known_landmarks;
};

// ---- world ----
struct World {
    std::array<Cell, MAP_W * MAP_H> cells;
    std::unordered_map<uint32_t, Player> players;
    std::vector<NPC> npcs;
    std::vector<Bldg> buildings;
    std::unordered_map<std::string, InteriorRoom> interiors;
    std::unordered_map<std::string, BuildingState> building_states;
    uint32_t next_player_id = 1;
    uint32_t day = 1;
    float day_seconds = 0.0f;   // seconds since 6:00 AM
    static constexpr float DAY_LENGTH_S = 800.0f; // 20h @ 40s per game hour

    Vec2 house_tl{38, 78};

    Cell&     at(int x, int y) { return cells[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)]; }
    Cell const& at(int x, int y) const { return cells[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)]; }
    Cell&     at(Vec2 p) { return cells[static_cast<size_t>(p.y) * MAP_W + static_cast<size_t>(p.x)]; }
    Cell const& at(Vec2 p) const { return cells[static_cast<size_t>(p.y) * MAP_W + static_cast<size_t>(p.x)]; }
    bool in_bounds(int x, int y) const { return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H; }
    bool in_bounds(Vec2 p) const { return in_bounds(p.x, p.y); }

    bool walkable(int x, int y) const {
        if (!in_bounds(x, y)) return false;
        Tile t = at(x, y).tile;
        if (t == Tile::Water || t == Tile::WaterNorth || t == Tile::WaterSouth ||
            t == Tile::WaterEast || t == Tile::WaterWest) return false;
        ObjType o = at(x, y).obj.type;
        if (o == ObjType::Tree || o == ObjType::Rock || o == ObjType::Stump ||
            o == ObjType::FencePost || o == ObjType::FenceRail ||
            o == ObjType::Pine || o == ObjType::Building ||
            o == ObjType::Sprinkler || o == ObjType::Statue) return false;
        // Trellis crops (green bean, hops) are impassable
        const Crop& crop = at(x, y).crop;
        if (crop.is_crop() && crop.is_trellis) return false;
        return true;
    }
    bool walkable(Vec2 p) const { return walkable(p.x, p.y); }

    bool in_house(int x, int y) const {
        return x >= house_tl.x && x < house_tl.x + 3 && y >= house_tl.y && y < house_tl.y + 4;
    }
    Vec2 door() const { return {int16_t(house_tl.x + 1), int16_t(house_tl.y + 3)}; }
};

void generate_world(World& world);
void resolve_water_edges(World& world);
void init_npcs(World& world);
void place_buildings(World& world);
void clear_paths(World& world);   // keep bridges + doorways passable (post-scatter)
void init_interiors(World& world);
int  hour_of_day(const World& w);      // 6..26 (26 == 2:00 AM)
const char* clock_str(const World& w); // "Day 3 · 10:40 AM"
int  season_index(uint32_t day);       // 0 spring .. 3 winter
int  season_day(uint32_t day);         // 1..28 within season
const char* season_name(int season);
int  weather_of_day(uint32_t day);     // 0 sunny, 1 rainy
const char* weather_of_day_name(uint32_t day);
const char* region_at(const World& w, int x, int y);
const char* npc_line(const char* name, int season);
int  npc_at(const World& w, int x, int y);   // index into world.npcs or -1
bool is_festival_day(uint32_t day);          // Egg Festival: Spring 13
// daily schedule slot for hour; writes the anchor to walk to (-1 = free-roam)
int  schedule_slot(const std::string& name, uint32_t day, int hour, Vec2& anchor);
std::string forage_table(int season, int& count);       // fills count
std::string fish_table(int season, int& count);         // fills count
bool bfs_path(World& world, Vec2 from, Vec2 to, std::vector<Vec2>& out, size_t max_len = 64);
std::string serialize_world(const World& w);
bool deserialize_world(World& w, const std::string& json_str);
void add_item(Player& p, Item item, uint16_t count);     // first free slot, else adds to existing
bool consume_item(Player& p, Item item, uint16_t count);