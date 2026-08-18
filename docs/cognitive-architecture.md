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
- **Persistence**: Working memory is NOT persisted to disk (ephemeral by design). Only the hidden state vector and long-term memories persist.

### 3.3 Episodic Memory
- **Storage**: Structured event records (same schema as `TownEvent` from Phase 7): `{system, event_type, payload, emotional_tag, tick}`. Stored per-agent, not global.
- **Retrieval**: Associative lookup by `{context_tag}` (current season, location, emotional state) using a small associative memory matrix (`associative_memory` weights: 16×16, fixed + bounded updates).
- **Replay**: Subconscious replay runs during sleep (`tick_sanity` / rest events) — reinforces emotional tags, strengthens predictions, updates world-model biases.
- **Implementation**: `src/episodic_memory.cpp`. Uses `std::map<uint32_t tick, EventRecord>`. Replay selects events with high emotional_tag / high context-match.
- **Persistence**: Episodic memory IS persisted to disk (serialized to `data/npc_cognitive_state/<agent_id>.json`). On load, the full episodic history is restored, allowing the agent to remember across sessions.

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
- **Persistence**: All emotional state, drive states, and drive weights are persisted to disk as part of the agent's cognitive state.

### 3.6 Prediction / World-Model
- **World-model**: Per-agent predictive model — not a full physics simulator, but a compact predictive MLP (`world_model` weights: 8 inputs → 4 hidden → 3 outputs: `predicted_weather_shift`, `predicted_social_response`, `predicted_resource_availability`). Inputs = current season, recent events from episodic memory, semantic memory facts, emotional state.
- **Fixed weights** (trained offline with synthetic data; see §6). Online adaptation: only a small `world_model_bias` vector (3 floats) updates per tick based on prediction errors (`new_bias += 0.01 * error`).
- **Implementation**: `src/world_model.cpp`. Uses `tiny_mlp.h` (hand-written 8×4 MLP, no external library dependency).
- **Persistence**: World model bias vector and any adapted weights are persisted to disk as part of the agent's cognitive state.

### 3.7 Learned Preferences
- **Preference vector**: Per-agent float array (e.g., `crop_preference`, `gift_preference_shift`, `location_preference`, `social_target_preference`). Updated by positive emotional tags (`new += 0.02 * positive_tag`) and decayed (`new *= 0.999` per tick).
- **Bounded**: Each preference clamped to `[0, 1]`. The model outputs preference-shift predictions; the agent's actual preferences adapt independently.
- **Implementation**: `src/preferences.h` — simple float array with decay/update logic.
- **Persistence**: All learned preferences are persisted to disk as part of the agent's cognitive state.

### 3.8 Social Cognition / Theory-of-Mind
- **Social graph**: Per-agent `social_graph` — nodes = known agents; edges = `{trust, familiarity, emotional_history_sum, imitation_target}`. Persistent across saves.
- **Theory-of-mind**: Not a full mental simulation; a simple inference: `predicted_social_action = f(edge.trust, edge.familiarity, agent_A.self_state, agent_A.world_model)`. This predicts whether another agent will help, compete, or ignore, enabling cooperative planning.
- **Social learning**: Agents observe successful actions of others (`imitation_target` edges). When `edge.imitation_target > 0.5` and the observed action yields positive emotional tag, the observer copies the behavior preference. This creates **cultural transmission** (e.g., if one NPC learns to repair buildings quickly, others near them learn the repair behavior).
- **Bounded adaptation**: Edge weights (`social_edge_weights`) adapt by `new = 0.95*old + 0.05*interaction_result`. Very slow.
- **Implementation**: `src/social_cognition.cpp`. Graph stored as `std::map<std::string agent_id, SocialEdge>`. Updates in `NPC::tick_social()` called each tick.
- **Persistence**: Full social graph (nodes, edges, weights) is persisted to disk as part of the agent's cognitive state.

### 3.9 Self-Model
- **Self-representation**: Per-agent vector `{self_esteem_estimate, competence_estimate, autonomy_estimate}`. Updated by comparing actual outcomes to predictions (`world_model`). If predictions are consistently wrong (high error), `self_esteem_estimate` drops slowly; if predictions are accurate, it rises.
- **Effect**: Low self-esteem → higher `safety` drive weight, lower `curiosity` weight, reduced social initiative. High self-esteem → more exploration, more social risk-taking. Creates personality divergence.
- **Bounded adaptation**: Self-state updates by `new += 0.005 * normalized_prediction_accuracy` (extremely slow, ~days of experience).
- **Implementation**: `src/self_model.cpp`. Small vector update in the agent tick loop.
- **Persistence**: Self-model state is persisted to disk as part of the agent's cognitive state.

