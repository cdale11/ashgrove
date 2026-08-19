# Changelog

All notable changes to Ashgrove Valley are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added — Forest Ecology & Tree Evolution (ROADMAP 2.5, 2026-08-19)
- Per-tree `TreeState` on cells (age, height, biomass, carbon, water, root depth,
  canopy area, mycorrhiza, old-growth, player-managed, 16-allele genome) +
  `seed_bank`/`seed_bank_species`; World gains `seed_agents` + forest aggregates;
  14-species trait table; legacy-save backfill for pre-2.5 trees.
- Deterministic daily `tick_forest_ecology`: LAI 8-neighbour light environment with
  canopy-gap brightening; carbon/water physiology (gpp/resp/npp, allometry, hp proxy);
  allele drift + directional selection; mycorrhiza coevolution + network diffusion;
  old-growth threshold; wind-dispersed seed agents (storm range 3) settling into soil
  banks; seed-bank germination (pioneers need gaps) + succession; disturbance legacy
  decay (windthrow 14d, nurse logs ~30d).
- Player feedback: `ecology`/`foreststatus` report; `planttree <species>` consumes a
  sapling for a player-managed TreeState (excluded from wild seeding); chop clears
  TreeState (old-growth yields +2 hardwood, +6 logs, nurse log); hoe clears seed bank;
  windthrow storms use real physiology; NatureMind `sync_from_world()` grounds chunk
  aggregates in real trees. Rule verbs `ecology`/`foreststatus` in the intent fast path.

### Fixed — adaptation-poisoning crash (2026-08-19)
- The runtime intent LoRA emits strings/objects into numeric adaptation fields
  (`"intensity": "none"`), which threw `nlohmann type_error.302` in
  `World::apply_adaptations` and aborted the server at the first consolidation.
  `apply_adaptations` type-guards every read; `parse_llm_response` merges only
  type-compatible values; the poisoned `data/adaptations.json` was reset to defaults.

### Fixed — forest carbon-economy calibration (2026-08-19)
- gpp 0.06×canopy vs resp 0.012×biomass put the break-even at ~13 kg biomass, so every
  legacy tree ran a carbon deficit, shrank, and never reproduced. Recalibrated to gpp
  0.25×canopy, resp 0.010×biomass (break-even ~0.55×max; dense stands self-thin via the
  light field, gaps/edges grow); germination rolls are now per-cell deterministic.

### Added — Atmospheric Physics (ROADMAP 2.6, 2026-08-19)
- 32×24 deterministic synoptic atmosphere grid over the 128×96 map (4×4 tiles per cell):
  `atmos_temp/humidity/cloud/precip/pressure/wind_u/wind_v` (compact uint8/int8, ~5 KB
  serialized); lazy init for old saves.
- Daily `tick_atmosphere`: FFT spectral fields (radix-2, padded 32) for pressure/temp/
  humidity anomalies; travelling low/high pressure systems on deterministic tracks; a
  meandering cold-front band (L-system branch wiggle) trailing from the low; geostrophic
  wind from the pressure gradient + seasonal trade wind; semi-Lagrangian cloud advection
  + condensation from humidity + wind convergence; precip fallout (drizzle/rain/driving
  rain/storms); Town Consciousness `weather_*` scalars inject bias.
- Per-tile microclimates: `temp_here` (ice/snow cold, water cool, forest shade, buildings
  warm, north highlands cold), `humidity_here` (water/forest +, sand/ice −, built-up −),
  `wind_here` (speed 0–100), `wind_vec_here` (components), `weather_at` (0 sun, 1 rain,
  2 fog, 3 storm).
- Consumer migration: crop watering, forest ecology rain/wind/seed dispersal, water-table
  recharge, pest/disease humidity, well/pond recharge, rain leaching, storm crop damage/
  windthrow (local wind speed), fishing/foraging local weather — all now read per-tile
  queries instead of the global `rain`/`severe_storm` flags.
- Commands: `/weather` (alias `forecast`) with regional synopsis, 32×24 ASCII map, local
  conditions; intent rule verbs `weather`/`forecast`; `/state` exposes `atmos` summary
  (regional counts, temp min/max, per-player `weather_local`).
- Intent regression 26/30 held (same 4 known param-only failures).

### Added — Structural Physics (ROADMAP 2.7, 2026-08-19)
- BuildingState gains `rot`/`erosion`/`stress`/`material`/`fire_*`; Cell gains
  `building_id`/`fire_*`; InvSlot gains `durability`/`max_durability`; lazy init for old saves.
