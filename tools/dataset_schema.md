# Ashgrove Command Dataset Schema

Defines the on-disk formats used by the Phase 8 (Model Distillation) pipeline.
Three related files are produced/consumed:

| File | Producer | Purpose |
|------|----------|---------|
| `data/cmdlog.jsonl` | Server (live gameplay) | Raw, ever-growing record of real commands + intents + responses. |
| `data/dataset.jsonl` | `tools/gen_dataset.py` | Distillation training set: paraphrases → canonical `{action, parameters}`. |
| `data/eval_set.jsonl` | `tools/eval_intents.py` | Curated golden examples with expected intents for accuracy/latency measurement. |

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

## 2. Training dataset (`data/dataset.jsonl`)

Produced offline by `tools/gen_dataset.py` using a cloud LLM as the teacher.
One JSON object per line:

| Field | Type | Description |
|-------|------|-------------|
| `text` | string | The player utterance (raw command or paraphrase). |
| `intent` | object | Canonical `{action, parameters}` target the model must produce. |
| `source` | string | `paraphrase` (teacher-generated) or `live` (from cmdlog). |

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
