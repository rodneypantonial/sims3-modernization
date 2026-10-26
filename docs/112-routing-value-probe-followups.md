# 112. Routing value probe follow-ups: status field, R writers, context bit 0x20, Job_Cancel, BP readings

Written 2026-10-18. Static only (capstone on the Steam 1.67 `TS3W.exe` plus the logs of the first `bp-value-probe` run, docs/108); no game run and no ts3native change. Inputs: `probe-out/queue/value-probe-bp-report.txt`, the run's ts3native log and native counter JSON (both git-ignored). Ledger: docs/51 §52 (I-112-1 .. I-112-12).

## 1. Summary

| Item | Verdict |
|---|---|
| **U5, `pvStatus` 19,188 of 19,190 plans in bucket 19+** | **Probe bug** [verified]. At both `Route_PlanCommon` call sites the result struct is argument **1**; the probe reads argument 2, a pointer to a caller-local vector, and dereferences it, so it bins the vector's heap pointer (always > 18). The JSON confirms it: 100 % of both histograms is in bucket 19, nothing in any other bucket. No other `pv*` reading shares the mistake (§2.3). docs/101 §4.2 stands (static store scan); U5 is still open live until the fixed probe runs |
| **R changed after load** | Two separate writers, neither a probe bug. (a) The sum-A change (sum B equal) is a **type-bit rewrite**, not the blocked bit: sum A masks bit 7 out, so the blocked bit cannot move it. The G build at load (`WaterGrid_BuildShorelineMap 0x66FD00`, docs/100 §3.2 step 8) rewrites types `0x10`->`0x13` and `0x02`->`0x15` in R's flag byte, and in this run it ran **after** the probe's snapshot [verified code, log order]. (b) Blocked 0 -> 284: the bit has exactly **two writers**, `RoutingWorld_ApplyLotUpdate 0x82B590` and a new one, `RoutingWorld_RestoreLotBlockedBits 0x82A190`; `0x832890` only reads it (docs/99 §1.7 and docs/91 §1 are wrong there) [verified]. 284 bits appear lazily as lots get their first plan (257 `LotGraph_Build`, 256 `ApplyLotUpdate` in the window) [inferred from the counts] |
| docs/76 events cover the blocked bit? | **Partly.** Build/buy exit and lot add/remove are covered; lot load (routing message `0xF252DC33`, sent by `Lot_Load`) is covered through event `0x10` from the same function; lazy first-use builds, the script IC `World.LotSetEntranceByTargetPoint`, lot saves and a water-plan lot rebuild are not. Recommendation: hook the two leaf writers, not events (§3.4) |
| **I-109-2** (contexts built without `LotPlanCtx_Init`) | **Closed** [verified]: 13 functions build a lot search context (Init plus 12 inline copies, found by the `+0x38` member constant `0xFEC0C4`); every one stores `X+0x50` in {0, `0x100`, `0x10000`, `0x110000`, job word}; no instruction in `.text` stores or ORs an immediate with bit `0x20` into a `[reg+0x50]` of a routing object; the other cell callers pass no context (0) |
| **I-109-6** (async bypasses `Job_Cancel`?) | **Premise wrong** [verified]: no route path, sync or async, reaches the callback at `0x59A603`. `RoutingJob_dtor` releases a job handle that the ctor zeroes, and the destroy path calls `Job_Cancel(job, 0)`, which skips the callback. Route cancels are our job table's (`DropJob`), never the game's. Drop repro C of docs/109 §6 |
| I-105-3, 1,278 display-level calls "inside" CanSee | **Report label wrong** [verified]: `pvCanSeeRaise` counts every call at `0x789EB5`/`0x78A218`, from any `World_CanSeeObject` caller (script IC too), not only inside the goal-building frame. It is a write (lot `+0x39C`, level shape bit `0x10000`) in goal building, not in the search proper; it runs on whichever thread runs `Route_PlanCommon`, the worker for async `Route.Plan` jobs [inferred from the async design]. At most 639 raise/restore pairs in 10 min |
| `RoomGraph_FirstUse` 444k off-sim | **Entry count, not builds** [verified]: the function returns the room's built byte at once when the room exists; only unbuilt rooms build. Worker entries are inside the job's `+0x918` hold, sim entries go through the route gate stub at `0x63D2F0` (Enforce=1 in this run). No race found [inferred from the gate design, docs/60, docs/68] |
| `EnsureLevelPolygons` 5,651 (2,864 off sim, 0 in plan) | **Entry count, not rebuilds** [verified]: a valid level returns after one byte test. Worker entries are outside `RoutingJob_Execute` (goal building in the IC part of the job), so inside the worker's `+0x918` hold; the sim side is the known docs/82/84 case (C-A, `caSimInJob`) [inferred]. Nothing new |
| Side finding | The load-time G build ran on the **sim** thread (log: message path `0x835390`, thread class 1), not the main thread docs/100 §1 item 1 states [verified live, BP only] |

