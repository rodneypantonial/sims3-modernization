using System;
using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent parts of RouteSources.cs (docs/151 §4-§6), checked offline by tools/selftest/routesrc.
    public static class RouteSrcCore
    {
        // Source buckets of docs/151 §2 (V1). Index = bucket; names for the agent and the report.
        public const int kOther = 0, kLegEnd = 1, kHouseboat = 2, kWatchdog = 3, kTraffic = 4, kStandWait = 5, kStuckCheck = 6,
            kBouncer = 7, kLotVisit = 8, kSafeLocation = 9, kRabbitHole = 10, kMovingTarget = 11, kSimReplan = 12, kLane = 13,
            kLookWander = 14, kInteraction = 15, kBuckets = 16;
        public static readonly string[] BucketNames = { "other", "S1_legend", "S2_houseboat", "S3a_watchdog", "S4_traffic",
            "S5_standwait", "S6_stuckcheck", "S7_bouncer", "S8_lotvisit", "S3b_safeloc", "S9_rabbithole", "S10_movingtarget",
            "S11_simreplan", "S12_lane", "S13_lookwander", "S14_interaction" };

        // Plan IC kinds as ts3native numbers them: 0 Plan, 1 Replan, 2 PlanFromPoint, 3 ReplanFromPoint.
        public const int kPlan = 0, kReplan = 1, kPlanFromPoint = 2, kReplanFromPoint = 3;

        // Frames between the IC and its caller that say nothing about the source: the route classes themselves and our
        // pass-through hook frames (docs/151 §1). fullType = DeclaringType.FullName (nested types with '+').
        public static bool IsPassThrough(string fullType, string method)
        {
            if (fullType == null) return true;
            if (fullType.StartsWith("ScriptCore.Route") || fullType.StartsWith("Sims3.SimIFace.Route")) return true;
            if (fullType.EndsWith("+RouteBackoffHook")) return true;
            if (fullType == "Ts3Fixes.Fixes" && method != null && method.StartsWith("RouteSrc")) return true;
            if (fullType.StartsWith("Ts3Fixes.RouteSrc") || fullType.StartsWith("System.Diagnostics.")) return true;
            return method != null && method.EndsWith("Orig");
        }

        // frames = "FullType.Method" from the first frame above the pass-through run, innermost first.
        // Rules of docs/151 Reproduce: S2 within the first 6 frames, S1 = first frame base RoutingComponent.DoRoute on a
        // replan-from-point, S3a within 5 frames, then first-frame matches in table order.
        public static int Classify(IList<string> frames, int kind)
        {
            if (frames == null || frames.Count == 0) return kOther;
            for (int i = 0; i < frames.Count && i < 6; i++)
                if (Has(frames[i], "CheckAndUpdateForHouseboats") || Has(frames[i], "HouseboatThroughGangway")) return kHouseboat;
            string f = frames[0];
            if (kind == kReplanFromPoint && Has(f, "ObjectComponents.RoutingComponent.DoRoute")) return kLegEnd;
            for (int i = 0; i < frames.Count && i < 5; i++)
                if (Has(frames[i], "StuckWatchdog.CheckIdle")) return kWatchdog;
            if (Has(f, "LiteDriver.Simulate") || Has(f, "DriveAwayInNpcCar")) return kTraffic;
            if (Has(f, "StandAndWaitCycleHandler") || Has(f, "ObstacleEncounteredRouteInterruptAction.PerformAction")
                || Has(f, "DestinationObstructedRouteInterruptAction.PerformAction")) return kStandWait;
            if (Has(f, "RunStuckCheck")) return kStuckCheck;
            if (Has(f, "WatchDoor.LoopDel") || Has(f, "WatchDoor.Run")) return kBouncer;
            // RouteToSlot sits between the bouncer frames and the IC (docs/151 §3 S7)
            if (Has(f, "RoutingComponent.RouteToSlot"))
                for (int i = 1; i < frames.Count && i < 4; i++) if (Has(frames[i], "+WatchDoor.")) return kBouncer;
            if (Has(f, "Lot.PlanToLotEx") || Has(f, "Lot.PlanRouteIntoLot") || Has(f, "Lot.RouteToDoor") || Has(f, "Lot.PlanToLot")
                || Has(f, "PlanToMailbox")) return kLotVisit;
            if (Has(f, "IsPointInLotSafelyRoutable") || Has(f, "IsInSafeLocation") || Has(f, "SafeSpot") || Has(f, "Instantiat"))
                return kSafeLocation;
            if (Has(f, "RabbitHole.RouteOutside") || Has(f, "RouteNearEntranceAndEnterRabbitHole") || Has(f, "AnimateEnterRabbitHole"))
                return kRabbitHole;
            if (Has(f, "ChangeDestinationRouteInterruptAction")) return kMovingTarget;
            if (Has(f, "SimRoutingComponent.DoRoute") || Has(f, "CreateGoHereWithInteractionInstance")) return kSimReplan;
            if (Has(f, "PortalComponent.GetPathToLane") || Has(f, "PortalComponent.GetPathThroughLane")) return kLane;
            if (Has(f, "ViewObjects.CreateRouteToObject") || Has(f, "Sim.Wander") || Has(f, "PlanRouteTurnToFace") || Has(f, "CreateCircularPath"))
                return kLookWander;
            return kInteraction;
        }

        static bool Has(string s, string part) { return s != null && s.IndexOf(part, StringComparison.Ordinal) >= 0; }

        // V3 / D1 distance bands, Sim -> next path start: 0 = < 0.5 m (D1 may skip), 1 = < 2 m, 2 = more.
        public static int DistanceBand(float dx, float dz)
        {
            float d2 = dx * dx + dz * dz;
            return d2 < kLegSkipXZ * kLegSkipXZ ? 0 : d2 < 4f ? 1 : 2;
        }

        public const float kLegSkipXZ = 0.5f, kLegSkipY = 0.5f;

        // D1 (legreplanskip): the game's leg-end replan may be dropped when the action would have returned plain
        // ContinueAndPopPath without the flag (so DoRoute pops the leg and pushes the planned remainder, the same code path
        // as after a successful replan) and the remainder still starts where the Sim stands:
        //  - the result is ContinueAndPopPathAndReplan only because of ReplanWhenFinished (not a portal mbReplan),
        //  - the action was not forcibly replanned (DoRoute would turn a plain pop into ContinueAndFollowPath),
        //  - more than one path left and not a loop (else a plain pop ends the route),
        //  - the plan succeeded, the route's follower was the Sim before the action (the remainder was planned with its
        //    profile, not the boat's), the Sim has no routing parent and is not swimming (DoRoute replans from Position, not
        //    PositionOnFloor, and the get-out into water changes posture),
        //  - next path start within 0.5 m (XZ) and 0.5 m (height) of the Sim's floor position.
        public static bool LegSkipEligible(bool replanResult, bool replanWhenFinished, bool forciblyReplanned, uint numPaths, bool isLoop,
            bool planOk, bool followerWasSim, bool simParented, bool swimming)
        {
            return replanResult && replanWhenFinished && !forciblyReplanned && numPaths > 1 && !isLoop && planOk && followerWasSim
                && !simParented && !swimming;
        }

        public static bool LegSkipNear(float dx, float dy, float dz)
        {
            return DistanceBand(dx, dz) == 0 && Math.Abs(dy) < kLegSkipY;
        }

        // D3 (bouncerskip): the bouncer stands on RoutingSlot_2 (0.1 m, same height within 0.25 m) and faces the slot's
        // forward within ~10 degrees (dot of the XZ unit vectors >= 0.985). A bouncer pushed off by a crowd is > 0.1 m away
        // and routes back at its next loop, as in the game.
        public static bool AtSlot(float dx, float dy, float dz, float fwdDot)
        {
            return dx * dx + dz * dz < kSlotXZ * kSlotXZ && Math.Abs(dy) < 0.25f && fwdDot >= kSlotFacing;
        }
        public const float kSlotXZ = 0.1f, kSlotFacing = 0.985f;

        // D5 (idlememo): cap of watchdog routability checks per pass with the switch on (game-free: RouteFixes.cs uses it).
        public const int kIdleChecksCapped = 4;
    }

    // V2: the last plan per follower, 64 entries round-robin. A repeat = same follower, same kind, goal within 0.25 m,
    // within 10 s; band 0 = < 0.5 s, 1 = < 2 s, 2 = < 10 s; split by the previous plan's result.
    public class RepeatRing
    {
        struct Entry { public ulong Id; public int Kind; public float X, Y, Z; public uint AtMs; public bool Ok, Used; }
        readonly Entry[] mE = new Entry[64];
        int mNext;

        // Returns -1 (no repeat) or band*2 + (previous ok ? 0 : 1); then records this plan's result.
        public int Note(ulong id, int kind, float x, float y, float z, uint nowMs, bool ok)
        {
            int slot = -1, res = -1;
            for (int i = 0; i < mE.Length; i++) if (mE[i].Used && mE[i].Id == id) { slot = i; break; }
            if (slot >= 0)
            {
                Entry e = mE[slot];
                uint dt = unchecked(nowMs - e.AtMs);
                float dx = x - e.X, dy = y - e.Y, dz = z - e.Z;
                if (e.Kind == kind && dx * dx + dy * dy + dz * dz <= 0.0625f && dt < 10000)
                    res = (dt < 500 ? 0 : dt < 2000 ? 1 : 2) * 2 + (e.Ok ? 0 : 1);
            }
            else { slot = mNext; mNext = (mNext + 1) % mE.Length; }
            mE[slot] = new Entry { Id = id, Kind = kind, X = x, Y = y, Z = z, AtMs = nowMs, Ok = ok, Used = true };
            return res;
        }
    }

    // D5 / V7: the last successful plan per Sim (start spot, sim time). A watchdog idle check may be skipped when the Sim
    // planned successfully from within 1 m of where it stands since it went still. Bounded: cleared at 1024 Sims.
    public class PlanOkMemo
    {
        struct Entry { public float X, Y, Z; public long At; }
        readonly Dictionary<ulong, Entry> mE = new Dictionary<ulong, Entry>();
        public int Count { get { return mE.Count; } }

        public void Note(ulong id, float x, float y, float z, long at)
        {
            if (mE.Count >= 1024 && !mE.ContainsKey(id)) mE.Clear();
            mE[id] = new Entry { X = x, Y = y, Z = z, At = at };
        }

        public void Forget(ulong id) { mE.Remove(id); }

        // still = sim time it went still; a plan before it, after now (clock went back) or > 1 m away does not count
        public bool Covers(ulong id, float x, float y, float z, long stillSince, long now)
        {
            Entry e;
            if (!mE.TryGetValue(id, out e) || e.At < stillSince || e.At > now) return false;
            float dx = x - e.X, dy = y - e.Y, dz = z - e.Z;
            return dx * dx + dy * dy + dz * dz <= 1f;
        }
    }
}
