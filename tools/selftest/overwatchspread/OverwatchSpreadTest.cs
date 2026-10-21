using System;
using System.Collections.Generic;
using Ts3Fixes;

// Offline self-check of OverwatchSpreadCore (docs/145): who runs the nightly cleanups, the upstream marker, slots, slices.
static class OverwatchSpreadTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL " + what); } }

    // Stand-ins for NRaas.Overwatch with and without the upstream marker.
    class OverwatchWithMarker { public static readonly int StaggeredCleanupVersion = 1; }
    class OverwatchWithoutMarker { public static readonly int kSomethingElse = 1; }
    class OverwatchWrongMarker { public static readonly string StaggeredCleanupVersion = "1"; }
    class OverwatchPrivateMarker { static readonly int StaggeredCleanupVersion = 1; }

    sealed class Item { public int Id; public bool Broken; public int Checks; }

    // Model of our inventory pass with the budget counted in items: a 700-item inventory in 3 stacks plus small ones, items
    // checked last to first, broken ones taken out, stacks left empty removed at the inventory's end. Without changes the
    // result equals the one-shot check; with the game adding and taking items while we sleep, no item is checked twice,
    // an item taken away is never checked after, and items added after the turn are left for the next night.
    static void GiantInventory(bool gameChangesMidSplit)
    {
        List<List<List<Item>>> inventories = new List<List<List<Item>>>(), reference = new List<List<List<Item>>>();
        int[][] shapes = { new int[] { 400, 250, 50 }, new int[] { 3 }, new int[] { 0, 2 } };
        int id = 0;
        foreach (int[] shape in shapes)
        {
            List<List<Item>> inv = new List<List<Item>>(), copy = new List<List<Item>>();
            foreach (int size in shape)
            {
                List<Item> stack = new List<Item>(), refStack = new List<Item>();
                for (int i = 0; i < size; i++)
                {
                    Item it = new Item(); it.Id = id++; it.Broken = it.Id % 5 == 0 || (size == 50); stack.Add(it);
                    Item r = new Item(); r.Id = it.Id; r.Broken = it.Broken; refStack.Add(r);
                }
                inv.Add(stack);
                copy.Add(refStack);
            }
            inventories.Add(inv);
            reference.Add(copy);
        }
        foreach (List<List<Item>> inv in reference) // one-shot reference
        {
            foreach (List<Item> s in inv) for (int i = s.Count - 1; i >= 0; i--) if (s[i].Broken) s.RemoveAt(i);
            inv.RemoveAll(delegate(List<Item> s) { return s.Count == 0; });
        }
        List<Item> taken = new List<Item>(), added = new List<Item>();
        DailyPairPass<List<List<Item>>, Job> pass = new DailyPairPass<List<List<Item>>, Job>();
        pass.Begin(inventories, delegate(List<List<Item>> inv)
        {
            List<Job> u = new List<Job>();
            List<List<Item>> stacks = new List<List<Item>>(inv);
            foreach (List<Item> stack in stacks)
            {
                List<Item> s = stack;
                EaSpreadCore.AddItemUnits<Item>(u, delegate { return s.Count; }, delegate(int i) { return s[i]; },
                    delegate(int i) { Check(!taken.Contains(s[i]), "a taken item is never checked"); s[i].Checks++; if (s[i].Broken) s.RemoveAt(i); },
                    delegate { return false; });
            }
            u.Add(delegate { foreach (List<Item> s in stacks) if (s.Count == 0) inv.Remove(s); });
            return u;
        }, delegate(List<List<Item>> inv, Job unit) { unit(); return true; }, delegate { });
        int slices = 0, units;
        bool done = false;
        while (!done)
        {
            done = pass.Slice(OverwatchSpreadCore.kMaxItemsPerSlice, 2000, delegate { return 0; }, out units);
            Check(units <= OverwatchSpreadCore.kMaxItemsPerSlice, "slice within the item budget");
            if (++slices == 3 && gameChangesMidSplit)
            {
                List<Item> big = inventories[0][0];
                for (int i = 0; i < 40; i += 4) { taken.Add(big[i]); }
                foreach (Item t in taken) big.Remove(t);                                       // the game takes items
                for (int i = 0; i < 5; i++) { Item a = new Item(); a.Id = 100000 + i; big.Insert(0, a); added.Add(a); } // and adds some
            }
        }
        Check(slices >= 700 / OverwatchSpreadCore.kMaxItemsPerSlice, "the big inventory took >= 5 slices (" + slices + ")");
        foreach (List<List<Item>> inv in inventories)
            foreach (List<Item> s in inv)
                foreach (Item it in s) Check(it.Checks == (added.Contains(it) ? 0 : 1), "item " + it.Id + " checked " + it.Checks + " times");
        if (gameChangesMidSplit) return;
        for (int k = 0; k < inventories.Count; k++)
        {
            Check(inventories[k].Count == reference[k].Count, "inventory " + k + ": same stacks left as the one-shot check");
            for (int s = 0; s < Math.Min(inventories[k].Count, reference[k].Count); s++)
            {
                Check(inventories[k][s].Count == reference[k][s].Count, "inventory " + k + " stack " + s + ": same items left");
                for (int i = 0; i < Math.Min(inventories[k][s].Count, reference[k][s].Count); i++)
                    Check(inventories[k][s][i].Id == reference[k][s][i].Id, "same item order");
            }
        }
    }

    static int Main()
    {
        OverwatchSpreadCore.Mode ours = OverwatchSpreadCore.Mode.Ours, theirs = OverwatchSpreadCore.Mode.Theirs, up = OverwatchSpreadCore.Mode.Upstream;
        // yield path: the marker (real or faked) wins over everything, also over a switch that is on
        Check(OverwatchSpreadCore.Decide(true, true, true, false) == up, "marker present, switch on -> upstream");
        Check(OverwatchSpreadCore.Decide(true, true, false, true) == up, "faked marker, switch on -> upstream");
        Check(OverwatchSpreadCore.Decide(false, false, true, false) == up, "marker present, hook absent -> upstream");
        Check(OverwatchSpreadCore.Decide(true, true, false, false) == ours, "switch on, hook in -> ours");
        Check(OverwatchSpreadCore.Decide(false, true, false, false) == theirs, "switch off -> theirs");
        Check(OverwatchSpreadCore.Decide(true, false, false, false) == theirs, "hook missing -> theirs");

        Check(OverwatchSpreadCore.HasMarker(typeof(OverwatchWithMarker)), "public static int marker found");
        Check(!OverwatchSpreadCore.HasMarker(typeof(OverwatchWithoutMarker)), "no marker");
        Check(!OverwatchSpreadCore.HasMarker(typeof(OverwatchWrongMarker)), "marker of another type ignored");
        Check(!OverwatchSpreadCore.HasMarker(typeof(OverwatchPrivateMarker)), "non-public marker ignored");
        Check(!OverwatchSpreadCore.HasMarker(null), "no Overwatch type");

        // every cleanup type gets its own slot, gap apart, the first at once
        for (int i = 1; i < 30; i++)
            Check(OverwatchSpreadCore.SlotMinutes(i, OverwatchSpreadCore.kGapMinutes) - OverwatchSpreadCore.SlotMinutes(i - 1, OverwatchSpreadCore.kGapMinutes) == OverwatchSpreadCore.kGapMinutes, "slot " + i + " gap");
        Check(OverwatchSpreadCore.SlotMinutes(0, OverwatchSpreadCore.kGapMinutes) == 0f, "first slot at once");
        Check(OverwatchSpreadCore.SlotMinutes(19, OverwatchSpreadCore.kGapMinutes) < 24 * 60, "20 types fit in one sim-day");

        GiantInventory(false);
        GiantInventory(true);

        Console.WriteLine(sFails == 0 ? "overwatchspread selftest: PASS" : "overwatchspread selftest: " + sFails + " FAIL");
        return sFails == 0 ? 0 : 1;
    }
}
