# 215. Per-method mod guard: which replaced game methods the worker closures reach

Written 2026-10-30. Static only: no build, no game run, no queue. Contributor research task (Rodney, round 2, task 1).

**Question:** `[AutShadow]` turns itself off for the whole session when the `Sims3GameplaySystems` / `Sims3GameplayObjects` MVID pair is not on its accept list (docs/122 §4.1 step 4, G-M1 in docs/128 §5). A per-method guard (docs/122 §12.4 item G) would turn off only what a mod really changes. For each mod that ships its own copy of a game script assembly: which methods differ from the game's, and which of those can the worker reach?

**Output:** `docs/data/mod-method-diff.csv` (`mod, assembly, type, method, il_hash_ea, il_hash_mod, differs, reachable_from_worker, notes`, 891 rows). Tool: `tools/mod-method-diff.ts` (`--self-check`).

Tags: **[verified]** = from the tool output or the assembly bytes; **[inferred]** = reasoning, not run.

## Summary

- **3 mods on the reference install ship game script assemblies:** simler90's core mod (SGS, SGO), NRaas ErrorTrap (`ScriptCore`), and the tiny-ui-fix 1.5.3 patcher output (`UI`, `Sims3StoreObjects`, and SGS / SGO patched over the core mod) [verified].
- **simler90 UPDATE242 changes 467 methods and adds 1; 77 of them are in a worker closure** [verified]. Only 3 are in the v0 pilot union (`Autonomy.CheckAvailabilityOnLot`, `MetaAutonomyTuning.GetIntensity`, `Household.IsGreetedOnLot`) and 6 in a direct tier-2 closure. The other 68 are reached only through overrides (`-typed`, `-cha`).
- **ErrorTrap: 15 changed and 44 added methods, 4 reachable**, all only through overrides (`Simulator.DestroyObject` by the class-hierarchy walk; `Simulator.AddObject`, `CreateObject`, `ScriptProxy.SetLogic` by the typed run) [verified]. None is in a v0 or direct tier-2 closure, which matches docs/128 §2.2.
- **tiny-ui-fix's own changes reach no SGS / SGO closure method** (70 + 27 changed, 0 reachable). 7 of its `UI` / `Sims3StoreObjects` changes are on the typed tier-2 closure, through UI calls [verified].
- **No tier-2 verdict changes with any of the three** [verified]. `autonomy-tier2.ts` and `autonomy-virtual-closure.ts` re-run on each mod's assemblies give the EA verdict for 251 of 251 and 270 of 270 rows.
- **The MVID is not a content identity** [verified]. tiny-ui-fix's `UI.dll` and `Sims3StoreObjects.dll` carry the game's exact MVIDs with different bytes and 195 / 14 changed methods. Its SGS / SGO keep the MVIDs of the core they were patched over. Its re-patched NRaas DLLs keep their original MVIDs. So an MVID list accepts a patched copy of an accepted module without notice.
- **Recommendation (§5):** key the accept list by a hash of the module bytes, not by MVID. For a module that is not on the list, check the token-normalized IL hash of only the closure methods of each enabled definition, and turn off only the definitions whose closure has a differing method. Keep the session switch for a module that cannot be hashed.

## 1. Method

