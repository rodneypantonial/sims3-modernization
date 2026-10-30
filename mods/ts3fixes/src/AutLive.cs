using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using Sims3.Gameplay;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.ActorSystems;
using Sims3.Gameplay.Autonomy;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Careers;
using Sims3.Gameplay.Core;
using Sims3.Gameplay.Interactions;
using Sims3.Gameplay.Interfaces;
using Sims3.SimIFace;
using AutonomyT = Sims3.Gameplay.Autonomy.Autonomy;

namespace Ts3Fixes
{
    // [AutShadow] go-live (docs/124 §16-§17): the game uses the worker's meta scores. ts3native [AutShadow] Mode=3 plus the
    // ts3fixes switches autshadow and autlive (both off by default; Mode 3 with autlive off = the game's search, no request).
    // Per manager meta search (B13 entry, the manager task):
    //   1. eligible (manager search, a yielding context, not the inspection window): request as Mode R (TryRequest), the
    //      actor fields kept for the resume compare, the meta objects and their lots watched for ObjStale; then
    //      Simulator.Sleep(0) until the result is ready (AutFinish copied it) or MaxWait ticks passed. After each yield the
    //      game's own YieldIfPossible returns: interrupted -> SearchInterrupted, actor destroyed -> ActorDestroyed.
    //   2. result checks (§3.1): run status, world epoch since the cut, game state, LotCurrent, head interaction, actor fields.
    //      Any miss: the game's B13 as today (counted by reason).
    //   3. selection: per object valid = allow-listed implementor, no worker abort, no AlwaysChooseBest ad, no event since the
    //      cut (ObjStale on the object and its lot); the worker's passing pairs of valid objects by score; band 1 = TopN.
    //   4. the game's B13 runs; the B14 hook replaces the body for valid objects in the request: per ad in MetaAds order, the
    //      game's commodity filter, then off-list ads and band-1 pairs through the game's C1 (live Test + score + BestScores);
    //      other objects, objects after a B13 yield (late) and objects not in the request run the game's B14.
    //   5. after B13 (still in the manager task, before D2): the §16.1 bands through C1 (AutLiveCore.Bands).
    // Dry=1 (G7): steps 1-3 as live, then the game's own B13 unchanged; its C1 calls are recorded and replayed with and
    // without what consume would hold back (AutLiveCore.Heap), compared with the game's queue: top-10 set and top 1.
    // Counters: ts3native ts3native_autshadow.al[] (LiveStat, slots kS* below). Records: autshadow.jsonl live-drop, dry-diff.
    public static partial class Fixes
    {
        // al[] slots (docs/124 §17.3, docs/registry.md autshadow v20)
        const int kSEligible = 0, kSPosted = 1, kSConsumed = 2, kSDryRuns = 3, kSNotPosted = 4, kSDropWait = 5, kSDropStopped = 6,
            kSDropEpochRun = 7, kSDropNoWorker = 8, kSDropExc = 9, kSDropEpoch = 10, kSDropLot = 11, kSDropActor = 12, kSDropState = 13,
            kSInterrupted = 14, kSDestroyed = 15, kSObjNotInReq = 16, kSObjInvalid = 17, kSObjStale = 18, kSObjIdent = 19, kSObjAcb = 20,
            kSObjLate = 21, kSObjConsumed = 22, kSPairsBand1 = 23, kSPairsBand = 24, kSPairsOffList = 25, kSBand2 = 26, kSBand3 = 27,
            kSBandFull = 28, kSWhyCount = 29, kSWhyMargin = 30, kSDrift0 = 31 /* 31-35: drift bins */, kSDryTop10 = 36, kSDryTop1 = 37,
            kSDrySimMis = 38, kSApplyUs = 39, kSWaitTicks = 40, kSVerifyOff = 41, kSCandidates = 42, kSDropHead = 43,
            kSDryMissStale = 44, kSDryMissBand = 45, kSGuardOff = 46, kSVerifyRuns = 47, kSVerifyTop1 = 48, kSVerifyTop10 = 49,
            kSPairsFilterDiff = 50, kSDropOther = 51, kSDryOffList = 52, kSDryMissOther = 53;
        const int kLiveMaxLogPerReason = 20, kDryMaxLog = 50;

