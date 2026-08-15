# Ashgrove Valley – Vision & Roadmap (Deterministic + AI‑Enhanced)

**Purpose**: Provide a long‑term, high‑level design reference for future contributors and LLM‑agents. It captures the dual‑system architecture, feature ambitions, and phased roadmap while enforcing the project's existing documentation and quality standards.

---

## 1. Core Vision

### 1.1 Dual‑System Architecture
The game is built on two complementary pillars:

1. **Deterministic / RNG Core** – The authoritative simulation layer. All world state (tiles, NPC schedules, crop cycles, weather, economy, physics) is fully deterministic, reproducible, and parallel‑friendly. No cognitive or AI components reside here; only rule‑based systems driven by seeded randomness.

2. **Cognitive AI/ML Layer (llama.cpp)** – A local LLM runs on‑server to provide:
   - Natural‑language command parsing and intent extraction (NLP interface).
   - Context‑aware NPC dialogue, personality, and decision‑making.
   - Narrative event generation (quests, horror encounters, moral dilemmas).
   - Continuous world‑learning: the model ingests aggregated player‑world logs and updates its internal policy via periodic fine‑tuning.

**Boundary Rule**: All AI outputs are converted to deterministic actions before being fed back to the core. The core never mutates state based on nondeterministic decisions without a deterministic checkpoint (seed derived from world state).

### 1.2 Text‑First, Imagination‑Driven Interaction
- **Primary interface**: Prose‑style text commands (e.g., `"I carefully examine the ancient statue while the rain patters"`). Output reads like novel prose, prompting the player's imagination.
- **Minimap**: A secondary visual reference only (PIXI.js client). It never replaces textual description.
- **Command NLP**: Raw player text → LLM → structured intent → deterministic executor. This keeps the command surface simple while allowing deep expressivity.

### 1.3 Living, Thinking, Learning World
- **Alive without the player**: NPCs follow schedules, economy fluctuates, weather evolves, crops grow, quests generate, and horror events trigger even when no player is online.
- **Adaptive systems**: NPCs learn player habits; weather reacts to irrigation/deforestation; economy recalibrates to aggregate production; the LLM fine‑tunes on world logs each cycle.
- **Real‑world mimicry**: Day/night, seasons, moon phases, tides, disease, aging, decay, structural wear, social dynamics — all modeled with enough fidelity to feel authentic.

### 1.4 Narrative & Horror Inspiration
The world's tone draws from:
- **Stardew Valley** – Cozy farming loop, relationships, festivals, progression.
- **FROM (TV series)** – Uncanny town, night‑time dread, inescapable mystery.
- **Higurashi** – Cyclical horror, hidden truths, sanity erosion.
- **Doki Doki Literature Club** – Meta‑narrative breaks, character self‑awareness, psychological horror.
- **Disco Elysium** – Rich internal voices, skill‑based dialogue, political/philosophical depth.

These influences manifest as: night‑only encounters, sanity mechanics, branching story threads, moral choice consequences, and NPCs that occasionally "break character" or remember across cycles.

### 1.5 Human‑Like Core NPCs
A handful of central NPCs (e.g., the Mayor, the Witch, the Traveler) are driven by the LLM with persistent memory, evolving personalities, and the capacity for genuine surprise. They are not scripted quest‑givers but simulated minds with goals, fears, and secrets.

### 1.6 Emergence as a First‑Class Goal
All subsystems publish to a central **Event Bus**. Listeners (deterministic or AI) react, enabling complex cascades the developers never explicitly coded. The game should *become something it was not originally coded for*.

### 1.7 Extensible Plugin Architecture
New gameplay modules (horror overlay, new crafting stations, custom quest lines) are added as shared libraries (`plugins/`) that register Event Bus listeners at startup. No core code changes required.

---

## 2. Architecture Overview

```
Ashgrove Server
├─ Deterministic Core (C++20)
│   ├─ World Generation & Persistence
│   ├─ NPC Scheduler & Pathfinding
│   ├─ Crop / Weather / Economy Systems
│   └─ Event Bus (topics: tick, player_cmd, npc_action, world_change, quest_generated, horror_event, economy_shift, sanity_change)
├─ AI/ML Layer (llama.cpp, GGUF, local only)
│   ├─ Command‑NLU Processor
│   ├─ NPC Dialogue & Personality Engine
│   ├─ Quest / Horror Narrative Generator
│   └─ Learning Pipeline (log → dataset → fine‑tune → hot‑swap)
└─ Interface Layer
    ├─ HTTP/JSON API (existing)
    ├─ WebSocket Push (planned)
    └─ Text MUD Engine (frontend client)
```

- **Local LLM only** – `llama.cpp` integrated as static library or subprocess. No external API keys.
- **Plugin System** – `extern "C" void register_plugin(EventBus&)` in `plugins/*.so`.

---

## 3. Phased Roadmap

