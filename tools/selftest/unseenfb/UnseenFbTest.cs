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

        // §5.6 C-site push cadence and verdict cache
        Check(UnseenFallbackCore.SitesSleepTicks(0, 20, 500) == UnseenFallbackCore.kSleepDefault, "no rate: default sleep");
        Check(UnseenFallbackCore.SitesSleepTicks(8, 20, 500) == 200, "speed 3: 8 ticks / 20 ms -> 200 ticks per 500 ms");
        Check(UnseenFallbackCore.SitesSleepTicks(1, 1000, 500) == 1, "slow clock floors at 1");
        Check(UnseenFallbackCore.SitesSleepTicks(1000, 1, 500) == UnseenFallbackCore.kSleepMax, "cap");
        Check(UnseenFallbackCore.SitesSleepTicks(8, 20, -5) == 1, "late push: shortest sleep");
        Check(!UnseenFallbackCore.SitesRecheck(0.5f, 100, false) && UnseenFallbackCore.SitesRecheck(1f, 100, false) && UnseenFallbackCore.SitesRecheck(0f, UnseenFallbackCore.kRecheckMs, false) && UnseenFallbackCore.SitesRecheck(0f, -1, false), "recheck rule");
        Check(UnseenFallbackCore.SitesRecheck(0f, 100, true), "recheck on a new interaction or route");
        // docs/202 §5.8: per-tick budget of the push's Sim pass, every Sim covered once
        UfSitesPass sp = new UfSitesPass();
        for (int total = 0; total <= 3 * UnseenFallbackCore.kSitesPerTick + 1; total += 5)
        {
            sp.Begin(total);
            int covered = 0, wakes = 0, from;
            bool over = false, gap = false;
            while (!sp.Done)
            {
                int got = sp.Slice(out from);
                if (got > UnseenFallbackCore.kSitesPerTick || got <= 0) over = true;
                if (from != covered) gap = true;
                covered += got;
                if (++wakes > 100) break;
            }
            Check(!over && !gap && covered == total && wakes == (total + UnseenFallbackCore.kSitesPerTick - 1) / UnseenFallbackCore.kSitesPerTick, "sites pass: <= " + UnseenFallbackCore.kSitesPerTick + " Sims per wake, all " + total + " covered");
        }
        sp.Begin(0);
        int f0;
        Check(sp.Done && sp.Slice(out f0) == 0, "empty pass");

        // §5.7 E: loop detector (restart after a failed plan, same key, inside the window; flags once)
        UfLoopDetector ld = new UfLoopDetector(750);
        object i0 = new object();
        Check(!ld.Note(1, "Eat@5", i0, true, 0) && !ld.Note(1, "Eat@5", i0, false, 10), "first instance, replans: no restart");
        bool flagged = false;
        for (int i = 1; i <= UfLoopDetector.kLoopRestarts; i++) { bool f = ld.Note(1, "Eat@5", new object(), true, 10 + i); Check(f == (i == UfLoopDetector.kLoopRestarts), "flag on restart " + i); flagged |= f; }
        Check(flagged && !ld.Note(1, "Eat@5", new object(), true, 30), "flags once per streak");
        UfLoopDetector ld2 = new UfLoopDetector(750);
        ld2.Note(2, "K", new object(), false, 0);
        for (int i = 0; i < 10; i++) Check(!ld2.Note(2, "K", new object(), false, i + 1), "restarts after successes are not a loop");
        UfLoopDetector ld3 = new UfLoopDetector(750);
        ld3.Note(3, "K", new object(), true, 0);
        for (int i = 0; i < 3; i++) ld3.Note(3, "K", new object(), true, 1 + i);
        Check(!ld3.Note(3, "K", new object(), true, 800) && ld3.Restarts(3) == 0, "window end starts over");
        ld3.Note(3, "K", new object(), true, 801);
        Check(!ld3.Note(3, "Other", new object(), true, 802) && ld3.Restarts(3) == 0, "other key starts over");
        // §5.7 E: breaker scope (Sim side: unseen only; lot side: Rodney's lot rules) and the per-tick budget
        Check(UfLoopBreakCore.SimFix(true, UnseenFallbackCore.kSeen) && !UfLoopBreakCore.SimFix(false, UnseenFallbackCore.kSeen), "sim fix: live + unseen");
        foreach (int wy in new int[] { UnseenFallbackCore.kHousehold, UnseenFallbackCore.kActiveLot, UnseenFallbackCore.kOnScreen, UnseenFallbackCore.kNearViewers, UnseenFallbackCore.kHeld, UnseenFallbackCore.kSocialSeen })
            Check(!UfLoopBreakCore.SimFix(true, wy), "sim fix refused for why " + wy);
        Check(UfLoopBreakCore.LotFix(true, false, true, false, false, false), "lot fix: unseen own/community lot");
        Check(!UfLoopBreakCore.LotFix(true, false, true, false, true, false), "never a lot the active household owns");
        Check(!UfLoopBreakCore.LotFix(true, false, true, false, false, true), "never with a household member there or heading there");
        Check(!UfLoopBreakCore.LotFix(true, false, true, true, false, false), "never a lot in view");
        Check(!UfLoopBreakCore.LotFix(true, true, true, false, false, false) && !UfLoopBreakCore.LotFix(true, false, false, false, false, false) && !UfLoopBreakCore.LotFix(false, false, true, false, false, false), "world lot, other lot, out of live");
        UfTickBudget tb = new UfTickBudget(UfLoopBreakCore.kObjectsPerTick);
        int used = 0;
        for (int i = 0; i < 10; i++) if (tb.Try(100)) used++;
        Check(used == UfLoopBreakCore.kObjectsPerTick && tb.Try(101), "per-tick budget, resets next tick");

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

        // §3.6.4 lot trips: covered kinds unchanged unless unseenfbtriphold; mod subclasses only with unseenfbtrip, always held
        Check(UfPreCore.TripMode(false, false, false) == 1 && UfPreCore.TripMode(false, true, false) == 1, "covered trip: placed at once as shipped");
        Check(UfPreCore.TripMode(false, false, true) == 2 && UfPreCore.TripMode(false, true, true) == 2, "covered trip + unseenfbtriphold: held");
        Check(UfPreCore.TripMode(true, false, false) == 0 && UfPreCore.TripMode(true, false, true) == 0, "mod subclass trip off without unseenfbtrip");
        Check(UfPreCore.TripMode(true, true, false) == 2, "mod subclass trip with unseenfbtrip: held");
        Check(UfPreCore.TripScanAssembly("StudentUnionLibrary", "ts3fixes") && !UfPreCore.TripScanAssembly("ts3fixes", "ts3fixes")
            && !UfPreCore.TripScanAssembly("Sims3GameplaySystems", "ts3fixes") && !UfPreCore.TripScanAssembly("mscorlib", "ts3fixes")
            && !UfPreCore.TripScanAssembly(null, "ts3fixes"), "scan mod assemblies only");

        // docs/202 §3.4.2 finish the walk (UfFinishCore)
        Check(UfFinishCore.HoldMinutes(0f) == 0f && UfFinishCore.HoldMinutes(-3f) == 0f && UfFinishCore.HoldMinutes(float.NaN) == 0f, "finish: no length = finish at once");
        Check(System.Math.Abs(UfFinishCore.HoldMinutes(50f) - 50f / UfPreCore.kWalkMPerSimMin) < 0.001f, "finish: remaining length / speed, no detour");
        Check(UfFinishCore.HoldMinutes(1e6f) == UfPreCore.kMaxHoldMin, "finish: capped at 2 sim-hours");
        Check(UfFinishCore.RouteShape(true, false, 0, 1, UfFinishCore.kSimPath, false), "finish: last walking subpath");
        Check(!UfFinishCore.RouteShape(false, false, 0, 1, UfFinishCore.kSimPath, false), "finish: vehicle / boat / UFO subclass");
        Check(!UfFinishCore.RouteShape(true, true, 0, 1, UfFinishCore.kSimPath, false), "finish: already killed");
        Check(!UfFinishCore.RouteShape(true, false, 1, 1, UfFinishCore.kSimPath, false), "finish: slaves");
        Check(!UfFinishCore.RouteShape(true, false, 0, 2, UfFinishCore.kSimPath, false), "finish: a portal / door subpath left");
        Check(!UfFinishCore.RouteShape(true, false, 0, 0, UfFinishCore.kSimPath, false), "finish: no path");
        Check(!UfFinishCore.RouteShape(true, false, 0, 1, 1348865362u, false), "finish: portal path");
        Check(!UfFinishCore.RouteShape(true, false, 0, 1, UfFinishCore.kSimPath, true), "finish: an action queued after");
        Check(!UfFinishCore.Denied(UfFinishCore.kTerrainGoHere) && UfPreCore.Denied(UfFinishCore.kTerrainGoHere), "finish: a townie's Go Here allowed (D denies it)");
        Check(UfFinishCore.Denied("Sims3.Gameplay.Core.Terrain+GoHereWith") && UfFinishCore.Denied("NRaas.GoHereSpace.Interactions.GoHereEx")
            && UfFinishCore.Denied("Sims3.Gameplay.Actors.Sim+GoJogging") && !UfFinishCore.Denied("Sims3.Gameplay.Objects.Seating.Chair+Sit"), "finish: deny list otherwise D's");
        Check(UfFinishCore.Step(true, true, true, 10, 20) == UfFinishCore.kKeep, "finish: hold until the walk time");
        Check(UfFinishCore.Step(true, true, true, 20, 20) == UfFinishCore.kFinish, "finish: walk time over");
        Check(UfFinishCore.Step(false, true, true, 30, 20) == UfFinishCore.kGiveUp, "finish: native released (seen, expired)");
        Check(UfFinishCore.Step(true, false, true, 30, 20) == UfFinishCore.kGiveUp, "finish: route action changed");
        Check(UfFinishCore.Step(true, true, false, 30, 20) == UfFinishCore.kGiveUp, "finish: seen at the finish check");

        if (fails > 0) { Console.WriteLine("unseenfb selftest FAILED: " + fails); Environment.Exit(1); }
        Console.WriteLine("unseenfb selftest ok");
    }
}
