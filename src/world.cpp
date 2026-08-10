#include "world.hpp"
#include <nlohmann/json.hpp>
#include <random>
#include <cmath>
#include <algorithm>
#include <deque>
#include <sstream>
#include <iostream>

using json = nlohmann::json;

static float noise(int x, int y, float scale) {
    return std::sin((x + y) * scale) * std::cos((x - y) * scale * 0.7f) * 0.5f + 0.5f;
}

static bool is_water(Tile t) {
    return t == Tile::Water || t == Tile::WaterNorth || t == Tile::WaterSouth ||
           t == Tile::WaterEast || t == Tile::WaterWest;
}

void generate_world(World& world) {
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // ---- base grass ----
    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            float v = noise(x, y, 0.35f) + noise(x, y, 0.9f) * 0.5f;
            world.at(x, y).tile = v > 0.62f ? Tile::GrassVar : Tile::Grass;
        }
    }

    // ---- snow biome in the far north (wavy treeline, clear around the house) ----
    auto snow_line = [&](int x) {
        return int(12 + noise(x, 3, 0.22f) * 5.0f - 2.5f);
    };
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            if (y >= snow_line(x)) continue;
            if (x >= world.house_tl.x - 3 && x < world.house_tl.x + 7 &&
                y <= world.house_tl.y + 6) continue;          // keep the homestead green
            if (world.at(x, y).tile == Tile::Grass || world.at(x, y).tile == Tile::GrassVar)
                world.at(x, y).tile = Tile::Snow;
        }

    // ---- frozen glacier lake up in the tundra ----
    float fl_cx = 34.0f + noise(0, 9, 0.5f) * 4.0f, fl_cy = 3.5f;
    {
        float lcx = fl_cx, lcy = fl_cy;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < MAP_W; ++x) {
                float d = std::hypot((x - lcx) * 1.35f, (y - lcy));
                if (d < 3.2f + 1.2f * std::sin(x * 0.6f) * std::cos(y * 0.5f) + 1.4f * noise(x, y, 0.4f))
                    world.at(x, y).tile = Tile::Ice;
            }
    }

    // ---- river (meanders N-S near x~29) ----
    float cx = MAP_W * 0.30f;
    for (int y = 0; y < MAP_H; ++y) {
        cx += std::sin(y * 0.09f) * 1.8f + std::sin(y * 0.23f + 2.0f) * 1.1f;
        for (int dx = -1; dx <= 1; ++dx) {
            int ix = int(cx) + dx;
            if (world.in_bounds(ix, y)) world.at(ix, y).tile = Tile::Water;
        }
    }
    // carve a mid-map bridge crossing at y~30 so west/east halves stay connected
    {
        float bcx = 29.0f + std::sin(30 * 0.09f) * 1.8f + std::sin(30 * 0.23f + 2.0f) * 1.1f;
        int bx = int(bcx);
        for (int dx = -2; dx <= 2; ++dx) {
            int ix = bx + dx;
            if (world.in_bounds(ix, 30)) world.at(ix, 30).tile = Tile::Bridge;
        }
        // dirt path leading to the bridge from both sides
        for (int x = 14; x < bx - 2; ++x) { if (world.in_bounds(x, 30) && !is_water(world.at(x,30).tile)) world.at(x,30).tile = Tile::Dirt; }
        for (int x = bx + 3; x < 50; ++x) { if (world.in_bounds(x, 30) && !is_water(world.at(x,30).tile)) world.at(x,30).tile = Tile::Dirt; }
    }

    // ---- western stream: tumbles out of the tundra, joins the river mid-south ----
    {
        float sx = 20.0f;
        for (int y = 10; y < 42; ++y) {
            sx += std::sin(y * 0.17f) * 0.9f + std::cos(y * 0.31f) * 0.6f;
            sx += (29.0f - sx) * 0.06f;   // drift back toward the main river
            for (int dx = -1; dx <= 0; ++dx) {
                int ix = int(sx) + dx;
                if (world.in_bounds(ix, y)) world.at(ix, y).tile = Tile::Water;
            }
        }
    }

    // ---- pond lower-left ----
    float pcx = MAP_W * 0.22f, pcy = MAP_H * 0.78f;
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            if (std::hypot((x - pcx) * 1.6f, y - pcy) < 5.5f)
                world.at(x, y).tile = Tile::Water;

    // ---- Mirror Lake: big round lake in the south meadow ----
    {
        float mcx = 42.0f, mcy = 46.0f;
        for (int y = 0; y < MAP_H; ++y)
            for (int x = 0; x < MAP_W; ++x) {
                float d = std::hypot((x - mcx) * 1.45f, (y - mcy));
                if (d < 6.8f + 1.1f * std::sin(x * 0.35f + y * 0.2f))
                    world.at(x, y).tile = Tile::Water;
            }
    }

    // ---- southern ocean: the whole bottom edge falls away into the sea ----
    for (int y = MAP_H - 3; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            world.at(x, y).tile = Tile::Water;

    std::array<bool, MAP_W * MAP_H> was_water{};
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            was_water[y * MAP_W + x] = is_water(world.at(x, y).tile);

    // ---- dirt paths (planned town) ----
    auto road = [&](int x, int y) {
        if (!world.in_bounds(x, y) || is_water(world.at(x, y).tile)) return;
        world.at(x, y).tile = Tile::Dirt;
    };
    // main E-W high street (gently curving), from the farm doorstep to the east edge
    for (int x = 8; x < MAP_W - 6; ++x) {
        int y = 10 + int(std::round(std::sin(x * 0.11f) * 1.5f));
        road(x, y); road(x, y + 1);
    }
    // N-S lane linking high street to the southern farm gate
    for (int y = 9; y < 40; ++y) { road(60, y); road(61, y); }
    // farm drive: high street -> farm gate row
    for (int x = 54; x < 76; ++x) road(x, 20);
    // town square (west of the plaza, east of the river bridge)
    for (int y = 14; y < 20; ++y)
        for (int x = 42; x < 52; ++x) road(x, y);
    // lakeside boardwalk: stardrop plaza -> mirror lake
    for (int y = 20; y < 46; ++y) road(77, y);
    road(77, 46); road(78, 46); road(79, 46); road(80, 46);   // lake shore stalls

    // path + river crossings become bridges
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            if (was_water[y * MAP_W + x] && world.at(x, y).tile == Tile::Dirt)
                world.at(x, y).tile = Tile::Bridge;

    // bridge any short water run that a road punches through (the stream cuts
    // the high street, the river cuts its own crossing) — walk a path, and
    // where consecutive water tiles sit between two road tiles, plank them.
    auto bridge_run = [&](int step, auto&& next_x, auto&& next_y) {
        for (int i = 0; i < step; ++i) {
            int x = next_x(i), y = next_y(i);
            if (!world.in_bounds(x, y) || !is_water(world.at(x, y).tile)) continue;
            int j = i;
            while (j + 1 < step) {
                int nx = next_x(j + 1), ny = next_y(j + 1);
                if (!world.in_bounds(nx, ny) || !is_water(world.at(nx, ny).tile)) break;
                ++j;
            }
            bool lc = i > 0;
            bool rc = j + 1 < step;
            if (lc && rc && (j - i + 1) <= 6) {
                for (int k = i; k <= j; ++k) {
                    int bx = next_x(k), by = next_y(k);
                    if (world.at(bx, by).obj.type == ObjType::None)
                        world.at(bx, by).tile = Tile::Bridge;
                }
            }
            i = j;
        }
    };
    // high street (both rows of the lane)
    bridge_run(MAP_W - 14, [](int i) { return i + 8; },
               [](int i) { return 10 + int(std::round(std::sin((i + 8) * 0.11f) * 1.5f)); });
    bridge_run(MAP_W - 14, [](int i) { return i + 8; },
               [](int i) { return 10 + int(std::round(std::sin((i + 8) * 0.11f) * 1.5f)) + 1; });
    // N-S lane down to the farm gate
    bridge_run(31, [](int) { return 60; }, [](int i) { return i + 9; });
    bridge_run(31, [](int) { return 61; }, [](int i) { return i + 9; });
    // lakeside boardwalk
    bridge_run(27, [](int) { return 77; }, [](int i) { return i + 20; });

    // ---- farm soil patch ----
    for (int y = 10; y < 20; ++y)
        for (int x = MAP_W - 26; x < MAP_W - 6; ++x)
            world.at(x, y).tile = Tile::Tilled;

    // ---- sand near water ----
    const std::pair<int,int> dirs[4] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            if (world.at(x,y).tile == Tile::Grass || world.at(x,y).tile == Tile::GrassVar)
                for (auto [dx, dy] : dirs) {
                    int nx = x + dx, ny = y + dy;
                    if (world.in_bounds(nx, ny) && is_water(world.at(nx, ny).tile)) {
                        world.at(x, y).tile = Tile::Sand;
                        break;
                    }
                }

    // ---- house + fences around the farm patch ----
    auto blocked_house = [&](int x, int y) {
        return x >= world.house_tl.x - 1 && x < world.house_tl.x + 5 &&
               y >= world.house_tl.y - 1 && y < world.house_tl.y + 6;
    };
    auto on_farm_patch = [&](int x, int y) {
        return x >= MAP_W - 26 && x < MAP_W - 6 && y >= 10 && y < 20;
    };
    auto in_snow = [&](int x, int y) { return y < snow_line(x); };

    auto scatter = [&](int count, ObjType type, int hp) {
        for (int i = 0; i < count; ++i) {
            int x = 4 + int(dist(rng) * (MAP_W - 8));
            int y = 4 + int(dist(rng) * (MAP_H - 8));
            Cell& c = world.at(x, y);
            if (is_water(c.tile) || c.tile == Tile::Ice || blocked_house(x, y) ||
                on_farm_patch(x, y) || c.obj.type != ObjType::None ||
                c.tile == Tile::Dirt || c.tile == Tile::Tilled)
                continue;
            c.obj = {type, uint8_t(hp)};
        }
    };
    scatter(70, ObjType::Tree, 3);
    scatter(35, ObjType::Rock, 2);
    scatter(55, ObjType::Weed, 1);
    scatter(25, ObjType::TallGrass, 1);
    scatter(40, ObjType::Flower, 1);
    scatter(50, ObjType::Bush, 1);
    scatter(18, ObjType::Mushroom, 1);

    // ---- Whisper Wood: dense old-growth forest west of the stream ----
    for (int y = 16; y < 46; ++y)
        for (int x = 4; x < 20; ++x) {
            if (!world.in_bounds(x, y)) continue;
            if (is_water(world.at(x,y).tile) || world.at(x,y).tile == Tile::Dirt ||
                world.at(x,y).tile == Tile::Tilled || world.at(x,y).tile == Tile::Bridge)
                continue;
            float d = dist(rng);
            if (d < 0.35f) world.at(x, y).obj = {ObjType::Pine, 3};
            else if (d < 0.48f) world.at(x, y).obj = {ObjType::Tree, 3};
            else if (d < 0.58f) world.at(x, y).obj = {ObjType::Bush, 1};
            else if (d < 0.66f) world.at(x, y).obj = {ObjType::Mushroom, 1};
            else if (d < 0.72f) world.at(x, y).obj = {ObjType::Flower, 1};
        }
    // carve explicit corridors through Whisper Wood so it's always passable
    // horizontal trail at y=24 and y=38, vertical trail at x=12
    for (int x = 4; x < 21; ++x) {
        if (world.in_bounds(x, 24)) world.at(x, 24).obj = FarmObj{};
        if (world.in_bounds(x, 38)) world.at(x, 38).obj = FarmObj{};
    }
    for (int y = 16; y < 46; ++y) {
        if (world.in_bounds(12, y)) world.at(12, y).obj = FarmObj{};
    }

    // ---- tundra pine barrens: scattered pines + some rocks under the snow line ----
    for (int y = 3; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            if (!in_snow(x, y)) continue;
            if (x >= world.house_tl.x - 3 && x < world.house_tl.x + 7 &&
                y <= world.house_tl.y + 6) continue;
            Cell& c = world.at(x, y);
            if (c.tile != Tile::Snow || c.obj.type != ObjType::None) continue;
            float d = dist(rng);
            if (d < 0.30f) c.obj = {ObjType::Pine, 3};
            else if (d < 0.36f) c.obj = {ObjType::Rock, 2};
        }
    // carve a clear corridor from the high street up to the glacier
    for (int y = 3; y < 14; ++y) {
        if (world.in_bounds(40, y)) world.at(40, y).obj = FarmObj{};
        if (world.in_bounds(41, y)) world.at(41, y).obj = FarmObj{};
    }

    // ---- winding trails + glades, so no forest pocket is ever sealed away ----
    for (int y = 2; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = world.at(x, y);
            if (c.obj.type != ObjType::Pine && c.obj.type != ObjType::Tree &&
                c.obj.type != ObjType::Bush) continue;
            if ((x * 3 + y * 7) % 23 < 5) c.obj = FarmObj{};
        }
    // wide clearing ring around the frozen lake + a solid footpath corridor east of
    // the river bank that runs from the high street bridge up to the ice
    for (int y = 0; y < 20; ++y)
        for (int x = 22; x < 52; ++x) {
            float d = std::hypot((x - fl_cx) * 1.35f, y - fl_cy);
            if (d < 9.5f) { Cell& c = world.at(x, y); c.obj = FarmObj{}; }
            else if (y >= 6 && y <= 20 && x >= 33 && x <= 44)
                { Cell& c = world.at(x, y); c.obj = FarmObj{}; }
        }

    // orchard row along the south side of the high street
    for (int x = 14; x < MAP_W - 8; x += 3) {
        int y = 12 + int(std::round(std::sin(x * 0.11f) * 1.5f));
        Cell& c = world.at(x, y);
        if (is_water(c.tile) || c.obj.type != ObjType::None) continue;
        c.obj = {ObjType::Tree, 3};
    }
    // town square flower beds
    for (int y = 15; y < 19; ++y)
        for (int x = 43; x < 51; ++x)
            if ((x + y) % 3 == 0)
                world.at(x, y).obj = {ObjType::Flower, 1};
    // mirror lake shore: reeds + rocks ring
    for (int y = 40; y < 54; ++y)
        for (int x = 34; x < 54; ++x) {
            Cell& c = world.at(x, y);
            if (c.tile != Tile::Grass && c.tile != Tile::GrassVar && c.tile != Tile::Sand) continue;
            float ring = std::hypot((x - 42.0f) * 1.45f, y - 46.0f);
            if (ring > 6.4f && ring < 7.6f && dist(rng) < 0.5f)
                c.obj = {ObjType::Bush, 1};
            else if (ring > 7.6f && ring < 9.0f && dist(rng) < 0.25f)
                c.obj = {ObjType::TallGrass, 1};
        }
    // dense border woodlands (keep the map edge green and drivable)
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            bool edge = x < 3 || x >= MAP_W - 3 || y < 2 || y >= MAP_H - 3;
            if (edge && dist(rng) < 0.75f) {
                Cell& c = world.at(x, y);
                if (is_water(c.tile) || c.tile == Tile::Ice || c.obj.type != ObjType::None ||
                    c.tile == Tile::Dirt)
                    continue;
                c.obj = {in_snow(x, y) ? ObjType::Pine : ObjType::Tree, 3};
            }
        }

    // ore deposits: copper in the tundra highlands (north), iron mid, gold in the
    // mountain ridges east/south, iridium rare everywhere rocky.
    // Also upgrades existing plain rocks (from scatter) to ore-bearing rocks.
    for (int y = 8; y < MAP_H - 6; ++y)
        for (int x = 4; x < MAP_W - 4; ++x) {
            Cell& c = world.at(x, y);
            // skip water/ice/snow tiles
            if (is_water(c.tile) || c.tile == Tile::Ice || c.tile == Tile::Snow) continue;
            // allow ore on empty tiles OR on existing plain rocks (no ore yet)
            bool plain_rock = (c.obj.type == ObjType::Rock && c.obj.ore == 0);
            if (c.obj.type != ObjType::None && !plain_rock) continue;
            float r = dist(rng);
            uint8_t tier = 0;
            if (r < 0.025f) tier = 4;          // iridium
            else if (r < 0.09f && (x > 60 || y < 14)) tier = 3;  // gold ridges
            else if (r < 0.22f && y < 30) tier = 1;            // copper north
            else if (r < 0.26f) tier = 2;                        // iron mid
            if (tier) {
                int hp = tier == 1 ? 2 : 3;
                c.obj = {ObjType::Rock, (uint8_t)hp, tier};
            }
        }

    // fence ring around farm patch (gap at the farm drive gate)
    for (int x = MAP_W - 27; x < MAP_W - 5; ++x) {
        for (auto y : {9, 20}) {
            if (y == 20 && x >= 72 && x <= 75) continue;  // gate
            if (world.at(x, y).obj.type == ObjType::None)
                world.at(x, y).obj = {ObjType::FencePost, 255};
        }
    }
    for (int y = 10; y < 20; ++y) {
        for (auto x : {MAP_W - 27, MAP_W - 6}) {
            if (world.at(x, y).obj.type == ObjType::None)
                world.at(x, y).obj = {ObjType::FencePost, 255};
        }
    }

    place_buildings(world);
    init_interiors(world);
    resolve_water_edges(world);
    // cart-track west from the village street to the bus stop
    for (int x = 9; x <= 28; ++x) {
        Cell& c = world.at(x, 10);
        if (is_water(c.tile) || c.obj.type == ObjType::Building) continue;
        c.tile = Tile::Dirt;
        c.obj = FarmObj{};
    }
    // station approach: lane along the station's south edge from the boardwalk
    for (int y = 33; y <= 33; ++y)
        for (int x = 77; x <= 91; ++x) {
            Cell& c = world.at(x, y);
            if (is_water(c.tile) || c.obj.type == ObjType::Building) continue;
            c.tile = Tile::Dirt;
            c.obj = FarmObj{};
        }
    // ensure house is clear of river water + keep door tile walkable grass
    for (int y = world.house_tl.y - 1; y < world.house_tl.y + 5; ++y)
        for (int x = world.house_tl.x - 1; x < world.house_tl.x + 4; ++x)
            if (world.in_bounds(x, y) && (is_water(world.at(x, y).tile) ||
                world.at(x, y).tile == Tile::Dirt))
                world.at(x, y).tile = Tile::Grass;
}

