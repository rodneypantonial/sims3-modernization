using System;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Autonomy;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Objects.Vehicles;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Live-mode fallback for unseen Sims (docs/202, "hitch guard"): no freeze >= 300 ms from routing work for a Sim the player
    // cannot see. All switches default off.
    //   unseenfbstuck    (phase B, docs/202 §3.3): the stuck check plans nothing for an unseen Sim and re-arms itself at the
    //                    same round (the game's own CreateAlarm); a Sim that moves ends it, a seen Sim gets the game's check.
    //   unseenfbplan     (phase A, §3.1-§3.2): a plan whose follower is unseen is marked for ts3native ([UnseenFallback]),
    //                    which cuts that sync plan or route job at DeadlineMs through the existing no-path exits; a cut plan
    //                    returns as an ordinary failed plan. Then a no-retry window (same Sim, same 1 m goal cell, 1.5 s) and
    //                    a per-Sim cap (4 cuts in 10 s, then uncut) (UfLimiter).
    //   unseenfbteleport (§3.2 item 1, needs unseenfbplan): a cut own-task Route.Plan of a standing unseen townie places it at
    //                    a point-routable, unseen route goal (the trip shortcuts' PlaceSim) and plans once more from there.
    // Counters: [303] stuck checks deferred, [304] stuck checks run (Sim seen), [305] plans marked, [306] marked plans cut,
    // [307] teleports, [308] teleports refused (no spot), [309] no-retry answers, [310] plans checked but seen,
    // [311] marks refused (no ts3native, [UnseenFallback] off or not installed), [312] longest marked plan (ms, wall).
    public static partial class Fixes
    {
        static readonly UfLimiter sUf = new UfLimiter();
        static bool sInRoutableCheck;  // inside Sim.IsPointInLotSafelyRoutable (RoutableCacheHook): never marked
        static bool sUfInLeg;          // D-leave walks its visible leg (UfLeave): plans inside are left to the game

        // docs/202 §5.6 (5): plans of >= kUfSlowPlanMs (wall, the hook's whole call: sync search, or the task's sleep for an async
        // job) are named in ts3native.log with the scope verdict, so long routing hitches can be split into seen Sims (accepted),
        // marked-but-not-cut and never-asked (why -1: no Sim follower, stuck-check probe, own check, pet, D leg, switches off)
        const int kUfSlowPlanMs = 150, kUfSlowPlanLogMax = 200;
        static int sUfLastWhy, sUfSlowLogged;
        static bool sUfLastMarked, sUfLastCut;
        static Sim sUfLastSim;
        static void UfSlowPlanLog(int kind, int ms)
        {
            if (ms < kUfSlowPlanMs || sUfSlowLogged >= kUfSlowPlanLogMax) return;
            sUfSlowLogged++;
            Sim s = sUfLastSim;
            DiagLine("ufplan: kind " + kind + " " + ms + " ms why " + sUfLastWhy + (sUfLastMarked ? " marked" : "") + (sUfLastCut ? " cut" : "")
                + (s != null ? " " + s.FullName + (Simulator.CurrentTask == s.ObjectId ? " own task" : " other task") : ""));
            sUfLastSim = null;
        }

        // docs/202 §2; 0 = unseen. GameStates.IsLiveState = in-world substate id 0 (LiveModeState): Build, Buy, CAS and Edit
        // Town are other substates [verified, decompiled].
        // Same order and codes as UnseenFallbackCore.Why, but each test runs only when the ones before it passed: the camera
        // query (4 GetOption2 ICs per position), the on-screen IC and the posture checks are skipped for Sims an earlier test
        // already calls seen (docs/202 §5.4b).
        static int UfWhy(Sim s)
        {
            if (!GameStates.IsLiveState || GameStates.IsTravelling || GameStates.IsWorldShuttingDown) return UnseenFallbackCore.kNotLive;
            if (s == null || s.HasBeenDestroyed || s.SimDescription == null) return UnseenFallbackCore.kGone;
            if (s.IsSelectable || s.Household == Household.ActiveHousehold) return UnseenFallbackCore.kHousehold;
            if (UfLotSeen(s.LotCurrent)) return UnseenFallbackCore.kActiveLot;
            if (World.IsObjectOnScreen(s)) return UnseenFallbackCore.kOnScreen;
            if (!UfPosUnseen(s.Position, kUfSimRadiusM)) return UnseenFallbackCore.kNearViewers;
            if (UfHeld(s)) return UnseenFallbackCore.kHeld;
            Sim t = s.CurrentInteraction != null ? s.CurrentInteraction.Target as Sim : null;
            return t != null && t != s && (t.IsSelectable || t.Household == Household.ActiveHousehold || World.IsObjectOnScreen(t) || !UfPosUnseen(t.Position, kUfSimRadiusM))
                ? UnseenFallbackCore.kSocialSeen : UnseenFallbackCore.kSeen;
        }

        // docs/202 §2.1 camera checks. Seen unless ts3native's camera query (unseenfb.cpp UfCamMsg: the game's camera frustum
        // and the reach rule max(80 m, 2 |cam y - target y|), wide camera = all seen) says unseen; the map view is all seen; plus
        // our 80 m household rule (FarFromViewers). Without ts3native 0.36.89+ the query answers 0: everything is seen, nothing acts.
        const float kUfSimRadiusM = 3f;  // EA's per-agent on-screen byte tests 3x the model bounds (docs/143 §3)
        static bool UfPosUnseen(Vector3 p, float r)
        {
            if (CameraController.IsMapViewModeEnabled() || !FarFromViewers(p)) return false;
            return UfCamAsk(3, p.x) && UfCamAsk(4, p.y) && UfCamAsk(5, p.z) && UfCamAsk(6, r);
        }
        static bool UfCamAsk(int code, float f) { return sYieldProbe.GetOption2((Sims3.SimIFace.Route.RouteOption2)(0x8300000000000000UL | (ulong)(uint)code << 48 | Bits(f))); }

        // the camera's lot (the lot under the camera target, LotManager.GetLotAtPoint), the active household's lot, or a lot whose
        // bounding circle reaches into the view (frustum or reach); the world lot (streets) is judged by position only
        static bool UfLotSeen(Lot lot)
        {
            if (lot == null || lot.IsWorldLot) return false;
            if (lot == Household.ActiveHouseholdLot || lot == UfCameraLot()) return true;
            long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
            if (now != sUfLotTick) { sUfLotTick = now; sUfLotSeen.Clear(); }
            bool seen;
            if (sUfLotSeen.TryGetValue(lot.LotId, out seen)) return seen;
            Vector3 c = lot.GetCenterPosition();
            float r = 0f;
            Corners k = lot.Corners;
            if (k != null) for (int i = 0; i < 4; i++) r = Math.Max(r, (k[i] - c).Length());
            seen = !UfPosUnseen(c, r);
            sUfLotSeen[lot.LotId] = seen;
            return seen;
        }
        static long sUfLotTick = -1, sUfCamLotTick = -1;
        static readonly System.Collections.Generic.Dictionary<ulong, bool> sUfLotSeen = new System.Collections.Generic.Dictionary<ulong, bool>();
        static Lot sUfCamLot;
        static Lot UfCameraLot()
        {
            long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
            if (now != sUfCamLotTick) { sUfCamLotTick = now; sUfCamLot = LotManager.GetLotAtPoint(CameraController.GetTarget()); }
            return sUfCamLot;
        }

        // §3.2 / §2.1 (2): a teleport goal must be out of view: the point (Sim-sized), its lot (not the camera's, not in view)
        static bool UfGoalUnseen(Vector3 goal)
        {
            if (!UfPosUnseen(goal, kUfSimRadiusM)) return false;
            Lot lot = LotManager.GetLotAtPoint(goal);
            return !UfLotSeen(lot);
        }

        // riding, ridden, in a vehicle, carried, or carrying a child, pet or object (their routes move another object too).
        // Issue #89: the posture chain is walked here, bounded, instead of through Posture.Satisfies, which recurses down
        // PreviousPosture with no end check: EA's Sim.set_Posture can leave two postures pointing at each other on a pop
        // (PosturePopHook below), and Satisfies then overflows the interpreter stack. Same answer as the four Satisfies calls
        // (Satisfaction > 0 at any posture of the chain, Satisfies is not virtual) [verified, decompiled]; a chain that does not end
        // within kPostureChainCap postures is a broken state: held, so nothing of ours moves or fakes this Sim. Counter [371].
        static bool UfHeld(Sim s)
        {
            if (s.IsInRidingPosture || s.IsInBeingRiddenPosture || s.Parent is Vehicle || s.Parent is Sim) return true;
            int n = 0;
            for (Sims3.Gameplay.ActorSystems.Posture p = s.Posture; p != null; p = p.PreviousPosture)
            {
                if (!UnseenFallbackCore.ChainStepOk(++n)) { PatchCounters()[371]++; return true; }
                if (p.Satisfaction(CommodityKind.CarryingChild, null) > 0f || p.Satisfaction(CommodityKind.CarryingPet, null) > 0f
                    || p.Satisfaction(CommodityKind.CarryingObject, null) > 0f || p.Satisfaction(CommodityKind.BeingRidden, null) > 0f) return true;
            }
            return false;
        }

        // docs/202 §3.7: a cut plan (or a no-retry answer) is not a route failure of the Sim: our watchdog's route-fail count
        // (NotePlanResult, read by PlanHook through UfTakeNotFail), the backoff entry (30 sim-minutes of instant failures) and
        // failrepeat's entry for this goal are dropped; Overwatch V2 never saw it (sOwProbe above)
        static bool sUfNotFail;
        static bool UfTakeNotFail() { bool v = sUfNotFail; sUfNotFail = false; return v; }
        static void UfNotAFailure(ulong id, Vector3 dest)
        {
            sUfNotFail = true;
            BackoffKey k = new BackoffKey(id, dest);
            sBackoff.Remove(k);
            sFailRepeat.Remove(k);
        }

        // ---- docs/202 §3.5 D helpers ----

        // adult-like human townie on its own: teen to elder, not a pet, no group (GroupingSituation walks members together), not
        // riding, carried or carrying (UfHeld); the interaction it runs is not one whose point is the walk, nor a social (jigs,
        // two-Sim routes)
        static bool UfPreSim(Sim s)
        {
            SimDescription d = s.SimDescription;
            if (d == null || d.IsPet || !d.TeenOrAbove || !s.IsHuman || UfHeld(s)) return false;
            if (s.GetSituationOfType<Sims3.Gameplay.Situations.GroupingSituation>() != null) return false;
            Sims3.Gameplay.Interactions.InteractionInstance cur = s.CurrentInteraction;
            return cur != null && !(cur is Sims3.Gameplay.Socializing.SocialInteraction) && !UfPreCore.Denied(cur.GetType().FullName);
        }

        // §3.5 (1) door and lot rules: the cached verdicts are keyed by route profile, not by household or Sim, so per-Sim door
        // permissions are not in them; D refuses every lot with a door or gate whose lock is not "anybody", a single-gender door
        // or a one-way room (the per-lot scan cached for one sim-hour). Only community lots and the Sim's own home; no apartments,
        // shells, moving houseboats, resorts, diving lots.
        static bool UfPreLot(Sim sim, Lot lot)
        {
            if (lot == null || lot.IsWorldLot || lot == Household.ActiveHouseholdLot) return false;
            if (!lot.IsCommunityLot && lot != sim.LotHome) return false;
            if (lot.IsApartmentLot || lot.BuildableShells.Length > 0 || !HouseboatOk(lot)) return false;
            if (lot.CommercialLotSubType == CommercialLotSubType.kEP10_Resort || lot.CommercialLotSubType == CommercialLotSubType.kEP10_Diving) return false;
            long now = Sims3.Gameplay.Utilities.SimClock.CurrentTicks;
            UfDoors e;
            if (sUfDoors.TryGetValue(lot.LotId, out e) && e.At <= now && now - e.At < kUfDoorTicks) return e.Open;
            e.At = now;
            e.Open = true;
            foreach (CommonDoor d in lot.GetObjects<CommonDoor>())
                if (d == null || d.LockType != CommonDoor.tLock.Anybody || d is DoorSingleGender || d.OneWayRoomDestinationId != 0) { e.Open = false; break; }
            if (sUfDoors.Count >= 256) sUfDoors.Clear();
            sUfDoors[lot.LotId] = e;
            return e.Open;
        }
        struct UfDoors { public long At; public bool Open; }
        static readonly System.Collections.Generic.Dictionary<ulong, UfDoors> sUfDoors = new System.Collections.Generic.Dictionary<ulong, UfDoors>();
        static readonly long kUfDoorTicks = Sims3.Gameplay.Utilities.SimClock.ConvertToTicks(1f, Sims3.Gameplay.Utilities.TimeUnit.Hours);

        // outdoors (room 0) and on the ground (within 0.5 m of the terrain): no stairs, elevators or rooms behind doors
        static bool UfOutdoorGround(Vector3 p)
        {
            return World.GetRoomId(p) == 0 && Math.Abs(p.y - World.GetTerrainHeight(p.x, p.z)) < 0.5f;
        }

        // first managed frame above the route funnels and our hooks ("FullType.Method"), as routesrc reads it; Lot.PlanToLot(Ex)
        // is skipped too, so RoutingComponent.RouteToLot (plans through Lot.PlanToLot) is seen as the caller
        static string UfCaller(out string above)
        {
            above = null;
            for (int i = 1; i < 32; i++)
            {
                System.Reflection.MethodBase m = new System.Diagnostics.StackFrame(i, false).GetMethod();
                if (m == null) return null;
                string ft = m.DeclaringType != null ? m.DeclaringType.FullName : null;
                if (RouteSrcCore.IsPassThrough(ft, m.Name) || UfPreCore.PlanToLotFrame(ft, m.Name) || UfPreCore.OwnFrame(ft, m.Name)) continue;
                System.Reflection.MethodBase n = new System.Diagnostics.StackFrame(i + 1, false).GetMethod();
                if (n != null) above = (n.DeclaringType != null ? n.DeclaringType.FullName : null) + "." + n.Name;
                return ft + "." + m.Name;
            }
            return null;
        }

        // the caller follows the route (UfPreCore.FollowsRoute); a refused caller frame is counted by name (AppDomain
        // ts3fixes.unseenfb.callers, read by the agent as unseenfbpre_callers; 64 names at most) to show what a wider allow-list
        // would reach
        static bool UfFollows()
        {
            string above, f = UfCaller(out above);
            if (UfPreCore.FollowsRoute(f, above)) return true;
            var d = AppDomain.CurrentDomain.GetData("ts3fixes.unseenfb.callers") as System.Collections.Generic.Dictionary<string, int>;
            if (d == null) { d = new System.Collections.Generic.Dictionary<string, int>(); AppDomain.CurrentDomain.SetData("ts3fixes.unseenfb.callers", d); }
            int n;
            if (d.TryGetValue(f ?? "?", out n) || d.Count < 64) d[f ?? "?"] = n + 1;
            return false;
        }

        // docs/202 §3.5.1 reject gate: counts slot (328-340, Fixes.cs) and answers "not ours"
        static int UfRj(int slot) { PatchCounters()[slot]++; return 0; }
        static bool UfNo(int slot) { PatchCounters()[slot]++; return false; }

        // the game's flood colours (ts3native UfColourMsg, codes 11-13 after x / z; 7-10 = the C1-C4 list): 1 = start and goal in one world component,
        // -1 = different components (no land path), 0 = unknown (no waypoint near, several colours near, older ts3native). A point
        // on a lot with no waypoint near is asked again at its lot's entry point (the lot's street side).
        static int UfSameComponent(Sim sim, Vector3 start, Vector3 dest, Lot goalLot)
        {
            return UfColourA(start, sim.LotCurrent) ? UfColourB(dest, goalLot) : 0;
        }
        // A = the colour at p (or at its lot's entry point); true = known
        static bool UfColourA(Vector3 p, Lot lot) { return UfColourAsk(11, p) || (lot != null && !lot.IsWorldLot && UfColourAsk(11, lot.EntryPoint())); }
        // 1 = p (or its lot's entry point) has A's colour, -1 = proven different, 0 = unknown
        static int UfColourB(Vector3 p, Lot lot)
        {
            for (int k = 0; k < 2; k++)
            {
                if (k == 1 && (lot == null || lot.IsWorldLot)) break;
                Vector3 g = k == 0 ? p : lot.EntryPoint();
                if (UfColourAsk(12, g)) return 1;
                if (UfColourAsk(13, g)) return -1;
            }
            return 0;
        }
        static bool UfColourAsk(int code, Vector3 p) { return UfCamAsk(3, p.x) && UfCamAsk(5, p.z) && UfCamAsk(code, 0f); }

        // risk 10c fix: D never runs a sim-thread routability plan to fill a verdict. A cell is proven by the SafeSpot cache
        // (filled by SafeSpot / the watchdog as before) or by D's own list (SpotVerdicts.PlanOk): a normal plan of a D-eligible Sim
        // that succeeded from off the goal lot proves the goal cell joins its lot's edge, and the start cell (on its lot) can leave
        // it [inferred: a path that crosses the lot edge]. PlanOk is read by D only, never by SafeSpot or the watchdog.
        static bool? UfVerdict(Sim sim, Lot lot, RouteProfile prof, Vector3 pos)
        {
            SpotVerdicts v = Verdicts(lot);
            bool? ok = SpotVerdict(sim, lot, v, prof, pos, false, false, false);
            if (ok.HasValue) return ok;
            return v.PlanOk.ContainsKey(KeyOf(prof, pos)) ? (bool?)true : null;
        }
        // a D path skipped for a missing verdict: fill it from this route's own plan (UfPlan, after the plan)
        struct UfFill { public object Route; public Lot GoalLot, StartLot; public Vector3 Goal, Start; public RouteProfile Prof; }
        static UfFill sUfFill;
        static void UfFillNote()
        {
            UfFill f = sUfFill;
            int[] c = PatchCounters();
            for (int k = 0; k < 2; k++)
            {
                Lot lot = k == 0 ? f.GoalLot : f.StartLot;
                if (lot == null || lot.IsWorldLot || (k == 1 && lot == f.GoalLot)) continue;
                SpotVerdicts v = Verdicts(lot);
                if (v.PlanOk.Count >= 256) continue;  // shortcut: a full lot list stops growing until its verdicts expire (a sim-day)
                v.PlanOk[KeyOf(f.Prof, k == 0 ? f.Goal : f.Start)] = true;
                c[327]++;
            }
        }

        // ts3fixes.unseenfb.holding: Sim object id -> hold end (sim ticks), read by our watchdogs and the agent (in_transit)
        static System.Collections.Generic.Dictionary<ulong, long> UfHolds()
        {
            var d = AppDomain.CurrentDomain.GetData("ts3fixes.unseenfb.holding") as System.Collections.Generic.Dictionary<ulong, long>;
            if (d == null) { d = new System.Collections.Generic.Dictionary<ulong, long>(); AppDomain.CurrentDomain.SetData("ts3fixes.unseenfb.holding", d); }
            return d;
        }
        static bool UfInTransit(Sim s) { return UfPreCore.InTransit(sUfHoldsCache ?? (sUfHoldsCache = UfHolds()), s.ObjectId.Value, Sims3.Gameplay.Utilities.SimClock.CurrentTicks); }
        static System.Collections.Generic.Dictionary<ulong, long> sUfHoldsCache;

        // §3.5 (2)-(3) walk-time hold in the Sim's own task (a yielding context: MayYield checked by the caller), one sim-minute at a
        // time; the Sim stands, its interaction runs on and its time passes as for a walk. Every wake-up re-checks the camera rule
        // for the Sim and the goal. false = given up: seen, moved, reset, exit reason or another interaction (the caller plans
        // normally). Never pushes an interaction of its own (§3.7: the queue head stays what the game and mods read).
        static bool UfHold(Sim sim, Vector3 dest, float minutes)
        {
            int[] c = PatchCounters();
            long until = Sims3.Gameplay.Utilities.SimClock.CurrentTicks + Sims3.Gameplay.Utilities.SimClock.ConvertToTicks(minutes, Sims3.Gameplay.Utilities.TimeUnit.Minutes);
            uint step = (uint)Sims3.Gameplay.Utilities.SimClock.ConvertToTicks(1f, Sims3.Gameplay.Utilities.TimeUnit.Minutes);
            Sims3.Gameplay.Interactions.InteractionInstance cur = sim.CurrentInteraction;
            Vector3 at = sim.Position;
            ulong id = sim.ObjectId.Value;
            var holds = UfHolds();
            holds[id] = until;
            c[317]++;
            if ((int)minutes > c[318]) c[318] = (int)minutes;
            int t0 = System.Environment.TickCount;
            string why = "time up";
            UfHoldLog(sim, cur, "start, " + minutes.ToString("0") + " sim-min, live " + GameStates.IsLiveState, false);
            try
            {
                // a tick sleep never wakes while the clock is paused (family picker, Build/Buy), and the game then waits on this
                // interaction forever ("Play" arrow stays gray, v0.2.0 reports): yield per frame, cheap exits every frame
                long nextCheck = Sims3.Gameplay.Utilities.SimClock.CurrentTicks + step;
                while (Sims3.Gameplay.Utilities.SimClock.CurrentTicks < until)
                {
                    Simulator.Sleep(0);
                    why = sim.HasBeenDestroyed || !sim.InWorld ? "gone" : sim.CurrentInteraction != cur || (cur != null && cur.Cancelled) ? "interaction ended"
                        : sim.HasExitReason() ? "exit reason" : !GameStates.IsLiveState ? "not live" : sim.IsSelectable || sim.Household == Household.ActiveHousehold ? "family started" : null;
                    if (why != null) return false;
                    if (Sims3.Gameplay.Utilities.SimClock.CurrentTicks < nextCheck) continue;
                    nextCheck = Sims3.Gameplay.Utilities.SimClock.CurrentTicks + step;
                    if ((sim.Position - at).LengthSqr() > 0.01f || UfWhy(sim) != 0 || !TownieUnseen(sim, null) || !UfGoalUnseen(dest)) { why = "seen or moved"; return false; }
                }
                why = "time up";
                return true;
            }
            finally
            {
                holds.Remove(id);
                // pauses and family starts are what hung the Play step on v0.2.0/v0.2.1: always log them
                UfHoldLog(sim, cur, "end: " + why + " after " + (System.Environment.TickCount - t0) + " ms", why == "not live" || why == "family started");
            }
        }

        // docs/202 §3.6.4 lot trips (unseenfbtrip / unseenfbtriphold): D's rules on top of the trip shortcut's own (townie, lot rules,
        // SafeSpot verdict for `spot`): the Sim's own task in a yielding context, Sim and spot unseen by the camera checks (§2), start
        // and the destination lot not proven in different flood colours (unknown = allowed, as the shortcuts always were), then the
        // walk-time hold (UfHold: in transit for our watchdogs and the harness, camera re-checked every sim-minute) and the same
        // checks again right before the caller places the Sim. false = the caller runs the game's own trip from where the Sim stands.
        static bool UfTripHold(Sim sim, Lot lot, Vector3 spot)
        {
            int[] c = PatchCounters();
            if (Simulator.CurrentTask != sim.ObjectId || !MayYield()) { c[347]++; return false; }
            if (UfWhy(sim) != 0 || !UfGoalUnseen(spot)) { c[345]++; return false; }
            if (UfSameComponent(sim, sim.Position, spot, lot) < 0) { c[346]++; return false; }
            if (!UfHold(sim, spot, UfPreCore.HoldMinutes((spot - sim.Position).Length())) || sim.HasBeenDestroyed || UfWhy(sim) != 0
                || !TownieUnseen(sim, null) || !UfGoalUnseen(spot) || LotManager.GetLotAtPoint(spot) != lot) { c[345]++; return false; }
            c[344]++;
            return true;
        }

        // phase B (StuckCheckRun): true = deferred (re-armed at the same round, nothing planned)
        static bool UfStuckDefer(object helper, Sim sim)
        {
            if (sim == null || !StuckNearReflect(helper.GetType())) return false;
            Lot lot = sim.LotCurrent;
            // the game's own early return and its world-lot branch (plans only for selectable Sims) plan nothing: leave them to it
            if (lot == null || lot.IsWorldLot || lot == Household.ActiveHouseholdLot || sim.Position != (Vector3)sStuckLoc.GetValue(helper) || sim.SimInRabbitHolePosture) return false;
            int[] c = PatchCounters();
            if (UfWhy(sim) != 0) { c[304]++; return false; }
            sStuckAlarm.Invoke(null, new object[] { sim, (uint)sStuckAttempt.GetValue(helper) });
            c[303]++;
            return true;
        }

        public partial class RouteBackoffHook
        {
            // docs/202 §3.1 channel (ts3native unseenfb.cpp UfMsg): GetOption2 with optHi top byte 0x83 (bits 63 + 57 + 56, no
            // RouteOption2 name), optLo 1 = MARK this route's next plan or job, 2 = CUT? (answers once, clears the mark).
            // The game and older ts3native answer false.
            const ulong kUfBits = 0x8300000000000000UL;
            bool UfSend(uint code) { return GetOption2((Sims3.SimIFace.Route.RouteOption2)(kUfBits | code)); }

            RoutePlanResult UfInner(int kind, Vector3 p)
            {
                switch (kind)
                {
                    case 0: return PlanBackoff();
                    case 1: return ReplanAsync();
                    case 2: return PlanFromPointAsync(p);
                    default: return ReplanFromPointAsync(p);
                }
            }

            // kind 0 Plan, 1 Replan, 2 PlanFromPoint, 3 ReplanFromPoint (the A10 kinds)
            // Issue #92 (docs/51 §386): every frame above the original plan counts against Mono's 63-frame limit for whatever the
            // plan calls back into (AboutToPlan / PostPlan callbacks, door asks, mod handlers). UfPlanBody only decides and returns
            // false when the plan is not ours, so the common plan runs from here: UfPlanBody and UfInner are off the stack then.
            RoutePlanResult UfPlan(int kind, Vector3 p)
            {
                int t0 = System.Environment.TickCount;
                sUfLastWhy = -1; sUfLastMarked = sUfLastCut = false; sUfLastSim = null;
                RoutePlanResult r;
                if (!UfPlanBody(kind, p, out r))
                    switch (kind)  // UfInner, inlined
                    {
                        case 0: r = PlanBackoff(); break;
                        case 1: r = ReplanAsync(); break;
                        case 2: r = PlanFromPointAsync(p); break;
                        default: r = ReplanFromPointAsync(p); break;
                    }
                UfSlowPlanLog(kind, unchecked(System.Environment.TickCount - t0));
                UfLoopNote(Follower, r);  // docs/202 §5.7 E (unseenfbloopdet)
                if (sUfFill.Route != null && ReferenceEquals(sUfFill.Route, this))
                {
                    if (r.mType == RoutePlanResultType.Succeeded) UfFillNote();
                    sUfFill = default(UfFill);
                }
                return r;
            }

            // false = not ours: the caller (UfPlan) runs the plan; true = r is the plan's result
            bool UfPlanBody(int kind, Vector3 p, out RoutePlanResult r)
            {
                r = default(RoutePlanResult);
                bool pre = kind == 0 && (ShortcutOn("unseenfbpre") || ShortcutOn("unseenfbarrive"));
                bool leave = kind == 0 && ShortcutOn("unseenfbleave");
                if (!pre && !leave && !ShortcutOn("unseenfbplan")) return false;
                IScriptProxy proxy = Follower;
                Sim sim = proxy != null ? proxy.Target as Sim : null;
                // never: stuck-check probes (phase B), our own route checks, IsPointInLotSafelyRoutable, non-Sims, pets, D-leave's own leg
                if (sim == null || sStuckIn || sInOwnCheck || sInRoutableCheck || sUfInLeg || sim.SimDescription == null || sim.SimDescription.IsPet)
                    return false;
                int[] c = PatchCounters();
                int why = UfWhy(sim);
                sUfLastWhy = why; sUfLastSim = sim;
                if (why != 0)
                {
                    if (leave && UnseenFallbackCore.SeenButMovable(why) && UfLeave(sim, GetApproximateOriginalDestination(), out r)) return true;
                    c[310]++;
                    return false;
                }
                if (!pre && !ShortcutOn("unseenfbplan")) return false;
                ulong id = sim.ObjectId.Value;
                Vector3 dest = GetApproximateOriginalDestination();
                int x = UnseenFallbackCore.Cell(dest.x), y = UnseenFallbackCore.Cell(dest.y), z = UnseenFallbackCore.Cell(dest.z);
                int t0 = System.Environment.TickCount;
                object prev;
                if (sUf.NoRetry(id, x, y, z, t0, out prev)) { c[309]++; sUfNotFail = kind == 0; r = FailFast((RoutePlanResult)prev); return true; }
                if (!sUf.MayMark(id, t0)) return false;
                if (pre)
                {
                    int v = UfPre(sim, dest, out r);
                    if (v == 2) { sUf.Cut(id, x, y, z, System.Environment.TickCount, r); return true; }  // proven unreachable: no-retry window
                    if (v == 1) return true;
                }
                if (!ShortcutOn("unseenfbplan")) return false;
                if (!UfSend(1)) { c[311]++; return false; }
                c[305]++;
                sUfLastMarked = true;
                bool cut = false;
                // docs/202 §3.7: Overwatch's Stuck Check V2 (OnPostPlan, inside the original plan) must not count a cut plan as the
                // Sim's route failure: the same (Sim, task) hide owprobeplans uses for EA's stuck-check probes (OverwatchProbe.cs)
                ulong task = Simulator.CurrentTask.Value;
                sOwProbe.Enter(id, task);
                try { r = UfInner(kind, p); }
                finally { cut = UfSend(2); sOwProbe.Exit(id, task); }
                int ms = unchecked(System.Environment.TickCount - t0);
                if (ms > c[312]) c[312] = ms;
                if (!cut || r.mType == RoutePlanResultType.Succeeded) return true;
                c[306]++;
                sUfLastCut = true;
                UfGuardNote(id, "cut (kind " + kind + ")");
                UfNotAFailure(id, dest);
                if (kind != 0) sUfNotFail = false;  // only Route.Plan (PlanHook) reads it
                RoutePlanResult tr;
                if (kind == 0 && ShortcutOn("unseenfbteleport") && Simulator.CurrentTask == sim.ObjectId && UfTeleport(sim, dest, out tr)) { r = tr; return true; }
                sUf.Cut(id, x, y, z, System.Environment.TickCount, r);
                return true;
            }

            // docs/202 §3.5 D (switch unseenfbpre): an unseen townie's own Route.Plan, made by a RoutingComponent helper that follows
            // the route right after, to an unseen outdoor ground-level goal on another lot (community lot or its own home, no door
            // with a lock) plans nothing. Proofs first: the game's flood colours (ts3native) put the start and the goal in one
            // world component, and the cached SafeSpot verdicts (the game's IsPointInLotSafelyRoutable for this route profile)
            // say the goal, and a start on a lot, connect to their lot's edge. Then the Sim stands where it is for the walk time
            // (UfHold, in its own task, listed as in transit), is placed at the goal and plans the last metres from there: the
            // caller follows that short route as usual. 0 = not ours (plan normally), 1 = done (r = the short plan), 2 = proven
            // unreachable on land (r = no path, nothing searched; the caller's own failure branch runs).
            int UfPre(Sim sim, Vector3 dest, out RoutePlanResult r)
            {
                r = default(RoutePlanResult);
                int[] c = PatchCounters();
                // reject gates count 328-339 (UfRj) so a run shows which rule keeps D from acting
                if (dest == Vector3.Origin || dest == Vector3.Invalid || Simulator.CurrentTask != sim.ObjectId) return UfRj(328);
                Vector3 start = sim.Position;
                float dist = (dest - start).Length();
                if (dist < UfPreCore.kMinDistM) return UfRj(329);
                if (!UfPreSim(sim)) return UfRj(330);
                if (!TownieUnseen(sim, null)) return UfRj(331);
                if (UfCarOrBoat()) return UfRj(332);
                // goal in view: D-arrive skips only the hidden leg (§3.6)
                if (!UfGoalUnseen(dest)) return ShortcutOn("unseenfbarrive") ? UfArrive(sim, start, dest, dist, out r) : 0;
                if (!ShortcutOn("unseenfbpre")) return 0;
                Lot goalLot = LotManager.GetLotAtPoint(dest);
                if (goalLot == sim.LotCurrent) return UfRj(333);
                if (!UfPreLot(sim, goalLot)) return UfRj(334);
                if (!UfOutdoorGround(dest)) return UfRj(335);
                if (!UfFollows()) return UfRj(336);
                if (!MayYield()) return UfRj(337);
                int same = UfSameComponent(sim, start, dest, goalLot);
                if (same < 0 && !GetOption(RouteOption.EnableWaterPlanning))
                {
                    c[315]++;
                    UfNotAFailure(sim.ObjectId.Value, dest);
                    r = FailFast(RoutePlanResultType.FailedBecauseNoPathToDestination);
                    return 2;
                }
                RouteProfile prof = ProfileOf(sim);
                Lot startLot = sim.LotCurrent;
                if (same <= 0) { c[314]++; return UfRj(338); }
                if (!prof.Ok || !IsPointRoutable(dest)) { c[314]++; return UfRj(339); }
                bool? gv = UfVerdict(sim, goalLot, prof, dest);
                bool? sv = startLot != null && !startLot.IsWorldLot ? UfVerdict(sim, startLot, prof, start) : true;
                if (gv == false || sv == false) { c[314]++; return 0; }
                if (!gv.HasValue || !sv.HasValue)
                {
                    // no verdict: no sim-thread routability plan; the normal plan runs and its success fills D's list
                    c[326]++;
                    sUfFill = new UfFill { Route = this, GoalLot = gv.HasValue ? null : goalLot, StartLot = sv.HasValue ? null : startLot, Goal = dest, Start = start, Prof = prof };
                    return 0;
                }
                float minutes = UfPreCore.HoldMinutes(dist);
                // after the hold, right before the move: the Sim and the goal still unseen (same tick), the goal still free ground
                if (!UfHold(sim, dest, minutes) || sim.HasBeenDestroyed || UfWhy(sim) != 0 || !TownieUnseen(sim, null) || !UfGoalUnseen(dest) || !IsPointRoutable(dest))
                {
                    c[316]++;
                    return 0;  // plans normally from where it stands: it lost only the hold, which the walk would have used too
                }
                Vector3 fwd = dest - start;
                fwd.y = 0f;
                fwd = fwd.LengthSqr() > 0.01f ? fwd / fwd.Length() : sim.ForwardVector;
                PlaceSim(sim, dest, fwd, "unseenfbpre route goal");
                c[313]++;
                ShortcutLog(sim.FullName + " => route goal (unseenfbpre, " + (int)dist + " m, held " + (int)minutes + " sim-min)");
                r = PlanBackoff();  // the last metres (slot facing, radial range) from the goal: a short plan the caller follows
                return 1;
            }

            bool UfCarOrBoat()
            {
                return GetOption(RouteOption.EnablePlanningAsCar) || GetOption(RouteOption.BeginAsCar) || GetOption2(RouteOption2.EnablePlanningAsBoat)
                    || GetOption2(RouteOption2.BeginAsBoat);
            }

            // docs/202 §3.6 hand-off point H between `from` and `to` (dist apart): 10 m steps from `from`, the first point that is
            // out of view with its lot (UfGoalUnseen), on the street (world lot: never inside a lot), outdoors on the ground,
            // routable for this route and in colour A (set by the caller). shortcut: straight-line candidates only, no R-node snap
            // or lot entry points; upgrade if unseenfbarrive/leave_no_spot stays high in game.
            bool UfHandOff(Vector3 from, Vector3 to, float dist, out Vector3 h)
            {
                for (int i = 0; i < UfPreCore.HandOffSteps(dist); i++)
                {
                    float s = UfPreCore.HandOffAt(i);
                    h = from + (to - from) * (s / dist);
                    h.y = World.GetTerrainHeight(h.x, h.z);
                    Lot hl = LotManager.GetLotAtPoint(h);
                    if ((hl == null || hl.IsWorldLot) && UfGoalUnseen(h) && UfOutdoorGround(h) && IsPointRoutable(h) && UfColourB(h, null) == 1) return true;
                }
                h = Vector3.Invalid;
                return false;
            }

            // §3.6 D-arrive (switch unseenfbarrive): the Sim is unseen, the goal is in view. Hold for the walk to H (the first unseen
            // street point from the goal towards the Sim), place the Sim at H facing the goal, then the real plan H -> goal: the
            // player sees it walk in. Proofs for the skipped leg: same colour start / H / goal, and a start on a lot has a verdict.
            int UfArrive(Sim sim, Vector3 start, Vector3 dest, float dist, out RoutePlanResult r)
            {
                r = default(RoutePlanResult);
                int[] c = PatchCounters();
                if (dist < UfPreCore.kSplitMinDistM) return UfRj(329);
                if (!UfFollows()) return UfRj(336);
                if (!MayYield()) return UfRj(337);
                RouteProfile prof = ProfileOf(sim);
                Lot sl = sim.LotCurrent;
                if (!prof.Ok) return UfRj(339);
                if (sl != null && !sl.IsWorldLot)
                {
                    bool? sv = UfVerdict(sim, sl, prof, start);
                    if (sv == false) { c[320]++; return 0; }
                    if (!sv.HasValue) { c[326]++; sUfFill = new UfFill { Route = this, StartLot = sl, Start = start, Prof = prof }; return 0; }
                }
                Vector3 h;
                if (!UfColourA(start, sl) || UfColourB(dest, LotManager.GetLotAtPoint(dest)) != 1) { c[320]++; return UfRj(338); }
                if (!UfHandOff(dest, start, dist, out h)) { c[320]++; return 0; }
                float minutes = UfPreCore.HoldMinutes((h - start).Length());
                if (!UfHold(sim, h, minutes) || sim.HasBeenDestroyed || UfWhy(sim) != 0 || !TownieUnseen(sim, null) || !UfGoalUnseen(h) || !IsPointRoutable(h))
                {
                    c[316]++;
                    return 0;
                }
                Vector3 fwd = dest - h;
                fwd.y = 0f;
                fwd = fwd.LengthSqr() > 0.01f ? fwd / fwd.Length() : sim.ForwardVector;
                PlaceSim(sim, h, fwd, "unseenfbarrive hand-off");
                c[319]++;
                ShortcutLog(sim.FullName + " => hand-off (unseenfbarrive, " + (int)(h - start).Length() + " m skipped, held " + (int)minutes + " sim-min)");
                r = PlanBackoff();  // the visible last leg H -> goal, planned and walked as usual
                return 1;
            }

            // §3.6 D-leave (switch unseenfbleave): the Sim starts in view (not the household, not held), the goal qualifies for D
            // (unseen, community lot or own home without locks, outdoor ground, verdict known). The Sim walks a normal short leg to
            // H (the first unseen street point towards the goal; its own route, planned and followed here in its own task), then,
            // still unseen at H, is held for the rest of the walk and placed at the goal; the real plan from the goal is returned.
            // false = plan normally from where the Sim stands (at the start, or at H after the visible leg: nothing visible lost).
            bool UfLeave(Sim sim, Vector3 dest, out RoutePlanResult r)
            {
                r = default(RoutePlanResult);
                int[] c = PatchCounters();
                if (dest == Vector3.Origin || dest == Vector3.Invalid || Simulator.CurrentTask != sim.ObjectId) return UfNo(328);
                if (sim.Posture != sim.Standing) return UfNo(331);
                if (!UfPreSim(sim)) return UfNo(330);
                if (UfCarOrBoat()) return UfNo(332);
                Vector3 start = sim.Position;
                float dist = (dest - start).Length();
                Lot goalLot = LotManager.GetLotAtPoint(dest);
                if (dist < UfPreCore.kSplitMinDistM) return UfNo(329);
                if (goalLot == sim.LotCurrent) return UfNo(333);
                if (!UfPreLot(sim, goalLot)) return UfNo(334);
                if (!UfGoalUnseen(dest)) return UfNo(340);
                if (!UfOutdoorGround(dest)) return UfNo(335);
                if (!UfFollows()) return UfNo(336);
                if (!MayYield()) return UfNo(337);
                RouteProfile prof = ProfileOf(sim);
                if (!IsPointRoutable(dest) || !prof.Ok) return UfNo(339);
                bool? gv = UfVerdict(sim, goalLot, prof, dest);
                if (gv == false) { c[322]++; return false; }
                if (!gv.HasValue) { c[326]++; sUfFill = new UfFill { Route = this, GoalLot = goalLot, Goal = dest, Prof = prof }; return false; }
                Vector3 h;
                if (!UfColourA(start, sim.LotCurrent) || UfColourB(dest, goalLot) != 1) { c[322]++; return UfNo(338); }
                if (!UfHandOff(start, dest, dist, out h)) { c[322]++; return false; }
                bool walked = false;
                sUfInLeg = true;
                try
                {
                    Route leg = sim.CreateRoute();
                    leg.PlanToPoint(h);
                    walked = leg.PlanResult.Succeeded() && sim.DoRoute(leg) && (sim.Position - h).Length() < 2f;
                }
                finally { sUfInLeg = false; }
                if (!walked || sim.HasBeenDestroyed || UfWhy(sim) != 0 || !TownieUnseen(sim, null) || !UfGoalUnseen(dest)) { c[316]++; return false; }
                float minutes = UfPreCore.HoldMinutes((dest - sim.Position).Length());
                if (!UfHold(sim, dest, minutes) || sim.HasBeenDestroyed || UfWhy(sim) != 0 || !TownieUnseen(sim, null) || !UfGoalUnseen(dest) || !IsPointRoutable(dest))
                {
                    c[316]++;
                    return false;
                }
                Vector3 fwd = dest - h;
                fwd.y = 0f;
                fwd = fwd.LengthSqr() > 0.01f ? fwd / fwd.Length() : sim.ForwardVector;
                PlaceSim(sim, dest, fwd, "unseenfbleave route goal");
                c[321]++;
                ShortcutLog(sim.FullName + " => route goal (unseenfbleave, " + (int)(dest - h).Length() + " m skipped, held " + (int)minutes + " sim-min)");
                r = PlanBackoff();
                return true;
            }

            // §3.2 item 1: own-task Route.Plan only (the caller follows this route next)
            bool UfTeleport(Sim sim, Vector3 dest, out RoutePlanResult r)
            {
                r = default(RoutePlanResult);
                int[] c = PatchCounters();
                if (dest == Vector3.Origin || dest == Vector3.Invalid || (sim.Position - dest).Length() < kBackoffMinDistance
                    || UfWhy(sim) != 0 || !TownieUnseen(sim, null) || !UfGoalUnseen(dest) || !IsPointRoutable(dest))  // checked again here, right before the move
                {
                    c[308]++;
                    return false;
                }
                PlaceSim(sim, dest, sim.ForwardVector, "unseenfb route goal");
                c[307]++;
                ShortcutLog(sim.FullName + " => route goal (unseenfb, cut plan)");
                bool marked = UfSend(1);
                try { r = PlanBackoff(); }
                finally { if (marked) UfSend(2); }
                UfGuardNote(sim.ObjectId.Value, "teleport, next plan " + r.mType);  // §5.7: the post-teleport plan result
                return true;
            }
        }
    }
}
