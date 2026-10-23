# 175. IP steering obstacle-query waits: why they remain, and the lock-free pass arm

Static work and existing run files only, 2026-10-23 (time rule: no build, selftest or game run before `chain143: all done` in `probe-out/queue/notes.txt`). Sources: `probe-out/profile/natural-play-2026-10-22-ip-natural-30-*` (hitches, freezes, `freeze-report.ts`), `probe-out/queue/natural-play-2026-10-22-ip-natural-30-native.json`, the `*-native.json` of `repro-check-release-2026-10-22-{bp,sv}-rcr-diner-crowd`, `rofix-ab-2026-10-22-bp-avoidro-verify`, `clean-night-routing-extra-2026-10-22-bp-avoidro-onfirst`, every earlier S2 run in `probe-out/queue`, `wait-ab.ts` on `avoidro-2026-10-21-bp-wr-steer-avoid{,-onfirst}` and `wait-reducers-ab-2026-10-20-bp-wr-steerro`, `native/ts3native/{steerro,avoidro,gate0ro,discourage,rtoro}.cpp`. Tags as in docs/51: [verified] = code or counters, [measured] = run files, [inferred] = reasoning.

## 1. Summary

- **The waits are not fallbacks: every piece that removes them was off.** The IP natural run (asi.0332, natural-play.json `ip-natural-30`) had `steerReady`, `avReady`, `g0Ready`, `usfReady`, `fp2Ready` and `rtoReady` all 0 [verified, native.json]. Its ini is the freeze-measure / sim-budget all-on set, which carries SlotEnabledRO and the RouteAsync fixes but none of S2, AvoidRO, Gate0RO, DeferFootprint(2) or RouteToObjRO. The steering key `0x85FE10` is the game's own `+0x918` wait in `Locomotion_ObstacleCheck 0x85FD10`, with nothing in front of it.
- **T-AV does not make AvoidRO fall back.** `avTripAv` counts the worker's own entries into `0x81D090` / `0x85FF30` while armed: ~7 per job (44,144 / 6,363 armed jobs in the BP diner run). A clone drops only when such an entry overlaps it: `avFbTrip` 1 (BP diner), 0 (SV diner), 1-2 in the other runs, against ~110-250 in-job answers per run and no other fallback [measured]. A per-object or epoch-per-path T-AV would win at most those 1-2 calls per run, so it is not built (§3.2).
- **S2 itself answers when on.** In the four 0.36.x runs with `[SteerRO]` on, fallbacks are 0.1-0.2% of in-job checks (state 3-10, QL busy 0-3, trip 2-4, shape 0 after the child-footprint whitelist) and the steering key is 1.1-2.5 ms/s (BP, S2 on) against 11-25 ms/s off [measured].
- **Turning S2 on alone would only move the wait.** A removed taker hands the rest of the job's hold to the next `+0x918` taker in the same tick (docs/152 §1, conservation measured four times). In BP with S2 on, AvoidRO on moved the avoidance key (18.9 -> 0.1 ms/s) into `0x8334CC` (+10..+16), gate 12 and `UpdateSimFootprint`. So the arm is the whole locomotion + animation pass at once (§4).
- **Fix delivered (config + repro + queue, no new code):** one arm of six built switches, each still its own switch and off by default: `steerro, avoidro, gate0ro, deferfootprint, deferfootprint2, routetoobjro`. New loop repro `ip-steer-pass-loop` (30 Sims + household walk to one IP lot while their jobs run, ABBA per firing) and queue `tools/harness/queues/steer-pass-ab.json` (day verify with shadows, clean-night A/B, IP natural 30 min with the arm on). Nothing ran.

## 2. What the IP run shows

### 2.1 Switches [verified, `natural-play-2026-10-22-ip-natural-30-native.json` after]

| Piece | Ready / mode | In-job answers |
|---|---|---|
| S2 `[SteerRO]` | `steerReady` 0, `steerMode` 0 | `steerCalls` 0 |
| AvoidRO | `avReady` 0 | 0 |
| Gate0RO | `g0Ready` 0 | 0 |
| Design U / U2 | `usfReady` 0, `fp2Ready` 0 (`discReady` 1) | 0 |
| RouteToObjRO | `rtoReady` 0 | 0 |
| SlotEnabledRO | on (job ini) | - |

`s2ArmJobs` 19,945 and `steerQlHolds` 563 are counted anyway: the worker arms the tripwire generation and takes QL for W1-W6 whether or not a clone reader is installed [verified, `steerro.cpp`].

### 2.2 Where the sim waited [measured, `-1800-hitches.jsonl`, 3,177 iterations >= 50 ms in 1,798 s]

Grouped by each iteration's longest route-job wait (`rtMaxKey`):

