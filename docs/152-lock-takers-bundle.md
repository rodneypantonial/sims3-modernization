# 152. One tick, all takers: the `+0x918` lock-taker bundle

Static study and existing data only, 2026-10-22. No build, no game run, no queue. Sources: docs/81-90, 94 §22.9-22.11, 102/106 (`docs/data/routing-indirect-sites.csv`), 136, 137, 140, 143 §8.14-8.15, `native/ts3native/{p5b,p5c,steerro,slotro,avoidro,discourage}.cpp`, `tools/harness/{wait-ab,profile-world}.ts`, and `node tools/harness/wait-ab.ts` run on three clean-night BP tags (copies of `probe-out/profile/clean-night-routing-extra-2026-10-22-bp-{avoidro,deferfp}-offfirst-*` and `clean-night-0.2.0-2026-10-21-bp-slotro-onfirst-*`). Tags: [verified] = read in code, disassembly-backed docs or counters; [measured] = run files; [inferred] = reasoning, not yet checked.

## 1. Summary

- **The question.** Four reducers each removed their own key and the sim's summed `+0x918` wait did not move: AvoidRO, SlotEnabledRO, DeferFootprint and the unseen throttle (docs/137 §12, docs/136 §6, docs/89 §11, docs/143 §8.15) [measured]. The wait is bounded by the route job's hold (`asyncUs` ~110-128 ms per wall second BP), not by how many sim takers there are. A removed taker hands the wait to the next one in the same tick.
- **Model [inferred, matches all four results].** In an iteration where a job is live, the sim waits for the rest of that job's hold as soon as any taker touches `+0x918` (or a lifetime wait). The iteration gains only if no taker touches the lock while the job is live. There are two ways to get there: (a) make every taker lock-free (read-only clone, deferral or park), or (b) lend or free the lock during the job (P5b/P5c). Per-tick takers decide the gain. Rare takers (script ICs, events) leave a residual in proportion to how often they fall into a job.
- **Taker inventory (§2).** 15 mapped keys plus about 7 unmapped ones. Four classes: read-only with balanced side-effect writes (steering, gather, avoidance, gate-0 hits, gate-12 read families, route-to-object), writers (UpdateSimFootprint and its siblings, gate 10, the gate-12 writer families), lifetime (destroy), and unknown.
- **The bundle (§4).** Arm B0 is what exists today (7 switches, no build). It cannot gain: gate 0 G-a and `0x83156D` still sit in every locomotion pass or right after SH. Arm B1 is B0 plus the gaps G1-G5, G7 and G8, about 20-33 h of work. The biggest gap is a gate-0 hit path under QL shared for the G-a callers, which becomes safe only because S2 and AvoidRO already make the callers' next touches read-only. The other gaps: RouteToObjRO, design U phase 2 (`0x8334A0`), deferral of `UpdateObjectLot` and `RemovePrivateFootprint`, and mapping the unknown keys. Arm B2 is B1 plus P5b `ReleaseLock`/`ReleaseGates`, which catches the RO clones' fallbacks during windows. P5c stays out until its own regression (docs/136 §3.8) is explained.
- **Expected gain [inferred].** In B1 the BP summed keyed wait drops from ~76-92 to ~20-35 ms/s (about -45 to -60 ms/s). If the sim is CPU-bound in those seconds, that is about +3-6% it/s. B0 should show about 0: all of the wait moves to gate 0 and `0x83156D`. That B0 result is the cheap way to falsify the model.
- **Compatibility (§5).** No lock-order inversion: both threads take `+0x918` before QL, and RO clones run only when the sim does not own `+0x918`. The same destroy cannot be parked twice, because design G and P5c share `P5cPark` and one graveyard [verified]. Three interactions need care:
  1. Admitted writers under P5c make clone answers stale and force the full fallback plan under the retaken lock. Deferrals reduce this.
  2. A forced `DiscDrain` while the sim holds the lent lock triggers the P5c hand-over tripwire, which turns `FreeLock` off for the session [inferred].
  3. With no sim waits, the job's managed callbacks (door asks) are pumped less often. `asyncUs` and `m2DoorWaitUs` can rise (docs/94 §22.11 risk 5). This may be part of P5c's IP +21.7 ms/s job time [inferred].

## 2. Takers, measured size and class

Keyed sim wait in ms/s BP, `wait-ab.ts`, off half of the three clean-night tags (avoidro off-first / deferfp off-first / slotro on-first) [measured]. "On" values are given only where the tag's own reducer moves the key.

