# 367. Atlas world-view pan stutter: measure first (2026-11-28, built + run once per arm, one sample each)

Report (Rodney, 2026-11-28): zoomed out on the stitched 4096 m world `AtlasCawStitchedWN*_4096`, panning the camera stutters a bit. Mouse input or real frame stutter? No fix before the numbers.

## 1. What was built
- Agent command `camera_line {x0,z0,x1,z1,speed?,height?,pitchDeg?,seconds?}` (ts3agent, `CameraLineHandlers.cs`; also `{status:true}` / `{stop:true}`). No mouse: the camera target ping-pongs on a line at `speed` m/s (default 200) by wall clock, camera 600 m up at 60 deg, 60 s, with 5 s still before and after. It moves only the camera (`SetPositionAndTarget`); no Sim, lot or save is touched, so no household exemption applies.
- Status gives the three segments in the `camera_sweep` shape (so `camera-sweep-report.ts` and `profile-world.ts` split `frames.csv` and Apex `FrameTimeline.csv` by segment), plus `stats`: per set `dt` (sim-thread ms between camera sets) p50/p99/max, step in metres p50/p99/max, and the gap between the position set and the position read back (the game clamping the camera).
- Verdict rule: real stutter = a frame over 2x the median (Apex rule column `apexDefHitches`, also >25/50/100 ms counts) or uneven steps (`dt` p99 or step p99 over 2x p50) during `pan`. Otherwise it is input or mouse smoothing. Camera steps are wall-clock exact, so uneven steps mean the sim thread stalled between sets; the render side is the frame series.
- Repros `atlas-pan-line-4096` (z=2048, x 200..3900) and `atlas-pan-line-sv` (control, vanilla Sunset Valley, 150..900 on z=512; the SV extent is [inferred]). Queue `atlas-pan-stutter-1128.json`: control first, then stitched. Both are `run.profile` with `loadWorld`, 4 min, correctness + measurement, one sample each, needs a quiet machine. Dry-run OK.
- Build `ts3agent.package.pan1128`. Harness change on this branch: `profile-world.ts` and `camera-sweep-report.ts` accept `camera_line` (merge only between game runs). No native counters were added: existing frame series, Apex timeline and the agent stats are used. Cause correlation per frame (lot streaming `0xC6C290`, imposter and texture loads, GC) is the follow-up if a hitch shows: use `frame-lead.ts` and the `[Atlas]` / stream lines of the same window.
- Not built: synthetic mouse-drag arm (no SendInput helper in the harness [verified by grep of tools/harness]); Atlas on/off arm on 2048.


## 1b. Result (2026-11-28, one 60 s pan per arm, not a quiet-machine timing, queue atlas-pan-stutter-1128, both PASS)
| arm | pan frames | p50 ms | p99 | p99.9 | max | > 50 ms | > 100 ms | camera dt max (sim thread) |
|---|---|---|---|---|---|---|---|---|
| Sunset Valley (control) | 3309 | 16.8 | 37.3 | 78.7 | 424 | 10 | 1 | 94 ms |
| stitched WN 4096 | 3098 | 16.7 | 37.1 | 94.8 | 2611 | 22 | 3 | 4547 ms |
- Verdict: REAL stutter on the stitched world, not only input [verified, measured]. Median and p99 equal the control; the tail is worse: two render freezes of 2.4 to 2.6 s (at 4.2 s and 56 s into the pan), 22 frames over 50 ms (control 10). Camera steps stay even by wall clock; the sim-thread camera task saw a 4.5 s gap, and the sim hitch log shows 2.9 to 4.5 s Sim / RoleMan script ticks (`threaded`, `locomotion`) at 28, 38, 42, 52 s that are NOT the render freezes (different times).
- Top cause: not decided [inferred]. Sim script ticks stall the sim thread but not the render frames; the two render freezes (4.2 s, 56 s) match no counter in this run (no GC mark, no route wait). Candidates left: lot streaming / imposter loads at the pan start and far end, or the Remix bridge present. Needs the next run with `WorldManager_ScoreAndStreamLots` (0xC6C290) and texture-load counters per frame (not built; new counters go behind an off-by-default switch).
- Caveat [verified]: the game clamps the camera. Set-read gap is 558 to 582 m: the requested 600 m height is not reached (distEnd 400 m SV, 110 m stitched), so this is not the player's far world view. The stitched arm ran at 110 m camera distance, which streams more than a far view. Next run: use the `camera` command's far view pose or a clamp-aware height.


