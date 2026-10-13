// Self-check for the patch helpers on local memory (no game needed). Built and run by build.cmd.
#define TS3NATIVE_NO_DLLMAIN
#include "ts3native.cpp"
#include <assert.h>

static int __cdecl Hook() { return 7; }

// --- route cache: key, table, and the hook against a fake Route_PlanLocked ---
static float g_pts[3][16];  // 64-byte route points: x,y,z at +0
static int g_status;        // what the fake path reports
static int g_origCalls;
static int __fastcall FakeStatus(void*, void*) { return g_status; }
static void* g_fakeVtbl[16];
static void* g_fakePath[2] = {g_fakeVtbl, 0};
static int g_node[3];  // list node: next, prev, path

static u8 __fastcall FakePlan(void*, void*, int* outList, int*, int*, DWORD, DWORD, u8*, int*, int*, DWORD, DWORD) {
    g_origCalls++;
    g_node[0] = g_node[1] = (int)(uintptr_t)outList;
    g_node[2] = (int)(uintptr_t)g_fakePath;
    outList[0] = outList[1] = (int)(uintptr_t)g_node;
    return g_status <= 2;
}

static void TestRouteCache() {
    u8 opts[0x38] = {0};
    int s[2] = {(int)(uintptr_t)g_pts[0], (int)(uintptr_t)g_pts[1]};
    int g[2] = {(int)(uintptr_t)g_pts[1], (int)(uintptr_t)g_pts[2]};
    g_pts[0][0] = 10.1f; g_pts[0][2] = 20.2f;
    g_pts[1][0] = 50.0f; g_pts[1][2] = 60.0f;
    u64 k = RouteKey(s, g, 0x100, opts, 5, 0);
    g_pts[0][0] = 10.4f;  // same 0.5 m cell
    assert(RouteKey(s, g, 0x100, opts, 5, 0) == k);
    g_pts[0][0] = 10.6f;  // next cell
    assert(RouteKey(s, g, 0x100, opts, 5, 0) != k);
    g_pts[0][0] = 10.1f;
    assert(RouteKey(s, g, 0x100, opts, 6, 0) != k);    // other follower
    assert(RouteKey(s, g, 0x800, opts, 5, 0) != k);    // other agent class
    opts[0x2C] = 0x10;                                 // RouteOption hi word bit
    assert(RouteKey(s, g, 0x100, opts, 5, 0) != k);
    opts[0x2C] = 0;
    g[1] += 64;                                        // one more goal
    assert(RouteKey(s, g, 0x100, opts, 5, 0) != k);
    g[1] -= 64;

    assert(CacheInit(300, 2000) && g_mask == 511);
    assert(!CacheHit(k, 1000));
    CacheStore(k, 1000);
    assert(CacheHit(k, 2999) && !CacheHit(k, 3000));   // window is exclusive
    CacheStore(k, 5000);                               // re-store refreshes, no duplicate slot
    int n = 0;
    for (DWORD i = 0; i <= g_mask; i++) n += g_tab[i].key == k;
    assert(n == 1 && CacheHit(k, 6000));
    // 9 live keys on one probe chain: the oldest is evicted, the table never grows
    for (u64 i = 0; i < 9; i++) CacheStore(512 * i + 7, 100 + i);
    assert(!CacheHit(7, 101) && CacheHit(512 * 8 + 7, 101) && CacheHit(512 + 7, 101));
    free(g_tab);

    // hook: 7/8 failure is stored, the repeat is answered without the original; success and 3 are never stored
    assert(CacheInit(4096, 2000));
    g_fakeVtbl[0x20 / 4] = (void*)FakeStatus;
    g_planOrig = FakePlan;
    int list[2];
    list[0] = list[1] = (int)(uintptr_t)list;
    g_status = 8;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 5, 0) == 0 && g_origCalls == 1);
    list[0] = list[1] = (int)(uintptr_t)list;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 5, 0) == 0 && g_origCalls == 1);
    assert(list[0] == (int)(uintptr_t)list);           // hit leaves the out list empty
    assert(ts3native_counters.hits == 1 && ts3native_counters.stored == 1);
    g_status = 3;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 6, 0) == 0 && g_origCalls == 2);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 6, 0) == 0 && g_origCalls == 3);
    g_status = 2;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 7, 0) == 1);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 7, 0) == 1 && g_origCalls == 5);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 0, opts, NULL, NULL, 5, 0) == 1 && g_origCalls == 6);  // doPlan=0 passes through
    assert(ts3native_counters.otherFails == 2 && ts3native_counters.hits == 1 && ts3native_counters.calls == 7);
}

