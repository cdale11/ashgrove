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
│  ├─ NPCs (types,          │
│  │   memory, beliefs)     │
│  ├─ Investigation         │
│  │   (knowledge, evidence)│
│  ├─ Player + Dialogue     │
│  │   (player character,   │
│  │   knowledge, dialogue) │
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
| `/api/action`            | POST   | Player action, e.g. `{"type":"move","target":{"x":14,"y":10}}` |
| `/ws`                    | WS     | Real-time state streaming + actions  |

Action verbs: `save`, `load`, `get_state`, `move` (`target: {x, y}`), `talk` (`target: <npc id>`),
`dialogue_topic` (`target: <npc id>`, `topic: <name>`), `inspect` (`target: <entity id>`),
`pickup` (`target: <item id>`), `use_item` (`target: <item id>`), `rest`.

## Status

Vertical slice in progress:

- [x] C++ project structure (CMake, C++20)
- [x] Time system (days, seasons, weather)
- [x] World model (regions, buildings, items, resources)
- [x] NPC model (3 tiers, memories, beliefs, relationships, goals, emotions)
- [x] Investigation system (knowledge, evidence, contradictions)
- [x] HTTP + WebSocket server with REST API
- [x] React/TypeScript client (time, village, NPC detail, investigation panels)
- [x] Player character + action verbs (move, talk, inspect, pickup, use_item, rest)
- [x] Dialogue system with NPC responses (topic-based, knowledge-gated)
- [ ] llama.cpp NPC cognition (simulation-validated LLM proposals)
- [ ] Procedural forest/cave generation
- [ ] Survival systems (hunger, fatigue, temperature)
