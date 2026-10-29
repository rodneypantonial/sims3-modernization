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

        // The task for all our script work. Sims3.Gameplay.OneShotFunctionTask counts itself in UIManager.TaskCount for its
        // whole life, and the game's single tasks (family picker Play, Edit Town lot place) only run while that count is 1:
        // three of our world-load loops kept it at 3 and Play grayed out for good (v0.2.0-v0.2.5, traced with TaskCountHook).
        internal sealed class LoopTask : Task
        {
            readonly Sims3.Gameplay.Function mFunction;
            public LoopTask(Sims3.Gameplay.Function f) { mFunction = f; }
            public override void Simulate()
            {
                try { mFunction(); }
                catch (Exception) { }
                finally { Simulator.DestroyObject(ObjectId); }
            }
            // same text as OneShotFunctionTask: hitch lines name a task by it (OverwatchSpread's chain marks)
            public override string ToString()
            {
                return mFunction == null ? "(OneShotFunctionTask) NULL function" : "(OneShotFunctionTask) Function method: " + mFunction.Method.ToString() + ", Declaring Type: " + mFunction.Method.DeclaringType.ToString();
            }
        }

        internal static void StartPlayTrace()
        {
            if (sPlayTraceListener != null) EventTracker.RemoveListener(sPlayTraceListener);
            sPlayTraceListener = EventTracker.AddListener(EventTypeId.kLotChosenForActiveHousehold, OnLotChosenForActive);
            sUfHoldLogged = 0;
            ApplyPlayHooks();
            int gen = ++sActionGen;  // a watcher left from an earlier load ends at its next wake-up
            Simulator.AddObject(new LoopTask(delegate { ActionWatch(gen); }));
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
                    DiagLine("action: mode " + modeName + " -> " + mn + ", family " + familyName + " -> " + fn + " at +" + (System.Environment.TickCount - t0) / 1000 + " s, holds " + UfHolds().Count + ", ui tasks " + Sims3.UI.UIManager.TaskCount + (first ? " (task count hook " + sTaskHookState + ")" : ""));
                    mode = m;
                    family = f;
                    modeName = mn;
                    familyName = fn;
                    first = false;
                    lines++;
                }
                try { PlayStuckCheck(); } catch (Exception) { }
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
            // the picker's Play arrow: EnableNavButtons(false), then a OneShotFunctionTask(PlayGame) that calls PlayExisting
            try { Patch(typeof(Sims3.UI.GameEntry.PlayFlowInstructions), "EnableNavButtons", new Type[] { typeof(bool) }, typeof(PlayUiHooks), "EnableHook", "EnableOrig"); } catch (Exception) { }
            try
            {
                Type flow = typeof(Sims3.UI.GameEntry.PlayFlowController).Assembly.GetType("Sims3.UI.GameEntry.PlayFlowInWorldFlow");
                if (flow != null) Patch(flow, "PlayGame", Type.EmptyTypes, typeof(PlayUiHooks), "PlayGameHook", "PlayGameOrig");
            }
            catch (Exception) { }
        }

        // who moves UIManager.TaskCount: PlayGame is a single task that only runs once the count is 1. Patched at script load
        // (Fixes.Apply), before the setter first runs; later the patch is refused (already compiled).
        static string sTaskHookState = "not tried";
        internal static void ApplyTaskCountHook()
        {
            try
            {
                sTaskHookState = Patch(typeof(Sims3.UI.UIManager), "set_TaskCount", new Type[] { typeof(uint) }, typeof(TaskCountHook), "SetHook", "SetOrig")
                    ? "on" : "refused: " + AppliedLog()[AppliedLog().Count - 1];
            }
            catch (Exception e) { sTaskHookState = "failed: " + e.Message; }
        }

        static int sTaskLogLeft = 150;     // changes logged per session outside the Play window
        static int sTaskLogPlayUntil;      // TickCount: log every change until then (10 s after Play)

        static string Callers()
        {
            try
            {
                System.Diagnostics.StackTrace st = new System.Diagnostics.StackTrace(2, false);
                StringBuilder b = new StringBuilder();
                for (int i = 0; i < st.FrameCount && i < 5; i++)
                {
                    System.Reflection.MethodBase m = st.GetFrame(i).GetMethod();
                    if (m == null) continue;
                    if (b.Length > 0) b.Append(" < ");
                    b.Append(m.DeclaringType != null ? m.DeclaringType.Name : "?").Append('.').Append(m.Name);
                }
                return b.ToString();
            }
            catch (Exception e) { return "stack unreadable: " + e.GetType().Name; }
        }

        public static class TaskCountHook
        {
            public static void SetHook(uint value)
            {
                uint old = 0;
                try { old = Sims3.UI.UIManager.TaskCount; } catch (Exception) { }
                SetOrig(value);
                try
                {
                    bool inPlay = sTaskLogPlayUntil != 0 && unchecked(System.Environment.TickCount - sTaskLogPlayUntil) < 0;
                    bool wrapped = value > 100000u;
                    if (!wrapped && !inPlay && sTaskLogLeft <= 0) return;  // from script load: the tasks that hold Play start early
                    if (!inPlay && !wrapped) sTaskLogLeft--;
                    DiagLine("play: ui tasks " + old + " -> " + value + (wrapped ? " WRAPPED (a task ended that was counted before the reset)" : "") + " by " + Callers());
                }
                catch (Exception) { }
            }
            public static void SetOrig(uint value) { }
        }

        // the Play arrow grays the nav buttons and queues PlayGame; reports (v0.2.0-v0.2.4) show PlayExisting never called
        static int sNavOffAt;          // TickCount when the picker's nav buttons went off, 0 = on
        static bool sPlayGameRan;

        static string ChosenLot(out ulong lotId)
        {
            lotId = ulong.MaxValue;
            try
            {
                object flow = Sims3.UI.GameEntry.PlayFlowController.Singleton != null ? Sims3.UI.GameEntry.PlayFlowController.Singleton.InWorldFlow : null;
                if (flow == null) return "no in-world flow";
                System.Reflection.FieldInfo fi = flow.GetType().GetField("mChosenLotInfo", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
                Sims3.UI.GameEntry.UIBinInfo info = fi != null ? fi.GetValue(flow) as Sims3.UI.GameEntry.UIBinInfo : null;
                if (info == null) return "no chosen lot";
                lotId = info.LotId;
                return "lot " + info.LotId + ", household " + info.HouseholdName + " (" + info.HouseholdId + ")";
            }
            catch (Exception e) { return "chosen lot unreadable: " + e.GetType().Name; }
        }

        // ActionWatch calls this every frame: Play pressed (buttons off) but PlayGame never ran for 10 s -> log, start the
        // chosen family ourselves and turn the buttons back on
        static void PlayStuckCheck()
        {
            if (sNavOffAt == 0 || sPlayGameRan || !GameStates.IsPlayFlowState || unchecked(System.Environment.TickCount - sNavOffAt) < 10000) return;
            sNavOffAt = 0;
            ulong lotId;
            string chosen = ChosenLot(out lotId);
            DiagLine("play: PlayGame never ran 10 s after the buttons went off (" + chosen + "); starting the family directly");
            try { if (lotId != ulong.MaxValue && PlayFlowModel.Singleton != null) PlayFlowModel.Singleton.PlayExisting(lotId); }
            catch (Exception e) { DiagLine("play: direct start threw " + e.GetType().Name + ": " + e.Message); }
            try { if (Sims3.UI.GameEntry.PlayFlowInstructions.Singleton != null) Sims3.UI.GameEntry.PlayFlowInstructions.Singleton.EnableNavButtons(true); } catch (Exception) { }
        }

        public class PlayUiHooks
        {
            public void EnableHook(bool enable)
            {
                if (GameStates.IsPlayFlowState)
                {
                    uint tasks = 0;
                    try { tasks = Sims3.UI.UIManager.TaskCount; } catch (Exception) { }
                    DiagLine("play: picker buttons " + (enable ? "on" : "off") + ", ui tasks " + tasks + " (Play runs only at 1)");
                    if (!enable)
                    {
                        sNavOffAt = System.Environment.TickCount; if (sNavOffAt == 0) sNavOffAt = 1; sPlayGameRan = false;
                        sTaskLogPlayUntil = System.Environment.TickCount + 10000; if (sTaskLogPlayUntil == 0) sTaskLogPlayUntil = 1;
                    }
                    else sNavOffAt = 0;
                }
                EnableOrig(enable);
            }
            public void EnableOrig(bool enable) { }
            public void PlayGameHook()
            {
                sPlayGameRan = true;
                ulong lotId;
                DiagLine("play: PlayGame task ran, picker state " + GameStates.IsPlayFlowState + ", " + ChosenLot(out lotId));
                try { PlayGameOrig(); DiagLine("play: PlayGame returned"); }
                catch (Exception e) { DiagLine("play: PlayGame threw " + e.GetType().Name + ": " + e.Message); throw; }
            }
            public void PlayGameOrig() { }
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
                Simulator.AddObject(new LoopTask(delegate { PlayWatch(hh); }));
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
