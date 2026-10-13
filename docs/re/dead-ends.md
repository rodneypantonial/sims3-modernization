# Dead ends: what we tried or analysed and dropped

Read this before starting something big: each entry says what was tried, the evidence, why it failed and what would have to change to make it worth another look. "Paused" means the code exists and is kept opt-in/off; "dropped" means no code is kept or it was never built.

Addresses are TS3W.exe Steam 1.67 (see [README](README.md)). Measurements are 10-minute in-game runs unless noted (BP = Bridgeport, SV = Sunset Valley, IP = Isla Paradiso; it/s = sim-loop iterations per second; hitches = iterations of 50 ms or more).

---

## 1. Async route plan (ts3native 0.5-0.7 `[RouteAsync]`): paused

**Tried.** Run the whole `IC_Route_Plan 0x765160` on a worker thread while the Sim's own script task sleeps tick by tick, so the sim thread keeps simulating. Handshake through `Route.GetOption` on unused RouteOption bit 58; the C# side (ts3fixes `Route.Plan` hook) yields until the result is ready. Blocking `Lock_Acquire` waits on the sim thread serve the worker's managed calls.

**Three in-game crashes, each root-caused:**
1. **Yield inside a nested invoke** (`interp.c:3595 pState->yieldReturn`). The Sim's task yielded while `Route.Plan` ran inside a native-to-managed call. EA's yield mark only sets `yieldReturn` on the task's root interpreter state, but the opcode asserts it on the current state. Fixed in 0.5.1/0.6.1: START refused unless the current state is the task's root state; ts3fixes `MayYield()` guards its other yields.
2. **Host delegate call `0xD75FA0` on the worker** cleared the running task id (host+0xBD0/+0xBD4) of the task the sim thread was running at the same time, and boxed values on a thread the GC does not scan. Assert plus a hang. Fixed in 0.7.1: the whole host call is proxied to the sim thread.
3. **Heap corruption** (free in the node table `0x61C020`). The goal step of every plan (`Route_PlanCommon 0x7606E0` -> `0x75C2D0` -> `0x63D2F0` -> A* reset `0x61E320`) runs **outside** the routing lock (world+0x918) on lot-planner state shared by all callers (`0x62EAB0` find-or-insert into the shared lot table). The worker and the sim thread ran it at once.

**Option B (thread-private A* scratch) is a dead end as designed** ([docs/18](../18-route-search-off-thread.md) "option B"): the "per-lot search object" is a lazily built, shared lot-graph cache, not scratch. The game has exactly one `cAStarSearch` (0x9C90 bytes), which keeps each search's state inside nodes that point at shared lot cells with non-atomic refcounts. A private search object needs ~21 load-site patches and still needs the routing lock around the goal step, at which point the lock alone serializes everything.

**What would make it worth revisiting.** The "routing gate": the worker holds world+0x918 for the whole job (start points, goal step, PlanLocked) and every unlocked sim-thread entry into the lot planner takes it too (about 10 small detours, recursive lock, no EA data changes). The gain is then limited to sim-thread work that touches no routing lock. Measure `asyncPumpUs` against `asyncUs` first. Making searches cheaper was judged the better bet.

---

## 2. Route worker, synchronous mode (`[RouteWorker] Mode=1`): paused, no gain

**Tried.** Every `Route_PlanLocked 0x82C6E0` runs on a worker while the calling thread waits; managed calls from the planner (runtime_invoke, host delegate call) are run back on the waiting caller.

**Evidence.** First run crashed (`mono_class_vtable` with domain NULL at `0xEA2B03`: the worker is no Mono thread); fixed by proxying managed calls. Then stable: `bp-worker1` 8,702 plans on the worker, 45,195 proxied calls, 0 fallbacks, 0 watchdogs, 0 stuck Sims; later 11,523 plans, 46,152 host calls, 0 crashes. But 28.7 it/s and 123 ms/s route time vs 37.5 it/s and 86 ms/s without it, and the allocator lock wait rose to 3.4% of sim-thread samples.

**Why.** The sim thread still waits for every plan, so offloading only adds hand-off cost and lock contention. It was the stepping stone for async plans (entry 1).

**Revisit when** an async design that does not wait (the routing gate) is in place.

---

## 3. Route search budget (`[RouteBudget]`, ts3native 0.4): paused

**Tried.** Past `MaxUs` (100 ms) or `MaxNodes`, every A* edge cost returns a wall, so the world A* `0x676260` drains and fails through its own "open list empty" exit. Five `mov [esp+0x60], imm32` sites swapped for stubs.

**Evidence.** BP 10 min with the budget installed (switch alternated on/off every 2 min): **41 stuck Sims** on every trip kind, although only one search was capped. Control with it off: 0 stuck. Stub review found the cost contract intact; real bugs found and fixed in 0.4.2 (the clock started before the routing lock was taken, so lock waits counted against the budget; one unprotected global instead of per-thread state). The `Route_PlanLocked` detour alone was later cleared (`bp-detour2`: 24,260 plans through it, 0 stuck).

