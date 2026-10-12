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
static u8 __fastcall WorkerPlan(void*, void*, int* outList, int* starts, int* goals, DWORD agent, DWORD, u8* opts, int*, int*,
                                DWORD guidLo, DWORD) {
    g_planTid = GetCurrentThreadId();
    g_planCw = GetX87();
    if (g_reenter) {  // a plan started on the worker runs there directly instead of waiting for itself
        g_reenter = 0;
        assert(PlanLockedHook(NULL, NULL, outList, starts, goals, agent, 1, opts, NULL, NULL, 1, 0) == 1);
        assert(g_planTid == GetCurrentThreadId());
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
    const unsigned short cw = GetX87();
    SetX87((unsigned short)(cw & ~0x300));  // 24-bit precision, as D3D9 can leave the game thread
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 0, 0) == 0);
    assert(g_planTid == g_workerTid && g_planTid != me && g_planCw == GetX87());
    SetX87(cw);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1 && g_planCw == cw);
    g_reenter = 1;
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 1, opts, NULL, NULL, 1, 0) == 1);
    assert(PlanLockedHook(NULL, NULL, list, s, g, 0x100, 0, opts, NULL, NULL, 1, 0) == 1 && g_planTid == me);  // doPlan=0 stays
    const Ts3nCounters& c = ts3native_counters;
    assert(c.workerCalls - c0.workerCalls == 3 && c.fallbacks - c0.fallbacks == 1 && c.watchdogs == 0);
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
    puts("selftest ok");
    return 0;
}
