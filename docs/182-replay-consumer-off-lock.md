# 182. P5 step 5: the replay consumer's records off `+0x918` (`[RouteSnap] RecsUnlocked`)

Written 2026-10-23. Static, model and selftest only: **no game run**. Built in ts3native 0.36.34, counters v105, switch default off. Inputs: docs/94 §22.29-§22.30 (the `snap-p5c` breakdown), docs/94 §16.6 (forward closure of the game's ApplyInsert / ApplyRemove), docs/156 (clone writer audit), docs/154 (lock model), docs/158 (gain model); `native/ts3native/replay.cpp`, `p4clone.cpp`, `p5.cpp`, `steerro.cpp`, `ts3native.cpp`. Ledger docs/51 §308. Tags as in AGENTS.md.

## 1. Problem [measured, docs/94 §22.29]

With a snapshot on (P4 / P5), the replay consumer ("snap replay" thread) applies the ring's records to the clone inside holds of `+0x918`, at most ~1 ms each. On 0.35.19 `snap-p5c` it held the routing lock **66.6 ms/s (BP) / 64.8 ms/s (IP)**, INS records ~97% / ~87% of it at 1.1-1.6 ms each; LOTS (~6 ms/s IP) moved off the lock in 0.35.23 (`LotsUnlocked`). The sim's `+0x918` waits were **87.5 / 98.8 ms/s with P5c against 68.0 / 73.1 without a snapshot**, the excess spread over every sim acquirer with waits/s up 38% at the same ms per wait: an extra holder, and the consumer is the only new one. 0.35.25's `InsFreeInPlace` cut the cost per INS (expected 5-15 ms/s of consumer holds, I-94-501); no run has measured the consumer with it yet.

The hold was never needed for the clone. The clone is guarded by `g_rpUse` (exclusive against a P4 / P5 clone run) and the private pool set (`RpOpTry`, pools.cpp), as `RpAudit`, `RpGc` and `LotsUnlocked` batches already rely on. The header of replay.cpp gives the lock one job: serialise the consumer with the sim's cut and builder, "which also write the clone".

## 2. Safety argument

### 2.1 Who else touches what an INS / REM / NOQ / STALE / SYNC record touches

| State the record touches | Other users | Guard without `+0x918` | Tag |
|---|---|---|---|
| `g_rpClone` (Qs, trees, map M, lots, footprint copies, `fps` / `kids` / `old` / `oldLive` tables) | P4 / P5 clone runs and acquires (worker), `P4CloneHolds` (sim, fault-guarded reads), the consumer's own audit / GC | `g_rpUse` exclusive + the pool set; the cut code (`RpCut`, `RpCutBody`, `RpEpoch`) is above `g_rpClone`'s declaration in replay.cpp and never touches it; the builder makes a new clone and posts it as an EPOCH record | verified source |
| tree nodes, leaf nodes, map nodes | the game's own pools are unlocked and shared (§16.6); the clone uses private pools | the pool set (`RpOpTry` makes this thread the op thread; the sim's epoch build and rebase copies try it too and defer when busy) | verified source |
| FMEs (`0x61ED80` / dtor `0x61ED10`), leaf arrays, buckets, polygon arrays | every thread | FME pool lock `0x11CEFA8`, EA heap critical section | verified (docs/94 §16.6) |
| mask table `0x11CF180` | ApplyInsert / leaf walks | written once at first use | inferred benign (§16.6) |
| footprint copies' vtable slots (`+0x2C` bounds, `+0x60`, `+0x84`, child forwarding to the parent's copy) | - | code + the copy's own fields; the copies' "owner" words are live pointers never followed by the replay (replay.cpp header) | inferred (I-182-1), counted by `ulLockTrip`, fault-guarded |
| record payloads' live addresses (Q, footprint, key) | - | table keys only; the one read of a live Q (`RpQKey`, a log line) is `Readable` + `__try` | verified source |
| `g_rpForce`, `g_rpNSoftPend`, `g_rpForceAll` | the sim's cut | the ring spin lock (`RpRemForce`, the NOCOPY path) | verified source |
| `g_rpNeedEpoch`, `g_rpEpochWhy`, `g_rpBad` | the cut clears `g_rpNeedEpoch` in `RpEpochBody`; P4 sets it | the same unlocked writes `LotsUnlocked` batches make since 0.35.23. Races: a consumer ask lost to the cut's clear is an ask on the clone that EPOCH replaces anyway (the EPOCH is pushed after the consumer's record); an ask after the clear but before `g_rpEpochAt` moves is one extra rebuild (cost only). `g_rpBad` stays set until the consumer applies that EPOCH, so P4 refuses in between | verified source; races inferred harmless (I-182-2) |
| `g_rpStale`, `g_rpSoftRun`, `g_rpSoftFresh`, `rpSkip`, `rpApplied` | consumer only (P4 reads `g_rpStale` holding `g_rpUse`) | single writer | verified source |

