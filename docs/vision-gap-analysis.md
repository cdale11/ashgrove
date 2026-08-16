# Ashgrove Valley — Vision Gap Analysis & Audit
## Current State vs. User Vision (2026-08-16)

**Purpose**: Audit existing docs + code against the full user vision. Each gap maps to a roadmap phase or design question.

---

## 1. Game Type & Core Loops

| Vision Element | Current State | Gap | Phase |
|---|---|---|---|
| **Text-first browser MUD/RPG** | HTTP/JSON API only; no WebSocket; no browser client | No WebSocket push, no PIXI.js client, no browser UI | 0/4 |
| **Stardew-like farming** | Crops, fertilizer, scarecrow, seasons | No animals (chickens/cows/goats), no cooking/recipes, no sprinklers/bee houses/casks, no festivals | 1/2 |
| **Stardew-like gathering/crafting** | Basic items, mining, foraging | No processing machines (kegs, preserves, mayo), no cellar aging, no skill perks | 2 |
| **Stardew-like progression** | Building condition, NPC hearts (partial) | No heart system persistence, no gift preferences, no marriage/children/roommates | 3 |
| **Stardew-like economy** | Market prices, shop price mods | No living economy (supply/demand recalc, market crashes/booms, job board) | 5 |
| **Stardew-like building** | DSL construction, farmhouse repair | No buyable plots, no interior rooms (barn, greenhouse, cellar, shrine), no seamless procgen transition | 4 |
| **Higurashi-like horror** | Sanity meter, night events, phantom sightings, horror_intensity adaptation | No authored horror narrative structure, no cyclical secrets, no meta-narrative breaks, no perception filters (hallucinations, distorted dialogue, false UI) | 6 |
| **Recurring runs / death & knowledge across runs** | Single world save only | No run system, no knowledge persistence across runs, no death consequences across runs | 7+ |
| **Disco Elysium-like cognition** | Cognitive architecture spec (7.7), CognitiveCore implemented | Not wired into NPC tick; no internal voices, no skill-based dialogue, no political/philosophical depth | 7.7-7.9 |
| **Minecraft-like persistence** | World saves, building DSL, terrain changes (building decay) | No terrain changes (floods, river shifts, erosion, fires, ecological succession), no offline simulation when server stopped | 8.1e/8.3 |
| **Effectively infinite** | Chunk system, procgen footprints | No seamless authored→procgen transition, no procedural wilderness beyond map edge, no procedural stories from world state | 4/5/8 |

---

## 2. Authored Town Design (CRITICAL GAP)

| Vision Element | Current State | Gap |
|---|---|---|
| **Meticulously designed WITH USER through questions** | 22 buildings placed in `place_buildings()` with hardcoded coordinates | **ZERO user collaboration on town design**. No district planning, no NPC home design, no horror location placement, no railway, no island design. |
| **Geography: districts, buildings, interiors, NPC homes** | Regions exist (Stardrop Plaza, Whisper Wood, etc.) but buildings hardcoded | No district zoning, no interior design for 21/22 buildings, no NPC home personalization |
| **River/island, farms, forest, railway, horror locations** | River exists (N-S at x≈44), farm footprint, Whisper Wood footprint | No island, no railway, no authored horror locations (basement, witch hut, fog zones) |

**Action Required**: Before any further implementation, we need a **town design session** with the user to author the town geography, districts, buildings, interiors, NPC homes, horror locations.

---

## 3. Always-Running World (Offline Simulation)

| Vision Element | Current State | Gap | Phase |
|---|---|---|---|
| NPCs, businesses, crops, weather, seasons, ecology continue offline | Game loop runs when server up | **No offline simulation when server stopped** — no tick advancement on restart | 8+ |
| Terrain changes: floods, snow, heatwaves, drought, erosion, fires, river changes, barren soil, ecological succession | Building decay, snow compaction, storm windthrow (L7) | **No terrain-level changes**: no flood CA, no river migration, no erosion CA, no fire spread CA, no ecological succession | 8.1e/8.3 |
| Persistent consequences | Building damage, crop loss, NPC displacement | Limited to building/crop/NPC schedule; no terrain or ecological legacy | 8.1e |

---

## 4. Player Freedom & Progression

| Vision Element | Current State | Gap |
|---|---|---|
| Farming, fishing, gathering, crafting, building, exploring, socializing, investigating, trading | Farming, gathering, crafting (basic), building (DSL), exploring, socializing (basic) | **No fishing**, no investigating, no trading depth, no quest following |
| Natural, rewarding progression | Building condition, NPC schedule disruption | No skill trees, no perk system, no clear progression milestones |
| Build properties freely where terrain/rules permit | DSL build anywhere on owned plot | No plot ownership system, no zoning rules, no terrain suitability checks |

---

