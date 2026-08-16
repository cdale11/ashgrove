#!/usr/bin/env python3
"""
Train cognitive MLPs (attention, action_evaluator, world_model) from synthetic data.
Saves weights in TinyMLP-compatible JSON format.
"""
import json
import random
import sys
import os

import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset

# Reproducibility
torch.manual_seed(42)
random.seed(42)

class TinyMLP(nn.Module):
    def __init__(self, n_in: int, n_hidden: int, n_out: int):
        super().__init__()
        self.fc1 = nn.Linear(n_in, n_hidden)
        self.relu = nn.ReLU()
        self.fc2 = nn.Linear(n_hidden, n_out)
        if n_out == 1:
            self.out_act = nn.Sigmoid()
        else:
            self.out_act = nn.Sigmoid()  # all outputs in [0,1]

    def forward(self, x):
        x = self.relu(self.fc1(x))
        x = self.out_act(self.fc2(x))
        return x

def load_data(path: str):
    data = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            data.append(json.loads(line))
    return data

def train_mlp(task_name: str, n_in: int, n_hidden: int, n_out: int, 
              train_data, val_data, epochs: int = 100, lr: float = 1e-3):
    device = torch.device("cpu")
    
    # Prepare tensors
    train_inputs = torch.tensor([d["inputs"] for d in train_data], dtype=torch.float32)
    train_targets = torch.tensor([d["target"] for d in train_data], dtype=torch.float32)
    if train_targets.dim() == 1:
        train_targets = train_targets.unsqueeze(1)
    
    val_inputs = torch.tensor([d["inputs"] for d in val_data], dtype=torch.float32)
    val_targets = torch.tensor([d["target"] for d in val_data], dtype=torch.float32)
    if val_targets.dim() == 1:
        val_targets = val_targets.unsqueeze(1)
    
    train_ds = TensorDataset(train_inputs, train_targets)
    val_ds = TensorDataset(val_inputs, val_targets)
    train_loader = DataLoader(train_ds, batch_size=32, shuffle=True)
    val_loader = DataLoader(val_ds, batch_size=32, shuffle=False)
    
    model = TinyMLP(n_in, n_hidden, n_out).to(device)
    criterion = nn.MSELoss()
    optimizer = optim.Adam(model.parameters(), lr=lr, weight_decay=1e-5)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs)
    
    best_val_loss = float('inf')
    best_state = None
    
    print(f"\nTraining {task_name}: in={n_in}, hidden={n_hidden}, out={n_out}")
    print(f"  Train samples: {len(train_data)}, Val samples: {len(val_data)}")
    
    for epoch in range(epochs):
        model.train()
        train_loss = 0.0
        for xb, yb in train_loader:
            xb, yb = xb.to(device), yb.to(device)
            optimizer.zero_grad()
            pred = model(xb)
            loss = criterion(pred, yb)
            loss.backward()
            optimizer.step()
            train_loss += loss.item() * xb.size(0)
        train_loss /= len(train_ds)
        
        model.eval()
        val_loss = 0.0
        with torch.no_grad():
            for xb, yb in val_loader:
                xb, yb = xb.to(device), yb.to(device)
                pred = model(xb)
                loss = criterion(pred, yb)
                val_loss += loss.item() * xb.size(0)
        val_loss /= len(val_ds)
        
        scheduler.step()
        
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}
        
        if epoch % 20 == 0 or epoch == epochs - 1:
            print(f"  Epoch {epoch:3d}: train_loss={train_loss:.6f}, val_loss={val_loss:.6f}")
    
    # Load best weights
    model.load_state_dict(best_state)
    print(f"  Best val loss: {best_val_loss:.6f}")
    
    # Export to TinyMLP JSON format
    state = model.state_dict()
    W1 = state["fc1.weight"].numpy().flatten().tolist()
    b1 = state["fc1.bias"].numpy().tolist()
    W2 = state["fc2.weight"].numpy().flatten().tolist()
    b2 = state["fc2.bias"].numpy().tolist()
    
    return {
        "W1": W1, "b1": b1, "W2": W2, "b2": b2,
        "n_in": n_in, "n_hidden": n_hidden, "n_out": n_out
    }

def main():
    print("Loading training data...")
    with open("data/mlp_train.json") as f:
        train_data = json.load(f)
    with open("data/mlp_val.json") as f:
        val_data = json.load(f)
    
    # Split by task
    attention_train = [d for d in train_data if d["task"] == "attention"]
    attention_val = [d for d in val_data if d["task"] == "attention"]
    action_train = [d for d in train_data if d["task"] == "action_evaluator"]
    action_val = [d for d in val_data if d["task"] == "action_evaluator"]
    world_train = [d for d in train_data if d["task"] == "world_model"]
    world_val = [d for d in val_data if d["task"] == "world_model"]
    
    print(f"Attention: {len(attention_train)} train, {len(attention_val)} val")
    print(f"ActionEvaluator: {len(action_train)} train, {len(action_val)} val")
    print(f"WorldModel: {len(world_train)} train, {len(world_val)} val")
    
    # Train each MLP
    weights = {}
    
    # Attention: 4 -> 8 -> 1
    weights["attention"] = train_mlp("attention", 4, 8, 1, 
                                     attention_train, attention_val, epochs=150, lr=2e-3)
    
    # Action Evaluator: 10 -> 16 -> 6
    weights["action_evaluator"] = train_mlp("action_evaluator", 10, 16, 6,
                                            action_train, action_val, epochs=200, lr=1e-3)
    
    # World Model: 8 -> 8 -> 3
    weights["world_model"] = train_mlp("world_model", 8, 8, 3,
                                       world_train, world_val, epochs=150, lr=2e-3)
    
    # Save combined weights
    out_path = "data/mlp_weights.json"
    with open(out_path, "w") as f:
        json.dump(weights, f, indent=2)
    print(f"\nSaved all weights to {out_path}")

if __name__ == "__main__":
    main()