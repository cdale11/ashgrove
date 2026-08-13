# Ashgrove Valley — Shipped Features

This document consolidates **everything that has shipped** across the map-redesign
roadmap (R0–R16), the expanded-vision addenda (A1–A9), and the forestation/tree
expansions. Each entry notes where it lives in the code so it can be found and
extended. The `docs/map-redesign-plan.md` roadmap now contains only unfinished,
deferred work.

---

## 1. Map & Geography (R0–R1)

- **Map size** bumped to 128×96 (`MAP_W`/`MAP_H` in `include/world.hpp`). Client
  already matched; save/load verified on the bigger map.
- **Valley bowl terrain** rebuilt in `generate_world()` (`src/world.cpp`):
  - Snow line, ring mountains, glacier lake in the NW corner.
  - Main N–S river at x≈44 (previously x=29).
  - NW tributary stream joining the main river.
  - Mountain lake **Lake Aurora** in the NE.
  - Southern ocean (bottom 3 rows).
  - **Whisper Wood** footprint (west, x≈4–22).
  - **East Moor** footprint (east, x≈78–100).
  - **Farm** footprint (south outskirts, west bank).
- `Tile::Cobble` (value 13) added to the tile enum; handled in water-edge passes.

## 2. Town Buildout (R2)

- All **22 buildings** placed per user-confirmed coordinates in `place_buildings()`
  (`src/world.cpp`).
- **Cobblestone roundabout** + central **statue** (`ObjType::Statue`) at the plaza.
- **3 bridges** (north y≈20, main y≈34, south footbridge y≈56).
- **Cobblestone main streets** (civic↔commerce loop) + dirt side roads.
- **Docks boardwalk** near the south ocean; **lakefront pier** at Lake Aurora.
- `clear_paths()` protects doorways/bridges and the farmhouse doorstep.

## 3. Wilderness & Ore (R3)

- Dense Whisper Wood with carved corridors for passability.
- East Moor with scattered pines + rocks.
- Mountain trail switchback up to the glacier lake.
- Ore redistribution: copper (mountain trail), iron (mid), gold (east ridges),
  iridium (rare).
- Dense border woodlands at map edges.

## 4. NPCs & Regions (R4)

- 5 NPCs re-anchored: Leah→Willow House, Abigail→Maple House, Elliot→Rowan
  Cottage, Robin→Home Field, Evelyn→Tearoom.
- Full daily schedules rewritten per NPC in `npc_schedule()`.
- `region_at()` recognizes the full named-region list (Stardrop Plaza, Whisper
  Wood, East Moor, Lake Aurora, Frostveil Tundra, Ashgrove Farm, Docks, etc.).

## 5. Movement Overhaul (R5)

- `Player::known_landmarks` set (serialized) — buildings learned on `enter`.
- `go` command supports:
  - **Path-walk**: `go to <landmark>` via BFS.
  - **Multi-tile**: `go <dir> <n>` (e.g. `go 5 north`, `go e 10`).
  - **Single-tile** (unchanged).
- Landmark registration hook in `enter`; help text updated.

## 6. Client Rendering (R6)

- `Tile::Cobble` render case with grey-tan gradient.
- `ObjType::Statue` glyph (🗿) at the roundabout.
- Seasonal tree-glyph lookup keyed by `season_index(day)` + region tag.
- New building emoji glyphs (🎣 Fish Shack, 🗼 Lighthouse, 🔨 Carpenter Shop,
  🍵 Tearoom, 🔭 Observatory, 🐕 Pet Shop).

## 7. Farmhouse Interior Redesign (R7)

- Farmhouse interior rewritten as a **7×9 traditional farmhouse** (bed, TV,
  table, stove, counter, fridge, shelf, chair).
- Every piece of furniture interactive via `interact` (bed→sleep, TV→forecast,
  stove→cook, counter→prep, fridge→store, shelf→browse, table→dine, chair→sit).
- **Interior room panel** in the client (`renderRoom()`, `assets/index.html`)
  shown while `inside` — replaces the world map, with furniture glyphs
  (`FURN_GLYPH`), player marker 🧭, and wall/door/floor tiles.

## 8. Farmhouse Weathering & Maintenance (R8)