| Key | Taker | Iterations | Summed longest wait | >= 100 ms | >= 200 ms | Piece of the arm |
|---|---|---|---|---|---|---|
| `0x85FE10` | steering obstacle query | 750 | 39.7 s | 307 | 57 | S2 |
| `0x8334CC` | `ObjectFootprintFromTool 0x8334A0` | 141 | 7.4 s | 69 | 14 | DeferFootprint2 |
| `0x833593` | `UpdateSimFootprint` | 119 | 5.4 s | 36 | 6 | DeferFootprint (U) |
| `0x62EF7A` | gate 0 | 112 | 5.3 s | 35 | 8 | Gate0RO (G-a frames only) |
| `0x83156D` | route-to-object check | 84 | 5.1 s | 31 | 7 | RouteToObjRO |
| `0x2000003` | destroy | 95 | 5.0 s | 28 | 3 | (DestroyPark, not in the arm) |
| `0x62FB74` | gate 12 writer families | 75 | 4.4 s | 30 | 2 | residual |
| `0x846D21` | avoidance A | 92 | 4.3 s | 32 | 3 | AvoidRO |
| `0x82D296` | `UpdateObjectLot` | 68 | 3.8 s | 19 | 4 | DeferFootprint2 |
| `0x85F6CB` | EditPath goal gather | 46 | 0.5 s | - | 8 | S2 (the clone runs EditPath's consumer) |

Shape of the steering iterations: 299 of 750 have that one wait only, 467 are led by the locomotion stage, and the long ones are one 235-340 ms wait (one of 744 ms in a 955 ms iteration): the sim reached the steering call while a job held `+0x918` and waited out the whole hold. In `freeze-report.ts` this is 6 of the 7 "locomotion" freezes >= 300 ms and one "route search" and one "other" freeze.

## 3. Why the waits remain

### 3.1 Uncovered, not fallen back [verified]
`LocoHook` returns the game's call at once when `steerMode` and `steerShadow` are 0 (`steerro.cpp`), and the module is not even installed without `[SteerRO] Enabled=1`. The callers of `0x85FD10` are the locomotion update only (`0x9573B7`, docs/143); `0x85FE10` is the return of its first `0x82EDB0` call, so no other caller hides behind the key. While a job runs (`A_RUNNING` = the worker holds the job's lock, `ts3native.cpp`), S2 covers every call that has a flagged node; sync worker plans (`g_callerTid`) block the sim itself, so they cannot meet a steering call.

### 3.2 T-AV
`AvTwEnter` counts an entry off the sim thread when the stubs are armed, a job runs or a clone is open (`avoidro.cpp` lines 53-60); `AvLocked` drops its answer only when `g_avTwActive` is set or `g_avTwEpoch` moved during its own QL-shared section (lines 104-107). Every job's tail copies options (`RoutingOptions_Assign 0x81D090`) and edits the planned path (`0x85FF30`) on the worker, so `avTripAv` grows with jobs, not with drops:

| Run | Armed jobs | `avTripAv` | per job | In-job answers (`avTakenMiss` + `avTakenB`) | `avFbTrip` | Other `avFb*` |
|---|---|---|---|---|---|---|
| bp-rcr-diner-crowd | 6,363 | 44,144 | 6.9 | 158 | 1 | 0 |
| sv-rcr-diner-crowd | 4,586 | 21,986 | 4.8 | 170 | 0 | 0 |
| bp-avoidro-verify (shadow) | 6,212 | 40,303 | 6.5 | 109 | 1 (+1 pin) | 0 |
| bp-avoidro-onfirst | 15,588 | 101,235 | 6.5 | 246 | 2 | 0 |

The release-check note that read 44k as "AvoidRO mostly falls back" is corrected in `queues/repro-check-release.json`. A narrower T-AV (drop only when the worker's write targets the followed path's options object `O`) would need the clone to publish `O` and the wrappers to compare it; it would save 1-2 answers per run and add a shared write per worker entry. Not built.

### 3.3 Conservation [measured, BP, `wait-ab.ts`]
`bp-wr-steer-avoid` (S2 on both halves, `--ab avoidro`): steering 1.3 / 1.1 ms/s, AvSite key 0.1 on / 18.9 off, `0x8334CC` 20.8 / 10.4, gate 12 Contains 15.4 / 14.5, U 12.1 / 9.3; summed keyed 103.1 / 106.9. On-first twin: `0x8334CC` 26.3 / 10.2, summed 112.1 / 69.5. `bp-wr-steerro` (S2 alone): steering 11.3 / 21.5, avoidance 17.4 / 9.9. Each partial arm moves the wait to the next taker of the same pass; the biggest absorber, `0x8334CC`, is DeferFootprint2's (built 2026-10-22, never run), and `0x83156D` is RouteToObjRO's (built, never run). That is why the arm in §4 carries all six.

## 4. The arm: locomotion + animation pass lock-free while a job runs

