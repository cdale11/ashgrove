#!/usr/bin/env python3
"""Phase 8 (B): expand the canonical seed dataset into a paraphrase corpus.

The teacher is a big, intelligent CLOUD model (OpenAI-compatible Chat
Completions endpoint). It reads the canonical seed rows produced by
`build_seed_dataset.py` (data/dataset.jsonl) and, for each seed, generates
`N` natural-language paraphrases. Each paraphrase inherits the seed's
canonical {action, parameters}, so the resulting rows are (utterance ->
intent) pairs ready to fine-tune the 0.5B student.

This script is NOT run by CI. It requires a cloud API key and costs tokens.
Defaults target NVIDIA NIM (nemotron-3-ultra-550b-a55b as the teacher).
Config via environment variables (never commit secrets):
    ASHGROVE_API_KEY     - cloud API key (required)
    ASHGROVE_BASE_URL    - base URL, default https://integrate.api.nvidia.com/v1
    ASHGROVE_MODEL       - teacher model id, default nvidia/nemotron-3-ultra-550b-a55b
    ASHGROVE_SEED        - canonical seed input (default data/dataset.jsonl)
    ASHGROVE_OUT         - output dataset path (default data/dataset_expanded.jsonl)
    ASHGROVE_PER_COMMAND - paraphrases to generate per seed (default 4)
    ASHGROVE_THINKING    - enable chain-of-thought for teacher (default 0)
"""
import json
import os
import sys
import time
import urllib.request
import urllib.error


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


def load_done_texts(path):
    """Seed texts already present in the output (for resuming an interrupted run)."""
    done = set()
    if not os.path.exists(path):
        return done
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                done.add(json.loads(line).get("text"))
            except json.JSONDecodeError:
                pass
    return done


class RateLimiter:
    """Ensure at most `rpm` requests per minute (adds 2.5s/call at 30 RPM)."""
    def __init__(self, rpm):
        self.min_interval = 60.0 / rpm
        self.last = 0.0

    def wait(self):
        now = time.monotonic()
        elapsed = now - self.last
        if elapsed < self.min_interval:
            time.sleep(self.min_interval - elapsed)
        self.last = time.monotonic()


def teacher_generate(base, api_key, model, prompt, thinking=False, max_retries=4):
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
        # NVIDIA NIM reasoning models take chat_template_kwargs to control
        # chain-of-thought. Disabled by default: paraphrasing is direct, and
        # CoT would multiply token cost across thousands of seed rows.
        "chat_template_kwargs": {"enable_thinking": thinking},
        "reasoning_budget": 4096 if thinking else 0,
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
    for attempt in range(max_retries):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                data = json.load(r)
            return data["choices"][0]["message"]["content"]
        except urllib.error.HTTPError as e:
            if e.code == 429 and attempt < max_retries - 1:
                # Rate limited: back off (honor Retry-After if present, else 30s).
                retry = e.headers.get("Retry-After") if e.headers else None
                wait = float(retry) if retry and retry.isdigit() else 30.0
                print(f"  (429 rate limit, backing off {wait:.0f}s)", flush=True)
                time.sleep(wait)
                continue
            raise
    raise RuntimeError("exhausted retries")


def main():
    seed_path = os.environ.get("ASHGROVE_SEED", "data/dataset.jsonl")
    out_path = os.environ.get("ASHGROVE_OUT", "data/dataset_expanded.jsonl")
    base = os.environ.get("ASHGROVE_BASE_URL", "https://integrate.api.nvidia.com/v1")
    model = os.environ.get("ASHGROVE_MODEL", "nvidia/nemotron-3.5-lightning-30b-a3b")
    per = int(os.environ.get("ASHGROVE_PER_COMMAND", "4"))
    rpm = int(os.environ.get("ASHGROVE_RPM", "30"))   # NIM default cap is 40; stay at 30
    thinking = os.environ.get("ASHGROVE_THINKING", "0") not in ("0", "", "false", "no")
    api_key = os.environ.get("ASHGROVE_API_KEY", "")

    if not api_key:
        print("Set ASHGROVE_API_KEY (and optional ASHGROVE_BASE_URL/MODEL).")
        print("This script is intentionally not run by CI; configure + run manually.")
        return 1

    seeds = load_seeds(seed_path)
    done = load_done_texts(out_path)
    todo = [s for s in seeds if s.get("text") not in done]
    print(f"loaded {len(seeds)} canonical seeds; {len(done)} already done, "
          f"{len(todo)} to process", flush=True)

    limiter = RateLimiter(rpm)
    seen = set(done)
    n_out = 0
    skipped = 0
    failures = 0
    total = len(todo)
    with open(out_path, "a") as out:
        for idx, seed in enumerate(todo, 1):
            text = seed.get("text", "")
            intent = seed.get("intent", {})
            # skip if another seed already contributed an identical utterance
            if text in seen:
                skipped += 1
                continue
            prompt = (
                f"Command: \"{text}\"\n"
                f"Canonical intent: {json.dumps(intent)}\n"
                f"Write {per} distinct paraphrase utterances a player might "
                f"type that mean exactly this. One per line, no numbering."
            )
            limiter.wait()
            try:
                paraphrases = teacher_generate(base, api_key, model, prompt, thinking)
            except Exception as e:
                print(f"\n  FAILED {text!r}: {e}", flush=True)
                failures += 1
                continue
            for line in paraphrases.strip().splitlines():
                line = line.strip()
                # Drop empty lines, the exact seed echo, and stray junk tokens
                # that reasoning models occasionally emit (":q", "---", etc.).
                if (not line or line == text or line.startswith(":") or
                        line in ("---", "```", "Done", "Here are", "Here's")):
                    continue
                if line in seen:
                    continue
                seen.add(line)
                out.write(json.dumps(
                    {"text": line, "intent": intent, "source": "paraphrase"}
                ) + "\n")
                out.flush()
                n_out += 1
            # Always keep the canonical seed itself in the corpus.
            if text not in seen:
                out.write(json.dumps(
                    {"text": text, "intent": intent, "source": "seed"}
                ) + "\n")
                out.flush()
                seen.add(text)
            # in-place progress bar
            pct = idx / total * 100
            bar = int(30 * idx / total)
            sys.stdout.write(
                f"\r[{('#'*bar).ljust(30)}] {idx}/{total} ({pct:.0f}%) "
                f"paraphrases={n_out} fails={failures}")
            sys.stdout.flush()
    print(f"\nwrote {n_out} paraphrase rows; {failures} failures; output: {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