// --- route worker: the original runs on the worker with the caller's FPU control word, composed with the cache ---
static DWORD g_planTid;
static unsigned short g_planCw;
static int g_reenter;
static int g_callInvoke;      // WorkerPlan makes a managed call through the runtime_invoke slot
static DWORD g_fakeSlot;      // stands in for 0x1195B60
static DWORD g_invokeTid;
static int g_invokeNested;    // the managed call starts a plan of its own
static void* __cdecl FakeInvoke(void* method, void* obj, void**, void** exc) {
    g_invokeTid = GetCurrentThreadId();
    if (exc) *exc = NULL;
    if (g_invokeNested) {  // like script code planning a route from inside the callback: back to the worker
        g_invokeNested = 0;
        u8 opts[0x38] = {0};
        int s[2] = {1, 2}, g[2] = {3, 4}, list[2];
        assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planTid == g_workerTid);
    }
    return (char*)obj + (uintptr_t)method;
}
static u8 __fastcall WorkerPlan(void*, void*, int* outList, int* starts, int* goals, DWORD agent, DWORD, u8* opts, int*, int*,
                                DWORD guidLo, DWORD) {
    g_planTid = GetCurrentThreadId();
    g_planCw = GetX87();
    if (g_reenter) {  // a plan started on the worker runs there directly instead of waiting for itself
        g_reenter = 0;
        assert(PlanLockedHook(NULL, NULL, outList, starts, goals, agent, 1, opts, NULL, NULL, 1, 0) == 1);
        assert(g_planTid == GetCurrentThreadId());
    }
    if (g_callInvoke) {  // the managed call runs on the waiting caller, the plan continues here with its result
        g_callInvoke = 0;
        void* r = ((InvokeFn)(uintptr_t)g_fakeSlot)((void*)1, (void*)0x100, NULL, NULL);
        assert(r == (void*)0x101 && g_invokeTid != GetCurrentThreadId());
        g_planTid = GetCurrentThreadId();
        g_planCw = GetX87();
    }
    return (u8)guidLo;  // echo, so each call's own result comes back
}

static void TestRouteWorker() {
    free(g_tab);
    g_tab = NULL;  // worker only
    assert(StartWorker());
    ts3native_counters.workerReady = 1;
    g_planOrig = WorkerPlan;
    u8 opts[0x38] = {0};
    int s[2] = {(int)(uintptr_t)g_pts[0], (int)(uintptr_t)g_pts[1]};
    int g[2] = {(int)(uintptr_t)g_pts[1], (int)(uintptr_t)g_pts[2]};
    int list[2];
    const DWORD me = GetCurrentThreadId();
    const Ts3nCounters c0 = ts3native_counters;

    ts3native_counters.workerMode = 0;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planTid == me);
    ts3native_counters.workerMode = 1;
    // runtime_invoke slot not as expected (Mono not up, or another build): refused, plans stay on the caller
    g_invokeSlot = &g_fakeSlot;
    g_invokeCheckBytes = false;
    g_invokeExpect = (DWORD)(uintptr_t)FakeInvoke;
    g_fakeSlot = 0xEA1080;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planTid == me);
    assert(ts3native_counters.invokeHook == -1 && g_fakeSlot == 0xEA1080);
    ts3native_counters.invokeHook = 0;
    g_fakeSlot = (DWORD)(uintptr_t)FakeInvoke;
    const unsigned short cw = GetX87();
    SetX87((unsigned short)(cw & ~0x300));  // 24-bit precision, as D3D9 can leave the game thread
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 0, 0) == 0);
    assert(g_planTid == g_workerTid && g_planTid != me && g_planCw == GetX87());
    SetX87(cw);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planCw == cw);
    assert(ts3native_counters.invokeHook == 1 && g_fakeSlot == (DWORD)(uintptr_t)InvokeHook);
    assert(((InvokeFn)(uintptr_t)g_fakeSlot)((void*)2, (void*)0x100, NULL, NULL) == (void*)0x102 && g_invokeTid == me);  // other threads: pass-through
    g_callInvoke = 1;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_invokeTid == me && g_planTid == g_workerTid);
    g_callInvoke = g_invokeNested = 1;  // worker plan -> managed call on the caller -> nested plan on the worker
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_invokeTid == me && !g_invokeNested);
    g_reenter = 1;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 0, opts, NULL, NULL, 1, 0) == 1 && g_planTid == me);  // doPlan=0 stays
    const Ts3nCounters& c = ts3native_counters;
    assert(c.workerCalls - c0.workerCalls == 6 && c.fallbacks - c0.fallbacks == 1 && c.watchdogs == 0 && c.invokes == 2);
    assert(c.waitUs >= c.workerUs);

    // with the cache: a 7/8 failure found on the worker is stored, the repeat is a hit that never reaches the worker
    assert(CacheInit(4096, 2000));
    g_planOrig = FakePlan;
    g_status = 8;
    const LONG calls0 = c.workerCalls, hits0 = c.hits;
    list[0] = list[1] = (int)(uintptr_t)list;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 9, 0) == 0);
    list[0] = list[1] = (int)(uintptr_t)list;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 9, 0) == 0);
    assert(c.workerCalls - calls0 == 1 && c.hits - hits0 == 1);
    ts3native_counters.workerMode = 0;
}

