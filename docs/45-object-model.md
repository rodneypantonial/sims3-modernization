# 45. Native object model: Object, components, object id hash, script handles (M-CORE2a)

Static map of TS3W.exe (Steam 1.67), done on a Ghidra copy with our symbols applied. No game runs. Sim-thread costs come only from the existing `probe-out/profile/*simstack*.jsonl` files (16 files, 10,251 samples: BP 4,077, SV 1,723, IP 4,451, base and fix configs mixed). 1% of sim samples is about 10 ms/s (docs/28).

## 1. Summary

- **A game object is one `Sims3::Objects::Object` block with its components inside it.** `Object_Allocate 0x9361D0` sizes one allocation (tag `Objects/Object`) for the 0x34-byte header, one pointer per component and every component body (size and alignment from the component descriptor), then runs each component constructor in place [verified]. There is no RTTI and no class hierarchy for game objects: `vt_Object 0x1042CA8` has one slot (deleting dtor); behaviour lives in the 15 component types and in C# [verified].
- **Components are a 32-bit mask.** `ComponentsService_RegisterAll 0x933FB0` registers 15 types at startup in a fixed order; `ObjectService_RegisterComponent 0x938A10` hands out bits 1, 2, 4, ... and each caller stores its bit in a global (`g_CompBit_Transform 0x11E9158`, ...). `Object_GetComponent 0x935770` is mask test + popcount into the pointer array at `+0x34` [verified].
- **Script `ObjectGuid` (64-bit) resolves through one hash in the `Objects/Service`**, not through a handle table: `ObjectService_FindById 0x939560` -> `ObjectId_Lookup 0x939100`, 1,033 fixed buckets (`id_lo % 0x409`), chain linked through the object itself (`+0x10`), **no lock, no generation counter, no AddRef** [verified]. 438 call sites, 222 of them in ICs [verified xrefs].
- **Script `uint` handles are a different table:** `ScriptHandles_*` in the script host base (`0x76A4C0-0x76AC40`), reached through host vtable `+0xC8`/`+0xCC`/`+0xD0`/`+0xD4` (docs/42 already saw `+0xCC`). Handle = `tag << 24 | random 24 bits`; one global lock `g_ScriptHandleLock 0x10076DC` around every call; a refcount per handle; no generation counter [verified]. UI windows (docs/43 "handle lookup, virtual call") and other native pointers handed to script go through it. Route handles use their own table (`RouteHandle_Lookup 0x74B2C0`, tag `0x25`, docs/18).
- **Cost on the sim thread [measured, leaf method §6]:** object id lookup 1.73% self (BP 2.06%, SV 1.33%, IP 1.44%), the largest single native leaf after the interpreter, GC and allocator; script host ObjectGuid maps `0xD726F0` 0.59%; `Query_GetObjectsOnLot_impl` 0.81%; `Object_GetComponent` 0.38%; script `uint` handle lookups 0.34%. **The 5.9% "inclusive" that `sample-split.ts` reports for `ScriptHandles_HandleToPtr` is a stale-stack artifact** (§6).
- **Threads:** all inserts and removes run on the sim thread in the paths found (ICs, world/lot load and unload, which docs/38 puts on the sim thread) [verified call graph]; with no lock, any off-thread reader is a race. One reader to watch is `Portal_LockState 0x64F2C0` (route search, docs/18) [inferred, §5].
- **Candidates:** O1 bigger id hash (only two functions hold the modulo, §7), O2 last-hit cache, O3 lot object query cost; all need a chain-length counter first.

## 2. Object layout (`Sims3::Objects::Object`)

From `Object_Allocate 0x9361D0`, `Object_DebugDump 0x935FA0` (format `"Object 0x%016I64x (components: 0x%08x, key: %hs, addr: %08p, refs: %d)"`), `ObjectId_Lookup`, `ObjectService_DestroyById 0x939420` and `ObjectService_RemoveFromUpdateList 0x938CA0`.

| Offset | Field | Status |
|---|---|---|
| `+0x00` | vtable: `0x107396C` (refcounted base, slot 0 = shared dtor `0x624F80`) while building, then `vt_Object 0x1042CA8` | [verified] |
| `+0x04` | refcount, changed only with `LOCK` instructions; dropping to 0 calls vtable slot 0 | [verified] |
| `+0x08` / `+0x0C` | 64-bit object id (`ObjectGuid` in script) = hash key | [verified] |
| `+0x10` | next object in the same id hash bucket (intrusive chain) | [verified] |
| `+0x18`..`+0x24` | resource key (16 bytes; printed as `key` in the dump) | [verified copy; meaning from the dump string] |
| `+0x28` | low 22 bits: index in `ObjectsToUpdate` (0 = not listed); bit `0x400000`: destroyed; top bits kept by the update-list code | [verified] |
| `+0x2C` | component mask | [verified] |
| `+0x30` | 0 at creation | [verified write, role unknown] |
| `+0x34` | component pointers, one per set mask bit, ordered by bit | [verified] |
| after | component bodies, each aligned to its descriptor alignment (4, 8 or 16) | [verified] |

