# 104. Routing mod guard: when our search must give the plan back to the game

Written 2026-10-18. Static only: the game, the probe server and the queues were not run. Goal (PLAN item 6): our own route search (snapshot search with the per-plan portal answer table of docs/92 §8, "PAT") must give the same answers as the game. Mods can change routing in script. This doc says exactly which managed code our search depends on, how native code can tell cheaply that a mod changed it, what each check costs, and what we log when we fall back to the game's own path. It is a spec for a later builder; no code was written.

Inputs: docs/92 (portals, event 59 handlers, mod risk), docs/103 and `docs/data/routing-managed-overrides.csv` (overrides that change routing decisions, mod list §6, receiver guard idea §7), docs/59 §3.6 (allow-list keyed by runtime type and MVID), docs/29, docs/56, docs/58 (embedded Mono, interpreter, loader). Tools: capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67, scratch scripts in the session scratchpad `io-q/`), `tools/managed-callgraph.ts` (a scratch copy with the portal group at depth 4 instead of 12), the decompiled C# in `game-copy/decompiled` (read only), and our own `mods/ts3fixes` and `mods/ts3agent` source.

Tags as in AGENTS.md: **[verified]** = read in the disassembly (address given), the IL index, the decompiled C# or our own source; **[inferred]** = reasoning or stock Mono 1.2.3 knowledge, to confirm with the docs/51 §38 rows; **[community]** = mod pages or source, linked in docs/103 §6.

## 1. Answer

