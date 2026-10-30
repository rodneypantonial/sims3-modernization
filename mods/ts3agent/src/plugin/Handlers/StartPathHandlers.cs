using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;
using Sims3.SimIFace.Enums;
using Sims3.UI;
using Sims3.UI.GameEntry;

namespace Ts3Agent
{
    // Start-of-game paths for the release check (PLAN.md "Release-check gap found 10-29": v0.2.0-v0.2.4 hung the family
    // picker's Play arrow and Edit Town's lot move, and no release job took those paths). Each command makes the call the
    // UI control makes, on the game's own flow objects (PlayFlowController flows, Edit Town's move tool), so the game's
    // own single tasks (OneShotFunctionTask singleTask: true, run only when UIManager.TaskCount is 1) are queued as a
    // player's click queues them. Nothing here waits in a script task: a waiting task counts in UIManager.TaskCount and
    // would hold the single task back itself; repros poll picker_state / picker_confirm / et_move_lot {action: result}.
    public static partial class Handlers
    {
        static void RegisterStartPaths()
        {
            Dispatcher.Register("picker_state", PickerState);
            Dispatcher.Register("enter_play_flow", EnterPlayFlow);
            Dispatcher.Register("picker_play", PickerPlay);
            Dispatcher.Register("picker_bin", PickerBin);
            Dispatcher.Register("picker_cas", PickerCas);
            Dispatcher.Register("picker_lot", PickerLot);
            Dispatcher.Register("picker_confirm", PickerConfirm);
            Dispatcher.Register("picker_nav", PickerNav);
            Dispatcher.Register("et_move_lot", EtMoveLot);
        }


        const uint kPurchaseBtn = 12579075u, kUnfurnishedRadio = 12579073u, kFurnishedRadio = 12579074u; // PlayFlowConfirmPurchaseLot

        // A harness step answers the next dialog itself (purchase confirm, Edit Town place dialog): the autopilot's auto_dialog
        // leaves dialogs alone until this tick (it would pick Zoom or a rotate button by its scoring).
        static int sHarnessDialogUntil;
        static bool HarnessOwnsDialog() { return sHarnessDialogUntil != 0 && sHarnessDialogUntil - System.Environment.TickCount > 0; }
        static void OwnDialogs(bool on) { sHarnessDialogUntil = on ? System.Environment.TickCount + 120000 : 0; }

        static object CurFlow(PlayFlowController c)
        {
            return c == null ? null : typeof(PlayFlowController).GetField("mCurrentFlow", kAny).GetValue(c);
        }
        static string FlowState(object flow)
        {
            FieldInfo f = flow == null ? null : flow.GetType().GetField("mCurrSubState", kAny);
            return f == null ? null : f.GetValue(flow).ToString();
        }
        static object Call(object o, string method, params object[] args)
        {
            MethodInfo m = o.GetType().GetMethod(method, kAny);
            if (m == null) throw new McpException(o.GetType().Name + "." + method + " not found");
            try { return m.Invoke(o, args); }
            catch (TargetInvocationException e) { throw new McpException(o.GetType().Name + "." + method + ": " + DescribeError(e)); }
        }
        static PlayFlowController Picker()
        {
            PlayFlowController c = PlayFlowController.Singleton;
            PlayFlowModel m = PlayFlowModel.Singleton;
            if (c == null || m == null || !m.IsInitialized) throw new McpException("the family picker is not up (state " + SubState() + "; poll picker_state until controller and model_ready)");
            return c;
        }
        static object UseFlow(PlayFlowController c, PlayFlowController.IFlow want)
        {
            if (CurFlow(c) != want) c.ActivateFlow(want); // the menu's "Play with ..." buttons
            return want;
        }

        static string sPickerTarget; // household picker_play / picker_bin chose

        // picker_state: {sub_state, controller, model_ready, flow, flow_state, modal, ui_tasks, active_household, live, target,
        // target_active}. live = LiveModeState with an active household (the start paths' pass condition).
        static object PickerState(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            PlayFlowController c = PlayFlowController.Singleton;
            PlayFlowModel m = PlayFlowModel.Singleton;
            object f = CurFlow(c);
            Household h = Household.ActiveHousehold;
            string st = SubState();
            r["sub_state"] = st;
            r["controller"] = c != null;
            r["model_ready"] = m != null && m.IsInitialized;
            r["flow"] = f != null ? f.GetType().Name : null;
            r["flow_state"] = FlowState(f);
            r["modal"] = UIManager.GetModalWindow() != null;
            r["ui_tasks"] = UIManager.TaskCount;
            r["active_household"] = h != null ? h.Name : null;
            r["live"] = st == "LiveModeState" && h != null;
            r["target"] = sPickerTarget;
            r["target_active"] = h != null && sPickerTarget != null && h.Name == sPickerTarget;
            return r;
        }