Components have their own vtables: `+0x24` debug text (used by the dump), `+0x2C` destroy (called by `Object_Destruct 0x935860` for every pointer) [verified]. Memory is returned through the service allocator (`ObjectService_GetAllocator 0x938A00` = `+0x1040`, vtable `+0xC` free) [verified].

## 3. Component types

Descriptors are static records in `.data` (`0x117B7B0`-`0x117BC50`). Layout: `+0x00` name, `+0x04` on-registered callback (gets the bit), `+0x0C` constructor `(memory, object)`, `+0x10` second hook (often the shared stub `0x933CA0`), `+0x14` stub `0x933CD0`, `+0x18` optional "implied by mask" predicate used by `ObjectService_ResolveComponentMask 0x938D70`, `+0x1C` size, `+0x20` alignment [verified for all 15; roles of `+0x04`, `+0x10` inferred from use].

Registration order in `ComponentsService_RegisterAll` (service `0xB2D3889B`, `Objects/ComponentsService`) and so the bit [verified: the register function doubles `+0x11C4` from 1 per call]:

| Bit | Component | Mask global | Descriptor | Ctor | Size / align | IO and other tags |
|---|---|---|---|---|---|---|
| `0x1` | Transform | `0x11E9158` | `0x117BB80` | `0x95A340` | 0x40 / 16 | `Objects/TransformComponentContext/Parents` |
| `0x2` | Model | `0x11E8FC4` | `0x117B980` | `0x94AE30` | 0x90 / 16 | `Objects/ModelComponent/CompositorArray`, `KeyRemapHashMap`, `Assign` |
| `0x4` | Animation | `0x11DC484` | `0x117B7B0` | `0x93F900` | 0x40 / 8 | `Animation/ComponentRuntime` |
| `0x8` | Script | `0x11E90F8` | `0x117BA48` | `0x94F680` | 0x28 / 8 | `Objects/ScriptComponent/CreateParams`, `SaveBindings`, `RawData` |
| `0x10` | Physics | `0x11E8FC8` | `0x117B9F0` | `0x94DD40` | 0x20 / 16 | `PhysicsComponent_Load 0x94E120` |
| `0x20` | Effect | `0x11E8E90` | `0x117B878` | `0x945370` | 0x1C / 4 | `Objects/EffectComponent/EffectName` |
| `0x40` | Tree | `0x11E9290` | `0x117BBD0` | `0x95CF30` | 0x28 / 8 | `Objects/TreeComponent/*` (SpeedTree link, docs/34) |
| `0x80` | Sim | `0x11E90FC` | `0x117BA84` | `0x9511D0` | 0x40 / 8 | |
| `0x100` | Steering | `0x11E9138` | `0x117BB2C` | `0x957470` | 0x110 / 8 | `Objects/SteeringComponent/AvoidanceField`; agent kinds GameSim/Bicycle/Paddleboat in `0x954E60` |
| `0x200` | Sacs | `0x11E90F0` | `0x117BA14` | `0x94EAA0` | 0x18 / 4 | `Objects/SacsComponent/mDrivers`, `StateMachineDriver` |
| `0x400` | Slot | `0x11E9114` | `0x117BAB4` | `0x952F40` | 0x68 / 8 | `Objects/SlotComponent/*Slots`, `mChildren` |
| `0x800` | Lighting | `0x11E8FC0` | `0x117B934` | `0x949670` | 0x28 / 8 | |
| `0x1000` | VisualState | `0x11E92A8` | `0x117BC28` | `0x95FBF0` | 0x1C / 4 | |
| `0x2000` | Footprint | `0x11E8FA8` | `0x117B90C` | `0x947690` | 0x78 / 8 | `Objects/FootprintComponent/*`, `FootprintComponentLock` |
| `0x4000` | Audio | `0x11E8E70` | `0x117B828` | `0x943CE0` | 0x50 / 16 | `Audio/AudioComponent/SoundName`, `Instances` |