### 3.10 Subconscious Processing
- **Subconscious replay buffer**: Runs during sleep/rest (`tick_sanity` or `sleep` event). Selects 3–5 episodic events with high emotional_tag and replays them: updates episodic confidence, reinforces predictions, updates social edges (if another agent involved), updates self-model.
- **Dream / nightmare synthesis**: A simple generative mechanism: combine replayed events with noise → generate `last_night_event` (already exists as `World::roll_night_event`). The cognitive architecture enhances this: replay events influence which events are selected, and emotional tags bias event generation.
- **Implementation**: `src/subconscious.cpp`. Runs in `World::tick_sanity()` or during `sleep` events. Uses replay logic + deterministic noise.
- **Persistence**: Subconscious replay modifies episodic memory, emotional tags, and biases which are all persisted. The replay buffer itself is not persisted (ephemeral).

### 3.11 Planning & Action Selection
- **Planning**: Not a full planner; a **goal-stack** + **action-evaluator**.
  - `goal_stack`: Per-agent ordered list of `DriveGoal` objects (`{drive_type, urgency, target_location, deadline_tick}`). Updated by emotional/reward state.
  - `action_evaluator`: Small MLP (`action_evaluator` weights: 10 inputs → 3 hidden → action_score). Inputs: current working memory top items, emotional state, drive urgency, prediction outputs. Outputs: score for each available action (`go`, `interact`, `talk`, `repair`, `harvest`, etc.).
- **Bounded adaptation**: Only the `action_evaluator` weights update, very slowly (`learning_rate = 0.002`), and only if action results in positive emotional tag. This creates **behavioral drift** without catastrophic forgetting.
- **Implementation**: `src/planning.cpp`. Action scores computed per tick; top-scored action selected (or combined with NPC schedule constraints from the existing `schedule_slot` system).
- **Persistence**: Goal stack and action evaluator weights are persisted to disk as part of the agent's cognitive state.

---

## 3.12 Hidden State Persistence (NEW — Core Requirement)

**Every cognitive agent (NPCs, animals, aggregate systems) maintains a persistent hidden state vector that evolves continuously and is serialized to disk with the save game.**

### 3.12.1 What Is Persisted
For each agent/aggregate, the following hidden state is serialized to `save.json` (or per-agent files in `data/npc_cognitive_state/<agent_id>.json`):

| Component | Serialized Fields | Size Estimate |
|-----------|-------------------|---------------|
| **Hidden State Vector** (RNN/SSM latent) | `float[hidden_dim]` — the core recurrent hidden state | 256–512 floats |
| **RNN/SSM Parameters** (if adapting) | `W_in`, `W_rec`, `W_out` (if online adaptation enabled) | ~1–4 KB |
| **Episodic Memory** | Full event records with emotional tags | ~10–50 KB |
| **Semantic Memory** | Facts with confidence scores | ~5–20 KB |
| **Working Memory** | NOT persisted (ephemeral by design) | — |
| **Emotional State** | Valence, arousal, 7 discrete tags | ~64 bytes |
| **Drive State** | 6 drive satisfactions + 6 weights | ~96 bytes |
| **World Model** | Bias vector (3 floats) + adapted weights (if any) | ~100 bytes |
| **Preferences** | Float array (crop, gift, location, social) | ~32 bytes |
| **Social Graph** | Nodes + edges (trust, familiarity, imitation_target) | ~1–10 KB |
| **Self Model** | 3 floats (esteem, competence, autonomy) | ~12 bytes |
| **Goal Stack** | Up to 5 goals with urgency/target/deadline | ~200 bytes |
| **Action Evaluator Weights** | 31 floats (if adapting) | ~124 bytes |
| **Causal Traces** | Ring buffer of last 20 decisions | ~2 KB |
| **RNN/SSM Hidden State** | Core latent vector for sequence modeling | 256–512 floats |

### 3.12.2 Save/Load Behavior
- **On World Save** (`World::save()`): All cognitive states are serialized to `save.json` (or companion files). This includes the hidden state vectors for every agent and aggregate system.
- **On World Load** (`World::load()`): Cognitive states are deserialized, restoring the exact hidden state, memories, and parameters. Agents resume with the same "mind" they had when the game was saved.
- **New Game**: When `newgame` command is issued, ALL cognitive states are discarded. Fresh agents are created with randomized initial hidden states (drawn from a distribution that ensures diversity). No carryover from previous saves.
- **Versioning**: Cognitive state schema includes a `schema_version` field. On load, if version mismatches, a migration function upgrades the state (e.g., adds new fields with defaults).

