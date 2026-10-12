// ts3native.asi: native patches for TS3W.exe (Steam 1.67, SHA-256 10b2423e..., base 0x400000, no ASLR).
// Loaded by Ultimate ASI Loader. Every patch site is checked against the exact original bytes first;
// a mismatch skips that patch and logs it. See docs/15-native-patches.md.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <xmmintrin.h>

#define TS3NATIVE_VERSION "0.4.1"

typedef unsigned char u8;

static HANDLE g_log = INVALID_HANDLE_VALUE;

static void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf_s(buf, sizeof(buf) - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)strlen(buf);
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD w;
    if (g_log != INVALID_HANDLE_VALUE) WriteFile(g_log, buf, (DWORD)n, &w, NULL);
}

static void Hex(const u8* p, size_t n, char* out, size_t cap) {
    out[0] = 0;
    for (size_t i = 0; i < n && (i * 3 + 3) < cap; i++) sprintf_s(out + i * 3, cap - i * 3, "%02X ", p[i]);
}

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (const u8*)p + n <= (const u8*)mbi.BaseAddress + mbi.RegionSize;
}

static bool Matches(const void* addr, const u8* expect, size_t n) {
    return Readable(addr, n) && memcmp(addr, expect, n) == 0;
}

static bool Write(void* addr, const void* bytes, size_t n) {
    DWORD old;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(addr, bytes, n);
    VirtualProtect(addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
    return true;
}

struct Site {
    DWORD addr;      // VA of the instruction
    u8 expect[8];    // original instruction bytes
    size_t len;
    size_t immOff;   // offset of the 32-bit immediate inside the instruction
};

// Verify every site first, then write the new immediate into all of them (all or nothing).
static bool PatchImm32(const char* name, const Site* sites, int count, DWORD value) {
    char hex[64];
    for (int i = 0; i < count; i++) {
        const u8* p = (const u8*)(uintptr_t)sites[i].addr;
        if (!Matches(p, sites[i].expect, sites[i].len)) {
            if (Readable(p, sites[i].len)) Hex(p, sites[i].len, hex, sizeof(hex)); else strcpy_s(hex, "<unreadable>");
            Log("SKIP %s: bytes at 0x%08X differ (found %s), nothing written", name, sites[i].addr, hex);
            return false;
        }
    }
    for (int i = 0; i < count; i++) {
        u8* imm = (u8*)(uintptr_t)sites[i].addr + sites[i].immOff;
        DWORD oldv;
        memcpy(&oldv, imm, 4);
        if (!Write(imm, &value, 4)) {
            Log("FAIL %s: VirtualProtect at 0x%08X failed (err %lu)", name, sites[i].addr, GetLastError());
            return false;  // shortcut: earlier sites of this group stay patched; they are independent immediates
        }
        Log("OK   %s: 0x%08X imm 0x%X -> 0x%X", name, sites[i].addr, oldv, value);
    }
    return true;
}

// Phase 2 helper (unused so far): 5-byte jmp detour at `target`. `prologue` is the verified original
// code, `len` >= 5 and ending on an instruction boundary, with no relative operands (it is copied
// verbatim into the trampoline). Returns the trampoline that runs the original function, or NULL.
void* InstallDetour(const char* name, DWORD target, const u8* prologue, size_t len, void* hook) {
    u8* t = (u8*)(uintptr_t)target;
    if (len < 5 || len > 16 || !Matches(t, prologue, len)) {
        Log("SKIP detour %s at 0x%08X: prologue differs", name, target);
        return NULL;
    }
    u8* tramp = (u8*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;
    memcpy(tramp, prologue, len);
    tramp[len] = 0xE9;
    *(LONG*)(tramp + len + 1) = (LONG)((t + len) - (tramp + len + 5));
    u8 jmp[16];
    memset(jmp, 0x90, sizeof(jmp));
    jmp[0] = 0xE9;
    *(LONG*)(jmp + 1) = (LONG)((u8*)hook - (t + 5));
    if (!Write(t, jmp, len)) { VirtualFree(tramp, 0, MEM_RELEASE); return NULL; }
    Log("OK   detour %s at 0x%08X", name, target);
    return tramp;
}

// --- Phase 1: Boehm GC (docs/13-gc-tuning.md) ---
static const DWORD GC_IS_INITIALIZED = 0x011F30D0;  // set at the end of GC_init (0xE4E160)
static const DWORD BLOCKS_PER_MB = 256;              // GC heap blocks are 4 KB

static const Site kInitialHeap[] = {
    {0x00E4E168, {0xBD, 0x00, 0x28, 0x00, 0x00}, 5, 1},  // GC_init: mov ebp,0x2800 (40 MB default)
};
static const Site kGrowthStep[] = {
    {0x00E4A1D8, {0x3D, 0x00, 0x08, 0x00, 0x00, 0x76, 0x2C}, 7, 1},  // GC_collect_or_expand: cmp eax,0x800 ; jbe
    {0x00E4A1FF, {0x3D, 0x00, 0x08, 0x00, 0x00, 0x77, 0x05}, 7, 1},  // cmp eax,0x800 ; ja
    {0x00E4A206, {0xB8, 0x00, 0x08, 0x00, 0x00, 0x50}, 6, 1},        // mov eax,0x800 ; push eax
};
static const Site kBudgetCeiling[] = {
    {0x01192324, {0xDC, 0x05, 0x00, 0x00}, 4, 0},  // .data dword 1500, per-frame GC budget ceiling
};

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void ApplyGc(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"GC", L"Enabled", 1, ini)) { Log("GC patches disabled (Enabled=0)"); return; }
    int heapMB = GetPrivateProfileIntW(L"GC", L"InitialHeapMB", 256, ini);
    int stepMB = GetPrivateProfileIntW(L"GC", L"GrowthStepMB", 32, ini);
    int ceiling = GetPrivateProfileIntW(L"GC", L"BudgetCeiling", 0, ini);
    // Limits: an initial heap the 32-bit process cannot reserve in one block makes GC_init exit(1).
    int heapC = Clamp(heapMB, 40, 1024), stepC = Clamp(stepMB, 8, 256);
    if (heapC != heapMB || stepC != stepMB) Log("clamped InitialHeapMB %d->%d, GrowthStepMB %d->%d", heapMB, heapC, stepMB, stepC);
    Log("settings: InitialHeapMB=%d GrowthStepMB=%d BudgetCeiling=%d", heapC, stepC, ceiling);

    char env[32];
    if (GetEnvironmentVariableA("GC_INITIAL_HEAP_SIZE", env, sizeof(env)))
        Log("note: GC_INITIAL_HEAP_SIZE=%s is set and overrides InitialHeapMB", env);

    const DWORD* inited = (const DWORD*)(uintptr_t)GC_IS_INITIALIZED;
    if (!Readable(inited, 4)) { Log("SKIP GC: 0x%08X unreadable, not the expected exe", GC_IS_INITIALIZED); return; }
    if (*inited) Log("SKIP initial heap: GC_init already ran (GC_is_initialized=1), plugin loaded too late");
    else if (heapC != 40) PatchImm32("GC initial heap", kInitialHeap, 1, (DWORD)heapC * BLOCKS_PER_MB);

    if (stepC != 8) PatchImm32("GC growth step", kGrowthStep, 3, (DWORD)stepC * BLOCKS_PER_MB);

    if (ceiling == 0) Log("budget ceiling unchanged (BudgetCeiling=0)");
    else PatchImm32("GC budget ceiling", kBudgetCeiling, 1, (DWORD)Clamp(ceiling, 500, 20000));
}

