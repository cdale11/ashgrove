#include "world.hpp"
#include <nlohmann/json.hpp>
#include <random>
#include <cmath>
#include <algorithm>
#include <cctype>
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
    scatter(45, ObjType::Rock, 2); // iron
    scatter(20, ObjType::Rock, 1); // copper ore veins
    scatter(10, ObjType::Rock, 2); // iron ore veins
    scatter(5, ObjType::Rock, 3);  // gold ore veins
    scatter(2, ObjType::Rock, 4);  // iridium ore veins
    scatter(4, ObjType::Well, 100);   // wells with full water
    scatter(6, ObjType::Pond, 100);   // ponds with full water
    scatter(70, ObjType::Weed, 1);
    scatter(35, ObjType::TallGrass, 1);
    scatter(50, ObjType::Flower, 1);
    scatter(60, ObjType::Bush, 1);
    scatter(22, ObjType::Mushroom, 1);

    // ---- trees around farmhouse (ornamental + fruit trees as crops) ----
    auto near_farmhouse = [&](int x, int y) {
        return x >= world.house_tl.x - 6 && x < world.house_tl.x + 9 &&
               y >= world.house_tl.y - 6 && y < world.house_tl.y + 12;
    };
    // Plant a ring of trees around the farmhouse (outside the 1-tile border)
    for (int dy = -6; dy <= 5; ++dy) {
        for (int dx = -6; dx <= 8; ++dx) {
            int x = world.house_tl.x + dx;
            int y = world.house_tl.y + dy;
            if (!world.in_bounds(x, y)) continue;
            if (!blocked_house(x, y) && near_farmhouse(x, y) &&
                world.at(x, y).obj.type == ObjType::None &&
                (world.at(x, y).tile == Tile::Grass || world.at(x, y).tile == Tile::GrassVar)) {
                float d = dist(rng);
                if (d < 0.20f) world.at(x, y).obj = {ObjType::Tree, 3}; // oak/maple
                else if (d < 0.30f) world.at(x, y).obj = {ObjType::Oak, 3};
                else if (d < 0.40f) world.at(x, y).obj = {ObjType::Maple, 3};
                else if (d < 0.55f) world.at(x, y).obj = {ObjType::Bush, 1};
                else if (d < 0.70f) world.at(x, y).obj = {ObjType::Flower, 1};
            }
        }
    }
    // Clear the doorstep area
    Vec2 d = world.door();
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (world.in_bounds(d.x + dx, d.y + dy))
                world.at(d.x + dx, d.y + dy).obj = FarmObj{};
    // L4: Initialize forest_state for farmhouse ornamental trees
    for (int dy = -6; dy <= 5; ++dy)
        for (int dx = -6; dx <= 8; ++dx) {
            int x = world.house_tl.x + dx;
            int y = world.house_tl.y + dy;
            if (!world.in_bounds(x, y)) continue;
            if (!blocked_house(x, y) && near_farmhouse(x, y)) {
                Cell& c = world.at(x, y);
                if (is_tree(c.obj.type)) {
                    c.forest_state = 0;
                    forest_set_canopy(c.forest_state, 3 + (rng() & 3)); // canopy 3-6
                    forest_set_undergrowth(c.forest_state, 1); // fern
                    forest_set_player_managed(c.forest_state, true);
                }
            }
        }

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
    // L4: Initialize forest_state for Whisper Wood tiles
    for (int y = 16; y < 74; ++y)
        for (int x = 4; x < 20; ++x) {
            if (!world.in_bounds(x, y)) continue;
            Cell& c = world.at(x, y);
            if (is_water(c.tile) || c.tile == Tile::Dirt || c.tile == Tile::Tilled || c.tile == Tile::Bridge)
                continue;
            // Old-growth forest: high canopy, fern/mushroom undergrowth
            c.forest_state = 0;
            forest_set_canopy(c.forest_state, 5 + (rng() & 3)); // canopy 5-7
            // Undergrowth: 0=none, 1=fern, 2=berry, 3=mushroom
            uint8_t ug = rng() % 4;
            forest_set_undergrowth(c.forest_state, ug);
            // Some nurse logs in old growth
            if ((rng() & 15) == 0) forest_set_nurse_log(c.forest_state, true);
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
    // Ensure every building has at least a minimal interior so entering never falls back
    for (auto& b : world.buildings) {
        if (world.interiors.find(b.name) == world.interiors.end()) {
            InteriorRoom r;
            r.building = b.name;
            // simple 3x3 floor with doorway at bottom centre
            r.rows = {
                "#####",
                "#...#",
                "## ##",
            };
            r.h = static_cast<int16_t>(r.rows.size());
            r.w = static_cast<int16_t>(r.rows[0].size());
            world.interiors[b.name] = std::move(r);
        }
    }
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
    // ROADMAP 2.1 (8.1a) — Initialize soil chemistry with biome-appropriate defaults.
    // Uses the tile type and biome to set initial NPK, pH, OM, microbiome.
    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            Cell& c = world.at(x, y);
            // Base values — temperate grassland default
            uint8_t n = 128, p = 128, k = 128;
            uint8_t ph = 70;          // 7.0 neutral
            uint8_t om = 50;          // 25% OM
            uint8_t micro = 100;      // moderate microbiome
            switch (c.tile) {
                case Tile::Grass:
                case Tile::GrassVar:
                    // Fertile grassland
                    n = 140; p = 130; k = 120;
                    ph = 68; om = 55; micro = 110;
                    break;
                case Tile::Dirt:
                    // Compacted, low fertility
                    n = 80; p = 90; k = 100;
                    ph = 65; om = 20; micro = 60;
                    break;
                case Tile::Sand:
                    // Sandy, low retention
                    n = 60; p = 70; k = 80;
                    ph = 72; om = 10; micro = 40;
                    break;
                case Tile::Tilled:
                    // Recently worked soil, high OM from previous cycle
                    n = 150; p = 140; k = 130;
                    ph = 68; om = 60; micro = 120;
                    break;
                case Tile::Water:
                case Tile::WaterNorth:
                case Tile::WaterSouth:
                case Tile::WaterEast:
                case Tile::WaterWest:
                    // Aquatic — no soil
                    n = 0; p = 0; k = 0;
                    ph = 70; om = 0; micro = 20;
                    break;
                case Tile::Cobble:
                case Tile::Bridge:
                    // Built surfaces
                    n = 20; p = 20; k = 20;
                    ph = 75; om = 5; micro = 10;
                    break;
                case Tile::Snow:
                case Tile::Ice:
                    // Frozen — dormant
                    n = 50; p = 50; k = 50;
                    ph = 60; om = 15; micro = 30;
                    break;
                default:
                    break;
            }
            // Biome variation: river proximity increases N (alluvial deposits)
            for (int y2 = 0; y2 < MAP_H; ++y2) {
                for (int x2 = 0; x2 < MAP_W; ++x2) {
                    // Simplified: if near water (within 5 tiles), boost N slightly
                    // Skip for performance - will be handled by leaching CA over time
                }
            }
            c.nitrogen = n;
            c.phosphorus = p;
            c.potassium = k;
            c.ph = ph;
            c.organic_matter = om;
            c.microbiome = micro;
            // ROADMAP 2.2 (8.1b) — Water Table initialization based on tile/biome
            // Water table depth: lower in wetlands/river areas, higher on hills
            if (c.tile == Tile::Water || c.tile == Tile::WaterNorth || c.tile == Tile::WaterSouth ||
                c.tile == Tile::WaterEast || c.tile == Tile::WaterWest) {
                c.water_table_depth = 0;   // at surface
                c.saturation = 255;
            } else if (c.tile == Tile::Sand || c.tile == Tile::Dirt) {
                c.water_table_depth = 50;  // shallow
                c.saturation = 180;
            } else if (c.tile == Tile::Grass || c.tile == Tile::GrassVar) {
                c.water_table_depth = 150; // ~1.5m default
                c.saturation = 100;
            } else {
                c.water_table_depth = 200; // deep / dry
                c.saturation = 50;
            }
            c.aquifer_transmissivity = 100;
            c.specific_yield = 50;
            c.recharge_capacity = 100;
        }
    }
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
    // keep the farm gate clear (gap in fence at y=85, x=38..41)
    {
        for (int xx = 37; xx <= 42; ++xx) {
            for (int yy = 83; yy <= 86; ++yy) {
                clear_cell(xx, yy);
            }
        }
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
        // ---- horror location structures (ROADMAP 1.1) ----
        {"Witch's Hut", 12, 52, 2, 2},          // Whisper Wood (Woodland district)
        {"Abandoned Sanitarium", 100, 42, 3, 3},// East Moor (Horror district)
        {"Ritual Circle", 20, 60, 2, 2},        // Whisper Wood / forest border
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
        r.h = static_cast<int16_t>(ground.size());
        r.w = static_cast<int16_t>(r.h ? ground[0].size() : 0);
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
    // ============================================================
    // PHASE 4: NEW INTERIOR ROOMS
    // ============================================================

    // Barn Interior - Animal housing
    room("Barn Interior", {
        "################",
        "#HHHHHHHHHHHHHH#",  // Hay loft above
        "#..............#",
        "#..AA..AA..AA..#",  // Animal stalls (A=animal)
        "#..............#",
        "#..FF..FF..FF..#",  // Feed troughs (F=feed)
        "#..............#",
        "#..MM..MM..MM..#",  // Milking stations (M=milk)
        "#..............#",
        "##            ##",
    }, {
        {   // Floor 1 - Hay loft storage
            "################",
            "#YYYYYYYYYYYYYY#",  // Hay bales
            "#..............#",
            "#..SS..SS..SS..#",  // Supply storage
            "#..............#",
            "#..TT..TT..TT..#",  // Tool storage
            "#..............#",
            "#..<>..........#",  // Stairs down
            "##            ##",
        }
    });
    world.interiors["Barn Interior"].type = InteriorType::BarnInterior;

    // Greenhouse Interior - Year-round growing
    room("Greenhouse Interior", {
        "################",
        "#GGGGGGGGGGGGGG#",  // Growing beds (G=growing)
        "#..............#",
        "#..GG..GG..GG..#",  // Plant beds
        "#..............#",
        "#..WW..WW..WW..#",  // Water tanks (W=water)
        "#..............#",
        "#..TT..TT..TT..#",  // Tool benches
        "#..............#",
        "##            ##",
    });
    world.interiors["Greenhouse Interior"].type = InteriorType::Greenhouse;

    // Cellar Interior - Cask aging
    room("Cellar Interior", {
        "################",
        "#CCCCCCCCCCCCCC#",  // Cask rows (C=cask)
        "#..............#",
        "#..CC..CC..CC..#",  // Casks
        "#..............#",
        "#..WW..WW..WW..#",  // Wine racks (W=wine)
        "#..............#",
        "#..TT..TT..TT..#",  // Tool benches
        "#..............#",
        "##            ##",
    });
    world.interiors["Cellar Interior"].type = InteriorType::Cellar;

    // Shrine Interior - Ancient ruin shrine
    room("Shrine Interior", {
        "################",
        "#..............#",
        "#..AA..AA..AA..#",  // Altar pieces (A=altar)
        "#..............#",
        "#..CC..CC..CC..#",  // Candles (C=candle)
        "#..............#",
        "#..RR..RR..RR..#",  // Offerings (R=offering)
        "#..............#",
        "#..<>..........#",  // Stairs down to catacombs
        "##            ##",
    }, {
        {   // Floor 1 - Catacombs
            "################",
            "#..............#",
            "#..TT..TT..TT..#",  // Tombs (T=tomb)
            "#..............#",
            "#..OO..OO..OO..#",  // Offerings (O=offering)
            "#..............#",
            "#..<>..........#",  // Stairs up
            "##            ##",
        }
    });
    world.interiors["Shrine Interior"].type = InteriorType::ShrineInterior;

    // Cabin Interior - Simple wilderness shelter
    room("Cabin Interior", {
        "########",
        "#B..V..S.#",  // Bed, TV, Shelf
        "#........#",
        "#..T..K..#",  // Table, Kitchen
        "#........#",
        "#..C..C..#",  // Chest storage
        "##   ##",
    });
    world.interiors["Cabin Interior"].type = InteriorType::CabinInterior;

    // Ruin Interior - Ancient structure
    room("Ruin Interior", {
        "################",
        "#..............#",
        "#..PP..PP..PP..#",  // Pillars (P=pillar)
        "#..............#",
        "#..RR..RR..RR..#",  // Rubble
        "#..............#",
        "#..<>..........#",  // Stairs down
        "##            ##",
    }, {
        {   // Floor 1 - Deep ruins
            "################",
            "#..............#",
            "#..TT..TT..TT..#",  // Treasure/Traps
            "#..............#",
            "#..<>..........#",  // Stairs up
            "##            ##",
        }
    });
    world.interiors["Ruin Interior"].type = InteriorType::RuinInterior;

    // Cave Interior - Natural cave
    room("Cave Interior", {
        "################",
        "#..............#",
        "#..SS..SS..SS..#",  // Stalactites (S=stalactite)
        "#..............#",
        "#..OO..OO..OO..#",  // Ore veins (O=ore)
        "#..............#",
        "#..<>..........#",  // Deeper tunnel
        "##            ##",
    }, {
        {   // Floor 1 - Deep cave
            "################",
            "#..............#",
            "#..CC..CC..CC..#",  // Crystals
            "#..............#",
            "#..<>..........#",  // Exit
            "##            ##",
        }
    });
    world.interiors["Cave Interior"].type = InteriorType::CaveInterior;

    // Well Interior - Water source
    room("Well Interior", {
        "########",
        "#WWWWWWWW#",  // Water (W=water)
        "#........#",
        "#..BB....#",  // Bucket (B=bucket)
        "#........#",
        "#..<>....#",  // Up
        "##   ##",
    });
    world.interiors["Well Interior"].type = InteriorType::WellInterior;

    // Windmill Interior - Grain processing
    room("Windmill Interior", {
        "########",
        "#GGGGGGGG#",  // Grain hoppers (G=grain)
        "#........#",
        "#..MM....#",  // Millstones (M=mill)
        "#........#",
        "#..FF....#",  // Flour output (F=flour)
        "##   ##",
    }, {
        {   // Floor 1 - Mechanism
            "########",
            "#GGGGGGGG#",  // Gears
            "#........#",
            "#..AA....#",  // Axle
            "#........#",
            "#..<>....#",  // Down
            "##   ##",
        }
    });
    world.interiors["Windmill Interior"].type = InteriorType::WindmillInterior;

    // Silo Interior - Grain storage
    room("Silo Interior", {
        "########",
        "#GGGGGGGG#",  // Grain (G=grain)
        "#........#",
        "#..CC....#",  // Conveyor (C=conveyor)
        "#........#",
        "#..<>....#",  // Up/Down
        "##   ##",
    }, {
        {   // Floor 1 - Upper storage
            "########",
            "#GGGGGGGG#",  // More grain
            "#........#",
            "#..<>....#",  // Down
            "##   ##",
        }
    });
    world.interiors["Silo Interior"].type = InteriorType::SiloInterior;

    // Shed Interior - Tool storage
    room("Shed Interior", {
        "########",
        "#TTTTTTTT#",  // Tools (T=tool)
        "#........#",
        "#..WW....#",  // Workbench (W=work)
        "#........#",
        "#..SS....#",  // Shelves (S=shelf)
        "##   ##",
    });
    world.interiors["Shed Interior"].type = InteriorType::ShedInterior;

    // ============================================================
    // HORROR LOCATION STRUCTURES (ROADMAP 1.1)
    // ============================================================

    // Witch's Hut (Woodland) — the Witch's home; knowledge broker / cycle witness.
    room("Witch's Hut", {
        "##########",
        "#..YYYY..#",  // Scrolls & lore shelves (Y)
        "#..T......#", // Table with an old mirror
        "#...KK...#",  // Kettle (brews things)
        "#..<>....#",  // Stairs back out
        "##       ##",
    });
    world.interiors["Witch's Hut"].type = InteriorType::WitchHut;

    // Abandoned Sanitarium (Horror district) — collective guilt made manifest.
    room("Abandoned Sanitarium", {
        "############",
        "#..DDDD....#",  // Derelict examination beds (D)
        "#..........#",
        "#..WWWW....#",  // Worn writings / patient records (W)
        "#..........#",
        "#..IIII....#",  // Instruments (I)
        "#..........#",
        "#..<>......#",  // Stairs back out
        "##        ##",
    });
    world.interiors["Abandoned Sanitarium"].type = InteriorType::Sanitarium;

    // Ritual Circle (forest border) — the cycle mechanics; entity communication.
    room("Ritual Circle", {
        "##########",
        "#....RR...#",  // Ritual candles / marks (R)
        "#..........#",
        "#..AAAA....#",  // Altar stone (A)
        "#..........#",
        "#..<>......#",  // Stairs back out
        "##        ##",
    });
    world.interiors["Ritual Circle"].type = InteriorType::RitualCircle;

    // ============================================================
    // THE BASEMENT (Phase 6) — under-map, reachable only after midnight
    // ============================================================
    room("Basement", {
        "############",
        "#..........#",
        "#..AAAA....#",  // Altar (A) — this is the source of the horror
        "#..........#",
        "#..CCCC....#",  // Candle / evidence (C)
        "#..........#",
        "#..SSSS....#",  // Strange writings / sketches (S)
        "#..........#",
        "#..<>......#",  // Stairs up
        "##        ##",
    });
    world.interiors["Basement"].type = InteriorType::Basement;
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
    // ~20% rainy days, plus autumn foggy mornings, rare severe storms
    if (season == 2) { // Fall
        unsigned r = ((day * 2654435761u) >> 16) % 100;
        if (r < 1) return 3; // 1% severe storm
        if (r < 3) return 2; // 2% foggy
        if (r < 5) return 1; // 2% rainy
        return 0;
    }
    // Spring/Summer: rare severe storms (0.5%)
    unsigned r = ((day * 2654435761u) >> 16) % 200;
    if (r < 1) return 3; // 0.5% severe storm
    return (r < 40) ? 1 : 0; // ~20% rainy
}