1. **Inputs.** Every `.package` under `<Documents>\Electronic Arts\The Sims 3\Mods` and the Steam install, read-only, plus `--pkg` files. All S3SA assemblies are pulled with `extract-assemblies.ts`. A module counts as a game module copy when its module name is a game script assembly (`ScriptCore`, `SimIFace`, `Sims3GameplaySystems`, `Sims3GameplayObjects`, `Sims3Metadata`, `Sims3StoreObjects`, `UI`, `mscorlib`, `System`, `System.Xml`) and its bytes differ from the game's. The comparison is by bytes, not by MVID: a copy with the game's MVID is compared too (§4).
2. **Method identity.** Methods are matched by full id (`Type::Name(params)`, from each module's own metadata). Compiler-generated names (`<PrivateImplementationDetails>{GUID}` switch tables, `CachedAnonymousMethodDelegate#`, `<>c__DisplayClass#`, `CS$<>8__locals#`, lambda and iterator bodies) are folded, so a plain recompile does not count as a change. A method whose id carries such a name is matched to its renamed twin by the folded id.
3. **Method hash.** `il_hash_*` is the first 16 hex digits of the SHA-256 of `ilShape()` (`managed-callgraph.ts`). It contains the opcodes with short and long forms folded, branch targets as instruction indices, tokens resolved to member names, locals and exception clauses. Max stack and init-locals are left out. Two copies with the same source and different token numbering hash the same.
4. **Notes.** For a changed method, the note lists calls and stores the mod adds (`+call`, `+stfld`, `+new`) or removes (`-`). It says "calls and stores unchanged: branches, constants or locals" when only the logic moved, with the IL line counts.
5. **`reachable_from_worker`**, most direct label first:

| Label | Reached by |
|---|---|
| `v0` | the docs/122 pilot union: v0 definitions, the 7 `HowMuchItWantsSimToCome` implementors, the meta base path; no overrides |
| `v0-typed` | the same roots, with the receiver-typed override run of docs/127 (`autonomy-virtual-closure.ts --edges`, 5 hops) |
| `v0-cha` | the same roots, overrides by class hierarchy only, 5 hops (over-approximates) |
| `tier2` | a docs/126 ready or new-rows candidate, direct closure |
| `tier2-typed` | any docs/126 candidate's typed override run (ready or blocked: the edges file does not keep the verdict) |
| `tier2-cha` | a ready or new-rows candidate, class-hierarchy overrides, 5 hops |
| `tier2-blocked`, `tier2-blocked-cha` | only through blocked candidates |
| `no` | none of the above |

The reach set holds 29,391 method ids: v0 967, v0-typed 285, v0-cha 2,885, tier2 1,461, tier2-typed 22,769, tier2-cha 824, tier2-blocked-cha 200 [verified].

## 2. Result per module

| Mod | Module | MVID | Methods | Changed | Added | Changed and reachable |
|---|---|---|---|---|---|---|
| simler90 core UPDATE242-Steam | Sims3GameplaySystems | `bb8cb38b…` | 75,877 | 398 | 0 | 69 (v0 3, v0-cha 1, tier2 6, tier2-cha 2, tier2-typed 57) |
| simler90 core UPDATE242-Steam | Sims3GameplayObjects | `a95a143f…` | 55,581 | 69 | 1 | 8 (v0-cha 2, tier2-typed 6) |
| NRaas ErrorTrap v100 | ScriptCore | `99b5bd68…` | 4,669 | 15 | 44 | 4 (v0-cha 1, tier2-typed 3) |
| tiny-ui-fix 1.5.3 | UI | `618382f8…` (the game's) | 14,957 | 195 | 31 | 6 (tier2-typed) |
| tiny-ui-fix 1.5.3 | Sims3StoreObjects | `bd729220…` (the game's) | 6,741 | 14 | 9 | 1 (tier2-typed) |
| tiny-ui-fix 1.5.3, against the core it patched | Sims3GameplaySystems | `8fc5bf3a…` (the input core's) | - | 70 | 9 | 0 |
| tiny-ui-fix 1.5.3, against the core it patched | Sims3GameplayObjects | `5a2a1248…` (the input core's) | - | 27 | 9 | 0 |

No mod removes a game method. The tiny-ui-fix SGS / SGO rows are its patch alone: they compare its output with the core it was run over (a local rebuild of the simler90 core with extra fixes, `--against`). That input is not public, so those 115 rows are not reproducible as they stand. Running the patcher over the shipped core and then `--against` the shipped core gives the same kind of row.

**simler90, the 9 direct-closure changes [verified]:**

| Label | Method | Change |
|---|---|---|
| v0 | `Autonomy.CheckAvailabilityOnLot` | logic only (IL lines 184 -> 190) |
| v0 | `MetaAutonomyTuning.GetIntensity` | logic only (194 -> 197) |
| v0 | `Household.IsGreetedOnLot` | +`get_IsPetHousehold` |
| tier2 | `Autonomy.CalculateScoreForSocial` | +3 `simler90.Other` checks, +`Sim.TestCurrentOutfitRevealing` |
| tier2 | `SocialInteractionA+Definition.CanSocializeWith` | +Bonehilda and resort front-desk checks |
| tier2 | `GoOutToYard+Definition.Test` | +household / `RoommateManager` checks |
| tier2 | `TraitFunctions+SwipeSomething+Definition.Test` | +`Other.KleptoSwipeLotCheck` |
| tier2 | `Sim+GoToVirtualHomeMeta+Definition.CalculateScore` | +`get_IsGhost` |
| tier2 | `ImprovedProtestSituation.CanProtest` | +`Other.CanProtestPoliceFix` |

**Changes on a reachable method that add a write [verified, from the notes]:** `Sim.GetCategoryAndIndexToUse` (+`set_CareerOutfitIndex`, v0-cha only; the typed run does not reach it), `Sim.SwitchToOutfitWithoutSpin` (+`stfld OutfitInfo.RevealingScore`, tier2-typed), `Occupation.ApplyOccupationUniform` (+`set_CareerOutfitIndex`), `Genetics.MakeSim` (+`set_VoicePitchModifier`, tier2-cha), `Sim.GrantUFOsToAliens` (+`CarOwnable.SetOwnerLot`), ErrorTrap `ScriptProxy.SetLogic` (+2 `stfld`). Every one of them is reached only through overrides, and the re-run classifiers (§3) keep their verdicts. A write behind an override is still a write on the worker if that override runs, which is what the per-method guard has to catch.

## 3. Verdicts with each mod's assemblies [verified]

Each mod's assemblies were put over the game's in a scratch folder, indexed with `managed-callgraph.ts build`, and both classifiers were re-run on that index:

| Assemblies | `autonomy-tier2.ts` (251 rows) | `autonomy-virtual-closure.ts` (270 rows, plain / typed / CHA) | Rows with mod overrides |
|---|---|---|---|
| EA | baseline | baseline | 0 |
| simler90 core UPDATE242-Steam | 0 change | 0 change | 0 |
| NRaas ErrorTrap v100 | 0 change | 0 change | 0 |
| tiny-ui-fix 1.5.3 (`UI`, `Sims3StoreObjects`, its own DLLs) | 0 change | 0 change | 0 |

So none of the three adds a blocker that the classifiers see: no new native IC and no new managed write on the closures they build. This agrees with docs/128 §2.1 for simler90. The per-method diff is still needed, for two reasons. First, the writes in §2 sit behind overrides, which `autonomy-tier2.ts`'s direct closure does not walk. Second, a changed branch in a closure method changes what the worker does without changing which methods the closure contains.

## 4. The MVID does not identify the content [verified]

| Module | Where | MVID | SHA-256 (first 12) | Changed methods vs the reference |
|---|---|---|---|---|
| UI.dll | game | `618382f8afc8ef4abec3ccf3956ee8cb` | `212c119ecb7e` | - |
| UI.dll | tiny-ui-fix 1.5.3 | `618382f8afc8ef4abec3ccf3956ee8cb` | `38baa3730309` | 195 changed, 31 added |
| Sims3StoreObjects.dll | game | `bd729220cca85a459609352de1e7ccd4` | `8ff310620877` | - |
| Sims3StoreObjects.dll | tiny-ui-fix 1.5.3 | `bd729220cca85a459609352de1e7ccd4` | `0908d829c293` | 14 changed, 9 added |
| NRaasErrorTrap.dll | NRaas_ErrorTrap.package | `9649edd603f98a43b7d3215479126cae` | `7d0477871be1` | - |
| NRaasErrorTrap.dll | tiny-ui-fix 1.5.3 | `9649edd603f98a43b7d3215479126cae` | `e3da38bc514c` | (not a game module; not diffed) |
| NRaasMasterController.dll | NRaas_MasterController.package | `b2f3807e42e4144a9f0abcbc7c54e7d8` | `0eb4a1245914` | - |
| NRaasMasterController.dll | tiny-ui-fix 1.5.3 | `b2f3807e42e4144a9f0abcbc7c54e7d8` | `4f353f1ab483` | (not a game module; not diffed) |

tiny-ui-fix rewrites assemblies in place and keeps each input's MVID. Its SGS / SGO on this install carry the MVIDs of the core it was run over. Run over the shipped simler90 core, they would carry `bb8cb38b…` / `a95a143f…`, which is accepted set 1 in `AutShadow.kMvidSets` [inferred: same tool behavior as the 4 modules above]. `[AutShadow]` would then accept 70 + 27 changed methods without logging anything. None of them reaches a closure today (§2), but the next patcher version can change that.

The same gap applies to `kScriptCoreMvids` (an ErrorTrap `ScriptCore` patched by a rewriter would keep `99b5bd68…`) and to the known-MVID skip in `offload-override-scan.ts` (docs/132).

## 5. Guard recommendation

1. **Accept list by content, not MVID.** Each accepted module gets an entry of module name and SHA-256 of the assembly bytes, as the S3SA resource holds them. Today that is 2 SGS / SGO sets and 2 `ScriptCore` builds. A module with the same MVID and different bytes counts as unknown. The bytes are in the S3SA resource at load. The in-game code still has to read them, either from the resource or from the loaded image's raw data (Mono 1.x `MonoImage.raw_data` / `raw_data_len`; offsets not checked in this exe [inferred]). Cost: one hash per game-module copy, once per session, a few ms for 13 MB [inferred].
2. **Unknown module: per definition, not per session.** Ship a table of `(method id, il_hash)` for the closure methods of each enabled definition, from the EA build and each accepted set. Use the CSV's hash definition. For a module not on the list, hash only those methods of that module (v0 today: 967 + 285 typed, 2,885 more with the hierarchy walk) and compare. Turn off only the definitions whose closure has a missing or differing method, and log their names. The hash needs tokens resolved to member names (§1.3), so it is native work on the loaded image's metadata, or a managed walk if the game's `mscorlib` exposes method bodies (not checked) [inferred]. Use the typed closure for the check. The class-hierarchy closure is the safe fallback, at about twice the methods.
3. **Session switch only when the guard cannot run:** an unreadable image, a hash failure, or a module from a loader the guard does not see (MonoPatcher's run-time swaps are already handled by the L3 check, docs/128 §5).
4. **Keep L3 (G-M4)** for run-time method swaps. A body hash cannot see a swap that leaves the IL alone and redirects the method pointer.

What this would change on the reference install [inferred, from §2 and docs/128 §2.1]: with simler90 accepted by content, nothing changes. With an unknown simler90 build, the session would stay on, and the definitions whose closure contains `CheckAvailabilityOnLot` or `IsGreetedOnLot` would go off. That is most of v0, so the gain is a clean log rather than more definitions. The gain is real for mods that change a few methods: ErrorTrap (0 direct-closure methods) and tiny-ui-fix (0 SGS / SGO closure methods) would keep every definition on.

## 6. Limits

- **Static.** A changed branch can route the worker into a method that the EA closure does not contain. The per-definition check covers it only if the closure is rebuilt on the mod's index. §3 does that for the three mods here: 0 verdict changes.
- **`tier2-typed` does not split ready from blocked candidates**, because the `--edges` file keeps the candidate group, not its verdict. Read it as "some tier-2 candidate".
- **Mods that add types** (subclasses of closure classes, docs/128 §2.4) are not in this table; that is G-M5's census.
- **Only the reference install.** Other script mods that ship game-module copies would need a re-run. One command per package (§7).
- **The tiny-ui-fix SGS / SGO delta** is computed against a non-public input core (§2).

## 7. Reproduce

```
node tools/managed-callgraph.ts build --asm game-copy/assemblies --out <scratch>/cg_ea
node tools/autonomy-virtual-closure.ts --asm game-copy/assemblies --index <scratch>/cg_ea/index.json --edges <scratch>/typed_ea.json > /dev/null
node tools/mod-method-diff.ts --asm game-copy/assemblies --index <scratch>/cg_ea/index.json --typed <scratch>/typed_ea.json \
  --pkg "<simler90GameplayCoreMod-UPDATE242-Steam.package>=simler90 core UPDATE242-Steam" \
  --pkg "<NRaas_ErrorTrap.package>=NRaas ErrorTrap v100" --csv <scratch>/a.csv
node tools/mod-method-diff.ts ... --pkg "<tiny-ui-fix.package>=tiny-ui-fix 1.5.3 (own changes)" --against <core it was patched over> --csv <scratch>/b.csv
node tools/mod-method-diff.ts --self-check --asm game-copy/assemblies
```

Pass `--game` / `--user` to scan a whole install instead of single packages (about 60 s). For §3, copy a mod's assemblies over `game-copy/assemblies` in a scratch folder, index it, and run `autonomy-tier2.ts` and `autonomy-virtual-closure.ts` with `--asm <folder> --index <folder index>`.

`managed-callgraph.ts` change in this branch: `indexAsm()` is split out of `loadAll`, the self-check output is byte-identical, and `loadStandalone()`, `ilShape()` and the `#US` heap offset are added for single-module use.
