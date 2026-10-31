using System;
using System.Reflection;
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

        // docs/124 §13.5: an L3 callee is one exact method (name + parameter types, as t.GetMethods sees it: declared or
        // inherited), never every overload with that name. Null when t has no such method.
        public static MethodInfo FindExact(Type t, string name, Type[] p)
        {
            const BindingFlags all = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
            foreach (MethodInfo m in t.GetMethods(all))
            {
                if (m.Name != name) continue;
                ParameterInfo[] q = m.GetParameters();
                if (q.Length != p.Length) continue;
                int i = 0;
                while (i < q.Length && q[i].ParameterType == p[i]) i++;
                if (i == q.Length) return m;
            }
            return null;
        }

        static long Ordered(int i){ return i >= 0 ? i : (long)int.MinValue - i; }

        // ---- H11 (docs/127 §4): the rain/hail shelter branch of Autonomy.CalculateScoreForObjectInteraction ----
        // The game's order: autonomy search, Seasons on, weather rain or hail, then the target (outdoors, not a Sim). Each
        // probe runs only when the ones before it passed (CurrentWeather needs Seasons; IsOutside is an IC).
        public interface IRainProbe { bool SeasonsOn(); int Weather(); bool TargetIsSim(); bool TargetOutside(); }
        public static bool RainShelter(bool autonomySearch, IRainProbe p, int rain, int hail)
        {
            if (!autonomySearch || !p.SeasonsOn()) return false;
            int w = p.Weather();
            return (w == rain || w == hail) && !p.TargetIsSim() && p.TargetOutside();
        }

        // H11 shelter answer (docs/127 §4.1): instead of aborting, the worker answers the branch from a bit taken on the sim
        // thread at the cut. The branch asks IsShelteredFromPrecipitation(target, instance), which uses the instance only
        // for ICanCountAsBeingIndoors targets [verified, decompiled SeasonsManager]; for every other target it is the
        // position test, so one bit per meta object stands for it. 0 = no bit (Sim, indoors-capable, not outside, no rain):
        // the worker aborts `rain` as before; 1 sheltered; 2 not sheltered.
        public const byte kShelterNone = 0, kSheltered = 1, kUnsheltered = 2;
        public static byte ShelterBit(bool rainOrHail, bool isSim, bool countsIndoors, bool outside, bool sheltered)
        {
            if (!rainOrHail || isSim || countsIndoors || !outside) return kShelterNone;
            return sheltered ? kSheltered : kUnsheltered;
        }
        // the hooked IsShelteredFromPrecipitation(obj, ii) on the worker: 1 / 0 = answer, -1 = abort `rain`. Only the armed
        // target's skipped call (no instance) is answered; anything else on the worker never had a cut bit.
        public static int ShelterAnswer(bool instanceNull, bool armedTarget, byte bit)
        {
            if (!instanceNull || !armedTarget || bit == kShelterNone) return -1;
            return bit == kSheltered ? 1 : 0;
        }
        // after the score: the pair counts only if exactly the one CreateInstance call was skipped and the hook answered it
        public static bool ShelterDone(int skips, bool answered) { return skips == 1 && answered; }

        // ---- docs/128 G-M4 / G-M5: method keys in the call-graph index format (tools/managed-callgraph.ts sigType) ----

        static string Alias(string full)
        {
            switch (full)
            {
                case "System.Void": return "void"; case "System.Boolean": return "bool"; case "System.Char": return "char";
                case "System.SByte": return "sbyte"; case "System.Byte": return "byte"; case "System.Int16": return "short";
                case "System.UInt16": return "ushort"; case "System.Int32": return "int"; case "System.UInt32": return "uint";
                case "System.Int64": return "long"; case "System.UInt64": return "ulong"; case "System.Single": return "float";
                case "System.Double": return "double"; case "System.String": return "string"; case "System.Object": return "object";
                case "System.TypedReference": return "typedref"; case "System.IntPtr": return "IntPtr"; case "System.UIntPtr": return "UIntPtr";
                default: return null;
            }
        }

        // C# aliases, '+' for nesting, Name`N<args> for generic instances, & / * / [] / [,] suffixes, generic parameters
        // by name (the index writes !N; keys with ! are not in the generated lists).
        public static string TypeKey(Type t)
        {
            if (t.IsByRef) return TypeKey(t.GetElementType()) + "&";
            if (t.IsPointer) return TypeKey(t.GetElementType()) + "*";
            if (t.IsArray) return TypeKey(t.GetElementType()) + "[" + new string(',', t.GetArrayRank() - 1) + "]";
            if (t.IsGenericParameter) return t.Name;
            if (t.IsGenericType && !t.IsGenericTypeDefinition)
            {
                Type[] a = t.GetGenericArguments();
                string[] s = new string[a.Length];
                for (int i = 0; i < a.Length; i++) s[i] = TypeKey(a[i]);
                return t.GetGenericTypeDefinition().FullName + "<" + string.Join(",", s) + ">";
            }
            string full = t.FullName ?? t.Name;
            return Alias(full) ?? full;
        }

        public static string ParamsKey(MethodBase m)
        {
            ParameterInfo[] p = m.GetParameters();
            string[] s = new string[p.Length];
            for (int i = 0; i < p.Length; i++) s[i] = TypeKey(p[i].ParameterType);
            return "(" + string.Join(",", s) + ")";
        }

        public static string SigKey(MethodBase m) { return TypeKey(m.DeclaringType) + "::" + m.Name + ParamsKey(m); }

        // Index keys to the declared methods and constructors of asms that carry them (first assembly with the type wins).
        // Keys whose type or method is missing are left out: the caller compares the count with keys.Length.
        public static System.Collections.Generic.Dictionary<string, MethodBase> Resolve(Assembly[] asms, string[] keys)
        {
            const BindingFlags decl = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            // per type, the member names wanted: key strings are built only for overloads of those names (the key building is
            // the cost under the game's interpreter; monohost GM4-sweep measures the whole sweep)
            System.Collections.Generic.Dictionary<string, bool> want = new System.Collections.Generic.Dictionary<string, bool>();
            System.Collections.Generic.Dictionary<string, System.Collections.Generic.Dictionary<string, bool>> types = new System.Collections.Generic.Dictionary<string, System.Collections.Generic.Dictionary<string, bool>>();
            foreach (string k in keys)
            {
                int c = k.IndexOf("::");
                want[k] = true;
                string tn = k.Substring(0, c);
                System.Collections.Generic.Dictionary<string, bool> ns;
                if (!types.TryGetValue(tn, out ns)) types[tn] = ns = new System.Collections.Generic.Dictionary<string, bool>();
                ns[k.Substring(c + 2, k.IndexOf('(', c) - c - 2)] = true;
            }
            System.Collections.Generic.Dictionary<string, MethodBase> res = new System.Collections.Generic.Dictionary<string, MethodBase>();
            foreach (System.Collections.Generic.KeyValuePair<string, System.Collections.Generic.Dictionary<string, bool>> e in types)
                foreach (Assembly a in asms)
                {
                    Type t = a.GetType(e.Key);
                    if (t == null) continue;
                    foreach (string n in e.Value.Keys)
                        foreach (MemberInfo mi in t.GetMember(n, MemberTypes.Method | MemberTypes.Constructor, decl))
                        {
                            MethodBase m = mi as MethodBase;
                            if (m == null) continue;
                            string k = SigKey(m);
                            if (want.ContainsKey(k)) res[k] = m;
                        }
                    break;
                }
            return res;
        }

        // G-M5 census: hit[i] = true when a type of modTypes overrides Slots[i] (a virtual declared on one of its non-mod
        // bases) or supplies the body of interface method Slots[i] in mod code. Returns the number of matching types and
        // names them in log. isMod tells mod types (their own bases are checked when the census reaches them).
        // shortcut: matches by name + parameter keys on each base, not by vtable slot; a newslot in between is still
        // counted (more skips, never fewer).
        public static int OverrideCensus(Type[] modTypes, Predicate<Type> isMod, string[] slots, bool[] hit, System.Collections.Generic.List<string> log)
        {
            int exempt;
            return OverrideCensus(modTypes, isMod, slots, hit, log, null, out exempt);
        }

        // docs/122 §14.13: slots dispatched on the scored definition itself (docs/127 §2 receiver table). The worker scores a
        // pair only when the definition's exact runtime type is allow-listed, so a mod override of these runs there only as
        // a subclass of an allow-listed definition: DerivedOwners covers that, the census does not mark the slot.
        public static readonly string[] kDefReceiverTypes = {
            "Sims3.Gameplay.Interactions.InteractionDefinition", "Sims3.Gameplay.Interactions.IMetaInteractionDefinition",
            "Sims3.Gameplay.Interactions.IOverridesCalculateScore", "Sims3.Gameplay.Interactions.IOverridesAgeTests" };
        public static bool OnScoredDefinition(string slot) { return Array.IndexOf(kDefReceiverTypes, slot.Substring(0, slot.IndexOf("::"))) >= 0; }

        // exemptSlot (may be null): matches on those slots are not marked, only counted in exempted (logged once).
        public static int OverrideCensus(Type[] modTypes, Predicate<Type> isMod, string[] slots, bool[] hit, System.Collections.Generic.List<string> log,
            Predicate<string> exemptSlot, out int exempted)
        {
            int ex = 0;
            Predicate<int> take = delegate(int i) { if (exemptSlot != null && exemptSlot(slots[i])) { ex++; return false; } return true; };
            const BindingFlags decl = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            System.Collections.Generic.Dictionary<string, int> idx = new System.Collections.Generic.Dictionary<string, int>();
            System.Collections.Generic.Dictionary<string, bool> names = new System.Collections.Generic.Dictionary<string, bool>();
            for (int i = 0; i < slots.Length; i++) { idx[slots[i]] = i; int c = slots[i].IndexOf("::"); names[slots[i].Substring(c + 2, slots[i].IndexOf('(', c) - c - 2)] = true; }
            int types = 0;
            foreach (Type t in modTypes)
            {
                if (t == null || t.IsInterface) continue;
                bool any = false;
                foreach (MethodInfo m in t.GetMethods(decl))
                {
                    if (!m.IsVirtual || (m.Attributes & MethodAttributes.NewSlot) != 0 || !names.ContainsKey(m.Name)) continue;
                    string tail = "::" + m.Name + ParamsKey(m);
                    for (Type b = t.BaseType; b != null; b = b.BaseType)
                    {
                        if (isMod(b)) continue;
                        int i;
                        if (idx.TryGetValue(TypeKey(b) + tail, out i) && take(i)) { hit[i] = any = true; if (log != null) log.Add(t.FullName + " overrides " + slots[i]); }
                    }
                }
                foreach (Type itf in t.GetInterfaces())
                {
                    if (isMod(itf)) continue;
                    string ik = TypeKey(itf) + "::";
                    bool listed = false;
                    foreach (string s in slots) if (s.StartsWith(ik)) { listed = true; break; }
                    if (!listed) continue;
                    InterfaceMapping map;
                    try { map = t.GetInterfaceMap(itf); }
                    catch (Exception)
                    {   // no map: every listed method of the interface counts (more skips, never fewer)
                        for (int i = 0; i < slots.Length; i++) if (slots[i].StartsWith(ik) && take(i)) { hit[i] = any = true; if (log != null) log.Add(t.FullName + " implements " + slots[i] + " (no map)"); }
                        continue;
                    }
                    for (int j = 0; j < map.InterfaceMethods.Length; j++)
                    {
                        int i;
                        if (map.TargetMethods[j] == null || !isMod(map.TargetMethods[j].DeclaringType) || !idx.TryGetValue(SigKey(map.InterfaceMethods[j]), out i) || !take(i)) continue;
                        hit[i] = any = true;
                        if (log != null) log.Add(t.FullName + " implements " + slots[i]);
                    }
                }
                if (any) types++;
            }
            exempted = ex;
            return types;
        }

        // G-M5 derive rule (docs/122 §14.13): ownerHit[o] = true when a type of modTypes derives from owners[o] (a mod
        // subclass replacing the definition's singleton, e.g. NRaas GoHere's VisitLotEx.Definition : VisitLot.Definition).
        // Logs the first such type per owner; returns the number of owners newly hit. Null owners are skipped.
        public static int DerivedOwners(Type[] modTypes, Type[] owners, bool[] ownerHit, System.Collections.Generic.List<string> log)
        {
            int n = 0;
            for (int o = 0; o < owners.Length; o++)
            {
                if (owners[o] == null) continue;
                foreach (Type t in modTypes)
                {
                    if (t == null || !t.IsSubclassOf(owners[o])) continue;
                    if (!ownerHit[o]) n++;
                    ownerHit[o] = true;
                    if (log != null) log.Add(owners[o].FullName + " (derived: " + t.FullName + ")");
                    break;
                }
            }
            return n;
        }

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