// --- route budget: stubs in place of the A*'s cost-function pointers, against a fake A* ---
static int g_costCalls;
static float __cdecl FakeCost(DWORD, DWORD, DWORD, DWORD, DWORD isEdge, DWORD, DWORD, DWORD) { g_costCalls++; return isEdge ? 1.0f : 2.0f; }
typedef float(__cdecl* CostFn)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
static CostFn g_cost;  // what the A* would load from [esp+0x60]
static int g_searchNodes;

// Like 0x676260: initial heuristic, then per expansion one edge cost; an edge below the wall generates a node (heuristic).
// Succeeds after 1000 nodes; once every edge is a wall the open list empties and it fails.
static u8 __fastcall FakeSearch(void*, void*, int*, int*, int*, DWORD, DWORD, u8*, int*, int*, DWORD, DWORD) {
    g_cost(0, 0, 0, 0, 0, 0, 0, 0);
    for (g_searchNodes = 0; g_searchNodes < 1000; g_searchNodes++) {
        if (g_cost(0, 0, 0, 0, 1, 0, 0, 0) >= 1e8f) return 0;
        assert(g_cost(0, 0, 0, 0, 0, 0, 0, 0) == 2.0f);
    }
    return 1;
}

static void TestRouteBudget() {
    u8 code[5][8];
    DWORD sites[5], fns[5];
    for (int i = 0; i < 5; i++) {
        fns[i] = (DWORD)(uintptr_t)FakeCost;
        sites[i] = (DWORD)(uintptr_t)code[i];
        const u8 mov[4] = {0xC7, 0x44, 0x24, 0x60};
        memcpy(code[i], mov, 4);
        memcpy(code[i] + 4, &fns[i], 4);
    }
    code[3][7] ^= 1;
    assert(!PatchCostSites(sites, fns) && *(DWORD*)(code[0] + 4) == fns[0]);  // one differs: nothing written
    code[3][7] ^= 1;
    assert(PatchCostSites(sites, fns));
    g_cost = *(CostFn*)(code[2] + 4);
    assert((DWORD)(uintptr_t)g_cost != fns[2]);

    QueryPerformanceFrequency(&g_qpf);
    ts3native_counters.workerReady = 0;
    ts3native_counters.budgetReady = 1;
    g_planOrig = FakeSearch;
    u8 opts[0x38] = {0};
    int list[2];
    const Ts3nCounters& c = ts3native_counters;

    ts3native_counters.budgetMode = 0;  // switch off: stubs pass through
    g_costCalls = 0;
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 1 && g_costCalls == 2001 && c.budgetCalls == 0);
    ts3native_counters.budgetMode = 1;
    g_maxNodes = 100000;  // above the search: same result, counted
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 1);
    assert(c.budgetCalls == 1 && c.budgetCapped == 0 && c.budgetMaxNodesOk == 1001 && c.budgetMaxNodes == 1001);
    g_maxNodes = 300;  // capped: the next edge is a wall, the search fails through its own exit
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 0);
    assert(g_searchNodes == 300 && c.budgetCapped == 1 && c.budgetCapNodes == 301 && c.budgetMaxNodesOk == 1001);
    assert(!t_budget.on);
    g_costCalls = 0;  // no budget outside a request: a search not started by Route_PlanLocked is untouched
    assert(FakeSearch(NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, 0, 0) == 1 && g_costCalls == 2001);
    g_onlyFlagged = 1;  // only routes with DebugPathPlan (hi word bit 0x1)
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 1 && c.budgetCalls == 2);
    opts[0x2C] = 1;
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 0 && c.budgetCapped == 2);
    g_onlyFlagged = 0;
    g_maxNodes = 0;
    g_maxUs = 5000;  // time cap: checked every 16 nodes
    g_planOrig = [](void*, void*, int*, int*, int*, DWORD, DWORD, u8*, int*, int*, DWORD, DWORD) -> u8 {
        g_cost(0, 0, 0, 0, 0, 0, 0, 0);
        for (;;) {
            if (g_cost(0, 0, 0, 0, 1, 0, 0, 0) >= 1e8f) return 0;
            g_cost(0, 0, 0, 0, 0, 0, 0, 0);
            Sleep(1);
        }
    };
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 0 && c.budgetCapped == 3 && c.budgetCapUs >= 5000);
    // clock starts at the first node: 30 ms waiting for the routing lock, then a quick search, is not capped,
    // but the request time in the counters still includes the wait
    g_planOrig = [](void* w, void* e, int* l, int* s, int* g, DWORD a, DWORD d, u8* o, int* o1, int* o2, DWORD gl, DWORD gh) -> u8 {
        Sleep(30);
        return FakeSearch(w, e, l, s, g, a, d, o, o1, o2, gl, gh);
    };
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 1 && c.budgetCapped == 3 && c.budgetMaxUsOk >= 30000);
    g_maxUs = 0;

    // per-thread state: a capped plan on another thread in the middle of ours neither ends nor feeds our budget
    g_maxNodes = 300;
    g_planOrig = [](void*, void*, int*, int*, int*, DWORD, DWORD, u8*, int*, int*, DWORD, DWORD) -> u8 {
        g_cost(0, 0, 0, 0, 0, 0, 0, 0);
        for (int i = 0; i < 1000; i++) {
            if (g_cost(0, 0, 0, 0, 1, 0, 0, 0) >= 1e8f) return 0;
            g_cost(0, 0, 0, 0, 0, 0, 0, 0);
            if (i == 200) {
                HANDLE t = CreateThread(NULL, 0, [](LPVOID) -> DWORD {
                    u8 o[0x38] = {0};
                    int l[2];
                    g_planOrig = FakeSearch;  // the other thread's own capped search
                    return CallPlan(NULL, NULL, l, NULL, NULL, 0, 1, o, NULL, NULL, 0, 0);
                }, NULL, 0, NULL);
                DWORD rc = 1;
                WaitForSingleObject(t, INFINITE);
                GetExitCodeThread(t, &rc);
                CloseHandle(t);
                assert(rc == 0 && t_budget.on && t_budget.nodes == 202 && !t_budget.capped);
            }
        }
        return 1;
    };
    const LONG capped0 = c.budgetCapped;
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 0 && c.budgetCapped - capped0 == 2);

    // on the route worker: the worker's own budget caps the plan, the caller's stays off
    ts3native_counters.workerReady = ts3native_counters.workerMode = 1;
    g_planOrig = FakeSearch;
    assert(RunPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 0 && c.budgetCapped - capped0 == 3);
    assert(g_searchNodes == 300 && !t_budget.on);
    ts3native_counters.workerReady = ts3native_counters.workerMode = 0;
    g_maxNodes = 0;
    ts3native_counters.budgetMode = 0;
    ts3native_counters.budgetReady = 0;
}

