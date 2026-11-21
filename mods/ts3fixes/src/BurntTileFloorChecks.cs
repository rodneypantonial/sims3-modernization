using System;
using System.Collections.Generic;
using System.Text;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Controllers;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interfaces;
using Sims3.Gameplay.ObjectComponents;
using Sims3.SimIFace;

namespace Ts3Fixes
{
    // Harness check for BurntTileFloor.cs design 3 (docs/277 §9), repro burnttilefloor-live. Calls the hook body directly
    // (no MethodInfo.Invoke on the swapped GetMoppableLocations, docs/312) on the active Sim's lot. The control burns one
    // floor tile next to the Sim for the length of the call and clears it again; nothing else is written.
    public static partial class Fixes
    {
        // BurntTileFloorCheck run: EA's tile list (the LocOrig stub = EA's body), the hooked list, and MopFloor Test's offer
        // answer (any tile PuddleManager.CanMopPuddle passes) for both. off: EA offers mopping only for tiles the level rule
        // cuts = BROKEN; on: the hooked offer is false = FIXED. Control: a burnt tile next to the Sim stays listed (and offered
        // when CanMopPuddle passes it) in both arms. Pure cases of BtCut / BtLinked come first. The hook calls add to the
        // counters 1136/1137/1545/1546 like any MopBurntTiles test.
        public static object BurntTileFloorCheck(string what)
        {
            int[] c = PatchCounters();
            string counters = "burnttilefloor level rule: tiles cut 1545=" + c[kCBtNoLink] + ", lists it alone emptied 1546=" + c[kCBtStaticEmpty]
                + "; " + BurntTileCheck("result");
            if (what == "result") return counters;
            if (what != "run") return "burnttilefloor <run|result>";
            bool pure = BtCut(2, true, false) && BtCut(5, true, false) && !BtCut(2, false, false) && !BtCut(2, true, true) && !BtCut(1, true, false)
                && !BtCut(0, true, false) && !BtCut(-1, true, false) && BtLinked(BtBit(1), 2) && BtLinked(BtBit(2), 2) && BtLinked(BtBit(3), 2)
                && !BtLinked(BtBit(0), 2) && !BtLinked(BtBit(4), 2) && !BtLinked(0UL, 2) && BtLinked(~0UL, 2) && BtBit(60) == 0 && BtBit(-9) == 0;
            Sim sim = Sim.ActiveActor;
            Lot lot = sim != null ? sim.LotCurrent : null;
            if (lot == null || lot.IsWorldLot) return "burnttilefloor check: no active Sim on a lot (N/A); pure=" + pure;
            ulong id = lot.LotId;
            string live = CkLive("burnttilefloor");  // "" = the swap is in place
            BurntTileHook h = new BurntTileHook();
            LotLocation[] ea = h.LocOrig(id);  // EA's body through the stub (null when the swap is not in)
            if (ea == null) return "burnttilefloor check: GetMoppableLocations not hooked (N/A); pure=" + pure + ", swap " + live;
            if (ea.Length == 0) return "burnttilefloor check: no burnt tiles on lot " + id.ToString("X") + " (N/A); pure=" + pure;
            bool on = ShortcutOn("burnttilefloor");
            sBtLinks.Remove(id);  // census as the lot is now
            ulong links = BtLinkMask(id);
            List<LotLocation> kept = new List<LotLocation>();
            int noLink = 0;
            foreach (LotLocation l in ea) if (BtStaticCut(id, l)) noLink++; else kept.Add(l);
            bool eaOffer = BtOffer(id, ea), cutOffer = BtOffer(id, kept.ToArray()), hookOffer = BtOffer(id, h.LocHook(id));
            string ctrl = BtControl(sim, id, h, ea);
            bool ctrlOk = ctrl.StartsWith("ok", StringComparison.Ordinal);
            string verdict;
            if (!ctrlOk && !ctrl.StartsWith("N/A", StringComparison.Ordinal)) verdict = "UNEXPECTED (control)";
            else if (!eaOffer || cutOffer) verdict = "N/A (EA offers no mopping here, or a tile the rule keeps is offered)";
            else if (live != "" || !pure) verdict = "UNEXPECTED";
            else verdict = on ? (hookOffer ? "UNEXPECTED" : "FIXED") : (hookOffer ? "BROKEN" : "UNEXPECTED");
            return "burnttilefloor check (switch " + (on ? "on" : "off") + ") " + sim.FullName + " level " + sim.Level + ", lot " + id.ToString("X")
                + ": burnt tiles " + ea.Length + ", level rule cuts " + noLink + "; level changers on levels "
                + BtLevels(links) + " " + BtLinkNames(lot) + "; offered EA=" + eaOffer + " after the rule=" + cutOffer + " hooked=" + hookOffer
                + "; control: " + ctrl + "; pure=" + pure + ", swap " + (live == "" ? "live" : live) + "; verdict: " + verdict + "; " + counters;
        }

