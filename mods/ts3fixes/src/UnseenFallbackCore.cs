using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent parts of the docs/202 unseen-Sim fallback (UnseenFallback.cs), checked offline by tools/selftest/unseenfb.
    public static class UnseenFallbackCore
    {
        // docs/202 §2: the first failing test of the scope rule, 0 = unseen (act). Order = the doc's table, so counters and
        // logs name the cheapest reason first.
        public const int kSeen = 0, kNotLive = 1, kGone = 2, kHousehold = 3, kActiveLot = 4, kOnScreen = 5, kNearViewers = 6, kHeld = 7, kSocialSeen = 8;
        public static int Why(bool live, bool exists, bool household, bool activeLot, bool onScreen, bool far, bool held, bool targetSeen)
        {
            if (!live) return kNotLive;
            if (!exists) return kGone;
            if (household) return kHousehold;
            if (activeLot) return kActiveLot;
            if (onScreen) return kOnScreen;
            if (!far) return kNearViewers;
            if (held) return kHeld;
            return targetSeen ? kSocialSeen : kSeen;
        }

        // D-leave (docs/202 §3.6): a Sim the player sees only through the camera (on the camera's / household's lot, on screen,
        // near a viewer) may start a split route; the household, held Sims, Sims with a seen social partner and out-of-live never
        public static bool SeenButMovable(int why) { return why == kActiveLot || why == kOnScreen || why == kNearViewers; }

        // 1 m destination cell, as RouteBackoff's BackoffKey
        public static int Cell(float f) { return (int)System.Math.Floor(f); }
    }

    // docs/202 §3.5 D (pre-emptive teleport): walk-time hold, interaction deny list, the "in transit" table.
    public static class UfPreCore
    {
        // shortcut: the walk speed is a constant [inferred: ~1.5-2 m/s animation at 1 sim-minute per real second at speed 1,
        // minus stops]; upgrade to the route's walk style speed once a verify run compares hold times with real walks.
        // D-arrive / D-leave hand-off candidates: every kHandOffStepM along the line, at least kHandOffGapM from both ends
        public const float kSplitMinDistM = 40f, kHandOffStepM = 10f, kHandOffGapM = 10f;
        public static int HandOffSteps(float distM) { return distM < 2f * kHandOffGapM ? 0 : (int)((distM - 2f * kHandOffGapM) / kHandOffStepM) + 1; }
        public static float HandOffAt(int i) { return kHandOffGapM + i * kHandOffStepM; }

        public const float kMinDistM = 20f, kDetour = 1.3f, kWalkMPerSimMin = 2.5f, kMinHoldMin = 1f, kMaxHoldMin = 120f;

        // straight-line distance x detour / walk speed, in sim-minutes; capped at 2 sim-hours (§3.7: shorter than Overwatch V1's
        // pass spacing, so one hold never spans two passes)
        public static float HoldMinutes(float distM)
        {
            float m = distM * kDetour / kWalkMPerSimMin;
            return m < kMinHoldMin ? kMinHoldMin : m > kMaxHoldMin ? kMaxHoldMin : m;
        }

        // interactions whose point is the walk (§3.5 (5)): never shortcut. Matched on the interaction's type full name (nested
        // Definition classes included); every NRaas interaction too (GoHere's own walks, Overwatch/ErrorTrap helpers).
        static readonly string[] kDeny = { "Jog", "Walk", "GoHere", "Stroll", "Sightsee", "Sneak", "Haunt", "Parade", "Chase", "Follow",
            "Wander", "Leash", "Patrol", "Tour", "Explore", "Flee", "Swim", "Dive", "Trick", "Mount", "Ride", "Carry", "Stalk" };
        public static bool Denied(string fullTypeName)
        {
            if (fullTypeName == null || fullTypeName.StartsWith("NRaas")) return true;
            foreach (string d in kDeny) if (fullTypeName.IndexOf(d, System.StringComparison.Ordinal) >= 0) return true;
            return false;
        }

        // D acts only when the plan's caller follows that route right after (docs/141 §2: the RoutingComponent helpers that
        // plan once and then call DoRoute on the same route [verified, decompiled]); never for plans made only to test or score
        // (definition Tests, lot picks, safe-spot checks), which must not move the Sim. frame = "FullType.Method" of the first
        // frame above the route funnels (RouteSrcCore.IsPassThrough).
        static readonly string[] kFollowers = { "RouteToSlot", "RouteToPoint", "RouteToPointRadius", "RouteToPointRadialRange", "RouteToLot",
            "RouteToObjectRadialRange", "RouteToMatrix" };
        const string kRoutingComponent = "Sims3.Gameplay.ObjectComponents.RoutingComponent.";
        public static bool FollowsRoute(string frame)
        {
            if (frame == null || !frame.StartsWith(kRoutingComponent)) return false;
            string m = frame.Substring(kRoutingComponent.Length);
            foreach (string f in kFollowers) if (m == f) return true;
            return false;
        }

        // RoutingComponent.RouteToLot plans through Lot.PlanToLot (static) -> Lot.PlanToLot / PlanToLotEx [verified, decompiled]:
        // those frames are skipped when D reads the caller, so RouteToLot is the frame FollowsRoute sees; lot trips (VisitLot.Run
        // etc. -> PlanToLotEx) then show their own Run frame and stay refused
        public static bool PlanToLotFrame(string fullType, string method)
        {
            return fullType == "Sims3.Gameplay.Core.Lot" && (method == "PlanToLot" || method == "PlanToLotEx");
        }

        // the hold table (AppDomain ts3fixes.unseenfb.holding: Sim object id -> hold end in sim ticks), read by our watchdogs and
        // the agent's sims_activity (in_transit); an entry past its end counts as not in transit (a hold that never ended = stuck)
        public static bool InTransit(Dictionary<ulong, long> holds, ulong sim, long now)
        {
            long until;
            return holds != null && holds.TryGetValue(sim, out until) && now <= until;
        }
    }

    // docs/202 §2/§3.2 per-Sim limits. Ticks are Environment.TickCount (ms, wraps; NoStartCore.Within handles that).
    //   NoRetry: a cut plan of this Sim to the same 1 m cell inside kNoRetryMs answers with the recorded result, no search.
    //   MayMark: a Sim cut kMaxCuts times inside kWindowMs is not marked again until the window ends (its plans run uncut,
    //            as without the switch), so a route the Sim must take is never failed for time forever.
    public sealed class UfLimiter
    {
        public const int kNoRetryMs = 1500, kWindowMs = 10000, kMaxCuts = 4, kMaxSims = 512;
        struct E { public int First, Cuts, CutTick, X, Y, Z; public object Result; }
        readonly Dictionary<ulong, E> mSims = new Dictionary<ulong, E>();

        public int Count { get { return mSims.Count; } }

        public bool MayMark(ulong sim, int now)
        {
            E e;
            return !mSims.TryGetValue(sim, out e) || !NoStartCore.Within(now, e.First, kWindowMs) || e.Cuts < kMaxCuts;
        }

        public bool NoRetry(ulong sim, int x, int y, int z, int now, out object result)
        {
            E e;
            result = null;
            if (!mSims.TryGetValue(sim, out e) || e.Result == null || e.X != x || e.Y != y || e.Z != z || !NoStartCore.Within(now, e.CutTick, kNoRetryMs)) return false;
            result = e.Result;
            return true;
        }

        public void Cut(ulong sim, int x, int y, int z, int now, object result)
        {
            E e;
            if (!mSims.TryGetValue(sim, out e))
            {
                if (mSims.Count >= kMaxSims) mSims.Clear(); // shortcut: a burst of distinct Sims starts over; entries are short-lived anyway
                e = new E();
                e.First = now;
            }
            else if (!NoStartCore.Within(now, e.First, kWindowMs)) { e.First = now; e.Cuts = 0; }
            e.Cuts++;
            e.CutTick = now;
            e.X = x; e.Y = y; e.Z = z;
            e.Result = result;
            mSims[sim] = e;
        }

        public void Clear() { mSims.Clear(); }
    }
}
