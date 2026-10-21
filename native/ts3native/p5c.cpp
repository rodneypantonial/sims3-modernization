// [RouteSnap] FreeLock=1: P5c (docs/94 §22.11; counters v65). Included by ts3native.cpp after p5b.cpp. Needs ReleaseLock=1
// (P5b's window: +0x918 lent out at the clone run's start, taken back before the answer, before each door ask and at any
// tripwire) and so Mode=2. P5b lends the lock only to the sim's steering and, with ReleaseGates, its read-only gate leaves.
// P5c widens the window of a taken job to every plain +0x918 acquire of the sim, and lets destroys run in it:
//   - admission (P5bAllowed): while g_p5cJob, a sim acquire outside a job callback that is not a gate wait (RgEnter's
//     t_waitKey unset, or the C-A rebuild's key 0x80E5B0: writers such as UpdateSimFootprint, steering, any nested caller)
//     takes the lent lock. A gate-0 miss given back keeps its key, so it never retries as a plain acquire.
//     Gate waits keep P5b's rules: the read families with ReleaseGates; every other gate waits for the job as in A10 (site 0
//     misses, sites 1-11 = lot planner, Q, goal step and the sync plans' own gate, whose shared state the suspended IC frame
//     consumed before Route_PlanLocked (docs/87); the coarse rebuilds 13-14; the gate-12 writer families);
//   - stale answers: the clone answers the START cut. Any recorded routing write since that cut (snap.cpp g_spWrites) refuses
//     the answer at the retake, and the game's own plan answers under the retaken lock (P5's fallback, reason epoch);
//   - lifetime (docs/88 design G): a destroy inside the window parks one reference to its object (FindByIdAddRef) and runs at
//     once, without D2's clone-run wait and without the job wait. Parked references are released on the owner (sim) thread at a
//     point with no job (destroy, save, closing and teardown entries; the START / READY polls), job starts held off through
//     g_rgOffSimOpen. Kept waits: a D-X record for the id, a full graveyard, off the sim thread or inside a job callback;
//   - no deadlock against the retake: the sim never waits for the job or its clone run while it owns the lent lock. A sync
//     plan under the lent lock plans on the sim without the job wait (RunPlan). Any other such wait hands the lock back,
//     waits for the whole job, and takes the lock again (P5cHandBegin / P5cHandEnd; a tripwire: P5c off for the session).
// A_RUNNING keeps "a job is live"; "the worker holds +0x918" gets the P5c window as a second exception next to P5b's
// (docs/94 §22.11.3: every site that reads it, and what it does now).

static volatile LONG g_p5cOn;   // FreeLock=1 installed
// g_p5cJob (p5b.cpp): 1 from before this job's window opens until after its last retake (worker writes it)
static __declspec(thread) bool t_p5cRan;  // worker: this job's window opened with P5c admission
static __declspec(thread) LONG t_p5cW1;   // worker: g_spWrites when it opened
static volatile LONG g_p5cLog;

// the START cut's recorded-write count, by job sequence number (sim at START, worker at the retake; 64 > kJobs)
struct P5cCut { volatile LONG seq, w; };
static P5cCut g_p5cCut[64];
static void P5cOnStart(const AsyncJob* j) {  // P5OnStart, right after the cut (this thread wrote nothing since)
    P5cCut& e = g_p5cCut[j->seq & 63];
    e.w = g_spWrites;
    InterlockedExchange(&e.seq, (LONG)j->seq);
}

// P5Plan around P4RunClone, in place of P5bBegin / P5bEnd
static void P5cLend(void* lock, bool verify) {
    Ts3nCounters& k = ts3native_counters;
    const bool want = g_p5cOn && k.p5cFree && !k.p5cOff && !verify;
    t_p5cW1 = g_spWrites;
    if (want) InterlockedExchange(&g_p5cJob, 1);  // published before P5bOpen publishes the window
    P5bBegin(lock, verify);
    t_p5cRan = want && t_p5b == P5B_OPEN;
    if (want && !t_p5cRan) InterlockedExchange(&g_p5cJob, 0);
    if (t_p5cRan) InterlockedIncrement(&k.p5cJobs);
}
static void P5cTake() {
    P5bEnd();  // the worker owns +0x918 again: no sim holder is left
    InterlockedExchange(&g_p5cJob, 0);
}
// P5Plan after the run: true = a recorded routing write landed after the START cut, so the clone answer is never taken
static bool P5cStale(DWORD seq) {
    if (!t_p5cRan) return false;
    t_p5cRan = false;
    const P5cCut& e = g_p5cCut[seq & 63];
    const LONG now = g_spWrites;
    const bool known = e.seq == (LONG)seq;
    if (known && now == e.w) return false;
    InterlockedIncrement(&ts3native_counters.p5cStale[known && now == t_p5cW1 ? 0 : 1]);  // 0: all before the window
    return true;
}

