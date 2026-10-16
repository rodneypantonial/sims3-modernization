using System;
using System.Reflection;

namespace Ts3Fixes
{
    // Faster object identity table for the save serializer (docs/15 "Where the save time goes": ~2.5 s of a ~12 s save is
    // ScriptCore.ReferenceHashtable.TryGetValue/TryAdd, the PersistWriter's mVisited/mGroups, object -> index). The game's
    // table stops growing at 16384 buckets, so at 190k-300k objects each lookup walks a long chain, and each hash is the
    // object's virtual GetHashCode (interpreted for game overrides). The hooks keep the game's fields and numbering
    // (TryAdd index = ++mElementCount, Add does not count, AddDuplicateItem/Count untouched) and swap the bucket array
    // for a FastRefTable (FastRefTable.cs): same indexes, so the save file bytes stay the same.
    //
    // Field access: a swapped method body runs with `this` = the real ReferenceHashtable. RefTableHook declares the same
    // two instance fields with the same kinds in the same order (one reference, one int), so Mono lays both classes out
    // alike (fields after the 8-byte object header) and the hook's ldfld/stfld hit the real fields; the interpreter does not
    // type-check `this` on field access [inferred]. Reflection per call would cost more than the chain walk it saves.
    // The converted table lives in mHashBuckets itself (declared object here; the interpreter does not check the type on
    // stfld [inferred]). Nothing ever stores into a Node[] (stelem of a foreign type would throw ArrayTypeMismatch). Both
    // [inferred] points are confirmed at startup by ProbeRefTable on real instances (through the game's PersistWriter;
    // count, bucket object, indexes, Clear and the original path checked via the real fields); if anything is off,
    // sRefTableOk stays false and every hook runs the game's own method without touching the fields.
    //
    // Formats never mix within one instance: an instance is converted only by TryAdd/Add while truly empty (count 0, all
    // buckets null) and the switch is on, and once converted it stays converted (Clear resets it in place). The game makes
    // a new PersistWriter (two tables) per save and per cached object, so flipping the switch takes effect at the next save.
    // ContainsKey goes through TryGetValue; Rehash is only called by the original Add, so it runs only on unconverted
    // instances and is not replaced.
    // Switch "fastreftable" (tuning kFastRefTable, default off). Counters: [68] tables converted, [69] most entries in one
    // table (recorded on grow and Clear), [70] table grows.
    public static partial class Fixes
    {
        static bool sRefTableOk;     // all five patches in and the startup probe passed
        static int sRefTableForce;   // probe only: 1 converts regardless of the switch, -1 never converts

        static void ApplyRefTable()
        {
            Type t = typeof(ScriptCore.PersistWriter).Assembly.GetType("ScriptCore.ReferenceHashtable");
            if (t == null) { AppliedLog().Add("ReferenceHashtable: not found, skipped"); return; }
            bool ok = Patch(t, "get_Item", null, typeof(RefTableHook), "ItemHook", "ItemOrig")
                    & Patch(t, "Clear", null, typeof(RefTableHook), "ClearHook", "ClearOrig")
                    & Patch(t, "TryGetValue", null, typeof(RefTableHook), "TryGetValueHook", "TryGetValueOrig")
                    & Patch(t, "TryAdd", null, typeof(RefTableHook), "TryAddHook", "TryAddOrig")
                    & Patch(t, "Add", null, typeof(RefTableHook), "AddHook", "AddOrig");
            if (!ok) { AppliedLog().Add("ReferenceHashtable: not all methods patched, fast table off"); return; }
            int[] c = PatchCounters();
            FastRefTable.sCounters = c;
            FastRefTable.sMaxSlot = 69;
            FastRefTable.sGrowSlot = 70;
            string why;
            try { why = ProbeRefTable(t); }
            catch (Exception e) { why = "failed at " + sRefTableStep + ": " + e; } // ToString: type, message, inner exceptions, stack
            finally { sRefTableForce = 0; c[68] = c[69] = c[70] = 0; }
            sRefTableOk = why == null;
            AppliedLog().Add("ReferenceHashtable probe: " + (why ?? "ok"));
        }

