// H6 design D (docs/84 §2.4): discourage footprints deferred on the sim thread while an async route job runs.
// Included by ts3native.cpp after rtsites.cpp. Installed with [RouteGate] Enforce=1 and [Discourage] Enabled=1 (default 1);
// runtime switch discMode (counters v30): 0 = the game's calls as they are, 1 = defer.
//
// RoutingWorld_AddDiscourageFootprint 0x834370 (thiscall world: obj, points, highImpassable, allowPlacementInWorld,
// placementFootprint; ret 0x14) and RoutingWorld_RemoveDiscourageFootprint 0x834860 (thiscall world: obj; ret 4) take
// +0x918, so while a job runs the sim thread waits for it (P0: 28.8% of the sim's waits). Script ignores their result
// except two debug interactions. While a job runs on the owner thread, a call is recorded (object id, copied points,
// flags) after the original's lock-free early outs and answers 1. Records are applied in order on the owner thread,
// holding +0x918 with no job running (the original's own preconditions), at script-IC points only (Add can fire
// World_FireEvent 0x11): the next D1/D2 call with no job running, GetOptionHook (START / READY / CANCEL), save and
// travel / quit drains. A record made in another world is dropped. Records added during a drain (an event handler) go to
// the tail.
//
// Fix D-X (0.16.0, counters v34, docs/84 §7.2): destroy does not remove routing footprints, so dropping a Remove record
// whose object is gone left the footprint behind under a dead id (PetGuardSitutation: Remove, then Destroy, in one call).
//   1. Destroy gate (switch discDestroyMode, [Discourage] DestroyApply): ObjectService_DestroyById 0x939420 (rtsites.cpp
//      RT_DESTROY, after its job wait, owner thread) applies that object's queued Remove records in order under +0x918 with
//      the original 0x834860 while the object is still in the id hash, drops its Add records, keeps every other record in
//      order. Inside a job callback (no wait possible) it only counts (discDestroyInInvoke).
//   2. Dead-id sweep (switch discSweepMode, [Discourage] DeadSweep): a Remove record whose object is gone at apply calls
//      LotRouting_RemoveObjectFootprints 0x635700 for its id on all lots (lot 0), as Remove does on a lock-key-map miss,
//      then clears world+0x994. A dead id still in the lock-key map is only counted.
//   3. Pointer check: the record keeps the object pointer (no reference, never dereferenced); FindById returning another
//      object means the id was reused: dropped and counted (discIdMismatch), not swept (the sweep would strip the new
//      object's footprints).
//   Tripwire T-D5: discDropRemove = discDeadSweep while discSweepMode is on.

