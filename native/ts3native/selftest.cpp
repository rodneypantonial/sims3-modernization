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

// Host delegate call 0xD75FA0 stand-in: records its thread and that the task id pair was cleared around it
static DWORD g_hostTid, g_hostIdSeen;
static void* __fastcall FakeHostCall(void* self, void*, void* out, void* a2, void* a3, void* a4) {
    g_hostTid = GetCurrentThreadId();
    g_hostIdSeen = *(DWORD*)(g_fakeHost + 0xBD0) | *(DWORD*)(g_fakeHost + 0xBD4);
    *(DWORD*)out = (DWORD)(uintptr_t)self + (DWORD)(uintptr_t)a2 + (DWORD)(uintptr_t)a3 + (DWORD)(uintptr_t)a4;
    return out;
}
static int g_callHost;  // WorkerPlan makes a host delegate call (through the vtable in the game)
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
    if (g_callHost) {  // the whole host call runs on the waiting caller, with the task id pair cleared there, then restored
        g_callHost = 0;
        DWORD out = 0;
        assert(HostCallHook((void*)0x10, NULL, &out, (void*)1, (void*)2, (void*)3) == &out && out == 0x16);
        assert(g_hostTid != GetCurrentThreadId() && g_hostIdSeen == 0);
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
    g_hostPtr = &g_fakeHostPtr;
    FakeTask(kRoot, 7);
    g_hostCallOrig = FakeHostCall;

    ts3native_counters.workerMode = 0;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planTid == me);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 0, 0) == 0);
    assert(ts3native_counters.planOffCalls - c0.planOffCalls == 2 && ts3native_counters.planOffFails - c0.planOffFails == 1);
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
    // host delegate call on the worker: runs on the caller; the caller's task id pair is back afterwards
    g_callHost = 1;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_hostTid == me && !g_callHost);
    assert(*(DWORD**)(g_fakeHost + 0xBC8) == &g_fakeRootSlot && *(DWORD*)(g_fakeHost + 0xBD0) == 7 && ts3native_counters.hostCalls == 1);
    DWORD hostOut = 0;  // other threads: pass-through
    assert(HostCallHook((void*)0x10, NULL, &hostOut, NULL, NULL, NULL) == &hostOut && hostOut == 0x10 && g_hostTid == me);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 0, opts, NULL, NULL, 1, 0) == 1 && g_planTid == me);  // doPlan=0 stays
    const Ts3nCounters& c = ts3native_counters;
    assert(c.workerCalls - c0.workerCalls == 7 && c.fallbacks - c0.fallbacks == 1 && c.watchdogs == 0 && c.invokes == 2);
    assert(c.planOnCalls - c0.planOnCalls == 9 && c.planOnFails - c0.planOnFails == 1);  // nested plans count too
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
    Cancel(kH1);  // drops the done kH1 result nobody collected
    assert(c.asyncDropped == 4 && g_async.state == A_IDLE);
    assert(Start(kH2) && c.asyncDropped == 4);
    WaitReady(kH2);
    assert(IcPlanHook(out, kH2) == out);
    assert(c.watchdogs == 0 && c.asyncUs >= 30000 && c.asyncMaxUs >= 30000);
    ts3native_counters.workerReady = 0;

    // CANCEL while the job runs (Sim reset): waits for it, drops it; no orphan job, the next START is not refused
    g_icSleepMs = 40;
    assert(Start(kH1));
    Cancel(kH2);  // another route's CANCEL leaves it alone
    assert(g_async.state == A_RUNNING);
    Cancel(kH1);
    assert(g_async.state == A_IDLE && c.asyncDropped == 5);
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == me && c.asyncDropped == 5);

    // mode switched off mid-job: the owner's next entry (READY here) drains and drops it, Route.Plan then plans inline
    assert(Start(kH1));
    ts3native_counters.asyncMode = 0;
    assert(Ready(kH1) && g_async.state == A_IDLE && c.asyncDropped == 6);
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == me);
    ts3native_counters.asyncMode = 1;
    assert(Start(kH1));
    ts3native_counters.asyncMode = 0;  // ... or the internal call itself
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == me && g_async.state == A_IDLE && c.asyncDropped == 7);
    ts3native_counters.asyncMode = 1;

    // lock pump on a finite deadline: managed call served, lock taken; a deadline that passes ends with the original's -2
    g_icHoldLock = g_icInvoke = 1;
    g_icSleepMs = 20;
    const LONG inv1 = c.invokes, waits1 = c.asyncPumpWaits;
    assert(Start(kH1));
    while (!g_icLockTaken) Sleep(1);
    g_invokeTid = 0;
    const DWORD later = GetTickCount() + 5000;
    assert(LockAcquireHook(&g_fakeLock, NULL, &later) == 1 && g_invokeTid == me && c.invokes == inv1 + 1 && c.asyncPumpWaits == waits1 + 1);
    LeaveCriticalSection(&g_fakeLock);
    WaitReady(kH1);
    assert(IcPlanHook(out, kH1) == out);
    g_icHoldLock = 1;
    g_icSleepMs = 300;
    assert(Start(kH1));
    while (!g_icLockTaken) Sleep(1);
    const DWORD soon = GetTickCount() + 30, t0 = GetTickCount();
    assert(LockAcquireHook(&g_fakeLock, NULL, &soon) == -2 && GetTickCount() - t0 < 250 && g_async.state == A_RUNNING);
    WaitReady(kH1);
    assert(IcPlanHook(out, kH1) == out);
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