### 3.12.3 Aggregate System Persistence
Aggregate minds (`VillageMind`, `EconomyMind`, `NatureMind`, `CultureMind`, `PerformanceMind`, `ValleyMind`) also persist their hidden states:
- **Aggregate Hidden State**: `float[aggregate_dim]` latent vector representing collective "mood"/pressure
- **Aggregate Memory**: Episodic + semantic memory at the system level
- **Aggregate Biases**: Current `schedule_bias`, `market_volatility`, `procgen_biases`, etc.
- These are saved in `save.json` under `village_memory`, `economic_memory`, `nature_memory`, `culture_memory`, `performance_memory`, `valley_memory`.

### 3.12.4 Implementation Notes
- **Serialization Format**: JSON for readability/debugging, with optional binary (MessagePack) for large hidden states
- **Save Frequency**: Autosave every 60s + on `sleep` command + on `save` command
- **Corruption Handling**: If cognitive state fails to load, agent reinitializes with fresh random state (logs warning)
- **Memory Budget**: Target <50 KB per important NPC, <200 KB per aggregate → total <2 MB for cognitive state in save file

---

## 4. Aggregate / Collective Cognition (Not Just Humans)

The architecture treats persistent game systems as **potential cognitive aggregates** — not individual brains, but collective systems that accumulate state, receive feedback, and develop emergent behavior.

### 4.1 Village Aggregate (`VillageMind`)
- **State**: Aggregate of all NPC emotional states (`mean_valence`, `mean_arousal`), economic trends (`market_volatility`, `demand_shift`), and social graph density (`average_edge_trust`).
- **Memory**: `village_memory` — aggregate episodic records (major events: festivals, disasters, player major actions, economic crashes). Shared with `TownConsciousness` but interpreted as collective memory.
- **Behavior**: No direct action selection, but biases NPC schedules (`schedule_bias`), economic prices (`market_volatility`), and horror intensity (`horror_night_event_weight`) through the adaptation loop.
- **Social transmission**: Cultural practices spread through `social_edge_weights` — a repair technique learned by one NPC propagates to others through observation (`imitation_target`).
- **Implementation**: `src/village_aggregate.cpp`. Updates `World::village_memory` (new field) and pushes aggregate biases to `adaptations.json` sections (`npc`, `economy`, `horror`).
- **Persistence**: `village_memory` (aggregate episodic + semantic), aggregate hidden state vector, and current biases are persisted to `save.json`. On load, the village mind resumes with its collective mood and memories intact.

### 4.2 Economy Aggregate (`EconomyMind`)
- **State**: Not just `market_prices`; a persistent `economic_memory` tracking `commodity_cycles`, `trade_route_health`, `inflation_rate`, `player_impact_on_supply`.
- **Memory**: Episodic records of economic events (`market_crash_day = 14`, `player_hoards_wood`). Semantic memory of `commodity_demand_patterns` (seasonal demand curves learned from `update_market_prices` history).
- **Behavior**: Economic adaptations (`price_elasticity`, `market_volatility`, `demand_shift`) are not arbitrary LLM outputs — they are computed from aggregate economic memory: `volatility += |price_change_rate - historical_mean|`, `demand_shift` reflects observed player buying patterns.
- **Implementation**: `src/economy_aggregate.cpp`. Updates `World::economic_memory` (new field) and feeds back to `update_market_prices()` and adaptation consumers.
- **Persistence**: `economic_memory` (episodic + semantic demand patterns), aggregate hidden state, and current market biases persisted to `save.json`.

### 4.3 Culture Aggregate (`CultureMind`)
- **State**: Aggregate of `semantic_memory` across NPCs — common beliefs, shared rituals, collective fears (horror), collective preferences (food, gifts, festivals).
- **Memory**: `culture_memory` — `ritual_practices` (frequency of repair, gift-giving, festival attendance), `shared_fears` (frequency of horror events affecting NPC emotional tags), `collective_preferences` (aggregate gift preferences).
- **Behavior**: Cultural transmission through `social_edge_weights`. If a practice (e.g., "repair buildings on rainy days") achieves positive emotional tags for multiple agents, it propagates through the social graph. `schedule_bias` in adaptations reflects aggregate behavior patterns.
- **Implementation**: `src/culture_aggregate.cpp`. Updates `World::culture_memory`; feeds `schedule_bias` and `dialogue_topic_weight` adaptations.
- **Persistence**: `culture_memory` (semantic aggregates), aggregate hidden state, and cultural biases persisted to `save.json`.