Bit values follow from the order [inferred: registration order read in the decompile, bit doubling verified; a probe reading the 15 globals would confirm]. `ResolveComponentMask` also adds Slot, Animation, Lighting and Footprint when the object resource holds certain resource types (`0x9E2A3B0E`, `0x8415534A`, `0x9CC7E9B2`, `0xFF0F8907`, `0x389CED06`) [verified constants; type names not looked up]. `Query_GetObjectsOnLot_impl` filters with a mask global `0x11ECF08` that is not one of these 15 [verified]; its owner is open.

## 4. Object service and the id hash

`Objects/Service`, id `0x3DEFAE8`, 0x11EC bytes, `ObjectService_ctor 0x93A510`, singleton `g_ObjectService 0x11DC350` (set by slot `+0x10` `ObjectService_Init 0x9388D0`, cleared by slot `+0x18` `0x938900`); `+0x1C`/`+0x20` update slots are the shared stub `0x861C50`, so the service has no per-frame work [verified].

| Offset | Field | Status |
|---|---|---|
| `+0x10` | id hash buckets, 0x409 (1,033) pointers | [verified] |
| `+0x1034` | end sentinel (`-1`); every wrapper compares the lookup result with it | [verified] |
| `+0x1038` | object count | [verified] |
| `+0x1040` | allocator | [verified] |
| `+0x1044` / `+0x10C4` / `+0x1144` | component descriptor / name hash / bit, 32 entries each | [verified] |
| `+0x11C4` / `+0x11C8` | next bit / component count | [verified] |
| `+0x11D4`..`+0x11DC` | `ObjectsToUpdate` vector (begin, end, capacity), swap-remove by the index in object `+0x28` | [verified] |
| `+0x11E4` | `forceRefresh` command-line flag | [verified] |

**Lookup path** for a script `ObjectGuid` (e.g. `IC_Objects_GetDisplayPosition 0x744D30`): `ObjectService_Get 0x938B50` (returns the global) -> `ObjectService_FindById 0x939560` -> `ObjectId_Lookup 0x939100` (`this` = service `+0x10`: bucket `id_lo % 0x409`, walk `+0x10`, compare both id halves) -> `Object_GetComponent(g_CompBit_Transform)` -> component call [verified]. No lock is taken anywhere in this path, in `ObjectIdHash_Insert 0x939170` or in `ObjectIdHash_EraseNode 0x938D00` [verified]. A stale id simply misses; there is no generation or reuse check, ids are random 64-bit values (`ObjectService_NewUniqueId 0x939680` retries until unused) [verified].

**Wrappers:** `FindById` (no AddRef, 438 sites), `FindByIdAddRef 0x9395A0` (8), `DestroyById 0x939420` (18), `ReserveId 0x9399F0`, `ExportObjects 0x939870` (lot save), `DestroyAll 0x939510` (shutdown) [verified xrefs].

**Object events:** `GlobalHandlerChain 0x936C90` with ids 3 (clone created), 4 (added to the hash), 5 (before destroy), 6 (destroyed); handler lists at `0x11DC2A8`-`0x11DC350` [verified ids and call places; event names inferred].

## 5. Threads

