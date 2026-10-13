# TS3 stutter fixes (`ts3fixes.package`)

A script mod for The Sims 3 (Steam 1.67, all expansions) that makes the simulation cheaper per tick, so the game stutters less and runs more sim time per real second. It replaces a few game methods at startup; it does not replace whole game assemblies (no core mod).

Measured with all fixes on, 10 minutes at speed 3 against the same saves without the mod (docs/04 F13):

| World | Sim iterations per second | Hitches per second (frames of 50 ms or more) |
|---|---|---|
| Bridgeport | +61% | -39% |
| Sunset Valley | +85% | -47% |
| Isla Paradiso | +133% | -51% |

## What it does

| Setting (tuning) | Default | What it changes |
|---|---|---|
| `kGoHomeShortcut` | on | A townie going home that nobody can see (off screen, far from the camera and from your Sims) appears on its home lot instead of walking or driving the whole way. Docked houseboats included. |
| `kBuildingTripShortcut` | on | The same for trips to work, school, shops and other rabbit holes: the townie starts at the entrance. |
| `kLotTripShortcut` | on | The same for visits to parks, venues and other homes. |
| `kVirtualHomeShortcut` | on | Homeless townies leaving the world fade out at once when unseen (the game would first walk them to a random lot). |
| `kNoRouteFailWalkOffscreen` | on | An unseen townie whose route fails skips the extra searches and the walk to the obstacle that only feed the route-fail animation. |
| `kRouteFailureBackoff` | on | An unseen townie does not re-plan a route to the same place for 30 sim-minutes after it failed. |
| `kNoStationDetourOffscreen` | on | An unseen townie keeps the route it planned instead of rolling for a subway or hover train detour (extra searches nobody would see). |
| `kStuckWatchdog` | on | An unseen townie that has not moved for 60 sim-minutes on its way home, or 120 sim-minutes while walking anywhere (e.g. two Sims blocking each other in a doorway), gets the game's own reset-and-send-home; a townie with no household home lot (homeless or virtual resident) is faded out and removed instead, as the game's own virtual-home trip ends. At most once per Sim per sim-day, plus a second time that day only if it is stuck again more than 10 m from the first spot. Each action logs the home kind (none/virtual, shell apartment, house) to `recent` and `moved`; counters `watchdog_home_none/shell/house`, `watchdog_second`. |
| `kStuckWatchdogIdle` | on | Extends the watchdog (needs `kStuckWatchdog`) to the cases NRaas Overwatch's nightly Stuck Check covers, checked every 10 sim-minutes: a Sim standing still on a spot it cannot walk off (the game's own "can walk off this spot" test; after 60 sim-minutes for an unseen townie, one sim-day for anyone else), a Sim in a hidden room, a Sim in the same spot and interaction for a whole sim-day, and any Sim, your household included, whose route plans failed 10 times over 30 sim-minutes without moving. Each gets the game's own placement (a Sim with nothing queued is just moved to a safe spot) or reset. Your household is only reset in the last case, as Overwatch does; Sims near the camera only in the one-day cases and the last. At most one route check per sim tick. Same daily limit as above. Counters `watchdog_idle_unroutable/hidden_room/stationary`, `watchdog_route_fails`. With this on, Overwatch's own Stuck Check can be turned off (installer entry `overwatch-no-stuckcheck`). |
| `kSaveCleanup` | **off** | Once a sim day, remove weak relationships between townies (strangers/acquaintances, never family, romance, coworkers or recent contacts) other-world Sims with no links to this world, and empty life-event lists, using the game's own removal functions (docs/14). Opt-in. |
| `kPortalAdaptorLeak` | on | A Sim swimming out to sea or boarding a houseboat no longer leaves a dead route callback (with its route and route objects) attached to it; in Isla Paradiso these piled up at ~23 per sim day in the save and could block a later route's replan (docs/09 §5). With `kSaveCleanup` on, the daily cleanup also removes the ones existing saves already hold. Test switch `portaladaptorleak`; counters `portaladaptor_unsubscribes`, `savecleanup_route_callbacks`. |
| `kCasGrid` | **off** | Create-a-Sim hair and makeup lists fill a few tiles per frame instead of all at once (the way the game already fills clothing), so the panel answers right away; hair preset data is kept for the rest of the CAS session so revisits are faster. Hats, costume makeup and lists with store items are unchanged. Test switches `casgrid`, `casmemo`; counters `casgrid_fills`, `casmemo_hits`, `casmemo_lookups` (docs/21 §6). Opt-in until tested in game. |
| `kSpatialCache` | **off** | Asking for the objects of one type on one lot again in the same sim tick (local autonomy, tomb-room routing, parked cars, auto lights) reuses the first answer instead of walking the lot again. Cleared every tick and whenever an object is destroyed; nothing is cached while paused. Test switch `spatialcache`; counters `spatialcache_hits`, `spatialcache_misses`, `spatialcache_clears` (docs/20 §4a). Opt-in until measured. |
| `kBroadcasterThrottle` | on | Reaction broadcasters (music, fires, smells, ...) far from the camera check for nearby Sims less often. |
| `kWalkStyleComparer` | on | A faster lookup of walk styles with the same result. |
| (none; only with NRaas Overwatch) | on | Overwatch's nightly Stuck Check runs one route check per sim tick instead of all in one (a ~300 ms hitch per sim night). Same checks, same order, same resets; later Sims and Overwatch's other nightly cleanups finish a few ticks later. Nothing happens without Overwatch. Test switch `overwatchstuck`; counters `overwatch_checks`, `overwatch_yields`. |

