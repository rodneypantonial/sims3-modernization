using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay;
using Sims3.Gameplay.Academics;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Objects.Miscellaneous;
using Sims3.Gameplay.Visa;
using Sims3.SimIFace;

namespace Ts3Agent
{
    // Travel between worlds (docs/19): the game's own boarding interactions, queued without the trip planner / phone UI.
    // Every command returns at once; the world switch runs as game tasks and the harness (tools/harness/travel-trip.ts)
    // waits on travel_state / the mailbox world flag. The mailbox is a static of the loader, so it survives the switch.
    public static partial class Handlers
    {
        static void RegisterTravel()
        {
            Dispatcher.Register("travel_vacation", TravelVacation);
            Dispatcher.Register("travel_university", TravelUniversity);
            Dispatcher.Register("travel_future", TravelFuture);
            Dispatcher.Register("travel_home", TravelHome);
            Dispatcher.Register("travel_state", TravelState);
        }

        static bool AwayFromHome()
        {
            return GameUtils.IsAnyTravelBasedWorld() || GameUtils.IsFutureWorld();
        }

        static void CheckCanLeave()
        {
            if (GameStates.IsTravelling) throw new McpException("a world switch is already running");
            if (AwayFromHome()) throw new McpException("already away from the home world (" + GameUtils.GetCurrentWorld() + "); travel_home first");
        }

        // TravelUtil.CheckForReasonsToFailTravel is private; FinalBoardingCall cancels the whole trip if any traveller fails it.
        static string TravelFailReason(SimDescription sd, WorldName world)
        {
            MethodInfo m = typeof(TravelUtil).GetMethod("CheckForReasonsToFailTravel", BindingFlags.NonPublic | BindingFlags.Static, null,
                new Type[] { typeof(SimDescription), typeof(WorldName), typeof(bool) }, null);
            return (string)m.Invoke(null, new object[] { sd, world, false });
        }

        // sims: names/ids (default: every active-household Sim in the world). The lead (first) is the active Sim when it travels.
        static List<Sim> Travellers(Args a, WorldName world, List<object> skipped)
        {
            Sim active = Util.ActiveSim();
            List<Sim> wanted = new List<Sim>();
            List<object> names = a.List("sims");
            if (names != null)
            {
                foreach (object n in names)
                {
                    Dictionary<string, object> d = Util.Obj();
                    d["sim"] = n;
                    wanted.Add(Util.FindSim(new Args(d), "sim"));
                }
            }
            else
            {
                foreach (Sim s in active.Household.AllActors)
                    if (!Household.RoommateManager.IsNPCRoommate(s.SimDescription.SimDescriptionId)) wanted.Add(s);
            }
            List<Sim> list = new List<Sim>();
            foreach (Sim s in wanted)
            {
                string why = TravelFailReason(s.SimDescription, world);
                if (!string.IsNullOrEmpty(why)) { skipped.Add(s.FullName + ": " + why); continue; }
                if (s == active) list.Insert(0, s); else list.Add(s);
            }
            if (list.Count == 0) throw new McpException("nobody can travel: " + string.Join("; ", skipped.ConvertAll(o => (string)o).ToArray()));
            return list;
        }

        static WorldName VacationWorld(string name)
        {
            switch (name.Trim().ToLowerInvariant())
            {
                case "france": case "champs les sims": return WorldName.France;
                case "china": case "shang simla": return WorldName.China;
                case "egypt": case "al simhara": return WorldName.Egypt;
            }
            throw new McpException("world must be france, china or egypt");
        }

