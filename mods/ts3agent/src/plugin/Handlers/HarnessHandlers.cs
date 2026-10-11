using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Autonomy;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.SimIFace;
using Sims3.UI;
using Sims3.UI.GameEntry;

namespace Ts3Agent
{
    // Test-harness commands (not in upstream Sims3-MCP): drive the game from the main menu to a playable,
    // saved world without clicks, using the same calls as EA's menu code (MainMenuState, PlayFlowModel).
    public static partial class Handlers
    {
        static void RegisterHarness()
        {
            Dispatcher.Register("game_state", GameState);
            Dispatcher.Register("load_world", LoadWorld);
            Dispatcher.Register("load_save", LoadSave);
            Dispatcher.Register("world_households", WorldHouseholds);
            Dispatcher.Register("play_household", PlayHousehold);
            Dispatcher.Register("quit_game", QuitGame);
            Dispatcher.Register("auto_dialog", AutoDialog);
            Dispatcher.Register("enter_live_mode", EnterLiveMode);
            Dispatcher.Register("reflect_call_async", ReflectCallAsync);
            Dispatcher.Register("sims_activity", SimsActivity);
            Dispatcher.Register("keep_alive", KeepAlive);
        }

        // Safety net for unattended runs (like NRaas MasterController's max motives, but only in the danger zone):
        // any active-household motive below the threshold goes to full. Above it the Sims still eat, sleep and use
        // the toilet themselves, so autonomy and routing load stay realistic.
        static object KeepAlive(Args a)
        {
            Household hh = Household.ActiveHousehold;
            if (hh == null) throw new McpException("no active household");
            float threshold = (float)a.Double("threshold", -50);
            List<object> topped = new List<object>();
            foreach (Sim s in hh.AllActors)
            {
                Motives m = s.Motives;
                if (m == null) continue;
                foreach (CommodityKind k in kShownMotives)
                {
                    if (k == CommodityKind.Temperature || !m.HasMotive(k)) continue;
                    float v = m.GetValue(k);
                    if (v >= threshold) continue;
                    m.SetMax(k);
                    topped.Add(s.FirstName + " " + k + " " + Math.Round(v));
                }
            }
            Dictionary<string, object> r = Util.Obj();
            r["topped_up"] = topped;
            return r;
        }

        // Every instantiated Sim with where it is and what it is doing; the watchdog diffs this over time to spot
        // Sims that stay on the same interaction at the same spot (stuck) or keep routing without arriving.
        static object SimsActivity(Args a)
        {
            List<object> list = new List<object>();
            foreach (Sim s in Sims3.Gameplay.Queries.GetObjects<Sim>())
            {
                if (s == null || s.SimDescription == null) continue;
                Dictionary<string, object> o = Util.Obj();
                o["id"] = Util.Id(s.ObjectId);
                o["name"] = s.FullName;
                Vector3 p = s.Position;
                o["x"] = Math.Round(p.x, 1);
                o["z"] = Math.Round(p.z, 1);
                o["lot"] = s.LotCurrent != null ? s.LotCurrent.Name : null;
                InteractionInstance cur = null;
                try { cur = s.CurrentInteraction; } catch (Exception) { }
                string curName = null;
                if (cur != null) { try { curName = cur.GetInteractionName(); } catch (Exception) { curName = cur.GetType().Name; } }
                o["interaction"] = curName;
                o["queue"] = s.InteractionQueue != null ? s.InteractionQueue.Count : 0;
                bool routing = false;
                try { routing = s.SimRoutingComponent != null && s.SimRoutingComponent.IsRouting; } catch (Exception) { }
                o["routing"] = routing;
                o["active_household"] = s.Household != null && s.Household == Household.ActiveHousehold;
                list.Add(o);
                if (list.Count >= 400) break;
            }
            return list;
        }

        // Current in-world sub state (LiveMode, EditTown, BuildMode, CAS...), read from GameStates' private state machines.
        static string SubState()
        {
            try
            {
                object gs = typeof(GameStates).GetField("sSingleton", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null);
                object inWorld = typeof(GameStates).GetField("mInWorldState", BindingFlags.NonPublic | BindingFlags.Instance).GetValue(gs);
                if (inWorld == null) return null;
                object sm = inWorld.GetType().GetField("mStateMachine", BindingFlags.NonPublic | BindingFlags.Instance).GetValue(inWorld);
                object cur = sm.GetType().GetProperty("CurState").GetValue(sm, null);
                return cur == null ? null : cur.GetType().Name;
            }
            catch (Exception e) { return "unknown: " + e.Message; }
        }

