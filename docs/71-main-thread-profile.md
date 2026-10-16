# 71. Main-thread (render/UI) profile (PLAN gap 2)

Where the main thread (`MainLoop 0xECA960`, docs/41) spends wall time in Bridgeport (BP) and Isla Paradiso (IP), from 200 ms stack samples. Script: `node tools/harness/main-profile.ts [files...]` (reads only; `--self-check`). Names come only from `game-copy/analysis/functions.csv` or from existing docs; anything else is marked unknown.

## 1. Data

| Run (`probe-out/profile/<tag>-mainstack.jsonl`) | Main samples | Notes |
|---|---|---|
| `mainstack-2026-10-16-bp-mainstack` | 6 | **Not usable.** This was the run queued for gap 2 (`tools/harness/queues/mainstack.json`). It ran at 09:07-09:14 PHT, before the sampler fix `fbd4fa0` (09:42 PHT): slow `/simstack` calls starved the probe, so the simstack file is empty and the run has 6 main samples in 63 s [verified] |
| `verify-2026-10-15-bp-verify` | 1978 | repro `verify-sweep`, main-only sampling for the whole window |
| `verify-2026-10-15-ip-verify` | 1013 | same |
| `verify-2026-10-16-bp-verify` | 1966 | same |
| `verify-2026-10-16-ip-verify` | 1942 | same |

So this profile uses the four `verify` runs. They sample the main thread only (profile-world.ts repro mode), every 200 ms, for 405 s, over the sweep steps moodlet, outfit_change (bulk), cas_open/category/close, thumbnails, save_game and camera_pan. Step times come from `probe-out/queue/<tag>.log`.

Caveats:
- **The game window was not focused** in every run (the logs say "window NOT focused"). That turns on the `App_IdleIfNotForeground 0x5887A0` `Sleep(10)` on each frame, and it pushes "idle" up. In focused play that time goes to Present/DXVK pacing instead [inferred from docs/41 §1].
- The runs used `game-copy/ts3native.asi.verify` with `[Verify]` hooks. 2.9% of the leaves are inside `ts3native.asi` [measured].
- The stacks are scans with 24 frames. Leaves are exact. Callers can include stale return addresses, so inclusive shares are approximate [docs/20].
- **Main-thread frame times were not recorded** (`frame-times.js` is only loaded by tests-be.ts). So no list of frames over 16 or 33 ms is possible. Section 4 uses a proxy instead.

## 2. Share of main wall time by subsystem [measured]

Bucket = the marker nearest the leaf (table in `main-profile.ts`, from docs/41/36/39/40/42/44/47/52). DXVK frames above the first TS3W frame count as present/D3D. Each bucket is a share of all samples in that run.

| Run | idle (unfocused sleep) | present/D3D | UI draw | scene/culling | render-job wait | sim-render consume (frame lock) | resource | Swarm | CAS SimService | allocator | texture compositor | job pump | other |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| BP 10-15 | 34.7% | 22.8% | 3.9% | 3.9% | 2.0% | 0.9% | 7.2% | 2.6% | 2.3% | 4.2% | 0.6% | 0.4% | 14.6% |
| IP 10-15 | 42.7% | 23.7% | 4.4% | 2.3% | 3.7% | 0.7% | 5.3% | 3.2% | 1.4% | 2.5% | 0.7% | 0.4% | 9.1% |
| BP 10-16 | 36.6% | 18.2% | 2.4% | 2.9% | 3.3% | 0.9% | 8.7% | 1.9% | 3.2% | 4.9% | 0.3% | 0.4% | 16.3% |
| IP 10-16 | 45.0% | 18.7% | 5.4% | 2.4% | 2.6% | 0.5% | 5.4% | 2.5% | 1.4% | 2.4% | 0.2% | 0.2% | 13.4% |

Per sweep step, pooled across all four runs:

