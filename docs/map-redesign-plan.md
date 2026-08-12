# Ashgrove Valley — Map Redesign Plan

Approved 2026-08-12. Geography first, town layered on top.

## Core Design Principles

### 1. Maximum Parallelism — Utilize All CPU Cores
**Every subsystem that can be parallelized MUST be parallelized.** The game server should scale across all available CPU cores:
- **World generation**: Parallel terrain passes (mountains, rivers, forests, ore) using thread pools
- **NPC scheduling**: Each NPC's pathfinding and decision-making runs on worker threads
- **Crop growth / weather simulation**: Per-cell updates distributed across threads
- **Save/load**: Async I/O with background serialization threads
- **Network handling**: Thread-per-connection or thread-pool for HTTP/WebSocket endpoints
- **Game loop**: Fixed-timestep simulation decoupled from network tick; systems run in parallel phases (movement → AI → environment → persistence)
- **Build system**: CMake with Ninja generator, `-j$(nproc)` by default
- **Profiling**: Continuous profiling (perf, VTune) to identify serialization bottlenecks

### 2. Simulation-First Realism — The Real Map Is the Mental Map
**The visual client (PIXI.js map) is merely a reference prompt. The TRUE game map lives in the server simulation and the player's imagination.**

- **MUD text is the primary interface** — every command produces rich, evocative prose that lets players *visualize* the world like reading a novel
- **Visual map is secondary** — a simplified minimap for orientation only; it does NOT replace textual description
- **Server simulation is authoritative and hyper-detailed**:
  - Every tile has: terrain, sub-terrain, moisture, temperature, soil quality, elevation, vegetation density, light level, sound propagation
  - Every building has: interior rooms with furniture placement, lighting, temperature, smell, sound, structural condition, ownership, history
  - Every road has: material (cobble/dirt/gravel/boardwalk), width, curvature, wear, drainage, lighting, traffic patterns
  - Every NPC has: detailed schedule, personality, memory, relationships, skills, inventory, home, workplace, favorite spots
  - Every crop has: variety, growth stage, water level, fertilizer, disease risk, pollination state, yield quality
  - Weather simulates: humidity, pressure, wind, precipitation, fog, temperature gradients across the valley
  - Time progresses: seasons, moon phases, festivals, aging, decay, growth cycles

### 3. Verbose, Imaginative MUD Commands
**Commands should read like prose from a novel, not terse computer output.**

Examples:
- `look` → *"You stand on packed cobblestone in Stardrop Plaza. The roundabout's central statue — a weathered stone figure of a farmer sowing seeds — watches over the radiating streets. Morning light catches the dew on the ornamental cherry trees ringing the plaza, their blossoms a soft pink against the grey stone. To the north, Mulberry Lane curves toward the north bridge, wagon ruts worn deep into the cobbles. The river murmurs beyond, cold and clear. Leah's easel is set up near the fountain, half-finished canvas catching the light."*

- `examine statue` → *"The statue stands three meters tall, carved from local grey granite veined with quartz. The farmer's face is worn smooth by decades of weather, features softened to an archetype: broad hat, rolled sleeves, bag of seed at hip. Moss clings in the folds of the carved cloak. A plaque at the base reads: 'To those who sow — the valley remembers.' Someone has tucked a fresh dandelion into the stone fingers."*

- `survey` → *"Stardrop Plaza (Civic District): 42×38 cobblestones. Central roundabout (7×7) with statue. Four radiating streets: north to North Bridge, south to Main Bridge, east to Commerce Row, west to Bus Stop. Buildings: Town Center (north face), Clinic (northwest), Museum (northeast), Old Mill (west), Stardrop Saloon (southeast). Cherry trees: 16 (spring blossom). Benches: 8. Fountain: 1 (running). Lighting: 12 gas lamps. NPCs present: Leah (painting), 2 townsfolk (walking)."*

### 4. Imagination-Driven Gameplay
**The player's mind renders a richer world than any GPU.** Design for mental visualization:
- Use sensory language: sight, sound, smell, touch, temperature, proprioception
- Describe spatial relationships precisely (distances, directions, landmarks)
- Convey atmosphere through environmental storytelling
- Let players "walk" the world through text, building cognitive maps
- Visual client shows only: player position, major landmarks, fog of war edges

### 5. Bug-First Development
**Fix flaws immediately when discovered.** No known bugs ship. Regression tests for every fix.

### 6. Documentation Standards
- **CHANGELOG.md**: Every commit summarized, user-facing changes highlighted
- **README.md**: Project overview, architecture, build/run instructions, design philosophy
- **Design docs**: Verbose, decision-logged, updated with each change
- **Code comments**: Explain *why*, not *what* (code shows what)

### 7. Parallelism Implementation Status (v0.6.1)

| Subsystem | Status | Approach |
|-----------|--------|----------|
| **Build** | ✅ Done | CMake + Ninja/Unix Makefiles, `-j$(nproc)`, LTO, native arch |
| **HTTP Server** | ✅ Done | `httplib` thread pool (one thread per connection) |
| **Game Loop** | 🟡 Partial | Two threads: autosave (60s) + simulation (16ms tick); mutex-protected world |
| **World Gen** | ⏳ Planned | `std::execution::par` for independent terrain passes (mountains, rivers, biomes, ore) |
| **NPC AI** | ⏳ Planned | Thread pool: each NPC's `schedule_slot` + `bfs_path` on workers |
| **Crops/Environment** | ⏳ Planned | Per-cell updates distributed; double-buffered world state |
| **Save/Load** | ⏳ Planned | Async JSON serialization on background thread |
| **Weather** | ⏳ Planned | Regional cells updated in parallel |

**Current bottleneck**: Single `g_mutex` protects entire `World` in game loop. Next step: fine-grained locking or double-buffered tick.

---

## Parallelism Roadmap (Post-v0.6)

### Phase P1 — Fine-Grained Locking (R5-adjacent)
- Split `World` mutex: `cells_mutex`, `players_mutex`, `npcs_mutex`, `buildings_mutex`
- Read-heavy ops (look, survey, map render) use shared_lock; writes use unique_lock
- Target: 2-4× throughput on 8+ cores for read-heavy workloads

