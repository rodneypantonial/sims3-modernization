// Usage: node tools/selftest/replandefer/run.ts  (compiles this with mods/ts3fixes/src/ReplanDeferCore.cs)
// Offline self-check of ReplanDeferCore.cs (docs/141 §6): the replandefer decision (only the case where the game's own
// replan ends in "insert the new path, kill this action" is deferred) and the replanspread queue (dedupe, at most K
// routing entries replayed per tick, dead entries dropped, order kept), the queue against a plain model on random
// sequences. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Fixes;

static class ReplanDeferTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    static void Decision()
    {
        Check(ReplanDeferCore.ShouldDefer(false, true, true, true, true), "followed route, current action: defer");
        Check(!ReplanDeferCore.ShouldDefer(true, true, true, true, true), "load replan: game's code");
        Check(!ReplanDeferCore.ShouldDefer(false, false, true, true, true), "not a Sim: game's code");
        Check(!ReplanDeferCore.ShouldDefer(false, true, false, true, true), "queued action, not current: game's code");
        Check(!ReplanDeferCore.ShouldDefer(false, true, true, false, true), "no path adaptor (Route.Replan, no kill): game's code");
        Check(!ReplanDeferCore.ShouldDefer(false, true, true, true, false), "route not followed yet (elevator approach in DoPortal): game's code");
    }

    class Comp { public bool Alive = true, Routing; public int Calls; public readonly int Id; public Comp(int id) { Id = id; } }

    static void SpreadCases()
    {
        SpreadQueue<Comp> q = new SpreadQueue<Comp>();
        Comp[] c = new Comp[10];
        for (int i = 0; i < c.Length; i++) { c[i] = new Comp(i); c[i].Routing = i % 2 == 0; }
        foreach (Comp x in c) Check(q.Add(x), "first add");
        Check(!q.Add(c[3]), "second add of the same component refused");
        Check(q.Count == 10, "count 10");
        List<int> order = new List<int>();
        c[4].Alive = false; // destroyed before its turn
        int calls = q.DrainTick(2, x => x.Alive, x => x.Routing, x => { x.Calls++; order.Add(x.Id); });
        // tick 1: 0 (routing), 1, [4 dead skipped later], 2 (routing) -> stops after the 2nd routing one
        Check(calls == 3 && string.Join(",", order.ConvertAll(i => i.ToString()).ToArray()) == "0,1,2", "tick 1 replays 0,1,2: " + string.Join(",", order.ConvertAll(i => i.ToString()).ToArray()));
        order.Clear();
        calls = q.DrainTick(2, x => x.Alive, x => x.Routing, x => { x.Calls++; order.Add(x.Id); });
        Check(string.Join(",", order.ConvertAll(i => i.ToString()).ToArray()) == "3,5,6,7,8", "tick 2 skips dead 4, stops after 6 and 8: " + string.Join(",", order.ConvertAll(i => i.ToString()).ToArray()));
        Check(q.Count == 1, "one left (9)");
        q.DrainTick(2, x => x.Alive, x => x.Routing, x => x.Calls++);
        Check(q.Count == 0 && c[9].Calls == 1 && c[4].Calls == 0, "drained, dead never called");
        Check(q.DrainTick(2, x => true, x => true, x => { }) == 0, "empty queue: no calls");
        Check(q.Add(c[3]), "re-add after its replay");
        q.Clear();
        Check(q.Count == 0, "Clear");
    }

    // random sequences against a plain model: every alive entry replayed exactly once per add, in add order, routing
    // replays per tick <= K
    static void SpreadModel()
    {
        Random r = new Random(141);
        for (int run = 0; run < 200 && sFails < 10; run++)
        {
            int k = 1 + r.Next(5);
            SpreadQueue<Comp> q = new SpreadQueue<Comp>();
            List<Comp> model = new List<Comp>();
            Comp[] pool = new Comp[30];
            for (int i = 0; i < pool.Length; i++) pool[i] = new Comp(i);
            List<int> replayed = new List<int>(), expected = new List<int>();
            for (int step = 0; step < 300; step++)
            {
                int op = r.Next(4);
                if (op < 2)
                {
                    Comp x = pool[r.Next(pool.Length)];
                    bool added = q.Add(x);
                    Check(added == !model.Contains(x), "Add result");
                    if (added) model.Add(x);
                }
                else if (op == 2) { Comp x = pool[r.Next(pool.Length)]; x.Alive = r.Next(4) != 0; x.Routing = r.Next(2) == 0; }
                else
                {
                    int routed = 0;
                    while (model.Count > 0)
                    {
                        Comp x = model[0];
                        if (!x.Alive) { model.RemoveAt(0); continue; }
                        if (x.Routing) { if (routed == k) break; routed++; }
                        model.RemoveAt(0);
                        expected.Add(x.Id);
                        if (x.Routing && routed == k) break;
                    }
                    int routedSeen = 0;
                    q.DrainTick(k, x => x.Alive, x => x.Routing, x => { replayed.Add(x.Id); if (x.Routing) routedSeen++; });
                    Check(routedSeen <= k, "routing replays per tick <= K");
                    Check(q.Count == model.Count, "count matches model at run " + run + " step " + step);
                }
            }
            Check(string.Join(",", replayed.ConvertAll(i => i.ToString()).ToArray()) == string.Join(",", expected.ConvertAll(i => i.ToString()).ToArray()), "replay order matches model, run " + run);
        }
    }

    static int Main()
    {
        Decision();
        SpreadCases();
        SpreadModel();
        Console.WriteLine(sFails == 0 ? "replandefer selftest ok" : "replandefer selftest: " + sFails + " failures");
        return sFails == 0 ? 0 : 1;
    }
}
