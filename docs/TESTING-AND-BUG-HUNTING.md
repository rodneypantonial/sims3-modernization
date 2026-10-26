# Test harness and bug hunting

How this project tests changes to The Sims 3 and how it chases bugs down to a cause. It is the overview: what each tool is for, how the pieces fit, and the method we follow when a run crashes, hangs or hitches. The exact flags, file formats and guard tables are in [docs/TESTING.md](TESTING.md); the full case notes are in the docs linked at the end.

Everything here runs from the repo root with Node.js >= 22.18 (`node script.ts`, no npm dependencies) on Windows, with the Steam edition of the game. Harness output goes to `probe-out/` (git-ignored). Saves, dumps, logs with personal paths and decompiled code never go into git.

## 1. The harness in one picture

```
queue.ts  (a JSON list of jobs, one game start per job, one result row per job)
  └─ per job: cycle.ts --restart  ->  build check  ->  ts3probe.py serve --launch  ->  load save
       └─ run: profile-world.ts (timing / A/B / repro loops) | repro.ts | soak | save matrix | age | ...
            ├─ in-game test agent (mods/ts3agent): commands over the probe, e.g. spawn Sims, send them somewhere, flip a fix
            ├─ native plugin (ts3native): counters, switches, crash lines, ts3native.log
            └─ readers afterwards: hitch-ab.ts, sim-budget-report.ts, memory-report.ts, run-verdict.ts, crash-ledger.ts
```

- **Probe** (`tools/probe/ts3probe.py`, Frida): starts or attaches to the game and serves JSON on `127.0.0.1:47891` (status, memory, sim and managed stacks, native counters, agent commands). Stop it with `get detach`, never by killing Python.
- **Test agent** (`mods/ts3agent`): a script mod that answers harness commands inside the game. Built with `node tools/harness/build-agent.ts` (full package, needs a game restart) or `--hot` (plugin only, reloaded with `reload-agent.ts` without restarting). One command from the shell: `node tools/harness/ag.mjs <cmd> '<json>'`.
- **Native plugin** (`native/ts3native`): every fix and every detector is an `ini` switch, default off until measured. Its log, `Game\Bin\ts3native.log`, is where crash lines, per-minute counters and diagnostics land.

### Test agent commands (examples)

Handlers live in `mods/ts3agent/src/plugin/Handlers/`. The groups that matter for testing:

| Group | Examples | Used for |
|---|---|---|
| Harness control | `game_state`, `load_save`, `save_game`, `autopilot`, `keep_alive`, `decline_requests`, `quiet_ui`, `set_speed`, `test_env` | Keep a run moving without a human: speed held, popups cleared, no frozen winter or full-moon skew |
| Scenario triggers | `spawn_sims`, `crowd_go`, `place_sim`, `sim_route_to`, `sim_visit_lot`, `sim_go_home`, `pet_guard`, build/buy and Edit Town cycles | Make a bug happen on demand instead of waiting for it in play |
| Switches and counters | `patches` (script-mod fixes on/off, counters), `native_set` (native switches at runtime), `reflect_call` | Flip a fix inside one load for an A/B, read counters |
| Diagnostics | stack readers, memory marks, seeded RNG (`seed_rng`) | Same random draws per run; tag memory before/after a stress cycle |

## 2. Running tests

### Queues (`tools/harness/queue.ts`)

```
node tools/harness/queue.ts tools/harness/queues/<name>.json [--only a,b] [--from <job>] [--dry-run] [--no-smoke]
```

A queue is a JSON list of jobs. Each job names the native build (`asi`), the save, extra `ini` keys (`"Section.Key": value`, merged fresh each job), optional fix installs, and a `run` kind (`profile`, `repro`, `soak`, `matrix`, `age`, ...). Fields that shape the verdict:

- `nativeRequire`, `logRequire`, `logForbid`: counter rules and log-line rules (`"<regex>": count`) that turn a row into FAIL, so "the fix ran" and "the bad line never appeared" are checked, not assumed.
- `scriptErrorsMax`: new script error reports allowed during the job (known EA ones are listed and not counted).
- `minCounters`: refuse to run when the build lacks the module the ini switches on.
- `noiseCheck`: on by default for timing jobs (see below); `false` for correctness-only jobs.
- `routingFailFast`: stop the job at once when routing goes over a threshold (below).
- `timeoutMin`, `pin`, `content` (run with or without custom content), `gameArgs`.