| Phase | Goal | Key Deliverables | Approx. Effort |
|------|------|------------------|----------------|
| **0 – Foundations** | Scaffold AI integration, event bus, plugin API, documentation pipeline. | • Thread‑safe EventBus.<br>• `llama.cpp` runtime wrapper (subprocess + JSON RPC).<br>• `CommandParser` calling LLM → structured intent.<br>• Plugin loader (`dlopen`) with registration hooks.<br>• CI: `clang‑tidy`, `cppcheck`, warning‑free build gate. | 2‑3 weeks |
| **1 – Core Stardew Features** | Match vanilla Stardew farming loop. | • Animals (chickens, cows, goats) with daily products.<br>• Fishing system (rod, bait, locations, minigame).<br>• Cooking & recipes (ingredients → dishes).<br>• Seasonal festivals (Spring Fair, Summer Luau, Autumn Harvest, Winter Festival).<br>• Expanded crop catalogue (100+ varieties, multi‑season, trellis, giant crops). | 4‑5 weeks |
| **2 – Advanced Crafting & Machines** | Processing & automation depth. | • Kegs, Preserves Jars, Mayonnaise Machine.<br>• Bee houses & honey production.<br>• Casks for cellar aging (wine, cheese).<br>• Sprinkler pressure, greenhouse, quality sprinklers.<br>• Skill perks (Tiller, Agriculturist). | 3‑4 weeks |
| **3 – Social & NPC Relationships** | Rich, LLM‑enhanced social layer. | • Heart‑system persistence (0‑14 hearts).<br>• Gift‑preference tables (love/like/neutral/dislike/hate).<br>• LLM‑driven dialogue with fallback scripts.<br>• Marriage, divorce, children, roommate events.<br>• NPC schedules adapt to player friendship. | 3‑4 weeks |
| **4 – Town & Map Expansion** | Procedural outskirts + authored core. | • Procgen region generator (forest, hills, caves, ruins).<br>• Buyable plots, construction via text DSL.<br>• New interior rooms (barn, greenhouse, cellar, shrine).<br>• Seamless transition from 128×96 authored map to infinite procgen. | 3‑4 weeks |
| **5 – Quest & Job System** | Dynamic, context‑aware content. | • Quest generator sampling templates + world state (season, NPC mood, weather, economy).<br>• Procgen side quests (fetch, kill, deliver, investigate, ritual).<br>• Job board NPCs offering repeatable work (farmhand, miner, courier, researcher).<br>• Living economy: supply/demand, price fluctuation, market crashes/booms.<br>• Event‑driven reward scaling. | 4‑5 weeks |
| **6 – Horror & Narrative Overlays** | Night‑time dread, sanity, meta‑horror. | • Under‑map "basement" accessible only after midnight.<br>• Random night events via LLM narrative scripts (chapter‑style).<br>• Sanity meter → perception filters (hallucinations, distorted dialogue, false UI).<br>• Higurashi‑style cyclical secrets, DDLC‑style fourth‑wall breaks, Disco Elysium internal voices.<br>• PIXI client: fog, shadows, audio cues, glitch effects. | 4‑6 weeks |
| **7 – Emergent World Learning** | Long‑term adaptation. | • Log collector → fine‑tuning dataset for `llama.cpp`.<br>• Adaptive NPC schedules (learn player routines).<br>• Weather patterns react to player‑driven irrigation/deforestation.<br>• Economy recalibrates to aggregate production/consumption.<br>• Horror intensity adapts to player sanity history. | Ongoing (iteration after each major release) |
| **8 – Model Distillation** | Kill the latency bottleneck with a game‑specialist student. | • Log collector (Phase A): every `/cmd` appended to `data/cmdlog.jsonl` — `{raw, intent, slots, world_hash, response, latency_ms}`.<br>• Teacher dataset (Phase B): offline script `tools/gen_dataset.py` runs NVIDIA NIM `nemotron-3.5-lightning-30b-a3b` to generate paraphrase→`{intent, slots}` pairs + prose samples in the game's voice.<br>• Student training (Phase C): CPU LoRA on Qwen2.5‑0.5B (fp32 base, ~1.5h on 4 cores), merge + `llama‑quantize` to Q4_K_M (~400 MB).<br>• Tiered runtime (Phase D): Tier 0 exact/grammar fast path (0 ms — `look`, `inventory`, `status`, `help` never touch a model) → Tier 1 student + GBNF grammar (`{intent, slots}` JSON, <1 s, big‑model fallback on low confidence) → Tier 2 async prose generation (night events, dialogue pre‑generated at day boundaries and cached).<br>• Iteration (Phase E): gameplay logs feed periodic retrains; eval harness tracks intent‑accuracy + p50/p95 latency. | 2‑3 weeks |
| **9 – Documentation & Tooling** | Future‑agent readiness. | • Update `docs/vision-and-roadmap.md` after each phase.<br>• Enforce `CHANGELOG.md`, `README.md` conventions.<br>• Add test‑coverage reports to docs.<br>• Create `docs/api-event-bus.md`, `docs/plugin-dev.md`, `docs/nlp-commands.md`.<br>• Generate Doxygen API reference in CI. | Continuous |

