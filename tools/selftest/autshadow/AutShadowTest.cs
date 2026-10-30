// Usage: node tools/selftest/autshadow/run.ts  (compiles this with mods/ts3fixes/src/AutShadowCore.cs)
// Offline self-check of AutShadowCore.cs: ULP distance, the Mode R pair classes A0-A4, the Mode W L report and the
// exact-signature L3 lookup, on fixed cases. Exit code 1 on failure.
using System;
using System.Reflection;
using Ts3Fixes;

class Overloads
{
    public virtual float Score(int a) { return a; }
    public float Score(string a) { return 0f; }
    float Score(int a, ref float b) { return b; }
}

class Derived : Overloads { public override float Score(int a) { return 1f; } }

// docs/128 G-M4 / G-M5 fixtures: "game" types and "Mod" types (isMod = name starts with Mod)
namespace Gm
{
    public class Host
    {
        public class Nested
        {
            public Nested(bool b) { }
            public int Go(ref int a, System.Collections.Generic.Dictionary<string, System.Collections.Generic.List<ulong>> d, object[] arr, float[,] m) { return a; }
        }
        public virtual bool InUse { get { return false; } }
        public virtual void Plain() { }
        public virtual void Unlisted() { }
    }
    public class Mid : Host { }
    public interface IThing { int Count(); void Other(); }
    public class GameImpl : IThing { public int Count() { return 0; } public void Other() { } }
    public class ModObj : Mid, IThing
    {
        public override bool InUse { get { return true; } }
        public new virtual void Plain() { }   // newslot: not an override
        public override void Unlisted() { }   // overrides a virtual no closure calls
        int IThing.Count() { return 1; }
        public void Other() { }
    }
    public class ModFromGame : GameImpl { }  // the interface bodies are the game base's
    public class ModQuiet { public virtual void Plain() { } }
    // docs/122 §14.13: definition-receiver slots (IDefLike / GameDef stand in for InteractionDefinition)
    public interface IDefLike { bool Test(); }
    public class GameDef : IDefLike { public virtual bool Test() { return true; } }
    public class ModOwnDef : IDefLike { public bool Test() { return false; } }      // a mod's own, unrelated definition
    public class ModSubDef : GameDef { public override bool Test() { return false; } }  // derives an allow-listed one
}

class Probe : AutShadowCore.IRainProbe
{
    public bool Seasons, Sim, Outside; public int W, Calls;
    public bool SeasonsOn() { Calls++; return Seasons; }
    public int Weather() { Calls += 10; return W; }
    public bool TargetIsSim() { Calls += 100; return Sim; }
    public bool TargetOutside() { Calls += 1000; return Outside; }
}

static class AutShadowTest
{
    static int sFails;
    static void Check(bool ok, string what) { if (!ok) { sFails++; Console.WriteLine("FAIL: " + what); } }

    static float Next(float f) { return BitConverter.ToSingle(BitConverter.GetBytes(AutShadowCore.Bits(f) + 1), 0); }