        // Edit Town pauses the clock; this is what the Edit Town "Play" button ends in.
        static object EnterLiveMode(Args a)
        {
            RunAsync("enter_live_mode", delegate { GameStates.TransitionToLiveMode(); });
            Dictionary<string, object> r = Util.Obj();
            r["from"] = SubState();
            return r;
        }

        // reflect_call for methods that yield (state transitions, waits): runs as a game task, result lands in async_log.
        static object ReflectCallAsync(Args a)
        {
            RunAsync("reflect_call " + a.Str("path"), delegate { ReflectCall(a); });
            Dictionary<string, object> r = Util.Obj();
            r["queued"] = a.Str("path");
            return r;
        }

        // ------------------------------------------------------------ blocking dialogs

        static readonly string[] kAcceptWords = { "ok", "accept", "yes", "continue", "done", "confirm", "close", "got it" };
        static readonly string[] kRejectWords = { "cancel", "no", "back", "quit", "exit", "decline", "never" };
        static readonly string[] kNames = { "Alex", "Sam", "Jamie", "Robin", "Casey", "Morgan", "Riley", "Quinn", "Avery", "Reese" };
        static int sNameIndex;

        static void FindTextEdits(WindowBase w, List<TextEdit> found, int depth)
        {
            if (w == null || depth > 12) return;
            TextEdit te = w as TextEdit;
            if (te != null && te.Visible) found.Add(te);
            for (uint i = 0; i < 200; i++)
            {
                WindowBase child;
                try { child = w.GetChildByIndex(i); } catch (Exception) { break; }
                if (child == null) break;
                FindTextEdits(child, found, depth + 1);
            }
        }

        static string Words(WindowBase b)
        {
            string tip = null;
            Button button = b as Button;
            if (button != null) { try { tip = button.TooltipText; } catch (Exception) { } }
            return ((b.Caption ?? "") + " " + (tip ?? "")).Trim().ToLowerInvariant();
        }

        static int Score(string words)
        {
            foreach (string w in kRejectWords) if (words == w || words.StartsWith(w + " ")) return -1;
            foreach (string w in kAcceptWords) if (words == w || words.StartsWith(w + " ") || words.IndexOf(w) >= 0) return 2;
            return words.Length == 0 ? 0 : 1;
        }

        // Clears whatever modal dialog blocks the game: fills empty text fields (e.g. naming a newborn),
        // then clicks the most "accepting" enabled button. Prefers tooltips because EA icon buttons have no caption.
        static object AutoDialog(Args a)
        {
            WindowBase modal = UIManager.GetModalWindow();
            Dictionary<string, object> r = Util.Obj();
            if (modal == null) { r["open"] = false; return r; }
            r["open"] = true;
            List<string> texts = new List<string>();
            CollectTexts(modal, texts, 0);
            r["title"] = texts.Count > 0 ? texts[0] : null;

            List<TextEdit> edits = new List<TextEdit>();
            FindTextEdits(modal, edits, 0);
            List<object> filled = new List<object>();
            foreach (TextEdit te in edits)
            {
                if (!string.IsNullOrEmpty(te.Caption)) continue;
                string name = kNames[sNameIndex++ % kNames.Length];
                te.Caption = name;
                filled.Add(name);
            }
            r["filled"] = filled;

            KeyValuePair<WindowBase, object>? best = null;
            int bestScore = int.MinValue;
            List<object> seen = new List<object>();
            foreach (KeyValuePair<WindowBase, object> kv in DialogButtons(modal))
            {
                string words = Words(kv.Key);
                bool enabled = true;
                try { enabled = kv.Key.Enabled; } catch (Exception) { }
                int s = enabled ? Score(words) : -2;
                seen.Add("#" + kv.Key.ID + " '" + words + "'" + (enabled ? "" : " (disabled)") + " score " + s);
                if (s > bestScore) { bestScore = s; best = kv; }
            }
            r["buttons"] = seen;
            if (best == null || bestScore < 0) throw new McpException("no acceptable button: " + string.Join(", ", seen.ConvertAll(o => (string)o).ToArray()));
            WindowBase btn = best.Value.Key;
            object glue = best.Value.Value;
            MethodInfo invoke = glue.GetType().GetMethod("Invoke", new Type[] { typeof(WindowBase), typeof(UIEventArgs) });
            UIButtonClickEventArgs args = new UIButtonClickEventArgs();
            args.Init(kButtonClick, btn, btn, (int)btn.ID, 0, 0, 0f, 0f, 0f, 0f, false, null);
            RunAsync("auto_dialog #" + btn.ID, delegate { invoke.Invoke(glue, new object[] { btn, args }); });
            r["clicked"] = "#" + btn.ID + " " + Words(btn);
            return r;
        }

