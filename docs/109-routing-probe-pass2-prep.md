# 109. Routing follow-ups: search flag byte, option bit, probe pass-2 roots

Written 2026-10-18. Static and offline only: the game, the probe server and the test queues were not run, and no ts3native source was changed or built. This closes the follow-ups docs/106 left for PLAN "Routing off-thread" item 6 (no gaps in our knowledge of the routing code): the two gates that kept 65 armed sites silent (§2, §3), the three unnamed footprint interface vtables (§4), the site table for probe pass 2 (§5) and a repro plan for the sites that did not fire (§6).

Tools: capstone 5 over `game-copy/bin/TS3W.exe` (Steam 1.67) and `game-copy/analysis/functions.csv`; `tools/managed-callgraph.ts` for one managed caller; scratch scripts in the session scratchpad (`io-x2/`). Ghidra was not needed. Tags as in AGENTS.md: **[verified]** = read in the disassembly (address given); **[inferred]** = reasoning, with a ledger row in docs/51 §45.

## 1. Summary

- **The "search byte `+0x1C`" is not a field: it is the sixth stack argument of `QuadTreeCellAStar 0x61E500`** (`[ebp+0x1C]`, thiscall, `ret 0x18`), the stamp flag docs/98 §8 already describes. Both callers pass 0 and nothing else reaches the function, so **`AStarSearch_StampReader 0x61C9B0` is unreachable in 1.67** and its 53 sites can never fire [verified] (§2).
- **Context bit `0x20` (`X+0x50`, gates `QuadLeaf_CountBlockingTypes 0x669530`) is never set in 1.67** [verified for every context builder found]. It is the docs/98 §3.5 "type surcharge" flag (+50 per extra blocking type in a penalised cell). The route job's flag word, the retry and obstacle-replan words and all seven `LotPlanCtx_Init` contexts were read; none contains `0x20` (§3). The same trace answers most of docs/98 U6 (which plans set which context bits).
- **The three unnamed interface vtables** belong to the private footprint (`0xFEBB98` of `vt_PrivateFootprint 0xFEBBC0`), the lot floor footprint (`0xFEBCE8` of `vt_LotFloorFootprint 0xFEBD10`) and the lot wall footprint (`0xFEBE10` of `vt_LotWallFootprint 0xFEBE38`) [verified ctors and alloc tags]. Correction to docs/106 §4: of the 9 interface slots, 5 are plain `this-4` thunks and 4 are shared functions that subtract 4 themselves (§4).
- **Probe pass 2 is prepared, not built.** `tools/snap-vcall-sites.py` takes `--extra-roots`, `--arm-thunk-prologues`, `--list` and `--base`; with no new option it writes the shipped table byte for byte (checked). The pass-2 table `native/ts3native/vcallsites2.h` has **1,534 sites** (all 1,182 of pass 1 plus 352 new) from 121 roots (the 51 built-in, 69 of the 71 docs/106 targets, two of which are already roots, plus the not-armed water grid builder). Rebuild steps in §5.3; draft jobs in `tools/harness/queues/snap-probe2.json` (dry run checked, not run).
- **Repro plan (§6):** most unfired groups map to existing repros (`rt-s2b-sweep`, `verify-v22`, `pet-guard-cancel`, `ip-portal-leak`, the save-matrix travel case). The missing steps are built (2026-10-18, static only, not run): agent `build_level`, the `tool_select` tool ids and `product: "first"`, `tool_drag` `tiles`, reflect roots `@door`/`@upstairs`/`@gangway`/`@route`, and repros `bp-build-levels`, `bp-upstairs-route`, `bp-door-lock`, `ip-houseboat-create` with four jobs in `snap-probe2.json` (§6.1).

## 2. The stamp flag (`QuadTreeCellAStar` argument 6)

| Fact | Where [verified] |
|---|---|
| The gate is `cmp byte [ebp+0x1C], 0` then `call AStarSearch_StampReader` | `0x61E7E1`, `0x61E7EE` |
| `QuadTreeCellAStar` is thiscall with 6 stack arguments; `[ebp+8]` is argument 1, so `[ebp+0x1C]` is argument 6 | prologue `0x61E500`, `ret 0x18` at `0x61E637` and `0x61E893` |
| Direct callers: only `InRoomPlanToPoint 0x632630` (`0x632C1A`) and `InRoomPlan 0x632E40` (`0x633594`) | call index over all of `.text` |
| `InRoomPlanToPoint` pushes `ebx` as argument 6; `ebx` is zeroed at `0x632BCE` and survives the call to `0x629E70` (callee-saved) | `0x632BCE`, `0x632C09` |
| `InRoomPlan` pushes the constant 0 on both paths that reach the call | `0x633563`, `0x63357F` |
| No data reference to `0x61E500` or `0x61C9B0` anywhere in the image (no vtable slot, no function pointer); the only caller of `0x61C9B0` is `0x61E7EE` | byte scan, call index |

So the stamp pass is compiled in but switched off in this build: the flag is a constant 0 at the only two call sites. Name: **stamp flag** (`QuadTreeCellAStar` arg 6, as docs/98 §8 calls it). Consequences:

- The 53 `AStarSearch_StampReader` sites leave the "unfired, matters" list of docs/106 §7: they are dead code, not a rare path.
- docs/92 §7 (standing Sims row) lists the stamp reader as a reader of standing Sims' footprints at hop 8. That is a static-closure path that never executes; the collectors are the readers.
- The A\* generation stamp `0x11580C4` (docs/101 §2 item 1) is read at runtime only in `AStarSearch_NodeState`; the load at `0x61CC73` is in dead code. A per-thread stamp needs 2 patch sites, not 3 [inferred from the above, I-109-3].

## 3. Context bit `0x20` and where `X+0x50` comes from

### 3.1 The gate and the bit

`QuadTreeCellPos_C 0x66B090` takes the lot search context X as its argument (`ebp`, loaded at `0x66B09D`); at `0x66B2B2` it tests bit `0x20` of `X+0x50` and, if set and the leaf holds more than one footprint (`0x626360` > 1), calls `QuadLeaf_CountBlockingTypes` at `0x66B2D9` [verified]. Its only callers are `QuadCell_Class 0x66B600` and `QuadCell_Multiplier 0x66B670`, which pass the context through (or 0, when they recompute without one) [verified]. docs/98 §3.5 gives the effect: +50 per extra blocking type in a penalised cell and +50 to enter a penalised cell from a normal one. Name: **`LotSearchFlag_TypeSurcharge` (`0x20`)**.

### 3.2 The main chain: route job to `X+0x50`

| Step | What happens to the word [verified] |
|---|---|
| `LotPlan_Inner 0x63F6E0` | `X+0x50` = its argument 9 (`[esp+0x1F4]` at `0x63F7E0`) `| 0x10000` (`0x63F823`), `| 0xF` when argument 12 (obstacle mode) is set (`0x63F817`), later `| 0x2000000` (`0x63F985`, docs/98 height test) |
| `LotPlan 0x6410B0` | passes its argument 9 as argument 9 (`0x6410CC`/`0x6410E5`, and the second call at `0x64129E`) |
| `PathPlanner_PlanHierarchical 0x65E0D0` | passes its own argument 9 (`[ebp+0x28]`) at all 7 `LotPlan` calls (`0x65E3C2`, `0x65F11C`, `0x65F582`, `0x660794`, `0x660ABE`, `0x660D83`, `0x661A9B`) and as argument 4 of `WaterPath_Plan2 0x656A70` (`0x6611A1`, `0x661308`), which hands it on as `LotPlan` argument 9 (`[ebp+0x14]`, `0x656FEE`). `[ebp+0x28]` is only tested in this function (`0x65E422`, `0x65E848`, `0x65F78C`, `0x65FE58`, `0x66055E`), never written |
| `PathPlan_WithRetries 0x661B50` (12 arguments, `ret 0x30`) | argument 9 in `esi`; retries OR in `0x800000` (`0x661BCC`) and `0x11000000` (`0x661E15`) |
| `RoutingJob_Execute 0x81B2A0` | builds the word in `ebx` from `RoutingOptions_Test` (`0x81BCA9..0x81BF16`) and pushes it as argument 9 at `0x81BF46`; the obstacle replan (`ReplanToFindObstacleWhenPathPlanFails`, docs/11 Q1) passes the constant `0x1F` at `0x81C636` |

