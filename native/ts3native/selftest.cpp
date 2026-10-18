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
static LONG __fastcall FakeLockRelease(void* lock, void*) { LeaveCriticalSection((CRITICAL_SECTION*)lock); return 0; }
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

// --- H2 locomotion window (locowin.cpp): thunk convention, hold, running at entry, drop before a wait, watchdog, nesting ---
static void* g_lpSelf;
static float g_lpDt;
static int g_lpCase, g_lpRuns;
static DWORD g_lpMs;  // case 4: how long the raw wait took
static void __fastcall FakePass(void* self, void*, float dt) {
    g_lpSelf = self;
    g_lpDt = dt;
    g_lpRuns++;
    const Ts3nCounters& c = ts3native_counters;
    u8 out[0x48];
    const int which = g_lpCase;
    g_lpCase = 0;  // a nested pass runs case 0
    switch (which) {
        case 1:  // Mode 1: a job queued inside the pass is not started before it returns (claimed at most, held)
            assert(g_g0Win == 1 && g_g0wIn == 1 && t_g0wDepth == 1);
            assert(Start(kH1));
            Sleep(60);
            assert(g_async.state != A_RUNNING && JobFor(kH1)->state != J_DONE && c.g0wJobsHeld >= 1);
            break;
        case 2:  // a job running at entry finishes inside; the one queued meanwhile is held
            assert(JobFor(kH1)->state == J_RUNNING || JobFor(kH1)->state == J_DONE);
            assert(Start(kH2));
            while (JobFor(kH1)->state != J_DONE) Sleep(1);  // waits only for the running job: allowed
            Sleep(40);
            assert(JobFor(kH2)->state != J_DONE && g_async.state != A_RUNNING);
            break;
        case 3: {  // a sim wait for a held job drops the window first and completes (no 250 ms watchdog)
            assert(Start(kH1));
            while (JobFor(kH1)->state != J_RUNNING) Sleep(0);  // claimed by the worker, start held
            Sleep(10);
            assert(g_async.state != A_RUNNING);
            const DWORD t0 = GetTickCount();
            assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == GetCurrentThreadId());  // not READY: planned here
            assert(GetTickCount() - t0 < 200 && g_g0Win == 0 && g_g0wIn == 1 && c.g0wDrop[G0W_JOBWAIT] == 1);
            break;
        }
        case 4: {  // [RouteWorker] Mode 1 sync plan while a start is held: dropped, the worker runs the job then the plan
            assert(Start(kH1));
            while (JobFor(kH1)->state != J_RUNNING) Sleep(0);
            u8 opts[0x38] = {0};
            int s[2] = {1, 2}, g[2] = {3, 4}, list[2];
            assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planTid == g_workerTid);
            assert(g_g0Win == 0 && c.g0wDrop[G0W_SYNCPLAN] == 1);
            break;
        }
        case 5: {  // a wait no G0wDrop covers: the worker lets go after 250 ms (watchdog), no deadlock
            assert(Start(kH1));
            const DWORD t0 = GetTickCount();
            while (JobFor(kH1)->state != J_DONE) Sleep(1);
            g_lpMs = GetTickCount() - t0;
            break;
        }
        case 6:  // nested pass: the inner call leaves the outer window alone
            LocoWinThunk((void*)0x77, NULL, 2.5f);
            assert(g_lpSelf == (void*)0x77 && g_lpDt == 2.5f && g_g0Win == 1 && g_g0wIn == 1 && t_g0wDepth == 1);
            g_lpSelf = self;
            g_lpDt = dt;
            break;
        case 7:  // Mode 0: count only, the job starts inside
            assert(g_g0Win == 0 && g_g0wIn == 1);
            assert(Start(kH1));
            while (JobFor(kH1)->state != J_DONE) Sleep(1);
            break;
        case 8:  // an exception leaves the pass: the window must not stay open
            RaiseException(0xE0000001, 0, 0, NULL);
    }
}
// the thunk as the game calls it: ecx = self, one float pushed, callee pops it (ret 4)
static void* volatile g_ctSelf;
static volatile float g_ctDt;
static void* g_ctThunk = (void*)LocoWinThunk;
static volatile DWORD g_ctEsp0, g_ctEsp1;
static __declspec(noinline) void CallThunk(void* self, float dt) {
    g_ctSelf = self;
    g_ctDt = dt;
    __asm {
        mov g_ctEsp0, esp
        push g_ctDt
        mov ecx, g_ctSelf
        call g_ctThunk
        mov g_ctEsp1, esp
    }
    assert(g_ctEsp0 == g_ctEsp1 && g_lpSelf == g_ctSelf && g_lpDt == g_ctDt);
}
static void Collect(DWORD h) {
    u8 out[0x48];
    WaitReady(h);
    assert(IcPlanHook(out, h) == out && *(DWORD*)(out + 0x40) == g_workerTid && *(DWORD*)(out + 4) == h);
}
static void TestLocoWindow() {
    static_assert(offsetof(Ts3nCounters, g0wReady) == 0x13E4 && offsetof(Ts3nCounters, g0wMode) == 0x13E8 &&
                  offsetof(Ts3nCounters, g0wDrop) == 0x140C && offsetof(Ts3nCounters, g0wGate0Us) == 0x1420 &&
                  sizeof(Ts3nCounters) >= 1289 * 4, "v37 layout");
    Ts3nCounters& k = ts3native_counters;
    const LONG wd0 = k.watchdogs;
    g_g0wOrig = FakePass;
    k.g0wReady = 1;
    k.g0wMode = 1;
    g_icSleepMs = 0;
    // 1. hold, through the game's calling convention
    g_lpCase = 1;
    CallThunk((void*)0x55, 1.5f);
    assert(g_g0Win == 0 && g_g0wIn == 0 && t_g0wDepth == 0 && g_rgOffSimOpen == 0);
    Collect(kH1);
    assert(k.g0wPasses == 1 && k.g0wJobsHeld == 1 && k.g0wJobStartInPass == 0 && k.g0wHoldUs >= 40000 && k.g0wRunningAtEntry == 0);
    // 2. running at entry
    g_icSleepMs = 50;
    assert(Start(kH1));
    while (g_async.state != A_RUNNING) Sleep(0);
    g_lpCase = 2;
    CallThunk((void*)0x55, 1.0f);
    assert(k.g0wRunningAtEntry == 1 && k.g0wJobStartInPass == 0 && k.g0wJobsHeld == 2);
    Collect(kH1);
    Collect(kH2);
    g_icSleepMs = 0;
    // 3. drop before WaitJobDone
    g_lpCase = 3;
    CallThunk((void*)0x55, 1.0f);
    assert(k.g0wDrop[G0W_JOBWAIT] == 1 && k.g0wWatchdog == 0 && k.g0wJobStartInPass == 1);  // it started after the drop
    // 4. drop before a sync worker plan
    k.workerReady = k.workerMode = 1;
    g_planOrig = WorkerPlan;
    g_lpCase = 4;
    CallThunk((void*)0x55, 1.0f);
    k.workerReady = k.workerMode = 0;
    Collect(kH1);
    assert(k.g0wDrop[G0W_SYNCPLAN] == 1 && k.g0wWatchdog == 0);
    // 5. watchdog: a raw wait with the window up ends after ~250 ms, logged once
    g_lpCase = 5;
    CallThunk((void*)0x55, 1.0f);
    assert(g_lpMs >= 240 && g_lpMs < 2000 && k.g0wWatchdog == 1 && k.g0wHoldMaxUs >= 240000);
    Collect(kH1);
    // 6. nested
    const LONG passes = k.g0wPasses;
    g_lpCase = 6;
    CallThunk((void*)0x55, 3.0f);
    assert(g_lpRuns > 0 && k.g0wPasses == passes + 1 && g_g0Win == 0 && g_g0wIn == 0);
    // 7. Mode 0 counts the start it would have held
    k.g0wMode = 0;
    const LONG held = k.g0wJobsHeld, inPass = k.g0wJobStartInPass;
    g_lpCase = 7;
    CallThunk((void*)0x55, 1.0f);
    assert(k.g0wJobsHeld == held && k.g0wJobStartInPass == inPass + 1);
    Collect(kH1);
    // 8. an exception through the pass closes the window
    k.g0wMode = 1;
    g_lpCase = 8;
    __try { LocoWinThunk((void*)0x55, NULL, 1.0f); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    assert(g_g0Win == 0 && g_g0wIn == 0 && t_g0wDepth == 0 && k.g0wPasses == passes + 3);
    // nothing left: no job, no window, no open off-sim frame, only the deliberate watchdog
    assert(!AsyncBusy() && g_rgOffSimOpen == 0 && k.watchdogs == wd0 && k.g0wPassMaxUs >= 240000);
    g_g0wOrig = (LocoUpdateFn)(uintptr_t)LOCO_UPDATE;
    memset(&k.g0wReady, 0, sizeof(LONG) * 16);
}

// --- A10 (a10.cpp, docs/87 §5-§6): Replan-family jobs through the real worker, M1 world and gate (TestRouteAsync's setup) ---
// fake originals: FakeIc's result plus the kind at +0x3C and the point at +0x10 (kinds 2-3)
static u8* __cdecl ArFakeReplan(u8* out, DWORD h) { FakeIc(out, h); out[0x3C] = 1; return out; }
static float g_arSeen[3];
static u8* ArFakePt(int kind, u8* out, DWORD h, float x, float y, float z) {
    FakeIc(out, h);
    out[0x3C] = (u8)kind;
    const float p[3] = {x, y, z};
    memcpy(out + 0x10, p, 12);
    memcpy(g_arSeen, p, 12);
    return out;
}
static u8* __cdecl ArFakePt2(u8* out, DWORD h, float x, float y, float z) { return ArFakePt(2, out, h, x, y, z); }
static u8* __cdecl ArFakePt3(u8* out, DWORD h, float x, float y, float z) { return ArFakePt(3, out, h, x, y, z); }
static u8 g_arActive;
static u8 __cdecl ArFakeIsActive(DWORD) { return g_arActive; }
static u8 ArStart(DWORD h, int kind, const float* pt) {
    for (int i = 0; pt && i < 3; i++) {
        DWORD b;
        memcpy(&b, &pt[i], 4);
        assert(GetOptionHook(h, b, kAsyncOptHi | kArHi | kArPtHi | (DWORD)i << 24, 1) == 1);
    }
    return GetOptionHook(h, 0, kAsyncOptHi | kArHi | (DWORD)kind << 24, 1);
}
static u8* ArCollect(int kind, u8* out, DWORD h, const float* pt) {
    switch (kind) {
        case 0: return IcPlanHook(out, h);
        case 1: return ArReplanHook(out, h);
        case 2: return ArPlanFromPointHook(out, h, pt[0], pt[1], pt[2]);
        default: return ArReplanFromPositionHook(out, h, pt[0], pt[1], pt[2]);
    }
}
// the result came from the worker's job of this kind (and point)
static bool ArFromJob(const u8* out, DWORD h, int kind, const float* pt) {
    return *(const DWORD*)(out + 4) == h && *(const DWORD*)(out + 0x40) == g_workerTid && out[0x3C] == kind &&
           (kind < 2 || memcmp(out + 0x10, pt, 12) == 0);
}
static bool ArSync(const u8* out, int kind) { return *(const DWORD*)(out + 0x40) == GetCurrentThreadId() && out[0x3C] == kind; }
static char g_arDLog[8];
static int g_arDLogN, g_arDAtJob;
static u8 __fastcall ArDRemove(void*, void*, void*) { g_arDLog[g_arDLogN++] = 'r'; return 1; }
static u8 g_arDObj[16];
static u8 __fastcall ArDFind(void*, void*, DWORD lo, DWORD, void** out) { *out = g_arDObj; return lo == 0xD; }
static void __fastcall ArDRelease(void**, void*) {}
static void* __cdecl ArDSvc() { return (void*)1; }
static u8* __cdecl ArFakePt3D(u8* out, DWORD h, float x, float y, float z) { g_arDAtJob = g_arDLogN; return ArFakePt3(out, h, x, y, z); }

static void TestA10() {
    static_assert(offsetof(Ts3nCounters, arReady) == 0x14A4 && offsetof(Ts3nCounters, arMode) == 0x14A8 &&
                  offsetof(Ts3nCounters, arStarted) == 0x14AC && offsetof(Ts3nCounters, arCollected) == 0x14B8 &&
                  offsetof(Ts3nCounters, arRefused) == 0x14C4 && offsetof(Ts3nCounters, arDropped) == 0x14D0 &&
                  offsetof(Ts3nCounters, arShadow) == 0x14DC && offsetof(Ts3nCounters, arActive) == 0x14E8 &&
                  offsetof(Ts3nCounters, arJobUs) == 0x14F4 && offsetof(Ts3nCounters, arSyncUs) == 0x1500 &&
                  offsetof(Ts3nCounters, arSyncN) == 0x150C && offsetof(Ts3nCounters, arMismatch) == 0x1518 &&
                  offsetof(Ts3nCounters, arOrphan) == 0x151C && offsetof(Ts3nCounters, arReserved) == 0x1520 && sizeof(Ts3nCounters) >= 1353 * 4, "v40 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.arReady, 0, sizeof(LONG) * 32);
    const LONG wd0 = k.watchdogs;
    const RouteIsActiveFn savedActive = g_routeIsActive;
    g_routeIsActive = ArFakeIsActive;
    g_arOrig[1] = (void*)ArFakeReplan;
    g_arOrig[2] = (void*)ArFakePt2;
    g_arOrig[3] = (void*)ArFakePt3;
    u8 out[0x48];
    const float p1[3] = {1.5f, 2.25f, -3.0f}, p2[3] = {7.0f, 0.5f, 9.75f};
    const DWORD kH3 = 0x25000003;
    g_icSleepMs = 0;

    // 1. a kind whose detour is not in place: refused, plain sync plan; the old encodings stay what they were
    k.arMode = 1;
    assert(!ArStart(kH1, 3, p1) && k.arRefused[2] == 1 && k.arStarted[2] == 0 && !JobFor(kH1));
    assert(!ArStart(kH1, 0, NULL));  // kind 0 is not an A10 START
    k.arRefused[2] = 0;
    k.arReady = kArBuilt;
    for (int kind = 1; kind <= 3; kind++) {
        const int i = kind - 1;
        if (!(kArBuilt >> i & 1)) continue;
        const float* pt = kind >= 2 ? p1 : NULL;
        k.arSyncN[i] = 0;  // the previous kind's mismatch case planned this IC once
        // 2. start -> not ready while running -> ready -> the IC takes the worker's result of its own kind
        g_icSleepMs = 30;
        assert(ArStart(kH1, kind, pt) && k.arStarted[i] == 1 && JobFor(kH1)->kind == kind);
        assert(!Ready(kH1));
        WaitReady(kH1);
        assert(ArCollect(kind, out, kH1, pt) == out && ArFromJob(out, kH1, kind, pt) && k.arCollected[i] == 1 && k.arJobUs[i] >= 25000);
        assert(!JobFor(kH1) && k.arSyncN[i] == 0);
        Cancel(kH1);
        g_icSleepMs = 0;
        // 3. refusals: nested interpreter state (NoYield), route being followed (Active), M1 (save busy); each planned sync
        FakeTask(kNested, 7);
        assert(!ArStart(kH1, kind, pt) && k.arRefused[i] == 1);
        FakeTask(kRoot, 7);
        g_arActive = 1;
        assert(!ArStart(kH1, kind, pt) && k.arActive[i] == 1 && k.arRefused[i] == 2);
        g_arActive = 0;
        k.rtBusy = 1;
        assert(!ArStart(kH1, kind, pt) && k.arRefused[i] == 3);
        k.rtBusy = 0;
        assert(ArCollect(kind, out, kH1, pt) == out && ArSync(out, kind) && k.arSyncN[i] == 1 && !JobFor(kH1));
        // kinds 2-3 need the point first (all three components since the last START)
        if (kind >= 2) {
            assert(!ArStart(kH1, kind, NULL) && k.arRefused[i] == 4 && !JobFor(kH1));
            k.arRefused[i] = 3;
        }
        // 4. Mode 0 (shadow): counted, refused, no job, nothing else touched (a done job of the route stays)
        assert(ArStart(kH2, kind, pt));
        WaitReady(kH2);
        k.arMode = 0;
        assert(!ArStart(kH2, kind, pt) && k.arShadow[i] == 1 && k.arStarted[i] == 2 && JobFor(kH2) && JobFor(kH2)->state == J_DONE);
        k.arMode = 1;
        // 5. the same route started again drops the older request (its caller is gone)
        assert(ArStart(kH2, kind, pt) && k.arDropped[i] == 1);
        WaitReady(kH2);
        assert(ArCollect(kind, out, kH2, pt) == out && ArFromJob(out, kH2, kind, pt) && k.arCollected[i] == 2);
        Cancel(kH2);
        // 6. kind mismatch: another IC of the same route meets the job: dropped, that IC plans here, logged once
        const LONG mm = k.arMismatch;
        assert(ArStart(kH2, kind, pt));
        WaitReady(kH2);
        const int other = kind < 3 && (kArBuilt >> kind & 1) ? kind + 1 : 0;
        assert(ArCollect(other, out, kH2, p2) == out && ArSync(out, other) && k.arMismatch == mm + 1 && k.arDropped[i] == 2 && !JobFor(kH2));
        Cancel(kH2);
        // ... and Route.Plan's job met by this IC
        assert(Start(kH2));
        WaitReady(kH2);
        assert(ArCollect(kind, out, kH2, pt) == out && ArSync(out, kind) && k.arMismatch == mm + 2 && !JobFor(kH2));
        Cancel(kH2);
        if (kind >= 2) {  // another start point is another plan
            assert(ArStart(kH2, kind, p1));
            WaitReady(kH2);
            assert(ArCollect(kind, out, kH2, p2) == out && ArSync(out, kind) && memcmp(g_arSeen, p2, 12) == 0 && k.arMismatch == mm + 3);
            Cancel(kH2);
        }
        // 7. a job queued behind another route's running one, its IC called before it started: taken back, planned here
        g_icSleepMs = 40;
        assert(Start(kH1) && ArStart(kH2, kind, pt));
        const LONG dr = k.arDropped[i];
        assert(ArCollect(kind, out, kH2, pt) == out && ArSync(out, kind) && k.arDropped[i] == dr + 1);
        WaitReady(kH1);
        assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == g_workerTid && out[0x3C] == 0);
        g_icSleepMs = 0;
        // 8. armed by READY, then async switched off before the IC: the job is drained, the IC plans here (tripwire)
        const LONG orphan = k.arOrphan;
        assert(ArStart(kH2, kind, pt));
        WaitReady(kH2);
        k.asyncMode = 0;
        assert(ArCollect(kind, out, kH2, pt) == out && ArSync(out, kind) && k.arOrphan == orphan + 1);
        k.asyncMode = 1;
        // 9. CANCEL while running (Sim reset): waited for, dropped; the next START is not refused
        g_icSleepMs = 30;
        assert(ArStart(kH1, kind, pt));
        Cancel(kH1);
        assert(g_async.state == A_IDLE && !JobFor(kH1) && ArStart(kH1, kind, pt));
        WaitReady(kH1);
        assert(ArCollect(kind, out, kH1, pt) == out && ArFromJob(out, kH1, kind, pt));
        g_icSleepMs = 0;
        // 10. a destroy during the job (RT_DESTROY, closure #4) waits for it; the result is still collected after
        k.rtReady = RT_ALL;
        g_icSleepMs = 40;
        assert(ArStart(kH3, kind, pt));
        while (g_async.state != A_RUNNING) Sleep(0);
        DWORD stack[24] = {};
        stack[20] = 0x999;
        stack[21] = 0x77;
        static DWORD simTid;
        simTid = GetCurrentThreadId();  // the destroy comes from the sim thread
        const DWORD* const savedSim = g_rtSimTidSlot;
        g_rtSimTidSlot = &simTid;
        RtEnter(7, &stack[20]);
        g_rtSimTidSlot = savedSim;
        assert(JobFor(kH3)->state == J_DONE && g_rgOffSimOpen == 1);
        assert(RgLeave() == 0x999 && g_rgOffSimOpen == 0);
        assert(Ready(kH3) && ArCollect(kind, out, kH3, pt) == out && ArFromJob(out, kH3, kind, pt));
        k.rtReady = 0;
        g_icSleepMs = 0;
        assert(!AsyncBusy() && g_rgOffSimOpen == 0);
    }

    // 11. design D: a discourage Remove deferred during a Replan job is applied at the next A10 START, before that job runs
    {
        u8* world = (u8*)g_rgLock - 0x918;
        const u8 w974 = world[0x974];
        world[0x974] = 1;  // DiscWorldOk
        LONG* const depth = (LONG*)((u8*)g_rgLock + 0x20);  // the game lock's count: the drain's release ends the ownership
        *depth = 1;
        *(DWORD*)(g_arDObj + 8) = 0xD;
        g_discRemoveOrig = ArDRemove;
        g_findRef = ArDFind;
        g_objSvc = ArDSvc;
        g_objRelease = ArDRelease;
        k.discMode = 1;
        g_icSleepMs = 30;
        assert(ArStart(kH1, 3, p1));
        while (g_async.state != A_RUNNING) Sleep(0);
        assert(DiscRemoveHook(world, NULL, g_arDObj) == 1 && g_discCount == 1 && g_arDLogN == 0);  // deferred
        while (JobFor(kH1)->state != J_DONE) Sleep(1);  // no READY in between (READY is a drain point too)
        g_arOrig[3] = (void*)ArFakePt3D;
        g_arDAtJob = -1;
        assert(ArStart(kH2, 3, p1) && g_discCount == 0 && g_arDLogN == 1);  // drained by the START, then queued
        WaitReady(kH1);
        assert(ArCollect(3, out, kH1, p1) == out && ArFromJob(out, kH1, 3, p1));
        WaitReady(kH2);
        assert(ArCollect(3, out, kH2, p1) == out && ArFromJob(out, kH2, 3, p1) && g_arDAtJob == 1);  // the job ran after the apply
        g_arOrig[3] = (void*)ArFakePt3;
        g_icSleepMs = 0;
        k.discMode = 0;
        world[0x974] = w974;
        *depth = 0;
        memset(&k.discReady, 0, offsetof(Ts3nCounters, discOverflow) + 4 - offsetof(Ts3nCounters, discReady));
    }

    // 12. random order over 4 routes and all kinds (incl. Route.Plan): START / READY / IC / CANCEL; every collected result
    // is its own route's and kind's, nothing is left at the end
    {
        static const DWORD kHs[4] = {0x25000011, 0x25000012, 0x25000013, 0x25000014};
        int want[4] = {-1, -1, -1, -1};  // kind of the START each route last got answered yes, -1 = none
        float pts[4][3];
        DWORD seed = 0xA10;
        auto rnd = [&seed](DWORD n) { seed = seed * 1103515245 + 12345; return (seed >> 16) % n; };
        long collected = 0, sync = 0;
        for (int step = 0; step < 600; step++) {
            const int r = (int)rnd(4), op = (int)rnd(4);
            const DWORD h = kHs[r];
            g_icSleepMs = rnd(3) ? 0 : rnd(4) + 1;
            if (op == 0) {
                int kind = (int)rnd(4);
                if (kind && !(kArBuilt >> (kind - 1) & 1)) kind = 0;  // an IC this build does not hook: Route.Plan
                for (int c = 0; c < 3; c++) pts[r][c] = (float)rnd(100) * 0.25f;
                const u8 ok = kind ? ArStart(h, kind, pts[r]) : Start(h);
                want[r] = ok ? kind : -1;
            } else if (op == 1 && want[r] >= 0) {  // the C# side: READY loop, then the original method's IC
                WaitReady(h);
                assert(ArCollect(want[r], out, h, pts[r]) == out && ArFromJob(out, h, want[r], pts[r]));
                Cancel(h);
                want[r] = -1;
                collected++;
            } else if (op == 2) {  // an IC call nobody armed (another caller of the route): plans here
                int kind = (int)rnd(4);
                if (kind && !(kArBuilt >> (kind - 1) & 1)) kind = 0;  // an IC this build does not hook: Route.Plan
                assert(ArCollect(kind, out, h, pts[r]) == out && ArSync(out, kind) && !JobFor(h));
                want[r] = -1;
                sync++;
            } else {
                Cancel(h);
                want[r] = -1;
            }
        }
        for (int r = 0; r < 4; r++) Cancel(kHs[r]);
        g_icSleepMs = 0;
        assert(collected > 20 && sync > 20 && !AsyncBusy() && g_rgOffSimOpen == 0 && g_async.state == A_IDLE);
        for (int i = 0; i < kJobs; i++) assert(g_jobs[i].state == J_FREE);
    }
    assert(k.watchdogs == wd0);
    g_routeIsActive = savedActive;
    memset(g_arOrig, 0, sizeof(g_arOrig));
    memset(&k.arReady, 0, sizeof(LONG) * 32);
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
    // H6 M1: START needs the gate's closures and a quiet world; the worker then holds the world's +0x918 for the job
    static u8 m1World[0xA00], m1P[0x200], m1Wm[0x200];
    *(u8**)(m1World + 0x95C) = m1P;
    *(u8**)(m1World + 0x964) = m1Wm;
    CRITICAL_SECTION* m1Lock = (CRITICAL_SECTION*)(m1World + 0x918);
    InitializeCriticalSection(m1Lock);
    g_rgLock = m1Lock;
    g_rgRelOrig = FakeLockRelease;
    assert(!Start(kH1) && c.asyncRefusedM1[0] == 1 && c.asyncStarted == 0);  // closures off
    ts3native_counters.rgEnforce = 1;

    // started -> not ready while running -> ready -> the next Route.Plan takes the worker's result
    g_icSleepMs = 30;
    assert(Start(kH1) && c.asyncStarted == 1);
    assert(!Ready(kH1));
    assert(Ready(kH2));  // no job for that route: "ready", nothing armed
    WaitReady(kH1);
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == g_workerTid && *(DWORD*)(out + 4) == kH1);
    assert(c.asyncCollected == 1 && g_async.state == A_IDLE && g_async.lock == m1Lock);
    assert(TryEnterCriticalSection(m1Lock));  // the worker released the job's lock
    LeaveCriticalSection(m1Lock);
    Cancel(kH1);

    // CANCEL after READY (destination went invalid, Plan returned before the internal call): the result is not handed to a later plan
    assert(Start(kH1));
    WaitReady(kH1);
    Cancel(kH1);
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == me && c.asyncDropped == 1 && c.asyncCollected == 1);

    // 0.11.3 queue: another route's START while a job runs is queued behind it; a plain plan of the running route waits
    // for it, drops the result and plans itself; the queued one runs next on the worker and is collected
    g_icSleepMs = 50;
    assert(Start(kH1));
    assert(Start(kH2) && c.asyncRefused == 1 && c.asyncQueueMax >= 2);  // 1 = the M1 refusal above
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == me && c.asyncDropped == 2);
    WaitReady(kH2);
    assert(IcPlanHook(out, kH2) == out && *(DWORD*)(out + 0x40) == g_workerTid && c.asyncCollected == 2 && !AsyncBusy());
    // a queued job that has not started yet is taken back by its route's own plan and planned on this thread
    g_icSleepMs = 50;
    assert(Start(kH1) && Start(kH2));
    assert(IcPlanHook(out, kH2) == out && *(DWORD*)(out + 0x40) == me && c.asyncDropped == 3);
    WaitReady(kH1);
    assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == g_workerTid && c.asyncCollected == 3 && !AsyncBusy());
    // the same route started again drops its older request; a result whose Sim vanished is reclaimed when the queue is full
    g_icSleepMs = 0;
    assert(Start(kH1));
    WaitReady(kH1);
    assert(Start(kH1) && c.asyncDropped == 4);
    WaitReady(kH1);
    Cancel(kH1);
    assert(c.asyncDropped == 5 && !JobFor(kH1));
    for (DWORD i = 0; i < 8; i++) assert(Start(0x25000100 + i));
    for (DWORD i = 0; i < 8; i++) WaitReady(0x25000100 + i);
    const LONG full0 = c.asyncQueueFull;
    assert(!Start(kH2) && c.asyncQueueFull == full0 + 1);  // 8 uncollected results, all fresh
    g_jobs[3].doneTick -= kDoneKeepMs;
    assert(Start(kH2) && c.asyncDropped == 6 && !JobFor(0x25000103));
    WaitReady(kH2);
    assert(IcPlanHook(out, kH2) == out && *(DWORD*)(out + 0x40) == g_workerTid && c.asyncCollected == 4);
    for (DWORD i = 0; i < 8; i++) Cancel(0x25000100 + i);
    assert(c.asyncDropped == 13 && !AsyncBusy());
    // the counts below continue from the single-slot test: dropped 3, collected 2 (relative checks keep them readable)
    ts3native_counters.asyncDropped = 3;
    ts3native_counters.asyncCollected = 2;

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
    while (g_async.state != A_RUNNING) Sleep(0);  // the worker has claimed it (queued jobs start a moment later)
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planTid == me);
    assert(JobFor(kH1) && JobFor(kH1)->state == J_DONE && g_invokeTid == me && c.invokes == inv0 + 2);
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
    assert(JobFor(kH1) != NULL);
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

    // v35: a long sync plan after a refused START is counted under its reason; short ones and LogFallbackMs=0 are not
    static_assert(offsetof(Ts3nCounters, rfbLogMs) == 0x1384 && offsetof(Ts3nCounters, rfbMs) == 0x1388 &&
                  offsetof(Ts3nCounters, rfbN) == 0x1394 && offsetof(Ts3nCounters, rfbMax) == 0x13A0 &&
                  sizeof(Ts3nCounters) >= 1257 * 4, "v35 layout");
    Ts3nCounters& w = ts3native_counters;
    w.rtBusy = 1;  // save busy: M1 refuses (asyncRefusedM1[1])
    g_icSleepMs = 60;
    assert(!Start(kH1) && IcPlanHook(out, kH1) == out && w.rfbN[0] == 0);  // LogFallbackMs=0: not timed
    w.rfbLogMs = 55;
    assert(!Start(kH1) && IcPlanHook(out, kH1) == out && w.rfbN[0] == 1 && w.rfbMs[0] >= 55);
    assert((w.rfbMax & 0xFF) == RFB_M1 + 1 && (w.rfbMax >> 8) >= 55 && g_rfbLines == 1 && g_rfbHandle == 0);
    assert(IcPlanHook(out, kH2) == out && w.rfbN[2] == 1);  // no START for this route: NoStart
    w.rtBusy = 0;
    g_icSleepMs = 0;
    assert(Start(kH1));
    WaitReady(kH1);
    assert(IcPlanHook(out, kH1) == out);  // async: collected, not counted
    assert(w.rfbN[0] == 1 && w.rfbN[1] == 0 && w.rfbN[2] == 1);
    for (int i = 0; i < 30; i++) RfbNote(RFB_QFULL, 300, "nojob", kH1, NULL);
    assert(w.rfbN[1] == 30 && w.rfbMs[1] == 9000 && (w.rfbMax >> 8) == 300 && (w.rfbMax & 0xFF) == RFB_QFULL);
    assert(g_rfbLines == (DWORD)kRfbLinesPerMin && g_rfbDropped == 30 - (kRfbLinesPerMin - 2));  // rate limit: 20 lines/min
    memset(&w.rfbLogMs, 0, sizeof(LONG) * 8);
    TestA10();         // A10 on the same setup
    TestLocoWindow();  // H2 on the same async setup (real worker, M1 world, gate enforced)
    ts3native_counters.rgEnforce = 0;
    ts3native_counters.rgOwnerTid = 0;
    ts3native_counters.rgAcquires = 0;  // the worker took m1Lock as the learned lock
    g_rgLock = NULL;
    g_async.lock = NULL;
    DeleteCriticalSection(m1Lock);
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

    // docs/67 double free on one thread: with Dedupe the second Free is dropped (counted), the bin keeps one link, and two
    // Mallocs of that size get two different chunks (without it they got the same chunk twice)
    {
        c.allocDedupe = 1;
        const LONG d0 = c.allocDupFrees;
        void* a = EaMallocHook(kHeapA, NULL, 300, 0);
        const int f0 = g_fakeFrees;
        EaFreeHook(kHeapA, NULL, a);
        EaFreeHook(kHeapA, NULL, a);
        assert(c.allocDupFrees == d0 + 1 && g_fakeFrees == f0);
        void* x = EaMallocHook(kHeapA, NULL, 300, 0);
        void* y = EaMallocHook(kHeapA, NULL, 300, 0);
        assert(x == a && y != a);
        EaFreeHook(kHeapA, NULL, x);
        EaFreeHook(kHeapA, NULL, y);
        EaFreeHook(kHeapA, NULL, x);  // dup behind another link: found by the walk
        assert(c.allocDupFrees == d0 + 2);
        // switch off: still counted, old behaviour (pushed again)
        c.allocDedupe = 0;
        void* u = EaMallocHook(kHeapA, NULL, 300, 0);
        EaFreeHook(kHeapA, NULL, u);
        EaFreeHook(kHeapA, NULL, u);
        assert(c.allocDupFrees == d0 + 3);
        // count 0 with a head (what the old double free left behind): AcPop treats the bin as empty
        AcThread* tt = (AcThread*)FlsGetValue(g_acFls);
        AcHeap* hh = AcFind(tt, kHeapA, false);
        AcBin* bb = &hh->bins[AcChunk(300) / 16 - 1];
        const DWORD keep = bb->count;
        bb->count = 0;
        const int mm = g_fakeMallocs;
        assert(EaMallocHook(kHeapA, NULL, 300, 0) != u && g_fakeMallocs == mm + 1);
        tt->bytes -= keep * AcChunk(300);  // drop the looped bin before later tests
        bb->head = NULL;
    }

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

