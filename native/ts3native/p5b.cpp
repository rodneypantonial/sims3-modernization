// [RouteSnap] ReleaseLock=1: P5b (docs/94 §22.9; counters v62). Included by ts3native.cpp after p5.cpp. Needs Mode=2.
// P5 runs its clone run inside the A10 job, and the job holds the live routing lock +0x918 for the whole IC call, so the sim
// still waits for the clone run as it waited for the game's own search. The clone run reads only the clone (T1-T4, the copy's
// world; a live +0x918 acquire from it is the livelock tripwire), so P5b lends +0x918 out while it runs:
//   - open (worker, P5Plan after the acquire, before P4RunClone; taken jobs only, verify jobs keep the hold): publish
//     g_p5bWin, wait until no disallowed acquire is inside the game's Lock_Acquire (P5bLock counts them: a try ends at once,
//     a blocking wait in there would get the lock the moment we let go, so it refuses the window), then release the worker's
//     whole recursion count with the game's Lock_Release;
//   - while open, only the sim's steering takers get the lock: RT_STEER frames (0x82EDB0 / 0x82F0B0, rtsites.cpp, t_steerRet
//     set) outside a gate wait and outside a job callback, plus recursive acquires of the holder. Every other acquire sees the
//     lock as the worker's, as today: a try fails, a wait goes on until the window is closed (and then for the job). Steering
//     is the docs/81-83 per-tick taker pair; under +0x918 it makes no managed call, remote call, world event or destroy and
//     waits for no job (docs/83 §1 closure, 5 hops [verified there]), so it always gives the lock back;
//   - g_async.state stays A_RUNNING: destroys, saves, BuildRoutingData, lot saves, closing, D drains, collects and sync plans
//     wait for the job as before (RtWaitJob, WaitAsyncDone, the gate waits), so nothing the suspended IC frame holds is
//     destroyed or freed meanwhile, and H2 / job starts are unchanged (no job starts while this one runs);
//   - retake (worker) before anything live: after the clone run (P5Plan, before the answer is moved or the game's plan runs),
//     before a door ask (its managed call goes to the sim, whose callbacks borrow the job's hold; reopened after the ask), and
//     at any tripwire (P4Abort: managed call, world event, live +0x918 acquire, lazy stop; not reopened). The retake tries in a
//     loop (the steering holder ends its call) and logs every 2 s with the owner thread.
// 0.34.2 ([RouteSnap] ReleaseGates=1, switch p5bGates; docs/94 §22.10): read-only gate leaves are admitted too, on the sim
// (the jobs' owner) outside a job callback, during a gate's +0x918 wait (RgEnter sets t_p5bSite / t_p5bSlot):
//   - gate 12 LotTable_Contains (reads only the table) for the read families: F-C front door (key 0x631814), F-G placement
//     (0x631F40), and F-B slot position (key 0x62F782 with second frame 0x76F15D). Not the rest of 0x62F782 / 0x62FB74: F-A
//     footprint level rebuild, F-D portal writer ICs (they write the live portal objects the clone holds), F-E, F-F;
//   - gate 0 LotTable_GetOrInsert on a hit (checked with LtFind once the lent lock is ours, so nothing else moves the table):
//     a hit writes nothing; a miss would insert, so the lock is given back and the entry waits as before.
// Their callers' portal counts are interlocked while P4 is armed (H20a). Writers (gate 10 and the coarse gates, the footprint
// writers 0x833593 / 0x8334CC / 0x82D296 / 0x82A0F6, discourage) still wait; destroys still wait (RtWaitJob; design G).
// 0.35.0 ([RouteSnap] FreeLock=1, p5c.cpp, docs/94 §22.11): P5c admits every plain sim acquire too (P5B_FREE), parks
// destroys and refuses stale answers.
// shortcut: gate families by fixed keys; a new read family (F-F) needs its closure read first, then a key here.

