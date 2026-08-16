#!/usr/bin/env python3
"""Phase 7: generate Town Consciousness consolidation training rows via a cloud teacher.

The local 0.5B student was trained ONLY on intent parsing. Given a Town Consciousness
consolidation prompt it emits intent-shaped JSON ({"status":"new","parameters":{...}})
which parse_llm_response cannot map to procgen/npc/economy/weather/horror/performance,
so adaptations stay at defaults. This script generates a supervised corpus of
(consolidation prompt -> canonical adaptations JSON) pairs so the student can be
retrained for both tasks.

Teacher: NVIDIA NIM (nemotron-3.5-lightning-30b-a3b), OpenAI-compatible API.
The teacher receives a scenario (day/season/memory/current adaptations/events) and
produces the PROPOSED adaptations JSON (pre-damping). We keep the prompt format
identical to TownConsciousness::build_consolidation_prompt().

Output rows (data/dataset_consolidation.jsonl), one JSON per line:
    {"input": "<full consolidation prompt>", "output": "<canonical adaptations JSON>",
     "source": "teacher", "day": N, "season": "Spring", "teacher": "<model>", "validated": true}

Config via environment variables (never commit secrets):
    NVIDIA_API_KEY       - NVIDIA NIM API key (required)
    ASHGROVE_BASE_URL    - default https://integrate.api.nvidia.com/v1
    ASHGROVE_TEACHER     - default nvidia/nemotron-3.5-lightning-30b-a3b
    ASHGROVE_OUT         - output path, default data/dataset_consolidation.jsonl
    ASHGROVE_SCENARIOS   - number of scenarios to generate (default 200)
    ASHGROVE_RPM         - requests per minute cap (default 30, NIM limit; keep <= 40)
    ASHGROVE_THINKING    - enable chain-of-thought for teacher (default 0; the model leaks
                           reasoning text into content when enabled, so keep it off)
    ASHGROVE_SEEK        - number of retries per scenario on schema-invalid output (default 3)

Run:
    NVIDIA_API_KEY=... python3 tools/gen_consolidation_dataset.py

Progress is written to data/dataset_consolidation_progress.json (for monitor_dataset_gen.py).
Resumable: rows already present in the output are skipped.
"""
import json
import os
import random
import sys
import time
import urllib.request
import urllib.error

random.seed(42)

# ---------------------------------------------------------------------------
# Adaptations schema (must match Adaptations struct in include/town_consciousness.hpp)
# Each top-level key maps to allowed value types and numeric ranges.
# ---------------------------------------------------------------------------
SECTION_TYPES = {
    "procgen": {
        "biome_preference": "object",
        "ruin_density": ("number", 0.0, 2.0),
        "resource_richness": ("number", 0.0, 2.0),
        "chunk_complexity": ("number", 0.0, 2.0),
    },
    "npc": {
        "personality_drift": "object",
        "schedule_bias": "object",
        "dialogue_topic_weight": "object",
        "gift_preference_shift": "object",
    },
    "economy": {
        "demand_shift": "object",
        "price_elasticity": ("number", 0.5, 2.0),
        "shop_price_mod": "object",
        "market_volatility": ("number", 0.0, 1.0),
    },
    "weather": {
        "pressure_bias": ("number", -1.0, 1.0),
        "humidity_drift": ("number", -1.0, 1.0),
        "storm_chance": ("number", 0.0, 0.5),
        "fog_intensity": ("number", 0.0, 1.0),
        "temperature_bias": ("number", -5.0, 5.0),
        "seasonal_anomaly": ("number", -1.0, 1.0),
    },
    "horror": {
        "intensity": ("number", 0.0, 1.0),
        "basement_unlock_progress": ("number", 0.0, 1.0),
        "night_event_weight": ("number", 0.0, 2.0),
        "sanity_drain_multiplier": ("number", 0.5, 2.0),
        "phantom_sighting_chance": ("number", 0.0, 0.5),
        "active_threat": "string",
    },
    "performance": {
        "thread_pool": "object",
        "tick_budget_ms": ("number", 4, 100),
        "chunk_load_radius": ("number", 1, 8),
        "npc_decision_interval_ticks": ("number", 1, 50),
        "weather_update_interval_ticks": ("number", 1, 100),
        "save_compression": "string",
        "llm_inference_interval_ticks": ("number", 1, 100),
    },
}
SEASONS = ["Spring", "Summer", "Autumn", "Winter"]


