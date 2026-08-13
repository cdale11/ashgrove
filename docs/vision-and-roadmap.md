# Ashgrove Valley – Vision & Roadmap (Deterministic + AI‑Enhanced)

**Purpose**: Provide a long‑term, high‑level design reference for future contributors and LLM‑agents. It captures the dual‑system architecture, feature ambitions, and phased roadmap while enforcing the project’s existing documentation and quality standards.

---

## 1. Core Vision

1. **Deterministic Simulation Core** – The authoritative game state (world tiles, NPC schedules, crop cycles, weather, economy) remains fully deterministic, reproducible, and parallel‑friendly. All gameplay that does not require cognition lives here.
2. **AI/ML Layer (llama.cpp)** – A local LLM runs on‑server to provide:
   - Natural‑language command parsing and intent extraction.
   - Context‑aware NPC dialogue and personality generation.
   - Narrative event generation (quests, horror encounters, moral dilemmas).
   - Continuous world‑learning: the model ingests aggregated player‑world logs and updates its internal policy.
3. **Text‑First Interaction** – Players issue prose‑style commands (e.g., `"I carefully examine the ancient statue while the rain patters"`). The command interpreter forwards the raw text to the LLM, which returns a structured intent for the deterministic core.
4. **Emergent World** – All subsystems (economy, NPC relationships, weather, quests) publish events to a central **Event Bus**. Listeners (deterministic or AI) react, enabling complex, emergent behavior without hard‑coded chains.
5. **Procedural Expansion** – A richly authored 128×96 core map is surrounded by procedurally‑generated terrain, allowing an effectively infinite world while keeping the hand‑crafted experience at the heart.
6. **Horror & Narrative Depth** – Inspired by *From TV*, *Higurashi*, *DDLC*, and *Disco Elysium*, we will layer night‑time atmospherics, sanity mechanics, branching story threads, and moral choice consequences.

---

## 2. Architecture Overview

```
Ashgrove Server
├─ Deterministic Core (C++20)
│   ├─ World Generation & Persistence
│   ├─ NPC Scheduler & Pathfinding
│   ├─ Crop / Weather / Economy Systems
│   └─ Event Bus (topics: tick, player_cmd, npc_action, world_change)
├─ AI/ML Layer (llama.cpp)
│   ├─ Command‑NLU Processor
│   ├─ NPC Dialogue Generator
│   ├─ Quest / Horror Narrative Engine
│   └─ Learning Pipeline (log → fine‑tune)
└─ Interface Layer
    ├─ HTTP/JSON API (existing)
    ├─ WebSocket Push (planned)
    └─ Text MUD Engine (frontend client)
```

- **Deterministic ↔ AI Boundary**: All AI outputs are converted to deterministic actions before being fed back to the core. The core never mutates state based on nondeterministic decisions without a deterministic checkpoint (e.g., random seed derived from world state).
- **Local LLM**: The project will integrate `llama.cpp` (GGUF format) as a static library or subprocess. No external API keys are required, satisfying the “llama.cpp exclusive” requirement.
- **Plugin System**: New gameplay modules (e.g., a horror overlay) can be added as shared libraries that register Event Bus listeners. This supports “the game should be able to add new features” without core code changes.

---

## 3. Phased Roadmap

