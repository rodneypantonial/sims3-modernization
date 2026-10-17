using System;
using System.Reflection;
using System.Runtime.InteropServices;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Script hot spots of docs/74 (PLAN gap 8): small caches and early-outs on game methods that the managed samples show
    // hot, each behind its own tuning (default off) and test switch. Game-independent pieces live in TickMemo.cs.
    public static partial class Fixes
    {
        // Verify switches (detailprioverify, hoursmemoverify) check one call in kVerifyEvery against the game's own method,
        // so an A/B half with verify on still shows most of the saving. A checked call returns the game's answer.
        const int kVerifyEvery = 8;
        static int sVerifyCalls;
        static bool VerifyNow(string name) { return (++sVerifyCalls % kVerifyEvery) == 0 && ShortcutOn(name); }

        static bool Paused() { return Sims3.Gameplay.Gameflow.CurrentGameSpeed == Sims3.Gameplay.Gameflow.GameSpeed.Pause; }

        static bool sDetailPrioReady;
        static readonly TickStamp sDetailStamp = new TickStamp();
        static readonly RefSet sDetailLots = new RefSet();

        static void ApplyScriptHotSpots()
        {
            sDetailPrioReady = Patch(typeof(Lot), "CalculateDetailPriority", Type.EmptyTypes, typeof(DetailPriorityHook), "CalcHook", "CalcOrig");
            sCaneEarlyReady = Patch(typeof(Sims3.Gameplay.Objects.Cane), "SimRequiresCaneWalk", new Type[] { typeof(Sim), typeof(Sim.WalkStyle) }, typeof(CaneEarlyOutHook), "RequiresHook", "RequiresOrig");
            sHoursMemoReady = Patch(typeof(Lot), "GetHoursOfOperation", new Type[] { typeof(float).MakeByRefType(), typeof(float).MakeByRefType() }, typeof(HoursMemoHook), "HoursHook", "HoursOrig");
            // docs/74 I-74-7: was the tooltip task already running when the patches went in? Logged for the first load.
            try
            {
                MethodInfo sim = typeof(Sims3.UI.TooltipManager).GetMethod("Simulate", BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly);
                AppliedLog().Add("TooltipManager.Simulate at patch time: " + (Marshal.ReadInt32(new IntPtr(sim.MethodHandle.Value.ToInt32() + 0x14)) != 0 ? "already running" : "not started"));
            }
            catch (Exception e) { AppliedLog().Add("TooltipManager.Simulate at patch time: " + e.Message); }
            sTooltipReady = Patch(typeof(Sims3.UI.UIManager), "GetWindowFromPoint", new Type[] { typeof(Vector2) }, typeof(TooltipThrottleHook), "FromPointHook", "FromPointOrig");
        }

        static bool sTooltipReady;
        static readonly PollGate sTooltipGate = new PollGate();
        static ulong sTooltipTask, sOtherTask;
        static bool sTooltipKnown, sOtherKnown;

        // True when the running script task is the UI's TooltipManager (its id learned on first sight; one other id cached).
        static bool InTooltipTask()
        {
            ulong t = Simulator.CurrentTask.Value;
            if (sTooltipKnown && t == sTooltipTask) return true;
            if (sOtherKnown && t == sOtherTask) return false;
            bool tip;
            try { tip = Simulator.GetTask(new ObjectGuid(t)) is Sims3.UI.TooltipManager; } catch (Exception) { tip = false; }
            if (tip) { sTooltipTask = t; sTooltipKnown = true; } else { sOtherTask = t; sOtherKnown = true; }
            return tip;
        }

        // docs/74 §5, switch tooltipthrottle (tuning kTooltipThrottle). The UI's TooltipManager task loops once per sim
        // tick: it asks UIManager.GetWindowFromPoint for the window under the cursor, then walks that window's parent chain
        // asking each whether it shows a tooltip (a tooltip-callback lookup and a tooltip-text string read per window).
        // When the window lookup answers null, the loop just sleeps one tick and changes nothing. So the hook answers null
        // to the tooltip task only, while the cursor stands on the point of its last walk and less than 100 ms passed:
        // the walk runs at most ~10 times a second with the mouse at rest, and as before when it moves. Other callers
        // (drag and drop, build/buy, the interaction queue) always get the game's answer. Tooltips may show or hide up to
        // 100 ms later. Hooking the window lookup instead of TooltipManager.Simulate avoids replacing a running endless
        // loop (I-74-7): if the lookup was already compiled at patch time the patch is refused, ts3fixes.applied says so,
        // and the game runs unchanged. Counters: [88] walks skipped, [89] walks run.
        public static class TooltipThrottleHook
        {
            public static Sims3.UI.WindowBase FromPointHook(Vector2 pos)
            {
                if (!sTooltipReady || !ShortcutOn("tooltipthrottle")) return FromPointOrig(pos);
                int now = System.Environment.TickCount;
                if (sTooltipGate.Skip(pos.x, pos.y, now) && InTooltipTask()) { PatchCounters()[88]++; return null; }
                Sims3.UI.WindowBase w = FromPointOrig(pos);
                if (InTooltipTask()) { sTooltipGate.Ran(pos.x, pos.y, now); PatchCounters()[89]++; }
                return w;
            }
            public static Sims3.UI.WindowBase FromPointOrig(Vector2 pos) { return null; } // receives Sims3.UI.UIManager.GetWindowFromPoint's original header
        }

        static bool sCaneEarlyReady;

        // docs/74 §6.2, switch caneearly (tuning kCaneEarlyOut). Cane.SimRequiresCaneWalk(sim, style), asked per route
        // sub-path and portal by the walk-style rules, first searches the Sim's inventory for a cane in use and only then
        // looks at the style; it can only answer true for Run, FastWalk, FastJog, Jog and FastRun [verified]. For any other
        // style the answer is false whatever the inventory holds, and the search has no side effects, so the hook answers
        // false at once. Same answer in every case. Counters: [83] early answers, [84] calls passed to the game's method.
        public static class CaneEarlyOutHook
        {
            public static bool RequiresHook(Sim sim, Sim.WalkStyle walkStyle)
            {
                switch (walkStyle)
                {
                    case Sim.WalkStyle.Run:
                    case Sim.WalkStyle.FastWalk:
                    case Sim.WalkStyle.FastJog:
                    case Sim.WalkStyle.Jog:
                    case Sim.WalkStyle.FastRun:
                        break;
                    default:
                        if (sCaneEarlyReady && ShortcutOn("caneearly")) { PatchCounters()[83]++; return false; }
                        break;
                }
                PatchCounters()[84]++;
                return RequiresOrig(sim, walkStyle);
            }
            public static bool RequiresOrig(Sim sim, Sim.WalkStyle walkStyle) { return false; } // receives Cane.SimRequiresCaneWalk's original header
        }

        static bool sHoursMemoReady;
        static readonly HoursMemo sHours = new HoursMemo();

        // docs/74 §6.3, switch hoursmemo (tuning kHoursMemo). Lot.GetHoursOfOperation(ref open, ref close) is asked by
        // IsOpenVenue (camera, autonomy, route plans, doors, venue visits) many times per tick per lot; each call reads the
        // venue's meta-autonomy tuning and bar data and, on a bar, scans every live situation for a started bachelor party
        // on the lot. The answer (and the hours when true) is kept per lot for one sim tick (TickStamp rules, nothing kept
        // while paused); a hit writes the outputs only when the answer was true, as the game's method. Only difference: a
        // bachelor party starting on that lot in the middle of a tick is seen one tick later; hoursmemoverify counts it.
        // Counters: [85] memo hits, [86] misses (game's method run and stored), [87] verify mismatches.
        public class HoursMemoHook
        {
            public bool HoursHook(ref float openHour, ref float closingHour)
            {
                if (!sHoursMemoReady || Paused() || !ShortcutOn("hoursmemo")) { sHours.Stamp.Reset(); return HoursOrig(ref openHour, ref closingHour); }
                if (sHours.Stamp.Renew(Simulator.TicksElapsed(), null, System.Environment.TickCount)) sHours.Clear();
                ulong id = ((Lot)(object)this).LotId;
                int[] c = PatchCounters();
                float open = openHour, close = closingHour;
                bool res;
                if (sHours.TryGet(id, ref open, ref close, out res))
                {
                    c[85]++;
                    if (VerifyNow("hoursmemoverify"))
                    {
                        float gOpen = openHour, gClose = closingHour;
                        bool g = HoursOrig(ref gOpen, ref gClose);
                        if (g != res || gOpen != open || gClose != close) c[87]++;
                        openHour = gOpen; closingHour = gClose;
                        return g;
                    }
                    openHour = open; closingHour = close;
                    return res;
                }
                c[86]++;
                res = HoursOrig(ref openHour, ref closingHour);
                sHours.Store(id, res, openHour, closingHour);
                return res;
            }
            public bool HoursOrig(ref float openHour, ref float closingHour) { return false; } // receives Lot.GetHoursOfOperation(ref float, ref float)'s original header
        }

        // docs/74 §6.1, switch detailprio (tuning kDetailPriorityCache). Every lot's Simulate pass hands the native world a
        // detail priority: Home for the active household's home lot, PlayerSim when an active-household Sim stands on the
        // lot outside a rabbit hole, else None. The game finds PlayerSim by building the household's actor list for every
        // lot on every tick. Here the Home test stays live and the actor walk is done once per sim tick into a set of lots
        // (TickStamp rules; household change rebuilds; nothing kept while paused). World.LotSetDetailPriority is still
        // called on every pass by the game's UpdateDetailPriority. Only difference: a household Sim changing lot in the
        // middle of a tick is seen by lots simulated later in that tick one tick late; detailprioverify counts that.
        // Counters: [80] answers from the tick set, [81] tick sets built, [82] verify mismatches.
        public class DetailPriorityHook
        {
            public LotDetailPriority CalcHook()
            {
                if (!sDetailPrioReady || Paused() || !ShortcutOn("detailprio")) { sDetailStamp.Reset(); return CalcOrig(); }
                Lot lot = (Lot)(object)this;
                Household hh = Household.ActiveHousehold;
                if (hh == null) return LotDetailPriority.None;
                if (hh.LotHome == lot) return LotDetailPriority.Home;
                int[] c = PatchCounters();
                if (sDetailStamp.Renew(Simulator.TicksElapsed(), hh, System.Environment.TickCount))
                {
                    sDetailLots.Clear();
                    foreach (Sim s in hh.AllActors)
                        if (s != null && s.RabbitHoleCurrent == null) sDetailLots.Add(s.LotCurrent);
                    c[81]++;
                }
                c[80]++;
                LotDetailPriority p = sDetailLots.Contains(lot) ? LotDetailPriority.PlayerSim : LotDetailPriority.None;
                if (VerifyNow("detailprioverify"))
                {
                    LotDetailPriority g = CalcOrig();
                    if (g != p) c[82]++;
                    return g;
                }
                return p;
            }
            public LotDetailPriority CalcOrig() { return LotDetailPriority.None; } // receives Lot.CalculateDetailPriority's original header
        }
    }
}
