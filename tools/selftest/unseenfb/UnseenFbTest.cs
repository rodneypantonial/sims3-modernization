// Offline self-check of mods/ts3fixes/src/UnseenFallbackCore.cs (docs/202 unseen-Sim fallback: scope rule order, per-Sim
// no-retry window and cut cap).
using System;
using Ts3Fixes;

static class UnseenFbTest
{
    static int fails;
    static void Check(bool ok, string what) { if (!ok) { fails++; Console.WriteLine("FAIL " + what); } }

    static void Main()
    {
        // scope rule: only all-clear is unseen; each test wins in the doc's order
        Check(UnseenFallbackCore.Why(true, true, false, false, false, true, false, false) == 0, "unseen");
        Check(UnseenFallbackCore.Why(false, true, false, false, false, true, false, false) == UnseenFallbackCore.kNotLive, "not live mode");
        Check(UnseenFallbackCore.Why(false, false, true, true, true, false, true, true) == UnseenFallbackCore.kNotLive, "not live first");
        Check(UnseenFallbackCore.Why(true, false, true, false, false, true, false, false) == UnseenFallbackCore.kGone, "gone");
        Check(UnseenFallbackCore.Why(true, true, true, false, false, true, false, false) == UnseenFallbackCore.kHousehold, "household / selected");
        Check(UnseenFallbackCore.Why(true, true, false, true, false, true, false, false) == UnseenFallbackCore.kActiveLot, "active lot");
        Check(UnseenFallbackCore.Why(true, true, false, false, true, true, false, false) == UnseenFallbackCore.kOnScreen, "on screen");
        Check(UnseenFallbackCore.Why(true, true, false, false, false, false, false, false) == UnseenFallbackCore.kNearViewers, "near camera or household");
        Check(UnseenFallbackCore.Why(true, true, false, false, false, true, true, false) == UnseenFallbackCore.kHeld, "rider / carrier");
        Check(UnseenFallbackCore.Why(true, true, false, false, false, true, false, true) == UnseenFallbackCore.kSocialSeen, "with a seen Sim");
        // every non-zero input combination is seen: exhaustive over the 8 flags
        for (int m = 0; m < 256; m++)
        {
            bool live = (m & 1) != 0, exists = (m & 2) != 0, hh = (m & 4) != 0, lot = (m & 8) != 0, scr = (m & 16) != 0, far = (m & 32) != 0, held = (m & 64) != 0, soc = (m & 128) != 0;
            bool unseen = live && exists && !hh && !lot && !scr && far && !held && !soc;
            Check((UnseenFallbackCore.Why(live, exists, hh, lot, scr, far, held, soc) == 0) == unseen, "rule mask " + m);
        }
        Check(UnseenFallbackCore.Cell(-0.5f) == -1 && UnseenFallbackCore.Cell(3.99f) == 3, "cell floor");

        // limiter
        UfLimiter u = new UfLimiter();
        object r;
        Check(u.MayMark(1, 1000) && !u.NoRetry(1, 0, 0, 0, 1000, out r) && r == null, "fresh Sim");
        u.Cut(1, 5, 0, 7, 1000, "fail-A");
        Check(u.NoRetry(1, 5, 0, 7, 1000 + UfLimiter.kNoRetryMs, out r) && (string)r == "fail-A", "no-retry inside the window");
        Check(!u.NoRetry(1, 5, 0, 7, 1001 + UfLimiter.kNoRetryMs, out r), "no-retry ends");
        Check(!u.NoRetry(1, 6, 0, 7, 1100, out r), "other cell plans");
        Check(!u.NoRetry(2, 5, 0, 7, 1100, out r), "other Sim plans");
        Check(!u.NoRetry(1, 5, 0, 7, 900, out r), "clock went back");
        for (int i = 1; i < UfLimiter.kMaxCuts; i++) { Check(u.MayMark(1, 1000 + i), "may mark before cap " + i); u.Cut(1, 5, 0, 7, 1000 + i, "fail"); }
        Check(!u.MayMark(1, 2000), "capped after kMaxCuts in the window");
        Check(u.MayMark(2, 2000), "cap is per Sim");
        Check(u.MayMark(1, 1001 + UfLimiter.kWindowMs), "cap ends with the window");
        u.Cut(1, 5, 0, 7, 1001 + UfLimiter.kWindowMs, "fail-B");
        Check(u.MayMark(1, 1002 + UfLimiter.kWindowMs), "new window counts from 1");
        // wrap of Environment.TickCount
        UfLimiter w = new UfLimiter();
        w.Cut(9, 0, 0, 0, int.MaxValue - 100, "wrap");
        Check(w.NoRetry(9, 0, 0, 0, unchecked(int.MaxValue + 200), out r), "tick wrap");
        // table bound
        UfLimiter b = new UfLimiter();
        for (ulong s = 0; s < (ulong)UfLimiter.kMaxSims + 10; s++) b.Cut(s, 0, 0, 0, 5, "x");
        Check(b.Count <= UfLimiter.kMaxSims, "table bound");

        // D (unseenfbpre, docs/202 §3.5): hold time, deny list, follow-up callers, in-transit table
        Check(UfPreCore.HoldMinutes(0f) == UfPreCore.kMinHoldMin, "hold floor");
        Check(System.Math.Abs(UfPreCore.HoldMinutes(100f) - 100f * UfPreCore.kDetour / UfPreCore.kWalkMPerSimMin) < 0.001f, "hold = distance x detour / speed");
        Check(UfPreCore.HoldMinutes(5000f) == UfPreCore.kMaxHoldMin, "hold capped at 2 sim-hours");
        Check(UfPreCore.Denied("Sims3.Gameplay.Core.Terrain+GoHere") && UfPreCore.Denied("Sims3.Gameplay.Actors.Sim+GoJogging")
            && UfPreCore.Denied("NRaas.GoHereSpace.Interactions.Anything") && UfPreCore.Denied(null), "walk-is-the-point denied");
        Check(!UfPreCore.Denied("Sims3.Gameplay.Objects.Seating.ChairDining+Sit") && !UfPreCore.Denied("Sims3.Gameplay.Core.GoHome"), "ordinary interactions allowed");
        Check(UfPreCore.FollowsRoute("Sims3.Gameplay.ObjectComponents.RoutingComponent.RouteToSlot")
            && UfPreCore.FollowsRoute("Sims3.Gameplay.ObjectComponents.RoutingComponent.RouteToObjectRadialRange"), "route-then-follow helpers");
        Check(!UfPreCore.FollowsRoute("Sims3.Gameplay.ObjectComponents.RoutingComponent.RouteToDynamicObjectRadiusWithCondition")
            && !UfPreCore.FollowsRoute("Sims3.Gameplay.Core.Lot.PlanToLotEx") && !UfPreCore.FollowsRoute("Sims3.Gameplay.Actors.Sim.IsPointInLotSafelyRoutable")
            && !UfPreCore.FollowsRoute(null), "plan-only and moving-target callers refused");
        Check(UfPreCore.PlanToLotFrame("Sims3.Gameplay.Core.Lot", "PlanToLot") && UfPreCore.PlanToLotFrame("Sims3.Gameplay.Core.Lot", "PlanToLotEx")
            && !UfPreCore.PlanToLotFrame("Sims3.Gameplay.Core.Lot", "AddLotEntryPointDestinations") && !UfPreCore.PlanToLotFrame("Sims3.Gameplay.Core.LotManager", "PlanToLot")
            && !UfPreCore.PlanToLotFrame(null, "PlanToLot"), "RouteToLot: only Lot.PlanToLot(Ex) frames skipped");
        var holds = new System.Collections.Generic.Dictionary<ulong, long>();
        holds[7] = 1000;
        Check(UfPreCore.InTransit(holds, 7, 999) && UfPreCore.InTransit(holds, 7, 1000), "in transit until the end");
        Check(!UfPreCore.InTransit(holds, 7, 1001) && !UfPreCore.InTransit(holds, 8, 10) && !UfPreCore.InTransit(null, 7, 10), "past the end, other Sim, no table");

        // D-arrive / D-leave: who may leave in view, hand-off candidates
        Check(UnseenFallbackCore.SeenButMovable(UnseenFallbackCore.kActiveLot) && UnseenFallbackCore.SeenButMovable(UnseenFallbackCore.kOnScreen)
            && UnseenFallbackCore.SeenButMovable(UnseenFallbackCore.kNearViewers), "seen through the camera may leave");
        Check(!UnseenFallbackCore.SeenButMovable(UnseenFallbackCore.kHousehold) && !UnseenFallbackCore.SeenButMovable(UnseenFallbackCore.kHeld)
            && !UnseenFallbackCore.SeenButMovable(UnseenFallbackCore.kNotLive) && !UnseenFallbackCore.SeenButMovable(UnseenFallbackCore.kSocialSeen)
            && !UnseenFallbackCore.SeenButMovable(0), "household, held, social, not live, unseen: never a D-leave");
        Check(UfPreCore.HandOffSteps(15f) == 0 && UfPreCore.HandOffSteps(20f) == 1 && UfPreCore.HandOffSteps(40f) == 3, "hand-off step count");
        int n = UfPreCore.HandOffSteps(100f);
        Check(UfPreCore.HandOffAt(0) == UfPreCore.kHandOffGapM && UfPreCore.HandOffAt(n - 1) <= 100f - UfPreCore.kHandOffGapM
            && UfPreCore.HandOffAt(n) > 100f - UfPreCore.kHandOffGapM, "candidates stay kHandOffGapM from both ends");

        if (fails > 0) { Console.WriteLine("unseenfb selftest FAILED: " + fails); Environment.Exit(1); }
        Console.WriteLine("unseenfb selftest ok");
    }
}
