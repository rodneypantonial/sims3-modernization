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

        public static float SlotMinutes(int index, int gapMinutes) { return index * (float)gapMinutes; }

    }
}
