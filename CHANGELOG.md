# Changelog

All notable changes to Ashgrove Valley are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Core design principles documented in `docs/map-redesign-plan.md`:
  - Maximum parallelism (all CPU cores utilized)
  - Simulation-first realism (hyper-detailed world model)
  - Imagination-driven MUD interface (verbose prose commands)
  - Bug-first development (fix immediately)
  - Documentation standards (CHANGELOG, README, verbose design docs)
- `README.md` with project overview, architecture, build instructions, design philosophy
- `CHANGELOG.md` (this file)

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