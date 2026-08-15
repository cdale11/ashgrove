#include "world.hpp"
#include "protocol.hpp"
#include "event_bus.hpp"
#include "llama_wrapper.hpp"
#include "intent_engine.hpp"
#include "command_log.hpp"
#include <httplib.h>
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

static uint64_t now_ms() {
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static bool is_water_any(Tile t) {
    return t == Tile::Water || t == Tile::WaterNorth || t == Tile::WaterSouth ||
           t == Tile::WaterEast || t == Tile::WaterWest;
}

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
    std::string s((std::istreambuf_iterator<char>(f)), {});
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

// Advance to the next day: crops that were watered grow, rain waters everything,
// all farmers get their energy back. Saves immediately.
static void advance_day(World& w) {
    w.day++;
    w.day_seconds = 0;
    bool rain = weather_of_day(w.day) == 1;
    for (auto& cell : w.cells) {
        if (cell.crop.is_crop()) {
            if (cell.crop.watered && cell.crop.days_left > 0) {
                int growth = 1;
                // Fertilizer bonus: basic=+1, quality=+2, premium=+3 extra growth per day
                // Fertilizer is consumed (one-time boost) - applied on first growth
                if (cell.obj.type == ObjType::None && cell.obj.ore >= 2) {
                    int bonus = (cell.obj.ore - 1);
                    growth += bonus;
                    // Consume fertilizer after applying boost
                    cell.obj.ore = 0;
                }
                // Moon phase bonus: crops planted on new moon (hp=1) grow 10% faster
                if (cell.obj.type == ObjType::None && cell.obj.hp == 1) {
                    // 10% chance of extra growth per day
                    if ((static_cast<int>(w.day) + cell.crop.days_left) % 10 == 0) growth++;
                }
                cell.crop.days_left = static_cast<int8_t>(std::max(0, static_cast<int>(cell.crop.days_left) - growth));
                // Recompute stage based on elapsed time vs total days.
                // Stage 0 = just planted, stage 3 = ready to harvest.
                const CropDef* cd = crop_def(cell.crop.crop);
                if (cd) {
                    int total = std::max(1, static_cast<int>(cd->days));
                    int elapsed = total - cell.crop.days_left;
                    cell.crop.stage = std::min<uint8_t>(
                        static_cast<uint8_t>((elapsed * 4) / total), static_cast<uint8_t>(3));
                }
            }
            cell.crop.watered = rain;   // overnight rain waters every plot
        }
    }
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
                cell.crop.watered = rain;
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

    // Wind pollination: flowers adjacent to crops boost quality chance at harvest
    // This is applied at harvest time, but we track it here for flavor
    // (Actual quality boost happens in harvest command)

    // Moon phase bonus is set at plant time (see plant command), not retroactively.
    // (Actual quality boost happens in harvest command)

    // Building weathering & maintenance (all buildings)
    bool winter = (season == 3);
    for (auto& [name, bs] : w.building_states) {
        if (rain) bs.roof_leak = std::min<uint8_t>(bs.roof_leak + 1, 100);
        if (winter) bs.foundation = bs.foundation > 2 ? bs.foundation - 2 : 0;
        // Condition decays based on damage
        if (bs.roof_leak > 50 || bs.foundation < 50) {
            bs.condition = bs.condition > 5 ? bs.condition - 5 : 0;
        } else if (bs.roof_leak > 20 || bs.foundation < 80) {
            bs.condition = bs.condition > 2 ? bs.condition - 2 : 0;
        } else {
            bs.condition = std::min<uint8_t>(bs.condition + 1, 100); // slow recovery if maintained
        }
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

    // Tree growth, sap production, and natural regrowth
    // Trees grow slowly (chance to grow from sapling to full tree)
    // Mature trees produce sap/resin/rubber daily (if tapped)
    // Stumps can regrow into trees over time
    // Weeds and tall grass regrow naturally
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = w.at(x, y);
            if (is_tree(c.obj.type)) {
                // Tree sap production: mature trees (hp > 50) produce sap daily
                if (c.obj.hp > 50 && c.obj.hp < 255) {
                    // Check if tree has been tapped (ore = 1 means tapped)
                    if (c.obj.ore == 1) {
                        c.obj.hp = std::min<uint8_t>(c.obj.hp + 1, 255);
                    }
                }
                // Tree growth: small chance to grow (hp increases)
                if (c.obj.hp < 255) {
                    int growth_chance = (c.obj.hp < 20) ? 5 : (c.obj.hp < 50) ? 3 : 1;
                    if ((static_cast<int>(w.day) * 7 + x * 13 + y * 19) % 100 < static_cast<unsigned>(growth_chance)) {
                        c.obj.hp = std::min<uint8_t>(c.obj.hp + 1, 255);
                    }
                }
            }
            // Stump regrowth: 2% chance per day to become a sapling (hp=1)
            else if (c.obj.type == ObjType::Stump) {
                if ((static_cast<int>(w.day) * 11 + x * 17 + y * 23) % 100 < 2) {
                    c.obj = {ObjType::Tree, 1, 0}; // regrow as generic tree
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
    if (w.in_bounds(next) && w.walkable(next) && npc_at(w, next.x, next.y) < 0)
        n.pos = next;
    else if (dy != 0) {
        next = n.pos;
        next.y += dy;
        if (w.walkable(next) && npc_at(w, next.x, next.y) < 0) n.pos = next;
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
        if (!spend(def.energy)) return "Exhausted";
        Tile t = c.tile;
        if (t == Tile::Grass || t == Tile::GrassVar || t == Tile::Dirt ||
            t == Tile::Sand || t == Tile::Tilled) {
            c.tile = Tile::Tilled;
            c.crop = Crop{};
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
        if (can.count == 0) return "Can is empty";
        if (c.tile == Tile::Tilled) {
            if (!spend(def.energy)) return "Exhausted";
            can.count--;
            c.crop.watered = true;
            return "";
        }
        return "Water the soil";
    }
    case Item::Axe: {
        if (!is_tree(c.obj.type) && c.obj.type != ObjType::Stump) return "";
        if (!spend(def.energy)) return "Exhausted";
        if (--c.obj.hp > 0) return "";
        ObjType was = c.obj.type;
        c.obj = FarmObj{};
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
        add_item(p, log, static_cast<uint16_t>(log_amount));
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
        if (c.obj.type != ObjType::Rock) return "";
        if (!spend(def.energy)) return "Exhausted";
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
        if (c.obj.type != ObjType::Weed && c.obj.type != ObjType::TallGrass &&
            c.obj.type != ObjType::Mushroom) return "";
        if (!spend(def.energy)) return "Exhausted";
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
    default: return "something";
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
    p.inv[0] = {Item::Hoe, 1};
    p.inv[1] = {Item::WateringCan, 40};
    p.inv[2] = {Item::Axe, 1};
    p.inv[3] = {Item::Pickaxe, 1};
    p.inv[4] = {Item::Scythe, 1};
    p.inv[5] = {Item::ParsnipSeeds, 15};
    p.inv[6] = {Item::PotatoSeeds, 15};
    p.inv[7] = {Item::CauliflowerSeeds, 10};
    return p;
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
        say("  scythe         clear weeds/grass");
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
        say(clock_str(w));
        say(std::string(season_name(season_index(w.day))) + " " +
            std::to_string(season_day(w.day)) + " · " + weather_of_day_name(w.day));
        say("Energy: " + std::to_string(static_cast<int>(p.energy)) + "/" + std::to_string(p.max_energy) +
            "   Money: " + std::to_string(p.money) + "g");
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
    if (cmd == "inventory" || cmd == "inv") {
        bool any = false;
        for (size_t i = 0; i < p.inv.size(); ++i)
            if (p.inv[i].item != Item::None) {
                any = true;
                say("  " + std::string(item_def(p.inv[i].item).name) +
                    (p.inv[i].count > 1 ? " x" + std::to_string(p.inv[i].count) : ""));
            }
        if (!any) say("You carry nothing.");
        return out;
    }
    if (cmd == "time") { say(clock_str(w)); return out; }
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
        int season = season_index(w.day);
        int hour = hour_of_day(w);
        const char* part = hour < 9 ? "early morning" : hour < 12 ? "morning" :
                           hour < 17 ? "afternoon" : hour < 21 ? "evening" : "night";
        say("You stand on " + std::string(terrain_name(c.tile)) + " in " +
            std::string(region_at(w, p.pos.x, p.pos.y)) + ".");
        say("It's a " + std::string(weather_of_day_name(w.day)) + " " +
            std::string(season_name(season)) + " " + std::string(part) + ".");
        // R9.3: Foggy weather reduces visibility
        bool foggy = (weather_of_day(w.day) == 2);
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
        if (ptier >= 2) {
            // Distorted vision: a "false" neighbor the player thinks they see.
            static const char* phantom[] = {"a figure standing at the treeline", "a face at a dark window",
                                            "the scarecrow closer than before", "a shape that is not there"};
            std::mt19937 hrng(p.id * 977u + w.day);
            say("You think you see " + std::string(phantom[hrng() % 4]) + ". It is gone when you look again.");
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
    if (cmd == "axe" || cmd == "chop") {
        if (!grab_tool(Item::Axe)) return out;
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("No tree there."); return out; }
        // Chop repeatedly until tree breaks (hp reaches 0) or it's not a tree.
        for (int attempts = 0; attempts < 10; ++attempts) {
            std::string m = act_tool(w, p, f.x, f.y);
            if (m == "Exhausted") { say("Too tired. Rest or sleep."); return out; }
            if (!m.empty()) { say("You swing your axe. " + m + "."); return out; }
            Cell& cell = w.at(f);
            if (cell.obj.type != ObjType::Tree &&
                cell.obj.type != ObjType::Stump &&
                cell.obj.type != ObjType::Pine) {
                say("Nothing to chop here.");
                return out;
            }
        }
        say("Nothing to chop here.");
        return out;
    }
    if (cmd == "pick" || cmd == "mine") {
        if (!grab_tool(Item::Pickaxe)) return out;
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("No rock there."); return out; }
        // Mine repeatedly until rock breaks (hp reaches 0) or it's not a rock.
        for (int attempts = 0; attempts < 10; ++attempts) {
            std::string m = act_tool(w, p, f.x, f.y);
            if (m == "Exhausted") { say("Too tired. Rest or sleep."); return out; }
            if (!m.empty()) { say("You strike the rock. " + m + "."); return out; }
            Cell& cell = w.at(f);
            if (cell.obj.type != ObjType::Rock) {
                say("Nothing to mine here.");
                return out;
            }
        }
        say("Nothing to mine here.");
        return out;
    }
    if (cmd == "scythe" || cmd == "cut") {
        if (!grab_tool(Item::Scythe)) return out;
        Vec2 f = facing_cell(p);
        if (!w.in_bounds(f)) { say("Nothing to cut."); return out; }
        std::string m = act_tool(w, p, f.x, f.y);
        if (m == "Exhausted") say("Too tired. Rest or sleep.");
        else if (m.empty()) say("Nothing to cut here.");
        else say("You sweep your scythe. " + m + ".");
        return out;
    }

    // ---------- planting ----------
    if (cmd == "plant" || cmd == "planting") {
        const CropDef* crop = crop_def(arg.c_str());
        if (!crop) { say("Plant what? parsnip, potato, cauliflower, corn, tomato, wheat, blueberry, green bean, hops, strawberry, melon, pumpkin, red cabbage, rhubarb, garlic, artichoke, bok choy, kale, cranberry, grape, apple, cherry, peach, pomegranate, apricot, orange, banana, mango, plum, pear, fig, avocado, lemon, lime, grapefruit, persimmon."); return out; }
        int season = season_index(w.day);
        if (season == 3) { say("The soil is frozen solid. Nothing grows in winter."); return out; }
        if (season < crop->min_season || season > crop->max_season) {
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
        if (fert_name == "basic") fert = Item::FertilizerBasic;
        else if (fert_name == "quality") fert = Item::FertilizerQuality;
        else if (fert_name == "premium") fert = Item::FertilizerPremium;
        else {
            say("Apply which fertilizer? 'fertilize basic', 'fertilize quality', 'fertilize premium'.");
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
        // Fertilizer improves crop yield/speed: apply quality modifier
        // Basic: +1 stage speed, Quality: +2, Premium: +3
        int bonus = (fert == Item::FertilizerBasic) ? 1 : (fert == Item::FertilizerQuality) ? 2 : 3;
        c.obj = {ObjType::None, 0, static_cast<uint8_t>(bonus + 1)}; // Store bonus in ore field
        say("Applied " + std::string(item_def(fert).name) + ". Crops here will grow faster.");
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
            int season = season_index(w.day);
            // Only harvestable if the tree has produced fruit this season (set by advance_day).
            // Prevents re-harvesting the same tree repeatedly in one season.
            if (c.crop.last_harvest_season != season) {
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
        if (flower_bonus > 0 && (static_cast<int>(w.day) * 7 + f.x * 13 + f.y * 19) % 100 < static_cast<unsigned>(20 * flower_bonus)) {
            // 20% chance per adjacent flower for quality bonus (double price)
            sell_price *= 2;
            quality_msg = " ★ Quality!";
        }
        // Regular crops: remove after harvest
        c.crop = Crop{};
        add_item(p, produce, 1);
        p.money += sell_price;
        say("You harvest a " + std::string(item_def(produce).name) + "! +" +
            std::to_string(sell_price) + "g" + quality_msg);
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
        int season = season_index(w.day);
        int count = 0;
        fish_table(season, count);
        static const struct { const char* name; int price; int min_h, max_h; } fish[5][5] = {
            {{"Anchovy",30,6,21},{"Sardine",40,6,19},{"Bream",55,6,20},{"Halibut",90,6,11},{"Salmon",100,6,21}},
            {{"Tuna",80,6,19},{"Rainbow Trout",100,6,21},{"Sunfish",80,6,19},{"Catfish",100,12,2},{"Pufferfish",180,12,16}},
            {{"Walleye",120,12,2},{"Eel",145,16,2},{"Salmon",100,6,21},{"Midnight Carp",150,22,2},{"Angler",200,6,21}},
            {{"Perch",90,6,21},{"Squid",100,18,2},{"Sturgeon",150,6,21},{"Ice Pip",200,6,21},{"Glacierfish",260,6,21}},
        };
        int hour = hour_of_day(w);
        bool rainy = weather_of_day(w.day) == 1;
        int roll = rand() % 100;
        int catch_chance = rainy ? 65 : 55;
        if (roll < catch_chance) {
            // pick among fish whose active hours cover now (wrap for 2 AM fish)
            const int seasi = season < 0 || season > 3 ? 0 : season;
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
        int season = season_index(w.day);
        static const struct { const char* name; int price; } forage[4][4] = {
            {{"Dandelion",25},{"Wild Horseradish",40},{"Leek",60},{"Morel Mushroom",90}},
            {{"Spice Berry",80},{"Grape",120},{"Sweet Pea",55},{"Red Mushroom",110}},
            {{"Blackberry",60},{"Hazelnut",90},{"Wild Plum",70},{"Chanterelle",160}},
            {{"Snow Yam",100},{"Crystal Fruit",150},{"Crocus",200},{"Winter Root",90}},
        };
        Cell& c = w.at(p.pos);
        if (is_water_any(c.tile) || c.tile == Tile::Tilled) { say("Nothing grows here."); return out; }
        bool rainy = weather_of_day(w.day) == 1;
        bool in_wood = std::string(region_at(w, p.pos.x, p.pos.y)) == "Whisper Wood";
        int roll = rand() % 100;
        int chance = (rainy ? 45 : 35) + (in_wood ? 15 : 0);
        if (roll < chance) {
            int sidx = season < 0 || season > 3 ? 0 : season;
            const auto& f = forage[sidx][rand() % 4];
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
            add_item(p, sap, amount);
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
        if (chance > 0 && (w.day * 7 + f.x * 13 + f.y * 19) % 100 < static_cast<unsigned>(chance)) {
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
        if (p.money < def.buy) { say("You can't afford " + std::string(def.name) + " (" + std::to_string(def.buy) + "g)."); return out; }
        Vec2 d = w.door();
        bool in_shop = p.inside == "General Store" || p.inside == "Market";
        if (!in_shop && (std::abs(int(d.x) - p.pos.x) > 3 || std::abs(int(d.y) - p.pos.y) > 3)) {
            say("The shop is run from your mailbox near the house door,");
            say("or step inside a shop building to 'buy'.");
            return out;
        }
        p.money -= def.buy;
        add_item(p, seed, 1);
        say("You buy " + std::string(def.name) + " for " + std::to_string(def.buy) + "g.");
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
        if (target_is_dynamic) delete target;
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
        w.farmhouse_level = next_level;
        
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
        p.inv[slot].count--;
        if (p.inv[slot].count == 0) p.inv[slot].item = Item::None;
        p.energy = std::min<float>(p.max_energy, p.energy + it->second.second);
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
        p.inv[slot].count--;
        if (p.inv[slot].count == 0) p.inv[slot].item = Item::None;
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
        int season = season_index(w.day);
        p.dir = 0;
        if (found->pos.y < p.pos.y) p.dir = 3;
        else if (found->pos.x > p.pos.x) p.dir = 2;
        else if (found->pos.x < p.pos.x) p.dir = 1;
        say(found->name + ": " + npc_line(found->name.c_str(), season));
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
            char ch = r.rows[sy][r.w / 2];
            if (ch == '.' || ch == ' ' || ch == 'P') { p.iny = sy; break; }
        }
        p.dir = 3;
        say("You step inside the " + p.inside + ".");
        return out;
    }
    if (cmd == "exit" || cmd == "leave") {
        if (p.inside.empty()) { say("You're outside already."); return out; }
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
        say("The basement is damp and wrong. The walls remember your footsteps.");
        say("A voice like your own whispers: \"You keep coming back. Why do you keep coming back?\"");
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
                char ch = room.rows[ty][tx];
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
                        int nd = weather_of_day(w.day + 1) == 1;
                        say("Weather: Tomorrow's forecast — " +
                            (nd ? std::string(weather_of_day_name(w.day + 1)) + ", bring a coat."
                                : std::string(weather_of_day_name(w.day + 1)) + ", skies will clear."));
                        std::vector<std::string> tips = {
                            "SPRING: Blueberry seeds (80g) sell for 100g — plant in Spring for a fat Summer harvest.",
                            "SUMMER: Preserve your blueberries & tomatoes in kegs. Quality stars sell for more.",
                            "FALL: Grapes hang heavy. Harvest before the first frost.",
                            "WINTER: The Travelling Cart visits Thurs–Sun this week. Prices are slashed.",
                        };
                        say("Correspondent: \"" + tips[season_index(w.day)] + "\"");
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
        int nd = weather_of_day(w.day + 1) == 1;
        say("Weather: Tomorrow's forecast — " +
            (nd ? std::string(weather_of_day_name(w.day + 1)) + ", bring a coat."
                : std::string(weather_of_day_name(w.day + 1)) + ", skies will clear."));
        // seasonal consequential tip
        std::vector<std::string> tips = {
            "SPRING: Blueberry seeds (80g) sell for 100g — plant in Spring for a fat Summer harvest.",
            "SUMMER: Preserve your blueberries & tomatoes in kegs. Quality stars sell for more.",
            "FALL: Grapes hang heavy. Harvest before the first frost.",
            "WINTER: The Travelling Cart visits Thurs–Sun this week. Prices are slashed.",
        };
        say("Correspondent: \"" + tips[season_index(w.day)] + "\"");
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
        int nextWeather = weather_of_day(w.day);
        say("Zzz...");
        // Phase 6: the night has its own story. Every sleep after midnight can
        // surface a chapter of the hidden narrative.
        if (sleep_hour >= 22) {
            std::string ev = w.roll_night_event();
            std::istringstream evss(ev);
            std::string line;
            while (std::getline(evss, line)) {
                if (!line.empty()) say(line);
            }
            if (p.night_event_log.size() > 12) p.night_event_log.erase(p.night_event_log.begin());
            p.night_event_log.push_back(ev);
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
            int n = 1 + rnd() % 36;
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
                w.buildings.push_back(barn);
                w.building_states["Barn"] = BuildingState{};
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
                w.buildings.push_back(coop);
                w.building_states["Coop"] = BuildingState{};
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
        int season = season_index(w.day);
        if (season == 0) say("🌸 Spring Festival begins! Villagers gather at the plaza.");
        else if (season == 1) say("☀️ Summer Luau! Beach party tonight.");
        else if (season == 2) say("🍂 Autumn Harvest Festival! Feast and games.");
        else say("❄️ Winter Star Festival! Lights and songs.");
        return out;
    }

    say("I don't understand '" + cmd + "'. Type 'help' for commands.");
    return out;
}

std::vector<std::string> process_intent(World& w, Player& p, const nlohmann::json& intent) {
    std::vector<std::string> out;
    auto say = [&](const std::string& s) { out.push_back(s); };
    std::string action = intent.value("action", "");
    auto params = intent.value("parameters", nlohmann::json::object());

    // For now, reconstruct a simple command string for fallback handling
    std::string cmd_str = action;
    if (!params.empty()) {
        // naive: append first param value
        for (auto it = params.begin(); it != params.end(); ++it) {
            cmd_str += " " + it->dump();
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
        ofs.write(res->body.data(), res->body.size());
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
    for (int i = 0; i < MAP_W * MAP_H; ++i) tile_map[i] = static_cast<uint8_t>(world.cells[i].tile);

    httplib::Server svr;
    svr.set_base_dir("assets");

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        auto path = fs::path("assets/index.html");
        if (fs::exists(path)) {
            std::ifstream f(path);
            res.set_content(std::string(std::istreambuf_iterator<char>(f), {}), "text/html");
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
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<json> plist;
        for (auto& [id, p] : world.players) {
            json inv = json::array();
            for (int i = 0; i < 12; ++i)
                inv.push_back({{"item", static_cast<int>(p.inv[i].item)},
                               {"count", p.inv[i].count}});
            plist.push_back({
                {"player_id", id}, {"x", p.pos.x}, {"y", p.pos.y}, {"dir", p.dir},
                {"moving", p.moving}, {"name", p.name}, {"energy", p.energy},
                {"max_energy", p.max_energy}, {"money", p.money}, {"sel", p.sel},
                {"region", region_at(world, p.pos.x, p.pos.y)},
                {"inside", p.inside}, {"inx", p.inx}, {"iny", p.iny},
                {"inv", inv},
                {"sanity", p.sanity}, {"sanity_tier", world.perception_tier(p)},
            });
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
        resp["weather"] = weather_of_day(world.day);
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
        res.set_content(resp.dump(), "application/json");
    });

    // ---- move (BFS) ----
    svr.Post("/move", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0);
        int16_t tx = j.value("target_x", 0);
        int16_t ty = j.value("target_y", 0);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (!p.inside.empty()) { res.set_content("{}", "application/json"); return; }
            std::vector<Vec2> path;
            if (bfs_path(world, p.pos, {tx, ty}, path)) {
                p.path = std::move(path);
                p.moving = !p.path.empty();
                p.move_start_ms = now_ms();
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
        uint32_t pid = j.value("player_id", 0);
        int16_t wx = j.value("x", 0), wy = j.value("y", 0);
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
        uint32_t pid = j.value("player_id", 0);
        int16_t tx = j.value("x", -9999);
        int16_t ty = j.value("y", -9999);
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
        uint32_t pid = j.value("player_id", 0);
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
            auto lines = handle_cmd(world, it->second, cmd);
            for (auto& l : lines) resp["lines"].push_back(l);
            uint64_t latency = now_ms() - t0;
            cmdlog.record(now_ms(), pid, world.day, season_name(season_index(world.day)),
                          hour_of_day(world), cmd, intent_json, tier, latency, lines);
        } else {
            resp["lines"].push_back("No farmer found. Rejoin the game.");
        }
        res.set_content(resp.dump(), "application/json");
    });

    // ---- sleep: advance a day ----
    svr.Post("/sleep", [&](const httplib::Request& req, httplib::Response& res) {
        json j = json::parse(req.body);
        uint32_t pid = j.value("player_id", 0);
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
        uint32_t pid = j.value("player_id", 0);
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
                    int16_t acx = cx + dx, acy = cy + dy;
                    if (std::abs(acx) > MAX_CHUNK_RADIUS || std::abs(acy) > MAX_CHUNK_RADIUS) continue;
                    const Chunk* ch = world.get_chunk_const(acx, acy);
                    if (ch && ch->generated) {
                        int building_count = ch->buildings.size();
                        int npc_count = ch->npcs.size();
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
        uint32_t pid = j.value("player_id", 0);
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
                    p.pos.x = target_cx * CHUNK_SIZE + CHUNK_SIZE / 2;
                    p.pos.y = target_cy * CHUNK_SIZE + CHUNK_SIZE / 2;
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
        uint32_t pid = j.value("player_id", 0);
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
        uint32_t pid = j.value("player_id", 0);
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
        uint32_t pid = j.value("player_id", 0);
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
        uint32_t pid = j.value("player_id", 0);
        std::string subcmd = j.value("subcmd", "");
        std::string job_id = j.value("job_id", "");
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            Player& p = it->second;
            if (subcmd == "list" || subcmd.empty()) {
                world.add_job_board_entries(); // Refresh daily
                resp["lines"].push_back("=== Job Board ===");
                for (auto& j : world.job_board) {
                    resp["lines"].push_back("[" + j.id + "] " + j.title + " (" + j.type + ")");
                    resp["lines"].push_back("  " + j.description);
                    resp["lines"].push_back("  Reward: " + std::to_string(j.reward_money) + "g" + 
                        (j.reward_item != Item::None ? ", " + std::to_string(j.reward_count) + "x " + item_def(j.reward_item).name : ""));
                    if (j.cooldown_until > world.day) {
                        resp["lines"].push_back("  Cooldown: " + std::to_string(j.cooldown_until - world.day) + " days");
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
        uint32_t pid = j.value("player_id", 0);
        json resp = {{"lines", json::array()}};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto it = world.players.find(pid); it != world.players.end()) {
            world.update_market_prices();
            resp["lines"].push_back("=== Market Prices (Day " + std::to_string(world.day) + ") ===");
            for (auto& mp : world.market_prices) {
                if (mp.current_price > 0) {
                    float ratio = static_cast<float>(mp.current_price) / mp.base_price;
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
        uint32_t pid = j.value("player_id", 0);
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
        uint32_t pid = j.value("player_id", 0);
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
                resp["lines"].push_back("The basement is damp and wrong. The walls remember your footsteps.");
                resp["lines"].push_back("A voice like your own whispers: \"You keep coming back.\"");
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
                for (auto& [id, p] : world.players) {
                    if (p.moving && !p.path.empty() &&
                        (now_ms() - p.move_start_ms) >= 110) {
                        Vec2 next = p.path.front();
                        if (world.walkable(next)) p.pos = next;
                        p.path.erase(p.path.begin());
                        p.move_start_ms = now_ms();
                        if (p.pos == p.target || p.path.empty()) p.moving = false;
                    }
                }
                for (auto& n : world.npcs) {
                    if (now_ms() < n.next_move_ms) continue;
                    n.next_move_ms = now_ms() + 400 + rand() % 300;
                    if (hour_of_day(world) >= 21) continue;   // villagers turn in at night
                    Vec2 anchor;
                    int slot = schedule_slot(n.name, world.day, hour_of_day(world), anchor);
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