// --- [TerrainGrave] (H6 S3a): the game's PhysShape_Release bytes under our detour, called from a fake swap site ---
static int g_tgDtors;
static void* g_tgLastDtor;
static void __fastcall TgFakeDtor(void* self, void*, int flag) { g_tgDtors++; g_tgLastDtor = self; assert(flag == 1); }
static void* g_tgFakeVtbl[1] = {(void*)TgFakeDtor};
struct TgShape { void** vtbl; u8 pad[0x48]; LONG count; };  // count at +0x4C
static_assert(offsetof(TgShape, count) == 0x4C, "shape layout");
static volatile LONG g_tgSeenInFlight;
static double __fastcall TgFakeRay(void*, void*, float, float) { g_tgSeenInFlight = g_tgInFlight; return 1.0; }

static void TestTerrainGrave() {
    Ts3nCounters& k = ts3native_counters;
    // PhysShape_Release 0x682760 as in the exe [verified disassembly]: add [ecx+4Ch],-1; push esi; mov esi,[ecx+4Ch];
    // jne +8; mov eax,[ecx]; mov edx,[eax]; push 1; call edx; mov eax,esi; pop esi; ret
    static const u8 kRelease[] = {0x83, 0x41, 0x4C, 0xFF, 0x56, 0x8B, 0x71, 0x4C, 0x75, 0x08, 0x8B, 0x01, 0x8B, 0x10, 0x6A, 0x01,
                                  0xFF, 0xD2, 0x8B, 0xC6, 0x5E, 0xC3};
    u8* code = (u8*)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    u8* rel = code;
    memcpy(rel, kRelease, sizeof(kRelease));
    // the swap site: ecx = [esp+4]; call rel; ret  (the return address after the call stands for 0xC2A896)
    u8* swap = code + 64;
    swap[0] = 0x8B; swap[1] = 0x4C; swap[2] = 0x24; swap[3] = 0x04;
    swap[4] = 0xE8;
    *(LONG*)(swap + 5) = (LONG)(rel - (swap + 9));
    swap[9] = 0xC3;
    typedef void(__cdecl* SwapFn)(void* shape);
    typedef LONG(__fastcall* RelFn)(void* shape, void*);
    const SwapFn Swap = (SwapFn)(void*)swap;
    const RelFn Release = (RelFn)(void*)rel;
    g_tgSwapRet = (DWORD)(uintptr_t)(swap + 9);
    g_tgTramp = InstallDetour("graveyard release", (DWORD)(uintptr_t)rel, kTgPrologue, sizeof(kTgPrologue), (void*)TgStub);
    assert(g_tgTramp);
    static TgShape shapes[70];
    for (TgShape& s : shapes) { s.vtbl = g_tgFakeVtbl; s.count = 1; }

    // other callers and the shared count: the game's release (count 2 -> 1, returns the new count, no destructor)
    shapes[0].count = 2;
    assert(Release(&shapes[0], NULL) == 1 && g_tgDtors == 0);
    // switch off: the swap frees at once; with a ray in flight that free is a race
    k.tgMode = 0;
    Swap(&shapes[0]);
    assert(shapes[0].count == 0 && g_tgDtors == 1 && g_tgLastDtor == &shapes[0] && k.vfRtGraveRaces == 0);
    g_tgInFlight = 1;
    Swap(&shapes[1]);
    assert(g_tgDtors == 2 && k.vfRtGraveRaces == 1);
    shapes[2].count = 2;  // shared: not freed, not a race
    Swap(&shapes[2]);
    assert(g_tgDtors == 2 && shapes[2].count == 1 && k.vfRtGraveRaces == 1);
    // switch on: parked (count untouched, no destructor) until a rebuild entry sees no ray in flight
    k.tgMode = 1;
    Swap(&shapes[3]);
    assert(shapes[3].count == 1 && g_tgDtors == 2 && k.vfRtGraveParked == 1 && k.vfRtGraveHeld == 1 && k.vfRtGraveRaces == 1);
    assert(Release(&shapes[4], NULL) == 0 && g_tgDtors == 3);  // not the swap: the game's release even when on
    TgOnSwap();  // a ray is in flight: nothing released
    assert(g_tgDtors == 3 && k.vfRtGraveHeld == 1 && k.vfSwaps == 1 && k.vfSwapsInRay == 1);
    g_tgInFlight = 0;
    TgOnSwap();
    assert(g_tgDtors == 4 && g_tgLastDtor == &shapes[3] && shapes[3].count == 0 && k.vfRtGraveFreed == 1 && k.vfRtGraveHeld == 0 &&
           k.vfSwaps == 2 && k.vfSwapsInRay == 1);
    // ring full: the 65th park waits for no ray, drains the 64, then parks itself
    for (int i = 5; i < 69; i++) Swap(&shapes[i]);
    assert(k.vfRtGraveHeld == 64 && g_tgDtors == 4);
    Swap(&shapes[69]);
    assert(k.vfRtGraveSpins == 1 && g_tgDtors == 68 && k.vfRtGraveFreed == 65 && k.vfRtGraveHeld == 1 && k.vfRtGraveParked == 66);
    TgOnSwap();
    assert(g_tgDtors == 69 && g_tgLastDtor == &shapes[69] && k.vfRtGraveHeld == 0);
    // switching off with nothing parked: back to the game's release
    k.tgMode = 0;
    shapes[0].count = 1;
    Swap(&shapes[0]);
    assert(g_tgDtors == 70 && k.vfRtGraveParked == 66);

    // the ray counts itself in flight only while the graveyard is installed
    const HcFn savedRay = g_hcOrig;
    g_hcOrig = TgFakeRay;
    g_tgOn = false;
    HcOrig(NULL, NULL, 0, 0);
    assert(g_tgSeenInFlight == 0);
    g_tgOn = true;
    assert(HcOrig(NULL, NULL, 0, 0) == 1.0 && g_tgSeenInFlight == 1 && g_tgInFlight == 0);
    k.heightMode = 1;  // no table (graveyard alone): the switch cannot reach the cache
    assert(HcHook(NULL, NULL, 0, 0) == 1.0 && g_tgSeenInFlight == 1);
    k.heightMode = 0;
    g_tgOn = false;
    g_hcOrig = savedRay;

    // Verify's release probe is counted inside this stub
    assert(VfOwnerSlot(TG_RELEASE) == &g_tgVf);
    g_tgTramp = NULL;  // the detour stays on the test code, which nothing calls again
    assert(!VfOwnerSlot(TG_RELEASE));
    memset(&k.tgReady, 0, offsetof(Ts3nCounters, vfRtGraveHeld) + 4 - offsetof(Ts3nCounters, tgReady));
    k.vfSwaps = k.vfSwapsInRay = 0;
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

// --- [JobQueue] (v23): sim record mask switch and ExecuteJob counters on fake records and jobs ---
static void TestJobQueue() {
    static_assert(offsetof(Ts3nCounters, jqReady) == 0xBF0 && offsetof(Ts3nCounters, jqSimMode) == 0xBF4 &&
                  offsetof(Ts3nCounters, jqWorkerJobs) == 0xC14 && offsetof(Ts3nCounters, d3dMtReady) == 0xC18 &&
                  offsetof(Ts3nCounters, allocDedupe) == 0xC1C && offsetof(Ts3nCounters, allocDupFrees) == 0xC20, "v23 layout");
    // only the dedicated sim record (0x80000004) is switched; the merged main+sim record (0x80000005) and others never
    assert(JqSimMask(0x80000004, 1) == 4 && JqSimMask(4, 0) == 0x80000004 && JqSimMask(4, 1) == 4 && JqSimMask(0x80000004, 0) == 0x80000004);
    assert(JqSimMask(0x80000005, 1) == 0x80000005 && JqSimMask(0x80000001, 1) == 0x80000001 && JqSimMask(0x80000000, 1) == 0x80000000 &&
           JqSimMask(2, 1) == 2);
    Ts3nCounters& k = ts3native_counters;
    __declspec(align(4)) u8 job[0x50] = {};
    *(DWORD*)(job + 0x10) = kJqRenderFns[2];
    *(LONG*)(job + 0x28) = -1;
    DWORD simTid = 0;
    g_jqSimTid = &simTid;
    const LONG r0 = k.jqRenderJobs, w0 = k.jqWorkerJobs, t0 = k.jqWorkerTids;
    JqNote(job);  // this thread is neither main (g_mainTid) nor sim
    assert(k.jqRenderJobs == r0 + 1 && k.jqWorkerJobs == w0 + 1 && k.jqWorkerTids == t0 + 1);
    JqNote(job);  // same thread: not a new worker
    assert(k.jqWorkerTids == t0 + 1 && k.jqWorkerJobs == w0 + 2);
    *(LONG*)(job + 0x28) = 4;  // sim-only job on a worker: not shared, not counted as worker work
    JqNote(job);
    assert(k.jqWorkerJobs == w0 + 2 && k.jqRenderJobs == r0 + 3);
    *(DWORD*)(job + 0x10) = 0;  // no fn: completes without running, ignored
    JqNote(job);
    assert(k.jqRenderJobs == r0 + 3);
    // as the sim thread: shared and frame jobs counted as sim work
    simTid = GetCurrentThreadId();
    *(DWORD*)(job + 0x10) = kJqRenderFns[0];
    *(LONG*)(job + 0x28) = -1;
    const LONG s0 = k.jqSimSharedJobs, sr0 = k.jqSimRenderJobs;
    JqNote(job);
    assert(k.jqSimSharedJobs == s0 + 1 && k.jqSimRenderJobs == sr0 + 1 && k.jqWorkerJobs == w0 + 2);
    // the slot stub flips bit 31 of the sim record per the switch and counts passes; other threads' records untouched
    static int calls;
    struct Fake { static DWORD __fastcall Run(void*, void*, u8*, DWORD, DWORD) { calls++; return 1; } };
    g_jqRunJobsOrig = Fake::Run;
    __declspec(align(4)) u8 rec[0x44] = {};
    *(DWORD*)(rec + 0x2C) = 0x80000004;
    k.jqSimMode = 1;
    const LONG p0 = k.jqSimPumps, sp0 = k.jqSimSkipPasses;
    assert(JqRunJobsHook(NULL, NULL, rec, 0, 1) == 1 && *(DWORD*)(rec + 0x2C) == 4 && k.jqSimSkipPasses == sp0 + 1);
    k.jqSimMode = 0;
    assert(JqRunJobsHook(NULL, NULL, rec, 0, 1) == 1 && *(DWORD*)(rec + 0x2C) == 0x80000004 && k.jqSimSkipPasses == sp0 + 1);
    assert(k.jqSimPumps == p0 + 2 && calls == 2);
    *(DWORD*)(rec + 0x2C) = 0x80000005;  // merged main+sim record: never switched
    k.jqSimMode = 1;
    JqRunJobsHook(NULL, NULL, rec, 0, 1);
    assert(*(DWORD*)(rec + 0x2C) == 0x80000005 && k.jqSimPumps == p0 + 2);
    simTid = 0;
    k.jqSimMode = 0;
    // #14: one Init bit per worker, clamped to 1-4
    assert(JqWorkerBits(1) == 1 && JqWorkerBits(2) == 3 && JqWorkerBits(3) == 7 && JqWorkerBits(4) == 0xF && JqWorkerBits(9) == 0xF &&
           JqWorkerBits(0) == 1);
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
                  offsetof(Ts3nCounters, vfShaderStartup) == 0xBE0 && offsetof(Ts3nCounters, vfMonoIcNestNeg) == 0xBEC && sizeof(Ts3nCounters) >= 764 * 4 &&
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

// [LotSinCos]: the fake game function is plain C (x87 sin/cos, then float math as docs/63's model); the hook must return
// the same bits with the cache cold, warm, off, and through Verify.
static int g_fakeW2LCalls;
static bool g_fakeW2LLie;
static void __fastcall FakeW2L(void* lotp, void*, float x, float z, float* outX, float* outZ) {
    g_fakeW2LCalls++;
    const u8* lot = (const u8*)lotp;
    float s, c;
    LscSinCos((const float*)(lot + 0x1A0), &s, &c);
    const float dx = x - *(const float*)(lot + 0x190), dz = z - *(const float*)(lot + 0x198);
    const float ns = g_lscNegZero - s;
    const float p1 = ns * dz, p2 = c * dx, p3 = s * dx, p4 = c * dz;
    *outX = p1 + p2;
    *outZ = p3 + p4;
    if (g_fakeW2LLie) *outX += 1.0f;
}

static void TestLotSinCos() {
    static_assert(offsetof(Ts3nCounters, lscReady) == 0xC24 && offsetof(Ts3nCounters, lscMode) == 0xC28 &&
                  offsetof(Ts3nCounters, lscMismatches) == 0xC34 && offsetof(Ts3nCounters, idcMode) == 0xC3C &&
                  offsetof(Ts3nCounters, idcObjects) == 0xC5C && sizeof(Ts3nCounters) >= 792 * 4, "v24 layout");
    Ts3nCounters& k = ts3native_counters;
    g_lscOrig = FakeW2L;
    __declspec(align(16)) static u8 lots[4][0x200];
    const float angles[6] = {0.0f, -0.0f, 1.5707964f, 3.1415927f, -2.1f, 5.0e3f};
    unsigned seed = 12345;
    auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (float)((seed >> 8) & 0xFFFF) / 65536.0f; };
    k.lscMode = 1;
    g_lscVerify = false;
    memset(g_lsc, 0, sizeof(g_lsc));
    for (int i = 0; i < 4000; i++) {
        u8* lot = lots[i & 3];
        *(float*)(lot + 0x190) = rnd() * 2048.0f;
        *(float*)(lot + 0x198) = rnd() * 2048.0f;
        *(float*)(lot + 0x1A0) = (i % 7 == 6) ? (rnd() - 0.5f) * 12.0f : angles[i % 6];  // repeats (hits) and fresh angles
        const float x = *(float*)(lot + 0x190) + (rnd() - 0.5f) * 128.0f, z = *(float*)(lot + 0x198) + (rnd() - 0.5f) * 128.0f;
        float ax, az, bx, bz;
        const int calls = g_fakeW2LCalls;
        LscHook(lot, NULL, x, z, &ax, &az);
        assert(g_fakeW2LCalls == calls);  // mode 1 never runs the game's function without Verify
        FakeW2L(lot, NULL, x, z, &bx, &bz);
        assert(LscBits(ax) == LscBits(bx) && LscBits(az) == LscBits(bz));
    }
    // fixed angles only: each misses at most once, then hits (random angles above may have evicted them)
    const unsigned __int64 h0 = g_lscHits;
    for (int i = 0; i < 600; i++) {
        *(float*)(lots[1] + 0x1A0) = angles[i % 6];
        float ax, az;
        LscHook(lots[1], NULL, 1.f, 2.f, &ax, &az);
    }
    assert(g_lscHits - h0 >= 594);
    // -0.0 and +0.0 are different keys (fsin keeps the sign): both answers stay exact
    float o1[2], o2[2];
    *(float*)(lots[0] + 0x1A0) = -0.0f; LscHook(lots[0], NULL, 5.f, 7.f, &o1[0], &o1[1]);
    *(float*)(lots[0] + 0x1A0) = 0.0f; LscHook(lots[0], NULL, 5.f, 7.f, &o2[0], &o2[1]);
    float r[2];
    FakeW2L(lots[0], NULL, 5.f, 7.f, &r[0], &r[1]);
    assert(LscBits(o2[0]) == LscBits(r[0]) && LscBits(o2[1]) == LscBits(r[1]));
    // Verify: a hit that disagrees with the game is counted and the game's bits are returned
    g_lscVerify = true;
    g_fakeW2LLie = true;
    const LONG m0 = k.lscMismatches;
    LscHook(lots[0], NULL, 5.f, 7.f, &o1[0], &o1[1]);
    assert(k.lscMismatches == m0 + 1 && LscBits(o1[0]) == LscBits(r[0] + 1.0f) && LscBits(o1[1]) == LscBits(r[1]));
    g_fakeW2LLie = false;
    LscHook(lots[0], NULL, 5.f, 7.f, &o1[0], &o1[1]);
    assert(k.lscMismatches == m0 + 1 && LscBits(o1[0]) == LscBits(r[0]));
    // a torn entry (check word not matching) is a miss, never a wrong answer
    g_lscVerify = false;
    LscEntry& e = LscSlot(LscBits(0.0f));
    e.s = 0.75f;
    const int calls = g_fakeW2LCalls;
    LscHook(lots[0], NULL, 5.f, 7.f, &o1[0], &o1[1]);
    assert(LscBits(o1[0]) == LscBits(r[0]) && LscBits(o1[1]) == LscBits(r[1]) && g_fakeW2LCalls == calls);
    // mode 0: the game's function only
    k.lscMode = 0;
    LscHook(lots[0], NULL, 5.f, 7.f, &o1[0], &o1[1]);
    assert(g_fakeW2LCalls == calls + 1);
    // install in this process: Enabled=0 does nothing; Enabled=1 finds no matching prologue in our own image
    wchar_t ini[MAX_PATH];
    GetTempPathW(MAX_PATH, ini);
    wcscat_s(ini, L"ts3n-lsc-test.ini");
    WritePrivateProfileStringW(L"LotSinCos", L"Enabled", L"0", ini);
    assert(!ApplyLotSinCos(ini));
    DeleteFileW(ini);
}

// [IdCache]: a fake bucket array with the game's layout (buckets at +0, sentinel at +0x1024, count at +0x1028; node id at
// +8/+0xC, next at +0x10). FakeIdLookup is the game's lookup as a C model (also checked against the exe by tools/emu
// object_id_lookup.py); FakeIdErase unlinks like 0x938D00.
static int g_fakeIdLookups;
static DWORD* __fastcall FakeIdLookup(u8* tbl, void*, DWORD* out, const DWORD* id) {
    g_fakeIdLookups++;
    u8** b = (u8**)(tbl + (id[0] % 0x409) * 4);
    for (u8* n = *b; n; n = *(u8**)(n + 0x10))
        if (*(DWORD*)(n + 8) == id[0] && *(DWORD*)(n + 0xC) == id[1]) { out[0] = (DWORD)(uintptr_t)n; out[1] = (DWORD)(uintptr_t)b; return out; }
    out[0] = *(DWORD*)(tbl + 0x1024);
    out[1] = (DWORD)(uintptr_t)(tbl + 0x1024);
    return out;
}
static void* __fastcall FakeIdErase(u8* tbl, void*, void* out, u8* node, void* bucket) {
    u8** p = (u8**)bucket;
    while (*p != node) p = (u8**)(*p + 0x10);
    *p = *(u8**)(node + 0x10);
    --*(LONG*)(tbl + 0x1028);
    return out;
}
static void IdTestInsert(u8* tbl, u8* node, DWORD lo, DWORD hi) {
    *(DWORD*)(node + 8) = lo;
    *(DWORD*)(node + 0xC) = hi;
    u8** b = (u8**)(tbl + (lo % 0x409) * 4);
    *(u8**)(node + 0x10) = *b;
    *b = node;
    ++*(LONG*)(tbl + 0x1028);
}

static void TestIdCache() {
    Ts3nCounters& k = ts3native_counters;
    static u8 tbl[0x1040];
    static u8 nodes[3000][0x20];
    static DWORD simTid;
    simTid = GetCurrentThreadId();
    g_idcSimTid = &simTid;
    g_idcLookupOrig = FakeIdLookup;
    g_idcEraseOrig = FakeIdErase;
    g_idcTbl = NULL;
    *(DWORD*)(tbl + 0x1024) = 0xE11D;  // sentinel value
    unsigned seed = 777;
    auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return seed; };
    for (int i = 0; i < 3000; i++) IdTestInsert(tbl, nodes[i], rnd(), rnd());  // ~3 per bucket, as in play
    k.idcMode = 1;
    g_idcVerify = false;
    // every answer (hit, fill, miss) equals the game's, iterator included
    auto same = [&](DWORD lo, DWORD hi) {
        const DWORD id[2] = {lo, hi};
        DWORD a[2], b[2];
        const int calls = g_fakeIdLookups;
        assert(IdcLookupHook(tbl, NULL, a, id) == a);
        assert(g_fakeIdLookups == calls);  // mode 1 on the sim thread never runs the game's walk
        FakeIdLookup(tbl, NULL, b, id);
        assert(a[0] == b[0] && a[1] == b[1]);
        return a[0];
    };
    for (int r = 0; r < 4; r++)
        for (int i = 0; i < 3000; i += 7) same(*(DWORD*)(nodes[i] + 8), *(DWORD*)(nodes[i] + 0xC));
    assert(g_idcHits > 1000 && g_idcFills >= 400 && g_idcSteps > 0);
    assert(same(1, 2) == 0xE11D && g_idcNotFound == 1);  // not in the hash: sentinel, nothing stored
    assert(same(1, 2) == 0xE11D && g_idcNotFound == 2);
    // erase drops the entry: the next lookup misses (sentinel), never the freed node
    u8* victim = nodes[21];
    const DWORD vlo = *(DWORD*)(victim + 8), vhi = *(DWORD*)(victim + 0xC);
    assert(same(vlo, vhi) == (DWORD)(uintptr_t)victim);
    IdcEraseHook(tbl, NULL, NULL, victim, tbl + (vlo % 0x409) * 4);
    assert(k.idcErases == 1 && same(vlo, vhi) == 0xE11D);
    // inserted again: found and cached again
    IdTestInsert(tbl, victim, vlo, vhi);
    assert(same(vlo, vhi) == (DWORD)(uintptr_t)victim);
    // erase while the switch is off still clears: switching back on cannot return a stale node
    k.idcMode = 0;
    IdcEraseHook(tbl, NULL, NULL, victim, tbl + (vlo % 0x409) * 4);
    k.idcMode = 1;
    assert(same(vlo, vhi) == 0xE11D);
    // the bucket array emptied behind our back (constructor re-run): no hit on a cached node
    {
        u8* n = nodes[84];
        const DWORD lo = *(DWORD*)(n + 8), hi = *(DWORD*)(n + 0xC);
        assert(same(lo, hi) == (DWORD)(uintptr_t)n);
        u8** b = (u8**)(tbl + (lo % 0x409) * 4);
        u8* keep = *b;
        *b = NULL;
        assert(same(lo, hi) == 0xE11D);
        *b = keep;
    }
    // Verify: a wrong entry (id still matches, node no longer in the chain) is caught, dropped and the walk's answer returned
    {
        u8* n = nodes[99];
        const DWORD lo = *(DWORD*)(n + 8), hi = *(DWORD*)(n + 0xC);
        assert(same(lo, hi) == (DWORD)(uintptr_t)n);
        FakeIdErase(tbl, NULL, NULL, n, tbl + (lo % 0x409) * 4);  // unlinked without our hook
        g_idcVerify = true;
        const LONG m = k.idcMismatches;
        assert(same(lo, hi) == 0xE11D && k.idcMismatches == m + 1 && IdcSlot(lo, hi).node == NULL);
        g_idcVerify = false;
    }
    // other threads and other tables go to the game
    {
        const DWORD id[2] = {*(DWORD*)(nodes[5] + 8), *(DWORD*)(nodes[5] + 0xC)};
        DWORD a[2];
        int calls = g_fakeIdLookups;
        simTid = GetCurrentThreadId() + 1;
        IdcLookupHook(tbl, NULL, a, id);
        assert(g_fakeIdLookups == calls + 1 && a[0] == (DWORD)(uintptr_t)nodes[5]);
        simTid = GetCurrentThreadId();
        static u8 other[0x1040];
        IdcLookupHook(other, NULL, a, id);
        assert(g_fakeIdLookups == calls + 2);
        k.idcMode = 0;
        IdcLookupHook(tbl, NULL, a, id);
        assert(g_fakeIdLookups == calls + 3);
    }
    IdcPublish(tbl);
    assert(k.idcObjects == 2998 && k.idcLookupsK == (LONG)(g_idcLookups / 1000));
    wchar_t ini[MAX_PATH];
    GetTempPathW(MAX_PATH, ini);
    wcscat_s(ini, L"ts3n-idc-test.ini");
    WritePrivateProfileStringW(L"IdCache", L"Enabled", L"0", ini);
    assert(!ApplyIdCache(ini));
    DeleteFileW(ini);
}

