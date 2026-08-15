# Changelog

All notable changes to Ashgrove Valley are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added — Phase 1 Core Stardew Features
- **Seasonal festivals** — one festival per season on day 13:
  - Spring Fair (egg hunt via `search`), Summer Luau, Autumn Harvest Festival, Winter Star Festival
  - Summer/Autumn/Winter award a one-time money + item reward guarded by `festival_claimed_day`
  - `festival_name()` / `next_festival_day()` helpers; `/state` exposes the current festival
- **Fishing system**:
  - `Fishing Rod` (starter inventory) + `Bait` (purchasable, +25 skill)
  - Location-based fish tables (Ocean/River/Lake/Mountain) with seasonal + time-of-day availability
  - Reeling mini-game: skill = 50 + energy/10 + bait/rain bonuses; deterministic catch roll
- **Cooking & recipes**:
  - 14 recipes (bread, salad, omelet, cheese omelet, milk pudding, fruit salad, jam toast, fish stew, pumpkin soup, grain porridge, honey bread, grilled fish, fruit tart, cheese plate)
  - `cook <recipe>` consumes ingredients and produces a dish item; dishes edible with energy restore
- **Animal system**:
  - Barn (capacity 4, cows/goats) + Coop (capacity 4, chickens) built with `build`
  - `place <animal> <building>` requires 1 forage; capacity- and ownership-guarded
  - `feed <building>` consumes 1 forage per hungry animal; feeding builds friendship
  - Daily tick: hunger grows, friendship rises when fed / falls when hungry
  - `collect` next to a building; friendship ≥80 doubles yield; hungry animals stop producing
  - `friendship` field added to `Animal` (0–100)
- **Expanded crops (toward 100+)**:
  - 11 new crops: tulip, blue jazz, starfruit, poppy, amaranth, yam, eggplant, okra, beet, ancient fruit, sweet gem berry (seeds + produce, item defs, crop tables, shop, sell)
  - **Giant crops**: mature 3×3 cauliflower/melon/pumpkin patches have a 1%/night chance to merge into a giant crop that harvests 6–12 produce

### Fixed
- **LLM layer stability**: `/cmd` (which runs `llama.parse_command` for every command) crashed under repeated/concurrent requests with heap corruption in llama's batch allocator. Root cause: the persistent `llama_context` reused its KV buffers/allocator across requests (`llama_memory_clear` left the batch allocator referencing freed memory). Fixed by creating a fresh `llama_context` per `parse_command` (the model loads once and is reused read-only), guarding all decode calls with an internal mutex, and limiting httplib to a single worker (`new_task_queue = ThreadPool(1)`). Verified with concurrent stress tests (5 simultaneous `/cmd` requests complete without crashing).
- Flash-attention / q8_0 KV-cache flags removed: gemma-4 is SWA-based and these trips GGML asserts on its attention mask. KV cache stays F16; only `offload_kqv = false` is set.

### Added — Phase 2: Advanced Crafting & Machines
- **Processing machines** (craftable, placeable, daily processing):
  - **Keg**: Ferments fruit→wine (7 days), hops→pale ale (2 days), wheat→beer (7 days), coffee→coffee (1 day), rice→sake (3 days), honey→mead (10 days). Craft: 30 Wood + 1 Copper Bar + 1 Iron Bar + 1 Oak Resin.
  - **Preserves Jar**: Fruit→jelly (3 days), vegetables→pickles (2 days). Craft: 50 Wood + 40 Stone + 8 Coal.
  - **Mayonnaise Machine**: Eggs→mayonnaise (3 hours). Craft: 15 Wood + 15 Stone + 1 Earth Crystal + 1 Copper Bar.
  - **Bee House**: Produces honey daily; wild honey near flowers (tulip, blue jazz, poppy, sunflower, sweet pea, fairy rose). Craft: 40 Wood + 8 Coal + 1 Iron Bar + 1 Maple Syrup.
  - **Cask** (cellar only): Ages wine/cheese through quality tiers (normal→silver 14d→gold 21d→iridium 28d). Craft: 20 Hardwood + 50 Wood + 20 Stone.
