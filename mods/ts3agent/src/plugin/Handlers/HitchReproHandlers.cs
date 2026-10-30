using System;
using System.Collections.Generic;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;

namespace Ts3Agent
{
    // Loop repros for script hitches (docs/169 §script causes): set up the state a path needs directly, re-created each loop.
    public static partial class Handlers
    {
        // strand_many {count?, tries?}: stand up to `count` (default 8) instantiated non-active Sims, each on its own lot (the
        // lot it is on, else its home lot; never the world lot or the active household's lot), on a spot of that lot that
        // fails the game's Sim.IsPointInLotSafelyRoutable (sim_strand's sampling, `tries` route plans per Sim at most). These
        // Sims fail every stuck-check plan, so the 3 AM check (Fixes.StuckCheckNow) does its full 10 plans for each.
        static object StrandMany(Args a)
        {
            int want = (int)a.Long("count", 8), tries = (int)a.Long("tries", 40);
            Household active = Household.ActiveHousehold;
            Lot activeLot = active != null ? active.LotHome : null;
            Dictionary<Lot, bool> used = new Dictionary<Lot, bool>();
            List<object> placed = new List<object>();
            int candidates = 0, plans = 0;
            foreach (Sim s in new List<Sim>(LotManager.Actors))
            {
                if (placed.Count >= want) break;
                if (s == null || s.HasBeenDestroyed || s.SimDescription == null || s.Household == active || !s.SimDescription.IsHuman
                    || !s.SimDescription.ChildOrAbove || s.Parent != null || s.SimInRabbitHolePosture) continue;
                Lot lot = s.LotCurrent != null && !s.LotCurrent.IsWorldLot ? s.LotCurrent : s.LotHome;
                if (lot == null || lot.IsWorldLot || lot == activeLot || used.ContainsKey(lot)) continue;
                candidates++;
                for (int i = 0; i < tries; i++)
                {
                    Vector3 p = lot.GetRandomPosition(true, true, TerrainType.LotTerrain | TerrainType.LotFloor | TerrainType.LotPool);
                    if (p == Vector3.Invalid) continue;
                    plans++;
                    if (s.IsPointInLotSafelyRoutable(lot, p)) continue;
                    s.InteractionQueue.CancelAllInteractions();
                    s.SetPosition(p);
                    used[lot] = true;
                    Dictionary<string, object> e = Util.Obj();
                    e["sim"] = s.FullName;
                    e["lot"] = lot.Name;
                    e["terrain"] = World.GetTerrainType(p).ToString();
                    placed.Add(e);
                    break;
                }
            }
            if (placed.Count == 0) throw new McpException("no Sim stranded (" + candidates + " candidate lots, " + plans + " plans)");
            Dictionary<string, object> r = Util.Obj();
            r["stranded"] = placed.Count;
            r["candidates"] = candidates;
            r["plans"] = plans;
            r["sims"] = placed;
            return r;
        }

        // posture_chain_check: issue #89, read-only. Sims whose posture chain (PreviousPosture) does not end within 16 postures
        // (the two-posture loop EA's Sim.set_Posture makes on a pop whose target is Compatible); "looped" stays empty with
        // posturepopcycle on. Walks the chain itself (bounded), never Posture.Satisfies.
        static object PostureChainCheck(Args a)
        {
            List<object> looped = new List<object>();
            int sims = 0, deepest = 0;
            foreach (Sim s in new List<Sim>(LotManager.Actors))
            {
                if (s == null || s.HasBeenDestroyed) continue;
                sims++;
                int n = 0;
                for (Sims3.Gameplay.ActorSystems.Posture p = s.Posture; p != null && n <= 16; p = p.PreviousPosture) n++;
                if (n <= 16) { if (n > deepest) deepest = n; continue; }
                Dictionary<string, object> e = Util.Obj();
                e["sim"] = s.FullName;
                e["posture"] = s.Posture.GetType().FullName;
                looped.Add(e);
            }
            Dictionary<string, object> r = Util.Obj();
            r["sims"] = sims;
            r["deepest"] = deepest;
            r["looped"] = looped;
            return r;
        }
    }
}