// --- H6 S2b: gate sites 13-15, exact G11 window, docs/60 §2.2 count-only sites, Verify counted inside an owner stub ---
// Runs after TestVerify (its role slots point at this thread as the sim thread).
static void TestRouteSites() {
    static_assert(offsetof(Ts3nCounters, rgUnlocked2) == 0xC60 && offsetof(Ts3nCounters, rgJob2) == 0xC6C &&
                  offsetof(Ts3nCounters, rtReady) == 0xC78 && offsetof(Ts3nCounters, vfRtDestroyInJob) == 0xC94 &&
                  offsetof(Ts3nCounters, vfRtLotIdxRebuildInJob) == 0xCA4 && offsetof(Ts3nCounters, vfRtPendingInJob) == 0xCB4 &&
                  offsetof(Ts3nCounters, vfRtTeardownInJob) == 0xCD0 && offsetof(Ts3nCounters, g11Plans) == 0xCD4 &&
                  offsetof(Ts3nCounters, g11End) == 0xCE4 && offsetof(Ts3nCounters, tgReady) == 0xCFC &&
                  offsetof(Ts3nCounters, tgMode) == 0xD00 && offsetof(Ts3nCounters, vfRtGraveHeld) == 0xD14 &&
                  sizeof(Ts3nCounters) >= 838 * 4, "v25 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.rgUnlocked2, 0, offsetof(Ts3nCounters, vfRtGraveHeld) + 4 - offsetof(Ts3nCounters, rgUnlocked2));
    const DWORD me = GetCurrentThreadId(), inf = 0xFFFFFFFF;
    static DWORD simTid;
    simTid = me;
    g_rtSimTidSlot = &simTid;
    const DWORD savedWorker = g_workerTid;
    const LockAcquireFn savedLock = g_lockOrig;
    g_lockOrig = RgFakeAcquire;
    g_rgRelOrig = RgFakeRelease;
    InitializeCriticalSection(&g_rgFakeCs);
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    k.rgReady = 7;
    g_rgLock = g_rgLockObj;
    k.rgOwnerTid = 0;
    DWORD stack[24] = {};
    auto reset = [&] { t_rg.n = 0; t_rg.readers = 0; };

    // gate sites 13-15 count in the v25 arrays
    RgEnter(12, &stack[20]);
    assert(k.rgUnlocked2[0] == 1 && k.rgJob2[0] == 0 && t_rg.n == 1 && stack[20] == (DWORD)(uintptr_t)RgLeaveThunk);
    reset();
    g_callerTid = 1234;
    RgEnter(14, &stack[20]);
    assert(k.rgUnlocked2[2] == 1 && k.rgJob2[2] == 1);
    g_callerTid = 0;
    reset();

    // G11: plan, then a gate entry closes the window (min(plan, gap) <= both); a second touch adds nothing
    assert(G11PlanBegin());
    Sleep(2);
    G11PlanEnd(true);
    assert(k.g11Plans == 0 && t_g11.end.QuadPart != 0);
    Sleep(1);
    RgEnter(13, &stack[20]);
    reset();
    assert(k.g11Plans == 1 && k.g11End[G11_GATE13] == 1 && k.g11PlanUs >= 1000 && k.g11MovableUs > 0 &&
           k.g11MovableUs <= k.g11PlanUs && k.g11MovableUs <= k.g11GapUs);
    G11Touch(G11_LOCK, 1);
    assert(k.g11Plans == 1);
    // nested plans: the inner one opens nothing and touches inside a plan are ignored
    assert(G11PlanBegin() && G11PlanBegin());
    G11Touch(G11_LOCK, 1);
    G11PlanEnd(true);
    assert(t_g11.end.QuadPart == 0 && k.g11Plans == 1);
    G11PlanEnd(true);
    assert(t_g11.end.QuadPart != 0);
    // an outermost +0x918 acquire closes it; a recursive one does not count again
    assert(LockAcquireHook(g_rgLockObj, NULL, &inf) == 1 && k.g11End[G11_LOCK] == 1 && k.g11Plans == 2);
    assert(LockReleaseHook(g_rgLockObj, NULL) == 0);
    // the next plan closes an open window
    G11PlanBegin(); G11PlanEnd(true);
    G11PlanBegin();
    assert(k.g11End[G11_PLAN] == 1 && k.g11Plans == 3);
    G11PlanEnd(true);
    // destroys and quadtree writes on the sim thread close it too (rtsites.cpp)
    k.rtReady = 1;
    RtEnter(7, &stack[20]);
    assert(k.g11End[G11_DESTROY] == 1 && k.g11Plans == 4 && k.vfRtDestroyInJob == 0 && k.vfRtDestroyOffSim == 0);
    G11PlanBegin(); G11PlanEnd(true);
    RtEnter(3, &stack[20]);
    assert(k.g11End[G11_QTWRITE] == 1 && k.g11Plans == 5 && k.vfRtQtWriteInJob == 0);
    // not the sim thread: no plan window; an unlocked gate entry counts as off-sim
    simTid = me + 1;
    assert(!G11PlanBegin());
    RgEnter(0, &stack[20]);
    assert(k.vfRtGateOffSim == 1);
    reset();

    // the worker: quadtree leaves outside a reader are bare; readers push frames (not gate frames)
    g_workerTid = me;
    RtEnter(5, &stack[20]);
    assert(k.vfRtQtBare == 1 && t_rg.n == 0);
    stack[20] = 0x111; stack[15] = 0x222; stack[8] = 0x333;
    RtEnter(0, &stack[20]);
    RtEnter(1, &stack[15]);  // nested reader: one outermost read
    assert(k.vfRtQtReads == 1 && t_rg.n == 2 && t_rg.readers == 2 && stack[15] == (DWORD)(uintptr_t)RgLeaveThunk);
    RtEnter(6, &stack[10]);
    assert(k.vfRtQtBare == 1);
    const LONG unl0 = k.rgUnlocked[0];
    RgEnter(0, &stack[8]);  // a gate entry inside a reader is still outermost; the worker is not off-sim
    assert(k.rgUnlocked[0] == unl0 + 1 && k.vfRtGateOffSim == 1 && t_rg.n == 3 && t_rg.readers == 2);
    assert(RgLeave() == 0x333 && RgLeave() == 0x222 && t_rg.readers == 1 && RgLeave() == 0x111 && t_rg.n == 0 && t_rg.readers == 0);
    RtEnter(0, &stack[10]);
    RtEnter(5, &stack[12]);  // above the reader's slot: the reader frame is dead, so this read is bare
    assert(k.vfRtQtBare == 2 && t_rg.n == 0 && t_rg.readers == 0);
    // pending lot updates applied by the worker: ecx = world (pushad slot -2), P = [world+0x95C], flag P+0x10E
    static u8 world[0xA00], planner[0x200];
    *(u8**)(world + 0x95C) = planner;
    stack[18] = (DWORD)(uintptr_t)world;
    RtEnter(9, &stack[20]);
    assert(k.vfRtPendingInJob == 0);
    planner[0x10E] = 1;
    RtEnter(9, &stack[20]);
    assert(k.vfRtPendingInJob == 1);
    RtEnter(8, &stack[20]);
    assert(k.vfRtLotGridWorker == 1);
    g_workerTid = savedWorker;
    RtEnter(9, &stack[20]);  // not the worker: not counted
    RtEnter(8, &stack[20]);
    assert(k.vfRtPendingInJob == 1 && k.vfRtLotGridWorker == 1);

    // during a job (Mode 1 caller set): every in-job counter; outside a job none
    simTid = me;
    g_callerTid = 1234;
    for (int i = 2; i < RT_N; i++) if (i != 5 && i != 6) RtEnter(i, &stack[20]);
    assert(k.vfRtQtWriteInJob == 3 && k.vfRtDestroyInJob == 1 && k.vfRtDestroyOffSim == 0 && k.vfRtLotIdxRebuildInJob[1] == 1 &&
           k.vfRtDrainSave == 1 && k.vfRtDrainBrd == 1 && k.vfRtDrainLotSave == 1 && k.vfRtDebugSkip == 1 && k.vfRtTeardownInJob == 3);
    g_callerTid = 0;
    for (int i = 2; i < RT_N; i++) if (i != 5 && i != 6) RtEnter(i, &stack[20]);
    assert(k.vfRtQtWriteInJob == 3 && k.vfRtDestroyInJob == 1 && k.vfRtDrainSave == 1 && k.vfRtTeardownInJob == 3);
    simTid = me + 1;
    RtEnter(7, &stack[20]);
    assert(k.vfRtDestroyOffSim == 1);
    simTid = me;
    k.rtReady = 0;
    RtEnter(7, &stack[20]);  // not installed: nothing
    assert(k.vfRtDestroyOffSim == 1 && k.vfRtDestroyInJob == 1);

    // every stub pushes its own site index and jumps through its own trampoline slot
    for (int i = 0; i < RT_N; i++) {
        const u8* s = (const u8*)(void*)kRtStubs[i];
        const DWORD slot = (DWORD)(uintptr_t)&g_rtTramp[i];
        bool push = false, jmp = false;
        for (int j = 0; j < 40; j++) {
            push |= s[j] == 0x6A && s[j + 1] == i;
            jmp |= s[j] == 0xFF && s[j + 1] == 0x25 && *(const DWORD*)(s + j + 2) == slot;
        }
        assert(push && jmp);
    }

    // Verify counted inside the owner stub (docs/68 §6 rule 3): the owner table, then a timed thread probe through RtEnter
    assert(!VfOwnerSlot(0x00939420) && !VfOwnerSlot(0x00628A90));
    g_rtTramp[7] = (void*)1;
    g_rgTramp[12] = (void*)1;
    assert(VfOwnerSlot(0x00939420) == &g_rtVf[7] && VfOwnerSlot(0x00628A90) == &g_rgVf[12] && !VfOwnerSlot(0x00939421));
    g_rtTramp[7] = g_rgTramp[12] = NULL;
    int probe = -1;
    for (int i = 0; i < VF_N; i++) if (kVf[i].at == 0x00939420) probe = i;
    assert(probe >= 0 && kVf[probe].thr != VF_NONE);
    k.vfMode = 1;
    k.rtReady = 1;
    g_rtVf[7] = probe + 1;
    const LONG sims = VfSlot(kVf[probe].thr)[1];
    stack[20] = 0x777;
    RtEnter(7, &stack[20]);
    assert(VfSlot(kVf[probe].thr)[1] == sims + 1 && stack[20] == (DWORD)(uintptr_t)VfLeaveThunk && VfRealRet(&stack[20]) == 0x777);
    assert(VfLeave() == 0x777 && t_vf.n == 0 && VfRealRet(&stack[20]) == (DWORD)(uintptr_t)VfLeaveThunk);
    g_rtVf[7] = 0;
    k.vfMode = 0;
    k.rtReady = 0;

    reset();
    memset(&t_g11, 0, sizeof(t_g11));
    k.rgReady = 0;
    g_rgLock = NULL;
    g_lockOrig = savedLock;
    DeleteCriticalSection(&g_rgFakeCs);
}

// --- H6 S3b + M1: gate enforced during an async job, borrow rule, closures, START limits (routegate.cpp, rtsites.cpp) ---
static LONG __fastcall RgTryAcquire(void* lock, void*, const DWORD* deadline) {  // 0x4E16F0 with deadline 0 = try once
    if (*deadline == 0) {
        if (!TryEnterCriticalSection(&g_rgFakeCs)) return -2;
    } else {
        EnterCriticalSection(&g_rgFakeCs);
    }
    return ++*(LONG*)((u8*)lock + 0x20);
}
static HANDLE g_enfHeld;
static DWORD WINAPI EnfWorker(LPVOID) {  // plays the worker: holds the job's lock for 30 ms, then ends the job
    static const DWORD inf = 0xFFFFFFFF;
    LockAcquireHook(g_rgLockObj, NULL, &inf);
    SetEvent(g_enfHeld);
    Sleep(30);
    LockReleaseHook(g_rgLockObj, NULL);
    InterlockedExchange(&g_async.state, A_IDLE);
    SetEvent(g_asyncDone);
    return 0;
}
static HANDLE EnfStartWorker() {
    ResetEvent(g_enfHeld);
    ResetEvent(g_asyncDone);
    InterlockedExchange(&g_async.state, A_RUNNING);
    HANDLE w = CreateThread(NULL, 0, EnfWorker, NULL, 0, NULL);
    WaitForSingleObject(g_enfHeld, INFINITE);
    return w;
}
static void EnfJoin(HANDLE w) {
    WaitForSingleObject(w, INFINITE);
    CloseHandle(w);
}

static void TestRouteEnforce() {
    static_assert(offsetof(Ts3nCounters, rgEnforce) == 0xD18 && offsetof(Ts3nCounters, asyncRefusedM1) == 0xD30 &&
                  offsetof(Ts3nCounters, vfRtDestroyInInvoke) == 0xD48 && offsetof(Ts3nCounters, rtClosing) == 0xD54 &&
                  sizeof(Ts3nCounters) >= 854 * 4, "v26 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.rgEnforce, 0, offsetof(Ts3nCounters, rtClosing) + 4 - offsetof(Ts3nCounters, rgEnforce));
    const DWORD me = GetCurrentThreadId(), inf = 0xFFFFFFFF;
    static DWORD simTid;
    simTid = me;
    g_rtSimTidSlot = &simTid;
    const DWORD savedWorker = g_workerTid;
    const LockAcquireFn savedLock = g_lockOrig;
    const HANDLE savedDone = g_asyncDone, savedInv = g_invReady;
    g_lockOrig = RgTryAcquire;
    g_rgRelOrig = RgFakeRelease;
    InitializeCriticalSection(&g_rgFakeCs);
    InitializeCriticalSection(&g_qtCs);
    g_asyncDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_invReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_enfHeld = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    k.rgReady = 7;
    k.rtReady = RT_ALL;
    k.rgEnforce = 1;
    k.rgOwnerTid = 0;
    g_workerTid = me + 7;  // nobody: the test thread is sim and owner
    g_async.owner = me;
    g_async.lock = g_rgLockObj;
    LONG& count = *(LONG*)(g_rgLockObj + 0x20);
    DWORD stack[24] = {};
    auto reset = [&] { t_rg.n = 0; t_rg.readers = 0; g_rgOffSimOpen = 0; };

    // M1: each refusal reason, then allowed with the world's lock
    static u8 world[0xA00], planner[0x200], wm[0x200];
    *(u8**)(world + 0x95C) = planner;
    *(u8**)(world + 0x964) = wm;
    g_rgLock = world + 0x918;
    void* lock = NULL;
    k.rgEnforce = 0;
    assert(!RtM1Allows(&lock) && k.asyncRefusedM1[0] == 1);
    k.rgEnforce = 1;
    k.rtBusy = 1;
    assert(!RtM1Allows(&lock) && k.asyncRefusedM1[1] == 1);
    k.rtBusy = 0;
    k.rtClosing = 1;
    assert(!RtM1Allows(&lock) && k.asyncRefusedM1[2] == 1);
    k.rtClosing = 0;
    planner[0x10E] = 1;
    assert(!RtM1Allows(&lock) && k.asyncRefusedM1[3] == 1);
    planner[0x10E] = 0;
    wm[0x1CC] = 1;
    assert(!RtM1Allows(&lock) && k.asyncRefusedM1[4] == 1);
    wm[0x1CC] = 0;
    k.rgOwnerTid = (LONG)me;
    assert(!RtM1Allows(&lock) && k.asyncRefusedM1[5] == 1);
    k.rgOwnerTid = 0;
    assert(RtM1Allows(&lock) && lock == world + 0x918 && !lock == false);
    g_rgLock = NULL;
    assert(!RtM1Allows(&lock) && k.asyncRefusedM1[0] == 2);  // no lock learned in this world yet
    g_rgLock = g_rgLockObj;

    // borrow: the owner inside a callback for the job takes no real lock, and releases without one
    g_async.state = A_RUNNING;
    g_inInvoke = 1;
    assert(LockAcquireHook(g_rgLockObj, NULL, &inf) == 2 && count == 0 && k.rgBorrows == 1 && t_borrow == 1);
    assert(LockReleaseHook(g_rgLockObj, NULL) == 1 && t_borrow == 0 && count == 0);
    RgEnter(0, &stack[20]);
    assert(k.rgBorrows == 2 && t_rg.n == 1 && t_rg.f[0].rel == RG_REL_OFFSIM && k.rgGateWaits == 0);
    reset();
    RtEnter(7, &stack[20]);  // a destroy there cannot wait for the job
    assert(k.vfRtDestroyInInvoke == 1 && k.vfRtDestroyWaitUs == 0);
    g_inInvoke = 0;
    g_async.state = A_IDLE;

    // a gate entry during a job waits for the worker's hold (pumping), holds +0x918 for the call, the leave releases it
    HANDLE w = EnfStartWorker();
    stack[20] = 0x444;
    RgEnter(0, &stack[20]);
    assert(k.rgGateWaits == 1 && k.rgGateWaitUs >= 10000 && count == 1 && t_rg.n == 1 && t_rg.f[0].rel == (RG_REL_LOCK | RG_REL_OFFSIM));
    assert(k.rgOwnerTid == (LONG)me);
    RgEnter(1, &stack[16]);  // nested: no second acquire
    assert(count == 1 && k.rgGateWaits == 1 && t_rg.n == 2);
    RgLeave();
    assert(RgLeave() == 0x444 && count == 0 && t_rg.n == 0 && g_rgOffSimOpen == 0 && k.rgOwnerTid == 0);
    EnfJoin(w);
    assert(g_async.state == A_IDLE);
    // no job running: the gate takes nothing
    g_async.state = A_IDLE;
    RgEnter(0, &stack[20]);
    assert(count == 0 && t_rg.f[0].rel == RG_REL_OFFSIM && k.rgGateWaits == 1);
    reset();

    // destroy and save during a job wait for it; the save refuses START until it returns
    w = EnfStartWorker();
    RtEnter(7, &stack[20]);
    assert(g_async.state == A_IDLE && k.vfRtDestroyWaitUs >= 10000 && t_rg.n == 1 && g_rgOffSimOpen == 1);
    EnfJoin(w);
    w = EnfStartWorker();
    stack[20] = 0x555;
    RtEnter(11, &stack[20]);
    assert(g_async.state == A_IDLE && k.vfRtDrainWaitUs >= 10000 && k.rtBusy == 1 && t_rg.n == 1);
    EnfJoin(w);
    g_rgLock = world + 0x918;
    assert(!RtM1Allows(&lock) && k.asyncRefusedM1[1] == 2);
    g_rgLock = g_rgLockObj;
    assert(RgLeave() == 0x555 && k.rtBusy == 0);
    // a thread that really holds +0x918 does not wait (the worker cannot be in its job body)
    g_async.state = A_RUNNING;
    k.rgOwnerTid = (LONG)me;
    RtEnter(7, &stack[20]);
    assert(g_async.state == A_RUNNING);
    k.rgOwnerTid = 0;

    // the routing debug frame is skipped during a job only
    assert(RtEnter(14, &stack[20]) == 1);
    g_async.state = A_IDLE;
    assert(RtEnter(14, &stack[20]) == 0);

    // quadtree: a worker reader holds g_qtCs until its leave; a mutator during a job takes it, outside a job not
    g_workerTid = me;
    stack[20] = 0x666;
    RtEnter(0, &stack[20]);
    assert(t_rg.n == 1 && t_rg.f[0].rel == RG_REL_QT && g_qtCs.RecursionCount == 1);
    assert(RgLeave() == 0x666 && g_qtCs.RecursionCount == 0);
    // 0.11.1: the extra readers lock too; a bare leaf read on the worker takes the lock as a fallback (and still counts)
    stack[20] = 0x676;
    RtEnter(20, &stack[20]);
    assert(t_rg.n == 1 && t_rg.f[0].rel == RG_REL_QT && g_qtCs.RecursionCount == 1);
    assert(RgLeave() == 0x676 && g_qtCs.RecursionCount == 0);
    const LONG bare0 = k.vfRtQtBare;
    stack[20] = 0x686;
    RtEnter(5, &stack[20]);
    assert(k.vfRtQtBare == bare0 + 1 && t_rg.n == 1 && g_qtCs.RecursionCount == 1);
    assert(RgLeave() == 0x686 && g_qtCs.RecursionCount == 0);
    g_workerTid = me + 7;
    g_async.state = A_RUNNING;
    stack[20] = 0x777;
    RtEnter(2, &stack[20]);
    assert(t_rg.n == 1 && g_qtCs.RecursionCount == 1);
    assert(RgLeave() == 0x777 && g_qtCs.RecursionCount == 0);
    g_async.state = A_IDLE;
    RtEnter(2, &stack[20]);
    assert(t_rg.n == 1 && g_rgOffSimOpen == 1 && g_qtCs.RecursionCount == 0);

    // travel / quit ICs set the closing flag; world init clears it and forgets the lock
    RtEnter(18, &stack[20]);
    RtEnter(19, &stack[20]);
    assert(k.rtClosing == 1 && k.vfRtClosingDrains == 2);
    RtEnter(15, &stack[20]);
    assert(k.rtClosing == 0 && g_rgLock == NULL);
    g_rgLock = g_rgLockObj;

    // an off-sim thread inside a gate leaf is published until its leave
    simTid = me + 1;
    stack[20] = 0x888;
    RgEnter(0, &stack[20]);
    assert(g_rgOffSimOpen == 1 && t_rg.f[0].rel == RG_REL_OFFSIM);
    assert(RgLeave() == 0x888 && g_rgOffSimOpen == 0);
    simTid = me;

    // a gate frame that held +0x918 and was left without returning is released at the next entry
    LockAcquireHook(g_rgLockObj, NULL, &inf);
    RgPush(t_rg, &stack[20], 0, RG_REL_LOCK, g_rgLockObj);
    RgEnter(0, &stack[22]);
    assert(k.rgDropLocked == 1 && count == 0 && t_rg.n == 1);
    reset();

    g_async.state = A_IDLE;
    g_async.lock = NULL;
    g_async.owner = 0;
    k.rgEnforce = 0;
    k.rgReady = 0;
    k.rtReady = 0;
    k.rgOwnerTid = 0;
    g_rgLock = NULL;
    g_workerTid = savedWorker;
    g_lockOrig = savedLock;
    CloseHandle(g_asyncDone);
    CloseHandle(g_invReady);
    CloseHandle(g_enfHeld);
    g_asyncDone = savedDone;
    g_invReady = savedInv;
    DeleteCriticalSection(&g_qtCs);
    DeleteCriticalSection(&g_rgFakeCs);
}

// --- vfG11CloseBy (0.11.2): each G11 close is keyed by its closer; a +0x918 acquire inside a steering query by its caller ---
static void TestG11CloseBy() {
    static_assert(offsetof(Ts3nCounters, g11CloseKey) == 0xD58 && offsetof(Ts3nCounters, g11CloseN) == 0xDD8 &&
                  offsetof(Ts3nCounters, g11CloseLostUs) == 0xE58 && offsetof(Ts3nCounters, g11CloseOther) == 0xED8 &&
                  sizeof(Ts3nCounters) >= 951 * 4, "v27 layout");
    static_assert(offsetof(Ts3nCounters, asyncSkipped) == 0xEDC && offsetof(Ts3nCounters, asyncQueueMax) == 0xEE8 &&
                  sizeof(Ts3nCounters) >= 955 * 4, "v28 layout");
    static_assert(offsetof(Ts3nCounters, blockKey) == 0xEEC && offsetof(Ts3nCounters, blockOther) == 0x106C &&
                  sizeof(Ts3nCounters) >= 1052 * 4, "v29 layout");
    static_assert(offsetof(Ts3nCounters, discReady) == 0x1070 && offsetof(Ts3nCounters, discOverflow) == 0x109C &&
                  sizeof(Ts3nCounters) >= 1064 * 4, "v30 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.g11CloseKey, 0, offsetof(Ts3nCounters, g11CloseOther) + 4 - offsetof(Ts3nCounters, g11CloseKey));
    const DWORD me = GetCurrentThreadId(), inf = 0xFFFFFFFF;
    static DWORD simTid;
    simTid = me;
    g_rtSimTidSlot = &simTid;
    const LockAcquireFn savedLock = g_lockOrig;
    g_lockOrig = RgTryAcquire;
    g_rgRelOrig = RgFakeRelease;
    InitializeCriticalSection(&g_rgFakeCs);
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    k.rgReady = 7;
    k.rtReady = RT_ALL;
    g_rgLock = g_rgLockObj;
    memset(&t_g11, 0, sizeof(t_g11));
    DWORD stack[24] = {};

    // a plan, then the steering query called from 0x85FE10 takes +0x918: the close is keyed by that call site
    G11PlanBegin();
    Sleep(1);
    G11PlanEnd(true);
    stack[20] = 0x0085FE10;
    RtEnter(23, &stack[20]);
    assert(t_steerRet == 0x0085FE10 && t_rg.n == 1);
    RtEnter(24, &stack[16]);  // nested gather: keeps the outer caller
    assert(t_steerRet == 0x0085FE10 && t_rg.n == 1);
    assert(LockAcquireHook(g_rgLockObj, NULL, &inf) == 1);
    assert(k.g11CloseKey[0] == 0x0085FE10 && k.g11CloseN[0] == 1 && k.g11CloseLostUs[0] >= 0);
    LockReleaseHook(g_rgLockObj, NULL);
    assert(RgLeave() == 0x0085FE10 && t_steerRet == 0 && t_rg.n == 0);
    // outside a steering query an acquire is keyed by Lock_Acquire's return address (here: inside this test)
    G11PlanBegin();
    G11PlanEnd(true);
    LockAcquireHook(g_rgLockObj, NULL, &inf);
    LockReleaseHook(g_rgLockObj, NULL);
    assert(k.g11CloseKey[1] != 0 && k.g11CloseKey[1] != 0x0085FE10 && k.g11CloseN[1] == 1);
    // a gate entry and a repeat of the first key
    G11PlanBegin();
    G11PlanEnd(true);
    RgEnter(0, &stack[20]);
    t_rg.n = 0;
    t_rg.readers = 0;
    assert(k.g11CloseKey[2] == 0x01000000 && k.g11CloseN[2] == 1);
    G11PlanBegin();
    G11PlanEnd(true);
    G11Touch(G11_LOCK, 0x0085FE10);
    assert(k.g11CloseN[0] == 2 && k.g11CloseKey[3] == 0);
    // a full table: the 33rd key counts as other
    for (DWORD i = 0; i < 40; i++) {
        G11PlanBegin();
        G11PlanEnd(true);
        G11Touch(G11_LOCK, 0x00900000 + i);
    }
    assert(k.g11CloseKey[31] != 0 && k.g11CloseOther == 40 - 29);

    k.rgReady = 0;
    k.rtReady = 0;
    k.rgOwnerTid = 0;
    k.rgAcquires = 0;
    g_rgLock = NULL;
    g_lockOrig = savedLock;
    memset(&t_g11, 0, sizeof(t_g11));
    DeleteCriticalSection(&g_rgFakeCs);
}

// --- design D (0.12.0, docs/84 §2.4): discourage footprints deferred while a job runs, applied in order later ---
static char g_dLog[64];
static int g_dLogN;
static u8 g_dObjA[16], g_dObjB[16];
static bool g_dNestOnce;
static u8 __fastcall FakeDiscAdd(void* world, void*, void* obj, void* points, int, int, int) {
    const u8* const* v = (const u8* const*)points;
    g_dLog[g_dLogN++] = obj == g_dObjA ? 'A' : 'B';
    assert(v && ((v[1] - v[0]) / 16 < 3 || *(const float*)v[0] == 1.5f));  // 3+ points: the copy made at defer time
    assert(((uintptr_t)v[0] & 15) == 0);  // the original reads them with movaps (0xAB88BD)
    if (g_dNestOnce) { g_dNestOnce = false; DiscRemoveHook(world, NULL, g_dObjB); }  // an event handler during the drain
    return 1;
}
static u8 __fastcall FakeDiscRemove(void* world, void*, void* obj) {
    g_dLog[g_dLogN++] = obj == g_dObjA ? 'a' : 'b';
    return world ? 1 : 0;
}
static void* __cdecl FakeSvc() { return (void*)1; }
static bool g_dObjBGone;
static u8 __fastcall FakeFindRef(void*, void*, DWORD lo, DWORD, void** out) {  // same convention as 0x9395A0 (ret 0xC)
    void* o = lo == 0xA ? (void*)g_dObjA : g_dObjBGone ? NULL : (void*)g_dObjB;
    if (o) *out = o;
    return o != NULL;
}
static int g_dReleases;
static void __fastcall FakeObjRelease(void** pp, void*) { assert(*pp); g_dReleases++; }

static void TestDiscourage() {
    Ts3nCounters& k = ts3native_counters;
    memset(&k.discReady, 0, offsetof(Ts3nCounters, discOverflow) + 4 - offsetof(Ts3nCounters, discReady));
    const DWORD me = GetCurrentThreadId();
    const LockAcquireFn savedLock = g_lockOrig;
    g_lockOrig = RgTryAcquire;
    g_rgRelOrig = RgFakeRelease;
    InitializeCriticalSection(&g_rgFakeCs);
    const HANDLE savedDone = g_asyncDone, savedInv = g_invReady;
    g_asyncDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_invReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_discAddOrig = FakeDiscAdd;
    g_discRemoveOrig = FakeDiscRemove;
    g_objSvc = FakeSvc;
    g_findRef = FakeFindRef;
    g_objRelease = FakeObjRelease;
    static u8 world[0xA00];
    world[0x974] = 1;
    g_rgLock = world + 0x918;
    *(DWORD*)(g_dObjA + 8) = 0xA;
    *(DWORD*)(g_dObjB + 8) = 0xB;
    k.discMode = 1;
    g_async.owner = me;
    __declspec(align(16)) float pts[3][4] = {{1.5f}, {2}, {3}};
    u8* vec[3] = {(u8*)pts, (u8*)pts + 48, (u8*)pts + 48};
    g_dLogN = 0;

    // no job: straight to the original
    assert(DiscAddHook(world, NULL, g_dObjA, vec, 0, 1, 0) == 1 && g_dLogN == 1 && k.discDeferredAdd == 0);
    // a job runs: recorded (points copied), answered 1, nothing applied yet; early outs still go to the original
    g_async.state = A_RUNNING;
    assert(DiscAddHook(world, NULL, g_dObjA, vec, 0, 1, 0) == 1 && DiscRemoveHook(world, NULL, g_dObjB) == 1);
    assert(DiscRemoveHook(world, NULL, g_dObjA) == 1 && g_dLogN == 1 && k.discDeferredAdd == 1 && k.discDeferredRemove == 2);
    pts[0][0] = 9;  // the record holds its own copy
    u8* shortVec[3] = {(u8*)pts, (u8*)pts + 32, (u8*)pts + 32};
    assert(DiscAddHook(world, NULL, g_dObjA, shortVec, 0, 0, 0) == 1 && g_dLogN == 2);  // fewer than 3 points: original
    DiscPoll();  // a job still runs and the records are fresh: nothing applied
    assert(g_dLogN == 2 && g_discCount == 3);
    // job over: the next poll applies all three in order with the object's reference held
    g_async.state = A_IDLE;
    g_dLogN = 0;
    DiscPoll();
    assert(g_dLogN == 3 && memcmp(g_dLog, "Aba", 3) == 0 && g_discCount == 0 && k.discApplied == 3 && g_dReleases == 3);
    assert(*(LONG*)(g_rgLockObj + 0x20) == 0);
    // an object gone by then is dropped; a record from another world is dropped
    pts[0][0] = 1.5f;
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, g_dObjB);
    DiscAddHook(world, NULL, g_dObjA, vec, 0, 0, 0);
    g_async.state = A_IDLE;
    g_dObjBGone = true;
    g_dLogN = 0;
    DiscPoll();
    assert(g_dLogN == 1 && g_dLog[0] == 'A' && k.discDropped == 1);
    g_dObjBGone = false;
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, g_dObjA);
    static u8 world2[0xA00];
    world2[0x974] = 1;
    g_rgLock = world2 + 0x918;  // travel: a new world before the drain
    g_async.state = A_IDLE;
    g_dLogN = 0;
    DiscPoll();
    assert(g_dLogN == 0 && k.discDropped == 2);
    g_rgLock = world + 0x918;
    // a record made during a drain (event handler) is applied in the same drain, after the one that made it
    g_async.state = A_RUNNING;
    DiscAddHook(world, NULL, g_dObjA, vec, 0, 0, 0);
    g_async.state = A_IDLE;
    g_dNestOnce = true;
    g_dLogN = 0;
    DiscPoll();
    assert(g_dLogN == 2 && memcmp(g_dLog, "Ab", 2) == 0 && k.discNested == 1 && g_discCount == 0);
    // a record older than the age limit is applied even while a job runs (the drain waits for it: here none runs)
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, g_dObjA);
    g_disc[g_discHead].t -= kDiscMaxAgeMs;
    g_async.state = A_IDLE;  // WaitAsyncDone returns at once in the test
    DiscPoll();
    assert(g_discCount == 0 && k.discForced == 0);  // no job ran: a plain drain
    // mode 0: never deferred
    k.discMode = 0;
    g_async.state = A_RUNNING;
    g_dLogN = 0;
    assert(DiscRemoveHook(world, NULL, g_dObjA) == 1 && g_dLogN == 1 && g_discCount == 0);
    g_async.state = A_IDLE;

    g_async.owner = 0;
    g_rgLock = NULL;
    k.discMode = 0;
    k.rgOwnerTid = 0;
    k.rgAcquires = 0;
    g_lockOrig = savedLock;
    CloseHandle(g_asyncDone);
    CloseHandle(g_invReady);
    g_asyncDone = savedDone;
    g_invReady = savedInv;
    DeleteCriticalSection(&g_rgFakeCs);
}

