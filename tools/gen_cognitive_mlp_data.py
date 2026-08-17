#!/usr/bin/env python3
"""
Generate synthetic training data for cognitive MLPs using NIM teacher.
Falls back to local heuristics if NIM unavailable.

Output: data/mlp_training_data.jsonl + train/val splits.
"""
import json
import os
import random
import time
import urllib.request
import urllib.error

random.seed(42)

# NIM config (same as gen_dataset.py)
NIM_URL = os.environ.get("ASHGROVE_BASE_URL", "https://integrate.api.nvidia.com/v1")
NIM_MODEL = os.environ.get("ASHGROVE_MODEL", "nvidia/nemotron-3.5-lightning-30b-a3b")
NIM_KEY = os.environ.get("ASHGROVE_API_KEY", "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")
NIM_RPM = int(os.environ.get("ASHGROVE_RPM", "30"))

HEADERS = {
    "Authorization": f"Bearer {NIM_KEY}",
    "Content-Type": "application/json",
}

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

def call_nim(prompt: str, max_tokens: int = 128, temp: float = 0.1, max_retries: int = 3) -> str:
    """Call NIM API with retry logic. Returns raw content. Disables thinking for pure JSON output."""
    payload = {
        "model": NIM_MODEL,
        "messages": [{"role": "user", "content": prompt}],
        "max_tokens": max_tokens,
        "temperature": temp,
        "stream": False,
        # Disable thinking/reasoning for pure JSON output (Nemotron 3.5 Lightning)
        "chat_template_kwargs": {"enable_thinking": False},
        "reasoning_budget": 0,
    }
    for attempt in range(max_retries):
        try:
            req = urllib.request.Request(
                f"{NIM_URL}/chat/completions",
                data=json.dumps(payload).encode(),
                headers=HEADERS,
                method="POST",
            )
            with urllib.request.urlopen(req, timeout=60) as r:
                data = json.load(r)
            return data["choices"][0]["message"]["content"]
        except urllib.error.HTTPError as e:
            if e.code == 429 and attempt < max_retries - 1:
                retry = e.headers.get("Retry-After") if e.headers else None
                wait = float(retry) if retry and retry.isdigit() else 30.0
                print(f"  (429 rate limit, backing off {wait:.0f}s)", flush=True)
                time.sleep(wait)
                continue
            raise
    raise RuntimeError("exhausted retries")

def extract_json(text: str):
    """Extract JSON from text that may contain reasoning/CoT or code blocks."""
    import re
    # Try code block first
    m = re.search(r'```(?:json)?\s*(\{.*?\})\s*```', text, re.DOTALL)
    if m:
        try:
            return json.loads(m.group(1))
        except json.JSONDecodeError:
            pass
    # Try bare JSON object
    m = re.search(r'(\{.*\})', text, re.DOTALL)
    if m:
        try:
            return json.loads(m.group(1))
        except json.JSONDecodeError:
            pass
    # Last resort: try parsing the whole thing
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None

# --- Local heuristic fallbacks (same as original) ---
def local_attention_gate(novelty, reward, social, survival):
    gate = 0.35 * survival + 0.25 * novelty + 0.25 * reward + 0.15 * social
    return max(0.0, min(1.0, gate + random.uniform(-0.05, 0.05)))

def local_action_scores(hunger, thirst, social, safety, curiosity, fatigue,
                        wmb_weather, wmb_social, wmb_resource, social_pressure):
    scores = [
        0.3 * hunger + 0.8 * thirst + 0.5 * safety + 0.4 * curiosity,  # go
        0.8 * curiosity + 0.3 * hunger,                                 # interact
        social + 0.5 * social_pressure,                                 # talk
        0.6 * safety + 0.2 * curiosity,                                 # repair
        0.7 * hunger + 0.3 * curiosity,                                 # harvest
        fatigue + 0.3 * safety,                                         # rest
    ]
    scores[0] += wmb_weather * 0.1
    scores[1] += wmb_social * 0.1
    scores[4] += wmb_resource * 0.1
    max_s = max(scores) if max(scores) > 0 else 1.0
    return [max(0.0, min(1.0, s / max_s)) for s in scores]