        public sealed class LiveSearch
        {
            public bool InUse, Ready, Ok, Abandoned, Dry, Late;
            public int Id, LastRun, Objs, Pairs, CutEpoch, CutFrame, PostFrame, ReadyFrame, ResumeFrame;
            public AutonomyT Aut;
            public Sim Actor;
            public IMetaObject[] Obj = new IMetaObject[64];
            public int[] ObjImpl = new int[64], Start = new int[65], ObjWhy = new int[64];
            public ulong[] ObjKey = new ulong[64], LotKey = new ulong[64];
            public bool[] Visited = new bool[64], Consumed = new bool[64];
            public InteractionObjectPair[] Pair = new InteractionObjectPair[128];
            public int[] PairObj = new int[128], Def = new int[128], Code = new int[128], Buf = new int[128];
            public float[] W = new float[128], Live = new float[128];
            public bool[] Acb = new bool[128], LiveHas = new bool[128], Sent = new bool[128], Sel = new bool[128], Eligible = new bool[128];
            public readonly Dictionary<IMetaObject, int> ObjIndex = new Dictionary<IMetaObject, int>();
            public readonly Dictionary<InteractionObjectPair, int> PairIndex = new Dictionary<InteractionObjectPair, int>();
            public readonly AutLiveCore.Bands Bands = new AutLiveCore.Bands();
            // actor fields at the request (docs/124 §3.1, §13.6)
            public int AgeSpecies, JobCount;
            public Lot LotCur, LotHome;
            public object Household, Role, Occ, Job;
            public IMetaObject Career;
            public bool Selectable, Chip;
            // dry: the game's C1 calls during its B13 (pair index, or a negative id for pairs outside the request)
            public int EvN;
            public int[] EvId = new int[128];
            public float[] EvScore = new float[128];
            public bool[] EvSend = new bool[128];
            public readonly Dictionary<InteractionObjectPair, int> OtherId = new Dictionary<InteractionObjectPair, int>();

            public void Size(int objs, int pairs)
            {
                if (Obj.Length < objs)
                {
                    int n = Math.Max(objs, Obj.Length * 2);
                    Obj = new IMetaObject[n]; ObjImpl = new int[n]; Start = new int[n + 1]; ObjWhy = new int[n];
                    ObjKey = new ulong[n]; LotKey = new ulong[n]; Visited = new bool[n]; Consumed = new bool[n];
                }
                if (Pair.Length < pairs)
                {
                    int n = Math.Max(pairs, Pair.Length * 2);
                    Pair = new InteractionObjectPair[n]; PairObj = new int[n]; Def = new int[n]; Code = new int[n]; Buf = new int[n];
                    W = new float[n]; Live = new float[n]; Acb = new bool[n]; LiveHas = new bool[n]; Sent = new bool[n]; Sel = new bool[n];
                    Eligible = new bool[n];
                }
                if (Bands.Order.Length < pairs) Bands.Order = new int[Math.Max(pairs, Bands.Order.Length * 2)];
            }
            public void AddEvent(int id, float score)
            {
                if (EvN == EvId.Length)
                {
                    int n = EvN * 2;
                    int[] a = new int[n]; Array.Copy(EvId, a, EvN); EvId = a;
                    float[] s = new float[n]; Array.Copy(EvScore, s, EvN); EvScore = s;
                    EvSend = new bool[n];
                }
                EvId[EvN] = id; EvScore[EvN] = score; EvN++;
            }
        }

        static readonly LiveSearch[] sLive = new LiveSearch[8];
        static LiveSearch sLiveCur;     // the search whose game B13 runs now with the consumer on (sim thread)
        static bool sLiveMode, sLiveOff, sB14Ok, sLiveGuardOk;
        static int sLiveTopN = 10, sLiveBands = 3, sLiveMaxWait = 3;
        static float sLiveMargin;
        static bool sLiveDry;
        static readonly int[] sLiveLogged = new int[64];
        static int sDryLogged;
        static FieldInfo fInterrupted, fInspection, fBestScores;
        static StopWatch sLiveSw;

        static void LiveStat(int slot) { AutShadowNative.LiveStat(slot, 1); }

