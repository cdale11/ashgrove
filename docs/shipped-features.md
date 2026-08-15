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

## 19. Phase 1 Core Stardew Features

### Seasonal Festivals
- One festival per season, on **day 13** of each season:
  - **Spring Fair** (Spring 13) — egg hunt via `search` (`fest_eggs` / `fest_tries`).
  - **Summer Luau** — one-time reward (+120g, Blueberry).
  - **Autumn Harvest Festival** — one-time reward (+150g, Pumpkin).
  - **Winter Star Festival** — one-time reward (+100g, Wine).
- Rewards guarded by `Player::festival_claimed_day` (serialized).
- `is_festival_day(day)`, `festival_name(day)`, `next_festival_day(day)` in `src/world.cpp`;
  `/state` `festival` field shows the current festival.

### Fishing
- `Fishing Rod` is starter gear; `Bait` purchasable (+25 skill).
- Location-based fish tables in the `fish` command (`src/main.cpp`): Ocean (Seaglass Docks),
  River, Lake (Lake Aurora), Mountain (Whisper Wood) — each with 5 species × 4 seasons and
  time-of-day availability windows.
- Reeling mini-game: catch skill = `50 + energy/10 + bait(+25) + rainy(+10)`, deterministic roll.
- Catches add `Item::Fish` and print the named species.

### Cooking & Recipes
- 14 recipes in the `cook` command (`src/main.cpp`): bread, salad, omelet, cheese omelet,
  milk pudding, fruit salad, jam toast, fish stew, pumpkin soup, grain porridge, honey bread,
  grilled fish, fruit tart, cheese plate.
- `cook <recipe>` validates + consumes ingredients, produces a dish item; dishes are edible
  and restore energy via the `eat` command's food table.

### Animals
- `build barn` / `build coop` create buildings with capacity 4 each.
- `place <animal> <building>` (chicken/cow/goat) costs 1 forage; capacity- and ownership-guarded.
- `feed <building>` consumes 1 forage per hungry animal; feeding raises friendship.
- `collect` requires standing next to a barn/coop; friendship ≥80 doubles yield; hungry
  animals (hunger ≥100) stop producing.
- Daily tick in `advance_day`: age, hunger (+20/day), friendship (rises fed / falls hungry).
- `Animal::friendship` (0–100) serialized; `BuildingState::capacity` field added.

### Crops (expanded + giant)
- **11 new crops** across seasons: tulip, blue jazz (Spring); starfruit, poppy (Summer);
  amaranth, yam, eggplant, okra, beet (Fall); ancient fruit, sweet gem berry (special).
  Added to `Item` enum, `item_def`, all three crop tables, shop listing, and sell map.
- **Giant crops**: mature 3×3 patches of cauliflower/melon/pumpkin have a 1%/night chance
  to merge (marked via `last_harvest_season == 99`); harvesting yields 6–12 produce.
- Crop catalogue now 45 entries (excluding fruit trees), progressing toward the 100+ target.

## 20. Phase 2: Advanced Crafting & Machines

### Processing Machines
All machines are craftable (`craft <machine>`), placeable (`place <machine>`), loaded via `interact add`, and collected via `collect`. Daily processing runs in `advance_day()`.

- **Keg** — Ferments fruit → wine (7 days), hops → pale ale (2 days), wheat → beer (7 days), coffee → coffee (1 day), rice → sake (3 days), honey → mead (10 days).  
  *Recipe: 30 Wood + 1 Copper Bar + 1 Iron Bar + 1 Oak Resin.*
- **Preserves Jar** — Fruit → jelly (3 days), vegetables → pickles (2 days).  
  *Recipe: 50 Wood + 40 Stone + 8 Coal.*
- **Mayonnaise Machine** — Eggs → mayonnaise (3 hours).  
  *Recipe: 15 Wood + 15 Stone + 1 Earth Crystal + 1 Copper Bar.*
- **Bee House** — Produces honey daily; wild honey (bonus value) when placed near flowers (tulip, blue jazz, poppy, sunflower, sweet pea, fairy rose).  
  *Recipe: 40 Wood + 8 Coal + 1 Iron Bar + 1 Maple Syrup.*
- **Cask** (cellar only) — Ages wine/cheese through quality tiers: normal → silver (14 days) → gold (21 days) → iridium (28 days).  
  *Recipe: 20 Hardwood + 50 Wood + 20 Stone.*

