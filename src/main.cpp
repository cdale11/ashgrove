#include "world.hpp"
#include "protocol.hpp"
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
#include <cctype>
#include <ctime>

namespace fs = std::filesystem;
using namespace std::chrono;

std::mutex g_mutex;

static uint64_t now_ms() {
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
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
                cell.crop.days_left--;
                // Recompute stage based on elapsed time vs total days.
                // Stage 0 = just planted, stage 3 = ready to harvest.
                const CropDef* cd = crop_def(cell.crop.crop);
                if (cd) {
                    int total = std::max(1, (int)cd->days);
                    int elapsed = total - cell.crop.days_left;
                    cell.crop.stage = std::min<uint8_t>(
                        (uint8_t)((elapsed * 4) / total), (uint8_t)3);
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
    }
    save_world(w, "save.json");
}

// Nudge one NPC toward its current waypoint.
static void step_npc(World& w, NPC& n) {
    Vec2 target = n.way[n.way_idx];
    if (n.pos == target) { n.way_idx ^= 1; return; }
    int dx = target.x > n.pos.x ? 1 : target.x < n.pos.x ? -1 : 0;
    int dy = target.y > n.pos.y ? 1 : target.y < n.pos.y ? -1 : 0;
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
    if (tool == Item::None && c.crop.is_crop() && c.crop.stage == 3 && c.crop.days_left <= 0) {
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
        if (c.obj.type != ObjType::Tree && c.obj.type != ObjType::Stump &&
            c.obj.type != ObjType::Pine) return "";
        if (!spend(def.energy)) return "Exhausted";
        if (--c.obj.hp > 0) return "";
        ObjType was = c.obj.type;
        c.obj = FarmObj{};
        if (was == ObjType::Stump) {
            add_item(p, Item::Wood, 2);
            return "+2 wood";
        }
        c.obj = {ObjType::Stump, 1};
        add_item(p, Item::Wood, 4);
        return was == ObjType::Pine ? "+4 wood (pine resin drips)" : "+4 wood";
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
        c.obj = FarmObj{};
        add_item(p, Item::Fiber, 1);
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
            c.crop.days_left = cd->days;
            c.crop.watered = false;
            return "Planted";
        }
    }
    return "";
}

// ---------- text command interpreter ----------

