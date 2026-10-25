using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.UI;
using Sims3.SimIFace;
using Sims3.UI;
using GF = Sims3.Gameplay.Gameflow;
using GameSpeed = Sims3.SimIFace.Gameflow.GameSpeed;
using Environment = System.Environment;
using Responder = Sims3.Gameplay.UI.Responder;
using OneShotFunctionTask = Sims3.Gameplay.OneShotFunctionTask;

namespace Ts3Agent
{
    // NRaas Saver check (docs/25 §8, player report 2026-10-24 "save is stuck" with v0.1.5 + NRaas Saver).
    // saver_like_save {name?, mapView?: false, pause?: false, restoreMapView?: true, cycles?: 4, timeoutS?: 120}: what Saver V22's
    //   Save() + OnSave() do (Chain-Reaction/NRaas NRaasSaver/Saver.cs), from game tasks, not this command's context: task 1 (as
    //   Saver's AutoSave task / alarm) toggles map view + Sleep(0) when asked and not already on, pauses when asked; task 2 (as
    //   Common.FunctionTask.Perform(OnSave)) renames the save the way Saver does (trailing digits = cycle count, +1, wraps after
    //   `cycles`) and calls OptionsModel.SaveGame(true, true, saveAs) inline, with the game running. Replies at once; the result
    //   (call ms, return value, file) is in save_watch. Deviations: refuses an empty save name (Saver would then save under the
    //   world's caption) and Baseline-/Story-/ApexTest names; restoreMapView (default on) leaves map view after the save so every
    //   firing toggles it again (Saver leaves it on).
    // save_watch {reset?, maxS?: 120}: a game task watches OptionsModel.SaveGameInProgress every tick and records each save
    //   (start/end ms, longest tick gap = the native write, modal window seen, file name after, ts3fixes asyncroute_started [56]
    //   in the 5 s before and during). Fails (ok:false) when a save took longer than maxS, one is still open after maxS, or a
    //   saver_like_save call returned false or threw. new_saves = Saver-made saves (SaverCount.IsSaverMade: a game save none of
    //   our commands started that ended on a new file) since the previous save_watch call, saver_saved = new_saves > 0 (for
    //   stepsRequire), new_records = every save recorded since then; saver_version = NRaas Saver's VersionStamp.sVersion;
    //   saver = Saver's timer state (SaverState). saverArm: true fails unless Saver's real-time AutoSave task is armed and its
    //   IsValidState() holds, with the reasons (use it right after reflect_call NRaas.Saver.RestartTimers).
    public static partial class Handlers
    {
        static void RegisterSaver()
        {
            Dispatcher.Register("saver_like_save", SaverLikeSave);
            Dispatcher.Register("save_watch", SaveWatch);
        }

        static readonly List<Dictionary<string, object>> sSaves = new List<Dictionary<string, object>>();
        static Dictionary<string, object> sOpenSave, sLikeSave;
        static int sWatchGen, sSavesReported, sWatchT0;
        static bool sWatchOn, sAgentSavePending;
        static int sAgentSaveAt; // save_game marks its save so the watch labels it "agent", not a Saver save
        static readonly int[] sAsyncRing = new int[6]; // asyncroute_started once a second, newest at sAsyncAt

        static int AsyncStarted()
        {
            int[] c = AppDomain.CurrentDomain.GetData("ts3fixes.counters") as int[];
            return c != null && c.Length > 56 ? c[56] : -1;
        }

        static int WatchMs() { return Environment.TickCount - sWatchT0; }

        static Dictionary<string, object> NewSaveRec(string via)
        {
            Dictionary<string, object> r = Util.Obj();
            int now = AsyncStarted();
            r["via"] = via;
            r["t0_ms"] = WatchMs();
            r["async_at_start"] = now;
            r["async_5s_before"] = now - sAsyncRing[0]; // oldest sample = ~5 s ago: > 0 means route jobs were starting
            r["max_gap_ms"] = 0;
            r["modal"] = false;
            r["file_before"] = GameStates.LoadFileName;
            return r;
        }