| Key | Taker (function, caller) | Pass / stage | BP off ms/s | Moved by its reducer (on) | Class |
|---|---|---|---|---|---|
| `0x85FE10` (+`0x85FE81`) | steering `RoutingWorld_ObstacleQuery 0x82EDB0` from `Locomotion_ObstacleCheck 0x85FD10` | locomotion, per tick per walking Sim | 17.0 / 17.2 / 18.9 | AvoidRO on: 23.9 | RO + balanced writes |
| `0x85F6CB` | goal footprint gather `0x82F0B0` from `RouteFollow_EditPath 0x85F5A0` (`0x85F6C6`); also sync-plan EditPath tails (`0x85FFC7` merged here, docs/87) | locomotion + script | 1.0 / 0.6 / 1.9 | - | RO + balanced writes |
| `0x846D21`, `0x85B50C` | avoidance `0x82EDB0` from `RouteFollow_A 0x846B70` / `_B 0x85B470` | locomotion | 8.2 (A-site key `0x6F28F468`) / 4.9+0.4 / 5.5 | AvoidRO on: 0.3 | RO + balanced writes |
| `0x62EF7A` | gate 0, `LotTable_GetOrInsert 0x62EAB0` from transform wrapper `0x62EF30`; G-a (RouteFollow_A/B, EditPath) and G-b (`0x751480`, `0x82E340`) | locomotion (G-a) + script (G-b) | 11.6 / 10.0 / 13.0 | slotro on: 11.3, deferfp on: 12.1 | RO on a hit, writer on a miss |
| `0x62F782` | gate 12 `LotTable_Contains 0x628A90`; F-B via slot helper `0x76F0A0` (`0x76F15D`) is ~69% | script ICs (`IsSlotEnabled`, `IsSlotUnobstructed`) | 8.1 / 9.8 / 10.5 | SlotEnabledRO on: 0.1 | RO (F-B) |
| `0x631814` | gate 12 F-C, front door `IC_World_GetLotFrontDoorImpl` | script | 1.4 / 2.3 / 1.6 | - | RO |
| `0x631F40` | gate 12 F-G, `FootprintsNearPoint` (placement) | script | 1.4 / 0.8 / 1.6 | - | RO |
| `0x62FB74` | gate 12 `GetIfPresent` (F-A footprint level rebuild, F-D portal writer ICs, F-F) | script / rebuild | 3.2 / 2.5 / 2.7 | - | writer families |
| `0x83156D` | `RoutingWorld_RouteToObjectCheck 0x831530` (`Lock_Acquire` at `0x831568`) | script ICs, right after the slot helper | 0.8 / 1.0 / 1.0 | SlotEnabledRO on: 6.3 | RO + balanced writes |
| `0x760BFF` | gate 10, goal step `0x75C2D0` in `Route_PlanCommon` of a sync plan | script ICs | 3.1 / 2.8 / 3.6 | - | writer (real dependency) |
| `0x833593` | `UpdateSimFootprint 0x833560` | both animation passes (`Locomotion_Notify`), event `0x21`, footprint ICs | 6.1 / (untimed) / 8.2 | DeferFootprint on: key gone (docs/89 §11) | writer |
| `0x8334CC` | `ObjectFootprintFromTool 0x8334A0` (event `0x0E` transform published) | animation root motion, script `SetPosition` | 1.8 / 3.3 / 5.7 | absorbs: 5.5 / 6.6 / 2.4 | writer |
| `0x82D296` | `UpdateObjectLot 0x82D280` (event `0x0A` lot change) | events | 1.1 / 0.8 / 2.2 | absorbs: 2.4 / 3.0 / 0.7 | writer |
| `0x82A0F6` | `RemovePrivateFootprint 0x82A0C0` (`IC_RouteManager_RemovePrivateFootprint`) | script | 0.7 / - / 0.2 | - | writer |
| `0x2000003` | destroy, closure #4 (`RtWaitJob`; not a `+0x918` acquire) | script, events | 5.9 / 5.3 / 7.0 | absorbs: 7.2 / 7.7 / 6.1 | lifetime |
| unmapped: `0x5000000`, `0x4B0021`, `0x75A64C`, `0x729E2C`, `0x728DC3`, `0x7345F2`, `0x831BF4`, small rest | not in any doc's key map; `0x75A64C` appears as a 134-296 ms `rtLock` frame in docs/134 §10 | ? | ~4-9 together | - | unknown |
| **all keyed** | | | 75.7 / 67.7 / 92.4 | on: 86.6 / 80.2 / 79.4 | |

