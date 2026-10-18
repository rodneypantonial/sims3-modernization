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
