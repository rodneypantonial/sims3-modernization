// Usage: node tools/selftest/autshadow/run.ts  (compiles this with mods/ts3fixes/src/AutShadowCore.cs)
// Offline self-check of AutShadowCore.cs: ULP distance, the Mode R pair classes A0-A4 and the Mode W L report, on fixed
// cases. Exit code 1 on failure.
using System;
using Ts3Fixes;

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

        Console.WriteLine(sFails == 0 ? "autshadow selftest: PASS" : "autshadow selftest: " + sFails + " FAIL");
        return sFails == 0 ? 0 : 1;
    }
}
