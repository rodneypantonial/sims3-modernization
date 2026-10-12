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
        [Tunable] public static bool kRouteFailureBackoff = true;  // unseen townies do not re-plan a route that just failed (30 sim-minutes)
        [Tunable] public static bool kNoStationDetourOffscreen = true; // unseen townies keep their route instead of a subway/hover train detour

        public const string Version = "0.1.0";

        static Instantiator()
        {
            try { Fixes.Apply(); } catch (Exception) { } // never take the game down at startup
            World.OnWorldLoadFinishedEventHandler += delegate { try { Fixes.StartWatchdog(); } catch (Exception) { } };
        }
    }
}