def clamp_num(v, lo, hi):
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    if f != f:  # NaN
        return None
    return max(lo, min(hi, f))


def validate_adaptations(j):
    """Return (ok, errors). j must be a dict with the six top-level sections;
    every key must exist in the schema with the right type/range."""
    errors = []
    if not isinstance(j, dict):
        return False, ["not an object"]
    for section, fields in SECTION_TYPES.items():
        if section not in j or not isinstance(j[section], dict):
            errors.append("%s missing or not object" % section)
            continue
        for key, spec in fields.items():
            if key not in j[section]:
                errors.append("%s.%s missing" % (section, key))
                continue
            val = j[section][key]
            if spec == "object":
                if not isinstance(val, dict):
                    errors.append("%s.%s not object" % (section, key))
            elif spec == "string":
                if not isinstance(val, str):
                    errors.append("%s.%s not string" % (section, key))
            else:  # (type, lo, hi)
                if isinstance(val, (dict, list)):
                    errors.append("%s.%s is container" % (section, key))
                else:
                    c = clamp_num(val, spec[1], spec[2])
                    if c is None:
                        errors.append("%s.%s bad number %r" % (section, key, val))
                    else:
                        j[section][key] = c
    return len(errors) == 0, errors


# ---------------------------------------------------------------------------
# Scenario builders: produce varied consolidation prompts.
# ---------------------------------------------------------------------------
def default_adaptations():
    return {
        "procgen": {"biome_preference": {}, "ruin_density": 1.0,
                    "resource_richness": 1.0, "chunk_complexity": 1.0},
        "npc": {"personality_drift": {}, "schedule_bias": {},
                "dialogue_topic_weight": {}, "gift_preference_shift": {}},
        "economy": {"demand_shift": {}, "price_elasticity": 1.0,
                    "shop_price_mod": {}, "market_volatility": 0.0},
        "weather": {"pressure_bias": 0.0, "humidity_drift": 0.0,
                    "storm_chance": 0.01, "fog_intensity": 0.0,
                    "temperature_bias": 0.0, "seasonal_anomaly": 0.0},
        "horror": {"intensity": 0.0, "basement_unlock_progress": 0.0,
                   "night_event_weight": 1.0, "sanity_drain_multiplier": 1.0,
                   "phantom_sighting_chance": 0.0, "active_threat": ""},
        "performance": {"thread_pool": {"world_gen": 2, "npc_ai": 2,
                                        "weather": 1, "io": 1},
                        "tick_budget_ms": 16, "chunk_load_radius": 3,
                        "npc_decision_interval_ticks": 5,
                        "weather_update_interval_ticks": 20,
                        "save_compression": "zstd:3",
                        "llm_inference_interval_ticks": 10},
    }


def random_events(rng, day):
    """Generate a plausible batch of gameplay events for the scenario."""
    types = []
    n = rng.randint(3, 14)
    for _ in range(n):
        r = rng.random()
        if r < 0.35:
            types.append(("player", "player_cmd",
                          {"action": rng.choice(
                              ["buy", "chop", "move", "talk", "craft", "explore"]),
                           "raw": "player command"}))
        elif r < 0.5:
            types.append(("weather", rng.choice(["state", "storm", "rain"]),
                          {"weather": rng.randint(0, 2),
                           "hour": rng.randint(6, 28),
                           "day": day}))
        elif r < 0.6:
            types.append(("npc", "npc_talk", {"npc": rng.choice(
                ["Mayor", "Blacksmith", "Witch", "Farmer", "Trader"]),
                "topic": rng.choice(["gossip", "trade", "quest", "weather"])}))
        elif r < 0.72:
            types.append(("economy", "buy",
                          {"item": rng.choice(["bread", "axe", "seed", "herb", "iron"]),
                           "cost": round(rng.uniform(2, 30), 2)}))
        elif r < 0.82:
            types.append(("crop", "crop_harvest",
                          {"crop": rng.choice(["wheat", "potato", "carrot", "corn"]),
                           "amount": rng.randint(1, 12)}))
        elif r < 0.9:
            types.append(("horror", "phantom_sighting",
                          {"intensity": round(rng.uniform(0.1, 0.6), 2)}))
        else:
            types.append(("building", "decay",
                          {"structure": rng.choice(["barn", "fence", "well", "house"]),
                           "damage": rng.randint(1, 5)}))
    events = []
    for i, (sys, et, payload) in enumerate(types):
        events.append({"system": sys, "event_type": et, "day": max(1, day - i % 3),
                       "payload": payload})
    return events


