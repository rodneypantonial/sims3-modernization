# IC safety, part A: sim, world, objects, routing, queries, animation

Thread-safety class of every internal call (IC) bound in `ScriptCore.Simulator`, `World`, `Objects`, `Slots`, `Route`, `RouteManager`, `RouteEvent`, `Queries`, `Animation`, `LookAtTask` and `LookAtMgr`: **672 ICs** of the 1,758 in `internal-calls-map.csv` (task T-IC-a). The question is what would break if a second managed thread (docs/30) called the IC instead of the sim thread. Data: `ic-safety-a.csv`. Static only: no game run.

Lot, Sim/SimDescription and autonomy natives have no class of their own in the binding table. They are `World_*` ICs (`World_GetLot*`, `World_LotSet*`, `World_IsSim*`, `World_FindGoodLocation`, ...) and are covered under `World`. `CASUtils` (SimDescription building) is not in part A.

## Result

| Verdict | Count | Meaning |
|---|---|---|
| UNSAFE | 283 | reads sim-mutated state (objects, lots, WorldManager, routing, physics, script handles) with no lock that covers it; racy from a second thread |
| PROXY | 379 | writes sim state or calls back into managed code; forward to the sim thread (void commands can be queued, value returns need a synchronous proxy) |
| SAFE | 8 | empty stubs (`World_SetRoofSlopeAngleImpl`, `World_LoadTickImpl`, ...), one flag read, `RouteManager_PlaceDestinationPointImpl` (pure copy) |
| MAIN | 2 | reach D3D device code on the calling thread with no owner check |

Per class: World 161 UNSAFE / 206 PROXY / 3 SAFE / 2 MAIN; Route 43/53; RouteManager 11/49/2; Simulator 12/15/2; Objects 12/9; LookAtTask 13/19; Animation 4/17/1; Slots 9/7; RouteEvent 9/2; Queries 9/1; LookAtMgr 0/1.

Confidence: 156 `verified`, 516 `inferred`. Verdict for practical purposes: **almost nothing in this set can run off the sim thread as is.** The handle and route-handle lookups are locked, but the object, lot or route they return is not.

## Notable findings