- Daily `tick_structural_physics`: rot/erosion CA (material-dependent, moisture-driven,
  neighbor spread); foundation stress fields (CSP load distribution: weight, foundation,
  erosion, saturation, Jacobi neighbor sharing); fire risk (fuel×dryness+wind); fire
  spread CA (fuel+wind+humidity deterministic spread, basement hatch escape).
- Tool wear grammar: per-tool max durability (hoe 200, watering can 150, axe 300,
  pickaxe 350, scythe 180); wear on use (hoe 2, can 1, axe 3, pickaxe 4, scythe 2);
  repair at Blacksmith (metal bars); broken tools repairable.
- Commands: `inspect`/`check` (building report), `toolrepair`/`fixtool` (blacksmith),
  `fire`/`ignite` (manual ignition), `structural` (valley report); intent rule verbs.
- Intent regression 26/30 held (same 4 known param-only failures).

### Added — Creature Biology (ROADMAP 2.8, 2026-08-19)
- Wildlife gains metabolism Petri nets (hunger/thirst/energy/body_temp/circadian/age/
  life_stage); disease contact graph (disease_level/type/timer/carrier/immunity);
  aging L-systems (infant/juvenile/adult/senior, gestation/birth, senior mortality,
  genetic inheritance+mutation); social graph rewriting (herd/pack formation, alpha/
  territory/cohesion, bond formation/decay, territorial disputes).
- Daily `tick_creature_metabolism/disease/aging/social`: circadian activity windows,
  hunger/thirst/energy/thermoregulation; proximity disease transmission with immunity
  genes; life-stage transitions, gestation/birth, genetic inheritance+mutation, senior
  mortality; herd/pack formation, alpha selection, territory/cohesion, bond formation/
  decay, territorial disputes.
- Commands: `creature`/`wildlife`/`animal` (census/report), `herd`/`pack` (details),
  `disease`/`sickness` (valley report); intent rule verbs.
- Intent regression 26/30 held (same 4 known param-only failures).

### Added — Pest / Disease / Predators (ROADMAP 2.4, 2026-08-19)
- Pest agents (aphids/caterpillars/locusts) + disease spore CA + crop-adjacency
  transmission graph; `Crop` gains `pest_level`/`disease_level`; deterministic daily
  `tick_pest_disease` (feeding, reproduction cap 50, age-out, seeding, severe-blight kill).
- Predators (ladybugs/lacewings) hunt adjacent prey 40%/day, drift toward the nearest
  pest (radius 14), age-out at 12 days, natural immigration when pests ≥ 6.
- Growth now penalized by `pest_factor`/`disease_factor` (up to −80%/−70% at 255).
- Commands: `pest`, `spray [all]` (100g+15 energy), `release [ladybugs|lacewings] [n]`
  (150g each, shortfall refunded), `companion`; companion planting (garlic −50%,
  hops +50%, green bean +20%, flowers −25%, scarecrow −70%) auto-applied.
- Rule verbs `pest`/`spray`/`release`/`companion` added to the intent fast path.

### Fixed — server crash on oversized LLM prompts (2026-08-19)
- Town Consciousness consolidation could build a prompt over `n_batch`, tripping
  `GGML_ASSERT(n_tokens_all <= n_batch)` and aborting the process. Both llama paths now
  cap tokenized prompts at 2000 tokens; memory sections truncated to 1500 chars;
  events block budgeted to 3500 chars.

### Changed — Compiler Warning Cleanup (2026-08-19)
- Swept all project sources to **zero warnings** under `-Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
  -Wunused -Woverloaded-virtual -Wdouble-promotion -Wformat=2 -Wnull-dereference`.
- `src/main.cpp`: fixed `-Wswitch` (fertilizer default), 7 `season` shadow renames,
  job-board `j`→`jb`, HTTP-handler `player_id`/`target_x`/`target_y` conversion casts,
  inventory/room/tile-map size_type indices, and ~40 `-Wconversion`/`-Wsign-conversion`
  sites across travel, farming, economy and the `/cmd` record path.
- `src/world.cpp`: terrain-gen float casts, do-while `-Wparentheses`, `Vec2 d` shadow →
  `dd`, market `flux()` casts, `BuildingState` initializer, `weather_of_day_adapted` param
  rename.
