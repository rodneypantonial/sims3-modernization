using System;
using System.Reflection;

namespace Ts3Fixes
{
    // Game-independent parts of OverwatchSpread.cs (docs/145), kept apart for the offline self-check
    // (tools/selftest/overwatchspread).
    public static class OverwatchSpreadCore
    {
        // Public static int on NRaas.Overwatch in Overwatch builds that spread the nightly cleanups themselves
        // (docs/data/upstream/nraas-overwatch-staggered-cleanup.patch). Present = their spreading wins, ours stays off.
        public const string MarkerField = "StaggeredCleanupVersion";

        public const int kGapMinutes = 10;   // sim-minutes between two cleanup types (same default as the upstream patch)
        public const int kMaxItemsPerSlice = 128; // inventory pass: a tick ends after this many inventory items or EaSpreadCore.kSliceUs
        public const int kMaxScanPerSlice = 512;  // world scan for inventories: objects checked per tick (or EaSpreadCore.kSliceUs)
        public const int kSlowTypeUs = 50000;     // an Overwatch type run whole at its slot that takes this long gets a log line

        public enum Mode { Theirs, Ours, Upstream, Chain }

        // Who runs tonight's cleanups. Ours only with the switch on, our hook in place and no spreading Overwatch build.
        public static Mode Decide(bool switchOn, bool hookReady, bool upstreamMarker, bool fakeUpstream)
        {
            if (upstreamMarker || fakeUpstream) return Mode.Upstream;
            return switchOn && hookReady ? Mode.Ours : Mode.Theirs;
        }

        // "overwatchchain" (docs/145 §10): Overwatch's types one tick apart in its own alarm task. Only where Overwatch's own
        // path would run: the upstream marker and "overwatchspread" both win over it.
        public static Mode WithChain(Mode decided, bool chainOn, bool hookReady)
        {
            return decided == Mode.Theirs && chainOn && hookReady ? Mode.Chain : decided;
        }

        // Only looks for the field: reading its value would run Overwatch's static constructor before the game does.
        public static bool HasMarker(Type overwatch) { return HasMarker(overwatch, MarkerField); }

        public static bool HasMarker(Type owner, string field)
        {
            if (owner == null) return false;
            FieldInfo f = owner.GetField(field, BindingFlags.Public | BindingFlags.Static);
            return f != null && f.FieldType == typeof(int);
        }

        // Marker version from which Overwatch's Stuck Check sleeps a frame after each route check itself (upstream patch v2):
        // then overwatchstuck still counts but adds no yield of its own. 0 = no marker or not read yet, -1 = faked.
        public const int kStuckYieldVersion = 2;
        public static bool OurStuckYield(int upstreamVersion) { return upstreamVersion < kStuckYieldVersion; }

        public static float SlotMinutes(int index, int gapMinutes) { return index * (float)gapMinutes; }

        // Chain (docs/145 §11): what the PerformAlarm hook does for the index-th type of Overwatch's own loop.
        // A tick between types only where a yield is allowed; otherwise the loop stays Overwatch's one-tick loop.
        public static bool SleepBefore(int index, bool mayYield) { return index > 0 && mayYield; }

        public enum TypeRun { Theirs, OursInventory, Nothing }

        // Our sliced inventory pass sleeps, so it replaces Overwatch's inventory cleanup only where a yield is allowed;
        // with the player's setting off Overwatch's own option would return at once, so nothing runs.
        public static TypeRun ChainType(bool isInventory, bool inventorySettingOn, bool mayYield)
        {
            if (!isInventory) return TypeRun.Theirs;
            if (!inventorySettingOn) return TypeRun.Nothing;
            return mayYield ? TypeRun.OursInventory : TypeRun.Theirs;
        }

    }
}