- **Greenhouse building** (`build greenhouse`): 10×6 structure allowing year-round planting (bypasses winter/season restrictions).
- **Skill system & perks** (Farming skill 0–10):
  - **Agriculturist** (Farming 10): All crops grow 10% faster (applied in `advance_day`).
  - **Tiller** (Farming 10): All crops sell for 10% more (applied in `sell` command).
  - Perk flags stored on `Player`, checked globally in daily tick and sell.
- **New items**: Machine items (Keg, PreservesJar, MayonnaiseMachine, BeeHouse, Cask, Greenhouse), machine outputs (Pale Ale, Beer, Sake, Mead, Coffee, Rice, Hot Pepper, Jelly, Pickles, Wild Honey, Aged Wine, Aged Cheese), egg variants (Large Egg, Brown Egg, Duck Egg, Void Egg, Dinosaur Egg), resources (Coal, Earth Crystal), vegetables (Cucumber, Carrot, Radish), flowers (Sunflower, Sweet Pea, Fairy Rose).
- **Commands**: `craft keg/preserves jar/mayonnaise machine/bee house/cask`, `place keg/preserves jar/mayonnaise machine/bee house/cask`, `interact add/put/fill` (load machines), `collect` (retrieve from machines), `build greenhouse`, `sell` (Tiller perk), `advance_day` (Agriculturist perk).

### Added — Phase 3: Social & NPC Relationships
- **Gift preference system**: Comprehensive love/like/neutral/dislike/hate tables for 5 villagers + 2 rabbits across 100+ items.
- **Hearts system expanded to 14**: 8 hearts (Bouquet → engagement), 10 hearts (Wedding Ring → marriage), 14 hearts (max).
- **Marriage system**: `gift <npc> Bouquet` at 8 hearts → engagement; `gift <npc> Wedding Ring` at 10 hearts → marriage; `divorce <spouse>` ends marriage.
- **Children & family**: After 14 days marriage + nursery, 5%/day chance for child (random names); `hearts` shows spouse + children.
- **Heart decay**: Ungifted NPCs lose 1 heart/week; spouse exempt; daily gift limit.
- **Roommate events**: At 8+ hearts, 2%/day chance to ask to stay over.
- **NPC schedule adaptation**: NPCs with ≥8 hearts have 20% chance to visit player's farm; spouse follows player.
- **LLM-driven dialogue**: `talk` attempts LLM generation with context, falls back to seasonal greetings.
- **New items**: Bouquet (200g), Wedding Ring (5000g).
- **Commands**: `gift` (Bouquet/Wedding Ring logic), `hearts` (14-heart display + spouse/children), `divorce`, `talk` (LLM fallback).

### Added — Phase 4: Town & Map Expansion
- **Infinite chunk system**: 128×128 tile chunks, 8 chunk radius (1024 tiles), `std::map<ChunkCoord, Chunk>` storage.
- **Procgen regions**: 8 types (Forest, Hills, Mountains, Caves, Ruins, Ocean, Swamp, Valley) with region-based terrain generation.
- **12+ new interiors**: Barn, Greenhouse, Cellar, Shrine, Cabin, Ruin, Cave, Well, Windmill, Silo, Shed, Well/Windmill/Silo/Shed interiors.
- **Infinite map navigation**: Chunk-based coordinates, universal `cell_at(gx, gy)`, global walkable check.
- **DSL construction system**: `dsl <structure> @ x,y` parser, supports barn/coop/silo/shed/well/windmill/greenhouse/shrine/cabin.
- **New commands**: `dsl`, `explore`/`map`, `travel`, `region add`, `build shrine|cabin`.
- **New items**: Machine items (Keg, PreservesJar, etc.), outputs (Pale Ale, Jelly, etc.), egg variants, resources (Coal, Earth Crystal), vegetables, flowers, marriage items (Bouquet, Wedding Ring).
- **Commands**: `dsl`, `explore`/`map`, `travel`, `region add`, `build shrine|cabin`, `gift` (Bouquet/Wedding Ring), `hearts` (14-heart + spouse/children), `divorce`, `talk` (LLM fallback).