The job word, bit by bit (RouteOption tested as `(lo, hi, which)` of `RoutingOptions_Test 0x81C880`, which = 1 for RouteOption2; C# names from docs/11 Q2 where it has them) [verified `0x81BCA9..0x81BF16`]:

| Context bit | Set when | Read by the in-room search (docs/98 §3.5) |
|---|---|---|
| `0x4` | lo `0x2` (`PassThroughObjects`) | penalise blocked classes up to 8 |
| `0x8` | lo `0x1` (`PassThroughWalls`) | penalise blocked classes up to 10 |
| `0x10` | lo `0x8` (`UseMostDirectPath`) | walkable non-1 classes get multiplier 1.0 |
| `0x2` | never; cleared at `0x81BDF8` when hi `0x80` is set | penalise up to 6 |
| `0x1`, `0x20` | never | penalise up to 4; type surcharge |
| `0x40`, `0x80`, `0x200`, `0x400`, `0x800`, `0x1000`, `0x2000`, `0x4000`, `0x8000` | RouteOption2 lo `0x4`; lo `0x40`; lo `0x80`; lo `0x80000000` (`RouteAsGhost`) unless a local byte is set; hi `0x80`; hi `0x100`; hi `0x200`; hi `0x400`; RouteOption2 lo `0x2` (`EnablePlanningAsBoat`) | not read by the in-room search |
| `0x200000`, `0x400000` | hi `0x1000`; hi `0x4000` | `0x200000`: small-cell clearance test |
| `0x1000000`, `0x8000000` | hi `0x10000000` (`EnableWaterPlanning`); hi `0x20000000` (`EnablePondPlanning`) | no |
| `0x4000000` | agent class `job+0xC0 & 0xCF00` in {`0x4100`, `0x4200`, `0x4300`} | no |
| `0x10000000`, `0x20000000` | two locals computed earlier in the job (`[esp+0x2F]`, `[esp+0x37]`) | no |
| `0x40000000`, `0x80000000` | RouteOption2 lo `0x8`; RouteOption2 lo `0x10` | no |

Plus, after the job: `0x800000` and `0x11000000` (retries), `0x10000` (always, `LotPlan_Inner`), `0xF` (obstacle mode), `0x1F` (obstacle replan), `0x2000000` (height test).

### 3.3 The other contexts

`LotPlanCtx_Init 0x6298F0` zeroes `+0x50` (`0x62993E`). Its seven callers and what they store there afterwards [verified stores; offsets corrected for the pushes in between]:

| Builder | `X+0x50` |
|---|---|
| `LotPlan_Inner 0x63F7CC` | the job word chain of §3.2 |
| `0x631050` (`0x63112F`) | `0x100` (`0x631164`) |
| `0x633FA0` (`0x634044`) | `0x10000` (`0x63407A`) |
| `RoomGraph_FirstUse 0x63D376` | `0x110000` (`0x63D3A0`) |
| `PathPlanner_PlanHierarchical 0x65E768` (portal nodes) | not written (0) |
| `0x824850` (`0x8249EB`) | not written (0) |
| `RoutingWorld_SurfaceAtPoint 0x834C1B` | not written (0) |

The obstacle-query contexts of docs/84 (`0x82EDB0`, `0x82F0B0`) set `ctx+0x50` = 0 [verified there].

**Verdict:** no context that reaches `QuadTreeCellPos_C` carries `0x20`, so `QuadLeaf_CountBlockingTypes` (12 sites) is unreachable in 1.67, like the stamp reader [verified for the builders above; a context built some other way would reopen it, I-109-2]. A replacement search does not need the surcharge rule; it does need `0x4`, `0x8`, `0x10` (from route options), `0xF`/`0x1F` (obstacle mode and replan), `0x100`, `0x80000`, `0x100000`, `0x200000` and `0x2000000`.

## 4. The three unnamed footprint interface vtables

| Interface vtable | Primary vtable (class) | Built by [verified stores] | Alloc tag |
|---|---|---|---|
| `0xFEBB98` | `0xFEBBC0` `vt_PrivateFootprint` | `RoutingWorld_AddPrivateFootprint` (`0x831FF4`/`0x831FFB`), `RoutingWorld_UpdateObjectFootprintCore` (`0x8326A8`/`0x8326AF`), `0x624540` (from the factory `0x62CB20`) | "Routing/PrivateFootprint" (docs/84) |
| `0xFEBCE8` | `0xFEBD10` `vt_LotFloorFootprint` | `LotFloorFootprint_ctor 0x807C90` (11 calls, all in `RoutingWorld_UpdateLotFloorFootprints 0x8336F0`), `0x625070` (factory) | "Routing/LotFloorFootprint" |
| `0xFEBE10` | `0xFEBE38` `vt_LotWallFootprint` | `LotWallFootprint_ctor 0x80B740` (`RoutingWorld_UpdateLotWalls` `0x830AAA`, `0x830B19`), `0x6250E0` (factory) | "Routing/LotWallFootprintImpassable" / "...Passable" |

Each constructor stores the primary vtable at `+0` and the interface vtable at `+4` [verified]. Interface slot `+0` reaches `0x624E80` (private), `0x6250C0` -> `0x807D00` (floor) and `0x625120` -> `0x80B7E0` (wall), which store the two vtables again: destructor-like, not a primary slot [verified stores; "destructor" inferred].

Slot layout, the same in all 11 interface vtables of docs/106 §4 [verified]: `+0`, `+4`, `+8`, `+0xC`, `+0x20` are `sub ecx,4` then a jump (`+0` to the destructor-like function above, `+4` primary `+0x7C` owner id, `+8` primary `+0xC` type byte, `+0xC` primary `+0x98`, `+0x20` primary `+0x60`); `+0x10` (`0x80E410`, or `0x807150` in `0xFEADE0`) and `+0x14` (`0x807220`) are shared functions that subtract 4 themselves; `+0x18` is the shared empty `ret` `0xC0FD60`; `+0x1C` is `FootprintIface_IsKind 0x620D20`, which reads `[ecx-4]`. docs/106 §4 says "all thunks": the effect is the same (every slot works on the primary object), the wording was not.

Names appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/109>`): `vt_FootprintIface_FEBBC0`, `vt_FootprintIface_FEBD10`, `vt_FootprintIface_FEBE38`, `vt_PrivateFootprint`, `vt_LotFloorFootprint`, `vt_LotWallFootprint`, `LotFloorFootprint_ctor`, `LotWallFootprint_ctor`.

## 5. Probe pass 2 (prepared, not built)

### 5.1 Tool changes (`tools/snap-vcall-sites.py`)

- `--extra-roots FILE`: one `0xADDR name` per line, `#` comments; roots already in the built-in `ROOTS` are skipped.
- An extra root that `functions.csv` lacks (a vtable-only function Ghidra never made: 32 of the 71) is read to its first `ret`/`int3` or outgoing `jmp`, capped at the next `functions.csv` entry. The built-in `ROOTS` keep the old rule (up to the next entry), so the pass-1 output does not change.
- `--arm-thunk-prologues`: keeps the sites in the first 16 bytes of those csv-less extra roots. They are vtable thunks such as `FootprintMember_FwdSlot0 0x626210`, whose only indirect call is in the first bytes; no detour sits on them (the docs/68/Frida address filter still applies, and `vcall.cpp` drops a site whose bytes changed at arm time). Without it, 14 such sites stay unarmed; with it 12 are armed and 2 stay skipped as near a hook address.
- `--list FILE`: one tab-separated row per site: site, length, call/jmp, function, name, hops, the root it was first reached from, and `pass1`/`new` against `--base` (default the shipped `vcallsites.h`).
- `--self-check` covers the roots parser and the thunk end rule.
- Check: `python tools/snap-vcall-sites.py` with no new option regenerates `native/ts3native/vcallsites.h` with identical content (1,723 functions, 1,182 sites).

### 5.2 The pass-2 table

Inputs: `docs/data/snap-pass2-roots.txt` = the 71 targets of docs/51 I-106-8 (`0x80E610` and `0x80EC30` are already built-in roots) plus `WaterGrid_CutWorldObjects 0x671000`, whose 4 sites docs/106 §7 lists as never armed (outside the closure). Outputs: `native/ts3native/vcallsites2.h` (not included by any source) and `docs/data/snap-pass2-sites.txt`.

| | Pass 1 (shipped) | Pass 2 |
|---|---|---|
| Roots | 51 | 121 (51 + 70) |
| Functions in the closure (12 hops) | 1,723 | 2,651 |
| Sites | 1,182 | 1,534 (1,389 call, 145 jmp) |
| Skipped: prologue / near a hook address | 11 / 4 | 19 / 7 |

All 1,182 pass-1 sites are in pass 2; 352 are new. Where the new ones come from (first root reached): `ScriptHost_InvokeDelegate 0xD75FA0` 103 and `0xD7D2F0` 96 (the Mono host: these closures run into the Mono runtime), `RouteDest_SlotArray_Generate 0x7596E0` 38, the engine routine `0x4F65B0` 34, resource code `0x5C2880` 11, the water grid builder 4, and 1-3 each for the remaining roots; 113 sit in the root functions themselves.

Cost: the sampler budget is per site (16 hits per 30 s window, docs/94 §7), so 352 more sites add at most about 5,600 dispatches per window, and only for the sites that are hot. The 199 Mono-host sites are the ones most likely to be hot (every script call goes through the host). If the pass-2 overhead check fails, comment out `0xD75FA0` and `0xD7D2F0` in the roots file and regenerate: about 150 new sites remain, and the managed exits stay covered by the pass-1 sites in `World_FireEvent` (`0x78F2FC`, `0x78F363`).

### 5.3 Rebuild steps (for whoever builds pass 2)

The shipped probe build is not touched: `vcall.cpp` includes `vcallsites.h`, so pass 2 is built from a throwaway checkout where that one header is replaced.

1. Regenerate only if `game-copy/bin/TS3W.exe`, `game-copy/analysis/functions.csv`, docs/68, `tools/probe/agent.js` or `tools/frida/*.js` changed since this commit (the hook-address filter reads them):
   `python tools/snap-vcall-sites.py --extra-roots docs/data/snap-pass2-roots.txt --arm-thunk-prologues --out native/ts3native/vcallsites2.h --list docs/data/snap-pass2-sites.txt`
   Expect "2651 functions, 1534 sites" and "352 sites not in vcallsites.h" (other numbers mean an input changed: read the list before building).
2. Make a scratch worktree at this commit (`git worktree add <dir> <commit>`), and in it replace the table: `copy /y native\ts3native\vcallsites2.h native\ts3native\vcallsites.h`. No other file changes; the version stays 0.23.0, counters v43.
3. Build there: `native\ts3native\build.cmd <absolute out dir without spaces>`. It compiles `ts3native.asi` and runs `selftest.exe` (including `TestProbeSnap`); stop on any failure.
4. Park the result: copy `<out>\ts3native.asi` to `game-copy\ts3native.asi.snapprobe2` in the main checkout (the name `tools/harness/queues/snap-probe2.json` expects). Remove the scratch worktree; do not commit the swapped header.
5. Check without the game: `node tools/harness/queue.ts tools/harness/queues/snap-probe2.json --dry-run` (the "MISSING" asi notice must be gone).
6. Run (Rodney, clean night, like pass 1): `node tools/harness/queue.ts tools/harness/queues/snap-probe2.json`. Pass: no crash, `vcReady` 1, `vcFaults` 0, `vcSkipped` 0 at arm time, `vcSites` 1534 in `/native`, frame times within noise of `bp-env-on`.
7. Read: `node tools/harness/snap-report.ts <ts3native.log>...` for NEW/OUT targets, then `node tools/indirect-live-vs-static.ts <raw logs> --pairs --unfired --sites native/ts3native/vcallsites2.h` for the join against docs/102 and the unfired list. Sites outside the docs/102 CSV show as `no-row`; the list file names their function and root.

## 6. Repro plan for the unfired sites

docs/106 §7 groups, updated with §2-§3 and with the static trigger of each site (callers traced with the direct-call index).

| Group (sites) | Static trigger [verified callers] | Existing command or repro | Missing step |
|---|---|---|---|
| Stamp reader (53), `QuadLeaf_CountBlockingTypes` (12) | none in 1.67 (§2, §3) | none needed | none: dead code |
| **A. Lot-table miss and lot unload**: `0x62DE40` (`0x62DF97`, `0x62DFB1`), `LotGraph_dtor` (`0x62B9D8`, `0x62B9EC`) | miss branch of `LotTable_GetOrInsert 0x62EAB0` (copy at `0x62EAFB`, two temporaries freed at `0x62EB1F`/`0x62EB2B`); also `LotTable_Erase` from `LotGraph_Reset 0x62E420`, and `0x62D610` from `0x635B70`/`0x637520`. A miss happens only for a lot not yet in the table (docs/80 §2: load-time inserts), so in a running world it needs a new world or a new lot | save-matrix travel case (`run.matrix ["--travel","--cases","travel","--to",...]`, as `tools/harness/queues/travel-all.json`; job `bp-snap2-travel`); agent `travel_vacation`/`travel_home`; `tools/harness/travel-trip.ts` | none for travel. For a lot added in the same world: **built**, repro `ip-houseboat-create` (job `ip-snap2-houseboat`): `reflect_call` `@gangway.ChangeHouseboatSize` `Small`/`Medium`, then `None`. That is the call the new-houseboat flow makes (`PlayFlowModel.CreateHouseboatLot` -> `Gangway.ChangeHouseboatSize` -> `World.CreateHouseboatLot`; `None` -> `World.DeleteHouseboatLot`) [verified in the script DLLs; not run] |
| **D. Lot-manager index rebuild** `0xC66700` (`0xC66714`, `0xC66755`, `0xC6676F`, `0xC66786`) | `WorldManager_RebuildLotIndex 0xC68680` runs lazily when `WM+0x1CC` is set. Setters on the world manager: `WorldManager_MarkLotIndexDirty 0xC5FEC0` (from `Lot_Load` `0xACF852`, and via `0xACEB20` from houseboat create, `World_RemoveLotCommon 0xA48CC0` and `0xD41630`) and `WorldManager_DetachLot 0xC65830` (`WorldManager_RemoveLot`). Placing or moving an object does not set it (docs/106 §7 said "place or move a lot object": not supported by the writers found) | the travel job above (every lot of the destination goes through `Lot_Load` after arming) | none for travel; houseboat create/delete as in A (`ip-houseboat-create`) |
| **B. Object churn, build/buy exit, level polygons**: `LotQuadTree_Insert` (`0x666D88`, `0x666E04`), `LotQuadTree_ApplyInsert` (`0x665CEF`, `0x665DAD`), `LotRouting_QuadTreeBuild 0x62F9C1`, `LotRouting_FootprintRoomIds 0x6354D7`, `Footprint_EnsureLevelPolygons 0x80E5F6` | object add/remove (`0x6376D0`, `LotRouting_RemoveObjectFootprints`), lot rebuilds at build/buy exit (`RoutingWorld_UpdateLotWalls` -> `QuadTreeBuild`, `LotGraph_Build`), polygon cache rebuild on the next `RouteFollow_EditPath`/segment test. `0x666D88`/`0x666E04` are the deferred branch: an insert while a query holds the tree (`Q+0x48 > 0`), whose in-game source is the door-lock refresh `InRoom_ApplyPortalLockFootprints 0x632460` during in-room planning (docs/101 §4.4). `0x665CEF` is a re-insert that drops the last reference of an old map entry | `rt-s2b-sweep` (10 objects placed and removed twice, buy mode on/off; job `bp-snap2-objects`), `verify-v22` (build/buy enter and exit, 10 objects), `object-id-cache`; agent `place_object`, `move_object`, `remove_objects`, `build_mode` | **Built** (§6.1). (1) Deferred insert: repro `bp-door-lock` (job `bp-snap2-door`): `@door.SetLockType SelectedHousehold`, the household Sim walks to the door, two visitors `sim_visit_lot` home and `sim_route_to` the door, then unlock. (2) Walls, floors and stairs on two levels: repro `bp-build-levels` (job `bp-snap2-build`) with the new `build_level` and the `tool_select` ids (0 wall, 2 floor, 16 stairs) |
| Routing over stairs (multi-level footprints during plans) | stair portals and level polygons are read when a plan crosses levels | `list_objects` then `do_interaction` on an object with `level >= 1` for a Sim downstairs, on any BP two-storey home | **Built**: repro `bp-upstairs-route` (job `bp-snap2-upstairs`). `list_objects` did not return `level` (this row said it did); it now returns `level` and takes `min_level`. The target is the reflect root `@upstairs`, because repro steps cannot pass one step's result to the next |
| Water obstruction query: `RouteObstructionAtPoint` (`0x655BC9`, `0x655E49`), `0x62F470` (`0x62F5B5`, `0x62F5DE`) | `RouteObstructionAtPoint 0x655880` is called from the hierarchical planner only on a water-grid branch (`0x6435B0` -> `0x6437AF`, after `WaterGrid_CellIndex`), and from `IC_Route_GetObstructionAtWorldOrLotPoint`, whose only managed caller is `GetInBoatRouteAction.StandAndWaitCycleHandler` (boat boarding); `0x62F470` is called from it (`0x655D55`). `0x63AFD0`, its other caller, has no caller or reference (dead) | `ip-portal-leak` (`sim_portal_trip` into the sea and onto a houseboat, IP); `ip-houseboat-gohome` | none |
| **C. Route cancelled mid-plan**: `Job_Cancel 0x59A603` | `RoutingJob_dtor` -> `Job_Release` -> `Job_Cancel` for a job released before it ran (docs/106 §5) | `pet-guard-cancel` (8 guard/cancel cycles with spawned Sims at ultra speed, built so cancels land during route jobs; job `bp-snap2-cancel`); agent `cancel_interactions` | none. With `[RouteAsync]` on, jobs are posted by our worker path; whether a cancel then still goes through the game's `Job_Cancel` is not checked [inferred, I-109-6]: if the site stays silent, rerun the job with `RouteAsync.Enabled=0` |
| Route reuse: `Route_UnlinkPaths` (`0x75732D`, `0x75733A`), `Route_PlanCommon 0x760F0A` | `Route_PlanCommon` calls `Route_UnlinkPaths` (`0x76078C`) on every plan, but these sites run only when the route already holds paths, i.e. a replan of a used route object; also the path-editing ICs (`IC_Route_InsertCustomPathAtIndex`, `IC_Route_Clone`, `IC_Route_SetLoopStart`, ...) | `bp-route-fail-active` (stranded Sim, repeated `sim_route_to`) [inferred to replan the same route object] | **Built** in `bp-upstairs-route`: `sim_route_to` keeps its route as `@route`, then `reflect_call` `@route.Replan` twice after each plan |
| `0xAC5832` (`0xAC5610`) | world object queries (`0xAC8310` from `IC_World_GetObjectsImpl`, `IC_World_GetObjectCountImpl` and 10 native callers), a branch inside | none specific | leave for pass 2 (it is armed in both tables) |
| Debug memory tracer `0x931292`, `0x93117F`, `0x9311A3` | off in release (docs/84) | none | none |

### 6.1 New agent steps and repros (2026-10-18, static only)

None of this has run in the game. The sources are the script DLLs (UI.dll, SimIFace, Sims3GameplaySystems), not a live check.

| Step | What it does | Source in the game scripts |
|---|---|---|
| `build_level {up?: true}` or `{level: n}`, optional `aim: [lx, lz]` | Floor up/down on the home lot, as the build UI's floor buttons; `aim` points the camera at a home-lot tile (15 m up, 12 m back) so `tool_drag` can calibrate. Returns `level` and `top` | `ZoomPuck` floor buttons -> `PlayFlowModel.ChangeDisplayLevel` -> `Lot.ChangeDisplayLevel` / `Lot.SetDisplayLevel` |
| `tool_select` ids | 0 wall, 1 wallpaper, 2 floor, 3 foundation, 4 object, 8 fence, 12 sledgehammer, 16 stairs, 17 railings, 20 column, 25 wall drag (room), 30 half wall | `Sims3.UI.ToolMessageID` (internal enum in UI.dll); the build controller selects 0 for the wall tool, 2 for floors, 16 for stairs |
| `tool_select {product: "first"}` | First catalog entry: the first floor/wall pattern preset (product and preset) for tools 2/1, else the first `UserToolUtils.GetProductList` entry of the same number (stairs: product type 16); no entry keeps the empty key | the build catalog fills its grids from the same calls. Whether the wall tool needs a product is not checked [inferred: the empty key may do] |
| `tool_drag {tiles: true}` | Points are home-lot tile coordinates on the displayed level (`World.GetWorldPosition`), so repros need no world coordinates | as `tile_info` |
| Reflect roots `@door`, `@upstairs`, `@gangway`, `@route` | Home-lot door nearest the active Sim; the object with `Level >= 1` nearest the active Sim (any lot); a port gangway without a houseboat (kept for the remove call); the last `sim_route_to` route. `go_here`, `do_interaction` and `sim_route_to` also take `"@..."` as `target` | `CommonDoor.SetLockType(tLock)`, `GameObject.Level`, `Lot.GetGangway`, `Gangway.ChangeHouseboatSize(HouseboatSize)`, `Route.Replan` |
| `sim_route_to {target, radius?}` | Plans to an object (`Route.PlanToPointRadius`) instead of a point | |
| `sim_visit_lot {lot: "home"}` | The active household's home lot | |

Jobs in `tools/harness/queues/snap-probe2.json` (same asi `snapprobe2` and ini as `bp-snap2-objects`, 5-minute windows): `bp-snap2-build`, `bp-snap2-upstairs`, `bp-snap2-door` (Baseline-BP-Spring), `ip-snap2-houseboat` (Baseline-IP-Spring).

Open points for the run:
- The build tiles 1..5 near the lot corner may be taken by the house; the tool then refuses (every tool step is optional). The stair click is a try at one tile.
- `@upstairs` fails in a world with no upper floors.
- `SelectedHousehold` = locked for all but the household is inferred from the enum and the lock menu.
- Whether `Gangway.ChangeHouseboatSize` called from the agent completes like the UI flow is not checked.
- First run (`bp-snap2-build`, 2026-10-18): ground-floor `tool_drag {tiles: true}` spread as expected (wall 1,1 -> 5,1 about 140 px), but after `build_level {up: true, aim: [3, 3]}` every level-1 point landed near one pixel (wall 1,1 -> 5,1 at 233,612 -> 232,592; floor 1,2 -> 2,3 1.3 px apart), so the level-1 tool steps likely did nothing and the run still reported ok. Cause [inferred, not confirmed live]: the homography maps one world plane (x, z) to pixels, but it was fitted from every floor pick on screen; on level 1 the picks over the lot and those over the terrain outside sit a floor apart, and a fit over both planes is wrong. Fix (ts3agent, built, not run): tile drags calibrate only from picks within 1.25 m of the tile's floor height (`calibrate {y}` does the same; the math is in `mods/ts3agent/src/plugin/ScreenFit.cs`, checked by `node tools/selftest/screenfit/run.ts` on a synthetic level-1 scene), on a 19 x 15 pick grid, and refuse a calibration taken while the camera moved. `tool_drag` now fails before any mouse event when points 0.5 m or more apart project under `min_px` (default 8) apart, and, for tiles, when a floor pick at a projected pixel lands more than 1 m from its tile; the result carries `calib` (samples, picks, `plane_y`, `max_error_px`) and `check_m`. A rerun should show level-1 drags spread like the ground floor with `check_m` under 1, or a step error naming the pick heights (then the level-1 picks miss the upper floor and the projection needs the camera model instead).
- `profile-world.ts` ignores a repro's `cleanup`, so a failed unlock leaves the door locked for that session only (no save step).

## 7. Files

- `tools/snap-vcall-sites.py`: pass-2 options (§5.1); default output unchanged.
- `docs/data/snap-pass2-roots.txt`, `docs/data/snap-pass2-sites.txt`, `native/ts3native/vcallsites2.h`: pass-2 inputs and outputs (§5.2). `vcallsites2.h` is not included by any source file.
- `tools/harness/queues/snap-probe2.json`: eight draft jobs (§5.3, §6, §6.1), not run.
- Pass 3 (§8.8): `native/ts3native/vcallsites3.h` (4 sites, `/DTS3N_VCSITES3`), `[ProbeSnap] Threads` in `vcall.cpp`, `tools/harness/queues/snap-probe3.json` (`bp-snap-probe3-worker`).
- `mods/ts3agent` (`BuildHandlers.cs`, `ControlHandlers.cs`, `ReproHandlers.cs`) and `tools/harness/repros.json`: the §6.1 steps and repros.
- `tools/ghidra/symbols/ts3w-1.67.symbols.csv`: 8 names appended (§4).
- Ledger: docs/51 §45 (I-109-1 to I-109-7), with verdicts on I-106-3 and I-106-4.

## 8. G2 verdict from probe pass 2

Written 2026-10-19 from the logs of 2026-10-18 (UTC). No game run and no code change: this section, 490 rows appended to `docs/data/routing-indirect-sites.csv` and docs/51 §86 (I-109-20 to I-109-27). Static checks with capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67) and `game-copy/analysis/functions.csv`; scratch scripts in the session scratchpad (`g2/`). Tags as in AGENTS.md.

