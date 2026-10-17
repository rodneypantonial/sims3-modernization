// Usage: node tools/selftest/hotspots/run.ts  (compiles this with mods/ts3fixes/src/TickMemo.cs)
// Offline self-check of TickMemo.cs: the per-tick stamp rules shared by the docs/74 memos, the reference set behind
// the detail-priority lot set, the hours-of-operation memo (outputs written only on a true answer) and the tooltip
// poll gate, each checked against fixed cases or a plain model on random call sequences. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Fixes;

static class HotSpotsTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    static void StampRules()
    {
        TickStamp s = new TickStamp();
        object a = new object(), b = new object();
        Check(s.Renew(5, a, 1000), "first use is stale");
        Check(!s.Renew(5, a, 1050), "same tick, owner, 50 ms: fresh");
        Check(!s.Renew(5, a, 1100), "100 ms: still fresh");
        Check(s.Renew(5, a, 1101), "101 ms: stale (time cap)");
        Check(s.Renew(6, a, 1102), "next tick: stale");
        Check(s.Renew(6, b, 1103), "owner changed: stale");
        Check(!s.Renew(6, b, 1104), "same again: fresh");
        Check(s.Renew(6, b, 1000), "clock went back: stale");
        s.Reset();
        Check(s.Renew(6, b, 1001), "after Reset: stale");
        Check(s.Renew(6, null, 1002), "owner null after b: stale");
        Check(!s.Renew(6, null, 1003), "owner null twice: fresh");
        // TickCount wraps after ~25 days: the age is taken modulo 2^32
        Check(s.Renew(7, null, int.MaxValue - 10), "wrap: new tick");
        Check(!s.Renew(7, null, unchecked(int.MaxValue + 50)), "wrap: 60 ms later is fresh");
    }

    static void RefSetModel()
    {
        Random r = new Random(74);
        object[] pool = new object[40];
        for (int i = 0; i < pool.Length; i++) pool[i] = i % 3 == 0 ? (object)("lot" + (i % 6)) : new object(); // equal text, distinct refs
        RefSet set = new RefSet();
        List<object> model = new List<object>();
        for (int step = 0; step < 20000; step++)
        {
            int op = r.Next(10);
            if (op == 0) { set.Clear(); model.Clear(); }
            else if (op < 5)
            {
                object o = r.Next(20) == 0 ? null : pool[r.Next(pool.Length)];
                set.Add(o);
                if (o != null && !model.Exists(x => ReferenceEquals(x, o))) model.Add(o);
            }
            else
            {
                object o = pool[r.Next(pool.Length)];
                Check(set.Contains(o) == model.Exists(x => ReferenceEquals(x, o)), "RefSet.Contains at step " + step);
            }
            Check(set.Count == model.Count, "RefSet.Count at step " + step);
            if (sFails > 10) return;
        }
    }

    // Model of Lot.GetHoursOfOperation: per lot and tick a fixed answer; the outputs are written only when it is true.
    static bool Game(ulong lot, long tick, ref float open, ref float close)
    {
        int h = (int)(lot * 7 + (ulong)(tick / 3)); // answers change every few ticks (a party starting, a venue changing)
        if (h % 4 == 0) return false;
        open = h % 24; close = (h * 5) % 24 + 0.5f;
        return true;
    }

    // The hook's logic around HoursMemo, without the game: same stamp, same hit and miss paths.
    static bool Memo(HoursMemo m, ulong lot, long tick, int nowMs, ref float open, ref float close, out bool hit)
    {
        if (m.Stamp.Renew(tick, null, nowMs)) m.Clear();
        float o = open, c = close;
        bool res;
        hit = m.TryGet(lot, ref o, ref c, out res);
        if (hit) { open = o; close = c; return res; }
        res = Game(lot, tick, ref open, ref close);
        m.Store(lot, res, open, close);
        return res;
    }

    static void HoursModel()
    {
        Random r = new Random(87);
        HoursMemo m = new HoursMemo();
        long tick = 0; int now = 0, hits = 0;
        for (int step = 0; step < 50000; step++)
        {
            if (r.Next(12) == 0) { tick++; now += 25; }
            if (r.Next(500) == 0) now += 150; // ticks stopped: the time cap must rebuild
            ulong lot = (ulong)r.Next(30) + 0x1000000000UL;
            float inOpen = r.Next(3) == 0 ? -1f : (float)r.NextDouble(), inClose = (float)r.NextDouble() * 3f; // caller's values
            float go = inOpen, gc = inClose, mo = inOpen, mc = inClose;
            bool hit;
            bool g = Game(lot, tick, ref go, ref gc);
            bool a = Memo(m, lot, tick, now, ref mo, ref mc, out hit);
            if (hit) hits++;
            Check(g == a && go == mo && gc == mc, "hours memo at step " + step + " lot " + lot + " tick " + tick);
            if (sFails > 10) return;
        }
        Check(hits > 10000, "hours memo hits " + hits);
    }

    static void GateRules()
    {
        PollGate g = new PollGate();
        Check(!g.Skip(10, 20, 0), "never ran: walk");
        g.Ran(10, 20, 1000);
        Check(g.Skip(10, 20, 1000), "same point, 0 ms: skip");
        Check(g.Skip(10, 20, 1099), "same point, 99 ms: skip");
        Check(!g.Skip(10, 20, 1100), "same point, 100 ms: walk");
        Check(!g.Skip(10.5f, 20, 1050), "cursor moved: walk");
        Check(!g.Skip(10, 20, 900), "clock went back: walk");
        g.Ran(10, 20, int.MaxValue - 20);
        Check(g.Skip(10, 20, unchecked(int.MaxValue + 30)), "wrap: 50 ms later skips");
        // a resting cursor polled every 25 ms (a ~40 ticks/s loop) walks about every 100 ms
        g = new PollGate();
        int walks = 0;
        for (int t = 0; t < 10000; t += 25) if (!g.Skip(5, 5, t)) { walks++; g.Ran(5, 5, t); }
        Check(walks >= 99 && walks <= 101, "resting cursor walks " + walks + " in 10 s");
    }

    static int Main()
    {
        StampRules();
        RefSetModel();
        HoursModel();
        GateRules();
        Console.WriteLine(sFails == 0 ? "hotspots selftest: PASS" : "hotspots selftest: FAIL (" + sFails + ")");
        return sFails == 0 ? 0 : 1;
    }
}