### Added — Phase 5: Quest & Job System
- **Quest system**: Dynamic quest generation sampling templates + world state (season, NPC mood, weather, economy).
  - Quest types: fetch, deliver, investigate, ritual, kill
  - Auto-generates 2-3 quests per day, expires in 3 days
  - Rewards: money, items based on quest type and target count
  - `/quest list|complete <id>|history` commands
- **Job board**: Repeatable work from NPCs
  - Job types: farmhand, miner, courier, researcher
  - Daily cooldown, immediate rewards on completion
  - `/job list|do <id>` commands
- **Living Economy**: Supply/demand price fluctuations
  - Market prices update daily based on seasonality and simulated supply/demand
  - Seasonal items cheaper in season, expensive out of season
  - `/market` command shows current prices with trend indicators
- **Event-driven reward scaling**: Rewards scale with target count and world state
- **Commands**: `quest`, `job`, `market`

### Added — Phase 6: Horror & Narrative Overlays
- **Sanity meter**: Per-player 0–100 sanity (serialized), daily drift tied to time of day, weather, and being in the basement. Restored by calm daylight or by finding secrets.
- **Perception tiers**: 4 tiers (Sane → Uneasy → Strained → Fractured) derived from sanity; drive horror flavor text, phantom sightings, and Disco-Elysium-style internal voices in `look`.
- **Under-map basement**: `basement` command + `/basement` endpoint; the hatch under the farmhouse only opens after midnight (hour ≥ 24). Entering drains sanity, increments the horror cycle (Higurashi-style), and can be left via `exit`.
- **Night events**: Sleeping at/after 22:00 rolls a chapter-style scripted night event (deterministic, sampled from season + weather + day), appended to a per-player `night_event_log` and surfaced on wake.
- **Fourth-wall / internal voices**: At strained/fractured sanity, `internal_voice()` injects self-aware monologue lines (DDLC-style) into command output.
- **Higurashi cyclical secrets**: `find_secret()` records discovered secrets; each secret restores a little sanity.
- **HTTP**: new `/horror` (sanity + narrative state) and `/basement` (enter/leave) endpoints; `/state` and `/join` now include `sanity` / `sanity_tier`.
- **PIXI client**: SANITY readout in the topbar; night vignette shadow, drifting fog (stronger in basement/at low sanity), glitch scanline overlay scaled to sanity tier, and procedural Web Audio drone cues (no assets).

### Fixed
- **Bridge run logic**: Fixed bridge generation for roads crossing water.
- **Memory management**: Fixed `has_item` helper in world.cpp for DSL construction.

### Fixed
- Interior `go to <landmark>` bug: leaving a building via `go to` now clears `p.inside` so the UI no longer shows the old interior while text claims a new location.
- `place <animal> <building>` never placed the first animal because the initial animal vector was empty (loop found no slot); now appends a new animal.

### Added — Phase 1 prerequisite (previous session)
- R16: Buyable plots + placeable structures
  - `buy plot` / `buy plot <name>` at the Town Center (list 4 parcels, purchase a parcel)
  - 4 buyable plots: Hillside (15,000g, cool mountain air), Forest Clearing (8,000g, temperate woodland), Lakeside (12,000g, humid lake breeze), Docks Lot (10,000g, salty coastal wind)
  - `place barn|silo|shed|well|windmill` inside an owned plot (costs gold + wood + stone, some fiber)
  - `plots` / `deeds` command lists all plots with ownership + structure count
  - `owned_plots` + `placed_structs` now serialized in save/load
  - Help text updated with the new commands

### Fixed
- `repair` command crash: pointer into the `w.buildings` vector was `delete`d; guarded so only the dynamically-allocated Farmhouse case is freed.

## [0.6.2] - 2026-08-12

### Fixed — Compiler Warning Cleanup
- All C-style casts replaced with `static_cast<>` (C++ Core Guidelines ES.49)
- Sign conversions fixed: `int8_t`/`uint8_t`, `int16_t`/`int`, `uint64_t`/`uint32_t`
- Array index sign conversions: `int` → `size_t`/`array::size_type` for `std::array<InvSlot,12>` and interior room vectors
- JSON deserialization: explicit casts for `hp`, `ore`, `stage`, `days_left`, `count`
- Server now builds clean with `-Wconversion -Wsign-conversion -Wold-style-cast -Werror=return-type -Werror=non-virtual-dtor`

