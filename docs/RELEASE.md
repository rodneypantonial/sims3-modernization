# Release checklist

How a release goes out. Current: **v0.1.4** (background routing on by default), published 2026-10-20 (PR #55, tag v0.1.4).

## v0.1.4 release build

| File | Version | sha256 |
|---|---|---|
| `native/ts3native/out/ts3native.asi` | ts3native 0.36.0 | `fa25a2c8420e5fb6546c13474b2e0ccfc6b03ac3b5b6ca9e28fc2196f525f007` |
| `native/ts3native/out/ts3native.ini` | release defaults | `68b2cfc27572b7679a1902c12c868efb6150f05c1e06a837ff068567114d1af0` |
| `build/ts3fixes.package` | defaults as v0.1.3 (17 tunables True; shortcuts kGoHome, kBuildingTrip, kLotTrip, kVirtualHome True) | `c09fb14d283f6261cc780c42ab5207763d2b379a235e2b5669d40f60f17414ee` |

Release `ts3native.ini` defaults: on = `[GC]`, `[WaterJps]`, `[Decompress]`, `[SaveWrite]`, `[LotRemoveGuard]`, `[OptsInit]`, and new in v0.1.4 `[RouteAsync]` (`Mode=1`, `Kinds=0`), `[RouteGate]` (`Sites=1`, `Enforce=1`, `G10Common=0`), `[Discourage]` (`Mode=1`, `DestroyApply=1`, `DeadSweep=1`). Everything else off, including `[RouteSnap]` (P3-P5c code ships, `Enabled=0`). The installer (`install-fixes.ts`) and CI (`.github/workflows/build.yml`) both ship `out/ts3native.ini`, which `build.cmd` copies from `native/ts3native/ts3native.ini`.

Rebuild check: a rebuild from the release commit must give the same ini hash; the asi hash can differ only if the toolchain changed. Selftest note: one build of 0.36.0 failed once at `selftest.cpp:8441` (`W6b lots: nDirty -1`, threaded replay test), then passed 3 times in a row and in the next full build; a flaky threaded test, unrelated to the ini change, not yet investigated.

## v0.2.0 (in preparation: release-validate Thursday night 2026-10-22, publish Friday 2026-10-23)

The routing release. Each candidate ships on only if its A/B passes (`tools/harness/queues/clean-night-0.2.0.json`, ~7 h, plus the existing per-feature queues for the ones not in it: `defer-footprint-ab`, `destroy-park-ab`, `candidate-loop-ab`, `replan-defer-ab`, `avoid` jobs). Mono features are not in this release.

### Decisions (fill from the A/B results)

| Feature id | What | A/B (clean night) | Decision | Edit when on |
|---|---|---|---|---|
| `fallbackasync` | `[RouteAsync]` foreign-task plans async + 1 s cap (docs/134) | `*-fb-*` | [to decide] (`SyncCapMs=1000` decided 2026-10-21 with it) | `FallbackAsync=1`, `SyncCapMs=1000` |
| `m1retry` | `[RouteAsync]` parked M1Pending STARTs (needs `fallbackasync`) | `*-fb-*` | [to decide] | `M1Retry=1` |
| `slotenabledro` | `[SlotEnabledRO]` (docs/90, docs/136) | `*-slotro-*` | ship decided on BP; IP/SV [to confirm] | `Enabled=1` |
| `avoidro` | `[AvoidRO]` (docs/137) | not in tonight's queue | [to decide] | `Enabled=1` |
| `deferfootprint` | `[Discourage] DeferFootprint` (design U) | `defer-footprint-ab.json` | [to decide] | `DeferFootprint=1` |
| `destroypark` | `[Discourage] DestroyPark` (design G) | `destroy-park-ab.json` | [to decide] | `DestroyPark=1` |
| `unseenthrottle` | `[UnseenThrottle]` (docs/143; phase 2 if it lands) | `*-ut-*` | [to decide] | `Enabled=1` |
| `stuckcheckasync` | ts3fixes 3 AM stuck check in a task | release-validate repro only | [to decide] | `kStuckCheckAsync` True |
| `replandefer`, `replanspread` | ts3fixes door / build-buy replans | `replan-defer-ab.json` | [to decide] | `kReplanDefer`, `kReplanSpread` True |
| `safespotcap`, `farthestsorted`, `indoorcap` | ts3fixes candidate loops | `candidate-loop-ab.json` | [to decide] | `kSafeSpotCap`, `kFarthestLotSorted`, `kIndoorPositionCap` True |
| `overwatchspread` | ts3fixes NRaas Overwatch nightly spread | `*-spreads-*` | [to decide] | `kOverwatchSpread` True |
| `easpreadlots`, `easpreadpets`, `easpreaddecay` | ts3fixes EA nightly alarms spread | `*-spreads-*` | [to decide] | `kEaSpreadLots`, `kEaSpreadPets`, `kEaSpreadDecay` True |
| `errortrapspread` | ts3fixes NRaas ErrorTrap load scan spread | `bp-et-*` | [to decide] | `kErrorTrapSpread` True |
| `unseenenv`, `unseenmotives`, `unseenbroadcast` | ts3fixes unseen script throttle (docs/144) | `*-unseen-*` | [to decide] | `kUnseenEnvThrottle`, `kUnseenMotiveThrottle`, `kUnseenBroadcastThrottle` True |

### Release defaults in one step

`node tools/release-features.ts <ids from the table, comma-separated> --write --docs` sets every candidate's `ts3native.ini` keys and `ts3fixes` tunables (`tools/setup/build-fixes.ts`) to on or off, refuses a feature without what it needs (`m1retry` needs `fallbackasync`), warns when CI's stable list in `.github/workflows/build.yml` lacks an enabled section, and resolves the feature blocks in `docs/PLAYERS.md` and the CHANGELOG `## v0.2.0` section (on text for chosen ones, the "shipped but off" line for the rest). Without `--write` it only prints the edits; `--list` shows the table, `--status` what the repo has on now; `none` is the v0.1.4 state. `--docs` is one way (the markers go), so run it once, on the release branch. Then: build (`native/ts3native/build.cmd`, `node tools/setup/build-fixes.ts`), fill the hashes below, copy the asi to `game-copy/ts3native.asi.rel020`, run `release-validate.json`.

CI's stable list now also accepts `[SlotEnabledRO]`, `[AvoidRO]` and `[UnseenThrottle]` as `Enabled=1`; drop the ones not shipped from the list before tagging.

### v0.2.0 release build

| File | Version | sha256 |
|---|---|---|
| `native/ts3native/out/ts3native.asi` | ts3native [to fill] | `[to fill]` |
| `native/ts3native/out/ts3native.ini` | release defaults from `release-features.ts [ids]` | `[to fill]` |
| `build/ts3fixes.package` | v0.1.4 defaults + [chosen ts3fixes features] (shortcuts kGoHome, kBuildingTrip, kLotTrip, kVirtualHome True) | `[to fill]` |

Release `ts3native.ini` defaults: as v0.1.4 (`[GC]`, `[WaterJps]`, `[Decompress]`, `[SaveWrite]`, `[LotRemoveGuard]`, `[OptsInit]`, `[RouteAsync]` `Mode=1` `Kinds=0`, `[RouteGate]`, `[Discourage]`) plus [to fill from the decisions]. Validation: `release-validate.json` (46 jobs, ~8 h real; parts A ~5 h and B ~3 h with `--only`, see the queue note), results `docs/data/results/[date]-queue-release-validate.md`; pass rules as below plus `fbCapped` per job and route fails not above v0.1.4's.

## Before publishing (Rodney OKs each)

1. **Validation queue passed** on a clean night: `node tools/harness/queue.ts tools/harness/queues/release-validate.json` (33 jobs, ~18 h of timeouts, about 10-12 h real; split with `--only`). Before it: copy `native/ts3native/out/ts3native.asi` to `game-copy/ts3native.asi.rel0360` (check the sha256 above), run from a checkout with the release ini merged (the queue writes `Bin/ts3native.ini` = repo ini; no job overrides it), install the release `ts3fixes.package`. Pass rules are in each job's note: 0 crashes, 0 watchdog lines, `ts3native 0.36.0` in the log, `asyncStarted > 0`, stuck Sims within docs/TESTING.md, every save matrix case PASS, travel cases PASS, rare repros ran with their Sims routing.
2. **Every crash triaged** against the change ledger (our build crashed = our bug until proven); a fix means a new build, new hashes here, and a rerun of the affected group (save path: the whole matrix).
3. **CHANGELOG `## v0.1.4` filled**: test table with case count, cases and results from the result page; date in the heading; measured-gain references checked (119 -> 50 ms/s and +10-12% iterations/s come from PLAN.md "Async routing (H6) state" and the 2026-10-16/17 `h6-m1` result pages).
4. **docs/PLAYERS.md** names every enabled section and every ts3fixes True tunable (CI fails otherwise).
5. **Gains wording OK** for players (no claim beyond what the runs show).
6. **Shortcuts on in the attached package** (test queues may switch them off at runtime; the release never ships them off): `grep -aoE '<k(GoHome|BuildingTrip|LotTrip|VirtualHome)Shortcut value="[A-Za-z]+"' build/ts3fixes.package` shows all four `True`, and the package hash matches the table above.

## Publishing (after the OKs; never from the private repo)

Via the scrubbed public copy only (Rodney): sync the tree into a clone of the public repo with `<user>` -> `<user>` and the machine name -> `<pc>` replaced, run `node tools/check-docs-code.ts`, open a PR, squash-merge, tag `v0.1.4`. The tag's CI run builds ts3native, checks the stable list and PLAYERS.md, and drafts the release from the CHANGELOG section; attach `ts3fixes.package` to the draft by hand (CI cannot build it: EA assemblies), then publish the draft. Never push local `master` to the public repo.

From v0.2.0 also on ModTheSims, right after the GitHub release and with the same three files: `release/mts/CHECKLIST.md`.
