# Ashgrove Valley — Roadmap (Canonical)

**Single source of truth for all open work.** Ordered by priority and blocking relationships.
Shipped work lives in [`docs/shipped-features.md`](./shipped-features.md).
Agent rules/SOP live in [`AGENTS.md`](../AGENTS.md).
Architecture spec lives in [`docs/cognitive-architecture.md`](./cognitive-architecture.md).
Consolidated 2026-08-17 from `vision-and-roadmap.md`, `conscious-town-roadmap.md`,
`vision-gap-analysis.md`, `legacy-completion-plan.md`, `map-redesign-plan.md`,
`design-decisions.md`, `BUG_REPORT.md`, `TEST_REPORT.md` (all deleted — content absorbed here).

---

## 1. Vision & North Star

- **Dual-system architecture**: (1) Deterministic/RNG core — authoritative simulation, seeded,
  reproducible, parallel-friendly; no cognition. (2) Cognitive AI/ML layer — local LLM ≤2B as
  **language interface and reasoning engine only** (intent parsing, dialogue, consolidation,
  narrative synthesis, `/town/why` traces). All AI outputs convert to deterministic actions
  before touching core state.
- **Text-first**: primary interface is prose MUD commands; PIXI.js minimap is a passive
  reference prompt. "The real game lives in the simulation and the player's imagination."
- **Living, thinking, learning world**: NPC schedules, economy, weather, crops, horror run
  without the player; adaptive systems learn from player actions; offline simulation (compressed
  tick) so the world advances while the server is down.
- **Horror inspirations**: Stardew (cozy loop), FROM (uncanny town), Higurashi (cyclical horror),
  DDLC (meta-narrative), Disco Elysium (internal voices, philosophical depth).
- **Emergence as first-class goal**: systems interact via Event Bus → cascades never explicitly
  coded. The game should *become something it was not originally coded for*.
- **Extensible plugins**: `extern "C" void register_plugin(EventBus&)` in `plugins/*.so`
  (partial — `command_dispatcher` exists).
- **Human-like core NPCs**: 7 important NPCs are simulated minds (goals, fears, secrets), not
  scripted quest-givers.

---

## 2. Design Decisions (user-approved, 2026-08-16)

### 2.1 P0 — Authored Town: districts, anchors, NPCs

**8 districts**: Civic, Commercial, Residential, Industrial, Riverside, Woodland, Farmstead, Horror.

**5 horror location anchors**:

| Location | District | Narrative Role |
|----------|----------|----------------|
| Basement entrance | Civic (Town Hall cellar) | Central mystery access; only after midnight |
| Witch's hut | Woodland | Knowledge broker, cycle witness |
| Abandoned sanitarium | Horror | Past trauma site, collective guilt manifestation |
| Ritual circle | Forest (Woodland/Forest border) | Cycle mechanics, entity communication |
| Fog zones | Riverside (night) | Perception filter, entity manifestation |

**7 important NPCs (full cognition)**:

| NPC | Role | District | Secret / Horror Connection |
|-----|------|----------|---------------------------|
| **Mayor** | Governance, order | Civic | Knows the cycle; suppresses truth to maintain control |
| **Witch** | Knowledge, magic | Woodland | Sees cycles; guides/manipulates player |
| **Traveler** | Outsider, observer | Commercial (inn) | Not bound by cycle; remembers across loops |
| **Doctor** | Health, sanity | Civic/Residential | Complicit; treats symptoms not cause |
| **Teacher** | Education, history | Civic | Witness; records but cannot act |
| **Carpenter** | Building, repair | Industrial | Maintains physical barriers; knows structural weaknesses |
| **Farmer** | Food, land steward | Farmstead | Connection to Valley's body; notices ecological shifts |

**Background NPCs**: cheaper cognition (statistical/behavioral only — no episodic memory, no
social graph, no self-model; simple drive-based action selection).

**2.1a P0 — DECIDED 2026-08-17 (user input)**:
- **River geography**: **Braid / multiple channels** — the river splits into channels creating
  islands. Riverside district lines the channels; flood zones cluster near braids. More
  bridges + flood complexity than a single channel.
