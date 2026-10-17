# 62. IC thread safety: merged list and what it means for autonomy (T-IC-c)

Merges the two static IC safety lists (`docs/data/ic-safety-a.csv`, T-IC-a: Simulator, World, Objects, Slots, Route*, Queries, Animation, LookAt*; `docs/data/ic-safety-b.csv`, T-IC-b: everything else) into **`docs/data/ic-safety.csv`**, one row per internal call (IC), with one verdict meaning. It then reads the result against the autonomy path (docs/59 §1.4), the second managed thread design (docs/30, docs/56, docs/57) and the runtime facts from the verify runs (`docs/data/results/2026-10-15-verify-findings.md`). Static only, no game run.

Tags: [verified] = disassembly, decompiled code or measurement, as cited; [inferred] = reasoning, or a mechanical scan verdict not checked by hand. A scan verdict is a rule applied to a call graph. Its `confidence` column says how much of the graph the scan saw. It is not a hand check of each IC.

## 1. Merge

### 1.1 One verdict meaning

The two parts used the same four words with different meanings. Part A called a racy read UNSAFE, and part B called the same thing PROXY. Part B called a yield or a managed callback UNSAFE, and part A called a managed callback PROXY. The merged `verdict` column uses part B's meaning, because it separates what a forwarder can do from what it cannot:

| Verdict | Meaning in `ic-safety.csv` | Count |
|---|---|---|
| **SAFE** | A second managed thread may call the IC directly. No sim-mutated native state, no lock, no post and no unresolved virtual call in the closure. Mostly empty stubs and getters of init-time config. docs/30 P1-P4 and the worker rules still apply (§2) | 159 (82 after §8) |
| **PROXY** | Reads or writes sim-owned native state with no covering lock. A worker must forward it to the sim thread: `proxy` = `sync` for a value return, `queued` for `void`, `sync-or-queued` where part B did not record the return type | 1,502 (1,587 after §9) |
| **UNSAFE** | Not forwardable as a plain native call. It yields or waits on the calling script task, reads the running task id, or re-enters managed code synchronously (`World_FireEvent`, `ScriptHost_InvokeDelegate`, `mono_runtime_invoke`, managed object registration). Also Mono reflection. It can only run inside a sim-thread script task | 62 (83 after §9) |
| **MAIN** | Device work on the calling thread with no owner check. It is already a race from the sim thread (docs/43). Needs a hand-off to the main thread, not a proxy | 6 |

Mapping applied, per row:

| Source verdict | Merged verdict |
|---|---|
| A `UNSAFE` (unlocked read of sim state) | PROXY, `sync` |
| A `PROXY` with `callsManaged = yes` (12 rows: World_FireEvent, InvokeDelegate, RegisterManagedObject, host `+0x04` object create) | UNSAFE |
| A `PROXY`, other | PROXY (`queued` when part A recorded `ret=void`, else `sync`) |
| A `SAFE`, `MAIN`; all part B verdicts | unchanged |
| Hand overrides, A rows (part A had no yield or task-id rule, so part B's rule is applied by name) [inferred] | `Simulator_SleepImpl` (yields), `Simulator_GetCurrentTaskImpl` (task id, host `+0x118` in part B's rule), `Simulator_SetYieldingDisabledImpl` (flag on the running task): UNSAFE |

`srcVerdict` and `srcConfidence` keep the original values, `source` says which part the row came from (`a`, `b`, `a+b`), and `note` starts with the reason for any change. `confidence` is on one scale: part B's high/medium/low as is; part A `verified` = high, `inferred` = medium when the closure is 40 functions or fewer with at most 4 unresolved virtual calls (part B's medium rule), else low.

### 1.2 Counts and the Slots overlap

- Rows: **1,729** = 672 (A) + 1,073 (B) - 16 overlap. Sources: 656 `a`, 1,057 `b`, 16 `a+b`.
- **Slots overlap:** all 16 `Slots_*` ICs are in both parts. After normalisation the verdicts agree on all 16 (14 PROXY, 2 UNSAFE: `Slots_CanAttachToSlotImpl` / `WithOffsetImpl`, `World_FireEvent` in both scans). No conflict. The kept row is the higher confidence one; on a tie, part B (depth 6 instead of 4, and its rules match the merged meaning). The note says "both parts: a .../..., b .../...; kept ...".
- Confidence: 356 high, 613 medium, 760 low.
- Proxy mode of the 1,502 PROXY rows: 693 sync, 252 queued, 557 sync-or-queued.
- **Not covered: the `SACS` class** (state machines). T-IC-a's list does not include it and T-IC-b excluded it. The binding table has 1,758 ICs and the merge has 1,729, so about 29 ICs (very likely all SACS) have no row [inferred from the counts]. See §6. **Update 2026-10-16:** confirmed, exactly 29 `SACS_*` bindings; now scanned and added (source `c`), see §7.

### 1.3 Per namespace