// --- Phase 2: negative cache for failed route plans (docs/11 Q5, docs/15 phase 2) ---
// Counters for test tools: Frida `Module.findExportByName('ts3native.asi', 'ts3native_counters')`, read 27 int32 (v4).
struct Ts3nCounters {
    LONG version;      // layout version, 4
    LONG enabled;      // 1 when the Route_PlanLocked detour is installed
    LONG calls;        // every Route_PlanLocked call
    LONG hits;         // answered from the cache: plain failure, no lock, no search
    LONG misses;       // keyed calls that ran the original
    LONG stored;       // no-path failures (status 7/8) recorded
    LONG otherFails;   // original returned 0 without a 7/8 path (not cached)
    LONG windowMs;     // active WindowMs, 0 when the cache is off
    // v2: route worker (docs/18 first step)
    volatile LONG workerMode;  // runtime switch, writable by test tools: 0 = plan on the calling thread, 1 = on the worker
    LONG workerReady;  // 1 when the worker thread runs and the detour is installed
    LONG workerCalls;  // plans run on the worker
    LONG workerUs;     // total time in the original on the worker, microseconds
    LONG workerMaxUs;  // longest single plan on the worker, microseconds
    LONG waitUs;       // total time callers waited for the worker (search + handoff), microseconds
    LONG fallbacks;    // mode 1 but run on the calling thread (a plan started from the worker itself)
    LONG watchdogs;    // waits that passed 2 s (logged; the caller keeps waiting)
    // v3: route search budget (docs/15 phase 4)
    volatile LONG budgetMode;  // runtime switch, writable by test tools: 0 = no cap, 1 = cap each Route_PlanLocked request
    LONG budgetReady;  // 1 when the cost-function stubs are patched in and the detour is installed
    LONG budgetCalls;  // requests run under a budget
    LONG budgetCapped; // requests that hit the cap (ended through the A*'s own "open list empty" exit)
    LONG budgetCapNodes;    // sum of nodes generated when the cap tripped
    LONG budgetCapUs;       // sum of request time of capped requests (search + drain + retries), microseconds
    LONG budgetMaxNodes;    // most nodes generated by one request
    LONG budgetMaxNodesOk;  // most nodes generated by a request that found a path (a cap above it fails none of them)
    LONG budgetMaxUsOk;     // longest request that found a path, microseconds
    // v4: managed calls the planner makes on the worker, run on the waiting caller thread instead (docs/18 crash 2026-10-13)
    LONG invokeHook;   // 1 = mono_runtime_invoke slot hooked, -1 = slot not as expected (worker mode refused), 0 = not yet
    LONG invokes;      // managed invokes from the worker that ran on the caller thread
};
extern "C" __declspec(dllexport) Ts3nCounters ts3native_counters = {4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

typedef unsigned __int64 u64;
struct CacheEntry { u64 key; ULONGLONG until; };
static CacheEntry* g_tab;
static DWORD g_mask;  // table size - 1 (power of two)
static ULONGLONG g_windowMs = 2000;
static SRWLOCK g_tabLock = SRWLOCK_INIT;
static const DWORD kProbe = 8;  // slots looked at per key; the oldest of them is evicted when all are live

static u64 Fnv(u64 h, DWORD v) {
    for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 0xFF; h *= 0x100000001B3ull; }
    return h;
}

