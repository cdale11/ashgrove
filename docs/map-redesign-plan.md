# Ashgrove Valley — Roadmap (Active / Deferred)

Approved 2026-08-12. **All R0–R16 work is complete and shipped** — see
[`docs/shipped-features.md`](./shipped-features.md) for the consolidated record
of what's live. This document is the **future-facing roadmap**: it keeps the
design principles that still guide development, the parallelism roadmap, the
expanded design vision with per-item ship status, and the deferred long-horizon
work.

---

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
**Commands should read like prose from a novel, not terse computer output.** See the approved examples in the original addendum (statue `examine`, `survey`, `look`).

### 4. Imagination-Driven Gameplay
**The player's mind renders a richer world than any GPU.** Design for mental visualization; the visual client shows only player position, major landmarks, and fog-of-war edges.

### 5. Bug-First Development
**Fix flaws immediately when discovered.** No known bugs ship. Regression tests for every fix.

### 6. Documentation Standards
- **CHANGELOG.md**: Every commit summarized, user-facing changes highlighted
- **README.md**: Project overview, architecture, build/run instructions, design philosophy
- **Design docs**: Verbose, decision-logged, updated with each change
- **Code comments**: Explain *why*, not *what*

---

## Parallelism Roadmap (Post-v0.6)

> Status table lives in `docs/shipped-features.md`. **Current bottleneck**: single
> `g_mutex` protects entire `World` in the game loop.

### Phase P1 — Fine-Grained Locking
- Split `World` mutex: `cells_mutex`, `players_mutex`, `npcs_mutex`, `buildings_mutex`
- Read-heavy ops (look, survey, map render) use shared_lock; writes use unique_lock
- Target: 2-4× throughput on 8+ cores for read-heavy workloads

### Phase P2 — Double-Buffered Tick
- Two `World` states: `front` (read by network/client) and `back` (written by simulation)
- Atomic pointer swap at tick boundary; zero-copy for readers
- Eliminates mutex contention for `/state` and `/cmd` reads

### Phase P3 — Parallel Systems
- `std::execution::par_unseq` for: crop growth, weather diffusion, ore respawn, leaf litter
- NPC thread pool: `schedule_slot` + `bfs_path` per NPC concurrently
- World gen: parallel terrain passes (mountains || rivers || biomes || ore)

### Phase P4 — Async Persistence
- Background serialization thread with lock-free queue
- Incremental saves (dirty chunks only)
- Save compression (zstd) on worker thread
---

## Expanded Vision (A1–A9) — ship status

> The A1–A9 sections describe the broader game-design vision. Most core content is
> shipped; remaining/unfinished sub-goals are listed per section.

### A1. Buildings enterable, weathering, maintenance — ✅ largely shipped
- All buildings enterable, `BuildingState` weathering + `repair` shipped for all 22.
- **Remaining**: storms (severe-weather variant), NPC schedule disruption on condition < 20 fully realized, 🏚️ render severity tiers.

### A2. Interiors well-designed + minimap swaps — ✅ shipped
- All 22 interiors (many multi-floor), interactive furniture, client interior room panel.

### A3. Atmosphere for eventual horror — ✅ shipped (Phase 6)
- Geography/naming/narrator designed to enable horror.
- **Shipped**: under-map basement (after midnight), night encounters via chapter scripts, sanity meter with 4 perception tiers, internal voices (DDLC/Disco Elysium style), Higurashi cyclical secrets, PIXI fog/shadows/glitch/audio. See `docs/shipped-features.md` §24.

### A4. Full Stardew farming + extras — ✅ shipped (Phases 1 & 2)
- Shipped: scarecrow + crows, fertilizer tiers, trellis crops, fruit trees, composter, wind pollination, moon-phase farming, multi-season crops, sprinklers, **kegs, preserves jars, mayonnaise machine, bee houses, cask aging, greenhouse, skill perks (Tiller/Agriculturist)**. See `docs/shipped-features.md` §19–20.

### A5. Farmhouse upgrade + buyable plots + free-building — ✅ shipped
- 4 upgrade levels, 4 buyable plots, `place` structures inside owned plots.

### A6. Living world — 🟡 partial
- Shipped: leaf litter, rabbits, autumn fog.
- **Remaining**: forest_state byte + seasonal undergrowth, snowpath compaction, deer/owls/fisher-cats, storms knocking down trees, rain recharging well/pond, snow tracks, subterranean under-map layer.

### A7. Improved movement — 🟡 shipped modes 1/2/4
- Shipped: path-walk `go to <landmark>`, multi-tile `go <dir> <n>`, single-tile.
- **Remaining**: `explore <dir>` auto-walk (mode 3), `fasttravel <name>` teleport at in-game time cost (mode 5).

### A8. Primary interaction = MUD text — ✅ shipped
- Constraint honored: every mechanic is exercisable from MUD commands alone.

### A9. Agent may always ask — ✅ standing policy
- Ask the user whenever interior design, atmosphere, balance, MUD integration, or naming is underspecified.

---

## R17+. Long-horizon systems (deferred — sequence in a future doc)

- **A6 subterranean under-map layer** — parallel under-map terrain, saved but unrendered, accessible from a specific tile (e.g., Old Mill basement).
- **A7 movement modes 3 & 5** — `explore <dir>`, `fasttravel <name>`.
- **Parallelism phases P1–P4** — fine-grained locking, double-buffered tick, parallel systems, async persistence.

---

# Agent operational rules

1. **Read `docs/shipped-features.md` first** — know what's already live before adding new work.
2. For any `[Q]` step, stop and ask the user before writing code.
3. For every `[A]` step, write code, run the build, run `node --check` if you edited the client, commit + push.
4. After each step, print a 2-3 line summary and proceed automatically to the next.
5. The user can interject at any time to redirect.
6. If a step fails (compile/test), rollback the change, fix, retry. Don't move on with red builds.
7. One commit per atomic step. Conventional commit messages. Push after each commit.
8. Never commit a broken state.
9. Keep `docs/shipped-features.md` updated as new work ships, and trim completed items from this roadmap.
10. **Commit and push regularly** — Every logical change must be committed with a conventional commit message and pushed to `origin/main` before moving to the next task. No local-only commits.
11. **Extensive skill usage** — Agents MUST leverage available OpenCode skills whenever they are applicable (CMake, C++ coding standards, testing, documentation, MCP, WebSockets, etc.), and may download additional skills if required. Skill usage must be documented in the PR/commit.
12. **Verify before push** — Run the project's lint/typecheck/test commands (e.g., `cmake --build build && ctest`) before every push. If no commands are documented, ask the user for the verification command and add it to `AGENTS.md`.

---

This document is the source of truth for **future** work. If any later discussion
contradicts it, update the document first, then proceed.