void resolve_water_edges(World& world) {
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            if (world.at(x, y).tile != Tile::Water) continue;
            bool n = world.in_bounds(x, y-1) && is_water(world.at(x, y-1).tile);
            bool s = world.in_bounds(x, y+1) && is_water(world.at(x, y+1).tile);
            bool w = world.in_bounds(x-1, y) && is_water(world.at(x-1, y).tile);
            bool e = world.in_bounds(x+1, y) && is_water(world.at(x+1, y).tile);
            if (!(n && s && w && e)) {
                if (!n && w && e) world.at(x, y).tile = Tile::WaterNorth;
                else if (!s)      world.at(x, y).tile = Tile::WaterSouth;
                else if (!e)      world.at(x, y).tile = Tile::WaterEast;
                else if (!w)      world.at(x, y).tile = Tile::WaterWest;
                else              world.at(x, y).tile = Tile::Water;
            }
        }
}

void place_buildings(World& world) {
    static const struct { const char* name; int16_t x, y, w, h; } bs[] = {
        // village shops (north row, backing the tundra)
        {"Blacksmith", 42, 8, 2, 2},
        {"General Store", 46, 8, 2, 2},
        {"Old Mill", 50, 8, 2, 2},
        {"Clinic", 54, 8, 2, 2},
        {"Museum", 58, 8, 2, 2},
        // plaza civic buildings
        {"Town Center", 42, 13, 4, 3},
        {"Stardrop Saloon", 48, 13, 3, 3},
        // market row on the plaza's south edge
        {"Market", 43, 20, 4, 2},
        // residential houses tucked around the village
        {"Willow House", 32, 12, 2, 2},
        {"Maple House", 62, 13, 2, 2},
        {"Rowan Cottage", 66, 12, 2, 2},
        // travel
        {"Bus Stop", 8, 8, 2, 2},
        {"Railway Station", 86, 27, 9, 6},
        // farm outbuildings
        {"Hawthorn Barn", 84, 22, 3, 3},
        {"Glasshouse", 70, 22, 2, 2},
    };
    for (auto& b : bs) {
        world.buildings.push_back({b.name, b.x, b.y, b.w, b.h});
        for (int y = b.y; y < b.y + b.h; ++y)
            for (int x = b.x; x < b.x + b.w; ++x) {
                if (!world.in_bounds(x, y)) continue;
                Cell& c = world.at(x, y);
                c.tile = Tile::Grass;
                c.crop = Crop{};
                c.obj = {ObjType::Building, 255};
            }
        // keep the doorstep clear + passable
        int dx = b.x + (b.w - 1) / 2, dy = b.y + b.h;
        if (world.in_bounds(dx, dy)) {
            Cell& d = world.at(dx, dy);
            d.crop = Crop{};
            d.obj = FarmObj{};
            if (is_water(d.tile)) d.tile = Tile::Grass;
        }
    }

    // ---- rail corridor running out of the station toward the eastern sea ----
    for (int y = 31; y <= 32; ++y)
        for (int x = 66; x < 86; ++x) {
            if (!world.in_bounds(x, y)) continue;
            Cell& c = world.at(x, y);
            if (is_water(c.tile)) continue;
            c.tile = Tile::Dirt;
            c.obj = FarmObj{};
        }
}