// Route points are 64-byte records, position x,y,z floats at +0 (x/z bounds-checked in Route_PlanCommon 0x7606E0).
static const size_t kPointSize = 64;
static u64 HashPoints(u64 h, const int* vec) {
    const u8* b = (const u8*)(uintptr_t)vec[0];
    const u8* e = (const u8*)(uintptr_t)vec[1];
    h = Fnv(h, (DWORD)((e - b) / kPointSize));
    for (const u8* p = b; p + kPointSize <= e; p += kPointSize)
        for (int i = 0; i < 3; i++) h = Fnv(h, (DWORD)(int)floorf(((const float*)p)[i] * 2.0f));  // 0.5 m cells
    return h;
}

// Request identity: agent class (route+0x3C), all four RoutingOptions words (options+0x28..+0x34), follower
// ObjectGuid (route+0x40/+0x44), start and goal points in 0.5 m cells, in order. Strict on purpose: a miss costs
// one normal plan, a false hit costs a wrong failure.
static u64 RouteKey(const int* starts, const int* goals, DWORD agent, const u8* opts, DWORD guidLo, DWORD guidHi) {
    u64 h = 0xCBF29CE484222325ull;
    h = Fnv(h, agent);
    for (int i = 0; i < 4; i++) h = Fnv(h, ((const DWORD*)(opts + 0x28))[i]);
    h = Fnv(Fnv(h, guidLo), guidHi);
    h = HashPoints(HashPoints(h, starts), goals);
    return h ? h : 1;  // 0 marks an empty slot
}

static bool CacheInit(DWORD maxEntries, DWORD windowMs) {
    DWORD n = 256;
    while (n < maxEntries && n < 65536) n <<= 1;
    g_tab = (CacheEntry*)calloc(n, sizeof(CacheEntry));
    if (!g_tab) return false;
    g_mask = n - 1;
    g_windowMs = windowMs;
    return true;
}

static bool CacheHit(u64 key, ULONGLONG now) {
    bool hit = false;
    AcquireSRWLockShared(&g_tabLock);
    for (DWORD i = 0; i < kProbe; i++) {
        const CacheEntry& e = g_tab[(key + i) & g_mask];
        if (e.key == key) { hit = now < e.until; break; }
    }
    ReleaseSRWLockShared(&g_tabLock);
    return hit;
}

static void CacheStore(u64 key, ULONGLONG now) {
    AcquireSRWLockExclusive(&g_tabLock);
    CacheEntry* pick = NULL;
    for (DWORD i = 0; i < kProbe; i++) {
        CacheEntry* e = &g_tab[(key + i) & g_mask];
        if (e->key == key) { pick = e; break; }
        if (!pick || (pick->until > now && e->until < pick->until)) pick = e;  // first free/expired, else the oldest
    }
    pick->key = key;
    pick->until = now + g_windowMs;
    ReleaseSRWLockExclusive(&g_tabLock);
}

// Route_PlanLocked 0x82C6E0 is __thiscall (ecx = route world), 10 stack args, `ret 28h`, result in AL.
// __fastcall with a dummy edx has the same stack layout and cleanup.
typedef u8(__fastcall* PlanLockedFn)(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent,
                                     DWORD doPlan, u8* opts, int* out1, int* out2, DWORD guidLo, DWORD guidHi);