// --- async route plan: START/READY/CANCEL handshake, collect, drop, refuse, lock pump, plans queued behind a job ---
static CRITICAL_SECTION g_fakeLock;  // stands in for the routing lock world+0x918
static volatile LONG g_icHoldLock, g_icLockTaken, g_icInvoke, g_icSleepMs;
static LONG __fastcall FakeLockAcquire(void* lock, void*, const DWORD* deadline) {
    if (*deadline == 0xFFFFFFFF) { EnterCriticalSection((CRITICAL_SECTION*)lock); return 1; }
    return TryEnterCriticalSection((CRITICAL_SECTION*)lock) ? 1 : -2;
}
static u8 __cdecl FakeGetOption(DWORD, DWORD, DWORD, DWORD) { return 0; }  // the game: bit 58 is never set
static u8* __cdecl FakeIc(u8* out, DWORD handle) {
    memset(out, 0, 0x48);
    *(DWORD*)out = 1;  // Succeeded
    *(DWORD*)(out + 4) = handle;
    *(DWORD*)(out + 0x40) = GetCurrentThreadId();
    if (g_icHoldLock) { EnterCriticalSection(&g_fakeLock); g_icLockTaken = 1; }
    if (g_icInvoke) {  // the portal-lock query: runs on the owner even though it is not waiting for us
        g_icInvoke = 0;
        void* r = ((InvokeFn)(uintptr_t)g_fakeSlot)((void*)1, (void*)0x100, NULL, NULL);
        assert(r == (void*)0x101 && g_invokeTid != GetCurrentThreadId());
    }
    if (g_icSleepMs) Sleep(g_icSleepMs);
    if (g_icHoldLock) { g_icHoldLock = g_icLockTaken = 0; LeaveCriticalSection(&g_fakeLock); }
    return out;
}
static const DWORD kH1 = 0x25000001, kH2 = 0x25000002;
static u8 Start(DWORD h) { return GetOptionHook(h, 0, kAsyncOptHi, 0); }
static u8 Ready(DWORD h) { return GetOptionHook(h, 0, kAsyncOptHi, 1); }
static void Cancel(DWORD h) { assert(GetOptionHook(h, 0, kAsyncOptHi | kAsyncCancelHi, 1) == 1); }
static void WaitReady(DWORD h) { while (!Ready(h)) Sleep(1); }
static u8 NoYield(DWORD h) { return GetOptionHook(h, 0, kAsyncOptHi | kNoYieldHi, 1); }

