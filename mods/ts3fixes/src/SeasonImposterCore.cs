using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent part of seasonimposter (SeasonImposter.cs, issue #82): the lot queue and which lot goes next.
    // Self-check: tools/selftest/seasonimposter.
    public sealed class SiQueue
    {
        public delegate bool WarmTest(ulong lotId);         // also true for a lot that is gone: dropped at its turn
        public delegate float DistanceTo(ulong lotId);
        readonly List<ulong> mIds = new List<ulong>();

        public int Count { get { return mIds.Count; } }
        public ulong this[int i] { get { return mIds[i]; } }

        // Queued once: a second season change before the rebuild needs no second rebuild.
        public bool Add(ulong id) { if (mIds.Contains(id)) return false; mIds.Add(id); return true; }
        public void RemoveAt(int i) { mIds.RemoveAt(i); }
        public int Clear() { int n = mIds.Count; mIds.Clear(); return n; }

        // A warm lot first (queue order); else, cold allowed, the nearest; -1 when only cold lots wait and cold is off.
        public int Pick(WarmTest warm, DistanceTo dist, bool cold)
        {
            int best = -1;
            float bestDist = float.MaxValue;
            for (int i = 0; i < mIds.Count; i++)
            {
                if (warm(mIds[i])) return i;
                if (!cold) continue;
                float d = dist(mIds[i]);
                if (d < bestDist) { bestDist = d; best = i; }
            }
            return best;
        }

        public string Join() { string s = ""; foreach (ulong v in mIds) s += (s.Length > 0 ? "," : "") + v; return s; }
    }
}
