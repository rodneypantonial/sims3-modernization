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
#include <emmintrin.h>

#define TS3NATIVE_VERSION "0.9.0"

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
// Counters for test tools: Frida `Module.findExportByName('ts3native.asi', 'ts3native_counters')`, read 83 int32 (v10).
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
    // v5: async route plan (docs/18 "Async plan"): the sim thread does not wait, the Sim's own task sleeps until the result is ready
    volatile LONG asyncMode;  // runtime switch, writable by test tools: 0 = every START answered "no" (plans stay synchronous), 1 = on
    LONG asyncReady;     // 1 when the three async detours (IC_Route_Plan, Route_GetOption, Lock_Acquire) are installed
    LONG asyncStarted;   // jobs started (C# START answered yes)
    LONG asyncRefused;   // START answered no: another route's job still ran, or the caller may not yield (nested call)
    LONG asyncCollected; // results handed to the C# caller that started them
    LONG asyncDropped;   // results thrown away (caller gave up: reset, destination gone, poll cap; or a plain plan of that route)
    LONG asyncUs;        // total job time on the worker, microseconds
    LONG asyncMaxUs;     // longest job, microseconds
    LONG asyncPumpUs;    // owner-thread time in contended Lock_Acquire waits while a job ran (managed calls served meanwhile), microseconds
    LONG asyncPumpWaits; // such waits
    // v6: save timing and the backup copy on a worker (docs/15 phase 7, savewrite.cpp)
    LONG saveReady;      // 1 when the six save call sites are hooked
    volatile LONG saveMode;  // runtime switch, writable by test tools: 0 = time only (game backs up), 1 = backup copy on the worker
    LONG saveJobs;       // worker backup copies started
    LONG savePromoted;   // worker copies used as the backup (the game's copy skipped)
    LONG saveFallbacks;  // worker copies dropped (failed, old save changed, rename failed); the game's own backup ran
    LONG saveCount;      // saves timed
    LONG saveLastUs;     // last save: whole native save (IC SaveGame -> FUN_00AB1030), microseconds
    LONG saveLastWriteUs;   // last save: package write incl. script serialization (FUN_00AAC110)
    LONG saveLastBackupUs;  // last save: sim-thread time in the backup step (game copy, or wait + rename)
    LONG saveLastCopyUs;    // last save: working copy -> save folder copy + flush (FUN_00AB0E80)
    LONG saveLastFinalUs;   // last save: 1a5tf1l3.dat + folder flush (FUN_00AAF5F0)
    // v7: faster RefPack decoder (docs/15 phase 6, decompress.cpp); 64-bit sums and per-thread detail stay in g_dstat (log)
    LONG decompressReady;   // 1 when the call at 0x4EC065 is retargeted
    volatile LONG decompressMode;    // runtime switch, writable by test tools: 0 = the code at 0x4EB3B0 (S3SS or game), 1 = RefpackFast
    volatile LONG decompressVerify;  // 1 = mode 1 also runs 0x4EB3B0 into a scratch buffer and compares (slow; first in-game runs)
    LONG decompressCalls;      // decodes
    LONG decompressFails;      // decodes that returned 0
    LONG decompressMismatches; // verify: result or bytes differed (the 0x4EB3B0 output is used then)
    LONG decompressMaxUs;      // longest single decode, microseconds
    LONG decompressInMB;       // compressed MB in
    LONG decompressOutMB;      // decompressed MB out
    LONG decompressMs;         // time inside the decoder (excludes verify), milliseconds
    LONG decompressMainCalls;  // decodes on the thread that loaded us (the main thread)
    LONG decompressMainMs;     // their time, milliseconds
    // v8 (0.7.1): host delegate calls proxied whole; route plans and failures per mode, for A/Bs
    LONG hostCalls;      // host delegate calls 0xD75FA0 from the worker that ran on the caller/owner thread
    LONG planOffCalls;   // Route_PlanLocked requests (past the original's early-outs) with worker and async mode off
    LONG planOffFails;   // ... of those, returned 0 (cache hits included)
    LONG planOnCalls;    // requests with worker or async mode on (also plain plans made while async is on)
    LONG planOnFails;
    // v9: per-thread cache in front of EA's general allocator (docs/15 phase 8, alloc.cpp); counts in units of 1024
    LONG allocReady;           // 1 when the Malloc/Memalign/Free/Shutdown detours (0x4E69D0, 0x4E6A20, 0x4E5920, 0x4E5CB0) are installed
    volatile LONG allocMode;   // runtime switch, writable by test tools: 0 = every call goes to the game, 1 = cache
    LONG allocHitsK;           // Malloc answered from the cache (no lock, no 0x4E5E40)
    LONG allocMissesK;         // cacheable Malloc that went to the heap
    LONG allocCachedK;         // Free kept in the cache
    LONG allocPassedK;         // cacheable Free passed to the heap (bin or thread full, 5th heap)
    LONG allocThreads;         // threads holding a ThreadKB quota
    LONG allocFlushed;         // chunks given back to their heap (thread exit, out of memory, after another heap's shutdown)
    LONG allocDropped;         // chunks forgotten because their heap shut down
    LONG allocShutdowns;       // EA heap shutdowns seen
    LONG allocOomRetries;      // Malloc that returned NULL, flushed this thread's chunks of that heap and retried
    // v10: water-grid jump-point search as one inlined loop (docs/15 phase 9, waterjps.cpp)
    LONG waterJpsReady;           // 1 when the call at 0x6207FB is retargeted
    volatile LONG waterJpsMode;   // runtime switch, writable by test tools: 0 = the game's jump 0x61FE10, 1 = WaterJump
    volatile LONG waterJpsVerify; // 1 = mode 1 also runs the game's jump and compares (its result is used on a mismatch)
    LONG waterJpsCallsK;          // jumps run by WaterJump, units of 1024
    LONG waterJpsCellsK;          // cells they stepped over, units of 1024
    LONG waterJpsMismatches;      // verify: WaterJump and the game's jump differed
    LONG waterJpsFallbacks;       // mode 1 calls passed to the game's jump (dir > 7, oversized grid)
};
extern "C" __declspec(dllexport) Ts3nCounters ts3native_counters = {10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

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
    if (c.asyncReady)
        Log("route async: mode %ld started %ld refused %ld collected %ld dropped %ld job %ld ms max %ld us pump %ld ms (%ld waits)", c.asyncMode,
            c.asyncStarted, c.asyncRefused, c.asyncCollected, c.asyncDropped, c.asyncUs / 1000, c.asyncMaxUs, c.asyncPumpUs / 1000, c.asyncPumpWaits);
    if (c.workerReady || c.asyncReady)
        Log("route plans: off %ld (fails %ld) on %ld (fails %ld) hostCalls %ld", c.planOffCalls, c.planOffFails, c.planOnCalls, c.planOnFails, c.hostCalls);
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
struct Budget { LONG on; LONG nodes; LARGE_INTEGER t0; LONG capped; };
// Per thread: a plan on another thread (another world's lock, or the route worker) never sees or overwrites this one.
// Implicit TLS in a LoadLibrary'd DLL (the ASI loader) works on Vista+, existing threads included.
static __declspec(thread) Budget t_budget;
static LONG g_maxNodes, g_maxUs, g_onlyFlagged;
static const float kWall = 1e9f;  // >= the planner's 1e8 blocked-edge constant at 0x00FEF678

static int __cdecl BudgetStep(DWORD costArg5) {
    Budget& b = t_budget;
    if (!b.on) return 0;
    if (b.capped) return costArg5 == 1;
    if (costArg5 != 0) return 0;
    // clock starts at the first node, after Route_PlanLocked took the routing lock: lock wait is not search time
    if (b.nodes++ == 0) QueryPerformanceCounter(&b.t0);
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
    // DebugPathPlan (RouteOption hi word bit 0x1) as the "budget this route" flag for a future C# caller (docs/11 Q3)
    if (ts3native_counters.budgetMode != 1 || !ts3native_counters.budgetReady ||
        (g_onlyFlagged && !(((const DWORD*)(opts + 0x2C))[0] & 1)))
        return g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    // A plan nested on this thread (from a managed call inside the search) gets its own budget; the outer one resumes after.
    const Budget outer = t_budget;
    const Budget fresh = {1, 0, {}, 0};
    t_budget = fresh;
    LARGE_INTEGER t0;  // whole request, lock wait included: for the counters only
    QueryPerformanceCounter(&t0);
    u8 r = g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    LONG us = UsSince(t0), nodes = t_budget.nodes;
    if (t_budget.capped) InterlockedExchangeAdd(&ts3native_counters.budgetCapUs, us);
    t_budget = outer;
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

// Async route plan (docs/18 "Async plan"): one job slot. The owner (sim) thread writes it while idle or done, the worker
// while running; `state` is the hand-off.
enum { A_IDLE, A_RUNNING, A_DONE };
struct AsyncSlot {
    volatile LONG state;
    DWORD handle;  // script route handle (0x25xxxxxx)
    DWORD owner;   // thread that started the job (the Mono sim thread); only it can run the job's managed calls
    unsigned short x87;
    unsigned int mxcsr;
    u8 result[0x48];  // RoutePlanResult as IC_Route_Plan writes it
};
static AsyncSlot g_async;
static HANDLE g_asyncReady, g_asyncDone;
// IC_Route_Plan 0x765160: cdecl, [esp+4] = hidden RoutePlanResult* (also returned in eax), [esp+8] = route handle.
typedef u8*(__cdecl* IcPlanFn)(u8* out, DWORD handle);
static IcPlanFn g_icPlanOrig;
static void WaitAsyncDone();
static int g_inInvoke;  // > 0 while the caller/owner thread runs a managed call for the worker (ServeInvoke)

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
// self NULL: runtime_invoke(a[0..3] = method, obj, params, exc); else host delegate call 0xD75FA0(self; a[0..3] = out, args 2-4)
struct InvokeJob { void* a[4]; void* self; void* result; };
static InvokeJob* volatile g_invJob;
static HANDLE g_invReady, g_invDone;

// Host delegate call (docs/18 "Crash: async route assert"): MonoScriptHost vtable slot 0x108B7C4 -> 0xD75FA0, thiscall, hidden
// out pointer + 3 args, ret 10h, returns out. It boxes the callback arguments (mono_value_box), clears the running task's
// id host+0xBD0/+0xBD4 (`this` is host+8: [ebx+0BC8h]) around its mono_runtime_invoke (0xD76200), restores it (0xD7621F). Run on the worker, the
// boxing happens on a thread the GC does not scan and the clear hits the task the sim thread runs meanwhile (no yield mark
// from 0xD70F60 -> interp.c:3595 assert). So the whole call goes to the caller/owner thread, as it runs without the worker.
typedef void*(__fastcall* HostCallFn)(void* self, void* edx, void* out, void* a2, void* a3, void* a4);
static HostCallFn g_hostCallOrig;
static const DWORD HOST_CALL = 0x00D75FA0;
static const u8 kHostCallPrologue[] = {0x55, 0x8D, 0x6C, 0x24, 0x98, 0x81, 0xEC, 0x84, 0x00, 0x00, 0x00};  // push ebp; lea ebp,[esp-68h]; sub esp,84h
static DWORD* g_hostPtr = (DWORD*)(uintptr_t)0x011EE514;    // MonoScriptHost (ctor 0xD7E010)

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
    while (!v.self && i < nSeen && seen[i] != v.a[0]) i++;
    if (!v.self && i == nSeen && nSeen < 8) {
        seen[nSeen++] = v.a[0];
        Log("route worker: planner made a managed call, MonoMethod %p obj %p exc %p", v.a[0], v.a[1], v.a[3]);
    }
    // EA's guard of 0xD75FA0, on the thread that runs the script: a nested managed call runs without the task's id pair
    // (no yield mark), the running task gets it back after
    DWORD* id = *g_hostPtr ? (DWORD*)(uintptr_t)(*g_hostPtr + 0xBD0) : NULL;
    const DWORD id0 = id ? id[0] : 0, id1 = id ? id[1] : 0;
    if (id) id[0] = id[1] = 0;
    g_inInvoke++;
    v.result = v.self ? g_hostCallOrig(v.self, NULL, v.a[0], v.a[1], v.a[2], v.a[3])
                      : g_invokeOrig(v.a[0], v.a[1], (void**)v.a[2], (void**)v.a[3]);
    g_inInvoke--;
    if (id) { id[0] = id0; id[1] = id1; }
    InterlockedIncrement(v.self ? &ts3native_counters.hostCalls : &ts3native_counters.invokes);
    SetEvent(g_invDone);
}

// A managed call on the worker for a waiting caller (sync plan) or for the job's owner (async plan)?
static bool ForCaller() { return GetCurrentThreadId() == g_workerTid && (g_callerTid || g_async.state == A_RUNNING); }

// Worker side: hands the call to the caller/owner thread and runs nested plans it sends back meanwhile.
static void* Proxy(InvokeJob& v) {
    InvokeJob* prev = g_invJob;
    g_invJob = &v;
    SetEvent(g_invReady);
    HANDLE h[2] = {g_invDone, g_jobReady};
    while (WaitForMultipleObjects(2, h, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) ServePlan();  // a plan from inside the call
    g_invJob = prev;
    return v.result;
}

// In the runtime_invoke slot: every thread but the worker goes straight to the original.
static void* __cdecl InvokeHook(void* method, void* obj, void** params, void** exc) {
    if (!ForCaller()) return g_invokeOrig(method, obj, params, exc);
    InvokeJob v = {{method, obj, params, exc}, NULL, NULL};
    return Proxy(v);
}

// Detour of 0xD75FA0: the same for the whole host delegate call (boxing and EA's task-id guard included).
static void* __fastcall HostCallHook(void* self, void* edx, void* out, void* a2, void* a3, void* a4) {
    if (!ForCaller()) return g_hostCallOrig(self, edx, out, a2, a3, a4);
    InvokeJob v = {{out, a2, a3, a4}, self, NULL};
    return Proxy(v);
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

// Worker side of an async job: the whole original IC_Route_Plan (start points, Route_PlanCommon, Route_PlanLocked under
// the routing lock, result mapping) with the starter's FPU state. Its managed calls go to the owner thread (InvokeHook).
static void ServeAsync() {
    AsyncSlot& a = g_async;
    const unsigned short cw = GetX87();
    const unsigned int csr = _mm_getcsr();
    SetX87(a.x87);
    _mm_setcsr(a.mxcsr);
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    g_icPlanOrig(a.result, a.handle);
    LONG us = UsSince(t0);
    InterlockedExchangeAdd(&ts3native_counters.asyncUs, us);
    Max(&ts3native_counters.asyncMaxUs, us);
    SetX87(cw);
    _mm_setcsr(csr);
    InterlockedExchange(&a.state, A_DONE);
    SetEvent(g_asyncDone);
}

static DWORD WINAPI WorkerMain(LPVOID) {
    for (;;) {
        HANDLE h[2] = {g_jobReady, g_asyncReady};
        if (WaitForMultipleObjects(2, h, FALSE, INFINITE) == WAIT_OBJECT_0) ServePlan();
        else ServeAsync();
    }
}

static bool StartWorker() {
    QueryPerformanceFrequency(&g_qpf);
    InitializeCriticalSection(&g_workerCs);
    g_jobReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_jobDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_invReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_invDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_asyncReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_asyncDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    // 1 MB reserve like a default thread; it starts running only after DllMain returns (loader lock) and then just waits
    HANDLE t = g_jobReady && g_jobDone && g_invReady && g_invDone && g_asyncReady && g_asyncDone
                   ? CreateThread(NULL, 1 << 20, WorkerMain, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, &g_workerTid)
                   : NULL;
    if (!t) return false;
    CloseHandle(t);
    return true;
}

static u8 RunPlan(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                  int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    const DWORD me = GetCurrentThreadId();
    bool viaWorker = ts3native_counters.workerMode == 1;
    // An async job of this thread still runs (it holds the routing lock and may wait for a managed call only this thread can
    // run). A plan made inside such a managed call is nested in the job's search, as it would be without us: it goes to the
    // worker, which waits in InvokeHook. Any other plan waits here for the job, running its managed calls, then goes on.
    if (g_async.state == A_RUNNING && me == g_async.owner) {
        if (g_inInvoke) viaWorker = true;
        else WaitAsyncDone();
    }
    if (!viaWorker || !ts3native_counters.workerReady)
        return CallPlan(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
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

static u8 PlanCached(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent,
                     DWORD doPlan, u8* opts, int* out1, int* out2, DWORD guidLo, DWORD guidHi, ULONGLONG now) {
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

static u8 __fastcall PlanLockedHook(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent,
                                    DWORD doPlan, u8* opts, int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    InterlockedIncrement(&ts3native_counters.calls);
    ULONGLONG now = GetTickCount64();
    MaybeLogCounters(now);
    // the original's own early-outs (no plan requested, no start, no goal) stay with the original
    if ((u8)doPlan == 0 || starts[0] == starts[1] || goals[0] == goals[1])
        return g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    const Ts3nCounters& c = ts3native_counters;
    const bool on = (c.workerMode == 1 && c.workerReady) || (c.asyncMode == 1 && c.asyncReady);
    InterlockedIncrement(on ? &ts3native_counters.planOnCalls : &ts3native_counters.planOffCalls);
    const u8 r = PlanCached(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi, now);
    if (!r) InterlockedIncrement(on ? &ts3native_counters.planOnFails : &ts3native_counters.planOffFails);
    return r;
}

static const DWORD ROUTE_PLAN_LOCKED = 0x0082C6E0;
static const u8 kPlanLockedPrologue[] = {0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00};  // sub esp,0xD8

// --- Async route plan (docs/18 "Async plan"): the C# hook on ScriptCore.Route.Plan (mods/ts3fixes RouteBackoff.cs) asks
// through Route.GetOption with RouteOption bit 58 (0x0400000000000000: no enum name, never set by the game, so the
// unpatched game answers false and the C# side plans synchronously):
//   GetOption(bit 58)            START:  start this route's plan on the worker, true = started (the Sim's task then sleeps)
//   GetOption2(bit 58)           READY:  true = the job is done (or there is none); arms the next IC_Route_Plan to take it
//   GetOption2(bit 58 | bit 62)  CANCEL: disarm (the C# finally, also on a Sim reset)
//   GetOption2(bit 58 | bit 61)  NOYIELD: true = a yield here would hit the interp.c:3595 assertion (any route handle, also 0)
// The C# side then calls the original Route.Plan once; IC_Route_Plan hands over the stored result, so the script's own
// tail (start points, PostPlanCallback) runs once, as without us. While a job runs the sim thread keeps simulating; every
// Lock_Acquire on it that would block runs the job's managed calls (portal-lock query) while waiting, so it cannot
// deadlock against the worker holding the routing lock world+0x918.
static const DWORD kAsyncOptHi = 0x04000000, kAsyncCancelHi = 0x40000000, kNoYieldHi = 0x20000000;

// Can the running script code yield (docs/18 "Crash: yield in a nested call")? EA's yield opcode (0xE59751) calls the
// host's sleep, which sets yieldReturn (+0x149) only on the running task's root interpreter state (host+0xBC8 -> slot ->
// state, 0xD70F60), then asserts yieldReturn on the *current* state (0xE59763). The current state is the innermost
// ves_exec's (0xE53EB0 keeps it in 0x11F4170); a native->managed call (runtime_invoke 0xE5A600, or 0xD8A8E0) runs on a
// fresh one. So a yield is only safe when the current state is the task's root state.
static DWORD* g_curState = (DWORD*)(uintptr_t)0x011F4170;   // current interpreter state
static bool CanYield() {
    const u8* host = (const u8*)(uintptr_t)*g_hostPtr;
    if (!host) return false;
    const DWORD* slot = *(const DWORD* const*)(host + 0xBC8);  // set with the task id (+0xBD0) for each task it runs (0xD7F000)
    const DWORD* id = (const DWORD*)(host + 0xBD0);
    return slot && (id[0] | id[1]) && *g_curState && *g_curState == *slot;
}
typedef u8(__cdecl* GetOptionFn)(DWORD handle, DWORD optLo, DWORD optHi, DWORD options2);  // 0x74D040, bool in AL
typedef LONG(__fastcall* LockAcquireFn)(void* lock, void* edx, const DWORD* deadline);       // 0x4E16F0 thiscall, ret 4
static GetOptionFn g_getOptOrig;
static LockAcquireFn g_lockOrig;
static DWORD g_collect;  // handle whose READY said "done": the owner's next IC_Route_Plan for it takes the stored result

// Owner side: runs the job's managed calls until it is done (2 s watchdog: log and keep waiting, as RunPlan does).
static void WaitAsyncDone() {
    for (DWORD s = 0; g_async.state == A_RUNNING;) {
        HANDLE h[2] = {g_asyncDone, g_invReady};
        const DWORD w = WaitForMultipleObjects(2, h, FALSE, 2000);
        if (w == WAIT_OBJECT_0 + 1) ServeInvoke();
        else if (w == WAIT_TIMEOUT) {
            s += 2;
            InterlockedIncrement(&ts3native_counters.watchdogs);
            Log("route async: thread %lu waited %lu s for the job of route %08lX, still waiting", GetCurrentThreadId(), s, g_async.handle);
        }
    }
}

// Owner side: waits for its job (running the job's managed calls) and throws the result away. No-op on other threads.
static void DropOwnJob() {
    if (g_async.state == A_IDLE || GetCurrentThreadId() != g_async.owner) return;
    WaitAsyncDone();
    InterlockedIncrement(&ts3native_counters.asyncDropped);
    InterlockedExchange(&g_async.state, A_IDLE);
}

static u8* __cdecl IcPlanHook(u8* out, DWORD handle) {
    if (ts3native_counters.asyncMode != 1) DropOwnJob();  // switched off mid-job: no job left without a collector
    // only the Mono sim thread calls internal calls (the worker calls g_icPlanOrig directly), so g_collect needs no lock
    const bool collect = g_collect == handle;
    g_collect = 0;
    if (g_async.state == A_IDLE || GetCurrentThreadId() != g_async.owner) return g_icPlanOrig(out, handle);
    // another route: its Route_PlanLocked waits for the job first (RunPlan)
    if (g_async.handle != handle) return g_icPlanOrig(out, handle);
    WaitAsyncDone();  // normally already done (READY said so)
    if (collect) {
        memcpy(out, g_async.result, sizeof(g_async.result));
        InterlockedIncrement(&ts3native_counters.asyncCollected);
    } else {
        InterlockedIncrement(&ts3native_counters.asyncDropped);  // a plan the C# hook did not arm: plan it now, as asked
    }
    InterlockedExchange(&g_async.state, A_IDLE);
    return collect ? out : g_icPlanOrig(out, handle);
}

static u8 AsyncStart(DWORD handle) {
    g_collect = 0;
    if (ts3native_counters.asyncMode != 1 || !ts3native_counters.asyncReady) return 0;  // asyncMode is writable at runtime
    // another job runs, or the caller could not sleep (nested native->managed call: its yield would abort the game)
    if (g_async.state == A_RUNNING || !CanYield()) { InterlockedIncrement(&ts3native_counters.asyncRefused); return 0; }
    if (g_async.state == A_DONE) InterlockedIncrement(&ts3native_counters.asyncDropped);  // its caller gave up
    EnterCriticalSection(&g_workerCs);
    const bool invokeOk = EnsureInvokeHook();  // the job's managed calls must come back to this thread
    LeaveCriticalSection(&g_workerCs);
    if (!invokeOk) return 0;
    g_async.handle = handle;
    g_async.owner = GetCurrentThreadId();
    g_async.x87 = GetX87();
    g_async.mxcsr = _mm_getcsr();
    ResetEvent(g_asyncDone);
    InterlockedExchange(&g_async.state, A_RUNNING);
    InterlockedIncrement(&ts3native_counters.asyncStarted);
    SetEvent(g_asyncReady);
    return 1;
}

static u8 __cdecl GetOptionHook(DWORD handle, DWORD optLo, DWORD optHi, DWORD options2) {
    if (optLo != 0 || (optHi & ~(kAsyncCancelHi | kNoYieldHi)) != kAsyncOptHi) return g_getOptOrig(handle, optLo, optHi, options2);
    if (optHi & kNoYieldHi) return !CanYield();
    if (ts3native_counters.asyncMode != 1) DropOwnJob();  // switched off mid-job: READY then says "done", Route.Plan plans inline
    if (!(u8)options2) return AsyncStart(handle);
    const bool mine = g_async.state != A_IDLE && g_async.handle == handle && GetCurrentThreadId() == g_async.owner;
    if (optHi & kAsyncCancelHi) {  // the caller gives up (finally, Sim reset): its job must not outlive it
        g_collect = 0;
        if (mine) DropOwnJob();
        return 1;
    }
    if (mine && g_async.state == A_RUNNING && WaitForSingleObject(g_invReady, 0) == WAIT_OBJECT_0) ServeInvoke();  // keep it moving
    if (mine && g_async.state == A_RUNNING) return 0;
    g_collect = mine ? handle : 0;
    return 1;
}

// Every Lock_Acquire in the game passes through here; only the job owner's waits during a job are changed (INFINITE and
// finite deadlines, an absolute GetTickCount): try-lock (deadline 0 = one TryEnter/0 ms wait in the original) in a loop,
// running the job's managed calls in between. A finite deadline that passes ends with one original call with it (one
// last try; the original's own result, -2 = timed out).
static LONG __fastcall LockAcquireHook(void* lock, void* edx, const DWORD* deadline) {
    if (g_async.state != A_RUNNING || *deadline == 0 || GetCurrentThreadId() != g_async.owner) return g_lockOrig(lock, edx, deadline);
    static const DWORD kTry = 0;
    LONG r = g_lockOrig(lock, edx, &kTry);
    if (r != -2) return r;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    InterlockedIncrement(&ts3native_counters.asyncPumpWaits);
    for (DWORD start = GetTickCount(), next = 2000;;) {
        if (g_async.state != A_RUNNING) { r = g_lockOrig(lock, edx, deadline); break; }  // job over: wait as the game would
        HANDLE h[2] = {g_invReady, g_asyncDone};
        if (WaitForMultipleObjects(2, h, FALSE, 1) == WAIT_OBJECT_0) ServeInvoke();
        if ((r = g_lockOrig(lock, edx, &kTry)) != -2) break;
        if (*deadline != 0xFFFFFFFF && (LONG)(GetTickCount() - *deadline) >= 0) { r = g_lockOrig(lock, edx, deadline); break; }
        if (GetTickCount() - start >= next) {
            next += 2000;
            InterlockedIncrement(&ts3native_counters.watchdogs);
            Log("route async: lock %p busy for %lu s during the job of route %08lX, still waiting", lock, next / 1000 - 2, g_async.handle);
        }
    }
    InterlockedExchangeAdd(&ts3native_counters.asyncPumpUs, UsSince(t0));
    return r;
}

static const DWORD IC_ROUTE_PLAN = 0x00765160, ROUTE_GET_OPTION = 0x0074D040, LOCK_ACQUIRE = 0x004E16F0;
static const u8 kIcPlanPrologue[] = {0x0F, 0x57, 0xC0, 0x8B, 0x0D, 0xC4, 0x40, 0x1D, 0x01};     // xorps xmm0,xmm0; mov ecx,[0x11D40C4]
static const u8 kGetOptionPrologue[] = {0x8B, 0x0D, 0xC4, 0x40, 0x1D, 0x01};                     // mov ecx,[0x11D40C4]
static const u8 kLockAcquirePrologue[] = {0x53, 0x55, 0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x24, 0x00};  // push ebx/ebp/esi; mov esi,ecx; cmp byte [esi+24h],0

// [RouteAsync]: returns the start value of the switch, or -1 when off.
static int ReadRouteAsync(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"RouteAsync", L"Enabled", 0, ini)) { Log("route async disabled (Enabled=0)"); return -1; }
    return Clamp(GetPrivateProfileIntW(L"RouteAsync", L"Mode", 1, ini), 0, 1);
}

// Needs the worker thread and the Route_PlanLocked detour (RunPlan) already in place. All three detours or none in use:
// a missing one leaves asyncReady 0, and START is then never answered yes.
static bool InstallAsync(int mode) {
    if (!g_hostCallOrig) { Log("FAIL route async: host call detour missing, async plans stay off"); return false; }
    g_lockOrig = (LockAcquireFn)InstallDetour("Lock_Acquire", LOCK_ACQUIRE, kLockAcquirePrologue, sizeof(kLockAcquirePrologue), (void*)LockAcquireHook);
    g_icPlanOrig = g_lockOrig ? (IcPlanFn)InstallDetour("IC_Route_Plan", IC_ROUTE_PLAN, kIcPlanPrologue, sizeof(kIcPlanPrologue), (void*)IcPlanHook) : NULL;
    g_getOptOrig = g_icPlanOrig ? (GetOptionFn)InstallDetour("Route_GetOption", ROUTE_GET_OPTION, kGetOptionPrologue, sizeof(kGetOptionPrologue), (void*)GetOptionHook) : NULL;
    if (!g_getOptOrig) { Log("FAIL route async: detours incomplete, async plans stay off"); return false; }
    ts3native_counters.asyncMode = mode;
    ts3native_counters.asyncReady = 1;
    Log("route async settings: Mode=%d", mode);
    return true;
}

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
    // the worker's host delegate calls must run on the caller/owner thread; without that detour the worker stays unused
    if (worker) g_hostCallOrig = (HostCallFn)InstallDetour("host call", HOST_CALL, kHostCallPrologue, sizeof(kHostCallPrologue), (void*)HostCallHook);
    if (worker && !g_hostCallOrig) Log("FAIL route worker: host call detour missing, plans stay on the calling thread");
    ts3native_counters.workerReady = worker && g_hostCallOrig;
    ts3native_counters.enabled = 1;
    g_nextLog = (LONG64)GetTickCount64() + 60000;
    return true;
}

#include "savewrite.cpp"
#include "decompress.cpp"
#include "alloc.cpp"
#include "waterjps.cpp"

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
        const int asyncMode = ReadRouteAsync(ini);
        if (asyncMode >= 0 && !worker && !(worker = StartWorker())) Log("FAIL route async: worker thread start (err %lu)", GetLastError());
        keepLog = InstallPlanDetour(cache, worker, ApplyRouteBudget(ini));
        if (asyncMode >= 0 && worker && keepLog) InstallAsync(asyncMode);
        keepLog = ApplySaveWrite(ini) || keepLog;
        // the NOYIELD query guards ts3fixes' other yields too, so it is answered with async off (START then says no)
        if (!g_getOptOrig)
            g_getOptOrig = (GetOptionFn)InstallDetour("Route_GetOption", ROUTE_GET_OPTION, kGetOptionPrologue, sizeof(kGetOptionPrologue), (void*)GetOptionHook);
        keepLog = ApplyDecompress(ini) || keepLog;
        keepLog = ApplyAlloc(ini) || keepLog;
        keepLog = ApplyWaterJps(ini) || keepLog;
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