**Why it failed.** Not fully known; the stranding appeared with the stubs installed even when the switch was off. 0.4.2 is built and self-tested but not rerun in game.

**Revisit** with 0.4.2 on a short single-Sim repro first, then `--ab routebudget`; stop at the first stranded Sim. A cap must never fail a search that would have succeeded (`budgetMaxNodesOk` tells where the cap is safe).

---

## 4. Route negative cache as the main routing fix: deprioritized

**Tried / analysed.** A cache at the `Route_PlanLocked` entry that answers repeat no-path requests with a plain failure before the lock (ts3native 0.2.0 `[RouteCache]`, built).

**Evidence.** Failures are only ~2% of plans in BP ([docs/04 F5](../04-re-findings.md)); SV and IP fail 7-12%, but route cost is mostly successful searches. The managed per-Sim backoff (ts3fixes `RouteBackoff.cs`) already covers repeat failures from script.

**Why.** Small target. Kept opt-in, never run in game (`bp-route-cache` pending).

**Revisit** if a world shows failure storms (many identical failing requests per second).

---

## 5. Parallel animation update: not built

**Analysed** ([docs/22](../22-parallel-animation.md)): run the per-actor rig pose `0x940430` from the animation update `0x571B90` as a parallel-for over actors.

**Why not.** Unsafe as a plain parallel-for: clip events fire synchronously during Advance into TrackManager and the SACS WalkerClipMonitor (unlocked vector pushes), the routing/locomotion notify runs inside it, and several steps touch shared refcounts and globals. A phase split (serial advance and events, parallel blend math, serial publish) is possible, but the whole family is ~33 ms/s and the parallel part saves an estimated 5-15 ms/s, only on lots with many animating Sims, at large code size and risk.

**Revisit** if animation grows to a larger share (many Sims on one lot) and every per-controller Evaluate vtable has been read for shared writes.

---

## 6. Faster RefPack decoder (ts3native 0.7 `[Decompress]`): built, small gain

**Tried.** Retarget the one call `0x4EC065` -> `0x4EB3B0` to a faster decoder with wild copies. Offline on 3.7 GB of shipped resources: identical output, 2.2x a C port of the game's decoder, 1.8x S3SS's `RefPackDecompressor`.

**Why it is not a win.** Decoding is only **0.24% of sim-thread time** in live mode (15 profiles); expected load gain ~0.2-0.5 s. Kept opt-in.

**Revisit** only for load-time work with many compressed packages.

---

## 7. 64-bit resource offload helper (docs/08): dropped

**Analysed and measured** ([docs/08](../08-resource-offload-design.md) Phase 0): a 64-bit helper process that holds the resource cache, package indexes and decoded resources outside the 32-bit address space.

**Evidence.** Package indexes are tiny (~17.6 B per resource: ~3.5 MB for 172 CC packages / 209,735 resources, ~15 MB for all EA packages). Packages are not memory-mapped. The resource cache holds 390-410 MB in BP **whatever the budget** (200, 50 or 0 MB): it is resources in use by the loaded world; only ~10-20 MB is cold. 143 CC packages added nothing measurable.

**Why.** At most ~20 MB of address space to win for an estimated 60-100 h of work.

**Revisit** only if a different workload (store repacks in use, placed store items) shows a large cold share.

---

## 8. Lowering or raising `CacheBudget`: no effect

**Tried.** `[Resources] CacheBudget` 200 / 50 / 0 MB in Phase 0 (installer experiment `cache-budget`).

**Evidence.** Cache average 415 / 410 / 394 MB, commit ~2.0 GB and largest free block ~1.55 GB in every variant; no pace change. The cache ignores its budget for resources in use (501 MB with store content).

**Why.** Nothing evictable to free. Note: `[ResourceSystem] MemoryUsageLimit` is the async loader's limit, not the cache, and `HoldOntoResource` (singular) in `Sims3.ini` is ignored (the engine reads `HoldOntoResources`).

---

## 9. Clearing caches every launch: not worth it

**Measured** ([docs/17 §6b](../17-heavy-store-content.md)): with CC and store content, cold caches 113 s load vs 104 s warm. Warm caches save ~9 s, mostly in the world load. Clearing every launch only costs time; the installer clears caches only after content changes.

---

## 10. Traffic car shortcut: dropped

**Tried** ([docs/04 F10](../04-re-findings.md)): an unseen ambient car (`Route.PlanToPoint` for a driverless NPC car) arrives instead of driving.

**Evidence.** BP: ~24 stuck Sims in 2.5 min with it vs 7 without (eating out, meet-ups, shows, visits); gain ~1%.

**Why.** The hook acted inside a route on an object other systems still tracked (driver, passengers, car assignment state). The same shape rules out elevator, boat, subway-portal and hover-train shortcuts ([docs/10](../10-expansion-travel-shortcuts.md)): shortcut only whole trips whose ends the game already handles.

