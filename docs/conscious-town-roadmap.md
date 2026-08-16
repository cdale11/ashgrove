# Ashgrove Valley — Conscious Town & Deep Simulation Roadmap
## Phased Plan for "Living, Breathing, Conscious Game" Vision

---

## Executive Summary

**Core Concept**: The town of Ashgrove Valley is a single, persistent, learning entity. Every system — soil microbiome, weather patterns, NPC minds, economy, disaster response, even the game's own performance tuner — shares a **global memory** and **adaptation loop**. The player isn't playing *against* scripted systems; they're inhabiting a world that *observes, learns, and adapts* to them.

**Architecture**: Two-tier as before, but Tier 1 (LLM) becomes the **Town Consciousness** — a single model instance with persistent context that:
- Ingests all world logs (player actions, NPC decisions, weather, economy, biology)
- Maintains a **Town Memory** (vector DB + structured facts)
- Emits **global adaptations** (procgen biases, NPC personality drifts, economic shifts, disaster probabilities, performance tunables)
- Drives **narrative anchors** (the Witch, the Basement, the Fog, the Mayor's secret) toward player-specific emergent arcs

**LoRA Student Role**: The fine-tuned Qwen2.5-0.5B *is* the Town Consciousness — small enough to run every tick (or every N ticks) on CPU, specialized for Ashgrove's JSON schemas (intent, adaptation, narrative beat, performance config).

---

## Phase 7: Conscious Town Core (The "Brain" + Memory)

**Goal**: Implement the persistent Town Consciousness that observes everything, learns patterns, and emits adaptations.

### 7.1 Global Event Log → Town Memory Pipeline
- **Input**: Every `world_change`, `tick`, `player_cmd`, `npc_action`, `economy_shift`, `horror_event`, `sanity_change`, `weather_update`, `crop_growth`, `building_decay` → appended to `data/town_log.jsonl` (structured, not just text).
- **Consolidation Job** (runs daily at 04:00 in-game): Town Consciousness (LoRA) reads last 24h of logs + long-term memory → outputs:
  - `town_memory.json` — structured facts: `{player_habits, npc_relationships, economic_trends, ecological_state, discovered_secrets, performance_profile}`
  - `adaptations.json` — tunables for next day: `{procgen_biases, npc_personality_drifts, shop_price_modifiers, disaster_chance, weather_tendency, horror_intensity, thread_pool_sizes, cache_sizes}`

### 7.2 Town Consciousness API (Internal)
```
POST /town/observe     # Systems push structured events (batched)
POST /town/consolidate # Daily job trigger
GET  /town/memory      # Read current memory (for debugging/inspection)
GET  /town/adaptations # Read current day's adaptations (consumed by systems)
```

### 7.3 Adaptation Consumers (Phase 7 ships the hooks; Phase 8+ deepens each)
| System | Adaptation Inputs | Effect |
|--------|-------------------|--------|
| Procgen | `biome_preference`, `ruin_density`, `resource_richness` | Next chunk generated leans toward player's explored patterns or *away* from them (variety) |
| NPC Scheduler | `personality_drift[<npc>]`, `schedule_bias` | NPCs subtly shift routines, dialogue topics, gift preferences |
| Economy | `demand_shift[<commodity>]`, `price_elasticity` | Prices adapt to player's selling/buying patterns; market crashes/booms feel "responsive" |
| Weather | `pressure_bias`, `humidity_drift`, `storm_chance` | Climate slowly shifts — droughts, wet seasons, unnatural fog correlate with horror cycle |
| Horror | `intensity`, `basement_unlock_progress`, `night_event_weight` | Sanity drain rate, night event frequency, phantom sightings scale to player's fear exposure |
| Performance Tuner | `thread_pool_target`, `cache_budget`, `tick_budget_ms` | **OS-like**: Town measures frame time, memory, CPU % → adjusts its own simulation fidelity to hit 60 FPS / target tick rate on *this hardware* |

### 7.4 Performance Tuner (Self-Optimizing Game)
- **Telemetry**: Per-tick `sim_ms`, `render_ms`, `net_ms`, `mem_mb`, `cpu_%` logged.
- **Policy**: Town Consciousness outputs `performance_config.json` each consolidation:
  ```json
  {
    "thread_pool": {"world_gen": 2, "npc_ai": 2, "weather": 1, "io": 1},
    "tick_budget_ms": 16,
    "chunk_load_radius": 3,
    "npc_decision_interval_ticks": 5,
    "weather_update_interval_ticks": 20,
    "save_compression": "zstd:3",
    "llm_inference_interval_ticks": 10
  }
  ```
- **Feedback Loop**: Next day's telemetry compared to targets → policy adjusted. Works on *any* hardware (i3-4160 → Threadripper).

### 7.5 Deliverables
- `src/town_consciousness.cpp/h` — wrapper around LoRA inference + memory consolidation
- `data/town_log.jsonl`, `data/town_memory.json`, `data/adaptations.json`
- `tools/consolidate_town.py` — daily job (cron or in-game 04:00 trigger)
- Integration hooks in: `World::tick()`, `NPC::decide()`, `Weather::update()`, `Economy::recalculate()`, `Procgen::generate_chunk()`, `PerformanceMonitor`
- **Test**: Run 10 in-game days → verify `adaptations.json` changes non-trivially and systems consume it

---

## Non-AI Emergence Toolkit (Used Throughout Phase 8)

The deep simulation layers use **deterministic, parallelizable, non-ML techniques** that naturally produce emergence. The Town Consciousness (LoRA) only *biases parameters* — the mechanics run on these:

| Technique | Where It Shines | Example in Ashgrove |
|-----------|-----------------|---------------------|
| **L-Systems** | Plant growth, branching structures, river networks, cave systems, fungal mycelium | Crop morphology (trellis vs bush), tree growth stages, root networks for water/nutrient uptake, procgen cave/ruin layouts |
| **Graph Rewriting / Graph Grammars** | NPC social networks, quest dependency graphs, building connection graphs, disease transmission networks | NPC relationship web (rewrite rules: "gift → edge weight +0.1"), quest template expansion, building upgrade trees, pathogen spread on contact graph |
| **Context-Free / Attribute Grammars** | DSL construction, narrative beat generation, item crafting recipes, building blueprints | `dsl barn @ x,y` parser, night-event chapter grammar, crafting recipe DAG, building interior layout grammar |
| **Evolutionary Algorithms (μ+λ, NEAT, CPPN)** | Creature AI (behavior trees), plant breeding, NPC personality drift, procgen parameter optimization | Wild animal behavior evolution, crop allele optimization (player selects → EA breeds), NPC schedule adaptation via neuroevolution, Town Consciousness tunes its own hyperparams |
| **Cellular Automata / Grid Automata** | Fire spread, water table diffusion, disease diffusion, fog/smoke, moss/lichen growth, corruption spread | Fire CA (wind+humidity+fuel), groundwater Darcy→CA approximation, pathogen diffusion, horror fog CA, basement corruption CA |
| **Particle / Agent-Based Simulation** | Pollen drift, insect swarms, rain droplets, debris, magic particles, sanity "echoes" | Pollination network (pollen agents), pest swarm movement, rain particle → soil moisture, horror sanity echoes as particles |
| **Constraint Satisfaction / Wave Function Collapse** | Building placement, interior layout, dungeon/room generation, NPC schedule solving | `region add` procgen (WFC tiles), interior room layout, NPC daily schedule CSP (hard constraints: work hours, soft: preferences) |
| **Signal Processing / Fields** | Weather fields, sound propagation, scent trails, ley lines, magical resonance | 2D pressure/temp fields (FFT-based advection), sound attenuation for stealth, scent trails for tracking, ley line magic network |
| **Petri Nets / Process Calculi** | Crafting pipelines, machine processing chains, metabolic pathways, quest state machines | Keg→Preserves→Cask pipeline, NPC metabolic pathways, quest state machine with concurrency |
| **Genetic Programming / Program Synthesis** | NPC micro-behaviors, tool use discovery, ritual generation, magic spell synthesis | NPC learns "use hoe then plant" as program, ritual step synthesis, spell component combination |

**Key Principle**: These systems are **deterministic given seed + parameters**. Town Consciousness only *adapts parameters* (mutation rates, rule probabilities, field biases, constraint weights). This keeps the simulation reproducible, debuggable, and parallel — while the *ensemble* produces open-ended emergence.

---

## Phase 8: Deep Simulation Layers (Deterministic Emergence)

**Goal**: Replace abstracted systems with mechanistic simulations using the toolkit above. Each layer ships incrementally; Town Consciousness biases parameters daily.

### 8.1 Soil & Plant Biology (Farming Core → Mechanistic)
| Layer | Technique(s) | Mechanistic Model | Data for Town Consciousness |
|-------|--------------|-------------------|----------------------------|
| **Soil Chemistry** | CA (diffusion), Signal Fields | NPK + pH + organic matter + microbiome diversity per tile. Rain leaches N (CA diffusion); compost builds OM; root exudates (particle agents) feed microbes. | `soil_health[gx][gy]`, `nutrient_profile`, `microbiome_diversity` → yield, disease resistance, flavor |
| **Plant Genetics** | Evolutionary Algorithms (allele EA), L-Systems (morphology) | Each variety = allele set (growth_rate, yield, drought_tol, cold_tol, disease_res, flavor, seed_viability). Cross-pollination (graph edges: pollinator agents) → offspring alleles via EA recombination. L-System encodes morphology (trellis, bush, vine, root depth). Giant crops = rare homozygous + perfect conditions. | `crop_genome[plant_id]`, `pollination_network` (graph), `morphology_lsys` → seed saving, breeding, heirloom discovery |
| **Water Table** | CA (Darcy approximation), Signal Fields | 2D groundwater: recharge (rain particles), discharge (wells, rivers), lateral flow (CA diffusion). Irrigation pumps lower local cone of depression. | `water_table[gx][gy]`, `well_yield`, `cone_of_depression` → irrigation decisions, well drying disaster |
| **Pest/Disease** | Agent-Based (pest agents), CA (diffusion), Graph (transmission network) | Pest agents (aphids, beetles) move on wind fields, feed on crops, lay eggs. Predators (ladybugs) hunt pests. Fungal spores = CA diffusion + humidity bias. Companion planting = graph edge modifiers (repellent edges). | `pest_pressure[region]`, `predator_population`, `spore_load[gx][gy]`, `companion_graph` → disaster_chance |

**Ship Order**: 8.1a Soil Chemistry (CA + Fields) → 8.1b Water Table (CA + Fields) → 8.1c Plant Genetics (EA + L-System + Graph) → 8.1d Pest/Disease (Agents + CA + Graph) → **8.1e Forest Ecology & Tree Evolution** (detailed below).

### 8.1e Forest Ecology & Tree Evolution (Detailed Biology)

**Goal**: Forests as living, evolving ecosystems — not static terrain. Every tree is an individual with genome, physiology, and history. Forests undergo succession, migration, and adaptation over in-game years.

| Subsystem | Technique(s) | Mechanistic Detail |
|-----------|--------------|-------------------|
| **Tree Individual Physiology** | L-Systems (3D morphology), Petri Nets (carbon/water balance), Signal Fields (light/hormone transport) | Each tree: `genome` (alleles for max_height, wood_density, shade_tol, drought_tol, seed_mass, root_depth, branching_angle, phenology). L-System grows rings annually: `height`, `dbh`, `crown_radius`, `root_extent` from carbon allocation (photosynthesis - respiration - exudates). Hormone fields (auxin, cytokinin) transport via xylem/phloem (CA on tree graph) → apical dominance, branch shedding, reaction wood. |
| **Forest Light Environment** | Ray-marching / Voxel Cone Tracing (precomputed per chunk), CA (canopy gap dynamics) | 3D light field `PPFD[gx][gy][gz]` computed from tree L-System crowns. Gaps from falls/death → light pulses → understory recruitment. Seasonal sun angle shifts field. |
| **Carbon & Water Economy** | Petri Nets (C/N/P pools), CA (soil-plant-atmosphere continuum) | Daily: `photosynthesis = f(PPFD, temp, VPD, leaf_N, genome)`. Carbon allocated to: height growth (light competition), radial growth (hydraulic safety), root growth (water access), storage, reproduction. Hydraulic failure (cavitation) risk from `water_table` + `soil_potential` + `genome.drought_tol`. Mortality if carbon < maintenance or hydraulic failure. |
| **Reproduction & Dispersal** | Agent-Based (seed agents), L-System (fruit/cone morphology), Graph (dispersal kernels) | Mast years (synchronized via climate cue + resource threshold). Seed agents: wind (dispersal kernel from `wind_field` + `seed_mass`), animal (graph edges: frugivore movement), gravity. Germination = `f(light, moisture, temperature, seed_bank_age, genome.shade_tol)`. Seed bank in soil (age-structured, decay rate). |
| **Forest Succession** | Graph Rewriting (community assembly rules), CA (gap-phase dynamics) | Species pool = regional (procgen biome) + migrants. Assembly rules: pioneer (high light, fast growth, short life) → mid-succession (shade tolerant, denser wood) → climax (high shade tol, long life, high wood density). Gap creation (windthrow, disease, fire, player) → rewrites local community graph. |
| **Intraspecific Evolution** | Evolutionary Algorithm (allele frequency per cohort), Quantitative Genetics | Each cohort (trees established same year) = population. Selection: survival to reproduction + fecundity. Allele frequencies shift: `Δp = h² * S` (breeder's equation). Heritability `h²` from genome architecture. Migration from adjacent chunks (pollen/seed flow). **Result**: Local adaptation — valley floor trees evolve deeper roots; ridge trees evolve cavitation resistance; player's clear-cut zones select for pioneers. |
| **Interspecific Coevolution** | Graph Rewriting (interaction network), EA (trait matching) | Mycorrhizal network: tree↔fungus graph edges (C-for-N/P trade). Partner choice → rewiring. Herbivore pressure (deer, insects) → defense allocation (tannins, thorns) evolves. Pollinator specialization → flower trait matching. |
| **Disturbance & Legacy** | CA (fire/wind), Signal Fields (soil legacy), Graph (nurse logs) | Fire CA leaves `char_layer` (nutrient pulse, pH shift). Windthrow creates `tip_up_mounds` (microtopography, seedbeds). Nurse logs (deadwood) → moisture retention, mycorrhizal inoculum, seedling substrate. Legacy persists decades → influences successional trajectory. |
| **Old-Growth Emergence** | L-System (complex crowns), Graph (canopy strata), Petri Nets (carbon storage) | Multi-century trees develop: complex reiterated crowns (L-System recursion), epicormic branching, heart rot cavities (habitat), massive carbon stocks. Ecosystem functions: carbon sequestration rate, biodiversity support (epiphytes, cavity nesters), hydraulic redistribution (nighttime water lift). |
| **Player Interaction Feedback** | All above | Clear-cut → pioneer pulse + soil erosion (CA) + microclimate shift. Selective harvest → alters age structure, genetic diversity. Planting → introduces genotypes, may swamp local adaptation. Fire suppression → fuel accumulation → catastrophic fire risk. Town Consciousness tracks `forest_state[chunk]` → biases procgen, disaster_chance, NPC forager yields. |

**Data Structures** (per chunk, serialized):
```cpp
struct TreeIndividual {
  Genome genome;           // 16 alleles, fixed loci
  LSystemState morphology; // height, dbh, crown_poly, root_graph
  CarbonPools carbon;      // leaf, sapwood, heartwood, root, storage, repro
  HydraulicState hydro;    // psi_leaf, psi_root, conductivity, embolism
  int age; int cohort_id;  // for evolutionary tracking
  ChunkCoord location;
};

struct ForestChunk {
  vector<TreeIndividual> trees;           // all living trees
  SeedBank seed_bank;                     // species × age-class counts
  MycorrhizalNetwork myco_graph;          // tree↔fungus edges
  LightField PPFD;                        // precomputed seasonal
  DisturbanceLegacy legacy;               // fire_age, windthrow_mounds, nurse_logs
  AlleleFrequencies allele_freq[species]; // for evolution tracking
};
```

**Town Consciousness Biases** (daily `adaptations.json`):
```json
{
  "forest": {
    "fire_suppression": 0.0,           // 0=nature, 1=total suppression
    "harvest_pressure": 0.0,           // player cutting rate
    "planting_genotypes": [],          // player-introduced genomes
    "climate_velocity": 0.0,           // forced migration rate
    "co2_fertilization": 1.0,          // photosynthesis multiplier
    "deer_browsing_pressure": 0.5      // herbivory on seedlings
  }
}
```

**Emergent Phenomena** (no scripting):
- **Shifting treelines** as climate_velocity pushes species upslope
- **Genetic rescue** when player plants diverse seedlings in inbred stand
- **Mycorrhizal collapse** after clear-cut → regeneration failure
- **Mast year synchrony** across chunks via climate cue → predator satiation
- **Old-growth persistence** in player-protected groves → unique habitat, carbon bank
- **Evolutionary arms race** between deer browsing pressure and seedling defense allocation

**Ship Order within 8.1e**:
1. **Tree Individual Physiology** (L-System + Petri Net + Hormone CA) — core tree object
2. **Light Environment & Carbon/Water** — drives growth, mortality, competition
3. **Reproduction & Dispersal** — seed agents, seed bank, germination
4. **Succession & Community Assembly** — graph rewriting, gap dynamics
5. **Intraspecific Evolution** — allele tracking, breeder's equation per cohort
6. **Mycorrhizal & Coevolution** — fungus graph, herbivore/pollinator matching
7. **Disturbance Legacy & Old-Growth** — fire/wind CA, nurse logs, multi-century emergence
8. **Player Feedback Integration** — all systems respond to player actions

---

### 8.2 Atmospheric & Weather Physics
- **Technique**: Signal Fields (2D grids) + Spectral Advection (FFT) + CA (cloud/precip) + L-System (storm fronts).
- **Model**: Pressure/temp/humidity fields on chunk grid. Solar forcing → pressure gradients → wind (vector field). Moisture advection (semi-Lagrangian). Cloud formation (CA threshold on humidity+cooling). Precipitation (particle fall). Microclimates from terrain (elevation, water, forest cover).
- **Town Consciousness Bias**: `weather_tendency` → shifts spectral forcing amplitudes, CA thresholds, storm L-System axiom.
- **Output**: `weather_state[chunk]` (temp, humidity, wind, precip, cloud, pressure) → farming, NPC comfort, horror fog, fire spread.

### 8.3 Structural & Material Physics
- **Technique**: CA (rot/erosion/fire), Constraint Satisfaction (building integrity), Signal Fields (stress distribution).
- **Building Decay**: Wood rot CA (humidity+time), stone erosion CA (rain+acid), foundation stress field (water table + weight → CSP for structural integrity). `repair` = CSP action consuming correct materials.
- **Tool Wear**: Attribute Grammar (tool → material → wear curve). Sharpening = grammar rewrite.
- **Fire Spread**: CA on flammability grid (fuel load, wind vector, humidity). Basement hatch = fire escape (graph pathfinding).
- **Town Consciousness Bias**: `disaster_chance` → CA ignition probability, wind field magnitude.

### 8.4 Creature Biology (NPCs + Wildlife)
- **Technique**: Petri Nets (metabolism), Graph Rewriting (social), Evolutionary Algorithms (behavior), Attribute Grammar (needs).
- **Metabolism**: Petri net places (hunger, thirst, energy, body_temp, circadian) + transitions (eat, drink, sleep, work). Nutrient profile from food items.
- **Disease**: Graph rewriting on contact network (NPC↔NPC, NPC↔animal, NPC↔player). Immunity = prior exposure tokens in Petri net.
- **Aging/Growth**: L-System (life stages: child→adult→elder) with attribute grammar stat curves.
- **Social/Emotional**: Graph rewriting rules (gift → edge_weight++, horror_event → fear_propagation). Town Consciousness biases rule probabilities (`npc_personality_drift`).
- **Outputs**: `population_immunity[pathogen]`, `social_graph`, `metabolic_state[npc]`, `emotional_needs[npc]`.

### 8.5 Disaster & Cascade System
- **Technique**: Petri Nets (cascade logic), CA (spatial spread), Graph Rewriting (system coupling), Signal Fields (intensity).
- **Trigger**: Town Consciousness `disaster_chance` + simulation state (dry CA + high pest agents + low water table = wildfire risk).
- **Propagation**: Fire CA → smoke field → respiratory Petri net transitions → NPC schedule CSP failure → economy Petri net dip → horror intensity field up.
- **Recovery**: Rain CA extinguishes fire, predator agents eat pests, trade route graph rewrites restore economy. **Memory persists** in Town Consciousness → biases future procgen (WFC constraints).

---

## Phase 9: Authored Backbone + Emergent Branches (Main Questline)

---

## Phase 9: Authored Backbone + Emergent Branches (Main Questline)

**Goal**: A quest arc with **fixed narrative anchors** but **emergent paths** between them. The Town Consciousness *is* the quest director.

### 9.1 Narrative Anchors (Fixed, Discoverable in Any Order)
| Anchor | Location | Core Revelation | Emergent Gates |
|--------|----------|-----------------|----------------|
| **The Mayor's Ledger** | Town Hall (night, high hearts) | Town's founding pact with *something* under the valley | Requires: Mayor ≥8 hearts, basement_unlocked, specific night event witnessed |
| **The Witch's Bargain** | Witch's Hut (basement access) | The Fog is a living entity; the town feeds it sanity | Requires: Witch ≥6 hearts, 3+ night events logged, sanity <50 once |
| **The Traveler's Map** | Random encounter (procgen chunk) | The valley is one of many; the basement connects them | Requires: explore 5+ chunks, find 2+ ruins, Traveler met 3× |
| **The Old Mill Machine** | Under-map basement (deep) | A device that *writes* the town's reality — the "OS" | Requires: all 3 above + horror_cycle ≥3, specific secret found |
| **The Choice** | Old Mill Machine room | **Player rewrites one rule of the valley** (permanent, saves to world seed) | Requires: all 4 anchors + Town Consciousness trust ≥ threshold |

### 9.2 Emergent Branches (Town Consciousness Generates)
Between anchors, the Town Consciousness spawns **personalized quest chains** based on:
- **Player's playstyle** (farmer → crop blight crisis; miner → cave-in rescue; social → NPC disappearance; horror → sanity anchor)
- **NPC relationship graph** (spouse kidnapped, rival sabotages, mentor reveals secret)
- **World state** (drought → water rights conflict; bumper crop → market crash; plague → quarantine)
- **Performance profile** (on weak hardware → fewer NPCs, simpler weather → quests adapt to simpler sim)

**Quest Template**: `tools/quest_templates/` — JSON + LoRA prompt. Town Consciousness fills slots at runtime:
```json
{
  "anchor": "mayor_ledger",
  "branch_type": "investigate",
  "slots": {"suspect_npc": "<high_hearts_npc>", "clue_location": "<unexplored_chunk>", "time_pressure": "<days_until_festival>"},
  "outcomes": {"success": "unlocks_next_anchor", "failure": "increases_horror_intensity", "partial": "npc_relationship_shift"}
}
```

### 9.3 Progression Feel (Non-Linear)
- **No quest log "Main Quest"** — instead, **Journal** auto-records discoveries: "Found a torn page in the Mayor's drawer...", "The Witch whispered of a machine..."
- **Anchor completion** = permanent world change (new basement area, new NPC dialogue, new procgen biome, new crafting recipe).
- **Player can ignore anchors** — but Town Consciousness escalates horror/economy/weather until engaged. "The valley demands attention."

---

## Phase 10: Integration & Polish — The Conscious Loop Closed

### 10.1 Full Loop Verification
1. Player acts → `town_log` entry
2. Daily consolidation → `town_memory` + `adaptations`
3. Systems consume adaptations → world changes
4. Player observes changes → new actions
5. Town Consciousness infers player intent → adjusts narrative anchors' accessibility
6. **Emergent story** unique to this save, this hardware, this player

### 10.2 Debug/Inspect Tools (For Development + Player Curiosity)
- `/town/inspect` — shows current memory summary, adaptation deltas, performance config
- `/town/why` — explains *why* a specific adaptation was made (LoRA reasoning trace)
- `/town/reset` — dev only: wipe memory, restart fresh

### 10.3 Save/Load Persistence
- `town_memory.json`, `adaptations.json`, `performance_config.json` serialized in save.
- New game = fresh consciousness (or "legacy" mode: inherit memory from previous save).

---

## Dependencies & Ordering

| Phase | Depends On | Can Start After |
|-------|------------|-----------------|
| 7 (Conscious Town Core) | Phase 6 (Horror, Event Bus, Log Collector), **LoRA trained & swapped to Tier 1** | **Immediately after LoRA merge** |
| 8.1 (Soil/Plant) | 7 (adaptation hooks) | After 7.1-7.3 |
| 8.2 (Weather) | 7, 8.1 (water table) | After 7 |
| 8.3 (Structural) | 7 | After 7 |
| 8.4 (Creature Bio) | 7, 8.1 (nutrition) | After 7 |
| 8.5 (Disasters) | 8.1-8.4 | After 8.1-8.4 |
| 9 (Questline) | 7 (Town Consciousness), 6 (anchors exist) | After 7.1-7.3 |
| 10 (Integration) | All above | Last |

---

## Immediate Next Steps (Post-LoRA)

1. **Merge LoRA → Q4_K_M → swap into Tier 1** (current blocker).
2. **Implement `TownConsciousness` class** with:
   - `observe(event)` — push to ring buffer (thread-safe)
   - `consolidate()` — run LoRA inference on buffer + memory → write `town_memory.json`, `adaptations.json`
   - `get_adaptation(system)` — read current day's config
3. **Wire into `World::tick()`**: call `consolidate()` at in-game 04:00; push events from all systems.
4. **Add adaptation consumers** (stubs first): Procgen, NPC, Economy, Weather, Horror, Performance.
5. **Run 30-day soak test** — verify adaptations change, no crashes, performance tuner converges.

### Known Gap: LoRA Student Only Trained on Intent Parsing

**Status: DEFERRED — do NOT start without user sign-off.** The Qwen2.5-0.5B LoRA adapter
(`data/lora-adapter/`, merged → `data/qwen2.5-0.5b-ashgrove-q4_k_m.gguf`) was trained exclusively
on command-intent JSON (`{"action", "parameters"}`). When prompted with the Town Consciousness
consolidation task, it emits intent-shaped JSON (e.g. `{"status":"new","parameters":{...}}`),
which `parse_llm_response` cannot map onto `procgen/npc/economy/weather/horror/performance`,
so adaptations stay at defaults. Verified live 2026-08-16.

**Planned work (user approved: "we will do training later"):**
1. Build a town-consciousness dataset: for each canonical adaptation JSON (all six sections with
   number/object values), generate natural "town state" inputs in the consolidation prompt format
   (`You are the Town Consciousness of Ashgrove Valley...` + memory/adaptations/events sections).
2. Append to the Phase 8 training split (`/tmp/opencode/train.json` / `val.json`) alongside intent
   examples so the model learns both formats.
3. Retrain with `tools/train_lora.py` (base `Qwen/Qwen2.5-0.5B-Instruct`, r16/alpha32), merge +
   quantize to Q4_K_M, swap into `data/qwen2.5-0.5b-ashgrove-q4_k_m.gguf`.
4. Re-run the 30-day soak test and confirm `adaptations.json` values drift from defaults after
   consolidation with non-empty event buffers.

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| LoRA too slow for daily consolidation (1.5h train → inference must be <5s) | Quantize to Q4_K_M (done), batch consolidation, cache KV, run on 4 cores |
| Memory grows unbounded | `town_log` ring buffer (last 7 days); `town_memory` = structured summary (fixed schema, ~50 KB) |
| Adaptations oscillate / destabilize | Damping: `new = 0.3 * proposed + 0.7 * old`; Town Consciousness trained to output smooth deltas |
| Player feels "manipulated" not "emergent" | Transparency: `/town/why` shows reasoning; adaptations are *biases* not hard overrides |
| Scope creep | Phase 7 ships **core loop only**; each 8.x is a separate PR; 9 quest templates data-driven |

---

## Success Criteria for "Conscious Game" Feel

1. **Replayability**: Two saves, same player, diverge visibly by Day 30 (different NPC routines, crop yields, weather patterns, quest branches).
2. **Hardware Adaptivity**: Game runs at stable tick rate on i3-4160 *and* Ryzen 9 without config changes.
3. **Narrative Coherence**: Player can recount a *story* that happened to them — not a quest they completed.
4. **Surprise**: Developer observes behavior not explicitly coded (e.g., NPCs spontaneously form a bucket brigade during fire because Town Consciousness biased `helpfulness` up after player helped them).

---

*This plan is a living document. Update `docs/vision-and-roadmap.md` and `docs/shipped-features.md` as each phase ships.*