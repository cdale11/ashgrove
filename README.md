# Ashgrove

A single-player psychological horror RPG where the player investigates an isolated
mountain village that slowly reveals disturbing truths. The world is a living
simulation — villagers have memories, beliefs, relationships, routines, and lives
that continue without the player.

## Architecture

```
┌──────────────────┐   WebSocket / REST    ┌──────────────────────────┐
│  React + TS web  │ ◄────────────────────► │  C++20 Game Server        │
│  client (Vite)   │                        │  ├─ Simulation (time,     │
└──────────────────┘                        │  │   weather, seasons)    │
                                            │  ├─ World (regions,      │
                                            │  │   buildings, items)    │
                                            │  ├─ NPCs (3 tiers,       │
                                            │  │   memory, beliefs)    │
                                            │  ├─ Investigation        │
                                            │  │   (knowledge, evidence)│
                                            │  └─ Networking (REST +   │
                                            │      WebSocket)          │
                                            └──────────────────────────┘
```

- **Backend**: C++20, owns the world simulation. The simulation is always the
  source of truth.
- **AI**: llama.cpp with local GGUF models (planned). The simulation validates
  everything the LLM proposes.
- **Frontend**: React + TypeScript, a visualization client over WebSocket/REST.
- **Python**: ML, offline tools, asset processing (not the simulation runtime).

## Prerequisites

- conda (all game libraries live in the `ashgrove` conda environment)
- CMake ≥ 3.25, a C++20 compiler
- Node.js ≥ 20

## Setup

```bash
# Create the conda environment (all game libraries live here)
conda env create -f environment.yml

# Backend
source /home/umang/miniconda3/etc/profile.d/conda.sh
conda activate ashgrove
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$CONDA_PREFIX \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j$(nproc)

# Frontend
cd frontend
npm install
```

## Running

```bash
# Terminal 1: game server (default port 8000)
conda activate ashgrove
./build/bin/ashgrove_server --port 8000

# Terminal 2: web client
cd frontend
npm run dev
# open http://localhost:5173
```

## Tests

```bash
conda activate ashgrove
./build/bin/ashgrove_tests
```

## API

| Endpoint                 | Method | Description                          |
|--------------------------|--------|--------------------------------------|
| `/api/health`            | GET    | Server status                        |
| `/api/world/state`       | GET    | Full world snapshot (JSON)           |
| `/api/action`            | POST   | Player action (`save`, `load`, ...)  |
| `/ws`                    | WS     | Real-time state streaming + actions  |

## Status

Vertical slice in progress:

- [x] C++ project structure (CMake, C++20)
- [x] Time system (days, seasons, weather)
- [x] World model (regions, buildings, items, resources)
- [x] NPC model (3 tiers, memories, beliefs, relationships, goals, emotions)
- [x] Investigation system (knowledge, evidence, contradictions)
- [x] HTTP + WebSocket server with REST API
- [x] React/TypeScript client (time, village, NPC detail, investigation panels)
- [ ] llama.cpp NPC cognition (simulation-validated LLM proposals)
- [ ] Player character + action verbs (move, interact, talk, inspect)
- [ ] Dialogue system with NPC-generated responses
- [ ] Procedural forest/cave generation
- [ ] Survival systems (hunger, fatigue, temperature)
