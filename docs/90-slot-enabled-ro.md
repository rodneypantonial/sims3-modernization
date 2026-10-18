# 90. Slot position read-only (design SH), gate-12 family F-B

Built 2026-10-18, static and offline only: the game was not run. **Off by default** (`[SlotEnabledRO] Enabled=0 Mode=1 Shadow=0`). ts3native 0.22.2, counters v42, `native/ts3native/slotro.cpp`. Addresses verified with capstone 5 on `game-copy/bin/TS3W.exe`; call graph by the direct-`E8`/`E9` caller index over `game-copy/analysis/functions.csv` and a 5-hop closure walk.

## 1. Where the wait is

`bp-p1b` (l1b build, 2026-10-18 11:57 PHT) put the biggest single gate-12 source, family F-B, at caller key `0x62F782` (`LotRouting_PortalsForObject 0x62F760` at its `LotTable_Contains`), second frame `0x76F15D` — 854 waits, 7.6 s in 10 min (12.7 ms/s), 69% of gate 12.

`0x76F15D` is the return of the `PortalsForObject` call inside **`0x76F0A0`**, the slot position helper (`thiscall`-less, `cdecl(float4* point, obj, ageGenderSpecies, a4)`, returns `al`, `and esp,-16` frame). Its callers [verified `E8` index]: `IC_Slots_GetPositionOfSlotImpl 0x76F2F0`, `0x755AB0` (reached from `IC_RouteManager_IsSlotUnobstructed 0x759160`), `0x7596E0` (slot-goal class), `0x830340` (`RoutingWorld_RouteToObjectCheck` family). Script sources: portal lanes (`Stairs`, `PortalComponent.GetPathToLane`, `Pool`, `BoxStall`), `GameObject.GetNearestSlot(Sim, …)`, `SimRoutingComponent.RouteToUnusedPart` [docs/84 §4.1 F-B].

**Not** `IC_Slots_IsSlotEnabledImpl 0x76F000` — the queue job (`[SlotEnabledRO]`) keeps that task's name, but `0x76F000` calls neither `0x76F0A0` nor the lot table (it does an id-hash lookup `0x935770`, an interlocked refcount on the found object, one virtual call `0x951940`, and a deleting call on release) [verified disassembly]. So the IC named in the wait is a misread; the wait is in the slot position helper reached from the *other* slot ICs. [I-90-1]

## 2. What `0x76F0A0` reads and writes (5 hops)

Early out [verified `0x76F0AC`, `0x76F0BC`]: with `ageGenderSpecies == 0` or a NULL object it returns `0` before any routing read, so the F-B path only runs with flags set and an object. Then:

- `0x8292D0` → `0x836200` reads the RoutingWorld global `[0x11D9128]`; `0x829BC0` reads a world byte; `0x935770` is a pure power-of-two bucket hash lookup (no writes). `0xC62D40` → `0xC60D30` → `0xC5FA60` finds the lot's routing sub-object (reads only). [verified: no store to a game global on these]
- **`LotRouting_PortalsForObject 0x62F760`** (`0x76F158`, returns `0x76F15D`): `LotTable_Contains 0x628A90` (gate 12) then, on a hit, `GetOrInsert 0x62EAB0` (gate 0, hit path), copies the object's matching portals into a vector and **AddRefs each** (`0x62F7E5`, or in the vector insert `0x8B70E0`). Its net change to each portal count over the call is **not** zero here (it leaves a reference in the returned vector, released later by the caller's `0x5BF1E0` tail) — unlike `0x631800` (L1), whose net is 0.
- `0xAB88A0`, `0xAB78B0` (nearest-point search over the lane geometry: reads the sub-object's arrays, FPU math, no global store), `0x82A420` → `0x62EB40` → `GetOrInsert` (lane transform, gate 0 hits), `0x5A8CD0`, `0x76E330`, `0x64E910`, `0x62F3B0` → `0x62EB40` → `GetOrInsert` (lot-to-world of the picked point). `0x5BF1E0` releases the portal vector at the end.

Deeper (hops 4-5) `0xC6A250`/`0xC688A0`/`0xC68680` and `0xB74A20` reach `Lock_Acquire 0x4E16F0` — a terrain/cell lock, **not** `world+0x918`. SH's hold is dropped before any `Lock_Acquire` (see §4), so this does not compose with the QL hold. [verified edges]

**Serialised reads that make the sim wait today:** only the gate calls (`Contains` = gate 12, `GetOrInsert` = gate 0). Their reads are the lot table (worker writer **W5** `LotTable_Insert 0x62E680`) and the lot entry's portal vector `entry+0x44/+0x48` (**W4** `ValidateLotData 0x636330` via `LotGraph_Build`), both held under QL exclusive on the worker [docs/83, docs/84 §4.2]. Everything else in `0x76F0A0` already runs beside the job (the gate frame holds `+0x918` only for the gate call itself). **Writes:** the body writes the caller's point out-parameter and its own stack, and the portal refcounts; no lot-table or quadtree write [verified: no store to the table, buckets or entry fields]. [I-90-2]

## 3. Design SH: run the game's body under a QL-shared hold

The hold is a lock, not a data change: SH detours `0x76F0A0` (`55 8B EC 83 E4 F0`, 6 bytes) and runs the **game's own body** (`g_slOrig`) with `g_ql` held shared (try only). Inside the hold, `RgEnter` (routegate.cpp) sees `t_slHold == 1` and, at an outermost non-worker gate entry:

- **gate 12** (`LotTable_Contains`): if the caller is `PortalsForObject` (`0x62F782`), check the object's portals in the entry are the interlocked count class with count ≥ 1 (`SlPortals`, our own `LtFind` walk); any other gate-12 caller passes too (its `Contains` reads only the table). Pass → no `+0x918` wait (`slPass12`).
- **gate 0** (`GetOrInsert`): our `LtFind` on the key; a **hit** writes nothing, so pass (`slPass0`); a **miss** would insert under a shared lock → drop.
- any other gate site, or either gate with a reason above failing → **drop**: release QL, then fall through to today's `+0x918` wait (`slDrop[reason]`).

Because the gates read W4/W5 state the body would read anyway, and the hold only skips the lock, the held body returns the same point and bool as the unheld call whenever the gate-skip answer matches `Contains`/`GetOrInsert` — proven in §4. [I-90-2]

Since `PortalsForObject` AddRefs portals on the sim while the worker may AddRef the same portals outside QL (docs/84 §4.2 #1, T-L3), the plain `add [ecx+0x70],1` / decrement pair races. SH swaps portal vtable `0xFED048` slots 0/+4 to **interlocked** `PortalAddRefX`/`PortalReleaseX` (same rule: Release at count ≤ 1 calls the deleting slot +8 with `1` and returns 0, else CAS-decrements) for the whole run (`slRefSwap`). If P1b's T-L3 counting thunks already sit there, L1's `g_ltAddRef`/`g_ltRelease` are repointed at the interlocked pair so both compose. [I-90-4]

**Lock order / no deadlock:** QL shared, try only, never nested (`t_qlDepth`/`t_qlShared`/`t_slHold` guard), never across a managed/host/`+0x918` call — `QlSharedTrip` drops the hold on any of those first, so it never counts in T-L2 (`ltQlHeldBlock` stays 0). An exception through the held body still releases QL (`SlCall` `__finally`). [I-90-5, I-90-6]

## 4. Offline proof (all run 2026-10-18)

| Check | What | Result |
|---|---|---|
| `build.cmd` | `/W4 /WX`, all targets, selftest | pass |
| selftest `TestSlotRo` (fakes, v42 layout) | interlocked pair == the plain pair's results incl. delete-at-1, and a 600,000-pair race on three threads (no lost update, no stray delete); `SlGate` pass on a `Contains` hit and `GetOrInsert` hit, drop on another caller's bad portal, gate-0 miss, active-lot, other gate site, odd table, and a managed call (T-L2 unchanged); `SlBegin` nested / broken / try-fail fallbacks; hook dispatch (off, no flags, no object, not owner, no job → game's call; job → hold then release; drop inside → fall back; `g_inInvoke` / `+0x918` owner / open gate frame → state fallback; QL held by another thread → try fallback); an exception through the held call releases QL; shadow equal, a skewed answer counted, a dropped dry hold counted | pass |
| `steerro-test.exe` section I (maps `TS3W.exe`, runs the game's code) | the game's `0x628A90 Contains` vs `LtFind` hit/miss, the game's `0x62EAB0 GetOrInsert` hit vs `LtFind`'s entry, and `SlGate`'s pass/drop over random lot tables; `PortalAddRefX`/`PortalReleaseX` vs the game's `0x64E8E0`/`0x64E8F0` | seeds 1-4 × 3000 = **12000 gate cases, all equal** (gate-12 pass 11379, drop 621; gate-0 pass 6930); interlocked pair equal in 12000 cases |
| `node tools/hook-sites.ts --check` | docs/68 row complete | 288 addresses, 0 missing |
| `node tools/check-docs-code.ts` | no decompiled code in docs | ok |

Equal answers of the held body itself are **structural**: `SlCall` runs the game's own `0x76F0A0`; the hold only removes the lock wait at gates the clone's find already shows are hits/table-reads. So the correctness question reduces to the gate-skip decision, which section I proves against the real game code. [I-90-3]

## 5. Gain, switches, live runs to queue

**Gain cap:** the F-B gate-12 share, ~12.7 ms/s BP (`bp-p1b`). Realistic less — a removed wait can reappear at the next routing touch (gate 0 is already ~12 ms/s). Together with the gate-0 `IsPointObstructedBySim` fix (other agent, v41) the two F-B/gate-0 sources are ~25 ms/s BP.

**Switches** (v42): `slMode` (+0x1568, `--ab slotenabledro`; 0 = shadow only, 1 = hold), `slShadow` (+0x156C, `--ab slotenabledshadow`). Shadow (no job, job starts held off): the game's call, then the call again under a dry hold on a copy of the point, `al` and the point compared (`slShadowMismatch` must stay 0).

**Night jobs** (`tools/harness/queues/clean-night-2.json`, async + queue + D on, asi `game-copy/ts3native.asi.slotro`):
1. `bp-slotro-shadow` (10 min, Mode 0 Shadow 1): `slShadowMismatch` 0 with `slShadowRuns` > 100, `ltQlHeldBlock` 0, game alive, 0 watchdog lines; read `slDrop[6]` split.
2. `bp-slotro-ab` / `ip-slotro-ab` (18 min, `--ab slotenabledro`, 2-min slices, shadow on): Mode 1 halves have `slTaken` > 0, gate-12 waits (`blockKey 0x62F782`) and iterations/s better than the Mode 0 halves, `slShadowMismatch` 0, `slDrop[Lock/Managed]` small, 0 stuck Sims (log only). Re-run P0 after (gains do not add, docs/84 §5).

## 6. Open items and risks

- **Unverified live:** that W4/W5 plus sim-thread writers are the whole write set of the body's gated reads during a job (inherits L1's open point, docs/84 §4.6); the live gain. [I-90-7]
- The `0x76F0A0` body is driven offline only at the gate-decision layer (section I); the full portal-lane body is not run offline because a valid routable object graph is out of scope. Mitigated: the hold does not change what the body computes, and the gate decisions it does change are proven on the real game code.
- SH shares the interlocked portal pair and the QL writer stubs (W4/W5/W6g via `InstallQlSubset`) with L1; both agree on the slots. Running SH with L1 on is untested together (each installs the pair idempotently).
