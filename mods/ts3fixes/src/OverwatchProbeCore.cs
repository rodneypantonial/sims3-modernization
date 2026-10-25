using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent part of switch "owprobeplans" (OverwatchProbe.cs, docs/163 §9): which (Sim, task) pairs are inside
    // EA's stuck check right now. Keyed by task too because the check's plans can sleep (async plans, our drain task):
    // another task's plan for the same Sim in that time is the Sim's own and still counts for Overwatch.
    // Enter/Exit pair through try/finally at the call site; nesting and two checks at once (alarm path + drain) stack.
    public sealed class OverwatchProbeCore
    {
        readonly List<ulong> mSim = new List<ulong>(), mTask = new List<ulong>();

        public int Depth { get { return mSim.Count; } }

        public void Enter(ulong sim, ulong task) { mSim.Add(sim); mTask.Add(task); }

        public void Exit(ulong sim, ulong task)
        {
            for (int i = mSim.Count - 1; i >= 0; i--)
                if (mSim[i] == sim && mTask[i] == task) { mSim.RemoveAt(i); mTask.RemoveAt(i); return; }
        }

        public bool Active(ulong sim, ulong task)
        {
            for (int i = mSim.Count - 1; i >= 0; i--)
                if (mSim[i] == sim && mTask[i] == task) return true;
            return false;
        }

        // world load: a task killed by the world unload never ran its finally
        public void Clear() { mSim.Clear(); mTask.Clear(); }
    }
}