        // Ready(), sim thread, once per load: Mode 3 and its settings
        static void LiveReady()
        {
            sLiveMode = AutShadowNative.Config(0) == 3;
            Array.Clear(sLiveLogged, 0, sLiveLogged.Length);
            sDryLogged = 0;
            if (!sLiveMode) return;
            sLiveTopN = AutShadowNative.Config(13);
            sLiveBands = AutShadowNative.Config(14);
            sLiveMargin = AutShadowNative.Config(15) / 1000f;
            sLiveMaxWait = AutShadowNative.Config(16);
            sLiveDry = AutShadowNative.Config(18) != 0;
            const BindingFlags inst = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public;
            fInterrupted = typeof(AutonomyT).GetField("mAutonomySearchWasInterrupted", inst);
            fInspection = typeof(AutonomyT).GetField("IsRunningAutonomyForInspectionWindow", inst);
            fBestScores = typeof(AutonomyT).GetField("mBestScores", inst);
            if (fInterrupted == null || fInspection == null || fBestScores == null)
            {
                AppliedLog().Add("AutShadow: go-live off: a private Autonomy field was not found");
                sLiveMode = false;
                return;
            }
            if (sLiveSw == null) sLiveSw = StopWatch.Create(StopWatch.TickStyles.Microseconds);
            AppliedLog().Add("AutShadow: go-live Mode 3 (" + (sLiveDry ? "dry" : "acting") + ") TopN " + sLiveTopN + " Bands " + sLiveBands + " Margin "
                + sLiveMargin + " MaxWait " + sLiveMaxWait + (sB14Ok ? "" : "; B14 hook missing: off") + (sLiveGuardOk ? "" : "; consumer path swapped: off"));
        }

        // 1 = consume, -1 = Mode 3 with autlive off (the game's search, no request), 0 = not Mode 3 (shadow modes as before)
        static int LiveWanted()
        {
            if (sProbe == 0)
            {
                try { Ready(); } catch (Exception) { }
            }
            if (sProbe <= 0 || !sLiveMode) return 0;
            return ShortcutOn("autlive") ? 1 : -1;
        }

        // docs/124 §13.5: the 18 consumer-path methods by exact signature; ours (B13, B14, C2) pass as own swaps
        static bool LiveGuard()
        {
            Type ip = typeof(InteractionObjectPair), ck = typeof(CommodityKind);
            bool ok = AllL3Ok(typeof(AutonomyT), new string[] { "ScoreInteractionsOnObjects", "ScoreInteractionsForMetaAutonomy", "YieldIfPossible",
                    "ScoreInteractionsForMetaObject", "CalculateScoreAndAddToCandidates", "CalculateScore", "AssignProbabilitiesAndChooseInteraction" },
                    new Type[][] { new Type[] { ck, typeof(bool) }, new Type[] { ck }, Type.EmptyTypes, new Type[] { typeof(IMetaObject), ck },
                        new Type[] { ip }, new Type[] { ip }, Type.EmptyTypes })
                && AllL3Ok(ip, new string[] { "get_Tuning" }, new Type[][] { Type.EmptyTypes })
                && AllL3Ok(typeof(BestScores), new string[] { "Add", "AddInteractionOnlyIfBest", "ChooseInteraction", "ChooseInteractions" },
                    new Type[][] { new Type[] { ip, typeof(float) }, new Type[] { ip, typeof(float) }, new Type[] { typeof(float), typeof(DecisionSnapshot) },
                        new Type[] { typeof(int) } });
            if (!ok) AppliedLog().Add("AutShadow: go-live off for the load: a consumer-path method is swapped (docs/124 §13.5)");
            return ok;
        }

        static LiveSearch LiveAcquire()
        {
            int now = AutShadowNative.Config(19);
            for (int i = 0; i < sLive.Length; i++)
            {
                LiveSearch L = sLive[i];
                if (L == null) return sLive[i] = Taken(new LiveSearch(), now);
                // a search whose task never came back (a reset destroys the parked task): reclaimed
                if (L.InUse && ((L.Ready && now - L.ReadyFrame > 64) || (!L.Ready && now - L.PostFrame > 600))) LiveRelease(L);
                if (!L.InUse) return Taken(L, now);
            }
            return null;
        }
        static LiveSearch Taken(LiveSearch L, int now) { L.InUse = true; L.PostFrame = now; return L; }

        static void LiveRelease(LiveSearch L)
        {
            Array.Clear(L.Obj, 0, L.Objs);
            Array.Clear(L.Pair, 0, L.Pairs);
            L.ObjIndex.Clear(); L.PairIndex.Clear(); L.OtherId.Clear();
            L.Aut = null; L.Actor = null; L.LotCur = L.LotHome = null; L.Household = L.Role = L.Occ = L.Job = null; L.Career = null;
            L.Ready = L.Ok = L.Abandoned = L.Late = false; L.EvN = 0; L.Objs = L.Pairs = 0;
            L.InUse = false;
        }
        static void LiveAbandon(LiveSearch L) { if (L.Ready) LiveRelease(L); else L.Abandoned = true; }

