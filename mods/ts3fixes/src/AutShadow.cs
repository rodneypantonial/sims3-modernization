using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.Autonomy;
using Sims3.Gameplay.ChildAndTeenUpdates;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Interfaces;
using Sims3.Gameplay.ObjectComponents;
using Sims3.Gameplay.Objects.Miscellaneous;
using Sims3.Gameplay.Pools;
using Sims3.Gameplay.Utilities;
using Sims3.SimIFace;
using AutonomyT = Sims3.Gameplay.Autonomy.Autonomy;

namespace Ts3Fixes
{
    // ts3native's [AutShadow] internal calls (docs/122 part 2). ts3native registers them as "Ts3Fixes.AutShadowNative::<Name>"
    // at the first Simulate tick; without ts3native the first call throws MissingMethodException (probed once per world load).
    public static class AutShadowNative
    {
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern int Version();
        // 0 Mode (-1 off/no cut hook, 0 count only, 1 R, 2 W), 1 Sample, 2 WarmIn, 3 DumpOnMismatch, 4 worker ready 1/0
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
    }

    // [AutShadow] pilot part 2, managed half (docs/122 §1.2, §3.5, §4, §5, §12.5). Shadow only: the worker's scores are
    // compared, never used. Switch "autshadow" (tuning kAutShadow, default off): off = every hook runs the game's method
    // unchanged. Flow per sampled meta search (B13):
    //   1. sim, B13 prefix (MetaHook): request = actor + the meta objects B13 scores, in its order; rows to ts3native; Post.
    //   2. sim, during the game's own B13: the game's per-pair scores are recorded (CalculateScore wrapper) for the L report.
    //   3. native cut at the end of the tick; Modes 1/2: AutReference() on the sim thread (RNG saved/restored by native),
    //      then AutWork() on the M5 worker in the quiescent window; then AutFinish() on the sim thread: compare, counters, JSONL.
    // Counters: [106-111] A0..A5, [112-115] L pairs / equal / Test agree / top-1 agree, [116] skips, [117] warm-in
    // reference runs, [118] requests posted, [119] Begin busy, [120] mod-guard skips, [121] managed exceptions in the driver.
    // Abort reasons per pair: AppDomain data ts3fixes.autshadow.reasons (int[20], native reason order).
    public static partial class Fixes
    {
        const int kMaxObjs = 1024, kMaxPairs = 8192, kMaxLogPerLoad = 200;
        const int kReasonExc = 12, kReasonStop = 13;

        public sealed class Request
        {
            public int Id = -1, Mode, Objs, Pairs;
            public Sim Actor;
            public AutonomyT Aut;
            public CommodityKind C;
            public AutonomySearchType SearchType;
            public InteractionFlags Flags;
            public float Hours;
            public readonly IMetaObject[] Obj = new IMetaObject[kMaxObjs];
            public readonly int[] ObjImpl = new int[kMaxObjs];      // implementor index; -1 not allow-listed, -2 mod guard
            public readonly int[] Start = new int[kMaxObjs + 1];    // first pair of each object; Start[Objs] = Pairs
            public readonly InteractionObjectPair[] Pair = new InteractionObjectPair[kMaxPairs];
            public readonly int[] PairObj = new int[kMaxPairs];
            public readonly int[] Def = new int[kMaxPairs];         // definition index; -1 not allow-listed, -2 mod guard
        }

        public sealed class Result
        {
            public int Id = -1, Exceptions;
            public bool Done;
            public readonly float[] Score = new float[kMaxPairs];
            public readonly int[] Code = new int[kMaxPairs];
            public readonly int[] Reason = new int[kMaxPairs];
            public readonly float[] Meta = new float[kMaxObjs];
        }

        // Rooted request and results (docs/122 §4.2), allocated at startup on the sim thread and reused.
        static readonly Request sReq = new Request();
        static readonly Result sRef = new Result(), sWork = new Result();
        static readonly float[] sGameScore = new float[kMaxPairs];
        static readonly bool[] sGameHas = new bool[kMaxPairs];
        static readonly Dictionary<InteractionObjectPair, int> sPairIndex = new Dictionary<InteractionObjectPair, int>();
        static readonly Dictionary<ulong, bool> sReqLots = new Dictionary<ulong, bool>();
        static readonly List<Vector3> sBakeSales = new List<Vector3>();
        static readonly string sEmpty = "";
        static readonly Exception sFmtException = new FormatException("autshadow");
        static readonly int[] sReasons = new int[20];

        // State. sPosted: a request is with ts3native until AutFinish. sInWork: the worker is inside AutWork (worker hooks gate).
        static bool sPosted, sInWork, sSessionOff, sHooksOk, sCalcPatched, sWorkReady, sGameDone;
        static AutonomyT sGameAut;           // the autonomy whose B13 is being recorded (sim thread)
        static int sProbe;                   // per load: 0 not probed, 1 on, -1 off
        static int sNextId, sCalls, sSample = 4, sWarmIn, sWarmInMax = 20, sLogged, sGardening;
        static int sForceJitBytes = -1;

