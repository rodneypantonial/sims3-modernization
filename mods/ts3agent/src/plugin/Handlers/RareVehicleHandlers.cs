using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Objects.Vehicles;
using Sims3.Gameplay.Socializing;
using Sims3.SimIFace;
using Sims3.SimIFace.CAS;
using Sims3.SimIFace.BuildBuy;

namespace Ts3Agent
{
    // Owned vehicles, a stroller with a child and a ridden horse for the docs/117 rare routing repros (§2.12-2.14). The
    // routing itself is the game's: a Sim with an owned, usable vehicle gets it from Sim.GetVehicle when the planner returns
    // a vehicle path (reserved, then preferred, then inventory, then parked), so these commands only set up the objects.
    public static partial class Handlers
    {
        static void RegisterRareVehicles()
        {
            Dispatcher.Register("own_vehicle", OwnVehicle);
            Dispatcher.Register("make_sim", MakeSim);
            Dispatcher.Register("stroll", Stroll);
            Dispatcher.Register("ride", Ride);
            AddRoot("vehicle", delegate { return sVehicle; });
            AddRoot("made", delegate { return sMade; });
        }

        static GameObject sVehicle; // last own_vehicle result ("@vehicle")
        static Sim sMade;           // last make_sim result ("@made")

        // Vehicle objects to try for a kind: the game's purchasable list (ParkingSpace.sPurchasableVehicleInfo, the parking
        // space Buy Vehicle dialog) for cars and bikes, catalog products named *stroller* for strollers; then fixed names.
        static GameObject CreateVehicle(string kind, string name)
        {
            List<object> tries = new List<object>();
            if (name != null) tries.Add(name);
            else if (kind == "stroller")
            {
                foreach (BuildBuyProduct p in Catalog())
                    if ((p.ObjectInstanceName ?? "").ToLowerInvariant().IndexOf("stroller") >= 0) tries.Add(p.ProductResourceKey);
                tries.Add("StrollerCheap"); // [inferred] name of the EP4 stroller, last resort
            }
            else
            {
                List<Vehicle.VehicleInfo> infos = new List<Vehicle.VehicleInfo>(ParkingSpace.sPurchasableVehicleInfo);
                infos.Sort(delegate (Vehicle.VehicleInfo x, Vehicle.VehicleInfo y) { return x.Cost.CompareTo(y.Cost); });
                foreach (Vehicle.VehicleInfo vi in infos) tries.Add(vi);
                if (kind == "bike") tries.Add("BicycleAdultStreet");
            }
            foreach (object t in tries)
            {
                GameObject go;
                try
                {
                    Vehicle.VehicleInfo vi = t as Vehicle.VehicleInfo;
                    go = (vi != null ? GlobalFunctions.CreateObjectOutOfWorld(vi.MedatorName, vi.ProdVersion)
                        : t is ResourceKey ? GlobalFunctions.CreateObjectOutOfWorld((ResourceKey)t)
                        : GlobalFunctions.CreateObjectOutOfWorld((string)t)) as GameObject;
                }
                catch (Exception) { continue; }
                if (go == null) continue;
                bool fits = name != null ? go is IOwnableVehicle
                    : kind == "car" ? go is CarOwnable
                    : kind == "bike" ? go is AdultBicycle && !(go is Motorcycle) && !(go is Scooter)
                    : go is Stroller;
                if (fits) return go;
                go.Destroy();
            }
            return null;
        }

        // own_vehicle {kind: car | bike | stroller | broom, sim?, name?}: an owned vehicle for the Sim, as the parking space
        // Buy Vehicle flow makes one (owner lot = the Sim's home, so Sim.CanUseVehicleRightNow accepts it) without the price;
        // into the Sim's own inventory (the family inventory does not count), else placed next to the Sim; set as the Sim's
        // preferred vehicle. broom: Sim.GrantWitchTraitBroom(true), the hidden inventory broom witches get. name: an object
        // instance name instead of the search. The vehicle is "@vehicle" for sim_visit_lot {vehicle: true} and stroll.
        static object OwnVehicle(Args a)
        {
            RequireLive();
            Sim s = Util.FindSim(a, "sim");
            string kind = a.Str("kind", "car").ToLowerInvariant();
            GameObject go;
            string where;
            if (kind == "broom")
            {
                s.GrantWitchTraitBroom(true);
                go = s.Inventory.Find<MagicBroom>() as GameObject;
                if (go == null) throw new McpException("no broom in " + s.FullName + "'s inventory after GrantWitchTraitBroom (Supernatural installed?)");
                where = "inventory (hidden broom)";
            }
            else
            {
                if (kind != "car" && kind != "bike" && kind != "stroller") throw new McpException("kind must be car, bike, stroller or broom");
                if (s.LotHome == null) throw new McpException(s.FullName + " has no home lot to own a vehicle on");
                go = CreateVehicle(kind, a.Has("name") ? a.Str("name") : null);
                if (go == null) throw new McpException("no " + kind + " object could be created (expansion not installed?)");
                go.SetOwnerLot(s.LotHome);
                if (s.Inventory.TryToAdd(go)) where = "inventory";
                else
                {
                    Vector3 pos = s.Position, fwd = Vector3.UnitZ;
                    if (!GlobalFunctions.FindGoodLocationNearby(go, ref pos, ref fwd)) { go.Destroy(); throw new McpException("no room for the " + kind); }
                    go.SetPosition(pos); go.SetForward(fwd); go.AddToWorld();
                    where = "placed";
                }
            }
            IOwnableVehicle ov = go as IOwnableVehicle;
            if (ov != null) s.SetPreferredVehicle(ov);
            sVehicle = go;
            Dictionary<string, object> r = Util.Obj();
            r["sim"] = s.FullName;
            r["vehicle"] = Util.ObjectName(go);
            r["type"] = go.GetType().Name;
            r["where"] = where;
            r["preferred"] = ov != null;
            r["can_drive"] = s.SimDescription.CanDrive;
            return r;
        }

