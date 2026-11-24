# docs/585 DoorMemoX=3: cross-job door memo with ts3fixes' door inputs in the generation

Status: built, default off (`[RouteSnap] DoorMemoX=-1`, switch `doorgen` off). One in-game A/B arm (below). Not proven exact by
argument alone; the 1-in-8 self-check and the self-disable stay on.

## 1. What it does

The route worker asks the sim thread "may this Sim pass this door?" (managed `Sim.OnPortalPathPlan` / `CommonDoor.IsAllowedThrough`,
door stub at 0x64F4D3, docs/159 §4.1) several times per plan, and each ask waits for the sim thread to serve it. DoorMemoX=1
(docs/189 phase 3) reuses another job's "unlocked" answer for the same key within `DoorMemoXMs` (250 ms) while the door
generation is unchanged. Its generation only moved on portal ICs, lot world events and event 59, so a change made by script code
(a lock set, a greeting, a role) could leave a stale row for up to 250 ms. That is why it stayed off.

Mode 3 adds the managed inputs of the door answer to the generation. ts3fixes `DoorGen.cs` (switch `doorgen`) hooks every setter
we found and sends UserDiag op 36 (lo 0 = bump) after the original ran; at world load it sends lo 1 (arm). Until armed, mode 3
memoizes nothing (an old package, or `doorgen` off, never gets a memo whose inputs are not tracked).

Inputs bumped (hooks call the original first, then bump):
- door locks and owner lists: `CommonDoor.SetLockTypeAndOwner` (both), `SetLockTypeOwnerList`, `AddToOwnerList`, `RemoveFromOwnerList`
- greeting and household membership: `Household.AddGreetedLotToHousehold`, `RemoveGreetedLotForHousehold`, `Add`, `Remove`
- per-lot handlers: `Lot.add_PortalPathPlan`, `remove_PortalPathPlan`
- fire on the lot (`Lot.IsFireOnLot`): `FireManager.RegisterFire`, `UnregisterFire`, `BuffOnFire` / `BuffTorched` `OnAddition`, `OnRemoval`
- `Sim.FlagSet` when the `IgnoreDoorLocks` bit changes
- tomb mummy: `OccultManager.AddOccultType`, `RemoveOccultType` (the NPC-household half is `Household.Add` / `Remove`)
- roles (proprietors): `Role.CreateRole`, `Role.RemoveSimFromRole`, `RoleManagerTask.FillInWorldRole`
- resort VIP rooms and closing: `ResortManager.set_Closed`, `DoUpdate` (when `Closed` changed), `CreateNewVIPDoorGroup`,
  `RemoveDoorFromVIPRoom`, `FindVIPRoomData` (when it added a row), `RefreshVIPRooms`, `VIPRoomData.AddGuest`, `RemoveGuest`
- gender doors and the diving board: `SimDescription.set_Gender`, `set_Age`, `set_IsVisuallyPregnant`

Not covered: portal types with their own `IsAllowedThrough` beyond these (elevators, box stalls, horse jumps, spiral stairs,
pools, tube elevators, ladders) and role teardown on travel (a new world starts a new table). Those stay on the safety net:
every 8th valid row is asked anyway and compared, and one mismatch turns the memo off for the session (`FAIL door memo x` line).

Household Sims: the memo only answers asks the worker makes inside a route job; the answer it returns is the one another job got
from the same managed code for the same key and generation, so a household Sim gets the same answer it would have got live.

## 2. Settings

```ini
[RouteSnap]
DoorMemoX=3        ; -1 = stub not installed, 0 = off, 1 / 2 as docs/189, 3 = this
DoorMemoXMs=250
```
plus the ts3fixes switch `doorgen` on (`EARLYFIX` / settings). Counters: `ts3native_doorgen` export (docs/registry.md), the
`door memo x mode 3:` and `door memo x misses:` log lines.

## 3. In-game evidence (run phase-20261115-1935, DoorMemoX=3, doorgen on; final log line)