        // Allow-list (docs/data/autonomy-allow.csv 'in' rows; assembly 0 = Sims3GameplaySystems, 1 = Sims3GameplayObjects).
        static readonly string[] kDefNames = {
            "Sims3.Gameplay.Core.VisitCommunityLot+Definition", "Sims3.Gameplay.Core.VisitLot+Definition",
            "Sims3.Gameplay.Autonomy.GoToGarden+Definition", "Sims3.Gameplay.Core.GoToCommunityLot+Definition",
            "Sims3.Gameplay.Core.VisitCommunityLot+DateDefinition", "Sims3.Gameplay.Core.VisitLotWithDate+Definition",
            "Sims3.Gameplay.Abstracts.ShoppingRabbitHole+AutonomouslyShopInRabbitHole+Definition",
            "Sims3.Gameplay.Abstracts.ShowVenue+AutonomouslyAttendShow+Definition",
            "Sims3.Gameplay.Objects.RabbitHoles.AutonomouslyGoForMealInRestaurant+Definition",
            "Sims3.Gameplay.Objects.RabbitHoles.CityHall+AutonomouslyGoToCityHall+Definition",
            "Sims3.Gameplay.Objects.RabbitHoles.Hospital+AutonomouslyVisitHospital+Definition",
            "Sims3.Gameplay.Objects.RabbitHoles.Stadium+AutonomouslyAttendGame+Definition" };
        static readonly int[] kDefAsm = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 };
        static readonly string[] kAsmMvid = { "3681889397f4aa44b399a2926b2454fc", "b2cc44b06fee39449dab2fb6b07e9995" };
        static readonly Type[] kImpl = { typeof(Lot), typeof(RabbitHole), typeof(HomeMetaObject), typeof(WorkMetaObject),
                                         typeof(GardeningMetaObject), typeof(WorldPondHelper), typeof(BakeSaleTable) };
        const int kImplRabbitHole = 1;
        static readonly Dictionary<Type, int> sDefIndex = new Dictionary<Type, int>();   // sim thread only
        static readonly Dictionary<Type, int> sImplVerdict = new Dictionary<Type, int>(); // sim thread only
        static bool[] sDefGuardHit = new bool[12];
        static bool sSharedGuardHit;

        // Private game members, read on the sim thread (request) and, for reference fields only, by the worker shims.
        static FieldInfo fLotsKnown, fGardening, fAllowedRooms, fRoomCallbacks, fDatingRecords, fAcceptedDates;
        static MethodInfo mCanAnyMeta, mCanFullMeta;

        static void ApplyAutShadow()
        {
            const BindingFlags all = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
            try
            {
                fLotsKnown = typeof(AutonomyT).GetField("mLotsKnownAbout", all);
                fGardening = typeof(Sim).GetField("sNumSimsDoingGardening", all);
                fAllowedRooms = typeof(Sim).GetField("mAllowedRooms", all);
                fRoomCallbacks = typeof(Lot).GetField("mRoomRestrictionCheckCallbacks", all);
                fDatingRecords = typeof(OnlineDatingManager).GetField("mActiveRecords", all);
                Type rec = typeof(OnlineDatingManager).GetNestedType("OnlineDatingRecord", BindingFlags.NonPublic);
                fAcceptedDates = rec != null ? rec.GetField("mAcceptedDates", all) : null;
                mCanAnyMeta = typeof(AutonomyT).GetMethod("CanRunAnyMetaAutonomy", all);
                mCanFullMeta = typeof(AutonomyT).GetMethod("CanRunFullMetaAutonomy", all);
                if (fLotsKnown == null || fGardening == null || fAllowedRooms == null || fRoomCallbacks == null || fDatingRecords == null
                    || fAcceptedDates == null || mCanAnyMeta == null || mCanFullMeta == null)
                    throw new MissingMemberException("private member not found");
            }
            catch (Exception e) { AppliedLog().Add("AutShadow: off: " + e.Message); sSessionOff = true; return; }
            // Worker shims first: the request hook goes in only when every worker hook did.
            bool ok = Patch(typeof(Sim), "IsAllowedInRoom", new Type[] { typeof(ulong), typeof(int) }, typeof(AutRoomHook), "AllowedHook", "AllowedOrig")
                & Patch(typeof(Sim), "get_NumSimsDoingGardening", null, typeof(AutGardenHook), "GardenHook", "GardenOrig")
                & Patch(typeof(Localization), "LocalizeString", new Type[] { typeof(ulong) }, typeof(AutLocHook), "KeyHook", "KeyOrig")
                & Patch(typeof(Localization), "LocalizeString", new Type[] { typeof(string), typeof(object[]) }, typeof(AutLocHook), "StrHook", "StrOrig")
                & Patch(typeof(Localization), "LocalizeString", new Type[] { typeof(bool), typeof(string), typeof(object[]) }, typeof(AutLocHook), "FemHook", "FemOrig")
                & Patch(typeof(Localization), "LocalizeString", new Type[] { typeof(bool[]), typeof(string), typeof(object[]) }, typeof(AutLocHook), "FemsHook", "FemsOrig")
                & Patch(typeof(OnlineDatingManager), "DidSimsMeetOnline", new Type[] { typeof(ulong), typeof(ulong) }, typeof(AutDatingHook), "MetHook", "MetOrig")
                & Patch(typeof(MetaAutonomyTuning), "GetSimCountForVenueWithCustomDesireFunction", null, typeof(AutDesireHook), "DesireHook", "DesireOrig")
                & Patch(typeof(Punishment), "GetPunishmentManagerFromSim", new Type[] { typeof(Sim) }, typeof(AutPunishHook), "PunishHook", "PunishOrig");
            sCalcPatched = Patch(typeof(AutonomyT), "CalculateScore", new Type[] { typeof(InteractionObjectPair) }, typeof(AutScoreHook), "ScoreHook", "ScoreOrig");
            sHooksOk = ok && sCalcPatched && Patch(typeof(AutonomyT), "ScoreInteractionsForMetaAutonomy", null, typeof(AutMetaHook), "MetaHook", "MetaOrig");
            AppDomain.CurrentDomain.SetData("ts3fixes.autshadow.reasons", sReasons);
            MvidCheck();
        }