static PlanLockedFn g_planOrig;
typedef int(__fastcall* PathStatusFn)(void* path, void* edx);  // path vtable +0x20, thiscall, no args
static volatile LONG64 g_nextLog;

static void MaybeLogCounters(ULONGLONG now) {
    LONG64 next = g_nextLog;
    if ((LONG64)now < next || InterlockedCompareExchange64(&g_nextLog, (LONG64)now + 60000, next) != next) return;
    const Ts3nCounters& c = ts3native_counters;
    if (c.windowMs) Log("route cache: calls %ld hits %ld misses %ld stored %ld otherFails %ld", c.calls, c.hits, c.misses, c.stored, c.otherFails);
    if (c.workerReady)
        Log("route worker: mode %ld calls %ld worker %ld ms max %ld us wait %ld ms fallbacks %ld watchdogs %ld invokeHook %ld invokes %ld",
            c.workerMode, c.workerCalls, c.workerUs / 1000, c.workerMaxUs, c.waitUs / 1000, c.fallbacks, c.watchdogs, c.invokeHook, c.invokes);
    if (c.budgetReady)
        Log("route budget: mode %ld calls %ld capped %ld capNodes %ld capMs %ld maxNodes %ld maxNodesOk %ld maxUsOk %ld", c.budgetMode,
            c.budgetCalls, c.budgetCapped, c.budgetCapNodes, c.budgetCapUs / 1000, c.budgetMaxNodes, c.budgetMaxNodesOk, c.budgetMaxUsOk);
}

static LARGE_INTEGER g_qpf;

static LONG UsSince(LARGE_INTEGER t0) {
    LARGE_INTEGER t1;
    QueryPerformanceCounter(&t1);
    return (LONG)((t1.QuadPart - t0.QuadPart) * 1000000 / g_qpf.QuadPart);
}

static void Max(LONG* m, LONG v) {
    for (LONG o; v > (o = *m) && InterlockedCompareExchange(m, v, o) != o;) {}
}

// --- Route search budget (docs/11 Q3, docs/15 phase 4) ---
// World A* 0x676260 calls its cost function (cdecl, 8 args, float in ST0) through a pointer it stores at [esp+0x60]
// from one of five `mov [esp+0x60], imm32`. We swap the immediates for stubs `push orig; jmp CostGate`. Arg 5 is 1
// for an edge cost (checked against the 1e8 wall at 0x676683/0x6768A6/0x676B24) and 0 for the heuristic, which runs
// once per generated node right before it is allocated and pushed. Over budget, every edge cost returns the wall:
// nothing new is pushed, the open list drains (goal check on pop still runs), and the search leaves through its own
// "open list empty" exit 0x676BF7 (returns 0, no leak), i.e. an ordinary no-path failure.
struct Budget { DWORD tid; LONG nodes; LARGE_INTEGER t0; LONG capped; };
static Budget g_budget;  // one search at a time: the planner holds the routing lock world+0x918; tid filters other threads
static LONG g_maxNodes, g_maxUs, g_onlyFlagged;
static const float kWall = 1e9f;  // >= the planner's 1e8 blocked-edge constant at 0x00FEF678

static int __cdecl BudgetStep(DWORD costArg5) {
    Budget& b = g_budget;
    if (!b.tid || b.tid != GetCurrentThreadId()) return 0;
    if (b.capped) return costArg5 == 1;
    if (costArg5 != 0) return 0;
    ++b.nodes;
    if ((g_maxNodes && b.nodes > g_maxNodes) || (g_maxUs && (b.nodes & 15) == 0 && UsSince(b.t0) > g_maxUs)) {
        b.capped = 1;
        InterlockedIncrement(&ts3native_counters.budgetCapped);
        InterlockedExchangeAdd(&ts3native_counters.budgetCapNodes, b.nodes);
    }
    return 0;
}

// [esp] = original cost function (pushed by the stub), [esp+4] = return address into the A*, [esp+8] = cost args 1..8
__declspec(naked) static void CostGate() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 0x20]  // cost arg 5: 0x18 at entry, +8 for the two pushes
        call BudgetStep
        add esp, 4
        pop edx
        pop ecx
        test eax, eax
        jnz wall
        ret                          // pops the original's address: a tail jump with the A*'s return address on top
    wall:
        add esp, 4
        fld dword ptr [kWall]
        ret
    }
}

static const DWORD kCostFns[] = {0x00674E40, 0x00673850, 0x00674250, 0x00675800, 0x006734E0};  // travel modes 1-4, default
static const DWORD kCostSites[] = {0x00676340, 0x0067634A, 0x00676354, 0x0067635E, 0x00676368};  // mov [esp+0x60], fn