def render_prompt(day, season, memory, adaptations, events, rng=None):
    """Render a consolidation prompt identical to build_consolidation_prompt()."""
    lines = []
    lines.append("You are the Town Consciousness of Ashgrove Valley. "
                 "Output ONLY valid JSON with updated adaptations.")
    lines.append("Day: %d | Season: %s" % (day, season))
    lines.append("Consolidation #%d\n" % (rng or random).randint(1, 30))
    lines.append("=== MEMORY ===")
    for key in ["player_habits", "npc_relationships", "economic_trends",
                "ecological_state", "discovered_secrets", "performance_profile",
                "narrative_state"]:
        if memory.get(key):
            lines.append("%s: %s" % (key, json.dumps(memory[key])))
    lines.append("")
    lines.append("=== CURRENT ADAPTATIONS ===")
    for key in ["procgen", "npc", "economy", "weather", "horror", "performance"]:
        lines.append("%s: %s" % (key, json.dumps(adaptations[key])))
    lines.append("")
    lines.append("=== EVENTS (last 24h) ===")
    lines.append("events: %d entries" % len(events))
    for e in events[:50]:
        lines.append("  - [%s:%s] day=%s payload=%s" % (
            e["system"], e["event_type"], e["day"], json.dumps(e["payload"])))
    lines.append("")
    lines.append("=== TASK ===")
    lines.append("Output ONLY valid JSON with keys: procgen, npc, economy, "
                 "weather, horror, performance.")
    lines.append("Each key contains an object with adaptation values (numbers).")
    lines.append("Apply damping: new = 0.3 * proposed + 0.7 * old.")
    lines.append("Output ONLY the JSON object, no extra text.")
    return "\n".join(lines) + "\n"