        static void CloseSaveRec(Dictionary<string, object> r)
        {
            int t1 = WatchMs();
            r["t1_ms"] = t1;
            r["ms"] = t1 - (int)r["t0_ms"];
            r["file"] = GameStates.LoadFileName;
            r["save_name"] = Responder.Instance.OptionsModel.SaveName;
            r["async_during"] = AsyncStarted() - (int)r["async_at_start"];
            r["saver_made"] = SaverCount.IsSaverMade((string)r["via"], r["file_before"] as string, r["file"] as string);
            sSaves.Add(r);
        }

        static void EnsureSaveWatch()
        {
            if (sWatchOn) return;
            sWatchOn = true;
            int gen = ++sWatchGen;
            sWatchT0 = Environment.TickCount;
            Simulator.AddObject(new OneShotFunctionTask(delegate
            {
                int last = Environment.TickCount, lastSample = 0, at = 0;
                try
                {
                    while (gen == sWatchGen && GameStates.IsInWorld())
                    {
                        int now = Environment.TickCount, gap = now - last;
                        last = now;
                        if (now - lastSample >= 1000)
                        {
                            lastSample = now;
                            // shift: [0] = oldest (~5 s ago)
                            for (int i = 0; i < sAsyncRing.Length - 1; i++) sAsyncRing[i] = sAsyncRing[i + 1];
                            sAsyncRing[sAsyncRing.Length - 1] = AsyncStarted();
                            at++;
                        }
                        bool busy = Responder.Instance.OptionsModel.SaveGameInProgress;
                        Dictionary<string, object> open = sLikeSave ?? sOpenSave;
                        if (open != null)
                        {
                            if (gap > (int)open["max_gap_ms"]) open["max_gap_ms"] = gap;
                            if (UIManager.GetModalWindow() != null) open["modal"] = true;
                        }
                        if (busy && sOpenSave == null && sLikeSave == null)
                        {
                            bool agent = sAgentSavePending && now - sAgentSaveAt < 10000;
                            sAgentSavePending = false;
                            sOpenSave = NewSaveRec(agent ? "agent" : "watch");
                        }
                        else if (!busy && sOpenSave != null) { CloseSaveRec(sOpenSave); sOpenSave = null; }
                        Simulator.Sleep(0);
                    }
                }
                catch (ResetException) { throw; }
                catch (Exception) { }
                finally { if (gen == sWatchGen) sWatchOn = false; }
            }));
        }

        static readonly string[] kGuardedSaves = { "baseline-", "story-", "apextest" };

        static object SaverLikeSave(Args a)
        {
            IOptionsModel om = Responder.Instance.OptionsModel;
            if (sLikeSave != null)
                throw new McpException("previous saver_like_save still running after " + (WatchMs() - (int)sLikeSave["t0_ms"]) / 1000 + " s");
            if (om.SaveGameInProgress) throw new McpException("a save is already in progress");
            if (a.Has("name")) om.SaveName = a.Str("name");
            string cur = om.SaveName;
            if (string.IsNullOrEmpty(cur)) throw new McpException("no save name; pass name (Saver would save under the world's caption)");
            foreach (string g in kGuardedSaves)
                if (cur.ToLowerInvariant().StartsWith(g)) throw new McpException("refusing to save over a guarded save name: " + cur);
            bool mapView = a.Bool("mapView", false), pause = a.Bool("pause", false), restore = a.Bool("restoreMapView", true);
            int cycles = (int)a.Long("cycles", 4);
            EnsureSaveWatch();
            Dictionary<string, object> rec = NewSaveRec("saver_like");
            rec["map_view"] = mapView;
            rec["pause"] = pause;
            sLikeSave = rec;
            // task 1: Saver.Save() (runs on Saver's AutoSave task or alarm)
            Simulator.AddObject(new OneShotFunctionTask(delegate
            {
                bool toggled = false;
                try
                {
                    if (SubState() == "LiveModeState")
                    {
                        if (mapView && !CameraController.IsMapViewModeEnabled())
                        {
                            Sims3.Gameplay.Core.Camera.ToggleMapView();
                            toggled = true;
                            Simulator.Sleep(0);
                        }
                        if (pause && GF.CurrentGameSpeed != GameSpeed.Pause) GF.SetGameSpeed(GameSpeed.Pause, GF.SetGameSpeedContext.User);
                    }
                    rec["toggled_map_view"] = toggled;
                    rec["queued_ms"] = WatchMs() - (int)rec["t0_ms"];
                }
                catch (Exception e) { rec["error"] = DescribeError(e); }
                // task 2: Common.FunctionTask.Perform(OnSave)
                Simulator.AddObject(new OneShotFunctionTask(delegate
                {
                    try
                    {
                        rec["name"] = SaverNextName(om, cycles, rec);
                        int c0 = Environment.TickCount;
                        rec["returned"] = om.SaveGame(true, true, (bool)rec["save_as"]);
                        rec["call_ms"] = Environment.TickCount - c0;
                        if (toggled && restore && CameraController.IsMapViewModeEnabled()) Sims3.Gameplay.Core.Camera.ToggleMapView();
                    }
                    catch (ResetException) { throw; }
                    catch (Exception e) { rec["error"] = DescribeError(e); }
                    finally { CloseSaveRec(rec); sLikeSave = null; }
                }));
            }));
            Dictionary<string, object> r = Util.Obj();
            r["started"] = cur;
            r["map_view"] = mapView;
            r["pause"] = pause;
            r["note"] = "runs in game tasks; save_watch reports the result";
            return r;
        }