        // TryRequest, after the request is built (sim thread, before Post)
        static void LiveFill(LiveSearch L, Request q)
        {
            L.Size(q.Objs, q.Pairs);
            L.Id = q.Id; L.Aut = q.Aut; L.Actor = q.Actor; L.Objs = q.Objs; L.Pairs = q.Pairs;
            L.Ready = L.Ok = L.Abandoned = L.Late = false; L.Dry = sLiveDry; L.EvN = 0;
            L.ObjIndex.Clear(); L.PairIndex.Clear(); L.OtherId.Clear();
            for (int o = 0; o < q.Objs; o++)
            {
                IMetaObject m = q.Obj[o];
                L.Obj[o] = m; L.ObjImpl[o] = q.ObjImpl[o]; L.Start[o] = q.Start[o];
                L.Visited[o] = L.Consumed[o] = false;
                if (!L.ObjIndex.ContainsKey(m)) L.ObjIndex[m] = o;
                L.ObjKey[o] = m.ObjectId.Value;
                Lot lot = m as Lot ?? m.LotCurrent;
                L.LotKey[o] = lot != null ? lot.LotId : 0UL;
                AutShadowNative.LiveWatch(L.ObjKey[o], 0);
                if (L.LotKey[o] != 0UL) AutShadowNative.LiveWatch(L.LotKey[o], 1);
            }
            L.Start[q.Objs] = q.Pairs;
            for (int p = 0; p < q.Pairs; p++)
            {
                L.Pair[p] = q.Pair[p]; L.PairObj[p] = q.PairObj[p]; L.Def[p] = q.Def[p]; L.Acb[p] = q.Acb[p];
                L.LiveHas[p] = L.Sent[p] = L.Sel[p] = L.Eligible[p] = false;
                if (!L.PairIndex.ContainsKey(q.Pair[p])) L.PairIndex[q.Pair[p]] = p;
            }
            ActorFields(L.Actor, out L.AgeSpecies, out L.LotCur, out L.LotHome, out L.Household, out L.Selectable, out L.Role, out L.Occ,
                out L.JobCount, out L.Job, out L.Career, out L.Chip);
        }

        static void ActorFields(Sim s, out int ageSpecies, out Lot lotCur, out Lot lotHome, out object household, out bool selectable,
            out object role, out object occ, out int jobCount, out object job, out IMetaObject career, out bool chip)
        {
            SimDescription d = s.SimDescription;
            ageSpecies = d != null ? (int)(d.Age | d.Species) : -1;
            lotCur = s.LotCurrent; lotHome = s.LotHome;
            household = d != null ? d.Household : null;
            selectable = s.IsSelectable;
            role = d != null ? d.AssignedRole : null;
            Occupation o = d != null ? d.Occupation : null;
            occ = o;
            jobCount = o != null ? o.JobCount : -1;
            job = o != null ? o.CurrentJob : null;
            career = s.CareerLocation;
            chip = s.HasTrait(TraitNames.CapacityToLoveChip);
        }

        // AutFinish (sim thread, end of frame): the worker's answers into the search; valid = the run (or its kept part) counts
        internal static void LiveFinish(LiveSearch L, Request q, Result w, int lastRun, bool valid)
        {
            if (L == null || !L.InUse || L.Id != q.Id) return;
            for (int p = 0; p < L.Pairs; p++) { L.Code[p] = w.Code[p]; L.W[p] = w.Score[p]; }
            L.LastRun = lastRun;
            L.Ok = valid && w.Id == q.Id;
            L.CutEpoch = AutShadowNative.Config(21);
            L.CutFrame = AutShadowNative.Config(22);
            L.ReadyFrame = AutShadowNative.Config(19);
            L.Ready = true;
            if (L.Abandoned) LiveRelease(L);
        }

        static bool LiveEligible(AutonomyT a)
        {
            if (sLiveOff || !sB14Ok || !sLiveGuardOk || a.Actor == null) return false;
            if (a.CurrentSearchType != AutonomySearchType.Autonomy || (bool)fInspection.GetValue(a)) return false;
            return Simulator.CheckYieldingContext(false);
        }

