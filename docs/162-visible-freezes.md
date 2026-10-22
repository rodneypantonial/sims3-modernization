# 162. Visible freezes and camera stutter: what the player sees

Written 2026-10-22. **Measurement built, not run in game.** The player's complaint is Sims freezing in place while the camera keeps moving, plus camera stutter. Sim-thread iteration times (docs/96, worst-hitches.ts) are a proxy for the first; this doc adds the direct measures and a report that attributes them with the same functions the hitch reports use.

Tags: [verified] = read in the disassembly of `game-copy/bin/TS3W.exe` (not copied here); [inferred] = reasoning, not checked live; [measured] = from a run (none yet).

## 1. Metrics and files

| Metric | What it measures | Source | File |
|---|---|---|---|
| Visible Sim freeze | Time between two sim-to-render publishes (`FrameDb_PublishFrame 0x593600` returns on the sim thread). The renderer only interpolates between the two newest published frames (docs/72 §2), so no new publish = Sims stand still on screen | `tools/frida/stutter-profile.js`, one timestamp per publish; gaps >= 50 ms logged with QPC start/end, the previous gap, the iteration number `n` and the iteration boundaries inside (`iters`) | `<tag>-<secs>-freezes.jsonl`; `publishes`, `maxPubGapMs` columns in `-seconds.csv`; `n` in every `-hitches.jsonl` line |
| Camera stutter | Presented frame time on the main thread (`0xEC9F00` with present = 1) | `tools/harness/frame-times.js` (CModule, no JS lock, nothing suspended), now in every profile-world run unless `--no-frames` (off with `--no-prof` / `--watchstalk`) | `<tag>-<secs>-frames.csv` |
| Sync plan reason | `route async: sync plan N ms, IC, reason, entry` + its script stack from `Bin/ts3native.log` (needs `RouteAsync.LogFallbackMs`), stamped with epoch ms by profile-world every second (the log has no time) | profile-world.ts log tail | `<tag>-nativelog.jsonl` |

Report: `node tools/harness/freeze-report.ts <tag>` (from a worktree `TS3_REPO=<main checkout>`; `--min 300`, `--top 10`; `--self-check` runs the fixture). It prints, after the first 60 s:

- visible freezes >= 100 / 300 / 500 / 1000 / 2000 ms per 10 min;
- publish gap p99 / p99.9 / max: exact from the tail, since the per-second `publishes` count gives the total (a percentile below 50 ms prints `< 50`);
- presented frames: FPS, p99 / p99.9 / max, frames > 50 / 100 / 300 ms per 10 min, focused share;
- the gap vs iteration check of §2;
- top causes for freezes >= 300 ms and the worst 10: the longest iteration in the gap through `worst-hitches.ts cause()` (class, the `sim-budget-report.ts` bucket that leads it, the longest `[HitchAttr]` wait by key, plans by entry), the sync plan logged in the gap (its time within the gap + 1.5 s, its ms <= gap + 50), and the managed category sampled in the gap (minutes 1-4 only, `stall-split.ts alignRun`, inferred alignment).

