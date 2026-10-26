# 195. Gate-12 "F-F" wait, key `0x62FB74`: what it is and what would remove it

Static analysis (capstone on the Steam 1.67 copy, raw `E8` caller scan) and existing run data only; no build, no game run. 2026-10-25. Ledger §352.

**Short answer.** `0x62FB74` is not a `+0x918` acquire. It is the return address of `LotTable_GetIfPresent 0x62FB60`'s call to `LotTable_Contains 0x628A90`, whose entry is route gate site 12 (`routegate.cpp` `kRgSites`): the sim thread waits there until the running async job leaves `A_RUNNING` (P6W class 1, `RgEnter` / `RtWaitJob`). "F-F" is the label `wait-ab.ts` gives the key; the docs/84 F-F family is wider (`SurfaceAtPoint`, PlaceGroupSocial), but almost all of the key is one caller: the script idle check `IC_RouteManager_IsOkToIdleAtCurrentLocation 0x7556B0`. The wait is gate policy, not a data dependency (docs/178 §8, I-178-8-3). `[IdleCheckRO]` (0.36.48, off) already answers the lookup exactly, but the same IC then runs its own obstacle query `0x82EDB0` (key `0x755990`), which takes `+0x918` and waits for the same job: in `night-1024-gate12` all 118 skips moved there (net <= 59 ms). Removing the wait needs both halves: `[IdleCheckRO]` plus a read-only answer at the IC's obstacle query (site C below). Expect it to move again (destroy, gate 0); in IP natural play this key leads only 2 of ~39 iterations >= 300 ms, so it is a stress-loop and absorber problem more than a natural-freeze cause.

## 1. The function and its callers

**`LotTable_GetIfPresent 0x62FB60`** (thiscall, lot planner P, args = lot key lo / hi, `ret 8`) [verified disassembly]: calls `LotTable_Contains 0x628A90` at `0x62FB6F` (return `0x62FB74` = the key); a hit then calls `LotTable_GetOrInsert 0x62EAB0` on `P+0x48` at `0x62FB86` (gate site 0, return `0x62FB8B`). Reads only; the gate exists because `GetOrInsert` writes the table's window slot and inserts on a miss, and a job may rehash the table (W5) or build a lot (W4) (docs/80, docs/84 §4.3). Under QL shared the `Contains` + hit read is safe (docs/84 §4.3; S2's `SrEntry(contains)`).