        // enter_play_flow: live mode -> the family picker (GameStates.TransitionToPlayFlow, the "change active household" path:
        // PlayFlowStartupState InWorldChangeHousehold).
        static object EnterPlayFlow(Args a)
        {
            RequireLive();
            RunAsync("enter_play_flow", delegate { GameStates.TransitionToPlayFlow(); });
            Dictionary<string, object> r = Util.Obj();
            r["from"] = Household.ActiveHousehold != null ? Household.ActiveHousehold.Name : null;
            return r;
        }

        // picker_play {name?}: the in-world flow: the household's info panel "approve" (HouseholdSelectApproved) and the Play
        // arrow (OnNavigation Play -> PlayGame single task -> PlayFlowModel.PlayExisting). Default: the first household by name
        // with a home lot, humans and not the active one.
        static object PickerPlay(Args a)
        {
            PlayFlowController c = Picker();
            string name = a.Str("name", null);
            Household target = null;
            foreach (Household h in Household.sHouseholdList)
            {
                if (h == null || h.LotHome == null || h.IsSpecialHousehold || h.IsServiceNpcHousehold || h == Household.ActiveHousehold || h.NumMembers == 0) continue;
                if (name != null ? !string.Equals(h.Name, name, StringComparison.OrdinalIgnoreCase) : target != null && string.CompareOrdinal(h.Name, target.Name) >= 0) continue;
                target = h;
                if (name != null) break;
            }
            if (target == null) throw new McpException(name != null ? "no playable household named '" + name + "'" : "no playable household with a home lot");
            UIBinInfo info = Sims3.UI.Responder.Instance.BinModel.GetLotInfo(target.LotHome.LotId);
            if (info == null) throw new McpException("no bin info for lot " + target.LotHome.Name);
            object flow = UseFlow(c, c.InWorldFlow);
            Call(flow, "HouseholdSelectApproved", info);
            string fs = FlowState(flow);
            if (fs != "ChosenState") throw new McpException("household not chosen (flow state " + fs + ")");
            Dictionary<string, object> r = Util.Obj();
            r["ui_tasks_before"] = UIManager.TaskCount;
            ((PlayFlowController.IFlow)flow).OnNavigation(PlayFlowController.NavigationType.Play);
            sPickerTarget = target.Name;
            r["household"] = target.Name;
            r["lot"] = target.LotHome.Name;
            r["note"] = "Play queued as a UI single task: poll picker_state until live and target_active";
            return r;
        }

        // picker_bin {name?}: the library flow (ImportFlow): approve a household-only bin entry with humans
        // (HouseholdSelectApproved -> ChooseHouse). Default: the first such entry by name that has an affordable lot.
        static object PickerBin(Args a)
        {
            PlayFlowController c = Picker();
            string name = a.Str("name", null);
            IExportBinContents pick = null;
            foreach (IExportBinContents b in Sims3.UI.Responder.Instance.BinModel.ExportBinContents)
            {
                if (b == null || b.Type != ExportBinType.Household || !b.HasHumanSims()) continue;
                if (name != null ? (b.HouseholdName ?? "").IndexOf(name, StringComparison.OrdinalIgnoreCase) < 0 : pick != null && string.CompareOrdinal(b.HouseholdName, pick.HouseholdName) >= 0) continue;
                if (PlayFlowModel.Singleton.CheckLotAvailability(b) != LotAvailability.LotsAvailable) continue;
                pick = b;
            }
            if (pick == null) throw new McpException(name != null ? "no household-only bin entry named like '" + name + "' with an affordable lot" : "no household-only bin entry with an affordable lot");
            object flow = UseFlow(c, c.ImportFlow);
            Call(flow, "HouseholdSelectApproved", pick);
            Dictionary<string, object> r = Util.Obj();
            r["household"] = pick.HouseholdName;
            r["funds"] = pick.HouseholdFunds;
            r["flow_state"] = FlowState(flow);
            sPickerTarget = pick.HouseholdName;
            return r;
        }

