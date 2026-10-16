# 46. Render frame jobs of TS3W.exe (M-D3D3a)

PLAN item 7, row M-D3D3a. Static only: Ghidra headless on a private copy of the M-PHY project with the symbols CSV imported first; the game was not run. Sim-thread cost from the existing `probe-out/profile/*simstack*.jsonl` files only. Steam TS3W.exe 1.67, image base `0x400000`, all addresses absolute.

Tags: **[verified]** = read in the decompile or disassembly; **[measured]** = from the sample files of §6; **[inferred]** = reasoning, still to confirm.

Builds on docs/41 (frame loop, `Renderer_BeginFrame 0x6EBB70`, `Renderer_EndFrame 0x6E8810`), docs/36 (JobManager, masks, sim pump `0xD81FC3`) and docs/37 (device is main-thread only).

## 1. Summary

- **The "5 render jobs" are visibility jobs: four view culls and one sort.** Each builds a frustum from a camera, culls the scene into a render set (draw list), and the last one sorts the main render sets. None of them draws [verified bodies; per-view roles partly inferred, §2].
- **All six renderer jobs have mask -1 (any thread) and priority 10.** `Renderer_Init 0x6F40B0` creates them with `JobManager_CreateJob` (default mask -1) and only calls `Job_SetPriority(10)` on them; nothing in the renderer writes the mask (`job+0x28`) [verified]. So they go to the shared queue and **any record with bit 31 can run them: JobThread0, the main thread while it waits, and the sim thread's pump** [verified masks, docs/36].
- **They are created once and resubmitted every frame** (state reset to 1 at handle `+0x1C` = `job+0x24`, then `Job_Submit`) [verified].
- **The main thread waits for them inside the scene draw, before Present, not only in EndFrame.** `Renderer_DrawScene 0x6F3F90` (from the scene window `0x7EB6D0`, inside the UI draw of `Frame_RenderAndPresent`) waits for the shadow and reflection culls, and each draw pass waits for the job it needs (`0x6ED0A0`, `0x6F1D90` wait the main cull, `0x6F0DB0` the sort, `0x6F1390` the reflection cull). `Renderer_EndFrame` then waits for 310/318/31C/320 again; they are already done by then [verified]. This corrects docs/41 P3: the wait sits in step 8 (draw), so a slow job delays Present directly.
- **With the threaded-render flag off (`+0x26C` = 0) the same five bodies run inline** on the main thread in `Renderer_DrawScene` [verified].
- **No job body reaches the D3D9 wrapper through direct calls** (closure to depth 6, 30-125 functions per job, none in `0x60E000-0x61B000` and no `Device_*`/`Texture_*` names) [verified static, direct calls only]. 19-52 indirect calls per job are not resolved [open]. The jobs already run on JobThread0 in normal play, and the device has no MULTITHREADED flag (docs/37), so they cannot rely on the device [inferred].
- **Can the sim pump run them? Yes.** The sim record has mask `0x80000004`, and its pump at `0xD81FC3` drains the shared queue with no deadline (docs/36) [verified]. It only does so when a render job is ready at the moment it pumps, and it keeps going while chained jobs become ready, so in the worst case it runs the whole chain [inferred].
- **How long would it stall the sim?** In 10,238 existing sim-thread samples, **0 samples** are in any of the job bodies, cull or sort helpers, and the 10 samples in `JobManager_ExecuteJob` on the sim thread are all resource/other jobs (`0x727F60` async load, `0x93AA80`, `0x4E16F0`) [measured]. Upper bound about 0.03% of sim time (~0.3 ms/s) [measured, 95% bound for 0 hits]. One stolen job costs one view cull (main view: whole scene; unmeasured, likely 0.5-3 ms per frame on a lot-heavy view [inferred]); the main thread then waits for the sim thread in its draw for that long too.

## 2. Job table

Renderer object = `Renderer_Get 0x6E8330` (`[0x11D1860]`). Handles are `job+8`; `handle+8` = fn, `handle+0xC` = arg (the renderer); the fn returns 1 and only runs its body when called with mode 4 (`ExecuteJob` passes 4) [verified].

