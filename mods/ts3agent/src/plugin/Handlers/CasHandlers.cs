using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;
using Sims3.SimIFace.CAS;
using Sims3.UI;
using Sims3.UI.CAS;

namespace Ts3Agent
{
    // Create-a-Sim and Buy catalog browsing, to time how long panels take to fill with lots of store content
    // (docs/17 §4 M6, §5). Entry and exit use the game's own calls: the "Edit in CAS" cheat path (Sim.EditSimInCAS:
    // CASLogic.LoadSim + UseTempSimDesc + GameStates.TransitionToCASMode), the CAS close button's discard path
    // (CASPuck.OnCloseClick: RequestClearChangeReport + CASController state None), GameStates.TransitionToBuyMode /
    // TransitionToLiveMode. Category switches call what the tab buttons call. Nothing here saves or commits: CAS edits a
    // temporary copy of the Sim and is always left through the discard path.
    // Every action runs as a game task and returns at once; the task times it (ms until ready and the item count stops
    // changing) and records the result, which the next call and cas_state report.
    public static partial class Handlers
    {
        static void RegisterCas()
        {
            Dispatcher.Register("cas_open", CasOpen);
            Dispatcher.Register("cas_category", CasCategory);
            Dispatcher.Register("cas_close", CasClose);
            Dispatcher.Register("cas_state", CasState);
            Dispatcher.Register("buy_open", BuyOpen);
            Dispatcher.Register("buy_category", BuyCategory);
            Dispatcher.Register("buy_close", BuyClose);
        }

        const BindingFlags kPriv = BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
        delegate bool Ready();
        delegate int Counter();

        static readonly List<Dictionary<string, object>> sUiTimings = new List<Dictionary<string, object>>();
        static volatile bool sUiMeasuring;

        // Runs start() in a game task, then polls every 100 ms until ready() holds and count() has not changed for 1.5 s
        // (or timeout). ms = time of the last change seen, i.e. when the panel finished filling.
        static object Measure(string label, Work start, Ready ready, Counter count, int timeoutMs)
        {
            if (sUiMeasuring) throw new McpException("previous CAS/Buy action is still being timed; wait and retry");
            Dictionary<string, object> prev;
            lock (sUiTimings) prev = sUiTimings.Count > 0 ? sUiTimings[sUiTimings.Count - 1] : null;
            sUiMeasuring = true;
            Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(delegate
            {
                Dictionary<string, object> r = Util.Obj();
                r["action"] = label;
                int t0 = System.Environment.TickCount;
                try
                {
                    start();
                    r["sync_ms"] = System.Environment.TickCount - t0;
                    int last = -1, lastChange = 0, lastPoll = -1000;
                    bool settled = false;
                    while (System.Environment.TickCount - t0 < timeoutMs)
                    {
                        Simulator.Sleep(0);
                        int now = System.Environment.TickCount - t0;
                        if (now - lastPoll < 100) continue;
                        lastPoll = now;
                        if (!ready()) continue;
                        int n = count != null ? count() : 0;
                        if (n != last) { last = n; lastChange = now; continue; }
                        if (now - lastChange >= 1500) { settled = true; break; }
                    }
                    r["ms"] = lastChange;
                    r["items"] = last;
                    r["settled"] = settled;
                }
                catch (Exception e)
                {
                    Exception x = e is TargetInvocationException && e.InnerException != null ? e.InnerException : e;
                    r["error"] = x.GetType().Name + ": " + x.Message;
                }
                finally
                {
                    r["state"] = SubState();
                    lock (sUiTimings)
                    {
                        sUiTimings.Add(r);
                        if (sUiTimings.Count > 60) sUiTimings.RemoveAt(0);
                    }
                    sUiMeasuring = false;
                }
            }));
            Dictionary<string, object> q = Util.Obj();
            q["queued"] = label;
            q["previous"] = prev;
            return q;
        }

        // Sum of all visible item grids under the main UI window (catalog/part lists), with depth and width caps.
        static int VisibleGridItems(WindowBase w, int depth)
        {
            if (w == null || depth > 24 || !w.Visible) return 0;
            ItemGrid g = w as ItemGrid;
            int n = g != null ? g.Count : 0;
            for (uint i = 0; i < 500; i++)
            {
                WindowBase child;
                try { child = w.GetChildByIndex(i); } catch (Exception) { break; }
                if (child == null) break;
                n += VisibleGridItems(child, depth + 1);
            }
            return n;
        }

