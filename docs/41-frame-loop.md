# 41. Frame loop and render pipeline of TS3W.exe (M-D3D2)

Static map of the main-thread frame: what runs each iteration of the main loop, in what order, how sim results reach the renderer, and where the main thread waits. Steam 1.67 `TS3W.exe`, Ghidra with `tools/ghidra/symbols/ts3w-1.67.symbols.csv`. No game run; no main-thread samples exist yet, so this doc gives no costs, only the list of functions the main-thread sampler should attribute (§7).

Builds on docs/04 F19 (the frame function), docs/36 (threads, job pumps, services), docs/37 (device wrapper, present parameters) and docs/03 (S3SS anchors).

## 1. Summary

- **One main loop, no render thread.** `MainLoop 0xECA960` runs on the main thread and does everything per iteration: load-state tick, engine services (main slot `+0x1C`), sim-to-render consume, scene render, Present, frame clock [verified]. There is no separate render thread; "render jobs" are JobManager jobs that the main thread waits for in the same frame (§4).
- **The frame limiter is Present.** The loop has no FPS cap of its own. Only vsync (`PresentationInterval ONE` when the vsync option is on, docs/37) or DXVK's `d3d9.maxFrameRate` / S3SS Precise's FPS cap pace the frames [verified loop code; vsync from docs/37]. Two built-in throttles exist: a **10 FPS render cap while the ResourceSystem says so** (`[0x11D3E28]` vtable `+0x58`, 100 ms stopwatch at `0xECAA65`; loading is the inferred meaning) and the **inactive-window sleeps** (10 ms per loop when another process owns the foreground window, and `SleepEx(30)` after Present when device `+0x8D` is set) [verified].
- **Sim-to-render is a 3-slot frame ring plus a double-buffered message list**, both in the `Services/FrameDatabase` object (`FrameDb_Get 0x5934C0`) [verified]:
  - Frame ring: 3 headers of 16 bytes at `+0x14`, read index `+0x68`, write index `+0x6C`, count `+0x70`, guarded by the critical section at `+0x48`. The sim fills the write slot and publishes it (`FrameDb_PublishFrame 0x593600`); when more than 2 frames are queued the oldest is dropped, so **the sim never blocks on a slow renderer** except for the lock itself [verified].
  - Messages: 0x50-byte render messages (transform updates from animation, docs/22) are posted into the back list (`RenderMessageBuffer_Post 0x593E60`, critical section `+0xD0`); the main thread swaps front/back under the same lock and runs each message (`FrameDb_DrainEvents 0x5937B0`) [verified].
- **The renderer interpolates between the two oldest queued sim frames** using its own render clock (`FrameDb_AdvanceRenderClock 0x693660`): render time += frame dt, clamped to `[tA, tB]`; the lerp factor goes to every object update (`0x6931A0`, `0x693440`, `0x6934C0`) [verified decompile; "two oldest" from `FrameDb_GetReadPair 0x593440`]. When the sim is late, render time sits at the newest frame and **motion freezes until the sim publishes again** (frame-pacing candidate P1).
- **The only blocking sync between the main and the sim thread in the frame path is the FrameDatabase lock `+0x48`.** The main thread takes it with `TryEnterCriticalSection` (never waits; skips the consume for that frame), then **holds it while it drains every pending render message and updates every interpolated object**. The sim takes it with `EnterCriticalSection` twice per tick (`SimRender_TripleBufferSync 0x593590`, `FrameDb_PublishFrame`) and therefore waits for the whole main-thread consume: this is the measured 0.6% sim-thread wait of docs/20 [verified code; the link to the 0.6% is inferred].
- **The main thread waits on render jobs, not on the sim.** `Renderer_BeginFrame 0x6EBB70` submits up to 5 chained render jobs (scene work, `+0x310`-`+0x320`) and `Renderer_EndFrame 0x6E8810` waits for them with `JobManager_Wait` after Present was issued [verified]. These jobs go to the shared queue, so `JobThread0`, the main thread (help-while-wait) and the **sim thread's unbudgeted pump** (docs/36 C1) can all run them [verified mask from docs/36, inferred for these 5 jobs].
- **GPU waits are hidden in Present** (`Device_Present 0x611680`, EndScene + Present): with vsync or a DXVK frame cap, the main thread blocks there; DXVK can also block in Present when its frame queue is full [inferred, standard D3D9/DXVK behaviour].