### Phase P2 — Double-Buffered Tick
- Two `World` states: `front` (read by network/client) and `back` (written by simulation)
- Atomic pointer swap at tick boundary; zero-copy for readers
- Eliminates mutex contention for `/state` and `/cmd` reads

### Phase P3 — Parallel Systems (R7+)
- `std::execution::par_unseq` for: crop growth, weather diffusion, ore respawn, leaf litter
- NPC thread pool: `schedule_slot` + `bfs_path` per NPC concurrently
- World gen: parallel terrain passes (mountains || rivers || biomes || ore)

### Phase P4 — Async Persistence
- Background serialization thread with lock-free queue
- Incremental saves (dirty chunks only)
- Save compression (zstd) on worker thread

---

## Map size

## Map size
- **128 x 96** (was 96 x 64). Update `MAP_W`, `MAP_H` in `include/world.hpp`.
- Client in `assets/index.html` already expects `MAP_W=128, MAP_H=96` — so the C++ side catches up (the existing mismatch is finally resolved).

## Geography: Valley Bowl

```
              N edge: mountains (impassable-ish, rocky pass to glacier lake)
              glacier lake (frozen in winter) tucked in the NW
              snow/tundra line ~y=12 with wavy treeline
              mountain trail switchbacks up to glacier lake

   W forests         |          E moor
   Whisper Wood      |          East Moor (sparse pines + rocks)
   (dense old growth)|   river  (open grass + ore)
                     |
                     |    =====  north bridge =====
                     |
                     |    [TOWN WEST]   [TOWN EAST]
                     |     civic plz     commerce
                     |     (roundabout   (shops,
                     |      + statue)     market,
                     |                    blacksmith)
                     |    =====  main-street bridge =====
                     |           (cobble plaza section)
                     |
                     |    [WEST HOUSES]  [EAST HOUSES]
                     |     Birch Court     Maple Court
                     |
                     |    =====  south footbridge =====
                     |
                     |    farm (south outskirts, overgrown)
                     |      Farmhouse + Shed (no tilled plot)
                     |
                     |    lakefront (mountain lake to NE)
                     |    docks/boardwalk (southern sea)
                     |    Lighthouse at southern tip
                     |
              S edge: Southern Ocean (whole bottom 3 rows)
```

### Key geographic features
1. **Surrounding mountains** — North edge rises into mountain tiles (snow/rock steps) by y~6, with a wavy tundra snow line around y=12. Passable switchback trail hugs the NW corner to access a glacier lake (ice tile island).
2. **Main river** — N-S, meanders around x = 44 (center of 128-wide map). Wider in the south, tapering in the north. Crossed by:
   - **North bridge** at y ≈ 20 (2-tile wide cobblestone, ties civic district to commerce district)
   - **Main-street bridge** at y ≈ 34 (2-tile wide, central crossing, connects district plazas)
   - **South footbridge** at y ≈ 56 (1-tile narrow, near the farm)
3. **Tributary stream** — Joins the main river from the NW at roughly y=28. Narrower than main river (1-tile wide most of its length, widens slightly near the junction).
4. **Mountain lake** — North-east-ish, around (88, 14). Frozen in winter. Lakefront district on its south shore with Tearoom + small pier.
5. **Southern ocean** — Whole bottom 3 rows of the map (`y >= MAP_H - 3`).
6. **Whisper Wood** — Dense old-growth forest in the west (x ≈ 4-22, y ≈ 16-60). Pine + regular trees + bush understory. Clear corridors ensure passability.
7. **East Moor** — Open grassland with scattered pines and ore-bearing rocks (x ≈ 78-100, y ≈ 16-50).
8. **Farm** — South outskirts, west of the river. Overgrown grass field at start (player tills it themselves). Farmhouse + small Shed, no pre-tilled plot.
9. **Docks/boardwalk district** — Along south ocean shore, east of river mouth. Fish Shack + wooden boardwalk tiles.
10. **Lighthouse** — Tiny building at the southernmost tip of the east bank.

## Town: Two districts, river-split

### District 1 — Civic Plaza (west bank, north of center)
- Anchor: **Roundabout with a central statue/monument**
  - 3x3 round grass tile circle in the middle of the plaza
  - Statue in the center (a prominent decorative object, maybe a stylized ✨ or 🗿)
  - Cobblestone roads radiate out in 4 directions
- Buildings: Town Center, Clinic, Museum, Stardrop Saloon
- Roads: paved cobblestone (`Tile::Bridge` is reserved for water crossings — use a new `Tile::Cobble` or repurpose `Tile::Tilled`? **decision: add a `Tile::Cobble` enum value** so we can render a distinct plaza stone look).
- Plaza ringed with ornamental trees (seasonal cherry blossom in spring!)

### District 2 — Commerce Row (east bank, north of center)
- Anchor: **Open paved market square**
- Buildings: General Store, Market, Blacksmith, Carpenter Shop, Pet Shop
- Roads: cobblestone road through the center, dirt roads branching to buildings

### Residential — West houses (Birch Court)
- 3 small cottages: Willow House, Maple House, Rowan Cottage
- Tucked on the west bank, between civic plaza and the south footbridge
- Accessed by a dirt cul-de-sac

### Residential — East houses (Maple Court)
- 2 small cottages (add new: "Hawthorne Cottage", "Birch Cottage")
- East bank south of commerce district, before the farm

### District 3 — Lakefront (north, near mountain lake)
- Building: Tearoom, Observatory (Observatory up on the hill near the trail)
- Small pier jutting into the lake

### District 4 — Docks (south, along the sea)
- Building: Fish Shack, Lighthouse
- Boardwalk tiles leading out over the water
- Boat ramp + a few moored boats (decorative)

## Bridge crossings (3)
1. **North bridge** (y≈20): 2-tile wide cobblestone. Connects civic district to commerce district via an L-shaped main street.
2. **Main-street bridge** (y≈34): 2-tile wide cobblestone. The most-traveled crossing, between the roundabout area and the commerce square.
3. **South footbridge** (y≈56): 1-tile narrow. A rickety plank bridge connecting the farm-side path to the east bank residential area.

