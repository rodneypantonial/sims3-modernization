# 55. UI input and event dispatch (M-UI4)

Static map of TS3W.exe (Steam 1.67) on a Ghidra copy with our symbols applied. No game runs. Costs come only from existing samples: `probe-out/profile/*simstack*.jsonl` (36 files, 10,251 sim samples, mixed configs) and the two main-thread files of the 2026-10-15 verify run (`verify-2026-10-15-{bp,ip}-verify-mainstack.jsonl`, 2,991 main samples). Leaf method as in docs/47 §5: a sample counts for a block when its leaf-most TS3W frame lies in the block; "on stack" counts any TS3W frame in the block (stale return addresses inflate it, docs/45).

Tags: **[verified]** = read in a decompile, the disassembly, the call graph or the exe's strings; **[measured]** = from the sample files named above; **[inferred]** = reasoning, still to confirm.

Builds on docs/41 (main loop), docs/43 (UI internal calls), docs/47 (window manager, lock `0xFBA5CC`).

## 1. Summary

- **Plain Win32 messages, no raw input, no DirectInput.** The exe imports no `RegisterRawInputDevices`/`GetRawInputData` and has no `dinput` string; the window procedure `Canvas_WndProc 0x4118C0` has no `WM_INPUT` case. Mouse and keyboard come from `WM_MOUSEMOVE`, `WM_*BUTTON*`, `WM_MOUSEWHEEL`, `WM_KEY*`, `WM_CHAR` and IME messages [verified]. Modifier keys are read with `GetAsyncKeyState` on every mouse event [verified].
- **XInput is loaded at run time** (`XInput_LoadLibrary 0x49C350`: `xinput1_3`, `xinput1_1`, `xinput9_1_0`) and polled by `InputMan_Update 0x49D040` on the main thread: every connected pad each frame, plus **one disconnected port every 250 ms** [verified]. `XInputGetState` on an empty port is known to be slow on some systems (up to milliseconds) [community]; here it is at most 4 calls per second, so at worst a small periodic main-thread hitch.
- **The whole input path up to the window manager runs on the main thread** [verified chain, §3]: `MainLoop` -> `ServiceList_Update` -> `CanvasService_Update 0x588A00` -> `Canvas_PumpMessages 0x410890` (PeekMessageW loop) -> `DispatchMessageW` -> `Canvas_WndProc` -> `Canvas_OnMouseMove 0x410D40` etc. -> `Canvas_DispatchEvent 0x410570` -> InputMan handlers -> `UIInput_ToWindowMessage 0x5989C0` -> `WindowManager_PostMessageNoTarget` (`+0x38`) under lock `0xFBA5CC`. This confirms ledger item I-47-3 statically.
- **Input latency: up to one sim tick plus one render frame.** The main thread only queues the UTFWin message. The sim thread handles it in the next `WindowManager_ProcessMessages` (inside `UIService_SimUpdate 0x7F1530`), where hit tests, focus/capture routing and the managed (C#) window callbacks run; the visual result is published by `UpdateVisuals` and drawn on the next main frame after that [verified threads; timing inferred]. When the sim tick is slow (heavy lots, GC), UI clicks and hover wait for it.
- **`0x598CC0` is `InputService_SetMouseFrozen`, and it does not touch the window tree.** It touches the input service's freeze state (under its own lock `0xFD9674`), four plain window-manager fields (`+0xCC/+0xD0` read, `+0xE4/+0xE5` byte stores), the cursor manager (`CursorManager_SetOverride 0x5057F0`, no lock) and Win32 (`ClipCursor`, `GetClientRect`, `PostMessageA`) [verified]. The "window slots +0x84/+0x9C/+0x40" of docs/47 §4 are on the **canvas** (OS window object `[0x11CDBD0]`), not on UTFWin windows. **Verdict: no race with the sim-owned window tree.** One small unlocked race remains on the cursor manager (§5, R-CUR) if the camera callers run on the main thread.
- **Cost:** input code is ~0% of the sim thread (0 leaf samples in all input blocks) [measured]. On the main thread the message pump is 0.57% on stack, 0.43% leaf (2,991 samples), the pad poll 0.03% [measured]. Not a cost problem; the interest is latency and correctness.

## 2. Functions

| Address | Name | Thread | What it does |
|---|---|---|---|
| `0x412070` | `Canvas_ctor` | main (startup) | EA canvas = the game window object, vtable `0xF9A378`, global `[0x11CDBD0]` (the same object `Canvas_InitDevice` uses) [verified] |
| `0x588980`, `0x588A00` | `CanvasService_ctor`, `CanvasService_Update` | main | Service `0x175D9C5`, vtable `0xFCBD34`; slot `+0x1C` calls canvas `+0x8C` (pump); on `WM_QUIT` calls `0x588770(1)` [verified disassembly] |
| `0x410890` | `Canvas_PumpMessages` | main | `PeekMessageW(PM_REMOVE)` loop over all windows of the thread; each message first goes to the canvas dispatcher as event `0x1EE100B`, then `TranslateMessage`/`DispatchMessageW` unless a listener consumed it [verified] |
| `0x4118C0` | `Canvas_WndProc` | main | Window procedure (registered at `0x412138`). Raw pre-dispatch event `0x1EE100C` to the canvas dispatcher, then per message: keys `0x100-0x105`, mouse `0x200-0x20A`, `WM_SETCURSOR` (cursor `+0x78`), activate, size, timer, menu-loop and DPI-style cases (`0x231`, `0x232`, `0x2A3`); IME through the `Imm*` helpers `0x412780-0x4134E0` [verified imports; their message cases not decoded]; private `0x500` (SetCursor) and `0x501` (`SetCursorPos` after `ClientToScreen`, only when foreground) [verified] |
| `0x410D40` | `Canvas_OnMouseMove` | main | Event type 7 with absolute and delta position (last position `+0x14/+0x18`), modifier bits (shift/ctrl from wParam, alt and Win keys via `GetAsyncKeyState`) [verified] |
| `0x411070` / `0x411130` | `Canvas_OnLButtonDown` / `Canvas_OnLButtonUp` | main | `SetCapture` / `ReleaseCapture`, then button event; same pattern for the right (`0x4110F0`/`0x411190`) and middle (`0x4110B0`/`0x411160`) buttons [verified imports] |
| `0x410FB0` | `Canvas_SendMouseButtonEvent` | main | Button event builder (type 5) [verified] |
| `0x410E90` | `Canvas_OnMouseWheel` | main | Type 6; calls `SystemParametersInfoA(SPI_GETWHEELSCROLLLINES)` **on every wheel event** [verified] |
| `0x410B80`, `0x410C30`, `0x410CE0` | `Canvas_OnKeyDown`, `Canvas_OnKeyUp`, `Canvas_OnChar` | main | Keyboard events; key state via `GetKeyState` [verified] |
| `0x410290` | `Canvas_TrackMouseLeave` | main | `TrackMouseEvent` at the start of each mouse handler [verified string] |
| `0x410570` | `Canvas_DispatchEvent` | main | Event `0x1EE1001` (input) goes to listener `+0x60` slot `+0x38`; other events to dispatcher `+0x5C` slot `+0x18`, else callback `+0x64` [verified] |
| `0x410620` | `Canvas_SetCursorPosAsync` | any | Canvas `+0x84`: `PostMessageA(hwnd, 0x501, 0, x | y<<16)`; the move itself runs in the WndProc on main [verified] |
| `0x598C60`, `0x5987E0` | `InputService_Create`, `InputService_ctor` | main (startup) | `Services/Input/Service`, id `0x175EC4F`, vtable `0xFD9690`, singleton `[0x11CE2C0]`; mouse freeze state `+0x48..+0x61` guarded by `0xFD9674` [verified] |
| `0x598FF0` | `InputService_Init` | main (startup) | Creates `Services/Input/InputMan` (`InputMan_ctor 0x49CEB0`) and `Services/Input/TriggerDispatcher` (`0x49DAC0`); registers `UIInput_ToWindowMessage` as handler 6 (`InputMan_AddHandler`, vtable `+0x34`) [verified] |
| `0x598660` | `InputService_Update` | main | Service `+0x1C`: `InputMan_Update` [verified] |
| `0x49CEB0`, `0x49BA60` | `InputMan_ctor`, `InputMan_Init` | main | Vtable `0xFB0E88`; 4 pad objects (`0xFB0ED4`, 0xC0 bytes each), mouse device `+0x340` (`0xFB0D9C`), keyboard `+0x350` (`0xFB0DD0`); init probes the 4 XInput ports [verified] |
| `0x49D040` | `InputMan_Update` | main | Polls connected pads (`InputMan_PollPad 0x49C5F0` -> `XInputGetState` via `[0x11C78A0]`), one disconnected port every 250 ms, then callback `+0x18` [verified] |
| `0x5989C0` | `UIInput_ToWindowMessage` | main | Handler 6: input event type 1/2 (key, char; chars above 0x7F become 0x20 when `[0x11CE2C4]` is set), 5 (button), 6 (wheel), 7 (move) to a UTFWin message, posted through WM `+0x38`. Button, wheel and move also call `0x6DEFF0` (renderer pick position) when the renderer has `+0x38`. While the mouse is frozen, a move warps the cursor back (canvas `+0x84`) [verified] |
| `0x598750`, `0x5987C0`, `0x5986C0`, `0x5985B0` | `CanvasMsgHandler_Create`, `_Get`, `_OnRawMessage`, `_SetSwallowMouseMove` | main | Listener for the raw event `0x1EE100C`: swallows `WM_MOUSEMOVE` while its flag is set; turns `0x501` into a private `0x502` posted after it, and `0x502` sets the flag back (so the synthetic move from a cursor warp is not fed back as input). On set, drains queued `WM_MOUSEMOVE`; on clear, posts UTFWin message `0x23` [verified] |
| `0x598CC0` | `InputService_SetMouseFrozen` | caller (sim or main, §5) | Freeze: read the UI cursor position (WM `+0x48`), clip the cursor to the client rect (when windowed check `0x60E800` is true), warp it to the centre, post `0x502`, set WM `+0x94` (byte `+0xE4`), set the override cursor; unfreeze: warp back, `ClipCursor(NULL)`, clear the override. Then stores the state under `0xFD9674` and WM `+0x40` (byte `+0xE5` = not frozen) [verified] |
| `0x598F90` | `InputService_SetMouseFrozenIfAny` | caller | Null check on `[0x11CE2C0]`; callers `IC_UIManager_FreezeMouseImpl 0x779640` and the camera controllers `0x9670F0`, `0x96EBE0`, `0x970200`, `0x9757A0`, `0x975EE0`, `0x971670`, `0x97C320` (vtables `0x1044EC8`-`0x1045F0C`) [verified call graph] |
| `0x505620`, `0x5057F0`, `0x5056D0` | `CursorManager_Apply`, `CursorManager_SetOverride`, `CursorManager_GetState` | caller | Cursor manager `[0x11CC9A4]`: override id `+0x6C` else default `+0x68`; walks the provider list `+0x14..+0x18` (ImageCursorProvider etc.) without a lock [verified] |
| `0x50ADC0`, `0x50B8A0`, `0x50B8B0`, `0x50B8C0` | `WindowManager_GetCursorPos` (`+0x48`), `_GetFlagE5` (`+0x3C`), `_SetFlagE5` (`+0x40`), `_SetFlagE4` (`+0x94`) | caller | Plain field reads/stores, no lock [verified disassembly] |
| `0xECA350` | `App_LoadPumpCallback` | caller (world load) | Registered by `WorldLoad_SetProgressCallback 0xC5FB40` and called from `WorldLoad_ProgressTick 0xC5FB60` (world/lot load, `IC_World_LoadTick`, `CASPartCacheService_Init`): at most once per stopwatch period it runs WM `ProcessMessages` and `UpdateVisuals` **on the calling thread**, then the canvas pump and `WndMsg_Render` only when that thread is main [verified] |

Clipboard (`0x4E9B80`/`0x4E9CB0`, a hidden `RegisterClassA` window with `SetClipboardViewer`) and the clipboard pump `UTFWinService_Update 0x723EE0` (docs/47) are not part of the input path.

## 3. Input path, threads and latency

| Step | Function | Thread | Lock |
|---|---|---|---|
| 1. Message pump, once per main-loop iteration (step 4 of docs/41 §3, inside `ServiceList_Update`) | `CanvasService_Update` -> `Canvas_PumpMessages` | main [verified] | none |
| 2. Pre-dispatch listeners (raw event `0x1EE100C`, mouse-move swallow) | `CanvasMsgHandler_OnRawMessage` | main | none |
| 3. Window procedure, per-message handler builds an input event (`0x1EE1001`) | `Canvas_WndProc`, `Canvas_OnMouseMove`, ... | main | none |
| 4. Event to InputMan, handler list (handler 6 = UI) | `Canvas_DispatchEvent` -> listener `+0x60` slot `+0x38` (InputMan mouse/keyboard device [inferred: the jump table was not decoded]) | main | none |
| 5. Pads, once per iteration (separate service) | `InputService_Update` -> `InputMan_Update` | main [verified] | none |
| 6. UTFWin message queued | `UIInput_ToWindowMessage` -> WM `+0x38` -> `WindowManager_PostMessage 0x50D660` | main | `0xFBA5CC` (short) |
| 7. Message pump of the window manager: hit test, focus/capture routing, window procs, managed callbacks | `UIService_SimUpdate 0x7F1530` -> `WindowManager_ProcessMessages 0x50D700` -> `WindowManager_HandleSystemMessage 0x50BE70` (`Window_HitTest 0x50AF60`) -> `Window_SendMessage 0x50B4C0` | sim [verified docs/47] | `0xFBA5CC` around the queue swap only |
| 8. Script sees the event | managed window procs and `RegisterEvent` handlers (docs/43 group 2), reached through the script-host bridge `0xD7F8B0` -> `Window_SendMessage` (docs/47 §5) | sim [verified IC thread; the bridge role is inferred] | none |
| 9. Visual result | `WindowManager_UpdateVisuals 0x50DE70` publish | sim | `0xFBA5CC` |
| 10. Drawn | `WindowManager_Draw 0x50B270` from `Frame_RenderAndPresent` | main | `0xFBA5CC` |

**Latency** [inferred from the thread split]: an OS input event waits for (a) the next main-loop iteration's service pass (up to one frame, the message stays in the OS queue during the previous frame's render and Present), (b) the next sim tick's `UIService_SimUpdate` (up to one sim tick: ~33 ms at a normal tick, much longer during sim spikes), (c) the next main frame after the publish. So click-to-visual is about 1 frame + 1 sim tick + 1 frame. The game camera is separate: camera controllers handle input on the main thread (camera code is on 0.47% of main stacks, `0x96A320`, `0x96FD10`, `0x962300` [measured]) and send `Misc/Camera/SimThreadCameraMessage` to the sim (docs/36) [inferred], so camera motion does not wait for the sim tick.