const char* weather_of_day_name(uint32_t day) {
    int w = weather_of_day(day);
    if (w == 3) return "Severe Storm";
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

// L6: Wildlife system
void World::init_wildlife() {
    wildlife.clear();
    std::mt19937 rng(day * 12345 + 67890);
    std::uniform_int_distribution<int> dist_x(0, MAP_W - 1);
    std::uniform_int_distribution<int> dist_y(0, MAP_H - 1);
    
    // Spawn deer in forest/edge biomes
    for (int i = 0; i < 8; ++i) {
        int x, y;
        int attempts = 0;
        do {
            x = dist_x(rng);
            y = dist_y(rng);
            attempts++;
            if (attempts > 100) break;
        } while (!in_bounds(x, y) || 
                 at(x, y).tile != Tile::Grass && at(x, y).tile != Tile::GrassVar ||
                 walkable(x, y) == false);
        if (attempts <= 100) {
            Wildlife d;
            d.type = WildlifeType::Deer;
            d.pos = {int16_t(x), int16_t(y)};
            d.home = {int16_t(x), int16_t(y)};
            d.range = 15;
            d.state = 1; // grazing
            d.last_move = 0;
            wildlife.push_back(d);
        }
    }
    
    // Spawn owls in forest (perch in large trees)
    for (int i = 0; i < 4; ++i) {
        int x, y;
        int attempts = 0;
        do {
            x = dist_x(rng);
            y = dist_y(rng);
            attempts++;
            if (attempts > 100) break;
        } while (!in_bounds(x, y) || 
                 !is_tree(at(x, y).obj.type) ||
                 at(x, y).obj.type != ObjType::Tree && at(x, y).obj.type != ObjType::Pine &&
                 at(x, y).obj.type != ObjType::Oak);
        if (attempts <= 100) {
            Wildlife o;
            o.type = WildlifeType::Owl;
            o.pos = {int16_t(x), int16_t(y)};
            o.home = {int16_t(x), int16_t(y)};
            o.range = 20;
            o.state = 4; // perching
            o.last_move = 0;
            wildlife.push_back(o);
        }
    }
    
    // Spawn fisher-cats in deep forest
    for (int i = 0; i < 2; ++i) {
        int x, y;
        int attempts = 0;
        do {
            x = dist_x(rng);
            y = dist_y(rng);
            attempts++;
            if (attempts > 100) break;
        } while (!in_bounds(x, y) || 
                 at(x, y).tile != Tile::Grass && at(x, y).tile != Tile::GrassVar ||
                 x < 4 || x > 20 || y < 16 || y > 74); // Whisper Wood area
        if (attempts <= 100) {
            Wildlife f;
            f.type = WildlifeType::FisherCat;
            f.pos = {int16_t(x), int16_t(y)};
            f.home = {int16_t(x), int16_t(y)};
            f.range = 25;
            f.state = 3; // hunting
            f.last_move = 0;
            wildlife.push_back(f);
        }
    }
}

void World::tick_wildlife() {
    uint32_t now = day * 10000; // approximate ms
    for (auto& w : wildlife) {
        // Only move occasionally
        if (now - w.last_move < 2000 + (w.type == WildlifeType::Owl ? 5000 : 0)) continue;
        
        int dx = 0, dy = 0;
        switch (w.type) {
            case WildlifeType::Deer: {
                // Deer: graze near home, flee from player/predators
                if (w.state == 2) { // fleeing
                    dx = (w.pos.x > w.target_x) ? 1 : (w.pos.x < w.target_x ? -1 : 0);
                    dy = (w.pos.y > w.target_y) ? 1 : (w.pos.y < w.target_y ? -1 : 0);
                } else {
                    // Graze: random walk near home
                    static std::mt19937 rng(day * 99991);
                    std::uniform_int_distribution<int> dist(-1, 1);
                    dx = dist(rng);
                    dy = dist(rng);
                    // Stay near home
                    if (abs(int(w.pos.x + dx) - w.home.x) > w.range) dx = 0;
                    if (abs(int(w.pos.y + dy) - w.home.y) > w.range) dy = 0;
                }
                break;
            }
            case WildlifeType::Owl: {
                // Owl: nocturnal, perch in trees, hunt at night
                int hour = hour_of_day(*this);
                if (hour >= 20 || hour < 6) {
                    w.state = 3; // hunting
                    // Move to hunting ground
                    dx = (w.pos.x > w.home.x) ? -1 : (w.pos.x < w.home.x ? 1 : 0);
                    dy = (w.pos.y > w.home.y) ? -1 : (w.pos.y < w.home.y ? 1 : 0);
                } else {
                    w.state = 4; // perching in tree
                    // Stay in tree
                }
                break;
            }
            case WildlifeType::FisherCat: {
                // Fisher-cat: territorial, hunts small game
                w.state = 3; // hunting
                // Patrol territory
                static std::mt19937 rng(day * 55555);
                std::uniform_int_distribution<int> dist(-1, 1);
                dx = dist(rng);
                dy = dist(rng);
                if (abs(int(w.pos.x + dx) - w.home.x) > w.range) dx = 0;
                if (abs(int(w.pos.y + dy) - w.home.y) > w.range) dy = 0;
                break;
            }
            default: break;
        }
        
        // Try to move
        int nx = w.pos.x + dx;
        int ny = w.pos.y + dy;
        if (this->in_bounds(nx, ny) && this->walkable(nx, ny)) {
            w.pos = {int16_t(nx), int16_t(ny)};
            w.last_move = day * 10000;
        }
    }
    
    // Remove wildlife that wandered too far
    wildlife.erase(std::remove_if(wildlife.begin(), wildlife.end(), 
        [this](const Wildlife& w) {
            return !this->in_bounds(w.pos.x, w.pos.y) || 
                   (abs(w.pos.x - w.home.x) > w.range * 2 && abs(w.pos.y - w.home.y) > w.range * 2);
        }), wildlife.end());
}

void World::spawn_wildlife_near(WildlifeType type, int x, int y, int count) {
    std::mt19937 rng(day * 12345 + 67890);
    std::uniform_int_distribution<int> dist(-3, 3);
    for (int i = 0; i < count; ++i) {
        int nx = x + dist(rng);
        int ny = y + dist(rng);
        if (!in_bounds(nx, ny) || !walkable(nx, ny)) continue;
        Wildlife w;
        w.type = type;
        w.pos = {int16_t(nx), int16_t(ny)};
        w.home = {int16_t(nx), int16_t(ny)};
        w.range = (type == WildlifeType::Owl) ? 20 : (type == WildlifeType::FisherCat ? 25 : 15);
        w.state = (type == WildlifeType::Owl) ? 4 : (type == WildlifeType::FisherCat ? 3 : 1);
        w.last_move = 0;
        wildlife.push_back(w);
    }
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
// L2: Forward declaration for building repair override check
bool check_building_repair_override(const std::string& name, int hour, Vec2& anchor, const World& w);

// Returns -1 when the villager has no schedule (keeps free-roaming).
int schedule_slot(const std::string& name, uint32_t day, int hour,
                  Vec2& anchor, const World* w) {
    if (is_festival_day(day) && hour >= 8 && hour < 20) {
        anchor = {21, 27};   // Town Center door
        return 100;          // festival overrides everything
    }
    
    // L2: Check if NPC's home/workplace needs repair (condition < 20)
    if (w && check_building_repair_override(name, hour, anchor, *w)) {
        return 101;  // repair override
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

// L2: Check if NPC's home/workplace building needs repair (condition < 20)
// If so, override schedule to make NPC go repair it.
bool check_building_repair_override(const std::string& name, int hour, Vec2& anchor, const World& w) {
    // Map NPC names to their home/workplace building names
    std::string building_name = "";
    bool is_home = true;
    
    if (name == "Leah") building_name = "Willow House";
    else if (name == "Abigail") building_name = "Maple House";
    else if (name == "Elliot") building_name = "Rowan Cottage";
    else if (name == "Robin") { building_name = "Carpenter Shop"; is_home = false; }
    else if (name == "Evelyn") building_name = "Tearoom";
    else return false;
    
    auto it = w.building_states.find(building_name);
    if (it == w.building_states.end()) return false;
    
    const BuildingState& bs = it->second;
    if (bs.condition >= 20) return false;  // Building is fine
    
    // NPC should go repair - find the building's door location
    // Find building door coordinates from the Bldg list
    const Bldg* target = nullptr;
    for (const auto& b : w.buildings) {
        if (b.name == building_name) {
            target = &b;
            break;
        }
    }
    if (!target) return false;
    
    // Building door is at center-bottom of building
    int door_x = target->x + (target->w - 1) / 2;
    int door_y = target->y + target->h - 1;
    
    // Only override during work hours (9-17) or if NPC is at home (evening)
    if (is_home) {
        // Home repair: NPC goes to building during evening (17-21) or morning (6-8)
        if (hour >= 17 && hour < 21) {
            anchor = {door_x, door_y};
            return true;
        }
    } else {
        // Workplace repair: Robin goes to Carpenter Shop during work hours
        if (hour >= 9 && hour < 17) {
            anchor = {door_x, door_y};
            return true;
        }
    }
    return false;
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

// P2: death-aware references from survivors of the loops. The 4 design-survivor
// NPCs (Mayor, Witch, Traveler, Doctor — ROADMAP §2.1) remember across cycles
// explicitly; the 5 talkable villagers react to the rumor of your deaths.
std::string npc_death_line(const char* name, uint32_t death_count) {
    if (death_count == 0) return "";
    std::string nm(name);
    if (nm == "Mayor") {
        return death_count == 1
            ? "I heard you had a bad night. The valley forgives, farmer — but it forgets nothing."
            : "That's " + std::to_string(death_count) + ". I count, even when I pretend not to.";
    }
    if (nm == "Witch") {
        return death_count == 1
            ? "Ah, you've seen it now. The first death is a door. The rest are a hallway."
            : "You keep dying and the valley keeps waking. I've watched you " + std::to_string(death_count) + " times now.";
    }
    if (nm == "Traveler") {
        return "I remember you. Everyone else forgets, but I don't. You've been through this " + std::to_string(death_count) + " times.";
    }
    if (nm == "Doctor") {
        return death_count == 1
            ? "I patched you up once already. Be careful — I can only do so much."
            : "You're a hard patient. " + std::to_string(death_count) + " visits to my care.";
    }
    // Talkable villagers: they don't know about loops, but rumors travel.
    if (death_count >= 2) {
        if (nm == "Leah")   return "I heard you collapsed out there. The town was talking all day.";
        if (nm == "Abigail")return "You look pale. Whatever got you — it didn't keep you down.";
        if (nm == "Elliot") return "That was a close one. I write tragedies, but I'd rather not star in yours.";
        if (nm == "Robin")  return "Tough luck out there. Rest up — the wood won't chop itself.";
        if (nm == "Evelyn") return "Oh, dear heart, you gave us a fright. Take care of yourself.";
    }
    return "";   // everyone else doesn't dwell on it
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
    uint32_t start = static_cast<uint32_t>(from.y) * MAP_W + static_cast<uint32_t>(from.x);
    uint32_t goal = static_cast<uint32_t>(to.y) * MAP_W + static_cast<uint32_t>(to.x);
    prev[start] = static_cast<int16_t>(start);
    q.push_back(start);
    while (!q.empty()) {
        uint32_t cur = q.front(); q.pop_front();
        if (cur == goal) break;
        int cx = static_cast<int>(cur % MAP_W), cy = static_cast<int>(cur / MAP_W);
        for (int i = 0; i < 4; ++i) {
            int nx = cx + DX[i], ny = cy + DY[i];
            if (!world.in_bounds(nx, ny) || !world.walkable(nx, ny)) continue;
            if (nx == to.x && ny == to.y) { prev[goal] = static_cast<int16_t>(cur); cur = goal; q.clear(); break; }
            uint32_t ni = static_cast<uint32_t>(ny) * MAP_W + static_cast<uint32_t>(nx);
            if (prev[ni] == -1) { prev[ni] = static_cast<int16_t>(cur); q.push_back(ni); }
        }
    }
    if (prev[goal] == -1) return false;
    out.clear();
    for (uint32_t c = goal; c != start && out.size() < max_len; c = static_cast<uint32_t>(prev[c]))
        out.push_back({static_cast<int16_t>(c % MAP_W), static_cast<int16_t>(c / MAP_W)});
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
        pl["sanity"] = p.sanity;
        pl["max_sanity"] = p.max_sanity;
        pl["last_sanity_day"] = p.last_sanity_day;
        pl["health"] = p.health;
        pl["max_health"] = p.max_health;
        pl["death_count"] = p.death_count;
        pl["safe_x"] = p.last_safe_pos.x;
        pl["safe_y"] = p.last_safe_pos.y;
        json sf = json::array();
        for (auto& s : p.secrets_found) sf.push_back(s);
        pl["secrets_found"] = sf;
        json nel = json::array();
        for (auto& e : p.night_event_log) nel.push_back(e);
        pl["night_event_log"] = nel;
        // ROADMAP 1.4 — perception-filter / basement-procgen state.
        pl["meta_break_fired"] = p.meta_break_fired;
        pl["basement_mark"] = p.basement_mark;
        pl["mark_days_left"] = p.mark_days_left;
        json dc = json::array();
        for (uint8_t t = 0; t < 4; ++t) dc.push_back(p.dread_counters[t]);
        pl["dread_counters"] = dc;
        j["players"].push_back(pl);
    }
    j["cells"] = json::array();
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x) {
            const Cell& c = w.at(x, y);
            if (c.obj.type == ObjType::None && !c.crop.is_crop() &&
                c.tile == Tile::Grass) continue;
            json cj{{"x", x}, {"y", y}, {"tile", static_cast<int>(c.tile)},
                    {"obj", static_cast<int>(c.obj.type)}, {"hp", c.obj.hp}, {"ore", c.obj.ore},
                    {"hp2", c.obj.hp2}, {"hp3", c.obj.hp3},
                    {"snow_compaction", c.snow_compaction}, {"forest_state", c.forest_state},
                    {"track_age", c.track_age}, {"track_type", c.track_type}, {"track_dir", c.track_dir}};
            // ROADMAP 2.1/2.2 — Soil Chemistry + Water Table
            cj["nitrogen"] = c.nitrogen;
            cj["phosphorus"] = c.phosphorus;
            cj["potassium"] = c.potassium;
            cj["ph"] = c.ph;
            cj["organic_matter"] = c.organic_matter;
            cj["microbiome"] = c.microbiome;
            cj["water_table_depth"] = c.water_table_depth;
            cj["saturation"] = c.saturation;
            cj["aquifer_transmissivity"] = c.aquifer_transmissivity;
            cj["specific_yield"] = c.specific_yield;
            cj["recharge_capacity"] = c.recharge_capacity;
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
    j["basement_unlocked"] = w.basement_unlocked;
    j["basement_visits"] = w.basement_visits;
    j["horror_cycle"] = w.horror_cycle;
    j["active_horror"] = w.active_horror;
    j["last_night_event"] = w.last_night_event;
    // ROADMAP 1.2 — Valley Entity state (corruption field regrows from anchors,
    // so only the scalar guilt/awakening are persisted).
    j["collective_guilt"] = w.collective_guilt;
    j["valley_awakening"] = w.valley_awakening;
    return j.dump();
}

bool deserialize_world(World& w, const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        w.day = static_cast<uint32_t>(j.value("day", 1));
        w.day_seconds = j.value("time", 0.0f);
        w.next_player_id = static_cast<uint32_t>(j.value("next_player_id", 1));
        w.farmhouse_level = j.value("farmhouse_level", w.farmhouse_level);
        for (auto& pl : j.value("players", json::array())) {
            Player p;
            p.id = pl["id"]; p.pos = {pl["x"], pl["y"]}; p.target = p.pos;
            p.dir = static_cast<uint8_t>(pl.value("dir", 0));
            p.energy = pl.value("energy", 270.0f);
            p.money = pl.value("money", 500);
            p.sel = static_cast<uint8_t>(pl.value("sel", 0));
            p.name = pl.value("name", "Player");
            p.max_energy = static_cast<uint16_t>(pl.value("max_energy", 270));
            p.fest_eggs = static_cast<uint8_t>(pl.value("fest_eggs", 0));
            p.fest_tries = static_cast<uint8_t>(pl.value("fest_tries", 8));
            p.inside = pl.value("inside", "");
            p.inx = static_cast<int16_t>(pl.value("inx", 0));
            p.iny = static_cast<int16_t>(pl.value("iny", 0));
            p.train_used = pl.value("train_used", false);
            p.inside_exit = {static_cast<int16_t>(pl.value("exit_x", 0)), static_cast<int16_t>(pl.value("exit_y", 0))};
            {
                json hearts = pl.value("hearts", json::object());
                for (auto& hn : hearts.items())
                    p.hearts[hn.key()] = hn.value().get<uint8_t>();
            }
            for (auto& g : pl.value("gifted_today", json::array()))
                p.gifted_today.insert(g.get<std::string>());
            for (auto& lm : pl.value("known_landmarks", json::array()))
                p.known_landmarks.insert(lm.get<std::string>());
            for (auto& o : pl.value("owned_plots", json::array()))
                p.owned_plots.insert(o.get<size_t>());
            for (auto& st : pl.value("placed_structs", json::array())) {
                Player::PlacedStruct ps;
                ps.plot_idx = st.value("plot_idx", static_cast<size_t>(0));
                ps.type = static_cast<uint8_t>(st.value("type", 0));
                ps.x = static_cast<int16_t>(st.value("x", 0));
                ps.y = static_cast<int16_t>(st.value("y", 0));
                p.placed_structs.push_back(ps);
            }
            int i = 0;
            for (auto& s : pl.value("inv", json::array())) {
                if (i >= 12) break;
                p.inv[i].item = static_cast<Item>(s.value("item", 0));
                p.inv[i].count = static_cast<uint16_t>(s.value("count", 0));
                ++i;
            }
            p.sanity = pl.value("sanity", 100.0f);
            p.max_sanity = pl.value("max_sanity", 100.0f);
            p.last_sanity_day = static_cast<uint32_t>(pl.value("last_sanity_day", 0));
            p.health = pl.value("health", 100.0f);
            p.max_health = pl.value("max_health", 100.0f);
            p.death_count = static_cast<uint32_t>(pl.value("death_count", 0));
            p.last_safe_pos = {static_cast<int16_t>(pl.value("safe_x", p.pos.x)),
                               static_cast<int16_t>(pl.value("safe_y", p.pos.y))};
            for (auto& s : pl.value("secrets_found", json::array()))
                p.secrets_found.push_back(s.get<std::string>());
            for (auto& e : pl.value("night_event_log", json::array()))
                p.night_event_log.push_back(e.get<std::string>());
            // ROADMAP 1.4 — perception-filter / basement-procgen state.
            p.meta_break_fired = pl.value("meta_break_fired", false);
            p.basement_mark = pl.value("basement_mark", std::string());
            p.mark_days_left = pl.value("mark_days_left", 0);
            {
                json dcs = pl.value("dread_counters", json::array());
                for (uint8_t t = 0; t < 4; ++t) {
                    if (t < dcs.size() && dcs[t].is_number())
                        p.dread_counters[t] = static_cast<uint16_t>(dcs[t]);
                }
            }
            w.players[p.id] = p;
        }
        for (auto& cj : j.value("cells", json::array())) {
            int x = cj.value("x", -1), y = cj.value("y", -1);
            if (!w.in_bounds(x, y)) continue;
            Cell& c = w.at(x, y);
            c.tile = static_cast<Tile>(cj.value("tile", static_cast<int>(c.tile)));
            c.obj.type = static_cast<ObjType>(cj.value("obj", 0));
            c.obj.hp = static_cast<uint8_t>(cj.value("hp", 1));
            c.obj.ore = static_cast<uint8_t>(cj.value("ore", 0));
            c.obj.hp2 = static_cast<uint8_t>(cj.value("hp2", 0));
            c.obj.hp3 = static_cast<uint8_t>(cj.value("hp3", 0));
            c.snow_compaction = static_cast<uint8_t>(cj.value("snow_compaction", 0));
            c.forest_state = static_cast<uint8_t>(cj.value("forest_state", 0));
            c.track_age = static_cast<uint8_t>(cj.value("track_age", 0));
            c.track_type = static_cast<uint8_t>(cj.value("track_type", 0));
            c.track_dir = static_cast<int8_t>(cj.value("track_dir", -1));
            // ROADMAP 2.1/2.2 — Soil Chemistry + Water Table
            c.nitrogen = static_cast<uint8_t>(cj.value("nitrogen", 128));
            c.phosphorus = static_cast<uint8_t>(cj.value("phosphorus", 128));
            c.potassium = static_cast<uint8_t>(cj.value("potassium", 128));
            c.ph = static_cast<uint8_t>(cj.value("ph", 70));
            c.organic_matter = static_cast<uint8_t>(cj.value("organic_matter", 50));
            c.microbiome = static_cast<uint8_t>(cj.value("microbiome", 100));
            c.water_table_depth = static_cast<uint8_t>(cj.value("water_table_depth", 150));
            c.saturation = static_cast<uint8_t>(cj.value("saturation", 100));
            c.aquifer_transmissivity = static_cast<uint8_t>(cj.value("aquifer_transmissivity", 100));
            c.specific_yield = static_cast<uint8_t>(cj.value("specific_yield", 50));
            c.recharge_capacity = static_cast<uint8_t>(cj.value("recharge_capacity", 100));
            if (cj.contains("crop")) {
                c.crop.crop = static_cast<Item>(cj["crop"]);
                c.crop.stage = static_cast<uint8_t>(cj.value("stage", 0));
                c.crop.days_left = static_cast<int8_t>(cj.value("days_left", 0));
                c.crop.watered = cj.value("watered", false);
                c.crop.is_trellis = cj.value("is_trellis", false);
                c.crop.is_fruit_tree = cj.value("is_fruit_tree", false);
                c.crop.last_harvest_season = static_cast<int8_t>(cj.value("last_harvest_season", -1));
            }
        }
        // Deserialize building states
        if (j.contains("building_states")) {
            for (auto& [name, bs_json] : j["building_states"].items()) {
                BuildingState bs;
                bs.condition = static_cast<uint8_t>(bs_json.value("condition", 100));
                bs.roof_leak = static_cast<uint8_t>(bs_json.value("roof_leak", 0));
                bs.foundation = static_cast<uint8_t>(bs_json.value("foundation", 100));
                bs.last_maintained_day = static_cast<uint32_t>(bs_json.value("last_maintained_day", 0));
                w.building_states[name] = bs;
            }
        }
        // Deserialize plots (R16)
        if (j.contains("plots") && !w.plots.empty()) {
            size_t idx = 0;
            for (auto& pj : j["plots"]) {
                if (idx >= w.plots.size()) break;
                w.plots[idx].owner_id = static_cast<uint32_t>(pj.value("owner_id", 0));
                ++idx;
            }
        }
        w.basement_unlocked = j.value("basement_unlocked", false);
        w.basement_visits = j.value("basement_visits", 0u);
        w.horror_cycle = j.value("horror_cycle", 0u);
        w.active_horror = j.value("active_horror", std::string());
        w.last_night_event = j.value("last_night_event", std::string());
        // ROADMAP 1.2 — Valley Entity state.
        w.collective_guilt = j.value("collective_guilt", 0.0f);
        w.valley_awakening = j.value("valley_awakening", 0.0f);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "save load failed: " << e.what() << "\n";
        return false;
    }
}

// Phase 4: Chunk system implementations
void World::ensure_chunk_generated(int16_t cx, int16_t cy) {
    if (!get_chunk_const(cx, cy)) {
        get_chunk(cx, cy).generated = true;
    }
}

void World::generate_region(RegionType type, int16_t cx, int16_t cy, int16_t radius, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    for (int16_t ry = -radius; ry <= radius; ++ry) {
        for (int16_t rx = -radius; rx <= radius; ++rx) {
            int16_t chunk_cx = cx + rx;
            int16_t chunk_cy = cy + ry;
            if (std::abs(rx) + std::abs(ry) > radius) continue;
            Chunk& chunk = get_chunk(chunk_cx, chunk_cy);
            chunk.cx = chunk_cx;
            chunk.cy = chunk_cy;
            chunk.generated = true;
            
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    int gx = chunk_cx * CHUNK_SIZE + x;
                    int gy = chunk_cy * CHUNK_SIZE + y;
                    float v = noise(gx, gy, 0.35f) + noise(gx, gy, 0.9f) * 0.5f;
                    
                    switch (type) {
                        case RegionType::Forest:
                            chunk.at(x, y).tile = v > 0.55f ? Tile::GrassVar : Tile::Grass;
                            if (dist(rng) < 0.03f) chunk.at(x, y).obj = {ObjType::Tree, 0, 0};
                            if (dist(rng) < 0.01f) {
                                NPC npc;
                                npc.name = "Rabbit";
                                npc.kind = "rabbit";
                                npc.color = 2;
                                npc.pos = {static_cast<int16_t>(gx), static_cast<int16_t>(gy)};
                                chunk.npcs.push_back(std::move(npc));
                            }
                            break;
                        case RegionType::Hills:
                            chunk.at(x, y).tile = v > 0.6f ? Tile::Grass : Tile::Grass;
                            if (dist(rng) < 0.02f) chunk.at(x, y).obj = {ObjType::Rock, 0, 0};
                            break;
                        case RegionType::Mountains:
                            chunk.at(x, y).tile = (y < CHUNK_SIZE/3) ? Tile::Ice : Tile::Snow;
                            if (dist(rng) < 0.03f) chunk.at(x, y).obj = {ObjType::Rock, 0, 0};
                            break;
                        case RegionType::Caves:
                            chunk.at(x, y).tile = Tile::Dirt;
                            if (dist(rng) < 0.02f) chunk.at(x, y).obj = {ObjType::Rock, 0, 0};
                            break;
                        case RegionType::Ruins:
                            chunk.at(x, y).tile = v > 0.5f ? Tile::GrassVar : Tile::Dirt;
                            if (dist(rng) < 0.04f) chunk.at(x, y).obj = {ObjType::Statue, 0, 0};
                            break;
                        case RegionType::Swamp:
                            chunk.at(x, y).tile = dist(rng) < 0.4f ? Tile::Water : Tile::Grass;
                            break;
                        case RegionType::Ocean:
                            chunk.at(x, y).tile = Tile::Water;
                            break;
                        default:
                            chunk.at(x, y).tile = v > 0.62f ? Tile::GrassVar : Tile::Grass;
                            break;
                    }
                }
            }
        }
    }
}

bool World::parse_dsl(const std::string& dsl, std::vector<std::pair<std::string, Vec2>>& out_structs, std::string& error) {
    out_structs.clear();
    std::stringstream ss(dsl);
    std::string token;
    while (std::getline(ss, token, ';')) {
        // trim
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (token.empty()) continue;
        
        size_t at_pos = token.find('@');
        std::string name;
        int x = 0, y = 0;
        if (at_pos != std::string::npos) {
            name = token.substr(0, at_pos);
            name.erase(name.find_last_not_of(" \t") + 1);
            std::string coords = token.substr(at_pos + 1);
            std::stringstream cs(coords);
            std::string cx, cy;
            if (!std::getline(cs, cx, ',') || !std::getline(cs, cy)) {
                error = "Invalid coordinates in DSL: " + token;
                return false;
            }
            try {
                x = std::stoi(cx);
                y = std::stoi(cy);
            } catch (...) {
                error = "Invalid coordinate numbers: " + token;
                return false;
            }
        } else {
            std::stringstream ts(token);
            ts >> name >> x >> y;
            if (name.empty()) {
                error = "Missing structure name: " + token;
                return false;
            }
        }
        out_structs.emplace_back(name, Vec2{static_cast<int16_t>(x), static_cast<int16_t>(y)});
    }
    return true;
}

bool World::build_dsl_structure(Player& p, const std::string& struct_name, int gx, int gy, std::string& error) {
    // Check if on owned plot
    bool on_plot = false;
    size_t plot_idx = SIZE_MAX;
    for (size_t i = 0; i < plots.size(); ++i) {
        const Plot& pl = plots[i];
        if (pl.owner_id == p.id && gx >= pl.x && gx < pl.x + pl.w && gy >= pl.y && gy < pl.y + pl.h) {
            on_plot = true;
            plot_idx = i;
            break;
        }
    }
    if (!on_plot) {
        error = "You can only build on your own plot";
        return false;
    }
    
    // Check tile availability
    if (!in_bounds_global(gx, gy)) {
        error = "Invalid coordinates";
        return false;
    }
    const Cell& c = cell_at(gx, gy);
    if (c.obj.type != ObjType::None || c.tile == Tile::Water) {
        error = "Tile occupied or water";
        return false;
    }
    
    // Simple structure placement - just place a building marker
    Bldg bldg;
    bldg.name = struct_name;
    bldg.x = gx;
    bldg.y = gy;
    bldg.w = 4;
    bldg.h = 3;
    bldg.door_x = 1;
    bldg.door_y = 1;
    buildings.push_back(bldg);
    
    // Mark plot as having this structure
    p.placed_structs.push_back({plot_idx, 0, gx, gy});
    
    return true;
}

// Phase 5: Quest & Job System implementations
void World::generate_daily_quests(Player& p) {
    std::mt19937 rng(day * 1000 + p.id);
    std::uniform_int_distribution<int> dist(0, 4);
    std::uniform_int_distribution<int> count_dist(1, 10);
    std::uniform_int_distribution<int> money_dist(50, 500);
    
    // Clear expired quests
    active_quests.erase(std::remove_if(active_quests.begin(), active_quests.end(),
        [this](const Quest& q) { return q.expiry_day < day; }), active_quests.end());
    
    // Generate 2-3 new quests per day
    int num_quests = 2 + dist(rng) % 2;
    const char* quest_types[] = {"fetch", "deliver", "investigate", "ritual", "kill"};
    
    for (int i = 0; i < num_quests; ++i) {
        Quest q;
        q.id = "q" + std::to_string(next_quest_id++);
        q.type = quest_types[dist(rng)];
        q.expiry_day = day + 3; // 3 days to complete
        
        if (q.type == "fetch" || q.type == "deliver") {
            // Use seasonal crops/items
            int season = season_index(day);
            Item items[] = {Item::Parsnip, Item::Potato, Item::Cauliflower, Item::Corn, 
                           Item::Tomato, Item::Blueberry, Item::Melon, Item::Pumpkin,
                           Item::Egg, Item::Milk, Item::Honey, Item::Wood, Item::Stone};
            q.target_item = items[dist(rng) % 12];
            q.target_count = count_dist(rng);
            q.title = q.type == "fetch" ? "Gather " + std::to_string(q.target_count) + " " + item_def(q.target_item).name
                                       : "Deliver " + std::to_string(q.target_count) + " " + item_def(q.target_item).name;
            q.reward_money = money_dist(rng) * q.target_count / 2;
        } else if (q.type == "investigate") {
            q.title = "Investigate Strange Occurrence";
            q.target_location = "Ruins";
            q.reward_money = money_dist(rng);
        } else if (q.type == "ritual") {
            q.title = "Perform Ancient Ritual";
            q.target_location = "Shrine";
            q.reward_money = money_dist(rng) * 2;
        } else if (q.type == "kill") {
            q.title = "Clear Monster Nest";
            q.target_location = "Caves";
            q.reward_money = money_dist(rng) * 2;
        }
        
        q.description = q.title + ". Expires day " + std::to_string(q.expiry_day) + ".";
        active_quests.push_back(q);
    }
}

void World::update_market_prices() {
    // Initialize market prices if empty
    if (market_prices.empty()) {
        Item all_items[] = {Item::Parsnip, Item::Potato, Item::Cauliflower, Item::Corn, 
                           Item::Tomato, Item::Blueberry, Item::Melon, Item::Pumpkin,
                           Item::Egg, Item::Milk, Item::Honey, Item::Wood, Item::Stone,
                           Item::IronOre, Item::GoldOre, Item::CopperBar, Item::IronBar};
        for (Item it : all_items) {
            MarketPrice mp;
            mp.item = it;
            mp.base_price = item_def(it).sell > 0 ? item_def(it).sell : item_def(it).buy;
            mp.current_price = mp.base_price;
            mp.supply = 100;
            mp.demand = 50;
            mp.last_update = day;
            market_prices.push_back(mp);
        }
    }
    
    // Update prices based on supply/demand
    std::mt19937 rng(day * 777);
    // Phase 7.3: market volatility widens the random flux band (0 stable .. 1 chaotic)
    int flux_span = static_cast<int>(10.0f + economy_market_volatility * 40.0f);
    std::uniform_int_distribution<int> flux(-flux_span, flux_span);
    
    for (auto& mp : market_prices) {
        // Simulate supply changes based on season
        int season = season_index(day);
        bool in_season = false;
        // Simple season logic
        if ((season == 0 && (mp.item == Item::Parsnip || mp.item == Item::Potato || mp.item == Item::Cauliflower)) ||
            (season == 1 && (mp.item == Item::Tomato || mp.item == Item::Blueberry || mp.item == Item::Melon)) ||
            (season == 2 && (mp.item == Item::Pumpkin || mp.item == Item::Corn))) {
            in_season = true;
        }
        
        // Phase 7.3: demand_shift adaptation nudges demand per commodity.
        float demand_mult = 1.0f;
        if (economy_demand_shift.is_object()) {
            std::string name = item_def(mp.item).name;
            if (economy_demand_shift.contains(name) && economy_demand_shift[name].is_number()) {
                demand_mult = economy_demand_shift[name].get<float>();
            }
        }
        
        if (in_season) {
            mp.supply += 5 + flux(rng);
            mp.demand += static_cast<int>(flux(rng) * demand_mult);
        } else {
            mp.supply -= 2 + flux(rng);
            mp.demand += static_cast<int>(2 + flux(rng) * demand_mult);
        }
        
        mp.supply = std::max(1, mp.supply);
        mp.demand = std::max(1, mp.demand);
        
        // Price = base * (demand/supply) * 0.5..1.5
        float ratio = static_cast<float>(mp.demand) / mp.supply;
        // Phase 7.3: price_elasticity steepens the demand/supply response
        // (0.5 dampens swings, 2.0 amplifies them).
        if (economy_price_elasticity > 0.0f && std::abs(economy_price_elasticity - 1.0f) > 0.01f) {
            ratio = std::pow(ratio, economy_price_elasticity);
        }
        mp.current_price = static_cast<int>(mp.base_price * ratio * (0.8f + flux(rng) * 0.01f));
        mp.current_price = std::max(1, mp.current_price);
        mp.last_update = day;
    }
}

// Phase 7.3: Town Consciousness adaptation consumers.
// Copies the model's proposed adaptations into typed scalars the game reads.
// Missing keys keep their current (default) value so a partial JSON is safe.
void World::apply_adaptations(const json& adaptations) {
    if (adaptations.contains("weather") && adaptations["weather"].is_object()) {
        const json& w = adaptations["weather"];
        if (w.contains("pressure_bias")) weather_pressure_bias = w["pressure_bias"];
        if (w.contains("humidity_drift")) weather_humidity_drift = w["humidity_drift"];
        if (w.contains("storm_chance")) weather_storm_chance = w["storm_chance"];
        if (w.contains("fog_intensity")) weather_fog_intensity = w["fog_intensity"];
        if (w.contains("temperature_bias")) weather_temperature_bias = w["temperature_bias"];
    }
    if (adaptations.contains("economy") && adaptations["economy"].is_object()) {
        const json& e = adaptations["economy"];
        if (e.contains("price_elasticity")) economy_price_elasticity = e["price_elasticity"];
        if (e.contains("market_volatility")) economy_market_volatility = e["market_volatility"];
        if (e.contains("demand_shift") && e["demand_shift"].is_object()) economy_demand_shift = e["demand_shift"];
        if (e.contains("shop_price_mod") && e["shop_price_mod"].is_object()) economy_shop_price_mod = e["shop_price_mod"];
    }
    if (adaptations.contains("horror") && adaptations["horror"].is_object()) {
        const json& h = adaptations["horror"];
        if (h.contains("intensity")) horror_intensity = h["intensity"];
        if (h.contains("sanity_drain_multiplier")) horror_sanity_drain_multiplier = h["sanity_drain_multiplier"];
        if (h.contains("night_event_weight")) horror_night_event_weight = h["night_event_weight"];
        if (h.contains("phantom_sighting_chance")) horror_phantom_sighting_chance = h["phantom_sighting_chance"];
    }
    if (adaptations.contains("performance") && adaptations["performance"].is_object()) {
        const json& p = adaptations["performance"];
        if (p.contains("npc_decision_interval_ticks")) perf_npc_decision_interval_ticks = p["npc_decision_interval_ticks"];
        if (p.contains("weather_update_interval_ticks")) perf_weather_update_interval_ticks = p["weather_update_interval_ticks"];
    }
}

// Adaptation-aware daily weather. Starts from the deterministic base roll, then
// shifts storm/fog/rain probability using the town's consolidated weather
// adaptations. Deterministic per-day (seeded), so a saved game still reproduces.
int World::weather_of_day_adapted(uint32_t day) const {
    int base = ::weather_of_day(day);
    if (weather_storm_chance <= 0.0f && weather_pressure_bias == 0.0f &&
        weather_humidity_drift == 0.0f && weather_fog_intensity <= 0.0f) {
        return base;
    }
    int season = season_index(day);
    unsigned r = ((day * 2654435761u) >> 16);
    // Base thresholds by season (mirror the free function).
    int storm_thresh = (season == 2) ? 1 : 1;      // ~0.5-1% base
    int fog_thresh   = (season == 2) ? 3 : 0;      // fall fog
    int rain_thresh  = (season == 2) ? 5 : 40;     // ~20% rainy
    // Low pressure (-) => more storms; high (+) => clearer skies.
    storm_thresh += static_cast<int>(std::max(0.0f, -weather_pressure_bias) * 10);
    storm_thresh += static_cast<int>(weather_storm_chance * 100);
    fog_thresh   += static_cast<int>(weather_fog_intensity * 15);
    rain_thresh  += static_cast<int>(weather_humidity_drift * 30);
    rain_thresh  += static_cast<int>(std::max(0.0f, -weather_pressure_bias) * 15);
    unsigned m = (season == 2) ? (r % 100) : (r % 200);
    if (m < static_cast<unsigned>(storm_thresh)) return 3;
    if (fog_thresh > 0 && m < static_cast<unsigned>(fog_thresh)) return 2;
    if (m < static_cast<unsigned>(rain_thresh)) return 1;
    return 0;
}

void World::add_job_board_entries() {
    // Add 3-4 job postings
    job_board.clear();
    std::mt19937 rng(day * 333);
    std::uniform_int_distribution<int> money_dist(100, 800);
    
    const char* job_types[] = {"farmhand", "miner", "courier", "researcher"};
    const char* job_titles[] = {"Farm Work", "Mining Shift", "Delivery Run", "Research Assistant"};
    const char* job_descs[] = {
        "Help with planting/harvesting on local farms.",
        "Mine ore in the mountains.",
        "Deliver packages between towns.",
        "Assist with magical research at the shrine."
    };
    
    for (int i = 0; i < 4; ++i) {
        Job j;
        j.id = "j" + std::to_string(next_job_id++);
        j.type = job_types[i];
        j.title = job_titles[i];
        j.description = job_descs[i];
        j.reward_money = money_dist(rng);
        j.cooldown_until = day + 1; // Available daily
        job_board.push_back(j);
    }
}

bool World::complete_quest(Player& p, const std::string& quest_id) {
    for (auto& q : active_quests) {
        if (q.id == quest_id && !q.completed) {
            // Check requirements
            bool has_items = true;
            if (q.target_item != Item::None) {
                int count = 0;
                for (auto& slot : p.inv) {
                    if (slot.item == q.target_item) count += slot.count;
                }
                if (count < q.target_count) has_items = false;
            }
            
            if (!has_items) return false;
            
            // Consume items if fetch/deliver
            if (q.type == "fetch" || q.type == "deliver") {
                int remaining = q.target_count;
                for (auto& slot : p.inv) {
                    if (slot.item == q.target_item && remaining > 0) {
                        int take = std::min<int>(slot.count, remaining);
                        slot.count -= take;
                        remaining -= take;
                    }
                }
            }
            
            // Give rewards
            p.money += q.reward_money;
            if (q.reward_item != Item::None) {
                add_item(p, q.reward_item, q.reward_count);
            }
            
            q.completed = true;
            q.claimed = true;
            completed_quests.push_back(q);
            active_quests.erase(std::remove_if(active_quests.begin(), active_quests.end(),
                [&](const Quest& qq) { return qq.id == q.id; }), active_quests.end());
            return true;
        }
    }
    return false;
}

bool World::start_job(Player& p, const std::string& job_id) {
    for (auto& j : job_board) {
        if (j.id == job_id && j.cooldown_until <= day) {
            // Job started - rewards given immediately for simplicity
            p.money += j.reward_money;
            if (j.reward_item != Item::None) {
                add_item(p, j.reward_item, j.reward_count);
            }
            j.cooldown_until = day + 1; // Next day
            return true;
        }
    }
    return false;
}

void World::generate_daily_quests_if_needed(Player& p) {
    // Check if quests were generated today
    bool has_today_quests = false;
    for (auto& q : active_quests) {
        if (q.expiry_day <= day + 3 && q.expiry_day >= day) {
            has_today_quests = true;
            break;
        }
    }
    if (!has_today_quests) {
        generate_daily_quests(p);
    }
}
// ============================================================
// Phase 6: Horror & Narrative Overlays
// ============================================================

// Perception tier from sanity:
//   >= 75 sane; 50..75 uneasy; 25..50 strained; <25 fractured.
int World::perception_tier(const Player& p) const {
    float s = p.sanity;
    if (s >= 75.0f) return 0;
    if (s >= 50.0f) return 1;
    if (s >= 25.0f) return 2;
    return 3;
}

void World::tick_sanity(Player& p) {
    if (p.last_sanity_day == day) return;
    p.last_sanity_day = day;
    int hour = hour_of_day(*this);
    // Being awake past midnight drains sanity.
    float drain = 0.0f;
    if (hour >= 24) drain += 4.0f;
    else if (hour >= 22) drain += 1.5f;
    // Rainy, overcast days are harder on the mind.
    if (weather_of_day_adapted(day) == 1) drain += 0.5f;
    // In the basement, the air itself weighs on you.
    if (p.inside == "Basement") drain += 6.0f;
    // Phase 7.3: horror adaptations scale the drain and add ambient dread at night.
    drain *= horror_sanity_drain_multiplier;
    if (hour >= 24 && horror_intensity > 0.0f) drain += 2.0f * horror_intensity;
    // A gentle natural recovery during the day when calm and outside.
    float recover = (hour >= 8 && hour < 18) ? 2.0f : 0.0f;
    p.sanity = std::clamp(p.sanity - drain + recover, 0.0f, p.max_sanity);
}

void World::restore_sanity(Player& p, float amt) {
    p.sanity = std::clamp(p.sanity + amt, 0.0f, p.max_sanity);
    p.last_sanity_day = day;
}

void World::damage_sanity(Player& p, float amt) {
    p.sanity = std::clamp(p.sanity - amt, 0.0f, p.max_sanity);
    p.last_sanity_day = day;
}

void World::damage_health(Player& p, float amt) {
    p.health = std::clamp(p.health - amt, 0.0f, p.max_health);
}

bool World::is_dead(const Player& p) const {
    return p.health <= 0.0f || p.sanity <= 0.0f;
}

// P2 recurring-runs model (ROADMAP 1.3): death = penalties, not a full reset.
// World state, NPC memories/relationships, player knowledge, stored items, and
// building progress all persist. On death: HP -> full, position -> last safe point,
// sanity -> reduced (not zero), temp buffs cleared. Each death is a narratively
// remembered "loop". Returns a multi-line narration for the death.
std::string World::handle_death(Player& p) {
    ++p.death_count;
    bool broke_mind = (p.sanity <= 0.0f);
    // 1. HP -> full.
    p.health = p.max_health;
    // 2. Sanity -> reduced, not zero (start of the new loop, carrying a scar).
    p.sanity = std::max(1.0f, p.max_sanity * 0.4f);
    p.last_sanity_day = day;
    // 3. Position -> last safe point (farmhouse door default).
    if (p.inside != "Farmhouse") p.inside.clear();
    Vec2 safe = p.last_safe_pos;
    if (safe.x == 0 && safe.y == 0) safe = door();
    p.pos = safe;
    p.target = safe;
    p.inside_exit = safe;
    p.path.clear();
    p.moving = false;
    p.train_used = false;
    // 4. Night-event log: a "loop" chapter is recorded (persists across cycles).
    std::string ev = broke_mind ? "chapter:The Loop Ends in the Dark"
                                : "chapter:The Body Gives Out";
    if (p.night_event_log.size() > 12) p.night_event_log.erase(p.night_event_log.begin());
    p.night_event_log.push_back(ev + "\n(death " + std::to_string(p.death_count) + ")");

    // ROADMAP 1.2: a death feeds the Valley's collective guilt and escalates it
    // per cycle — the Valley remembers each loop and stirs a little darker.
    add_guilt(0.15f);
    collective_guilt = std::clamp(
        collective_guilt + 0.03f * static_cast<float>(horror_cycle), 0.0f, 1.0f);

    std::string out = broke_mind
        ? "Your mind folds inward and the world goes white. Somewhere the Valley sighs—\n"
          "it has seen this before. It will see it again."
        : "Darkness folds around you. Your last thought is of the farmhouse door.\n"
          "The Valley holds you, and lets you go.";
    out += "\nYou wake at the door of the farmhouse, breathless. You have died " +
           std::to_string(p.death_count) + (p.death_count == 1 ? " time." : " times.");
    return out;
}

// Deterministic chapter-style night event. Uses world state (season, day,
// economy mood) to pick a scripted scenario.
std::string World::roll_night_event() {
    const char* names[] = {
        "The Hollow Well", "The Clock Stops", "The Stranger at the Door",
        "The Lost Letter", "The Bleeding Moon", "The Other Valley",
        "The House That Breathes", "The Day That Repeated",
    };
    std::mt19937 rng(day * 7919u + 17u);
    const char* ev = names[rng() % 8];
    int season = season_index(day);
    const char* season_str = season_name(season);
    std::string out = "chapter:" + std::string(ev);
    out += "\n(" + std::string(season_str) + ", Day " + std::to_string(day) + ")";
    if (weather_of_day(day) == 1) out += "\nThe rain does not touch you. You are not sure it is rain.";
    else out += "\nThe moon hangs too low. You count the windows twice and get different numbers.";
    out += "\nSomewhere in the house, a door you never opened stands ajar.";
    active_horror = ev;
    last_night_event = out;
    // ROADMAP 1.2: a horror night event feeds the Valley a little guilt.
    add_guilt(0.03f);
    return out;
}

void World::trigger_basement(Player& p) {
    if (p.inside == "Basement") return;
    basement_unlocked = true;
    ++basement_visits;
    damage_sanity(p, 10.0f);
    // Phase 6: the deeper the cycle, the more the basement's air taxes the body.
    damage_health(p, 12.0f + 8.0f * static_cast<float>(horror_cycle));
    p.inside = "Basement";
    p.inside_exit = p.pos;
    auto rit = interiors.find("Basement");
    if (rit != interiors.end()) {
        const InteriorRoom& r = rit->second;
        p.inx = static_cast<int16_t>(r.w / 2);
        p.iny = static_cast<int16_t>(r.h - 2);
    } else {
        p.inx = 5; p.iny = 7;
    }
    // Higurashi-style: each visit into the hidden space turns the cycle.
    ++horror_cycle;
    // ROADMAP 1.2: descending into the Valley's heart feeds its guilt and stirs
    // it toward awakening; each new cycle draws a darker baseline.
    add_guilt(0.05f);
    collective_guilt = std::clamp(
        collective_guilt + 0.04f * static_cast<float>(horror_cycle), 0.0f, 1.0f);
}

void World::leave_basement(Player& p) {
    p.inside.clear();
    p.pos = p.inside_exit;
    p.target = p.pos;
    p.path.clear();
    p.moving = false;
}

std::string World::horror_flavor(const Player& p) const {
    int tier = perception_tier(p);
    if (tier <= 1) return "";
    const char* pool[] = {
        "The shadows at the edge of the field are watching.",
        "You could swear the scarecrow moved since you last blinked.",
        "A window in the house is dark, then lit, then dark again.",
        "The trees are closer than they were this morning.",
        "Somewhere, softly, a music box is playing.",
        "The sky flickers for a heartbeat, like a bad signal.",
    };
    std::mt19937 rng(p.id * 104729u + day);
    return pool[rng() % 6];
}

std::string World::internal_voice(const Player& p) const {
    int tier = perception_tier(p);
    if (tier <= 2) return "";
    const char* pool[] = {
        "[INTERNAL] They're all pretending. The grass, the sun, the birds. You've seen this before.",
        "[INTERNAL] Don't say it. Whatever you're about to do, don't say it.",
        "[INTERNAL] You woke up this morning and felt the world reload. Same day. Again.",
        "[INTERNAL] There is a version of you that stayed in bed. You are not that version.",
        "[INTERNAL] The fourth wall is thin here. Something is reading over your shoulder.",
    };
    std::mt19937 rng(p.id * 32411u + day);
    return pool[rng() % 5];
}

void World::find_secret(Player& p, const std::string& secret) {
    if (std::find(p.secrets_found.begin(), p.secrets_found.end(), secret) != p.secrets_found.end())
        return;
    p.secrets_found.push_back(secret);
    restore_sanity(p, 8.0f);   // understanding restores a little clarity
    // ROADMAP 1.2: uncovering a hidden truth feeds the Valley's collective guilt.
    add_guilt(0.05f);
    // ROADMAP 1.4: secrets tilt the dread profile toward whispers/ritual.
    bump_dread(p, 2);
}

// ----------------------------------------------------------------------
// ROADMAP 1.4 — sanity/perception filters + basement procedural horror.
// Continuous, tier-gated filters (distorted dialogue, hallucinated scene
// text, false UI, meta-narrative breaks) + per-cycle basement procgen with
// a persistent surface "mark". A lightweight per-player dread profile biases
// filter content toward what unsettles THIS player most (deterministic; the
// LLM-driven adaptation is the deferred ROADMAP 1.5 work).
// ----------------------------------------------------------------------

namespace {

// Unsettling synonym table for word-swap at tier>=2. Case-insensitive
// whole-word replacement; deterministic per call via the provided RNG.
struct WordSwap { const char* from; const char* to; };
const WordSwap word_swaps[] = {
    {"happy", "hungry"},   {"friend", "witness"},   {"hello", "we've been waiting"},
    {"good", "wrong"},     {"nice", "hollow"},      {"fine", "barely"},
    {"love", "need"},      {"hope", "dread"},       {"smile", "stare"},
    {"warm", "cold"},      {"bright", "flickering"},{"safe", "watched"},
    {"home", "trap"},      {"welcome", "expected"}, {"glad", "tired"},
    {"today", "again"},    {"morning","same"},      {"lovely", "uncertain"},
};

// Replace up to `max_swaps` whole-word matches (case-insensitive) using `rng`
// to pick which matches to swap. Returns the distorted string.
std::string word_swap(std::string s, std::mt19937& rng, int max_swaps) {
    int swapped = 0;
    for (const auto& ws : word_swaps) {
        if (swapped >= max_swaps) break;
        std::string needle = ws.from;
        // case-insensitive find
        std::string hay = s;
        std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
        size_t pos = hay.find(needle);
        if (pos == std::string::npos) continue;
        // Only swap whole words: check boundaries.
        bool left_ok = (pos == 0) || !isalnum(static_cast<unsigned char>(s[pos - 1]));
        bool right_ok = (pos + needle.size() >= s.size()) ||
                        !isalnum(static_cast<unsigned char>(s[pos + needle.size()]));
        if (!left_ok || !right_ok) continue;
        // 50% chance per candidate (deterministic via rng) to avoid over-swapping.
        if ((rng() % 100u) >= 50u) continue;
        s.replace(pos, needle.size(), ws.to);
        ++swapped;
    }
    return s;
}

}  // namespace

std::string World::distort_dialogue(const Player& p, const std::string& npc_name,
                                    const std::string& line) const {
    int tier = perception_tier(p);
    if (tier <= 0) return line;

    // Deterministic per (player, day, npc) so distortion is stable within a day.
    std::mt19937 rng(p.id * 8191u + day * 31u +
                     static_cast<unsigned>(std::hash<std::string>{}(npc_name)));

    std::string out = line;

    // Whispered underlayer (tier >= 1): sometimes prefix a barely-caught whisper.
    if (tier >= 1 && (rng() % 100u) < 35u) {
        const char* whispers[] = {
            "[she mouths something you can't quite read]",
            "[there's a second voice under his, just out of reach]",
            "[you catch a word that wasn't spoken: 'again']",
            "[her lips move a beat too long for the words you heard]",
        };
        out = std::string(whispers[rng() % 4]) + " " + out;
    }

    // Word-swap (tier >= 2): rewrite unsettling synonyms.
    if (tier >= 2) {
        out = word_swap(std::move(out), rng, tier >= 3 ? 4 : 2);
    }

    // Hallucinated extra clause (tier >= 3): append a line the NPC
    // didn't say, biased by the player's dread theme.
    if (tier >= 3 && (rng() % 100u) < 45u) {
        uint8_t theme = dread_bias(p);
        const char* extra[][3] = {
            {"'You came back.'",                       "'It's here, isn't it. Below.'",     "'I saw you, last time. You didn't see me.'"},
            {"'Your hands are so cold.'",              "'The floor remembers you.'",         "'You smell like the cellar.'"},
            {"'Say it again. The part about the door.'","'The third one is still down there.'","'Don't. I already know what you'll tell me.'"},
            {"'You've been standing too long in one place.'", "'The ground doesn't want you here.'", "'You look ill. The soil looks ill.'"},
        };
        out += " " + std::string(extra[theme % 4][rng() % 3]);
    }

    return out;
}

std::string World::hallucinate_scene(const Player& p) const {
    int tier = perception_tier(p);
    if (tier < 2) return "";
    // Probability rises with tier and with the Valley's awakening.
    unsigned chance = (tier >= 3) ? 35u : 18u;
    unsigned awake_pct = static_cast<unsigned>(valley_awakening * 100.0f);
    chance = std::min(80u, chance + awake_pct / 3u);
    // Corruption at the player's tile amplifies local hallucinations.
    unsigned cor = at(p.pos).corruption;
    chance = std::min(80u, chance + cor / 8u);

    std::mt19937 rng(p.id * 54059u + day * 7919u);
    if ((rng() % 100u) >= chance) return "";

    uint8_t theme = dread_bias(p);
    const char* scenes[][4] = {
        {"a figure standing at the well, drawing water (?)",
         "the scarecrow's head turned toward you, just for a moment (?)",
         "a child you don't recognize wave to you from the treeline (?)",
         "a dark window lit from within by no lamp (?)"},                              // shadows
        {"frost creeping across the ground toward your boots (?)",
         "your breath hangs too long, not dispersing (?)",
         "the path behind you is iced over, though it was clear a moment ago (?)",
         "a thin skin of ice forms on the puddles as you pass (?)"},                    // cold
        {"a low chanting, just below hearing, from under the hill (?)",
         "words scratched into the dirt that vanish when you look directly (?)",
         "the wind says a name. It isn't yours. It isn't anyone's (?)",
         "a circle of pressed grass where nothing has walked (?)"},                     // whispers
        {"the leaves on the trees here are curling black at the edges (?)",
         "a patch of earth has gone soft and grey, as if something leaked out (?)",
         "you see a dead bird, then another, then a row of them (?)",
         "the soil is damp and wrong-colored where nothing has been planted (?)"},    // rot
    };
    return scenes[theme % 4][rng() % 4];
}

std::string World::roll_meta_break(Player& p, bool once) {
    if (once) {
        if (p.meta_break_fired) return "";
        if (p.death_count < 2) return "";   // needs at least two loops to recognize a pattern
        p.meta_break_fired = true;
        // The one-shot line. Fires exactly once across the entire save.
        return "The screen holds on a frame too long. Somewhere behind it, "
               "something notices you noticing. THIS IS NOT YOUR FIRST TIME "
               "HERE. is it? You blink, and the farmhouse is just a farmhouse again.";
    }
    // Rare repeatable 4th-wall break inside horror anchors at tier >= 3.
    int tier = perception_tier(p);
    if (tier < 3) return "";
    std::mt19937 rng(p.id * 99991u + day * 13u + p.death_count * 7u);
    if ((rng() % 100u) >= 5u) return "";
    const char* breaks[] = {
        "The game hesitates, as if it noticed you watching.",
        "There is a frame here with no code for it. You feel the gap.",
        "For one breath, the world is a held breath — and it's holding yours.",
        "Something is reading the same line of text you are, right now.",
    };
    return breaks[rng() % 4];
}

std::string World::roll_basement_procgen(Player& p) {
    // Seed by (horror_cycle, basement_visits) so each descent is a new room.
    std::mt19937 rng(horror_cycle * 6151u + basement_visits * 131u + 1u);

    // Hazard type also determines the mark carried to the surface.
    // 0 = cold, 1 = oily, 2 = whispering. Weighted by dread bias: the player's
    // feared theme is more likely to manifest in the basement.
    uint8_t bias = dread_bias(p);
    unsigned roll = static_cast<unsigned>(rng() % 100u);
    uint8_t hazard;
    if (bias == 1)      hazard = (roll < 45) ? 0 : (roll < 75) ? 1 : 2;
    else if (bias == 2) hazard = (roll < 25) ? 0 : (roll < 50) ? 1 : 2;
    else if (bias == 3) hazard = (roll < 30) ? 0 : (roll < 70) ? 1 : 2;
    else                hazard = (roll < 40) ? 0 : (roll < 65) ? 1 : 2;

    const char* room_names[] = {"The Cold Cellar", "The Slick Stair", "The Whispering Hall"};
    const char* room_desc[] = {
        "The air drops to a bitter, cellar cold. Your breath fogs. The walls weep.",
        "The floor is slick with something darker than water. Each step sticks, then releases.",
        "The corridor is silent, then not. Voices rasp from nowhere, just under hearing.",
    };
    // sanity/HP drain scaling with cycle is already applied in trigger_basement;
    // the mark adds a persistent surface consequence.
    p.basement_mark = (hazard == 0) ? "cold" : (hazard == 1) ? "oily" : "whispering";
    p.mark_days_left = 3 + static_cast<int>(horror_cycle);   // longer with deeper cycles

    std::string out = room_names[hazard];
    out += "\n" + std::string(room_desc[hazard]);
    out += "\nSomething of this place will follow you up. (" + p.basement_mark + " mark, " +
           std::to_string(p.mark_days_left) + " days)";
    // Bump the dread profile toward the encountered hazard's theme.
    bump_dread(p, hazard == 0 ? 1 : (hazard == 1 ? 3 : 2));
    return out;
}

void World::tick_basement_mark(Player& p) {
    if (p.mark_days_left <= 0 || p.basement_mark.empty()) return;
    // Small daily sanity malus while the mark lingers.
    float malus = 1.5f;
    // Whispers are worse at night.
    int hour = hour_of_day(*this);
    if (p.basement_mark == "whispering" && hour >= 22) malus += 1.5f;
    damage_sanity(p, malus);
    --p.mark_days_left;
    if (p.mark_days_left <= 0) p.basement_mark.clear();
}

void World::bump_dread(Player& p, uint8_t theme) {
    if (theme >= 4) return;
    // Saturate at 255 so one repeated encounter can't dominate forever.
    if (p.dread_counters[theme] < 65535) ++p.dread_counters[theme];
}

uint8_t World::dread_bias(const Player& p) const {
    uint16_t max_v = 0;
    uint8_t max_i = 0;
    uint16_t total = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t v = p.dread_counters[i];
        total += v;
        if (v > max_v) { max_v = v; max_i = i; }
    }
    if (total == 0) {
        // No encounters yet — return a deterministic default per player.
        std::mt19937 rng(p.id * 2654435761u + day);
        return static_cast<uint8_t>(rng() % 4);
    }
    // Weighted random among themes, but bias toward the dominant one:
    // 60% chance to pick the max, 40% weighted by counters.
    std::mt19937 rng(p.id * 40503u + day * 3607u);
    if ((rng() % 100u) < 60u) return max_i;
    std::uniform_int_distribution<unsigned> dist(0, total - 1);
    unsigned pick = dist(rng);
    uint16_t acc = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        acc += p.dread_counters[i];
        if (pick < acc) return i;
    }
    return max_i;
}

