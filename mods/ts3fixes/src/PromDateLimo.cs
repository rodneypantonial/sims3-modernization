using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Autonomy;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Objects.Vehicles;
using Sims3.Gameplay.Situations;

namespace Ts3Fixes
{
    // promdatelimo (tuning kPromDateLimo, off by default; docs/861). EA bug fix (player report: "my sims never get into the
    // limousine together if the date happens to be on the lot. The date will typically not get to the prom independently,
    // either. They will appear in a greeting card photo, though.")
    //
    // [verified, decompiled PromSituation.cs / CarpoolManager.cs] At 16:00 OnPushDatesToActiveHousehold sends each active-household
    // teen's prom date to the home lot (VisitLot). The prom limo (scheduled carpool, alarm 16:45) builds its rider list
    // (mRiderList, every teen on the active home lot) at dispatch; on arrival DoCarpoolPickupAndDropoff.Run pushes GetInCarpool
    // only to mRiderList, and a visitor (lot != LotHome) gets it only when its queue holds at most one visible interaction (an
    // active career: none, or an autonomous head). A date that arrives after 16:45, or is busy, is never told to get in.
    // PushAllNpcTeensToLocationOneShot (17:00 and every limo boarding) skips an NPC teen whose date is selectable: the date is
    // meant to ride the limo. The limo waits for every teen on the lot until 18:00 (LoopUpdate), then leaves; only Sims already
    // walking to it are sent to prom. The date stays home. GoToProm.TakePromPictureOneShot uses the date wherever it is.
    //
    // Fix, acting only on the date (an NPC: IsPlayerSim false; household teens and household or selected dates keep EA's code):
    //   limo: CarpoolWithDriver.SetOnCarEnteredDelegate runs once per carpool arrival, right after Run added its GetInCarpool
    //     definition and before its own pushes. For the prom limo only (the delegate's CarpoolData is PromSituation.mCarpool;
    //     school bus and career carpools return at once), each selectable rider's date that is on the pickup lot, a visitor, in
    //     the definition's Riders (so EA's Test accepts it), not a passenger and not already queued for GetInCarpool or GoToProm
    //     gets that definition's GetInCarpool, pushed as EA pushes riders who live there (UserDirected, AddNext), lowered by one
    //     as EA lowers visitors. EA may add a second copy for a date it pushes itself; boarding purges it (SimEnteredCarpool).
    //   fallback: PromSituation.AddSim (only caller GoToProm.InRabbitHole, once per entry) for a selectable teen: a date not in or
    //     walking to the limo and not queued for GoToProm is sent to prom with EA's ForceSituationSpecificInteraction, as
    //     PushAllNpcTeensToLocationOneShot sends NPC teens. Covers a date that arrived after the limo, never arrived, or failed.
    // Both hooks are field writes or list adds: no yield. Counters: [1535] dates pushed into the limo, [1536] dates sent to prom
    // on their own, [1537] limo cases seen with the switch off (EA may still push some itself), [1538] fallback cases seen with
    // the switch off (the date was left behind), [1539] errors. Check: PromDateLimoChecks.cs.
    //
    // promlimolot (tuning kPromLimoLot, off by default; docs/861 §2). EA bug fix [verified, decompiled]: AddAlarmsAndCarpool
    // fixes the limo's pickup lot (CarpoolData.mStartLotId) to the active household's home when the prom is announced, three
    // days ahead, but GetSimsRequiringCarpool and OnPushDatesToActiveHousehold read the household active on prom day. Careers
    // and schools reschedule their carpools on a move (GameplayMovingModel: RescheduleCarpool); the prom limo is never moved. After
    // a move or a switch to another household with teens, the limo parks at the old address and the current household's teens
    // are told to walk there (Run treats them as visitors of that lot), while their dates wait at the real home. Fix: when the
    // limo is dispatched (CarpoolData.PickupSims; called by its NeverPersisted alarm, so no saved delegate names the hook) the
    // prom limo's pickup lot is set to the current active household's home. It moves the limo, not a Sim; an EA bug fix, so
    // it applies to household teens as well (CLAUDE.md household rule). Counters: [1540] pickups moved to the current home,
    // [1541] the same seen with the switch off, [1542] errors.
    public static partial class Fixes
    {
        static readonly FieldInfo sPdlInstance = typeof(PromSituation).GetField("sInstance", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public);
        static readonly FieldInfo sPdlLocation = typeof(PromSituation).GetField("mLocation", kGenInst);
        static readonly MethodInfo sPdlGetDate = typeof(PromSituation).GetMethod("GetPromDate", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public, null, new Type[] { typeof(SimDescription) }, null);
        static readonly FieldInfo sPllStart = typeof(CarpoolManager.CarpoolData).GetField("mStartLotId", kGenInst);

