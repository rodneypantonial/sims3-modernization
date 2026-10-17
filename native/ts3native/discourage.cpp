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
// travel / quit drains. A record whose object is gone, or made in another world, is dropped (destroy removed its
// footprints [inferred]). Records added during a drain (an event handler) go to the tail.

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

struct DiscRec { bool add; DWORD high, allow, placement, idLo, idHi, t; int n; u8* pts; void* lock; };  // flags kept as whole dwords  // lock: the world it was made in
static const int kDiscCap = 256;
static const DWORD kDiscMaxAgeMs = 250;  // older records are applied even if that means waiting for the running job
static DiscRec g_disc[kDiscCap];
static int g_discHead, g_discCount;  // owner (sim) thread only
static bool g_discDraining;

static bool DiscWorldOk(void* world) { return world && *((const u8*)world + 0x974) != 0; }

static void DiscApplyOne(DiscRec& r, void* world) {
    Ts3nCounters& k = ts3native_counters;
    void* svc = g_objSvc();
    void* obj = NULL;
    if (!svc || !g_findRef(svc, NULL, r.idLo, r.idHi, &obj)) obj = NULL;
    if (!obj) {
        InterlockedIncrement(&k.discDropped);
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
    if (!DiscWorldOk(world) || !obj || n < 3) return g_discAddOrig(world, edx, obj, points, high, allow, placement);  // early outs
    if (DiscDefer(true, obj, v[0], n, high, allow, placement)) return 1;
    DiscDrain(true);  // older records first (no job runs, or the queue is full)
    return g_discAddOrig(world, edx, obj, points, high, allow, placement);
}

static u8 __fastcall DiscRemoveHook(void* world, void* edx, void* obj) {
    if (!DiscWorldOk(world) || !obj) return g_discRemoveOrig(world, edx, obj);  // early outs
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
    Log("OK   discourage deferral (Mode=%ld)", ts3native_counters.discMode);
}
