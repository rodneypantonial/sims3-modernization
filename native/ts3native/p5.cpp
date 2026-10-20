// [RouteSnap] Mode=2: routing snapshot P5 (docs/94 §19, §22; counters v61). Included by ts3native.cpp after p4clone.cpp.
// For an async ReplanFromPosition job (A10 kind 3, [RouteSnap] Kinds bit 8) the game takes the answer of the clone-run search
// instead of running its own plan. Everything else is P4's (p4.cpp, p4clone.cpp): the cut at START, the capture of the
// arguments at the job's Route_PlanLocked, the acquire of the working copy, the armed run with T1-T4, the private path list,
// the release. P5 adds:
//   - P5Plan (worker, the job's Route_PlanLocked, still under the job's +0x918 as every A10 job): the clone run into a private
//     list; a clean run's list nodes are moved into the route's out list, the very nodes P4CaptureAnswerRaw read for g_p4Copy,
//     so the taken answer is the one the §13 shadow compare compares;
//   - door asks of a Mode=2 clone run go to the game's own chain, proxied to the sim thread as in any A10 job (P4DoorEnter,
//     §19.1 (b)); the ask runs disarmed, so the P4 managed / event tripwires stay armed for everything else (M2-2);
//   - fallbacks: any refusal, lazy stop, fault, tripwire, stale cut (rtClosing / rtBusy at entry, the P3 epoch moved during the
//     run, the provider's own epoch checks) or a non-routing write the plan read (nr != 0) runs the game's own plan for that
//     request (PlanLockedBody), so a route is never left without an answer;
//   - VerifyEvery: one job in N runs the clone and then the game's plan, keeps the game's answer and writes the §13 record
//     (v3, "m2":1), so shadow-compare-report.ts applies G4 to what Mode=2 would have taken;
//   - mod guard (docs/104 as §19.2 narrows it for (b)): M2-1 the code bytes of every site the clone run passes, snapshotted
//     after the last install, compared at each armed START (a change = a foreign hook: Mode=2 off for the session); M2-2 three
//     tripwires turn it off; M2-3 MonoPatcher's L2 bytes logged once per finding (no fallback).
// [RouteSnap] ReleaseLock=1 (P5b, p5b.cpp, docs/94 §22.9) lends the job's +0x918 to the sim's steering while the clone run
// runs; without it the live +0x918 stays held through the clone run (A10's job hold). [RouteSnap] FreeLock=1 (P5c, p5c.cpp,
// docs/94 §22.11) lends it to every plain sim acquire, parks destroys, and refuses an answer with a routing write after the cut. shortcut: L1 (EA image GUIDs) is not built: it is log only under (b) and needs a managed class
// lookup; add it with docs/104's guard for option (a).

static void P5bBegin(void* lock, bool verify);  // p5b.cpp: lend +0x918 out for the clone run
static void P5bEnd();                           // p5b.cpp: +0x918 back before anything live
static void ApplyP5b(const wchar_t* ini);
static void P5cOnStart(const AsyncJob* j);      // p5c.cpp: the cut's write count for the stale check
static void P5cLend(void* lock, bool verify);   // p5c.cpp: P5bBegin, with every plain sim acquire admitted (FreeLock=1)
static void P5cTake();                          // p5c.cpp: P5bEnd
static bool P5cStale(DWORD seq);                // p5c.cpp: a recorded routing write since the cut: never take the answer
static bool P5cStaleEarly(DWORD seq, bool verify);  // p5c.cpp: the same, known before the acquire (0.35.26)

static volatile LONG g_p5On;   // Mode=2 installed and not turned off
static LONG g_p5Verify = 10;   // VerifyEvery (0 = never)
static DWORD g_p5N;            // sim thread: armed STARTs, for the verify sample
static void* volatile g_p5GuardLock;  // the routing lock of the world the L2 check last ran in
enum { P5_OFF_SITE = 1, P5_OFF_TRIPS, P5_OFF_FAULT };
static const int kP5Trips = 3;

