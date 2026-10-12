using System;
using System.Collections.Generic;
using Sims3.SimIFace;
using Sims3.UI;

namespace Ts3Agent
{
    // Standard Sims 3 script-mod entry point: the game parses the _XML tuning
    // resource whose instance id is FNV64("Ts3Agent.Instantiator"), which
    // touches this [Tunable] field and runs the static constructor.
    public class Instantiator
    {
        [Tunable] protected static bool kInstantiator = false;

        public const string ModVersion = "0.1.0";

        static Instantiator()
        {
            World.OnStartupAppEventHandler += OnStartupApp;
            World.OnWorldLoadFinishedEventHandler += OnWorldLoadFinished;
            World.OnWorldQuitEventHandler += OnWorldQuit;
            PluginHost.Init(); // registers the commands from the plugin assembly (hot-reloadable)
            // Open right away so the server can see the mod is loaded even
            // before a world is (state stays 0 until a world finishes loading).
            Mailbox.Open();
            // Tuning may load after the startup event already fired; start serving now as well.
            try { PollTask.Start(false); } catch (Exception) { }
        }

        // Harness change: poll from app startup too, so tools can drive the main menu (load/create saves).
        static void OnStartupApp(object sender, EventArgs e)
        {
            Mailbox.Open();
            PollTask.Start(false);
        }

        static void OnWorldLoadFinished(object sender, EventArgs e)
        {
            // keep the same buffer: a new one makes the probe rescan all memory (~18 s per pass) right after a load
            Mailbox.Open();
            Mailbox.SetWorldLoaded(true);
            PollTask.Start(true);
        }

        static void OnWorldQuit(object sender, EventArgs e)
        {
            Mailbox.SetWorldLoaded(false);
            PollTask.Start(false); // back at the main menu: keep serving
        }
    }

    // Simulator task that services the mailbox every tick on the game thread.
    public class PollTask : Task
    {
        static PollTask sInstance;
        bool mRunning = true;

        bool mAnnounce;

        public static void Start(bool announce)
        {
            Halt();
            sInstance = new PollTask();
            sInstance.mAnnounce = announce; // notifications need the in-world HUD
            Simulator.AddObject(sInstance);
        }

        public static void Halt()
        {
            if (sInstance != null)
            {
                sInstance.mRunning = false;
                sInstance = null;
            }
        }

        public override void Simulate()
        {
            bool announced = false;
            int ticks = 0;
            while (mRunning)
            {
                try { Mailbox.Poll(); }
                catch (Exception) { }
                if (mAnnounce && !announced && ++ticks > 300)  // let the HUD finish loading first
                {
                    announced = true;
                    try
                    {
                        StyledNotification.Show(new StyledNotification.Format(
                            "ts3agent " + Instantiator.ModVersion + " connected", StyledNotification.NotificationStyle.kSystemMessage));
                    }
                    catch (Exception) { }
                }
                Simulator.Sleep(0);
            }
            Simulator.DestroyObject(ObjectId);
        }
    }
}
