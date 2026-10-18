# 109. Routing follow-ups: search flag byte, option bit, probe pass-2 roots

Written 2026-10-18. Static and offline only: the game, the probe server and the test queues were not run, and no ts3native source was changed or built. This closes the follow-ups docs/106 left for PLAN "Routing off-thread" item 6 (no gaps in our knowledge of the routing code): the two gates that kept 65 armed sites silent (§2, §3), the three unnamed footprint interface vtables (§4), the site table for probe pass 2 (§5) and a repro plan for the sites that did not fire (§6).

Tools: capstone 5 over `game-copy/bin/TS3W.exe` (Steam 1.67) and `game-copy/analysis/functions.csv`; `tools/managed-callgraph.ts` for one managed caller; scratch scripts in the session scratchpad (`io-x2/`). Ghidra was not needed. Tags as in AGENTS.md: **[verified]** = read in the disassembly (address given); **[inferred]** = reasoning, with a ledger row in docs/51 §45.

## 1. Summary

- **The "search byte `+0x1C`" is not a field: it is the sixth stack argument of `QuadTreeCellAStar 0x61E500`** (`[ebp+0x1C]`, thiscall, `ret 0x18`), the stamp flag docs/98 §8 already describes. Both callers pass 0 and nothing else reaches the function, so **`AStarSearch_StampReader 0x61C9B0` is unreachable in 1.67** and its 53 sites can never fire [verified] (§2).
- **Context bit `0x20` (`X+0x50`, gates `QuadLeaf_CountBlockingTypes 0x669530`) is never set in 1.67** [verified for every context builder found]. It is the docs/98 §3.5 "type surcharge" flag (+50 per extra blocking type in a penalised cell). The route job's flag word, the retry and obstacle-replan words and all seven `LotPlanCtx_Init` contexts were read; none contains `0x20` (§3). The same trace answers most of docs/98 U6 (which plans set which context bits).
- **The three unnamed interface vtables** belong to the private footprint (`0xFEBB98` of `vt_PrivateFootprint 0xFEBBC0`), the lot floor footprint (`0xFEBCE8` of `vt_LotFloorFootprint 0xFEBD10`) and the lot wall footprint (`0xFEBE10` of `vt_LotWallFootprint 0xFEBE38`) [verified ctors and alloc tags]. Correction to docs/106 §4: of the 9 interface slots, 5 are plain `this-4` thunks and 4 are shared functions that subtract 4 themselves (§4).
- **Probe pass 2 is prepared, not built.** `tools/snap-vcall-sites.py` takes `--extra-roots`, `--arm-thunk-prologues`, `--list` and `--base`; with no new option it writes the shipped table byte for byte (checked). The pass-2 table `native/ts3native/vcallsites2.h` has **1,534 sites** (all 1,182 of pass 1 plus 352 new) from 121 roots (the 51 built-in, 69 of the 71 docs/106 targets, two of which are already roots, plus the not-armed water grid builder). Rebuild steps in §5.3; draft jobs in `tools/harness/queues/snap-probe2.json` (dry run checked, not run).
- **Repro plan (§6):** most unfired groups map to existing repros (`rt-s2b-sweep`, `verify-v22`, `pet-guard-cancel`, `ip-portal-leak`, the save-matrix travel case). Three steps are missing from `mods/ts3agent`: a build-floor level switch, the wall/stair tool ids for `tool_select`, and a repro that routes a Sim to an object upstairs (the commands for that one exist).

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
| **A. Lot-table miss and lot unload**: `0x62DE40` (`0x62DF97`, `0x62DFB1`), `LotGraph_dtor` (`0x62B9D8`, `0x62B9EC`) | miss branch of `LotTable_GetOrInsert 0x62EAB0` (copy at `0x62EAFB`, two temporaries freed at `0x62EB1F`/`0x62EB2B`); also `LotTable_Erase` from `LotGraph_Reset 0x62E420`, and `0x62D610` from `0x635B70`/`0x637520`. A miss happens only for a lot not yet in the table (docs/80 §2: load-time inserts), so in a running world it needs a new world or a new lot | save-matrix travel case (`run.matrix ["--travel","--cases","travel","--to",...]`, as `tools/harness/queues/travel-all.json`; job `bp-snap2-travel`); agent `travel_vacation`/`travel_home`; `tools/harness/travel-trip.ts` | none for travel. For a lot added in the same world: a houseboat create on IP (`IC_World_CreateHouseboatLotImpl 0x792CA0`) has no agent command; a `reflect_call` on the C# wrapper of that IC would do (method not looked up) |
| **D. Lot-manager index rebuild** `0xC66700` (`0xC66714`, `0xC66755`, `0xC6676F`, `0xC66786`) | `WorldManager_RebuildLotIndex 0xC68680` runs lazily when `WM+0x1CC` is set. Setters on the world manager: `WorldManager_MarkLotIndexDirty 0xC5FEC0` (from `Lot_Load` `0xACF852`, and via `0xACEB20` from houseboat create, `World_RemoveLotCommon 0xA48CC0` and `0xD41630`) and `WorldManager_DetachLot 0xC65830` (`WorldManager_RemoveLot`). Placing or moving an object does not set it (docs/106 §7 said "place or move a lot object": not supported by the writers found) | the travel job above (every lot of the destination goes through `Lot_Load` after arming) | none for travel; houseboat create/delete or bulldoze as in A |
| **B. Object churn, build/buy exit, level polygons**: `LotQuadTree_Insert` (`0x666D88`, `0x666E04`), `LotQuadTree_ApplyInsert` (`0x665CEF`, `0x665DAD`), `LotRouting_QuadTreeBuild 0x62F9C1`, `LotRouting_FootprintRoomIds 0x6354D7`, `Footprint_EnsureLevelPolygons 0x80E5F6` | object add/remove (`0x6376D0`, `LotRouting_RemoveObjectFootprints`), lot rebuilds at build/buy exit (`RoutingWorld_UpdateLotWalls` -> `QuadTreeBuild`, `LotGraph_Build`), polygon cache rebuild on the next `RouteFollow_EditPath`/segment test. `0x666D88`/`0x666E04` are the deferred branch: an insert while a query holds the tree (`Q+0x48 > 0`), whose in-game source is the door-lock refresh `InRoom_ApplyPortalLockFootprints 0x632460` during in-room planning (docs/101 §4.4). `0x665CEF` is a re-insert that drops the last reference of an old map entry | `rt-s2b-sweep` (10 objects placed and removed twice, buy mode on/off; job `bp-snap2-objects`), `verify-v22` (build/buy enter and exit, 10 objects), `object-id-cache`; agent `place_object`, `move_object`, `remove_objects`, `build_mode` | (1) For the deferred insert: a door whose permission differs per Sim (locked for non-household), a household Sim routing through it, then a visitor planning into that room. Commands exist (`do_interaction` on the door's lock entry, `sim_visit_lot`/`go_here`); the repro entry does not. (2) For walls on a second floor and stairs: `tool_select` and `tool_drag` exist, but the tool ids for walls, floors and stairs are not recorded anywhere, and there is no command to raise the build floor level (the build UI's floor-up action; `place_object` uses terrain height). Both must be found in the UI script DLL before a build repro can be written |
| Routing over stairs (multi-level footprints during plans) | stair portals and level polygons are read when a plan crosses levels | `list_objects` (returns `level` per object) then `do_interaction` on an object with `level >= 1` for a Sim downstairs, on any BP two-storey home | a repros.json entry with those two steps (no new command) |
| Water obstruction query: `RouteObstructionAtPoint` (`0x655BC9`, `0x655E49`), `0x62F470` (`0x62F5B5`, `0x62F5DE`) | `RouteObstructionAtPoint 0x655880` is called from the hierarchical planner only on a water-grid branch (`0x6435B0` -> `0x6437AF`, after `WaterGrid_CellIndex`), and from `IC_Route_GetObstructionAtWorldOrLotPoint`, whose only managed caller is `GetInBoatRouteAction.StandAndWaitCycleHandler` (boat boarding); `0x62F470` is called from it (`0x655D55`). `0x63AFD0`, its other caller, has no caller or reference (dead) | `ip-portal-leak` (`sim_portal_trip` into the sea and onto a houseboat, IP); `ip-houseboat-gohome` | none |
| **C. Route cancelled mid-plan**: `Job_Cancel 0x59A603` | `RoutingJob_dtor` -> `Job_Release` -> `Job_Cancel` for a job released before it ran (docs/106 §5) | `pet-guard-cancel` (8 guard/cancel cycles with spawned Sims at ultra speed, built so cancels land during route jobs; job `bp-snap2-cancel`); agent `cancel_interactions` | none. With `[RouteAsync]` on, jobs are posted by our worker path; whether a cancel then still goes through the game's `Job_Cancel` is not checked [inferred, I-109-6]: if the site stays silent, rerun the job with `RouteAsync.Enabled=0` |
| Route reuse: `Route_UnlinkPaths` (`0x75732D`, `0x75733A`), `Route_PlanCommon 0x760F0A` | `Route_PlanCommon` calls `Route_UnlinkPaths` (`0x76078C`) on every plan, but these sites run only when the route already holds paths, i.e. a replan of a used route object; also the path-editing ICs (`IC_Route_InsertCustomPathAtIndex`, `IC_Route_Clone`, `IC_Route_SetLoopStart`, ...) | `bp-route-fail-active` (stranded Sim, repeated `sim_route_to`) [inferred to replan the same route object] | if it stays silent: a repro that calls `Route.Replan` on a planned route through `reflect_call` |
| `0xAC5832` (`0xAC5610`) | world object queries (`0xAC8310` from `IC_World_GetObjectsImpl`, `IC_World_GetObjectCountImpl` and 10 native callers), a branch inside | none specific | leave for pass 2 (it is armed in both tables) |
| Debug memory tracer `0x931292`, `0x93117F`, `0x9311A3` | off in release (docs/84) | none | none |

## 7. Files

- `tools/snap-vcall-sites.py`: pass-2 options (§5.1); default output unchanged.
- `docs/data/snap-pass2-roots.txt`, `docs/data/snap-pass2-sites.txt`, `native/ts3native/vcallsites2.h`: pass-2 inputs and outputs (§5.2). `vcallsites2.h` is not included by any source file.
- `tools/harness/queues/snap-probe2.json`: four draft jobs (§5.3, §6), not run.
- `tools/ghidra/symbols/ts3w-1.67.symbols.csv`: 8 names appended (§4).
- Ledger: docs/51 §45 (I-109-1 to I-109-7), with verdicts on I-106-3 and I-106-4.
