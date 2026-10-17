# 82. The steering line, 4 hops deep: vtable targets, shared writes and finer locks

Written 2026-10-17. Static only, continues docs/81. Tools: capstone 5 linear sweeps of the Steam 1.67 `TS3W.exe` over the function list of our Ghidra database (forward closures, vtable-store index, caller index, slot-signature search), plus Ghidra headless decompiles on a private copy of the docs/80 project (`game-copy/ghidra-sl`, read-only runs). The game was not run. Scratch scripts stay in the session scratchpad (not committed).

Tags as in AGENTS.md: **[verified]** = read in the disassembly or decompile (address given); **[inferred]** = reasoning to confirm with a probe (§9).

**Hop rule.** Hop 1 = the function's own locks, waits and calls. Hop k = the same for every function first reached at hop k-1. Direct calls are followed; a vtable call is followed when its targets were resolved (§3); the Mono block `0xD70000-0xEC0000`, `Lock_*`, `EA_Alloc*`/`EA_Free` and memcpy-type helpers are recorded, not expanded. Callees first reached at hop 5 are listed as **open**. Flags searched: `Lock_Acquire 0x4E16F0` (with the `ecx` source), Win32 waits and critical sections (import table), managed entries (`0xD75FA0`, `0xEA1400`, `0xE5A600`, `0xE59FD0`, `0xD7EED0`, `0xD7F000`, `0xD7F210`), `RemoteCall_Dispatch 0x7D98C0`, `World_FireEvent 0x78F2B0`, Mono allocation, `lock`-prefixed writes and absolute global writes. Function ends come from the Ghidra list; a few small functions have no end there and were read by hand.

## 1. Summary

- **A managed call does hide in the steering line, behind the agent vtable.** The per-tick obstacle list (`Locomotion_UpdateAgent 0x957210`, agent vtable `+0x60` at `0x9573DB`) and the "goal obstructed" notice (`ObstacleAvoidance 0x847390`, agent `+0x64` at `0x8476AD`) both end in `World_FireEvent`: agent class vtable `0x1044590` [verified, unique signature match] slot `+0x60` `0x954750` forwards to the agent's listener `[agent+0x90]` slot 0 = `0x74B280` -> `0x796580` -> `World_FireEvent(0x3A)`; slot `+0x64` `0x954790` forwards to listener slot `+8` = `0x7521B0`, which takes the listener's own lock `+0xB0` (`0x7521CE`) and calls `0x791510` -> `World_FireEvent(0x39)` with route event type `0x12` [verified call chains]. The listener is the native **Route** object (vtable `0x10047CC`, constructors `0x75D4B0` from `IC_Route_CreateRoute` and `0x760200`) [inferred: the only `.rdata` table with a code reference whose slot 0 returns 0x14 and slot 8 returns 0x28 bytes]. `World_FireEvent` calls the script host slot `+0x134`/`+0x138` (runtime invoke) [verified, decompile]. Both calls run on the sim thread **after** `+0x918` is released [verified order]. So the steering answers are script-visible: a skipped or stale answer also skips or repeats a route callback into C#.
- **Footprint vtables resolved; no managed call in them.** 9 concrete footprint classes (§3.1). All 130 slot targets of their vtables (outer slots `0..0xEC`, sub-object slots `0..0x20`) scanned 4 hops deep: no managed entry, remote call, world event or wait [verified, direct calls; 92 nested vtable calls in those closures not followed]. Locks inside: footprint registry `0x11D9068` (delete path `0x80F2F0`), a global `0x11D8F90` (slot `+0x50`), and an object lock `X+0xA0` (`0xB96380`, `0xB99150`, `0xB971F0`) reached from one class's polygon rebuild [verified sites].
- **New shared write: the footprint polygon cache.** The polygon tests steering calls (`+0xB8 0x80E8A0`, `+0xDC 0x80F790`, `+0xE8 0x80E690`, and `RouteFollow_EditPath` directly at `0x85FA50`) rebuild a per-level cache on first use: entry `[fp+0x38] + level*0x24`, valid byte `+0x20`, bounds `+0x10..+0x1C`, old vectors freed by `0x61EE20`, rebuilt through vtable `+0xF8`/`+0xFC` (`0x80E5B0`) [verified]. For two classes `+0xF8` reaches `LotTable_GetIfPresent -> LotTable_GetOrInsert`, `World_GetHeightAt` and the `X+0xA0` lock. The job rebuilds the same caches (`+0x70` from the quadtree apply-insert `0x665D36`, `+0xDC` at `0x637192` and `0x634A16`) [verified sites; receiver = footprint inferred]. A fourth race next to docs/81's three.
- **Atomic counter alone does not work.** `LotQuadTree_Insert/Remove` check `Q+0x48 < 1` and then mutate the tree directly; an atomic counter leaves this check-then-act race open, and refcounts and polygon caches still race (§6).
- **No structure split.** Steering never touches the planner scratch, but every structure steering reads (lot table, per-level quadtrees, footprints and their caches) is also written by the job (§7). Two locks by structure would leave both on the same lock.
- **Best design: a read-only fast path, split by operation (design E, §8).** The worker takes our own lock QL exclusively only around its *mutations* of the quadtree/footprint/lot-table layer (about 14 entry detours, worker thread only); during a job the sim thread runs the steering queries read-only under QL shared: no counter, no flush, no refcounts, no cache rebuild (fallback to a docs/81 skip when a cache is invalid). Feasible with extra moves, ~6-9 days. Same steering bound as docs/81 (a) (**~5.8 BP / 7.0 IP ms/s** if steering closes all 83%), but without the 2-tick cap, without suppressing route callbacks, and it extends to the docs/80 gate hits (9%) and sim-thread quadtree readers (8%). Because movable time grows steeply as the last closers go (§9 model), steering + gate + readers together could reach **12-27 ms/s BP** if steering is 75-80% of closers. Build only after the closer probe.
- **"Start the job after locomotion"** (§8): cheap (~1 day) but gives at most the steering share and costs latency: the job loses its overlap with the GC step and the pacing sleep, and plans longer than the post-locomotion part of the iteration are picked up one tick later.