### Added — Documentation
- `README.md`: Architecture, build instructions, design philosophy, parallelism strategy
- `CHANGELOG.md`: Full version history from v0.1.0

## [0.6.1] - 2026-08-12

### Added — Build System & Parallelism Infrastructure
- **CMake 3.25+** with modern practices: `FetchContent`, target-based config, generator expressions
- **Parallelism-ready CMake**:
  - `Threads::Threads` linked for `std::thread`, `std::execution::par`
  - Release: `-O3 -march=native -mtune=native -flto=auto`
  - Debug: sanitizer support (ASan, UBSan, LSan, TSan on Clang) via `ENABLE_SANITIZERS=ON`
  - Warning flags as errors for critical issues (`-Werror=return-type`, `-Werror=non-virtual-dtor`)
  - Compile commands export for LSP/clangd
  - Format target (`clang-format`) and CPack packaging
- **Thread pool infrastructure** (in `src/main.cpp` game loop): autosave thread + game loop thread decoupled from HTTP server threads

### Fixed
- CMake warning flags now applied via `target_compile_options` (not `CMAKE_CXX_FLAGS`) to avoid shell splitting bugs
- Build now passes with aggressive warning flags (`-Wconversion`, `-Wsign-conversion`, etc.)

## [0.6.0] - 2026-08-12

### Added — Town Buildout Complete (R2)
- **R2.3**: Cobblestone roundabout at Stardrop Plaza (24,28) with 3×3 grass center + 🗿 statue
- **R2.5**: Cobblestone main streets on both river banks forming civic↔commerce loop via North/Main bridges
- **R2.6**: Docks boardwalk (wooden planks over south ocean, x=62–120) + Lake Aurora pier at Tearoom
- **R2.8**: All 22 buildings placed with 3 explicit bridges; `clear_paths()` protects doorways/bridges

### Added — Movement Overhaul (R5.1–R5.4)
- **R5.1**: `Player::known_landmarks` (std::set<string>) persisted in save/load
- **R5.2**: `go` command extensions:
  - `go 5 north` / `go e 10` — multi-tile walk (1–50 tiles)
  - `go to town center` / `go to farmhouse` — BFS path-walk to known landmarks
- **R5.3**: Landmark registration on `enter` command
- **R5.4**: Updated help text with new movement syntax

### Added — Client Rendering (R6.1–R6.5)
- **R6.1**: `Tile::Cobble` rendering (grey-tan gradient with per-tile noise)
- **R6.2**: Seasonal tree glyphs by region tag:
  - Town: 🌸 (spring), 🌳 (summer), 🍂 (fall), 🌲 (winter)
  - Forest/Moor/Tundra/Lakeside/Shore: context-appropriate variants
- **R6.3**: New building emojis in `BLD_STYLE`:
  - Fish Shack 🎣, Lighthouse 🗼, Carpenter Shop 🔨, Tearoom 🍵, Observatory 🔭, Pet Shop 🐕, Hawthorne Cottage 🏠
- **R6.4**: `ObjType::Statue` (🗿) at roundabout center
- **R6.5**: Client renders all new map features

### Changed
- Map size: 128×96 (was 96×64) — `MAP_W`, `MAP_H` in `include/world.hpp`
- `Tile::Cobble = 13` enum value added
- `ObjType::Statue = 14` enum value added
- `Player::known_landmarks` added to `Player` struct
- Save format v2: includes `known_landmarks` array per player

### Fixed
- Bridge rendering: `bridge_run()` now handles cobblestone crossings over tributary stream
- Save/load: `known_landmarks` properly serialized/deserialized
- NPC schedules: updated for new building coordinates

## [0.5.0] - 2026-08-11

### Added — Geography Rebuild (R1)
- Valley bowl terrain: mountains (N), glacier lake (NW), snow line ~y=12
- Main river N-S at x≈44 with 3 bridge crossings
- NW tributary stream joining at y≈22
- Lake Aurora (NE) + southern ocean (bottom 3 rows)
- Whisper Wood (W, x=4–22) with carved corridors
- East Moor (E, x=78–100) with scattered pines/rocks
- Farm plot (S, overgrown, player-tills) with fence + gate
- Ore deposits: copper (N), iron (mid), gold (E ridges), iridium (rare)
- Dense border woodlands at map edges

