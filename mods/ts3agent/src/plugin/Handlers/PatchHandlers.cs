using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using Sims3.Gameplay;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;

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

        // ---- Startup patch table. Swaps only work for methods the interpreter has not compiled yet (patch_test), so
        // they are applied once per game session, from the first plugin load (tuning time, before any world), never on
        // a hot reload. Rule from patch_test_cold: a replacement calls the original only through an instance stub on
        // its own class that received the original's header; nothing else may call that stub.
        const string kPatchState = "ts3agent.patch.state";

        // GoHome counters (see GoHomeHook); shared across hot reloads through AppDomain data
        static int[] PatchCounters()
        {
            int[] c = AppDomain.CurrentDomain.GetData(kPatchState) as int[];
            if (c == null) { c = new int[8]; AppDomain.CurrentDomain.SetData(kPatchState, c); }
            return c;
        }

        static List<object> sPatchLog;

        static void ApplyStartupPatches()
        {
            if (AppDomain.CurrentDomain.GetData(kPatchState + ".applied") != null) return;
            AppDomain.CurrentDomain.SetData(kPatchState + ".applied", true);
            sPatchLog = new List<object>();
            AppDomain.CurrentDomain.SetData(kPatchState + ".log", sPatchLog);
            PatchCounters();
            Patch(typeof(GoHome), "Run", typeof(GoHomeHook), "RunHook", "RunOrig");
        }

        static void Patch(Type target, string method, Type hook, string replacement, string stub)
        {
            const BindingFlags all = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            try
            {
                MethodInfo t = target.GetMethod(method, all), r = hook.GetMethod(replacement, all), s = hook.GetMethod(stub, all);
                if (Marshal.ReadInt32(new IntPtr(t.MethodHandle.Value.ToInt32() + 0x14)) != 0)
                    throw new InvalidOperationException("already compiled");
                SwapMethod(s, t);
                SwapMethod(t, r);
                sPatchLog.Add(target.Name + "." + method + ": ok");
            }
            catch (Exception e) { sPatchLog.Add(target.Name + "." + method + ": " + e.Message); }
        }

        // Off-screen go-home (PLAN fix b): a townie whose trip home nobody can see skips the walk/drive and appears on its
        // home lot. Only the route is replaced: the game's own lead-in (carpool wait, leaving the visit and group) runs, and
        // the success bookkeeping matches a real arrival, so whatever the Sim does next is unchanged. Anything not clearly
        // safe runs the original. Counters: [0] runs, [1] shortcuts, [2] no spot found on the lot, [3] loop guard.
        public class GoHomeHook : GoHome
        {
            static int[] sCounters;
            public bool RunHook()
            {
                if (sCounters == null) sCounters = PatchCounters();
                sCounters[0]++;
                if (!ShortcutOn("gohome") || !Unseen(Actor, Target)) return RunOrig();
                // a Sim shortcut twice within 10 sim-minutes is in some loop the game drives: let it walk
                ulong id = Actor.SimDescription.SimDescriptionId;
                long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
                long last;
                if (sLastShortcut.TryGetValue(id, out last) && now - last < kLoopTicks)
                {
                    sCounters[3]++;
                    return RunOrig();
                }
                Sims3.SimIFace.Vector3 pos = Sims3.SimIFace.Vector3.Invalid, fwd = Sims3.SimIFace.Vector3.Invalid;
                bool found = false;
                for (int i = 0; i < 3 && !found; i++) // the spot must be on the home lot, or the game still sees the Sim away
                    found = GlobalFunctions.FindGoodLocation(Actor, new World.FindGoodLocationParams(Target.GetRandomPosition(false, true)), out pos, out fwd)
                        && LotManager.GetLotAtPoint(pos) == Target;
                if (!found)
                {
                    sCounters[2]++;
                    return RunOrig();
                }
                sLastShortcut[id] = now;
                if (sLog.Count >= 40) sLog.RemoveAt(0);
                sLog.Add(Actor.FullName + " -> " + Target.Name + " from " + (Actor.LotCurrent != null ? Actor.LotCurrent.Name : "street"));
                // same lead-in as GoHome.Run
                if (!Sims3.Gameplay.Objects.Vehicles.CarpoolManager.WaitForCarpool(Actor)) return false;
                Sims3.Gameplay.Situations.VisitSituation.OnSimLeavingLot(Actor);
                Sims3.Gameplay.Situations.GroupingSituation.RemoveSimFromGroupSituation(Actor);
                Actor.SimRoutingComponent.DisableDynamicFootprint();
                Actor.SetPosition(pos);
                Actor.SetForward(fwd);
                sCounters[1]++;
                NumSuccess++;
                BeginCommodityUpdates();
                EndCommodityUpdates(true);
                return true;
            }
            public bool RunOrig() { return false; } // receives GoHome.Run's original header
            static readonly Dictionary<ulong, long> sLastShortcut = new Dictionary<ulong, long>();
            public static readonly List<object> sLog = GoHomeLog();
            static readonly long kLoopTicks = Sims3.Gameplay.Utilities.SimClock.ConvertToTicks(10f, Sims3.Gameplay.Utilities.TimeUnit.Minutes);

            // Townie, alone, standing, going to its own ordinary lot, and neither it nor that lot near the camera or the
            // active household. Houseboats and apartment shells keep their own arrival code.
            bool Unseen(Sims3.Gameplay.Actors.Sim sim, Lot home)
            {
                if (sim == null || home == null || sim.IsSelectable || sim.Household == Household.ActiveHousehold) return false;
                if (sim.LotHome != home || sim.LotCurrent == home || home == Household.ActiveHouseholdLot) return false;
                if (SimFollowers != null && SimFollowers.Count > 0) return false;
                if (sim.Posture != sim.Standing || sim.IsInRidingPosture || sim.IsInBeingRiddenPosture) return false;
                if (home.IsHouseboatLot() || home.BuildableShells.Length > 0) return false;
                if (Sims3.Gameplay.Controllers.TombRoomManager.IsObjectInATombRoom(sim)) return false;
                if (World.IsObjectOnScreen(sim)) return false;
                return FarFromViewers(sim.Position) && FarFromViewers(home.Position);
            }
        }

        // Outside the camera's reach (a radius that grows as the camera zooms out) and not near any active-household Sim.
        static bool FarFromViewers(Sims3.SimIFace.Vector3 p)
        {
            Sims3.SimIFace.Vector3 target = CameraController.GetTarget(), cam = CameraController.GetPosition();
            float reach = Math.Max(kShortcutMinDistance, 2f * Math.Abs(cam.y - target.y));
            if ((p - target).Length() < reach || (p - cam).Length() < reach) return false;
            if (Household.ActiveHousehold != null)
                foreach (Sims3.Gameplay.Actors.Sim s in Household.ActiveHousehold.AllActors)
                    if (s != null && (p - s.Position).Length() < kShortcutMinDistance) return false;
            return true;
        }

        const float kShortcutMinDistance = 80f;

        static List<object> GoHomeLog()
        {
            List<object> l = AppDomain.CurrentDomain.GetData("ts3agent.shortcut.gohome.log") as List<object>;
            if (l == null) { l = new List<object>(); AppDomain.CurrentDomain.SetData("ts3agent.shortcut.gohome.log", l); }
            return l;
        }

        // shortcuts {name: bool}: runtime switches (all on by default), shared across hot reloads
        static bool ShortcutOn(string name)
        {
            object v = AppDomain.CurrentDomain.GetData("ts3agent.shortcut." + name);
            return v == null || (bool)v;
        }

        // patches: what was applied at startup and the hook counters
        static object Patches(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            r["applied"] = AppDomain.CurrentDomain.GetData(kPatchState + ".log");
            if (a.Has("gohome")) AppDomain.CurrentDomain.SetData("ts3agent.shortcut.gohome", a.Bool("gohome", true));
            int[] c = PatchCounters();
            r["gohome"] = ShortcutOn("gohome");
            r["gohome_runs"] = c[0];
            r["gohome_shortcuts"] = c[1];
            r["gohome_no_spot"] = c[2];
            r["gohome_loop_guard"] = c[3];
            r["gohome_recent"] = GoHomeLog();
            return r;
        }
    }
}