Gate G2 (docs/94 §3.4, docs/116 §4): is everything the route search executes inside the static map (docs/102 closure plus the CSV), and are its global writes known?

### 8.1 Input and join

Nine `ts3native.log` files from `probe-out/queue/snap-probe2-2026-10-18-*`: `bp-snap-probe2` (the main job, plus an earlier short start of it), `bp-snap2-objects`, `-cancel`, `-travel`, `-build`, `-upstairs`, `-door` (all Baseline-BP-Spring) and `ip-snap2-houseboat` (Baseline-IP-Spring, the only IP job). Join: `node tools/indirect-live-vs-static.ts <the nine logs> --pairs --unfired --sites native/ts3native/vcallsites2.h`, against the CSV as it was before this section.

| Class | Pairs | Sites |
|---|---|---|
| match | 364 | 246 |
| resolves | 368 | 216 |
| family | 346 | 251 |
| hint | 5 | 4 |
| **new** | **103** | **75** |
| **no-row** | **720** | **415** |
| all | 1,906 | 1,202 |

The main job alone gave the 1,059 sites and 1,543 pairs of the brief (85 new, 580 no-row). The repro jobs and the IP job add 18 new and 140 no-row pairs; 61 pairs come only from the IP job.

### 8.2 Method: inside or outside the search

