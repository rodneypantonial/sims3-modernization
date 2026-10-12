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
            int[] c = d.GetData("ts3fixes.counters") as int[] ?? new int[48];
            Dictionary<string, bool> sw = d.GetData("ts3fixes.switches") as Dictionary<string, bool>;
            if (sw == null) { sw = new Dictionary<string, bool>(); d.SetData("ts3fixes.switches", sw); }
            string[] names = { "gohome", "rabbithole", "visit", "visitlot", "gotolot", "gotocommunity", "broadcast", "virtualhome", "failreplan", "watchdog", "backoff", "stations", "spotspread", "savecleanup" };
            foreach (string n in names)
            {
                if (a.Has(n)) sw[n] = a.Bool(n, true);
                bool on;
                r[n] = sw.TryGetValue(n, out on) ? on : n != "savecleanup"; // unset = the mod's tuning (all on by default except savecleanup)
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
            r["watchdog_shortcuts"] = c[31]; // stuck Sims reset
            if (c.Length > 32) r["stations_shortcuts"] = c[32]; // station detours skipped
            if (c.Length > 43) { r["savecleanup_pairs"] = c[40]; r["savecleanup_minisims"] = c[41]; r["savecleanup_runs"] = c[42]; r["savecleanup_skipped"] = c[43]; }
            r["recent"] = d.GetData("ts3fixes.recent");
            r["moved"] = d.GetData("ts3fixes.moved");
            return r;
        }
    }
}