// --- fix D-X (0.16.0, docs/84 §7.2): destroy-gate apply, dead-id sweep, pointer check ---
static u8 g_xObj[3][16];          // objects with ids 0xA, 0xB, 0xC
static u8* g_xLive[3];            // what the id hash answers per id (NULL = gone, another object = reused id)
static char g_xLog[64];
static int g_xLogN, g_xSweeps, g_xKeyFinds;
static bool g_xSweepHit, g_xKeyHit;
static DWORD g_xSweepArgs[4];
static int XIdx(const void* o) { return o == g_xObj[0] ? 0 : o == g_xObj[1] ? 1 : 2; }
static u8 __fastcall XAdd(void*, void*, void* obj, void*, int, int, int) { g_xLog[g_xLogN++] = (char)('A' + XIdx(obj)); return 1; }
static u8 __fastcall XRemove(void*, void*, void* obj) { g_xLog[g_xLogN++] = (char)('a' + XIdx(obj)); return 1; }
static u8 __fastcall XFindRef(void*, void*, DWORD lo, DWORD hi, void** out) {
    assert(hi == 0 && lo >= 0xA && lo <= 0xC);
    u8* o = g_xLive[lo - 0xA];
    if (o) *out = o;
    return o != NULL;
}
static u8 __fastcall XLotRemove(void* p, void*, DWORD lotLo, DWORD lotHi, DWORD idLo, DWORD idHi) {
    assert(p != NULL);
    g_xSweeps++;
    g_xSweepArgs[0] = lotLo, g_xSweepArgs[1] = lotHi, g_xSweepArgs[2] = idLo, g_xSweepArgs[3] = idHi;
    return g_xSweepHit;
}
static void __fastcall XKeyFind(void* map, void*, DWORD* out, const DWORD* id) {
    g_xKeyFinds++;
    const DWORD* buckets = *(const DWORD* const*)((u8*)map + 4);
    out[0] = g_xKeyHit && id[0] == 0xA ? 0x1234 : buckets[*(const DWORD*)((u8*)map + 8)];
    out[1] = 0;
}

static void TestDiscourageDX() {
    static_assert(offsetof(Ts3nCounters, discDestroyMode) == 0x135C && offsetof(Ts3nCounters, discSweepMode) == 0x1360 &&
                  offsetof(Ts3nCounters, discDropAdd) == 0x1364 && offsetof(Ts3nCounters, discDropRemove) == 0x1368 &&
                  offsetof(Ts3nCounters, discDestroyApplied) == 0x136C && offsetof(Ts3nCounters, discDestroyInInvoke) == 0x1370 &&
                  offsetof(Ts3nCounters, discDeadSweep) == 0x1374 && offsetof(Ts3nCounters, discDeadHit) == 0x1378 &&
                  offsetof(Ts3nCounters, discDeadKeyMap) == 0x137C && offsetof(Ts3nCounters, discIdMismatch) == 0x1380 &&
                  sizeof(Ts3nCounters) >= 1249 * 4, "v34 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.discReady, 0, offsetof(Ts3nCounters, discOverflow) + 4 - offsetof(Ts3nCounters, discReady));
    memset((void*)&k.discDestroyMode, 0, offsetof(Ts3nCounters, discIdMismatch) + 4 - offsetof(Ts3nCounters, discDestroyMode));
    const DWORD me = GetCurrentThreadId();
    const LockAcquireFn savedLock = g_lockOrig;
    g_lockOrig = RgTryAcquire;
    g_rgRelOrig = RgFakeRelease;
    InitializeCriticalSection(&g_rgFakeCs);
    const HANDLE savedDone = g_asyncDone, savedInv = g_invReady;
    g_asyncDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_invReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_discAddOrig = XAdd;
    g_discRemoveOrig = XRemove;
    g_objSvc = FakeSvc;
    g_findRef = XFindRef;
    g_objRelease = FakeObjRelease;
    g_lotRemove = XLotRemove;
    g_keyFind = XKeyFind;
    static u8 world[0xA00];
    static u8 planner[0x200];
    static DWORD buckets[2] = {0, 0x5E5E5E5E};  // one bucket, then the end sentinel
    world[0x974] = 1;
    *(u8**)(world + 0x95C) = planner;
    *(DWORD**)(world + 0x8BC) = buckets;
    *(DWORD*)(world + 0x8C0) = 1;
    *(DWORD*)(world + 0x8C4) = 1;  // the lock-key map holds entries
    g_rgLock = world + 0x918;
    for (int i = 0; i < 3; i++) { *(DWORD*)(g_xObj[i] + 8) = 0xA + i; *(DWORD*)(g_xObj[i] + 0xC) = 0; g_xLive[i] = g_xObj[i]; }
    u8* const A = g_xObj[0], * const B = g_xObj[1], * const C = g_xObj[2];
    k.discMode = 1;
    k.discDestroyMode = 1;
    k.discSweepMode = 1;
    g_async.owner = me;
    __declspec(align(16)) float pts[3][4] = {{1.5f}, {2}, {3}};
    u8* vec[3] = {(u8*)pts, (u8*)pts + 48, (u8*)pts + 48};
    const int rel0 = g_dReleases;

    // 1. destroy of A with A's and others' records queued: A's Removes applied in order, A's Add dropped, B and C keep order
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, A);
    DiscAddHook(world, NULL, B, vec, 0, 0, 0);
    DiscAddHook(world, NULL, A, vec, 0, 0, 0);
    DiscRemoveHook(world, NULL, C);
    DiscRemoveHook(world, NULL, A);
    assert(g_discCount == 5 && g_xLogN == 0);
    g_async.state = A_IDLE;  // RT_DESTROY waited for the job
    DiscOnDestroy(0xA, 0, true);
    assert(g_xLogN == 2 && memcmp(g_xLog, "aa", 2) == 0 && k.discDestroyApplied == 2 && k.discDropAdd == 1 && k.discDropped == 1);
    assert(g_discCount == 2 && k.rgOwnerTid == 0 && !g_discDraining);
    DiscPoll();
    assert(g_xLogN == 4 && memcmp(g_xLog, "aaBc", 4) == 0 && g_discCount == 0 && k.discApplied == 4 && g_dReleases - rel0 == 4);
    // a destroy of an object with no record, and one with an empty queue: nothing happens
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, B);
    DiscOnDestroy(0xC, 0, true);
    assert(g_discCount == 1 && g_xLogN == 4);
    g_async.state = A_IDLE;
    DiscPoll();
    DiscOnDestroy(0xA, 0, true);
    assert(g_xLogN == 5 && g_xLog[4] == 'b' && g_discCount == 0);

    // 2. destroy inside a job callback: skipped and counted, the record stays; at the drain the object is gone: dead-id sweep
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, A);
    DiscAddHook(world, NULL, A, vec, 0, 0, 0);
    DiscOnDestroy(0xA, 0, false);
    assert(k.discDestroyInInvoke == 1 && g_discCount == 2 && k.discDestroyApplied == 2);
    g_xLive[0] = NULL;  // DestroyById ran
    g_xSweepHit = true;
    g_xKeyHit = true;
    g_async.state = A_IDLE;
    DiscPoll();
    assert(g_xLogN == 5 && g_discCount == 0 && k.discDropRemove == 1 && k.discDropAdd == 2 && k.discDropped == 3);
    assert(k.discDeadSweep == 1 && k.discDeadHit == 1 && k.discDeadKeyMap == 1 && g_xSweeps == 1 && g_xKeyFinds == 1);
    assert(g_xSweepArgs[0] == 0 && g_xSweepArgs[1] == 0 && g_xSweepArgs[2] == 0xA && g_xSweepArgs[3] == 0);  // lot 0 = all lots
    assert(world[0x994] == 0 && k.discDropRemove == k.discDeadSweep);  // T-D5
    // an empty lock-key map is not looked up; a sweep that finds nothing is not a hit
    world[0x994] = 1;
    *(DWORD*)(world + 0x8C4) = 0;
    g_xSweepHit = false;
    g_async.state = A_RUNNING;
    g_xLive[0] = A;
    DiscRemoveHook(world, NULL, A);
    g_xLive[0] = NULL;
    g_async.state = A_IDLE;
    DiscPoll();
    assert(k.discDeadSweep == 2 && k.discDeadHit == 1 && g_xKeyFinds == 1 && world[0x994] == 0 && k.discDropRemove == k.discDeadSweep);

    // 3. switches off: no gate, no sweep (the A/B baseline of the bug: discDropRemove > discDeadSweep)
    k.discDestroyMode = 0;
    k.discSweepMode = 0;
    g_xLive[0] = A;
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, A);
    DiscOnDestroy(0xA, 0, true);
    assert(g_discCount == 1 && k.discDestroyApplied == 2);
    g_xLive[0] = NULL;
    g_async.state = A_IDLE;
    DiscPoll();
    assert(k.discDropRemove == 3 && k.discDeadSweep == 2 && g_xSweeps == 2);
    k.discDestroyMode = 1;
    k.discSweepMode = 1;

    // 4. pointer check: the id now names another object (reused): dropped, counted, released, never swept or applied
    g_xLive[0] = A;
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, B);
    DiscAddHook(world, NULL, B, vec, 0, 0, 0);
    g_xLive[1] = C;
    const int rel1 = g_dReleases;
    g_async.state = A_IDLE;
    DiscPoll();
    assert(k.discIdMismatch == 2 && g_xLogN == 5 && g_xSweeps == 2 && g_dReleases - rel1 == 2 && k.discDropRemove == 3);
    // the destroy gate does the same check: a reused id is not applied there either
    g_async.state = A_RUNNING;
    g_xLive[1] = B;
    DiscRemoveHook(world, NULL, B);
    g_xLive[1] = C;
    DiscOnDestroy(0xB, 0, true);
    assert(k.discIdMismatch == 3 && k.discDestroyApplied == 2 && g_discCount == 0 && g_xLogN == 5);
    g_xLive[1] = B;
    g_async.state = A_IDLE;

    // 5. through the RT_DESTROY stub: DestroyById's arguments (idLo, idHi) are slot[1], slot[2]
    static DWORD simTid;
    simTid = me;
    g_rtSimTidSlot = &simTid;
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    k.rtReady = RT_ALL;
    k.rgEnforce = 1;
    g_async.state = A_RUNNING;
    DiscRemoveHook(world, NULL, C);
    DiscRemoveHook(world, NULL, B);
    g_async.state = A_IDLE;
    DWORD stack[24] = {};
    stack[20] = 0x999;
    stack[21] = 0xC;
    stack[22] = 0;
    RtEnter(7, &stack[20]);
    assert(k.discDestroyApplied == 3 && g_discCount == 1 && g_xLogN == 6 && g_xLog[5] == 'c' && g_rgOffSimOpen == 1);
    assert(RgLeave() == 0x999 && g_rgOffSimOpen == 0);
    DiscPoll();
    assert(g_discCount == 0 && g_xLogN == 7 && g_xLog[6] == 'b');
    k.rtReady = 0;
    k.rgEnforce = 0;

    g_async.owner = 0;
    g_rgLock = NULL;
    k.discMode = 0;
    k.rgOwnerTid = 0;
    k.rgAcquires = 0;
    g_lockOrig = savedLock;
    CloseHandle(g_asyncDone);
    CloseHandle(g_invReady);
    g_asyncDone = savedDone;
    g_invReady = savedInv;
    DeleteCriticalSection(&g_rgFakeCs);
}

// --- design S2 (0.13.0, docs/84 §3.6): QL frames on the worker, W4, tripwires and the clone's give-up paths on fakes.
// The clone against the game's own code: steerro-test.exe (needs the exe). ---
static int g_srLocoCalls;
static void __fastcall FkLoco(u8*, void*, u8*, const void*, DWORD, const void*, DWORD) { g_srLocoCalls++; }
static u8 __fastcall FkS60(const u8*, void*, DWORD) { return 1; }
static u8 __fastcall FkSFalse(const u8*, void*) { return 0; }
static u8 __fastcall FkSTrue(const u8*, void*) { return 1; }
static int __fastcall FkS8(const u8* o, void*, const void*, DWORD) { return o[0x34]; }
static int __fastcall FkSC(const u8* o, void*) { return o[0x35]; }
static unsigned __int64 __fastcall FkSId(const u8* o, void*) { return *(const unsigned __int64*)(o + 0x14); }
static unsigned __int64 __fastcall FkSubId(const u8* s, void*) { return *(const unsigned __int64*)(s - 4 + 0x14); }
static int __fastcall FkSubType(const u8* s, void*) { return s[-4 + 0x35]; }
static bool g_srE8Touch;
static u8 __fastcall FkSE8(const u8* o, void*, DWORD, const float* a, const float* b) {
    assert(((uintptr_t)a & 15) == 0 && ((uintptr_t)b & 15) == 0);  // 0x80E690 reads them with movaps
    if (g_srE8Touch) (*(u8**)(o + 0x38))[0x10]++;                 // a bounds write by someone else meanwhile
    return 1;
}
static void* g_srFkVt[0x40];
static void* g_srFkSubVt[4];
static u8* SrFakeFp(u8 cls, u8 type, DWORD id) {
    u8* f = (u8*)_aligned_malloc(0x80, 16);
    memset(f, 0, 0x80);
    *(void***)f = g_srFkVt;
    *(void***)(f + 4) = g_srFkSubVt;
    f[0x34] = cls;
    f[0x35] = type;
    *(DWORD*)(f + 0x14) = id;
    u8* cache = (u8*)_aligned_malloc(0x24, 16);
    memset(cache, 0, 0x24);
    cache[0x20] = 1;
    *(u8**)(f + 0x38) = cache;
    return f;
}
static u8 g_srLeaf[0x20];
static u8 g_srIter[0x24];
static bool g_srBumpQ;
static void __fastcall FkWalkPt(const void* q, void*, void* ctx, float, float, float, void* visit, void* list) {
    if (g_srBumpQ) (*(LONG*)((u8*)q + 0x48))++;  // a job's query or pin on the same tree during the walk
    ((u8(__stdcall*)(void*, const u8*, void*))visit)(ctx, g_srIter, list);
}