        // Saver.OnSave's name handling: strip ".sims3", trailing digits = count, count + 1 (1 after `cycles`), base + count
        static string SaverNextName(IOptionsModel om, int cycles, Dictionary<string, object> rec)
        {
            string saveName = om.SaveName;
            if (!string.IsNullOrEmpty(saveName)) saveName = saveName.Replace(".sims3", "");
            int count = 0;
            if (!string.IsNullOrEmpty(saveName))
            {
                int i;
                for (i = saveName.Length - 1; i >= 0; i--)
                {
                    int test;
                    if (!int.TryParse(saveName[i].ToString(), out test)) break;
                }
                int.TryParse(saveName.Substring(i + 1), out count);
                saveName = saveName.Substring(0, i + 1);
            }
            count++;
            if (count > cycles) count = 1;
            rec["save_as"] = !string.IsNullOrEmpty(saveName);
            if (!string.IsNullOrEmpty(saveName)) om.SaveName = saveName + count.ToString();
            return om.SaveName;
        }

        static object SaveWatch(Args a)
        {
            if (a.Bool("reset", false)) { sSaves.Clear(); sSavesReported = 0; }
            EnsureSaveWatch();
            int maxMs = (int)(a.Double("maxS", 120) * 1000);
            Dictionary<string, object> r = Util.Obj();
            r["saves"] = new List<Dictionary<string, object>>(sSaves);
            int newSaver = SaverCount.SaverMadeSince(sSaves, sSavesReported);
            r["new_saves"] = newSaver;
            r["saver_saved"] = newSaver > 0;
            r["new_records"] = sSaves.Count - Math.Min(sSavesReported, sSaves.Count);
            sSavesReported = sSaves.Count;
            r["save_in_progress"] = Responder.Instance.OptionsModel.SaveGameInProgress;
            r["saver_version"] = null; // every NRaas mod has its own NRaas.VersionStamp: read the one next to NRaas.Saver
            Type saver = SaverType();
            Type vs = saver != null ? saver.Assembly.GetType("NRaas.VersionStamp") : null;
            System.Reflection.FieldInfo vf = vs != null ? vs.GetField("sVersion") : null;
            if (vf != null) r["saver_version"] = vf.GetValue(null);
            List<string> why = new List<string>();
            if (saver != null) r["saver"] = SaverState(saver, why);
            if (a.Bool("saverArm", false))
            {
                if (saver == null) why.Add("NRaas.Saver not installed");
                if (why.Count > 0) throw new McpException("save_watch saverArm: " + string.Join("; ", why.ToArray()));
                r["saver_armed"] = true;
            }
            List<string> fails = new List<string>();
            foreach (Dictionary<string, object> s in sSaves)
            {
                string who = s["via"] + " " + (s.ContainsKey("name") ? s["name"] : s["save_name"]);
                if ((int)s["ms"] > maxMs) fails.Add(who + " took " + (int)s["ms"] / 1000 + " s");
                if (s.ContainsKey("error")) fails.Add(who + " threw " + s["error"]);
                if (s.ContainsKey("returned") && !(bool)s["returned"]) fails.Add(who + " SaveGame returned false");
            }
            Dictionary<string, object> open = sLikeSave ?? sOpenSave;
            if (open != null)
            {
                int age = WatchMs() - (int)open["t0_ms"];
                r["open"] = open;
                r["open_ms"] = age;
                if (age > maxMs) fails.Add(open["via"] + " save still in progress after " + age / 1000 + " s (modal " + open["modal"] + ")");
            }
            if (fails.Count > 0) throw new McpException("save_watch: " + string.Join("; ", fails.ToArray()) + " (" + sSaves.Count + " saves recorded)");
            return r;
        }