        static int UiItems() { return VisibleGridItems(UIManager.GetMainWindow(), 0); }

        // ------------------------------------------------------------ CAS

        static bool CasUp()
        {
            string s = SubState();
            return s != null && s.StartsWith("CAS") && CASPuck.Instance != null && CASController.Singleton != null;
        }

        static bool CasReady() { return CasUp() && CASPuck.Instance.Visible && !Responder.Instance.CASModel.IsProcessing(); }

        // the same refusals as Sim.EditSimInCAS (not in the active household, babies, pregnancy, odd outfits); null = can edit
        static string CasRefusal(Sim s)
        {
            SimDescription sd = s.SimDescription;
            if (sd.Household == null || sd.Household != Household.ActiveHousehold) return sd.FullName + " is not in the active household";
            if (!s.IsHuman || sd.Baby || sd.IsPregnant || sd.IsVisuallyPregnant) return sd.FullName + " cannot be edited in CAS (not human, baby or pregnant)";
            OutfitCategories oc = s.CurrentOutfitCategory;
            if (oc != OutfitCategories.Everyday && oc != OutfitCategories.Formalwear && oc != OutfitCategories.Sleepwear
                && oc != OutfitCategories.Swimwear && oc != OutfitCategories.Athletic && oc != OutfitCategories.Outerwear)
                return sd.FullName + " is wearing " + oc + "; CAS needs an everyday/formal/sleep/swim/athletic/outerwear outfit";
            return null;
        }

        // cas_open {sim?}: Edit-in-CAS for the named Sim, else the active Sim, else the first household member CAS accepts
        // (the active one may be in a towel or a career outfit); CAS works on a copy.
        static object CasOpen(Args a)
        {
            if (SubState() != "LiveModeState") throw new McpException("not in live mode (state " + SubState() + ")");
            Sim s = a.Has("sim") ? Util.FindSim(a, "sim") : Util.ActiveSim();
            string why = CasRefusal(s);
            if (why != null && !a.Has("sim"))
                foreach (Sim m in Household.ActiveHousehold.Sims)
                    if (CasRefusal(m) == null) { s = m; why = null; break; }
            if (why != null) throw new McpException(why);
            SimDescription sd = s.SimDescription;
            OutfitCategories oc = s.CurrentOutfitCategory;
            object r = Measure("cas_open " + sd.FullName, delegate
            {
                CASLogic cas = CASLogic.GetSingleton();
                cas.LoadSim(sd, oc, 0);
                cas.UseTempSimDesc = true;
                GameStates.TransitionToCASMode();
            }, CasReady, null, 180000);
            return r;
        }

        static readonly string[] kCasCategories = { "basics", "body", "hair", "face", "makeup", "personality", "everyday", "formal",
            "sleepwear", "swimwear", "athletic", "outerwear", "tops", "bottoms", "shoes", "outfits", "accessories" };

