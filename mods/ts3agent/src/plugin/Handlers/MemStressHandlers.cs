using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;
using Sims3.SimIFace.BuildBuy;
using Sims3.SimIFace.CustomContent;
using Sims3.UI;
using Sims3.UI.GameEntry;

namespace Ts3Agent
{
    // docs/200 (CC stress in Build/Buy and Edit Town): loops that fill a mode with content through the game's own tools, empty
    // it again, and bracket every cycle with the same marks as the live saturation repro (live-full / live-empty): here
    // <prefix>-full after the fill and <prefix>-empty after the removal, each followed by a forced-GC pair (gc-before /
    // gc-after), so tools/mem-report.ts sameTagDeltas shows growth that survives removal per cycle (same tag every cycle).
    // STRESS (labelled, not a player flow): the counts and the loop are the agent's; each single step is the player's path.
    public static partial class Handlers
    {
        // Ticks(n > 0) waits for sim ticks, and those stop while Build/Buy/Edit Town pause the clock: the task never
        // wakes there (bp-cc-buildbuy 2026-10-25: world frozen in Buy mode 10 min, no bb mark) [inferred]. Wait about n frames
        // of real time instead (n x 33 ms), yielding one frame per Sleep(0) like the agent's own command loop.
        static void Ticks(uint n)
        {
            int t0 = System.Environment.TickCount;
            do Simulator.Sleep(0); while (System.Environment.TickCount - t0 < (int)n * 33);
        }

        // Wait in a game task until ready() or about maxTicks frames (Ticks) passed.
        static bool WaitReady(Ready ready, int maxTicks)
        {
            for (int t = 0; t < maxTicks; t += 10)
            {
                if (ready()) return true;
                Ticks(10);
            }
            return ready();
        }

        static void GcRounds(int rounds)
        {
            for (int i = 0; i < rounds; i++)
            {
                GC.Collect();
                Ticks(2);  // finalizers drain in MonoScriptHost_Simulate (see MemGc)
            }
        }

        // the cycle marks: <tag>, then gc-before / 2 forced collections / gc-after (as mem_gc)
        static void MarkGc(string tag)
        {
            bool sus;
            MemMarkTag(tag, out sus);
            MemMarkTag("gc-before", out sus);
            GcRounds(2);
            MemMarkTag("gc-after", out sus);
        }