## 2. Functions

### 2.1 Main loop and frame

| Address | Name | Thread | What it does |
|---|---|---|---|
| `0xECA960` | `MainLoop` | main | Loop until state `0x28D869F`. Reads `[Config] NoInactiveIdle` once; `Stopwatch_Init(4,1)` (ms) for the loading cap [verified] |
| `0xEC6C30` | `LoadState_Update` | main | Tick of the load-state machine (`Misc/LoadState/Warnings`, `EALogo`, `Intro`, `Gameplay`...; states created by `LoadState_CreateState 0xEC6A80`), state vtable `+0xC` [verified tags, role inferred] |
| `0x5887A0` | `App_IdleIfNotForeground` | main | `Sleep(10)` unless the foreground window belongs to this process; skipped when `NoInactiveIdle` or the loading cap is active [verified] |
| `0x72F1C0` | `ResourceSystem_Get` | any | `[0x11D3E28]`, set by the ResourceSystem service init `0x733F40` (`Timeslicing/ResourceSystem`) [verified]. Its vtable `+0x58` gates the 10 FPS cap [verified call, meaning "loading busy" inferred] |
| `0x594300` / `0x594330` | `GameClock_SetPauseFlags` / `GameClock_ClearPauseFlags` | main, sim | Pause bits at clock `+0x40` under lock `0xFD7C84`. MainLoop sets bit 4 while device `+0x8D` (lost or not foreground) is set and passes game dt 0 that frame; bit 1 is the script pause (`IC_GameUtils_PauseImpl`) [verified] |
| `0x588E00` -> `0x59ED20` | `ServiceList_Update` | main | Every service's `+0x1C(realDt, gameDt)` (`[0x11CDD9C]`, `[0x11CDDAC]`), incl. `JobManagerService_Update 0x599A10` (main job pump with timeslice, docs/36) and `RenderSystemService_Update 0x6E3620` [verified] |
| `0x9E1B70`, `0x9DE140` | `VideoCapture_Get`, `VideoCapture_RenderFrame` | main | Only while the capture object (`[0x11E9AF0]`) has pending work: renders a frame through `Frame_RenderCallback 0xECA030` (present = 0), then encodes; audio chunks in `VideoCapture_WriteAudio 0x9E0C70` [verified calls; "video capture" inferred from the 0x2004-sample audio chunks and the fixed-step switch] |
| `0x6E8330` | `Renderer_Get` | any | `[0x11D1860]` (241 callers), the renderer/scene object [verified] |
| `0x6EBB70` | `Renderer_BeginFrame` | main | Once per frame (flag `+0x290`), skipped when device `+0x8D`: drains the `RenderingJobs` queue (`RenderingJobs_Pop 0x6E33F0`, lock `0xFF7700`, `JobManager_Wait` between tries), runs the deferred callbacks (`Renderer_RunDeferredCallbacks 0x6E4130`), the pre-passes `0x6E01B0` and `0x6DFEC0` (a `Device_BeginScene` caller: render-to-texture passes), `Renderer_ChooseFrameJobs 0x6EBA90`, then submits up to 5 chained jobs (`+0x310`, `+0x314`, `+0x318`, `+0x31C`, `+0x320`; dependencies through `0x59A870`) [verified] |
| `0xEC9F00` | `Frame_RenderAndPresent` | main | `Device_BeginScene`, viewport, the UI tree draw (`0x50AB70()` vtable `+0x30`; the 3D scene is drawn by the UI's scene window, `0x7EB6D0` also calls Begin/EndFrame [inferred]), capture grab (`0x9E19E0` `+0x48`), `Device_EndSceneAndPresent 0x611760` when present = 1, then `SleepEx(30)` when device `+0x8D` (S3SS Precise anchor `0xEC9FBA`) [verified, docs/04 F19] |
| `0x6E8810` | `Renderer_EndFrame` | main | `JobManager_Wait` on the 3-4 frame jobs, `0x6E4760`, screen capture `0x706650` when `+0x26A`, then one more job submit + `RenderingJobs_Advance 0x6E2120` + wait when `+0x26B` [verified] |
| `0x588E20` | `Canvas_ApplyPendingMode` | main | Mode change / device reset when device `+0x8E` (docs/37) [verified] |
| `0x5943F0` | `GameClock_FrameTick` | main | QPC delta since last frame -> real dt (`+0x54` = `[0x11CDD9C]`), frame count `+0x4C`; fixed dt when `+0x44` (capture); then `GameClock_Advance 0x594230`: game dt = real dt x scale (`+0x74`), clamped to `+0x78`, x speed (`+0x68`), accumulated at `+0x60`, unless paused [verified] |

Clock object: `0x11CDD48` (fields above) [verified].

### 2.2 Sim-to-render (FrameDatabase)

| Address | Name | Thread | What it does |
|---|---|---|---|
| `0x5934C0` | `FrameDb_Get` | any | FrameDatabase singleton (12 callers) [verified] |
| `0x593D60`, `0x593EC0` | `FrameDb_ctor`, `FrameDbService_Create` | main (startup) | `Services/FrameDatabase/EventList`, `/Service`, `/Block` [verified tags] |
| `0x76C5C0` | `Simulate_CanSeeSky_A` (name kept; really **sim frame begin**) | sim | Called once early in `MonoScriptHost_Simulate` (`0xD81A81`); takes a write slot through `SimRender_TripleBufferSync` [verified]. The CSV names `0x76C5C0`/`0x76C8A0` come from docs/20 and do not match the code; addresses kept to avoid a rename clash |
| `0x76C8A0` | `Simulate_CanSeeSky_B` (really **sim frame publish**) | sim | Called at `0xD81DF5` and `0xD81E9E`: write slot again, clears it (`0x5933C0`), writes the tick header (time, dt, world values, a callback list through `0x93ABC0`), marks it ready (`slot+0xC = 1`), `FrameDb_PublishFrame` [verified] |
| `0x593590` | `SimRender_TripleBufferSync` | sim | Locks `+0x48`; if more than 2 frames queued: drop the oldest (normal mode) or return 0 = skip this publish (fixed-step mode `+0xFC`); returns the write slot `+0x14 + 16 x [+0x6C]` [verified] |
| `0x593600` | `FrameDb_PublishFrame` | sim | Locks `+0x48`; write index + 1 mod 3, count + 1, cap 2 (3 in fixed-step mode), drop oldest above the cap [verified] |
| `0x593560`, `0x6933E0` | `FrameDb_SetFixedStepFlag`, `FrameDb_SetFixedStep` | main | Fixed render step (`consumer+0x14` > 0) sets `+0xFC`; only caller the capture path `0x9E0C70` [verified] |
| `0x593E60` | `RenderMessageBuffer_Post` | sim (and others) | Appends a 0x50-byte message to the back list under `+0xD0` [verified] |
| `0x6E3620` | `RenderSystemService_Update` | main | RenderSystem service `+0x1C` (vtable `0xFF7D84`, created by `RenderSystemService_Create 0x6E3C60`, object built by `RenderSystem_ctor 0x6E3250` with `Renderer/RenderSystem/RenderingJobs`, `CondemnedResources`, `GlobalShaderSettings`): `FrameDb_ConsumeAndInterpolate(dt)` on `+0x25C`, then `0x6FDE10`/`0x701270` (scene update with the same dt), `0x6E3190`, `0x6E8500`, `0x6F3E90`, `0x6E3540` (condemned resources) [verified] |
| `0x693780` | `FrameDb_ConsumeAndInterpolate` | main | Skips unless `+0xF8 == 0` and `TryEnterCriticalSection(+0x48)` succeeds. Loop: read pair (`FrameDb_GetReadPair 0x593440`), stop if the newer slot is not ready, advance the render clock, step to the next pair while render time passed it. Then `FrameDb_DrainEvents` (messages up to the frame time, callback `0x693170`), `Renderer_SetRenderTime 0x6E8920` (lerped time to renderer `+0x370`), per-object interpolation (`0x6931A0`, and `0x693440`/`0x6934C0` filtered by masks `[0x11E9158]`, `[0x11DC484]`), then releases `+0x48` [verified] |
| `0x693660` | `FrameDb_AdvanceRenderClock` | main | Render time += dt (or the fixed step), clamped to `[tA, tB]`; returns `(t - tA) / (tB - tA)` clamped to [0, 1] [verified] |
| `0x5937B0` | `FrameDb_DrainEvents` | main | Swaps front/back message lists under `+0xD0` when the front is empty, runs each message; marker messages (`0xD33CAA7C`) stop at the frame time [verified] |

## 3. Per-frame order of work (main thread)

One `MainLoop` iteration, in order [verified unless marked]:

1. `LoadState_Update` (game state machine).
2. Foreground check: `Sleep(10)` when another process owns the foreground window (not during the loading cap, not with `NoInactiveIdle`).
3. Read the previous frame's real dt and game dt from the clock. Device lost or not foreground: pause bit 4, game dt = 0.
4. `ServiceList_Update(realDt, gameDt)`: every engine service's main slot. Includes:
   - `JobManagerService_Update`: main job pump with a timeslice (docs/36), runs main-only jobs (lot renderer create, imposters, remote calls with selector 0) and shared jobs.
   - `RenderSystemService_Update`: **sim-to-render consume under the FrameDatabase lock** (§2.2), scene update with dt.
   - `World/Service` -> `WorldManager_Update 0xC6D570` (lot scoring and streaming, docs/28 §5) and the other main-slot services. Service order follows the service list (not mapped here).
5. Loading cap: if `ResourceSystem` vtable `+0x58` is true and the 100 ms stopwatch has not elapsed, skip steps 6-10 (no render this iteration, no sleep: the loop spins through services).
6. Video capture frame when capturing (`VideoCapture_RenderFrame`, renders through `Frame_RenderCallback` with present = 0).
7. `Renderer_BeginFrame`: drain RenderingJobs, deferred callbacks, pre-passes (`0x6DFEC0` BeginScene passes), submit the frame jobs.
8. `Frame_RenderAndPresent(1)`: BeginScene, UI tree draw (3D scene window inside it, inferred), capture grab, EndScene + **Present** (vsync / DXVK cap blocks here), `SleepEx(30)` if not foreground.
9. `Renderer_EndFrame`: **wait for the frame jobs**, screen capture, RenderingJobs tail.
10. `Canvas_ApplyPendingMode` (device reset when a mode change is pending).
11. `GameClock_FrameTick`: measure real dt with QPC, advance game time.

Other `Device_BeginScene` passes (docs/37 §9) run inside steps 4, 7 or 8 depending on the subsystem: lights `0x6B31D0`/`0x6B32F0`/`0x6B3510` (via `0x6B56E0`), shadows `0x699300`, terrain `0xC296E0` (from `WorldTerrain_Update 0xC845C0`, a step-4 service path), imposters `0xADDEA0`, thumbnails `0xD64730`, CAS `0x5FC170` [verified callers from `CallerTree`; which step each lands in is inferred from the caller roots].

## 4. Sync points between the main and the sim thread

| # | Sync point | Main side | Sim side | Blocking? |
|---|---|---|---|---|
| S1 | FrameDatabase frame lock `+0x48` | `FrameDb_ConsumeAndInterpolate` (TryEnter, holds it through the message drain and object interpolation) | `SimRender_TripleBufferSync` (frame begin and publish), `FrameDb_PublishFrame` (EnterCriticalSection) | **Sim waits for main** for the length of the consume; main never waits (skips the consume instead) [verified] |
| S2 | FrameDatabase message lock `+0xD0` | `FrameDb_DrainEvents` (swap only) | `RenderMessageBuffer_Post` (every transform update) | Short: held only for a swap or one append [verified] |
| S3 | Frame ring capacity (2, or 3 in fixed step) | consumes 0-n frames per render frame | drops the oldest when full | **Non-blocking by design**: sim faster than render -> dropped frames; sim slower -> render clock clamps, motion freezes [verified] |
| S4 | JobManager lock `0xFD9744` and shared queue | main pump (timeslice), `Renderer_EndFrame` waits | sim pump at `0xD81FC3` (no budget) runs shared jobs, incl. render jobs | Main waits for a render job that the sim thread picked up only until that job ends [inferred from docs/36 masks] |
| S5 | Remote calls (`RemoteCall_Dispatch`, docs/36 §5) | posts selector 1 (lot add/remove, async), runs selector 0 jobs in its pump | posts selector 0 sync calls (lot renderer create, imposters, `RunOnMainThreadSync`) and **waits for the main pump** | **Sim waits for main** on sync posts: up to one main frame plus the main pump's timeslice [verified post/wait, delay inferred] |
| S6 | Game clock lock `0xFD7C84` | `GameClock_FrameTick`, pause flags | `IC_GameUtils_*` pause/speed calls, `0x76B330` (sim waits for the service to unpause, `Sleep(10)` loop) | Short, except the sim's pause loop [verified] |
| S7 | `RenderingJobs` queue lock `0xFF7700` | `Renderer_BeginFrame` drain, `Renderer_EndFrame` | `RemoteCall_JobFn 0x7D9840` offers kind-4 calls to `RenderingJobs_Pop` when `[0x11D1530]` | Short [verified callers; offer semantics inferred] |

What the main thread waits on, in order of likely length [inferred until the main-thread sampler runs]: Present (vsync, DXVK frame queue or cap, GPU), `Renderer_EndFrame` job waits (parallel scene work not done yet), sync jobs in the main pump, `Sleep(10)` / `SleepEx(30)` when not foreground. It never waits for the sim thread in the frame path; it only skips a consume when the sim holds S1.

## 5. S3SS and DXVK interplay

- S3SS Smooth Patch Precise patches `0xEC9FBA` (after Present) to add an FPS cap and a "tick at most once per frame" signal, and replaces the sim's idle call at `0xD81FDE` with its own limiter (docs/03) [community + verified anchors]. With the signal on, the sim publishes at most about one frame per render frame, so the ring rarely drops frames but **every late tick shows as a frozen frame** (S3).
- DXVK `d3d9.maxFrameRate` / `presentInterval` act inside Present (step 8) and overrule the game's vsync option (docs/37 C6) [community].
- With 480 TPS (S3SS default) and 60 FPS the sim publishes ~8 frames per render frame: the ring keeps the newest 2, and the 6 others are dropped after their messages were posted, so the main thread still drains every message (S1 held longer) [inferred from the code paths]. **Refuted for the count (docs/72):** the sim runs 34-90 iterations/s, not 480, and publishes ~1 frame per iteration, so 1.3-1.6 frames per render frame were measured; the drain of every message is confirmed statically.

## 6. Candidates for frame-pacing stutter

| # | Candidate | Why | Check |
|---|---|---|---|
| P1 | **Render clock clamp when the sim is late** | `FrameDb_AdvanceRenderClock` never extrapolates: when the newest frame is older than render time, motion freezes until the next publish, then jumps. Any long sim tick (GC, routing burst, ThreadedObjects overrun, docs/28) becomes a visible hitch, even at a steady FPS | Count consume calls where the clamp hit (`t == tB` and no next slot), next to sim tick length |
| P2 | **Consume holds the frame lock through the message drain** | The sim blocks in `SimRender_TripleBufferSync` for the whole drain + interpolation; many animated Sims = many messages = longer hold. Measured 0.6% sim-thread wait (docs/20) | Main-thread time inside `0x693780` and `0x5937B0` per frame; possible fix: swap under the lock, drain after releasing it |
| P3 | **Frame jobs waited for after Present** | `Renderer_EndFrame` waits for jobs that may sit on the sim thread's unbudgeted pump or on the single `JobThread0`; a frame then takes as long as the slowest job | Main-thread samples in `JobManager_Wait` from `0x6E8810` |
| P4 | **Main-pump sync remote calls** | Lot renderer create, imposter build, lot rebuild run on the main pump inside step 4; heavy ones (imposters, docs/25) lengthen one frame | Samples under `JobManagerService_Update 0x599A10` |
| P5 | **Loading cap** | While the ResourceSystem gate is true, render drops to 10 FPS. If the gate also fires during live-mode streaming, that would be a direct stutter source | Read the `+0x58` slot target; log the gate state during camera pans |
| P6 | **dt spikes** | `GameClock_FrameTick` measures real dt; one long frame gives a long dt to every service and to the render clock in the next frame (catch-up jump) | Frame-time histogram (test B, `frame-times.js`) vs main-thread samples |
| P7 | **Device reset per frame** | `Canvas_ApplyPendingMode` retries reset each frame (docs/37) | Only on alt-tab or mode change |

## 7. Main-thread sampler attribution list (`--mainstack`, docs/20 §7)

Buckets for `sample-split.ts --main`, inclusive, innermost bucket wins:

| Bucket | Functions (entry) |
|---|---|
| load state | `LoadState_Update 0xEC6C30` |
| idle sleep | `App_IdleIfNotForeground 0x5887A0`, `Sleep_Wrapper_SmoothPatchTarget 0x4E1320` (from `0xEC9FD1`) |
| services (other) | `ServiceList_Update 0x59ED20` minus the buckets below |
| job pump | `JobManagerService_Update 0x599A10` |
| world / lot scoring | `WorldManager_Update 0xC6D570`, `World_LotStreaming 0xC622D0`, `WorldTerrain_Update 0xC845C0` |
| sim-to-render consume | `RenderSystemService_Update 0x6E3620`, `FrameDb_ConsumeAndInterpolate 0x693780`, `FrameDb_DrainEvents 0x5937B0`, message callback `0x693170`, object interpolation `0x6931A0`, `0x693440`, `0x6934C0` |
| scene update | `0x6FDE10`, `0x701270` (called from `0x6E3620`) |
| video capture | `VideoCapture_RenderFrame 0x9DE140`, `VideoCapture_WriteAudio 0x9E0C70` |
| frame begin | `Renderer_BeginFrame 0x6EBB70`, `RenderingJobs_Pop 0x6E33F0`, `Renderer_RunDeferredCallbacks 0x6E4130`, pre-passes `0x6E01B0`, `0x6DFEC0` |
| draw (UI + scene) | `Frame_RenderAndPresent 0xEC9F00` minus Present, `0x7EB6D0` (scene window) |
| present | `Device_EndSceneAndPresent 0x611760`, `Device_Present 0x611680` (and d3d9.dll/DXVK frames below it) |
| frame end / job wait | `Renderer_EndFrame 0x6E8810`, `JobManager_Wait` under it |
| device reset | `Canvas_ApplyPendingMode 0x588E20`, `Device_TryReset 0x610AB0` |
| frame clock | `GameClock_FrameTick 0x5943F0`, `GameClock_Advance 0x594230` |
| other render passes | `Device_BeginScene` callers of docs/37 §9 (lights, shadows, terrain, imposters, thumbnails, CAS) |

The sim thread's side of S1 is already visible in the sim sampler as `SimRender_TripleBufferSync 0x593590` and `FrameDb_PublishFrame 0x593600` waits.

## 8. Open items

- The five frame jobs (`Renderer+0x310`-`0x320`): job functions and masks not read (M-D3D3 scene and culling).
- Service order inside `ServiceList_Update` (which services run before and after the consume).
- ResourceSystem vtable `+0x58` target (confirm "loading busy").
- The `0x50AB70` UI object and the scene window `0x7EB6D0`: confirm the 3D scene is drawn inside the UI draw call (M-UI).
- Main-thread costs: queue `tools/harness/queues/mainstack.json` and split with the buckets of §7.

## 9. Method notes

- Start from the decompile of the loop and its direct callees (`DecompileTo` depth 0), then `ListInsns` (scratch: instruction listing with float/dword values at data operands) for constants the decompile hides (the 100.0 at `0x10F5B10`, the clock fields).
- The consumer of the frame ring had no direct caller in the decompile; `XrefsTo` on it found the call at `0x6E3653` inside an undefined function, and a `DumpPtrs` of the RenderSystem service vtable showed it is slot `+0x1C`.
- Two CSV names from docs/20 (`Simulate_CanSeeSky_A/B`) do not match the code; the doc names their roles and leaves the CSV rows unchanged.
