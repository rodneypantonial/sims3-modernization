# 127. Autonomy closures with virtual dispatch resolved

2026-10-20. Static only: call-graph index and assemblies of the Steam 1.67 install, the decompiled class library for the receiver sites, NRaas ErrorTrap and Overwatch extracted read-only from the Mods folder into the session scratchpad. No game run, no build. Per-definition table: `docs/data/autonomy-virtual-closure.csv`; generator `tools/autonomy-virtual-closure.ts` (closure in `tools/autonomy-tier2.ts`, dispatch in `tools/managed-callgraph.ts`).

The allow-list closures (docs/121 v0, docs/126 tier 2) followed a virtual or interface call only into its declared body (docs/126 §1 "Limit"). This note follows every override the receiver can dispatch to, up to 5 calls from each definition's `Test`/`CalculateScore` roots, and re-runs the docs/126 verdict.

Tags: **[verified]** = tool output, IL or the decompiled class library read for this note; **[inferred]** = reasoning, not checked live.

## Summary

| Run | Ready | New rows | Blocked | Hooked |
|---|---|---|---|---|
| docs/121 + docs/126 (declared body only) | 191 | 61 | 16 | 2 |
| **Overrides, typed receivers (this note)** | **23** | **131** | **114** | 2 |
| Overrides, typed, with hook H11 (§4, what-if) | 51 | 154 | 63 | 2 |
| Overrides, plain class hierarchy (upper bound) | 20 | 12 | 236 | 2 |

270 closures: the 12 v0 definitions, the 7 `HowMuchItWantsSimToCome` implementors and the 251 tier-2 candidates [verified: tool output].