        // docs/122 §4.1 step 4: game script assemblies must be the ones the allow-list was built from, else off for the session.
        static void MvidCheck()
        {
            Assembly[] asm = { typeof(Lot).Assembly, typeof(BakeSaleTable).Assembly };
            for (int i = 0; i < asm.Length; i++)
            {
                // the game's mscorlib has no Guid.ToByteArray: compare in "N" format
                string got = asm[i].ManifestModule.ModuleVersionId.ToString("N"), want = AutShadowCore.GuidNFromRaw(kAsmMvid[i]);
                if (got != want)
                {
                    sSessionOff = true;
                    AppliedLog().Add("AutShadow: off for the session: " + asm[i].GetName().Name + " MVID " + got + " != " + want);
                }
            }
        }

        // World load finished (Instantiator): per-load state; ts3native is probed again at the next sampled search.
        internal static void AutShadowWorldLoad()
        {
            sProbe = 0; sPosted = false; sInWork = false; sGameAut = null; sWarmIn = 0; sLogged = 0; sWorkReady = sGameDone = false;
        }

        // ---- probe, warm-up, mod guard (sim thread) ----

        static bool Ready()
        {
            if (sProbe != 0) return sProbe > 0;
            sProbe = -1;
            int v;
            try { v = AutShadowNative.Version(); }
            catch (Exception) { AppliedLog().Add("AutShadow: no ts3native internal calls this load (off)"); return false; }
            if (v != 2) { AppliedLog().Add("AutShadow: ts3native contract version " + v + ", want 2 (off)"); return false; }
            try
            {
                if (!GuardSweep()) return false;
                // Warm-up: nothing is pending, so each returns at once; it transforms them on the sim thread first.
                AutFinish(); AutReference(); AutWork();
                AutRoomHook.Warm(); AutDatingHook.Warm();
                sSample = Math.Max(1, AutShadowNative.Config(1));
                sWarmInMax = AutShadowNative.Config(2);
                AutShadowNative.SetRefs(sEmpty, sFmtException);
                const BindingFlags st = BindingFlags.Static | BindingFlags.NonPublic;
                AutShadowNative.SetMethods(typeof(Fixes).GetMethod("AutFinish", st).MethodHandle.Value,
                    typeof(Fixes).GetMethod("AutReference", st).MethodHandle.Value, typeof(Fixes).GetMethod("AutWork", st).MethodHandle.Value);
            }
            catch (Exception e) { AppliedLog().Add("AutShadow: warm-up failed: " + e.Message); return false; }
            sProbe = 1;
            return true;
        }

        // docs/104 L3: MonoMethod+0x08 (klass) equals the declaring class, read as MonoType.data.klass from TypeHandle.Value
        // [inferred layout; checked below against our own method and our own patch before any verdict is trusted].
        // shortcut: methods of generic classes pass unchecked (an inflated method's klass is the inflated class); add an
        // inflated-class compare if a generic base's Test ever matters.
        static bool L3Ok(MethodBase m)
        {
            Type d = m.DeclaringType;
            if (d == null || d.IsGenericType) return true;
            IntPtr mk = Marshal.ReadIntPtr(new IntPtr(m.MethodHandle.Value.ToInt32() + 8));
            return mk == Marshal.ReadIntPtr(d.TypeHandle.Value);
        }

        static bool AllL3Ok(Type t, string[] names)
        {
            const BindingFlags all = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
            foreach (MethodInfo m in t.GetMethods(all))
                if (Array.IndexOf(names, m.Name) >= 0 && !L3Ok(m)) { AppliedLog().Add("AutShadow: mod guard: " + m.DeclaringType.FullName + "." + m.Name + " replaced"); return false; }
            return true;
        }

