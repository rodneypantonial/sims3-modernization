// Usage: node tools/selftest/reftable/run.ts  (compiles this with mods/ts3fixes/src/FastRefTable.cs, runs it against the game's ScriptCore.dll)
// Runs the game's own ScriptCore.ReferenceHashtable (loaded from game-copy/assemblies, called through reflection-built
// delegates; no game code is copied here) and FastRefTable driven the way the RefTable.cs hooks drive it, side by side on
// the same operations, and checks every returned index, Count, exception and Clear. Then times the save pattern
// (TryGetValue miss + TryAdd per object, then a lookup per object) on both. Exit code 1 on any mismatch.
using System;
using System.Diagnostics;
using System.Reflection;
using System.Reflection.Emit;
using Ts3Fixes;

delegate bool TryFn(object table, object key, out int index);
delegate void AddFn(object table, object key, int index);
delegate int ItemFn(object table, object key);
delegate void VoidFn(object table);
delegate int CountFn(object table);

// Game objects override GetHashCode; many collide. Equality stays reference equality.
class Collider
{
    public int H;
    public override int GetHashCode() { return H; }
}

// FastRefTable plus the element count, exactly as RefTableHook uses them.
class FastSide
{
    public FastRefTable T = new FastRefTable();
    public int Count;
    public bool TryAdd(object o, out int index)
    {
        int next = Count + 1;
        if (!T.TryAdd(o, next, out index)) return false;
        Count = next;
        return true;
    }
    public int Item(object o)
    {
        if (o == null) return 0;
        int index;
        if (T.TryGet(o, out index)) return index;
        throw new ArgumentException("Object not found.");
    }
    public void Clear() { T.Reset(); Count = 0; }
}

static class Program
{
    static TryFn gTryGet, gTryAdd; static AddFn gAdd; static ItemFn gItem; static VoidFn gClear, gDup; static CountFn gCount;
    static int sFailures;

    static Delegate Bind(Type t, string name, Type del)
    {
        MethodInfo m = t.GetMethod(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        MethodInfo inv = del.GetMethod("Invoke");
        ParameterInfo[] ps = inv.GetParameters();
        Type[] types = new Type[ps.Length];
        for (int i = 0; i < ps.Length; i++) types[i] = ps[i].ParameterType;
        DynamicMethod dm = new DynamicMethod(name, inv.ReturnType, types, typeof(Program).Module, true);
        ILGenerator il = dm.GetILGenerator();
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Castclass, t);
        for (int i = 1; i < types.Length; i++) il.Emit(OpCodes.Ldarg, i);
        il.Emit(OpCodes.Call, m);
        il.Emit(OpCodes.Ret);
        return dm.CreateDelegate(del);
    }

    static void Check(bool ok, string what)
    {
        if (ok) return;
        if (++sFailures <= 20) Console.WriteLine("MISMATCH: " + what);
    }

    static string Throws(Action a)
    {
        try { a(); return "none"; } catch (Exception e) { return e.GetType().Name; }
    }