| Switch (`native_set` / `--ab`) | ini | Takes | Status |
|---|---|---|---|
| `steerro` | `[SteerRO] Enabled=1 Mode=1` | `0x85FE10`, `0x85F6CB` | live: 0 mismatches in 203k+ shadow compares, fallbacks ~0.2% |
| `avoidro` | `[AvoidRO] Enabled=1 Mode=1` | `0x846D21`, `0x85B50C` | live: 0 mismatches in 43k shadow compares |
| `gate0ro` | `[Gate0RO] Enabled=1 Mode=1` | gate 0 G-a frames (`0x846E0E`, `0x85B57E`, `0x85F76D`, `0x85F7A9`); answers only while `steerMode` and `avMode` are on (`G0_FB_PIECES`) | built 2026-10-22, never run |
| `deferfootprint` | `[Discourage] DeferFootprint=1` | `0x833593` | ran in the release-check loops (asi.0331) |
| `deferfootprint2` | `[Discourage] DeferFootprint2=1` | `0x8334CC`, `0x82D296` | built 2026-10-22, never run |
| `routetoobjro` | `[RouteToObjRO] Enabled=1 Mode=1` | `0x83156D` | built 2026-10-22, never run |

All six are in asi.0332 (counters v99: `steerReady`, `avReady`, `g0Ready`, `usfReady`, `fp2Ready`, `rtoReady` present) [verified], so no new build is needed and no asi is parked. Each stays off by default; the arm only flips their Mode switches together.

**Repro** `ip-steer-pass-loop` (`tools/harness/repros.json`): 30 spawned Sims + the household at ultra; per firing `crowd_go` to the farthest community lot (one destination: they steer round each other and the lot's objects while the 30+ async jobs hold `+0x918`), a fixed 45 s window on both arms, home leg as setup; ABBA, 4 firings per arm, 16 min cap.

**Queue** `tools/harness/queues/steer-pass-ab.json`, measurement settings of `freeze-measure.json` (all-on set, `[HitchAttr] Holders`, ForegroundRender, RouteJobPhases + SyncPlans, LogFallbackMs 20 / stack 16, noSamplers):
1. `ip-steerpass-verify` (day, ~8 min, after chain143): one ABBA cycle with all four shadows on. Pass: six OK log lines, every shadow mismatch, `usfApplyInJob`, `fp2ApplyInJob` and `*SimWriteInRo` 0, `steerTaken`, `avTakenMiss`, `g0Served`, `usfDeferred` >= 1. Any FAIL stops the queue.
2. `ip-steerpass-ab` (clean night, ~18 min): the A/B. Read per arm: iterations >= 200 / >= 300 ms, visible freezes, keyed wait by `rtMaxKey` (removed keys and the absorbers `0x62FB74`, `0x760BFF`, `0x2000003`, `0x82A0F6`, `0x631F40`), it/s, `asyncUs`, `m2DoorWaitUs`.
3. (dropped 2026-10-23 in the lock-taker integration: natural runs are discovery only; the combined arm is tested in `queues/locktaker-ab.json`.)

**Expected [inferred].** On the on arm the six keys go to about 0 while a job runs; the locomotion stage stops leading freezes. By conservation the hold moves to whatever per-event taker comes next (gate 12 writers, gate 10, destroy, `0x82A0F6`, F-G): those are not per tick, so iterations that meet a job no longer always wait, and a long hold is more often split across published sim frames instead of one visible freeze. Size: the arm's keys carry ~71.5 s of the 87.8 s of longest-wait time over all iterations of §2.2 [measured]; docs/152 §4.2 puts B1 (this arm plus G4 and G7) at -45..-60 ms/s summed keyed wait BP. The A/B decides; pass rules as docs/152 §4.3 (summed keyed on <= off - 20 ms/s, it/s >= off - 2%, 0 crashes, 0 new stuck Sims).

## 5. Risks and what is not covered

- Three of the six pieces (Gate0RO, DeferFootprint2, RouteToObjRO) never ran in game: the verify job runs them with shadows first. A crash on the on arm is ours until proven otherwise (`stop-tests-on-obvious-errors`); triage against docs/173 (animation pass 2 track crash at `0x567121`), since U and U2 move footprint writes out of the animation passes.
- Door-ask pump (docs/152 §5 item 3): with no sim waits the sim serves job callbacks less often, so `asyncUs` may grow. Read `m2DoorWaitUs` per arm; if it grows, a pump point at the end of `Locomotion_Update` is the next step.
- Not in the arm: destroy (DestroyPark, release-check set only), `0x62FB74` writers, gate 10, `0x82A0F6` (G4 not built), the unmapped keys (G7). The job's own hold length (other lanes: early-out, deadline) scales every residual.
- No new counters (docs/registry.md unchanged).

## 6. Ledger

docs/51 §285 (I-175-1 to I-175-5).
