using System;

namespace Ts3Fixes
{
    // Game-independent parts of ErrorTrapSpread.cs (docs/146), kept apart for the offline self-check
    // (tools/selftest/errortrapspread). Who runs the check: OverwatchSpreadCore.Decide (same rule).
    public static class ErrorTrapSpreadCore
    {
        // Public static int on NRaas.ErrorTrap in ErrorTrap builds that spread their load-up scan themselves
        // (docs/data/upstream/nraas-errortrap-spread-scan.patch). Present = their spreading wins, ours stays off.
        public const string MarkerField = "SpreadScanVersion";

        public const int kSliceUs = 2000;          // inventory pass: sleep one tick after this much work...
        public const int kMaxItemsPerSlice = 128;  // ...or once the inventories checked this tick hold this many items

        // Only looks for the field: reading it would run ErrorTrap's static constructor before the game does.
        public static bool HasMarker(Type errorTrap) { return OverwatchSpreadCore.HasMarker(errorTrap, MarkerField); }

        // The budget counts inventory items, not inventories. ErrorTrap's own check runs one inventory whole, so an inventory
        // bigger than the budget is still one tick (upstream work, docs/146 §2.1).
        public static bool SliceFull(int items, long elapsedUs) { return items >= kMaxItemsPerSlice || elapsedUs >= kSliceUs; }
    }
}
