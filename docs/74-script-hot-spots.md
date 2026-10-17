# 74. Script hot spots: Stereo, motive panel, tooltips and other cheap wins (PLAN gap 8)

PLAN.md "Gaps" item 8 named three managed hot spots from the older pooled samples: `Stereo.AddEnjoyingMusicCallback` 1.8%, `SimInfo.OnMotivesChanged` 1.1%, `TooltipManager` 0.5% of sim-thread samples. This doc re-measures them in the trusted 2026-10-16 samples, explains why each is hot from the extracted script DLLs, designs the smallest in-place fix that keeps game results identical, and adds the other cheap wins of the same kind from the top of the managed list. No game run was made. The four new fixes were built afterwards (2026-10-16, §10), opt-in and not yet run in game.

Tags: [verified] = read in the decompiled script DLLs (`game-copy/decompiled/`, from `tools/extract-assemblies.ts`; not copied here); [measured] = managed samples from the runs below; [inferred] = reasoning from both, not checked live.

## 1. Answer

| Hot spot | Share now, installed default (S3PP caches on) | Why it is hot | Fix | Expected saving |
|---|---|---|---|---|
| `Stereo.AddEnjoyingMusicCallback` | **0.27%** (0.11% / 0.51%); 1.92% only with S3PP `MonoTypeEHashProbe` + `MonoIsinstCache` off | every music pulse removes and re-adds the dance interaction; the remove scans the Sim's whole interaction list calling `GetType()` per entry, and `GetType()` is the Mono type-hash lookup S3PP caches | **none of ours**: S3PP's caches already remove ~90% [measured]; keep them on (docs/69) | 0 (already taken by S3PP) |
| `SimInfo.OnMotivesChanged` | **1.16%** (1.24% / 1.02%) | every motive update of every household Sim rebuilds that Sim's HUD info; for the selected Sim the motive panel then redraws all six bars, loading four UI images per bar, and the name getters run the localization lookup | **existing opt-in fixes**: `kMotiveBarImageCache` (docs/49) and `kSimNameCache` (docs/54); run their A/B and turn them on | ~0.6-0.7% = **6-7 ms/s** [inferred] |
| `TooltipManager.Simulate` | **0.61%** (0.90% / 0.17%) | a task that, every sim tick, asks the UI for the window under the cursor and walks its parent chain reading each window's tooltip text through a string IC | **new**: poll at most every 100 ms while the cursor stands still | ~0.4-0.5% = **4-5 ms/s** with the mouse idle [inferred] |
| `Lot.UpdateDetailPriority` (new) | **1.77%** (2.26% / 1.02%; 2.82% cache off) | every lot, every tick, builds the active household's actor list (two new lists) to see whether a player Sim stands on it | **new**: one active-household lot set per sim tick | ~1.5% = **~15 ms/s** [inferred] |
| `Cane.SimRequiresCaneWalk` (new) | **0.75%** | per route segment it searches the Sim's inventory for an active cane before looking at the walk style, though the answer is false for every style but five | **new**: test the walk style first (exactly the same answer) | ~0.4-0.7% = **4-7 ms/s** [inferred] |
| `Lot.GetHoursOfOperation` (new) | **1.09%** (0.79% / 1.54%) | `IsOpenVenue` (camera, autonomy, route plans, doors) re-reads two tuning dictionaries and scans all situations for a bachelor party on each call | **new**: memo per lot per sim tick | ~0.5-0.8% = **5-8 ms/s** [inferred] |
| `TombRoomManager.AboutToPlanRouteCallback` | **0.95%** | doors and stairs of the lot fetched again on every route plan | **existing opt-in** `kSpatialCache` (docs/20 §4a) | ~0.5-0.8% [inferred] |
| Moodlet grid rebuilds (`SimInfo.OnBuffsChanged`) | **0.75%** | every buff add/remove clears and reloads the moodlet grid | **existing opt-in** `kBuffGridInPlace` (docs/49) | ~0.5% [inferred] |

