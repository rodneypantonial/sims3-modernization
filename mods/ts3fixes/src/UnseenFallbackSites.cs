using System;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // docs/202 §3.4 C1-C4 (switch unseenfbsites, off): the ids of the Sims that pass the unseen rule (UfWhy == 0: live mode,
    // not the household, not on a seen lot, off screen, far from the camera and the household, not riding / carried /
    // carrying, not with a visible Sim) go to ts3native's [UnseenFallback] Sites=1 every few sim ticks. Its four sites then
    // wait at most SiteWaitMs for a running route job's lock on behalf of a listed Sim and skip that step with a harmless
    // answer. Channel: GetOption2 with optHi top byte 0x83 and a code in bits 48-55 (no RouteOption2 name): 7 BEGIN, 8 id low,
    // 9 id high (appends one id), 10 END with the count. Out of live mode the list is empty; ts3native treats a list older
    // than 1.5 s as none, so nothing acts when this loop stops. BEGIN refused (no site installed, older ts3native, the game)
    // ends the loop for this world load.
    // Counters: [323] lists pushed, [324] ids pushed (sum), [325] pushes refused (BEGIN refused or a field refused).
    public static partial class Fixes
    {
        const uint kUfSitesPushTicks = 5;  // as the viewer push; ts3native's list lives 1.5 s
        const int kUfSitesMax = 256;       // ts3native's list size
        static int sUfSitesGen;

        internal static void StartUfSitesPush()
        {
            int gen = ++sUfSitesGen;  // a loop left from an earlier load ends at its next wake-up
            Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(delegate { UfSitesLoop(gen); }));
        }

        static void UfSitesLoop(int gen)
        {
            while (gen == sUfSitesGen)
            {
                try { if (ShortcutOn("unseenfbsites") && !UfSitesPush()) return; } catch (Exception) { return; }
                Simulator.Sleep(kUfSitesPushTicks);
            }
        }

        static bool UfSitesSend(int code, uint value)
        {
            return sYieldProbe.GetOption2((Sims3.SimIFace.Route.RouteOption2)(0x8300000000000000UL | (ulong)(uint)code << 48 | value));
        }

        // false = BEGIN refused: no C site this load
        static bool UfSitesPush()
        {
            int[] c = PatchCounters();
            if (!UfSitesSend(7, 0)) { c[325]++; return false; }
            uint n = 0;
            bool live = GameStates.IsLiveState && !GameStates.IsTravelling && !GameStates.IsWorldShuttingDown;
            if (live)
                foreach (Sim s in Sims3.Gameplay.Queries.GetObjects<Sim>())
                {
                    if (n == kUfSitesMax) break;
                    if (s == null || s.SimDescription == null || s.SimDescription.IsPet || UfWhy(s) != 0) continue;  // pets and horses: never (as the plan marks)
                    ulong id = s.ObjectId.Value;
                    // a refused field: the old list stays and goes stale, so nothing acts
                    if (!UfSitesSend(8, (uint)id) || !UfSitesSend(9, (uint)(id >> 32))) { c[325]++; return true; }
                    n++;
                }
            if (!UfSitesSend(10, n)) { c[325]++; return true; }
            c[323]++;
            c[324] += (int)n;
            return true;
        }
    }
}