### 4.4 Nature / Forest Aggregate (`NatureMind`)
- **State**: Aggregate of `ecological_state` from Phase 8.1 (forest ecology). `forest_memory` — `disturbance_legacy`, `succession_stage`, `climate_trend`.
- **Memory**: Episodic: `fire_events`, `storm_damage`, `harvest_patterns`. Semantic: `soil_health_trend`, `biodiversity_index`, `carbon_storage`.
- **Behavior**: The forest does not "act" in the NPC sense, but its aggregate state influences adaptations (`procgen_biases`, `storm_chance`, `disaster_chance`). Over long time scales (10+ in-game days), persistent feedback loops create emergent ecological behavior: droughts reduce biodiversity, which reduces pollination, which reduces crop yields, which increases player stress, which increases horror events. This is **not scripted** — it emerges from the interaction of deterministic systems (CA for fire, agent-based for pests, Petri nets for soil) biased by the adaptation loop.
- **Implementation**: `src/nature_aggregate.cpp`. Updates `World::nature_memory`; feeds `procgen` and `weather` adaptations.
- **Persistence**: `nature_memory` (episodic + semantic), aggregate hidden state, and ecological biases persisted to `save.json`.

### 4.5 Performance Aggregate (`PerformanceMind` — already exists)
- **State**: `performance_profile` (CPU %, memory, tick latency) from Phase 7.4.
- **Behavior**: `tick_budget_ms`, `npc_decision_interval_ticks`, `chunk_load_radius` adjustments are not just "tune for FPS" — they represent the aggregate's adaptive response to resource constraints. Slow tick budget = more deliberate, slower NPCs; faster budget = more reactive, more frequent observations.
- **Implementation**: Already partially implemented (`PerformanceMonitor`). Extended: add `World::performance_memory` tracking historical performance, feeding `performance_adapt` in `adaptations.json`.
- **Persistence**: `performance_memory` (historical profile), aggregate hidden state, and tuner state persisted to `save.json`.

### 4.6 Valley Entity (`ValleyMind`)
- **State**: `collective_guilt`, `valley_awakening`, spatial corruption CA.
- **Memory**: `valley_memory` — spatial corruption CA state, horror cycle counter, collective guilt trajectory.
- **Behavior**: Drives the valley-as-entity feedback loop: guilt → corruption → awakening → horror intensity → more guilt.
- **Implementation**: `src/valley_mind.cpp`. Updates `World::valley_memory`; feeds horror adaptations.
- **Persistence**: `valley_memory` (corruption CA state, guilt trajectory, cycle counter), aggregate hidden state persisted to `save.json`.

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

## 11. Comprehensive AI/ML/LLM Systems Inventory

This section provides a complete inventory of every AI, ML, neural network, and LLM system currently implemented or planned for Ashgrove Valley, categorized by function and implementation status.

### 11.1 Large Language Models (LLMs) — Tier 2 (On-Demand, ≤2B Parameters)

| System | Model | Role | Status | Training Method |
|--------|-------|------|--------|-----------------|
| **Intent Parser** | Qwen2.5-0.5B LoRA (GGUF Q4_K_M) | `/cmd` natural language → structured intent JSON `{action, parameters}` | **Implemented & Wired** (Tier 1 fallback when rule engine fails) | Student-teacher distillation from NVIDIA NIM (NIM teacher → student LoRA). Dataset: `tools/gen_dataset.py` + `data/eval_set.jsonl` (30 cases). |
| **Town Consciousness Consolidation** | Same Qwen2.5-0.5B LoRA | Daily (04:00) consolidation of event buffer → `adaptations.json` (biases for procgen, NPC, economy, weather, horror, performance) | **Implemented & Wired** (async worker, once per in-game day) | Same LoRA. **Limitation**: Trained only on intent format; consolidation outputs malformed JSON → adaptations stay at defaults. **Deferred** (ROADMAP 1.5). |
| **NPC Dialogue Generator** | Same Qwen2.5-0.5B LoRA (currently) | One-line in-character dialogue given emotional state + context (ROADMAP 1.7d) | **Wired with Template Fallback** (ROADMAP 1.7d) | **Current model unsuited** (intent LoRA produces narrative garbage). **Future**: separate dialogue-tuned LoRA or unified model with dialogue head. |
| **Narrative Event Synthesis** | Same Qwen2.5-0.5B LoRA | Complex `roll_night_event` chapters, `/town/why` explanations | **Implemented** (partial) | Same LoRA; limited to structured outputs it was trained for. |
| **Future: Unified Reasoning Engine** | Planned: Qwen2.5-1.5B or 3B LoRA (≤2B target) | Consolidation + dialogue + reasoning in one model; multi-head output (intent, consolidation, dialogue) | **Planned (Post-ROADMAP 1.5)** | Multi-task LoRA training on expanded dataset (intent + consolidation + dialogue). |