def build_scenario(seed):
    """Deterministic per-seed scenario variation for resumability."""
    rng = random.Random(seed)
    day = rng.randint(1, 60)
    season = SEASONS[(day // 15) % 4]
    adaptations = default_adaptations()
    # Introduce drift into current adaptations so the model sees non-default state.
    a = rng.random()
    if a > 0.4:
        adaptations["weather"]["pressure_bias"] = round(rng.uniform(-0.6, 0.6), 2)
        adaptations["weather"]["storm_chance"] = round(max(0.0, rng.uniform(0.005, 0.3)), 3)
        adaptations["weather"]["temperature_bias"] = round(rng.uniform(-3, 3), 1)
    if a > 0.6:
        adaptations["horror"]["intensity"] = round(rng.uniform(0.1, 0.9), 2)
        adaptations["horror"]["phantom_sighting_chance"] = round(rng.uniform(0.05, 0.4), 3)
    if a > 0.75:
        adaptations["economy"]["market_volatility"] = round(rng.uniform(0.2, 0.9), 2)
        adaptations["economy"]["price_elasticity"] = round(rng.uniform(0.6, 1.8), 2)
    if a > 0.85:
        adaptations["procgen"]["ruin_density"] = round(rng.uniform(0.3, 1.8), 2)
    memory = {}
    if rng.random() > 0.3:
        memory["player_habits"] = {"cmd_frequency": {
            "buy": rng.randint(0, 6), "chop": rng.randint(0, 6),
            "explore": rng.randint(0, 4), "talk": rng.randint(0, 5)},
            "day_span": rng.randint(1, day)}
    if rng.random() > 0.5:
        memory["npc_relationships"] = {
            npc: {"interactions": rng.randint(0, 8),
                  "hearts": round(rng.uniform(0, 1), 2)}
            for npc in rng.sample(["Mayor", "Blacksmith", "Witch", "Farmer"],
                                     rng.randint(1, 3))}
    if rng.random() > 0.5:
        memory["economic_trends"] = {
            "buy_history": {item: round(rng.uniform(0, 120), 1)
                            for item in rng.sample(["bread", "seed", "iron", "herb"],
                                                      rng.randint(1, 3))}}
    if rng.random() > 0.5:
        memory["performance_profile"] = {
            "weather_events": rng.randint(0, 25),
            "horror_events": rng.randint(0, 4),
            "last_consolidation_day": day - rng.randint(1, 3)}
    events = random_events(rng, day)
    prompt = render_prompt(day, season, memory, adaptations, events, rng)
    return {"day": day, "season": season, "prompt": prompt,
            "memory": memory, "adaptations": adaptations, "events": events}


# ---------------------------------------------------------------------------
# Teacher call (rate-limited).
# ---------------------------------------------------------------------------
class RateLimiter:
    def __init__(self, rpm):
        self.min_interval = 60.0 / rpm
        self.last = 0.0

    def wait(self):
        now = time.monotonic()
        elapsed = now - self.last
        if elapsed < self.min_interval:
            time.sleep(self.min_interval - elapsed)
        self.last = time.monotonic()


def teacher_generate(base, api_key, model, prompt, thinking, max_retries=5):
    url = base.rstrip("/") + "/chat/completions"
    body = {
        "model": model,
        "messages": [
            {"role": "system", "content": (
                "You are the Town Consciousness of Ashgrove Valley, a persistent "
                "world-simulation entity. Read the gameplay events. PROPOSE (do NOT damp) "
                "the next day's adaptation values. Respond with ONLY one JSON object with "
                "exactly the top-level keys: procgen, npc, economy, weather, horror, "
                "performance. Each key is an object with the same nested keys as in "
                "CURRENT ADAPTATIONS. Change values where the events justify it (weather "
                "after storms, horror after sightings, economy after buys). Keep objects "
                "that have no justification unchanged. No markdown fences, no explanation, "
                "no reasoning text, just the JSON.")},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.3,
        "top_p": 0.9,
        "max_tokens": 2048,
        "chat_template_kwargs": {"enable_thinking": bool(thinking)},
        "reasoning_budget": 4096 if thinking else 0,
    }
    req = urllib.request.Request(
        url, data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json",
                 "Authorization": "Bearer " + api_key},
        method="POST")
    for attempt in range(max_retries):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                data = json.load(r)
            return data["choices"][0]["message"]["content"]
        except urllib.error.HTTPError as e:
            if e.code == 429 and attempt < max_retries - 1:
                retry = e.headers.get("Retry-After") if e.headers else None
                wait = float(retry) if retry and retry.isdigit() else 30.0
                print("  (429, backing off %.0fs)" % wait, flush=True)
                time.sleep(wait)
                continue
            raise
    raise RuntimeError("exhausted retries")


def extract_json(text):
    start = text.find("{")
    if start == -1:
        return None
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    api_key = os.environ.get("NVIDIA_API_KEY", "")
    base = os.environ.get("ASHGROVE_BASE_URL", "https://integrate.api.nvidia.com/v1")
    model = os.environ.get("ASHGROVE_TEACHER",
                           "nvidia/nemotron-3.5-lightning-30b-a3b")
    out_path = os.environ.get("ASHGROVE_OUT", "data/dataset_consolidation.jsonl")
    n_scenarios = int(os.environ.get("ASHGROVE_SCENARIOS", "200"))
    rpm = int(os.environ.get("ASHGROVE_RPM", "30"))
    thinking = os.environ.get("ASHGROVE_THINKING", "0") not in ("0", "", "false", "no")
    seek = int(os.environ.get("ASHGROVE_SEEK", "3"))
    progress_path = os.environ.get(
        "ASHGROVE_PROGRESS", "data/dataset_consolidation_progress.json")

    if not api_key:
        print("Set NVIDIA_API_KEY and run again.")
        return 1

    # Resumable: remember seeds already written.
    done = set()
    if os.path.exists(out_path):
        with open(out_path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    done.add(json.loads(line).get("seed"))
                except json.JSONDecodeError:
                    pass

    def write_progress(extra=None):
        d = {"ts": int(time.time()), "start_ts": t0, "port": None}
        if extra:
            d.update(extra)
        with open(progress_path, "w") as f:
            json.dump(d, f)

    limiter = RateLimiter(rpm)
    stats = {"ok": len(done), "invalid": 0, "failed": 0, "requests": 0}
    t0 = time.time()

    with open(out_path, "a") as out:
        for seed in range(1, n_scenarios + 1):
            if seed in done:
                continue
            scenario = build_scenario(seed)
            row = None
            for attempt in range(1, seek + 1):
                limiter.wait()
                try:
                    text = teacher_generate(base, api_key, model,
                                            scenario["prompt"], thinking)
                    stats["requests"] += 1
                except Exception as e:
                    print("\n  FAILED seed=%d attempt=%d: %s" % (seed, attempt, e),
                          flush=True)
                    stats["failed"] += 1
                    write_progress({"seed": seed, "n_scenarios": n_scenarios,
                                    "status": "error", "msg": str(e),
                                    **stats})
                    break
                js = extract_json(text)
                if js is None:
                    stats["invalid"] += 1
                    print("\n  seed=%d no JSON in response (attempt %d)" % (seed, attempt),
                          flush=True)
                    continue
                try:
                    parsed = json.loads(js)
                except json.JSONDecodeError as e:
                    stats["invalid"] += 1
                    print("\n  seed=%d unparseable JSON (attempt %d)" % (seed, attempt),
                          flush=True)
                    continue
                ok, errors = validate_adaptations(parsed)
                if ok:
                    row = {"task": "consolidation",
                           "input": scenario["prompt"],
                           "output": json.dumps(parsed),
                           "source": "teacher", "day": scenario["day"],
                           "season": scenario["season"], "seed": seed,
                           "teacher": model, "validated": True}
                    break
                stats["invalid"] += 1
                print("\n  seed=%d schema-invalid (attempt %d): %s"
                      % (seed, attempt, ", ".join(errors[:5])), flush=True)
                # Feed the errors back so the model can self-correct on retry.
                scenario["prompt"] += ("\n[Validation] Your previous output was invalid: %s. "
                                       "Return corrected JSON only.\n" % ", ".join(errors[:8]))

            if row:
                out.write(json.dumps(row) + "\n")
                out.flush()
                stats["ok"] += 1
                done.add(seed)
            else:
                stats["failed"] += 1

            elapsed = time.time() - t0
            done_count = len(done)
            pct = 100.0 * done_count / n_scenarios
            if stats["requests"]:
                rps = stats["requests"] / elapsed
                eta = (n_scenarios - done_count) / rps if rps > 0 else -1
            else:
                eta = -1
            sys.stdout.write(
                "\r[%5.1f%%] done=%d invalid=%d failed=%d req=%d rpm=%.1f eta=%s   "
                % (pct, done_count, stats["invalid"], stats["failed"],
                   stats["requests"], rps * 60,
                   "%dh%02dm" % (eta // 3600, (eta % 3600) // 60) if eta > 0 else "?"))
            sys.stdout.flush()
            write_progress({"seed": seed, "n_scenarios": n_scenarios,
                            "done": done_count, "status": "running", **stats})

    write_progress({"seed": n_scenarios, "n_scenarios": n_scenarios,
                    "done": len(done), "status": "complete", **stats})
    print("\nwrote %d rows to %s" % (len(done), out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())