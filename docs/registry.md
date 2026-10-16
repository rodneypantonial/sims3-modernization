# Registry: counter slots and ts3native counter versions

Shared numbers that two PRs must never both take. Test tools, logs and results files refer to them, so a number is never reused or renumbered, even after its fix is removed.

## Rule

1. Reserve before you code: in your issue comment, name the next free ts3fixes slot range (after the last row below) and/or the next ts3native counters version.
2. Add your rows here with status `reserved (#issue)` in your PR; change them to the final meaning before merge.
3. Two PRs that reserved the same number: the first merged keeps it, the other rebases onto the next free one.
4. Gaps stay gaps (slots 19-20): old logs and results already read them as zero.

## ts3fixes counters (`AppDomain` data `ts3fixes.counters`, `int[80]`)

Source of truth: the comment block above `PatchCounters()` in `mods/ts3fixes/src/Fixes.cs` (an older note in PLAN.md "Resume here" puts the async route counters at 52-54; they are 56-58). The test agent reads them in `mods/ts3agent/src/plugin/Handlers/PatchHandlers.cs` (`patches` command).

| Slots | Owner (file, switch) | Meaning |
|---|---|---|
| 0-3 | `Fixes.cs`, `gohome` | go-home runs, shortcuts, no safe spot, loop guard |
| 4-6 | `Fixes.cs`, `rabbithole` | building (rabbit hole) trips: calls, shortcuts, no safe spot |
| 7-9 | `Fixes.cs`, `visit` | lot trip: runs, shortcuts, no safe spot |
| 10-12 | `Fixes.cs`, `visitlot` | same, visit lot |
| 13-15 | `Fixes.cs`, `gotolot` | same, go to lot |
| 16-18 | `Fixes.cs`, `gotocommunity` | same, go to community lot |
| 19-20 | free | never used; leave empty |
| 21 | `Fixes.cs` (lot trips) | houseboat placements |
| 22-23 | `Fixes.cs`, `broadcast` | reaction broadcaster pulses, skipped |
| 24-26 | `RouteBackoff.cs`, `backoff` | per-Sim route backoff: plans seen, failures recorded, plans skipped |
| 27-28 | `RouteFixes.cs`, `virtualhome` | virtual-home go-home runs, fades |
| 29-30 | `RouteFixes.cs`, `failreplan` | obstacle replans switched off, route-fail walks skipped |
| 31 | `RouteFixes.cs`, `watchdog` | stuck Sims reset or faded by the watchdog |
| 32 | `RouteFixes.cs`, `stations` | station detours skipped |
| 33-36 | `RouteFixes.cs`, `watchdog` | watchdog home kind: none/virtual (faded), shell apartment, house; second resets in one sim-day |
| 37-39 | `Fixes.cs` (SafeSpot verdict cache) | cache hits, misses, full checks run |
| 40-43 | `SaveCleanup.cs`, `savecleanup` | relationship pairs removed, MiniSims removed, cleanup runs, runs skipped or failed |
| 44-45 | `OverwatchStuck.cs`, `overwatchstuck` | NRaas Overwatch stuck-check plans seen, yields |
| 46-51 | `RouteFixes.cs`, `watchdogidle` | idle on an unroutable spot, hidden room, stationary a sim-day, route plans failing, routability checks asked, route-fail Sims tracked |
| 52 | `SaveCleanup.cs`, `savecleanup` | empty life-event lists trimmed |
| 53 | `SaveCleanup.cs` (tunable `kSnapshotCap`) | memory snapshots capped |
| 54-55 | `PortalAdaptorLeak.cs`, `portaladaptorleak` | route adaptor callbacks unsubscribed after DoPortal; stale ones removed by the save cleanup |
| 56-58 | `RouteBackoff.cs` (with ts3native `[RouteAsync]`) | async route plans started, yields, poll cap reached |
| 59-61 | `CasGrid.cs`, `casgrid` / `casmemo` | CAS incremental fills started, hair preset cache hits, preset lookups run |
| 62-64 | `SpatialCache.cs`, `spatialcache` | lot object query cache hits, misses, clears on object destroy |
| 65-66 | `FewerPlans.cs`, `routablecache` | routability cache hits, plans run and recorded |
| 67 | `FewerPlans.cs`, `stuckcheckbusy` | 3 AM stuck checks skipped for busy Sims |
| 68-70 | `RefTable.cs`, `fastreftable` | save identity tables converted, most entries in one table (on grow and Clear), table grows |
| 71-73 | `CachedReflection.cs`, `cachedreflection` | serializer type caches started with the switch on (one per save), answers from the cache, answers stored |
| 74-75 | `UiCaches.cs`, `motivebar` | motive bar updates that kept their moodlet images, updates run by the game (image loads) |
| 76-77 | `UiCaches.cs`, `buffgrid` | moodlet grid updates in place, rebuilds by the game (switch on) |
| 78-79 | `NameCache.cs`, `namecache` | Sim name answers from the cache, answers computed and stored |
| **80+** | next free | reserve here |