// --- decompress (phase 6): random valid RefPack streams with every command type, then corrupted ones ---
static DWORD g_rnd = 12345;
static DWORD Rnd(DWORD n) { g_rnd = g_rnd * 1103515245 + 12345; return (g_rnd >> 8) % n; }

// Writes a stream that decodes to `size` bytes (also written to `plain`); returns the stream length.
static DWORD GenRefpack(u8* out, u8* plain, DWORD size) {
    u8* o = out;
    *o++ = 0x10; *o++ = 0xFB;
    *o++ = (u8)(size >> 16); *o++ = (u8)(size >> 8); *o++ = (u8)size;
    DWORD done = 0;
    const DWORD alpha = 2 + Rnd(200);  // small alphabets give long matches
    for (;;) {
        const DWORD left = size - done;
        if (left < 4 && Rnd(2)) {  // stop code with the 0-3 tail bytes
            *o++ = (u8)(0xFC | left);
            for (DWORD i = 0; i < left; i++) *o++ = plain[done++] = (u8)Rnd(alpha);
            return (DWORD)(o - out);
        }
        const DWORD lit = Rnd(4) < left ? Rnd(4) : 0, kind = Rnd(4);
        const DWORD avail = done + lit;  // history the match may reach back into
        DWORD n = 0, off = 0, maxN = 0, maxOff = 0;
        if (kind == 0) { maxN = 10; maxOff = 1024; n = 3 + Rnd(8); }
        if (kind == 1) { maxN = 67; maxOff = 16384; n = 4 + Rnd(64); }
        if (kind == 2) { maxN = 1028; maxOff = 131072; n = 5 + Rnd(Rnd(2) ? 1024 : 40); }
        if (maxN && avail && n <= maxN && lit + n <= left) {
            off = 1 + Rnd(avail < maxOff ? avail : maxOff);
            if (Rnd(3) == 0) off = 1 + Rnd(avail < 20 ? avail : 20);  // overlapping copies
            const DWORD o1 = off - 1;
            if (kind == 0) { *o++ = (u8)(((o1 >> 3) & 0x60) | ((n - 3) << 2) | lit); *o++ = (u8)o1; }
            if (kind == 1) { *o++ = (u8)(0x80 | (n - 4)); *o++ = (u8)((lit << 6) | (o1 >> 8)); *o++ = (u8)o1; }
            if (kind == 2) { *o++ = (u8)(0xC0 | (((o1 >> 16) & 1) << 4) | (((n - 5) >> 8) << 2) | lit); *o++ = (u8)(o1 >> 8); *o++ = (u8)o1; *o++ = (u8)(n - 5); }
            for (DWORD i = 0; i < lit; i++) *o++ = plain[done++] = (u8)Rnd(alpha);
            for (DWORD i = 0; i < n; i++, done++) plain[done] = plain[done - off];
        } else if (left >= 4) {
            DWORD k = 1 + Rnd(28);
            if (k * 4 > left) k = left / 4;
            *o++ = (u8)(0xE0 | (k - 1));
            for (DWORD i = 0; i < k * 4; i++) *o++ = plain[done++] = (u8)Rnd(alpha);
        }
    }
}

