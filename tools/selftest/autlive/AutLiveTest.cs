// Usage: node tools/selftest/autlive/run.ts  (compiles this with mods/ts3fixes/src/AutLiveCore.cs)
// Offline self-check of the docs/124 §16-§17 go-live consume logic: the BestScores queue model, the worker order, the band
// rule, and the exactness claim of §16.1: with live scores equal to the worker's (or all scaled by one factor), the queue
// that consume builds (band 1 at B14, bands after B13) equals the queue the game builds from every passing pair. Exit 1 on
// failure.
using System;
using Ts3Fixes;

static class AutLiveTest
{
    static int sFail;
    static void Check(string what, bool ok) { if (!ok) { sFail++; Console.WriteLine("FAIL " + what); } }

    static int Main()
    {
        // queue: scores > 0 only; full = replace the smallest only when strictly greater
        AutLiveCore.Heap h = new AutLiveCore.Heap(3);
        h.Add(1, 5f); h.Add(2, 3f); h.Add(3, 0f); h.Add(4, -1f); h.Add(5, 4f);
        Check("heap fills with positives", h.N == 3 && h.Has(1) && h.Has(2) && h.Has(5) && !h.Has(3));
        h.Add(6, 2f);
        Check("smaller than the minimum: ignored", !h.Has(6));
        h.Add(7, 4f);
        Check("greater than the minimum: replaces it", h.Has(7) && !h.Has(2) && h.Min() == 4f);
        h.Add(8, 4f);
        Check("equal to the minimum: ignored (strict)", !h.Has(8));
        Check("top", h.TopId() == 1);
        // rule
        Check("count rule", AutLiveCore.Fires(9, 10, 1f, 0f, 0f, 100f) == 1);
        Check("tie fires", AutLiveCore.Fires(10, 10, 2f, 0f, 0f, 2f) == 2);
        Check("below: no band", AutLiveCore.Fires(10, 10, 1.9f, 0f, 0f, 2f) == 0);
        Check("drift widens", AutLiveCore.Fires(10, 10, 1.9f, 0.1f, 0f, 2f) == 2);
        Check("margin widens", AutLiveCore.Fires(10, 10, 1.9f, 0f, 0.06f, 2f) == 2);
        Check("drift fail = 1", AutLiveCore.Drift(5f, 0f) == 1f && AutLiveCore.Drift(5f, 6f) == 0f && Math.Abs(AutLiveCore.Drift(4f, 3f) - 0.25f) < 1e-6f);
        Check("drift bins", AutLiveCore.DriftBin(0f) == 0 && AutLiveCore.DriftBin(0.005f) == 1 && AutLiveCore.DriftBin(0.03f) == 2 && AutLiveCore.DriftBin(0.1f) == 3 && AutLiveCore.DriftBin(0.5f) == 4);
        // order: score desc, ties by index
        int[] ord = new int[8];
        int on = AutLiveCore.Order(new float[] { 1f, 3f, 3f, 2f, 9f }, new bool[] { true, true, true, true, false }, 5, ord);
        Check("order", on == 4 && ord[0] == 1 && ord[1] == 2 && ord[2] == 3 && ord[3] == 0);
        // bands: max 2 = band 1 + one more, then full
        {
            AutLiveCore.Bands b = new AutLiveCore.Bands();
            float[] w = new float[30];
            bool[] el = new bool[30], sent = new bool[30];
            for (int i = 0; i < 30; i++) { w[i] = 30 - i; el[i] = true; }
            for (int i = 0; i < 30; i++) b.Order[i] = i;
            b.Start(4, 2, 0f, 30);
            for (int i = 0; i < 4; i++) sent[i] = true;
            int[] buf = new int[30];
            int n = b.Step(10, 10, 100f, el, sent, w, buf);
            Check("no fire above s10", n == 0);
            n = b.Step(3, 10, 100f, el, sent, w, buf);
            Check("count rule sends the next 4", n == 4 && buf[0] == 4 && buf[3] == 7 && b.LastWhy == 1 && !b.Full);
            for (int i = 0; i < n; i++) sent[buf[i]] = true;
            el[9] = false;
            n = b.Step(10, 10, 22f, el, sent, w, buf);  // w = 22 (pair 8) >= 22: tie fires, the last band = all remaining eligible
            Check("full band: every remaining eligible pair", n == 21 && buf[0] == 8 && buf[1] == 10 && b.Full && b.LastWhy == 2);
            Check("after full: nothing", b.Step(0, 10, 0f, el, new bool[30], w, buf) == 0);
        }

        // exactness (§16.1): random searches; vanilla = every pair through C1 in request order plus local and off-list entries;
        // consume = the same calls minus the allow-listed passing pairs outside band 1, then the bands after B13
        Random rnd = new Random(12345);
        int exactRuns = 0, exactMiss = 0, scaledMiss = 0, noisyRuns = 0, noisyMiss = 0, fired = 0;
        for (int run = 0; run < 3000; run++)
        {
            int pairs = rnd.Next(0, 60), local = rnd.Next(0, 13), off = rnd.Next(0, 20);
            int topN = 1 + rnd.Next(12), bands = 1 + rnd.Next(4);
            float[] w = new float[pairs], live = new float[pairs];
            bool[] pass = new bool[pairs], liveHas = new bool[pairs];
            for (int i = 0; i < pairs; i++)
            {
                pass[i] = rnd.Next(3) > 0;
                w[i] = pass[i] ? (float)(1 + rnd.NextDouble() * 99) + i * 1e-3f : 0f;
                liveHas[i] = true;
            }
            int mode = run % 3;  // 0 exact, 1 one scale factor, 2 noisy per pair (measured, not asserted)
            float f = (float)(0.5 + rnd.NextDouble() * 0.5);
            for (int i = 0; i < pairs; i++)
                live[i] = !pass[i] ? 0f : mode == 0 ? w[i] : mode == 1 ? w[i] * f : w[i] * (float)(0.8 + rnd.NextDouble() * 0.4);
            // calls in the game's order: local entries first (before B13), then a shuffle of meta pairs and off-list ads
            int calls = local + pairs + off;
            int[] id = new int[calls];
            float[] sc = new float[calls];
            bool[] send = new bool[calls];
            int k = 0;
            for (int i = 0; i < local; i++) { id[k] = -1 - i; sc[k] = (float)(rnd.NextDouble() * 120); send[k] = true; k++; }
            int[] mix = new int[pairs + off];
            for (int i = 0; i < mix.Length; i++) mix[i] = i;
            for (int i = mix.Length - 1; i > 0; i--) { int j = rnd.Next(i + 1); int t = mix[i]; mix[i] = mix[j]; mix[j] = t; }
            bool[] cand = new bool[pairs];
            for (int i = 0; i < pairs; i++) cand[i] = pass[i];
            AutLiveCore.Bands b = new AutLiveCore.Bands();
            if (b.Order.Length < pairs) b.Order = new int[pairs];
            int n = AutLiveCore.Order(w, cand, pairs, b.Order);
            b.Start(topN, bands, 0f, n);
            bool[] sel = new bool[pairs], sent = new bool[pairs];
            for (int r = 0; r < n && r < topN; r++) sel[b.Order[r]] = true;
            foreach (int m in mix)
            {
                if (m < pairs) { id[k] = m; sc[k] = live[m]; send[k] = sel[m]; if (sel[m]) { sent[m] = true; b.Saw(w[m], live[m]); } }
                else { id[k] = -100 - m; sc[k] = (float)(rnd.NextDouble() * 120); send[k] = true; }
                k++;
            }
            AutLiveCore.Heap van = new AutLiveCore.Heap(10), con = new AutLiveCore.Heap(10);
            AutLiveCore.Replay(van, calls, id, sc, null);
            AutLiveCore.Replay(con, calls, id, sc, send);
            fired += AutLiveCore.RunBands(con, b, cand, sent, w, live, liveHas, new int[pairs]);
            bool same = van.SameSet(con) && van.TopId() == con.TopId();
            if (mode == 0) { exactRuns++; if (!same) exactMiss++; }
            else if (mode == 1) { if (!same) scaledMiss++; }
            else { noisyRuns++; if (!same) noisyMiss++; }
        }
        Check("exact live scores: consume queue = game queue in every run", exactMiss == 0);
        Check("one scale factor: consume queue = game queue in every run", scaledMiss == 0);
        Console.WriteLine("autlive: " + exactRuns + " exact runs (misses " + exactMiss + "), scaled misses " + scaledMiss + ", noisy +-20%: "
            + noisyMiss + " of " + noisyRuns + " differ (not asserted), bands fired " + fired);
        Console.WriteLine(sFail == 0 ? "autlive selftest: PASS" : "autlive selftest: " + sFail + " FAIL");
        return sFail == 0 ? 0 : 1;
    }
}
