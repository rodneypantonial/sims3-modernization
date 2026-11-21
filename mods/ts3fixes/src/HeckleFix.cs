using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay.ActiveCareer.ActiveCareers;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Interfaces;
using Sims3.Gameplay.Objects.Misc;
using Sims3.Gameplay.Services;
using Sims3.Gameplay.Utilities;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // hecklefix / hecklefixhh (tuning kHeckleFix / kHeckleFixHousehold, both off by default; docs/860). EA bug fix: Sims heckle
    // ordinary Sims at random ("it spams and doesn't work correctly", player report).
    //
    // [verified, decompiled] Sim+Heckle+Definition.Test (EP6) lets any standing or sitting Sim with one of 13 traits
    // (SocialButterfly, Brave, Childish, Diva, Dramatic, Evil, Grumpy, HotHeaded, Inappropriate, Insane, MeanSpirited,
    // NaturalBornPerformer, Rebellious) heckle any other Sim, autonomous or not. It has no "target is performing" test: it only
    // refuses a performer in a gig (those are heckled through ShowStage+WatchTheShowHeckle) and two magic-trick interactions.
    // Run gives the target SlippedUp, sets Target.Heckled and starts a 10-minute cooldown alarm owned by the ACTOR; the
    // cooldown is per target, so one heckler walks from Sim to Sim. Harness logs show townies and household Sims heckling
    // ordinary Sims autonomously, plus route failures to heckle targets.
    //
    // Fix (hecklefix): an autonomous heckle whose EA Test passed is refused unless the target is performing (HfPerforming).
    // Player-ordered heckles keep EA's Test. Household, selected and unknown-scope actors (HouseholdExempt) keep EA's Test
    // unless hecklefixhh is on as well.
    //
    // Stuck flag [verified, decompiled]: Sim.mHeckled is a saved Sim field, cleared only by HeckleReset (the actor's alarm).
    // AlarmManager.OnDispose clears a disposed owner's timers without calling them, so a heckler that is destroyed (townie
    // cleanup, a household leaving the world) before the 10 minutes end leaves the target Heckled for good: never heckled
    // again, and BuffFeelingUnlucky.ShouldAdd keeps treating it as heckled. Save/load alone does not strand it
    // [inferred: the alarm is AlwaysPersisted and InteractionInstance is Persistable, so the instance comes back with it].
    // ShowStage's heckle clears the flag in WatchTheShow.Cleanup and PerformTheShow.Cleanup, so it does not stick.
    // With hecklefix on: at world load every non-gig Sim's flag is cleared (a heckle under 10 sim-minutes old loses the rest
    // of its cooldown; its persisted alarm still fires and clears again), and in play a flag the hook has seen set for
    // kHfStaleMin sim-minutes (6x EA's cooldown) is cleared.
    // Counters: [1530] autonomous heckles of a non-performer refused, [1531] the same seen with the switch off, [1532] exempt
    // actors refused (hecklefixhh), [1533] stale Heckled flags cleared, [1534] errors. Check: HeckleFixChecks.cs.
    public static partial class Fixes
    {
        const float kHfStaleMin = 60f;
        static readonly Type[] kHfTestArgs = { typeof(Sim), typeof(Sim), typeof(bool), typeof(GreyedOutTooltipCallback).MakeByRefType() };
        static Type sHfDef;
        static Sim sHfFakePerformer;  // HeckleFixCheck only: stands in for a performing target
        static readonly Dictionary<ulong, float> sHfSeen = new Dictionary<ulong, float>();  // sim thread, by SimDescriptionId
        static readonly Dictionary<Type, bool> sHfTypes = new Dictionary<Type, bool>();

        // performing interactions that are protected or live in pack assemblies, matched on the class chain by full name
        static readonly string[] kHfPerformNames = {
            "Sims3.Gameplay.Objects.HobbiesSkills.BandInstrument+PlayForTips`1", "Sims3.Gameplay.Objects.HobbiesSkills.BandInstrument+Perform`1",
            "Sims3.Gameplay.Objects.HobbiesSkills.Piano+PianistPerform", "Sims3.Store.Objects.Violin+PlayForTips",
            "Sims3.Gameplay.Objects.HobbiesSkills.PoolTable+PerformForTips", "Sims3.Gameplay.Objects.HobbiesSkills.SnakeCharmingBasket+CharmForTips",
            "Sims3.Gameplay.Objects.HobbiesSkills.MagicBroomArena+StuntRideForTips", "Sims3.Gameplay.Objects.Entertainment.KaraokeMachine+Sing",
            "Sims3.Gameplay.Objects.Entertainment.KaraokeMachine+SingAlone", "Sims3.Gameplay.Objects.Entertainment.KaraokeMachine+SingWith" };

        static void ApplyHeckleFix()
        {
            sHfDef = typeof(Sim.Heckle).GetNestedType("Definition", BindingFlags.Public | BindingFlags.NonPublic);
            if (sHfDef == null) { AppliedLog().Add("hecklefix: Sim+Heckle+Definition not found"); return; }
            Patch(sHfDef, "Test", kHfTestArgs, typeof(HfTestHook), "TestHook", "TestOrig");
            try { World.OnWorldLoadFinishedEventHandler += HfWorldLoaded; }
            catch (Exception) { AppliedLog().Add("hecklefix: world load event threw (no load sweep)"); }
        }

        // Pure: the hooked Test's answer.
        internal static bool HfDecide(bool on, bool hhOn, bool autonomous, bool exempt, bool performing, bool eaOk)
        {
            if (!eaOk || !autonomous || performing || !on) return eaOk;
            return exempt && !hhOn;
        }

        // Pure: clear a flag first seen set at firstSeen?
        internal static bool HfStale(float firstSeen, float now, bool inGig)
        {
            return !inGig && now - firstSeen >= kHfStaleMin;
        }

        static bool HfInGig(Sim t)
        {
            PerformanceCareer pc = t.OccupationAsPerformanceCareer;
            return pc != null && pc.StagePerformingOn != null;
        }

        // Street acts for tips (Showtime careers, spellcasting), hired performers, career acts on a performance prop
        // (balancing ball, boxes, ring of fire, front stage kit), instruments played for tips or on stage, karaoke.
        // shortcut: DJ booth and other unlisted acts count as not performing; add their classes when a log shows one.
        internal static bool HfPerforming(Sim t)
        {
            if (t == null) return false;
            if (sHfFakePerformer != null && ReferenceEquals(t, sHfFakePerformer)) return true;
            InteractionInstance ia = t.CurrentInteraction;
            if (ia == null) return false;
            IPerformForTipsInteraction tips = ia as IPerformForTipsInteraction;
            if (tips != null) return tips.Performing;
            if (ia is PerformanceCareer.PerformerPerformForTips || ia is PerformanceCareer.PerformanceTrick || ia is DoPerformance) return true;
            if (ia.Target is GenericPerformanceObject && t.OccupationAsPerformanceCareer != null) return true;
            Type type = ia.GetType();
            bool hit;
            if (sHfTypes.TryGetValue(type, out hit)) return hit;
            for (Type x = type; x != null && !hit; x = x.BaseType)
            {
                string n = (x.IsGenericType ? x.GetGenericTypeDefinition() : x).FullName;
                hit = Array.IndexOf(kHfPerformNames, n) >= 0;
            }
            sHfTypes[type] = hit;
            return hit;
        }

        // From the Test hook with the switch on: clear a target's flag that has stayed set for kHfStaleMin.
        static void HfWatchFlag(Sim t)
        {
            if (t == null || t.SimDescription == null) return;
            ulong id = t.SimDescription.SimDescriptionId;
            if (!t.Heckled) { sHfSeen.Remove(id); return; }
            float now = SimClock.ElapsedTime(TimeUnit.Minutes), first;
            if (!sHfSeen.TryGetValue(id, out first) || now < first)
            {
                if (sHfSeen.Count >= 256) sHfSeen.Clear();  // a memo only delays a clear
                sHfSeen[id] = now;
                return;
            }
            if (!HfStale(first, now, HfInGig(t))) return;
            t.Heckled = false;
            sHfSeen.Remove(id);
            PatchCounters()[1533]++;
        }

        static void HfWorldLoaded(object sender, EventArgs e)
        {
            sHfSeen.Clear();
            if (!ShortcutOn("hecklefix")) return;
            try
            {
                foreach (Sim s in Sims3.Gameplay.Queries.GetObjects<Sim>())
                    if (s != null && s.Heckled && !HfInGig(s)) { s.Heckled = false; PatchCounters()[1533]++; }
            }
            catch (Exception) { PatchCounters()[1534]++; }
        }

        public class HfTestHook
        {
            public bool TestHook(Sim a, Sim target, bool isAutonomous, ref GreyedOutTooltipCallback greyedOutTooltipCallback)
            {
                bool on = false;
                try { on = ShortcutOn("hecklefix"); if (on) HfWatchFlag(target); }
                catch (Exception) { PatchCounters()[1534]++; }
                bool ok = TestOrig(a, target, isAutonomous, ref greyedOutTooltipCallback);
                if (!ok || !isAutonomous) return ok;
                try
                {
                    if (HfPerforming(target)) return true;
                    bool exempt = HouseholdExempt(a);
                    if (!on) { if (!exempt) PatchCounters()[1531]++; return true; }
                    if (HfDecide(true, ShortcutOn("hecklefixhh"), true, exempt, false, true)) return true;
                    PatchCounters()[exempt ? 1532 : 1530]++;
                    return false;
                }
                catch (Exception) { PatchCounters()[1534]++; return true; }
            }
            public bool TestOrig(Sim a, Sim target, bool isAutonomous, ref GreyedOutTooltipCallback greyedOutTooltipCallback) { return false; } // receives Sim+Heckle+Definition.Test's original body
        }
    }
}