## 2. U5: the status histogram

### 2.1 What the code does

Both call sites are `thiscall` with two stack arguments and the callee ends with `ret 8` [verified]:

| Site | ecx | arg 1 (pushed last) | arg 2 |
|---|---|---|---|
| `0x764298` (Plan/Replan family) | route (`esi`) | `edi` = the IC's own out pointer (`[esp+0x68]`), returned in `eax` at `0x7642AE` | `&local` (`lea edx, [esp+0xC]`); after the call the caller reads that local and frees it (`0x76429D..0x7642A6`, `0x4E3F70`) |
| `0x7643FB` (point ICs) | route (`edi`) | `esi` = `[ebp+8]`, returned in `eax` at `0x764412` | `&local` (`lea ecx, [esp+0x30]`), freed the same way at `0x764400` |

Inside `Route_PlanCommon 0x7606E0`: every exit copies 18 dwords (`rep movsd`, `ecx` = `0x12`) from the local result at `[esp+0x78]` to `[ebp+8]` = **argument 1** and returns it; the first dword of that local is `mType` (`mov dword ptr [esp+0x78], 0xA` at `0x760882`, `0x760AC3`, `0x760C28` before the copy). Argument 2 (`[ebp+0xC]`) is read as a vector: its first dword is loaded at `0x760951`, `0x760B68`, `0x760BE1` [verified]. The 18-dword result and the default `mType` match `docs/re/native-symbols.md` (`0x7606E0` row).

### 2.2 The bug and the fix

`values.cpp` `PvEnter`, case `PV_PC_PLAN`/`PV_PC_POINT`, stores `slot[2]` (argument 2) as the frame argument; `PvLeave` reads the dword it points to. That dword is the caller-local vector's begin pointer, a heap address, so `PvBucket(type, 20)` always returns 19 [verified: 17,192 + 2,026 counts in bucket 19, 0 elsewhere, in the run's counter JSON]. `slot` there is the return-address slot of the redirected `call`, so `slot[1]` is argument 1.

The fix (for the next probe build, not applied here):

<!-- docs-code: ok -->
```text
values.cpp, PvEnter, case PV_PC_PLAN / PV_PC_POINT:
  PvPush(t, id, slot, 0, (LONG)slot[2], false);   // wrong: arg 2 = caller temp vector
->PvPush(t, id, slot, 0, (LONG)slot[1], false);   // arg 1 = the 18-dword result, mType at +0
```

Fix the trailing comment to say so. Counter layout (v44) is unchanged. `tools/harness/values-report.ts` needs no change for this; its rule ("only {1, 2, 3, 4, 10, 16, 18}") is right. A selftest dummy for this site should call through a two-argument `ret 8` callee that writes a known `mType` into argument 1 and puts a pointer in argument 2, so this mix-up fails offline.

### 2.3 The other argument reads, checked

