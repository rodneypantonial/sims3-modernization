using System;
using System.Reflection;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // NRaas Overwatch's nightly Stuck Check (docs/04 F18, docs/07 §2.2) plans a route per standing-still Sim
    // (SimEx.IsPointInLotSafelyRoutable -> Route.PlanFromPoint), all in one FunctionTask tick: ~300 ms per sim night.
    // The hook runs NRaas's own check unchanged, then yields one sim tick when the caller may yield (the alarm's
    // FunctionTask or the menu interaction), so each plan lands in its own tick. The loop, its order and every decision
    // stay NRaas's: the plan and the reset choice for a Sim happen in the same tick as before; only later Sims and the
    // other nightly cleanups run a few ticks later. Not cached: the route depends on the whole Sim (species, posture,
    // ghost buff, boat, room limits in SetRouteOptions), so another Sim's or another tick's verdict is not exact.
    // Overwatch is a separate assembly, maybe loaded after us: tried at startup, again at world load (the check only
    // runs from the nightly alarm, which starts after world load, so the method is not compiled yet).
    // Switch "overwatchstuck" (default on). Counters: [44] checks seen, [45] yields.
    public static partial class Fixes
    {
        static bool sOverwatchDone, sOwNoYieldLogged;

        internal static void ApplyOverwatchStuck(bool lastTry)
        {
            if (sOverwatchDone) return;
            Type simEx = null;
            foreach (Assembly a in AppDomain.CurrentDomain.GetAssemblies())
                if (a.GetName().Name == "NRaasOverwatch") { simEx = a.GetType("NRaas.CommonSpace.Helpers.SimEx"); break; }
            if (simEx == null)
            {
                if (lastTry) { sOverwatchDone = true; AppliedLog().Add("Overwatch SimEx: not loaded, skipped"); }
                return;
            }
            sOverwatchDone = true;
            MethodInfo m = simEx.GetMethod("IsPointInLotSafelyRoutable", BindingFlags.Static | BindingFlags.Public, null,
                new Type[] { typeof(Sim), typeof(Lot), typeof(Vector3) }, null);
            if (m == null || m.ReturnType != typeof(bool)) { AppliedLog().Add("Overwatch SimEx: signature differs, skipped"); return; }
            Patch(simEx, "IsPointInLotSafelyRoutable", typeof(OverwatchStuckHook), "RoutableHook", "RoutableOrig");
        }

        public static class OverwatchStuckHook
        {
            public static bool RoutableHook(Sim ths, Lot lot, Vector3 pos)
            {
                bool ok;
                sInOwnCheck = true; // not counted as the Sim's own route failures (watchdog route-fail case)
                try { ok = RoutableOrig(ths, lot, pos); } finally { sInOwnCheck = false; }
                int[] c = PatchCounters();
                c[44]++;
                if (!ShortcutOn("overwatchstuck")) return ok;
                if (!OverwatchSpreadCore.OurStuckYield(sOwUpstreamVersion)) return ok; // Overwatch v2+ sleeps after each check itself
                if (MayYield()) // not inside a native->managed call (RouteBackoff.cs MayYield)
                {
                    c[45]++;
                    Simulator.Sleep(0);
                }
                else if (!sOwNoYieldLogged)
                {
                    // which gate refused (docs/145 §11): EA's may-yield test 0xE53550 / yielding-disabled flag, or ts3native's root-state answer
                    sOwNoYieldLogged = true;
                    OwLog("StuckCheck plan without a yield: context " + Simulator.CheckYieldingContext(false) + ", yielding disabled " + Simulator.YieldingDisabled
                        + ", ts3native no-yield " + sYieldProbe.GetOption2(kNoYield));
                }
                return ok;
            }
            public static bool RoutableOrig(Sim ths, Lot lot, Vector3 pos) { return false; } // receives SimEx.IsPointInLotSafelyRoutable's original header
        }
    }
}