Before each job the queue checks that the installed native plugin and script package are exactly the job's build (sha256 and the version line in `ts3native.log`), and hashes the baseline saves before and after, so a run can never silently measure the wrong build or damage a shared save. `--dry-run` prints the full plan per job.

### Measurement smoke

When a queue holds more than about an hour of timing jobs, `queue.ts` first runs one short smoke job and stops unless it proves the measurement works: the job passed, every A/B window splits per arm, heavy measurement tools are off in timing jobs, and every switch the job sets reads back with its value. A pass is cached per queue, build and harness commit. Correctness-only queues skip it (`--no-smoke`).

### Noise check

Every job samples machine load every 30 s (`machine-load.ts`). A timing job stops about 2.5 minutes in when other processes use 10% or more of the CPU or the clock drops, and a job that ends with a high noise verdict reruns once at the end of the queue. The game is pinned to its own cores at high priority while it runs (`pin.ts`). Timing runs happen on a quiet machine; correctness runs can share it.

### Fail-fast guards

No wait in the harness sits for long when nothing happens. Every wait checks at least every 30 s that what it waits for is alive (game process, probe, agent heartbeat, world flag, committed memory, log growth); two misses in a row end it with a named reason. Long timeouts are only a last resort for slow work that is still moving. A guard never turns a FAIL into a PASS; it only fails a dead wait sooner. The per-wait table is in [TESTING.md "Fail-fast guards"](TESTING.md#fail-fast-guards).

**Routing fail-fast** (`routingFailFast: { ms, scope, allow }`, `routing-failfast.ts`): from the start of the profile window, every 30 s, any routing cost above `ms` (route-plan fallbacks, own-job waits, route-led hitches) stops the job with `FAIL: routing fail-fast: ...`. Scope `unseen` watches only the hitch guard's own lines and is the one to use in A/B or loop jobs (the off arm would trip `all`). A triaged false positive goes into `tools/harness/routing-failfast-allow.json` with a `match`, a `reason` and a `ref` to the doc that cleared it; an entry without a reason and a reference is refused.

### Hold for crash-triage builds (`hold.ts`)

A crash-triage logging or fix build starts the moment the running job ends, and no new job may start while it builds:

```
node tools/harness/hold.ts on <reason>      # queue.ts waits before the next job
node tools/harness/hold.ts wait-job-end     # block until the running job ends
# build, self-test
node tools/harness/hold.ts off              # queue continues within 10 s
```

### Repros: forced loops vs natural play

Every bug and every fix gets a repro: an entry in `tools/harness/repros.json` (save, agent steps, what to watch, pass rule) plus the trigger command in the test agent. `node tools/harness/repro.ts run <id> [--set fix=false]` runs one. We never wait for a bug to show up on its own.

For timing, two kinds of evidence:

- **Forced loops** (`"loop"` in a repro, run by `profile-world.ts --repro` / a queue `profile.repro`): setup steps, then the scenario body once per arm in **ABBA** order (off, on, on, off, repeated) until a fixed event count per arm. The switch flips inside one load (`"$on"` in the step args), so world state and load order cancel out. This is the only way to compare rare long hitches with any confidence.
- **Natural play** (`profile-world.ts` without a repro, `--ab <switch>` for in-load slices): the game plays itself at speed 3. Used to confirm a fix in real play on the worlds that still hitch, and for soaks. A single natural pair can show changes in the >= 100 ms count, but long hitches are too rare there for a natural A/B to separate (docs/168 §1).

### Profiling (`profile-world.ts`)

`node tools/harness/profile-world.ts --tag <tag> [--minutes 10] [--ab <switch> --slice 2] [--repro <id>] [--seed N]` runs on a loaded world: autopilot, the native stutter profiler (every sim iteration, hitches >= 50 ms, route, GC), optional managed and native stack samplers, a clock guard, and a stuck-Sim count at the end. Performance A/B runs are 10 minutes; 5 minutes or less only for smoke, crash and "does the switch work" runs.

## 3. Reading results

| Tool | What it answers |
|---|---|
| `hitch-ab.ts <tag>` | Per run and per arm: iterations, p99, p99.9, max, and counts of iterations >= **100 / 200 / 250 / 300 / 500 / 1000 / 2000 ms** (also per 10 min). 200 and 250 ms are the noticeable-hitch targets; 300 ms and up are the freeze classes. Pairs and pools print 95% confidence intervals. |
| `hitch-ab.ts --causes <tag>` | Ranks what led each >= 300 / >= 1000 ms iteration (sync route plans, script task batches, lock waits, GC), matched from the native log and managed stacks. |
| `hitch-ab.ts --scenario`, `--pool`, `--power`, `--reuse` | Cost per scenario step; pooled arms across runs; how many runs a given reduction needs; earlier valid runs that can be reused as evidence. |
| `sim-budget-report.ts` | Where each millisecond of the sim thread goes per second of play (Mono, internal calls, route plans, lock waits, GC, animation, other), with the top contributors in each bucket (docs/160). |
| `tools/memory-report.ts <ts3native.log>` | Memory per load and over time (commit, largest free block, resource cache, Mono heap, ...), with a leak / cache / flat verdict per metric, destroyed-but-alive objects by type, type growth, and native allocation sites that survive a remove cycle (docs/199-201). |
| `run-verdict.ts`, `crash-ledger.ts triage` | Pass/fail and timing validity per job; each crash report mapped to the change set that was active. |

We judge a change by **noticeable hitches** (p99, p99.9, max, and counts per threshold), not by average speed.

### Crash and hang outputs

- **`ts3native.log` crash lines** (`[Crash] Log`, on in every build): first-chance exception lines with module + offset, registers and a stack scan, written before the game's own handler runs.
- **`mono fault:` line**: when the fault is inside the game's script interpreter, the plugin adds the method that faulted, the virtual call it was making, the receiver object with its type words, and the script stack. This turns "access violation in the interpreter" into "this C# method called this delegate on a freed object".
- **Hang capture** (`game-alive.ts`): when the game is alive but stuck (heartbeat frozen, probe answering), the harness reads the sim-thread stack five times, every other thread's stack, native counters and lock holders, and the managed stack, before it kills the game.
- **Minidump**: every path that kills a still-running game for a stall, hang or timeout writes a full-memory dump first (`probe-out/dumps/`, git-ignored, never shared: dumps hold personal data). Lock owners in a deadlock are read from it.
- The game's own crash report (`xcpt*.txt`) and script error reports are collected per job.

## 4. How we chase a bug

The rules first, then the steps, then a real case.

### Rules

1. **Our build crashed = our bug until proven otherwise.** A crash on a build with our code is investigated as ours. "Possible reasons" are not a cause; the cause is the faulting instruction, the bad value and who wrote it. A fix counts after at least two clean runs of the same job.
2. **Static before bisect.** Read the data the runs already wrote (counters per slice, hitch records, log lines, the design doc of each switch involved, the disassembly of the fault path) before any live rerun. Feature on/off bisect loads cost a lot of game time and rarely name a cause; they are the last option.
3. **Keep every detector on across rounds; assume several causes.** Each round adds logging and never removes the earlier round's lines. One crash can have more than one race; every round reports every bad value it caught, not only the first.
4. **Fail fast.** A run with an obvious error stops and gets fixed; it does not run to its timeout. Night chains skip a failed job and go on.
5. **Measure the tool's cost.** Detectors and samplers change timing. Their cost is measured and subtracted; heavy tools only run in correctness runs.
6. **Correctness runs vs timing runs.** A correctness run may carry every heavy detector; a timing run carries only light ones (the crash logger, light forensics) and passes the smoke and noise checks.
7. **Quiet machine for timing.** Other CPU load or a clock drop voids a timing run.
8. **One change at a time,** compared against a baseline or in one load with runtime switches.
9. **Label claims** `[verified]`, `[community]` or `[inferred]`, and confirm inferred ones before code depends on them.

### Steps

1. **Make it reproducible.** A repro entry with a fixed save, a fixed RNG seed and an agent-driven scenario, run in a queue. A crash that needs hours of natural play first gets its logging added to normal runs, so the next natural crash is caught.
2. **Log at the crash site.** From the fault address and registers, read the path in the disassembly, then add log lines that say what the code was doing when it faulted (for script faults, the `mono fault:` line).
3. **Control runs, one variable at a time.** Same job with the suspected feature off in every arm; then the crashing job again with only the control's extra tools added, to split "the feature" from "the tools changed timing".
4. **Narrowing runs.** Each job answers one question, and the doc says beforehand what each outcome means.
5. **Instrument the runtime.** When the fault is a symptom (a freed object, a zeroed word), add checks inside the engine that catch the bad state when it appears, not when it is used.
6. **Find the writer.** Watch the exact bytes that go bad and log the instruction that writes them.
7. **Fix, plus a backstop.** Fix the writer at its source, keep a cheap guard for the symptom, both off by default behind switches.
8. **Validate in real runs.** The narrowing job with the fix on must show the fix acting (its counter > 0), the bad state never seen again and no crash, then the normal queues and a soak before it can become a default.

### Worked example: Crash S (details in [docs/67 §22-§22.9](67-crash-triage.md))

**Symptom.** In a forced ABBA loop on Sunset Valley (30 extra Sims sent to a far lot, ultra speed, fixed seed), the game crashed in the same arm window twice: an access violation inside the script interpreter's virtual-call lookup, on an object whose type had no method table. That means a managed object was used after the garbage collector freed it.

**Round 1: log the site.** The fault was in EA's interpreter with only EA frames on the stack, and the new feature's acting paths had not run. Still the default suspect (rule 1), so the next build added the `mono fault:` line. The repeat crash named the script method and the freed delegate (a Sim's work loop in a rabbit hole).

**Round 2: control.** The same job with every arm off passed past the crash time, but it also carried two extra detectors, so it was not a clean control. A static check of everything the "on" arm runs found nothing that frees, hides or unroots a managed object. The best static fit (a known double release in EA's frame-arena pool) got a guard switch.

**Round 3: narrowing.** The crashing ABBA with the control's tools crashed again at the same time, so the extra tools were not what made the control pass. The arena-pool theory was refuted by reading the code. Left: something made the collector miss live data.

**Round 4: instrument the runtime.**
- *GC root check:* at every stop-the-world mark, walk every script task and its frame chunks and check that each task is rooted and each frame is inside the scanned range. Result: rooted, in range, and the object was still freed.
- *Frame audit:* after each mark, check that every object word in a live frame was marked. The few unmarked words turned out to be the collector's own deferred-finish window, not the bug.
- *Descriptor watch:* new static finding. Each frame chunk stores its GC layout descriptor in its last word, right after the frame area. If that word is zero, the collector scans nothing in the chunk and frees what it holds. The watch checked every chunk at every mark, and saw a zeroed descriptor minutes before the crash. The watch itself first slowed marks from about 2 ms to over 600 ms per second and failed both jobs, so it was rewritten to one bounded pass (rule 5).
- *Backstop:* `TailPad` moves the chunk end 64 bytes down, so a frame that writes past its end hits padding instead of the descriptor. A restore option rewrote zeroed descriptors and that run passed, which confirmed the mechanism.

**Round 5: find the writer.** A *hardware write watch* (debug registers on the descriptor word of the chunk where a new frame ends near the end) first logged only harmless header writes from the next chunk; the filter was fixed and the watch rerun. It then caught the writer: the interpreter's `initobj` opcode zeroed 12 bytes into an 8-byte `long`/`ulong`/`double` slot. EA's embedded Mono sizes that opcode by the boxed instance size instead of the value size. When such a local is the last one in a frame that ends exactly at the chunk end, the extra 4 bytes zero the descriptor, the collector skips the chunk, and a delegate held only there is freed. It is EA's bug, and vanilla play can reach it. Our feature only changed timing and frame layout so that the case came up within minutes.

**Fix.** `[ArenaGuard] InitobjFix` sizes `initobj` by the real value size (never more than EA wrote), and `TailPad` stays as the backstop. Both are off by default until validated. **Validation:** the narrowing job with the fix on must show `initobj size: N clamped` > 0, zero restored descriptors, zero bad descriptors and no write past the end, followed by the normal queues and a soak.

What the case shows: the first two theories were reasonable and wrong. Each round kept its detectors (rule 3), and the answer came from finding the writer of the bad bytes, not from switching features on and off.

## 5. Detailed docs

- [docs/TESTING.md](TESTING.md): every harness flag, guard table, save matrix, baseline saves, how to report results
- [docs/67-crash-triage.md](67-crash-triage.md): every crash investigated, with the full Crash S rounds (§22)
- [docs/120-mono-gc-multithread.md](120-mono-gc-multithread.md): the garbage collector and frame arena of the embedded Mono
- [docs/130-mono-runtime-re-map.md](130-mono-runtime-re-map.md): the interpreter's opcodes, frames and call paths
- [docs/161-hitch-baseline.md](161-hitch-baseline.md): hitch metrics and the per-world baseline
- [docs/168-hitch-confidence-scenarios.md](168-hitch-confidence-scenarios.md): natural vs forced play, confidence intervals, profiler cost
- [docs/201-alloc-sites.md](201-alloc-sites.md): native allocation sites for memory work
- [docs/202-unseen-sim-fallback.md](202-unseen-sim-fallback.md): the hitch guard whose test loop surfaced Crash S
