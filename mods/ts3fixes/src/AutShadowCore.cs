using System;
using System.Runtime.InteropServices;

namespace Ts3Fixes
{
    // Game-independent compare rules of the [AutShadow] pilot (AutShadow.cs, docs/122 §5), kept free of game types so the
    // offline self-test (tools/selftest/autshadow) compiles them alone.
    public static class AutShadowCore
    {
        // Result codes per (meta object, ad): docs/122 §4.2. Filtered = the game's commodity filter drops the ad (not scored
        // by the game either, so never compared or counted).
        public const int kPass = 0, kTestFailed = 1, kSkip = 2, kAbort = 3, kFiltered = -1;
        // Mode R classes (docs/122 §5.1). A5 comes from the native run status, not from a pair.
        public const int A0 = 0, A1 = 1, A2 = 2, A3 = 3, A4 = 4, A5 = 5, kNoClass = -1;

        [StructLayout(LayoutKind.Explicit)]
        struct FloatBits { [FieldOffset(0)] public float F; [FieldOffset(0)] public int I; }

        public static int Bits(float f) { FloatBits b = new FloatBits(); b.F = f; return b.I; }

        // Distance in units in the last place: floats mapped onto one ordered integer line (+0 and -0 both at 0). NaN
        // payloads map like any other bit pattern, so two different NaNs are far apart.
        public static long UlpDistance(float a, float b)
        {
            long x = Ordered(Bits(a)), y = Ordered(Bits(b));
            return x > y ? x - y : y - x;
        }

        // MVID as metadata stores it (16 bytes in hex, docs/data/autonomy-allow.csv) to Guid.ToString("N"): the first three
        // fields are little-endian, so their bytes are reversed.
        public static string GuidNFromRaw(string raw)
        {
            int[] order = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
            char[] n = new char[32];
            for (int i = 0; i < 16; i++) { n[2 * i] = raw[2 * order[i]]; n[2 * i + 1] = raw[2 * order[i] + 1]; }
            return new string(n);
        }

        static long Ordered(int i){ return i >= 0 ? i : (long)int.MinValue - i; }

        // Class of one pair: reference (sim thread) vs worker, both from the same driver on the same request. kNoClass when
        // neither side scored it (both filtered).
        public static int Classify(int refCode, float refScore, float refMeta, int workCode, float workScore, float workMeta)
        {
            if (refCode == kFiltered && workCode == kFiltered) return kNoClass;
            if (workCode == kSkip || workCode == kAbort || refCode == kSkip || refCode == kAbort) return A4;
            if (refCode != workCode) return A3;
            if (Bits(refScore) == Bits(workScore) && Bits(refMeta) == Bits(workMeta)) return A0;
            long d = Math.Max(UlpDistance(refScore, workScore), UlpDistance(refMeta, workMeta));
            return d <= 1 ? A1 : A2;
        }

        // Mode W report for one request (docs/122 §5.2): only pairs the worker scored (pass or Test failed) that the game
        // also scored. The game's C2 returns 0 for a failed Test, so Test agreement is "score > 0" on both sides.
        // top1 is set when at least one pair counts; top-1 agrees when both sides' first highest score is the same pair.
        public struct LReport { public int Pairs, Equal, TestAgree; public bool Top1Valid, Top1Agree; }

        public static LReport Report(float[] gameScore, bool[] gameHas, float[] workScore, int[] workCode, int n)
        {
            LReport r = new LReport();
            int bestW = -1, bestG = -1;
            for (int i = 0; i < n; i++)
            {
                if (!gameHas[i] || (workCode[i] != kPass && workCode[i] != kTestFailed)) continue;
                r.Pairs++;
                if (Bits(gameScore[i]) == Bits(workScore[i])) r.Equal++;
                if ((gameScore[i] > 0f) == (workScore[i] > 0f)) r.TestAgree++;
                if (bestW < 0 || workScore[i] > workScore[bestW]) bestW = i;
                if (bestG < 0 || gameScore[i] > gameScore[bestG]) bestG = i;
            }
            r.Top1Valid = r.Pairs > 0;
            r.Top1Agree = r.Top1Valid && bestW == bestG;
            return r;
        }
    }
}