// interior furniture: '#' wall, '.' floor, letters = furnishings, ' ' = doorway.
// everything except '.', ' ' and 'P' blocks movement. Stepping down from the
// last walkable row exits the building. Door is always bottom-centre.
void init_interiors(World& world) {
    auto room = [&](const char* building, std::vector<std::string> rows) {
        InteriorRoom r;
        r.building = building;
        r.rows = rows;
        r.h = (int16_t)rows.size();
        r.w = (int16_t)(r.h ? rows[0].size() : 0);
        world.interiors[building] = r;
    };
    room("Farmhouse", {
        "#######",
        "#B.TV.#",
        "#.....#",
        "#.S...#",
        "##   ##",
    });
    room("General Store", {
        "#######",
        "#CCC..#",
        "#.....#",
        "#.....#",
        "##   ##",
    });
    room("Market", {
        "#######",
        "#MMMM.#",
        "#.....#",
        "#.....#",
        "##   ##",
    });
    room("Stardrop Saloon", {
        "########",
        "#BB..T##",
        "#......#",
        "#...TT.#",
        "#......#",
        "##   ###",
    });
    room("Clinic", {
        "#######",
        "#DDD..#",
        "#.....#",
        "#.B..B#",
        "##   ##",
    });
    room("Museum", {
        "########",
        "#E.E.E.#",
        "#....E.#",
        "#......#",
        "#E.E...#",
        "##   ###",
    });
    room("Blacksmith", {
        "#######",
        "#A.FF.#",
        "#.....#",
        "#.....#",
        "##   ##",
    });
    room("Old Mill", {
        "######",
        "#X..X#",
        "#....#",
        "#....#",
        "##  ##",
    });
    room("Town Center", {
        "########",
        "#..GG..#",
        "#..GG..#",
        "#......#",
        "#......#",
        "##   ###",
    });
    room("Willow House", {
        "#####",
        "#B.T#",
        "#...#",
        "## ##",
    });
    room("Maple House", {
        "#####",
        "#B.T#",
        "#...#",
        "## ##",
    });
    room("Rowan Cottage", {
        "#####",
        "#B.T#",
        "#...#",
        "## ##",
    });
    room("Bus Stop", {
        "#####",
        "#N..#",
        "#...#",
        "## ##",
    });
    room("Railway Station", {
        "#########",
        "#T......#",
        "#.......#",
        "#..PPP..#",
        "#.......#",
        "##     ##",
    });
    room("Hawthorn Barn", {
        "#######",
        "#H.H.H#",
        "#.....#",
        "##   ##",
    });
    room("Glasshouse", {
        "#######",
        "#GG.GG#",
        "#.....#",
        "##   ##",
    });
}