| Step | Samples | idle | present/D3D | UI draw | resource | CAS SimService | allocator | other |
|---|---|---|---|---|---|---|---|---|
| moodlet | 387 | 47.0% | 14.7% | 5.7% | 4.4% | 2.1% | 3.1% | 10.9% |
| outfit_change | 1166 | 40.8% | 19.2% | 4.3% | 6.0% | 1.8% | 2.5% | 13.7% |
| cas_open | 586 | 60.1% | 22.9% | 1.7% | 1.2% | 1.2% | 0.9% | 5.1% |
| cas_close | 463 | 36.5% | 28.3% | 3.9% | 5.8% | 1.7% | 2.8% | 12.3% |
| thumbnails | 391 | 28.4% | 22.5% | 6.1% | 7.2% | 3.3% | 5.6% | 19.2% |
| save_game | 1096 | 46.1% | 20.8% | 3.9% | 4.0% | 2.2% | 2.4% | 12.7% |
| camera_pan | 2519 | 30.7% | 18.5% | 4.0% | 11.0% | 2.7% | 5.5% | 17.2% |

Readings:
- BP keeps the main thread busier than IP: 35-37% idle against 43-45%. The extra BP time goes to resource work (7-9% against 5%) and the allocator (4-5% against 2.5%) [measured].
- Camera pan is the busiest step. Resource work is 11% of it (docs/39 block `0x72D470`-`0x72DBD0`, `DBPF_IndexCompressed_Find 0x4AA1B0`, `0x4AC9C0`). In BP, unknown `0x7D80E0` is on 16.6% of the camera_pan stacks (inclusive) [measured; function unknown].
- "other" (9-16%) is mostly wrappers with no marker: `Lock_Acquire 0x4E16F0` 1.3% and `Lock_Release 0x4E17B0` 1.2% as innermost named frame, `GeneralAllocator_Free 0x4E5920` 0.5%, and `ServiceList_Update 0x59ED20` self 0.4%.

## 3. Top functions (pooled, 6905 samples) [measured]

| # | Self | Address | Name (source) |
|---|---|---|---|
| 1 | 37.5% | 0x5887A0 | App_IdleIfNotForeground (docs/41) |
| 2 | 2.1% | 0x6FDFB0 | SceneObjMgr_FindObject (docs/48) |
| 3 | 2.0% | 0x4E16F0 | Lock_Acquire (CSV; 84% of these leaves are in OS wait) |
| 4 | 1.9% | 0x619540 | Texture_CreateD3D (docs/40) |
| 5 | 1.8% | 0x4E5E40 | GeneralAllocator_MallocCore (docs/25) |
| 6 | 1.7% | 0x4E17B0 | Lock_Release (CSV) |
| 7 | 1.3% | 0xEC9F00 | Frame_RenderAndPresent (docs/41) |
| 8 | 1.3% | 0x7347C0 | unknown |
| 9 | 1.3% | 0x72DBD0 | unknown (docs/39 resource/async-load block) |
| 10 | 1.2% | 0x6191D0 | unknown (texture range next to Texture_CreateD3D) |
| 11 | 1.2% | 0x4AA1B0 | DBPF_IndexCompressed_Find (docs/39) |
| 12 | 1.1% | 0x72FFF0 | unknown |
| 13 | 1.1% | 0x7D80E0 | unknown |
| 14 | 1.0% | 0x4AC9C0 | unknown (part/index key enumeration, docs/42) |
| 15 | 1.0% | 0x91BAB0 | unknown |
| 16 | 0.9% | 0x5492E0 | List_Find4Key (docs/25) |
| 17 | 0.7% | 0xD726F0 | ScriptHost_HandleMapLookup (docs/45) |
| 18 | 0.7% | 0x60EAD0 | Device_ApplySamplerAndRenderStates (docs/37) |

