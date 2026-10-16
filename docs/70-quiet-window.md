# 70. Quiet window for the autonomy stage-C worker

PLAN.md "Gaps" item 1. docs/59 §3.5 places the stage-C scoring worker in the **native tail** of each sim iteration: after the script work (script tick, TaskObjects, OneShot, ThreadedObjects, GC step, finalizers) and before the next script tick. In that tail the sim thread runs animation (`animSacs` `0x571AD0`, `animPose` `0x571B90`), locomotion `0x83CED0`, render publish `0x593590` and frame waits, and no script runs. This doc measures how long that window is.

Tool: `node tools/harness/quiet-window.ts [--fit <ms>] [files...]` (reads `probe-out/profile/*-seconds.csv` with the `animSacs` column and the matching `*-hitches.jsonl`; `--self-check` tests the parser).

## Method and limits

- `stutter-profile.js` writes **per-second sums**, not per-iteration rows, so the window is derived as a per-second mean per iteration [measured sums, derived mean]:
  - **tail** = (`animSacs` + `animPose` + `locomotion` + `publish`) / `iters`: timed native-tail work. Lower bound of the window.
  - **tail + untimed** = (wall − script side) / `iters`, script side = `script` + `taskObjects` + `oneShot` + `threaded` + `gcMark` + `fin`. Adds frame waits, catch-up overhead and other untimed native work. Upper bound.
- Real per-iteration rows exist only for hitch iterations (≥ hitch threshold, ~64-77 ms median) in `*-hitches.jsonl`; they are listed separately.
- The window is at most one iteration long by design (script work runs between windows), so "length in ticks" is < 1 tick each time [inferred: assumes every iteration runs a script tick; per-second data cannot show iterations with no script work].
- Assumed: the four tail stages run as one stretch after the script work, and `publish` does not nest in a script stage (the hook comment says it "may nest") [inferred, not checked in the disassembly]. If `publish` nests, the tail is slightly overstated.
- Paused/loading seconds (`iters` = 0) are dropped. The `mainstack` run had the main-thread sampler on (24 it/s), so its iterations are longer than the others.

## Per-second mean window per iteration (ms)

Fit column: share of seconds whose mean window is at least 1.5 ms (docs/59 §3.3 scoring per search T ≈ 1.3-1.5 ms [inferred there]).

| Run (`probe-out/profile/`) | s | it/s | iter ms | script ms/it | tail p50 / p90 / p99 / max | tail+untimed p50 / p90 / p99 / max | tail % of time | tail+untimed % | s tail ≥ 1.5 | s tail+untimed ≥ 1.5 |
|---|---|---|---|---|---|---|---|---|---|---|
| `lotstream-2026-10-16-bp-lotstream-s3ss-off-360` | 353 | 69.1 | 14.5 | 8.4 | 2.29 / 5.13 / 7.13 / 9.93 | 8.07 / 13.48 / 18.20 / 23.34 | 10.7 | 41.7 | 64% | 100% |
| `lotstream-2026-10-16-bp-lotstream-s3ss-on-360` | 352 | 61.3 | 16.3 | 9.7 | 2.36 / 5.21 / 7.33 / 15.88 | 7.87 / 13.21 / 21.67 / 43.06 | 11.2 | 40.8 | 66% | 100% |
| `lotstream-2026-10-16-ip-lotstream-s3ss-on-360` | 345 | 46.1 | 21.7 | 14.7 | 2.22 / 4.26 / 7.52 / 9.75 | 7.58 / 11.23 / 18.26 / 55.62 | 9.4 | 32.4 | 75% | 100% |
| `mainstack-2026-10-16-bp-mainstack-360` | 349 | 24.3 | 41.2 | 29.5 | 5.39 / 8.51 / 13.56 / 18.69 | 12.03 / 17.44 / 26.76 / 35.17 | 12.5 | 28.5 | 100% | 100% |
| `s3pp-cache-2026-10-16-bp-s3pp-cache-off-600` | 589 | 43.6 | 22.9 | 16.2 | 3.18 / 4.57 / 5.89 / 8.14 | 7.39 / 9.64 / 12.02 / 15.14 | 11.9 | 29.4 | 92% | 100% |
| `s3pp-cache-2026-10-16-bp-s3pp-cache-on-600` | 587 | 33.5 | 29.9 | 21.0 | 4.13 / 5.43 / 6.90 / 10.07 | 9.29 / 11.56 / 15.29 / 17.30 | 12.7 | 29.6 | 98% | 100% |
| `verify-2026-10-15-bp-verify-420` | 412 | 79.4 | 12.6 | 7.6 | 2.30 / 3.94 / 5.29 / 6.25 | 7.37 / 10.36 / 12.73 / 13.85 | 9.3 | 39.6 | 67% | 100% |
| `verify-2026-10-15-ip-verify-420` | 197 | 88.3 | 11.3 | 7.4 | 1.15 / 2.71 / 5.24 / 6.31 | 5.15 / 8.42 / 14.41 / 15.76 | 6.2 | 34.8 | 37% | 99% |
| `verify-2026-10-16-bp-verify-420` | 399 | 75.3 | 13.3 | 8.6 | 2.72 / 6.39 / 9.59 / 10.89 | 7.56 / 13.76 / 18.08 / 30.02 | 10.2 | 35.6 | 67% | 100% |
| `verify-2026-10-16-ip-verify-420` | 376 | 75.4 | 13.3 | 9.1 | 2.20 / 5.32 / 7.63 / 8.87 | 7.95 / 13.51 / 17.32 / 31.96 | 6.6 | 31.3 | 66% | 99% |