static void P5Off(int why, DWORD at) {
    if (!InterlockedExchange(&g_p5On, 0)) return;
    Ts3nCounters& k = ts3native_counters;
    k.m2Off = why;
    InterlockedAnd(&k.m2Ready, ~1);
    if (why == P5_OFF_SITE) Log("RouteSnap: Mode=2 off: site 0x%08lX owned by unknown hook; plans run as A10", at);
    else Log("RouteSnap: Mode=2 off: %s; plans run as A10", why == P5_OFF_TRIPS ? "3 clone-run tripwires (M2-2)" : "P4 fault");
}

// --- M2-1: the code bytes of our sites in the clone run's closure, as they were after every install ---
struct P5Site { DWORD at; u8 b[5]; };
static const int kP5Sites = 160;
static P5Site g_p5Sites[kP5Sites];
static int g_p5NSites;
static void P5SnapAt(DWORD at) {
    if (!at || g_p5NSites == kP5Sites || !Readable((const void*)(uintptr_t)at, 5)) return;
    for (int i = 0; i < g_p5NSites; i++) if (g_p5Sites[i].at == at) return;
    P5Site& s = g_p5Sites[g_p5NSites++];
    s.at = at;
    memcpy(s.b, (const void*)(uintptr_t)at, 5);
}
// DllMain, after the last section installed (S3SS and S3PP install later, from their own threads: docs/68 §5)
static void P5Snapshot() {
    if (!g_p5On) return;
    Ts3nCounters& k = ts3native_counters;
    for (int i = 0; i < P4_SITES; i++) P5SnapAt(g_p4At[i]);
    P5SnapAt(P4_DOOR_AT);
    P5SnapAt(ROUTE_PLAN_LOCKED);
    P5SnapAt(0x00765550);  // IC_Route_ReplanFromPosition: the A10 collect detour
    if (g_pcQIns) P5SnapAt(0x00666C50);
    if (g_pcQRem) P5SnapAt(0x00666E30);
    for (int i = 0; i < RG_SITES; i++) if (g_rgTramp[i]) P5SnapAt(kRgSites[i].addr);
    for (int i = 0; i < RT_N; i++) if (g_rtTramp[i]) P5SnapAt(kRtSites[i].addr);
    for (int i = 0; i < SN_N; i++) if (g_snJmp[i]) P5SnapAt(kSn[i].at);
    for (int i = 0; i < 3; i++) if (g_p4DlJmp[i]) P5SnapAt(kP4Dl[i].at);
    k.m2Sites = g_p5NSites;
    InterlockedOr(&k.m2Ready, 2);
    Log("route snapshot P5: M2-1 snapshot of %d code sites; clone provider %s", g_p5NSites,
        g_p4Clone ? "registered" : "none (every Mode=2 job plans as A10, fallback \"clone\")");
}
static DWORD P5SiteChanged() {
    for (int i = 0; i < g_p5NSites; i++) {
        const P5Site& s = g_p5Sites[i];
        if (!Readable((const void*)(uintptr_t)s.at, 5) || memcmp((const void*)(uintptr_t)s.at, s.b, 5)) return s.at;
    }
    return 0;
}

// --- M2-3 (log only): MonoPatcher.asi's native sites (docs/107 §2.2, docs/104 L2) ---
struct P5L2 { DWORD at; u8 len; u8 b[8]; const char* what; };
static P5L2 g_p5L2[5] = {  // b = the vanilla bytes [verified, docs/104 §5.4, docs/107 §2.2; steerro-test checks them on the image]
    {0x00D7E560, 1, {0x51}, "MonoScriptHost_Init: detoured (MinHook)"},
    {0x00E5CA90, 8, {0x55, 0x8D, 0x6C, 0x24, 0x98, 0x81, 0xEC, 0x08}, "GenerateCode: ReplaceIL detour"},
    {0x00E59218, 6, {0x0F, 0x86, 0xA9, 0xB1, 0xFF, 0xFF}, "ret-values jump rewritten (MonoPatcher.asi at load)"},
    {0x00E5637D, 2, {0x75, 0x26}, "JIT check 1: ForceJIT on"},
    {0x00E648DC, 2, {0x74, 0x4C}, "JIT check 2: ForceJIT on"},
};
static void P5ModGuard() {
    Ts3nCounters& k = ts3native_counters;
    for (int i = 0; i < 5; i++) {
        const P5L2& s = g_p5L2[i];
        const u8* p = (const u8*)(uintptr_t)s.at;
        if (!Readable(p, s.len)) continue;
        const bool found = memcmp(p, s.b, s.len) != 0;
        if (!found || (k.m2Guard & (1 << i))) continue;
        InterlockedOr(&k.m2Guard, 1 << i);
        Log("ModGuard: L2 0x%08lX %s (MonoPatcher; log only, Mode=2 stays on: docs/94 §19.2 M2-3)", s.at, s.what);
    }
    InterlockedOr(&k.m2Ready, 4);
}