- armed at world load; 2560 managed-input bumps.
- hits 116,120, misses 203,112 (36% answered from the memo), invalidations 19,126, self-check re-asks 2,475, **mismatches 0**.
- host calls per route job 9.4 (shadow arm 1727) -> 5.3; host-call queue wait per job 11.2 -> 10.0 ms (HostSplit counters,
  diagnostic build). Most calls are served promptly at the pump sites; the READY-poll site keeps ~6 ms per job.
- 6 game hitches vs 1-2 in the other arms: one 1 s sim frame (a single poll, cause not logged), one hourly, the rest at game
  times that hitch in other arms too (9:12, 12:47). None attributable to the memo or the DoorGen hooks.
- Invalidations are at most 9% of the misses, so generation churn from the bumps is not what limits the hit rate. The
  `door memo x misses:` line (no row / other generation / older than DoorMemoXMs / answer not unlocked) splits the rest in the
  next run.

## 4. With [DoorFast] (PR #395, docs/548, open)

DoorFast predicts the same door asks natively from mirrored door data and runs first in the same stub; an ask it answers never
reaches DoorMemo / DoorMemoX, and an ask it lets through is memoized as now. Both are exact under their own self-checks, so both
on is consistent. Merging both needs a textual merge in `HcDoorEnter` (both edit its first lines) and in `ApplyHostCalls`
(PR #395 still rejects `DoorMemoX` > 2: keep 3).

## 5. Household rule (2026-11-16)

In the acting modes (1, 3) a memo answer is used only when the running route job is known unseen (scope 2 or a ts3fixes
`uf` mark) or a seen NPC's (scope 3, ts3fixes op 13). Household and selected Sims (op 10, scope 1) and unknown scope (no
answer, 0) always get the live host call, so their door answers are exactly vanilla [verified: selftest `TestAskLent`, one
case per scope]. Their live answer is still kept for others and compared with a valid row, so a stale row seen on a household
ask turns the memo off for the session. Counter `ts3native_doorgen.exempt` (0x20) counts those asks. The scope (`t_hcScope`) is set at each worker job start from `j->kdScope`. PR #430's 0 mismatches covered all scopes, NPCs
included, so the restricted memo ships ON: `DoorMemoX=3` in the ini, `doorgen` on in ts3fixes.

## 6. Privacy and mod door handlers (2026-11-17, branch `bugfix/doormemox-privacy-1117`, 0.36.295-dmxp1120)

Job `sv-hhsearch` (queue hhsearch-sv-1116, 2026-11-16 19:39 UTC, master-1116c asi + ini, hoursmemo-svprobe ts3fixes) logged
`FAIL door memo x` once: `door memo x shadow: answer 1, other job's 2 (79 ms before), key 8B52BB30 6C11001B 6A938280 F87500F5
5F050F00 6C110020 0001 0004 00`, i.e. lot 0x6C11001B8B52BB30, actor F87500F56A938280 (Razta Dog, active household, scope
exempt: it got the live answer, so no wrong answer reached it), room 1 -> 4. The memo was off for the rest of the session.
No other mode-3 log in probe-out (72) has a mismatch; HouseholdToo was off; the branch's ts3fixes changes do not touch doors.

Cause [verified, static, ledger §550 V-550-2/3; that it caused this mismatch is inferred]: the only EA `Lot.PortalPathPlan` handler beyond `Sim.OnPortalPathPlan` is `PrivacySituation`
(shower, toilet, bath, sink, WooHoo). Its answer for room `SyncTarget.RoomId` reads the asking Sim's current (or transition)
interaction, autonomous flag and priority, which change between jobs without any generation bump. The add / remove of the
handler was bumped; its per-ask inputs were not. Also untracked (not this case): `Lot.IsRoomHidden`, `actor.Service`.