## 1c. Rerun in the game's own world view (2026-11-28, build ts3agent.package.pan1128b, map_view step + camera_line keepOffset, Documents backed up first)
- Camera: map view gives a 750 m camera distance (offset 430 / 614 / 0); set-read gap now 9 to 29 m (was 560 m): the game no longer clamps the pan [verified].
- Counters used (no new native build: existing ones cover the list [verified]): frames.csv stage columns (`svcMs`, `drawMs`, `presentMs`, `loadMs`), `[LotStream]` Enabled/Log/Thumb (lot adds / removes / score per call), sim hitch log.
| arm | pan frames | p50 | p99 | p99.9 | max | > 50 ms | camera dt max |
|---|---|---|---|---|---|---|---|
| Sunset Valley | 1635 | 17.4 | 41.1 | 94.5 | 96 | 8 | 109 ms |
| stitched WN 4096 | 2489 | 16.9 | 33.3 | 43.0 | **2671** | 2 | 4844 ms |
- One freeze of 2.67 s at 47.9 s into the stitched pan. Its frame: `svcMs` 2657, `drawMs` 0.6, `presentMs` 0.4, `loadMs` 0 [verified]. The sim hitch log has a 4.06 s tick at the same time (48.4 s) in a Sim ScriptProxy task (`threaded` bucket, 204 ms route search), and the camera task on the sim thread saw the same 4.8 s gap [verified].
- Lot streaming: 0 adds and 0 removes during the pan (16895 adds all at load, 0 removes; `lotScoreUs` 374 ms over 2928 calls in the whole job) [verified]. Present 0.4 ms. No GC mark in the hitch.
- Verdict: the stitched-world freeze is a sim-thread stall inside one Sim script task that the render frame waits for (service phase), NOT lot streaming, imposter or texture loads, GC, or the Remix present [verified for this sample; one sample]. The earlier second freeze (56 s, 2.4 s) in the clamped-camera run was also in `svcMs`. Which Sim and which call is not known: the sim stack file of the run was empty.
- Plan (our lane): (1) rerun with `WatchStalk` / sim stall stack capture on to name the Sim task (static: `Sim.ScriptProxy` 4 s, 204 ms route search inside); (2) a stall that long on an unseen Sim is covered by the existing 2 s ceiling work (docs/242 style) once the task is named, with the household / selected-Sim exemption; (3) the pan itself (render side) is clean, so nothing for Apex.


## 1d. Stall stack rerun (2026-11-28, queue atlas-pan-stall-1128, profile.taskstall 1500, stitched world, map view)
- The pan froze again: max frame 2625 ms (p50 16.8, p99 33.0; 3 frames over 50 ms) [verified]. The `/taskstall` watch caught 40 sim-thread stalls of 2.1 to 6.0 s in the job, 3 inside the pan window (05:29:02, :07, and one just after); the watch's own sampling stretches them, so the unsampled 2.6 to 4.8 s fits [inferred].
- Every stall is ONE native route-engine call on the sim thread under a `ScriptCore.ScriptProxy.Simulate` task [verified from the sampled managed stacks]: `RouteManager_AddPrivateFootprint` / `RemovePrivateFootprint` (boat `BoatRoutingComponent.EnableDynamicFootprint` / `ForceUpdateDynamicFootprint` / `DisableDynamicFootprint`, from `DoSingleRoute` or `OnStartup`), `Route_GetOption` (from `Sim.RouteToSlot` / `Boat.RouteOrig`), `Route_SetAGS`, `RouteManager_RoutingBlockerCheck2d` (Piano / portal lane check in `DoSingleRoute`). Not a RoleMan tick, not a stuck-Sim check, not a route search; a few seconds inside one route-manager call means the call waits (route lock or worker) [inferred, same family as the docs/140 / docs/196 route-job waits].
- Boats: the stitched world carries water and boats (docs/325, houseboat docs); private footprints on a large route graph are the most frequent stall frame. Household or NPC: the capture records class and method only, no Sim id, so not named [not verified]; the pan camera is far from any household Sim, so most are probably NPCs [inferred].
- Plan: (1) add the Sim id, household flag and the call's lock wait to the taskstall record (our lane, diag only, default off) and rerun; (2) the existing routing wait reducers apply, with the household / selected-Sim exemption: cap or defer boat dynamic footprint updates for unseen NPC boats (docs/89 §15 audit of AddPrivateFootprint readers first, static); (3) compare the same pan on the stitched world with Atlas P0 off to see whether the large route graph (4096 m) makes the calls slower than on Sunset Valley (the control had no such stall: max 96 ms in 1635 frames).


