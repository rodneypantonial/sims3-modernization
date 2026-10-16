# Verify run findings, 2026-10-15

Queue `verify` (docs/51): `bp-verify` PASS (9.2 min), `ip-verify` FAIL (game froze during the `thumbnails` step, heartbeat lost 14:29:47 UTC, no EA xcpt log; the queue killed it). Build `game-copy/ts3native.asi.verify` (0.10.6, counters v20, `cc9c0b15dfe1`), all `[Verify]` groups on, main-thread sampler on (2,991 main samples). Window not in front, so fps was capped: thread and race counts are valid, timings are not comparable with earlier runs. Counters are cumulative from game start, so they include the world load.

Raw dumps: `probe-out/queue/verify-2026-10-15-{bp,ip}-verify-native.json`. Mechanical verdicts were written into docs/51 by `verify-verdicts.ts --write` (12 rows); the rows below were read by hand.

## Confirmed

| Claim | Doc | Evidence (BP / IP) |
|---|---|---|
| Thumbnail texture create `0xD5D910` runs on the sim thread, off the device thread | 43, fix #4 | Sim 29 / 9 calls (`OffDevice` same), max 89.7 ms / 7.5 ms. The `thumbnails` step got no reply in BP and froze the game in IP [measured; freeze cause inferred] |
| Render-job bodies can run on the sim thread (sim pump takes shared-queue jobs) | 46, fix #21 | main cull `0x6F1B00` Sim 75 / 50, sort `0x6F1D10` Sim 118 / 96, shadow culls 71+45 / 41+13; `ExecuteJob` on sim 55,414 / 45,836 jobs |
| The package probe walks every package on each cache miss | 39 R1, fix #9 | `GetPackageSource` 2.38M / 1.21M calls, 131.6 s / 67.4 s total, max 227 ms / 65 ms; 334M / 153M index searches = ~140 packages per call, max 263 |
| The object id hash (1,033 buckets) is overloaded | 45, fix #22 | 11,813 / 11,718 objects (~11.4 per bucket); most lookups walk 8-15 entries (21.0M of 43.1M in BP), max chain 25 |
| Texture create `0x619540`/`0x60D290`, `0x6F3DC0`, PendingTextures drain `0x50A860` run on main only | 40, 46, 47 | Sim 0 and Other 0 in both worlds |
| Async load completion `0x7294F0` runs on main | 39 | Main 167,159, Sim 0, Other 0 |
| The game stores 0 for VRAM under DXVK | 37 C1, 40 | `vfMemVramBytes` 0 in both worlds |
| Device API wrapper is a large main-thread cost | 37, 40 | 14.9% of main samples in `0x60E000-0x61B000` |

## Not seen in this run (no evidence yet, not refuted)

| Claim | Doc | Evidence |
|---|---|---|
| Terrain shape swap while a route ray is in flight | 38 C1, fix #1 | 128 swaps, 0 overlaps |
| `GetKeyList` overlapping `RegisterDatabase` | 39 R2, fix #2 | 2,045 calls, 0 overlaps |
| Object id lookups from non-sim threads | 45, fix #24 | 0 (async routing was off) |

## Refuted or odd

- **10 FPS cap gate `0xECAA65`:** reached 0 times in both runs, so it does not fire in live mode (docs/41 P5).
- **I-41-1:** refuted by `verify-verdicts.ts` (0.20% of main samples in `0x611680-0x611800`).
- **Readouts to fix in the probe:** `vfMemSpeedTreeBytes` is negative (-10,057 / -9,373 KB), so `[0x11D8628]` is not a plain live-bytes total or is read wrong. `vfMemResCacheBudgetBytes` reads 15.7 MB, which is the AnimationCache size, not the 200 MB ResourceCache budget; the ResourceCache holds 442 / 389 MB.
- **UI lock `0xFBA5CC`:** 0 calls on every thread; one `vfReady` bit is clear, so that probe was most likely not hooked. Fix #28 is still unmeasured.
- **FrameDb:** main consume max 1.19 s and sim wait max 1.17 s, both most likely during the world load; the totals (25.8 s / 4.0 s in BP) cover the load too. Needs a live-only window.

## Next