static std::string lower_trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
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
    default: return nullptr;
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
    for (int i = 0; i < 12; ++i)
        if (p.inv[i].item == it) return i;
    return -1;
}
static int count_item(Player& p, Item it) {
    int n = 0;
    for (int i = 0; i < 12; ++i) if (p.inv[i].item == it) n += p.inv[i].count;
    return n;
}
static bool has_item(Player& p, Item it, int need) { return count_item(p, it) >= need; }
static void consume_item(Player& p, Item it, int need) {
    for (int i = 0; i < 12 && need > 0; ++i) if (p.inv[i].item == it) {
        int t = std::min<int>(need, (int)p.inv[i].count);
        p.inv[i].count -= (uint16_t)t; need -= t;
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
    for (int i = 1; i <= 35; ++i) {
        Item it = (Item)i;
        std::string nm = lower_trim(item_def(it).name);
        if (nm == lc) return it;
    }
    // plural / "seeds" suffix
    for (int i = 1; i <= 35; ++i) {
        Item it = (Item)i;
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
        say("  go <dir>       move (north/south/east/west, or n/s/e/w)");
        say("  go <dir> <n>   walk n tiles (e.g. 'go 5 north', 'go e 10')");
        say("  go to <name>   path-walk to a known landmark/building");
        say("  look           examine your surroundings (alias: l)");
        say("  inventory      list what you carry (alias: inv)");
        say("  status         day, time, energy, money (alias: stats)");
        say("  hoe            till soil in front of you");
        say("  plant <crop>   plant seeds on tilled soil");
        say("  water          water the soil in front of you");
        say("  harvest        pick a ripe crop");
        say("  axe            chop a tree   |   pick  mine a rock");
        say("  scythe         clear weeds   |   forage near grass");
        say("  fish           cast a line if water is near");
        say("  talk <name>    chat with a nearby villager");
        say("  gift <n> <it>  give a present to an adjacent villager");
        say("  eat <item>     snack for energy (bread, forage, crops)");
        say("  buy <item>     buy at the shop (type 'shop')");
        say("  sell <item>    sell produce/items");
        say("  craft <item>  make things from resources (bread)");
        say("  place <thing>  build a structure: place sprinkler (2 Iron + 1 Gold)");
        say("  enter [<name>] step into a building (stand at its door)");
        say("  exit           leave the building you're in (alias: leave)");
        say("  interact       use what's around you inside (alias: use)");
        say("  train          ride the Zuzu City Express from the station");
        say("  bus            take the town bus to the plaza");
        say("  tv             watch valley news in the farmhouse");
        say("  hearts         check friendship with the villagers");
        say("  festival       join in (Spring 13)  |  search <patch> find eggs");
        say("  sleep          rest until morning (near the house door)");
        say("  save [<name>]  write the whole game state (default: save.json)");
        say("  load [<name>]  restore a save file (list with 'saves')");
        say("  newgame        start a fresh farm — the old save is kept as a backup");
        say("  saves          list save files on the server");
        say("  tv             watch the valley news (in the farmhouse)");
        return out;
    }

    // ---------- status / inventory ----------
    if (cmd == "status" || cmd == "stats") {
        say(clock_str(w));
        say(std::string(season_name(season_index(w.day))) + " " +
            std::to_string(season_day(w.day)) + " · " + weather_of_day_name(w.day));
        say("Energy: " + std::to_string((int)p.energy) + "/" + std::to_string(p.max_energy) +
            "   Money: " + std::to_string(p.money) + "g");
        return out;
    }
    if (cmd == "inventory" || cmd == "inv") {
        bool any = false;
        for (int i = 0; i < 12; ++i)
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
        
        // Check for "go to <landmark>" syntax
        if (d == "to" && !arg.empty()) {
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
                    p.move_start_ms = now_ms();
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
                p.move_start_ms = now_ms();
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
        int dx = 0, dy = 0;
        if (it->second == 3) dy = -1; else if (it->second == 0) dy = 1;
        else if (it->second == 2) dx = 1; else dx = -1;
        p.dir = (uint8_t)it->second;
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
                if (nx < 0 || nx >= r.w || ny < 0 || r.rows[ny][nx] == '#') {
                    if (walked > 0) say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
                    say("A wall stops you."); return out;
                }
                char ch = r.rows[ny][nx];
                if (ch != '.' && ch != ' ' && ch != 'P') {
                    if (walked > 0) say("You walk " + std::to_string(walked) + " step" + (walked > 1 ? "s" : "") + " " + std::string(dn[it->second]) + ".");
                    say("Blocked by " + furniture_name(ch) + "."); return out;
                }
                p.inx = (int16_t)nx;
                p.iny = (int16_t)ny;
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
            char here = r.rows[p.iny][p.inx];
            if (here != '.' && here != ' ' && here != 'P')
                say("You stand beside " + furniture_name(here) + ".");
            std::vector<std::string> around;
            for (auto [ox, oy, di] : std::vector<std::tuple<int,int,const char*>>{{0,-1,"north"},{0,1,"south"},{-1,0,"west"},{1,0,"east"}}) {
                int tx = p.inx + ox, ty = p.iny + oy;
                if (tx < 0 || tx >= r.w || ty < 0 || ty >= r.h) continue;
                char c = r.rows[ty][tx];
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
        // NPCs in earshot
        std::vector<std::string> folks;
        for (auto& n : w.npcs) {
            int dist = std::abs(int(n.pos.x) - p.pos.x) + std::abs(int(n.pos.y) - p.pos.y);
            if (dist <= 3)
                folks.push_back(n.name + (dist <= 1 ? " (right here)" :
                    " to the " + std::string(n.pos.y < p.pos.y ? "north" : n.pos.y > p.pos.y ? "south" :
                                            n.pos.x > p.pos.x ? "east" : "west")));
        }
        for (auto& f : folks) say("You see " + f + ". Try 'talk " + f.substr(0, f.find(' ')) + "'.");
        return out;
    }

    // ---------- tool actions (act on facing cell) ----------
    auto grab_tool = [&](Item tool) -> bool {
        int slot = find_slot(p, tool);
        if (slot < 0) { say("You don't have a " + std::string(item_def(tool).name) + "."); return false; }
        p.sel = (uint8_t)slot;
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
        if (!crop) { say("Plant what? parsnip, potato, cauliflower, corn, tomato, wheat or blueberry."); return out; }
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
        p.sel = (uint8_t)slot;
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
        c.crop.days_left = crop->days;
        c.crop.watered = false;
        say("You plant " + std::string(crop->name) + " seeds. (" +
            std::to_string(crop->days) + " days to harvest)");
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
        c.crop = Crop{};
        add_item(p, produce, 1);
        p.money += item_def(produce).sell;
        say("You harvest a " + std::string(item_def(produce).name) + "! +" +
            std::to_string(item_def(produce).sell) + "g");
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

    // ---------- shop ----------
if (cmd == "buy") {
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
        p.inv[slot].count--;
        if (p.inv[slot].count == 0) p.inv[slot].item = Item::None;
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
        if (thing == "bread") {
            if (!has_item(p, Item::Wheat, 3)) {
                say("Recipe: bread — need 3 Wheat."); return out;
            }
            consume_item(p, Item::Wheat, 3);
            add_item(p, Item::Bread, 1);
            say("Crafted Bread (3 Wheat).");
            return out;
        }
        say("Recipes: 'craft copper bar' (5 ore + 1 wood), 'craft iron bar', 'craft gold bar', 'craft bread' (3 wheat).");
        say("Place machines: 'place sprinkler' (2 Iron Bar + 1 Gold Bar).");
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
            c.obj = {ObjType::Sprinkler, 255, (uint8_t)(big ? 3 : 2)}; // ore field = tier
            say("Placed a " + std::string(big ? "Iridium" : "Steel") + " Sprinkler on the farmland.");
            say("     It will water adjacent tiles overnight.");
            return out;
        }
        say("Can place: sprinkler (2 Iron Bar + 1 Gold Bar).");
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
            {"forage", {Item::Forage, 25}}, {"fish", {Item::Fish, 20}},
        };
        auto it = foods.find(arg);
        if (it == foods.end()) {
            say("Eat what? bread, parsnip, potato, cauliflower, corn, tomato, wheat, blueberry, forage, fish.");
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
                p.inside = "Farmhouse";
                p.inside_exit = d;
            }
            for (auto& b : w.buildings)
                if (p.pos.x == b.x + (b.w - 1) / 2 && p.pos.y == b.y + b.h) {
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
                p.inside = "Farmhouse";
                p.inside_exit = d;
            } else {
                for (auto& b : w.buildings)
                    if (lower_trim(b.name).find(nm) != std::string::npos) { target = &b; break; }
                if (!target) { say("There's no '" + arg + "' around here."); return out; }
                if (p.pos.x != target->x + (target->w - 1) / 2 || p.pos.y != target->y + target->h) {
                    say("Stand at the door of " + std::string(target->name) + " first."); return out;
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
        p.inx = (int16_t)(r.w / 2);
        p.iny = (int16_t)(r.h - 2);
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
        say("You step out the door.");
        p.inside.clear();
        p.pos = p.inside_exit;
        p.target = p.pos;
        p.path.clear();
        p.moving = false;
        return out;
    }

    // ---------- interact ----------
    if (cmd == "interact" || cmd == "use") {
        if (p.inside.empty()) {
            say("There's nothing to interact with out here. Try 'enter' at a building's door.");
            return out;
        }
        const std::string& b = p.inside;
        if (b == "Farmhouse") {
            advance_day(w);
            say("You crawl into bed and sleep...");
            say("--- Day " + std::to_string(w.day) + " · " +
                std::string(season_name(season_index(w.day))) + " ---");
            say("You wake up refreshed. Energy restored.");
        } else if (b == "General Store" || b == "Market") {
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
        advance_day(w);
        std::string new_season = season_name(season_index(w.day));
        int nextWeather = weather_of_day(w.day);
        say("Zzz...");
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
            if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') return false;
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

    say("I don't understand '" + cmd + "'. Type 'help' for commands.");
    return out;
}

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    std::srand((unsigned)std::time(nullptr));
    World world;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        generate_world(world);
        if (load_or_generate(world)) std::cout << "Loaded save\n";
        else std::cout << "New world generated\n";
        init_npcs(world);
    }

    // precompute static tile map (never changes after gen)
    std::vector<uint8_t> tile_map(MAP_W * MAP_H);
    for (int i = 0; i < MAP_W * MAP_H; ++i) tile_map[i] = (uint8_t)world.cells[i].tile;

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
            if (j.contains("sel")) p.sel = (uint8_t)std::clamp<int>(int(j["sel"]), 0, 11);
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
        std::lock_guard<std::mutex> lock(g_mutex);
        json resp = {{"lines", json::array()}};
        if (auto it = world.players.find(pid); it != world.players.end()) {
            auto lines = handle_cmd(world, it->second, cmd);
            for (auto& l : lines) resp["lines"].push_back(l);
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
                        n.sched_slot = (int8_t)slot;
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
    svr.listen("0.0.0.0", port);
    return 0;
}