using System;
using System.Collections.Generic;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
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
            Dispatcher.Register("sim_strand", SimStrand);
            Dispatcher.Register("sim_visit_lot", SimVisitLot);
            Dispatcher.Register("sim_route_to", SimRouteTo);
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

        // sim_strand {sim, lot?, tries?}: stand the Sim on a spot of `lot` (default: the lot it is on) that fails the game's
        // Sim.IsPointInLotSafelyRoutable (an enclosed room, a fenced corner, a pool tile), the case NRaas Overwatch and the
        // ts3fixes watchdog reset as "unroutable". Samples up to `tries` (default 40) random lot positions, one route plan
        // each, in this one call (a test hitch is fine). Error if every sample is routable.
        static object SimStrand(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            Lot lot = a.Has("lot") ? FindLotByName(a.Str("lot")) : s.LotCurrent;
            if (lot == null || lot.IsWorldLot) throw new McpException("needs a non-world lot (pass lot)");
            int tries = a.Has("tries") ? (int)a.Double("tries") : 40;
            for (int i = 0; i < tries; i++)
            {
                Vector3 p = lot.GetRandomPosition(true, true, TerrainType.LotTerrain | TerrainType.LotFloor | TerrainType.LotPool);
                if (p == Vector3.Invalid || s.IsPointInLotSafelyRoutable(lot, p)) continue;
                s.InteractionQueue.CancelAllInteractions();
                s.SetPosition(p);
                Dictionary<string, object> r = Util.Obj();
                r["sim"] = s.FullName;
                r["lot"] = lot.Name;
                r["x"] = Math.Round(p.x, 2); r["y"] = Math.Round(p.y, 2); r["z"] = Math.Round(p.z, 2);
                r["terrain"] = World.GetTerrainType(p).ToString();
                r["samples"] = i + 1;
                return r;
            }
            throw new McpException("no unroutable spot in " + tries + " samples on " + lot.Name + " (try another lot or more tries)");
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

        // Lot by name (case-insensitive, exact first, then "contains").
        static Lot FindLotByName(string name)
        {
            Lot partial = null;
            foreach (Lot l in LotManager.AllLots)
            {
                if (l == null || l.Name == null) continue;
                if (string.Equals(l.Name, name, StringComparison.OrdinalIgnoreCase)) return l;
                if (partial == null && l.Name.IndexOf(name, StringComparison.OrdinalIgnoreCase) >= 0) partial = l;
            }
            if (partial == null) throw new McpException("no lot named '" + name + "'");
            return partial;
        }

        // sim_visit_lot {sim, lot}: the game's own visit interaction (VisitCommunityLot for community lots, VisitLot for
        // homes), autonomous priority like a townie's own choice; triggers the lot-visit shortcuts on demand.
        static object SimVisitLot(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            Lot lot = FindLotByName(a.Str("lot"));
            InteractionDefinition def = lot.IsCommunityLot ? VisitCommunityLot.Singleton : VisitLot.Singleton;
            InteractionInstance i = def.CreateInstance(lot, s, new InteractionPriority(InteractionPriorityLevel.Autonomous), true, true);
            Dictionary<string, object> r = Util.Obj();
            r["queued"] = s.InteractionQueue.Add(i);
            r["lot"] = lot.Name;
            r["interaction"] = def.GetType().DeclaringType != null ? def.GetType().DeclaringType.Name : def.GetType().Name;
            return r;
        }

        // sim_route_to {sim, x, z}: plan a route for any Sim to a point (go_here only drives the active Sim); a route-failure
        // trigger for the backoff / route-fail repros (the plan goes through the patched planner). Plans only: walking must
        // run in the Sim's own task, not the agent's.
        static object SimRouteTo(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            Vector3 p = new Vector3((float)a.Double("x"), 0f, (float)a.Double("z"));
            p.y = World.GetTerrainHeight(p.x, p.z);
            Route route = s.CreateRoute();
            route.PlanToPoint(p);
            Dictionary<string, object> r = Util.Obj();
            r["plan"] = route.PlanResult.mType.ToString();
            return r;
        }
    }
}
