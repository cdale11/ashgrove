# Ashgrove Valley — Mistakes Log

Living record of agent mistakes and the lessons learned. **Read this at the start of every
session.** Append any new mistake immediately so no future agent repeats it.

---

## 2026-08-16

### M1 — `pkill -f ashgrove_server` killed the agent's own shell (hang/timeout)

**What happened:** Tried to restart the server with `pkill -f ashgrove_server`. `-f` matches the
full command line, which included the agent's own `bash -c '... pkill -f ashgrove_server ...'`
wrapper. The agent killed its own shell → tool hung until timeout, server left in an unknown
state, wasted a long debugging session.

**Lesson:** Never `pkill -f` / `pgrep -f` on a pattern that appears in your own command line.
Use exact process-name match: `pkill -x ashgrove_server`. Also never start the server with `&`
in a bash tool call — the tool kills the whole process group on timeout. Use
`./tools/launch_server.sh start` (screen-based).

### M2 — Background server assumed killed when it wasn't (and vice-versa)

**What happened:** Assumed a `screen`-launched server would not persist and that ordinary
background launches would. Both assumptions were wrong: screen sessions DO persist across tool
calls in this environment, while `&`-backgrounded processes die with the tool's process group.

**Lesson:** Always verify actual state before acting — `pgrep -x ashgrove_server` and
`curl -s localhost:8080/state`. Don't assume process lifetime from intuition.

### M3 — Town Consciousness event buffer was never fed; memory never written

**What happened:** The `TownConsciousness` class existed with an `observe()` ring buffer and a
`memory_` struct (player_habits, npc_relationships, etc.), but `main.cpp` never called
`observe()` — so every consolidation prompt showed `events: 0` — and nothing ever wrote the
memory sections, so `town_memory.json` had every field `null`. Diagnosed during a
"check town consciousness" review.

**Lesson:** When reviewing a system, trace the **data flow end-to-end** (producer → buffer →
consumer → persistence), not just the system's own code. A well-formed class with no callers is
dead code. Wire producers before declaring a feature "done."

### M4 — LoRA trained only on intent parsing, not consolidation format

**What happened:** The fine-tuned model answers the Town Consciousness consolidation prompt with
intent-shaped JSON (`{"status":"new","parameters":{...}}`) instead of
`{"procgen":{...},"npc":{...},...}`. `parse_llm_response` therefore never maps anything and
adaptations stay at defaults.

**Lesson:** A model trained for one task will not magically do another. Before expecting
"town consciousness" output, the training set must include consolidation-format examples
(tracked in `docs/ROADMAP.md` Tier 1.5). Deferred per user — don't start.

### M5 — Debug builds / warnings ignored during LLM-path testing

**What happened:** While testing the `/cmd` LLM path, compiler warnings (sign-conversion,
shadowing) in touched files were left in place; one latent build error (`history.value(...).get<double>()`)
surfaced at the next build.

**Lesson:** Build and fix warnings after every edit, immediately, not at the end. Never ship a
file with warnings you added. (`cmake --build build -j4`, watch `-Wshadow`, `-Wsign-conversion`.)

### M6 — `/cmd` expects JSON field `"cmd"`, not `"text"`
**What happened:** While verifying ROADMAP 1.4, test curl calls sent `{"player_id":1,"text":"..."}`.
The server reads `j.value("cmd","")`, so `cmd` was empty → every `/cmd` fell to the LLM tier,
which returned `{"action":"load"}` → every response was "Loaded save.json — ...". This looked
like an intent/regression bug. Worse, hammering the empty-cmd LLM path repeatedly triggered
concurrent `w = std::move(fresh)` in the `load` handler and crashed the server (segfault in a
worker; gdb only showed the main `accept()` thread).

**Lesson:** Confirm the request schema first. `/cmd` uses `"cmd"` (see
`tools/eval_intent_accuracy.py` which posts `{"cmd": text, ...}`). Test the correct field before
debugging. Also: never fire the LLM `load`/`newgame` path concurrently in a loop — it replaces
the whole `World` under the mutex while worker threads may hold references; treat it as fragile.

### M7 — LLM one-line dialogue: the runtime model (intent LoRA) is unsuited for dialogue generation
**What happened:** While implementing ROADMAP 1.7d (LLM one-line cognitive dialogue), the `LlamaWrapper` loads the intent-parsing LoRA (Qwen2.5-0.5B fine-tuned for intent JSON). Given a free-form dialogue prompt, it produces narrative continuation ("Leah said. Sure, I can drink...") instead of a single quoted dialogue line. Strict validation rejects this garbage, falling back to the static template.
**Lesson:** The architecture doc assumes a dialogue-capable LLM at runtime, but the deployed model is an intent LoRA. For production dialogue, a separate dialogue-tuned model (or a unified model with dialogue head) is needed. The current fallback-to-template is correct defensive behavior. Document this gap in the cognitive-architecture doc and ROADMAP (future training task).