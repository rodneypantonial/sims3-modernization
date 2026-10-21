# 105. Routing: portal answer table and the line-of-sight goal path (event 68)

Written 2026-10-18. Static only; the game, the probe server and the queues were not run. Goal (PLAN item 6, our own off-thread route search with no knowledge gaps): close the two design questions docs/92 left open. (1) How the search gets every portal answer without calling script while it runs: the exact inputs of each of the 12 decisions of docs/92 §5, every change that can flip them, and the cheapest correct scheme. (2) What `World_CanSeeObject` really does during goal building, whether its display-level change matters, and where that work must run.

Tools: Ghidra 12.1.4 headless decompiles on a private copy of the docs/89 project (`game-copy/ghidra-io-r`, copied from `ghidra-usf`, read-only runs); the docs/92 capstone listings of `0x789DD0` and `0xAC0570`; `game-copy/decompiled` (grep) and `tools/managed-callgraph.ts callers` (docs/78). Scratch stays in the session scratchpad (`io-r/`).

Tags as in AGENTS.md: **[verified]** = read in the disassembly, decompile or decompiled C# (address or file given); **[inferred]** = reasoning, to confirm with the docs/51 §39 rows.

## 1. Answer

- **Correction: goal building does not fire World event 68 [verified].** Both `Lot_SetDisplayLevel 0xAC0570` calls in `World_CanSeeObject 0x789DD0` (at `0x789EB5` and `0x78A218`) push four arguments `(level, 1, 1, 1)`. The fourth is a "quiet" flag: at `0xAC068B` it is tested and, when set, the function jumps to its epilogue `0xAC078B`, skipping the whole notify block (scene node refresh `Lot_UpdateObjectSceneNode`, `0xAB4B50`, the `0x41F76BB` message, `0x7920A0` = event 68, `0xCEF8B0`). docs/92 §1, §4, §5 row 12, I-92-1 and I-92-7 are wrong on this point, and so is the comment of the `0xAC0570` row in the symbols CSV (fires event 68 only when called without the quiet flag). The script `World.CanSeeObject` IC (`0x78F090`) calls the same function the same way, so script line-of-sight tests do not fire it either.
- **The display-level change is part of the answer, not a side effect [verified mechanism, meaning inferred].** `Lot_SetDisplayLevel` walks the lot's per-level shape table (`lot+0x378..+0x384`, count `lot+0x184 + 1`) and sets or clears bit `0x10000` of each level shape's flag word (`shape+0x2C`, through `0x686B30 -> 0x681F20`): set for levels up to the display level, clear above it. The line-of-sight ray (`PhysicsWorld_RayCastSegment 0x686FD0`, type `0x10`, last argument 1) adds `0x10000` to its collision mask. So with the camera showing a lower floor, upper-floor shapes are invisible to the ray; `World_CanSeeObject` raises the lot's display level to the target's level first (only when the target is higher than the current display level) and restores it after. Without the raise the test would see through upper floors.
- **Event 68 decision: keep radial line-of-sight goal building on the sim thread; no native port [inferred design].** The test reads the live physics world (loose quadtree, shapes, level-enable bits), the target's physics shape and lot component, and on the restore path calls the camera (`[0x11E9400]+0x14 -> +0x8C`, slot `+0x54`). It writes `lot+0x39C` and the level-shape bits, both read by picking and rendering. Goal building already runs on the sim thread in every planned design (docs/92 §8 item 5); for A10 (whole Replan-family ICs on the worker) a route whose destination set holds a radial destination and whose options have `ForcePhysicsBasedRaycast` stays synchronous. A native port would need a physics snapshot with level bits; the static count (§6.4: about 20 entry methods behind 8 route builders, two of them autonomous and frequent) does not pay for it.
- **Portal answers: answer natively, with one managed prologue per plan [inferred design].** Of the 12 decisions, the dispatcher (1) is replaced by a native evaluator that reproduces `Sim.OnPortalPathPlan` then the privacy handlers in order; 6 decisions read only Sim facts that a per-plan **Sim profile** carries (2, 3, 5, 8, 9, 10); the door rule (4) reads Sim facts plus door and lot fields that a **door mirror** and a **lot mirror** carry; 3 depend on the Sim's running interaction (6, 7, 11, plus the Store turnstile and VIP-room doors) and are answered in the same prologue for the lots the plan names. The prologue is a ts3fixes body patch of the four `ScriptCore.Route` plan methods (the only implementations, docs/103 §1), so it adds no native-to-managed transition. Search-time script calls: 0. Event 59 dispatches per plan: 0, versus 2-30 lazy ones today (docs/18 estimate) and about 20-100 for the eager per-plan table of docs/92 §8 (§5).
- **Mirror refresh needs few hooks**: `World.DoorLockStateChanged` (IC `0x788310`, already a natural chokepoint of every lock change), ts3fixes patches on the three door owner-list mutators, portal add/remove (docs/94 IC slot swaps), lot load/unload, and privacy situation start/end. Everything that changes often (posture, running interaction, age, traits, pregnancy, role, greeted lots, fire, venue hours) is read fresh in the prologue instead of hooked.
- **Guard and fallback** stay as docs/92 §8 and docs/103 §7: EA types and handler sets only; anything else (GoHere's handler, simler90 DLLs, MonoPatcher) runs today's path. A lot the search reaches that the prologue did not describe and that has a live privacy situation, box stall, horse jump, turnstile or VIP door falls back to the proxied event 59 call (A10's existing `Proxy` round trip).

## 2. What one portal answer is [verified]

`Portal_LockState 0x64F2C0` builds the native answer L0 first: locked if cost `>= 1e6`, else unlocked; locked if the Sim's AGS mask matches neither `+0x78` nor any variation (skipped when L0 is already locked); locked if the owner id is on the route's forbidden list. Then, if the portal owner's lot has `+0x8F` set, it **always** asks script, passing `initialLocked = (L0 == locked)`, the router's object id (`agent+8`), the portal owner id and the two rooms mapped through `0x6240C0` (swapped for the reverse direction). A script answer of 1/2/3 **replaces** L0 completely; 0 or > 3 clears `lot+0x8F` and keeps L0. The answer is stored per (owner id, room pair) with one byte per direction, for one `LotPortalSearch` call (docs/92 §3.2).

`Lot.OnPortalPathPlan` (`Lot.cs:4825`) makes one `PortalPlanEventArgs` with `Locked = initialLocked`, runs every handler of the invocation list on that same object (exceptions swallowed), and returns Locked (1) before Discouraged (3) before Unlocked (2); no handler gives `NoListeners`. Every lot adds `Sim.OnPortalPathPlan` at load (`Lot.cs:4031`); `PrivacySituation.RegisterCallbacks` adds its handler later and `CleanUp` removes it, so in vanilla the Sim handler always runs first. The `PortalPathPlan` add accessor calls `Route.SetPortalLockStatus(lot, true)` only for the first handler and the remove accessor `false` only for the last (`Lot.cs:1996-2017`).

`Sim.OnPortalPathPlan` (`Sim.cs:54341`), in order, on the shared args (S = the Sim, P = the portal object, rf/rt = rooms):

1. S is a tomb mummy: Locked = true.
2. S's assigned role is a `Proprietor` and P's lot is the role object's lot: Locked = false. This can unlock a portal that L0 locked (cost, AGS or forbidden list) [verified: plain assignment].
3. P is a `Door` (so not `NonAnimatedDoor`/`SecretDoor`, docs/103 §1): if Locked is already true, or the door's `IsAllowedThrough(rf, rt, S)` says restricted, Locked = true **and the door is added to `SimRoutingComponent.LockedDoorsDuringPlan`**. When Locked is already true, `IsAllowedThrough` is not called.
4. Firefighter pole, box stall, horse jump, elevator doors: restricted sets Locked (never clears it).
5. Diving board, only if not locked yet: Locked = toddler-or-below or visibly pregnant.
6. Ladder: Discouraged = elder or a ladder-avoiding trait.

Then each live `PrivacySituation` on the lot: if rt is the room of its `SyncTarget` (a Sim, read live: `SyncTarget.RoomId`) and the lot is the target's current lot, and S's current interaction (else transition interaction) passes the priority gate (autonomous with priority at most Privacy, or non-autonomous below UserDirected), Locked |= not `OtherSimCanIgnorePrivacy(S)` (8 overrides, docs/103 §3) (`PrivacySituation.cs:362`).

So the answer is a pure function of: L0 (native, per route), Sim facts, portal-object facts, lot facts, the Sim's current interaction, and the live privacy situations on the lot. It never depends on search progress (as docs/92 §5 said).

## 3. The 12 decisions: inputs and what flips them

Tier: **N** = native from the mirrors (§4.1, §4.2) and the Sim profile (§4.3); **P** = read fresh in the plan prologue (§4.3); **S** = evaluated in the prologue by calling the game's own C# for the lots the plan names (§4.4). "Flip" lists every state change found in the C# that can change the answer; for P/S inputs no hook is needed because they are read per plan.

| # | Decision | Exact inputs [verified, file] | What can flip it | Tier |
|---|---|---|---|---|
| 1 | Dispatcher | lot handler list (identity and order); `lot+0x8F` | handler add/remove (`Lot.PortalPathPlan` accessors; vanilla: lot load, privacy start/end); `SetPortalLockStatus` IC `0x74B490` | N (native evaluator), guard checks the list identity per lot in the prologue |
| 2 | Tomb mummy | `SimDescription.IsTombMummy` (`SimDescription.cs:2062`) | mummy creation (WA tombs) | P (profile bit) |
| 3 | Proprietor | `SimDescription.AssignedRole` is `Proprietor`, its `RoleGivingObject.LotCurrent`; P's `LotCurrent` | role assign/unassign: `AssignedRole` is a public field written at 5 sites, no setter to hook | P (profile: proprietor lot id) |
| 4a | Door (`CommonDoor.IsAllowedThrough`, `CommonDoor.cs:1149`) | lot hidden rooms (rf, rt); S's service type (fire, reaper, police, ... repoman ignore locks); S stray and not greeted on the lot (`Sim.IsGreetedOnLot`: home or real-estate lot, household greeted list, service/tourist per-Sim list); door one-way room `mOneWayDestinationRoomId` vs rt; S is a horse vs barn door / double gate / electronic gate + `CanLargeAnimalUse`; fire on lot (`Lot.IsFireOnLot`: fire manager or a Sim on fire) for barn doors or pets; S flag `IgnoreDoorLocks`; `mLockType` and `mLockOwner` (Anybody, Nobody, SelectedActor, SelectedHousehold, VIPRoom, Pets, OwnerList); owner list ids | lock changes: `SetLockTypeAndOwner -> OnLockChanged -> World.DoorLockStateChanged` (IC `0x788310`; also `RotatingWallDoor` calls it directly); owner list: `SetLockTypeOwnerList` (fills the list after the lock event), `AddToOwnerList`, `RemoveFromOwnerList`, `HouseholdSimChanged` (no lock event); hidden rooms: world-builder markers at load (`Lot.UpdateRoomMarkers`); greeted: `GreetSimOnLot`, `RemoveSimGreetedOnLot`, household changes; fire start/end; Sim service, flags, species, household: rare | N for Anybody, Nobody, Pets, SelectedActor, SelectedHousehold, OwnerList (door mirror + lot mirror + profile); S for VIPRoom (resort manager data) |
| 4b | `DoorSingleGender` override (`DoorSingleGender.cs:21`, does not call the base rule) | EP10 installed; lot is a resort and S is a resort worker or S's household owns the venue; S human and gender vs door gender; lot residential or **open venue** (`Lot.IsOpenVenue`: venue hours against `SimClock`) | venue hours (clock), real-estate purchase/sale, lot type | P (per named lot: open-venue and owned-venue bits; gender in profile) |
| 4c | `Turnstile` override (Store) | service type; carried child/pet and S each `SatisfiesAllLocks` (tickets, can buy tickets: funds) | ticket purchase, funds, posture | S |
| 5 | Firefighter pole (`FirefighterPole.cs:735`) | toddler-or-below; posture is `CarryingChildPosture` | age-up; posture change (frequent) | P |
| 6 | Box stall (`BoxStall.cs:3191`) | running interaction is an `IAllowUseOfGateInteraction` whose `TargetBoxStall()` is P | interaction start/end | P (profile: gate stall id, 0 if none) |
| 7 | Horse jump (`HorseJump.cs:1620`) | current interaction or its linked interaction is an `IHorseJumpInteraction` | interaction start/end, link set | P (profile bit) |
| 8 | Elevator doors (`ElevatorDoors.cs:1448`) | toddler-or-below, horse, wild animal; lot `HasVirtualResidentialSlots` (saved flag and residential); `CanTravelInElevator` (home lot, greeted, ...) | age-up; greeting; apartment flag at load | P (per named lot: elevator-allowed bit); N elsewhere only when the lot has no virtual residential slots |
| 9 | Diving board (`Sim.cs:54374`) | toddler-or-below; `IsVisuallyPregnant` (flag field) | age-up; pregnancy show | P |
| 10 | Ladder discourage (`Ladder.cs:453`) | elder; any of `kTraitsThatAvoidLadders` | age-up; trait add/remove | P |
| 11 | Privacy (`PrivacySituation.cs:362`) | target Sim's current room and lot; S's current or transition interaction, its `Autonomous` and priority; virtual `OtherSimCanIgnorePrivacy(S)` (postures, carried state, docs/103) | situation start/end (`RegisterCallbacks`/`CleanUp`), target Sim room change, S's interaction start/end/priority change, posture | S, only for lots with a live privacy situation |
| 12 | Line of sight in goal building | not a portal answer: see §6 | — | stays on the sim thread |

Notes [verified unless tagged]: `Sim.IsStray`, species, gender and `IsBonehilda` are `SimDescription` facts that change only with the Sim. `ToddlerOrBelow` and `Elder` read the age flags. Hidden rooms change only through world-builder markers when the lot loads or is rebuilt [inferred: no other writer of `mHiddenRooms` found]. `IsOpenVenue` makes the single-gender door rule time-dependent: the same Sim and door can flip at the venue's opening hour, so no per-(Sim, door) cache without a clock hook is correct for it.

## 4. The scheme

### 4.1 Door mirror (per script-relevant portal object)

**Corrected 2026-10-18 (§10.3):** the mirror is built lazily per named lot at START after invalidation, not when the portals are added, and a ts3fixes postfix on CommonDoor.ImportContent joins the refresh hooks; save load restores the lock fields by reflection with no event (§10.1).

Built when a portal object's portals are added (lot load, `Route.AddPortal` family, door slots; docs/92 §2.2) and refreshed by: `World.DoorLockStateChanged` (IC slot swap of `0x788310`, docs/94 §2.3 pattern), ts3fixes patches of `CommonDoor.AddToOwnerList`, `RemoveFromOwnerList` and `HouseholdSimChanged` (the `Patch` helper in `mods/ts3fixes/src/Fixes.cs`), the door's destroy (docs/76 object event lists). Fields: kind (`Door` subclass, `DoorSingleGender`, `Turnstile`, pole, stall, jump, elevator doors, diving board, ladder, other), lock type, lock owner SimDescription id and that owner's household id, owner id list, one-way room, barn/double-gate/electronic-gate flags and `CanLargeAnimalUse`, single-gender door gender. The refresh itself is one managed read of these fields on the sim thread inside the hooked call.

Gap closed by the order of events: `SetLockTypeOwnerList` fires the lock event before it fills the list; the owner-list patches refresh again after, so the mirror ends right. A SelectedHousehold door whose owner changes household is caught by `HouseholdSimChanged` only for the lot's own household [inferred]; to stay exact, the prologue re-reads the owner household for SelectedHousehold doors on the named lots (one field read per such door; player-set, rare).

### 4.2 Lot mirror

Per loaded lot: hidden room ids (at load and rebuild), apartment flag, lot type and commercial subtype, handler-list identity, and the set of live privacy situations (room of the target Sim is read per plan, not mirrored). Refresh: lot load/unload, ts3fixes patches of `PrivacySituation.RegisterCallbacks` and `CleanUp`. Handler-list identity is free: `Delegate.Combine`/`Remove` return a new delegate object on every add and remove [inferred for Mono 1.2.3; probe I-105-6], so the prologue compares one reference per named lot against the vanilla list it saw last.

### 4.3 Plan prologue and Sim profile (tier P)

A ts3fixes body patch of `ScriptCore.Route::Plan`, `Replan`, `PlanFromPoint`, `ReplanFromPosition` runs on the sim thread before the IC: it reads the route's follower Sim and writes a small fixed record into a buffer the native side reads (ts3native already shares state with ts3fixes; exact channel is a build choice). Contents:

- Sim: object id, SimDescription id, household id, home lot id, greeted lot ids (household list, or the per-Sim list for service and tourist households), real-estate lot ids, species/horse/pet/wild/human, gender, toddler-or-below, elder, tomb mummy, bonehilda, visibly pregnant, `IgnoreDoorLocks`, stray, service type, resort worker, proprietor lot id, ladder-trait bit, carrying-child bit, gate stall id, horse-jump bit, privacy-gate bit (the priority test of row 11).
- Named lots: the start lot, every destination's lot (destination set, docs/92 §6) and the lot of a given start point. Per named lot: fire on lot, open venue now, household owns the venue, elevator-allowed, handler-list identity.
- S-tier answers for the named lots (§4.4).

Cost: about 30 field reads and a few short list walks, in the interpreter, once per plan [inferred: comparable to one of today's event 59 dispatches, which also allocate].

### 4.4 Per-plan managed answers (tier S)

For each named lot, the prologue calls the game's own methods for the few objects whose answer needs live interaction or resort state: each live privacy situation (`OtherSimCanIgnorePrivacy(S)` plus the room/lot test, giving a locked-room set), each turnstile (`IsAllowedThrough` both ways), each VIPRoom-locked door, and `CanTravelInElevator` on apartment lots. Box stalls and horse jumps need no call (profile bits). In vanilla saves these objects are absent from most lots, so the usual S cost is zero calls [inferred].

### 4.5 Native evaluator (worker, during the search)

For portal P in direction (rf, rt) on lot L: compute L0 as today; if L has `+0x8F` clear, return L0 (no handlers: today's behaviour). Else apply §2's order with mirror, lot and profile data, then the privacy locked-room set if L has one. Record (door, locked) pairs where step 3 locked a door: the collect step adds exactly those doors to `LockedDoorsDuringPlan` on the sim thread (docs/93 E3). If L is not a named lot and its mirror lists any S-tier object or a live privacy situation, the evaluator returns "ask" and the job uses today's proxied event 59 call for that portal (or falls back to a sync plan).

`NoListeners` cannot happen under the guard (the Sim handler is always in the list); a lot whose list is empty fails the guard and runs today's path, which clears `+0x8F` natively as before.

### 4.6 Guard

As docs/92 §8 condition 2 and docs/103 §7: handler list per named lot is exactly {`Sim.OnPortalPathPlan`, `PrivacySituation.OnPortalPathPlan` x n} in that order; the Sim, its routing component, posture, and every portal object on the named lots have EA runtime types from EA-MVID assemblies; the event 59 delegate is EA's `LotManager.PortalLockedCallback`. The door mirror stores the runtime type, so the per-plan guard is one check per named lot, not per portal [inferred]. MonoPatcher present: always fallback.

### 4.7 Why not the other schemes

- **Eager per-plan table (docs/92 §8):** asks every portal of every touched lot in both directions through the full event path, per plan. Correct, but more calls than today (§5).
- **Per-(Sim, portal, direction) cache with invalidation:** needs hooks for posture, interaction, age, traits, greeting, role (a public field with no setter), fire and the clock (venue hours). Several have no single chokepoint; missing one gives a wrong answer that persists across plans. Reading those inputs per plan is cheaper than hooking them.
- **Lazy fill on first need:** only exists as a call during the search (today's proxy). Kept as the fallback for undescribed lots.

## 5. Script calls per plan

Portal counts per lot are not measured [inferred estimate]: a family house has about 5-15 doors and gates (one portal each, two directions), one or two staircases (helper portals, several per staircase), and sometimes a pool ladder; community lots 5-25 doors; Late Night apartment lots add elevator doors per floor. Event 59 is asked for every portal on a `+0x8F` lot, stairs included (the handler runs for all and only the type tests skip). Probe I-105-5 counts them.

| Scheme | Event 59 dispatches per plan | Other managed work per plan | Thread round trips while searching |
|---|---|---|---|
| Today (sync, lazy, cache per lot search) | 2-30 (docs/18 estimate: portals the search costs) | — | 0 (sync) or 2-30 (A10 proxy) |
| Eager table at submit (docs/92 §8) | about 20-50 per lot x 1-2 lots = 20-100 | — | 0 |
| Per-Sim cache with hooks | fill once per (Sim, lot) visit, then only after a hook | hooks on many setters | 0 |
| **This doc: native evaluator + prologue** | **0** | 1 prologue (about 30 reads); S-tier calls only for privacy, turnstile, VIP doors, apartment elevators on the named lots (0 on most lots) | 0, except "ask" on undescribed lots with S-tier objects |

At the 2026-10-18 night run's rate (`asyncStarted` +14,734 in 10 minutes, about 24 plans per second, docs/data/results/2026-10-18-queue-clean-night-2.md), replacing 2-30 dispatches per plan with one prologue removes roughly 50-700 managed dispatches and their `PortalPlanEventArgs` allocations per second of sim time [inferred: the dispatch count per plan is the docs/18 estimate].

## 6. Line of sight in goal building (the "event 68" path)

### 6.1 Where it runs [verified]

`Route_PlanCommon` calls `Route_GenerateDestinationCandidates 0x751FF0`, which calls each destination's slot `+0x10`; for a radial-range destination (vtable `0x100423C`) that is `0x75BE60 -> 0x75AFA0 -> GoalPoint_Validate 0x759B20`. There, when `RoutingOptions_Test(0x2000000)` (`ForcePhysicsBasedRaycast`) is set and the level/lot constraint check `0x74CB80` passes, it calls `World_CanSeeObject (eye, targetId, 0, ignoreList, &blocker)` with: eye = the candidate goal point raised by the destination's vertical offset (destination `+0x70`; C# `mfRayCastVerticalOffset`, 1.6 in most callers), target = destination `+0x50/+0x54` (C# `mTargetObject`), ignore list = the route's ignore-for-route list (`0x81C8D0`), see-through flag 0. Not visible rejects the point and, if the result has no blocker yet, records the blocking object's id there.

### 6.2 What it reads [verified]

`ObjectService_FindById` on the target, its physics component (`g_CompBit_Physics`, shape at `+8`, centre at shape `+0x10`) and lot component (lot id `+8/+0xC`, level `+0x18`, `0x7FFFFFFF` = none); `WorldManager_FindLotById` (through `0xC62D40`, which also checks the lot's type through slot `+0x40`) and the lot's display level `+0x39C`; then repeated `PhysicsWorld_RayCastSegment` calls from the eye to the target's centre. Each hit: the target's own id = visible; an id on the ignore list = restart the ray just past the hit; a shape with flag bit 2 and type `0x65BAD16` = a see-through test `0x789A80` (lot wall data `lot+0x290`, `Lot_WorldToLocal2D`, `0x788F20` twice), and if it passes the ray continues past the hit; anything else = blocked, with that shape's owner id as the blocker. The window/door-opening branch (`0xB12360`, `0xB08B20`, `0x784B50`) only runs with the see-through flag set, which goal building never passes.

### 6.3 What it writes [verified]

Only when the target's level is above the lot's current display level: `Lot_SetDisplayLevel(targetLevel, forced, 1, quiet)` before the rays and `(oldLevel, forced, 1, quiet)` after. Each call clamps the level to the lot's range (`+0x17C`, `+0x180`; `0xB95DF0` takes lock `0x106FB00` for a per-level test), writes `+0x39C`, and rewrites bit `0x10000` on every level shape (§1). The restore call (lower level) first calls the camera object's slot `+0x54` (`[0x11E9400]+0x14 -> +0x8C`, the camera path of docs/22) [verified call, purpose inferred: floor change refresh]. Quiet suppresses scene-node updates, the message and event 68. When the target is at or below the display level, the test is read-only.

### 6.4 How often (static) [verified sites, frequency inferred]

`ForcePhysicsBasedRaycast` is set at 10 sites in the script assemblies (one of them clears it again); all build a radial-range destination:

| Route builder | Reached from | Pack (inferred) | Frequency (inferred) |
|---|---|---|---|
| `ViewObjects.CreateRouteToObject` (via `ViewObjects.RouteToObject`) | `ViewObjects.Run` (view painting/sculpture), `TraitFunctions.CritiqueArt`, `Photography.TakePhotoOfObject`, `Reminisce`, `ScanWorkOfArt`, `MoonDial.CheckHoroscope`, `AntFarm.Watch`, `PetStone.Play`, `EyeOfHorus_Teleport`; directly `Trampoline.Play`, `InteriorDesigner.EvaluateRenovation`/`TakeMeasurements` | base and several EPs | **high**: viewing art is a common autonomous fun interaction |
| `PetSurfaceComponent.CatJumpHelper.RouteToForEntry` | cats jumping onto counters, tables and other surfaces | Pets | **high** in cat households |
| `PhotographableUtil.GetRouteForTakePhoto` | `AbstractArtObject`, `SoulpeaceStatue` photo routes | WA, Supernatural | low |
| `Aromainator.Meditate` (own copy of the view route) | meditate near the aromainator | store/EP object | low |
| `Windows.PlanRouteToExtinguish` (with `DoLineOfSightCheckUserOverride`) | window interactions in `Windows.cs` | Seasons | low |
| `TombTrap` `RouteToConeNearPoint` | trap escape | WA | low |
| `InventingSkill` detonate `RouteToTarget` (x2) | explosives | Ambitions | low |
| `Sim` celebrate `Run` (`RouteToDynamicObjectRadius(..., ForcePhysicsBasedRaycast)` when tuning routes to the target) | celebrate interactions | base | medium |

The level raise happens only when the player has cut the viewed lot below the target's floor [inferred: display level of lots not being viewed is their top level]. Probe I-105-3 counts both.

### 6.5 Decision

Keep the radial line-of-sight test where it is: in sim-thread goal building. For the snapshot search nothing changes (it starts after goal building). For A10, the submit-time rule is: destination set holds a radial-range destination **and** RouteOption bit `0x2000000` is set -> plan synchronously (or run goal building on the sim thread and only the search on the worker, the docs/92 §8 split). No event 68 handling is needed in either case. A native line-of-sight over our snapshot would need the physics loose quadtree, the level-enable bits as they would be after the raise, the see-through shape types and the lot wall data; not worth it for these plans.

## 7. Verified vs inferred

Verified: the quiet flag and the skipped notify block in `0xAC0570`; the four-argument pushes at both call sites; the level-shape bit writes and the ray's `0x10000` mask bit; what `World_CanSeeObject` reads and when it raises the level; the camera call on the restore; `GoalPoint_Validate`'s arguments; the 10 `ForcePhysicsBasedRaycast` sites and their callers; `Portal_LockState`'s L0 and always-ask behaviour; the dispatcher's shared args and result order; every handler's inputs in §3 with file and line; the lock-change, owner-list and privacy registration code paths.

Inferred: that the `0x10000` bit means "collides for masks that ask for it" (the ray's mask match was not decompiled); that level shapes are per-floor walls and floors; that the camera slot `+0x54` is a floor refresh; that unviewed lots sit at their top display level; portal counts per lot and every per-plan cost in §5; the handler-list identity check on Mono 1.2.3; the household-change gap of SelectedHousehold doors; pack attribution; frequency of each line-of-sight caller.

## 8. Ledger

Rows I-105-1 to I-105-8 in docs/51 §39.

## 9. Offline closure of I-105-2, I-105-6, I-105-7, I-105-8 (2026-10-18)

The four rows the live value probe could not measure (docs/108 §4). Offline only: no game run, no ts3native change. Two checks, both fail on any mismatch and both have a self-check:

- **Emulator** (docs/63): `tools/emu/specs/los_level_mask.py` runs the game's own `Lot_SetDisplayLevel 0xAC0570` and the physics leaf visitor `0x680F80` from the exe image on constructed lots, groups and shapes, against a Python model. Command (a worktree has no `game-copy/`): `TS3W_EXE=<main checkout>/game-copy/bin/TS3W.exe python tools/emu/run.py tools/emu/specs/los_level_mask.py`. Self-check: the spec's tally must show cases whose answer depends on the level bit (a model without the bit fails those), plus `run.py --self-check` for the harness.
- **Script side**: `node tools/portal-answer-check.ts` (reads the `tools/managed-callgraph.ts build` index of the game's script DLLs and mscorlib, per-method calls and field reads/writes; options `--index`, `--decompiled`). Each check is an exact expected set: a new writer, caller, override or callee fails it and prints the difference. `node tools/portal-answer-check.ts --self-check` runs the checks on a synthetic index, which must pass, and again with a planted extra writer, which must be caught.

| Item | Verdict | Check | Result (2026-10-18) |
|---|---|---|---|
| I-105-2 (raise matters) | **verified (emulator)**; one layout link inferred (below) | `los_level_mask.py` | 321/321 cases, visitor blocks 15/15, 152 cases where the level bit decides the answer |
| I-105-6 (delegate identity) | **verified (static: IL index + the game's mscorlib)** | `portal-answer-check.ts` I-105-6a-f | PASS |
| I-105-7 (evaluator = script) | **input half verified (static)**; equivalence **open** (needs the evaluator) | I-105-7a-f; shadow read below | PASS |
| I-105-8 (mirror refresh covers every change) | **verified (static)**, with one missed writer: `CommonDoor.ImportContent` | I-105-8a-e | PASS (the gap is part of the expected set) |

### 9.1 I-105-2: the raise changes the answer [verified, emulator]

- **The ray's mask rule** [verified, disassembly + emulator]: the line-of-sight type `0x10` maps to mask `0x8FFFF` (`Physics_MaskForType 0x686B40`, switch table `0x686B80`), and `PhysicsWorld_RayCastSegment 0x686FD0` ORs `0x10000` into it when its fifth argument is set (`0x6870AB`; `World_CanSeeObject` passes 1). The leaf visitor `0x680F80` skips a shape unless, for every group in its parent chain (shape `+0x50`, then group `+0x10`) and for the shape's own word `+0x34`, `(word & mask)` is greater than `(mask & 0xFFFF0000)` as unsigned numbers: every high mask bit present and at least one low bit. Only then come the bounding-sphere test (squared distance from the ray < shape `+0x30`), the shape's own ray test (vtable `+0x10`) and the nearest-hit update.
- **The writer** [verified, emulator]: `Lot_SetDisplayLevel` passes each per-level entry of the lot (deque `lot+0x378`/`+0x37C`, block map `+0x384`, count `+0x184 + 1`) to `PhysicsWorld_SetShapeLevelEnable 0x686B30 -> 0x681F20`, which sets bit `0x10000` of that group's word `+0x2C` for levels up to the display level (after clamping to `lot+0x17C`..`+0x180 + 1`, the `+1` when `lot+0x2AC` is empty) and clears it above. The camera path (`0x96B390`, `0xF20C20`) runs exactly when the new level is lower than the current one, so on the restore of a raise, never on the raise itself.
- **Result**: with the line-of-sight mask, a wall under a level group above the display level is invisible to the ray; after the raise to the target's level the same wall blocks it; with the plain mask the bit does not matter. Edge cases in the spec: upper-floor wall with the camera on the ground floor (visible without the raise, blocked with it), basements (minimum level -1), clamp above the top level, nearest hit among several walls, shapes without a parent or under the root group, own word without low bits, sphere miss, ray-test miss, non-positive distance, empty leaf. The restore puts every group word back.
- **Still inferred**: that a lot's wall and floor shapes of level k have level group k in their `+0x50` chain (the layout fits: the same `+0x2C` word and chain the visitor reads, the per-level entries passed to the writer are such groups; the writer of shape `+0x50` was not traced). Live check if ever needed: read `+0x50` of a wall shape the ray hits on an upper floor and compare with the lot's entry for that level.
- **Correction to §6.4**: docs/107 §5.2 (I-107-14) found that lots start at display level 0 and keep the last level set; the "unviewed lots sit at their top level" inference of §6.4 and §7 is wrong. So the raise runs for every target above the ground floor of a lot the camera never cut, more often than §6.4 assumed; the decision of §6.5 (keep the test on the sim thread) does not change. I-105-3 counts it.

### 9.2 I-105-6: the handler-list reference is a version of the handler set [verified, static]

- Only the two accessors write `Lot.mOnPortalPathPlanHandler`; the add accessor uses `Delegate.Combine`, the remove accessor `Delegate.Remove` (6a, 6b).
- In the game's mscorlib (MVID pinned, 6f): `Combine` with an empty list returns the added delegate itself (each `+=` site creates a new delegate object), and with two non-empty lists builds a new chain of clones of every node of both. `Remove` returns null when the result is empty, the same reference when the value is not in the list (the set is unchanged), and otherwise a new chain of clones. The chain link `MulticastDelegate.prev` is written only by the constructors and, on freshly cloned nodes, by `CombineImpl`, `RemoveImpl` and `GetInvocationList`, which all call `Clone` (6c, 6d); the target and method fields only by the constructors (`method_info` is a lazy cache, 6e). A published delegate object therefore never changes its handler set.
- So: **same reference implies the same handler set**, and every change of the set gives a different reference or null. The converse does not hold (remove and re-add the same handler gives a new reference with an equal set); that only causes one extra re-describe, never a wrong answer.
- Design condition [inferred]: the prologue keeps the last-seen list as a managed reference (a ts3fixes static) and compares with `ReferenceEquals`. A raw pointer kept only on the native side could match a freed and reused address, because Mono 1.2.3's collector does not move objects but does reuse their memory.
- Side finding [verified, IL]: the dispatcher `Lot.OnPortalPathPlan` calls `GetInvocationList` on every dispatch, which clones every handler node and allocates an array. Each of today's 2-30 event 59 dispatches per plan (§5) allocates n + 2 objects (n handlers, the array, the args), not only the args object.

### 9.3 I-105-7: inputs complete [verified, static]; equivalence open

- The direct calls of the three managed pieces equal the inputs of §2 and §3: `Sim.OnPortalPathPlan` 22 callees (rows 2-10), `PrivacySituation.OnPortalPathPlan` 13 (row 11), the dispatcher 5 (one args object, every handler, Locked before Discouraged) (7a-c). The door fields read directly by `CommonDoor.IsAllowedThrough` are the four of the door mirror (lock type, lock owner, one-way room, owner list) (7f).
- Virtual targets: 22 `IsAllowedThrough(int, int, Sim)` implementations in the DLLs (7d); the handler's virtual call on a door reaches `CommonDoor`, `DoorSingleGender`, `Turnstile`, `NonAnimatedDoor` and `SecretDoor` (`node tools/managed-callgraph.ts overrides` on the `CommonDoor` method; the last two fail the handler's `Door` type test, docs/103 §1), so rows 4a-4c are complete. `OtherSimCanIgnorePrivacy` has the base and 8 overrides (docs/103 §3) and `DiscourageSimFromUsing` only `Ladder` (7e). A new override or callee in a patch would fail the check.
- **Open**: that the native evaluator gives the same answer needs the evaluator, which is not built. Exact check once it exists (shadow mode, no behaviour change): in a ts3fixes patch of `Lot.OnPortalPathPlan`, after the vanilla body, evaluate natively with the same (Sim object id, portal object id, rooms from/to, initialLocked, lot id) and log every pair whose answer (1/2/3) differs, with the evaluator's input record; at plan end compare the plan's `LockedDoorsDuringPlan` additions with the evaluator's recorded (door, locked) pairs. Repro and expected result as I-105-7.

### 9.4 I-105-8: every door-mirror change [verified, static], one writer missed by §4.1

- Lock fields (8a): `mLockType` is written by `SetLockTypeAndOwner`, the constructor and **`CommonDoor.ImportContent`**; `mLockOwner` only by `SetLockTypeAndOwner`; the one-way room by `SetLockTypeAndOwner` and the constructor; the owner list field only by the constructor. `DoorSingleGender`'s gender and the `Turnstile` lock list are set only in their constructors.
- Owner list contents (8b, 8c): mutated only by `SetLockTypeOwnerList`, `AddToOwnerList`, `RemoveFromOwnerList` and `HouseholdSimChanged`. The lock interaction (`CommonDoor+LockDoor.Run`) reads the list for its picker and mutates only its own lists before calling `SetLockTypeOwnerList`; nothing calls the `OwnerList` getter, so no alias can change the list elsewhere.
- Lock event (8d, 8e): `World.DoorLockStateChanged` is called by `OnLockChanged` (reached from `SetLockTypeAndOwner`) and by `RotatingWallDoor` at move start and end (a refresh with no lock change: harmless). **`ImportContent` raises no event**: it reads the lock type from the import stream and only updates the visual state.
- Correction to §4.1: add a ts3fixes patch of `CommonDoor.ImportContent` (refresh after the original) to the refresh hooks, or have the prologue re-read the lock type of doors on the named lots. Whether the import runs after the door's portals are added is not known [inferred: possible for objects placed from a saved lot or the library]; the patch covers both orders.

### 9.5 Ledger

Rows I-105-9 to I-105-14 in docs/51 §50.

## 10. Door lock state outside the lock event: import, load and copy paths (I-105-14, 2026-10-18)

Static only, no game run. Sources: the decompiled script DLLs (Sims3GameplaySystems, Sims3GameplayObjects, Sims3StoreObjects, ScriptCore) and `node tools/managed-callgraph.ts callers` on the IL index. Refreshes §4.1 and §9.4 and answers I-105-14. Ledger rows in docs/51 §55.

### 10.1 Writers of door-mirror state that raise no lock event [verified, static unless tagged]

The §4.1 mirror fields are lock type, lock owner, owner list, one-way room, the `DoorSingleGender` gender, plus kind and type flags (fixed per class). §9.4 listed the IL field writers (`stfld`). Two kinds of writes do not show up there: element stores into an array held in a field, and reflection.

| Path | What it writes | Lock event | Matters for the mirror |
|---|---|---|---|
| `CommonDoor.ImportContent` (`CommonDoor.cs:1509`), reached through the chain of every door subclass (`Door`, `NonAnimatedDoor`, `ElectronicGate`, `RotatingWallDoor`, `SecretDoor`, ...) | `mLockType` only. It reads key `0x45A3586F` with default 3 (`Anybody`) and keeps the value when the enum defines it. `ExportContent` writes the key only for `Anybody`, `Nobody` and `VIPRoom`, so an imported door ends as `Nobody`, `VIPRoom` or `Anybody`: `SelectedActor`, `SelectedHousehold`, `Pets` and `OwnerList` locks come back as `Anybody`. Owner, owner list and one-way room are not in the stream, so a new object keeps its constructor values (null, empty, `36863`) | none: it only calls `UpdateLockVisualState` | **yes** |
| `ElectronicGate.ImportContent` | after the base chain, reads `mIsOpen`; an open gate calls `SetLockType(Anybody)` | open gate: yes (after the import write). Closed gate: none | the closed-gate case is the row above |
| `Turnstile.ImportContent` (Store) | replaces elements of `mLocks` with new `LockingSpecification` objects. These are array element stores, so I-105-8a ("`mLocks` only in constructors") holds for the field but not for its contents. The add-lock code of the turnstile interactions stores elements the same way | none | **no**: the turnstile is answered per plan (row 4c, tier S) and has no mirror field |
| **Save load** (`PersistReader`, ScriptCore) | every field of a `[Persistable]` class and its bases, through `FieldInfo.SetValue`. `GameObject` is `[Persistable]`, `PersistedClass` walks the base types, and none of the door lock fields is marked `[Persistable(false)]`, so `mLockType`, `mLockOwner`, `mOwnerList`, `mOneWayDestinationRoomId`, the gender and the turnstile locks come back this way [mechanism verified; that these exact fields are in the persisted set is inferred from the missing opt-out] | none | **yes**. The IL index sees no `stfld` for these writes, so I-105-8a cannot list them |
| World builder data (`CommonDoor.LoadWorldBuilderData`, world-builder load only) | `SetLockType(...)`, then `ResortManager.AddDoorToVIPRoom` for VIP doors | yes (through `SetLockTypeAndOwner`) | covered by the event |
| Script copy: `GameObject.Clone` -> `CloneScriptData` and `CloneObjects` -> `CloneScriptDataList` (export, then import into an object created at `Vector3.OutOfWorld`) | whatever `ImportContent` writes (the first row) | none | **no door reaches it** [inferred from the callers' types]: the callers are `TraitFunctions+SwipeSomething`, `Stove`, the books' and `SheetMusic`'s `CloneForOmniPlant`, `Potion+MixPotionIntoDrink`, `BunkBedContainer.Clone` and `GameplayMovingModel.PackFurniture`. `PackFurniture` filters with `Lot.CanObjBePackedOrSold`, which rejects build-category objects unless they override `PackForMovingOverride`, and no door class does. A clone also has no portals until it is placed |

Writes on portal objects that are not mirror inputs (for completeness, no action):

- `Door.ImportContent` reads `mIsNPCDoor`, picks up a velvet rope in containment slot 0 and calls `RecalculatePortalCost`, which only calls `Lot.RequestPrivacyUpdate`. `mIsNPCDoor` is read by the `EnterDoor` test and by the apartment door-to-virtual-resident assignment, not by `IsAllowedThrough`. I-105-11 lists the direct reads of `IsAllowedThrough`: the four mirror fields only.
- `NonAnimatedDoor` (auto-shut flag) and `SecretDoor` (discovered flags) import their own state; neither is ever asked by the door handler (it tests `is Door`, docs/103 §1).
- `Lot.ImportContent` imports the door list for virtual residents (`PostImportDoorFixup` fills `mDoorsToVirtualResidentDictionary`). The door handler does not read it.
- `Ladder.ImportContent` calls `AddPortals` and `ElevatorDoors.ImportContent` calls `RefreshPortal`. Both are native portal writes and go through the docs/94 E9/E10 IC slots.

### 10.2 I-105-14: can the import run after the door's portals exist?

**Verdict: yes for bin/library lot placement and Edit Town lot rotation [managed order verified; portal timing inferred]. Save load and travel arrival raise the same problem through the reflection restore of §10.1, not through `ImportContent`.**

- **Lot from the bin or library, Edit Town place and rotate.** Callers: `Bin.PlaceLot` (from `PickObjectTask.ProcessClick`) and `BinCommon.PlaceLotContents` (from `EditTownModel.PlaceLotContents`, `EditTownModel.RotateLotContents` and `PlayFlowModel.PlaceLotContents`) -> `DownloadContent.ImportLotContents`. That method first calls the native `DownloadContent_ImportLotContentsImpl 0x997540`. Only after the IC returns does it call `ImportManagedContent` -> `Lot.ImportContent`, which calls `ImportContent` on each lot object [verified, managed code and IL callers]. So every door already exists, is placed and has run its script `Init` when its lock type is written. The IC's static reach takes the routing world lock and fires world events synchronously (`docs/data/ic-safety.csv` row `DownloadContent_ImportLotContentsImpl`, verdict UNSAFE, `World_FireEvent`) [verified reach]. Door portals are made by the native door footprint `0x80CB40` from the `RoutingSlot_Door%i_Front/_Rear` slots when the lot's footprint level is rebuilt (docs/92 §2.2, docs/84 §4.1 F-A). That rebuild may run inside the IC or on the first later use [inferred]. In both cases, a mirror built when the portal is added would read the constructor default `Anybody`, or the value the object was created with, for a door saved as `Nobody` or `VIPRoom`.
- **Travel arrival.** The destination world loads from its save (the save load below). The travelling household comes in through `Bin.ImportHouseholdForTravel` -> `DownloadContent.ImportHouseholdContents` and `ImportInventory`: Sims, households and inventories, not lot doors. A door inside an inventory has no portals [inferred], so it is read correctly when it is placed.
- **Save load.** `ImportContent` is not used. The lock fields come back through `PersistReader` (§10.1) with no event. The lot's native portals come from the routing lot data (`RoutingWorld_LoadLotData 0x82CA10`, docs/92 §2.2), while the managed proxies are created later from the script host's pending list (docs/50 C3). So the portals most likely exist before the managed fields do [order inferred; the drain of the pending list is not traced]. A mirror built when the portals are added would then read an object that does not exist yet or still holds defaults.
- **Edit Town household move** moves households only. **Moving a lot** is a bin export followed by a bin place, which is the first bullet. **Moving with furniture** never packs doors (§10.1).

### 10.3 What the docs/94 snapshot must do

1. **Do not build the door mirror when the portals are added.** The native portal add (E9), the world-load epoch (docs/76 §2.2, events 5, 7, 9 and `0x0A`), lot added (`0x0D`) and the build/buy exit slots only mark the lot's door mirror invalid. The prologue at START (§4.3) builds the door mirror of each named lot that is invalid: one managed read of the §4.1 fields per door, on the sim thread. No plan starts while a synchronous import or load is still running, because both run on the sim thread and the plan's START is a later sim-thread call [inferred from docs/50 §1: every create path is on the sim thread]. So the lazy build sees the imported and restored values. This covers save load and travel without a new hook.
2. **Add one refresh hook: a ts3fixes postfix on `CommonDoor.ImportContent`** (the `Patch` helper in `mods/ts3fixes/src/Fixes.cs`). It refreshes that door's mirror entry, or marks its lot invalid, after the original body. It is needed because nothing proves that a bin placement or an Edit Town rotate bumps the epoch. Which world event ids the import IC fires is not traced, and the docs/76 Edit Town exit is only the managed `kExitInWorldSubState` fallback. Cost: one call per imported door, player-driven, rare. A postfix on `CommonDoor.ImportContent` also covers `ElectronicGate`'s closed case and every subclass, because they all chain to it.
3. **docs/76 covers only part of this.** Its epoch (world load, travel, build/buy exit) covers the reflection restore as long as the door mirror is built lazily (point 1). None of its hooks see managed field writes, and none is proven to fire for a bin lot import. Its object events (`0x21`, `0x0E`, `0x12`, docs/79) report position and parent changes, not lock fields.
4. **Turnstile, VIP and owner-household reads stay as in §4.4** (tier S per plan). The array writes in §10.1 need nothing.
5. **Check limit.** `tools/portal-answer-check.ts` I-105-8a sees only `stfld`. Reflection restores and array element stores are invisible to it. The shadow diff of I-105-8 (mirror vs script read at every plan) remains the live proof and should run with the repros of I-105-18.

Correction to §4.1: replace "Built when a portal object's portals are added" with the lazy per-lot build of point 1, and add the `ImportContent` postfix (point 2) to the refresh hooks.

### 10.4 Native side of I-105-16 and I-105-17 (2026-10-18, static)

Static only, no game run, no native change. Tools: Ghidra headless (`DecompileTo`, `CallerTree`, `StringXrefs`, `XrefsTo` and a one-off breadth-first walk over direct callees from a root, depth 12) on the 1.67 Steam image, capstone 5 scans of the same image (rel32 and absolute references, `[reg+disp]` uses) and the decompiled ScriptCore. "Direct reach" means direct calls only; virtual calls are not followed unless named. Ledger rows: docs/51 §62, I-105-20 to I-105-25 (updates I-105-16 and I-105-17).

#### 10.4.1 Who makes a door's portals [verified]

- **`DoorRouting_AddDoorPortals 0x80CB40` is the door portal creator.** It is footprint slot `+0x100` (classes `0xFEB848`, `0xFEB970`), reached only through `Footprint_EnsureLevelPolygons 0x80E5B0` (level invalid) -> slot `+0xF8` -> `0x80C430` (docs/84 F-A). For level 0 it collects the object's existing portals (`LotRouting_PortalsForObject 0x62F760`, vector tag `Routing/existingPortals`), walks the `RoutingSlot_Door%i_Front` / `_Rear` slot pairs, keeps an existing portal whose four endpoint coordinates match (no add), otherwise adds one through `LotRouting_AddPortal 0x6379E0` (the E9 leaf), then removes the leftovers and resets the front-door index (`0x636130`). So a door portal is created when something first needs that door's level-0 footprint polygons, and re-added only when the slot positions moved.
- **Callers of `0x80E5B0`** [verified, `CallerTree` with vtable data refs]: `RouteFollow_EditPath 0x85F5A0` (route following, Sim obstacle checks), `0x85FF30` (from `Route_PlanCommon`, `IC_Route_InsertRouteSubPathsAtIndex`, `IC_Route_InsertCustomPathAtIndex`, `IC_Route_SetAGS`) and the four footprint test slots `0x80E690` (`+0xE8`), `0x80E770` (`+0xEC`), `0x80E8A0` (`+0xB8`), `0x80E910` (`+0xBC`). The worker runs `0x80CB40` too, inside jobs (W6g, docs/83, docs/84 §4.2).
- **The lot rebuild does not make door portals.** `RoutingSystem_RebuildLotRouting` and the lot import both reach `0x832890`, which adds only the lot's edge (entry) portals, one per lot-edge record and only where `PortalsForObject` finds none for that record's id (an id built from the record index, inferred synthetic). Its `Routing/existingPortals` vector is that check. The docs/92 §2 row "re-adds existing portals" therefore means "adds missing lot-edge portals".

#### 10.4.2 (a) I-105-16: does `0x997540` make the door portals before it returns?

**Verdict: no direct path [verified]; door portals come later, on first use [verified mechanism]. A virtual path inside the IC is not excluded [inferred unlikely], so one probe stays (10.4.6).**

The IC body: `0xAD5000` (find the lot, read the export package) -> lot method `0xACE4A0`, which in order imports the lot data layers (`0xAC79E0`: terrain, deck and foundation, walls through `RoutingSystem_RebuildLotWalls 0x8350C0`, then `0x82ABB0` and the lot-edge portals `0x832890`), creates the objects (`0xAB5FC0`, docs/50 path B), updates the lot bounds (`0xA97940` -> `0xA94F40` -> `0xAB6D60`), sets the lot's dirty bytes, and at the end marks the lot table entry dirty and refreshes the room graph (`LotTable_MarkDirty 0x631AA0`, `0x630F10`, `RoomGraph_Refresh 0x63D860`, `0x631AF0`) [verified decompile]. The walk from `0x997540` reaches 3,291 functions. It finds `0x6379E0` only through `0x832890` (lot edge) and does not reach `0x80CB40`, `0x80E5B0` or `RoutingWorld_LoadLotData 0x82CA10`. Not covered: virtual calls to the footprint test slots from inside the import (for example a placement or room-graph step calling slot `+0xE8` on a door footprint); `RoomGraph_Refresh`'s virtual callees were not checked.

Expected order for a bin/library placement or an Edit Town rotate: the IC creates and places the door (constructor lock type) -> the IC returns -> `ImportManagedContent` -> `CommonDoor.ImportContent` writes `mLockType` -> later, the first route plan, route follow or footprint test on that lot runs `0x80CB40` (on the sim thread, or on the worker inside a job) and adds the door portals. A mirror built at portal add would read the imported value here, but it would have to run on the worker whenever the add happens inside a job, which a managed mirror cannot do. Both points keep the lazy build of §10.3 point 1.

#### 10.4.3 (b) I-105-17: save load order

**Verdict: per lot, portals before objects [verified]; managed fields restored before any proxy can reach the object [verified, managed]. Where the `OBJS` read sits inside the world load is not traced.**

1. `WorldManager_LoadWorld 0xC6CF80` fires event `5` (load started), creates the lots (`WorldManager_CreateLotsFromLayer 0xC65E00`), posts world init to the main thread (`0xC622D0`, sync), then loads the lots (`0xC64630` or `WorldManager_LoadLots 0xC678D0`) [verified decompile].
2. Per lot, `Lot_FinishLoad 0xACF170` calls **`0xAB4920` -> `RoutingWorld_LoadLotData 0x82CA10` first** (site `0xACF22C`) and **`Lot_LoadObjectLayer 0xACE0D0` after it** (site `0xACF293`) [verified]. The routing load deserializes the lot's portals (`0x82CA10 -> 0x63C8E0 -> 0x6236D0 -> 0x62C920 -> 0x64EEF0`); its direct reach (422 functions) has no `LotRouting_AddPortal` and no `World_FireEvent` call [verified]. So loaded door portals exist before their door objects exist natively, **no E9 add fires for them**, and later `0x80CB40` finds them in `existingPortals` and keeps them.
3. The object layer creates the objects (docs/50 path C); each script component queues (object id, saved index) in the host (`ScriptComponent_LoadFromStream 0x950280` -> `0x9501B0` / `0x94FCE0`) [verified, docs/50].
4. **Managed field restore.** The ScriptComponent type object (vtable `0x1043F8C`, slots `+4` `0x9501F0` and `+8` `0x950240`) reads the `OBJS` resource (type `0x06B981ED`) in `0x94FF00` and calls host vtable slot `+0xE8` = `0x769B80`, which invokes `ScriptCore.LoadSaveManager.BeginLoadObjects` (string push at `0x769C45`) [verified; host vtable base `0x108B690` inferred, consistent with the docs/50 slots `+0xE0`/`+0xE4`]. `BeginLoadObjects` runs one `PersistReader.Load` over the whole `OBJS` stream: every persisted object, door lock fields included, is created and filled in this one call [verified, managed]. Which world-load step calls the type slot (before or after the lot loop) is not traced; the object service dispatches it virtually.
5. **Binding.** `BindLoadedObject(proxy, index)` sets the proxy's target to `mObjectGroupReader[index]`, which exists only after step 4; then `ScriptProxy.PostLoad` -> `Init(true)`; `EndLoadObjects` (host slot `+0xF8` = `0x769470`) runs `ExecutePostLoads` and drops the reader [verified, managed code and vtable]. The native drain of the pending (id, index) list, that is the caller of `BindLoadedObject` and `PostLoad`, is not traced: the host caches the managed methods in a table filled from `0xD7E77A` on (fields `+0x1620` to `+0x164C`), and which field holds which method is not settled.
6. `0xC6CF80` then posts post-load init (`World_PostLoadInit 0xC6AEB0`, sync on main), calls `LoadSaveManager.OnObjectGroupsPostLoad` (host `+0x138`, string push at `0xC6D36E`) and fires event `9` (world file) or `0x0A` (save game) [verified decompile].

What matters for the mirror: a managed door is reachable through its proxy only after step 5, and its fields were restored in step 4, before any binding. Any managed read of a door after load therefore sees the restored lock fields or no target at all, never constructor defaults [verified, managed]. The portals (step 2) are older than both and never pass E9. `CommonDoor` has no load-time lock fixup: `OnStartup` and `Init` do not set the lock [verified C#].

#### 10.4.4 (c) World events of the lot import, and the pending-list drain

**Events** inside `0x997540`, so before it returns and before `ImportContent` [verified call chains; conditions as marked]:

| Event | Name | Path | Condition |
|---|---|---|---|
| `0x0F` | `OnLotBoundsChanged` | `0xACE4A0` -> `0xA97940` -> `0xA94F40` -> `0xAB6D60` (fire at `0xAB6F28`) | the calls down to `0xAB6D60` are unconditional; it fires only when lot `+0x290` is set, the field `Lot_FinishLoad` also tests before `0xA952C0` (meaning inferred: the lot has terrain/foundation data, so most lots) |
| `0x11` | `OnObjectLotUpdate` | `0xAC79E0` -> `0xAB2970` -> `0xA954C0` -> `0xA8FDD0` -> `0xC8AD90` -> `0xC8AC70` -> `0xC8A290` | conditional: `0xAB2970` needs lot `+0x290` and a true `0xA954C0` |
| `0x19`, `0x2D` | `HandToolAllowUserPickup`, `OnLotEditZonesChanged` | through lot managers `0xB6E4F0` (lot `+0x2BC`) and `0xB552B0` (lot `+0x298`) | in direct reach only; not shown to fire |

No load or lot-added event (`5`, `7`, `9`, `0x0A`, `0x0D`) is in the IC's direct reach [verified]. The docs/76 epoch therefore sees a bin placement only through `0x0F` (in its filter), and that event fires **before** the managed import writes the lock. As a mark (§10.3 point 1) that is fine; an eager rebuild on it would read the pre-import value. The `ImportContent` postfix (§10.3 point 2) stays needed.

**Pending-list drain:** 10.4.3 step 5. Settled: binding and `Init(true)` can only run after `BeginLoadObjects`. Open: which native function drains the list, and whether it runs inside `WorldManager_LoadWorld` or on the first script tick after it. The design does not depend on it.

#### 10.4.5 Effect on the docs/94 door mirror plan

1. **Key the mirror by the lot's managed doors, not by native portals.** Door portals of an imported lot appear after the import, on first use, possibly on the worker inside a job (W6g), and loaded portals never pass E9. The prologue enumerates the named lot's door objects on the managed side and reads their fields; the native evaluator looks up a portal's object id in that table, and an id with no entry takes the guard path (§4.6).
2. **Drop "E9 portal add" as a door invalidation source** (keep it for other portal kinds if they need it): it does not fire on load, and when it fires for a door the lock fields are already final. Door invalidation: docs/76 load epoch (`5`, `7`, `9`, `0x0A`), lot added `0x0D`, lot bounds `0x0F` (fires inside the bin import), build/buy exit, and the `ImportContent` postfix.
3. **Never fill a missing managed target with defaults.** After load, a door without a bound target leaves its lot invalid (retry at the next START). Once a target exists, the managed order rules out defaults.
4. No new native hook.

#### 10.4.6 Live probe (for the open remainders only)

Open: (i) a virtual path inside `0x997540` to `0x80CB40`; (ii) where the pending list is drained. Neither changes 10.4.5; the probe only confirms. **No ts3agent repro places a lot from the bin or library** (`tools/harness/repros.json` has none; the agent can enter Edit Town but has no lot-placement command).

- **`[ProbeDoorImport]`** (ts3native, off by default; reserve a counter slot in `docs/registry.md`): a depth counter around `IC_DownloadContent_ImportLotContentsImpl 0x997540` (IC slot swap, the docs/94 pattern); count entries of `0x80CB40` (through the existing W6g stub in `steerro.cpp`) and of `LotRouting_AddPortal 0x6379E0` while the depth is non-zero, and log the footprint's object id (`+0x14/+0x18`, docs/84 "Footprints outlive their object id"). Expected: 0 entries for doors.
- **ts3fixes prefix on `CommonDoor.ImportContent`** (debug build): log object id, `mLockType` before and after, and the portal count from a native call of `0x62F760` (thiscall convention of docs/84 §4.1). Expected: count 0 at entry.
- **Load order:** log a sequence number and thread id at the first return of `RoutingWorld_LoadLotData 0x82CA10`, at entry of host slot `+0xE8` (`0x769B80`), at the first `LoadSaveManager.BindLoadedObject` (ts3fixes prefix on the private static) and the first `ScriptProxy.PostLoad`, plus whether `WorldManager_LoadWorld 0xC6CF80` is on the stack (depth counter). Repro: load `Baseline-BP-Spring`, after `bp-door-lock` has set a locked door.
- **New repro for (i):** an agent command `place_lot_from_library` (Edit Town, library lot with a `Nobody` door, `BinCommon.PlaceLotContents`) and a repros.json entry; until it exists, a manual placement with the probe on.

### 10.5 Managed door mirror (ts3fixes, built 2026-10-18, offline only)

The managed half of the §10.4.5 plan, switch `doormirror` (tuning `kDoorMirror`, default off). Built and self-tested offline; not run in game. Code: `mods/ts3fixes/src/DoorMirror.cs` (game reads, postfix, interface) and `mods/ts3fixes/src/DoorMirrorCore.cs` (records, per-lot table; no game types). Self-test: `node tools/selftest/doormirror/run.ts`.

**Interface for the native binding.** The native contract landed in ts3native 0.26.0 (docs/94 §14); the binding is built (below). Managed interface, all calls on the sim thread:

| Call | Meaning |
|---|---|
| `Ts3Fixes.Fixes.DoorMirror.GetLotDoors(ulong lotId, out uint epoch)` -> `DoorRecord[]` | The lot's door records. Built at the first call after an invalidation (§10.3 point 1, the prologue at START); cached until the next one. `null` = the lot is invalid: switch off, lot gone, or a door that cannot be mirrored; the native side takes the guard path (§4.6) and asks again at the next START. `epoch` changes on every invalidation of that lot, so records kept under epoch E are current while the call answers E with a non-null array. |
| `Ts3Fixes.Fixes.DoorMirror.Invalidate(ulong lotId)` | Mark one lot invalid: for lot added `0x0D`, lot bounds `0x0F` and build/buy exit (per lot), and the `ImportContent` postfix below. |
| `Ts3Fixes.Fixes.DoorMirror.InvalidateAll()` | Mark every lot invalid: for the docs/76 load epoch (`5`, `7`, `9`, `0x0A`). |

`DoorRecord` (one per `CommonDoor` on the lot, from `Lot.GetObjects<CommonDoor>()`): `ObjectId` (the portal owner id, the native answer key of §2), `LockType` (`mLockType`, `tLock` 0-6), `LockOwnerId` (`mLockOwner.SimDescriptionId`, 0 = none), `LockOwnerHouseholdId` (that owner's household id, 0 = none), `OwnerList` (a copy of `mOwnerList`), `OneWayRoom` (`mOneWayDestinationRoomId`, `36863` = both ways), `RoomA`/`RoomB` (`GetRoomConnections()`, the room pair the door's portals join: the portal mapping available managed-side; native portals are matched by owner id), and `Kind` bits (`Door` 1, `IBarnDoor` 2, `GateDouble` 4, `IElectronicGate` 8, `CanLargeAnimalUse` 16, `DoorSingleGender` 32), the class facts `IsAllowedThrough` tests. A record is refused, and with it the whole lot, for a destroyed door, an object id 0, a lock value outside `tLock`, or `SelectedHousehold` with no owner (the game's rule throws on it, so only the game's own code may answer that door) [verified: `CommonDoor.IsAllowedThrough` reads `mLockOwner.Household` for `SelectedHousehold` without a null check].

**Unbound doors.** `Lot.GetObjects` is the native lot object query and returns managed objects; a door whose proxy has no bound target is not reachable from it [inferred: the managed query cannot return an object that does not exist yet, §10.4.3 step 5]. So the managed side cannot see "a door with no bound target" by itself. The rule of §10.4.5 point 3 is kept on the native side instead: **a portal whose owner id has no record in a valid lot table is unknown and takes the guard path, never `Anybody`.** A `null` element or a destroyed door in the query does refuse the lot.

**`CommonDoor.ImportContent` postfix** (`Patch()` header swap, hook `DoorImportHook`): after the game's body, it marks the door's lot invalid (no lot: all lots) and, with the switch on, logs `door import <id> lot <lot> lock <type>` to `ts3fixes.recent`. Every door subclass reaches it through its base chain, the closed `ElectronicGate` included (§10.1). It runs with the switch off too, so a table built while the switch was on is never kept stale; it touches only our table.

**Counters** (ts3fixes slots, docs/registry.md; agent `patches` names): `[102]` `doormirror_imports` (postfixes run), `[103]` `doormirror_invalidations`, `[104]` `doormirror_builds` (lot tables built), `[105]` `doormirror_refused`. Array raised to `int[106]` (slots 99-101 stay reserved for invcache, docs/115).

**Self-test** [measured offline]: record building (owner list copied, not aliased; room fallback; refused cases), lazy build and epochs, refused and throwing reads keep the lot invalid and are retried, `InvalidateAll`, and a random model (50 runs x 400 operations over 6 fake lots) where every lock change, bind change or new door is followed by its invalidation: every valid answer equals a fresh read of the fake doors and one epoch never maps to two record sets. Passes.

**Native binding (built 2026-10-18, offline only; ledger docs/51 §72, I-105-40 to I-105-43).** `DoorChannel` in `DoorMirrorCore.cs` (no game types) encodes the docs/94 §14 channel; `DoorMirror.SyncRoute` in `DoorMirror.cs` calls it from the `ScriptCore.Route.Plan`, `Replan`, `PlanFromPoint` and `ReplanFromPoint` hooks (`RouteBackoff.cs`), switch `doormirror` only. Each message is one `GetOption2` on the handle-0 route that `MayYield` already uses, RouteOption2 value `0x8400000000000000 | code << 48 | value` (ts3native sees `optHi = 0x84000000 | code << 16`, `optLo = value`).

| Step | Messages (code: value) |
|---|---|
| Lots | the start lot (`World.GetLotLocation` of the follower's position, or of the point for the `*FromPoint` kinds) and the primary goal lot (of `GetApproximateOriginalDestination()`); lot 0 skipped, one sync when both are the same lot |
| Query | `21` lot low, `22` lot high. `21` refused = no channel (no ts3native, or one without the door channel): stop, nothing read. `22` = 1: native table current, done |
| Push (on `22` = 0) | the managed lot entry is invalidated first, then read fresh (`GetLotDoors` rules); a refused read sends nothing. Else `1`/`2` lot, `16` managed epoch (once per lot, right after the open, so an empty lot carries one too), per record `3`/`4` object id, `5` kind, `6` lock, `7`/`8` lock owner, `9`/`10` household, `11` one-way room, `12`/`13` rooms (ints sent as their 32-bit pattern), `14`/`15` per owner-list id; then `17` with the record count. Stops at the first refused message (the lot stays invalid natively) |
| `Invalidate(lot)` / `InvalidateAll()` | also send `18`/`19` lot, or `20`, whatever the switch says (rare; a native table filled while the switch was on never outlives a managed invalidation) |

Why a push re-reads instead of using the managed cache: ts3native invalidates on events the managed table never hears (the door lock event, build/buy exit, world epoch; docs/94 §14), so "native not valid" has to mean "managed stale" too. The managed cache only spares reads between a native-valid answer and the next invalidation.

Counters: none new. Native `spDoor[0..4]` (messages, lots ended, records, invalidations, protocol errors) show the channel; ts3fixes `[104]`/`[105]` count the reads the pushes make.

Self-test (`node tools/selftest/doormirror/run.ts`) [measured offline]: a C# port of `SpDoorMsg` replays snap.cpp's own door-channel selftest sequence (`selftest.cpp`, "door channel") message for message with the same expected answers; the option bit layout; 200 random lots (0-5 doors, 64-bit ids, owner lists, negative room ids) pushed and decoded field for field; the adapter against the port: no channel (one call, no read), first push, query only when valid, a native-only lock-event invalidation and a world epoch each push a fresh read, a refused read pushes nothing and leaves the lot invalid natively, managed `Invalidate`/`InvalidateAll` over the channel, 0 protocol errors. The port has to follow snap.cpp; the replayed vectors are the check.

**Not done here:** P4's reader (nothing native reads the table yet), transit lots (docs/94 §11.2: known only after the world search), an agent dump command, the live rows I-105-40 to I-105-43, and the shadow diff of I-105-8 (mirror vs `IsAllowedThrough` at each plan). This file does not touch `Lot.OnPortalPathPlan` (docs/115 patches it). No ts3agent command dumps the table yet; add one with the binding. Effort left on the managed side once the native contract lands: ~30-60 min (binding calls, one agent dump command, a `bp-door-lock` repro run with the switch on), plus one live job (~10-15 min).

> **Note 2026-10-21 (docs/134 §7):** live rate of the I-105-3 refusal: `arLosRefused` is 1-3% of async STARTs in the freeze runs of 2026-10-20, and only about one Los plan per 10 minutes of play takes >= 20 ms (53-130 ms; caller `ViewObjects.RouteToObject`). Decision unchanged: Los plans stay on the sim thread. The way to move them later is a thread-local display level for the worker's `World_CanSeeObject` (detours on `Lot_SetDisplayLevel 0xAC0570` and the ray leaf visitor `0x680F80`, docs/134 §7 option B), not a prediction at START.