int hour_of_day(const World& w) {
    int sec = int(w.day_seconds) % int(World::DAY_LENGTH_S);
    return 6 + sec * 24 / int(World::DAY_LENGTH_S);
}

const char* clock_str(const World& w) {
    int sec = int(w.day_seconds) % int(World::DAY_LENGTH_S);
    int h = 6 + sec * 24 / int(World::DAY_LENGTH_S);
    int m = (sec * 24 * 60 / int(World::DAY_LENGTH_S)) % 60;
    static char buf[32];
    const char* ap = h < 12 ? "AM" : "PM";
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    std::snprintf(buf, sizeof buf, "Day %u · %d:%02d %s", w.day, h12, m, ap);
    return buf;
}

int season_index(uint32_t day) { return (int)((day - 1) / 28) % 4; }
int season_day(uint32_t day)   { return (int)((day - 1) % 28) + 1; }

const char* season_name(int s) {
    static const char* names[4] = {"Spring", "Summer", "Fall", "Winter"};
    return names[s < 0 ? 0 : (s > 3 ? 3 : s)];
}

int weather_of_day(uint32_t day) {
    return (((day * 2654435761u) >> 16) % 5) == 0 ? 1 : 0;   // ~20% rainy days
}

const char* weather_of_day_name(uint32_t day) {
    return weather_of_day(day) ? "Rainy" : "Sunny";
}