The probe records no thread and no caller per hit, so "inside" is decided by reachability [verified per function, inferred per hit]:

1. Direct closure of `Route_PlanLocked 0x82C6E0`: direct calls, tail jumps and function-pointer immediates (as docs/101 closure D, which reaches the world cost modes; docs/102 did not follow immediates), stopped at `Lock_Acquire`, `Lock_Release` and `EA_AllocTagged` as `tools/snap-vcall-sites.py` is. 1,334 functions. Without the stop list: 1,339, and no site changes class.
2. Live-extended closure: plus every live (site, target) pair whose site lies in a function already reached. Live pairs only: CSV predictions are not used as edges, because heuristic rows made false bridges (for example `0xB7C28F` predicted `Footprint_LevelPolygonQuery`, live `0xB71940`). 2,629 functions within 4 indirect hops. The script host entries `ScriptHost_InvokeDelegate 0xD75FA0` and `0xD7D2F0` are not expanded (their callers are the event-59 door callback, N12).
3. A site is **inside** when its function is in the live-extended closure and the first indirect edge on its path is not a shared helper whose live target belongs to a class built outside the search (§8.4). Every other site is **outside**, labelled by the docs/102 group of its row (new pairs) or by the pass-2 root it was first reached from (no-row).
4. For every inside target the body was read (writes to non-stack memory, global writes, callees), and every global write in the 1,295 functions of the live-extended closure that are not in the direct closure was listed and compared with docs/101 §2.2, docs/91 and docs/94 §8/§12/§17.

### 8.3 Classes

| Join class | Class | Sites | Pairs |
|---|---|---|---|
| new | inside the search | 38 | 54 |
| new | outside: writer or writer+portal group (`0x80E200`, resource lookups from writers, object removal `World_RemoveObjectFromObjectManager 0x798D86`, `0xB96380`, `0xC63140`, `0xC89F20`, `0x637900`) | 26 | 32 |
| new | outside: plan group (`Route_GoalStep 0x75C2D0`, sim thread before the job) | 5 | 10 |
| new | outside: portal and route ICs (`IC_Route_SetLockKeys`, `IC_RouteManager_AddPortal`, `IC_RouteManager_AddPortalLocationVariation`, `RouteHandle_Lookup`, `0x56F830`, `0x6257F0`) | 6 | 7 |
| no-row | inside the search | 43 | 66 |
| no-row | outside: route following (`RouteFollow_Update 0x8538B0`, `RouteFollow_Slot38`, `0x84C590`, `0x851710`, `Footprint_SegmentHitsLevel`, docs/107) | 186 | 264 |
| no-row | outside: steering (`Steering_PathStep 0x847390`, `Locomotion_ObstacleList 0x85FFE0`, `RouteFollow_A/B`) | 65 | 74 |
| no-row | outside: Mono host and runtime behind event 59 (`ScriptHost_InvokeDelegate`, `0xD7D2F0`, the GC and class code under them; 11 sites call MSVCR80 or ntdll) | 66 | 196 |
| no-row | outside: destination generation (`RouteDest_SlotArray_Generate 0x7596E0`, `0x830340`, `0x82A780`, `0x76F0A0`) | 27 | 27 |
| no-row | outside: resource system (`0x5C2880` family, `ResourceMan_FindDatabase`, `0x739DA0`/`0x739DE0`/`0x739E30`) | 17 | 35 |
| no-row | outside: route object release (`Route_Release 0x8ABE60`) | 1 | 8 |
| no-row | outside: shared helper, receiver class built outside the search (§8.4) | 10 | 50 |
| no-row | inside, mixed shared helper (`RefCounted_ReleaseAt8 0x9158AB`: 3 footprint targets inside, 5 other classes outside) | 1 | 8 |

Kinds of the 490 rows (from the call instruction and the register load up to 24 instructions before it, as docs/102 names them): vcall 316 (slot `+0x8` and up), vcall-lifetime 118 (slot `+0`/`+4`; docs/106 §6: footprint classes use these slots for type and cost queries, not lifetime), jumptable 36 (the two world cost modes inside the search, the rest in route following, the allocator, the timer and the Mono runtime), global-fnptr 14 (CRT and ntdll imports called through pointers, all in the Mono runtime) and fnptr 6 (register loaded outside the window; all outside). Inside the search there is no callback, CRT or Mono site: 79 vtable-slot calls and 2 switch tables.

**Inside, new targets (38 sites, 54 pairs).** 23 of the sites are already named in docs/106 (§4 rows A, D, E, F, G, or §7 as never fired); the CSV was never corrected, which is why they still showed as new. All targets were read [verified]:

| Family | Sites | Live targets | Reads / writes |
|---|---|---|---|
| Footprint refcount `+0x90`/`+0x94` (docs/106 §4 A) | `0x807421`, `0x807443`, `0x8295F5`, `0x6644B4`, `0x625D26`, `0x6688BE` | `RefCounted_AddRefAt8`, `RefCounted_ReleaseAt8` | the footprint's own count |
| Footprint type, owner and polygon slots (docs/106 §4 E, G) | 12 in `PathPlanner_AddHangingFootprint 0x636BF0`, `0x631471`, `0x631492`, `LotQuadTree_ApplyInsert 0x665C7F`, `LotRouting_FootprintRoomIds 0x6354D7` (IP only, first hit of this docs/109 §6 group-B site) | owner id and alias getters, type and kind queries, `Footprint_HasMembers`, `FootprintMember_GetFloat10`, `Footprint_LevelPolygonQuery` (W6a, known writer), `0x806FD0` (forwards to slot `+0x70`) | read only, except W6a |
| A\* heuristic and path extract (docs/106 §4 D, F) | `AStarSearch_NodeState 0x61E155`, `0x61E23E`, `AStarSearch_ExtractPath 0x61DD67` | `InRoomHeuristic_Euclid`, `AStarHeuristic_Zero`, `0x7EAEA0` | read only (`0x7EAEA0` = return 1; its symbol name is one of 546 vtable uses of a folded stub, not a log reporter) |
| Water goal (docs/106 §4 E) | `WaterJps_EdgeCost 0x61FFE9`, `0x620008` | `WaterPathGoal_BothNear`, `WaterPathGoal_Heuristic` | read only |
| Physics and scene stubs (docs/106 §4 D, E; N8) | `FootprintQuadtree_Leaf 0x681BD9`, `0xB7C28F`, `0xB7C2AC`, `0xB7C45D`, `0xB7CC9E` | `0x67DEE0` (return 0.0, folded stub), `0x6827D0` (negated float field), `0xB71940` (array getter, vtable `0x106ED60`), `0x8AB4F0` (byte getter) | read only |
| Lot getters (docs/106 §4 D; N2, N4) | `Lot_RoomLookup 0xA8E064`, `0xAB33B6`, `0xB0A358`, `WorldManager_FindLotByIdTyped 0xC62D62`, lot index rebuild `0xC66755` | `Lot_GetVec190`, `Lot_GetFloat1A0`, `Stub_Return1`, `0xAA93C0` (vector length, slot `+0x58` of `0x10641B0`) | read only. `0xC66755` is the first hit of docs/109 §6 group D (`0xC66700`) |
| Object component (N7) | `0x958643` | `0x7F1620` (address of field `+0x20`) | read only |