        internal static string PdlMissing()
        {
            List<string> m = new List<string>();
            if (sPdlInstance == null) m.Add("PromSituation.sInstance");
            if (sPdlLocation == null) m.Add("PromSituation.mLocation");
            if (sPdlGetDate == null) m.Add("PromSituation.GetPromDate(SimDescription)");
            if (sPcCarpool == null) m.Add("PromSituation.mCarpool");
            if (sPllStart == null) m.Add("CarpoolData.mStartLotId");
            return string.Join(", ", m.ToArray());
        }

        static void ApplyPromDateLimo()
        {
            string missing = PdlMissing();
            if (missing.Length > 0) { AppliedLog().Add("promdatelimo: members not found (" + missing + "), not patched"); return; }
            Patch(typeof(CarpoolWithDriver), "SetOnCarEnteredDelegate", typeof(PdlCarHook), "SetHook", "SetOrig");
            Patch(typeof(PromSituation), "AddSim", typeof(PdlPromHook), "AddHook", "AddOrig");
            Patch(typeof(CarpoolManager.CarpoolData), "PickupSims", typeof(PllPickHook), "PickHook", "PickOrig");
        }

        // Pure: move the prom limo's pickup to the current home lot? (0 = no home lot)
        internal static bool PllRetarget(bool promLimo, ulong startLot, ulong homeLot)
        {
            return promLimo && homeLot != 0 && startLot != homeLot;
        }

        static void PllAtDispatch(CarpoolManager.CarpoolData cp)
        {
            PromSituation prom = PdlProm();
            if (prom == null || cp == null || !ReferenceEquals(sPcCarpool.GetValue(prom), cp)) return;  // the prom limo only
            Household hh = Household.ActiveHousehold;
            Lot home = hh != null ? hh.LotHome : null;
            if (!PllRetarget(true, cp.StartLotId, home != null ? home.LotId : 0)) return;
            if (!ShortcutOn("promlimolot")) { PatchCounters()[1541]++; return; }
            sPllStart.SetValue(cp, home.LotId);  // a readonly field: reflection writes it like any other
            PatchCounters()[cp.StartLotId == home.LotId ? 1540 : 1542]++;
        }

        public class PllPickHook
        {
            public void PickHook()
            {
                try { PllAtDispatch((CarpoolManager.CarpoolData)(object)this); }
                catch (Exception) { PatchCounters()[1542]++; }
                PickOrig();
            }
            public void PickOrig() { } // receives CarpoolManager+CarpoolData.PickupSims's original body
        }

        // Pure: push this date into the prom limo?
        internal static bool PdlLimo(bool playerDate, bool onLot, bool visitor, bool rider, bool passenger, bool queued)
        {
            return !playerDate && onLot && visitor && rider && !passenger && !queued;
        }

        // Pure: send this date to prom on its own?
        internal static bool PdlFallback(bool actorSelectable, bool playerDate, bool goingToProm, bool inLimo)
        {
            return actorSelectable && !playerDate && !goingToProm && !inLimo;
        }

        static PromSituation PdlProm() { return sPdlInstance.GetValue(null) as PromSituation; }

        static Sim PdlDate(Sim s)
        {
            if (s == null || s.SimDescription == null) return null;
            SimDescription d = sPdlGetDate.Invoke(null, new object[] { s.SimDescription }) as SimDescription;  // EA's own (skips dead dates)
            return d != null ? d.CreatedSim : null;
        }

        static bool PdlInCar(CarNpc car, Sim s) { return car != null && Array.IndexOf(car.Passengers, s) >= 0; }

        static bool PdlQueued(Sim s, InteractionDefinition d) { return s.InteractionQueue != null && s.InteractionQueue.HasInteractionOfType(d); }

