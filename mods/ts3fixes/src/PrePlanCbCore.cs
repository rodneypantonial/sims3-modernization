using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent parts of preplancb (RouteBackoff.cs, docs/185 §5), checked offline by tools/selftest/preplancb.
    public static class PrePlanCbCore
    {
        // routeType EA's ScriptCore.Route passes to AboutToPlanCallback per async kind: Plan (0) and PlanFromPoint (2) "Plan",
        // Replan (1) "Replan", ReplanFromPoint (3) "ReplanFromPoint" (IL of the four methods, docs/185 §5).
        public static string Name(int kind)
        {
            return kind == 1 ? "Replan" : kind == 3 ? "ReplanFromPoint" : "Plan";
        }

        // after the job's wait: the door list the job's door answers filled (null = nothing to keep)
        public static List<T> Snapshot<T>(List<T> live)
        {
            return live != null && live.Count > 0 ? new List<T>(live) : null;
        }

        // after the original plan: put the job's doors back when the original's own AboutToPlanCallback emptied the list.
        // A list the original refilled (a re-run sync search, docs/18 SyncIc) is left as it is.
        public static bool Restore<T>(List<T> live, List<T> copy)
        {
            if (live == null || copy == null || live.Count != 0) return false;
            live.AddRange(copy);
            return true;
        }
    }
}
