# Ashgrove Valley — Legacy Completion Plan
## Finishing Open Items from Map-Redesign Roadmap (Phases 1-6)

**Purpose**: Complete all explicitly deferred/remaining work from `docs/map-redesign-plan.md` before advancing to Conscious Town (Phase 7+). These are concrete, scoped features with clear acceptance criteria.

> **Status (updated 2026-08-16)**: Groups 1-4 (L1-L9, L11, L12) are **complete** — shipped in commit `764ccbe`. L10 (subterranean) and P1-P4 (parallelism) remain intentionally deferred to Phase 9/10. This document is retained for reference; the shipped work is also recorded in `docs/shipped-features.md`.

---

## Open Items Inventory (from `map-redesign-plan.md`)

| ID | Source | Item | Status | Effort |
|----|--------|------|--------|--------|
| L1 | A1 | Severe-weather storm variant | ✅ Shipped (`764ccbe`) | M |
| L2 | A1 | NPC schedule disruption when building condition < 20 | ✅ Shipped (`764ccbe`) | S |
| L3 | A1 | 🏚️ Render severity tiers for building decay | ✅ Shipped (`764ccbe`) | S |
| L4 | A6 | `forest_state` byte + seasonal undergrowth | ✅ Shipped (`764ccbe`) | M |
| L5 | A6 | Snowpath compaction | ✅ Shipped (`764ccbe`) | S |
| L6 | A6 | Wildlife: deer, owls, fisher-cats | ✅ Shipped (`764ccbe`) | M |
| L7 | A6 | Storms knocking down trees | ✅ Shipped (`764ccbe`) | M |
| L8 | A6 | Rain recharging well/pond | ✅ Shipped (`764ccbe`) | S |
| L9 | A6 | Snow tracks (visual + tracking) | ✅ Shipped (`764ccbe`) | S |
| L10 | A6 | Subterranean under-map layer (parallel terrain) | ⏳ Deferred (Phase 9) | L |
| L11 | A7 | `explore <dir>` auto-walk (mode 3) | ✅ Shipped (`764ccbe`) | M |
| L12 | A7 | `fasttravel <name>` teleport at in-game time cost (mode 5) | ✅ Shipped (`764ccbe`) | M |
| P1 | Parallelism | Fine-grained locking (split World mutex) | ⏳ Deferred (Phase 10) | L |
| P2 | Parallelism | Double-buffered tick (front/back World) | ⏳ Deferred (Phase 10) | L |
| P3 | Parallelism | Parallel systems (`std::execution::par_unseq`) | ⏳ Deferred (Phase 10) | L |
| P4 | Parallelism | Async persistence (background serialization) | ⏳ Deferred (Phase 10) | L |

---

## Prioritization & Grouping

### Group 1: Atmosphere & Polish (Low Risk, High Player Visibility) — **Do First**
- L1 Severe storms (extends Phase 6 weather/horror)
- L3 Building render severity tiers (visual feedback for existing weathering)
- L5 Snowpath compaction (winter feel)
- L8 Rain recharging well/pond (resource logic)
- L9 Snow tracks (tracking gameplay + visual)

### Group 2: Living World Depth (Extends Phase 4/6 Ecology) — **Do Second**
- L4 `forest_state` byte + seasonal undergrowth (data foundation for forest evolution)
- L6 Wildlife: deer, owls, fisher-cats (ambient life, not full simulation yet)
- L7 Storms knocking down trees (connects weather + forest)

### Group 3: Movement Completion (Finishes Phase 5/R5) — **Do Third**
- L11 `explore <dir>` auto-walk
- L12 `fasttravel <name>` with time cost

### Group 4: NPC Responsiveness (Extends Phase 3) — **Do Fourth**
- L2 NPC schedule disruption at condition < 20

### Group 5: Major Systems (Defer Until After Conscious Town Core) — **Defer**
- L10 Subterranean under-map layer → merge with Phase 8.1e forest + Phase 9 questline
- P1-P4 Parallelism → Phase 10 infrastructure work

