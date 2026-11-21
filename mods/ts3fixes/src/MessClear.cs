using System;
using System.Collections.Generic;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Controllers;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // messclearunreach (tuning kMessClearUnreach, default off; docs/277 §10). EA bug fix, every Sim.
    //
    // burnttilefloor leaves an unreachable burnt tile out of the list, so the tile stays burnt and a later load or a memo
    // expiry brings it back. This switch cleans it instead, with EA's own code: Sim.MopFloor.Run (MopBurntTiles and
    // MopPuddles) already clears every listed tile it cannot mop (IsValidPuddleLocation false: no solid floor, an object on
    // it, a diagonal wall) with PuddleManager.RemovePuddle + World.SetBurnt(false) and no route [verified, decompiled]. Before
    // EA's Run, the hook gives the same two calls to the tiles we know are unreachable: the burnttilefloor level rule
    // (BtStaticCut: a valid tile on level 2+ with no level changer near it) and the tiles EA's planner already failed (the
    // design-2 memo, learned with either switch on, for autonomous MopBurnt and MopPuddle). EA's Run then mops the rest as
    // before, so a reachable tile is still mopped normally. MopFloor is an ImmediateInteraction (no animation in its Run), so
    // the clear plays none, as EA's own floorless clear. The state written is EA's normal clean state, exactly what mopping
    // writes (OnTileMopped: SetBurnt false / RemovePuddle): no mod data in the save. While this switch is on, burnttilefloor's
    // list cut is off, so MopFloor's Test still offers the mop and the clear can run. Service Sims run EA's Run unchanged
    // (EA's IsLocationMoppableBySim rule for them).
    // Counters (registry fixes-1547): [1547] burnt tiles cleared, [1548] puddles cleared, [1549] errors.
    // Check: MessClearCheck (below), repro messclear-live.
    public static partial class Fixes
    {
        const int kCMcBurnt = 1547, kCMcPuddle = 1548, kCMcErr = 1549;
        static readonly List<int> sMcCleared = new List<int>();  // keys of the last 64 tiles cleared (check only), sim thread
        static ulong sMcLot;
        static LotLocation sMcCtrl = LotLocation.Invalid, sMcPuddle = LotLocation.Invalid;  // tiles the check staged

        static void ApplyMessClearUnreach()
        {
            Patch(typeof(Sim.MopFloor), "Run", Type.EmptyTypes, typeof(MessClearHook), "RunHook", "RunOrig");
        }

        // Pure: clean this listed tile before EA's Run? Only for a non-service actor, and only a tile known unreachable.
        internal static bool McClear(bool on, bool service, bool levelCut, bool learned) { return on && !service && (levelCut || learned); }

        static void McRun(Sim actor, bool burnt)
        {
            int[] c = PatchCounters();
            try
            {
                if (actor == null || actor.SimDescription == null) return;
                Lot lot = actor.LotCurrent;
                if (lot == null || lot.IsWorldLot) return;
                ulong id = lot.LotId;
                LotLocation[] tiles = burnt ? World.GetBurntTiles(id, LotLocation.Invalid) : World.GetPuddles(id, LotLocation.Invalid);
                if (tiles == null || tiles.Length == 0) return;
                bool memo = sBtUnreach.ContainsKey(id), service = actor.SimDescription.IsServicePerson;
                foreach (LotLocation l in tiles)
                {
                    if (!McClear(true, service, BtStaticCut(id, l), memo && BtLearned(id, l))) continue;
                    PuddleManager.RemovePuddle(id, l);  // EA's clear for a tile it cannot mop (MopFloor.Run)
                    World.SetBurnt(id, l, false);
                    c[burnt ? kCMcBurnt : kCMcPuddle]++;
                    if (sMcCleared.Count >= 64) sMcCleared.RemoveAt(0);
                    sMcCleared.Add(BtKey(l));
                }
            }
            catch (Exception) { c[kCMcErr]++; }
        }

        public class MessClearHook : Sim.MopFloor
        {
            protected override InteractionDefinition DefinitionToUse { get { return null; } }

            public bool RunHook()
            {
                if (ShortcutOn("messclearunreach") && !OnWorker())
                {
                    object self = this;  // the real instance: Sim+MopBurntTiles or Sim+MopPuddles
                    McRun(Actor, self is Sim.MopBurntTiles);
                }
                return RunOrig();
            }
            public bool RunOrig() { return false; } // receives Sim.MopFloor.Run's original body
        }

        // Harness check (test only, agent reflect_call), on the active Sim's lot:
        //   stage    burns one unburnt floor tile next to the active Sim (reachable control) and puts a puddle on the first
        //            burnt tile the level rule flags (unreachable puddle); both stay until mopped, cleared or 'unstage'
        //   push     queues EA's MopBurntTiles and MopPuddles (autonomous, as EA pushes them) on the household humans there
        //   run      verdict: off, flagged tiles still burnt = BROKEN; on, none left, the control mopped by the Sim (not by
        //            our clear) and the staged puddle cleared = FIXED
        //   unstage  clears the control tile and the staged puddle if still there
        //   result   counters 1547-1549
        public static object MessClearCheck(string what)
        {
            int[] c = PatchCounters();
            string counters = "messclearunreach: burnt cleared 1547=" + c[kCMcBurnt] + ", puddles cleared 1548=" + c[kCMcPuddle] + ", errors 1549=" + c[kCMcErr];
            if (what == "result") return counters;
            bool pure = McClear(true, false, true, false) && McClear(true, false, false, true) && !McClear(true, false, false, false)
                && !McClear(false, false, true, true) && !McClear(true, true, true, true);
            Sim sim = Sim.ActiveActor;
            Lot lot = sim != null ? sim.LotCurrent : null;
            if (lot == null || lot.IsWorldLot) return "messclear check: no active Sim on a lot (N/A); pure=" + pure;
            ulong id = lot.LotId;
            bool on = ShortcutOn("messclearunreach");
            if (what == "stage")
            {
                sMcLot = id;
                sMcCtrl = McFreeTileNear(sim, id);
                if (McSet(sMcCtrl)) World.SetBurnt(id, sMcCtrl, true);
                sMcPuddle = LotLocation.Invalid;
                foreach (LotLocation l in World.GetBurntTiles(id, LotLocation.Invalid))
                {
                    if (!BtStaticCut(id, l)) continue;
                    Vector3 p = Vector3.Invalid;
                    World.GetWorldPosition(id, l, ref p);
                    PuddleManager.AddPuddle(p);
                    if (BtHas(World.GetPuddles(id, LotLocation.Invalid), l)) { sMcPuddle = l; break; }
                }
                return "messclear stage: control " + McTile(id, sMcCtrl) + ", puddle on a flagged tile " + McTile(id, sMcPuddle) + "; " + McState(id) + "; pure=" + pure;
            }
            if (what == "push")
            {
                int n = 0;
                foreach (Sim s in lot.GetSims())
                {
                    if (s == null || s.IsPet || s.Household != sim.Household) continue;
                    InteractionPriority pr = new InteractionPriority(InteractionPriorityLevel.Autonomous);
                    InteractionInstance a = Sim.MopBurntTiles.Singleton.CreateInstance(s, s, pr, true, true), b = Sim.MopPuddles.Singleton.CreateInstance(s, s, pr, true, true);
                    if (a != null && s.InteractionQueue.Add(a)) n++;
                    if (b != null && s.InteractionQueue.Add(b)) n++;
                }
                return "messclear push: " + n + " mop interactions queued (switch " + (on ? "on" : "off") + ")";
            }
            if (what == "unstage")
            {
                if (sMcLot != 0 && BtHas(World.GetBurntTiles(sMcLot, LotLocation.Invalid), sMcCtrl)) World.SetBurnt(sMcLot, sMcCtrl, false);
                if (sMcLot != 0 && BtHas(World.GetPuddles(sMcLot, LotLocation.Invalid), sMcPuddle)) PuddleManager.RemovePuddle(sMcLot, sMcPuddle);
                sMcLot = 0;
                return "messclear unstage: " + McState(id);
            }
            if (what != "run") return "messclear <stage|push|run|unstage|result>";
            int flagged = 0;
            foreach (LotLocation l in World.GetBurntTiles(id, LotLocation.Invalid)) if (BtStaticCut(id, l) || BtLearned(id, l)) flagged++;
            bool staged = sMcLot == id;
            bool ctrlBurnt = staged && BtHas(World.GetBurntTiles(id, LotLocation.Invalid), sMcCtrl), ctrlByUs = staged && sMcCleared.Contains(BtKey(sMcCtrl));
            bool puddleLeft = staged && BtHas(World.GetPuddles(id, LotLocation.Invalid), sMcPuddle);
            bool hasPuddle = staged && McSet(sMcPuddle), hasCtrl = staged && McSet(sMcCtrl);
            string verdict;
            if (!pure || (on && CkLive("messclearunreach") != "")) verdict = "UNEXPECTED (pure / swap)";
            else if (!on) verdict = flagged > 0 || (hasPuddle && puddleLeft) ? "BROKEN" : "N/A (nothing flagged unreachable)";
            else if (flagged > 0 || (hasPuddle && puddleLeft) || ctrlByUs) verdict = "UNEXPECTED";
            else if (hasCtrl && ctrlBurnt) verdict = "UNEXPECTED (control not mopped yet)";
            else verdict = "FIXED";
            return "messclear check (switch " + (on ? "on" : "off") + ", burnttilefloor " + (ShortcutOn("burnttilefloor") ? "on" : "off") + "): flagged burnt tiles left " + flagged
                + ", control " + McTile(id, sMcCtrl) + (hasCtrl ? (ctrlBurnt ? " still burnt" : ctrlByUs ? " cleared by the fix (wrong)" : " mopped") : "")
                + ", staged puddle " + (hasPuddle ? (puddleLeft ? "still there" : "gone") : "none") + "; " + McState(id) + "; pure=" + pure + "; verdict: " + verdict + "; " + counters;
        }

        static LotLocation McFreeTileNear(Sim sim, ulong id)
        {
            LotLocation at = LotLocation.Invalid;
            if (World.GetLotLocationAtFloor(sim.Position, ref at) != id) return LotLocation.Invalid;
            LotLocation[] burnt = World.GetBurntTiles(id, LotLocation.Invalid);
            int[] dx = { 1, -1, 0, 0, 1, -1, 1, -1 }, dz = { 0, 0, 1, -1, 1, 1, -1, -1 };
            for (int i = 0; i < dx.Length; i++)
            {
                LotLocation n = at;
                n.mX = (short)(at.mX + dx[i]);
                n.mZ = (short)(at.mZ + dz[i]);
                if (World.HasSolidFloor(id, n) && !BtHas(burnt, n) && PuddleManager.IsValidPuddleLocation(id, n)) return n;
            }
            return LotLocation.Invalid;
        }

        static bool McSet(LotLocation l) { return l.mX != LotLocation.Invalid.mX || l.mZ != LotLocation.Invalid.mZ; }

        static string McTile(ulong id, LotLocation l) { return McSet(l) ? l.mX + "," + l.mZ + " L" + l.mLevel : "none"; }

        static string McState(ulong id)
        {
            LotLocation[] b = World.GetBurntTiles(id, LotLocation.Invalid), p = World.GetPuddles(id, LotLocation.Invalid);
            return "lot " + id.ToString("X") + " burnt " + (b == null ? 0 : b.Length) + ", puddles " + (p == null ? 0 : p.Length);
        }
    }
}