        // travel_vacation {world: france|china|egypt, days? (2), sims?, cost? (0)}: SitInCarToTriggerTravel, the interaction
        // TravelUtil.TriggerTravelToVacationWorld queues after the trip planner. Targeting the world lot skips the walk to the
        // farthest lot, so the switch starts on the next tick (same FinalBoardingCall + GameStates.TravelToVacationWorld).
        // Skips the planner's visa/duration limits and the ITF "travel with" warning.
        static object TravelVacation(Args a)
        {
            CheckCanLeave();
            WorldName world = VacationWorld(a.Str("world"));
            List<object> skipped = new List<object>();
            List<Sim> sims = Travellers(a, world, skipped);
            Sim lead = sims[0];
            List<ulong> ids = new List<ulong>();
            foreach (Sim s in sims) ids.Add(s.SimDescription.SimDescriptionId);
            TravelUtil.SitInCarToTriggerTravel trip = TravelUtil.SitInCarToTriggerTravel.Singleton.CreateInstance(LotManager.GetWorldLot(), lead,
                new InteractionPriority(InteractionPriorityLevel.High), false, false) as TravelUtil.SitInCarToTriggerTravel;
            trip.IsGoingToUniversity = false;
            trip.Followers = sims.GetRange(1, sims.Count - 1);
            trip.TravelingSimGuids = ids;
            trip.TravelCost = (int)a.Double("cost", 0);
            trip.TravelDuration = (int)a.Double("days", 2);
            trip.DestinationWorldName = world;
            lead.InteractionQueue.CancelAllInteractions();
            Dictionary<string, object> r = Util.Obj();
            r["queued"] = lead.InteractionQueue.Add(trip);
            r["world"] = world.ToString();
            r["days"] = trip.TravelDuration;
            r["sims"] = sims.ConvertAll(s => (object)s.FullName);
            r["skipped"] = skipped;
            return r;
        }

        // travel_university {days?}: the phone's "Enroll for University" path: AcademicCareer.EnrollInAcademicCareer (opens the
        // enrolment dialog: answer it with auto_dialog / respond_dialog), then TravelUtil.TriggerTravelToUniversityWorld (moving
        // van). days overrides the van's trip length (default: the chosen term length x 7). Runs as a game task (modal dialog).
        static object TravelUniversity(Args a)
        {
            CheckCanLeave();
            Sim lead = Util.ActiveSim();
            GreyedOutTooltipCallback why = null;
            if (!TravelUtil.CanSimTriggerTravelToUniversityWorld(lead, ref why))
                throw new McpException(lead.FullName + " cannot go to University now" + (why != null ? ": " + why() : ""));
            int days = a.Has("days") ? (int)a.Double("days") : 0;
            RunAsync("travel_university", delegate
            {
                List<SimDescription> enrolled;
                int tuition;
                if (!AcademicCareer.EnrollInAcademicCareer(lead, out enrolled, out tuition) || enrolled == null)
                    throw new Exception("enrolment cancelled");
                TravelUtil.TriggerTravelToUniversityWorld(enrolled, tuition);
                if (days <= 0) return;
                foreach (SimDescription sd in enrolled)
                {
                    if (sd.CreatedSim == null) continue;
                    foreach (InteractionInstance i in sd.CreatedSim.InteractionQueue.InteractionList)
                    {
                        TravelUtil.SitInMovingVanToTriggerTravelToUniversity van = i as TravelUtil.SitInMovingVanToTriggerTravelToUniversity;
                        if (van != null) van.TravelDuration = days;
                    }
                }
            });
            Dictionary<string, object> r = Util.Obj();
            r["enrolling"] = lead.FullName;
            r["days"] = days > 0 ? (object)days : "term";
            return r;
        }

        static TimePortal FindTimePortal()
        {
            foreach (TimePortal p in Sims3.Gameplay.Queries.GetObjects<TimePortal>()) return p;
            throw new McpException("no time portal in this world");
        }

        // The portal's own "Travel" interaction (private TimePortal.Travel): confirm dialog (auto_dialog), walk in, then
        // TimePortal.TravelToFuture -> GameStates.TravelToVacationWorld(FutureWorld); in the future world it returns home.
        static object QueuePortalTravel(Sim s)
        {
            TimePortal portal = FindTimePortal();
            Type travel = typeof(TimePortal).GetNestedType("Travel", BindingFlags.NonPublic);
            InteractionDefinition def = (InteractionDefinition)travel.GetField("Singleton", BindingFlags.Public | BindingFlags.Static).GetValue(null);
            InteractionInstance i = def.CreateInstance(portal, s, new InteractionPriority(InteractionPriorityLevel.High), false, true);
            s.InteractionQueue.CancelAllInteractions();
            Dictionary<string, object> r = Util.Obj();
            r["queued"] = s.InteractionQueue.Add(i);
            r["sim"] = s.FullName;
            // the interaction's Test (not run here) also needs these; false means the game would grey it out
            r["portal_active"] = portal.Active;
            r["portal_broken"] = portal.IsBroken;
            r["traveler_summoned"] = portal.HasTimeTravelerBeenSummoned();
            return r;
        }