**Model Policy**: Runtime LLM is local llama.cpp only (CPU inference, ~28 tok/s on i3-4160). NVIDIA NIM is used **only offline** for training data generation (`tools/gen_dataset.py`, `gen_cognitive_mlp_data.py`), never at runtime.

### 11.10 Custom Ashgrove LLM — Purpose-Built Language Model (Future)

**Goal**: Replace the general-purpose Qwen2.5-0.5B LoRA with a **purpose-trained Ashgrove Language Model** that deeply understands the game's mechanics, lore, cognitive architecture, and narrative style.

#### 11.10.1 Why Build Our Own?
| Issue with Current Approach | Custom Model Solution |
|----------------------------|----------------------|
| Intent LoRA produces narrative garbage for dialogue | Trained specifically for **cognitive-state-conditioned dialogue generation** |
| Consolidation prompt fails (wrong output format) | Trained on **consolidation prompt → adaptations JSON** format |
| No understanding of game mechanics/lore | Trained on **game state → narrative** pairs |
| General-purpose = wasted capacity | **Specialized tokenizer + architecture** for game concepts |
| Can't do cognitive-state-conditioned reasoning | **Cognitive state embedding** as conditional input |

#### 11.10.2 Model Design
| Aspect | Specification |
|--------|---------------|
| **Base Architecture** | State-space model (Mamba/SSM) or efficient Transformer (≤1B params) |
| **Context Window** | 4K–8K tokens (sufficient for cognitive state + prompt) |
| **Conditional Inputs** | Cognitive state embeddings (hidden state + memory summaries) prepended to prompt |
| **Multi-Head Outputs** | 1) Intent JSON, 2) Consolidation JSON, 3) Dialogue line, 4) Narrative prose |
| **Tokenizer** | Custom BPE trained on game text (mechanics terms, lore, NPC names, item names) |
| **Training** | Multi-task: intent parsing + consolidation + dialogue + narrative synthesis |
| **Quantization** | Q4_K_M GGUF for CPU inference (~28 tok/s on i3-4160) |

#### 11.10.3 Training Pipeline
1. **Data Generation**: Use NIM teacher to generate synthetic data for all tasks:
   - `{(game_state, player_text) → intent_json}`
   - `{(event_buffer, world_state) → adaptations_json}`
   - `{(cognitive_state, player_context) → dialogue_line}`
   - `{(world_state, narrative_beat) → narrative_prose}`
2. **Multi-Task Training**: Single model with task-specific prefix tokens (`<intent>`, `<consolidate>`, `<dialogue>`, `<narrate>`)
3. **Cognitive State Embedding**: Learn projection from cognitive state vector → token embeddings
4. **Curriculum**: Start with intent (easy), add consolidation, then dialogue, then narrative
5. **Distillation**: If needed, distill to smaller student (0.3B) for faster inference

#### 11.10.4 Integration Points
- **Intent Parsing**: `<intent>` prefix + player text → structured JSON
- **Consolidation**: `<consolidate>` + event_buffer + world_state → adaptations JSON
- **Dialogue**: `<dialogue>` + cognitive_state_embedding + player_context → one-line reply
- **Narrative**: `<narrate>` + world_state + narrative_beat → prose chapter
- **Reasoning** (`/town/why`): `<reason>` + adaptation_history → explanation

#### 11.10.5 Roadmap Placement
| Tier | Item | Dependency |
|------|------|------------|
| 1.5 | Consolidation-format dataset + retrain existing LoRA | User sign-off |
| 1.7d+ | Dialogue LoRA (interim) | Consolidation dataset |
| **Post-1.5** | **Custom Ashgrove LLM (unified multi-task)** | Consolidation dataset + dialogue data + compute budget |
| 4.1+ | Full narrative integration | Custom LLM + all aggregates implemented |

**This custom model is the endgame for Tier 2 — a single, purpose-built model that replaces all current LLM uses and enables true cognitive-state-conditioned language generation.**

---

### 11.2 Lightweight Neural Networks (MLPs) — Tier 1 (Every Tick, Sub-Millisecond)

These are **fixed-weight** MLPs trained offline on synthetic data; only small bounded online adaptation permitted.

