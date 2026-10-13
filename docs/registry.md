# Registry: counter slots and ts3native counter versions

Shared numbers that two PRs must never both take. Test tools, logs and results files refer to them, so a number is never reused or renumbered, even after its fix is removed.

## Rule

1. Reserve before you code: in your issue comment, name the next free ts3fixes slot range (after the last row below) and/or the next ts3native counters version.
2. Add your rows here with status `reserved (#issue)` in your PR; change them to the final meaning before merge.
3. Two PRs that reserved the same number: the first merged keeps it, the other rebases onto the next free one.
4. Gaps stay gaps (slots 19-20): old logs and results already read them as zero.

## ts3fixes counters (`AppDomain` data `ts3fixes.counters`, `int[68]`)

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
| **68+** | next free | reserve here |

Adding slots: raise the array size in **both** `Fixes.cs` (`PatchCounters`, `new int[68]`) and `PatchHandlers.cs` (`?? new int[68]`), extend the `Fixes.cs` comment block, and read the new slots in `PatchHandlers.cs` behind `if (c.Length > N)` so an older mod build still answers.

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
| **v11** | next | 0x14C- | reserve here |

v10 total: 83 int32. The v4 release number is inferred from the docs/18 history; every other row matches PLAN.md.

Adding a version: append fields at the end of `Ts3nCounters` under a `// vN: ...` comment, set the first initializer value to N, update the "read N int32 (vN)" comment above the struct, bump `TS3NATIVE_VERSION`, add the `if (c.readS32() >= N) f.push(...)` line in `tools/probe/agent.js`, and add the row here. Runtime switches are `volatile LONG` and written by the probe (`/native?...`).
