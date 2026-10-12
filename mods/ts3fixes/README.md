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
| `kStuckWatchdog` | on | An unseen townie that has not moved for 60 sim-minutes on its way home, or 120 sim-minutes while walking anywhere (e.g. two Sims blocking each other in a doorway), gets the game's own reset-and-send-home; at most once per Sim per sim-day. |
| `kSaveCleanup` | **off** | Once a sim day, remove weak relationships between townies (strangers/acquaintances, never family, romance, coworkers or recent contacts) and other-world Sims with no links to this world, using the game's own removal functions (docs/14). Opt-in. |
| `kBroadcasterThrottle` | on | Reaction broadcasters (music, fires, smells, ...) far from the camera check for nearby Sims less often. |
| `kWalkStyleComparer` | on | A faster lookup of walk styles with the same result. |

Never affected: your household, any Sim near the camera or near your Sims, Sims travelling with others, and lots with their own arrival rules (apartments, resorts, diving lots, moving houseboats). After a shortcut the game's own code runs the last few metres and the arrival, so what the Sim does next is unchanged; spots are checked with the game's own "can walk off this spot" test so nobody is left stuck.

## Install

Put `ts3fixes.package` in `Documents\Electronic Arts\The Sims 3\Mods\Packages\` (needs the usual `Mods\Resource.cfg`). Script mods must be enabled in the game options.

To change a setting, edit the tuning resource `Ts3Fixes.Instantiator` in the package (for example with s3pe) and set the value to `False`; it takes effect at the next game start.

## Compatibility

Conflicts with any mod that replaces the same methods: `GoHome.Run`, `VisitCommunityLot.Run`, `VisitLot.Run`, `GoToLot.Run`, `GoToCommunityLot.Run`, `RabbitHole.RouteNearEntranceAndEnterRabbitHole`, `Sim.GoToVirtualHome.GoToVirtualHomeInternal.Run`, `SimRoutingComponent.SetRouteOptions` / `GetRouteFailPath`, `SimRoutingComponent.CheckAndUpdateRouteForStations`, `ScriptCore.Route.Plan`, `ReactionBroadcaster.PulseSims`, `SimRoutingComponent.WalkStyleComparer.Compare`. Tested with NRaas ErrorTrap and Overwatch, MonoPatcher, simler90 core and the community routing fixes listed in docs/01.

## Build from source

`node tools/setup/build-fixes.ts` (Windows; needs the game's assemblies extracted with `tools/extract-assemblies.ts`). Or install with `node tools/setup/install-fixes.ts install --only stutter-fixes`.

Details and measurements: `docs/04-re-findings.md` F8-F13, `docs/10`, `docs/11`, `docs/12`.