### Greenhouse
- `build greenhouse` creates a 10×6 building. Tilled tiles inside the greenhouse grow crops year-round (winter/season restrictions bypassed). Implemented by checking tile containment in `plant` command and `advance_day`.

### Skill System & Perks
- **Farming skill** (0–10) tracked on `Player::farming_level`.
- **Agriculturist perk** (`perk_agriculturist`): Crops grow 10% faster (applied in `advance_day` via growth multiplier).
- **Tiller perk** (`perk_tiller`): Crops sell for 10% more (applied in `sell` command).
- Perk flags stored on `Player`, checked globally in daily tick and sell command.

### New Items
- **Machine items**: Keg, PreservesJar, MayonnaiseMachine, BeeHouse, Cask, Greenhouse.
- **Machine outputs**: Pale Ale, Beer, Sake, Mead, Coffee, Rice, Hot Pepper, Jelly, Pickles, Wild Honey, Aged Wine, Aged Cheese.
- **Egg variants**: Large Egg, Brown Egg, Large Brown Egg, Duck Egg, Void Egg, Dinosaur Egg.
- **Resources**: Coal, Earth Crystal.
- **Vegetables**: Cucumber, Carrot, Radish.
- **Flowers**: Sunflower, Sweet Pea, Fairy Rose.

### Commands Extended
- `craft keg|preserves jar|mayonnaise machine|bee house|cask`
- `place keg|preserves jar|mayonnaise machine|bee house|cask`
- `interact add|put|fill` (load machines), `collect` (retrieve from machines)
- `build greenhouse`
- `sell` (Tiller perk: +10% crop value)
- `advance_day` / `sleep` (Agriculturist perk: +10% crop growth speed)

---

## 21. Phase 3: Social & NPC Relationships

### Gift Preference System
- **Comprehensive tables** for 5 villagers (Leah, Abigail, Elliot, Robin, Evelyn) + 2 rabbits across 100+ items (forage, fish, crops, minerals, artisan goods).
- **5-tier tastes**: Love (+2), Like (+1), Neutral (0), Dislike (-1), Hate (-2).
- Covers 100+ items: forage, fish, crops, minerals, artisan goods, eggs.

### Hearts System (0–14)
- **8 hearts**: Give Bouquet → engagement.
- **10 hearts**: Give Wedding Ring → marriage.
- **14 hearts**: Max (spouse at max).
- `hearts`/`friends` command shows 14-heart bars + spouse/children.

### Marriage & Family
- `gift <npc> Bouquet` at 8 hearts → engagement.
- `gift <npc> Wedding Ring` at 10 hearts → marriage, spouse moves in.
- `divorce <spouse>` → ends marriage, hearts drop.
- **Children**: After 14 days marriage + nursery built, 5%/day chance for child (random boy/girl names); `hearts` command shows spouse + children.

### Heart Mechanics
- **Decay**: Ungifted NPCs lose 1 heart/week (every 7 days); spouse exempt.
- **Daily gift limit**: 1 gift per NPC per day tracked.
- **Roommate events**: At 8+ hearts, 2%/day chance to ask to stay over.

### NPC Schedule Adaptation
- NPCs with ≥8 hearts: 20% chance to visit player's farm instead of normal schedule.
- Spouse follows player or stays in farmhouse.

### Dialogue
- `talk` command enhanced with spouse-specific lines ("Love you"), jealousy lines.
- LLM-driven dialogue attempted with fallback to seasonal greetings.

### New Items
- Bouquet (200g), Wedding Ring (5000g).

### Commands Added/Extended
- `gift <npc> <item>` (with Bouquet/Wedding Ring logic)
- `hearts`/`friends` (14-heart display + spouse/children)
- `divorce <spouse>`
- `talk <npc>` (enhanced with LLM fallback)

---

## 22. Phase 4: Town & Map Expansion

### Infinite Chunk System
- **Chunked world**: 128×128 tile chunks, up to 8 chunks radius from origin (1024 tiles each direction).
- **ChunkCoord** key for `std::map<ChunkCoord, Chunk>` storage.
- **On-demand generation**: Chunks generated on-demand when player enters or structure placed.
- **Region system**: Pre-defined regions (Forest, Hills, Mountains, Caves, Ruins, Ocean, Swamp) with configurable radius and seed.

