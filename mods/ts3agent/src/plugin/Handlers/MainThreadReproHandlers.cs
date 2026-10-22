using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;
using Sims3.SimIFace.CAS;

namespace Ts3Agent
{
    // Main-thread hitch repros (docs/169 "main thread"): set the triggering state directly and re-create it in a loop, so the
    // main-slot services of docs/167 §7 fire many times per run instead of waiting for play to cause them.
    // camera_jumps: World/Service lot streaming (jumps across far lots) and CAS/SimService builds (jumps to populated lots,
    // Sims spawned into view, forced outfit changes). vfx_burst: SwarmSystem (fire / magic / lightning effects in view).
    // Only the in-game camera moves: the game window is never moved or focused. Nothing is saved.
    public static partial class Handlers
    {
        static void RegisterMainThreadRepro()
        {
            Dispatcher.Register("camera_jumps", CameraJumps);
            Dispatcher.Register("vfx_burst", VfxBurst);
        }

        static int sJumps, sJumpOutfits, sJumpSpawned, sJumpLoops;

        // camera_jumps {order?: far|populated, lots?, dwellS?, height?, loops?, outfits?, spawn?} | {stop:true} | {status:true}
        // A game task jumps the camera (no pan) to one lot after another and holds it still dwellS (default 8) wall-clock
        // seconds at street zoom (height, default 25 m), so lot scoring streams lots in (docs/28 §5.2: activation needs a slow
        // camera). order far (default): `lots` (default 10) lots in farthest-next order, every jump crosses the town;
        // populated: the `lots` lots with the most Sims on them, re-ranked every loop. Per jump: outfits = up to that many
        // humans on the target lot switch outfit (athletic / everyday by loop parity: a SimService build each); spawn = that
        // many new Sims (spawn_sims households, tracked for remove_sims) stand around the lot centre in view; Sims spawned by
        // the previous loop are removed first, so every loop builds new ones. loops (default 1000) = until stop. Shares the
        // camera_pan generation: a new camera_jumps or camera_pan ends a running one. Returns the first loop's path.
        static object CameraJumps(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            if (a.Has("status") || a.Has("stop"))
            {
                if (a.Has("stop")) { ++sPanGen; r["stopped"] = true; }
                r["jumps"] = sJumps; r["loops"] = sJumpLoops; r["outfitChanges"] = sJumpOutfits; r["spawned"] = sJumpSpawned;
                return r;
            }
            RequireLive();
            int gen = ++sPanGen;
            bool populated = a.Str("order", "far").ToLowerInvariant() == "populated";
            int k = (int)a.Long("lots", 10), loops = (int)a.Long("loops", 1000), outfits = (int)a.Long("outfits", 0), spawn = (int)a.Long("spawn", 0);
            int dwellMs = (int)(a.Double("dwellS", 8) * 1000);
            float height = (float)a.Double("height", 25);
            if (k < 2 || dwellMs < 500 || spawn < 0 || spawn > 10) throw new McpException("lots >= 2, dwellS >= 0.5, spawn 0..10");
            List<Lot> first = populated ? PopulatedLots(k) : FarLots(k);
            if (first.Count < 2) throw new McpException("fewer than 2 lots to jump between");
            sJumps = sJumpOutfits = sJumpSpawned = sJumpLoops = 0;
            Simulator.AddObject(new OneShotFunctionTask(delegate
            {
                for (int loop = 0; loop < loops && sPanGen == gen; loop++)
                {
                    if (spawn > 0) RemoveSims(new Args(new Dictionary<string, object>()));
                    List<Lot> path = loop == 0 ? first : populated ? PopulatedLots(k) : FarLots(k);
                    OutfitCategories cat = loop % 2 == 0 ? OutfitCategories.Athletic : OutfitCategories.Everyday;
                    foreach (Lot lot in path)
                    {
                        if (sPanGen != gen) break;
                        Vector3 c = lot.GetCenterPosition();
                        Vector3 target = new Vector3(c.x, World.GetTerrainHeight(c.x, c.z), c.z);
                        CameraController.SetPositionAndTarget(target + new Vector3(-height * 0.7f, height, -height * 0.7f), target);
                        sJumps++;
                        if (spawn > 0) sJumpSpawned += SpawnAround(c, spawn);
                        if (outfits > 0) sJumpOutfits += ChangeOutfitsOn(lot, outfits, cat);
                        int t0 = System.Environment.TickCount;
                        while (sPanGen == gen && System.Environment.TickCount - t0 < dwellMs) Simulator.Sleep(0);
                    }
                    sJumpLoops++;
                }
            }));
            List<object> pts = new List<object>();
            foreach (Lot l in first) pts.Add(Util.LotName(l));
            r["path"] = pts;
            r["dwellMs"] = dwellMs;
            return r;
        }