        // make_sim {age?: Toddler, species?: Human, gender?: Female}: Sim.MakeRandomSim (human) or Sim.MakePet (dog, cat,
        // horse...) at the active Sim's position, which joins the household of the lot there (the active household when the
        // active Sim is at home; household caps 8 humans / 6 pets apply). A horse gets liking 50 with the active Sim, above
        // the Mount test (5) and accept (10) limits. The new Sim is "@made" for stroll and ride.
        static object MakeSim(Args a)
        {
            RequireLive();
            Sim me = Util.ActiveSim();
            CASAgeGenderFlags age = (CASAgeGenderFlags)Enum.Parse(typeof(CASAgeGenderFlags), a.Str("age", "Toddler"), true);
            CASAgeGenderFlags gender = (CASAgeGenderFlags)Enum.Parse(typeof(CASAgeGenderFlags), a.Str("gender", "Female"), true);
            CASAgeGenderFlags species = (CASAgeGenderFlags)Enum.Parse(typeof(CASAgeGenderFlags), a.Str("species", "Human"), true);
            Sim made = species == CASAgeGenderFlags.Human ? Sim.MakeRandomSim(me.Position, age, gender) : Sim.MakePet(me.Position, age, gender, species);
            if (made == null) throw new McpException("the game made no " + species + " " + age + " (expansion not installed or household full?)");
            sMade = made;
            Dictionary<string, object> r = Util.Obj();
            if (species == CASAgeGenderFlags.Horse)
            {
                Relationship rel = Relationship.Get(me, made, true);
                if (rel != null) { rel.LTR.SetLiking(50f); r["liking"] = rel.LTR.Liking; }
            }
            r["sim"] = made.FullName;
            r["id"] = Util.Id(made.ObjectId);
            r["household"] = made.Household != null ? made.Household.Name : null;
            r["active_household"] = made.Household != null && made.Household == Household.ActiveHousehold;
            return r;
        }

        // stroll {child?: "@made", sim?}: Stroller.GoOnStrollSocial (the public variant of Go On Stroll, no Sim picker) with
        // the "@vehicle" stroller: the Sim puts the baby or toddler in and walks to a lot 0..360 m away for about 2 hours.
        static object Stroll(Args a)
        {
            RequireLive();
            Sim s = Util.FindSim(a, "sim");
            Stroller st = sVehicle as Stroller;
            if (st == null) throw new McpException("no stroller: own_vehicle {kind: stroller} first");
            Sim child = a.Has("child") ? FindTarget(a, "child") as Sim : sMade;
            if (child == null) throw new McpException("no child: make_sim {age: Toddler} first or pass child");
            InteractionInstance ii = Stroller.GoOnStrollSocial.Singleton.CreateInstance(child, s, new InteractionPriority(InteractionPriorityLevel.UserDirected), false, true);
            Stroller.GoOnStrollSocial g = ii as Stroller.GoOnStrollSocial;
            if (g == null) throw new McpException("the game made no Go On Stroll instance");
            g.Stroller = st;
            Dictionary<string, object> r = Util.Obj();
            r["queued"] = s.InteractionQueue.Add(ii);
            r["child"] = child.FullName;
            return r;
        }

        // ride {horse?: "@made", target | to | x,z}: not riding yet -> queue Sim.Mount on the horse (call again after it ran);
        // riding -> queue Terrain.RideHere (the tests-skipping definition; plain Go Here is refused while mounted) to the goal.
        static object Ride(Args a)
        {
            RequireLive();
            Sim me = Util.ActiveSim();
            Dictionary<string, object> r = Util.Obj();
            InteractionPriority prio = new InteractionPriority(InteractionPriorityLevel.UserDirected);
            if (!me.IsInRidingPosture)
            {
                Sim horse = a.Has("horse") ? FindTarget(a, "horse") as Sim : sMade;
                if (horse == null) throw new McpException("no horse: make_sim {species: Horse, age: Adult} first or pass horse");
                r["mount"] = me.InteractionQueue.Add(Sim.Mount.Singleton.CreateInstance(horse, me, prio, false, true));
                r["horse"] = horse.FullName;
                return r;
            }
            Vector3 goal = WalkGoal(a, me);
            if (goal == Vector3.Invalid) throw new McpException("give target, to or x,z");
            InteractionInstance ii = Terrain.RideHere.SkipGoHereTestsSingleton.CreateInstance(Terrain.Singleton, me, prio, false, true);
            Terrain.RideHere rh = ii as Terrain.RideHere;
            if (rh == null) throw new McpException("the game made no Ride Here instance");
            rh.SetDestination(goal);
            r["ride"] = me.InteractionQueue.Add(ii);
            r["x"] = Util.Round(goal.x); r["z"] = Util.Round(goal.z);
            return r;
        }
    }
}
