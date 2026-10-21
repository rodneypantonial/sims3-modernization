// Usage: node tools/selftest/slicequeue/run.ts  (compiles this with mods/ts3fixes/src/SliceQueue.cs)
// Offline self-check of SliceQueue: FIFO order (the stuck checks run in alarm order), the depth and wait gauges, a clock
// that went back, Clear after a killed task, and a random Add/Next model. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Fixes;

static class SliceQueueTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    static int Main()
    {
        SliceQueue<string> q = new SliceQueue<string>();
        string v;
        Check(!q.Next(0, out v) && v == null, "empty: nothing");
        q.Add("a", 10); q.Add("b", 12); q.Add("c", 13);
        Check(q.Count == 3 && q.MaxDepth == 3, "depth 3");
        Check(q.Next(20, out v) && v == "a" && q.MaxWait == 10, "a first, waited 10");
        Check(q.Next(21, out v) && v == "b" && q.MaxWait == 10, "b, 9 < 10");
        Check(q.Next(5, out v) && v == "c" && q.MaxWait == 10, "clock went back: no negative max");
        Check(!q.Next(30, out v) && q.Count == 0 && q.MaxDepth == 3, "drained, depth kept");
        q.Add("d", 40); q.Add("e", 41);
        Check(q.Clear() == 2 && q.Count == 0 && !q.Next(50, out v), "Clear drops and counts");

        // random model: a plain list, one Next per tick, adds in bursts
        Random r = new Random(134);
        SliceQueue<int> m = new SliceQueue<int>();
        List<int> model = new List<int>();
        List<long> at = new List<long>();
        int next = 0, depth = 0;
        long maxWait = 0;
        for (long t = 0; t < 5000; t++)
        {
            int adds = r.Next(10) == 0 ? r.Next(1, 6) : 0;
            for (int i = 0; i < adds; i++) { m.Add(next, t); model.Add(next); at.Add(t); next++; depth = Math.Max(depth, model.Count); }
            int got;
            bool ok = m.Next(t, out got);
            Check(ok == (model.Count > 0), "model: Next answers when non-empty at " + t);
            if (ok && model.Count > 0)
            {
                Check(got == model[0], "model: FIFO at " + t);
                maxWait = Math.Max(maxWait, t - at[0]);
                model.RemoveAt(0); at.RemoveAt(0);
            }
        }
        Check(m.MaxDepth == depth && m.MaxWait == maxWait, "model: gauges " + m.MaxDepth + "/" + depth + " " + m.MaxWait + "/" + maxWait);

        Console.WriteLine(sFails == 0 ? "slicequeue selftest ok" : sFails + " failures");
        return sFails == 0 ? 0 : 1;
    }
}