        static List<Lot> JumpLots()
        {
            List<Lot> c = new List<Lot>();
            foreach (Lot l in LotManager.AllLots) if (l != null && !l.IsWorldLot) c.Add(l);
            return c;
        }

        // farthest-next order from the lot farthest from the camera: each jump goes to the remaining lot farthest from the last
        static List<Lot> FarLots(int k)
        {
            List<Lot> c = JumpLots(), path = new List<Lot>();
            Vector3 at = CameraController.GetTarget();
            while (path.Count < k && c.Count > 0)
            {
                int best = 0; float bestD = -1;
                for (int i = 0; i < c.Count; i++)
                {
                    float d = Flat(c[i].GetCenterPosition() - at).Length();
                    if (d > bestD) { bestD = d; best = i; }
                }
                at = c[best].GetCenterPosition();
                path.Add(c[best]);
                c.RemoveAt(best);
            }
            return path;
        }

        // lots by Sims on them (Sim.LotCurrent), most first; lots with none fill up the list in their own order
        static List<Lot> PopulatedLots(int k)
        {
            Dictionary<ulong, int> n = new Dictionary<ulong, int>();
            foreach (Sim s in Sims3.Gameplay.Queries.GetObjects<Sim>())
                if (s != null && s.LotCurrent != null && !s.LotCurrent.IsWorldLot)
                {
                    int v; n.TryGetValue(s.LotCurrent.LotId, out v); n[s.LotCurrent.LotId] = v + 1;
                }
            List<Lot> c = JumpLots();
            c.Sort(delegate(Lot x, Lot y)
            {
                int vx, vy; n.TryGetValue(x.LotId, out vx); n.TryGetValue(y.LotId, out vy);
                return vy.CompareTo(vx);
            });
            if (c.Count > k) c.RemoveRange(k, c.Count - k);
            return c;
        }

        // spawn_sims' creation (random non-active household with a home lot), placed within 6 m of pos; tracked for remove_sims
        static int SpawnAround(Vector3 pos, int n)
        {
            List<Household> homes = new List<Household>();
            foreach (Household h in Household.sHouseholdList)
                if (!h.IsActive && !h.IsSpecialHousehold && h.LotHome != null) homes.Add(h);
            if (homes.Count == 0) return 0;
            List<ulong> spawned = SpawnedSims();
            int made = 0;
            for (int i = 0; i < n; i++)
            {
                Household h = RandomUtil.GetRandomObjectFromList(homes);
                SimUtils.SimCreationSpec spec = new SimUtils.SimCreationSpec();
                spec.Age = CASAgeGenderFlags.YoungAdult | CASAgeGenderFlags.Adult | CASAgeGenderFlags.Elder;
                spec.Normalize();
                SimDescription sd = spec.Instantiate();
                h.Add(sd);
                Vector3 p = pos;
                p.x += RandomUtil.GetFloat(-6f, 6f);
                p.z += RandomUtil.GetFloat(-6f, 6f);
                spawned.Add(sd.SimDescriptionId);
                if (sd.Instantiate(p) != null) made++;
            }
            return made;
        }