        static readonly string[] kDefCallees = { "Test", "MetaAdSatisfiesCommodity", "CalculateScore" };
        static readonly string[] kImplCallees = { "HowMuchItWantsSimToCome", "get_MetaAds" };

        // Per load and whenever MonoPatcher's ForceJIT bytes change: definitions and implementors whose direct callees were
        // swapped are skipped; the shared C2 callees hit = every definition skipped. L2 bytes are logged only (docs/68 M2-3).
        static bool GuardSweep()
        {
            const BindingFlags st = BindingFlags.Static | BindingFlags.NonPublic;
            if (!L3Ok(typeof(Fixes).GetMethod("AutFinish", st)) || (sCalcPatched && L3Ok(typeof(AutonomyT).GetMethod("CalculateScore", new Type[] { typeof(InteractionObjectPair) }))))
            {
                AppliedLog().Add("AutShadow: off for the session: L3 klass self-check failed");
                sSessionOff = true;
                return false;
            }
            sForceJitBytes = ForceJitBytes();
            if (Marshal.ReadInt32(new IntPtr(0xE5CA90)) != 0x246C8D55 || Marshal.ReadInt32(new IntPtr(0xE5CA94)) != 0x08EC8198)
                AppliedLog().Add("AutShadow: L2: GenerateCode 0xE5CA90 hooked (ReplaceIL possible; log only)");
            if (sForceJitBytes != 0x4C742675) AppliedLog().Add("AutShadow: L2: ForceJIT bytes changed (log only)");
            sSharedGuardHit = !AllL3Ok(typeof(InteractionObjectPair), new string[] { "get_Tradeoff", "get_InteractionDefinition", "get_Target", "TestAndCalculateScore" })
                || !AllL3Ok(typeof(Tradeoff), new string[] { "SatisfiesCommodity" })
                || !AllL3Ok(typeof(AutonomyT), new string[] { "CalculateScoreForObjectInteraction" });
            Assembly[] asm = { typeof(Lot).Assembly, typeof(BakeSaleTable).Assembly };
            sDefIndex.Clear();
            for (int k = 0; k < kDefNames.Length; k++)
            {
                Type t = asm[kDefAsm[k]].GetType(kDefNames[k]);
                if (t == null) { AppliedLog().Add("AutShadow: allow-list type missing: " + kDefNames[k]); continue; }
                sDefIndex[t] = k;
                sDefGuardHit[k] = sSharedGuardHit || !AllL3Ok(t, kDefCallees);
            }
            sImplVerdict.Clear();
            return true;
        }

        // ForceJIT sites 0xE5637D (vanilla 75 26) and 0xE648DC (vanilla 74 4C) as one int.
        static int ForceJitBytes()
        {
            return Marshal.ReadByte(new IntPtr(0xE5637D)) | Marshal.ReadByte(new IntPtr(0xE5637E)) << 8
                | Marshal.ReadByte(new IntPtr(0xE648DC)) << 16 | Marshal.ReadByte(new IntPtr(0xE648DD)) << 24;
        }

        // Implementor index of a runtime type: exact type in the 7, or an EA rabbit-hole subclass (RabbitHole itself is
        // abstract; every real one is a subclass in the two MVID-checked assemblies). -1 not allowed, -2 mod guard.
        static int ImplVerdict(Type t)
        {
            int v;
            if (sImplVerdict.TryGetValue(t, out v)) return v;
            v = Array.IndexOf(kImpl, t);
            if (v < 0 && t.IsSubclassOf(typeof(RabbitHole)) && (t.Assembly == typeof(Lot).Assembly || t.Assembly == typeof(BakeSaleTable).Assembly))
                v = kImplRabbitHole;
            if (v >= 0 && !AllL3Ok(t, kImplCallees)) v = -2;
            sImplVerdict[t] = v;
            return v;
        }

        // ---- request (sim thread, B13 prefix) ----

