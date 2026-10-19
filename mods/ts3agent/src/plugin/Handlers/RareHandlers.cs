using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay.ActorSystems;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.SimIFace;

namespace Ts3Agent
{
    // Rare routing scenarios (docs/117): crowds on one spot, a death during a walk, and route targets that need a search
    // (pond, sea, far lot). Route option bits for sim_route_to are in ReproHandlers.
    public static partial class Handlers
    {
        static void RegisterRare()
        {
            Dispatcher.Register("crowd_go", CrowdGo);
            Dispatcher.Register("kill_sim", KillSim);
            Dispatcher.Register("zombies", Zombies);
        }

        // Point for {to}: "pond" = representative point of the world pond nearest the Sim; "ocean" = 10 m into the sea
        // found on rings of 10..400 m round the Sim; "far" = centre of the community lot farthest from the Sim.
        // Vector3.Invalid when the world has none.
        static Vector3 RarePoint(Sim s, string to)
        {
            to = to.Trim().ToLowerInvariant();
            if (to == "ocean" || to == "sea") return SeaPoint(s);
            float best = -1f;
            Vector3 pos = Vector3.Invalid;
            if (to == "pond")
            {
                IEnumerable<Sims3.Gameplay.Pools.WorldPond> ponds = Sims3.Gameplay.Pools.PondManager.GetAllWorldPonds();
                if (ponds == null) return pos;
                best = float.MaxValue;
                foreach (Sims3.Gameplay.Pools.WorldPond p in ponds)
                {
                    Vector3 c = p.RepresentativePondPosition();
                    float d = (c - s.Position).LengthSqr();
                    if (d < best) { best = d; pos = c; }
                }
                return pos;
            }
            if (to == "far")
            {
                Lot far = FarLot(s);
                return far != null ? far.GetCenterPosition() : pos;
            }
            throw new McpException("to must be pond, ocean or far");
        }

        // The community lot (not the world lot) farthest from the Sim; null when the world has none.
        static Lot FarLot(Sim s)
        {
            Lot far = null;
            float best = -1f;
            foreach (Lot l in LotManager.AllLots)
            {
                if (l == null || !l.IsCommunityLot || l.IsWorldLot) continue;
                float d = (l.GetCenterPosition() - s.Position).LengthSqr();
                if (d > best) { best = d; far = l; }
            }
            return far;
        }

        static Vector3 SeaPoint(Sim s)
        {
            for (float rad = 10f; rad <= 400f; rad += 10f)
                for (int i = 0; i < 24; i++)
                {
                    double ang = i * Math.PI / 12;
                    Vector3 dir = new Vector3((float)Math.Cos(ang), 0f, (float)Math.Sin(ang));
                    Vector3 p = s.Position + dir * rad, far = s.Position + dir * (rad + 10f);
                    if ((TerrainType)World.GetTerrainType(p.x, p.z, 0) == TerrainType.WorldSea
                        && (TerrainType)World.GetTerrainType(far.x, far.z, 0) == TerrainType.WorldSea) return far;
                }
            return Vector3.Invalid;
        }

        static Vector3 RequireRarePoint(Sim s, string to)
        {
            Vector3 p = RarePoint(s, to);
            if (p == Vector3.Invalid) throw new McpException("no " + to + " point in this world near " + s.FullName);
            return p;
        }

        static void QueueGoHere(Sim s, Vector3 pos)
        {
            InteractionInstance ii = Terrain.GoHere.GetSingleton(s, pos).CreateInstance(Terrain.Singleton, s,
                new InteractionPriority(InteractionPriorityLevel.UserDirected), false, true);
            Terrain.GoHere gh = ii as Terrain.GoHere;
            if (gh != null) gh.SetDestination(pos, false);
            if (!s.InteractionQueue.Add(ii)) throw new McpException("could not queue Go Here for " + s.FullName);
        }

        // Sims spawn_sims made that are in the world now.
        static List<Sim> SpawnedInWorld()
        {
            List<Sim> list = new List<Sim>();
            foreach (ulong id in SpawnedSims())
            {
                SimDescription sd = SimDescription.Find(id);
                if (sd != null && sd.CreatedSim != null && sd.CreatedSim.InWorld) list.Add(sd.CreatedSim);
            }
            return list;
        }

        // crowd_go {target | to | x,z, max?: 50, household?: false}: every spawn_sims Sim (and the active household with
        // household: true) walks to the same point at once (Go Here, user-directed), so the arrivals steer round each other.
        static object CrowdGo(Args a)
        {
            RequireLive();
            Sim me = Util.ActiveSim();
            Vector3 pos = WalkGoal(a, me);
            if (pos == Vector3.Invalid) throw new McpException("give target, to or x,z");
            List<Sim> crowd = SpawnedInWorld();
            if (a.Bool("household", false) && Household.ActiveHousehold != null)
                foreach (Sim s in Household.ActiveHousehold.AllActors) if (!crowd.Contains(s)) crowd.Add(s);
            int max = (int)a.Long("max", 50);
            List<object> sent = new List<object>();
            foreach (Sim s in crowd)
            {
                if (sent.Count >= max) break;
                try { QueueGoHere(s, pos); sent.Add(s.FullName); }
                catch (Exception e) { sent.Add(s.FullName + ": " + e.Message); }
            }
            if (sent.Count == 0) throw new McpException("no crowd: spawn_sims first (or household: true)");
            Dictionary<string, object> r = Util.Obj();
            r["sent"] = sent;
            r["x"] = Util.Round(pos.x); r["z"] = Util.Round(pos.z);
            return r;
        }