        static void Click(double[] px, uint frames)
        {
            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseMove, (float)px[0], (float)px[1], 0u, 0u);
            Ticks(frames);
            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseDown, (float)px[0], (float)px[1], 1000u, 0u);
            Ticks(frames);
            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseUp, (float)px[0], (float)px[1], 1000u, 0u);
            Ticks(frames);
        }

        static bool BuildUp() { return SubState() == "BuildModeState" && BuildController.Active; }
        static bool LiveUp() { return SubState() == "LiveModeState"; }

        // bb_cc_cycle {name?: "CCGen", objects?: 20, cycles?: 4, skip?: 0, mode?: "buy" | "build", paint_presets?: 0,
        // dwell_ticks?: 10, prefix?: "bb"} | {result: true}: live mode, active household's home lot. Per cycle: enter Buy (or
        // Build) mode on the home lot (GameStates.TransitionToBuyMode / ToBuildMode, what the buttons call), camera over the
        // lot centre; per object: the catalogue click (BuyModel.SelectBuyProduct = UserTool 4 with the product, Build mode:
        // UserToolGeneric 4) and one click at a free spot (FindGoodLocationNearby of an out-of-world probe of the same product,
        // destroyed again), placement seen by World.OnObjectPlacedInLot; mark <prefix>-full + GC; per placed object the sell
        // tool (UserToolGeneric 12, BuyController.OnSellTool) clicked on it; objects the click left behind are destroyed and
        // counted (fallback_deletes); exit to live mode; mark <prefix>-empty + GC. Products: distinct catalogue matches of
        // name, cycle c uses matches skip + c*objects.. (wraps). paint_presets k (Build mode only): first a 4x4 floor
        // rectangle round the lot centre tile painted with floor preset (c-1) mod k (tool 2 drag), the same tiles every cycle
        // (bounded world state, a new pattern each cycle).
        static Dictionary<string, object> sBbResult;
        static object BbCcCycle(Args a)
        {
            if (a.Has("result"))
            {
                if (sBbResult == null) throw new McpException("no bb_cc_cycle started");
                return sBbResult;
            }
            if (sBbResult != null && !(bool)sBbResult["done"]) throw new McpException("bb_cc_cycle still running");
            RequireLive();
            Lot lot = HomeLot();
            if (lot == null) throw new McpException("no home lot for the active household");
            string want = a.Str("name", "CCGen").ToLowerInvariant(), prefix = a.Str("prefix", "bb");
            bool build = a.Str("mode", "buy") == "build";
            int per = Math.Max(1, (int)a.Long("objects", 20)), cycles = Math.Max(1, (int)a.Long("cycles", 4)), skip = (int)a.Long("skip", 0);
            int paint = build ? (int)a.Long("paint_presets", 0) : 0;
            uint dwell = (uint)Math.Max(1, a.Long("dwell_ticks", 10));
            List<BuildBuyProduct> prods = new List<BuildBuyProduct>();
            foreach (BuildBuyProduct p in SortedCatalog())
                if (Placeable(p) && (p.CatalogName ?? "").ToLowerInvariant().IndexOf(want) >= 0) prods.Add(p);
            if (prods.Count == 0) throw new McpException("no placeable catalogue product named like '" + want + "'");
            List<BuildBuyPreset> presets = new List<BuildBuyPreset>();
            if (paint > 0)
            {
                List<object> src = UserToolUtils.GetFloorPatternPresetListByType(0xFFFFFFFF);
                if (src != null)
                    foreach (object o in src)
                    {
                        BuildBuyPreset bp = o as BuildBuyPreset;
                        if (bp != null && bp.Product != null) presets.Add(bp);
                        if (presets.Count >= paint) break;
                    }
                if (presets.Count == 0) throw new McpException("no floor pattern preset to paint with");
            }
            Dictionary<string, object> r = Util.Obj();
            r["done"] = false;
            r["mode"] = build ? "build" : "buy";
            r["products"] = prods.Count;
            r["cycles_done"] = 0;
            r["placed"] = 0;
            r["click_misses"] = 0;
            r["sold"] = 0;
            r["fallback_deletes"] = 0;
            r["painted"] = 0;
            List<object> perCycle = new List<object>();
            r["per_cycle"] = perCycle;
            sBbResult = r;
            RunAsync("bb_cc_cycle", delegate
            {
                List<ulong> seen = new List<ulong>();
                EventHandler onPlaced = delegate(object s, EventArgs e)
                {
                    World.OnObjectPlacedInLotEventArgs pe = e as World.OnObjectPlacedInLotEventArgs;
                    if (pe != null && pe.LotId == lot.LotId && !seen.Contains(pe.ObjectId.Value)) seen.Add(pe.ObjectId.Value);
                };
                World.OnObjectPlacedInLotEventHandler += onPlaced;
                try
                {
                    for (int c = 1; c <= cycles; c++)
                    {
                        Dictionary<string, object> pc = Util.Obj();
                        perCycle.Add(pc);
                        r["phase"] = "cycle " + c + ": enter";
                        if (build) GameStates.TransitionToBuildMode(lot, null); else GameStates.TransitionToBuyMode(lot, null);
                        if (!WaitReady(build ? (Ready)BuildUp : (Ready)BuyUp, 3000)) { r["error"] = "cycle " + c + ": " + r["mode"] + " mode did not open (state " + SubState() + ")"; return; }
                        Ticks(30);
                        Vector3 centre = lot.GetCenterPosition();
                        CameraController.SetPositionAndTarget(new Vector3(centre.x, centre.y + 25f, centre.z - 20f), centre);
                        Ticks(30);
                        sHomography = null;
                        if (presets.Count > 0)
                        {
                            BuildBuyPreset bp = presets[(c - 1) % presets.Count];
                            LotLocation loc = new LotLocation();
                            World.GetLotLocation(centre, ref loc);
                            double[] p0 = TilePixels(lot, loc.mX - 2, loc.mZ - 2), p1 = TilePixels(lot, loc.mX + 1, loc.mZ + 1);
                            UserToolUtils.UserToolGeneric(2u, bp.Product.ProductResourceKey, (uint)bp.ID);
                            UIManager.GetSceneWindow().StartForwardingEventsToUserTools();
                            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseMove, (float)p0[0], (float)p0[1], 0u, 0u);
                            Ticks(dwell);
                            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseDown, (float)p0[0], (float)p0[1], 1000u, 0u);
                            for (int s = 1; s <= 8; s++)
                            {
                                double t = s / 8.0;
                                UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseMove,
                                    (float)(p0[0] + (p1[0] - p0[0]) * t), (float)(p0[1] + (p1[1] - p0[1]) * t), 0u, 0u);
                                Ticks(1);
                            }
                            UserToolUtils.ForwardUserToolMouseEvent(UserToolUtils.UserToolMouseEventType.kUserToolMouseUp, (float)p1[0], (float)p1[1], 1000u, 0u);
                            Ticks(dwell);
                            pc["paint"] = bp.Product.CatalogName;
                            r["painted"] = (int)r["painted"] + 1;
                        }
                        List<ulong> mine = new List<ulong>();
                        int misses = 0;
                        for (int i = 0; i < per; i++)
                        {
                            BuildBuyProduct p = prods[(skip + (c - 1) * per + i) % prods.Count];
                            GameObject probe = GlobalFunctions.CreateObjectOutOfWorld(p.ProductResourceKey) as GameObject;
                            if (probe == null) { misses++; continue; }
                            Vector3 pos = centre, fwd = Vector3.UnitZ;
                            bool spot = GlobalFunctions.FindGoodLocationNearby(probe, ref pos, ref fwd) && LotManager.GetLotAtPoint(pos) == lot;
                            probe.Destroy();
                            if (!spot) { misses++; continue; }
                            if (BuyUp()) Sims3.UI.Responder.Instance.BuyModel.SelectBuyProduct(p, false);
                            else UserToolUtils.UserToolGeneric(4u, p.ProductResourceKey);
                            UIManager.GetSceneWindow().StartForwardingEventsToUserTools();
                            int before = seen.Count;
                            Click(ToPixels(pos.x, pos.z, double.NaN), 2);
                            Ticks(dwell);
                            if (seen.Count == before) misses++;
                            else for (int k = before; k < seen.Count; k++) mine.Add(seen[k]);
                            r["phase"] = "cycle " + c + ": place " + (i + 1) + "/" + per + " (placed " + mine.Count + ", misses " + misses + ")";
                        }
                        UserToolUtils.UserToolGeneric(4u, new ResourceKey(0uL, 0u, 0u));  // hand tool: nothing on the cursor
                        pc["placed"] = mine.Count;
                        pc["misses"] = misses;
                        r["placed"] = (int)r["placed"] + mine.Count;
                        r["click_misses"] = (int)r["click_misses"] + misses;
                        MarkGc(prefix + "-full");
                        r["phase"] = "cycle " + c + ": sell";
                        int sold = 0, fallback = 0;
                        UserToolUtils.UserToolGeneric(12u, new ResourceKey(0uL, 0u, 0u));
                        UIManager.GetSceneWindow().StartForwardingEventsToUserTools();
                        foreach (ulong id in mine)
                        {
                            GameObject o = GameObject.GetObject(new ObjectGuid(id));
                            if (o == null || !o.InWorld) continue;
                            Click(ToPixels(o.Position.x, o.Position.z, double.NaN), 2);
                            Ticks(dwell);
                            o = GameObject.GetObject(new ObjectGuid(id));
                            if (o == null || !o.InWorld) { sold++; continue; }
                            o.Destroy();  // the click hit something else (another object, a wall): leave no CC behind
                            fallback++;
                        }
                        UserToolUtils.UserToolGeneric(4u, new ResourceKey(0uL, 0u, 0u));
                        pc["sold"] = sold;
                        pc["fallback_deletes"] = fallback;
                        r["sold"] = (int)r["sold"] + sold;
                        r["fallback_deletes"] = (int)r["fallback_deletes"] + fallback;
                        GameStates.TransitionToLiveMode();
                        if (!WaitReady(LiveUp, 3000)) { r["error"] = "cycle " + c + ": live mode did not come back (state " + SubState() + ")"; return; }
                        Ticks(30);
                        MarkGc(prefix + "-empty");
                        r["cycles_done"] = c;
                    }
                }
                catch (Exception e) { r["error"] = DescribeError(e); }
                finally
                {
                    World.OnObjectPlacedInLotEventHandler -= onPlaced;
                    r["done"] = true;
                }
            });
            return r;
        }

        // BinCommon.GetLotDimensions (internal): side lengths from the lot corners, in tiles
        static string PlotSize(Lot l)
        {
            Corners k = l.Corners;
            Vector2 o = k.GetVector2Corner(Corner.Origin);
            return (int)((k.GetVector2Corner(Corner.XAxis) - o).Length() + 0.5f) + "x" + (int)((k.GetVector2Corner(Corner.YAxis) - o).Length() + 0.5f);
        }

        static string PlotInfo(IEditTownModel m, Lot l)
        {
            return Util.Id(l.LotId) + " '" + l.Name + "' " + PlotSize(l) + " " + l.LotType + "/" + (l.LotType == LotType.Residential ? l.ResidentialLotSubType.ToString() : l.CommercialLotSubType.ToString())
                + (m.LotIsEmpty(l.LotId) ? " empty" : " built") + (m.LotIsOwned(l.LotId) ? " owned" : "") + (l.Household != null ? " household" : "")
                + (l.IsHouseboatLot() || l.CommercialLotSubType == CommercialLotSubType.kEP10_Port ? " boat/port" : "");
        }

        static double[] TilePixels(Lot lot, int lx, int lz)
        {
            Vector3 wp = Vector3.Zero;
            World.GetWorldPosition(lot.LotId, new LotLocation(lx, lz, 0, 0), ref wp);
            return ToPixels(wp.x, wp.z, double.NaN);
        }

        // et_lot_cycle {cycles?: 4, bin?: lot name part, lot?: lot name | id, dwell_ticks?: 60, prefix?: "et", stay?: false, library?: false,
        // cc_name?: "ccgen", min_objects?: 0, min_cc?: 0} |
        // {result: true}: live mode or Edit Town. Enters Edit Town (GameStates.TransitionToEditTown, the Options menu path),
        // then per cycle: a lot-only entry of the game library (EditTownModel.GetGameBinInfoList, no household, no houseboat
        // or port lot; bin = its name contains) placed on an empty unowned plot it fits (LotContentsWillFit: smaller on bigger
        // passes), a plot of its lot type first, else any type (the place call retypes the plot; the old type is put back with
        // EditTownModel.ChangeLotType after the run, type_restored) through EditTownPlaceTool.PlaceLotTask's model calls (BeginPlaceLotContents, UpdateRoutingData,
        // PlaceLotContents auto rotation / centre, EndPlaceLotContents); mark <prefix>-full + GC; EditTownModel.BulldozeLot
        // (the bulldoze tool's call: kick Sims, BinCommon.BulldozeLot, thumbnails); mark <prefix>-empty + GC. The same plot and
        // bin entry every cycle. Back to live mode at the end unless stay. Lot plots are fixed by the world file (docs/200 §3):
        // this cycles contents on one plot, it cannot add plots.
        // library: true (docs/200 §10) = the player's Library (EditTownModel.GetExportBinInfoList, ExportBinType.Lot) instead of the
        // town's game bin; the place tool's BeginPlaceLotContents imports an export entry itself. Per cycle cc_objects = lot objects whose
        // CatalogName contains cc_name (default "ccgen", the cc-gen clone names); min_objects / min_cc fail the cycle below them.
        // {result: true, check: true} throws when the run ended with an error (load check step).
        static Dictionary<string, object> sEtResult;
        static object EtLotCycle(Args a)
        {
            if (a.Has("result"))
            {
                if (sEtResult == null) throw new McpException("no et_lot_cycle started");
                if (a.Bool("check", false) && (bool)sEtResult["done"] && sEtResult.ContainsKey("error")) throw new McpException("et_lot_cycle failed: " + sEtResult["error"]);
                return sEtResult;
            }
            if (sEtResult != null && !(bool)sEtResult["done"]) throw new McpException("et_lot_cycle still running");
            string st = SubState();
            if (st != "LiveModeState" && st != "EditTownState") throw new McpException("not available: " + st + " (LiveModeState or EditTownState only)");
            int cycles = Math.Max(1, (int)a.Long("cycles", 4));
            uint dwell = (uint)Math.Max(1, a.Long("dwell_ticks", 60));
            string bin = a.Has("bin") ? a.Str("bin").ToLowerInvariant() : null, lotArg = a.Str("lot", null), prefix = a.Str("prefix", "et");
            bool stay = a.Bool("stay", false), library = a.Bool("library", false);
            string ccName = a.Str("cc_name", "ccgen").ToLowerInvariant();
            int minObjects = (int)a.Long("min_objects", 0), minCc = (int)a.Long("min_cc", 0);
            Dictionary<string, object> r = Util.Obj();
            r["done"] = false;
            r["cycles_done"] = 0;
            List<object> perCycle = new List<object>();
            r["per_cycle"] = perCycle;
            sEtResult = r;
            RunAsync("et_lot_cycle", delegate
            {
                try
                {
                    if (!LiveUp() && SubState() != "EditTownState") { r["error"] = "state changed to " + SubState(); return; }
                    if (LiveUp()) GameStates.TransitionToEditTown();
                    if (!WaitReady(delegate { return SubState() == "EditTownState"; }, 3000)) { r["error"] = "Edit Town did not open (state " + SubState() + ")"; return; }
                    Ticks(60);
                    IEditTownModel m = Sims3.UI.Responder.Instance.EditTownModel;
                    UIBinInfo info = null;
                    Lot target = null;
                    List<Lot> lots = new List<Lot>();
                    foreach (Lot l in LotManager.AllLots) if (l != null && !l.IsWorldLot) lots.Add(l);
                    lots.Sort(delegate(Lot x, Lot y) { return x.LotId.CompareTo(y.LotId); });  // same plot every run
                    List<UIBinInfo> entries = new List<UIBinInfo>();
                    foreach (UIBinInfo bi in library ? m.GetExportBinInfoList() : m.GetGameBinInfoList())
                    {
                        if (bi.HouseholdId != ulong.MaxValue || bi.IsHouseboatLot || bi.IsPortLot) continue;
                        if (library && bi.BinType != ExportBinType.Lot) continue;
                        if (bin != null && (bi.LotName ?? "").ToLowerInvariant().IndexOf(bin) < 0) continue;
                        entries.Add(bi);
                    }
                    // Pass 0: a plot of the entry's lot type; pass 1: any type. BeginPlaceLotContents sets the plot's type to the
                    // entry's itself (the place tool lets a player drop any lot on any plot), we put the old type back after the run.
                    // LotContentsWillFit = the game's own fit test: smaller contents on a bigger plot pass (the tool centres them).
                    for (int pass = 0; pass < 2 && info == null; pass++)
                        foreach (UIBinInfo bi in entries)
                        {
                            foreach (Lot l in lots)
                            {
                                if (lotArg != null && l.Name != lotArg && l.LotId.ToString() != lotArg) continue;
                                if (l.Household != null || l.IsHouseboatLot() || l.CommercialLotSubType == CommercialLotSubType.kEP10_Port) continue;
                                if ((pass == 0 && l.LotType != bi.LotType) || !m.LotIsEmpty(l.LotId) || m.LotIsOwned(l.LotId) || !m.LotContentsWillFit(bi, l.LotId)) continue;
                                target = l;
                                break;
                            }
                            if (target != null) { info = bi; break; }
                        }
                    if (info == null)
                    {
                        List<object> bins = new List<object>(), plots = new List<object>();
                        foreach (UIBinInfo bi in entries) bins.Add(bi.LotName + " " + (int)(bi.LotSizeX + 0.5f) + "x" + (int)(bi.LotSizeY + 0.5f) + " " + bi.LotType);
                        foreach (Lot l in lots) plots.Add(PlotInfo(m, l));
                        r["entries"] = bins;
                        r["plots"] = plots;
                        r["error"] = "no lot-only " + (library ? "Library" : "game bin") + " entry" + (bin != null ? " named like '" + bin + "'" : "") + " fits an empty unowned plot" + (lotArg != null ? " named " + lotArg : "")
                            + " (" + entries.Count + " entries, " + lots.Count + " plots: see entries / plots)";
                        return;
                    }
                    LotType origType = target.LotType;
                    CommercialLotSubType origCom = target.CommercialLotSubType;
                    ResidentialLotSubType origRes = target.ResidentialLotSubType;
                    r["bin"] = info.LotName;
                    r["lot"] = target.Name;
                    r["lot_id"] = Util.Id(target.LotId);
                    r["lot_type"] = origType.ToString();
                    r["lot_size"] = PlotSize(target);
                    try
                    {
                    for (int c = 1; c <= cycles; c++)
                    {
                        Dictionary<string, object> pc = Util.Obj();
                        perCycle.Add(pc);
                        pc["lot_id"] = Util.Id(target.LotId);
                        pc["lot_size"] = PlotSize(target);
                        pc["lot_type"] = origType + "/" + (origType == LotType.Residential ? origRes.ToString() : origCom.ToString());
                        pc["bin"] = info.LotName + " " + (int)(info.LotSizeX + 0.5f) + "x" + (int)(info.LotSizeY + 0.5f) + " " + info.LotType;
                        r["phase"] = "cycle " + c + ": place";
                        // Phase marks land in ts3native.log inline (no sim tick needed): EA's import and place block the sim thread
                        // for one native call each (a Library lot: ImportLotContentsFromExportBin, then ImportLotContents behind the
                        // Processing dialog), so a stall past the harness heartbeat is located by the last -ph- mark (docs/67 crash Q).
                        bool sus;
                        int t0 = System.Environment.TickCount;
                        MemMarkTag(prefix + "-ph-import", out sus);
                        int rot;
                        if (!m.BeginPlaceLotContents(info, target.LotId, out rot)) { r["error"] = "cycle " + c + ": BeginPlaceLotContents refused"; return; }
                        m.UpdateRoutingData();
                        pc["import_ms"] = System.Environment.TickCount - t0;
                        t0 = System.Environment.TickCount;
                        MemMarkTag(prefix + "-ph-place", out sus);
                        LotRotationAngle angle = LotRotationAngle.kLotRotateAuto;
                        LotPosition position = LotPosition.kLotPositionCenter;
                        pc["placed"] = m.PlaceLotContents(ref angle, ref position, true);
                        m.EndPlaceLotContents();
                        pc["place_ms"] = System.Environment.TickCount - t0;
                        MemMarkTag(prefix + "-ph-placed", out sus);
                        Ticks(dwell);
                        GameObject[] placed = target.GetObjects<GameObject>();
                        int cc = 0;
                        foreach (GameObject o in placed) if ((o.CatalogName ?? "").ToLowerInvariant().IndexOf(ccName) >= 0) cc++;
                        pc["objects"] = placed.Length;
                        pc["cc_objects"] = cc;
                        MarkGc(prefix + "-full");
                        if (!(bool)pc["placed"] || placed.Length < minObjects || cc < minCc)
                        {
                            r["error"] = "cycle " + c + ": load check failed (placed " + pc["placed"] + ", objects " + placed.Length + " < " + minObjects + " or cc_objects " + cc + " < " + minCc + ")";
                            m.BulldozeLot(target.LotId);
                            return;
                        }
                        MemMarkTag(prefix + "-ph-bulldoze", out sus);
                        m.BulldozeLot(target.LotId);  // yielding context: runs inline (KickSimsOffLotAndBulldoze)
                        Ticks(dwell);
                        pc["objects_after"] = target.GetObjects<GameObject>().Length;
                        MarkGc(prefix + "-empty");
                        r["cycles_done"] = c;
                    }
                    }
                    finally
                    {
                        // the plot is bulldozed (empty) on every exit after the pick: put its own lot type back
                        if (target.LotType != origType || target.CommercialLotSubType != origCom || target.ResidentialLotSubType != origRes)
                            r["type_restored"] = m.ChangeLotType(target.LotId, false, origType, origCom, origRes);
                    }
                    if (!stay)
                    {
                        GameStates.TransitionToLiveMode();
                        if (!WaitReady(LiveUp, 3000)) r["error"] = "live mode did not come back (state " + SubState() + ")";
                    }
                }
                catch (Exception e) { r["error"] = DescribeError(e); }
                finally { r["done"] = true; }
            });
            return r;
        }
    }
}
