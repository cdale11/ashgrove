#!/usr/bin/env python3
"""
Generate synthetic training data for cognitive MLPs using local heuristics.
No NIM API required — uses the same logic as CognitiveCore fallback.

Output: data/mlp_training_data.jsonl + train/val splits.
"""
import json
import random
import math

random.seed(42)

def generate_attention_samples(n: int) -> list:
    """Attention MLP: 4 features -> 1 gate score."""
    samples = []
    for _ in range(n):
        novelty = random.random()
        reward = random.random()
        social = random.random()
        survival = random.random()
        # Heuristic: survival > novelty > reward > social
        gate = 0.35 * survival + 0.25 * novelty + 0.25 * reward + 0.15 * social
        gate = max(0.0, min(1.0, gate + random.uniform(-0.05, 0.05)))
        samples.append({
            "task": "attention",
            "inputs": [novelty, reward, social, survival],
            "target": gate,
        })
    return samples

def generate_action_evaluator_samples(n: int) -> list:
    """Action Evaluator: 10 inputs -> 6 action scores."""
    samples = []
    for _ in range(n):
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
        scores = [max(0.0, min(1.0, s / max_s)) for s in scores]
        
        samples.append({
            "task": "action_evaluator",
            "inputs": [hunger, thirst, social, safety, curiosity, fatigue,
                       wmb_weather, wmb_social, wmb_resource, social_pressure],
            "target": scores,
        })
    return samples

def generate_world_model_samples(n: int) -> list:
    """World Model: 8 inputs -> 3 predictions."""
    samples = []
    for _ in range(n):
        season = random.randint(0, 3)
        weather = random.randint(0, 5)
        valence = random.uniform(-1, 1)
        arousal = random.random()
        hunger = random.random()
        social = random.random()
        safety = random.random()
        resource = random.random()
        
        # Heuristic predictions
        weather_shift = (0.5 - weather/5.0) * 0.5 + random.uniform(-0.2, 0.2)
        social_response = 0.5 + valence * 0.3 + random.uniform(-0.1, 0.1)
        resource_avail = resource * 0.8 + random.uniform(-0.1, 0.1)
        
        weather_shift = max(-1.0, min(1.0, weather_shift))
        social_response = max(0.0, min(1.0, social_response))
        resource_avail = max(0.0, min(1.0, resource_avail))
        
        samples.append({
            "task": "world_model",
            "inputs": [float(season)/3.0, float(weather)/5.0, valence, arousal,
                       hunger, social, safety, resource],
            "target": [weather_shift, social_response, resource_avail],
        })
    return samples

def main():
    print("Generating cognitive MLP training data (local heuristics)...")
    
    all_samples = []
    all_samples.extend(generate_attention_samples(200))
    all_samples.extend(generate_action_evaluator_samples(200))
    all_samples.extend(generate_world_model_samples(100))
    
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