# 167. Main-loop stage timers per presented frame

Gap #1 of docs/162: a slow presented frame (camera stutter) had a length but no cause. `tools/harness/frame-times.js` now times every main-loop stage of docs/41 §3 inside the frame interval it already records, with no thread suspended, so each frame > 50 / 100 / 300 ms gets a lead stage. Built and dummy-tested 2026-10-22; **not run in game**.

## 1. What is measured

Per presented frame (`Frame_RenderAndPresent 0xEC9F00` with present = 1, the existing record), the interval from the previous presented frame's start to this one's start (`frameMs`) is split into:

| Column | Stage | Hook | Accepted when |
|---|---|---|---|
| `loadMs` | load-state tick | `LoadState_Update 0xEC6C30` | return address `0xECA9B9` (MainLoop call at `0xECA9B4`) |
| `idleMs` | inactive `Sleep(10)` | `App_IdleIfNotForeground 0x5887A0` | return `0xECA9ED` |
| `svcMs` | engine services main slot `+0x1C` | `ServiceList_Update` thunk `0x588E00` (-> `0x59ED20`) | return `0xECAA55` |
| `consumeMs` | sim-to-render consume (inside `svcMs`) | `FrameDb_ConsumeAndInterpolate 0x693780` | main thread |
| `captureMs` | video capture frame | `VideoCapture_RenderFrame 0x9DE140` | return `0xECAADC` |
| `rcbMs` | render callback, present = 0 (inside `captureMs`) | `Frame_RenderCallback 0xECA030` (called through `call [ebp+8]` at `0x9DE2E7`) | main thread |
| `beginMs` | `Renderer_BeginFrame 0x6EBB70` | entry | return `0xECAAEE` |
| `drawMs` | frame entry to Present entry (BeginScene, UI tree + 3D scene, capture grab) | computed | - |
| `presentMs` | `Device_EndSceneAndPresent 0x611760` (vsync, DXVK cap and queue, GPU) | entry | return `0xEC9FBA` |
| `postMs` | Present return to frame return: **S3SS Precise limiter** (its 9-byte patch at `0xEC9FBA`-`0xEC9FC2` runs there) and the inactive `SleepEx(30)` at `0xEC9FD1` | computed | - |
| `endMs` | `Renderer_EndFrame 0x6E8810` (`JobManager_Wait` on the frame jobs) | entry | return `0xECAB0C` |
| `otherMs` | `frameMs` minus the top-level stages: `GameClock_FrameTick`, `Canvas_ApplyPendingMode`, `0xC0FD60`, the loading-cap spin (iterations that skip rendering), and any stage left unhooked | computed | - |
| `consumes`, `tryFail`, `notReady` | consume calls; of them returned before `FrameDb_GetReadPair 0x593440` (TryEnter on `+0x48` failed, or `+0xF8` set: `0x6937B5` branch) and read a pair but never drained (newest frame not ready, `0x6937D9` branch) | `0x593440` counted inside the consume | main thread |
| `drains`, `msgs` | `FrameDb_DrainEvents 0x5937B0` calls and render messages run (its return value: the message counter at `[esp+0x10]` goes to eax at `0x5938FA`, markers not counted) | entry + return value | main thread |

All addresses, call sites and prologue bytes [verified, Steam 1.67 disassembly 2026-10-22]. Stage columns are inclusive as hooked; `freeze-report.ts` turns them exclusive (services minus consume, capture minus render callback). The top-level stages plus `otherMs` add up to `frameMs` exactly (dummy check). `presentMs` + `postMs` of frame N's call land in frame N+1's interval, which is the interval they lengthen.

## 2. Hook safety