## Buildings — full list (22 buildings)

### Existing 16 — kept:
1. **Farmhouse** — south outskirts, west bank (the house_tl structure)
2. **Blacksmith** — commerce row, east bank
3. **General Store** — commerce row
4. **Old Mill** — civic plaza district (west bank, decorative)
5. **Clinic** — civic plaza district
6. **Museum** — civic plaza district
7. **Town Center** — anchor of civic plaza district, facing the roundabout
8. **Stardrop Saloon** — civic plaza district (corner of cobblestone plaza)
9. **Market** — commerce row (facing the market square)
10. **Willow House** — Birch Court (west residential)
11. **Maple House** — Birch Court (west residential)
12. **Rowan Cottage** — Maple Court (east residential)
13. **Bus Stop** — west edge near civic plaza (the entry point)
14. **Railway Station** — north edge near mountain trail
15. **Hawthorn Barn** — moved: now on the FARM (south outskirts)
16. **Glasshouse** — moved: now on the FARM (south outskirts)

### New 6 — added per user vote:
17. **Fish Shack** — docks district (south)
18. **Lighthouse** — southern tip (docks district)
19. **Carpenter Shop** — commerce row (next to Blacksmith)
20. **Tearoom** — lakefront (near mountain lake)
21. **Observatory** — on the hill near the mountain trail
22. **Pet Shop** — commerce row

## Roads
- **Plaza + main street**: Cobblestone (`Tile::Cobble` — needs new tile enum + render color)
- **All other roads**: 2-tile-wide Dirt, gently curving
- **Farm lanes + cul-de-sacs**: 1-tile dirt paths

## Seasonal visual swaps
Swap tree objects tile-by-tile based on `season_index(day)`:
- **Spring**: cherry-blossom trees (🌸 variant) ring the civic plaza
- **Summer**: full green canopy (🌳 / 🌲) everywhere
- **Fall**: autumn-colored trees (🍂 variant) near the farm and along main street
- **Winter**: snow blankets all town tiles (everything north of snow line + extending south for the season), river tiles freeze to `Tile::Ice`

Implementation: each tree cell stores its "base type" (Tree/Pine), and the renderer chooses the emoji glyph by `season_index(day)` + a region tag (town / wild / farm / lakeside).

## Regions (named via `region_at()`)
Update `region_at` in `src/world.cpp` to recognize:
- Frostveil Tundra (north snow)
- Frozen Lake (glacier lake)
- Whisper Wood (west forest)
- East Moor (east open land)
- Mulberry Lane (main commerce street)
- Stardrop Plaza (civic roundabout + plaza ring)
- Birch Court (west residential)
- Maple Court (east residential)
- Mirror Lake → renamed **Lake Aurora** (mountain lake in NE)
- Seaglass Shore (southern ocean coast)
- Ashgrove Farm (south outskirts)
- The Docks (boardwalk/dock area)
- Mountain Trail (the switchback path up to glacier)
- Tannery Reach (optional, for tributary stream)

## NPC schedule updates
The 5 NPCs get new anchors reflecting the new district layout:
- **Leah** — Stardrop Plaza (civic district center)
- **Abigail** — farm gate (south outskirts)
- **Elliot** — main-street bridge (writing perch)
- **Robin** — Carpenter Shop (commerce row)
- **Evelyn** — Tearoom (lakefront) days, Docks at evening (for sunset walks)

Re-add/build their schedule slots with new coordinates.

## New tile enum value
Add `Tile::Cobble = 13` to `include/world.hpp`. Add render rule for it in `assets/index.html` (grey-tan cobblestone color + subtle texture). Replace all uses of `Tile::Dirt` in the plaza + main-street with `Tile::Cobble`.

## Implementation phasing

### Phase 1 — Geography rebuild (src/world.cpp `generate_world`)
1. Update `MAP_W = 128, MAP_H = 96` in `include/world.hpp`.
2. Rewrite terrain generation: valley bowl mountains (N), main river (x≈44, N-S), tributary stream (NW join at y≈28), mountain lake (NE), southern ocean.
3. Add `Tile::Cobble` enum value.
4. New `resolve_water_edges` works the same.

### Phase 2 — Town buildout (`place_buildings`, roads, districts)
1. Rewrite `place_buildings` with all 22 buildings at new coordinates.
2. Add cobblestone plaza + roundabout with statue object.
3. Add 3 bridges explicitly (north, main, south footbridge).
4. Add cobblestone main streets and dirt side roads.
5. Add docks boardwalk near southern ocean.
6. Add lakefront pier near mountain lake.

### Phase 3 — Wilderness
1. Whisper Wood on the west (x 4-22).
2. East Moor on the east (x 78-100).
3. Mountain trail + glacier lake.
4. Ore deposits relocated to mountain trails/moor.

### Phase 4 — NPC schedules + regions
1. Update `init_npcs` and `npc_schedule` with new anchor coords.
2. Update `region_at` with new named regions.

### Phase 5 — Client rendering (`assets/index.html`)
1. Add `Tile::Cobble` case in `getTileTex` with grey-tan gradient.
2. Add seasonal tree-glyph lookup function.
3. Add new emoji glyphs for new buildings (Fish Shack 🎣, Lighthouse 🗼, Carpenter Shop 🔨, Tearoom 🍵, Observatory 🔭, Pet Shop 🐕).
4. Add statue object glyph (🗿) at the roundabout.

### Phase 6 — Interiors (`init_interiors`)
1. Add interior rooms for the 6 new buildings (Fish Shack, Lighthouse, Carpenter Shop, Tearoom, Observatory, Pet Shop).
2. Shed interior (small farm outbuilding).

### Phase 7 — Map size + client rebuild
1. Update client `MAP_W=128, MAP_H=96` (already in client per state notes — verify).
2. Recompile server, test save/load round trip.

## Visual layout sketch (ASCII, with districts)

