# 102. Routing indirect call sites, resolved statically

Written 2026-10-18. Static only: the game was not run, no probe, no native code, no Ghidra. Part of PLAN "Routing off-thread" item 6 (replace the game's route search with our own search on a snapshot, docs/91-94). Indirect calls (vtable slots, function pointers, switch tables) are where a static read of the routing code can miss behaviour. This doc lists **every indirect call or jump site** in the routing code and the targets each can reach, as far as static analysis gets. A later live probe will record which targets the sites really reach; `docs/data/routing-indirect-sites.csv` is the static side of that comparison.

Tools: capstone 5 over `game-copy/bin/TS3W.exe` (Steam 1.67) with `game-copy/analysis/functions.csv`; small Python scripts in the session scratchpad (`io-v/`: closure, value slicer, receiver typing, report). Tags as in AGENTS.md: **[verified]** = read in the disassembly (address given); **[inferred]** = reasoning, with a ledger row in docs/51 §36.

## 1. Summary

- **1,819 indirect sites** in 2,836 functions: **560 resolved, 559 partly resolved, 700 unresolved** (§3). The route search alone (`Route_PlanLocked 0x82C6E0` closure, 1,322 functions) has **927 sites: 265 resolved, 330 partly, 332 unresolved**.
- **Only 31 search sites are unresolved and do something other than reference counting or deletion** (§4.1). The other 301 unresolved search sites are lifetime calls: `+0x4` Release on a null-checked local smart pointer, or `+0x0` with flag 1 (deleting destructor) [verified pattern at each site; target class not proven]. Of the 31, 7 are in the routing library, 20 in engine code the search calls (scene graph, cell grid raycast, physics quadtree), 4 in the allocator and job manager.
- **Function pointers in the search are all resolved but two** [verified]: the world A\* cost function (7 call sites in `WorldAStar_Search 0x676260`) takes one of **five** targets: the four `RouteCost_Mode1..4` and the switch default `RouteCost_Default 0x6734E0` (docs/91 §3.1 lists four); the hierarchical planner's sort (14 sites in `0x650260..0x653810`) always gets the comparator `0x64FE80` (float at `+4`, descending) from `PathPlanner_PlanHierarchical` at `0x65EA57`; the lot quadtree walkers call back into `0x662D00`, `0x662D30`, `0x662DB0`, `0x662DD0` (passed by `0x6672E0`, `0x667350`, `0x667400`, `0x667480`, `0x6674F0`, `0x667560`), and the second callback of `QuadTreeCellPos_A 0x663B20` is always null.
- **Four documented class families cover 371 sites** (§2.4): footprints (185 sites, docs/82 §3.1), A\* nodes (96, docs/91 §3.6), `RoutingPathImp` (80, docs/93) and the route object (10, docs/92). Their targets are the family's slot functions; which class within the family is not proven per site, so these are "partly resolved" (9 path-list sites in `Route_PlanCommon`/`Route_PlanLocked` are "resolved": the list holds only `RoutingPathImp`).
- **No new managed exit among the static targets** [verified against the docs/92 reverse closure of the managed entries, 8 hops]: of the 722 distinct resolved or partly resolved targets, only two reach managed code, `ScriptHost_InvokeDelegate 0xD75FA0` and `0xD7D2F0`, both from the script-host slots `+0x134`/`+0x138` inside `World_FireEvent 0x78F2B0` (`0x78F2FC`, `0x78F363`), on the known event-59 portal-lock path (`Portal_LockState 0x64F2C0`, docs/92 §4). Unresolved sites can still hide one; the probe of §6 checks them.
- Method finding: `functions.csv` gives the **inclusive** last byte as `end`. Scripts that disassemble `end - entry` bytes drop the last instruction of every function (often a tail `jmp`); the 856/858-site counts in docs/91 §4 and docs/92 §4 came from that reading [verified: `0x82C80F ret 0x28` ends at `0x82C811` = csv end]. With the fix, the search closure has 1,322 functions (docs/92: 1,308).

## 2. Method

### 2.1 Scope (roots)

| Group | Roots | Functions in the direct-call closure |
|---|---|---|
| search | `Route_PlanLocked 0x82C6E0` | 1,322 |
| plan | the four route plan ICs `0x765160`, `0x765490`, `0x765240`, `0x765550` and `Route_PlanCommon 0x7606E0` (docs/92 §6) | 1,392 |
| writer | the routing-data writers of docs/94 §2.2 and docs/91 §2 (footprint core `0x8321F0`, remove leaves, lot rebuilds, quadtree insert/remove/flush, lot table and lot graph builders, terrain/wall/floor footprints, `ApplyLotUpdate`, `ApplyPendingUpdates`, build/buy exit, the footprint, discourage and private-footprint ICs): 48 roots | 2,197 |
| portal | the 21 `IC_RouteManager_*` portal, lock-key, ocean-owner, room-update and build/buy ICs plus `IC_Route_SetLockKeys` | 1,318 |

The closure follows every direct `call`/`jmp` with no depth cut and no stop list; allocator, lock, job and CRT code it reaches is in scope and tagged by area in the CSV. Union: 2,836 functions. A site belongs to every group whose closure holds its function.

### 2.2 Sites

Every `call`/`jmp` whose operand is not an immediate: memory operands through the import table, globals, stack slots or object fields, `call reg`, and `jmp [reg*4 + table]` switches. Indirect `jmp reg` tail calls are included. 1,819 sites.

### 2.3 Resolution steps

1. **Imports** (`call [IAT]`, also through a register): 298 sites, resolved to the DLL export.
2. **Switch tables**: 97 sites, entries read from the table up to the `cmp` bound, all inside the caller.
3. **Value slicing**: a backward slice over the function's control-flow graph (switch edges included) with stack tracking (esp deltas solved forward from entry and backward from every `ret`; `ebp` frames; only functions with consistent deltas are trusted). It gives each call operand a value: constant, global, argument, `this`, return of a call, field load, or local whose address was passed out.
4. **Function pointers**: constants (7 sites), global pointers with their immediate stores (11), argument chains followed into every direct caller (35, recursion-safe; a null argument is recorded as `null`).
5. **Receiver class of a virtual call** (data flow, 103 resolved sites): constructor return, allocation followed by a constructor call or by vtable stores in the same frame (inlined derived constructors), global objects with a vtable store, global pointers fed by those, constructor-set fields, and `this` typed from the direct callers. A virtual `this` (caller is itself a slot of some vtables) or a constructor-set field gives a partial answer (the subclass or setter may differ).
6. **Families** (§2.4): site facts from docs/82, 91, 92, 93 where the receiver is documented, and a family rule: when every concrete class built inside the closure that fits the site's slot signature belongs to one documented family, the family's slot functions are the targets.
7. **Signature fallback**: the slot signature of a receiver (every slot used on it in the frame, with the stack bytes pushed) is matched against all 2,865 vtable bases, minus the 155 abstract ones (any slot = `0xF7E8AC`, the `_purecall` thunk). At most 6 distinct targets over all vtables gives "partly resolved, slot signature" (17 sites); at most 6 over the classes built in the closure gives "partly resolved, closure-built heuristic" (180 sites; it assumes routing code built the receiver, which is false for game objects passed in). Otherwise "unresolved"; the targets column then holds the closure-built candidates when there are at most 12, as a hint only.

### 2.4 Families

| Family | Vtables | Source | Sites |
|---|---|---|---|
| footprint | `0xFEBBC0`, `0xFEBA98`, `0xFEB848`, `0xFEB970`, `0xFEBE38`, `0xFEBD10`, `0xFEB5F8`, `0xFEB720`, `0xFEAE08`, middle `0x1020AE8`, member `0xFEBF90` (built by `0x807400` from `Footprint_MidCtor 0x80EED0`) | docs/82 §3.1, docs/91 §3.7 | 185 |
| astar-node | `0xFE9AD0`, `0xFEA080`, `0xFEABD0` | docs/91 §3.6 | 96 |
| routing-path | `0x10214D8` (`RoutingPathImp`, constructor `0x8204B0`) | docs/93 | 80 |
| route | `0x10047CC` (`vt_Route`) | docs/92 §6 | 10 |

Two routing classes found on the way [verified stores and slots]: `0xFED994`, built only by `0x664650`, which `LotRouting_QuadTreeBuild 0x62F8A0` calls (`+0` AddRef and `+4` Release on a count at `+0x34`, `+8` deleting destructor); and `0xFEC058` (`RoutingSearchPath_ctor 0x64E5C0`, `+0` `0x7D9FA0` AddRef, `+4` `0xEC4FC0` Release), which resolves most search-path references in `PathPlan_WithRetries` and `LotPlan_Inner`. Most other routing classes use the same two conventions: `+0`/`+4` AddRef/Release, or `+0` deleting destructor called with flag 1 when an inline count at `+4` drops to zero (`RefPtr_Release 0x7460F0`).

## 3. Counts

### 3.1 By status

| Group | Sites | Resolved | Partly resolved | Unresolved |
|---|---|---|---|---|
| all | 1,819 | 560 | 559 | 700 |
| search | 927 | 265 | 330 | 332 |
| plan | 1,019 | 275 | 378 | 366 |
| writer | 1,260 | 461 | 360 | 439 |
| portal | 983 | 304 | 310 | 369 |

Groups overlap (a site in a shared helper counts in each group).

### 3.2 By basis

| Basis | Resolved | Partly resolved |
|---|---|---|
| import | 298 | |
| switch table | 97 | |
| receiver class (data flow) | 103 | |
| argument chain / constant / global | 53 | |
| family | 9 | 362 |
| closure-built heuristic | | 180 |
| slot signature | | 17 |

### 3.3 Lifetime calls

| | Resolved | Partly | Unresolved |
|---|---|---|---|
| all, lifetime (`+0`/`+4` vtable slot, or deleting destructor) | 51 | 73 | 527 |
| all, other | 509 | 486 | 173 |
| search, lifetime | 31 | 63 | 301 |
| search, other | 234 | 267 | 31 |

Lifetime sites are classed by slot and pattern, not by target: a `+4` call on a class whose `+4` is not Release would be counted here too. The probe settles that (§6, row 5).

## 4. Unresolved sites, ranked

Rank score (CSV note column): search group 4 plus up to 4 for closeness to `Route_PlanLocked` (12 minus hop, divided by 3), plan 3, writer 2, portal 1; area routing library 3, route ICs 2, route following 1.5, other engine code 1, allocator/job/CRT 0; plus 2 when not a lifetime call; plus 2 when unresolved, 0.5 when partly. The score orders the probe's attention; it is not a measure of cost.

### 4.1 Top 20 (unresolved, not lifetime)

| # | Site | Caller | Slot | Receiver (static) | Why it matters |
|---|---|---|---|---|---|
| 1 | `0x63546E` | `0x635440` (room ids, docs/91 §3.5) | `+0xC` | argument 2 | lot build inside plans; receiver from the Lot side, 401 targets fit |
| 2 | `0x63556F` | `0x635440` | `+0xC` | `esi` at entry | same frame |
| 3 | `0x6374D6` | `0x636BF0` (hanging footprints) | `+0x8` | element of a local list | lot build inside plans; the frame's other sites are footprints |
| 4 | `0x62A665` | `0x62A470` (A\* setup helper) | field `+0x10` | function pointer in an object returned by an indirect call | inside the in-room search setup |
| 5 | `0x655BAA` | `RouteObstructionAtPoint 0x655880` | `+0xC` | `edi` at entry | obstruction query; frame's other sites are footprints |
| 6 | `0x6697B7` | `0x669530` (quadtree collector) | `+0xC` | local filled through its address | quadtree read path |
| 7 | `0x667A24` | `0x667A10` | `+0x8` | `this+8` | quadtree helper under `0x667DB0` |
| 8 | `0xC2A69D` | `WorldCellGrid_Raycast` | `+0x10` | cell shape `cell+0x38` (kd-tree or height field, docs/91 row 9) | height reads; shapes swapped on the main thread (docs/38) |
| 9-11 | `0xC66714`, `0xC6676F`, `0xC66786` | `0xC66700` | `+0x40`, `+0x4C`, `+0x58` | argument 0 (scene node) | scene-graph walk, hop 4 |
| 12 | `0x4E626C` | `GeneralAllocator_MallocCore` | field `+0x4AC` | allocator hook pointer | runs on every allocation |
| 13 | `0x59A603` | `Job_Cancel` | field `+0x10` | job callback | |
| 14-15 | `0x95894E`, `0x95895F` | `0x958560` | field `+0x14`, `+0x1C` | object from an indirect call / `0x952560` | object component lookup |
| 16 | `0xC6CA2B` | `0xC6C810` | `+0x10` | `[0xC2A470 result + 0x38]` (cell shape) | as #8 |
| 17 | `0xB600C5` | `0xB5FCF0` | `+0x10` | field `+0x38` | |
| 18-20 | `0xC5FA98`, `0xC5FAAE`, `0xC5FAC5` | `SceneNode_FindRecursive` | `+0x40`, `+0x58`, `+0x4C` | argument 0 | scene graph |

The remaining 11 search sites of this kind: `SceneNode_FindRecursive 0xC5FAE0`, `0x931292`/`0x93117F`/`0x9311A3` (function pointers at `[[this+0x580]+0x7C] + 0x24/0x44`), `0x4E431F` (allocator), `JobManager_ExecuteJob 0x59974F`, `LooseQuadTree_RayNodeShapes 0x68115A` (physics shapes), `0xAC5832`, `0xAC9939`, `0x4F5966`, `0x682B83`. Outside the search, the highest are `0x671000` (4 sites, writer/portal; called from `WaterGrid_BuildCells 0x6729D0`, receiver argument 1), `Route_GenerateDestinationCandidates 0x752042` (destination slot `+0x10`, the radial-range path of docs/92 §4) and `0x74FDD0`.

### 4.2 What the unresolved sites mean for the replacement search

- **Routing library**: 7 non-lifetime sites in the search, all inside lazy lot builds and quadtree reads that docs/94 E16-E18 already move to the sim thread at snapshot time. None sits in the A\* loops (`WorldAStar_Search`, `QuadTreeCellAStar`, `AStarSearch_Run`, `AStarSearch_StampReader` are fully family- or data-resolved).
- **Engine code** (20 sites): scene graph, cell-grid raycast and physics loose quadtree, reached through height and obstruction queries (docs/91 rows 9-10). The snapshot replaces these reads with its own height and shape data, so their targets matter only for the "same answer" check.
- **Lifetime** (301 search sites): mostly temporaries in `PathPlanner_PlanHierarchical` (55), `PathPlan_WithRetries` and the A\* code. They free or release objects the plan itself made; a target that writes shared state would be a surprise, and the probe checks it.

## 5. Coverage limits

- Direct-call closure only from the listed roots: code reached only through an unresolved site is not in the scope (its own indirect sites are missing). The probe's unknown-target list is the check.
- The slicer is per function: values that come through fields written elsewhere, through out-parameters filled by a callee, or through containers stay unresolved.
- Family membership is by documented receiver or by the closure-built rule, not proven per site; a game object of another class at a family site would be missed.
- Indirect sites inside import DLLs and Mono are out of scope.

## 6. What the live probe should compare

| # | Check | Expected |
|---|---|---|
| 1 | For each CSV site with status resolved: every live target is in the targets column | 0 misses |
| 2 | Partly resolved: live targets ⊆ targets column, by basis | family and data-flow rows 0 misses; closure-built heuristic rows: list the misses |
| 3 | Unresolved, not lifetime (173 sites, 31 in the search): record every target | the first list of real targets for §4.1 |
| 4 | Cost function `0x676388`..`0x676B4A`: only the 5 `RouteCost_*` functions | yes |
| 5 | Lifetime sites: every target is an AddRef, Release or destructor (name or size check) and none writes routing data | yes |
| 6 | Sites whose function is never seen in a plan (writer and portal only) | zero hits during plans |

The CSV keeps one row per site with a stable address, so a report script can join live `(site, target)` pairs on `site` directly.

## 7. Files and names

- `docs/data/routing-indirect-sites.csv`: columns `site, caller, kind, slot offset, targets, status, note`. `kind` is `import`, `jumptable`, `global-fnptr`, `direct-via-reg`, `fnptr`, `fnptr-field`, `vcall`, `vcall-lifetime` (`+0`/`+4` slot) or `vcall-delete` (`+0` with flag 1). `targets` are `;`-separated addresses with symbol names where known (`null` = callers pass a null pointer). The note starts with the basis in brackets and ends with the groups, search hop, code area and rank score.
- Ledger: docs/51 §36 (I-102-1 to I-102-8).
- Names appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/102>`): `SortCmp_Field4Greater 0x64FE80`, `LotQuadTree_ctor 0x664650`, `vt_LotQuadTree 0xFED994`, `vt_RoutingSearchPath 0xFEC058`, `purecall_thunk 0xF7E8AC`, `LotQuadTree_QueryCbA..D 0x662D00/0x662D30/0x662DB0/0x662DD0`.
