using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Utilities;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Routing fixes from docs/10, 11 and 12. Counters: [27] virtual-home runs, [28] virtual-home fades, [29] obstacle
    // replans switched off, [30] route-fail walks skipped, [31] stuck Sims reset by the watchdog, [32] station detours skipped.
    public static partial class Fixes
    {
        static void ApplyRouteFixes()
        {
            Patch(typeof(Sim.GoToVirtualHome.GoToVirtualHomeInternal), "Run", typeof(VirtualHomeHook), "RunHook", "RunOrig");
            Patch(typeof(SimRoutingComponent), "SetRouteOptions", typeof(RouteOptionsHook), "SetRouteOptionsHook", "SetRouteOptionsOrig");
            Patch(typeof(SimRoutingComponent), "GetRouteFailPath", typeof(RouteOptionsHook), "GetRouteFailPathHook", "GetRouteFailPathOrig");
            Patch(typeof(SimRoutingComponent), "CheckAndUpdateRouteForStations", typeof(StationsHook), "CheckStationsHook", "CheckStationsOrig");
        }

        // After every successful cross-lot plan, SimRoutingComponent.CheckAndUpdateRouteForStations rolls a chance for an
        // NPC to take the subway or hover train instead, scanning every station and planning up to 3 more routes
        // (docs/10). Nobody sees an unseen townie ride the subway, so it keeps the route it already has. The first static
        // hook: a static replacement and a static stub (the stub may only be called from the hook). Counter [32].
        public static class StationsHook
        {
            public static void CheckStationsHook(Route r)
            {
                if (ShortcutOn("stations"))
                {
                    Sim sim = r.Follower != null ? r.Follower.Target as Sim : null;
                    if (sim != null && TownieUnseen(sim, null))
                    {
                        PatchCounters()[32]++;
                        return;
                    }
                }
                CheckStationsOrig(r);
            }
            public static void CheckStationsOrig(Route r) { } // receives CheckAndUpdateRouteForStations' original header
        }

        // Homeless and virtual-apartment townies "go home" by walking to a random or apartment lot and fading out there
        // (Sim.GoToVirtualHome.GoToVirtualHomeInternal.Run; 4.4% of script time in Bridgeport, docs/10, docs/12). When the
        // townie and that home are unseen, end the trip the game's own way right away: fade out and remove the Sim.
        // Tourists and aliens keep their own handling.
        public class VirtualHomeHook : Sim.GoToVirtualHome.GoToVirtualHomeInternal
        {
            public bool RunHook()
            {
                int[] c = PatchCounters();
                c[27]++;
                SimDescription d = Actor.SimDescription;
                Lot home = d.VirtualLotHome;
                if (ShortcutOn("virtualhome") && d.Household != null && !d.Household.IsTouristHousehold && !d.Household.IsAlienHousehold
                    && TownieUnseen(Actor, null) && (home == null || FarFromViewers(home.Position)))
                {
                    c[28]++;
                    ShortcutLog(Actor.FullName + " -> virtual home (faded)");
                    Actor.FadeOut(true, true);
                    Simulator.Sleep(uint.MaxValue);
                    return true;
                }
                return RunOrig();
            }
            public bool RunOrig() { return false; } // receives GoToVirtualHomeInternal.Run's original header
        }

        // A failed plan for a Sim costs a second full search to find the blocker (ReplanToFindObstacleWhenPathPlanFails,
        // set on every Sim route) and then more plans to walk the Sim to it for the route-fail animation
        // (GetRouteFailPath; docs/11). Nobody sees that for an unseen townie: switch the option off for its routes and
        // skip the walk (callers already handle "no fail path").
        public class RouteOptionsHook : SimRoutingComponent
        {
            public void SetRouteOptionsHook(Route r, uint ageGenderFlags)
            {
                SetRouteOptionsOrig(r, ageGenderFlags);
                if (ShortcutOn("failreplan") && TownieUnseen(OwnerSim, null))
                {
                    r.SetOption(Route.RouteOption.ReplanToFindObstacleWhenPathPlanFails, false);
                    PatchCounters()[29]++;
                }
            }
            public void SetRouteOptionsOrig(Route r, uint ageGenderFlags) { } // receives SetRouteOptions' original header

            public Route GetRouteFailPathHook(Route r)
            {
                if (ShortcutOn("failreplan") && TownieUnseen(OwnerSim, null))
                {
                    PatchCounters()[30]++;
                    return null;
                }
                return GetRouteFailPathOrig(r);
            }
            public Route GetRouteFailPathOrig(Route r) { return null; } // receives GetRouteFailPath's original header
        }

        // Watchdog for stuck townies (docs/12): an unseen townie that has not moved for 60 sim-minutes while going home
        // (GoHome, GoToVirtualHome or its internal step), or for 120 sim-minutes while routing anywhere (two Sims blocking
        // each other in a doorway wait it out forever otherwise), gets the game's own Sim.SetToResetAndSendHome(). At most
        // once per Sim per sim-day, so a Sim that gets stuck again on the way home is not reset in a loop. Checks every
        // 10 sim-minutes while a world is loaded.
        const float kStuckGoHomeMinutes = 60f, kStuckRoutingMinutes = 120f, kWatchMinutes = 10f;

        public static void StartWatchdog()
        {
            if (!ShortcutOn("watchdog")) return;
            Simulator.AddObject(new StuckWatchdog());
        }

        class StuckWatchdog : Task
        {
            struct Seen { public Vector3 Pos; public long Since; }
            readonly Dictionary<ulong, Seen> mSeen = new Dictionary<ulong, Seen>();
            readonly Dictionary<ulong, long> mLastReset = new Dictionary<ulong, long>();

            public override void Simulate()
            {
                uint wait = (uint)SimClock.ConvertToTicks(kWatchMinutes, TimeUnit.Minutes);
                while (true)
                {
                    try { Check(); } catch (Exception) { mSeen.Clear(); }
                    Simulator.Sleep(wait);
                }
            }

            void Check()
            {
                long now = SimClock.CurrentTicks, day = SimClock.ConvertToTicks(1f, TimeUnit.Days);
                long goHome = SimClock.ConvertToTicks(kStuckGoHomeMinutes, TimeUnit.Minutes), routing = SimClock.ConvertToTicks(kStuckRoutingMinutes, TimeUnit.Minutes);
                Dictionary<ulong, Seen> keep = new Dictionary<ulong, Seen>();
                foreach (Sim s in Sims3.Gameplay.Queries.GetObjects<Sim>())
                {
                    InteractionInstance cur = s.CurrentInteraction;
                    if (cur == null) continue;
                    bool home = cur is GoHome || cur is Sim.GoToVirtualHome || cur is Sim.GoToVirtualHome.GoToVirtualHomeInternal;
                    bool moving = s.SimRoutingComponent != null && s.SimRoutingComponent.IsRouting;
                    if (!home && !moving) continue;
                    if (!TownieUnseen(s, null)) continue;
                    ulong id = s.ObjectId.Value;
                    Seen seen;
                    if (!mSeen.TryGetValue(id, out seen) || (s.Position - seen.Pos).Length() > 1f)
                        seen = new Seen { Pos = s.Position, Since = now };
                    long last;
                    bool resetToday = mLastReset.TryGetValue(s.SimDescription.SimDescriptionId, out last) && now - last < day && last <= now;
                    if (!resetToday && now - seen.Since >= (home ? goHome : routing))
                    {
                        PatchCounters()[31]++;
                        mLastReset[s.SimDescription.SimDescriptionId] = now;
                        ShortcutLog(s.FullName + " -> reset and sent home (stuck on " + cur.GetType().Name + ")");
                        s.SetToResetAndSendHome();
                        continue;
                    }
                    keep[id] = seen;
                }
                mSeen.Clear();
                foreach (KeyValuePair<ulong, Seen> kv in keep) mSeen[kv.Key] = kv.Value;
                if (mLastReset.Count > 1024) mLastReset.Clear();
            }
        }
    }
}