## 5. AI Architecture (Cognitive System)

| Vision Element | Current State | Gap | Phase |
|---|---|---|---|
| LLM ≤2B only for language/conceptual tasks | ✅ Specified in cognitive-architecture.md, LoRA training pipeline exists | Need to enforce in code (no direct state mutation) | 7.7+ |
| Actual cognition: persistent state + lightweight neural models | CognitiveState, TinyMLP, CognitiveCore implemented | **Not wired into NPC tick**; no MLP weights trained/loaded; no attention/emotion/memory/prediction/preferences/social cognition/goals/personality/self-model/world-model active | 7.7 |
| Mostly fixed weights + small bounded online adaptation | Specified in architecture | Not implemented (no online adaptation loops) | 7.7 |
| Important NPCs = individualized persistent minds | CognitiveRegistry holds per-agent cores | Not integrated with World NPCs; no tiered cognition (important vs lower-tier) | 7.7 |
| NPCs learn from each other, develop culture/beliefs/unexpected behavior | SocialEdge with imitation_target in CognitiveState | No cultural transmission implementation, no belief propagation | 7.8 |
| **Collective cognition**: groups, town, culture, economy, nature/ecology, whole world | Architecture spec has VillageMind, EconomyMind, NatureMind, CultureMind | **None implemented** — no aggregate cognitive states, no feedback loops | 7.9 |
| Mechanisms for consciousness-like behavior (not hard-coded) | Architecture spec | Not implemented | 7.9+ |
| Deterministic systems produce intelligent behavior | Non-AI Emergence Toolkit in roadmap | Soil/plant/forest/weather/creature mechanistic models partially specced, not implemented | 8.1-8.4 |

---

## 6. Horror & Narrative

| Vision Element | Current State | Gap |
|---|---|---|
| **Authored, original, high-quality horror story (Higurashi-inspired)** | Horror mechanics exist (sanity, night events, phantom sightings, horror_intensity) | **No authored narrative structure** — no cyclical chapters, no hidden truths, no character arcs, no meta-narrative breaks |
| Procedural quests emerge from world state, history, relationships, unresolved goals | Quest/Job structs in World | No quest generator sampling from world state |
| Death and knowledge matter across runs | Single world save | No run system, no cross-run knowledge |
| Effectively infinite through persistent simulation, construction, relationships, procedural stories, procedural wilderness | Chunk system, building DSL | No procedural story generator, no procedural wilderness beyond map |

---

## 7. Technical Constraints

| Vision Element | Current State | Gap |
|---|---|---|
| Authoritative world state deterministic & inspectable | Deterministic core, Event Bus | No inspectable causal traces, no deterministic replay |
| LLM never directly mutates state | Architecture specifies this | Not enforced in code (TownConsciousness emits adaptations but could theoretically emit invalid state) |
| Bounded memory, cognitive LOD | Specified in architecture | Not implemented (no memory bounds, no LOD for distant NPCs) |
| Efficient offline simulation | Game loop only runs when server up | No offline tick advancement |
| Deterministic seeds & causal/debug traces | World seeded, but no trace logging | No causal trace system |
| Target ~8GB RAM | Current ~2.6GB RSS + 5GB total | Within budget but no memory budgeting system |

---

## 8. Development Method

| Vision Element | Current State | Gap |
|---|---|---|
| Inspect → identify conflicts/reusable systems → ask design questions → propose roadmap → implement incrementally → test real player path → investigate anomalies | Being followed in this session | Need to formalize as process |
| Suggest improvements when they better support vision | Done in this audit | Ongoing |
| Do not silently alter major design decisions | Followed | Ongoing |
| No major rewrite until audit + architecture + gaps + proposed roadmap presented | **This document is that presentation** | Ready for discussion |

---

## 9. Specific Missing Systems (from vision-and-roadmap.md Phases)

| Phase | System | Status |
|---|---|---|
| 0 | WebSocket push, browser client (PIXI.js) | ❌ Not started |
| 1 | Animals (chickens, cows, goats), fishing, cooking, festivals | ❌ Not started |
| 2 | Kegs, preserves, mayo, bee houses, casks, sprinklers, greenhouse, skill perks | ❌ Not started |
| 3 | Heart system persistence, gift preferences, LLM dialogue, marriage/children/roommates, adaptive NPC schedules | ⚠️ Heart struct missing, gift prefs missing, LLM dialogue not wired, marriage missing |
| 4 | Procgen region generator, buyable plots, construction DSL, interior rooms (barn, greenhouse, cellar, shrine), seamless authored→procgen | ⚠️ Chunk system exists, DSL parse exists, buyable plots missing, interiors missing |
| 5 | Quest generator (templates + world state), job board, living economy | ❌ Quest/Job structs exist but no generator |
| 6 | Basement, night events (LLM narrative), sanity meter, perception filters, Higurashi/DDLC/Disco Elysium effects | ⚠️ Sanity meter partial, night events partial, perception filters missing |
| 7 | Log collector → fine-tuning, adaptive NPC schedules, weather reacts to irrigation, economy recalibrates, horror adapts | ⚠️ Log collector exists (cmdlog), fine-tuning pipeline exists, adaptive systems missing |
| 8 | Model distillation (Tier 0/1/2), CPU LoRA on Qwen2.5-0.5B, merge+quantize, eval harness | ⚠️ Training pipeline exists, student not deployed, eval harness exists |
| 9 | Documentation, tooling, API docs, plugin dev guide, Doxygen | 🔄 Ongoing |