| Slot | Job fn (wrapper) | Body | Runs when | What it does | Inputs | Output | Depends on | Waited by |
|---|---|---|---|---|---|---|---|---|
| `+0x318` | `RenderJob_MainCullFn 0x6F3C30` | `Renderer_CullMainView 0x6F1B00` | always (when jobs on) | frustum of the main camera, scene cull through `Renderer/SceneCullFilter/ObjectList` (two filter variants by `+0x514`/`+0x515`), sort of two sub-lists (`+0x110`, `+0x120`), `ScreenCoverageSet_Update 0x59E7C0` (`Renderer/ScreenCoverageSet`, lock `0xFDDDDC`) when `0x500B80` is true [verified] | camera `+0x24`, scene/view `+0x1C0`, masks `+0x2C0`/`+0x2C4`, flags `+0x262`/`+0x267`/`+0x270`/`+0x2A2` | render set `+0x10` | none (submitted first) | `0x6ED0A0`, `Renderer_DrawMainPass 0x6F1D90`, EndFrame |
| `+0x310` | `RenderJob_ShadowCullFn 0x6EEB30` | `Renderer_CullShadowView 0x6EBE30` | `+0x28E` (shadow map texture `0x633E72E2` present, set by `Renderer_ChooseFrameJobs 0x6EBA90`) | fits the shadow camera to the view (`Shadow_FitLightCamera 0x723BE0`, or `0x6E9F00` / `0x6EBD70` in the other modes), frustum, cull; clears `+0x28E` when the set is empty [verified code; "shadow" from the texture name and the `0x723BE0` light fit, inferred] | cameras `+0x24`, `+0x2C`, light data `+0x1C0`+0x140, `+0x40`-`+0x1A0` | render set `+0x14` | `+0x318` | `Renderer_DrawScene`, EndFrame |
| `+0x314` | `RenderJob_ReflectionCullFn 0x6EDA60` | `Renderer_CullReflectionView 0x6EA260` | `+0x294` != 0 and not `+0x270` | camera `+0x30`, mask from `+0x2C4`/`+0x2CC`, water-plane check `0x725700` (WaterReflection block `0x725000`), cull [verified code; "reflection" inferred from the block] | camera `+0x30`, render set `+0x10`+0x90 | render set `+0x1C` | `+0x310` if shadows on, else `+0x318` | `Renderer_DrawScene`, `Renderer_DrawReflectionPass 0x6F1390` (not EndFrame) |
| `+0x320` | `RenderJob_Shadow2CullFn 0x6EEB50` | `Renderer_CullShadow2View 0x6EBF50` | `+0x29C` (needs `+0x261`, `+0x275`, shadows on, `+0x270` off and three checks `0x6E8EA0`/`0x6E8310`/`0x6EB100`) | second light-camera fit (`0x723BE0`, camera `+0x34`), cull; clears `+0x29C` when empty [verified code; a second shadow view, e.g. merged/far shadow, inferred] | cameras `+0x24`, `+0x34`, `+0x1A8`, `+0x110`, `+0xD0`, `+0x150` | render set `+0x20` | the last job submitted before it (318, 310 or 314) | `Renderer_DrawScene`, EndFrame |
| `+0x31C` | `RenderJob_SortMainFn 0x6F3C50` | `Renderer_SortMainRenderSets 0x6F1D10` | always (when jobs on) | `RenderSet_Sort 0x6F16B0` on six sub-lists of render set `+0x10` (`+0x140`, `+0x190`-`+0x1D0`; `Renderer/SceneManager/RenderSetEntryBuffer`, key sort `0x6EF500`/`0x6EF480`) [verified] | render set `+0x10` | sorted render set `+0x10` | `+0x318` | `Renderer_DrawMainRenderSets 0x6F0DB0`, EndFrame |
| `+0x30C` | (fn not set in `Renderer_Init`) | RenderingJobs queue job | `+0x26B` | `Renderer_BeginFrame` pops the `RenderingJobs` queue (`RenderingJobs_Pop 0x6E33F0`, lock `0xFF7700`) and waits for each entry; `Renderer_EndFrame` submits this job, steps the queue (`RenderingJobs_Advance 0x6E2120`) and waits [verified]; its body is not found yet [open] | RenderingJobs deque in the RenderSystem `[0x11D1530]` `+0x1C8`-`+0x1E4` | - | - | EndFrame |

All six: mask -1, priority 10 (`job+0x20`, set by `Job_SetPriority 0x59A820`; the meaning "priority" is inferred from the re-queue) [verified]. Dependencies through `Job_AddDependency 0x59A870` -> `JobManager_AddDependencyLocked 0x59A6E0`: the dependent waits for the prerequisite; a cancelled prerequisite cancels the dependent [verified].

### 2.1 Other submit and wait sites