---

## Detailed Specs & Acceptance Criteria

### L1: Severe-Weather Storm Variant
**Design**: Rare (1-2%/day in spring/summer, 0.5% fall/winter) intense storm event lasting 6-12 in-game hours.
- **Weather fields**: Pressure drop >20 hPa, wind 30-50 m/s, precipitation 3-5× normal, lightning strikes.
- **Effects**: 
  - Crops: 10-30% flattened (recoverable), 5% destroyed
  - Trees: 1-5% windthrow per chunk (creates nurse logs, gaps — feeds L7/L4)
  - Buildings: Condition -5 to -15 (triggers L2/L3)
  - NPCs: Seek shelter, schedules suspended, unique storm dialogue
  - Player: `look` shows dramatic prose; sanity drain if outside; lightning can strike near player (rare)
- **Visual (PIXI)**: Dark sky, heavy rain particles, wind-bent vegetation, lightning flashes, screen shake.
- **Acceptance**: Storm triggers, runs course, leaves persistent world changes (windthrows, building damage, crop loss).

### L2: NPC Schedule Disruption at Building Condition < 20
**Design**: When an NPC's home/workplace condition drops below 20:
- **Schedule rewrite**: NPC spends 50% of "work" time attempting `repair` (if materials) or `search` for materials.
- **Dialogue**: Complains about disrepair, asks player for help.
- **Relationship**: Heart decay accelerates (-2/week instead of -1) until condition ≥ 40.
- **Extreme (<10)**: NPC may temporarily relocate to Town Hall / inn (schedule shows "displaced").
- **Acceptance**: NPC behavior visibly shifts; player can intervene; condition restoration normalizes schedule.

### L3: Building Render Severity Tiers
**Design**: Client renders building sprites with condition-based overlays:
| Tier | Condition | Visual |
|------|-----------|--------|
| Pristine | ≥90 | Normal |
| Weathered | 60-89 | Faded paint, moss at base |
| Damaged | 30-59 | Cracked walls, missing shingles, boarded window |
| Ruin | 10-29 | Gaping holes, collapsed roof section, vines |
| Collapsed | <10 | Rubble pile, only foundation visible |
- **Implementation**: `BuildingState.condition` → sprite variant index; PIXI loads `building_<name>_tier<N>.png` or applies shader overlays.
- **Acceptance**: Visual state matches condition at all times; transitions smooth.

### L4: `forest_state` Byte + Seasonal Undergrowth
**Design**: Per-tile `uint8_t forest_state` (packed bitfield):
```
bits 0-2: canopy_density (0-7)     // affects light, undergrowth
bits 3-4: undergrowth_type (0-3)   // 0=none, 1=fern, 2=berry, 3=mushroom
bit 5:  has_nurse_log
bit 6:  recent_windthrow (decays yearly)
bit 7:  player_managed (planted/protected)
```
- **Seasonal update** (in `advance_day` at season change):
  - Spring: undergrowth_type → berry/fern (moisture-dependent), canopy_density +1 for deciduous
  - Summer: canopy_density max, berry → ripe (forageable)
  - Autumn: canopy_density -1 (leaf fall), mushroom bloom (undergrowth_type=3)
  - Winter: canopy_density min (deciduous), undergrowth dormant (type=0), snow cover
- **Forage interaction**: `forage` checks `undergrowth_type` + season → yields.
- **Acceptance**: Tile state visible in `look`/`survey`; forage yields vary correctly; state persists save/load.

### L5: Snowpath Compaction
**Design**: Repeated player/NPC movement on snow tiles compacts them:
- **State**: `uint8_t snow_compaction` per tile (0=fluffy, 255=ice).
- **Update**: Each footfall +5 compaction; daily decay -2 (temp-dependent).
- **Effects**: 
  - Movement cost: fluffy 2.0×, packed 1.2×, ice 0.8× (slippery).
  - Visual: PIXI shader — fluffy=soft white, packed=dense, ice=glossy blue-white.
  - Tracks: Recent footfalls visible as darker depressions (decay over hours).