---

## 10. Immediate Design Questions for User

Before proceeding, these need answers:

### Town Design (BLOCKING for all content)
1. **What are the districts/zones of Ashgrove town?** (Civic, Commercial, Residential, Industrial, Riverside, Woodland, Farmstead, Horror zones?)
2. **What are the 20-30 key buildings and their exact purposes?** (Not just "Carpenter Shop" — what rooms, what NPCs live/work there, what services?)
3. **Where are the horror locations?** (Basement entrance, Witch's hut, Fog zones, Ritual sites, Abandoned places?)
4. **What is the river/island geography?** (Is there an island? Bridges? Flood zones?)
5. **Where are NPC homes?** (Each NPC needs a personalized interior reflecting personality)
6. **Where is the railway?** (Station, tracks, tunnels — connects to procedural wilderness?)
7. **What are the farm layouts?** (Player farm, NPC farms, communal fields?)

### Horror Narrative (BLOCKING for Phase 6)
8. **What is the core horror mystery?** (Higurashi structure: cyclical, hidden truth, what is the "curse"?)
9. **Who are the key horror NPCs?** (Mayor, Witch, Traveler, Doctor, Teacher — what are their secrets?)
10. **What are the horror cycles/chapters?** (How many loops? What escalates each loop?)

### Recurring Runs (BLOCKING for Phase 7+)
11. **What persists across runs?** (Knowledge, map discoveries, NPC relationships, crafted items, buildings?)
12. **What resets?** (Player stats, inventory, world state?)
13. **How does death work?** (Permadeath? Knowledge retention? Run counter?)

### Cognitive Architecture Priorities
14. **Which NPCs are "important" (full cognition) vs "background" (cheaper)?** — Need list
15. **Which aggregate minds to implement first?** (VillageMind? EconomyMind? NatureMind? CultureMind?)
16. **What is the cognitive LOD distance?** (How far from player before cognition simplifies?)

### Technical
17. **WebSocket + browser client priority?** (Needed for real MUD feel)
18. **Offline simulation approach?** (Tick advancement on restart? Compressed simulation?)

---

## Summary: What Exists vs What's Needed

| Category | Exists (✅) | Partial (⚠️) | Missing (❌) |
|---|---|---|---|
| **Core Architecture** | Event Bus, Plugin Loader, DSL, World, HTTP API | Town Consciousness (consolidation only), Cognitive Core (unwired) | WebSocket, Browser Client, Offline Sim |
| **Farming** | Crops, seasons, fertilizer, scarecrow, soil | | Animals, cooking, machines, sprinklers, bee houses, casks, skill perks |
| **Social** | NPCs, schedules, regions, heart decay (L2) | | Heart persistence, gift prefs, LLM dialogue, marriage, children, adaptive schedules |
| **Building** | DSL, farmhouse repair, placed structs | | Buyable plots, zoning, interior rooms (barn, greenhouse, cellar, shrine), seamless procgen |
| **Economy** | Market prices, shop mods, job/quest structs | | Living economy, job board, quest generator, supply/demand recalc |
| **Horror** | Sanity, night events, phantom sightings, horror_intensity | Night events (LLM), horror mechanics | Authored narrative, cyclical structure, perception filters, meta-breaks, basement |
| **Cognition** | Cognitive architecture spec, CognitiveState, TinyMLP, CognitiveCore, Registry | | MLP training, NPC integration, social cognition, collective minds, LOD |
| **Simulation** | Weather, crops, wildlife, building decay, chunks | Forest ecology (spec only), water table (spec) | Terrain changes (floods, erosion, fires, river shifts, succession), offline sim |
| **Narrative** | Quest/Job structs, horror events | | Authored horror story, procedural quest generator, cross-run knowledge |
| **Technical** | Event bus, plugins, deterministic core, LoRA training | | Causal traces, cognitive LOD, memory budgeting, WebSocket, PIXI client |

---

**Next Step**: Present this audit to user. Get answers to design questions (especially Town Design and Horror Narrative). Then propose updated roadmap with prioritized phases.