def local_world_model(season, weather, valence, arousal, hunger, social, safety, resource):
    weather_shift = (0.5 - weather/5.0) * 0.5 + random.uniform(-0.2, 0.2)
    social_response = 0.5 + valence * 0.3 + random.uniform(-0.1, 0.1)
    resource_avail = resource * 0.8 + random.uniform(-0.1, 0.1)
    weather_shift = max(-1.0, min(1.0, weather_shift))
    social_response = max(0.0, min(1.0, social_response))
    resource_avail = max(0.0, min(1.0, resource_avail))
    return [weather_shift, social_response, resource_avail]

def write_progress(task: str, completed: int, total: int):
    """Write progress to file for web UI."""
    progress_file = "data/mlp_gen_progress.json"
    data = {}
    if os.path.exists(progress_file):
        with open(progress_file) as f:
            try:
                data = json.load(f)
            except Exception:
                pass
    data[task] = {"completed": completed, "total": total}
    with open(progress_file, "w") as f:
        json.dump(data, f)

# --- NIM-based generation ---
def generate_attention_samples(n: int, limiter: RateLimiter, use_nim: bool) -> list:
    """Attention MLP: 4 features -> 1 gate score."""
    samples = []
    for i in range(n):
        novelty = random.random()
        reward = random.random()
        social = random.random()
        survival = random.random()
        
        if use_nim:
            prompt = (
                "You are an attention gating system for an NPC in Ashgrove Valley.\n"
                "Score the salience of a stimulus (0.0 to 1.0) given:\n"
                f"  novelty={novelty:.3f}, reward_potential={reward:.3f}, "
                f"social_relevance={social:.3f}, survival_urgency={survival:.3f}\n"
                "Higher scores mean the stimulus enters working memory.\n"
                "Output ONLY a JSON object: {\"gate_score\": <float>}"
            )
            try:
                limiter.wait()
                response = call_nim(prompt, max_tokens=64, temp=0.1)
                teacher_data = extract_json(response)
                teacher_gate = teacher_data["gate_score"] if teacher_data else None
            except Exception:
                teacher_gate = None
            if teacher_gate is None:
                teacher_gate = local_attention_gate(novelty, reward, social, survival)
        else:
            teacher_gate = local_attention_gate(novelty, reward, social, survival)
        
        samples.append({
            "task": "attention",
            "inputs": [novelty, reward, social, survival],
            "target": teacher_gate,
        })
        if i % 20 == 0 or i == n - 1:
            write_progress("attention", i + 1, n)
    return samples

def generate_action_evaluator_samples(n: int, limiter: RateLimiter, use_nim: bool) -> list:
    """Action Evaluator: 10 inputs -> 6 action scores."""
    samples = []
    for i in range(n):
        hunger = random.random()
        thirst = random.random()
        social = random.random()
        safety = random.random()
        curiosity = random.random()
        fatigue = random.random()
        wmb_weather = random.uniform(-1, 1)
        wmb_social = random.uniform(-1, 1)
        wmb_resource = random.uniform(-1, 1)
        social_pressure = random.random()
        
        if use_nim:
            prompt = (
                "You are an action evaluator for an NPC in Ashgrove Valley.\n"
                "Given drive urgencies (0=satisfied, 1=desperate) and context, "
                "output action scores (0.0 to 1.0) for: go, interact, talk, repair, harvest, rest.\n"
                f"hunger={hunger:.3f}, thirst={thirst:.3f}, social={social:.3f}, "
                f"safety={safety:.3f}, curiosity={curiosity:.3f}, fatigue={fatigue:.3f}\n"
                f"world_bias: weather={wmb_weather:.3f}, social={wmb_social:.3f}, resource={wmb_resource:.3f}\n"
                f"social_pressure={social_pressure:.3f}\n"
                "Output ONLY JSON: {\"scores\": [<6 floats>]}"
            )
            try:
                limiter.wait()
                response = call_nim(prompt, max_tokens=128, temp=0.1)
                teacher_data = extract_json(response)
                teacher_scores = teacher_data["scores"] if teacher_data else None
            except Exception:
                teacher_scores = None
            if teacher_scores is None:
                teacher_scores = local_action_scores(hunger, thirst, social, safety, curiosity, fatigue,
                                                      wmb_weather, wmb_social, wmb_resource, social_pressure)
        else:
            teacher_scores = local_action_scores(hunger, thirst, social, safety, curiosity, fatigue,
                                                  wmb_weather, wmb_social, wmb_resource, social_pressure)
        
        samples.append({
            "task": "action_evaluator",
            "inputs": [hunger, thirst, social, safety, curiosity, fatigue,
                       wmb_weather, wmb_social, wmb_resource, social_pressure],
            "target": teacher_scores,
        })
        if i % 20 == 0 or i == n - 1:
            write_progress("action_evaluator", i + 1, n)
    return samples

