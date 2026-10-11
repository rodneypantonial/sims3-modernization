using System;
using System.Collections.Generic;
using System.Reflection;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
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
        }

        static object GameState(Args a)
        {
            Dictionary<string, object> r = Util.Obj();
            r["in_world"] = GameStates.IsInWorld();
            r["shutting_down"] = GameStates.IsGameShuttingDown;
            Household active = Household.ActiveHousehold;
            r["active_household"] = active != null ? active.Name : null;
            r["households"] = Household.sHouseholdList != null ? Household.sHouseholdList.Count : 0;
            r["save_name"] = Sims3.Gameplay.UI.Responder.Instance.OptionsModel.SaveName;
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
