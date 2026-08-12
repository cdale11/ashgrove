# Ashgrove Valley

A Stardew Valley–inspired farming simulation MUD with a hyper-detailed simulation backend and a minimal visual reference client.

## Design Philosophy

> **The visual map is a reference prompt. The real game lives in the simulation and the player's imagination.**

Ashgrove Valley is built on three pillars:

1. **Maximum Parallelism** — Every subsystem uses all available CPU cores. World generation, NPC AI, crop simulation, weather, persistence, and networking all run on thread pools scaled to `std::thread::hardware_concurrency()`.

2. **Simulation-First Realism** — The server maintains a hyper-detailed world model:
   - Per-tile: terrain, sub-terrain, moisture, temperature, soil quality, elevation, vegetation density, light, sound
   - Per-building: room layouts, furniture, lighting, temperature, smell, sound, structural condition, ownership, history
   - Per-NPC: schedules, personality, memory, relationships, skills, inventory, home, workplace
   - Per-crop: variety, stage, water, fertilizer, disease, pollination, yield quality
   - Weather: humidity, pressure, wind, precipitation, fog, temperature gradients
   - Time: seasons, moon phases, festivals, aging, decay, growth cycles

3. **Imagination-Driven MUD Interface** — Primary interaction is text commands that read like novel prose. The visual client (PIXI.js) is a secondary minimap for orientation only.

## Quick Start

### Prerequisites
- C++20 compiler (GCC 12+, Clang 15+, MSVC 19.35+)
- CMake 3.25+
- Ninja build system
- nlohmann/json (via FetchContent)
- cpp-httplib (via FetchContent)
- Node.js (for client asset verification)

### Build & Run
```bash
# Configure with Ninja (parallel by default)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build using all cores
cmake --build build --parallel $(nproc)

# Run server on port 8080
./build/ashgrove_server 8080

# Open http://localhost:8080 in browser
```

### Development Build
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build
```

## Architecture

```
ashgrove/
├── include/
│   ├── world.hpp          # Core types: World, Player, NPC, Tile, ObjType, Item, Building, InteriorRoom
│   └── protocol.hpp       # HTTP/JSON API contracts
├── src/
│   ├── world.cpp          # World generation, simulation, persistence, NPC schedules, regions
│   └── main.cpp           # HTTP server, command interpreter, game loop, networking
├── assets/
│   ├── index.html         # Single-file PIXI.js client (map, terminal, inventory, status)
│   ├── emoji/             # Twemoji PNG sprites for crisp rendering
│   └── vendor/            # PIXI.js minified
├── docs/
│   ├── map-redesign-plan.md  # Source-of-truth design document + roadmap
│   └── CHANGELOG.md          # Version history
├── CMakeLists.txt
└── README.md
```

## Key Systems

### World Generation (`src/world.cpp::generate_world`)
- **Valley Bowl geography**: 128×96 map with mountains (N), river (N-S), glacier lake (NW), mountain lake/Lake Aurora (NE), southern ocean (S)
- **Biomes**: Whisper Wood (dense forest W), East Moor (open grassland E), Frostveil Tundra (N), Farm (S)
- **Districts**: Civic Plaza (W), Commerce Row (E), Birch/Maple Courts (residential), Lakefront, Docks
- **22 Buildings**: Farmhouse, shops, civic, residential, travel, farm outbuildings, lakefront, docks
- **3 Bridges**: North (y≈20), Main (y≈34), South footbridge (y≈56)
- **Roads**: Cobblestone main loop + dirt side roads + farm lanes
- **Ores**: Copper (N), Iron (mid), Gold (ridges E), Iridium (rare)

### Simulation Loop (`src/main.cpp::game_loop`)
- Fixed 16ms tick (60 Hz) decoupled from network
- Parallel phases under mutex: time advance → player movement → NPC AI (schedules + pathfinding) → environment (crops, weather) → autosave (60s interval)

### MUD Command Interpreter (`src/main.cpp::handle_cmd`)
Rich text commands with verbose output:
- **Movement**: `go north`, `go 5 east`, `go to town center`, `go to farmhouse`
- **Farming**: `hoe`, `plant parsnip`, `water`, `harvest`, `fertilize`
- **Gathering**: `axe`, `pick`, `scythe`, `forage`, `fish`
- **Social**: `talk Leah`, `gift Abigail parsnip`, `hearts`
- **Buildings**: `enter`, `exit`, `interact`, `buy`, `sell`, `craft`
- **Travel**: `train`, `bus`, `sleep`
- **Meta**: `look`, `status`, `inventory`, `save`, `load`, `help`

### Visual Client (`assets/index.html`)
Single-file PIXI.js app with:
- **Map renderer**: Painterly Stardew-style tiles, seasonal palette, day/night filter, weather particles
- **Terminal**: MUD log with syntax highlighting, clickable suggestions
- **Status bar**: Day, time, season, weather, energy, gold, connection
- **Minimap**: Zoom/pan, hover tooltip, region label
- **Interior view**: ASCII-room swap when inside buildings
- **Inventory**: 12-slot grid with drag-select
- **Legend**: Dynamic tile/object/NPC/building glyphs

## Client-Server Protocol

### HTTP Endpoints
- `POST /join {name}` → `{player_id, tile_map, buildings, house, day, time, season, weather, welcome}`
- `GET /state` → Live snapshot: players, cells, NPCs, interiors, buildings, time, season, weather
- `POST /cmd {player_id, cmd}` → `{lines: [...]}` — MUD command execution
- `POST /move {player_id, target_x, target_y}` → BFS pathfinding for click-movement
- `POST /action {player_id, x, y, sel?}` → Tool use on adjacent tile
- `POST /sleep {player_id}` → Advance day (if at farmhouse door)
- `POST /warp {player_id, x, y}` — Dev only

### WebSocket (planned)
Real-time push for: NPC movement, player movement, weather changes, chat, events.

## Parallelism Implementation

| System | Parallelization Strategy |
|--------|-------------------------|
| World Gen | `std::for_each(std::execution::par, ...)` for independent terrain passes |
| NPC AI | Thread pool: each NPC's `schedule_slot` + `bfs_path` on worker threads |
| Crops | Per-cell growth updates distributed across threads |
| Weather | Regional simulation cells updated in parallel |
| Persistence | Async JSON serialization on background thread; double-buffered world state |
| Network | `httplib::Server` with thread pool; each connection handled concurrently |

Target: Linear scaling to 16+ cores.

## Contributing

1. Read `docs/map-redesign-plan.md` — it is the source of truth
2. Follow the roadmap (R0→R17) in order
3. Every `[Q]` step requires user confirmation before coding
4. Write verbose MUD output — test by playing via `nc localhost 8080`
5. Fix bugs immediately; add regression tests
6. Update CHANGELOG.md and design docs with every change
7. Commit with conventional messages: `feat:`, `fix:`, `chore:`, `docs:`, `refactor:`

## License

MIT — see LICENSE file.