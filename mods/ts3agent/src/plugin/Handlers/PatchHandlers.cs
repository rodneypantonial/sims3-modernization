using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Ts3Agent
{
    // Patch host experiment: replace a C# method at runtime by copying the replacement's MonoMethod header over the
    // original's (the idea MonoPatcher's ReplaceMethod uses: 40 bytes, keeping the name pointer at +0x18; +0x14 is the
    // interpreter info this build caches per method). patch_test checks which call paths see a swap made mid-game.
    public static partial class Handlers
    {
        const int kMethodHeader = 40, kNameOffset = 0x18;

        static byte[] SwapMethod(MethodBase original, MethodBase replacement)
        {
            IntPtr o = original.MethodHandle.Value, r = replacement.MethodHandle.Value;
            byte[] saved = new byte[kMethodHeader], repl = new byte[kMethodHeader];
            Marshal.Copy(o, saved, 0, kMethodHeader);
            Marshal.Copy(r, repl, 0, kMethodHeader);
            Marshal.Copy(repl, 0, o, kNameOffset);
            Marshal.Copy(repl, kNameOffset + 4, new IntPtr(o.ToInt32() + kNameOffset + 4), kMethodHeader - kNameOffset - 4);
            return saved;
        }

        static void RestoreMethod(MethodBase original, byte[] saved)
        {
            Marshal.Copy(saved, 0, original.MethodHandle.Value, kMethodHeader);
        }

        public class PtVirt
        {
            public virtual int F() { return 1; }
            public int G() { return 2; }
        }

        static int PtA() { return 1; }
        static int PtB() { return 2; }
        static int PtCallWarm() { return PtA(); }
        static int PtCallCold() { return PtA(); }
        static int PtVirtCall(PtVirt v) { return v.F(); }

        // method_handle {type (or "self"), method, warm?}: MonoMethod pointer and the interpreter info at +0x14 (0 until first run), for the probe's read
        static object MethodHandle(Args a)
        {
            const BindingFlags all = BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            string tn = a.Str("type");
            Type t = tn == "self" ? typeof(Handlers) : null; // this plugin's own test methods
            if (t == null) foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type x = asm.GetType(tn, false);
                if (x != null) t = x; // last match: a hot-reloaded plugin loads after its predecessors
            }
            if (t == null) throw new McpException("no type " + tn);
            MethodInfo m = t.GetMethod(a.Str("method"), all);
            if (m == null) throw new McpException("no method " + a.Str("method"));
            if (a.Bool("warm", false)) { PtCallWarm(); PtB(); PtVirtCall(new PtVirt()); new PtVirt().G(); }
            IntPtr h = m.MethodHandle.Value;
            Dictionary<string, object> r = Util.Obj();
            r["method"] = "0x" + h.ToInt32().ToString("x");
            r["info"] = "0x" + Marshal.ReadInt32(new IntPtr(h.ToInt32() + 0x14)).ToString("x");
            return r;
        }

        // Cold variants: never run before patch_test_cold swaps them, as game methods are at startup.
        public class PcBase
        {
            public int Field = 40;
            public virtual int Run() { return Field + 1; }
            public int Orig() { return -1; } // stub: receives the original's header, so the replacement can call it
        }
        public class PcFix : PcBase
        {
            public int Run2() { return Orig() + 100; } // replacement: wraps the original
        }
        static int PcCallDirect(PcBase b) { return b.Run(); }

        static object PatchTestCold(Args a)
        {
            const BindingFlags all = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            MethodInfo run = typeof(PcBase).GetMethod("Run", all), orig = typeof(PcBase).GetMethod("Orig", all);
            MethodInfo fix = typeof(PcFix).GetMethod("Run2", all);
            Dictionary<string, object> r = Util.Obj();
            if (a.Bool("swap", true))
            {
                SwapMethod(orig, run); // stub now runs the original body
                SwapMethod(run, fix);  // Run now runs the replacement
            }
            PcBase b = new PcBase();
            r["virtual_call"] = PcCallDirect(b);     // expect 141 (original 41 + 100)
            r["stub_call"] = b.Orig();               // expect 41
            r["derived_call"] = PcCallDirect(new PcFix());
            return r;
        }

        static object PatchTest(Args a)
        {
            const BindingFlags all = BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            Type h = typeof(Handlers);
            Dictionary<string, object> r = Util.Obj();
            PtVirt v = new PtVirt();
            // compile the warm callers before the swap; the cold one only after
            r["before_warm"] = PtCallWarm();
            r["before_virt"] = PtVirtCall(v);
            MethodInfo a1 = h.GetMethod("PtA", all), b1 = h.GetMethod("PtB", all);
            MethodInfo f = typeof(PtVirt).GetMethod("F", all), g = typeof(PtVirt).GetMethod("G", all);
            byte[] sa = SwapMethod(a1, b1), sf = SwapMethod(f, g);
            try
            {
                r["after_warm"] = PtCallWarm();
                r["after_cold"] = PtCallCold();
                r["after_virt"] = PtVirtCall(v);
                r["after_reflect"] = a1.Invoke(null, null);
            }
            finally
            {
                RestoreMethod(a1, sa);
                RestoreMethod(f, sf);
            }
            r["restored_warm"] = PtCallWarm();
            r["restored_virt"] = PtVirtCall(v);
            return r;
        }

        // ---- The fixes themselves live in the player mod (mods/ts3fixes); this test agent only reads and switches them
        // through the AppDomain data that mod shares (ts3fixes.applied, .counters, .switches, .recent, .moved).
        // patches {name: bool, ...}: switch fixes for this session (overrides the mod's tuning), returns state and counters.
        static object Patches(Args a)
        {
            AppDomain d = AppDomain.CurrentDomain;
            Dictionary<string, object> r = Util.Obj();
            r["applied"] = d.GetData("ts3fixes.applied");
            int[] c = d.GetData("ts3fixes.counters") as int[] ?? new int[99];
            Dictionary<string, bool> sw = d.GetData("ts3fixes.switches") as Dictionary<string, bool>;
            if (sw == null) { sw = new Dictionary<string, bool>(); d.SetData("ts3fixes.switches", sw); }
            string[] names = { "gohome", "rabbithole", "visit", "visitlot", "gotolot", "gotocommunity", "broadcast", "virtualhome", "failreplan", "watchdog", "watchdogidle", "backoff", "stations", "spotspread", "savecleanup", "overwatchstuck", "portaladaptorleak", "casgrid", "casmemo", "spatialcache", "routablecache", "stuckcheckbusy", "fastreftable", "cachedreflection", "motivebar", "buffgrid", "namecache", "detailprio", "detailprioverify", "hoursmemo", "hoursmemoverify", "caneearly", "tooltipthrottle", "safespotreuse", "safespotreuseverify", "wildflowerprobe", "wildflowertask" };
            foreach (string n in names)
            {
                if (a.Has(n)) sw[n] = a.Bool(n, true);
                bool on;
                r[n] = sw.TryGetValue(n, out on) ? on : n != "savecleanup" && n != "casgrid" && n != "casmemo" && n != "spatialcache" && n != "routablecache" && n != "stuckcheckbusy" && n != "fastreftable" && n != "cachedreflection" && n != "motivebar" && n != "buffgrid" && n != "namecache" && n != "detailprio" && n != "detailprioverify" && n != "hoursmemo" && n != "hoursmemoverify" && n != "caneearly" && n != "tooltipthrottle" && n != "safespotreuse" && n != "safespotreuseverify" && n != "wildflowerprobe" && n != "wildflowertask"; // unset = the mod's tuning (all on by default except savecleanup, the CAS grid and the spatial cache)
            }
            r["gohome_runs"] = c[0]; r["gohome_shortcuts"] = c[1]; r["gohome_no_spot"] = c[2]; r["gohome_loop_guard"] = c[3];
            r["rabbithole_calls"] = c[4]; r["rabbithole_shortcuts"] = c[5]; r["rabbithole_no_spot"] = c[6];
            string[] lotTrips = { "visit", "visitlot", "gotolot", "gotocommunity" };
            for (int i = 0; i < lotTrips.Length; i++)
            {
                r[lotTrips[i] + "_runs"] = c[7 + 3 * i];
                r[lotTrips[i] + "_shortcuts"] = c[8 + 3 * i];
                r[lotTrips[i] + "_no_spot"] = c[9 + 3 * i];
            }
            r["houseboat_shortcuts"] = c[21];
            r["broadcast_pulses"] = c[22];
            r["broadcast_skipped"] = c[23];
            r["backoff_plans"] = c[24]; r["backoff_failures"] = c[25]; r["backoff_shortcuts"] = c[26]; // skipped plans
            r["virtualhome_runs"] = c[27]; r["virtualhome_shortcuts"] = c[28];
            r["failreplan_off"] = c[29]; r["failreplan_shortcuts"] = c[30]; // route-fail walks skipped
            r["watchdog_shortcuts"] = c[31]; // stuck Sims reset or faded
            if (c.Length > 32) r["stations_shortcuts"] = c[32]; // station detours skipped
            if (c.Length > 36) { r["watchdog_home_none"] = c[33]; r["watchdog_home_shell"] = c[34]; r["watchdog_home_house"] = c[35]; r["watchdog_second"] = c[36]; } // watchdog home kinds (none = faded), second resets in a day
            if (c.Length > 39) { r["safespot_hits"] = c[37]; r["safespot_misses"] = c[38]; r["safespot_checks"] = c[39]; } // SafeSpot verdict cache; checks = full route plans run
            if (c.Length > 43) { r["savecleanup_pairs"] = c[40]; r["savecleanup_minisims"] = c[41]; r["savecleanup_runs"] = c[42]; r["savecleanup_skipped"] = c[43]; }
            if (c.Length > 45) { r["overwatch_checks"] = c[44]; r["overwatch_yields"] = c[45]; } // NRaas Stuck Check plans seen / spread to their own tick
            if (c.Length > 51) { r["watchdog_idle_unroutable"] = c[46]; r["watchdog_idle_hidden_room"] = c[47]; r["watchdog_idle_stationary"] = c[48]; r["watchdog_route_fails"] = c[49]; r["watchdog_idle_verdicts"] = c[50]; r["watchdog_route_fail_sims"] = c[51]; } // watchdog idle cases (switch watchdogidle): resets per case, routability verdicts asked, Sims with failing plans tracked
            if (c.Length > 52) r["savecleanup_lifeevent_lists"] = c[52]; // empty life-event lists trimmed (docs/14 7a)
            if (c.Length > 53) r["savecleanup_snapshots"] = c[53]; // old memory pictures removed by the snapshot cap (docs/14 7b)
            if (c.Length > 55) { r["portaladaptor_unsubscribes"] = c[54]; r["savecleanup_route_callbacks"] = c[55]; } // route adaptor leak on ocean/houseboat portals (docs/09 section 5)
            if (c.Length > 58) { r["asyncroute_started"] = c[56]; r["asyncroute_yields"] = c[57]; r["asyncroute_pollcap"] = c[58]; } // async route plans (docs/18, ts3native [RouteAsync])
            if (c.Length > 61) { r["casgrid_fills"] = c[59]; r["casmemo_hits"] = c[60]; r["casmemo_lookups"] = c[61]; } // CAS hair/makeup incremental fills, hair preset cache (docs/21)
            if (c.Length > 64) { r["spatialcache_hits"] = c[62]; r["spatialcache_misses"] = c[63]; r["spatialcache_clears"] = c[64]; } // lot object query cache: hits, native lot walks, clears on object destroy (docs/20)
            if (c.Length > 67) { r["routablecache_hits"] = c[65]; r["routablecache_plans"] = c[66]; r["stuckcheck_busy_skipped"] = c[67]; } // fewer route plans (FewerPlans.cs, docs/04a section 7)
            if (c.Length > 70) { r["reftable_converted"] = c[68]; r["reftable_max_entries"] = c[69]; r["reftable_grows"] = c[70]; } // fast ReferenceHashtable (RefTable.cs)
            if (c.Length > 73) { r["typecache_started"] = c[71]; r["typecache_hits"] = c[72]; r["typecache_stored"] = c[73]; } // serializer per-type answer cache (CachedReflection.cs)
            if (c.Length > 79) { r["motivebar_cached"] = c[74]; r["motivebar_loads"] = c[75]; r["buffgrid_inplace"] = c[76]; r["buffgrid_rebuilds"] = c[77]; r["namecache_hits"] = c[78]; r["namecache_misses"] = c[79]; } // HUD caches (UiCaches.cs, docs/49), Sim name cache (NameCache.cs, docs/54)
            if (c.Length > 82) { r["detailprio_hits"] = c[80]; r["detailprio_sets"] = c[81]; r["detailprio_mismatches"] = c[82]; } // lot detail priority from one lot set per tick (ScriptHotSpots.cs, docs/74)
            if (c.Length > 84) { r["caneearly_skipped"] = c[83]; r["caneearly_passed"] = c[84]; } // cane-walk checks answered without the inventory search / run by the game (ScriptHotSpots.cs, docs/74)
            if (c.Length > 87) { r["hoursmemo_hits"] = c[85]; r["hoursmemo_misses"] = c[86]; r["hoursmemo_mismatches"] = c[87]; } // lot hours of operation memo per tick (ScriptHotSpots.cs, docs/74)
            if (c.Length > 92) { r["safespotreuse_hits"] = c[90]; r["safespotreuse_searches"] = c[91]; r["safespotreuse_mismatches"] = c[92]; } // SafeSpot placements from a stored FindGoodLocation result / native searches run / verify: a fresh search found another spot
            if (c.Length > 89) { r["tooltipthrottle_skipped"] = c[88]; r["tooltipthrottle_walks"] = c[89]; } // tooltip window walks skipped while the cursor rests / run (ScriptHotSpots.cs, docs/74)
            if (c.Length > 98) { r["wildflower_fires"] = c[93]; r["wildflower_tries"] = c[94]; r["wildflower_placed"] = c[95]; r["wildflower_failed"] = c[96]; r["wildflower_maxms"] = c[97]; r["wildflower_saveaborts"] = c[98]; } // spring wildflower drain (docs/110): fires that took a lot, tries, placed, failed, longest fire/slice ms, save aborts
            r["wildflower_log"] = d.GetData("ts3fixes.wildflower"); // one line per fire with wildflowerprobe or wildflowertask on
            r["recent"] = d.GetData("ts3fixes.recent");
            r["moved"] = d.GetData("ts3fixes.moved");
            return r;
        }
    }
}
