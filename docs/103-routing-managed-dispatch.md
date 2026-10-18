# 103. Routing: managed virtual dispatch across all packs

Written 2026-10-18. Static only; the game, the probe server and the queues were not run. Goal (PLAN item 6, our own off-thread route search with no knowledge gaps): close the managed (C#) side of the routing I/O study. docs/92 mapped the two managed bridges the search reaches (World events 59 and 68) and docs/93 the 234 managed callers of the plan methods, but both followed only declared call targets. A `callvirt` to a virtual or interface method can land in any override, in any pack, and a mod can add more. This doc lists every override of every unresolved call in the routing-relevant C#, flags the ones that change a routing decision, and names the packs and mods they come from.

Tool: `tools/managed-callgraph.ts` (docs/78) with two new subcommands, `overrides <method>` and `dispatch` (class hierarchy analysis over all loaded assemblies, below). Input: the 10 script assemblies in `game-copy/assemblies`, cross-read in `game-copy/decompiled`. Output: `docs/data/routing-managed-overrides.csv`. Scratch scripts (S3SA and OBJK scans of the install) stay in the session scratchpad.

Tags as in AGENTS.md: **[verified]** = read in the IL/metadata, the decompiled C# or a scan of the installed packages; **[inferred]** = reasoning; **[community]** = public mod source or pages, linked.

## 1. Answer

- **The EP and SP packs ship no script code of their own [verified scan].** All 267 `.package` files under the Steam install (base, EP1-EP11, SP1-SP9) were scanned for S3SA resources: only `Game\Bin` holds any, and those are exactly the 10 assemblies in `game-copy/assemblies` (`gameplay.package`: `Sims3GameplaySystems`, `Sims3GameplayObjects`, `Sims3StoreObjects`, `UI`; `scripts.package`: `ScriptCore`, `SimIFace`, `Sims3Metadata`; `simcore.package`: `mscorlib`, `System`, `System.Xml`). Every pack's behaviour is already inside these DLLs (patch 1.67 unified them), so "all packs" = these 10 assemblies. Packs only decide which classes get instantiated (OBJK script class names, §5).
- **Scope reached [verified tool output].** Seven root groups (§2): the event 59 handlers and everything they call (depth 12, overrides followed: 3,818 methods), event 68 handlers (22), route setup and plan callbacks (2,038), the `RoutingComponent` hierarchy (312), the 234 plan callers of docs/93, the route actions (198) and the destination classes (12).
- **Unresolved calls [verified].** 3,193 call sites (2,575 `virt`, 464 `iface`, 154 `constrained.`) to 574 distinct declared targets. 102 targets (451 sites) are BCL or constrained calls (collections, `object` members, `IDisposable`): not routing. The other 472 game targets have **6,055 overrides/implementations** in total; 29 have none (the declared body is the only one).
- **Overrides that change a routing decision: 42 declared methods, 441 override rows** (`y` in the csv): 8 portal allow/deny (P, 182 rows), 10 search inputs set before the plan (C: options, forbidden list, slave objects, footprints; 42 rows), 16 goal/destination choices (G, 161 rows), 8 result handling (R, 56 rows). Table in §3.
- **The route API and the destinations carry no managed dispatch [verified].** Each of the 92 `Sims3.SimIFace.Route` abstract members reached has exactly one implementation (`ScriptCore.Route`), and `RouteDestination` and its four subclasses (`Point`, `Lot`, `RadialRange`, `SlotArray`) declare no virtual member at all. The only destination dispatch is native (docs/92 §1, radial vtable `0x100423C`).
- **Event 59 depends on more classes than docs/92 §5 listed [verified].** Besides the door, box stall and horse jump `IsAllowedThrough` and the 8 `OtherSimCanIgnorePrivacy` overrides, the privacy answer reaches **70 `Posture.Satisfaction` and 64 `Posture.Container` overrides** (the WooHoo, bathtub and other privacy overrides test whether the Sim is carried, standing or a walking toddler), and the door answer reaches **32 `Service.ServiceType` overrides** (constants per service class). docs/92's NonAnimatedDoor/SecretDoor note holds: their `IsAllowedThrough` overrides exist but `Sim.OnPortalPathPlan` only calls the method on a `Door`, and both derive from `CommonDoor`, not `Door`.
- **Mod risk is the real gap, not the packs [community + verified].** Closed-world analysis over EA's DLLs is complete for vanilla, but NRaas GoHere swaps every Sim's routing component and every door's portal component for its own subclasses at world load and adds its own `PortalPathPlan` handler per lot (§6). simler90's core mod replaces the two gameplay DLLs whole; MonoPatcher mods replace method bodies. A replacement search needs a runtime guard (§7): receiver runtime types at the y sites must be EA types from EA-MVID assemblies, else fall back to today's path.

## 2. Method

**Class hierarchy analysis (CHA)** in `managed-callgraph.ts` [verified implementation, self-check in §9]:
- *Class virtual* `M` declared on `T`: every method in a subclass of `T` (any assembly) with the same name and signature that is virtual and not `newslot`; a `newslot` method hides the slot for its subtree. A signature that mentions a generic type variable matches by name and parameter count (subclasses of `Base<X>` override with concrete types).
- *Interface method*: every non-interface class whose interface closure (own, base interfaces, base classes) contains the interface, with its explicit `MethodImpl` or the matching virtual found up its base chain, plus every class override of that implementation.
- *Closure*: breadth first from the roots over `call`, `cv`, `virt` (declared body), `ifaceN` and `newobj` (docs/78). Groups marked "follow" also enter every override of each `virt`/`iface` site, so the override bodies' own virtual calls are listed too. Delegate invokes are not followed (§8).
- *Csv*: one row per (declared method, override); the call-site column gives the number of calling methods and the first three. A declared method reviewed `n` with more than 40 overrides gets one summary row (18 such, e.g. `GameObject.OnStartup` 941, `InteractionInstance.Cleanup` 1,330). The `affects routing decision` column comes from the hand review list `REVIEW` in the tool (§3), so the csv is reproducible.

**Root groups** (`ROUTING_GROUPS` in the tool):

| Group | Roots | Depth | Follow overrides | Methods reached |
|---|---|---|---|---|
| portal | `LotManager.PortalLockedCallback`, `Lot.OnPortalPathPlan`, `Sim.OnPortalPathPlan`, `PrivacySituation.OnPortalPathPlan` (docs/92 §5) | 12 | yes | 3,818 |
| event68 | `Lot.OnLevelChanged`, `LotManager.OnLotLevelInfoChanged`, `HudModel.OnLotLevelInfoChange` (docs/92 §4) | 2 | yes | 22 |
| setup | every `AboutToPlan*`/`PostPlan*`/`UpdateFollowersOnReplan` callback (taking `Route, string`), all of `ScriptCore.Route` and `Sims3.SimIFace.Route` | 4 | yes | 2,038 |
| component | `RoutingComponent` and all subclasses (`SimRoutingComponent`, car, boat, bird, marine life) | 0 | no | 312 |
| consumer | the callers of the four plan methods on either route class (docs/93 §6.2) | 0 | no | 234 |
| routeaction | `RouteAction` and all subclasses (route actions by `PathType`, interrupt actions) | 0 | no | 198 |
| destination | `RouteDestination` and subclasses | 0 | no | 12 |

Depth was set by size: the event 68 closure at depth 12 with overrides followed reaches 8,776 methods through `GameObject.SetOpacity` and visual-effect overrides (no routing effect), and the setup closure at depth 6 reaches 10,269. At depth 12 the portal closure already drifts into tooltip, localization and lazy-init code (`GameObject.GetLocalizedName`, `Skill.LifetimeOpportunities`, weather profiles): of its 212 game declared targets, the routing ones are all within four calls of a handler [verified: the 441 y rows are identical with the portal group at depth 4 and at depth 12].

## 3. Overrides that change routing decisions

Categories: **P** portal allow/deny (event 59 answer), **C** search input set before the plan (route options, forbidden portals, ignore/slave objects, footprints in the routing world), **G** goal or destination choice, **R** handling of the plan result (route action per subpath, replan, fail). "Packs" = packs whose OBJK script classes instantiate the overriding class or a subclass (§5); "-" = not an object class (postures, components, services, situations), pack from the namespace [inferred].

| Cat | Declared method (sites) | Overrides | Assemblies | Packs | What it decides |
|---|---|---|---|---|---|
| P | `CommonDoor.IsAllowedThrough(int,int,Sim)` (1: `Sim.OnPortalPathPlan`) | 4 (2 reachable: `DoorSingleGender`, `Turnstile`) | Systems, StoreObjects | base, store | door lock, service, greeted, VIP, gender, ticket rules |
| P | `IBoxStall.IsAllowedThrough` | 1 (`BoxStall`) | Objects | EP5 | stall gate per running interaction |
| P | `IHorseJump.IsAllowedThrough` | 1 (`HorseJump`) | Objects | EP5 | jump per running/linked interaction |
| P | `InteractionInstance.LinkedInteractionInstance` | 3 | Systems | - (EP5 riding) | what the horse jump test reads |
| P | `PrivacySituation.OtherSimCanIgnorePrivacy` | 8 (generic, bathtub, shower, sponge bath, toilet, urinal, WooHoo, store routine machine) | Systems, Objects, StoreObjects | base, store | privacy room lock |
| P | `Posture.Satisfaction` | 70 | all three | base, EP2-EP11, SP4, SP9, store | carried/standing/toddler tests inside the privacy overrides |
| P | `Posture.Container` | 64 | all three | same | the carrying Sim in the WooHoo privacy rule |
| P | `Service.ServiceType` | 32 | Systems, Objects | - (base services plus EP ones, e.g. EP9 mascot, EP10 resort staff) | door rule for service NPCs (constant per class) |
| C | `GameObject.IsInPublicResidentialRoom` (3) | 6 (5 door/shell classes reachable; `Lot` not) | Systems, Objects | base, EP3, EP6, EP9, EP11 | `SimRoutingComponent.AboutToPlanRouteCallback` puts non-public doors on the forbidden-portal list |
| C | `RoutingComponent.CreateRoute` (17) | 5 (Sim, car, boat, bird, marine life) | Systems, Objects | - | route options and AGS per mover |
| C | `Terrain.GoHere.SetSpecialRouteOptions` (3) | 3 (swim here, swim here with, boat here with) | Systems | - (EP10) | swim/boat options |
| C | `SocialJigTwoPerson.AddRouteOptionsToJigA/B` | 1 each (nurse foal) | Systems | base, EP5 | jig route options |
| C | `Posture.HasSlaveRouteObjects` | 2 (being ridden, leading horse) | Systems | - (EP5) | slave route objects |
| C | `IBoat.IsSlaveRoute` | 8 (boats, windsurf board, houseboat) | Systems | EP10 | boat slave route objects |
| C | `IBoat.UsesTurnHelperFootprint` | 2 | Systems | EP10 | boat turn footprint |
| C | `DynamicRegionBase.UpdateFootprint` | 3 (discourage, impassable, social group discourage) | Systems | - | dynamic footprints in the routing world |
| C | `ObjectWithFlexibleBuyModePlacement.UpdateGeoStateAndFootprint` | 7 (adventure rewards) | Objects | base (World Adventures reward objects by name [inferred]) | object footprint state |
| G | `IGameObject.SimLine` (1: `PreRouteCheckForLine`) | 56 | all three | base, EP2-EP11, SP9, store | where the Sim queues |
| G | `IViewableBase.GetViewRouteTarget` (2) | 34 | all three | base, EP3-EP11, store | view-object route target |
| G | `PortalComponent.GetStartSlotsForApproachingSim` | 24 | Systems, Objects | base, EP2, EP3, EP5, EP7, EP10, EP11 | portal approach slots |
| G | `SocialJig.GetSlotForActor` / `RouteToJigA` | 13 / 3 | Systems, Objects | base, EP5, EP9 | jig slot and route |
| G | `MarineLifeState.WanderRoutePrePlanning` | 5 (shark states) + base | Systems, Objects | EP10 | fish/shark destination |
| G | `Shell.GetRouteToShell` | 5 | Systems, Objects | base, EP6 | shell/rabbit hole entry |
| G | `PartData.GetContainmentSlot` / `RouteTarget` | 3 / 1 | Systems, Objects | EP4, EP7, EP9, EP11, SP4 | part slot (sandbox, bunk bed upper) |
| G | `CommonDoor.GetRoutingSlotsOnSide`, `SimQueueBehindSlotLine.AlterRadialRangeDestination`, `BarAdvanced.RoutingSlotBartender`, `Stereo.RouteDistanceForUse`, `HorseJump.Start/EndSlots`, `Vehicle.RouteToAndGetIn` | 1-2 each | Systems, Objects | base, EP3, EP5, EP8, EP10 | slots, radius, line spot, vehicle entry |
| R | `RouteAction.PerformAction` (2) | 28 | Systems, StoreObjects | base, EP8, store | the action per subpath; interrupt actions replan, change destination or fail |
| R | `FollowRouteAction.ForceReplan` | 7 | Systems | - | replan from the action |
| R | `PortalComponent.LockRoutingLane` / `IsFullyBlocked` | 4 / 1 | Systems | base, EP3, EP10 | lane lock failure at a portal |
| R | `RoutingComponent.DoRoute` (15), `OnAboutToStartFollowingRoute`, `GameObject.DoRoute` (149) | 3 / 3 / 2 | Systems | base, EP10 | following, followers, vehicles, marine life |
| R | `GoHereWithChildSituation.OnRouteReplan` | 2 | Systems | - | follower replan in a post-plan callback |

Reviewed **n** although routing-named or close (examples; the csv has all): `PortalComponent` animation and lane hooks (`OnPortalStart` 24, `GetLaneFlagsForLaneIndex` 8, `OnApproachingPortalObject` 5, `DoPortal` 3: traversal after the plan), steering and script adaptors (`Init`, `PreRoute`, `PostRoute`), `RouteAction.RoutingObject`, `GameObject.InUse` (48: decides whether a route is tried, not the route), `IGameObject` members with a single implementation (`LotCurrent`, `RoomId`, `Position`, slot helpers: `GameObject` only), `BuildableShell.OnLotLevelChanged` (3, event 68: lights and opacity).

**What this adds to docs/92 §5's verdicts [inferred].** All P rows depend only on the Sim, the portal object and the Sim's running interaction, so the per-plan portal answer table (b-submit) still covers them; the new inputs are the posture class (privacy) and the service class (doors). C, G and R rows run on the sim thread before or after the plan in today's code and stay there in the snapshot design; they matter to the shadow compare (docs/93 §9) only through the route options, forbidden list and goals they produce, which the job record copies.

## 4. Not routing

- **BCL and constrained calls** (102 targets, 451 sites): collection enumerators and comparers, `object.ToString/Equals/GetHashCode`, `IDisposable.Dispose`, `Delegate` internals.
- **Big generic virtuals** (18 collapsed rows): lifecycle (`OnStartup`, `OnCreation`, `OnLoad`, `Destroy`), cleanup, thumbnails, posture idles and transitions, lifetime opportunity UI.
- **Event 68** (docs/92 row 12): the C# side is visual only (shell lights, opacity, HUD); its 22-method closure (depth 2) has no routing input [verified]; deeper levels only fan out through opacity and visual-effect overrides.
- **Deep portal closure**: past four calls the handlers reach logging, tooltip and lazy-init paths (`Sim.ToTooltipString`, `Occupation` work-hour getters, skill opportunity listeners); none returns into the lock answer [inferred from the call paths: they sit behind exception and tooltip branches].

## 5. Pack attribution

- **Assemblies**: Systems 254 of the 441 y rows, Objects 161, StoreObjects 26 [verified].
- **Packs**: OBJK resources (type `0x02DC343F`) name each catalog object's script class. A scan of all 10,084 OBJK resources in the install gives 3,833 class names [verified scan]. An override's pack is the union over its outermost class and that class's subclasses; base-game classes appear in `GameData` and, because the patch copies content, also in EP1-EP3 and SP1-SP3 for many classes, so "base" wins when `GameData` is present. Of the 441 y rows, 93 map to base, 26 to Store content (`Sims3StoreObjects`, installed from Store downloads, not the Steam packs), and per pack: EP5 Pets 36, EP10 Island Paradise 33, EP8 Seasons 19, EP11 Into the Future 16, EP9 University 14, EP3 Late Night 13, EP7 Supernatural 12, EP4 Generations 10, EP6 Showtime 6, EP2 Ambitions 5, SP4 and SP9 3 each. 170 rows belong to non-object classes (postures, components, services, route actions) and carry "-".
- **EP-only routing decisions** worth a repro each (`tools/harness/repros.json`): box stall and horse jump locks (EP5), boat slave routes and houseboat lanes (EP10), elevator lane locks (EP3), shell doors' public-room flag (EP3/EP6/EP9/EP11), shark wander goals (EP10), store turnstile tickets (Store).

## 6. Mods that replace these classes [community]

| Mod | What it changes on this list | Source |
|---|---|---|
| NRaas GoHere | At world load, adds `SimRoutingComponentEx` (a `SimRoutingComponent` subclass overriding `CreateRoute` and `DoRoute`) to every Sim and `DoorPortalComponentEx` (a `Door.DoorPortalComponent` subclass overriding `OnApproachingPortalObject` and `OnLaneLocked`) to every door; registers its own `PortalPathPlan` handler on lots with door settings and turns the lot's portal lock callback on; its `RoutingComponentEx.DoRoute` replaces the result handling for Sims and cars (door filters, costs, time locks, route-fail behaviour) | https://github.com/Chain-Reaction/NRaas (`NRaasGoHere/GoHereSpace/Helpers/SimRoutingComponentEx.cs`, `DoorPortalComponentEx.cs`, `RoutingComponentEx.cs`; docs/03 §5) |
| NRaas StoryProgression | reads each lot's `PortalPathPlan` invocation list (situation manager) | same repo, `NRaasStoryProgression/StoryProgressionSpace/Managers/ManagerSituation.cs` |
| NRaas Woohooer | replaces WooHoo interactions and their privacy rules (the WooHoo privacy override above) | https://www.nraas.net/community/Woohooer [community, page not re-read here] |
| simler90's Gameplay Core Mod | ships its own `Sims3GameplaySystems.dll` and `Sims3GameplayObjects.dll` (every class above can differ; docs/92 notes `CommonDoor`) | https://modthesims.info/d/659969/simler90-s-gameplay-systems-core-mod.html (docs/07 §2.1) |
| MonoPatcher-based mods | replace any method body at run time, virtual or not | https://github.com/LazyDuchess/MonoPatcher |
| Object CC with scripts | new `Door`, `Posture`, `PrivacySituation`, `PortalComponent` subclasses from Mods\Packages S3SA (docs/17: the Store repacks alone carry 66 S3SA) | docs/17 [verified scan of Rodney's test set] |

So the closed-world override list is complete only for EA's assemblies: GoHere alone puts mod subclasses behind `RoutingComponent.CreateRoute`, `RoutingComponent.DoRoute` and the door portal component on every Sim and door, and adds an event 59 handler. The MVID check of docs/92 §8 condition 2 catches simler90 but not GoHere (GoHere leaves EA's DLLs untouched) nor MonoPatcher.

## 7. Consequence for the replacement search

Extend docs/92 §8's handler-set guard with a **receiver type guard** at submit time [inferred design, to build with the PAT]:
1. The Sim's routing component, every portal object on the touched lots, that portal's portal component, the Sim's posture, any live privacy situation and the route's slave objects must have a runtime type whose declaring assembly is one of the 10 EA assemblies with its EA MVID (docs/78 index stores them). Any other type means a mod override is in the path: run the plan as today.
2. The csv's y rows are the methods those runtime types can dispatch to; the guard needs no per-method check while (1) holds.
3. MonoPatcher replaces bodies without changing types; detecting it needs a check of the patched methods' native code pointers (docs/59 §3.6 keyed mods by runtime type and MVID, which misses it). Until then, treat a MonoPatcher install as "fallback always" by its presence in Mods.

## 8. Limits

- No receiver typing: CHA lists every override of the declared target, also ones the receiver's static type rules out (the NonAnimatedDoor case was checked by hand; others may be over-listed).
- Delegate invokes (`dinv`) are not resolved; the two that matter for routing were enumerated by source search in docs/92 §5 (two vanilla `PortalPathPlan` subscribers) and docs/93 §6.2 (plan callbacks: `SimRoutingComponent`, `CarRoutingComponent`, `TombRoomManager`, `BunkBedContainer`, plus post-plan callbacks in `HorseJump`, `TrainingPosts`, `Shark`, three `MarineLifeState`s, `Sim`, `GoHereWithSituation` and `Stroller`, all roots of the setup group here).
- Generic signature matching by parameter count can over-match overloads of the same name and arity on generic bases [inferred: no such case in the y rows].
- The y/n review is by declared method plus named exceptions; a y declared method marks all its overrides y even where one override returns a constant that cannot change the answer.
- Pack attribution covers object classes only; postures, components and services carry "-".

## 9. Reproduce

```sh
node tools/managed-callgraph.ts --self-check --asm <repo>/game-copy/assemblies
node tools/managed-callgraph.ts overrides Sims3.Gameplay.Situations.PrivacySituation::OtherSimCanIgnorePrivacy --asm <repo>/game-copy/assemblies
node tools/managed-callgraph.ts dispatch --asm <repo>/game-copy/assemblies --packs <objk-classes.json> --csv docs/data/routing-managed-overrides.csv
```

The self-check now also asserts: the privacy overrides include the generic and the Store one, the door overrides include `DoorSingleGender` and `Turnstile`, `Route.Plan` has exactly `ScriptCore.Route::Plan()`, and `IBoxStall` has exactly `BoxStall`. `--packs` takes a JSON map of OBJK script class name to pack folders (scratch scan of the install; without it the packs note is left out).

## 10. Ledger

Rows I-103-1 to I-103-7 in docs/51 §37.