        static void TryRequest(AutonomyT a, CommodityKind c)
        {
            if (++sCalls % sSample != 0 || sPosted || sGameAut != null || !Ready()) return;
            int mode = AutShadowNative.Config(0);
            if (mode < 0) return;
            sSample = Math.Max(1, AutShadowNative.Config(1));
            if (ForceJitBytes() != sForceJitBytes && !GuardSweep()) return;
            Sim actor = a.Actor;
            if (actor == null || !(bool)mCanAnyMeta.Invoke(a, null)) return;
            int[] cnt = PatchCounters();
            if (!AutShadowNative.Begin()) { cnt[119]++; return; }
            Request q = sReq;
            q.Id = ++sNextId; q.Mode = mode; q.Actor = actor; q.Aut = a; q.C = c;
            q.SearchType = a.CurrentSearchType; q.Flags = a.GetInteractionFlagsForCurrentSearch(); q.Hours = SimClock.HoursPassedOfDay;
            q.Objs = q.Pairs = 0;
            sRef.Id = sWork.Id = -1;
            sPairIndex.Clear(); sReqLots.Clear(); sBakeSales.Clear();
            Array.Clear(sGameHas, 0, sGameHas.Length);
            try
            {
                sGardening = (int)fGardening.GetValue(null);
                AutShadowNative.AddRow(actor.ObjectId.Value, 1);
                if ((bool)mCanFullMeta.Invoke(a, null))
                {
                    foreach (IMetaObject m in AutonomyT.PublicMetaObjects) AddObj(q, m);
                    List<IMetaObject> known = fLotsKnown.GetValue(a) as List<IMetaObject>;
                    if (known != null) foreach (IMetaObject m in known) AddObj(q, m);
                }
                if (a.CanRunLocalAutonomy())
                {
                    if (actor.LotCurrent != null) AddObj(q, actor.LotCurrent);
                    if (actor.LotCurrent != actor.LotHome && actor.LotHome != null) AddObj(q, actor.LotHome);
                    if (actor.CareerLocation != null) AddObj(q, actor.CareerLocation);
                }
                foreach (Sim s in LotManager.Actors)
                    if (s != null && s != actor) AutShadowNative.AddRow(s.ObjectId.Value, Reachable(s) ? 1 : 2);
            }
            catch (Exception e) { cnt[121]++; AutNote("request: " + e.Message); }
            q.Start[q.Objs] = q.Pairs;
            sPosted = true;
            sGameAut = a; sGameDone = sWorkReady = false;
            cnt[118]++;
            AutShadowNative.Post(q.Id);
        }

        static void AddObj(Request q, IMetaObject m)
        {
            if (m == null || m.HasBeenDestroyed || q.Objs >= kMaxObjs) return;
            int o = q.Objs++;
            q.Obj[o] = m;
            q.Start[o] = q.Pairs;
            q.ObjImpl[o] = ImplVerdict(m.GetType());
            AutShadowNative.AddRow(m.ObjectId.Value, 0);
            Lot lot = m as Lot ?? m.LotCurrent;
            if (lot != null) { sReqLots[lot.LotId] = true; if (lot != m) AutShadowNative.AddRow(lot.ObjectId.Value, 0); }
            if (m is BakeSaleTable) sBakeSales.Add(m.Position);
            ArrayList ads = m.MetaAds;
            if (ads == null) return;
            for (int i = 0; i < ads.Count && q.Pairs < kMaxPairs; i++)
            {
                InteractionObjectPair iop = ads[i] as InteractionObjectPair;
                if (iop == null) continue;
                int p = q.Pairs++, k;
                q.Pair[p] = iop;
                q.PairObj[p] = o;
                q.Def[p] = iop.InteractionDefinition != null && sDefIndex.TryGetValue(iop.InteractionDefinition.GetType(), out k) ? (sDefGuardHit[k] ? -2 : k) : -1;
                sPairIndex[iop] = p;
            }
        }

        // Kind 1 Sims (docs/122 §2.3): on a scored lot or within a bake sale table's meta radius; the rest are kind 2.
        static bool Reachable(Sim s)
        {
            Lot l = s.LotCurrent;
            if (l != null && sReqLots.ContainsKey(l.LotId)) return true;
            if (sBakeSales.Count == 0) return false;
            Vector3 p = s.Position;
            float r2 = BakeSaleTable.kMetaAutonomyDesiredRadius * BakeSaleTable.kMetaAutonomyDesiredRadius;
            foreach (Vector3 b in sBakeSales) if ((p - b).LengthSqr() <= r2) return true;
            return false;
        }

        // ---- native entry points (no arguments; request and results live in the statics above) ----

        static void AutReference()
        {
            if (!sPosted) return;
            if (sReq.Mode == 2)
            {
                if (sWarmIn >= sWarmInMax) return;
                sWarmIn++;
                PatchCounters()[117]++;
            }
            ScoreMeta(sReq, sRef);
        }

        // Worker thread: no counters, no logs, no Simulator calls; everything it learns goes into sWork.
        static void AutWork()
        {
            if (!sPosted) return;
            sInWork = true;
            try { ScoreMeta(sReq, sWork); }
            finally { sInWork = false; }
        }

        static void AutFinish()
        {
            if (!sPosted) return;
            sPosted = false;
            sInWork = false;
            int lr = AutShadowNative.LastRun();
            int[] c = PatchCounters();
            c[121] += (sRef.Id == sReq.Id ? sRef.Exceptions : 0) + (sWork.Id == sReq.Id ? sWork.Exceptions : 0) + (lr == 5 ? 1 : 0);
            if (lr == 1 || lr == 2 || (lr == 0 && !sWork.Done)) { c[111]++; return; }
            if (lr != 0 || sWork.Id != sReq.Id) return;
            for (int i = 0; i < sReq.Pairs; i++)
                if (sWork.Code[i] == AutShadowCore.kSkip) { c[116]++; if (sReq.Def[i] == -2 || sReq.ObjImpl[sReq.PairObj[i]] == -2) c[120]++; }
            if (sReq.Mode == 1 && sRef.Id == sReq.Id && sRef.Done) CompareR(c);
            sWorkReady = true;
            TryLReport();
        }