        static Type SaverType()
        {
            foreach (System.Reflection.Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type t = asm.GetType("NRaas.Saver");
                if (t != null) return t;
            }
            return null;
        }

        const System.Reflection.BindingFlags kStatic = System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic;
        const System.Reflection.BindingFlags kInst = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic;

        static object Field(Type t, object o, string name)
        {
            System.Reflection.FieldInfo f = t.GetField(name, o == null ? kStatic : kInst);
            return f != null ? f.GetValue(o) : null;
        }

        // NRaas Saver V22's real-time timer (NRaasSaver/Saver.cs: sTimer = AutoSave task, armed by RestartTimers, stopped by
        // OnPreSave in live mode, re-armed by OnPostSave) and why AutoSave.IsValidState() would hold a save back. Adds every
        // reason the timer cannot save to `why`.
        static Dictionary<string, object> SaverState(Type saver, List<string> why)
        {
            Dictionary<string, object> s = Util.Obj();
            try
            {
                object settings = saver.GetProperty("Settings", kStatic).GetValue(null, null);
                string style = settings.GetType().GetProperty("SaveStyle").GetValue(settings, null).ToString();
                s["save_style"] = style;
                s["real_minutes"] = Field(settings.GetType(), settings, "mRealMinutesBetweenSaves");
                if (style != "RealTime") why.Add("SaveStyle " + style + " (real-time timer not used)");
                s["count"] = Field(saver, null, "sCount");
                s["save_requested"] = Field(saver, null, "sSaveWasRequested");
                s["save_performed"] = Field(saver, null, "sSaveWasPerformed");
                object timer = Field(saver, null, "sTimer");
                s["timer_armed"] = timer != null;
                if (timer == null) why.Add("sTimer is null (no AutoSave task)");
                else
                {
                    Type at = timer.GetType();
                    bool exit = (bool)Field(at, timer, "mExit");
                    s["timer_exit"] = exit;
                    s["interval_min"] = Field(at, timer, "mSaveInterval");
                    object sw = Field(at, timer, "mTimer");
                    s["stopwatch"] = sw != null; // null until the task's first Simulate tick
                    if (sw != null) s["elapsed_min"] = sw.GetType().GetMethod("GetElapsedTime").Invoke(sw, null);
                    if (exit) why.Add("AutoSave task stopping (mExit)");
                }
                Type autoSave = saver.GetNestedType("AutoSave", kStatic);
                System.Reflection.MethodInfo valid = autoSave != null ? autoSave.GetMethod("IsValidState", kStatic) : null;
                bool ok = valid != null && (bool)valid.Invoke(null, null);
                s["valid_state"] = ok;
                if (!ok)
                {
                    List<string> v = new List<string>();
                    if (valid == null) v.Add("IsValidState not found");
                    WindowBase modal = UIManager.GetModalWindow();
                    if (modal != null) v.Add("modal window " + modal.GetType().Name);
                    if (Sims3.SimIFace.Gameflow.SaveIsDisabled) v.Add("saving disabled (Gameflow.SaveIsDisabled)");
                    string sub = SubState();
                    if (sub != "LiveModeState") v.Add("sub state " + sub + " (build/buy only with mPromptInBuildBuy)");
                    if (v.Count == 0) v.Add("video recording running");
                    s["invalid_because"] = string.Join("; ", v.ToArray());
                    why.Add("IsValidState() false: " + s["invalid_because"]);
                }
            }
            catch (Exception e) { s["error"] = DescribeError(e); why.Add("Saver state unreadable: " + s["error"]); }
            return s;
        }
    }
}
