// Usage: node tools/selftest/routesrc/run.ts  (compiles this with mods/ts3fixes/src/RouteSourcesCore.cs)
// Offline self-check of RouteSourcesCore.cs (docs/151 §6): source classification of the §3 caller stacks (V1), the
// pass-through frame rule, the V2 repeat ring, the D1 leg-end skip rule and distance bands, the D3 bouncer-at-slot
// rule and the D5 success memo. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Fixes;

static class RouteSrcTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    static int C(int kind, params string[] frames) { return RouteSrcCore.Classify(new List<string>(frames), kind); }

    static void Classify()
    {
        const string G = "Sims3.Gameplay.";
        // S1: leg-end replan from base RoutingComponent.DoRoute
        Check(C(3, G + "ObjectComponents.RoutingComponent.DoRoute", G + "Actors.SimRoutingComponent.DoSingleRoute") == RouteSrcCore.kLegEnd, "S1 leg-end");
        // a Replan (kind 1) from DoRoute is not S1
        Check(C(1, G + "ObjectComponents.RoutingComponent.DoRoute") != RouteSrcCore.kLegEnd, "S1 needs ReplanFromPoint");
        // SimRoutingComponent.DoRoute must not match the base class rule
        Check(C(3, G + "Actors.SimRoutingComponent.DoRoute") == RouteSrcCore.kSimReplan, "S11 SimRoutingComponent.DoRoute");
        // S2 beats S1: an S1 replan re-triggers the fixup, the leg plan's stack has the fixup within 6 frames
        Check(C(2, "fp.houseboatfix.Main.EnterHouseboatThroughGangway", G + "Actors.SimRoutingComponent.CheckAndUpdateForHouseboats",
            G + "Actors.SimRoutingComponent.PostPlanRouteCallback", "Sims3.SimIFace.Route+PostPlanDelegate.Invoke",
            "Ts3Fixes.Fixes+RouteBackoffHook.ReplanFromPoint", G + "ObjectComponents.RoutingComponent.DoRoute") == RouteSrcCore.kHouseboat, "S2 houseboat");
        Check(C(3, G + "Actors.SimRoutingComponent.ExitHouseboatThroughGangway") == RouteSrcCore.kHouseboat, "S2 EA exit");
        // S3a: our watchdog via SpotVerdict
        Check(C(2, "Ts3Fixes.Fixes+RoutableCacheHook.IsPointInLotSafelyRoutable", "Ts3Fixes.Fixes.SpotVerdict",
            "Ts3Fixes.Fixes+StuckWatchdog.CheckIdle", "Ts3Fixes.Fixes+StuckWatchdog.Simulate") == RouteSrcCore.kWatchdog, "S3a watchdog");
        // S3b: the same check from a reset
        Check(C(2, "Ts3Fixes.Fixes+RoutableCacheHook.IsPointInLotSafelyRoutable", G + "Actors.Sim.IsInSafeLocation",
            G + "Actors.Sim.AttemptToFindSafeLocation") == RouteSrcCore.kSafeLocation, "S3b safe location");
        Check(C(0, G + "Objects.Vehicles.LiteDriver.Simulate") == RouteSrcCore.kTraffic, "S4 LiteDriver");
        Check(C(0, G + "Objects.Vehicles.CarNpcManager.DriveAwayInNpcCar", G + "Objects.Vehicles.CarNpcWithNoDriver.DriveOffscreenAndRelease") == RouteSrcCore.kTraffic, "S4 drive-off");
        Check(C(1, G + "Routing.ObstacleEncounteredRouteInterruptAction.StandAndWaitCycleHandler") == RouteSrcCore.kStandWait, "S5");
        Check(C(0, "Ts3Fixes.Fixes+StuckCheckBusyHook.RunStuckCheck") == RouteSrcCore.kStuckCheck, "S6");
        Check(C(0, G + "ObjectComponents.RoutingComponent.RouteToSlot", G + "ObjectComponents.RoutingComponent.RouteToSlot",
            G + "Actors.Sim.RouteToSlot", G + "Objects.Miscellaneous.VelvetRopes+WatchDoor.LoopDel") == RouteSrcCore.kBouncer, "S7 bouncer via RouteToSlot");
        Check(C(0, G + "ObjectComponents.RoutingComponent.RouteToSlot", G + "Actors.Sim.RouteToSlot", "X.Chair.Sit") == RouteSrcCore.kInteraction, "RouteToSlot elsewhere = S14");
        Check(C(0, G + "Core.Lot.PlanRouteIntoLot", "Ts3Fixes.Fixes+GoHomeHook.Run") == RouteSrcCore.kLotVisit, "S8");
        Check(C(0, G + "Objects.RabbitHoles.RabbitHole.RouteOutside") == RouteSrcCore.kRabbitHole, "S9");
        Check(C(1, G + "Routing.ChangeDestinationRouteInterruptAction.PerformAction") == RouteSrcCore.kMovingTarget, "S10");
        Check(C(0, G + "ObjectComponents.PortalComponent.GetPathToLane") == RouteSrcCore.kLane, "S12");
        Check(C(0, G + "Actors.Sim.PlanRouteTurnToFace") == RouteSrcCore.kLookWander, "S13");
        Check(C(0, G + "Objects.Seating.Chair.SitDown") == RouteSrcCore.kInteraction, "S14");
        Check(C(0) == RouteSrcCore.kOther, "no frames = other");
        Check(RouteSrcCore.BucketNames.Length == RouteSrcCore.kBuckets, "one name per bucket");
    }

    static void PassThrough()
    {
        Check(RouteSrcCore.IsPassThrough("ScriptCore.Route", "Plan"), "ScriptCore.Route");
        Check(RouteSrcCore.IsPassThrough("Sims3.SimIFace.Route", "PlanToSlot"), "SimIFace.Route");
        Check(RouteSrcCore.IsPassThrough("Ts3Fixes.Fixes+RouteBackoffHook", "PlanAsync"), "our hook");
        Check(RouteSrcCore.IsPassThrough("Ts3Fixes.Fixes", "RouteSrcBegin"), "our classifier");
        Check(RouteSrcCore.IsPassThrough("System.Diagnostics.StackFrame", ".ctor"), "StackFrame ctor");
        Check(RouteSrcCore.IsPassThrough("X.Y", "ReplanFromPointOrig"), "*Orig");
        Check(!RouteSrcCore.IsPassThrough("Ts3Fixes.Fixes", "SpotVerdict"), "SpotVerdict is a caller");
        Check(!RouteSrcCore.IsPassThrough("Ts3Fixes.Fixes+RoutableCacheHook", "IsPointInLotSafelyRoutable"), "RoutableCacheHook is a caller");
        Check(!RouteSrcCore.IsPassThrough("Sims3.Gameplay.ObjectComponents.RoutingComponent", "DoRoute"), "DoRoute is a caller");
    }

    static void Repeats()
    {
        RepeatRing r = new RepeatRing();
        Check(r.Note(1, 0, 0, 0, 0, 1000, false) == -1, "first plan: no repeat");
        Check(r.Note(1, 0, 0.1f, 0, 0.1f, 1300, true) == 1, "0.3 s, prev fail -> band 0 fail (1)");
        Check(r.Note(1, 0, 0, 0, 0, 2800, true) == 2, "1.5 s, prev ok -> band 1 ok (2)");
        Check(r.Note(1, 0, 0, 0, 0, 9000, true) == 4, "6.2 s -> band 2 ok (4)");
        Check(r.Note(1, 0, 0, 0, 0, 20000, true) == -1, "11 s: no repeat");
        Check(r.Note(1, 1, 0, 0, 0, 20100, true) == -1, "other kind: no repeat");
        Check(r.Note(1, 1, 0.3f, 0, 0, 20200, true) == -1, "goal 0.3 m away: no repeat");
        Check(r.Note(2, 1, 0.3f, 0, 0, 20300, true) == -1, "other follower: no repeat");
        Check(r.Note(1, 1, 0.3f, 0, 0, unchecked((uint)20200 + 100), false) == 0, "same goal again 0.1 s -> band 0 ok (0)");
        // 64 others push follower 1 out of the ring
        for (ulong id = 100; id < 170; id++) r.Note(id, 0, 0, 0, 0, 30000, true);
        Check(r.Note(1, 1, 0.3f, 0, 0, 30100, true) == -1, "evicted follower: no repeat");
        // clock wrap: uint difference
        RepeatRing w = new RepeatRing();
        w.Note(5, 0, 0, 0, 0, uint.MaxValue - 100, true);
        Check(w.Note(5, 0, 0, 0, 0, 200, true) == 0, "wrap: 0.3 s");
    }

    static void LegSkip()
    {
        Check(RouteSrcCore.LegSkipEligible(true, true, false, 2, false, true, true, false, false), "eligible base case");
        Check(!RouteSrcCore.LegSkipEligible(false, true, false, 2, false, true, true, false, false), "not a replan result");
        Check(!RouteSrcCore.LegSkipEligible(true, false, false, 2, false, true, true, false, false), "portal mbReplan (no ReplanWhenFinished)");
        Check(!RouteSrcCore.LegSkipEligible(true, true, true, 2, false, true, true, false, false), "forcibly replanned");
        Check(!RouteSrcCore.LegSkipEligible(true, true, false, 1, false, true, true, false, false), "last path: a plain pop ends the route");
        Check(!RouteSrcCore.LegSkipEligible(true, true, false, 3, true, true, true, false, false), "loop");
        Check(!RouteSrcCore.LegSkipEligible(true, true, false, 2, false, false, true, false, false), "plan failed");
        Check(!RouteSrcCore.LegSkipEligible(true, true, false, 2, false, true, false, false, false), "remainder planned for the boat");
        Check(!RouteSrcCore.LegSkipEligible(true, true, false, 2, false, true, true, true, false), "still parented");
        Check(!RouteSrcCore.LegSkipEligible(true, true, false, 2, false, true, true, false, true), "swimming");
        Check(RouteSrcCore.LegSkipNear(0.3f, 0.1f, 0.3f), "0.42 m XZ: near");
        Check(!RouteSrcCore.LegSkipNear(0.4f, 0f, 0.4f), "0.57 m XZ: far");
        Check(!RouteSrcCore.LegSkipNear(0f, 0.6f, 0f), "0.6 m up: other level");
        Check(RouteSrcCore.DistanceBand(0.49f, 0) == 0 && RouteSrcCore.DistanceBand(1.9f, 0) == 1 && RouteSrcCore.DistanceBand(0, 2.1f) == 2, "bands");
    }

    static void Bouncer()
    {
        Check(RouteSrcCore.AtSlot(0.05f, 0, 0.05f, 1f), "on the slot, facing");
        Check(!RouteSrcCore.AtSlot(0.08f, 0, 0.08f, 1f), "0.113 m: route");
        Check(!RouteSrcCore.AtSlot(0, 0, 0, 0.9f), "turned ~26 degrees: route");
        Check(!RouteSrcCore.AtSlot(0, 0.3f, 0, 1f), "other height: route");
    }

    static void Memo()
    {
        PlanOkMemo m = new PlanOkMemo();
        m.Note(7, 10, 0, 10, 500);
        Check(m.Covers(7, 10.5f, 0, 10.5f, 400, 600), "0.71 m, after still: covered");
        Check(!m.Covers(7, 10.5f, 0, 10.9f, 400, 600), "1.03 m: not covered");
        Check(!m.Covers(7, 10, 0, 10, 501, 600), "plan before the Sim went still: not covered");
        Check(!m.Covers(7, 10, 0, 10, 400, 499), "clock went back: not covered");
        Check(!m.Covers(8, 10, 0, 10, 0, 600), "other Sim: not covered");
        m.Forget(7);
        Check(!m.Covers(7, 10, 0, 10, 0, 600), "forgotten");
        for (ulong i = 0; i < 1024; i++) m.Note(i, 0, 0, 0, 1);
        Check(m.Count == 1024, "1024 kept");
        m.Note(5000, 0, 0, 0, 1);
        Check(m.Count == 1, "cleared at the bound");
        Check(RouteSrcCore.kIdleChecksCapped == 4, "cap");
    }

    static int Main()
    {
        Classify(); PassThrough(); Repeats(); LegSkip(); Bouncer(); Memo();
        Console.WriteLine(sFails == 0 ? "routesrc selftest: all passed" : "routesrc selftest: " + sFails + " failed");
        return sFails == 0 ? 0 : 1;
    }
}
