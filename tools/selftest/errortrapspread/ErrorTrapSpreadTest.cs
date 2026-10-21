using System;
using Ts3Fixes;

// Offline self-check of ErrorTrapSpreadCore (docs/146): the upstream marker (found without running ErrorTrap's static
// constructor), who runs the load-up inventory check, slices.
static class ErrorTrapSpreadTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL " + what); } }

    public static bool sCctorRan;

    // Stand-ins for NRaas.ErrorTrap with and without the upstream marker; the real one has a static constructor.
    class ErrorTrapWithMarker
    {
        public static readonly int SpreadScanVersion = 1;
        static ErrorTrapWithMarker() { sCctorRan = true; }
    }
    class ErrorTrapWithoutMarker { public static readonly int StaggeredCleanupVersion = 1; } // Overwatch's marker name
    class ErrorTrapWrongMarker { public static readonly string SpreadScanVersion = "1"; }
    class ErrorTrapPrivateMarker { static readonly int SpreadScanVersion = 1; }

    static int Main()
    {
        Check(ErrorTrapSpreadCore.HasMarker(typeof(ErrorTrapWithMarker)), "public static int marker found");
        Check(!sCctorRan, "marker check does not run the static constructor");
        Check(!ErrorTrapSpreadCore.HasMarker(typeof(ErrorTrapWithoutMarker)), "Overwatch's marker name is not ErrorTrap's");
        Check(!ErrorTrapSpreadCore.HasMarker(typeof(ErrorTrapWrongMarker)), "marker of another type ignored");
        Check(!ErrorTrapSpreadCore.HasMarker(typeof(ErrorTrapPrivateMarker)), "non-public marker ignored");
        Check(!ErrorTrapSpreadCore.HasMarker(null), "no ErrorTrap type");
        Check(OverwatchSpreadCore.HasMarker(typeof(ErrorTrapWithoutMarker)), "Overwatch marker check unchanged");

        // the hook's rule (OverwatchSpreadCore.Decide): a marker, real or faked, wins over the switch
        OverwatchSpreadCore.Mode ours = OverwatchSpreadCore.Mode.Ours, theirs = OverwatchSpreadCore.Mode.Theirs, up = OverwatchSpreadCore.Mode.Upstream;
        Check(OverwatchSpreadCore.Decide(true, true, false, true) == up, "faked marker, switch on -> upstream");
        Check(OverwatchSpreadCore.Decide(true, true, false, false) == ours, "switch on, hook in -> ours");
        Check(OverwatchSpreadCore.Decide(false, true, false, false) == theirs, "switch off -> theirs");
        Check(OverwatchSpreadCore.Decide(true, false, false, false) == theirs, "hook missing -> theirs");

        Check(!ErrorTrapSpreadCore.SliceFull(0, 0), "empty slice");
        Check(!ErrorTrapSpreadCore.SliceFull(ErrorTrapSpreadCore.kMaxItemsPerSlice - 1, ErrorTrapSpreadCore.kSliceUs - 1), "slice under both budgets");
        Check(ErrorTrapSpreadCore.SliceFull(0, ErrorTrapSpreadCore.kSliceUs), "slice full by time");
        Check(ErrorTrapSpreadCore.SliceFull(ErrorTrapSpreadCore.kMaxItemsPerSlice, 0), "slice full by items");
        // the pass loop: inventories of these sizes, the budget checked before each; only an inventory bigger than the
        // budget on its own (ErrorTrap's check runs it whole) may put a tick over it
        int[] sizes = { 3, 900, 40, 40, 40, 40, 1, 1, 200, 5 };
        int inTick = 0, ticks = 1;
        foreach (int size in sizes)
        {
            if (ErrorTrapSpreadCore.SliceFull(inTick, 0)) { ticks++; inTick = 0; }
            Check(inTick == 0 || inTick + size <= ErrorTrapSpreadCore.kMaxItemsPerSlice + size, "budget checked before each inventory");
            inTick += size;
            Check(inTick < ErrorTrapSpreadCore.kMaxItemsPerSlice + size, "a tick goes over the budget only by its last inventory");
        }
        Check(ticks >= 4, "the 900-item and 200-item inventories each end a tick (" + ticks + " ticks)");

        Console.WriteLine(sFails == 0 ? "errortrapspread selftest: PASS" : "errortrapspread selftest: " + sFails + " FAIL");
        return sFails == 0 ? 0 : 1;
    }
}