        // MopFloor Test's loop over the list (IsLocationMoppableBySim is true for a non-service Sim; mopunreachable's memo on it is left out)
        static bool BtOffer(ulong lotId, LotLocation[] tiles)
        {
            if (tiles == null) return false;
            GameObject blocker;
            foreach (LotLocation l in tiles) if (PuddleManager.CanMopPuddle(lotId, l, out blocker, false)) return true;
            return false;
        }

        // Burns a floor tile next to the Sim on its level (not burnt yet), reads the hooked list and the offer, then clears it.
        static string BtControl(Sim sim, ulong id, BurntTileHook h, LotLocation[] ea)
        {
            LotLocation at = LotLocation.Invalid;
            if (World.GetLotLocationAtFloor(sim.Position, ref at) != id) return "N/A (Sim not on its lot's floor grid)";
            int[] dx = { 1, -1, 0, 0, 1, -1, 1, -1 }, dz = { 0, 0, 1, -1, 1, 1, -1, -1 };
            for (int i = 0; i < dx.Length; i++)
            {
                LotLocation n = at;
                n.mX = (short)(at.mX + dx[i]);
                n.mZ = (short)(at.mZ + dz[i]);
                if (!World.HasSolidFloor(id, n) || BtHas(ea, n)) continue;
                World.SetBurnt(id, n, true);
                try
                {
                    LotLocation[] after = h.LocOrig(id), hooked = h.LocHook(id);
                    if (!BtHas(after, n)) continue;  // the game did not take it (no burnable floor there)
                    bool keptIt = BtHas(hooked, n), offer = BtOffer(id, new LotLocation[] { n }), hookOffer = BtOffer(id, hooked);
                    string tile = "tile " + n.mX + "," + n.mZ + " L" + n.mLevel;
                    if (!keptIt) return "CUT " + tile + " (a reachable tile must stay)";
                    if (offer && !hookOffer) return "NOT OFFERED " + tile;
                    return "ok, " + tile + " kept" + (offer ? ", offered" : ", not moppable now (CanMopPuddle), kept only");
                }
                finally { World.SetBurnt(id, n, false); }
            }
            return "N/A (no unburnt floor tile next to the Sim)";
        }

        static bool BtHas(LotLocation[] tiles, LotLocation l)
        {
            if (tiles != null) foreach (LotLocation t in tiles) if (t.mX == l.mX && t.mZ == l.mZ && t.mLevel == l.mLevel) return true;
            return false;
        }

        static string BtLevels(ulong mask)
        {
            if (mask == ~0UL) return "all (unknown)";
            StringBuilder b = new StringBuilder();
            for (int l = -8; l < 56; l++) if ((mask & BtBit(l)) != 0) b.Append(b.Length > 0 ? "," : "").Append(l);
            return b.Length > 0 ? b.ToString() : "none";
        }

        static string BtLinkNames(Lot lot)
        {
            StringBuilder b = new StringBuilder("(");
            int n = 0;
            foreach (GameObject o in lot.GetObjects<GameObject>())
            {
                if (o == null || o is Door || !(o is IHasPortalComponent || o is IPortalConnectionObject || o is IElevatorObject)) continue;
                if (n++ < 6) b.Append(n > 1 ? ", " : "").Append(o.GetType().Name).Append(" L").Append(o.Level);
            }
            return b.Append(n > 6 ? ", +" + (n - 6) : "").Append(')').ToString();
        }
    }
}
