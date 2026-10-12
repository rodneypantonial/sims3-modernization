using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using Sims3.Gameplay;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Abstracts;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    public static partial class Fixes
    {
        static void ApplyRouteBackoff()
        {
            // ScriptCore.Route.Plan is the one funnel for every PlanTo* helper (Sims3.SimIFace.Route) and is not overloaded
            Patch(typeof(ScriptCore.Route), "Plan", typeof(RouteBackoffHook), "PlanHook", "PlanOrig");
        }

        // Route-failure backoff (docs/11 recommendation 3): a failed plan expands every reachable node, then runs a native
        // obstacle replan and 4-6 C# fail-path plans. When an unseen townie's plan to a destination fails, its next plans to
        // the same destination (1 m cell) fail at once for kBackoffMinutes, with no blocker, so callers take their normal
        // failure branch and DoRouteFailureBehavior skips its fail-path plans. Successes are never cached; a transient
        // failure (blocked by a Sim or vehicle) is not recorded. Counters: [24] plans seen, [25] failures recorded, [26] skipped.
        public class RouteBackoffHook : ScriptCore.Route
        {
            public RoutePlanResult PlanHook()
            {
                RoutePlanResult r = PlanBackoff();
                NotePlanResult(Follower, r.mType == RoutePlanResultType.Succeeded); // the stuck watchdog's route-fail count (RouteFixes.cs)
                return r;
            }

            RoutePlanResult PlanBackoff()
            {
                int[] c = PatchCounters();
                c[24]++;
                if (!ShortcutOn("backoff")) return PlanOrig();
                IScriptProxy proxy = Follower;
                Sims3.Gameplay.Actors.Sim sim = proxy != null ? proxy.Target as Sims3.Gameplay.Actors.Sim : null;
                Vector3 dest = GetApproximateOriginalDestination();
                // non-Sim routers, no destination yet, or a short hop (turn in place, fail-path near the Sim): untouched
                if (sim == null || dest == Vector3.Origin || dest == Vector3.Invalid || (sim.Position - dest).Length() < kBackoffMinDistance)
                    return PlanOrig();
                BackoffKey key = new BackoffKey(proxy.ObjectId.Value, dest);
                long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
                BackoffEntry e;
                // From <= now guards against a clock that went back (another save loaded)
                if (sBackoff.TryGetValue(key, out e) && e.From <= now && now < e.Until && BackoffEligible(sim))
                {
                    c[26]++;
                    return FailFast(e.Type);
                }
                RoutePlanResult r = PlanOrig();
                if ((r.mType == RoutePlanResultType.FailedBecauseNoPathToDestination) // not plain Failed: ts3native's route cache returns that on a hit (docs/15)
                    && r.mBlockerType != RouteBlockerType.Sim && r.mBlockerType != RouteBlockerType.Vehicle && BackoffEligible(sim))
                {
                    if (sBackoff.Count >= kMaxBackoffEntries) PruneBackoff(now);
                    e.From = now;
                    e.Until = now + kBackoffTicks;
                    e.Type = r.mType;
                    sBackoff[key] = e;
                    c[25]++;
                }
                return r;
            }
            public RoutePlanResult PlanOrig() { return default(RoutePlanResult); } // receives ScriptCore.Route.Plan's original header

            // What Plan leaves after a real failure, minus the blocker (so no fail-path plans): empty paths, invalid start points.
            RoutePlanResult FailFast(RoutePlanResultType type)
            {
                ClearPaths();
                RoutePlanResult res = default(RoutePlanResult);
                res.mType = type;
                PlanResult = res;
                if (sStartFields == null)
                {
                    const BindingFlags f = BindingFlags.Instance | BindingFlags.NonPublic;
                    Type t = typeof(ScriptCore.Route);
                    sStartFields = new FieldInfo[] { t.GetField("mOriginalStartPoint", f), t.GetField("mCurrentStartPoint", f), t.GetField("mPlanStartPoint", f) };
                }
                foreach (FieldInfo fi in sStartFields)
                    if (fi != null) fi.SetValue(this, Vector3.Invalid);
                return res;
            }
            static FieldInfo[] sStartFields;
        }

        // Unseen townie on foot, not a pet or horse (their routes carry their own follower/owner logic).
        static bool BackoffEligible(Sims3.Gameplay.Actors.Sim sim)
        {
            return sim.SimDescription != null && !sim.SimDescription.IsPet && TownieUnseen(sim, null);
        }

        // Drops expired entries; if all are live (a burst of distinct failures), starts over rather than grow.
        static void PruneBackoff(long now)
        {
            List<BackoffKey> dead = new List<BackoffKey>();
            foreach (KeyValuePair<BackoffKey, BackoffEntry> kv in sBackoff)
                if (now >= kv.Value.Until || kv.Value.From > now) dead.Add(kv.Key);
            foreach (BackoffKey k in dead) sBackoff.Remove(k);
            if (sBackoff.Count >= kMaxBackoffEntries) sBackoff.Clear();
        }

        const float kBackoffMinutes = 30f, kBackoffMinDistance = 5f;
        const int kMaxBackoffEntries = 2048;
        static readonly long kBackoffTicks = Sims3.Gameplay.Utilities.SimClock.ConvertToTicks(kBackoffMinutes, Sims3.Gameplay.Utilities.TimeUnit.Minutes);
        static readonly Dictionary<BackoffKey, BackoffEntry> sBackoff = new Dictionary<BackoffKey, BackoffEntry>(new BackoffKeyComparer());

        struct BackoffEntry { public long From, Until; public RoutePlanResultType Type; }

        // Follower object id plus the destination's 1 m cell (x, height, z).
        struct BackoffKey
        {
            public readonly ulong Sim;
            public readonly int X, Y, Z;
            public BackoffKey(ulong sim, Vector3 p)
            {
                Sim = sim;
                X = (int)Math.Floor(p.x); Y = (int)Math.Floor(p.y); Z = (int)Math.Floor(p.z);
            }
        }

        // explicit comparer: the default one boxes structs on every lookup in this Mono
        class BackoffKeyComparer : IEqualityComparer<BackoffKey>
        {
            public bool Equals(BackoffKey a, BackoffKey b) { return a.Sim == b.Sim && a.X == b.X && a.Y == b.Y && a.Z == b.Z; }
            public int GetHashCode(BackoffKey k) { return k.Sim.GetHashCode() ^ (k.X * 73856093) ^ (k.Y * 19349663) ^ (k.Z * 83492791); }
        }
    }
}
