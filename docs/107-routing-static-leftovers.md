# 107. Routing: static leftovers (portal preference, end-turn list, TypePatch, polygon tests, locomotion reads, camera slot)

Written 2026-10-18. Static only; the game, the probe server and the queues were not run, no native code was written. Goal (PLAN item 6, our own route search with no gaps in what we know before the game's code is replaced): close five items earlier docs left as "not checked" or "inferred": the readers of the preferred portal and of the end-turn ignore list (docs/92 §6), MonoPatcher's `[TypePatch]` and two Mono offsets (docs/104 §4, §5.4), the internals of `0x627780` and `0xC6BAB0` (docs/99 §10.1), what route following reads from a planned path each tick (docs/93 §6), and the camera slot `+0x54` plus the display level of lots nobody looks at (docs/105 §2).

Tools: capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67) for call sites, arguments and a linear register trace of path receivers; Ghidra 12.1.4 headless decompiles on `game-copy/ghidra-io-h` (read-only runs); `game-copy/decompiled` (grep); MonoPatcher's public source at commit `2fa43bf` (https://github.com/LazyDuchess/MonoPatcher, files named below), downloaded as text to the session scratchpad and read, not built. Scratch stays in the session scratchpad (`io-s/`).

Tags as in AGENTS.md: **[verified]** = read in the disassembly, decompile, exe data or decompiled C# (address or file given); **[inferred]** = reasoning, with the docs/51 §43 row that settles it; **[community]** = MonoPatcher's source, link given. Field names are ours.

## 0. Summary

| # | Item | Closed | Still open |
|---|---|---|---|
| 1 | Preferred portal `O+0x78` and end-turn list `O+0x48` | Every native and managed reader found: the portal cost in both portal searches (preferred = cost 1, every other portal x4), and two route-follow end-turn checks that pass the list to script event 60 (`IsOkToTurnFaceToDestination`) | Writer of the third id list `O+0x58` (no IC, not found) |
| 2 | MonoPatcher `[TypePatch]`, delegate `prev`, method token | `[TypePatch]` is `ReplaceMethod` per matching method, no type change; `prev` is `+0x1C` and the token is `+0x04` in EA's build; the full native hook list of `MonoPatcher.asi` on 1.67 | The live check of I-104-1/I-104-3 stays useful but is no longer needed for the offsets |
| 3 | `0x627780` and `0xC6BAB0` | Both read in full: a segment-versus-footprint test over a 256 m bucket grid in P, and a world-bounds plus terrain-paint point test | Meaning of terrain blend layer 0; the writer that sizes P's bucket grid |
| 4 | What route following reads from a path each tick | The current-path link (`agent+8`), the path methods called by the follow behaviour and the obstacle list, with fields; the follow writes a progress cursor into the path | Per-field use inside steering (`0x847390`, `0x85D980`) beyond the options lists stays with docs/86 |
| 5 | Camera slot `+0x54`; display level of lots nobody views | Slot `+0x54` exists only on the main (and edit-in-game) camera controller and clears a cached point at `+0x7B0`; a lot is built with display level 0 and keeps the last level set | none static |

Corrections to earlier docs (not edited here; ledger rows record them): docs/99 §10.1 step 11 calls `opts+0x8C` "the polygon set"; it is the routing agent record, and the polygons come from P's bucket grid (I-107-8). docs/92 §6 says the end-turn list is not read by the search: right, but it is read by route following (I-107-3). docs/105 §2 infers "unviewed lots sit at their top display level": static code builds lots at level 0 (I-107-14). docs/104 §5.4 assumed `[TypePatch]` is `ReplaceMethod` per member: confirmed, with two quirks (I-107-5).

## 1. Preferred portal and end-turn ignore list

### 1.1 The options object's lists and accessors [verified, `0x81C800..0x81D460`]

The route options object O (vtable `0x10213E4`, embedded at `route+0x158`, copied into the job at `job+0x40` by `RoutingJob_ctor 0x819D50` through `0x81D440 -> 0x81D090`, and into every path at `path+0x08` by path slot `+0x10` = `0x81FBE0`, called in the PlanCommon tail at `0x760F7A` with `route+0x158`):

| Field | Content | Accessor | Callers |
|---|---|---|---|
| `O+0x38/+0x3C` | ignore-for-route ids (vector of u64) | `0x81C8D0` (address), `0x81C970` (contains) | as docs/92 §6 |
| `O+0x48/+0x4C` | **ignore-for-route-end-turn ids** (vector of u64) | `0x81C8E0` (address) | the four `Route_*ObjectToIgnoreForRouteEndTurn` ICs (`0x762A80` add, `0x759040` remove, `0x750E90` clear, `0x74D2C0` get) and **`0x84C590`, `0x851710`** |
| `O+0x58/+0x5C` | third id list | `0x81C930` (contains) | `RouteFollow_A 0x846B70` (`0x846DB3`, `0x846FDB`), `Locomotion_ObstacleList 0x85FFE0` (`0x8602A6`); no IC writes it |
| `O+0x68/+0x6C` | forbidden portal owners | `0x81C8F0`, `0x81C9B0` | as docs/92 §6 |
| `O+0x78/+0x7C` | **preferred portal object id** (u64) | set `0x81C900`, get **`0x81C920`** | set: only `IC_Route_SetPreferredPortal 0x74D3A0`; get: **`LotPortalAStar 0x63E340` (`0x63E58D`) and `LotPortalSearch 0x63EC40` (`0x63EED9`)** |

No other code reads `O+0x78` or `route+0x1D0`: a capstone scan for non-stack `+0x1D0`/`+0x1D4` operands over the planner (`0x620000..0x680000`), route IC (`0x740000..0x770000`) and job/follow (`0x810000..0x870000`) ranges finds none, Execute, the splitter and `Route_PlanLocked` never read the job copy (`job+0x40+0x78`), and the assignment `0x81D090` copies `+0x78/+0x7C` with the four vectors (`0x81D14A..0x81D153`). Nothing clears the preferred portal between plans: it lives as long as the route, and every later plan of the same route uses it [verified writers: the setter, the two constructors `0x81CB60`/`0x81CC40` (zero) and the assignment].

### 1.2 What the search does with the preferred portal [verified, `PortalEdgeCost 0x634500`]