### Added — NPC Schedules & Regions (R4)
- 5 NPCs with new home anchors: Leah→Willow House, Abigail→Maple House, Elliot→Rowan Cottage, Robin→Carpenter Shop, Evelyn→Tearoom
- Full daily schedules per NPC with time slots
- `region_at()` with 15 named regions (Frostveil Tundra, Whisper Wood, Stardrop Plaza, etc.)

### Added — Client Infrastructure
- PIXI.js v8 renderer with painterly tile textures (LPC tileset)
- Seasonal palette (spring/summer/fall/winter tints)
- Day/night ColorMatrixFilter
- Weather particles (rain, snow)
- Emoji sprite preloading (Twemoji PNGs)
- Interior ASCII-room view on minimap swap

## [0.4.0] - 2026-08-10

### Added — Core Gameplay Loop
- Farming: till, plant, water, harvest (parsnip, potato, cauliflower, corn, tomato, wheat, blueberry)
- Tools: hoe, watering can, axe, pickaxe, scythe (energy costs)
- Fishing: seasonal fish tables, time-of-day windows, weather bonuses
- Foraging: seasonal tables, biome bonuses (Whisper Wood)
- Mining: copper/iron/gold/iridium ore from rocks
- Crafting: smelt bars (5 ore + 1 wood), bread (3 wheat)
- Sprinklers: steel (2 iron + 1 gold), iridium (2 iron + 2 gold + 1 iridium)
- Shopping: General Store / Market (seasonal seeds, bread)
- Social: talk, gift, hearts (0–10), NPC greetings by season
- Buildings: enter/exit, interact (sleep, heal, eat, shop, browse)
- Travel: bus (25g to plaza), train (advance day), sleep (advance day at farmhouse)
- Festivals: Egg Festival (Spring 13) with 36-patch search minigame
- Persistence: save/load/newgame/saves commands, JSON serialization, autosave (60s)

### Added — NPC System
- 5 villagers with patrol waypoints and daily schedules
- BFS pathfinding (A* with Manhattan heuristic)
- Festival override (all gather at Town Center)
- Friendship hearts (gift taste per NPC)

## [0.3.0] - 2026-08-09

### Added — Visual Client
- Single-file `assets/index.html` with PIXI.js
- Terminal log with syntax highlighting (me/sys/err/ok/gold/npc)
- Status bar (day, time, season, weather, energy, gold)
- Minimap with zoom/pan/hover tooltip/region label
- Inventory grid (12 slots) with click-select
- Dynamic legend (villagers, buildings, nature)
- Building roof/wall colors + emoji glyphs
- Seasonal tree rendering (placeholder)

### Added — Infrastructure
- CMake + Ninja build with FetchContent (nlohmann/json, cpp-httplib)
- HTTP server (httplib) with `/join`, `/state`, `/cmd`, `/move`, `/action`, `/sleep`, `/warp`
- Threaded game loop (16ms tick) + autosave thread
- JSON persistence (world, players, cells, NPCs, interiors)

## [0.2.0] - 2026-08-08

### Added — Prototype
- 96×64 map with basic terrain (grass, water, dirt, snow)
- Player movement (N/S/E/W), look, inventory
- Basic farming (hoe, plant, water, harvest)
- 3 NPCs with simple patrol AI
- Save/load to `save.json`
- ASCII terminal client (pre-PIXI)

## [0.1.0] - 2026-08-05

### Added — Initial Scaffold
- CMake project structure
- `World`, `Player`, `NPC`, `Cell`, `Tile`, `Item`, `ObjType` types
- Basic world generation (perlin noise terrain)
- Single-threaded game loop
- Minimal HTTP endpoints

---

## Versioning Policy

- **Major**: Breaking protocol changes, save format incompatible, architecture rewrites
- **Minor**: New features, new content, new commands, map expansions (save-compatible)
- **Patch**: Bug fixes, performance, text tweaks, client-only changes

Save compatibility is maintained within minor versions. Major versions provide migration tooling.