        // MetaHook with Mode 3 and autlive on. Returns B13's YieldResult as int (0 Continue, 1 SearchInterrupted, 2 ActorDestroyed).
        static int LiveMeta(AutMetaHook h, AutonomyT a, CommodityKind c)
        {
            if (!LiveEligible(a)) return h.MetaOrig(c);
            LiveStat(kSEligible);
            LiveSearch L = null;
            try
            {
                Request q = TryRequest(a, c, true);
                L = q != null ? q.Live : null;
            }
            catch (Exception e) { PatchCounters()[121]++; AutNote("go-live request: " + e.Message); }
            if (L == null) { LiveStat(kSNotPosted); return h.MetaOrig(c); }
            LiveStat(kSPosted);
            int waited = 0;
            while (true)
            {
                Simulator.Sleep(0);
                waited++;
                // the game's YieldIfPossible after its own Sleep: the same two returns
                if ((bool)fInterrupted.GetValue(a)) { fInterrupted.SetValue(a, false); LiveAbandon(L); LiveStat(kSInterrupted); return 1; }
                if (a.Actor == null || a.Actor.HasBeenDestroyed) { LiveAbandon(L); LiveStat(kSDestroyed); return 2; }
                if (L.Ready) break;
                if (waited >= sLiveMaxWait) { LiveAbandon(L); LiveDrop(L, kSDropWait); AutShadowNative.LiveStat(kSWaitTicks, waited); return h.MetaOrig(c); }
            }
            AutShadowNative.LiveStat(kSWaitTicks, waited);
            uint us = 0;
            sLiveSw.Restart();
            int why;
            try { why = LiveCheck(L, a); if (why == 0) LiveSelect(L); }
            catch (Exception e) { why = kSDropOther; PatchCounters()[121]++; AutNote("go-live check: " + e.Message); }
            us += sLiveSw.GetElapsedTime();
            if (why != 0) { LiveDrop(L, why); LiveRelease(L); AutShadowNative.LiveStat(kSApplyUs, (int)us); return h.MetaOrig(c); }
            BestScores bs = (BestScores)fBestScores.GetValue(a);
            AutLiveCore.Heap before = L.Dry ? Snapshot(L, bs) : null;
            L.ResumeFrame = AutShadowNative.Config(19);
            sLiveCur = L;
            int r;
            try
            {
                r = h.MetaOrig(c);
                if (sLiveCur == L) sLiveCur = null;
                if (r == 0)
                {
                    sLiveSw.Restart();
                    try
                    {
                        if (L.Dry) DryCompare(L, bs, before);
                        else LiveBands(L, a, bs);
                    }
                    catch (Exception e) { PatchCounters()[121]++; AutNote("go-live bands: " + e.Message); }
                    us += sLiveSw.GetElapsedTime();
                }
            }
            finally
            {
                if (sLiveCur == L) sLiveCur = null;
                LiveRelease(L);
            }
            LiveStat(L.Dry ? kSDryRuns : kSConsumed);
            AutShadowNative.LiveStat(kSApplyUs, (int)us);
            return r;
        }

        static void LiveDrop(LiveSearch L, int why)
        {
            LiveStat(why);
            if (why < sLiveLogged.Length && sLiveLogged[why]++ < kLiveMaxLogPerReason)
                AutShadowNative.Log("{\"t\":\"live-drop\",\"why\":" + why + ",\"req\":" + L.Id + ",\"lastRun\":" + L.LastRun + "}");
        }

        // §3.1 result level; 0 = use it
        static int LiveCheck(LiveSearch L, AutonomyT a)
        {
            if (!L.Ok) return L.LastRun == 2 ? kSDropEpochRun : L.LastRun == 3 || L.LastRun == 4 ? kSDropNoWorker : L.LastRun == 5 ? kSDropExc : kSDropStopped;
            if (AutShadowNative.Config(20) != L.CutEpoch) return kSDropEpoch;
            if (!GameStates.IsLiveState || GameStates.IsGameShuttingDown) return kSDropState;
            Sim s = L.Actor;
            if (s.LotCurrent != L.LotCur) return kSDropLot;
            if (s.InteractionQueue != null && s.InteractionQueue.GetHeadInteraction() != null) return kSDropHead;
            int ags, jobs; Lot lc, lh; object hh, role, occ, job; bool sel, chip; IMetaObject career;
            ActorFields(s, out ags, out lc, out lh, out hh, out sel, out role, out occ, out jobs, out job, out career, out chip);
            if (ags != L.AgeSpecies || lh != L.LotHome || hh != L.Household || sel != L.Selectable || role != L.Role || occ != L.Occ
                || jobs != L.JobCount || job != L.Job || career != L.Career || chip != L.Chip) return kSDropActor;
            return 0;
        }