- **Farm layout**: **Player farm sits on a river island** in the Farmstead district, fully
  surrounded by water, accessed by bridge/boat — reinforces isolation and the valley-entity
  theme; a threshold crossing into the town's mysteries.
- **Horror anchor placement**: **kept exactly as designed** (§2.1 table above) — no repositioning.

### 2.2 P1 — Horror narrative: core concept (mechanics still need authoring)

- **The Valley itself (genius loci)**: the land is alive/cursed; collective guilt (witch trials,
  betrayal, massacre) gave it consciousness. Three intertwined mechanisms:
  1. Time loops / cyclical tragedy — events repeat with variations; truth hidden in each cycle.
  2. Entity feeding on fear/sanity — the Valley consumes sanity/fear to grow stronger.
  3. Collective guilt manifests physically — corruption, fog, mutations are guilt made manifest.
- **Cycle structure (Higurashi-inspired)**: 4–5 major cycles before "true ending" path unlocks;
  escalation per cycle; divergence points from player choices; memory across cycles (Traveler,
  Witch, Valley, player retain knowledge).
- **Sanity/perception filters**: hallucinations (PIXI effects), distorted dialogue, false UI,
  meta-narrative breaks (DDLC), internal voices (Disco Elysium).
- **Basement**: accessible only after midnight (24:00–04:00); procedural horror content per
  cycle; persistent consequences affecting the surface; the Valley's "heart".

**2.2a P1 — DECIDED 2026-08-17 (user input)**:
- **Cycles to author**: **4 concrete cycles** (Higurashi-style) — 3 escalating "fragment"
  loops + 1 truth/cataclysm loop, then the true-end path unlocks.
- **Player agency within a loop**: **free-form per loop** — player roams freely; consequences
  emerge from how the 4–5 cycles play out, relying on the emergent systems (Tier 2/4) rather
  than rigid per-cycle missions. Truth fragments persist across loops.
- **Sanity/perception filters (first pass)**: **core filters only** — hallucinated text/dialogue,
  occasional false UI, one meta-narrative break. PIXI visual distortion deferred until the
  WebSocket + browser client ships (Tier 5.2).

### 2.3 P2 — Recurring runs (DESIGN DONE, implementation open)

- **Single persistent world; death = penalties, not reset.**
- **Persists**: all world state, NPC memories/relationships (they remember player deaths),
  player knowledge (map, recipes, horror truths, secrets), stored items, building progress.
- **Resets on death**: HP → full, position → last safe point, sanity → reduced (not zero),
  temp buffs cleared.
- No run counter / no full reset; each death is a "loop" narratively.

### 2.4 Cognitive architecture priorities

- 7 important NPCs get full CognitiveCore (done); background NPCs statistical only.
- **Aggregate order** (all shipped): NatureMind → VillageMind → EconomyMind → CultureMind →
  PerformanceMind (existed).
- **Next**: Valley Entity mechanics (collective guilt → corruption → horror intensity).

---

## 3. Open Work — Priority & Blocking Order

Legend: **[USER]** = needs user collaboration/sign-off · **[DEFERRED]** = deferred per user ·
**blocker arrows** show dependency direction (A → B means A blocks B).

### Tier 0 — User design prerequisites (block all content phases)

> **DONE 2026-08-17** — user provided all decisions. See §2.1a (geography) and §2.2a (narrative).

| # | Item | Blocked by | Blocks |
|---|------|-----------|--------|
| 0.1 | **P0: Authored Town Design session** — geography = river **braid/islands**; farm = **river island**; horror anchors = as designed. **DONE** | — | 1.1, 1.4, 4.1 |
| 0.2 | **P1: Horror narrative authoring** — **4 cycles**; **free-form per loop**; **core sanity filters** first. **DONE** | 0.1 | 1.2, 1.4, 4.1 |

### Tier 1 — Immediate engineering (unblocks narrative & content)

