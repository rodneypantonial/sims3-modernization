// Offline self-check of mods/ts3fixes/src/OverwatchProbeCore.cs (switch owprobeplans, docs/163 §9): the (Sim, task) marks
// that hide EA's stuck-check probe plans from NRaas Overwatch's Stuck Check V2. Same Enter / try / finally / Exit shape
// as FewerPlans.cs StuckCheckRun.
using System;
using Ts3Fixes;

static class OwProbeTest
{
    static int fails;
    static void Check(bool ok, string what) { if (!ok) { fails++; Console.WriteLine("FAIL " + what); } }
    static readonly OverwatchProbeCore P = new OverwatchProbeCore();

    // StuckCheckRun's shape: mark, run the body (which may nest or throw), unmark in finally
    static void Run(ulong sim, ulong task, Action body)
    {
        P.Enter(sim, task);
        try { body(); } finally { P.Exit(sim, task); }
    }

    static void Main()
    {
        Check(P.Depth == 0 && !P.Active(1, 10), "empty");

        // plain: active inside only, for that Sim in that task only
        Run(1, 10, delegate
        {
            Check(P.Active(1, 10), "inside");
            Check(!P.Active(1, 11), "same Sim, other task (the Sim's own plan while the check sleeps) still counts");
            Check(!P.Active(2, 10), "other Sim, same task");
        });
        Check(P.Depth == 0 && !P.Active(1, 10), "after");

        // nested same key (a check re-entered from inside a plan callback): still active after the inner one ends
        Run(1, 10, delegate
        {
            Run(1, 10, delegate { Check(P.Depth == 2, "nested depth"); });
            Check(P.Active(1, 10), "outer still marked after inner exit");
        });
        Check(P.Depth == 0, "nested after");

        // two checks at once (alarm path in the world lot task + our drain task), interleaved exits
        P.Enter(1, 10); P.Enter(2, 20);
        P.Exit(1, 10);
        Check(!P.Active(1, 10) && P.Active(2, 20), "interleaved exit");
        P.Exit(2, 20);
        Check(P.Depth == 0, "interleaved after");

        // exception inside the body: unmarked by the finally, exception goes on to the caller
        bool caught = false;
        try { Run(3, 30, delegate { Run(3, 30, delegate { throw new InvalidOperationException("plan threw"); }); }); }
        catch (InvalidOperationException) { caught = true; }
        Check(caught, "exception propagates");
        Check(P.Depth == 0 && !P.Active(3, 30), "exception-safe");

        // unmatched exit is a no-op; Clear drops leftovers of a task the world unload killed
        P.Exit(9, 90);
        P.Enter(4, 40);
        P.Clear();
        Check(P.Depth == 0 && !P.Active(4, 40), "clear");

        if (fails > 0) { Console.WriteLine("owprobe selftest FAILED: " + fails); Environment.Exit(1); }
        Console.WriteLine("owprobe selftest ok");
    }
}