        // per object validity, the worker order of the valid objects' passing pairs, band 1
        static void LiveSelect(LiveSearch L)
        {
            for (int o = 0; o < L.Objs; o++)
            {
                int why = 0;
                if (L.ObjImpl[o] < 0) why = kSObjInvalid;
                for (int p = L.Start[o]; p < L.Start[o + 1] && why == 0; p++)
                {
                    if (L.Acb[p]) why = kSObjAcb;
                    else if (L.Def[p] >= 0 && (L.Code[p] == AutShadowCore.kAbort || L.Code[p] == AutShadowCore.kSkip)) why = kSObjInvalid;
                }
                if (why == 0 && (AutShadowNative.ObjStale(L.ObjKey[o], 0, L.CutFrame) != 0
                    || (L.LotKey[o] != 0UL && AutShadowNative.ObjStale(L.LotKey[o], 1, L.CutFrame) != 0))) why = kSObjStale;
                L.ObjWhy[o] = why;
            }
            for (int p = 0; p < L.Pairs; p++)
                L.Eligible[p] = L.Def[p] >= 0 && L.Code[p] == AutShadowCore.kPass && L.ObjWhy[L.PairObj[p]] == 0;
            int n = AutLiveCore.Order(L.W, L.Eligible, L.Pairs, L.Bands.Order);
            L.Bands.Start(sLiveTopN, sLiveBands, sLiveMargin, n);
            for (int r = 0; r < n && r < sLiveTopN; r++) L.Sel[L.Bands.Order[r]] = true;
            AutShadowNative.LiveStat(kSCandidates, n);
            // a candidate stays eligible for the bands only once B14 consumed its object (set in LiveB14)
            for (int p = 0; p < L.Pairs; p++) L.Eligible[p] = false;
        }

        // B14 hook, the consumer for one meta object (inside the game's B13)
        static void LiveB14(LiveSearch L, AutonomyT a, IMetaObject m, CommodityKind c, AutB14Hook h)
        {
            int o;
            if (m == null || !L.ObjIndex.TryGetValue(m, out o)) { LiveStat(kSObjNotInReq); h.B14Orig(m, c); return; }
            L.Visited[o] = true;
            // B13 yielded (Y2) since the resume: the rest of this search is older than one tick
            if (L.Late || AutShadowNative.Config(19) != L.ResumeFrame) { L.Late = true; LiveStat(kSObjLate); h.B14Orig(m, c); return; }
            int why = L.ObjWhy[o];
            if (why == 0 && !SameAds(L, o, m.MetaAds)) why = kSObjIdent;
            if (why != 0) { LiveStat(why); h.B14Orig(m, c); return; }
            L.Consumed[o] = true;
            LiveStat(kSObjConsumed);
            for (int p = L.Start[o]; p < L.Start[o + 1]; p++)
                if (L.Def[p] >= 0 && L.Code[p] == AutShadowCore.kPass) L.Eligible[p] = true;
            if (L.Dry) { h.B14Orig(m, c); return; }  // the game scores every ad; the C2 hook records what consume would have sent
            for (int p = L.Start[o]; p < L.Start[o + 1]; p++)
            {
                InteractionObjectPair iop = L.Pair[p];
                if (!PassesFilter(iop, c)) continue;
                if (L.Def[p] < 0) { LiveStat(kSPairsOffList); a.CalculateScoreAndAddToCandidates(iop); }
                else if (L.Code[p] == AutShadowCore.kFiltered) { LiveStat(kSPairsFilterDiff); a.CalculateScoreAndAddToCandidates(iop); }
                else if (L.Sel[p]) { L.Sent[p] = true; LiveStat(kSPairsBand1); a.CalculateScoreAndAddToCandidates(iop); }
            }
        }

        // B14's own commodity filter (the same as the worker driver's ScorePair)
        static bool PassesFilter(InteractionObjectPair iop, CommodityKind c)
        {
            Tradeoff t = iop.Tradeoff;
            if (t == null) return false;
            if (c == CommodityKind.None || t.SatisfiesCommodity(c)) return true;
            IMetaInteractionDefinition md = iop.InteractionDefinition as IMetaInteractionDefinition;
            return md != null && md.MetaAdSatisfiesCommodity(iop.Target as Lot, c);
        }

        // the object's MetaAds now are the request's pairs, same references and count (AddObj skipped non-pairs; the game's B14
        // would throw on one, so a mismatch hands the object to the game's B14)
        static bool SameAds(LiveSearch L, int o, ArrayList ads)
        {
            if (ads == null) return L.Start[o + 1] == L.Start[o];
            int n = L.Start[o + 1] - L.Start[o];
            if (ads.Count != n) return false;
            for (int j = 0; j < n; j++) if (!ReferenceEquals(ads[j], L.Pair[L.Start[o] + j])) return false;
            return true;
        }

