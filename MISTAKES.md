# Ashgrove Valley — Mistakes Log

Living record of agent mistakes and the lessons learned. **Read this at the start of every
session.** Append any new mistake immediately so no future agent repeats it.

---

## 2026-08-19

### M8 — Seed-name parsing assumed single tokens; multi-word seeds broke `breed`

**What happened:** `breed Parsnip Seeds Parsnip Seeds` hit "Usage: breed <seed1> <seed2>".
First attempt split the argument on whitespace into 4 tokens. Second attempt matched two seed
names by prefix but only continued scanning forward past the first match, so it never re-found
`known_seeds[0]` for the second seed after trimming (the loop index had already advanced past it).

**Lesson:** When matching a repeated vocabulary that may itself contain spaces, do two
independent passes over the full vocabulary (match seed1 at start → trim → match seed2 at
start), never one forward-only loop with a trimmed cursor.

### M9 — Edit removed the whole body of a command block during refactor

**What happened:** Replacing the `breed` block accidentally dropped its closing brace and the
rest of its implementation, so the following `if (cmd == "build")` block became nested and the
build failed with "a function-definition is not allowed here". Reverted via `git checkout` and
re-applied cleanly.

**Lesson:** For large block replacements, read the full existing block first, keep the closing
brace, and prefer `git checkout <file>` + a clean re-edit over surgical in-place edits of
half-understood code.

### M10 — Launch script "Server process exited early!" is a false alarm

**What happened:** After `./tools/launch_server.sh start`, the script printed "Server process
exited early! See /tmp/server.log" but `pgrep -x ashgrove_server` showed the server healthy
(LLM model load makes readiness flaky). Assumed a crash, wasted debug time.

**Lesson:** Always confirm with `pgrep -x ashgrove_server` + `curl /state` before assuming the
server is down; the readiness probe can race the ~2s model load.

### M11 — ROADMAP progress summary overclaimed unimplemented work

**What happened:** Session summary described "L-System growth in crop growth" and "harvest seed
saving with EA recombination" as already done, but `git diff` showed only the Crop struct +
serialization + a stub `breed` command existed. The real implementation had to be written.

**Lesson:** Trust `git diff`/`git status`, not memory of what a previous session "planned".
Verify claimed work against the actual working tree before building on it.

### M12 — Unused-parameter comment applied to the wrong parameter

**What happened:** In `SocialCognition::receive_belief` the compiler flagged `fact` as an unused
parameter, but the cleanup commented out `source_confidence` instead — a parameter the body
still uses (`effective_confidence = source_confidence * edge.trust`). The full build failed
with "source_confidence was not declared in this scope" and a second build cycle was needed.

**Lesson:** Always re-read the exact warning text (it names the parameter) before editing, and
re-run the fast `g++ -fsyntax-only` single-file check before kicking off the full 3-minute
build — it catches these immediately.

### M13 — Batch variable rename left a live reference to the old name

**What happened:** Renamed the job-board loop variable `j` → `jb` to fix a `-Wshadow`, but a
`j.cooldown_until` reference deeper in the same block wasn't matched by the replacement batch.
The syntax check passed (shadow gone), but the full build failed with a hard error
(`json has no member named 'cooldown_until'`). Fixed by editing the remaining reference.

**Lesson:** After a multi-line `replace` batch, grep the file for the old identifier to catch
unmatched references; the fast syntax-only check validates compile-ability but not
name-correctness across a whole block. The `-fsyntax-only` loop is the cheapest first gate.

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
**Lesson:** The architecture doc assumes a dialogue-capable LLM at runtime, but the deployed model is an intent LoRA. For production dialogue, a separate dialogue-tuned model (or a unified model with dialogue head) is needed. The current fallback-to-template is correct defensive behavior. Document this gap in the cognitive-architecture doc and ROADMAP (future training task).### M14 — Release placement loop lacked a `placed < count` guard on the inner loop
**What happened:** The new `release` command asked for 2 ladybugs but placed 3 (and charged
for 2). The outer loop checked `placed < count` but the inner `dx` loop placed one predator
per neighbor unconditionally, so every outer iteration over-released by up to 3.
**Lesson:** When two nested loops both contribute to a budget, guard BOTH loop conditions
(and never derive cost from the requested count when the placed count may differ — refund the
difference and report the actual charge).

