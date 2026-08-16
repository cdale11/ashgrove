# Ashgrove Command Dataset Schema

Defines the on-disk formats used by the Phase 8 (Model Distillation) pipeline.
Three related files are produced/consumed:

| File | Producer | Purpose |
|------|----------|---------|
| `data/cmdlog.jsonl` | Server (live gameplay) | Raw, ever-growing record of real commands + intents + responses. |
| `data/dataset.jsonl` | `tools/build_seed_dataset.py` | Canonical seed corpus: every command × slot × alias mapped to `{action, parameters}`, generated deterministically from the game code. |
| `data/dataset_expanded.jsonl` | `tools/gen_dataset.py` | Distillation training set: canonical seeds + cloud-teacher paraphrases → `{action, parameters}`. |
| `data/eval_set.jsonl` | `tools/eval_intents.py` | Curated golden examples with expected intents for accuracy/latency measurement. |
| `data/dataset_consolidation.jsonl` | `tools/gen_consolidation_dataset.py` | Town Consciousness distillation set: consolidation prompt → canonical adaptations JSON, teacher-generated (NVIDIA NIM). |
| `data/dataset_consolidation_progress.json` | `tools/gen_consolidation_dataset.py` | Live progress snapshot consumed by `tools/monitor_dataset_gen.py` (web UI on :8138). |

## Common `Intent` shape

Every record carries the canonical intent produced by the parser:

```json
{ "action": "move", "parameters": { "target": "north" } }
```

`action` is a verb from the game's command surface. `parameters` is a JSON
object of slot values (may be empty). Unknown/ambiguous input is represented
with `"action": "unknown"`.

## 1. Command log (`data/cmdlog.jsonl`)

One JSON object per line, one per `/cmd` HTTP call. The full set of fields:

| Field | Type | Description |
|-------|------|-------------|
| `ts` | int | Millisecond monotonic timestamp (host clock). |
| `player_id` | int | Player that issued the command. |
| `day` | int | World day index. |
| `season` | string | `Spring` / `Summer` / `Autumn` / `Winter`. |
| `hour` | int | `hour_of_day()` (6..26, 26 == 2:00 AM). |
| `raw` | string | The verbatim command text sent by the player. |
| `intent` | object | `{action, parameters}` — the intent that was produced. |
| `tier` | string | `rule` (Tier 0 fast path), `llm` (Tier 1 fallback), or `none` (unparsed). |
| `latency_ms` | int | Wall-clock time for intent parsing only (not response generation). |
| `lines` | array[str] | The response lines returned to the player. |

Example:

```json
{"ts":3471886060,"player_id":1,"day":19,"season":"Spring","hour":9,
 "raw":"look","intent":{"action":"look","parameters":{}},"tier":"rule",
 "latency_ms":2,"lines":["You stand on grass in Ashgrove Farm."]}
```

This log is the ground truth for how players actually phrase commands, and the
seed material the dataset generator expands into paraphrases.

## 2. Training dataset (`data/dataset.jsonl`, `data/dataset_expanded.jsonl`)

Produced in two stages:

1. `tools/build_seed_dataset.py` writes `data/dataset.jsonl` — the canonical
   seed: every action × slot value × alias, with `source: "seed"`. Generated
   deterministically from the game's own command surface, so the intents are
   guaranteed correct.
2. `tools/gen_dataset.py` reads the seed and calls a cloud teacher
   (`ASHGROVE_MODEL`) to emit `N` natural-language paraphrases per seed,
   inheriting the seed's canonical intent. Writes `data/dataset_expanded.jsonl`
   (seed rows kept, paraphrases added with `source: "paraphrase"`).

One JSON object per line:

| Field | Type | Description |
|-------|------|-------------|
| `text` | string | The player utterance (raw command, alias, or paraphrase). |
| `intent` | object | Canonical `{action, parameters}` target the model must produce. |
| `source` | string | `seed` (from code) or `paraphrase` (teacher-generated). |

This is the supervised fine-tuning corpus: `text` → serialize(`intent`).

## 3. Evaluation set (`data/eval_set.jsonl`)

Curated golden examples with expected intents, used by `tools/eval_intents.py`
to measure Tier 0/Tier 1 accuracy and latency. Fields:

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Unique name for the case. |
| `text` | string | Player utterance. |
| `expected_action` | string | Correct `action`. |
| `expected_params` | object | Correct `parameters` (exact slot match). |
| `note` | string | Optional description of what the case exercises. |

The eval harness compares parsed `action` + `parameters` against the expected
values and reports per-tier accuracy, p50/p95 latency, and a confusion matrix.

## 4. Town Consciousness dataset (`data/dataset_consolidation.jsonl`)

Produced by `tools/gen_consolidation_dataset.py` using a cloud teacher
(NVIDIA NIM `nvidia/nemotron-3.5-lightning-30b-a3b`, OpenAI-compatible API).
The 0.5B student was originally trained ONLY on intent parsing; when given a
Town Consciousness consolidation prompt it emits intent-shaped JSON
(`{"status":"new",...}`), so adaptations never change. This corpus teaches the
consolidation format so one LoRA can do both tasks.

Input is a synthetic scenario rendered in exactly the same format as
`TownConsciousness::build_consolidation_prompt()`:
`You are the Town Consciousness of Ashgrove Valley...` + `=== MEMORY ===` +
`=== CURRENT ADAPTATIONS ===` (six sections) + `=== EVENTS (last 24h) ===`
(actual events, not just a count) + `=== TASK ===`.

The teacher PROPOSES the next day's adaptation values (pre-damping). Each row is
schema-validated against the `Adaptations` struct (key names, types, numeric
ranges, e.g. `storm_chance` ∈ [0, 0.5]) and clamped before being written.

One JSON object per line:

| Field | Type | Description |
|-------|------|-------------|
| `task` | string | Always `"consolidation"` (drives per-row template in `train_lora.py`). |
| `input` | string | Full consolidation prompt (identical to the game's runtime prompt). |
| `output` | string | Serialized canonical adaptations JSON (`procgen`/`npc`/`economy`/`weather`/`horror`/`performance`). |
| `source` | string | `teacher`. |
| `day` | int | World day of the scenario. |
| `season` | string | `Spring`/`Summer`/`Autumn`/`Winter`. |
| `seed` | int | Deterministic scenario seed (resume key). |
| `teacher` | string | Teacher model id. |
| `validated` | bool | Schema validation passed. |

### Additive training (concatenation, never replacement)

`tools/train_lora.py` reads this file and **appends** the rows onto the existing
intent train/val splits (`/tmp/opencode/train.json` / `val.json`). Each row
carries its own `task` marker, so `build_prompt` picks the consolidation
template (`{input}\n\n{output}`) per row while intent rows keep the
instruction template. The model retains intent knowledge (those rows are still
present) while learning the consolidation task. Run with `--task mixed`
(default) to enable the merge.
