using System;
using System.Reflection;
using MonoPatcherLib;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;

// TEST ONLY (docs/94 §20 job 17, bp-snap-mod-monopatcher): a MonoPatcher [TypePatch] on Lot.OnPortalPathPlan that
// behaves like the game's method and counts calls, so the snapshot mod guard (docs/104, docs/107 §2) sees a real
// TypePatch on a routing callback. Never shipped, never part of the player install.
namespace Ts3PortalPlanTest
{
    // MonoPatcher creates [Plugin] classes at its start and then runs PatchAll on this assembly (no tuning XML needed).
    [Plugin]
    public class Main
    {
        public Main() { }
    }

    // Read with the agent: reflect_call Ts3PortalPlanTest.PortalPlanCounts.Read
    public static class PortalPlanCounts
    {
        // shortcut: plain ++ (the game's mscorlib has no Interlocked); a call off the sim thread may lose a count, fine for "runs at all"
        public static int Calls, WithListeners, Locked, Discouraged, Errors;
        internal static FieldInfo sHandler;

        public static string Read()
        {
            return "calls=" + Calls + " listeners=" + WithListeners + " locked=" + Locked + " discouraged=" + Discouraged + " errors=" + Errors;
        }
    }

    // Every method declared here replaces the same-named, same-signature method of Lot (docs/107 §2.1), so keep only
    // OnPortalPathPlan in this class. `this` is the Lot at run time.
    [TypePatch(typeof(Lot))]
    public class LotPortalPlanPatch
    {
        public PortalLockedCallbackResults OnPortalPathPlan(Sim actor, GameObject portalObj, int roomFrom, int roomTo, bool initialLockedSetting)
        {
            PortalPlanCounts.Calls++;
            Lot lot = (Lot)(this as object);
            if (PortalPlanCounts.sHandler == null)
                PortalPlanCounts.sHandler = typeof(Lot).GetField("mOnPortalPathPlanHandler", BindingFlags.Instance | BindingFlags.NonPublic);
            PortalPathPlanHandler listeners = PortalPlanCounts.sHandler.GetValue(lot) as PortalPathPlanHandler;
            if (listeners == null) return PortalLockedCallbackResults.NoListeners;

            PortalPlanCounts.WithListeners++;
            PortalPlanEventArgs e = new PortalPlanEventArgs(actor, portalObj, lot, roomFrom, roomTo, initialLockedSetting);
            // each listener on its own, a throwing listener is skipped (as the game does)
            foreach (Delegate d in listeners.GetInvocationList())
            {
                try { ((PortalPathPlanHandler)d)(e); }
                catch (Exception) { PortalPlanCounts.Errors++; }
            }
            if (e.Locked) { PortalPlanCounts.Locked++; return PortalLockedCallbackResults.PortalLocked; }
            if (e.Discouraged) { PortalPlanCounts.Discouraged++; return PortalLockedCallbackResults.PortalDiscouraged; }
            return PortalLockedCallbackResults.PortalUnlocked;
        }
    }
}