The "absorbs" column is the conservation of docs/143 §8.14, read directly. With AvoidRO on, steering rises by +6.9 and `0x8334CC` by +3.7. With DeferFootprint on, `0x8334CC` rises by +3.3, `0x82D296` by +2.2 and destroy by +2.4. With SlotEnabledRO on, `0x83156D` rises by +5.3 [measured, single tags; pair means in the cited docs].

## 3. Downstream, 5 hops: what each taker reads and writes under the lock

"Indirect" gives the docs/102/106 site status inside the taker's own body (`routing-indirect-sites.csv`, rows whose caller is the function: resolved / partly / unresolved) [verified counts]. Closures are taken from the cited docs and were not re-walked here.

| Taker | Reads under `+0x918` (5 hops) | Writes under `+0x918` (5 hops) | Managed / event / other locks | Indirect in body | Verdict | Source |
|---|---|---|---|---|---|---|
| `0x82EDB0` steering / avoidance | lot table `P+0x48`, lot entry transform and level vector, per-level quadtree nodes and footprint lists, footprint type/id/class slots, polygon caches `[fp+0x38]+i*0x24`, world manager lot grids (`World_LotLookup 0xC68990`, lock `wm+0x228` leaf) | quadtree pin `Q+0x48` (plain +1/-1, balanced), deferred flush `0x666A40` on an outermost query (frees nodes, clears `Q+0x4C`), footprint refcounts `[fp+4]` (balanced; delete at 0 through the vtable), polygon-cache rebuild (`+0xF8`/`+0xFC` `0x80E5B0`, which reaches `GetOrInsert`, `World_GetHeightAt` and object lock `X+0xA0`) | none under the lock; the agent vtable `+0x60`/`+0x64` -> `World_FireEvent 0x3A/0x39` fire **after** release (docs/82 §1); leaf locks `0x11D9068`, `0x11D8F90`, `X+0xA0` | 0/0/2 (`vcall-lifetime`, `vcall-delete`) | **RO with balanced writes**: an RO clone that skips the writes (S2, AvoidRO) is admissible; P5b admits the game's call | docs/81 §3, 82 §1-4, 84 §3, 137 §3 |
| `0x82F0B0` gather (EditPath) | same as `0x82EDB0` plus the goal point's footprints | same | same; EditPath has 16 indirect sites (0/7/9) | 0/0/2 | RO with balanced writes; S2 covers it (`steerro.cpp` calls the EditPath consumer itself) | docs/83 §2-4, `steerro.cpp` header |
| `0x62EAB0` gate 0 | hit: `LotTable_Find 0x627E00` (count `+8`, buckets `+4`, chain `node+0x190`) | hit: the caller's stack only. Miss: `LotGraph_ctor`, `LotTable_Insert 0x62E680`, `Rehash 0x625940`, allocator | none within 5 hops | 0 sites | **RO on a hit** (P5b gates use `LtFind`). The real risk is what the callers do next without the guard (docs/86 option R) | docs/86 §3, 5 |
| `0x628A90` gate 12 / slot helper `0x76F0A0` | lot table, lot entry portal vector `entry+0x44/+0x48`, lane geometry | portal AddRefs (net +1, released by the caller's `0x5BF1E0` tail); otherwise none | terrain/cell `Lock_Acquire` at hops 4-5 (not `+0x918`; SH drops QL first) | `0x76F0A0` 0/1/0, `0x62F760` 0/0/1 | **RO** (F-B, F-C, F-G); F-A/F-D/F-F callers write | docs/90 §2, 94 §22.10.1 |
| `0x831530` route-to-object (`0x830340`, `0x82E340`) | lot table, transforms, quadtree point and segment walks, polygon caches, object maps of a level (`0x6643E0`), slot helper, planner flag `P+0xD0` bit 16, physics world | pin and flush, footprint refcounts and the `0x6649C0` write-back, polygon-cache rebuild | physics reads are sim-owned; no managed call | `0x830340` 0/11/0, `0x82E340` 0/15/0 | **RO with balanced writes**: needs a clone (SH over the whole body was rejected because of the plain refcounts, pin and flush). Open: the object-map inserter (I-198-5) and the `P+0xD0` writer (I-198-6) | docs/140 §3-4 |
| `0x75C2D0` gate 10 | candidate goal records, lot planner, room graphs | lot planner A* object Q (`AStarSearch_Reset 0x61E320`), lazy `LotGraph_Build 0x63CB90` / `RoomGraph_FirstUse 0x63D2F0`, lot table find-or-insert, quadtree pin and flush, lot index rebuild (`wm+0x228`), then `Route_PlanLocked` | none; `wm+0x228` leaf | 1/13/1 | **writer, real dependency**: no RO form; only moving the sync plan off the sim (A10 `Replan`) or accepting it as a residual | docs/87 §1 |
| `0x833560` UpdateSimFootprint | lock-key map `+0x8B8`, lot, footprint, transform, steering components | `P+0x10D`, world `+0x994`, remove-all + re-insert of footprints (`LotQuadTree_Insert 0x666C50`, `0x6675B0`, flush), footprint objects + refcounts, lot table, `QuadTreeBuild 0x62F8A0`, `MarkDirty`, registry `0x11D9068`, entry pool `0x11CEFA8`, `RemovePortals 0x6399E0` | none within 5 direct hops (and 6 through the 93 routing vtables) | 0 in the body; `0x8321F0` 15/2/0 | **writer**: defer (design U, built) | docs/89 §3.1 |
| `0x8334A0` ObjectFootprintFromTool | as `0x833560`, `enable = 1`; caller brackets world `+0x978` | as `0x833560` (not-parked branch) | none [inferred: same core `0x8321F0`] | 0 | **writer**: defer as U phase 2 (the `+0x978` bracket must be replayed) | docs/89 §1-2 |
| `0x82D280` UpdateObjectLot | object lot id, lot membership | footprint re-home across lots (the same core and lot routing writes) [inferred from docs/89 I-89-6] | none known | 0 | **writer**: defer with U's per-object FIFO | docs/89 §5.2 |
| `0x82A0C0` RemovePrivateFootprint | object by `FindById` (AddRef/Release) | footprint remove, quadtree, lot table | none known | 0 | **writer**: defer (D-queue family) | docs/89 §3.1, 94 §22.10.1 |
| destroy `0x939420` | object id hash, owners | update list, id hash, lot membership, render lists; **no routing state** within 7 hops | managed release at the last `lock dec` (`Object_Destruct`) | `0x939420` 0/2/0 | **lifetime**: park (design G, built) | docs/88 §3 |

Nothing in any closure above calls managed code, fires an event or waits for a job while it holds `+0x918` [verified in the cited docs, within 5 direct hops; vtable calls followed only where the docs say so]. Every taker is therefore admissible in principle to lending (no deadlock against the worker's retake). The difference is what it writes: RO takers can run beside a clone run, but writers make the clone's answer stale.

## 4. The bundle: what makes a full locomotion + animation pass lock-free while a job runs

Order of one sim iteration [verified, docs/81 §2, 86 §5, 89 §1]: script tasks (`0xD81A41..0xD81BDF`, where `Route.Plan` starts jobs), `Animation_SACSPass` (`0xD81A70`), `Locomotion_Update` (`0xD81C5C`), `Animation_UpdateActors` (`0xD81C73`). A job that starts in the task stage meets every pass taker in the same iteration.

### 4.1 Per taker: existing piece, gap, effort

| Taker | Pass or script | Existing piece | Status | Gap, effort (hours, Claude with agents) |
|---|---|---|---|---|
| steering `0x85FE10` + gather `0x85F6CB` | pass | S2 `[SteerRO]` (`steerro`) | built, ships off; falls back on `SR_FB_*` | none; the bundle report must show the fallback rate |
| avoidance `0x846D21` / `0x85B50C` | pass | `[AvoidRO]` (`avoidro`) | built, off | none |
| gate 0 G-a (RouteFollow_A/B, EditPath second frames) | pass | ObstructedRO covers only `IC_Route_IsPointObstructedBySim`; P5b gates admit hits only inside windows; H2 only delays new job starts | **gap** | **G1 gate-0 G-a hit RO**: in `RgEnter`, for the G-a second frames only and only while S2 and AvoidRO are on, do `LtFind` under QL shared; on a hit, skip the `+0x918` wait (the hit path writes nothing). docs/86 rejected option R because it "removes the guard for what the callers do next". In this bundle the callers' next touches are the S2/AvoidRO clones, so the guard is no longer needed. G-b frames keep the gate. Reuses the `p5b.cpp` hit check. **3-5 h** + selftest; needs the gate-0 split by second frame (P1b) to size G-a vs G-b |
| UpdateSimFootprint `0x833593` | animation passes | design U `[Discourage]` USF (`deferfootprint`) | built, off | none |
| `0x8334CC` | animation root motion, script | - | **gap** | **G2 U phase 2** on `0x8334A0`: replay the caller's `+0x978` bracket, per-object FIFO with U, **3-5 h** |
| `0x82D296` | events | - | **gap** | **G3 defer `UpdateObjectLot 0x82D280`** into the same per-object FIFO (it re-homes footprints; order against U's records, I-89-6), **3-5 h** |
| `0x82A0F6` | script | - | **gap** (small) | **G4 defer `RemovePrivateFootprint`** in the D family, **1-2 h** |
| gate 12 F-B | script | `[SlotEnabledRO]` SH | built, release on | none |
| `0x83156D` | script (right after F-B) | `[RouteToObjRO]` design (docs/140); built 2026-10-22 (docs/140 §12, counters v93, `--ab routetoobjro`), not run | **gap** (built, live open) | **G5 build RouteToObjRO**, **8-12 h** + 2 game runs (docs/140 §11) |
| gate 12 F-C | script | `[LotFrontDoorRO]` L1 (`lotfdro`) | built | none |
| gate 12 F-G | script | P5b gates (window only) | partial | **G6 optional**: lotfd-style RO, **2-3 h** (0.8-1.6 ms/s) |
| `0x62FB74` writer families | script, rebuild | - | residual | none planned (real writers, 2.5-3.4 ms/s) |
| gate 10 | script (sync plans) | A10 `Replan` (`asyncreplan`) measured +7.4 keyed (docs/136 §3.6) | residual | none in this bundle |
| destroy | script, events | design G (`destroypark`), the same `P5cPark` as P5c [verified] | built, off (docs/67 §11 crash I fixed in 0.36.19, 1 of 2 verify nights) | none |
| discourage add/remove | script | design D | built | none |
| unmapped keys | ? | - | **gap** | **G7 map the 7 keys statically** (`0x5000000`, `0x4B0021`, `0x75A64C`, `0x729E2C`, `0x728DC3`, `0x7345F2`, `0x831BF4`), **1-2 h**; then decide per key |
| measurement | - | probe P0 32 key slots, rofix seeds 14 keys | partial | **G8 seed every bundle key** (about 24) + a bundle report (`wait-ab.ts` with removed / residual / absorbed groups and per-piece fallbacks; one avoided wait counted once), **1-2 h** |

The pass becomes lock-free with S2 + AvoidRO + G1 (locomotion) and U + G2 (both animation passes). The script stage then needs SH + G5 + L1 + G3/G4 + G (park). Residual takers in B1: gate 10, `0x62FB74`, F-G (without G6), gate-0 G-b, RO fallbacks, D-X destroys, and whatever G7 finds.

**Total gap effort: G1-G5 + G7 + G8 = ~20-33 h, ~22-36 h with G6.** Game runs: 2 for G5's own shadow, plus the bundle jobs (§4.3).

### 4.2 Arms

| Arm | Switches (all in one `--ab` list, toggled together at every slice) | Build | Expected summed keyed BP [inferred] | Purpose |
|---|---|---|---|---|
| **B0** | `steerro,avoidro,slotenabledro,lotfdro,obstructedro,deferfootprint,destroypark` | none (rofix asi + seeded keys) | about equal to off: gate 0 G-a, `0x83156D`, `0x8334CC`, `0x82D296` absorb | falsify the model: if B0 drops by >= 20 ms/s, the "one taker is enough" model is wrong and the gaps matter less |
| **B1** | B0 + `gate0ro` (G1), `routetoobjro` (G5), U2/lot/private deferrals (G2-G4 as `deferfootprint` modes or their own switches) | G1-G5, G7, G8 | ~20-35 (from 68-92): **-45 to -60 ms/s** | the bundle |
| **B2** | B1 + `routesnap` Mode 2 with `routesnaprel,routesnapgate` | none beyond B1 | B1 minus the RO fallbacks inside windows (a few ms/s, times *f* of docs/94 §22.10.4) | does lending add on top of RO? |
| (P5c) | not in the bundle | - | - | stays parked until docs/136 §3.8 (BP -12% it/s, IP keyed +13.8) is explained; see §5 for why writers in a P5c window make it worse |

`[LocoWindow]` (H2) stays off in every arm. Once the pass is lock-free it only delays job starts, and it pushes held starts onto `Animation_UpdateActors`' footprint updates (docs/89 §1). `asyncreplan` stays off: gate 10 is a residual.

Why B1's figure: the residual takers are per event, not per tick. Gate 10 waits about 0.47/s for about 7.7 ms each (docs/87). `0x62FB74`, F-G and G-b are similar in size. Under conservation each residual event absorbs the rest of the job's hold, so the residual is roughly (residual events per second that hit a live job) x (mean remaining hold): about 2-4/s x 8-10 ms ≈ 15-35 ms/s [inferred]. CPU added: the RO bodies are tiny (SH 0.03 ms/s [measured]), and S2/AvoidRO run anyway in place of the game's call. it/s gain: about 5% of wall time freed in BP. That turns into iterations only where the sim is CPU-bound rather than paced, so the forecast is **+3-6% BP, IP smaller** (IP summed keyed 66-80) [inferred]. Both are below or at the BP pair SD of 5.0% (docs/136 §6), so the keyed wait is the primary metric and it/s needs at least 4 pairs.

### 4.3 Bundle A/B as one switch

- **Harness:** `profile-world.ts --ab <arm list>` already toggles every named counter switch at the same slice boundary (`counterSwitches`, one `on` per slice) [verified source]. Add a preset name `lockbundle` that expands to the arm's list, and seed every key of §2 (G8) so that no key goes untimed.
- **Jobs:** BP + IP, 10 min, `--slice 2`, both orders (`--ab-on-first` twin), clean night, machine load line ok. B0 first: one day verify job (mechanism only) + one clean pair. B1 after G1-G8: day verify, then 2 clean nights x BP/IP pairs (4 BP pairs to bring the it/s SE under 2.5%).
- **Pass rules:** (1) summed keyed wait on <= off - 20 ms/s BP (B1), untimed waits/min about 0; (2) it/s pair mean >= off - 2% (offload rule); (3) per piece, fallbacks per minute and removed-key wait about 0 on; (4) `asyncUs` on <= off + 10% and `m2DoorWaitUs` / pump split reported (§5 item 6); (5) 0 crashes, 0 new stuck Sims, S2/AvoidRO/SH shadow mismatches 0 when shadow is on in the verify job; (6) the residual keys' sum explains >= 80% of the on side's summed wait (otherwise there is an unmapped taker).
- **Reading B0:** about 0 net, with gate 0 G-a and `0x83156D` rising by about the removed keys' sum, confirms the model and the gap order. A large drop says the residuals cost less than one hold each, and G1/G5 can wait.

## 5. Compatibility of each RO / defer / park piece with P5b and P5c

| Piece | Lock it uses | With P5b (lent to steering + gate leaves) | With P5c (lock free in the search) | Tripwires and double counts |
|---|---|---|---|---|
| S2 | QL shared, only while `A_RUNNING` and the sim does not own `+0x918` | S2 answers first. A fallback calls the game's `0x82EDB0` in an `RT_STEER` frame, which P5b admits: the two layer cleanly | the same; admitted sim writers now run during jobs, so `steerSimTreeWrite` and `SR_FB_TRIP` rise and fallbacks take the free lock | a fallback granted by P5b counts in both `steerFb*` and `p5bSimAcq`. The bundle report counts the avoided wait once (key time is the truth) |
| AvoidRO | QL shared; misses call the game's `0x82EDB0` from AvSite | AvSite's pass-through still enters `0x82EDB0`, so the `RT_STEER` admission holds [inferred from the frame being on `0x82EDB0`, not the call site]. The P0 key becomes the asi return address (docs/137 §12) | as S2 | T-AV (`0x81D090` / `0x85FF30`) is independent of lending. Seed AvSite's key |
| SH (SlotEnabledRO), L1, ObstructedRO, G1, G5 | QL shared over the game body or a clone; the gates inside are skipped | with `ReleaseGates`, P5b admits only SH/L1 fallbacks (`slFb`); gains do not add (docs/94 §22.10.4) | the same; G5's skipped `0x831568` acquire would be admitted by P5c anyway, but G5 also works outside windows | **check:** each RO path must take the game path when the sim already owns `+0x918` (a lent or recursive hold), or it would hold QL shared under `+0x918`. That order is the same as the worker's (`+0x918` then QL), so it cannot deadlock, but it would hide a P5c stale write from S2's T3. Confirm the owner test in `slotro.cpp`, `lotfd.cpp`, `obstructro` and the new G1/G5 [not verified here] |
| U / U2 / lot / private deferrals (D queue) | none at record time; apply at D drain points with no job | the writer never reaches the lock, so P5b refuses fewer windows (`p5bRefused[2]`, a sim waiter inside `Lock_Acquire`) | **synergy:** a deferred writer never takes the lent lock, so `p5cStale[1]` (routing writes in the window that refuse the clone answer and force the game's plan under the retaken lock) falls. **Risk [inferred]:** a forced `DiscDrain` (queue full, record older than 250 ms) while the sim owns the lent lock goes through `WaitAsyncDone`, which is the P5c hand-over tripwire and sets `FreeLock` off for the session (docs/94 §22.11.1). Either the drain points must be outside any sim `+0x918` hold, or the force rule must skip while lent | `usfApplyInJob` must stay 0 |
| G (DestroyPark) | none; one `FindByIdAddRef` + flush at a no-job point | P5b keeps destroys waiting; G parks them anyway (standalone) | the same function `P5cPark`, one graveyard (512) and one flush [verified `p5c.cpp`]: no double park, no double release | `dpKeptWait` + `p5cKeptWait[4]` both count a kept wait; sum them once |
| QL shared vs lent `+0x918` | - | the worker's retake waits only for `+0x918`. A sim QL-shared hold never blocks it, because the worker takes QL exclusive only under its own `+0x918` (W1-W6) | the same; during the clone search the worker takes no QL, so RO clones never wait | no double-counted hold: `rgOwnerTid` tracks only `+0x918`, and QL holds are counted by the `steerQl*` counters |
| H6 borrow / door asks | - | windows close before door asks (§22.9.1) | the same | **risk [inferred]:** with no sim waits, the sim pumps job callbacks less often (`asyncPumpUs` is the sim's wait time spent serving them). Door asks then wait for the sim's next pump point, so the job's hold and `asyncUs` grow. This is docs/94 §22.11 risk 5, and P5c's IP `asyncUs` +21.7 / pump +19 ms/s (docs/136 §3.8) fits it. Measure `m2DoorWaitUs` per slice in every arm; if it grows, add a pump point at the end of `Locomotion_Update` (cheap, sim thread) |

**Why P5c stays out of the bundle:** P5c admits every plain writer acquire. Each admitted routing write inside the window makes the clone answer stale, the worker then runs the game's full plan under the retaken lock, and the sim's next taker waits for that longer hold. With B1's deferrals, the window sees almost only RO takers, which P5b already admits, so P5c would add little beyond P5b and keep its own risk. Revisit P5c only after B2.

## 6. Open items

- I-152-1: G-a vs G-b split of the gate-0 key by second frame (P1b data or a seeded second-frame key). It sizes G1.
- I-152-2: what the seven unmapped keys are (G7).
- I-152-3: the owner test of every RO path when the sim already owns `+0x918` (§5).
- I-152-4: forced `DiscDrain` under a lent lock (§5).
- I-152-5: the door-ask pump effect on `asyncUs` with the pass lock-free (§5).
- Other lanes cover the job's own hold length (job hold breakdown, upstream job sources, RouteEarlyOut acting mode, P5 correctness). A shorter hold scales every residual in §4.2 down in proportion.

## G1. Gate-0 G-a hits under QL shared: `[Gate0RO]` (built 2026-10-22, not run)

`native/ts3native/gate0ro.cpp` (+ `RgEnter` / `RgLeave` in `routegate.cpp`), ts3native 0.36.23 (version not bumped), counters **v91** at `0x3998` (built right after v89; v88 and v90 are other branches', so v91 moves at its merge; docs/registry.md). `[Gate0RO] Enabled=0 Mode=1 Shadow=0`, runtime switches `g0Mode` (`--ab gate0ro`) and `g0Shadow` (`--ab gate0shadow`). Parked `game-copy/ts3native.asi.gate0ro`, sha256 `DEE0A1BC9E59C7DD6BB0225A753F9A8A69DE7E5C476ABFFD605E5ADEC536E84F`. Ledger §239 (I-152-G1-1..8).

### G1.1 What was checked in the disassembly

| Check | Result | Tag |
|---|---|---|
| Gate-0 hit path | `LotTable_GetOrInsert 0x62EAB0` calls `LotTable_Find 0x627E00`, on a hit (`jne 0x62EB34`) returns `node + 0x10` in `eax`, `ret 4`; writes only its stack. A miss builds, inserts, may rehash | verified |
| Wrapper frame | `0x62EF30` keeps `ebp` (`push ebp; mov ebp, esp`), so the gate stub reads its caller at `[slot[-6] + 4]` (P1b's rule), and uses `eax` as the entry right after the call (`[eax+0xA0]`, `[eax+0xB0]`) | verified |
| G-a second frames and order | `0x846E0E`: `RouteFollow_A` per-footprint loop, after `0x82EDB0` at `0x846D1C` and a plain refcount `+1`; then footprint slot 0 and `+0xB8` unlocked. `0x85B57E`: `RouteFollow_B` after `0x82EDB0` at `0x85B507` and `World_LotLookup 0xC68990`; then its footprint loop (plain refcounts). `0x85F76D` / `0x85F7A9`: `RouteFollow_EditPath` after `0x82F0B0` at `0x85F6C6` and two `World_LotLookup` | verified |
| G-b | every other second frame (`0x75167F`, `0x655ABE`, `0x82E696`/`0x82E6C7`, portal builders): keeps the gate | verified (docs/86 §2 table) |
| What G1 changes for the callers | today's gate frame holds `+0x918` only for the `GetOrInsert` call (released in `RgLeave`), so the work after the transform already runs beside the next job; G1 removes only the wait for the call itself. The "guard for what the callers do next" of docs/86 option R was never longer than the call | verified (`routegate.cpp`) |
| Who still reaches these frames with S2 + AvoidRO on | the clones answer the transform themselves (`SrEntry` + `SrXform`), so only the game's own path does: RO fallbacks, and calls where no job ran at the RO decision but one started in the gap after the game's own release | inferred (I-152-G1-5; `g0Calls` sizes it) |
| Owner test (I-152-3 for G1) | `RgEnter` reaches G1 only for an outermost entry by a thread that does not own `+0x918` (`rgOwnerTid`); G1 also refuses a thread other than the jobs' owner and a callback served for the job (`g_inInvoke`, the borrow case) | verified source |

### G1.2 Design as built

In `RgEnter` (enforce path, after `g_rgOffSimOpen` is published), site 0 only: second frame G-a, a job running, `g0Mode` on, `steerMode` and `avMode` both on (and ready), not nested in another QL hold (`t_qlDepth`, `t_qlShared`, SH's `t_slHold`), `g_steerBroken` clear, `TryAcquireSRWLockShared(&g_ql)` succeeds, and `LtFind` (the P5b / SH hit check) finds the key: the frame keeps QL shared and skips the `+0x918` wait; `GetOrInsert` runs the game's own hit path. The leave (`RgLeaveThunk` now hands `eax` to `RgLeave`) compares the predicted entry with the returned one (`g0Checks`, `g0Mismatch`: a difference means an insert ran) and releases QL; a frame dropped by an unwind releases QL in `RgUndo`. Every other case (G-b, miss, active lot, odd table, pieces off, QL busy, not the owner, callback) waits as today and counts in `g0Fb[]` / `g0Other`. Shadow (no job): the same prediction under a short QL try, compared at the leave, behaviour unchanged. The QL writer subset W4/W5/W6g is installed (`InstallQlSubset`), so the worker's inserts and rehashes take QL exclusive.

### G1.3 Offline proof and what is left

| Check | Result |
|---|---|
| `build.cmd` (`/W4 /WX`) + selftest | pass; `TestGate0Ro`: off waits; each G-a frame served in < 5 ms while a worker thread holds the real `+0x918` CRITICAL_SECTION, QL shared kept to the leave (another thread cannot take it exclusive), count of `+0x918` untouched; eax mismatch counted; unwind and full frame stack release QL; G-b / miss / active lot / odd table / pieces off / QL busy (a W5 holder) / not owner each wait for the job (>= 8 ms) with the right counter; callback borrows as before; shadow compares only; threaded vs a writer that rehashes the fake table under QL exclusive and poisons the old bucket array: 3,593 served, 336 QL busy, 2,002 rehashes, 0 mismatches, no fault |
| `steerro-test` seeds 1-3 x 3000 | ok (no change in the RO clones; the build passes the shared sections) |
| `node tools/hook-sites.ts --check` | 552 addresses, the same 7 not in docs/68 as master (docs/68 row added; G1 adds no patch site) |
| `node tools/check-docs-code.ts`, `queue.ts --self-check`, `--dry-run` | ok |

Not covered offline: the game's own `0x62EAB0` under a live worker, the live G-a share, and the gain. Queue `tools/harness/queues/bundle-b0.json`: `bp-gate0ro-verify` (day, 5 min, B0 on, `--ab gate0ro`; pass `g0Mismatch` 0, `g0Served` > 0 on, `g0Fb_0` 0), then the night pairs `bp-g0b0-play-*`, `bp-g0b0-crowd-*`, `ip-g0b0-play-*` (B0 + `gate0ro` as one switch; compare with the B0 pairs). Expected [inferred]: small. In `bp-p1b` only 12 of 1,055 gate-0 waits had a G-a second frame (docs/86 §10); G1 matters only if B0 moves the wait onto these frames (§4.3 reading of B0). `g0Calls` vs `g0Other` in the verify job answers I-152-1 for the B0 setup.