No SV run carries these columns yet.

## Hitch iterations (real per-iteration rows, ms)

| Run | hitch its | wall p50 | tail p50 / p90 / max | wall − script p50 / p90 / max |
|---|---|---|---|---|
| `lotstream-...-bp-...-s3ss-off` | 1176 | 64.0 | 4.51 / 9.90 / 90.66 | 14.39 / 28.31 / 132.80 |
| `lotstream-...-bp-...-s3ss-on` | 1387 | 64.3 | 4.62 / 12.08 / 135.12 | 16.26 / 31.05 / 152.88 |
| `lotstream-...-ip-...-s3ss-on` | 1014 | 73.2 | 2.71 / 6.45 / 80.87 | 9.53 / 18.23 / 291.48 |
| `mainstack-...-bp` | 1883 | 68.6 | 6.18 / 12.05 / 133.00 | 13.57 / 24.25 / 138.34 |
| `s3pp-cache-...-off` | 1677 | 64.0 | 3.40 / 5.76 / 42.98 | 7.77 / 12.08 / 47.35 |
| `s3pp-cache-...-on` | 1997 | 65.7 | 4.21 / 7.38 / 47.96 | 9.65 / 16.04 / 72.28 |
| `verify-2026-10-15-bp` | 917 | 65.3 | 3.23 / 5.53 / 20.22 | 9.31 / 21.64 / 75.21 |
| `verify-2026-10-15-ip` | 384 | 76.8 | 2.06 / 4.44 / 103.64 | 6.96 / 13.72 / 182.33 |
| `verify-2026-10-16-bp` | 1292 | 65.6 | 4.72 / 9.81 / 30.57 | 12.04 / 25.48 / 88.65 |
| `verify-2026-10-16-ip` | 1024 | 73.4 | 3.18 / 7.95 / 74.83 | 10.18 / 19.34 / 103.89 |

## Verdict

- **Timed tail**: ~2-4 ms per iteration at p50 (BP 2.3-4.1, IP 1.2-2.2), 6-12% of sim time [measured sums; per-iteration mean derived].
- **Tail + untimed** (frame waits included): ~7-9 ms per iteration at p50, ~30-42% of sim time; ≥ 1.5 ms in 99-100% of seconds [measured sums; that the untimed part is free of script work is inferred from docs/28 G12].
- **Fit of the stage-C work** (docs/59 §3.3, all [inferred] there): scoring per search ~1.3-1.5 ms fits the p50 tail in every run and the p50 tail+untimed with 5x margin; it does not fit the timed tail alone in ~1/3 of seconds on BP and IP (`verify` runs). The snapshot build (native full 0.3-1.2 ms, incremental 0.05-0.1 ms) runs on the sim thread before hand-off, so it does not need the window; incremental fits trivially either way.
- In hitch iterations the window grows with the hitch (tail p50 3-6 ms, wall − script p50 7-16 ms), so long iterations give more room, not less [measured].
- Throughput, not window length, is the limit: at 45-90 it/s and one search per iteration, ~1.5 ms per search is 70-135 ms/s, above the ~44 ms/s scoring budget of docs/59; searches happen on a fraction of iterations [inferred], so the window is not the binding constraint.

## Open

- Per-iteration window distribution (not per-second means): add a ring buffer of per-iteration `wall − script` and tail to `stutter-profile.js`, or log every iteration for a 60 s slice.
- Order check: confirm in the sim loop that all four tail calls run after the script stages and before the next script tick, and whether `publish` nests.
- One SV run with these columns.