static volatile LONG g_p5bOn;     // ReleaseLock=1 installed
static volatile LONG g_p5cJob;    // P5c (p5c.cpp, FreeLock=1): this job's window admits every plain sim acquire
static void ApplyP5c(const wchar_t* ini);  // p5c.cpp
static volatile LONG g_p5bWin;    // 1 while the worker has +0x918 lent out (published before the release, cleared after the retake)
static volatile LONG g_p5bIn;     // disallowed +0x918 acquires inside the game's Lock_Acquire now (P5bLock)
static void* g_p5bLock;           // the lock lent out (worker only)
static LONG g_p5bN;               // the recursion count to take back
static LARGE_INTEGER g_p5bT0;     // window start
enum { P5B_CLOSED, P5B_OPEN, P5B_ASK, P5B_DONE };
static __declspec(thread) int t_p5b;  // worker: window open, retaken for a door ask (reopened after it), retaken for this run
static const LONG kP5bInWaitUs = 200;  // a try in flight ends in microseconds; longer = a blocking wait: no window
static volatile LONG g_p5bWdLog;

static inline LONG P5bOwner(const void* lock) { return (LONG)(uintptr_t)((const CRITICAL_SECTION*)lock)->OwningThread; }

enum { P5B_NO, P5B_STEER, P5B_G12, P5B_G0, P5B_FREE };  // P5B_FREE: P5c (p5c.cpp), any plain sim acquire
static const DWORD kP5bPfoRet = 0x0062F782, kP5bSlotRet = 0x0076F15D;  // F-B: PortalsForObject's key, its caller 0x76F0A0

// a gate's +0x918 wait on the sim: a read-only leaf (P5B_G12, P5B_G0 = a hit still to check) or not (P5B_NO)
static int P5bGate() {
    if (!ts3native_counters.p5bGates || !t_waitKey || !t_p5bSlot) return P5B_NO;
    if (t_p5bSite == 1) return P5B_G0;
    if (t_p5bSite != 13) return P5B_NO;
    if (t_waitKey == 0x00631814 || t_waitKey == 0x00631F40) return P5B_G12;
    return t_waitKey == kP5bPfoRet && VfRealRet(t_p5bSlot + 7) == kP5bSlotRet ? P5B_G12 : P5B_NO;  // P1b's frame offset
}
// gate 0 with the lent lock held: GetOrInsert(P+0x48)(key*) finds the key (slot[-2] = ecx, slot[1] = key*)
static bool P5bGate0Hit() {
    const DWORD* slot = t_p5bSlot;
    const DWORD* key = (const DWORD*)(uintptr_t)slot[1];
    const u8* e;
    return LtFind((const u8*)(uintptr_t)slot[-2] - 0x48, key[0], key[1], &e) == LT_OK && e;
}

// may this acquire get the lent-out lock? (the holder's own recursion, the sim's steering call, a read-only gate leaf)
static int P5bAllowed(const void* lock) {
    const DWORD me = GetCurrentThreadId();
    if (P5bOwner(lock) == (LONG)me) return P5B_STEER;
    if (me != g_async.owner || g_inInvoke) return P5B_NO;
    if (t_steerRet && !t_waitKey) return P5B_STEER;
    // P5c: every acquire outside a gate wait (t_waitKey stays set through a gate-0 miss given back; the C-A rebuild wait,
    // steerro.cpp, keys itself 0x80E5B0 and is a writer)
    return g_p5cJob && (!t_waitKey || t_waitKey == 0x0080E5B0) ? P5B_FREE : P5bGate();
}

