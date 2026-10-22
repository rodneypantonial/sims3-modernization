# 136. Wait reducers A/B, night of 2026-10-20/21: per-slice read and v0.1.5 decisions

Data analysis only (no game run, no build). Input: queue `wait-reducers-ab.json` (7 BP jobs, build 3643ebe, `game-copy/ts3native.asi.snap03524`, base = snap-p5c-ab's settings without `InsFreeInPlace`) and the quiet rerun of `snap-p5c-ab.json` (2 jobs, build 46767ab, 0.35.26). Results page: `docs/data/results/2026-10-20-queue-wait-reducers-ab.md`, `docs/data/results/2026-10-20-queue-snap-p5c-ab.md`. Reducer designs: docs/116 §4.1. Ledger rows: docs/51 §191.

All 9 jobs PASS, 0 crashes. Every job's machine line says **noise: high** (other apps 9.6-11.5% of the game CPUs), so this was not a clean night: timing numbers are read for direction only (memory rule: noisy runs = trust correctness, not timing).

## 1. Method

Each job is one load, `profile-world.ts --ab <name>`, 10 min, 2-min slices: slice 0 warm-up (on, not counted), then off, on, on, off (ABBA). Per counted slice:

- **it/s, hitches/min, p99, worst, route ms/s** from `<tag>-600-seconds.csv` and `<tag>-600-hitches.jsonl`. Row index = second of the run (as `tools/probe/profile-compare.mjs`); the first 15 s after each switch are dropped. Hitch = sim iteration >= 50 ms. **p99** is exact: every slice has more hitches than 1% of its iterations (~400 hitches vs ~45 iterations at the 1% mark), so the p99 iteration is the (1% of iterations)-th longest hitch. **Worst** = longest hitch wall time in the slice.
- **Keyed sim-thread waits** (ms/s per docs/84 key) from the counter snapshots at each switch (`<tag>-ab-native.jsonl`), whole slice. The on/off means equal `node tools/harness/wait-ab.ts <tag>` exactly (checked on all 9 tags).
- **Hitch attribution** per side: for every hitch >= 50 ms whose longest single wait is >= 10 ms, count its `rtMaxKey` (the fields `tools/harness/hitch-attr-report.ts` reads). Whole-run view: `hitch-attr-report.ts` (route-job waits are 27.5-28% of >= 50 ms hitch wall time in steerro, locowin and slotenabledro).

Script: a scratch per-slice splitter over the same files (not committed; same slice rule as profile-compare.mjs).

## 2. Noise band

| metric | same-setting slice-to-slice difference, median / max (8 BP jobs x 2 sides) | slot means across the 8 BP jobs (s1 off / s2 on / s3 on / s4 off) | position bias of on - off (from the slot means) | no-op locowin on - off |
|---|---|---|---|---|
| it/s | 3.6 / 14.2 | 38.5 / 47.3 / 46.6 / 43.3 | **+6.1 (+15%)** | -4.1 (-8%) |
| hitches/min | 13.1 / 77.1 | 217 / 188 / 195 / 230 | **-32 (-14%)** | -1.2 |
| p99 ms | 19.9 / 57.2 | 113 / 108 / 134 / 127 | +1 | +37.8 |
| worst ms | 63.9 / 288 | 283 / 313 / 348 / 388 | -5 | -57 |
| route ms/s | 7.3 / 19.8 | 35.5 / 45.0 / 47.1 / 46.7 | +5.0 | -4.4 |
| all keyed waits ms/s | 16.6 / 26.7 | 119 / 110 / 95 / 99 | **-6.6** | -10.1 |

Per-key null (on - off of keys a job does not target, ms/s): steering `0x85FE10` -1.5 .. **+9.4**; avoidance `0x846D21` -5.1 .. +2.4; gate 0 `0x62EF7A` **-4.0 .. -0.3**; gate 12 Contains `0x62F782` -2.5 .. +2.0; `0x83156D` -1.6 .. 0.0; gate 10 `0x760BFF` -0.5 .. +2.6.

What this means [verified from the data above]:

1. **Slot 1 (the first off slice) is the slowest slice in 6 of 7 wait-reducer jobs** (35-38 it/s, 215-247 hitches/min), whatever the reducer. Same save and seed, so the game is in a heavy stretch then. ABBA cancels a linear drift, not that. Every reducer's on side therefore starts with about +15% it/s and -14% hitches/min for free.
2. Leave-one-out excess (a job's on - off minus the other jobs' mean on - off) for the no-op locowin is -11.7 it/s, +35 hitches/min, +42 ms p99. **Run-level metrics (it/s, hitches/min, p99, worst) cannot resolve any reducer on this night;** the "it/s not down > 2%" gate is not measurable with 2 slices per side. Only mechanism-level key shifts larger than the per-key null count.
3. The PLAN entry's summed figures (locowin -46, SlotEnabledRO -55, asyncreplan +26.5, P5c IP +68 ms/s) do not match `wait-ab.ts`'s "all keyed waits" line (-10.1, -14.3, +7.4, +13.8). This doc uses the keyed line; pump (`asyncPumpUs`) and worker job time (`asyncUs`) are given separately where they matter.

## 3. Per reducer, on vs off

Slices are listed `[s2/s3]` for on and `[s1/s4]` for off. All BP.

### 3.1 `[SlotEnabledRO]` (`slotenabledro`, docs/90 design SH)

| metric | on [s2/s3] | off [s1/s4] | on - off |
|---|---|---|---|
| it/s | 42.8 [42.8/42.8] | 35.0 [36.1/34.0] | +7.8 (+22%; bias +15%) |
| hitches/min | 197 [203/190] | 253 [215/292] | -57 (-22%; bias -14%) |
| p99 / worst ms | 137 / 365 | 140 / 341 | -2 / +24 |
| route ms/s | 45.1 | 38.2 | +6.9 |
| gate 12 Contains `0x62F782` | **0.0 [0.0/0.0]** | 12.5 [11.1/13.7] | **-12.5** |
| `0x83156D` | **9.0 [11.2/6.9]** | 1.7 [1.4/2.1] | **+7.3** |
| gate 0 `0x62EF7A` | 9.3 | 13.0 | -3.7 (= null) |
| all keyed waits | 108.1 | 122.4 | -14.3 (no-op -10.1) |
| pump / worker job time | 97.9 / 144.0 | 110.8 / 157.5 | -12.9 / -13.5 |
| hitches whose longest wait is `0x62F782` / `0x83156D` | 1 / 39 | 56 / 10 | 40 vs 66 together |

- The target key is gone in both on slices [verified]. About 60% of it reappears at `0x83156D`, a new key: the return address after the `+0x918` acquire at `0x831568` inside `RoutingWorld_RouteToObjectCheck 0x831530` (docs/81 row 5, docs/84: it calls `0x830340` -> `0x76F0A0` under `+0x918`) [inferred: docs/84 names the `+0x918` take at `0x831568`, and a 5-byte `call` there returns to `0x83156D`; not disassembled in this pass]. So the sim now waits on the outer lock of `IsRouteToObjectUnobstructed` / `IsSlotUnobstructed` instead of inside the slot helper. Net on the two keys: -5.2 ms/s.
- The 10/17 regression (`bp-slotro-ab`, 0.22.2: -17% it/s) is **not reproduced**: both on slices (42.8, 42.8) are above both off slices (36.1, 34.0), and slot 4 (34.0) is not the heavy slot 1.
- PLAN gates (no crash, target key down, keyed total not up, it/s not down): all met.

**Decision: ship in v0.1.5 (on by default).** Gain is small (net ~5 ms/s on its keys, keyed total within noise) but the target is removed, the regression worry is cleared, and it ran 10 min with 0 crashes on the release base. Release check: the v0.1.5 BP/IP benchmark must show no it/s loss beyond the band above; IP was not in this queue. Follow-up lever: a read-only answer for `RoutingWorld_RouteToObjectCheck 0x831530` (the same QL-shared hold one level up), static study first.

### 3.2 `[SteerRO]` alone (`steerro`, S2)

| metric | on | off | on - off |
|---|---|---|---|
| it/s / hitches/min / p99 | 50.4 / 200 / 128 | 37.1 / 240 / 130 | +13.3 / -40 / -2 (all inside bias + band) |
| steering `0x85FE10` | 11.3 [12.8/9.8] | 21.5 [28.5/14.8] | **-10.2** (null max +9.4) |
| avoidance `0x846D21` | 17.4 [22.2/12.6] | 9.9 [9.2/10.5] | **+7.5** (null -5.1 .. +2.4) |
| gate 0 | 8.7 | 12.6 | -3.9 (= null) |
| all keyed waits | 103.1 | 112.9 | -9.8 (= no-op -10.1) |
| hitches whose longest wait is S2 / avoidance | 34 / 67 | 66 / 40 | 101 vs 106 |

The wait moves from the S2 site to the next `+0x918` taker, avoidance steering `0x846D21` [verified: same slices, opposite sign, hitch counts follow]. Net S2 + avoidance -2.7 ms/s.

**Decision: needs more data; not in v0.1.5.** Keep the code (off). Ship only together with an avoidance reducer (PLAN item 4, docs/84 read-only clone of `0x846D21` reusing S2's QL and W stubs); then A/B the pair.

### 3.3 `[SteerRO]` + `[ObstructedRO]` (`steer-obst`)

| metric | on | off | on - off |
|---|---|---|---|
| it/s / hitches/min / p99 | 47.9 / 200 / 131 | 34.7 / 251 / 129 | +13.2 / -50 / +2 |
| steering `0x85FE10` | 10.5 [12.0/9.1] | 18.8 [21.0/16.7] | -8.3 |
| gate 0 `0x62EF7A` | 7.9 [9.4/6.5] | 12.6 [16.2/9.3] | -4.7 (no-op -3.6) |
| avoidance `0x846D21` | 12.2 [14.3/10.2] | 8.1 [11.2/5.1] | +4.1 |
| all keyed waits | 96.8 | 111.5 | -14.7 (no-op -10.1) |
| hitches longest S2 / avoidance / gate 0 | 36 / 46 / 31 | 64 / 28 / 52 | 113 vs 144 |

Same move to avoidance as 3.2; the gate-0 part is the same size as in the no-op. **Decision: needs more data; not in v0.1.5** (same follow-up as 3.2).

### 3.4 `[ObstructedRO]` alone (`obstructedro`, gate-0 fix)

gate 0 7.7 [7.1/8.2] vs 11.5 [12.7/10.4]: -3.8 ms/s, the same as the no-op locowin (-3.6) and inside the gate-0 null (-4.0 .. -0.3). Keyed total -8.0 (no-op level). Hitches longest gate 0: 21 vs 46, but locowin also shows 28 vs 41.

**Decision: drop from v0.1.5.** No effect beyond noise, as expected from its 91% shape fallbacks (docs/116 §4.1). Revisit only after S2 learns the common slot types.

### 3.5 `[LotFrontDoorRO]` (`lotfdro`, design L1)

- Target `0x631814` (cap ~1.3-1.8 ms/s in the other jobs) is missing from the native key table on both sides of this run [unverified why]; hitches whose longest wait is `0x631814`: **0 on vs 10 off**.
- All keyed waits 110.9 vs 117.4: -6.5 (no-op level). it/s 53.3 vs 37.6, hitches/min 181 vs 249, p99 102 vs 138: every run-level number favours on, but inside bias + no-op excess (§2).
- Steering `0x85FE10` +9.5 (on [29.4/23.9] vs off [19.7/14.7]), the top of the S2 null; a 1.8 ms/s front-door key cannot push 9.5 ms/s into steering [inferred], so read as noise.

**Decision: needs more data (low priority); not in v0.1.5.** Gain cap ~1.8 ms/s; one more A/B in a later bundle, watching the S2 key.

### 3.6 `[RouteAsync] Kinds=3` (`asyncreplan`, design A10)

| metric | on | off | on - off |
|---|---|---|---|
| gate 10 `0x760BFF` | 2.5 [3.7/1.4] | 3.7 [3.8/3.7] | -1.2 |
| all keyed waits | 95.2 | 87.8 | **+7.4** (no-op -10.1, leave-one-out excess +15.9) |
| pump / worker job time | 90.5 / 127.9 | 81.5 / 125.2 | +9.0 / +2.7 |
| waits in the seconds file (rtLock + other + job + plan) | 105.7 | 86.4 | +19.3 |
| it/s / hitches/min / p99 | 45.0 / 195 / 122 | 41.7 / 220 / 113 | +3.3 / -25 / +9 |

The plan time leaves the sim thread and comes back as waits, as in `ip-ar-ab` (Kinds=7, +49 ms/s). Fails the "keyed total not up" gate. **Decision: drop** (keep code off; no further A/B unless gate 10's sync callers change).

### 3.7 `[LocoWindow]` (`locowin`, design H2)

No-op as expected (3 of the gate-0 waits fall inside the locomotion pass, docs/86). Its numbers are the noise reference in §2. **Decision: drop** (nothing to ship).

### 3.8 P5c snapshot path (snap-p5c-ab quiet rerun, 0.35.26)

| metric | BP on | BP off | BP on - off | IP on | IP off | IP on - off |
|---|---|---|---|---|---|---|
| it/s | 44.5 [41.0/48.1] | 50.7 [44.8/56.6] | -6.2 (-12%) | 69.2 [67.1/71.3] | 68.2 [71.2/65.1] | +1.0 (+1%) |
| hitches/min | 183 | 164 | +19.5 (+12%) | 125 | 121 | +3.8 |
| p99 / worst ms | 97 / 255 | 86 / 385 | +11 / -130 | 106 / 457 | 104 / 521 | +2 / -64 |
| all keyed waits | 98.6 | 93.4 | +5.2 | 80.2 [82.9/77.4] | 66.4 [75.3/57.7] | **+13.8** |
| pump / worker job time | 91.3 / 145.4 | 86.3 / 146.7 | +5.0 / -1.3 | 89.4 / 125.8 | 70.4 / 104.1 | +19.0 / +21.7 |
| IP gate 0 / gate 12 Contains | | | | 9.8 / 9.6 | 4.1 / 4.6 | +5.7 / +5.0 |

BP is the only BP job whose on side lost against the slot bias (leave-one-out excess -14.0 it/s, +59 hitches/min, +13.3 keyed). IP keyed waits up in both on slices vs the lower off slice. **Decision: drop from v0.1.5; P5c stays parked** (confirms PLAN 2026-10-20).

## 4. Decision table

| Reducer | Target key on / off (ms/s) | Where the wait went | All keyed waits on - off (no-op -10.1) | it/s on - off (bias +15%) | hitches/min on - off (bias -14%) | p99 on - off | Decision |
|---|---|---|---|---|---|---|---|
| `[SlotEnabledRO]` | `0x62F782` 0.0 / 12.5 | `0x83156D` +7.3 (RouteToObjectCheck `+0x918` acquire, inferred) | -14.3 | +7.8 (+22%) | -57 (-22%) | -2 | **ship in v0.1.5**; next: read-only `0x831530` |
| `[SteerRO]` | `0x85FE10` 11.3 / 21.5 | avoidance `0x846D21` +7.5 | -9.8 | +13.3 (+36%) | -40 (-16%) | -2 | needs more data: pair with an avoidance reducer |
| `[SteerRO]`+`[ObstructedRO]` | S2 10.5 / 18.8, gate 0 7.9 / 12.6 | avoidance +4.1; gate 0 = null | -14.7 | +13.2 (+38%) | -50 (-20%) | +2 | needs more data (as above) |
| `[ObstructedRO]` | `0x62EF7A` 7.7 / 11.5 | none measurable (= no-op -3.6) | -8.0 | +5.4 (+14%) | -36 (-16%) | -13 | drop from v0.1.5 |
| `[LotFrontDoorRO]` | `0x631814` hitches 0 / 10 (key not in table) | S2 +9.5 (noise, inferred) | -6.5 | +15.7 (+42%) | -68 (-27%) | -36 | needs more data (low priority) |
| `[RouteAsync] Kinds=3` | `0x760BFF` 2.5 / 3.7 | back as waits: pump +9.0 | **+7.4** | +3.3 (+8%) | -25 (-11%) | +9 | drop |
| `[LocoWindow]` | `0x62EF7A` 8.2 / 11.8 | no-op | -10.1 | -4.1 (-8%) | -1 | +38 | drop (noise reference) |
| P5c (BP / IP) | - | IP gate 0 +5.7, gate 12 +5.0 | +5.2 / **+13.8** | -6.2 / +1.0 | +19.5 / +3.8 | +11 / +2 | drop, stays parked |

## 5. What this does not settle

- No reducer's run-level effect (it/s, hitches/min, p99) is resolved; that needs a clean night, or more slices per side (a 20-min run = 4 per side cuts the standard error only ~1.4x), and the slot effect handled: alternate the start side (off-first vs on-first) between two jobs of the same reducer.
- `0x83156D`'s mapping to `0x831568` is inferred from the address; confirm in the disassembly before designing its reducer.
- Why `0x631814` was absent from the native key table in the lotfdro run is not checked. (2026-10-22: the 32-slot key table overflow, §6.)

## 6. SlotEnabledRO on the clean night: BP -5.4% it/s, what it costs (2026-10-22)

Data: `probe-out/profile/clean-night-0.2.0-2026-10-21-{bp,ip,sv}-slotro-{offfirst,onfirst}` (asi `.0314`, release base, `SlotEnabledRO` Mode 1 Shadow 0, `--ab slotenabledro`). Measured unless marked.

| BP | off-first (on / off) | on-first (on / off) |
|---|---|---|
| it/s (all seconds of the counted slices) | 46.8 / 48.7 (-4.0%) | 50.5 / 53.5 (-5.5%) |
| gate 12 `0x62F782` ms/s | not timed in either half (see below) | 0.1 / 10.5 |
| `0x83156D` (RouteToObjectCheck, docs/140) ms/s | 5.5 / 0.7 | 6.3 / 1.0 |
| summed keyed wait ms/s | 89.6 / 78.6 (gate 12 untimed) | 79.4 / 92.4 |
| RO body `slUs` | 6.3 ms in 240 s (574 answers, 11 us each) | 6.6 ms in 245 s (900 answers) |
| worker job time `asyncUs` ms/s | 128.3 / 131.7 | 120.0 / 127.6 |

What it is not [verified, counters]: the RO body costs ~0.03 ms/s (`slUs`, max 220 us); the QL-shared hold does not slow the worker (`asyncUs` lower on, `steerQlWaitUs` 0); script, route and locomotion ms/s move < 1.6 ms/s, in opposite directions in the two orders. Gate 12 drops 10.4 ms/s and about half of it reappears at `0x83156D`: `IsSlotUnobstructed` (`0x755AB0`) calls the slot helper first and `RoutingWorld_RouteToObjectCheck 0x831530` right after (docs/140 [verified]), so the same IC waits one call later. The rest goes to the next taker in the tick (conserved wait, docs/143 §8.14).

The off-first BP table lacks gate 12: the warm-up slice is on, so gate 12 had no wait until the 32 probe-P0 slots were full, and its off-half waits only counted in `blockOther` (200 in the first off slice vs ~20-60 in the others) [verified]. Corrected off-first total with ~10 ms/s for gate 12 [inferred]: ~89 off vs 89.6 on; pair mean ~-6 ms/s summed wait. The same overflow is why `0x631814` was absent in the lotfdro run (§5).

Verdict: no cost mechanism found. -4.0 / -5.5% (pair -4.3% from all seconds, -5.4% in the draft) is within the BP pair noise: the standard deviation of pair means over 10 BP switch pairs of the same two nights is 5.0% (avoidro -1.0, slotro -4.3, spreads -0.3, unseen-play +8.5, ut-play +3.5, fb-play +0.9, ut2 avoid -3.1, footprint -8.9, list +5.4, rate -2.8). IP +4.9%, SV -1.9%. A smaller summed wait does not show in it/s at this noise. The earlier BP ship decision rested on gate 12 -12.5 ms/s, which holds; an it/s change is not shown in either direction. What would gain: docs/140 `[RouteToObjRO]` for the moved half, and only together with the tick's other takers (docs/137 §12). Re-measure with every key timed: `tools/harness/queues/rofix-ab.json` (`bp-slotro-verify` day job, then `bp/ip-slotro-offfirst/onfirst`, asi `.rofix`). Ledger docs/51 §218.