- `src/llama_wrapper.cpp`: migrated to non-deprecated llama.cpp API
  (`llama_model_load_from_file` / `llama_model_free` / `llama_init_from_model`).
- `src/town_consciousness.cpp`, `src/social_cognition.cpp`, `src/nature_mind.cpp`,
  `src/village_mind.cpp`: unused-parameter/variable cleanups and size_type/float casts.
- Third-party warnings (httplib.h, llama.cpp) suppressed with scoped `#pragma GCC
  diagnostic` around the includes.
- Verified: full build emits zero warnings; server smoke-tested (join/move/till/plant/
  sleep day-advance).

### Added — Plant Genetics (ROADMAP 2.3, 2026-08-19)
- **Allele Set per Crop**: 16-allele `std::array<int8_t,16>` (0 = variety reference),
  `homozygosity` (fraction of reference loci × 255), `giant_crop_counter`, `is_giant`, and
  L-System morphology (`height`, `biomass`, `root_depth`, `canopy_width`), all serialized.
- **Allele Drift & Morphology Growth**: daily allele random-walk converges drifted loci toward
  the variety reference (reference loci stay stable), homozygosity recomputed, morphology grows
  from allele weights.
- **Seed Saving**: harvesting a crop saves a seed carrying its genetics (1% per-locus mutation)
  into the player's `seed_gen` (persisted in `save.json`).
- **Planting Inheritance**: planting a saved/bred seed inherits its genetics; generic seeds
  start from the variety reference.
- **Breeding** (`breed <seed1> <seed2>`): consumes one of each seed and EA-recombines both
  lines 50/50 per locus with 1% mutation, storing the child line under seed1's type.
- **Giant Crops**: crops with homozygosity ≥ 200 and biomass ≥ 30 roll 2.5%/day to become
  giant; giant crops sell at 3× price.
- Verified end-to-end: plant→grow→harvest seed saving, breed recombination, seed_gen
  persistence, giant harvest at 3× (+105g parsnip), intent baseline 26/30 unchanged.

### Added — Cognitive Depth (ROADMAP 1.7, 2026-08-18)
- **Cognitive LOD** (`LodLevel` tiers): Full (7 important NPCs, every tick), Lightweight (5 talkable villagers, every ~10 ticks), Statistical (background wildlife, aggregate only). Surfaced via `/cog` endpoint.
- **LRU Episodic Eviction**: Replaced FIFO with importance-based eviction (emotional salience × confidence × recency via new `EpisodicEvent::last_access_tick`). Evicts minimum-importance event when over 256 cap.
- **Causal Traces**: Per-decision ring (cap 20) recording chosen action, drive urgency vector, top stimuli, dominant emotion, final action scores. Exposed via `/cog` endpoint.
- **LLM Cognitive Dialogue**: Talk handler builds prompt from NPC's emotion/drive/trust/memory → calls `LlamaWrapper::infer` (40 tokens, temp 0.8) → strict validation (rejects narrative garbage) → template fallback. Distortion (1.4) still applies on top. Debug logging for raw LLM output.
- **/cog Debug Endpoint**: Lists all agent cores with LOD, memory counts, and causal traces.
- Verified end-to-end: LOD tiers correct, causal traces populated, dialogue falls back to template (intent LoRA unsuited for generation), distortion on top works; intent baseline 26/30 unchanged.

### Added — Soil Chemistry & Composting (ROADMAP 2.1, 2026-08-18)
- **Soil Chemistry per Tile**: N/P/K nutrients (0–255), pH (×10), organic matter (%), microbiome diversity index — all persisted in `Cell` struct and saved/loaded.
- **Rain Leaching CA**: N leaches 3× faster than P, K at 1.5×; severe storms double rate; rain slightly acidifies soil (−0.1 pH) and builds OM.
- **Root Exudates & Crop Uptake**: Legumes fix N (+2/day); all crops boost microbiome; crops uptake N/P/K via Liebig's law of the minimum per growth tick; pH outside 6.0–7.0 reduces growth.
- **Fertilizer System Rewrite**: N/P/K-specific fertilizers (+40 each), Balanced 10-10-10 (+25 each), Organic (+15 each +OM +microbiome), legacy Basic/Quality/Premium. pH amendments: Lime (+0.8 pH), Sulfur (−0.8 pH), Gypsum (neutral).
- **Soil Test Command** (`soil`/`soiltest`): Shows N/P/K/pH/OM/microbiome with status tags, crop info, automated recommendations.
- **Composter Overhaul**: Accepts fiber/manure/ash/bone → produces N/P/K/organic/balanced fertilizer based on input NPK ratio. 4-day cycle tracked in `FarmObj` (hp=days, ore=N, hp2=P, hp3=K).
- **Crop Growth Integration**: Growth rate = base × pH_factor × nutrient_factor × OM_factor × microbe_factor; nutrient uptake depletes soil per growth tick; legacy fertilizer bonus still works.
- **Intent regression 26/30 (86.7%)** — unchanged from baseline.

