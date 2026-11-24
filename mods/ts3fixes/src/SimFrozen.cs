using System;
using Sims3.Gameplay;
using Sims3.Gameplay.Utilities;

namespace Ts3Fixes
{
    // Rodney 2026-11-23: nothing of ours does sim-state work while the game is paused, in CAS, Buy/Build, Edit Town or menus.
    // Per-frame tasks (LoopTask + Simulator.Sleep(0)) keep running in all of these: ScriptTick and TaskObjects run while the
    // sim runs 0 steps (docs/85 I-85-2), so each such task asks this before acting. True = do not act this frame.
    // [verified, decompiled GameStates.cs] IsLiveState = in-world substate LiveMode, so Buy, Build, CAS and Edit Town are not live.
    // A minimized window stops the clock through GameClock pause bit 2 without touching CurrentGameSpeed (docs/85 I-85-1,
    // clock-minimize-pause PASS 2026-10-17; docs/162 §7): the clock check covers it. Offline part: SimFrozenCore.
    public static partial class Fixes
    {
        static long sSfTicks;
        static int sSfMovedMs;

        public static bool SimFrozenOrNotLive()
        {
            if (SimFrozenCore.FrozenOrNotLive(GameStates.IsLiveState,
                    GameStates.IsTravelling || GameStates.IsGameShuttingDown || GameStates.IsWorldShuttingDown,
                    Gameflow.CurrentGameSpeed == Sims3.SimIFace.Gameflow.GameSpeed.Pause, false)) return true;
            return SimFrozenCore.ClockStill(SimClock.CurrentTicks, Environment.TickCount, ref sSfTicks, ref sSfMovedMs);
        }
    }
}