Kept under `+0x918` (unchanged): EPOCH (`RpGraveClone` drops the replaced clone's **live** portal references), the grave steps (FME destructors, portal releases, map node frees of the old clone), `RpLotsFreePend` (untaken shells: portal releases), the PathPlanner_Reset check (`PlMoved` reads `world+0x95C`), `RpAuditDiff`, the switch-off drop, and LOTS when `LotsUnlocked` is off. AUDIT stays outside as before.

### 2.2 Hooks the game's ApplyInsert / ApplyRemove reach on the consumer thread

Every hook address in the closure's ranges (`0x61ED..`, `0x627..-0x668..`, `0x80E..-0x80F..`; 61 sites in our source) was read for what it does on a thread that is neither the sim nor the worker [verified source]:

- P1 W1 / W2 / W6 stubs (`SnEnter`): return at once on `t_rpReplay` (0.32.1).
- QL sites on `0x665C50`, `0x666750`, W6a-d (`QlEnterBody`): count P1 only (`SnEnter`, skipped), take QL only on the worker, tripwire counters only on the sim. Nothing for the consumer.
- T4 tripwires `0x6653A0`, `0x666010`, `0x61ED80`, `0x61ED10` (`TwEnterBody`): on any other thread while a job is armed or running, they raise `g_twActive` and move `g_twEpoch`, and **an S2 clone (steering RO on the sim) that sees either drops its answer**. Today the consumer cannot be in them during a job (the job holds or lends `+0x918`, and P5bLock keeps the consumer out of a lent window, docs/154 `p5b-main-rp`). Unlocked, it would be, and S2 would fall back to the locked path more often. **Fix in this build:** `TwEnterBody` returns on `t_rpUl` (set only inside an unlocked round): the consumer writes clone trees and FMEs from the locked FME pool, which no S2 read sees. Switch off = unchanged.
- pools.cpp pool pops / pushes: private for the op thread (the consumer holds the op inside the round).
- routegate / verify `LotTable_Contains 0x628A90`, rtsites sites: not in the direct closure (§16.6) [verified static]; any that an indirect footprint slot reached would end in `LockAcquireHook`, which now counts `ulLockTrip` on the consumer (expected 0).
- loadprof `0x665C50` (load profiler timing), avoidro / obstructro / rtoro `0x80E...` (sim-side RO callers): no effect on this thread.

### 2.3 Lock model (docs/154, `tools/model/p5-lock-model.ts`)

New: actor state `op` (the pool set); a sim op `cut` (acquire `+0x918`, try the pool set, rebase or defer, release); the worker's clone acquire takes `g_rpUse` and the pool set; a consumer path `u1`-`u3` (`g_rpUse`, try the pool set, apply with the "consumer writes the clone during a clone run" check, release) and an optional `+0x918` acquire inside the round through P5bLock (`rpuTrip`, the `ulLockTrip` case).

| Config | States | Result |
|---|---|---|
| `rpu-base` (start, collect, steer, gate, cut; main acquirer) | 115,220 | clean |
| `rpu-p5c` (P5b + P5c windows, door ask, writer, cut) | 108,908 | clean |
| `rpu-tu` (TailUnlock lends) | 46,690 | clean |
| `rpu-trip-p5c` (a `+0x918` acquire inside the round, holding `g_rpUse` and the pool set) | 85,924 | clean: PcAcquire refuses by its deadline (mat), the cut defers its rebase, no deadlock, no lend lost |
| `mut-rpu-nouse` (mutation: round without `g_rpUse` and the pool set) | 871 | caught: "replay consumer writes the clone during a clone run" |

Each of `g_rpUse` and the pool set alone excludes a clone run in the model (a first mutation that dropped only `g_rpUse` stayed clean for that reason); the code takes both. `--self-check ok` (all 41 configs).

## 3. Change (0.36.34, `[RouteSnap] RecsUnlocked`, default 0)

- replay.cpp `RpRecsUnlocked`: with `RecsUnlocked=1`, a run of INS / REM / NOQ / STALE / SYNC records at the ring tail is applied holding `g_rpUse` and the pool set, not `+0x918`, through the same `RpApply`, up to 1 ms a round, under `__try` (a fault drops the clone and asks a rebuild, as the locked path). The locked hold stops at such a record and leaves it to the next round. The GC runs every 16 rounds or when caught up, as after locked holds. `g_rpPhase` bit 16 marks the round (P5PreLock and PcAcquire still see the consumer busy).
- `t_rpUl` (ts3native.cpp): set inside a round; `TwEnterBody` returns on it (§2.2); `LockAcquireHook` counts `ulLockTrip` when the consumer reaches the routing lock inside a round.
- Counters v105 at `+0x4548` (docs/registry.md): `ulMode`, `ulRounds`, `ulRecs`, `ulUs`, `ulMaxUs`, `ulOpBusy`, `ulLockTrip`, `ulFaults`; agent.js `>= 105`. The 5-min "consumer time under the routing lock by record" line gets a last bucket "records outside the lock (RecsUnlocked rounds)"; `rpApplyUs` (the snap-audit-report's consumer hold) counts only locked holds.
- ini: `[RouteSnap] RecsUnlocked=0` in the OK line. No `native_set` / `--ab` switch (the replay setting is read once at startup, like `LotsUnlocked`).

**Checks [offline].** `TestRecsUnlocked` (selftest): the 8,000-step random replay of `TestInsFreeInPlace` (inserts, removes, replaced footprints, W3 / W4, lot adds and removes, cuts every 5 steps, every-lot audits every 400) with `InsFreeInPlace=1`, switch off and on: audits equal both ways, same clone footprint count, off 0 rounds, on every applied INS / REM through an unlocked round, `ulLockTrip` 0, no fault. Lock model §2.3. `hook-sites.ts --check` ok (no new site), `check-docs-code.ts` ok. Build and selftest results: §6.

## 4. Expected effect [inferred, I-182-3]

| | BP | IP |
|---|---|---|
| consumer under `+0x918` now (0.35.19, before InsFreeInPlace) | 66.6 ms/s | 64.8 ms/s |
| of it moved by RecsUnlocked (INS / REM / NOQ; LOTS already off with LotsUnlocked) | ~64 | ~56 + (LOTS 6 already off) |
| left under the lock (grave, EPOCH portal drops, leftover frees) | ~1-3 | ~1-3 |
| sim `+0x918` wait with P5c (87.5 / 98.8) | **-15..-20 -> ~68-72** if the consumer still holds ~50-66 ms/s with InsFreeInPlace; **-2..-8** if InsFreeInPlace already took it to 5-15 ms/s | **-18..-26 -> ~73-80**, or -2..-8 likewise |

Method: the P5c excess over no snapshot (+19.5 BP, +25.7 IP) is the consumer's share of the sim waits (docs/94 §22.29.2 a); a holder that leaves the lock takes its share with it, minus the waits that move to the next holder (jobs) [moved-waits rule]. Upper bound: the excess; lower bound: zero if the excess was job holds lengthened by P5 instead.

Side effects [inferred]: (a) the consumer reaches a job's cut while the job holds `+0x918`, so `P5PreLock` waits less and PcAcquire's `mat/timeout` refusals (consumer behind) should fall; (b) the consumer holds the pool set ~6% of the time instead of only inside its locked holds, so a sim cut's rebase or epoch build defers to the next cut more often ("rebase ... waits, a P4 run owns the private pools" lines; consumer side `ulOpBusy`); (c) S2 unchanged (§2.2 fix).

## 5. Run (not run here; day job, correctness first)

On the next snap / P5c queue (`snap-p5c.json` or `p5-bundle-verify.json` settings plus `RecsUnlocked=1`), BP + IP, 10 min:

1. Pass: no crash, `ulFaults` 0, `ulLockTrip` 0, `rpMis` and `rpDiverge` as the switch-off run, no "fault applying record", P5 verify M1-M3 0.
2. Read: the 5-min consumer line (locked buckets should be grave + epoch only), `rpApplyUs` ms/s, `ulUs` ms/s, `ulOpBusy`, "rebase waits" lines, `snPcRefused[4]` (mat/timeout).
3. Gain: sim `+0x918` waits (`haUs[0]`, wait-ab) with RecsUnlocked 0 vs 1 on the same build and settings (a clean-night A/B per the clean-night rule; the switch is ini-only, so two loads).

## 6. Build

2026-10-23 08:14 UTC, `native/ts3native/build.cmd <worktree>\out-p5c` (all targets, `/W4 /WX`): selftest ok, `TestRecsUnlocked`: off 0 rounds; on 2,816 rounds applied 24,739 records outside the lock (all 20,913 INS / REM), audits equal, same clone footprint count. The size and version asserts of the other counter tests moved to v105 (4442 words). asi `out-p5c/ts3native.asi` in this worktree (not committed), sha256 `f9c8c3c40770ce56efe71d6b7d67dd603bd7fc4c778585e5782175fb993c5c26`. Not run: steerro-test (game image; no game image path changed).

## 7. Ledger

docs/51 §308: I-182-1 (no footprint slot reached from the clone's ApplyInsert / ApplyRemove takes `+0x918` or reads lock-guarded live state; check `ulLockTrip` 0 and no fault in 2 runs per world), I-182-2 (unlocked `g_rpNeedEpoch` / `g_rpBad` races cost at most one extra rebuild; check `rpEpochs` per 10 min switch off vs on), I-182-3 (the gain in §4; check §5 step 3).

**Effort (Claude, wall clock):** reading + design ~45 min, code + model + selftest ~40 min, docs ~20 min. Left: 1 BP + 1 IP correctness job (~10 min each), then a clean-night A/B pair.

## 8. Live finding 2026-10-23: a route gate inside the round (I-182-1 refuted as stated; switch stays off)

**Measured (`p5c-recs-0339`, BP and IP, `RecsUnlocked=1`):** `ulLockTrip` 4 per run (expected 0) and copy `MISMATCH` 7 (BP) / 30 (IP); P5c alone had 0 mismatches on BP. A debug build (master + a caller log in `LockAcquireHook`, not committed) names the caller of the `+0x918` acquire inside the round: ts3native's own `RgEnter` (routegate.cpp, `+0x498` in a `/MAP` rebuild of the same source). So the game's ApplyInsert `0x665C50` / ApplyRemove `0x666750` on a clone Q (or `RpInsCheck` / `RpRemFlat`'s `+0x2C` bounds ask on the copy) reaches a route-gated site through an indirect footprint slot: none of the 15 `kRgSites` is in the direct closure (docs/94 §16.6 [verified static]), so the path is a vtable slot the static closure did not follow (the inferred part of I-182-1). `RgEnter` treats the consumer as an unlocked non-worker entry: with a job running it waits for the live `+0x918` (the trip; lock model `rpu-trip-p5c`: no deadlock), with no job it runs the site on the consumer with no lock at all, so the 4 trips per run are the lower bound of the gate entries (the per-site `rgUnlocked[]` counts hold them, mixed with the sim's).

**Which site [named live, `p5c-site-0340` BP, asi.0340 with the gate log]:** every `RpUlGate` line is site 12 `LotTable_Contains 0x628A90` from `0x62FB74` and site 0 `lot table find-or-insert 0x62EAB0` from `0x62FB8B`, record types 1 and 2 (INS / REM), job none. The function is `LotTable_GetIfPresent 0x62FB60` (thiscall P, lot key lo / hi: `Contains` on P, then `GetOrInsert` on `P+0x48` with the key on a hit, 0 on a miss) [verified disassembly]. It is a **direct** call in both apply bodies, missed by the §16.6 closure: `ApplyInsert 0x665C50` at `0x665D1E` with `ecx = [Q+0x20]` (the planner P the Q belongs to), args `[Q+0x18]` / `[Q+0x1C]` (the Q's lot key), result kept at `[esp+0x1C]` and used as the lot node for the footprint slot `+0x68` call (`node + 0xA0`, `0x665DF0`) and as the "no lot: skip the record" test (`je 0x665FF4`); `ApplyRemove 0x666750` at `0x666796`, the same with slot `+0x6C` (`node + 0xE0`, `0x6669A1`) and `je 0x666A09`. The clone's Qs are byte copies, so `[Q+0x20]` is the **live** planner: the ask walked the live lot table (a window-slot write on every hit, docs/94 §21.6) and, inside a round with a job running, waited for `+0x918` at the gate (the 4 trips).

**Fix (0.36.35 merge build, `out-redirect`):** both call sites retargeted (rel32, each checked as a call to `0x62FB60` first, both or the switch turns itself off) to replay.cpp `RpGetIfPresent`: outside a round the game's call on the caller's P, unchanged; inside a round (`t_rpUl`) the same game body on the clone's P copy `c->p`, whose bucket array `c->lotBk` chains the clone's own lot node copies (`RpTableLink` / `RpTableUnlink`, the table the P4 clone runs plan on). The answer is the clone's node (its `+0xA0` / `+0xE0` are the copy's), nothing live is read or written except the active-lot id pair `Contains` reads through `0xC88FD0` (the sim writes it at a lot switch only; a torn read makes a miss). A miss on the clone table (the lot left since the cut) returns 0, so the game's body skips the record, and `RpForceLot` rebases the lot at the next cut (log line `not in the clone's lot table inside a RecsUnlocked round`, first 8). `RgEnter` passes sites 0 and 12 through under `t_rpUl` (no frame, no lock; `RpUlGate` returns true); any other site inside a round still logs its first 8 hits. `ulLockTrip` expected 0 again. Selftest (`TestRecsUnlocked`): a fake table walk records which P it was asked on: outside a round the live P and node, inside the clone's P and the clone's node, a miss answers 0 and forces the lot, `ulLockTrip` unchanged; `RpUlGate` passes 0 / 12 only. docs/68 row "Replay lot table on the clone". Lock model: `rpu-trip-p5c` keeps the no-deadlock proof for a trip that is now gone by design.

**Decision per site (the table that led to the fix; sites 0 / 12 resolved as above):**

| site | touches | rule |
|---|---|---|
| 6 `Q reset 0x61E320`, 7 `Q reset (clear path) 0x61E420`, 9 `Q node table clear 0x61C020` | the Q the call is made on: the clone's own Q (clone-private) | pass through in `RgEnter` when `t_rpUl` (no frame, no lock) |
| 8 `search generation stamp 0x61E360` | one live global word (the search generation) | a shared write: keep the record type locked (or skip the stamp: it is only read by searches) |
| 0 `lot table find-or-insert 0x62EAB0`, 12 `LotTable_Contains 0x628A90` | the live planner's lot table: a hit writes the table's window slot `[table+0x50]` (docs/94 §21.6), a miss inserts | live shared state on every path: the record type stays locked, or the ask is answered from the clone's lot list (`RpFindLot`) before the game's body |
| 2 `lazy room connectivity 0x63D2F0`, 3 `lazy lot build 0x63CB90`, 4 `lot rebuild 0x63D7B0`, 5 `room graph refresh 0x63D860` | live lot graphs and the shared pools (docs/94 §16.6) | never from the consumer: the record type stays locked, and a hit here also means the copy asked a live lot (a writer the replay must not drive) |
| 1, 10, 11, 13, 14 (planner, goal step, IC, build/buy rebuilds) | the planner | not reachable from an apply; a hit is a bug in the path, keep locked |

**Mismatch hypothesis [inferred, I-182-4]:** the 7 / 30 copy mismatches with the switch on fit the live ask: (a) the live lot node's `+0xA0` (insert) / `+0xE0` (remove) fed the clone copy's slots, so a clone footprint carried live lot data where the clone's own node could differ, and (b) the window-slot write on the live table raced the sim's lookups (a wrong lot answer for a sim-side footprint ask). The switch-off run of the same build (0 mismatches on BP with P5c) is the control; with the ask on the clone's table the count should return to it. If the mismatches stay, they come from elsewhere (I-182-2's lost ask, or the unlocked read of `g_rpClone` by the audit).

**Status:** `RecsUnlocked` stays 0 by default (unchanged). Next: the 2 correctness jobs of §5 again on `out-redirect` (expect `ulLockTrip` 0, no `RpUlGate` line, mismatches at the switch-off count), then the clean-night A/B.
