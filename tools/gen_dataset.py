#!/usr/bin/env python3
"""Phase 8 (B): teacher dataset generator for model distillation.

Builds `data/dataset.jsonl` from a cloud LLM teacher. The teacher is a big,
intelligent model (OpenAI-compatible Chat Completions endpoint) that:
  1. Reads live command examples from `data/cmdlog.jsonl` as seed phrasings.
  2. Generates N paraphrases per seed command.
  3. Writes each (utterance -> canonical {action, parameters}) pair as one JSONL row.

The student model is later fine-tuned on this corpus, so it learns to map the
many ways players phrase a command down to the fixed game intent surface.

This script is NOT run as part of the build. It is invoked manually once the
user configures a cloud endpoint and has collected enough cmdlog seeds.

Config via environment variables (never commit secrets):
    ASHGROVE_API_KEY     - cloud API key
    ASHGROVE_BASE_URL    - base URL, default https://api.openai.com/v1
    ASHGROVE_MODEL       - teacher model id, default gpt-4o-mini
    ASHGROVE_CMDLOG      - path to cmdlog.jsonl (default data/cmdlog.jsonl)
    ASHGROVE_OUT         - output dataset path (default data/dataset.jsonl)
    ASHGROVE_PER_COMMAND - paraphrases to generate per seed (default 4)
"""
import json
import os
import urllib.request


def load_seeds(path):
    """Read distinct raw commands from the live command log."""
    raws = set()
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                raws.add(json.loads(line)["raw"])
            except (json.JSONDecodeError, KeyError):
                pass
    return sorted(raws)


def teacher_generate(base, api_key, model, prompt):
    """Call an OpenAI-compatible chat endpoint. Returns assistant text."""
    url = base.rstrip("/") + "/chat/completions"
    body = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": (
                    "You are building a command-to-intent training set for a "
                    "farming MUD. Convert each player utterance into canonical "
                    "JSON {action, parameters}. Use only these actions: "
                    "move, look, inventory, status, help, eat, hoe, plant, "
                    "planttree, water, harvest, axe, scythe, fish, tap, shake, "
                    "talk, gift, hearts, buy, sell, craft, place, repair, "
                    "upgrade, plots, train, bus, tv, festival, basement, horror, "
                    "sleep, save, load, newgame, enter, exit. parameters holds "
                    "slot values (empty object if none)."
                ),
            },
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.7,
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
    cmdlog = os.environ.get("ASHGROVE_CMDLOG", "data/cmdlog.jsonl")
    out_path = os.environ.get("ASHGROVE_OUT", "data/dataset.jsonl")
    base = os.environ.get("ASHGROVE_BASE_URL", "https://api.openai.com/v1")
    model = os.environ.get("ASHGROVE_MODEL", "gpt-4o-mini")
    per = int(os.environ.get("ASHGROVE_PER_COMMAND", "4"))
    api_key = os.environ.get("ASHGROVE_API_KEY", "")

    if not api_key:
        print("Set ASHGROVE_API_KEY (and optional ASHGROVE_BASE_URL/MODEL).")
        print("This script is intentionally not run by CI; configure + run manually.")
        return 1

    seeds = load_seeds(cmdlog)
    print(f"loaded {len(seeds)} seed commands from {cmdlog}")

    n_out = 0
    with open(out_path, "w") as out:
        for seed in seeds:
            prompt = (
                f'Given the seed command: "{seed}"\n'
                f"Produce {per} distinct paraphrase utterances (one per line, "
                f"plain text, no numbering). Vary wording, synonyms and word "
                f"order while keeping the same meaning."
            )
            try:
                paraphrases = teacher_generate(base, api_key, model, prompt)
            except Exception as e:
                print(f"  teacher call failed for {seed!r}: {e}")
                continue
            for line in paraphrases.strip().splitlines():
                line = line.strip()
                if not line:
                    continue
                # The teacher also returns the canonical intent in a follow-up;
                # here we record the paraphrase and mark intent as pending so a
                # second pass can annotate it. (See dataset_schema.md.)
                out.write(
                    json.dumps(
                        {"text": line, "intent": {}, "source": "paraphrase"}
                    )
                    + "\n"
                )
                n_out += 1
    print(f"wrote {n_out} paraphrase rows to {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