// sim thread, async START of a Mode=2 build (P4OnStart): arm ReplanFromPosition jobs, every VerifyEvery-th one to verify
static void P5OnStart(AsyncJob* j) {
    Ts3nCounters& k = ts3native_counters;
    if (!g_p5On || !k.m2Take || j->kind != 3 || !(g_p4Kinds & 8)) return;
    if (const DWORD at = P5SiteChanged()) {
        k.m2SiteAt = (LONG)at;
        P5Off(P5_OFF_SITE, at);
        return;
    }
    if (g_rgLock != g_p5GuardLock) {  // once per world: ForceJIT can change between worlds
        g_p5GuardLock = g_rgLock;
        P5ModGuard();
    }
    j->p4 = g_p5Verify && ++g_p5N % (DWORD)g_p5Verify == 0 ? 3 : 2;
    j->p4Ver = P4CloneCut();
    P5cOnStart(j);
    InterlockedIncrement(&k.m2Armed);
    InterlockedIncrement(&k.snShadowArmed);
}

// worker, ServeAsync before it takes the job's +0x918 (0.35.19, docs/94 §22.22): the replay consumer applies the records up
// to the job's cut only under +0x918 (RpDrainBody), so once the job holds it PcAcquire can only wait out kPcWaitMs with the
// lock the sim waits for held, then refuse (mat/timeout) and plan with the game. Wait for the consumer here instead, holding
// nothing, bounded the same way; idle too (caught up, it runs RpGc holding g_rpUse, which the acquire needs)
static void P5PreLock(const AsyncJob* j) {
    if (j->p4 < 2 || !j->p4Ver || !g_rpThreaded || g_p4Clone != &kPcProvider) return;
    const DWORD ver = j->p4Ver;
    const ULONGLONG until = GetTickCount64() + kPcWaitMs;
    while ((g_rpTail != ver || g_rpPhase) && (DWORD)(ver - g_rpTail) <= (DWORD)(g_rpHead - g_rpTail) && GetTickCount64() <= until) {
        SetEvent(g_rpEvent);
        Sleep(0);
    }
}

// the clone run's nodes into the route's out list (empty: P5Plan checks it at entry). Same EASTL list, same allocator words
// (P4Head copied the live head's), so the route owns and frees them as its own [layout verified, docs/94 §18.7 I-94-102]
static void P5Move(int* head, int* outList) {
    if (head[0] == (int)(uintptr_t)head) return;
    int* first = (int*)(uintptr_t)head[0];
    int* last = (int*)(uintptr_t)head[1];
    first[1] = last[0] = (int)(uintptr_t)outList;
    outList[0] = (int)(uintptr_t)first;
    outList[1] = (int)(uintptr_t)last;
    head[0] = head[1] = (int)(uintptr_t)head;
}