static void TestSteerRO() {
    static_assert(offsetof(Ts3nCounters, steerReady) == 0x10A0 && offsetof(Ts3nCounters, steerMode) == 0x10A4 &&
                  offsetof(Ts3nCounters, steerShadow) == 0x10A8 && offsetof(Ts3nCounters, steerFb) == 0x10B4 &&
                  offsetof(Ts3nCounters, steerUntracked) == 0x1108 && offsetof(Ts3nCounters, caMode) == 0x1110 &&
                  offsetof(Ts3nCounters, caSimInJob) == 0x1114 && offsetof(Ts3nCounters, caQlHeld) == 0x112C &&
                  sizeof(Ts3nCounters) >= 1100 * 4, "v31 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.steerReady, 0, offsetof(Ts3nCounters, caQlHeld) + 4 - offsetof(Ts3nCounters, steerReady));
    const DWORD me = GetCurrentThreadId();
    static DWORD simTid;
    simTid = me + 3;
    g_rtSimTidSlot = &simTid;
    const DWORD savedWorker = g_workerTid;
    DWORD stack[40] = {};
    auto reset = [&] { t_rg.n = 0; t_rg.readers = 0; };

    // worker: QlEnter holds QL exclusive until the leave; nested entries count up; the leave releases
    g_workerTid = me;
    stack[30] = 0x111;
    QlEnter(4, &stack[30]);
    assert(t_qlDepth == 1 && t_rg.n == 1 && t_rg.f[0].rel == RG_REL_QL && t_rg.readers == 1 && !TryAcquireSRWLockShared(&g_ql));
    stack[20] = 0x222;
    QlEnter(0, &stack[20]);
    assert(t_qlDepth == 2 && k.steerQlHolds == 1);
    assert(RgLeave() == 0x222 && t_qlDepth == 1);
    assert(RgLeave() == 0x111 && t_qlDepth == 0 && TryAcquireSRWLockShared(&g_ql));
    ReleaseSRWLockShared(&g_ql);
    // a frame left without returning is released at the next entry (RgDrop)
    QlEnter(4, &stack[20]);
    QlEnter(4, &stack[30]);  // the slot above the open frame: that frame is gone
    assert(t_qlDepth == 1 && t_rg.n == 1);
    RgLeave();
    assert(t_qlDepth == 0);
    // no frame left (16 open): QL is released at once and the clone is switched off for good
    for (int i = 0; i < 16; i++) RgPush(t_rg, &stack[39 - i], 1);
    QlEnter(4, &stack[2]);
    assert(k.steerUntracked == 1 && g_steerBroken == 1 && t_qlDepth == 0 && TryAcquireSRWLockShared(&g_ql));
    ReleaseSRWLockShared(&g_ql);
    reset();
    // W4: the LotGraph_Build gate stub (site 3) on the worker holds QL; other sites and other threads do not
    static u8 world[0xA00];
    g_rgLock = world + 0x918;
    k.rgReady = 7;
    g_qlOn = 1;
    stack[30] = 0x333;
    RgEnter(3, &stack[30]);
    assert(t_rg.n == 1 && t_rg.f[0].rel == RG_REL_QL && t_qlDepth == 1);
    assert(RgLeave() == 0x333 && t_qlDepth == 0);
    RgEnter(2, &stack[30]);
    assert(t_rg.f[0].rel == 0 && t_qlDepth == 0);
    reset();
    g_workerTid = me + 7;
    RgEnter(3, &stack[30]);
    assert(t_rg.f[0].rel == 0 && t_qlDepth == 0);
    reset();
    k.rgReady = 0;
    g_rgLock = NULL;
    // sim-thread writes during a job are counted; inside the clone the answer is dropped
    simTid = me;
    g_async.state = A_RUNNING;
    QlEnter(5, &stack[30]);
    QlEnter(1, &stack[30]);
    assert(k.steerSimCacheWrite == 1 && k.steerSimTreeWrite == 1 && t_rg.n == 0);
    t_inRo = true;
    QlEnter(5, &stack[30]);
    assert(k.steerSimWriteInRo == 1 && t_srFail == SR_FB_TRIP);
    t_inRo = false;
    t_srFail = 0;
    g_async.state = A_IDLE;
    QlEnter(5, &stack[30]);
    assert(k.steerSimCacheWrite == 2);

    // fix C-A: on the sim thread during a job, an invalid level of one of the four classes takes +0x918 in the W6a stub,
    // i.e. before the original body marks the level valid and frees its polygons; never with QL held
    {
        const LockAcquireFn savedLock = g_lockOrig;
        g_lockOrig = RgTryAcquire;
        g_rgRelOrig = RgFakeRelease;
        InitializeCriticalSection(&g_rgFakeCs);
        if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
        LONG& count = *(LONG*)(g_rgLockObj + 0x20);
        g_rgLock = g_rgLockObj;
        g_async.lock = g_rgLockObj;
        g_async.owner = me;
        g_async.state = A_RUNNING;
        g_caOn = 1;
        static u8 caFp[0x40], caCache[0x24 * 2];
        *(DWORD*)caFp = g_caClasses[1];
        *(u8**)(caFp + 0x38) = caCache;
        DWORD fr[8] = {};
        fr[2] = (DWORD)(uintptr_t)caFp;  // ecx in the pushad frame (slot[-2])
        fr[4] = 0x444;                   // return address (slot)
        fr[5] = 1;                       // level argument (slot[1])
        k.caMode = 0;  // count only
        QlEnter(QL_W6A, &fr[4]);
        assert(k.caSimInJob[1] == 1 && k.caWaits == 0 && count == 0 && t_rg.n == 0);
        k.caMode = 1;
        QlEnter(QL_W6A, &fr[4]);
        assert(count == 1 && k.caWaits == 1 && caCache[0x24 + 0x20] == 0);  // held before the body touches the level
        assert(t_rg.n == 1 && t_rg.f[0].rel == (RG_REL_LOCK | RG_REL_OFFSIM) && fr[4] == (DWORD)(uintptr_t)RgLeaveThunk);
        caCache[0x24 + 0x20] = 1;  // the body ran: level valid
        assert(RgLeave() == 0x444 && count == 0 && g_rgOffSimOpen == 0);
        // valid level, other class, other level index: nothing
        fr[4] = 0x444;
        QlEnter(QL_W6A, &fr[4]);
        *(DWORD*)caFp = 0x00FEBBC0;
        caCache[0x24 + 0x20] = 0;
        QlEnter(QL_W6A, &fr[4]);
        assert(count == 0 && t_rg.n == 0 && k.caSimInJob[1] == 2 && k.caSimInJob[0] + k.caSimInJob[2] + k.caSimInJob[3] == 0);
        // QL held on this thread: counted, no wait (lock order: +0x918 is never awaited under QL)
        *(DWORD*)caFp = g_caClasses[3];
        t_qlDepth = 1;
        QlEnter(QL_W6A, &fr[4]);
        assert(k.caQlHeld == 1 && count == 0 && t_rg.n == 0 && k.caSimInJob[3] == 1);
        t_qlDepth = 0;
        // the +0x918 owner, or a callback served for the job: no wait
        k.rgOwnerTid = (LONG)me;
        QlEnter(QL_W6A, &fr[4]);
        assert(count == 0 && t_rg.n == 0);
        k.rgOwnerTid = 0;
        g_inInvoke = 1;
        QlEnter(QL_W6A, &fr[4]);
        assert(count == 0 && t_rg.n == 0);
        g_inInvoke = 0;
        // no job: the stub does nothing
        g_async.state = A_IDLE;
        QlEnter(QL_W6A, &fr[4]);
        assert(k.caSimInJob[3] == 3 && k.caWaits == 1);  // counted twice above (owner, invoke), not here
        g_caOn = 0;
        k.caMode = 0;
        g_async.lock = NULL;
        g_rgLock = NULL;
        k.rgOwnerTid = 0;
        k.rgAcquires = 0;
        g_lockOrig = savedLock;
        DeleteCriticalSection(&g_rgFakeCs);
    }

    // the clone's pieces on fakes (slot whitelist off: these vtables are ours)
    g_srCheckSlots = false;
    const float* savedOne = g_srOne;
    static const float one = 1.0f;
    g_srOne = &one;  // 0x107A538 does not exist here
    g_srFkVt[0x60 / 4] = (void*)FkS60;
    g_srFkVt[0x28 / 4] = (void*)FkSFalse;
    g_srFkVt[2] = (void*)FkS8;
    g_srFkVt[3] = (void*)FkSC;
    g_srFkVt[0x84 / 4] = (void*)FkSId;
    g_srFkVt[0xC0 / 4] = (void*)FkSTrue;
    g_srFkVt[0xE8 / 4] = (void*)FkSE8;
    g_srFkSubVt[1] = (void*)FkSubId;
    g_srFkSubVt[2] = (void*)FkSubType;
    // a leaf with a heap list: class 3 type 5; class 2 (local list) type 5; type 0x1F ("seen"); class 1; class 0xD; id 0
    u8* fps[6] = {SrFakeFp(3, 5, 0x10), SrFakeFp(2, 5, 0x11), SrFakeFp(3, 0x1F, 0x12), SrFakeFp(1, 5, 0x13), SrFakeFp(0xD, 5, 0x14),
                  SrFakeFp(3, 5, 0)};
    u8 ents[6][0x20] = {};
    DWORD list[6][3];
    for (int i = 0; i < 6; i++) {
        *(u8**)(ents[i] + 8) = fps[i];
        list[i][0] = (DWORD)(uintptr_t)ents[i];
        list[i][1] = list[i][2] = 0xFFFFFFFF;
    }
    *(DWORD*)(g_srLeaf + 8) = (DWORD)(uintptr_t)list | 1;
    *(DWORD*)(g_srLeaf + 0xC) = 6;
    static SrCtx c;
    auto collect = [&](const u8* rec, DWORD flag) {
        c.n = 0;
        c.fail = 0;
        c.rec = rec;
        c.flag = flag;
        SrCollect(&c, g_srLeaf, false, 0);
    };
    collect(NULL, 1);  // EditPath's filter: class >= 2 and not 0xD; class 2 only with a "seen" entry in the same leaf
    assert(!c.fail && c.n == 3 && c.objs[0] == fps[0] && c.objs[1] == fps[5] && c.objs[2] == fps[1]);
    fps[2][0x35] = 5;
    collect(NULL, 1);
    assert(c.n == 3 && c.objs[0] == fps[0] && c.objs[1] == fps[2] && c.objs[2] == fps[5]);  // no "seen": class 2 dropped
    __declspec(align(16)) u8 rec[0x30] = {};
    collect(rec, 0);  // the obstacle query: all classes, but not the record's own id (0)
    assert(c.n == 5 && c.objs[4] == fps[4]);
    // masked lists: the entry's bit for the sub-cell
    list[0][1] = 0;
    list[0][2] = 1u << 3;
    c.n = 0;
    c.fail = 0;
    c.rec = rec;
    c.flag = 0;
    SrCollect(&c, g_srLeaf, true, 35);
    assert(c.n == 5);
    c.n = 0;
    SrCollect(&c, g_srLeaf, true, 3);
    assert(c.n == 4);  // fps[0] not in sub-cell 3
    list[0][1] = list[0][2] = 0xFFFFFFFF;
    // a NULL footprint (the original would dereference it), the whitelist, overflow: give up
    *(u8**)(ents[3] + 8) = NULL;
    collect(rec, 0);
    assert(c.fail == SR_FB_SHAPE && t_srShape == SRS_NULLFP);
    *(u8**)(ents[3] + 8) = fps[3];
    g_srCheckSlots = true;
    collect(rec, 0);
    assert(c.fail == SR_FB_SHAPE && t_srShape == SRS_SLOT);
    g_srCheckSlots = false;
    c.n = kSrCap;  // full: the walk goes on counting (v39 s2MaxObjs), SrWalk gives up after it
    memset(c.objs, 0, sizeof(c.objs));  // none of the leaf's footprints stored yet
    c.fail = 0;
    c.over = 0;
    c.flag = 0;
    SrCollect(&c, g_srLeaf, false, 0);
    assert(!c.fail && c.n == kSrCap && c.over == 5);
    c.over = 0;

    // walks: the deferred flag with the pin counter below 1 (the original flushes first), the pin counter moving meanwhile
    static u8 q[0x60];
    *(u8**)(g_srIter + 0xC) = g_srLeaf;
    g_srIter[0x1C] = 0;
    const SrWalkPtFn savedWalk = g_srWalkPt;
    g_srWalkPt = FkWalkPt;
    q[0x4C] = 1;
    *(LONG*)(q + 0x48) = 0;
    c.fail = 0;
    c.n = 0;
    assert(SrWalk(&c, q, true, 0, 0, 1) == SR_FB_DEFERRED && c.n == 0);
    *(LONG*)(q + 0x48) = 1;
    assert(SrWalk(&c, q, true, 0, 0, 1) == SR_OK && c.n == 5);
    g_srBumpQ = true;
    c.n = 0;
    assert(SrWalk(&c, q, true, 0, 0, 1) == SR_FB_TRIP && k.steerPinMoved == 1);
    g_srBumpQ = false;
    g_srWalkPt = savedWalk;

    // EditPath's consumer: the level-0 test needs a valid cache, and the cache must not change under it (T3)
    static u8 entry[0x100];  // lot entry: rows identity
    float* rows = (float*)(((uintptr_t)entry + 15) & ~(uintptr_t)15);
    (void)rows;
    SrNode nd = {};
    static float rawPt[4] = {1, 0, 2, 0};
    nd.raw = (const u8*)rawPt;
    nd.eLot = false;
    static SrOut out;
    out.nIds = 0;
    c.n = 1;
    c.objs[0] = fps[0];
    const SrIdListFn savedIdList = g_srIdList;
    g_srIdList = [](DWORD lo, DWORD hi, const void* l, DWORD n) -> u8 {  // IdList_Contains 0x85AAA0
        for (DWORD i = 0; l && i < n; i++) if (((const DWORD*)l)[i * 2] == lo && ((const DWORD*)l)[i * 2 + 1] == hi) return 1;
        return 0;
    };
    assert(SrConsume(&c, NULL, nd, NULL, 0, &out, NULL, 0, NULL, 0, NULL, 0) == SR_OK && out.nIds == 1 && out.ids[0][0] == 0x10);
    // ignore list, ids already present, exclude list: no second append
    const DWORD have[2] = {0x10, 0};
    out.nIds = 0;
    assert(SrConsume(&c, NULL, nd, have, 1, &out, NULL, 0, NULL, 0, NULL, 0) == SR_OK && out.nIds == 0);
    assert(SrConsume(&c, NULL, nd, NULL, 0, &out, have, 1, NULL, 0, NULL, 0) == SR_OK && out.nIds == 0);
    assert(SrConsume(&c, NULL, nd, NULL, 0, &out, NULL, 0, have, 1, NULL, 0) == SR_OK && out.nIds == 0);
    // with a lot for the level-0 test: an invalid cache gives up (the original would rebuild it), a changed one too (T3)
    static u8 p[0x200], lotNode[0x200];
    static DWORD buckets[2];
    u8* ln = (u8*)(((uintptr_t)lotNode + 15) & ~(uintptr_t)15);
    *(DWORD*)ln = 0x20;
    float* r = (float*)(ln + 0x10 + 0xA0);
    r[0] = r[5] = r[10] = r[15] = 1;
    buckets[0] = (DWORD)(uintptr_t)ln;
    buckets[1] = 0x5E117E1;
    *(DWORD**)(p + 0x4C) = buckets;
    *(DWORD*)(p + 0x50) = 1;
    const SrLotFindFn savedFind = g_srLotFind;
    g_srLotFind = [](const void* tbl, void*, DWORD* o, const DWORD* key) {  // LotTable_Find 0x627E00, one bucket
        const u8* n = (const u8*)(uintptr_t)(*(const DWORD* const*)((const u8*)tbl + 4))[0];
        o[0] = *(const DWORD*)n == key[0] ? (DWORD)(uintptr_t)n : 0x5E117E1;
        o[1] = 0;
    };
    nd.eLot = true;
    nd.eLo = 0x20;
    (*(u8**)(fps[0] + 0x38))[0x20] = 0;
    assert(SrConsume(&c, p, nd, NULL, 0, &out, NULL, 0, NULL, 0, NULL, 0) == SR_FB_CACHE);
    (*(u8**)(fps[0] + 0x38))[0x20] = 1;
    g_srE8Touch = true;
    assert(SrConsume(&c, p, nd, NULL, 0, &out, NULL, 0, NULL, 0, NULL, 0) == SR_FB_CACHE);
    g_srE8Touch = false;
    out.nIds = 0;
    assert(SrConsume(&c, p, nd, NULL, 0, &out, NULL, 0, NULL, 0, NULL, 0) == SR_OK && out.nIds == 1);
    nd.eLo = 0x21;  // EditPath's lot not in the table: GetOrInsert would insert it
    assert(SrConsume(&c, p, nd, NULL, 0, &out, NULL, 0, NULL, 0, NULL, 0) == SR_FB_LOTMISS);
    // Contains rules: key 0 and -1 and the active lot have no entry; a key mismatch at the entry neither
    static u8 act[0x60];
    static const u8* actp;
    actp = act;
    const SrActiveLotFn savedAct = g_srActiveLot;
    g_srActiveLot = []() -> const u8* { return actp; };
    const u8* e;
    assert(SrEntry(p, 0, 0, true, &e) == SR_OK && !e);
    assert(SrEntry(p, 0xFFFFFFFF, 0xFFFFFFFF, true, &e) == SR_OK && !e);
    *(DWORD*)(act + 0x48) = 0x20;
    assert(SrEntry(p, 0x20, 0, true, &e) == SR_OK && !e);
    *(DWORD*)(act + 0x48) = 0x99;
    assert(SrEntry(p, 0x20, 0, true, &e) == SR_OK && !e);  // node+0x10 holds no key yet
    *(DWORD*)(ln + 0x10) = 0x20;
    assert(SrEntry(p, 0x20, 0, true, &e) == SR_OK && e == ln + 0x10);
    assert(SrEntry(p, 0x21, 0, true, &e) == SR_OK && !e);
    actp = NULL;
    assert(SrEntry(p, 0x20, 0, true, &e) == SR_FB_SHAPE && t_srShape == SRS_NOACTLOT);
    static_assert(offsetof(Ts3nCounters, p10Ready) == 0x1424 && offsetof(Ts3nCounters, s2Shape) == 0x1464 &&
                      offsetof(Ts3nCounters, s2MaxObjs) == 0x1490 && offsetof(Ts3nCounters, s2MaxDepth) == 0x14A0 &&
                      sizeof(Ts3nCounters) >= 1321 * 4 && SRS_N == 11,
                  "v39 layout");
    const LONG sh0 = k.s2Shape[SRS_NOACTLOT], fbs0 = k.steerFb[SR_FB_SHAPE - 1];
    SrFallback(SR_FB_SHAPE);  // counted under the last site
    assert(k.s2Shape[SRS_NOACTLOT] == sh0 + 1 && k.steerFb[SR_FB_SHAPE - 1] == fbs0 + 1);
    // cell graph bounds (level base +0x40, vector +0x74/+0x78)
    static DWORD qv[2] = {0x1234, 0x5678};
    *(LONG*)(ln + 0x10 + 0x40) = 1;
    *(DWORD**)(ln + 0x10 + 0x74) = qv;
    *(DWORD**)(ln + 0x10 + 0x78) = qv + 2;
    assert(!SrCell(ln + 0x10, 0) && SrCell(ln + 0x10, 1) == (const u8*)0x1234 && SrCell(ln + 0x10, 2) == (const u8*)0x5678 && !SrCell(ln + 0x10, 3));
    g_srActiveLot = savedAct;
    g_srLotFind = savedFind;
    g_srIdList = savedIdList;

    // the hook: no switch, not the owner, a job with QL held elsewhere, the job's callback, the broken flag
    g_locoOrig = FkLoco;
    static u8 agent[0x100], node[0x40];
    *(u8**)(agent + 8) = node;
    *(u8**)node = agent + 8;
    node[0x30] = 1;
    static DWORD idsv[3];
    g_srLocoCalls = 0;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 1 && k.steerCalls == 0);
    k.steerMode = 1;
    g_async.owner = me + 1;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 2 && k.steerCalls == 0);
    g_async.owner = me;
    g_async.state = A_RUNNING;
    g_twArmed = 1;  // the worker arms the tripwire stubs for its job (0.18.0)
    g_steerBroken = 0;
    g_inInvoke = 1;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 3 && k.steerFb[SR_FB_STATE - 1] == 1);
    g_inInvoke = 0;
    g_steerBroken = 1;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 4 && k.steerFb[SR_FB_STATE - 1] == 2);
    g_steerBroken = 0;
    // QL held exclusive by another thread (a worker writer): try fails, today's call (fake world: World_Get answers it)
    static u8 fworld[0xA00], fwm[0x200], fp[0x200];
    fworld[0x974] = 1;
    *(u8**)(fworld + 0x95C) = fp;
    *(u8**)(fworld + 0x964) = fwm;
    static u8* fwmSlot;
    fwmSlot = fwm;
    const SrWorldFn savedWorld = g_srWorldGet;
    u8* const* savedWm = g_srWmSlot;
    const SrConvFn savedConv = g_srConv;
    const SrLotLookupFn savedLook = g_srLotLookup;
    g_srWorldGet = []() -> u8* { return fworld; };
    g_srWmSlot = &fwmSlot;
    g_srConv = [](const void*, void*) {};
    g_srLotLookup = [](void*, void*, float, float) -> u8* { return NULL; };
    HANDLE held = CreateEventW(NULL, TRUE, FALSE, NULL), go = CreateEventW(NULL, TRUE, FALSE, NULL);
    struct Hold { HANDLE held, go; } h = {held, go};
    HANDLE th = CreateThread(NULL, 0, [](void* p) -> DWORD {
        Hold* hh = (Hold*)p;
        AcquireSRWLockExclusive(&g_ql);
        SetEvent(hh->held);
        WaitForSingleObject(hh->go, 5000);
        ReleaseSRWLockExclusive(&g_ql);
        return 0;
    }, &h, 0, NULL);
    WaitForSingleObject(held, 5000);
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 5 && k.steerFb[SR_FB_TRY - 1] == 1);
    SetEvent(go);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    CloseHandle(held);
    CloseHandle(go);
    // QL free, no lot anywhere: the read-only answer is "nothing to add" without the game's call
    const LONG taken0 = k.steerTaken;
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 5 && k.steerTaken == taken0 + 1);
    // a dirty lot index: give up
    fwm[0x1CC] = 1;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 6 && k.steerFb[SR_FB_DIRTY - 1] == 1);
    fwm[0x1CC] = 0;
    g_srWorldGet = savedWorld;
    g_srWmSlot = savedWm;
    g_srConv = savedConv;
    g_srLotLookup = savedLook;
    g_srOne = savedOne;
    g_srCheckSlots = true;
    g_async.state = A_IDLE;
    g_twArmed = 0;
    g_async.owner = 0;
    g_qlOn = 0;
    g_workerTid = savedWorker;
    k.steerMode = 0;
    simTid = 0;
    for (u8* f : fps) { _aligned_free(*(u8**)(f + 0x38)); _aligned_free(f); }
}

// --- S2 tripwires T4 and T-S7 (0.15.0): who is counted, the frame and its leave, a clone dropping its answer ---
// One tripwire call on a thread that is neither the sim nor (unless `worker`) the worker: enter, optionally block, leave.
struct TwCall { int site; bool worker; LONG qlDepth; HANDLE in, go; int nested; int fill; };
static DWORD WINAPI TwThread(void* p) {
    TwCall* c = (TwCall*)p;
    if (c->worker) g_workerTid = GetCurrentThreadId();
    t_qlDepth = c->qlDepth;
    DWORD stack[40] = {};
    for (int i = 0; i < c->fill; i++) RgPush(t_rg, &stack[39 - i], 0);  // frames already open on this thread
    stack[20] = 0x555;
    TwEnter(c->site, &stack[20]);
    if (c->nested) {  // a second tripwire call inside the first: the outer frame covers it
        stack[10] = 0x666;
        TwEnter(c->nested, &stack[10]);
        assert(stack[10] == 0x666 && t_twDepth == 1);
    }
    if (c->in) { SetEvent(c->in); WaitForSingleObject(c->go, 5000); }
    if (stack[20] != 0x555) assert(RgLeave() == 0x555);  // the site returned through the leave thunk
    assert(t_twDepth == 0);
    t_qlDepth = 0;
    return 0;
}
static void TwRun(TwCall c) {
    HANDLE t = CreateThread(NULL, 0, TwThread, &c, 0, NULL);
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
}
static int g_twFkMode;  // inside the clone (from the fake active-lot lookup): 1 = a foreign tripwire call starts and ends;
                        // 2 = the job ends (the worker disarms the stubs); 3 = the job ends and the next one arms them again
static u8 g_twAct[0x60];
static const u8* TwFkActiveLot() {
    if (g_twFkMode == 1) TwRun({6});
    if (g_twFkMode >= 2) TwArm(false);
    if (g_twFkMode == 3) TwArm(true);
    return g_twAct;
}

