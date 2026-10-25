namespace Ts3Fixes
{
    // Game-independent parts of the docs/163 fixes (stuckchecknear in FewerPlans.cs, failrepeat in RouteBackoff.cs),
    // checked offline by tools/selftest/nostart.
    public static class NoStartCore
    {
        // stuckchecknear: plan order for the stuck check's candidate points, nearest first (squared distances d2[0..n-1]);
        // ties keep the game's draw order, so equal distances plan in the order the game would have drawn them.
        public static int[] NearestFirst(float[] d2, int n)
        {
            int[] order = new int[n];
            for (int i = 0; i < n; i++) order[i] = i;
            for (int i = 1; i < n; i++) // insertion sort: stable, n <= Sim.kNumberOfPlanAttemptsForStuckSim (10)
            {
                int v = order[i], j = i - 1;
                while (j >= 0 && d2[order[j]] > d2[v]) { order[j + 1] = order[j]; j--; }
                order[j + 1] = v;
            }
            return order;
        }

        // stuckcheckcap (FewerPlans.cs StuckCheckRun, docs/163 §10): a check's outcome counter slot from its plans, successful
        // plans and round: 298 no plan, 299 not stuck, 300 all failed before the last round (re-arm), 301 last round (reset)
        public static int StuckOutcome(int plans, int ok, bool last)
        {
            return plans == 0 ? 298 : ok > 0 ? 299 : last ? 301 : 300;
        }

        // stuckcheckcap: probes may be cut only before the last round (attempt < 3), so a reset always follows full searches
        public static bool StuckMayCut(uint attempt) { return attempt < 3; }

        // docs/163 §10.1: low word of the tagged PROBE mark (ts3native ScProbeMark): bit 16 = tagged form, bits 0-7 round
        // (mAttempt + 1, 0 = unknown), bit 8 = cut (stuckcheckcap), bit 9 = last round
        public static ulong StuckTag(int round, bool cut, bool last)
        {
            return 0x10000UL | (cut ? 0x100UL : 0UL) | (last ? 0x200UL : 0UL) | (uint)round & 0xFFUL;
        }

        // failrepeat: whether a failure that ended at thenTick (Environment.TickCount) is still within windowMs at nowTick;
        // TickCount wraps every ~49.7 days, the unchecked difference handles that, a clock that went back answers false.
        public static bool Within(int nowTick, int thenTick, int windowMs)
        {
            int dt = unchecked(nowTick - thenTick);
            return dt >= 0 && dt <= windowMs;
        }
    }
}
