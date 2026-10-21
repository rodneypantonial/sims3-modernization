// Usage: node tools/selftest/farthestpick/run.ts  (compiles this with mods/ts3fixes/src/FarthestPick.cs)
// Random worlds (lot count, distances with ties, routable set): the sorted pick equals the game's running-maximum loop,
// plans only lots the game also plans, and with a cap returns the next lot in distance order. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Fixes;

static class FarthestPickTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok && sFails++ < 20) Console.WriteLine("FAIL: " + what); }

    static int Main()
    {
        Random r = new Random(141);
        long gamePlans = 0, sortedPlans = 0;
        for (int t = 0; t < 20000; t++)
        {
            int n = r.Next(0, 40);
            float[] d2 = new float[n];
            bool[] ok = new bool[n];
            double pOk = r.NextDouble();
            for (int i = 0; i < n; i++) { d2[i] = r.Next(0, 3) == 0 ? r.Next(0, 6) * 100f : (float)(r.NextDouble() * 1e6); ok[i] = r.NextDouble() < pOk; }
            List<int> gameAsked = new List<int>(), ourAsked = new List<int>();
            int gp, sp;
            bool capped;
            int g = FarthestPick.Game(d2, delegate(int i) { gameAsked.Add(i); return ok[i]; }, out gp);
            int s = FarthestPick.Sorted(d2, delegate(int i) { ourAsked.Add(i); return ok[i]; }, int.MaxValue, out sp, out capped);
            Check(g == s, "world " + t + ": game " + g + " sorted " + s);
            Check(!capped && sp == ourAsked.Count && gp == gameAsked.Count, "world " + t + ": plan counts");
            foreach (int i in ourAsked) Check(gameAsked.Contains(i), "world " + t + ": sorted planned " + i + ", the game did not");
            gamePlans += gp; sortedPlans += sp;

            // cap: never more than `cap` plans; uncapped answer when it lies within the first `cap` lots planned
            int cap = r.Next(1, 5), cp;
            List<int> capAsked = new List<int>();
            int c = FarthestPick.Sorted(d2, delegate(int i) { capAsked.Add(i); return ok[i]; }, cap, out cp, out capped);
            Check(cp <= cap && cp == capAsked.Count, "world " + t + ": cap " + cap + " ran " + cp);
            if (!capped) Check(c == s, "world " + t + ": not capped, same answer");
            else
            {
                Check(cp == cap && !ok[capAsked[cap - 1]] && c >= 0 && !capAsked.Contains(c), "world " + t + ": capped answer is the next unplanned lot");
                foreach (int i in capAsked) Check(d2[i] > d2[c] || (d2[i] == d2[c] && i < c), "world " + t + ": capped answer is nearer than every planned lot");
            }
        }
        // a fixed case: list order A(far, unroutable) B(near) C(farthest, routable) D(far as C, routable)
        int p1, p2;
        bool cap1;
        float[] fd = { 50f, 10f, 90f, 90f };
        bool[] fo = { false, true, true, true };
        Check(FarthestPick.Game(fd, delegate(int i) { return fo[i]; }, out p1) == 2 && p1 == 3, "fixed: game plans A B C, picks C");
        Check(FarthestPick.Sorted(fd, delegate(int i) { return fo[i]; }, 3, out p2, out cap1) == 2 && p2 == 1, "fixed: sorted plans C only");

        Console.WriteLine("plans: game " + gamePlans + ", sorted " + sortedPlans);
        Console.WriteLine(sFails == 0 ? "farthestpick selftest ok" : sFails + " failures");
        return sFails == 0 ? 0 : 1;
    }
}