static int __cdecl FakeCore(u8*, DWORD, const u8*, DWORD) { return -2; }

static void TestDecompress() {
    const DWORD kMax = 300000, kGuard = 64;
    u8* plain = (u8*)malloc(kMax);
    u8* stream = (u8*)malloc(kMax * 2 + 64);
    u8* a = (u8*)malloc(kMax + kGuard);
    u8* b = (u8*)malloc(kMax + kGuard);
    u8* c = (u8*)malloc(kMax + kGuard);
    for (int iter = 0; iter < 3000; iter++) {
        const DWORD size = iter < 50 ? (DWORD)iter : (Rnd(4) ? Rnd(5000) : Rnd(kMax));
        DWORD len = GenRefpack(stream, plain, size);
        memset(a, 0xAA, kMax + kGuard);
        memset(b, 0xAA, kMax + kGuard);
        assert(RefpackRef(a, size, stream, len) == (int)size && memcmp(a, plain, size) == 0);
        assert(RefpackFast(b, size, stream, len) == (int)size && memcmp(b, plain, size) == 0);
        assert(b[size] == 0xAA);
        // Corrupt: flip bytes, cut the stream, shrink or grow the cap. Same result; same bytes on success; nothing past cap.
        for (int k = 0; k < 4; k++) {
            DWORD clen = len;
            const DWORD flips = Rnd(4);
            for (DWORD f = 0; f < flips && len > 2; f++) stream[2 + Rnd(len - 2)] ^= (u8)(1 + Rnd(255));
            if (Rnd(3) == 0) clen = Rnd(len + 1);
            DWORD cap = Rnd(3) == 0 ? Rnd(size + 1) : Rnd(2) ? size : (size + Rnd(64) < kMax ? size + Rnd(64) : size);
            memset(a, 0xAA, kMax + kGuard);
            memset(b, 0xAA, kMax + kGuard);
            memset(c, 0x55, kMax + kGuard);
            const int ra = RefpackRef(a, cap, stream, clen), rb = RefpackFast(b, cap, stream, clen);
            assert(ra == rb && RefpackRef(c, cap, stream, clen) == ra);
            // Every byte the game's decoder wrote (same in a and c despite different fills) is the same in b. Bytes it
            // left alone may hold scratch from a wild copy, only when the stream ends short of its header size.
            if (ra > 0)
                for (DWORD i = 0; i < cap; i++) assert(a[i] != c[i] || b[i] == a[i]);
            for (DWORD g = cap; g < cap + kGuard; g++) assert(b[g] == 0xAA);
            len = GenRefpack(stream, plain, size);  // a fresh valid stream for the next round
        }
    }
    free(plain); free(stream); free(a); free(b); free(c);

    // The hook site: `call core` rel32 is retargeted only when the 5 bytes match.
    u8* site = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    const DWORD at = (DWORD)(uintptr_t)site, core = (DWORD)(uintptr_t)FakeCore;
    site[0] = 0xE8;
    *(LONG*)(site + 1) = (LONG)(core - (at + 5));
    assert(!PatchRefpackCall(at, core + 1, (void*)RefpackHook));
    assert(*(LONG*)(site + 1) == (LONG)(core - (at + 5)));
    assert(PatchRefpackCall(at, core, (void*)RefpackHook));
    assert(at + 5 + *(LONG*)(site + 1) == (DWORD)(uintptr_t)RefpackHook);
    // Hook: mode 0 runs the original, mode 1 the fast decoder, verify keeps the original's result on a mismatch.
    g_refpackOrig = FakeCore;
    QueryPerformanceFrequency(&g_dqpf);
    g_dstat.mainTid = (LONG)GetCurrentThreadId();
    u8 src[] = {0x10, 0xFB, 0x00, 0x00, 0x04, 0xFC | 0, 0};
    u8 hello[] = {0x10, 0xFB, 0x00, 0x00, 0x04, 0xE0, 'a', 'b', 'c', 'd', 0xFC};
    u8 out[8];
    ts3native_counters.decompressMode = 0;
    assert(RefpackHook(out, 4, src, 6) == -2);
    ts3native_counters.decompressMode = 1;
    assert(RefpackHook(out, 4, hello, sizeof(hello)) == 4 && memcmp(out, "abcd", 4) == 0);
    ts3native_counters.decompressVerify = 1;
    assert(RefpackHook(out, 4, hello, sizeof(hello)) == -2 && ts3native_counters.decompressMismatches == 1);
    g_refpackOrig = RefpackRef;
    assert(RefpackHook(out, 4, hello, sizeof(hello)) == 4 && ts3native_counters.decompressMismatches == 1);
    assert(ts3native_counters.decompressCalls == 4 && ts3native_counters.decompressMainCalls == 4 && g_dstat.tid[0] == (LONG)GetCurrentThreadId());
    ts3native_counters.decompressVerify = 0;
}