        static void CompareR(int[] c)
        {
            int[] n = new int[6];
            for (int i = 0; i < sReq.Pairs; i++)
            {
                int o = sReq.PairObj[i];
                int k = AutShadowCore.Classify(sRef.Code[i], sRef.Score[i], sRef.Meta[o], sWork.Code[i], sWork.Score[i], sWork.Meta[o]);
                if (k < 0) continue;
                c[106 + k]++; n[k]++;
                if (k == AutShadowCore.A4)
                {
                    int r = sWork.Code[i] == AutShadowCore.kAbort ? sWork.Reason[i] : sRef.Code[i] == AutShadowCore.kAbort ? sRef.Reason[i] : -1;
                    if (r >= 0 && r < sReasons.Length) sReasons[r]++;
                }
                else if (k != AutShadowCore.A0 && sLogged < kMaxLogPerLoad) { sLogged++; LogPair(i, k); }
            }
            if (ShortcutOn("autshadowverify"))
                AutShadowNative.Log("{\"t\":\"req\",\"req\":" + sReq.Id + ",\"actor\":" + sReq.Actor.ObjectId.Value + ",\"objs\":" + sReq.Objs + ",\"pairs\":" + sReq.Pairs
                    + ",\"a\":[" + n[0] + "," + n[1] + "," + n[2] + "," + n[3] + "," + n[4] + "]}");
        }

        static void LogPair(int i, int k)
        {
            try
            {
                InteractionObjectPair p = sReq.Pair[i];
                int o = sReq.PairObj[i];
                StringBuilder s = new StringBuilder(320);
                s.Append("{\"t\":\"A").Append(k).Append("\",\"req\":").Append(sReq.Id).Append(",\"actor\":").Append(sReq.Actor.ObjectId.Value)
                 .Append(",\"meta\":").Append(sReq.Obj[o].ObjectId.Value).Append(",\"def\":\"").Append(p.InteractionDefinition.GetType().FullName)
                 .Append("\",\"target\":").Append(p.Target != null ? p.Target.ObjectId.Value : 0UL);
                AutNum(s, "ref", sRef.Score[i]); AutNum(s, "work", sWork.Score[i]); AutNum(s, "refMeta", sRef.Meta[o]); AutNum(s, "workMeta", sWork.Meta[o]);
                s.Append(",\"refCode\":").Append(sRef.Code[i]).Append(",\"workCode\":").Append(sWork.Code[i]).Append('}');
                AutShadowNative.Log(s.ToString());
            }
            catch (Exception) { }
        }

        static void AutNum(StringBuilder s, string name, float f)
        {
            s.Append(",\"").Append(name).Append("\":");
            if (float.IsNaN(f) || float.IsInfinity(f)) s.Append('"').Append(f.ToString(CultureInfo.InvariantCulture)).Append('"');
            else s.Append(f.ToString("R", CultureInfo.InvariantCulture));
            s.Append(",\"").Append(name).Append("Bits\":\"").Append(AutShadowCore.Bits(f).ToString("X8")).Append('"');
        }

        // L report once both the worker result (AutFinish) and the game's own B13 (MetaHook exit) are in for the request.
        static void TryLReport()
        {
            if (!sWorkReady || !sGameDone) return;
            sWorkReady = sGameDone = false;
            AutShadowCore.LReport r = AutShadowCore.Report(sGameScore, sGameHas, sWork.Score, sWork.Code, sReq.Pairs);
            int[] c = PatchCounters();
            c[112] += r.Pairs; c[113] += r.Equal; c[114] += r.TestAgree;
            if (r.Top1Agree) c[115]++;
        }

        static void AutNote(string what)
        {
            List<object> log = AppliedLog();
            if (log.Count < 400) log.Add("AutShadow: " + what);
        }

        // ---- driver (sim thread for the reference, worker for the run) ----

        // Our own B14/C1/C2 per meta object (docs/122 §3.6): commodity filter, then the game's Test and score on the game's
        // pair objects (S1: C2 writes nothing into them). No BestScores, no InteractionCheckTable, no yield, no stopwatch,
        // no events. Abort reasons are read after each meta object; a stop from the sim abandons the rest (A5).
        public static void ScoreMeta(Request q, Result r)
        {
            r.Id = q.Id; r.Exceptions = 0; r.Done = false;
            for (int o = 0; o < q.Objs; o++)
            {
                int s = q.Start[o], e = q.Start[o + 1];
                r.Meta[o] = 0f;
                if (AutShadowNative.Stopped())
                {
                    for (int i = s; i < q.Pairs; i++) { r.Code[i] = AutShadowCore.kAbort; r.Reason[i] = kReasonStop; r.Score[i] = 0f; }
                    return;
                }
                for (int i = s; i < e; i++) { r.Score[i] = 0f; r.Code[i] = AutShadowCore.kSkip; r.Reason[i] = -1; }
                if (q.ObjImpl[o] < 0) continue;
                try
                {
                    r.Meta[o] = q.Obj[o].HowMuchItWantsSimToCome(q.Actor, q.Hours);
                    for (int i = s; i < e; i++)
                        if (q.Def[i] >= 0) r.Code[i] = ScorePair(q, q.Pair[i], out r.Score[i]);
                }
                catch (Exception)
                {
                    r.Exceptions++;
                    for (int i = s; i < e; i++) if (q.Def[i] >= 0) { r.Code[i] = AutShadowCore.kAbort; r.Reason[i] = kReasonExc; }
                }
                int ab = AutShadowNative.Aborted();
                if (ab >= 0)
                {
                    for (int i = s; i < e; i++) if (q.Def[i] >= 0) { r.Code[i] = AutShadowCore.kAbort; r.Reason[i] = ab; }
                    AutShadowNative.ResetAbort();
                }
            }
            r.Done = true;
        }

