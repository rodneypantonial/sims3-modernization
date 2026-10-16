# Native sampler "hang" 2026-10-16: the game was alive, the probe was clogged

Triage of the two `s3pp-cache` jobs (S3PP Mono caches on and off) that the queue marked FAIL about 15 s into the 180 s native `/simstack` window (PLAN.md "s3pp-cache queue 2026-10-16"). Static and data only: no game run. Times UTC.

## Verdict

**The game did not hang.** The probe agent's RPC queue filled with slow `/simstack` calls, so the watchdog's `/agentstatus` heartbeat call waited behind them for over 60 s, `game-alive.ts` reported "game died", and the queue killed a running game. Each `/simstack` call was slow because of the probe's stack walker, not because of S3PP, the window length or the managed sampler.

## Evidence

1. **The sim loop kept running until the kill** [verified]. `probe-out/profile/s3pp-cache-2026-10-16-bp-s3pp-cache-{on,off}-600-seconds.csv` (stutter profiler, separate Frida session) has a row every second to +377 s / +378 s, which is the kill time (01:23:16 / 01:32:04), with 9 to 63 sim iterations per second in the last 2 minutes. No gap over 1.6 s in either run.
2. **The probe answered, but slowly** [verified]. `memory.csv` (an `address_space` RPC every 10 s) in `probe-out/20261016-091408` and `20261016-092341`: intervals of 10 s until the window, then 17, 30, 46 s (on) and 17, 30, 73 s (off). Address-space numbers kept changing, so the process was live.
3. **Every `/simstack` call timed out, in every windowed run since 2026-10-13 10:27** [verified]. `*-simstack.jsonl` is 0 bytes for `ip-jps-verify` (10-13), `mainstack-2026-10-16` (the "passing" run) and both `s3pp-cache` runs; the last non-empty one is `bp-worker071` (10-13 09:19). profile-world writes a line for any reply, even an error, so an empty file means every call hit its 5 s client timeout. The probe logs show the matching aborted replies (`ConnectionAbortedError`, 20 in `mainstack`, 24 and 25 in `s3pp-cache`). In `mainstack` only 6 `/threadstack` samples landed in 120 s, 9 to 20 s apart.
4. **`mainstack` passed only because its window was shorter** [inferred]. Same failure, but with 120 s of backlog the heartbeat got through within 60 s; with 180 s it did not.
5. **No other cause fits** [verified code]. `threadStack` already resumes in a `finally`, and the walk runs after the resume, so no thread stays suspended after an error. The managed sampler (`/managedstack`, TLS-based thread lookup) finished before the window and wrote its data. S3PP on vs off made no difference. No EA xcpt log was written, because nothing crashed.

## Cause

Two costs in `tools/probe/agent.js`, unchanged since 2026-10-11, that grow with the game's thread and module counts:

- `isCode()` called `ModuleMap.update()` (re-enumerates every module) on **every miss**. The stack walk's fallback scans up to 512 stack words when the frame-pointer chain is short, and most words are not code, so one walk could run hundreds of module enumerations [verified code].
- `findSimThread()` caches the sim thread only when it finds `MonoScriptHost::Simulate` (0xD81840..0xD82100) in the first 24 walked frames. When it does not (deep interpreter stacks, or hooks without EBP frames in the chain since 2026-10-13 [inferred]), every call runs `Process.enumerateThreads()` (suspends each of ~144 threads in turn) plus one walk per thread [verified code].

Measured on a 32-bit `ping.exe` dummy with 8 extra threads (`tools/probe/simstack-test.py`) [verified dummy]: the old agent took 56 ms per `/simstack` call when the thread is not found and 53 ms with a stored id. The game has ~144 threads (`memory.csv`) and many more modules, so seconds per call is expected there [inferred, scale not measured in game].

Each aborted HTTP request still runs to the end in the agent, and profile-world sends the next one 0.2 s later, so the backlog grows for as long as the window lasts.

## Fix (this commit)

- `agent.js` `isCode`: at most one module-map refresh per second.
- `agent.js` `findSimThread`: take the sim thread id the game stores at app init, `[0x11D8A8C]` (docs/28 §5.2, already used by `/threadstack` to refuse the sim thread) [verified static]; the Simulate search is now only a fallback before init, run at most once per 10 s while it fails.
- `ts3probe.py`: one `/simstack` or `/threadstack` call in flight; a second one returns `busy` at once, so a timed-out sampler can no longer queue work in front of `/agentstatus`.
- `profile-world.ts`: writes only samples with a stack, and prints `native sampler done: N sim samples lost`, so an empty `simstack.jsonl` cannot go unnoticed again.

Not needed: a suspend watchdog or try/finally (already there), and a lower sample rate (each call is now short).

**Test** [verified dummy]: `python tools/probe/simstack-test.py` PASS: 0.6 ms per call with a stored id, 3.1 ms average over 20 calls when nothing is found (one search, then the 10 s back-off), `/threadstack` still refuses the sim thread, dummy alive after detach. `python tools/probe/threadstack-test.py` still PASS; its `main` sample went from ~30 ms (docs/20 §7) to 0.9 ms per call. Pass an old agent as the argument to compare: `python tools/probe/simstack-test.py <old agent.js>`.

## Open

- `[0x11D8A8C]` is verified static only. The first game run must confirm it is the thread that runs `Simulate`: the samples should show `MonoScriptHost::Simulate` / Mono interpreter frames, as in the pre-2026-10-13 `simstack` files (docs/20).
- `game-alive.ts` treats a slow probe as a dead game. With the busy guard that should not recur, but a check of the stutter profiler's per-second rows (or the TS3W.exe CPU time) before the kill would tell the two apart. Not changed here (not owned by this task).
- `ts3probe.py` `Symbols.name` refreshes the module list (one RPC) for every address outside a known module. Not seen to be slow; left as is.

## Repro for the main session

Re-run the failed job, which is a 10-min BP profile with the 180 s windows:

```
node tools/harness/queue.ts tools/harness/queues/s3pp-cache.json
```

Pass: both jobs PASS; profile-world prints `native sampler done: N sim samples lost` with N small (under 5% of ~900); `probe-out/profile/s3pp-cache-<date>-bp-s3pp-cache-on-simstack.jsonl` is non-empty and its top frames include `TS3W.exe` interpreter / Simulate callees; `memory.csv` keeps its 10 s rhythm through minutes 5 to 8. Quick check by hand in a loaded world: `python tools/probe/ts3probe.py get "simstack?frames=24"` answers in well under a second, and its stack reaches `MonoScriptHost::Simulate` (0xD81840..0xD82100) at least in some samples.
