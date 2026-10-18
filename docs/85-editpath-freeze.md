# 85. The `bp-p0-sync` game-time freeze: not a stuck `RouteFollow_EditPath`, a paused game clock

Written 2026-10-17. Static study plus a re-read of the logs of the runs named below; the game was not run. Tools: capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67) with a raw `E8`/`E9` caller scan and an immediate scan, the PE import table, `git show` of the ts3native sources at `ac2ab68` (0.11.4) and `222e5cc` (0.11.5), and the run files in `probe-out/queue` and `probe-out/profile`. All times UTC.

Tags as in AGENTS.md: **[verified]** = read in the disassembly, source or a log (address or file given); **[inferred]** = reasoning, to confirm with the §6 probe.

## 1. Summary

Question (PLAN.md "Resume here" step 4, H6 bullet): in `bp-p0-sync` (0.11.4, probe P0, async off) game time stopped and did not come back; the bisect `bp-p0-sync-noedit` (same build, `RouteFollow_EditPath 0x85F5A0` not framed) ran on, so 0.11.5 dropped the frame. What in `0x85F5A0` / `GoalFootprintGather 0x82F0B0`, our hooks or the save can cause it, and can it happen in 0.12.4?

Answer:

- **The sim thread was not stuck.** Through the whole freeze `MonoScriptHost_Simulate 0xD81840` kept iterating 100-300 times per second, ran the script tick and TaskObjects every iteration, and the agent answered commands (placed and removed objects, saved with result 0) [verified, `h6-m1-2026-10-17-bp-p0-sync-600-seconds.csv` rows 67-580 and the queue log]. What stopped is the **step path**: catch-up steps 0, ThreadedObjects 0, locomotion 0, routes 0 [verified, same rows]. No sim-stack sample exists for the run (the simstack file is empty), so nothing places the thread inside `0x85F5A0`.
- **The freeze began before the save, not after it.** Run 2 (job start 02:53) stopped stepping at about 02:56:37, 3-6 s after the `place_objects` step and 162 s before `save_game` (02:59:18) [verified: hitch timestamps, §2]. Run 1 (02:27) most likely stopped during the camera pan, before its save [inferred from the clock rate, §2].
- **The mechanism is a paused game clock.** Simulate computes 0 steps whenever its copy of `GameClock_IsPaused` is set [verified, §3]. That flag comes from the clock's pause bits, and the only pause bit that stays set with no script involvement is the **minimize pause** (bit 2): the window procedure posts message `0x1EE1008` when the game window is minimized and `0x1EE1010` when it is restored, and the simulator thread object sets or clears bit 2 on them unless the config value `SimWhileMinimized` is set [verified, §3]. The managed speed still reads "Triple" because bit 2 is native only, which is what the probe saw [verified: `set_speed` results at 03:00:18 and 03:02:01 show speed Triple and the same `minutes_total`].
- **The same freeze happened without any steering frame.** The 2026-10-16 `bp-h6-m1` job (23:36, ts3native **0.11.1**, 23 route gate sites, no `RT_STEER` sites, async A/B) shows the identical signature from about 23:46:57 to the end of its profile, 240 s [verified, `h6-m1-2026-10-16-bp-h6-m1-720-seconds.csv` rows 448-687 and `h6-m1-2026-10-16-bp-h6-m1-2336-ts3native.log`]. And `bp-p0-async` (0.11.4, the EditPath frame installed) did not freeze [verified, `h6-m1-2026-10-17-bp-p0-async-0240-ts3native.log`, seconds CSV]. So the frame is neither necessary nor sufficient: the "2 of 2 with, 0 of 1 without" bisect is too small to name it.
- **`0x85F5A0` and `0x82F0B0` cannot loop or block forever** in any way that leaves Simulate iterating: a block on `+0x918` would stop the iteration count, not the step count [verified, §4]. Our 0.11.4 frame does no waiting with `Enforce=0` and does not change registers that the callers use [verified, §5].

Verdicts per hypothesis:

| # | Hypothesis | Verdict | Evidence |
|---|---|---|---|
| H1 | The sim thread loops or waits inside `0x85F5A0` / `0x82F0B0` (`+0x918`, a loop over path nodes or footprints) | **ruled out** | Simulate kept iterating (§2); `0x82F0B0` releases `+0x918` on both exits, both functions' loops are bounded walks (§4) |
| H2 | The P0 frame corrupts the caller's state (registers, flags, x87, XMM) | **ruled out** | `0x85F5A0` is `cdecl` void with 12 stack arguments and no register inputs; both callers read no `eax`/XMM/x87 value after the call (§5) |
| H3 | A frame left behind by a `longjmp` (managed exception in a callback) makes `RgLeave` return to a stale address | **inferred unlikely** | Would jump to the wrong caller (a crash or garbage), not stop the clock cleanly; no crash, no `rgUntracked` (§5) |
| H4 | The save takes or keeps a lock, or changes world state, that stops the sim afterwards | **ruled out** for run 2 (freeze 162 s before the save); **inferred** for run 1 | §2; with `Enforce=0` the save site only counts (§5) |
| H5 | The game clock was paused by the minimize pause (bit 2) or the device pause (bit 4) | **mechanism verified; that it fired in these runs inferred** | §3; no log records the window state (the probe's window watcher skips minimized windows, `tools/probe/ts3probe.py` line 181) |
| H6 | The step accumulator `host+0xC14` became NaN or infinite (steps stay 0 for good) | **inferred possible, no source found** | §3; the §6 probe reads it |
| H7 | The EditPath frame itself causes the freeze by some path not found here | **open, weakened** | One freeze without the frame (0.11.1), one run with the frame and no freeze (`bp-p0-async`) |

**0.12.4:** the EditPath frame is gone since 0.11.5 [verified, `222e5cc`]; the steering frames on `0x82EDB0`/`0x82F0B0` are still installed and were also in the frozen runs. H5 is game behaviour on the main thread, so it can happen in 0.12.4 with async on or off whenever the window is minimized during a run [inferred]. The 0.11.5 clock guard in `profile-world.ts` turns it into a fast FAIL instead of a silent PASS.

## 2. Timeline of the frozen runs

Run 2 (`bp-p0-sync`, job 02:53, profile from 02:55:26) [verified, `h6-m1-2026-10-17-bp-p0-sync.log`, `-600-seconds.csv`, `-600-hitches.jsonl`]:

| Time | Event | Profile |
|---|---|---|
| 02:56:00 | `set_speed` ultra: clock 08:45 (`minutes_total` 20685.7) | steps 10 per iteration |
| 02:56:28-02:56:31 | `place_objects` | 3.3 s hitch at profile second 63.8, steps 10 |
| 02:56:36 | last iteration with steps (hitch at 69.3 s) | second 66: 104 iterations, ThreadedObjects 240 ms |
| from 02:56:37 | | second 67 on: 110-320 iterations/s, steps 0, ThreadedObjects 0, locomotion 0, routes 0, TaskObjects 80-350 ms/s, script tick 4-9 ms/s, publish 5-50 ms/s |
| 02:57:23 | `buy_open` | 656 ms hitch at 114.5 s, steps 0 |
| 02:59:18 | `save_game`, save result 0, 12.4 s | 12.46 s iteration at 243.7 s, steps 0 |
| 03:00:18 | `set_speed` ultra: clock 13:20 (20960.8), speed Triple | steps 0 |
| 03:02:01 | `set_speed` normal: still 20960.8 | steps 0 |
| 03:05:37 | 47 "stuck" Sims listed; run PASS | |

Check of the clock arithmetic: 20685.7 to 20960.8 is 275 game minutes; ultra speed ran 6.2-7.0 game minutes per second in the `noedit` job (03:08:21 to 03:12:39: 1602 minutes in 258 s, or in 228 s without the buy-mode and save pauses), so 275 minutes take 39-44 s: 02:56:00 + 40 s = 02:56:40, matching the profile [verified numbers, rate inferred].

Run 1 (`bp-p0-sync`, job 02:27; its profile files were overwritten by run 2): 07:52 at 02:30:31, 22:49 (897 minutes later) at 02:34:50 and still 22:49 at 02:36:32. At 6.2-7 minutes/s, 897 minutes are 128-145 s of running time; with the 15 s buy-mode pause that ends between 02:32:54 and 02:33:11, during the camera pan and before `save_game` at 02:33:50 [inferred].

Run of 2026-10-16 (`bp-h6-m1`, job 23:36, 0.11.1, `--ab routeasync`): the save is the 15.7 s iteration at profile second 226 (`save_game` 23:42:59); steps stop at second 448, about 23:46:57, after the last repro step (23:45:41) and stay 0 for the remaining 240 rows [verified rows, wall time inferred from the save anchor]. Nobody looked at it because the clock guard did not exist yet.

Not related: the NRaas ErrorTrap "Destroyed Object Found" reports at 02:31:56 and 02:56:47 come after the freeze in run 2 and also appear in the `noedit` job, which did not freeze [verified, `probe-out/<session>/events.jsonl`].

## 3. Why Simulate stops stepping

`MonoScriptHost_Simulate 0xD81840` (docs/36 §3.1) [verified disassembly]:

1. `0x769420` (`0xD81A41`) copies the time that the main thread posted into the host (`host+0xAA8`, written by the host method `0x7693E0` through `0x76C3F0` under the lock at `host+0xA78`) to `host+0xA64`. `0x76C3F0` adds the clock's game dt (`clock+0x64`) to `+4`, two real-time fields to `+8`/`+0xC`, and stores `GameClock_IsPaused 0x594540` in `+0x10`.
2. `0x76C5C0` (`0xD81A81`) takes a frame-ring write slot (`SimRender_TripleBufferSync 0x593590`). It returns 0 only in fixed-step mode with a full ring (`+0xFC`, video capture only, docs/72 §2), which these runs never use, so the step branch at `0xD81B23` is taken [verified code, capture mode off inferred].
3. Step branch: `host+0xC14` += game dt (`0xD81B25`-`0xD81B32`). Then `0x8A6D10` (`0xD81B38`) returns the paused copy `host+0xA64+0x10`; **if it is set, the step count stays 0** (the jump at `0xD81B3F` skips the count). Otherwise steps = truncate(`host+0xC14` x 30) (`0xD81B41`-`0xD81B51`, constant `0x10049E4` = 30.0), capped at 10, the backlog reset to 0.667 above 19. With 0 steps (`0xD81B9D`) the iteration skips locomotion, animation, `ServiceList_SimUpdate`, OneShot and ThreadedObjects and goes on to publish, the remote-call pump and pacing. That is the measured signature.

`GameClock_IsPaused 0x594540`: true when the pause bits at `0x11CDD48+0x40` are not 0 or the speed `+0x68` is within 1.2e-7 of 0 [verified]. `GameClock_SetPauseFlags 0x594300` also zeroes the game dt `+0x64` [verified]. The bit setters (direct calls only, raw caller scan):

| Bit | Set | Cleared | Trigger |
|---|---|---|---|
| 1 | `IC_GameUtils_PauseImpl 0xEC1240`; `0x76B384` (modal pump on the main thread, cleared at `0x76B3AE` when it ends) | `IC_GameUtils_UnpauseImpl 0xEC1250` | Script pause; managed `GameSpeed` would no longer read "Triple" [inferred] |
| 2 | `0x76B459` | `0x76B442` | Simulator thread object message handler `0x76B410`: message `0x1EE1008` sets bit 2 unless the object's byte `+0x2C` is set; `0x1EE1010` clears it. The byte is read by `0x76B3C0` from the config value `SimWhileMinimized` (UTF-16 key at `0x1007C50`, lookup `0x58AAD0`) |
| 4 | `MainLoop 0xECAA22` | `0xECAA39` | Per frame while device `+0x8D` is set (docs/41 §2.1); cleared on the next frame without it |

Who sends `0x1EE1008` and `0x1EE1010` (immediate scan of `.text`; the other hits are listener registrations through vtable `+0x24`/`+0x2C`, `0x76B2FF`, `0x76B7B9`, `0x864FE3`, `0x8675DB`, and the handlers `0x76B414`, `0x866906`) [verified]:

- `0x411230` (called from the window procedure at `0x411A15` with the low and high words of `lParam`): first argument 0 posts `0x1EE1010` (`0x4112A2`), 1 posts `0x1EE1008` (`0x411285`), 2 only sets a flag. These are the three `WM_SIZE` codes: restored, minimized, maximized [verified branches; the message being `WM_SIZE` inferred from the width/height arguments].
- `0x4113C0` (activation): posts `0x1EE1006`/`0x1EE1007`, then calls `IsIconic` (import `0xF958F4`); if the window is iconic it posts `0x1EE1008`, else on activation `0x1EE1010` [verified].

So with `SimWhileMinimized` unset, **minimizing the game window pauses the sim until the window is restored or activated**, and nothing in our harness restores it (the queue keeps the window unfocused and the probe only moves it: `tools/harness/focus-game.ts`, `tools/probe/ts3probe.py`). `Win+D` / "show desktop" minimizes it too [inferred].

H6: `host+0xC14` only grows by the posted game dt and shrinks by 1/30 per step (`0xD81D4E`-`0xD81D5C`); a NaN or infinity there would also give 0 steps for good (the truncation gives `0x80000000`, below 1). No source of a non-finite dt was found [inferred]; the §6 probe reads the value.

## 4. `RouteFollow_EditPath 0x85F5A0` and `GoalFootprintGather 0x82F0B0`: what could block

`0x85F5A0` [verified disassembly `0x85F5A0`-`0x85FD0B`]: `cdecl`, 12 stack arguments (`[ebp+8]`-`[ebp+0x34]`), returns at `0x85F97F` with no result. No lock of its own. Calls `0x82F0B0` once (`0x85F6C6`), then: a loop over the gathered footprint vector (`0x85F7D0`-`0x85F941`, step 4, ends at the vector end), a linear scan of the `[ebp+0x14]` pair list (`0x85F8A1`-`0x85F8BB`), a scan of the `[ebp+0x24]` list (`0x85F8D3`-`0x85F908`), and an outer loop over 16-byte entries (`0x85FAA0`-`0x85FC2B`) with an inner loop bounded by the entry's point count (`0x85FAE0`-`0x85FB2C`). All bounds are vector ends or counts read once; nothing waits [verified]. Virtual calls on the footprint objects (`0x85F7E9`, `0x85F992`, `0x85FA43`, `0x85FA79`, `0x85FBAD`) are the docs/82 agent-vtable paths; docs/82 found no wait in them [verified there].

`0x82F0B0` [verified disassembly `0x82F0B0`-`0x82F469`]: `Lock_Acquire 0x4E16F0` on world `+0x918` with the INFINITE deadline (`0x1021CF0` = `0xFFFFFFFF`) at `0x82F0D0`; released at `0x82F191` (early exit, returns 0) and `0x82F45C` (returns 1). One loop: a container walk with the successor `0x4E0B20` until the end node (`0x82F337`-`0x82F3DD`).

In sync mode with `Enforce=0` no worker exists and nothing else holds `+0x918` for long, and a wait there would stop the iteration count. A loop that never ends would do the same. Both are ruled out by the 100-300 iterations per second during the freeze.

## 5. Our 0.11.4 frame on `0x85F5A0`

Source at `ac2ab68` (`native/ts3native/rtsites.cpp`, `routegate.cpp`) [verified]:

- Entry stub: `pushad`/`pushfd`, `RtEnter(25, slot)`, restore, jump to the trampoline. For `RT_STEER`, `RtEnter` only does `RgDrop`, sets `t_steerRet` to the caller (outermost only) and pushes a frame that swaps the return address for `RgLeaveThunk`. It waits only in the `RT_DESTROY`, `RT_CLOSING`, save/BRD/lot-save and gate paths, and all of them need `rgEnforce`, which was 0 (`route gate closures off (Enforce=0)` in both 0.11.4 logs).
- Leave thunk: restores `eax`, `edx`, flags and the x87 stack, not XMM. `0x85F5A0` takes no XMM input (its first XMM use writes the register, `0x85F5AC`), returns nothing, and neither caller reads a register after the call: `0x85FEDC` reloads `ecx` from the stack, `0x85FFC7` increments its own counter [verified].
- `t_steerRet` only changes the key that `LockAcquireHook` and `G11Touch` record; it changes no lock behaviour [verified, `ts3native.cpp` lines 1320-1324 at `ac2ab68`].
- A frame skipped by a `longjmp` (managed exception through a callback) stays on `t_rg` until the next `RgDrop` at a higher slot. If `0x85F5A0` returned first, `RgLeave` would pop the stale frame and return into the wrong caller, a crash rather than a stopped clock [inferred]. No crash was logged and `rgUntracked` stayed 0 [verified, `-native.json`].

Nothing in this path can set a GameClock pause bit or reach `0x594300` (no direct caller in the routing code, §3) [verified caller scan; indirect calls through managed `GameUtils.Pause` inferred absent, since managed speed still read "Triple"].

The counters agree with a stopped clock rather than a stuck call: the frozen job ran 1,554 plans in the window against 18,341 in `noedit` [verified, `g11Plans` after minus before], because Sims do not route while no steps run.

## 6. Probe and repro to confirm H5 (short, no long run)

**P-clock (observe only, in the profiler and the agent):**

- `stutter-profile.js`: hook `0x8A6D10` on the sim thread (called with `ecx = host+0xA64`, at `0xD81B38` and `0xD81E03`); per second record the returned paused byte and `host+0xC14` (`ecx+0x1B0`). New CSV columns `simPaused`, `stepAcc`.
- Agent command `clock_state` (read-only): `[0x11CDD48+0x40]` pause bits, `+0x64` game dt, `+0x68` speed, `+0x74` scale, `IsIconic` and foreground state of the game window, managed `GameSpeed`.
- `ts3probe.py` window watcher: log "game window minimized" / "restored" with UTC when `IsIconic` changes (today it skips iconic windows silently).
- `profile-world.ts` clock guard: on a trip, call `clock_state` once and write it to the run log before failing.

Expected if H5: at the next freeze, bits contain 2 (or 4), the window is iconic, `simPaused` = 1 from the freeze second on, `stepAcc` finite. If bits are 0 and `stepAcc` is NaN or infinite: H6. If bits are 0, the window is normal and `stepAcc` finite: back to H7, rerun `bp-p0-sync` with the frame and P-clock.

**Repro `clock-minimize-pause` (repros.json style, about 90 s, any world):**

| Step | Action | Wait | Expected |
|---|---|---|---|
| 1 | `set_speed` ultra | 10 s | clock moves |
| 2 | `clock_state` | - | bits 0, not iconic |
| 3 | `window_minimize` (new: `ShowWindow(h, SW_SHOWMINNOACTIVE)`, no focus) | 20 s | |
| 4 | `clock_state`, `time` | - | bit 2 set, iconic, `minutes_total` unchanged over 10 s |
| 5 | `window_restore` (new: `ShowWindow(h, SW_SHOWNOACTIVATE)`, no focus; sends the restored `WM_SIZE`) | 20 s | |
| 6 | `clock_state`, `time` | - | bit 2 clear, clock moves again |

The two window actions neither focus nor move the window, but they touch it, so they need Rodney's OK under the game-window rule. Run it on 0.12.4 as is (no ts3native change needed beyond P-clock). Expected if H5: steps 3-6 as in the table; that is enough to retire H7 for the two 0.11.4 runs together with the 0.11.1 occurrence.

**Built (2026-10-17, offline only; not run in game yet):**

- `stutter-profile.js`: `0x8A6D10` is `mov al, [ecx+0x10]; ret` (4 bytes, `thiscall`, no stack arguments) [verified disassembly], too short for a Frida detour (5 bytes), so the profiler hooks its call site `0xD81B38` (`mov ecx, edi; call 0x8A6D10; test al, al`, `edi = esi+0xA64` from `0xD81A46`, no other `edi` write before the call) and reads the same byte and `[ecx+0x1B0]` (= `host+0xC14`, the `fstp` at `0xD81B32`) [verified]. The second call at `0xD81E03` is not hooked: `edi` is reloaded at `0xD81C13` (`esi+0xC18`) on the way, so its `ecx` is not established [verified that the reload exists; the path not followed]. Columns `simPaused` (1 if any iteration of the second saw the paused copy set) and `stepAcc` (value at the last iteration; -1 = the step branch did not run that second; NaN/Infinity = H6), appended after `lotSetActive` (before `t`, as for earlier columns; every reader goes by the header). Byte check on the call site and the getter; a mismatch turns only P-clock off. `0xD81B38` is not in any docs/68 window.
- Agent `clock_state` (`ControlHandlers.cs`): `pause_bits` and `script_pause` / `minimize_pause` / `device_pause` (masks 1/2/4 of `0x11CDD48+0x40`; the setters pass `ecx = 0x11CDD48`, e.g. `0x76B454`, `0xEC1242` [verified]), `native_paused` (the `0x594540` rule), `game_dt` `+0x64`, `speed` `+0x68`, `scale` `+0x74`, managed `game_speed`, `minutes_total` and `minutes_moved` (game minutes since the previous `clock_state` call). Plain aligned dword reads, no clock lock.
- Window state and actions live in the probe server (`ts3probe.py`), because the game's Mono resolves `DllImport` through its internal-call table (docs/06, [inferred]): the `/agent` endpoint adds `window: {found, iconic, foreground}` to every `clock_state` reply (`foreground` = the foreground window belongs to TS3W.exe), and answers `window_minimize` / `window_restore` itself with `ShowWindowAsync(h, SW_SHOWMINNOACTIVE)` / `ShowWindowAsync(h, SW_SHOWNOACTIVATE)` (posted, so a busy game thread cannot block the probe). Each acts only when it changes the state (`SW_SHOWNOACTIVATE` on a window that is not minimized would un-maximize it). The window watcher logs `game window minimized` / `restored` events (UTC) to `events.jsonl`.
- `profile-world.ts` clock guard: on a trip, one `clock_state` call, logged as `clock_state at the trip: {...}` before the FAIL exit.
- Repro `clock-minimize-pause` (`repros.json`, save `Baseline-BP-Spring`): the §6 table as `until` checks on `clock_state` (`minimize_pause`, `window.iconic`, `minutes_moved` >= 1 running, <= 0 over 10 s minimized); a failed check is a FAIL with the last reply. `cleanup: [window_restore]` (new in `repro.ts`) runs before any FAIL exit so the game is never left minimized. Queue job `bp-clock-minimize-pause` at the end of `queues/clean-night.json` (tested asi, 15 min timeout).

Not verifiable offline: that Frida attaches at `0xD81B38` on the live exe, the clock field values, that the agent mailbox keeps answering while minimized (it did during the frozen runs, §1), and the H5 verdict itself.

### 6.1 Result (2026-10-17 18:18 PHT, `clean-night` job `bp-clock-minimize-pause`, PASS, laptop locked)

Mechanism confirmed live [verified]: before minimizing, `pause_bits` 0 and the clock moved ~51 game minutes in 5 s at Triple; after `window_minimize`, `pause_bits` 2 (`minimize_pause` true) and the clock froze (0 minutes in 10 s) while the speed still read Triple; after `window_restore`, bit 2 cleared and the clock moved again (~100 minutes in 10 s). So a minimized window stops game time exactly as the frozen runs showed. That the earlier frozen runs were minimized is still inferred (nothing logged the window then; the probe logs it now). The whole run had the Windows session locked: locking does not set any pause bit (bit 4 stayed clear), so unattended runs may run locked.

## 7. Fix design

The cause is not ours if §6 confirms H5; no ts3native change. Harness side:

1. Keep the 0.11.5 clock guard and add the `clock_state` dump on a trip (§6).
2. Log window minimize/restore in `ts3probe.py` so every future freeze has its window state in the run log.
3. Option for test runs only: set `SimWhileMinimized` so a minimized window keeps simulating. Its config source (the object `[0x114FAB0]` read by `0x58AAD0`) is not mapped yet; find which ini or command-line key feeds it before relying on it [inferred]. Do not put it in the player install: pausing while minimized is the game's intended behaviour.
4. The "do not frame `0x85F5A0` again" comment in `rtsites.cpp` can stay until P-clock has explained one freeze; after that the frame is safe to reuse for a probe [inferred].

If §6 shows bits 0 and a finite accumulator (H7), the next step is a sync run with the frame plus P-clock and the `[Crash] Log=1` logger, and a `RgLeave` assertion that the popped frame's slot equals the thunk's own return slot (catches H3) before any other change.

## 8. Open items

- Which config source feeds `SimWhileMinimized` (`0x58AAD0`, `[0x114FAB0]`).
- What sets device `+0x8D` (bit 4) besides a lost device; docs/41 lists "lost or not foreground", which would pause every unfocused run, and those run on, so the "not foreground" part needs a check [inferred].
- Why `bp-p0-async` and several later CSVs show no buy-mode steps-0 seconds while `noedit` and the g11 runs do (14 s each); not needed for this question.
