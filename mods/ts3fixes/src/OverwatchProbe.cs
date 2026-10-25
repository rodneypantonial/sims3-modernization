using System;
using System.Reflection;
using Sims3.Gameplay.Actors;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // NRaas Overwatch's Stuck Check V2 (StuckCheck.OnPostPlan on Route.PostPlanCallback) counts every failed plan of a Sim
    // that has not moved and resets it after more than mMinimumRouteFail (10) failures spread over mRouteFailTestMinutesV2
    // (30) sim-minutes. EA's own stuck check (LotManager.RunStuckCheckHelper, FewerPlans.cs) plans up to 10 times per round
    // from the Sim's spot to random lot points (sea too), so its probe failures look like the Sim's own: a sleeping Sim
    // gets 11 failures over two rounds and Overwatch resets it (player log, v0.1.5). Overwatch means to skip these: its
    // Tracer ignores a reset whose stack has "Void RunStuckCheck" on RunStuckCheckHelper; with our body swap that frame is
    // RunOrig, with stuckchecknear it is ours and with stuckcheckasync the alarm is gone from the stack (docs/163 §9).
    // Switch "owprobeplans" (tuning kOverwatchIgnoreStuckProbes, default on): StuckCheckRun marks (Sim, task) for the length
    // of the check; Overwatch's OnPostPlan returns at once for a plan of that Sim in that task. Everything else in
    // Overwatch and every other PostPlanCallback subscriber is unchanged. Overwatch absent or shaped differently, or
    // OnPostPlan already compiled (a plan ran before we got to it): one applied-log line, nothing patched.
    // Counters: [293] probe plans hidden from Overwatch, [294] stuck-check runs flagged.
    public static partial class Fixes
    {
        static bool sOwProbeTried, sOwProbePreLoadHooked;
        static readonly OverwatchProbeCore sOwProbe = new OverwatchProbeCore();

        internal static void ApplyOverwatchProbe(bool lastTry)
        {
            if (!sOwProbePreLoadHooked)
            {
                sOwProbePreLoadHooked = true; // Overwatch subscribes OnPostPlan here and its first plan compiles it: try before that
                LoadSaveManager.ObjectGroupsPreLoad += delegate { sOwProbe.Clear(); try { ApplyOverwatchProbe(false); } catch (Exception) { } };
            }
            if (sOwProbeTried) return;
            Assembly ow = null;
            foreach (Assembly a in AppDomain.CurrentDomain.GetAssemblies())
                if (a.GetName().Name == "NRaasOverwatch") { ow = a; break; }
            if (ow == null)
            {
                if (lastTry) { sOwProbeTried = true; AppliedLog().Add("owprobeplans: Overwatch not loaded, off"); }
                return;
            }
            sOwProbeTried = true;
            Type sc = ow.GetType("NRaas.OverwatchSpace.Alarms.StuckCheck");
            Type[] args = { typeof(Route), typeof(string), typeof(string) };
            MethodInfo m = sc == null ? null : sc.GetMethod("OnPostPlan", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic, null, args, null);
            if (m == null || m.ReturnType != typeof(void)) { AppliedLog().Add("owprobeplans: StuckCheck.OnPostPlan(Route, string, string) not found, off"); return; }
            Patch(sc, "OnPostPlan", args, typeof(OwPostPlanHook), "PostPlanHook", "PostPlanOrig"); // logs "StuckCheck.OnPostPlan: ok" or why not
        }

        // StuckCheckRun (FewerPlans.cs) wraps both the game's body and stuckchecknear, on the alarm path and in our drain task.
        static void OwProbeEnter(Sim sim, out ulong id, out ulong task)
        {
            id = sim == null ? 0 : sim.ObjectId.Value;
            task = Simulator.CurrentTask.Value;
            sOwProbe.Enter(id, task);
            PatchCounters()[294]++;
        }

        static void OwProbeExit(ulong id, ulong task) { sOwProbe.Exit(id, task); }

        public static class OwPostPlanHook
        {
            public static void PostPlanHook(Route r, string routeType, string result)
            {
                if (sOwProbe.Depth > 0 && r != null && r.Follower != null && ShortcutOn("owprobeplans"))
                {
                    Sim sim = r.Follower.Target as Sim;
                    if (sim != null && sOwProbe.Active(sim.ObjectId.Value, Simulator.CurrentTask.Value)) { PatchCounters()[293]++; return; }
                }
                PostPlanOrig(r, routeType, result);
            }
            public static void PostPlanOrig(Route r, string routeType, string result) { } // receives StuckCheck.OnPostPlan's original header
        }
    }
}