int npc_at(const World& w, int x, int y) {
    for (size_t i = 0; i < w.npcs.size(); ++i)
        if (w.npcs[i].pos.x == x && w.npcs[i].pos.y == y) return (int)i;
    return -1;
}

const char* region_at(const World& w, int x, int y) {
    for (auto& b : w.buildings)
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
            return b.name.c_str();
    if (w.in_house(x, y)) return "Your Farmhouse";
    if (x >= 12 && x <= 20 && y >= 5 && y <= 12) return "Home Field";
    if (x >= MAP_W - 27 && x < MAP_W - 5 && y >= 9 && y <= 20) return "Ashgrove Farm";
    if (x >= 42 && x < 52 && y >= 14 && y < 20) return "Stardrop Plaza";
    if (y >= 10 && y < 12 && x >= 8 && x < MAP_W - 6) return "Mulberry Lane";
    if (x >= 33 && x <= 52 && y >= 38 && y <= 54) return "Mirror Lake";
    if (y >= MAP_H - 4) return "Seaglass Shore";
    if (w.in_bounds(x, y) && w.at(x, y).tile == Tile::Ice) return "Frozen Lake";
    {
        int line = int(12 + noise(x, 3, 0.22f) * 5.0f - 2.5f);
        if (y < line) return "Frostveil Tundra";
    }
    if (x >= 26 && x <= 34) return "Willow River";
    if (x >= 4 && x <= 20 && y >= 16 && y <= 46) return "Whisper Wood";
    if (y < 30 && ((x * 7 + y * 13) % 10) < 5) return "East Moor";
    return "Ashgrove Valley";
}