- **Acceptance**: Paths form naturally; visual distinct; movement speed reflects compaction.

### L6: Wildlife (Deer, Owls, Fisher-Cats)
**Design**: Ambient wildlife entities (not full NPCs) — spawn in appropriate biomes, follow simple behaviors.
- **Deer**: Forest/edge biomes. Dawn/dusk grazing. Flee from player/predators. Drop venison/hide on death. Browse seedlings (reduces forest regeneration — feeds 8.1e).
- **Owls**: Nocturnal. Perch in large trees. Hunt rodents (pest control — reduces crop pest pressure). Hoot at night (ambient sound/PIXI).
- **Fisher-Cats**: Rare, deep forest. Territorial. Prey on rabbits/small game. Aggressive if cornered.
- **Implementation**: `Wildlife` struct (type, position, home_range, activity_schedule, state). Updated in parallel chunk tick. No pathfinding — local steering only.
- **Acceptance**: Encounterable in appropriate biomes; behaviors observable; drops/ecological effects work.

### L7: Storms Knocking Down Trees
**Design**: During severe storm (L1) or high-wind events:
- **Per-tree check**: `windthrow_prob = f(wind_speed, tree_height, wood_density, root_depth, soil_saturation, canopy_sail_area)`.
- **Outcome**: 
  - Uprooted: Tree becomes `nurse_log` (L4 bit 5), creates canopy gap, root plate exposed.
  - Snapped: Trunk remains (snag), crown becomes debris.
  - Leaned: Condition reduced, growth redirected (reaction wood).
- **Aftermath**: Gap → light pulse → undergrowth surge → succession (feeds 8.1e).
- **Acceptance**: Windthrows occur during storms; create persistent forest changes; player sees results.

### L8: Rain Recharging Well/Pond
**Design**: Groundwater recharge from precipitation:
- **Well**: `water_level += rainfall_mm * catchment_area * infiltration_rate`. Daily decay (usage + seepage).
- **Pond/Lake**: Level rises with runoff; overflows to river.
- **Drought**: Extended dry period → well level drops → `draw_water` yields less / fails.
- **Player feedback**: `look well` shows "water table high/normal/low/dry".
- **Acceptance**: Well/pond levels track rainfall; drought has gameplay impact.

### L9: Snow Tracks
**Design**: Visual + gameplay tracking in winter:
- **Track creation**: Entity (player, NPC, deer, rabbit) leaves track segment on snow tile.
- **Track aging**: `track_age` increments each tick; visibility fades (wind, new snow).
- **Tracking skill**: `search` on track tile → reveals entity type, direction, age (hours).
- **PIXI**: Track sprites overlay on snow; direction arrows; fade shader.
- **Acceptance**: Tracks visible; `search` extracts info; wind/snowfall erases over time.

### L11: `explore <dir>` Auto-Walk (Mode 3)
**Design**: `explore north` — player auto-moves in direction until:
- Landmark discovered (building, region boundary, resource node)
- Obstacle impassable (water, cliff, dense forest without path)
- Player types any command (interrupts)
- Energy < 20% (auto-stops, warns)
- **Speed**: 1 tile/tick (same as `go`), but continuous until stop condition.
- **Output**: Prose narration of passed terrain ("You walk north through whispering pines, the ground rising steadily...").
- **Acceptance**: Command works; stops at correct conditions; narrates; interruptible.

### L12: `fasttravel <name>` Teleport (Mode 5)
**Design**: Teleport to known landmark at in-game time cost:
- **Prerequisites**: Landmark in `known_landmarks` (visited at least once).
- **Time cost**: `base_hours = distance_chunks * 0.5` + `terrain_modifier` (forest +0.5, mountain +1.0, road -0.5).
- **Constraints**: Cannot fast-travel if: encumbered (>80% weight), in combat, sanity < 25, in basement, during festival.
- **Effect**: Player position = landmark entrance; time advances; energy -10%; sanity -5 (disorientation).
- **Narration**: "You make your way to the Carpenter's Shop, arriving mid-morning. The journey took 3 hours."
- **Acceptance**: Works for all landmarks; time/energy/sanity costs applied; constraints enforced.