| Who | Touches | Thread | Status |
|---|---|---|---|
| ~222 ICs (`Objects_*`, `Slots_*`, `World_*`, `Route_*`, `CASUtils_*`) | FindById, Get/FindAddRef | sim (Mono runs only there, docs/43) | [verified xrefs] |
| `ObjectService_CreateObject 0x93A1E0` <- `IC_World_ObjectDesigner_SetObjectImpl`, `TrackManager_OnEventSwitch`, `IC_World_LotSetEntranceByTargetPointImpl`, `0x84C3B0` (locomotion) | insert | sim | [verified call graph] |
| `ObjectService_CreateFromStream 0x939B60` <- lot object load `0xC70320` (lot vtable `+0x24`) | insert | sim (docs/38 step 3) | [verified caller; thread from docs/38] |
| `ObjectService_CloneObject 0x93A2C0` <- `IC_Objects_Clone`, `IC_ThumbnailManager_CloneObject`, `Lot_Save` | insert | sim | [verified call graph] |
| `ObjectService_CreateFromResource 0x938ED0` <- `IC_DownloadContent_ImportInventoryImpl` | insert | sim | [verified call graph] |
| `DestroyById` <- `Animation_DrainDeferred`, `Lot_Unload` <- `WorldManager_LoadLots`, `WorldManager_Shutdown`, `IC_World_ObjectDetachAutoDestroyEffects`, ODS `0xB39770` <- IC | remove | sim | [verified call graph] |
| `DestroyById` <- `0xECBE70` (app shutdown block) | remove | not traced | open |
| `Portal_LockState 0x64F2C0` and its cache `0x64F1E0` | FindById | sim in all samples; reached from route search, which ts3native H6 can run on a worker (docs/18 proxies only the C# callback) | [measured sim; worker use inferred] |
| Script handles (`0x76A520` etc.) | handle table | any; guarded by `g_ScriptHandleLock` | [verified lock] |

Rule for offload work (docs/28, H6): code that leaves the sim thread must not call `ObjectService_FindById`, `Object_GetComponent` on live objects or anything that walks the id hash, unless the sim thread is parked; resolve ids before the hand-off. A runtime probe counting `0x939100` calls per thread id would settle the route-worker question.

## 6. Sim-thread cost [measured]

Method: like `tools/harness/sample-split.ts` (sample-split method, docs/42 §7) but by leaf frame only: a sample counts when its leaf-most TS3W frame lies in the range. Inclusive numbers from stack scanning were checked against return addresses.

| Block | Self (all) | BP / SV / IP | Main callers (next named frame) |
|---|---|---|---|
| Object id lookup (`0x939100` + wrappers + `ObjectService_Get`) | **1.73%** | 2.06 / 1.33 / 1.44% | interpreter ICs (stacks mostly lose the wrapper frame), `Portal_LockState` 0.22%, audio service `0x869000`, `PathPlan_WithRetries`, `IC_Slots_GetParentIdImpl` |
| Script host ObjectGuid maps `0xD726F0` (task/proxy maps, docs/04) | 0.59% | 0.70 / 0.70 / 0.49% | `0xEA30F0`, `0xEA3820`, `MonoScriptHost_Simulate`, `IC_Simulator_GetTaskImpl` |
| `Query_GetObjectsOnLot_impl 0x7499B0` (walks the lot's object vector) | 0.81% | 0.94 / 0.93 / 0.54% | `IC_Query_GetObjectsOnLot`, `IC_Query_CountObjectsOnLot` |
| `Object_GetComponent 0x935770` | 0.38% | 0.47 / 0.35 / 0.31% | `Query_GetObjectsOnLot_impl`, `0x959720`, `0x847390` |
| Script `uint` handles (`0x76A4C0`, `0x76A520`, inner hash `0xC45F60`) | 0.34% | 0.24 / 0.23 / 0.45% | interpreter, `0x671930`, `IC_StopWatch_IsRunningStopwatch` |
| Object create/destroy (`0x93A1E0`, `0x939420`, `0x9361D0`) | under 0.05% | | world load only |

Correction to `sample-split.ts` inclusive: it shows `0x76A520` at 5.9% and `0x939560` at 2.7% inclusive. For `0x76A520`, 527 of 531 hits are the same return address `0x76A5A3` and 64% of those samples have the interpreter as leaf: the address is left over in the interpreter's stack area after the IC returned. Only leaf-based numbers are trustworthy for small, very frequent functions. `Lock_Acquire` (0.92% self) has no attributable TS3W caller in most samples, so the handle lock's share of it is unknown.

Sum of the object-model blocks: about 3.9% self, ~39 ms/s of sim time, almost all of it lookups by id or handle.

## 7. Candidates

| Id | Change | Expected gain | Risk | Before coding |
|---|---|---|---|---|
| O1 | Bigger id hash: `% 0x409` appears only in `ObjectId_Lookup 0x939100` and `ObjectIdHash_Insert 0x939170` [verified: scalar search; corrects docs/20 "every inline user"]. The bucket array is inline in the service (`+0x10`), 9 functions in `0x939420`-`0x939A60` compare with the sentinel at `+0x1034`, and `DestroyAll 0x939510` walks the buckets directly. A detour of Lookup, Insert, EraseNode and the walker onto an own 16K-bucket table that returns the same sentinel keeps the wrappers unchanged | up to ~1.5% sim self if chains are long [inferred] | medium: all users must move together; save/load and shutdown walk the table | counter: objects (`+0x1038`) and average chain length per lookup, in BP/SV/IP |
| O2 | One-entry last-hit cache in `FindById` (id -> object, cleared by EraseNode) | the share of repeated ids (`get_LotCurrent` -> `Slots_GetParentIdImpl` asks for the same id often, docs/20) | low, if EraseNode clears it | hit-rate counter |
| O3 | `Query_GetObjectsOnLot` cost: per call it walks every object on the lot and tests a component field | 0.8% self + managed list building (docs/20 1.7% managed) | low (read only) | which C# callers, how often per tick |
| O4 | Script handle lock `0x10076DC` on every UI/handle IC | small (0.34% plus an unknown part of `Lock_Acquire`) | low | none now |

## 8. Relation to lots and scene nodes

- A lot (docs/38) is not a `Sims3::Objects::Object`: it is a scene node in the world tree (`WorldManager_FindLotById 0xC60D30` -> `SceneNode_FindRecursive 0xC5FA60`, linear over `WM+0x9C`) with its own 64-bit id at `+0x48`, so lot lookups do not use the object id hash [verified docs/38 + this map]. A lot holds pointers to its objects (`+0x14`/`+0x18`), which `Query_GetObjectsOnLot_impl` walks [verified].
- Objects reach the renderer through their Model component; the scene add per object is `Lot_UpdateObjectSceneNode 0xABFAC0` (docs/38) [inferred link; CORE2b].
- Script objects (C# `GameObject`) hold the 64-bit id; the native side resolves it per IC call. `ObjectGuid.ObjectFromId<T>` is a separate managed proxy lookup (`Simulator_GetTaskImpl`, docs/04a) that lands in the host maps (`0xD726F0`), not in the object hash.

## 9. Anchors for M-CORE2b (lifecycle)

| What | Address |
|---|---|
| Allocate object + construct components | `Object_Allocate 0x9361D0` |
| Create (new id, mask, allocate, init, add) | `ObjectService_CreateObject 0x93A1E0` |
| Create from object resource / catalog key | `ObjectService_CreateFromResource 0x938ED0` (`Objects/ObjectResource` `0x93BD50`, spec storage `0x93B020`) |
| Create from saved stream (lot load) | `ObjectService_CreateFromStream 0x939B60` <- `0xC70320` |
| Clone | `ObjectService_CloneObject 0x93A2C0` |
| Add to hash + event 4 | `ObjectService_AddObject 0x939830` |
| Destroy by id (events 5/6, flag `0x400000`) | `ObjectService_DestroyById 0x939420`; all `ObjectService_DestroyAll 0x939510` |
| Destruct / free | `Object_Destruct 0x935860`, `Object_DeletingDtor 0x935DF0` |
| Script create entry | `IC_Simulator_CreateObjectImpl 0x76D0E0` (not traced here) |
| Script handle create / release | `ScriptHandles_GetOrCreate 0x76AA60`, `ScriptHandles_ReleaseHandle 0x76A5B0`, `ScriptHandles_RemovePtr 0x76A660` |
| Component ctors | table §3 |
| Scene add | `Lot_UpdateObjectSceneNode 0xABFAC0` (docs/38) |

## 10. Script handle table (`uint` handles)

In the script host base class (`ScriptHostBase_ctor 0x76AC40`, vtable `0x10078F0`; `MonoScriptHost_ctor 0xD7E010` builds on it; the host is `[0x11EE514]`, docs/18) [verified].

- Outer hash keyed by the handle's top byte (type tag), node 0x48 bytes from `ScriptHandles_NewTagNode 0x76A850`: `+0x04` `Scripting/HandleInfoMap` (handle -> {pointer, refcount}), `+0x24` `Scripting/PointerToHandleMap` (pointer -> handle), next `+0x44` [verified].
- `ScriptHandles_GetOrCreate 0x76AA60` (host `+0xC8`): returns the existing handle for a pointer (refcount +1) or makes `tag << 24 | (random & 0xFFFFFF)`, retrying on collision [verified]. `ScriptHandles_HandleToPtr 0x76A520` (host `+0xCC`) [verified]. Release `+0xD0`, remove by pointer `+0xD4` [verified].
- One `g_ScriptHandleLock 0x10076DC` for all of it [verified]. A debug command "Dump script handles" prints the table [verified string].
- 388 vtable calls at displacement `+0xCC` in the exe, 327 of them in ICs [verified count; not all are guaranteed to be the host, inferred most are].

## 11. Names

76 new rows in `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/45>`): object, service, hash, component register and descriptor ctors, script handle functions, service globals, the 15 mask globals and 4 vtables. Existing names kept: `ObjectId_Lookup 0x939100`, `Object_GetComponent 0x935770`, `ScriptHost_HandleMapLookup 0xD726F0`, `Query_GetObjectsOnLot_impl 0x7499B0`.

## 12. Open items

- Chain length of the id hash in big worlds (needed before O1/O2): read `g_ObjectService+0x1038` and sample bucket lengths in a probe.
- Thread of `Portal_LockState` id lookups under ts3native async routing; thread of `0xECBE70`.
- Owner of mask global `0x11ECF08` used by the lot object query.
- The 15 mask globals' runtime values (bit order is read from code, not measured).
