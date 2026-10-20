# Release checklist

How a release goes out. Current: **v0.1.4** (background routing on by default), draft in `CHANGELOG.md`.

## v0.1.4 release build

| File | Version | sha256 |
|---|---|---|
| `native/ts3native/out/ts3native.asi` | ts3native 0.36.0 | `fa25a2c8420e5fb6546c13474b2e0ccfc6b03ac3b5b6ca9e28fc2196f525f007` |
| `native/ts3native/out/ts3native.ini` | release defaults | `68b2cfc27572b7679a1902c12c868efb6150f05c1e06a837ff068567114d1af0` |
| `build/ts3fixes.package` | defaults as v0.1.3 (17 tunables True) | `eec32200463d4f5650c80145baa4d01d90e2eed5f16f63d02d08322f1efc90fb` |

Release `ts3native.ini` defaults: on = `[GC]`, `[WaterJps]`, `[Decompress]`, `[SaveWrite]`, `[LotRemoveGuard]`, `[OptsInit]`, and new in v0.1.4 `[RouteAsync]` (`Mode=1`, `Kinds=0`), `[RouteGate]` (`Sites=1`, `Enforce=1`, `G10Common=0`), `[Discourage]` (`Mode=1`, `DestroyApply=1`, `DeadSweep=1`). Everything else off, including `[RouteSnap]` (P3-P5c code ships, `Enabled=0`). The installer (`install-fixes.ts`) and CI (`.github/workflows/build.yml`) both ship `out/ts3native.ini`, which `build.cmd` copies from `native/ts3native/ts3native.ini`.

Rebuild check: a rebuild from the release commit must give the same ini hash; the asi hash can differ only if the toolchain changed. Selftest note: one build of 0.36.0 failed once at `selftest.cpp:8441` (`W6b lots: nDirty -1`, threaded replay test), then passed 3 times in a row and in the next full build; a flaky threaded test, unrelated to the ini change, not yet investigated.

## Before publishing (Rodney OKs each)

1. **Validation queue passed** on a clean night: `node tools/harness/queue.ts tools/harness/queues/release-validate.json` (33 jobs, ~18 h of timeouts, about 10-12 h real; split with `--only`). Before it: copy `native/ts3native/out/ts3native.asi` to `game-copy/ts3native.asi.rel0360` (check the sha256 above), run from a checkout with the release ini merged (the queue writes `Bin/ts3native.ini` = repo ini; no job overrides it), install the release `ts3fixes.package`. Pass rules are in each job's note: 0 crashes, 0 watchdog lines, `ts3native 0.36.0` in the log, `asyncStarted > 0`, stuck Sims within docs/TESTING.md, every save matrix case PASS, travel cases PASS, rare repros ran with their Sims routing.
2. **Every crash triaged** against the change ledger (our build crashed = our bug until proven); a fix means a new build, new hashes here, and a rerun of the affected group (save path: the whole matrix).
3. **CHANGELOG `## v0.1.4` filled**: test table with case count, cases and results from the result page; date in the heading; measured-gain references checked (119 -> 50 ms/s and +10-12% iterations/s come from PLAN.md "Async routing (H6) state" and the 2026-10-16/17 `h6-m1` result pages).
4. **docs/PLAYERS.md** names every enabled section and every ts3fixes True tunable (CI fails otherwise).
5. **Gains wording OK** for players (no claim beyond what the runs show).
6. **Shortcuts on in the attached package** (test queues may switch them off at runtime; the release never ships them off): `grep -aoE '<k(GoHome|BuildingTrip|LotTrip|VirtualHome)Shortcut value="[A-Za-z]+"' build/ts3fixes.package` shows all four `True`, and the package hash matches the table above.

## Publishing (after the OKs; never from the private repo)

Via the scrubbed public copy only (Rodney): sync the tree into a clone of the public repo with `<user>` -> `<user>` and the machine name -> `<pc>` replaced, run `node tools/check-docs-code.ts`, open a PR, squash-merge, tag `v0.1.4`. The tag's CI run builds ts3native, checks the stable list and PLAYERS.md, and drafts the release from the CHANGELOG section; attach `ts3fixes.package` to the draft by hand (CI cannot build it: EA assemblies), then publish the draft. Never push local `master` to the public repo.
