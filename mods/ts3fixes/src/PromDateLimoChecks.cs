using System;
using System.Collections.Generic;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Objects.Vehicles;
using Sims3.Gameplay.Situations;

namespace Ts3Fixes
{
    // Harness checks for PromDateLimo.cs (docs/861), repro promdatelimo-live. Read-only on the world: nothing is pushed and no
    // live carpool is changed. The limo and prom-entry events need a prom day between 16:00 and 18:00, so the checks run the
    // hooks' decisions (PdlLimoWants / PdlFallbackWants, the code the hooks run) on live Sims standing in as rider, date and
    // actor; testers verify the real prom.
    public static partial class Fixes
    {
        static string PdlCounters()
        {
            int[] c = PatchCounters();
            return "promdatelimo: limo pushes 1535=" + c[1535] + ", sent to prom 1536=" + c[1536] + ", limo cases (switch off) 1537=" + c[1537]
                + ", left behind (switch off) 1538=" + c[1538] + ", errors 1539=" + c[1539] + "; promlimolot: pickups moved 1540=" + c[1540]
                + ", seen (switch off) 1541=" + c[1541] + ", errors 1542=" + c[1542];
        }

        // PromDateLimoCheck run: pure cases, the swaps, then the hook decisions with live Sims: an NPC visitor as the date of the
        // selected Sim (limo: push; prom entry: send), a household Sim as the date (never), an NPC as the prom-goer (never).
        public static object PromDateLimoCheck(string what)
        {
            if (what == "result") return PdlCounters();
            if (what != "run") return "promdatelimo <run|result>";
            bool pure = PdlLimo(false, true, true, true, false, false) && !PdlLimo(true, true, true, true, false, false)
                && !PdlLimo(false, false, true, true, false, false) && !PdlLimo(false, true, false, true, false, false)
                && !PdlLimo(false, true, true, false, false, false) && !PdlLimo(false, true, true, true, true, false)
                && !PdlLimo(false, true, true, true, false, true)
                && PdlFallback(true, false, false, false) && !PdlFallback(false, false, false, false) && !PdlFallback(true, true, false, false)
                && !PdlFallback(true, false, true, false) && !PdlFallback(true, false, false, true);
            string missing = PdlMissing();
            if (missing.Length > 0) return "promdatelimo check: members not found (" + missing + ", N/A); pure=" + pure + "; " + PdlCounters();
            string live = CkLive("promdatelimo");
            Sim sel = Sims3.Gameplay.Actors.Sim.ActiveActor, npc = null, mate = null;
            foreach (Sim s in Sims3.Gameplay.Queries.GetObjects<Sim>())
            {
                if (s == null || s == sel || s.HasBeenDestroyed || s.LotCurrent == null) continue;
                if (npc == null && IsNpcSim(s) && s.LotCurrent != s.LotHome && !PdlQueued(s, CarpoolManager.GetInCarpool.Singleton)
                    && !PdlQueued(s, PromSituation.GoToProm.Singleton)) npc = s;
                if (mate == null && sel != null && s.Household == sel.Household) mate = s;
            }
            if (sel == null || npc == null) return "promdatelimo check: no selected Sim or no NPC visitor on a lot (N/A); pure=" + pure + ", swap " + (live == "" ? "live" : live) + "; " + PdlCounters();
            List<Sim> riders = new List<Sim>(new Sim[] { sel, npc });
            bool limo = PdlLimoWants(npc, npc.LotCurrent, null, riders), fall = PdlFallbackWants(sel, npc, null);
            bool limoHh = mate != null && PdlLimoWants(mate, mate.LotCurrent, null, new List<Sim>(new Sim[] { mate })), fallHh = mate != null && PdlFallbackWants(sel, mate, null);
            bool fallNpc = PdlFallbackWants(npc, sel, null), notRider = PdlLimoWants(npc, npc.LotCurrent, null, new List<Sim>());
            bool ok = pure && live == "" && limo && fall && !limoHh && !fallHh && !fallNpc && !notRider;
            PromSituation prom = PdlProm();
            return "promdatelimo check (switch " + (ShortcutOn("promdatelimo") ? "on" : "off") + "): NPC date " + npc.FullName + " limo=" + limo + ", prom entry=" + fall
                + ", not a rider=" + notRider + "; household date " + (mate != null ? mate.FullName + " limo=" + limoHh + ", prom entry=" + fallHh : "none (N/A)")
                + "; NPC prom-goer=" + fallNpc + "; prom " + (prom != null ? "scheduled" : "none") + "; pure=" + pure + ", swap " + (live == "" ? "live" : live)
                + "; verdict: " + (ok ? "OK" : "UNEXPECTED") + "; " + PdlCounters();
        }

        // PromLimoLotCheck run: pure cases, the swap, and the readonly-field write the hook does, tried on a throwaway CarpoolData
        // (never scheduled); with a prom scheduled, its limo's pickup lot against the active household's home (read only).
        public static object PromLimoLotCheck(string what)
        {
            if (what == "result") return PdlCounters();
            if (what != "run") return "promlimolot <run|result>";
            bool pure = PllRetarget(true, 1, 2) && !PllRetarget(false, 1, 2) && !PllRetarget(true, 2, 2) && !PllRetarget(true, 1, 0);
            if (PdlMissing().Length > 0) return "promlimolot check: members not found (N/A); pure=" + pure + "; " + PdlCounters();
            string live = CkLive("promlimolot");
            bool write = false;
            try
            {
                CarpoolManager.CarpoolData probe = (CarpoolManager.CarpoolData)Activator.CreateInstance(typeof(CarpoolManager.CarpoolData), true);
                sPllStart.SetValue(probe, 0x1234UL);
                write = probe.StartLotId == 0x1234UL;
            }
            catch (Exception) { }
            Household hh = Household.ActiveHousehold;
            Lot home = hh != null ? hh.LotHome : null;
            PromSituation prom = PdlProm();
            CarpoolManager.CarpoolData cp = prom != null ? sPcCarpool.GetValue(prom) as CarpoolManager.CarpoolData : null;
            string state = cp == null ? "no prom limo scheduled" : "prom limo pickup " + cp.StartLotId.ToString("X") + ", home " + (home != null ? home.LotId.ToString("X") : "none")
                + ", would move=" + PllRetarget(true, cp.StartLotId, home != null ? home.LotId : 0);
            bool ok = pure && write && live == "";
            return "promlimolot check (switch " + (ShortcutOn("promlimolot") ? "on" : "off") + "): " + state + "; readonly write=" + write + "; pure=" + pure
                + ", swap " + (live == "" ? "live" : live) + "; verdict: " + (ok ? "OK" : "UNEXPECTED") + "; " + PdlCounters();
        }
    }
}
