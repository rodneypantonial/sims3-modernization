using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using Sims3.Gameplay;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Abstracts;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // The fixes (docs/04 F8-F12). Each replaces a game method by copying a hook's MonoMethod header over it before the
    // interpreter compiles it, so they are applied once, at tuning time (Instantiator), before any world loads. A hook
    // calls the original only through an instance stub on its own class that received the original's header.
    // State shared with test tools through AppDomain data (the ts3agent test mod reads and switches it):
    //   ts3fixes.applied (log), ts3fixes.counters (int[]), ts3fixes.switches (name -> bool, overrides the tuning),
    //   ts3fixes.recent (last shortcuts), ts3fixes.moved (every Sim a shortcut moved).
    public static partial class Fixes
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


        public static void Apply()
        {
            if (AppDomain.CurrentDomain.GetData("ts3fixes.applied") != null) return;
            AppDomain.CurrentDomain.SetData("ts3fixes.applied", new List<object>());
            PatchCounters();
            Patch(typeof(GoHome), "Run", typeof(GoHomeHook), "RunHook", "RunOrig");
            Patch(typeof(RabbitHole), "RouteNearEntranceAndEnterRabbitHole", typeof(RabbitHoleHook), "EnterHook", "EnterOrig");
            Patch(typeof(VisitCommunityLot), "Run", typeof(VisitCommunityLotHook), "RunHook", "RunOrig");
            Patch(typeof(VisitLot), "Run", typeof(VisitLotHook), "RunHook", "RunOrig");
            Patch(typeof(GoToLot), "Run", typeof(GoToLotHook), "RunHook", "RunOrig");
            Patch(typeof(GoToCommunityLot), "Run", typeof(GoToCommunityLotHook), "RunHook", "RunOrig");
            if (Instantiator.kWalkStyleComparer)
                Patch(typeof(Sims3.Gameplay.Actors.SimRoutingComponent).GetNestedType("WalkStyleComparer", BindingFlags.NonPublic), "Compare", typeof(WalkStyleCompareHook), "CompareHook", null);
            Patch(typeof(Sims3.Gameplay.Autonomy.ReactionBroadcaster), "PulseSims", typeof(BroadcasterPulseHook), "PulseHook", "PulseOrig");
            ApplyRouteFixes();
            ApplyRouteBackoff();
            ApplyOverwatchStuck(false);
            ApplyPortalAdaptorLeak();
            ApplyCasGrid();
            ApplySpatialCache();
            ApplyFewerPlans();
            ApplyRefTable();
            ApplyCachedReflection();
            ApplyUiCaches();
            ApplyNameCache();
            ApplyScriptHotSpots();
        }

        static List<object> AppliedLog() { return (List<object>)AppDomain.CurrentDomain.GetData("ts3fixes.applied"); }

        // Counters, read by test tools: [0-3] go-home runs/shortcuts/no spot/loop guard, [4-6] buildings, [7-18] lot
        // trips (visit, visitlot, gotolot, gotocommunity: runs/shortcuts/no spot each), [21] houseboat placements,
        // [22-23] broadcaster pulses/skipped, [24-26] route backoff, [27-36] route fixes (RouteFixes.cs), [37-39] SafeSpot
        // verdict cache hits/misses/full checks, [40-43] save cleanup (SaveCleanup.cs), [44-45] Overwatch stuck-check
        // plans/yields (OverwatchStuck.cs), [46-51] watchdog idle cases (RouteFixes.cs: idle on an unroutable spot, hidden
        // room, stationary a sim-day, route plans failing, routability checks asked, route-fail Sims tracked), [52] empty life-event lists trimmed, [53] memory snapshots capped (SaveCleanup.cs), [54-55] route
        // adaptor callbacks unsubscribed after DoPortal / stale ones removed by the save cleanup (PortalAdaptorLeak.cs), [56-58] async
        // route plans started/yields/poll cap reached (RouteBackoff.cs, with ts3native [RouteAsync]), [59-61] CAS incremental
        // fills started / hair preset cache hits / preset lookups run (CasGrid.cs), [62-64] lot object query cache hits /
        // misses / clears on object destroy (SpatialCache.cs), [65-67] routability cache hits / plans
        // recorded, stuck checks skipped for busy Sims (FewerPlans.cs), [68-70] save identity tables converted / most entries
        // in one table / table grows (RefTable.cs), [71-73] serializer type caches started / answers from the cache /
        // answers stored (CachedReflection.cs), [74-75] motive bar updates without / with image loads, [76-77] moodlet grid
        // in-place updates / rebuilds (UiCaches.cs), [78-79] Sim name cache hits / misses (NameCache.cs),
        // [80-82] lot detail priority answers from the tick set / tick sets built / verify mismatches,
        // [83-84] cane-walk checks answered early / passed to the game, [85-87] hours-of-operation memo hits / misses / verify mismatches,
        // [88-89] tooltip window walks skipped / run (ScriptHotSpots.cs).
        static int[] sCountersCache;
        static int[] PatchCounters()
        {
            if (sCountersCache != null) return sCountersCache; // hooks call this hundreds of times a second
            int[] c = AppDomain.CurrentDomain.GetData("ts3fixes.counters") as int[];
            if (c == null) { c = new int[90]; AppDomain.CurrentDomain.SetData("ts3fixes.counters", c); }
            return sCountersCache = c;
        }

        // A fix is on when the test switch says so, else per the tuning (Instantiator).
        static Dictionary<string, bool> sSwitches;
        static bool ShortcutOn(string name)
        {
            if (sSwitches == null)
            {
                sSwitches = AppDomain.CurrentDomain.GetData("ts3fixes.switches") as Dictionary<string, bool>;
                if (sSwitches == null) { sSwitches = new Dictionary<string, bool>(); AppDomain.CurrentDomain.SetData("ts3fixes.switches", sSwitches); }
            }
            bool on;
            if (sSwitches.TryGetValue(name, out on)) return on;
            switch (name)
            {
                case "gohome": return Instantiator.kGoHomeShortcut;
                case "rabbithole": return Instantiator.kBuildingTripShortcut;
                case "visit": case "visitlot": case "gotolot": case "gotocommunity": return Instantiator.kLotTripShortcut;
                case "broadcast": return Instantiator.kBroadcasterThrottle;
                case "virtualhome": return Instantiator.kVirtualHomeShortcut;
                case "failreplan": return Instantiator.kNoRouteFailWalkOffscreen;
                case "watchdog": return Instantiator.kStuckWatchdog;
                case "watchdogidle": return Instantiator.kStuckWatchdogIdle;
                case "backoff": return Instantiator.kRouteFailureBackoff;
                case "stations": return Instantiator.kNoStationDetourOffscreen;
                case "portaladaptorleak": return Instantiator.kPortalAdaptorLeak;
                case "casgrid": case "casmemo": return Instantiator.kCasGrid;
                case "spatialcache": return Instantiator.kSpatialCache;
                case "routablecache": return Instantiator.kRoutableCache;
                case "stuckcheckbusy": return Instantiator.kStuckCheckBusy;
                case "fastreftable": return Instantiator.kFastRefTable;
                case "cachedreflection": return Instantiator.kCachedReflection;
                case "motivebar": return Instantiator.kMotiveBarImageCache;
                case "buffgrid": return Instantiator.kBuffGridInPlace;
                case "namecache": return Instantiator.kSimNameCache;
                case "detailprio": return Instantiator.kDetailPriorityCache;
                case "hoursmemo": return Instantiator.kHoursMemo;
                case "caneearly": return Instantiator.kCaneEarlyOut;
                case "tooltipthrottle": return Instantiator.kTooltipThrottle;
                case "detailprioverify": case "hoursmemoverify": return false; // verify switches: test runs only, no tuning
                case "spotspread": return true;
                default: return true;
            }
        }

        static void Patch(Type target, string method, Type hook, string replacement, string stub)
        {
            Patch(target, method, null, hook, replacement, stub);
        }

        // args picks one overload of target.method (null when the name is unique). True when the patch went in.
        static bool Patch(Type target, string method, Type[] args, Type hook, string replacement, string stub)
        {
            const BindingFlags all = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            try
            {
                MethodInfo t = args == null ? target.GetMethod(method, all) : target.GetMethod(method, all, null, args, null), r = hook.GetMethod(replacement, all);
                if (Marshal.ReadInt32(new IntPtr(t.MethodHandle.Value.ToInt32() + 0x14)) != 0)
                    throw new InvalidOperationException("already compiled");
                if (stub != null) SwapMethod(hook.GetMethod(stub, all), t);
                SwapMethod(t, r);
                AppliedLog().Add(target.Name + "." + method + ": ok");
                return true;
            }
            catch (Exception e) { AppliedLog().Add(target.Name + "." + method + ": " + e.Message); return false; }
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
                PlaceSim(Actor, pos, fwd, "home " + Target.Name);
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
                // GoHome.Run may add the household horse as a follower after this point; keep pet households on foot
                if (sim.Household != null && sim.Household.Pets.Count > 0) return false;
                if (sim.LotHome != home || sim.LotCurrent == home || home == Household.ActiveHouseholdLot) return false;
                if (!HouseboatOk(home) || home.BuildableShells.Length > 0) return false;
                return FarFromViewers(home.Position);
            }
        }

        // Outside the camera's reach (a radius that grows as the camera zooms out) and not near any active-household Sim.
        static bool FarFromViewers(Sims3.SimIFace.Vector3 p)
        {
            RefreshViewers();
            if ((p - sCamTarget).Length() < sReach || (p - sCamPos).Length() < sReach) return false;
            for (int i = 0; i < sViewerCount; i++)
                if ((p - sViewers[i]).Length() < kShortcutMinDistance) return false;
            return true;
        }

        // Camera and active-household positions, read once per sim tick (the broadcaster hook alone asks ~800 times/s).
        static long sViewTick = -1;
        static Sims3.SimIFace.Vector3 sCamTarget, sCamPos;
        static float sReach;
        static Sims3.SimIFace.Vector3[] sViewers = new Sims3.SimIFace.Vector3[16];
        static int sViewerCount;
        static void RefreshViewers()
        {
            long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
            if (now == sViewTick) return;
            sViewTick = now;
            sCamTarget = CameraController.GetTarget();
            sCamPos = CameraController.GetPosition();
            sReach = Math.Max(kShortcutMinDistance, 2f * Math.Abs(sCamPos.y - sCamTarget.y));
            sViewerCount = 0;
            if (Household.ActiveHousehold == null) return;
            foreach (Sims3.Gameplay.Actors.Sim s in Household.ActiveHousehold.AllActors)
            {
                if (s == null) continue;
                if (sViewerCount == sViewers.Length) Array.Resize(ref sViewers, sViewers.Length * 2);
                sViewers[sViewerCount++] = s.Position;
            }
        }

        const float kShortcutMinDistance = 80f;

        // A spot for a shortcut Sim on `lot` near `near`: on the lot (else the game still sees the Sim away) and passing the
        // game's Sim.IsPointInLotSafelyRoutable (a plan from the spot to the lot's corners and edge midpoints), so the Sim can
        // walk on from there instead of standing stuck. That check is a full route plan (362 ms once in Bridgeport, docs/04
        // F18), so its verdict is cached per 0.5 m cell and route profile (SpotVerdict). Spots next to the lot's verified ones are tried first,
        // rotating through them so arrivals spread out; then fresh spots from `near` (the entrance), whose cells may cost a
        // full check within the per-tick budget. No verified spot = false, and the caller runs the game's own trip.
        static bool SafeSpot(Sims3.Gameplay.Actors.Sim sim, Lot lot, Sims3.SimIFace.Vector3 near, out Sims3.SimIFace.Vector3 pos, out Sims3.SimIFace.Vector3 fwd)
        {
            SpotVerdicts v = Verdicts(lot);
            RouteProfile prof = ProfileOf(sim);
            // "spotspread" off = the earlier build: always start from the lot's last verified spot (A/B for the Bridgeport slowdown)
            bool spread = ShortcutOn("spotspread");
            for (int i = 0; i < 2 + 3; i++)
            {
                bool fresh = i >= 2;
                if (!fresh && v.Good.Count == 0) continue;
                Sims3.SimIFace.Vector3 start = fresh ? near : v.Good[spread ? v.Next++ % v.Good.Count : v.Good.Count - 1];
                if (!GlobalFunctions.FindGoodLocation(sim, new World.FindGoodLocationParams(start), out pos, out fwd)) continue;
                if (LotManager.GetLotAtPoint(pos) != lot || SpotVerdict(sim, lot, v, prof, pos, fresh, false, true) != true) continue;
                if (lot.IsHouseboatLot()) PatchCounters()[21]++;
                return true;
            }
            pos = fwd = Sims3.SimIFace.Vector3.Invalid;
            return false;
        }

        // Verdicts of the game's Sim.IsPointInLotSafelyRoutable for one lot (SafeSpot and the stuck watchdog). The check
        // plans with CreateRouteAsAdult(): the Sim's own species/gender flags with the age forced to adult, plus every option
        // SetRouteOptions sets from the Sim (real age, posture, ghost buff, mermaid, lifeguard, UFO, frozen ponds, pet,
        // horse, special-service pass-through, or none at all under an IHasCustomRouteOptions interaction), then water
        // planning = IsHuman; no valid-room limit (the adult flags skip it). So a verdict is keyed by the cell plus that
        // exact route profile (RouteProfile, read back from a route built the same way), never by IsHuman alone. Kept for
        // one sim-day, and dropped when the lot's household changes (a move-in or move-out refurnishes it), the lot moved
        // (a houseboat at another dock) or the clock went back (another save or world was loaded).
        class SpotVerdicts
        {
            public long Created;
            public ulong Household;
            public Sims3.SimIFace.Vector3 LotPos;
            public readonly Dictionary<VerdictKey, bool> Cells = new Dictionary<VerdictKey, bool>(new VerdictKeyComparer());
            public readonly List<Sims3.SimIFace.Vector3> Good = new List<Sims3.SimIFace.Vector3>();
            public int Next;
        }
        static readonly Dictionary<ulong, SpotVerdicts> sSpotVerdicts = new Dictionary<ulong, SpotVerdicts>();
        static readonly long kVerdictTicks = Sims3.Gameplay.Utilities.SimClock.ConvertToTicks(1f, Sims3.Gameplay.Utilities.TimeUnit.Days);
        const int kMaxGoodSpots = 8, kMaxSpotChecksPerTick = 1;
        static long sSpotCheckTick = -1;
        static int sSpotChecks;

        static SpotVerdicts Verdicts(Lot lot)
        {
            long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
            ulong hh = lot.Household != null ? lot.Household.HouseholdId : 0;
            SpotVerdicts v;
            if (!sSpotVerdicts.TryGetValue(lot.LotId, out v) || now < v.Created || now - v.Created > kVerdictTicks || v.Household != hh
                || (lot.Position - v.LotPos).Length() > 0.5f)
            {
                v = new SpotVerdicts();
                v.Created = now;
                v.Household = hh;
                v.LotPos = lot.Position;
                sSpotVerdicts[lot.LotId] = v;
            }
            return v;
        }

        // What the game's check feeds the planner for this Sim right now: the route's age/gender/species flags and every
        // RouteOption/RouteOption2 bit, read back from a route built exactly as IsPointInLotSafelyRoutable builds it. Not
        // cacheable (Ok = false) for a Sim with a parent (a boat route gets the boat as follower and ignore-object, which
        // the option bits do not show; vehicles, beds).
        struct RouteProfile { public bool Ok; public uint Flags; public long Opts, Opts2; }
        static Route.RouteOption[] sRouteOpts;
        static Route.RouteOption2[] sRouteOpts2;
        static RouteProfile ProfileOf(Sims3.Gameplay.Actors.Sim sim)
        {
            RouteProfile p = new RouteProfile();
            if (sim == null || sim.Parent != null || sim.SimRoutingComponent == null || sim.SimDescription == null) return p;
            if (sRouteOpts == null)
            {
                sRouteOpts = (Route.RouteOption[])Enum.GetValues(typeof(Route.RouteOption));
                sRouteOpts2 = (Route.RouteOption2[])Enum.GetValues(typeof(Route.RouteOption2));
            }
            Route r = sim.SimRoutingComponent.CreateRouteAsAdult();
            r.SetOption(Route.RouteOption.EnableWaterPlanning, sim.IsHuman); // as the game's check does next
            foreach (Route.RouteOption o in sRouteOpts) if (o != Route.RouteOption.None && r.GetOption(o)) p.Opts |= (long)o;
            foreach (Route.RouteOption2 o in sRouteOpts2) if (o != Route.RouteOption2.None && r.GetOption2(o)) p.Opts2 |= (long)o;
            p.Flags = (sim.SimDescription.SimFlags & 0xFFFFFF80u) | 0x20u; // CreateRouteAsAdult's flags
            p.Ok = true;
            return p;
        }

        // Cells are 0.5 m and floor-aligned, so a cell never straddles a wall or fence on a tile edge; y in 1 m steps keeps
        // floors apart.
        struct VerdictKey { public long Cell, Opts, Opts2; public uint Flags; }
        class VerdictKeyComparer : IEqualityComparer<VerdictKey> // explicit: the default one boxes structs in this Mono
        {
            public bool Equals(VerdictKey a, VerdictKey b) { return a.Cell == b.Cell && a.Opts == b.Opts && a.Opts2 == b.Opts2 && a.Flags == b.Flags; }
            public int GetHashCode(VerdictKey k) { return k.Cell.GetHashCode() ^ (k.Opts.GetHashCode() * 31) ^ (k.Opts2.GetHashCode() * 17) ^ (int)k.Flags; }
        }

        // The verdict for `pos`: cached when the profile is cacheable, else a full check when `mayCheck` and this sim tick's
        // budget (shared by SafeSpot and the watchdog) allow; null = no verdict now (SafeSpot treats it as unsafe, the
        // watchdog retries next tick). `recheckBad`: a cached "not routable" is re-checked (the watchdog acts on it; a stale
        // bad verdict must not reset a Sim). `addGood`: remember the spot for SafeSpot's spreading.
        // Counters: [37] cache hits, [38] misses, [39] full checks run.
        static VerdictKey KeyOf(RouteProfile p, Sims3.SimIFace.Vector3 pos)
        {
            VerdictKey key = new VerdictKey();
            key.Cell = ((long)((int)Math.Floor(pos.x * 2f) & 0xFFFFF) << 40) | ((long)((int)Math.Floor(pos.z * 2f) & 0xFFFFF) << 20)
                | (long)(uint)((int)Math.Floor(pos.y) & 0xFFFFF);
            key.Opts = p.Opts; key.Opts2 = p.Opts2; key.Flags = p.Flags;
            return key;
        }

        static bool? SpotVerdict(Sims3.Gameplay.Actors.Sim sim, Lot lot, SpotVerdicts v, RouteProfile p, Sims3.SimIFace.Vector3 pos, bool mayCheck, bool recheckBad, bool addGood)
        {
            int[] c = PatchCounters();
            VerdictKey key = KeyOf(p, pos);
            bool ok;
            if (p.Ok && v.Cells.TryGetValue(key, out ok) && (ok || !recheckBad)) { c[37]++; return ok; }
            c[38]++;
            long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
            if (now != sSpotCheckTick) { sSpotCheckTick = now; sSpotChecks = 0; }
            if (!mayCheck || sSpotChecks >= kMaxSpotChecksPerTick) return null;
            sSpotChecks++;
            c[39]++;
            sInOwnCheck = true;
            try { ok = sim.IsPointInLotSafelyRoutable(lot, pos); } finally { sInOwnCheck = false; }
            if (p.Ok) v.Cells[key] = ok;
            if (ok && addGood && p.Ok && v.Good.Count < kMaxGoodSpots) v.Good.Add(pos);
            return ok;
        }

        // Houseboat lots (PLAN fix b2): most of their cost is Sims boarding and leaving through gangways
        // (SimRoutingComponent.CheckAndUpdateForHouseboats, 5-8% in Isla Paradiso), not the boats moving, so trips to a
        // houseboat may be shortcut too, but only while it is docked and not moving.
        static bool HouseboatOk(Lot lot)
        {
            if (!lot.IsHouseboatLot()) return true;
            Sims3.Gameplay.Objects.Island.Houseboat b = lot.GetHouseboat();
            return b != null && b.IsDocked;
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
            List<object> log = RecentLog();
            if (log.Count >= 40) log.RemoveAt(0);
            log.Add(line);
        }

        // Every Sim a shortcut moved this session (name -> last move and where it was placed), so a stuck Sim found
        // later can be matched for certain; the 40-line log above only covers the last few seconds.
        static Dictionary<string, object> MovedSims()
        {
            Dictionary<string, object> d = AppDomain.CurrentDomain.GetData("ts3fixes.moved") as Dictionary<string, object>;
            if (d == null) { d = new Dictionary<string, object>(); AppDomain.CurrentDomain.SetData("ts3fixes.moved", d); }
            return d;
        }

        static void PlaceSim(Sims3.Gameplay.Actors.Sim sim, Sims3.SimIFace.Vector3 pos, Sims3.SimIFace.Vector3 fwd, string what)
        {
            PlaceSim(sim, pos, fwd);
            MovedSims()[sim.FullName] = what + " @" + Math.Round(pos.x, 1) + "," + Math.Round(pos.z, 1) + " " + Sims3.Gameplay.Utilities.SimClock.CurrentTime().ToString();
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
                            PlaceSim(a, pos, fwd, "building " + proxy.CatalogName);
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
        // own arrival code (moving houseboats, apartment shells, resorts, diving lots, the active household's lot) are left alone,
        // and so are townies whose household has pets (the game may add a pet or horse as a follower after the move, which
        // would then start far away).
        static bool ShortcutToLot(Sims3.Gameplay.Actors.Sim sim, Lot lot, string name, int idx)
        {
            int[] c = PatchCounters();
            c[idx]++;
            if (!ShortcutOn(name) || sim == null || lot == null || lot.IsWorldLot || sim.LotCurrent == lot) return false;
            if (!HouseboatOk(lot) || lot.BuildableShells.Length > 0 || lot == Household.ActiveHouseholdLot) return false;
            if (lot.CommercialLotSubType == CommercialLotSubType.kEP10_Resort || lot.CommercialLotSubType == CommercialLotSubType.kEP10_Diving) return false;
            if (sim.Household != null && sim.Household.Pets.Count > 0) return false;
            if (!TownieUnseen(sim, null) || (sim.Position - lot.Position).Length() <= kShortcutMinDistance || !FarFromViewers(lot.Position)) return false;
            Sims3.SimIFace.Vector3 pos, fwd;
            if (!SafeSpot(sim, lot, lot.EntryPoint(), out pos, out fwd)) { c[idx + 2]++; return false; }
            string from = sim.LotCurrent != null ? sim.LotCurrent.Name : "street";
            PlaceSim(sim, pos, fwd, name + " " + lot.Name);
            c[idx + 1]++;
            ShortcutLog(sim.FullName + " => " + lot.Name + " (" + name + " from " + from + ")");
            return true;
        }

        // Walk-style comparer (PLAN fix a): SimRoutingComponent's private WalkStyleComparer.Compare looks both walk styles up
        // in a Dictionary<WalkStyle, int>, and with the default enum comparer each lookup boxes and calls Enum.Equals and
        // GetHashCode: 5% of script time as Enum.Equals alone in Bridgeport (docs/04 F7). The enum is sequential from 0, so
        // an array indexed by value gives the same priorities; a style missing from the table still throws, as before.
        public class WalkStyleCompareHook
        {
            static int[] sPrio;
            public int CompareHook(Sims3.Gameplay.Actors.Sim.WalkStyle x, Sims3.Gameplay.Actors.Sim.WalkStyle y)
            {
                if (sPrio == null) sPrio = WalkStylePriorities();
                int px = (int)x < sPrio.Length ? sPrio[(int)x] : -1, py = (int)y < sPrio.Length ? sPrio[(int)y] : -1;
                if (px < 0 || py < 0) throw new KeyNotFoundException();
                return py - px;
            }

            static int[] WalkStylePriorities()
            {
                Sims3.Gameplay.Actors.Sim.WalkStyle[] order = (Sims3.Gameplay.Actors.Sim.WalkStyle[])typeof(Sims3.Gameplay.Actors.SimRoutingComponent)
                    .GetField("kWalkStylePriorities", BindingFlags.Static | BindingFlags.NonPublic).GetValue(null);
                int max = 0;
                foreach (Sims3.Gameplay.Actors.Sim.WalkStyle w in order) max = Math.Max(max, (int)w);
                int[] p = new int[max + 1];
                for (int i = 0; i < p.Length; i++) p[i] = -1;
                for (int i = 0; i < order.Length; i++) p[(int)order[i]] = i; // later duplicates win, as the dictionary's indexer
                return p;
            }
        }

        // Reaction broadcasters (PLAN fix c): every broadcaster (music, fires, smells, ...) pulses about once per second:
        // a spatial query for Sims in its radius plus line-of-sight checks per Sim (ReactionBroadcaster.PulseSims, 7% of
        // script time in Bridgeport). One far from the camera and the active household now pulses about one call in four
        // (staggered by a shared call counter), so an unseen Sim starts or stops reacting a few sim-seconds later; near the
        // player nothing changes. ReactionBroadcaster has no parameterless constructor, so the hook is a plain class.
        // Counters: [22] pulses, [23] skipped.
        public class BroadcasterPulseHook
        {
            static int sCalls;
            public void PulseHook(float pulseRadius, bool fullSimulation)
            {
                Sims3.Gameplay.Autonomy.ReactionBroadcaster b = (Sims3.Gameplay.Autonomy.ReactionBroadcaster)(object)this;
                int[] c = PatchCounters();
                c[22]++;
                Sims3.Gameplay.Interfaces.IGameObject o = b.BroadcastingObject;
                if (ShortcutOn("broadcast") && o != null && (++sCalls & 3) != 0 && FarFromViewers(o.Position))
                {
                    c[23]++;
                    return;
                }
                PulseOrig(pulseRadius, fullSimulation);
            }
            public void PulseOrig(float pulseRadius, bool fullSimulation) { } // receives ReactionBroadcaster.PulseSims' original header
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

        static List<object> RecentLog()
        {
            List<object> l = AppDomain.CurrentDomain.GetData("ts3fixes.recent") as List<object>;
            if (l == null) { l = new List<object>(); AppDomain.CurrentDomain.SetData("ts3fixes.recent", l); }
            return l;
        }

    }
}
