using System;
using System.Collections.Generic;
using System.Text;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.EventSystem;
using Sims3.SimIFace;
namespace Ts3Fixes
{
    // Problem-report lines for starting a family (v0.2.0/v0.2.1 reports: the family picker's Play arrow grays out and the
    // family never loads). In ts3native.log as "ts3fixes: play: ..." and "ts3fixes: hold: ...": when the game picks the lot
    // for the active household, every 5 s for 60 s whether the household is active yet, and if not each of its Sims with
    // their interaction and whether our unseen-fallback hold has them. Logging only.
    public static partial class Fixes
    {
        static EventListener sPlayTraceListener;
        static int sUfHoldLogged;
        const int kUfHoldLogMax = 30;

        static int sActionGen;

        internal static void StartPlayTrace()
        {
            if (sPlayTraceListener != null) EventTracker.RemoveListener(sPlayTraceListener);
            sPlayTraceListener = EventTracker.AddListener(EventTypeId.kLotChosenForActiveHousehold, OnLotChosenForActive);
            sUfHoldLogged = 0;
            ApplyPlayHooks();
            int gen = ++sActionGen;  // a watcher left from an earlier load ends at its next wake-up
            Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(delegate { ActionWatch(gen); }));
        }

        // The player's actions as mode and family changes ("ts3fixes: action: ..."): family picker, live, Build, Buy, CAS,
        // Edit Town, ... and the active household. A per-frame compare of two values; a line only on a change (at most 200).
        static void ActionWatch(int gen)
        {
            // compare values only, build strings on a change: Enum.ToString every frame churned the GC (gcN 32531 in a
            // v0.2.3 report)
            int mode = int.MinValue, lines = 0, t0 = System.Environment.TickCount;
            Household family = null;
            bool first = true;
            string modeName = "start", familyName = "start";
            while (gen == sActionGen && lines < 200)
            {
                int m;
                Household f;
                try
                {
                    m = GameStates.IsPlayFlowState ? -1 : (int)GameStates.GetCurrentSubState();
                    f = Household.ActiveHousehold;
                }
                catch (Exception) { m = -2; f = null; }
                if (first || m != mode || f != family)
                {
                    string mn = m == -1 ? "FamilyPicker" : m == -2 ? "?" : ((InWorldState.SubState)m).ToString(), fn = f != null ? f.Name : "none";
                    DiagLine("action: mode " + modeName + " -> " + mn + ", family " + familyName + " -> " + fn + " at +" + (System.Environment.TickCount - t0) / 1000 + " s, holds " + UfHolds().Count);
                    mode = m;
                    family = f;
                    modeName = mn;
                    familyName = fn;
                    first = false;
                    lines++;
                }
                Simulator.Sleep(0);
            }
        }

        // The Play path inside the game (PlayFlowModel): entry, result and exceptions of the family picker's Play
        // (PlayExisting / PlayBinHousehold). Not ActivateSimScreenMaskedFunc: patching it hid it from reflection (test agent).
        static bool sPlayHooksTried;
        static void ApplyPlayHooks()
        {
            if (sPlayHooksTried) return;
            sPlayHooksTried = true;
            try { Patch(typeof(PlayFlowModel), "PlayExisting", new Type[] { typeof(ulong) }, typeof(PlayHooks), "PlayExistingHook", "PlayExistingOrig"); } catch (Exception) { }
            try { Patch(typeof(PlayFlowModel), "PlayBinHousehold", new Type[] { typeof(Sims3.UI.GameEntry.IExportBinContents), typeof(ulong), typeof(bool) }, typeof(PlayHooks), "PlayBinHook", "PlayBinOrig"); } catch (Exception) { }
        }