## 2. Locks and lock order found (all roots, 4 hops)

| Lock | Taken at | Held with `+0x918`? | Order | Tag |
|---|---|---|---|---|
| `world+0x918` (recursive) | `0x82EDD0` (`lea ecx,[esi+0x918]` at `0x82EDC1`), `0x82F0D0` | - | first | verified |
| World manager lot index `wm+0x228` | `WorldManager_RebuildLotIndex 0xC68680` (`0xC6869C`), via `World_LotLookup 0xC68990 -> 0xC688A0` (h3-h4 of every query; also called by `RouteFollow_A/B`, `EditPath` before or after the query) | yes (query) and no (callers) | `+0x918` -> `+0x228`; leaf | verified sites |
| Footprint registry `0x11D9068` (role inferred) | `0x80F2F0` (`0x80F37C`) from the footprint delete `0x80F3D0` (sub-object slot 0 adjustors, e.g. `0x624E70 -> 0x624E80`); also `0x80EDC0` (`0x80EDD7`) from slot `+0xA0` | yes when the last reference drops under the lock (`0xAAC090`, `0x82F00F`) | `+0x918` -> `0x11D9068`; leaf | verified sites |
| Object lock `X+0xA0` | `0xB96380` (`0xB963A9`, `0xB96507`), `0xB99150`, `0xB971F0`, via class `0xFEBD10` `+0xF8` `0x80B060 -> 0x809930 -> 0xB99600` | yes (polygon rebuild under the query lock) | `+0x918` -> `X+0xA0`; owner class not identified | verified sites, h4 of the slot |
| Route listener `route+0xB0` | `0x7521B0` (`0x7521CE`) | no: called after `RouteFollow_A` returned (`0x847697` then `0x8476AD`) | independent | verified |
| Allocator critical section | `GeneralAllocator_Memalign 0x4E6A20`, `_Free 0x4E5920`, `_MallocCore 0x4E5E40` | both | leaf | verified (imports) |
| Job side (6 hops from `Route_PlanLocked 0x82C6E0`) | `+0x918` (`0x82C733`), JobManager job locks (`Job_Release 0x59A990`, `Job_Cancel 0x59A540`), `wm+0x228` | - | `+0x918` -> job locks, `+0x228` | verified sites |

No root takes `+0x918` while holding any of the other locks within 4 hops, and the job does not take `route+0xB0` within 6 hops [verified for the scanned closures]. No Win32 wait, event or sleep appears in any closure [verified, import scan].

