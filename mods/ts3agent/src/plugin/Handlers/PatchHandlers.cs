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
            if (c == null) { c = new int[32]; AppDomain.CurrentDomain.SetData(kPatchState, c); }
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
            Patch(typeof(Sims3.Gameplay.Abstracts.RabbitHole), "RouteNearEntranceAndEnterRabbitHole", typeof(RabbitHoleHook), "EnterHook", "EnterOrig");
            Patch(typeof(VisitCommunityLot), "Run", typeof(VisitCommunityLotHook), "RunHook", "RunOrig");
            Patch(typeof(VisitLot), "Run", typeof(VisitLotHook), "RunHook", "RunOrig");
            Patch(typeof(GoToLot), "Run", typeof(GoToLotHook), "RunHook", "RunOrig");
            Patch(typeof(GoToCommunityLot), "Run", typeof(GoToCommunityLotHook), "RunHook", "RunOrig");
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
        // safe runs the original. Counters: [0] runs, [1] shortcuts, [2] no safe spot on the lot, [3] loop guard.
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
                Sims3.SimIFace.Vector3 pos, fwd;
                if (!SafeSpot(Actor, Target, Target.GetRandomPosition(false, true), out pos, out fwd))
                {
                    sCounters[2]++;
                    return RunOrig();
                }
                sLastShortcut[id] = now;
                ShortcutLog(Actor.FullName + " -> " + Target.Name + " from " + (Actor.LotCurrent != null ? Actor.LotCurrent.Name : "street"));
                // same lead-in as GoHome.Run
                if (!Sims3.Gameplay.Objects.Vehicles.CarpoolManager.WaitForCarpool(Actor)) return false;
                Sims3.Gameplay.Situations.VisitSituation.OnSimLeavingLot(Actor);
                Sims3.Gameplay.Situations.GroupingSituation.RemoveSimFromGroupSituation(Actor);
                PlaceSim(Actor, pos, fwd);
                sCounters[1]++;
                NumSuccess++;
                BeginCommodityUpdates();
                EndCommodityUpdates(true);
                return true;
            }
            public bool RunOrig() { return false; } // receives GoHome.Run's original header
            static readonly Dictionary<ulong, long> sLastShortcut = new Dictionary<ulong, long>();
            static readonly long kLoopTicks = Sims3.Gameplay.Utilities.SimClock.ConvertToTicks(10f, Sims3.Gameplay.Utilities.TimeUnit.Minutes);

            // Townie, alone, standing, going to its own ordinary lot, and neither it nor that lot near the camera or the
            // active household. Houseboats and apartment shells keep their own arrival code.
            bool Unseen(Sims3.Gameplay.Actors.Sim sim, Lot home)
            {
                if (home == null || !TownieUnseen(sim, SimFollowers)) return false;
                if (sim.LotHome != home || sim.LotCurrent == home || home == Household.ActiveHouseholdLot) return false;
                if (home.IsHouseboatLot() || home.BuildableShells.Length > 0) return false;
                return FarFromViewers(home.Position);
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

        // A spot for a shortcut Sim on `lot` near `near`: on the lot (else the game still sees the Sim away) and passing the
        // game's Sim.IsPointInLotSafelyRoutable (a short plan from the spot to the lot's edges), so the Sim can walk on from
        // there instead of standing stuck. That check is itself a route plan, so each lot keeps its last verified spot:
        // reuses start next to it and re-verify every 8th time.
        static readonly Dictionary<ulong, Sims3.SimIFace.Vector3> sSafeSpot = new Dictionary<ulong, Sims3.SimIFace.Vector3>();
        static readonly Dictionary<ulong, int> sSafeUses = new Dictionary<ulong, int>();
        static bool SafeSpot(Sims3.Gameplay.Actors.Sim sim, Lot lot, Sims3.SimIFace.Vector3 near, out Sims3.SimIFace.Vector3 pos, out Sims3.SimIFace.Vector3 fwd)
        {
            pos = fwd = Sims3.SimIFace.Vector3.Invalid;
            Sims3.SimIFace.Vector3 known;
            bool cached = sSafeSpot.TryGetValue(lot.LotId, out known);
            int uses;
            sSafeUses.TryGetValue(lot.LotId, out uses);
            sSafeUses[lot.LotId] = uses + 1;
            for (int i = 0; i < 3; i++)
            {
                bool fromKnown = cached && i == 0;
                if (!GlobalFunctions.FindGoodLocation(sim, new World.FindGoodLocationParams(fromKnown ? known : near), out pos, out fwd)) continue;
                if (LotManager.GetLotAtPoint(pos) != lot) continue;
                if (fromKnown && uses % 8 != 0) return true;
                if (sim.IsPointInLotSafelyRoutable(lot, pos)) { sSafeSpot[lot.LotId] = pos; return true; }
                if (fromKnown) sSafeSpot.Remove(lot.LotId);
            }
            return false;
        }

        static void PlaceSim(Sims3.Gameplay.Actors.Sim sim, Sims3.SimIFace.Vector3 pos, Sims3.SimIFace.Vector3 fwd)
        {
            sim.SimRoutingComponent.DisableDynamicFootprint();
            sim.SetPosition(pos);
            sim.SetForward(fwd);
        }

        // A townie on its own, on foot, standing, and not near anything the player can see. Shared by the trip shortcuts.
        static bool TownieUnseen(Sims3.Gameplay.Actors.Sim sim, List<Sims3.Gameplay.Actors.Sim> followers)
        {
            if (sim == null || sim.IsSelectable || sim.Household == Household.ActiveHousehold) return false;
            if (followers != null && followers.Count > 0) return false;
            if (sim.Posture != sim.Standing || sim.IsInRidingPosture || sim.IsInBeingRiddenPosture) return false;
            if (Sims3.Gameplay.Controllers.TombRoomManager.IsObjectInATombRoom(sim) || World.IsObjectOnScreen(sim)) return false;
            return FarFromViewers(sim.Position);
        }

        static void ShortcutLog(string line)
        {
            List<object> log = GoHomeLog();
            if (log.Count >= 40) log.RemoveAt(0);
            log.Add(line);
        }

        // Trips into buildings (PLAN fix b3): work, school, shops, restaurants. An unseen townie heading to an unseen rabbit
        // hole far away is placed at a safe spot by the entrance, then the game's own method runs: it plans only the last
        // few metres and does everything after the walk unchanged (entering animation, success count, followers).
        // Counters: [4] calls, [5] shortcuts, [6] no safe spot.
        public class RabbitHoleHook : Sims3.Gameplay.Abstracts.RabbitHole
        {
            static int[] sCounters;
            public override RabbitHoleTuningClass RabbitHoleTuning { get { return null; } } // never instantiated; required by the base class
            public bool EnterHook(Sims3.Gameplay.Actors.Sim a, IRabbitHoleFollowers inst, BeforeEnteringRabbitHoleDelegate beforeEntering, bool canUseCar, Route.RouteMetaType routeMetaType, bool playRouteFailure)
            {
                if (sCounters == null) sCounters = PatchCounters();
                sCounters[4]++;
                Sims3.Gameplay.Abstracts.RabbitHole proxy = RabbitHoleProxy;
                if (ShortcutOn("rabbithole") && proxy != null && proxy.EnterSlots.Count > 0 && TownieUnseen(a, inst != null ? inst.SimFollowers : null))
                {
                    Lot lot = proxy.LotCurrent;
                    Sims3.SimIFace.Vector3 door = proxy.GetPositionOfSlot(proxy.EnterSlots[0]);
                    if (lot != null && !lot.IsWorldLot && a.LotCurrent != lot && (a.Position - door).Length() > kShortcutMinDistance
                        && !World.IsObjectOnScreen(proxy) && FarFromViewers(door))
                    {
                        Sims3.SimIFace.Vector3 pos, fwd;
                        if (SafeSpot(a, lot, door, out pos, out fwd))
                        {
                            string from = a.LotCurrent != null ? a.LotCurrent.Name : "street";
                            PlaceSim(a, pos, fwd);
                            sCounters[5]++;
                            ShortcutLog(a.FullName + " => " + proxy.CatalogName + " from " + from);
                        }
                        else sCounters[6]++;
                    }
                }
                return EnterOrig(a, inst, beforeEntering, canUseCar, routeMetaType, playRouteFailure);
            }
            public bool EnterOrig(Sims3.Gameplay.Actors.Sim a, IRabbitHoleFollowers inst, BeforeEnteringRabbitHoleDelegate beforeEntering, bool canUseCar, Route.RouteMetaType routeMetaType, bool playRouteFailure)
            { return false; } // receives RabbitHole.RouteNearEntranceAndEnterRabbitHole's original header
        }

        // Shared by the lot-trip shortcuts: counts a call in [idx], and if the townie and the lot are both unseen and far
        // apart, places the townie on a safe spot on the lot ([idx+1]) or gives up ([idx+2], no safe spot). Lots with their
        // own arrival code (houseboats, apartment shells, resorts, diving lots, the active household's lot) are left alone,
        // and so are townies whose household has pets (the game may add a pet or horse as a follower after the move, which
        // would then start far away).
        static bool ShortcutToLot(Sims3.Gameplay.Actors.Sim sim, Lot lot, string name, int idx)
        {
            int[] c = PatchCounters();
            c[idx]++;
            if (!ShortcutOn(name) || sim == null || lot == null || lot.IsWorldLot || sim.LotCurrent == lot) return false;
            if (lot.IsHouseboatLot() || lot.BuildableShells.Length > 0 || lot == Household.ActiveHouseholdLot) return false;
            if (lot.CommercialLotSubType == CommercialLotSubType.kEP10_Resort || lot.CommercialLotSubType == CommercialLotSubType.kEP10_Diving) return false;
            if (sim.Household != null && sim.Household.Pets.Count > 0) return false;
            if (!TownieUnseen(sim, null) || (sim.Position - lot.Position).Length() <= kShortcutMinDistance || !FarFromViewers(lot.Position)) return false;
            Sims3.SimIFace.Vector3 pos, fwd;
            if (!SafeSpot(sim, lot, lot.EntryPoint(), out pos, out fwd)) { c[idx + 2]++; return false; }
            string from = sim.LotCurrent != null ? sim.LotCurrent.Name : "street";
            PlaceSim(sim, pos, fwd);
            c[idx + 1]++;
            ShortcutLog(sim.FullName + " => " + lot.Name + " (" + name + " from " + from + ")");
            return true;
        }

        // Home visits (VisitLot) and plain lot trips (GoToLot, GoToCommunityLot): same as community visits, the game's
        // Run follows with a short route to the door and its own arrival. Counters: visitlot [10-12], gotolot [13-15],
        // gotocommunity [16-18].
        public class VisitLotHook : VisitLot
        {
            public bool RunHook() { ShortcutToLot(Actor, Target, "visitlot", 10); return RunOrig(); }
            public bool RunOrig() { return false; } // receives VisitLot.Run's original header
        }
        public class GoToLotHook : GoToLot
        {
            public bool RunHook()
            {
                Sims3.Gameplay.Actors.Sim sim = Actor as Sims3.Gameplay.Actors.Sim;
                if (sim != null && (Followers == null || Followers.Count == 0)) ShortcutToLot(sim, Target, "gotolot", 13);
                return RunOrig();
            }
            public bool RunOrig() { return false; } // receives GoToLot.Run's original header
        }
        public class GoToCommunityLotHook : GoToCommunityLot
        {
            public bool RunHook() { ShortcutToLot(Actor, Target, "gotocommunity", 16); return RunOrig(); }
            public bool RunOrig() { return false; } // receives GoToCommunityLot.Run's original header
        }

        // Visits to community lots (PLAN fix b3): parks, beaches, venues. An unseen townie going alone to an unseen lot far
        // away is placed at a safe spot on that lot, then VisitCommunityLot.Run runs as usual: its route to the front door
        // is now a few metres, and arrival (success count, "do something after arriving") is the game's own. The follower
        // list is preset empty so the random "bring a pet or housemate" pick, whose followers would start far away, is
        // skipped for that trip. Resorts, diving lots, houseboats and apartment shells keep their own code.
        // Counters: [7] runs, [8] shortcuts, [9] no safe spot.
        public class VisitCommunityLotHook : VisitCommunityLot
        {
            public bool RunHook()
            {
                if ((mFollowers == null || mFollowers.Count == 0) && ShortcutToLot(Actor, Target, "visit", 7))
                    mFollowers = new List<Sims3.Gameplay.Actors.Sim>();
                return RunOrig();
            }
            public bool RunOrig() { return false; } // receives VisitCommunityLot.Run's original header
        }

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
            if (a.Has("rabbithole")) AppDomain.CurrentDomain.SetData("ts3agent.shortcut.rabbithole", a.Bool("rabbithole", true));
            r["rabbithole"] = ShortcutOn("rabbithole");
            r["rabbithole_calls"] = c[4];
            r["rabbithole_shortcuts"] = c[5];
            r["rabbithole_no_spot"] = c[6];
            string[] lotTrips = { "visit", "visitlot", "gotolot", "gotocommunity" };
            for (int i = 0; i < lotTrips.Length; i++)
            {
                string n = lotTrips[i];
                if (a.Has(n)) AppDomain.CurrentDomain.SetData("ts3agent.shortcut." + n, a.Bool(n, true));
                r[n] = ShortcutOn(n);
                r[n + "_runs"] = c[7 + 3 * i];
                r[n + "_shortcuts"] = c[8 + 3 * i];
                r[n + "_no_spot"] = c[9 + 3 * i];
            }
            r["recent"] = GoHomeLog();
            return r;
        }
    }
}
