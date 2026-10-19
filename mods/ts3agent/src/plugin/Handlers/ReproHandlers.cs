using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Situations;
using Sims3.SimIFace;
using Sims3.SimIFace.CAS;

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
            Dispatcher.Register("sim_portal_trip", SimPortalTrip);
            Dispatcher.Register("thumbnails", Thumbnails);
            Dispatcher.Register("outfit_change", OutfitChange);
            Dispatcher.Register("pet_guard", PetGuard);
            Dispatcher.Register("wildflower_drain", WildflowerDrain);
            // Reflect roots for the docs/109 §6 repros (reflect_call "@door.SetLockType", go_here {target: "@upstairs"}).
            AddRoot("route", LastLiveRoute);
            AddRoot("door", NearestHomeDoor);
            AddRoot("upstairs", NearestUpstairsObject);
            AddRoot("gangway", RepoGangway);
        }

        // The last route sim_route_to planned: reflect_call "@route.Replan" replans a route object that already holds paths.
        static Route sLastRoute;

        // "@route": sLastRoute while its native handle and follower Sim still exist (a disposed route has handle 0).
        static object LastLiveRoute()
        {
            if (sLastRoute == null) throw new McpException("no live route: run sim_route_to first");
            if (sLastRoute.RouteHandle == 0 || sLastRoute.Follower == null)
                throw new McpException("no live route: the last sim_route_to route was disposed or its Sim is gone");
            return sLastRoute;
        }
        static Sims3.Gameplay.Objects.Island.Gangway sGangway;

        // "@door": the door (CommonDoor) on the home lot nearest the active Sim.
        static object NearestHomeDoor()
        {
            Sim me = Util.ActiveSim();
            GameObject best = null;
            float bestD = float.MaxValue;
            foreach (GameObject o in Sims3.Gameplay.Queries.GetObjects<GameObject>(HomeLot()))
            {
                if (!(o is CommonDoor) || !o.InWorld) continue;
                float d = (o.Position - me.Position).LengthSqr();
                if (d < bestD) { bestD = d; best = o; }
            }
            if (best == null) throw new McpException("no door on the home lot");
            return best;
        }

        // "@upstairs": the in-world object (not a Sim) on an upper floor (Level >= 1) nearest the active Sim, on any lot.
        static object NearestUpstairsObject()
        {
            Sim me = Util.ActiveSim();
            GameObject best = null;
            float bestD = float.MaxValue;
            foreach (Lot l in LotManager.AllLots)
            {
                if (l == null) continue;
                foreach (GameObject o in Sims3.Gameplay.Queries.GetObjects<GameObject>(l))
                {
                    if (o == null || !o.InWorld || o is Sim || o is Lot || o.Level < 1) continue;
                    float d = (o.Position - me.Position).LengthSqr();
                    if (d < bestD) { bestD = d; best = o; }
                }
            }
            if (best == null) throw new McpException("no object on an upper floor");
            return best;
        }

        // "@gangway": a port gangway without a houseboat, kept so the create call (ChangeHouseboatSize "Small") and the
        // remove call ("None") use the same one. Only worlds with ports (Isla Paradiso).
        static object RepoGangway()
        {
            if (sGangway != null && sGangway.InWorld) return sGangway;
            foreach (Lot l in LotManager.AllLots)
            {
                if (l == null || !l.IsPortLot()) continue;
                Sims3.Gameplay.Objects.Island.Gangway g = l.GetGangway();
                if (g != null && g.GetHouseboat() == null) return sGangway = g;
            }
            throw new McpException("no port gangway without a houseboat");
        }

        static Sims3.Gameplay.Utilities.AlarmHandle sWfDrainAlarm = Sims3.Gameplay.Utilities.AlarmHandle.kInvalidHandle;

        // wildflower_drain {lots?: 20, every_min?: 30, clear?: false, stop?: false}: docs/110 §7.5 repro (repros.json
        // wildflower-drain), so no test waits for spring. Replaces Wildflower's spring drain list with `lots` community lot
        // ids (cycling through them, shuffled with our own seed so the game's RNG stream is untouched) and starts our own
        // repeating alarm, every `every_min` sim minutes, that calls the game's drain handler OnGrowWildflowersOnLot (as the
        // game's hourly alarm does; with ts3fixes loaded that is the wildflower hook). The game's own alarm, if live, drains
        // the same list. clear: before each fire destroy uncollected wildflowers lying in the world, else the game's
        // 20-flower cap stops the drain after 20 placed. Our alarm ends itself when the list is empty; stop removes it.
        // Returns the list size before and after, whether the game's alarm is live, can_spawn and the uncollected count.
        static object WildflowerDrain(Args a)
        {
            RequireLive();
            const BindingFlags st = BindingFlags.Static | BindingFlags.NonPublic;
            Type wt = typeof(Sims3.Gameplay.Objects.Wildflower);
            FieldInfo lotsF = wt.GetField("sLotsToGetWildflowers", st), alarmF = wt.GetField("sGrowWildflowerInSpringAlarm", st);
            FieldInfo collected = wt.GetField("mCollected", BindingFlags.Instance | BindingFlags.NonPublic);
            MethodInfo grow = wt.GetMethod("OnGrowWildflowersOnLot", st);
            if (lotsF == null || alarmF == null || collected == null || grow == null) throw new McpException("Wildflower drain members not found:" + (lotsF == null ? " sLotsToGetWildflowers" : "") + (alarmF == null ? " sGrowWildflowerInSpringAlarm" : "") + (collected == null ? " mCollected" : "") + (grow == null ? " OnGrowWildflowersOnLot" : ""));
            Sims3.Gameplay.Utilities.AlarmManager.Global.RemoveAlarm(sWfDrainAlarm);
            sWfDrainAlarm = Sims3.Gameplay.Utilities.AlarmHandle.kInvalidHandle;
            Dictionary<string, object> r = Util.Obj();
            List<ulong> before = lotsF.GetValue(null) as List<ulong>;
            r["list_before"] = before == null ? -1 : before.Count;
            if (!a.Bool("stop", false))
            {
                int n = (int)a.Long("lots", 20);
                List<Lot> comm = LotManager.GetAllCommunityLots();
                if (comm == null || comm.Count == 0) throw new McpException("no community lots in this world");
                Random rnd = new Random(110);
                List<ulong> ids = new List<ulong>();
                for (int i = 0; i < n; i++)
                {
                    if (i % comm.Count == 0)
                        for (int k = comm.Count - 1; k > 0; k--) { int j = rnd.Next(k + 1); Lot t = comm[k]; comm[k] = comm[j]; comm[j] = t; }
                    ids.Add(comm[i % comm.Count].LotId);
                }
                lotsF.SetValue(null, ids);
                bool clear = a.Bool("clear", false);
                float every = (float)a.Double("every_min", 30);
                sWfDrainAlarm = Sims3.Gameplay.Utilities.AlarmManager.Global.AddAlarmRepeating(every, Sims3.Gameplay.Utilities.TimeUnit.Minutes, delegate
                {
                    List<ulong> l = lotsF.GetValue(null) as List<ulong>;
                    if (l == null || l.Count == 0)
                    {
                        Sims3.Gameplay.Utilities.AlarmManager.Global.RemoveAlarm(sWfDrainAlarm);
                        sWfDrainAlarm = Sims3.Gameplay.Utilities.AlarmHandle.kInvalidHandle;
                        return;
                    }
                    if (clear)
                        foreach (Sims3.Gameplay.Objects.Wildflower w in Sims3.Gameplay.Queries.GetObjects<Sims3.Gameplay.Objects.Wildflower>())
                            if (w.InWorld && !(bool)collected.GetValue(w)) w.Destroy();
                    grow.Invoke(null, null);
                }, every, Sims3.Gameplay.Utilities.TimeUnit.Minutes, "ts3agent wildflower drain", Sims3.Gameplay.Utilities.AlarmType.NeverPersisted, null);
            }
            List<ulong> after = lotsF.GetValue(null) as List<ulong>;
            r["list_after"] = after == null ? -1 : after.Count;
            r["game_alarm_live"] = !((Sims3.Gameplay.Utilities.AlarmHandle)alarmF.GetValue(null)).Equals(Sims3.Gameplay.Utilities.AlarmHandle.kInvalidHandle);
            r["our_alarm_live"] = !sWfDrainAlarm.Equals(Sims3.Gameplay.Utilities.AlarmHandle.kInvalidHandle);
            r["can_spawn"] = Sims3.Gameplay.Objects.Wildflower.CanSpawnWildflowers();
            int un = 0;
            foreach (Sims3.Gameplay.Objects.Wildflower w in Sims3.Gameplay.Queries.GetObjects<Sims3.Gameplay.Objects.Wildflower>())
                if (!(bool)collected.GetValue(w)) un++;
            r["uncollected"] = un;
            return r;
        }

        // pet_guard {action: "start" | "state" | "cancel", target?}: docs/84 §7.2 fix D-X repro (repros.json pet-guard-cancel).
        // start: a pet of the active household guards an object on the home lot (PetGuardSitutation.GuardObject, the Guard
        // interaction); with no pet in the household a random adult dog is made, joins it next to the object and is tracked
        // for remove_sims. target = an object id, else a random object on the home lot (no Sim, not carried). Once the pet
        // arrives the guard adds a route discourage footprint on a hidden DynamicSlot (Route.AddDiscourageFootprint).
        // cancel: cancels the pet's interactions; the guard's Cleanup then removes the footprint and destroys the slot in the
        // same script call (Remove, then Destroy: the case D dropped). state: guarding / area present, for an "until" step.
        // ensure: base saves (spring-saves): with no pet in the household, a random adult dog joins it on the home lot and is
        // NOT tracked, so it stays in the save; returns pets (the household's pet count) for an "until" check.
        static object PetGuard(Args a)
        {
            RequireLive();
            string action = a.Str("action", "start");
            Household h = Household.ActiveHousehold;
            if (h == null || h.LotHome == null) throw new McpException("no active household with a home lot");
            Sim pet = null;
            foreach (Sim s in h.Pets) if (s != null && s.InteractionQueue != null) { pet = s; break; }
            Dictionary<string, object> r = Util.Obj();
            if (action == "ensure")
            {
                if (pet == null)
                {
                    SimDescription sd = GeneticsPet.MakeRandomPet(CASAgeGenderFlags.Adult, CASAgeGenderFlags.Female, CASAgeGenderFlags.Dog);
                    if (sd == null) throw new McpException("could not make a dog (Pets not installed?)");
                    h.Add(sd);
                    Sim near = Sim.ActiveActor; // next to the active Sim (inside the house, on the lot); the lot centre may be inside a wall
                    Vector3 pos = near != null && near.LotCurrent == h.LotHome ? near.Position : h.LotHome.GetCenterPosition();
                    pet = sd.Instantiate(pos);
                    if (pet == null) throw new McpException("dog made but not instantiated");
                    r["spawned"] = true;
                }
                int n = 0;
                foreach (Sim s in h.Pets) if (s != null) n++;
                r["pets"] = n;
                r["pet"] = pet.FullName;
                return r;
            }
            if (action == "start")
            {
                GameObject target = a.Has("target") ? Util.FindObject(a, "target") : null;
                if (target == null)
                {
                    List<GameObject> cand = h.LotHome.GetObjects<GameObject>(delegate(GameObject o)
                        { return !(o is Sim) && o.Parent == null && o.InWorld; });
                    if (cand.Count == 0) throw new McpException("no object on the home lot to guard");
                    target = RandomUtil.GetRandomObjectFromList(cand);
                }
                if (pet == null)
                {
                    SimDescription sd = GeneticsPet.MakeRandomPet(CASAgeGenderFlags.Adult, CASAgeGenderFlags.Female, CASAgeGenderFlags.Dog);
                    if (sd == null) throw new McpException("could not make a dog (Pets not installed?)");
                    h.Add(sd);
                    SpawnedSims().Add(sd.SimDescriptionId);
                    Vector3 pos = target.Position;
                    pos.x += 2f;
                    pet = sd.Instantiate(pos);
                    if (pet == null) throw new McpException("dog made but not instantiated");
                    r["spawned"] = true;
                }
                pet.InteractionQueue.CancelAllInteractions();
                InteractionInstance ii = PetGuardSitutation.GuardObject.Singleton.CreateInstance(target, pet,
                    new InteractionPriority(InteractionPriorityLevel.UserDirected), false, true);
                r["queued"] = pet.InteractionQueue.Add(ii);
                r["target"] = Util.ObjectName(target);
                r["targetId"] = Util.Id(target.ObjectId);
            }
            else if (pet == null) throw new McpException("the active household has no pet (run pet_guard start first)");
            else if (action == "cancel") pet.InteractionQueue.CancelAllInteractions();
            else if (action != "state") throw new McpException("action must be ensure, start, state or cancel");
            PetGuardSitutation.GuardObject g = pet.InteractionQueue.GetHeadInteraction() as PetGuardSitutation.GuardObject;
            FieldInfo area = typeof(PetGuardSitutation.GuardObject).GetField("mDiscourageArea", BindingFlags.NonPublic | BindingFlags.Instance);
            GameObject slot = g != null && area != null ? area.GetValue(g) as GameObject : null;
            r["pet"] = pet.FullName;
            r["guarding"] = g != null;
            r["area"] = slot != null;
            if (slot != null) r["areaId"] = Util.Id(slot.ObjectId);
            return r;
        }

        // thumbnails {lots?, household?, maxS?, status?}: the ThumbnailManager ICs that create D3D textures from the calling
        // thread (docs/43 §4, docs/51): lot thumbnails for the active home lot and the next lived-in lots (lots, default 3), the
        // active household's thumbnail (household, default true), then UpdateDirtyLotThumbnails. Follow with save_game for the
        // save path. The ICs run in a game task, one per tick, and no new one starts after maxS (default 15) seconds: the reply
        // is the plan at once, so a slow or stuck IC no longer holds the mailbox (it did in both verify runs, 2026-10-15).
        // status:true returns the last run's partial result (done, timed_out, finished, ms).
        static Dictionary<string, object> sThumbs;
        static volatile bool sThumbsBusy;

        static object Thumbnails(Args a)
        {
            if (a.Bool("status", false))
            {
                Dictionary<string, object> s = Util.Obj();
                lock (sThumbsLock) if (sThumbs != null) foreach (KeyValuePair<string, object> kv in sThumbs) s[kv.Key] = kv.Value;
                s["busy"] = sThumbsBusy;
                return s;
            }
            RequireLive();
            if (sThumbsBusy) throw new McpException("previous thumbnails run still going; thumbnails {status:true} shows it");
            int max = a.Has("lots") ? (int)a.Double("lots") : 3;
            int limitMs = (int)(a.Double("maxS", 15) * 1000);
            List<Lot> lots = new List<Lot>();
            Household h = Household.ActiveHousehold;
            if (h != null && h.LotHome != null) lots.Add(h.LotHome);
            foreach (Lot l in LotManager.AllLots)
            {
                if (lots.Count >= max) break;
                if (l != null && !l.IsWorldLot && l.Household != null && !lots.Contains(l)) lots.Add(l);
            }
            List<object> planned = new List<object>();
            foreach (Lot l in lots) planned.Add(l.Name);
            bool hh = h != null && a.Bool("household", true);
            ulong hhId = hh ? h.HouseholdId : 0;
            List<object> done = new List<object>();
            Dictionary<string, object> st = Util.Obj();
            st["lots"] = done;
            st["household"] = false;
            st["dirty_update"] = false;
            st["timed_out"] = false;
            st["finished"] = false;
            lock (sThumbsLock) sThumbs = st;
            sThumbsBusy = true;
            Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(delegate
            {
                int t0 = System.Environment.TickCount;
                try
                {
                    // one IC per tick; Over() stops before the next one once the time limit is gone
                    foreach (Lot l in lots)
                    {
                        if (Over(st, t0, limitMs)) break;
                        ThumbnailManager.GenerateLotThumbnails(l.LotId, l.LotId, ThumbnailSizeMask.All);
                        lock (sThumbsLock) done.Add(l.Name);
                        Simulator.Sleep(0);
                    }
                    if (hh && !Over(st, t0, limitMs))
                    {
                        ThumbnailManager.GenerateHouseholdThumbnail(hhId, hhId, ThumbnailSizeMask.All);
                        lock (sThumbsLock) st["household"] = true;
                        Simulator.Sleep(0);
                    }
                    if (!Over(st, t0, limitMs))
                    {
                        ThumbnailManager.UpdateDirtyLotThumbnails();
                        lock (sThumbsLock) st["dirty_update"] = true;
                    }
                }
                catch (Exception e) { lock (sThumbsLock) st["error"] = e.GetType().Name + ": " + e.Message; }
                finally
                {
                    lock (sThumbsLock) { st["ms"] = System.Environment.TickCount - t0; st["finished"] = true; }
                    sThumbsBusy = false;
                }
            }));
            Dictionary<string, object> r = Util.Obj();
            r["queued"] = true;
            r["lots"] = planned;
            r["household"] = hh;
            r["maxS"] = limitMs / 1000.0;
            return r;
        }

        static readonly object sThumbsLock = new object();

        static bool Over(Dictionary<string, object> st, int t0, int limitMs)
        {
            if (System.Environment.TickCount - t0 < limitMs) return false;
            lock (sThumbsLock) st["timed_out"] = true;
            return true;
        }

        // outfit_change {count?, category?}: several Sims change outfit at once (outfit builds through the SimService, docs/42,
        // docs/51): up to count (default 6) humans, child or older, active household first, switch to category (default
        // athletic; everyday to change back) without the spin.
        static object OutfitChange(Args a)
        {
            RequireLive();
            int max = a.Has("count") ? (int)a.Double("count") : 6;
            string name = a.Str("category", "athletic").ToLowerInvariant();
            Sims3.SimIFace.CAS.OutfitCategories cat;
            switch (name)
            {
                case "everyday": cat = Sims3.SimIFace.CAS.OutfitCategories.Everyday; break;
                case "formal": cat = Sims3.SimIFace.CAS.OutfitCategories.Formalwear; break;
                case "sleepwear": cat = Sims3.SimIFace.CAS.OutfitCategories.Sleepwear; break;
                case "swimwear": cat = Sims3.SimIFace.CAS.OutfitCategories.Swimwear; break;
                case "athletic": cat = Sims3.SimIFace.CAS.OutfitCategories.Athletic; break;
                default: throw new McpException("category: everyday, formal, sleepwear, swimwear or athletic");
            }
            List<Sim> sims = new List<Sim>();
            Household h = Household.ActiveHousehold;
            if (h != null) foreach (Sim s in h.Sims) if (sims.Count < max) sims.Add(s);
            foreach (Sim s in Sims3.Gameplay.Queries.GetObjects<Sim>())
                if (sims.Count < max && !sims.Contains(s)) sims.Add(s);
            List<object> changed = new List<object>(), skipped = new List<object>();
            foreach (Sim s in sims)
            {
                SimDescription sd = s == null ? null : s.SimDescription;
                if (sd == null) continue;
                if (!sd.IsHuman || !sd.ChildOrAbove || sd.IsSupernaturalForm) { skipped.Add(sd.FullName + ": not a human child or older in normal form"); continue; }
                s.SwitchToOutfitWithoutSpin(Sim.ClothesChangeReason.Force, cat);
                changed.Add(sd.FullName + " (" + s.CurrentOutfitCategory + ")");
            }
            Dictionary<string, object> r = Util.Obj();
            r["category"] = cat.ToString();
            r["changed"] = changed;
            r["skipped"] = skipped;
            return r;
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
            if (string.Equals(name, "home", StringComparison.OrdinalIgnoreCase)) return HomeLot();
            if (string.Equals(name, "far", StringComparison.OrdinalIgnoreCase))  // farthest community lot from the active Sim (docs/117)
            {
                Lot far = FarLot(Util.ActiveSim());
                if (far == null) throw new McpException("no community lot in this world");
                return far;
            }
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

        // sim_visit_lot {sim, lot} (lot "home", "far" or a name): the game's own visit interaction (VisitCommunityLot for community lots, VisitLot for
        // homes), autonomous priority like a townie's own choice; triggers the lot-visit shortcuts on demand.
        static object SimVisitLot(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            Lot lot = FindLotByName(a.Str("lot"));
            InteractionDefinition def = lot.IsCommunityLot ? VisitCommunityLot.Singleton : VisitLot.Singleton;
            InteractionInstance i = def.CreateInstance(lot, s, new InteractionPriority(InteractionPriorityLevel.Autonomous), true, true);
            Dictionary<string, object> r = Util.Obj();
            // vehicle: true reserves the own_vehicle object ("@vehicle"), as CarOwnable.VisitCommunityLot does before its visit
            if (a.Bool("vehicle", false))
            {
                Sims3.Gameplay.Objects.Vehicles.Vehicle v = sVehicle as Sims3.Gameplay.Objects.Vehicles.Vehicle;
                if (v == null) throw new McpException("no vehicle: own_vehicle first");
                s.SetReservedVehicle(v);
                r["vehicle"] = Util.ObjectName(v);
            }
            r["queued"] = s.InteractionQueue.Add(i);
            r["lot"] = lot.Name;
            r["interaction"] = def.GetType().DeclaringType != null ? def.GetType().DeclaringType.Name : def.GetType().Name;
            return r;
        }

        // sim_route_to {sim, x, z} or {sim, target, radius?: 1}: plan a route for any Sim to a point or to an object (any
        // FindTarget form, e.g. "@upstairs" for a plan across levels) or {sim, to: pond|ocean|far}, options?, options2? (go_here only drives the active Sim); a route-failure
        // trigger for the backoff / route-fail repros (the plan goes through the patched planner). Plans only: walking must
        // run in the Sim's own task, not the agent's. The route is kept as "@route" for reflect_call "@route.Replan".
        static object SimRouteTo(Args a)
        {
            Sim s = Util.FindSim(a, "sim");
            Route route = s.CreateRoute();
            // options / options2: extra Route.RouteOption / RouteOption2 bits by name on top of the Sim's own
            // (SetRouteOptions), e.g. ["RouteAsGhost", "PassThroughWalls"], ["EnablePlanningAsBoat"] (docs/117)
            List<string> set = new List<string>();
            foreach (object o in a.List("options") ?? new List<object>())
            {
                route.SetOption((Route.RouteOption)Enum.Parse(typeof(Route.RouteOption), (string)o, true), true);
                set.Add((string)o);
            }
            foreach (object o in a.List("options2") ?? new List<object>())
            {
                route.SetOption2((Route.RouteOption2)Enum.Parse(typeof(Route.RouteOption2), (string)o, true), true);
                set.Add((string)o);
            }
            if (a.Has("target"))
            {
                GameObject t = FindTarget(a, "target");
                route.PlanToPointRadius(t, t.Position, (float)a.Double("radius", 1.0));
            }
            else if (a.Has("to")) route.PlanToPoint(RequireRarePoint(s, a.Str("to")));  // "pond", "ocean", "far"
            else
            {
                Vector3 p = new Vector3((float)a.Double("x"), 0f, (float)a.Double("z"));
                p.y = World.GetTerrainHeight(p.x, p.z);
                route.PlanToPoint(p);
            }
            sLastRoute = route;
            Dictionary<string, object> r = Util.Obj();
            r["plan"] = route.PlanResult.mType.ToString();
            if (set.Count > 0) r["options"] = set;
            return r;
        }

        // sim_portal_trip {sim?, via?}: Go Here (user-directed, default the active Sim) into the sea (via "ocean", default:
        // 10 m past the first sea point found on rings of 10..400 m round the Sim) or onto the nearest houseboat (via
        // "houseboat"). Both routes cross a portal whose PortalAnimateThroughAction.DoPortal skips the route: the trigger
        // for the route adaptor leak (repro ip-portal-leak, docs/09 section 5). Needs a world with sea / houseboats (IP).
        static object SimPortalTrip(Args a)
        {
            Sim s = a.Has("sim") ? Util.FindSim(a, "sim") : Util.ActiveSim();
            string via = a.Has("via") ? a.Str("via").Trim().ToLowerInvariant() : "ocean";
            Vector3 pos = Vector3.Invalid;
            string what = null;
            if (via == "houseboat")
            {
                float best = float.MaxValue;
                foreach (Lot l in LotManager.AllLots)
                {
                    if (l == null || !l.IsHouseboatLot()) continue;
                    Sims3.Gameplay.Objects.Island.Houseboat hb = l.GetHouseboat();
                    if (hb == null) continue;
                    float d = (hb.Position - s.Position).LengthSqr();
                    if (d < best) { best = d; pos = hb.Position; what = l.Name; }
                }
            }
            else
            {
                pos = SeaPoint(s);
                if (pos != Vector3.Invalid) what = "sea";
            }
            if (what == null) throw new McpException(via == "houseboat" ? "no houseboat in this world" : "no sea within 400 m of " + s.FullName);
            InteractionInstance ii = Terrain.GoHere.GetSingleton(s, pos).CreateInstance(Terrain.Singleton, s,
                new InteractionPriority(InteractionPriorityLevel.UserDirected), false, true);
            Terrain.GoHere gh = ii as Terrain.GoHere;
            if (gh != null) gh.SetDestination(pos, false);
            Dictionary<string, object> r = Util.Obj();
            r["sim"] = s.FullName;
            r["to"] = what;
            r["x"] = Math.Round(pos.x, 1); r["z"] = Math.Round(pos.z, 1);
            r["queued"] = s.InteractionQueue.Add(ii);
            return r;
        }
    }
}
