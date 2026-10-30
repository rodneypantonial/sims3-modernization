<!-- Draft reply for issue #87. Not posted. -->
Thanks. We confirmed the cause in the decompiled code: all four `WaitToRoute.Init` go straight to `RouteToLot` when the worker is already on the lot. That happens inside the situation's constructor. Child abuse then sleeps in `ShiftKidsToNewHouseholds` / `MoveSimDescriptionToNewHousehold`. The ErrorTrap entries are yours, so we mark them as community-reported.

What's on branch `feature/seasons-spread-ea-fixes` (switch `socialworkeronlot`, off by default) is a smaller change than yours. We hook `RouteToLot.Init` in the four situations, not `WaitToRoute.Init`. When the worker is already on the lot, the whole `RouteToLot.Init` runs 1 sim-minute later, from a yielding alarm (`AlarmWillYield`). It runs only if the situation is still in that state.

- **Same behaviour as EA otherwise.** The visit is not routed through `TimeToRoute`, so it does not get the active-household and adult-on-lot exits. Only the timing changes, and the visit no longer runs inside a constructor.
- **Also covered:** the rare case where `TimeToRoute`'s own alarm (which does not yield) fires after the worker has walked onto the lot.
- **Limit:** our alarm is not persisted. A save made in that one sim-minute loads with the situation still waiting in `RouteToLot`.

Counters: visits deferred, deferred visits dropped (the situation ended first).

We did not build a forced repro, because forcing a child abuse visit takes a household's children. `EaFixCheck swstate` lists the social worker situations and their state, so a leftover `RouteToLot` with an idle worker shows up. If the on-lot case comes back in your game with the switch on, the counters and a `swstate` line would confirm it.