        static int ScorePair(Request q, InteractionObjectPair iop, out float score)
        {
            score = 0f;
            Tradeoff t = iop.Tradeoff;
            if (t == null) return AutShadowCore.kFiltered;
            bool want = q.C == CommodityKind.None || t.SatisfiesCommodity(q.C);
            if (!want)
            {
                IMetaInteractionDefinition md = iop.InteractionDefinition as IMetaInteractionDefinition;
                if (md != null) want = md.MetaAdSatisfiesCommodity(iop.Target as Lot, q.C);
            }
            if (!want) return AutShadowCore.kFiltered;
            InteractionFlags flags = q.Flags;
            if (q.Aut.OverrideInteractionParameters.HasValue) flags |= q.Aut.OverrideInteractionParameters.Value.Flags;
            InteractionInstanceParameters p = new InteractionInstanceParameters(iop, q.Actor, new InteractionPriority(InteractionPriorityLevel.Autonomous), q.SearchType, flags);
            GreyedOutTooltipCallback tip = null;
            if (iop.InteractionDefinition.Test(ref p, ref tip) != InteractionTestResult.Pass) return AutShadowCore.kTestFailed;
            IOverridesCalculateScore ov = iop.InteractionDefinition as IOverridesCalculateScore;
            score = ov != null ? ov.CalculateScore(iop, q.Aut) : q.Aut.CalculateScoreForObjectInteraction(iop);
            return AutShadowCore.kPass;
        }

        static bool OnWorker() { return sInWork && AutShadowNative.IsWorker(); }

        // ---- hooks ----

        // B13 (private; its YieldResult is a private enum, so int stands in for it). Request prefix, then the game's own
        // method, recorded for the L report while it runs (it may yield across ticks).
        public class AutMetaHook
        {
            public int MetaHook(CommodityKind c)
            {
                AutonomyT a = (AutonomyT)(object)this;
                bool mine = false;
                if (!sSessionOff && ShortcutOn("autshadow"))
                {
                    try { TryRequest(a, c); } catch (Exception) { PatchCounters()[121]++; }
                    mine = sGameAut == a && sPosted;
                }
                if (!mine) return MetaOrig(c);
                try { return MetaOrig(c); }
                finally
                {
                    if (sGameAut == a) { sGameAut = null; sGameDone = true; TryLReport(); }
                }
            }
            public int MetaOrig(CommodityKind c) { return 0; } // receives Autonomy.ScoreInteractionsForMetaAutonomy's original header
        }

        // C2 entry used by C1: records what the game scored for the sampled search (same answer returned).
        public class AutScoreHook
        {
            public float ScoreHook(InteractionObjectPair iop)
            {
                if (sGameAut == null || (object)this != sGameAut) return ScoreOrig(iop);
                float s = ScoreOrig(iop);
                int i;
                if (iop != null && sPairIndex.TryGetValue(iop, out i)) { sGameScore[i] = s; sGameHas[i] = true; }
                return s;
            }
            public float ScoreOrig(InteractionObjectPair iop) { return 0f; } // receives Autonomy.CalculateScore(iop)'s original header
        }

        // Sim.IsAllowedInRoom(lotId, roomId) on the worker: read the Sim's room cache, never recalculate it (a W write).
        public class AutRoomHook
        {
            public bool AllowedHook(ulong lotId, int roomId)
            {
                if (!OnWorker()) return AllowedOrig(lotId, roomId);
                Sim s = (Sim)(object)this;
                Lot cur = s.LotCurrent;
                if (cur == null) { AutShadowNative.Abort(10); return false; }
                if (lotId != cur.LotId && !Cached(s, cur.LotId, 0, false)) return false;
                return Cached(s, lotId, roomId == -1 ? 0 : roomId, false);
            }
            public bool AllowedOrig(ulong lotId, int roomId) { return false; } // receives Sim.IsAllowedInRoom(ulong, int)'s original header

            // I-122-37: mint transforms a method on its first call; warm Cached on the sim thread (lot 0 -> early return, no abort).
            internal static void Warm() { Cached(null, 0, 0, true); }