**Coalescing:** none on the native side. Every `WM_MOUSEMOVE` becomes one posted UTFWin message (7 dwords, recycled through a free list capped at 16, docs/47) [verified]. A burst of mouse moves while the sim tick is long queues one message per OS move; all are dispatched (and hit-tested) in the next pump [inferred: no merge of consecutive moves was found in `ProcessMessages`].

**Focus and capture:** OS capture follows the left/right/middle button (`SetCapture`/`ReleaseCapture` in the WndProc, main). UTFWin focus and capture (IC `SetFocus`, `SetCaptureTarget`, `ReleaseCapture`, `GetFocus`, docs/43) live in the window manager and are applied during the sim-side pump; the 10 deferred slots `+0x6B0` are the likely focus/capture targets (docs/47) [inferred]. There is no cross-thread focus state: the main thread never reads UTFWin focus [verified: no main-thread reader of the WM found besides `+0x30`, `+0x38` and the freeze fields below].

## 4. Event dispatch into C#

- Script registers per-window event handlers with `IC_UIManager_RegisterEvent 0x77B150` / `DeRegisterEvent` / `SetUICallbacks 0x779750` (docs/43), all on the sim thread [verified docs/43].
- The handlers run when `ProcessMessages` delivers a message to a window whose proc list (`+0x218`, message-type mask, docs/47) holds a script-side proc; that proc calls into Mono through the script host (the `0xD7F8B0` block in docs/47's stacks) [inferred]. Synchronous `Window_SendMessage` calls from managed code go the other way (UI ICs).
- The callback message `0x74D2E9` in `HandleSystemMessage` calls a stored function pointer on the sim thread; it is the hook for "run this on the UI pump" [verified id, docs/47].
- Tool input for build/buy goes through `IC_UserToolUtils_ForwardUserToolMouseEventImpl` (string at `0x1081708`) [verified string; path not traced].
- Cost: managed UI dispatch is in the managed sampler (UI ICs 1.97% managed self, `TooltipManager.Simulate` 0.52%, docs/43), native UTFWin 1.19% leaf (docs/47). Nothing new here.

## 5. The `0x598CC0` question and races

Callers of `InputService_SetMouseFrozen` (through `0x598F90`):
- `IC_UIManager_FreezeMouseImpl 0x779640`: **sim** [verified: UI ICs run on the sim thread, docs/43].
- Seven camera controller methods `0x96xxxx`-`0x97xxxx` (enable/disable mouse look, e.g. `0x971670(bool)` freezes on true and unfreezes on false): **main** [inferred: the camera controllers handle input on main, camera code is on main stacks; none of these seven functions is in any sample]. Ledger item I-47-7 (`roles:598F90`, verify-pan) settles it at run time.

What the function touches, and whether another thread touches it too:

| State | Access in `0x598CC0` | Other side | Verdict |
|---|---|---|---|
| Input service freeze state `+0x48..+0x61` | write under `0xFD9674` | main (`UIInput_ToWindowMessage` reads under `0xFD9674`) | safe [verified] |
| WM `+0xCC/+0xD0` (UI cursor position) | read (WM `+0x48`) | written by the sim pump on mouse moves [inferred] | torn read of two floats at worst; harmless |
| WM `+0xE4`, `+0xE5` | byte stores | read by the sim pump [inferred] | benign flag race |
| UTFWin window tree, windows, display lists | **none** | sim | **no race** [verified: no window object is read or written] |
| Cursor manager `[0x11CC9A4]` (`+0x6C` override, provider list walk with slot calls) | `CursorManager_SetOverride` -> `CursorManager_Apply` | sim: `IC_UIManager_SetOverrideCursorImpl`, `RegisterWindowCursor`, `InvalidateCursor` (docs/43); the camera `0x96C690` also calls `SetOverride` | **R-CUR [inferred]:** unlocked on both threads. Two threads in `Apply` at once can leave the wrong cursor active (cosmetic). It is a crash risk only if the sim adds or removes a cursor provider (vector realloc) while main walks the list; provider registration is rare (UI load) |
| Canvas / Win32 (`ClipCursor`, `GetClientRect`, `PostMessageA`) | calls | - | thread-safe Win32; the cursor warp is posted, so it runs on main [verified] |

**Verdict:** "the sim thread owns the window tree" holds. `0x598CC0` does not touch the tree, so there is no main-vs-sim race on windows through it. The only shared unlocked state it reaches is the cursor manager (R-CUR, cosmetic unless providers change concurrently) and a few window-manager flag bytes.

**Second finding, `App_LoadPumpCallback 0xECA350` (R-LOAD):** during world and lot loads, the load progress tick runs WM `ProcessMessages` and `UpdateVisuals` on whatever thread is loading. On the sim thread (`IC_World_LoadTick`) that is the owner thread and is fine. If a load path reaches the tick on the main thread while the sim also runs `UIService_SimUpdate`, both threads would pump the same window tree [inferred risk]. `CASPartCacheService_Init` (one caller) is a startup service init, probably before the sim thread runs UI; `Lot_FinishLoad` callers were not traced to a thread. Probe: count `0xECA350` entries by thread during a world load.

## 6. Cost (existing samples)

| Block | Sim leaf | Sim on stack | Main leaf | Main on stack |
|---|---|---|---|---|
| Canvas + WndProc `0x40F000-0x413800` | 0 | 17 (0.17%, stale) | 13 (0.43%) | 24 (0.80%) |
| `Canvas_PumpMessages` | 0 | 0 | - | 17 (0.57%) |
| `CanvasService_Update` | 0 | 0 | - | 8 (0.27%) |
| InputMan + XInput `0x49B000-0x49F000` | 0 | 0 | 0 | 2 (0.07%) |
| `InputMan_PollPad` | 0 | 0 | - | 1 (0.03%) |
| Input service `0x598400-0x599200` | 0 | 1 | 1 (0.03%) | 1 (0.03%) |
| UI trigger service `0x7EF000-0x7F1500` | 0 | 15 (0.15%, stale) | 0 | 0 |
| Cursor manager `0x505380-0x505900` | 0 | 0 | 0 | 0 |

[measured: 10,251 sim samples, 2,991 main samples (verify run, autopilot, little real input)]. The main-thread pump time is mostly inside `PeekMessageW`/`DispatchMessageW` (OS), and the verify run had no human mouse input, so the real-play share is higher but still small. The sim-thread "on stack" hits have no input leaf and no consistent chain: stale return addresses (docs/45 caveat).

## 7. Candidates

All default off; costs are small, so these are latency and robustness items.

1. **IN-L1: measure input-to-pump latency** (tooling, cheap). Timestamp in `UIInput_ToWindowMessage` (main) and at dequeue in `WindowManager_ProcessMessages` (sim); report p50/p99 of the gap and the queue depth per pump. Repro: click and hover over the HUD on a heavy lot at speed 3. Decides whether IN-L2 is worth it.
2. **IN-L2: coalesce mouse moves** before posting: when the previous queued message is also a move (type 7) and not yet taken, overwrite it instead of appending. Cuts hit tests and managed hover callbacks after long sim ticks. Needs a hook in `WindowManager_PostMessage` under `0xFBA5CC` [inferred safe: the queue is only touched under that lock].
3. **IN-X1: skip the disconnected XInput probe** (`InputMan_Update`, the 250 ms branch) when no pad has ever connected, or move it to a worker. Removes up to 4 `XInputGetState` calls per second on empty ports from the main thread [community: these calls can stall; not measured here]. Measure first with a counter on `0x49C5F0` (calls and µs per call with no pad).
4. **IN-W1: cache `SPI_GETWHEELSCROLLLINES`** (read per wheel event in `Canvas_OnMouseWheel`). Tiny; only with IN-X1 in the same hook set.
5. **R-CUR: probe only.** Count overlapping `CursorManager_Apply` entries by thread; fix (a spin lock around `Apply`/`SetOverride`) only if a crash in the cursor providers shows up in the crash ledger.
6. **R-LOAD: probe only.** Thread of `App_LoadPumpCallback` entries during a world load and a lot load.
7. **Not candidates:** raw input (the game never uses it; adding it changes nothing without moving consumers), moving UTFWin message handling to the main thread (the managed callbacks must run on the sim thread).

## 8. Open items

- Decode the canvas listener `+0x60` slot `+0x38` jump table (which InputMan device receives each event) and the TriggerDispatcher (`0x49DAC0`, `Services/Input/TriggerMapResource` `0x7EFC20`, mouse wheel threshold/hysteresis `0x7F0500`).
- Run-time thread of the seven camera callers of `0x598F90` (ledger I-47-7) and of `0xECA350` (R-LOAD).
- Role of `0x6DEFF0` (renderer pick position update from UI input) and of the second pump `0x4123C0` (PeekMessageA, canvas `+0x90`).
- IME path `0x412780-0x4134E0` (composition strings) for M-UI3.

## 9. Names

43 rows appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv`, tagged `<docs/55>`, applied to `game-copy/ghidra-ui4`. No address reused with a different name.