        // kill_sim {sim? | pick?: "routing" | "any", death?: "Electrocution"}: Sim.Kill (Urnstone.KillSim at MaxDeath
        // priority, which cancels the walk). Without sim it picks a spawn_sims Sim, one that is routing right now when
        // pick is "routing" (the default; falls back to any spawned Sim, reported in "routing"). Never the active Sim
        // unless named.
        static object KillSim(Args a)
        {
            RequireLive();
            Sim victim = null;
            bool routing = false;
            if (a.Has("sim")) { victim = Util.FindSim(a, "sim"); routing = victim.IsRouting; }
            else
            {
                List<Sim> spawned = SpawnedInWorld();
                if (a.Str("pick", "routing") == "routing")
                    foreach (Sim s in spawned) if (s.IsRouting) { victim = s; routing = true; break; }
                if (victim == null && spawned.Count > 0) victim = spawned[0];
            }
            if (victim == null) throw new McpException("no spawned Sim to kill: spawn_sims first or pass sim");
            SimDescription.DeathType dt = (SimDescription.DeathType)Enum.Parse(typeof(SimDescription.DeathType), a.Str("death", "Electrocution"), true);
            Dictionary<string, object> r = Util.Obj();
            r["sim"] = victim.FullName;
            r["routing"] = routing;
            r["death"] = dt.ToString();
            r["killed"] = victim.Kill(dt);
            return r;
        }

        // Destination of a walk for commands that take {target | to | x,z}; Vector3.Invalid when none is given.
        static Vector3 WalkGoal(Args a, Sim me)
        {
            if (a.Has("target"))
            {
                GameObject t = FindTarget(a, "target");
                return t is Lot ? ((Lot)t).GetCenterPosition() : t.Position;
            }
            if (a.Has("to")) return RequireRarePoint(me, a.Str("to"));
            if (a.Has("x")) return new Vector3((float)a.Double("x"), (float)a.Double("y", 0), (float)a.Double("z"));
            return Vector3.Invalid;
        }

        // zombies {spawn?: 0, spawned?: false, target | to | x,z?}: Supernatural full-moon zombies on demand (docs/117).
        // spawn: n townies that pass the game's own LunarCycleManager.IsZombifiable are instantiated on the active Sim's lot
        // and get Sim.BeReanimatedSolo at CriticalNPCBehavior, as LunarCycleManager.PossiblySpawnZombie does without its 20%
        // roll (grave-rise animation, Zombie transformation buff). spawned: every spawn_sims Sim gets the Zombie buff
        // (Origin.FromFullMoon, what TriggerLunarEvents gives its two picks). With a goal every zombie then gets a Go Here
        // there. Our zombies are not in the game's sunrise list: they stay zombies until the game ends.
        static object Zombies(Args a)
        {
            RequireLive();
            Sim me = Util.ActiveSim();
            List<Sim> zs = new List<Sim>();
            List<object> names = new List<object>();
            int n = (int)a.Long("spawn", 0);
            if (n > 0)
            {
                MethodInfo ok = typeof(Sims3.Gameplay.Controllers.LunarCycleManager).GetMethod("IsZombifiable", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static);
                if (ok == null) throw new McpException("LunarCycleManager.IsZombifiable not found");
                foreach (SimDescription sd in Household.AllTownieSimDescriptions())
                {
                    if (zs.Count >= n) break;
                    if (sd == null || (sd.CreatedSim != null && !sd.CreatedSim.InWorld) || !(bool)ok.Invoke(null, new object[] { sd })) continue;
                    Sim z = sd.CreatedSim ?? sd.Instantiate(me.LotCurrent);
                    if (z == null) continue;
                    InteractionInstance ii = Sim.BeReanimatedSolo.Singleton.CreateInstance(z, z,
                        new InteractionPriority(InteractionPriorityLevel.CriticalNPCBehavior), false, false);
                    z.InteractionQueue.AddNext(ii);
                    zs.Add(z); names.Add(z.FullName + " (risen)");
                }
            }
            if (a.Bool("spawned", false))
                foreach (Sim s in SpawnedInWorld())
                {
                    bool added = s.BuffManager != null && s.BuffManager.AddElement(BuffNames.Zombie, Origin.FromFullMoon);
                    if (added) zs.Add(s);
                    names.Add(s.FullName + (added ? " (buff)" : " (buff refused)"));
                }
            if (zs.Count == 0) throw new McpException("no zombie made: spawn n (needs zombifiable townies) or spawned: true after spawn_sims");
            Vector3 goal = WalkGoal(a, me);
            int walking = 0;
            if (goal != Vector3.Invalid)
                foreach (Sim z in zs)
                    try { QueueGoHere(z, goal); walking++; } catch (Exception e) { names.Add(z.FullName + ": " + e.Message); }
            Dictionary<string, object> r = Util.Obj();
            r["zombies"] = names;
            r["walking"] = walking;
            return r;
        }
    }
}