| # | Inclusive (approx.) | Address | Name (source) |
|---|---|---|---|
| 1 | 43.6% | 0x5887A0 | App_IdleIfNotForeground |
| 2 | 13.2% | 0xECA960 | MainLoop (docs/41) |
| 3 | 6.2% | 0x59ED20 | ServiceList_Update (docs/41) |
| 4 | 6.1% | 0x7D80E0 | unknown (calls Lock_Release; callers 0x5CF6F0, 0x5C9C40 unknown) |
| 5 | 4.9% | 0x588E00 | ServiceManager::Update (docs/24) |
| 6 | 4.5% | 0x50B270 | WindowManager_Draw (docs/52) |
| 7 | 4.3% | 0x50EB20 | UTFWin_DrawDisplayLists (docs/47) |
| 8 | 4.0% | 0x4E5E40 | GeneralAllocator_MallocCore |
| 9 | 3.8% | 0x5DA0C0 | unknown |
| 10 | 3.6% | 0x5F0E50 | SimService_Update (docs/42) |
| 11 | 3.3% | 0x608630 | TextureCompositorQueue_Update (docs/67) |
| 12 | 2.8% | 0x7EB6D0 | SceneWindow_Draw (docs/47) |
| 13 | 2.8% | 0x5EC6D0 | unknown (called by SimService_Update in 97% of its samples) |
| 14 | 2.6% | 0x71E640 | Swarm service (docs/44) |
| 15 | 2.6% | 0xEC9F00 | Frame_RenderAndPresent |
| 16 | 2.3% | 0x6F1D90 | Renderer_DrawMainPass (docs/46) |

## 4. Spikes

There is no frame-time data (section 1), so this section looks for long blocks instead: 3 or more consecutive non-idle samples (about 400 ms or more) whose innermost named frame stays the same.
- There are 9 such blocks in about 6900 samples, and none is longer than 4 samples (about 600-800 ms). All of them are on the present side: `Frame_RenderAndPresent 0xEC9F00`, or DXVK waits with no TS3W frame (`KERNELBASE` wait under `d3d9.dll`), plus one under `Lock_Release` during camera_pan [measured].
- 6 of the 9 blocks are in BP 10-15 during cas_open, cas_category and cas_close (14:19:25-14:20:10Z). The others are one in BP 10-16 camera_pan (00:10:48Z) and one in IP 10-16 save_game (00:18:17Z).
- No block sits in SimService, resource, UI draw or the frame lock [measured].
- A 16 or 33 ms frame spike is shorter than the 200 ms sample interval, so these runs cannot see one. That needs `frame-times.js` loaded in a `--mainstack` run, focused, with sample times matched to frame times.

## 5. Claims checked

| Claim | Verdict |
|---|---|
| docs/41 §1: the main thread never waits on the sim. The FrameDatabase lock `+0x48` is a TryEnter, and it skips the consume if the lock is busy | **Consistent.** The sim-render consume (`FrameDb_ConsumeAndInterpolate 0x693780`, `FrameDb_DrainEvents 0x5937B0`) is 0.5-0.9% of main time, with no wait leaves and no long blocks [measured] |
| docs/41 §1: the main thread waits on render jobs (`Renderer_EndFrame 0x6E8810`, `JobManager_Wait`) | **Weak in this data.** `0x6E8810` is on 0% of stacks and `Renderer_BeginFrame 0x6EBB70` on about 1%. The render-job bucket (2-3.7%) comes mostly from `Renderer_DrawMainPass 0x6F1D90`. These waits are short or rare when the window is unfocused [measured; focused run needed] |
| docs/41 §1: Present is the frame limiter | **Not testable unfocused.** The idle `Sleep(10)` (35-45%) does the pacing here. present/D3D is 13-29%, and DXVK waits are about 4% of samples [measured] |
| PLAN (20): cap SimService time slicing (`[SimService+0x139]`) against main-thread hitches in bulk outfit builds | **Low priority on this data.** `SimService_Update 0x5F0E50` is on 6.5% (BP) and 2.8% (IP) of main stacks during the bulk outfit change, mostly through `0x5EC6D0`. It has no run of 2 or more consecutive samples. CAS model build `0x5E2050` (gap 9, "160 ms") is on at most 2.1% of samples, and never in 2 consecutive samples. Whether a cap helps depends on frame times, which these runs did not record [measured; verdict inferred] |

## 6. Next

1. Rerun `mainstack` (fixed sampler, focused window) with `frame-times.js` loaded, to get spikes over 16 and 33 ms and the true Present share.
2. Name `0x7D80E0` (6.1% inclusive, 16.6% in BP camera_pan), `0x5DA0C0`, `0x5EC6D0`, `0x7347C0` and `0x72FFF0` in Ghidra, then add them to the symbols CSV.