        // outfit_change's rule (human, child or older, normal form) for the Sims on one lot
        static int ChangeOutfitsOn(Lot lot, int max, OutfitCategories cat)
        {
            int done = 0;
            foreach (Sim s in Sims3.Gameplay.Queries.GetObjects<Sim>())
            {
                if (done >= max) break;
                SimDescription sd = s == null ? null : s.SimDescription;
                if (sd == null || s.LotCurrent != lot || !sd.IsHuman || !sd.ChildOrAbove || sd.IsSupernaturalForm) continue;
                s.SwitchToOutfitWithoutSpin(Sim.ClothesChangeReason.Force, cat);
                done++;
            }
            return done;
        }

        static volatile int sVfxGen;
        static int sVfxStarted, sVfxMissing, sVfxRounds;
        static readonly string[] kVfxDefault = { "fireSystemFx", "fireSystemMidFx", "ep7WandSpellIce_main", "ep7FairyFlight_main",
            "ep11LightningHit_main", "ep8LightningHit", "ep7BuffSparkly_main", "ep7WandFail_main" };

        // vfx_burst {names?, count?, radius?, seconds?, restartS?} | {stop:true} | {status:true}: SwarmSystem load in view.
        // A game task creates `count` (default 16) effects, names round-robin (default: the game's own fire, ice spell, fairy,
        // lightning and sparkle effect names from its scripts), on a ring of `radius` m (default 8) around the camera target,
        // starts them, and every restartS (default 4) wall-clock seconds stops, disposes and re-creates them at the current
        // target, for `seconds` (default 600). Effects only: no Fire object, nothing burns, spreads or saves. An unknown name
        // counts as missing. A new vfx_burst or {stop:true} ends a running one and disposes its effects.
        static object VfxBurst(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            if (a.Has("status") || a.Has("stop"))
            {
                if (a.Has("stop")) { ++sVfxGen; r["stopped"] = true; }
                r["rounds"] = sVfxRounds; r["started"] = sVfxStarted; r["missing"] = sVfxMissing;
                return r;
            }
            RequireLive();
            int gen = ++sVfxGen;
            List<string> names = new List<string>();
            if (a.Has("names")) foreach (object o in a.List("names")) names.Add(Convert.ToString(o));
            if (names.Count == 0) names.AddRange(kVfxDefault);
            int count = (int)a.Long("count", 16), totalMs = (int)(a.Double("seconds", 600) * 1000), roundMs = (int)(a.Double("restartS", 4) * 1000);
            float radius = (float)a.Double("radius", 8);
            if (count < 1 || count > 64 || roundMs < 500) throw new McpException("count 1..64, restartS >= 0.5");
            sVfxStarted = sVfxMissing = sVfxRounds = 0;
            Simulator.AddObject(new OneShotFunctionTask(delegate
            {
                List<VisualEffect> live = new List<VisualEffect>();
                int t0 = System.Environment.TickCount;
                try
                {
                    while (sVfxGen == gen && System.Environment.TickCount - t0 < totalMs)
                    {
                        DisposeAll(live);
                        Vector3 c = CameraController.GetTarget();
                        for (int i = 0; i < count; i++)
                        {
                            VisualEffect fx = VisualEffect.Create(names[i % names.Count]);
                            if (fx == null) { sVfxMissing++; continue; }
                            double ang = 2 * Math.PI * i / count;
                            float x = c.x + radius * (float)Math.Cos(ang), z = c.z + radius * (float)Math.Sin(ang);
                            fx.SetPosAndOrient(new Vector3(x, World.GetTerrainHeight(x, z), z), Vector3.UnitZ, Vector3.UnitY);
                            fx.Start();
                            live.Add(fx);
                            sVfxStarted++;
                        }
                        sVfxRounds++;
                        int r0 = System.Environment.TickCount;
                        while (sVfxGen == gen && System.Environment.TickCount - r0 < roundMs) Simulator.Sleep(0);
                    }
                }
                finally { DisposeAll(live); }
            }));
            r["names"] = names;
            r["count"] = count;
            return r;
        }

        static void DisposeAll(List<VisualEffect> live)
        {
            foreach (VisualEffect fx in live)
            {
                try { fx.Stop(); fx.Dispose(); }
                catch (Exception) { }  // already gone with the world
            }
            live.Clear();
        }
    }
}