static void TestSteerTrip() {
    static_assert(offsetof(Ts3nCounters, steerTwStubs) == 0x131C && offsetof(Ts3nCounters, steerT4Bare) == 0x1320 &&
                  offsetof(Ts3nCounters, steerS7Foreign) == 0x1324 && offsetof(Ts3nCounters, steerS7Sim) == 0x1328 &&
                  offsetof(Ts3nCounters, steerTripDiscard) == 0x132C && offsetof(Ts3nCounters, steerTwSite) == 0x1330 &&
                  sizeof(Ts3nCounters) >= 1239 * 4, "v33 layout");
    static_assert(offsetof(Ts3nCounters, rfbLogMs) == 0x1384 && offsetof(Ts3nCounters, s2TscPerUs) == 0x13A4 &&
                  offsetof(Ts3nCounters, s2EntryKt) == 0x13B0 && offsetof(Ts3nCounters, s2TwCalls) == 0x13B8 &&
                  offsetof(Ts3nCounters, s2QlCalls) == 0x13C4 && offsetof(Ts3nCounters, s2ShadowKt) == 0x13D0 &&
                  offsetof(Ts3nCounters, s2TryMaxT) == 0x13E0 && sizeof(Ts3nCounters) >= 1273 * 4, "v36 layout");
    static_assert(TW_N == 11 && TW_T4 == 4, "sites");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.steerTwStubs, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, steerTwStubs));
    const LONG fb0 = k.steerFb[SR_FB_TRIP - 1], inRo0 = k.steerSimWriteInRo, unt0 = k.steerUntracked;
    const DWORD me = GetCurrentThreadId();
    static DWORD simTid;
    simTid = me;
    const DWORD* const savedSimSlot = g_rtSimTidSlot;
    g_rtSimTidSlot = &simTid;
    const DWORD savedWorker = g_workerTid;
    g_workerTid = me + 7;
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);

    // no job and no clone: nothing counted, nothing held
    LONG e0 = g_twEpoch;
    TwRun({4});
    assert(k.steerS7Foreign == 0 && g_twActive == 0 && g_twEpoch == e0);
    // a job: T-S7 on another thread is counted, held for the call, the epoch moves on the leave
    g_async.state = A_RUNNING;
    TwRun({4});
    assert(k.steerS7Foreign == 1 && k.steerTwSite[4] == 1 && g_twActive == 0 && g_twEpoch == e0 + 1);
    TwRun({0});  // T4 on a thread outside QL
    assert(k.steerT4Bare == 1 && k.steerTwSite[0] == 1 && g_twEpoch == e0 + 2);
    // the worker: T4 inside a W frame (QL depth > 0) is covered; outside it counts; T-S7 counts either way
    TwRun({1, true, 1});
    assert(k.steerT4Bare == 1 && g_twEpoch == e0 + 2);
    TwRun({1, true, 0});
    assert(k.steerT4Bare == 2 && k.steerTwSite[1] == 1);
    TwRun({5, true, 1});
    assert(k.steerS7Foreign == 2 && k.steerTwSite[5] == 1);
    g_workerTid = me + 7;
    // the sim thread: T-S7 by design (counted apart), T4 not at all; inside the clone either one drops the answer
    DWORD fr[4] = {0, 0x777, 0, 0};
    const int rgN = t_rg.n;
    TwEnter(4, &fr[1]);
    TwEnter(2, &fr[1]);
    assert(k.steerS7Sim == 1 && k.steerS7Foreign == 2 && k.steerT4Bare == 2 && fr[1] == 0x777 && t_rg.n == rgN);
    t_inRo = true;
    TwEnter(3, &fr[1]);
    assert(t_srFail == SR_FB_TRIP && k.steerSimWriteInRo == inRo0 + 1 && fr[1] == 0x777);
    t_inRo = false;
    t_srFail = 0;
    // nested on one thread: one frame, the inner call only moves the epoch
    e0 = g_twEpoch;
    TwRun({6, false, 0, NULL, NULL, 7});
    assert(k.steerTwSite[6] == 1 && k.steerTwSite[7] == 1 && g_twActive == 0 && g_twEpoch == e0 + 2);
    // no frame left (16 open): the call is counted, the clone is switched off for good (it could not see this writer leave)
    TwRun({8, false, 0, NULL, NULL, 0, 16});
    assert(k.steerTwSite[8] == 1 && g_steerBroken == 1 && k.steerUntracked == unt0 + 1 && g_twActive == 0);
    g_steerBroken = 0;

    // the clone: a fake world whose only flagged node lies on a lot the active-lot check rejects (no walk, nothing appended)
    static u8 fworld[0xA00], fwm[0x200], fp[0x200], flot[0x60];
    fworld[0x974] = 1;
    *(u8**)(fworld + 0x95C) = fp;
    *(u8**)(fworld + 0x964) = fwm;
    *(DWORD*)(flot + 0x48) = 0x55;
    *(DWORD*)(g_twAct + 0x48) = 0x55;
    static u8* fwmSlot;
    fwmSlot = fwm;
    const SrWorldFn savedWorld = g_srWorldGet;
    u8* const* savedWm = g_srWmSlot;
    const SrConvFn savedConv = g_srConv;
    const SrLotLookupFn savedLook = g_srLotLookup;
    const SrActiveLotFn savedAct = g_srActiveLot;
    const float* savedOne = g_srOne;
    static const float one = 1.0f;
    g_srOne = &one;
    g_srWorldGet = []() -> u8* { return fworld; };
    g_srWmSlot = &fwmSlot;
    g_srConv = [](const void*, void*) {};
    g_srLotLookup = [](void*, void*, float, float) -> u8* { return flot; };
    g_srActiveLot = TwFkActiveLot;
    g_locoOrig = FkLoco;
    g_srLocoCalls = 0;
    static u8 agent[0x100], node[0x40];
    *(u8**)(agent + 8) = node;
    *(u8**)node = agent + 8;
    node[0x30] = 1;
    static DWORD idsv[3];
    g_async.owner = me;
    k.steerMode = 1;
    // the stubs not armed (a job seen running, but the worker has already disarmed): no answer, the game's call (0.18.0)
    const LONG tkA = k.steerTaken;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 1 && k.s2JobEndDrop == 1 && k.steerTaken == tkA && g_srInClone == 0);
    g_srLocoCalls = 0;
    TwArm(true);
    assert(g_twArmed == 1 && k.s2ArmJobs == 1);
    // no tripwire call: a read-only answer
    const LONG tk0 = k.steerTaken;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 0 && k.steerTaken == tk0 + 1 && k.steerTripDiscard == 0 && g_srInClone == 0);
    // the job ends during the clone (disarm), with or without the next job's arm before the clone ends: writers after the
    // disarm were not seen, so the answer is dropped (generation moved)
    for (int m = 2; m <= 3; m++) {
        const LONG d0 = k.s2JobEndDrop, c0 = g_srLocoCalls;
        g_twFkMode = m;
        LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
        g_twFkMode = 0;
        assert(g_srLocoCalls == c0 + 1 && k.s2JobEndDrop == d0 + 1 && k.steerTaken == tk0 + 1 && g_srInClone == 0);
        TwArm(true);
    }
    g_srLocoCalls = 0;
    assert(k.s2EntryMaxT > 0 && k.s2TryCalls == 4 && k.steerTaken == tk0 + 1);
    // a writer already running on another thread: the clone gives up at once, the game's call runs
    HANDLE in = CreateEventW(NULL, FALSE, FALSE, NULL), go = CreateEventW(NULL, FALSE, FALSE, NULL);
    TwCall blk = {9, false, 0, in, go};
    HANDLE th = CreateThread(NULL, 0, TwThread, &blk, 0, NULL);
    WaitForSingleObject(in, 5000);
    assert(g_twActive == 1);
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 1 && k.steerTripDiscard == 1 && k.steerFb[SR_FB_TRIP - 1] == fb0 + 1 && k.steerTaken == tk0 + 1);
    SetEvent(go);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    assert(g_twActive == 0);
    // a writer that starts and ends inside the clone: the epoch moved, the answer is dropped
    g_twFkMode = 1;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 2 && k.steerTripDiscard == 2 && k.steerFb[SR_FB_TRIP - 1] == fb0 + 2 && k.steerTwSite[6] == 2);
    // shadow (no job, stubs disarmed): the clone's window alone makes the call count, and the clone's answer is dropped
    g_async.state = A_IDLE;
    TwArm(false);
    k.steerShadow = 1;
    LocoHook(agent, NULL, (u8*)idsv, NULL, 0, NULL, 0);
    assert(g_srLocoCalls == 3 && k.steerTwSite[6] == 3 && k.steerTripDiscard == 3 && k.steerShadowMismatch == 0 &&
           g_rgOffSimOpen == 0 && g_srInClone == 0);
    g_twFkMode = 0;
    k.steerShadow = 0;
    assert(k.steerS7Foreign == 8 && k.steerT4Bare == 2 && g_twActive == 0);
    CloseHandle(in);
    CloseHandle(go);
    g_srWorldGet = savedWorld;
    g_srWmSlot = savedWm;
    g_srConv = savedConv;
    g_srLotLookup = savedLook;
    g_srActiveLot = savedAct;
    g_srOne = savedOne;
    g_async.owner = 0;
    k.steerMode = 0;
    g_workerTid = savedWorker;
    g_rtSimTidSlot = savedSimSlot;
}

// --- design L1 and probe P1b (0.14.0, lotfd.cpp): clone rules on fakes, no writes, QL rules, hook dispatch, P1b keys, T-L3 ---
static u64 g_fdSkew;
static int g_fdCalls;
static u64 __fastcall FkFd(const u8* p, void*, DWORD lo, DWORD hi, DWORD type, const u8** out) {  // stands for 0x631800
    g_fdCalls++;
    u64 r;
    const u8* pt;
    LtFrontDoor(p, lo, hi, type, &r, &pt);
    if (out && pt) *out = pt;
    return r + g_fdSkew;
}
static LONG __fastcall FkPortalAddRef(u8* o, void*) { return ++*(LONG*)(o + 0x70); }
static LONG __fastcall FkPortalRelease(u8* o, void*) { return --*(LONG*)(o + 0x70); }
static DWORD Fnv32(const void* p, size_t n) {
    DWORD h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ ((const u8*)p)[i]) * 16777619u;
    return h;
}

static void TestLotFd() {
    static_assert(offsetof(Ts3nCounters, ltReady) == 0x1130 && offsetof(Ts3nCounters, ltMode) == 0x1134 &&
                  offsetof(Ts3nCounters, ltShadow) == 0x1138 && offsetof(Ts3nCounters, ltFb) == 0x1144 &&
                  offsetof(Ts3nCounters, ltUs) == 0x115C && offsetof(Ts3nCounters, ltShadowMismatch) == 0x1168 &&
                  offsetof(Ts3nCounters, ltQlHeldBlock) == 0x116C && offsetof(Ts3nCounters, portalRef) == 0x1170 &&
                  offsetof(Ts3nCounters, p1bReady) == 0x1190 && offsetof(Ts3nCounters, p1bKey) == 0x1194 &&
                  offsetof(Ts3nCounters, p1bN) == 0x1214 && offsetof(Ts3nCounters, p1bUs) == 0x1294 &&
                  offsetof(Ts3nCounters, p1bOther) == 0x1314 && offsetof(Ts3nCounters, p1bNoFrame) == 0x1318 &&
                  sizeof(Ts3nCounters) >= 1223 * 4, "v32 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.ltReady, 0, offsetof(Ts3nCounters, p1bNoFrame) + 4 - offsetof(Ts3nCounters, ltReady));
    const DWORD me = GetCurrentThreadId();
    static DWORD simTid;
    simTid = me;
    g_rtSimTidSlot = &simTid;
    const DWORD savedWorker = g_workerTid;
    g_workerTid = me + 7;
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);

    // a lot table: P+0x48, buckets +0x4C (2 + the end slot), count +0x50; lots 0x10 (head of bucket 0), 0x12 (behind it),
    // 0x13 (bucket 1; its node+0x10 holds another key); the active lot 0x99
    static u8 p[0x200], wm[0x200], act[0x60], nodes[3][0x1A0], portals[3][0x80];
    static DWORD bk[3], vt[2], vec[4];
    static u8* wmSlot;
    wmSlot = wm;
    u8* const* savedWm = g_srWmSlot;
    g_srWmSlot = &wmSlot;
    *(u8**)(wm + 0xB0) = act;
    *(DWORD*)(act + 0x48) = 0x99;
    const DWORD keys[3] = {0x10, 0x12, 0x13};
    for (int i = 0; i < 3; i++) {
        *(DWORD*)nodes[i] = keys[i];
        *(DWORD*)(nodes[i] + 0x10) = keys[i];
    }
    *(DWORD*)(nodes[2] + 0x10) = 0x77;
    bk[0] = (DWORD)(uintptr_t)nodes[0];
    *(u8**)(nodes[0] + 0x190) = nodes[1];
    bk[1] = (DWORD)(uintptr_t)nodes[2];
    bk[2] = 0x5E117E1;
    *(DWORD**)(p + 0x4C) = bk;
    *(DWORD*)(p + 0x50) = 2;
    // lot 0x12's entry: 3 portals (the middle one NULL), front door indices +0x18 = 0, +0x1C = 2
    u8* e = nodes[1] + 0x10;
    vt[0] = (DWORD)(uintptr_t)FkPortalAddRef;
    vt[1] = (DWORD)(uintptr_t)FkPortalRelease;
    for (int i = 0; i < 3; i++) {
        *(DWORD**)portals[i] = vt;
        *(DWORD*)(portals[i] + 8) = 0x1000 + i;
        *(DWORD*)(portals[i] + 0xC) = 0xA0 + i;
        *(LONG*)(portals[i] + 0x70) = 1;
        vec[i] = i == 1 ? 0 : (DWORD)(uintptr_t)portals[i];
    }
    *(DWORD**)(e + 0x44) = vec;
    *(DWORD**)(e + 0x48) = vec + 3;
    *(DWORD*)(e + 0x18) = 0;
    *(DWORD*)(e + 0x1C) = 2;
    const DWORD savedAdd = g_ltAddRef, savedRel = g_ltRelease;
    g_ltAddRef = vt[0];
    g_ltRelease = vt[1];
    auto sum = [&] { return Fnv32(p, sizeof(p)) ^ Fnv32(nodes, sizeof(nodes)) ^ Fnv32(portals, sizeof(portals)) ^ Fnv32(vec, sizeof(vec)) ^ Fnv32(act, sizeof(act)); };
    auto fd = [&](DWORD lo, DWORD hi, DWORD type, u64 want, const u8* wantPt) {
        u64 r;
        const u8* pt;
        const DWORD before = sum();
        const int why = LtFrontDoor(p, lo, hi, type, &r, &pt);
        assert(sum() == before);  // the clone writes nothing (no refcount either)
        if (!why) assert(r == want && pt == wantPt);
        return why;
    };
    const u64 id0 = 0xA0ull << 32 | 0x1000, id2 = 0xA2ull << 32 | 0x1002;
    assert(fd(0, 0, 0, 0, NULL) == 0 && fd(0xFFFFFFFF, 0xFFFFFFFF, 0, 0, NULL) == 0);  // Contains rejects
    *(DWORD*)(act + 0x48) = 0x12;
    assert(fd(0x12, 0, 0, 0, NULL) == 0);  // the active lot
    *(DWORD*)(act + 0x48) = 0x99;
    assert(fd(0x12, 0, 0, id2, portals[2]) == 0);  // type 0: +0x1C valid
    assert(fd(0x12, 0, 1, id2, portals[2]) == 0 && fd(0x12, 0, 2, id0, portals[0]) == 0 && fd(0x12, 0, 3, 0, NULL) == 0);
    assert(fd(0x12, 1, 0, 0, NULL) == 0 && fd(0x14, 0, 0, 0, NULL) == 0);  // chain miss (hi differs), chain end
    assert(fd(0x13, 0, 0, 0, NULL) == 0);  // node+0x10 holds another key: Contains says no
    *(DWORD*)(e + 0x1C) = 0xFFFFFFFF;
    assert(fd(0x12, 0, 0, id0, portals[0]) == 0 && fd(0x12, 0, 1, 0, NULL) == 0);  // -1: type 0 takes +0x18, type 1 none
    *(DWORD*)(e + 0x1C) = 3;
    assert(fd(0x12, 0, 0, id0, portals[0]) == 0 && fd(0x12, 0, 1, 0, NULL) == 0);  // >= n: the same
    *(DWORD*)(e + 0x18) = 1;
    assert(fd(0x12, 0, 0, 0, NULL) == 0 && fd(0x12, 0, 2, 0, NULL) == 0);  // NULL portal
    *(DWORD*)(e + 0x18) = 0xFFFFFFFF;
    assert(fd(0x12, 0, 2, 0, NULL) == 0);
    *(DWORD*)(e + 0x18) = 0;
    *(DWORD*)(e + 0x1C) = 2;
    assert(fd(0x10, 0, 0, 0, NULL) == 0);  // head of the chain, no portals
    // doubts: slots that are not the count pair, a count below 1 (the original's Release would delete), no table, no active lot
    vt[1] = (DWORD)(uintptr_t)FkPortalAddRef;
    assert(fd(0x12, 0, 0, 0, NULL) == LT_FB_SHAPE);
    vt[1] = (DWORD)(uintptr_t)FkPortalRelease;
    *(LONG*)(portals[2] + 0x70) = 0;
    assert(fd(0x12, 0, 0, 0, NULL) == LT_FB_SHAPE);
    *(LONG*)(portals[2] + 0x70) = 1;
    *(DWORD*)(p + 0x50) = 0;
    assert(fd(0x12, 0, 0, 0, NULL) == LT_FB_SHAPE);
    *(DWORD*)(p + 0x50) = 2;
    *(u8**)(wm + 0xB0) = NULL;
    assert(fd(0x12, 0, 0, 0, NULL) == LT_FB_NOWM);
    *(u8**)(wm + 0xB0) = act;
    wmSlot = NULL;
    assert(fd(0x12, 0, 0, 0, NULL) == LT_FB_NOWM);
    wmSlot = wm;

    // QL rules: never nested (QL exclusive or shared already held on this thread), never with a lost writer frame; T-L2 flag
    // set only inside the hold
    u64 r;
    const u8* pt;
    assert(LtClone(p, 0x12, 0, 0, &r, &pt) == LT_OK && r == id2 && t_qlShared == 0 && TryAcquireSRWLockExclusive(&g_ql));
    ReleaseSRWLockExclusive(&g_ql);
    t_qlDepth = 1;
    assert(LtClone(p, 0x12, 0, 0, &r, &pt) == LT_FB_NESTED && r == 0);
    t_qlDepth = 0;
    t_qlShared = 1;
    assert(LtClone(p, 0x12, 0, 0, &r, &pt) == LT_FB_NESTED);
    t_qlShared = 0;
    g_steerBroken = 1;
    assert(LtClone(p, 0x12, 0, 0, &r, &pt) == LT_FB_BROKEN && TryAcquireSRWLockExclusive(&g_ql));
    ReleaseSRWLockExclusive(&g_ql);
    g_steerBroken = 0;
    // T-L2: a Lock_Acquire while this thread holds QL shared is counted
    const LockAcquireFn savedLock = g_lockOrig;
    g_lockOrig = [](void*, void*, const DWORD*) -> LONG { return 1; };
    static const DWORD inf = 0xFFFFFFFF;
    static u8 otherLock[0x28];
    t_qlShared = 1;
    LockAcquireHook(otherLock, NULL, &inf);
    t_qlShared = 0;
    LockAcquireHook(otherLock, NULL, &inf);
    assert(k.ltQlHeldBlock == 1);
    g_lockOrig = savedLock;

    // the hook: switches off, not the owner, no job, a job (read-only answer), each fall-back
    const FdFn savedFd = g_fdOrig;
    g_fdOrig = FkFd;
    g_fdCalls = 0;
    const u8* out = NULL;
    g_async.owner = me;
    assert(FdHook(p, NULL, 0x12, 0, 0, &out) == id2 && g_fdCalls == 1 && k.ltCalls == 0);  // switches off
    k.ltMode = 1;
    g_async.owner = me + 1;
    assert(FdHook(p, NULL, 0x12, 0, 0, &out) == id2 && g_fdCalls == 2 && k.ltCalls == 0);
    g_async.owner = me;
    g_async.state = A_IDLE;
    assert(FdHook(p, NULL, 0x12, 0, 0, &out) == id2 && g_fdCalls == 3 && k.ltCalls == 1 && k.ltShadowRuns == 0);
    g_async.state = A_RUNNING;
    out = NULL;
    assert(FdHook(p, NULL, 0x12, 0, 0, &out) == id2 && g_fdCalls == 3 && k.ltTaken == 1 && out == portals[2]);
    assert(FdHook(p, NULL, 0x12, 0, 3, NULL) == 0 && g_fdCalls == 3 && k.ltTaken == 2);
    g_inInvoke = 1;
    assert(FdHook(p, NULL, 0x12, 0, 0, NULL) == id2 && g_fdCalls == 4 && k.ltFb[LT_FB_STATE - 1] == 1);
    g_inInvoke = 0;
    k.rgOwnerTid = (LONG)me;
    assert(FdHook(p, NULL, 0x12, 0, 0, NULL) == id2 && g_fdCalls == 5 && k.ltFb[LT_FB_STATE - 1] == 2);
    k.rgOwnerTid = 0;
    t_rg.n = 1;  // a gate frame open on this thread
    assert(FdHook(p, NULL, 0x12, 0, 0, NULL) == id2 && g_fdCalls == 6 && k.ltFb[LT_FB_STATE - 1] == 3);
    t_rg.n = 0;
    t_qlDepth = 1;
    assert(FdHook(p, NULL, 0x12, 0, 0, NULL) == id2 && g_fdCalls == 7 && k.ltFb[LT_FB_NESTED - 1] == 1);
    t_qlDepth = 0;
    *(LONG*)(portals[2] + 0x70) = 0;
    FdHook(p, NULL, 0x12, 0, 0, NULL);  // (the fake game call refuses it too)
    assert(g_fdCalls == 8 && k.ltFb[LT_FB_SHAPE - 1] == 1);
    *(LONG*)(portals[2] + 0x70) = 1;
    // QL held exclusive by a worker writer: try fails, the game's call
    HANDLE held = CreateEventW(NULL, TRUE, FALSE, NULL), go = CreateEventW(NULL, TRUE, FALSE, NULL);
    struct Hold { HANDLE held, go; } h = {held, go};
    HANDLE th = CreateThread(NULL, 0, [](void* a) -> DWORD {
        Hold* hh = (Hold*)a;
        AcquireSRWLockExclusive(&g_ql);
        SetEvent(hh->held);
        WaitForSingleObject(hh->go, 5000);
        ReleaseSRWLockExclusive(&g_ql);
        return 0;
    }, &h, 0, NULL);
    WaitForSingleObject(held, 5000);
    assert(LtClone(p, 0x12, 0, 0, &r, &pt) == LT_FB_TRY);
    assert(FdHook(p, NULL, 0x12, 0, 0, NULL) == id2 && g_fdCalls == 9 && k.ltFb[LT_FB_TRY - 1] == 1);
    SetEvent(go);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    CloseHandle(held);
    CloseHandle(go);
    // shadow (no job): the game's call, then the clone; a different answer is counted, *out compared with what the game left
    k.ltMode = 0;
    k.ltShadow = 1;
    assert(FdHook(p, NULL, 0x12, 0, 0, NULL) == id2 && g_fdCalls == 10 && k.ltFb[LT_FB_TRY - 1] == 1);  // job: game only
    g_async.state = A_IDLE;
    out = (const u8*)(uintptr_t)0x1234;
    assert(FdHook(p, NULL, 0x14, 0, 0, &out) == 0 && out == (const u8*)(uintptr_t)0x1234 && g_fdCalls == 11);
    assert(FdHook(p, NULL, 0x12, 0, 0, &out) == id2 && out == portals[2] && g_fdCalls == 12);
    assert(k.ltShadowRuns == 2 && k.ltShadowMismatch == 0 && g_rgOffSimOpen == 0);
    g_fdSkew = 1;
    FdHook(p, NULL, 0x12, 0, 0, NULL);
    assert(k.ltShadowRuns == 3 && k.ltShadowMismatch == 1);
    g_fdSkew = 0;
    k.ltShadow = 0;
    g_fdOrig = savedFd;

    // P1b: second frames at the fixed offsets of the four gate-12 keys, the ebp frame of the gate-0 key
    DWORD st[48] = {};
    st[37] = 0x0080CF36;  // slot[7]
    P1bNote(12, 0x0062F782, &st[30], 100);
    P1bNote(12, 0x0062F782, &st[30], 50);
    assert(k.p1bKey[0] == (LONG)(1u << 24 | 0x80CF36) && k.p1bN[0] == 2 && k.p1bUs[0] == 150);
    st[34] = 0x0076F15D;  // slot[4]
    P1bNote(12, 0x0062FB74, &st[30], 1);
    st[34] = 0x00788373;
    P1bNote(12, 0x00631814, &st[30], 1);
    st[39] = 0x00634F15;  // slot[9]
    P1bNote(12, 0x00631F40, &st[30], 1);
    assert(k.p1bKey[1] == (LONG)(2u << 24 | 0x76F15D) && k.p1bKey[2] == (LONG)(3u << 24 | 0x788373) &&
           k.p1bKey[3] == (LONG)(4u << 24 | 0x634F15));
    st[24] = (DWORD)(uintptr_t)&st[40];  // slot[-6] = ebp of 0x62EF30; [ebp + 4] = its return address
    st[41] = 0x0062F3D0;
    P1bNote(0, 0x0062EF7A, &st[30], 1);
    assert(k.p1bKey[4] == (LONG)(5u << 24 | 0x62F3D0) && k.p1bN[4] == 1);
    st[24] = (DWORD)(uintptr_t)&st[10];  // ebp below the slot: no frame
    P1bNote(0, 0x0062EF7A, &st[30], 1);
    assert(k.p1bNoFrame == 1);
    P1bNote(0, 0x0062F782, &st[30], 1);  // a gate-12 key seen at another gate, an unknown key: nothing
    P1bNote(12, 0x00123456, &st[30], 1);
    st[37] = 0x10001000;  // a return address outside the exe
    P1bNote(12, 0x0062F782, &st[30], 1);
    assert(k.p1bKey[5] == (LONG)(1u << 24 | 0xFFFFFF) && !k.p1bKey[6] && k.p1bNoFrame == 1);
    for (DWORD i = 0; i < 32; i++) {  // the table fills, then p1bOther
        st[37] = 0x00500000 + i;
        P1bNote(12, 0x0062F782, &st[30], 1);
    }
    assert(k.p1bOther == 6 && k.p1bKey[31] == (LONG)(1u << 24 | 0x500019));
    // through the gate stub's entry: an enforced gate-12 wait during a job is keyed by its second frame too
    memset(k.p1bKey, 0, sizeof(k.p1bKey) * 3 + 8);
    {
        const HANDLE savedDone = g_asyncDone, savedInv = g_invReady;
        g_lockOrig = RgTryAcquire;
        g_rgRelOrig = RgFakeRelease;
        InitializeCriticalSection(&g_rgFakeCs);
        g_asyncDone = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_invReady = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_enfHeld = CreateEventW(NULL, TRUE, FALSE, NULL);
        k.rgReady = 7;
        k.rgEnforce = 1;
        g_async.lock = g_rgLockObj;
        g_rgLock = g_rgLockObj;
        g_p1bOn = 1;
        st[30] = 0x0062FB74;
        st[34] = 0x0080D798;
        HANDLE w = EnfStartWorker();
        RgEnter(12, &st[30]);
        assert(k.p1bKey[0] == (LONG)(2u << 24 | 0x80D798) && k.p1bN[0] == 1 && k.p1bUs[0] >= 10000 && t_rg.n == 1);
        assert(RgLeave() == 0x0062FB74 && *(LONG*)(g_rgLockObj + 0x20) == 0 && g_rgOffSimOpen == 0);
        EnfJoin(w);
        g_p1bOn = 0;
        g_async.state = A_IDLE;
        g_async.lock = NULL;
        g_rgLock = NULL;
        k.rgEnforce = 0;
        k.rgReady = 0;
        k.rgOwnerTid = 0;
        g_lockOrig = savedLock;
        CloseHandle(g_asyncDone);
        CloseHandle(g_invReady);
        CloseHandle(g_enfHeld);
        g_asyncDone = savedDone;
        g_invReady = savedInv;
        DeleteCriticalSection(&g_rgFakeCs);
    }

    // T-L3: the counting thunks in the vtable slots, by thread, during a job only
    static u8 portal[0x80];
    static DWORD pvt[2];
    pvt[0] = vt[0];
    pvt[1] = vt[1];
    assert(InstallPortalRefThunks(pvt) && pvt[0] == (DWORD)(uintptr_t)PortalAddRefThunk);
    assert(!InstallPortalRefThunks(pvt));  // swapped already: refused
    *(DWORD**)portal = pvt;
    *(LONG*)(portal + 0x70) = 1;
    typedef LONG(__fastcall * RefFn)(u8*, void*);
    g_async.state = A_RUNNING;
    assert(((RefFn)(uintptr_t)pvt[0])(portal, NULL) == 2 && ((RefFn)(uintptr_t)pvt[1])(portal, NULL) == 1);
    assert(k.portalRef[0] == 1 && k.portalRef[4] == 1);
    simTid = me + 3;
    g_workerTid = me;
    t_qlDepth = 1;
    ((RefFn)(uintptr_t)pvt[0])(portal, NULL);
    t_qlDepth = 0;
    ((RefFn)(uintptr_t)pvt[1])(portal, NULL);
    g_workerTid = me + 7;
    ((RefFn)(uintptr_t)pvt[0])(portal, NULL);
    g_async.state = A_IDLE;
    ((RefFn)(uintptr_t)pvt[0])(portal, NULL);
    assert(k.portalRef[1] == 1 && k.portalRef[6] == 1 && k.portalRef[3] == 1 && *(LONG*)(portal + 0x70) == 3);
    assert(k.portalRef[0] + k.portalRef[1] + k.portalRef[2] + k.portalRef[3] == 3);

    g_ltAddRef = savedAdd;
    g_ltRelease = savedRel;
    g_srWmSlot = savedWm;
    g_workerTid = savedWorker;
    g_async.owner = 0;
    k.ltMode = 0;
    simTid = 0;
}