// ---- NPC patrols ----
void init_npcs(World& world) {
    world.npcs.clear();
    auto add_npc = [&](std::string name, uint8_t color, Vec2 a, Vec2 b) {
        NPC n;
        n.name = name;
        n.color = color;
        n.pos = a;
        n.way[0] = a;
        n.way[1] = b;
        n.next_move_ms = 0;
        world.npcs.push_back(n);
    };
    add_npc("Leah", 1, {46, 17}, {46, 17});          // Stardrop Plaza
    add_npc("Abigail", 2, {74, 20}, {74, 20});       // farm gate
    add_npc("Elliot", 3, {30, 10}, {31, 11});        // the river bridge
    add_npc("Robin", 4, {12, 11}, {14, 11});         // home field
    add_npc("Evelyn", 5, {77, 46}, {78, 45});        // mirror lake shore
}

static const char* npc_greeting(const char* name, int season) {
    struct Line { const char* who; const char* l[4]; };
    static const Line lines[] = {
        {"Leah", {"\"The plaza blooms every spring. A good season for painting.\"",
                  "\"Summer afternoons by the river are the best. The light is golden.\"",
                  "\"Autumn's here. Gather berries before the frost takes them.\"",
                  "\"Winter is quiet... but the snow on the rooftops is beautiful.\""}},
        {"Abigail", {"\"Careful with those seeds, farmer. Sprout 'em right or they'll sulk.\"",
                     "\"Hah, summer! Corn grows fast when the nights stay warm.\"",
                     "\"The harvest moon will be full soon. Best time to work late.\"",
                     "\"Frozen ground means hot soup. Stop by the stove sometime.\""}},
        {"Elliot", {"\"The river sings when the snows melt. Listen closely.\"",
                    "\"Catfish love warm water. Dusk is the hour to try.\"",
                    "\"I write by candlelight in fall. The stories come easier.\"",
                    "\"The river freezes at the edges now. Peaceful, isn't it?\""}},
        {"Robin", {"\"Mornings are for planting, evenings for tools. That's the rule.\"",
                   "\"Don't let the sun beat you down in summer. Water twice if you must.\"",
                   "\"I'll be patching roofs all fall. You gather the apples.\"",
                   "\"Winter prep: stock wood, mend fences, warm the bones.\""}},
        {"Evelyn", {"\"The lake steams at dawn in spring. Walk the shore with me sometime.\"",
                    "\"Listen to the shorebirds, dear. Summer won't last.\"",
                    "\"The reeds turn bronze in fall. Bring a basket; the berries are heavy.\"",
                    "\"When the lake freezes over, the whole world goes quiet.\""}},
    };
    for (auto& l : lines)
        if (std::string(l.who) == name) return l.l[season];
    return "\"Hello, farmer.\"";
}