// Builds the stubs and writes their addresses into the five sites (all verified first). Parameters let the selftest use local memory.
static bool PatchCostSites(const DWORD* sites, const DWORD* fns) {
    for (int i = 0; i < 5; i++) {
        u8 expect[8] = {0xC7, 0x44, 0x24, 0x60};
        memcpy(expect + 4, &fns[i], 4);
        if (!Matches((const void*)(uintptr_t)sites[i], expect, 8)) {
            Log("SKIP route budget: bytes at 0x%08X differ, nothing written", sites[i]);
            return false;
        }
    }
    u8* stubs = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stubs) return false;
    for (int i = 0; i < 5; i++) {
        u8* s = stubs + i * 10;
        s[0] = 0x68;  // push imm32
        memcpy(s + 1, &fns[i], 4);
        s[5] = 0xE9;  // jmp rel32
        *(LONG*)(s + 6) = (LONG)((u8*)CostGate - (s + 10));
    }
    for (int i = 0; i < 5; i++) {
        DWORD stub = (DWORD)(uintptr_t)(stubs + i * 10);
        if (!Write((u8*)(uintptr_t)sites[i] + 4, &stub, 4)) {
            Log("FAIL route budget: VirtualProtect at 0x%08X failed (err %lu)", sites[i], GetLastError());
            return false;  // shortcut: earlier sites stay patched; harmless, the stubs pass through while no budget is active
        }
    }
    Log("OK   route budget: cost-function stubs at 0x%08X..0x%08X", sites[0], sites[4]);
    return true;
}

// Runs the original Route_PlanLocked under a budget on this thread (the caller's or the worker's).
static u8 CallPlan(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                   int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    const DWORD me = GetCurrentThreadId();
    // DebugPathPlan (RouteOption hi word bit 0x1) as the "budget this route" flag for a future C# caller (docs/11 Q3)
    if (ts3native_counters.budgetMode != 1 || !ts3native_counters.budgetReady || g_budget.tid == me ||
        (g_onlyFlagged && !(((const DWORD*)(opts + 0x2C))[0] & 1)))
        return g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    Budget b = {me, 0, {}, 0};
    QueryPerformanceCounter(&b.t0);
    g_budget = b;
    u8 r = g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    LONG us = UsSince(g_budget.t0), nodes = g_budget.nodes;
    if (g_budget.capped) InterlockedExchangeAdd(&ts3native_counters.budgetCapUs, us);
    g_budget.tid = 0;
    InterlockedIncrement(&ts3native_counters.budgetCalls);
    Max(&ts3native_counters.budgetMaxNodes, nodes);
    if (r) { Max(&ts3native_counters.budgetMaxNodesOk, nodes); Max(&ts3native_counters.budgetMaxUsOk, us); }
    return r;
}

// --- Route worker (docs/18 first step, offload-sync): the original runs on our thread while the caller waits ---
// Same arguments, same result; only the thread changes.
struct PlanJob {
    void* world; int* outList; int* starts; int* goals; DWORD agent; DWORD doPlan; u8* opts; int* out1; int* out2;
    DWORD guidLo, guidHi;
    unsigned short x87;
    unsigned int mxcsr;
    u8 result;
};
static PlanJob* volatile g_job;  // the plan the worker runs now; a nested plan saves and restores it
static HANDLE g_jobReady, g_jobDone;
static DWORD g_workerTid;
static volatile DWORD g_callerTid;  // the thread waiting for the worker's current plan, 0 when idle
static CRITICAL_SECTION g_workerCs;  // one caller at a time; another calling thread waits here (recursive for nesting)

// Managed calls (docs/18 "Crash 2026-10-13"): the planner can call script code through mono_runtime_invoke, and the worker
// is no Mono thread (no domain in TLS 0x11959DC, not in Boehm's thread table, which only holds the GC_init thread).
// Each such call goes back to the waiting caller thread and runs there, as it would have without the worker. Only one
// of the two threads runs at a time, so a plan started inside that managed call goes to the worker the same way.
typedef void*(__cdecl* InvokeFn)(void* method, void* obj, void** params, void** exc);
static DWORD* g_invokeSlot = (DWORD*)(uintptr_t)0x01195B60;  // set by mono_install_runtime_invoke 0xEA1470; 0xEA1080 before
static DWORD g_invokeExpect = 0x00E5A600;  // the interpreter's runtime_invoke, installed by mono_interp_init 0xE5A680
static bool g_invokeCheckBytes = true;     // selftest turns it off (its fake invoke has another prologue)
static const u8 kInvokePrologue[] = {0x81, 0xEC, 0x50, 0x01, 0x00, 0x00, 0x68, 0x50, 0x01, 0x00, 0x00};  // sub esp,150h; push 150h
static InvokeFn g_invokeOrig;
struct InvokeJob { void* method; void* obj; void** params; void** exc; void* result; };
static InvokeJob* volatile g_invJob;
static HANDLE g_invReady, g_invDone;