### Procgen Regions
- **8 region types**: Valley (core), Forest, Hills, Mountains, Caves, Ruins, Ocean, Swamp.
- **Region attributes**: Type, center chunk, radius (in chunks), seed.
- **Per-chunk generation**: Terrain, objects, NPCs based on region type.
  - Forest: Dense trees, rabbits.
  - Hills: Rocky terrain, scattered rocks.
  - Mountains: Ice/snow peaks, rocks.
  - Caves: Dirt terrain, cave entrances.
  - Ruins: Rubble, statues, ancient structures.
  - Swamp: Water/grass mix.
  - Ocean: Water tiles.

### New Interior Rooms (12+)
- **Barn Interior** (2 floors): Hay loft, animal stalls, feed troughs, milking stations.
- **Greenhouse Interior**: Year-round growing beds, water tanks, tool benches.
- **Cellar Interior**: Cask rows, wine racks, tasting table.
- **Shrine Interior** (2 floors): Altar, candles, offerings, catacombs.
- **Cabin Interior**: Simple shelter with bed, kitchen, storage.
- **Ruin Interior** (2 floors): Pillars, rubble, treasure/traps.
- **Cave Interior** (2 floors): Stalactites, ore veins, crystals.
- **Well Interior**: Water, bucket, rope.
- **Windmill Interior** (2 floors): Grain hoppers, millstones, flour output, gears.
- **Silo Interior** (2 floors): Grain storage, conveyor.
- **Shed Interior**: Tools, workbench, shelves.
- **Well/Windmill/Silo/Shed Interiors**: Functional interiors for placeable structures.

### Infinite Map Navigation
- **Chunk-based coordinates**: Global → chunk + local conversion.
- **Universal cell access**: `cell_at(gx, gy)` handles chunk 0,0 and generated chunks.
- **Global walkable check**: `walkable_global(gx, gy)` for pathfinding across chunks.
- **Chunk generation**: On-demand with region-based terrain generation.

### DSL Construction System
- **Text-based placement**: `dsl <structure> @ x,y` or `dsl <dsl_string>`.
- **Example**: `dsl barn @ 110,30; coop @ 115,30; silo @ 120,30`.
- **Parser**: Handles `name @ x,y` or `name x y` syntax.
- **Validation**: Checks plot ownership, tile availability, resources.
- **Supported structures**: barn, coop, silo, shed, well, windmill, greenhouse, shrine, cabin.

### New Commands
- `dsl <structure> @ x,y` / `dsl <dsl_string>` — Batch construct structures on owned plots.
- `explore` / `map` — Show current chunk and adjacent chunks with building counts.
- `travel <chunk_x> <chunk_y>` — Fast-travel to adjacent explored chunks.
- `region add <type> @ cx,cy radius` — Generate procgen region (forest, hills, mountains, caves, ruins, swamp, ocean).

### New Items
- **Machine items**: Keg, PreservesJar, MayonnaiseMachine, BeeHouse, Cask, Greenhouse.
- **Machine outputs**: Pale Ale, Beer, Sake, Mead, Coffee, Rice, Hot Pepper, Jelly, Pickles, Wild Honey, Aged Wine, Aged Cheese.
- **Egg variants**: Large Egg, Brown Egg, Large Brown Egg, Duck Egg, Void Egg, Dinosaur Egg.
- **Resources**: Coal, Earth Crystal.
- **Vegetables**: Cucumber, Carrot, Radish.
- **Flowers**: Sunflower, Sweet Pea, Fairy Rose.
- **Marriage items**: Bouquet (200g), Wedding Ring (5000g).

### Commands Added/Extended
- `craft keg|preserves jar|mayonnaise machine|bee house|cask`
- `place keg|preserves jar|mayonnaise machine|bee house|cask`
- `interact add|put|fill` (load machines), `collect` (retrieve from machines)
- `build greenhouse|shrine|cabin`
- `dsl <structure> @ x,y` / `dsl <dsl_string>`
- `explore` / `map`
- `travel <chunk_x> <chunk_y>`
- `region add <type> @ cx,cy radius`
- `build shrine|cabin`
- `sell` (Tiller perk: +10% crop value)
- `advance_day` / `sleep` (Agriculturist perk: +10% crop growth speed)
- `hearts`/`friends` (14-heart display + spouse/children)
- `divorce <spouse>`
- `talk <npc>` (enhanced with LLM fallback)