---

## 11. JPS+ precompute for the water route search: not built

**Analysed** ([docs/15 Phase 9](../15-native-patches.md)): precompute jump distances per water grid instead of scanning.

**Why not.** A jump depends on the goal and on the callback's start/goal boxes, so results do not carry over between searches, and per-grid tables need an invalidation point for the routing world's grids (+0x7B0 / +0x7C0: houseboats, terrain edits) that we have not found. Built instead: the same jump as one inlined loop (`WaterJump`, 3.9x faster offline, opt-in).

**Revisit** once a hook is found that fires whenever the water grids change.

---

## 12. 64-bit rebuild of the game: not realistic

40,263 native functions plus EA's embedded Mono 1.2. Only possible piece by piece (per-function rewrites verified live with the probe). The 4 GB LAA limit stays; we can only waste less of it.

---

## 13. Replacing the native route planner, or a learned route model: not doing

**Why** (PLAN, step 4 (d)): the routing world (footprints, slots, portals, water, cars, ladders, gangways, elevators) lives in native data; a replacement planner is months of work with a high risk of stuck Sims. A learned model cannot guarantee exactly valid paths and would cost more than A* in a 32-bit process. Simple prediction ("this failed recently") is the negative cache (entry 4).

---

## 14. Mid-game managed method swaps (hot patching): does not work

**Tried** ([docs/04 F8](../04-re-findings.md)): copy a replacement's 40-byte MonoMethod header over the game method's while the game runs.

**Evidence.** A swap reaches only callers the interpreter compiles after it (and reflection). EA's interpreter binds call targets when it compiles a caller, so already-compiled callers keep the old code.

**Consequence.** ts3fixes applies every patch once at startup, before any world loads, never on a hot reload; each patch checks that the target's interpreter info (`MonoMethod+0x14`) is still empty.

---

## 15. Native offload of Create-a-Sim grid building: not built

**Analysed** ([docs/21](../21-cas-offload.md)): hair takes 3.2 s to open (261 tiles, ~1 s is the tab switch, ~2.2 s a synchronous Mono tile loop).

**Why not native.** Every per-tile call goes into a shared engine service: the UI window system, the global `ObjectDesignerService` lock (`0x106B874`) and its "current part" field, the resource manager. Same proxying problem as the route worker with less gain. Pre-generating thumbnails does nothing for the 3.2 s (they are async and cached on disk). Compositing is already queued and timesliced.

**Done instead.** Script-side incremental fill and per-part memo (ts3fixes `CasGrid.cs`, opt-in).

---

## 16. Save write offload beyond the backup copy: no gain

**Analysed** ([docs/15 Phase 7](../15-native-patches.md)): of the native save steps, only the backup copy of the old save reads data nothing else writes. Copying the new save into the save folder and writing `1a5tf1l3.dat` need the package write done first, so moving them only adds a wait. Script serialization and the package write stay on the sim thread (Mono callbacks). Only the backup copy goes to a worker (`[SaveWrite] Mode=1`, untested in game).

---

## 17. Attaching the route worker to Mono/Boehm: not possible cheaply

**Analysed** ([docs/15 Phase 3](../15-native-patches.md)): the Boehm thread table (`0x1221000`, 256 entries) only ever gets the `GC_init` thread; the game has no wrapper that registers other threads, and Mono needs domain TLS and interpreter state. So native worker threads must never run managed code; every managed call goes back to the owning thread.

---

## 18. Smaller items

| Idea | Why dropped | Source |
|---|---|---|
| `GC_DONT_GC` / disabling `GC_try_to_collect` (community "Chunky Patch") | memory runs out within minutes; collection still happens from allocation under pressure | [docs/13](../13-gc-tuning.md), [docs/03](../03-prior-art-re.md) |
| `GC_MAXIMUM_HEAP_SIZE` | at the cap allocation returns NIL after 2 retries: Mono out-of-memory crash | [docs/13](../13-gc-tuning.md) |
| Route plans cut by caching routability checks (`routablecache` + `stuckcheckbusy`) | in-load A/B BP: 38.8 vs 38.2 it/s, 2.68 vs 2.64 hitches/s, no measurable change (no 3 AM check in the window) | PLAN |
| Splitting `ScriptProxy.Simulate` time by target with unsuspended reads | the proxy pointer races; needs reads in the suspended window | [docs/04 F6a](../04-re-findings.md) |
| Mapping `frame[1]` to a MonoMethod by memory scan (`/rmname`) | no match found, ~35 s per scan; solved instead via `info+8` | [docs/04 F6a](../04-re-findings.md) |
| "RAM booster" tools, HeapSize ini edits, raising `DBCacheMaxSizeMB` | myths or no evidence | [docs/17](../17-heavy-store-content.md), [docs/01](../01-existing-fixes-comparison.md) |
| Moving houseboats instead of a docked-houseboat shortcut | boats moving cost 0.3%; boarding via gangways 5-8% | PLAN archive |