## 3. Vtable targets resolved

### 3.1 Footprints (vector elements in every query result)

Elements are the sub-object at `obj+4`; refcount at `[sub+4]` (= `obj+8`); the full object's vtable is at `[sub-4]` [verified, `0x82EFA0..0x82EFB6`, `0x846E2C`]. Constructor chain: base `0x807A20` (outer `0x10205B8`, sub `0x1020594`), middle `0x80EED0` (outer `0x1020AE8`, sub `0x1020AC4`) [verified stores]. Concrete classes (outer / sub vtable, constructors) [verified stores]:

| Outer | Sub | Constructors | Slot `+0` kind | `+0xB8` point test | `+0xF8` polygon rebuild |
|---|---|---|---|---|---|
| `0xFEBBC0` | `0xFEBB98` | `UpdateObjectFootprintCore 0x8321F0`, `AddPrivateFootprint 0x831BA0`, `0x624540` | 1 | `0x80E8A0` | `0xB9C560` (leaf) |
| `0xFEBA98` | `0xFEBA74` | `0x836340`, `0x624500` | 1 | `0x80E8A0` | `0xB9C560` |
| `0xFEB848` | `0xFEB820` | `0x80C780` (called by `0x75DE90` and `0x8321F0`), `0x6244B0` | 3 | `0x80E8A0` | `0x80C430` (empty) |
| `0xFEB970` | `0xFEB94C` | `0x6244E0` | 3 | `0x80E8A0` | `0x80C430` |
| `0xFEBE38` | `0xFEBE10` | `0x80B740`, `0x6250E0` | 1 | `0x80E8A0` | `0x80C200`: `LotTable_GetIfPresent`, `World_GetHeightAt`, vtable `+0xF0` |
| `0xFEBD10` | `0xFEBCE8` | `0x807C90`, `0x625070` | 1 | `0x80E8A0` | `0x80B060`: as above plus `0x809930 -> 0xB99600` (`X+0xA0` lock) |
| `0xFEB5F8` | `0xFEB5D4` | `LotGraph_Build 0x63CB90`, `0x624420` | 1 | `0x80E8A0` | `0xB9C560` |
| `0xFEB720` | `0xFEB6F8` | `0x624480` | 1 | `0x80E8A0` | `0xB9C560` |
| `0xFEAE08` | `0xFEADE0` | `0x620C80`, `0x80B3C0` (base-derived; table ends before `+0xDC`) | 1 | `0xBFB320` | none |

Other slots used by steering: `+0xC0` = shared constant stubs (`0x7EAEA0` true, `0x620D60` false; Ghidra names them after unrelated ICs because identical stubs were folded); `+0xDC` `0x80F790` (polygon set of a level, lazy); `+0xE8` `0x80E690` (segment test, `0x7CE550 -> 0x7CD7E0`); sub `+0` deleting destructor adjustor, sub `+4` object id (`0x806FE0`) [verified]. Polygon cache helpers: `0x80E5B0` (one level), `+0x70 0x80E610` and `+0x78 0x80E980` (all levels), `+0xF0 0x80EC30`, `+0xFC 0x80E570` (bounds); `+0x18 0x80E0B0` marks every level invalid [verified bodies]. Whatever calls `+0x18` outside a footprint move was not traced (open).

### 3.2 Locomotion agent and its listener

| Object | Vtable | Slots used by steering | Target, 4 hops | Tag |
|---|---|---|---|---|
| Locomotion agent (`[rec+8]` in `Locomotion_Update`, `[this+0xC]` in `ObstacleAvoidance`) | `0x1044590` (constructor `0x9562F0`) | `+0x60` obstacle ids, `+0x64` goal obstructed, `+0x10` id `0x956480`, `+0x14` id via component `0x954350`, `+0x18` state `0x96B6D0`, `+0x1C` position `0x954380` (`Object_GetComponent`, `0x959000`), `+4` type test `0x956450`, `+0x68` timer | `+0x60` -> listener slot 0; `+0x64` -> listener slot 8; the rest are leaves or sim-only object-model reads | verified (only `.rdata` table matching `+0x60` ret `0xC` / `+0x64` ret 0 with a code reference) |
| Route listener `[agent+0x90]` (set to 0 by agent `+0x44`/`+0x48`) | `0x10047CC` | slot 0 `0x74B280` -> `0x796580` (builds a variant array, `World_FireEvent(0x3A)`); slot 8 `0x7521B0` (lock `+0xB0`, progress fields `+0x60..+0x9C`, `+0xA0/+0xA8`, atomic refcount of `[route+0xEC]` and its vtable `+0x120`, then `0x791510 -> World_FireEvent(0x39)`) | world event delegate table `0x11D6A78 + id*0x24` -> host `+0x134`/`+0x138` -> managed (hop counts in §4) | chains verified; listener type inferred (§1) |

