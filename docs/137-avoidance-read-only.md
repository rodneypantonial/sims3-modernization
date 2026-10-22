# 137. Avoidance steering read-only (`[AvoidRO]`, key `0x846D21`)

Design 2026-10-21, **built offline the same day (§11; counters v77 (built as v74, moved at the 0.36.12 merge), ts3native 0.36.12; no game run yet)**. Design pass: static work on `game-copy/bin/TS3W.exe` (Steam 1.67), no build, no game run. Sources: Ghidra headless (`DecompileTo`, `CallerTree`, `StoreScan` on a project copy), capstone 5 over the exe, the docs/82 vtable table, `native/ts3native/steerro.cpp` (S2). Tags as in AGENTS.md; ledger rows in docs/51 §192.

## 1. Why now

`wait-reducers-ab` (2026-10-21, BP, PLAN.md): with S2 `[SteerRO]` on, the steering key `0x85FE10` fell 21.5 -> 11.3 ms/s, but the avoidance key `0x846D21` rose 9.9 -> 17.4 ms/s (S2 + gate-0 fix: 8.1 -> 12.2). Both keys are `+0x918` acquires of `RoutingWorld_ObstacleQuery 0x82EDB0` in the same per-agent locomotion update: once S2 answers the first one, the sim thread reaches the second one while the job still holds the lock and waits there. Avoidance has no reducer (docs/116 §4.1). This doc maps the avoidance path and designs one.

## 2. The path to `0x846D21` [verified]

`0x846D21` is the return address of the `call 0x82EDB0` at `0x846D1C` in **`RouteFollow_A 0x846B70`**.

| Caller of `RouteFollow_A` | Site | When |
|---|---|---|
| `ObstacleAvoidance 0x847390` (behaviour vtable `0x1026E04`, slot `+0xC` at `0x1026E10`; built by `0x846820`, run from `Locomotion_UpdateAgent 0x957210`) | `0x847697` | per tick x walking Sim, only when the remaining path distance is under the threshold the function computes (`0x8469F0` against `[0xF9A62C]` or a scaled per-agent value) |
| `IC_Route_GetDestinationObstruction 0x74C820` | `0x74C8A9` | per script call |
| `IC_Route_GetObstructionAtPoint 0x74C8E0` | `0x74C996` | per script call |

All three run on the sim thread. `RouteFollow_A` is `cdecl(path, agent, goal*, ignoreIds*, ignoreCount)`, returns a 64-bit object id in `edx:eax`, standard `ebp` frame, prologue `55 8B EC 83 E4 F0` (the same 6 bytes S2 detours on `0x85FD10`) [verified disassembly]. `ObstacleAvoidance` passes the path it follows, its agent `[this+0xC]`, the path goal (`0x846AE0`), and a `"Locomotion/IgnoreList"` of the agent's own id plus its slave ids; a non-zero answer calls the agent's vtable `+0x64` (the "goal obstructed" notice, docs/81 §4) [verified].

**What `RouteFollow_A` does, in order** [verified decompile and disassembly]:

1. `path` vtable `+0xC` (the options copy `path+0x08`, docs/107 §9) kept in a stack slot; `RoutingWorld_Get 0x8292D0`; lot planner `0x829BC0`; goal level `0x8468C0(goal)`; goal height `0x829470` (when a world exists); its own lot lookup `World_LotLookup 0xC68990` on `[0x11ECBC4]` (the world manager S2 calls `wm`). All before any lock.
2. Option record `{x, height, z, w, 0..., +0x20 = 1.0, +0x24 = level}`; option word = `0x100` when the agent is NULL (or 8, a NULL after the `-8` adjust), else the agent's posture flags `0x953690`.
3. **`0x82EDB0(record, radius 0.0, &vec, mode 1, optionWord)`** (`thiscall` world, `ret 0x14`): takes `+0x918`, does its own lot lookup on `[world+0x964]` (S2's `wmw`), gathers through `0x631F10` -> `LotTable_Contains` (gate 12 rules), `0x62FB10` (level quadtree), `0x62ED40` (transform), `LotQuadTree_QueryPoint 0x667480`, which picks visitor `0x662DD0` (`0x669CD0` with flag 1) for a non-zero mode byte, else `0x662DB0` (flag 0) [verified `0x667493..0x6674A4`]; copies the set into `vec` sorted by sub-object pointer with a plain `+1` each; releases the lock. Returns 0 only when the point is on no lot.
4. **Consumer** (no lock), per footprint sub-object `fp` in `vec`: plain `+1` on `[fp+4]`; type `fp` vtable `+8` == `0x14`: skip; id `fp` vtable `+4` in the ignore list (`IdList_Contains 0x85AAA0`): skip; with an options object, id in `O+0x38` (`0x81C970`) or in `O+0x58` (`0x81C930`): skip; then
   - no lot from step 1's own lookup: **hit at once** (no polygon test);
   - else the goal in lot coordinates through `0x62EF30` (gate 0, `GetOrInsert` hit path), the vector `{u, 0, v, u}`, and for `i` in `0 .. outer slot 0 () - 1` the point test outer slot `+0xB8 (i, &vec)`; any true = hit.
   - hit: the answer is `fp` vtable `+4` (its id); plain `-1` (delete through slot 0 at zero, never reached while `vec` holds its reference); stop.
   - no hit: plain `-1`, next.
5. Release `vec` (`0xAAC090`, plain `-1` each) and free its buffer.
6. A non-zero id returns. Otherwise the **Sim scan**: the agent's own id and posture box (agent vtable `+0x10`, `+0x3C`), then every locomotion agent in the list at `[0x11D9140]+0x40` other than the caller's, not in the ignore list or the options lists, in state 1 or 4, near the goal (squared distances against a box from component `0x935770([0x11E90FC])` flags `+0x38 & 0xCF00`), tested with `0x7CD7E0` against the 4-corner box built by `0x959060`; first hit returns its id. **No `+0x918`, no lock, no store outside the stack and out-parameters** [verified `StoreScan` depth 3: `0x5ECB30`, `0x959060`, `0x5F3D50` write only their out-parameters].

**Point tests** (outer vtable slot `+0xB8`) of the nine footprint classes (docs/82 §3.1), read from the exe [verified]: eight classes `0x80E8A0`, one (`0xFEAE08`) `0xBFB320`, a constant `return 0`. `0x80E8A0` is `thiscall(fp, level i, pt*)`: when the cache entry `[fp+0x38] + i*0x24` has valid byte `+0x20 == 0` it **rebuilds it** (`0x80E5B0`: sets the byte, frees the old vectors `0x61EE20`, clears the bounds, calls slots `+0xF8`/`+0xFC`); then `0x6772E0` loops the entry's 16-byte polygon records with `0x676F70`. With a valid entry the test stores nothing outside its stack and calls no lock or allocator [verified `StoreScan` depth 3]. Outer slot 0 is `0x619EF0` (returns 1) or `0x83F5F0` (returns 3) on all nine classes [verified vtable read], so `i` is 0 or 0..2.

**`RouteFollow_B 0x85B470`** (key `0x85B50C`, 0.2 ms/s, docs/84 §1): `cdecl`, 5 args, converts its point with `0x85AE60`, calls `0x82EDB0(record, 0.0, &vec, 1, arg5)` at `0x85B507`, then returns 1 when any footprint has type `0xC`, `0xD` or `0x17`, else 0; it computes a lot transform but never uses it [verified decompile]. Reached only from the candidate loop `0x85D980 -> 0x85C590 / 0x85BFC0 -> 0x85B670`, which `0x847390` runs **before** `RouteFollow_A` (`0x8474FE`) and `Locomotion_ObstacleList 0x85FFE0` / `0x852700` also reach [verified `CallerTree`].

## 3. What the path reads and writes, and what a running job writes

| State | `RouteFollow_A` / `_B` | Under `+0x918` today | Job writes it? | Read-only answer |
|---|---|---|---|---|
| Lot table `P+0x48`, lot entry transform, level vector | read (`Contains`, `0x62FB10`, `0x62ED40`, `0x62EF30`) | query part yes, `0x62EF30` no (gate 0 frame) | insert on a miss (W5), lot build (W4) | S2's `SrEntry` / `SrCell` / `SrXform` (bit-exact, steerro-test B) under QL shared; a `GetOrInsert` miss = fallback |
| Level quadtree nodes, footprint lists, deferred maps, pin `Q+0x48`, flag `Q+0x4C` | read; `Q+0x48` plain `+1`/`-1`; flush when `< 1` and flagged | yes | yes (W1-W3, pin in `LotPlan_Inner`) | S2's `SrWalk` (point walk, no pin write, gives up on deferred flag or a moved pin) |
| Footprint refcount `[fp+4]` | plain `+1`/`-1` in the query copy, the consumer and the release | query part yes, consumer and release no | yes, plain, in every job gather (docs/81 §3) | **not written**: the clone keeps raw pointers under QL shared and never AddRefs |
| Footprint type / id / class slots | vtable calls | partly | no (whitelisted pure getters, docs/84 §3.6) | S2's `SrSlotsOk` plus `+0xB8` in {`0x80E8A0`, `0xBFB320`} and slot 0 in {`0x619EF0`, `0x83F5F0`} |
| Polygon cache `[fp+0x38] + i*0x24` | read; **rebuilt** when invalid | no (consumer) | yes, W6a-W6g under QL exclusive on the worker | valid byte checked before, 0x24 bytes compared before/after (S2's T3); invalid = fallback |
| World manager lot grids `[0x11ECBC4]`, `[world+0x964]` | read | partly | no (already unlocked, docs/60 §1.3) | as S2: dirty byte `+0x1CC` on either = fallback |
| Path options `O+0x38`, `O+0x58` (the followed path) | read (`0x81C970`, `0x81C930`) | no (consumer, already unlocked today) | **maybe**: `Route_PlanCommon 0x7606E0` copies options into the planned path (`RoutingOptions_Assign 0x81D090` via path slot `+0x10`, `0x760F7A`) and `RouteFollow_EditPathForRoute 0x85FF30` fills `+0x58` (docs/107 §9); on the worker under `[RouteAsync]` (docs/84 §3.4). Whether a Replan job can target the path a Sim is following now is open | the game's own `0x81C970` / `0x81C930` (same reads as today), plus tripwire T-AV (§4.4) on `0x81D090` and `0x85FF30` |
| Ignore list, goal, agent | read | no | no (caller's stack, sim-owned agent) | read directly |
| Locomotion agent list, Sim boxes (Sim scan) | read | no | no | **not cloned**: the game's own code runs it, as today |

Stated plainly: the only reads a job can race are those S2 already guards (lot table, quadtree, polygon caches, lot grids), plus the followed path's two id lists, which today are read unlocked too [verified for the reads; the path overlap inferred, I-192-5].

**Why not design SH (docs/90, run the game's body under a QL-shared hold)?** SH works for `0x76F0A0` because its only serialised reads are gate calls that write nothing on a hit. `0x82EDB0`'s body writes under the lock: the pin `Q+0x48` (plain add, also written by the job's lot plan), the deferred flush, footprint refcounts (plain), stack sets from the allocator. Running it under a shared hold would race the job on all three. The consumer part needs no hold (it is unlocked today). So SH is rejected for this site; the query must be cloned as S2 does [verified writes; I-192-2].

## 4. Design `[AvoidRO]`

### 4.1 Hooks

Off by default: `[AvoidRO] Enabled=0` hooks nothing. `Enabled=1` needs `[SteerRO] Enabled=1` (it reuses S2's QL, the 12 W1-W6 stubs, the W4 gate stub and the 11 armed T4/T-S7 tripwire stubs; without them nothing is hooked) and `[RouteGate] Enforce=1`, `[RouteAsync]`.

| Hook | Bytes | Role |
|---|---|---|
| **A-entry**: detour `RouteFollow_A 0x846B70` | `55 8B EC 83 E4 F0` (6) | run the clone; hit = return the id without the game's body; miss = set `t_avMiss` and call the game's body; fallback = call the game's body unchanged |
| **A-site**: retarget the `E8` rel32 at `0x846D1C` | `E8 8F 80 FE FF` | `thiscall`, 5 args, `ret 0x14`, same as `0x82EDB0`. `t_avMiss` set (and `ecx` = the world the clone used): clear it, return 0 (the caller's vector is still empty: it is created empty on the stack just before). Else jump to `0x82EDB0` |
| **B-entry**: detour `RouteFollow_B 0x85B470` | `55 8B EC 83 E4 F0` (6) | run the clone; answer = return 0/1; fallback = the game's body |

Why the call-site retarget instead of copying the Sim scan: when `0x82EDB0` returns 0 the game's own code skips the consumer, releases an empty vector, and runs the Sim scan, exactly the continuation of a lock-path call whose footprints did not hit [verified flow, §2 steps 3-6]. The Sim scan stays the game's code, so it needs no clone and no shadow. `t_avMiss` is cleared by A-entry after the body returns, so a body that never reaches `0x846D1C` (no routing world) cannot leave it set. Install order: A-site first (harmless while `t_avMiss` is never set), then the detours; any prologue or `E8` target mismatch: nothing is hooked. `node tools/hook-sites.ts --check` must list the three addresses (none of them is hooked today: no hit in docs/68, `native/ts3native`, `tools/hook-sites.ts`).

### 4.2 When the clone runs

Same gates as S2's job path (docs/84 §3.6): owner (sim) thread, a job is running (`A_RUNNING`), the thread does not own `+0x918`, is not serving a job callback, has no gate frame open, `g_steerBroken` clear, tripwires armed (`g_twArmed`, generation read as in 0.18.0). Otherwise the game's call (today's wait, or no wait without a job). `Mode=1` (runtime switch, `--ab avoidro`) uses answers; `Mode=0` with `Shadow=1` only compares (§5).

### 4.3 The clone

**A, phase 1 (no lock, as the original before its lock):** compute the inputs the same way the original does: the options object (`path` vtable `+0xC`, only when `path` is not NULL), the world (`0x8292D0`), level (`0x8468C0`), height (`0x829470`), option word (`0x100` or `0x953690(agent)`), both lot lookups (`wm` for the consumer, `wmw` for the query). These are the same game calls the original makes before `0x82EDB0` and are already unlocked [verified store scan: none of them stores outside its stack]. Dirty lot index on either manager: fallback.

**A, phase 2 (QL shared, try only):** `SrQuery` for a **point walk with flag 1** (today S2 hard-wires point = flag 0, segment = flag 1: one new parameter), radius 0.0, record `{optionWord, 0, ..., +0x24 = 1, +0x28 = 0}` as `0x82EDB0` builds it, lot from `wmw`; the objects come out sorted by sub-object pointer, the copy order of `0x82EDB0`. Then the consumer rules of §2 step 4 in order, on raw pointers: type `0x14` skip; `0x85AAA0` on the caller's ignore list; with an options object `0x81C970` then `0x81C930`; no `wm` lot: first survivor is the hit; else `SrEntry(contains = false)` on the `wm` lot (miss = fallback, since `0x62EF30` would insert) and `SrXform` on the raw goal `x, z`, vector `{u, 0, v, u}` 16-byte aligned, then for `i < slot0()` (1 or 3): cache entry `i` valid byte set (else fallback, the original would rebuild), copy its 0x24 bytes, call the game's `+0xB8` (`0x80E8A0` reads only; `0xBFB320` returns 0), compare the bytes again (T3), true = hit. Pin `Q+0x48` and flag `Q+0x4C` unchanged across the walk, no sim-thread W1-W6 entry, no exclusive QL hold between, tripwire generation and epoch unchanged: else fallback. Release QL.

**A, answer:** hit = return the hit's 64-bit id from A-entry (the game's body does not run). Miss = `t_avMiss = 1`, call the game's body (A-site returns 0, Sim scan as today). The only effects the original has and the clone skips are the paired plain `+1`/`-1` on footprint refcounts, the pin `+1`/`-1` and the stack allocations, none visible after the call returns when no other thread interleaves [verified pairs; I-192-3].

**B:** phase 1 = `0x85AE60` on the point (the original calls it before its lock; S2 already calls it outside), `wmw` lookup; phase 2 = the same point walk, flag 1, option word arg 5; answer 1 when any collected footprint's sub vtable `+8` returns `0xC`, `0xD` or `0x17` (the same getter S2's consumer calls), else 0. B is tiny and removes the next lock the sim thread would meet inside `0x847390` when an avoidance candidate loop runs.

**Caps:** S2's (`kSrCap` objects, local class-2 entries, depth); A has one query and at most 3 point tests per footprint, so no new caps.

### 4.4 Tripwire T-AV (the followed path's id lists)

Two more armed stubs, same mechanism and arming as S2 0.18.0 (`TwGo<i>`, `g_twGo[i]`, counted only while armed, `g_twActive` / epoch / generation): `RoutingOptions_Assign 0x81D090` and `RouteFollow_EditPathForRoute 0x85FF30`, counted when entered on a thread other than the sim. A clone that overlaps one of them drops its answer (`avFbTrip`). Prologue bytes are read at build time and added to docs/68 (`0x85FF30` = `55 8B EC 83 E4 F0 83 EC`, docs/114 §2). This is conservative: it also drops answers when the job copies options into a path no Sim follows yet (once per job, at its tail). If `avTripAv` shows the drops are rare, keep it; if live data proves a job never targets a followed path (I-192-5), the two stubs can go.

### 4.5 Correctness argument

1. **Same inputs.** Every input of the clone is computed by the same game calls on the same thread, or read from the caller's arguments; the query inputs are bit-for-bit those the original passes to `0x82EDB0` (record, radius, mode, option word) [verified layout §2 step 2-3; steerro-test J checks it].
2. **Same set, same order.** Under QL shared the worker's writers of the lot table, quadtree and polygon caches (W1-W6, W4) cannot run; tripwires T4 / T-S7 catch the others; the pin and deferred-flag checks catch a walk that the job interleaved (S2's argument, docs/84 §3.6 and 0.18.0). The visitor rules for flag 1 are the ones S2 already runs for its segment gathers [verified on fake worlds by steerro-test and `tools/emu/specs/ro_point_visitor.py`]; the point walk with flag 1 is new (emu and steerro-test J/L).
3. **Same consumer decision.** The filters are the game's own functions (`0x85AAA0`, `0x81C970`, `0x81C930`, slot getters, `+0xB8`), called in the original order; the transform is S2's bit-exact copy of `0x62EF30`; a polygon test that would write (cache rebuild) is never called.
4. **Same continuation.** Hit: the original would return the same id before the Sim scan. Miss: the A-site makes the original see "no lot" and continue into the Sim scan, which is the same code path as "no footprint hit" [verified flow].
5. **No new writes on shared state.** The clone writes no refcount, pin, flag or allocator state; only its stack, `t_avMiss` and counters. So the sim never blocks the worker while it holds QL shared (as S2: no lock, allocator, managed or event call in phase 2 [verified callees: `0x85AAA0`, `0x81C970`, `0x81C930` pure 24-32 instruction scans, `0x80E8A0` with a valid cache, slot getters already whitelisted]).
6. **Answer age.** The answer reflects the world in the middle of a job; only the job's own in-flight writes differ (S2's argument, docs/81 §4). Footprint moves on the sim thread cannot happen during the clone (it is on the sim thread). The worst stale case is one tick with a late or early "goal obstructed" notice, which the caller re-checks next tick.

## 5. Shadow compare (like `bp-s2cov-shadow`)

`[AvoidRO] Shadow=1` with `Mode=0`, run only with no job running and job starts held off (`g_rgOffSimOpen`, as S2 shadow), stubs disarmed (S2 0.18.0 rule: the answer is never used):

- **Query level** (`avShadowQueryMismatch`): A-entry calls `0x82EDB0` itself into a private vector with the record it built, compares the element sequence with the clone's set (then releases it with the game's `0xAAC090`). Same for B.
- **Answer level, A** (`avShadowMismatch`): run the game's `RouteFollow_A` (lock path) for its id `r`; the clone's footprint answer `c`. `c != 0`: match iff `r == c`. `c == 0`: match iff `r == 0` or `r` is the id of an agent in the locomotion list (the Sim scan answered; walk `[0x11D9140]+0x40` with vtable `+0x10`, read only), counted `avShadowSimScan`. Order mismatches counted apart (`avShadowOrder`).
- **Answer level, B** (`avShadowMismatchB`): game's 0/1 vs clone's.
- Pass: `avShadowMismatch`, `avShadowMismatchB`, `avShadowQueryMismatch` all 0 with `avShadowRuns` in the thousands, 0 watchdog lines, fallback mix printed (`avFb[]`).

Queue jobs (not added here): `bp-avro-shadow` (S2 Mode 1 as in `bp-wr-steerro`, AvoidRO `Mode=0 Shadow=1`, BP 10 min; daytime is fine: correctness only), then `bp-avro-ab` on a clean night: in-load A/B with `--ab avoidro` 2-min slices and S2 on in both halves, read with `node tools/harness/wait-ab.ts` (keys `0x846D21`, `0x85B50C`, and where the wait goes next) and `hitch-attr-report.ts`; IP after BP.

## 6. Counters (reserve ts3native **v77**, docs/registry.md; v73 went to the docs/134 freeze fix, v74-v76 to design U, M1Retry and design G at the 0.36.11 merge)

| Field | Meaning |
|---|---|
| `avReady` | bit 0 A-entry, 1 A-site, 2 B-entry, 3 T-AV stubs installed |
| `avMode`, `avShadow` | runtime switches (`--ab avoidro`, `--ab avoidshadow`) |
| `avTakenHit`, `avTakenMiss`, `avTakenB` | answers used: A hit, A miss (Sim scan ran), B |
| `avSiteSkip` | A-site returns of 0 (must equal `avTakenMiss`) |
| `avFb[10]` | fallbacks: state, QL busy, dirty lot index, lot miss, deferred flag, polygon cache, shape/slot, tripwire, pin moved, job end |
| `avUs`, `avMaxUs` | clone time (rdtsc-scaled as S2's `s2*Kt`) |
| `avTripAv`, `avTwSite[2]` | T-AV foreign entries by site (`0x81D090`, `0x85FF30`) |
| `avShadowRuns`, `avShadowMismatch`, `avShadowMismatchB`, `avShadowQueryMismatch`, `avShadowOrder`, `avShadowSimScan` | §5 |
| `avSimWriteInRo` | sim-thread W1-W6 entry during a clone (= 0) |

`tools/probe/agent.js` names them per version, `profile-world.ts` offsets checked against `selftest.cpp` asserts, as for every version.

## 7. Selftest and offline proof plan

| Check | What |
|---|---|
| `build.cmd` (`/W4 /WX`) | all targets |
| selftest `TestAvoidRO` (fakes) | dispatch: off, not owner, no job, job callback, `+0x918` owner, gate frame, broken, QL busy, disarmed, generation moved, job end; A-site: flag clear = jump through, flag set = 0 once and cleared, flag left set by a body that skipped the site is cleared by A-entry; consumer: type `0x14`, ignore list, options lists (NULL options object), no lot = first survivor hits, kinds 1 and 3, `0xBFB320` class never hits, invalid cache at level 0 / 2 = fallback, T3 byte change = fallback, `GetOrInsert` miss = fallback; B: types `0xC`/`0xD`/`0x17` true, others false; T-AV hits at start and inside a clone drop it; shadow: equal, skewed id, Sim-scan id accepted |
| `steerro-test` section **J** (maps `TS3W.exe`, game code) | the game's `0x846B70` vs A-entry + A-site on random worlds (S2's fake lots, quadtrees, nine footprint classes with polygon caches, paths with options lists, ignore lists, an empty and a 1-3 agent locomotion list); only allocator, lock, `0x85AE60`, `World_LotLookup`, `0x8292D0`, `0x829470` replaced as in sections A-F; pass = equal ids in every compared case |
| section **K** | the game's `0x85B470` vs B-entry |
| section **L** / `tools/emu/specs/ro_point_visitor.py` | the game's `0x667480` with mode 1 vs the `SrVisit` model with flag 1 (point walks with footprints, class-2 locals, type `0x1F` merge) |
| `node tools/hook-sites.ts --check`, `node tools/check-docs-code.ts`, `tools/emu/run.py --self-check` | pass |

## 8. Expected gain [inferred]

- **Key level.** With S2 on, `0x846D21` was 17.4 ms/s (S2 alone) and 12.2 (S2 + gate-0 fix). The clone shares S2's whitelist, so expect S2's fallback mix (31% unknown-slot fallbacks in `bp-s2cov-shadow`) plus few cache fallbacks: **the key drops by ~8-12 ms/s** (to ~4-6 with S2 alone). B adds at most 0.2 ms/s. Part of gate 0 (the `0x62EF30` call at `0x846E09`, docs/86 G-a) also goes, since answered calls never reach it.
- **Net.** The S2 result shows waits move to the next `+0x918` acquirer: of S2's 10.2 ms/s, 7.5 came back at `0x846D21`. With S2 + AvoidRO + B, the locomotion pass has no `0x82EDB0` / `0x82F0B0` acquire left for answered agents; the next sim waits are `UpdateSimFootprint 0x833593` (~7-9 ms/s, docs/89 design U, not built), gate 0 / 12 / 10 in the following script stages, and destroy. Expect **net summed `+0x918` wait -3 to -8 ms/s versus S2 alone** (S2 + gate0 + AvoidRO vs base: about -57 to -62 against S2 + gate0's -54), inside the ~±40 ms/s noise band of summed waits, so read it per key and by it/s, not by the total. The real use of AvoidRO is as a prerequisite: without it, every S2 gain is absorbed here; with it, design U (docs/89) becomes the next lever.
- **Cost.** RouteFollow_A runs only near the path's end, so the clone runs far less often than S2 (`bp-s2-on` had ~205k S2 checks per 10 min); per call cost ~S2's point query (one walk, ≤ 3 point tests per survivor). No tripwire cost beyond the two T-AV stubs, which are armed only during jobs.

## 9. Risks

| # | Risk | Effect | Mitigation |
|---|---|---|---|
| 1 | Wait just moves again (as S2 showed) | no it/s gain | key-level A/B; pair with design U next |
| 2 | Point walk with flag 1 behaves unlike S2's tested flag-1 segment walks | wrong set | emu + steerro-test L before any live run; shadow query level |
| 3 | A job writes the followed path's options (`O+0x38` / `O+0x58`) | stale or torn id-list read | T-AV tripwire drops; I-192-5 probe; today's reads are already unlocked |
| 4 | S2's unknown-slot fallbacks (31%) | lower coverage | whatever extends `SrSlotsOk` helps both; counted in `avFb` |
| 5 | Call-site retarget at `0x846D1C` collides with a future probe | double hook | docs/68 row, `hook-sites.ts --check` |
| 6 | A hit returned while the game's Sim scan would have found the same Sim earlier | none: the original returns the footprint hit before the Sim scan too | - |

## 10. Effort

Wall-clock for Claude with agents: **~5-8 h** build and offline proof (`avoidro.cpp` reusing steerro internals plus the `SrQuery` flag parameter ~2-3 h; selftest ~1 h; steerro-test J/K/L and the emu cases ~1.5-2.5 h; docs/68, registry, agent.js, profile-world offsets ~0.5 h), then **2 game runs**: one daytime shadow (`bp-avro-shadow`, ~15 min job) and one clean-night in-load A/B (`bp-avro-ab`, ~15 min job; IP after).

## 11. As built (2026-10-21, `native/ts3native/avoidro.cpp`, counters v77; v74 on its branch)

Offline only: no game run, no queue run. ts3native version is set at merge. Ledger rows: docs/51 §196.

**What is built** (default off, `[AvoidRO] Enabled=0`):

| Piece | As designed (§4)? | Notes |
|---|---|---|
| A-entry detour `0x846B70`, A-site rel32 at `0x846D1C` -> `AvSite`, B-entry detour `0x85B470` | yes | install order T-AV, A-site, A, B; every byte checked first (both prologues, `0x85FF30`, `0x81D090`, the `E8` and its target) |
| Clone A: phase 1 unlocked, phase 2 under QL shared (try only) with S2's protocol | yes | `AvLocked` = OrLocked's protocol plus the T-AV active/epoch pair; the query is `SrQuery(..., mode1 = true)` |
| Consumer A | yes, plus | an answer with id 0 is treated as a miss (the original stops its footprint loop and then runs the Sim scan, the same path) [verified `0x846F1D`]; the cache is checked only when slot `+0xB8` is `0x80E8A0` (the `0xBFB320` class stores floats at `+0x38`, I-94-203) |
| Miss = the game's body with the site skipped | yes | `t_avMiss` 1 (counted in `avSiteSkip`) or 2 (shadow, not counted); cleared after the body in every case |
| Clone B | **corrected** | B calls `0x62EF30` (GetOrInsert) whenever the query found a lot and its wm has a lot at the raw point: a miss there inserts (§2 said "never uses it": true for the result, not for the side effect) [verified `0x85B536..0x85B579`]. So B needs `SrEntry(contains = false)` to hit, else the game's call (`avFbLotMiss`) |
| Phase 1 dirty check | **added** | `0x8468C0` (goal level) runs its own `World_LotLookup` on `[0x11ECBC4]` [verified `0x8468FB`], so the dirty-index check comes before it |
| No routing world | not in §4 | the original goes straight to its Sim scan without a lock: the hook calls the game's code and counts nothing |
| T-AV | **changed** | two C wrappers (count, call the trampoline, leave) instead of `TwGo` counting stubs: no `RgPush` frame and no new routegate release kind. `0x81D090` thiscall(src) `ret 4`, `0x85FF30` cdecl(3 args), no register input but `ecx` [verified]. Counted off the sim thread while armed, a job runs, or a clone is open; a clone drops on an active one or a moved epoch |
| Shadow A | **changed** | instead of walking the locomotion list (§5), a clone miss runs the game's body once more with the site skipped (`t_avMiss = 2`) and compares with the first call: exact, on the game's own Sim scan, which stores nothing outside its stack [verified store scan, §2 step 6] |
| Shadow query level | yes | `0x82EDB0` mode 1 radius 0 into our vector vs the clone's set (A and B); set equal / other order / differ |
| Counters | v77 (built as v74 at +0x3174, moved after v76 at the 0.36.12 merge: avReady +0x31EC .. avSimWriteInRo +0x325C) | `avFb[8]` pin moved = a tripwire drop with `steerPinMoved` moved; `avFb[9]` job end = disarmed at or during the clone |

**Offline gates (all pass)**

| Gate | Where | Result |
|---|---|---|
| G-AV1 bytes | steerro-test AV | prologues of `0x846B70`, `0x85B470`, `0x85FF30`, `0x81D090`; the `E8` at `0x846D1C` to `0x82EDB0`; 12 call edges of A (incl. `0x8468C0`, `0x829470`, `0x953690`, `0x85AAA0`, `0x81C970`, `0x81C930`, `0x62EF30`, `0xAAC090`) and 4 of B; QueryPoint's mode byte -> visitor bytes `0x667493..0x6674A3` |
| G-AV2 mode-1 point walk | steerro-test AV | the game's `0x82EDB0` with mode 1 vs `SrQuery(mode1)`: 1600 of 1600 equal per seed (radius 0 in 1/4, 0.2-6 otherwise; 567-622 footprints, 30-46 walks holding a child footprint), 0 fallbacks |
| G-AV3 RouteFollow_A | steerro-test AV | the game's `0x846B70` (its own query, consumer, `0x62EF30`, `0x80E8A0`, Sim scan on an empty agent list; `0x8468C0` and `0x829470` faked) vs `AvRunA`: 1600 of 1600 equal per seed (131-155 footprint ids, 26-44 of them via no goal lot, 0-3 from a child; the rest none -> Sim scan); paths with and without options lists, ignore lists, agent NULL / posture words |
| G-AV4 RouteFollow_B | steerro-test AV | the game's `0x85B470` vs `AvRunB`: 1538-1541 of 1600 equal (50-59 answer 1), the rest `SR_FB_LOTMISS` (lot 2 not in the table: the game inserts) |
| G-AV5 hooks on the image | steerro-test AV | A-site + both detours: per case the hooked call during a job equals the trampoline call without one (30-44 hits, 356-370 misses with `avSiteSkip == avTakenMiss`, 384-386 B answers per 400); shadow 800 runs, 0 mismatches of any kind; QL held by a "worker": 50 of 50 calls fall back to the game's code with the same answer |
| G-AV6 dispatch and counters | selftest `TestAvoidRO` | every gate (off, not owner, no job, job callback, `+0x918` owner, gate frame, broken, QL busy, disarmed = job end, no world), consumer rules (type `0x14`, ignore list, both option lists, NULL options, agent NULL / 8 -> `0x100`, no goal lot = first survivor, 1 and 3 levels, invalid cache at level 0 / 2, cache changed during the test, unknown `+0xB8`), B types `0xC`/`0xD`/`0x17`, B lot miss, A-site flag 0 / 1 / 2, a body that never reaches the site, T-AV counted only off the sim thread while armed (epoch and active), shadow equal / skewed id / game hit vs clone miss / Sim-scan answer / query order / query set; v74 layout asserts |
| Seeds | steerro-test | 400 cases seeds 1, 2, 8 and 3000 cases seeds 1-4 pass (`steerro-test ok`). At 400 cases seeds 3-7 stop at the same assertion in this build as in the main checkout's binary (K8's reach line for 3 and 4, section A's id-count threshold for 5, a K8 `assert(false)` for 6 and 7), all outside AV, whose gates pass on 3, 4, 6, 7 before that; children draw from their own generator and the AV section restores `g_seed`, so every older section sees the worlds it saw before |
| Repo checks | | `build.cmd` (`/W4 /WX`, selftest ok); `node tools/hook-sites.ts --check` lists every new address in docs/68 (the 6 missing are older); `node tools/check-docs-code.ts` ok; `node tools/harness/queue.ts --self-check` ok; `--dry-run` of `queues/avoidro.json` parses |

Not done from §7: the emulator spec `tools/emu/specs/ro_point_visitor.py` (G-AV2 compares the game's own query on the mapped image instead, which is the stronger check).

**Queue** `tools/harness/queues/avoidro.json` (not run): `bp-avoidro-shadow` (S2 Mode 1 Shadow 1, AvoidRO Mode 0 Shadow 1, BP 10 min; correctness, daytime is fine; also the live check of S2's child-footprint whitelist, docs/84 §3.6), then `bp-wr-steer-avoid` (S2 on in both halves, `--ab avoidro`, off-first) and `bp-wr-steer-avoid-onfirst` (same, `abFirst: "on"`: slices on,off,off,on), per docs/136 §5. `profile-world.ts` got `--ab-first on|off` and `queue.ts` `profile.abFirst` for that. The asi path `game-copy/ts3native.asi.avoidro` is to be parked at merge.

## 12. Why `avTakenHit` is 0, and why there is no gain (2026-10-22, clean pair + daytime pair, rofix build)

All numbers measured from `probe-out/profile/clean-night-routing-extra-2026-10-22-bp-avoidro-{offfirst,onfirst}` and `avoidro-2026-10-21-bp-wr-steer-avoid{,-onfirst}` (`-ab-native.jsonl`, `wait-ab.ts`), pair means of an off-first and an on-first job.

**It answers [verified, counters].** `avTakenHit` counts only answers where a footprint at the goal point gives an id. A miss is also an answer: the clone ran under QL shared, found no footprint, and the A-site skipped the game's `0x82EDB0` call, so that call's `+0x918` wait is gone (`avSiteSkip` = `avTakenMiss` in every slice: 72-91 per 2-min on slice clean, ~480-600 with S2 on; `avTakenB` 10-43; fallbacks 0-4 per job). The goal point of a followed path almost never lies inside an obstruction footprint, so 0 hits is the expected mix: the 2026-10-21 shadow job (`probe-out/queue/avoidro-2026-10-21-bp-avoidro-shadow-native.json`) compared 43,186 calls with the game's own answer: 0 mismatches, 294 answered by the Sim scan.

**The key does drop [verified].** The A-site retarget made the probe-P0 key of every pass-through `0x82EDB0` wait AvSite's own return address inside the asi (`0x6F28F468` clean, `0x6D37F468` daytime), not `0x846D21`; that is why `0x846D21` was missing from every table. Read under that key, the clean off-first job: off slice 1 104 waits / 1.19 s (~9.9 ms/s), on slices 2-3 two waits, off slice 4 64 / 0.83 s; daytime with S2 on: ~23 ms/s off, ~0 on.

**Measurement gap [verified].** Probe P0 keeps 32 keys first-come (`WaitNote`); a key first hit after they are full only counts in `blockOther` (no time). In the on-first jobs the avoidance key never got a slot (the warm-up slice is on, so it had no waits), so the on-first off half lost the avoidance time; the draft's "wait +6.0 ms/s" is the off-first +10.9 averaged with an on-first +1.2 that lacks it. Corrected with the off-first job's ~9 ms/s [inferred]: on-first ~-8, pair mean ~+1.5 ms/s.

**Where the wait went [verified per key].** Steering `0x85FE10` (S2 off on the release base, `steerReady` 0): 17.0 -> 23.9 and 15.7 -> 25.4 ms/s, +8.3 pair mean, against the avoidance key's ~-8 to -10. The sim's summed keyed wait is conserved, as docs/143 §8.14 found for the unseen throttle: the next `+0x918` taker in the same locomotion pass waits out the same job hold. it/s +4.7% / -6.8% (pair -1.0%), within the BP pair noise (standard deviation of pair means 5.0% over 10 BP switch pairs of 2026-10-21/22, docs/136 §6).

**Verdict: no gain by design.** AvoidRO alone cannot cut the sim's wait while another taker follows it in the same tick. What would: a shorter or lent job hold (P5b `ReleaseLock`, P5c `FreeLock`), H2 (docs/86, no job start during the locomotion pass), or every taker in the pass lock-free at once (S2 + AvoidRO + SlotEnabledRO + docs/140 + design U with phase 2), measured as one bundle.

**rofix build (ts3native 0.36.19 + rofix, `game-copy/ts3native.asi.rofix`, sha256 `10c2c9d21e445086b89e8afb68f2987279a906436e8555aa60f161a2f11cb2fe`):** no feature behaviour change. `AvSite` sets `t_avSite` around its pass-through call and `rtsites.cpp` keys that steering frame `0x846D21`; `SeedWaitKeys()` gives 14 A/B keys (`0x85FE10`, `0x62EF7A`, `0x62F782`, `0x833593`, `0x2000003`, `0x846D21`, `0x8334CC`, `0x760BFF`, `0x62FB74`, `0x631814`, `0x631F40`, `0x83156D`, `0x82D296`, `0x85B50C`) their slots at start; `wait-ab.ts` prints untimed waits per minute per side. Selftest: `TestSeedWaitKeys` and an AvSite-key case in `TestG11CloseBy` (selftest ok). Queue `tools/harness/queues/rofix-ab.json`: `bp-avoidro-verify` (day, 5 min, Shadow=1 + Mode=1, counter pass rules), then `bp/ip-avoidro-offfirst/onfirst` (clean night). Ledger docs/51 §218.