        // cas_category {category}: the tab the CAS buttons switch to (CASCharacterSheet/CASFacialDetails/CASClothing call
        // CASController.SetCurrentState + Activate; the clothing tabs also set CASModel.OutfitCategory; the clothing type
        // row is CASClothingCategory.SetTypeCategory).
        static object CasCategory(Args a)
        {
            if (!CasUp()) throw new McpException("CAS is not open (state " + SubState() + ")");
            string c = a.Str("category").Trim().ToLowerInvariant();
            CASMidState mid = CASMidState.Clothing;
            CASPhysicalState phys = CASPhysicalState.None;
            CASClothingState cloth = CASClothingState.Everyday;
            OutfitCategories outfit = OutfitCategories.Everyday;
            string type = null;
            switch (c)
            {
                case "basics": case "body": mid = CASMidState.Basics; break;
                case "hair": mid = CASMidState.Physical; phys = CASPhysicalState.Hair; break;
                case "face": mid = CASMidState.FacialDetails; phys = CASPhysicalState.FacialDetailsSummary; break;
                case "makeup": mid = CASMidState.FacialDetails; phys = CASPhysicalState.Makeup; break;
                case "personality": mid = CASMidState.Character; break;
                case "clothing": case "everyday": break;
                case "formal": cloth = CASClothingState.Formal; outfit = OutfitCategories.Formalwear; break;
                case "sleepwear": cloth = CASClothingState.Sleepwear; outfit = OutfitCategories.Sleepwear; break;
                case "swimwear": cloth = CASClothingState.Swimwear; outfit = OutfitCategories.Swimwear; break;
                case "athletic": cloth = CASClothingState.Exercise; outfit = OutfitCategories.Athletic; break;
                case "outerwear": cloth = CASClothingState.Outerwear; outfit = OutfitCategories.Outerwear; break;
                case "tops": case "bottoms": case "shoes": case "outfits": case "accessories": type = c; break;
                default: throw new McpException("unknown CAS category '" + c + "'; one of: " + string.Join(", ", kCasCategories));
            }
            if (mid != CASMidState.Clothing) { cloth = CASClothingState.None; outfit = OutfitCategories.None; }
            CASState target = new CASState(CASTopState.CreateASim, mid, phys, cloth);
            return Measure("cas_category " + c, delegate
            {
                ICASModel m = Responder.Instance.CASModel;
                if (outfit != OutfitCategories.None && m.OutfitCategory != outfit) m.OutfitCategory = outfit;
                CASController ctl = CASController.Singleton;
                CASState cur = ctl.CurrentState;
                if (cur.mTopState != target.mTopState || cur.mMidState != target.mMidState || cur.mPhysicalState != target.mPhysicalState || cur.mClothingState != target.mClothingState)
                {
                    typeof(CASController).GetMethod("SetCurrentState", kPriv).Invoke(ctl, new object[] { target }); // internal
                    ctl.Activate(true);
                }
                if (type == null) return;
                // the clothing panel loads behind a fade; its type row exists once it is up
                FieldInfo f = typeof(CASClothingCategory).GetField("gSingleton", kPriv);
                int t0 = System.Environment.TickCount;
                CASClothingCategory cc = null;
                while ((cc = f.GetValue(null) as CASClothingCategory) == null && System.Environment.TickCount - t0 < 15000) Simulator.Sleep(0);
                if (cc == null) throw new McpException("clothing panel did not load");
                cc.SetTypeCategory((CASClothingCategory.Category)Enum.Parse(typeof(CASClothingCategory.Category), type, true));
            }, CasReady, UiItems, 60000);
        }

        // cas_close: the close button's "exit without saving" path, minus its Yes/No dialog; back to live mode.
        static object CasClose(Args a)
        {
            if (!CasUp()) throw new McpException("CAS is not open (state " + SubState() + ")");
            return Measure("cas_close", delegate
            {
                ICASModel m = Responder.Instance.CASModel;
                CASController ctl = CASController.Singleton;
                ctl.AllowCameraMovement(false);
                int t0 = System.Environment.TickCount;
                while (m.IsProcessing() && System.Environment.TickCount - t0 < 30000) Simulator.Sleep(0);
                m.RequestClearChangeReport();
                ctl.Shutdown(); // = SetCurrentState(CASState.None): CASModel.Done -> ClosingDown -> Quit -> GameStates.ReturnFromCAS
            }, delegate { return SubState() == "LiveModeState"; }, null, 180000);
        }

        // cas_state: in-world state, CAS tab, Buy catalog, whether a timing is running, and all recorded timings.
        static object CasState(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            r["state"] = SubState();
            if (CasUp())
            {
                CASState st = CASController.Singleton.CurrentState;
                r["cas"] = st.mTopState + "/" + st.mMidState + "/" + st.mPhysicalState + "/" + st.mClothingState;
                r["cas_ready"] = CasReady();
                r["outfit"] = Responder.Instance.CASModel.OutfitCategory.ToString();
            }
            if (BuyController.Active)
            {
                object c = BuyCtl();
                r["buy_catalog"] = Convert.ToString(c.GetType().GetField("mCurrCatalogType", kPriv).GetValue(c));
                r["buy_items"] = BuyItems();
            }
            r["measuring"] = sUiMeasuring;
            lock (sUiTimings) r["timings"] = new List<Dictionary<string, object>>(sUiTimings);
            return r;
        }

        // ------------------------------------------------------------ Buy