- **The search depends on one piece of managed code: the event 59 chain** [verified, docs/92 §4]. Everything else the C# side does (route options, forbidden portals, goals, follower setup, result handling) runs on the sim thread before submit or after collect, in today's code and in the snapshot design, and reaches the search only as data the job record copies. So the guard has three tiers:
  - **S (search answer):** code that runs inside World event 59 (`LotManager.PortalLockedCallback` -> `Lot.OnPortalPathPlan` -> lot handlers -> their virtual calls). A mod change here can change a portal answer, or make the PAT's extra calls (all portals, both directions, instead of the lazy subset) visible as side effects. Guarded always.
  - **M (moved code):** C, G and R rows of docs/103 §3 and World event 68. They only need a guard if a later step moves them off the sim thread or replaces them natively (A10 whole Replan family, native line of sight for radial goals, docs/92 §8 point 5). Listed in §3.4 with the trigger.
  - **N:** traversal and animation hooks (`PortalComponent.OnPortalStart`, GoHere's `DoorPortalComponentEx` overrides), UI, tooltips. Never guarded.
- **Four kinds of mod change reach tier S, each with a cheap native test** (§5):
  1. A replaced EA assembly (simler90's core mod): compare the GUID string Mono keeps per image (`image+0x28`) with the EA MVIDs from the docs/78 index. Once per world load, 10 string compares.
  2. An added or replaced handler (NRaas GoHere's `PortalPathPlan` handler, any mod `World.OnPortalLockedCallback` subscriber): walk the delegate chain in the native event 59 slot `0x11D72C4` and in each touched lot's `mOnPortalPathPlanHandler` field; each element's method must be one of three EA `MonoMethod`s. Per submit, a few loads per lot.
  3. A mod subclass that overrides a guarded virtual (CC doors, mod postures, interactions, privacy situations, services): a **census at class vtable creation** marks every non-EA class whose vtable slot for one of the 35 guarded virtuals points to non-EA code. Empty census = no per-object work at all (expected for vanilla, GoHere and most setups [inferred]). Non-empty census = check the runtime class of the plan's receivers (portal objects natively, Sim-side objects in ts3fixes) against it.
  4. A replaced method body (MonoPatcher `ReplaceMethod`, our own ts3fixes patch host, `ReplaceIL`, `ForceJIT`): a header swap rewrites the original `MonoMethod`'s class pointer (`+0x08`) to the replacement's class, so **`m->klass == declaring class`** over the 550 methods of the depth-4 event 59 closure catches it; `ReplaceIL` and `ForceJIT` patch three known code sites whose vanilla bytes are fixed (`0xE5CA90`, `0xE5637D`, `0xE648DC`).
- **Our own patch is inside the guarded closure** [verified]: ts3fixes swaps `Lot.GetHoursOfOperation` (hoursmemo, docs/74), which `DoorSingleGender.IsAllowedThrough` reaches through `Lot.IsOpenVenue`. The guard must accept exactly that swap (target, hook class `HoursMemoHook`), or every vanilla install would fall back.
- **A dispatch audit does not work as a backstop** [verified]: EA's interpreter keeps a one-entry inline cache per `callvirt` site (class -> target, `0xE54AF4`-`0xE54B22`) and calls the resolver `0xE51FC0` only on a miss, so a hook there misses most dispatches. The census replaces it.
- **Costs** (§6) [inferred, to measure]: load-time work about 1-5 ms once per world load; per submit about 1-3 µs in the common case (no census hits), plus 0.3-1 µs per touched lot; a rolling method-integrity slice of 64 methods per submit (about 0.3 µs). Against plans of 2-25 ms (docs/04), the guard costs well under 0.1%.
- **Fallback** = the plan runs as today (sync, or today's async with lazy event 59 calls proxied to the sim thread). One log line per reason and object class per session, a 10-minute summary, and counters (§7).

## 2. What the guard has to protect

The snapshot search (docs/92 §8) replaces the lazy event 59 calls with a table built on the sim thread at submit. Three variants are planned; each needs the S tier for a different reason [inferred]:

| PAT variant | What it does with event 59 | Why a mod change breaks equality |
|---|---|---|
| b-submit | calls the registered chain for every portal of the touched lots, both directions, at submit | the call set differs from the lazy one: a mod handler or override with side effects (counters, logs, GoHere's route-fail bookkeeping) runs for portals the game would not ask about, and a stateful mod answer can depend on call order |
| b-cached | reuses answers across plans with change hooks (docs/92 §5 rows 2-5, 8-10) | the hooks watch the state EA's code reads; mod code can read anything |
| a (native) | evaluates rows natively, skips the managed chain | mod code never runs at all |

So the rule is the same for all three: **every method that can run inside event 59 must be EA's unmodified code or one of our own known swaps**. When that holds, docs/92 §5 already showed the answers are a pure function of the Sim, the portal object and the Sim's running interaction (apart from `LockedDoorsDuringPlan`, reproduced at collect).

## 3. Guard list

The S tier is the depth-4 event 59 closure with overrides followed: **550 methods, 43 declared virtual or interface targets in 27 declaring types (35 of them game types, 8 BCL)** [verified: scratch copy of the tool, portal group at depth 4; the 441 routing-decision rows of the csv are identical at depth 4 and 12, docs/103 §2]. The 8 targets that change a portal answer (P rows of docs/103 §3) are `CommonDoor.IsAllowedThrough`, `IBoxStall.IsAllowedThrough`, `IHorseJump.IsAllowedThrough`, `InteractionInstance.LinkedInteractionInstance`, `PrivacySituation.OtherSimCanIgnorePrivacy`, `Posture.Satisfaction`, `Posture.Container`, `Service.ServiceType`. The other 27 game targets (`IGameObject` members, `ISurface.Surface`, `PartData.InUse`, `ObjectComponent.BaseType`, chair and snack helpers inside posture `Satisfaction` overrides, and the depth-drifted `PortalComponent` rows) do not decide the answer but run inside the chain; a mod override of them changes the PAT's side effects only. The census uses all 35 (strict); a lenient build may use the 8.

### 3.1 At load (script host init, after ts3fixes `Apply`, after each world load)

| # | What | Why it matters | Check |
|---|---|---|---|
| L1 | The 10 EA script assemblies (`ScriptCore`, `SimIFace`, `Sims3GameplaySystems`, `Sims3GameplayObjects`, `Sims3StoreObjects`, `UI`, `Sims3Metadata`, `mscorlib`, `System`, `System.Xml`) | a core mod (simler90) replaces `Sims3GameplaySystems`/`Sims3GameplayObjects` whole; every S method can differ | image GUID string vs the table in §5.1 |
| L2 | MonoPatcher's native hooks: `GenerateCode` detour at `0xE5CA90` (`ReplaceIL`), JIT cache checks at `0xE5637D`, `0xE648DC` (`ForceJIT`) | `ReplaceIL` changes a body without touching the `MonoMethod`; `ForceJIT` recompiles callers so late swaps take effect | byte compare (§5.4); repeated per submit for the two `ForceJIT` sites, which MonoPatcher writes on demand (docs/68) |
| L3 | Header integrity of the 550 S methods (resolved once to `MonoMethod*`) | `ReplaceMethod`-style swaps (MonoPatcher, other patchers, our patch host) | `klass` field check (§5.4) |
| L4 | The ts3fixes swap whitelist inside S: `Lot::GetHoursOfOperation(float&,float&)` -> `HoursMemoHook` [verified: `mods/ts3fixes/src/ScriptHotSpots.cs`, path through `DoorSingleGender.IsAllowedThrough` -> `Lot.IsOpenVenue`, both from the tool's `callers`] | our own patch must not trip the guard | accept a swapped S method only if its new class is the listed hook class in the ts3fixes image (§5.4) |
| L5 | Guarded slot table: for each of the 35 game targets, the declaring `MonoClass*` (or interface) and its slot (`MonoMethod+0x1C >> 12`) | input to the census | resolved once by name (§5.2) |
| L6 | Event 68 subscribers (`World.OnLotLevelInfoChangeCallback`: vanilla `LotManager.OnLotLevelInfoChanged`, `HudModel.OnLotLevelInfoChange` [verified: the only two `+=` sites]) | **M tier only**: matters if goal building moves or the radial line of sight goes native (docs/92 §1, event 68 would no longer fire) | delegate walk of the static field (§5.3) |

### 3.2 At plan submit (sim thread, ts3native's async START, `RouteBackoff.cs` handshake)

| # | What | Why | Check |
|---|---|---|---|
| P1 | World event 59 delegate in `g_WorldEventDelegates[59]` = `0x11D72C4` [verified: `IC_World_RegisterCallbackDelegateImpl 0x7861A0` stores the delegate object at `0x11D6A78 + event * 0x24`, `World_FireEvent 0x78F2B0` reads it; `World_FirePortalLockedQuery 0x791C00` passes `0x3B`] | a mod `+=` on `World.OnPortalLockedCallback` (every add or remove re-registers the combined delegate [verified: `World.cs` event accessor]) | chain of exactly one element, method `LotManager::PortalLockedCallback` |
| P2 | Each touched lot's `Lot.mOnPortalPathPlanHandler` | NRaas GoHere adds its own handler on lots with door settings [community]; StoryProgression only reads the list | every chain element's method is `Sim::OnPortalPathPlan` (static) or `PrivacySituation::OnPortalPathPlan` (instance; target class checked as in P4) |
| P3 | `ForceJIT` bytes (L2) and the integrity generation (§5.4) | MonoPatcher can patch at run time | 2 byte compares; re-run L3 when they changed; else one rolling slice |
| P4 | Census state (§5.5) | decides whether per-object checks run at all | one load: zero = skip §3.3 |
| P5 | Negative caches: lots whose handler list failed P2, census classes | avoid repeating work | hash lookups |

"Touched lots" = the lots whose portals the PAT covers (start, goal and transit lot legs, docs/92 §8 point 1).

### 3.3 Per object (only for census bases that have members; sim thread at submit)

| # | Receiver | Guarded targets on it | Where the class comes from | Check |
|---|---|---|---|---|
| O1 | Each portal owner object on the touched lots (doors, gates, stalls, horse jumps, poles, elevators, ladders, turnstiles) | `CommonDoor.IsAllowedThrough`, `IBoxStall`/`IHorseJump.IsAllowedThrough`, `GameObject.IsInPublicResidentialRoom` (M), `IGameObject` members | portal record `+0x08` object id [verified docs/92 §2.1] -> managed object through `IC_Simulator_GetTaskImpl 0x76CA60` (host -> simulator -> find by id) [verified call shape; returns the managed task object, inferred] | class not in census for these bases |
| O2 | The lot's privacy situations (instance delegate targets in P2) | `PrivacySituation.OtherSimCanIgnorePrivacy` | delegate `+0x08` target [verified layout §4] | class not in census |
| O3 | The Sim's current posture | `Posture.Satisfaction`, `Posture.Container` | Sim-side (ts3fixes, below) | class not in census |
| O4 | The Sim's current, transition and linked interactions | `InteractionInstance.LinkedInteractionInstance` (horse jump, box stall, privacy priority) | Sim-side | class not in census |
| O5 | The Sim's service (service NPCs) | `Service.ServiceType` | Sim-side | class not in census |

O3-O5 are read on the managed side because their native paths go through managed fields of `Sim` and `InteractionQueue` that native code should not chase. ts3fixes' `PlanAsync` already runs at submit with the Sim in hand [verified: `RouteBackoff.cs`]; it asks ts3native for the census bits (one call through the existing `GetOption` handshake, new option bit) and, only when a Sim-side base has members, compares `GetType()` of the three receivers against a `Hashtable` of census types filled once per class [inferred design]. With an empty census (the expected case) this is one option read.

### 3.4 Not in the S tier (M tier: guard only if the design moves them)

| docs/103 rows | Runs today | Becomes guarded when |
|---|---|---|
| C (10 declared, 42 rows): `RoutingComponent.CreateRoute` (GoHere `SimRoutingComponentEx`), `SetSpecialRouteOptions`, jig route options, slave route objects, boat footprints, `DynamicRegionBase.UpdateFootprint`, `IsInPublicResidentialRoom` in `AboutToPlanRouteCallback` | sim thread, before the plan; the options, forbidden list and footprints reach the search as copied data | the job record stops copying them (for example a worker-side replan that rebuilds options) |
| G (16 declared, 161 rows): lines, view targets, portal approach slots, jig slots, shark wander, shell entry, part slots | sim thread, goal building before `Route_PlanLocked` | A10 moves `Route_PlanCommon` goal building to the worker (docs/87) |
| R (8 declared, 56 rows): `RouteAction.PerformAction`, `ForceReplan`, lane locks, `DoRoute` (GoHere `RoutingComponentEx.DoRoute`) | sim thread, after collect | never for the search answer; the async handoff already keeps their order (`PlanOrig` runs after the handoff) |
| Event 68 (`Lot.OnLevelChanged`, HUD) | sim thread inside goal building | radial line of sight goes native (docs/92 §8 point 5): then L6 decides whether skipping the event is invisible |
| Route object callbacks (`AboutToPlan*`, `PostPlan*`) | sim thread around the plan IC | the async collect changes their order relative to the plan |

## 4. Mono structures the checks read

EA's Mono differs from stock 1.2.3 in object header size and class layout, so every offset below was read in this exe.

| Structure | Offset | Content | Tag |
|---|---|---|---|
| `MonoObject` | `+0x00` | `MonoVTable*`; **header is 4 bytes, no sync field**: `MonoString` length at `+4`, chars at `+8` (`0xEA401F`), reflection objects' payload at `+4` (`0xE53375`, `0xE533D4`) | verified |
| `MonoVTable` | `+0x00` | `MonoClass*` (`0xEA0FBC`, `0xE51FE5`) | verified |
| `MonoClass` | `+0x00` | `MonoImage*` (`0xE6DDF0`; `image+0x298` class cache and `+0x30` mempool used right after) | verified |
| | `+0x24` | parent (`mono_class_setup_parent 0xE6DB40`, store `0xE6DBD8`; NULL for `System.Object`) | verified |
| | `+0x28` | nesting class (`0xE6DEA1`) | verified |
| | `+0x30` / `+0x34` / `+0x38` | typedef token / name / namespace (`0xE6DDEA`-`0xE6DDF3`) | verified |
| | `+0x3C` / `+0x40` | supertypes array / depth (u16) (`mono_class_setup_supertypes 0xE6AA00`) | verified |
| | `+0x60` | TypeDef flags; bit `0x20` = interface (`0xE51FE1`) | verified |
| | `+0x70` / `+0x94` | method count / `MonoMethod*` array (`mono_class_get_method_from_name_flags 0xE70330`) | verified |
| | `+0xBC` | virtual slot table, `MonoMethod*` per slot (`0xE52019`, `0xEA0FE1`, set-up check `0xE6EBA6`) | verified (docs/58 names `+0x8C` for `0xE6A030`; not re-read here) |
| `MonoImage` | `+0x28` | GUID string of the first `#GUID` heap entry, `"%02X%02X%02X%02X-..."` (`0xE83218`-`0xE83221`, formatter `0xE9D8B0`, format at `0x10ECE30`) | verified |
| | `+0x50` / `+0x54` | `#GUID` heap data / size (assert size >= 16 at `0xE831F2`) | verified |
| `MonoMethod` (40 bytes) | `+0x00` / `+0x02` | flags / impl flags (`0xE8653D`, `0xE86537`) | verified |
| | `+0x04` | metadata token | inferred (stock layout) |
| | `+0x08` | declaring `MonoClass*` (`0xE86534`) | verified |
| | `+0x0C` | signature (`0xE86525`) | verified |
| | `+0x14` | interpreter `RuntimeMethod*`, made on first use by `0xE51F20` (0x48 bytes, `RuntimeMethod+0x08` = the `MonoMethod`, store back at `0xE51F8B`) | verified |
| | `+0x18` | name (kept by the patch host swap) | verified (docs/04 F8) |
| | `+0x1C` | bit field, slot = value `>> 12` (signed, -1 = none) (`0xEA1027`, `0xE52007`) | verified |
| `MonoClassField` | `+0x00` / `+0x08` | type / instance offset including the 4-byte header (`mono_field_get_value 0xEA1350`, `0xD7E93A`) | verified |
| `System.Delegate` | `+0x08` | target object (`m_target`, `0xE533FE`) | verified |
| | `+0x10` | `RuntimeMethod*` (field `method_ptr`, `0xE5342A`) | verified |
| | `+0x18` | `MethodInfo` object (`0xE533F2`) | verified |
| `System.MulticastDelegate` | `+0x1C` / `+0x20` | `prev` / `kpm_next` (declaration order after the six `Delegate` fields, matching the three verified offsets) | inferred |
| World event table | `0x11D6A78 + id * 0x24` | delegate object per event id (`0x7861B0` store, `0x78F2BF` read; `id <= 0x4B`); event 59 at `0x11D72C4`, event 68 at `0x11D7408` | verified |
| Script host | `[0x11EE514]` | host singleton (docs/06); `ScriptHost_FindClass 0xD79CA0` (this = host, args namespace, name) returns `MonoClass*`; host `+0xAC4` = domain for `mono_class_vtable` (`0xD7E8F3`-`0xD7E912`) | verified call shape |
| Static fields | | `mono_class_get_field_from_name 0xE6F720` (class, name) and `0xE9DD40` (vtable) = static data base; address = base + field offset (`0xD7E917`-`0xD7E93D`, `sSimTickCnt`) | verified call shape, names inferred |

Virtual dispatch in the interpreter [verified]: each `callvirt` site holds a two-word cache (`MonoClass*`, `RuntimeMethod*`); on a hit (receiver class equal, `0xE54AF9`) the target comes from the cache, on a miss `0xE51FC0` resolves it (receiver `ecx`, declared `RuntimeMethod` `ebx`; interface targets through `0xD70870` (class, interface id at `MonoClass+0x44`), class targets through `class+0xBC` slot) and the site stores the new pair (`0xE54B1A`-`0xE54B22`). `ldvirtftn` (`0xE58396`) calls the resolver without a cache. Consequence: a method header swapped after a site cached its target is not seen by that site, and a hook on `0xE51FC0` sees only misses.

## 5. Detection methods

### 5.1 Assembly identity (L1)

- Get each EA image from one anchor class: `ScriptHost_FindClass` (`ScriptCore.Simulator`, `Sims3.SimIFace.World`, `Sims3.Gameplay.Core.Lot`, a `Sims3GameplayObjects` class such as `Sims3.Gameplay.Objects.Plumbing.Urinal`, `Sims3.Store.Objects.Turnstile`, one `UI` and one `Sims3Metadata` class, `System.Object`, one `System` and one `System.Xml` class), then `MonoClass+0x00` -> `MonoImage+0x28`.
- Compare the string with the docs/78 index MVIDs formatted the Mono way (bytes 3,2,1,0 - 5,4 - 7,6 - 8,9 - 10-15) [verified formatter order at `0xE9D8B0`]:

| Assembly | Mono GUID string (`image+0x28`) |
|---|---|
| ScriptCore | `3AF857EE-565F-4F38-9EDE-12B34A086C39` |
| SimIFace | `3D0E30C5-DBFE-42F3-BE98-52248E3D39B9` |
| Sims3GameplayObjects | `B044CCB2-EE6F-4439-9DAB-2FB6B07E9995` |
| Sims3GameplaySystems | `93888136-F497-44AA-B399-A2926B2454FC` |
| Sims3Metadata | `7F39EC2E-B364-466F-9F79-C571199B0FC5` |
| Sims3StoreObjects | `209272BD-A8CC-455A-9609-352DE1E7CCD4` |
| System.Xml | `8C150F97-4B82-46BA-A7C7-BAE31C76BA74` |
| System | `7F6A8FA8-A197-4182-A32E-0925433AE1F2` |
| UI | `F8828361-C8AF-4AEF-BEC3-CCF3956EE8CB` |
| mscorlib | `6701D6AE-713F-4943-B8AF-D3B84087182A` |

  The index stores the Module row's MVID; Mono stores the first `#GUID` heap entry. They are the same entry when the Module row's MVID index is 1, which compilers emit [inferred: the builder asserts index 1 when generating the table].
- Result: a set of 10 `MonoImage*` ("EA images"). Any mismatch or missing anchor: guard off for the session (fallback for every plan), log once. A mismatch in `UI` or `Sims3Metadata` alone does not reach S; the strict build still falls back, a lenient one may ignore those two.
- "Is this class EA?" is then `klass->image` in a 10-entry array (or one compare against a range of known pointers): 1 load and up to 10 compares.

### 5.2 Classes, slots and subclass test (L5, census, O1-O5)

- Declaring classes of the 35 guarded targets: `ScriptHost_FindClass` by namespace and name (nested types through their outer class, `MonoClass+0x28`), method by name and parameter count through `mono_class_get_method_from_name_flags 0xE70330`, slot from `MonoMethod+0x1C >> 12`. For interface targets keep the interface `MonoClass*` and the method's index in it.
- Subclass test (class C derives from D): `C+0x40 >= D+0x40` and `C+0x3C[D+0x40 - 1] == D` [verified fields; test is the stock Mono idiom, inferred]. 3 loads.
- Interface implementation: through `0xD70870` (C, interface id) as the resolver does [verified call shape]; or, cheaper for the census, take the vtable entry the resolver would take.
- Runtime class of an object: `obj+0x00` -> `vtable+0x00`. 2 loads.

### 5.3 Delegate lists (P1, P2, L6)

- Walk: element = delegate object; method = `[[d+0x10]+0x08]` (`RuntimeMethod` -> `MonoMethod`); target = `[d+0x08]`; next = `[d+0x1C]` (`prev`, inferred: verify once at load by checking that a freshly combined two-element chain of known EA handlers walks to both). Cap the walk at 16 elements (a longer chain = fail).
- Event 59 (P1): slot `0x11D72C4`; vanilla is one element, `LotManager::PortalLockedCallback`, static (target NULL) [verified: the only `World.OnPortalLockedCallback +=` site is `LotManager.cs`].
- Lot lists (P2): lot object = `IC_Simulator_GetTaskImpl` on the lot id, as for portal objects [inferred: `Lot` is a script object with its own object id; confirm with I-104-6]; field offset of `mOnPortalPathPlanHandler` resolved once from the `Lot` class (`0xE6F720`). Vanilla elements: `Sim::OnPortalPathPlan` (added once per lot at load [verified docs/92 §5]) and `PrivacySituation::OnPortalPathPlan` per live privacy situation (target = the situation, class must pass census, O2). An empty list means native `lot+0x8F` is clear and the search asks nothing on that lot.
- Event 68 (L6, M tier): the native slot holds EA's own `World.OnLotLevelInfoChange` (registered through `RegisterCallbackMethod`); the subscriber list is the static field `World.OnLotLevelInfoChangeCallback`, read through §4's static field path.
- Why not just compare the delegate pointer with the last one seen: `Delegate.Combine` makes a new object per add, but Boehm can reuse a freed address [inferred], so a pointer cache can alias. Walks are short; walk every time.

### 5.4 Method bodies (L2, L3, L4, P3)

What patchers do [verified: docs/04 F8, docs/06 §1, `mods/ts3fixes/src/Fixes.cs`]:

| Patcher | Mechanism | What changes in memory | Our test |
|---|---|---|---|
| MonoPatcher `ReplaceMethod` / `[ReplaceProperty]` (pure managed) | copies 40 bytes of the replacement `MonoMethod` over the original, keeping `+0x18` | original's `+0x08` becomes the replacement's class (a mod class), `+0x0C`, `+0x14`, `+0x1C`, `+0x20` follow | `m+0x08 != expected declaring class` |
| ts3fixes patch host (ours) | same swap, plus a stub that receives the original header | as above, new class = ts3fixes hook class | whitelist L4 |
| MonoPatcher `[TypePatch]` | not read; assumed to be `ReplaceMethod` per member [inferred] | as above | same |
| MonoPatcher `ReplaceIL` (needs `MonoPatcher.asi`) | detour on `GenerateCode 0xE5CA90` swaps the IL pointer and size while transforming | nothing in the `MonoMethod`; the code at `0xE5CA90` | first 8 bytes `55 8D 6C 24 98 81 EC 08` [verified in our copy] |
| MonoPatcher `ForceJIT` (on demand) | writes `90 90` at `0xE5637D` and `EB` at `0xE648DC` | those bytes; callers get re-transformed, so swaps made after the callers compiled take effect | vanilla `75 26` and `74 4C` [verified] |
| Vtable slot edit (hypothetical, no known mod) | writes `MonoClass+0xBC[slot]` | slot entry | census also checks EA classes: an EA class whose guarded slot points to non-EA code is a hit |

L3 per method `m` with expected declaring class `K` (resolved at load by name, so it finds the original struct even after a swap, because the name pointer is kept [verified `0xE70379` compares `MonoMethod+0x18`]):
1. `m+0x08 == K`, else: accept only if the pair (`m`, `[m+0x08]`) is on the whitelist (class name `HoursMemoHook` at `+0x34`, its image not an EA image, and the same image as the other ts3fixes hook classes; the expected class names come from ts3fixes' patch table at build time); else fail.
2. If `m+0x14` is not NULL: `[[m+0x14]+0x08] == m` (a header copied from a compiled method would point elsewhere).
3. Optional: `m+0x04` equals the token from the index (inferred offset; skip until I-104-3 confirms it).

Resolving the 550 methods at load creates `MonoMethod`s for classes not loaded yet (about 40 bytes each, no class constructor runs [inferred: `mono_class_init` and `mono_runtime_class_init` are separate, docs/58]). If that side effect is unwanted, resolve only methods whose class is already in the image's class cache and re-run the sweep on the next world load; a method that does not exist yet cannot have been swapped (a swap needs its `MethodHandle`).

When to re-check: L3 runs in full at load, after ts3fixes `Apply`, after each world load, and whenever P3 sees the `ForceJIT` bytes change. Between those, a rolling slice of 64 methods per submit covers the whole set every 9 submits. A swap made in between without `ForceJIT` reaches only callers transformed after it (docs/04 F8), which the next slices catch [inferred]; the window is the trade for not paying 550 checks per plan.

### 5.5 Mod override census (P4, O1-O5)

- Hook the return of `mono_class_create_runtime_vtable 0xEA1E30` (the slow path of `mono_class_vtable`, once per class per domain, before the first instance [inferred: an object cannot be allocated without its vtable]). ts3native installs its hooks in the ASI's start-up, before the script host initialises [inferred: ASIs load at process start, docs/07 §1.1], so every script class passes through it.
- For class C (EA or not): for each of the 35 guarded slots whose declaring class or interface D is a base of C (§5.2), read the target `MonoMethod` from C's slot table and test its `+0x08` class's image. A non-EA target = **census hit (C, D.slot)**. Store a verdict byte per `MonoClass*` in a small open-addressed table (classes never unload [inferred, docs/29]).
- Per base group keep a hit count: door side (O1: `CommonDoor`, `IBoxStall`, `IHorseJump`, `GameObject.IsInPublicResidentialRoom` when M applies, `IGameObject`), privacy (O2), posture (O3), interaction (O4), service (O5). P4 reads the counts; zero means the per-object row is skipped.
- Expected census per setup [inferred]: vanilla 0; NRaas GoHere 0 (its subclasses override `CreateRoute`, `DoRoute`, `OnApproachingPortalObject`, `OnLaneLocked`, none in the 35; its effect on S is the P2 handler); NRaas Woohooer possibly O2/O4 (its WooHoo privacy and interaction classes); CC doors and gates with scripts O1; simler90 nothing to count (L1 already fails).
- Why not audit dispatches during the PAT build instead: the inline cache (§4) hides hits from any hook on `0xE51FC0`; hooking the hit path means a detour inside the interpreter loop at `0xE54AF4` on every virtual call of the game. The census costs one check per class, ever.

## 6. Cost and caching

Estimates assume warm caches at ~1 ns per load and ~50-100 ns for a cache miss [inferred; to measure with the I-104 rows].

| Check | When | Work | Cost estimate | Cache |
|---|---|---|---|---|
| L1 images | load, world load | 10 class lookups (`FindClass`, loader lock), 10 string compares | ~20-50 µs | EA image array |
| L2 hook bytes | load; `ForceJIT` part per submit | 3 compares | < 20 ns | none |
| L3 method integrity | load, world load, `ForceJIT` change | 550 x (2-3 loads, compare) | ~3-30 µs per sweep; resolution of 550 methods by name once ~1-5 ms | `MonoMethod*` + expected class array |
| L3 rolling slice | per submit | 64 methods | ~0.1-0.5 µs | cursor |
| L5 slot table | load | 35 method lookups | ~50-200 µs once | slot table |
| Census | per class vtable creation | up to 35 subclass tests + slot reads, only for C deriving from a guarded base | ~50-200 ns per class; a few thousand classes per session | verdict byte per class; per-group counts |
| P1 event 59 chain | per submit | 1 element, ~4 loads | < 10 ns | none (aliasing, §5.3) |
| P2 lot lists | per submit, per touched lot | `GetTaskImpl` (host and simulator virtual calls, id hash) + field read + 1-3 elements | ~0.2-1 µs per lot | negative cache per lot id (failed lots stay failed until world reload) |
| O1 portal objects | per submit, only if door-side census > 0 | `GetTaskImpl` + 2 loads + verdict lookup per portal | ~0.2-0.5 µs per portal; 10-60 portals per plan | per class verdict; per lot "has census class" flag cleared on object add/remove events (docs/76 hooks) |
| O3-O5 Sim side | per submit, only if a Sim-side census group > 0 | ts3fixes: 3-5 property reads and `Hashtable` lookups in interpreted C# | ~5-20 µs | `Hashtable` Type -> bool |
| Fallback decision | per submit | OR of the above | - | - |

Common case per submit (empty census, 2-3 touched lots): ~1-3 µs. Worst realistic case (door census hit, 60 portals, Sim-side groups non-empty): ~40-50 µs, still about 0.2% of a 25 ms Isla Paradiso plan (docs/04).

## 7. Fallback and what we tell the player

Fallback = do not build the PAT; the plan runs as today (async with lazy event 59 calls proxied to the sim thread when async is on, else the synchronous plan). The player sees no difference other than the missing speed-up on those plans.

- **ini** (`[ModGuard]` in `ts3native.ini`, new section): `Enabled=1` (0 = no guard, only for tests), `Strict=1` (35 slots and all 10 assemblies; 0 = 8 P slots, ignore `UI`/`Sims3Metadata` MVIDs), `LogEvery=600` (seconds between summaries).
- **One line per reason, first time per session** (ts3native.log, same style as the existing `OK`/`SKIP` lines), naming the cause so a player or helper can map it to a mod. Examples (names and GUIDs are placeholders):
  - `ModGuard: Sims3GameplaySystems is not the EA build (GUID 1234ABCD-...): own route search off for this session (core mod such as simler90's)`
  - `ModGuard: MonoPatcher ReplaceIL hook at 0xE5CA90: own route search off for this session`
  - `ModGuard: Lot.GetHoursOfOperation replaced by NRaas.Something.Hook (image GUID ...): own route search off until the next world load`
  - `ModGuard: World.OnPortalLockedCallback has 2 handlers (extra: NRaas.GoHereSpace.Helpers.X::OnPortalLocked): portal checks stay with the game`
  - `ModGuard: lot 0x1234... has a portal handler from NRaas.GoHere (GoHereSpace.Y::OnPortalPathPlan): plans through this lot use the game's search`
  - `ModGuard: class Mod.Doors.SaloonDoor (image GUID ...) overrides CommonDoor.IsAllowedThrough: plans through lots with this door use the game's search`
  Class and method names come from `+0x34`/`+0x38` and `+0x18`; the image is named by its GUID (the assembly name is not read today; add it if a later read finds the `MonoImage` name field).
- **Summary every `LogEvery` seconds while any fallback happened:** `ModGuard: 37 of 412 plans used the game's search (lot handlers 30, census door 7, session 0)`.
- **Counters** in the `/native` dump (reserve in docs/registry.md before adding): plans guarded, plans passed, fallbacks per reason (session, method, chain, lot, census door, census Sim side), census classes per group, L3 sweeps and failures.
- No in-game notification: the fallback is correct behaviour, not an error. ts3fixes may add one line to its own applied log (`AppDomain` data `ts3fixes.applied`) so the agent harness can read it.

## 8. Builder order

1. Offline: extend `tools/managed-callgraph.ts` with a `guard` subcommand that writes the S method list (550, with declaring class, name, parameter count, token), the 35 guarded targets with their declaring types, and the MVID table, into a header ts3native compiles in. Regenerate when the EA DLLs change (never on Steam 1.67).
2. ts3native: L1, L2, L5, L3 at script host init and world load; census hook on `0xEA1E30`; P1-P5 at async START, refusing START with a new reason when a check fails (the existing `asyncRefused` path).
3. ts3fixes: the Sim-side check (O3-O5) behind a census bit, and the hoursmemo hook class name exported for L4.
4. Probes before the PAT relies on it: the docs/51 §38 rows (vanilla run with zero fallbacks; GoHere save with lot-level fallbacks only; MonoPatcher install with session fallback; a test mod with a door subclass).

## 9. Limits and open points

- `MulticastDelegate.prev` at `+0x1C` and `MonoMethod` token at `+0x04` are inferred; the rest of §4 was read in the code.
- `IC_Simulator_GetTaskImpl` for lot objects is inferred (§5.3); if lots are not findable by id, read the handler lists from the managed side (ts3fixes) instead.
- The census misses classes whose vtables were created before ts3native's hooks (none expected, since the ASI loads before the script host [inferred]); a full sweep over the image class caches at world load would close that gap.
- Mod code that changes EA data (tuning, statics) is not a code change: b-submit sees it, b-cached does not (its change hooks watch EA's writers). b-cached must invalidate on any write it cannot see, or stay off when non-EA images are loaded at all.
- Reflection writes to EA private fields that the event 59 chain reads are data changes too; same rule as above.
- Native mods that patch the route search itself (S3SS, S3PP, others) are outside this guard; docs/68 and `tools/hook-sites.ts --check` cover them.
- The 35-slot list comes from CHA without receiver typing (docs/103 §8); it can over-list, which only makes the census stricter.
- MonoPatcher's `[TypePatch]` mechanism was not read.

## 10. Ledger

Rows I-104-1 to I-104-10 in docs/51 §38. New symbols (resolver, inline cache, delegate constructor, class and image fields, World event slots 59 and 68) are appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv`.