        // C2 hook (AutScoreHook): the game's own score of a pair of the live search
        internal static void LiveScored(LiveSearch L, InteractionObjectPair iop, float s)
        {
            int p = -1;
            if (iop == null || !L.PairIndex.TryGetValue(iop, out p)) p = -1;
            if (p >= 0) { L.Live[p] = s; L.LiveHas[p] = true; }
            if (L.Dry)
            {
                int id = p;
                if (p < 0)
                {
                    if (iop == null || !L.OtherId.TryGetValue(iop, out id)) { id = -1 - L.OtherId.Count; if (iop != null) L.OtherId[iop] = id; }
                }
                L.AddEvent(id, s);
            }
            else if (p >= 0 && L.Sent[p] && L.Def[p] >= 0 && L.Code[p] == AutShadowCore.kPass) L.Bands.Saw(L.W[p], s);
        }

        // after the game's B13: the §16.1 bands through C1, on the game's own BestScores
        static void LiveBands(LiveSearch L, AutonomyT a, BestScores bs)
        {
            sLiveCur = L;  // the bands' C2 calls feed drift
            try
            {
                while (true)
                {
                    int cnt = bs.Count;
                    float s10 = 0f;
                    for (int i = 0; i < cnt; i++) { float v = bs.Element(i).InteractionScore; if (i == 0 || v < s10) s10 = v; }
                    int n = L.Bands.Step(cnt, AutLiveCore.kCap, s10, L.Eligible, L.Sent, L.W, L.Buf);
                    if (n == 0) break;
                    LiveStat(L.Bands.Full ? kSBandFull : L.Bands.Fired == 1 ? kSBand2 : kSBand3);
                    LiveStat(L.Bands.LastWhy == 1 ? kSWhyCount : kSWhyMargin);
                    for (int i = 0; i < n; i++)
                    {
                        int p = L.Buf[i];
                        L.Sent[p] = true;
                        if (L.Obj[L.PairObj[p]].HasBeenDestroyed) continue;
                        LiveStat(kSPairsBand);
                        a.CalculateScoreAndAddToCandidates(L.Pair[p]);
                    }
                    if (L.Bands.Full) break;
                }
            }
            finally { if (sLiveCur == L) sLiveCur = null; }
            LiveStat(kSDrift0 + AutLiveCore.DriftBin(L.Bands.DriftMax));
        }

        // ---- G7 dry compare ----

        static int OtherIdOf(LiveSearch L, InteractionObjectPair iop)
        {
            int id;
            if (iop != null && L.PairIndex.TryGetValue(iop, out id)) return id;
            if (iop == null || !L.OtherId.TryGetValue(iop, out id)) { id = -1 - L.OtherId.Count; if (iop != null) L.OtherId[iop] = id; }
            return id;
        }
        static AutLiveCore.Heap Snapshot(LiveSearch L, BestScores bs)
        {
            AutLiveCore.Heap h = new AutLiveCore.Heap(AutLiveCore.kCap);
            for (int i = 0; i < bs.Count; i++) { InteractionCandidate e = bs.Element(i); h.Add(OtherIdOf(L, e.InteractionObjectPair), e.InteractionScore); }
            return h;
        }
        static AutLiveCore.Heap Copy(AutLiveCore.Heap h)
        {
            AutLiveCore.Heap x = new AutLiveCore.Heap(AutLiveCore.kCap);
            for (int i = 0; i < h.N; i++) x.Add(h.Id[i], h.S[i]);
            return x;
        }