| # | Item | Blocked by | Blocks |
|---|------|-----------|--------|
| 1.1 | **Horror location structures** — basement, witch hut, sanitarium, ritual circle, fog zones as real map structures with interiors/behavior. **DONE 2026-08-17** (basement existed; Witch's Hut, Abandoned Sanitarium, Ritual Circle added with interiors + entry/interact horror; fog zones atmospheric — exist) | 0.1 | 1.4, 4.1 |
| 1.2 | **Valley Entity mechanics** — collective guilt → corruption → horror intensity feedback loop (valley-as-entity system state). **DONE 2026-08-17** (ValleyMind aggregate, spatial corruption CA seeded at 4 horror anchors, guilt from deaths/secrets/basement/night events with slow decay + per-cycle escalation, awakening pushes horror_intensity/fog/drain/phantom; `/valley` diagnostic endpoint) | 0.2 | 1.4 |
| 1.3 | **Death consequences (P2 impl)** — HP/position/sanity reset rules, NPC references to past player deaths, "loop" narrative hooks. **DONE 2026-08-17** | — | — |
| 1.4 | **Sanity/perception filters depth** — hallucinations, distorted dialogue, false UI, meta-narrative breaks; basement procedural horror content + persistent surface consequences. **DONE 2026-08-18** (distorted dialogue w/ whispered underlayer + word-swap + hallucinated clause; hallucinated scene lines tagged `(?)`; false inventory phantom items + false clock gated at tier ≥3; one-shot death meta-break at death_count ≥2 + rare 5% anchor meta-breaks at tier ≥3; procedural basement room/hazard/mark per descent with persistent daily-sanity-malus mark; per-player adaptive dread profile biasing filter content; `/valley` dread diagnostics) | 0.1, 0.2, 1.1, 1.2 | 4.1 |
| 1.5 | **LoRA consolidation-format training** — build town-consciousness dataset (consolidation prompt format), append to training, retrain with `tools/train_lora.py`, merge+quantize, re-run soak. **[DEFERRED]** (user: "we will do training later") | user sign-off | 1.6 |
| 1.6 | **Town Consciousness verification + endpoints** — 30-day soak test (adaptations drift from defaults, no crashes, perf tuner converges); `/town/inspect`, `/town/why`, `/town/memory`, `/town/adaptations` endpoints | 1.5 (meaningful drift) | — |
| 1.7 | **Cognitive depth** — Cognitive LOD (important vs background NPCs; distant entities statistical), memory budgeting/LRU eviction, LLM one-line dialogue wired to NPC cognitive state (emotional tag + preferences + social graph), causal traces for debugging. **DONE 2026-08-18** (LOD tiers: Full=7 important NPCs tick every call, Lightweight=5 villagers tick every ~10, Statistical=background aggregate; LRU episodic eviction by recency+importance replacing FIFO; causal-trace ring recorded per decision, surfaced via /cog; LLM one-line dialogue with cognitive-state prompt + strict validation + template fallback; /cog debug endpoint for LOD+causal traces). | — | — |

### Tier 2 — Deep simulation layers (sequential: 8.1a → 8.1e → 8.2 → 8.3 → 8.4 → 8.5)

Mechanistic, deterministic, emergent — see §5 toolkit and §6 layer specs.

| # | Item | Blocked by | Blocks |
|---|------|-----------|--------|
| 2.1 | **8.1a Soil Chemistry** — NPK + pH + OM + microbiome per tile; rain leaching CA; compost; root exudates. **DONE 2026-08-18** (Cell gains N/P/K/pH/OM/microbiome; rain leaching CA moves nutrients downward; composter produces N/P/K/organic fertilizer from organic inputs; root exudates boost microbiome and fix N for legumes; crop growth uses Liebig's law of the minimum with pH/organic matter/microbiome factors; soil test command shows levels + recommendations; fertilize command accepts N/P/K/balanced/organic/lime/sulfur/gypsum; composter accepts fiber/manure/ash/bone for NPK-specific output) | — | 2.2–2.5 |
| 2.2 | **8.1b Water Table** — 2D groundwater Darcy→CA; recharge/discharge; cone of depression; well drying. **DONE 2026-08-18** (Cell gains water_table_depth, saturation, aquifer_transmissivity, specific_yield, recharge_capacity; rain leaching CA moves nutrients downward; composter produces N/P/K/organic fertilizer from organic inputs; root exudates boost microbiome and fix N for legumes; crop growth uses Liebig's law of the minimum with pH/organic matter/microbiome factors; soil test command shows levels + recommendations; fertilize command accepts N/P/K/balanced/organic/lime/sulfur/gypsum; composter accepts fiber/manure/ash/bone for NPK-specific output; well mechanics with cone of depression and drying; irrigation via watering can from wells; water table affects nutrient mobility) | 2.1 | 2.3–2.5 |
| 2.3 | **8.1c Plant Genetics** — allele sets per variety; EA recombination; L-System morphology; seed saving/breeding; giant crops from homozygosity. **DONE 2026-08-19** (Crop gains 16-allele set, homozygosity, giant_crop_counter, is_giant + L-System morphology height/biomass/root_depth/canopy_width; plant inherits saved seed genetics; daily allele drift converges toward variety reference with reference-stable homozygosity; giant crop unlocks at homozygosity ≥200 + biomass ≥30 with a 2.5%/day roll; harvest of giant = 3× sell price; harvest saves a seed carrying the crop genetics with 1% per-locus mutation; `breed <seed1> <seed2>` EA-recombines two seed lots 50/50 per locus with 1% mutation, storing the child line; seed genetics persist in save.json `seed_gen`) | 2.2 | 2.4, 2.5 |
| 2.4 | **8.1d Pest/Disease** — pest agents + spore CA + transmission graph; predators; companion planting. **DONE 2026-08-19** (World gains `pests`/`predators` agent lists + `pest_bias`; Crop gains `pest_level`/`disease_level`; deterministic daily `tick_pest_disease`: disease spore CA with simultaneous update + 4-neighbour spread, severe blight (level>230, 2%/day) kills crops; pest agents feed (growth penalty), reproduce along the crop-adjacency transmission graph (cap 50), wander-free lifecycle age 10d; new infestations seed onto susceptible crops scaled by season + pest_bias + companion modifiers; predators 128=ladybug (aphids) / 129=lacewing (caterpillars+locusts) hunt 40%/day and drift toward the nearest pest (radius 14); natural ladybug immigration when pests ≥6; growth uses `pest_factor`/`disease_factor` (up to −80%/−70% at level 255); commands `pest`, `spray [all]` (100g+15 energy, clears level on 3×3 or farm), `release [ladybugs|lacewings] [n]` (150g each, placed on 3×3, refunds shortfall), `companion`; companion planting auto-applied — garlic −50%, hops +50%, green bean +20%, flowers −25%, scarecrow 17×17 −70%; rule verbs pest/spray/release/companion added to the intent fast path) | 2.3 | 2.5 |
| 2.5 | **8.1e Forest Ecology & Tree Evolution** — 8 steps: (1) tree individual physiology (L-System + carbon/water), (2) light env + carbon/water economy, (3) reproduction/dispersal (seed agents, seed bank), (4) succession & community assembly, (5) intraspecific evolution (breeder's equation), (6) mycorrhizal & coevolution, (7) disturbance legacy & old-growth, (8) player feedback integration. NatureMind (shipped) is the aggregate bias consumer. **DONE 2026-08-19** (Cell gains `tree` TreeState (age/height/biomass/carbon/water/root_depth/canopy_area/mycorrhiza/old_growth/player_managed/16-allele genome/homozygosity) + `seed_bank`/`seed_bank_species`; World gains `seed_agents` + forest aggregates; deterministic daily `tick_forest_ecology`: (2) LAI 8-neighbour light env with canopy-gap brightening, (1) carbon/water physiology (gpp/resp/npp, allometry, hp proxy), legacy-save backfill for pre-2.5 trees, (5) allele drift + directional selection, (6) mycorrhiza coevolution + network diffusion, (7) old-growth threshold + windthrow legacy decay (14d), (3) wild seed production (mature, non-managed, fitness-seeded vigor, 1% mutation), wind-dispersed agents (storm range 3, settle into soil bank age≥3, bank cap 255/cell, agent cap 200), (4) seed-bank germination (pioneers need gaps, per-cell rolls) + succession; `ecology`/`foreststatus` command; intent rule verbs; player-managed planttree excluded from wild seeding; chop clears TreeState (old-growth gives +2 hardwood +6 logs + nurse log); hoe clears seed bank; NatureMind `sync_from_world()` grounds chunk aggregates in real trees; windthrow storms use real root/canopy/wood-density physiology; calibrated carbon economy (gpp 0.25×canopy, resp 0.010×biomass → break-even ~0.55 max, dense stands self-thin, gaps/edges grow) | 2.4 | 2.6, 2.8, 3.2 |
| 2.6 | **8.2 Atmospheric physics** — pressure/temp/humidity fields, spectral advection, cloud/precip CA, microclimates | 2.5 | 2.8 |
|     | **DONE 2026-08-19** (World gains `atmos_*` 32×24 grid: temp/humidity/cloud/precip/pressure/wind_u/wind_v; deterministic daily `tick_atmosphere`: FFT spectral fields (radix-2, padded 32) for synoptic pressure/temp/humidity anomalies + travelling low/high pressure systems + meandering cold-front band (L-system branch wiggle) + geostrophic wind from pressure gradient + trade wind; semi-Lagrangian cloud advection + condensation from humidity + wind convergence + orographic proxy; precip fallout (drizzle, rain, driving rain/storms); Town Consciousness `weather_*` scalars inject bias (temp_bias, humidity_drift, storm_chance); per-tile microclimates (ice/snow cold, water cool, forest transpiration, buildings warm, north highlands); queries `weather_at/rain_here/temp_here/humidity_here/wind_here/wind_vec_here`; consumers migrated: crop watering, forest ecology rain/wind/seed dispersal, water-table recharge, pest/disease humidity, well/pond recharge, rain leaching, storm crop damage/windthrow, fishing/foraging local weather; `/weather` + `forecast` command + intent rule verbs; `/state` exposes `atmos` summary with regional stats + per-player local weather; serialization + lazy init for old saves) |     |     |
| 2.7 | **8.3 Structural physics** — rot/erosion CA, tool wear grammar, fire spread CA, stress fields, basement hatch escape | — | 2.8 |
| 2.8 | **8.4 Creature biology** — metabolism Petri nets, disease on contact graph, aging L-systems, social graph rewriting | 2.5, 2.6, 2.7 | 2.9 |
| 2.9 | **8.5 Terrain & ecological change** — flood CA, river migration, erosion CA, fire spread, succession, soil degradation | 2.8 | 3.2 |
| 2.10 | **Hidden State Persistence** — serialize all cognitive hidden states (RNN/SSM latent vectors, memories, adapted weights) to `save.json` per agent/aggregate; restore on load; discard on `newgame`. Applies to 7 important NPCs, 5 villagers, 6 aggregate minds. | 1.7 (cognitive core wired) | 4.1 (narrative anchors need persistent minds) |
| 2.11 | **Custom Ashgrove LLM** — purpose-trained multi-task model (intent, consolidation, dialogue, narrative) with cognitive-state conditioning; custom tokenizer; replaces Qwen LoRA. | 1.5 (consolidation dataset), 1.7d (dialogue data) | 4.1+ (full narrative integration) |

### Tier 3 — Procedural expansion

| # | Item | Blocked by | Blocks |
|---|------|-----------|--------|
| 3.1 | **8.6 Procedural wilderness** — seamless authored→procgen transition, infinite exploration, biome transitions, landmark generation | — | 4.1 |
| 3.2 | **8.7 Procedural stories** — quest generator sampling world state/history/relationships/unresolved goals (basic template+state generator exists since Phase 5; deepen + emergent narrative) | 2.9 | 4.1 |
| 3.3 | **8.8 Offline simulation** — compressed tick on restart (crops, NPC schedules, weather, economy, ecology), deterministic replay, batch processing | 2.9 | — |

### Tier 4 — Questline backbone (Phase 9)

| # | Item | Blocked by | Blocks |
|---|------|-----------|--------|
| 4.1 | **Narrative anchors + emergent branches** — 5 anchors (below), Town Consciousness as quest director, personalized branches from playstyle/relationships/world state; Journal auto-records discoveries; escalating pressure until engaged | 0.1, 0.2, 1.1, 1.4, 3.1 | 4.2 |
| 4.2 | **L10: Subterranean under-map layer** — parallel under-map terrain, Old Mill basement access, merges with 4.1 + 2.5 | 4.1 | — |

**Anchor table**:

| Anchor | Location | Core Revelation | Emergent Gates |
|--------|----------|-----------------|----------------|
| The Mayor's Ledger | Town Hall (night, high hearts) | Founding pact with *something* under the valley | Mayor ≥8 hearts, basement unlocked, specific night event |
| The Witch's Bargain | Witch's Hut (basement access) | The Fog is a living entity; the town feeds it sanity | Witch ≥6 hearts, 3+ night events, sanity <50 once |
| The Traveler's Map | Random encounter (procgen chunk) | The valley is one of many; the basement connects them | 5+ chunks explored, 2+ ruins, Traveler met 3× |
| The Old Mill Machine | Under-map basement (deep) | A device that *writes* the town's reality — the "OS" | All 3 above + horror_cycle ≥3 + specific secret |
| The Choice | Old Mill Machine room | **Player rewrites one rule of the valley** (permanent, saved to world seed) | All 4 anchors + Town Consciousness trust ≥ threshold |

### Tier 5 — Infrastructure & polish

| # | Item | Notes |
|---|------|-------|
| 5.1 | **Parallelism P1–P4** — fine-grained locking (split World mutex), double-buffered tick, parallel systems (`std::execution::par_unseq`, NPC thread pool), async persistence (dirty-chunk, zstd) | **[DEFERRED]** to Phase 10 |
| 5.2 | **WebSocket push + PIXI.js browser client** — real-time NPC/player movement, weather, chat, events | — |
| 5.3 | **CI gates** — clang-tidy, cppcheck, warning-free build gate, Doxygen API reference in CI | — |
| 5.4 | **QA backlog** — bus stop path blocked by river (needs bridge-crossing logic); ObjType enum gap (value 18 missing, `include/world.hpp`); client keyboard shortcuts (Ctrl+C clear, copy/paste); verify severe storms (L1, 1% chance), snow compaction (L5), festival variety, winter crops | — |
| 5.5 | **Content breadth** — crop catalogue toward 100+ (currently 45); skill lines beyond Farming; deeper gift/interaction tables | — |

---

## 4. Dependencies & Ordering

| Phase/Item | Depends on |
|------------|-----------|
| 7 (Conscious Town Core) | ✅ DONE (7.1–7.5, 7.7–7.9 shipped) |
| 8.1 (Soil/Plant) | ✅ 7 adaptation hooks wired |
| 8.2 (Weather) | 8.1b water table |
| 8.3 (Structural) | 7 |
| 8.4 (Creature Bio) | 8.1, 8.2, 8.3 |
| 8.5 (Disasters/Terrain) | 8.1–8.4 |
| 9 (Questline) | 7, 6 (anchors), P0/P1, horror locations, L10 |
| 10 (Integration) | All above |

---

## 5. Non-AI Emergence Toolkit (deterministic techniques for Phase 8)

Town Consciousness only *biases parameters*; mechanics run on these deterministic,
parallelizable techniques (no ML at runtime):

| Technique | Where It Shines | Example in Ashgrove |
|-----------|-----------------|---------------------|
| **L-Systems** | Plant growth, river networks, caves, mycelium | Crop morphology, tree growth stages, root networks, procgen caves |
| **Graph Rewriting / Grammars** | Social networks, quest dependency graphs, disease networks | NPC relationship web, quest templates, building upgrades, pathogen spread |
| **CFG / Attribute Grammars** | DSL, narrative beats, recipes, blueprints | `dsl` parser, night-event chapter grammar, crafting DAG, interiors |
| **Evolutionary Algorithms** | Creature AI, plant breeding, personality drift, procgen tuning | Crop allele optimization, NPC schedule neuroevolution, self-tuning hyperparams |
| **Cellular Automata** | Fire, water diffusion, disease, fog, corruption | Fire CA (wind+humidity+fuel), groundwater Darcy→CA, horror fog CA, basement corruption |
| **Agent-Based / Particles** | Pollen, insects, rain, debris, sanity echoes | Pollination agents, pest swarms, rain→soil moisture, horror echoes |
| **Constraint Satisfaction / WFC** | Building placement, interiors, dungeon gen, schedules | `region add` procgen, interior layout, NPC daily schedule CSP |
| **Signal Processing / Fields** | Weather, sound, scent, ley lines | 2D pressure/temp FFT advection, stealth sound, tracking scent |
| **Petri Nets / Process Calculi** | Crafting pipelines, metabolism, quest state machines | Keg→Preserves→Cask, NPC metabolism, quest concurrency |
| **Genetic Programming** | NPC micro-behaviors, ritual synthesis, spell synthesis | NPC "hoe then plant" programs, ritual step composition |

Key principle: deterministic given seed + parameters; reproducible, debuggable, parallel.

---

## 6. Phase 8 Layer Specs (condensed)

### 8.1 Soil & Plant Biology — ship order
8.1a Soil Chemistry (CA + fields) → 8.1b Water Table (CA + fields) → 8.1c Plant Genetics
(EA + L-System + graph) → 8.1d Pest/Disease (agents + CA + graph) → 8.1e Forest Ecology (below).

### 8.1e Forest Ecology & Tree Evolution — key elements
> **Shipped 2026-08-19 as a cell-based model** (simpler than the full individual-based
> vision below, but covering all 8 steps). Trees live on map cells as `TreeState`
> with a 16-allele genome; seed agents + per-cell seed banks; LAI light field;
> carbon/water economy; drift + selection; mycorrhizal network diffusion;
> old-growth + disturbance legacy; player planting/chopping feedback.
- **Tree individual**: genome (16 alleles: growth_rate, wood_density, shade_tol, drought_tol,
  seed_mass, root_depth, branching, phenology), L-System morphology (height/dbh/crown/root),
  carbon pools (leaf/sapwood/heartwood/root/storage/repro), hydraulic state (psi, conductivity,
  embolism), age + cohort_id. *(impl: single-pool biomass + water scalar per cell)*
- **ForestChunk**: `vector<TreeIndividual>`, seed bank (species × age class), mycorrhizal
  network graph, precomputed seasonal light field, disturbance legacy (fire age, windthrow
  mounds, nurse logs), allele frequencies per species.
- **Dynamics**: daily photosynthesis = f(PPFD, temp, VPD, leaf_N, genome); carbon allocation
  (height/radial/root/storage/repro); mortality from carbon deficit or hydraulic failure;
  mast years via climate cue + resource threshold; seed dispersal kernels; succession
  assembly (pioneer → mid → climax); breeder's equation allele shift per cohort; fire/wind
  disturbance CA + legacy; old-growth emergence.
- **Town Consciousness biases** (`adaptations.json` → `forest`): fire_suppression,
  harvest_pressure, planting_genotypes, climate_velocity, co2_fertilization, deer_browsing_pressure.
- **Emergent phenomena**: shifting treelines, genetic rescue, mycorrhizal collapse after
  clear-cut, mast-year synchrony, old-growth persistence, deer/defense arms race.
- **Player feedback**: clear-cut → pioneer pulse + erosion; selective harvest → age/genetic
  shifts; planting → genotype introduction; fire suppression → catastrophic-fire risk.
- NatureMind (shipped) consumes these to bias procgen/storm/disaster/foraging.

### 8.2 Atmosphere
Signal fields + spectral advection (FFT) + cloud/precip CA + storm-front L-systems; terrain
microclimates; bias: `weather_tendency`.

### 8.3 Structural
Building decay CA (rot/erosion), foundation stress fields (CSP integrity), tool wear grammar,
fire spread CA (fuel + wind + humidity; basement hatch = fire escape path).

### 8.4 Creature biology
Metabolism Petri nets (hunger/thirst/energy/body_temp/circadian), disease on contact graph,
aging L-systems, social/emotional graph rewriting; outputs: population immunity, social graph,
metabolic state per NPC.

### 8.5 Disasters & cascades
Petri net cascade logic + spatial CA + graph rewriting; risk from state (dry CA + pests +
low water = wildfire risk); propagation (fire → smoke → respiratory → schedule → economy →
horror); recovery (rain, predators, trade-route rewrite); memory persists in Town Consciousness.

---

## 7. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| LoRA too slow for daily consolidation | Q4_K_M quantized (done); batch consolidation; KV cache; 4 cores |
| Memory grows unbounded | town_log ring buffer (7 days); town_memory fixed schema ~50 KB; bounded memories per agent |
| Adaptations oscillate | Damping `new = 0.3*proposed + 0.7*old`; smooth-delta training |
| Player feels "manipulated" | `/town/why` transparency; adaptations are biases not overrides |
| Scope creep | Phase 7 shipped core loop only; each 8.x separate PR; quest templates data-driven |
| Cognitive systems conflict | LLM consolidation acts as tiebreaker/authority on adaptations.json |

---

## 8. Success Criteria ("Conscious Game" feel)

1. **Replayability**: two saves, same player, diverge visibly by Day 30.
2. **Hardware adaptivity**: stable tick rate on i3-4160 *and* Ryzen 9 without config.
3. **Narrative coherence**: player recounts a *story* that happened — not a quest completed.
4. **Surprise**: developer observes behavior not explicitly coded (e.g., NPC bucket brigade
   because Town Consciousness biased `helpfulness` after player helped them).

---

## 8. Known Issues

| Issue | Status | Notes |
|-------|--------|-------|
| Weather system: `weather_of_day_adapted` returns Sunny for days that should be Rainy (e.g., day 5). Base `weather_of_day` function works correctly, but `weather_of_day_adapted` returns Sunny when all modifiers are zero. Root cause unknown — possibly early return logic or modifier loading issue. | Open | Blocks water table recharge testing; water table simulation works correctly when rain occurs. |
| Well refill message: Watering can refills from well correctly (count increases) but displays "You water the soil" instead of "Drew X units from well". | Minor | Logic executes correctly; only message is wrong. |
| Compiler warnings: fixed in a dedicated cleanup pass (2026-08-19). Swept `src/main.cpp`, `include/world.hpp`, `src/world.cpp`, `src/llama_wrapper.cpp`, `src/town_consciousness.cpp`, `src/social_cognition.cpp`, `src/nature_mind.cpp`, `src/village_mind.cpp` — `-Wconversion`, `-Wsign-conversion`, `-Wshadow`, `-Wunused`, `-Wparentheses`, deprecated llama.cpp API. The full `ashgrove_server` target now builds with **zero warnings** under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow ...`. Third-party warnings (httplib.h `-Wformat-nonliteral`/`-Wunused-parameter`, llama.cpp `-Wshadow`) are suppressed via scoped `#pragma GCC diagnostic` around the includes. | Resolved | Cleanup pass before ROADMAP 2.4. |
| Boot-at-04:00 consolidation: if the save clock (`time` in `save.json`) loads inside the consolidation window (day_seconds 734–766), a consolidation can fire during llama boot init and the first decode occasionally dies silently (rare/intermittent, exact boot clock only). Normal boots and normal-play consolidations are stable. Fix: gate the first consolidation on an "LLM ready" flag instead of clock position. | Open (hardening) | Discovered 2026-08-19 during 2.5 verification (see MISTAKES.md M19). |

---

## 9. Doc Map

| File | Role |
|------|------|
| `docs/ROADMAP.md` | **This file** — all open work, priority/blocking ordered |
| `docs/shipped-features.md` | Everything shipped (R0–R16, A1–A9, Phases 1–8, cognitive core, aggregates, QA) |
| `docs/cognitive-architecture.md` | Cognitive architecture spec (Tier 1/2, components, integration) |
| `AGENTS.md` | Agent SOP + rules (read first every session) |
| `MISTAKES.md` | Living mistake log (read first every session) |
| `CHANGELOG.md` | Version history |
| `tools/dataset_schema.md` | Training dataset schemas |