### Added — Water Table & Groundwater (ROADMAP 2.2, 2026-08-18)
- **Water Table per Tile**: N/P/K nutrients (0–255), pH (×10), organic matter (%), microbiome — all persisted in `Cell` struct and saved/loaded.
- **Rain Leaching CA**: N leaches 3× faster than P, K at 1.5×; severe storms 2× rate; rain slightly acidifies soil, builds OM.
- **Root Exudates & Crop Uptake**: Legumes fix N (+2/day); all crops boost microbiome; crops uptake N/P/K via Liebig's law; pH outside 6.0–7.0 reduces growth.
- **Fertilizer Rewrite**: N/P/K-specific (+40 each), Balanced 10-10-10 (+25), Organic (+15 +OM +microbiome -0.2 pH), legacy Basic/Quality/Premium; pH amendments: Lime (+0.8), Sulfur (−0.8), Gypsum (neutral).
- **Soil Test Command** (`soil`/`soiltest`): Shows N/P/K/pH/OM/microbiome with status tags, crop info, automated recommendations.
- **Composter Overhaul**: Accepts fiber/manure/ash/bone → N/P/K/organic/balanced fertilizer based on input NPK; 4-day cycle in `FarmObj` (hp=days, ore=N, hp2=P, hp3=K).
- **Crop Growth Integration**: Growth = base × pH_factor × nutrient_factor × OM_factor × micro_factor; nutrient uptake depletes soil per growth tick; legacy fertilizer bonus preserved.
- **Well Mechanics**: Cone of depression, well drying when water table drops; `well` command shows water level/depth/saturation; watering can refills from well.
- **Irrigation**: `water` command uses well water (adjacent/standing) or can; raises saturation +5.
- **Crop Growth Integration**: Growth = base × pH_factor × nutrient_factor × OM_factor × micro_factor; nutrient uptake depletes soil per tick; legacy fertilizer bonus preserved.
- Verified end-to-end (all filters, basement procgen + mark malus, death meta-break, dread profile); intent baseline 26/30.

### Added — Sanity / Perception Filters & Procedural Basement (ROADMAP 1.4, 2026-08-18)
- **Distorted dialogue** (`World::distort_dialogue`): word-swap + whispered underlayer
  (tier ≥1) + hallucinated dread-biased clause (tier ≥3).
- **Hallucinated scene text** (`World::hallucinate_scene`): `(?)`-tagged false descriptions
  + phantom scarecrow sighting in `look`/explore, gated by tier + awakening + corruption.
- **False UI** (tier ≥3): phantom inventory items (25%, seeded per day) + lying `/status`
  clock (30%, deterministic per day).
- **Meta-narrative breaks** (`World::roll_meta_break`): one-shot at death_count ≥2
  (sets `meta_break_fired`) + rare 5% repeatable inside horror anchors at tier ≥3.
- **Procedural basement** (`World::roll_basement_procgen`): per-descent room/hazard/mark
  (cold/oily/whispering) weighted by dread bias; persistent mark with daily sanity malus
  (`tick_basement_mark`, 1.5/day, +1.5 whispering at night).
- **Adaptive dread profile** (per-player `dread_counters[4]`): bumps from phantom sightings,
  night events, corrupted-tile traversal, basement hazard; `dread_bias` dominant theme biases
  filter content; surfaced via `/valley` (`dread_bias_theme`, `dread_counters`).
- New player save fields persisted: `meta_break_fired`, `basement_mark`, `mark_days_left`,
  `dread_counters`.
- Verified end-to-end (all filters, basement procgen + mark malus, death meta-break, dread
  profile); intent baseline unchanged 26/30.

### Added — Valley Entity Mechanics (ROADMAP 1.2, 2026-08-17)
- **ValleyMind** aggregate: the Valley itself (genius loci) as a living system state
  (collective guilt → spatial corruption CA → awakening → horror feedback loop).