---

## 23. Phase 5: Quest & Job System

### Quest System
- **Dynamic quest generation** sampling templates + world state (season, NPC mood, weather, economy).
- **5 quest types**: fetch, deliver, investigate, ritual, kill
- **Auto-generation**: 2-3 quests per day, expires in 3 days
- **Rewards**: Money (50-1000g scaled by target count), items based on quest type
- **Quest tracking**: Active quests, completed history, expiration handling

### Job Board
- **4 job types**: farmhand, miner, courier, researcher
- **Daily repeatable work** with cooldown system
- **Immediate rewards** on completion: money + items
- **Job descriptions** with thematic flavor text

### Living Economy
- **Supply/demand price fluctuations** updating daily
- **Seasonal pricing**: In-season items cheaper, out-of-season more expensive
- **Market price tracking** for 17+ commodities (crops, animal products, resources, ores, bars)
- **Trend indicators** showing price direction (▲ ▼)

### Commands Added/Extended
- `quest [list|complete <id>|history]` — View active quests, complete for rewards, see history
- `job [list|do <id>]` — View job board, start jobs for rewards
- `market` — View current market prices with supply/demand data and trends

---

## 24. Phase 6: Horror & Narrative Overlays

### Sanity Meter
- Per-player `sanity` (0–100) + `max_sanity`, serialized in saves.
- Daily drift in `tick_sanity`: drains past 22:00 and harder past midnight, slightly on rainy days, and steeply inside the basement; recovers during calm daylight (8:00–18:00).
- `restore_sanity` / `damage_sanity` helpers; `find_secret()` restores a little clarity.

### Perception Filters
- 4 tiers derived from sanity via `perception_tier()`: **Sane (≥75) → Uneasy (50) → Strained (25) → Fractured (<25)**.
- `look` gains ambient horror flavor at Uneasy+, phantom sightings at Strained, and fractured-state internal voices at Fractured.
- Exposed to the client as `sanity` / `sanity_tier` in `/state`.

### Under-Map Basement
- The hatch under the farmhouse is reachable only after midnight (hour ≥ 24).
- `basement` command and `/basement` HTTP endpoint (subcmd `enter`/`leave`).
- Entering drains 10 sanity, unlocks `basement_unlocked`, increments `basement_visits` and `horror_cycle` (Higurashi-style looping), and places the player in the `Basement` interior.
- `exit`/`leave` returns the player above ground.

### Night Events (Chapter-Style)
- Sleeping at/after 22:00 rolls a deterministic chapter-style night event (`roll_night_event()`), sampling season, weather, and day.
- Titles include "The Hollow Well", "The Clock Stops", "The House That Breathes", etc.
- Appended to a per-player `night_event_log` (bounded at 12) and displayed on waking.

### Fourth-Wall / Internal Voices
- At Strained/Fractured sanity, `internal_voice()` injects Disco-Elysium/DDLC-style self-aware monologue lines into command output.

### Higurashi-Style Cyclical Secrets
- `find_secret()` records named secrets per player; each restores sanity.

### Client (PIXI) Effects
- **SANITY** readout in the topbar (color-coded by tier).
- **Night vignette shadow** growing after 20:00.
- **Drifting fog** bands at night, in the basement, or at low sanity (stronger with strain).
- **Glitch scanline overlay** scaled to sanity tier (0 → 0.85 opacity).
- **Web Audio drone cues** (procedural, no assets) that thicken and detune as sanity drops.

### Commands/Endpoints Added
- `basement` (command) — enter the hidden under-map after midnight
- `POST /horror` — sanity, tier, basement visits, cycle, active narrative, secrets, night log
- `POST /basement` — subcmd `enter`/`leave`
- `/state` now includes `sanity` and `sanity_tier`

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

Animals/cooking: `build barn|coop`, `place <animal> <building>`, `feed
<building>`, `collect`, `cook <recipe>`, `eat`.

Social/life: `talk`, `gift`, `hearts`/`friends`, `eat`, `sleep`/`rest`,
`festival`/`fest`.

Horror/narrative: `basement`, `horror` (sanity, cycles, secrets, night log).

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