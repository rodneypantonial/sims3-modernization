// Offline self-check of mods/ts3fixes/src/NoStartCore.cs (switches stuckchecknear and failrepeat, docs/163).
using System;
using Ts3Fixes;

static class NoStartTest
{
    static int fails;
    static void Check(bool ok, string what) { if (!ok) { fails++; Console.WriteLine("FAIL " + what); } }

    static void Main()
    {
        // NearestFirst: ascending, stable on ties, a permutation of 0..n-1
        int[] o = NoStartCore.NearestFirst(new float[] { 4f, 1f, 4f, 0f, 1f }, 5);
        Check(string.Join(",", Array.ConvertAll(o, x => x.ToString())) == "3,1,4,0,2", "order with ties");
        Check(NoStartCore.NearestFirst(new float[0], 0).Length == 0, "empty");
        Check(NoStartCore.NearestFirst(new float[] { 9f, 9f }, 1).Length == 1, "n below array length");

        // Stuck check model: 10 draws (some invalid), reachability per point. The game plans in draw order and stops at the
        // first success; ours plans nearest first. Verdict (any point routes) must be equal every time.
        Random rng = new Random(160);
        long gameFailed = 0, oursFailed = 0, gamePlans = 0, oursPlans = 0;
        for (int run = 0; run < 100000; run++)
        {
            int n = rng.Next(0, 11);
            float[] d2 = new float[n];
            bool[] ok = new bool[n];
            float far = (float)rng.NextDouble();
            for (int i = 0; i < n; i++)
            {
                float d = (float)rng.NextDouble() * 40f;
                d2[i] = d * d;
                ok[i] = rng.NextDouble() > far * d / 40f; // farther points fail more often (inferred shape, any shape keeps the verdict)
            }
            bool game = false;
            for (int i = 0; i < n; i++) { gamePlans++; if (ok[i]) { game = true; break; } gameFailed++; }
            bool ours = false;
            int[] ord = NoStartCore.NearestFirst(d2, n);
            for (int j = 0; j < n; j++)
            {
                Check(j == 0 || d2[ord[j - 1]] <= d2[ord[j]], "ascending");
                oursPlans++;
                if (ok[ord[j]]) { ours = true; break; }
                oursFailed++;
            }
            Check(game == ours, "verdict run " + run);
        }
        Console.WriteLine("stuck model: plans game " + gamePlans + " ours " + oursPlans + ", failed plans game " + gameFailed + " ours " + oursFailed);

        // Within: window, clock back, TickCount wrap
        Check(NoStartCore.Within(1000, 600, 500), "inside");
        Check(NoStartCore.Within(1100, 600, 500), "edge");
        Check(!NoStartCore.Within(1101, 600, 500), "outside");
        Check(!NoStartCore.Within(500, 600, 500), "clock back");
        Check(NoStartCore.Within(int.MinValue + 100, int.MaxValue - 100, 500), "wrap");

        // stuckcheckcap: outcome slots and the cut rule (a reset only after uncut searches)
        Check(NoStartCore.StuckOutcome(0, 0, true) == 298, "no plan");
        Check(NoStartCore.StuckOutcome(3, 1, true) == 299 && NoStartCore.StuckOutcome(1, 1, false) == 299, "not stuck");
        Check(NoStartCore.StuckOutcome(10, 0, false) == 300, "re-arm");
        Check(NoStartCore.StuckOutcome(10, 0, true) == 301, "reset");
        Check(NoStartCore.StuckMayCut(1) && NoStartCore.StuckMayCut(2) && !NoStartCore.StuckMayCut(3) && !NoStartCore.StuckMayCut(4), "cut rounds");
        // docs/163 §10.1 tag word (ts3native ScProbeMark reads the same bits; selftest.cpp checks 0x10204 / 0x10002)
        Check(NoStartCore.StuckTag(4, false, true) == 0x10204UL && NoStartCore.StuckTag(2, true, false) == 0x10102UL && NoStartCore.StuckTag(0, false, false) == 0x10000UL, "stuck tag");

        if (fails > 0) { Console.WriteLine("nostart selftest FAILED: " + fails); Environment.Exit(1); }
        Console.WriteLine("nostart selftest ok");
    }
}