- `Renderer_ProcessRenderRequests 0x6F3E90` (from `RenderSystemService_Update 0x6E3620`, main slot): for each queued request in list `Renderer+0x4F8` it calls `Renderer_RenderRequest 0x6F3DC0` (waits **all** frame jobs through `Renderer_WaitFrameJobsAndDraw 0x6F3C90`, then a `Renderer/SceneManager` pass) and submits the request's own completion job, with a QPC time check [verified]. These completion jobs belong to the requester (thumbnails, CAS), not to the frame chain.
- `Renderer_WaitFrameJobsAndDraw 0x6F3C90` / `0x6F3D30` are also called from `ThumbnailManager_Update 0xD61670` (via `0xD5C6D0` -> `0xD59C10`), `VideoCapture_RenderFrame`, `0xC69FF0` and `0x6F3DC0` from `IC_CASUtils_IsSceneLoadedImpl 0x5A9780` [verified call edges]. If the IC edge is real, a sim-thread IC waits for the render jobs and then draws: worth a thread check (folded stub names exist, docs/33 §8) [open].
- The other `Job_Submit` callers in `0x6C0000-0x730000` are the async loader (`AsyncLoader_CreateLoad 0x7297E0`, `0x727241`) and the mesh pool (`0x6CF32B`), not frame jobs [verified xrefs].

## 3. Data flow per frame

1. Step 4 (`ServiceList_Update`): `FrameDb_ConsumeAndInterpolate` writes interpolated object transforms and the render time under the FrameDatabase lock `+0x48`; scene update `0x6FDE10`/`0x701270` (docs/41) [verified].
2. Step 7 (`Renderer_BeginFrame`): RenderingJobs drain, deferred callbacks, pre-passes, `Renderer_ChooseFrameJobs` sets `+0x28E`/`+0x28F`/`+0x29C`, then submit: 318; 310 after 318; 314 after 310 or 318; 320 after the last; 31C after 318 [verified].
3. Jobs run on JobThread0, on the main thread when it waits, or on the sim pump. They read cameras and scene objects (the interpolated state of step 1) and write only their own render sets and the two "empty" flags `+0x28E`/`+0x29C` [verified bodies]. They do not call the FrameDatabase functions of docs/41 at depth 0 [verified]; deeper closures not checked for FrameDb calls [inferred: none].
4. Step 8 (draw): each pass waits for its job, then draws the render set on the main thread with the device [verified waits; draw roles inferred].
5. Step 9 (`Renderer_EndFrame`): waits 310 (if `+0x28E`), 318, 31C, 320 (if `+0x29C`), `0x6E4760`, then the RenderingJobs tail [verified].

No overlap with step 4 of the next frame, so the scene is not written by the main thread while a job reads it [verified order]. The sim thread does not write render scene objects directly (it posts render messages, docs/41) [inferred from docs/41; not rechecked here].

## 4. Thread rules

| Rule | Status |
|---|---|
| Frame jobs have mask -1 and go to the shared queue | [verified] |
| JobThread0 (`0x80000000`), main (`0x80000001`) and sim (`0x80000004`) records take shared jobs; the resource worker (mask 2) does not | [verified, docs/36] |
| Main thread helps while it waits: `JobManager_WaitLocked` on a job thread record runs its own jobs one at a time, and `RunJobsLocked` moves a higher-priority shared head into its own queue | [verified, docs/36] |
| Sim pump `0xD81FC3` runs shared jobs until both queues are empty, no deadline | [verified, docs/36] |
| Job bodies do not call the D3D9 wrapper directly | [verified static, direct calls; indirect calls open] |
| Job bodies take one lock: ScreenCoverageSet `0xFDDDDC` (main cull only) | [verified in the decompile of `0x59E7C0`; other locks in indirect calls open] |
| Threaded render off (`+0x26C` = 0): all bodies inline on the main thread | [verified] |

## 5. Sim-pump answer

- **Possible:** yes. Nothing pins the frame jobs; the sim record matches mask -1 and its pump has no budget [verified].
- **When:** only if the sim thread reaches `0xD81FC3` between BeginFrame's submit and the moment JobThread0 or the main thread took the job. JobThread0 is woken by `ReadyJob` (it wakes one idle thread or waiter), so the window per job is short [inferred]. Chained jobs become ready while the sim is still pumping, so it can take several in a row [inferred].
- **Measured:** 0 of 10,238 sim samples in any frame job body or its cull/sort helpers; sim-thread `ExecuteJob` samples (10) are all resource and other jobs [measured]. So today this is rare, under ~0.3 ms/s [measured bound].
- **Stall size if it happens:** one cull body, unmeasured. The main-thread sampler (docs/41 §7, bucket "frame begin" / "draw") has to give the job lengths [open]. Two effects at once: the sim tick gets longer by that time, and the main thread's draw waits for the sim thread to finish the job (the main thread cannot take a job that is already running).

## 6. Sim-thread cost (existing samples)

