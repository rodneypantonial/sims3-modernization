# 132. Offload override scan: installed script mods vs the `[AutShadow]` allow-list

Static, 2026-10-20. No game run, no build into the main checkout. Tool: `tools/offload-override-scan.ts`. Also records the `AutShadow.AllL3Ok` exact-signature fix (docs/124 §13.5 finding).

## Summary

- Scanned **439 packages** read-only (Steam install 267, `Mods` 171, `DCBackup` 1, `DCCache` 0 `.dbc`), all readable. **17 packages** carry **91 S3SA assemblies**: **10 on the known-MVID list** (the game's own, all in the Steam install) and **81 not** (all in `Mods`). Every assembly parsed.
- **3 replaced game modules:** simler90's core mod ships its own `Sims3GameplaySystems` and `Sims3GameplayObjects` (MVID guard: `[AutShadow]` **off for the session** on this install while it is in `Mods`); NRaas ErrorTrap ships its own `ScriptCore` (not MVID-checked by `[AutShadow]`, L3 per method only).
- **54 matching types** in 6 packages: **21 fall back safely** (skip: 2 subclasses of an allow-listed definition, 5 subclasses of `RabbitHole`, 14 new meta definitions) and **35 can put mod code on the worker** (they override a virtual or implement an interface method the allow-listed closures call); 2 store rabbit holes are in both groups. All 35 are in the four Sims 3 Store repack packages (21 + 12 in the two large ones).
- **864 definition overrides covered** by the exact-type check (mod definitions' own `Test`, `GetInteractionName`, age-test overrides and the like): the worker scores only allow-listed definition types, so they never run there.
- Verdict [inferred, static]: on this machine the pilot stays off (simler90) until the profile moves the core mod out; without it, only store objects with `InUse`, posture or meta-venue overrides can reach the worker, as an upper bound.

## 1. Method

1. Packages: every `*.package` under the Steam `The Sims 3` folder, under `Documents\Electronic Arts\The Sims 3\Mods`, `DCBackup`, and `*.dbc`/`*.package` under `DCCache` (store content installed by the launcher). Only the header, the index and the S3SA entries are read (some install packages are GBs). Saves and caches are not scanned.
2. Assemblies: S3SA bodies through `tools/extract-assemblies.ts` (RefPack, S3SA decrypt, PE, module name). MVID from the Module row. Known list = the 10 assemblies of the call-graph index (`game-copy/assemblies`, docs/78).
3. An unknown assembly named like a game module is a replacement: `Sims3GameplaySystems`/`Sims3GameplayObjects` = session off (`AutShadow.MvidCheck`), any other = unchecked. The rest load into one `managed-callgraph.ts` type model together with the game assemblies (`typeModel`, game types win name resolution).
4. Allow-list: docs/122 v0 (`docs/data/autonomy-allow.csv`, verdict `in`, 12 definitions) plus docs/126 tier 2 (`docs/data/autonomy-tier2.csv`, `ready`/`new-rows` rows that name a type): **233 definitions**. Implementors: the 7 of `AutShadow.kImpl`. Meta interfaces: `IMetaObject`, `IMetaInteractionDefinition`, `IOverridesCalculateScore`.
5. Closure: `defRoots` of the 233 definitions, depth 12 over the index, not following delegate creation and cut at the same worker hooks as `tools/autonomy-tier2.ts` (docs/121 §1, docs/122 §12.5, docs/126 §3 H7-H10): **2,665 methods**, **148 virtual or interface targets** declared in game script assemblies.
6. Per mod type: base chain and interface closure against the sets above; each non-`newslot` virtual matched to the first base method with the same signature; each interface method counted only where mod code supplies the body.

## 2. Effect classes

| Kind | Rule | Effect on the worker | Types here |
|---|---|---|---|
| `core` | replaced `Sims3GameplaySystems` / `Sims3GameplayObjects` | off for the session (MVID guard) | 1 package (2 modules) |
| `unchecked` | replaced other game module (`ScriptCore`, `SimIFace`, `UI`, ...) | not MVID-checked by `[AutShadow]`; per-method L3 still applies; risk | 1 package (`ScriptCore`) |
| `def` | derives an allow-listed definition | fallback: skip that ad (exact runtime type), game B14 on the sim thread | 2 |
| `impl` | derives one of the 7 implementors, or implements `IMetaObject` | fallback: skip that object (exact type; `RabbitHole` subclasses only from the two EA assemblies) | 5 (4 store rabbit holes + our `Ts3Fixes` hook class, never instantiated) |
| `meta` | new `IMetaInteractionDefinition` / `IOverridesCalculateScore` type | fallback: skip that ad (not allow-listed) | 14 |
| `vcall` | overrides a closure virtual or implements a closure interface method | the worker runs mod code when it meets the object: EA ICs without a shim abort (`ic`), managed effects are unguarded (Mode R A2/A3, go-live risk) | 35 |

`vcall` by declared target (types; one type can hit several): `GameObject.get_InUse` 23 and `get_Cost` 1, `IGameObject.get_InUse` 23, `Posture` virtuals 7 (`Satisfaction`, `PopulateInteractions`, `get_Container`, `get_Name`, `OnExitPosture`, `get_Implicit`), `IMetaObject.GetMetaAutonomyVenueType` 2, `InteractionInstance.GetTargetLot` 1, `IThrowAwayable` 1, `ICanCountAsBeingIndoors` 1.

## 3. What it means

- **simler90 decides the pilot on this install.** `MvidCheck` turns `[AutShadow]` off for every load while the core mod is in `Mods` (by design, docs/122 §4.1 step 4, docs/104 L1). Shadow and go-live runs need a profile without it, or an allow-list built from simler90's assemblies (a second MVID pair plus a re-run of docs/121/126 closures over its DLLs: ~3-5 h static, not planned).
- **ErrorTrap's `ScriptCore`** is outside `[AutShadow]`'s MVID pair. docs/104's routing guard checks all 10 modules; the pilot checks two. Adding `ScriptCore` and `SimIFace` to `MvidCheck` would turn the pilot off with ErrorTrap installed, so the cheaper fix is a per-method L3 on the few `ScriptCore` methods the closure calls (open item, ~30 min static).
- **The 35 `vcall` types are an upper bound.** The scan matches declared targets, not receiver types: a store object overriding `InUse` reaches the worker only if a tier-2 local definition's target is that object. For go-live, the consumer's exact-type check (docs/124 §2.4) should extend to the target object of each scored pair (game type or allow-listed implementor, else the pair runs live); cost one type compare per pair.
- **Fallbacks need nothing.** `def`, `impl` and `meta` rows already skip by exact type in the pilot (`AutShadow.ImplVerdict`, `sDefIndex`).

## 4. `AllL3Ok` by exact signature (docs/124 §13.5)

`AutShadow.AllL3Ok` matched by name, so every overload with a listed name fell under the same L3 check, including ones the closure never calls; in go-live this would put both `Autonomy.CalculateScore` overloads under our own record hook's whitelist. Now each L3 callee is resolved by declaring type (as `GetMethods` sees it), name and exact parameter types through `AutShadowCore.FindExact`; a listed signature that is not found counts as a guard hit (logged), so a wrong list cannot pass silently. The lists: shared `InteractionObjectPair` getters and `TestAndCalculateScore(Autonomy, AutonomySearchType, InteractionFlags)`, `Tradeoff.SatisfiesCommodity(CommodityKind)`, both `CalculateScoreForObjectInteraction` overloads (the one-argument one calls the other); per definition `Test(Sim, <target>, bool, ref GreyedOutTooltipCallback)` with the target per `kDefNames` entry, the `Test(ref InteractionInstanceParameters, ref GreyedOutTooltipCallback)` wrapper, `MetaAdSatisfiesCommodity(Lot, CommodityKind)`, `CalculateScore(InteractionObjectPair, Autonomy)`; per implementor `HowMuchItWantsSimToCome(Sim, float)` and `get_MetaAds()`, plus `Lot`'s private `HowMuchItWantsSimToCome(MetaAutonomyVenueType, Sim, float)`. Signatures checked against the call-graph index. Selftest (`node tools/selftest/autshadow/run.ts`) checks that a same-name overload is never returned. ts3fixes builds in a worktree; not run in the game.

## 5. Open items

- `ifaceN` call edges (interface calls pre-resolved to 1-4 game classes) and BCL-declared virtuals (`ToString`, `Equals`, `GetHashCode`) are not counted (tool `shortcut:` comments).
- The definition rule assumes no closure method calls another definition's virtuals; a Mode R mismatch traced to one reopens it.
- Re-run after any mod change: `node tools/offload-override-scan.ts --summary`.

## Reproduce

```
node tools/offload-override-scan.ts --self-check
node tools/offload-override-scan.ts [--index <main checkout>/game-copy/analysis/callgraph/index.json] [--csv <scratch>/scan.csv] [--summary]
```

Per-package tables go to stdout (type, what it overrides, effect); keep them out of the repo (they list installed mods).