// The caller's FPU state goes with the job: D3D9 can leave the game thread's x87 at single precision, and the
// search must round the same way to find the same path.
static unsigned short GetX87() {
    unsigned short cw;
    __asm { fnstcw cw }
    return cw;
}
static void SetX87(unsigned short cw) {
    __asm { fldcw cw }
}

// Worker side: runs g_job (also nested, from inside InvokeHook) and keeps the worker's own FPU state around it.
static void ServePlan() {
    PlanJob& j = *g_job;
    const unsigned short cw = GetX87();
    const unsigned int csr = _mm_getcsr();
    SetX87(j.x87);
    _mm_setcsr(j.mxcsr);
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    j.result = CallPlan(j.world, NULL, j.outList, j.starts, j.goals, j.agent, j.doPlan, j.opts, j.out1, j.out2, j.guidLo, j.guidHi);
    LONG us = UsSince(t0);
    InterlockedIncrement(&ts3native_counters.workerCalls);
    InterlockedExchangeAdd(&ts3native_counters.workerUs, us);
    Max(&ts3native_counters.workerMaxUs, us);
    SetX87(cw);
    _mm_setcsr(csr);
    SetEvent(g_jobDone);
}

// Caller side: runs the worker's managed call on this (Mono-attached) thread.
static void ServeInvoke() {
    InvokeJob& v = *g_invJob;
    static void* seen[8];  // first distinct MonoMethod pointers, logged once each, to name the callbacks later
    static int nSeen;
    int i = 0;
    while (i < nSeen && seen[i] != v.method) i++;
    if (i == nSeen && nSeen < 8) {
        seen[nSeen++] = v.method;
        Log("route worker: planner made a managed call, MonoMethod %p obj %p exc %p", v.method, v.obj, v.exc);
    }
    v.result = g_invokeOrig(v.method, v.obj, v.params, v.exc);
    InterlockedIncrement(&ts3native_counters.invokes);
    SetEvent(g_invDone);
}

