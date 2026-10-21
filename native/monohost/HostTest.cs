// Managed side of the monohost selftest (docs/119 §11): our own test code, compiled against the game's mscorlib and
// SimIFace by build.cmd (no EA code here). C# 2 only: Mono 1.2.3's interpreter, no generics or lambdas needed.
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Sims3.SimIFace;
using Ts3Fixes;

namespace MonoHostTest
{
    public class Node
    {
        public Node Next;
        public Node[] Kids;
        public int Value;
        public string Tag;
    }

    public class Finalizable
    {
        public static int Finalized;
        public int Id;
        ~Finalizable() { Finalized++; }
    }

    // docs/139 MintBench: two receiver types behind one abstract base (monomorphic and alternating callvirt sites)
    public abstract class Shape
    {
        public int W;
        public abstract int Area();
        public virtual int Id() { return 1; }
    }
    public class Sq : Shape { public override int Area() { return W * W; } }
    public class Rect : Shape
    {
        public int H;
        public override int Area() { return W * H; }
        public override int Id() { return 2; }
    }

    public static class HostTest
    {
        // ---- docs/139: interpreter workload for the mint dispatch A/B. The game's own mscorlib does the work
        // (List/Dictionary generics, struct and interface enumerators, boxed Enum.Equals, GetType), the shapes add
        // monomorphic and two-type callvirt sites. The result is a checksum: every dispatch variant must return the same.
        public static int MintBench(int n)
        {
            System.Collections.Generic.Dictionary<int, Shape> map = new System.Collections.Generic.Dictionary<int, Shape>();
            System.Collections.Generic.List<Shape> list = new System.Collections.Generic.List<Shape>();
            for (int i = 0; i < 256; i++)
            {
                Shape s;
                if (i % 3 == 0) { Rect r = new Rect(); r.H = i + 1; s = r; } else s = new Sq();
                s.W = i;
                map[i * 7] = s;
                list.Add(s);
            }
            int acc = 0;
            for (int r = 0; r < n; r++)
            {
                foreach (Shape s in list) acc += s.Area() + s.Id();
                for (int i = 0; i < 256; i++) { Shape s; if (map.TryGetValue(i * 7 + (r & 1), out s)) acc ^= s.W; }
                System.Collections.Generic.IEnumerable<Shape> e = list;
                foreach (Shape s in e) if (s is Sq) acc++;
                if (((object)DayOfWeek.Monday).Equals(r % 7 == 0 ? DayOfWeek.Monday : DayOfWeek.Friday)) acc += 5;
                if (list[r & 255].GetType() == typeof(Rect)) acc += 3;
            }
            return acc;
        }

        // ---- M1.4: a graph kept alive only through a GC handle ----
        static Node Build(int n, int seed)
        {
            Node root = new Node();
            root.Kids = new Node[n];
            Node prev = null;
            for (int i = 0; i < n; i++)
            {
                Node k = new Node();
                k.Value = seed * 31 + i;
                k.Tag = new string((char)('a' + i % 26), i % 7 + 1);
                k.Kids = new Node[i % 4];
                for (int j = 0; j < k.Kids.Length; j++) { k.Kids[j] = new Node(); k.Kids[j].Value = k.Value ^ j; }
                k.Next = prev;
                prev = k;
                root.Kids[i] = k;
            }
            root.Next = prev;
            return root;
        }

        static int Sum(Node root, int n, int seed)
        {
            if (root == null || root.Kids == null || root.Kids.Length != n) return -1;
            int bad = 0, chain = 0;
            for (int i = 0; i < n; i++)
            {
                Node k = root.Kids[i];
                if (k == null || k.Value != seed * 31 + i || k.Tag == null || k.Tag.Length != i % 7 + 1 || k.Tag[0] != (char)('a' + i % 26)) { bad++; continue; }
                if (k.Kids.Length != i % 4) bad++;
                for (int j = 0; j < k.Kids.Length; j++) if (k.Kids[j] == null || k.Kids[j].Value != (k.Value ^ j)) bad++;
            }
            for (Node c = root.Next; c != null; c = c.Next) chain++;
            if (chain != n) bad++;
            return bad;
        }