const char* npc_line(const char* name, int season) {
    return npc_greeting(name, season);
}

// ---- seasonal forage/fish tables ----
struct ForageEntry { const char* name; int price; };
struct FishEntry { const char* name; int price; int min_hour, max_hour; };

std::string forage_table(int season, int& count) {
    static const ForageEntry spring[4] = {{"Dandelion",25},{"Wild Horseradish",40},{"Leek",60},{"Morel",90}};
    static const ForageEntry summer[4] = {{"Spice Berry",80},{"Grape",120},{"Sweet Pea",55},{"Red Mushroom",110}};
    static const ForageEntry fall[4]  = {{"Blackberry",60},{"Hazelnut",90},{"Wild Plum",70},{"Chanterelle",160}};
    static const ForageEntry winter[4]= {{"Snow Yam",100},{"Crystal Fruit",150},{"Crocus",200},{"Winter Root",90}};
    const ForageEntry* t = season == 0 ? spring : season == 1 ? summer : season == 2 ? fall : winter;
    count = 4;
    std::string s = t[0].name;
    for (int i = 1; i < 4; ++i) s += std::string(", ") + t[i].name;
    return s;
}

std::string fish_table(int season, int& count) {
    static const FishEntry spring[5] = {{"Anchovy",30,6,21},{"Sardine",40,6,19},{"Bream",55,6,20},{"Halibut",90,6,11},{"Salmon",100,6,21}};
    static const FishEntry summer[5] = {{"Tuna",80,6,19},{"Rainbow Trout",100,6,21},{"Sunfish",80,6,19},{"Catfish",100,12,2},{"Pufferfish",180,12,16}};
    static const FishEntry fall[5]   = {{"Walleye",120,12,2},{"Eel",145,16,2},{"Salmon",100,6,21},{"Midnight Carp",150,22,2},{"Angler",200,6,21}};
    static const FishEntry winter[5] = {{"Perch",90,6,21},{"Squid",100,18,2},{"Sturgeon",150,6,21},{"Ice Pip",200,6,21},{"Glacierfish",260,6,21}};
    const FishEntry* t = season == 0 ? spring : season == 1 ? summer : season == 2 ? fall : winter;
    count = 5;
    std::string s = t[0].name;
    for (int i = 1; i < 5; ++i) s += std::string(", ") + t[i].name;
    return s;
}

// ---- BFS pathfinding (A* with manhattan heuristic) ----
bool bfs_path(World& world, Vec2 from, Vec2 to, std::vector<Vec2>& out, size_t max_len) {
    if (!world.in_bounds(to) || !world.walkable(to)) return false;
    if (from == to) { out.clear(); return true; }
    static constexpr int8_t DX[4] = {1, -1, 0, 0};
    static constexpr int8_t DY[4] = {0, 0, 1, -1};
    std::vector<int16_t> prev(MAP_W * MAP_H, -1);
    std::deque<uint32_t> q;
    uint32_t start = from.y * MAP_W + from.x;
    uint32_t goal = to.y * MAP_W + to.x;
    prev[start] = start;
    q.push_back(start);
    while (!q.empty()) {
        uint32_t cur = q.front(); q.pop_front();
        if (cur == goal) break;
        int cx = cur % MAP_W, cy = cur / MAP_W;
        for (int i = 0; i < 4; ++i) {
            int nx = cx + DX[i], ny = cy + DY[i];
            if (!world.in_bounds(nx, ny) || !world.walkable(nx, ny)) continue;
            if (nx == to.x && ny == to.y) { prev[goal] = cur; cur = goal; q.clear(); break; }
            uint32_t ni = ny * MAP_W + nx;
            if (prev[ni] == -1) { prev[ni] = cur; q.push_back(ni); }
        }
    }
    if (prev[goal] == -1) return false;
    out.clear();
    for (uint32_t c = goal; c != start && out.size() < max_len; c = prev[c])
        out.push_back({int16_t(c % MAP_W), int16_t(c / MAP_W)});
    std::reverse(out.begin(), out.end());
    return !out.empty();
}