// 0.35.26 (docs/94 §22.30), log only (no counter change; worker only): acquires made inside the lent window, stale jobs that
// skipped the acquire, cut holds dropped by a refusal before the acquire; logged every 5 min
static LONG g_p5EarlyLend, g_p5EarlyStale, g_p5Drops;
static void P5Log5() {
    static ULONGLONG s_at;
    static LONG s_l0, s_s0, s_d0;
    const ULONGLONG now = GetTickCount64();
    if (!s_at) s_at = now;
    if (now - s_at < 300000) return;
    Log("P5: last %lu s: acquires inside the lent window %ld (of %ld acquired since start), stale before the acquire %ld "
        "(skipped acquire and run), cut holds dropped before the acquire %ld", (DWORD)((now - s_at) / 1000), g_p5EarlyLend - s_l0,
        ts3native_counters.snPcAcquired, g_p5EarlyStale - s_s0, g_p5Drops - s_d0);
    s_at = now, s_l0 = g_p5EarlyLend, s_s0 = g_p5EarlyStale, s_d0 = g_p5Drops;
}

static volatile LONG g_p5VerLog;
// worker, the armed job's Route_PlanLocked (P4LivePlan after the argument capture; the job holds +0x918)
static u8 P5Plan(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts, int* out1,
                 int* out2, DWORD guidLo, DWORD guidHi) {
    Ts3nCounters& k = ts3native_counters;
    P4Job& c = g_p4Job;
    const bool verify = c.mode == 3;
    InterlockedIncrement(&k.m2Plans);
    int why = c.abort;  // the capture's
    if (why < 0 && (!g_p5On || !(k.snP4Ready & 1))) why = P4_AB_OTHER;
    if (why < 0 && outList[0] != (int)(uintptr_t)outList) {  // the clone's nodes go into an empty list only
        why = P4_AB_CAPTURE;
        InterlockedIncrement(&k.m2FbList);
    }
    if (why < 0 && (k.rtClosing || k.rtBusy)) why = P4_AB_EPOCH;
    if (why < 0 && P4Cold()) why = P4_AB_COLD;
    // 0.35.26 (docs/94 §22.30): an answer P5c refuses anyway (a routing write since the START cut) skips the acquire and the run
    if (why < 0 && P5cStaleEarly(c.seq, verify)) why = P4_AB_EPOCH, g_p5EarlyStale++;
    P4Clone cl = {};
    bool have = false, ran = false, lent = false;
    if (why < 0) {
        // 0.35.26: the copy (5-6 ms on BP) inside the lent window when it makes no game call; else under the hold as before
        if (!verify && g_p4Clone == &kPcProvider && PcAcquireOwnOnly(c.ver)) {
            P5cLend((u8*)world + 0x918, verify);
            lent = true;
            g_p5EarlyLend++;
        }
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        const int r = P4CloneAcquire(c.ver, &cl);
        InterlockedExchangeAdd(&k.snMatUs, UsSince(t0));
        if (r >= 0) why = r; else have = true;
    } else if (c.ver) {
        P4CloneDrop(c.ver);  // refused before the acquire: the consumer's hold moves on now, not at kPcPendMs (as P4's shadow)
        g_p5Drops++;
    }
    int head[4];
    P4Head(head, c);
    LONG rel = 0;
    const LONG read0 = t_p4Read;  // the IC's own reader bits so far (P4RunClone resets the thread's)
    if (have && !lent) P5cLend((u8*)world + 0x918, verify);
    if (have) rel = P4RunClone(c, cl, head, true);
    if (have || lent) P5cTake();  // also after a refused acquire in the window: +0x918 back before the game's plan
    if (have) {
        ran = true;
        t_p4Read = read0;
        if (c.abort >= 0) why = c.abort;
        InterlockedExchangeAdd(&k.m2JobUs, c.jobUs);
        const LONG n = c.nAsk;
        InterlockedIncrement(&k.m2DoorHist[n == 0 ? 0 : n <= 2 ? 1 : n <= 5 ? 2 : n <= 10 ? 3 : 4]);
        const LONG recs = g_p4Clone == &kPcProvider ? (LONG)(g_rpHead - c.ver) : 0;  // replay records written since the cut
        if (recs > 0) { InterlockedIncrement(&k.m2Stale); InterlockedExchangeAdd(&k.m2StaleRecs, recs); }
    }
    LONG nr1[kP4Fam];
    P4NrSnap(nr1);
    const LONG nr = P4NrBits(c.nr0, nr1, read0 | c.cloneRead);
    if (why < 0 && (k.rtClosing || k.rtBusy || c.epoch != (DWORD)g_spEpoch)) why = P4_AB_EPOCH;  // the world moved during the run
    if (why < 0 && P5cStale(c.seq)) why = P4_AB_EPOCH;  // P5c: the lock was free; a routing write after the cut refuses the answer
    if (why < 0 && !g_p4Copy.valid) why = P4_AB_CAPTURE;
    if ((why == P4_AB_MANAGED || why == P4_AB_EVENT) && InterlockedIncrement(&k.m2Trips) >= kP5Trips) P5Off(P5_OFF_TRIPS, 0);
    if (why == P4_AB_FAULT) P5Off(P5_OFF_FAULT, 0);
    u8 r;
    if (why < 0 && !verify && !nr) {  // taken: the answer is the clone run's, no game plan
        P5Move(head, outList);
        if (out1) *out1 = g_p4Copy.out1;
        if (out2) *out2 = g_p4Copy.out2;
        r = g_p4Copy.ok;
        InterlockedIncrement(&k.m2Taken);
    } else {
        // a lazy stop already released the list; a faulted run's list is left alone (half-built, the copy is discarded)
        if (ran && why != P4_AB_FAULT && !P4FreeList(head)) Log("P5: private path list of plan %lu not released", c.seq);
        if (why >= 0) InterlockedIncrement(&k.m2Fb[why]);
        else if (!verify) InterlockedIncrement(&k.m2FbNr);
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        r = PlanLockedBody(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
        c.gameUs = UsSince(t0);
        if (verify) {
            int cls = 4;
            if (P4CaptureAnswerRaw(outList, r, out1 ? *out1 : -1, out2 ? *out2 : -1, &g_p4Live) && why < 0 && g_p4Copy.valid)
                cls = P4Classify(g_p4Live, g_p4Copy, nr, P4TieN(c.starts, c.goals));
            else if (why < 0) why = P4_AB_CAPTURE;
            InterlockedIncrement(&k.m2Verify);
            InterlockedIncrement(&k.m2VerM[cls]);
            if (cls == 2 || cls == 3) {
                InterlockedIncrement(&k.m2VerBad);
                if (InterlockedIncrement(&g_p5VerLog) <= 20)
                    Log("P5 verify: plan %lu M%d (the clone answer would have been taken; the game's was kept)", c.seq, cls);
            }
            if (!g_p4Live.valid) g_p4Live.nPaths = 0;  // the record prints no paths for an unreadable answer
            P4Record(c, why >= 0 ? kP4Ab[why] : NULL, nr);
        }
    }
    if (have) P4CloneRelease(&cl, rel, c.dirty, c.nDirty);
    if (rel & P4REL_WRAP) g_p4Work.wrap = 0;
    c.armed = 0;  // nothing left for P4Shadow after J_DONE
    P4JobFree(c);
    P5Log5();
    return r;
}

// [RouteSnap] Mode=2 (from ApplyP4, after P4 installed: the thunks, the door stub and the capture are P4's)
static void ApplyP5(const wchar_t* ini) {
    Ts3nCounters& k = ts3native_counters;
    g_p5Verify = Clamp(GetPrivateProfileIntW(L"RouteSnap", L"VerifyEvery", 10, ini), 0, 1000);
    if (!(g_p4Kinds & 8)) Log("route snapshot P5: Kinds has no ReplanFromPosition (8): no answer is taken");
    if (g_p4Kinds & 7) Log("route snapshot P5: takes ReplanFromPosition only; Kinds bits 1, 2, 4 are not taken (P6 widens)");
    if (!(k.arReady & 4)) Log("route snapshot P5: [RouteAsync] Kinds has no ReplanFromPosition (4): no Mode=2 job will start");
    k.m2Take = 1;
    k.m2Ready = 1;
    g_p5On = 1;
    Log("OK   route snapshot P5 (Mode=2): ReplanFromPosition jobs take the clone answer; VerifyEvery=%ld; door asks proxied to the "
        "sim thread; fallback = the game's own plan", g_p5Verify);
    ApplyP5b(ini);
}