        static void DryCompare(LiveSearch L, BestScores bs, AutLiveCore.Heap before)
        {
            AutLiveCore.Heap game = Snapshot(L, bs), van = Copy(before), pred = Copy(before);
            int offList = 0;
            for (int i = 0; i < L.EvN; i++)
            {
                int p = L.EvId[i];
                bool send = true;
                if (p >= 0)
                {
                    int o = L.PairObj[p];
                    if (L.Def[p] < 0) offList++;
                    else if (L.Consumed[o] && L.Code[p] != AutShadowCore.kFiltered) send = L.Sel[p];
                    if (send && L.Sel[p] && L.Consumed[o]) { L.Sent[p] = true; L.Bands.Saw(L.W[p], L.EvScore[i]); }
                }
                L.EvSend[i] = send;
            }
            AutLiveCore.Replay(van, L.EvN, L.EvId, L.EvScore, null);
            AutLiveCore.Replay(pred, L.EvN, L.EvId, L.EvScore, L.EvSend);
            int fired = AutLiveCore.RunBands(pred, L.Bands, L.Eligible, L.Sent, L.W, L.Live, L.LiveHas, L.Buf);
            for (int k = 1; k <= fired; k++) LiveStat(k == fired && L.Bands.Full ? kSBandFull : k == 1 ? kSBand2 : kSBand3);  // as LiveBands
            if (fired > 0) LiveStat(L.Bands.LastWhy == 1 ? kSWhyCount : kSWhyMargin);  // the last band's rule
            LiveStat(kSDrift0 + AutLiveCore.DriftBin(L.Bands.DriftMax));
            AutShadowNative.LiveStat(kSDryOffList, offList);
            if (!van.SameSet(game)) LiveStat(kSDrySimMis);
            bool top10 = pred.SameSet(game), top1 = pred.TopId() == game.TopId();
            if (top10) LiveStat(kSDryTop10);
            if (top1) LiveStat(kSDryTop1);
            if (top10 && top1) return;
            StringBuilder s = sDryLogged < kDryMaxLog ? new StringBuilder(512) : null;
            if (s != null)
            {
                sDryLogged++;
                s.Append("{\"t\":\"dry-diff\",\"req\":").Append(L.Id).Append(",\"actor\":").Append(L.Actor.ObjectId.Value)
                 .Append(",\"top1\":").Append(top1 ? 1 : 0).Append(",\"drift\":").Append(L.Bands.DriftMax.ToString("R"))
                 .Append(",\"bands\":").Append(fired).Append(",\"miss\":[");
            }
            bool first = true;
            for (int i = 0; i < game.N; i++)
            {
                int id = game.Id[i];
                if (pred.Has(id)) continue;
                int why = id < 0 ? kSDryMissOther : L.Code[id] == AutShadowCore.kTestFailed ? kSDryMissStale
                    : L.Code[id] == AutShadowCore.kPass && !L.Sent[id] ? kSDryMissBand : kSDryMissOther;
                LiveStat(why);
                if (s == null) continue;
                if (!first) s.Append(',');
                first = false;
                s.Append("{\"id\":").Append(id).Append(",\"live\":").Append(game.S[i].ToString("R"));
                if (id >= 0)
                    s.Append(",\"def\":\"").Append(L.Pair[id].InteractionDefinition != null ? L.Pair[id].InteractionDefinition.GetType().Name : "?")
                     .Append("\",\"code\":").Append(L.Code[id]).Append(",\"work\":").Append(L.W[id].ToString("R")).Append(",\"sel\":").Append(L.Sel[id] ? 1 : 0);
                s.Append('}');
            }
            if (s != null) { s.Append("]}"); AutShadowNative.Log(s.ToString()); }
        }

        // AutFinish in Mode 3 on a cut that ran the reference (VerifyEvery): top picks of worker vs reference over the pairs both
        // answered; an A2/A3 from CompareR turns go-live off for the session
        static void LiveVerify(int a23Before)
        {
            int[] c = PatchCounters();
            if (c[108] + c[109] > a23Before)
            {
                if (!sLiveOff) AppliedLog().Add("AutShadow: go-live off for the session: VerifyEvery found an A2/A3 (request " + sReq.Id + ")");
                sLiveOff = true;
                LiveStat(kSVerifyOff);
            }
            bool[] ans = new bool[sReq.Pairs];
            for (int i = 0; i < sReq.Pairs; i++)
            {
                if (sReq.Def[i] < 0 || sReq.ObjImpl[sReq.PairObj[i]] < 0) continue;
                int a = sWork.Code[i], b = sRef.Code[i];
                ans[i] = (a == AutShadowCore.kPass || a == AutShadowCore.kTestFailed) && (b == AutShadowCore.kPass || b == AutShadowCore.kTestFailed);
            }
            int[] tw = Top(sWork, 10, ans), tr = Top(sRef, 10, ans);
            int k = Math.Min(Count(tw), Count(tr));
            LiveStat(kSVerifyRuns);
            if (tw[0] == tr[0]) LiveStat(kSVerifyTop1);
            if (Count(tw) == Count(tr) && SameSet(tw, tr, k)) LiveStat(kSVerifyTop10);
        }

        // B14 (private, void, one caller: B13). Passes through unless a live search of this autonomy is in its game B13.
        public class AutB14Hook
        {
            public void B14Hook(IMetaObject m, CommodityKind c)
            {
                LiveSearch L = sLiveCur;
                if (L == null || (object)this != (object)L.Aut) { B14Orig(m, c); return; }
                LiveB14(L, (AutonomyT)(object)this, m, c, this);
            }
            public void B14Orig(IMetaObject m, CommodityKind c) { } // receives Autonomy.ScoreInteractionsForMetaObject's original header
        }
    }
}