| Class | ICs | SAFE | PROXY | UNSAFE | MAIN |
|---|---|---|---|---|---|
| World | 372 | 3 | 353 | 14 | 2 |
| CASUtils | 205 | 11 | 183 | 8 | 3 |
| SocialFeatures | 205 | 51 | 154 | 0 | 0 |
| UIManager | 110 | 1 | 107 | 2 | 0 |
| Route | 96 | 0 | 96 | 0 | 0 |
| GameUtils | 86 | 29 | 52 | 5 | 0 |
| UserToolUtils | 66 | 8 | 54 | 4 | 0 |
| RouteManager | 62 | 2 | 59 | 1 | 0 |
| DownloadContent | 61 | 4 | 55 | 1 | 1 |
| CameraController | 44 | 4 | 40 | 0 | 0 |
| OnlineFeatures | 39 | 6 | 32 | 1 | 0 |
| ThumbnailManager | 39 | 2 | 25 | 12 | 0 |
| LookAtTask | 32 | 0 | 32 | 0 | 0 |
| Simulator | 29 | 2 | 22 | 5 | 0 |
| StreamHost | 27 | 2 | 25 | 0 | 0 |
| Audio | 25 | 0 | 25 | 0 | 0 |
| Animation | 22 | 1 | 21 | 0 | 0 |
| DeviceConfig | 22 | 10 | 12 | 0 | 0 |
| Objects | 21 | 0 | 21 | 0 | 0 |
| VisualEffect | 20 | 0 | 20 | 0 | 0 |
| LoadSaveManager | 17 | 5 | 10 | 2 | 0 |
| Slots | 16 | 0 | 14 | 2 | 0 |
| VideoRecorder | 13 | 0 | 13 | 0 | 0 |
| RouteEvent | 11 | 0 | 11 | 0 | 0 |
| StopWatch | 11 | 0 | 11 | 0 | 0 |
| Queries | 10 | 0 | 9 | 1 | 0 |
| EATrace | 8 | 8 | 0 | 0 | 0 |
| CacheManager, DataFactory | 7 each | 0 | 7 each | 0 | 0 |
| LocalizedStringService | 7 | 4 | 3 | 0 | 0 |
| ProfilerUtils | 5 | 5 | 0 | 0 | 0 |
| CommandSystem, DebugDraw | 5 each | 0 | 5 each | 0 | 0 |
| AutomationUtils | 4 | 1 | 3 | 0 | 0 |
| CTProductModularObject, EAText | 4 each | 0 | 4 each | 0 | 0 |
| EventQueue | 2 | 0 | 0 | 2 | 0 |
| Reflection | 2 | 0 | 0 | 2 | 0 |
| NameGuidMapService, ScriptProxy, WeatherControl | 2 each | 0 | 2 each | 0 | 0 |
| LookAtMgr, Random | 1 each | 0 | 1 each | 0 | 0 |
| **Total** | **1,729** | **159** | **1,502** | **62** | **6** |

UNSAFE by cause: 33 yield, wait or task context (12 ThumbnailManager, 8 CAS build/scene/outfit, 5 GameUtils waits, 2 EventQueue, `LoadSaveManager_SaveWorld`, `OnlineFeaturesHost_WaitForDownload`, `UIManager_CaptureSceneAsImage`, 3 Simulator); 27 synchronous managed callbacks (14 World lot and object ICs, 4 UserToolUtils, 2 Slots, `Simulator_AddObjectImpl` / `CreateObjectImpl`, `Query_GetObjectsOverlappingObject`, `RouteManager_AddDiscourageFootprint`, `UIManager_GridGenericImpl`, `LoadSaveManager_LoadGame_Impl`, `DownloadContent_ImportLotContentsImpl`); 2 reflection.

MAIN: `CASUtils_AddObjectToBabyMakerSceneWindowImpl`, `CASUtils_IsSceneLoadedImpl`, `CASUtils_SimBuilderShareOutfitImpl`, `DownloadContent_SaveCustomSim`, `World_ObjectDesigner_ShareDesignPresetImpl`, `World_SetLightingValuesImpl`.

## 2. Safe list: what a second managed thread may call directly

**82 ICs** since the 2026-10-16 rescan (§8; 159 at the merge; `verdict = SAFE` in the CSV; 81 high and 1 medium confidence) [inferred: scan verdicts on closures the scan saw whole; an unbounded closure dump of all 82 found no unresolved code and no lock, §8]. By group:

