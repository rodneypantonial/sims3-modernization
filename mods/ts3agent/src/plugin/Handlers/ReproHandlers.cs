using System;
using System.Collections.Generic;
using Sims3.Gameplay.Actors;
using Sims3.SimIFace;

namespace Ts3Agent
{
    // Repro steps (tools/harness/repro.ts): put a Sim somewhere and give it a trip, so a bug that depends on a place can
    // be reproduced on demand instead of waiting for it.
    public static partial class Handlers
    {
        static void RegisterRepro()
        {
            Dispatcher.Register("place_sim", PlaceSim);
            Dispatcher.Register("sim_go_home", SimGoHome);
        }

        // place_sim {sim, x, z, y?}: stand the Sim at a world position (resets what it was doing, like the game's teleport)
        static object PlaceSim(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            Vector3 p = s.Position;
            p.x = (float)a.Double("x");
            p.z = (float)a.Double("z");
            p.y = a.Has("y") ? (float)a.Double("y") : World.GetTerrainHeight(p.x, p.z);
            s.InteractionQueue.CancelAllInteractions();
            s.SetPosition(p);
            Dictionary<string, object> r = Util.Obj();
            r["sim"] = s.FullName;
            r["lot"] = s.LotCurrent != null ? s.LotCurrent.Name : null;
            return r;
        }

        // sim_go_home {sim}: the game's own Sim.MakeSimGoHome (GoHome, or the virtual-home trip for homeless townies)
        static object SimGoHome(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            Dictionary<string, object> r = Util.Obj();
            r["queued"] = Sim.MakeSimGoHome(s, false);
            r["home"] = s.LotHome != null ? s.LotHome.Name : null;
            return r;
        }
    }
}