**Inside, no-row sites (43 sites, 66 pairs).** None is in the CSV because docs/102 stopped where a live target first leads; all now have rows:

| Family | Sites | What runs | State |
|---|---|---|---|
| A\* nodes | `AStarNode_Reset 0x61B1B4`, `0x61B41B` | node release (`0x61B520`, `AStarGoalNode_DeletingDtor`), `AStarNode_RemoveNeighbor` | A\* scratch and the goal-node free list `0x11CEF90` (docs/91 §3.6, docs/106 §6, I-106-6) |
| Quadtree entry lifetime (lazy) | `FootprintMapEntry_DeletingDtor 0x61ED25`, `0x61ED36`, `0x662E0F` | `0x662E00` = `vt_LotQuadTree` `+4` Release, `LotQuadTree_dtor` | docs/101 §2.2 #3 (`0x11CF168`) and #4 (`0x11581DC`) |
| Footprint member and interface slots | `0x620D30`, `0x626217`, `0x626288`, `0x6262B8`, `0x6262CB` (IP only), `0x6262D8`, `0x806B8D`, `0x806BFE`, `0x806C3C`, `0x806C6C`, `0x806CBE`, `0x806D08`, `0x806D48`, `0x8070BB`, `0xEC4FD0` | forwards to the primary footprint slot; kind tests (`0x80C300`, `0x807B70`, `0x80F840` read route options through `RoutingWorld_Get`); polygon overlap and contain tests; `0x6263B0` frees a member block | read only, except `0x6263B0` (a footprint member's own block) |
| Water goal | `WaterPathGoal_BothNear 0x66DCD1` | `WaterRoute_NearStartOrGoal` | read only |
| World cost modes | `RouteCost_Mode3 0x674360`, `RouteCost_Mode1 0x674F4F` (switch tables `0x674E2C`, `0x6757EC`) | cases inside the same functions | read only. Missing from docs/102 because the cost modes are reached by function-pointer immediates |
| Physics loose quadtree ray (N8) | `0x683E9B` | `0x682F70` fills the caller's hit record | the caller's record only |
| Polygon caches and door portals (lazy: W6a, W6d, W6g) | `0x80C43F`, `DoorRouting_AddDoorPortals 0x80CD6D`, `0x80D00E`, `0x80D1B8`, `0x80E11D`, `0x80E15D`, `Footprint_EnsureAllPolygons 0x80E668`, `0x80E675`, `0x80EC8C`, `0x80ED95`, `0x744A72` | `DoorRouting_AddDoorPortals 0x80CB40` (W6g root), `0x80EC30` (W6d), `0x80E570`, getters `0xC6DD50`, `0xAB1CD0`, `0x80C450` | known writers (docs/83, docs/94 §11, §15); reached from `LotQuadTree_ApplyInsert`/`ApplyRemove` and `PathPlanner_AddHangingFootprint`, the lazy work docs/94 E16-E18 forces at the cut |
| Object transform | `Matrix_CopyVirtual 0x80E071` | `0x80D8C0` (slot `+0x18` of component vtable `0x10440A8`) | **writes a function-local static, not in docs/101 §2.2** (§8.5) |
| `RoutingPathImp` slots (docs/93) | `0x81DECA`, `0x81E42B`, `0x81E603`, `0x81E618`, `0x81F353`, `0x81FE90` | path slots `+0x80`, `+0xC4`, `+0xEC`, `+0x148` of `0x10214D8`, `RoutingPathImp_DeletingDtor`, node vector frees | the plan's own output path (H11, per job); `0x820830` reads terrain height through `World_GetHeightAt` (N9) |
| Lot graph release (lazy) | `RefCounted_ReleaseAt8 0x9158AB` | footprint releases `0x626320`, `0x624E20`, `0x624E70` (inside); `0x8FD860`, `0x8E8DD0`, `0x8DD3D0`, `0x8A7160`, `0x9106E0` (outside, §8.4) | footprint pools #5, #6 |

### 8.4 Shared helpers: targets that belong to other callers

Five helpers inside the closure take a receiver from their caller and fire, live, targets of classes the search never builds. They are generic loops: `0x8A65D0` and `0x5BF1E0` call slot `+4` on every element of a pointer vector, `0x8CE540` assigns a range of refcounted pointers, `RefCounted_ReleaseAt8 0x915890` and `Job_Destroy 0x59A650` release a payload. `0x8A65D0` has 8 direct callers, `0x5BF1E0` 78.

| Site (search caller) | Targets classed outside | Evidence |
|---|---|---|
| `0x8A65EB` (`0x628EA0`, from `LotGraph_ctor`) | `0x731D40` (52 vtable uses; its own sites fan out to 30 targets in `0x6C`-`0x6D` code), `0x92C5F0` | the routing target at the same site is `vt_LotQuadTree` Release `0x662E00` (inside) |
| `0x5BF1FB` (`LotGraph_dtor`) | `0x7DBC30` (UI resource: `UIResource_ReleaseD3D`; vtable `0x101A9D8`) | vtable stored only by `0x7DBC80`, outside the live-extended closure |
| `0x8CE574` (`0x621DB0`, `LotGraph_Build` path) | `0x8BC100` (20 vtables `0x10297F8`..) | vtables stored only by `0x86DF20`, outside |
| `0x9158AB` (`LotGraph_dtor 0x62B9EC`) | `0x8FD860`, `0x8E8DD0`, `0x8DD3D0`, `0x8A7160`, `0x9106E0` | `0x8A7160`'s class (`0x103A4A0`) is built only by `0x8A7170`, outside |
| `0x59A688` (`RoutingJob_dtor -> Job_Release -> Job_Destroy`) | `RefCounted_AtomicRelease` (10 destructors: model builder, render and resource objects), `0x727690` | job payloads of other job kinds; the job release path is outside the search proper (docs/101 closure P stops at `RoutingJob_dtor`/`Job_Release`) |

One more is **verified** outside: `0x4CF583` (an array helper that calls its function-pointer argument per element) fired `0x5A8CD0` and `0x5C9D50`, which only `0x91A4A0`, `0xC10800` and `0x5DA9E0` pass; the search's caller `0xB76F60` passes `0x5C89E0`.

The same applies to two `resolves` sites outside this task's classes: `JobManager_ExecuteJob 0x59974F` (all job kinds, reached from the job release path through `Job_Cancel` and `JobManager_ReadyJob`) and `Job_Cancel 0x59A603`.

These targets write globals the routing docs do not list (§8.5). Classing them outside rests on the class evidence above, not on a live attribution [inferred, I-109-22, I-109-25].

### 8.5 State not already listed

Global writes in the 1,295 functions the live edges add to the direct closure: 66 store instructions, 61 at addresses no routing doc names.

- **Inside the search, new: `0x80D8C0` writes `0x11D9020..0x11D905F` (four 16-byte rows) behind the guard bit `0x11D9060`** [verified: `test`/`or` of bit 1 at `0x80D8CE`/`0x80D8E5`, then four `movaps` stores of constants]. It is a function-local static (an identity matrix) returned by slot `+0x18` of the component class `0x10440A8` (built by `0x9512F0`). Reached from `Matrix_CopyVirtual 0x80E060` (4,919 hits) and `0x958643` (component lookup `0x958560`, N7, search hop 7), and from `DoorRouting_AddDoorPortals` (lazy). Class **I** of docs/101 §2.2: written once, always the same bytes. It needs no redirect; it joins the warm-up check of docs/94 §12.3.2 as item #27 [I-109-21].
- Inside, already listed: `0x11581DC` (#4), `0x1165B80` (#5), `0x1165D48` (#6), `0x11CF168` (#3), `0x11D8FC0..0x11D8FDC` (#24-#26), `0x114C908` (#7), the goal-node free list `0x11CEF90` (docs/106 §6).
- Allocator path, gated off: `0x4E431F` (a hash table's allocation callback) reaches `0x624590`, which calls the debug memory tracker `0x9336F0` (writes `0x117B6E0`, `0x11DC190`) only when `[0x11DC18C]` is non-zero; release builds leave it zero (docs/84) and the tracker's sites never fired.
- Outside by class (§8.4), not search state if I-109-22 holds: the unlocked free list `0x11DA038` (deleting destructor `0x8A71E0` of class `0x103A4A0`), `0x11D9FE0` (`0x8A6A50`), `0x1157E40` (`0x6000D0`, design-mode compositor data in `docs/data/ic-safety-a.csv`), UI texture statics `0x11CD034..0x11CD078` (`UTFWin_PendingTexturesInit`), render and job statics (`0x11CE310..0x11CE370`, `0x11D16C0`/`D0`, `0x11D2FD0`/`E0`, `0x115B8E0`, `0x11D3C58..68`, `0x11CEB68..80`, `0x11D1CE0`/`F0`), thread and allocator statics (`0x11AF588..98`, `0x11CB744`, `0x11DC168..84`, `0x1234380`/`7C`), and config bytes `0x11ECEEC`/`ED`, `0x11E9128` (handler chain behind `Job_Cancel`).

Heap state: every inside target writes only state docs/91, docs/93, docs/101 §2.3 or docs/94 §11/§15/§17 already lists (A\* scratch and free lists, the plan's own path, footprint refcounts and member blocks, the W6 polygon caches, quadtree lifetime) or a caller's stack record. No new routing input and no new non-routing read: the reads are N2, N4, N7, N8 and N9 of docs/94 §17.2.

Managed exits: the only edges from the closure into the script host are `World_FireEvent 0x78F2FC`/`0x78F363` (event 59, N12, replayed under `Mode=1`); `0xD71510` is the allocator's core-block callback (`GeneralAllocator_MallocCore 0x4E626C`) and calls `GeneralAllocator_Malloc` only.

### 8.6 Verdict

After the 490 rows, the join shows **0 new and 0 no-row pairs** over the nine logs (match 364, family 1,169, resolves 368, hint 5): every target the probe saw has a row. **G2 does not pass yet. The gaps:**

1. **New global, class I:** `0x11D9020..0x11D9060` (`0x80D8C0`, §8.5). Add it to docs/101 §2.2 as #27 and to the warm-up check. No redirect needed, so G stays the choice under the docs/94 §3.4 rule.
2. **Attribution not proven live:** the non-routing targets of the five shared helpers of §8.4 (`0x8A65EB`, `0x5BF1FB`, `0x8CE574`, `0x9158AB`, `0x59A688`; also `0x59974F` and `0x59A603`): 5 non-routing targets at `0x9158AB` plus 8 no-row sites (45 pairs) reached only through them. Static class evidence says outside; the check is a worker-only pass: sample these seven sites only on the route worker inside `ServeAsync`, or only while `g_callerTid` is set, and expect only routing targets (I-109-22, I-109-25). If a non-routing target shows up, its writes in §8.5 become search state.
3. **IP coverage:** pass 2 has one 5-minute IP job (houseboat create and delete). It added 61 pairs that only IP produced, 3 of them at inside sites. Water, boat and portal plans in IP were sampled by pass 1 only. Run the main pass-2 job once in Baseline-IP-Spring (I-109-26).

Not counted as gaps: the 1,139 CSV sites that never fired (as docs/106 §7; the rare-path repros are §6) and the outside classes of §8.3, which are not the search.

### 8.7 IP pass 2

Written 2026-10-19 from `ip-snap-probe2` (Baseline-IP-Spring, 2026-10-18 20:27-20:37 UTC, the settings of the main job `bp-snap-probe2`). No game run and no code change: this section, 6 rows of `docs/data/routing-indirect-sites.csv` extended with the 9 new targets below, and docs/51 §88 (I-109-28 to I-109-30). Same method as §8.2.

**Run.** PASS: 20 sampler windows, 312,081 hits, 1,559 pairs, `faults 0`, `skipped 0`, armed 1,284 at the last window; machine load ok (other CPU 3.8%), 0 stuck Sims, game alive. The water sites fired in every window (`WaterJps_EdgeCost 0x61FFE9`/`0x620008`, `WaterPathGoal_BothNear 0x66DCD1`; in BP in 3 windows), and `WaterPath_Plan2 0x657136` fired only here.

**Join.** `node tools/harness/snap-report.ts` on the IP log, then `node tools/indirect-live-vs-static.ts <IP log> --pairs --unfired --sites native/ts3native/vcallsites2.h` against the CSV after §8: 1,049 sites, 1,559 pairs; match 313, resolves 252, family 981, hint 4, **new 9 (6 sites), no-row 0**. Against the nine logs of §8.1 (targets in MSVCR80 compared by module and low 16 bits, because the DLL base moves per run: `0x7004D25F` here, `0x715CD25F` in BP), the IP job has 85 pairs (50 sites) that no BP job saw. 37 of them also came from `ip-snap2-houseboat` and are classed in §8.3. **48 pairs (30 sites) are new to this job**: 9 new, 26 family, 11 resolves, 2 match.

**Classes of the 48 pairs.** Inside or outside as §8.2: the site's function is in the live-extended closure of `Route_PlanLocked` (with the IP edges 2,994 functions, 365 more than the 2,629 of §8.2), and its first indirect edge is not a shared helper of §8.4 [verified per function, inferred per hit; the probe logs no thread].

| Class | Site function (sites) | Pairs | Live targets | Evidence | State |
|---|---|---|---|---|---|
| inside | `RouteObstructionAtPoint 0x655880` (`0x655B41`, `0x655BAA`, `0x655BB8`, `0x655D8E`, `0x655DA3`, `0x655DC1`, `0x655DFB`, `0x655E09`, `0x655E1A`, `0x655EA0`) and its callee `0x62F470` (`0x62F4F2`) | 12 | `0x80F840`, `0x80C300`, `0x807090` (kind tests), `FootprintMember_FwdSlotC 0x626220`, `Footprint_GetTypeByte 0x624450`, `Footprint_GetOwnerIdAlias 0x620CC0`, `Footprint_GetOwnerId 0x806FE0`, `RefCounted_AddRefAt8 0x915880`, `RefCounted_ReleaseAt8 0x915890` | direct closure, hop 0 under `PathPlanner_PlanHierarchical` | read only; footprint refcount (docs/106 §4 A) |
| inside | `LotQuadTree_CollectLeaf 0x669850` (`0x669982`, `0x6699C6`, `0x6699D4`) | 7 | `0x807B70`, `0x624450`, `0x626220`, `Stub_Return10 0x70AE20` | direct closure | read only |
| inside | `QuadTreeCellPos_C 0x66B090` and its switch cases (`0x66B383`, `0x66B395`, `0x66B3C5`, `0x66B3E3`, `0x66B412`) | 8 | `0x626220`, `FootprintMember_GetOwnerIdAlias 0x6262D0`, `FootprintMember_FwdSlot0 0x626210`, `Stub_Return3 0x83F5F0`, `0x806BF0`, `0x807B70`, `0x80C300`, case `0x66B440` | direct closure (cases at hop 1 through the switch tables `0x66B5B4`/`0x66B5CC`) | read only; case `0x66B440` writes only the cell cursor's own fields `+0x10`, `+0x1C`, `+0x20` |
| inside | `QuadTreeCellAStar` (`0x61E78B`), `WorldAStar_Search` (`0x6768D4`) | 2 | `0x7EAEA0` (return 1), `RouteCost_Mode4 0x675800` | direct closure | read only |
| inside | `0x9DC510` (`0x9DC529`, refcounted pointer assign under `LotRouting_QuadTreeBuild`) | 1 | `RefCounted_AddRefAt8` | direct closure | the footprint's own count |
| outside: object events behind the job release path | `GlobalHandlerChain 0x936C90` (`0x936CB5`) | 8 | handlers `0x9439C0`, `FootprintComp_Event0EHandler 0x947660`, `0x9499E0`, `0x94DE50` (physics transform), `0x953030`, `0x959200`, `0x95E1B0`, `0xC8B2A0` | the only path from `Route_PlanLocked` is the `Job_Cancel` callback `0x59A603 -> 0x950DA0` (an object job: `Object_GetComponent`, `PostRenderMessage`), which a route job never runs (docs/112 §4); pass-2 root `RouteFollow_Update` (depth 5); the object event chain of docs/76 and docs/50 | sim-thread writers (below) |
| outside: object creation | `Object_Allocate 0x9361D0` (`0x93632A`) | 3 | `AudioComponent_ctor 0x943CE0`, `EffectComponent_ctor 0x945370`, `0xC89FC0` | not reached from `Route_PlanLocked` with every live edge of the ten logs; pass-2 root `RouteFollow_Update`; component ctors of the create path (docs/50) | not search state |
| outside: route following | `0x95A0E0`, `0x95A360`, `0x95B460` (`0x95A140`, `0x95A6A7`, `0x95A779`, `0x95B4BA`) | 4 | `0x951370`, `0x951310` (slots `+0x74`/`+0x78` and `+0x8`/`+0xC` of component vtable `0x10440A8`) | not reached; pass-2 root `RouteFollow_Update` (depth 4-6); BP fired `0x951350`/`0x951330` at the same sites (§8.3) | not search state |
| outside: shared helper (§8.4) | `0x8A65D0` (`0x8A65EB`) | 1 | `0x7DBC30` (UI resource release, vtable `0x101A9D8`) | the target §8.4 lists for `0x5BF1FB`; the class is built only by `0x7DBC80`, outside the closure | not search state |
| outside: shared helper (§8.4) | `0x731D40` (`0x731DA1`) | 2 | `0x5BD160`, `0x6D8EA0` (deleting destructors of vtables `0xFE26E8`, `0xFF66DC`) | the site is reached only through `0x8A65EB`; no constructor of either class is in the closure (`0xFE26E8` is stored by `0x5BD0C0`, outside) | not search state |

Inside: 30 pairs; every target is a routing-family slot, a folded constant or a world cost mode, and all but case `0x66B440` were already in the §8.2 closure [verified]. Outside: 18 pairs, among them all 9 new ones.

**State not already listed.** Of the 365 functions the IP edges add to the closure, 362 hang off `0x936CB5` (236 through `0xC8B2A0`, 75 through `FootprintComp_Event0EHandler`, 19 through `0x94DE50`, the rest through the other five handlers), 5 off `0x731DA1`, and 1 is case `0x66B440`. Their 6 global stores: `0x1157D28` (`0x5ECB30` under footprint slot `0x80E200`; docs/101 §2.2 #11), `0x11587F8`/`0x1158808` (physics loose quadtree free lists, `LooseQuadTree_Unlink 0x6813F0` and `0x6811D0` under `PhysicsComponent_SetTransform`: the N8 mutators of docs/94 §17 and docs/33), and `0x11EB5A0` (a free-list head under the lock at `0x11EB5B0`, `0xAE00A0`; `docs/data/ic-safety-a.csv`). All are on outside paths; none is new search state. No new managed exit, and no CRT or Mono site inside.

**Verdict for IP.** The §8.6 verdict holds for IP: the IP plans (water, boats, lots) execute nothing outside the static map, and every IP target inside the search writes only state already listed. After the 6 row updates the join over all ten logs shows **0 new and 0 no-row pairs** (1,212 sites, 1,971 pairs: match 375, family 1,212, resolves 379, hint 5). Gap 3 of §8.6 is closed (I-109-26, I-109-28).

**G2 gaps left:**

1. The class-I static `0x11D9020..0x11D9060` (§8.5): add it to docs/101 §2.2 as #27 and to the warm-up check. ~0.5 h, no game run. **Done 2026-10-19** (§8.8.1).
2. Shared-helper attribution, now 4 sites: `0x8A65EB` (one more non-routing target here, `0x7DBC30`), `0x5BF1FB`, `0x8CE574` and `0x9158AB`, plus the sites reached only through them (now also `0x731DA1`). The job release sites `0x59A688`, `0x59A603` and `0x59974F` drop out: `RoutingJob_dtor` calls `Job_Release` only for a non-zero handle `+0x10`, which the ctor zeroes (docs/112 §4), so the whole release path is dead for a route job and their live hits belong to other jobs (I-109-29). What is left is the worker-only pass of §8.6: ~1 h build, then one 10-minute BP job (game run). If a non-routing target fires on the worker, its §8.5 writes become search state (I-109-30).

### 8.8 G2 gaps 1 and 2: warm-up item #27 and probe pass 3 (built, not run)

Written 2026-10-19. Code built and self-tested; no game run.

#### 8.8.1 Gap 1 closed: docs/101 §2.2 #27

`0x11D9020..0x11D905F` with guard dword `0x11D9060` is now docs/101 §2.2 row #27 (class I). The guard is bit 0: `test` against `eax = 1` at `0x80D8CE`, `or` at `0x80D8E5` [verified]. The `or` comes **before** the four stores (I-109-31), so a thread that sees the guard set while another thread runs the first call would read a matrix that is not written yet. The bytes never change, so it needs no redirect and no copy, only warm-up: the P4 cold check (`P4Cold` in `native/ts3native/p4.cpp`, docs/94 §12.3.2) now also refuses a clone run (`cold`) while bit 0 of `0x11D9060` is clear. Selftest: P4 part A refuses `cold` with the #27 guard at 2 (bit 0 clear). The `[ProbeValues]` guard print (`pvGuard`, counters v44) is unchanged: it still prints #12 and #14 only.

#### 8.8.2 Gap 2: probe pass 3 (worker-only attribution of the four shared-helper sites)

Question: do `0x8A65EB`, `0x5BF1FB`, `0x8CE574` and `0x9158AB` fire on the route worker during a route job, and with which targets (I-109-22, I-109-32)?

Pass 2 logged no thread. The pass-2 logs show all four sites spend their 16-hit budget in most windows, so a worker-only answer needs both a thread tag and a budget the sim thread cannot use up.

**Build.** `[ProbeSnap] Threads=1` (`native/ts3native/vcall.cpp`, ts3native 0.29.0, counters unchanged at v53, no registry slot). Every (site, target) pair also counts its hits per thread class: `job` = the route worker (`g_workerTid`) inside a plan (`g_callerTid` set or an async job running, the existing `ForCaller()` test), `worker` = the route worker between plans, `sim` = the game's sim thread (`0x11D8A8C`, as `[RouteGate]`), `other` = any other thread. The log line gains ` thr=job:N,worker:N,sim:N,other:N` at the end. Only `job` hits spend `PerWindow`; other hits disarm a site after 16 x `PerWindow` (a fixed share, so a busy sim thread cannot keep the int3 armed for long). With `Threads=0` (default) the sampler and its log lines are unchanged. The four-site table is `native/ts3native/vcallsites3.h` (rows copied from `vcallsites2.h`, all `call edx`), chosen by building with `/DTS3N_VCSITES3`; the shipped build still includes `vcallsites.h`. `tools/harness/snap-report.ts` reads the `thr=` split, adds it across logs and prints it per pair.

Build steps (done in the agent worktree; Rodney parks the result):

1. `set CL=/DTS3N_VCSITES3` then `native\ts3native\build.cmd out\probe3` (selftest runs as usual); clear `CL` afterwards.
2. Copy `native\ts3native\out\probe3\ts3native.asi` to `game-copy\ts3native.asi.snapprobe3` in the main checkout.
3. `node tools/harness/queue.ts tools/harness/queues/snap-probe3.json --dry-run`: the `MISSING` notice must be gone.

**Job.** `bp-snap-probe3-worker` in `tools/harness/queues/snap-probe3.json`: Baseline-BP-Spring, 10-minute profile, seed 12345, `RouteAsync.Enabled=1`, `Mode=1`, `RouteGate` and `Discourage` as `bp-snap-probe2`, `ProbeSnap.Enabled=1`, `ProbeSnap.Threads=1`, `minCounters` 53. One game run (~15 min with load).

**Run pass rule.** No crash; `vcReady` 1, `vcSites` 4, `vcFaults` 0, `vcSkipped` 0; machine load ok; at least one window with a `job` hit at each of the four sites (otherwise that site is **not answered**: rerun with `repro: rt-s2b-sweep` added, as `bp-snap2-objects`, so lot graphs are rebuilt during plans).

**How to read.** `node tools/harness/snap-report.ts <row.nativeLog> --all`, then per site:

- every target with `job > 0` must be a routing target: at `0x8A65EB` `vt_LotQuadTree` Release `0x662E00`, at `0x9158AB` the footprint releases `0x626320`, `0x624E20`, `0x624E70`. Pass 2 also fired `0x64E8F0` (at `0x5BF1FB`, `0x8A65EB`, `0x8CE574`) and `0x5B07A0` (at `0x5BF1FB`, `0x8A65EB`), which have no symbol row and are not in the §8.4 lists; if either shows `job > 0`, read its body as §8.3 before the verdict;
- a §8.4 non-routing target with `job > 0` (`0x7DBC30`, `0x731D40`, `0x92C5F0`, `0x8BC100`, `0x8FD860`, `0x8E8DD0`, `0x8DD3D0`, `0x8A7160`, `0x9106E0`) fails I-109-22 for that target: its §8.5 writes become search state, and the target goes into docs/101 §2.2 or §2.3 with a redirect plan;
- non-routing targets with only `sim`/`other` hits confirm the §8.4 class evidence (I-109-22 verified for that site);
- `worker` hits (between plans) are not search work and do not count either way.

If all four sites pass, G2 has no open gap left (docs/116).

### 8.9 Probe pass 3 result and G2 verdict

Written 2026-10-19 from `bp-snap-probe3-worker` (Baseline-BP-Spring, 2026-10-18 21:34-21:44 UTC = 2026-10-19 05:34-05:44 PHT, ts3native 0.29.0 built with `/DTS3N_VCSITES3`, settings of §8.8.2). No game run and no code change for this section. Static reads with capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67), `functions.csv` bounds and a direct-call closure of `Route_PlanLocked 0x82C6E0` (E8 calls and outgoing E9, 12 hops, stopped at `Lock_Acquire`/`Lock_Release`/`EA_AllocTagged`: 1,295 functions, as §8.5); `native/ts3native/replay.cpp` for the clone side. Ledger: docs/51 §91 (I-109-33 to I-109-39).

**Run.** PASS on the run rules of §8.8.2 except one site: game alive, 20 windows, 14,120 hits, 17 pairs, `faults 0`, `skipped 0`, machine load ok (other CPU 3.4%, clock at least 180%), 0 stuck Sims. `job` hits came in 20 of 20 windows at `0x5BF1FB`, 19 at `0x8A65EB` and 19 at `0x9158AB`. **`0x8CE574` had no `job` hit in any window** (4,352 hits, all `other`), so by the §8.8.2 rule it is not answered live; it is closed statically below. `worker` (route worker between plans) is 0 for every pair. Counts are capped by the window budget (16 `job` hits per site per 30 s), so they are not rates.

**Per pair** (`node tools/harness/snap-report.ts --all <log>`; bodies [verified, disassembly]; class as §8.3):

| Site | Target | job / worker / sim / other | Body | Class | Verdict |
|---|---|---|---|---|---|
| `0x5BF1FB` (`0x5BF1E0`, release every element of a pointer vector) | `0x64E8F0` | 268 / 0 / 1,953 / 0 | portal Release, slot `+4` of the portal vtable `0xFED048` (docs/92 §1): plain load, decrement and store of the portal's count `+0x70`; at a count <= 1 it calls slot `+8` (deleting dtor `0x64F1A0`: frees the variation vector `+0x5C` and the portal, both on the locked EA heap). No global | **inside the search, routing class** (portal). Writes the **live portal's reference count** | not in the snapshot design (H20 below) |
| `0x5BF1FB` | `0x7DBC30` (UI resource release, vtable `0x101A9D8`) | 0 / 0 / 0 / 98 | as §8.4 | outside | I-109-22 verified for this pair |
| `0x5BF1FB` | `RefCounted_AtomicRelease 0x692720` | 0 / 0 / 0 / 1 | render and resource objects (§8.4) | outside | other thread only |
| `0x8A65EB` (`0x8A65D0`, the same loop over a range) | `0x64E8F0` | 286 / 0 / 2,252 / 0 | portal Release as above | **inside, routing class** (portal) | as the first row (H20) |
| `0x8A65EB` | `0x924180`, `0x92C5F0`, `0x7DBC30` | 0 / 0 / 0 / 10, 3, 1 | Swarm and UI classes (§8.4) | outside | I-109-22 verified |
| `0x8CE574` (`0x8CE540`, assign a range of counted pointers) | `0x8BC100` (20 vtables built by `0x86DF20`), `RefCounted_ReleaseAt8 0x915890` | 0 / 0 / 0 / 4,129 and 223 | as §8.4 | outside | site not answered live; **closed statically** (below) |
| `0x9158AB` (`RefCounted_ReleaseAt8`: slot 0 of the secondary vtable when the count reaches 0) | `0x626320` | 16 / 0 / 838 / 0 | this-adjust thunk to `0x626330`: deleting dtor of a footprint member block (secondary vtable `0xFEBF6C`); returns the block to pool #5 `0x1165B80` under lock `0x11D8F90` (`0x807050`) | inside, footprint lifetime | covered: docs/101 §2.2 #5 (locked); clone footprints are never deleted (below) |
| `0x9158AB` | `0x624F20` | 8 / 0 / 0 / 0 | thunk to `0x625050`: deleting dtor of the one-level cached classes `0xFEB5F8` and `0xFEB720` (secondary vtables `0xFEB5D4`, `0xFEB6F8`); runs the middle dtor `0x80F3D0` (level polygon and vertex arrays to the EA heap, the one-level block to pool #6 `0x1165D48` under lock `0x11D9068`) and the base dtor `0x807AA0` (child list `+0x24`: release each child, free the list nodes; the lock-key set at `+0x20`: plain decrement of its count `+4`, delete at 0), then the EA free | inside, footprint lifetime | covered: #6 locked, EA heap locked, `fp+0x20` private in the clone (docs/94 §21.1) |
| `0x9158AB` | `0x80EF70` | 8 / 0 / 0 / 0 | thunk to `0x80F660`: deleting dtor of the middle class `0x1020AE8` itself (the lot-edge and polygon footprints of docs/94 §16.8); same chain as `0x624F20` | inside, footprint lifetime | covered, as `0x624F20` |
| `0x9158AB` | `0x624E20`, `0x624E70` | 0 / 0 / 743, 18 / 0 | footprint deleting dtors (§8.3) | inside | sim only in this run |
| `0x9158AB` | `0x8FD860`, `0x8DD3D0`, `0x8E8DD0` | 0 / 0 / 0 / 2,829, 215, 209 | Swarm VFX classes (below) | outside | I-109-22 verified |

`0x5B07A0` (§8.8.2) did not fire; `0x8A7160` and `0x9106E0` (§8.4) did not fire in this run.

**No §8.4 non-routing target fired on the route worker.** Every `job` hit is a routing class: portal Release and three footprint deleting destructors. I-109-22 is **verified** for `0x5BF1FB`, `0x8A65EB` and `0x9158AB` (I-109-33). The §8.5 writes of the non-routing classes stay outside the search.

**`0x8CE574` closed statically [verified].** The closure reaches `0x8CE540` only through the lazy builds: `0x6290F4` (in `0x6290E0`, under `0x62A860` <- W3 `0x62F8A0`) and `0x621E30`/`0x621E6F` (`0x621DB0`, under `0x6230F0` <- `0x6234D0` <- W4 `LotGraph_Build 0x63CB90`). With W3, W4 and `ApplyPendingUpdates 0x82C420` as extra stops the closure drops to 1,043 functions, and `0x8CE540` and `0x621DB0` leave it (I-109-34). A P4 clone run builds no lot (prebuild at load, the cut's first use, D4 tripwire: docs/94 §16.1, §17.4), so this site is not clone-run code. No rerun with `rt-s2b-sweep` is needed for G2.

**Which search code releases portals on the worker** [paths verified; per-hit attribution inferred, the probe logs no caller; I-109-35]. With the lazy builds and pending updates cut off (the 1,043-function closure), three paths remain, all in the lot-level search:

1. **Room record copy** in `0x633FA0` (under `LotSearch_Entry 0x640E00`, call at `0x640F29`): copies one 0x44-byte room record of `graph+0x90` by value (`0x6223C0`; its two pointer vectors `+4` and `+0x14` are copied with an AddRef per element through `0x9EC420`, site `0x9EC452`) and destroys the copy with `0x621D20` (a Release per element through `0x5BF1E0`). The release fires `0x5BF1FB`.
2. **`LotPath_AddPortalNodes 0x62FBE0`**: pushes portals into a local vector (`0xD53DB0` -> `0x8B70E0`; a grow AddRefs into a new EA block, releases the old elements through `0x8A65D0`, which fires `0x8A65EB`, and EA-frees the old block at `0x8B7205`), and releases the vector at the end (`0x62FD62`, `0x63040F`; fires `0x5BF1FB`).
3. **Quadtree unpin** (H13, docs/101 §2.3): `0x63C5B0` -> `0x63AF30` -> `0x639A80` moves the lot graph's pending portal adds `+0x54` into the portal list `+0x44` (AddRef per element at `0x639ABE`; push and grow through `0x8B70E0`), releases `+0x54` (`0x639AEB`) and applies the pending removes `+0x64` through `0x635EE0`. `+0x54` is filled only while the planner's pin count `P+0xC8` is non-zero (`LotRouting_AddPortal 0x6379E0`, branch at `0x637BAF`), that is, by a portal IC that ran while a plan had the lot pinned (I-109-37).

**Footprint destructors on the worker** [path inferred, bodies verified; I-109-38]. A footprint is deleted on the worker only when its count reaches 0 there: the plan's own `ApplyPendingUpdates` and the unpin's pending removes (H12, H13) drop the routing world's last reference to a footprint the sim removed. Nothing else holds a counted reference, so there is no docs/88 lifetime question: docs/88 design G is about objects that a job's `FindById` pointers reach, and these blocks have no holder left. The writes are the locked pools #5 and #6, the locked EA heap, the footprint's own child list and the count of its lock-key set `fp+0x20` (a set on the object's footprint component, docs/101 §6; the plain decrement races the unlocked lock-key ICs, which is vanilla async behaviour, not something the snapshot adds). **In a P4 clone run these destructors cannot run on clone footprints:** the clone's footprints carry `RP_BIAS` in their count (docs/94 §16.1), their `fp+0x20` points to private set copies (§21.1), and the cut runs `ApplyPendingUpdates` and the flushes on the sim before the clone run (§21.1 `cut`), so the clone run has no pending footprint removes.

*Note 2026-10-19 (docs/117 §6.1, I-117-6):* the rare-scenario logs add a fourth footprint dtor behind `0x9158AB`, `0x620E10` (thunk to `0x620E20`), the deleting dtor of the **box footprint `0xFEAE08`** (secondary vtable `0xFEADE0`). Its writes are a subset of the list above: the box vtable stores, the base dtor `0x807AA0` (child list `+0x24`, lock-key set count) and the EA free; no pool work, since the box class has no level cache [verified disassembly]. The box class is in the replay class table, so its clone copies carry `RP_BIAS` like the others and the same "never on clone footprints" argument holds. Nothing new for docs/101 §2.3 or P4.

**The "other" thread** [inferred; the probe records the thread class, not the thread; I-109-39]: every `other` target is a Swarm VFX or render class. `0x8FD860` (vtable slot at `0x103EDCC`) and `0x8E8DD0` (`0x103E14C`) belong to classes built by `0x8FD2C0` and `0x8E8860`, whose allocator tags are `Swarm/ParticlesDesc/SurfaceRecs`, `.../StateData`, `.../PathPoints`, `Swarm/PendingDescriptions` and `Swarm/MetaParticlesEffects`; the vtable of `0x8DD3D0` (`0x103B980`) is stored by 21 functions in the same Swarm range `0x8C4600`-`0x930400`; `0x924180` and `0x92C5F0` lie in it too; `0x7DBC30` is `UIResource_ReleaseD3D`, and `RefCounted_AtomicRelease 0x692720` releases render and resource objects (§8.4). docs/44 §1 places Swarm and the render blocks on the main (render) thread, so `other` is the main thread. `0x8BC100`'s family (20 vtables built by `0x86DF20`) is unnamed; it fired only there.

**New shared state: live portal reference counts (H20)** [verified source and disassembly; consequences inferred; I-109-36]. Under option G the clone does not own the portals:

- `replay.cpp` copies the lot graph vectors `+0x44`, `+0x54`, `+0x64`, `+0x74` and the room records `+0x90` with their `+0x24` and `+0x34` vectors into private buffers, but the elements of `+0x44`/`+0x54` are raw pointers to **live** portal objects with **no reference taken**, and the room records' `+4` and `+0x14` portal vectors are **not copied**: their buffers are the live room record's.
- Paths 1 and 2 run in every clone-run lot search. They AddRef and Release live portals (plain read-modify-write on `+0x70`) from the worker while the clone run does not hold the live `+0x918`, and they read the live room buffers. The sim does the same on the same portals (`PortalsForObject 0x62F760` and the sim's own plans: the `sim` counts above), and the portal ICs (`AddPortal`, `RemovePortals`) run on the sim without `+0x918` (docs/101 §6). Results: a lost update leaks a portal or deletes it early later on the sim (a crash at a later use); a `RemovePortals` or lot graph rebuild on the sim during the clone window frees a portal or a room buffer the clone still points to (use after free on the worker).
- Path 3 is a no-op in a clone run while the clone's `+0x54` and `+0x64` are empty. If they were not, the grow in `0x8B70E0` would EA-free a buffer that `RpAlloc` took from our private heap (heap corruption). The same holds for a lazy `LotGraph_Build` in a clone run (D4 lets it run "on the copy"; its pushes grow `RpAlloc` vectors the same way).
- docs/101 §2.3 had no row for this write; it is now H20. It is not a global, so docs/101 §2.2 and the warm-up check do not change.

**G2 verdict: PASS.** Everything the route search executed on the worker in pass 3 is inside the static map (routing classes only, all with CSV rows), the non-routing targets of the four shared-helper sites never ran on the job thread, `0x8CE574` is lazy-build-only code, and no new global appeared. G2 has no open gap left. What pass 3 adds is one P4 design item for the heap write set, not a G2 gap:

| Item | Belongs in | What | Effort |
|---|---|---|---|
| P4-H20a: interlocked portal counts while P4 is armed | docs/94 §21 (P4 part B: the §21.2 private/live table and §21.6 risks) | with `[RouteSnap] Mode>=1`, swap portal vtable `0xFED048` slots 0/`+4` to the interlocked `PortalAddRefX`/`PortalReleaseX` pair that design SH already has (`slotro.cpp`, `lotfd.cpp`; equal to the game's pair offline, I-90-4) | ~0.5 h |
| P4-H20b: the clone owns its portal references | docs/94 §21 (replay clone, `RpLotShell`/`RpLotFree`) | copy room `+4`/`+0x14` into private buffers; at clone build and rebase (sim, under `+0x918`) AddRef each portal in the cloned `+0x44`, `+0x54` and room `+4`/`+0x14`, and Release them when the lot copy is freed. A sim-side portal removal then cannot delete a portal the clone holds (the docs/88 design G "park a reference" rule applied to portals) | ~1-1.5 h + selftest |
| P4-H20c: no game resize of a private vector | docs/94 §17.4 (D4) and §21.1 (`cut`) | refuse or rebase a lot whose clone `+0x54`/`+0x64` is not empty at the cut (expected never, I-109-37); D4: a lazy build in a clone run sets `abort = lazy` **before** the build runs instead of letting it write the copy | ~0.5 h |
| P4-H20d: portal fields read live | docs/94 §17 (non-routing reads, D5) | the clone run also reads live portal fields (cost `+0x2C`, flags, lock word) that the unlocked portal ICs write on the sim; add the portal IC family to D5's window counters (M5 class), unless H20b copies the portals as plain records (docs/92 §1) instead of referencing them | ~0.5 h |

Total ~2.5-3 h, no game run of its own (it rides on the P4 shadow jobs of docs/94 §21.4). **Built 2026-10-19 in ts3native 0.30.0 (counters v54), offline checks only: docs/94 §21.8** (`+0x64` holds 8-byte ids, not pointers; H20d needed no new hook, the `[RouteSnap]` IC slot counts 4-14 are the family).