| Reading | Probe reads | Disassembly | OK |
|---|---|---|---|
| `PV_ASTAR` opts | `slot[6]` at entry | `[esp+0x58]` after `sub esp, 0x3C` and one push = return slot + `0x18` | yes |
| `PV_LOTPLAN` flags, obstacle mode | `slot[9]`, `slot[12]` | at `0x63F7E0` the frame is `0x1C0` + 4 saved registers, so `[esp+0x1F4]` = slot + `0x24`; `[esp+0x200]` at `0x63F7D1` = slot + `0x30`; same in the early test at `0x63F719` (`0x1C0` + 1 push) | yes |
| `PV_QUERY` initialLocked | `slot[9]` | nine pushes before `0x64F4D3`; the first (`push ecx` with `sete cl`) is argument 9 | yes |
| `PV_LS_*` path tag | `slot[3]` = wrapper's return address | wrappers `0x64F550`/`0x64F570`: two pushes then the call, so the wrapper's own return address is slot + `0xC` | yes |
| `PV_LAZY4`, `PV_GB_*` | `slot[-2]` = `ecx` from `pushad` | thiscall | yes |
| `PV_IPNUM`, `PV_LS_*`, `PV_QUERY` leave | `eax` | return values | yes |

Only the status reading is wrong.

## 3. R after load

### 3.1 What the checksum saw

`PvRSum` sum A hashes the flag byte `& 0x7F`, sum B `& 0x60`; the blocked bit `0x80` is in neither, and is counted separately [verified, `values.cpp`]. The run logged: snapshot sum A `0x1CCBD509`, 0 blocked; then the G message path and the G build (688 ms, thread class 1); then "R changed: sum `0xB7D76F77` / `0xFB9E8561` (sum B unchanged), 284 blocked". Sum B unchanged with sum A changed means only bits `0x1F` (type) moved, and `n`, `bx`, `bz`, refs and edge bytes did not.

The type writer is the shoreline step of every G build: `WaterGrid_BuildShorelineMap 0x66FD00` first reverts earlier rewrites (`0x13`->`0x10`, `0x15`->`0x02`; store at `0x66FE5A`) and then rewrites shoreline waypoints (`0x10`->`0x13`, `0x02`->`0x15`; store at `0x670310`), keeping bits `0x20`-`0x80` [verified]. The XORs of these pairs (`0x03`, `0x17`) lie inside `0x1F`, as the sums require. Callers: `WaterGrid_BuildShorelines 0x672440` from `WaterGrid_BuildFromWorld 0x82A030` and `RoutingWorld_BuildWorldRoutingData 0x8349C0` [verified]. So the probe's snapshot is pre-G-build R (good for the F-2 compare with the decoder), and the one "R change" is the load's own G build, not play.

**Probe consequences.** `pvR[4]` counts only sum-A changes, so it cannot see blocked-bit flips that keep the count (a lot unblocked while another is blocked). Next probe build: take the I-91 §6.6 baseline after the first G build (re-snapshot when `pvGBuild` changes), and add a third hash over `(chunk, record, flags & 0x80)`.

### 3.2 Writers of the blocked bit

Image scan of every `or`/`and`/`xor`/`mov` of `0x80`/`0x7F` into `byte ptr [reg+1]` (capstone, skipdata) [verified for these instruction forms]: 9 hits in 5 functions; three (`0x5D1010`, `0x7162F0`, `0x7169C8`) store constant bytes in unrelated code. The two R writers:

| Writer | What it does [verified] | Callers [verified] | Thread, lock, rate |
|---|---|---|---|
| `RoutingWorld_ApplyLotUpdate 0x82B590` (thiscall world, lot) | walks the lot's edge waypoint list and sets or clears bit 7 (`0x82B6C8`, `0x82B6ED`, `0x82B6F3`, `0x82B7BF`), mirrors it into portal word bit 29 (docs/91 §1) | `RoutingWorld_ApplyPendingUpdates 0x82C420` (every lot with a pending update when `P+0x10E` is set; callers `Route_PlanLocked` after its `+0x918` acquire at `0x82C733`, `RoutingSystem_SaveLot 0x8361B0`); `RoutingWorld_RebuildLot 0x82B810` (after `LotGraph_RebuildRooms 0x63D7B0`; callers the lot-save helper `0x63C690` from `RoutingWorld_SaveLotData 0x82C820`, `RoutingWorld_SaveLotData` itself, `WorldManager_LotRebuildCaller 0xC6BCC0` (world save), and **`WaterPath_Plan2 0x657423`** when a lot on the water path has a pending update, i.e. inside a plan); `RoutingSystem_RebuildLotRouting 0x835110` (houseboat create, lot save, lot remove, world save); `RoutingSystem_OnExitBuildBuy 0x835240`; `RoutingWorld_OnMessage 0x835300` for message `0xF252DC33` (`0x83617A`; the message is sent per lot by `Lot_Load 0xACF520` at `0xACF983`) | BP window: 256 calls, 171 off the sim thread (the async worker's `Route_PlanLocked`), 0 inside `RoutingJob_Execute` (`ApplyPendingUpdates` runs before it) [measured]. Under `+0x918` on the plan path [verified]; the others on the sim thread (docs/91) |
| **`RoutingWorld_RestoreLotBlockedBits 0x82A190`** (new name; thiscall world, lot) | finds the lot's table entry (`0x62FB60` on `world+0x95C`), walks its portal pointers `entry+0x44..+0x48`; for each portal with bit 1 of byte `+0x58`, decodes the R reference in portal word `+0x4C` (chunk = bits 0-13, waypoint = bits 14-23) and copies bit 29 into the waypoint's bit 7 (`0x82A1FA` set, `0x82A200` clear) | `LotRouting_FinishBuild 0x63CAF0` (new name), called by `LotGraph_Build 0x63D2CC` when the lot's outside room (`lot+0x16C`) is not built yet, and by `LotRouting_SetEntranceTransform 0x63D720` (new name; only caller `IC_World_LotSetEntranceByTargetPointImpl 0x788495`, script, sim); `RoutingWorld_OnMessage` message `0xF252DC33` (`0x836184`, right after the `ApplyLotUpdate` call) | lazy first use: sim or worker, inside a plan or a goal step (`LotGraph_Build` 257 calls, 192 off sim, 22 inside `RoutingJob_Execute`) [measured]; the route gate covers `LotGraph_Build` (gate site) |

Not a writer: `RoutingWorld_AddLotObjectPortals 0x832890` reads bit 7 (`movzx ecx, byte ptr [ebp+1]` at `0x832E5A`) and copies it into portal word bit 29 (`shl 0x16`, mask `0x20000000`, `0x832E6E`) [verified]. So the portal word is the stored copy, `0x832890` seeds it from R when portals are made, `0x82A190` puts it back into R when a lot's routing is (re)built, and `ApplyLotUpdate` writes both. Corrections: docs/99 §1.7 ("set and cleared only by `0x82B590` and `0x832890`") and docs/91 §1 ("same pattern in `0x832890`") should read "`0x82B590` and `0x82A190`; `0x832890` mirrors R into the portal word" (not edited here).

Whole-R writers (not bit-level): the loader `RTData_Load 0x66D110` (load, travel) and the rebuild `WorldRoutingData_LoadChunks 0x810720` from `BuildWorldRoutingData` (docs/91 row 1). Full-byte or wider stores into a record's flag byte other than these were not searched; the bit-op scan is complete only for the forms above [inferred: the remaining risk is a `mov byte ptr [reg+1], reg8` writer].

### 3.3 Why 0 -> 284

`LotGraph_Build` sets the pending flag `P+0x10E` (`0x63CC1E`, docs/94 E16); the next `Route_PlanLocked` runs `ApplyPendingUpdates`, which calls `ApplyLotUpdate` for each lot with `LotTable_LotHasPendingUpdate 0x631A60`. The window's 257 `LotGraph_Build` calls against 256 `ApplyLotUpdate` calls fit one apply per first-built lot [inferred: the probe does not record lot ids]. So the snapshot's 0 blocked was "no lot built yet", not the steady state; blocked bits change after load with every lot's first use, and later with build/buy exit, lot edits, saves and the cases above.

### 3.4 Change hooks for the docs/94 snapshot