**Milestone Naming**: `Rxx‑Y‑Name` (e.g., `R17‑0‑Foundations`). Commits use conventional messages (`feat:`, `fix:`, `docs:`, `refactor:`) referencing the roadmap step.

---

## 4. Implementation Guidelines

- **Parallelism** – All new subsystems use the existing thread‑pool (`std::execution::par`). No global locks; fine‑grained mutexes or lock‑free queues only.
- **Determinism** – Randomness seeded from `world.seed`. AI decisions affecting state must be reproducible: store LLM token output as data for replay.
- **Testing** – Unit tests per feature + integration tests (command → intent → core mutation). Use existing `cpp-test` harness.
- **Documentation** – Every public class/function gets Doxygen‑style comment explaining **why** (design rationale). Design docs updated in same PR.
- **No New Bugs Policy** – Static analysis / compile warnings fixed immediately. Runtime bugs → regression test before proceeding. CI gate: `-Werror` on project code.
- **Commit Discipline** – One logical change per commit. Full build + tests locally before push. Commit message references roadmap step.
- **Agent Rules** – See Section 7 below.
- **Simplify UI & Commands** – Minimize keystrokes; NLP handles synonyms, fuzzy matching, and context. The minimap remains a passive reference.
- **Emergence Over Hard‑Coding** – Prefer event‑driven interactions to scripted sequential flows. Systems interact via Event Bus; avoid direct cross‑module calls.

---

## 5. Event‑Bus Interaction Model

| Topic | Producer | Consumer(s) |
|-------|----------|------------|
| `tick` | Game loop | All deterministic systems, AI scheduler |
| `player_cmd` | HTTP `/cmd` handler | LLM command parser → deterministic core |
| `npc_action` | NPC AI | World state (movement, dialogue) |
| `world_change` | Any system mutating state | AI learning pipeline, economy recalculator |
| `quest_generated` | Quest engine | NPC dialogue, player notification |
| `horror_event` | Horror overlay | NPC AI (panic), World (weather), UI (effects) |
| `economy_shift` | Economy simulator | Shop prices, NPC job availability, quest rewards |
| `sanity_change` | Horror system | Perception filters, NPC behavior, LLM prompt injection |

Listeners subscribe via `EventBus::subscribe<Topic>(callback)`. Decouples systems and enables emergent cascades.

---

## 6. Future Extensibility (Plugin Architecture)

- Plugins compiled as shared objects in `plugins/`.
- Each implements `extern "C" void register_plugin(EventBus& bus)`.
- Plugin manager loads all `.so` at startup, invoking registration.
- Plugins can add commands, event topics, or modify existing ones. Satisfies "the game should be able to add new features" without core changes.

---

## 7. Rules & Operating Procedures (Recap)

1. **Ask before coding** – Any `[Q]` step triggers a clarification question to the user.
2. **Fix bugs immediately** – No broken commits; regressions prohibited. Add regression test first.
3. **Document relentlessly** – Every change updates `CHANGELOG.md`, this roadmap, and relevant API docs.
4. **Use local LLM only** – `llama.cpp` is the sole AI engine; no external services.
5. **Maintain parallelism** – New code respects the thread‑pool architecture.
6. **Emergence over hard‑coding** – Event‑driven interactions preferred.
7. **Agent‑initiated suggestions** – The agent is encouraged to propose improvements, refactorings, or new features based on its own analysis of the codebase and roadmap.
8. **Modular, organized code** – Keep the codebase modular, follow existing patterns, and maintain clear separation of concerns to facilitate development, maintenance, and collaboration.
9. **Simplify for the player** – Text commands are the UI; NLP handles complexity; minimap is reference only.
10. **Always ask before acting** – Before any non‑trivial coding change, the agent must use the `question` tool to ask the user for clarification or confirmation. This instruction must also be included in the documentation so future agents know to ask.
11. **Commit and push regularly** – Every logical change must be committed with a conventional commit message and pushed to `origin/main` before moving to the next task. No local-only commits. Push after each atomic step.
12. **Extensive skill usage** – Agents MUST leverage available OpenCode skills whenever they are applicable (CMake, C++ coding standards, testing, documentation, MCP, WebSockets, etc.), and may download additional skills if required to accomplish a task. Skill usage must be documented in the PR/commit.
13. **Verify before push** – Run the project's lint/typecheck/test commands (e.g., `cmake --build build && ctest`) before every push. If no commands are documented, ask the user for the verification command and add it to `AGENTS.md`.

---

*Prepared by the OpenCode agent on 2026‑08‑15. This document supersedes prior ad‑hoc notes and serves as the canonical source for all future development.*