The managed side of events `0x39`/`0x3A` was not resolved statically (delegate registered at run time); `Sims3.SimIFace.Route.EvaluateCallbacks` is the likely receiver [inferred, name only].

### 3.3 Not resolved (open)

`RouteFollow_A` `param_1 +0xC`, `param_2 +0x10/+0x3C` (route-follow context); the reference-counted object at `ObstacleAvoidance` `ebx` (`+0x9C`, slot 0); the gather context object released atomically at `0x82F07A`/`0x82F43E` (set during the gather, type not found); the allocator vtable `+0x94` in `0x6282F0`; the visitor callbacks in the quadtree walks are function pointers (`0x662DD0`/`0x662DB0` for points), not vtables. `ObstacleAvoidance` closure: 135 vtable calls, mostly atomic reference-counted helpers (`RefPtr_Release 0x7460F0`).

## 4. 4-hop tables per root

**`RoutingWorld_ObstacleQuery 0x82EDB0` / `GoalFootprintGather 0x82F0B0`** (31 functions, 14 vtable calls, 21 open)

| Hop | Locks, waits, managed | Shared writes, allocator | Vtable calls |
|---|---|---|---|
| 1 | `+0x918` | caller vector; plain `+1` `0x82EFB6`, plain `-1` `0x82EFF3`; atomic release `0x82F07A`/`0x82F43E` | footprint sub `+0` (delete at zero) |
| 2 | none | `0xAAC090` (plain `-1`, delete), `0x82D030`/`0xAAE690` (sorted insert, plain `+1`), `0x6282F0` (hash set free), `EA_Free` | allocator `+0x94` |
| 3 | none | `QueryPoint 0x667480` / `QuerySegment 0x667400` (`Q+0x48` plain), `LotTable_GetCellGraph 0x62FB10`, `LotTable_Contains`, transform `0x62ED40`, lot grid `0xC688A0` | `0xD2F300` |
| 4 | `wm+0x228` (`0xC68680`) | `FlushDeferred 0x666A40` (outermost), `LotTable_GetOrInsert 0x62EAB0` (insert on a miss), walks `0x663E20`/`0x663B20` | walk visitors |
| open | | apply insert `0x665C50` (calls footprint `+0x70`: polygon rebuild, refcount `+1`), apply remove `0x666750`, `LotTable_Insert 0x62E680`, `LotGraph_ctor/dtor`, quadtree cell helpers `0x669xxx`/`0x66Axxx` | |

**`Locomotion_ObstacleCheck 0x85FD10`** (43 functions, 27 vtable calls) and **`RouteFollow_EditPath 0x85F5A0`** (47, 29)

| Hop | Locks, waits, managed | Shared writes | Vtable calls |
|---|---|---|---|
| 1 | none (reads path node byte `+0x30`) | none | none in `0x85FD10`; `0x85F5A0`: footprint `+0`, `+0xB8`, `+0xC0`, `+0xDC`, `+0xE8`, sub `+0`/`+4` |
| 2 | `+0x918` (`0x82EDB0` from `0x85FD10`; `0x82F0B0` from `0x85F5A0`) | `0x80E5B0` polygon cache (direct from `0x85FA50`), `0xAAC090`, blocking-id vector `0x572400` | polygon slots (§3.1) |
| 3 | `+0x918` (`0x82F0B0` via `EditPath`) | query internals (row 2 above) | `+0xF8`/`+0xFC` rebuild |
| 4 | `wm+0x228`; `X+0xA0` (rebuild of class `0xFEBD10`) | `Q+0x48`, flush, gate `0x62EAB0` (from `0x62EF30` and from `+0xF8` rebuilds) | |
| caller | `Locomotion_ObstacleList 0x85FFE0` -> `Locomotion_UpdateAgent` -> agent `+0x60` -> `0x74B280` -> `0x796580` -> `World_FireEvent(0x3A)` (hop 4 from `0x957210`; managed at hop 5) | | |