- `BuildingState` struct (condition / roof_leak / foundation / last_maintained_day).
- Per-day decay in `World::tick()`: rain → `roof_leak`, winter → `foundation`.
- `repair farmhouse` command at the Carpenter Shop (10 wood + 5 stone per 10
  condition), restores to 100.
- Condition shown in `status`; decay-triggered flavor text ("The roof groans…").

## 9. Living World Proof-of-Concept (R9)

- `ObjType::LeafLitter` autumn overlay near deciduous trees in Whisper Wood.
- **Rabbits** (non-hostile NPCs) near the farm; at dawn they eat an adjacent
  un-harvested crop if no fence/scarecrow.
- **Autumn fog** chance in the mornings; reduces `look` to a 1-tile radius.

## 10. Farming: Scarecrow + Fertilizer (R10)

- **Scarecrow** object + crow logic: each night un-scarecrowed mature crops have
  a 5% chance each to be eaten. Scout covers 17×17; quality 99-tile.
- **Fertilizer tiers** (Basic/Quality/Premium) applied to tilled soil before
  planting, boosting growth + sell value.

## 11. Farming: Trellis Crops + Fruit Trees (R11)

- **Trellis crops** (green beans, hops) — impassable while growing, harvestable.
- **Fruit trees** (apple, cherry, peach, pomegranate) — 28 days to mature,
  produce once per season after.

## 12. Farmhouse Upgrade (R12)

- `FarmhouseLevel` in `World` state (1–4).
- `upgrade farmhouse` command at Carpenter Shop:
  - **Cottage** (2): 10,000g + 350 wood — adds kitchen + bedroom.
  - **House** (3): 50,000g + 450 wood + 200 stone — adds cellar + study.
  - **Manor** (4): 100,000g + premium materials — adds nursery + verandah.

## 13. Farming: Composter + Wind Pollination + Moon Phase (R13)

- **Composter** machine — place on farm, add fiber/weeds, harvest fertilizer
  after 4 days.
- **Wind pollination** — flowers adjacent to crops boost crop tier chance at
  harvest (20% per flower for 2× sell price).
- **Moon-phase farming** — `moon_phase(day)` returns 0..7; new moon = 10% faster
  growth for crops planted that day.
- Expanded to **16 new crops** and **12 new fruit trees**.

## 14. All 22 Building Interiors + Multi-Floor (R14)

- Hand-designed, functional interiors for every building, many multi-floor:
  - **Civic (6)**: Town Center (2F), Clinic, Museum (2F), Old Mill, Stardrop
    Saloon (3F), Farmhouse (enhanced).
  - **Commerce (5)**: Blacksmith, General Store, Market, Carpenter Shop, Pet Shop.
  - **Residential (4)**: Willow House, Maple House, Rowan Cottage, Hawthorne Cottage.
  - **Travel (2)**: Bus Stop, Railway Station (2F).
  - **Farm (2)**: Hawthorn Barn, Glasshouse.
  - **Lakefront (2)**: Tearoom (2F), Observatory (3F).
  - **Docks (2)**: Fish Shack, Lighthouse (4F).
- `InteriorRoom` supports multiple floors with stairs (`<>`) connecting them.
- Interactive furniture letters mapped to actions (bed, TV, counter, forge, etc.).
- Client room panel renders multi-floor interiors.

## 15. Forestation & Tree Products (added alongside R14)

- **11 forestation tree types**: Oak, Maple, Birch, Cedar, Redwood, Teak,
  Mahogany, Rubber Tree, Walnut, Hickory, Chestnut (plus **Deodar**).
- Tree products: Sap, Resin, Rubber, Bark, Hardwood, Maple Syrup, Oak Resin,
  Pine Tar, Deodar Resin/Oil.
- Seeds/saplings for all types; logs per tree (Oak Log, Maple Log, …).
- Lumber processing: Lumber, Plank, Plywood.
- Nuts: Walnut, Hickory Nut, Chestnut, Acorn.
- `planttree <tree>` command to plant forestation trees.
- `tap` command to install/collect tappers for sap/syrup/resin/rubber.
- Tree growth simulation (hp increases over time).
- Axe drops appropriate logs; hardwood trees give Hardwood + logs.
- All tree products edible with energy values.

