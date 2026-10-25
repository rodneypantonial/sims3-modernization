using System;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Script-mod entry point: the game parses the tuning resource whose instance is FNV64("Ts3Fixes.Instantiator"),
    // which runs this static constructor at startup, before any world loads (the patches must go in before the game
    // compiles the methods they replace). The other tunables are the player's settings (true = on); they are read when
    // a fix runs, so a changed tuning file takes effect at the next game start.
    public class Instantiator
    {
        [Tunable] protected static bool kInstantiator = false;
        [Tunable] public static bool kGoHomeShortcut = true;       // unseen townies arrive home without the long walk
        [Tunable] public static bool kBuildingTripShortcut = true; // ... at work, school and shops
        [Tunable] public static bool kLotTripShortcut = true;      // ... at parks, venues and other homes
        [Tunable] public static bool kBroadcasterThrottle = true;  // off-screen reaction broadcasters pulse less often
        [Tunable] public static bool kWalkStyleComparer = true;    // faster walk-style lookups (same result)
        [Tunable] public static bool kVirtualHomeShortcut = true;  // unseen homeless townies leave the world without the walk
        [Tunable] public static bool kNoRouteFailWalkOffscreen = true; // unseen townies skip the extra route-fail searches
        [Tunable] public static bool kStuckWatchdog = true;        // reset unseen townies stuck going home for 60 sim-minutes
        [Tunable] public static bool kStuckWatchdogIdle = true;    // ... also idle Sims on unroutable spots or in hidden rooms, a day in one spot, failing route plans (needs kStuckWatchdog)
        [Tunable] public static bool kSaveCleanup = false;       // opt-in: once a sim day drop weak NPC-NPC relationships, unlinked other-world MiniSims and empty life-event lists (docs/14)
        [Tunable] public static int kSnapshotCap = 0;            // opt-in: keep only the newest N memory pictures per active-household Sim, 0 = off (docs/14 7b)
        [Tunable] public static bool kRouteFailureBackoff = true;  // unseen townies do not re-plan a route that just failed (30 sim-minutes)
        [Tunable] public static bool kNoStationDetourOffscreen = true; // unseen townies keep their route instead of a subway/hover train detour
        [Tunable] public static bool kPortalAdaptorLeak = true;    // ocean/houseboat portals no longer leave a dead route callback on the Sim
        [Tunable] public static bool kCasGrid = false;             // opt-in: CAS hair/makeup grids fill a few tiles per tick; hair preset data cached per CAS session (docs/21)
        [Tunable] public static bool kSpatialCache = false;        // opt-in: repeated "objects of type T on lot L" queries in one sim tick share one native lot walk (docs/20)

        [Tunable] public static bool kRoutableCache = false;       // opt-in until measured: the game's "is this spot safely routable" checks share the SafeSpot verdict cache
        [Tunable] public static bool kStuckCheckBusy = false;      // opt-in until measured: the 3 AM stuck check skips Sims held by a bed, chair or other object
        [Tunable] public static bool kStuckCheckAsync = false;     // opt-in until measured: the 3 AM stuck check runs in our own task, one Sim per tick, so [RouteAsync] FallbackAsync plans it on the worker without the world lot task sleeping in the alarm (docs/134 §8)
        [Tunable] public static bool kSafeSpotCap = false;         // opt-in until measured: a reset's safe-spot search near the front door checks at most 6 cells (EA's own vacation-start limit), docs/141 §7.1
        [Tunable] public static bool kFarthestLotSorted = false;   // opt-in until measured: GetFarthestLot with routing plans the farthest lot first (same answer), at most 3 plans, sim-day cache, docs/141 §7.2
        [Tunable] public static bool kIndoorPositionCap = false;   // opt-in until measured: Lot.GetRoutableIndoorPositions gives a room up after 4 failed plans, docs/141 §7.3
        [Tunable] public static bool kFastRefTable = true;         // on since v0.1.2 (save matrix 27/28 PASS, saves -14..-24%): the save serializer's object identity table no longer degrades past 16k objects (RefTable.cs)
        [Tunable] public static bool kCachedReflection = false;    // opt-in until measured: the save serializer answers repeated per-type questions from a per-save cache (CachedReflection.cs)
        [Tunable] public static bool kMotiveBarImageCache = true;  // on since v0.1.2 (BP A/B PASS, UI only): motive bars keep their moodlet images while the moodlet is unchanged (UiCaches.cs)
        [Tunable] public static bool kBuffGridInPlace = true;      // on since v0.1.2 (BP A/B PASS, UI only): the moodlet grid updates its cells in place unless moodlets were added, removed or changed icon (UiCaches.cs)
        [Tunable] public static bool kSimNameCache = true;         // on since v0.1.2 (BP A/B PASS, UI only): Sim first/last/full names cached by their stored strings (NameCache.cs)
        [Tunable] public static bool kDetailPriorityCache = false; // opt-in until measured: lots learn whether an active-household Sim stands on them from one lot set per sim tick (ScriptHotSpots.cs, docs/74)
        [Tunable] public static bool kCaneEarlyOut = false;        // opt-in until measured: route walk styles skip the inventory search for a cane when the style can never need one (same answer; ScriptHotSpots.cs, docs/74)
        [Tunable] public static bool kTooltipThrottle = false;     // opt-in until measured: with the mouse at rest the tooltip task looks for the window under the cursor at most every 100 ms (ScriptHotSpots.cs, docs/74)
        [Tunable] public static bool kSafeSpotReuse = false;       // opt-in until measured: SafeSpot reuses the FindGoodLocation result stored with a verified spot instead of searching again (docs/95 5.1)
        [Tunable] public static bool kWildflowerProbe = false;     // test only: the spring wildflower drain logs tries and times per fire, same behaviour (ScriptHotSpots.cs, docs/110)
        [Tunable] public static bool kWildflowerTask = false;      // opt-in until measured: spring wildflower placement runs one try per sim tick in its own task instead of all tries inside the alarm (docs/110 W1)
        [Tunable] public static bool kDoorMirror = false;          // opt-in: per-lot managed door records, pushed to the ts3native door mirror at each plan (DoorMirror.cs, docs/105 §10.5)
        [Tunable] public static bool kAutShadow = false;          // test only: [AutShadow] pilot, meta autonomy scored again in shadow on ts3native's worker and compared (AutShadow.cs, docs/122)
        [Tunable] public static bool kReplanDefer = false;         // opt-in: door/elevator route replans go to the Sim's own route loop instead of the callback (ReplanDefer.cs, docs/141 §6)
        [Tunable] public static bool kReplanSpread = false;        // opt-in: build/buy exit and replan-all spread over sim ticks, 4 routing Sims a tick (ReplanDefer.cs, docs/141 §6)
        [Tunable] public static bool kOverwatchSpread = false;     // opt-in until measured: NRaas Overwatch's nightly cleanups run one type at a time, 10 sim-minutes apart, its inventory cleanup as our sliced pass; off by itself with a spreading Overwatch build (OverwatchSpread.cs, docs/145)
        [Tunable] public static bool kOverwatchChain = false;      // opt-in until measured: NRaas Overwatch's nightly cleanups one sim tick apart in its own order and task, its inventory cleanup as our sliced pass; overwatchspread and a spreading Overwatch build win (OverwatchSpread.cs, docs/145 §10)
        [Tunable] public static bool kErrorTrapSpread = false;     // opt-in until measured: NRaas ErrorTrap's load-up inventory check skips the name lookup for objects without an inventory and spreads over ticks; off by itself with a spreading ErrorTrap build (ErrorTrapSpread.cs, docs/146)
        [Tunable] public static bool kHoursMemo = false;           // opt-in until measured: a lot's opening hours are worked out once per sim tick (ScriptHotSpots.cs, docs/74)
        [Tunable] public static bool kUnseenEnvThrottle = false;   // opt-in until measured: unseen high-LOD Sims score their surroundings one time in kUnseenThrottleEvery (UnseenThrottle.cs, docs/144)
        [Tunable] public static bool kUnseenMotiveThrottle = false; // opt-in until measured: unseen high-LOD Sims update needs, moodlets and mood every kUnseenThrottleEvery sim-minutes, same totals (docs/144)
        [Tunable] public static bool kUnseenBroadcastThrottle = false; // opt-in until measured: unseen reaction broadcasters pulse one time in kUnseenBroadcastEvery each, instead of the shared 1 in 4 (docs/144)
        [Tunable] public static int kUnseenThrottleEvery = 4;      // N for the two Sim throttles above
        [Tunable] public static int kUnseenBroadcastEvery = 16;    // N for the broadcaster throttle (kBroadcasterThrottle is already 1 in 4)
        [Tunable] public static bool kEaSpreadLots = false;        // opt-in until measured: EA's 3 AM venue cleanup does a few lots per sim tick in its own task, same steps (EaSpread.cs, docs/147)
        [Tunable] public static bool kEaSpreadPets = false;        // opt-in until measured: the pet pool alarm builds its pet in a task, so EA's own sleeps between CAS steps take effect (docs/147)
        [Tunable] public static bool kEaSpreadDecay = false;       // opt-in until measured: midnight relationship decay, a slice of Sims per sim tick, one step per pair per day (docs/147)
        [Tunable] public static bool kUnseenCarThrottle = false;   // opt-in until measured: an unseen traffic car (LiteDriver) bound for an unseen spot stays parked on kUnseenCarEvery-1 of kUnseenCarEvery rounds instead of planning a drive (UnseenThrottle.cs, docs/143 §8.3)
        [Tunable] public static int kUnseenCarEvery = 4;           // N for the traffic car rule
        [Tunable] public static bool kRouteSources = false;        // test only: route plans counted by source (docs/151 §2 buckets), repeats, leg-end and bouncer shares; no behaviour change (RouteSources.cs)
        [Tunable] public static bool kLegReplanSkip = false;       // opt-in until measured: no replan after a boat/vehicle get-out when the planned rest of the route starts within 0.5 m of the Sim (RouteSources.cs, docs/151 D1)
        [Tunable] public static bool kBouncerSlotSkip = false;     // opt-in until measured: a velvet-rope bouncer standing on its spot does not re-route to it every 5 s (RouteSources.cs, docs/151 D3)
        [Tunable] public static bool kStuckCheckNear = false;      // opt-in until measured: the 3 AM stuck check plans its random lot points nearest first (same points, same verdict), so fewer failed plans before the first success (FewerPlans.cs, docs/163 §3)
        [Tunable] public static bool kFailRepeat = false;          // opt-in until measured: a Sim's plan that repeats its own no-path failure (same goal, same spot) within 500 ms returns that failure without searching again (RouteBackoff.cs, docs/163 §4)
        [Tunable] public static bool kIdlePlanMemo = false;        // opt-in until measured: the idle watchdog skips Sims that planned a route from their spot since they went still, at most 4 checks a pass (RouteFixes.cs, docs/151 D5)

        public const string Version = "0.1.6";

        static Instantiator()
        {
            try { Fixes.Apply(); } catch (Exception) { } // never take the game down at startup
            World.OnWorldLoadFinishedEventHandler += delegate { try { Fixes.StartWatchdog(); } catch (Exception) { } try { Fixes.StartViewerPush(); } catch (Exception) { } try { Fixes.StartSaveCleanup(); } catch (Exception) { } try { Fixes.ApplyOverwatchStuck(true); } catch (Exception) { } try { Fixes.ApplyOverwatchSpread(true); Fixes.OverwatchSpreadWorldLoad(); } catch (Exception) { } try { Fixes.ApplyErrorTrapSpread(true); Fixes.ErrorTrapSpreadWorldLoad(); } catch (Exception) { } Fixes.ClearNameCache(); Fixes.AutShadowWorldLoad(); try { Fixes.DiagWorldLoad(); } catch (Exception) { } };
        }
    }
}