        static object BuyCtl() { return typeof(BuyController).GetField("sController", kPriv).GetValue(null); }

        static int BuyItems()
        {
            object c = BuyCtl();
            ItemGrid g = c == null ? null : c.GetType().GetField("mCatalogGrid", kPriv).GetValue(c) as ItemGrid;
            return g == null ? 0 : g.Count;
        }

        static bool BuyUp() { return SubState() == "BuyModeState" && BuyController.Active && BuyCtl() != null; }

        // buy_open: Buy mode on the active household's lot (what the Buy button does).
        static object BuyOpen(Args a)
        {
            if (SubState() != "LiveModeState") throw new McpException("not in live mode (state " + SubState() + ")");
            Lot lot = HomeLot();
            if (lot == null) throw new McpException("no home lot for the active household");
            return Measure("buy_open", delegate { GameStates.TransitionToBuyMode(lot, null); }, BuyUp, BuyItems, 180000);
        }

        static void FindTagged(WindowBase w, string tagType, List<Button> found, int depth)
        {
            if (w == null || depth > 24) return;
            Button b = w as Button;
            if (b != null && b.Tag != null && b.Tag.GetType().Name == tagType && b.Tag.GetType().DeclaringType == typeof(BuyController)) found.Add(b);
            for (uint i = 0; i < 500; i++)
            {
                WindowBase child;
                try { child = w.GetChildByIndex(i); } catch (Exception) { break; }
                if (child == null) break;
                FindTagged(child, tagType, found, depth + 1);
            }
        }

        // buy_category {category}: a by-function category (Comfort, Surfaces, Decor, ...: the game's internal or
        // localized name, exact or contained). Switches the catalog to by-function first if needed; then calls the
        // category button's own click handler (BuyController.OnCategoryButtonClick -> OnSelectCategory).
        static object BuyCategory(Args a)
        {
            if (!BuyUp()) throw new McpException("Buy mode is not open (state " + SubState() + ")");
            if (sUiMeasuring) throw new McpException("previous CAS/Buy action is still being timed; wait and retry");
            string want = a.Str("category").Trim();
            object c = BuyCtl();
            Type t = typeof(BuyController);
            if (Convert.ToString(t.GetField("mCurrCatalogType", kPriv).GetValue(c)) != "ByCategory")
            {
                Button byFunction = t.GetField("mCatalogCategoryModeButton", kPriv).GetValue(c) as Button;
                if (byFunction == null) throw new McpException("no by-function catalog button");
                t.GetMethod("OnCatalogButtonClick", kPriv).Invoke(c, new object[] { byFunction, null });
            }
            List<Button> buttons = new List<Button>();
            FindTagged((WindowBase)c, "Category", buttons, 0);
            Button hit = null, partial = null;
            List<string> names = new List<string>();
            foreach (Button b in buttons)
            {
                Type ct = b.Tag.GetType();
                string name = ct.GetField("mName").GetValue(b.Tag) as string ?? "";
                string loc = ct.GetField("mLocalizedName").GetValue(b.Tag) as string ?? "";
                names.Add(name);
                if (string.Equals(name, want, StringComparison.OrdinalIgnoreCase) || string.Equals(loc, want, StringComparison.OrdinalIgnoreCase)) hit = b;
                else if (partial == null && (name.IndexOf(want, StringComparison.OrdinalIgnoreCase) >= 0 || loc.IndexOf(want, StringComparison.OrdinalIgnoreCase) >= 0)) partial = b;
            }
            hit = hit ?? partial;
            if (hit == null) throw new McpException("no buy category '" + want + "'; found: " + string.Join(", ", names.ToArray()));
            return Measure("buy_category " + want, delegate
            {
                t.GetMethod("OnCategoryButtonClick", kPriv).Invoke(c, new object[] { hit, null });
            }, BuyUp, BuyItems, 60000);
        }

        // buy_close: back to live mode (nothing bought, so nothing to undo).
        static object BuyClose(Args a)
        {
            if (!BuyUp()) throw new McpException("Buy mode is not open (state " + SubState() + ")");
            return Measure("buy_close", delegate { GameStates.TransitionToLiveMode(); }, delegate { return SubState() == "LiveModeState"; }, null, 180000);
        }
    }
}