```
        y=0  ▒▒▒▒▒▒▒▒▒▒▒▒ Mountains ▒▒▒▒▒▒▒▒▒▒▒▒
                 ▒ Glacier Lake (ice) ▒         Observatory
                  ▒ ▒ (mountain trail zig-zag up) ▒
        y=12  snow line w/ pines
        y=20  ==== North Bridge ====                           Lake Aurora
              [CIVIC WEST]  |  [COMMERCE EAST]
               (Clinic)(Museum) | (Blacksmith)(Carpenter)
               (Old Mill)(Saloon)| (General Store)(Market)
                                     (Pet Shop)
        y=34  ==== Main-Street Bridge ====  ← roundabout plaza
                   Town Center
                   ★ statue  ← cobblestone ring
        y=40  ┌─ Birch Court (west) ─┐  ┌─ Maple Court (east) ─┐
              │ Willow │ Maple │     │  │ Rowan │ Hawthorne    │
        y=56  ==== South Footbridge ====
              Farm: Farmhouse + Shed + Hawthorn Barn + Glasshouse
              (overgrown field, till-yourself)
        y=68  Docks district
              Fish Shack · Lighthouse at tip · boardwalk
        y=93  ▓ ▓ ▓ Southern Ocean ▓ ▓ ▓
```

## Confirmation checklist
- [ ] User confirms plan above matches their intent
- [ ] Proceed phase by phase, getting user feedback at each phase boundary
- [ ] Don't try to write all 7 phases in one go; verify compile + browser render after phase 2

---

# Addendum — Expanded Vision (approved 2026-08-12)

The plan above covers the *geography + town layout*. The sections below cover the broader game-design vision the user laid out. These are bigger-than-a-single-PR features; the roadmap at the bottom of this doc sequences them and the agent will auto-execute.

## A1. Buildings are enterable, weather, and require maintenance

**All buildings are enterable by both NPCs and the player.** NPCs follow their daily schedules *into* buildings (e.g., Robin walks into the Carpenter Shop at 9 AM and is visible inside at her workbench). The player sees them as a furniture-blocked NPC tile within the interior grid, and can `talk`/`gift` as usual.

**Maintenance & weathering** — every building has a `BuildingState` struct (added to `world.hpp`):
```
struct BuildingState {
    uint8_t condition = 100;     // 0..100, decays over time
    uint8_t roof_leak = 0;        // rain damage accrued
    uint8_t foundation = 100;     // winter frost damage
    uint8_t last_maintained_day = 0;
};
std::unordered_map<std::string, BuildingState> bldg_state;
```

- **Rainy days**: outdoor buildings (Barn, Glasshouse, Lighthouse, Docks Fish Shack) take `roof_leak += 1` per rainy day. indoors buildings take less.
- **Winter**: every building loses `foundation` slowly; the Farmhouse loses fastest (player lives there). Forest cabin types lose slower (insulated by trees).
- **Storms** (a new severe weather variant): random event in summer/fall that adds 5-10 damage to a random outdoor building.
- **Repair**: player buys wood + stone from Carpenter Shop + uses `repair <building>` command. Cost scales with damage. Fully repaired = 100/100.
- **Visual feedback**: render severity tiers — `condition < 30` shows 🏚️ (ruined) emoji overlay; `30-60` shows 🏚 (damaged); `60-85` minor cracks; `85-100` fine.
- **NPC schedule disruption**: if a building's condition < 20 its owner-NPC stays home (path-finding refuses to path inside). Adds emergent story.

**First ship**: this degrades gracefully on the Farmhouse only in this first PR. Other buildings get the state struct but don't decay yet, so we don't break schedules. Decay hooks wire up per-district in a follow-up PR.

## A2. Interiors are well-designed and functional; minimap swaps to interior view