**Direct callers of `0x62FB60`** (raw `E8` scan, 30 sites) [verified static]. Only the ones that can run on the sim thread while a job runs matter (the gate passes the job's own calls):

| caller (return = P1b second frame) | path up | thread | share of the key |
|---|---|---|---|
| `IC_RouteManager_IsOkToIdleAtCurrentLocation 0x7556B0` (`0x7558DC`) | script `Sim.RunIdleLogic` -> IC | sim | 781 of 801 waits, 6.98 of 7.17 s in the four P1b runs of 2026-10-18 [measured]; `night-1024-gate12`: 15 of 152 waits came while `[IdleCheckRO]` was on, 137 while off [measured, `ik` column; consistent with this caller leading, arm lengths not compared] |
| `RoutingWorld_SurfaceAtPoint 0x834B80` directly (`0x834CF6`) and via `RoutingWorld_EnsureTerrainFootprints 0x8341D0` (`0x834201`) | `World_SurfaceTypeAtPoint 0xAB9580` <- `FootstepAudio_Surface 0x8693E0`, `IC_World_GetInformationAboutRoomImpl 0x7933A0` (via `0xB01B50`), placement `0xB463F0` / `0xB470C0`; also `UpdateTerrainFootprints` | sim | 3 waits in P1b; writes lazy terrain footprints (W1) |
| `0x823800` (`0x8238C5`) | `0x8273B0` / `0x827430` <- `IC_RouteManager_PlaceGroupSocialImpl 0x764910` (docs/84) | sim | 0 seen; writers unknown (docs/189 §8.2) |
| `IC_RouteManager_SetPortalLockStatus 0x74B490` (`0x74B4B5`) | script door lock | sim | 0 seen; a writer (F-D) |
| footprint level rebuild `0x806770`, `0x80B060`, `0x80C200`, `DoorRouting_AddDoorPortals 0x80CB40` (`0x806B1C`, `0x80B163`, `0x80C29A`, `0x80D798`) | F-A rebuild (`0x80E5B0`) | sim | 0 seen in P1b (docs/178 §8) |
| `ClosestWaypoints_GatherChunk 0x654790` (`0x654BE5`), `PathPlanner_PlanHierarchical`, `LotEdgePortals_*`, `Portal_LockState 0x64F2C0`, `LotQuadTree_*`, `RoutingWorld_ApplyLotUpdate`, `RoutingSystem_OnExitBuildBuy`, `RoutingDebug_*` | the planner (job), sim sync plans, lot rebuilds, build/buy exit | job or sync plan | 17 waits / 0.19 s (`0x654BE5`, a sim sync plan) |

**What the idle-check IC does after the lookup** [verified disassembly, `0x7558C0`-`0x755A60`]: a hit runs the leaf `0x625140` (no calls; reads the lot entry's rooms and portals; 1 = "OK to idle", IC returns `0x40`). A miss or a 0 goes to `0x75592B`: `0x6508F0` (builds a query shape on the stack), then `RoutingWorld_ObstacleQuery 0x82EDB0` at `0x75598B` (mode 0, the IC's option word, a fresh empty footprint vector with tag `0x1004514`), which takes `+0x918` (key `0x755990`). An empty answer goes straight to the release of the empty vector and the IC's "nothing here" return; a non-empty one enters a hit loop (`0x7559A0`-`0x755A55`): per footprint virtual reads (type, owner id, a flag call, a class call), `0x788EA0`, `FindById 0x939560` and an object flag test against `[0x11E90FC]`; return codes 4 and others, vector released by `0x652470`. No routing write in the IC outside the two lock-taking calls [verified for the callees to 4 hops, docs/178 §9.2].

**`[IdleCheckRO]` live result** (`night-1024-gate12` `ip-idlecheckro-shadow`, 0.36.49) [measured]: 3,961 shadow compares, 0 mismatch; 118 taken, **pass 1, onward 117**: the leaf almost never says "OK to idle", so nearly every idle check goes on to `0x82EDB0`. That is why every skip moved to `0x755990`.

## 2. How much there is

| run | 0x62FB74 ms/s | >= 300 ms iterations led by it | max |
|---|---|---|---|
| IP natural-30, 2026-10-22 (v0.1.4-era defaults) | not keyed in that build | 0 of 47 (75 iterations have it as the longest wait) | wait 180 ms, iteration 298 ms |
| IP v0.1.5 bundle A/B, `overnight-1023` offfirst / onfirst (natural, in-load ABBA) | **bundle on 21.3 / 22.0**, bundle off 3.2 / 4.5 | 2 of 27 / 0 of 12 (whole run) | wait 260 ms, iteration 362 ms |
| IP locktaker verify `rerun-0333` (stress) | - | 3 of 21 | wait 338 ms, iteration 957 ms |
| IP P6 ladder 2026-10-25 (stress loop, `wait-ab.ts` on / off) | 19.5-30.8 on all rungs; kinds rung on 55.8 | not counted here | rung 1: 1,122 waits, 27.3 s, avg 24 ms, max 338 ms; kinds: 1,323 waits, 56.1 s, avg 42 ms, **max 2.93 s** |

[measured: `wait-ab.ts`, `*-hitches.jsonl` `rtMaxKey`, `ntN/ntUs/ntMaxUs_3` of the ladder's `-ab-native.jsonl`.] The ladder ran with `[IdleCheckRO]` and P1b off, so the caller split there is inferred from the earlier runs.

Two readings [inferred]:
- **It is an absorber.** With the v0.1.5 bundle off the key is 3-4 ms/s; with it on (SteerRO, AvoidRO, SlotEnabledRO, Gate0RO, RouteToObjRO, PlacementRO, DeferFootprint, ...) it is 21-22 ms/s, while steering `0x85FE10` drops 24-27 -> 0-2. The waits the bundle removes land on the idle check later in the same tick: docs/152 §1 conservation (I-152-0).
- **Its size follows the job span.** A gate wait lasts until the job leaves `A_RUNNING`, so each one costs the rest of the job: ~24 ms average in the loop, ~42 ms on the kinds rung where refused foreign jobs run clone + game plan (docs/189 §17.4). That is why it doubled there, and why its max reached 2.9 s.

## 3. Coverage today

| switch | covers `0x62FB74`? |
|---|---|
| `[IdleCheckRO]` (0.36.48, docs/178 §8) | the lookup + leaf at `0x7558D7` / `0x755916`, exactly; **off in release defaults** (`Enabled=0`); alone it only moves the wait to `0x755990` |
| SlotEnabledRO (SH), PlacementRO, RouteToObjRO, AvoidRO, Gate0RO, SteerRO (S2), LotFrontDoorRO, ObstructedRO, GoalValidRO (T6), DeferFootprint / 2 | no: different call sites; they feed it (absorber) |
| docs/152 bundle B0 | no (lists `0x62FB74` as residual writers, which docs/178 §8 refuted) |
| P5b / P6 HoldKeyed | no: `0x62FB60` is "kept out" (docs/155, docs/189 §8.2: F-D / F-F share the key and are not told apart) |

## 4. Design: finish the idle check (`[IdleCheckRO]` site C)

**Cheapest correct design:** keep A and B as built and add one call-site retarget **C at `0x75598B`** (the `E8` to `0x82EDB0`), run only right after A/B on the same IC call (`g_ikA` style state, owner thread, job running, `AvMayRun`):
1. **C1, empty answers (T6 pattern):** reuse `gvro.cpp`'s `GvRun` (S2's `SrQuery`, mode 0, visitor `0x662DB0`, record option word = the IC's option) under QL shared with S2's protocol. The call site matches T6's shape [verified]: mode 0 pushed, fresh empty vector, answer in `al`. Lot found + no footprints = return `al` with the vector untouched; the IC then takes its own empty path, exact as for T6.
2. **C2, non-empty answers (ObstructedRO pattern, optional second step):** clone the hit loop's reads under QL shared on the clone's footprint list without the refcount +1/-1 (net 0), using the game's own virtual calls, `0x788EA0` and `0x939560`, and return the IC's code from the hook. Do C2 only if the C1 run shows non-empty answers dominate. In a crowd, other Sims' footprints near an idling Sim are likely, so C1 alone may cover little [inferred].
3. Any doubt (QL busy, tripwire, sequence or generation moved, job end, lot not in table, deferred flush, buffer limit, mode != 0, non-empty vector without C2) calls the game's `0x82EDB0`: today's wait.
Shadow: with no job, the game's call, then the clone, compared (`al`, footprint count, and with C2 the IC's return code).

**Expected effect [inferred].** In the IC: A+B+C1 leave it lock-free for empty answers; A+B+C1+C2 for all non-fallback answers. Gate wait `0x62FB74` + `0x755990` -> ~0 for covered calls. **Moved-waits caveat (docs/152 §1, I-152-0, docs/189 §17):** the sim still meets the job later in the tick. The next takers in the bundle-on arm are destroy `0x2000003` (25-36 ms/s), gate 0 `0x62EF7A` (9-16), `0x83156D` (5-6), `0x82F4AB` (~5) and the unmapped `0x729E2C` / `0x04B0021` (2-3). Expect most of the 21-22 ms/s to reappear there, so summed sim wait changes little. A real gain needs either the whole taker bundle of the tick or shorter job spans; the idle check is one more piece of that bundle, not a fix alone. On IP natural play this key leads 0-2 iterations >= 300 ms per 10 min, so the ceiling for visible freezes is small; the stress-loop numbers (19-56 ms/s, max 2.9 s) overstate it.

**Alternatives rejected.** SH-style admission (run the game's IC under a QL hold): `0x82EDB0` takes `+0x918` and adds footprint refcounts, so no. Defer: the script needs the answer now. Reduce the job's hold: that is the P6 line, stopped by docs/189 §17.7.

**Effort (Claude time):** C1 ~1-2 h (hook, `GvRun` reuse, counters in the free v119 tail or a new version reserved via `tools/registry.ts`, selftest cases like `TestGoalValidRO`); C2 ~3-5 h more (loop clone + selftest with fake footprints). Game runs: 1 shadow / smoke job (~15 min), then 1 verify job.

**Proof (validate-and-fix, no new A/B, smoke before timing).** One job on `ip-idlecheck-crowd-loop` (exists, `night-1024-gate12.json` shape) with `[IdleCheckRO] Enabled=1 Shadow=1` + C, `[MovedWaits] Enabled=1`, `[ProbeP1b] Enabled=1`:
- pass: no crash, `ikShadowMismatch` 0 and the C shadow mismatch 0 over >= 20 compares each; C1 empty share and C2 taken counts > 0.
- read in place: `moved waits idlecheck` net (avoided - moved) and the new moved key (expected destroy / gate 0, not `0x755990`); P1b second frame of the remaining `0x62FB74` waits (confirms `0x7558DC` still leads on a current build, I-178-8-1).
- then only if net > 0: put it in the next IP natural bundle smoke and read the >= 300 ms count by `rtMaxKey` with `hitch-ab.ts` (natural A/B only where hitches remain).

## 5. Open

- Caller split on current builds and in the ladder (P1b was off): I-195-1.
- C1 empty share in a crowd: I-195-3; if small, only C2 helps.
- Whether the moved wait lands on destroy / gate 0 (I-195-4); if so, this belongs with DestroyPark and G1 (docs/152) in one bundle, not alone.