### L2: NPC Schedule Disruption (Details)
**Integration**: In `NPC::tick_schedule()`:
```cpp
if (building_condition(home) < 20 || building_condition(workplace) < 20) {
  if (rng.chance(0.5)) { action = Action::REPAIR; target = worst_building; }
  else { action = Action::SEARCH; target = Material::WOOD | STONE; }
  heart_decay_rate = 2.0; // per week
}
```
- **Dialogue injection**: `talk` checks condition → adds "This roof leaks something awful..." lines.
- **Acceptance**: NPCs visibly prioritize repair; hearts decay faster; player can help.

---

## Implementation Order (Recommended)

| Sprint | Items | Rationale |
|--------|-------|-----------|
| 1 | L1, L3, L5, L8, L9 | Pure atmosphere/polish; no new systems; high visibility |
| 2 | L4, L6, L7 | Living world depth; L4 foundation for 8.1e; L7 uses L1 |
| 3 | L11, L12 | Completes movement system (R5) |
| 4 | L2 | NPC polish (extends Phase 3) |
| — | L10, P1-P4 | **Defer** — merge with Conscious Town infrastructure |

---

## Dependencies on Current Codebase

| Item | Touches | Notes |
|------|---------|-------|
| L1 | `Weather`, `World::tick`, `Crop`, `Building`, `NPC`, PIXI client | Extends Phase 6 weather |
| L2 | `NPC::tick_schedule`, `Dialogue`, `BuildingState` | Extends Phase 3 |
| L3 | PIXI client `renderBuilding`, `BuildingState` | Client-only mostly |
| L4 | `World`, `Tile`, `forage`, `advance_day`, save/load | New tile byte |
| L5 | `World`, `Player::move`, `NPC::move`, PIXI snow shader | New tile byte |
| L6 | New `Wildlife` system, `World::tick`, PIXI | New system |
| L7 | `Weather` (storm), `Forest` (trees), L1/L4 | Needs tree individuals |
| L8 | `Well`, `Pond`, `Weather`, `World::tick` | Extends water |
| L9 | `World`, `Player`, `NPC`, `Wildlife`, PIXI, `search` | New track system |
| L11 | `CommandParser` (`explore`), `Player::move`, `World::pathfind` | Extends `go` |
| L12 | `CommandParser` (`fasttravel`), `Player`, `World::landmarks` | New command |

---

## Estimated Effort

| Item | Dev Days | Risk |
|------|----------|------|
| L1 | 3-4 | Medium (cross-system) |
| L2 | 1-2 | Low |
| L3 | 1-2 | Low (client) |
| L4 | 2-3 | Medium (save format) |
| L5 | 2 | Low |
| L6 | 3-4 | Medium (new system) |
| L7 | 2-3 | Medium (needs tree physics) |
| L8 | 1-2 | Low |
| L9 | 2-3 | Medium (visual + logic) |
| L11 | 2-3 | Low-Medium |
| L12 | 2-3 | Low-Medium |
| **Total (Groups 1-4)** | **~22-32 days** | |

---

## Decision Point

**Resolution (2026-08-16)**: Groups 1-4 (L1-L9, L11, L12, L2) were **completed** in commit `764ccbe`, prior to Phase 7 Conscious Town Core. This cleared all legacy debt and provided the rich substrate (forest_state, wildlife, storms, tracks) that Town Consciousness now observes/adapts.

**Remaining**: Parallelism (P1-P4) and Subterranean (L10) belong in Phase 10 infrastructure and Phase 9 questline respectively — as originally scoped.

---

*Update this plan as items complete. Move finished items to `docs/shipped-features.md`.*