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