        public static IntPtr BuildGraph(int n, int seed) { return GCHandle.ToIntPtr(GCHandle.Alloc(Build(n, seed))); }
        public static IntPtr BuildWeak(int n, int seed) { return GCHandle.ToIntPtr(GCHandle.Alloc(Build(n, seed), GCHandleType.Weak)); }
        public static int CheckGraph(IntPtr h, int n, int seed) { return Sum((Node)GCHandle.FromIntPtr(h).Target, n, seed); }
        public static bool IsAlive(IntPtr h) { return GCHandle.FromIntPtr(h).Target != null; }
        public static void Free(IntPtr h) { GCHandle.FromIntPtr(h).Free(); }

        // garbage only: nothing outlives the call
        public static int Churn(int n)
        {
            int s = 0;
            for (int i = 0; i < n; i++)
            {
                Node x = new Node();
                x.Kids = new Node[8];
                x.Tag = new string('x', 16);
                s += x.Kids.Length + x.Tag.Length;
            }
            return s;
        }

        public static void MakeFinalizable(int n)
        {
            for (int i = 0; i < n; i++) { Finalizable f = new Finalizable(); f.Id = i; }
        }
        // docs/128 G-M4 / G-M5 under the game's Mono: resolve the generated closure list (mods/ts3fixes/src/AutShadowLists.cs)
        // in the loaded script assemblies, L3 each method, then the override census over non-game assemblies. monohost times it.
        public static int SweepFound, SweepForeign, SweepAsms, CensusTypes;
        static readonly string[] kGame = { "mscorlib", "System", "System.Xml", "ScriptCore", "SimIFace", "Sims3GameplaySystems",
            "Sims3GameplayObjects", "Sims3Metadata", "Sims3StoreObjects", "UI" };
        public static int GuardSweep()
        {
            System.Collections.Generic.List<System.Reflection.Assembly> game = new System.Collections.Generic.List<System.Reflection.Assembly>(), other = new System.Collections.Generic.List<System.Reflection.Assembly>();
            foreach (System.Reflection.Assembly a in AppDomain.CurrentDomain.GetAssemblies())
                (Array.IndexOf(kGame, a.GetName().Name) >= 0 ? game : other).Add(a);
            SweepAsms = game.Count;
            System.Collections.Generic.Dictionary<string, System.Reflection.MethodBase> got = Ts3Fixes.AutShadowCore.Resolve(game.ToArray(), Ts3Fixes.AutShadowLists.Methods);
            int foreign = 0;
            foreach (System.Reflection.MethodBase m in got.Values)
                if (Marshal.ReadIntPtr(new IntPtr(m.MethodHandle.Value.ToInt32() + 8)) != Marshal.ReadIntPtr(m.DeclaringType.TypeHandle.Value)) foreign++;
            SweepFound = got.Count; SweepForeign = foreign;
            System.Collections.Generic.List<Type> types = new System.Collections.Generic.List<Type>();
            foreach (System.Reflection.Assembly a in other) types.AddRange(a.GetTypes());
            bool[] hit = new bool[Ts3Fixes.AutShadowLists.Slots.Length];
            CensusTypes = Ts3Fixes.AutShadowCore.OverrideCensus(types.ToArray(), delegate(Type t) { return t != null && other.Contains(t.Assembly); },
                Ts3Fixes.AutShadowLists.Slots, hit, null);
            return got.Count;
        }
        public static int GetSweep(int what) { return what == 0 ? SweepFound : what == 1 ? SweepForeign : what == 2 ? SweepAsms : what == 3 ? CensusTypes : Ts3Fixes.AutShadowLists.Methods.Length; }
        public static int FinalizedCount() { return Finalizable.Finalized; }
        public static int Collections() { return GC.CollectionCount(0); }

        // ---- M1.5: a task that yields through Simulator.Sleep ----
        public static int Steps;
        public static ulong[] SeenTask = new ulong[32];
        public static int GetSteps() { return Steps; }
        public static ulong GetSeenTask(int i) { return SeenTask[i]; }

        // Sleeps i + 1 ticks after step i; checks that a local graph and the loop state survive each yield (and the host's
        // collections between ticks). Returns Steps, or a negative error code.
        public static int SleepLoop(int n)
        {
            Node local = Build(50, 7);
            int acc = 0;
            for (int i = 0; i < n; i++)
            {
                Steps++;
                SeenTask[i] = Simulator.CurrentTask.Value;
                acc += i;
                Simulator.Sleep((uint)(i + 1));
                if (Sum(local, 50, 7) != 0) return -100 - i;
                Churn(200);
            }
            if (acc != n * (n - 1) / 2) return -2;
            Steps += 1000;
            return Steps;
        }