// ----------------------------------------------------------------------
// ROADMAP 1.2 — Valley Entity mechanics (collective guilt -> corruption ->
// horror intensity). The Valley itself is a genius loci whose state emerges
// from accumulated guilt (deaths, secrets, horror events) and a spatial
// corruption field (cellular automaton seeded at the 4 horror anchors).
// tick_valley() is the daily heartbeat; ValleyMind pushes awakening back
// into the horror_* adaptation scalars each consolidation.
// ----------------------------------------------------------------------

void World::add_guilt(float amt) {
    collective_guilt = std::clamp(collective_guilt + amt, 0.0f, 1.0f);
}

float World::corruption_density() const {
    // Downsampled average over the core authored map (stride 4). 0=clean, 1=full.
    long sum = 0;
    int cnt = 0;
    for (int y = 0; y < MAP_H; y += 4) {
        for (int x = 0; x < MAP_W; x += 4) {
            sum += at(x, y).corruption;
            ++cnt;
        }
    }
    return cnt > 0 ? static_cast<float>(sum) / static_cast<float>(cnt * 255) : 0.0f;
}

void World::tick_valley() {
    // 1) Slow daily decay of collective guilt — the Valley forgets a little,
    //    so sustained atrocity is required to keep it awake.
    collective_guilt = std::max(0.0f, collective_guilt - 0.02f);

    // 2) Awakening rises from guilt and the spread of corruption together.
    float density = corruption_density();
    valley_awakening = std::clamp(0.55f * collective_guilt + 0.45f * density, 0.0f, 1.0f);

    // 3) Reseed corruption at the horror anchors. Each anchor's corruption is
    //    pushed up to the awakening-driven floor (valley feeds the anchors).
    struct Anchor { int x; int y; float w; };
    static constexpr Anchor anchors[] = {
        {22, 25, 1.0f},   // Town Center / basement cellar (Civic anchor)
        {12, 52, 0.85f},  // Witch's Hut (Woodland)
        {100, 42, 1.0f},  // Abandoned Sanitarium (Horror)
        {20, 60, 0.9f},   // Ritual Circle (Forest border)
    };
    const int anchor_floor = static_cast<int>(valley_awakening * 255.0f);
    for (const Anchor& a : anchors) {
        if (!in_bounds(a.x, a.y)) continue;
        int floor_v = std::min(255, static_cast<int>(static_cast<float>(anchor_floor) * a.w));
        Cell& c = at(a.x, a.y);
        if (c.corruption < floor_v) c.corruption = static_cast<uint8_t>(floor_v);
    }

    // 4) Corruption cellular automaton: diffuse toward neighbor mean, gentle decay,
    //    anchors reseeded each pass. Double-buffered over the core map.
    static std::array<uint8_t, MAP_W * MAP_H> next_buf;
    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            int sum = 0;
            int cnt = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
                    sum += at(nx, ny).corruption;
                    ++cnt;
                }
            }
            int avg = cnt > 0 ? sum / cnt : 0;
            int self = at(x, y).corruption;
            // Diffuse 1/4 of the gap toward the neighbor mean, then decay by 1.
            int nv = self + (avg - self) / 4 - 1;
            if (nv < 0) nv = 0;
            if (nv > 255) nv = 255;
            next_buf[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)] =
                static_cast<uint8_t>(nv);
        }
    }
    // Commit, then re-apply anchor floors so seeds survive the decay.
    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            at(x, y).corruption =
                next_buf[static_cast<size_t>(y) * MAP_W + static_cast<size_t>(x)];
        }
    }
    for (const Anchor& a : anchors) {
        if (!in_bounds(a.x, a.y)) continue;
        int floor_v = std::min(255, static_cast<int>(static_cast<float>(anchor_floor) * a.w));
        Cell& c = at(a.x, a.y);
        if (c.corruption < floor_v) c.corruption = static_cast<uint8_t>(floor_v);
    }
}
