using System;

namespace Ts3Fixes
{
    // Which candidate LotManager.GetFarthestLot(lot, actor, true) picks, without game types, so tools/selftest/farthestpick
    // checks it offline (CandidateLoops.cs, switch farthestsorted, docs/141 §7.2).
    public delegate bool IndexTest(int i);

    public static class FarthestPick
    {
        // The game's loop, as a reference: lots in list order, a running maximum of the squared distance, and a route plan
        // only for a lot strictly farther than the best accepted so far. Result: the farthest lot the plan accepts, ties to
        // the first in list order; -1 when none. `plans` = plans the game runs.
        public static int Game(float[] d2, IndexTest routable, out int plans)
        {
            int best = -1;
            float num = float.MinValue;
            plans = 0;
            for (int i = 0; i < d2.Length; i++)
            {
                if (!(d2[i] > num)) continue;
                plans++;
                if (!routable(i)) continue;
                best = i;
                num = d2[i];
            }
            return best;
        }

        // Same answer with fewer plans: farthest first (ties by list order), the first lot the plan accepts wins. Plans run =
        // 1 + the unroutable lots farther than the answer, a subset of the game's plans (each of those beats the game's
        // running maximum when it meets it). After `cap` failed plans the next lot in that order is returned unplanned
        // (`capped`), as the game's own no-route overload GetFarthestLot(lot) would pick among the rest.
        public static int Sorted(float[] d2, IndexTest routable, int cap, out int plans, out bool capped)
        {
            int n = d2.Length;
            int[] order = new int[n];
            for (int i = 0; i < n; i++) order[i] = i;
            Array.Sort(order, delegate(int a, int b) { return d2[a] != d2[b] ? d2[b].CompareTo(d2[a]) : a.CompareTo(b); });
            plans = 0;
            capped = false;
            foreach (int i in order)
            {
                if (plans >= cap) { capped = true; return i; }
                plans++;
                if (routable(i)) return i;
            }
            return -1;
        }
    }
}
