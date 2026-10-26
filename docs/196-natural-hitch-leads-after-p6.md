# What leads natural-play hitches after P6

2026-10-25. Read-only analysis of existing runs: no game run, no build. Context: P6 (routing lock-wait work) is stopped because it moves waits instead of removing them (docs/189 §17). Target = noticeable hitches: p99 / p99.9, max, counts >= 300 ms and >= 1000 ms per 10 min (docs/161).

Tags: [measured] = tool output on the runs below; [inferred] = joined from design docs or by duration.

## 1. Inputs

No natural IP run on the shipped v0.1.5 package is longer than 4.5 min, so IP pools three builds. All counts are Simulate iterations >= 300 ms from `hitch-ab.ts --causes` (repro windows, first minute, sampler and probe-call windows excluded, docs/161 §6).

| World | Runs (real min) | Build | Notes |
|---|---|---|---|
| IP | `natural-play-2026-10-22-ip-natural-30` (24.3), `release-check-2026-10-24-ip-rc-smoke` (4.5), `overnight-1023-...-ip-v015bundle-{offfirst,onfirst}-0338` off arms (6.2) | 0.36.27 / v0.1.5 / asi.0338 | 35.0 min pooled, HitchAttr on in all |
| SV | `sv-cause-run-2026-10-24-sv-cause-run` (28.9) | master 0.36.39 = release defaults + logging | stack per sync plan, own-job wait lines |
| SV | `stuckcheck-cap-verify2/3-...-sv-sccap{2,3}-nat-{off,on}` (4 x ~28.5) | asi.sccap2 0.36.62, `stuckcheckown` on in all, `stuckcheckcap` off/on | verify2 timing noisy (other apps 12-13%); verify3 clean (9.1%) |
| BP | `release-check-2026-10-24-bp-rc-soak` (35.8) | ts3native 0.36.37, but ts3fixes package `93b16bb8ffbd`, not the v0.1.5 `949dac596eda` (docs/145 §12.2) | Overwatch chain was off in this package |

Rates per run [measured, `hitch-ab.ts`]: IP natural-30 17.7 >= 300 / 0 >= 1000 per 10 min (max 499 ms); IP rc-smoke 20.1 / 0 (max 496); SV cause run 8.6 / 2.4 (max 2,431, p99 61 ms); BP rc-soak 3.9 / 0 (max 498).

## 2. What leads each hitch

### 2.1 IP (35.0 min, 61 iterations >= 300 ms = 17.4 per 10 min, 2 >= 1 s) [measured]

| Cause group | >= 300 | per 10 min | >= 1 s | ms | Detail |
|---|---|---|---|---|---|
| Sim waits behind the async route job (`+0x918`, gates, destroy) | 21 | 6.0 | 0 | 8,176 | gate-12 F-G placement 5, `ObjectFootprintFromTool` 4, UpdateSimFootprint 3, steering obstacle query 3, then one each: gate-12 `GetIfPresent`, gate-12 `LotTable_Contains`, goal validation `0x75A64C`, destroy, UpdateObjectLot, gate 0 |
| NRaas Overwatch nightly alarm in one tick (`FunctionTask>OverwatchSpreadHook.OnTimer`) | 19 | 5.4 | 0 | 7,240 | all from builds where `overwatchchain` was off (natural-30 ran 0.36.27 without it) |
| Sync route plans on the sim thread | 12 | 3.4 | 1 | 5,354 | `ReplanFromPosition` 10 (243-339 ms, Foreign / not logged), `Replan` 1, `Route_Plan` 1 (1,640 ms, v015bundle off arm, no fallback line) |
| Locomotion stage, no wait key | 6 | 1.7 | 0 | 1,915 | steering waits likely [inferred, docs/178 §2 pattern] |
| Script: `ScriptProxy>Sims3.Gameplay.Actors.Sim` run | 3 | 0.9 | 1 | 1,851 | the known 1.1-1.3 s Sim stall that ends at `RouteBackoffHook.AsyncWait` (PLAN open list, now 4 sightings) |

Not seen leading any IP hitch: GC, load/stream, ErrorTrap, EA 3 AM / pet pool / decay, dispose.

### 2.2 SV, release defaults (28.9 min, 25 >= 300 = 8.6 per 10 min, 7 >= 1 s = 2.4) [measured]

| Cause group | >= 300 | per 10 min | >= 1 s | ms |
|---|---|---|---|---|
| Stuck-check probe plans, sync on the sim (`StuckCheckDrain` task, Foreign, `nojob`; docs/data/results/2026-10-24-sv-cause-run-analysis.md) | 12 | 4.2 | 7 | 13,353 |
| Sim waits behind the route job (`GetIfPresent` 2, `ObjectFootprintFromTool` 2, `RemovePrivateFootprint` 2, F-G 1, avoidance 1, steering 1) | 9 | 3.1 | 0 | 3,262 |
| Locomotion, no key | 4 | 1.4 | 0 | 1,530 |

Plus 2 more >= 1 s stuck-check plans inside a repro window (2,363 and 1,356 ms), excluded by the tool.

### 2.3 SV with `stuckcheckown` on: the stuck check moves into locomotion [measured]