        // a yield from a .cctor frame is refused by the interpreter (0xE53550): the task gets NotSupportedException
        public static int SleepInCctor() { return CctorSleeper.X; }
        public static int SleepInCctor2() { return CctorSleeper2.X; }  // M5: the same exception task after the arena fix is in

        // ---- M5: jobs for the second managed thread (pure managed work, no internal calls of the game) ----
        public static int LastWorker;  // a static is a root: the host reads the worker's result from here, not from the boxed return
        public static int GetLastWorker() { return LastWorker; }
        public static string LastError = "";  // the worker entry drops managed exceptions; the jobs record theirs here
        public static string GetLastError() { return LastError; }

        // A graph referenced only from this frame, i.e. the worker's stack, while both threads allocate and collect.
        public static int WorkerGraph(int rounds)
        {
            LastError = "";
            try { return WorkerGraphBody(rounds); }
            catch (Exception e) { LastError = e.GetType().FullName + ": " + e.Message + " @ " + e.StackTrace; LastWorker = -1000000; return LastWorker; }
        }
        static int WorkerGraphBody(int rounds)
        {
            Node mine = Build(400, 13);
            int bad = 0;
            for (int r = 0; r < rounds; r++)
            {
                Churn(300);
                if (Sum(mine, 400, 13) != 0) bad++;
                if ((r & 63) == 63) GC.Collect();  // the worker collects too: the main thread's stack then comes from P2
            }
            LastWorker = bad == 0 ? rounds : -bad;
            return LastWorker;
        }

        // Math.Min(float) asks Single.IsNaN through the interpreter's native-to-managed entry 0xD8A8E0 (docs/122 §12)
        public static int MinLoop(int n)
        {
            int bad = 0;
            for (int i = 0; i < n; i++)
            {
                float a = i, b = n - i;
                if (Math.Min(a, b) != (a < b ? a : b)) bad++;
            }
            return bad;
        }
        // F1 (docs/120 §9.4): host internal call that runs MinLoop(n) through ScriptHost_NativeToManaged 0xD8A8E0
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern int N2mMinLoop(int n);
        public static int MainN2m(int n) { return N2mMinLoop(n); }
        public static int WorkerN2m(int n)
        {
            LastError = "";
            try { LastWorker = N2mMinLoop(n); }
            catch (Exception e) { LastError = e.GetType().FullName + ": " + e.Message + " @ " + e.StackTrace; LastWorker = -1000000; }
            return LastWorker;
        }
        public static int WorkerMin(int n)
        {
            LastError = "";
            try { LastWorker = MinLoop(n); }
            catch (Exception e) { LastError = e.GetType().FullName + ": " + e.Message + " @ " + e.StackTrace; LastWorker = -1000000; }
            return LastWorker;
        }
    }

    // ---- G-A2 (docs/122 §14.4): [AutShadow] part 2 on the M5 worker. The host registers ts3native's guard thunks for
    // these names; their bodies are host fakes with the game's IC layouts (Objects_GetPosition, World_GetLotLocationImpl,
    // Query_GetObjectsOnLot, World_GetSunriseTime, World_GetWorldNameKey, EAText_GetMoneyString, Random_NextDoubleImpl).
    public static class AsIc
    {
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern Vector3 Objects_GetPosition(ulong id);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern ulong World_GetLotLocationImpl(Vector3 worldPos, ref LotLocation location, int roomDefinition);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern Array Query_GetObjectsOnLot(Type type, ulong lotId, int roomId);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern float World_GetSunriseTime();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern string World_GetWorldNameKey();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern string EAText_GetMoneyString(long amount);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern double Random_NextDoubleImpl();
        // host helpers (not guarded): the lot's object array and the world name key the fake bodies answer with
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void HostSetLotObjects(object[] objs);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void HostSetNameKey(string key);
    }

