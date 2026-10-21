using System;
using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent parts of the unseen script throttle (UnseenThrottle.cs, docs/144), kept free of game types so the
    // offline self-test (tools/selftest/unseen) compiles them alone.

    // "Run one call in n" per key (a Sim or a broadcaster) while the key is unseen. A key's first unseen call is staggered
    // by the key itself, so Sims that turn unseen together (a world load, a camera jump) do not all run on the same call.
    // Seen(key) forgets the key: a seen caller always runs, and its next unseen stretch starts staggered again.
    public sealed class EveryN
    {
        public const int kMaxKeys = 4096; // destroyed Sims and broadcasters leave keys behind; a full table starts over
        readonly Dictionary<int, int> mLeft = new Dictionary<int, int>();
        public int Clears;

        public int Count { get { return mLeft.Count; } }

        // True = run this call. n <= 1 always runs.
        public bool Unseen(int key, int n)
        {
            if (n <= 1) return true;
            int left;
            if (!mLeft.TryGetValue(key, out left))
            {
                if (mLeft.Count >= kMaxKeys) { mLeft.Clear(); Clears++; }
                left = (key & 0x7FFFFFFF) % n;
            }
            if (left <= 0) { mLeft[key] = n - 1; return true; }
            mLeft[key] = left - 1;
            return false;
        }

        public void Seen(int key) { mLeft.Remove(key); }
        public void Clear() { mLeft.Clear(); }
    }

    public static class UnseenMath
    {
        // Motive updates (Autonomy.Update) integrate the sim-minutes since the Sim's last update. An unseen Sim's update
        // runs only once that gap reaches n base steps, so one update integrates the whole gap (n x dt). The gap stays
        // under capMin: Motive.Update switches a high-LOD Sim to the time-of-day auto-satisfy curve past
        // kMaxUpdateHoursBeforeAutoSatisfy (1 sim-hour), which would change the result, not just delay it.
        public static bool Due(float elapsedMin, int n, float baseMin, float capMin)
        {
            if (n <= 1) return true;
            return elapsedMin >= Math.Min(n * baseMin, capMin);
        }

        // Stable small key for an object id (ulong) or a reference hash.
        public static int Key(ulong id) { return unchecked((int)(id ^ (id >> 32))); }
    }
}