| Network | Architecture | Inputs → Outputs | Online Adaptation | Training Data Source | Status |
|---------|--------------|------------------|-------------------|---------------------|--------|
| **Attention Gating MLP** | 4 → 32 → 1 (salience score) | [novelty, emotional_arousal, relevance, survival] → gate score | `attention_weights` (4 floats, lr=0.001) | Synthetic: `tools/gen_cognitive_mlp_data.py` (500 samples) | **Implemented** (Phase 7.7) |
| **Action Evaluator MLP** | 10 → 3 → 6 (action scores) | [WM_top3, emotion, drives, predictions] → 6 action scores | `action_evaluator_weights` (31 floats, lr=0.002) | Synthetic (500 samples) | **Implemented** (Phase 7.7) |
| **World Model MLP** | 8 → 4 → 3 (predictions) | [season, recent_events, semantic_facts, emotion] → [weather_shift, social_response, resource_avail] | `world_model_bias` (3 floats, lr=0.01) | Synthetic (500 samples) | **Implemented** (Phase 7.7) |
| **Associative Memory Matrix** | 16×16 fixed weights | Context tag → episodic recall | Fixed + bounded updates | N/A (rule-based) | **Implemented** |
| **Emotion Prediction MLP** | 8 → 16 → 7 (discrete tags) | Event features → 7 emotional tag intensities | Fixed (no online adaptation) | Synthetic | **Planned** (Phase 7.7) |

**Key Properties**:
- All weights frozen by default after offline training
- Online adaptation only on tiny parameter subsets (biases, 4–31 floats max)
- Learning rates extremely slow (0.001–0.1), clamped to [0,1] or [-1,1]
- No external ML library at runtime — hand-coded `tiny_mlp.h` forward pass in C++
- Total per-agent inference: <0.1 ms per tick

### 11.3 Deterministic Cognitive Systems (Zero Training, Pure C++)

These systems implement biologically-inspired cognitive functions without any neural network weights. They are the "thinking engine" that runs every tick.

| System | Function | Key Data Structures | Status |
|--------|----------|---------------------|--------|
| **Perception / Attention** | Gating MLP + deterministic visibility/audibility checks → top-K stimuli | `WorkingMemoryItem` ring buffer (cap 7), `attention_weights` (4 floats) | **Implemented** |
| **Working Memory** | Bounded buffer (4-7 items), exponential decay, replay refresh | `std::deque<WorkingMemoryItem>`, decay_factor ×0.995/tick | **Implemented** |
| **Episodic Memory** | Structured event records, associative lookup by context tag, replay | `std::deque<EpisodicEvent>` (cap 256), `last_access_tick` for LRU (1.7b) | **Implemented** + LRU |
| **Semantic Memory** | Long-term facts `{subject, predicate, object, confidence}`, social transmission | `std::map<std::string, SemanticFact>` (cap 64), confidence decay 0.998/tick | **Implemented** |
| **Emotion / Drive System** | Continuous valence×arousal + 7 discrete tags; 6 drives with satisfaction curves | `EmotionalTag`, `DriveState` (6 drives), bounded weight adaptation | **Implemented** |
| **World Model (Predictive)** | MLP predictions + online bias correction from prediction error | `world_model_bias[3]`, prediction error → bias update | **Implemented** |
| **Learned Preferences** | Per-agent float array updated by emotional tags, decayed | `preference_vector[]`, `new *= 0.999`/tick | **Implemented** |
| **Social Graph / Theory of Mind** | Nodes=agents, edges={trust, familiarity, imitation_target}; simple ToM inference | `std::map<agent_id, SocialEdge>`, bounded updates (lr=0.05) | **Implemented** |
| **Self Model** | {self_esteem, competence, autonomy} updated by prediction accuracy | 3-float vector, update rate 0.005 | **Implemented** |
| **Subconscious Replay** | Sleep/rest replay: reinforces emotional tags, updates social edges, self-model | Triggered by `tick_sanity`/`sleep`; selects high-emotion events | **Implemented** |
| **Planning / Action Selection** | Goal stack + action_evaluator MLP scores → top action | `goal_stack` (cap 5), MLP scores, drive urgency | **Implemented** |
| **Causal Traces (Debug)** | Per-decision record: drives, stimuli, emotion, scores | `CausalTrace` ring (cap 20) | **Implemented** (ROADMAP 1.7c) |

### 11.4 Aggregate / Collective Cognitive Systems (System-Level Cognition)

These are NOT individual agents but persistent system-level states that accumulate memory and emit biases.