Source: all non-empty `probe-out/profile/*simstack*.jsonl` (10,238 samples: BP 3,400, IP 4,439, SV 1,723, other 676; base and fix configs mixed). Method of `tools/harness/sample-split.ts`: leaf-most TS3W frame = self, any of the first 24 TS3W frames = inclusive, nearest function start from `game-copy/analysis/functions.csv`. Script in the session scratch folder [measured].

| Block | Self | Inclusive |
|---|---|---|
| Five job wrappers and bodies (§2) | 0 | 0 |
| `Scene_CullIntoRenderSet 0x6E6530`/`0x6E6570`, `RenderSet_Sort 0x6F16B0`, frustum `0x68DEE0`/`0x68DFF0` | 0 | 0 |
| Wait-and-draw `0x6F3C90`/`0x6F3D30`/`0x6F3F90`, `IC_CASUtils_IsSceneLoadedImpl`, `ThumbnailManager_Update` | 0 | 0 |
| `JobManager_ExecuteJob 0x599720` | 0 | 10 (0.10%), none a frame job |
| `JobManager_Wait`/`WaitLocked` | 0 | 6 (0.06%) |
| Renderer address range `0x6D0000-0x730000` | 50 (0.49%) | - (mostly `0x72D470`-`0x72DBD0`, the resource/async-load block of docs/39, not frame jobs) |

## 7. Candidates

| # | Candidate | Kind | Notes |
|---|---|---|---|
| RJ1 | **Measure first:** count frame-job executions per thread id (hook `JobManager_ExecuteJob 0x599720`, compare `job+0x10` with the five fns) and their length, next to the main-thread sampler | ts3native counter or Frida, log only | Settles whether the sim pump ever steals them during lot streaming or heavy scenes, and gives the stall size |
| RJ2 | **Keep frame jobs off the sim thread:** give the five jobs a private mask bit (e.g. 8) in `Renderer_Init` after creation and add that bit to JobThread0's and the main record's masks. Not mask 1 (main only): that serialises all culls on the main thread | small patch, opt-in | `ReadyJob` gives a non-(-1) mask job to the **first** matching record's own queue, so with two matching records only one gets it: check the record order or keep mask -1 and instead patch the sim pump (RJ3) |
| RJ3 | **Sim pump skips the shared queue** (or the docs/36 C1 deadline): run the sim pump with the "own queue only" path, so shared jobs (render, CAS loads) stay with JobThread0 and main | small patch, opt-in | Wider effect: CAS and store jobs then run only on JobThread0/main; measure CAS load times |
| RJ4 | **More generic workers:** `JobManager_Init(1)` in `JobManager_ReadTimeslices 0x5992D0` creates one worker per set bit; `3` gives JobThread0 + JobThread1. The 4-5 culls of a frame then run in parallel and the chance the sim pump finds a ready job drops further | one-byte patch, opt-in | 8-core target; check that no shared job assumes a single worker (CAS, store jobs) [open] |
| RJ5 | **Wait placement:** the draw waits are already per pass; nothing to gain until RJ1 shows long waits in step 8 | none | Correction to docs/41 P3 |

## 8. Open items

- Body of the RenderingJobs job `+0x30C` (fn not set in `Renderer_Init`; look in `RenderSystem_ctor 0x6E3250` or the first `+0x26B` set site).
- Indirect calls in the job closures (19-52 per job): resolve with `VCallSites` to make the "no D3D" claim complete.
- Thread of `IC_CASUtils_IsSceneLoadedImpl -> 0x6F3DC0` (a sim-thread IC that waits for frame jobs and draws would be a device-thread bug like docs/43).
- Exact roles of `+0x314` (reflection) and `+0x320` (second shadow view), and of the pass `0x6ED0A0`.
- Main-thread job lengths (main-thread sampler).

## 9. Names

29 new rows in `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/46>`): `Renderer_Init`, `Renderer_Shutdown`, the five job fns and bodies, `RenderSet_Sort`, `Scene_CullIntoRenderSet`, `CullFrustum_FromCamera`, `Shadow_FitLightCamera`, `ScreenCoverageSet_Update`, `Job_SetPriority`, `Job_AddDependency`, `JobManager_AddDependencyLocked`, `Renderer_DrawScene` and the draw passes, `Renderer_WaitFrameJobsAndDraw(B)`, `Renderer_RenderRequest`, `Renderer_ProcessRenderRequests`. No address reused with a different name.

Method: decompile BeginFrame/EndFrame, then an instruction search for stores to the job slots (`+0x30C`-`+0x320`) in the renderer range found `Renderer_Init` and every wait site; job fns are the constants stored at `handle+8`; a direct-call closure (depth 6) per body checked for device calls.
