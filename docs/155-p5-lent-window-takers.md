# 155. Upstream side of a lent window: every sim `+0x918` taker against the job's phases (P5b, P5c, TailUnlock)

Static study, 2026-10-22. No build, no game run, no queue. Sources: docs/94 §17, §21.8, §22.9-§22.11; docs/150 §9, §11; docs/152 §2-§5; docs/101 §2, §6; docs/84 §2 (discourage decompile); docs/86, 88, 89, 90, 92, 140; `docs/data/routing-indirect-sites.csv` (docs/102/106); our source (`p5.cpp`, `p5b.cpp`, `p5c.cpp`, `p4.cpp`, `p4clone.cpp`, `snap.cpp`, `routesnap.cpp`, `routegate.cpp`, `tailunlock.cpp`). New tool: `tools/p5-lent-walk.py` (5-hop closure per taker from the `mono-glue-closure.py` index of the Steam 1.67 exe, indirect edges from the docs/102/106 CSV), output `docs/data/p5-lent-unresolved.csv`. Tags: [verified] = read in code, the index or a disassembly-backed doc; [inferred] = reasoning, not checked; [unknown] = blocked by an unresolved indirect site or missing control flow. Ledger §242 (I-155-1 to I-155-12).

## 1. Summary

- **Three windows lend `+0x918` today:** P5b (clone run, steering + read-only gate leaves), P5c (clone run, every plain sim acquire + parked destroys), TailUnlock (live post-pass passes 16-19, P5b's admission). In all three the worker reads no live routing state that an admitted taker writes: the clone run reads its copy (lot table, quadtrees, footprints, polygon caches, lock-key sets) and TailUnlock's passes read only job-private nodes plus world-manager data [verified docs/94 §21, docs/150 §11.1]. So **no taker races the window itself.**
- **One data race found, after the window [inferred, static order verified]:** gate-12 F-G (`FootprintsNearPoint`, wait key `0x631F40`, in `0x631F10`) is admitted for its `LotTable_Contains` leaf only (`[RouteGate]` takes `+0x918` for the leaf call, `routegate.cpp` header). After the leaf, `0x631F10` calls `0x667480` at `0x631F9A`, a pinned lot-quadtree query that can flush (`0x666A40` -> W1/W2), with no `+0x918` [verified call order in the index; branch structure not checked]. If the window closes while the sim is in that tail, the worker's next live work (P5/P5b/P5c fallback plan, or the code after `TuS19` in TailUnlock) pins and flushes the same live quadtrees: a plain-RMW pin counter and a node-freeing flush on two threads. Same class as I-94-352 (A10's queued-start gap); lending makes it more frequent. **Fix: drop F-G from `P5bGate`** (0.8-1.6 ms/s BP, docs/152 §2).
- **No other race.** Steering and gather hold their own `+0x918` for all their writes; gate-0 hits write nothing; F-B's tail takes portal references through the interlocked pair (H20a); sync plans planned on the sim in a P5c window use the live A\* stamp and clip scratch while the clone run uses its own (T-patches, `p4.cpp` 76-77, 165-168) [verified]. Unlocked portal and lock-key IC writers run in every window, as they do in A10: the portal cost (4-byte) and age/gender flags (torn 64-bit pair) are a benign race that `nr` bit 256 refuses [verified docs/94 §21.8.1, `p4.cpp` `P4NrSnap`].
- **P5b's admitted set is not strictly read-only, but every write is either balanced or a cache fill on the sim's own hold:** quadtree pin (+1/-1, plain), deferred flush (applies queued W1/W2, frees nodes), footprint refcounts at `+8` (plain, balanced), polygon-cache fill (W6b at hop 5), lazy lot-table insert and rehash (W5 at hop 4 via `GetOrInsert` on a miss inside steering). None of these is read by the clone run or by passes 16-19, and the next live phase runs only after the retake [verified closures, §3]. The C-A rebuild `0x80E5B0`, which can lazily build door portals (`W6g` -> `LotRouting_AddPortal` at hop 3), is **not** admitted by P5b (it waits under its own key) [verified `P5bAllowed`].
- **P5c staleness coverage: no writer class is missed by choosing cut-based over window-based; the gaps are elsewhere.** Every recorded routing leaf counts (`g_spWrites`), portal and lock-key ICs, destroys, physics, terrain, lot index and display level count through `nr` [verified]. Not covered: `SetOceanPortalOwner` (IC slot 15, world `+0x988/+0x98C`, outside family 9's slots 4-14) and the RouteToObjectCheck write-backs (`0x6649C0`, `P+0xD0` bit 16; I-198-5/6) [unknown]. **Over-covered:** cache fills (W4, W5, W6 without a source, flush-sourced W1/W2) and every parked destroy (family 2, always counted) refuse answers that a held P5 job would have taken. With per-tick writers admitted and an animation pass inside the window, nearly every P5c answer is refused and replaced by the game's plan under the retaken lock [inferred]; this is a likely cause of P5c's measured regression (docs/136 §3.8).
- **One P5c hazard [verified path, inferred timing]:** discourage add `0x834370` (`[Discourage]` D off) fires `World_FireEvent(0x11)` (object lot changed, a managed callback) through `0xC8ADD0` at `0x834537`, between its acquire `0x8343B4` and its last release `0x83484A`. In a P5c window that runs managed listeners while the sim owns the lent lock during a live job. Our hand-over brackets catch the known job waits; an unknown one hangs (docs/94 §22.11.6 R4).
- **Recommendation (§8):** P5b and TailUnlock: steering pair (all callers), gate-0 hits, F-B, F-C; **not F-G**. P5c: keep `FreeLock` off as built; if revisited, admit only parked destroys (counted as skew, not `nr` refusal) on top of P5b, require D and U on, and count staleness from the window with cache-fill leaves filtered.
- **Unmapped keys of docs/152 §2 resolved (G7 in part):** `0x75A64C` = steering `0x82EDB0` called at `0x75A647` from goal validation `0x759B20` (radial goal generation, docs/92 §2), already admitted as steering; `0x831BF4` = `AddPrivateFootprint 0x831BA0` (writer, P5c only); `0x729E2C`, `0x728DC3` (AsyncLoader `0x729E10`, `0x728DB0`, lock `[this+0x40]`), `0x7345F2` (`0x7345D0` -> `0x72C6C0`) and `0x4B0021` (`GetPackageSource 0x4AFFC0`, lock `[this+0x48]`) are **not `+0x918`** [verified acquire operands in the index; classes from docs/08]; `0x5000000` is a synthetic key.

## 2. The windows and what runs beside them

| Window | Opened / closed (worker) | Worker inside the window | Sim may take `+0x918` | Job phases outside the window |
|---|---|---|---|---|
| P5b (`ReleaseLock`, 0.34.0) | `P5cLend` -> `P5bOpen` after the clone acquire (or before it when `PcAcquireOwnOnly`), `P5bRetake` after `P4RunClone`, before a door ask, at a tripwire | clone run on the copy; live reads N1-N12 under D1-D3 (docs/94 §17.4); `P4CloneAcquire` from the replay world when own-only [verified `p4clone.cpp` 775] | holder recursion; `RT_STEER` frames (`0x82EDB0`, `0x82F0B0`) outside a gate wait and callback; with `ReleaseGates` gate-0 hits and gate-12 F-B/F-C/F-G [verified `P5bAllowed`] | START cut (sim, before), prelude (`0x82C733` lock, `ApplyPendingUpdates 0x82C420`), answer move `P5Move` or fallback `PlanLockedBody`, IC tail, `J_DONE` (all under the worker's hold) |
| P5c (`FreeLock`, 0.35.0) | as P5b, with `g_p5cJob` | as P5b | P5b's classes plus every sim acquire outside a gate wait (`t_waitKey` 0 or `0x80E5B0`); destroys parked; `RunPlan` on the sim under the lent lock | as P5b; `P5cStale` / `P5cStaleEarly` and `nr` decide taken vs fallback |
| TailUnlock (0.36.23) | `TuS16` before pass 16; retake on a failing pass or after pass 19 | live passes 16-19 on job-private nodes; world manager (lot index under `WM+0x228`), terrain rays (`[TerrainGrave]`), Lot objects, partitions, `FindById` under `World_GetHeightAt` [verified docs/150 §11.1] | P5b's admission (`g_tuOn` in `P5bLock`) | everything else of the live plan, under the hold |

A START cut is never taken while a job runs: P5 refuses it and borrows the last cut (`PcCutFn`, "a running job, or P5 (Mode=2), refuses as before") [verified]. So no START cut overlaps a window, and a borrowed cut's older write count only makes `P5cStale` stricter.

## 3. Takers: 5-hop closures

Re-walk: `python tools/p5-lent-walk.py --cache <dir with mono-glue-index-v2.pkl>` (direct calls plus resolved/partly resolved indirect targets; "hop" = BFS depth; reachability, not control flow). Unresolved = CSV `unresolved` plus sites the CSV does not list (outside the docs/102 routing closures). Waits: every `+0x918` closure reaches `Sleep` / `WaitForSingleObject` through `Lock_Acquire`'s own spin and the allocator; no job-wait function of ours is in game code.

| Taker (key) | Root | Admitted by | Fns | Unres. | Writes reached (hop) | Managed / event | Class |
|---|---|---|---|---|---|---|---|
| steering (`0x85FE10`, `0x846D21`, `0x85B50C`, `0x75A64C`) | `0x82EDB0` | P5b, TailUnlock, P5c | 150 | 18 | pinned query 2, flush 3, `GetOrInsert` 3, W1/W2/W5 4, Rehash 5, W6b 5; footprint `ReleaseAt8` 2 | none | RO + balanced writes + cache fills |
| gather (`0x85F6CB`) | `0x82F0B0` | same | 158 | 19 | as steering | none | same |
| goal validation (`0x75A64C`) | `0x759B20` | its `0x82EDB0` call only | 403 | 47 | as steering inside the call; `World_FireEvent` 4 (event 68 via `World_CanSeeObject 0x789DD0` at `0x75AC22`, after the steering call returns) | after the lock | caller of steering; tail goes to the gate-10 wait |
| gate 0 G-a (`0x62EF7A`) | `0x62EF30` / `0x62EAB0` | P5b gates (hit) | 61 / 76 | 6 / 8 | W5 1-2, Rehash 2-3 (miss path only; a miss is given back before the body runs) | none | RO on a hit |
| gate 12 F-B (`0x62F782` + `0x76F15D`) | `0x62F760`, `0x76F0A0` | P5b gates | 72 / 147 | 14 / 20 | `GetOrInsert` after `Contains` (guarded: no insert, docs/101 §6), portal AddRef/Release | none | RO; tail interlocked (H20a) |
| gate 12 F-C (`0x631814`) | `0x631800` | P5b gates | 63 | 8 | `GetOrInsert` after `Contains` (`0x631828`); two unresolved vcalls in the tail (`0x63187F`, `0x63189A`) | none | RO leaf; tail unknown |
| gate 12 F-G (`0x631F40`) | `0x631F10` | P5b gates | 138 | 38 | **unlocked tail:** `0x62FB10` and `0x62ED40` (gate 0, gated), then `0x667480` at `0x631F9A` (pinned query, flush 2, W1/W2 3, W6 4-5) | none | RO leaf, **writer tail without `+0x918`** |
| gate 12 writer families (`0x62FB74`) | `0x62FB60` | kept out | 63 | 6 | W5 2 | none | writer family |
| RouteToObjectCheck (`0x83156D`) | `0x831530` | P5c | 277 | 41 | steering 2, W6a 3, W5 4, flush 5, W6e/W6g 5; `0x6649C0` write-back, `P+0xD0` (docs/140) | none | RO + writes; two writes unknown |
| C-A rebuild (`0x80E5B0`) | `0x80E5B0` | P5c | 282 | 45 | W6a 0, W6g door 2, `LotRouting_AddPortal` 3, `LotRemoveObjectFootprints` 4 | none | writer (lazy door portals) |
| UpdateSimFootprint (`0x833593`) | `0x833560` | P5c | 464 | 76 | FPCORE 1, W3/grid/RemovePortals 3, W1/W5/water 4, AddPortal 5 | none (docs/89) | writer |
| ObjectFootprintFromTool (`0x8334CC`) | `0x8334A0` | P5c | 460 | 70 | as `0x833560` | none | writer |
| UpdateObjectLot (`0x82D296`) | `0x82D280` | P5c | 103 | 16 | RemovePortals 2, footprint remove 3, W5 4 | none | writer |
| Remove / AddPrivateFootprint (`0x82A0F6`, `0x831BF4`) | `0x82A0C0`, `0x831BA0` | P5c | 196 / 510 | 29 / 117 | footprint remove / add, quadtree, grid, W1/W2 | none | writer |
| Discourage add / remove (`0x8343B9`, `0x834890`) | `0x834370`, `0x834860` | P5c when D is off | 636 / 504 | 84 / 85 | FPCORE 1 and the footprint family | **add: `World_FireEvent(0x11)` at hop 3, under the lock** (§1) | writer + managed |
| gate 10 / sync plan (`0x760BFF`) | `0x7606E0` | kept out; `RunPlan` here in P5c | 675 | 180 | goal step, `Route_PlanLocked`, `ApplyLotUpdate`, W4, Q reset | none known | writer (real dependency) |
| prelude (job phase) | `0x82C420` | worker, under hold | 153 | 27 | `ApplyLotUpdate` 1, W4 4, Q reset 4 | none | job's own writes before the window |
| tail passes 16-19 (job phase) | `0x64C7C0`, `0x64D5B0`, `0x64A690`, `0x641DF0` | worker in TailUnlock | 142 / 117 / 96 / 1 | 6 each | none of `+0x918` state; `WM+0x228` rebuild 3, object Release 5 | none | job-private + world manager |

Unresolved sites: 460 distinct (1,052 root rows) in `docs/data/p5-lent-unresolved.csv`. In the P5b-admitted closures (98 distinct): mostly slot 0/4 of unknown receivers in the quadtree apply/free path (`0x6653A0`, `0x665C50`, `0x666010`, `0x666750`: node and entry destructors), the allocator (`0x4E42F0`, `0x4E5E40`), the terrain kd-tree (`0xC66700`, `0xC5FA60`), and 21 in the goal-validation caller `0x759B20` (outside steering's hold). These are lifetime and allocator slots under the sim's own hold; none is reached by the worker in a window. They are listed as [unknown] rather than closed.

## 4. Conflict matrix

Cells: **S** safe; **Sk** time skew accepted (the answer is the cut's, as every P5 job; no refusal needed); **St** stale-answer risk, refused (`P5cStale` or `nr`) -> fallback plan; **R** data race; **U** unknown (unresolved indirect or missing control flow); n/a = cannot overlap. Columns: (A) clone run in a P5b/P5c window, (B) TailUnlock passes 16-19, (C) START cut, (D) prelude, (E) after the retake: answer move or fallback live plan (P5) / rest of the live plan (TailUnlock), (F) `J_DONE` publish.

| Taker | A | B | C | D | E | F | Notes |
|---|---|---|---|---|---|---|---|
| steering / gather (all callers) | S; under P5c its W1/W2/W5/W6 count -> St (over-refusal) | S | n/a | n/a | S (writes end at its own release) | S | [verified closures; P5c counting via `SpNote`] |
| gate-0 hit | S | S | n/a | n/a | S [inferred: tail reads the entry transform] | S | miss given back unexecuted [verified `P5bLock`] |
| F-B | S | S | n/a | n/a | S (portal refs interlocked, H20a) | S | |
| F-C | S | S | n/a | n/a | U (2 tail vcalls) | S | |
| **F-G** | S | S | n/a | n/a | **R** (unlocked `0x667480` tail vs the fallback / rest of plan) [inferred] | S | drop from admission (§8) |
| RouteToObjectCheck (P5c) | St (W6); U (`0x6649C0`, `P+0xD0`) | n/a | n/a | n/a | S | S | I-198-5/6 open |
| C-A rebuild (P5c) | St (W6, door portals via `0x635700`) | n/a | n/a | n/a | S | S | new live portals are not in the clone; refusal correct |
| footprint writers U-family, lot, private (P5c) | St [verified leaves FPCORE, private, remove] | n/a | n/a | Sk: pending lot updates queued in the window are applied by the next plan's prelude, as in A10 | S | S | |
| discourage add/remove (P5c, D off) | St + **U** (managed event 0x11 under the lent lock) | n/a | n/a | n/a | S | S | hazard, not a race |
| sync plan via `RunPlan` (P5c) | S (private stamp/clip in the clone, T-patches) ; St via `ApplyLotUpdate`/W4 | n/a | n/a | n/a | S | S | |
| parked destroy (P5c) | St (family 2 always counted) — would be Sk if counted by id | n/a | n/a | n/a | S (park keeps memory; fallback sees the object gone) | S | live check I-94-372 |
| unlocked IC writers (portal cost/flags, lock keys) — not `+0x918`, any window | benign race on live portal fields, St via `nr` 256 / family 6 | S (not read) | Sk | Sk | S | S | exists in A10 too |
| `SetOceanPortalOwner` (unlocked, slot 15) | **U**: world `+0x988/+0x98C`, no `nr` family | S | Sk | Sk | S | S | rare (ocean lots) |
| AsyncLoader / package keys | not a `+0x918` taker | | | | | | §1 |

## 5. P5c staleness: cut-based vs window-based (docs/94 §22.11 open question)

| Writer class that can run in a P5c window | Counted by | From | Verdict |
|---|---|---|---|
| footprint core / private / remove, lot remove, grid add/remove, water marks, lot apply/restore, G build, blend, world events 5/7/9/0x0A, 0x0D-0x10 | `g_spWrites` (`SpNote`, `routesnap.cpp` `kSn`) | START cut (`P5cOnStart`) | covered [verified] |
| cache fills: W4 `LotGraph_Build`, W5 `LotTable_Insert`, W6a-g without a source, W1/W2 from `FlushDeferred` | `g_spWrites` | cut | **over-covered**: the clone prebuilt or copied these at the cut (docs/101 H4, H5, H7; the cut applies flushes and pending updates first, `PcPrepare`) [inferred], so they do not change its answer |
| portal ICs (slots 4-14), lock-key ICs (0-3) | `nr` families 8 and 6 | live `Route_PlanLocked` entry | covered for the window; pre-window writes accepted as skew [verified `P4NrSnap`] |
| destroys | `nr` family 2, always | live plan entry | **over-covered**: a parked destroy refuses every answer of its window |
| physics, terrain swaps, lot index, display level | `nr` with reader bits | live plan entry | covered when read |
| event 0x11 (object lot changed) | not counted | - | covered: the same call then runs FPCORE (docs/84 §2 order) |
| `SetOceanPortalOwner` (slot 15), `DestroyPortalHandle` (16), `MarkRoomForUpdate` (17) | none | - | 15: gap [inferred rare]; 16: handle table only; 17: queues a pending update, applied by the next prelude (skew as in A10) |
| RouteToObjectCheck `0x6649C0` write-back, `P+0xD0` bit 16 | none | - | unknown until I-198-5/6 say whether the clone reads them live |

**Answer to the open question:** the cut-based rule is a superset of the window-based one, so it misses nothing the window rule would catch. The pre-window part it adds is exactly the skew every held P5 job already accepts (`m2Stale` counts it, no refusal). So a **window-based rule is as safe as P5 itself**, and refusing pre-window writes buys nothing. The real problem is over-refusal: cache-fill leaves and destroys, both of which an animation pass or a locomotion pass inside a window produces almost every tick. Recommended rule: count from the window's open (`t_p5cW1`, already recorded), only the input-changing leaves of row 1, and `nr` with destroys counted only when the clone's or the plan's `FindById` touched that id (a per-job id set, ~1 h) [inferred].

## 6. Loops that re-enter a taker many times per tick

| Loop | Takers per iteration | Window can span it? | Effect |
|---|---|---|---|
| `Locomotion_Update` (per agent, `0x957210` from `0x83CF52`) | steering `0x85FD10` -> `0x82EDB0`, avoidance `RouteFollow_A/B`, gather `0x85F5A0`, gate-0 G-a via `0x62EF30`, C-A rebuild `0x80E5B0` | yes: a clone run is 5-31 ms (docs/150 §1), a whole locomotion pass a few ms | P5b: each agent's call is a separate outermost admission; the pass runs through. **Retake fairness [inferred]:** `P5bRetake` tries with `TryEnterCriticalSection` + `SwitchToThread` and keeps admitting while it tries, so back-to-back per-agent acquires can delay the retake by up to the rest of the pass (`p5bRetakeMaxUs` measures it) |
| `Animation_SACSPass` `0xD81A70`, `Animation_UpdateActors` `0xD81C73` (per actor) | `UpdateSimFootprint 0x833560`, `0x8334A0` | yes | P5c only: one admitted footprint write per moving actor; any one refuses the answer (§5). With design U on they are deferred and never reach the lock |
| script tasks (IC loops: slot checks, front door, placement) | gate 12 F-B/F-C/F-G, `0x831530`, private footprints, sync plans | yes, a window can span a whole sim iteration (~16-33 ms) | P5b: reads pass; F-G's tail is the race of §4 |

## 7. Unresolved list

`docs/data/p5-lent-unresolved.csv` (columns root, root name, class, function, site, kind, operand, status). Counts per root are in the §3 table. Before widening admission to any new class, its rows here must be resolved or shown to be lifetime/allocator slots under the taker's own hold.

## 8. Recommended admission

| Window | Admit | Keep out | Change |
|---|---|---|---|
| P5b | holder recursion; `RT_STEER` frames from any caller (incl. `0x759B20`); with `ReleaseGates`: gate-0 hits, gate-12 F-B, F-C | **F-G `0x631F40`**, gate-12 writer families, gate 0 misses, gates 1-11, 13-15, plain writers, C-A rebuild | remove `0x00631F40` from `P5bGate` (1 line); F-C stays (U only in its tail vcalls; re-check after resolving `0x63187F`, `0x63189A`) |
| TailUnlock | as P5b (shares `P5bAllowed`) | as P5b | follows automatically |
| P5c | as built: not recommended (over-refusal, discourage event). Revisit as **P5c-lite**: P5b's set + parked destroys, with design U and D required (`FAIL route snapshot P5c` when either is off), window-based staleness with the cache-fill filter and destroys by id (§5) | `P5B_FREE` plain writers (they are deferred by U/D; whatever is left would refuse the answer anyway) | ~3-4 h build + selftest |

**Applied 2026-10-22 (integration merge, ts3native 0.36.23 combined build `game-copy/ts3native.asi.0324`):** F-G `0x631F40` removed from `P5bGate` (p5b.cpp; the same admission covers TailUnlock, docs/150 §11); an F-G acquire during an open window now waits for the retake and counts in `p5bFgHeldOut` (counters v95, +0x3FEC). Selftest `TestP5b` checks the gate class and the held-out count (ledger I-155-13). TW-2 (§9) is not needed with F-G out.

## 9. Required tripwires (proposed, not built; counters to be reserved in docs/registry.md)

| # | Tripwire | Where | Why |
|---|---|---|---|
| TW-1 | `p5cStaleWhy[k]`: which leaf class or `nr` family refused each P5c answer (input leaf, cache fill, destroy, portal, other) | `SpNote` (leaf class into a per-window mask), `P5cStale` | measures §5's over-refusal before any rule change |
| TW-2 | F-G tail in flight at retake (if F-G stays admitted): a sim flag set at a granted `0x631F40` admission and cleared when `0x631F10` returns; the worker counts and waits for it before a fallback | `P5bLock`, a return hook on `0x631F10` | the §4 race; simpler to drop F-G |
| TW-3 | `p5cEvLent`: `World_FireEvent` on the sim while it owns the lent lock (id logged, first 20) | the existing `SN_WEV` stub | the discourage-add event and any other managed callback under the lent lock |
| TW-4 | `p5bRetakeGrants`: outermost admissions granted after `P5bRetake` began; optional closing flag that refuses new outermost admissions during the retake | `P5bLock`, `P5bRetake` | retake starvation in per-agent loops (§6) |
| TW-5 | slot 15 (`SetOceanPortalOwner`) into `nr` family 8 (`i <= 15`) | `P4NrSnap` | the one unlocked routing writer with no family |

## 10. Effort (Claude with agents)

| Work | Time |
|---|---|
| This study (re-walk tool, CSV, matrix, doc) | done |
| Drop F-G from `P5bGate` + selftest line + rerun `TestP5b` / `TestTailUnlock` | ~30 min |
| TW-1, TW-3, TW-4, TW-5 (counters, report lines) | ~1.5-2 h |
| P5c-lite (window rule, leaf filter, destroys by id, U/D prerequisite) | ~3-4 h + 1 clean night |
| Resolve F-C tail vcalls and I-198-5/6 for RouteToObjectCheck | ~1-2 h static |