- **The overrides change 191 of 270 verdicts** [verified: tool output]. They reach **346 IC bindings** the old closures did not (bindings, so `UTFConnector*` twins count twice): 212 class a (29 of them already in the pilot snapshot set), 91 w, 27 b (3 already abort branches), 8 c, 6 a-stub, 2 d.
- **All 12 v0 definitions go from ready to blocked, for one reason: a static write** [verified]. In rain or hail with Seasons on, `Autonomy.CalculateScoreForObjectInteraction` creates a throwaway interaction instance for an outdoor target to ask `SeasonsManager.IsShelteredFromPrecipitation`; `InteractionInstance.Init` increments the static `InteractionInstance.sNextId` and allocates a stack trace. The old closure missed it because `CreateInstance` is virtual on the definition. The docs/122 guard thunk would not catch it either: it guards ICs, and this is a managed write. 35 of the 37 tier-2 meta definitions are blocked by the same branch.
- **With a worker hook on that branch (H11, §4)** the 12 v0 definitions need one new row each (`Query_GetGlobalObjects`, through `WorldLot.GetObjects<T>` overriding `Lot.GetObjects<T>`) and 28 of 37 tier-2 meta definitions are ready again.
- **Implementors:** `Lot` and `BakeSaleTable` need the same new row; the other 5 stay ready [verified].
- **Local phase:** the main new row is `World_IsOnRoofImpl` (reached by 152 closures, the first new reason of 61 verdict changes: `InteractionDefinitionUtilities.SpecialCaseTests` reads `IGameObject.IsOnRoof`, whose only implementation is `GameObject`'s). The main new blocker is route creation in `Test`: 31 `Test` bodies call `actor.CreateRoute()`, and `SimRoutingComponent.CreateRoute` sets route options, followers and valid rooms on the new native route (`Route_Set*`, class w by the docs/126 name rule).
- **Yields and event sends:** 15 closures newly reach one (§3.3), all through override bodies of objects or instances, none in v0.
- **Mods:** NRaas ErrorTrap and Overwatch add 27 overriding methods, all on NRaas's own interaction and definition classes; none on an object, posture, service or component class (§5).

## 1. Method

**Roots and cuts.** As docs/126 §1: `defRoots` (own or nearest `Test` + `CalculateScore`), depth 12, delegates not followed, docs/121's two guards, the pilot's worker hooks and H7-H10. Candidates: the docs/126 251, plus v0 and the implementors (docs/122 §12.3).

**Which overrides.** At a `virt` or `iface` call (docs/78 edge kinds) in a method fewer than 5 calls from a root, the closure also enters overrides; deeper calls keep the declared body only, as before. Three runs:

1. **Class hierarchy (CHA):** every override in the game assemblies (docs/103 §2's `overridesOf`). BCL declared targets (object and collection members) are not expanded. This is the sound upper bound and is far too wide: `InteractionDefinition`3.Test(actor, target, ...)` alone has 4,932 overrides, `GameObject.get_InUse` dozens, so 236 of 270 closures block.
2. **Typed (the verdicts used here):** each visit carries the class of its `this`, and a call enters only the overrides that class can reach (`familyTargets`: overrides in the class's subtree plus the nearest one up its base chain; for an interface, overrides on classes that implement it). The callee's class comes from, in order:
   - a hand-reviewed receiver site (§2);
   - the receiver's **static type in the IL** (`receiverTypes` in `managed-callgraph.ts`): C# names the original declaration in a `callvirt`, so the target says nothing about the receiver; the tool walks back from the call over instructions with a known stack effect to the one that pushed the receiver (argument, local, field, call result, cast, `newobj`) and takes its type. It gives up at a branch target in between (stack merge), on any other opcode or on a generic parameter type. When the receiver is `ldarg.0`, the caller's own class carries over [verified: implementation + self-check];
   - else the caller's class, for a call between two methods on that class's base chain [inferred: receiver not tracked there; verified for the generic base `Test` calling `this.Test(actor, target, ...)`].
   Roots carry the definition type (implementors: the implementor type).
3. **Typed + what-if cut:** the typed run with extra cuts (`--what-if`), used once for H11 (§4).

**Verdict.** docs/126 §1 step 5 unchanged (pilot union and snapshot set from the old closures). A new IC already in the snapshot set is marked `*` in the csv and does not change the verdict.

**Self-check** (`managed-callgraph.ts --self-check`, added): `Sim.OnPortalPathPlan` with overrides reaches `Turnstile.IsAllowedThrough` and without does not; `System.Object.ToString` is not expanded; the generic base `Test` resolves to 3 methods for `VisitLot+Definition` (itself and its 2 subclasses) of 4,932; `GameObject.GetInUseAutonomyMultiplier` reads `InUse` on `this`; `CalculateScoreForObjectInteraction` calls `GetInUseAutonomyMultiplier` on an `IGameObject` local; the `Lot` family of `get_InUse` is smaller than all overrides [verified: passes]. `autonomy-tier2.ts` without overrides still reproduces `docs/data/autonomy-tier2.csv` byte for byte [verified].

## 2. Hand-reviewed receivers

Where the IL type is too wide (an `IGameObject` local, a field typed by an interface) the receiver was read in the decompiled class library [verified unless marked]:

| Caller (or producer) | Declared | Receiver |
|---|---|---|
| any, receiver from `InteractionObjectPair.get_Target` / `get_InteractionDefinition` | any | the scored pair's target / definition [inferred for callers outside `CalculateScore*` and `CheckAvailability*`] |
| any, receiver from `InteractionInstanceParameters.get_Actor` / `get_Target` | any | the root `Test`'s actor / target class (the `Test(ref parameters)` overloads) |
| `MetaInteractionScoredByVenue`3.CalculateScore` | `IMetaObject` members | target (cast into a local first) |
| `Autonomy.CalculateScoreForObjectInteraction` | `IGameObject.GetInUseAutonomyMultiplier` | target (kept in a local) |
| `SeasonsManager.IsShelteredFromPrecipitation` | `ICanCountAsBeingIndoors.IsInteractionConsideredIndoors` | target on the scoring path [inferred for other callers] |
| `InteractionDefinition`3.CreateInstance` | `InteractionInstance` members | the definition's `TInteraction` (from its generic base, `instanceTypeOf`) |
| `GameObject.CreateRoute` | `RoutingComponent.CreateRoute` | `SimRoutingComponent` when `this` is a Sim (`Sim` adds that component at construction) |

The target class is the root `Test`'s second parameter, so it holds once `Test` has passed (scoring runs after it) [inferred]. Closures without a definition (base paths, scoring functions) have no actor/target/definition class, so those sites fall back to all overrides.

## 3. Results

### 3.1 v0 and the implementors

| Definition | Closure: old / typed / CHA | Typed verdict | New ICs outside the snapshot set | With H11 |
|---|---|---|---|---|
| `VisitCommunityLot+Definition` | 548 / 930 / 3,864 | blocked (`sNextId`) | `Query_GetGlobalObjects` | new rows |
| `VisitLot+Definition` | 595 / 1,030 / 3,930 | blocked (`sNextId`) | `Query_GetGlobalObjects` | new rows |
| other 10 v0 definitions | 389-411 / 796-884 / ~3,840 | blocked (`sNextId`) | `Query_GetGlobalObjects` | new rows |
| `Lot`, `BakeSaleTable` implementors | 373, 354 / 461, 422 | new rows | `Query_GetGlobalObjects` | new rows |
| `RabbitHole`, `GardeningMetaObject`, `WorldPondHelper`, `HomeMetaObject`, `WorkMetaObject` | | ready | none (only snapshot-set ICs) | ready |

[verified: tool output] The other new v0 ICs (`World_IsPositionOutsideImpl`, `World_GetMoonPhaseImpl`, `World_GetDoorRoomsImpl`, product price and resource key, `GameUtils_GetWorldName`, `Query_GetObjectsOnLot`) are already in the snapshot set; they now show up per definition because the venue score enters the target's own `HowMuchItWantsSimToCome`, which the pilot union already covers through the implementor roots.

`Query_GetGlobalObjects` path: `Lot.CommonVisitTest` -> `Lot.FindCallbox` -> `GetObjects<T>` dispatched to `WorldLot` -> global object query [verified: tool path]. The docs/62 scan reads it as class a; it is a world-wide query like `Query_GetObjects` (class d, I-121-7), so its row is a per-type global list [inferred]. Whether the target `Lot` of a visit can be the world lot at all is open (I-127-3).

### 3.2 Tier 2

| Group | Old: ready / rows / blocked | Typed | Typed + H11 |
|---|---|---|---|
| meta-rest (37) | 37 / 0 / 0 | 0 / 0 / 37 | 28 / 8 / 1 |
| local-sampled (31) | 21 / 4 / 6 | 6 / 14 / 11 | 6 / 15 / 10 |
| row5-candidate (169) | 102 / 57 / 10 | 4 / 113 / 52 | 4 / 115 / 50 |
| base-local (11, 2 hooked) | 9 / 0 / 0 | 7 / 1 / 1 | 7 / 1 / 1 |
| scoring-function (3) | 3 / 0 / 0 | 1 / 1 / 1 | 1 / 1 / 1 |

[verified: tool output]

Newly blocked, by the first new reason: `InteractionInstance.sNextId` 51 (the rain branch), `Route_SetOption` 31 (route creation in `Test`, with the static `Route.sCtorInfo` and `Route_SetFollower`/`SetAGS`/`SetValidRooms`/`AddObjectToIgnoreForRoute` behind it), `World_TestObjectPlacementP` 5 (another definition's `Test` called from a `Test`), `World_RemoveObjectFromObjectManagerImpl` 4 (`BBallHoopBall.get_InUse` destroys a stale ball), `Relationship.sAllRelationships` 2, `CheckObjectPlacement` 2, one each for stair product info, world file search and a level-up sound through a chess-rank lookup in `GetInteractionName`.

Newly new-rows, by first reason (all 270 rows): `World_IsOnRoofImpl` 61, `Query_GetGlobalObjects` 30 (2 of them implementors; `WorldLot.GetObjects<T>`, `RoomConnectionObject.IsOutside` via door rooms), `Objects_GetForward` 2 (relaxing posture vision cone). Reached at all: `Query_GetGlobalObjects` by 206 closures, `World_IsOnRoofImpl` by 152.

The b and w ICs reached most often: object create/add/destroy (`Simulator_CreateObjectImpl` 34, `Simulator_AddObjectImpl` 11), placement tests (13), `Simulator_SleepImpl` 11, footprint and room-marker writes, SACS driver acquire/release, UI window create/destroy (42 via `UIManager` in `GetInteractionName`/tooltip overrides) [verified: tool output; class from the docs/126 rule, inferred].

### 3.3 Yields and event sends

15 closures newly reach a yield (`Simulator.Sleep`, `StateMachineClient.RequestState`, SACS driver acquire) or an `EventTracker.SendEvent`: meta-rest `BakeSaleTable+GotoBakeSaleTable`, `ShoppingRabbitHole+ShopInRabbitHole`1`, `WorkInRabbitHole`, `ProSports+PlayGame`; the base path `CalculateScoreForObjectInteraction` (no definition context, so its `CreateInstance` site is open); the phone `CallDefinition`; row 5 `PushableStatue+Move`, `PlayOnRug`, `MagicCauldronRegency+TeleportPotion`, `Sim+PetPlayOnCarpet`, `Helm+ReturnToPortTerrainProxy`, `InteriorDesigner+CompleteRenovation`, `RockBand+GetBandMemberToLot`, `Jetpack+FlyHere`, `OccultUnicorn+UnicornTeleport` [verified: tool output]. None is a v0 definition; all 15 are blocked in the typed run.

## 4. What changes for the pilot

**H11, rain/hail shelter branch** (new worker hook, design only): in `CalculateScoreForObjectInteraction` the shelter test runs only with Seasons on, weather rain or hail, an autonomy search and an outdoor target [verified]. On the worker, answer the branch without creating an instance: abort the pair to the sim thread when the three conditions hold (weather is constant within a search), or later serve `IsShelteredFromPrecipitation` from a per-target snapshot bit. Abort is enough for the pilot: it costs only rainy outdoor pairs. Effort: ~30-60 min (one ts3fixes hook + abort reason `rain`, counter slot per docs/registry.md) plus a Mode R check in a rain save.

**New rows:** `Query_GetGlobalObjects` (all v0 definitions, 2 implementors, most tier-2 closures; per-type global list, ~0.5-1 h after I-127-3) and `World_IsOnRoofImpl` (152 closures; one bit per row, ~0.25 h).

**Route creation in `Test`** (31 local definitions): a Sim's `CreateRoute` builds a native route and sets options on it. Writes go to an object the closure itself creates, so the w class may be too strict [inferred, I-127-2]; until checked these stay on the sim thread.

**The managed-write gap.** The docs/122 guard thunk aborts on unexpected ICs only. A static write in an override body (the `sNextId` case) runs silently on the worker. Mode R (docs/122 §4.3) compares results, not static fields, so it would not show a lost increment either. The allow-list therefore needs the typed closure of this note (or an equivalent check) before a definition goes in, not only the IC guard [inferred].

## 5. Mods

NRaas ErrorTrap and Overwatch were extracted from the user's Mods folder (read-only, `tools/extract-assemblies.ts`, copies in the session scratchpad, not in git) and loaded after the game assemblies for the class hierarchy only (`--mods`) [verified: tool output]:

- **27 overriding methods**, all on NRaas's own interaction and definition classes: `CreateInstance`, `Test`, `GetInteractionName` and `GetTargetLot` of Overwatch's replacement definitions (`GetMailEx`, `HarvestEx`, `AgeUpEx`, `GetSmeltEx`, `RobotScanEx`) and the shared NRaas `BaseInteraction`/`CommonInteraction` bases. None overrides an object, posture, service or component member.
- They are reached only when the scored definition (or created instance) is an NRaas type. The allow-list keys on type + EA assembly MVID (docs/122 §3.6, docs/104), so an NRaas definition never runs on the worker [inferred].
- **ErrorTrap ships its own `ScriptCore.dll`** (S3SA in `NRaas_ErrorTrap.package`) [verified: extraction]. The tool skips it (same name as EA's); the mod guard must key on MVID, not on assembly name.
- simler90's core mod (also in this Mods folder) replaces the gameplay assemblies whole; not scanned, covered by the same MVID rule (docs/103 §1).

## 5.1 Limits

- 5 hops: an override reached deeper than 5 calls from a root is not entered; the declared body is still followed to depth 12, as before.
- The receiver type is local to one call site, plus the `this` carry-over and the §2 sites; there is no flow across calls otherwise. A visit keeps the first path found per (method, class) pair.
- Base paths and scoring functions have no definition context; their typed result is close to CHA at the definition and target sites.
- Classes of the new ICs come from the docs/126 rule over the docs/62 scan, not from the disassembly (I-126-1 applies).

## 6. Ledger

docs/51 §171: I-127-1 (H11 branch conditions are the only way to the instance create on the scoring path), I-127-2 (route `Set*` ICs write only the new route), I-127-3 (a visit target `Lot` is never the world lot), I-127-4 (the `this` carry-over between chain methods).

## Reproduce

```sh
node tools/managed-callgraph.ts --self-check --asm <repo>/game-copy/assemblies
node tools/extract-assemblies.ts <scratch>/mods "<Documents>/Electronic Arts/The Sims 3/Mods/Packages/NRaas_ErrorTrap.package" ".../NRaas_Overwatch.package"
node tools/autonomy-virtual-closure.ts --asm <repo>/game-copy/assemblies --index <repo>/game-copy/analysis/callgraph/index.json --mods <scratch>/mods > docs/data/autonomy-virtual-closure.csv
node tools/autonomy-virtual-closure.ts --asm ... --index ... --what-if "Sims3.Gameplay.Interactions.InteractionDefinition\`3::CreateInstance(,Sims3.Gameplay.Interactions.InteractionDefinition::CreateInstance(,Sims3.Gameplay.Interactions.IInteractionDefinition::CreateInstance(,Sims3.Gameplay.Seasons.SeasonsManager::IsShelteredFromPrecipitation(" > /dev/null   # H11 counts (stderr)
```

Csv columns: `target_type`/`instance_type` (from the root `Test` and the generic base), closure sizes per run, `new_ics` and `new_ic_classes` (`name:class@depth`, `*` = in the snapshot set), blocking ICs, new yields/events, new static writes (`(pilot)` = the pilot union already reaches it), NRaas overrides seen in the CHA run, the old / typed / CHA verdicts and the first new reason with its path (`=>` marks a step into an override).
