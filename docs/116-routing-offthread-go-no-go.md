# 116. Routing off-thread: go/no-go consolidation (draft)

Written 2026-10-18. Static consolidation only: no game run, no probe, no code. It pulls together the routing I/O study (docs/91-115) and the docs/51 ledger sections §24-§68 at commit `47c7603` (P3 world images built, P4 design and the §11 footprint design merged). The question is whether to keep building the off-thread route search of docs/94 (snapshot H, option G: the game's own search code on a worker-owned clone).

**Update 2026-10-18 (late):** synced with docs/94 §13-§19, the P1 and P3 live runs, the pass-2 main job and docs/111. Research phase is done; P1, P1b and P3 world images are built, P3-B (design B lot side) and P4 part A are building. Changes: P1 live G1 numbers fixed design B (§3); P3 live audit result with two fixes in progress (§3); pass-2 main job join (§3); non-routing reads D1-D6 and I-94-66 closed statically (docs/94 §17, §1.2, §4); door answers via the host-call proxy and the reduced mod guard (docs/94 §19) move the native evaluator out of P5 (§2 J, §4). Hours follow the CLAUDE.md rule (Claude with Rodney; clean nights listed apart).

**Update 2026-10-18 (night):** synced with docs/94 §15, §15.1, §16 (including §16.6), §17.8, §19 and §20, docs/99 §12.7 and §12.8, docs/113 §7 and §8, docs/108 §7, and the test runs of the same day. docs/94 has no §18 (its numbering goes from §17 to §19). Main changes: pass 16 gives every node a real height, so the compare keeps y and the clone run reads two more non-routing sources (N13, N14); watch-stalk U1 and U10 are answered for BP and IP sim-thread plans (no redirect needed); the design B replay clone is built (0.28.0), but docs/94 §16.6 refutes its pool locking, so private pools are now required; the box-footprint cache layout explains the 1.7% clone-copy faults; P5 door answers and the P6 release checklist are drafted; first results of the world-data copy and the lot copy are in (§3). Confidence figures were reset (§0). Hours are Claude with Rodney (CLAUDE.md).

**Update 2026-10-19 (G2):** probe pass 3 ran and PASSed; **G2 passes** (docs/109 §8.9): only routing classes run on the route worker at the four shared-helper sites, `0x8CE574` is lazy-build-only code, no new global. It found one heap write the clone does not cover yet, live portal reference counts (docs/101 §2.3 H20); its fix, P4-H20a-d (~2.5-3 h), joins P4 (§2 B, §4).

Tags as in AGENTS.md: **[verified]** = disassembly, decompile, emulator/offline harness on the exe's own bytes, or a run file; **[inferred]** = reasoning, with its ledger row. "Live" means a seeded 10-min harness job on BP (Bridgeport) or IP (Isla Paradiso), env-on settings. Effort is in hours of Claude working with Rodney (CLAUDE.md); clean nights run unattended and are listed apart.

## 0. Verdict in short

**Snapshot cost goal (Rodney, 2026-10-20):** stage 1 = cut cost + sim cost (copies + rebases) <= 5 ms/s combined per world, a first target well inside the measured gain: `hitch-attr` BP (0.34.10, env-on, 2026-10-19) puts sim waits on `+0x918` at 68.0 ms/s (all route-job waits 80.0 ms/s) and 607 of 1,594 hitches >= 50 ms would drop below 50 ms without them; IP (2026-10-20) 73.1 ms/s on `+0x918` (78.5 all), 354 of 1,106 >= 50 ms hitches; top key both worlds: steering obstacle query `0x85FE10`. Hit stage 1 first; only then, if time allows, aim for the original G3/P3-B budgets (cut <= 2, sim <= 1 ms/s). The report scripts still print FAIL against 2/1. Replay holds on `+0x918` are tracked separately (hitch risk). 0.35.13: BP 17.5 + 5.8, IP 23.9 + 11.9 ms/s.

| Question | Answer | Confidence |
|---|---|---|
| Q1. Do we know every input and output of the route search? | **Yes.** Fourteen input families, the output contract and every global the search writes are mapped, statically and live. The live write set (U1) is answered for BP and IP sim-thread plans: only the A\* stamp and the shared empty hash bucket `0x114C900` (zero over zero, no redirect); U3 never ran. Pass 16 gives every node a real height (docs/99 §12.8), so the "y = -100" assumption is gone and two more non-routing reads (N13 foundation, N14 water) are listed. Still open: U1 on the route worker (the first worker run hit a stale build, §3), proof that no change escapes the change signals (P1b attribution, G3 rerun) and 85 new indirect pairs to classify. | **About 97%** understanding |
| Q2. Can option G give identical answers off the sim thread? | **Probably yes.** G runs the game's own bytes on a game-layout clone, so the answer is identical by construction when (a) the clone equals live routing data at the cut, (b) every per-search global is redirected, (c) door answers equal the live ones and (d) insertion order is kept. (b) is verified statically and U1 live; (a) holds offline for the world side and the design B replay clone (4 seeds, 4,123 lot audits, 0 mismatches) and the first live world-data copy matched in 137,610 checks, but the replay clone's pool locking was refuted and needs private pools (§2 K); (c) for `Mode=2` is decided (host-call proxy), not built; (d) is a design property. No shadow compare has run. | **About 83-85%** identical answers |
| Q3. Can it be built? | **Yes.** P1, P1b, P3 world images and the P3-B replay clone are built and selftested. P4 is designed down to the patch bytes, P5 door answers and the P6 release queue are drafted. The parts left are bounded: P3 fixes and private pools (~5-8 h), P4 (~12-18 h), P5 (~3.5-5 h), P6 (~6-8 h), about 28-40 h in all. The lot copy did not build live (unhandled footprint type, fixed in a branch) and the copy step cost 5.5 ms/s against a 2 ms/s limit, so P3 still has work. | **About 93%** can be built |
| Q4. Will it reduce stutter? | **Unknown until G5.** A10 already halves sim route time (113 to 55 ms/s BP) and gave no gain in the IP in-load A/B; the snapshot adds a recorder and a cut on the sim and only removes the `+0x918` contention. A `Mode=2` door ask can also add ticks of latency per plan (docs/94 §19.1). | **About 50-60%** stutter gain |

**Recommendation: GO to the next gates (private pools and the P3 fixes with a G3 rerun, P1b count run, P4 shadow). No decision on P5 (answers taken) before G4 passes.** The first hard stop is G3 (the rerun after the fixes below); the second is G4 (shadow compare). Neither has a passing result yet. The cheapest early read on the 50-60% is a P4 shadow run, which already shows worker time and recorder cost.

## 1. Verdicts with evidence

### 1.1 Q1: inputs and outputs of the search

**Boundary [verified, docs/93 §1]:** the search proper is `Route_PlanLocked 0x82C6E0` (takes `+0x918`, applies pending updates, runs `RoutingJob_Execute 0x81B2A0`). Goal building, start points and result mapping run before or after it on the IC side.

**Inputs [verified unless marked]:**

| Family | Where mapped | Live evidence |
|---|---|---|
| World waypoint graph R (`world+0x54`), its record layout, cost modes | docs/91 §2-§3.1, docs/99 §1-§3, §11 (F-3, F-4, F-13 run on the exe's bytes under `tools/emu`) | docs/108 §7 F-2: in-memory R checksums equal the `.world` decoder on BP and IP; only the blocked bit (bit 7) changes in play; type bits are rewritten once by the load's G build (docs/112 §1) |
| Water/cell grid G, high-cost maps, `blockedNodeMap`, passability maps | docs/100 §1-§3, §12 (emulator) | docs/108 §7 L1/L3/L10: G built once at load on the sim thread (688 / 505 ms), no rebuild in play, sea height fixed |
| Lot table, lot graphs, per-level quadtrees, footprints, portals, lock keys | docs/91 §2, docs/92, docs/98 (in-room A\* spec), docs/101 §2.3, §6 | docs/94 §11.1 (P1 counters): writer rates per leaf, sources per write |
| Footprint grid `P+0xDC` | docs/107 §10, docs/94 §8.2 | P1: grid add 5.8 / 13 per s; grid remove one per remove-all |
| Terrain blend layer 0 (`0xC6BAB0` routability) | docs/107 §11, docs/94 §9.4 | decoded offline for BP, IP, SV (docs/108 §7); live call shape pending (I-114-7) |
| Tuning | docs/99 §2.1 | docs/108 §7 F-1: the compiled defaults of `0x673C70`; Sims3.ini does not replace them [verified live] |
| Door permission (World event 59, per Sim, managed) | docs/92 §3-§5, docs/103, docs/105, docs/115 | docs/108 §7: 4.30 / 8.78 event-59 calls per plan, answers 1 and 2 (BP), 3 also seen (IP) |
| Route parameters (options object, AGS word, ignore/forbidden/preferred lists, destinations) | docs/92 §6, docs/107 §1, docs/109 §3, docs/112 §4.1 | docs/108 §7 U6/F-11: option bits as mapped; world A\* context bits 1/2/4/8 never set (0 of 16,325 / 3,292) |
| Room partitions, floor heights (non-routing reads) | docs/101 §7 | docs/108 §7 U8: 0 partition writes in play |

**Outputs [verified, docs/93 §2-§8, docs/99 §12]:** the 0x48-byte result struct (`RoutePlanResult`), the path list at `route+0xE0` with per-path status, nodes (xz and a **real y**: the -100 the builders store is a placeholder, and pass 16 `0x64C7C0` with its helper `0x64C580` overwrites it on every plan, level-aware through `World_GetHeightAt 0xC6A250`, so 20,344 of 20,344 searched paths and 14,755 of 14,755 direct ones had real y live, docs/99 §12.8; lengths `+0x20`/`+0x24`/`+0x8C`, flags `+0x2C`, with pass 16 adding flags 0x01 "height sampled" and 0x02 "inserted"), blocker records, and the `O+0x58` id list (an output, docs/107 §9). Status codes produced: {1, 2, 3, 4, 10, 16, 18} (docs/101 §4.2, static store scan); the live histogram is pending (I-112-13, value probe 0.24.1). The contract a caller can notice is the status, chosen start and goal, and the subpath signature (docs/93 §8).

**Side effects and globals [verified static, docs/101 §2]:** 26 items, 59 `.data` addresses; only the A\* stamp `0x11580C4` and the clip scratch `0x1158580` are per-search scratch shared with the sim's planner, plus the routing-world root `0x11D9128`. A raw 4-byte scan of the whole image finds no other reference to those three addresses (I-94-60, docs/94 §12.2.2). Required side effects: pending-update apply and the event-59 callback (docs/93 §5).

**Indirect calls and managed exits:**

- Static: 1,819 indirect sites mapped (docs/102); no managed exit except the two script-host slots in `World_FireEvent` (`0x78F2FC`, `0x78F363`) [verified static].
- Live pass 1 (docs/106 §1, §3) [verified run files]: 145 resolved sites fired with 253 pairs, **0 contradict the static targets**; 82 truly new pairs at 60 partly resolved sites, all explained by five static mistakes (footprint interface subobject at `+4`, wrong family assignments, a heuristic object), none managed, none a new writer.
- Live pass-2 repro jobs (build, upstairs, door; `probe-out/queue/snap-probe2-2026-10-18-*`, joined with `tools/indirect-live-vs-static.ts`): 1,085 sites / 1,646 pairs; 319 match, 316 resolves, 282 family, 89 new, 635 no-row; **0 contradictions** [verified by the join lane; this doc did not re-run the join].
- Live pass-2 **main job** joined (same tool): 1,059 sites / 1,543 pairs; 313 match, 281 resolves, 279 family, 85 new, 580 no-row; **0 contradictions** [verified by the join lane]. The no-row pairs are mostly at pass-2 sites the docs/102 CSV does not carry, many in the Mono host closures [inferred from docs/109 §5.2]. The 85 new pairs still need the docs/106 §4 classification (gap register, group B).

**What keeps Q1 below "high" for completeness:**

1. The **change signals** are complete only on paper. P1 found 1.7% (BP) / 0.7% (IP) of sim writer rows unattributed; the callers are mapped statically (docs/94 §11.6: platform manager, user-tool drop, tree init, lazy terrain footprints, reader flushes, design D drain) and the P1b frames that would prove it are built (0.27.0). On the P1 BP file the P1b report reads 1.87% unattributed (top caller 0.27%, other bucket 1.15%, above the 1% bar), so checks 1-4 stay open until the daytime count run is read (I-94-54, docs/94 §15). The unlocked pool writers of docs/94 §16.6 (lazy lot builds, lot rebuilds, portal ICs, reader flushes) are new writer families to attribute.
2. The **world-level** signals (R bit 7, G, grid, `blockedNodeMap`, blend, high-cost tiles) were audited live by P3 (I-94-38): every world input had 0 mismatches except `tune` (580 BP / 483 IP). The cause is found (`MinimumDistanceForCarTravel` at `W+0x8C`, written in play) and fixed in 0.28.0 (re-read at every cut); the rerun result is in §3.
3. The **live write set** inside `Route_PlanLocked` (register-computed addresses) was checked by U1 (docs/113 §7, §8): on 24 sim-thread plans per world only the A\* stamp `0x11580C4` and the shared empty hash bucket array `0x114C900` (a zero stored over zero, 351 stores BP / 1,358 IP, whitelist as "zero-store sentinel") were seen [verified live and static]. U10 (route reader) is not refuted: the odd hits are freed-and-reused memory, not readers. U3 (clip scratch `0x66AB70`) never ran, so its thunk is low priority. **U1 on the route worker has not run**: `bp-watch-stalk-worker` crashed at about 177 s, before the stalk phase, and the evidence points to a stale ts3native 0.10.1 build (async routing without the gate); the queue now refuses a stale build (§3).

### 1.2 Q2: identical answers under option G

**Why identity is reachable [verified for the mechanism]:**

- The worker calls the game's own `Route_PlanLocked` with `ecx` = the clone's routing world (docs/94 §12.4.2). Every heap structure the search writes hangs off that root (docs/101 §2.3), so a clone in game layout makes them private.
- The shared scratch is five instruction sites (stamp `0x61E360`, `0x61E030`, dead `0x61CC73`; scratch `0x66AC37`; root getter `0x836200`), bytes checked on Steam 1.67, flags dead at the patched instructions (I-94-60, I-94-61) [verified static].
- The only managed call inside `Route_PlanLocked` is `World_FirePortalLockedQuery 0x791C00` from `0x64F4D3`; in shadow (`Mode=1`) its answers are replayed from the live run (docs/94 §12.5; row I-94-68 open live).
- Inputs that matter for ties are known: goal ties exist live (0 in 21,063 BP plans, 2 in 8,257 IP plans, docs/108 §7 F-7); the in-room open list order is specified exactly (docs/98 §8.3, offline checked in docs/98 §15); neighbour order, root-border wrap and cell tests are verified offline against the exe (docs/98 §15.1: 0 of 323,133 list entries differ from the model).
- The compare rule is exact (bit-equal y and lengths, equal status, goal, signature), with ties allowed only inside the F-7 tie set (docs/94 §12.6, §13).

**What is not shown yet:**

| Condition | State | Row |
|---|---|---|
| Clone equals live at the cut (lot side) | design B replay clone **built** (0.28.0, docs/94 §16): selftest 4 seeds x 600 writes, 487 cuts, 4,123 lot audits, 530 rebased lots, 0 mismatches; steerro-test REPLAY on the game's own `ApplyInsert`/`ApplyRemove` (seeds 1-2, 2-3, 3-4 x 3000 writes, 13,430-15,267 lot audits each, 0 mismatches) [verified offline]. Live: the lot copy did not build in today's run (unhandled footprint type, fixed in a branch, not rerun) and its copy step cost 5.5 ms/s, over the 2 ms/s limit (§3). **Pool locking refuted** (docs/94 §16.6): the replay shares the game's unlocked tree and map node pools with sim writers, and `PathPlanner_Reset` frees them, so private pools are required before the audit jobs are trusted. P4 still needs the clone provider (world copy around the clone's planner) | I-94-52, I-94-53, I-94-57, I-94-81, I-106-20 to I-106-29 |
| Clone equals live at the cut (world side) | built in 0.26.0, offline K1/K2/K5 and steerro-test SNAP pass (0 mismatches over 714 and 718 cuts) [verified offline]; live audit: only `tune` mismatched, cause fixed in 0.28.0; today's live copy of the world data matched the live data in 137,610 checks (§3). One steerro-test SNAP seed (4, step 725) fails on a `Gtiles` cell, also before the fixes (I-94-89, open) | I-94-38, I-94-39, I-94-40, I-94-89 |
| No other global written live | static census verified; **U1 live on BP and IP sim-thread plans: nothing beyond the A\* stamp and the zero-store sentinel `0x114C900`**; worker mode not run yet | I-113-2, I-101-1, I-94-10 |
| Insertion order kept in the clone | design property (docs/94 §12.8 item 2); the replay re-chains the whole footprint map at each rebase in the live order with the live bucket count, because chain order decides removal order (a K1 seed failed on a final bit before that fix) [verified offline] | I-94-53 |
| Footprint polygon caches copy without faulting | **cause found, fix designed** (docs/94 §15.1): the 647 faults in 39,132 clone-copy probes (1.7%) are probably box footprints `0xFEAE08`, whose `+0x38` holds a float, not a level array; the copy must check vtable slot `+0x70` first, copy boxes flat (0x4C bytes), and take the copy after the game's own build. Test: next probe build counts W1 hits per outer vtable [inferred, I-94-125] | I-94-72, I-94-120 to I-94-127 |
| Node heights (pass 16) agree between live and clone run | pass 16 runs on every plan inside `Route_PlanLocked`, so the clone run computes real y on the worker from live non-routing data; a terrain LOD swap in the window is M5 through D5 [inferred] | I-99-50 to I-99-55 |
| Node stamps behave across searches | assumed persistent; offline check planned | I-94-65 |
| Non-routing live reads without the lock are safe | designed (docs/94 §17.4 D1-D6): seven changing reads get the clone-window guard (`SnLive()` plus the `g_snNrIn` handshake at 8 sim-side sites), M5 class for stale answers; **N13 (foundation heights `0xB06A10`) and N14 (water heights `0xB793F0`) added by pass 16** (docs/94 §17.8): live reads, static while the sim runs, no new guard [inferred, I-99-54]; not built, shadow run decides | I-94-69, I-94-90 to I-94-98, I-99-54 |
| Door answers in `Mode=2` | decided (docs/94 §19.1): route (b), the host-call proxy keeps the sim round trip for door-asking plans (stub branch at `0x64F4D3`, tripwire exemption for event `0x3B`, 4 counters, selftest: ~1-1.5 h). The native evaluator (a) is deferred (~11-16 h + 2-4 live jobs) and is built only if, in the G5 A/B, the door wait exceeds half the `Mode=2` job time or ticks slept per plan exceed A10's by more than 1 at p95. Latency is the cost: each ask waits for the next serve point (about one tick), with 4.3 / 8.8 asks per plan. Coverage of plans with no ask is not measured | I-105-7, I-105-12 (now "evaluator only"), I-94-110 to I-94-118 |
| Mods with `Mode=2` | reduced guard: M2-1 foreign hooks in the clone closure (via `hook-sites.ts`), M2-2 asks only through the stub (3 trips turn `Mode=2` off), M2-3 log-only mod lines (~0.5 h). Mod runs (GoHere, MonoPatcher test mod, S3PP) belong to G6 (docs/94 §19.2, §20) | I-94-114, I-94-115 |
| Paths built on the clone hold no pointer into it | **closed statically** (docs/94 §17.5: blocker records are 40-byte values, nodes and subpath records hold scalars, sibling links stay in the private list) [verified, capstone on 1.67]; K5 stays as the closing test | I-94-66 |

Confidence is **about 83-85%**: there is no known reason G would differ and the clone is equal to live in every offline check and in the first live world-data check, but nothing has compared a single answer yet. The two things that moved it down in this sync are the refuted pool locking (a race there would show up as a rare crash, not as an audit mismatch, docs/94 §16.6) and the box-footprint copy faults; the two that moved it up are U1 live and the replay clone checks.

### 1.3 Q3: can it be built

- **P1** built and run (ts3native 0.25.0, counters v46, docs/94 §10) [verified run files, docs/94 §11.1].
- **P1b** built (0.27.0, counters v48, docs/94 §15): six attribution frames, design B record rates (BP file: ~505 records/s, rebases 0.12/s), rebase copy timing; the count run (G1') is next. The clone-equivalent copy faulted 1.7% of the time; docs/94 §15.1 traces it to box footprints and gives the copy recipe (7 steps).
- **P3 world side** built (0.26.0, counters v47, docs/94 §14, ~3 h of session time) with selftest `TestSnapImages`, steerro-test SNAP and `hook-sites --check` passing [verified offline]; live audit ran, see §3. Two P3 fixes (`tune` re-read at every cut; high-cost key map with tombstones and 32-cell tiles, docs/94 §16.2) are in 0.28.0; the steerro-test SNAP seed 4 failure (I-94-89) is open.
- **P3-B** (design B replay clone and lot audit) **built** (0.28.0, counters v49, docs/94 §16, ~6 h of session time): clone of the lot planner with its own footprint map, recorder ring (65,536 records), cut with rebase by signature compare, consumer thread, lot hash audit, load-time lot prebuild (`Prebuild=1`). Offline checks pass (§1.2). **Pool locking refuted** (docs/94 §16.6): private map pool (~0.5 h), four tree-pool redirects (~2-3 h), epoch check (~0.5 h) are required before the audit jobs are trusted; P4's A\* and cell pools need six more redirects (~2 h).
- **P4 G redirect** designed to the byte level (docs/94 §12), shadow record format fixed (docs/94 §13), non-routing read guards added (docs/94 §17, D1-D6, plus N13/N14 of §17.8); P4 part A is building. It still needs a clone provider (1.5-2.5 h, docs/94 §16.5).
- **Door mirror** managed side built (ts3fixes `doormirror`, off, docs/105 §10.5); native channel built in 0.26.0 (docs/94 §14); `Mode=2` takes door answers through the host-call proxy (docs/94 §19.1); the native evaluator is deferred.
- **Mod guard** reduced to M2-1 to M2-3 for `Mode=2` (docs/94 §19.2); the full docs/104 guard stays with the deferred evaluator; the event-59 handler cache `invcache` designed and built separately (docs/115).
- **P6 release queue** drafted, not run: `queues/snap-release.json`, 20 jobs (3 save matrices, 6 travel cases, a 45-minute soak, 2 sweeps, 3 repros, 3 mod jobs, 2 G5 A/B jobs), ~330 min unattended; placeholders for `RouteSnap.Mode=2` and `Kinds` names; the A/B jobs are clean night only (docs/94 §20).

The engineering risk is not "can we write it" but the number of shared hook sites (docs/94 §12.10 #8), the pass-through rule across seven stub modules (§12.4.3) and, since docs/94 §16.6, the allocator pools the game shares between unlocked writers. Gain (G5) is the open question, because A10 already halves sim route time (113 to 55 ms/s BP, clean night 2026-10-17, PLAN.md) and gave none in the IP in-load A/B.

## 2. Gap register

Every docs/51 row from §24-§68 that is still open and bears on the replacement, grouped by what will close it. "P4" = blocks the shadow (P4 runs give no trustworthy answer without it); "P5" = must close before answers are taken. Rows in the same family are merged where they share a closer.

### A. P3 audit (`snap-p3.json`, `bp-snap-p3`, `ip-snap-p3`): ran; 0 mismatches except `tune` (580 / 483), IP cut cost 4.28 ms/s and a full key map (512); `tune` and key map fixed in 0.28.0; today's world-data copy matched in 137,610 checks, but the copy step cost 5.5 ms/s (limit 2 ms/s)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-94-38, I-94-17, I-94-19, I-94-23 | World-level change signals complete: R only via `0x82B590`/`0x82A190` and G builds, grid only via `0x636BF0`/`0x62D9F0`, every writer on the sim or under `+0x918` | P3 | yes | yes |
| I-94-39, I-94-26, I-94-20 | High-cost dirty tiles = vertex box of `0x6733C0` arg 5 plus one 4 m cell | P3 | yes | yes |
| I-94-40 | World copies need no lock other than `+0x918` | P3 | yes | yes |
| I-94-37, I-94-5 | Cut cost (tens of µs empty; ≤ 2 ms/s BP) and memory ≤ 64 MB. **Live: IP cut cost 4.28 ms/s (first P3 run) and 5.5 ms/s for the copy step (today's run), both over the 2 ms/s budget; the key map filled at 512 entries (fixed: 16,384 slots, erase on `0x670510`, 32-cell tiles; the cost is still over, so the cut needs another cheaper step)** | P3 | no | yes |
| (audit result) | `tune` image: 580 (BP) / 483 (IP) mismatches, cause `W+0x8C` = `MinimumDistanceForCarTravel` written in play by `0x82A590`, fixed by a 72-byte re-read at every cut (0.28.0); every other world input 0. Today's world-data copy: 137,610 checks, all equal to live | P3 | yes | yes |
| I-94-89 | steerro-test SNAP K1 seed 4 step 725: one `Gtiles` cell cleared outside its key's box; fails with the 0.28.0 changes reverted too, seeds 1-2 pass | P3 | yes | yes |
| I-94-41 | RouteOption bits 63 + 58 never set by the game (door channel cannot collide) | P3 | no | yes |
| I-94-6 | Terrain heights static in live mode | P3 | yes | yes |
| I-94-22, I-94-28 | Blend layer 0 changes only by reload (needs `snap-buildbuy-edit`, `snap-bulldoze`: repros not in the queue) | P3 + edit repros | no | yes |
| I-94-1 (world part) | Every write goes through the hooked leaves (S1 class) | P3 | yes | yes |

### B. Probe pass 2 main job (`snap-probe2.json`, `bp-snap-probe2`): ran, join done (0 contradictions, 85 new pairs left to classify)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-94-12, I-94-14, I-102-3, I-102-4, I-102-5, I-102-6, I-107-25 | Indirect sites reach only mapped targets; per-search `QuadTreeCellAStar` callbacks small and known; family/lifetime sites as classified | P2 | no | yes |
| I-94-13 | No routing-closure site calls outside TS3W.exe (mods, EP DLLs, Mono code) | P2 | no | yes |
| I-94-15, I-94-16, I-109-7 | Sampler cost and arming after start-up hooks (pass 2: 1,534 sites) | P2 (tooling) | no | no |
| — (pass-2 joins) | The 89 + 85 new pairs of the repro and main-job joins: classify as docs/106 §4 (managed? writer?). **Done 2026-10-19** (docs/109 §8): 490 CSV rows, 0 new / 0 no-row left; G2 still open on I-109-21 and I-109-22 | P2 | no | yes |
| I-109-26, I-109-28 | IP pass 2 (`ip-snap-probe2`, main-job settings in Baseline-IP-Spring). **Closed 2026-10-19** (docs/109 §8.7): run PASS (0 faults, 0 skipped); 48 pairs new to the job, 30 inside (all already mapped, read only or own refcount / cursor), 18 outside, incl. all 9 new pairs (object events behind the dead job release path, object creation, route following, §8.4 shared helpers); 6 rows extended, ten-log join 0 new / 0 no-row; §8.6 verdict holds for IP | P2 | no | yes |
| I-109-21, I-109-31 | G2: the class-I static `0x11D9020..60` as docs/101 §2.2 #27. **Closed 2026-10-19** (docs/109 §8.8.1): row #27 added; `P4Cold` refuses a clone run while guard bit 0 of `0x11D9060` is clear (the guard is set before the stores, so a cold first call on the worker could race the sim) | P2 | no | yes |
| I-109-22, I-109-29, I-109-30, I-109-32 | G2 left: the worker-only pass for the 4 shared-helper sites `0x8A65EB`, `0x5BF1FB`, `0x8CE574`, `0x9158AB`. **Built 2026-10-19, not run** (docs/109 §8.8.2): `[ProbeSnap] Threads=1` (hits by thread class: job/worker/sim/other), 4-site table `vcallsites3.h`, job `bp-snap-probe3-worker` in `queues/snap-probe3.json`. The job release sites `0x59A688`/`0x59A603`/`0x59974F` left the list (dead for a route job, I-109-29). **Closed 2026-10-19** (docs/109 §8.9, I-109-33 to I-109-39): the run PASSed; every `job` hit is a routing class (portal Release `0x64E8F0` at `0x5BF1FB`/`0x8A65EB`, footprint deleting dtors at `0x9158AB`); no non-routing target ran on the worker; `0x8CE574` (no `job` hit) is lazy-build-only code (static); `other` = main (render) thread, Swarm/UI classes [inferred]. **G2 PASS.** New heap write H20 (live portal counts) moves to P4 (next row) | P2 | no | yes |
| I-109-36, I-109-37 | P4 design item from pass 3: in a clone run the worker AddRefs/Releases **live** portals (plain `+0x70` updates, no live `+0x918`) and reads live room buffers; a game grow of a clone vector would EA-free an `RpAlloc` block. Items P4-H20a-d (docs/109 §8.9): interlocked portal count pair while P4 is armed, the clone owns one reference per portal it holds (copy room `+4`/`+0x14`), cut guard on `+0x54`/`+0x64` and D4 abort before the build, portal IC family in D5. **Built 2026-10-19 in 0.30.0 (counters v54), offline checks pass** (docs/94 §21.8; `+0x64` holds 8-byte ids, verified); live check rides on the `snap-p4` shadow jobs. **The three open H20 risks are settled statically (docs/94 §21.8.1, docs/51 §97):** inline `+0x70` writes none [verified]; the variation vector `+0x5C` cannot be regrown under a clone-held portal outside D2 (accept, fault counter plus a count-only tripwire, ~0.5 h) [paths verified]; the lazy stop always lands in `P4Guarded`, leaks bounded (~0.1 h `XcptLog` fix) [frames verified] | P4 | yes (built, not run; ~0.6 h of fixes) | yes |

### C. Watch-stalk U1 (`watch-stalk.json`, docs/113): sim-thread runs done on BP and IP; worker runs not run (stale-build crash, fixed in the queue)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-113-2, I-101-1, I-94-10 | Addresses written inside `Route_PlanLocked` = the docs/101 §2.2 items, nothing register-computed. **Answered for sim-thread plans (BP and IP, 24 plans each): only the A\* stamp and the zero-store sentinel `0x114C900`; harmless, no redirect.** Open: the same on the route worker (`bp-watch-stalk-worker`, `ip-watch-stalk-worker`), needs `out/` rebuilt from 0.27.0 or later first | P2 (G2) | **yes** for the worker read (docs/94 §12.10 #1) | yes |
| I-113-3 | Clip scratch user `0x66AB70` runs in plans only through H13: **not seen** in 24 plans per world (0 blocks, 0 `0x66AC37` loads); low priority, thunk stays because H13 can still reach it | P2 | no | no |
| I-113-1 | `route+0x1F4` has no reader: **not refuted** (the odd hits are freed-and-reused memory; the probe's `vtOk` is not a liveness test, so a later probe ends a watch at route release via `0x75D640`) | P2 | no | no |

### D. F-4b (`f4b-inlot-route`, `f4b-street-route`, docs/99 §12.7, §12.8): ran, outcome D (every searched path has real y)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-99-33, I-99-39 | **Answered:** pass 16 (`0x64C7C0`, helper `0x64C580`, store at `0x64C620`) sets every node's y level-aware; -100 never reaches CopyNodes; followed paths are near-flat (length ratio 1.00), so no look-ahead defect. The compare keeps y and `+0x20` | P4 (compare rule) | no (G copies whatever the game does) | no |
| I-99-37, I-99-41, I-99-42 | Follower prepends the agent position (prepend y within 0.01 m of node 1); agent y is the lot floor, 3.32 m above bare terrain in the in-lot run, so the probe's flag-0 height read was the wrong comparison, not a defect | after search | no | no |
| I-99-50 to I-99-55 | Pass 16 reads non-routing data on whichever thread plans: N1, N2, N4, N5, N9 plus N13 foundation and N14 water heights (docs/94 §17.8); insert condition of its step 2 not read (`0x800` entry cap), `0x64C580` not run in the emulator | P4 | yes (live reads, M5 class) | no |
| I-99-60 to I-99-62 | `bp-f4b-street` crashed 3 s after its data was complete, at the probe unload, 10 ms after the stutter profiler closed its file. Cause not reproduced (inferred: two Frida agents unloading together). Unload guard added to the probes; side-session unload now staggered 5 s after the profiler (harness commit `406d23d`) | harness | no | no |

Under G the first two rows are informational; they matter for option R (our own search) and for the docs/93 "equivalent" metric. docs/99 §12.5's "inner nodes stay at -100" is replaced: a replacement search would have to reproduce pass 16.

### E. Live job queued: live reads 2 (`live-reads2`, docs/114)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-114-1, I-114-2, I-114-3, I-107-15, I-107-17 | `O+0x58` filled after `Route_PlanCommon`, holds start/goal footprints; `route+0x158+0x58` empty | P4 compare | no | yes (output contract) |
| I-114-4, I-107-19 | Footprint grid size formula and fill | P3 | no | no |
| I-114-5, I-107-20 | Grid writers on the sim thread only (async off; worker case needs `Mode=1`) | P3 | yes | yes |
| I-114-7, I-107-21, I-107-11, I-107-27 | `0xC6BAB0` call shape and layer meaning per world | P3 | no | no |
| I-114-8, I-107-24, I-107-22, I-107-12 | Follow slot `+0x38` caller, steering path reads (outside the search) | none (steering) | no | no |
| I-114-9, I-100-13 | Houseboat Sims plan in the lot slot frame | P4 (supported set) | no (refused as `unsupported`) | no |

### F. In-room live reads (docs/111): ran, results filled (BP + IP PASS: I-98-15 0 walkable wrap pairs, I-101-4 verified, U7 `X+0x4C` = 0; I-98-21 closed statically)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-111-1/I-98-15, I-111-2/I-98-21, I-111-3/I-101-4, I-111-4, I-111-5 | Root-border wrap never yields in real trees; lot tree bounds; static `0x11CF02C` per world; in-room context bits; `X+0x4C` = 0 | P4 | no (G runs the same code) | no |

Under G these only confirm the docs/98 spec; they block option R, not G.

### G. Live job queued: value probe 0.24.1 (`value-probe.json`, counters v45)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-112-13, I-108-14 | Live status histogram in {1, 2, 3, 4, 10, 16, 18} | P4 | no | yes |
| I-112-14, I-112-15, I-108-2, I-108-3, I-94-21 | R fixed after the post-G-build baseline; blocked-bit restore at lot first use | P3 | yes | yes |
| I-112-9, I-108-19, I-105-3 | Line-of-sight raises inside goal building only (goal building stays on the sim) | P5 | no | yes |
| I-112-11, I-112-4 | Polygon level builds are mostly the valid fast path; blocked 0 -> 284 is lazy first use | P3 | no | no |

Rows I-108-1 to I-108-20 are answered for 0.24.0 in docs/108 §7 except U5 (status), R writers and the blocked-bit hash; the ledger verdict column still says "open" (ledger lag, §2 J).

### H. P1b (built, 0.27.0; the daytime count run `snap-p1b.json` is the open step)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-94-54 | Unattributed P1 hits come from the mapped families (G1' check 1) | P1b | **yes** (design B correctness) | yes |
| I-94-56 | Footprint map size per world; deep lot copy 0.25-1.5 ms (rebase cost, check 4). P1b report on the P1 BP file: unattributed 1.87% (bar 1%), ~505 records/s, rebases 0.12/s, checks 1-4 open | P1b | yes | yes |
| I-94-72, I-94-125, I-94-127 | Box footprints (vtable slot `+0x70` = empty `ret`) read as level arrays: the likely cause of the 647 clone-copy faults in 39,132 (1.7%). Next probe build checks slot `+0x70` first and counts W1 hits per outer vtable; copy time so far is a lower bound (no build, no allocation) | P1b / P3-B | yes | yes |
| I-94-73, I-94-74 | Record rate and worst bucket are upper bounds (W4 nested leaf hits not suppressed); the copy covers level 0 only | P1b | no | no |
| I-94-50 | Transit lots get their lot legs only after the world A\* | P1b | no | yes |
| I-94-57 (records/s buckets) | Record rate per 1-s bucket (ring size, check 2) | P1b | yes | yes |
| I-94-33, I-94-34, I-94-36 | Lot argument positions; no object chain open at world event 7; START windows | P1/P1b | yes | yes |
| I-94-32, I-94-35 | IC slot swap takes effect; object-event chains rarely stopped (I-94-58 supports it for BP) | P1 | no | yes |

### I. P4 shadow (`bp-snap-shadow`, `ip-snap-shadow`) and offline K1/K3/K4

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-94-52, I-94-53, I-94-4 | Replaying the game's apply leaves in order on the clone equals live (history-dependent level trees) | P3 (design B) | **yes** | yes |
| I-94-55, I-94-1 (lot part) | W6 hits outside a writer frame are only cache fills; no write bypasses the recorded leaves | P3 audit (design B) | **yes** | yes |
| I-94-62, I-94-63, I-94-64, I-94-65, I-94-67 | Thread selector, per-worker scratch, cold statics, stamp persistence, private path list | P4 selftest/offline | **yes** | yes |
| I-94-68 | Only managed exit in `Route_PlanLocked` is `0x791C00` | P4 shadow | yes | yes |
| I-94-69, I-99-54 | Non-routing live reads without `+0x918` are not torn; design D1-D6 (docs/94 §17.4) in P4 part A, shadow + `snap-buildbuy-edit` decide. Pass 16 adds N13 (foundation) and N14 (water) heights, both static outside build mode [not measured] | P4 shadow + `snap-buildbuy-edit` | yes | **yes** |
| I-94-66 | Clone-built paths hold no pointer into the working copy: **closed statically** (docs/94 §17.5); K5 offline stays as the closing test (2026-10-19: K5 passes, docs/94 §13.3) | P4 offline | no | no |
| I-94-193 | The replay clone's inner tree nodes keep `+8` (parent) on the live parent; the game's neighbour climb `0x66A220` follows it, so a clone run reads the live tree: **found by K3 2026-10-19** [verified offline], fix = `RpTree` sets the clone parent (docs/94 §13.3) | P4 offline, before any shadow run | **yes** | yes |
| I-94-190, I-94-191, I-94-192 | Under G only the goal draw is random (per-plan RDTSC seed); no other RNG draw; the pointer-ordered start set never has two nodes (docs/94 §13.1) | P4 shadow (`tie` field, path-tie count) | yes | yes |
| I-93-10, I-93-3 | Equivalence metric; truncated multi-subpath plans reported as Succeeded | P4 | no (G compares exactly) | yes |
| I-92-5, I-92-8, I-101-7, I-101-8 | Event-59 answer inputs; search reads standing Sims through footprints only; doors asked twice per plan; portal-lock flag readers | P4 | no | yes |
| I-99-10, I-99-24, I-99-17, I-98-6, I-101-3, I-101-5, I-109-3 | Node fields that reach the route; y = -100; opts fields; root mirror; quadtree count; 5 patch sites; stamp reader | P4 | no (covered by the exact compare) | no |
| I-99-30 | Live subpath types follow the static table | P4 | no | no |
| I-94-9 | Event 59 asked only for start/goal lot portals | P4 | no | yes (door table scope) |

### J. Nothing scheduled

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-105-7, I-105-12 | Native door evaluator equals `Lot.OnPortalPathPlan` | deferred (docs/94 §19: P5 uses the host-call proxy) | no | **only for the deferred native evaluator** (was "yes") |
| I-105-14, I-105-16, I-105-17, I-105-18, I-105-19, I-105-25, I-105-30, I-105-31, I-105-32, I-105-40 to I-105-43 | Door mirror order at import/load, lazy rebuild, `Patch()` header swap, `GetObjects<CommonDoor>`, unbound proxies (repro `bp-door-lock` exists, no job queued) | deferred | no | evaluator path only |
| I-104-2 to I-104-10 | Full mod guard (MVID check, handler chain walk, MonoPatcher prologue, GoHere) | deferred; P5 keeps only M2-1 to M2-3 (docs/94 §19.2), mod runs belong to G6 | no | **only for the deferred native evaluator** (was "yes") |
| I-103-3, I-103-4, I-103-6, I-103-7 | Managed dispatch facts behind the guard | P5/P6 | no | yes (release with mods) |
| I-94-110 to I-94-118 | P5 prerequisites (docs/94 §19, ledger docs/51 §74): proxy exemption, M2-1 to M2-3, mod runs | P5 / G6 | no | yes; built in 0.33.0 (docs/94 §22; the exemption became a disarmed ask, §22.2); live proof in `snap-p5` (I-94-280 to I-94-288, docs/51 §117), mod runs stay G6 |
| I-115-3 to I-115-7 | `invcache` correctness and gain | separate fix | no | no |
| I-94-11 | With H the A10 wait growth disappears | P5 A/B | no | **yes** (the G5 question) |
| I-92-1, I-92-2, I-92-10 | Managed exits per plan; lock cache lifetime; `Route_OnRouteEvent` never entered from a plan | P4 | no | no |
| I-101-2, I-101-9, I-101-10 | One-time statics set before the first worker job; unlocked portal IC writes; partition writes sim-only | P4 | no | yes |
| I-107-3 to I-107-10, I-107-14, I-107-18, I-107-26 | End-turn list, `O+0x58` writer, MonoPatcher TypePatch, preferred portal cost, grid cell index, display level reset | P4/P6 | no | no |
| I-91-3, I-91-9, I-91-11, I-99-8 | R fixed except bit 7; lot routing size; heightmap reproduction; ini tuning (answered by docs/108 F-1) | — | no | no |
| I-106-6 | A\* free lists lock coverage: **refuted** (docs/94 §16.6). The tree, map, A\*, goal and cell pools have no lock; 72 and 67 unlocked roots reach the tree and map closures. P4's worker search needs per-job A\* and cell pools (6 sites, ~2 h) through the same thread selector | P4 | yes | yes |

### K. P3-B replay clone (0.28.0, `snap-p3b.json`, `bp-snap-p3b`, `ip-snap-p3b`; docs/94 §16): built, offline checks pass, live lot copy did not build

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-106-20 to I-106-29 | **Private pools for the clone**: map pool (set clone `M+0x1C`, no detour, ~0.5 h), tree pools (4 sites selected by an owner tid: pop `0x667920`, `0x667990`, push `0x6679D0` and `0x668962`, ~2-3 h with selftest, steerro-test REPLAY and docs/68 rows), epoch check on `[P+0x28]`, `[0x11CF42C]`, `[0x11CF430]` and counter `rpLivePool` = 0 (~0.5 h). Without it a sim write during a drain corrupts a free list (shows later as a crash) and `PathPlanner_Reset` frees the clone's nodes | P3-B | **yes** | yes |
| I-94-81, I-94-99 | Live check of the clone: `rpFaults` 0, no crash cannot prove the pools safe; a lot copy that does not build is not a pass. Unhandled footprint type found live, fix in a branch (not rerun). `Prebuild=1` removes most lazy lot builds for lots in the table, not for lots added later | P3-B | yes | yes |
| (live result) | Building all lot graphs at load took 2 ms for 36 lots; the copy step cost 5.5 ms/s against the 2 ms/s limit (P3-B pass needs footprint copies + rebases ≤ 1 ms/s of sim time, clone ≤ 128 MB) | P3-B | no | yes |
| I-94-80 to I-94-89 (I-94-87 for the active lot) | P4 clone provider (`cut`, `acquire`, `release`), private copies of lock-key sets `fp+0x20`, room vector `+0x90`, grid `+0xDC`, pending maps (K4: no clone pointer into live memory), the active-lot read of `LotTable_Contains` at the cut (I-94-87), §8.5 steps 2-4 at the async START | P4 | **yes** | yes |
| I-94-89 | See group A (SNAP seed 4) | P3 | yes | yes |

### L. P5 and P6 prerequisites (docs/94 §19, §20)

| Row(s) | Gap | Phase | P4 | P5 |
|---|---|---|---|---|
| I-94-110 to I-94-116 | Door path (b): stub branch at `0x64F4D3`, event `0x3B` exemption, counters `snM2DoorAsk/Hist/WaitUs/JobUs`, selftest; `LockedDoorsDuringPlan` repeats on an async-to-sync fallback as in A10 | P5 | no | yes |
| I-94-112 | Build rule for the evaluator (a): door wait > 0.5 of `Mode=2` job time, or ticks slept per plan at p95 more than 1 above A10 | G5 | no | no |
| I-94-114, I-94-115, I-94-117, I-94-118 | Mod runs on GoHere, a MonoPatcher test mod and S3PP; L4 whitelist for our `Lot.OnPortalPathPlan` patch (only if (a) is built); trip action under (a) is to drop to the proxy, never to turn `Mode=2` off | G6 | no | evaluator path only |
| (docs/94 §20.4) | P6 build and real key names (`RouteSnap.Mode`, `Kinds`, counter version) ~1 h; `routesnap` switch in `profile-world.ts` ~0.25 h; GoHere fixes entry and save ~1-1.5 h; MonoPatcher test mod ~1.5-2 h; verify repros `steer-crowd` and `ip-houseboat-create` ~0.5 h; rerun `travel-all` on the current build with the snapshot off (~1.5 h unattended) | P6 | no | no (G6) |

**Ledger lag (rows a later doc already closed; the verdict column was not updated):** I-91-6 (answered in the row), I-94-8 (docs/100 §1 item 6: frozen ponds change no routing structure [verified static]), I-106-3 and I-106-4 (docs/109 §2-§3: stamp reader unreachable, bit `0x20` never set [verified static]), I-109-6 (docs/112 §1: premise wrong), I-92-7 (row says wrong), I-108-1 to I-108-20 (docs/108 §7, 0.24.0). A ledger pass should move these to their verdicts.

## 3. Live results so far

| Result | Numbers | Source |
|---|---|---|
| Probe pass 1, indirect targets | 145 resolved sites / 253 pairs: 0 contradictions; 60 sites / 82 pairs new, all static mistakes; 181 sites newly resolved; managed exits only the two `World_FireEvent` slots | docs/106 §1, §3, ledger §42 [verified run files] |
| Probe pass 2, repro jobs (build, upstairs, door) | 1,085 sites / 1,646 pairs: 319 match, 316 resolves, 282 family, 89 new, 635 no-row; **0 contradictions** | `snap-probe2-2026-10-18-*` run files, `tools/indirect-live-vs-static.ts` join [reported by the join lane; write-up into docs/106/109 pending] |
| Value probe 0.24.0 | tuning = defaults; R at load = `.world` (checksums match); only bit 7 moves (BP 0 -> 284 blocked, IP 98); G built once on the sim (688 / 505 ms); ties 0 / 21,063 and 2 / 8,257; world A\* bits 1/2/4/8 never set; origin 0,0; interest points always empty; H9 rewrites 8 of 21,063 plans; partition writes 0; event 68 never inside CanSee; event 59 4.30 / 8.78 per plan | docs/108 §7, ledger §44 [verified live]; U5 read was a probe bug (docs/112 §2) |
| Value probe follow-ups | R type-bit rewrite by the load G build; blocked bit has two writers (`0x82B590`, `0x82A190`); lazy-build counts are entries, not builds; G build thread = sim | docs/112 §1, ledger §52 [verified code + log] |
| P1 change counters (G1) | writer rows 1,598 / 1,941 per s; STARTs 38.3 / 19.1 per s; dirty lots per START p50 2 / 2-3, p95 6-10; `0x8321F0` 240 / 322 per s (73% / 84% from object event `0x0E`); W6 cache fills 433 / 571 per s; discourage ICs a third / a sixth of rows; R bit 7 and lot graph builds rare (0.3, 0.12 / 0.3, 0.18 per s); unattributed 16,336 / 7,841 (1.7% / 0.7%). **Old G1 FAIL**; G1' checks 2, 5, 6 PASS, 1 OPEN, 3 and 4 need P1b | docs/94 §10, §11.1, §11.5, ledger §63, §67 [verified run files] |
| Probe pass 2, main job | 1,059 sites / 1,543 pairs: 313 match, 281 resolves, 279 family, 85 new, 580 no-row; **0 contradictions** | `bp-snap-probe2` run file, `tools/indirect-live-vs-static.ts` join [reported by the join lane] |
| Probe pass 2, IP main job | 1,049 sites / 1,559 pairs against the CSV after docs/109 §8: 313 match, 252 resolves, 981 family, 4 hint, 9 new, 0 no-row; 48 pairs new to the job, 30 inside (mapped), 18 outside (all 9 new); ten-log join after 6 row updates 0 new / 0 no-row | `ip-snap-probe2` run file, docs/109 §8.7, ledger §88 [verified static over live edges] |
| P1 verdict (G1, live) | BP ~1,600 / IP ~1,941 sim writer rows per s: per-START lot copies are ruled out, so **option B** (ordered leaf replay into a worker-owned clone, docs/94 §11.4) | docs/94 §11.1, §11.5 [verified run files] |
| P3 live audit (G3 first read) | BP/IP: all world inputs 0 mismatches except `tune` (580 BP / 483 IP); IP cut cost 4.28 ms/s (budget 2 ms/s); key map full at 512; **G3 not passed**. Causes and fixes in 0.28.0: `tune` = `W+0x8C` written in play (re-read at every cut), key map never erased (tombstones, 16,384 slots, 32-cell tiles) | `bp-snap-p3`, `ip-snap-p3`, docs/94 §14, §16.2 [reported by the run lane; causes verified static] |
| Watch-stalk U1/U10/U3, sim-thread plans | BP and IP, 357 s each, 24 plans followed per world (of 6,396 / 2,520 sim-thread plans), 0 probe errors. U1: only `0x11580C4` (A\* stamp) and `0x114C900` (shared empty hash bucket array, zero stored over zero: 351 stores BP, 1,358 IP) beyond the known set; U10: odd hits are freed-and-reused memory (`0x5B8645`, `0x62035B` BP; `0x6BB9DC` IP), no route reader; U3 not seen (0 blocks, 0 `0x66AC37` loads) | docs/113 §6-§8 [verified live + static] |
| Watch-stalk, route worker | `bp-watch-stalk-worker` crashed at about 177 s (22:36:52 PHT), before the stalk phase; empty `xcpt` file. Cause (inferred, strong): the `asi: "tested"` file was a stale ts3native 0.10.1 (async routing without the gate), as for every `tested` job since 2026-10-17, so their results that need newer features need reruns. `queue.ts` now refuses a stale build. Probe worker path passes the dummy 5 of 5 | docs/113 §8.4-§8.6, docs/51 §82 [verified logs, cause inferred] |
| F-4b (BP in-lot and street) | Outcome D: real y on 20,344 of 20,344 searched and 14,755 of 14,755 direct paths; writer = pass 16 helper `0x64C580` (store at `0x64C620`); prepends 5 / 54 and 4 / 290; agent y median 3.32 m above bare terrain in the lot, 0.00 m in the street. The street job's game crashed 3 s after its data was complete, at the probe unload (10 ms after the stutter profiler closed its file); unload guard added, side-session unload staggered | docs/99 §12.7, §12.8 [verified live + static; crash cause inferred] |
| P3 and P3-B, today's runs | World-data copy equal to live in **137,610 checks**; building all lot graphs at load **2 ms for 36 lots**; the lot copy **did not build** (unhandled footprint type, **fixed in a branch**, not rerun); copy step **5.5 ms/s**, over the 2 ms/s limit; one test run **crashed when two test tools unloaded at the same moment**, the tools now unload 5 s apart (commit `406d23d`) | today's test runs [reported; not yet written into docs/94 §14 or §16.4] |
| Clone-copy probe (P1b) | 647 faults in 39,132 clone-equivalent copies (1.7%); likely box footprints, not yet confirmed | docs/94 §15.1 [inferred, I-94-125] |
| In-room live reads | I-98-15 0 walkable wrap pairs (330 / 380 trees), I-101-4 ecx = P in 200 / 200 builds, U7 `X+0x4C` = 0; docs/111 §5 filled | docs/111 §5 [verified live] |
| Wildflower first minute (not the route search) | 12 fires, 3 slow = all 20 tries failed, 551-1,025 ms each, time in native `World_FindGoodLocation` cell tests | docs/110 §12 [verified live]; relevant only as a sim-thread world query outside `Route_PlanLocked` |

**Pending slots** (fill when the job reports; each names the doc section that receives it):

| Job | Answers | Result |
|---|---|---|
| P3 audit `bp-snap-p3`, `ip-snap-p3` (G3) | I-94-37 to I-94-41, world part of I-94-1 | first read in; today's run: 137,610 checks equal, copy step 5.5 ms/s still over → G3 not passed; write into docs/94 §14 |
| P3-B audit `bp-snap-p3b`, `ip-snap-p3b` (P3-B pass) | clone held, 0 lot mismatches in ≥ 200 lot audits, footprint copies + rebases ≤ 1 ms/s, clone ≤ 128 MB | lot copy did not build (footprint type, fix in a branch); rerun after the fix and after the private pools → docs/94 §16.4 |
| Probe pass 2 main job `bp-snap-probe2` | I-94-12 to I-94-16 on the pass-2 sites; classify the 89 + 85 new pairs | join in (0 contradictions); classified in docs/109 §8; IP main job `ip-snap-probe2` classified in docs/109 §8.7 (§8.6 verdict holds for IP) |
| Watch-stalk `bp-watch-stalk`, `ip-watch-stalk` (U1, U3, U10) | I-113-1 to I-113-3 | done (sim thread) → docs/113 §6-§8 |
| Watch-stalk worker `bp-watch-stalk-worker`, `ip-watch-stalk-worker` (U1 on the worker) | I-113-2 for P4 | crashed on a stale build; rerun after rebuilding `out/` (queue now refuses stale) → docs/113 §8.6 |
| F-4b `f4b-inlot-route`, `f4b-street-route` | I-99-33, -37, -39, -41, -42 | done, outcome D → docs/99 §12.7, §12.8 |
| Live reads 2 `bp-live-reads2`, `ip-live-reads2` | I-114-1 to I-114-9 | pending → docs/114 §6 |
| In-room reads `bp-inroom-reads`, `ip-inroom-reads` | I-111-1 to I-111-5 | done, docs/111 §5 filled |
| Value probe 0.24.1 | I-112-13 to I-112-15, U5, blocked-bit hash | pending → docs/108 §8 |
| P1b count run `snap-p1b.json` | G1' checks 1-4 | built (0.27.0), not run → docs/94 §11.5, §15 |

## 4. Build plan status (P0-P6)

Hours from docs/94 §5, §11.4, §12.9 and §14, reconciled: §11 removed the P4 materialiser (1-2 h of §12.9's 8-13 h), and the P3 world-image part is already spent.

| Phase | State | Spent | Left (hours) | Gate | Unlocks |
|---|---|---|---|---|---|
| P0 static maps | done (docs/91-115) | — | 0 | read set names every hooked structure: **pass** | P1 |
| P1 change counters | built (0.25.0), run BP + IP | ~1.5-2.5 (estimate, not timed) | 0 | old G1: **FAIL** (rates rule out per-START lot copies) → design B; G1' (§11.5) partly PASS | design B choice |
| P1b attribution and sizing | built (0.27.0, counters v48, docs/94 §15), count run not yet read | ~1-1.5 (estimate) | 0 + one daytime run | G1' all six checks | P3 lot side |
| P2 read-set / indirect targets | pass 1 done; pass-2 repro jobs and main job joined (0 contradictions); IP pass 2 and probe pass 3 (worker-only attribution) done 2026-10-19; U1 done on sim-thread plans (BP, IP), worker run to redo on a fresh build | (study) | ~0 + the U1 worker run | **G2: PASS 2026-10-19** (docs/109 §8.9): executed ⊆ closure + listed gaps; write set known (new heap row H20, portal counts, carried to P4); G chosen (static: docs/101 §3, docs/94 §12) | P4 runs (U1 first) |
| P3 snapshot | world images + audit built (0.26.0); fixes for `tune` and the key map in 0.28.0; today's world-data copy equal in 137,610 checks, copy step 5.5 ms/s (over 2 ms/s), lot graphs at load 2 ms for 36 lots. Lot side (design B replay clone, recorder ring, consumer, rebase, lot audit, prebuild) = P3-B **built** (0.28.0); live lot copy failed on an unhandled footprint type (fixed in a branch); pool locking refuted, private pools required | ~3 (world) + ~6 (P3-B) | ~5-8: private pools ~3-4, cheaper copy step and footprint-type fix merge ~1-2 [inferred], audit reruns and the SNAP seed 4 item ~1-2 | **G3:** 0 audit mismatches on BP/IP/SV, travel and Edit Town incl. R, grid and G tiles; cut ≤ 2 ms/s; ≤ 64 MB; 0 crashes | P4 |
| P4 search shadow (G) | designed (§12), record format (§13), non-routing reads D1-D6 (§17, +2-3 h; N13/N14 add no guard); part A building; also needs the clone provider (1.5-2.5 h, in part A [inferred]) and per-job A\* and cell pools (+2 h, §16.6) | (design) | **~12-18** + portal references (H20, docs/109 §8.9: built 2026-10-19 in 0.30.0, ~3.5 h, docs/94 §21.8) + two 10-min shadow jobs; G4 prep 2026-10-19 (tie rules, `tie` capture in 0.30.3, report rule, K3/K4/K5 offline, ~4.5 h, docs/94 §13.1-§13.3) | **G4 (docs/94 §13.2):** per world ≥ 10k compares; M3 = 0, M2 = 0, real M1 = 0; goal ties counted apart (tie-set records ≤ 0.5% of compares, else OPEN); path ties = 0 under G (else OPEN, read each); M4 < 5%; worker time ≤ game plan time (sum, p50, p95); 0 faults/managed/events/live-lock/dead stamp/display-level writes; K3, K4, K5 pass with no relink warning (needs the `RpTree` parent fix, I-94-193); U1 or its K3 stand-in | P5 |
| P5 answers taken (`Mode=2`, `ReplanFromPosition` first) | **built 2026-10-19 (0.33.0, counters v61, docs/94 §22), not run**: the job's `Route_PlanLocked` takes the clone run's answer (list nodes moved into the route, byte for byte the §13 `copy` answer); door asks to the game's chain through the host-call proxy, run disarmed; fallback to the game's own plan on every refusal, lazy stop, fault, tripwire, stale cut or `nr` skew; `VerifyEvery` jobs keep the game's answer and write `"m2":1` records; M2-1 site snapshot, M2-2 trips, M2-3 L2 log; runtime switch `routesnap`. **The live `+0x918` is still held through the clone run** (A10's job hold), unless P5b is on. **P5b steering slice built 2026-10-19 (0.34.0, counters v62, docs/94 §22.9), not run:** `[RouteSnap] ReleaseLock=1` lends the job's `+0x918` to the sim's steering calls (`0x82EDB0` / `0x82F0B0`) while a taken job's clone run runs. `A_RUNNING` is kept, so every destroy, save and drain still waits for the job (I-94-287 holds; docs/88 design G is not needed for this slice). The lock is taken back before the answer, each door ask and any tripwire. Offline threaded test: 93% of job time lent, steering inside a job 654 of 676 calls vs 0 of 21 held, answers equal, no use after free, no hang. **P5b gates built 2026-10-19 (0.34.2, counters v63, docs/94 §22.10), not run:** `ReleaseGates=1` also admits the sim's read-only gate leaves (gate-12 `Contains` for F-B / F-C / F-G, gate-0 `GetOrInsert` hits; ~27 ms/s BP of keyed waits); writers, gate-12 writer families, gate-0 misses and destroys still wait. Offline: gate-12 reads 40 of 46 inside a lent job (0.79 vs 6.0 ms), gate-0 hits 14-29 of 19-36, all others 0, answers equal. Expected ~5-9 ms/s x the lent share of job time [inferred]. **P5c built 2026-10-19 (0.35.0, counters v65, docs/94 §22.11), not run:** `FreeLock=1` frees `+0x918` during the clone search for every plain sim acquire (writers too); destroys in the window park one reference (docs/88 design G) and run; answers with a routing write after the cut are refused (the game's plan answers); the sim never waits for the job holding the lent lock. Offline: 82-86% of sim acquires in a job no longer wait (P5b 20-49%), no stale answer, no use after free, no hang; stale refusals 18-76% of windows in the test mix | ~3 (P5) + ~4.5 (P5b slice) | **1 clean night** (`queues/snap-p5.json`, BP + IP, 10 min) + fix rounds ~1-3 + the G5 A/B night; P5b: 1 night `queues/snap-p5b.json` + its G5 A/B night (`--ab routesnaprel`). Wider P5b admission (read-only gate leaves ~2-3 h; docs/88 design G in the window ~3-5 h + 1 night) [inferred]. P5c (~6 h built): 1 night `queues/snap-p5c.json` + its G5 A/B night (`--ab routesnapfree`). The native evaluator (a) stays deferred: ~11-16 h + 2-4 live jobs, only if G5 needs it | **G5:** keyed sim waits not above off, it/s not down > ~2% (BP and IP), 0 crashes, 0 new sim waits, 0 audit mismatches; P5 verify M2/M3 = 0. Expected about neutral until P5b [inferred] | P6 |
| P6 widen and release | checklist drafted (`snap-release.json`, 20 jobs, ~330 min unattended, not run); build not started | ~1 (checklist) | ~6-8: P6 build ~1, the missing items of docs/94 §20.4 (GoHere save, MonoPatcher test mod, repro checks, runtime switch) ~4.25-5.25, fix rounds 1-2; + 1 clean night for the queue | **G6:** release checklist with the snapshot on | release (opt-in, off by default) |

**Total left for G: ~28-40 h hands-on** (P2 ~0.5-1, P3 ~5-8, P4 ~12-18, P5 ~3.5-5, P6 ~6-8; plus ~11-16 h for the native door evaluator only if the proxy is not good enough) **and 2-3 clean nights** [inferred, from the per-phase estimates above; was ~20-31 h before this sync, which added the private pools (~3-4 h), the A\*/cell pool redirects (~2 h) and the P6 prerequisites of docs/94 §20.4 (~4-5 h)]. Option R (our own search) would add ~6-10 h and is not recommended while G's static case holds.

Cheap stop points in order: G1' (P1b built, one daytime run), G3 (after the private pools, the cheaper copy step and a rerun), K1 for design B (passed offline, rerun with randomised addresses on the private pools), G4 (after ~12-18 h of P4).

### 4.1 Built but off: sim-wait reducers outside P5 (2026-10-20)

Same-build A/Bs on 0.35.24/25 put the full copy + P5/P5b/P5c path at about neutral (-5..+1 ms/s). Sim waits on `+0x918` by key are ~70-90 ms/s on BP: steering `0x85FE10` ~22-35, gate 12 `0x62F782` ~14-18, gate 0 `0x62EF7A` ~9-14, destroy ~9, UpdateSimFootprint `0x833593` ~7-9, avoidance `0x846D21` ~5-9, gate 10 `0x760BFF` ~4-6. These reducers are in master and off by default; none has had a live A/B on the snapshot builds. `tools/harness/queues/wait-reducers-ab.json` runs each in one load (BP, 10 min, `--ab` 2-min slices, HitchAttr on) on `game-copy/ts3native.asi.snap03524` with snap-p5c-ab's settings (`InsFreeInPlace` left out: not in 0.35.24). Read each with `node tools/harness/wait-ab.ts <tag>` (keyed waits on vs off) and `hitch-attr-report.ts`. No code fix was needed for them to install on current master: no overlapping hook sites (`hook-sites.ts --check`), counter offsets in `profile-world.ts` match `selftest.cpp`'s asserts [verified statically]. Whether they work with P5c's lent lock is not known until the run [unverified].

Night of 2026-10-20/21 (`wait-reducers-ab.json`, 7/7 PASS, 0 crashes, noisy machine): read per slice in docs/136. Run-level it/s, hitches/min and p99 are inside the slot bias and noise (the first off slice is the slowest in 6/7 jobs; the no-op moved keyed waits -10.1 ms/s), so the decisions below rest on key-level shifts. The last column is now the v0.1.5 decision.

| Reducer (ini, `--ab`) | Design | Target key | State | Expected [inferred] | Decision (docs/136) |
|---|---|---|---|---|---|
| `[SteerRO]`, `steerro` | S2 read-only `0x85FD10` (docs/84 §3.6) | `0x85FE10` steering | `bp-wr-steerro` 2026-10-21 (docs/136 §3.2): key 21.5 -> 11.3, but avoidance `0x846D21` 9.9 -> 17.4 (the wait moves); keyed total -9.8 = no-op | <= ~69% of the key, ~15-24 ms/s | **needs more data**: not in v0.1.5; pair with an avoidance reducer |
| `[ObstructedRO]`, `obstructedro` | gate-0 fix, `0x751480` from the clone (docs/86 §10) | `0x62EF7A` gate 0 | `bp-wr-obstructedro` (docs/136 §3.4): gate 0 11.5 -> 7.7, the same -3.8 as the no-op locowin; with S2 (`bp-wr-steer-obst`, §3.3) S2 -8.3, gate 0 -4.7, avoidance +4.1 | cap ~11.8 ms/s, ~1 ms/s until S2 learns the slot types | **drop** from v0.1.5 (alone); the S2 pair: needs more data |
| `[LotFrontDoorRO]`, `lotfdro` | L1 front door `0x631800` (docs/84 §4.6) | `0x631814` gate 12 F-C | `bp-wr-lotfdro` (docs/136 §3.5): key hitches 0 vs 10 (key missing from the native table both sides), keyed total -6.5 = no-op, S2 +9.5 (noise, inferred) | the F-C share of gate 12 (~1.8 ms/s) | **needs more data** (low priority), not in v0.1.5 |
| `[RouteAsync] Kinds=3`, `asyncreplan` | A10 async Replan / PlanFromPoint (docs/87 §5) | `0x760BFF` gate 10 | `bp-wr-asyncreplan` (docs/136 §3.6): key 3.7 -> 2.5, keyed total **+7.4**, pump +9.0 (as `ip-ar-ab` Kinds=7, +49 ms/s) | key ~0; net unknown | **drop** |
| `[LocoWindow]`, `locowin` | H2 no job start in `Locomotion_Update` (docs/86 §5) | `0x62EF7A` gate 0 | `bp-wr-locowin` (docs/136 §3.7): no-op; keyed -10.1, it/s -8%, the noise reference | ~0, a no-harm check | **drop** |
| `[SlotEnabledRO]`, `slotenabledro` | SH QL-shared hold over `0x76F0A0` (docs/90) | `0x62F782` gate 12 | `bp-wr-slotenabledro` (docs/136 §3.1): key 12.5 -> 0.0, ~60% reappears at `0x83156D` (`RoutingWorld_RouteToObjectCheck 0x831530` `+0x918` take, inferred) +7.3; keyed -14.3; it/s on slices 42.8/42.8 vs off 36.1/34.0, so the 0.22.2 -17% regression is not reproduced | cap ~12.7 ms/s; net ~5 ms/s on its keys | **ship in v0.1.5** (on); IP in the release benchmark; next lever read-only `0x831530` |
| `[FpRebuildLock]`, `fprebuildlock` | C-A (docs/84 §4.4) | rebuild inside a job | built, Mode 0 observe only | none: the same wait, earlier | not queued |
| `[Discourage]` DestroyApply / DeadSweep, `discdestroy` / `discsweep` | D-X (docs/84 §7.2) | destroy, discourage | on in every base (defaults 1) | already in the baseline | not queued |
| `[Discourage] DestroyPark`, `destroypark` | docs/88 design G standalone (docs/88 §10): a destroy during any async job parks one reference on P5c's graveyard and runs without the job wait; released at the next no-job destroy / save / travel / quit / START-READY poll, owner thread only. Built 2026-10-21 (counters v76 in 0.36.11), offline only: selftest `TestDestroyPark`, steerro-test DG seeds 1-2 x 3000 (0 freed reads, 0 job-thread destructors; the no-wait control shows both). The old standalone v52 branch (`14a25ef`) stays unmerged | `0x2000003` destroy | not run | ~7-11 ms/s BP cap minus P5c's window share; expected net 3-6 ms/s BP, 1-3 IP [inferred] | queued: `queues/destroy-park-ab.json` (BP off-first + on-first, IP; clean night) |
| `[Discourage] DeferFootprint`, `deferfootprint` | docs/89 design U: `0x833560` deferred into D's queue while a job runs (docs/89 §10) | `0x833593` | built 2026-10-21, offline only (counters v74, selftest ok); queued `defer-footprint-ab.json` (BP off-first, BP on-first, IP), not run | key ~7-9 -> ~0-1 ms/s; keyed total -3..-5 ms/s BP | needs the A/B |
| `[AvoidRO]`, `avoidro` | read-only `RouteFollow_A 0x846B70` / `_B 0x85B470` on S2's clone; a footprint miss runs the game's body with its query call skipped (docs/137) | `0x846D21` avoidance, `0x85B50C` | **built 2026-10-21 offline** (docs/137 §11, counters v77): G-AV1-6 pass (the game's A / B / mode-1 query vs the clone equal on every compared case), not run; queue `avoidro.json` (shadow, then off-first and on-first `--ab avoidro` with S2 on in both halves) | key down ~8-12 ms/s with S2 on; part moves to `0x833593` and gates 0 / 12 / 10 | pending: `bp-avoidro-shadow`, then the A/B pair |
| `[IcLockCheck]` | counts the Mono loader lock | - | measurement only | none | not queued |

Avoidance `0x846D21` has a reducer since 2026-10-21 (`[AvoidRO]`, row above, offline only). S2's unknown slot types (31% / 91% fallbacks in S2 and the gate-0 fix): all of S2's were `SRS_SLOT`, and the only live footprint class the whitelist refused is the child footprint `0xFEBF90`; S2 now accepts it when its parent passes (docs/84 §3.6, 2026-10-21) [inferred to cover the 31%, I-196-1]. Expected: S2, `[ObstructedRO]` and `[AvoidRO]` all answer ~99% instead of ~69% / ~9%; the S2 and gate-0 rows above were measured before it, so their A/Bs are worth a rerun with it.

## 5. Risks, ranked

| # | Risk | Effect if it hits | Mitigation / check | Rows |
|---|---|---|---|---|
| 1 | **Gain too small (G5).** A10 already moves the plan off the sim; the snapshot adds a recorder (~0.15-0.6 ms/s) and a cut on the sim and only removes the `+0x918` contention. IP showed no gain in the in-load A/B. | the whole build stays off after ~28-40 h (about 50-60% chance of a real gain, §0) | measure keyed waits per site (`wait-ab.ts`) early: a P4 shadow run already shows worker time and recorder cost before P5 | I-94-11, I-94-57 |
| 2 | **Lot-side sync misses a write** (in-place footprint scalar writes, portal fields without an IC slot, unattributed callers) | S1 audit mismatches; stale clone = wrong answers | P1b frames, design B clone-vs-live hash audit, K1 with random addresses. Portal fields (H20, docs/94 §21.8.1): cost and age/gender flags are atomic or torn-pair stores (M5, no fault), the `+0x5C` grow of existing portals is behind D2; tripwire on the `AddPortalLocationVariation` slot | I-94-54, I-94-55, I-94-1, I-94-178, I-94-181 |
| 3 | **Shared allocator pools** (docs/94 §16.6): the clone's tree and map nodes come from free lists that 72 and 67 unlocked sim roots also use, and `PathPlanner_Reset` frees them | a node owned twice or lost: a crash later in an unrelated quadtree or map, not an audit mismatch; use-after-free at the first save or world change | private pools (map pool, 4 tree-pool sites, then 6 A\*/cell sites for P4), epoch check, `rpLivePool` = 0 | I-106-6, I-106-20 to I-106-29 |
| 4 | **Unredirected global** (register-computed write the static census missed) | silent race between sim and worker, shows as M2/M3 noise | U1 on the worker before any P4 run (sim-thread U1 is clean on BP and IP); K4 guard page; whitelist `0x114C900` | I-113-2, I-101-1 |
| 5 | **Torn non-routing reads** (world manager lot list, `Lot+0x290`, `Lot+0x298`, physics, terrain, foundation and water heights from pass 16) read with no lock | rare wrong answer or fault during build/buy or lot load | epoch check; `snap-buildbuy-edit` in the shadow; refuse during lot events | I-94-69, I-99-54 |
| 6 | **Replay non-determinism / order** (pointer-keyed sets, history-dependent level trees) | M1 / M1-tie at a low rate, easy to misread as ties | K1 with randomised clone addresses; tie-set rule tied to F-7 | I-94-52, I-94-53 |
| 7 | **Door answers in `Mode=2`** | proxy keeps sim round trips and adds up to a tick per ask (4.3 / 8.8 asks per plan), which limits gain; evaluator may differ from script | start with the proxy; evaluator only if G5 needs it (rule in docs/94 §19.1), shadowed against script | I-105-7, I-105-12, I-94-112 |
| 8 | **Mods** (GoHere handler, MonoPatcher detours, replaced EA assemblies) | event-59 answers differ, or our hooks collide | M2-1 to M2-3 for the proxy path; the full docs/104 guard only for the evaluator; mod jobs 16-18 of the release queue; `hook-sites --check` | I-104-*, I-103-6, I-107-6, I-94-114, I-94-115 |
| 9 | **Test tooling crashes** (two Frida agents unloading together, a stale `tested` asi) | a lost run or a false "our crash"; results from stale-build jobs of 2026-10-17 and later may be wrong | probe unload guards, side-session unload 5 s after the profiler, `queue.ts` refuses a stale ts3native, rerun jobs that need newer features | I-99-60 to I-99-62, docs/51 §81-§82 |
| 10 | **Hook sharing** (T1 in `[RouteGate]` site 8, door replay in `[ProbeValues]` `0x64F4D3`, pass-through in seven modules) | crash or skewed counters | selftests per shared stub; docs/68 rows in the same commit | docs/94 §12.10 #8 |
| 11 | **Rare paths unseen live** (sailing houseboat, option `0x200000`, frozen-pond planning, build levels, travel) | wrong answer in a corner case | supported-set refusal (M4 `unsupported`) until each is seen in shadow | I-114-9, I-100-13, I-99-30 |
| 12 | **Memory** (clone 10-40 MB, replay clone ~20-60 MB [inferred] plus prebuilt lot graphs, 4 MB ring, world images 8-10 MB) | address-space pressure in a 32-bit process | 64 MB cap for the cut, `ReplayCapMB=128`, refuse above; docs/66 budget (largest free block 1.3-1.5 GB on BP with DXVK) | I-94-5, I-94-56 |
| 13 | **Copy cost on the sim** (copy step 5.5 ms/s vs the 2 ms/s limit; IP cut 4.28 ms/s earlier) | eats the gain before G5 is measured | cheaper cut, tiles of 32 cells, `Prebuild=1` to avoid rebases; measure again after the private pools | I-94-37, I-94-86 |

## 6. Next steps (in order)

1. Merge the footprint-type fix for the lot copy, build the private pools (map pool, four tree-pool sites, epoch check, `rpLivePool`; ~3-4 h) and make the copy step cheaper than 2 ms/s; add the box-footprint check of docs/94 §15.1 in the same build.
2. Rerun `bp-snap-p3`/`ip-snap-p3` and `bp-snap-p3b`/`ip-snap-p3b` (G3 and the P3-B pass). Write today's results (137,610 checks, 2 ms for 36 lots, 5.5 ms/s, the crash and the 5-second unload gap) into docs/94 §14 and §16.4. Look at the SNAP seed 4 item (I-94-89).
3. Rebuild `native/ts3native/out/` from the current source, then run the remaining queued live jobs: watch-stalk on the worker (U1 for P4), live reads 2, value probe 0.24.1. Rerun any `tested`-build job of 2026-10-17 or later that needs features newer than 0.10.1.
4. Run the P1b count job (`snap-p1b.json`, G1'), with the box-footprint vtable counter.
5. Build P4 (part A, clone provider, D1-D6, A\*/cell pools; ~12-18 h); two 10-min shadow jobs; apply G4. Worker time and recorder cost from these runs are the first sign for the 50-60% gain figure.
6. Only then P5: door path (b) host-call proxy plus guard M2-1 to M2-3 (docs/94 §19); the native evaluator stays deferred behind the I-94-112 rule.
7. P6: build, the missing items of docs/94 §20.4, one clean night for `snap-release.json` (20 jobs, about 5.5 h), including the G5 A/B jobs.
8. Ledger pass for the rows in §2 J "ledger lag"; classify the 85 + 89 new indirect pairs of the pass-2 joins.
