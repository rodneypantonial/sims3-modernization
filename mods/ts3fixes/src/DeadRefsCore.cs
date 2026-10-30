using System;
using System.Collections.Generic;

namespace Ts3Fixes
{
    // Game-independent part of deadrefsweep (DeadRefs.cs, issue #91): dropping gone entries from the holders' lists,
    // arrays and dictionaries, and the guard that refuses a pass that looks like a load-time mistake (every description
    // read from a save is invalid until SimDescription.Fixup runs). Self-check: tools/selftest/deadrefs.
    public static class DeadRefsCore
    {
        // Entries for which gone() is true, removed in place; returns how many.
        public static int DropAll<T>(List<T> list, Predicate<T> gone)
        {
            if (list == null) return 0;
            return list.RemoveAll(gone);
        }

        // A copy without the gone entries, or null when nothing is gone (the caller keeps the old array).
        public static T[] Without<T>(T[] items, Predicate<T> gone, out int removed)
        {
            removed = 0;
            if (items == null) return null;
            List<T> keep = new List<T>(items.Length);
            foreach (T t in items) { if (gone(t)) removed++; else keep.Add(t); }
            return removed == 0 ? null : keep.ToArray();
        }

        // Keys for which gone() is true, removed; returns how many.
        public static int DropKeys<K, V>(Dictionary<K, V> d, Predicate<K> gone)
        {
            if (d == null) return 0;
            List<K> drop = new List<K>();
            foreach (K k in d.Keys) if (gone(k)) drop.Add(k);
            foreach (K k in drop) d.Remove(k);
            return drop.Count;
        }

        // True when a pass should not act: more than half of a large holder gone is what a sweep run before
        // SimDescription.Fixup sees (every household member invalid), not real leftovers.
        public static bool LooksLikeLoad(int gone, int total)
        {
            return total >= 16 && gone * 2 > total;
        }
    }
}