| Aggregate | State Components | Memory | Output (Adaptations) | Status |
|-----------|------------------|--------|----------------------|--------|
| **Valley Entity / ValleyMind** | `collective_guilt`, `valley_awakening`, spatial corruption CA | `corruption` per cell (0-255), `horror_cycle` counter | `horror_intensity`, `horror_sanity_drain_multiplier`, `weather_fog_intensity`, `horror_phantom_sighting_chance` | **Implemented** (ROADMAP 1.2) |
| **VillageMind** | `mean_valence`, `mean_arousal`, `market_volatility`, `demand_shift`, `avg_edge_trust` | Aggregate episodic records, `village_memory` | `schedule_bias`, `market_volatility`, `horror_night_event_weight` | **Implemented** (Phase 7.9) |
| **EconomyMind** | `commodity_cycles`, `trade_route_health`, `inflation_rate`, `player_impact` | `economic_memory` (episodic + semantic demand patterns) | `price_elasticity`, `market_volatility`, `demand_shift` | **Implemented** (Phase 7.9) |
| **NatureMind** | `disturbance_legacy`, `succession_stage`, `climate_trend`, `biodiversity` | `nature_memory` (fire, storm, harvest episodes) | `procgen_biases`, `storm_chance`, `disaster_chance` | **Implemented** (Phase 7.9) |
| **CultureMind** | `ritual_practices`, `shared_fears`, `collective_preferences` | `culture_memory` (semantic aggregates) | `schedule_bias`, `dialogue_topic_weight` | **Implemented** (Phase 7.9) |
| **PerformanceMind** | `cpu_pct`, `memory`, `tick_latency` history | `performance_memory` (historical profile) | `tick_budget_ms`, `npc_decision_interval`, `chunk_load_radius` | **Implemented** (Phase 7.4 + extensions) |
| **Town Consciousness** | LLM-based consolidation of event buffer | `town_memory.json`, `adaptations.json`, `town_log.jsonl` | `adaptations.json` (6 sections) | **Implemented** (Phase 7) |

### 11.5 Training & Data Generation Pipeline (Offline Only)

| Component | Purpose | Technology | Status |
|-----------|---------|------------|--------|
| **NVIDIA NIM Teacher** | Generate high-quality training data (intent, consolidation, dialogue) | NIM API (cloud GPU) | **Used offline** |
| **Student LoRA Training** | Qwen2.5-0.5B → intent LoRA (`qwen2.5-0.5b-ashgrove-q4_k_m.gguf`) | `tools/train_lora.py` (PyTorch/PEFT) | **Done** |
| **Future: Consolidation LoRA** | Consolidation-format training data → retrain LoRA | `tools/train_lora.py` + expanded dataset | **Deferred** (ROADMAP 1.5) |
| **Future: Dialogue LoRA** | Dialogue-tuned adapter for NPC one-liners | Multi-head or separate adapter | **Planned** |
| **Cognitive MLP Synthetic Data** | Generate 500 samples for attention/action/world_model | `tools/gen_cognitive_mlp_data.py` | **Implemented** |
| **Synthetic Data Format** | `{(inputs, targets)}` JSONL for MLP training | `data/cognitive_mlp_train.jsonl` | **Implemented** |
| **MLP Weight Export** | JSON `mlp_weights.json` loaded at C++ startup | `data/mlp_weights.json` | **Implemented** |

### 11.6 Runtime Inference Pipeline (How It All Connects)

```
┌─────────────────────────────────────────────────────────────────────┐
│                     WORLD TICK LOOP (every ~16ms)                   │
├─────────────────────────────────────────────────────────────────────┤
│  Per NPC (Cognitive Core - Tier 1):                                 │
│  1. Perception → Attention MLP (novelty×emotion×relevance)          │
│  2. Working Memory update (decay, insert salient)                   │
│  3. Episodic Memory record (emotional tag, tick, payload)           │
│  4. Semantic Memory update (confidence decay, social transmission)  │
│  5. Social Graph update (trust, familiarity, imitation_target)      │
│  6. Subconscious Replay (if rest/sleep tick)                        │
│  7. Emotion/Drives update (RPE from action outcomes)                │
│  8. World Model prediction → bias update from prediction error      │
│  9. Goal Stack update (drive urgency → goal urgency)                │
│  10. Action Evaluator MLP → action scores → select action           │
│  11. Self Model update (prediction accuracy → self_esteem)          │
│  12. Causal Trace record (for debugging)                            │
│  [ALL ABOVE: Zero LLM calls, pure C++ + tiny MLP forward passes]    │
├─────────────────────────────────────────────────────────────────────┤
│  Aggregate Systems (per tick):                                      │
│  - ValleyMind tick (spatial corruption CA, guilt decay)             │
│  - VillageMind tick (emotional aggregate, social density)           │
│  - EconomyMind tick (price cycles, demand tracking)                 │
│  - NatureMind tick (ecology, biodiversity, disturbance)             │
|  - CultureMind tick (ritual frequency, shared fears)                |
|  - PerformanceMind tick (CPU/memory/tick latency history)           |
├─────────────────────────────────────────────────────────────────────┤
│  ON-DEMAND LLM CALLS (Tier 2):                                      │
│  - /cmd intent parsing (when rule engine fails)                     │
│  - NPC dialogue (cognitive prompt → LLM → validation → fallback)    │
│  - Daily consolidation (04:00, async worker)                        │
│  - /town/why (player curiosity)                                     │
│  - Complex narrative event synthesis                                │
├─────────────────────────────────────────────────────────────────────┤
│  DAILY CONSOLIDATION (04:00 in-game):                               │
│  1. TownConsciousness collects event buffer                         │
│  2. aggregate_memory() → structured summary                         │
│  3. LLM inference (1024 tokens, 0.1 temp) → adaptations JSON        │
│  4. parse_llm_response → apply_adaptations() with 0.3/0.7 damping   │
│  5. Adaptations applied to: procgen, NPC, economy, weather, horror  │
└─────────────────────────────────────────────────────────────────────┘
```