- **MAIN, device on the sim thread** [verified call path]: `World_ObjectDesigner_ShareDesignPresetImpl` builds a thumbnail texture on the calling thread (`0xB3AE00` -> `0xD61060` -> `ThumbnailManager_CreateThumbnailTexture` -> `Texture_Create2D` / `Texture_FillLevels`), the same missing hand-off as the 12 thumbnail ICs in docs/43. `World_SetLightingValuesImpl` reaches `Device_ApplyGammaRamp` (`0x6C4770` -> `0x6C3480` -> `0x6C29E0`), with no owner-thread check in the direct-call path.
- **Reads that call back into managed code** [verified direct-call path; whether the event fires each call is inferred]: `World_CanSeeObjectImpl`, `Slots_CanAttachToSlotImpl` / `WithOffsetImpl` and `Query_GetObjectsOverlappingObject` reach `World_FireEvent`, which runs managed delegates (host `+0x134` `ScriptHost_InvokeDelegate`, `+0x138` call by name). A query IC can re-enter script.
- **Physics queries** (`Query_GetObjectsInRange`, `CountObjectsInRange`, `GetSimsOverlappingFootprint`, `World_CanSee*`, `World_FindPlaceOutsideLot`): UNSAFE [verified, docs/33]. Readers take no lock, and the tree is written on the sim thread.
- **Route planning** (`Route_Plan`, `PlanFromPoint`, `Replan`, `ReplanFromPosition`): UNSAFE [verified, docs/18]. They plan under `RoutingWorld+0x918`, but per-lot planner state is not locked.
- **Self-marshalling writers**: `World_CreateHouseboatLotImpl` and `World_LotClusterTreesImpl` reach the physics writers that check `GetSimThreadId` and post `PhysicsSimMsg` (docs/33). `World_BuildRoutingDataImpl` dispatches through `0x7D98C0`. 7 lot/height-map ICs touch the JobManager (`0x11CE2CC`), and 34 post render messages. These hand-offs cover only that sub-step, not the rest of the IC.
- **Thread checks**: only 2 ICs reach `0x7D99F0` / `[0x11D8A8C]` within depth 4, both through the physics writers. No IC in this set checks its own thread.
- **Script handles are locked, objects are not**: 155 ICs resolve a script handle through host `+0xCC` (`ScriptHandles_HandleToPtr`, under `Lock_Acquire`), and 108 ICs touch the route handle table (`RouteHandle_Lookup` , under `g_RouteHandleTableLock`). The 282 `Lock_Acquire` hits are nearly all these lookups and the allocator. They protect the table, not the object state read after the lookup.
- **Managed allocation**: 47 ICs. Native bodies call `mono_array_new` or `GC_malloc` (22 through host `+0xC0`, the ObjectGuid array builder `0xD798F0`), or return a managed string or array (inferred from the C# return type). All of them need the GC thread registration of docs/30.

## Method

1. IC list: `game-copy/analysis/internal-calls-map.csv`, filtered to the classes above (672 rows, 3 shared addresses).
2. Ghidra copy `game-copy/ghidra-ica` (from `ghidra-ui4`), with `ts3w-1.67.symbols.csv` imported, read-only runs.
3. `tools/ghidra/IcSafetyDepth4.java` (new; extends the `UIBoundary` BFS): for each IC, a breadth-first scan over direct calls to depth 4 (cap 1,500 functions; thunks followed). It records:
   - thread checks: `0x7D99F0`, `0x7D99D0`, `Device_IsOwnerThread`, the `0x11D8A88` / `0x11D8A8C` globals;
   - posts: `0x7D98C0`, `PostRenderMessage`, `PhysicsSimMsg_ctor`, `UTFWin_QueuePendingTexture`, JobManager `0x11CE2CC`;
   - locks: `Lock_Acquire`, critical sections, Interlocked, waits, `Route_PlanLocked`, `RoutingWorld_ObstacleQuery`;
   - managed calls: `mono_runtime_invoke`, the interpreter invoke, `ScriptHost_InvokeDelegate`, `World_FireEvent`, `0xEA1090` [inferred invoke];
   - managed allocation: `mono_array_new`, `mono_object_new`, `mono_string_new`, `GC_malloc`, `GC_allocobj`, and helpers `0xEA3FE0` / `0xEA3140` / `0xEA4160` [inferred];
   - device calls: `Texture_*`, `Device_*`;
   - object and routing mutators;
   - every read and write of a writable global to depth 3;
   - stores through non-stack pointers at depth 1 or less;
   - virtual calls at depth 1 or less.
   GC and interpreter internals (`0xE40000`-`0xE67000`) are not expanded.
4. **Script host vtable resolution** [verified]: `ScriptHost_GetWorld` returns `MonoScriptHost+8` (`g_ScriptHostForWorldEvents` is written as `g_MonoScriptHost + 8` in `0xD7F2B0`), whose vtable is `0x108B690`. The primary vtable `0x108B818` is checked against docs/27 (`+0x28` = `0xD70EE0`). The scan resolves the first vcall after `ScriptHost_GetWorld` (direct `[reg+disp]` or `MOV reg,[reg+disp]; CALL reg`) and follows it. `+0x17C` (`0xD7CFD0`) returns `this-8`, the primary host, and the next vcall is resolved against `0x108B818`. This closes the docs/50 open item: `IC_Simulator_CreateObjectImpl` = host `+0x17C` then primary `+0x04` = `0xD7B550`. Slots seen: `+0xCC` handle to pointer (155 ICs), `+0x134` invoke delegate, `+0x138` call managed by name (`0xD7D2F0`: method lookup, marshals args, invokes), `+0xC0` / `+0xC4` ObjectGuid array and map, `+0x58` / `+0x68`-`+0x78` pointer and array helpers. `+0x10` (`MonoScriptHost_Init`) came out of 3 misresolved calls and is discarded.
5. `tools/ic-safety-classify.ts` turns the scan into verdicts, applying the first rule that matches:
   1. empty stub: SAFE;
   2. device reach: MAIN;
   3. managed callback: PROXY;
   4. the method name is a command (not Get/Is/Has/Can/Count/Find/Test/...) and the IC touches state or stores through a pointer: PROXY;
   5. a command reached only through vcalls: PROXY;
   6. a read with only unresolved vcalls: UNSAFE (conservative);
   7. no sim-mutated state: SAFE;
   8. state read only under the routing lock: SAFE;
   9. otherwise: UNSAFE.
   Sim-mutated state means every writable global except ones set once at init (host pointer, heaps, resource manager pointer, thread ids, JobManager pointer), plus object components, `ObjectService`, `WorldManager`, lot, routing-world and script-host-field access found by callee name. Return types come from the decompiled `ScriptCore` extern declarations (read locally, not committed).
6. Overrides from earlier verified docs (`ic-safety-a-overrides.json`): docs/18 route planning, docs/33 physics readers, docs/45 and docs/50 object insert and remove, `Query_*OnLot`, plus the two MAIN paths checked here.
7. Confidence `verified` means one of two things. Either the row is an override backed by a verified doc or a call path checked here, or the IC's closure is small (30 functions or fewer) with no virtual call at depth 1 or less, so the scan saw every call. The verdict rule itself is still mechanical. Everything else is `inferred`.

## Limits

- Virtual calls are followed only for the script host. Object, component and lot vtables are not, so state behind them is seen only through callee names or not at all. That is why the rule counts unresolved vcalls as unsafe.
- Depth 4 truncates deep paths. `posts` and `threadcheck` mean "reached in the direct-call graph", not "taken on every call".
- `writes` for commands is inferred from the name plus any global writes and pointer stores. Object field writes through `this` are not separated from out-parameter stores.
- Reproduce: copy the Ghidra project and import the symbols, run `IcSafetyDepth4.java <ics.csv> <out.tsv> 4` headless (read-only), then run `node tools/ic-safety-classify.ts <out.tsv> <decompiled ScriptCore dir> docs/data/ic-safety-a.csv docs/data/ic-safety-a-overrides.json`.