        // Drives the hooks through the game's own PersistWriter (Begin, AddObject, AddGroup, Dispose), the same calls a save
        // makes, and reads the real fields through reflection. Reflection cannot call a swapped method: Mono's InternalInvoke
        // checks the target against the method's class, which the swap set to RefTableHook ("Unable to invoke an invalid
        // target", seen in game 2026-10-14); interpreted calls have no such check. ContainsKey is not swapped, so it is
        // invoked directly (it calls TryGetValue). The indexer has no caller in ScriptCore and is not probed (same field reads
        // as TryGetValue). Boxed ints persist as Int32, so AddObject goes straight to TryGetValue/TryAdd with no class lookup.
        // Null when all checks pass; sRefTableStep names the step that threw.
        static string sRefTableStep;
        static string ProbeRefTable(Type t)
        {
            const BindingFlags inst = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            Type pwType = typeof(ScriptCore.PersistWriter);
            FieldInfo fb = t.GetField("mHashBuckets", inst), fc = t.GetField("mElementCount", inst),
                      fVisited = pwType.GetField("mVisited", inst), fGroups = pwType.GetField("mGroups", inst);
            MethodInfo contains = t.GetMethod("ContainsKey", inst);
            if (fb == null || fc == null || fVisited == null || fGroups == null || contains == null) return "fields or methods not found";
            object a = 1, b = 2, g = new object(), h = new object();
            sRefTableOk = true;

            sRefTableStep = "original path";
            sRefTableForce = -1;
            ScriptCore.PersistWriter orig = new ScriptCore.PersistWriter();
            object ov = fVisited.GetValue(orig), og = fGroups.GetValue(orig);
            orig.Begin();
            int o1 = orig.AddObject(a), o2 = orig.AddObject(a), o3 = orig.AddObject(b);
            orig.AddGroup(g);
            if ((int)fc.GetValue(ov) != 3 || fb.GetValue(ov) is FastRefTable || fb.GetValue(og) is FastRefTable) return "original path state";
            if (!(bool)contains.Invoke(ov, new object[] { b }) || !(bool)contains.Invoke(og, new object[] { g })) return "original lookup";
            sRefTableStep = "group table with entries";
            sRefTableForce = 1;
            orig.AddGroup(h); // mGroups holds g with count 0: must stay unconverted
            if (fb.GetValue(og) is FastRefTable || !(bool)contains.Invoke(og, new object[] { h })) return "converted a non-empty table";

            sRefTableStep = "fast path";
            ScriptCore.PersistWriter pw = new ScriptCore.PersistWriter();
            object v = fVisited.GetValue(pw), gr = fGroups.GetValue(pw);
            pw.Begin();
            int f1 = pw.AddObject(a);
            if (!(fb.GetValue(v) is FastRefTable)) return "bucket field (layout)";
            if ((int)fc.GetValue(v) != 1) return "count field (layout)";
            int f2 = pw.AddObject(a), f3 = pw.AddObject(b); // a again: the game's AddDuplicateItem bumps the real count
            if (f1 != o1 || f2 != o2 || f3 != o3 || (int)fc.GetValue(v) != 3) return "indexes " + f1 + "," + f2 + "," + f3 + " vs " + o1 + "," + o2 + "," + o3;
            pw.AddGroup(g);
            if (!(fb.GetValue(gr) is FastRefTable) || (int)fc.GetValue(gr) != 0) return "group Add";
            if (!(bool)contains.Invoke(v, new object[] { b }) || !(bool)contains.Invoke(gr, new object[] { g }) || (bool)contains.Invoke(gr, new object[] { h })) return "fast lookup";
            sRefTableStep = "Clear";
            pw.Dispose();
            if ((int)fc.GetValue(v) != 0 || !(fb.GetValue(v) is FastRefTable) || (bool)contains.Invoke(v, new object[] { a })) return "Clear";
            sRefTableStep = null;
            return null;
        }

        // Swapped into ScriptCore.ReferenceHashtable. Instance fields must mirror the game's class exactly (see above): add
        // no other instance fields here.
        public class RefTableHook
        {
            object mHashBuckets;  // game: Node[]; a FastRefTable once converted
            int mElementCount;

            public int ItemHook(object obj)
            {
                if (obj == null) return 0;
                FastRefTable f = sRefTableOk ? mHashBuckets as FastRefTable : null;
                if (f == null) return ItemOrig(obj);
                int index;
                if (f.TryGet(obj, out index)) return index;
                throw new ArgumentException("Object not found.");
            }

            public void ClearHook()
            {
                FastRefTable f = sRefTableOk ? mHashBuckets as FastRefTable : null;
                if (f == null) { ClearOrig(); return; }
                f.Reset();
                mElementCount = 0;
            }

            public bool TryGetValueHook(object obj, out int index)
            {
                FastRefTable f = sRefTableOk ? mHashBuckets as FastRefTable : null;
                if (f == null) return TryGetValueOrig(obj, out index);
                return f.TryGet(obj, out index);
            }

            public bool TryAddHook(object obj, out int index)
            {
                FastRefTable f = sRefTableOk ? mHashBuckets as FastRefTable : null;
                if (f == null && (f = Convert()) == null) return TryAddOrig(obj, out index);
                int next = mElementCount + 1;
                if (!f.TryAdd(obj, next, out index)) return false;
                mElementCount = next;
                return true;
            }

            public void AddHook(object obj, int index)
            {
                FastRefTable f = sRefTableOk ? mHashBuckets as FastRefTable : null;
                if (f == null && (f = Convert()) == null) { AddOrig(obj, index); return; }
                f.Set(obj, index);
            }

            // A FastRefTable in place of the buckets when this instance is still truly empty and the switch is on.
            FastRefTable Convert()
            {
                if (!sRefTableOk || mElementCount != 0 || sRefTableForce < 0) return null;
                if (sRefTableForce == 0 && !ShortcutOn("fastreftable")) return null;
                object[] buckets = mHashBuckets as object[]; // Node[] is an object[] (array covariance); read only
                if (buckets == null) return null;
                for (int i = 0; i < buckets.Length; i++) if (buckets[i] != null) return null; // Add entries (Add does not count)
                FastRefTable f = new FastRefTable();
                mHashBuckets = f;
                PatchCounters()[68]++;
                return f;
            }

            // receive the original headers
            public int ItemOrig(object obj) { return 0; }
            public void ClearOrig() { }
            public bool TryGetValueOrig(object obj, out int index) { index = 0; return false; }
            public bool TryAddOrig(object obj, out int index) { index = 0; return false; }
            public void AddOrig(object obj, int index) { }
        }
    }
}