// ---- inventory helpers ----
void add_item(Player& p, Item item, uint16_t count) {
    for (auto& s : p.inv)
        if (s.item == item) { s.count += count; return; }
    for (auto& s : p.inv)
        if (s.item == Item::None) { s.item = item; s.count = count; return; }
}

bool consume_item(Player& p, Item item, uint16_t count) {
    for (auto& s : p.inv)
        if (s.item == item && s.count >= count) {
            s.count -= count;
            if (s.count == 0) s.item = Item::None;
            return true;
        }
    return false;
}

// ---- persistence ----
std::string serialize_world(const World& w) {
    json j;
    j["day"] = w.day;
    j["time"] = w.day_seconds;
    j["next_player_id"] = w.next_player_id;
    j["players"] = json::array();
    for (auto& [id, p] : w.players) {
        json pl{{"id", id}, {"x", p.pos.x}, {"y", p.pos.y}, {"dir", p.dir},
                {"energy", p.energy}, {"money", p.money}, {"sel", p.sel},
                {"name", p.name}};
        pl["inside"] = p.inside;
        pl["inx"] = p.inx; pl["iny"] = p.iny;
        pl["train_used"] = p.train_used;
        pl["exit_x"] = p.inside_exit.x; pl["exit_y"] = p.inside_exit.y;
        pl["inv"] = json::array();
        for (auto& s : p.inv)
            pl["inv"].push_back({{"item", static_cast<int>(s.item)}, {"count", s.count}});
        json h = json::object();
        for (auto& [nm, val] : p.hearts) h[nm] = val;
        pl["hearts"] = h;
        json gd = json::array();
        for (auto& n : p.gifted_today) gd.push_back(n);
        pl["gifted_today"] = gd;
        pl["max_energy"] = p.max_energy;
        j["players"].push_back(pl);
    }
    j["cells"] = json::array();
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            const Cell& c = w.at(x, y);
            if (c.obj.type == ObjType::None && !c.crop.is_crop() &&
                c.tile == Tile::Grass) continue;
            json cj{{"x", x}, {"y", y}, {"tile", static_cast<int>(c.tile)},
                    {"obj", static_cast<int>(c.obj.type)}, {"hp", c.obj.hp}, {"ore", c.obj.ore}};
            if (c.crop.is_crop()) {
                cj["crop"] = static_cast<int>(c.crop.crop);
                cj["stage"] = c.crop.stage;
                cj["days_left"] = c.crop.days_left;
                cj["watered"] = c.crop.watered;
            }
            j["cells"].push_back(cj);
        }
    return j.dump();
}

bool deserialize_world(World& w, const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        w.day = j.value("day", 1);
        w.day_seconds = j.value("time", 0.0f);
        w.next_player_id = j.value("next_player_id", 1);
        for (auto& pl : j.value("players", json::array())) {
            Player p;
            p.id = pl["id"]; p.pos = {pl["x"], pl["y"]}; p.target = p.pos;
            p.dir = pl.value("dir", 0);
            p.energy = pl.value("energy", 270.0f);
            p.money = pl.value("money", 500);
            p.sel = pl.value("sel", 0);
            p.name = pl.value("name", "Player");
            p.max_energy = pl.value("max_energy", 270);
            p.inside = pl.value("inside", "");
            p.inx = pl.value("inx", 0);
            p.iny = pl.value("iny", 0);
            p.train_used = pl.value("train_used", false);
            p.inside_exit = {(int16_t)pl.value("exit_x", 0), (int16_t)pl.value("exit_y", 0)};
            for (auto& hn : pl.value("hearts", json::object()).items())
                p.hearts[hn.key()] = hn.value().get<uint8_t>();
            for (auto& g : pl.value("gifted_today", json::array()))
                p.gifted_today.insert(g.get<std::string>());
            int i = 0;
            for (auto& s : pl.value("inv", json::array())) {
                if (i >= 12) break;
                p.inv[i].item = static_cast<Item>(s.value("item", 0));
                p.inv[i].count = s.value("count", 0);
                ++i;
            }
            w.players[p.id] = p;
        }
        for (auto& cj : j.value("cells", json::array())) {
            int x = cj.value("x", -1), y = cj.value("y", -1);
            if (!w.in_bounds(x, y)) continue;
            Cell& c = w.at(x, y);
            c.tile = static_cast<Tile>(cj.value("tile", static_cast<int>(c.tile)));
            c.obj.type = static_cast<ObjType>(cj.value("obj", 0));
            c.obj.hp = cj.value("hp", 1);
            c.obj.ore = cj.value("ore", 0);
            if (cj.contains("crop")) {
                c.crop.crop = static_cast<Item>(cj["crop"]);
                c.crop.stage = cj.value("stage", 0);
                c.crop.days_left = cj.value("days_left", 0);
                c.crop.watered = cj.value("watered", false);
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "save load failed: " << e.what() << "\n";
        return false;
    }
}