// LockAcquireAsync's every call of the game's Lock_Acquire (tries, non-owner waits, the deadline's last try)
static LONG P5bLock(void* lock, void* edx, const DWORD* d) {
    if (!g_p5bOn || lock != g_rgLock) return g_lockOrig(lock, edx, d);
    Ts3nCounters& k = ts3native_counters;
    if (const int adm = P5bAllowed(lock)) {
        const bool outer = P5bOwner(lock) != (LONG)GetCurrentThreadId();
        const LONG r = g_lockOrig(lock, edx, d);
        if (r <= 0 || !outer || !g_p5bWin) return r;
        if (adm != P5B_G0 || P5bGate0Hit()) {
            InterlockedIncrement(adm == P5B_STEER ? &k.p5bSimAcq : adm == P5B_FREE ? &k.p5cAcq : &k.p5bGateAcq[adm - P5B_G12]);
            return r;
        }
        g_rgRelOrig(lock, NULL);  // a miss would insert: the lock back to the window, this entry waits as before
        t_p5bSite = 0;
        InterlockedIncrement(&k.p5bGateMiss);
    }
    for (bool counted = false;;) {
        InterlockedIncrement(&g_p5bIn);  // Dekker with P5bOpen: published, then the window read
        if (!g_p5bWin) {
            const LONG r = g_lockOrig(lock, edx, d);
            InterlockedDecrement(&g_p5bIn);
            return r;
        }
        InterlockedDecrement(&g_p5bIn);
        if (!counted) { counted = true; InterlockedIncrement(&k.p5bHeldOut); }
        // the lock is the worker's for this caller: a try or a passed deadline fails as it would against the worker's hold
        if (*d == 0 || (*d != 0xFFFFFFFF && (LONG)(GetTickCount() - *d) >= 0)) return -2;
        while (g_p5bWin && (*d == 0xFFFFFFFF || (LONG)(GetTickCount() - *d) < 0)) Sleep(1);
    }
}

// worker: lend the job's +0x918 out (P5Plan before the clone run, P5bAsk after a door ask)
static void P5bOpen(void* lock) {
    Ts3nCounters& k = ts3native_counters;
    const CRITICAL_SECTION* cs = (const CRITICAL_SECTION*)lock;
    const DWORD me = GetCurrentThreadId();
    int why = -1;
    if (!k.p5bRelease) why = 0;
    else if (lock != g_rgLock || lock != g_async.lock || g_async.state != A_RUNNING) why = 3;
    // the worker holds it n times (ServeAsync's hold and any outer frame's); the game's own count must agree
    else if (P5bOwner(lock) != (LONG)me || cs->RecursionCount < 1 || cs->RecursionCount > 16 ||
             *(const LONG*)((const u8*)lock + 0x20) != cs->RecursionCount)
        why = 1;
    if (why < 0) {
        InterlockedExchange(&g_p5bWin, 1);
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        while (InterlockedCompareExchange(&g_p5bIn, 0, 0) && UsSince(t0) < kP5bInWaitUs) YieldProcessor();
        if (InterlockedCompareExchange(&g_p5bIn, 0, 0)) {
            InterlockedExchange(&g_p5bWin, 0);
            why = 2;
        }
    }
    if (why >= 0) {
        InterlockedIncrement(&k.p5bRefused[why]);
        t_p5b = P5B_DONE;
        return;
    }
    g_p5bLock = lock;
    g_p5bN = cs->RecursionCount;
    Max(&k.p5bRecMax, g_p5bN);
    InterlockedCompareExchange(&k.rgOwnerTid, 0, (LONG)me);
    for (LONG i = 0; i < g_p5bN; i++) g_rgRelOrig(lock, NULL);
    QueryPerformanceCounter(&g_p5bT0);
    InterlockedIncrement(&k.p5bWins);
    t_p5b = P5B_OPEN;
}