    static int Main(string[] args)
    {
        string dll = args.Length > 0 ? args[0] : "game-copy/assemblies/ScriptCore.dll";
        Type t = Assembly.LoadFrom(dll).GetType("ScriptCore.ReferenceHashtable", true);
        gTryGet = (TryFn)Bind(t, "TryGetValue", typeof(TryFn));
        gTryAdd = (TryFn)Bind(t, "TryAdd", typeof(TryFn));
        gAdd = (AddFn)Bind(t, "Add", typeof(AddFn));
        gItem = (ItemFn)Bind(t, "get_Item", typeof(ItemFn));
        gClear = (VoidFn)Bind(t, "Clear", typeof(VoidFn));
        gDup = (VoidFn)Bind(t, "AddDuplicateItem", typeof(VoidFn));
        gCount = (CountFn)Bind(t, "get_Count", typeof(CountFn));

        const int n = 300000;
        Collider[] objs = new Collider[n];
        for (int i = 0; i < n; i++) objs[i] = new Collider { H = i % 997 };
        object ea = Activator.CreateInstance(t, true);
        FastSide fast = new FastSide();
        int[] counters = new int[3];
        FastRefTable.sCounters = counters; FastRefTable.sMaxSlot = 1; FastRefTable.sGrowSlot = 2;

        for (int round = 0; round < 2; round++) // second round reuses both tables after Clear
        {
            gClear(ea); fast.Clear();
            for (int i = 0; i < n; i++)
            {
                object o = objs[i];
                int a, b;
                bool ra = gTryGet(ea, o, out a), rb = fast.T.TryGet(o, out b);
                Check(ra == rb && a == b, "miss " + i);
                if (i % 11 == 0) { gAdd(ea, o, 0); fast.T.Set(o, 0); }
                else { ra = gTryAdd(ea, o, out a); rb = fast.TryAdd(o, out b); Check(ra == rb && a == b, "TryAdd " + i + ": " + a + " vs " + b); }
                if (i % 5 == 0) { gDup(ea); fast.Count++; }
                if (i % 13 == 0) { ra = gTryAdd(ea, o, out a); rb = fast.TryAdd(o, out b); Check(ra == rb && a == b, "TryAdd again " + i); }
            }
            Check(gCount(ea) == fast.Count, "Count " + gCount(ea) + " vs " + fast.Count);
            for (int i = 0; i < n; i += 17) { gAdd(ea, objs[i], -i); fast.T.Set(objs[i], -i); } // Add over an existing key
            for (int i = 0; i < n; i++)
            {
                int a, b;
                bool ra = gTryGet(ea, objs[i], out a), rb = fast.T.TryGet(objs[i], out b);
                Check(ra == rb && a == b, "lookup " + i);
                Check(gItem(ea, objs[i]) == fast.Item(objs[i]), "indexer " + i);
            }
            Collider stranger = new Collider { H = 5 };
            Check(Throws(() => gItem(ea, stranger)) == Throws(() => fast.Item(stranger)), "indexer missing");
            Check(gItem(ea, null) == fast.Item(null), "indexer null");
            int x;
            Check(Throws(() => gTryGet(ea, null, out x)) == Throws(() => fast.T.TryGet(null, out x)), "TryGetValue null");
            Check(Throws(() => gTryAdd(ea, null, out x)) == Throws(() => fast.TryAdd(null, out x)), "TryAdd null");
            Check(Throws(() => gAdd(ea, null, 1)) == Throws(() => fast.T.Set(null, 1)), "Add null");
            Console.WriteLine("round " + round + ": count " + fast.Count + ", entries " + fast.T.Used);
        }
        gClear(ea); fast.Clear();
        int y;
        Check(gCount(ea) == 0 && fast.Count == 0 && !gTryGet(ea, objs[1], out y) && !fast.T.TryGet(objs[1], out y), "after Clear");
        Console.WriteLine("counters: max entries " + counters[1] + ", grows " + counters[2]);

        Time("colliding hashes (997 values)", t, objs);
        for (int i = 0; i < n; i++) objs[i] = new Collider { H = i * 7919 };
        Time("distinct hashes", t, objs);

        Console.WriteLine(sFailures == 0 ? "PASS: identical results" : "FAIL: " + sFailures + " mismatches");
        return sFailures == 0 ? 0 : 1;
    }

    // PersistWriter.GetObjectIndex pattern: TryGetValue miss then TryAdd, then one more lookup per object.
    static void Time(string label, Type t, object[] objs)
    {
        object ea = Activator.CreateInstance(t, true);
        FastSide fast = new FastSide();
        int idx;
        Stopwatch sw = Stopwatch.StartNew();
        foreach (object o in objs) { if (!gTryGet(ea, o, out idx)) gTryAdd(ea, o, out idx); }
        foreach (object o in objs) gTryGet(ea, o, out idx);
        long eaMs = sw.ElapsedMilliseconds;
        sw = Stopwatch.StartNew();
        foreach (object o in objs) { if (!fast.T.TryGet(o, out idx)) fast.TryAdd(o, out idx); }
        foreach (object o in objs) fast.T.TryGet(o, out idx);
        long fastMs = sw.ElapsedMilliseconds;
        Console.WriteLine(label + ", " + objs.Length + " objects: game table " + eaMs + " ms, FastRefTable " + fastMs + " ms (JIT, not the game's interpreter)");
    }
}
