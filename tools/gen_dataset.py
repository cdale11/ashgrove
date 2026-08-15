#!/usr/bin/env python3
"""Phase 8 (B): expand the canonical seed dataset into a paraphrase corpus.

The teacher is a big, intelligent CLOUD model (OpenAI-compatible Chat
Completions endpoint). It reads the canonical seed rows produced by
`build_seed_dataset.py` (data/dataset.jsonl) and, for each seed, generates
`N` natural-language paraphrases. Each paraphrase inherits the seed's
canonical {action, parameters}, so the resulting rows are (utterance ->
intent) pairs ready to fine-tune the 0.5B student.

This script is NOT run by CI. It requires a cloud API key and costs tokens.
Config via environment variables (never commit secrets):
    ASHGROVE_API_KEY     - cloud API key (required)
    ASHGROVE_BASE_URL    - base URL, default https://api.openai.com/v1
    ASHGROVE_MODEL       - teacher model id, default gpt-4o-mini
    ASHGROVE_SEED        - canonical seed input (default data/dataset.jsonl)
    ASHGROVE_OUT         - output dataset path (default data/dataset_expanded.jsonl)
    ASHGROVE_PER_COMMAND - paraphrases to generate per seed (default 4)
"""
import json
import os
import sys
import urllib.request


def load_seeds(path):
    """Read canonical seed rows: {text, intent, source}."""
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return rows


def teacher_generate(base, api_key, model, prompt):
    url = base.rstrip("/") + "/chat/completions"
    body = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": (
                    "You are helping build a command-to-intent training set for "
                    "a farming MUD. You will receive a player command and its "
                    "canonical intent. Produce only natural paraphrase "
                    "utterances a player might type, keeping the exact same "
                    "meaning. One per line, plain text, no numbering, no "
                    "explanation."
                ),
            },
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.8,
    }
    req = urllib.request.Request(
        url,
        data=json.dumps(body).encode(),
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + api_key,
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=120) as r:
        data = json.load(r)
    return data["choices"][0]["message"]["content"]


def main():
    seed_path = os.environ.get("ASHGROVE_SEED", "data/dataset.jsonl")
    out_path = os.environ.get("ASHGROVE_OUT", "data/dataset_expanded.jsonl")
    base = os.environ.get("ASHGROVE_BASE_URL", "https://api.openai.com/v1")
    model = os.environ.get("ASHGROVE_MODEL", "gpt-4o-mini")
    per = int(os.environ.get("ASHGROVE_PER_COMMAND", "4"))
    api_key = os.environ.get("ASHGROVE_API_KEY", "")

    if not api_key:
        print("Set ASHGROVE_API_KEY (and optional ASHGROVE_BASE_URL/MODEL).")
        print("This script is intentionally not run by CI; configure + run manually.")
        return 1

    seeds = load_seeds(seed_path)
    print(f"loaded {len(seeds)} canonical seeds from {seed_path}")

    n_out = 0
    skipped = 0
    with open(out_path, "w") as out:
        for seed in seeds:
            text = seed.get("text", "")
            intent = seed.get("intent", {})
            prompt = (
                f"Command: \"{text}\"\n"
                f"Canonical intent: {json.dumps(intent)}\n"
                f"Write {per} distinct paraphrase utterances a player might "
                f"type that mean exactly this. One per line, no numbering."
            )
            try:
                paraphrases = teacher_generate(base, api_key, model, prompt)
            except Exception as e:
                print(f"  teacher call failed for {text!r}: {e}")
                continue
            for line in paraphrases.strip().splitlines():
                line = line.strip()
                if not line or line == text:
                    continue
                out.write(json.dumps(
                    {"text": line, "intent": intent, "source": "paraphrase"}
                ) + "\n")
                n_out += 1
            # Always keep the canonical seed itself in the corpus.
            out.write(json.dumps(
                {"text": text, "intent": intent, "source": "seed"}
            ) + "\n")
            skipped += 1
    print(f"wrote {n_out} paraphrase rows (+{skipped} seed rows) to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
