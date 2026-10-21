// Usage: node tools/selftest/unseen/run.ts  (compiles this with mods/ts3fixes/src/UnseenThrottleCore.cs)
// Offline self-check of the unseen script throttle math (docs/144): the per-key "one call in n" gate with its stagger
// and snap-back, the motive time gate with its cap, and the N x dt equivalence of a clamped linear integrator (the shape
// of Motive.UpdateMotiveWithDecay + SetValue) against N steps of dt, including how late a threshold is crossed.
// Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Fixes;

static class UnseenTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    static void GateCadence()
    {
        EveryN g = new EveryN();
        for (int n = 1; n <= 8; n++)
            for (int key = 0; key < 40; key++)
            {
                g.Clear();
                int runs = 0, last = -1, first = -1;
                bool even = true;
                for (int call = 0; call < n * 10; call++)
                    if (g.Unseen(key * 7919, n))
                    {
                        if (first < 0) first = call;
                        if (last >= 0 && call - last != n) even = false;
                        last = call; runs++;
                    }
                Check(runs == 10, "n=" + n + " key " + key + ": 10 runs in 10n calls (" + runs + ")");
                Check(even, "n=" + n + " key " + key + ": runs exactly n calls apart");
                Check(first == (key * 7919) % n, "n=" + n + " key " + key + ": first run staggered by the key");
            }
        // eight Sims called once each per round, n = 4: two of them run every round (load spread, not all at once)
        g.Clear();
        for (int round = 0; round < 12; round++)
        {
            int runs = 0;
            for (int key = 0; key < 8; key++) if (g.Unseen(key, 4)) runs++;
            Check(runs == 2, "round " + round + ": 2 of 8 Sims run (" + runs + ")");
        }
        // snap back: a seen call forgets the key, the next unseen stretch starts staggered again
        g.Clear();
        Check(!g.Unseen(3, 4) && !g.Unseen(3, 4) && !g.Unseen(3, 4) && g.Unseen(3, 4), "key 3, n 4: runs on its 4th unseen call");
        g.Seen(3);
        Check(g.Count == 0, "Seen removes the key");
        Check(!g.Unseen(3, 4), "after Seen: staggered again, not run at once (the seen call just ran)");
        Check(g.Unseen(4, 4), "key 4, n 4: first unseen call runs (4 % 4 == 0)");
        Check(g.Unseen(-5, 0) && g.Unseen(-5, 1), "n <= 1 always runs");
        Check(g.Unseen(-5, 3) == (((-5) & 0x7FFFFFFF) % 3 == 0), "negative key staggered on its low 31 bits");
        // a full table starts over instead of growing
        g.Clear();
        for (int key = 0; key < EveryN.kMaxKeys + 10; key++) g.Unseen(key, 4);
        Check(g.Clears == 1 && g.Count == 10, "full table restarts once (" + g.Clears + ", " + g.Count + ")");
    }

    static void TimeGate()
    {
        Check(!UnseenMath.Due(3.99f, 4, 1f, 30f), "3.99 of 4 sim-minutes: wait");
        Check(UnseenMath.Due(4f, 4, 1f, 30f), "4 of 4: due");
        Check(UnseenMath.Due(0f, 1, 1f, 30f), "n = 1: always due (the game's own 1-minute floor applies)");
        Check(!UnseenMath.Due(29.9f, 100, 1f, 30f) && UnseenMath.Due(30f, 100, 1f, 30f), "cap: due at 30 whatever n");
        Check(UnseenMath.Key(0x0000000100000002UL) == 3, "key folds the high half in");
    }

    // Clamped linear integrator: value += rate * dt, clamped to [-100, 100] after each step (SetValue's clamp).
    static float Step(float v, float rate, float dt) { return Math.Max(-100f, Math.Min(100f, v + rate * dt)); }

    static void Integrators()
    {
        Random r = new Random(1234);
        for (int i = 0; i < 2000; i++)
        {
            int n = 1 + r.Next(16);
            float dt = 0.05f + (float)r.NextDouble() * 0.5f;  // hours per base update
            float rate = (float)(r.NextDouble() * 400 - 200);  // motive points per hour, either sign
            float v0 = (float)(r.NextDouble() * 200 - 100);
            float small = v0;
            for (int k = 0; k < n; k++) small = Step(small, rate, dt);
            float big = Step(v0, rate, n * dt);
            Check(Math.Abs(small - big) < 1e-2f, "constant rate: N steps == one N x dt step (" + small + " vs " + big + ")");
        }
        // the rate changes inside the window (an interaction ends): the error is bounded by |r1 - r2| x window
        for (int i = 0; i < 2000; i++)
        {
            int n = 2 + r.Next(15), j = 1 + r.Next(n - 1);
            float dt = 0.05f + (float)r.NextDouble() * 0.5f;
            float r1 = (float)(r.NextDouble() * 100 - 50), r2 = (float)(r.NextDouble() * 100 - 50);
            float v0 = (float)(r.NextDouble() * 100 - 50);
            float small = v0;
            for (int k = 0; k < n; k++) small = Step(small, k < j ? r1 : r2, dt);
            float big = Step(v0, r2, n * dt); // one update at the end sees only the new rate
            Check(Math.Abs(small - big) <= Math.Abs(r1 - r2) * j * dt + 1e-2f, "rate change: error within |r1 - r2| x (j dt)");
        }
        // a threshold (a moodlet trigger such as Hungry at -50) is noticed at most one window late
        for (int i = 0; i < 500; i++)
        {
            int n = 1 + r.Next(16);
            float dt = 0.05f + (float)r.NextDouble() * 0.2f, rate = -(10f + (float)r.NextDouble() * 90f), th = -50f;
            float v = 100f, t = 0f, tSmall = -1f;
            for (int k = 0; k < 10000 && tSmall < 0; k++) { v = Step(v, rate, dt); t += dt; if (v <= th) tSmall = t; }
            v = 100f; t = 0f; float tBig = -1f;
            for (int k = 0; k < 10000 && tBig < 0; k++) { v = Step(v, rate, n * dt); t += n * dt; if (v <= th) tBig = t; }
            Check(tBig >= tSmall - 1e-3f && tBig - tSmall <= (n - 1) * dt + 1e-3f, "threshold seen at most N-1 base steps late");
        }
    }

    // The game skips an update below 1 sim-minute and leaves mLastTime alone; the throttle does the same below N. Calls
    // arrive at irregular gaps (SimUpdate slots): integrated + pending time always equals elapsed time, and the pending gap
    // stays under the cap.
    static void NoTimeLost()
    {
        Random r = new Random(99);
        for (int run = 0; run < 200; run++)
        {
            int n = 1 + r.Next(40);
            float now = 0f, last = 0f, integrated = 0f, maxPending = 0f;
            for (int call = 0; call < 5000; call++)
            {
                now += 0.1f + (float)r.NextDouble() * 1.5f;
                float gap = now - last;
                if (gap < 1f || !UnseenMath.Due(gap, n, 1f, 30f)) { maxPending = Math.Max(maxPending, gap); continue; }
                integrated += gap; last = now;
            }
            Check(Math.Abs(integrated + (now - last) - now) < 0.05f, "n=" + n + ": no sim time lost or added");
            Check(maxPending < 30f + 1.6f, "n=" + n + ": pending gap stays under the cap plus one call gap (" + maxPending + ")");
            Check(maxPending < 60f, "n=" + n + ": never reaches the 60-minute auto-satisfy switch");
        }
    }

    static int Main()
    {
        GateCadence();
        TimeGate();
        Integrators();
        NoTimeLost();
        Console.WriteLine(sFails == 0 ? "unseen selftest: PASS" : "unseen selftest: FAIL (" + sFails + ")");
        return sFails == 0 ? 0 : 1;
    }
}