    // Synthetic shadow search: per meta object one score from IC answers (row, position-keyed lot, per-lot list, world
    // constants, a reference answer, a stub string), every 7th object also draws (the worker aborts 'rng'). The
    // sim-thread reference and the worker run the same code on the same cut; Finish compares them bit for bit (A0-A5).
    public static class AsDriver
    {
        public const int kObjects = 50;
        static ulong[] sReq;               // the request: meta object ids (rooted static, as ts3fixes sReq)
        static float[] sRef = new float[kObjects], sWork = new float[kObjects];
        static int[] sRefCode = new int[kObjects], sWorkCode = new int[kObjects];
        static object[] sLot;              // the lot's objects (also held here: the arrays the worker gets hold these)
        public static int A0, A1, A2, A3, A4, A5, Runs, Finished, Abandoned, Rng, OtherAborts, MainBad, LastStatus = -1;
        public static string Error = "";
        public static int Get(int what)
        {
            switch (what)
            {
                case 0: return A0; case 1: return A1; case 2: return A2; case 3: return A3; case 4: return A4; case 5: return A5;
                case 6: return Runs; case 7: return Finished; case 8: return Rng; case 9: return OtherAborts; case 10: return MainBad;
                case 11: return LastStatus; case 12: return Abandoned; default: return Error.Length;
            }
        }
        public static string GetError() { return Error; }
        public static void ResetCounts() { A0 = A1 = A2 = A3 = A4 = A5 = Runs = Finished = Abandoned = Rng = OtherAborts = MainBad = 0; Error = ""; }

        public static int Setup()
        {
            sLot = new object[12];
            for (int i = 0; i < sLot.Length; i++) { Node n = new Node(); n.Value = i; sLot[i] = n; }
            AsIc.HostSetLotObjects(sLot);
            AsIc.HostSetNameKey(new string('k', 3) + "WorldNameKey");  // only the host's masked copy and later the snapshot hold it
            // warm-up on this ("sim") thread: every method and IC signature the worker runs (docs/30 §5 step 6)
            Finish(); Reference(); Work();
            float[] s = new float[kObjects]; int[] c = new int[kObjects];
            ulong[] ids = new ulong[kObjects];
            for (int i = 0; i < kObjects; i++) ids[i] = 0x500000000UL + (ulong)(i + 1);
            Score(ids, s, c);
            AutShadowNative.SetRefs(string.Empty, new NullReferenceException());
            AutShadowNative.SetMethods(typeof(AsDriver).GetMethod("Finish").MethodHandle.Value,
                typeof(AsDriver).GetMethod("Reference").MethodHandle.Value, typeof(AsDriver).GetMethod("Work").MethodHandle.Value);
            return AutShadowNative.Version();
        }

        public static int Request(int id)
        {
            if (!AutShadowNative.Begin()) return 0;
            ulong[] ids = new ulong[kObjects];
            for (int i = 0; i < kObjects; i++) { ids[i] = 0x500000000UL + (ulong)(i + 1); AutShadowNative.AddRow(ids[i], 0); }
            sReq = ids;
            AutShadowNative.Post(id);
            return 1;
        }

        static bool sInRef;
        public static void Reference() { if (sReq == null) return; sInRef = true; try { Score(sReq, sRef, sRefCode); } finally { sInRef = false; } }
        public static void Work()
        {
            if (sReq == null) return;
            try { Score(sReq, sWork, sWorkCode); }
            catch (Exception e) { Error = e.GetType().FullName + ": " + e.Message; throw; }
        }

