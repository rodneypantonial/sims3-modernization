// Usage: node tools/selftest/typecache/run.ts  (compiles this with mods/ts3fixes/src/TypeCache.cs)
// Offline self-check of the serializer type cache (CachedReflection.cs hooks, TypeCache.cs). A model of
// PersistedClassDB.GetPersistedClass / Persist.GetPersistedTypeCode with the game's side effects (class entry created,
// Init once with the class marked tested before it is decided, class index on first requireClassReference, field list
// built once, nested calls from Init and from the field list) runs the same random call sequences plain and through
// wrappers that do exactly what the hooks do. Every answer and the class index order must match. A second run without the
// Init guard (TypeCache.sBusy) must find a mismatch, so the guard is shown to matter. Exit code 1 on failure.
using System;
using System.Collections.Generic;
using Ts3Fixes;

class Key { public int Id; public override int GetHashCode() { return Id % 3; } } // stands for a Type

class ModelClass { public bool Tested, Persistable, FieldsBuilt; public int Index = -1; }

class Model
{
    public readonly Key[] Keys;
    public readonly Dictionary<Key, ModelClass> Classes = new Dictionary<Key, ModelClass>();
    public readonly List<int> InOrder = new List<int>();
    public bool Cached, Guard;
    public TypeCache Cache;
    public Model(Key[] keys, bool cached, bool guard) { Keys = keys; Cached = cached; Guard = guard; }

    static bool IsEnum(Key k) { return k.Id % 5 == 0; }
    static bool Persistable(Key k) { return k.Id % 7 != 3; }
    Key Dep(Key k, int n) { return Keys[(k.Id * 31 + n * 17) % Keys.Length]; }

    // ---- the game's methods, modelled
    ModelClass GetClassOrig(Key k, bool req)
    {
        ModelClass c;
        if (!Classes.TryGetValue(k, out c)) { c = new ModelClass(); Classes.Add(k, c); }
        if (!c.Tested) Init(k, c);
        if (!c.Persistable) return null;
        if (req)
        {
            if (c.Index < 0) { c.Index = InOrder.Count; InOrder.Add(k.Id); }
            if (!c.FieldsBuilt)
            {
                for (int i = 0; i < 3; i++) { int f; Code(Dep(k, i), out f); } // field types, in order
                c.FieldsBuilt = true;
            }
        }
        return c;
    }

    void Init(Key k, ModelClass c)
    {
        if (Guard) TypeCache.sBusy++;
        try
        {
            c.Tested = true;                      // marked tested before decided, as PersistedClass.Init
            if (k.Id % 4 == 1) GetClass(k, false); // nested call sees the half-built class (null)
            if (k.Id % 3 == 2) { int f; Code(Dep(k, 7), out f); } // a generic argument check (BCLDefaultFormatter)
            c.Persistable = Persistable(k);
            if (IsEnum(k)) c.FieldsBuilt = true; // enums get an empty field list in Init, as the game's
        }
        finally { if (Guard) TypeCache.sBusy--; }
    }

    int CodeOrig(Key k, out int flags)
    {
        flags = k.Id & 0x7FF;
        if (IsEnum(k)) return GetClass(k, true) != null ? 23 : 0;
        return GetClass(k, false) != null ? 16 : 0;
    }

    // ---- entry points: the hooks when cached
    public object GetClass(Key k, bool req)
    {
        if (!Cached) return GetClassOrig(k, req);
        object pc;
        if (Cache.TryClass(k, req, out pc)) return pc;
        pc = GetClassOrig(k, req);
        Cache.StoreClass(k, req, pc);
        return pc;
    }

    public int Code(Key k, out int flags)
    {
        if (!Cached) return CodeOrig(k, out flags);
        int code;
        if (Cache.TryCode(k, out code, out flags)) return code;
        code = CodeOrig(k, out flags);
        Cache.StoreCode(k, code, flags);
        return code;
    }
}