// --- EA allocator cache (phase 8): a fake heap that lays chunks out like the game's (16-byte header, in-use bit in the next chunk) ---
static __declspec(align(16)) u8 g_fakeArena[1 << 18];
static DWORD g_fakeTop = 0x10;
static int g_fakeMallocs, g_fakeFrees, g_fakeOom;
static void* __fastcall FakeEaMalloc(void*, void*, DWORD n, DWORD) {
    g_fakeMallocs++;
    if (g_fakeOom) { g_fakeOom--; return NULL; }
    const DWORD sz = n >= 0x1000 ? 0x1000 : AcChunk(n);
    if (g_fakeTop + sz + 0x20 > sizeof(g_fakeArena)) g_fakeTop = 0x10;
    u8* c = g_fakeArena + g_fakeTop;
    g_fakeTop += sz;
    *(DWORD*)(c + 4) = sz | 1;
    *(DWORD*)(c + sz + 4) = 1;  // next chunk: previous (this one) in use
    return c + 0x10;
}
static int g_fakeAligns;
static void* __fastcall FakeEaMemalign(void* h, void* e, DWORD n, DWORD, DWORD, DWORD f) { g_fakeAligns++; return FakeEaMalloc(h, e, n, f); }
static void __fastcall FakeEaFree(void*, void*, void*) { g_fakeFrees++; }
static DWORD __fastcall FakeEaShutdown(void*, void*) { return 1; }
static CRITICAL_SECTION g_fakeCs;
static void* __fastcall LockedFakeMalloc(void* h, void* e, DWORD n, DWORD f) { EnterCriticalSection(&g_fakeCs); void* r = FakeEaMalloc(h, e, n, f); LeaveCriticalSection(&g_fakeCs); return r; }
static void __fastcall LockedFakeFree(void* h, void* e, void* p) { EnterCriticalSection(&g_fakeCs); FakeEaFree(h, e, p); LeaveCriticalSection(&g_fakeCs); }
static void* const kHeapA = (void*)0x1000;
static void* const kHeapB = (void*)0x2000;
static DWORD WINAPI AcFreeThree(void*) {
    void* p[3];
    for (int i = 0; i < 3; i++) p[i] = EaMallocHook(kHeapA, NULL, 100, 0);
    for (int i = 0; i < 3; i++) EaFreeHook(kHeapA, NULL, p[i]);
    return 0;
}

