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

static int mountain_top(int x) {
    return int(3 + noise(x, 1, 0.18f) * 5.0f);
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

    // ---- mountains along the north edge (valley bowl) ----
    // Rough ridge: a jagged rocky band above y~8, with a wavy treeline at y~12
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            if (y < mountain_top(x))
                world.at(x, y).tile = Tile::Ice;   // snow-capped ridge
            else if (y < mountain_top(x) + 4)
                world.at(x, y).tile = Tile::Snow;  // rocky talus below ridge
        }

    // ---- snow biome in the far north (wavy treeline, clear around the house) ----
    auto snow_line = [&](int x) {
        return int(13 + noise(x, 3, 0.22f) * 5.0f - 2.5f);
    };
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            if (y >= snow_line(x)) continue;
            if (x >= world.house_tl.x - 3 && x < world.house_tl.x + 7 &&
                y <= world.house_tl.y + 6) continue;          // keep the homestead green
            if (world.at(x, y).tile == Tile::Grass || world.at(x, y).tile == Tile::GrassVar)
                world.at(x, y).tile = Tile::Snow;
        }

    // ---- frozen glacier lake up in the tundra (NW) ----
    float fl_cx = 30.0f + noise(0, 9, 0.5f) * 4.0f, fl_cy = 3.5f;
    {
        float lcx = fl_cx, lcy = fl_cy;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < MAP_W; ++x) {
                float d = std::hypot((x - lcx) * 1.35f, (y - lcy));
                if (d < 3.2f + 1.2f * std::sin(x * 0.6f) * std::cos(y * 0.5f) + 1.4f * noise(x, y, 0.4f))
                    world.at(x, y).tile = Tile::Ice;
            }
    }

    // ---- main river: meanders N-S around x~44 (center of 128-wide map), bounded ----
    // Below the south footbridge the river swings east so the farm (west bank)
    // and the docks (east of the river mouth) stay separate.
    std::array<float, MAP_H> river_cx{};
    for (int y = 0; y < MAP_H; ++y) {
        float drift = y > 55 ? (y - 55) / 41.0f * 15.0f : 0.0f;
        float cy = MAP_W * 0.34f + std::sin(y * 0.09f) * 3.2f + std::sin(y * 0.21f + 1.5f) * 1.8f + drift;
        river_cx[y] = cy;
        float w_variation = 2.4f + 1.0f * std::sin(y * 0.07f);
        int half_w = std::max(1, int(w_variation));
        for (int dx = -half_w; dx <= half_w; ++dx) {
            int ix = int(std::round(cy)) + dx;
            if (world.in_bounds(ix, y)) world.at(ix, y).tile = Tile::Water;
        }
    }
    // three bridge crossings: north y~20, main y~34, south footbridge y~56
    // (bridges are placed at the river's exact position that row)
    {
        const int rows[3] = {20, 34, 56};
        const int widths[3] = {2, 2, 1};
        for (int b = 0; b < 3; ++b) {
            int by = rows[b];
            int bx = int(std::round(river_cx[by]));
            for (int dx = -widths[b]; dx <= widths[b]; ++dx) {
                int ix = bx + dx;
                if (world.in_bounds(ix, by)) world.at(ix, by).tile = Tile::Bridge;
            }
        }
    }

    // ---- tributary stream: tumbles out of the NW, joins the river at y~22 ----
    {
        float sx = 20.0f;
        for (int y = 10; y < 30; ++y) {
            float progress = float(y - 10) / 20.0f;
            sx += std::sin(y * 0.12f) * 0.8f + std::cos(y * 0.25f) * 0.5f;
            sx += (river_cx[y] - sx) * (0.03f + 0.06f * progress);  // drift toward river
            int half_w = 1 + (progress > 0.6f ? 1 : 0);  // widens near junction
            for (int dx = -half_w; dx <= 0; ++dx) {
                int ix = int(std::round(sx)) + dx;
                if (world.in_bounds(ix, y)) world.at(ix, y).tile = Tile::Water;
            }
        }
    }

    // ---- Lake Aurora: big round mountain lake in the NE ----
    {
        float mcx = 96.0f, mcy = 16.0f;
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
    auto croad = [&](int x, int y) {
        if (!world.in_bounds(x, y) || is_water(world.at(x, y).tile)) return;
        world.at(x, y).tile = Tile::Cobble;
    };
    // main E-W high street on each bank (gently curving), broken at the river — COBBLESTONE (main loop)
    for (int x = 8; x < 42; ++x) {
        int y = 18 + int(std::round(std::sin(x * 0.11f) * 1.5f));
        croad(x, y); croad(x, y + 1);
    }
    for (int x = 48; x < MAP_W - 6; ++x) {
        int y = 18 + int(std::round(std::sin(x * 0.11f) * 1.5f));
        croad(x, y); croad(x, y + 1);
    }
    // N-S lane east bank linking high street to the farm gate
    for (int y = 14; y < 70; ++y) { road(60, y); road(61, y); }
    // farm drive: high street -> farm gate row
    for (int x = 54; x < 76; ++x) road(x, 24);
    // town square (civic plaza footprint, west bank) — refined in R2
    for (int y = 26; y < 32; ++y)
        for (int x = 18; x < 32; ++x) road(x, y);

    // ---- cobblestone roundabout at plaza center (Stardrop Plaza) ----
    // 3x3 grass circle at center (24,28), cobblestone ring around it, statue in middle
    const int rx = 24, ry = 28;
    // grass center (3x3)
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (world.in_bounds(rx + dx, ry + dy))
                world.at(rx + dx, ry + dy).tile = Tile::Grass;
    // cobblestone ring (5x5 minus 3x3 center)
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            if (world.in_bounds(rx + dx, ry + dy) &&
                (dx < -1 || dx > 1 || dy < -1 || dy > 1))
                world.at(rx + dx, ry + dy).tile = Tile::Cobble;
    // statue at center
    if (world.in_bounds(rx, ry))
        world.at(rx, ry).obj = {ObjType::Statue, 255};
    // cobblestone roads radiating 4 directions from ring
    // north to high street (y=22)
    for (int y = 22; y < ry - 2; ++y) {
        world.at(rx, y).tile = Tile::Cobble;
        world.at(rx + 1, y).tile = Tile::Cobble;
    }
    // south to main-street bridge (y=34)
    for (int y = ry + 3; y <= 34; ++y) {
        world.at(rx, y).tile = Tile::Cobble;
        world.at(rx + 1, y).tile = Tile::Cobble;
    }
    // east toward river / commerce district
    for (int x = rx + 3; x < 44; ++x) {
        world.at(x, ry).tile = Tile::Cobble;
        world.at(x, ry + 1).tile = Tile::Cobble;
    }
    // west toward bus stop
    for (int x = 8; x < rx - 2; ++x) {
        world.at(x, ry).tile = Tile::Cobble;
        world.at(x, ry + 1).tile = Tile::Cobble;
    }

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
    // high street (both rows of the lane), west bank segment
    bridge_run(34, [](int i) { return i + 8; },
               [](int i) { return 18 + int(std::round(std::sin((i + 8) * 0.11f) * 1.5f)); });
    bridge_run(34, [](int i) { return i + 8; },
               [](int i) { return 18 + int(std::round(std::sin((i + 8) * 0.11f) * 1.5f)) + 1; });
    // high street (both rows of the lane), east bank segment
    bridge_run(MAP_W - 54, [](int i) { return i + 48; },
               [](int i) { return 18 + int(std::round(std::sin((i + 48) * 0.11f) * 1.5f)); });
    bridge_run(MAP_W - 54, [](int i) { return i + 48; },
               [](int i) { return 18 + int(std::round(std::sin((i + 48) * 0.11f) * 1.5f)) + 1; });
    // N-S lane down to the farm gate
    bridge_run(56, [](int) { return 60; }, [](int i) { return i + 14; });
    bridge_run(56, [](int) { return 61; }, [](int i) { return i + 14; });

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
        return x >= 24 && x < 44 && y >= 70 && y < 86;
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
    scatter(90, ObjType::Tree, 3);
    scatter(45, ObjType::Rock, 2);
    scatter(70, ObjType::Weed, 1);
    scatter(35, ObjType::TallGrass, 1);
    scatter(50, ObjType::Flower, 1);
    scatter(60, ObjType::Bush, 1);
    scatter(22, ObjType::Mushroom, 1);

    // ---- Whisper Wood: dense old-growth forest west of the stream ----
    for (int y = 16; y < 74; ++y)
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
    // horizontal trail at y=30 and y=46, vertical trail at x=12
    for (int x = 4; x < 21; ++x) {
        if (world.in_bounds(x, 30)) world.at(x, 30).obj = FarmObj{};
        if (world.in_bounds(x, 46)) world.at(x, 46).obj = FarmObj{};
    }
    for (int y = 16; y < 74; ++y) {
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
    for (int y = 3; y < 16; ++y) {
        if (world.in_bounds(42, y)) world.at(42, y).obj = FarmObj{};
        if (world.in_bounds(43, y)) world.at(43, y).obj = FarmObj{};
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
        int y = 20 + int(std::round(std::sin(x * 0.11f) * 1.5f));
        Cell& c = world.at(x, y);
        if (is_water(c.tile) || c.obj.type != ObjType::None) continue;
        c.obj = {ObjType::Tree, 3};
    }
    // town square flower beds
    for (int y = 27; y < 31; ++y)
        for (int x = 21; x < 29; ++x)
            if ((x + y) % 3 == 0)
                world.at(x, y).obj = {ObjType::Flower, 1};
    // lake aurora shore: reeds + rocks ring
    for (int y = 10; y < 24; ++y)
        for (int x = 86; x < 106; ++x) {
            Cell& c = world.at(x, y);
            if (c.tile != Tile::Grass && c.tile != Tile::GrassVar && c.tile != Tile::Sand) continue;
            float ring = std::hypot((x - 96.0f) * 1.45f, y - 16.0f);
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
                c.obj = {ObjType::Rock, static_cast<uint8_t>(hp), tier};
            }
        }

    // ---- farm plot (south outskirts, overgrown; player tills themselves) ----
    // The farm is a grassy field west of the river with a few weeds/rocks for character.
    for (int y = 70; y < 86; ++y)
        for (int x = 24; x < 44; ++x) {
            if (!world.in_bounds(x, y)) continue;
            Cell& c = world.at(x, y);
            if (c.tile == Tile::Dirt || is_water(c.tile)) continue;
            float d = dist(rng);
            if (d < 0.06f) c.obj = {ObjType::Weed, 1};
            else if (d < 0.10f) c.obj = {ObjType::Rock, 2};
        }

    // fence ring around farm plot (gap at the farm drive gate, south side)
    for (int x = 24; x < 44; ++x) {
        for (auto y : {70, 85}) {
            if (y == 85 && x >= 38 && x <= 41) continue;  // gate
            if (world.at(x, y).obj.type == ObjType::None)
                world.at(x, y).obj = {ObjType::FencePost, 255};
        }
    }
    for (int y = 71; y < 85; ++y) {
        for (auto x : {24, 43}) {
            if (world.at(x, y).obj.type == ObjType::None)
                world.at(x, y).obj = {ObjType::FencePost, 255};
        }
    }
    // farm lane: south footbridge -> farm gate (west bank, beside the river)
    for (int y = 56; y < 86; ++y) {
        Cell& c = world.at(40, y);
        if (is_water(c.tile)) continue;
        if (c.obj.type == ObjType::Building) continue;
        c.tile = Tile::Dirt;
        c.obj = FarmObj{};
    }
    // east-bank approach: N-S lane across to the footbridge at y=56
    for (int x = 44; x <= 60; ++x) {
        Cell& c = world.at(x, 56);
        if (is_water(c.tile)) continue;
        c.tile = Tile::Dirt;
        c.obj = FarmObj{};
    }

    place_buildings(world);
    init_interiors(world);
    // Initialize building states for all placed buildings (default condition = 100)
    for (auto& b : world.buildings) {
        world.building_states[b.name] = {100, 0, 100, 0};
    }
    init_plots(world);
    resolve_water_edges(world);

    // ---- docks boardwalk (south ocean shore, east of river mouth) ----
    // River mouth at south edge is around x≈58-60. Boardwalk runs east from there.
    for (int x = 62; x <= 120; ++x) {
        for (int y = 90; y <= 92; ++y) {
            if (!world.in_bounds(x, y)) continue;
            Cell& c = world.at(x, y);
            if (is_water(c.tile)) {
                // wooden planks over water (Bridge tile renders as wooden bridge)
                c.tile = Tile::Bridge;
            } else if (c.tile == Tile::Grass || c.tile == Tile::GrassVar || c.tile == Tile::Sand) {
                // wooden planks on land
                c.tile = Tile::Bridge;
                c.obj = FarmObj{};
            }
        }
    }
    // short pier extending south from boardwalk toward ocean
    for (int y = 90; y < MAP_H - 1; ++y) {
        int x = 100;
        if (!world.in_bounds(x, y)) continue;
        Cell& c = world.at(x, y);
        if (is_water(c.tile) || c.tile == Tile::Grass || c.tile == Tile::GrassVar || c.tile == Tile::Sand) {
            c.tile = Tile::Bridge;
            c.obj = FarmObj{};
        }
    }

    // ---- lakefront pier at Lake Aurora (south shore, near Tearoom) ----
    // Lake Aurora center (96, 16), Tearoom at (90, 22). Pier extends north from shore into lake.
    for (int y = 18; y >= 12; --y) {
        int x = 90;
        if (!world.in_bounds(x, y)) continue;
        Cell& c = world.at(x, y);
        if (is_water(c.tile) || c.tile == Tile::Grass || c.tile == Tile::GrassVar || c.tile == Tile::Sand) {
            c.tile = Tile::Bridge;
            c.obj = FarmObj{};
        }
    }
    // small T-shape at end of pier
    for (int dx = -1; dx <= 1; ++dx) {
        int x = 90 + dx, y = 13;
        if (!world.in_bounds(x, y)) continue;
        Cell& c = world.at(x, y);
        if (is_water(c.tile)) {
            c.tile = Tile::Bridge;
            c.obj = FarmObj{};
        }
    }

    // cart-track west from the village street to the bus stop
    for (int x = 10; x <= 32; ++x) {
        Cell& c = world.at(x, 16);
        if (is_water(c.tile) || c.obj.type == ObjType::Building) continue;
        c.tile = Tile::Dirt;
        c.obj = FarmObj{};
    }
    // station approach: lane along the station's south edge from the boardwalk
    for (int y = 16; y <= 16; ++y)
        for (int x = 96; x <= 116; ++x) {
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

    clear_paths(world);
}

// The scatter/ore passes run late and can drop rocks, stumps and fence posts on
// bridge tiles, building doors and the land right around them — which silently
// breaks crossings and NPC schedule paths. Clear blocking objects there after
// generation (and again after loading a save, since saves predate this fix).
void clear_paths(World& world) {
    auto blocking = [](ObjType o) {
        return o == ObjType::Tree || o == ObjType::Rock || o == ObjType::Stump ||
               o == ObjType::FencePost || o == ObjType::FenceRail ||
               o == ObjType::Pine;
    };
    auto clear_cell = [&](int x, int y) {
        if (!world.in_bounds(x, y)) return;
        Cell& c = world.at(x, y);
        if (c.obj.type == ObjType::Building || c.obj.type == ObjType::Sprinkler)
            return;
        if (c.crop.is_crop()) return;   // never erase the player's crops
        if (blocking(c.obj.type)) c.obj = FarmObj{};
    };
    // every bridge tile must stay open, and so must the land beside it
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            if (world.at(x, y).tile == Tile::Bridge) {
                clear_cell(x, y);
                clear_cell(x - 1, y); clear_cell(x + 1, y);
                clear_cell(x, y - 1); clear_cell(x, y + 1);
            }
    // keep the doorstep and the 3x3 around every door open for NPC arrivals
    for (auto& b : world.buildings) {
        int dx = b.x + (b.w - 1) / 2, dy = b.y + b.h;
        for (int yy = dy - 1; yy <= dy + 1; ++yy)
            for (int xx = dx - 1; xx <= dx + 1; ++xx)
                clear_cell(xx, yy);
    }
    // keep the farmhouse doorstep clear too (the farmhouse isn't in the building
    // table, so scatter/ore passes can leave a rock right on its door tile)
    {
        Vec2 d = world.door();
        for (int yy = d.y - 1; yy <= d.y + 1; ++yy)
            for (int xx = d.x - 1; xx <= d.x + 1; ++xx)
                clear_cell(xx, yy);
    }
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
        // civic plaza (west bank, north of center)
        {"Town Center", 20, 24, 4, 3},
        {"Clinic", 16, 16, 2, 2},
        {"Museum", 26, 16, 2, 2},
        {"Old Mill", 12, 22, 2, 2},
        {"Stardrop Saloon", 28, 22, 3, 3},
        // commerce row (east bank, north of center)
        {"Blacksmith", 50, 16, 2, 2},
        {"General Store", 54, 16, 2, 2},
        {"Market", 58, 18, 4, 2},
        {"Carpenter Shop", 50, 22, 2, 2},
        {"Pet Shop", 54, 22, 2, 2},
        // residential: Birch Court (west)
        {"Willow House", 20, 38, 2, 2},
        {"Maple House", 26, 38, 2, 2},
        // residential: Maple Court (east)
        {"Rowan Cottage", 50, 38, 2, 2},
        {"Hawthorne Cottage", 56, 38, 2, 2},
        // travel
        {"Bus Stop", 8, 14, 2, 2},
        {"Railway Station", 108, 10, 9, 6},
        // farm outbuildings (on the farm plot, clear of the farmhouse + doorstep)
        {"Hawthorn Barn", 30, 80, 3, 3},
        {"Glasshouse", 34, 80, 2, 2},
        // lakefront district (NE)
        {"Tearoom", 90, 22, 2, 2},
        {"Observatory", 110, 4, 2, 2},
        // docks district (south)
        {"Fish Shack", 96, 82, 2, 2},
        {"Lighthouse", 122, 88, 2, 2},
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
    for (int y = 16; y <= 17; ++y)
        for (int x = 96; x < 108; ++x) {
            if (!world.in_bounds(x, y)) continue;
            Cell& c = world.at(x, y);
            if (is_water(c.tile)) continue;
            c.tile = Tile::Dirt;
            c.obj = FarmObj{};
        }
}

// ---- buyable plots (R16) ----
void init_plots(World& world) {
    world.plots = {
        {"Hillside",       104, 28, 6, 6,  15000, "cool mountain air"},
        {"Forest Clearing",  4, 46, 6, 6,   8000, "temperate woodland"},
        {"Lakeside",        92, 30, 6, 6,  12000, "humid lake breeze"},
        {"Docks Lot",      100, 76, 6, 6,  10000, "salty coastal wind"},
    };
    // Clear the plot areas: flatten to grass, remove obstacles
    for (auto& p : world.plots)
        for (int y = p.y; y < p.y + p.h; ++y)
            for (int x = p.x; x < p.x + p.w; ++x) {
                if (!world.in_bounds(x, y)) continue;
                Cell& c = world.at(x, y);
                c.tile = Tile::Grass;
                c.obj = FarmObj{};
                c.crop = Crop{};
            }
}

// interior furniture: '#' wall, '.' floor, letters = furnishings, ' ' = doorway.
// everything except '.', ' ' and 'P' blocks movement. Stepping down from the
// last walkable row exits the building. Door is always bottom-centre.
// '>' = stairs up, '<' = stairs down (on upper floors).
// Furniture letters for interact:
//   B=Bed(sleep) V=TV(forecast) T=Table(eat/social) S=Shelf(browse) C=Counter(shop)
//   F=Fridge/Stove/Forge/Anvil G=Governor/Growing H=Hay I=Item rack K=Kettle
//   L=Lamp/Loom M=Market/Millstone N=Notice O=Observatory P=Pet/Platform
//   R=Register U=Upgrade W=Workbench X=Exit/Stairs Y=Library Z=Zen garden
void init_interiors(World& world) {
    auto room = [&](const char* building, std::vector<std::string> ground, std::vector<std::vector<std::string>> upper = {}) {
        InteriorRoom r;
        r.building = building;
        r.rows = ground;
        r.floors = upper;
        r.h = (int16_t)ground.size();
        r.w = (int16_t)(r.h ? ground[0].size() : 0);
        world.interiors[building] = r;
    };

    // ============================================================
    // CIVIC PLAZA DISTRICT (West Bank)
    // ============================================================

    // Town Center (4x3) - 2 floors: civic hall + archive
    room("Town Center", {
        "##########",
        "#..GGGG..#",  // Governor's desk area
        "#..GGGG..#",
        "#........#",
        "#..LL..LL#",  // Lamps
        "#........#",
        "##    ##",    // doorway
    }, {
        {   // Floor 1 - Archive/Records
            "##########",
            "#YYYYYYYY#",  // Library shelves
            "#YYYYYYYY#",
            "#........#",
            "#..<>....#",  // Stairs down
            "#........#",
            "##    ##",
        }
    });

    // Clinic (2x2) - Medical care
    room("Clinic", {
        "########",
        "#DDDDDD.#",  // Doctor desk + cabinets
        "#.......#",
        "#.BB..BB#",  // Patient beds
        "#.......#",
        "#.MM..MM#",  // Medicine cabinets
        "##   ##",
    });

    // Museum (2x2) - 2 floors: exhibits + curator office
    room("Museum", {
        "##########",
        "#EE.EE.EE#",  // Exhibits
        "#........#",
        "#..E...E.#",
        "#........#",
        "#..<>....#",  // Stairs up
        "##    ##",
    }, {
        {   // Floor 1 - Curator office + rare items
            "##########",
            "#Y.Y.Y.Y.#",  // Rare book shelves
            "#........#",
            "#..D......#",  // Curator desk
            "#........#",
            "#..<>....#",  // Stairs down
            "##    ##",
        }
    });

    // Old Mill (2x2) - Working mill
    room("Old Mill", {
        "########",
        "#X....X.#",  // Millstones
        "#........#",
        "#..M...M.#",  // Grain hoppers
        "#........#",
        "#..S...S.#",  // Sacks of flour
        "##   ##",
    });

    // Stardrop Saloon (3x3) - 3 floors: tavern + guest rooms + owner's quarters
    room("Stardrop Saloon", {
        "############",
        "#BBBB.BBBB.#",  // Bar counter (B=bar)
        "#..........#",
        "#..TT.TT..#",  // Tables (T=table)
        "#..........#",
        "#..PP.PP..#",  // Private booths (P=booth)
        "#..........#",
        "#..KK..KK.#",  // Kitchen (K=kitchen)
        "#..SS..SS.#",  // Stage (S=stage)
        "#..<>......#",  // Stairs up
        "##      ##",
    }, {
        {   // Floor 1 - Guest rooms
            "############",
            "#BB....BB..#",  // Guest beds
            "#..........#",
            "#..BB....BB#",
            "#..........#",
            "#..BB....BB#",
            "#..........#",
            "#..BB....BB#",
            "#..........#",
            "#..<>......#",  // Stairs up
            "##      ##",
        },
        {   // Floor 2 - Owner's quarters (Gus) + attic storage
            "############",
            "#B..V..S...#",  // Gus's bed, TV, shelf
            "#..........#",
            "#..KK..KK..#",  // Small kitchenette
            "#..........#",
            "#..DD..DD..#",  // Desk + dresser
            "#..........#",
            "#..AA..AA..#",  // Attic storage (A=archive)
            "#..........#",
            "#..<>......#",  // Stairs down
            "##      ##",
        }
    });

    // ============================================================
    // COMMERCE ROW (East Bank)
    // ============================================================

    // Blacksmith (2x2) - Forge + shop
    room("Blacksmith", {
        "########",
        "#A.FFF..#",  // Anvil + Forge
        "#........#",
        "#..AA..A.#",  // Tool racks
        "#........#",
        "#..CC..C.#",  // Coal bin
        "##   ##",
    });

    // General Store (2x2) - Full general store
    room("General Store", {
        "########",
        "#CCCCCC.#",  // Counter
        "#........#",
        "#..SS..S.#",  // Shelves
        "#........#",
        "#..SS..S.#",
        "##   ##",
    });

    // Market (4x2) - Open market hall
    room("Market", {
        "############",
        "#MMMMMMMMMM#",  // Market stalls
        "#..........#",
        "#..MM..MM..#",
        "#..........#",
        "#..MM..MM..#",
        "##      ##",
    });

    // Carpenter Shop (2x2) - Workshop + blueprints
    room("Carpenter Shop", {
        "########",
        "#WWWWWW.#",  // Workbenches
        "#........#",
        "#..UU..U.#",  // Upgrade blueprints
        "#........#",
        "#..LL..L.#",  // Lumber storage
        "##   ##",
    });

    // Pet Shop (2x2) - Adoption center
    room("Pet Shop", {
        "########",
        "#PP.PP.P.#",  // Pet beds
        "#........#",
        "#..CC..C.#",  // Counter
        "#........#",
        "#..FF..F.#",  // Food bowls
        "##   ##",
    });

    // ============================================================
    // RESIDENTIAL: BIRCH COURT (West)
    // ============================================================

    // Willow House (2x2) - Cozy cottage
    room("Willow House", {
        "########",
        "#B..T...#",  // Bed + TV
        "#........#",
        "#..SS..S.#",  // Shelf
        "#........#",
        "#..KK..K.#",  // Kitchenette
        "##   ##",
    });

    // Maple House (2x2)
    room("Maple House", {
        "########",
        "#B..T...#",
        "#........#",
        "#..SS..S.#",
        "#........#",
        "#..KK..K.#",
        "##   ##",
    });

    // ============================================================
    // RESIDENTIAL: MAPLE COURT (East)
    // ============================================================

    // Rowan Cottage (2x2)
    room("Rowan Cottage", {
        "########",
        "#B..T...#",
        "#........#",
        "#..SS..S.#",
        "#........#",
        "#..KK..K.#",
        "##   ##",
    });

    // Hawthorne Cottage (2x2)
    room("Hawthorne Cottage", {
        "########",
        "#B..T...#",
        "#........#",
        "#..SS..S.#",
        "#........#",
        "#..KK..K.#",
        "##   ##",
    });

    // ============================================================
    // TRAVEL
    // ============================================================

    // Bus Stop (2x2) - Simple shelter
    room("Bus Stop", {
        "########",
        "#N......#",  // Notice board
        "#........#",
        "#..BB....#",  // Benches
        "#........#",
        "#........#",
        "##   ##",
    });

    // Railway Station (9x6) - 2 floors: platform + ticket hall
    room("Railway Station", {
        "################",
        "#TTTTTTTTTTTTTTT#",  // Ticket counters
        "#..............#",
        "#..PPPPPPPPPP..#",  // Platform benches
        "#..............#",
        "#..LL..LL..LL..#",  // Lamps
        "#..............#",
        "#..<>..........#",  // Stairs up
        "##            ##",
    }, {
        {   // Floor 1 - Waiting hall + conductor office
            "################",
            "#YYYYYYYYYYYYYY#",  // Waiting area chairs
            "#..............#",
            "#..DD..........#",  // Conductor desk
            "#..............#",
            "#..............#",
            "#..............#",
            "#..<>..........#",  // Stairs down
            "##            ##",
        }
    });

    // ============================================================
    // FARM OUTBUILDINGS
    // ============================================================

    // Hawthorn Barn (3x3) - Animal barn
    room("Hawthorn Barn", {
        "##########",
        "#HHHHHHHHH#",  // Hay bins
        "#..........#",
        "#..AA..AA..#",  // Animal stalls
        "#..........#",
        "#..WW..WW..#",  // Water troughs
        "#..........#",
        "#..FF..FF..#",  // Feed bins
        "##      ##",
    });

    // Glasshouse (2x2) - Year-round growing
    room("Glasshouse", {
        "########",
        "#GGGGGGG.#",  // Growing tables
        "#........#",
        "#..GG..GG.#",
        "#........#",
        "#..WW..W.#",  // Water barrels
        "##   ##",
    });

    // ============================================================
    // LAKEFRONT DISTRICT (NE)
    // ============================================================

    // Tearoom (2x2) - 2 floors: tea room + meditation
    room("Tearoom", {
        "########",
        "#TTTTTTT.#",  // Tea tables
        "#........#",
        "#..KK..K.#",  // Kettle station
        "#........#",
        "#..<>.....#",  // Stairs up
        "##   ##",
    }, {
        {   // Floor 1 - Meditation/reading room
            "########",
            "#ZZZZZZZ.#",  // Zen cushions
            "#........#",
            "#..YY..Y.#",  // Book shelves
            "#........#",
            "#..<>.....#",  // Stairs down
            "##   ##",
        }
    });

    // Observatory (2x2) - 3 floors: telescope + library + roof
    room("Observatory", {
        "########",
        "#OOOOOOO.#",  // Main telescope
        "#........#",
        "#..SS..S.#",  // Star charts
        "#........#",
        "#..<>.....#",  // Stairs up
        "##   ##",
    }, {
        {   // Floor 1 - Library
            "########",
            "#YYYYYYYY.#",
            "#........#",
            "#..YY..Y.#",
            "#........#",
            "#..<>.....#",  // Stairs up
            "##   ##",
        },
        {   // Floor 2 - Roof observatory
            "########",
            "#........#",  // Open roof
            "#..OO......#",  // Portable telescope
            "#........#",
            "#........#",
            "#..<>.....#",  // Stairs down
            "##   ##",
        }
    });

    // ============================================================
    // DOCKS DISTRICT (South)
    // ============================================================

    // Fish Shack (2x2) - Processing + shop
    room("Fish Shack", {
        "########",
        "#FFFFFFFF#",  // Fillet tables
        "#........#",
        "#..SS..S.#",  // Smoker racks
        "#........#",
        "#..CC..C.#",  // Counter
        "##   ##",
    });

    // Lighthouse (2x2) - 4 floors: keeper quarters + lamp room
    room("Lighthouse", {
        "########",
        "#LLLLLLLL#",  // Lamp base machinery
        "#........#",
        "#..CC..C.#",  // Coal storage
        "#........#",
        "#..<>.....#",  // Stairs up
        "##   ##",
    }, {
        {   // Floor 1 - Keeper quarters
            "########",
            "#B..T..S.#",  // Bed, TV, Shelf
            "#........#",
            "#..KK..K.#",  // Kitchenette
            "#........#",
            "#..<>.....#",  // Stairs up
            "##   ##",
        },
        {   // Floor 2 - Lens room
            "########",
            "#LLLLLLLL#",  // Fresnel lens
            "#........#",
            "#..TT.....#",  // Tool table
            "#........#",
            "#..<>.....#",  // Stairs up
            "##   ##",
        },
        {   // Floor 3 - Lantern room (top)
            "########",
            "#........#",  // Open balcony
            "#..LL.....#",  // Lantern
            "#........#",
            "#........#",
            "#..<>.....#",  // Stairs down
            "##   ##",
        }
    });

    // Farmhouse (already exists, enhanced)
    room("Farmhouse", {
        "#######",
        "#B.V..#",
        "#.....#",
        "#.T...#",
        "#.G...#",
        "#..S.C#",
        "#..F.X#",
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

int season_index(uint32_t day) { return static_cast<int>((day - 1) / 28) % 4; }
int season_day(uint32_t day)   { return static_cast<int>((day - 1) % 28) + 1; }

const char* season_name(int s) {
    static const char* names[4] = {"Spring", "Summer", "Fall", "Winter"};
    return names[s < 0 ? 0 : (s > 3 ? 3 : s)];
}

int weather_of_day(uint32_t day) {
    int season = season_index(day);
    // ~20% rainy days, plus autumn foggy mornings
    if (season == 2) { // Fall
        int r = ((day * 2654435761u) >> 16) % 10;
        if (r < 2) return 2; // 20% foggy
        if (r < 4) return 1; // 20% rainy
        return 0;
    }
    return (((day * 2654435761u) >> 16) % 5) == 0 ? 1 : 0;   // ~20% rainy days
}

const char* weather_of_day_name(uint32_t day) {
    int w = weather_of_day(day);
    if (w == 2) return "Foggy";
    return w ? "Rainy" : "Sunny";
}

int npc_at(const World& w, int x, int y) {
    for (size_t i = 0; i < w.npcs.size(); ++i)
        if (w.npcs[i].pos.x == x && w.npcs[i].pos.y == y) return static_cast<int>(i);
    return -1;
}

const char* region_at(const World& w, int x, int y) {
    for (auto& b : w.buildings)
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
            return b.name.c_str();
    if (w.in_house(x, y)) return "Your Farmhouse";
    if (x >= 24 && x <= 43 && y >= 70 && y <= 85) return "Ashgrove Farm";
    if (x >= 18 && x <= 32 && y >= 14 && y <= 32) return "Stardrop Plaza";
    if (x >= 18 && x <= 32 && y >= 36 && y <= 46) return "Birch Court";
    if (x >= 48 && x <= 62 && y >= 36 && y <= 46) return "Maple Court";
    if (x >= 48 && x <= 62 && y >= 14 && y <= 28) return "Commerce Row";
    if (y >= 10 && y < 12 && x >= 8 && x < MAP_W - 6) return "Mulberry Lane";
    if (y >= 16 && y <= 20 && x >= 8 && x < MAP_W - 6) return "Mulberry Lane";
    if (x >= 86 && x <= 106 && y >= 6 && y <= 26) return "Lake Aurora";
    if (x >= 56 && x <= 80 && y >= 80 && y <= 92) return "Seaglass Docks";
    if (y >= MAP_H - 4) return "Seaglass Shore";
    if (w.in_bounds(x, y) && w.at(x, y).tile == Tile::Ice) return "Frozen Lake";
    {
        int line = int(13 + noise(x, 3, 0.22f) * 5.0f - 2.5f);
        if (y < mountain_top(x)) return "Frostveil Peaks";
        if (y < line) return "Frostveil Tundra";
    }
    if (x >= 40 && x <= 46 && y >= 18 && y <= 30 && is_water(w.at(x, y).tile)) return "Willow River";
    if (x >= 4 && x <= 20 && y >= 16 && y <= 74) return "Whisper Wood";
    if (x >= 82 && x <= 122 && y >= 20 && y <= 70) return "East Moor";
    return "Ashgrove Valley";
}

// ---- NPC patrols ----
void init_npcs(World& world) {
    world.npcs.clear();
    auto add_npc = [&](std::string name, std::string kind, uint8_t color, Vec2 a, Vec2 b) {
        NPC n;
        n.name = name;
        n.kind = kind;
        n.color = color;
        n.pos = a;
        n.way[0] = a;
        n.way[1] = b;
        n.next_move_ms = 0;
        world.npcs.push_back(n);
    };
    add_npc("Leah", "villager", 1, {21, 40}, {21, 40});          // Willow House door (Birch Court)
    add_npc("Abigail", "villager", 2, {27, 40}, {27, 40});        // Maple House door (Birch Court)
    add_npc("Elliot", "villager", 3, {51, 40}, {51, 40});         // Rowan Cottage door (Maple Court)
    add_npc("Robin", "villager", 4, {51, 24}, {51, 24});          // Carpenter Shop (commerce row)
    add_npc("Evelyn", "villager", 5, {94, 24}, {95, 23});         // Tearoom shore (Lake Aurora)
    // R9.2: Rabbits near the farm (wildlife, kind="rabbit")
    add_npc("Cottontail", "rabbit", 2, {35, 75}, {38, 75});       // Near farm, west side
    add_npc("Thumper", "rabbit", 3, {45, 75}, {48, 75});          // Near farm, east side
}

// ---- NPC daily schedules ----
// Each slot covers [start_hour, end_hour). The villager walks to the anchor
// when the slot becomes active and stays there until the next one. Hours are
// game hours (6 AM .. ~30). Festival days override the morning/afternoon
// schedule: everyone gathers at the Town Center.
struct SchedSlot { int8_t start_hour; int8_t end_hour; int16_t x, y; };

static const SchedSlot* npc_schedule(const std::string& name, int& n) {
    static const SchedSlot leah[] = {
        {6, 9, 21, 40},    // home (Willow House door)
        {9, 16, 24, 27},   // Stardrop Plaza (civic)
        {16, 21, 43, 34},  // the main bridge (sketching)
        {21, 30, 21, 40},  // home
    };
    static const SchedSlot abigail[] = {
        {6, 9, 27, 40},    // home (Maple House door)
        {9, 17, 40, 84},   // farm gate
        {17, 21, 30, 25},  // Stardrop Saloon
        {21, 30, 27, 40},  // home
    };
    static const SchedSlot elliot[] = {
        {6, 9, 51, 40},    // home (Rowan Cottage door)
        {9, 13, 43, 34},   // the main bridge (writing)
        {13, 17, 24, 27},  // Stardrop Plaza
        {17, 21, 30, 25},  // Stardrop Saloon
        {21, 30, 51, 40},  // home
    };
    static const SchedSlot robin[] = {
        {6, 9, 51, 24},    // home (Carpenter Shop)
        {9, 13, 60, 20},   // the market
        {13, 17, 51, 24},  // home (workshop)
        {17, 21, 30, 25},  // Stardrop Saloon
        {21, 30, 51, 24},  // home
    };
    static const SchedSlot evelyn[] = {
        {6, 10, 94, 24},   // tearoom shore (Lake Aurora)
        {10, 16, 24, 27},  // Stardrop Plaza
        {16, 21, 94, 24},  // tearoom shore
        {21, 30, 94, 24},  // home (the shore)
    };
    struct Table { const char* who; const SchedSlot* slots; int n; };
    static const Table tables[] = {
        {"Leah", leah, 4}, {"Abigail", abigail, 4}, {"Elliot", elliot, 5},
        {"Robin", robin, 5}, {"Evelyn", evelyn, 4},
    };
    for (auto& t : tables)
        if (name == t.who) { n = t.n; return t.slots; }
    n = 0;
    return nullptr;
}

// Egg Festival — Spring 13. Everyone gathers at the Town Center.
bool is_festival_day(uint32_t day) {
    return season_index(day) == 0 && season_day(day) == 13;
}

// Current schedule slot for this hour; writes the anchor to walk to.
// Returns -1 when the villager has no schedule (keeps free-roaming).
int schedule_slot(const std::string& name, uint32_t day, int hour,
                  Vec2& anchor) {
    if (is_festival_day(day) && hour >= 8 && hour < 20) {
        anchor = {21, 27};   // Town Center door
        return 100;          // festival overrides everything
    }
    int n = 0;
    const SchedSlot* slots = npc_schedule(name, n);
    for (int i = 0; i < n; ++i)
        if (hour >= slots[i].start_hour && hour < slots[i].end_hour) {
            anchor = {slots[i].x, slots[i].y};
            return i;
        }
    return -1;
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
        pl["fest_eggs"] = p.fest_eggs;
        pl["fest_tries"] = p.fest_tries;
        json kl = json::array();
        for (auto& lm : p.known_landmarks) kl.push_back(lm);
        pl["known_landmarks"] = kl;
        json op = json::array();
        for (auto& o : p.owned_plots) op.push_back(o);
        pl["owned_plots"] = op;
        json ps = json::array();
        for (auto& st : p.placed_structs)
            ps.push_back({{"plot_idx", st.plot_idx}, {"type", st.type}, {"x", st.x}, {"y", st.y}});
        pl["placed_structs"] = ps;
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
                cj["is_trellis"] = c.crop.is_trellis;
                cj["is_fruit_tree"] = c.crop.is_fruit_tree;
                cj["last_harvest_season"] = c.crop.last_harvest_season;
            }
            j["cells"].push_back(cj);
        }
    // Serialize building states
    j["building_states"] = json::object();
    for (auto& [name, bs] : w.building_states) {
        j["building_states"][name] = {
            {"condition", bs.condition},
            {"roof_leak", bs.roof_leak},
            {"foundation", bs.foundation},
            {"last_maintained_day", bs.last_maintained_day}
        };
    }
    // Serialize plots (R16)
    j["plots"] = json::array();
    for (auto& p : w.plots) {
        j["plots"].push_back({{"name", p.name}, {"owner_id", p.owner_id}});
    }
    j["farmhouse_level"] = w.farmhouse_level;
    return j.dump();
}

bool deserialize_world(World& w, const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        w.day = j.value("day", 1);
        w.day_seconds = j.value("time", 0.0f);
        w.next_player_id = j.value("next_player_id", 1);
        w.farmhouse_level = j.value("farmhouse_level", w.farmhouse_level);
        for (auto& pl : j.value("players", json::array())) {
            Player p;
            p.id = pl["id"]; p.pos = {pl["x"], pl["y"]}; p.target = p.pos;
            p.dir = pl.value("dir", 0);
            p.energy = pl.value("energy", 270.0f);
            p.money = pl.value("money", 500);
            p.sel = pl.value("sel", 0);
            p.name = pl.value("name", "Player");
            p.max_energy = pl.value("max_energy", 270);
            p.fest_eggs = pl.value("fest_eggs", 0);
            p.fest_tries = pl.value("fest_tries", 8);
            p.inside = pl.value("inside", "");
            p.inx = pl.value("inx", 0);
            p.iny = pl.value("iny", 0);
            p.train_used = pl.value("train_used", false);
            p.inside_exit = {(int16_t)pl.value("exit_x", 0), (int16_t)pl.value("exit_y", 0)};
            for (auto& hn : pl.value("hearts", json::object()).items())
                p.hearts[hn.key()] = hn.value().get<uint8_t>();
            for (auto& g : pl.value("gifted_today", json::array()))
                p.gifted_today.insert(g.get<std::string>());
            for (auto& lm : pl.value("known_landmarks", json::array()))
                p.known_landmarks.insert(lm.get<std::string>());
            for (auto& o : pl.value("owned_plots", json::array()))
                p.owned_plots.insert(o.get<size_t>());
            for (auto& st : pl.value("placed_structs", json::array())) {
                Player::PlacedStruct ps;
                ps.plot_idx = st.value("plot_idx", static_cast<size_t>(0));
                ps.type = st.value("type", 0);
                ps.x = st.value("x", 0);
                ps.y = st.value("y", 0);
                p.placed_structs.push_back(ps);
            }
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
                c.crop.is_trellis = cj.value("is_trellis", false);
                c.crop.is_fruit_tree = cj.value("is_fruit_tree", false);
                c.crop.last_harvest_season = cj.value("last_harvest_season", -1);
            }
        }
        // Deserialize building states
        if (j.contains("building_states")) {
            for (auto& [name, bs_json] : j["building_states"].items()) {
                BuildingState bs;
                bs.condition = bs_json.value("condition", 100);
                bs.roof_leak = bs_json.value("roof_leak", 0);
                bs.foundation = bs_json.value("foundation", 100);
                bs.last_maintained_day = bs_json.value("last_maintained_day", 0);
                w.building_states[name] = bs;
            }
        }
        // Deserialize plots (R16)
        if (j.contains("plots") && !w.plots.empty()) {
            size_t idx = 0;
            for (auto& pj : j["plots"]) {
                if (idx >= w.plots.size()) break;
                w.plots[idx].owner_id = pj.value("owner_id", 0);
                ++idx;
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "save load failed: " << e.what() << "\n";
        return false;
    }
}