static class Program
{
    // Runs one sequence on a plain and a cached model; true when every answer and the index order match.
    static bool Same(int seed, int types, int calls, bool guard, out string why)
    {
        Key[] keys = new Key[types];
        for (int i = 0; i < types; i++) keys[i] = new Key { Id = i };
        Model plain = new Model(keys, false, true), cached = new Model(keys, true, guard);
        cached.Cache = new TypeCache(cached, true);
        Random r = new Random(seed);
        for (int n = 0; n < calls; n++)
        {
            Key k = keys[r.Next(types)];
            int op = r.Next(3);
            string a, b;
            if (op == 2)
            {
                int fa, fb;
                int ca = plain.Code(k, out fa), cb = cached.Code(k, out fb);
                a = ca + "/" + fa; b = cb + "/" + fb;
            }
            else
            {
                object pa = plain.GetClass(k, op == 1), pb = cached.GetClass(k, op == 1);
                a = pa == null ? "null" : "class " + ((ModelClass)pa).Index;
                b = pb == null ? "null" : "class " + ((ModelClass)pb).Index;
            }
            if (a != b) { why = "call " + n + " op " + op + " type " + k.Id + ": " + a + " vs " + b; return false; }
        }
        if (string.Join(",", plain.InOrder) != string.Join(",", cached.InOrder)) { why = "class index order differs"; return false; }
        why = null;
        return true;
    }

    static int Main()
    {
        int fails = 0;
        foreach (int types in new[] { 7, 60, 3000 })
            for (int seed = 1; seed <= 40; seed++)
            {
                string why;
                if (!Same(seed, types, types * 6, true, out why)) { Console.WriteLine("FAIL types " + types + " seed " + seed + ": " + why); fails++; }
            }
        if (TypeCache.sBusy != 0) { Console.WriteLine("FAIL sBusy left at " + TypeCache.sBusy); fails++; }
        int caught = 0;
        for (int seed = 1; seed <= 40; seed++) { string why; if (!Same(seed, 60, 360, false, out why)) caught++; }
        if (caught == 0) { Console.WriteLine("FAIL the model never needs the Init guard: test too weak"); fails++; }
        // table: many keys, grow, identity (colliding GetHashCode), both answer kinds on one key
        TypeCache t = new TypeCache(null, true);
        Key[] many = new Key[20000];
        for (int i = 0; i < many.Length; i++) { many[i] = new Key { Id = i }; t.StoreCode(many[i], i & 0xFF, i & 0x7FF); if ((i & 1) == 0) t.StoreClass(many[i], (i & 2) == 0, (i & 4) == 0 ? null : many[i]); }
        for (int i = 0; i < many.Length; i++)
        {
            int code, flags; object pc;
            if (!t.TryCode(many[i], out code, out flags) || code != (i & 0xFF) || flags != (i & 0x7FF)) { Console.WriteLine("FAIL code " + i); fails++; break; }
            bool even = (i & 1) == 0, req = (i & 2) == 0, isNull = (i & 4) == 0;
            bool gotAny = t.TryClass(many[i], false, out pc);
            if (gotAny != even || (even && pc != (isNull ? null : many[i]))) { Console.WriteLine("FAIL class any " + i); fails++; break; }
            bool gotRef = t.TryClass(many[i], true, out pc);
            if (gotRef != (even && (req || isNull))) { Console.WriteLine("FAIL class ref " + i); fails++; break; }
        }
        int c2, f2; object p2;
        if (t.TryCode(new Key { Id = 5 }, out c2, out f2) || t.TryClass(new Key { Id = 5 }, false, out p2)) { Console.WriteLine("FAIL equal-but-other key hit"); fails++; }
        TypeCache.sBusy = 1; t.StoreCode(many[0], 1, 1); TypeCache.sBusy = 0;
        int c3, f3; t.TryCode(many[0], out c3, out f3);
        if (c3 != 0) { Console.WriteLine("FAIL stored while busy"); fails++; }
        t.StoreCode(new Key { Id = -1 }, 1, 0x10000);
        Console.WriteLine(fails == 0 ? "typecache selftest: ok (120 sequences match; guard-off caught in " + caught + "/40)" : "typecache selftest: " + fails + " failures");
        return fails == 0 ? 0 : 1;
    }
}