// worker: the hold back, every level of it; then the window closes (disallowed acquirers saw the worker's hold throughout)
static void P5bRetake(int next) {
    if (t_p5b != P5B_OPEN) return;
    Ts3nCounters& k = ts3native_counters;
    static const DWORD kTry = 0, kInf = 0xFFFFFFFF;
    void* lock = g_p5bLock;
    InterlockedExchangeAdd(&k.p5bWinUs, UsSince(g_p5bT0));
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    // only an admitted steering call can hold it now; it waits for no job, so it ends (the log names the holder if not)
    for (LONG next2 = 2000; g_lockOrig(lock, NULL, &kTry) == -2;) {
        SwitchToThread();
        if (UsSince(t0) / 1000 >= next2) {
            InterlockedIncrement(&k.p5bWatchdog);
            if (InterlockedIncrement(&g_p5bWdLog) <= 20)
                Log("P5b: worker waited %ld s to take +0x918 %p back (owner thread %ld, sim %lu); still trying", next2 / 1000, lock,
                    P5bOwner(lock), g_async.owner);
            next2 += 2000;
        }
    }
    for (LONG i = 1; i < g_p5bN; i++) g_lockOrig(lock, NULL, &kInf);  // recursive: ours already
    k.rgOwnerTid = (LONG)GetCurrentThreadId();
    InterlockedExchange(&g_p5bWin, 0);
    const LONG us = UsSince(t0);
    InterlockedExchangeAdd(&k.p5bRetakeUs, us);
    Max(&k.p5bRetakeMaxUs, us);
    t_p5b = next;
}

// P5Plan, around P4RunClone
static void P5bBegin(void* lock, bool verify) {
    t_p5b = P5B_CLOSED;
    if (!g_p5bOn) return;
    if (verify) { InterlockedIncrement(&ts3native_counters.p5bRefused[0]); return; }
    P5bOpen(lock);
}
static void P5bEnd() {
    P5bRetake(P5B_DONE);
    t_p5b = P5B_CLOSED;
}
// p4.cpp P4DoorEnter, a Mode=2 door ask: the hold back before it, lent out again after it
static void P5bAsk(bool begin) {
    if (begin) {
        if (t_p5b != P5B_OPEN) return;
        InterlockedIncrement(&ts3native_counters.p5bRetakeAsk);
        P5bRetake(P5B_ASK);
    } else if (t_p5b == P5B_ASK) {
        P5bOpen(g_p5bLock);
    }
}
// p4.cpp P4Abort (worker, a tripwire in the clone run): the hold back for the rest of the run
static void P5bAbort() {
    if (t_p5b == P5B_OPEN) {
        InterlockedIncrement(&ts3native_counters.p5bRetakeTrip);
        P5bRetake(P5B_DONE);
    } else if (t_p5b == P5B_ASK) {
        t_p5b = P5B_DONE;
    }
}

// [RouteSnap] ReleaseLock=1 (from ApplyP5)
static void ApplyP5b(const wchar_t* ini) {
    Ts3nCounters& k = ts3native_counters;
    if (!GetPrivateProfileIntW(L"RouteSnap", L"ReleaseLock", 0, ini)) {
        Log("route snapshot P5b off (ReleaseLock=0): P5 clone runs keep the job's +0x918");
        if (GetPrivateProfileIntW(L"RouteSnap", L"FreeLock", 0, ini)) Log("FAIL route snapshot P5c: FreeLock=1 needs ReleaseLock=1");
        return;
    }
    const LONG steer = 1 << 23 | 1 << 24;  // rtsites.cpp: the two RT_STEER sites (they set t_steerRet)
    if (!g_lockOrig || !g_rgRelOrig || !k.rgEnforce || (k.rtReady & steer) != steer) {
        Log("FAIL route snapshot P5b: needs [RouteGate] Enabled=1 Sites=1 Enforce=1 (Lock_Release, steering frames); P5 clone "
            "runs keep the job's +0x918");
        return;
    }
    k.p5bRelease = 1;
    k.p5bGates = GetPrivateProfileIntW(L"RouteSnap", L"ReleaseGates", 0, ini) ? 1 : 0;
    k.p5bReady = 1;
    g_p5bOn = 1;
    Log("OK   route snapshot P5b (ReleaseLock=1): +0x918 lent to the sim's steering%s while a P5 clone run runs; taken back "
        "before the answer, each door ask and any tripwire", k.p5bGates ? " and its read-only gate leaves (ReleaseGates=1)" : "");
    ApplyP5c(ini);
}
