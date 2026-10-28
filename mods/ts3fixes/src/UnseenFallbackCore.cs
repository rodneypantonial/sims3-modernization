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

        // docs/202 §5.6 C-site push (UnseenFallbackSites.cs). Simulator.Sleep counts SimClock ticks (37.5 per sim-minute), and at
        // speed 3 the clock moves ~8 ticks per iteration, so a fixed Sleep(5) woke every iteration (~42 pushes/s in v020-natural
        // IP). The loop pushes every kPushMs of wall time and sizes its sleep from the tick rate it just saw.
        public const int kPushMs = 500, kRecheckMs = 2000;
        public const uint kSleepDefault = 5, kSleepMax = 2000;
        public static uint SitesSleepTicks(long dTicks, int dMs, int wantMs)
        {
            if (dTicks <= 0 || dMs <= 0) return kSleepDefault;  // no rate yet (first wake, paused clock)
            long t = dTicks * System.Math.Max(wantMs, 1) / dMs;
            return t < 1 ? 1u : t > kSleepMax ? kSleepMax : (uint)t;
        }
        // a cached verdict is reused unless the Sim moved >= 1 m, the camera moved >= 1 m (cache cleared), or it is kRecheckMs old;
        // ts3native re-tests frustum, reach and EA's on-screen byte at the wait itself, so a stale "unseen" never acts on a seen Sim
        // docs/202 §5.8: also when the Sim started routing or changed interaction since the cached verdict
        public static bool SitesRecheck(float movedSq, int ageMs, bool changed) { return changed || movedSq >= 1f || ageMs < 0 || ageMs >= kRecheckMs; }

        // docs/202 §5.8 (Rodney's per-Sim loop rule): a push's list is built over several ticks, at most kSitesPerTick Sims per wake
        // from one snapshot (round-robin cursor), so no tick pays for every Sim in the world (the 0.36.104 push: 2.7-5 ms in one tick)
        public const int kSitesPerTick = 8;
    }

    public sealed class UfSitesPass
    {
        int mNext, mCount = 0;
        public void Begin(int count) { mCount = count < 0 ? 0 : count; mNext = 0; }
        public bool Done { get { return mNext >= mCount; } }
        // this wake's slice: [from, from + return value), never more than kSitesPerTick
        public int Slice(out int from)
        {
            from = mNext;
            int n = System.Math.Min(UnseenFallbackCore.kSitesPerTick, mCount - mNext);
            if (n < 0) n = 0;
            mNext += n;
            return n;
        }
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

        // docs/202 §3.6.4 lot trips: 0 = no shortcut, 1 = place at once (the trip shortcuts as shipped), 2 = D's walk-time hold, then
        // place. Kinds the shortcuts already cover (newKind false; their own switch was checked by the caller) keep 1 unless
        // unseenfbtriphold; kinds they miss (mod subclasses that override the trip's Run) act only with unseenfbtrip, always held.
        public static int TripMode(bool newKind, bool tripOn, bool holdOn)
        {
            if (newKind) return tripOn ? 2 : 0;
            return holdOn ? 2 : 1;
        }

        // assemblies scanned for mod subclasses of the trip interactions: not the game's, not the runtime's, not ours
        static readonly string[] kGameAssemblies = { "mscorlib", "System", "System.Xml", "ScriptCore", "SimIFace", "Sims3GameplaySystems",
            "Sims3GameplayObjects", "Sims3StoreObjects", "Sims3Metadata", "UI" };
        public static bool TripScanAssembly(string name, string ours)
        {
            if (name == null || name == ours) return false;
            foreach (string g in kGameAssemblies) if (name == g) return false;
            return true;
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

    // docs/202 §3.4.2 finish the walk (UnseenFallbackFinish.cs, switch unseenfbfinish): pure rules, offline-tested
    public static class UfFinishCore
    {
        public const uint kSimPath = 3328183143u;  // PathType.SimPath: an ordinary walk (no portal, vehicle, boat, water, stroller)

        // remaining planned path length / walk speed, in sim-minutes; no detour factor (it is the path's own length); 0 = finish at
        // the next check; capped at D's 2 sim-hours (§3.7: shorter than Overwatch V1's pass spacing). NaN or negative = 0.
        public static float HoldMinutes(float remainingM)
        {
            if (!(remainingM > 0f)) return 0f;
            float m = remainingM / UfPreCore.kWalkMPerSimMin;
            return m > UfPreCore.kMaxHoldMin ? UfPreCore.kMaxHoldMin : m;
        }

        // the route can be finished by placing the Sim at its end: the game's own FollowSubPathRouteAction (not a vehicle, boat or
        // UFO subclass), not killed yet, no slave Sims, the last subpath (no portal, door or stairs left between the Sim and the end:
        // those are separate subpaths), an ordinary walking path, and no route action queued after it
        public static bool RouteShape(bool exactType, bool killed, int slaves, uint numPaths, uint pathType, bool actionAfter)
        {
            return exactType && !killed && slaves == 0 && numPaths == 1 && pathType == kSimPath && !actionAfter;
        }

        // D's deny list (interactions whose point is the walk), except a townie's plain Go Here: Rodney 2026-10-27 excludes Go Here
        // only for the player's Sims, which the scope rule (UfWhy: household, selectable) never lets through; GoHereWith and NRaas
        // GoHere stay denied (groups, its own walks)
        public const string kTerrainGoHere = "Sims3.Gameplay.Core.Terrain+GoHere";
        public static bool Denied(string fullTypeName) { return fullTypeName != kTerrainGoHere && UfPreCore.Denied(fullTypeName); }

        // a claimed hold at a check: 0 keep holding, 1 finish now, 2 give up (the Sim walks on from where it stood)
        public const int kKeep = 0, kFinish = 1, kGiveUp = 2;
        public static int Step(bool nativeHeld, bool sameAction, bool stillOk, long now, long until)
        {
            if (!nativeHeld || !sameAction || !stillOk) return kGiveUp;
            return now >= until ? kFinish : kKeep;
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
    // docs/202 §5.7 E: loop detector. A loop = the same interaction definition on the same target started again (a new
    // instance) by one Sim, after the previous instance of it had a failed plan, kLoopRestarts times inside the window (sim
    // ticks). Flags once per streak; a different key, a success-only restart or the window's end start over.
    public sealed class UfLoopDetector
    {
        public const int kLoopRestarts = 4, kMaxSims = 512;
        sealed class E { public string Key; public object Inst; public int Restarts; public long First; public bool Failed, Flagged; }
        readonly Dictionary<ulong, E> mSims = new Dictionary<ulong, E>();
        readonly long mWindow;
        public UfLoopDetector(long windowTicks) { mWindow = windowTicks; }
        public int Count { get { return mSims.Count; } }
        public int Restarts(ulong sim) { E e; return mSims.TryGetValue(sim, out e) ? e.Restarts : 0; }

        // one plan of sim inside interaction inst (definition + target = key); true = the plan that makes it a loop
        public bool Note(ulong sim, string key, object inst, bool planFailed, long now)
        {
            E e;
            if (!mSims.TryGetValue(sim, out e))
            {
                if (mSims.Count >= kMaxSims) mSims.Clear();  // shortcut: a burst of distinct Sims starts over
                mSims[sim] = e = new E();
            }
            if (e.Key != key || now - e.First > mWindow)
            {
                e.Key = key; e.Inst = inst; e.Restarts = 0; e.First = now; e.Failed = planFailed; e.Flagged = false;
                return false;
            }
            if (ReferenceEquals(e.Inst, inst)) { e.Failed |= planFailed; return false; }
            bool count = e.Failed;  // a restart after a failure
            e.Inst = inst;
            e.Failed = planFailed;
            if (!count) return false;
            e.Restarts++;
            if (e.Restarts < kLoopRestarts || e.Flagged) return false;
            e.Flagged = true;
            return true;
        }
    }

    // docs/202 §5.7 E: loop breaker scope (Rodney 2026-10-27) and per-tick budget
    public static class UfLoopBreakCore
    {
        public const int kObjectsPerTick = 4;
        // Sim side (motives, negative moodlets): live mode and UfWhy == 0 (unseen: not the household, not selected, not on a seen
        // lot, off screen, outside the 80 m reach of the camera and the household)
        public static bool SimFix(bool live, int why) { return live && why == UnseenFallbackCore.kSeen; }
        // lot side: the Sim's own home lot or a community lot, not the world lot, not in view, not owned by the active household
        // (home, real estate, vacation home, houseboat), and no active-household member on it or heading there. Checked before
        // every step: a household Sim arriving mid-way stops the fix.
        public static bool LotFix(bool live, bool worldLot, bool simHomeOrCommunity, bool lotSeen, bool ownedByActive, bool activeMemberThere)
        {
            return live && !worldLot && simHomeOrCommunity && !lotSeen && !ownedByActive && !activeMemberThere;
        }
    }

    // at most PerTick steps per sim tick (a tick = one SimClock value)
    public sealed class UfTickBudget
    {
        readonly int mPerTick;
        long mTick = long.MinValue;
        int mUsed;
        public UfTickBudget(int perTick) { mPerTick = perTick; }
        public bool Try(long tick)
        {
            if (tick != mTick) { mTick = tick; mUsed = 0; }
            if (mUsed >= mPerTick) return false;
            mUsed++;
            return true;
        }
    }
}
