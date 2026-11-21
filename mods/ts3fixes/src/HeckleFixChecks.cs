using System;
using System.Collections.Generic;
using Sims3.Gameplay.Actors;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Harness check for HeckleFix.cs (docs/860), repro hecklefix-live. Runs the Test hook (the body the swapped Sim+Heckle+Definition.Test runs; CkLive reports the swap) on live Sims;
    // nothing is pushed or changed (the "performing" case is a static stand-in read only by HfPerforming).
    public static partial class Fixes
    {
        // HeckleFixCheck run: a townie pair EA lets heckle autonomously (EA's own Test, through the TestOrig stub), then the hooked
        // Test for: that pair (off true, on false), the same target standing in as a performer (true), the pair player-ordered
        // (true), and a household / selected actor (EA's answer unless hecklefixhh is on as well). Pure cases follow.
        public static object HeckleFixCheck(string what)
        {
            int[] c = PatchCounters();
            string counters = "hecklefix: refused 1530=" + c[1530] + ", would refuse (switch off) 1531=" + c[1531] + ", exempt refused 1532=" + c[1532]
                + ", stale flags cleared 1533=" + c[1533] + ", errors 1534=" + c[1534];
            if (what == "result") return counters;
            if (what != "run") return "hecklefix <run|result>";
            bool pure = HfDecide(true, false, true, false, false, true) == false && HfDecide(false, false, true, false, false, true)
                && HfDecide(true, false, true, false, true, true) && HfDecide(true, false, false, false, false, true)
                && HfDecide(true, false, true, true, false, true) && !HfDecide(true, true, true, true, false, true)
                && !HfDecide(true, false, true, false, true, false) && HfStale(0f, 60f, false) && !HfStale(0f, 59f, false) && !HfStale(0f, 600f, true);
            if (sHfDef == null) return "hecklefix check: Sim+Heckle+Definition not hooked (N/A); pure=" + pure + "; " + counters;
            string live = CkLive("hecklefix");  // "" = the swap is in place
            bool on = ShortcutOn("hecklefix"), hh = ShortcutOn("hecklefixhh");
            HfTestHook ea = new HfTestHook();
            List<Sim> sims = new List<Sim>(Sims3.Gameplay.Queries.GetObjects<Sim>());
            Sim ta = null, tt = null, ha = null, ht = null;
            GreyedOutTooltipCallback cb = null;
            foreach (Sim a in sims)
            {
                if (a == null || a.SimDescription == null) continue;
                bool ex = HouseholdExempt(a);
                if (ex ? ha != null : ta != null) continue;
                foreach (Sim t in sims)
                {
                    if (t == null || ReferenceEquals(a, t) || t.SimDescription == null || HfPerforming(t)) continue;
                    if (!ex && HouseholdExempt(t)) continue;
                    if (!ea.TestOrig(a, t, true, ref cb)) continue;  // EA's body: this actor may heckle this target autonomously
                    if (ex) { ha = a; ht = t; } else { ta = a; tt = t; }
                    break;
                }
                if (ta != null && ha != null) break;
            }
            if (ta == null) return "hecklefix check: no townie pair EA lets heckle autonomously among " + sims.Count + " Sims (N/A); pure=" + pure + "; " + counters;
            bool plain = HfCall(ta, tt, true), perf, user = HfCall(ta, tt, false);
            sHfFakePerformer = tt;
            try { perf = HfCall(ta, tt, true); }
            finally { sHfFakePerformer = null; }
            string hhPart = "household actor: none EA lets heckle (N/A)";
            bool hhOk = true;
            if (ha != null)
            {
                bool hr = HfCall(ha, ht, true);
                hhOk = hr == !(on && hh);
                hhPart = "household actor " + ha.FullName + " -> " + ht.FullName + " autonomous=" + hr;
            }
            bool ok = live == "" && pure && perf && user && hhOk && plain == !on;
            string verdict = !ok ? "UNEXPECTED" : on ? "FIXED" : "BROKEN";
            return "hecklefix check (hecklefix " + (on ? "on" : "off") + ", hecklefixhh " + (hh ? "on" : "off") + "): townie " + ta.FullName + " -> " + tt.FullName
                + " autonomous=" + plain + ", at a performer=" + perf + ", player-ordered=" + user + "; " + hhPart + "; pure=" + pure + ", swap " + (live == "" ? "live" : live) + "; verdict: " + verdict + "; " + counters;
        }

        static bool HfCall(Sim a, Sim t, bool autonomous)
        {
            GreyedOutTooltipCallback cb = null;
            return new HfTestHook().TestHook(a, t, autonomous, ref cb);  // no MethodInfo.Invoke on a swapped method (docs/312)
        }
    }
}