        // code: 0 pass, 3 + reason abort, 2 not reached (stop)
        static void Score(ulong[] ids, float[] score, int[] code)
        {
            for (int i = 0; i < ids.Length; i++) { score[i] = 0; code[i] = 2; }
            for (int i = 0; i < ids.Length; i++)
            {
                if (AutShadowNative.Stopped()) return;
                AutShadowNative.RngUnit(i);  // docs/125: each object draws from its own stream
                AutShadowNative.ResetAbort();
                float sc = 0;
                try
                {
                    Vector3 p = AsIc.Objects_GetPosition(ids[i]);
                    LotLocation loc = new LotLocation();
                    ulong lot = AsIc.World_GetLotLocationImpl(p, ref loc, 0);
                    Array a = AsIc.Query_GetObjectsOnLot(typeof(Node), lot, 0);
                    int sum = 0;
                    if (a != null) for (int j = 0; j < a.Length; j++) { Node n = a.GetValue(j) as Node; sum += n == null ? 1000 : n.Value; }
                    string key = AsIc.World_GetWorldNameKey();
                    string money = AsIc.EAText_GetMoneyString(i);
                    sc = p.x * 0.5f + p.z * 0.25f + AsIc.World_GetSunriseTime() + loc.mX + loc.mZ * 2 + loc.mRoom + (float)(lot & 0xFFFF)
                        + (a == null ? -1 : a.Length) + sum + (key == null ? -7 : key.Length * 3 + key[0]) + (money == null ? -3 : money.Length);
                    Node garbage = new Node(); garbage.Kids = new Node[3]; garbage.Tag = "g" + i;  // allocation on both threads
                    // docs/125 addendum §4.1: a reference-only draw (a hooked call) in a bracket leaves the unit stream alone
                    if (sInRef && i % 7 == 0) { AutShadowNative.RngPause(1); AsIc.Random_NextDoubleImpl(); AutShadowNative.RngPause(0); }
                    if (i % 7 == 0) sc += (float)AsIc.Random_NextDoubleImpl();
                }
                catch (Exception) { code[i] = 3 + 12; score[i] = 0; AutShadowNative.ResetAbort(); continue; }
                int ab = AutShadowNative.Aborted();
                code[i] = ab >= 0 ? 3 + ab : 0;
                score[i] = sc;
            }
        }

        public static void Finish()
        {
            if (sReq == null) return;
            LastStatus = AutShadowNative.LastRun();
            Finished++;
            if (LastStatus == 0)
            {
                Runs++;
                for (int i = 0; i < sReq.Length; i++)
                {
                    int w = sWorkCode[i], r = sRefCode[i];
                    if (w >= 3) { A4++; if (w == 3 + 3) Rng++; else OtherAborts++; continue; }
                    if (w == 2) { Abandoned++; continue; }
                    if (w != r) { A3++; continue; }
                    int bw = BitConverter.ToInt32(BitConverter.GetBytes(sWork[i]), 0), br = BitConverter.ToInt32(BitConverter.GetBytes(sRef[i]), 0);
                    if (bw == br) A0++;
                    else if (bw - br == 1 || br - bw == 1) A1++;
                    else { A2++; if (Error.Length == 0) Error = "A2 at " + i + ": ref " + sRef[i] + " work " + sWork[i]; }
                }
            }
            else A5++;
            sReq = null;
        }

        // the main ("sim") thread's own passes during the window: the same ICs pass through to the host bodies
        public static int MainPass()
        {
            float[] s = new float[kObjects]; int[] c = new int[kObjects];
            ulong[] ids = new ulong[kObjects];
            for (int i = 0; i < kObjects; i++) ids[i] = 0x500000000UL + (ulong)(i + 1);
            Score(ids, s, c);
            int bad = 0;
            for (int i = 0; i < kObjects; i++) if (c[i] != 0 || (i % 7 != 0 && s[i] != sRef[i])) bad++;  // i % 7: live RNG draws
            MainBad += bad;
            return bad;
        }
    }

    public class CctorSleeper
    {
        public static int X;
        static CctorSleeper() { Simulator.Sleep(1); X = 5; }
    }

    public class CctorSleeper2
    {
        public static int X;
        static CctorSleeper2() { Simulator.Sleep(1); X = 6; }
    }
}

namespace Ts3Fixes
{
    // ts3native's [AutShadow] internal calls (registered at the first Simulate tick; docs/122 §14.2): the same contract as
    // mods/ts3fixes/src/AutShadow.cs, declared here so the host test runs the native side unchanged.
    public static class AutShadowNative
    {
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern int Version();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern int Config(int key);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern bool IsWorker();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void Abort(int reason);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern int Aborted();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void ResetAbort();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern bool Stopped();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern bool Begin();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void AddRow(ulong objectId, int kind);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void Post(int requestId);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern int LastRun();
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void SetMethods(IntPtr finish, IntPtr reference, IntPtr work);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void SetRefs(string empty, Exception fmtException);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void Log(string jsonLine);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void RngUnit(int unit);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern void RngPause(int on);
    }
}