- `World::add_guilt` / `tick_valley` / `corruption_density`; guilt from deaths, secrets,
  basement descents, night events; slow decay (-0.02/day); carries + escalates across
  horror_cycles.
- Spatial corruption CA (`Cell::corruption`, 0..255) seeded at 4 horror anchors;
  double-buffered diffusion + decay + anchor reseed.
- Valley awakening pushes `horror_intensity` / `horror_sanity_drain_multiplier` /
  `weather_fog_intensity` / `horror_phantom_sighting_chance` each consolidation.
- `/valley` diagnostic endpoint; ambient corruption flavor on `explore`.
- Fix: `apply_adaptations` moved inside the once-per-day consolidation guard (was
  clobbering the minds' pushes every loop iter).
- Verified: full feedback loop end-to-end (seeded guilt 0.5 → all consumers shift);
  intent baseline 26/30.

### Added — Horror Location Structures (ROADMAP 1.1, 2026-08-17)
- New real map structures with interiors + horror behavior: **Witch's Hut** (Whisper Wood,
  scrolls/mirror/kettle), **Abandoned Sanitarium** (East Moor, beds/records/instruments),
  **Ritual Circle** (forest border, candles/altar). Basement already existed.
- New `InteriorType::{WitchHut, Sanitarium, RitualCircle}`.
- Entry drains sanity + surfaces narrative; furniture `interact` yields fragments and grants
  secrets (`sanitarium_records`, `ritual_altar`) via `World::find_secret` (surfaced in `/horror`).
- Fix: `interact`/`use` added to rule intent engine (was falling through to LLM).
- Verified: enter/exit, sanity drain (100→85), interact narrative + secrets, intent baseline 26/30.

### Added — Death & the "Loop" (P2 / ROADMAP 1.3, 2026-08-17)
- Player HP (`health`/`max_health`, default 100) + last-safe-point (`last_safe_pos`) + death
  counter (`death_count`), persisted in save.
- Death model: `health <= 0` or `sanity <= 0` → `World::is_dead()`; `World::handle_death`
  applies P2 penalties — HP full, position → safe point, sanity reduced (40%, never zero);
  world/NPCs/knowledge/items persist. Every death is a narratively remembered "loop".
- Basement entry now damages HP (`12 + 8 × horror_cycle`).
- Death-aware wiring in `/cmd` (action) and the game loop (time-based, queued in
  `pending_death`, surfaced once by `/state`/`status`).
- NPC death references (`npc_death_line`): Mayor/Witch/Traveler/Doctor remember loops;
  the 5 villagers react once you've died ≥2 times.
- Verified: basement HP drain, death reset (HP→100, sanity→40, death_count→1, pos→door),
  status death line, villager death dialogue. Intent baseline unchanged (26/30).

### Tier 0 — Design Decisions Locked (2026-08-17)
- **P0 geography**: river as **braid/multiple channels** creating islands; player farm on a
  **river island** (Farmstead district, bridge/boat access); horror anchor placement **kept
  as designed** (Basement/`Witch's hut`/Sanitarium/Ritual circle/Fog zones).
- **P1 narrative**: author **4 cycles** (3 fragments + 1 truth) before true-end path; **free-form
  player agency per loop** (emergence-driven); **core sanity filters** first (hallucinated text,
  occasional false UI, one meta-break) — PIXI visuals deferred to Tier 5.2.
- `docs/ROADMAP.md`: §2.1a/§2.2a added; Tier 0 items 0.1 & 0.2 marked **DONE** — unblocks 1.1,
  1.2, 1.4, 4.1.

### Docs — Roadmap Consolidation (2026-08-17)
- **New canonical `docs/ROADMAP.md`**: all open work consolidated into one file, ordered by
  priority and blocking (Tier 0 user design sessions → Tier 1 engineering → Tier 2 deep
  simulation → Tier 3 procedural expansion → Tier 4 questline → Tier 5 infrastructure).
  Includes vision, user-approved design decisions (districts, horror anchors, NPCs, runs
  model), emergence toolkit, layer specs, risks, success criteria.
- **`docs/shipped-features.md`** extended: QA & bug-fix history (commits `403bf3e`,
  `1b2523f`, `9db0875`), verified test pass of 2026-08-16, performance profile, updated
  commit list (through `a4e913b`).
