# Ashgrove Valley — Agent Operating Procedures (SOP)

Standard Operating Procedures for any AI agent (opencode, Claude, etc.) working on this
repository. Read this file first, every session.

---

## 1. The Project

Ashgrove Valley is a C++17 / CMake text-first farming game with an embedded local LLM:

- **LLM role 1 — Intent parsing** (`LlamaWrapper::parse_command`): turns natural language into
  `{"action": ..., "parameters": {...}}` JSON (LoRA student, Qwen2.5-0.5B).
- **LLM role 2 — Town Consciousness** (`TownConsciousness`): daily consolidation job that ingests
  the game's event log, aggregates a Town Memory, and emits world adaptations.

Hardware: i3-4160, 4 cores, 7.7 GiB RAM, 11 GiB swap. **CPU-only inference (~28 tok/s).**
Everything must stay fast and memory-light.

Build: `cmake --build build -j4`. Binary: `build/ashgrove_server`.

---

## 2. Starting / Managing the Server

> **THE most important rule.** The server dies silently if started wrong, and naive restarts
> make the agent's own shell hang. Follow this exactly.

### Launch reliably in a detached screen

```bash
cd /home/umang/ashgrove
./tools/launch_server.sh start
```

That script (already written, keep it in sync) does:

```bash
screen -dmS ashgrove bash -c 'cd /home/umang/ashgrove && exec ./build/ashgrove_server 8080 > /tmp/server.log 2>&1'
```

- It must run on **port 8080**.
- Output goes to `/tmp/server.log` — always tail that file to debug.
- Check health: `curl -s http://localhost:8080/state` (expect HTTP 200).
- Stop: `./tools/launch_server.sh stop` · Restart: `./tools/launch_server.sh restart`
- Attach to the live console: `screen -r ashgrove` (detach with `Ctrl-A Ctrl-D`).

### NEVER do these

- **NEVER `pkill -f ashgrove_server`.** `-f` matches the *agent's own shell command line*
  (`bash -c '... pkill -f ashgrove_server ...'`), so the agent kills its own shell → hang/timeout.
  Use **exact match only**: `pkill -x ashgrove_server` (or `pgrep -x`).
- **NEVER start it with `&` inside a bash tool call and rely on the tool returning.** The bash
  tool kills the whole process group on command timeout. Always use `screen -dmS` (screen sessions
  DO persist across tool calls in this environment — verified).
- **NEVER run the server in the foreground of a tool call** (it blocks forever → tool timeout).

---

## 3. Every Agent Must

1. **Use all available skills and tools.** The repo has a skill system
   (`.agents/skills/`, loaded via the `skill` tool). If a needed skill/tool doesn't exist, prompt
   the user to install it — don't silently hack around. Document skill usage in the commit.
2. **Introduce no new bugs.** Every change must build and run.
3. **Fix bugs immediately when found** — then and there, in the same session, not "later".
4. **Fix all compiler warnings immediately.** The project builds with `-Wall`-class warnings;
   a clean build must stay clean. A warning you ship is a bug you shipped.
   (`cmake --build build -j4` — do not ignore warnings, including `-Wsign-conversion`,
   `-Wshadow`, `-Wunused-parameter`.)
5. **Document exhaustively** for future agents. Update the relevant docs
   (`docs/ROADMAP.md`, `docs/shipped-features.md`, `README.md`, this SOP, `MISTAKES.md`)
   whenever behavior or architecture changes.
6. **When in doubt, ASK.** If something is ambiguous, risky, or beyond your confidence, use the
   question tool to ask the user instead of guessing.
7. **Keep a `MISTAKES.md`.** Every mistake made by any agent gets recorded there with the lesson.
   This file is a living record — read it at the start of every session too.
8. **Commit and push regularly.** Small, frequent, well-scoped commits. Never leave the tree dirty
   at the end of a session. Follow conventional commit messages (`feat:`, `fix:`, `docs:`, etc.).

## 3.1 Additional Standing Rules

- **Audit before major work** — before any major rewrite or new phase, audit the project against
  the vision, present gaps, and get user sign-off on the updated roadmap.
- **Agent-initiated suggestions** — propose improvements, refactorings, or new features based on
  your own analysis of the codebase and roadmap.
