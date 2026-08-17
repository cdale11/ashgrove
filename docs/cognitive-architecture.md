# Cognitive Architecture — Ashgrove Valley AI Design

> **Status**: Phases 7.7–7.9 **implemented & wired** (CognitiveCore, SocialCognition, all four
> aggregates). **Remaining: LLM dialogue model training (deferred)** (see `docs/ROADMAP.md` Tier 1.7). Phase 8–10 integration pending.
> (see `docs/ROADMAP.md` Tier 1.7). Phase 8–10 integration pending.
> **Principle**: The local LLM (≤2B parameters) is a **language interface and reasoning engine**, not the brain of any agent or system. All persistent cognition lives in lightweight learned networks + structured adaptive state, with fixed weights and bounded online updates.
> **Author**: 2026-08-16 (post-training pipeline + docs survey session)
> **Cross-ref**: `docs/ROADMAP.md` (open work), `docs/shipped-features.md` §26 (shipped state)

---

## 1. Design Philosophy: Why Not LLM-Brain?

The existing `TownConsciousness` (Phase 7) uses a fine-tuned Qwen2.5-0.5B to emit `adaptations.json` once per in-game day. That is a **bias generator**, not cognition. A true cognitive architecture requires:

- **Persistence**: Memory and learned preferences survive between LLM calls and between days.
- **Real-time responsiveness**: A garden pest outbreak or NPC betrayal cannot wait for a 260-second LLM inference.
- **Emergence**: Consciousness-like behavior should arise from interaction between simpler systems, not from a prompt.
- **Efficiency**: 260 s/step training + ~1 s/LLM-call budget; cognition must run in sub-millisecond per tick.
- **Individualization**: Each NPC must diverge through experience; copying the same LLM output produces clones.

The architecture is **biologically inspired** (not biomimetic to the point of inefficiency): perception → working memory → emotional/reward modulation → predictive modeling → planning → action. It borrows from neuroscience (attention gating, episodic replay, drive reduction, social cognition) and machine learning (lightweight MLPs, reservoir computing, associative memory) but avoids large generative models for cognition.

---

## 2. Two-Tier Architecture (Confirmed)

| Tier | Role | Size | Frequency | What it does |
|------|------|------|-----------|--------------|
| **LLM (Tier 2)** | Natural-language generator / interpreter / complex reasoner | ≤ 2B params | On demand (user `/cmd`, NPC dialogue, consolidation prompt, narrative event) | Converts structured intent to prose; interprets ambiguous commands; generates consolidation prompts; produces narrative beats; explains reasoning (`/town/why`). Never makes tick-level decisions. |
| **Cognitive Core (Tier 1)** | Persistent adaptive cognition | Lightweight MLPs + persistent state vectors (KB per agent, not GB) | Every tick (sub-ms) | Perception, attention, working memory, episodic/semantic storage, emotion/drives, prediction, planning, action selection, world-model updates, social cognition. Runs deterministically on CPU. |

The LLM is the **vocabulary and syntax engine** for complex conceptual work; the Cognitive Core is the **thinking system** that operates continuously.

---

## 3. Cognitive Components (Per Agent / Per System)

Every important agent (important NPCs, the village aggregate, the forest aggregate, the economy aggregate) holds a persistent **CognitiveState**. The architecture has 11 core modules:

### 3.1 Perception / Attention
- **Input**: Sensor arrays — visual (line-of-sight ray checks against `World::at()`), auditory (distance-attenuated event sounds), olfactory/proprioceptive (state variables from `Player`/`World`).
- **Attention mechanism**: Gating MLP (small, ~32 hidden units, fixed weights + bounded updates) that scores stimuli by **salience = (novelty × emotional_arousal × relevance_to_goals)**. Only top-K stimuli enter working memory.
- **Implementation**: `src/attention.cpp`. Uses deterministic rules (visibility checks) + learned attention weights stored in agent's `attention_weights` (4 floats: novelty, reward, social, survival).