### 11.7 Future AI/ML Systems (Roadmap)

| System | Roadmap Tier | Description | Dependency |
|--------|--------------|-------------|------------|
| **Consolidation-Format LoRA** | 1.5 (Deferred) | Retrain LoRA on consolidation prompt→adaptations format | `tools/gen_dataset.py` expansion + user sign-off |
| **Dialogue LoRA / Multi-Head Model** | 1.7d+ | Separate dialogue adapter or multi-head LoRA for NPC one-liners | Consolidation dataset + compute budget |
| **Procedural Wilderness ML** | 3.1 | Biome transitions, landmark generation via learned spatial priors | 2.x ecology chain (2.5→2.8→2.9→3.1) |
| **Procedural Story Generator** | 3.2 | Quest generation from world state/history/relationships | 2.9 (terrain/ecological change) |
| **Offline Simulation (Compressed Tick)** | 3.3 | Batch processing of crops/NPC schedules/weather/economy on restart | 2.9 |
| **Structural Physics ML** | 2.7 | Rot/erosion CA, tool wear grammar, fire spread CA | 2.5-2.8 |
| **Creature Biology ML** | 2.8 | Metabolism Petri nets, disease contact graphs, aging L-systems | 2.5, 2.6, 2.7 |
| **Distributed Consciousness Integration** | 4.1+ | Aggregate interaction loops (Nature→Economy→Village→Culture→Nature) | All aggregates implemented + 30-day soak |

### 11.8 Hardware & Performance Constraints

| Constraint | Value | Impact on AI Design |
|------------|-------|---------------------|
| **CPU** | i3-4160 (4 cores, 4 threads) | No GPU; all MLPs hand-coded C++; LLM CPU-only llama.cpp |
| **RAM** | 7.7 GiB + 11 GiB swap | Model ≤500 MB (Q4_K_M ~374 MB); MLPs <1 MB |
| **LLM Inference** | ~28 tok/s (Q4_K_M, CPU) | Consolidation ~1024 tok = ~40s; dialogue ~40 tok = ~1.5s |
| **Build** | `cmake --build build -j4` ~3 min (BOINC CPU hog) | Prefer detached screen builds |
| **LLM Concurrency** | Serialized by `llama.m_mutex` | Consolidation (1024 tok) blocks all other LLM calls |

### 11.9 Design Principles Summary

1. **LLM is vocabulary/syntax, not cognition** — Tier 2 only for language; Tier 1 (Cognitive Core) is the thinking system
2. **Neural weights frozen by default** — Only tiny bounded online adaptation (biases, 4–31 floats)
3. **Cognition is persistent state + deterministic rules + tiny MLPs** — Not large generative models
4. **Aggregate systems as cognitive entities** — Village, Economy, Nature, Culture have their own persistent memory and feedback loops
5. **Emergence over scripting** — Interactions between aggregates produce unscripted long-term behavior
6. **Individualization** — Each NPC diverges through experience (cognitive state serialized per agent)
7. **Efficiency first** — Sub-millisecond per-agent tick; LLM only on-demand
8. **Training offline, inference online** — All training (NIM, synthetic data) done offline; runtime is pure inference
9. **Model policy strict** — Local llama.cpp only at runtime; NIM/NVIDIA only for offline data generation

---

## 10. Long-Term Vision: The Distributed Mind