            static bool Cached(Sim s, ulong lotId, int roomId, bool warm)
            {
                Lot lot = LotManager.GetLot(lotId);
                if (lot == null) { if (!warm) AutShadowNative.Abort(10); return false; }
                ICollection cb = fRoomCallbacks.GetValue(lot) as ICollection;
                if (cb != null && cb.Count > 0) { AutShadowNative.Abort(9); return false; }
                Dictionary<ulong, Pair<int, Dictionary<int, bool>>> rooms = fAllowedRooms.GetValue(s) as Dictionary<ulong, Pair<int, Dictionary<int, bool>>>;
                Pair<int, Dictionary<int, bool>> e;
                if (rooms == null || !rooms.TryGetValue(lotId, out e) || e.Second == null || lot.GetObjectCacheVersion<IPortalConnectionObject>() != e.First)
                { AutShadowNative.Abort(10); return false; }
                bool v;
                return e.Second.TryGetValue(roomId, out v) && v;
            }
        }

        // Sim.NumSimsDoingGardening on the worker: the count read at the request, no clock write (I-121-15).
        public static class AutGardenHook
        {
            public static int GardenHook() { return OnWorker() ? sGardening : GardenOrig(); }
            public static int GardenOrig() { return 0; } // receives Sim.get_NumSimsDoingGardening's original header
        }

        // Localization.LocalizeString on the worker: the rooted empty string (no EAText, no FillInTokens RNG draw).
        public static class AutLocHook
        {
            public static string KeyHook(ulong stringKey) { return OnWorker() ? sEmpty : KeyOrig(stringKey); }
            public static string KeyOrig(ulong stringKey) { return null; }
            public static string StrHook(string entryKey, object[] parameters) { return OnWorker() ? sEmpty : StrOrig(entryKey, parameters); }
            public static string StrOrig(string entryKey, object[] parameters) { return null; }
            public static string FemHook(bool isFemale, string entryKey, object[] parameters) { return OnWorker() ? sEmpty : FemOrig(isFemale, entryKey, parameters); }
            public static string FemOrig(bool isFemale, string entryKey, object[] parameters) { return null; }
            public static string FemsHook(bool[] isFemale, string entryKey, object[] parameters) { return OnWorker() ? sEmpty : FemsOrig(isFemale, entryKey, parameters); }
            public static string FemsOrig(bool[] isFemale, string entryKey, object[] parameters) { return null; }
        }

        // H4: OnlineDatingManager.DidSimsMeetOnline on the worker reads the records and never creates one (a missing record
        // answers as a fresh one: no accepted dates, I-122-15).
        public static class AutDatingHook
        {
            public static bool MetHook(ulong a, ulong b)
            {
                if (!OnWorker()) return MetOrig(a, b);
                if (!OnlineDatingManager.IsActive) return false;
                IDictionary recs = fDatingRecords.GetValue(OnlineDatingManager.Instance) as IDictionary;
                return recs != null && (Accepted(recs, a, b) || Accepted(recs, b, a));
            }
            public static bool MetOrig(ulong a, ulong b) { return false; } // receives OnlineDatingManager.DidSimsMeetOnline's original header

            // I-122-37: the game never calls IDictionary.Contains/indexer on this dictionary type; run them once on the sim thread.
            internal static void Warm()
            {
                IDictionary d = Activator.CreateInstance(fDatingRecords.FieldType) as IDictionary;
                if (d == null) return;
                d.Add(1UL, null);
                Accepted(d, 1UL, 2UL);
            }

            static bool Accepted(IDictionary recs, ulong owner, ulong other)
            {
                object rec = recs.Contains(owner) ? recs[owner] : null;
                List<ulong> dates = rec != null ? fAcceptedDates.GetValue(rec) as List<ulong> : null;
                return dates != null && dates.Contains(other);
            }
        }

        // H5: the tuning's reflection desire function is unknown to the closure: abort on the worker.
        public class AutDesireHook
        {
            public float DesireHook(IMetaObject venue, float hours24, bool bAtleastOneActiveSimAtVenue)
            {
                if (!OnWorker()) return DesireOrig(venue, hours24, bAtleastOneActiveSimAtVenue);
                AutShadowNative.Abort(15);
                return 0f;
            }
            public float DesireOrig(IMetaObject venue, float hours24, bool bAtleastOneActiveSimAtVenue) { return 0f; }
        }

        // H6: Punishment.GetPunishmentManagerFromSim on the worker: an existing manager is read; creating one aborts.
        public static class AutPunishHook
        {
            public static Punishment PunishHook(Sim sim)
            {
                if (!OnWorker()) return PunishOrig(sim);
                if (sim == null || !sim.CanBePunished) return null;
                if (sim.Punishment == null) AutShadowNative.Abort(16);
                return sim.Punishment;
            }
            public static Punishment PunishOrig(Sim sim) { return null; } // receives Punishment.GetPunishmentManagerFromSim's original header
        }
    }
}