static void TestAlloc() {
    Ts3nCounters& c = ts3native_counters;
    g_eaMallocOrig = FakeEaMalloc;
    g_eaMemalignOrig = FakeEaMemalign;
    g_eaFreeOrig = FakeEaFree;
    g_eaShutdownOrig = FakeEaShutdown;
    assert(AcInit(16, 1, 4));
    c.allocMode = 1;
    assert(AcChunk(0) == 16 && AcChunk(4) == 16 && AcChunk(5) == 32 && AcChunk(40) == 64 && AcChunk(36) == 48 && AcChunk(1000) == 1024);

    // free then malloc of the same chunk size: same pointer, no heap call; header and user bytes untouched
    void* p = EaMallocHook(kHeapA, NULL, 40, 0);
    assert(g_fakeMallocs == 1 && ((uintptr_t)p & 15) == 0);
    memset(p, 0xAB, 40);
    const DWORD hdr = ((DWORD*)p)[-3];
    EaFreeHook(kHeapA, NULL, p);
    assert(g_fakeFrees == 0 && ((u8*)p)[0] == 0xAB && ((u8*)p)[39] == 0xAB && ((DWORD*)p)[-3] == hdr);
    assert(EaMallocHook(kHeapA, NULL, 36, 0) != p && g_fakeMallocs == 2);  // 48-byte chunk: another bin
    assert(EaMallocHook(kHeapB, NULL, 40, 0) != p && g_fakeMallocs == 3);  // other heap
    assert(EaMallocHook(kHeapA, NULL, 37, 8) != p && g_fakeMallocs == 4);  // flag 8: direct VirtualAlloc path
    assert(EaMallocHook(kHeapA, NULL, 37, 0) == p && g_fakeMallocs == 4);  // 37 -> 64 too

    // memalign: only align <= 16 with offset 0 (0x4E63F0's plain 0x4E5E40 path) may come from the cache
    EaFreeHook(kHeapA, NULL, p);
    assert(EaMemalignHook(kHeapA, NULL, 40, 32, 0, 0) != p && g_fakeAligns == 1);
    assert(EaMemalignHook(kHeapA, NULL, 40, 4, 16, 0) != p && g_fakeAligns == 2);  // ZoneObject: align 4, offset 16
    assert(EaMemalignHook(kHeapA, NULL, 40, 16, 0, 0) == p && g_fakeAligns == 2);

    // not cached: direct-mapped, fastbin-tagged, too big, NULL, next chunk says free
    u8* q = (u8*)EaMallocHook(kHeapA, NULL, 100, 0);
    const DWORD qh = ((DWORD*)q)[-3];
    ((DWORD*)q)[-3] = qh | 2;          EaFreeHook(kHeapA, NULL, q); assert(g_fakeFrees == 1);
    ((DWORD*)q)[-3] = qh | 0x80000004; EaFreeHook(kHeapA, NULL, q); assert(g_fakeFrees == 2);
    ((DWORD*)q)[-3] = qh;
    DWORD* next = (DWORD*)(q - 0x10 + (qh & ~15u) + 4);
    *next &= ~1u;                      EaFreeHook(kHeapA, NULL, q); assert(g_fakeFrees == 3);
    *next |= 1;
    EaFreeHook(kHeapA, NULL, EaMallocHook(kHeapA, NULL, 1013, 0)); assert(g_fakeFrees == 4);  // 1040-byte chunk
    EaFreeHook(kHeapA, NULL, NULL);    assert(g_fakeFrees == 5);

    // per-bin cap 4, then the 16 KB thread cap
    void* v[24];
    for (int i = 0; i < 5; i++) v[i] = EaMallocHook(kHeapA, NULL, 200, 0);
    for (int i = 0; i < 5; i++) EaFreeHook(kHeapA, NULL, v[i]);
    assert(g_fakeFrees == 6);
    AcThread* t = (AcThread*)FlsGetValue(g_acFls);
    assert(t && t->bytes == 4 * 224);
    for (int i = 0; i < 24; i++) v[i] = EaMallocHook(kHeapA, NULL, 1000 - 16 * (i % 6), 0);
    for (int i = 0; i < 24; i++) EaFreeHook(kHeapA, NULL, v[i]);
    assert(t->bytes <= 16 * 1024 && g_fakeFrees > 6);

    // mode 0: straight through
    c.allocMode = 0;
    int m = g_fakeMallocs, f = g_fakeFrees;
    void* z = EaMallocHook(kHeapA, NULL, 200, 0);
    EaFreeHook(kHeapA, NULL, z);
    assert(g_fakeMallocs == m + 1 && g_fakeFrees == f + 1);
    c.allocMode = 1;

    // heap A shuts down: its chunks are forgotten (not freed), heap B's go back to B
    void* b1 = EaMallocHook(kHeapB, NULL, 60, 0);
    EaFreeHook(kHeapB, NULL, b1);
    f = g_fakeFrees;
    const LONG dropped = c.allocDropped, flushed = c.allocFlushed;
    EaShutdownHook(kHeapA, NULL);
    assert(g_fakeFrees == f + 1 && c.allocFlushed == flushed + 1 && c.allocDropped > dropped && t->bytes == 0 && c.allocShutdowns == 1);

    // out of memory: this thread's chunks of the heap go back, then one retry
    b1 = EaMallocHook(kHeapB, NULL, 60, 0);
    EaFreeHook(kHeapB, NULL, b1);
    f = g_fakeFrees;
    m = g_fakeMallocs;
    g_fakeOom = 1;
    assert(EaMallocHook(kHeapB, NULL, 500, 0) != NULL);
    assert(g_fakeMallocs == m + 2 && g_fakeFrees == f + 1 && c.allocOomRetries == 1);

    // thread exit returns its chunks and its quota
    f = g_fakeFrees;
    const LONG threads = g_acThreads;
    HANDLE th = CreateThread(NULL, 0, AcFreeThree, NULL, 0, NULL);
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    assert(g_fakeFrees == f + 3 && g_acThreads == threads);

    // quota: 1 MB / 16 KB = 64 threads; the 65th runs uncached
    g_acThreadsMax = g_acThreads;
    f = g_fakeFrees;
    th = CreateThread(NULL, 0, AcFreeThree, NULL, 0, NULL);
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    assert(g_fakeFrees == f + 3 && g_acThreads == threads);
    g_acThreadsMax = 64;

    // benchmark: one free + malloc pair through the cache vs the fake heap behind a CRITICAL_SECTION (uncontended)
    InitializeCriticalSectionAndSpinCount(&g_fakeCs, 10);
    LARGE_INTEGER t0, t1, t2, fq;
    QueryPerformanceFrequency(&fq);
    const int N = 2000000;
    void* r = EaMallocHook(kHeapB, NULL, 100, 0);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < N; i++) { EaFreeHook(kHeapB, NULL, r); r = EaMallocHook(kHeapB, NULL, 100, 0); }
    QueryPerformanceCounter(&t1);
    g_fakeTop = 0x10;
    for (int i = 0; i < N; i++) { LockedFakeFree(kHeapB, NULL, r); r = LockedFakeMalloc(kHeapB, NULL, 100, 0); if (g_fakeTop > 0x30000) g_fakeTop = 0x10; }
    QueryPerformanceCounter(&t2);
    printf("alloc cache: %.1f ns per free+malloc hit, %.1f ns for lock + bump allocator\n", (t1.QuadPart - t0.QuadPart) * 1e9 / fq.QuadPart / N,
           (t2.QuadPart - t1.QuadPart) * 1e9 / fq.QuadPart / N);
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
    TestDecompress();
    TestAlloc();
    puts("selftest ok");
    return 0;
}