| Phase | Goal | Key Deliverables | Approx. Effort |
|------|------|------------------|----------------|
| **0 – Foundations** | Scaffold AI integration, event bus, plugin API, documentation pipeline. | • EventBus implementation (thread‑safe).<br>• `llama.cpp` runtime wrapper.<br>• `CommandParser` that calls LLM and returns intent.<br>• Plugin loader (dlopen) with registration hooks. | 2‑3 weeks |
| **1 – Core Stardew Features** (all‑of‑the‑above) | Expand farming loop to match vanilla Stardew experience. | • Animals (chickens, cows, goats).<br>• Fishing system with rod, bait, locations.<br>• Cooking & recipes.<br>• Seasonal festivals (Spring Fair, Summer Luau, etc.).<br>• Expanded crop catalogue (100+ varieties). | 4‑5 weeks |
| **2 – Advanced Crafting & Machines** | Introduce processing and automation. | • Kegs, Preserves Jars, Mayonnaise Machine.<br>• Bee houses & honey production.<br>• Casks for cellar aging.<br>• Sprinkler pressure adjustments, greenhouse. | 3‑4 weeks |
| **3 – Social & NPC Relationships** | Rich NPC personalities, gifts, events, marriage. | • Heart‑system persistence.<br>• Gift‑preference tables.<br>• LLM‑driven dialogue trees (fallback to scripted).<br>• Marriage & wedding events. | 3‑4 weeks |
| **4 – Town & Map Expansion** | Procedural outskirts, buyable plots, construction tools. | • Procgen region generator (forest, hills, caves).<br>• Plot purchase UI (text commands).
• Building placement & interior design via DSL.<br>• New interior rooms (e.g., barn, greenhouse). | 3‑4 weeks |
| **5 – Quest & Job System** | Dynamic, context‑aware quests; player jobs; living economy. | • Quest generator that draws from world state (season, NPC mood, weather).<br>• Job board NPCs offering repeatable tasks.<br>• Economy model: supply/demand, price fluctuation based on world events.<br>• Event‑driven reward scaling. | 4‑5 weeks |
| **6 – Horror & Narrative Overlays** | Night‑time horror encounters, sanity, moral choice. | • Under‑map “basement” area accessible only after midnight.<br>• Random night events governed by LLM narrative scripts (chapter‑style).<br>• Sanity meter influencing perception of NPC dialogue.<br>• Audio/visual cues in PIXI client (fog, shadows). | 4‑6 weeks |
| **7 – Emergent World Learning** | World adapts over long‑term playthroughs. | • Log collector that feeds a fine‑tuning dataset for `llama.cpp`.
• Adaptive NPC schedules (e.g., NPCs learn player habits).
• Weather patterns reacting to player‑driven irrigation/deforestation.
• Economy recalibrates based on aggregate production/consumption. | Ongoing (iteration after each major release) |
| **8 – Documentation & Tooling** | Ensure future LLM‑agents can understand everything. | • Update `docs/vision-and-roadmap.md` after each phase.
• Enforce `CHANGELOG.md`, `README.md` conventions.
• Add test‑coverage reports to docs.
• Create `docs/api-event-bus.md` reference. | Continuous |

**Milestone Naming** follows the existing `Rxx` style (e.g., `R17‑0‑Foundations`). Each commit will use conventional messages (`feat:`, `fix:`, `docs:`) and update the roadmap accordingly.

---

## 4. Implementation Guidelines

- **Parallelism** – All new subsystems must use the existing thread‑pool model (`std::execution::par`). Avoid global locks; prefer fine‑grained mutexes or lock‑free queues.
- **Determinism** – Randomness is seeded from the world hash (`world.seed`). AI decisions that affect state must be reproducible: store the LLM’s token output and treat it as data for later re‑play.
- **Testing** – Unit tests for each new feature, plus integration tests covering command → intent → core mutation. Use the existing test harness (`cpp-test`).
- **Documentation** – Every public class/function gets a Doxygen‑style comment explaining **why** (design rationale). Update the design docs as part of the same PR.
- **No New Bugs Policy** – If a static analysis or compile warning appears, fix immediately. If a runtime bug is discovered while prototyping, add a regression test before proceeding.
- **Commit Discipline** – One logical change per commit. Run the full build and tests locally before push. The commit message must reference the roadmap step (e.g., `feat(R17-1): add llama.cpp command parser`).

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

Listeners subscribe via a simple interface (`EventBus::subscribe<Topic>(callback)`). This decouples systems and enables emergent cascades.

---

## 6. Future Extensibility (Plugin Architecture)

- Plugins are compiled as shared objects placed in `plugins/`.
- Each plugin implements `extern "C" void register_plugin(EventBus& bus)`.
- At server start, the plugin manager loads all `.so` files, invoking the registration function.
- Plugins can add new commands, new event topics, or modify existing ones. This satisfies the requirement that *the game should be able to add new features* without core code changes.

---

## 7. Rules & Operating Procedures (Recap)

1. **Ask before coding** – Any `[Q]` step triggers a clarification question.
2. **Fix bugs immediately** – No broken commits; regressions are prohibited.
3. **Document relentlessly** – Every change updates `CHANGELOG.md`, this roadmap, and relevant API docs.
4. **Use local LLM only** – `llama.cpp` will be the sole AI engine; no external services.
5. **Maintain parallelism** – New code must respect the thread‑pool architecture.
6. **Emergence over hard‑coding** – Prefer event‑driven interactions to scripted sequential flows.

---

*Prepared by the OpenCode agent on 2026‑08‑14. This document supersedes prior ad‑hoc notes and serves as the canonical source for all future development.*