        public class PlayHooks
        {
            public void PlayExistingHook(ulong lotId)
            {
                DiagLine("play: Play pressed (existing family), lot " + lotId + ", live " + GameStates.IsLiveState + ", holds " + UfHolds().Count);
                try { PlayExistingOrig(lotId); DiagLine("play: PlayExisting returned"); }
                catch (Exception e) { DiagLine("play: PlayExisting threw " + e.GetType().Name + ": " + e.Message); throw; }
            }
            public void PlayExistingOrig(ulong lotId) { }
            public void PlayBinHook(Sims3.UI.GameEntry.IExportBinContents bin, ulong lotId, bool buyFurnished)
            {
                DiagLine("play: Play pressed (bin family), lot " + lotId + ", furnished " + buyFurnished);
                try { PlayBinOrig(bin, lotId, buyFurnished); DiagLine("play: PlayBinHousehold returned"); }
                catch (Exception e) { DiagLine("play: PlayBinHousehold threw " + e.GetType().Name + ": " + e.Message); throw; }
            }
            public void PlayBinOrig(Sims3.UI.GameEntry.IExportBinContents bin, ulong lotId, bool buyFurnished) { }
            public bool MaskedHook(object param)
            {
                DiagLine("play: activating the family (screen masked)");
                try { bool ok = MaskedOrig(param); DiagLine("play: activation returned " + ok); return ok; }
                catch (Exception e) { DiagLine("play: activation threw " + e.GetType().Name + ": " + e.Message); throw; }
            }
            public bool MaskedOrig(object param) { return false; }
        }

        static ListenerAction OnLotChosenForActive(Event e)
        {
            try
            {
                Lot lot = e.TargetObject as Lot;
                Household hh = lot != null ? lot.Household : null;
                DiagLine("play: lot chosen " + (lot != null ? lot.Name : "?") + ", household " + (hh != null ? hh.Name : "?") + ", live " + GameStates.IsLiveState
                    + ", holds " + UfHolds().Count);
                Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(delegate { PlayWatch(hh); }));
            }
            catch (Exception) { }
            return ListenerAction.Keep;
        }

        // real-time polls (the clock may be paused): stops once the household is active, after 60 s, or on a new world
        static void PlayWatch(Household hh)
        {
            int t0 = System.Environment.TickCount, next = t0 + 5000;
            while (unchecked(System.Environment.TickCount - t0) < 60000)
            {
                Simulator.Sleep(0);
                if (Household.ActiveHousehold != null && Household.ActiveHousehold == hh)
                {
                    DiagLine("play: household active after " + (System.Environment.TickCount - t0) + " ms");
                    return;
                }
                if (unchecked(System.Environment.TickCount - next) < 0) continue;
                next = System.Environment.TickCount + 5000;
                StringBuilder b = new StringBuilder("play: still waiting after " + (System.Environment.TickCount - t0) + " ms, live " + GameStates.IsLiveState + ", active " + (Household.ActiveHousehold != null ? Household.ActiveHousehold.Name : "none") + ";");
                Dictionary<ulong, long> holds = UfHolds();
                if (hh != null)
                    foreach (Sim s in hh.AllActors)
                    {
                        if (s == null) continue;
                        Sims3.Gameplay.Interactions.InteractionInstance cur = s.CurrentInteraction;
                        b.Append(' ').Append(s.FullName).Append(" [").Append(cur != null ? cur.GetType().Name : "none").Append(holds.ContainsKey(s.ObjectId.Value) ? ", HELD" : "").Append(']');
                    }
                DiagLine(b.ToString());
            }
            DiagLine("play: gave up watching after 60 s");
        }

        // a hold start or end: the first kUfHoldLogMax per session, and every one that matters for a family start
        static void UfHoldLog(Sim sim, Sims3.Gameplay.Interactions.InteractionInstance cur, string what, bool always)
        {
            if (!always && sUfHoldLogged >= kUfHoldLogMax) return;
            sUfHoldLogged++;
            try { DiagLine("hold: " + sim.FullName + " [" + (cur != null ? cur.GetType().Name : "none") + "] " + what); } catch (Exception) { }
        }
    }
}
