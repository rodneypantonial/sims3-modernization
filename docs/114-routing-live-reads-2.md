# 114. Routing live reads, pass 2 (I-107-15/16/17/19/20/21/24, I-100-13)

Written 2026-10-18. Plan and tooling; the game has not been run for this doc. Results go in §6 when the queue has run. Ledger: docs/51 §54 (I-114-1 .. I-114-9). Pattern and safety rules: docs/111 (in-room live reads).

## 1. What is open and why a live read

| Item | Question | Static state |
|---|---|---|
| I-107-15 | How often is `O+0x58` (the path's options copy, `path+0x60..+0x64`) non-empty after a plan? | writer verified (docs/107 §9.1) |
| I-107-16 | Is the route's own list `route+0x158+0x58` always empty? | verified static negative; one live confirmation for free |
| I-107-17 | Which ids land in `O+0x58` (start or goal object)? | meaning inferred (§9.2) |
| I-107-19 | P's footprint grid `P+0xDC`: cell count `(P+0xE0 - P+0xDC) / 32` and records per cell, per world | size formula verified (§10.1) |
| I-107-20 | Which threads call the grid fill `0x636BF0` and remove `0x62D9F0`, from which callers? | callers verified (§10.2) |
| I-107-21 | Does `0xC6BAB0` answer like the decoded blend-0 mask (`tools/rtdata-decode.ts --blend0`)? | decode verified offline (§11) |
| I-107-24 | Who calls follow behaviour slot `+0x38` (`0x853820`)? | open (§12.3) |
| I-100-13 (L7) | Does a Sim on a houseboat plan from start points in the lot's slot frame (beyond `WM+0xC0`)? | placement verified (emulator), Sim frame open (docs/100 §12.2) |

All are reads of state the game already has, so this is a Frida script; ts3native and its counters are left alone.

## 2. The probe: `tools/frida/live-reads2.js`

Writes no game memory. Every read is guarded (a bad pointer = an error entry). Lists are bounded (rows 64-300, key maps 32-64 keys, 32 stack samples per grid function).

| Hook (prologue bytes checked) | Read | Answers |
|---|---|---|
| `RouteFollow_EditPathForRoute 0x85FF30` (cdecl `path, ids, x`; bytes `55 8B EC 83 E4 F0 83 EC`) | on return: id count of the `ids` vector (8-byte ids), whether `ids` = `path+0x60`, first 6 ids, the follower id of the route being planned on that thread; caller by return address (`Route_PlanCommon` `0x760F97`, `IC_Route_SetAGS` `0x755672`, the two insert ICs `0x758D4F`, `0x7627F9`) | I-107-15, I-107-17 |
| same, at its entry, once at the window start and once ~25 s before the end | grid dump (below) and the 20 point calls (below), on the planning thread | I-107-19, I-107-21 |
| `Route_PlanCommon 0x7606E0` (thiscall on the route, `ret 8`; bytes `55 8B EC 83 E4 F0 81 EC`) | entry: start-point vector (argument 2, 64-byte records, docs/15 §2), count and first x/y/z; follower id `route+0x40/+0x44`; start z against `WM+0xC0`; return: `route+0x1B0..+0x1B4` (= `route+0x158+0x58`) | I-100-13, I-107-16 |
| `0x636BF0` (`55 8B EC 83 E4 F0 81 EC`), `0x62D9F0` (`83 EC 4C 53 55 56 8B F1`) | thread, caller by return address, whether `ecx` is P; for the first 32 calls off the sim thread, the stack words that point into `.text` (up to 4 KB) and whether any lies in `LotGraph_Build 0x63CB90..0x63D2E2` | I-107-20 |
| `0x853820` (`51 53 56 8B F1 8B 4E 14`) | return-address histogram, threads, code words on the stack for the first 4 return addresses | I-107-24 |
| `Lot_SetHouseboatParent 0xAB9C40` (`55 8B EC 83 E4 F0 8B 45`) | on return: caller, boat id, `Lot+0x444`, corners `Lot+0x134` (4 × (x, z), 8 bytes apart [verified: the G-build loop at `0x67327C` reads them so]), `WM+0xC0`, `WM+0xC4` | I-100-13 |

**Grid dump** [layout from docs/107 §3.1 and §10.1]. P = `[[0x11D9128]+0x95C]` (the planner of docs/111; the fill hook counts `ecx = P` to confirm it is the grid's owner). Cells `P+0xDC..+0xE0`, 32 bytes; per cell bucket array `+4`, bucket count `+8`, chains through `rec+0x48`, sentinel `buckets[count]`; records with lot id `+0x40/+0x44` = 0 are counted ("no lot"); `+0x0C` is read as the element count [inferred EASTL layout] and compared with the walk. Expected cell count from `WM = [0x11ECBC4]` bounds `+0xB4..+0xC0`: `trunc((maxX - minX + 255) / 256) * trunc((maxZ - minZ + 255) / 256)`.

**Point calls.** `0xC6BAB0` is thiscall(WM, x, z), `ret 8`, result in `al` [verified disassembly]: false outside `minX <= x < maxX`, `minZ <= z < maxZ + WM+0xC4`; then layer weight of world-builder layer 0 at `(floor x, floor z)`; weight < 0.5 = true, else true only when `World_LotLookup 0xC68990(WM, x, z)` finds a lot. The probe calls it, and `0xC68990` for the lot flag, on a 5 × 4 grid over the world bounds (20 points, +0.25 m off the grid lines), inside the `0x85FF30` hook only, and only when both functions' first 8 bytes are the game's. Both are reads of world data built at load and of the lot index; the planning thread is where the game itself calls them (`IC_World_IsSimRoutableImpl`, cost modes). This is the one place the probe calls game code; it is listed so (docs/111's probe called none).

**Threads.** The jobs set `[RouteWorker]` and `[RouteAsync]` off, so every plan, and so the `0x85FF30` hook, runs on the sim thread (docs/111 §2); the sim thread is taken as the thread with the most `0x85FF30` calls. I-107-20's "off the sim thread" half therefore shows only threads other than the planner (loading, streaming); the worker case of docs/107 §10.2 needs a run with `[RouteWorker] Mode=1` (same probe, a third job; not queued here).

**Prologue rule.** A hook whose bytes differ is refused and listed, except when the site starts with `jmp rel32` into executable memory: another Frida session (e.g. a second probe on the same site) or a ts3native detour (`[RouteGate] G10Common=1` puts one on `0x7606E0`, docs/68). Frida then hooks on top of the jump and the file lists it under `chained`. None of the six sites is hooked by `stutter-profile.js` or `inroom-reads.js`; ts3native touches only `0x7606E0` (G10Common, off by default) and calls `0xC68990` (design S2, no patch).

**Static note found while building** [verified, direct `E8` scan of `.text`]: `0x636BF0` has a caller docs/107 §10.2 does not list, `0x63AFD0` (sites `0x63B3AE`, `0x63B432`); the probe names it so the run shows whether it matters.

**Safety (probe-safety rule).** `python tools/probe/live-reads2-test.py` runs the unchanged script in a 32-bit dummy (`ping.exe`): a fake world manager, routing world, 6-cell grid, route, path, start vectors in G and beyond the far edge, and a houseboat lot; stubs of the same calling shapes, with `Route_PlanCommon` calling `EditPathForRoute` as the game does; thread A (sim) plans, fills, removes, follows and parents, thread B (worker) fills through a `LotGraph_Build` stand-in and removes directly; the two call targets are real thiscall `ret 8` functions. Checks: wrong bytes refuse every hook and both call targets; a `jmp` already on the follow site is chained, and is back after detach; both dumps on the sim thread; grid, points, ids, start frames, off-sim classification (fill under the build = yes, direct remove = no), follow and houseboat rows; no probe errors; with no plan for two 30 s checks the run fails with that reason; the dummy's threads run on after the end and after unload. **PASS 2026-10-18.** `node tools/harness/live-reads2-report.ts --self-check` runs the grid walker on fake memory (sentinel, no-lot records, element count, a bad vector, a bad bucket pointer), the point grid, the corner test and every verdict rule.

**Fail-fast.** A progress line every 30 s (counts per hook, dumps, minutes left). The start dump needs one `0x85FF30` call: two checks with no plan, EditPath or follow call end the run with `fail: no route plan for 60 s`. `profile-world.ts` kills the Frida process 90 s past its limit. The game window is never touched by the probe.

## 3. How to run and read

```
node tools/harness/queue.ts tools/harness/queues/live-reads2.json --dry-run
node tools/harness/queue.ts tools/harness/queues/live-reads2.json
node tools/harness/live-reads2-report.ts probe-out/profile/live-reads2-<date>-bp-live-reads2-livereads2.json probe-out/profile/live-reads2-<date>-ip-live-reads2-livereads2.json --game-copy <main checkout>/game-copy
```

Jobs: `bp-live-reads2` (Baseline-BP-Spring) and `ip-live-reads2` (Baseline-IP-Spring, repro `ip-houseboat-gohome` at the window start), asi `tested`, `RouteWorker.Enabled=0`, `RouteAsync.Enabled=0`, profile 5 min seeded 12345 with `livereads2: true`; ~8 min each with load and teardown, ~16 min for the queue. Run from the main checkout (the worktree has no tested build; the dry run says so). Files per job: `<tag>-livereads2.json` and `<tag>-livereads2.log`. The report picks `Bridgeport.world` / `IslaParadiso.world` from `<game-copy>/worlds` by the `-bp-` / `-ip-` tag (or `--world`), and names return addresses from `<game-copy>/analysis/functions.csv`.

## 4. Expected verdicts

| Row | Expected | Refuted when |
|---|---|---|
| I-107-15 | non-empty after a share of `Route_PlanCommon` calls (Sims leaving chairs, beds, counters); `ids` = `path+0x60` in every call | always empty |
| I-107-16 | route list empty at every PlanCommon return | any non-empty |
| I-107-17 | 1-2 ids per list, often the follower's target object; the follower's own id rarely [inferred] | stays open here: the file has the ids, not the object kinds (resolve them with the agent's object lookup in a follow-up) |
| I-107-19 | cells = formula (Bridgeport and Isla Paradiso: bounds from the file); records far fewer than footprints; element counts = walk; 0 cells possible if no non-full reset sized it yet (lazy size) | other cell count |
| I-107-20 | every fill and remove on the sim thread in this config; callers `PathPlanner_AddFootprint`, `LotRouting_RemoveObjectFootprints`, some `LotGraph_Build` | an off-sim call not under `LotGraph_Build` |
| I-107-21 | every answer = (byte < 128 at floor x, z) or a lot there; points on both sides of the mask | any mismatch |
| I-107-24 | one return address, in the behaviour manager's per-tick update (a vtable call) | — (it answers the question either way) |
| I-100-13 | IP: plans of the houseboat households start beyond `WM+0xC0` (slot frame); BP: none (control) | IP: none while the repro ran = display frame, or nobody planned on board (read the repro log) |

## 5. Limits

- Plain counts and ids; object kinds of the `O+0x58` ids are not resolved (I-107-17 stays open).
- Grid read from the planning thread while a job could read it: reads only, guarded; a torn chain gives an error entry.
- Off-sim stack test is a scan of code words on the stack, not an unwound chain: a stale `LotGraph_Build` address above the caller could mark a call "under the build" wrongly (false verified); the caller histogram is the cross-check.
- Houseboat lots parented before the window (at world load) are not in the corner list; I-100-13 then rests on the `z > WM+0xC0` test alone (only houseboat slots lie there, docs/100 §12.2 [verified, emulator]).
- 20 points cannot show row-order errors in a mask region smaller than the grid spacing.
- Unload guard (2026-10-18, after the `bp-f4b-street` crash at unload, docs/99 §12.7, docs/51 §81): the `0xC6BAB0` / `0xC68990` calls release the JS lock, so `finish()` (timer or dispose) sets `closing` first (the point loop checks it before each point and stops), waits up to 2 s until no thread is inside a call, then detaches and flushes. The file records `unloadWait` (`ms`, calls still in flight). A dump cut short by the unload keeps the points read so far and `points.closing: true`. Dummy test section 4 (`tools/probe/live-reads2-test.py --race`): slow copies of both call targets, 5 unloads through a `frida_tools.repl -t 1` host while thread A is inside the start dump's calls; the fixed script waited 13-76 ms and the dummy survived every time. The old script also survived, but its file lost the dump in progress (`dumps: []`).

## 6. Results

Not run yet.

| Job | Run | I-107-15 | I-107-16 | I-107-17 | I-107-19 | I-107-20 | I-107-21 | I-107-24 | I-100-13 |
|---|---|---|---|---|---|---|---|---|---|
| bp-live-reads2 | | | | | | | | | |
| ip-live-reads2 | | | | | | | | | |