| Change source | docs/76 hook covers it? |
|---|---|
| Build/buy exit (`OnExitBuildBuy`) | yes: IC slot `RouteManager_OnExitBuildBuy` |
| Lot added/removed, houseboat create/delete (`RebuildLotRouting`) | yes: world events `0x0D`/`0x0E`/`0x0F`/`0x10` |
| Lazy first use (`LotGraph_Build` -> `0x82A190`; pending -> `ApplyPendingUpdates`) | no (routing's own lazy work, no event) |
| `World.LotSetEntranceByTargetPoint` (script IC) | no |
| Routing message `0xF252DC33`: sent by `Lot_Load 0xACF520` (`push 0xF252DC33`, message server vtable `+0x18` at `0xACF983`), once per lot load [verified send] | yes, indirectly: `Lot_Load` also fires world event `0x10` per lot (docs/76 §2.2) [inferred: order of event and message not checked] |
| Lot save, world save (`SaveLot`, `SaveLotData`, `WorldManager_LotRebuildCaller`) | no (docs/94 E19 handles saves with `g_rtBusy`) |
| `WaterPath_Plan2` rebuild of a pending lot inside a plan | no; gone once the cut runs `ApplyPendingUpdates` first (docs/94 §3.2 step 2) [inferred] |
| G build (type bits, §3.1) | docs/100 §5 "G build epoch" |

**Recommendation** [design]: copy on change at the two leaf writers instead of events. Entry detours on `0x82B590` (`83 EC 2C 56 57`, 5 bytes, already detoured by `[ProbeValues]` `PV_LAZY3`: one owner per site, docs/68 rule 3) and `0x82A190` (`8B 44 24 04 85 C0 56`, 7 bytes, no relative operand) record the lot (argument 1) in the snapshot's dirty-lot set; at the cut the builder recopies byte `+1` of that lot's edge waypoints (the same list `ApplyLotUpdate` walks, or the portal words' R references). Plus the G build epoch for the type bits. Both writers run under `+0x918` or on the sim thread with no job running (gate), so the dirty set is never written by two threads at once [inferred].

## 4. docs/109 caveats

### 4.1 I-109-2: every lot search context

The member at context `+0x38` is the constant `0xFEC0C4` (`LotPlanCtx_Init 0x6298F0` stores it at `0x62991A`, with `+0x24` = 1, `+0x20` = `0x114C900`, `+0x50` = 0). Every immediate `0xFEC0C4` in `.text` is a store of that member, so these are all builders [verified, image scan; no data reference to the constant]. With X = the store's offset - `0x38` (push depths checked per site):

| Builder | `X+0x50` [verified store] |
|---|---|
| `LotPlanCtx_Init 0x6298F0` and its 7 callers | as docs/109 §3.3 |
| `0x633750`, `LotPlan_PortalPointCheck 0x6338A0` (inline; both pass `lea [esp+0x10]` to `QuadCell_Class` at `0x6337F8`/`0x63394F`) | `0x10000` |
| `0x634930` | 0 (`ebp`, zeroed at `0x634947`) |
| `0x634F10` | 0 (`ebx`, zeroed at `0x634F1B`) |
| `0x635080` (in-room caller of docs/111 §2) | `0x10000` |
| `0x63AFD0` | `0x10000` |
| `LotPortalSearch 0x63EC40` | `0x10000` |
| `0x643A60` (constructor, context at object `+0x10`) | `0x100` |
| `RoutingWorld_BlockerCheck2d 0x82E340` | 0 (`eax` zeroed at `0x82E566`, stored at `0x82E61B` before two pushes) |
| `RoutingWorld_ObstacleQuery 0x82EDB0`, `0x82F0B0` | 0 (docs/84) |
| `RoutingWorld_PortalCheckLocked 0x82F470` | 0 (`ebx`, zeroed at `0x82F47F`) |

Cell callers that pass no context: `0x663589`/`0x663596` push 0; the quadtree callbacks `0x662D00`/`0x662D30` are registered with user data 0 (`push 0` before `push 0x662D00` at `0x667300`, before `push 0x662D30` at `0x667388`) [verified pushes; that the user data reaches the callback's first argument is inferred]. The remaining cell callers pass a context from their own caller (vtable slots `0x61AD90`, `0x61AF80`, `0x61B010` in the vtables at `0xFE9B00`, `0xFEA0B0`, `0xFEAC00`; `0x66BB90`, `0x66BCE0`, `0x66C0C0`, `0x6675E0`), so they hold a builder's word. A scan for `mov`/`or` of an immediate with bit `0x20` into `[reg+0x50]` finds 14 non-stack hits, all vtable pointers or -1 in non-routing classes (`0x4B84D3` .. `0xC6720D`) [verified]. **Verdict:** no context carries `0x20`; `QuadLeaf_CountBlockingTypes` is dead in 1.67. The only remaining assumption is a word written by register after the build, which docs/109 §3.2 traced for the job word.

### 4.2 I-109-6: Job_Cancel and route cancels

- `Route_PlanLocked 0x82C6E0` builds the `RoutingJob` on its own stack (`RoutingJob_ctor 0x819D50` at `0x82C7A4`), runs `RoutingJob_Execute` inline (`0x82C7AD`) and destroys it right after (`RoutingJob_dtor 0x819AE0` at `0x82C7B6`); there is no early exit between ctor and dtor [verified]. The vtable `0x1021258` is referenced only by ctor and dtor [verified], so the object is never handed to the job manager.
- The ctor zeroes the job handle `+0x10` (`0x819D6C`); the dtor calls `Job_Release` only when it is non-zero (`0x819B11..0x819B19`). `RoutingJob_Execute` has no store to `this+0x10` [verified for the function itself; its callees not scanned, inferred].
- Even with a handle: `Job_Release 0x59A990` -> `Job_Destroy 0x59A650` (new name) on the last reference, which calls `Job_Cancel(job, 0)` (`push 0` at `0x59A65A`); `Job_Cancel` calls the callback at `0x59A603` only when its second argument is non-zero (`cmp byte ptr [esp+0x18], bl` at `0x59A5D7`) [verified]. The callers that pass 1 are job-manager paths (`0x599898`, `0x59990F`, `0x59A120`, `0x59A729`, `0x59A7C1`, `0x59A8D8`, `0x59AA89`), i.e. other subsystems' jobs.
- Async: a queued job is freed by our `DropJob` (CAS `J_QUEUED`->`J_FREE`) before any game code runs; a running one finishes `Route_PlanLocked` in full and its result is dropped (`ts3native.cpp` `ServeAsync`, `DropJob`) [verified].

**Verdict:** neither the vanilla game nor our async path cancels a route through `Job_Cancel`; `0x59A603` stays silent for any route cancel, and that is expected. docs/109 §6 repro C (`pet-guard-cancel` for `0x59A603`) cannot fire it; take it off the pass-2 repro list and leave the site as "job manager, not routing" (docs/106 §7).

**Rerun triage 2026-10-27 (`untested-v020` `bp-snap2-cancel`, 2026-10-26, build 0dc5fca8):** FAIL at the first cycle, `pet_guard until area=true not met in 120 s`; harness, not routing [measured: activity samples show Roxy Dog running `Guard Object` on the floor target "Festus 44", then `Dig Hole`; no crash, no ProbeSnap verdict reached]. The guard ended without its discourage area: `GuardObject.Run` returns at once when the radial-range route close to the target fails, and a lazy pet naps instead (route failure inferred: no route-fail line names the pet). The poll then waited out a dead guard. Fix: ts3agent `pet_guard {action: state, restart: true}` starts a new guard on a new random target when the pet has none queued (branch `bugfix/pet-guard-restart`); `pet-guard-cancel` polls with `restart: true`. Rerun the job after the merge.

## 5. Other BP readings

### 5.1 I-105-3: display level inside CanSee

- `pvCanSee` counts calls at `0x75AC22` (goal building's radial validation); `pvCanSeeRaise` counts every call at `0x789EB5` and `0x78A218`, whatever called `World_CanSeeObject` (callers `0x75AC22` and the script IC at `0x78F0F7`) [verified, `values.cpp`]. The report line "display-level calls inside 1278" overstates it. Before the window: 75 CanSee, 150 raise calls (exactly 2 per call); in the window: 6,973 and 1,278. The raise and the restore always come as a pair (docs/105 §3), so at most 639 CanSee calls (9 %) raised a level, fewer if script checks raised some.
- Is it a state write inside a search? It is a write [verified, docs/105 §1]: `lot+0x39C` and bit `0x10000` of every level shape's flag word, raised then restored, quiet (no event 68: `pvEv68InCanSee` 0 confirms it live). It is in goal building (`Route_PlanCommon` -> destinations -> `GoalPoint_Validate`), before `Route_PlanLocked`, not in the A\* search. For an async `Route.Plan` job the worker runs the whole IC (`ServeAsync`), so the raise can happen on the worker while the render thread or another sim-thread ray reads the shapes [inferred: the probe does not record the thread]. A reader in that gap sees upper floors solid for one test.
- Probe fix: count `PV_RAISE*` only with an open `PV_CANSEE` frame, and per thread class (sim / worker / other). Design note for the offload: the raise only changes the ray mask, so an off-thread replacement can pass a per-call mask instead of writing shapes [inferred].

### 5.2 RoomGraph_FirstUse, 961,649 entries, 444,279 off the sim thread

`0x63D2F0` (thiscall, `ret 8`; arguments lot graph and room index) builds the lot graph only when `lot+0x8C` is 0 (`LotGraph_Build`), and the room only when its record byte `+0` is 0 (build from `0x63D36A`, with `LotPlanCtx_Init` at `0x63D376`); otherwise it returns the built byte (`0x63D707`) [verified]. The counter is an entry counter, so 961k (about 46 per plan) is mostly the read path. Off-sim entries are the async worker's (thread id in the gate log) and run inside the job's `+0x918` hold; sim-thread entries go through the route gate stub that owns `0x63D2F0` (the probe counted from inside it) and wait for a running job (Enforce=1 in this run, log "route gate closures on") [verified log]. The other room-record writers, `LotGraph_RebuildRooms 0x63D7B0` and the room graph refresh `0x63D860`, are gate sites too [verified log]. No race found [inferred: relies on the gate covering every reader]. Probe fix: count builds, i.e. entries with `[slot[1]+0x8C]` byte 0 or room record byte 0.

### 5.3 Footprint_EnsureLevelPolygons, 5,651 entries

`0x80E5B0` tests the level's valid byte (`[fp+0x38] + level*0x24 + 0x20`) and returns at once when set; only an invalid level is marked valid, freed (`0x61EE20`) and rebuilt through slots `+0xF8`/`+0xFC` [verified]. 0 entries inside `RoutingJob_Execute`; 2,864 off-sim entries are the worker outside the search proper, i.e. goal building and object checks in the IC part of the job, still inside its `+0x918` hold [inferred]. The 2,787 sim entries are the steering and footprint queries of docs/82; whether any rebuilt while a job ran is the `[FpRebuildLock]` Mode=0 counter `caSimInJob` (docs/84 C-A), not this one. Consistent with docs/82/84; nothing new. Probe fix: count rebuilds (valid byte 0) per thread class.

### 5.4 G build thread

docs/100 §1 item 1 says the load-time G build runs on the main thread inside `World_PostLoadInit` (message `0x38E3C1E`). The BP log shows the message path at `0x835390` and the build on thread 7200, which the same log's route gate lines name the sim thread (main is another id) [verified live, one run, BP]. So `World_PostLoadInit` (or at least its message send) runs on the sim thread here; I-108-8's "main thread" expectation is wrong for this build. Not a cost problem (load screen), but the docs/100 snapshot timing note should say "sim".

## 6. Symbols added (CSV, append only)

`tools/ghidra/symbols/ts3w-1.67.symbols.csv`, tag `<docs/112>`: `RoutingWorld_RestoreLotBlockedBits 0x82A190`, `LotRouting_FinishBuild 0x63CAF0`, `LotRouting_SetEntranceTransform 0x63D720`, `Job_Destroy 0x59A650`, `LotPlan_PointCellCheck 0x633750`, `LotSearchCtxHolder_ctor 0x643A60`. Roles from the callers and stores above; names inferred.

## 7. Ledger

docs/51 §52, rows I-112-1 to I-112-12.