`node+0x30` is written only by the job's path builder (`0x819640` at `0x81966C`, `0x819BE0` at `0x819C2D`/`0x819C62`/`0x819CFF`, `0x81A180` at `0x81A1BC`; all reached only from `RoutingJob_Execute`) [verified writes and roots]: it is a plan-time attribute of the node, never cleared by steering, so a skipped check is retried next tick while the Sim is on that segment [inferred]. This closes the docs/81 open item.

**`RouteFollow_A 0x846B70`** (72 functions, 43 vtable calls) with caller **`ObstacleAvoidance 0x847390`** (84, 135)

| Hop | Locks, waits, managed | Shared writes | Vtable calls |
|---|---|---|---|
| 1 | none | plain `+1`/`-1` on footprints | footprint sub `+0`/`+4`, outer `+0`, `+0xB8`; agents in the locomotion list `+0x10`, `+0x14`, `+0x18`, `+0x1C`; context `+0xC`, `+0x10`, `+0x3C` (open) |
| 2 | `+0x918` (`0x82EDB0`) | `0x5ECB30` writes global `0x1157D28`; `Object_GetComponent`, `SimService_Get` (sim-only object model) | polygon slots |
| 3-4 | `wm+0x228` | as `0x82EDB0`; gate via `0x62EF30` | |
| caller | `ObstacleAvoidance`: agent `+0x64` (h1) -> `0x954790` (h2) -> `0x7521B0`: `route+0xB0` lock (h3) -> `0x791510` -> `World_FireEvent(0x39)` (h4) -> managed (h5) | many atomic `lock xadd` (reference-counted helpers) | |

**`RouteFollow_B 0x85B470`** (38 functions, 15 vtable calls): hop 1 `World_LotLookup`, `0x62EF30`, `0xAAC090`, footprint slots; hop 2 `+0x918`; hops 3-4 as `0x82EDB0` (`wm+0x228` at hop 4). No managed call, no event. Its answer is a boolean that rejects a candidate; nothing script-visible [inferred].

## 5. Shared write sites (Q2)

| State | Sites (function: address) | Thread class of the callers | Patch per site |
|---|---|---|---|
| `Q+0x48` query counter (plain `add`, 4 bytes, next instruction relocatable) | Pin `0x666F7E` (+1); Unpin `0x662CF0` (-1); `0x6672E0`: `0x6672F7`/`0x66733A`; `0x667350`: `0x667367`/`0x6673E5`; QuerySegment `0x66740F`/`0x667462`; QueryPoint `0x66748F`/`0x6674D0`; `0x6674F0`: `0x6674FF`/`0x667548`; `0x667560`: `0x66756F`/`0x66759B` (14) | Pin/Unpin: job (`0x631BF0`/`0x63C5B0`), lot save/load (`0x627100`). `0x6672E0`: `LotPlan_Inner`, `0x645310` (job), `0x6462D0` (IC). `0x667350`: via `0x6675E0` (IC, job). Segment: `0x631E20` (steering, `BlockerCheck2d`), `0x643E60` (job, IC). Point: `0x631F10` (steering, job via `0x82EDB0`), `0x631FD0` (footstep/placement, sim, no `+0x918`). `0x6674F0`: `0x631050` (job, IC). `0x667560`: `0x632060` (IC), `0x634930` (job, IC, sim) | `lock add` needs 5 bytes: no in-place patch; a 5-byte jump to a cave per site, moving the next instruction (14 detours, practical) [verified sizes] |
| `Q+0x48`/`Q+0x4C` init | `0x6646C0`/`0x6646C3`, `0x6646FE` | `QuadTreeBuild 0x62F8A0` (job, IC, sim) | not needed |
| `Q+0x4C` deferred flag | set `0x666C89` (Insert), `0x666E64` (Remove); clear `0x666B50` (Flush) | Insert: `0x632460`, `0x6376D0`, `0x639DE0`, `0x63A1C0`, `LotGraph_Build`, `QuadTreeBuild` (job and sim); Remove: `0x6675B0` (job, sim), `UpdateLotWalls 0x830940` (IC); Flush: all six query wrappers, Pin, Insert, Remove | byte store is atomic; the problem is the decision before it (§6) |
| Deferred maps `[Q+0x20]+0x30/+0x3C` | queued in Insert/Remove, drained in Flush (`0x665C50`, `0x666750`, node frees) | as above | entry detours only |
| Footprint refcount `[fp+4]` (plain) | steering 4-hop closure: 10 `+1` and 12 load/add/store `-1` sites (`0x82EDB0`, `0x82F0B0`, `0x82D030`, `0xAAE690`, `0xCA0240`, `RouteFollow_A/B`, `EditPath`, `0xAAC090`); job closure (9 hops from `0x82C6E0`): 13 and 32 (`0x628220`, `0x62F470`, `0x635700`, `0x655880`, `0x6593F0`, `0x6646F0`, `0x665C50`, `0x666750`, `LotPlan_Inner`, `LotRouting_FindFootprintByKey 0x62F600`, `0x82EDB0`) | both | 22 + 45 detours at least (pattern count; some `[reg+4]` may be other objects), plus sites beyond the closures |
| Footprint polygon cache (§1) | steering: `0x80E5B0` via `+0xB8/+0xBC/+0xDC/+0xE8/+0xEC` and `0x85FA50`; job: `+0x70` `0x665D36`, `+0xDC` `0x637192`, `0x634A16`, `+0xFC` `0x80F6F5`; invalidation `+0x18 0x80E0B0` | both | entry detours on `0x80E5B0`, `0x80E610`, `0x80E980`, `0x80EC30`, `0x80E0B0` |