Thresholds `FREEZE_MS` / `FRAME_MS` and `tailPct` are exported for `hitch-ab.ts` (another agent's; not on master when this was written): keep both on the same lists.

## 2. Publish gap vs iteration time [verified unless marked]

- `MonoScriptHost_Simulate 0xD81840` publishes through `0x76C8A0` at most once per iteration: at `0xD81DF5` at the end of the step branch (then `jmp 0xD81F91`), or at `0xD81E9E` in the paused branch. An unpaused iteration with 0 steps (`0xD81B9D` -> `0xD81E01`, paused check false -> `0xD81F0D`) publishes nothing. `0x76C8A0` also returns without publishing when `0x5934C0` gives no FrameDatabase, its byte `+4` is 0, or `0x593590` returns 0 (fixed-step mode, docs/72 §2).
- Catch-up steps (up to 10, docs/85 §3) all run inside one iteration before its single publish, so a long iteration is one gap and the next frame jumps forward in game time.
- `0x593590` (the profiler's `publish` stage) runs twice per publishing iteration (`0x76C5C0` at `0xD81A81` and again inside `0x76C8A0`), so its exits are not the publish clock; the freeze log times `0x593600` instead.
- The profiler's iteration boundary is `GC_try_to_collect` at `0xD819AA`, before the steps; the publish comes after them. A gap therefore spans the tail of iteration `n - iters` after its publish and iteration `n` up to its publish; `n - iters .. n` are the iterations it overlaps, and their `-hitches.jsonl` lines carry the same `n`.

So a gap can be longer than its longest iteration when iterations without a publish chain (0 steps while unpaused), and is roughly that iteration's wall otherwise. The report measures it: gap minus the longest iteration inside (p50, p90), gaps >= 100 ms with no iteration >= 50 ms inside, gaps spanning >= 2 boundaries, and publishes per iteration (docs/72 §3 estimated ~1.0 BP, ~0.75 IP).

Interpolation delay [inferred]: the render clock runs about one publish interval behind and holds at the newest frame (`FrameDb_AdvanceRenderClock 0x693660`, docs/41 P1), so motion keeps going for up to one previous interval after the last publish. On-screen stillness is between gap - previous gap and gap; each freeze line carries `prevGapMs` and the report prints its median (expected ~15-35 ms, small against the >= 100 ms thresholds).

## 3. Camera stutter

frame-times.js records `frameMs` per presented frame and `fg` (window focus). Profile runs are never focused (game-window rule), and the unfocused game sleeps (MainLoop `Sleep(10)` per loop; the `SleepEx(30)` / S3SS inactive limit at `0xEC9FBA` only while device `+0x8D` is set, §7), so these runs have a raised frame floor (2026-10-22 `bp-freeze-pan`: p50 62 ms) unless `[Harness] ForegroundRender=1` (§7); frames > 50 ms are real stalls of the main thread. A long frame and a sim freeze are separate: the main thread keeps drawing (and the player's camera keeps moving) while the sim is frozen.

## 4. Camera movement

profile-world's seeded run did not move the camera. `--camera-pan` (queue `profile.cameraPan: true`) calls the agent's existing `camera_pan` (`mods/ts3agent` ContentHandlers): 8 lots around the densest lot, 25 m up, 120 s per pass, there and back for the whole window, stopped at the end. No window focus, no input. The path depends only on lot positions and the pass clock (no RNG draws), so the seeded run stays seeded.

Limits:
- `camera_pan` moves the camera from a sim-thread task (`Simulator.Sleep(0)` per tick), so the scripted camera stops while the sim is frozen; a player's mouse camera would not [verified code; effect on screen inferred]. The frame times are still the main thread's own.
- The camera decides which Sims and lots are seen (EA LOD, `[UnseenThrottle]`, lot streaming), so pan runs change sim work and compare only with pan runs. It stays off by default; the queue has a BP control job without it.

## 5. Overhead [inferred, not measured]

- Freeze log: one Frida JS callback per publish (~30-60/s) on the sim thread, beside the ~15 hooks per iteration the profiler already has; one JSON line per gap >= 50 ms. Not in the all-or-nothing prologue check: a mismatch at `0x593600` (bytes `56 57 8b f1 8d 7e 48 57`) only turns the freeze log off. A ts3native or S3PP detour there is followed to the wrapper, as for the other hooks.
- Frames: one CModule enter/leave per frame on the main thread (two QPC reads), drained every 250 ms on Frida's thread; tests B (tests-be.ts) ran it next to the stutter profiler. Own Frida session, unloads 5 s after the profiler (two agents unloading together, docs/99 §12.7).
- Log tail: one `stat` and one read of the new bytes per second in the harness process.

## 6. Queue

`tools/harness/queues/freeze-measure.json` (dry run ok, NOT run): `bp-freeze-pan`, `bp-freeze-still`, `ip-freeze-pan`, `sv-freeze-pan`; sim-budget.json's all-on set on `asi.0329` (was `asi.0325`; 0328 + ForegroundRender) + `[Harness] ForegroundRender=1` + `[HitchAttr]` + `LogFallbackMs 20` / stack 16, seeded 10 min with both samplers, ~48 min. Pass: logs show `[ts3prof] publish gaps: FrameDb_PublishFrame` and `[frames] hooked`, freezes.jsonl and frames.csv not empty, the pan job's `camera pan:` line without FAILED. Timing only on a clean night.

Ledger: docs/51 §254, I-162-1 to I-162-5.

## 7. Unfocused pacing: `[Harness] ForegroundRender` (TEST ONLY)

Measured 2026-10-22 (`freeze-measure-2026-10-22-bp-freeze-pan`): the unfocused run rendered at 12.9 FPS (frame interval p50 62 ms, hooked render + Present p50 2 ms). With S3SS Precise's once-per-frame signal the sim published only 0.17 times per iteration, and 41 of 46 visible freezes >= 300 ms had no sim iteration >= 50 ms: an artifact of the unfocused window. Real players run focused.

`[Harness] ForegroundRender=1` (ts3native 0.36.24, `fgrender.cpp`, default 0, **test only: never in a release ini**, not in the repo `ts3native.ini`; queues set it per job) removes the frame loop's inactive-window sleeps at load and puts the bytes back on detach. Sites [verified, disassembly]:

| Site | Code | Patch | S3SS |
|---|---|---|---|
| `0xECA9E2` in MainLoop `0xECA960` | `je +9` over `call App_IdleIfNotForeground 0x5887A0` (`Sleep(10)` unless our process owns the foreground window). The flag is "`NoInactiveIdle` key absent" (lookup `0x58A8D0` at `0xECA976`), also skipped while the loading cap is on | `74 09` -> `EB 09`: same as the game's own `NoInactiveIdle` key | not touched by S3SS |
| `0xEC9FD1` in `Frame_RenderAndPresent 0xEC9F00` | `call 0x4E1320` (`SleepEx(30)` wrapper), reached only when device `+0x8D` is set | 5-byte nop (the `push ecx` / `add esp,4` around it stay balanced) | S3SS Precise rewrites the 9 bytes `0xEC9FBA`-`0xEC9FC2` (`cmp [esi+8Dh]` -> call to its own limiter, `je` -> `jmp`), so with S3SS this call is dead code and the patch changes nothing; S3SS's limiter, its once-per-frame signal and its inactive limit (used only while `+0x8D` is set) stay as they are |

Each site is patched only if its bytes are the original ones (another mod's bytes are left alone and logged); `hook-sites.ts --check` clean (docs/68). Selftest `TestForegroundRender` patches and reverts both on dummies.

- `+0x8D` is set for a lost device or a minimized window, not for an unfocused visible one (I-85-6, inferred; it also pauses the game clock, bit 4, which unfocused runs do not show). So in our runs the `Sleep(10)` is the inactive throttle that fires; the `0xEC9FD1` patch is for completeness (setups without S3SS). [inferred until a run]
- `Sleep(10)` alone does not explain a 62 ms p50 (at the default 15.6 ms timer it costs ~16 ms). If a ForegroundRender run still shows a p50 well above the focused rate, the rest is outside the exe's loop (Windows background timer/power throttling of the unfocused process, DXVK pacing) and needs its own check (I-162-7).
- The 10 FPS ResourceSystem cap (`0xECAA65`, docs/41) **cannot fire in play** [verified, static]: its flag is ResourceSystem `+0x1F0` (vtable `0xFFE2F0` `+0x58` getter `0x736600`, `+0x5C` setter `0x733EE0`); the only set is `push 1` at `0xD60A02` in the thumbnail preload `0xD60800`, called from `0xD61689` only when `+0x5B` is set, which `0xD585A8` does only with the `generateThumbnails` config key. Nothing clears it, so in that mode the game would stay at 10 FPS; normal play never sets it.

Ledger: docs/51 §257, I-162-6 to I-162-8.