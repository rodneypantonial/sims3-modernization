# 108. Routing value probe ([ProbeValues], ts3native 0.24.0, counters v44)

Status 2026-10-18: built and tested offline (selftest, steerro-test seeds 1 and 2 x 3000, `hook-sites.ts --check`); no game run yet. Rows I-108-1 to I-108-20 in docs/51 §44 wait for the two queue jobs below.

## 1. What it is

The routing studies docs/91, 98, 99, 100, 101 and 105 settled the logic statically and left a list of **values** open: numbers only a running game has (ini tuning in memory, world origin, how often a branch fires, which status codes occur). `[ProbeValues]` reads those values in one observe-only run per world. It never changes behaviour: every hook counts, then runs the game's own code with the same registers, flags and stack.

- Source: `native/ts3native/values.cpp` (included by `ts3native.cpp`), one-line owner hook in `routegate.cpp` (`g_pvRg`).
- Switch: `[ProbeValues] Enabled=0` (default off), `PollSec=2`.
- Counters: v44, `+0x15EC..0x19DB` (docs/registry.md), read by `tools/probe/agent.js` as `pv*` (arrays `name_i`, `name_i_j`).
- Log: `ts3native.log` lines starting with `values` (per-world snapshot, G builds with caller and thread, message paths, sea height and G header changes, R changes, the first 8 A\* contexts).
- Report: `node tools/harness/values-report.ts <...-native.json> --log <...-ts3native.log> [--world <file.world>]` prints each docs item id with its value and the verdict of the rule below. `--self-check` runs offline.
- R reference: `node tools/rtdata-decode.ts <file.world> --checksum` prints the same checksum from the world file (Sunset Valley: `0x8F578136 / 0xF11FC59F`, 39,265 waypoints).
- Queue: `tools/harness/queues/value-probe.json`, jobs `bp-value-probe` and `ip-value-probe` (10 min, seed 12345, the env-on settings of the snap-probe jobs plus `ProbeValues.Enabled=1`, asi `game-copy/ts3native.asi.values`, `minCounters` 44).

Run (not started by this change):

```text
node tools/harness/queue.ts tools/harness/queues/value-probe.json
node tools/harness/values-report.ts probe-out/queue/value-probe-<date>-bp-value-probe-native.json --log <row.nativeLog>
```

## 2. Mechanism

Two hook kinds, both a pass-through stub (`pushad`, `pushfd`, `PvEnter(id, return slot)`, `popfd`, `popad`, `jmp` on):