## 6. Can steering stop needing `+0x918` by changing the writes? (Q2)

- **(i) atomic `Q+0x48` + flush only by the lock owner: not feasible alone.** Insert and Remove read the counter and, when it is below 1, flush and mutate the tree at once (`0x666C50`, `0x666E30`) [verified]. With steering incrementing atomically without the lock, the job can pass that check just before the increment and unlink nodes while steering walks. Also leaves the refcount and polygon-cache races. The 14 detours are cheap (~1 day) but buy nothing on their own.
- **(ii) atomic footprint refcounts: necessary only if both sides keep counting.** At least 67 plain sites in the two closures; superseded by not counting at all on the sim side (design E): a footprint found in the quadtree is held by the quadtree, so it cannot be freed while the walker blocks removals [inferred from the delete path: refcount zero only after the quadtree drops its reference].
- **(iii) a separate quadtree lock taken by both for their whole use: no gain.** The planner pins the lot's quadtrees for the whole `LotPlan_Inner` (`0x63FB79` .. 7 unpin sites, docs/81), queries them per expansion (`0x6672E0`) and gathers through `0x82EDB0` itself; a coarse quadtree lock held for those spans equals `+0x918`. Only the fine-grained form works: exclusive per mutation, shared for read-only walks (design E).

## 7. Job vs steering, structure by structure (Q3)

4 hops from `IC_Route_Plan 0x765160` reach only `RouteHandle_Lookup` (h2, handle-table lock), `Route_PlanCommon 0x7606E0` (h3), `Route_GoalStep 0x75C2D0` and `Route_PlanLocked` (h4). The shared structures sit deeper (first hop in brackets) [verified distances]: `0x82EDB0`/`0x82F0B0` and `ApplyPendingUpdates 0x82C420` (5), `RoomGraph_FirstUse 0x63D2F0` and `LotTable_GetOrInsert` (6), `LotGraph_Build`, `LotTable_Insert`, `RebuildLotIndex` (7), Insert, Flush, `QuadTreeBuild`, polygon `+0xDC` in `0x636BF0` (8), `LotPlan_Inner`, apply insert/remove (9), Pin/Unpin, `0x632460`, Remove, `0x643E60`, `0x6672E0` (10).