## 1e. Measured cause: the route worker holds the routing lock for seconds (2026-11-28, measurements only, no fix; Rodney: accepted until the route tiles)
Run: queue `atlas-pan-stall-1128` with `taskstall` 1500 ms and the new `routelockDiag` (`--routelock-diag`: the probe's `/routelock` hold / wait per thread over the whole 4 min window, `<tag>-routelock.json`), map-view pan, stitched WN 4096 vs Sunset Valley control, no household active (all Sims NPC) [verified: game_state `active_household` null].
| lock `world+0x918`, 4 min window | sim waits (total / max) | sim holds (total / max) | route worker holds (total / max) | plans (n / p50 / p99 / max) |
|---|---|---|---|---|
| Sunset Valley | 2.1 s / 114 ms | 36 s / 10.0 s | 57 s / 202 ms | 3903 / 2.9 ms / 72 ms / 196 ms |
| stitched 4096 | **40.2 s / 5.07 s** | 4.5 s / 3.1 s | **172.7 s / 6.03 s** | 609 / 77 ms / 4.7 s / 6.0 s |
- Cause [verified, measured]: on the stitched world the route worker runs plans of seconds (p99 4.7 s, max 6.0 s, 539 of 609 plans on the worker thread) while holding the routing lock; the sim thread blocks on it for up to 5.1 s (40 s in 4 min). Sunset Valley's plans max out at 196 ms. This matches the sampled stalls: every stalled call (`RouteManager_AddPrivateFootprint` / `RemovePrivateFootprint` on boats, `Route_GetOption`, `Route_SetAGS`, `World_GetConnectedRoomsImpl`) takes the same lock [verified, Atlas docs/064, docs/89]. The footprint calls are victims, not the cost: a boat cap would only move the wait to the next lock taker. The earlier "rtLock" hitch buckets read 0 (the native waits there are keyed sites only); the Frida lock tool sees the wait.
- How often (pan windows of 60 s): stalls over 1.5 s on the sim thread: stitched 8 and 11 per minute in two runs (max 6.1 s with the sampler on), Sunset Valley 0; render freezes over 1 s: one of 2.6 s in 3 of 4 stitched pans, none in the control (max frame 442 ms). The freeze is the frame that waits for the sim service (`svcMs`), see §1c.
- Not captured: the holder Sim id and household flag (the lock tool has no Sim identity; a route plan's Sim is not read). With no household active every holder is an NPC in these runs; with a household active, unknown counts as household. Hold start times are in `<tag>-routelock.json` only as aggregates.
- Fix: planned, not built. The route tiles of Atlas docs/046 / docs/064 split the 4096 m graph into squares so a plan holds only its tile's lock; until then the stall is accepted. No job cap, no boat cap (Rodney's call). Tool: `tools/harness/taskstall-report.ts` groups a stall file by innermost call.

## 2. Plan if real (nothing built; [inferred] until the numbers)
1. Lot streaming: spread `WorldManager_ScoreAndStreamLots` (`0xC6C290`) work over frames with a per-frame budget; prefetch lots and imposters ahead of the pan direction.
2. Move stream decode and texture upload work off the sim/render thread (our lane; docs/278 stream priority).
3. GC timing: schedule collections off camera motion where the counters show GC in the hitch frames.
4. Renderer-side (Apex area, proposal only): present-time spikes in the RTX Remix bridge, imposter texture uploads.

## 3. NVIDIA Smooth Motion
- Driver-level frame generation (AI, no game integration), works with DirectX 11/12 and Vulkan [verified from secondary reports, [Tom's Hardware](https://www.tomshardware.com/pc-components/gpus/nvidia-adds-vulkan-compatibility-to-smooth-motion-frame-generation-complete-with-a-boost-for-emulators), [Vulkan.org](https://vulkan.org/news/auto-23676-574fc4d693c51106906cc0287881e870)].
- GPU support: RTX 40 and RTX 50 only; the RTX 30 series is not supported, so Rodney's laptop GPU cannot use it [community-reported by several sources, no official NVIDIA support page found].
- With the RTX Remix bridge: the bridge is a Vulkan path, so it would be eligible on an RTX 40/50 [inferred, untested]. It would not hide sim-thread stutter anyway (generated frames repeat the sim state).