        // The live inputs of PdlLimo (also run by PromDateLimoCheck on live Sims).
        internal static bool PdlLimoWants(Sim date, Lot lot, CarNpc car, List<Sim> riders)
        {
            if (date == null || date.HasBeenDestroyed || lot == null || riders == null) return false;
            return PdlLimo(IsPlayerSim(date), date.LotCurrent == lot, date.LotHome != lot, riders.Contains(date), PdlInCar(car, date),
                PdlQueued(date, CarpoolManager.GetInCarpool.Singleton) || PdlQueued(date, PromSituation.GoToProm.Singleton));
        }

        // The live inputs of PdlFallback (also run by PromDateLimoCheck on live Sims).
        internal static bool PdlFallbackWants(Sim actor, Sim date, CarNpc car)
        {
            if (actor == null || date == null || date.HasBeenDestroyed) return false;
            return PdlFallback(actor.IsSelectable, IsPlayerSim(date), PdlQueued(date, PromSituation.GoToProm.Singleton),
                PdlInCar(car, date) || PdlQueued(date, CarpoolManager.GetInCarpool.Singleton));
        }

        static CarpoolManager.GetInCarpool.Definition PdlGetIn(CarNpc car)
        {
            List<InteractionObjectPair> l = car.Interactions;
            for (int i = l.Count - 1; i >= 0; i--)  // the last one: Run added it just before setting the delegate
            {
                CarpoolManager.GetInCarpool.Definition d = l[i] != null ? l[i].InteractionDefinition as CarpoolManager.GetInCarpool.Definition : null;
                if (d != null) return d;
            }
            return null;
        }

        static void PdlAtLimo(CarpoolWithDriver car, CarpoolWithDriver.OnCarEnteredDelegate del)
        {
            PromSituation prom = PdlProm();
            if (prom == null || del == null || car == null) return;
            CarpoolManager.CarpoolData cp = sPcCarpool.GetValue(prom) as CarpoolManager.CarpoolData;
            if (cp == null || !ReferenceEquals(del.Target, cp)) return;  // the prom limo only
            CarpoolManager.GetInCarpool.Definition def = PdlGetIn(car);
            Lot lot = LotManager.GetLot(cp.StartLotId);
            if (def == null || def.Riders == null || lot == null) return;
            bool on = ShortcutOn("promdatelimo");
            foreach (Sim rider in new List<Sim>(def.Riders))
            {
                if (rider == null || rider.HasBeenDestroyed || !rider.IsSelectable) continue;  // EA's own test (PushAllNpcTeensToLocationOneShot)
                Sim date = PdlDate(rider);
                if (!PdlLimoWants(date, lot, car, def.Riders)) continue;
                if (!on) { PatchCounters()[1537]++; continue; }
                InteractionInstance ii = def.CreateInstance(car, date, new InteractionPriority(InteractionPriorityLevel.UserDirected), false, true);
                ii.LowerPriority();
                date.InteractionQueue.AddNext(ii);
                PatchCounters()[1535]++;
            }
        }

        static void PdlAtProm(Sim actor)
        {
            if (actor == null || !actor.IsSelectable) return;
            PromSituation prom = PdlProm();
            if (prom == null) return;
            Sim date = PdlDate(actor);
            CarpoolManager.CarpoolData cp = sPcCarpool.GetValue(prom) as CarpoolManager.CarpoolData;
            if (!PdlFallbackWants(actor, date, cp != null ? cp.Car : null)) return;
            if (!ShortcutOn("promdatelimo")) { PatchCounters()[1538]++; return; }
            RabbitHole loc = sPdlLocation.GetValue(prom) as RabbitHole;
            if (loc == null) return;
            prom.ForceSituationSpecificInteraction(loc, date, PromSituation.GoToProm.Singleton, null, null, null);
            PatchCounters()[1536]++;
        }

        public class PdlCarHook
        {
            public void SetHook(CarpoolWithDriver.OnCarEnteredDelegate del)
            {
                SetOrig(del);
                try { PdlAtLimo((CarpoolWithDriver)(object)this, del); }
                catch (Exception) { PatchCounters()[1539]++; }
            }
            public void SetOrig(CarpoolWithDriver.OnCarEnteredDelegate del) { } // receives CarpoolWithDriver.SetOnCarEnteredDelegate's original body
        }

        public class PdlPromHook
        {
            public static void AddHook(Sim sim)
            {
                AddOrig(sim);
                try { PdlAtProm(sim); }
                catch (Exception) { PatchCounters()[1539]++; }
            }
            public static void AddOrig(Sim sim) { } // receives PromSituation.AddSim's original body
        }
    }
}