// 0.35.26 (docs/94 §22.30), P5Plan before the acquire: a recorded routing write since the START cut already means P5cStale
// refuses the answer at the retake whatever the run finds, so the job skips the acquire and the clone run and the game's plan
// answers at once (counted as stale before the window). A window P5bOpen would refuse could have taken it: conservative
static bool P5cStaleEarly(DWORD seq, bool verify) {
    Ts3nCounters& k = ts3native_counters;
    if (!g_p5cOn || !k.p5cFree || k.p5cOff || verify) return false;
    const P5cCut& e = g_p5cCut[seq & 63];
    if (e.seq != (LONG)seq || g_spWrites == e.w) return false;
    InterlockedIncrement(&k.p5cStale[0]);
    return true;
}

// --- lifetime: the destroy graveyard (docs/88 §5.1) ---
struct P5cGrave { void* obj; LARGE_INTEGER t; };
static const int kP5cGrave = 512;
static P5cGrave g_p5cGy[kP5cGrave];
static volatile LONG g_p5cGyN;
static SRWLOCK g_p5cGyLock = SRWLOCK_INIT;

static bool P5cDiscHas(DWORD idLo, DWORD idHi) {  // D-X: a queued discourage record of this id (owner thread only)
    for (int i = 0; i < g_discCount; i++) {
        const DiscRec& r = g_disc[(g_discHead + i) % kDiscCap];
        if (r.idLo == idLo && r.idHi == idHi && r.lock == g_rgLock) return true;
    }
    return false;
}

// rtsites.cpp RT_DESTROY (thiscall svc; idLo, idHi), before D2 and the job wait: true = one reference parked, the destroy runs
// now; its last Release cannot run before a flush, so nothing the suspended frame or the clone run reads is freed.
// Design G standalone ([Discourage] DestroyPark=1, dpMode; docs/88 §10): the same park for any running async job, with or
// without P5c / RouteSnap. The job's FindById pointers (docs/88 §3.2) stay allocated until a no-job flush, as in the window.
static bool P5cPark(DWORD idLo, DWORD idHi) {
    Ts3nCounters& k = ts3native_counters;
    const bool alone = !(g_p5cJob && g_p5bWin);
    if (alone && !(k.dpReady && k.dpMode == 1 && g_async.state == A_RUNNING && !k.rtClosing)) return false;
    int kept = -1;
    if (GetCurrentThreadId() != g_async.owner || g_inInvoke) kept = 2;
    else if (P5cDiscHas(idLo, idHi)) kept = 0;  // D-X applies its records under the job wait
    else if (g_p5cGyN >= kP5cGrave) kept = 1;   // only this thread parks; a flush only shrinks the list
    void* obj = NULL;
    if (kept < 0) {
        void* svc = g_objSvc();
        if (!svc || !g_findRef(svc, NULL, idLo, idHi, &obj) || !obj) kept = 3;
    }
    if (kept >= 0) {
        InterlockedIncrement(&k.p5cKeptWait[kept]);
        if (alone) InterlockedIncrement(&k.dpKeptWait);
        return false;
    }
    AcquireSRWLockExclusive(&g_p5cGyLock);
    P5cGrave& g = g_p5cGy[g_p5cGyN];
    g.obj = obj;
    QueryPerformanceCounter(&g.t);
    InterlockedIncrement(&g_p5cGyN);
    ReleaseSRWLockExclusive(&g_p5cGyLock);
    Max(&k.p5cPendMax, g_p5cGyN);
    InterlockedIncrement(&k.p5cParked);  // every park (graveyard total); dpParked = those outside a P5c window
    if (alone) InterlockedIncrement(&k.dpParked);
    return true;
}