### 3.2 Working Memory
- **Capacity**: Bounded buffer (4–7 items, inspired by Miller's law but parameterized). Each item = `{stimulus_ref, emotional_tag, timestamp, decay_factor}`.
- **Decay**: Exponential decay by tick (`decay_factor *= 0.995` per tick). Replay events (`episodic_memory.replay()`) refresh working memory.
- **Implementation**: `src/working_memory.h` — fixed-size ring buffer per agent; no LLM involvement.

### 3.3 Episodic Memory
- **Storage**: Structured event records (same schema as `TownEvent` from Phase 7): `{system, event_type, payload, emotional_tag, tick}`. Stored per-agent, not global.
- **Retrieval**: Associative lookup by `{context_tag}` (current season, location, emotional state) using a small associative memory matrix (`associative_memory` weights: 16×16, fixed + bounded updates).
- **Replay**: Subconscious replay runs during sleep (`tick_sanity` / rest events) — reinforces emotional tags, strengthens predictions, updates world-model biases.
- **Implementation**: `src/episodic_memory.cpp`. Uses `std::map<uint32_t tick, EventRecord>`. Replay selects events with high emotional_tag / high context-match.

### 3.4 Semantic Memory
- **Storage**: Long-term facts derived from repeated experience (e.g., "the smith always opens at 9", "rain reduces fishing yield", "gift of wine increases heart with merchant").
- **Structure**: `{fact_id: {subject, predicate, object, confidence, last_updated, source_agent_ids}}`. Confidence decays slowly (`0.998` per tick) unless reinforced by new evidence or replay.
- **Social transmission**: Agents can share semantic facts (`social_communication`) with confidence-weighted averaging (`new_conf = 0.7*old_conf + 0.3*source_conf`).
- **Implementation**: `src/semantic_memory.cpp`. Small JSON-like internal store, serialized to agent's save data.

### 3.5 Emotion / Reward System
- **Emotion model**: Continuous 2-D (valence × arousal) per agent, plus discrete emotional tags (`joy`, `fear`, `trust`, `anger`, `disgust`, `surprise`, `anticipation`). Tags derived from event payload (`horror_intensity`, `gift_accepted`, `storm_damage`, etc.).
- **Reward function**: Not a single scalar; per-drive scalar (`hunger`, `thirst`, `social`, `safety`, `curiosity`, `reproduction`). Each drive has its own `satisfaction` curve. The **reward prediction error** (`RPE = actual_reward - predicted_reward`) updates drives and updates the world-model.
- **Bounded adaptation**: Drive weights (`drive_weights` array of 6 floats) update by: `new = 0.9*old + 0.1*RPE_gradient`. Very slow convergence (intentionally) to preserve stability.
- **Implementation**: `src/emotion_drive.cpp`. Fixed weight MLP predicts emotional response from event features; only the 6 drive floats adapt online.

### 3.6 Prediction / World-Model
- **World-model**: Per-agent predictive model — not a full physics simulator, but a compact predictive MLP (`world_model` weights: 8 inputs → 4 hidden → 3 outputs: `predicted_weather_shift`, `predicted_social_response`, `predicted_resource_availability`). Inputs = current season, recent events from episodic memory, semantic memory facts, emotional state.
- **Fixed weights** (trained offline with synthetic data; see §6). Online adaptation: only a small `world_model_bias` vector (3 floats) updates per tick based on prediction errors (`new_bias += 0.01 * error`).
- **Implementation**: `src/world_model.cpp`. Uses `tiny_mlp.h` (hand-written 8×4 MLP, no external library dependency).

### 3.7 Learned Preferences
- **Preference vector**: Per-agent float array (e.g., `crop_preference`, `gift_preference_shift`, `location_preference`, `social_target_preference`). Updated by positive emotional tags (`new += 0.02 * positive_tag`) and decayed (`new *= 0.999` per tick).
- **Bounded**: Each preference clamped to `[0, 1]`. The model outputs preference-shift predictions; the agent's actual preferences adapt independently.
- **Implementation**: `src/preferences.h` — simple float array with decay/update logic.

### 3.8 Social Cognition / Theory-of-Mind
- **Social graph**: Per-agent `social_graph` — nodes = known agents; edges = `{trust, familiarity, emotional_history_sum, imitation_target}`. Persistent across saves.
- **Theory-of-mind**: Not a full mental simulation; a simple inference: `predicted_social_action = f(edge.trust, edge.familiarity, agent_A.self_state, agent_A.world_model)`. This predicts whether another agent will help, compete, or ignore, enabling cooperative planning.
- **Social learning**: Agents observe successful actions of others (`imitation_target` edges). When `edge.imitation_target > 0.5` and the observed action yields positive emotional tag, the observer copies the behavior preference. This creates **cultural transmission** (e.g., if one NPC learns to repair buildings quickly, others near them learn the repair behavior).
- **Bounded adaptation**: Edge weights (`social_edge_weights`) adapt by `new = 0.95*old + 0.05*interaction_result`. Very slow.
- **Implementation**: `src/social_cognition.cpp`. Graph stored as `std::map<std::string agent_id, SocialEdge>`. Updates in `NPC::tick_social()` called each tick.

### 3.9 Self-Model
- **Self-representation**: Per-agent vector `{self_esteem_estimate, competence_estimate, autonomy_estimate}`. Updated by comparing actual outcomes to predictions (`world_model`). If predictions are consistently wrong (high error), `self_esteem_estimate` drops slowly; if predictions are accurate, it rises.
- **Effect**: Low self-esteem → higher `safety` drive weight, lower `curiosity` weight, reduced social initiative. High self-esteem → more exploration, more social risk-taking. Creates personality divergence.
- **Bounded adaptation**: Self-state updates by `new += 0.005 * normalized_prediction_accuracy` (extremely slow, ~days of experience).
- **Implementation**: `src/self_model.cpp`. Small vector update in the agent tick loop.

### 3.10 Subconscious Processing
- **Subconscious replay buffer**: Runs during sleep/rest (`tick_sanity` or `sleep` event). Selects 3–5 episodic events with high emotional_tag and replays them: updates episodic confidence, reinforces predictions, updates social edges (if another agent involved), updates self-model.
- **Dream / nightmare synthesis**: A simple generative mechanism: combine replayed events with noise → generate `last_night_event` (already exists as `World::roll_night_event`). The cognitive architecture enhances this: replay events influence which events are selected, and emotional tags bias event generation.
- **Implementation**: `src/subconscious.cpp`. Runs in `World::tick_sanity()` or during `sleep` events. Uses replay logic + deterministic noise.

### 3.11 Planning & Action Selection
- **Planning**: Not a full planner; a **goal-stack** + **action-evaluator**.
  - `goal_stack`: Per-agent ordered list of `DriveGoal` objects (`{drive_type, urgency, target_location, deadline_tick}`). Updated by emotional/reward state.
  - `action_evaluator`: Small MLP (`action_evaluator` weights: 10 inputs → 3 hidden → action_score). Inputs: current working memory top items, emotional state, drive urgency, prediction outputs. Outputs: score for each available action (`go`, `interact`, `talk`, `repair`, `harvest`, etc.).
- **Bounded adaptation**: Only the `action_evaluator` weights update, very slowly (`learning_rate = 0.002`), and only if action results in positive emotional tag. This creates **behavioral drift** without catastrophic forgetting.
- **Implementation**: `src/planning.cpp`. Action scores computed per tick; top-scored action selected (or combined with NPC schedule constraints from the existing `schedule_slot` system).

---

## 4. Aggregate / Collective Cognition (Not Just Humans)

The architecture treats persistent game systems as **potential cognitive aggregates** — not individual brains, but collective systems that accumulate state, receive feedback, and develop emergent behavior.

### 4.1 Village Aggregate (`VillageMind`)
- **State**: Aggregate of all NPC emotional states (`mean_valence`, `mean_arousal`), economic trends (`market_volatility`, `demand_shift`), and social graph density (`average_edge_trust`).
- **Memory**: `village_memory` — aggregate episodic records (major events: festivals, disasters, player major actions, economic crashes). Shared with `TownConsciousness` but interpreted as collective memory.
- **Behavior**: No direct action selection, but biases NPC schedules (`schedule_bias`), economic prices (`market_volatility`), and horror intensity (`horror_night_event_weight`) through the adaptation loop.
- **Social transmission**: Cultural practices spread through `social_edge_weights` — a repair technique learned by one NPC propagates to others through observation (`imitation_target`).
- **Implementation**: `src/village_aggregate.cpp`. Updates `World::village_memory` (new field) and pushes aggregate biases to `adaptations.json` sections (`npc`, `economy`, `horror`).

### 4.2 Economy Aggregate (`EconomyMind`)
- **State**: Not just `market_prices`; a persistent `economic_memory` tracking `commodity_cycles`, `trade_route_health`, `inflation_rate`, `player_impact_on_supply`.
- **Memory**: Episodic records of economic events (`market_crash_day = 14`, `player_hoards_wood`). Semantic memory of `commodity_demand_patterns` (seasonal demand curves learned from `update_market_prices` history).
- **Behavior**: Economic adaptations (`price_elasticity`, `market_volatility`, `demand_shift`) are not arbitrary LLM outputs — they are computed from aggregate economic memory: `volatility += |price_change_rate - historical_mean|`, `demand_shift` reflects observed player buying patterns.
- **Implementation**: `src/economy_aggregate.cpp`. Updates `World::economic_memory` (new field) and feeds back to `update_market_prices()` and adaptation consumers.

### 4.3 Culture Aggregate (`CultureMind`)
- **State**: Aggregate of `semantic_memory` across NPCs — common beliefs, shared rituals, collective fears (horror), collective preferences (food, gifts, festivals).
- **Memory**: `culture_memory` — `ritual_practices` (frequency of repair, gift-giving, festival attendance), `shared_fears` (frequency of horror events affecting NPC emotional tags), `collective_preferences` (aggregate gift preferences).
- **Behavior**: Cultural transmission through `social_edge_weights`. If a practice (e.g., "repair buildings on rainy days") achieves positive emotional tags for multiple agents, it propagates through the social graph. `schedule_bias` in adaptations reflects aggregate behavior patterns.
- **Implementation**: `src/culture_aggregate.cpp`. Updates `World::culture_memory`; feeds `schedule_bias` and `dialogue_topic_weight` adaptations.

### 4.4 Nature / Forest Aggregate (`NatureMind`)
- **State**: Aggregate of `ecological_state` from Phase 8.1 (forest ecology). `forest_memory` — `disturbance_legacy`, `succession_stage`, `climate_trend`.
- **Memory**: Episodic: `fire_events`, `storm_damage`, `harvest_patterns`. Semantic: `soil_health_trend`, `biodiversity_index`, `carbon_storage`.
- **Behavior**: The forest does not "act" in the NPC sense, but its aggregate state influences adaptations (`procgen_biases`, `storm_chance`, `disaster_chance`). Over long time scales (10+ in-game days), persistent feedback loops create emergent ecological behavior: droughts reduce biodiversity, which reduces pollination, which reduces crop yields, which increases player stress, which increases horror events. This is **not scripted** — it emerges from the interaction of deterministic systems (CA for fire, agent-based for pests, Petri nets for soil) biased by the adaptation loop.
- **Implementation**: `src/nature_aggregate.cpp`. Updates `World::nature_memory`; feeds `procgen` and `weather` adaptations.

### 4.5 Performance Aggregate (`PerformanceMind` — already exists)
- **State**: `performance_profile` (CPU %, memory, tick latency) from Phase 7.4.
- **Behavior**: `tick_budget_ms`, `npc_decision_interval_ticks`, `chunk_load_radius` adjustments are not just "tune for FPS" — they represent the aggregate's adaptive response to resource constraints. Slow tick budget = more deliberate, slower NPCs; faster budget = more reactive, more frequent observations.
- **Implementation**: Already partially implemented (`PerformanceMonitor`). Extended: add `World::performance_memory` tracking historical performance, feeding `performance_adapt` in `adaptations.json`.

---

## 5. Implementation Order (Practical, Given Training Constraints)

The user explicitly said: "Keep neural weights mostly fixed because training is expensive" and "allow small bounded online adaptation of selected parameters/state." The architecture is designed so that **most of it requires zero training** — only the small MLP predictors (`action_evaluator`, `attention`, `world_model`) need synthetic data, and they are small enough to train quickly.

### Phase 7.7 — Cognitive Core Foundation (Can Start After `47f1898` + Docs Refresh)
1. **Fixed-weight MLP scaffolds** (`tiny_mlp.h`): 8-3-1 architecture (~31 weights per MLP), hand-coded forward pass, no external dependencies. Train one small synthetic dataset (generated by `NIM` or `tools/gen_dataset.py`) for `attention`, `action_evaluator`, `world_model` predictions.
2. **Persistent state structures** (`CognitiveState`, `EpisodicMemory`, `SocialGraph`, `DriveState`) — C++ structs serialized to agent save data.
3. **Perception + Attention** (`attention.cpp`): deterministic visibility + learned attention gating (small MLP, weights mostly fixed).
4. **Working Memory + Episodic Memory** (`episodic_memory.cpp`): ring buffer + associative lookup, no ML needed.
5. **Subconscious Replay** (`subconscious.cpp`): deterministic replay logic, emotional tagging, no ML.
6. **Self-Model + Drives** (`self_model.cpp`, `emotion_drive.cpp`): scalar updates with bounded adaptation (`0.005` rate), no training needed.

**Training burden**: One small synthetic dataset (~500 samples, generated from existing `data/eval_set.jsonl` + `build_seed_dataset.py` patterns) for the 3 MLPs. Estimated training time: <30 min with `tools/train_lora.py` (if using LoRA on the same adapter) or direct PyTorch fine-tuning of the MLP weights.

### Phase 7.8 — Social + Cultural Transmission (Depends on 7.7 + `data/dataset_expanded.jsonl` for Dialogue)
1. **Social Graph** (`social_cognition.cpp`): persistent per-agent edges, bounded updates (`0.05` rate).
2. **Theory-of-Mind Prediction**: simple inference using `world_model` predictions + `social_graph` edges — no ML training needed; uses fixed prediction rules.
3. **Imitation / Cultural Transmission**: `social_edge_weights.imitation_target` updates based on observed positive results; behavior preferences propagate through graph.

### Phase 7.9 — Aggregate Cognition (Depends on 7.8 + 30-Day Soak Data)
1. **Village Aggregate** (`village_aggregate.cpp`): aggregates emotional states, feeds `schedule_bias`.
2. **Economy Aggregate** (`economy_aggregate.cpp`): extends `update_market_prices()` with persistent `economic_memory`.
3. **Nature Aggregate** (`nature_aggregate.cpp`): connects `World::tick_wildlife()` and forest state to `adaptations.json`.
4. **Performance Aggregate** (`performance_aggregate.cpp`): extends existing tuner with historical memory.

### Phase 8 — Deep Integration (Depends on 7.9 + Trained Student Model + Consolidation Dataset)
1. **Consolidation Dataset**: Build synthetic dataset for consolidation (using `NIM` or `tools/gen_dataset.py` expanded with town-state inputs) — this is the user's deferred work (tracked in `docs/ROADMAP.md` Tier 1.5).
2. **Integration Hooks**: `World::tick()` → `apply_adaptations(town_consciousness.snapshot_adaptations())` (already wired in 2026-08-16 session); extend to read aggregate adaptations for `procgen`, `npc`, `weather`, `economy`, `horror`, `performance`.
3. **NPC Schedule Integration**: Wire `schedule_slot()` to read `schedule_bias` from `adaptations.json` and `social_graph` preferences.
4. **Forest/Procgen Integration**: Connect `procgen` biases to aggregate `nature_memory`.

### Phase 9+ — Emergence & Refinement
1. **Distributed Consciousness**: As aggregate systems interact (village emotional state affects horror intensity, which affects economy, which affects nature), emergent behavior appears without explicit scripting.
2. **Observation Tools**: `/town/inspect` (planned in 7.2) shows aggregate cognitive states (`village_memory`, `nature_memory`, `culture_memory`).
3. **Player Awareness**: `horror_flavor()` and `internal_voice()` (already exist) can reference aggregate states (e.g., "The valley remembers..." referencing `village_memory` events).

---

## 6. Key Design Constraints (Enforced by Architecture)

These are the rules the user explicitly requested; they are embedded in the architecture:

| Constraint | Architecture Enforcement |
|-----------|-------------------------|
| LLM ≤2B, only for NLG/interpretation/reasoning | LLM (`LlamaWrapper`) is only invoked for: `/cmd` intent parsing (Tier 1 fallback), consolidation prompt inference (once/day), dialogue generation (`npc_line` with complex reasoning), narrative event synthesis (`roll_night_event` for complex chapters). All tick-level decisions use fixed MLPs + deterministic rules. |
| Actual cognition from lightweight networks + persistent adaptive state | `attention`, `action_evaluator`, `world_model` are MLPs (~30 weights each). `episodic_memory`, `social_graph`, `semantic_memory`, `drive_state`, `self_model` are persistent state vectors. |
| Neural weights mostly fixed; small bounded online adaptation | All MLP weights are initialized (synthetic data) and frozen by default. Only `world_model_bias` (3 floats), `attention_weights` (4 floats per agent), `action_evaluator_weights` (31 floats, very slow `lr=0.002`), `drive_weights` (6 floats, `lr=0.1`), `social_edge_weights` (3 floats, `lr=0.05`) adapt online. All bounded (`clamp` to `[0,1]` or `[-1,1]`). |
| Each important NPC has individualized persistent cognitive state | Every `NPC` gains `CognitiveState` (serialized to save). `Player` also gets one (optional). Aggregate systems (`VillageMind`, `EconomyMind`, `NatureMind`, `CultureMind`) have their own persistent states. |
| Social learning, imitation, cultural transmission | `social_graph` with `imitation_target` edges propagates behavior preferences. Positive emotional tags reinforce observed actions; `schedule_slot()` and `action_evaluator` read learned preferences. `CultureMind` aggregates common practices. |
| Synthetic offline data (NIM/student) for compact models; C++/deterministic for non-ML | `tiny_mlp.h` uses hand-coded C++ (no PyTorch at runtime). Synthetic training data generated by `NIM` endpoint or `tools/gen_dataset.py`. `attention`, `action_evaluator`, `world_model` trained with this data; all other cognition is deterministic (ring buffers, graph updates, scalar decay). |
| Not just humans — aggregate systems as cognitive entities | `VillageMind`, `EconomyMind`, `NatureMind`, `CultureMind` each have `CognitiveState` analogs (`aggregate_memory`, `aggregate_preferences`, `feedback_loop_history`). They observe events, accumulate memory, and emit biases through `adaptations.json`. |
| Long-term: evolving distributed artificial mind; consciousness-like emergence | Aggregate systems interact: `NatureMind` feeds `EconomyMind` (resource availability); `EconomyMind` feeds `VillageMind` (social stress); `VillageMind` feeds `CultureMind` (shared beliefs); `CultureMind` feeds `NatureMind` (management practices). These interactions produce behavior not scripted in any single system. `subconscious_replay` + `episodic_memory` + `self_model` create individual awareness patterns. |
| Biologically inspired; persistent feedback loops; emergence; efficiency | Perception (attention gating) → working memory → emotional/reward modulation → prediction (world-model) → planning (goal-stack + action-evaluator) → action → outcome → reward prediction error → adaptation. All persistent (serialized), all bounded, all deterministic except small MLP predictions. |

---

## 7. Data Flow & Integration with Existing Phase 7 (Town Consciousness)

The cognitive architecture does **not** replace `TownConsciousness`. It coexists:

```
World Tick Loop (per agent, per tick):
  1. Perception (attention) reads world state
  2. Working memory updates with salient events
  3. Episodic memory records event (with emotional tag)
  4. Semantic memory updates (if repeated pattern)
  5. Social graph updates (if agent interaction)
  6. Subconscious replay (during rest/sleep, asynchronous)
  7. Emotion / drives updated by RPE
  8. Prediction (world_model) compares expected vs actual
  9. Planning selects action (action_evaluator + goal_stack)
  10. Action executed (NPC schedule, player command, etc.)
  11. Outcome observed → reward prediction error → adaptation
  12. Aggregate systems (Village, Economy, Nature, Culture) observe events
  13. Town Consciousness (LLM) consolidates daily (04:00) → adaptations.json
  14. Adaptation consumers (Phase 7.3, already wired) apply biases
  15. Cognitive Core updates its state (bounded online adaptation)
  16. Next tick... (emergence accumulates over time)
```

The LLM (`LlamaWrapper`) is **only** called for:
- Step 13 (consolidation prompt — once per in-game day, async worker thread)
- `/cmd` intent parsing when Tier 0 rule fast-path fails (already implemented)
- Complex NPC dialogue (future: LLM generates one-line reply given emotional state + context)
- `/town/why` reasoning trace (on-demand, player curiosity)
- Narrative event synthesis for major story beats (already implemented as `World::roll_night_event`)

All other cognition (steps 1–12, 14–16) runs on the Cognitive Core with **zero LLM invocations**.

---

## 8. Existing Codebase Integration Points

| File | Current State | Cognitive Architecture Integration |
|------|---------------|-----------------------------------|
| `src/town_consciousness.cpp` | LLM-based consolidation, ring buffer, save/load | Add `snapshot_adaptations()` (already done 2026-08-16); becomes one of several `adaptation_source`s; `CognitiveCore` may produce its own `per_agent_adaptations` that complement the LLM's town-wide biases. |
| `src/world.cpp` | `World::tick_wildlife()`, `tick_sanity()`, `update_market_prices()`, `roll_night_event()` | Add `World::village_memory`, `economic_memory`, `culture_memory`, `nature_memory`, `performance_memory` (aggregate cognitive state). Each `tick_*` updates its corresponding aggregate. `apply_adaptations()` already wired; extends to read aggregate biases. |
| `src/main.cpp` | `World` instance, game loop, `/cmd` endpoint, `/horror`, `/market`, `/state` | Game loop calls `apply_adaptations()` each tick (already done 2026-08-16). Add `CognitiveCore` per NPC; NPC tick reads cognitive state, emits action selection. `/town/inspect` endpoint reads aggregate cognitive states. |
| `include/world.hpp` | World class with cells, players, npcs, building_states, market_prices | Add aggregate memory fields (`VillageMemory`, `EconomyMemory`, `NatureMemory`, `CultureMemory`, `PerformanceMemory`). Each NPC gets `CognitiveState` member. |
| `include/town_consciousness.hpp` | `TownConsciousness` class, `Adaptations` struct | Add `CognitiveCore` (new class) per NPC. `Adaptations` struct already covers all 6 sections; aggregate cognition produces deltas that modify the LLM's biases or supply them when LLM is unavailable. |
| `tools/train_lora.py` | LoRA training for Qwen2.5-0.5B | Extend to also train compact MLPs (`attention`, `action_evaluator`, `world_model`) from synthetic data (~500 samples). Save MLP weights to `data/mlp_weights.json` (loaded at startup). |
| `tools/gen_dataset.py` | NIM teacher-student dataset generator | Add `--task cognitive_mlp` mode: generates synthetic `{(inputs, target_outputs)}` for attention/action-evaluation/world-model training. |
| `data/eval_set.jsonl` | 30 intent cases | Add `data/cognitive_eval.jsonl`: 50–100 cases for cognitive components (e.g., "given emotional state X and event Y, expected drive update Z"; "given trust 0.8 and observed action A, expected imitation weight update"). |
| `data/town_log.jsonl` | Structured event log | Cognitive Core per-agent writes to `data/npc_cognitive_state/*.json` (one per NPC) for debugging; aggregate systems write to `data/aggregate_state.json`. |

---

## 9. Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Cognitive Core becomes another tuning burden | Most of it (episodic memory, working memory, self-model, drives) is **zero training** — just C++ state machines. Only 3 small MLPs need training data. |
| Agents diverge too much → chaotic behavior | Bounded adaptation rates (`0.002`–`0.1`), clamping to `[0,1]`, deterministic decay. Personality divergence is bounded by physics (hunger, thirst, safety drives always dominate). |
| Aggregate systems contradict each other | The LLM's daily consolidation acts as a **tiebreaker** — its biases are weighted against aggregate biases (weighted average). `TownConsciousness` is the final authority on `adaptations.json`. |
| Player cannot tell what's happening | `/town/inspect` endpoint exposes aggregate cognitive states; `horror_flavor()` and `internal_voice()` surface NPC awareness in narrative text. |
| Performance cost of per-tick cognitive updates | Each NPC update is O(working_memory_size + episodic_recent_size) = O(10–20 ops). Aggregate updates are O(npc_count) = O(20–50 ops per tick). Total: sub-millisecond per tick. |
| LLM still needed for some reasoning | LLM is **available** — just not the default. `force_consciousness_think()` escape hatch for debugging or "hard mode" where player wants more LLM reasoning. |

---

## 10. Long-Term Vision: The Distributed Mind

As the architecture matures, aggregate systems interact:

```
NatureMind (soil, biodiversity, climate)
    ↓ resource availability
EconomyMind (prices, cycles, inflation)
    ↓ social stress
VillageMind (collective emotion, rituals)
    ↓ shared beliefs
CultureMind (norms, fears, preferences)
    ↓ management practices
NatureMind (regeneration, harvest, fire suppression)
```

This loop runs over weeks of in-game time. A drought (NatureMind) reduces crop yields → commodity prices spike (EconomyMind) → villagers feel economic anxiety (VillageMind) → collective fear of storms increases (CultureMind) → fire suppression practices decline (back to NatureMind) → fire risk rises. This cycle is **not scripted** — it emerges from the bounded adaptation of each aggregate.

Individual NPC cognition (Cognitive Core) layers on top: each NPC has its own emotional response, learned preferences, and social relationships. Two NPCs in the same village can have opposite reactions to the same drought — one farmer adapts, another panics, based on their individual cognitive history.

The LLM (Tier 2) provides language for this: when the player asks `/horror`, the response synthesizes the current aggregate horror state into prose. When `/town/why`, it explains recent adaptation choices. When an NPC says something, the LLM generates a one-line reply informed by that NPC's cognitive state (emotional tag + learned preferences + social graph).

The result is a game where:
- NPCs have **distinct personalities** that emerge from experience.
- The village has a **mood** that shifts over time.
- The economy has **cycles** that feel organic.
- The forest has **health** that responds to player actions.
- Horror intensity is **calibrated** to collective anxiety, not arbitrary LLM output.
- The player's choices have **persistent effects** on all of these systems.

---

*This document is the cognitive architecture specification. Implementation begins Phase 7.7 (after docs refresh + training pipeline commit `47f1898`). Update this doc as components are designed in detail.*