def generate_world_model_samples(n: int, limiter: RateLimiter, use_nim: bool) -> list:
    """World Model: 8 inputs -> 3 predictions."""
    samples = []
    for i in range(n):
        season = random.randint(0, 3)
        weather = random.randint(0, 5)
        valence = random.uniform(-1, 1)
        arousal = random.random()
        hunger = random.random()
        social = random.random()
        safety = random.random()
        resource = random.random()
        
        if use_nim:
            prompt = (
                "You are a world model predictor for an NPC in Ashgrove Valley.\n"
                "Predict next-tick changes given current state:\n"
                f"season={season}, weather={weather}, valence={valence:.3f}, arousal={arousal:.3f}\n"
                f"drives: hunger={hunger:.3f}, social={social:.3f}, safety={safety:.3f}, resource={resource:.3f}\n"
                "Output ONLY JSON with:\n"
                "  weather_shift: float (-1..1, negative=worsening)\n"
                "  social_response: float (0..1, positive=good)\n"
                "  resource_availability: float (0..1)"
            )
            try:
                limiter.wait()
                response = call_nim(prompt, max_tokens=128, temp=0.1)
                teacher_data = extract_json(response)
                if teacher_data:
                    teacher = [teacher_data["weather_shift"], teacher_data["social_response"], teacher_data["resource_availability"]]
                else:
                    teacher = None
            except Exception:
                teacher = None
            if teacher is None:
                teacher = local_world_model(season, weather, valence, arousal, hunger, social, safety, resource)
        else:
            teacher = local_world_model(season, weather, valence, arousal, hunger, social, safety, resource)
        
        samples.append({
            "task": "world_model",
            "inputs": [float(season)/3.0, float(weather)/5.0, valence, arousal,
                       hunger, social, safety, resource],
            "target": teacher,
        })
        if i % 10 == 0 or i == n - 1:
            write_progress("world_model", i + 1, n)
    return samples

def main():
    use_nim = bool(NIM_KEY)
    print(f"Generating cognitive MLP training data ({'NIM teacher' if use_nim else 'local heuristics'})...")
    print(f"NIM: {NIM_MODEL} @ {NIM_URL} (RPM={NIM_RPM})")
    
    limiter = RateLimiter(NIM_RPM)
    
    all_samples = []
    print("Generating attention samples (200)...")
    all_samples.extend(generate_attention_samples(200, limiter, use_nim))
    
    print("Generating action_evaluator samples (200)...")
    all_samples.extend(generate_action_evaluator_samples(200, limiter, use_nim))
    
    print("Generating world_model samples (100)...")
    all_samples.extend(generate_world_model_samples(100, limiter, use_nim))
    
    print(f"Total samples: {len(all_samples)}")
    
    out_path = "data/mlp_training_data.jsonl"
    with open(out_path, "w") as f:
        for s in all_samples:
            f.write(json.dumps(s) + "\n")
    print(f"Saved to {out_path}")
    
    random.shuffle(all_samples)
    split = int(len(all_samples) * 0.9)
    train, val = all_samples[:split], all_samples[split:]
    
    with open("data/mlp_train.json", "w") as f:
        json.dump(train, f)
    with open("data/mlp_val.json", "w") as f:
        json.dump(val, f)
    print(f"Train: {len(train)}, Val: {len(val)}")

if __name__ == "__main__":
    main()