### M15 — Town Consciousness consolidation could crash the whole server
**What happened:** The consolidation prompt grows with accumulated town memory and events.
On a long-lived save it exceeded the model's `n_batch`, tripping
`GGML_ASSERT(n_tokens_all <= n_batch)` (an abort, not an exception) and killing the server
silently mid-session. Restarts appeared to "randomly" fail until `/tmp/server.log` showed the
assert.
**Lesson:** Any code path that builds a prompt from player/event data must bound its length
well under the decode batch. A bare `llama_decode` of an over-long batch is a process-level
abort — never assume it returns an error you can catch. Cap prompts centrally in
`LlamaWrapper` (2000 tokens) AND budget the prompt builder (memory ≤1500 chars/section,
events ≤3500 chars). Check `/tmp/server.log` for `GGML_ASSERT` when the server dies.

### M16 — Unguarded JSON→float conversion crashed the server on poisoned adaptation data
**What happened:** The runtime LoRA (M4) emits strings/objects into numeric adaptation
fields (`"intensity": "none"`, `{"maybe":"maybe"}`). `World::apply_adaptations` did
`horror_intensity = h["intensity"]` — implicit `from_json<float>` on a string threw
`nlohmann type_error.302` ("type must be number, but is string") and aborted the process at
the FIRST consolidation after load. The data/adaptations.json had been progressively poisoned
across many consolidations, so any server restart that reached an hour-28 consolidation
crashed. The gdb backtrace pinned it to `apply_adaptations` → `from_json<float>`.
**Lesson:** Every `json` → scalar read on LLM-produced or persisted data must be
type-guarded (`is_number()` etc.). Also prevent re-poisoning at the write side:
`parse_llm_response` now merges a proposed adaptation value only when its JSON type matches
the existing one. And the poisoned `data/adaptations.json` was reset to clean defaults —
sanitize corrupted state files, don't just fix the reader.

### M17 — Forest carbon economy broke even at 13 kg, so no tree ever reproduced
**What happened:** `gpp = canopy * 0.06` vs `resp = biomass * 0.012` puts npp = 0 at
biomass ~13 kg. Every legacy tree (backfilled biomass tens–hundreds of kg) ran a chronic
carbon deficit, shrank, and never satisfied the seed condition `npp > 0 && biomass > 0.3*max`.
The /ecology report showed carbon stock falling and zero seed agents. Recalibrated to
gpp 0.25×canopy / resp 0.010×biomass (break-even ~0.55×max; dense stands self-thin via the
light field, gaps/edges grow) — verified: mature trees mast, seeds drift, banks form,
germination adds trees.
**Lesson:** Calibrate ecological balance equations against a realistic state, not just
dimensionally. Before declaring a growth model done, compute its equilibrium point and check
it against the intended mature state (here: big healthy trees should roughly break even and
only stress-limited trees should senesce).

### M18 — Day-seeded RNG roll made seed-bank germination all-or-nothing per day
**What happened:** The germination roll was `(day * 2654435761u >> 16) % 1000` with no cell
index, so on any given day every eligible cell used the same roll → either every banked cell
germinated or none did (0% most days, then a burst). Fixed to mix the cell coordinates into
the hash.
**Lesson:** Any per-cell stochastic decision seeded only by global state (day) is a correlated
roll across the whole map. Always mix per-cell identity into the hash for spatial processes.

### M19 — Server boots at hour 28 can crash in llama decode (rare, intermittent)
**What happened:** If the save's clock (`time` in save.json) lands inside the 04:00
consolidation window (day_seconds 734–766), consolidation fires seconds after boot — racing
llama init — and the first decode occasionally dies silently (no GGML_ASSERT message; gdb
reproduces only sometimes because boot timing shifts). Normal boots (any other hour) and
normal-play consolidations are stable; this only triggers on that exact boot clock.
**Lesson:** Boot-time LLM use is a different risk profile than steady-state. If it recurs,
defer the first consolidation until the model has finished init (e.g., gate on an "LLM ready"
flag) rather than relying on clock position. Recorded as a hardening item in ROADMAP.