Fix: DoorGen.cs sends op 36 with hi 1 (held) / 2 (free) and the lot id's low dword after each handler add / remove and for
every lot at arm, held = the lot has any handler other than `Sim.OnPortalPathPlan` (or the field is unreadable). Native keeps
32 held lots (overflow or low dword 0 = every lot held until the next arm); an ask on a held lot skips the memo entirely (no
answer from it, nothing kept, no compare) and counts `ts3native_doorgen.held` (new last field), logged as "asks on lots
with a privacy / mod door handler (live, not kept)" on the `door memo x misses:` line [verified: selftest `TestAskLent`
hold / free / overflow / arm cases]. Release v0.2.11 ships `DoorMemoX=-1`: no player exposure (v0.3.0 too). Rerun queues
`hhsearch-sv-1116-rerun1-1117` / `-rerun2-1117` (SV, repro `doormemox-privacy`: forced toilet + shower with NPCs on the household lot).

Lot key (2026-11-20) [verified, static, ledger §550 V-550-1]: the script key `(uint)Lot.LotId` is the native door key's key[0]
(the lot id's low dword, pushed at `0x64F4D3`), so a held lot matches its asks; the logged key `8B52BB30 6C11001B` of §6 and §7 is
low, high. The reflected field `Lot.mOnPortalPathPlanHandler` and the two EA subscribers are right [verified, static, V-550-2].
Not covered: a mod that writes the field without the event [inferred, I-550-4].

## 7. Second hit: same lot, same rooms (2026-11-20, chain-gcgate-rr3-1119, job sv-gcgate-ab)

Master build set (asi gchold2-1117, ts3fixes dalexlol-1117b; the §6 fix is not on master), SV-Spring, `DoorMemoX=3`, `doorgen` on,
0 mismatches in the first 5 counter lines (hits 15,550, miss 20,191, 328 bumps), then `FAIL door memo x` at game time 233,219,750 ms:
`door memo x shadow: answer 1, other job's 2 (121 ms before), key 8B52BB30 6C11001B 844E9B20 38B400F5 5F050F00 6C110020 0001 0004 00`.
Lot 0x6C11001B8B52BB30 and rooms 1 -> 4 are the same as §6; the actor differs (38B400F5844E9B20, scope household-or-seen, so it got the
live answer; its route 250862E5 then logged `routefail ... result 16` 204 ms, the "Razta Dog LookOutWindow" fail). Same lot, same
room pair in two sessions 4 days of game save apart: the stable thing is the home lot's bathroom door, which fits the §6 cause
(PrivacySituation handler answers from the asker's current interaction, no generation bump), not a lock-state change of a pet door
[inferred; a pet door lock change is bumped by `SetLockTypeAndOwner`]. Counters of the run show no `held` column (master build).
The harness guard killed the game 60 s after the self-check FAIL (the job stops on `ts3native hook failed`); EA wrote an xcpt
(`? at ?`, no stack) 1 s later and queue.ts filed it CRASH; it now reads KILLED (`bugfix/guard-kill-label-1120`; the 10-18 f4b and
travel rows had the same pattern, not reclassified).

Repro and correctness queue are the §6 ones, no second repro: `repros.json` `doormemox-privacy` (4 townies teleported into the home
bathroom, toilet forced twice, Go Here back in through the door) and `hhsearch-sv-1116-rerun2-1117` on the fix build, both on
`bugfix/doormemox-privacy-1117` (pending merge). Fix build: expect no `FAIL door memo x` and `(live, not kept)` > 0. Reproduced
on current code = the same FAIL on the master build with that repro; run it as the pre-fix arm only after the branch merges (the
repro entry is not on master). Until then SV timing queues that do not test DoorMemoX set `RouteSnap.DoorMemoX` 0
(`gc-idle-gate-time-1117-rerun4-1120`).

## 8. Third hit: EA 1.69, no privacy handler, a fresh Sim (2026-11-24, bundle-day-1124-correct EA, job bp-vbowner-correct-5)

Build 0.36.301-eabundle1124b (test/bundle-day-1124 b8ba526 + eafinal2 EA rows), EA app 1.69.47, Baseline-BP, repro
`vb-owner-delete-stress` (4 rounds of 25 objects placed on the home lot in live mode, deleted in Buy mode). `DoorMemoX=3`,
`DoorMemoXMs=250`, doorgen armed. After 1274 clean compares: `door memo x shadow: answer 1, other job's 2 (7 ms before), key
BEC1DF80 28590035 A70A1940 7AB100F5 C1469290 28590035 0019 0016 00`, i.e. lot 0x28590035BEC1DF80 (the home lot: vbowner-1 logs
`routefail buildbuy: lot 0x28590035BEC1DF80 object removed`), actor 0x7AB100F5A70A1940, door 0x28590035C1469290, rooms 25 -> 22,
initialLocked 0. Totals: hits 6321, miss 9647, inval 712, shadow 1275, mismatch 1. vbowner-1..4: 0 mismatches.

What it is not [verified, log]:
- Not an EA port gap: the door stub (`OK door stub 0x00650AB3 -> 0x00792D90`), `World_FireEvent` (`OK detour ... 0x00790440`), all
  39 routesnap IC slots (each slot checked against its expected function before it is hooked) and the event 59 slot
  (`0x011D72C4` -> `0x01231374`) are in place on this build; DoorMemoX has no other address.
- Not the privacy case of §6/§7: `asks on lots with a privacy / mod door handler (live, not kept) 0` in this session while
  vbowner-2..4 on the same build count 81..465, so the EA hold works and this lot had no extra handler.
- Not a placed object: the actor id is not one of the 4 rounds' first objects and object placement feeds no door rule.

Actor [inferred]: 0x7AB100F5A70A1940 is newer than round 3's objects (A70A0B10, 03:49:21) and older than round 4's (A70A5C60,
03:50:19): a Sim instantiated in between. The line before the FAIL is `ufplan: kind 0 172 ms why -1 src pet Stray Cat` (same tick).

Untracked inputs found [verified, static, EA Sims3GameplaySystems; same code on Steam]:
- `SimDescription.Household`: DoorGen bumped `Household.Add` / `Remove(SimDescription)` only. `Household.AddTemporary`,
  `RemoveTemporary`, `RemoveSimKeepHousehold` and `PetPool.AddPet` (`AddSilent` + `OnHouseholdChanged`) / `RemovePet` change it
  with no bump; 45 call sites use the silent / temporary forms. It feeds `IsStray` (pet household + pool), `IsGreetedOnLot`
  (null household = not greeted), `SelectedHousehold` locks and `CanSimTreatAsHome`. A stray that routes before its pool add
  gets "unlocked" (no stray rule), then "locked" once it is a stray: the 2 -> 1 seen here.
- `Sim.Service` (CommonDoor: police, firefighter, reaper, burglar, repoman, social worker ignore locks): no bump.
- Routing ICs outside slots 4-14: `DestroyPortalHandle` (16), `OnExitBuildBuy` (22, rooms may get new numbers: DoorShare already
  clears on it), `DoorLockStateChanged` (24, `RotatingWallDoor` calls it without `SetLockTypeAndOwner`), `BuildRoutingData` (29),
  `BulldozeLot` (30), houseboat lot create / delete (31, 32).

Fix (bugfix/ea-doormemox-1124): DoorGen.cs hooks `SimDescription.OnHouseholdChanged` (the only writer of `mHousehold` besides
`Delete`, which calls `Household.Remove` first) and `Sim.set_Service` (every `Worker.Service = x`; `Service.EndService` writes the
field after `value.EndService()`, whose situations clear it through the setter [inferred]), and logs the first 40 such
changes of instantiated Sims as `doorgen: household|service <name> 0x<sim id> household <kind>, stray <b>, service <type>`, so the
next mismatch's actor can be matched. Native `DmxGen` adds the 7 IC slots above (selftest `TestAskLent`: each one invalidates a
row). The self-check (every 8th valid row re-asked, one mismatch turns the memo off) stays. Which input flipped this ask is not
proven: the next mismatch, if any, names the actor through the new lines. Steam runs the same managed code and ships
`DoorMemoX=3`: the gap is on both editions.

Household: the memo answers only scope 2 / 3 jobs (unseen, seen NPC); household, selected and unknown scope ask live
[verified, `HcDoorEnter` and selftest]. A stale "unlocked" can let an NPC plan through a door it may not use, once, until the
re-ask or age cap ends the row; no household Sim gets a memo answer. Verify queue: `doormemox-verify-ea-1124`.