static const DWORD DISC_ADD = 0x00834370, DISC_REMOVE = 0x00834860;
static const u8 kDiscAddPro[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-16
static const u8 kDiscRemovePro[] = {0x83, 0xEC, 0x14, 0x53, 0x55};     // sub esp,14h; push ebx; push ebp
typedef u8(__fastcall* DiscAddFn)(void* world, void* edx, void* obj, void* points, int high, int allow, int placement);
typedef u8(__fastcall* DiscRemoveFn)(void* world, void* edx, void* obj);
static DiscAddFn g_discAddOrig;
static DiscRemoveFn g_discRemoveOrig;
// the IC's own lookup and release (IC_RouteManager_RemoveDiscourageFootprint 0x754580)
typedef void*(__cdecl* SvcFn)();
// ObjectService_FindByIdAddRef 0x9395A0: thiscall(svc; idLo, idHi, Object** out), ret 0xC, true + AddRef'd *out when found
// [verified disassembly 2026-10-17; 0.12.0 declared two arguments and crashed the game: the callee popped 4 bytes too many]
typedef u8(__fastcall* FindRefFn)(void* svc, void* edx, DWORD idLo, DWORD idHi, void** out);
typedef void(__fastcall* ReleaseFn)(void** pp, void* edx);                            // releases *pp
static SvcFn g_objSvc = (SvcFn)(uintptr_t)0x00938B50;
static FindRefFn g_findRef = (FindRefFn)(uintptr_t)0x009395A0;
static ReleaseFn g_objRelease = (ReleaseFn)(uintptr_t)0x007460F0;
// D-X: LotRouting_RemoveObjectFootprints 0x635700: thiscall(P = [world+0x95C]; lotLo, lotHi, idLo, idHi), ret 0x10, al = a
// footprint was removed; lot 0 walks every lot [verified disassembly 0x635700..0x635916, Remove's call 0x834957..0x834964]
typedef u8(__fastcall* LotRemoveFn)(void* p, void* edx, DWORD lotLo, DWORD lotHi, DWORD idLo, DWORD idHi);
// lock-key map find 0x82AD60: thiscall(map = world+0x8B8; DWORD out[2], const DWORD* id), ret 8; out[0] = node, or the end
// sentinel buckets[bucketCount] (map+4, map+8) on a miss [verified disassembly 0x82AD60..0x82ADC5, Remove 0x8348F4..0x834918]
typedef void(__fastcall* KeyFindFn)(void* map, void* edx, DWORD* out, const DWORD* id);
static LotRemoveFn g_lotRemove = (LotRemoveFn)(uintptr_t)0x00635700;
static KeyFindFn g_keyFind = (KeyFindFn)(uintptr_t)0x0082AD60;

// flags kept as whole dwords; lock: the world it was made in; obj: the object at defer time (D-X pointer check, no reference)
struct DiscRec { bool add; DWORD high, allow, placement, idLo, idHi, t; int n; u8* pts; void* lock; void* obj; };
static const int kDiscCap = 256;
static const DWORD kDiscMaxAgeMs = 250;  // older records are applied even if that means waiting for the running job
static DiscRec g_disc[kDiscCap];
static int g_discHead, g_discCount;  // owner (sim) thread only
static bool g_discDraining;

static bool DiscWorldOk(void* world) { return world && *((const u8*)world + 0x974) != 0; }

// D-X 2: the Remove record's object is gone; remove whatever footprints its id left in the lot quadtrees. Caller holds +0x918.
static void DiscDeadSweep(const DiscRec& r, u8* world) {
    Ts3nCounters& k = ts3native_counters;
    void* p = *(void**)(world + 0x95C);
    if (!p) return;
    if (*(const DWORD*)(world + 0x8C4)) {  // Remove looks the id up only when the map holds entries
        const DWORD id[2] = {r.idLo, r.idHi};
        DWORD it[2] = {};
        g_keyFind(world + 0x8B8, NULL, it, id);
        const DWORD* buckets = *(const DWORD* const*)(world + 0x8BC);
        if (it[0] != buckets[*(const DWORD*)(world + 0x8C0)]) InterlockedIncrement(&k.discDeadKeyMap);  // shortcut: not erased
    }
    InterlockedIncrement(&k.discDeadSweep);
    if (g_lotRemove(p, NULL, 0, 0, r.idLo, r.idHi)) InterlockedIncrement(&k.discDeadHit);
    world[0x994] = 0;  // as Remove does after 0x635700
}

// true = applied with the original
static bool DiscApplyOne(DiscRec& r, void* world) {
    Ts3nCounters& k = ts3native_counters;
    void* svc = g_objSvc();
    void* obj = NULL;
    if (!svc || !g_findRef(svc, NULL, r.idLo, r.idHi, &obj)) obj = NULL;
    const bool applied = obj && obj == r.obj;
    if (obj && obj != r.obj) {  // the id names another object now (reused id): not the one the call was made for
        InterlockedIncrement(&k.discIdMismatch);
        g_objRelease(&obj, NULL);
        InterlockedIncrement(&k.discDropped);
    } else if (!obj) {
        InterlockedIncrement(&k.discDropped);
        InterlockedIncrement(r.add ? &k.discDropAdd : &k.discDropRemove);
        if (!r.add && k.discSweepMode) DiscDeadSweep(r, (u8*)world);
    } else {
        if (r.add) {
            u8* vec[3] = {r.pts, r.pts + r.n * 16, r.pts + r.n * 16};  // begin, end, capacity (the original reads begin and end)
            g_discAddOrig(world, NULL, obj, vec, (int)r.high, (int)r.allow, (int)r.placement);
        } else {
            g_discRemoveOrig(world, NULL, obj);
        }
        g_objRelease(&obj, NULL);
        InterlockedIncrement(&k.discApplied);
    }
    _aligned_free(r.pts);
    r.pts = NULL;
    return applied;
}

// Owner thread: applies every record in order. force = wait for a running job; else only when no job runs.
static void DiscDrain(bool force) {
    Ts3nCounters& k = ts3native_counters;
    if (!g_discCount || g_discDraining || GetCurrentThreadId() != g_async.owner) return;
    if (!force && g_async.state == A_RUNNING) return;
    const bool jobRan = g_async.state == A_RUNNING;
    void* lock = g_rgLock;
    void* world = lock ? (u8*)lock - 0x918 : NULL;
    g_discDraining = true;
    InterlockedIncrement(&g_rgOffSimOpen);  // the worker starts no job until the drain is done
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    WaitAsyncDone();
    static const DWORD kInfinite = 0xFFFFFFFF;
    if (lock) LockAcquireHook(lock, NULL, &kInfinite);  // uncontended: no job runs and none can start
    const LONG waited = UsSince(t0);
    InterlockedExchangeAdd(&k.discDrainWaitUs, waited);
    HaNote(0x06000000, waited, HA_JOB);
    if (jobRan) InterlockedIncrement(&k.discForced);
    const DWORD now = GetTickCount();
    while (g_discCount) {  // records added by an event handler meanwhile go to the tail and are applied too
        DiscRec r = g_disc[g_discHead];
        g_disc[g_discHead].pts = NULL;
        g_discHead = (g_discHead + 1) % kDiscCap;
        g_discCount--;
        Max(&k.discAgeMaxMs, (LONG)(now - r.t));
        if (r.lock == lock && DiscWorldOk(world)) DiscApplyOne(r, world);  // a record from another world is dropped
        else { InterlockedIncrement(&k.discDropped); _aligned_free(r.pts); }
    }
    if (lock) LockReleaseHook(lock, NULL);
    InterlockedDecrement(&g_rgOffSimOpen);
    g_discDraining = false;
}

// A drain point that is not a D1/D2 call: apply when no job runs, or when the oldest record is too old.
static void DiscPoll() {
    if (!g_discCount) return;
    DiscDrain(GetTickCount() - g_disc[g_discHead].t >= kDiscMaxAgeMs);
}

// D-X 1, from rtsites.cpp RT_DESTROY before ObjectService_DestroyById(idLo, idHi) runs; canWait = its job wait returned (no
// job runs now), false inside a job callback. Applies the object's Remove records in order under +0x918 (managed-free,
// docs/84 §7.1, so safe at every native destroy caller), drops its Add records, keeps the other records in their order.
static void DiscOnDestroy(DWORD idLo, DWORD idHi, bool canWait) {
    Ts3nCounters& k = ts3native_counters;
    if (!g_discCount || g_discDraining || GetCurrentThreadId() != g_async.owner) return;
    void* lock = g_rgLock;
    const int n0 = g_discCount;
    int mine = 0;
    for (int i = 0; i < n0; i++) {
        const DiscRec& r = g_disc[(g_discHead + i) % kDiscCap];
        if (r.idLo == idLo && r.idHi == idHi && r.lock == lock) mine++;
    }
    if (!mine) return;
    if (!canWait) { InterlockedIncrement(&k.discDestroyInInvoke); return; }  // left queued: the dead-id sweep handles it
    void* world = lock ? (u8*)lock - 0x918 : NULL;
    if (!k.discDestroyMode || !DiscWorldOk(world)) return;
    static const DWORD kInfinite = 0xFFFFFFFF;
    g_discDraining = true;  // no drain inside; a record made meanwhile (none expected: Remove has no managed reach) goes last
    LockAcquireHook(lock, NULL, &kInfinite);  // uncontended: no job runs and none starts while the destroy is open
    int w = 0;
    for (int i = 0; i < n0; i++) {
        DiscRec& r = g_disc[(g_discHead + i) % kDiscCap];
        if (r.idLo != idLo || r.idHi != idHi || r.lock != lock) {
            if (w != i) { g_disc[(g_discHead + w) % kDiscCap] = r; r.pts = NULL; }
            w++;
        } else if (r.add) {
            InterlockedIncrement(&k.discDropAdd);
            InterlockedIncrement(&k.discDropped);
            _aligned_free(r.pts);
            r.pts = NULL;
        } else if (DiscApplyOne(r, world)) {
            InterlockedIncrement(&k.discDestroyApplied);
        }
    }
    for (int i = n0; i < g_discCount; i++, w++) {  // records made during the loop move down behind the kept ones
        DiscRec& r = g_disc[(g_discHead + i) % kDiscCap];
        g_disc[(g_discHead + w) % kDiscCap] = r;
        r.pts = NULL;
    }
    g_discCount = w;
    LockReleaseHook(lock, NULL);
    g_discDraining = false;
}

// true = recorded (the caller answers 1)
static bool DiscDefer(bool add, void* obj, const u8* begin, int n, int high, int allow, int placement) {
    Ts3nCounters& k = ts3native_counters;
    if (k.discMode != 1 || GetCurrentThreadId() != g_async.owner) return false;
    if (!g_discDraining && g_async.state != A_RUNNING) return false;  // no job: apply now (after older records)
    if (g_discCount == kDiscCap) { InterlockedIncrement(&k.discOverflow); return false; }
    DiscRec& r = g_disc[(g_discHead + g_discCount) % kDiscCap];
    r.add = add;
    r.high = (DWORD)high;
    r.allow = (DWORD)allow;
    r.placement = (DWORD)placement;
    r.idLo = *(const DWORD*)((const u8*)obj + 8);
    r.idHi = *(const DWORD*)((const u8*)obj + 0xC);
    r.t = GetTickCount();
    r.n = n;
    r.pts = NULL;
    r.lock = g_rgLock;
    r.obj = obj;
    if (add) {
        // 16-byte aligned like the game's own vector: the original reads the points with movaps (0xAB88BD); a malloc'd
        // copy (8-byte aligned) crashed it in 0.12.2 (#GP reported as an access violation at 0xFFFFFFFF, 2026-10-17)
        r.pts = (u8*)_aligned_malloc((size_t)n * 16, 16);
        if (!r.pts) { InterlockedIncrement(&k.discOverflow); return false; }
        memcpy(r.pts, begin, (size_t)n * 16);
    }
    g_discCount++;
    if (g_discDraining) InterlockedIncrement(&k.discNested);
    InterlockedIncrement(add ? &k.discDeferredAdd : &k.discDeferredRemove);
    Max(&k.discPendingMax, g_discCount);
    return true;
}

static u8 __fastcall DiscAddHook(void* world, void* edx, void* obj, void* points, int high, int allow, int placement) {
    const u8* const* v = (const u8* const*)points;
    const int n = v && v[0] ? (int)((v[1] - v[0]) / 16) : 0;
    if (t_snapRun || !DiscWorldOk(world) || !obj || n < 3) return g_discAddOrig(world, edx, obj, points, high, allow, placement);  // early outs
    if (DiscDefer(true, obj, v[0], n, high, allow, placement)) return 1;
    DiscDrain(true);  // older records first (no job runs, or the queue is full)
    return g_discAddOrig(world, edx, obj, points, high, allow, placement);
}

static u8 __fastcall DiscRemoveHook(void* world, void* edx, void* obj) {
    if (t_snapRun || !DiscWorldOk(world) || !obj) return g_discRemoveOrig(world, edx, obj);  // early outs, P4 clone run
    if (DiscDefer(false, obj, NULL, 0, 0, 0, 0)) return 1;
    DiscDrain(true);
    return g_discRemoveOrig(world, edx, obj);
}

static void InstallDiscourage(const wchar_t* ini) {
    if (!ts3native_counters.rgEnforce) return;  // the deferral is only needed (and only safe to reason about) with the gate on
    // default off until the 0.12.4 alignment fix has run in game (0.12.2 crashed on an 8-byte aligned points copy)
    if (!GetPrivateProfileIntW(L"Discourage", L"Enabled", 0, ini)) { Log("discourage deferral off (Enabled=0)"); return; }
    g_discAddOrig = (DiscAddFn)InstallDetour("RoutingWorld_AddDiscourageFootprint", DISC_ADD, kDiscAddPro, sizeof(kDiscAddPro), (void*)DiscAddHook);
    g_discRemoveOrig = g_discAddOrig ? (DiscRemoveFn)InstallDetour("RoutingWorld_RemoveDiscourageFootprint", DISC_REMOVE, kDiscRemovePro,
                                                                 sizeof(kDiscRemovePro), (void*)DiscRemoveHook) : NULL;
    if (!g_discRemoveOrig) { Log("FAIL discourage deferral: detours incomplete"); return; }  // shortcut: a lone Add detour stays, mode 0
    ts3native_counters.discReady = 1;
    ts3native_counters.discMode = Clamp(GetPrivateProfileIntW(L"Discourage", L"Mode", 1, ini), 0, 1);
    // D-X: correctness parts of D, on with it unless switched off for an A/B
    ts3native_counters.discDestroyMode = Clamp(GetPrivateProfileIntW(L"Discourage", L"DestroyApply", 1, ini), 0, 1);
    ts3native_counters.discSweepMode = Clamp(GetPrivateProfileIntW(L"Discourage", L"DeadSweep", 1, ini), 0, 1);
    Log("OK   discourage deferral (Mode=%ld DestroyApply=%ld DeadSweep=%ld)", ts3native_counters.discMode,
        ts3native_counters.discDestroyMode, ts3native_counters.discSweepMode);
}