Adding slots: raise the array size in **both** `Fixes.cs` (`PatchCounters`, `new int[80]`) and `PatchHandlers.cs` (`?? new int[80]`), extend the `Fixes.cs` comment block, and read the new slots in `PatchHandlers.cs` behind `if (c.Length > N)` so an older mod build still answers.

## ts3native counters (`ts3native_counters` export, `Ts3nCounters` in `native/ts3native/ts3native.cpp`)

Append-only struct of `LONG` (int32). `version` (offset 0) tells readers how many fields exist; `tools/probe/agent.js` `nativeCounters` adds one field list per version. Offsets are bytes from the export.

| Version | ts3native | Offsets | Fields |
|---|---|---|---|
| v1 | 0.2.0 | 0x00-0x1C | `version`, `enabled`, `calls`, `hits`, `misses`, `stored`, `otherFails`, `windowMs` (route negative cache) |
| v2 | 0.3.0 | 0x20-0x3C | `workerMode` (switch), `workerReady`, `workerCalls`, `workerUs`, `workerMaxUs`, `waitUs`, `fallbacks`, `watchdogs` (route worker) |
| v3 | 0.4.0 | 0x40-0x60 | `budgetMode` (switch), `budgetReady`, `budgetCalls`, `budgetCapped`, `budgetCapNodes`, `budgetCapUs`, `budgetMaxNodes`, `budgetMaxNodesOk`, `budgetMaxUsOk` (route search budget) |
| v4 | 0.4.x (worker crash fix, docs/18) | 0x64-0x68 | `invokeHook`, `invokes` (managed invokes from the worker run on the caller thread) |
| v5 | 0.5.0 | 0x6C-0x90 | `asyncMode` (switch), `asyncReady`, `asyncStarted`, `asyncRefused`, `asyncCollected`, `asyncDropped`, `asyncUs`, `asyncMaxUs`, `asyncPumpUs`, `asyncPumpWaits` (async route plan) |
| v6 | 0.6.0 | 0x94-0xBC | `saveReady`, `saveMode` (switch), `saveJobs`, `savePromoted`, `saveFallbacks`, `saveCount`, `saveLastUs`, `saveLastWriteUs`, `saveLastBackupUs`, `saveLastCopyUs`, `saveLastFinalUs` (save write) |
| v7 | 0.7.0 | 0xC0-0xEC | `decompressReady`, `decompressMode` (switch), `decompressVerify` (switch), `decompressCalls`, `decompressFails`, `decompressMismatches`, `decompressMaxUs`, `decompressInMB`, `decompressOutMB`, `decompressMs`, `decompressMainCalls`, `decompressMainMs` (RefPack decoder) |
| v8 | 0.7.1 | 0xF0-0x100 | `hostCalls`, `planOffCalls`, `planOffFails`, `planOnCalls`, `planOnFails` (host calls proxied; plans per mode for A/B) |
| v9 | 0.8.0 | 0x104-0x12C | `allocReady`, `allocMode` (switch, +0x108), `allocHitsK`, `allocMissesK`, `allocCachedK`, `allocPassedK`, `allocThreads`, `allocFlushed`, `allocDropped`, `allocShutdowns`, `allocOomRetries` (allocator cache; `K` = units of 1024) |
| v10 | 0.9.0 | 0x130-0x148 | `waterJpsReady`, `waterJpsMode` (switch, +0x134), `waterJpsVerify` (switch), `waterJpsCallsK`, `waterJpsCellsK`, `waterJpsMismatches`, `waterJpsFallbacks` (water JPS) |
| v11 | 0.10.0 | 0x14C-0x198 | `compressReady`, `compressMode` (switch, +0x150), `compressVerify` (switch, +0x154), `compressBatches`, `compressJobs`, `compressHits`, `compressMisses`, `compressMismatches`, `compressInMB`, `compressWaitMs`, `compressLastMs`, `compressPeakMB` (save package compression on workers), `flushReady`, `flushMode` (switch, +0x180), `flushDeferred`, `flushJobs`, `flushWaits`, `flushWaitMs`, `flushLastMs`, `flushFails` (save flushes on a worker) |
| v12 | 0.10.1 | 0x19C-0x1AC | `compressCommits`, `compressCommitPending`, `compressDirectCalls`, `compressDirectMs`, `compressDirectMB` (where save compression runs: commit hook calls, codec compress calls outside a batch) |
| v13 | 0.10.2 | 0x1B0-0x1C8 | `heightReady`, `heightMode` (switch, +0x1B4), `heightCalls`, `heightHits`, `heightMisses`, `heightRebuilds`, `heightMismatches` (terrain height cache) |
| v14 | 0.10.2 | 0x1CC-0x1D8 | `gcCheckReady`, `gcOwnerTid`, `gcForeignAllocs`, `gcForeignTid` (GC allocation thread check, measurement) |
| v15 | 0.10.2 | 0x1DC-0x200 | `icLockReady`, `icLockEntersK`, `icLockContended`, `icLockWaitUs`, `icLockMaxWaitUs`, `icLockOwnerChanges`, `icLockPrevTid`, `icLockLastTid`, `icLockLeaveWaiters`, `icLockCallback` (Mono loader lock check, measurement) |
| v16 | 0.10.3 | 0x204-0x278 | `rgReady`, `rgLock`, `rgOwnerTid`, `rgAcquires`, `rgEntries`, `rgUntracked`, `rgUnlocked[12]` (+0x21C), `rgJob[12]` (+0x24C); arrays in docs/18 S0 gate-site order, agent.js names them `rgUnlocked_<site>` / `rgJob_<site>` after `RL_GATES` (route gate measurement, docs/18 S2a) |
| v17 | 0.10.3 | 0x27C-0x2A0 | `cacheReady`, `cacheMode` (switch), `cacheHits`, `cacheMisses`, `cacheMismatches`, `cacheEvicted`, `cacheEntries`, `cacheKB`, `cacheHitMB`, `cacheHashMs` (save compressed-bytes cache, docs/26 §8) |
| v18 | 0.10.4 | 0x2A4-0x2C0 | `mtReady`, `mtMode` (switch, +0x2A8), `mtSimTid`, `mtThreads`, `mtSimRegistered`, `mtPushCalls`, `mtStacksPushed`, `mtCtxFails` (second managed thread step 1: GC thread registration and multi-stack root scan, docs/30 P1/P2) |
| v19 | 0.10.5 | 0x2C4-0x344 | `lsReady`, `lsMode` (switch, +0x2C8), `lotAdds`, `lotAddUs`, `lotAddMaxUs`, `lotAddObjs`, `lotAddMaxObjs`, `lotRemoves`, `lotRemoveUs`, `lotRemoveMaxUs`, `pumpCalls`, `pumpUs`, `pumpMaxUs` (+0x2F4), `pumpLotPasses`, `pumpMaxLots`, `lotScoreCalls`, `lotScoreUs`, `lotScoreMaxUs`, `lotScoreChanged`, `lotSetOn`, `lotSetOff`, `syncWaits`, `syncWaitUs`, `syncWaitMaxUs`, `objScenes`, `objSceneUs`, `objSceneMaxUs`, `objSceneHist[4]` (+0x330; agent.js `objSceneHist_lt0_1ms` .. `_ge10ms`), `lsOtherFns`, `lsLogDropped` (lot streaming measurement, docs/28 §5.5; `*Max*` fields hold the max since the last reset: `stutter-profile.js` zeroes `lotAddMaxUs` and `pumpMaxUs` each second) |
| v20 | 0.10.6 | 0x348-0x558 | `vfReady`, `vfMode` (switch, +0x34C), `vfThread[12][6]` (+0x350; per probe `main`, `sim`, `other`, `offDevice`, `us`, `maxUs` for 0xD5D910, 0x619540, 0x60D290, 0x6F3DC0, 0x599720, 0x6F1B00, 0x6EBE30, 0x6EA260, 0x6EBF50, 0x6F1D10, 0x50A860, 0x7294F0), `vfSwaps` (+0x470), `vfSwapsInRay`, `vfRayCallsK`, `vfRayMaxInFlight`, `vfKeyLists`, `vfRegDbs`, `vfKeyListOverlaps`, `vfIdLookupsK`, `vfIdOffSim`, `vfIdOffSimRet[8]` (+0x494), `vfPkgCalls` (+0x4B4), `vfPkgUs`, `vfPkgMaxUs`, `vfPkgWalked`, `vfPkgMaxWalked`, `vfIdChain[6]` (+0x4C8), `vfIdMaxChain`, `vfIdObjects`, `vfW2lCallsK` (+0x4E8), `vfFindLotCalls`, `vfTreeRebuilds`, `vfTreeMaxPerFrame`, `vfFdbHolds` (+0x4F8), `vfFdbHoldUs`, `vfFdbHoldMaxUs`, `vfFdbWaits`, `vfFdbWaitUs`, `vfFdbWaitMaxUs`, `vfUiLock[3][3]` (+0x510; per role main/sim/other: acquires, us, maxUs; `Lock_Acquire` calls with timeout pointer 0xFBA5CC, which 0.10.6 wrongly compared with ecx), `vfFpsGate` (+0x534), `vfSimSlice`, `vfSpeedTreeKB` (-1 from 0.10.7), `vfVramMB`, `vfRcBytesKB`, `vfRcFlushKB`, `vfRcBudgetMB`, `vfDeviceTid` (+0x550), `vfUntracked`, `vfLogDropped` ([Verify] mapping-doc checks; agent.js keys follow `vf<Group><Target><Suffix>`, mapping in docs/15 [Verify]) |
| v21 | 0.10.7 | 0x55C-0x8A4 | `vfReady2` (bit i: probe 27 + i), `vfThread2[34][6]` (+0x560; thread slots 12-45, same six fields as `vfThread`, for 0x6583E0, 0x686CC0, 0x7140D0, 0x7135B0, 0x712F00, 0x863DC0, 0xD7F8B0, 0x782BC0, 0x401BE0, 0x4E1DF0, 0x619770, 0xC6D970, 0xC688A0, 0x7D98C0, 0x722E00, 0x5AD8F0, 0x5E2050, 0xADD2C0, 0x6883A0, 0xADAE50, 0x939170, 0x703CB0, 0x50B300, 0x5989C0, 0x598F90, 0x6E4770, 0xABFAC0, 0x7D2DB0, 0x50B270, 0x5172E0, then shape swap 0xC2A840, tree rebuild 0x712480, Lock_Acquire JobManager 0xFD9744 and SimService 0xFE4DC4), `vfOverlap[4]` (+0x890; Scene, 50AB70, R1, 686CC0), `vfFdbPublishes` (+0x8A0), `vfFpsLoops` (+0x8A4) ([Verify] docs/51 probes; keys in docs/15 [Verify]) |
| v22 | 0.10.8 | 0x8A8-0xBEC | `vfReady3` (bit i: probe 58 + i), `vfThread3[34][6]` (+0x8AC; thread slots 46-79, same six fields, agent.js key prefix per slot: `vfMonoE85020`, `vfMonoEA1E30`, `vfMonoEA3382` (count only), `vfMonoE51400`, `vfMonoE78780`, `vfMonoE53070`, `vfRt628A90`, `vfRt835240`, `vfRt835110`, `vfRt939420`, `vfRt682760` (swap calls only), `vfEv59A430`, `vfEvReadyInPump`, `vfEvPumpSamePass`, `vfEvRcInline`, `vfEvRcInLotAdd`, `vfEvJobInSyncWait`, `vfEvLotAddInSyncWait`, `vfEv76B1A0`, `vfThrAC1130` (lot add via 0xABD3C0), `vfEvLotAddManaged`, `vfThrC6C290`, `vfThr452D30`, `vfThr6916F0`, `vfThr452630`, `vfThr59BDE0`, `vfThr49C5F0`, `vfThr799780`, `vfThr799810`, `vfThrECA350`, `vfThr505620`, `vfMonoD71900`, `vfMonoE4BEB0`, `vfMonoInvoke`), `vfOverlapCur` (+0xBDC, `vfRace505620Overlap`), `vfShaderStartup[2]` (+0xBE0, `vfCost799780Startup`/`vfCost799810Startup`), `vfMonoIcNestMax` (+0xBE8), `vfMonoIcNestNeg` (+0xBEC, `vfMonoIcNestNegCalls`) ([Verify] docs/51 I-52..I-62; docs/15 [Verify]) |
| **v23** | next | 0xBF0- | reserve here |

v22 total: 764 int32 (v21: 554, v20: 343, v19: 210). The v4 release number is inferred from the docs/18 history; every other row matches PLAN.md.

Adding a version: append fields at the end of `Ts3nCounters` under a `// vN: ...` comment, set the first initializer value to N, update the "read N int32 (vN)" comment above the struct, bump `TS3NATIVE_VERSION`, add the `if (c.readS32() >= N) f.push(...)` line in `tools/probe/agent.js`, and add the row here. Runtime switches are `volatile LONG` and written by the probe (`/native?...`).