Both portal searches fetch the options through the agent record (`RoutingAgent_GetOptions 0x8294E0`, i.e. the job's copy), read the id once before the loop into a stack u64 (0 when there is no agent), and pass it as arguments 5/6 of every `PortalEdgeCost` call (`0x63E6E6..0x63E708`, `0x63F045..0x63F063`; no other write to those stack slots in either function).

`PortalEdgeCost` uses it only on the **traverse edge**, the edge whose two ends are the same portal record (`from == to`, `0x63459D`): after the lock tests (locked in either direction = `1e6`, a wall) and the endpoint checks, the cost is

- preferred id is 0: `portal+0x2C` (the portal's cost float) + penalty;
- preferred id equals the portal's owner id (`portal+0x08/+0x0C`): **exactly 1.0** (`fld1` at `0x6346C9`), no penalty;
- preferred id set but another portal: `portal+0x2C * 4.0` (`0xFB2F6C`) + penalty.

Penalty = 500 (`0xFEB5B4`) when `Portal_IsDiscouraged 0x64F570` is true in either direction, else 0. Edges between two portals (`0x6346F7..0x6347E0`) and the portal-to-room edges never read the id. So a preferred portal does not force the route through it: it makes that portal nearly free, every other portal four times dearer, and drops the discouraged penalty for the preferred one only. A locked preferred portal stays a wall.

Managed callers that set it [verified, `game-copy/decompiled`]: `CommonDoor` (3 routes, `CommonDoor.cs:1386-1422`), `Stairs.cs:537`, `ElevatorDoors.cs:1685/1692`, `GetInPool.cs:189`, `PoolPortalComponent.cs:84`, `SwimHere.cs:493`. There is no managed getter (no `Route_GetPreferredPortal` IC).

For our search: the preferred id is a plain input of the job (one u64), and the rule above is pure data. It must be applied only on traverse edges and with the same constants (1, x4, no discourage for the preferred portal).

### 1.3 The end-turn list: who reads it [verified]

The search never reads `O+0x48` (docs/92 §6 stands). Two route-follow functions do, both on the **path's** options copy (`path+0x08`, reached through path slot `+0x0C` = `0x949D30`, `lea eax, [ecx+8]`):

- `0x84C590` (route follower, at the start of a turn in place; callers `0x852700`, `0x853500`, `0x8538B0`): runs only when the turn angle is above a threshold, the follower is not flagged `+0x111`, the path's last node has flags exactly 8 (`path slot +0x4C` count, `+0x58` node `+0x2C`, `0x84CB7C..0x84CB9D`), the agent's AGS word `+0x84` is `0x200`, `0x600`, or `0x400` with species bit `1 << [+0x7C] != 4`, and the path's options do not have `DisableRouteLeadIns` (`0x400000`, `0x84CBD1..0x84CBEC`).
- `0x851710` (same callers; at the route end facing step, gated by its first argument): the same AGS and option gate (`0x851B14..0x851B5D`).

Both then call `0x7967D0` with: the Sim's object id (`[follower+0xC4]` slot `+0x10`), the destination object id (path slot `+0x150` = `path+0x168`, set by slot `+0x14C` in the PlanCommon tail), the end position and orientation (stack), the path's ignore-for-route list (`0x81C8D0`), the end-turn list (`0x81C8E0`) and a "full Sim jig" flag (0 from `0x84C590` at `0x84CBF9`, 1 from `0x851710` at `0x851B6A`). `0x7967D0` packs these into script variants (u64 arrays for the two lists, `0x7967DC..0x7968FC`) and calls **`World_FireEvent(0x3C)`** at `0x796DE6`: event 60, `WorldEvent.IsOkToTurnFaceToDestination` (`SimIFace/Sims3.SimIFace/WorldEvent.cs`, enum index 60). A false answer sets `follower+0x185 = 1` and returns 0 (`0x84CC4A`, `0x851BC2`) [verified write; meaning "turn refused, skip it" inferred].

Managed side [verified, `World.cs:2056`, `LotManager.cs:2106-2220`]: `World.IsOkToTurnFaceToDestination` builds the event args (the end-turn list is `EndTurnObjectIdsToIgnore`) and calls `World.IsOkToTurnFaceToDestinationCallback`; the only subscriber is `LotManager.IsOkToTurnFaceToDestination` (added at `LotManager.cs:857`). It returns true for non-actors, swimming Sims and Sims without a description; otherwise it builds an ignore list (the Sim, the route ignore list, the end-turn list, the target, the rider when ridden, and the current route's `GetObjectsToIgnoreForRoute()`), places a temporary jig at the end position and orientation (full Sim footprint when the flag is set, else the horse, foal/deer or full-size dog jig; other Sims get no jig and true), and returns `World.TestObjectPlacement`. On a failure with errors it sets `SimRoutingComponent.HasRouteFailureFromTurnJig` and `RouteFailureTurnJigBlocker`. Nothing else reads `EndTurnObjectIdsToIgnore`, and no game code calls `Route.GetObjectsToIgnoreForRouteEndTurn()`.

Managed writers: `BoxStall.cs:3311/3395` and `SocialJigTwoPersonNurseFoal.cs:25/33` (adds only).

So the end-turn list is a follow-time input of a script callback for horses, foals, deer and large dogs, not a search input. A route search replacement does not need it; it must still be copied into the path options as the PlanCommon tail does today (path slot `+0x10`), because the follower reads it from there.

### 1.4 The third list `O+0x58` [verified readers, writer open]

`RouteFollow_A` (the steering obstacle gather, `"Locomotion/FootprintSet"`, called from `0x847390` and the two obstruction ICs) and `Locomotion_ObstacleList 0x85FFE0` skip a footprint when its id is in `O+0x38` (`0x81C970`) **or** in `O+0x58` (`0x81C930`). No IC writes `O+0x58`; the copy and assignment functions copy it. A capstone search for pushes into `+0x58`/`+0x60`/`+0x1B0` vectors found no writer. Until one is found, treat it as a follow-only ignore list that is empty in vanilla [inferred; I-107-4].

## 2. MonoPatcher `[TypePatch]` and two Mono offsets

### 2.1 What `[TypePatch]` does [community: `MonoPatcher/TypePatchAttribute.cs`, `MonoPatcher.cs`, `Utility.cs`, `ReplaceMethodAttribute.cs` at https://github.com/LazyDuchess/MonoPatcher/tree/2fa43bf18e4bbc43620f2d3275ad1ad27109877c]

- `PatchAll(assembly)` walks every type of the mod assembly; for each `[TypePatch(typeof(T))]` on a type R it calls `Apply(R)`; then it applies `[ReplaceMethod]` and `[ReplaceProperty]` on members. Each assembly is patched once.
- `Apply(R)`: for every method of R (public, non-public, static, instance) that R declares itself and that has no `[ReplaceMethod]`, it looks for the **first** method of T with the same name and the same parameter types (`Utility.FindEquivalentMethod`, over `T.GetMethods(...)` without `DeclaredOnly`) and calls `MonoPatcher.ReplaceMethod(original, replacement)`. Then the same for properties.
- `ReplaceMethod` copies 40 bytes from the replacement's `MethodHandle.Value` (the `MonoMethod*`) over the original's, **skipping bytes 24..27** (`+0x18`, the name). No type, class, vtable or field changes.

So `[TypePatch]` does not change types at run time. It is `ReplaceMethod` for every same-named, same-signature method, as docs/104 assumed. Three consequences for the mod guard (docs/104 §5.4):

1. **The L3 class check catches it** [inferred from the copy]: the original's `+0x08` becomes R's class. The copy also overwrites `+0x04` (token), `+0x0C` (signature), `+0x14` (the interpreter's `RuntimeMethod*` cache) and `+0x1C` (the slot field). The optional token check of docs/104 §5.4 adds nothing.
2. **Inherited targets** [inferred from `GetMethods` semantics]: without `DeclaredOnly`, T's method list includes public and protected methods T inherits. A TypePatch on a subclass can therefore swap the **base class's** `MonoMethod`, changing the method for every subclass. The guard must check the declaring class of each guarded method, not the patched type.
3. **Slot reads after a patch are wrong** [inferred]: `+0x1C` carries the replacement's slot (usually -1 for a non-virtual mod method). docs/104 §5.2 takes a guarded method's slot from `MonoMethod+0x1C >> 12`; done after MonoPatcher ran, that gives the replacement's slot. Resolve slots before mods load, or find the slot by scanning the class's slot table (`MonoClass+0xBC`) for the `MonoMethod*`; virtual dispatch itself still reaches the patched header because the slot table holds the same `MonoMethod*`.

Two quirks [community source; effect inferred]: `FindEquivalentProperty` returns the replacement property instead of the original, so the property loop of `Apply` copies each accessor over itself (no effect); the accessors are still replaced, because the method loop already matched `get_X`/`set_X` by name. And a method is matched by name and parameter types only, not by return type or static-ness.

### 2.2 `MonoPatcher.asi` native sites on 1.67 [verified pattern scan of `MonoPatcher.CPP/src/Addresses.cpp` against our exe; behaviour from `MonoHooks.cpp`, `Core.cpp`]

| Site | Address (1.67) | What the ASI does | When |
|---|---|---|---|
| `InitializeScriptHost` | `0xD7E560` (`MonoScriptHost_Init`) | MinHook detour: after the original, registers the ICs `MonoPatcherLib.Internal.Hooking::ReplaceMethodIL` and `::ForceJIT` plus each plugin DLL's `ICallSetup`, then creates `MonoPatcherLib.DLLEntryPoint` | at load, always |
| `GenerateCode` | `0xE5CA90` | MinHook detour: swaps a method header's IL pointer and size for `ReplaceIL` | at load, always |
| "ret: more values on stack" | `0xE59218` (in `0xE53EB0`) | writes `90 E9` over `0F 86` (always jump, log spam off); vanilla bytes `0F 86 A9 B1 FF FF` | at load, always |
| JIT check 1 / 2 | `0xE5637D` / `0xE648DC` | `ForceJIT(true)`: `90 90` / `EB`; `false`: back to `75 26` / `74 4C` | on demand |
| `CreateMonoClass`, `mono_add_internal_call`, script host singleton | `0xD7DB80`, `0xE82550`, `0xD7C7F2` (pointer at `+2`) | called, not patched | |

docs/104 L2 listed `0xE5CA90`, `0xE5637D` and `0xE648DC`. The two always-on byte changes at `0xD7E560` (detour) and `0xE59218` are cheaper and earlier signatures of the ASI. Without the ASI, MonoPatcher can still run from its XML entry point (`InitializationTypes.XML`, late); then only the managed `ReplaceMethod`/`[TypePatch]` path exists and only the L3 class check sees it.

### 2.3 `MulticastDelegate.prev` = `+0x1C` [verified]

`0xE8F720` is EA's `mono_marshal_get_delegate_invoke` (assert string "Invoke", `method->klass->parent == mono_defaults.multicastdelegate_class`, marshal.c). It emits the delegate-invoke wrapper with the field offsets as compile-time constants through `0xE8E820` (`mono_mb_emit_ldflda`: emits `0xF0` prefix then the offset): `0x1C` twice (`0xE8F977`, `0xE8FD3D`: the `prev != null` test and the recursive call on `prev`), then `8` (`0xE90045`, target) and `0x10` (`0xE90188`, `0xE9031A`, method pointer). The `8` and `0x10` match docs/104 §4's verified `m_target` and `RuntimeMethod*`, so the same struct gives **`prev` at `+0x1C`**. Global `0x1220E94` = `mono_defaults.multicastdelegate_class` (stored at `0xE7622C`).

### 2.4 `MonoMethod` token = `+0x04` [verified]

`0xE85040` (loader.c) loads `[m+4]`, masks `0xFF000000`, compares with `0x06000000` and asserts "mono_metadata_token_table (m->token) == MONO_TABLE_METHOD" (`0xE850C9..0xE850EC`), then uses `[m+4] & 0xFFFFFF` minus 1 as the row of the image's method table (`image+0xAC`, `0xE850FA..0xE85110`).

## 3. The two geometry tests of the world cost functions

### 3.1 `0x627780`: segment against the planner's footprint grid [verified, decompile and capstone]

Call: `thiscall(P, float* a, float* b, agent*, bool skipFlagged)`; `this` = `W+0x08` = P (`[[opts+0x88]+8]` at `0x67535D..0x675363`; docs/99 §5). `a` and `b` point at `(x, z, 0)` triples; only the first two floats are read. `agent` = `opts+0x8C`, the routing agent record (AGS word `+0`, object id `+0x08/+0x0C`, options via `RoutingAgent_GetOptions 0x8294E0` = `agent+0x28`). Returns 1 when the segment crosses a blocking footprint polygon, else 0.

Callers: cost modes 1-4 (`0x6753CF`, `0x674849`, `0x673B14`, `0x675D90`; flag 0; a hit is a wall), the closest-waypoint segment test `0x658A00` (`0x658C25`, `0x659096`; flag 0; only with search option `0x40`), and `0x66E2A0` (a water-grid edge cost, no direct callers; flag 1; a hit multiplies the cost by 40 instead of walling).

Steps:

1. Grid = P's vector of 32-byte cells at `P+0xDC..+0xE0`, one per 256 m square. Row length = `trunc((WM+0xBC - WM+0xB4 + 256 - 1) / 256)` from the world manager `0x11ECBC4` (`cvttss2si` at `0x62784F`). Cell coordinates = `trunc(x / 256)`, `trunc(z / 256)` of the two points (x87 with the control word OR `0xC00`, i.e. truncation), **without subtracting the world origin** (the same frame question as docs/99 F-12). The loop walks every cell of the segment's bounding box, row by row; out-of-range indices are skipped.
2. Each cell is a hash set (bucket array `cell+4`, bucket count `cell+8`); records chain through `rec+0x48`. Record fields read: key u64 `+0x00/+0x04`, polygon set `+0x08`, bounds `+0x18..+0x24` (min x, min z, max x, max z), owner id u64 `+0x28/+0x2C`, flag byte `+0x30`, class mask u64 `+0x38/+0x3C`.
3. The record's object id is the key when the class mask is all ones, else the key's bitwise complement.
4. With an agent: skip the record when its id is the agent's own id; skip it when its owner id is non-zero and not the agent's id (a footprint private to another agent).
5. Class test: when the mask is all ones, or the agent has a non-zero AGS word, `AGS_ToFootprintClassMask 0x624180(AGS)` must be a subset of the record mask; a record with a partial mask is skipped when there is no agent or the AGS word is 0. `0x624180` maps the AGS word (`& 0xCF00` species/kind, `& 0x7F` age, two 2-bit size fields at bits 12 and 20) to a 64-bit class bit.
6. Water-only classes: when the mask is non-zero and has no bit in `lo & 0x3FFFF87F` or `hi & 0xFFFFF020`, the record blocks only if the terrain height at both points (`World_GetHeightAt 0xC6A250`) is at or below the sea height (`WorldManager_GetSeaHeight 0xC61670`).
7. Skip the record when `skipFlagged` is set and byte `+0x30` is set, or when its id is in the options' ignore list (`RoutingOptions_IgnoreListHas 0x81C970`).
8. Hit = the segment's bounding box overlaps the record bounds (inclusive min, exclusive max on the far side) and `PolySet_SegmentIntersects 0x67B190(rec+8, a, b)` (endpoint in polygon, or an edge crossing through `0x7CDAA0`).

The result is a pure "any hit" bool, so the bucket order does not matter. Siblings on the same grid [verified accesses]: `0x627540` (point in footprint, from goal validation `0x759B20`; same id, owner, class and ignore rules, point-in-polygon `PolySet_ContainsPoint`), `0x62D0D0` (from `RouteObstructionAtPoint 0x655880`), and `0x62D9F0` (re-files a footprint by its complemented key; called from the lot-graph footprint update chain `0x635700`). The grid is an input docs/91 §2 does not list; a replacement needs it (or the equivalent footprint set) for every edge of bytes 2 and 7.

This also answers docs/99 F-10 statically: the "polygon set" is not in the job; it is P's footprint grid, filtered per agent.

### 3.2 `0xC6BAB0`: point routable on world terrain [verified, decompile and capstone]

Call: `thiscall(WM, float x, float z)` -> bool. `this` = the world manager.

1. False unless `WM+0xB4 <= x < WM+0xBC` and `WM+0xB8 <= z < WM+0xC0 + WM+0xC4`.
2. If `WM+0x54` is non-zero and its `+0x0C` (`0x89F6C0`) is non-zero, that object is the terrain blend map ("Terrain/BlendMap/LayerMaps"): `0xC22050(floor(x), floor(z), layer 0, &w)` reads layer 0's weight at the 1 m cell (16 x 16 cell blocks, a block is either a uniform byte or a pointer to 256 bytes; `0xC21AE0`; weight = byte / 255).
3. When that read succeeds and `w >= 0.5` (`0xF9A5AC`), the point is routable only if `World_LotLookup 0xC68990(x, z)` finds a lot. Otherwise the point is routable.

So off-lot points painted with blend layer 0 at 50 % or more are not routable. Which layer that is in each world (a "no-route" paint, or simply the first terrain paint) is not in the code [inferred; I-107-11]. Callers besides the cost modes: `Routing_PhysicsRayVertical 0x6583E0`, `0x66FD00`, `IC_World_IsSimRoutableImpl 0x785280`, the world routing data builders `0x809930`..`0x814C90`, `0xC75920`.

## 4. What route following reads from a planned path each tick

### 4.1 How the follower reaches the path [verified]

`Locomotion_Update 0x83CED0` walks the agent list and calls `Locomotion_UpdateAgent 0x957210` per agent. The **current path is `agent+0x08`** (`0xC93970` = `mov eax, [ecx+8]`; the debug draw `0x839F70` reads `param+8` and calls path slots `+0x16C` and `+0x7C` on it). The route-follow behaviour has vtable `0x102727C`; its slot `+0x0C` is `0x8538B0` (pointer at `0x1027288`), which calls `0x852700`, which calls the helpers below. Steering is the avoidance behaviour (`0x1026E04`, slot `+0x0C` = `0x847390`, docs/86) and the obstacle list `0x85FFE0` (from `0x957210` with `(agent, path, ...)`).

### 4.2 Path methods called per tick and the fields behind them [verified call sites by a capstone register trace from `agent+8` and path links; field sets from the slot bodies]

| Path slot | Reads (`RoutingPathImp`) | Callers in the follow closure | Role |
|---|---|---|---|
| `+0x0C` | returns `path+0x08` (options copy) | `0x84B0C0`, `0x84C590`, `0x851710`, `0x852700`, `0x853500`, `0x84D7E0`, `0x84F560`, `0x9564A0`, `RouteFollow_A`, `0x85FFE0` | option bits (`TryOtherParts 0x2000` at `0x84B250`, `DisableRouteLeadIns 0x400000`), ignore list `+0x38`, end-turn list `+0x48`, list `+0x58` |
| `+0x30` | `+0xB0` (PathType, object id, portal start) | `0x84FB20`, `0x852700`, `0x85FFE0` (also on the linked path) | subpath kind |
| `+0x4C`, `+0x58` | node count; node `+0x2C` flags | `0x84C590` | "last node flag == 8" (final path) |
| `+0x7C` | `+0x17C` next subpath | `0x84B0C0`, `0x851710`, `0x852700`, `0x853500`, `0x839F70` | end of route test, path chaining |
| `+0x8C` | `+0x184` second link | `0x85FFE0` | obstacle list looks one path ahead |
| `+0xA0` | `+0x188` vs `+0x180` | `0x852700` | "has a distinct second previous link" |
| `+0x100` | nodes `+0x98..+0x9C`; **writes** node `+0x10/+0x20/+0x24/+0x28/+0x2C` | `0x852700` | recomputes directions and lengths |
| `+0x118` | `0x821120` (point at a distance along the path) | `0x84EA00`, `0x852700` | look-ahead point |
| `+0x11C` | nodes, cursors `+0xE0/+0x108/+0x120`; **writes** cursor block `+0xE0..+0x160` | `0x84FB20`, `0x851710` | project the Sim onto the path |
| `+0x12C` | `+0x8C` path length | `0x851710`, `0x852700`, `0x853500` | progress |
| `+0x134` | last node position | `0x84B0C0`, `0x84C590`, `0x851710`, `0x85FFE0` | end point |
| `+0x13C`, `+0x140` | `+0x150` end orientation (has / get) | `0x84B0C0`, `0x84C590`, `0x851710`, `0x852700`, `0x853500`, `0x8538B0` | end facing |
| `+0x144` | **writes** `+0x150` | `0x84C590` | end facing set during the turn |
| `+0x150` | `+0x168` destination object id | `0x84C590`, `0x851710` | event 60 target |
| `+0x48`, `+0xD0`, `+0x11C` | node count, node by index | `0x849600` (path passed as its argument, with a reference taken, from `0x84FB20` at `0x84FBBC` and `0x852700` at `0x852F3C`) | segment walk |

Cursor block written by slot `+0x11C` and its siblings (`+0x44`, `+0x98`, `+0xF4..+0x110`) [verified writes]: `+0xE0`, `+0x104` (-1 = none), `+0x108`, `+0x120` (node indices), `+0x130` (point), `+0x140` (float), `+0x144` and `+0x160` (bytes). They are reset when an index is out of range (`0x81DA29..0x81DA75`).

What this means for a replacement [inferred]: route following treats a `RoutingPathImp` as mutable follow state (cursors, end facing, recomputed node directions), not as a frozen plan. Our builder must hand over real `RoutingPathImp` objects through the game's own constructor and setters (as docs/93 §4.3 already requires), with: nodes and node flags (last node 8, second to last 4), PathType, object id and portal start, the next/previous links `+0x17C/+0x180` (and `+0x184/+0x188`, written by slots `+0x88/+0x90`, set to 0 by PlanCommon's unlink), the path length `+0x8C`, the end orientation `+0x150`, the destination id `+0x168`, and the options copy at `+0x08` (all four id lists and the preferred portal).

Not traced here: the per-field reads inside `ObstacleAvoidance 0x847390` and `ObstacleAvoidance_B 0x85D980` beyond the options lists (docs/86 covers their footprint and agent reads), and `0x853500`, which has no static reference (no vtable entry or call found) [verified negative].

## 5. Camera slot `+0x54` and the display level of lots nobody views

### 5.1 The camera call in `Lot_SetDisplayLevel 0xAC0570` [verified]

When the new level is below the current one (`0xAC05C4`), the function calls `0x96B390` (returns `[0x11E9400]`, the camera manager, vtable `0x10450DC`, built by `0x96B8A0`), `0xF20C20` (manager `+0x14`, the camera controller glue, vtable `0x10454F0`, "Misc/Camera/CameraControllerGlue"), `0x96B6D0` (glue `+0x8C`, the active controller, set by `0x96BB40`), then the active controller's slot `+0x54`. The same call sits in `Lot_RoofUp 0xAC2190` (`0xAC21E2..0xAC2204`).

The glue creates nine controllers (`0x96E280`); their slot `+0x54`:

| Controller | Glue field | Constructor | Vtable | Slot `+0x54` |
|---|---|---|---|---|
| CameraControllerMain | `+0x94` | `0x973B40` | `0x1045CF8` | **`0x9742A0`** |
| EditInGameCamera | `+0xB0` | `0x973B40` | `0x1045CF8` | `0x9742A0` |
| TargetCamera | `+0x98` | `0x97A590` | `0x1045D70` | `0xC0FD60` (`ret`) |
| DebugCamera / FlyCamera | `+0x9C` / `+0xA0` | `0x9675B0` | `0x1044EB0` | `ret` |
| CASCamera | `+0xA4` | `0x965010` | `0x1044C98` | `ret` |
| CameraControllerVideo | `+0xA8` | `0x97E590` | `0x1045EF0` | `ret` |
| CameraControllerHomeVideo | `+0xB4` | `0x96FD10` | `0x10459F8` | `ret` |
| CameraControllerFlyThrough | `+0xAC` | `0x96B1C0` | `0x1045040` | `ret` |

`0x9742A0` stores a zero vector into `controller+0x7B0`. The main camera's update (`0x976FC0`, a vtable slot) compares that vector's y with a height and, when y is below it, copies `controller+0x780` into `+0x7B0` (`0x979CFD..0x979D3F`); `0x9757A0` also writes it. So slot `+0x54` resets a cached camera point that the next camera update refills from the current camera point [verified writes and the refill; "floor-change anchor" is our name, inferred]. It touches only camera state: no routing input. For off-thread work it is one more reason the line-of-sight restore stays on the sim thread (docs/105 §2): it writes the main camera controller.

### 5.2 Display level of lots nobody views [verified writes]

`lot+0x39C` is written only by: the lot constructor `0xAC19F0` (`= 0`, `0xAC1CDE`, `ebx` cleared at `0xAC1A17`), the lot setup/reset `0xACEB20` (`= 0` at `0xACEB5E`, then `0xAC5C90` sets the level range `+0x174..+0x184` and rebuilds the level shapes from the current display level), `Lot_SetDisplayLevel 0xAC0570` and `Lot_RoofUp 0xAC2190`. Callers of the two setters: the ICs (`LotSetDisplayLevel`, `LotChangeDisplayLevel` = current ± 1, `LotRoofUp`), the build/live floor UI (`0xCD18A0`, `0xCDB260`, `0xD0BFD0`, `0xD0C110`), lot level-range changes (`0xACEEE0`, `0xACF520`, which keep the current level), the line-of-sight pair, and `0x815C70` (routing debug display, sets a lot to its top level `+0x180`). Managed callers set it for the lot being played or moved into (`PlayFlowModel.cs:852/889/988`: 1 with a foundation, else 0; `EditTownModel.cs`: roof level + 1; `Sim.cs:53704`: active community lot when the Sim is on a roof or attic level; doors, windows, time portal and similar interactions on their own lot).

No code resets a lot's level when the camera leaves it. So a lot nobody has viewed sits at **display level 0** (ground floor; `0xAC0570` clamps any later set to `[+0x17C, +0x180]` or `+0x180 + 1`), and a lot viewed earlier keeps the last level set [verified writers; "no reset" is a verified negative over the writers above]. With level 0, upper-floor level shapes lack bit `0x10000` and are invisible to the line-of-sight ray, which is why `World_CanSeeObject` raises the level to the target's level (docs/105 §2). docs/105's inference "unviewed lots at the top level" is wrong; the raise path is the normal case for any target above the ground floor of a lot not on screen.

## 6. Names added

Appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (append only): `RoutingOptions_GetPreferredPortal 0x81C920`, `RoutingOptions_EndTurnList 0x81C8E0`, `RoutingOptions_List58Has 0x81C930`, `RoutingOptions_Assign 0x81D090`, `World_FireIsOkToTurnFaceToDestination 0x7967D0`, `RouteFollow_TurnInPlaceJigCheck 0x84C590`, `RouteFollow_EndFacingJigCheck 0x851710`, `RouteFollow_Update 0x8538B0`, `vt_RouteFollowBehaviour 0x102727C`, `RoutingPathImp_SetOptions 0x81FBE0`, `AGS_ToFootprintClassMask 0x624180`, `PathPlanner_FootprintPointHit 0x627540`, `World_IsPointRoutableTerrain 0xC6BAB0`, `TerrainBlendMap_GetLayerWeight 0xC22050`, `WaterGrid_EdgeCostFootprint 0x66E2A0`, `CameraManager_Get 0x96B390`, `CameraGlue_GetActiveController 0x96B6D0`, `CameraControllerMain_ResetAnchor 0x9742A0`, `vt_CameraControllerMain 0x1045CF8`, `Lot_RoofUp 0xAC2190`, `mono_marshal_get_delegate_invoke 0xE8F720`, `mono_mb_emit_ldflda 0xE8E820`, `g_mono_multicastdelegate_class 0x1220E94`.

## 7. What is still open

- `O+0x58` writer (I-107-4).
- The blend map layer 0 per world and how often it is painted off lots (I-107-11).
- The writer that sizes P's footprint grid `P+0xDC` and fills records (insert through `0x62D9F0`'s callees is the likely path) (I-107-10).
- Per-field reads inside `0x847390`/`0x85D980` (docs/86) and the reference to `0x853500`.

## 8. Follow-up 2026-10-18: the open items of §7

Static only, as above (capstone 5 on `game-copy/bin/TS3W.exe`, Ghidra headless decompiles on `game-copy/ghidra-io-h`, read-only; the Sunset Valley `.world` in `game-copy/worlds` read by `tools/rtdata-decode.ts`). Ledger rows: docs/51 §49 (I-107-15 to I-107-27).

| # | Item (§7) | Closed | Still needs a live read |
|---|---|---|---|
| 1 | Writer of `O+0x58` | `RouteFollow_EditPath 0x85F5A0` appends to it at plan time, through the path's options copy (§9) | how often it is non-empty (I-107-15) |
| 2 | P's footprint grid `P+0xDC` | sized, filled, updated and cleared: all writers, callers and events (§10) | the cell count and record count per world (I-107-19) |
| 3 | Blend layer 0 | It is the world-builder layer map 0, its own resource (type `0x05DA8AF6`), decoded offline: a binary 0/255 mask over 84 % of Sunset Valley (§11) | none for the data; per-world coverage needs the other `.world` files |
| 4 | Steering path reads; `0x853500` | `0x847390` and `0x85D980` path slots listed; `0x853500` is called from a vtable slot Ghidra never made a function (§12) | who calls behaviour slot `+0x38` (I-107-24) |
| 5 | I-107-12 sites for the sampler | 107 sites added to `docs/data/routing-indirect-sites.csv` (§13) | the sampler does not arm them yet: `vcallsites.h` needs new roots (§13.2) |

Corrections (earlier text not edited; ledger rows record them): §1.4 and I-107-4 say `O+0x58` has no writer and is empty in vanilla; it is filled by every plan whose path nodes stand in a footprint (I-107-15). §3.1 says `0x62D9F0` "re-files a footprint by its complemented key"; it removes an object's records of one lot from the grid and re-marks the water grid (I-107-18). §4.2 lists `0x853500` as unreferenced; it is called from `0x853820` (I-107-23). The symbol `RouteCost_Dispatch 0x627780` (docs/20, inferred) is the segment test of §3.1, not a dispatcher; the CSV row is kept (append only) and a new name added (§14).

## 9. The third id list `O+0x58`: footprints the planned path stands in [verified writer, meaning inferred]

### 9.1 Writer

- `RouteFollow_EditPathForRoute 0x85FF30(path, ids, x)`: for every node of the path (slot `+0x48` count, slot `+0xD4` node by index) it calls `RouteFollow_EditPath 0x85F5A0` with `ids` as its fourth argument (`[ebp+0x14]`) [verified, `0x85FF71..0x85FFD1`].
- `RouteFollow_EditPath` is the consumer docs/83 §5 describes (footprint gather, types `0x14, 9, 0x11, 0xC, 0xD, 0x17` skipped, the two ignore lists through `IdList_Contains 0x85AAA0`, ids already in `ids`, the `exclude` footprints, then footprint slot `+0xC0` and the level polygon test `+0xE8`). Its only write to `ids` is `IdVec_PushBack 0x572400` at `0x85FC42`; `0x85F897..0x85F8BD` first tests whether the id is already in the list [verified].
- The four callers of `0x85FF30` pass **the path's own options list `+0x58`**: each calls path slot `+0x0C` (returns `path+0x08`, the options copy) and then `0x545170`, a shared one-instruction getter `lea eax, [ecx+0x58]` [verified]:

| Caller | Site | When |
|---|---|---|
| `Route_PlanCommon 0x7606E0` | `0x760F8B` (then `0x85FF30` at `0x760F92`) | every plan, right after the options copy into the path (`0x760F7A`, slot `+0x10`) |
| `IC_Route_SetAGS 0x755600` | `0x755666` | script changes the route's AGS on a planned route |
| `IC_Route_InsertRouteSubPathsAtIndex 0x758BA0` | `0x758D43` | script inserts subpaths |
| `IC_Route_InsertCustomPathAtIndex 0x7623A0` | `0x7627ED` | script inserts a custom path |

Why the §1.4 scan missed it: the field address comes out of the shared getter and the push happens two calls deeper, in a cdecl callee that only sees a vector pointer.

Nothing writes the route's list (`route+0x158+0x58`) or the job copy (`job+0x40+0x58`): the options code only constructs (`0x81CB60`, `0x81CC40`: empty), clears (`0x81D3C0`), destroys (`0x81CBE0`) and assigns (`0x81D090`, callers `RoutingJob_ctor` via `0x81D440`, route copy `0x760370`, `0x81D3C0`) [verified, every function in `0x81C800..0x81D460` that touches `+0x58..+0x60`, and the options vtable `0x10213E4` has one slot, the destructor]. So the list on the route and in the job is always empty, and the path's list starts empty at each plan (assigned from the route at `0x760F7A`) and then holds what `EditPath` collected [verified writes; "empty at each plan" follows from the assignment order].

### 9.2 Meaning and threads

The ids are the footprints that block a node of the planned path (the start or goal stands inside them, or a portal node sits in one) and that `EditPath` did not edit the path around [inferred from docs/83 §5's consumer, which is the same function]. Steering then skips them (`RouteFollow_A 0x846B70` at `0x846DB3`/`0x846FDB`, `Locomotion_ObstacleList 0x85FFE0` at `0x8602A6`, both via `0x81C930`), so a Sim does not try to steer out of the object its own route starts or ends in [inferred]. All four writers run on the sim thread (the PlanCommon tail and three ICs); the gather inside `EditPath` takes the steering lock `+0x918` (docs/81, docs/83) [verified call chain; thread from docs/60].

For a route search replacement: `O+0x58` is not a search input. It is a plan **output** that the PlanCommon tail builds after the search; as long as our search hands its path to the game's PlanCommon tail (docs/93 §4.3), the list is built as today. A builder that bypasses the tail must call `0x85FF30(path, &path.options+0x58, ...)` itself, on the sim thread.

## 10. P's footprint grid `P+0xDC` [verified unless marked]

P is the path planner `[[opts+0x88]+8]` (docs/99 §5; constructor/init `PathPlanner_Init 0x62E860`, which zeroes `+0xDC..+0xE4`; destructor `0x637520`, vtable `0xFEC680`).

### 10.1 Layout

Vector of 32-byte cells, `begin = P+0xDC`, `end = P+0xE0`, `cap = P+0xE4`. Cell index = `row * cols + col`, `cols = trunc((WM+0xBC - WM+0xB4 + 256 - 1) / 256)` and `rows = trunc((WM+0xC0 - WM+0xB8 + 256 - 1) / 256)` (constants `256.0` at `0xFCC24C`, `1.0` at `0x107A538`, `1/256` at `0x108AF84`). Records as §3.1 step 2, plus the **lot id** at `+0x40/+0x44` (0 for footprints with no lot; copied from `lot+0x48/+0x4C`) [verified in the `0x636BF0` decompile, read back by `0x62D9F0`].

### 10.2 Who sizes, fills, updates and clears it

| Action | Function | How | Reached from | Thread |
|---|---|---|---|---|
| Size (full) | `PathPlanner_Reset 0x635B70(full)` | frees every cell's records (`0x62E5A0`), `end = begin`; when `full` and the world manager exists, resizes to `rows * cols` (`0x635620`) | `PathPlanner_Setup 0x637650` (full; from `RoutingWorld_LockedSaveStep 0x829F10`), `0x6376C0` (full; `RoutingWorld_Clear 0x82D140`), `0x6376B0` (not full; `RoutingWorld_DestroyPlanners 0x8299E0`, `0x829C40`), destructor `0x637520` (not full) | sim (world load, clear, destroy) [inferred: the RoutingWorld callers are world lifecycle code] |
| Size (lazy) | `PathPlanner_AddHangingFootprint 0x636BF0` | when the grid is empty, `0x635620(rows * cols)` from the world bounds (`0x624F30`) | first insert after a non-full reset | as the insert |
| Fill | `0x636BF0(fp, lot, flag)` ("Routing/HangingFootprints") | footprint bounds (slot `+0x2C`, widened by 0.25 or 0.75 m for some classes), every cell the bounds overlap: builds a record (bounds, owner id when the footprint's kind slot `+0x98` is `0xB`, Sim flag `+0x30`, class mask from `fp+0x18/+0x1C`, lot id) and files it in the cell and in a key-to-cells index unless that index already lists the cell (`0x6643E0` lookup; inserts `0x62D530`, `0x626910`, which one is the cell is not checked) [inferred split]; also marks the water grid (`0x6733C0`) and recurses into child footprints | `PathPlanner_AddFootprint 0x6376D0` (lot quadtree insert `0x666C50`, then the grid when `0x626F40` says the footprint reaches outside its lot, or when it has no lot), `LotGraph_Build 0x63CB90`, `0x639DE0`, `0x63A1C0` (from `RoutingWorld_AddLotObjectPortals 0x832890`, `0x63B710`) | sim through the writer layer; `LotGraph_Build` also inside jobs (docs/83 W4) |
| Update | remove then re-add: the writers call `LotRouting_RemoveObjectFootprints 0x635700` (which calls `0x62D9F0`) and then `0x6376D0` | object footprint moves, enable/disable, walls, floors, terrain | `RoutingWorld_UpdateObjectFootprintCore 0x8321F0`, `AddPrivateFootprint 0x831BA0`, `UpdateLotWalls 0x830940`, `UpdateTerrainFootprints 0x831710`, `UpdateLotFloorFootprints 0x8336F0`, `Lot_FinishLoad 0xACF170`, `RemoveFootprintLocked 0x82A0C0`, `LockedLotUpdate 0x82B450`, `RemoveDiscourageFootprint 0x834860` | sim (docs/60 §1.1, docs/94 §2.2) |
| Remove | `0x62D9F0(lotId, key)` | for both the key and its complement: every cell listed for it, records of that lot: re-mark the water grid without them (`0x670510`, `0x66F6D0` for plain world records), then erase (`0x62D4E0`, `0x62C1C0`) | `0x635700`, `0x637900` (`Lot_FinishLoad`, `UpdateTerrainFootprints`, `0x639B40`), `0x62E3E0` (no direct caller) | sim, and the job when `LotGraph_Build` runs there |
| Read | `0x627780`, `PathPlanner_FootprintPointHit 0x627540`, `0x62D0D0` (`RouteObstructionAtPoint 0x655880`, `RoutingDebug_DisplayFootprints 0x818CB0`) | §3.1 | cost modes 1-4, waypoint segment test, goal validation | job (cost modes), sim (obstruction ICs) |

The "reaches outside its lot" rule of `0x626F40` (footprint slot `+0x64`, more than one level part, bounds compares) is read only in outline [inferred]. The record insert `0x626910` and the erase pair `0x62D4E0`/`0x62C1C0` were not decompiled; their roles come from the call shape [inferred].

### 10.3 Search input list and snapshot

- **docs/91 input list:** yes. The grid is an input of every world-search edge with cost bytes 2 and 7 (§3.1) and of goal validation; docs/91 §2 does not list it (I-107-8 already said so). It belongs next to the per-lot quadtrees as "world-level hanging footprints".
- **docs/94 snapshot (design H):** every sim-side writer above is under a writer-layer leaf docs/94 §3.2 already marks (`0x8321F0`, `0x831BA0`, `0x82A0C0`, `0x635700`, and the lot rebuild leaves), and each record carries its lot id, so a dirty-lot copy can take the grid records of the dirty lots plus the no-lot records [inferred: no grid writer found outside those leaves, the reset paths and `LotGraph_Build`]. The grid is not inside any lot image: H needs one more world-level table (records by cell, keyed by lot id) rebuilt from the dirty lots at START. The lazy fill from `LotGraph_Build` inside a job writes the worker's own copy under H, as docs/94 §3.1 expects for lazy builds. Size: records exist only for footprints that reach outside their lot or have none, so the table is small compared with the lot quadtrees [inferred; I-107-19 counts it].

## 11. Terrain blend layer 0 of `0xC6BAB0` [verified]

### 11.1 Which map it is

`WM+0x54` is the terrain data object; its `+0x0C` (read by the shared getter `0x89F6C0`) is the terrain blend map (`TerrainBlendMap_ctor 0xC226E0`, 0x88 bytes, "Terrain/TerrainData/BlendMap"). The blend map has three vectors of layer maps: `+0x1C` "LayerMaps" (the paint layers), **`+0x2C` "mWorldBuilderLayerMaps"** and `+0x3C` "mBrushLayerMaps". `TerrainBlendMap_GetLayerWeight 0xC22050` indexes **`+0x2C`**, so `0xC6BAB0` reads world-builder layer map 0, not the first terrain paint.

Loading at world load (both from `0xC2ECA0` and `0xC62AC0`): `TerrainData_LoadBlendMap 0xC29DB0` creates the blend map and reads resource type `0x3D8632D0` (instance 1) into the paint layers (`0xC238E0`); `TerrainData_LoadWorldBuilderLayer0 0xC29CE0` then reads resource type **`0x05DA8AF6`** (instance 1) with `0xC23CB0`, which (re)creates world-builder layer 0 (`0xC22DD0(+0x2C, 0)`) and fills it record by record (`0xC21C70`). Without that resource, layer 0 does not exist and `0xC6BAB0` accepts every point inside the world bounds.

Format (`0xC21A20`: the magic's high half is `0xA5FF`, the `0xFFA5` form is byte-swapped; the low half is the version, accepted `0x100..0x3FF`; `0xC23CB0` leaves the layer empty below `0x200`): u32 magic, u32 width, u32 height (1 m cells), from version 0x300 u32 record width and height (else 32), u32 present flag, u32 data size, u32 record count, u8 layer index, then records of u8 block high, u8 block low, u8 kind; kind 1 = one byte for the whole 16 x 16 block, kind 0 = 256 bytes row by row. Blocks not listed are 0; weight = byte / 255 (`0x1071AE0`).

### 11.2 Offline decode

`node tools/rtdata-decode.ts <file.world> --blend0` decodes it (16 x 16 records only, the only size seen; other sizes stop with a message). Sunset Valley (the only world in `game-copy/worlds`):

| Value | Sunset Valley |
|---|---|
| Header | version 0x300, 2048 x 2048 m, 16 x 16 records, 14,244 records (12,946 uniform, 1,298 raw) of 16,384 blocks |
| Cells > 0 | 3,513,359 (83.77 %) |
| Cells with weight >= 0.5 | 3,513,359 (83.77 %): every non-zero cell is 255, so the layer is a binary mask |
| R waypoints (docs/99) on such cells | 665 of 39,265 |

So layer 0 marks the **unplayable part of the map** (the borders, sea and terrain outside the playable area) as not routable; off-lot points there fail `0xC6BAB0`, on-lot points pass. The few R waypoints on it are where a lot covers the mask or near its edge [inferred; I-107-21]. It is world data written by the world builder (no game code writes `+0x2C` after load besides the brush/undo paths of the edit tools) [inferred from the vector names; edit-tool writers not traced]. Our search can read the same resource offline per world and must apply the same rule: weight >= 0.5 at `floor(x), floor(z)` and no lot at the point.

Other worlds: run the same command on each `.world` file; no live read is needed for the data. A live check is still useful once: `0xC6BAB0` against the decoded mask for a few points (I-107-21).

## 12. Steering path reads and `0x853500`

### 12.1 `ObstacleAvoidance 0x847390` [verified]

Takes the current path (`agent+0x08` via `0xC93970`, `0x8473A1`), holds a reference, and uses it in four ways: path slot `+0x9C` (`0x81FD60`: "has a next or second link", reads `+0x17C`/`+0x184`) at `0x847510`; `0x83A280(agent, path)` at `0x8473D5`; as the path argument of `ObstacleAvoidance_B 0x85D980` (`0x8474FE`) and of `RouteFollow_A 0x846B70` (`0x847697`). No direct field read of the path in its own body.

### 12.2 `ObstacleAvoidance_B 0x85D980` [verified slot calls on its path argument `[ebp+0xC]`, register trace; six more inferred]

| Slot | Reads | Sites |
|---|---|---|
| `+0x0C` | options copy (`0x81C970` ignore list at `0x85DC62`) | `0x85DB78` |
| `+0x30` | `+0xB0` PathType block | `0x85DCA9`, `0x85DCBB`, `0x85DCCD`, `0x85E168`, `0x85E28C`, `0x85E7A4`, `0x85ED64` |
| `+0x7C`, `+0x84`, `+0x8C` | links `+0x17C`, `+0x180`, `+0x184` | `0x85E574`; `0x85DE8A`, `0x85DE9E`; `0x85E108`, `0x85E736` |
| `+0x11C` | project onto the path; **writes the cursor block** (§4.2) | `0x85E003`, `0x85E874`, `0x85EF2D`, `0x85EFCB` |
| `+0x120` | `0x821850`: point query on nodes `+0x98` and `+0x90` | `0x85E0FC`, `0x85E1A7` |
| `+0x12C` | path length `+0x8C` | `0x85E12E`, `0x85E5E9`, `0x85EE1D` |
| `+0x134`, `+0x138` | last and first node positions | `0x85E336`, `0x85E402`, `0x85E4E3`, `0x85E527`, `0x85ED8F`, `0x85EEA5`; `0x85E511` |
| `+0x48`, `+0x118`, `+0x12C`, `+0x134`, `+0x13C`, `+0x140` | node count, point at distance, length, end point, end orientation | `0x85EAB8`, `0x85EC64`, `0x85EC4C`, `0x85EAF8`, `0x85EB0C`, `0x85EB24` (receiver `esi`, a copy of the path at `0x85E716` by a linear trace; control flow not followed) [inferred] |

New slots against I-107-12: `+0x84`, `+0x9C`, `+0x120`, `+0x138` (and `+0x16C` in the debug draw). Like the follow behaviour, steering B writes the path's cursor block (slot `+0x11C`), so the path is follow state shared by follow and steering on the sim thread [verified write slot; "shared" inferred from both callers being per-tick sim code].

### 12.3 `0x853500` [verified]

Not dead code: `0x853820` calls it (`0x853886`), and `0x853820` is **slot `+0x38` of the route-follow behaviour vtable** (pointer at `0x10272B4`). Ghidra never created a function at `0x853820` (not in `game-copy/analysis/functions.csv`), and the 16 bytes before it are `0x853500`'s own jump table (referenced at `0x853755`), which is why no reference showed. `0x853820` gets the current path (`[this+0x14]` agent, `0xC93970`), holds a reference, may call its own slot `+0x18` when `+0x17C` is set and its list `+0x1C` is empty, then `0x84AF20`, then (unless `+0x17C`) `0x852700` and on failure `0x84B0C0(0)`, then `0x853500`. `0x853500` walks the behaviour's list at `+0x1C` (element `+0x24` byte, `+0x1C` object slot `+0x10`), then reads the path (slots `+0x0C`, `+0x7C`, `+0x12C`, `+0x13C`) and calls `0x84B0C0`, `0x84C590`, `0x851710`, `0x8484D0`, `0x850B90`. Its role (a second per-tick entry of the follow behaviour, for the route end) is inferred. Who calls slot `+0x38`: not found in the locomotion closure (open, I-107-24).

## 13. I-107-12 as sampler sites

### 13.1 Rows added [verified sites; receivers verified by register trace unless marked]

107 rows appended to `docs/data/routing-indirect-sites.csv` (none of these sites was in it; the docs/102 closure stopped before the follow code): kind `vcall`, target = the `RoutingPathImp` slot function (vtable `0x10214D8`; the path constructors `0x81FA45`, `0x81FF66`, `0x8204CE` store only this vtable, and the slot targets checked appear in no other vtable), status `resolved`; six `0x85D980` sites whose receiver is inferred are `partly resolved`. Functions: `0x84B0C0`, `0x84C590`, `0x84D7E0`, `0x84EA00`, `0x84F560`, `0x84FB20`, `0x851710`, `0x852700`, `0x853500`, `0x8538B0`, `0x9564A0` (current path or a link), `0x85FFE0`, `RouteFollow_A 0x846B70`, `0x849600`, `0x847390`, `0x85D980` (path argument), and `0x839F70` (debug draw only). Slots: `+0x0C`, `+0x30`, `+0x48`, `+0x4C`, `+0x58`, `+0x7C`, `+0x84`, `+0x8C`, `+0x9C`, `+0xA0`, `+0xD0`, `+0x100`, `+0x118`, `+0x11C`, `+0x120`, `+0x12C`, `+0x134`, `+0x138`, `+0x13C`, `+0x140`, `+0x144`, `+0x150`, `+0x16C`. A live pair at one of these sites with another target is classed `new` by `tools/indirect-live-vs-static.ts` (checked with two synthetic log lines: `match` and `family`).

### 13.2 What the sampler still needs

The `[ProbeSnap]` sampler arms only the sites in `native/ts3native/vcallsites.h`, generated by `tools/snap-vcall-sites.py` from the routing roots; none of the 107 sites is in it. To confirm I-107-12 live, add `RouteFollow_Update 0x8538B0`, `0x853820`, `ObstacleAvoidance 0x847390`, `Locomotion_ObstacleList 0x85FFE0`, `0x84C590` and `0x851710` to its `ROOTS` (depth 2-3 is enough), regenerate the header and rebuild ts3native; then run `node tools/indirect-live-vs-static.ts <log> --class new,resolves` on a short walk-across-a-lot sample. Not done here (native build change). Prologue and hook-site exclusions: none of the 107 sites is within 16 bytes of a function start or of any address named in docs/68, `tools/probe/*.js` or `tools/frida/*.js` (465 addresses) [verified, the generator's own rule].

## 14. Names added (second batch)

Appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv`: `RouteFollow_Update38 0x853820`, `RouteFollow_EndStep 0x853500`, `Get_Plus58 0x545170`, `PathPlanner_Reset 0x635B70`, `PathPlanner_SizeFootprintGrid 0x635620`, `PathPlanner_Setup 0x637650`, `PathPlanner_dtor 0x637520`, `PathPlanner_AddFootprint 0x6376D0`, `PathPlanner_AddHangingFootprint 0x636BF0`, `PathPlanner_RemoveHangingFootprint 0x62D9F0` (second name; the old row stays), `PathPlanner_SegmentHitsFootprint 0x627780` (second name), `PathPlanner_FootprintReachesOutsideLot 0x626F40`, `TerrainBlendMap_ctor 0xC226E0`, `TerrainData_LoadBlendMap 0xC29DB0`, `TerrainData_LoadWorldBuilderLayer0 0xC29CE0`, `TerrainBlendMap_ReadWorldBuilderLayer 0xC23CB0`, `TerrainBlendMap_ReadLayers 0xC238E0`, `TerrainBlendMap_ReadRecord 0xC21C70`, `TerrainLayerMap_GetWeight 0xC21AE0`, `Get_Plus0C 0x89F6C0`.

## 15. What is still open

- How often `O+0x58` is non-empty, and which object kinds land in it (I-107-15, live read of `path+0x60..+0x64`).
- Grid size per world: cells (`(P+0xE0 - P+0xDC) / 32`) and records (I-107-19, live read).
- Blend layer 0 for the other worlds (offline, needs their `.world` files) and one live point check (I-107-21).
- Caller of the follow behaviour's slot `+0x38` (I-107-24).
- Arming the 107 sites in the sampler (§13.2).