New fixes together: ~**28-35 ms/s** of sim-thread time (2.8-3.5%); turning on the four existing opt-in fixes adds ~**17-23 ms/s** [inferred, 1% of samples ~ 10 ms/s as in docs/data/results/2026-10-16-queue-s3pp-cache.md]. All of it is BP only: the trusted runs are BP; IP has no managed samples from 2026-10-16.

## 2. Data and method

- **Runs (trusted, sampler fixed 2026-10-16):** `s3pp-cache-2026-10-16-bp-s3pp-cache-on` and `-off` (PASS run 01:43 UTC, build 2b45b85, 885 managed samples each) and `mainstack-2026-10-16-bp-mainstack` (586 samples). Files: `probe-out/profile/<run>-managed.jsonl` (one C# stack per line, innermost first; `tools/probe/managed-sample.mjs`). The verify runs of 2026-10-16 have no managed samples (repro mode samples the main thread only), and the lotstream runs ran no managed sampler.
- **Appended files:** `managed-sample.mjs` appends, so both s3pp-cache jsonl files also hold the FAIL run of 01:14 UTC (1,762 and 1,770 lines). Only the last N lines (N = `samples` in the matching `-managed.txt`) are used. The FAIL-run halves give the same picture (OnMotivesChanged 1.47-1.60%, `UpdateDetailPriority` 2.03-2.28%, Stereo 0.23% on / 1.92% off) [measured], but are not counted.
- **Default config** = cache-on run + mainstack run (both have S3PP's Mono caches on, the installed default): 1,471 samples, 411 with no script running. The cache-off run is shown for reference only.
- **Shares** are the samples whose stack contains the frame (inclusive), over all samples. With 5-30 hits per item, the error is about ±20-45% of the share (Poisson). ms/s uses 1% of samples ~ 10 ms per second of sim-thread time.
- **Analysis:** a throwaway node script over the jsonl (shares, caller and callee splits); nothing committed. The older 1.8% / 1.1% / 0.5% come from the pooled pre-2026-10-16 samples (docs/35, docs/43); whether those runs had the S3PP caches on was not checked.

## 3. `Stereo.AddEnjoyingMusicCallback`

**Measured.** 1 of 885 samples (0.11%) with the caches on, 3 of 586 (0.51%) in mainstack, 17 of 885 (**1.92%**) with them off. With the caches off, 15 of the 17 sit in `GameObject.RemoveInteractionByType` and 13 have `System.Object.GetType` as the leaf; all 17 come from `ReactionBroadcaster` through `BroadcastCallback.Invoke` (our `BroadcasterPulseHook` above it) [measured].

**Why [verified].** The stereo (and each wired or wall speaker) owns a `ReactionBroadcaster` whose enter callback is `AddEnjoyingMusicCallback`. The broadcaster pulses about once per sim minute and calls the enter callback for each Sim in range the first time and again every `PulseRepeatTime` sim minutes (default 20, `ReactToMultiplePulses` default true). Each call, for a non-pet Sim on the stereo's lot: removes and re-adds the `EnjoyingMusic` buff (both fire `BuffsChanged`, so a household Sim also gets a HUD and moodlet-grid refresh, §4), then `RemoveInteractionByType(typeof(DanceTogetherA.Definition))`, then `AddInteraction(new DanceTogetherA.Definition(...), checkForDuplicate: true)`. The remove walks the Sim's whole interaction list and compares each entry's `InteractionDefinition.GetType()` with the type; the add walks it again with `InteractionObjectPair.Equals`. A Sim's list is long, and in the embedded Mono 1.2.3 `GetType()` goes through the type-hash lookup `0xEBBDE0` / `mono_metadata_type_equal 0xEA8720` (docs/29 §5), which is what S3PP's `MonoTypeEHashProbe` caches.

**Fix: none of ours.** The S3PP caches already take the cost from 1.92% to 0.11-0.51% [measured]. The remaining work is the game's intended semantics: the buff remove/add resets the buff's timeout, and the interaction remove/add moves the entry to the end of the list (pie-menu and autonomy enumeration order). Skipping either would change game results, so no dedupe is safe. If S3PP were ever dropped, the same lookup would have to come back as our own type cache (docs/69 #1/#3); that is not planned.

## 4. `SimInfo.OnMotivesChanged` (motive panel)

**Measured.** 11 of 885 (1.24%) and 6 of 586 (1.02%) samples carry `SimInfo.OnMotivesChanged`; `HudModel.UpdateSimInfo` from any trigger is 1.81% and 2.22% [measured]. Split of the motive path in the default config (17 samples): motive bar image loads 5, Sim name getters 4, other motive bar work (size, colours, tag, arrows) 4, the rest of `UpdateSimInfo` 4 (motive colours, current moodlet, celebrity level). The buff path (`SimInfo.OnBuffsChanged`, 11 samples, 0.75%) is mostly the moodlet grid rebuild [measured]. In these runs `MotiveBarHook.Update` and `NameCacheHook.get_*` are on the stack but call straight through to the originals: both switches were off (tuning default) [measured].

**Why [verified].** `HudModel` subscribes each household Sim's `Motives.MotiveChanged` to that Sim's `SimInfo.OnMotivesChanged`. `Motives.UpdateMotives` fires the event at the end of every motive update, so every household Sim's update rebuilds its `SimInfo`: name (through the localized name getters, docs/54), sex, age, occult info, celebrity progress, mood colours, the enemies mood penalty, and per motive the UI value, colours and current moodlet. Only when a motive UI value changed and the Sim is the selected one does `SimMotivesChanged` fire; the motive panel then calls `MotiveBar.Update` for all six bars, and each bar loads its moodlet icon and three background images with `UIManager.LoadUIImage` even when nothing about them changed (docs/49: 58 of 59 `LoadUIImage` samples).

**Fix: turn on what exists.** No new fix is needed for the two biggest parts:
- `kMotiveBarImageCache` / switch `motivebar` (`mods/ts3fixes/src/UiCaches.cs`, counters 74-75): skips the four image loads while the bar's moodlet icon and background row are those the game last set. Takes the image loads (5 of 17, plus 1 on the buff path), ~0.4% [inferred].
- `kSimNameCache` / switch `namecache` (`NameCache.cs`, counters 78-79): names from a cache keyed by the stored name text. Takes the name part, ~0.27% [inferred].
- `kBuffGridInPlace` / switch `buffgrid` (counters 76-77) covers the buff-path grid rebuilds, ~0.5% [inferred].

Rejected: skipping bars whose inputs did not change (the motive values decay on every update, so nearly every bar changes [inferred]); skipping the name read for the motive context (would delay a rename's `SimNameChanged`, not identical); coalescing to one refresh per UI frame (docs/43 candidate 2): the event already fires once per Sim update, which `SimUpdate` throttles to 2 high-LOD Sims per tick (docs/04a), so there is little to merge [inferred].

## 5. `TooltipManager.Simulate`

**Measured.** 8 of 885 (0.90%), 4 of 885 (0.45%, cache off), 1 of 586 (0.17%). 7 of the 8 cache-on samples sit under `WindowBase.ShouldShowTooltip`, 5 with `UIManager_GetStringPropertyImpl` as the leaf; the rest are `GetWindowFromPoint`, `GetParentWindow`, the custom-control lookup and the tooltip-callback dictionary [measured]. The spread between runs follows where the cursor rests (the harness never moves the mouse, `game-window-hands-off`).

**Why [verified].** `UIManager` starts one `TooltipManager` task at UI init. Its `Simulate` is an endless loop that sleeps one tick (`Simulator.Sleep(0)`) per pass. Each pass gets the cursor position and the window under it, then walks from that window up through every parent; for each it asks `ShouldShowTooltip`, whose base version reads the window's tooltip callback (a dictionary lookup) and its `TooltipText` (a string property IC, the string marshalled into a new managed string) until one answers. Then it compares the result with the last pass and starts or times out the tooltip with two stopwatches (show after 1.5 s, hide after 15 s by default). With the cursor resting on a deep HUD window the walk reads several strings per tick, at ~33-44 ticks per second [measured iteration rate, s3pp-cache Analysis].

**Fix design (new, `TooltipThrottle`).** Replace `TooltipManager.Simulate` with the same loop plus one gate: run the walk only if the cursor position changed since the last walk or at least `kTooltipPollMs` = 100 ms of wall time (`Environment.TickCount`) passed; otherwise just sleep one tick. Everything else (timing, create, show, hide, dispose task) is the game's code path unchanged.
- Game results: identical. The task only reads UI state and shows or hides tooltip windows; no sim state depends on it [verified: no gameplay calls in the loop besides the tooltip callbacks, which build UI].
- What changes for the player: a tooltip may appear up to 100 ms later than its 1.5 s delay, and a tooltip whose text is set while the cursor stands still appears up to 100 ms later. Below what anyone can see [inferred].
- Saving: with the cursor still, 33-44 walks/s become 10/s, ~70-77% of the cost; with the cursor moving, nothing changes. ~0.4-0.5% = 4-5 ms/s with the mouse idle [inferred].
- Risk to check first: the task starts at UI init, possibly before `Ts3Fixes.Instantiator`'s static constructor patches methods. If `Simulate` is already running, the patch helper's "already compiled" guard refuses it and `ts3fixes.applied` says so. Fallback in that case: at world load, destroy the running `TooltipManager` (found through `Simulator` by type) and add a new one, which runs the patched body. Decide after reading `ts3fixes.applied` from one load [inferred: order unknown].
- **As built (§10):** the gate sits on `Sims3.UI.UIManager.GetWindowFromPoint` instead of a replaced `Simulate`. When that lookup returns null the game's loop just sleeps one tick and changes no state [verified], so answering null to the tooltip task (identified by `Simulator.CurrentTask`) while the cursor rests and < 100 ms passed is the same gate without copying the loop. The restart fallback above would not work: `Patch` refuses any method already compiled, and a new `TooltipManager` would run the same compiled `Simulate`. So a refused patch leaves the fix inert, and `Apply` logs `TooltipManager.Simulate at patch time: already running / not started` to answer the order question from the first load.

## 6. Other cheap wins from the top of the managed list

Looked at: the top 45 self and top 150 inclusive non-IC frames of the default config. Kept only the ones where a cache, early-out or dedupe keeps game results identical.

### 6.1 `Lot.UpdateDetailPriority`: one lot set per tick (new, biggest)

**Measured.** 20 of 885 (2.26%), 25 of 885 (2.82%, cache off), 6 of 586 (1.02%); in the default config 16 of its 26 samples are in `Household.get_AllActors`, 5 in `get_LotCurrent`, 2 in `get_ActiveHousehold`, 1 in the IC [measured]. docs/04a §5 had flagged it as a steady per-lot drag, cost then inferred.

**Why [verified].** `Lot.Simulate` runs for every lot; each pass calls `UpdateDetailPriority`, which calls `CalculateDetailPriority` and hands the answer to the native `World.LotSetDetailPriority`. `CalculateDetailPriority` returns `Home` if the lot is the active household's home lot, else `PlayerSim` if any active-household actor has this lot as `LotCurrent` and is not in a rabbit hole, else `None`. The actor loop reads `Household.ActiveHousehold.AllActors`, which builds a pruned description list and a new actor list on every call. So every lot pays two list allocations and a walk of the household per tick.

**Fix design (new, `DetailPriorityCache`).** Hook `Lot.CalculateDetailPriority` (private, no arguments). Keep the `Home` test live (one field compare). Replace the actor loop with a lookup in a per-tick set of lots: built on the first call in a sim tick from `ActiveHousehold.AllActors` (lots of actors with no rabbit hole), keyed on `Simulator.TicksElapsed()` plus the household reference, at most 100 ms old, never kept while paused (same rules as `SpatialCache.cs`). `World.LotSetDetailPriority` is still called on every pass, so the native side sees the same call sequence.
- Game results: the value is a streaming/detail hint for the native world, never read back by script [verified: the script DLLs only set it, there is no getter]. The only difference: an active-household Sim that changes lot in the middle of a tick is seen by the lots simulated later in that tick one tick late. A verify switch computes both answers and counts mismatches.
- Saving: from one actor-list build per lot per tick to one per tick. ~1.5% = ~15 ms/s in BP [inferred]; more in towns with more lots.

### 6.2 `Cane.SimRequiresCaneWalk`: walk style first (new, exact)

**Measured.** 11 of 1,471 (0.75%; 0.79% / 0.68% / 0.56% per run), 9 of them in `Inventory.FindAll` under `Cane.GetActiveCaneForActor`; the callers are `SimWalkStyleRules.PostProcessWalkStyle` from `GetAdjustedWalkStyleForRoute`, for every route sub-path and portal [measured].

**Why [verified].** `SimRequiresCaneWalk(sim, style)` first finds the Sim's active cane (`Inventory.FindAll<Cane>` builds a list from every inventory stack, then looks for one with `UsingCane`), and only then switches on the walk style: it can return true only for `Run`, `FastWalk`, `FastJog`, `Jog` and `FastRun`. For every other style (`Walk`, swim styles, `Stomp`, ...) the inventory search is wasted. `GetActiveCaneForActor` has no side effects.

**Fix design (new, `CaneEarlyOut`).** Hook `Cane.SimRequiresCaneWalk`: return false at once when the style is not one of the five; otherwise call the original. Same answer in every case [verified: the original returns false for all other styles after the search]. Saving: the share of calls with a non-running style; most routes walk [inferred], so ~0.4-0.7% = 4-7 ms/s. A counter for early-outs vs calls shows the real share.

Not done: an inventory "has a cane" index (needs add/remove hooks on `Inventory`); the early-out takes most of the cost without state.

### 6.3 `Lot.GetHoursOfOperation`: memo per lot per tick (new)

**Measured.** 16 of 1,471 (1.09%; 0.79% / 1.54% per run). Callers: `Lot.IsOpenVenue` from `Camera.GetClosestVisibleLot` (camera task), `Autonomy.CheckAvailabilityOnLot`, `SimRoutingComponent.AboutToPlanRouteCallback`, `DoorSingleGender.IsAllowedThrough`, `VisitCommunityLot.SharedVisitVenueTests`. Inside: `Situation.GetSituations<BachelorParty>` 6, `MetaAutonomyManager.GetTuning` and `Bartending.TryGetBarData` dictionary lookups (with enum keys through the default comparer) 7 [measured].

**Why [verified].** The ref overload reads the venue's meta-autonomy tuning, then asks `Bartending.TryGetHoursOfOperationExtended`: bar data for the venue type, and for a bar lot a scan of **all** live situations (a new list) for a started `BachelorParty` on this lot, which moves the closing hour. Bar data and tuning are static tuning; the venue type changes only in edit mode. Both false paths leave the ref outputs untouched [verified].

**Fix design (new, `HoursMemo`).** Hook `Lot.GetHoursOfOperation(ref float, ref float)`. Per lot, remember (result, open, close) for the current sim tick (same tick key, 100 ms cap and pause rule as §6.1); a hit writes the refs only when the result was true, as the original does. Game results: identical except a bachelor party that starts on that lot in the middle of a tick, seen one tick later; the verify switch counts mismatches. Saving: every repeat call in a tick, ~0.5-0.8% = 5-8 ms/s [inferred]; the hit counter tells.

### 6.4 Existing opt-in fixes that cover the rest

- `TombRoomManager.AboutToPlanRouteCallback` 0.95% (9 of its 14 samples in `Lot.GetObjects`): `kSpatialCache` (docs/20 §4a, counters 62-64) answers repeated (type, lot) queries within a tick. Unmeasured in game.
- Motive panel, names, moodlet grid: §4.

### 6.5 Looked at, not a cheap identical fix

| Frame | Share | Why not |
|---|---|---|
| Dictionaries with enum or `ulong` keys through the default comparer (`Enum.Equals`/`GetHashCode` boxing): `IsInteractionProvidingBenefit`, `ZombieDanceTimeOut.HasStereoOnTheLot` (every lot every tick, EP7), `TryGetBarData`, `GetTuning`, buff and trait `GenericManager` | ~1.0% spread over ~10 dictionaries, none above 0.5% [measured] | a typed comparer needs the dictionary rebuilt; a rebuilt Mono 1.2 dictionary can enumerate in a different order (chains rebuilt), which is a game-result change wherever the game iterates it. Only for dictionaries the game never enumerates, one by one; not worth it now |
| `LotManager.UpdateObjectLot` -> `InternalOnSetRoomAndLevel` | 1.97% | real work per room change event |
| `Sim.SwitchToOutfitWithSpin` | 1.09% | outfit generation (random outerwear, transformation outfits) |
| `Sim.IsAllowedInRoom` -> `RecalculateAllowedRoomsIfNecessary` | 1.09% | already a versioned cache; the cost is the version lookups and real recomputes |
| `CommodityInteractionMap.RemoveInteractionForCommodity` (`List.Remove` with `InteractionObjectPair.Equals`) | 0.75% | on object removal; an index would change list order |
| `SimUpdate.CalculateSimEnvironmentScore` | 0.95% | objects in the Sim's room, real work |
| `Camera.Simulate` -> `GetClosestVisibleLot` | 0.82% | mostly `IsOpenVenue` (taken by §6.3) and `AllActors` |
| `Lot.UpdateReactions` / broadcasters | 3.67% | ours already: `kBroadcasterThrottle` |
| `Route_Plan` and other routing ICs | ~15% self | native route search, docs/04, docs/64 |

## 7. Repro and verify plan

All new fixes follow the house pattern: tuning switch default **off** until measured, a runtime switch for in-load A/B (`patches {name: true/false}`, memory `bundle-testing`), counters, and for the two tick memos a verify switch that runs the original too and counts mismatches. One bundle run per world, BP first.

1. **Build** (main session, not this doc): add the four hooks to `mods/ts3fixes` (one file, e.g. `ScriptHotSpots.cs`), tunings `kDetailPriorityCache`, `kCaneEarlyOut`, `kHoursMemo`, `kTooltipThrottle`, switches `detailprio`, `caneearly`, `hoursmemo`, `tooltipthrottle`, plus `detailprioverify` / `hoursmemoverify`. Read `ts3fixes.applied` after one load: all four patches in (the tooltip one is the order risk, §5).
2. **Mismatch check (one BP load, verify switches on):** both memos compute the original as well; pass = 0 mismatches over 10 minutes of play at speed 3, or only same-tick lot/party changes logged with the Sim or lot id.
3. **In-load A/B:** a managed-sampler profile (`profile-world`, 10 min) with each switch flipped at the midpoint, and `stall-split.ts` on the two halves. Pass: the frame's share falls as expected (§1), no new script errors, 0 stuck Sims. Same for the existing opt-in fixes `motivebar`, `namecache`, `buffgrid` (repro `ui-motivebar-cache` exists in `tools/harness/repros.json`), `spatialcache`.
4. **By eye (Rodney, no harness mouse):** tooltips on HUD buttons still show after ~1.5 s and hide on leave; motive bars, moodlet icons and the moodlet grid look as before.
5. **IP:** repeat 2-3 in IP (more lots and venues; the detail-priority and hours memos scale with lots).

Repro entries to add with the build (memory `repro-actions`): `detail-priority-cache` (switch active household / travel a Sim between lots: counters rise, mismatches 0), `cane-early-out` (an elder with a cane runs and walks), `hours-memo` (bar lot with a bachelor party starting), `tooltip-throttle` (counters only; no mouse).

## 8. Counter slots (reserved 2026-10-16 in docs/registry.md, array raised to 90)

The build took the 10 slots from 80 and raised the array from 80 to 90 in both `Fixes.cs` and `PatchHandlers.cs`:

| Proposed | Switch | Meaning |
|---|---|---|
| 80-82 | `detailprio` | answers from the tick set, tick sets built, mismatches (verify) |
| 83-84 | `caneearly` | early-outs, calls passed to the game |
| 85-87 | `hoursmemo` | memo hits, misses, mismatches (verify) |
| 88-89 | `tooltipthrottle` | walks skipped, walks run |

Agent `patches` keys: `detailprio_hits/_sets/_mismatches`, `caneearly_skipped/_passed`, `hoursmemo_hits/_misses/_mismatches`, `tooltipthrottle_skipped/_walks`. No ts3native counters are needed.

## 9. Ledger

Rows I-74-1 to I-74-8 in docs/51 §8.

## 10. Build status (2026-10-16, not run in game)

| Fix | Hook | Tuning (default) | Switches | Counters | Repro |
|---|---|---|---|---|---|
| §6.1 detail priority | `Lot.CalculateDetailPriority` | `kDetailPriorityCache` (off) | `detailprio`, `detailprioverify` | 80-82 | `detail-priority-cache` |
| §6.2 cane early-out | `Cane.SimRequiresCaneWalk` | `kCaneEarlyOut` (off) | `caneearly` | 83-84 | `cane-early-out` |
| §6.3 hours memo | `Lot.GetHoursOfOperation(ref, ref)` | `kHoursMemo` (off) | `hoursmemo`, `hoursmemoverify` | 85-87 | `hours-memo` |
| §5 tooltip throttle | `Sims3.UI.UIManager.GetWindowFromPoint` | `kTooltipThrottle` (off) | `tooltipthrottle` | 88-89 | `tooltip-throttle` |

- Code: `mods/ts3fixes/src/ScriptHotSpots.cs` (hooks), `TickMemo.cs` (game-independent `TickStamp`, `RefSet`, `HoursMemo`, `PollGate`); offline self-test `node tools/selftest/hotspots/run.ts` (stamp rules incl. clock wrap, reference set vs a list model, hours memo vs a write-only-on-true model over 50k calls, poll gate) passes.
- Verify switches check **one answer in 8** against the game's method and return the game's answer, so an A/B half with verify on keeps most of the saving; a mismatch is counted, not logged per lot (deviation from §7 step 2's "logged with the Sim or lot id": cheaper, and the allowed causes are rare).
- Tick memos follow `SpatialCache.cs`: rebuilt on a new `Simulator.TicksElapsed()`, after 100 ms of wall time, on an active-household change (detail priority), and never kept while paused.
- Draft queue `tools/harness/queues/ts3fixes-hotspots.json` (BP + IP, 12 min, bundle `--ab detailprio,hoursmemo,caneearly,tooltipthrottle`, 3-min slices, repro `hotspots-verify` turns both verify switches on for the load); `queue.ts --dry-run` parses it. Not run. The `--ab` mode runs no managed sampler, so the share drop per frame (I-74-4, I-74-8) needs a second, sampler run (`profile-world` without `--ab`, switch on vs off) once the mismatch check passes.