        // picker_cas: "Create a new household" (PlayFlowModel.GotoCAS -> CAS). cas_close (Done) returns to the picker's CAS flow.
        static object PickerCas(Args a)
        {
            Picker();
            RunAsync("picker_cas", delegate { PlayFlowModel.Singleton.GotoCAS(); });
            Dictionary<string, object> r = Util.Obj();
            r["queued"] = true;
            return r;
        }

        // picker_lot {lot?}: a lot click on the map in the library or CAS flow (MaptagSelected -> ConfirmPurchaseTask -> the
        // purchase dialog). Default: the first unoccupied residential lot (by id) the household can buy unfurnished. Answer the dialog with
        // picker_confirm (the autopilot leaves it alone meanwhile).
        static object PickerLot(Args a)
        {
            PlayFlowController c = Picker();
            object flow = CurFlow(c);
            if (flow == null) throw new McpException("no picker flow");
            FieldInfo hf = flow.GetType().GetField("mChosenHouseholdInfo", kAny) ?? flow.GetType().GetField("mHouseholdInfo", kAny);
            IExportBinContents hh = hf != null ? hf.GetValue(flow) as IExportBinContents : null;
            if (hh == null) throw new McpException(flow.GetType().Name + " has no chosen household (picker_bin or picker_cas + cas_close first)");
            PlayFlowModel m = PlayFlowModel.Singleton;
            Lot lot = null;
            int unf = 0, fur = 0;
            if (a.Has("lot"))
            {
                string want = a.Str("lot");
                foreach (Lot l in LotManager.AllLots) if (l != null && (l.Name == want || l.LotId.ToString() == want)) { lot = l; break; }
                if (lot == null) throw new McpException("no lot '" + want + "'");
                m.GetLotPrice(lot.LotId, ref unf, ref fur);
            }
            else
            {
                List<Lot> lots = new List<Lot>();
                foreach (Lot l in LotManager.AllLots) if (l != null) lots.Add(l);
                lots.Sort(delegate(Lot x, Lot y) { return x.LotId.CompareTo(y.LotId); });
                foreach (Lot l in lots)
                {
                    if (l.IsWorldLot || l.LotType != LotType.Residential || l.Household != null || l.IsHouseboatLot() || l.ResidentialLotSubType == ResidentialLotSubType.kEP9_Apartment) continue;
                    if (m.GetLotValidity(l.LotId) != LotValidity.Valid) continue;
                    int u = 0, f = 0;
                    if (!m.GetLotPrice(l.LotId, ref u, ref f) || u > hh.HouseholdFunds) continue;
                    lot = l; unf = u; fur = f;
                    break;
                }
                if (lot == null) throw new McpException("no empty residential lot " + hh.HouseholdName + " can buy (funds " + hh.HouseholdFunds + ")");
            }
            OwnDialogs(true);
            ((PlayFlowController.IFlow)flow).MaptagSelected(lot.LotId, MaptagTypes.AvailableEmptyLot, Vector2.Zero);
            Dictionary<string, object> r = Util.Obj();
            r["lot"] = lot.Name;
            r["lot_id"] = Util.Id(lot.LotId);
            r["price"] = unf;
            r["price_furnished"] = fur;
            r["funds"] = hh.HouseholdFunds;
            r["flow"] = flow.GetType().Name;
            return r;
        }