- **Logging and profiling stubs (21):** all 8 `EATrace_*`, all 5 `ProfilerUtils_*`, `GameUtils_AddOrConfigureLogReporter` / `AddOrConfigureProfiler` / `DumpProfilerScopeAverages` / `ResetProfilerPeaks` / `ResetProfilerScopeAverages`, `GameUtils_BeginLoadEvent` / `EndLoadEvent` / `ResetLoadEvents`. `DumpMemoryReport` / `DumpProfilerReport` are UNSAFE since §8.
- **Init-time config getters:** `DeviceConfig_*` (10), all 8 `UserToolUtils_*` price scales, `GameUtils_GetProductFlags`, `IsPreExistingInstallation`, `GetModFilesCount`, `GetGameTimeScaleImpl`, `GetGameTimeSpeedLevel`, `GetCurrentFps`, `GetFrameNumber`, `GetCurrentMemoryUsage`, `IsCallstackProfilingEnabled`, `Camera_GetControllerType` / `GetCustomFlyThroughIndex` / `IsHorizontalRotationInverted` / `IsVerticalRotationInverted`, `ThumbnailManager_IsCheatSaveSimFaceEnabled` / `SetCheatSaveSimFace`, `DownloadContent_IsAnyExportBinPackageHiddenImpl` / `IsCustomContentEnabled` / `ShutdownImpl`.
- **Online and social stubs (15):** 12 `SocialFeaturesHost_*` counters and flags (`GetID`, `GetJobCount`, `GetNumberOfBadges`, ...) and `OnlineFeaturesHost_IsLoggedIn` / `IsSims3ServiceUnreachable` / `IsStatusUpdateComplete`. The other 39 social and 3 online ICs take the social host's member lock or write its state: PROXY since §8.
- **Other stubs:** `GameUtils_UpdateLocalAutonomyRadiusDisplay`, `LoadSaveManager_GetCurrentGameGuid_Impl`, `StreamHost_Flush`, `Animation_SetMissingAnimationCallback`, `RouteManager_GetSteeringObjectRadius` / `SetCarSpeedGlobalMultiplier`, `Simulator_DidSecuritySystemFailImpl`, `World_ChimneyCapObjectFromFireplaceObjectImpl`, `World_GetLevelHeightImpl`, `World_ObjectDesigner_SaveCurrentPresetAsXmlImpl`.
- The CAS tattoo template helpers, the cheat and debug-option ICs, the passport and challenge data load and save ICs and the `LocalizedStringService` ICs were SAFE only because the scan never read their bodies (§8). They are PROXY now.
**None of the 82 (or of the 159 before §8) is on the autonomy path** (§3). For autonomy the safe list is empty. It helps only a worker that logs or profiles.

