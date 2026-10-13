using System;
using System.Reflection;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Fewer route plans on the sim thread (docs/04a §7, ranking from the managed profiles and /taskstall). Both default off
    // until measured: `node tools/harness/profile-world.ts --tag bp-fewer --ab routablecache,stuckcheckbusy`.
    public static partial class Fixes
    {
        static void ApplyFewerPlans()
        {
            Patch(typeof(Sim), "IsPointInLotSafelyRoutable", typeof(RoutableCacheHook), "RoutableHook", "RoutableOrig");
            Type helper = typeof(LotManager).GetNestedType("RunStuckCheckHelper", BindingFlags.NonPublic);
            if (helper == null) { AppliedLog().Add("LotManager.RunStuckCheckHelper: not found, skipped"); return; }
            Patch(helper, "RunStuckCheck", typeof(StuckCheckBusyHook), "RunHook", "RunOrig");
        }

        // Sim.IsPointInLotSafelyRoutable is a full route plan (spot to the lot's 4 corners and 4 edge midpoints) that the
        // game asks in loops: FindRoutablePointInsideNearFrontDoor tests candidate points near the door one plan each (every
        // reset with send-home, NRaas Instantiation), AttemptToFindSafeLocation tries the current spot then two nearby ones,
        // CreateTeleportInstanceToPosition one more. Profiles: ~60 of 15.4k managed samples outside our own checks, plus 345
        // and 456 ms stalls (taskstall 2026-10-13 01:49). The hook answers from the verdict cache SafeSpot and the watchdog
        // already use (0.5 m cell + exact route profile, one sim-day per lot, dropped on a household change or a lot move;
        // see SpotVerdicts), so the game's callers and ours share verdicts. Uncached: the world lot (no plan there) and a
        // Sim with a parent (profile not exact). Our own checks (SpotVerdict, sInOwnCheck) pass straight through.
        // Sim has no use for a subclass here, so the hook is a plain class (as BroadcasterPulseHook).
        // Switch "routablecache" (default off). Counters: [65] cache hits, [66] plans run and recorded.
        public class RoutableCacheHook
        {
            public bool RoutableHook(Lot lot, Vector3 pos)
            {
                Sim sim = (Sim)(object)this;
                if (sInOwnCheck || lot == null || lot.IsWorldLot || !ShortcutOn("routablecache")) return RoutableOrig(lot, pos);
                RouteProfile p = ProfileOf(sim);
                if (!p.Ok) return RoutableOrig(lot, pos);
                SpotVerdicts v = Verdicts(lot);
                VerdictKey key = KeyOf(p, pos);
                int[] c = PatchCounters();
                bool ok;
                if (v.Cells.TryGetValue(key, out ok)) { c[65]++; return ok; }
                ok = RoutableOrig(lot, pos);
                v.Cells[key] = ok;
                c[66]++;
                return ok;
            }
            public bool RoutableOrig(Lot lot, Vector3 pos) { return false; } // receives Sim.IsPointInLotSafelyRoutable's original header
        }

        // LotManager.RunStuckCheckHelper (3 AM, every actor): a Sim that has not moved 20-60 sim-minutes later gets up to
        // Sim.kNumberOfPlanAttemptsForStuckSim (10) plans to random lot points, synchronously in one alarm tick; up to 3
        // rounds before a reset. At 3-4 AM nearly every townie is asleep, so nearly every check plans from a bed.
        // Profiles: 110 of 15.4k managed samples (0.7%, all at night) and one 645 ms stall (taskstall 2026-10-13 08:06).
        // A Sim held by an object (bed, chair, any slotted posture; not a vehicle) is busy, not stuck: the hook ends the
        // check as the game does after a successful plan (no re-arm). Standing, swimming and in-vehicle Sims run the game's
        // check unchanged. Frozen in an object stays covered by our watchdog (watchdogidle: a sim-day in one spot) and
        // Overwatch. Switch "stuckcheckbusy" (default off). Counter: [67] checks skipped.
        public class StuckCheckBusyHook
        {
            public void RunHook(Sim sim)
            {
                if (ShortcutOn("stuckcheckbusy") && sim != null && sim.Parent != null && !(sim.Posture is Sim.StandingPosture)
                    && !(sim.Parent is Sims3.Gameplay.Objects.Vehicles.Vehicle))
                {
                    PatchCounters()[67]++;
                    return;
                }
                RunOrig(sim);
            }
            public void RunOrig(Sim sim) { } // receives RunStuckCheckHelper.RunStuckCheck's original header
        }
    }
}