        // picker_confirm {furnished?: false}: answers the purchase dialog picker_lot opened (furnish radio, Purchase) and
        // reports {open, clicked, flow_state, ready}; ready = the flow is in its "ready to play" state.
        static object PickerConfirm(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            object flow = CurFlow(PlayFlowController.Singleton);
            WindowBase modal = UIManager.GetModalWindow();
            bool open = modal != null && modal.GetChildByID(kPurchaseBtn, true) != null;
            r["open"] = open;
            if (open)
            {
                uint radio = a.Bool("furnished", false) ? kFurnishedRadio : kUnfurnishedRadio;
                List<KeyValuePair<WindowBase, object>> clicks = new List<KeyValuePair<WindowBase, object>>();
                foreach (uint id in new uint[] { radio, kPurchaseBtn })
                    foreach (KeyValuePair<WindowBase, object> kv in DialogButtons(modal)) if (kv.Key.ID == id) { clicks.Add(kv); break; }
                List<object> names = new List<object>();
                foreach (KeyValuePair<WindowBase, object> kv in clicks) names.Add("#" + kv.Key.ID);
                r["clicked"] = names;
                RunAsync("picker_confirm", delegate
                {
                    foreach (KeyValuePair<WindowBase, object> kv in clicks)
                    {
                        UIButtonClickEventArgs args = new UIButtonClickEventArgs();
                        args.Init(kButtonClick, kv.Key, kv.Key, (int)kv.Key.ID, 0, 0, 0f, 0f, 0f, 0f, false, null);
                        kv.Value.GetType().GetMethod("Invoke", new Type[] { typeof(WindowBase), typeof(UIEventArgs) }).Invoke(kv.Value, new object[] { kv.Key, args });
                    }
                });
            }
            string fs = FlowState(flow);
            r["flow"] = flow != null ? flow.GetType().Name : null;
            r["flow_state"] = fs;
            r["ready"] = !open && (fs == "ReadyToPlay" || fs == "LotChosen");
            if ((bool)r["ready"]) OwnDialogs(false);
            return r;
        }

        // picker_nav {nav?: Play}: a navigation button of the current picker flow (Play arrow, Cancel, ...).
        static object PickerNav(Args a)
        {
            PlayFlowController c = Picker();
            object flow = CurFlow(c);
            if (flow == null) throw new McpException("no picker flow");
            PlayFlowController.NavigationType nav = Util.ParseEnum<PlayFlowController.NavigationType>(a.Str("nav", "Play"));
            Dictionary<string, object> r = Util.Obj();
            r["ui_tasks_before"] = UIManager.TaskCount;
            r["flow"] = flow.GetType().Name;
            r["flow_state"] = FlowState(flow);
            ((PlayFlowController.IFlow)flow).OnNavigation(nav);
            r["nav"] = nav.ToString();
            return r;
        }

        // ------------------------------------------------------------ Edit Town: move a lot (EditTownMoveRotateTool)

        static readonly Type kMoveTool = typeof(PlayFlowController).Assembly.GetType("Sims3.UI.GameEntry.EditTownMoveRotateTool");
        static readonly Type kPlaceDialog = typeof(PlayFlowController).Assembly.GetType("Sims3.UI.GameEntry.EditTownPlaceDialog");
        static Dictionary<string, object> sEtMove;
        static object sEtDialogBefore;
        static int sEtSeenAt;

        static object PlaceDialog() { return kPlaceDialog.GetField("mDialog", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null); }

