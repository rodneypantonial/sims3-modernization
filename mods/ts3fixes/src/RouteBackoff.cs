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
            // A10 (docs/87 §5): the Replan family through the same async handshake; ts3native [RouteAsync] Kinds decides
            Patch(typeof(ScriptCore.Route), "Replan", typeof(RouteBackoffHook), "ReplanHook", "ReplanOrig");
            Patch(typeof(ScriptCore.Route), "PlanFromPoint", typeof(RouteBackoffHook), "PlanFromPointHook", "PlanFromPointOrig");
            Patch(typeof(ScriptCore.Route), "ReplanFromPoint", typeof(RouteBackoffHook), "ReplanFromPointHook", "ReplanFromPointOrig");
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
                IScriptProxy proxy = Follower;
                Sims3.Gameplay.Actors.Sim sim = proxy != null ? proxy.Target as Sims3.Gameplay.Actors.Sim : null;
                if (!ShortcutOn("backoff")) return PlanAsync(sim);
                Vector3 dest = GetApproximateOriginalDestination();
                // non-Sim routers, no destination yet, or a short hop (turn in place, fail-path near the Sim): untouched
                if (sim == null || dest == Vector3.Origin || dest == Vector3.Invalid || (sim.Position - dest).Length() < kBackoffMinDistance)
                    return PlanAsync(sim);
                BackoffKey key = new BackoffKey(proxy.ObjectId.Value, dest);
                long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
                BackoffEntry e;
                // From <= now guards against a clock that went back (another save loaded)
                if (sBackoff.TryGetValue(key, out e) && e.From <= now && now < e.Until && BackoffEligible(sim))
                {
                    c[26]++;
                    return FailFast(e.Type);
                }
                RoutePlanResult r = PlanAsync(sim);
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

            // Async route plan (docs/18 "Async plan", ts3native [RouteAsync]): when the plan comes from the Sim's own task and
            // that task may yield, ts3native searches on its worker thread while this task sleeps one tick at a time; the sim
            // thread keeps running everything else. Handshake through GetOption on RouteOption bit 58, which has no name and
            // is never set by the game: without ts3native (or with it off) START answers false and this is one extra call.
            // The original Plan then runs once, as without us, and its internal call hands over the worker's result.
            // CheckYieldingContext is not enough: when this plan runs inside a native->managed call during the task, a
            // yield aborts the game (interp.c:3595 `pState->yieldReturn'), so ts3native refuses START there (MayYield).
            // Switch "asyncroute" (default on; ts3native decides). Counters: [56] started, [57] yields, [58] poll cap.
            // H6 M1 (docs/60 §2.4): live mode only, no travel, no world shutdown (ts3native also refuses START then).
            RoutePlanResult PlanAsync(Sims3.Gameplay.Actors.Sim sim)
            {
                if (!AsyncEligible(sim, "asyncroute") || !GetOption(kAsyncStart))
                    return PlanOrig();
                try
                {
                    AsyncWait();
                    return PlanOrig();
                }
                finally
                {
                    GetOption2(kAsyncCancel); // disarm, also when the Sim is reset while sleeping
                }
            }

            bool AsyncEligible(Sims3.Gameplay.Actors.Sim sim, string shortcut)
            {
                return sim != null && ShortcutOn(shortcut) && Simulator.CurrentTask == sim.ObjectId
                    && GameStates.IsLiveState && !GameStates.IsTravelling && !GameStates.IsWorldShuttingDown
                    && Simulator.CheckYieldingContext(false);
            }

            // after a START answered yes: sleep a tick at a time until READY
            void AsyncWait()
            {
                int[] c = PatchCounters();
                c[56]++;
                for (int i = 0; !GetOption2(kAsyncReady); i++)
                {
                    // shortcut: a fixed cap (~10-20 s real time); past it the plan waits natively and is then made
                    // again synchronously. Upgrade to a time-based cap if long searches ever reach it.
                    if (i >= kAsyncMaxPolls) { c[58]++; break; }
                    c[57]++;
                    Simulator.Sleep(0);
                }
            }

            // A10 (docs/87 §5, ts3native [RouteAsync] Kinds): Replan (kind 1), PlanFromPoint (2) and ReplanFromPoint (3) the
            // same way. START is GetOption2 with bits 58 + 59 and the kind in bits 56-57; kinds 2-3 first send the point,
            // one component per call (bit 60, component in bits 56-57, float bits in the low dword). The game has no
            // RouteOption2 names up there, so without ts3native START answers false (the point calls are reads whose
            // answer is ignored). ts3native refuses a route that is being followed. Switch "asyncreplan" (default on).
            public RoutePlanResult ReplanHook()
            {
                if (!AsyncKindStart(1, Vector3.Origin)) return ReplanOrig();
                try { AsyncWait(); return ReplanOrig(); }
                finally { GetOption2(kAsyncCancel); }
            }
            public RoutePlanResult ReplanOrig() { return default(RoutePlanResult); }

            public RoutePlanResult PlanFromPointHook(Vector3 startPosition)
            {
                if (!AsyncKindStart(2, startPosition)) return PlanFromPointOrig(startPosition);
                try { AsyncWait(); return PlanFromPointOrig(startPosition); }
                finally { GetOption2(kAsyncCancel); }
            }
            public RoutePlanResult PlanFromPointOrig(Vector3 startPosition) { return default(RoutePlanResult); }

            public RoutePlanResult ReplanFromPointHook(Vector3 fromPt)
            {
                if (!AsyncKindStart(3, fromPt)) return ReplanFromPointOrig(fromPt);
                try { AsyncWait(); return ReplanFromPointOrig(fromPt); }
                finally { GetOption2(kAsyncCancel); }
            }
            public RoutePlanResult ReplanFromPointOrig(Vector3 fromPt) { return default(RoutePlanResult); }

            bool AsyncKindStart(int kind, Vector3 p)
            {
                IScriptProxy proxy = Follower;
                if (!AsyncEligible(proxy != null ? proxy.Target as Sims3.Gameplay.Actors.Sim : null, "asyncreplan")) return false;
                if (kind >= 2) { AsyncPoint(0, p.x); AsyncPoint(1, p.y); AsyncPoint(2, p.z); }
                return GetOption2((Sims3.SimIFace.Route.RouteOption2)(kArStart | (ulong)kind << 56));
            }
            void AsyncPoint(int i, float f)
            {
                FloatBits b = default(FloatBits);
                b.F = f;
                GetOption2((Sims3.SimIFace.Route.RouteOption2)(kArPoint | (ulong)i << 56 | b.U));
            }

            const Sims3.SimIFace.Route.RouteOption kAsyncStart = (Sims3.SimIFace.Route.RouteOption)0x0400000000000000L;
            const Sims3.SimIFace.Route.RouteOption2 kAsyncReady = (Sims3.SimIFace.Route.RouteOption2)0x0400000000000000L;
            const Sims3.SimIFace.Route.RouteOption2 kAsyncCancel = (Sims3.SimIFace.Route.RouteOption2)0x4400000000000000L;
            const ulong kArStart = 0x0C00000000000000UL, kArPoint = 0x1C00000000000000UL;
            const int kAsyncMaxPolls = 1000;

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

        [StructLayout(LayoutKind.Explicit)]
        struct FloatBits { [FieldOffset(0)] public float F; [FieldOffset(0)] public uint U; }

        // Whether a Simulator.Sleep here is safe. CheckYieldingContext only tests for a current task; EA's yield also needs
        // the running code to be the task's own top-level interpreter state, not a native->managed call made during the
        // task, or the game aborts on "interp.c:3595 `pState->yieldReturn' not met" (docs/18 "Crash: yield in a nested
        // call"). ts3native answers that through GetOption2 bit 58|61 (true = may not yield) on any Route, here one with
        // handle 0. Without ts3native the game answers false (handle 0 has no route), so the old rule applies.
        static bool MayYield()
        {
            return Simulator.CheckYieldingContext(false) && !sYieldProbe.GetOption2(kNoYield);
        }
        static readonly ScriptCore.Route sYieldProbe = new ScriptCore.Route();
        const Sims3.SimIFace.Route.RouteOption2 kNoYield = (Sims3.SimIFace.Route.RouteOption2)0x2400000000000000L;

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