| Structure | Job | Steering | Split by structure? |
|---|---|---|---|
| Planner scratch (`planner+0x10`, docs/18) | reads/writes | never (no planner function in the steering closures) [verified] | yes, job only |
| Route path nodes (`node+0x30`) | writes for the Sim being planned | reads for walking Sims; the planned Sim is not walking (its task waits, docs/18) | disjoint in practice [inferred] |
| Lot table `P+0x48` | hit; insert/rehash on a miss (lazy) | hit (`0x62FB10`, `0x62EF30`) | no: shared, job writes rarely |
| Per-level quadtrees | pin, query, insert/remove (portal refresh `0x632460`, lazy builds, pending updates), flush | query (counter, flush) | no: both write |
| Footprints | refcounts, polygon caches, portal re-flag | refcounts, polygon caches, reads type/id/polygons | no: both write |
| World manager lot index | read; rebuild under `wm+0x228` | read; rebuild under `wm+0x228` | shared, own lock; the lookup itself reads without it (risk under E, §9) |

Answer: no split by structure helps. The job needs the whole quadtree/footprint layer that steering needs. A split by **operation** does: on that layer the job's writes are short and countable (§8 E), while steering only needs to read.

## 8. Designs (Q4, Q5)

**Sim iteration order** (`MonoScriptHost_Simulate 0xD81840`, main step branch) [verified call order]: `ScriptTick(0)` `0xD818FF` -> `TaskObjects` `0xD81948` (script tasks: `Route.Plan`, job START) -> GC `0xD819AA` -> finalizers `0xD81A09`/`0xD81A30` -> `0x769420` -> `Animation_SACSPass` `0xD81A70` -> `ServiceList_SimUpdate` `0xD81AA2` -> pacing sleep `0xD81AF7`/`0xD81B16` -> `0x76C5E0` -> `0x7FD520` `0xD81C45` -> **`Locomotion_Update` `0xD81C5C`** -> `Animation_UpdateActors` `0xD81C73` -> `ServiceList_SimUpdate` `0xD81C8E` -> `ScriptTick(n)` `0xD81CF5` -> `OneShotObjects` `0xD81D06` -> `ThreadedObjects` `0xD81D0D` -> `0xD7FE80` -> `RunCatchUpSteps` `0xD81D29`. A second branch without locomotion runs services and script ticks only. `+0x918` between the end of locomotion and the next one, 4-6 hops of direct calls: `Animation_UpdateActors -> Locomotion_Notify -> UpdateSimFootprint` (h4, moving-state changes only) and the same from `SACSPass` (h5); none from `0x7FD520`, `0x76C5E0`, `0x8A6D10`, `0x769420`; the service `+0x20` slots listed in docs/36 §3.3 are not routing [inferred]; the script stages reach every `IC_*` acquirer per script call (not bounded statically).