        // et_move_lot {action?: enter | move (default) | result, from?, to?}
        //   enter: live mode -> Edit Town (GameStates.TransitionToEditTown, the Options menu path).
        //   move (in Edit Town): the move tool's two map clicks (OnToolSelected, OnLotPick(from) -> Placing, OnLotPick(to) ->
        //     OnPlace -> PlaceTask as a UI single task). from: default the first built residential lot (by id) with no household,
        //     not locked; to: default the first empty unowned plot of its type the contents fit.
        //   result: {placing, place_task_ran (its place dialog opened), accepted, placed (from empty, to built), ui_tasks}; the
        //     first poll >= 2 s after the dialog opened presses its Accept (EditTownPlaceDialog.OnAcceptButtonClick).
        static object EtMoveLot(Args a)
        {
            string action = a.Str("action", "move");
            Dictionary<string, object> r = Util.Obj();
            if (action == "enter")
            {
                RequireLive();
                RunAsync("et_move_lot enter", delegate { GameStates.TransitionToEditTown(); });
                r["entering"] = true;
                return r;
            }
            if (kMoveTool == null || kPlaceDialog == null) throw new McpException("EditTownMoveRotateTool / EditTownPlaceDialog not found in UI");
            object tool = kMoveTool.GetProperty("Instance", BindingFlags.Public | BindingFlags.Static).GetValue(null, null);
            IEditTownModel m = Sims3.UI.Responder.Instance.EditTownModel;
            if (action == "result") return EtMoveResult(tool, m);
            if (action != "move") throw new McpException("action: enter | move | result");
            if (SubState() != "EditTownState") throw new McpException("not in Edit Town (" + SubState() + "): et_move_lot {action: enter} first");
            List<Lot> lots = new List<Lot>();
            foreach (Lot l in LotManager.AllLots) if (l != null && !l.IsWorldLot && !l.IsHouseboatLot() && l.CommercialLotSubType != CommercialLotSubType.kEP10_Port) lots.Add(l);
            lots.Sort(delegate(Lot x, Lot y) { return x.LotId.CompareTo(y.LotId); });
            Lot from = null, to = null;
            UIBinInfo fromInfo = null;
            foreach (Lot l in lots)
            {
                if (a.Has("from") ? l.Name != a.Str("from") && l.LotId.ToString() != a.Str("from")
                    : l.LotType != LotType.Residential || l.Household != null || m.LotIsEmpty(l.LotId) || m.IsLotLocked(l.LotId)) continue;
                if (m.GetLotValidity(l.LotId) != LotValidity.Valid || (fromInfo = m.FindLotBinInfo(l.LotId)) == null) continue;
                from = l;
                break;
            }
            if (from == null) throw new McpException("no lot to move" + (a.Has("from") ? " named " + a.Str("from") : " (built residential, no household, not locked)"));
            foreach (Lot l in lots)
            {
                if (l == from || (a.Has("to") ? l.Name != a.Str("to") && l.LotId.ToString() != a.Str("to")
                    : l.LotType != from.LotType || l.Household != null || !m.LotIsEmpty(l.LotId) || m.LotIsOwned(l.LotId))) continue;
                if (m.GetLotValidity(l.LotId) != LotValidity.Valid || !m.LotContentsWillFit(fromInfo, l.LotId)) continue;
                to = l;
                break;
            }
            if (to == null) throw new McpException("no empty plot " + from.Name + " fits" + (a.Has("to") ? " named " + a.Str("to") : ""));
            sEtDialogBefore = PlaceDialog();
            sEtSeenAt = 0;
            Call(tool, "OnToolSelected");
            if (!(bool)Call(tool, "OnLotPick", from.LotId)) throw new McpException("the move tool refused " + from.Name);
            OwnDialogs(true);
            Call(tool, "OnLotPick", to.LotId);
            bool placing = (bool)kMoveTool.GetField("mPlacing", kAny).GetValue(tool);
            if (!placing) { OwnDialogs(false); throw new McpException("the move tool did not queue its PlaceTask (target " + to.Name + ")"); }
            r["from"] = from.Name;
            r["from_id"] = Util.Id(from.LotId);
            r["to"] = to.Name;
            r["to_id"] = Util.Id(to.LotId);
            r["ui_tasks"] = UIManager.TaskCount;
            r["queued"] = true;
            r["accepted"] = false;
            sEtMove = r;
            return r;
        }

        static object EtMoveResult(object tool, IEditTownModel m)
        {
            if (sEtMove == null) throw new McpException("no et_move_lot move started");
            Dictionary<string, object> r = new Dictionary<string, object>(sEtMove);
            bool placing = (bool)kMoveTool.GetField("mPlacing", kAny).GetValue(tool);
            object dlg = PlaceDialog();
            bool ran = dlg != null && dlg != sEtDialogBefore;
            if (ran && sEtSeenAt == 0) sEtSeenAt = System.Environment.TickCount | 1;
            if (ran && placing && !(bool)sEtMove["accepted"] && System.Environment.TickCount - sEtSeenAt >= 2000)
            {
                sEtMove["accepted"] = true;
                RunAsync("et_move_lot accept", delegate { kPlaceDialog.GetMethod("OnAcceptButtonClick", kAny).Invoke(dlg, new object[] { null, null }); });
            }
            ulong from = Convert.ToUInt64(sEtMove["from_id"]), to = Convert.ToUInt64(sEtMove["to_id"]);
            bool fromEmpty = m.LotIsEmpty(from), toEmpty = m.LotIsEmpty(to);
            r["placing"] = placing;
            r["place_task_ran"] = ran;
            r["accepted"] = sEtMove["accepted"];
            r["from_empty"] = fromEmpty;
            r["to_empty"] = toEmpty;
            r["placed"] = (bool)sEtMove["accepted"] && !placing && fromEmpty && !toEmpty;
            r["ui_tasks"] = UIManager.TaskCount;
            if (!placing && (bool)sEtMove["accepted"]) OwnDialogs(false);
            return r;
        }
    }
}