- One CModule (`stage_enter` / `stage_leave`, stage id as listener data) next to the existing frame hook: no JS on the main thread, no thread suspended, no lock taken [verified, code].
- The call-site filter (return address) keeps each function's other callers out (`0x588E00`, `0x6EBB70`, `0x6E8810` have callers outside MainLoop; the scene window `0x7EB6D0` also calls Begin/EndFrame, docs/41 §2.1). Nested consume / drain / read pair / render callback are taken on the main thread only.
- A stage call that straddles a frame start (a present = 1 frame from the window-message path `0xECA2A0` nested in services) is clipped at that start, so a frame never gets more stage time than its interval.
- Every prologue is compared with the expected bytes before attaching. A mismatch (ts3native `[Verify]` `fdbConsume` detours `0x693780` when its `VF_COSTS` flag is on, docs/68 line `[Verify]`; any S3SS/S3PP or other session's detour) leaves that stage unhooked, logs `stage NOT hooked ...`, and writes `na` in its column; the frame hook itself still refuses the whole script on a mismatch as before. None of the 12 sites is patched by S3SS 1.6.3, S3PP or another ts3native section (docs/68 grep 2026-10-22); DXVK patches nothing in `TS3W.exe`. `hook-sites.ts --check` stays clean (it scans ts3native sources; this is a Frida harness script).
- Dummy test `tools/probe/frame-stages-test.py` (32-bit ping.exe, never the game; stubs with the game's shapes: thiscall `ret 4` frame, `ret 0x14` drain returning the count, nested present = 0 frame in the render callback, a second call site of the load stub, a second thread calling consume): **PASS** 2026-10-22 [verified]. Every column within its sleep +0.3..+3 ms, sum = `frameMs`, nesting, the second call site and the other thread ignored, consume counters for normal / TryEnter failed / not ready, refused stage `na` + logged, and unload while the main thread sleeps 2 s inside services (dummy alive, both threads looping).

## 3. Cost

Dummy loop with no sleeps, hooks on vs off: **~4.8 us per frame** for 13 hooked calls + the frame hook (17 ns -> 4.8 us per iteration) [verified, dummy]. At 30-60 FPS that is 0.01-0.03% of the main thread. In game the frame has the same 12-13 hooked calls (services, consume, read pair 1-3x, drain, begin, present, end, load, idle; capture only while capturing) [inferred: count from the code paths].

## 4. Output and report

- `<tag>-<secs>-frames.csv` gains `loadMs,idleMs,svcMs,consumeMs,captureMs,rcbMs,beginMs,drawMs,presentMs,postMs,endMs,otherMs,consumes,tryFail,notReady,drains,msgs` after the old five columns. Readers that map by header name (`tests-be.ts`, `hitch-ab.ts`, `freeze-report.ts`) are unaffected.
- `freeze-report.ts`: section **Slow frames by stage**: per exclusive stage, the number of real frames > 50 / 100 / 300 ms it leads, its ms in frames >= 100 ms (share) and its median per frame; frames >= 100 ms with consume skipped (TryEnter failed), no ready sim frame, or no consume call; then the worst frames >= 100 ms with lead stage, the next two stages and the consume counters. Same real/excluded split as the frame stats (`exclusion()` of `worst-hitches.ts`: first minute, profiler edge, repro steps, samplers, agent calls drop the frame). Older frames.csv files print "no stage columns". Exports `STAGES`, `frameStages`, `leadStage`; existing exports unchanged.

## 5. How to read it

| Lead stage | Points to | Next step |
|---|---|---|
| present | GPU, vsync, DXVK frame queue / cap | DXVK HUD frametimes, GPU load; not a main-thread CPU problem |
| after present | S3SS Precise limiter (or unfocused `SleepEx(30)`; check `fg`) | S3SS FPS cap settings |
| end-frame job wait | frame jobs not done: on `JobThread0` or the sim thread's unbudgeted pump (docs/41 P3) | sim stack at that QPC time: is the sim thread inside a render job |
| services / consume | main-slot services (job pump, world, scene update) / message drain under `+0x48` (docs/41 P2: `msgs` per frame) | `--mainstack` samples split by docs/41 §7 buckets |
| draw / begin frame | scene and UI draw, pre-passes | docs/46-48 render jobs, culling |
| other | loading cap (10 FPS gate, docs/41 P5) or an unhooked stage | check the log for `stage NOT hooked` |

`tryFail` > 0 in a slow frame = the sim held `+0x48` at that moment (a publish or triple-buffer sync, docs/41 S1); the main thread skipped the consume, it did not wait [verified, code: `TryEnterCriticalSection`].

## 6. Open

- Game run: one `bp-freeze-pan` style job with the profiler on, then `freeze-report.ts <tag>` (ledger §262).
- Frames from the window-message path (`0xECA2A0`) are recorded like any presented frame; their stage split covers only what ran since the previous frame start.

## 7. Addendum: the services stage by service (2026-10-22, ledger §264)

Measured first (`freeze-measure-2026-10-22-bp-freeze-pan`, `asi.0329`, BP, camera pan, ForegroundRender): of 643 real presented frames >= 100 ms, the services stage holds 85% of the time and leads all 18 frames > 300 ms; median 18.3 ms per frame. So the next split is inside `ServiceList_Update`.

### 7.1 The list and its walk [verified, Steam 1.67 disassembly]

- `0x588E00` loads the list object from `[0x11CE3F4]` and calls `ServiceList_Update 0x59ED20(realDt, gameDt)` (`ret 8`).
- `0x59ED20` returns at once when `[list+0xC]` is 0. Otherwise it walks the circular node list from `[list]` back to the list object. A node is called when its enabled byte `+0x1A` is set and flag bit 0 at `+0x14` is set. The call is `ecx = [node+8]` (the service), then `call [[ecx]+0x1C]` with the two floats. The call setup is at `0x59ED3D`, the call at `0x59ED57`, and the return at `0x59ED59`. A skipped node jumps straight to `0x59ED59`. Inside the function only `0x59ED31`, `0x59ED59`, `0x59ED5F` and `0x59ED60` are jump targets.
- Services are built by `Service_ctor 0x59ECC0` (60 call sites; base vtable `0xFDDE44`, stub `0x861C50` in `+0x1C`/`+0x20`). The table lists the 24 whose vtable `+0x1C` is not the stub, by scanning every ctor call site for the vtable stored into `[esi]`.
- Vtables and slots are [verified]. Names come from the heap/config strings next to the creator call, or from earlier docs [inferred]. Run order is the list order at run time; the probe logs it at install (`[frames] services (+0x1C, call order): ...`). Whether a given service's flag bit 0 is set is only known at run time.

| vtable | Name (column value) | `+0x1C` update | ctor | Known content |
|---|---|---|---|---|
| `0xFCBCCC` | MessageServer | `0x588890` | `0x5888B0` | `g_MessageServer` ProcessQueue (docs/61) |
| `0xFCBD34` | Canvas | `0x588A00` | `0x588980` | `Canvas_PumpMessages` PeekMessage loop (docs/55) |
| `0xFD9690` | Input | `0x598660` | `0x5987E0` | InputMan update (docs/55) |
| `0xFD9810` | JobManager | `0x599A10` | `0x599970` | main job pump, timeslice `[JM+0xCC]` (docs/36) |
| `0xFE514C` | CAS/SimService | `0x5F0E50` | `0x5F1450` | Sim builds and texture cache (docs/42) |
| `0xFE6B08` | CAS/TextureCompositor | `0x608630` | `0x607BD0` | compositor queue (docs/42, docs/67) |
| `0xFE6F08` | NPMessageServer | `0x60C5A0` | `0x60BFB0` | launcher pipe (docs/36) |
| `0xFF0070` | AnimatedTextureManager | `0x687C80` | `0x687C10` | `Renderer/AnimatedTextureManager` [inferred] |
| `0xFF7D84` | RenderSystem | `0x6E3620` | `0x6E3C60` | consume, scene update, condemned resources (docs/41) |
| `0xFFC420` | SwarmSystem | `0x71E640` | `0x71D680` | effects (docs/44) |
| `0xFFCF8C` | UTFWin | `0x723EE0` | `0x724310` | clipboard pump (docs/47) |
| `0xFFDF58` | ResourceChangeMonitor | `0x733D20` | `0x7337F0` | also a sim slot (docs/36 §3.3) |
| `0xFFEB10` | ResourceSystem | `0x7377F0` | `0x739120` | resource system service (docs/39) [inferred] |
| `0x1007BEC` | ScriptThread | `0x76B330` | `0x76B1B0` | `Scripting/Service/Thread`; pause/clock handshake (docs/36, docs/41) |
| `0x1015DF4` | ShaderSystem | `0x7A08C0` | `0x7A07A0` | docs/53 |
| `0x101C158` | UI/IME | `0x7E6840` | `0x7E5110` | docs/43 |
| `0x1029060` | Audio | `0x868FF0` | `0x868EE0` | audio manager (docs/35) |
| `0x102CCE0` | OnlineInformation | `0x883080` | `0x883040` | `Misc/Network/OnlineInformation` [inferred] |
| `0x10450DC` | Camera | `0x96DBD0` | `0x96B8A0` | camera manager (docs/107) |
| `0x1051C78` | VideoManager | `0x9D96D0` | `0x9D86C0` | movie player |
| `0x105DEE4` | OnlineEventListener | `0xA37E60` | `0xA3A270` | `Misc/Network/OnlineEventListener` [inferred] |
| `0x106BE6C` | ObjectDesigner | `0xB3A960` | `0xB38A90` | thumbnail requests, CAS part data (docs/42) |
| `0x107A358` | World/Service | `0xC7E3C0` | `0xC7E350` | `WorldManager::Update 0xC6D570`: lot scoring and streaming, SpeedTree wind (docs/28 §5, docs/34) |
| `0x1088F2C` | UI/ThumbnailManager | `0xD61670` | `0xD57D30` | request queues, render to texture (docs/43, docs/46) |

What should dominate in a BP camera pan [inferred, static]: **World/Service** first. Camera movement changes the lot scores, so lots stream in and out from `WorldManager_Update` on the main thread (docs/28 §5). Next come **RenderSystem** (scene update over newly visible objects, plus the consume), **CAS/SimService** and **CAS/TextureCompositor** (outfits and textures of newly streamed Sims; docs/71 found `0x7D80E0`, called from the CAS range, on 16.6% of BP camera_pan main stacks) and **ResourceSystem**. **JobManager** is capped by its timeslice (5 ms default), so it should not lead a 300 ms frame. A game run decides (I-167-6).

### 7.2 Probe [verified, dummy]

- `frame-times.js` checks all 68 bytes of `0x59ED20`. If any byte differs (Apex Radiance's developer-only Frame Profiler hooks this entry, docs/68 §1.5), the services split is logged as refused and its columns are `na`.
- When the bytes match, it adds two CModule probes (`Interceptor.attach(address, cm.fn)`: onEnter only, no thread suspended):
  - `0x59ED3D`: `mov ecx,[esi+8]` + `fld`, 7 bytes, no jump target inside. It pushes (node `esi`, service vtable `[[esi+8]]`, QPC).
  - `0x59ED59`: `mov esi,[esi]` + `cmp` + `jnz`, 6 bytes, no jump target after its first byte. It pops only when `esi` is the node on top of the stack, so a skipped node (jump, no call) is ignored.
- Only the outermost walk counts, and only inside the svc stage (`in_svc`) on the main thread. A nested walk (`0xB36D10` also calls `0x59ED20`) is inside its outer service. Time is clipped at the frame start like the stages.
- Per vtable (up to 64), time is summed per frame interval. At the frame start the top 3 go into the record: columns `svc1,svc1Ms,svc2,svc2Ms,svc3,svc3Ms` after `msgs`. The name comes from the table above, else `vt_<hex>`. A cell is empty when fewer than 3 services ran.
- `freeze-report.ts`:
  - **Services by service**: per service, the real frames > 50 / 100 / 300 ms it leads, its ms in frames >= 100 ms (share of frame ms), how many of those frames have it in the top 3, and its median when in the top 3.
  - An `other services` row holds services stage + consume minus the top 3.
  - The worst slow frames list gets a `top services ms` column.
  - The csv reader now keeps text cells.
  - shortcut: only the top 3 per frame are stored. If a 4th service matters, widen the record.
- Dummy (`tools/probe/frame-stages-test.py`, PASS 3 of 3) [verified]:
  - Setup: a list walk with the same shape (A, R calling consume, disabled S, D walking a second list with E through the same code).
  - Every frame lists R, D, A in that order, each within its sleep +0..+2 ms. S and the nested E are never listed. The top 3 sum to at most `svcMs`.
  - Changed walk bytes give `na` and a log line. The probes are removed on unload (bytes restored).
  - Unloading while the main thread sleeps 2 s inside service A leaves the dummy alive with both threads looping.
- Found on the way [verified, dummy]: Frida writes the 5 hook bytes without suspending the thread. A loop with no sleeps that is attached or detached can execute a half-written `jmp` (access violation at a stub entry +1 / +4). The unchanged HEAD test crashed this way 1 time in 6. The bench now attaches and detaches while the loop sleeps. In game each hooked site runs a few times per frame, so the window there is a few ns per frame [inferred].

### 7.3 Cost

Dummy, loop with no sleeps: 4.8 -> **6.7 us per frame** for 9 extra probe calls (~0.2 us each) [verified, dummy]. In game about 24 services run per frame, which is about 48 probe calls plus the top-3 pass over up to 64 entries: **~10 us per frame** [inferred from the dummy rate]. At 30-60 FPS that is 0.03-0.06% of the main thread.
