#!/usr/bin/env python3
"""Build intent-only train/val splits in the schema train_lora.py expects.

Schema per row: {"instruction": "...", "input": "<raw command>", "output": "<intent JSON>"}
Output: /tmp/opencode/train_intent.json + /tmp/opencode/val_intent.json (JSON arrays)
"""
import json, os, sys, random

random.seed(42)

DATA_DIR = "/home/umang/ashgrove/data"
OUT_DIR = "/tmp/opencode"
INSTRUCTION = "Classify this command in Ashgrove Valley."

def load(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except Exception:
                pass
    return rows

dataset = load(f"{DATA_DIR}/dataset.jsonl")
expanded = load(f"{DATA_DIR}/dataset_expanded.jsonl")
all_intent = dataset + expanded
print(f"loaded dataset={len(dataset)} expanded={len(expanded)} total={len(all_intent)}", file=sys.stderr)

eval_set = load(f"{DATA_DIR}/eval_set.jsonl")
eval_texts = {r.get("text", "").strip().lower() for r in eval_set}
print(f"eval holdout texts={len(eval_texts)}", file=sys.stderr)

seen = set()
shaped = []
for r in all_intent:
    text = r.get("text", "").strip()
    if not text:
        continue
    if text.lower() in eval_texts:
        continue
    intent = r.get("intent")
    if not intent:
        continue
    key = (text.lower(), json.dumps(intent, sort_keys=True))
    if key in seen:
        continue
    seen.add(key)
    shaped.append({
        "instruction": INSTRUCTION,
        "input": text,
        "output": json.dumps(intent, separators=(",", ":")),
    })

print(f"after dedup+eval-leakage-filter: {len(shaped)}", file=sys.stderr)

random.shuffle(shaped)
split = int(len(shaped) * 0.95)
train, val = shaped[:split], shaped[split:]
print(f"train={len(train)} val={len(val)}", file=sys.stderr)

os.makedirs(OUT_DIR, exist_ok=True)
with open(f"{OUT_DIR}/train_intent.json", "w") as f:
    json.dump(train, f)
with open(f"{OUT_DIR}/val_intent.json", "w") as f:
    json.dump(val, f)

print("OK")