1. Fix #4 (thumbnail texture race) is now the top crash fix: defer `0xD5D910` to the device thread. Add a repro that does not hang the agent (a timeout on the step).
2. Fix #9 (key-to-package map) and #22 (bigger id hash) have measured costs; build them with switches and A/B.
3. Fix the SpeedTree, cache budget and UI lock readouts, then rerun with a live-only counter reset after the load.

## Follow-up: probe 0.10.7 (2026-10-15, not run in game)

- **UI lock:** the probe was hooked (`vfReady` `0x17FFFFFF`: bit 27 is no probe). `0xFBA5CC` is the timeout pointer the 10 UI call sites pass to `Lock_Acquire`, not the lock (that is `ecx`), so the `ecx` filter never matched [verified asm]. The JobManager and SimService "locks" `0xFD9744` / `0xFE4DC4` are the same kind of constant. 0.10.7 classifies by the timeout pointer.
- **SpeedTree bytes:** `[0x11D8628]` adds and subtracts do not pair (e.g. alloc +0x13810, free -0x13800) [verified asm]: not a live total, readout dropped.
- **Cache budget:** 15.7 MB was the AnimationCache (created first, budget forced 15 MB); the 442 / 389 MB above are both caches together. 0.10.7 reads the priority-0 ResourceCache only.
- **VRAM:** offset verified statically (DeviceConfig `+0x174` hw info, `+0xC4` from the DxDiag path, 8,192 MB wraps to 0): the 0 is the game's value.
- **10 FPS gate:** `0xECAA65` is on the gate-true branch of the main loop only; 0.10.7 adds the loop count at `0xECA9ED`.
- **Helper bug:** the race rule took `vfRace*Calls` keys as race keys, which wrote `confirmed` for I-38-4 and I-39-1 (0 overlaps) and for I-34-6 (negative bytes); fixed, cells cleared (docs/51 §4).
- **Live window:** the next run zeroes the maxima after the load and the verdicts use after - before. The `thumbnails` step now replies at once and stops starting ICs after 15 s.

## Rerun 2026-10-16 (probe 0.10.7, counters v21, maxima reset after the load)

Both jobs PASS (BP 9.0 min, IP 9.1 min); no freeze. The new `thumbnails` step finished in BP (20.3 s) and timed out once in IP after the save. 3,908 main samples. Window not in front, so timings are not comparable with earlier runs. Ledger: 22 confirmed, 2 refuted, 89 open (the helper's `max:` rule is inverted for I-36-1; set by hand).

| Claim | Result (live window, BP + IP) |
|---|---|
| Thumbnail texture create `0xD5D910` on the sim thread (fix #4) | **confirmed again**: 44 sim calls |
| ThreadedObjects stage overruns its 5 ms budget (I-36-1) | **confirmed**: max 885 ms in `0xD7F8B0` |
| Native scene add is cheap (I-50-2, expected < 1 ms) | **refuted**: max 4.5 ms per call in `0xABFAC0` |
| UI effects updated while main draws (docs/47 R1) | **confirmed overlap**: 500k overlaps (one-frame visual tear, not a crash) |
| UI lock wait (fix #28) | sim 149k acquires, 21 ms total, max 6 ms: **small, fix #28 low value** |
| LoadLayout runs on the sim thread (I-36-3) | **confirmed**: 2,241 calls, max 15.7 ms |
| CAS model build `0x5E2050` and outfit callback `0x5AD8F0` on main | **confirmed**; build step max 160 ms on main (a main-thread hitch during outfit changes, fix #20) |
| Scene message handler `0x703CB0` on main | **confirmed**: 9.8M calls |
| Physics ray entry `0x6583E0` | sim only (4.6M calls), no main callers: the docs/33 "camera/UI picking on main" path does not go through it |
| Frames published vs consumed (I-41-4) | 52,454 published, 31,657 consumed: ~40% of sim frames are merged or dropped before the renderer sees them |
| Sync remote call `0x7D98C0` | max 20.9 ms wait |
| SpeedTree cluster rebuild `0x712480` | 0 calls in the live window (no tree changes during the pan) |
| Terrain shape swap race, GetKeyList overlap, off-sim id lookups | still 0 |