// MonoScriptHost stand-in: +0xBC8 -> slot -> the task's root interpreter state, +0xBD0 task id; g_fakeCur = current state
static u8 g_fakeHost[0xC00];
static DWORD g_fakeHostPtr, g_fakeCur, g_fakeRootSlot;
static const DWORD kRoot = 0x1000, kNested = 0x2000;
static void FakeTask(DWORD cur, DWORD id) {
    g_fakeHostPtr = (DWORD)(uintptr_t)g_fakeHost;
    g_fakeRootSlot = kRoot;
    *(DWORD**)(g_fakeHost + 0xBC8) = &g_fakeRootSlot;
    *(DWORD*)(g_fakeHost + 0xBD0) = id;
    g_fakeCur = cur;
}

static void TestRouteAsync() {
    const DWORD me = GetCurrentThreadId();
    const Ts3nCounters& c = ts3native_counters;
    free(g_tab);
    g_tab = NULL;  // no route cache: the fake plans below have no real points
    InitializeCriticalSection(&g_fakeLock);
    g_icPlanOrig = FakeIc;
    g_getOptOrig = FakeGetOption;
    g_lockOrig = FakeLockAcquire;
    u8 out[0x48];
    const DWORD inf = 0xFFFFFFFF;
    g_hostPtr = &g_fakeHostPtr;
    g_curState = &g_fakeCur;
    FakeTask(kRoot, 7);  // the Sim's own task at its top level: may yield

    ts3native_counters.asyncMode = 0;  // switch off: START says no, the C# hook plans synchronously
    assert(!Start(kH1) && c.asyncStarted == 0);
    assert(GetOptionHook(kH1, 1, 0, 0) == 0);  // other options pass through
    ts3native_counters.asyncMode = 1;
    assert(!Start(kH1) && c.asyncStarted == 0);  // detours not all in place (only Route_GetOption, for NOYIELD): START says no
    ts3native_counters.asyncReady = 1;
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == me);  // no job: plain call

    // started -> not ready while running -> ready -> the next Route.Plan takes the worker's result
    g_icSleepMs = 30;
    assert(Start(kH1) && c.asyncStarted == 1);
    assert(!Ready(kH1));
    assert(Ready(kH2));  // no job for that route: "ready", nothing armed
    WaitReady(kH1);
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == g_workerTid && *(DWORD*)(out + 4) == kH1);
    assert(c.asyncCollected == 1 && g_async.state == A_IDLE);
    Cancel(kH1);

    // CANCEL after READY (destination went invalid, Plan returned before the internal call): the result is not handed to a later plan
    assert(Start(kH1));
    WaitReady(kH1);
    Cancel(kH1);
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == me && c.asyncDropped == 1 && c.asyncCollected == 1);

    // another route's START while a job runs is refused; a plain plan of the running route waits for it, then plans itself
    g_icSleepMs = 50;
    assert(Start(kH1));
    assert(!Start(kH2) && c.asyncRefused == 1);
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == me && c.asyncDropped == 2 && g_async.state == A_IDLE);

    // a result nobody collected (the Sim was reset while sleeping) is dropped by the next START
    g_icSleepMs = 0;
    assert(Start(kH1));
    while (g_async.state == A_RUNNING) Sleep(1);
    assert(Start(kH2) && c.asyncDropped == 3);
    WaitReady(kH2);
    assert(IcPlanHook(out, kH2) == out && *(DWORD*)(out + 0x40) == g_workerTid && c.asyncCollected == 2);

    // lock pump: the job holds the routing lock and makes a managed call; the owner, blocked on that lock, runs it meanwhile
    g_icHoldLock = g_icInvoke = 1;
    g_icSleepMs = 20;
    const LONG inv0 = c.invokes;
    assert(Start(kH1));
    while (!g_icLockTaken) Sleep(1);
    g_invokeTid = 0;
    assert(LockAcquireHook(&g_fakeLock, NULL, &inf) == 1 && g_invokeTid == me && c.invokes == inv0 + 1 && c.asyncPumpWaits == 1);
    LeaveCriticalSection(&g_fakeLock);
    assert(LockAcquireHook(&g_fakeLock, NULL, &inf) == 1);  // no job, or not contended: straight through
    LeaveCriticalSection(&g_fakeLock);
    WaitReady(kH1);
    assert(IcPlanHook(out, kH1) == out && c.asyncCollected == 3);

    // a plan of another route (Route_PlanLocked) while a job runs: waits for the job, running its managed call, then runs here
    ts3native_counters.workerReady = 1;
    ts3native_counters.workerMode = 0;
    g_planOrig = WorkerPlan;
    g_icInvoke = 1;
    g_icSleepMs = 30;
    u8 opts[0x38] = {0};
    int s[2] = {1, 2}, g[2] = {3, 4}, list[2];
    assert(Start(kH1));
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planTid == me);
    assert(g_async.state == A_DONE && g_invokeTid == me && c.invokes == inv0 + 2);
    Cancel(kH1);
    assert(Start(kH2) && c.asyncDropped == 4);  // the done kH1 result was never collected
    WaitReady(kH2);
    assert(IcPlanHook(out, kH2) == out);
    assert(c.watchdogs == 0 && c.asyncUs >= 30000 && c.asyncMaxUs >= 30000);
    ts3native_counters.workerReady = 0;
    g_icSleepMs = 0;

    // yield guard (interp.c:3595): only the task's root interpreter state may yield
    assert(NoYield(0) == 0 && NoYield(kH1) == 0);
    FakeTask(kNested, 7);  // inside a native->managed call during the task
    const LONG refused = c.asyncRefused, started = c.asyncStarted;
    assert(NoYield(0) == 1 && !Start(kH1) && c.asyncRefused == refused + 1 && c.asyncStarted == started && g_async.state == A_IDLE);
    FakeTask(kRoot, 0);  // no task running (stale slot)
    assert(NoYield(0) == 1 && !Start(kH1));
    FakeTask(0, 7);
    assert(NoYield(0) == 1);
    g_fakeHostPtr = 0;  // host not created yet
    assert(NoYield(0) == 1);
    FakeTask(kRoot, 7);
    assert(GetOptionHook(kH1, 0, kNoYieldHi, 1) == 0 && GetOptionHook(kH1, 1, kAsyncOptHi | kNoYieldHi, 1) == 0);  // not ours: passes through
}