// In the runtime_invoke slot: every thread but the worker goes straight to the original.
static void* __cdecl InvokeHook(void* method, void* obj, void** params, void** exc) {
    if (GetCurrentThreadId() != g_workerTid || !g_callerTid) return g_invokeOrig(method, obj, params, exc);
    InvokeJob v = {method, obj, params, exc, NULL};
    InvokeJob* prev = g_invJob;
    g_invJob = &v;
    SetEvent(g_invReady);
    HANDLE h[2] = {g_invDone, g_jobReady};
    while (WaitForMultipleObjects(2, h, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) ServePlan();  // a plan from inside the call
    g_invJob = prev;
    return v.result;
}

// Hooks the slot once, from the first worker plan (it comes from script, so Mono is up). false: plans stay inline.
// Caller holds g_workerCs.
static bool EnsureInvokeHook() {
    if (ts3native_counters.invokeHook) return ts3native_counters.invokeHook > 0;
    const DWORD cur = Readable(g_invokeSlot, 4) ? *g_invokeSlot : 0;
    const void* code = (const void*)(uintptr_t)cur;
    if (cur != g_invokeExpect ||
        (g_invokeCheckBytes && (!Readable(code, sizeof(kInvokePrologue)) || memcmp(code, kInvokePrologue, sizeof(kInvokePrologue)) != 0))) {
        Log("FAIL route worker: runtime_invoke slot 0x%08X holds 0x%08X, expected 0x%08X with its prologue; plans stay on the calling thread",
            (DWORD)(uintptr_t)g_invokeSlot, cur, g_invokeExpect);
        ts3native_counters.invokeHook = -1;
        return false;
    }
    g_invokeOrig = (InvokeFn)(uintptr_t)cur;
    if ((DWORD)InterlockedCompareExchange((volatile LONG*)g_invokeSlot, (LONG)(uintptr_t)InvokeHook, (LONG)cur) != cur) {
        Log("FAIL route worker: runtime_invoke slot changed while hooking; plans stay on the calling thread");
        ts3native_counters.invokeHook = -1;
        return false;
    }
    Log("OK   route worker: runtime_invoke slot 0x%08X -> InvokeHook (original 0x%08X)", (DWORD)(uintptr_t)g_invokeSlot, cur);
    ts3native_counters.invokeHook = 1;
    return true;
}

static DWORD WINAPI WorkerMain(LPVOID) {
    for (;;) {
        WaitForSingleObject(g_jobReady, INFINITE);
        ServePlan();
    }
}

static bool StartWorker() {
    QueryPerformanceFrequency(&g_qpf);
    InitializeCriticalSection(&g_workerCs);
    g_jobReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_jobDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_invReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_invDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    // 1 MB reserve like a default thread; it starts running only after DllMain returns (loader lock) and then just waits
    HANDLE t = g_jobReady && g_jobDone && g_invReady && g_invDone
                   ? CreateThread(NULL, 1 << 20, WorkerMain, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, &g_workerTid)
                   : NULL;
    if (!t) return false;
    CloseHandle(t);
    return true;
}

static u8 RunPlan(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                  int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    if (ts3native_counters.workerMode != 1 || !ts3native_counters.workerReady)
        return CallPlan(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    const DWORD me = GetCurrentThreadId();
    if (me == g_workerTid) {  // waiting for ourselves would deadlock
        InterlockedIncrement(&ts3native_counters.fallbacks);
        return CallPlan(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    }
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    EnterCriticalSection(&g_workerCs);
    if (!EnsureInvokeHook()) {
        LeaveCriticalSection(&g_workerCs);
        return CallPlan(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    }
    PlanJob j = {world, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi, GetX87(), _mm_getcsr(), 0};
    PlanJob* prevJob = g_job;  // non-NULL: this plan comes from a managed call we run for the worker's plan
    const DWORD prevCaller = g_callerTid;
    g_job = &j;
    g_callerTid = me;
    SetEvent(g_jobReady);
    // Serve the worker's managed calls until the plan is done. Watchdog: log deadlock evidence and keep waiting;
    // there is no safe way to take the search back.
    HANDLE h[2] = {g_jobDone, g_invReady};
    for (DWORD s = 0;;) {
        const DWORD w = WaitForMultipleObjects(2, h, FALSE, 2000);
        if (w == WAIT_OBJECT_0) break;
        if (w == WAIT_OBJECT_0 + 1) {
            ServeInvoke();
            continue;
        }
        s += 2;
        InterlockedIncrement(&ts3native_counters.watchdogs);
        Log("route worker: thread %lu waited %lu s for worker thread %lu (world %p), still waiting", me, s, g_workerTid, world);
    }
    g_callerTid = prevCaller;
    g_job = prevJob;
    LeaveCriticalSection(&g_workerCs);
    InterlockedExchangeAdd(&ts3native_counters.waitUs, UsSince(t0));
    return j.result;
}

static u8 __fastcall PlanLockedHook(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent,
                                    DWORD doPlan, u8* opts, int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    InterlockedIncrement(&ts3native_counters.calls);
    ULONGLONG now = GetTickCount64();
    MaybeLogCounters(now);
    // the original's own early-outs (no plan requested, no start, no goal) stay with the original
    if ((u8)doPlan == 0 || starts[0] == starts[1] || goals[0] == goals[1])
        return g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    if (!g_tab) return RunPlan(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);  // worker only
    u64 key = RouteKey(starts, goals, agent, opts, guidLo, guidHi);
    if (CacheHit(key, now)) {
        InterlockedIncrement(&ts3native_counters.hits);
        return 0;  // same as the original's pre-lock early-outs: out list untouched, Failed with no blocker
    }
    InterlockedIncrement(&ts3native_counters.misses);
    u8 r = RunPlan(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    if (r) return r;
    int* node = (int*)(uintptr_t)outList[0];
    void* path = node != outList ? (void*)(uintptr_t)node[2] : NULL;
    int status = path ? ((PathStatusFn)(*(void***)path)[0x20 / 4])(path, NULL) : 0;
    if (status == 7 || status == 8) {  // no path to destination (0x752620 -> FailedBecauseNoPathToDestination)
        CacheStore(key, GetTickCount64());
        InterlockedIncrement(&ts3native_counters.stored);
    } else {
        InterlockedIncrement(&ts3native_counters.otherFails);
    }
    return r;
}

static const DWORD ROUTE_PLAN_LOCKED = 0x0082C6E0;
static const u8 kPlanLockedPrologue[] = {0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00};  // sub esp,0xD8

// Returns true when the cache table is ready; InstallPlanDetour installs the detour.
static bool ApplyRouteCache(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"RouteCache", L"Enabled", 0, ini)) { Log("route cache disabled (Enabled=0)"); return false; }
    int windowMs = Clamp(GetPrivateProfileIntW(L"RouteCache", L"WindowMs", 2000, ini), 100, 60000);
    int maxEntries = Clamp(GetPrivateProfileIntW(L"RouteCache", L"MaxEntries", 4096, ini), 256, 65536);
    if (!CacheInit((DWORD)maxEntries, (DWORD)windowMs)) { Log("FAIL route cache: table allocation"); return false; }
    Log("route cache settings: WindowMs=%d MaxEntries=%d (table %lu)", windowMs, maxEntries, g_mask + 1);
    ts3native_counters.windowMs = windowMs;
    return true;
}

// Starts the worker thread; Mode is the start value of the runtime switch `workerMode`.
static bool ApplyRouteWorker(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"RouteWorker", L"Enabled", 0, ini)) { Log("route worker disabled (Enabled=0)"); return false; }
    int mode = Clamp(GetPrivateProfileIntW(L"RouteWorker", L"Mode", 0, ini), 0, 1);
    if (!StartWorker()) { Log("FAIL route worker: thread start (err %lu)", GetLastError()); return false; }
    ts3native_counters.workerMode = mode;
    Log("route worker settings: Mode=%d (worker thread %lu)", mode, g_workerTid);
    return true;
}

// Reads [RouteBudget]; Mode is the start value of the runtime switch `budgetMode`. Patches the cost sites only when the
// detour that sets the budget will be installed too (InstallPlanDetour), so true here means "patch them".
static bool ApplyRouteBudget(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"RouteBudget", L"Enabled", 0, ini)) { Log("route budget disabled (Enabled=0)"); return false; }
    int maxNodes = GetPrivateProfileIntW(L"RouteBudget", L"MaxNodes", 0, ini);
    int maxUs = GetPrivateProfileIntW(L"RouteBudget", L"MaxUs", 100000, ini);
    g_maxNodes = maxNodes > 0 ? Clamp(maxNodes, 1000, 10000000) : 0;
    g_maxUs = maxUs > 0 ? Clamp(maxUs, 5000, 2000000) : 0;
    g_onlyFlagged = GetPrivateProfileIntW(L"RouteBudget", L"OnlyFlagged", 0, ini) != 0;
    if (!g_maxNodes && !g_maxUs) { Log("route budget disabled (MaxNodes=0 and MaxUs=0)"); return false; }
    QueryPerformanceFrequency(&g_qpf);
    ts3native_counters.budgetMode = Clamp(GetPrivateProfileIntW(L"RouteBudget", L"Mode", 1, ini), 0, 1);
    Log("route budget settings: MaxNodes=%ld MaxUs=%ld OnlyFlagged=%ld Mode=%ld", g_maxNodes, g_maxUs, g_onlyFlagged, ts3native_counters.budgetMode);
    return true;
}