Every building's interior (`InteriorRoom`) is hand-designed to be **functional**, not a stub. Each has:
- A coherent floor layout (walls separating rooms where appropriate, door at the south)
- Furniture placed for a purpose (e.g., Blacksmith = anvil + forge + tool rack + storage chest + Clint's bed)
- **Interactive furniture** — every non-`.` non-`#` character triggers an event when the player uses `interact` while standing beside it. New `interior_interactables` table maps `(building, ch) → action`.
- NPCs visible inside in their schedule slots (rendered as colored sprite on the interior cell)
- **Light sources** — each interior has a list of lit tiles for day/night variation; at night only tiles near a `T` (torch) or lantern emit emojis of light

**Minimap swap**: when player is `inside`, the client minimap replaces the world map with the interior grid (rendered as ASCII in the minimap panel, with `@` for player, letter furniture, `#` walls, `.` floor, ` ` door). The world map is hidden until `exit`. New client function `renderMinimapInterior(room, px, py)`.

The agent will ask the user clarifying questions about each interior's design before writing it (room size, furniture count, NPC location, light sources, interactivity). The Farmhouse interior in this PR is the reference template.

## A3. Atmosphere designed around eventual horror

The long-term goal is a Stardew-style cozy game that **hosts horror elements**: creeping dread, abandoned cabins, night-only encounters, an under-map system, and AI-driven NPCs (see A6). Geography must support that vibe while staying cozy on the surface.

Concrete atmosphere choices in this redesign:
- **Valley bowl geography** — surrounded by mountains gives the player a **contained, watchful** feeling. "Things outside the valley don't reach in — and what's inside doesn't get out."
- **Whisper Wood** is dense, dark, and intentionally confusing — the perfect "you shouldn't be here after dark" zone. At night, rare audio cues (flavors in MUD text: *"The wind through the pines sounds almost like a voice."*).
- **Old Mill** sits abandoned on the civic plaza's edge; until restored it'll have a "locked door / strange writing" interaction, foreshadowing.
- **Observatory** on the mountain trail is the only building with a telescope; at certain hours it shows moving stars / a sixth star that shouldn't be there.
- **Tearoom** by Lake Aurora has a quiet, liminal feel — calm but off. Lake Aurora's reflection at midnight shows "your" reflection's eyes are closed — flavor text only.
- **Docks** have a `Lighthouse` because the lighthouse is the canonical horror prop: at night the beam sweeps the ocean, sometimes illuminating a thing that isn't there the next sweep.
- **The Mountain Trail** to the glacier lake has a **side fork** that leads nowhere interesting (a cul-de-sac of trees) — perfect for a future "the woods watch you" beat.
- **Sound-light gradient**: the MUD narrator's language shifts subtly after 10 PM. Daytime: warm and prosaic. Night: shorter sentences, more negatives, more passive voice.

The horror subsystem itself (jump-scares, the under-map, the AI NPCs) is roadmap'd separately and not built in this round. But the geography, naming, and narrator voice are designed to **enable** it without rewriting later.

## A4. Full Stardew farming + extra mechanics

Implement the full Stardew Valley farming loop, including mechanics currently missing. Listed as *current state → target state*:

| Mechanic | Current | Target |
|---|---|---|
| Till / water / plant / harvest | ✅ | ✅ |
| Scarecrow (protect crops from crows) | ❌ | ✅ — crows eat un-scarecrowed crops overnight, chance scales with crop count |
| Sprinkler (auto-water adjacent tiles) | ✅ (object exists, basic) | ✅ — upgrade tiers: basic (4 adjacent), quality (8), iridium (24) |
| Sprinkler pressure (sprinklers near scarecrows fail) | ❌ | ✅ |
| Trellis crops (beans, hops — impassable, climbable) | ❌ | ✅ — green beans, hops, grapes |
| Multi-season crops (e.g., corn) | ✅ | ✅ |
| Fertilizer tiers (basic, quality, premium) | ❌ | ✅ — boost growth speed + sell value |
| Fruit trees (plant once, harvest seasonally) | ❌ | ✅ — apples, cherries, peaches, pomegranates |
| Keg / preserves jar / mayonnaise machine | ❌ | ✅ |
| Cellar aging (cask) | ❌ | ✅ — wine and cheese improve over time |
| Greenhouse (year-round growing) | ✅ (building only) | ✅ (year-round planting inside) |
| Bee houses | ❌ | ✅ (place near flowers; produces honey faster with closer flowers) |
| Crops can be walked through once mature | ❌ | ✅ |
| Tiller / Agriculturist profession perks (skill tree) | ❌ | ✅ (skills capstone) |

Plus *non-Stardew* new mechanics the user wants:
- **Composting** — convert weeds/fiber into fertilizer
- **Wind pollination** — flowers near crops boost their tier chance next harvest
- **Moon-phase farming** — certain crops grow faster when planted on the new moon

The agent should ship the **missing core** first (scarecrow + trellis crops + fruit trees + fertilizer tiers + composting). Beehouses, kegs, and the cellar come in a follow-up.

## A5. Farmhouse upgrade + buyable plots + free-building

**Farmhouse upgrades** — three levels (Stardew-style):
1. **Starter** — single small room (current interior, 5×7)
2. **Cottage** — adds a kitchen (stove for cooking bread) + a bedroom. Cost 10,000g + 350 wood.
3. **House** — adds a cellar (cask aging) + a study. Cost 50,000g + 450 wood + 200 stone.
4. **Manor** — adds a nursery + a south-facing verandah. Cost 100,000g + premium materials.

Triggered via `upgrade` command at the Carpenter Shop. Each upgrade expands the Farmhouse interior's `rows` vector by appending/additioning new rooms to the south or east.

**Buyable plots** — additional parcels of land beyond the starter Ashgrove Farm:
- **Hillside Plot** — north of town near the mountain trail (cool climate, slower crops but higher value)
- **Forest Clearing** — inside Whisper Wood (high-quality compost + unique wild crops)
- **Lakeside Plot** — by Lake Aurora (great for flowers/trees; visit Tearoom for tea while you work)
- **Docks Lot** — small, by the Fish Shack (sea-mist weathering trades off shipping convenience)

Buy at the Town Center with `buy plot <name>`. Once owned, you can `build` on the plot:
- **Place buildings**: barn, silo, shed, well, mill, mill, scarecrow None — player picks tiles inside the owned plot and `place <structure>` consumes resources.
- **Plots are objects** in the world with an owner flag (`plot_owner[x][y]`).

**First ship**: just the Farmhouse upgrade-level-2 path. Other plots and the place-build system come after.

## A6. Living world — forests, wildlife, seasons react

The world should feel less like a static map and more like an ecosystem. Concrete subsystems:

**Seasonal forest response**
- Whisper Wood has a `forest_state` byte (0 = bud, 1 = lush, 2 = wither, 3 = dormant). It shifts with season. Affects: undergrowth density, forage variety, NPC path choices (Leah won't forage in wither-Wood).
- Deciduous trees "drop" leaves in fall — adjacent tiles gain a `LeafLitter` overlay object (decorative, slows movement 1.0→1.5s).
- Winter snow compacts into paths — once any forest tile has been walked > 5 times in winter, it becomes a snowpath.

**Wildlife (not weather)**
- **Deer** in Whisper Wood — random-walk entities, flee from the player. Daytime only.
- **Rabbits** near farm — eat un-harvested crops at dawn if no scarecrow/fence.
- **Crows** — see A4.
- **Owls** at night near forest — audio cue only, part of the horror setup.
- **Fisher-cats** by Lake Aurora — visible at dawn/dusk; a paragraph of MUD text when you spot one.

**Weather realism**
- **Foggy mornings** in autumn — reduce `look` radius in the MUD ("You can only see a few tiles ahead.")
- **Storms** (see A1) damage buildings + knock down random trees (converts `Tree` → `Stump`).
- **Rain** recharges the well, waters outdoor crops automatically, replenishes the pond, and reduces the daily energy cost of using the watering can.
- **Snow** in winter covers grass → tiles render white; player walking on snow leaves tracks (visual only).

**Subterranean interactivity** (horror roadmap)
- A persistent **under-map layer** parallel to the surface map (saved, not rendered yet). It has its own terrain generation. Accessible from a specific tile (e.g., the Old Mill basement) once unlocked. Each tile carries parallel `under_cells[x][y]` data. Per-day, "things" beneath the map drift one tile — to be built out in the horror PR.

The agent should ship **fall leaf-litter + rabbits + foggy mornings** in this initial round as proof-of-concept; the rest is roadmap'd separately.

## A7. Improved movement — fast travel within the MUD

Current `go <dir>` moves exactly one tile. Exploring 128×96 by single-tile commands is unworkable.

**New movement modes** (all available simultaneously):

1. **Path-walking** (primary) — `go to <landmark>` or `go to <building name>` uses BFS to a known landmark; player auto-walks the entire path with a one-line confirmation.
   - `go to farm` `go to saloon` `go to blacksmith` `go to docks` `go to farmhouse`
   - `go to lake` `go to forest` `go to mountain` `go to bus stop`
   - Server uses `bfs_path()` to compute path, walks player step-by-step (existing client animates smoothly). Movement still obeys `walkable()`.
   - If no path found: "There's no clear way there." If the path is very long, ask "Walk 24 tiles to Maple House? (yes/no)" before setting off.

2. **Multi-tile movement** — `go <dir> <count>` or `go 5 north` walks N tiles in the given direction, stopping if blocked.
   - `go 5 north` `go e 3` `walk 10 w`
   - If blocked at tile 3 of 5: "You walk 3 tiles east, then a rock blocks the way."

3. **Auto-explore direction** — `explore <dir>` walks in that direction until hitting a road fork, a landmark, or a barrier. "You wander west until the path forks at the bridge."

4. **Directional step** (existing, unchanged) — `go north` / `n` still moves one tile for fine-grained control.

5. **Map fast-travel** (unlocked once visited): once the player has `enter`ed a building at least once (saved in `Player::known_landmarks` set), they can `fasttravel <name>` to teleport to its doorstep at the cost of in-game time (10 minutes). Long-distance only.

**Implementation**: refactor the movement command in `src/main.cpp` lines 496-546 to support all 5 modes by parsing the arg. `bfs_path()` already exists; wrap it. Add `Player::known_landmarks` set for fasttravel unlock.

**First ship**: modes 1, 2, 4 (path-walk to landmark, multi-tile directional, single-tile). Modes 3 & 5 come in a follow-up.

## A8. Primary interaction = MUD text

The game's primary interaction modality is text commands, not the visual map. The PIX- rendered map in `assets/index.html` is the secondary visualization (a "smart minimap"). All new features **must** be accessible and readable from the MUD output alone:
- New mechanics emit flavor text (e.g., "The rabbit nibbles your parsnip — gone!").
- Status ("%s", condition, plot ownership) is queryable by text (`status`, `plots`, `buildings`).
- Geo features (fog, snow, leaf-litter) reduce or expand `look` output.
- All `enter` / `interact` / `gift` etc. come from the MUD command loop.

This is the dominant constraint on the design — no mechanic ships if it can't be exercised from a telnet session.

## A9. The agent may always ask

The agent is explicitly directed to ask the user clarifying questions at any point if any of the following are true:
- a building interior's design is underspecified
- a horror/atmosphere beat needs confirming before locking in
- a balance number is unprincipled (costs, decay rates, growth times)
- ambiguity in how a feature should integrate with the MUD command set
- naming anything (regions, items, NPCs) where a thematic choice affects tone

The user prefers being asked over being wrong.

---

# Roadmap (agent will execute these in order)

The following is the *ordered, ship-able work*. The agent will:
1. **Read this whole doc as the source of truth.**
2. Execute the next un-checked step.
3. Compile + verify (`node --check` on client, `cmake --build` on server).
4. Commit + push to GitHub with a conventional commit message.
5. Move to the next step. Continue until all steps done.
6. At any sub-step that is underspecified, **pause and ask the user** before proceeding.

Legend: `[A]` = agent can do unilaterally. `[Q]` = must ask user before doing this step.

## R0. Pre-flight
- [x] [R0.1][A] Verify current branch state, run existing build and client check, confirm green baseline.
- [x] [R0.2][A] Update `MAP_W = 128, MAP_H = 96` in `include/world.hpp`. Client already has these values — verify. Recompile, confirm save/load still works with the bigger map (existing saves left blank). Commit `chore: bump map size to 128x96`.

## R1. Geography rebuild (Phase 1 of town plan)
- [x] [R1.1][A] Rewrite `generate_world()` in `src/world.cpp`:
  - Snow line, mountains, glacier lake (NW corner).
  - Main N-S river around x=44 (was x=29).
  - NW tributary stream.
  - Mountain lake (Lake Aurora) in NE.
  - Southern ocean (last 3 rows).
  - Whisper Wood footprint (west x≈4-22).
  - East Moor footprint (east x≈78-100).
  - Farm footprint (south outskirts, west bank).
- [x] [R1.2][A] Add `Tile::Cobble` to the enum + to `resolve_water_edges` (no-op for cobble).
- [x] [R1.3][A] Update sand and water-edge passes for the new dimensions.
- [x] [R1.4][A] Build, run, quick-connect with `nc` to confirm `look` from spawn makes sense. Commit `feat: valley bowl geography (128x96)`.

## R2. Town buildout (Phase 2)
- [x] [R2.1][Q] Ask the user the final coordinates for each of the 22 buildings (give them a proposed list, let them adjust). Same for the roundabout's exact tile. — *User confirmed design; buildings placed at coordinates in `place_buildings()`.*
- [x] [R2.2][A] Rewrite `place_buildings()` with the 22 buildings per user-confirmed coords.
- [x] [R2.3][A] Add the cobblestone roundabout + statue object (`ObjType::Statue` — new) at plaza center.
- [x] [R2.4][A] Add 3 bridges (north y≈20, main y≈34, south footbridge y≈56) explicitly.
- [x] [R2.5][A] Lay cobblestone main streets (civic↔commerce loop) + dirt side roads (per plan).
- [x] [R2.6][A] Build docks boardwalk near south ocean with planks. Lakefront pier at Lake Aurora.
- [x] [R2.7][A] Update interior doors cleared via existing `clear_paths()`.
- [x] [R2.8][A] Build, smoke-test, commit `feat: town districts + 22 buildings + 3 bridges`.

## R3. Wilderness + ore
- [x] [R3.1][A] Whisper Wood dense forest, carved corridors ensure passability.
- [x] [R3.2][A] East Moor scattered pines + rocks.
- [x] [R3.3][A] Mountain trail switchback up to glacier lake.
- [x] [R3.4][A] Ore deposits: copper near mountain trail, iron mid, gold ridges east, iridium rare.
- [x] [R3.5][A] Dense border woodlands at map edges.
- [x] [R3.6][A] Build, smoke-test, commit `feat: wilderness + ore redistribution`.

## R4. NPC schedules + named regions
- [x] [R4.1][A] Update `init_npcs()` with all 5 NPCs at new home anchors (Leah→Willow House door, Abigail→Maple House door, Elliot→Rowan Cottage door, Robin→Home Field, Evelyn→Tearoom).
- [x] [R4.2][A] Rewrite `npc_schedule()` with new coordinates per slot per NPC.
- [x] [R4.3][A] Rewrite `region_at()` with the full named-region list from the plan.
- [x] [R4.4][A] Build, smoke-test, commit `feat: npc schedules + region names for new town`.

## R5. Movement overhaul (A7, modes 1+2+4)
- [x] [R5.1][A] Add `Player::known_landmarks` set to `world.hpp`. Serialize/deserialize.
- [x] [R5.2][A] Refactor the `go` command in `src/main.cpp` lines 496-546:
  - Parse `<dir> <count>` → multi-tile walk.
  - Parse `to <landmark>` → BFS to nearest known landmark.
  - Default → single-tile (existing).
- [x] [R5.3][A] Add landmark-registration hook in `enter` (player learns the building name into `known_landmarks`).
- [x] [R5.4][A] Update help text. Build, smoke-test.
- [ ] [R5.5][A] Commit `feat: path-walk to landmarks + multi-tile movement`.

## R6. Client rendering for new features (Phase 5)
- [x] [R6.1][A] Add `Tile::Cobble` case in `getTileTex()` with grey-tan cobblestone gradient.
- [x] [R6.4][A] Add `ObjType::Statue` with 🗿 glyph at the roundabout.
- [x] [R6.2][A] Add seasonal tree-glyph function in `assets/index.html` keyed by `season_index(day)` + region tag.
- [x] [R6.3][A] Add new building emoji glyphs (🎣 Fish Shack, 🗼 Lighthouse, 🔨 Carpenter Shop, 🍵 Tearoom, 🔭 Observatory, 🐕 Pet Shop).
- [x] [R6.5][A] Build, smoke-test, commit `feat: client renders cobble + seasonal trees + new buildings`.

## R7. Farmhouse interior redesign (A2 template)
- [x] [R7.1][Q] Ask the user: confirm Farmhouse level-1 interior layout — **Expanded to 7×9 traditional farmhouse** with bed, TV, table, stove, counter, fridge, shelf, chair.
- [x] [R7.2][A] Rewrite `Farmhouse` interior in `init_interiors()` with the agreed 7×9 layout.
- [x] [R7.3][A] Make each piece of furniture interactive via `interact` command (bed→sleep, TV→forecast, stove→cook, counter→prep, fridge→store, shelf→browse, table→dine, chair→sit).
- [ ] [R7.4][A] Client: render interior on the minimap panel when player is `inside`. New `renderMinimapInterior(room, px, py)` function.
- [ ] [R7.5][A] Build, smoke-test that entering/exiting works, commit `feat: redesigned farmhouse interior + minimap swap`.

## R8. Farmhouse weathering + maintenance (A1, Farmhouse only)
- [x] [R8.1][A] Add `BuildingState` struct + per-day decay hook in `World::tick()`. Rainy days → `roof_leak += 1` on Farmhouse. Winter → `foundation -= 2/day`.
- [x] [R8.2][A] Add `repair farmhouse` command at the Carpenter Shop. Cost: 10 wood + 5 stone per 10 condition. Restores `condition` to 100.
- [x] [R8.3][A] Show condition in `status`: "Farmhouse: roof 87, foundation 92, condition 95".
- [x] [R8.4][A] Add decay-triggered flavor text: rainy night → "The roof groans. A drip lands on the kitchen floor." (logged to server console for players inside)
- [ ] [R8.5][A] Build, smoke-test, commit `feat: farmhouse weathering + repair (first ship of A1)`.

## R9. Living world proof-of-concept (A6)
- [x] [R9.1][A] Add `ObjType::LeafLitter` (overlay) and a per-day autumn hook that scatters leaf litter adjacent to deciduous trees in Whisper Wood.
- [x] [R9.2][A] Add `Rabbit` NPCs (non-hostile, data in the existing NPC array using a `kind` field). 2 rabbits near the farm. At dawn they eat one adjacent un-harvested crop if no fence.
- [x] [R9.3][A] Add `weather` chance of `foggy` in autumn mornings; fog reduces `look` output to 1-tile radius (adjacent only). `clear` later in day.
- [x] [R9.4][A] Build, smoke-test, commit `feat: living world — leaf litter, rabbits, autumn fog`.

## R10. A4 farming: scarecrow + fertilizer
- [x] [R10.1][A] Add scarecrow (object) + crow overnight logic: each night, un-scarecrowed mature crops have 5% chance each to be eaten by crows. Scout scarecrow covers a 17×17 area; quality scarecrow 99-tile.
- [x] [R10.2][A] Add fertilizer tiers (Basic, Quality, Premium). New `Item::FertilizerBasic` etc. Applied to tilled soil before planting.
- [x] [R10.3][A] Build, smoke-test, commit `feat: scarecrow + fertilizer tiers`.

## R11. A4 farming: trellis crops + fruit trees
- [x] [R11.1][A] Add trellis crops (green beans, hops). Impassable tiles. Mature-harvestable.
- [x] [R11.2][A] Add fruit trees (apple, cherry, peach, pomegranate). Take 28 days to mature, produce once/season after that.
- [x] [R11.3][A] Build, smoke-test, commit `feat: trellis crops + fruit trees`.

## R12. A5 farmhouse upgrade level 2
- [x] [R12.1][A] Add `FarmhouseLevel` to `World` state (1, 2, 3, 4).
- [x] [R12.2][A] Add `upgrade farmhouse` command at the Carpenter Shop. Cost: 10,000g + 350 wood. Expands interior to add kitchen + bedroom. Also added levels 3 (House) and 4 (Manor) with costs.
- [x] [R12.3][A] Build, smoke-test, commit `feat: farmhouse upgrade level 2 (cottage) + more crops/fruit trees`.

## R13. A4 farming: composting + wind-pollination + moon-phase
- [x] [R13.1][A] Add `Composter` machine. Place on farm, throw fiber in, harvest fertilizer after 4 days.
- [x] [R13.2][A] Wind pollination: flowers adjacent to crops increase crop tier chance at harvest (20% per flower for 2x sell price).
- [x] [R13.3][A] Moon phase (`moon_phase(day)` returns 0..7). New moon: crops planted that day grow 10% faster (bonus growth day).
- [x] [R13.4][A] Build, smoke-test, commit `feat: composter + wind pollination + moon phase farming + 16 new crops + 12 new fruit trees`.

## R14. A2 interiors: do them right for all buildings
- [x] [R14.1][Q] Ask the user to confirm layouts per building (group by district: civic, commerce, residential, lakefront, docks, rail/bus). Get sign-off on ASCII schemas for each room (size, furniture count, light sources, NPC location).
- [x] [R14.2..R14.21][A] All 22 building interiors implemented with rich, multi-floor designs:
  - **Civic (6)**: Town Center (2F), Clinic, Museum (2F), Old Mill, Stardrop Saloon (2F - tavern + guest rooms)
  - **Commerce (5)**: Blacksmith, General Store, Market, Carpenter Shop, Pet Shop
  - **Residential (4)**: Willow House, Maple House, Rowan Cottage, Hawthorne Cottage
  - **Travel (2)**: Bus Stop, Railway Station (2F)
  - **Farm (2)**: Hawthorn Barn, Glasshouse
  - **Lakefront (2)**: Tearoom (2F), Observatory (3F)
  - **Docks (2)**: Fish Shack, Lighthouse (4F)
  - Farmhouse (enhanced)
  - Multi-floor support added to InteriorRoom structure
  - Stairs (`<>`) connect floors; each floor has unique furniture/purpose
  - Interactive furniture letters mapped to actions (bed, TV, counter, forge, etc.)
- [x] Build, smoke-test, commit `feat: R14 all 22 building interiors + multi-floor support`.

## Stardew-style tree/weed mechanics (completed)
- [x] Saplings drop from mature trees when chopped (20% chance for hp > 100)
- [x] `shake` command: shake mature trees for saplings (25% chance, costs 2 energy)
- [x] Weeds give random mixed seeds when cut with scythe (15% chance)

## R15. A1 weathering + maintenance for all 22 buildings
- [R15.1][A] Wire decay into all 22 `BuildingState` entries. Tune per district (docks buildings decay faster, civic slower, mountain trail buildings need roofwork each winter).
- [R15.2][A] Add `repair <building>` general command. Carpenter Shop sells materials.
- [R15.3][A] Building condition < 20 → renders 🏚️ + blocks NPC entry (NPC stays home).
- [R15.4][A] Build, smoke-test, commit `feat: building decay for all 22 + repair command`.

## R16. A5 buyable plots + place-building
- [R16.1][Q] Ask the user: confirm 4 buyable plot locations (Hillside, Forest Clearing, Lakeside, Docks Lot) + price + climate.
- [R16.2][A] Add `Plot` struct + `world.plots` array. Add plot-buy at Town Center.
- [R16.3][A] Add `place <struct>` inside owned plots (barn, silo, shed, well, scarecrow).
- [R16.4][A] Build, smoke-test, commit `feat: buyable plots + placeable structures`.

## R17+. Long-horizon systems (deferred from this round; sequenced in future docs)
- A3 horror system (under-map, night encounters, AI NPCs) — separate roadmap doc when ready.
- A4 full Stardew (bee houses, kegs, cellar aging) — comes after R13.
- A3 narrator-voice gradient for night — comes with horror system.
- A6 subterranean under-map layer — horror.

---

## Forestation & Tree Products Expansion (added alongside R14)
- [x] 11 new forestation tree types: Oak, Maple, Birch, Cedar, Redwood, Teak, Mahogany, Rubber Tree, Walnut, Hickory, Chestnut
- [x] Tree products: Sap, Resin, Rubber, Bark, Hardwood, Maple Syrup, Oak Resin, Pine Tar
- [x] Tree seeds/saplings for all 11 types
- [x] Logs for each tree type (Oak Log, Maple Log, etc.)
- [x] Lumber processing: Lumber, Plank, Plywood
- [x] Nuts: Walnut, Hickory Nut, Chestnut, Acorn
- [x] `planttree` command to plant forestation trees
- [x] `tap` command to install tappers on mature trees for sap/syrup/resin/rubber
- [x] Tree growth simulation (hp increases over time)
- [x] Axe drops appropriate logs per tree type (hardwood trees give Hardwood + logs)
- [x] All tree products edible with energy values

---

# Agent operational rules
1. Start at R0.1. Check the box. Move on.
2. For any `[Q]` step, stop and ask the user before writing code.
3. For every `[A]` step, write code, run the build, run `node --check` if you edited the client, commit + push.
4. After each step, print a 2-3 line summary and proceed automatically to the next.
5. The user can interject at any time to redirect.
6. If a step fails (compile/test), rollback the change, fix, retry. Don't move on with red builds.
7. One commit per atomic step. Conventional commit messages. Push after each commit.
8. Never commit a broken state.

---

This document is the source of truth. If any later discussion contradicts it, update the document first, then proceed.
