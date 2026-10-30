using System;
using System.Collections.Generic;
using Ts3Fixes;

// Offline self-check of SiQueue (issue #82): the drain loop of SeasonImposter.cs modelled with random warm sets, distances
// and season changes arriving mid-drain; every queued lot is rebuilt exactly once (final state correct), warm lots go
// before cold ones, cold lots wait while cold is off and the flush takes the rest.
static class SeasonImposterTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; if (sFails < 20) Console.WriteLine("FAIL " + what); } }

    static void Main()
    {
        Random rnd = new Random(82);
        SiQueue q = new SiQueue();
        Check(q.Add(5) && !q.Add(5) && q.Count == 1, "a lot is queued once");
        Check(q.Pick(delegate(ulong id) { return false; }, delegate(ulong id) { return 0f; }, false) == -1, "cold only, cold off: wait");
        Check(q.Clear() == 1 && q.Count == 0, "clear returns the count");

        for (int round = 0; round < 2000; round++)
        {
            bool cold = rnd.Next(2) == 0;
            HashSet<ulong> warm = new HashSet<ulong>();
            Dictionary<ulong, float> dist = new Dictionary<ulong, float>();
            HashSet<ulong> wanted = new HashSet<ulong>();
            List<ulong> rebuilt = new List<ulong>();
            int arrivals = rnd.Next(1, 30);
            for (int step = 0; step < 200 && (arrivals > 0 || q.Count > 0); step++)
            {
                if (arrivals > 0 && rnd.Next(3) == 0)
                {
                    ulong id = (ulong)rnd.Next(1, 12);
                    if (rnd.Next(4) == 0) warm.Add(id);
                    dist[id] = (float)rnd.NextDouble() * 1000f;
                    if (!wanted.Contains(id)) wanted.Add(id);
                    else if (rebuilt.Contains(id)) rebuilt.Remove(id); // a newer season change wants a newer rebuild
                    q.Add(id);
                    arrivals--;
                }
                int i = q.Pick(delegate(ulong id) { return warm.Contains(id); }, delegate(ulong id) { return dist[id]; }, cold);
                if (i < 0) { for (int k = 0; k < q.Count; k++) Check(!warm.Contains(q[k]), "waited with a warm lot queued"); continue; }
                ulong pick = q[i];
                if (!warm.Contains(pick))
                {
                    Check(cold, "cold lot rebuilt with cold off");
                    for (int k = 0; k < q.Count; k++)
                    {
                        Check(!warm.Contains(q[k]), "cold picked before a warm one");
                        Check(dist[q[k]] >= dist[pick], "cold lot not the nearest");
                    }
                }
                q.RemoveAt(i);
                rebuilt.Add(pick);
            }
            while (q.Count > 0) { rebuilt.Add(q[0]); q.RemoveAt(0); } // the save / travel flush
            foreach (ulong id in wanted) Check(rebuilt.Contains(id), "lot " + id + " never rebuilt");
            Check(new HashSet<ulong>(rebuilt).Count == rebuilt.Count, "a lot rebuilt twice for one change");
        }
        Console.WriteLine(sFails == 0 ? "seasonimposter: all checks passed" : "seasonimposter: " + sFails + " failures");
        Environment.Exit(sFails == 0 ? 0 : 1);
    }
}
