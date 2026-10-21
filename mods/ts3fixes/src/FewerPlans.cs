using System;
using System.Reflection;
using Sims3.Gameplay;
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
                if (sim != null && ShortcutOn("stuckcheckasync") && GameStates.IsLiveState && !GameStates.IsTravelling && !GameStates.IsWorldShuttingDown)
                {
                    StuckCheckDefer(this, sim);
                    return;
                }
                RunOrig(sim);
            }
            public void RunOrig(Sim sim) { } // receives RunStuckCheckHelper.RunStuckCheck's original header
        }

        // docs/134 §8: the stuck check's alarm runs inside the world lot's task (Lot.Simulate -> AlarmManager.Simulate, a plain
        // alarm, not YieldRequired). Its plans are foreign-task plans there, so with [RouteAsync] FallbackAsync=1 the world lot
        // task sleeps inside the alarm while the worker plans (up to 10 plans per Sim), holding back every later world alarm
        // and the world lot's own updates; with FallbackAsync=0 they all run in one tick. Switch "stuckcheckasync" (default off):
        // the alarm only queues the check; our one task runs the game's own RunStuckCheck (RunOrig, same helper object, so the
        // same position test, attempt count, reset or re-arm) for one Sim per tick. Its plans are still foreign-task plans (our
        // task is not the Sim's), so FallbackAsync sends them to the worker and only our task sleeps. The check runs >= 1 tick
        // after the alarm instead of in it. Counters: [127] queued, [128] run, [129] dropped (Sim gone, world left, exception),
        // [130] most waiting at once, [131] longest wait in sim-seconds.
        sealed class StuckCheck { public StuckCheckBusyHook Helper; public Sim Sim; } // Helper is really a RunStuckCheckHelper (swapped body)
        static readonly SliceQueue<StuckCheck> sStuckQueue = new SliceQueue<StuckCheck>();
        static ObjectGuid sStuckTask = ObjectGuid.InvalidObjectGuid;

        static void StuckCheckDefer(StuckCheckBusyHook helper, Sim sim)
        {
            int[] c = PatchCounters();
            if (!(sStuckTask.IsValid && Simulator.GetTask(sStuckTask) != null))
            {
                c[129] += sStuckQueue.Clear(); // left over from a task the world unload killed
                sStuckTask = Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(StuckCheckDrain));
            }
            StuckCheck e = new StuckCheck();
            e.Helper = helper;
            e.Sim = sim;
            sStuckQueue.Add(e, Sims3.Gameplay.Utilities.SimClock.CurrentTicks);
            c[127]++;
            c[130] = sStuckQueue.MaxDepth;
        }

        static void StuckCheckDrain()
        {
            int[] c = PatchCounters();
            try
            {
                StuckCheck e;
                while (sStuckQueue.Next(Sims3.Gameplay.Utilities.SimClock.CurrentTicks, out e))
                {
                    c[131] = (int)(sStuckQueue.MaxWait * 60 / Math.Max(1L, Sims3.Gameplay.Utilities.SimClock.ConvertToTicks(1f, Sims3.Gameplay.Utilities.TimeUnit.Minutes)));
                    if (GameStates.IsWorldShuttingDown || GameStates.IsTravelling || e.Sim.HasBeenDestroyed) { c[129]++; continue; }
                    c[128]++;
                    try { e.Helper.RunOrig(e.Sim); }
                    catch (Exception) { c[129]++; } // the alarm's own caller (AlarmManager.Simulate) swallows these too
                    if (sStuckQueue.Count > 0) Simulator.Sleep(0); // one Sim per tick
                }
            }
            finally { sStuckTask = ObjectGuid.InvalidObjectGuid; }
        }
    }
}