        static object GameState(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            r["in_world"] = GameStates.IsInWorld();
            r["sub_state"] = SubState();
            r["shutting_down"] = GameStates.IsGameShuttingDown;
            Household active = Household.ActiveHousehold;
            r["active_household"] = active != null ? active.Name : null;
            r["households"] = Household.sHouseholdList != null ? Household.sHouseholdList.Count : 0;
            r["save_name"] = Sims3.Gameplay.UI.Responder.Instance.OptionsModel.SaveName;
            r["save_in_progress"] = Sims3.Gameplay.UI.Responder.Instance.OptionsModel.SaveGameInProgress;
            return r;
        }

        static InWorldState.SubState ParseState(string s)
        {
            return (InWorldState.SubState)Enum.Parse(typeof(InWorldState.SubState), s, true);
        }

        // file: a world such as "Sunset Valley.world" (new game) ; state: LiveMode (default), PlayFlow or EditTown.
        static object LoadWorld(Args a)
        {
            if (GameStates.IsInWorld()) throw new McpException("already in a world; quit to the main menu first");
            string file = a.Str("file");
            InWorldState.SubState state = ParseState(a.Str("state", "LiveMode"));
            WorldFileMetadata info = new WorldFileMetadata();
            info.mWorldFile = file;
            if (!Sims3.Gameplay.UI.Responder.Instance.MainMenuModel.GetWorldFileDetails(ref info))
                throw new McpException("unknown world file '" + file + "'");
            RunAsync("load_world " + file, delegate { GameStates.TransitionToGameStateToInWorld(file, info, state, true); });
            Dictionary<string, object> r = Util.Obj();
            r["loading"] = file;
            r["state"] = state.ToString();
            return r;
        }

        // file: a save such as "Baseline-SV.sims3" from Documents\Electronic Arts\The Sims 3\Saves.
        static object LoadSave(Args a)
        {
            if (GameStates.IsInWorld()) throw new McpException("already in a world; quit to the main menu first");
            string file = a.Str("file");
            SaveGameMetadata info = new SaveGameMetadata();
            info.mSaveFile = file;
            if (!Sims3.Gameplay.UI.Responder.Instance.MainMenuModel.GetSaveFileDetails(ref info))
                throw new McpException("unknown save '" + file + "'");
            RunAsync("load_save " + file, delegate { GameStates.TransitionToGameStateToInWorld(file, info, InWorldState.SubState.LiveMode, true); });
            Dictionary<string, object> r = Util.Obj();
            r["loading"] = file;
            return r;
        }

        // Households that live on a lot, so a fresh world can start with a premade family.
        static object WorldHouseholds(Args a)
        {
            List<object> list = new List<object>();
            foreach (Household h in Household.sHouseholdList)
            {
                if (h == null || h.LotHome == null || h.IsServiceNpcHousehold || h.IsSpecialHousehold) continue;
                Dictionary<string, object> o = Util.Obj();
                o["name"] = h.Name;
                o["id"] = Util.Id(h.HouseholdId);
                o["members"] = h.NumMembers;
                o["lot"] = h.LotHome.Name;
                list.Add(o);
            }
            return list;
        }

        // Makes a household the active one and switches to live mode, as PlayFlowModel does after "Play".
        static object PlayHousehold(Args a)
        {
            string name = a.Str("name");
            Household target = null;
            foreach (Household h in Household.sHouseholdList)
                if (h != null && h.LotHome != null && string.Equals(h.Name, name, StringComparison.OrdinalIgnoreCase)) { target = h; break; }
            if (target == null) throw new McpException("no household on a lot named '" + name + "'");
            RunAsync("play_household " + name, delegate
            {
                PlumbBob.ForceSelectActor(target.GetRandomNonRoommateSim());
                target.LotHome.EnsureLotObjects();
                // PlayFlowModel.GotoLiveMode is private; call it when the play flow is up, else switch directly.
                MethodInfo m = typeof(PlayFlowModel).GetMethod("GotoLiveMode", BindingFlags.NonPublic | BindingFlags.Instance);
                if (PlayFlowModel.Singleton != null && m != null) m.Invoke(PlayFlowModel.Singleton, null);
                else GameStates.TransitionToLiveMode();
            });
            Dictionary<string, object> r = Util.Obj();
            r["playing"] = target.Name;
            return r;
        }

        static object QuitGame(Args a)
        {
            RunAsync("quit_game", delegate { GameStates.TransitionToGameStateQuitNoCheck(); });
            Dictionary<string, object> r = Util.Obj();
            r["quitting"] = true;
            return r;
        }
    }
}
