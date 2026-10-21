# 142. Route early-out: why failing plans cost seconds, and a no-path pre-check (`[RouteEarlyOut]`)

Written 2026-10-21. **Static study plus an offline build:** no game run, no queue started. ts3native stays `0.36.9` on its branch (version string unchanged; merged into 0.36.12), counters **v78**, selftest `TestEarlyOut` (`selftest ok`). Question (Rodney, top priority): the worst freezes left after docs/134 are single long sync plans: 7,361 ms and 7,350 ms (SV big crowd), 4,060 ms (BP rare-route-bits), 1,015 ms and 1,313 ms (SV). Does the world search exhaust the whole graph before it reports no path, and is there a cheap pre-check that proves "no path" early with the answer the full search would give? Ledger rows: docs/51 §203. Tags as in AGENTS.md.

## Summary

- **A failing world search visits everything it can reach.** The world A\* `WorldAStar_Search 0x676260` has exactly two exits: the goal popped, or the open list empty; no node, time or cost cap [verified, docs/11 Q1, re-read at `0x676BE9..0x676C11`]. A no-path answer therefore costs a flood of the start's whole reachable part of the waypoint graph R (Sunset Valley: up to ~27,000 walkable waypoints and ~110,000 edge costs, each byte-2/7 edge with a polygon segment test), plus the circle pre-pass that runs first [verified `0x67628A..0x6762C4`].
- **The planner multiplies it.** For a goal inside a lot, `PathPlanner_PlanHierarchical 0x65E0D0` tries the goal lot's entry portals one by one: per candidate one colour check, then one full world A\*; on failure it drops the candidate and takes the next [verified, decompile of the loop at `0x65F697..0x65F842`]. All candidates share the start, so when the goal is cut off by walls the colours do not see (blocked lot edges, polygon walls, lot-hub rules, mode rules), every one of K candidates repeats the same flood. For open-ground goals with several goal records it runs one A\* per distinct goal waypoint, **without** the colour check [verified, `0x65FB99`, `0x65FDA5` paths]. Above that, `PathPlan_WithRetries 0x661B50` and the RoutingJob's obstacle replan add whole plans (docs/11 Q1 table) [verified there].
- **Data:** the BP 4,060 ms plan is the forced car-only plan of `rare-route-bits` (`EnablePlanningAsCar` + `DisablePlanningAsPedestrian` + `BeginAsCar` + `EndAsCar` to the farthest community lot, result `FailedBecauseNoPathToDestination`): its step took 8 s where every other step took 2.5-3 s [verified timestamps in `probe-out/queue/freeze-data-2026-10-20-bp-frz-rare-route-bits.log`; the match to the 4,060 ms line is inferred from it being the only multi-second line of that run]. Car-only = cost mode 2, which admits only edge byte 3 (street waypoints `0xD`); lot entry waypoints (type 2) have no byte-3 edge, so **every** goal-lot candidate of a car-only plan into a lot fails by construction [verified edge rules docs/99 §10.1, edge pairing §10.4]. The SV 7.3 s and 1.0-1.3 s lines have no script stack in that build and stay unattributed.
- **The pre-check (built as a probe, `[RouteEarlyOut]`, off by default, observe only):** before each world A\* call three proofs are tried; each implies "this call returns no path": (1) **car**: mode 2 and the goal is not in the start's byte-3 component of R (our own union-find, exact for mode 2); (2) **colour**: modes 1-3 without water/boat context flags, the game's own flood colours of start and goal differ (the test the game already makes for goal lots, extended to open-ground goals); (3) **memo**: an earlier unrestricted search of the same request with the same start and the same wall context failed and the goal is not in its closed set (the closed bits of a failed search are exactly the start's reachable set). Mode 1 still runs every search, counts the time a real early-out would save, and checks each proof against the real result (`eoWrong`, expected 0).
- **Why not act yet:** the proofs keep the yes/no answer by construction, but a skipped search also skips the "best node" it writes for the blocker hint (`*best` is reset to the start at every call and updated during the search [verified `0x67641E..0x67642A`]), and the memo depends on two inferred facts (no wall opens between two searches of one request; wall-ness does not depend on the SHORT/LONG cost set). Both are checked live by the probe before an acting mode exists (§5).
- **Not covered by any pre-check:** time spent outside the world A\* (lot legs `LotPlan 0x6410B0` per candidate, portal A\* `0x63E340`, closest-waypoint searches, post passes, managed callbacks). The probe's per-request line splits a slow plan into world-A\* time and the rest, which is the first thing the next run must answer (§6).

## 1. The slow plans

| Run (freeze-data 2026-10-20) | Line | Plan | Result | Tag |
|---|---|---|---|---|
| BP rare-route-bits | 4,060 ms, NoStart | `sim_route_to far` with car-only options (step id 51) | `FailedBecauseNoPathToDestination`, blocker location set | verified result; link to the line inferred (§0) |
| SV rare-route-bits | 1,313 ms, NoStart | not attributable: SV steps all took 5 s and most ended `FailedBecauseNoDestinationSpecified` | - | open |
| SV big crowd | 1,015 + 7,361 + 7,350 ms back to back, NoStart | crowd plans (social group routing per docs/134 §1.1 shapes) | not logged | open |

Other forced plans of the same run were cheap: the horse plan (`FailedBecauseNoPathToDestination`), the mounted plan (no path), plain car (`EnablePlanningAsCar`, mode 3, no path) and UFO (no path) added no visible time to their steps (gaps equal the steps' waits at the log's 1 s resolution) and no other sync plan line of that run exceeds 123 ms [verified], so "no path" alone is not slow; a multi-second no-path plan is one whose search has a large reachable part and many tries.

## 2. Termination and amplifiers

### 2.1 The world A\* (`0x676260`)

- Entry: thiscall on the A\* container `W+0x18`, seven stack arguments (R, G, &start ref, &goal ref, cost mode, search context, &best), `ret 1Ch`, result in EAX (non-zero = path) [verified, argument loads `0x676264..0x676282`, returns `0x676C11` / `0x676C9C`].
- Circle pre-pass: if the context's circle radius² (`+0xA0`) equals 0.0 at entry and `0x673600` accepts the pair, the A\* sets the circle and calls itself; on failure it clears the radius and runs again unrestricted [verified `0x676268..0x6762C4`, constant `0xF98964` = 0.0 read from the exe]. On success the radius stays set (a later call with the same context runs once, restricted) [verified: the success branch jumps to the epilogue].
- Closed set: the bit array at container `+0x10` (`+0x08` bytes) is cleared at the start of each search; the bit of ref (chunk, wp) is `table[(chunk + 1) & 0xFFFF] * 8 + wp` with the per-chunk byte table at `+0x0C` [verified `0x6762ED..0x676300`, `0x6764FA..0x67650B`, `0x676557..0x676588`].
- Exits: goal popped (`0x676C14`) or open list empty (`0x676BF7`: frees the heap array, returns 0). **The closed bits are not touched on the way out** [verified], so after a failure they hold every node the search closed = every node reachable from the start under that search's walls.
- `*best` is set to the start at entry [verified `0x67641E..0x67642A`] and moved to the generated node with the smallest heuristic during the search (docs/99 §2.4).

### 2.2 Multiplication above the A\*

| Where | What repeats | Tag |
|---|---|---|
| A\* circle pre-pass | a restricted flood before the full one (failures only) | verified |
| Goal-lot entry loop (`0x65E0D0`) | one colour check and one full A\* per entry candidate, same start, until one succeeds or the list is empty; a colour mismatch ends the loop at once | verified (decompile) |
| Same loop | a start-side lot leg (`LotPlan`, "Routing/lotStart") inside the loop body | inferred: the branch's condition was not resolved |
| Open-ground goals | one A\* per distinct goal waypoint (`W+0x100` tried set), no colour check | verified (decompile) |
| `PathPlan_WithRetries 0x661B50` | whole planner again with the retry flag; one planner run per distinct goal lot | verified (docs/11 Q1) |
| RoutingJob `0x81C67A` | a second PlanPath with relaxed flags when ReplanToFindObstacleWhenPathPlanFails is set (every Sim route) | verified (docs/11 Q1) |
| C# route-fail behaviour | further `Route.Plan` calls (separate IC calls, separate log lines) | verified call sites (docs/11) |

### 2.3 Existing caps

None in the game [verified docs/11 Q1/Q3]. Ours: `[RouteBudget]` (node/time cap through the A\*'s five cost-function pointers, off; in-game test 2026-10-13 stranded Sims, docs/15 phase 4) and docs/134 `SyncCapMs` (the same stubs, sim-thread sync plans only, off by default). Both **change answers** (a findable path becomes "no path" over the cap). The early-out is the opposite kind: it may only skip searches whose answer is already known to be "no path".

## 3. The pre-check

All three proofs run at the A\* entry, outside any lock of ours, in the same thread and request as the search. Virtual (water) ends and start = goal are never proven.

### 3.1 Car components (mode 2)

- **Condition:** cost mode 2 and `lab(start) != lab(goal)`, where `lab` is a union-find label over every R record reference whose edge byte is 3 (records with the edge-bytes flag `0x40` only).
- **Why it never turns a path into a failure:** the mode-2 cost function walls every edge whose byte is not 3, walls every virtual end, and has no other test that could admit an edge (no blocked-flag test, no polygon test; the circle only removes edges) [verified, docs/99 §10.1]. So every path the mode-2 search can find uses byte-3 edges only and stays inside one label. The labels are built over undirected edges, a superset of the forward references the search follows. A reference the search could follow outside the chunk table (out-of-range chunk or waypoint index) makes the labels unusable for that R (`ok = false`), so the proof never covers an edge it did not model.
- **Exactness:** for mode 2 the label is also sufficient (same label = reachable when the circle is off), so the proof misses nothing.

### 3.2 The game's colours (modes 1-3)

- **Condition:** cost mode 1, 2 or 3, context flags without `0x80` (water planning) and `0x100` (boat), both colours `>= 0`, and different.
- **Why:** the game's flood `RTData_BuildComponents 0x66CE10` follows every record reference regardless of edge byte, blocked flag or mode (docs/99 §1.8) [verified there], so a search without virtual edges stays inside one colour. Virtual edges are walls in modes 1 and 2 without flag `0x80` [verified, docs/99 §10.1]; mode 3 follows mode 1's virtual rules plus a water-lot rule [verified diff, docs/99 §10.1; that the `0x80` wall comes first in mode 3 is inferred]. Mode 4 is left out: its virtual-edge rule has no flag wall in docs/99 §10.1. The flood's colour classes are reach-closed only because R is symmetric (every edge has its reverse; all 14 shipped worlds, docs/99 §10.4) [verified data]; the game's own check at `0x65F781` relies on the same.
- **What it adds:** the game already makes this test for goal lots; the probe counts it where the game does not (open-ground goals, a second goal-lot candidate in another colour cannot happen because the loop breaks).

### 3.3 Same-request memo

- **Store:** after an outer A\* call (not the circle rerun) that returned no path, entered with radius² 0 and left with radius² 0, not capped by `[RouteBudget]`/`SyncCapMs`, and with both cost sets (container `+0x28` SHORT, `+0x4C` LONG) made only of finite positive factors: copy the closed bits and the call's wall context.
- **Wall context (key):** container, R, G, cost mode, start ref, context `+0x00..+0x63` (flags, start/goal lot hubs, start/goal lot points, edge-group sets), `+0x88` (W), `+0x8C` (job block with the polygon set), `+0xA8..+0xB3` (start point). Not in the key: the goal ref `+0xB4`, the goal point `+0xB8`, the cost-set slots `+0x64..+0x87` and the circle `+0x90..+0xA0` (written by the A\* itself).
- **Condition:** same key, same request (one `Route_PlanLocked` on this thread), goal's closed bit clear in the copy.
- **Why:** with the circle off, a failed search closes exactly the set of nodes reachable from the start through non-wall edges. Whether an edge is a wall depends on its two ends, its edge byte and the context, not on the goal or the visiting order: the only parent-dependent rule (edge byte 1 out of a hub, docs/99 §10.1 case d) gives finite factors without the retry flag and infinite or NaN (both walls) with it; the hub-entry 5,000,000 is not a wall; heuristic results are never tested against the wall [verified, docs/99 §10.1, §2.4]. The goal enters only through the heuristic (order, not reach), the circle (off) and the SHORT/LONG choice (equal walls when every factor is finite and positive). So a later search with the same key reaches the same set, and a goal outside it fails. A restricted later search (radius set at entry) reaches a subset and fails too.
- **Assumptions (inferred, checked live by `eoWrong`):** no wall changes inside one request between the two searches. The candidates: lazy lot routing builds inside the plan (`LotGraph_Build 0x63CB90` -> `RoutingWorld_RestoreLotBlockedBits 0x82A190` rewrites lot-edge blocked bits, docs/99 §1.7), the world-manager point test `0xC6BAB0` on type-2 targets, and managed callbacks between two searches.

### 3.4 Keeping the components right when the world changes

| Change | Car labels | Game colours | Memo |
|---|---|---|---|
| World load / new R | rebuilt when R's record pointer, size, w or h differ (~ms once) | the game rebuilds them at load | per request: gone |
| Lot load, lot streaming, build/buy, lot-edge blocked bits | edges of mode 2 never test blocked bits; records do not change after load (`WorldRoutingData_LoadChunks` runs only for BuildRoutingData / world save, docs/99 §1.9) | colours ignore walls: blocked bits only remove edges, so the old colours stay a superset | per request; inside a request see §3.3 |
| Portals and doors | lot-side (lot graph, portal A\*); not in R | same | same |
| Water links (G maps) | mode 2 walls them | flags `0x80`/`0x100` excluded | part of the key (G, flags) |

## 4. What was built (observe only)

- `native/ts3native/earlyout.cpp` (included by `ts3native.cpp`): detour on `0x676260` (prologue `83 EC 3C 53 8B 5C 24 58`, the bytes `[ProbeValues]` also checks; ours installs first, ProbeValues then logs SKIP for that one entry), request scope in `CallPlan` (every `Route_PlanLocked` on the caller or the worker; the old function is now `CallPlanBudget`), car labels, colour read, memo, counters, log lines.
- Ini `[RouteEarlyOut]` `Enabled=0` (installs the A\* detour and the `Route_PlanLocked` detour), `Mode=1` (start value of `eoMode`, runtime switch, `--ab earlyout`), `LogMs=200`.
- Per request `>= LogMs` (20 lines/min): `route early-out: plan N ms (caller|worker, path|no path): world A* a calls f fails x ms, provable p (car c colour k memo m) y ms, wrong w`. With `[RouteAsync] LogFallbackMs` set, it sits right before the matching `route async: sync plan` line (same thread, inner scope).
- Every 60 s: `route early-out: mode ... plans ... A* ... provable ... wrong ... slow N (ms, A* ms, provable ms, saved S), reach max R, labels L (us)`.
- Counters v78 (`docs/registry.md`): `eoMode` `+0x3260` ... `eoSlowSaved` `+0x32B0` (after v77 AvoidRO's last field; built at `+0x3174` on its branch, moved at the 0.36.12 merge); `eoSlowSaved` = slow requests whose provable time alone would bring them under `LogMs`.
- Selftest `TestEarlyOut` (`native/ts3native/earlyout-test.h`): fake R (2 chunks, byte-3 and byte-7 edges, an isolated waypoint), fake container and a fake search that floods like the game's (circle rerun through the detoured entry): car proof and no proof in one component, labels kept per R and refused for an out-of-range reference, colour proof and none with the water flag, memo store / proof / no proof inside the reached set / key changes / goal ref and goal point outside the key, a wall that opens behind the memo counted as `eoWrong`, no store after a restricted, capped or zero-factor search, a restricted search proven from an unrestricted memo, nested request scope, slow-request counters, Mode 0 pass-through, `CallPlan` scope on and off. Plus the v78 layout asserts.

## 5. Acting mode (design, not built)

`Mode=2`, after the probe shows `eoWrong` 0 over the A/B and a useful `eoSlowProvMs`:

1. On a proof, return 0 from the A\* hook without calling the game, after writing `*best` as the search would: the start ref, then the reachable node with the smallest heuristic. Car and memo proofs know the reachable set (the label class; the memo bits), so `best` is computed by one pass over it with the game's own heuristic call (cost function with the edge flag 0), first node in chunk/record order on exact float ties (the game: first in push order; the two differ only on exactly equal distances). Colour proofs do not know the set: act on them only where `best` is unused, or not at all.
2. Memo epoch: drop the memo when a blocked-bit writer (`0x82B590`, `0x82A190`) or a lot graph build (`0x63CB90`) runs inside the request (the counting stubs of `[RouteSnap]`/`[ProbeValues]` already sit there; docs/68), so §3.3's assumption is enforced instead of assumed.
3. Keep the closed bits consistent for the planner: the game does not read them after the A\* returns [inferred: no reader outside `0x676260` and its clear `0x673500`/`0x6735C0` was seen in docs/99]; checked by the shadow run of Mode 2 (both answers compared on the first N proofs).
4. Same answer, same blocker, fewer searches; the result line in the log and `RoutePlanResult` stay the game's.

## 6. Expected effect on the freeze lines

Upper bound = the provable share of world-A\* time in a slow plan; the probe measures both numbers per plan.

- **Car-only plans into a lot (the BP 4,060 ms line):** every mode-2 A\* call is provable (car proof). The world A\* share of that plan is unknown: a mode-2 flood is small (Bridgeport has 2,325 street waypoints), so if the plan is mostly A\*, it is mostly many candidates (K x 2 floods); if `eoSlowAsMs` is small, the time is in the per-candidate lot legs, and the fix moves up a level: one proof per goal lot before the entry loop (mode 2 and a goal in a lot = no world path at all), which needs a hook on the planner, not the A\*.
- **Pedestrian plans cut off by walls the colours do not see:** the memo saves K-1 of K floods in the goal-lot loop and the open-ground goal loop; the first flood is always paid (~100-200 ms in SV/BP; the longest single search measured in Bridgeport was 188 ms, `[RouteBudget] MaxUs` note in `native/ts3native/ts3native.ini`) [inferred scale].
- **Not reached:** plan-level retries with other flags (other key), the obstacle replan (relaxed flags), C# fail-path plans (separate requests), lot-side searches. If the 7 s lines are mostly those, the early-out is the wrong tool and the line split says so.

## 7. A/B queue (not run)

`tools/harness/queues/early-out-ab.json`, 8 jobs: BP and SV `rare-route-bits` (6 min) and `rare-big-crowd` (8 min), each OFF (`[RouteEarlyOut] Enabled=0`) then ON (`Enabled=1 Mode=1 LogMs=50`), seed 12345, the freeze-data saves and repros, both arms `LogFallbackMs=20` and `LogFallbackStack=8`. Before the run: build this commit (`native/ts3native/build.cmd`), copy `native/ts3native/out/ts3native.asi` to `game-copy/ts3native.asi.eo142`; `minCounters 78`. Pass: no crash, `eoReady` 1, `eoWrong` 0, watchdogs 0. Read per slow sync plan: the early-out line before it (A\* ms of the plan, provable ms, which proof), the script stack line (which managed caller). Decision rules: `eoWrong > 0` = the argument broke, stop and find which assumption (§3.3 list) before anything else; `eoSlowAsMs` well below `eoSlowMs` = the freezes are lot-side or retries, design the next pre-check there; `eoSlowProvMs` close to `eoSlowAsMs` = build Mode 2 (§5).

## 8. Effort

Static study and probe build: ~3 h (agent, offline). Game: the A/B queue, 8 jobs (~1.5 h of game time; correctness and the line split are valid on a noisy machine, timings only on a clean night). Mode 2 if the probe says so: ~2-4 h build + one A/B + 2 clean runs before turning it on.