| # | Design | Verdict | Cost | Movable, model below | Risks |
|---|---|---|---|---|---|
| E | **Read-only fast path, split by operation.** Worker: our lock QL (recursive, writer) taken around `LotQuadTree_Insert 0x666C50`, `Remove 0x666E30`, `FlushDeferred 0x666A40`, `QuadTreeBuild 0x62F8A0`, `LotGraph_Build 0x63CB90`, `RoomGraph_FirstUse 0x63D2F0`, `LotTable_Insert 0x62E680`, `0x632460`, `ApplyPendingUpdates 0x82C420`, polygon cache `0x80E5B0`/`0x80E610`/`0x80E980`/`0x80EC30`, invalidation `0x80E0B0` and the footprint delete `0x80F3D0` family, only on the worker thread (job holds `+0x918` first: order `+0x918` -> QL). Sim, only while a job owns `+0x918`: at the 5 query call sites (`0x85FE0B`, `0x85FE7C`, `0x846D1C`, `0x85B507`, `0x85F6C6`) take QL shared, run `0x631F10`/`0x631E20` minus the counter and flush (call the walks `0x663E20`/`0x663B20` directly), return raw pointers, and set a per-thread "no-ref" flag that turns the caller's refcount sites and `0xAAC090` into no-ops until the caller returns; release QL before the agent `+0x60`/`+0x64` calls. If any returned footprint has an invalid cache byte (`+0x20 == 0`) or the lot table misses: drop QL and answer "nothing new" (docs/81 skip) | feasible with extra moves | 6-9 days (detours ~14 worker + ~25 sim sites, own walk wrapper, probes) | steering share s up to 0.83 -> 5.8 BP / 7.0 IP; with gate hits and quadtree readers on the same path (+0.17) see table | footprint lifetime argument (§6 ii) unverified; `+0x18` callers on the worker not traced; lot index read race; a missed worker mutation site corrupts the tree (probe `vfQlMiss`) |
| A | docs/81 caller-level try-lock skip/reuse (cap 2) | feasible | ~1-1.5 days | same steering bound, less for jobs longer than 2 iterations | **new:** a skip also drops world events `0x3A`/`0x39` (route callbacks in C#) for that tick; a reused id re-fires `0x39` |
| F | Start the job after `Locomotion_Update` returns (queue START at `Route.Plan`, drain after `0xD81C5C`) | feasible | ~1 day | at most steering share x P(job < W), W = iteration - locomotion ~ 24 ms (locomotion ~2.4% of sim samples ~ 0.6 ms of 25 ms) [inferred] | the job no longer overlaps GC, SACS, services and the pacing sleep; it now meets the script stages and the next `TaskObjects`; plans longer than the post-locomotion part are read one tick later (Sim walks a tick later, docs/18 "dead time") |
| i/ii/iii | atomics only, or a coarse quadtree lock (§6) | not feasible / no gain | - | 0 | - |
| C | queue whole steering checks to the worker | not feasible | - | - | world events `0x39`/`0x3A`, object-model reads and the agent list are sim-only |
| B | per-job snapshot of footprints (docs/81) | feasible with extra moves | several days | as E for steering | superseded by E (E reads live data safely) |

**Model** (docs/81 §6) [inferred]: f = (1 - e^(-x)) / x, x = 55.6 x (1 - s), s = share of window closers a design removes; movable = f x plan time (BP 55, IP 66 ms/s).

| Removed closers | s | f | BP ms/s | IP ms/s |
|---|---|---|---|---|
| steering = half of the 83% | 0.415 | 3.1% | 1.7 | 2.0 |
| steering = all of the 83% (A, E, F upper bound) | 0.83 | 10.6% | 5.8 | 7.0 |
| half steering + gate hits + readers (E extended) | 0.585 | 4.3% | 2.4 | 2.9 |
| all steering + gate hits (E + docs/80 seqlock) | 0.92 | 22.2% | 12.2 | 14.7 |
| E extended with steering 78% (s 0.95) | 0.95 | 33.7% | 18.6 | 22.3 |
| E extended with steering 80% (s 0.97) | 0.97 | 48.6% | 26.8 | 32.1 |

The curve is steep near s = 1: each design alone stays under the docs/18 go rule (15 BP / 30 IP), but E plus the docs/80 gate fix crosses it only if steering is at least ~75% of the outermost `+0x918` closers. The probes decide which branch.

## 9. Probes before building (ts3native `[Verify]` style, observe only)

| Counter | What | Gates |
|---|---|---|
| `vfG11CloseBy` (docs/80/81) | closer per window by return address: the 5 steering sites, `0x631FD0`, `0x82E340`, gate families, footprint writers, script ICs | s for every row above |
| `vfFpCache` | at the steering polygon calls during a job: footprints with an invalid level cache (`[fp+0x38]+lvl*0x24+0x20 == 0`), and worker calls of `0x80E5B0`/`0x80E610`/`0x80E0B0` | E fallback rate; whether the worker invalidates caches |
| `vfQlWrites` | per job: calls and time in the E writer list | E exclusive hold time (sim wait) |
| `vfQlMiss` | per job: `Q+0x4C` set, node count or map size changed outside the writer list (compare before/after each writer) | completeness of the E writer list |
| `vfLotIdx` | `WorldManager_RebuildLotIndex` calls during jobs, per thread | lot index read race under E |
| `vfEvt39/3A` | per tick: world events `0x39` (type `0x12`) and `0x3A` from `0x8476AD` / `0x9573DB` | how much script behaviour a skip (A) suppresses |
| `vfStageMs` | per iteration: time per Simulate stage and job end relative to the next locomotion and next `TaskObjects` | F window, tick cost |

## 10. Open beyond hop 4

- Managed receivers of world events `0x39`/`0x3A` (run-time delegate table); what C# does on them (route callbacks, replans).
- Callers of footprint `+0x18` (`0x80E0B0`) and whether the worker reaches them.
- The 92 nested vtable calls inside footprint slot closures; `0xB99600` object (owner of `X+0xA0`).
- Hop 5+ of the queries: apply insert/remove (`0x665C50`, `0x666750`) and their footprint `+0x70` rebuild chain, lot table miss path, quadtree cell helpers.
- Unresolved vtables of §3.3.