| Arm (2 loads each) | min | >= 300 per 10 min | >= 1000 per 10 min | max | Leader |
|---|---|---|---|---|---|
| `stuckcheckcap` off | 57.9 | 6.6 | 1.4 (8) | 1,446 | locomotion 18 (7 of them >= 1 s), steering 5 (2 >= 1 s) |
| `stuckcheckcap` on | 57.2 | 9.6 | 0.35 (2) | 1,747 | locomotion 35 (all < 1 s), steering 6, destroy 3 (1 >= 1 s), gate 0 1 (1,747 ms) |

With `stuckcheckown` the probe plan runs as an own route job; the sim's locomotion pass waits for `+0x918` at the steering query `0x85FE10` for the whole job (docs/163 §10). The cap cuts each probe at `PlanDeadlineMs` 300 + overrun, so each >= 1 s wait becomes a 300-410 ms wait: >= 1 s falls (verify3 9 -> 2, CI "fewer with on" at >= 500 ms), >= 300 ms does not (8.0 -> 9.5, no clear difference).

### 2.4 BP (35.8 min, 14 >= 300 = 3.9 per 10 min, 0 >= 1 s) [measured]

Overwatch alarm 13 (3.6 per 10 min, 4,447 ms, max 463), `ScriptCore.ScriptProxy: Dispose` 1 (340 ms). Nothing else.

### 2.5 Across worlds

- Script batches lead: Overwatch alarm (BP, IP; SV rarely), Sim proxy runs (IP, SV), dispose (BP, SV). Owner classes are NRaas Overwatch `StuckCheck`/cleanups and the Sim's own interaction run up to the next yield; no other script owner leads more than one >= 300 ms iteration in these runs (single ones: `WorldLot` `Lot.Simulate`, `OneShotFunction`, AutonomyManager).
- Sync route plans lead: SV stuck check (all >= 1 s on SV), IP Sim `ReplanFromPosition`.
- Route-job waits lead about a third of IP and SV >= 300 ms freezes; they never reached 1 s in the release-default runs.
- GC: 0. Load/stream: 0 (the first minute is excluded; the ErrorTrap load tick is load-only and `errortrapspread` is on in v0.1.5).

## 3. Top 5 causes: fix state