// --- P10 (docs/87 §6): route IC tags, plan time, early returns, gate-10 waits by IC; G10-C frame (docs/87 §5) ---
static DWORD g_p10PlanTid;
static u8 __fastcall P10FakePlan(void*, void*, int*, int*, int*, DWORD, DWORD, u8*, int*, int*, DWORD, DWORD) {
    g_p10PlanTid = GetCurrentThreadId();
    Sleep(2);
    return 1;
}
// --- design SH (slotro.cpp, docs/90): interlocked portal counts, gate pass / drop, hook dispatch, shadow, fallbacks ---
static int g_slDel;
static void __fastcall SlFkDelete(u8* p, void*, DWORD flag) { assert(flag == 1 && *(LONG*)(p + 0x70) <= 1); g_slDel++; }
static int g_slOrigCalls, g_slOrigHold, g_slTripLock, g_slSkew, g_slThrow;
static DWORD __cdecl SlFkOrig(float* pt, const u8*, DWORD ags, DWORD) {
    g_slOrigCalls++;
    g_slOrigHold = t_slHold * 10 + t_qlShared;
    if (g_slTripLock) QlSharedTrip(SLD_LOCK);  // a Lock_Acquire inside the call
    if (g_slThrow) RaiseException(0xE0005151, 0, 0, NULL);
    pt[0] += 1.0f;
    pt[1] = (float)ags;
    return t_slDry && g_slSkew ? 0 : 1;
}
static void TestSlotRo() {
    static_assert(offsetof(Ts3nCounters, arReady) == 0x14A4 && offsetof(Ts3nCounters, orReady) == 0x1524 &&
                  offsetof(Ts3nCounters, slReady) == 0x1564 && offsetof(Ts3nCounters, slMode) == 0x1568 &&
                  offsetof(Ts3nCounters, slShadow) == 0x156C && offsetof(Ts3nCounters, slPass12) == 0x1580 &&
                  offsetof(Ts3nCounters, slDrop) == 0x1588 && offsetof(Ts3nCounters, slFb) == 0x15A0 &&
                  offsetof(Ts3nCounters, slShadowRuns) == 0x15B0 && offsetof(Ts3nCounters, slRelRetry) == 0x15C0 &&
                  sizeof(Ts3nCounters) >= 1393 * 4, "v42 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.slReady, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, slReady));
    // interlocked count pair: same results as 0x64E8E0 / 0x64E8F0, delete through slot +8 at a count <= 1, no lost update
    static DWORD vt[3];
    static u8 pt[0x80];
    vt[2] = (DWORD)(uintptr_t)SlFkDelete;
    *(DWORD**)pt = vt;
    *(LONG*)(pt + 0x70) = 1;
    assert(PortalAddRefX(pt, NULL) == 2 && PortalReleaseX(pt, NULL) == 1 && g_slDel == 0);
    assert(PortalReleaseX(pt, NULL) == 0 && g_slDel == 1 && *(LONG*)(pt + 0x70) == 1);  // deleted, count left as the original does
    HANDLE th[2];
    for (int i = 0; i < 2; i++)
        th[i] = CreateThread(NULL, 0, [](void*) -> DWORD {
            for (int n = 0; n < 200000; n++) { PortalAddRefX(pt, NULL); PortalReleaseX(pt, NULL); }
            return 0;
        }, NULL, 0, NULL);
    for (int n = 0; n < 200000; n++) { PortalAddRefX(pt, NULL); PortalReleaseX(pt, NULL); }
    WaitForMultipleObjects(2, th, TRUE, INFINITE);
    CloseHandle(th[0]);
    CloseHandle(th[1]);
    assert(*(LONG*)(pt + 0x70) == 1 && g_slDel == 1);  // 600,000 pairs on three threads: no update lost, never deleted

    // a lot table with lot 0x12 holding two portals of object 0x5000:0x7, one of another object
    const DWORD me = GetCurrentThreadId();
    static u8 p[0x200], wm[0x200], act[0x60], node[0x1A0], prt[3][0x80];
    static DWORD bk[2], vec[3];
    static u8* wmSlot;
    wmSlot = wm;
    u8* const* savedWm = g_srWmSlot;
    g_srWmSlot = &wmSlot;
    *(u8**)(wm + 0xB0) = act;
    *(DWORD*)(act + 0x48) = 0x99;
    *(DWORD*)node = 0x12;
    *(DWORD*)(node + 0x10) = 0x12;
    bk[0] = (DWORD)(uintptr_t)node;
    bk[1] = 0x5E117E1;
    *(DWORD**)(p + 0x4C) = bk;
    *(DWORD*)(p + 0x50) = 1;
    for (int i = 0; i < 3; i++) {
        *(DWORD*)prt[i] = LT_PORTAL_VT;
        *(DWORD*)(prt[i] + 8) = i < 2 ? 0x5000 : 0x6000;
        *(DWORD*)(prt[i] + 0xC) = 7;
        *(LONG*)(prt[i] + 0x70) = 1;
        vec[i] = (DWORD)(uintptr_t)prt[i];
    }
    *(DWORD**)(node + 0x10 + 0x44) = vec;
    *(DWORD**)(node + 0x10 + 0x48) = vec + 3;
    // gate stacks: f[2] = ecx of the stub's pushad frame (slot[-2]), slot = f + 4
    DWORD f[16] = {};
    DWORD* slot = f + 4;
    auto hold = [&] { assert(SlBegin(false) == SL_OK && t_slHold == 1 && t_qlShared == 1); };
    auto end = [&] { return SlEnd(); };
    auto g12 = [&](DWORD lo, DWORD ret, DWORD idLo) {
        slot[-2] = (DWORD)(uintptr_t)p, slot[0] = ret, slot[1] = lo, slot[2] = 0, slot[9] = idLo, slot[10] = 7;
        return SlGate(12, slot);
    };
    DWORD key[2];
    auto g0 = [&](DWORD lo) {
        key[0] = lo, key[1] = 0;
        slot[-2] = (DWORD)(uintptr_t)(p + 0x48), slot[0] = 0x0062F7A2, slot[1] = (DWORD)(uintptr_t)key;
        return SlGate(0, slot);
    };
    hold();
    assert(g12(0x12, SL_PFO_RET, 0x5000) && g12(0x12, 0x0062FB74, 0x5000) && g12(0x44, SL_PFO_RET, 0x5000));  // hit, other caller, miss
    assert(g0(0x12) && t_slHold == 1 && k.slPass12 == 3 && k.slPass0 == 1);
    *(LONG*)(prt[1] + 0x70) = 0;  // its Release would delete
    assert(!g12(0x12, SL_PFO_RET, 0x5000) && t_slHold == 2 && t_qlShared == 0 && k.slDrop[SLD_PORTAL] == 1);
    assert(!end() && t_slHold == 0);
    hold();
    assert(g12(0x12, SL_PFO_RET, 0x6000));  // another object's portals only: the bad one is not its
    *(LONG*)(prt[1] + 0x70) = 1;
    *(DWORD*)prt[0] = 0x00FED999;  // another class
    assert(!g12(0x12, SL_PFO_RET, 0x5000) && k.slDrop[SLD_PORTAL] == 2);
    end();
    *(DWORD*)prt[0] = LT_PORTAL_VT;
    hold();
    assert(!g0(0x44) && k.slDrop[SLD_MISS] == 1 && t_slHold == 2);  // GetOrInsert miss would insert under shared QL
    end();
    hold();
    assert(!g0(0x99) && k.slDrop[SLD_MISS] == 2);  // the active lot: Contains rules say no entry, so no pass
    end();
    hold();
    assert(!SlGate(3, slot) && k.slDrop[SLD_GATE] == 1);  // any other gate site
    end();
    hold();
    *(DWORD*)(p + 0x50) = 0;
    assert(!g0(0x12) && k.slDrop[SLD_TABLE] == 1);  // the original would divide by 0
    *(DWORD*)(p + 0x50) = 1;
    end();
    hold();
    const LONG tl2 = k.ltQlHeldBlock;
    QlSharedTrip(SLD_MANAGED);
    assert(t_slHold == 2 && k.slDrop[SLD_MANAGED] == 1 && k.ltQlHeldBlock == tl2);  // a managed call drops, never counts in T-L2
    end();
    // SlBegin fallbacks
    t_qlDepth = 1;
    assert(SlBegin(false) == SL_FB_NESTED);
    t_qlDepth = 0;
    g_steerBroken = 1;
    assert(SlBegin(false) == SL_FB_BROKEN);
    g_steerBroken = 0;

    // the hook: off, no flags, not the owner, no job, job (hold), drop inside, state fallbacks, QL busy, shadow, unwind
    g_slOrig = SlFkOrig;
    g_async.owner = me;
    g_async.state = A_RUNNING;
    __declspec(align(16)) float pt4[4] = {1, 2, 3, 0};
    const u8* obj = p;
    assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigHold == 0 && k.slCalls == 0);  // both switches off
    k.slMode = 1;
    assert(SlHook(pt4, obj, 0, 0) == 1 && SlHook(pt4, NULL, 4, 0) == 1 && k.slCalls == 0);  // returns before any routing read
    g_async.owner = me + 1;
    assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigHold == 0 && k.slCalls == 0);
    g_async.owner = me;
    assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigHold == 11 && k.slCalls == 1 && k.slTaken == 1 && t_slHold == 0 && t_qlShared == 0);
    assert(TryAcquireSRWLockExclusive(&g_ql));  // released after the call
    ReleaseSRWLockExclusive(&g_ql);
    g_slTripLock = 1;
    assert(SlHook(pt4, obj, 4, 0) == 1 && k.slTaken == 1 && k.slDrop[SLD_LOCK] == 1 && t_slHold == 0);
    g_slTripLock = 0;
    g_inInvoke = 1;
    assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigHold == 0 && k.slFb[SL_FB_STATE - 1] == 1);
    g_inInvoke = 0;
    k.rgOwnerTid = (LONG)me;
    assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigHold == 0 && k.slFb[SL_FB_STATE - 1] == 2);
    k.rgOwnerTid = 0;
    t_rg.n = 1;
    assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigHold == 0 && k.slFb[SL_FB_STATE - 1] == 3);
    t_rg.n = 0;
    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE qh = CreateThread(NULL, 0, [](void* e) -> DWORD {
            AcquireSRWLockExclusive(&g_ql);
            SetEvent((HANDLE)e);
            Sleep(200);
            ReleaseSRWLockExclusive(&g_ql);
            return 0;
        }, ev, 0, NULL);
        WaitForSingleObject(ev, INFINITE);
        assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigHold == 0 && k.slFb[SL_FB_TRY - 1] == 1);  // a worker writer holds QL
        WaitForSingleObject(qh, INFINITE);
        CloseHandle(qh);
        CloseHandle(ev);
    }
    // an exception through the held call still releases QL
    g_slThrow = 1;
    __try {
        SlHook(pt4, obj, 4, 0);
        assert(!"no exception");
    } __except (GetExceptionCode() == 0xE0005151 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
    }
    g_slThrow = 0;
    assert(t_slHold == 0 && t_qlShared == 0 && TryAcquireSRWLockExclusive(&g_ql));
    ReleaseSRWLockExclusive(&g_ql);
    // shadow (no job): the game's call on the point, then the dry hold on a copy; equal, then a skewed answer counted
    g_async.state = A_IDLE;
    assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigHold == 0 && k.slShadowRuns == 0);  // shadow off: the game's call only
    k.slShadow = 1;
    const int calls0 = g_slOrigCalls;
    pt4[0] = 1;
    assert(SlHook(pt4, obj, 4, 0) == 1 && g_slOrigCalls == calls0 + 2 && g_slOrigHold == 11 && pt4[0] == 2.0f);
    assert(k.slShadowRuns == 1 && k.slShadowMismatch == 0 && g_rgOffSimOpen == 0 && t_slHold == 0);
    g_slSkew = 1;
    assert(SlHook(pt4, obj, 4, 0) == 1 && k.slShadowMismatch == 1);  // the game's answer is returned
    g_slSkew = 0;
    g_slTripLock = 1;
    assert(SlHook(pt4, obj, 4, 0) == 1 && k.slShadowDrop == 1 && k.slShadowMismatch == 1);
    g_slTripLock = 0;
    k.slMode = k.slShadow = 0;
    g_async.owner = 0;
    g_srWmSlot = savedWm;
    memset(&k.slReady, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, slReady));
    puts("slot position RO (SH): interlocked counts, gate pass/drop, hook, fallbacks, shadow, unwind ok");
}

static void TestP10() {
    static_assert(offsetof(Ts3nCounters, p10Ready) == 0x1424 && offsetof(Ts3nCounters, p10Entries) == 0x1428 &&
                  offsetof(Ts3nCounters, p10PlanUs) == 0x1438 && offsetof(Ts3nCounters, p10EarlyRet) == 0x1448 &&
                  offsetof(Ts3nCounters, p10CanYield) == 0x1458 && sizeof(Ts3nCounters) >= 1305 * 4, "v38 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.p10Ready, 0, sizeof(LONG) * 16);
    memset(k.p1bKey, 0, sizeof(k.p1bKey) * 3 + 8);
    const DWORD me = GetCurrentThreadId();
    static DWORD simTid;
    simTid = me;
    g_rtSimTidSlot = &simTid;
    const DWORD savedWorker = g_workerTid;
    const LockAcquireFn savedLock = g_lockOrig;
    const PlanLockedFn savedPlan = g_planOrig;
    const HANDLE savedDone = g_asyncDone, savedInv = g_invReady;
    g_lockOrig = RgTryAcquire;
    g_rgRelOrig = RgFakeRelease;
    InitializeCriticalSection(&g_rgFakeCs);
    g_asyncDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_invReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_enfHeld = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    g_workerTid = me + 7;
    g_async.owner = me;
    g_rgLock = g_rgLockObj;
    g_planOrig = P10FakePlan;
    g_hostPtr = &g_fakeHostPtr;
    g_curState = &g_fakeCur;
    FakeTask(kRoot, 7);
    DWORD st[48] = {};
    const DWORD thunk = (DWORD)(uintptr_t)RgLeaveThunk;
    int list[2] = {}, pts[2] = {1, 2};  // starts != ends: the hook plans (no early-out)
    u8 opts[0x38] = {};

    // off: nothing tagged
    st[40] = 0x111;
    P10Enter(1, &st[40]);
    assert(t_p10 == 0 && t_rg.n == 0 && st[40] == 0x111 && k.p10Entries[1] == 0);
    g_p10On = 1;
    // another thread than the sim: not tagged
    simTid = me + 1;
    P10Enter(1, &st[40]);
    assert(t_p10 == 0 && t_rg.n == 0);
    simTid = me;

    // 1. tags nest and clear; a reader frame, not a gate frame; CanYield counted (not for Plan)
    P10Enter(1, &st[40]);
    assert(t_p10 == 2 && st[40] == thunk && t_rg.n == 1 && t_rg.readers == 1 && k.p10Entries[1] == 1 && k.p10CanYield[0] == 1);
    st[30] = 0x222;
    FakeTask(kNested, 7);  // a nested interpreter state: no yield here
    P10Enter(2, &st[30]);
    FakeTask(kRoot, 7);
    assert(t_p10 == 3 && t_rg.n == 2 && k.p10Entries[2] == 1 && k.p10CanYield[1] == 0);
    // gate site 10 reached inside PlanFromPoint, no plan: an early return of that IC at its leave
    k.rgReady = 7;
    const LONG unl10 = k.rgUnlocked[10];
    st[20] = 0x00760BFF;
    RgEnter(10, &st[20]);
    assert(t_p10 == (3 | P10_REACHED) && k.rgUnlocked[10] == unl10 + 1 && t_rg.n == 3);
    assert(RgLeave() == 0x00760BFF && RgLeave() == 0x222 && t_p10 == 2 && k.p10EarlyRet[2] == 1);
    // a plan inside Replan: timed and charged to Replan; reached + planned is no early return
    st[20] = 0x00760BFF;
    RgEnter(10, &st[20]);
    RgLeave();
    k.rgReady = 0;  // PlanLockedHook would learn a lock from the NULL world otherwise
    assert(PlanLockedHook(NULL, NULL, list, pts, pts, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_p10PlanTid == me);
    assert(k.p10PlanUs[1] >= 1000 && k.p10PlanUs[0] == 0 && t_p10 == (2 | P10_REACHED | P10_PLANNED));
    assert(RgLeave() == 0x111 && t_p10 == 0 && t_rg.n == 0 && t_rg.readers == 0 && k.p10EarlyRet[1] == 0);
    // no tag: a plan is not charged
    const LONG planUs1 = k.p10PlanUs[1];
    PlanLockedHook(NULL, NULL, list, pts, pts, 0x100, 1, opts, NULL, NULL, 1, 0);
    assert(k.p10PlanUs[1] == planUs1 && k.p10PlanUs[0] + k.p10PlanUs[2] + k.p10PlanUs[3] == 0);

    // 2. a frame left without returning (managed exception): dropped at the next entry, its tag cleared
    k.rgReady = 7;
    st[40] = 0x333;
    P10Enter(3, &st[40]);
    st[30] = 0x00760BFF;
    RgEnter(10, &st[30]);
    assert(t_p10 == (4 | P10_REACHED) && t_rg.n == 2);
    st[44] = 0x444;
    P10Enter(1, &st[44]);  // above both: they are dead
    assert(t_p10 == 2 && t_rg.n == 1 && t_rg.f[0].ret == 0x444 && k.p10EarlyRet[3] == 1 && k.p10Entries[3] == 1);
    // a stale tag is not charged to a plan outside that IC: the plan's own slot drops it first
    k.rgReady = 0;
    const LONG planUs1b = k.p10PlanUs[1];
    t_rg.f[0].slot = (DWORD*)((u8*)st - 0x10000);  // pretend the Replan frame lay below this call (left by an exception)
    PlanLockedHook(NULL, NULL, list, pts, pts, 0x100, 1, opts, NULL, NULL, 1, 0);
    assert(t_p10 == 0 && t_rg.n == 0 && k.p10PlanUs[1] == planUs1b);
    k.rgReady = 7;

    // 3. IcPlanHook tags Plan through its own return slot (no job: plain sync plan)
    {
        struct Fake { static u8* __cdecl Ic(u8* out, DWORD) {
            assert(t_p10 == 1 && t_rg.n == 1);
            return out;
        } };
        const IcPlanFn savedIc = g_icPlanOrig;
        g_icPlanOrig = Fake::Ic;
        u8 out[0x48];
        assert(IcPlanHook(out, 0x77) == out && t_p10 == 0 && t_rg.n == 0 && k.p10Entries[0] == 1);
        g_icPlanOrig = savedIc;
    }

    // 4. a gate-10 wait during a job, keyed in the P1b table by the IC (row 6): Replan
    k.rgEnforce = 1;
    g_async.lock = g_rgLockObj;
    st[40] = 0x555;
    P10Enter(1, &st[40]);
    st[30] = 0x00760BFF;
    HANDLE w = EnfStartWorker();
    RgEnter(10, &st[30]);
    assert(k.p1bKey[0] == (LONG)(6u << 24 | 0x765490) && k.p1bN[0] == 1 && k.p1bUs[0] >= 10000);
    assert(RgLeave() == 0x00760BFF && RgLeave() == 0x555 && *(LONG*)(g_rgLockObj + 0x20) == 0 && g_rgOffSimOpen == 0);
    EnfJoin(w);
    // without P10 (P1b alone) the row keys by Route_PlanCommon's caller through the ebp frame
    g_p10On = 0;
    st[24] = (DWORD)(uintptr_t)&st[40];
    st[41] = 0x0076429D;
    P1bNote(10, 0x00760BFF, &st[30], 1);
    assert(k.p1bKey[1] == (LONG)(6u << 24 | 0x76429D));
    g_p10On = 1;

    // 5. G10-C: the prologue is checked; the frame on Route_PlanCommon keeps job starts off for the whole plan, the goal
    // step and other gate sites inside are nested (not counted), the plan stays on this thread, a dropped frame closes
    static const u8 badPro[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10};
    static const u8 goodPro[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};
    const RgSite saved10 = kRgSites[10];
    assert(!RgUseG10Common(badPro) && !g_g10c && kRgSites[10].addr == 0x0075C2D0);
    assert(RgUseG10Common(goodPro) && g_g10c && kRgSites[10].addr == (DWORD)(uintptr_t)goodPro);
    g_async.state = A_IDLE;
    st[30] = 0x0076429D;
    RgEnter(10, &st[30]);
    assert(t_g10cOpen == 1 && g_rgOffSimOpen == 1 && t_rg.f[0].rel == (RG_REL_OFFSIM | RG_REL_G10C));
    const LONG unl1 = k.rgUnlocked[1];
    RgEnter(1, &st[26]);  // the goal step's lot planner inside the frame
    assert(k.rgUnlocked[1] == unl1 && t_rg.n == 2 && g_rgOffSimOpen == 1);
    RgLeave();
    k.workerMode = k.workerReady = 1;  // a sync worker plan would wait for a worker held by this frame: planned here
    g_p10PlanTid = 0;
    RunPlan(NULL, NULL, list, pts, pts, 0x100, 1, opts, NULL, NULL, 1, 0);
    assert(g_p10PlanTid == me);
    k.workerMode = k.workerReady = 0;
    assert(RgLeave() == 0x0076429D && t_g10cOpen == 0 && g_rgOffSimOpen == 0);
    // a job running at entry: one wait, +0x918 held to the frame's end
    w = EnfStartWorker();
    st[30] = 0x0076429D;  // a leave does not write the slot back (the thunk returns there itself)
    RgEnter(10, &st[30]);
    assert(*(LONG*)(g_rgLockObj + 0x20) == 1 && t_rg.f[0].rel == (RG_REL_LOCK | RG_REL_OFFSIM | RG_REL_G10C) && t_g10cOpen == 1);
    EnfJoin(w);
    RgEnter(2, &st[26]);
    assert(*(LONG*)(g_rgLockObj + 0x20) == 1 && t_rg.n == 2);  // no second acquire inside
    RgLeave();
    assert(RgLeave() == 0x0076429D && *(LONG*)(g_rgLockObj + 0x20) == 0 && t_g10cOpen == 0 && t_rg.n == 0);
    // dropped: the next outer entry closes it
    RgEnter(10, &st[30]);
    RgEnter(0, &st[34]);
    assert(t_g10cOpen == 0 && t_rg.n == 1 && g_rgOffSimOpen == 1);
    RgLeave();
    assert(g_rgOffSimOpen == 0);
    kRgSites[10] = saved10;
    g_g10c = 0;

    g_p10On = 0;
    k.rgEnforce = 0;
    k.rgReady = 0;
    k.rgOwnerTid = 0;
    g_async.state = A_IDLE;
    g_async.lock = NULL;
    g_async.owner = 0;
    g_rgLock = NULL;
    g_workerTid = savedWorker;
    g_lockOrig = savedLock;
    g_planOrig = savedPlan;
    CloseHandle(g_asyncDone);
    CloseHandle(g_invReady);
    CloseHandle(g_enfHeld);
    g_asyncDone = savedDone;
    g_invReady = savedInv;
    DeleteCriticalSection(&g_rgFakeCs);
    simTid = 0;
    memset(&k.p10Ready, 0, sizeof(LONG) * 16);
}

// --- [ObstructedRO] (obstructro.cpp, docs/86 §10): read-only IC_Route_IsPointObstructedBySim ---
static int g_orRefIn, g_orRefOut, g_orGameCalls, g_orQueryWhy;
static u64 g_orGameAnswer;
static u8* g_orFakeRoute;
static const u8* g_orFps[8];
static int g_orNFps;
static float g_orPtU, g_orPtV;
static u8* g_orCacheSkew;  // the +0xD4 fake writes this cache (a writer the clone must notice)
static void __fastcall FkOrRef(u8*, void*) { g_orRefIn++; }
static void __fastcall FkOrUnref(u8*, void*) { g_orRefOut++; }
static u8* __fastcall FkOrRoute(const void*, void*, DWORD handle) { return handle == 0x25000001 || handle == 0x25000002 ? g_orFakeRoute : NULL; }
static void __cdecl FkOrLevel(const u8*, const float* pt, float* io) {
    assert(((uintptr_t)pt & 15) == 0 && pt[3] == pt[0]);  // 0x76BF90's {x, y, z, x}, 16-aligned as the IC's frame
    io[0] = io[1] = 7.0f;
    *(LONG*)&io[2] = 0;
}
static int FkOrQuery(SrCtx* c, const u8*, DWORD lo, DWORD, const SrPt& pt, float r, const u8* rec) {
    assert(lo == 0x30 && r == 0.25f && pt.v[0] == 3.0f && pt.v[2] == 5.0f && *(const DWORD*)rec == 0xABCD && rec[0x24] == 1);
    c->n = g_orNFps;
    for (int i = 0; i < g_orNFps; i++) c->objs[i] = g_orFps[i];
    return g_orQueryWhy;
}
static int __fastcall FkOrType(const u8* o, void*) { return (signed char)o[0x35]; }
static unsigned __int64 __fastcall FkOrId(const u8* o, void*) { return (u64)*(const DWORD*)(o + 0x18) << 32 | *(const DWORD*)(o + 0x14); }
static u8 __fastcall FkOrC0(const u8* o, void*) { return o[0x48]; }
static u8 __fastcall FkOrPt(const u8* o, void*, DWORD level, float u, float v, float r) {
    assert(level == 0 && r == 0.25f);
    g_orPtU = u, g_orPtV = v;
    if (g_orCacheSkew) g_orCacheSkew[0] ^= 1;
    return o[0x49];
}
static u64 __cdecl FkOrGame(DWORD, OrV3) { g_orGameCalls++; return g_orGameAnswer; }
static void TestObstructedRO() {
    static_assert(offsetof(Ts3nCounters, arReady) == 0x14A4 && offsetof(Ts3nCounters, orReady) == 0x1524 &&
                      offsetof(Ts3nCounters, orMode) == 0x1528 && offsetof(Ts3nCounters, orShadow) == 0x152C &&
                      offsetof(Ts3nCounters, orFb) == 0x1538 && offsetof(Ts3nCounters, orUs) == 0x1550 &&
                      offsetof(Ts3nCounters, orShadowMismatch) == 0x155C && offsetof(Ts3nCounters, orHits) == 0x1560 &&
                      sizeof(Ts3nCounters) >= 1369 * 4,
                  "v41 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.orReady, 0, 16 * 4);
    const DWORD me = GetCurrentThreadId();
    static DWORD simTid;
    simTid = me;
    g_rtSimTidSlot = &simTid;
    const DWORD savedWorker = g_workerTid;
    g_workerTid = me + 7;
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    // world, world manager, planner P with one lot (key 0x30) in a 1-bucket table; identity rows, so (u, v) = (x, z)
    static u8 world[0xA00], wm[0x200], wmw[0x200], p[0x200], lot[0x60], act[0x60], rm[0x10];
    static __declspec(align(16)) u8 node[0x200];
    static DWORD bk[2];
    static u8 *wmSlot, *rmSlot;
    wmSlot = wm;
    rmSlot = rm;
    world[0x974] = 1;
    *(u8**)(world + 0x95C) = p;
    *(u8**)(world + 0x964) = wmw;
    *(u8**)(wm + 0xB0) = act;
    *(DWORD*)(act + 0x48) = 0x99;
    *(DWORD*)(lot + 0x48) = 0x30;
    *(DWORD*)node = 0x30;
    *(DWORD*)(node + 0x10) = 0x30;
    float* rows = (float*)(node + 0x10 + 0xA0);
    memset(rows, 0, 64);
    rows[0] = rows[5] = rows[10] = rows[15] = 1.0f;
    rows[12] = 10.0f;  // translation in x: u = x + 10
    bk[0] = (DWORD)(uintptr_t)node;
    bk[1] = 0x5E117E1;
    *(DWORD**)(p + 0x4C) = bk;
    *(DWORD*)(p + 0x50) = 1;
    static float one = 1.0f, radius = 0.25f;
    static bool lotMissing;
    const SrWorldFn savedWorld = g_srWorldGet;
    const SrLotLookupFn savedLook = g_srLotLookup;
    const SrLotFindFn savedFind = g_srLotFind;
    u8* const* savedWm = g_srWmSlot;
    const float* savedOne = g_srOne;
    g_srWorldGet = []() -> u8* { return world; };
    g_srLotLookup = [](void*, void*, float, float) -> u8* { return lotMissing ? NULL : lot; };
    g_srLotFind = [](const void* tbl, void*, DWORD* o, const DWORD* key) {
        const DWORD* b = *(const DWORD* const*)((const u8*)tbl + 4);
        o[0] = key[0] == *(const DWORD*)(uintptr_t)b[0] ? b[0] : b[1];
    };
    g_srWmSlot = &wmSlot;
    g_srOne = &one;
    g_orRmSlot = &rmSlot;
    g_orRadius = &radius;
    g_orRoute = FkOrRoute;
    g_orLevel = FkOrLevel;
    g_orQuery = FkOrQuery;
    g_srCheckSlots = false;
    // the route: vtable pair +0x10 / +0x14, option word +0x3C, own Sim id +0x40, ignore list at +0x158 + 0x38
    static DWORD rvt[8], ign[4];
    rvt[4] = (DWORD)(uintptr_t)FkOrRef;
    rvt[5] = (DWORD)(uintptr_t)FkOrUnref;
    static u8 route[0x200];
    g_orFakeRoute = route;
    *(DWORD**)route = rvt;
    *(DWORD*)(route + 0x3C) = 0xABCD;
    *(DWORD*)(route + 0x40) = 0x500;
    ign[0] = 0x600;
    *(DWORD**)(route + 0x190) = ign;
    *(DWORD**)(route + 0x194) = ign + 2;
    // footprints (fake class): type +0x35, id +0x14/+0x18, slot +0xC0 = byte +0x48, slot +0xD4 = byte +0x49, cache +0x38
    static DWORD fvt[0x40];
    fvt[3] = (DWORD)(uintptr_t)FkOrType;
    fvt[0x84 / 4] = (DWORD)(uintptr_t)FkOrId;
    fvt[0xC0 / 4] = (DWORD)(uintptr_t)FkOrC0;
    fvt[0xD4 / 4] = (DWORD)(uintptr_t)FkOrPt;
    static u8 fps[6][0x60], caches[6][0x24];
    const DWORD ids[6] = {0x700, 0x500, 0x600, 0x701, 0x702, 0x703};  // own Sim and ignored at 1 and 2
    for (int i = 0; i < 6; i++) {
        *(DWORD**)fps[i] = fvt;
        *(DWORD*)(fps[i] + 0x14) = ids[i];
        fps[i][0x35] = 0x14;
        fps[i][0x48] = 1;
        *(u8**)(fps[i] + 0x38) = caches[i];
        caches[i][0x20] = 1;
        g_orFps[i] = fps[i];
    }
    fps[0][0x35] = 3;     // not a Sim
    fps[3][0x48] = 0;     // slot +0xC0 false
    fps[4][0x49] = 0;     // the point test misses
    fps[5][0x49] = 1;     // the answer
    g_orNFps = 6;
    const OrV3 at = {3.0f, 1.0f, 5.0f};
    u64 r;
    auto run = [&](bool job) {
        const int in0 = g_orRefIn, out0 = g_orRefOut;
        const int why = OrRun(0x25000001, at, job, &r);
        assert(g_orRefIn - in0 == g_orRefOut - out0 && t_qlShared == 0 && t_inRo == false && g_srInClone == 0);  // pair balanced
        return why;
    };
    assert(run(false) == SR_OK && r == 0x703 && g_orPtU == 13.0f && g_orPtV == 5.0f && g_orRefIn == 1);
    assert(run(true) == SR_FB_STATE && r == 0);  // a job answer needs the tripwire stubs armed
    TwArm(true);
    assert(run(true) == SR_OK && r == 0x703);
    fps[5][0x49] = 0;
    assert(run(true) == SR_OK && r == 0);  // no footprint covers the point
    fps[5][0x49] = 1;
    // shapes and doubts: no route, no world manager, no lot: 0 as the IC; dirty lot index, lot not in the table, deferred
    // flush (query), invalid or changed cache, unknown +0xD4 target, planner missing, tripwire, lost writer frame
    assert(OrRun(0x25000009, at, true, &r) == SR_OK && r == 0);
    wmSlot = NULL;
    assert(run(true) == SR_OK && r == 0);
    wmSlot = wm;
    lotMissing = true;
    assert(run(true) == SR_OK && r == 0);
    lotMissing = false;
    wmw[0x1CC] = 1;
    assert(run(true) == SR_FB_DIRTY);
    wmw[0x1CC] = 0;
    *(DWORD*)(lot + 0x48) = 0x31;
    assert(run(true) == SR_FB_LOTMISS && r == 0);  // GetOrInsert would insert
    *(DWORD*)(lot + 0x48) = 0x30;
    g_orQueryWhy = SR_FB_DEFERRED;
    assert(run(true) == SR_FB_DEFERRED && OrMap(SR_FB_DEFERRED) == OR_FB_LOT);
    g_orQueryWhy = SR_OK;
    caches[4][0x20] = 0;
    assert(run(true) == SR_FB_CACHE && r == 0);  // the original would rebuild level 0
    caches[4][0x20] = 1;
    g_orCacheSkew = caches[4];
    assert(run(true) == SR_FB_CACHE);
    g_orCacheSkew = NULL;
    caches[4][0] = 0;
    g_srCheckSlots = true;
    assert(run(true) == SR_FB_SHAPE);  // +0xD4 is not 0x80F030
    g_srCheckSlots = false;
    world[0x974] = 0;
    assert(run(true) == SR_FB_SHAPE);
    world[0x974] = 1;
    g_twActive = 1;
    assert(run(true) == SR_FB_TRIP);
    g_twActive = 0;
    g_steerBroken = 1;
    assert(run(true) == SR_FB_STATE);
    g_steerBroken = 0;
    t_qlDepth = 1;
    assert(run(true) == SR_FB_STATE);
    t_qlDepth = 0;
    assert(run(true) == SR_OK && r == 0x703);

    // the hook: switches off, not the owner, no job, a job (read-only answer), each state fall-back, QL busy, shadow
    const OrFn savedOrig = g_orOrig;
    g_orOrig = FkOrGame;
    g_orGameAnswer = 0x703;
    g_async.owner = me;
    g_async.handle = 0x25000002;
    g_async.state = A_RUNNING;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 1 && k.orCalls == 0);  // switches off
    k.orMode = 1;
    g_async.owner = me + 1;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 2 && k.orCalls == 0);
    g_async.owner = me;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 2 && k.orTaken == 1 && k.orHits == 1);
    fps[5][0x49] = 0;
    assert(OrHook(0x25000001, at) == 0 && g_orGameCalls == 2 && k.orTaken == 2 && k.orHits == 1);
    fps[5][0x49] = 1;
    assert(OrHook(0x25000002, at) == 0x703 && g_orGameCalls == 3 && k.orFb[OR_FB_STATE - 1] == 1);  // the job plans this route
    g_inInvoke = 1;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 4 && k.orFb[OR_FB_STATE - 1] == 2);
    g_inInvoke = 0;
    k.rgOwnerTid = (LONG)me;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 5 && k.orFb[OR_FB_STATE - 1] == 3);
    k.rgOwnerTid = 0;
    t_rg.n = 1;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 6 && k.orFb[OR_FB_STATE - 1] == 4);
    t_rg.n = 0;
    caches[4][0x20] = 0;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 7 && k.orFb[OR_FB_CACHE - 1] == 1);
    caches[4][0x20] = 1;
    g_twActive = 1;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 8 && k.orFb[OR_FB_TRIP - 1] == 1);
    g_twActive = 0;
    // QL held exclusive by a worker writer: the try fails, the game's call
    HANDLE held = CreateEventW(NULL, TRUE, FALSE, NULL), go = CreateEventW(NULL, TRUE, FALSE, NULL);
    struct Hold { HANDLE held, go; } h = {held, go};
    HANDLE th = CreateThread(NULL, 0, [](void* a) -> DWORD {
        Hold* hh = (Hold*)a;
        AcquireSRWLockExclusive(&g_ql);
        SetEvent(hh->held);
        WaitForSingleObject(hh->go, 5000);
        ReleaseSRWLockExclusive(&g_ql);
        return 0;
    }, &h, 0, NULL);
    WaitForSingleObject(held, 5000);
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 9 && k.orFb[OR_FB_TRY - 1] == 1);
    SetEvent(go);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    CloseHandle(held);
    CloseHandle(go);
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 9 && k.orTaken == 3);
    // no job: the game's call (Mode 1 alone), then shadow: the game's call and the clone compared, job starts held meanwhile
    TwArm(false);
    g_async.state = A_IDLE;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 10 && k.orShadowRuns == 0);
    k.orShadow = 1;
    assert(OrHook(0x25000001, at) == 0x703 && g_orGameCalls == 11 && k.orShadowRuns == 1 && k.orShadowMismatch == 0);
    g_orGameAnswer = 0x702;
    assert(OrHook(0x25000001, at) == 0x702 && g_orGameCalls == 12 && k.orShadowRuns == 2 && k.orShadowMismatch == 1);
    caches[4][0x20] = 0;  // a shadow clone that gives up is counted, not compared
    assert(OrHook(0x25000001, at) == 0x702 && k.orShadowRuns == 3 && k.orShadowMismatch == 1 && k.orFb[OR_FB_CACHE - 1] == 2);
    caches[4][0x20] = 1;
    assert(g_rgOffSimOpen == 0 && g_orRefIn == g_orRefOut);
    printf("obstructed RO: clone, %ld read-only answers, fallbacks %ld/%ld/%ld/%ld/%ld/%ld, shadow %ld runs\n", k.orTaken, k.orFb[0],
           k.orFb[1], k.orFb[2], k.orFb[3], k.orFb[4], k.orFb[5], k.orShadowRuns);

    g_orOrig = savedOrig;
    g_srWorldGet = savedWorld;
    g_srLotLookup = savedLook;
    g_srLotFind = savedFind;
    g_srWmSlot = savedWm;
    g_srOne = savedOne;
    g_srCheckSlots = true;
    g_orQuery = OrQueryGame;
    g_workerTid = savedWorker;
    g_async.owner = 0;
    g_async.handle = 0;
    memset(&k.orReady, 0, 16 * 4);
    simTid = 0;
}