// --- save write: the backup job against a fake native save on real temp folders ---
static wchar_t g_sv[MAX_PATH];         // <tmp>\User
static const wchar_t* g_svName;        // name the fake game passes to the backup step
static bool g_svTouch, g_svReachBackup;
static int g_svOrigBackups;
static DWORD __fastcall FakeInfo(void*, void*, u8*, int, int) { return 0; }
static DWORD __fastcall FakeWrite(void*, void*, void*, int) {
    Sleep(30);
    if (g_svTouch) {  // the old save changes while the worker copies it
        wchar_t p[MAX_PATH];
        swprintf_s(p, L"%s\\Saves\\Foo.sims3\\Meta.data", g_sv);
        HANDLE h = CreateFileW(p, FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
        DWORD n;
        WriteFile(h, "x", 1, &n, NULL);
        CloseHandle(h);
    }
    return 0;
}
static DWORD __fastcall FakeBackup(void*, void*, const wchar_t*) { g_svOrigBackups++; return 1; }
static DWORD __fastcall FakeCopy(void*, void*, const wchar_t*, int) { return 1; }
static DWORD __fastcall FakeFinal(void*, void*) { return 1; }
static DWORD __fastcall FakeGame(void*, void*, void* info, int travel, int) {  // FUN_00AB1030's order of steps
    if (SaveInfoHook(NULL, NULL, (u8*)info, 0, travel)) return 2;
    SaveWriteHook(NULL, NULL, NULL, 1);
    if (!travel && g_svReachBackup && SaveBackupHook(NULL, NULL, g_svName)) SaveCopyHook(NULL, NULL, g_svName, 0);
    SaveFinalHook(info, NULL);
    return 0;
}

static void SvFile(const wchar_t* rel, const char* text) {
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\%s", g_sv, rel);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    assert(h != INVALID_HANDLE_VALUE);
    DWORD n;
    WriteFile(h, text, (DWORD)strlen(text), &n, NULL);
    CloseHandle(h);
}
static bool SvExists(const wchar_t* rel) {
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\%s", g_sv, rel);
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}
static u64 SvHash(const wchar_t* rel) {
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\%s", g_sv, rel);
    u64 h, b;
    assert(TreeHashOf(p, &h, &b));
    return h;
}
static void SvReset() {  // old save with a world package, metadata and a sub folder; an older backup with other files
    RemoveTree(g_sv);
    wchar_t p[MAX_PATH];
    CreateDirectoryW(g_sv, NULL);
    const wchar_t* dirs[] = {L"Saves", L"Saves\\Foo.sims3", L"Saves\\Foo.sims3\\Sub", L"Saves\\Foo.sims3.backup"};
    for (const wchar_t* d : dirs) { swprintf_s(p, L"%s\\%s", g_sv, d); assert(CreateDirectoryW(p, NULL)); }
    SvFile(L"Saves\\Foo.sims3\\World_0x1.nhd", "world package bytes");
    SvFile(L"Saves\\Foo.sims3\\Meta.data", "meta");
    SvFile(L"Saves\\Foo.sims3\\Sub\\x.package", "sub");
    SvFile(L"Saves\\Foo.sims3.backup\\Old.nhd", "older save");
}

static void TestSaveWrite() {
    // PatchCalls: a site that is not a call to the expected target -> nothing written; a matching one -> rel32 rewritten.
    u8* code = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    const DWORD site = (DWORD)(uintptr_t)code, target = site + 0x20;
    code[0] = 0xE8;
    *(LONG*)(code + 1) = 0x20 - 5;
    CallSite wrong[] = {{site, target + 1, (void*)Hook}}, right[] = {{site, target, (void*)Hook}};
    assert(!PatchCalls("wrong", wrong, 1) && *(LONG*)(code + 1) == 0x20 - 5);
    assert(PatchCalls("right", right, 1) && site + 5 + *(LONG*)(code + 1) == (DWORD)(uintptr_t)Hook);

    g_saveGameOrig = FakeGame; g_saveInfoOrig = FakeInfo; g_saveWriteOrig = FakeWrite;
    g_saveBackupOrig = FakeBackup; g_saveCopyOrig = FakeCopy; g_saveFinalOrig = FakeFinal;
    QueryPerformanceFrequency(&g_qpf);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf_s(g_sv, L"%sts3n-selftest-%lu", tmp, GetCurrentProcessId());
    static u8 info[0x1000];
    swprintf_s((wchar_t*)(info + kInfoNhdPath), 520, L"%s\\Saves\\Foo.sims3\\World_0x1.nhd", g_sv);
    Ts3nCounters& c = ts3native_counters;

    // 1. mode 1: the worker copy becomes the backup, the game's backup copy is skipped, no temp copy left.
    SvReset();
    const u64 old = SvHash(L"Saves\\Foo.sims3");
    c.saveMode = 1; g_svName = L"Foo"; g_svTouch = false; g_svReachBackup = true; g_svOrigBackups = 0;
    assert(SaveGameHook(NULL, NULL, info, 0, 0) == 0);
    assert(g_svOrigBackups == 0 && c.saveJobs == 1 && c.savePromoted == 1 && c.saveFallbacks == 0 && c.saveCount == 1);
    assert(SvHash(L"Saves\\Foo.sims3.backup") == old && !SvExists(L"Saves\\Foo.sims3.backup\\Old.nhd"));
    assert(!SvExists(L"ts3native-backup.tmp") && c.saveLastWriteUs >= 20000);

    // 2. the name with ".sims3" is the same folder.
    SvReset(); g_svName = L"Foo.sims3";
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(g_svOrigBackups == 0 && c.savePromoted == 2);

    // 3. old save changed during the write -> copy dropped, the game backs up.
    SvReset(); g_svName = L"Foo"; g_svTouch = true;
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(g_svOrigBackups == 1 && c.saveFallbacks == 1 && c.savePromoted == 2);
    assert(SvExists(L"Saves\\Foo.sims3.backup\\Old.nhd") && !SvExists(L"ts3native-backup.tmp"));  // ours never touched it
    g_svTouch = false;

    // 4. another folder name at the backup step -> fallback.
    SvReset(); g_svName = L"Bar";
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(g_svOrigBackups == 2 && c.saveFallbacks == 2 && SvExists(L"Saves\\Foo.sims3.backup\\Old.nhd"));

    // 5. save fails before the backup step: the job is waited for and its copy removed; backup untouched.
    SvReset(); g_svName = L"Foo"; g_svReachBackup = false;
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(c.saveJobs == 5 && !SvExists(L"ts3native-backup.tmp") && SvExists(L"Saves\\Foo.sims3.backup\\Old.nhd"));
    g_svReachBackup = true;

    // 6. travel save, mode 0 and a new save (no world package yet) start no job.
    SvReset();
    SaveGameHook(NULL, NULL, info, 1, 1);
    c.saveMode = 0;
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(c.saveJobs == 5 && g_svOrigBackups == 3);
    c.saveMode = 1;
    SvReset();
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\Saves\\Foo.sims3\\World_0x1.nhd", g_sv);
    DeleteFileW(p);
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(c.saveJobs == 5 && g_svOrigBackups == 4 && c.saveCount == 8);
    assert(RemoveTree(g_sv));
}

int main() {
    // PatchImm32: all sites match -> written; one differs -> nothing written.
    static u8 buf[16] = {0x3D, 0x00, 0x08, 0x00, 0x00, 0x76, 0x2C, 0xB8, 0x00, 0x08, 0x00, 0x00, 0x50};
    Site ok[] = {{(DWORD)(uintptr_t)buf, {0x3D, 0x00, 0x08, 0x00, 0x00, 0x76, 0x2C}, 7, 1},
                 {(DWORD)(uintptr_t)(buf + 7), {0xB8, 0x00, 0x08, 0x00, 0x00, 0x50}, 6, 1}};
    Site bad[] = {ok[0], {(DWORD)(uintptr_t)(buf + 7), {0xB8, 0x00, 0x09, 0x00, 0x00, 0x50}, 6, 1}};
    assert(!PatchImm32("bad", bad, 2, 0x2000));
    assert(buf[2] == 0x08 && buf[9] == 0x08);
    assert(PatchImm32("ok", ok, 2, 0x2000));
    assert(buf[2] == 0x20 && buf[9] == 0x20 && buf[5] == 0x76);

    // InstallDetour on a tiny function: mov eax,42 ; ret
    u8* fn = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    const u8 code[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    memcpy(fn, code, sizeof(code));
    assert(!InstallDetour("bad", (DWORD)(uintptr_t)fn, (const u8*)"\xB8\x2B\x00\x00\x00", 5, (void*)Hook));
    void* tramp = InstallDetour("ok", (DWORD)(uintptr_t)fn, code, 5, (void*)Hook);
    assert(tramp);
    assert(((int(__cdecl*)())fn)() == 7);
    assert(((int(__cdecl*)())tramp)() == 42);
    TestRouteCache();
    TestRouteWorker();
    TestRouteBudget();
    TestRouteAsync();
    TestSaveWrite();
    puts("selftest ok");
    return 0;
}