| # | Cause | Built fix (switch, default) | Status | Expected reduction | Effort |
|---|---|---|---|---|---|
| 1 | SV stuck-check probe plans (>= 1 s leader) | `stuckcheckasync` (on, shipped), `owprobeplans` (on), `stuckcheckown` (off), `stuckcheckcap` (off, needs `PlanDeadlineMs` > 0), `PlanDeadlineMs` (sync path, `bp-plandeadline-ab` PASS), `stuckchecknear` (off, built, never run) | own + cap: verify2/3 run, waits PASS, reset parity (counters 297-301) still unread (queue.ts dropped them in verify2); near: not run | >= 1 s SV 2.4 -> ~0.4 [measured with cap on]; >= 300 unchanged until each probe wait is under 300 ms. **New:** a probe-only deadline (~180 ms, so deadline + overrun stays < 300) would move those ~6 per 10 min below the bar [inferred from the 300-410 ms wait spread] | probe deadline ~30-60 min build + SV natural pair (2 x 45 min); `stuckchecknear` smoke ~15 min |
| 2 | Sim waits behind the route job (IP 6.0, SV 3.1, plus most "locomotion") | RO clones S2 `steerro`, `avoidro`, `gate0ro`, `slotenabledro`, `routetoobjro`, `placementro`, `deferfootprint2`, `idlecheckro` (all off) | each built and verified alone; alone they move the wait (`idlecheckro` -> `0x755990`, docs/195; deferfp2 alone more >= 100 ms ticks); bundle never measured as one in natural IP; P6 stopped | upper bound IP -6.0 per 10 min if the whole set covers every key (docs/174 conservation: one switch alone moves the wait) [inferred]; no measured removal yet | bundle smoke ~15 min, IP natural pair 2 x 12 min; **needs a design** for what is left: the waits equal the job's remaining length (SV 464 ms wait / 486 ms job, 313 / 318 [measured]), so only shorter long jobs remove them |
| 3 | Overwatch nightly alarm in one tick (BP 3.6, IP 5.4) | `overwatchchain` (on in the v0.1.5 package `949dac596eda`), `overwatchstuckslice` (off, for SV's 100-336 ms StuckCheck walk) | chain: `bp-overwatch-ab-loop4-clean` 2 -> 0 >= 300, max 315 -> 112; active in `sv-cause-run` (172 `OwChainTheirs` mentions, never led a freeze). **No natural BP/IP run on the shipped package yet.** Slice: BP verify FAIL (no 8 ms stretch on BP), rerun on SV | BP 3.6 -> ~0, IP 5.4 -> ~0 [inferred from the loop result] | one natural BP + IP run each on the shipped package (~35 min each); fix the `queue.ts` `fixActivity` gate first (~20 min) so `owchain_alarms` is recorded |
| 4 | Sync `ReplanFromPosition` on IP (3.4) | ForeignKinds / A10 foreign RFP async (docs/176, off), `FallbackAsync` (off) | built in the lock-taker build; in the IP v015bundle on arms `Route_Replan` sync plans rose to 8 (13.3 per 10 min, 6 min): moved, not removed [measured, small sample] | IP -2.9 per 10 min if nothing moves [inferred]; risk of the `Replan` move above | correctness read of the on-arm `Replan` lines (static, ~30 min) before any run |
| 5 | Sim proxy run 1.1-1.6 s ending at `RouteBackoffHook.AsyncWait`; unkeyed locomotion | none | no design; 4 sightings | IP ~0.9 + 1 >= 1 s per 35 min | attribution first: managed samples of those iterations (correctness run only: samplers are heavy) |

Smaller, not top 5: `ScriptProxy: Dispose` (`disposespread` built, verify PASS, off; 2 freezes in all runs); ErrorTrap load tick (`errortrapspread` shipped on); EA 3 AM / pets / decay (`easpread*` built, off, not run: led 0 freezes in these runs); unseen throttles (`unseenthrottle`, `unseenbroadcast` / `unseenenv` / `unseenmotives`: ~0 hitch effect measured or no hitch evidence, docs/143/144); `jobyield` (reaches only script-side waits, the waits above are native); `failrepeat` (no repeated failed plan in these runs).

## 4. Ranked plan (validate and fix first, no new timing A/B unless Rodney asks)

1. **Engage check on the shipped package (BP + IP natural, 30 min each).** Fix `queue.ts` `fixActivity` (gate on `profile.fixesOn` or read counters before `restoreFixes`, docs/163 §10.2) so `owchain_alarms`, `stuckcheckasync_run`, `errortrapspread` counters land in the row. Pass: `OwChainTheirs` task lines, counters > 0, no Overwatch-led iteration >= 300 ms. Fold into the v0.1.6 hotfix gate if it already has natural jobs. Expected: Overwatch gone from BP and IP (IP 17.4 -> ~12 per 10 min).
2. **Stuck check (SV >= 1 s).** Read reset parity from verify3 (counters 297-301; rerun the pair only if missing). Then a probe-only deadline (~180 ms) in `ScProbeMark` / `ServeAsync` and a `stuckchecknear` smoke; one SV natural pair as correctness + hitch read. Ship `stuckcheckown` + cap only with reset parity. Expected SV >= 1 s 2.4 -> ~0.4, >= 300 ms -4..-6 per 10 min [inferred].
3. **`overwatchstuckslice` verify on SV** (repro `bp-overwatch-stuckslice-loop` moved to SV): correctness only.
4. **IP lock-taker set as one bundle:** correctness smoke with every RO switch on (incl. `idlecheckro` only together with a cover for `0x755990`), then one IP natural pair, then the moved-waits loop on what remains. Fold ForeignKinds in only after the `Replan` move in #4 of §3 is explained.
5. **Design (new):** shorter long own jobs, since route-job waits equal the job's remaining length: per-kind job deadlines like the probe cut (failed plan -> the game's own retry), judged on behaviour (route fails, stuck Sims) before hitches. **Built 2026-10-25 as `[RouteAsync] KindDeadlines` (docs/197, not run).**
6. **Attribution only:** the Sim proxy >= 1 s stall and unkeyed locomotion: one IP correctness run with managed samples on hitches.

Not next: P6 (stopped), unseen throttles, easpread, failrepeat, Mono offload work for hitches (T6 is steady ms/s, not hitches, docs/28 §0.5).

## 5. Reproduce

```
TS3_REPO=<main checkout> node tools/harness/hitch-ab.ts --causes release-check-2026-10-24-ip-rc-smoke natural-play-2026-10-22-ip-natural-30 overnight-1023-2026-10-23-ip-v015bundle-offfirst-0338 overnight-1023-2026-10-23-ip-v015bundle-onfirst-0338
TS3_REPO=<main checkout> node tools/harness/hitch-ab.ts --causes sv-cause-run-2026-10-24-sv-cause-run
TS3_REPO=<main checkout> node tools/harness/hitch-ab.ts --causes stuckcheck-cap-verify2-2026-10-25-sv-sccap2-nat-on stuckcheck-cap-verify3-2026-10-25-sv-sccap3-nat-on
TS3_REPO=<main checkout> node tools/harness/hitch-ab.ts --causes stuckcheck-cap-verify2-2026-10-25-sv-sccap2-nat-off stuckcheck-cap-verify3-2026-10-25-sv-sccap3-nat-off
TS3_REPO=<main checkout> node --stack-size=60000 tools/harness/hitch-ab.ts --causes release-check-2026-10-24-bp-rc-soak
TS3_REPO=<main checkout> node tools/harness/hitch-ab.ts stuckcheck-cap-verify3 stuckcheck-cap-verify2 sv-cause-run-2026-10-24
```

Not checked: `freeze-report.ts` visible-freeze counts (iterations used throughout; docs/data/results/2026-10-24-v015-hitch-causes.md shows the two agree within a few events), `sim-budget-report.ts` (docs/28 §0 already has the budget), managed samples inside the Sim proxy stalls (none in these runs), and verify3's outcome counters.