// the owner (sim) thread outside a job callback, no job running: every parked reference released, oldest first, with job
// starts held off (ServeAsync's Dekker on g_rgOffSimOpen). A release may destroy more; a nested flush finds the list already
// taken. Owner only (0.36.9 design G): the free runs component destructors and the managed release, which belong to the sim.
static void P5cFlush() {
    if (!g_p5cGyN) return;
    if (GetCurrentThreadId() != g_async.owner || g_inInvoke) return;
    Ts3nCounters& k = ts3native_counters;
    InterlockedIncrement(&g_rgOffSimOpen);
    if (g_async.state == A_RUNNING) {
        InterlockedDecrement(&g_rgOffSimOpen);
        InterlockedIncrement(&k.p5cFlushRefused);
        return;
    }
    P5cGrave out[kP5cGrave];
    AcquireSRWLockExclusive(&g_p5cGyLock);
    const LONG n = g_p5cGyN;
    memcpy(out, g_p5cGy, n * sizeof(P5cGrave));
    InterlockedExchange(&g_p5cGyN, 0);
    ReleaseSRWLockExclusive(&g_p5cGyLock);
    for (LONG i = 0; i < n; i++) {
        void* obj = out[i].obj;
        Max(&k.p5cAgeMaxUs, UsSince(out[i].t));
        // T-G1: our reference must still be there (count at obj+4, docs/88 §3.1); else leak it rather than free twice
        if (!Readable((const u8*)obj + 4, 4) || *(volatile LONG*)((u8*)obj + 4) < 1) {
            InterlockedIncrement(&k.p5cCountLow);
            continue;
        }
        g_objRelease(&obj, NULL);
    }
    InterlockedExchangeAdd(&k.p5cFlushed, n);
    InterlockedDecrement(&g_rgOffSimOpen);
}

// --- no wait for the job while holding its lent lock ---
static bool P5cLent(DWORD tid) {  // this thread owns +0x918 inside a window (the lock the worker lent out)
    void* lock = g_p5bLock;
    return g_p5bWin && lock && P5bOwner(lock) == (LONG)tid;
}
// RunPlan on the owner while a job runs: with the lent lock the job is in its lock-free search, and waiting would deadlock
// against its retake; the sync plan runs here, under this thread's hold, as A10 runs a sync plan with no job
static bool P5cPlanHere() {
    if (!P5cLent(GetCurrentThreadId())) return false;
    InterlockedIncrement(&ts3native_counters.p5cPlanHere);
    return true;
}
// WaitAsyncDone, WaitJobDone, P5cNrEnter: the sim about to wait for the job (or its clone run) while it owns the lent lock.
// Every level goes back for the wait (the worker's retake gets it); P5cHandEnd waits for the whole job, then takes the levels
// again through the pump (the job's managed calls keep running). Tripwire: P5c is off for the rest of the session.
static __declspec(thread) void* t_p5cHand;
static LONG P5cHandBegin() {
    const DWORD me = GetCurrentThreadId();
    if (!P5cLent(me) || RgIsWorker(me)) return 0;
    Ts3nCounters& k = ts3native_counters;
    void* lock = g_p5bLock;
    const LONG n = ((const CRITICAL_SECTION*)lock)->RecursionCount;
    InterlockedIncrement(&k.p5cHandOver);
    k.p5cOff = 1;
    if (InterlockedIncrement(&g_p5cLog) <= 8)
        Log("P5c: thread %lu waited for the job of route %08lX holding its lent +0x918 (%ld levels): handed back for the wait; "
            "FreeLock off for this session", me, g_async.handle, n);
    InterlockedCompareExchange(&k.rgOwnerTid, 0, (LONG)me);
    for (LONG i = 0; i < n; i++) g_rgRelOrig(lock, NULL);
    t_p5cHand = lock;
    return n;
}
static void P5cHandEnd(LONG n) {
    if (!n) return;
    static const DWORD kInf = 0xFFFFFFFF;
    WaitAsyncDone();  // the whole job: a D2 writer must not write into a re-armed clone run either
    for (LONG i = 0; i < n; i++) LockAcquireHook(t_p5cHand, NULL, &kInf);
}
// D2 entry (rtsites.cpp, routegate.cpp coarse gates): P4NrEnter waits for the clone run, which may need the lock back first
static void P5cNrEnter() {
    const LONG n = SnLive() ? P5cHandBegin() : 0;
    P4NrEnter();
    P5cHandEnd(n);
}

// [RouteSnap] FreeLock=1 (from ApplyP5b, after P5b installed)
static void ApplyP5c(const wchar_t* ini) {
    Ts3nCounters& k = ts3native_counters;
    if (!GetPrivateProfileIntW(L"RouteSnap", L"FreeLock", 0, ini)) {
        Log("route snapshot P5c off (FreeLock=0): P5b admission only");
        return;
    }
    k.p5cFree = 1;
    k.p5cReady = 1;
    g_p5cOn = 1;
    Log("OK   route snapshot P5c (FreeLock=1): a taken clone run lends +0x918 to every plain sim acquire (writers too); destroys "
        "park one reference and run; answers with a routing write after the cut are refused (the game's plan answers)");
}
