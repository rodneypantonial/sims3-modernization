using System;
using System.Collections.Generic;
using Sims3.SimIFace;
using GF = Sims3.Gameplay.Gameflow;
using GameSpeed = Sims3.SimIFace.Gameflow.GameSpeed;

namespace Ts3Agent
{
    // Test-harness autopilot, in-game so it reacts within a frame instead of the harness's 30 s loop:
    // holds the game at the target speed (the game drops to normal on events) and clears popups as they open.
    // Popups pause or lock the speed, so GameSpeedChanged triggers an immediate dialog check; a 250 ms poll
    // catches the rest. Live mode only; the harness turns it off around saves (a save pauses on purpose).
    public static partial class Handlers
    {
        // Settings and generation live in AppDomain data so they survive a hot reload; an old plugin's task
        // and event handler see a newer generation and stop.
        const string kApCfg = "ts3agent.autopilot.cfg", kApGen = "ts3agent.autopilot.gen";
        static int sApGen;
        static volatile bool sApPoke;
        static int sApSpeedFixes;
        static readonly List<object> sApCleared = new List<object>();

        static void StartAutopilot()
        {
            object g = AppDomain.CurrentDomain.GetData(kApGen);
            sApGen = (g is int ? (int)g : 0) + 1;
            AppDomain.CurrentDomain.SetData(kApGen, sApGen);
            int myGen = sApGen;
            GF.GameSpeedChanged += delegate (GameSpeed s, bool locked)
            {
                if (myGen == sApGen && (s == GameSpeed.Pause || locked)) sApPoke = true;
            };
            Simulator.AddObject(new AutopilotTask(myGen));
        }

        static Dictionary<string, object> ApCfg()
        {
            Dictionary<string, object> c = AppDomain.CurrentDomain.GetData(kApCfg) as Dictionary<string, object>;
            if (c == null)
            {
                c = new Dictionary<string, object>();
                c["enabled"] = false; c["speed"] = "ultra"; c["dialogs"] = true;
                AppDomain.CurrentDomain.SetData(kApCfg, c);
            }
            return c;
        }

        // autopilot {enabled?, speed?: normal|fast|ultra, dialogs?}: updates settings, returns them plus what it
        // did since the last call (speed corrections and cleared dialogs).
        static object Autopilot(Args a)
        {
            Dictionary<string, object> c = ApCfg();
            if (a.Has("enabled")) c["enabled"] = a.Bool("enabled", false);
            if (a.Has("speed")) c["speed"] = a.Str("speed");
            if (a.Has("dialogs")) c["dialogs"] = a.Bool("dialogs", true);
            Dictionary<string, object> r = Util.Obj();
            foreach (KeyValuePair<string, object> kv in c) r[kv.Key] = kv.Value;
            r["speed_fixes"] = sApSpeedFixes;
            r["cleared"] = new List<object>(sApCleared);
            sApSpeedFixes = 0;
            sApCleared.Clear();
            return r;
        }

        static GameSpeed ApTarget(string s)
        {
            switch (s) { case "normal": return GameSpeed.Normal; case "fast": return GameSpeed.Double; default: return GameSpeed.Triple; }
        }

        static void AutopilotStep(ref int lastDialog, ref int lastSpeed)
        {
            Dictionary<string, object> c = ApCfg();
            if (!(bool)c["enabled"] || SubState() != "LiveModeState") return;
            int now = System.Environment.TickCount;
            if ((bool)c["dialogs"] && (sApPoke || now - lastDialog >= 250))
            {
                sApPoke = false;
                lastDialog = now;
                if (Sims3.UI.UIManager.GetModalWindow() != null)
                {
                    Dictionary<string, object> d = AutoDialog(new Args(new Dictionary<string, object>())) as Dictionary<string, object>;
                    if (d != null && sApCleared.Count < 200) sApCleared.Add(d["title"] + " -> " + d["clicked"]);
                    return; // let the dialog close before touching the speed
                }
            }
            if (now - lastSpeed < 1000) return;
            lastSpeed = now;
            GameSpeed target = ApTarget((string)c["speed"]);
            if (!GF.GameSpeedLocked && GF.CurrentGameSpeed != target)
            {
                GF.SetGameSpeed(target, GF.SetGameSpeedContext.User);
                sApSpeedFixes++;
            }
        }

        class AutopilotTask : Task
        {
            readonly int mGen;
            public AutopilotTask(int gen) { mGen = gen; }

            public override void Simulate()
            {
                int lastDialog = 0, lastSpeed = 0;
                while (mGen == sApGen && object.Equals(AppDomain.CurrentDomain.GetData(kApGen), mGen))
                {
                    try { AutopilotStep(ref lastDialog, ref lastSpeed); }
                    catch (Exception) { }
                    Simulator.Sleep(0);
                }
                Simulator.DestroyObject(ObjectId);
            }
        }
    }
}