        // travel_future: the active Sim alone through the time portal (TravelWith, the group version, is not wired).
        static object TravelFuture(Args a)
        {
            CheckCanLeave();
            Sim s = Util.ActiveSim();
            GreyedOutTooltipCallback why = null;
            if (!TravelUtil.CanSimTriggerTravelToFutureWorld(s, ref why))
                throw new McpException(s.FullName + " cannot travel to the future now" + (why != null ? ": " + why() : ""));
            return QueuePortalTravel(s);
        }

        // travel_home: everyone returns early, the way the game's own UI does it per world type.
        static object TravelHome(Args a)
        {
            if (GameStates.IsTravelling) throw new McpException("a world switch is already running");
            Sim s = Util.ActiveSim();
            if (GameUtils.IsFutureWorld()) return QueuePortalTravel(s);
            Dictionary<string, object> r = Util.Obj();
            if (GameUtils.IsUniversityWorld())
            {
                List<Sim> all = new List<Sim>(Household.ActiveHousehold.AllActors);
                TravelUtil.TriggerTravelHomeFromUniversity(all, new List<Sim>());
                r["via"] = "TriggerTravelHomeFromUniversity";
            }
            else if (GameUtils.IsOnVacation())
            {
                GreyedOutTooltipCallback why = null;
                if (!TravelUtil.CanSimTriggerTravelToHomeWorld(s, ref why))
                    throw new McpException("game refuses the trip home" + (why != null ? ": " + why() : ""));
                s.InteractionQueue.CancelAllInteractions();
                TravelUtil.TriggerTravelToHomeWorld(s, true, null);
                r["via"] = "TriggerTravelToHomeWorld";
            }
            else throw new McpException("not away from home (" + GameUtils.GetCurrentWorld() + ")");
            r["sim"] = s.FullName;
            return r;
        }

        static int MiniSimCount()
        {
            FieldInfo f = typeof(MiniSimDescription).GetField("sMiniSims", BindingFlags.NonPublic | BindingFlags.Static);
            ICollection d = f != null ? f.GetValue(null) as ICollection : null;
            return d != null ? d.Count : 0;
        }

        // travel_state: where the game is and how the trip stands; readable during most of a switch (check in_travel).
        static object TravelState(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            r["in_world"] = GameStates.IsInWorld();
            r["in_travel"] = GameStates.IsTravelling;
            r["world"] = GameUtils.GetCurrentWorld().ToString();
            r["world_type"] = GameUtils.GetCurrentWorldType().ToString();
            r["world_file"] = World.GetWorldFileName();
            r["away"] = AwayFromHome();
            r["destination"] = GameStates.DestinationTravelWorld.ToString();
            r["on_vacation"] = GameStates.IsOnVacation;
            int len = GameStates.TripLength, day = GameStates.CurrentDayOfTrip;
            r["trip_length"] = len;
            r["day_of_trip"] = day;
            r["days_left"] = len > 0 ? (object)Math.Max(0, len - day) : null;
            r["minisims"] = MiniSimCount();
            r["sims_in_world"] = SimDescription.GetSimDescriptionsInWorld().Count;
            Household hh = Household.ActiveHousehold;
            r["household"] = hh != null ? hh.Name : null;
            if (hh != null)
            {
                int inWorld = 0;
                foreach (SimDescription sd in hh.AllSimDescriptions) if (sd.CreatedSim != null) inWorld++;
                r["household_members"] = hh.AllSimDescriptions.Count;
                r["household_in_world"] = inWorld;
            }
            List<ulong> ids = GameStates.TravelerIds;
            r["travelers"] = ids != null ? ids.Count : 0;
            return r;
        }
    }
}