// --- [ProbeSnap] indirect call target sampler (vcall.cpp): decode, target, live int3 round trip on our own code ---
static volatile LONG g_vcT;
static void __cdecl VcT1() { g_vcT += 1; }
static void __cdecl VcT2() { g_vcT += 10; }
static void VcExec(DWORD code, DWORD eaxv, DWORD ecxv, DWORD arg) {
    __asm {
        mov eax, eaxv
        mov ecx, ecxv
        push arg
        call code
        add esp, 4
    }
}
static void TestProbeSnap() {
    static_assert(offsetof(Ts3nCounters, vcReady) == 0x15C4 && offsetof(Ts3nCounters, vcHits) == 0x15D0 &&
                  offsetof(Ts3nCounters, vcFaults) == 0x15E0 && offsetof(Ts3nCounters, vcArmed) == 0x15E8 &&
                  sizeof(Ts3nCounters) >= 1403 * 4, "v43 layout");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.vcReady, 0, 10 * sizeof(LONG));
    static DWORD tbl[3], vt[2];
    tbl[0] = tbl[2] = vt[1] = (DWORD)(uintptr_t)VcT1;
    tbl[1] = (DWORD)(uintptr_t)VcT2;
    VcSite d;
    const u8 a1[] = {0xFF, 0x50, 0x08}, a2[] = {0xFF, 0x24, 0x85, 0x10, 0, 0, 0}, a3[] = {0xFF, 0x54, 0x24, 0x04},
             a4[] = {0xFF, 0x15, 0, 0, 0, 0}, a5[] = {0xFF, 0x30}, a6[] = {0xFF, 0x94, 0x8E, 0x00, 0x01, 0x00, 0x00};
    assert(VcDecode(a1, 3, d) && d.call && d.mod == 1 && d.base == 0 && d.index == 8 && d.disp == 8);
    assert(!VcDecode(a1, 2, d));  // length differs from the table
    assert(VcDecode(a2, 7, d) && !d.call && d.base == 8 && d.index == 0 && d.scale == 2 && d.disp == 0x10);
    assert(VcDecode(a3, 4, d) && d.call && d.base == 4 && d.index == 8 && d.disp == 4);
    assert(!VcDecode(a4, 6, d) && !VcDecode(a5, 2, d));  // call [abs], push [eax]
    assert(VcDecode(a6, 7, d) && d.base == 6 && d.index == 1 && d.scale == 2 && d.disp == 0x100);  // call [esi+ecx*4+0x100]
    CONTEXT c = {};
    c.Esi = (DWORD)(uintptr_t)tbl - 0x100 - 8;
    c.Ecx = 3;
    DWORD t = 0;
    assert(VcTarget(d, c, &t) && t == tbl[1]);
    VcDecode(a1, 3, d);
    c.Eax = 0x10;
    assert(!VcTarget(d, c, &t));  // unreadable operand

    // live: call [eax+4], call ecx, jmp [eax*4+tbl], call [esp+4], and a row whose bytes differ from memory
    u8* code = (u8*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(code);
    memset(code, 0xC3, 64);
    const DWORD b = (DWORD)(uintptr_t)code, ta = (DWORD)(uintptr_t)tbl;
    VcSiteDef defs[5] = {{b, 3, {0xFF, 0x50, 0x04}}, {b + 8, 2, {0xFF, 0xD1}}, {b + 16, 7, {0xFF, 0x24, 0x85}},
                         {b + 24, 4, {0xFF, 0x54, 0x24, 0x04}}, {b + 32, 2, {0xFF, 0xD0}}};
    memcpy(defs[2].bytes + 3, &ta, 4);
    for (int i = 0; i < 4; i++) memcpy(code + defs[i].addr - b, defs[i].bytes, defs[i].len);
    code[32] = code[33] = 0x90;
    assert(VcSetup(defs, 5) == 5 && k.vcSites == 5);
    g_vcPer = 3;
    PVOID h = AddVectoredExceptionHandler(1, VcVeh);
    assert(h);
    VcArmAll();
    assert(code[0] == 0xCC && code[8] == 0xCC && code[16] == 0xCC && code[24] == 0xCC && code[32] == 0x90);
    assert(k.vcSkipped == 1 && k.vcArmed == 4 && k.vcWindows == 1);
    g_vcT = 0;
    VcExec(b, (DWORD)(uintptr_t)vt, 0, 0);
    assert(g_vcT == 1);
    VcExec(b + 8, 0, (DWORD)(uintptr_t)VcT2, 0);
    assert(g_vcT == 11);
    VcExec(b + 16, 1, 0, 0);
    VcExec(b + 16, 0, 0, 0);
    assert(g_vcT == 22);
    VcExec(b + 24, 0, 0, (DWORD)(uintptr_t)VcT2);
    assert(g_vcT == 32 && k.vcHits == 5 && k.vcPairs == 5 && k.vcOutside == 0 && g_vc[2].p[0].tgt == (LONG)tbl[1]);
    VcExec(b, (DWORD)(uintptr_t)vt, 0, 0);
    VcExec(b, (DWORD)(uintptr_t)vt, 0, 0);  // third hit: the site disarms itself
    assert(code[0] == 0xFF && k.vcArmed == 3 && g_vc[0].p[0].n == 3);
    VcExec(b, (DWORD)(uintptr_t)vt, 0, 0);  // runs natively now
    assert(g_vcT == 35 && k.vcHits == 7);
    // targets in another module, and more than 8 targets on one site
    g_vcPer = 1000;
    const HMODULE k32 = GetModuleHandleA("kernel32.dll");
    const char* const fn[8] = {"GetTickCount", "GetCurrentThreadId", "GetCurrentProcessId", "GetLastError",
                               "GetCurrentThread", "GetCurrentProcess", "GetProcessHeap", "GetOEMCP"};
    for (int i = 0; i < 8; i++) VcExec(b + 8, 0, (DWORD)(uintptr_t)GetProcAddress(k32, fn[i]), 0);
    assert(k.vcOutside == 7 && k.vcOverflow == 1 && g_vc[1].over == 1 && k.vcPairs == 12);
    assert(VcLogPairs() == 13 && VcLogPairs() == 0);  // 1 + (8 + overflow) + 2 + 1 changed pairs, then none
    // next window: hit budget reset, a site whose bytes another hook changed is dropped for good
    code[1] = 0x51;
    VcArmAll();
    assert(g_vc[0].dead && code[0] == 0xFF && k.vcSkipped == 2 && k.vcWindows == 2 && k.vcArmed == 3);
    assert(RemoveVectoredExceptionHandler(h));
    AcquireSRWLockExclusive(&g_vcPoke);
    for (int i = 0; i < g_vcN; i++) VcPokeLocked(g_vc[i], false);
    ReleaseSRWLockExclusive(&g_vcPoke);
    assert(code[8] == 0xFF && code[16] == 0xFF && code[24] == 0xFF && k.vcArmed == 0);
    VirtualFree(code, 0, MEM_RELEASE);
    free(g_vc);
    g_vc = NULL;
    g_vcN = 0;
    g_vcPer = 16;
    memset(&k.vcReady, 0, 10 * sizeof(LONG));
}

// --- [ProbeValues] (values.cpp, docs/108): layout, R checksum (same constants as tools/rtdata-decode.ts), frames, live stubs ---
static int __stdcall PvFakeQuery(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD) { return 2; }
typedef int(__stdcall* PvLotFn)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);

static void TestProbeValues() {
    static_assert(offsetof(Ts3nCounters, pvReady) == 0x15EC && offsetof(Ts3nCounters, pvTune) == 0x15FC &&
                  offsetof(Ts3nCounters, pvG) == 0x1650 && offsetof(Ts3nCounters, pvR) == 0x1664 &&
                  offsetof(Ts3nCounters, pvGBuild) == 0x1684 && offsetof(Ts3nCounters, pvAStarBit) == 0x16D0 &&
                  offsetof(Ts3nCounters, pvLotPlanBit) == 0x176C && offsetof(Ts3nCounters, pvStatus) == 0x17FC &&
                  offsetof(Ts3nCounters, pvPortal) == 0x18A8 && offsetof(Ts3nCounters, pvQuery) == 0x190C &&
                  offsetof(Ts3nCounters, pvLazy) == 0x195C && offsetof(Ts3nCounters, pvFrameFull) == 0x19D8 &&
                  sizeof(Ts3nCounters) == 1655 * 4, "v44 layout");
    static_assert(PV_N == 42 && sizeof(kPvStubs) / sizeof(kPvStubs[0]) == PV_N, "one stub per site");
    Ts3nCounters& k = ts3native_counters;
    memset(&k.pvReady, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, pvReady));

    // R checksum: the 2x1-chunk graph of rtdata-decode's self-check in memory form (version 2 records)
    static const u8 raw[31] = {0x02, 0x00, 0x06, 0x00, 0x0E, 0x00, 0x01, 0x41, 0x28, 0x08, 0x10, 0x00, 0x70, 0x04, 0x00, 0x8E,
                               0x28, 0x01, 0x02, 0x01, 0x00, 0x04, 0x00, 0x01, 0x42, 0x28, 0xC8, 0xFF, 0x00, 0x68, 0x03};
    static DWORD tbl[2] = {0, 19};
    DWORD R[5] = {2, 1, (DWORD)(uintptr_t)tbl, (DWORD)(uintptr_t)raw, sizeof(raw)}, out[4] = {};
    assert(PvRSum((const u8*)R, out) && out[0] == 0xE6F9D31A && out[1] == 0x1750A4D1 && out[2] == 3 && out[3] == 1);
    R[4] = 30;  // one byte short: the last record runs past the area
    assert(!PvRSum((const u8*)R, out));
    R[3] = 0;
    assert(!PvRSum((const u8*)R, out));

    // frames on a fake stack: plan scope, H9 per plan, family, dead frames, portal path and state
    DWORD stk[64] = {};
    DWORD mType = 16, opts[20] = {};
    opts[0] = 0x31;  // bits 1, 0x10, 0x20
    opts[5] = 7;     // +0x14
    stk[40] = 0x1234;
    PvEnter(PV_JOB, &stk[40]);
    assert(t_pv.n == 1 && stk[40] == (DWORD)(uintptr_t)PvLeaveThunk && k.pvJobs == 1);
    PvEnter(PV_H9REM, &stk[30]);
    PvEnter(PV_H9INS, &stk[30]);
    stk[22] = (DWORD)(uintptr_t)&mType;
    stk[20] = 0x5678;
    PvEnter(PV_PC_POINT, &stk[20]);
    stk[16] = (DWORD)(uintptr_t)opts;
    PvEnter(PV_ASTAR, &stk[10]);
    assert(k.pvH9[0] == 1 && k.pvH9[1] == 1 && k.pvH9InPlan == 2 && k.pvAStar[2] == 1 && k.pvAStarBit[2][0] == 1 &&
           k.pvAStarBit[2][4] == 1 && k.pvAStarBit[2][5] == 1 && k.pvAStarBit[2][1] == 0 && k.pvAStarRef14[2] == 1 && k.pvAStarRef44[2] == 0);
    assert(PvLeave(&stk[20]) == 0x5678 && k.pvStatus[1][16] == 1 && t_pv.n == 1);
    stk[19] = 0;  // eax
    assert(PvLeave(&stk[40]) == 0x1234 && k.pvH9Plans == 1 && k.pvH9Max == 2 && t_pv.n == 0);
    PvEnter(PV_JOB, &stk[40]);
    PvEnter(PV_PC_PLAN, &stk[30]);
    PvEnter(PV_TIE, &stk[50]);  // above both: they were unwound
    assert(t_pv.n == 0 && k.pvTies == 1 && k.pvH9Plans == 1);
    PvEnter(PV_PATH_LOT, &stk[40]);
    stk[23] = 0x00641234;  // some other caller of the wrapper
    PvEnter(PV_LS_LOCKED, &stk[20]);
    stk[19] = 1;
    PvLeave(&stk[20]);
    stk[23] = PV_INROOM_RET;
    PvEnter(PV_LS_DISC, &stk[20]);
    stk[19] = 3;
    PvLeave(&stk[20]);
    stk[29] = 1;  // initialLocked (arg 9 of the query at slot 20)
    PvEnter(PV_QUERY, &stk[20]);
    stk[19] = 7;
    PvLeave(&stk[20]);
    assert(k.pvPortal[2][1] == 1 && k.pvPortal[4][3] == 1 && k.pvQuery[1][4] == 1 && t_pv.n == 1);
    PvLeave(&stk[40]);
    assert(t_pv.n == 0);

    // live: a call site (frame, args, return value) and an entry detour, on dummies of the same shape
    u8* code = (u8*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(code);
    u8* c = code;
    *c++ = 0x6A; *c++ = 0x01;                              // push 1 (arg 9, initialLocked)
    for (int i = 0; i < 8; i++) { *c++ = 0x6A; *c++ = 0x00; }  // push 0 (args 8..1)
    const DWORD site = (DWORD)(uintptr_t)c, fake = (DWORD)(uintptr_t)PvFakeQuery;
    *c++ = 0xE8;
    const LONG rel = (LONG)(fake - (site + 5));
    memcpy(c, &rel, 4);
    c += 4;
    *c++ = 0xC3;
    assert(PvInstallAt(PV_QUERY, site, fake + 1) == 0);  // E8 to another target: skipped
    assert(PvInstallAt(PV_QUERY, site, fake) == 1 && g_pvJmp[PV_QUERY] == (void*)PvFakeQuery);
    assert(((int(__cdecl*)())code)() == 2 && k.pvQuery[1][2] == 1 && t_pv.n == 0);
    u8* lp = code + 256;
    static const u8 kLp[] = {0x81, 0xEC, 0xC0, 0x01, 0x00, 0x00, 0x81, 0xC4, 0xC0, 0x01, 0x00, 0x00, 0xC2, 0x34, 0x00};
    memcpy(lp, kLp, sizeof(kLp));
    assert(PvInstallAt(PV_LOTPLAN, (DWORD)(uintptr_t)lp, 0) == 1);
    stk[42] = (DWORD)(uintptr_t)&mType;
    stk[40] = 0x1111;
    PvEnter(PV_PC_PLAN, &stk[40]);  // the plan family frame (its slot is above this function's callees)
    ((PvLotFn)lp)(1, 2, 3, 4, 5, 6, 7, 8, 0x200011, 10, 11, 1, 13);
    assert(k.pvLotPlan[1] == 1 && k.pvLotPlanBit[1][0] == 1 && k.pvLotPlanBit[1][4] == 1 && k.pvLotPlanBit[1][9] == 1 &&
           k.pvLotPlanBit[1][1] == 0 && k.pvLotPlanObst[1] == 1 && t_pv.n == 1);
    assert(PvLeave(&stk[40]) == 0x1111 && k.pvStatus[0][16] == 1);
    VirtualFree(code, 0, MEM_RELEASE);

    // an entry owned by a route gate stub is counted from inside it (0x63D2F0 is not mapped here: the bytes differ)
    void* saved = g_rgTramp[2];
    g_rgTramp[2] = (void*)1;
    assert(PvInstallAt(PV_LAZY2, kRgSites[2].addr, 0) == 2 && g_pvRg[2] == PV_LAZY2 + 1);
    g_rgTramp[2] = saved;
    g_pvRg[2] = 0;
    assert(PvInstallAt(PV_LAZY1, 0x10, 0) == 0);  // unreadable, no owner: skipped
    memset(&k.pvReady, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, pvReady));
}

int main() {
    static DWORD noSimTid;  // the game global 0x11D8A8C does not exist here
    g_rtSimTidSlot = &noSimTid;
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
    TestTerrainGrave();
    TestIcLock();
    TestRouteGate();
    TestMonoThread();
    TestLotStream();
    TestVerify();
    TestRouteSites();
    TestRouteEnforce();
    TestG11CloseBy();
    TestDiscourage();
    TestDiscourageDX();
    TestSteerRO();
    TestSteerTrip();
    TestLotFd();
    TestObstructedRO();
    TestSlotRo();
    TestP10();
    TestProbeSnap();
    TestProbeValues();
    TestJobQueue();
    TestLotSinCos();
    TestIdCache();
    puts("selftest ok");
    return 0;
}