## 16. Tree / Weed Mechanics (completed alongside R14)

- Saplings drop from mature trees when chopped (20% chance for hp > 100).
- `shake` command: shake mature trees for saplings (25% chance, costs 2 energy).
- Weeds give random mixed seeds when cut with scythe (15% chance).

## 17. Building Weathering & Repair for All 22 (R15)

- Decay wired into all 22 `BuildingState` entries, tuned per district (docks
  decay faster, civic slower, mountain-trail buildings need roofwork each winter).
- General `repair <building>` command at the Carpenter Shop.
- Building condition < 20 renders 🏚️ and blocks NPC entry (owner-NPC stays home).
- **Fix**: repaired a crash where the repair handler `delete`d a pointer into the
  `w.buildings` vector — guarded so only the dynamically-allocated Farmhouse case
  is freed.

## 18. Buyable Plots + Placeable Structures (R16)

- `buy plot` / `buy plot <name>` at the **Town Center** (list + purchase):
  - **Hillside** — 15,000g, cool mountain air.
  - **Forest Clearing** — 8,000g, temperate woodland.
  - **Lakeside** — 12,000g, humid lake breeze.
  - **Docks Lot** — 10,000g, salty coastal wind.
- `place barn|silo|shed|well|windmill` inside an owned plot, with gold + wood +
  stone (+fiber) costs and ownership/boundary/occupancy guards.
- `plots` / `deeds` command lists plots with ownership + structure count
  (structure count also scans the plot's map cells for `ObjType::Building`).
- `owned_plots` + `placed_structs` serialized in save/load.
- Help text updated.

---

## Current command surface (MUD)

Core: `help`, `status`/`stats`, `inventory`/`inv`, `time`, `look`/`l`,
`go`/`move` (+ dirs, multi-tile, `go to <landmark>`).

Farming/tools: `hoe`/`till`, `plant`, `water`, `harvest`, `fertilize`,
`axe`/`chop`, `pick`/`mine`, `scythe`/`cut`, `planttree`/`forest`, `tap`,
`shake`.

World: `fish`, `forage`, `search`, `shop`, `buy`, `sell`, `craft`, `place`.

Buildings: `enter`, `exit`/`leave`, `interact`/`use`, `repair`, `upgrade
farmhouse`, `train`, `bus`, `tv`/`watch`.

Social/life: `talk`, `gift`, `hearts`/`friends`, `eat`, `sleep`/`rest`,
`festival`/`fest`.

Land/persistence: `buy plot`, `plots`/`deeds`, `save`, `load`, `saves`,
`newgame`.

---

## Commits (recent, on `origin/main`)

- `24d41bb` docs: confirm original checklist complete
- `725a422` docs: mark R5.5/R7.4/R7.5/R8.5/R15 complete on roadmap
- `401288e` feat: buyable plots + placeable structures (R16)
- `c1cbf4c` fix: avoid deleting vector element in repair command
- `80ee542` Fix: farmhouse entry — relocate barn/glasshouse, clear doorstep
- `9b85f1c` Fix: initialize building_states for farmhouse condition checks
- `d92f989` feat: R15 building weathering + Deodar tree + natural regrowth
- `7bb2428` feat: Stardrop Saloon 3-floor inn enhancement
- `461ae90` feat: R14 all 22 building interiors + multi-floor support
- `a4710a5` feat: Stardew-style tree/weed mechanics + shake command
- `468470a` feat: forestation + tree products + tap system + crop/tree expansion
- `181a85c` feat: R13 composter + wind pollination + moon phase + crops/trees
- `be95756` feat: R12 farmhouse upgrade + more crops/fruit trees
- `7e43a16` feat: R11 trellis crops + fruit trees
- `945b407` feat: R10 scarecrow + fertilizer
- `9f59b2c` feat: R9 living world (leaf litter, rabbits, autumn fog)
- `9be6dd9` feat: R8 farmhouse weathering + maintenance
- `92fb769` feat: R7 farmhouse interior redesign
- `12ba2e8` feat: town buildout + movement overhaul (R2, R5, R6)
- `eff9bc5` feat(world): valley bowl geography at 128x96