// One detour serves all three: cache lookup on the calling thread, then the original there or on the worker, under a budget.
// Returns true when installed (the log must then stay open for the periodic counters and the watchdog).
static bool InstallPlanDetour(bool cache, bool worker, bool budget) {
    if (!cache && !worker && !budget) return false;
    g_planOrig = (PlanLockedFn)InstallDetour("Route_PlanLocked", ROUTE_PLAN_LOCKED, kPlanLockedPrologue, sizeof(kPlanLockedPrologue), (void*)PlanLockedHook);
    if (!g_planOrig) { ts3native_counters.windowMs = 0; return false; }
    ts3native_counters.budgetReady = budget && PatchCostSites(kCostSites, kCostFns);
    ts3native_counters.workerReady = worker;
    ts3native_counters.enabled = 1;
    g_nextLog = (LONG64)GetTickCount64() + 60000;
    return true;
}

static void Run(HMODULE self) {
    wchar_t path[MAX_PATH], ini[MAX_PATH], log[MAX_PATH], exe[MAX_PATH];
    bool keepLog = false;
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wchar_t* dot = wcsrchr(path, L'.');
    if (dot) *dot = 0;
    swprintf_s(ini, L"%s.ini", path);
    swprintf_s(log, L"%s.log", path);
    g_log = CreateFileW(log, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    SYSTEMTIME st;
    GetLocalTime(&st);
    Log("ts3native " TS3NATIVE_VERSION " %04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    HMODULE base = GetModuleHandleW(NULL);
    Log("host %ls base 0x%p", exe, base);
    const wchar_t* name = wcsrchr(exe, L'\\');
    if (base != (HMODULE)0x400000 || _wcsicmp(name ? name + 1 : exe, L"TS3W.exe") != 0) {
        Log("SKIP all: host is not TS3W.exe at 0x400000");
    } else {
        ApplyGc(ini);
        bool cache = ApplyRouteCache(ini);
        bool worker = ApplyRouteWorker(ini);
        keepLog = InstallPlanDetour(cache, worker, ApplyRouteBudget(ini));
    }
    Log("done");
    if (!keepLog && g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
}

#ifndef TS3NATIVE_NO_DLLMAIN
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // Patch now: the ASI loader runs before the exe's entry point, so before GC_init (lazy, first GC use).
        Run(inst);
    }
    return TRUE;
}
#endif