Conditions on every SAFE call from a worker [verified rules from the cited docs]:
1. docs/30 P1-P4 installed; the GC lock covers any managed allocation the IC's wrapper makes (none of the 159 allocates in its native body).
2. Warm-up: call each IC once on the sim thread first, so the trampoline thunk hash `[0x11F4128]` is filled there (docs/56 §3: it is written without a lock on a signature's first call).
3. docs/56 lists an EA IC nesting counter `[0x11F2D84]` (non-atomic `add`/`sub` in 17 EA functions) and makes "no EA internal call" a worker rule. Whether any of the 159 bodies or their transition touches it was not checked (§6). Until it is, Verify mode should snapshot the counter around worker runs, as docs/56 says.
4. "SAFE" is about the native side only. The managed wrapper must not touch game objects (docs/30 §5 table).

## 3. Proxies for autonomy, ranked

The 21 ICs of docs/59 §1.4 plus `Objects_IsValid` and the three `LocalizedStringService_GetLocalizedStringBy*` ICs that the tooltip path reaches. Hot set (per tested or scored pair) first, then per search, then conditional.

| Rank | IC | docs/59 frequency | Verdict | Proxy | Conf. | What a worker should do instead [inferred] |
|---|---|---|---|---|---|---|
| 1 | `Slots_GetParentIdImpl` | several per pair (parent walk in every Test, every `LotCurrent` / `InUse`) | PROXY | sync | medium (a+b) | snapshot parent id per object on the lot |
| 2 | `Objects_GetPosition` | 3+ per scored pair | PROXY | sync | high | snapshot position per object |
| 3 | `World_IsPositionOutsideImpl` | 1+ per scored pair | PROXY | sync | **low** | snapshot an "outside" flag per object |
| 4 | `World_IsOnRoofImpl` | 1 per Test reaching the special-case tests | PROXY | sync | high | snapshot an "on roof" flag per object |
| 5 | `Simulator_GetTaskImpl` | per search + per meta object (`HasBeenDestroyed`), per real parent in Test | PROXY | sync | medium | snapshot a "destroyed" flag per object |
| 6 | `Query_GetObjectsOnLot` | 1 per local search | PROXY | sync (returns a managed array) | high | the snapshot is this list |
| 7 | `StopWatch_RestartStopwatch` | 1 per meta search + 1 per yield | PROXY | sync-or-queued | medium | not needed: a worker does not yield (§4) |
| 8 | `StopWatch_GetElapsedTimeFloatStopwatch` | every 5th meta object | PROXY | sync | medium | not needed (same) |
| 9 | `World_IsObjectOnScreenImpl` | at most 1 per Sim per 60 ticks | PROXY | sync | medium | keep the LOD decision on the sim thread, before the snapshot |
| 10 | `Random_NextDoubleImpl` | 1-2 per search | PROXY | sync (returns double) | medium | worker-owned RNG seeded per search by the sim thread (docs/27) |
| 11 | `GameUtils_GetWorldType`, `GameUtils_GetWorldName` | per pair with a world restriction | PROXY | sync | medium | constant per world: snapshot once per load |
| 12 | `World_GetLotLocationImpl` | Seasons rain/hail: per outdoor pair | PROXY | sync | **low** | snapshot only while it rains or hails |
| 13 | `World_HasAnyRoofOrFloorAbovePosImpl` | rain/hail/deep snow: per outdoor pair | PROXY | sync | **low** | same |
| 14 | `World_GetSunsetTime`, `World_GetSunriseTime` | vampire actor: per outdoor pair | PROXY | sync | high | snapshot once per search |
| 15 | `World_GetSeaLevelImpl` | rare (diving lots) | PROXY | sync | high | constant per world |
| 16 | `World_ObjectGetHiddenFlags` | rare | PROXY | sync | high | snapshot per object |
| 17 | `LocalizedStringService_GetLocalizedStringBy*` (3) | per failing pair (tooltip) | PROXY | sync, allocates a string | low | skip: autonomous tooltips are never shown (docs/59 C7) |
| - | `Objects_IsValid` | Sim task, not the search | PROXY | sync | high | stays on the sim thread |
| - | `Simulator_GetCurrentTaskImpl`, `Simulator_SleepImpl`, `Simulator_AddObjectImpl` | per yield / per search / immediate push | UNSAFE | - | §4 | |

**Expected proxy cost.** The engine's own forwarder is `RemoteCall_Dispatch 0x7D98C0`: from another thread it posts a JobManager job with mask 4 for the sim thread and, if synchronous, waits in `0x59A500`. The sim thread runs mask-4 jobs in its pump at `0xD81FC3` [verified, docs/36, docs/28 §5]. So:
- **queued** (void): the worker pays a job build and submit, a few µs [inferred]; the sim thread pays the IC body plus job overhead at its next pump.
- **sync**: the worker blocks until the sim thread reaches its pump. The pump runs once per sim pass [inferred: it is one call site in the Simulate loop], so a sync proxy costs up to one sim iteration (ms scale), not the sub-µs of the IC itself. The only measurement of that hand-off is the other direction: sim thread waiting on main, max 20.9 ms (verify rerun 2026-10-16) [measured]. A worker also cannot be served while the sim thread runs a long task, and the autonomy search is such a task.
- **Consequence:** the hot set is 4 sync ICs called several times per pair, over every pair on the lot. A per-call sync proxy would turn one search into hundreds of sim-pump round trips. It is slower than running the search on the sim thread, whatever the IC cost. Proxies are only usable for rare, batchable calls. Every autonomy IC above becomes a snapshot field or a constant.

**Snapshot cost on the sim thread [inferred, with one measured input].** One `Objects_GetPosition`, `Slots_GetParentIdImpl`, `World_IsPositionOutsideImpl` and `World_IsOnRoofImpl` per object on the lot, plus the actor's own values: about 4 native calls per object, once per search, instead of several per pair today. Each object lookup walks the overloaded id hash, most often 8-15 entries (verify 2026-10-15, docs/45 fix #22) [measured], so fix #22 also cuts snapshot cost. docs/59 §1.4 already suggests the same memo inside the search on the sim thread. That memo needs no worker and is the cheaper first step.

## 4. UNSAFE ICs autonomy needs, and what they mean for the snapshot design (T-AUT-c)

Autonomy reaches three UNSAFE ICs, no MAIN IC, and one open-ended unsafe set that is not an IC:

| IC or set | Why it cannot be proxied | Snapshot design consequence [inferred] |
|---|---|---|
| `Simulator_SleepImpl` (Y1, Y2, Y3) | yields the calling script task (host vtable 0x108B818 +0x20, then the yield mark: verified in the body, §9) | The worker runs the whole search in one go: no `YieldIfPossible`, no stopwatch. The meta-autonomy 1 ms slicing (docs/59 §1.3) exists only to share the sim thread and is dropped on a worker. The interrupt check that the yield guards (`mAutonomySearchWasInterrupted`, actor destroyed) moves to the apply step: the sim thread drops the result if the Sim's search type changed or the actor died since the snapshot. |
| `Simulator_GetCurrentTaskImpl` (`CheckYieldingContext`) | returns the running task id (tail-jumps to host +0x118: verified, §9); on a worker or in a proxy it names the wrong task | Not called on the worker (no yields). The task identity is captured with the snapshot if apply needs it. |
| `Simulator_AddObjectImpl` (D5 immediate interactions) | registers a managed object and calls into managed code (+0x14 = ScriptHost_RegisterManagedObject: verified, §9) | The pick and push (D) stay on the sim thread, as docs/59 §1.6 already concludes. The worker returns a candidate list or a pick, never an `InteractionInstance`. |
| Interaction `Test()` bodies, tuning scoring functions, `Check.Test`, posture multipliers (C3, C8, C11) | arbitrary script, mods included; their ICs are not mapped (docs/59 §1.4 "not mapped") | This is the real blocker, not the 21 ICs. A snapshot can cover the base Test (C3-C7) and scoring (C8-C10) because their ICs are known. A definition's own `Test` can reach any IC, including UNSAFE ones (`World_CanSeeObjectImpl` and `Query_GetObjectsOverlappingObject` re-enter script through `World_FireEvent`). The worker needs an allow-list of audited definitions, and the sim thread scores all others as today. |
| Rain/hail path (C8) | allocates an `InteractionInstance` per outdoor pair | Replace with a snapshot shelter flag per target; never construct game objects on the worker. |
| Low-LOD `Update` (B3) | sends events, arbitrary listeners | Stays on the sim thread, before the snapshot. |

Further UNSAFE ICs in the World and Queries set that a definition `Test` may plausibly call [inferred from names, not traced]: `World_CanSeeObjectImpl` (line of sight), `Query_GetObjectsOverlappingObject`. Neither is in the base path. They are the first ICs to look for when auditing definitions.

So the T-AUT-c shape is: (1) sim thread: LOD decision, low-LOD `Update`, snapshot of the lot (per object: position, parent id, outside, on roof, hidden flags, destroyed, room, lot), the actor's motives and use values, world constants, sun times, weather shelter flags when needed, and an RNG seed; (2) worker: C1-C11 for allow-listed definitions on that copy, no IC except SAFE ones; (3) sim thread: re-check that the actor and the top candidates are still valid, then D2-D5 as today.

## 5. Runtime facts from the verify runs

Verify runs of 2026-10-15 and the rerun of 2026-10-16 (probe 0.10.7):

| Fact [measured] | Effect on verdicts |
|---|---|
| Thumbnail texture create `0xD5D910` runs on the sim thread (29 / 9 calls, 44 in the rerun), off the device thread | **Confirms** the device hazard behind the 12 `ThumbnailManager_Generate*` UNSAFE rows ("MAIN hazard too") and the thumbnail MAIN rows (`World_ObjectDesigner_ShareDesignPresetImpl`, `CASUtils_SimBuilderShareOutfitImpl`, `DownloadContent_SaveCustomSim`), if those ICs reach the same function. The static paths go through `ThumbnailManager_CreateThumbnailTexture`; that it is `0xD5D910` was not checked here [inferred]. |
| CAS model build `0x5E2050` and outfit callback `0x5AD8F0` on main | **Consistent** with UNSAFE for `CASUtils_SimBuilderBuildModelImpl` and `SetOutfitInGameObject(Delayed)Impl`: the work is posted to main and the script task waits (docs/42). |
| LoadLayout on the sim thread (2,241 calls, max 15.7 ms) | **Consistent** with UIManager PROXY: UI ICs run on the sim thread today, and a worker would have to forward them. |
| Physics ray entry `0x6583E0` sim only (4.6M calls) | **Consistent** with PROXY for the physics-query ICs: the sim thread is the only reader today. It does not test the missing lock, because there is no second reader. |
| Off-sim object id lookups 0 (async routing off) | **No evidence** for or against the Objects verdicts. |
| `Lock_Acquire` operand `0xFBA5CC` is the **timeout pointer** the caller passes, not the lock (the lock is `ecx`); the JobManager and SimService "locks" `0xFD9744` / `0xFE4DC4` are the same kind of constant [verified asm, probe 0.10.7 follow-up] | **Contradicts some lock columns.** Part B took "the lock global in the 8 instructions before the call". 216 merged rows name a lock operand in the `0xF9xxxx`-`0x107xxxx` constant range (for example `0x105E578` / `0x105E7C8` SocialFeatures, `0x10468AC` DownloadContent, `0x10786C8` UI/Slots/Audio), the same range as `0xFBA5CC`. These are most likely timeout constants, not lock names [inferred]. Verdicts do not change, because no rule upgraded a row to SAFE for holding a lock. But the "PROXY with a real lock" claims in `ic-safety-b.md` need the `ecx` operand instead. Named locks (`g_ScriptHandleLock`, `g_ResourceLock`, `g_ThumbnailManagerLock`) come from symbols and are not affected. |
| Sync remote call `0x7D98C0` wait max 20.9 ms | Input for §3: a synchronous hand-off between the game threads can cost a whole frame. |
| Object id hash overloaded: 11.8k objects in 1,033 buckets, most lookups walk 8-15 entries | Every handle-to-object PROXY IC (most of World, Objects, Slots) pays this on the sim thread today. It sets the snapshot cost in §3. |

No runtime fact contradicts a verdict. The one contradiction is the lock naming above, which affects notes and lock columns, not verdicts.

## 6. Gaps that matter for autonomy

| Gap | Why it matters | Next step |
|---|---|---|
| `World_IsPositionOutsideImpl` is low confidence (closure 52 functions, a `Lock_Acquire` at depth 3 via `0xB96380`) | Hot set rank 3. If the lock covers the room or lot data it reads, the snapshot could skip it; if not, it stays PROXY. Same lock path in `World_HasAnyRoofOrFloorAbovePosImpl` | Read `0xB96380` and the `ecx` lock (not the timeout operand) in Ghidra |
| `World_GetLotLocationImpl` low (closure 42, lock via `0xC68680`) | Weather path, per outdoor pair | Same check as above |
| `Slots_GetParentIdImpl` medium; both scans see only `g_ObjectService` / transform component reads behind 2 unresolved virtual calls | Hottest IC. Verdict PROXY is not in doubt (object state, no lock), but the read set decides what the snapshot must copy | Hand-check the body (5 functions) |
| Part A task ICs re-classified by name (`Simulator_SleepImpl`, `GetCurrentTaskImpl`, `SetYieldingDisabledImpl`) | They decide the "no yield on the worker" rule | Done 2026-10-16 (§9): confirmed in the bodies through host `+0x17C`; all three stay UNSAFE |
| `Random_NextDoubleImpl`, `StopWatch_*` return types not recorded in part B (`sync-or-queued`) | Minor: autonomy replaces them anyway | none |
| SACS (about 29 ICs) has no row | Not on the scoring path, but interaction `Run` and posture code use state machines; a later "run interactions off thread" idea would need them | Done 2026-10-16 (§7): 26 PROXY, 3 UNSAFE |
| Interaction `Test` bodies and scoring functions not mapped (docs/59) | The real blocker for T-AUT-c (§4) | Map the ICs reached by the most-scored definitions first (a managed call graph from the decompiled scripts, as in docs/59) |
| EA IC nesting counter `[0x11F2D84]` (docs/56) | Decides whether even SAFE ICs are callable from a worker | Check whether the IC transition or any SAFE body writes it |
| Thunk entries in the original part B scan (§8): before the fix the scan never read a body behind an IC entry thunk. Unscanned bodies came out SAFE (fixed for those rows), but a non-SAFE row whose verdict came from an override may still hide an UNSAFE path (a yield or managed callback) | A PROXY that is really UNSAFE would break a forwarder | Done 2026-10-16 (§9): no part B or SACS PROXY row moved; 18 part A rows moved to UNSAFE |
| 760 low-confidence rows, nearly all PROXY behind unresolved virtual calls | Errs toward PROXY, so not a safety risk; only a lost chance of more SAFE rows. None is on the autonomy path except those listed above | Resolve object and component vtables (part A Limits) only if a worker needs them |

## 7. SACS update (2026-10-16)

The 29 `SACS_*` ICs (animation state machines) that had no row now have one, `source` = `c`. Run: `tools/ghidra/ICSafety.java` at depth 6 on the 29 bindings from the IC map, then `tools/ghidra/icsafety/classify.ts` with no overrides (part B's rules, so the verdict meaning is the merged one). `proxy` comes from the name (`Get`/`Has`/`Can`/`Is` = `sync`, else `sync-or-queued`, return types not recorded). All verdicts are [inferred] scan verdicts except the one override below.

| | Before | After |
|---|---|---|
| Rows | 1,729 | **1,758** (matches the binding table) |
| SAFE | 159 | 159 |
| PROXY | 1,502 | 1,528 (+26) |
| UNSAFE | 62 | 65 (+3) |
| MAIN | 6 | 6 |

SACS confidence: 9 medium, 20 low (most closures end in many unresolved virtual calls into the animation system).

- **UNSAFE (3, 2 after §9):** `SACS_RequestStateImpl` (yields, host `+0x124`; also posts `PostRenderMessage`; **PROXY since §9**: `+0x124` signals a waiter, is not a yield, and is not reached from this IC), `SACS_AcquireDriverImpl` (marks a yield, host `+0x11C`, and reads the task id), `SACS_GetActiveClipCountImpl` (reaches `mono_runtime_invoke` within depth 6, so a managed callback; surprising for a getter, the path is not hand-checked). These must stay in a sim-thread script task. `SACS_AcquireDriverNoYieldImpl` is PROXY, so the no-yield variant is the one a forwarder could use.
- **Override (1):** `SACS_SetParamImpl` scanned SAFE (closure of 2). Its body calls the script host vtable `+0xCC` (handle lookup) and then a setter on the state machine; the scan stopped at the indirect call [verified, bytes]. Set to PROXY, like `SetParamStringImpl`.
- **PROXY (26):** everything else, state machine objects reached through the script handle table (table locked, object not). No SACS IC is SAFE.

For autonomy (§4) nothing changes: SACS is not on the scoring path. It matters for running interaction `Run` or posture code off thread, where `RequestState` and `AcquireDriver` are on the common path and both yield.

## 8. Unresolved-code rescan of the SAFE rows (2026-10-16)

`SACS_SetParamImpl` (§7) showed that the scan could see a closure end early and still call it SAFE. Rechecking all 159 SAFE rows found three holes of that kind [verified: bytes and scan output]:

1. **Thunk entries (75 rows moved).** Most IC entries are a 5-byte `JMP` to the real body. `ICSafety.java` treated a thunk as a dead end, even at depth 0, so for 87 SAFE rows it scanned nothing (`n=1`) and `classify.ts` turned the empty closure into SAFE. Part A's `IcSafetyDepth4.java` already followed thunks.
2. **Undisassembled bytes (1 row, plus `SACS_SetParamImpl`).** Ghidra left the bytes after `0x766F4B` undisassembled, so the `SACS_SetParamImpl` body ends mid-flow, before its host `+0xCC` call. No rule counted a body that falls into bytes with no function. `UIManager_GetWorldMetadataImpl` is the extreme case: no Ghidra function at `0x77FE80` at all (`NOFUNC`), which `classify.ts` also read as an empty closure.
3. **Computed tail jumps (1 row).** Part A's scan counted computed `CALL`s only. `Simulator_ClearReflectionCache` tail-jumps through the script host vtable `+0x178`.

Fix: `ICSafety.java` follows thunks at the same depth and reports `tr=` (instructions that fall out of the body into bytes with no function; also added to `ind`) and `open=` (callees cut off by the depth limit). `classify.ts` gives SAFE only when `ind`, `tr` and `open` are 0, the size cap was not hit and the line is not `NOFUNC` (`NOFUNC` = PROXY, low), and it now reads CRLF scan files. `IcSafetyDepth4.java` reports `vcAll=` (computed calls and jumps at any depth) and `tr=`.

Rescan: `ICSafety.java` depth 6 on the 159 SAFE rows plus `SACS_SetParamImpl`, then `classify.ts` with `overrides-b.json`; `IcSafetyDepth4.java` depth 4 on the 8 part A SAFE rows as a cross-check. Independent check: a throwaway Ghidra script dumped the unbounded call closure (thunks followed, cap 200 functions) of every SAFE row; all 82 rows that stay SAFE have no computed call or jump other than import calls, no fall-out of a body, no `Lock_Acquire` and no cap hit.

| | Before | After |
|---|---|---|
| SAFE | 159 | **82** (81 high, 1 medium) |
| PROXY | 1,528 | 1,603 (+75) |
| UNSAFE | 65 | 67 (+2) |
| MAIN | 6 | 6 |

Rows moved (77; each CSV note starts with "unresolved-code rescan 2026-10-16 (docs/62 §8)"; all [inferred] scan verdicts unless marked):
- **UNSAFE (2):** `GameUtils_DumpMemoryReport`, `GameUtils_DumpProfilerReport`. Both bodies call into the script host block (`0xEC2220`), which reads the task id (host `+0x118`), marks a yield (`+0x11C`), waits (`+0x120`) and reaches `mono_runtime_invoke` within depth 2.
- **PROXY, thunk entries (73):** 39 `SocialFeaturesHost_*` (most take the social host's member lock and read or write its state), 11 `CASUtils_*` tattoo template helpers (writes through pointers and object fields; some may be caller-owned buffers, which the scan cannot tell apart [inferred]), 9 `GameUtils_*` (cheats, debug options, generic string, mod file names, installation check, startup and load screens), 4 `LoadSaveManager_*` challenge and passport data load and save (file I/O plus shared state), 4 `LocalizedStringService_*` (string tables, docs/54), 3 `OnlineFeaturesHost_*`, `Automation_SetFailureExitCode`, `DownloadContent_IsDevBuild`, `StreamHost_Close`. Confidence of all 75 PROXY rows moved: 43 low, 24 medium, 8 high.
- **PROXY, other (2):** `Simulator_ClearReflectionCache` (host `+0x178` tail jump, `queued`, medium [verified, bytes]); `UIManager_GetWorldMetadataImpl` (`NOFUNC`; raw bytes show a direct call, then a virtual call through `+0x58` on its result; `sync`, low [verified, bytes]).
- `SACS_SetParamImpl` was already PROXY by override (§7); the fixed scan gives PROXY on its own (`tr=1`).

None of the moved rows is on the autonomy path; §3 and §4 do not change.

Hand checks of three rows that stay SAFE [verified, disassembly]:
- `Simulator_DidSecuritySystemFailImpl`: reads one byte global (`0x11D677C`) and returns. SAFE.
- `DeviceConfig_EnableRegistryTelemetry` (closure 6, among the largest kept): tail-jumps to a helper that opens or creates an HKLM key and writes one value through the `advapi32` imports; reads only init-time key name globals. No game state; registry calls are thread-safe [inferred, Win32 docs]. SAFE.
- `Camera_GetCustomFlyThroughIndex`: camera global `0x11E9400`, then three plain field loads (`+0x14`, `+0xAC`, `+0x19C`), no virtual call, no write. SAFE by the rule, but it reads live camera object state with no lock: a word-sized racy read, and the object chain could change under a worker [inferred]. A worker that needs a stable value should proxy it anyway.

The ledger claim I-62-1 (docs/51) now reads 82.

## 9. Full rescan of all rows with the fixed scan (2026-10-16)

§8 rescanned only the SAFE rows. This pass ran the fixed `ICSafety.java` at depth 6 on all 1,758 bindings (1,714 bodies, on a private copy of the Ghidra project), classified them with `classify.ts` and `overrides-b.json`, and diffed the result against the CSV. Every changed row's note starts with "full rescan 2026-10-16 (docs/62 §9)".

**The thunk bug did not hide anything in the part B or SACS rows.** No part B or SACS PROXY row moved. A non-SAFE part B row could not have come from an unread thunk body (an empty closure classified SAFE), so the §6 worry about override rows is cleared [verified: scan diff]. Of the 45 rows where scan and CSV differ, 43 are part A rows (scanned at depth 4 with other rules by `IcSafetyDepth4.java`); the other two hinged on one mislabelled scan flag.

| | Before | After |
|---|---|---|
| SAFE | 82 | 82 |
| PROXY | 1,603 | **1,587** (-18, +2) |
| UNSAFE | 67 | **83** (+18, -2) |
| MAIN | 6 | 6 |

**Scan rule fix: host `+0x124` is not a yield.** In the host's primary vtable, `+0x120` (`0xD77F30`) puts a key, two values and an object into the host map at `+0x1720` and queues it. `+0x124` (`0xD77FB0`) looks the key up in that map and queues it to be woken (`+0x175C`). A depth-6 scan of `0xD77FB0` finds no yield, wait or managed call [verified: asm and scan]. So `+0x120` registers a waiter (every IC that calls it also calls the yield mark `+0x11C`), and `+0x124` signals one: a post, not a yield [inferred from the asm]. `ICSafety.java` now flags `+0x124` as `post:host+124_SignalWaiter`. Two rows were UNSAFE only through `+0x124` and are now PROXY:
- `SACS_RequestStateImpl`: also, the only `+0x124` call (in `0x8042D0`) runs only when its 5th argument is non-zero, and the IC passes 0, as does the one recursive call [verified asm]. The IC does not reach `+0x124` at all and has no other yield or managed call in its closure. This agrees with docs/73 §1 (I-73-4): the yield of `StateMachineClient.RequestState` is its managed `Sleep(0)` loop. PROXY, `sync-or-queued`, low (104 unresolved virtual calls).
- `CASUtils_SimBuilderResetAnimationImpl`: reaches `0x8042D0` through `0x804A20`, which passes its caller's 5th argument on. Whichever way that goes, `+0x124` is not a yield. PROXY, low.

**PROXY to UNSAFE (18, all part A, none on the autonomy path)** [verified: scan paths at depth 6 through direct calls; branch conditions not traced]:
- **Synchronous managed callback via `World_FireEvent 0x78F2B0` (10):** `Objects_SetPosition`, `SetForward`, `SetRotation`; `World_SetRoomMarkerSeed`, `ClearRoomMarkerSeed`, `DesignModeSwap_ApplyToObjectImpl`, `DesignModeSwap_DisableSubmitOfUndoableImpl`, `RestrictBuildBuyInBuildingsImpl`, `GetLotWorth`, `GetUnfurnishedLotWorth` (both getters reach it through a lazy lot worth recompute). Part A stopped at depth 4; these paths are 5 or 6 calls deep.
- **Host delegate call `+0x134` (2):** `World_FindGoodLocation`, `World_FindGoodLocationAtCustomDepth` (through the FGL core `0xB470C0`).
- **`mono_runtime_invoke` (1):** `Route_InsertCustomPathAtIndex`, through host `+0x130` (`0xD71280`, a managed member read that ends in `0xEA1400`).
- **Task context or yield (5):** `RouteManager_BeginPortalRouting` (reads the running task id, host `+0x118`, in its own body); `World_CapturePhoto`, `CaptureSceneForMemoryOrPicture`, `CaptureMeshForSculpture` (task id, register waiter, yield mark in the body); `World_WaitForDetailedLots` (yield mark plus `mono_runtime_invoke`).

**Kept against the scan (25 rows):**
- **5 `Simulator_*` task ICs stay UNSAFE.** The scan sees PROXY because each body calls host `+0x17C`, which returns the host itself (docs/27), and then a slot of the host's second vtable `0x108B818` that the scan does not follow. The slots [verified asm]: `SleepImpl` +0x20 = `0xD77B20` (puts the task in the host sleep map, then the yield mark `+0x11C`); `GetCurrentTaskImpl` +0x1C = `0xD71500` (tail-jumps to `+0x118`, `0xD70E90`, which returns the task id at host `+0xBC8`); `AddObjectImpl` +0x14 = `0xD77920` (`ScriptHost_RegisterManagedObject`, docs/50); `CreateObjectImpl` +0x04 = `0xD7B550`; `SetYieldingDisabledImpl` +0x10 = `0xD70B80` (task-keyed host calls; what it sets is [inferred]). This closes the §6 gap "confirm host `+0x118` / `+0x11C`". Sleep, GetCurrentTask and AddObject are now high confidence.
- **19 rows the scan calls SAFE stay PROXY.** The SAFE rule ignores global reads. 17 of the 19 are part A getters with a recorded unlocked read of sim-mutated state (sun and moon times, sea level, object designer state); `World_ExportInfoImpl` and `World_HandToolGetObjectHeld` were not hand-checked. Three are autonomy rows (`World_GetSeaLevelImpl` C5, `GetSunsetTime` C6, `GetSunriseTime` C7): closures of 1-2 functions with no virtual call, so the snapshot copy in §3 is cheap, but they stay PROXY.
- **`World_SetLightingValuesImpl` stays MAIN.** The depth-6 scan leaves 19 callees past the depth limit and does not reach the device call part A found; part A's path is kept [not rechecked].

**Hand checks** [verified asm unless marked]: the five `Simulator_*` rows and `SACS_RequestStateImpl` (above); `RouteManager_BeginPortalRouting` (calls host `+0x118` on every non-null path after the object lookup) and `Route_InsertCustomPathAtIndex` (the host `+0x130` call is in the body; `0xD71280` ends in `mono_runtime_invoke`), the two routing rows; and three others: `Objects_SetPosition` (`0xC8A290` fires event type `0x11` through `World_FireEvent`), `World_FindGoodLocation` (the FGL core calls host `+0x134` with the delegate at params `+0x28`) and `World_GetLotWorth` (lot lookup, then a lazy worth recompute `0xABB390` that writes lot fields and calls the setter `0xB036E0` that fires the event [path verified, "lazy" inferred]).

**Design impact.** None on autonomy: no autonomy row changes verdict, so §3 and §4 stand, now with the three task ICs confirmed in their bodies. docs/60 (async routing) does not rely on any moved row: route planning, the `+0x918` lock sites and the physics readers are unchanged. `BeginPortalRouting` and `InsertCustomPathAtIndex` show that portal routing and custom path edits are script-task operations (task id, managed reads), so they stay on the sim thread in any off-thread routing design. docs/61 and docs/30 name no moved IC. One new fact for docs/30 workers: `Objects_SetPosition` / `SetForward` / `SetRotation` fire managed events synchronously, so an off-thread result can never move an object directly; it must be applied on the sim thread.

## Reproduce

The merge is mechanical: read both CSVs, apply the §1.1 mapping and the three hand overrides, dedupe on `ScriptCore.<Class>::<Method>` (preferring higher confidence, then part B), and tag the docs/59 §1.4 rows in the `autonomy` column (`B1`-`B4` hot set, `A1`-`A9` per search, `C1`-`C9` conditional, `S1` Sim task). The script was a one-off and is not kept in the repo; the CSV is the result.