- **Deleted (content absorbed into ROADMAP/shipped-features)**: `docs/vision-and-roadmap.md`,
  `docs/conscious-town-roadmap.md`, `docs/vision-gap-analysis.md`,
  `docs/legacy-completion-plan.md`, `docs/map-redesign-plan.md`, `docs/design-decisions.md`,
  `BUG_REPORT.md`, `TEST_REPORT.md`.
- **`AGENTS.md`** (SOP): merged standing rules (audit before major work, agent
  suggestions, emergence over hard-coding, parallelism, player simplification, verify before
  push, model policy), updated doc map + HTTP API list (`/town/*` endpoints).
- **`README.md`** / **`docs/cognitive-architecture.md`** / **`MISTAKES.md`**: references
  updated to the new doc set.

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

### Added — Phase 8 (Model Distillation): pipeline scaffolding
- **Tiered intent engine** (`IntentEngine`, `src/intent_engine.cpp`): Tier-0 deterministic rule fast path for the fixed command surface (~40 verbs/aliases, sub-millisecond) with Tier-1 LLM fallback via the existing `LlamaWrapper`. `/cmd` now routes rule-first.
- **Command log collector** (`CommandLog`, `src/command_log.cpp`): appends one JSONL record per `/cmd` to `data/cmdlog.jsonl` — `{ts, player_id, day, season, hour, raw, intent, tier, latency_ms, lines}`. This is the ever-growing Phase 8-A training dataset.
- **Dataset schema** (`tools/dataset_schema.md`): defines the `cmdlog.jsonl`, `dataset.jsonl`, and `eval_set.jsonl` formats.
- **Golden eval set** (`data/eval_set.jsonl`, 30 cases): canonical + paraphrased commands with expected `{action, parameters}`.
- **Eval harness** (`tools/eval_intents.py`): measures per-case accuracy + p50/p95 latency against a live server.
- **Cloud teacher generator** (`tools/gen_dataset.py`): Phase 8-B scaffold that expands the live cmdlog into paraphrase training rows via an OpenAI-compatible cloud endpoint. Not run by CI; requires `ASHGROVE_API_KEY` (etc.).
- **Canonical seed generator** (`tools/build_seed_dataset.py`): deterministically emits 431 canonical `text → {action, parameters}` rows (`data/dataset.jsonl`) from the game's command surface — 36 actions × slots × aliases, `source: seed`. `gen_dataset.py` now consumes this seed and expands it via a cloud teacher into `data/dataset_expanded.jsonl`, inheriting correct intents.
- **Student base downloaded**: Qwen2.5-0.5B-Instruct GGUF Q8_0 (training base) + Q4_K_M (inference) into `llama.cpp/models/`; verified loading/generating with the built llama.cpp.
- **Latency win**: `look`/`inventory`/`status`/`go` etc. now return in **~10 ms** via Tier 0 (previously 10-30 s through the LLM).

### Added — Phase 8 (Model Distillation): ✅ COMPLETE
- **Dataset**: `data/dataset_expanded.jsonl` — 1338 rows (1007 paraphrases + 331 seeds), 0 API failures, 36/36 actions, 945 with params. Expanded via NVIDIA NIM `nemotron-3.5-lightning-30b-a3b` (thinking disabled, ≤30 RPM) from canonical seeds (`tools/build_seed_dataset.py`).
- **Training**: CPU LoRA via PyTorch + PEFT in `ashgrove` conda env (torch 2.13 CPU, transformers 5.15, peft 0.20, accelerate 1.14). Qwen2.5-0.5B fp32 base (~2 GB). Targets q/k/v/o + gate/up/down (r=16, alpha=32, dropout 0.05). Batch 8, accum 2 (effective 16), 3 epochs → 240 steps. Eval every 40 steps. Final loss ~0.235. Live monitor at `http://<host>:8137`.
- **Student Model**: `data/qwen2.5-0.5b-ashgrove-q4_k_m.gguf` — merged LoRA + HF base → F16 GGUF → `llama-quantize` Q4_K_M (373.71 MiB, 6.35 BPW).
- **Tier 1 Swap**: `find_model()` in `main.cpp` prioritizes Ashgrove student GGUF. IntentEngine: Tier 0 (rule, ~10 ms) → Tier 1 (student + GBNF grammar, <1 s) → Tier 2 (async prose).
- **Cleanup**: Removed `qwen2.5-0.5b-instruct-fp16.gguf` (1.2 GB), `qwen2.5-0.5b-instruct-q8_0.gguf`, `build/_deps` (418 MB), stale logs (~0.6 GB). ~2.2 GB freed.

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