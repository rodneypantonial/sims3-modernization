using System;
using System.Collections.Generic;
using Ts3Fixes;

// Offline self-check of DeadRefsCore (issue #91): random holders with random gone entries; after a drop no gone entry
// is left, every live entry is kept in order, counts match, and arrays come back null when nothing was gone.
static class DeadRefsTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; if (sFails < 20) Console.WriteLine("FAIL " + what); } }

    static void Main()
    {
        Random rnd = new Random(91);
        for (int round = 0; round < 3000; round++)
        {
            int n = rnd.Next(0, 40);
            HashSet<int> gone = new HashSet<int>();
            List<int> list = new List<int>();
            int[] arr = new int[n];
            Dictionary<int, string> dict = new Dictionary<int, string>();
            for (int i = 0; i < n; i++)
            {
                list.Add(i); arr[i] = i; dict[i] = "v" + i;
                if (rnd.Next(4) == 0) gone.Add(i);
            }
            Predicate<int> isGone = delegate(int v) { return gone.Contains(v); };

            int dl = DeadRefsCore.DropAll(list, isGone);
            Check(dl == gone.Count && list.Count == n - gone.Count, "list count " + round);
            for (int i = 1; i < list.Count; i++) Check(list[i - 1] < list[i], "list order kept " + round);
            foreach (int v in list) Check(!gone.Contains(v), "list has no gone entry " + round);

            int removed;
            int[] copy = DeadRefsCore.Without(arr, isGone, out removed);
            Check(removed == gone.Count, "array count " + round);
            if (gone.Count == 0) Check(copy == null, "nothing gone = null " + round);
            else
            {
                Check(copy != null && copy.Length == n - gone.Count, "array length " + round);
                foreach (int v in copy) Check(!gone.Contains(v), "array has no gone entry " + round);
                Check(arr.Length == n, "old array untouched " + round);
            }

            int dk = DeadRefsCore.DropKeys(dict, isGone);
            Check(dk == gone.Count && dict.Count == n - gone.Count, "dict count " + round);
            foreach (int k in dict.Keys) Check(!gone.Contains(k), "dict has no gone key " + round);
        }
        Check(DeadRefsCore.DropAll<int>(null, delegate { return true; }) == 0, "null list");
        int r0;
        Check(DeadRefsCore.Without<int>(null, delegate { return true; }, out r0) == null && r0 == 0, "null array");
        Check(DeadRefsCore.DropKeys<int, int>(null, delegate { return true; }) == 0, "null dict");
        Check(!DeadRefsCore.LooksLikeLoad(15, 15), "small holder never refused");
        Check(DeadRefsCore.LooksLikeLoad(60, 100), "most gone = load signature");
        Check(!DeadRefsCore.LooksLikeLoad(50, 100), "half gone is still acted on");
        Check(!DeadRefsCore.LooksLikeLoad(7, 103), "real leftovers acted on");

        Console.WriteLine(sFails == 0 ? "deadrefs selftest: PASS" : "deadrefs selftest: FAIL " + sFails);
        Environment.Exit(sFails == 0 ? 0 : 1);
    }
}
