# 177. Route job "rest of job": goal candidate generation, the duplicate ring, and the footprint refcount race

Written 2026-10-23. **Static + existing run data only, no game run.** P-G, D1 and the H2 IC window were built later the same day (§6, not run). Follows the route job `+0x918` release study (PLAN.md 2026-10-23: no further safe release beyond TailUnlock and DoorMemo; the way left is shorter jobs). Sources: `[RouteJobPhases]` dumps of 56 runs (`probe-out/queue/*-native.json`, 2026-10-22/23), `SyncPlans` log lines of 73 logs (6,429 sim-thread plans), 130 hitch files of the same days, capstone on `game-copy/bin/TS3W.exe` (Steam 1.67), the local decompile of `SimIFace` / `ScriptCore` / `Sims3GameplaySystems` (read only, nothing copied here). Ledger rows: docs/51 §287. Tags: **[verified]** = code, bytes or data; **[inferred]** = derived, method named; **[measured]** = runs.

## Summary

- **The 221.8 ms IP max is not attributed and is not typical [measured].** In 56 phase-timed runs, "rest of job" is 10-22% of the hold and 1.0-2.5 ms per job; its max is 13-80 ms in every BP and SV run. Only two IP runs go above 200 ms (verify 221.8, natural-30 206.3). Foreign jobs, which log a per-job split, never show more than 11 ms outside plan and host calls. Sim-thread IC plans, which include goal building, show at most 38 ms "other" in 729 logged plans. Goal validation cannot be confirmed or ruled out for the IP outliers without a split (§5, probe P-G).
- **Goal generation validates every point, with no early stop and no cap [verified].** A radial-range destination makes rings of points between min and max radius. Each point goes through `GoalPoint_Validate 0x759B20`: steering query `0x82EDB0` with its hit loop, goal footprint gather `0x82F0B0` with its hit loop, an optional line-of-sight raycast, and an optional physics overlap. Points that pass go to the candidate list, points that fail to the non-candidate list. The multi-goal search then picks among all of them by cost.
- **Every radius route validates its ring twice [verified].** The script's `PlanToPointRadius` / `RouteToObjectRadius` / `RouteToPointRadius` pass max radius -1, and `RouteToDynamicObjectRadius(r)` passes min = max. In the native generator `0x75BE60`, max <= min sets the ring count to 1 with a radius step of 0. The ring loop then runs `i = 0..1`, so it calls the ring builder `0x75B610` twice with bit-identical inputs. The only argument that differs is a flag that `0x75B610` never reads. The script sets the spacing to πr/8.5, so a full ring has 17 points (16 distinct, because the first and last angles coincide). A radius route therefore costs 34 validations plus one facing point, where 18 would do.
- **Smallest exact reduction, D1 (§3):** change one immediate in the game code (`0x75C05C`: ring count 1 to 0 on the max <= min branch only), so the identical second ring is not built. The candidate and non-candidate lists keep the same values in the same order; only the trailing exact copies go. The chosen goal value is the same, and so are the blocker id and the arc end points. Gain: about 49% of goal validations on every radius route (~17 of 35). With an inferred 20-60 µs per validation, that is ~0.3-1 ms per radius job, roughly a third to a half of the measured 1.0-2.5 ms "rest of job" mean. Expected change in sim-wait hitches at >= 100 / 300 ms: **about zero.** Those waits come from jobs that are long in cell A*, world A* or host calls, and D1 takes at most ~1 ms off any job. D1 qualifies as a proven-safe rare-case bundle item (lower worker hold ms/s, shorter average `+0x918` waits), not as a hitch fix.
- **Footprint refcount race: real [verified code], rare [inferred], present with the default settings.** On the sim thread, `RouteFollow_A 0x846B70` (and `RouteFollow_B`, `RouteFollow_EditPath`) bumps and drops footprint refcounts `[fp+4]` with plain, unlocked read-modify-writes *after* its steering query has released `+0x918` (`0x846D50` +1; `0x846E76` / `0x846EAE` / `0x846ED9` -1 with a delete at 0; the hit vector's release `0xAAC090` from `0x846F07`). A route job starts right in that gap (docs/86), and inside the job the worker does the same plain writes on footprints from the same quadtree: goal validation `0x75A686` / `0x75A6E3` / `0x75A908` / `0x75AA4E` / `0x75AB34`, the query's own copy `0x82EFB6` / `0x82EFF6`, EditPath `0x85F7DE` / `0x85F8E5` / `0x85F916`, and `0xAAC090`. The whole-job hold does not order a write on a thread that holds no lock. A lost +1 means an early delete (use-after-free); a lost -1 means a leak. `[RouteAsync] H2=1` (docs/149, built, off, not run) closes the window for the locomotion pass. The script-IC callers (docs/80 A4) stay exposed. No crash on record matches yet. **§7 (built, not run): `[RouteAsync] RefcountGuard`** keeps the query's `+0x918` level on the sim thread until its caller returns, which closes the race for all 12 callers, also with TailUnlock / P5b lends (`0x75A6E3` turned out not to be a refcount write).

## 1. Where "rest of job" goes: the data

### 1.1 `[RouteJobPhases]` over 56 runs [measured]

| Set | Runs | Rest share of hold | Rest per job (mean) | Rest max (one job) |
|---|---|---|---|---|
| BP (verify, natural, repros, loops, fresh/reused) | 50 | 10.3-21.7% | 1.02-2.11 ms | 13.3-73.5 ms |
| SV (natural-30, rcr-diner-crowd) | 2 | 15.7-17.1% | 1.59-1.70 ms | 29.8-67.6 ms |
| IP (verify, natural-30, cellmemo, routealloc) | 4 | 16.8-18.9% | 1.85-2.45 ms | **221.8**, **206.3**, 79.5, 84.0 |
| Verify job, BP / IP (asi.0326) | - | 18.8% / 18.7% | 1.07 / 2.38 ms | 49.3 / 221.8 ms |

`rjpMaxUs` is a max since game start, so two queue jobs in one game process show the same value (`fvr-fresh-1` / `fvr-reused-1` 73.5). The IP verify run logs four own-START jobs of 204-243 ms (`route async: long job`, IC kind 0). Rest max 221.8 + other phases fits the 225 ms one, but the log does not split own jobs. IP natural-30 logs 33 own-START jobs >= 200 ms (up to 750 ms), also without a split.

### 1.2 Jobs and plans that do log a split [measured]

- **Foreign jobs** (`route async: long job ... foreign START ... plan P host H other O`, 59 lines over all queue logs, 17 in IP natural-30): `other` = job minus plan minus host is at most 11 ms on every line. It goes negative when host calls nest inside the plan. These are `ReplanFromPosition`-type jobs that go through the same IC body.
- **Sim-thread IC plans** (`route phases: sync plan`, `SyncPlans=1`, 729 IC plans out of 6,429 logged): the session covers the whole IC, goal building included, and `other` is IC code outside every timed phase. Max 38 ms (SV), median 0. The `Los` plans are radial routes with `ForcePhysicsBasedRaycast`, which stay on the sim thread (I-105-3) and run raycasts per goal point; they show `other` 21-26 ms out of 39-52 ms. The `Route_PlanLocked (no IC)` sessions (5,700) have no IC around them, so their `other` is 0 by construction.

So the sync data puts goal building plus the rest of an IC at <= 38 ms. A 221.8 ms worker "rest" needs either a destination set much bigger than anything the sync path logged, or time that is not goal work at all (descheduling, a lock or wait outside the 15 timed sites, a goal step `0x75C2D0` portal search, or a lot destination `0x74FB00` -> `0x655EE0`). [inferred: nothing in the data separates these.]

### 1.3 Sim waits behind jobs [measured]

130 hitch files of 2026-10-22/23: frames >= 100 ms where the `+0x918` wait (`rtLock`) is >= 50 ms number BP 3,351 / IP 1,275 / SV 602. Frames >= 300 ms with `rtLock` >= 150 ms number BP 33 / IP 94 / SV 51. The leading keys are `0x85FE10` (steering list), `0x8334CC` / `0x833593` (UpdateSimFootprint), gate 0 `0x62EF7A`, and F-G `0x631F40`. The goal validation key `0x75A64C` leads 177 frames >= 100 ms; those are the **sim's** own sync goal building waiting for a job, not worker goal time. The two verify runs (BP, IP) have no frame with `rtLock` >= 50 ms in their 180 s hitch windows.

## 2. Goal candidate generation, statically [verified unless marked]

Call chain on the job: `Route_PlanCommon 0x7606E0` -> `Route_GenerateDestinationCandidates 0x751FF0` (each destination's slot `+0x10`, docs/101 §4.1) -> radial range `0x75BE60`.

| Step | What it does |
|---|---|
| `0x75BE60` setup | target transform (`0x938B50` / `0x939560` / `0x935770` / `0x958EE0`), lot/level (`0x74BF10` / `0x74BE50`), `0x74F760`. If the height difference between the passed point and the centre is < 1 [meaning inferred], one **facing point** via `0x75AFA0`, which validates once with `0x759B20` |
| ring count | max > min: `max(2, round((max - min) / spacing))` rings plus one, so radii run min..max inclusive. **max <= min: count 1, step 0** (immediate at `0x75C05C`) |
| ring loop | `i = 0..count` inclusive (`0x75C180..0x75C25A`); radius `i * step + min`; calls `0x75B610` with 16 arguments. The 16th (`[ebp+0x44]`, a first/last ring flag) is never read in `0x75B610`. If no ring hit the preferred radius, one more ring at that radius |
| `0x75B610` points | `N = max(1, floor(span * r / spacing + 0.5))`, angles `a0 + k * span / (N - 1)`. For a full circle (span 2π) point `N-1` repeats point 0 up to float rounding. Each point gets terrain / floor / water height snaps (`0xC68990`, `0xADD7A0`, `0xB015B0`, `0xB01060`, `0x829470`, `0xC61670`), then `0x759B20`. Pass: push to the candidate vector (`0x40`-byte records). Fail: non-candidate list (`0x74C650`). Loop `0x75B800..0x75BE4B`, no exit before `N` |
| `0x759B20` per point | level/lot and room checks (`0xB01390`, `0xB06A10`, `0xB499D0`, `0x6243D0`, `0x66DA10`, `0x627540`); **steering query `0x82EDB0`** (`0x75A647`, takes `+0x918`, held already on the job) and its hit loop with virtual calls (`0x75A672..0x75A92E`); **goal footprint gather `0x82F0B0`** (`0x75AA11`) and its hit loop (`0x75AA40..0x75AB5A`); line of sight `World_CanSeeObject 0x789DD0` (only with `ForcePhysicsBasedRaycast`, docs/105 §6); physics overlap query `0xAC8E80` with the loop `0x75AEC2..0x75AEE6` |

**What the script asks for** [verified, decompile read locally]: `PlanToPointRadialRange` sets spacing `π * r / 8.5` (r = min radius, or half the range when min <= 0), clamps it to the range width, and widens it when the script's own estimate passes 100 points (`GetSpacingToGenerateNPoints`). So a full ring has **17 points at any radius**, and a range destination about 100-200 (the script's estimate and the native count differ). Defaults: cone 2π, min 1, max -1, preferred -1.

**Candidates per plan** [inferred from the code; not measured]:

| Route | Validations today | Distinct points |
|---|---|---|
| `RouteToObjectRadius` / `RouteToPointRadius` / `PlanToPointRadius` (max -1), `RouteToDynamicObjectRadius(r)` (min = max) | 2 x 17 + 1 facing = **35** | 16 + 1 |
| `PlanToPointRadialRange(min < max)` | ~100-200 + 1 | the same, minus 1 per full ring |
| Point / slot-array destination | 1 / one `RouteToObjectCheck 0x831530` per slot | same |

Literal radii at `RouteToObjectRadius` call sites in the base game scripts (68 of 153): 1 m x18, 0.7 x11, 0.5 x10, 2 x7, 1.5 x5, 3 x5, 5 x3, up to 15. Radius routes are the common way to walk up to an object (153 + 26 + 57 + 13 call sites, plus the dynamic variants), next to 219 `PlanToPointRadialRange` sites.

**Cost per validation** [inferred, not measured]: two quadtree queries, each with a per-hit loop of virtual calls, plus a physics overlap. Without raycasts, an estimate of 20-60 µs in a busy lot (no measurement of either query's cost per call exists). At 35 validations, 0.7-2 ms per radius job matches the measured 1.0-2.5 ms rest-of-job mean. Raycast routes run on the sim thread and are not in the worker's number.

## 3. Design D1: skip the identical second ring

**Change:** at `0x75C05C` the game stores ring count 1 when max <= min. Store 0 instead. This is one immediate byte in a 7-byte `mov dword [esp+0x20], imm32` (the bytes there match the Steam 1.67 exe [verified]). The max > min branch writes its own count at `0x75C0E4` and is untouched. No detour, no new state. It belongs in ts3native's byte-patch table behind a switch (`[GoalGen] SameRing=1`, default off until a run; name and counter slot to be reserved in `docs/registry.md`). There is no other hook at `0x75BE60..0x75C2CB` in docs/68.

**Why the result is exact** [verified unless marked]:

1. With step 0, ring `i = 1` has radius `1 * 0 + min = min`, bit-equal to ring 0. Every other argument of `0x75B610` is the same value or the same pointer (centre, lot word, angles `a0` / `a1`, the `0x74F760` value, the output lists and records). The one argument that differs (`[ebp+0x44]`) is never read.
2. `0x75B610` reads only those arguments, init-once statics (`0x11D4BA0`, `0x11D4B90`) and the world. Within a job the world is the job's (the worker holds `+0x918`); on the sim thread nothing runs between the two rings. So ring 1's validations repeat ring 0's answers. On the worker the sim can move objects between the two rings, so today's second answers can differ by timing. Skipping them yields the answer the game gives when the rings run back to back, which is one of today's possible outcomes [inferred].
3. The candidate vector gets ring 0's records and then exact copies of them. Dropping the copies keeps every value and the first-occurrence order. Whatever rule the search uses to pick among equal-cost goals, the picked value is the same: a first-wins rule picks the same record, and a last-wins rule picks a copy with the same point, facing, cost, lot and level words [inferred: the consumer was not traced past the vector]. The open-ground goal loop already skips repeated waypoints (`W+0x100` tried set, docs/142 §2.2), so the search does no extra A* for copies today either.
4. Side outputs: the result's blocker id is written only while unset, so the first failing point of ring 0 sets it either way. The arc end points (`[ebp+0x38]` / `[ebp+0x3C]`) get the same values. The non-candidate list loses only exact copies. The preferred-radius extra ring depends on whether *any* ring hit the preferred radius, and ring 0 already decides that.
5. Line-of-sight routes (sim thread only): ring 1 repeats ring 0's paired `Lot_SetDisplayLevel` raise/restore and its quiet event 68 pair (docs/105 §6.3). D1 drops those no-op pairs. That is the only behaviour difference. To make D1 byte-exact for them as well, gate it on `route+0x3C` not having `0x2000000`; that needs a detour instead of the byte patch.

**Rejected:**

| Option | Why |
|---|---|
| Drop the repeated full-circle end point (point `N-1` = point 0) | `fsin` / `fcos` of `a0` and `a0 + 2π` differ in the last bits, so the candidate differs by ~1e-7 m and the validation answer is not provably equal. Saves 1 of 17 per ring. Possible later with a live equality check. |
| Stop at the first valid point, or cap the count | The search picks by path cost plus goal cost, which only the search knows. A cap changes the chosen goal. |
| Memo of validation answers within a job | After D1 the only exact repeats left are destinations that overlap across the route's destination list. Nothing seen in the script calls suggests that is common. |
| Validate lazily (only goals the search reaches) | Not exact: the non-candidate list and the blocker id feed `GetDestinationObstruction` and the failure result. It also needs a search change. |

**Expected effect** [inferred]: about -17 validations per radius job, roughly -0.3 to -1 ms of worker hold per radius job, and the same per sim-thread radius plan. In hold ms/s that is a few ms/s if radius routes are a large share of jobs (the share is unknown, probe P-G). On sim-wait hitches: a wait behind a job gets shorter by at most the job's saving (<= ~1 ms), so only frames within ~1 ms of a threshold move. Expected change in frames >= 100 ms or >= 300 ms per 10 min: **0 within noise.** The two IP outliers are not explained by D1.

**Tests (offline first):** a selftest that runs `0x75BE60` on a fake radial destination twice (switch off and on) with a stub `0x759B20` counting calls, and compares the candidate vectors (on = off with the trailing copies removed, values bit-equal); `tools/hook-sites.ts --check`. Then one bundle A/B (`--ab sameRing`) with the move-to-object repro and the hitch-ab moved-waits table. Effort: ~20-40 min agent build plus one 10 min A/B job.

## 4. The footprint refcount race (lead from the release study)

**Code** [verified, capstone]:

| Thread | Function | Plain RMW on `[fp+4]` | Lock state at the write |
|---|---|---|---|
| sim | `RouteFollow_A 0x846B70` | `0x846D50` add 1 (bytes `83 46 04 01`); `0x846E76`, `0x846EAE`, `0x846ED9` sub 1 with deleting destructor at 0 | **none**: `0x82EDB0` acquired at `0x82EDD0` and released at `0x82F098` before returning to `0x846D21` |
| sim | hit vector release `0xAAC090` (from `0x846F07`) | `0xAAC0A9` sub 1, delete at 0 | none |
| sim | `RouteFollow_B 0x85B470`, `RouteFollow_EditPath 0x85F5A0` (locomotion, `IC_Route_SetAGS`, script obstruction ICs) | same pattern after `0x82EDB0` / `0x82F0B0` (docs/152 G-a, docs/137 §2) | none |
| worker (job) | `0x82EDB0` copy | `0x82EFB6` +1, `0x82EFF6` -1 | `+0x918` |
| worker (job) | `GoalPoint_Validate 0x759B20` hit loops | `0x75A686` +1, `0x75A6E3` / `0x75A908` -1; `0x75AA4E` +1, `0x75AB34` -1; `0xAAC090` from `0x75A950` / `0x75AB7A` | `+0x918` |
| worker (job) | EditPath from `Route_PlanCommon` (`0x760F8B` -> `0x85FF30` -> `0x85F5A0`) | `0x85F7DE` (`83 47 04 01`), `0x85F8E5` +1; `0x85F916` -1 | `+0x918` |

**Verdict: a real data race, present today, rare** [verified code; overlap and rate inferred].

- The hold orders the worker's writes against other `+0x918` holders. The sim's writes above run while the sim holds nothing.
- A job can be running at that moment: jobs start in the gap right after a steering `+0x918` release (docs/86). That gap is the window in which `RouteFollow_A` walks its hits (its gate-0 call at `0x846E09` sits inside the same loop).
- Both sides hit footprints of the same quadtree. A walking Sim's obstacles and a job's goal ring around an object near that Sim share footprints (other Sims, the target object).
- x86 `add [mem], 1` without `lock` is not atomic across cores. A lost +1 makes the last release delete the footprint while something still holds it (use-after-free, crash later at an unrelated site). A lost -1 leaks it.
- The window is a few instructions per write, so a hit is rare. Its rate cannot be derived statically.
- In the base game every one of these writers runs on the sim thread [inferred from docs/18 and docs/150 §1: no other thread routes in vanilla]. So the race comes with `[RouteAsync]`.

**What closes it:**

1. `[RouteAsync] H2=1` (docs/149, built, off, not run) starts no queued job while the sim runs `Locomotion_Update`, which covers the per-tick `RouteFollow_A` / `RouteFollow_B` / EditPath writers [inferred from the docs/149 window].
2. The rest are script-IC writers outside the pass: `GetObstructionAtPoint`, `GetDestinationObstruction`, `FindObstaclesForSimAlongRoute`, and `SetAGS` / `Insert*` -> `0x85FF30` (docs/80 A4). Closing them needs the same no-start window around those ICs (H2's thunk with an IC-entry flag), or `lock` prefixes on **both** sides. Locking only one side does not help, and the worker side has more than 12 sites.
3. P5b / P5c lend windows (off by default) add more writers on the sim side while the job is suspended; docs/155 calls the lent window's refcount writes "balanced", which holds only while the threads do not overlap.

**Evidence to look for:** a crash whose fault is a deleting destructor reached from `0x846E90` / `0x846EC8` / `0x846EF7` / `0xAAC0B1` / `0x85F91E`, or a footprint vtable read on freed memory in steering, would fit. None of crashes A-I (docs/67) or `0x567121` (docs/173) is at these sites [verified, grep of the triage docs]. Probe idea (not built): in a test build, count `[fp+4]` values seen < 1 at the -1 sites, and the -1 to 0 deletes per thread.

## 5. Next steps

| # | What | Kind | Effort |
|---|---|---|---|
| P-G | Split "rest of job": three more `[RouteJobPhases]` sites, goal generation `0x751FF0`, goal step `0x75C2D0`, `0x85FF30` (EditPath for route), plus validations per job (`0x759B20` entries) and a log line for jobs with rest >= 50 ms (destination kinds and counts, route handle). Counter slots after v97 (reserve in `docs/registry.md`) | build + 1 IP job (natural or verify, 10 min) | ~30-60 min + 1 run |
| D1 | `[GoalGen] SameRing` byte patch (§3) | build + selftest + bundle A/B | ~20-40 min + 1 run |
| R1 | Extend H2's no-start window to the A4 script ICs, then run H2 in the next bundle | design exists (docs/149); build | ~30-60 min + 1 run |

P-G comes first under the "measure before build" rule. It tells whether IP's 200+ ms rest is goal generation (then range destinations: count and cost per validation), the goal step, or wall time outside any code we time.

## 6. Build (2026-10-23, ts3native 0.36.30, counters v102, `asi.0334`; built and selftested, not run in the game)

All three pieces are off by default, each behind its own ini switch, with a runtime switch for the A/B. Branch `route-goal-bundle` (integration branch `asi.0333` + this doc). Ledger: docs/51 §291. Registry: v102 (`pg*`, `gg*`, `hx*`, docs/registry.md).

| Piece | Switch | What is built | Check |
|---|---|---|---|
| P-G (§5) | `[RouteJobPhases] GoalSplit=1` (needs `Enabled=1`), observe only | Four more phase sites in routephases.cpp, timed only on the worker inside a job (not in sync plan sessions): goal generation `0x751FF0`, goal step `0x75C2D0`, `EditPathForRoute 0x85FF30`, `GoalPoint_Validate 0x759B20` (6-byte prologue `55 8B EC 83 E4 F0`, no branch into it in a linear sweep of `.text` [verified]). `0x75C2D0` and `0x85FF30` are timed inside `[RouteGate]`'s and `[AvoidRO]` T-AV's trampolines when those own them. Self time per phase (`pgN/pgUs/pgMaxUs`), validations per job (`pgValHist`, `pgValMax`), and a log line `route phases: long rest N ms ...` (other, goal gen, goal step, EditPathForRoute, validate, validation count, kind, route) for every job whose rest is >= 50 ms (`pgLongRest`, 20 lines a minute). With GoalSplit on, v87's OTHER no longer contains these four, so "rest of job" = OTHER + `pgUs` | selftest `TestGoalSplit`: fake generator calling a fake `ret 0x20` validation 3 times; worker-only frames, histogram, long-rest count, rest split |
| D1 (§3) | `[GoalGen] SameRing=1`, `Mode` 0/1/2 (`ggMode`, `native_set goalgen`), `BytePatch=1` | goalgen.cpp. The 8 bytes of the count store at `0x75C05C` (`mov dword [esp+0x20], 1`) are checked first; on a mismatch nothing is written and the log says so. The ring builder call `0x75C246` (rel32 to `0x75B610`, thiscall, `ret 0x40` [verified]) goes through a thunk that reads the loop's count, index and step from the caller's frame (return slot + 0x44: +0x20, +0x28, +0x3C [verified, push count at `0x75C1F0..0x75C243`]). Mode 1 skips the duplicate (count 1, index 1, step bits 0) with `ret 0x40`: the loop then ends as with count 0 (the loop compare at `0x75C252` is the count's only later reader [verified]). It is built as a call-site skip, not the immediate write, so the A/B can flip it per firing; `BytePatch=1` writes the immediate (1 -> 0 at `0x75C060`) as the release form. Mode 2 (shadow) runs both rings and compares ring 0's `ecx` + 15 arguments with the duplicate's (`ggArgMismatch`, must stay 0) and, on worker jobs with GoalSplit, the validation answers of both rings in order (`ggValCmp`, `ggValMismatch`; answers from the facing point's validation are told apart by return address `0x75BD8B`). It compares answers, not the candidate records (cheap; the records come from the compared inputs) | selftest `TestGoalGen`: a fake generator loop with the game's frame layout; mode 0 counts, mode 1 skips (stack balanced), ranged generators and non-zero steps untouched, shadow equal / argument mismatch / answer mismatch / other call site / lost state |
| H2 IC window (§4 item 2, R1) | `[RouteAsync] H2Ic=1`, `H2IcMode` (`hxMode`, `native_set h2ic`) | locowin.cpp. The IC binding slots of the 7 script ICs (table entries `{fn, name}`, Steam 1.67 [verified bytes]): `GetDestinationObstruction 0x74C820`, `GetObstructionAtPoint 0x74C8E0`, `IsPointObstructedBySim 0x751480`, `FindObstaclesForSimAlongRoute 0x750430`, `SetAGS 0x755600`, `InsertRouteSubPathsAtIndex 0x758BA0`, `InsertCustomPathAtIndex 0x7623A0`, swapped only while they hold the game's fn. On the sim thread the shim raises H2's `g_g0Win` before the body and drops it at return (return slot swapped, per-thread stack of 8), so ServeAsync holds a queued start for the IC's length, with H2's Dekker order against A_RUNNING. A job running at entry still holds `+0x918` for its body, so the IC's first acquire waits for it (`hxRunningAtEntry`). Sim waits for a held start drop the window (`G0wDrop`, same thread id); the 250 ms watchdog stays. An IC left by an exception keeps its frame until the next IC entry at that depth or a locomotion pass entry drops it (`hxStale`). Inside the locomotion pass the IC passes through (the pass's window covers it). `[AutShadow]` takes the shims as chained | selftest `TestH2Ic` on the real worker: a start queued inside the IC held until return, nesting, a `ret 8` IC, drop before WaitJobDone, unwound frame dropped, running at entry, Mode 0, another thread. `tools/model/p5-lock-model.ts` configs `ic-rc-today` (the race without the window: found), `h2x`, `h2-h2x` (clean), `h2x-tu` (TailUnlock lend to the IC's query: race still found, see Limits), `h2x-watchdog` (the watchdog reopens the race: the window must stay short) |

**Limits.** The window does not cover a job that is already running when the IC enters and lends `+0x918`. The model found this with TailUnlock on (config `h2x-tu`, race found [inferred, model]): the job lends `+0x918` to the IC's obstacle query during passes 16-19, takes it back, and goes on to its plan tail (EditPath `0x85FF30`, plain `[fp+4]` writes) while the IC does its own plain writes. TailUnlock is on in the measurement set, so the A/B leaves this gap open. Closing it needs the IC to wait for a running job's end when a lend window is open (or no lend to an IC frame); not built. P5b/P5c lends are off by default. The watchdog lets a held start go after 250 ms, so an IC that waits that long for something else reopens the race (`g0wWatchdog` and `hxHeld` show it). P-G times worker jobs only; sim-thread radius plans keep their docs/164 sync rows. The shadow's answer compare needs GoalSplit on the same job.

**Queue** `tools/harness/queues/route-goal-ab.json` (dry run only, nothing run): job 1 `ip-goal-split-verify` (8 min natural IP, GoalSplit + D1 shadow; correctness: `ggArgMismatch` 0, validations counted; reads the long-rest lines for I-177-3); job 2 `ip-goal-ring-ab` (repro `ip-goal-ring-loop`, ABBA to 10 firings per arm, D1 + H2Ic flipped together per firing, measurement settings of `tool-check.json`). Expected: no hitch change beyond noise (§3); job 2 is mainly the safety check of the window (no watchdog, no new wait keys in the moved-waits table).

## 7. Footprint refcount guard `[RouteAsync] RefcountGuard` (2026-10-23, ts3native 0.36.32, counters v104, `asi.0337`; static + model + selftest, not run in the game)

Ledger: docs/51 §304. Registry: v104 (`rc*`). Code: `native/ts3native/rcguard.cpp`, selftest `rcguard-test.h`, model configs `rc-*` in `tools/model/p5-lock-model.ts`, queue `tools/harness/queues/refcount-guard.json`, repro `ip-refcount-ring-loop`.

### 7.1 Every footprint refcount write, static [verified at the instruction unless marked]

Footprint refcounts are EA's intrusive count at `[fp+4]` (`fp` = the footprint sub-object the quadtree hands out). +1 is `add dword [r+4], 1`; -1 is `mov eax,[r+4]; add eax,-1; mov [r+4],eax; jne`, then `mov dword [r+4],1` and the deleting destructor (vtable slot 0) at zero. Scan: every `[r+4]` +/-1 read-modify-write in the job closure (`Route_PlanCommon 0x7606E0`, `Route_PlanLocked 0x82C6E0`, `RoutingJob_Execute 0x81B2A0`, 6 hops, 1,304 functions, 57 sites of many classes) and in every direct caller of the two footprint queries (capstone over the `tools/mono-glue-closure.py` index).

| Where | Sites | Thread, lock |
|---|---|---|
| `RoutingWorld_ObstacleQuery 0x82EDB0` copy loop | `0x82EFB6` +1 / `0x82EFF6` -1 on a temporary (balanced), `0x82D061` +1 for the vector's element (`0x82D030` push), `0xAAC0A9` -1 when the old vector contents go (`0x82EDE1`) | both; inside the query's own `+0x918` (`0x82EDD0` .. `0x82F098`) |
| `GoalFootprintGather 0x82F0B0` copy loop | `0x82F351` +1 / `0x82F3AD` -1 (temporary), `0x82F38F` +1 and `0xAAE781` / `0xAAE6F8` (`0xAAE690` insert) for the vector, `0xAAC0A9` via `0x82F0E1` | both; inside its own `+0x918` (`0x82F0D0` .. `0x82F45C`) |
| `GoalPoint_Validate 0x759B20` | `0x75A686` +1 / `0x75A908` -1 (steering hits), `0x75AA4E` +1 / `0x75AB34` -1 (gather hits), vector release `0xAAC090` from `0x75A950` / `0x75AB7A`, single release `0x55CFA0` from `0x75AABF` / `0x75ACA0` | worker in a job (whole-job hold); **sim** in sync plans after the query released |
| `Route_GoalStep 0x75C2D0` | `0x75CC7D` +1 / `0x75CD55` -1, `0xAAC090` from `0x75CBFF` / `0x75D0BA` / `0x75D1E3` | worker in a job; sim in sync plans |
| `RouteObstructionAtPoint 0x655880` | `0x655B1F` +1, `0x655B58` / `0x655BF5` / `0x655C3C` -1, `0xAAC090` x3 | worker (lot planner, hop 6) and sim (`IC_Route_GetObstructionAtWorldOrLotPoint`) |
| route-to-object `0x830340` | `0x83041A` +1 / `0x830455` -1, `0xAAC090` `0x830487`, `0x55CFA0` `0x830538` | worker for slot destinations [inferred from §2's table]; sim from the slot ICs (docs/140) |
| `RouteFollow_EditPath 0x85F5A0` | `0x85F7DE` +1, `0x85F8E5` +1, `0x85F916` -1, `0xAAC090` `0x85F960` | worker via `EditPathForRoute 0x85FF30` (`0x760F92`); sim via `Locomotion_ObstacleCheck 0x85FD10`, `SetAGS`, `Insert*PathAtIndex` |
| `RouteFollow_A 0x846B70` | `0x846D50` +1, `0x846E79` / `0x846EB1` / `0x846EDC` -1, `0xAAC090` `0x846F07` | sim only, after the query released |
| `RouteFollow_B 0x85B470` | `0x85B59B` +1, `0x85B5D0` / `0x85B622` -1, `0xAAC090` `0x85B600` / `0x85B649` | sim only |
| `IC_Route_IsPointObstructedBySim 0x751480` | `0x7516E6` +1 / `0x75178D` -1, `0xAAC090` `0x7517CF`, `0x55CFA0` `0x7517FD` | sim |
| `0x755EA0`, `0x8243E0` | `0x756039` +1, `0x756055` / `0x756082` -1; `0x8244C5` +1, `0x82451C` -1, `0xAAC090` | sim [callers inferred: script ICs] |
| `IC_RouteManager_IsOkToIdleAtCurrentLocation 0x7556B0` | no own write; vector released through `0x652470` -> `0xAAC090` | sim |
| Quadtree insert / remove / deferred flush (`0x665C50`, `0x666750`, `0x6646F0`, `0x6649C0`, `0x66BFD0`, ...) | 20+ more `[r+4]` sites in the job closure | worker and sim, always under `+0x918` [inferred: not classified as footprint vs node / entry counts] |

Corrections to §4: `0x75A6E3` is not a refcount write (`add eax,-1` on a virtual call's result, then `cmp eax,0x16`: a type switch). The query copy's `0x82EFB6` / `0x82EFF6` pair is balanced on a temporary inside the query's own hold; the vector's count is `0x82D061`. The early releases `0x82EE53` / `0x82F191` run after the vector was emptied (`0x82EDE1` / `0x82F0E1`), so they return no hits.

**The race in one sentence:** every worker write above runs while the job holds `+0x918` (the whole job; TailUnlock's lent passes 16-19 touch no footprint, docs/150 §11.1), and every sim write outside the queries runs in the 12 callers after `0x82F098` / `0x82F45C` gave the lock back. So the only unordered pairs are (any worker write) x (a sim caller's writes between the query's release and the caller's end). The only other sim writers outside a hold are object lifetimes, which the destroy / save / close drains already keep away from job bodies (docs/154 'life' writes).

### 7.2 Options, and why the guard holds the sim side

| Option (Rodney's order) | Verdict |
|---|---|
| (a) `EditPathForRoute 0x85FF30` on the sim at collect | Not built. It moves 3 worker sites (of ~15 footprint sites in the job) and none of the others (query copies, goal validation, goal step, `0x655880`, flushes), so the race stays. The moved EditPath would itself be a sim caller after a query (it needs this guard anyway), and its answer would be computed on the world at collect time, not the job's (not exact). |
| (b1) atomic worker writes | **Refuted.** The sim's -1 is `mov eax,[fp+4]` ... `mov [fp+4],eax` (3 instructions): a worker `lock add` between the load and the store is overwritten. Both sides atomic: the 4-byte adds fit `lock inc` (`F0 FF 46 04`), but the 9-byte releases cannot be rewritten in place while a thread may stand between their instructions, the shared helpers `0xAAC090` / `0x55CFA0` (88 callers) / `0xAAE690` are exe-wide, and completeness would need every `[r+4]` site of the 1,304-function closure classified. |
| (b2) defer the worker's writes to the sim | Rejected: the writes sit in thread-agnostic shared code (`0xAAC090`, the copy loops), so it needs thread-gated detours in generic helpers, and the flush writes are not balanced pairs. |
| (b3) hold the sim's writes behind a lock the worker respects | **Chosen**, with the existing `+0x918` instead of a new spin: the sim keeps the query's level until the caller returns. No new lock, no worker-side change, provable from 12 callers instead of the job closure. |

### 7.3 Design as built

- **Sites:** the two `call 0x4E17B0` (Lock_Release, thiscall, no arguments) at `0x82F098` / `0x82F45C` are retargeted (`E8` bytes checked) to `RcRelThunk`. Off the sim thread, with an empty vector, a caller outside the table, a different lock, or while the sim serves a worker callback (`g_inInvoke`, borrowed level): jump to `0x4E17B0` as before (through `[RouteGate]`'s Lock_Release detour).
- **Keep:** else the level is kept (`eax` = count - 1, as Lock_Release returns). The caller's frame is the query's saved `ebp` (`[ebp+0]`; all 12 callers keep an ebp frame and never write ebp after the prologue [verified, capstone]); its return slot `ebp+4` is swapped for `RcLeaveThunk` (per-thread stack of 8 frames; a second query in the same caller adds a level to the same frame). The caller is identified by `t_steerRet` (rtsites.cpp's RT_STEER key: the real return address even behind other owners' leave thunks), so it works with `[RouteGate]` / `[RouteJobPhases]` / `[Verify]` frames on the query and with `[SteerRO]` / `[AvoidRO]` detours on the callers. AvoidRO's A-site `0x846D1C` now enters through `AvSiteEntry`, which records RouteFollow_A's `ebp` (`g_avRfEbp`).
- **Release:** at the caller's return, every kept level (`0x4E17B0` again, so the gate's owner bookkeeping runs). Before every sim wait for the worker (`WaitAsyncDone`, `WaitJobDone`, `RunPlan`'s worker plan, `RtWaitJob`, `P4NrEnter`), `RcDrop` releases all kept levels first (`rcDropped[0]`). Frames an exception unwound (slot below the stack pointer, or a newer call's return address in the slot) are released at the next query or wait (`rcDropped[1]`).
- **Switches:** `[RouteAsync] RefcountGuard=1` installs (needs `[RouteGate] Enabled/Sites/Enforce=1`, i.e. the steering frames, and the async path; default 0). `RefcountGuardMode` = `rcMode` start (1 keep, 0 count only: `rcWould`); runtime switch `native_set refcountguard` / `--ab refcountguard` (+0x44D4).

### 7.4 Why it closes the race, and why it cannot deadlock

1. Every sim footprint write of a listed caller after its query happens while the sim owns `+0x918`, until that caller returns [verified: the writes are inside the caller (§7.1), the release is at its return].
2. A job body runs only while the worker owns `+0x918` (ServeAsync takes it before `A_RUNNING`), or in a lend window. TailUnlock's lent passes write no footprint (docs/150 §11.1); a lend the sim got for its query stays with the sim until the caller returns, and the worker's retake waits for it. So no job write runs beside a guarded sim write. This also closes `h2x-tu` (§6 Limits) and does not depend on H2, H2Ic or the 250 ms watchdog.
3. **No deadlock:** the sim keeps a level only inside game code between the query and its caller's return. Every wait of the sim for the worker is in our code and drops the levels first. The worker, while blocked on `+0x918`, holds no lock the sim can need (WM+0x228 only under the hold or in a lend, where FIX E's retake order already applies: model `rc-guard-wm`). Recursive acquires by the sim pass (CRITICAL_SECTION; P5bLock admits the holder's own recursion, p5b.cpp `P5bAllowed`).
4. **Model** (`node tools/model/p5-lock-model.ts --self-check`, 37 configs OK): `rc-guard` (guard alone), `rc-guard-tu`, `rc-guard-all` (H2 + H2Ic + TailUnlock, pass and IC callers), `rc-guard-watchdog`, `rc-guard-p5b` (door ask), `rc-guard-wm` (WM+0x228 with a lent lock) and `rc-guard-wait` (a caller that collects a job before returning) are clean. Mutation `mut-rc-early` (level released at the query, today's order) finds the refcount race again; `mut-rc-nodrop` (no RcDrop before a wait) finds the deadlock. `ic-rc-today`, `h2x-tu` and `h2x-watchdog` still find the race without the guard.

### 7.5 Cost estimate [inferred, not measured]

- Thunk: one call, a scan of 15 keys and a TLS stack per non-empty query on the sim, ~0.1-0.3 us; frame swap and leave thunk ~0.1 us.
- Extra hold: the rest of each caller after its query. RouteFollow_A / _B, `0x85FD10` and the ICs: their hit loops, microseconds. `GoalPoint_Validate` on the sim (sync radius plans): the rest of one validation (line of sight, physics overlap), tens of microseconds. `Route_GoalStep` on the sim: the rest of a sync plan's goal step (sync plans already wait for jobs at gate 10). A job that wanted the lock starts or resumes that much later; `rcHoldUs` / `rcHoldMaxUs` and `rcBusyAtOpen` measure it.
- Expected change in hitches >= 100 / 300 ms: ~0 (the moved wait is the callers' loop time). It is a correctness fix; it ships only if job 2 shows no hitch rise.

### 7.6 Not verified / limits

- Not run in the game. The selftest uses fake queries with the game's frame layout on the real worker; the game's 12 callers are verified statically only.
- Callers outside the table pass through (`rcPass[1]`): ts3native's own shadow calls of `0x82EDB0` (`[SteerRO]` / `[AvoidRO]` Shadow=1, off in every queue) release their vectors outside the hold.
- P5b / P5c clone runs (off by default): whether a clone run writes refcounts of live footprints is not checked; the guard keeps the lent level through the caller, which covers the sim side only.
- An exception through a caller keeps its level until the next query or sim wait (the worker is delayed; no deadlock, every wait drops first).
- The quadtree insert / remove / flush sites in §7.1's last row are not classified; they run under `+0x918` on both threads, so the guard does not depend on them.
- A sim wait inside a caller (an event or callback reaching an IC collect) drops the level early, so that caller's later writes are unguarded (`rcDropped[0]`, expected 0 in runs).

### 7.7 Tests and queue

- Selftest `TestRcGuard` (real worker, M1 lock): level kept until the caller returns, no hits, Mode 0, two queries in one caller, other thread, caller not listed, unwound frame dropped at the next query, `RcDrop` inside the caller, a claimed job blocked on the kept level collected without deadlock. `tools/hook-sites.ts --check`: 0 unlisted (docs/68 row).
- Queue `tools/harness/queues/refcount-guard.json` (dry run only): job 1 `sv-refcount-guard-verify` (SV `diner-crowd-loop`, 3 firings per arm, which also flips AvoidRO and SlotEnabledRO per arm; the `ip-locktaker-ab` settings: every lock-taker piece, TailUnlock, H2; guard on; pass = OK line, `rcDeferred` > 0, `rcPass_4` 0, `rcDropped_1` 0, no watchdog, no crash); job 2 `ip-refcount-guard-ab` (`ip-refcount-ring-loop`, ABBA 10 firings per arm, `tool-check.json` settings; hitches must not rise).