Never affected by the shortcuts: your household, any Sim near the camera or near your Sims (the stuck checks above are the only exceptions, as listed), Sims travelling with others, and lots with their own arrival rules (apartments, resorts, diving lots, moving houseboats). After a shortcut the game's own code runs the last few metres and the arrival, so what the Sim does next is unchanged; spots are checked with the game's own "can walk off this spot" test so nobody is left stuck.

## Install

Put `ts3fixes.package` in `Documents\Electronic Arts\The Sims 3\Mods\Packages\` (needs the usual `Mods\Resource.cfg`). Script mods must be enabled in the game options.

To change a setting, edit the tuning resource `Ts3Fixes.Instantiator` in the package (for example with s3pe) and set the value to `False`; it takes effect at the next game start.

## Compatibility

Conflicts with any mod that replaces the same methods: `GoHome.Run`, `VisitCommunityLot.Run`, `VisitLot.Run`, `GoToLot.Run`, `GoToCommunityLot.Run`, `RabbitHole.RouteNearEntranceAndEnterRabbitHole`, `Sim.GoToVirtualHome.GoToVirtualHomeInternal.Run`, `SimRoutingComponent.SetRouteOptions` / `GetRouteFailPath`, `SimRoutingComponent.CheckAndUpdateRouteForStations`, `ScriptCore.Route.Plan`, `ReactionBroadcaster.PulseSims`, `SimRoutingComponent.WalkStyleComparer.Compare`, `PortalAnimateThroughAction.DoPortal`, `CASHair.PopulateTypesGrid`, `CASMakeup.PopulatePartsGrid`, `Sims3.SimIFace.Queries.GetObjects(Type, ulong)`, `Simulator.DestroyObject`, and NRaas Overwatch's `NRaas.CommonSpace.Helpers.SimEx.IsPointInLotSafelyRoutable` (Overwatch's own copy only). Tested with NRaas ErrorTrap and Overwatch, MonoPatcher, simler90 core and the community routing fixes listed in docs/01.

## Build from source

`node tools/setup/build-fixes.ts` (Windows; needs the game's assemblies extracted with `tools/extract-assemblies.ts`). Or install with `node tools/setup/install-fixes.ts install --only stutter-fixes`.

Details and measurements: `docs/04-re-findings.md` F8-F13, `docs/10`, `docs/11`, `docs/12`.
