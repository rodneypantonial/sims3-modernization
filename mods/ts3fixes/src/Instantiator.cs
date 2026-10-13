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

        public const string Version = "0.1.0";

        static Instantiator()
        {
            try { Fixes.Apply(); } catch (Exception) { } // never take the game down at startup
            World.OnWorldLoadFinishedEventHandler += delegate { try { Fixes.StartWatchdog(); } catch (Exception) { } try { Fixes.StartSaveCleanup(); } catch (Exception) { } try { Fixes.ApplyOverwatchStuck(true); } catch (Exception) { } };
        }
    }
}