    static int Main()
    {
        const int P = AutShadowCore.kPass, F = AutShadowCore.kTestFailed, S = AutShadowCore.kSkip, X = AutShadowCore.kAbort, N = AutShadowCore.kFiltered;
        Check(AutShadowCore.UlpDistance(1f, 1f) == 0, "same float: 0 ULP");
        Check(AutShadowCore.UlpDistance(1f, Next(1f)) == 1, "neighbour: 1 ULP");
        Check(AutShadowCore.UlpDistance(0f, -0f) == 0, "+0 and -0: 0 ULP");
        Check(AutShadowCore.UlpDistance(-float.Epsilon, float.Epsilon) == 2, "across zero: 2 ULP");
        Check(AutShadowCore.UlpDistance(1f, 2f) == 0x800000, "1 to 2: 2^23 ULP");

        Check(AutShadowCore.Classify(P, 1.5f, 0.25f, P, 1.5f, 0.25f) == AutShadowCore.A0, "bit-equal: A0");
        Check(AutShadowCore.Classify(F, 0f, 0.25f, F, 0f, 0.25f) == AutShadowCore.A0, "both failed: A0");
        Check(AutShadowCore.Classify(P, 1.5f, 0f, P, Next(1.5f), 0f) == AutShadowCore.A1, "1 ULP score: A1");
        Check(AutShadowCore.Classify(P, 0f, 0f, P, -0f, 0f) == AutShadowCore.A1, "-0 vs +0: A1 (bits differ)");
        Check(AutShadowCore.Classify(P, 1f, 0.5f, P, 1f, Next(Next(0.5f))) == AutShadowCore.A2, "2 ULP meta: A2");
        Check(AutShadowCore.Classify(P, 1f, 0f, P, 1.1f, 0f) == AutShadowCore.A2, "different score: A2");
        Check(AutShadowCore.Classify(P, 1f, 0f, F, 0f, 0f) == AutShadowCore.A3, "pass vs fail: A3");
        Check(AutShadowCore.Classify(N, 0f, 0f, P, 0f, 0f) == AutShadowCore.A3, "filtered on one side only: A3");
        Check(AutShadowCore.Classify(P, 1f, 0f, X, 0f, 0f) == AutShadowCore.A4, "worker abort: A4");
        Check(AutShadowCore.Classify(P, 1f, 0f, S, 0f, 0f) == AutShadowCore.A4, "worker skip: A4");
        Check(AutShadowCore.Classify(X, 0f, 0f, P, 1f, 0f) == AutShadowCore.A4, "reference abort: A4");
        Check(AutShadowCore.Classify(N, 0f, 0f, N, 0f, 0f) == AutShadowCore.kNoClass, "both filtered: no class");

        float[] g = { 2f, 0f, 5f, 1f, 9f };
        bool[] has = { true, true, true, false, true };
        float[] w = { 2f, 0f, 4f, 7f, 3f };
        int[] c = { P, F, P, P, S };
        AutShadowCore.LReport r = AutShadowCore.Report(g, has, w, c, 5);
        Check(r.Pairs == 3, "L pairs: game-scored and worker pass/fail only");
        Check(r.Equal == 2, "L equal scores");
        Check(r.TestAgree == 3, "L Test agreement (score > 0 both sides)");
        Check(r.Top1Valid && r.Top1Agree, "L top-1: pair 2 on both sides");
        w[0] = 6f;
        r = AutShadowCore.Report(g, has, w, c, 5);
        Check(r.Top1Valid && !r.Top1Agree, "L top-1 differs");
        r = AutShadowCore.Report(g, new bool[5], w, c, 5);
        Check(r.Pairs == 0 && !r.Top1Valid, "L empty");

        const string raw = "3681889397f4aa44b399a2926b2454fc";
        byte[] b = new byte[16];
        for (int i = 0; i < 16; i++) b[i] = Convert.ToByte(raw.Substring(2 * i, 2), 16);
        Check(AutShadowCore.GuidNFromRaw(raw) == new Guid(b).ToString("N"), "MVID raw bytes to Guid N format");

        // docs/124 §13.5: L3 callees resolve by exact signature; a same-name overload is never the one returned.
        Type o = typeof(Overloads);
        MethodInfo one = AutShadowCore.FindExact(o, "Score", new Type[] { typeof(int) });
        Check(one != null && one.GetParameters()[0].ParameterType == typeof(int), "exact overload found");
        MethodInfo two = AutShadowCore.FindExact(o, "Score", new Type[] { typeof(string) });
        Check(two != null && two != one, "each overload resolves to itself");
        Check(AutShadowCore.FindExact(o, "Score", new Type[] { typeof(double) }) == null, "same name, other parameters: not accepted");
        Check(AutShadowCore.FindExact(o, "Score", Type.EmptyTypes) == null, "same name, no parameters: not accepted");
        MethodInfo pr = AutShadowCore.FindExact(o, "Score", new Type[] { typeof(int), typeof(float).MakeByRefType() });
        Check(pr != null && pr != one && !pr.IsPublic, "private ref overload resolved separately");
        Check(AutShadowCore.FindExact(typeof(Derived), "Score", new Type[] { typeof(int) }).DeclaringType == typeof(Derived), "override wins over base");

        // G-M1: the accepted MVIDs are 16-byte raw hex that converts like EA's
        foreach (string m in new string[] { "bb8cb38b0d83b44490103053df82374c", "a95a143fe9314a44805eb847548ffe7f", "99b5bd68c981aa42a5f220f2cf7ae4d2", "ee57f83a5f56384f9ede12b34a086c39" })
        {
            for (int i = 0; i < 16; i++) b[i] = Convert.ToByte(m.Substring(2 * i, 2), 16);
            Check(AutShadowCore.GuidNFromRaw(m) == new Guid(b).ToString("N"), "G-M1 MVID " + m);
        }

        // G-M4: index keys (tools/managed-callgraph.ts format) from reflection, and their resolution
        Type nt = typeof(Gm.Host.Nested);
        string goKey = "Gm.Host+Nested::Go(int&,System.Collections.Generic.Dictionary`2<string,System.Collections.Generic.List`1<ulong>>,object[],float[,])";
        Check(AutShadowCore.SigKey(nt.GetMethod("Go")) == goKey, "SigKey: nested, byref, generic instance, arrays: " + AutShadowCore.SigKey(nt.GetMethod("Go")));
        Check(AutShadowCore.SigKey(nt.GetConstructors()[0]) == "Gm.Host+Nested::.ctor(bool)", "SigKey: constructor");
        Check(AutShadowCore.SigKey(typeof(Gm.Host).GetMethod("get_InUse")) == "Gm.Host::get_InUse()", "SigKey: property getter");
        System.Collections.Generic.Dictionary<string, MethodBase> got = AutShadowCore.Resolve(new Assembly[] { typeof(Gm.Host).Assembly },
            new string[] { goKey, "Gm.Host+Nested::.ctor(bool)", "Gm.Host::Plain()", "Gm.Host::Plain(int)", "Gm.Missing::X()" });
        Check(got.Count == 3 && got[goKey] == nt.GetMethod("Go") && got.ContainsKey("Gm.Host::Plain()"), "Resolve: 3 of 5 (other overload and missing type left out)");

        // G-M5: override census
        string[] slots = { "Gm.Host::get_InUse()", "Gm.Host::Plain()", "Gm.IThing::Count()", "Gm.IThing::Other()", "Gm.Mid::Nothing()" };
        bool[] hit = new bool[slots.Length];
        System.Collections.Generic.List<string> log = new System.Collections.Generic.List<string>();
        int n = AutShadowCore.OverrideCensus(new Type[] { typeof(Gm.ModObj), typeof(Gm.ModFromGame), typeof(Gm.ModQuiet), typeof(Gm.IThing) },
            delegate(Type t) { return t != null && t.Name.StartsWith("Mod"); }, slots, hit, log);
        Check(n == 1, "census: one mod type overrides closure slots, got " + n);
        Check(hit[0] && !hit[1] && hit[2] && hit[3] && !hit[4], "census: InUse override, explicit and implicit interface bodies; newslot and game-base bodies not counted");
        Check(log.Count == 3, "census log lines " + log.Count);
        // receiver rule: the old census marks the definition slot for a mod's own definition (mono-cost4 bug); with the
        // definition slots exempt only the object slot is marked, and the subclass skips its base by the derive rule
        string[] ds = { "Gm.IDefLike::Test()", "Gm.GameDef::Test()", "Gm.Host::get_InUse()" };
        Type[] mt = { typeof(Gm.ModOwnDef), typeof(Gm.ModSubDef), typeof(Gm.ModObj) };
        Predicate<Type> modP = delegate(Type t) { return t != null && t.Name.StartsWith("Mod"); };
        bool[] dh = new bool[ds.Length];
        AutShadowCore.OverrideCensus(mt, modP, ds, dh, null);
        Check(dh[0] && dh[1] && dh[2], "old census: own definition marks the interface slot");
        dh = new bool[ds.Length];
        int ex;
        int ntypes = AutShadowCore.OverrideCensus(mt, modP, ds, dh, null, delegate(string sl) { return sl.StartsWith("Gm.IDefLike::") || sl.StartsWith("Gm.GameDef::"); }, out ex);
        Check(!dh[0] && !dh[1] && dh[2] && ntypes == 1 && ex >= 2, "receiver rule: only the object slot marked, exempted " + ex + ", types " + ntypes);
        bool[] oh = new bool[3];
        System.Collections.Generic.List<string> dl = new System.Collections.Generic.List<string>();
        int dn = AutShadowCore.DerivedOwners(mt, new Type[] { typeof(Gm.GameDef), null, typeof(Gm.IThing) }, oh, dl);
        Check(dn == 1 && oh[0] && !oh[1] && !oh[2] && dl.Count == 1 && dl[0].Contains("Gm.ModSubDef"), "derive rule: the subclass skips its base only");
        Check(AutShadowCore.OnScoredDefinition("Sims3.Gameplay.Interactions.InteractionDefinition::Test(x)") && !AutShadowCore.OnScoredDefinition("Sims3.Gameplay.Interfaces.IGameObject::get_InUse()"), "OnScoredDefinition");

        // H11 (docs/127 §4): conditions in the game's order, later probes not run once one fails
        Probe pr2 = new Probe();
        Check(!AutShadowCore.RainShelter(false, pr2, 1, 2) && pr2.Calls == 0, "H11: not an autonomy search: no probe");
        pr2.Calls = 0;
        Check(!AutShadowCore.RainShelter(true, pr2, 1, 2) && pr2.Calls == 1, "H11: Seasons off: weather not read");
        pr2.Seasons = true; pr2.W = 3; pr2.Calls = 0;
        Check(!AutShadowCore.RainShelter(true, pr2, 1, 2) && pr2.Calls == 11, "H11: dry weather: target not probed");
        pr2.W = 2; pr2.Sim = true; pr2.Calls = 0;
        Check(!AutShadowCore.RainShelter(true, pr2, 1, 2) && pr2.Calls == 111, "H11: hail, Sim target: no IsOutside");
        pr2.Sim = false; pr2.Calls = 0;
        Check(!AutShadowCore.RainShelter(true, pr2, 1, 2) && pr2.Calls == 1111, "H11: hail, indoor object");
        pr2.Outside = true; pr2.W = 1;
        Check(AutShadowCore.RainShelter(true, pr2, 1, 2), "H11: rain, outdoor object: abort");

        Console.WriteLine(sFails == 0 ? "autshadow selftest: PASS" : "autshadow selftest: " + sFails + " FAIL");
        return sFails == 0 ? 0 : 1;
    }
}
