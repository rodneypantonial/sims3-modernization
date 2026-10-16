// Self-check for the patch helpers on local memory (no game needed). Built and run by build.cmd.
#define TS3NATIVE_NO_DLLMAIN
#include "ts3native.cpp"
#include <assert.h>
#include "savecompress-fake.h"

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
static int g_svFlushes, g_svLoadCopies;  // the game's flush FUN_00AAF4B0 ran / the load copy ran
static void __cdecl FakeFlush(const wchar_t*) { g_svFlushes++; }
static void SvFlush() {  // steps 9 and 11 flush the new save folder
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\Saves\\Foo.sims3", g_sv);
    SaveFlushHook(p);
}
static DWORD __fastcall FakeCopy(void*, void*, const wchar_t*, int) { SvFlush(); return 1; }
static DWORD __fastcall FakeFinal(void*, void*) { SvFlush(); return 1; }
static DWORD __fastcall FakeLoadCopy(void*, void*, const wchar_t*, int) { g_svLoadCopies++; return 1; }
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
    g_saveFlushOrig = FakeFlush; g_loadCopyOrig = FakeLoadCopy;
    QueryPerformanceFrequency(&g_qpf);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf_s(g_sv, L"%sts3n-selftest-%lu", tmp, GetCurrentProcessId());
    // own page: a static buffer can end at a section edge, where Readable() over the 520-char path fails
    static u8* const info = (u8*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
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

// --- save flush (phase 10): steps 9 and 11 flushes recorded during the save, replayed by a worker after it ---
static void TestSaveFlush() {
    Ts3nCounters& c = ts3native_counters;
    // own page: a static buffer can end at a section edge, where Readable() over the 520-char path fails
    static u8* const info = (u8*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    swprintf_s((wchar_t*)(info + kInfoNhdPath), 520, L"%s\\Saves\\Foo.sims3\\World_0x1.nhd", g_sv);
    c.saveMode = 0;
    g_svName = L"Foo"; g_svTouch = false; g_svReachBackup = true;

    // 1. not ready (no [SaveFlush]): the game flushes during the save, as before.
    SvReset();
    g_svFlushes = 0;
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(g_svFlushes == 2 && c.flushDeferred == 0 && c.flushJobs == 0 && !g_fj.thread);

    // 2. mode 1: both flushes recorded, none by the game during the save; the worker flushes the 3 files twice.
    c.flushReady = 1; c.flushMode = 1;
    g_svFlushes = 0;
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(g_svFlushes == 0 && c.flushDeferred == 2 && c.flushJobs == 1 && g_fj.thread);
    WaitForSingleObject(g_fj.thread, INFINITE);
    assert(g_fj.files == 6 && g_fj.fails == 0 && c.flushFails == 0);

    // 3. the next save waits for the worker first (here already done: no wait counted), then defers again.
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(c.flushDeferred == 4 && c.flushJobs == 2 && c.flushWaits == 0);

    // 4. the load copy waits for the worker before it runs.
    g_svLoadCopies = 0;
    assert(LoadCopyHook(NULL, NULL, L"Foo", 1) == 1 && g_svLoadCopies == 1 && !g_fj.thread);

    // 5. a flushed folder that is gone by then counts as a fail; mode 0 lets the game flush again.
    SaveGameHook(NULL, NULL, info, 0, 0);
    WaitForSingleObject(g_fj.thread, INFINITE);
    assert(g_fj.fails == 0);
    g_fj.n = 1;
    swprintf_s(g_fj.path[0], L"%s\\gone", g_sv);
    FlushMain(NULL);
    assert(g_fj.fails == 1 && c.flushFails == 1);
    c.flushMode = 0;
    g_svFlushes = 0;
    SaveGameHook(NULL, NULL, info, 0, 0);
    assert(g_svFlushes == 2 && c.flushDeferred == 6 && !g_fj.thread);
    c.flushReady = 0; c.saveMode = 1;
    assert(RemoveTree(g_sv));
}

// --- save compress (phase 10): a fake package commit with a fake compressor; output identical with and without workers ---
static LONG g_fkSlotCalls;  // the game's slot ran (bound, miss or verify)
static u8 g_fkSalt;         // != 0: the workers' compressor gives other bytes (verify test)
typedef void*(__fastcall* FkAllocFn)(void*, void*, DWORD, const char*, DWORD);
typedef void(__fastcall* FkFreeFn)(void*, void*, void*, DWORD);
static int FkCompressImpl(u8* dst, const u8* src, DWORD len, void* alloc, DWORD mode, u8 salt) {
    void** vt = *(void***)alloc;  // takes its scratch tables like 0x4EC0A0 does, and writes them
    void* h = len > 0x4000 ? ((FkAllocFn)vt[2])(alloc, NULL, 0x40000, "h", 0) : NULL;
    void* t = ((FkAllocFn)vt[2])(alloc, NULL, ScWindowBytes(mode), "t", 0);
    assert(t && (len <= 0x4000 || h));
    memset(t, 0x5A, ScWindowBytes(mode));
    if (h) memset(h, 0xA5, 0x40000);
    const int n = 2 + (int)(len / 3) + (int)mode;
    if (dst) {
        dst[0] = (u8)(0x10 | mode); dst[1] = 0xFB;
        for (int i = 0; i < n - 2; i++) dst[2 + i] = (u8)(src[((DWORD)i * 3) % len] ^ (u8)i ^ salt);
    }
    if (h) ((FkFreeFn)vt[3])(alloc, NULL, h, 0);
    ((FkFreeFn)vt[3])(alloc, NULL, t, 0);
    return n;
}
static int __cdecl FkCompressWorker(u8* dst, DWORD, const u8* src, DWORD len, void* alloc, DWORD mode) {
    return FkCompressImpl(dst, src, len, alloc, mode, g_fkSalt);
}
static int __fastcall FkSlot(void* codec, void*, const u8* src, DWORD len, u8* dst, DWORD, DWORD flags) {  // as 0x4EC200
    InterlockedIncrement(&g_fkSlotCalls);
    if (!dst && (flags & 1)) return (int)((len * 20) >> 4) + 0x20;
    return FkCompressImpl(dst, src, len, ((FkCodec*)codec)->alloc, ScModeOf(flags), 0);
}
static void* __fastcall FkHeapAlloc(void*, void*, DWORD n, const char*, DWORD) { return HeapAlloc(GetProcessHeap(), 0, n); }
static void __fastcall FkHeapFree(void*, void*, void* p, DWORD) { HeapFree(GetProcessHeap(), 0, p); }
static void* g_fkHeapVt[4] = {NULL, NULL, (void*)FkHeapAlloc, (void*)FkHeapFree};
static void* g_fkHeap = g_fkHeapVt;

static DWORD g_fkRnd = 4711;
static DWORD FkRnd(DWORD n) { g_fkRnd = g_fkRnd * 1103515245 + 12345; return (g_fkRnd >> 8) % n; }
static const int kFkN = 240;
static u8* g_fkData[kFkN];
static DWORD g_fkLen[kFkN];
static u8* g_fkRef[kFkN];
static int g_fkRefLen[kFkN];
static u8* g_fkGot[kFkN];
static int g_fkGotLen[kFkN];

static void FkRun(int compression, u8** out, int* outLen) {
    FkPkg p;
    FkBuild(&p, g_fkData, g_fkLen, kFkN, compression);
    g_fkOut = out;
    g_fkOutLen = outLen;
    ScCommitHook(&p, NULL);
    assert(*(int*)(p.b + 0x2FC) == 0);
    FkFree(&p);
}
static void FkCheckSame() {
    for (int i = 0; i < kFkN; i++) {
        assert(g_fkGotLen[i] == g_fkRefLen[i] && !memcmp(g_fkGot[i], g_fkRef[i], (size_t)g_fkRefLen[i]));
        free(g_fkGot[i]);
    }
}

static void TestSaveCompress() {
    static_assert(offsetof(Ts3nCounters, compressMode) == 0x150 && offsetof(Ts3nCounters, flushMode) == 0x180 && offsetof(Ts3nCounters, heightMode) == 0x1B4 && sizeof(Ts3nCounters) >= 210 * 4 && offsetof(Ts3nCounters, mtReady) == 0x2A4 && offsetof(Ts3nCounters, mtMode) == 0x2A8 && offsetof(Ts3nCounters, cacheMode) == 0x280 && offsetof(Ts3nCounters, icLockReady) == 0x1DC && offsetof(Ts3nCounters, rgReady) == 0x204 && offsetof(Ts3nCounters, rgUnlocked) == 0x21C && offsetof(Ts3nCounters, rgJob) == 0x24C, "v18 layout");
    static_assert(offsetof(Ts3nCounters, lsReady) == 0x2C4 && offsetof(Ts3nCounters, lsMode) == 0x2C8 && offsetof(Ts3nCounters, pumpMaxUs) == 0x2F4 &&
                  offsetof(Ts3nCounters, lotScoreUs) == 0x304 && offsetof(Ts3nCounters, lotSetOn) == 0x310 && offsetof(Ts3nCounters, objSceneHist) == 0x330 &&
                  offsetof(Ts3nCounters, lsLogDropped) == 0x344, "v19 layout");
    Ts3nCounters& k = ts3native_counters;
    QueryPerformanceFrequency(&g_qpf);
    g_scCompress = FkCompressWorker; g_scSlotOrig = FkSlot; g_scCommitOrig = FkCommit;
    g_scLock = FkLock; g_scUnlock = FkUnlock;
    g_scThreads = 4;
    g_sb.maxBytes = 1 << 20;  // small window: the queue refills many times
    g_fkCodecVt[1] = (void*)ScSlotHook;
    g_fkCodec.vt = g_fkCodecVt;
    g_fkCodec.alloc = &g_fkHeap;
    int large = 0;
    for (int i = 0; i < kFkN; i++) {
        const DWORD kind = FkRnd(10);
        g_fkLen[i] = kind < 3 ? 0x32 + FkRnd(kScMinLen - 0x32) : kind < 9 ? kScMinLen + FkRnd(300000) : 1000000 + FkRnd(1500000);
        g_fkData[i] = (u8*)malloc(g_fkLen[i]);
        for (DWORD j = 0; j < g_fkLen[i]; j++) g_fkData[i][j] = (u8)FkRnd(256);
        large += g_fkLen[i] >= kScMinLen;
    }

    // 1. mode 0: the game's slot for every call (bound + compress per record); this is the reference.
    k.compressMode = 0;
    FkRun(1, g_fkRef, g_fkRefLen);
    assert(g_fkSlotCalls == 2 * kFkN && k.compressBatches == 0);

    // 2. mode 1: every large record served by a job, byte-identical; only bounds and small records reach the game's slot.
    k.compressMode = 1;
    g_fkSlotCalls = 0;
    FkRun(1, g_fkGot, g_fkGotLen);
    FkCheckSame();
    assert(k.compressBatches == 1 && k.compressJobs == large && k.compressHits == large && k.compressMisses == 0);
    assert(g_fkSlotCalls == 2 * kFkN - large && k.compressPeakMB <= 1 + 2 * 3);

    // 3. the commit uses another mode than predicted (+0x20 = 1 predicts mode 2; flags 2 = mode 1): all misses, same bytes.
    for (int i = 0; i < kFkN; i++) free(g_fkRef[i]);
    k.compressMode = 0;
    g_fkFlags = 2;
    FkRun(1, g_fkRef, g_fkRefLen);  // mode 1 reference
    k.compressMode = 1;
    FkRun(1, g_fkGot, g_fkGotLen);
    FkCheckSame();
    g_fkFlags = 0;
    assert(k.compressMisses == large && k.compressHits == large);

    // 4. +0x20 = 3 (flags 2, predicted right), with a size-only call before the real one (codec plugin path): both
    //    calls served by the same job.
    g_fkSizeFirst = true;
    FkRun(3, g_fkGot, g_fkGotLen);
    FkCheckSame();
    g_fkSizeFirst = false;
    assert(k.compressHits == 3 * large && k.compressMisses == large);

    // 5. verify: the workers' bytes differ; mismatches counted, the game's bytes used.
    g_fkSalt = 1;
    k.compressVerify = 1;
    FkRun(3, g_fkGot, g_fkGotLen);
    FkCheckSame();
    assert(k.compressMismatches == large);
    g_fkSalt = 0;
    k.compressVerify = 0;

    // 6. no compression on the package (+0x20 = 0): no batch; outside a batch the hook is the game's slot.
    const LONG batches = k.compressBatches;
    FkRun(0, g_fkGot, g_fkGotLen);
    for (int i = 0; i < kFkN; i++) free(g_fkGot[i]);
    g_fkSlotCalls = 0;
    u8 dst[64];
    assert(ScSlotHook(&g_fkCodec, NULL, g_fkData[0], 20, dst, 64, 2) == 2 + 20 / 3 + 1 && g_fkSlotCalls == 1 && k.compressBatches == batches);
    assert(k.compressDirectCalls > 0 && k.compressCommits > 0 && k.compressCommitPending == kFkN);

    // 7. compressed-bytes cache (docs/26 §8), outside a batch as every save record is today: the second pass over the same
    //    inputs is answered from the cache with the game's bytes and never reaches the game's slot.
    const DWORD kBuf = 3000000;
    u8* got = (u8*)malloc(kBuf);
    u8* ref = (u8*)malloc(kBuf);
    const int n7 = 40;
    g_ccMax = 64 << 20;
    k.cacheMode = 1;
    for (int pass = 0; pass < 2; pass++) {
        g_fkSlotCalls = 0;
        const LONG h0 = k.cacheHits, m0 = k.cacheMisses;
        for (int i = 0; i < n7; i++) {
            const int r = ScSlotHook(&g_fkCodec, NULL, g_fkData[i], g_fkLen[i], got, kBuf, 2);
            const int rr = FkCompressImpl(ref, g_fkData[i], g_fkLen[i], &g_fkHeap, 1, 0);
            assert(r == rr && !memcmp(got, ref, (size_t)r));
        }
        assert(pass ? g_fkSlotCalls == 0 && k.cacheHits - h0 == n7 && k.cacheMisses == m0 : g_fkSlotCalls == n7 && k.cacheMisses - m0 == n7);
    }
    assert(k.cacheEntries == n7 && k.cacheMismatches == 0 && k.cacheEvicted == 0);
    // same bytes, other mode (flags 0x10000 = mode 2): a miss, its own entry
    g_fkSlotCalls = 0;
    ScSlotHook(&g_fkCodec, NULL, g_fkData[0], g_fkLen[0], got, kBuf, 0x10000);
    assert(g_fkSlotCalls == 1 && k.cacheEntries == n7 + 1);
    // dst too small for the stored output: the game's slot (a miss), nothing stored twice
    ScSlotHook(&g_fkCodec, NULL, g_fkData[1], g_fkLen[1], got, 4, 2);
    assert(g_fkSlotCalls == 2 && k.cacheEntries == n7 + 1);
    // the same input with one word changed: a miss
    g_fkData[2][g_fkLen[2] / 2] ^= 1;
    ScSlotHook(&g_fkCodec, NULL, g_fkData[2], g_fkLen[2], got, kBuf, 2);
    g_fkData[2][g_fkLen[2] / 2] ^= 1;
    assert(g_fkSlotCalls == 3 && k.cacheEntries == n7 + 2);
    // verify: a corrupted entry is counted, dropped, and the game's bytes are returned
    CcEntry* e = *CcFind(CcHash(g_fkData[3], g_fkLen[3]), g_fkLen[3], 1);
    assert(e);
    ((u8*)(e + 1))[2] ^= 0xFF;
    k.compressVerify = 1;
    const int r7 = ScSlotHook(&g_fkCodec, NULL, g_fkData[3], g_fkLen[3], got, kBuf, 2);
    assert(r7 == FkCompressImpl(ref, g_fkData[3], g_fkLen[3], &g_fkHeap, 1, 0) && !memcmp(got, ref, (size_t)r7));
    assert(k.cacheMismatches == 1 && k.cacheEntries == n7 + 1 && g_fkSlotCalls == 4);
    ScSlotHook(&g_fkCodec, NULL, g_fkData[4], g_fkLen[4], got, kBuf, 2);  // a good entry under verify: a hit, no mismatch
    assert(k.cacheMismatches == 1 && g_fkSlotCalls == 5);
    k.compressVerify = 0;
    // switch off: the game's slot, no lookup
    k.cacheMode = 0;
    const LONG h7 = k.cacheHits;
    ScSlotHook(&g_fkCodec, NULL, g_fkData[4], g_fkLen[4], got, kBuf, 2);
    assert(g_fkSlotCalls == 6 && k.cacheHits == h7);
    k.cacheMode = 1;
    // a 4 MB budget over all records: least recently used entries go, the budget holds, the newest entry stays
    while (g_ccLru.next != &g_ccLru) CcDrop(g_ccLru.next);  // the budget is fixed at load in game; start empty here
    g_ccMax = 4 << 20;
    int last = -1;
    for (int i = 0; i < kFkN; i++) {
        ScSlotHook(&g_fkCodec, NULL, g_fkData[i], g_fkLen[i], got, kBuf, 2);
        assert(g_ccBytes <= g_ccMax);
        if (FkCompressImpl(NULL, g_fkData[i], g_fkLen[i], &g_fkHeap, 1, 0) <= (int)(g_ccMax / 4)) last = i;
    }
    assert(k.cacheEvicted > 0 && k.cacheKB <= 4096 && last >= 0);
    g_fkSlotCalls = 0;
    ScSlotHook(&g_fkCodec, NULL, g_fkData[last], g_fkLen[last], got, kBuf, 2);
    assert(g_fkSlotCalls == 0);
    while (g_ccLru.next != &g_ccLru) CcDrop(g_ccLru.next);
    assert(g_ccBytes == 0 && k.cacheEntries == 0);
    g_ccMax = 0;
    LARGE_INTEGER q0, q1;  // hash speed, for the cost per save (docs/26 §8.3)
    memset(got, 0x37, kBuf);
    unsigned __int64 hs = 0;
    QueryPerformanceCounter(&q0);
    for (int i = 0; i < 40; i++) hs += CcHash(got + (i & 3), kBuf - 4);
    QueryPerformanceCounter(&q1);
    printf("save compress cache: hash %.0f MB/s (%llx)\n", 40.0 * kBuf / 1048576 / ((double)(q1.QuadPart - q0.QuadPart) / g_qpf.QuadPart), hs & 0xF);
    free(got), free(ref);
    for (int i = 0; i < kFkN; i++) free(g_fkRef[i]), free(g_fkData[i]);
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

// --- water jps (phase 9): hand-checked jumps on tiny grids; waterjps-test.exe compares against the game's code ---
static DWORD __cdecl FakeJump(const u8* const*, const u8* const*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*) { return 1234; }

static void TestWaterJps() {
    Ts3nCounters& k = ts3native_counters;
    static u8 g1[9], g2[9];
    const u8* p1 = g1;
    const u8* p2 = g2;
    k.waterJpsMode = 1;
    auto J = [&](DWORD w, DWORD h, DWORD goal, DWORD cell, DWORD dir, DWORD flag) { return WaterJumpHook(&p1, &p2, w, h, goal, cell, dir, flag, NULL); };
    memset(g1, 1, 9);
    assert(J(6, 1, 0xFFFFFFFF, 0, 2, 0) == 0xFFFFFFFF);  // open row east: no jump point before the edge
    assert(J(6, 1, 4, 0, 2, 0) == 4);                    // the goal
    g2[3] = 1;
    assert(J(6, 1, 0xFFFFFFFF, 0, 2, 0) == 2);  // next to a cost-layer cell
    g2[3] = 0;
    g1[0] = 0;
    assert(J(3, 1, 0xFFFFFFFF, 0, 2, 1) == 1);  // start on land: one step out
    g1[0] = 1;
    assert(J(3, 1, 0xFFFFFFFF, 0, 2, 1) == 0xFFFFFFFF);
    // 3x3 sea, diagonal from (0,0): the east scan off (1,1) wraps past the row end (the game's index step) to (0,2), (1,2),
    // (2,2), so it finds either goal and (1,1) is the jump point
    assert(J(3, 3, 7, 0, 3, 0) == 4 && J(3, 3, 8, 0, 3, 0) == 4);
    assert(J(3, 3, 0xFFFFFFFF, 0, 3, 0) == 0xFFFFFFFF);
    // mode 0 and the odd cases go to the game's jump
    g_wjOrig = FakeJump;
    assert(J(3, 3, 7, 0, 0xFF, 0) == 1234 && k.waterJpsFallbacks == 1);
    k.waterJpsMode = 0;
    assert(J(3, 3, 7, 0, 3, 0) == 1234 && k.waterJpsFallbacks == 1);
    g_wjOrig = (WjJumpFn)(uintptr_t)WJ_JUMP;
    k.waterJpsFallbacks = 0;
}

static int g_fakeHeightCalls;
static double g_fakeHeight = 12.5;
static double __fastcall FakeHeight(void*, void*, float x, float z) { g_fakeHeightCalls++; return g_fakeHeight + x - z; }

static int g_fakeRebuilds;
static DWORD __fastcall FakeRebuild(void*, void*, int, int, void*, void*, void*) { g_fakeRebuilds++; return 0; }

static void TestHeightCache() {
    static u8 grid[0xD4];
    *(DWORD*)(grid + 0xCC) = 4;  // 4 x 4 cells of 256
    *(DWORD*)(grid + 0xD0) = 4;
    g_hc = (HcEntry*)calloc(1024, sizeof(HcEntry));
    g_hcMask = 511;
    g_hcOrig = FakeHeight;
    g_hcRebuildOrig = FakeRebuild;
    Ts3nCounters& s = ts3native_counters;
    s.heightMode = 0;
    assert(HcHook(grid, NULL, 3.f, 1.f) == 14.5 && g_fakeHeightCalls == 1 && s.heightCalls == 0);  // mode 0: the game's raycast
    s.heightMode = 1;
    assert(HcHook(grid, NULL, 3.f, 1.f) == 14.5 && g_fakeHeightCalls == 2 && s.heightMisses == 1);
    assert(HcHook(grid, NULL, 3.f, 1.f) == 14.5 && g_fakeHeightCalls == 2 && s.heightHits == 1);   // hit: no raycast
    assert(HcHook(grid, NULL, 3.f, 2.f) == 13.5 && g_fakeHeightCalls == 3);                  // other point
    *(DWORD*)(grid + 0x34) = 7;  // ray start changed
    assert(HcHook(grid, NULL, 3.f, 1.f) == 14.5 && g_fakeHeightCalls == 4);
    g_hcGen++;                   // cell rebuild
    g_fakeHeight = 20;
    assert(HcHook(grid, NULL, 3.f, 1.f) == 22 && g_fakeHeightCalls == 5);
    g_fakeHeight = 30;           // terrain changed without a rebuild: Verify returns the real height and counts it
    g_hcVerify = true;
    assert(HcHook(grid, NULL, 3.f, 1.f) == 32 && g_fakeHeightCalls == 6 && s.heightMismatches == 1);
    g_hcVerify = false;
    g_fakeHeight = 12.5;
    g_hcGen++;

    // per-cell invalidation: rebuilding cell (1, 0) keeps cell (0, 0); boundary x = 256 belongs to cell 1 (floor >> 8)
    int c = g_fakeHeightCalls;
    HcHook(grid, NULL, 3.f, 1.f); HcHook(grid, NULL, 255.9f, 1.f); HcHook(grid, NULL, 256.f, 1.f); HcHook(grid, NULL, 300.f, 1.f);
    c = g_fakeHeightCalls;
    HcHook(grid, NULL, 3.f, 1.f); HcHook(grid, NULL, 255.9f, 1.f); HcHook(grid, NULL, 256.f, 1.f); HcHook(grid, NULL, 300.f, 1.f);
    assert(g_fakeHeightCalls == c);  // all four cached
    const LONG rb = s.heightRebuilds;
    HcRebuildHook(grid, NULL, 256, 0, NULL, NULL, NULL);
    assert(g_fakeRebuilds == 1 && s.heightRebuilds == rb + 1);
    assert(HcHook(grid, NULL, 3.f, 1.f) == 14.5 && HcHook(grid, NULL, 255.9f, 1.f) == 12.5 + 255.9f - 1.f && g_fakeHeightCalls == c);
    assert(HcHook(grid, NULL, 256.f, 1.f) == 267.5 && g_fakeHeightCalls == c + 1);  // cell 1 rebuilt: raycast again
    assert(HcHook(grid, NULL, 300.f, 1.f) == 311.5 && g_fakeHeightCalls == c + 2);
    // a rebuild outside the grid bumps the global generation: everything is dropped
    HcRebuildHook(grid, NULL, 4 * 256, 0, NULL, NULL, NULL);
    assert(HcHook(grid, NULL, 3.f, 1.f) == 14.5 && g_fakeHeightCalls == c + 3);
    // points outside the grid (incl. negative, NaN-free) are never cached
    HcHook(grid, NULL, -0.5f, 1.f);
    assert(HcHook(grid, NULL, -0.5f, 1.f) == 11 && g_fakeHeightCalls == c + 5);
    assert(HcHook(grid, NULL, 3.f, 1100.f) == 12.5 + 3 - 1100 && g_fakeHeightCalls == c + 6);
    assert(HcFloor(-0.5f) == -1 && HcFloor(-1.f) == -1 && HcFloor(255.99f) == 255 && HcFloor(256.f) == 256 && HcFloor(-0.f) == 0);

    // 2 ways: two keys of one set both stay; a third evicts the one used least recently
    const DWORD gk = (DWORD)(uintptr_t)grid, x3 = 0x40400000;  // 3.f
    HcEntry* set0 = HcSet(gk, x3, 0x3F800000);                // (3, 1)
    float zs[2]; int found = 0;
    for (float z = 2.f; found < 2 && z < 1000.f; z += 0.125f) if (HcSet(gk, x3, *(DWORD*)&z) == set0) zs[found++] = z;
    assert(found == 2);
    HcHook(grid, NULL, 3.f, zs[0]);  // set: (3, 1) and zs[0]
    c = g_fakeHeightCalls;
    assert(HcHook(grid, NULL, 3.f, 1.f) == 14.5 && HcHook(grid, NULL, 3.f, zs[0]) == 15.5 - zs[0] && g_fakeHeightCalls == c);
    HcHook(grid, NULL, 3.f, 1.f);    // (3, 1) used last
    HcHook(grid, NULL, 3.f, zs[1]);  // evicts zs[0]
    assert(g_fakeHeightCalls == c + 1);
    HcHook(grid, NULL, 3.f, 1.f);
    assert(g_fakeHeightCalls == c + 1);
    HcHook(grid, NULL, 3.f, zs[0]);
    assert(g_fakeHeightCalls == c + 2);
    s.heightMode = 0;
    free(g_hc);
    g_hc = NULL;
}

// --- loader lock check: counting on a local section, contention from a second thread ---
static HANDLE g_ilHeld;
static DWORD WINAPI IlHolder(void*) {
    IlLock();
    SetEvent(g_ilHeld);
    Sleep(40);  // main thread is waiting by now
    IlUnlock();
    return 0;
}

static void TestIcLock() {
    static CRITICAL_SECTION cs;
    static DWORD slot;
    InitializeCriticalSection(&cs);
    g_ilCs = &cs;
    g_ilCallbackSlot = &slot;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_ilUsPerTick = 1e6 / (double)f.QuadPart;
    Ts3nCounters& k = ts3native_counters;
    const LONG me = (LONG)GetCurrentThreadId();
    IlLock(); IlLock(); IlUnlock(); IlUnlock();  // recursive, uncontended
    assert(g_ilEnterLow == 2 && k.icLockContended == 0 && k.icLockLastTid == me && k.icLockOwnerChanges == 0 && k.icLockLeaveWaiters == 0);
    assert(k.icLockCallback == 0 && g_ilCallerCount >= 1);  // slot still empty; callers noted (inlining decides how many)
    g_ilHeld = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, IlHolder, NULL, 0, NULL);
    WaitForSingleObject(g_ilHeld, INFINITE);
    slot = 0x1234;
    IlLock();  // waits ~40 ms for the holder
    assert(k.icLockContended == 1 && k.icLockWaitUs >= 20000 && k.icLockMaxWaitUs == k.icLockWaitUs);
    assert(k.icLockOwnerChanges == 2 && k.icLockLastTid == me && k.icLockPrevTid != me && k.icLockPrevTid != 0);
    assert(k.icLockLeaveWaiters == 1 && k.icLockCallback == 0x1234);  // the holder's unlock had us waiting
    IlUnlock();
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    CloseHandle(g_ilHeld);
    for (int i = 0; i < 1020; i++) { IlLock(); IlUnlock(); }  // 4 + 1020 = 1024 enters
    assert(k.icLockEntersK == 1 && g_ilEnterLow == 0 && k.icLockLeaveWaiters == 1);
    DeleteCriticalSection(&cs);
}

// --- route gate measurement: owner tracking of a recursive lock, outermost unlocked gate entries, dead frames ---
static CRITICAL_SECTION g_rgFakeCs;
static LONG __fastcall RgFakeAcquire(void* lock, void*, const DWORD*) {  // like 0x4E16F0: returns the recursion count
    EnterCriticalSection(&g_rgFakeCs);
    return ++*(LONG*)((u8*)lock + 0x20);
}
static LONG __fastcall RgFakeRelease(void* lock, void*) {
    const LONG r = --*(LONG*)((u8*)lock + 0x20);
    LeaveCriticalSection(&g_rgFakeCs);
    return r;
}
static u8 g_rgLockObj[0x28], g_rgOtherObj[0x28];
static HANDLE g_rgHeld, g_rgGo;
static DWORD WINAPI RgHolder(void*) {
    const DWORD inf = 0xFFFFFFFF;
    LockAcquireHook(g_rgLockObj, NULL, &inf);
    SetEvent(g_rgHeld);
    WaitForSingleObject(g_rgGo, INFINITE);
    LockReleaseHook(g_rgLockObj, NULL);
    return 0;
}

static void TestRouteGate() {
    Ts3nCounters& k = ts3native_counters;
    const LONG me = (LONG)GetCurrentThreadId();
    const DWORD inf = 0xFFFFFFFF;
    const LockAcquireFn savedLock = g_lockOrig;
    g_lockOrig = RgFakeAcquire;
    g_rgRelOrig = RgFakeRelease;
    InitializeCriticalSection(&g_rgFakeCs);
    InitializeCriticalSection(&g_rgCs);
    // inner: mov eax,42; ret.  outer (stdcall, 1 arg): mov eax,0; call inner; ret 4
    u8* code = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    static const u8 kInner[5] = {0xB8, 0x2A, 0x00, 0x00, 0x00}, kOuter[5] = {0xB8, 0x00, 0x00, 0x00, 0x00};
    u8* inner = code;
    u8* outer = code + 16;
    memcpy(inner, kInner, 5);
    inner[5] = 0xC3;
    memcpy(outer, kOuter, 5);
    outer[5] = 0xE8;
    *(LONG*)(outer + 6) = (LONG)(inner - (outer + 10));
    outer[10] = 0xC2; outer[11] = 0x04; outer[12] = 0x00;
    g_rgTramp[0] = InstallDetour("gate outer", (DWORD)(uintptr_t)outer, kOuter, 5, (void*)RgStub0);
    g_rgTramp[1] = InstallDetour("gate inner", (DWORD)(uintptr_t)inner, kInner, 5, (void*)RgStub1);
    assert(g_rgTramp[0] && g_rgTramp[1]);
    typedef int(__stdcall* OuterFn)(int);
    typedef int(__cdecl* InnerFn)();
    const OuterFn Outer = (OuterFn)(void*)outer;
    const InnerFn Inner = (InnerFn)(void*)inner;

    k.rgReady = 7;
    assert(Outer(5) == 42 && k.rgEntries == 0);  // lock not learned yet: pass-through, nothing counted
    g_rgLock = g_rgLockObj;
    assert(Outer(5) == 42 && k.rgEntries == 2 && t_rg.n == 0);
    assert(k.rgUnlocked[0] == 1 && k.rgUnlocked[1] == 0);  // the nested inner entry is not outermost
    assert(Inner() == 42 && k.rgUnlocked[1] == 1 && k.rgJob[0] == 0 && k.rgJob[1] == 0);

    // recursive hold: still owned after the inner release, free after the outer one; only the outermost acquire counts
    assert(LockAcquireHook(g_rgLockObj, NULL, &inf) == 1 && LockAcquireHook(g_rgLockObj, NULL, &inf) == 2);
    assert(k.rgAcquires == 1 && k.rgOwnerTid == me);
    assert(LockReleaseHook(g_rgLockObj, NULL) == 1 && k.rgOwnerTid == me);
    assert(Outer(5) == 42 && Inner() == 42 && k.rgUnlocked[0] == 1 && k.rgUnlocked[1] == 1 && k.rgEntries == 6);
    assert(LockReleaseHook(g_rgLockObj, NULL) == 0 && k.rgOwnerTid == 0);
    assert(Outer(5) == 42 && k.rgUnlocked[0] == 2);
    // any other lock is not tracked
    assert(LockAcquireHook(g_rgOtherObj, NULL, &inf) == 1 && k.rgAcquires == 1 && k.rgOwnerTid == 0);
    assert(LockReleaseHook(g_rgOtherObj, NULL) == 0);

    // another thread holds it: our entries still count, its release frees the owner
    g_rgHeld = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_rgGo = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, RgHolder, NULL, 0, NULL);
    WaitForSingleObject(g_rgHeld, INFINITE);
    assert(k.rgOwnerTid != 0 && k.rgOwnerTid != me && k.rgAcquires == 2);
    assert(Outer(5) == 42 && k.rgUnlocked[0] == 3);
    SetEvent(g_rgGo);
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    CloseHandle(g_rgHeld);
    CloseHandle(g_rgGo);
    assert(k.rgOwnerTid == 0);

    // a route-worker job runs: the same entries also count as job entries
    g_callerTid = 1234;
    assert(Inner() == 42 && k.rgUnlocked[1] == 2 && k.rgJob[1] == 1);
    g_callerTid = 0;

    // frames left without returning: an entry at or above a frame's return slot drops it (it is outermost again)
    DWORD stack[20] = {};
    stack[10] = 0x111;
    RgEnter(0, &stack[10]);
    assert(t_rg.n == 1 && stack[10] == (DWORD)(uintptr_t)RgLeaveThunk && k.rgUnlocked[0] == 4);
    RgEnter(1, &stack[5]);  // deeper: nested, not counted
    assert(t_rg.n == 2 && k.rgUnlocked[1] == 2);
    RgEnter(1, &stack[12]);  // above both: they are dead
    assert(t_rg.n == 1 && k.rgUnlocked[1] == 3 && t_rg.f[0].ret == 0);
    t_rg.n = 0;
    // 16 open frames: a 17th entry is counted as untracked and its return address left alone
    for (int i = 0; i < 17; i++) RgEnter(1, &stack[19 - i]);
    assert(t_rg.n == 16 && k.rgUntracked == 1 && stack[3] == 0 && k.rgUnlocked[1] == 4);
    t_rg.n = 0;

    // every stub pushes its own site index and jumps through its own trampoline slot
    for (int i = 0; i < RG_SITES; i++) {
        const u8* s = (const u8*)(void*)kRgStubs[i];
        const DWORD slot = (DWORD)(uintptr_t)&g_rgTramp[i];
        bool push = false, jmp = false;
        for (int j = 0; j < 40; j++) {
            push |= s[j] == 0x6A && s[j + 1] == i;
            jmp |= s[j] == 0xFF && s[j + 1] == 0x25 && *(const DWORD*)(s + j + 2) == slot;
        }
        assert(push && jmp);
    }

    k.rgReady = 0;
    g_rgLock = NULL;
    g_lockOrig = savedLock;
    VirtualFree(code, 0, MEM_RELEASE);
    DeleteCriticalSection(&g_rgFakeCs);
}

// --- mono thread step 1: the P2 root push over a fake GC thread table, P1 registration guard ---
static DWORD g_mtFakeTab[256 * 5];
static volatile LONG g_mtFakeMax, g_mtFakeStopped;
static int g_mtOrigCalls, g_mtOnes, g_mtStacks;
static DWORD g_mtOneVals[16], g_mtLo[4], g_mtHi[4];
static void __cdecl MtFakeOrig() { g_mtOrigCalls++; }
static void __cdecl MtFakeOne(DWORD p) { if (g_mtOnes < 16) g_mtOneVals[g_mtOnes] = p; g_mtOnes++; }
static void __cdecl MtFakeStack(DWORD lo, DWORD hi) { if (g_mtStacks < 4) { g_mtLo[g_mtStacks] = lo; g_mtHi[g_mtStacks] = hi; } g_mtStacks++; }
static void* __cdecl MtFakeNewThread() {  // like 0xE51070: claims the next entry for the caller
    const LONG i = ++g_mtFakeMax;
    DWORD* e = g_mtFakeTab + i * 5;
    e[0] = 1; e[1] = GetCurrentThreadId(); e[3] = (DWORD)(uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase;
    return e;
}
static volatile DWORD g_mtMarkerAt, g_mtBase, g_mtTid;
static HANDLE g_mtReady, g_mtQuit;
static DWORD WINAPI MtVictim(void*) {
    volatile DWORD marker = 0xC0FFEE11;  // a "root" on this stack
    g_mtMarkerAt = (DWORD)(uintptr_t)&marker;
    g_mtBase = (DWORD)(uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase;
    g_mtTid = GetCurrentThreadId();
    SetEvent(g_mtReady);
    WaitForSingleObject(g_mtQuit, INFINITE);
    return marker == 0xC0FFEE11 ? 0 : 1;
}
static DWORD WINAPI MtOtherRegister(void*) { return MtRegisterCurrentThread() ? 1 : 0; }

static void TestMonoThread() {
    g_mtOrigPush = MtFakeOrig; g_mtPushOne = MtFakeOne; g_mtPushStack = MtFakeStack; g_mtNewThread = MtFakeNewThread;
    g_mtTable = g_mtFakeTab; g_mtMaxIdx = &g_mtFakeMax; g_mtStopped = &g_mtFakeStopped;
    Ts3nCounters& k = ts3native_counters;
    const DWORD me = GetCurrentThreadId();
    g_mtReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_mtQuit = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, MtVictim, NULL, 0, NULL);
    WaitForSingleObject(g_mtReady, INFINITE);
    assert(SuspendThread(t) != (DWORD)-1);
    // [0] the collector itself (suspended flag set to prove the tid test), [1] the victim, [2] not suspended, [3] freed
    DWORD* e = g_mtFakeTab;
    e[0] = 1; e[1] = me; e[2] = 0; e[3] = 0x1000; e[4] = 1;
    e[5] = 1; e[6] = g_mtTid; e[7] = (DWORD)(uintptr_t)t; e[8] = g_mtBase; e[9] = 1;
    e[10] = 1; e[11] = g_mtTid + 1; e[12] = (DWORD)(uintptr_t)t; e[13] = g_mtBase; e[14] = 0;
    e[15] = 0; e[16] = g_mtTid + 2; e[17] = (DWORD)(uintptr_t)t; e[18] = 0; e[19] = 1;
    g_mtFakeMax = 3;
    // world running (incremental step): only the game's push
    g_mtFakeStopped = 0;
    MtPushOtherRoots();
    assert(g_mtOrigCalls == 1 && g_mtOnes == 0 && g_mtStacks == 0 && k.mtPushCalls == 0);
    // world stopped: the victim's 7 registers and its stack [ESP, base), which holds the marker
    g_mtFakeStopped = 1;
    MtPushOtherRoots();
    CONTEXT c;
    c.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL;
    assert(GetThreadContext(t, &c));
    assert(g_mtOrigCalls == 2 && g_mtOnes == 7 && g_mtStacks == 1 && k.mtPushCalls == 1 && k.mtStacksPushed == 1 && k.mtCtxFails == 0);
    assert(g_mtOneVals[0] == c.Eax && g_mtOneVals[1] == c.Ebx && g_mtOneVals[2] == c.Ecx && g_mtOneVals[3] == c.Edx);
    assert(g_mtOneVals[4] == c.Esi && g_mtOneVals[5] == c.Edi && g_mtOneVals[6] == c.Ebp);
    assert(g_mtLo[0] == c.Esp && g_mtHi[0] == g_mtBase && g_mtLo[0] <= g_mtMarkerAt && g_mtMarkerAt < g_mtHi[0]);
    // GetThreadContext fails (bad handle): whole committed stack, still covering the marker
    e[7] = 0x1234;
    MtPushOtherRoots();
    assert(k.mtCtxFails == 1 && g_mtOnes == 7 && g_mtStacks == 2 && g_mtHi[1] == g_mtBase);
    assert(g_mtLo[1] <= g_mtLo[0] && g_mtLo[1] <= g_mtMarkerAt && (g_mtLo[1] & 0xFFF) == 0);
    // max index above 255 is clamped (entries past the fake's 4 are zero)
    e[7] = (DWORD)(uintptr_t)t;
    g_mtFakeMax = 1000;
    MtPushOtherRoots();
    assert(g_mtStacks == 3 && k.mtCtxFails == 1);
    g_mtFakeMax = 3;
    ResumeThread(t);
    SetEvent(g_mtQuit);
    WaitForSingleObject(t, INFINITE);
    DWORD code = 1;
    GetExitCodeThread(t, &code);
    assert(code == 0);
    CloseHandle(t);
    // P1: only the sim thread registers, once
    g_mtSimTid = me;
    e[1] = me + 7;  // the collector entry is someone else now: "me" is missing
    HANDLE o = CreateThread(NULL, 0, MtOtherRegister, NULL, 0, NULL);
    WaitForSingleObject(o, INFINITE);
    GetExitCodeThread(o, &code);
    CloseHandle(o);
    assert(code == 0 && g_mtFakeMax == 3);
    assert(MtRegisterCurrentThread() && g_mtFakeMax == 4 && MtFind(me) == 4);
    assert(MtRegisterCurrentThread() && g_mtFakeMax == 4);
    CloseHandle(g_mtReady);
    CloseHandle(g_mtQuit);
    memset(&k.mtReady, 0, 8 * sizeof(LONG));
}

// --- lot streaming measurement: each hook on a fake of the game function it wraps (docs/28 §5.5) ---
static void LsSpin(double ms) { const LONGLONG t0 = LsNow(); while ((LsNow() - t0) * g_lsUsPerTick < ms * 1000) {} }
static int g_lsFakeExecs, g_lsPumpNest;
static u8 g_lsCallAdd[0x1C], g_lsCallRemove[0x1C], g_lsCallOther[0x1C];
static u8 g_lsLot[0x400], g_lsLot2[0x400];
static DWORD g_lsLotObjs[3];
static DWORD __fastcall LsFakeExec(u8*, void*) { g_lsFakeExecs++; LsSpin(2); return 0x55; }
static DWORD __fastcall LsFakePump(void*, void*, DWORD a, DWORD b, DWORD c) {
    assert(a == 1 && b == 2 && c == 3);
    if (g_lsPumpNest++ == 0) {
        LsAddHook(g_lsCallAdd, NULL);
        LsRemoveHook(g_lsCallRemove, NULL);
        LsPumpHook(NULL, NULL, 1, 2, 3);  // nested pass (a wait helping run jobs): part of the outer one
    }
    return 1;
}
static DWORD __fastcall LsFakeSetActive(u8* lot, void*, DWORD on) { if (!lot[0xC9]) lot[0xC1] = (u8)on; return 0; }
static DWORD __fastcall LsFakeScore(void*, void*, DWORD a, DWORD, DWORD, DWORD d) {
    assert(a == 0x3F800000 && d == 4);
    LsSetActiveHook(g_lsLot, NULL, 1);   // switches
    LsSetActiveHook(g_lsLot2, NULL, 1);  // [lot+0xC9] set: the game ignores it
    return 9;
}
static DWORD __fastcall LsFakeSync(void*, void*, DWORD, DWORD) { LsSpin(1); return 1; }
static DWORD __fastcall LsFakeObj(u8*, void*, DWORD a, DWORD, DWORD) { LsSpin(2); return a + 1; }
static DWORD WINAPI LsOtherThread(void*) { g_lsPumpNest = 0; return LsPumpHook(NULL, NULL, 1, 2, 3); }

static void TestLotStream() {
    Ts3nCounters& k = ts3native_counters;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_lsUsPerTick = 1e6 / (double)f.QuadPart;
    static DWORD simTid, mainTid;
    simTid = GetCurrentThreadId();
    mainTid = simTid + 1;  // main is another thread: synchronous posts to it wait
    g_lsSimTid = &simTid;
    g_lsMainTidSlot = &mainTid;
    g_lsAddOrig = g_lsRemoveOrig = LsFakeExec;
    g_lsPumpOrig = LsFakePump;
    g_lsScoreOrig = LsFakeScore;
    g_lsSetActiveOrig = LsFakeSetActive;
    g_lsSyncOrig = LsFakeSync;
    g_lsObjOrig = LsFakeObj;
    *(u8**)(g_lsCallAdd + 0x0C) = g_lsLot - 0x10;  // the execute adds [call+0x14] to [call+0x0C]
    *(int*)(g_lsCallAdd + 0x14) = 0x10;
    *(DWORD*)(g_lsCallAdd + 0x10) = LS_ADD_FN;
    *(u8**)(g_lsCallRemove + 0x0C) = g_lsLot;
    *(DWORD*)(g_lsCallRemove + 0x10) = LS_REMOVE_FN;
    memcpy(g_lsCallOther, g_lsCallAdd, sizeof(g_lsCallOther));
    *(DWORD*)(g_lsCallOther + 0x10) = 0x12345678;
    *(DWORD**)(g_lsLot + 0x14) = g_lsLotObjs;
    *(DWORD**)(g_lsLot + 0x18) = g_lsLotObjs + 3;
    *(DWORD*)(g_lsLot + 0x48) = 1;
    *(DWORD*)(g_lsLot + 0x4C) = 2;
    g_lsLot2[0xC9] = 1;
    wchar_t tmp[MAX_PATH], path[MAX_PATH], ini[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf_s(path, L"%sts3n-selftest-lotstream.jsonl", tmp);
    swprintf_s(ini, L"%sts3n-selftest-lotstream.ini", tmp);
    assert(LsOpenLog(path));

    // mode 0: every hook passes straight through, nothing counted or logged
    k.lsMode = 0;
    assert(LsAddHook(g_lsCallAdd, NULL) == 0x55 && g_lsFakeExecs == 1 && k.lotAdds == 0);
    assert(LsPumpHook(NULL, NULL, 1, 2, 3) == 1 && k.pumpCalls == 0 && k.lotAdds == 0 && k.lotRemoves == 0);
    assert(LsScoreHook(NULL, NULL, 0x3F800000, 2, 3, 4) == 9 && k.lotScoreCalls == 0 && k.lotSetOn == 0 && g_lsLot[0xC1] == 1);
    g_lsLot[0xC1] = 0;
    k.lsMode = 1;

    // add outside a pass; another fn on the same execute is not counted
    const int e0 = g_lsFakeExecs;
    assert(LsAddHook(g_lsCallAdd, NULL) == 0x55 && g_lsFakeExecs == e0 + 1);
    assert(k.lotAdds == 1 && k.lotAddObjs == 3 && k.lotAddMaxObjs == 3 && k.lotAddUs >= 1500 && k.lotAddMaxUs == k.lotAddUs);
    assert(LsAddHook(g_lsCallOther, NULL) == 0x55 && k.lotAdds == 1 && k.lsOtherFns == 1 && g_lsOtherFnCount == 1);
    assert(LsRemoveHook(g_lsCallAdd, NULL) == 0x55 && k.lotRemoves == 0 && k.lsOtherFns == 2 && g_lsOtherFnCount == 2);  // add fn on the remove execute

    // pass on the sim thread: add + remove inside it, the nested pass is part of it
    g_lsPumpNest = 0;
    assert(LsPumpHook(NULL, NULL, 1, 2, 3) == 1);
    assert(k.pumpCalls == 1 && k.pumpLotPasses == 1 && k.pumpMaxLots == 2 && k.lotAdds == 2 && k.lotRemoves == 1);
    assert(k.pumpMaxUs >= 3500 && k.pumpUs == k.pumpMaxUs && k.lotRemoveUs >= 1500 && g_lsPumpTid == 0);
    // a pass on another thread is not timed (its lot calls still are)
    HANDLE t = CreateThread(NULL, 0, LsOtherThread, NULL, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    assert(k.pumpCalls == 1 && k.pumpLotPasses == 1 && k.lotAdds == 3 && k.lotRemoves == 2);
    // the reader's per-second reset
    k.pumpMaxUs = 0;
    g_lsPumpNest = 1;  // a pass with no lot calls
    LsPumpHook(NULL, NULL, 1, 2, 3);
    assert(k.pumpCalls == 2 && k.pumpLotPasses == 1 && k.pumpMaxUs < 1500);

    // scoring: one switch made inside it; SetActive's own change test
    assert(LsScoreHook(NULL, NULL, 0x3F800000, 2, 3, 4) == 9);
    assert(k.lotScoreCalls == 1 && k.lotScoreChanged == 1 && k.lotSetOn == 1 && k.lotSetOff == 0 && g_lsScoreTid == 0);
    LsSetActiveHook(g_lsLot, NULL, 1);  // already active
    assert(k.lotSetOn == 1);
    LsSetActiveHook(g_lsLot, NULL, 0x100);  // byte arg: low byte 0 = off
    assert(k.lotSetOff == 1 && g_lsLot[0xC1] == 0 && k.lotScoreChanged == 1);

    // sync dispatch: only a synchronous post to another thread is a wait (low bytes of both args)
    assert(LsSyncHook(NULL, NULL, 0, 0) == 1 && k.syncWaits == 1 && k.syncWaitUs >= 800);
    LsSyncHook(NULL, NULL, 1, 0);      // target = sim = caller: runs inline
    LsSyncHook(NULL, NULL, 0, 1);      // async
    assert(k.syncWaits == 1);
    LsSyncHook(NULL, NULL, 0x100, 0x100);
    assert(k.syncWaits == 2);

    // per object: histogram, slow ones logged
    g_lsObjLogUs = 1000;
    assert(LsObjHook(g_lsLot, NULL, 0xABCD, 0, 0) == 0xABCE && k.objScenes == 1 && k.objSceneHist[2] == 1 && k.objSceneMaxUs >= 1500);
    g_lsObjLogUs = 1000000;
    LsObjHook(g_lsLot, NULL, 0xABCD, 0, 0);
    assert(k.objScenes == 2 && k.lsLogDropped == 0);

    // event file: add (no pass), add + remove (in the pass), add + remove (other thread's pass, untimed), obj
    CloseHandle(g_lsLog);
    g_lsLog = INVALID_HANDLE_VALUE;
    FILE* fp = NULL;
    assert(_wfopen_s(&fp, path, L"rb") == 0 && fp);
    char line[512], lines[8][512];
    int n = 0;
    while (n < 8 && fgets(line, sizeof(line), fp)) strcpy_s(lines[n++], line);
    fclose(fp);
    DeleteFileW(path);
    assert(n == 6);
    assert(strstr(lines[0], "\"kind\":\"add\",\"lot\":\"0x") && strstr(lines[0], "\"lotId\":\"0x0000000200000001\",\"objs\":3,\"ms\":") &&
           strstr(lines[0], "\"passMs\":null}\n") && strncmp(lines[0], "{\"t\":17", 7) == 0);
    assert(strstr(lines[1], "\"kind\":\"add\"") && !strstr(lines[1], "null") && strstr(lines[2], "\"kind\":\"remove\"") && !strstr(lines[2], "null"));
    assert(strstr(lines[3], "\"kind\":\"add\"") && strstr(lines[3], "\"passMs\":null") && strstr(lines[4], "\"kind\":\"remove\""));
    assert(strstr(lines[5], "\"kind\":\"obj\"") && strstr(lines[5], "\"objs\":null") && strstr(lines[5], ",\"obj\":\"0x0000ABCD\"}\n"));

    // install: refused as a whole when the prologues are not there (no game in this process)
    WritePrivateProfileStringW(L"LotStream", L"Enabled", L"0", ini);
    assert(!ApplyLotStream(ini));
    WritePrivateProfileStringW(L"LotStream", L"Enabled", L"1", ini);
    assert(!ApplyLotStream(ini) && k.lsReady == 0 && g_lsLog == INVALID_HANDLE_VALUE);
    DeleteFileW(ini);
    memset(&k.lsReady, 0, (offsetof(Ts3nCounters, lsLogDropped) - offsetof(Ts3nCounters, lsReady) + 4));
}

// --- [Verify]: every probe through its real stub, trampoline and leave thunk on a dummy (nop prologue, mov eax; ret 8) ---
typedef DWORD(__fastcall* VfDummyFn)(DWORD ecx, DWORD edx, DWORD a1, DWORD a2);
static VfDummyFn g_vfDummy[VF_N];
static DWORD VfCall(int id, DWORD ecx = 0, DWORD a1 = 0, DWORD a2 = 0) { return g_vfDummy[id](ecx, 0, a1, a2); }
struct VfCallArgs { int id; DWORD ecx, a1, a2, ret; };
static DWORD WINAPI VfCallThread(void* p) {
    VfCallArgs* a = (VfCallArgs*)p;
    a->ret = g_vfDummy[a->id](a->ecx, 0, a->a1, a->a2);
    return (DWORD)t_vf.n;  // every frame left again
}
static DWORD VfOnOtherThread(int id, DWORD ecx = 0, DWORD a1 = 0, DWORD a2 = 0) {
    VfCallArgs a = {id, ecx, a1, a2, 0};
    HANDLE t = CreateThread(NULL, 0, VfCallThread, &a, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    DWORD open = 1;
    GetExitCodeThread(t, &open);
    CloseHandle(t);
    assert(open == 0);
    return a.ret;
}

// v22 probes (docs/51 I-52..I-62); this thread is the sim thread, main is me + 1
static void TestVerifyV22(DWORD me, DWORD* stack) {
    Ts3nCounters& k = ts3native_counters;
    assert(*g_vfSimTidSlot == me && *g_vfMainTidSlot == me + 1);
    // timed thread probes: one sim call each, off the device thread, us total = max
    static const int kTimed[] = {58, 59, 61, 62, 63, 64, 65, 66, 67, 71, 73, 74, 75, 76, 77, 78, 81, 82};
    for (int i : kTimed) {
        assert(kVf[i].thr >= 46 && kVf[i].thr < VF_NSLOT && VfCall(i) == 0x1000u + i);
        const LONG* s = VfSlot(kVf[i].thr);
        assert(s == k.vfThread3[kVf[i].thr - 46] && s[0] == 0 && s[1] == 1 && s[2] == 0 && s[3] == 1 && s[5] == s[4]);
    }
    // count-only probes (the mid-function cctor slow path, arena grow, GC_free): no time, no frame
    static const int kCount[] = {60, 83, 84};
    for (int i : kCount) {
        assert(kVf[i].kind == VK_COUNT && VfCall(i) == 0x1000u + i && t_vf.n == 0);
        const LONG* s = VfSlot(kVf[i].thr);
        assert(s[1] == 1 && s[0] == 0 && s[4] == 0 && s[5] == 0);
    }
    // shape release: only the terrain swap's call (return address 0xC2A896) counts
    assert(VfCall(68) == 0x1044 && VfSlot(VF_SLOT_SHAPEREL)[1] == 0);
    stack[10] = VF_SWAP_RELEASE_RET;
    VfEnter(68, &stack[10]);
    assert(VfSlot(VF_SLOT_SHAPEREL)[1] == 1 && t_vf.n == 0 && stack[10] == VF_SWAP_RELEASE_RET);
    // shaders: creates before the first world load 0xC6D970 are startup ones
    g_vfWorldLoaded = 0;
    VfCall(79);
    VfCall(80);
    assert(k.vfShaderStartup[0] == 1 && k.vfShaderStartup[1] == 1);
    VfCall(38);  // worldLoad
    VfCall(79);
    assert(k.vfShaderStartup[0] == 1 && VfSlot(73)[1] == 2 && VfSlot(74)[1] == 1);
    // pump same pass: a job readied inside a RunJobs pass and executed in it counts once
    const DWORD jobA = 0xA000, jobB = 0xB000;
    VfCall(70, 0, jobA);  // outside a pass: not recorded
    assert(VfSlot(VF_SLOT_READY)[1] == 0);
    stack[10] = 0xAAA;
    VfEnter(69, &stack[10]);
    VfCall(70, 0, jobA);
    VfCall(4, 0, jobB);
    VfCall(4, 0, jobA);
    VfCall(4, 0, jobA);
    assert(VfSlot(VF_SLOT_READY)[1] == 1 && VfSlot(VF_SLOT_SAMEPASS)[1] == 1 && t_vf.pumpDepth == 1);
    assert(VfLeave() == 0xAAA && t_vf.pumpDepth == 0 && VfSlot(57)[1] == 1);
    VfCall(4, 0, jobA);  // after the pass
    stack[10] = 0xAAA;
    VfEnter(69, &stack[10]);  // a new pass forgets the old one's jobs
    VfCall(4, 0, jobA);
    assert(VfLeave() == 0xAAA && VfSlot(VF_SLOT_SAMEPASS)[1] == 1 && VfSlot(57)[1] == 2);
    // remote calls: selector 1 = sim (this thread): inline; selector 0 = main: posted
    VfCall(40, 0, 1, 1);
    VfCall(40, 0, 0, 1);
    assert(VfSlot(VF_SLOT_RCINLINE)[1] == 1 && t_vf.syncDepth == 0);
    // a synchronous post opens a wait: jobs and lot adds run in it count
    static DWORD callObj[8], otherCall[8];
    callObj[4] = VF_LOTADD_FN;
    otherCall[4] = 0x12345678;
    stack[10] = 0xBBB;
    stack[11] = 0;
    stack[12] = 0;
    VfEnter(40, &stack[10]);
    assert(t_vf.syncDepth == 1);
    VfCall(4, 0, jobB);
    VfCall(72, (DWORD)(uintptr_t)callObj);
    assert(VfSlot(VF_SLOT_JOBINWAIT)[1] == 1 && VfSlot(VF_SLOT_LOTADDINWAIT)[1] == 1 && VfSlot(65)[1] == 1);
    assert(VfLeave() == 0xBBB && t_vf.syncDepth == 0);
    // lot add: only fn 0xAC1130; managed entries and inline re-posts inside it count
    VfCall(72, (DWORD)(uintptr_t)otherCall);
    VfCall(85);
    assert(VfSlot(65)[1] == 1 && VfSlot(VF_SLOT_INVOKE)[1] == 1 && VfSlot(VF_SLOT_LOTADDMANAGED)[1] == 0);
    stack[8] = (DWORD)(uintptr_t)callObj;  // ecx in the pushad block
    stack[10] = 0xCCC;
    VfEnter(72, &stack[10]);
    assert(t_vf.lotAddDepth == 1);
    VfCall(86);
    VfCall(87);
    VfCall(88);
    VfCall(40, 0, 1, 1);
    assert(VfSlot(VF_SLOT_INVOKE)[1] == 4 && VfSlot(VF_SLOT_LOTADDMANAGED)[1] == 3 && VfSlot(VF_SLOT_RCINLOTADD)[1] == 1);
    assert(VfLeave() == 0xCCC && t_vf.lotAddDepth == 0 && VfSlot(65)[1] == 2 && VfSlot(VF_SLOT_RCINLINE)[1] == 2);
    // a dropped lot-add frame gives its depth back
    stack[5] = 0xEEE;
    stack[3] = (DWORD)(uintptr_t)callObj;
    stack[12] = 0xFFF;
    VfEnter(72, &stack[5]);
    VfEnter(0, &stack[12]);
    assert(t_vf.lotAddDepth == 0 && t_vf.n == 1 && VfLeave() == 0xFFF);
    // cursor manager: two threads in Apply at once (self group 4); own nesting does not count
    stack[10] = 0xDDD;
    VfEnter(82, &stack[10]);
    VfCall(82);
    assert(k.vfOverlapCur == 0);
    VfOnOtherThread(82);
    assert(k.vfOverlapCur == 1 && VfLeave() == 0xDDD && g_vfOvIn[4][0] == 0 && g_vfOvIn[4][1] == 0 && t_vf.n == 0);
}

static void TestVerify() {
    static_assert(offsetof(Ts3nCounters, vfReady) == 0x348 && offsetof(Ts3nCounters, vfMode) == 0x34C && offsetof(Ts3nCounters, vfThread) == 0x350 &&
                  offsetof(Ts3nCounters, vfSwaps) == 0x470 && offsetof(Ts3nCounters, vfIdOffSimRet) == 0x494 && offsetof(Ts3nCounters, vfPkgCalls) == 0x4B4 &&
                  offsetof(Ts3nCounters, vfIdChain) == 0x4C8 && offsetof(Ts3nCounters, vfW2lCallsK) == 0x4E8 && offsetof(Ts3nCounters, vfFdbHolds) == 0x4F8 &&
                  offsetof(Ts3nCounters, vfUiLock) == 0x510 && offsetof(Ts3nCounters, vfFpsGate) == 0x534 && offsetof(Ts3nCounters, vfSimSlice) == 0x538 &&
                  offsetof(Ts3nCounters, vfDeviceTid) == 0x550 && offsetof(Ts3nCounters, vfLogDropped) == 0x558, "v20 layout");
    static_assert(offsetof(Ts3nCounters, vfReady2) == 0x55C && offsetof(Ts3nCounters, vfThread2) == 0x560 && offsetof(Ts3nCounters, vfOverlap) == 0x890 &&
                  offsetof(Ts3nCounters, vfFdbPublishes) == 0x8A0 && offsetof(Ts3nCounters, vfFpsLoops) == 0x8A4 &&
                  VF_NSLOT - VF_NT == 68, "v21 layout");
    static_assert(offsetof(Ts3nCounters, vfReady3) == 0x8A8 && offsetof(Ts3nCounters, vfThread3) == 0x8AC && offsetof(Ts3nCounters, vfOverlapCur) == 0xBDC &&
                  offsetof(Ts3nCounters, vfShaderStartup) == 0xBE0 && offsetof(Ts3nCounters, vfMonoIcNestNeg) == 0xBEC && sizeof(Ts3nCounters) == 764 * 4 &&
                  VF_N - VF_LOOP - 1 == 31, "v22 layout");
    Ts3nCounters& k = ts3native_counters;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_vfFreq = f.QuadPart;
    const DWORD me = GetCurrentThreadId();
    static DWORD mainTid, simTid;
    static u8 dev[0x20];
    static u8* devPtr = dev;
    mainTid = me + 1;  // this thread is the sim thread; the device belongs to "main"
    simTid = me;
    *(DWORD*)(dev + 0x10) = mainTid;
    g_vfMainTidSlot = &mainTid;
    g_vfSimTidSlot = &simTid;
    g_vfDevSlot = &devPtr;
    g_vfGroups = VF_THREADS | VF_RACES | VF_COSTS;
    u8* code = (u8*)VirtualAlloc(NULL, VF_N * 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    u8 nops[16];
    memset(nops, 0x90, sizeof(nops));
    for (int i = 0; i < VF_N; i++) {
        u8* d = code + i * 32;
        const int n = kVf[i].len;
        assert(n >= 5 && n <= 10);
        memset(d, 0x90, n);
        d[n] = 0xB8;  // mov eax, 0x1000 + i ; ret 8
        *(DWORD*)(d + n + 1) = 0x1000 + i;
        d[n + 5] = 0xC2; d[n + 6] = 0x08; d[n + 7] = 0x00;
        assert(VfInstallAt(i, (DWORD)(uintptr_t)d, nops));
        g_vfDummy[i] = (VfDummyFn)(void*)d;
    }
    wchar_t tmp[MAX_PATH], path[MAX_PATH], ini[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf_s(path, L"%sts3n-selftest-verify.jsonl", tmp);
    swprintf_s(ini, L"%sts3n-selftest-verify.ini", tmp);
    g_vfLog = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(g_vfLog != INVALID_HANDLE_VALUE);
    static DWORD buckets[0x40C], obj[3][5];
    for (DWORD j = 0; j < 3; j++) { obj[j][2] = 5 + j * VF_ID_BUCKETS; obj[j][3] = j + 1; obj[j][4] = j < 2 ? (DWORD)(uintptr_t)obj[j + 1] : 0; }
    buckets[5] = (DWORD)(uintptr_t)obj[0];
    buckets[0x1028 / 4] = 3;
    DWORD idLast[2] = {5 + 2 * VF_ID_BUCKETS, 3}, idFirst[2] = {5, 1}, idMiss[2] = {6, 0};
    const DWORD B = (DWORD)(uintptr_t)buckets;

    // mode 0: every probe passes straight through, nothing counted
    k.vfMode = 0;
    for (int i = 0; i < VF_N; i++) assert(VfCall(i, i == 16 ? B : 0, i == 25 ? VF_UI_LOCK : 0, i == 16 ? (DWORD)(uintptr_t)idLast : 0) == 0x1000u + i);
    for (const LONG* p = &k.vfThread[0][0]; p <= &k.vfMonoIcNestNeg; p++) assert(*p == 0 || p == &k.vfReady2 || p == &k.vfReady3);
    assert(t_vf.n == 0 && g_vfRayK == 0);
    k.vfMode = 1;

    // threads: role, off-device, time; the leave thunk returns the dummy's eax
    for (int i = 0; i < VF_NT; i++) assert(VfCall(i) == 0x1000u + i);
    for (int i = 0; i < VF_NT; i++) {
        const LONG* s = k.vfThread[i];
        assert(s[0] == 0 && s[1] == 1 && s[2] == 0 && s[3] == 1 && s[4] >= 0 && s[5] == s[4]);
    }
    // v21 thread probes: slots 12-41 in probe order, the same counting
    for (int i = 27; i < VF_LOOP; i++) {
        assert((kVf[i].kind == VK_THREAD || kVf[i].kind == VK_RCALL) && kVf[i].thr == VF_NT + i - 27 && VfCall(i) == 0x1000u + i);
        const LONG* s = VfSlot(kVf[i].thr);
        assert(s == k.vfThread2[i - 27] && s[0] == 0 && s[1] == 1 && s[2] == 0 && s[3] == 1 && s[4] >= 0 && s[5] == s[4]);
    }
    for (int g = 0; g < VF_NOV; g++) assert(*VfOvCount(g) == 0 && g_vfOvIn[g][0] == 0 && g_vfOvIn[g][1] == 0);
    assert(t_vf.n == 0 && t_vf.syncDepth == 0);
    mainTid = me;  // now the main thread, which owns the device
    *(DWORD*)(dev + 0x10) = me;
    assert(VfCall(0) == 0x1000 && k.vfThread[0][0] == 1 && k.vfThread[0][3] == 1);
    mainTid = me + 1;
    *(DWORD*)(dev + 0x10) = mainTid;
    assert(VfOnOtherThread(1) == 0x1001 && k.vfThread[1][2] == 1 && k.vfThread[1][3] == 2);
    DWORD stack[20] = {};
    stack[10] = 0x111;
    VfEnter(0, &stack[10]);
    assert(t_vf.n == 1 && stack[10] == (DWORD)(uintptr_t)VfLeaveThunk);
    Sleep(5);
    const LONG us0 = k.vfThread[0][4];
    assert(VfLeave() == 0x111 && t_vf.n == 0 && k.vfThread[0][4] - us0 >= 3000 && k.vfThread[0][5] >= 3000);

    // races: a shape swap while another thread casts a ray; own rays do not count
    assert(VfCall(12) == 0x100C && k.vfSwaps == 1 && k.vfSwapsInRay == 0);
    assert(VfCall(13) == 0x100D && g_vfRayK == 1 && g_vfRayIn == 0 && k.vfRayMaxInFlight == 1);
    stack[10] = 0x222;
    VfEnter(13, &stack[10]);  // this thread is inside a ray
    assert(g_vfRayIn == 1 && t_vf.rayDepth == 1);
    VfCall(12);
    assert(k.vfSwaps == 2 && k.vfSwapsInRay == 0);
    VfOnOtherThread(12);
    assert(k.vfSwaps == 3 && k.vfSwapsInRay == 1);
    assert(VfLeave() == 0x222 && g_vfRayIn == 0 && t_vf.rayDepth == 0);
    assert(VfSlot(VF_SLOT_SWAP)[1] == 2 && VfSlot(VF_SLOT_SWAP)[2] == 1 && k.vfSwaps == 3);  // swap thread roles (I-38-3)
    // overlap groups: own nesting never counts, another thread inside the other side does (both directions)
    stack[10] = 0x777;
    VfEnter(48, &stack[10]);  // scene message handler open here
    VfCall(5);
    assert(k.vfOverlap[0] == 0 && t_vf.ov[0][0] == 1);
    VfOnOtherThread(5);
    VfOnOtherThread(48);  // side A again: no other thread inside side B
    assert(k.vfOverlap[0] == 1 && VfLeave() == 0x777 && g_vfOvIn[0][0] == 0 && t_vf.ov[0][0] == 0);
    stack[10] = 0x777;
    VfEnter(7, &stack[10]);  // a cull job open here, the scene handler on another thread
    VfOnOtherThread(48);
    assert(k.vfOverlap[0] == 2 && VfLeave() == 0x777 && g_vfOvIn[0][1] == 0);
    stack[10] = 0x777;
    VfEnter(28, &stack[10]);  // route ray: a self group
    VfCall(28);
    assert(k.vfOverlap[3] == 0);
    VfOnOtherThread(28);
    assert(k.vfOverlap[3] == 1 && VfLeave() == 0x777 && g_vfOvIn[3][0] == 0 && g_vfOvIn[3][1] == 0 && t_vf.ov[3][1] == 0);
    stack[10] = 0x777;
    VfEnter(55, &stack[10]);  // UI draw open: a layout IC on another thread is a UI overlap, an effect update an R1 one
    VfOnOtherThread(34);
    VfOnOtherThread(56);
    assert(k.vfOverlap[1] == 1 && k.vfOverlap[2] == 1 && VfLeave() == 0x777);
    stack[5] = 0x888;  // a frame left without returning gives its groups back when it is dropped
    stack[12] = 0x999;
    VfEnter(56, &stack[5]);
    assert(g_vfOvIn[2][0] == 1);
    VfEnter(0, &stack[12]);
    assert(g_vfOvIn[2][0] == 0 && t_vf.ov[2][0] == 0 && t_vf.n == 1 && VfLeave() == 0x999);
    // key list open on this thread, registration on another (and on the same thread: not an overlap)
    stack[10] = 0x333;
    VfEnter(14, &stack[10]);
    VfOnOtherThread(15);
    assert(k.vfRegDbs == 1 && k.vfKeyListOverlaps == 1);
    VfCall(15);
    assert(k.vfRegDbs == 2 && k.vfKeyListOverlaps == 1);
    assert(VfLeave() == 0x333 && g_vfKeyIn == 0 && g_vfRegIn == 0 && k.vfKeyLists == 1);
    VfCall(14);
    assert(k.vfKeyLists == 2 && k.vfKeyListOverlaps == 1);
    // a frame left without returning: dropped (and its in-flight count undone) at the next entry above it
    stack[5] = 0x555;
    stack[12] = 0x666;
    VfEnter(13, &stack[5]);
    assert(g_vfRayIn == 1);
    VfEnter(14, &stack[12]);
    assert(g_vfRayIn == 0 && t_vf.rayDepth == 0 && t_vf.n == 1 && g_vfKeyIn == 1);
    assert(VfLeave() == 0x666 && g_vfKeyIn == 0 && t_vf.n == 0);
    // 16 open frames: the 17th timed entry is untracked and its return address left alone
    memset(stack, 0, sizeof(stack));
    for (int i = 0; i < 17; i++) VfEnter(0, &stack[19 - i]);
    assert(t_vf.n == 16 && k.vfUntracked == 1 && stack[3] == 0);
    t_vf.n = 0;
    // object id lookups: chain walk on the sim thread, off-sim callers recorded once each
    assert(VfCall(16, B, 0, (DWORD)(uintptr_t)idLast) == 0x1010);
    assert(k.vfIdChain[2] == 1 && k.vfIdMaxChain == 3 && k.vfIdObjects == 3);
    VfCall(16, B, 0, (DWORD)(uintptr_t)idMiss);
    VfCall(16, B, 0, (DWORD)(uintptr_t)idFirst);
    assert(k.vfIdChain[0] == 1 && k.vfIdChain[1] == 1 && k.vfIdOffSim == 0);
    VfOnOtherThread(16, B, 0, (DWORD)(uintptr_t)idFirst);
    VfOnOtherThread(16, B, 0, (DWORD)(uintptr_t)idFirst);
    assert(k.vfIdOffSim == 2 && k.vfIdOffSimRet[0] != 0 && k.vfIdOffSimRet[1] == 0 && k.vfIdChain[1] == 1 && g_vfIdK == 5);

    // costs: package probe with the index searches inside it (outermost call only)
    stack[10] = 0x444;
    VfEnter(17, &stack[10]);
    VfCall(18);
    VfCall(18);
    VfCall(17);  // nested call: part of the outer one
    assert(VfLeave() == 0x444 && k.vfPkgCalls == 1 && k.vfPkgWalked == 2 && k.vfPkgMaxWalked == 2 && t_vf.pkgDepth == 0);
    VfCall(18);  // outside a probe: not counted
    assert(VfCall(17) == 0x1011 && k.vfPkgCalls == 2 && k.vfPkgWalked == 2);
    for (int j = 0; j < 1024; j++) VfCall(19);
    assert(k.vfW2lCallsK == 1 && g_vfW2lK == 1024);
    assert(VfCall(20) == 0x1014 && k.vfFindLotCalls == 1);
    mainTid = me;  // the consume and the UI draw run on main
    VfCall(21); VfCall(21); VfCall(22);
    assert(k.vfTreeRebuilds == 2 && k.vfTreeMaxPerFrame == 2 && k.vfFdbHolds == 1);
    VfCall(21); VfCall(22);
    assert(k.vfTreeMaxPerFrame == 2 && k.vfFdbHolds == 2 && k.vfFdbHoldMaxUs >= 0);
    assert(VfSlot(VF_SLOT_TREE)[0] == 3 && VfSlot(VF_SLOT_TREE)[1] == 0);  // tree rebuild thread roles (I-34-8)
    VfCall(23); VfCall(24);  // FrameDb lock on main: not a sim wait
    assert(k.vfFdbWaits == 0 && k.vfFdbPublishes == 0);
    // Lock_Acquire is classified by its timeout pointer [esp+4], not by ecx (the lock object)
    assert(VfCall(25, 0, VF_UI_LOCK) == 0x1019 && k.vfUiLock[0][0] == 1);
    mainTid = me + 1;
    VfCall(23); VfCall(24);
    assert(k.vfFdbWaits == 2 && k.vfFdbHolds == 2 && k.vfFdbPublishes == 1);
    assert(VfCall(25, VF_UI_LOCK, 0x12345678) == 0x1019 && k.vfUiLock[1][0] == 0);  // any other lock, even with ecx = the old filter
    VfCall(25, 0, VF_UI_LOCK);
    VfOnOtherThread(25, 0, VF_UI_LOCK);
    assert(k.vfUiLock[1][0] == 1 && k.vfUiLock[2][0] == 1 && k.vfUiLock[1][2] >= 0);
    VfCall(25, 0, VF_JOB_LOCK);
    VfOnOtherThread(25, 0, VF_JOB_LOCK);
    VfCall(25, 0, VF_SIMSVC_LOCK);
    assert(VfSlot(VF_SLOT_JOBLOCK)[1] == 1 && VfSlot(VF_SLOT_JOBLOCK)[2] == 1 && VfSlot(VF_SLOT_JOBLOCK)[4] >= 0 &&
           VfSlot(VF_SLOT_SIMSVCLOCK)[1] == 1 && VfSlot(VF_SLOT_SIMSVCLOCK)[0] == 0 && k.vfUiLock[1][0] == 1);
    assert(VfCall(26) == 0x101A && k.vfFpsGate == 1 && k.vfFpsLoops == 0);
    assert(VfCall(57) == 0x1039 && VfCall(57) == 0x1039 && k.vfFpsLoops == 2 && k.vfFpsGate == 1);
    assert(t_vf.n == 0);
    TestVerifyV22(me, stack);

    // every stub pushes its own probe index (the gate stub has none) and jumps through its own trampoline slot
    for (int i = 0; i < VF_N; i++) {
        const u8* s = (const u8*)(void*)kVfStubs[i];
        const DWORD slot = (DWORD)(uintptr_t)&g_vfTramp[i];
        bool push = i == VF_GATE || i == VF_LOOP, jmp = false;
        for (int j = 0; j < 40; j++) {
            push |= s[j] == 0x6A && s[j + 1] == i;
            jmp |= s[j] == 0xFF && s[j + 1] == 0x25 && *(const DWORD*)(s + j + 2) == slot;
        }
        assert(push && jmp);
    }

    // readouts over fake game objects; bad pointers end as -1 (SEH)
    static u8 dc[0x300], svc[0x140];
    static DWORD cache[0x30], anim[0x30], otherCache[4] = {0x12345678}, arr[6], mgr[0x30];
    *(LONG*)(dc + 0x238) = 2048;
    svc[0x139] = 1;
    cache[0] = 0xFFDD94; cache[0x94 / 4] = 3 << 20; cache[0x98 / 4] = 1 << 20; cache[0x8C / 4] = 200 << 20;
    anim[0] = 0xFFDD94; anim[0x94 / 4] = 7 << 20; anim[0x98 / 4] = 5 << 20; anim[0x8C / 4] = 15 << 20;
    // as in game: the AnimationCache (priority 1000) comes first, then another class, then the ResourceCache (priority 0)
    arr[0] = (DWORD)(uintptr_t)anim; arr[1] = 1000; arr[2] = (DWORD)(uintptr_t)otherCache; arr[4] = (DWORD)(uintptr_t)cache;
    mgr[0] = 0xFFE250; mgr[0xA0 / 4] = (DWORD)(uintptr_t)arr; mgr[0xA4 / 4] = (DWORD)(uintptr_t)(arr + 6);
    static u8 *dcp = dc, *svcp = svc, *mgrp = (u8*)mgr, *nul = NULL, *bad = (u8*)(uintptr_t)16;
    g_vfDevCfgSlot = &dcp; g_vfSimSvcSlot = &svcp; g_vfResMgrSlot = &mgrp;
    static LONG nest = 3;
    g_vfIcNestSlot = &nest;
    VfPollOnce();
    assert(k.vfMonoIcNestMax == 3 && k.vfMonoIcNestNeg == 0);
    nest = -1;
    VfPollOnce();
    assert(k.vfMonoIcNestMax == 3 && k.vfMonoIcNestNeg == 1);
    nest = 0;
    VfPollOnce();
    assert(k.vfSpeedTreeKB == -1 && k.vfVramMB == 2048 && k.vfSimSlice == 1 && k.vfRcBytesKB == 3072 && k.vfRcFlushKB == 1024 &&
           k.vfRcBudgetMB == 200 && k.vfDeviceTid == (LONG)mainTid);
    arr[5] = 1;  // no priority-0 instance: no values
    VfPollOnce();
    assert(k.vfRcBudgetMB == -1 && k.vfRcBytesKB == 0);
    g_vfSimSvcSlot = &nul; g_vfDevCfgSlot = &bad; g_vfResMgrSlot = &nul;
    VfPollOnce();
    assert(k.vfSimSlice == -1 && k.vfVramMB == -1 && k.vfRcBytesKB == 0 && k.vfRcBudgetMB == -1);

    // event file: first roles, off-sim lookup, the two overlaps
    CloseHandle(g_vfLog);
    g_vfLog = INVALID_HANDLE_VALUE;
    FILE* fp = NULL;
    assert(_wfopen_s(&fp, path, L"rb") == 0 && fp);
    static char text[65536];
    const size_t got = fread(text, 1, sizeof(text) - 1, fp);
    text[got] = 0;
    fclose(fp);
    DeleteFileW(path);
    assert(strncmp(text, "{\"t\":17", 7) == 0 && k.vfLogDropped == 0);
    assert(strstr(text, "\"kind\":\"first\",\"probe\":\"thumbTex\",\"role\":\"sim\""));
    assert(strstr(text, "\"kind\":\"first\",\"probe\":\"thumbTex\",\"role\":\"offDevice\""));
    assert(strstr(text, "\"kind\":\"first\",\"probe\":\"thumbTex\",\"role\":\"main\""));
    assert(strstr(text, "\"kind\":\"first\",\"probe\":\"texCreate\",\"role\":\"other\""));
    assert(strstr(text, "\"kind\":\"idOffSim\"") && strstr(text, "\"kind\":\"swapInRay\"") && strstr(text, "\"kind\":\"registerInKeyList\""));
    assert(strstr(text, "\"kind\":\"overlap\",\"group\":0,\"probe\":\"jobMainCull\"") && strstr(text, "\"probe\":\"routeRay\",\"role\":\"other\""));
    assert(strstr(text, "\"kind\":\"first\",\"probe\":\"jobLock\",\"role\":\"sim\"") && strstr(text, "\"probe\":\"effectUpdate\",\"role\":\"sim\""));
    assert(!strstr(text, "\"probe\":\"texUpload\",\"role\":\"main\""));
    assert(strstr(text, "\"kind\":\"first\",\"probe\":\"pumpSamePass\",\"role\":\"sim\"") && strstr(text, "\"probe\":\"lotAddManaged\",\"role\":\"sim\""));
    assert(strstr(text, "\"kind\":\"overlap\",\"group\":4,\"probe\":\"cursorApply\""));

    // install in this process: nothing matches the game's bytes, so no probe is hooked
    WritePrivateProfileStringW(L"Verify", L"Enabled", L"0", ini);
    assert(!ApplyVerify(ini));
    WritePrivateProfileStringW(L"Verify", L"Enabled", L"1", ini);
    WritePrivateProfileStringW(L"Verify", L"Threads", L"1", ini);
    WritePrivateProfileStringW(L"Verify", L"Races", L"1", ini);
    WritePrivateProfileStringW(L"Verify", L"Log", L"0", ini);
    k.vfReady = 0;
    assert(!ApplyVerify(ini) && k.vfReady == 0 && g_vfLog == INVALID_HANDLE_VALUE);
    DeleteFileW(ini);
    k.vfMode = 0;
    g_vfGroups = 0;
    VirtualFree(code, 0, MEM_RELEASE);
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
    TestSaveFlush();
    TestDecompress();
    TestAlloc();
    TestWaterJps();
    TestSaveCompress();
    TestHeightCache();
    TestIcLock();
    TestRouteGate();
    TestMonoThread();
    TestLotStream();
    TestVerify();
    puts("selftest ok");
    return 0;
}
