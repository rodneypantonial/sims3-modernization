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
        [Tunable] public static bool kJobYield = false;            // opt-in until measured: a route plan that would wait natively for a running route job sleeps its task a tick at a time instead (needs ts3native [RouteAsync] JobYield; RouteBackoff.cs StepAside, docs/183)
        [Tunable] public static bool kDriverTaskAsync = false;     // opt-in until measured: a traffic car's plan in its LiteDriver task and a ridden vehicle's plan in its rider's task go async as own-task plans (RouteBackoff.cs OwnerTaskStart, docs/163 §7)
        [Tunable] public static bool kOverwatchStuckSlice = false; // opt-in until measured: NRaas Overwatch's Stuck Check takes a sim tick inside its walk over every Sim once 8 ms have passed since its last tick (OverwatchStuck.cs, docs/145 §12)
        [Tunable] public static bool kStuckCheckOwn = false;      // opt-in until measured: with stuckcheckasync, the stuck check's plans in our drain task go async as own-task plans (RouteBackoff.cs OwnerTaskStart, docs/163 §7)
        [Tunable] public static bool kFailRepeat = false;         // opt-in until measured: a Sim's plan that repeats its own no-path failure (same goal, same spot) within 500 ms returns that failure without searching again (RouteBackoff.cs, docs/163 §4)
        [Tunable] public static bool kDisposeSpread = false;       // opt-in until measured: Sims leaving the world, service Sims and traffic cars at despawn are destroyed one per sim tick by our task instead of in one tick (DisposeSpread.cs, docs/74 §12)
        [Tunable] public static bool kIdlePlanMemo = false;        // opt-in until measured: the idle watchdog skips Sims that planned a route from their spot since they went still, at most 4 checks a pass (RouteFixes.cs, docs/151 D5)
        [Tunable] public static bool kSeasonMarkerCache = false;   // opt-in until measured: Queries.GetGlobalObjects(typeof(SeasonalLotMarker)) (every world-lot object Dispose) answered from a live marker list, 1 in 8 checked against the game (SeasonMarkerCache.cs, docs/74 §11 (a))
        [Tunable] public static bool kOverwatchStuckNoticeSummary = false; // opt-in until tried: NRaas Overwatch's per-Sim stuck-reset notices held, one summary per 2 sim-hours (OverwatchNotice.cs, docs/145 §13)
        [Tunable] public static bool kOverwatchIgnoreStuckProbes = true; // NRaas Overwatch's Stuck Check V2 does not count the game's own stuck-check probe plans as the Sim's route failures (no double resets; OverwatchProbe.cs, docs/163 §9)
        [Tunable] public static bool kSafeSpotYield = false;       // opt-in until measured: SafeSpot sleeps one tick after each FindGoodLocation in the Sim's own task (Fixes.cs SafeSpotYield, docs/74 §11 (b))
        [Tunable] public static bool kStuckCheckCap = false;      // opt-in until verified: with stuckcheckown, the stuck check's probe jobs of rounds 1-2 are cut at ts3native's PlanDeadlineMs like a foreign sync plan (FewerPlans.cs, RouteBackoff.cs OwnerTaskStart, docs/163 §10)
        [Tunable] public static bool kUnseenFallbackStuck = false;    // opt-in until verified: the 3 AM stuck check plans nothing for a Sim the player cannot see (live mode) and re-arms itself (UnseenFallback.cs, docs/202 §3.3)
        [Tunable] public static bool kUnseenFallbackPlan = false;     // opt-in until verified: route plans of unseen Sims are cut at ts3native's [UnseenFallback] DeadlineMs (docs/202 §3.1)
        [Tunable] public static bool kUnseenFallbackSites = false;    // opt-in until verified: pushes the unseen Sims' ids to ts3native's [UnseenFallback] Sites=1 (C1-C4 short lock waits, docs/202 §3.4)
        [Tunable] public static bool kUnseenFallbackTeleport = false; // opt-in until verified: a cut own plan of an unseen townie places it at its route goal (docs/202 §3.2)
        [Tunable] public static bool kUnseenFallbackArrive = false;   // opt-in until verified: an unseen townie walking to a spot in view skips the hidden part of the walk (hold, placed at a hand-off point out of view; docs/202 §3.6)
        [Tunable] public static bool kUnseenFallbackLeave = false;    // opt-in until verified: a townie leaving the view walks to a hand-off point out of view, then the rest is held and skipped (docs/202 §3.6)
        [Tunable] public static bool kUnseenFallbackPre = false;      // opt-in until verified: an unseen townie's own walk to an unseen lot goal is held for the walk time, then placed at the goal (no search; docs/202 §3.5)
        [Tunable] public static bool kPrePlanCallback = false;     // opt-in until measured: Route.AboutToPlanCallback runs before an async START, the job's locked-door list survives the original plan's second callback (RouteBackoff.cs PreCb, docs/185 §5)

        public const string Version = "0.1.1";

        static Instantiator()
        {
            try { Fixes.Apply(); } catch (Exception) { } // never take the game down at startup
            World.OnWorldLoadFinishedEventHandler += delegate { try { Fixes.StartWatchdog(); } catch (Exception) { } try { Fixes.StartViewerPush(); } catch (Exception) { } try { Fixes.StartUfSitesPush(); } catch (Exception) { } try { Fixes.StartSaveCleanup(); } catch (Exception) { } try { Fixes.ApplyOverwatchStuck(true); } catch (Exception) { } try { Fixes.ApplyOverwatchSpread(true); Fixes.OverwatchSpreadWorldLoad(); } catch (Exception) { } try { Fixes.ApplyOverwatchNotice(true); Fixes.OverwatchNoticeWorldLoad(); } catch (Exception) { } try { Fixes.ApplyOverwatchProbe(true); } catch (Exception) { } try { Fixes.ApplyErrorTrapSpread(true); Fixes.ErrorTrapSpreadWorldLoad(); } catch (Exception) { } Fixes.ClearNameCache(); Fixes.AutShadowWorldLoad(); Fixes.SeasonMarkerWorldLoad(); try { Fixes.DiagWorldLoad(); } catch (Exception) { } try { Fixes.MemSusWorldLoad(); } catch (Exception) { } };
        }
    }
}