- **Entry (E)**, 17 sites: the verified prologue moves to a trampoline (`InstallDetour`), as in `verify.cpp`.
- **Call site (C)**, 25 sites: the rel32 of one `call` points at the stub, which jumps to the original callee. The site itself names the caller, and an entry detour on the callee (ours or another module's) still runs.

Ids that need the leave (time, return value, or an "inside X" scope) swap the return address for a thunk and keep the real one on a per-thread stack of 16 frames. At the leave the thunk takes the frame with the highest slot at or below its stack pointer (so `ret N` callees work); frames left without returning (exception unwind) are dropped at the next entry or leave. Handlers do integer work only, so x87 returns pass through.

Every site's bytes are checked before writing (prologue, or `E8` plus the rel32 to the expected callee); a difference logs `SKIP values ...` and counts `pvSkipped`. `LotGraph_Build 0x63CB90` and `RoomGraph_FirstUse 0x63D2F0` are counted from inside the `[RouteGate]` stubs when that section owns them (docs/68 §6 rule 3). `steerro-test` asserts all 42 rows against the exe image.

One poll thread reads, per world (two stable polls after the routing world `[0x11D9128]`, the world planner `W = [world+0x960]` and R's record area exist), the tuning, origin, guards and R checksum, and on every poll the drifting values (sea height, `WM+0x1B4`, `world+0x983`, G header, blockedNodeMap size). R is checksummed again every 60 s.

"Family" below: the Route_PlanCommon call site the plan runs under, `0x764298` (Plan / Replan, `0x7641D0`) or `0x7643FB` (PlanFromPoint / ReplanFromPosition, `0x7642C0`), or none. "In plan": inside `RoutingJob_Execute` (call site `0x82C7AD` in `Route_PlanLocked`), on whichever thread runs the job (sim or route worker).

## 3. Items: what is measured and the answer rule

| Item | Measured | Counters / log | Answer rule |
|---|---|---|---|
| docs/99 F-1 | 13 floats of the A\* container: threshold `W+0x18+0x24`, SHORT set `+0x28..+0x3C`, LONG set `+0x4C..+0x60` (slots 7-9 of each set are per-search and not read) | `pvTune[13]`, log `values world ... tune` | All equal the `0x673C70` defaults (50; 1.3, 2, 2, 2, 2, 2; 1.3, 2, 4, 10, 10, 10) = the ini does not replace them. Threshold 80 and the ini values where the ini has one = the ini replaces them. Anything else: read the log line |
| docs/99 F-2, I-91-2 | FNV-1a checksum of R's records (per chunk the count; per record n, flags, bx, bz, refs, edge bytes; surface byte and offsets left out) with flags `& 0x7F` (sum A) and `& 0x60` (sum B), waypoints, blocked | `pvR[0..3]`, log line | Sum A equal to `rtdata-decode.ts --checksum` = memory R equals the decoder's records. Only sum B equal = the type bits differ (shoreline rewrite, docs/100 §3.2 step 8). Both differ = the conversion differs: diff by chunk |
| docs/91 §6.6 | R checksum again every 60 s | `pvR[4]` (changes), `pvR[3]` (blocked now) | 0 changes = R fixed apart from bit 7 |
| docs/99 F-7 | Calls of `JobRng_Next` at `0x81BC65` (the goal-tie branch) and plans | `pvTies`, `pvJobs` | Any tie = the shadow compare must accept the tie set (docs/94 M1); the rate sizes how often |
| docs/99 F-11 | At `WorldAStar_Search 0x676260` entry: opts flag bits 1, 2, 4, 8 (and 0x10-0x200), `opts+0x14` and `opts+0x44` non-zero, per family; first 8 contexts logged with both hub refs | `pvAStar*`, log `values A* opts` | Bits 1-8 never set = the substitutions and factor-30 bypasses are dead for this caller (docs/99 §10.1 inference holds); any set = live |
| docs/99 F-12 | World origin `WM+0xB4/+0xB8` and max `+0xBC/+0xC0` per world | `pvWm[4]` | Origin 0 in every world = the cost functions' chunk frame agrees with world coordinates; non-zero = they mix frames (real risk) |
| docs/98 U6 | Caller flag word of `LotPlan_Inner 0x63F6E0` (argument 9) per bit `0x1-0x8`, `0x10`, `0x20`, `0x100`, `0x80000`, `0x100000`, `0x200000`, `0x2000000`, and obstacle mode (argument 12), per family | `pvLotPlan*` | Print; the class model needs only the bits that occur |
| docs/100 L1, docs/91 §5.1/§6.4 | G builds `0x6734A0` by call site (`0x82A098` message path, `0x834A82` world rebuild), thread (main, sim, other), time; G header after each build | `pvGBuild*`, log `values G build` | At least one build at load on the main thread, through `0x82A098`; water worlds show water cells and bodies |
| docs/100 L2 | `WM+0x1B4`, `world+0x983`, `WorldManager_PostWaterGridDirty 0xC61FE0` entries | `pvWmDirty`, `pvWorld983`, `pvDirtyPosts` | Print (values decide whether lot and terrain edits re-arm the rebuild) |
| docs/100 L3 | G builds and `0x82A030` message sites during the 10 min window | deltas of `pvGBuild*`, `pvGMsg[3]` | 0 without build/buy = no rebuild in play |
| docs/100 L4 | High-cost stamps `0x6733C0` and mark writes `0x670750`/`0x670510` | `pvHighCost`, `pvLazy[6..7]` | Print (rate per minute) |
| docs/100 L6 | `0x66FB40`/`0x66F6D0` calls and `G+0x368` size (max) | `pvBlk[2]`, `pvG[4]` | Print |
| docs/100 L8 | `BuildWorldRoutingData 0x8349C0` by call site, `0x835A27` = message `0x7FDA974` | `pvRebuild[3]` | Non-zero at `0x835A27` = that message is sent (log has the thread) |
| docs/100 L10 | `[WM+0x60]+0x14` every poll | `pvSeaY`, `pvSeaChanges` | 0 changes = sea height fixed after load |
| docs/101 U2 | Dwords `0x11CF010`, `0x11CF02C`, `0x11CF030` after load | `pvGuard[3]` | Guard set after load; the `0x11CF02C` value to compare with lot graph `+0xB8` (the compare itself needs a lot walk: not done) |
| docs/101 U4 | Interest point allocations (`0x821B3D`) and non-zero `IC_Route_GetNumInterestPoints` answers | `pvIpAlloc`, `pvIpCalls`, `pvIpNonZero` | Both 0 = lists always empty (I-101-6) |
| docs/101 U5 | `mType` at `Route_PlanCommon` return, histogram 0-18 (19 = more) per family | `pvStatus[2][20]` | Only {1, 2, 3, 4, 10, 16, 18} = docs/101 §4.2 holds |
| docs/101 U6 | `Portal_LockState` answers (state 0-3, other) by path: goal step (`0x63F65E`), lot search (`0x641026`), lot A\* (`0x63E708`), in-room (wrapper return `0x632559`), other | `pvPortal[5][5]` | Print; same totals by path tell whether goal step and search ask the same doors (portal ids are not recorded) |
| docs/101 U7 | H9 rewrites (`0x63257E` remove, `0x632595` insert), inside plans, plans with a rewrite, most in one plan | `pvH9*` | Print (rate per plan) |
| docs/101 U8 | Partition grid writers `0xC08C30`, `0xC094E0`, and how many off the sim thread | `pvPart*` | Off-sim 0 = sim thread only; 0 writes in live play expected |
| docs/91 §5.4, §6.2 | Lazy work: `LotTable_Insert`, `LotGraph_Build`, `RoomGraph_FirstUse`, `ApplyLotUpdate`, `FlushDeferred` with queued work (`Q+0x4C`), `Footprint_EnsureLevelPolygons`, `0x670510`, `0x670750`: calls, inside a plan, off the sim thread | `pvLazy[8][3]` | Print; rare inside plans = the snapshot builder can run them on the sim thread at change time |
| docs/105 I-105-1 | World event 68 fires from `Lot_SetDisplayLevel` (`0xAC0763`), and those inside `World_CanSeeObject` called from goal building (`0x75AC22`) | `pvEv68`, `pvEv68InCanSee` | 0 inside = the quiet flag holds |
| docs/105 I-105-3 | `World_CanSeeObject` calls from `0x75AC22` and its `Lot_SetDisplayLevel` calls (`0x789EB5`, `0x78A218`) | `pvCanSee`, `pvCanSeeRaise` | Print (the route option bit is not read) |
| docs/105 I-105-4 | `World_FirePortalLockedQuery` answers by initialLocked (argument 9) | `pvQuery[2][5]` | Calls with initialLocked 1 exist and some answer 2 = script can unlock a natively locked portal |
| docs/105 I-105-5 | Event 59 queries inside plans per plan | `pvQueryInPlan` / `pvJobs` | Print (estimate 2-30 per plan) |

## 4. Not measured, and why

| Item | Why not here |
|---|---|
| docs/101 U10 (`route+0x1F4` hidden reader) | needs a hardware read watchpoint (x32dbg session) |
| docs/98 U7 (writers of `X+0x4C`) | needs a write watchpoint on a stack context, or a trace |
| docs/101 U1, U3 (worker write set, `0x66AB70` coverage) | need Stalker on the worker (P2 L1/L2, docs/94 §4.1) |
| docs/101 U9 (portal IC writes) | the P1 leaf counters already exist (lotfd.cpp P1b) |
| docs/91 §6.1 (read set per plan) | `[ProbeSnap]` (indirect targets) and `[Verify]` cover it; a per-accessor read count is a separate probe |
| docs/91 §6.3 (change rates of all writers) | partly here (marks, partition grid, lazy work); quadtree insert/remove and footprint updates are hot paths left to `[Verify]` |
| docs/91 §6.5 (allocator tag totals) | needs the EA allocator's tag table walk; not a cheap read |
| docs/99 F-3, F-10, F-13 | need per-node or per-edge dumps (splitter flags, polygon sets, edge costs): large logs, not counters |
| docs/99 F-4 | a route-side height read (docs/93 §6), outside the planner |
| docs/100 L5, L7, L9 | need a recomputation, a Sim on a sailing houseboat, or a walk over every lot |
| docs/105 I-105-2, I-105-6, I-105-7, I-105-8 | emulator compare or script-side (managed) code |

## 5. Cost

Per hit: one `pushad`/`pushfd` and a C call (about 30-60 ns), plus a TLS frame for the leave ids. Hot sites: `FlushDeferred` (every lot quadtree query that flushes), the two `Portal_LockState` call sites (each portal asked), `0x670510`/`0x670750` (object moves near water); per plan: `RoutingJob_Execute`, the world A\*, `LotPlan_Inner`, the three path tags. At an estimated 10,000-50,000 hits per second on the sim thread in a busy lot, that is 0.05-0.3 % of the sim thread [inferred, not measured]; the run's frame times against `bp-env-on` check it. The poll thread does all reads off the sim thread (R checksum about 1 ms per minute). Budget: under 2 % of the sim thread.

## 6. Pass rule for the run

`pvReady` 3, `pvSkipped` 0 (any `SKIP values` line names the site), `pvWorlds` at least 1, `pvFrameFull` 0, no crash, and the S3SS / S3PP install lines present (docs/68 §6 rule 7). A crash with this build is our bug until proven otherwise (capture, disassemble, ledger).

## 7. Results 2026-10-18 (`queues/value-probe.json`, ts3native 0.24.0, counters v44)

Both jobs PASS (pvReady 3, 42 sites, 0 skipped, 1 world snapshot, 0 frames not taken). BP 19:06 PHT, IP 19:20 PHT, 10 min seeded, env-on (async + gate + D). Daytime counts only. In both jobs the stutter profiler overran its window and the fail-fast guard killed it; that does not touch the value counters. Reports: `node tools/harness/values-report.ts probe-out/queue/value-probe-2026-10-18-<bp|ip>-value-probe-native.json --log <ts3native.log>`.

| Item | BP | IP | Verdict |
|---|---|---|---|
| F-1 tuning | threshold 50; keep 1.3; SHORT trespass/sidewalk/riding/street/water-lot 2; LONG 2/4/10/10/10 | same | the defaults of `0x673C70`; Sims3.ini does not replace them [verified live] |
| F-2 R checksum | `0x1CCBD509 / 0xFB9E8561`, 35,313 waypoints | `0x2E522F2F / 0xBC04C598`, 43,942 waypoints | **match**: `rtdata-decode.ts --checksum` on Bridgeport.world and IslaParadiso.world gives the same sums and waypoint counts (0 blocked in the file) [verified]: R at load is the `.world` data, nothing computed at load |
| 91 §6.6 R after load | 1 change: first sum moved, second sum and waypoint count kept, blocked 0 -> 284 | no change apart from the blocked bit (98 blocked) | **R is static except the per-waypoint blocked bit (bit 7)**: the snapshot must copy blocked bits on change (writer: docs/112) |
| F-7 goal ties | 0 in 21,063 plans | 2 in 8,257 (0.02%) | ties happen: a shadow compare must accept any member of the tie set |
| F-11 world A* context bits 1/2/4/8 | 0 of 16,325 | 0 of 3,292 | substitution and factor-30 bypass paths dead in play [verified live] |
| F-12 world origin | 0,0, max 2048 | same | cost frame agrees |
| 98 U6 `LotPlan_Inner` option bits | 0x4 on 744 of 13,857 Plan/Replan; 0x1/0x2/0x8/0x10 9 each; obstacle mode 10 | 0x4 642 of 6,665; 0x1/0x2/0x8/0x10 68; **0x200000 6** | bits match docs/109 §3; 0x200000 seen in IP only (retry bit family) |
| L1/L3 water grid G | built once at load on the sim thread, 688 ms; no rebuild in play | 505 ms; no rebuild | G is load-time state |
| L10 sea height | 13.63, no change | 27.38, no change | fixed after load |
| 101 U4 interest points | 0 allocs, 717 queries all empty | 0, 554 empty | I-101-6 holds |
| 101 U5 status | 19,188 plans in bucket 19+ | 9,036 in 19+ | **suspect read** (probe field/time or real codes): docs/112 decides |
| 101 U6 portal-lock answers | values 1 and 2 only | value 3 seen (goal step 916, lot A* 320) | lock answer 3 occurs in IP |
| 101 U7 H9 rewrites | 48 inside plans, 8 of 21,063 plans | 0 | rare |
| 101 U8 partition grid | 0 writes | 0 | no write in live play |
| 91 §6.2 lazy work | `RoomGraph_FirstUse` 961,649 (444k off sim); `LotGraph_Build` 257 (192 off sim); `EnsureLevelPolygons` 5,651 | 727,071; 236 (134 off sim) | lazy builds run on the worker with async on: race check in docs/112 |
| I-105-1 event 68 | 19, none inside CanSee | 47, none | quiet flag holds |
| I-105-3 display-level calls inside CanSee | 1,278 | 3,616 | a state call inside the goal search: docs/112 |
| I-105-4 portal queries with initialLocked 1 | 11,835 | 5,206 | script asks with initialLocked 1 |
| I-105-5 event 59 per plan | 4.30 | 8.78 | inside the 2-30 estimate |

Blend layer 0 (`rtdata-decode.ts --blend0`, docs/107 §10): Bridgeport 79.79 % of the map at weight >= 0.5, 482 of 35,313 R waypoints on it; Isla Paradiso 18.32 %, 1,332 of 43,942 (Sunset Valley 83.77 %, 665 of 39,265). `.world` files copied from the Steam install into the ignored `game-copy/worlds/`.

## 8. 0.24.1 fixes (counters v45, docs/112)

Built 2026-10-18, offline checks only; not run in game yet. Parked as `game-copy/ts3native.asi.values`; `queues/value-probe.json` now requires counters v45. Fields in docs/registry.md (v45, `+0x19DC..0x1A1B`); thread classes of the new fields are sim / route worker (`g_workerTid`) / other.

| Item | 0.24.0 | 0.24.1 |
|---|---|---|
| 101 U5 status (I-112-1) | read argument 2 of `Route_PlanCommon` (a caller-local vector): every plan in bucket 19 | reads argument 1 (`slot[1]`, the 18-dword result, `mType` at +0). Selftest: a live call site to a two-argument `ret 8` dummy that writes `mType` 16 into argument 1, with a heap pointer as the first dword of argument 2; reading argument 2 fails it (bucket 19) |
| I-105-3 raises (I-112-9) | `pvCanSeeRaise` = every `Lot_SetDisplayLevel` call at `0x789EB5`/`0x78A218`, any `World_CanSeeObject` caller | kept; new `pvRaiseIn[3]` counts only calls with an open `PV_CANSEE` frame (goal building, `0x75AC22`) |
| `RoomGraph_FirstUse` (I-112-10) | entries | `pvLazy[2]` still entries; new `pvRoomBuild[3]` counts entries that build: byte `G+0x8C` 0 (`0x63D300`) or the room's record byte 0 (record `0x44` bytes from `G+0x90`, room index = u16 argument 2, `0x63D359`) [verified disassembly]. Also counted when the route gate stub owns the entry |
| `EnsureLevelPolygons` (I-112-11) | entries | `pvLazy[5]` still entries; new `pvLevelBuild[3]` counts entries with byte `[fp+0x38] + level*0x24 + 0x20` 0 (`0x80E5BF`) [verified disassembly] |
| R baseline (I-112-2) | snapshot at the second poll with R present; the load G build after it counted as an R change | `pvR[0..1]` keep the snapshot (F-2 compare with the `.world` file); when a G build finishes after the snapshot, the poll thread retakes the baseline once (`pvRRebase`, log "values R baseline after the G build"); `pvR[4]` then counts changes against that baseline |
| R blocked bits (I-112-12) | count only (`pvR[3]`) | `PvRSum` adds an FNV-1a hash over (chunk, record) of every waypoint with bit 7; `pvRBlk` = hash at the baseline, now, and 60 s checks that saw it change (log "values R blocked bits changed") |
| R writers (I-112-3, -4) | `ApplyLotUpdate 0x82B590` counted (`pvLazy[3]`) | both writers log the lot pointer and id (lot `+0x48`/`+0x4C`) and thread class (log "values R writer", first 1,024 lines); new entry detour on `RoutingWorld_RestoreLotBlockedBits 0x82A190` (7 bytes `8B 44 24 04 85 C0 56`, no relative operand, docs/68 row updated) with `pvRestore[3]` = calls / inside a plan / off sim. 43 sites |

Checks: `build.cmd` selftest, `steerro-test` seeds 1-2 x 3000 (43 value sites match the image), `node tools/hook-sites.ts --check`, `node tools/check-docs-code.ts`, `values-report.ts --self-check`, `queue.ts tools/harness/queues/value-probe.json --dry-run`. `values-report.ts` prints the new lines (`R blocked`, `91 §6.2b`, I-105-3 by thread) for v45 dumps and says "no answer" for U5 on v44 dumps. Ledger: docs/51 §57.

## 9. Results 0.24.1 (2026-10-18 23:19-23:44 PHT, `queues/value-probe.json`, both PASS)

| Item | BP | IP | Verdict |
|---|---|---|---|
| 101 U5 status (fixed read, slot[1]) | Plan/Replan 1: 15,654, 10: 980, 16: 184, 18: 90; point 1: 1,975, 10: 24, 16: 123, 18: 61 | Plan/Replan 1: 5,049, 10: 860, 16: 635, 18: 19; point 1: 2,011, 10: 28, 16: 198, 18: 8 | **closed**: statuses stay in {1,2,3,4,10,16,18} (docs/101 §4.2 holds; the 19+ reading was the probe bug) |
| R blocked bits after the post-G-build re-baseline | hash unchanged (165 restores, 252 applies wrote the same values) | **changed in 2 checks** (104 restores, 248 applies); `values R writer` lines name the lots | blocked bits do change in play in IP: the snapshot's copy-on-change for R bit 7 (docs/94 §8) is needed, not optional |
| Lazy room-graph builds (`RoomGraph_FirstUse`, builds only) | 171 sim / 319 worker of 1,080,370 entries | 157 sim / 248 worker of 786,715 | most real builds run on the async worker; `Prebuild=1` (docs/94 §16) should remove them |
| `EnsureLevelPolygons` rebuilds | 0 of 5,925 entries | 0 of 2,888 | entries are all cache hits in play |
| I-105-3 display-level raises inside CanSee (goal building) | **146 sim / 400 worker** (7,022 CanSee calls) | 0 / 0 (2,889 calls) | in BP the async worker writes lot display state (lot `+0x39C`, level-shape bit) during goal building: a cross-thread write in today's async build; design in docs/94 §17.9 (pending) |