- **Emergence over hard-coding** — prefer event-driven interactions (Event Bus) over scripted
  sequential flows; avoid direct cross-module calls.
- **Maintain parallelism** — new code respects the thread-pool architecture; fine-grained
  mutexes or lock-free queues only; no global locks.
- **Simplify for the player** — text commands are the UI; NLP handles complexity; minimap is
  reference only.
- **Verify before push** — run `cmake --build build -j4` (and any tests) before every push.
- **Model policy** — runtime LLM is local llama.cpp only (student GGUF). NVIDIA NIM is used
  ONLY for offline training-data generation (`tools/gen_dataset.py`, `gen_cognitive_mlp_data.py`),
  never at runtime.

---

## 4. Architecture Reference (for agents)

### Layout
- `src/`, `include/` — C++17 source. HTTP via cpp-httplib.
- `data/` — runtime state: `save.json`, `cmdlog.jsonl`, `town_log.jsonl`, `town_memory.json`,
  `adaptations.json`, `npc_cognitive_state/`, `mlp_weights.json`, the model
  `qwen2.5-0.5b-ashgrove-q4_k_m.gguf`, `lora-adapter/`.
- `tools/` — `launch_server.sh`, `run_server.sh`, training scripts (`train_lora.py`), dataset gen.
- `docs/` — **`ROADMAP.md` (all open work, priority-ordered)**, `shipped-features.md`
  (everything shipped), `cognitive-architecture.md` (cognition spec).
- `.agents/skills/` — the agent skill library (CMake, C++ standards, testing, etc.).

### HTTP API (server routes)
`/` health · `/join` · `/state` · `/move` · `/warp` · `/action` · `/cmd` · `/sleep` · `/explore` ·
`/travel` · `/region` · `/dsl` · `/quest` · `/job` · `/market` · `/horror` · `/basement` ·
`/town/nature` · `/town/village` · `/town/economy` · `/town/culture`.

### `/cmd` flow (intent parsing)
1. `/cmd` receives raw text + player_id.
2. Classified into a tier: `rule` (command verbs), `llm` (natural language), `none`.
3. `llm` tier → `LlamaWrapper::parse_command` → intent JSON `{action, parameters}`.
4. `process_intent` reconstructs a command string and calls `handle_cmd`.
5. Every `/cmd` also pushes a `player/player_cmd` event into the Town Consciousness buffer.

### Town Consciousness
- `observe(TownEvent)` pushes into a thread-safe ring buffer (`event_buffer_`, `buffer_mutex_`).
- Game loop calls `observe` for a `weather/state` heartbeat every 60 s and for each `/cmd`.
- Daily at in-game 04:00 (`hour_of_day == 28`), an async worker:
  `aggregate_memory()` (fills `player_habits`, `npc_relationships`, `economic_trends`,
  `ecological_state`, `performance_profile`) → LLM consolidation inference →
  `parse_llm_response` applies adaptations with damping (`new = 0.3*proposed + 0.7*old`).
- Outputs: `data/town_log.jsonl` (event log), `data/town_memory.json`, `data/adaptations.json`.

### Known limitation (deferred — do not start without user sign-off)
The LoRA model was trained **only** on intent parsing. Given a Town Consciousness consolidation
prompt it emits intent-shaped JSON (`{"status":"new","parameters":{...}}`) that
`parse_llm_response` cannot map to `procgen/npc/economy/weather/horror/performance`, so
adaptations stay at defaults. Retraining with consolidation-format examples is tracked in
`docs/ROADMAP.md` (Tier 1.5) and is **deferred** per the user.

---

## 5. Working Style Checklist (per task)

- [ ] Read `MISTAKES.md` and this SOP first.
- [ ] Read `docs/ROADMAP.md` (open work) and `docs/shipped-features.md` (what's live).
- [ ] Understand the relevant code before editing (read surrounding context, follow conventions).
- [ ] Build: `cmake --build build -j4` — zero errors, zero new warnings.
- [ ] Test against the running server (or relaunch per §2).
- [ ] Fix any bug or warning discovered — immediately.
- [ ] Update docs that describe the changed behavior.
- [ ] Record mistakes in `MISTAKES.md`.
- [ ] Commit + push with a conventional message.