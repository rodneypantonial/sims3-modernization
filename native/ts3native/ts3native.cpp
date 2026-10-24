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
#include <intrin.h>  // _ReturnAddress (vfG11CloseBy)

#define TS3NATIVE_VERSION "0.36.37"

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

static DWORD g_mainTid;  // thread that loaded us (Run)

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

// [Crash] Log=1 (0.12.1): the first 16 first-chance exceptions with code, address, thread and up to 8 code addresses found
// on the stack (game .text or this module), written unbuffered to ts3native.log. Added after a run died with no EA xcpt
// report and no WER event (2026-10-17 bp-d-async). C++ exceptions (0xE06D7363) count for 2 lines at most.
static volatile LONG g_xcptN, g_xcptCpp;
static DWORD g_selfLo, g_selfHi;
// up to `max` code addresses (game .text or this module) in the first 512 stack words, as " %08lX" each
static void XcptRefs(const CONTEXT* c, char* refs, int cap, int max) {
    int len = 0, k = 0;
    refs[0] = 0;
    const DWORD* sp = (const DWORD*)(uintptr_t)c->Esp;
    for (int i = 0; i < 512 && k < max && Readable(sp + i, 4); i++) {
        const DWORD v = sp[i];
        if ((v >= 0x00401000 && v < 0x00F00000) || (v >= g_selfLo && v < g_selfHi)) {
            len += sprintf_s(refs + len, cap - len, " %08lX", v);
            k++;
        }
    }
}
static DWORD P5cRelMatch(const CONTEXT* c);  // p5c.cpp: registers in an object our graveyard flush released (crash I)
static LONG CALLBACK XcptLog(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    const DWORD code = r->ExceptionCode;
    // 0xE0503401: P4's lazy stop (p4.cpp kP4LazyCode), caught by P4Guarded; not a crash (docs/94 §21.8.1)
    if (code == 0x406D1388 || code == 0x40010006 || code == 0x4001000A || code == DBG_CONTROL_C || code == 0xE0503401)
        return EXCEPTION_CONTINUE_SEARCH;
    if (code == 0xE06D7363 && InterlockedIncrement(&g_xcptCpp) > 2) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_xcptN) > 16) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = e->ContextRecord;
    char refs[128];
    XcptRefs(c, refs, sizeof(refs), 8);
    Log("exception %08lX at 0x%08lX (info %08lX %08lX) thread %lu eip %08lX esp %08lX; stack code refs:%s", code,
        (DWORD)(uintptr_t)r->ExceptionAddress, r->NumberParameters > 0 ? (DWORD)r->ExceptionInformation[0] : 0,
        r->NumberParameters > 1 ? (DWORD)r->ExceptionInformation[1] : 0, GetCurrentThreadId(), c->Eip, c->Esp, refs);
    if (code == EXCEPTION_ACCESS_VIOLATION) P5cRelMatch(c);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void InstallXcptLog(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"Crash", L"Log", 1, ini)) return;
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void*)XcptLog, &self);
    const IMAGE_NT_HEADERS* nt = self ? (const IMAGE_NT_HEADERS*)((const u8*)self + ((const IMAGE_DOS_HEADER*)self)->e_lfanew) : NULL;
    g_selfLo = (DWORD)(uintptr_t)self;
    g_selfHi = nt ? g_selfLo + nt->OptionalHeader.SizeOfImage : g_selfLo;
    if (AddVectoredExceptionHandler(1, XcptLog)) Log("crash log: first 16 first-chance exceptions (module 0x%08lX-0x%08lX)", g_selfLo, g_selfHi);
}

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
// Counters for test tools: Frida `Module.findExportByName('ts3native.asi', 'ts3native_counters')`, read 4467 int32 (v108 gate site 16 after v107, version field 108; v107 PlanDrain after v106; v106 P5 steps 2-4 after v105 (layout of merge eaef8a1b; zeros without it); v105 RecsUnlocked after v104, version field 105; v104 refcount guard after v103; v103 job deadline + own waits + goal-step split after v102; v102 goal split + GoalGen D1 + H2 IC window after v101, version field 102; v101 foreign kinds after v100; v100 placement RO after v99; v99 plan deadline + fail memo after v98; v98 lock holders after v97; v97 sync plan phases after v96; v96 host-call split + DoorMemo after v93; v93 route-to-object RO appended after v95 at the 2026-10-22 0325 merge, version field stays 95; v95 F-G held-out count after v94, v94 design U phase 2 after v92, v92 route allocator sites after v91, v91 [Gate0RO] after v90, v90 replan dedupe after v88, all at the 2026-10-22 integration merge; version field 95; v88 cell memo probe appended after v89; v89 tail unlock; v87 route job phases; v86; v77 AvoidRO before v78, v79 unseen probe + throttle after v78, v80 unseen throttle phase 2, v81 foreign job cap, v83 unseen diagnostics, v84 foreign job split, v85 H2 counters, v86 acting early-out, no v52, v58 or v82 fields in this build).
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
    // v11: save package compression on workers and the save's flushes on a worker (docs/15 phase 10, savecompress.cpp, savewrite.cpp)
    LONG compressReady;           // 1 when the commit call 0x4A850F and the codec slot 0xFB901C are hooked
    volatile LONG compressMode;   // runtime switch, writable by test tools: 0 = the game compresses in the commit loop, 1 = workers ahead of it
    volatile LONG compressVerify; // 1 = a served call also runs the game's compressor and uses its result (counts mismatches)
    LONG compressBatches;         // commits run with mode 1
    LONG compressJobs;            // records queued for the workers
    LONG compressHits;            // codec calls answered from a finished job
    LONG compressMisses;          // codec calls in a batch that ran the game's slot (no job, other mode, job failed)
    LONG compressMismatches;      // verify: job output differed from the game's
    LONG compressInMB;            // uncompressed MB compressed by jobs
    LONG compressWaitMs;          // committing-thread time waiting for (or running) jobs, milliseconds
    LONG compressLastMs;          // last batch: whole commit (FUN_004A7910), milliseconds
    LONG compressPeakMB;          // most bytes in flight (copies + outputs), MB
    LONG flushReady;              // 1 when the two flush calls and the load copy call are hooked
    volatile LONG flushMode;      // runtime switch, writable by test tools: 0 = the game flushes during the save, 1 = a worker after it
    LONG flushDeferred;           // flush calls recorded for the worker
    LONG flushJobs;               // worker runs started
    LONG flushWaits;              // times the sim thread had to wait for the worker (next save, travel, load)
    LONG flushWaitMs;             // their total wait, milliseconds
    LONG flushLastMs;             // last worker run, milliseconds
    LONG flushFails;              // files the worker could not open or flush
    // v12 (0.10.1): where save compression really runs (records are committed one by one when closed, docs/15 phase 10)
    LONG compressCommits;         // calls of the commit hook 0x4A850F (any mode)
    LONG compressCommitPending;   // pending records at the last of them
    LONG compressDirectCalls;     // codec compress calls outside a batch (dst set), any thread
    LONG compressDirectMs;        // their time in the game's compressor, milliseconds
    LONG compressDirectMB;        // their input, MB
    // v13: terrain height cache (docs/20 §4b, heightcache.cpp)
    LONG heightReady;             // 1 when 0xC2A570 and 0xC2A840 are detoured
    volatile LONG heightMode;     // runtime switch, writable by test tools: 0 = the game's raycast, 1 = cache (+0x1B4)
    LONG heightCalls;             // calls in mode 1
    LONG heightHits;              // answered from the table
    LONG heightMisses;            // ran the raycast and stored the result
    LONG heightRebuilds;          // cell rebuilds (each drops every entry)
    LONG heightMismatches;        // Verify=1: hits whose cached height differed from the raycast
    // v14: GC allocation thread check (docs/29 §4.5, gcthreads.cpp), measurement only
    LONG gcCheckReady;            // bit 0: GC_allocobj detoured, bit 1: GC_alloc_large detoured
    LONG gcOwnerTid;              // first thread that reached a GC slow path
    LONG gcForeignAllocs;         // slow-path calls from any other thread (a latent heap race when > 0)
    LONG gcForeignTid;            // last such thread
    // v15: Mono loader lock 0x11F43A8 check (docs/29 §5, iclock.cpp), measurement only
    LONG icLockReady;             // bit 0: mono_loader_lock 0xE85020 detoured, bit 1: mono_loader_unlock 0xE85030 detoured
    LONG icLockEntersK;           // wrapper enters, units of 1024
    LONG icLockContended;         // enters whose TryEnterCriticalSection failed (another thread held it)
    LONG icLockWaitUs;            // their total wait, microseconds (QPC)
    LONG icLockMaxWaitUs;         // longest single wait
    LONG icLockOwnerChanges;      // enters by a thread other than the previous owner
    LONG icLockPrevTid;           // owner before the last change
    LONG icLockLastTid;           // last owner
    LONG icLockLeaveWaiters;      // unlocks with at least one thread waiting (Leave has to wake it)
    LONG icLockCallback;          // [0x11EE6CC] (0xDA5970's callback) at the first enter after it was set
    // v16: routing gate measurement (docs/18 H6 step S2a, routegate.cpp), no behaviour change
    LONG rgReady;                 // bit 0: Lock_Acquire hooked, bit 1: Lock_Release hooked, bit 2: all 12 gate sites hooked
    LONG rgLock;                  // world+0x918, learned from Route_PlanLocked (0 until the first plan)
    LONG rgOwnerTid;              // thread holding world+0x918 now, 0 = free
    LONG rgAcquires;              // outermost acquires of world+0x918 (Lock_Acquire returned recursion count 1)
    LONG rgEntries;               // gate-site entries seen once the lock is known (all threads, nested too): the hooks' cost
    LONG rgUntracked;             // entries not tracked because the thread's 16-frame return stack was full
    LONG rgUnlocked[12];          // per site (docs/18 S0 table order): outermost gate entries by a thread not holding +0x918
    LONG rgJob[12];               // the same entries made while a route-worker or async job runs
    // v17: compressed-bytes cache in the codec slot (docs/26 §8, savecompress.cpp)
    LONG cacheReady;              // 1 when [SaveCompress] CacheMB > 0 and the slot hook is in
    volatile LONG cacheMode;      // runtime switch, writable by test tools: 0 = no lookups or stores, 1 = cache (+0x280)
    LONG cacheHits;               // compress calls answered from the cache
    LONG cacheMisses;             // compress calls that ran the game's compressor (output stored)
    LONG cacheMismatches;         // Verify=1: a hit whose stored bytes differed from the game's (the game's are used)
    LONG cacheEvicted;            // entries dropped to stay under CacheMB (least recently used first)
    LONG cacheEntries;            // entries held now
    LONG cacheKB;                 // compressed bytes held now, KB
    LONG cacheHitMB;              // uncompressed input of the hits, MB
    LONG cacheHashMs;             // time hashing inputs, milliseconds
    // v18: second managed thread step 1 (docs/30 P1/P2, monothread.cpp)
    LONG mtReady;                 // bit 0: GC root hook [0x11949CC] replaced (P2), bit 1: Simulate slot hooked, bit 2: first tick ran (P1), bit 3: per-iteration tick (simloop.cpp)
    volatile LONG mtMode;         // runtime switch (+0x2A8); step 1 has only 0 = patches, no worker
    LONG mtSimTid;                // sim thread id (first Simulate tick)
    LONG mtThreads;               // GC thread table entries in use at the first tick (after registration)
    LONG mtSimRegistered;         // 1 = P1 added the sim thread, -1 = GC_new_thread did not, 0 = it was already registered
    LONG mtPushCalls;             // root hook calls with the world stopped
    LONG mtStacksPushed;          // other threads' stacks pushed in those calls
    LONG mtCtxFails;              // GetThreadContext failed (or ESP out of range): whole committed stack pushed instead
    // v19: lot streaming measurement (docs/28 §5.5, lotstream.cpp), no behaviour change. *MaxUs / *MaxLots / *MaxObjs:
    // highest since the last reset (stutter-profile.js zeroes them each second), all-time when nothing resets them
    LONG lsReady;                 // bits: 1 add execute, 2 remove execute, 4 RunJobs, 8 lot scoring, 16 SetActive, 32 sync wait, 64 objScene
    volatile LONG lsMode;         // runtime switch (+0x2C8): 0 = hooks pass through, 1 = measure
    LONG lotAdds;                 // AddLotObjectsToScene remote calls run (0xABD3C0, fn 0xAC1130)
    LONG lotAddUs;                // their total time, microseconds (QPC)
    LONG lotAddMaxUs;             // longest one
    LONG lotAddObjs;              // objects in the lots added
    LONG lotAddMaxObjs;           // most objects in one added lot
    LONG lotRemoves;              // remove remote calls run (0xABD3E0, fn 0xAB4C00)
    LONG lotRemoveUs;
    LONG lotRemoveMaxUs;
    LONG pumpCalls;               // outermost RunJobs 0x59A430 passes on the sim thread
    LONG pumpUs;
    LONG pumpMaxUs;               // longest pass: the per-pass spike
    LONG pumpLotPasses;           // passes that ran at least one lot add or remove
    LONG pumpMaxLots;             // most lot adds + removes in one pass
    LONG lotScoreCalls;           // lot scoring 0xC6C290 calls (main thread)
    LONG lotScoreUs;
    LONG lotScoreMaxUs;
    LONG lotScoreChanged;         // scoring calls that switched at least one lot
    LONG lotSetOn;                // Lot::SetActive calls that activate a lot (the game's own change test)
    LONG lotSetOff;               // ... that deactivate one
    LONG syncWaits;               // SyncWait=1: synchronous remote calls the sim thread posted to another thread
    LONG syncWaitUs;
    LONG syncWaitMaxUs;
    LONG objScenes;               // ObjScene=1: UpdateObjectSceneNode 0xABFAC0 calls
    LONG objSceneUs;
    LONG objSceneMaxUs;
    LONG objSceneHist[4];         // calls < 0.1 ms, < 1 ms, < 10 ms, >= 10 ms (+0x330)
    LONG lsOtherFns;              // add/remove execute calls with another fn pointer (not counted; first 8 logged)
    LONG lsLogDropped;            // events not written to the -lotstream.jsonl (buffer full or write failed)
    // v20: [Verify] runtime checks of the mapping-doc inferences (verify.cpp, docs/15 [Verify]), no behaviour change.
    // *MaxUs / *Max*: highest since the last reset (all-time when nothing resets them); *K: units of 1024 calls
    LONG vfReady;                 // bit i: probe i of kVf hooked (verify.cpp order), bit 28: readout thread runs
    volatile LONG vfMode;         // runtime switch (+0x34C): 0 = hooks pass through, 1 = measure
    LONG vfThread[12][6];         // +0x350, per thread probe: calls on main, sim, other, off the device thread; us total, max us
    LONG vfSwaps;                 // terrain cell shape swaps 0xC2A840 (docs/38 C1)
    LONG vfSwapsInRay;            // ... while another thread is inside the ray cast 0xC2A570
    LONG vfRayCallsK;             // ray casts 0xC2A570
    LONG vfRayMaxInFlight;        // most threads inside 0xC2A570 at once
    LONG vfKeyLists;              // GetKeyList 0x4B1AE0 calls (docs/39 R2)
    LONG vfRegDbs;                // RegisterDatabase 0x4B2D00 calls
    LONG vfKeyListOverlaps;       // a key list and a registration on two threads at once (counted by the later one)
    LONG vfIdLookupsK;            // object id lookups 0x939100 (docs/45)
    LONG vfIdOffSim;              // ... on a thread that is not the sim thread (once the sim thread id is known)
    LONG vfIdOffSimRet[8];        // +0x494, first 8 distinct return addresses of those
    LONG vfPkgCalls;              // GetPackageSource 0x4AFFC0, outermost calls (docs/39 R1)
    LONG vfPkgUs;
    LONG vfPkgMaxUs;
    LONG vfPkgWalked;             // package index searches 0x4AA1B0 inside them
    LONG vfPkgMaxWalked;          // most in one call
    LONG vfIdChain[6];            // +0x4C8, sim-thread lookups by nodes compared: 0, 1, 2-3, 4-7, 8-15, 16+ (docs/45 O1/O2)
    LONG vfIdMaxChain;
    LONG vfIdObjects;             // object count [service+0x1038] at the last lookup
    LONG vfW2lCallsK;             // Lot_WorldToLocal2D 0xAB8540 (docs/38 C2)
    LONG vfFindLotCalls;          // WorldManager_FindLotById 0xC60D30 (docs/38 C3)
    LONG vfTreeRebuilds;          // SpeedTree cluster rebuilds 0x712480 (docs/48 S2)
    LONG vfTreeMaxPerFrame;       // most between two FrameDb consumes (one per render frame)
    LONG vfFdbHolds;              // FrameDb_ConsumeAndInterpolate 0x693780 on main (docs/41 S1): its time bounds the +0x48 hold
    LONG vfFdbHoldUs;
    LONG vfFdbHoldMaxUs;
    LONG vfFdbWaits;              // sim-thread 0x593590 + 0x593600 calls (EnterCriticalSection +0x48 inside)
    LONG vfFdbWaitUs;
    LONG vfFdbWaitMaxUs;
    LONG vfUiLock[3][3];          // +0x510, Lock_Acquire with timeout pointer 0xFBA5CC (the UI lock's 10 sites) per role main/sim/other: acquires, wait us, max us (docs/47 UI-L1)
    LONG vfFpsGate;               // 10 FPS cap gate reached at 0xECAA65 (docs/41 P5)
    LONG vfSimSlice;              // readout: byte [SimService+0x139] (docs/42), -1 no service
    LONG vfSpeedTreeKB;           // 0.10.7: always -1 ([0x11D8628] is not a live-bytes total, docs/15 [Verify])
    LONG vfVramMB;                // readout: VRAM MB the game stored, [[0x11CDD10]+0x238] (docs/40 §5)
    LONG vfRcBytesKB;             // readout: ResourceCache held bytes +0x94 (0.10.7: the priority-0 ResourceCache only, docs/39)
    LONG vfRcFlushKB;             // readout: its +0x98
    LONG vfRcBudgetMB;            // readout: its CacheBudget +0x8C (0.10.6 read the AnimationCache's 15 MB)
    LONG vfDeviceTid;             // readout: device owner thread [[0x11CECC8]+0x10]
    LONG vfUntracked;             // timed entries not tracked (16 frames open on that thread)
    LONG vfLogDropped;            // lines not written to the -verify.jsonl
    // v21: [Verify] probes for the docs/51 rows the first run had no key for, loop count of the 10 FPS gate
    LONG vfReady2;                // +0x55C, bit i: probe 27 + i of kVf hooked
    LONG vfThread2[34][6];        // +0x560, thread slots 12-45 (verify.cpp order; 42-45 = shape swap, tree rebuild, JobManager and SimService Lock_Acquire)
    LONG vfOverlap[4];            // +0x890, two threads in an overlap group at once: scene writes vs cull jobs, UI draw vs layout IC, R1 effect update vs UI draw, route ray 0x686CC0 twice
    LONG vfFdbPublishes;          // +0x8A0, FrameDb_PublishFrame 0x593600 on the sim thread (frames published, docs/41 §5)
    LONG vfFpsLoops;              // +0x8A4, main-loop iterations at 0xECA9ED (the gate's denominator)
    // v22: [Verify] probes for the docs/52-62 ledger rows (docs/51 I-52..I-62)
    LONG vfReady3;                // +0x8A8, bit i: probe 58 + i of kVf hooked
    LONG vfThread3[34][6];        // +0x8AC, thread slots 46-79 (verify.cpp order: Mono warm-up, routing sites, pump same pass, remote calls, lot add, UI/input/shader probes)
    LONG vfOverlapCur;            // +0xBDC, two threads in CursorManager_Apply 0x505620 at once (docs/55 R-CUR)
    LONG vfShaderStartup[2];      // +0xBE0, Shader_CreateVertexShader 0x799780 / CreatePixelShader 0x799810 calls before the first world load 0xC6D970
    LONG vfMonoIcNestMax;         // +0xBE8, readout: largest [0x11F2D84] (EA IC nesting counter, docs/56) seen by the 1 s poll
    LONG vfMonoIcNestNeg;         // +0xBEC, polls that read it below 0
    // v23: native fix batch 2 (PLAN "Next, in order" 2): [JobQueue], [D3DMultithreaded], [Alloc] Dedupe
    LONG jqReady;                 // +0xBF0, bit 0 sim pump slot stub, bit 1 ExecuteJob counters, bit 2 worker count patched
    LONG jqSimMode;               // +0xBF4, switch (#13): 1 = sim pump skips the shared queue (bit 31 of its record cleared)
    LONG jqWorkers;               // +0xBF8, generic JobManager workers asked for (#14; 0 = game default 1, not patched)
    LONG jqSimPumps;              // +0xBFC, sim-thread RunJobs calls seen by the slot stub
    LONG jqSimSkipPasses;         // +0xC00, of those, passes run with bit 31 cleared
    LONG jqSimSharedJobs;         // +0xC04, mask -1 (shared) jobs executed on the sim thread (0 while jqSimMode = 1)
    LONG jqSimRenderJobs;         // +0xC08, frame jobs (docs/46 §2 fns) executed on the sim thread
    LONG jqRenderJobs;            // +0xC0C, frame jobs executed on any thread
    LONG jqWorkerTids;            // +0xC10, distinct non-main, non-sim threads that ran a shared job (JobThread0..n)
    LONG jqWorkerJobs;            // +0xC14, shared jobs executed on those threads
    LONG d3dMtReady;              // +0xC18, 1 = CreateDevice behaviour flags got D3DCREATE_MULTITHREADED (#4)
    LONG allocDedupe;             // +0xC1C, switch: 1 = [Alloc] drops a Free of a chunk already in this thread's bin
    LONG allocDupFrees;           // +0xC20, duplicate frees seen (and dropped while allocDedupe = 1)
    // v24: native fix batch 3 (PLAN "Next, in order" 3, in-place sim savings): [LotSinCos] (#11), [IdCache] (#22)
    LONG lscReady;                // +0xC24, 1 = Lot_WorldToLocal2D 0xAB8540 detoured
    volatile LONG lscMode;        // +0xC28, switch: 0 = the game's code, 1 = sin/cos from the cache
    LONG lscCallsK;               // +0xC2C, calls with the switch on, thousands (published every 1024 calls)
    LONG lscHitsK;                // +0xC30, of those, sin/cos taken from the cache, thousands
    LONG lscMismatches;           // +0xC34, Verify=1: hits whose output bits differ from the game's (the game's answer is returned)
    LONG idcReady;                // +0xC38, 1 = ObjectId_Lookup 0x939100 and EraseNode 0x938D00 detoured
    volatile LONG idcMode;        // +0xC3C, switch: 0 = the game's walk, 1 = sim-thread lookups use the id cache
    LONG idcLookupsK;             // +0xC40, sim-thread lookups with the switch on, thousands (published every 1024)
    LONG idcHitsK;                // +0xC44, of those, answered from the cache
    LONG idcFillsK;               // +0xC48, found by our walk and stored
    LONG idcNotFoundK;            // +0xC4C, not in the hash (sentinel returned, nothing stored)
    LONG idcStepsK;               // +0xC50, chain nodes visited by our walks (chain length = steps / (fills + notFound))
    LONG idcErases;               // +0xC54, EraseNode calls (each drops its cache entry)
    LONG idcMismatches;           // +0xC58, Verify=1: hits the chain walk disagreed with (entry dropped, walk answer returned)
    LONG idcObjects;              // +0xC5C, objects in the hash (service +0x1038), last publish
    // v25: H6 S2b closure-site measurement ([RouteGate] Sites=1, routegate.cpp + rtsites.cpp, docs/60 §2.2-2.3) and S3a
    // terrain graveyard ([TerrainGrave], heightcache.cpp, docs/60 §2.2 #2). "Job" = a route-worker plan (Mode 1 sync) or an
    // async plan runs. *WaitUs fields belong to S3b (the closures that wait) and stay 0 in this build.
    LONG rgUnlocked2[3];          // +0xC60, rgUnlocked for gate sites 13-15 (0x628A90, 0x835240, 0x835110)
    LONG rgJob2[3];               // +0xC6C, rgJob for gate sites 13-15
    LONG rtReady;                 // +0xC78, bit i = rtsites.cpp site i hooked
    LONG vfRtGateOffSim;          // +0xC7C, outermost unlocked gate entries (sites 1-15) by a thread neither sim nor worker
    LONG vfRtQtReads;             // +0xC80, worker entries into the quadtree readers 0x6583E0 / 0x83B3A0 (outermost)
    LONG vfRtQtBare;              // +0xC84, worker entries into the leaves 0x681820 / 0x681E00 outside a reader (must be 0)
    LONG vfRtQtWriteInJob;        // +0xC88, quadtree mutator calls (0x6823E0, 0x682080, 0x6819B0) while a job runs
    LONG vfRtQtWaitUs;            // +0xC8C, S3b
    LONG vfRtQtWaitMaxUs;         // +0xC90, S3b
    LONG vfRtDestroyInJob;        // +0xC94, ObjectService_DestroyById 0x939420 calls while a job runs
    LONG vfRtDestroyOffSim;       // +0xC98, 0x939420 calls on a thread other than the sim thread
    LONG vfRtDestroyWaitUs;       // +0xC9C, S3b
    LONG vfRtLotGridWorker;       // +0xCA0, worker entries into WorldManager_LotGridLookup 0xC688A0
    LONG vfRtLotIdxRebuildInJob[4];  // +0xCA4, lot index rebuild 0xC68680 entries during a job: main, sim, worker, other
    LONG vfRtPendingInJob;        // +0xCB4, worker entries into ApplyPendingUpdates 0x82C420 with P+0x10E set
    LONG vfRt228WaitUs;           // +0xCB8, S3b
    LONG vfRtDrainSave;           // +0xCBC, Save_WritePackage 0xAAC110 entries while a job runs (S3b drains there)
    LONG vfRtDrainBrd;            // +0xCC0, IC_World_BuildRoutingDataImpl 0x78AE40 entries while a job runs
    LONG vfRtDrainLotSave;        // +0xCC4, RoutingSystem_SaveLot 0x8361B0 entries while a job runs
    LONG vfRtDrainWaitUs;         // +0xCC8, S3b
    LONG vfRtDebugSkip;           // +0xCCC, RoutingDebug_FrameUpdate 0x818EA0 entries while a job runs (S3b skips them)
    LONG vfRtTeardownInJob;       // +0xCD0, 0x829410 / 0x82D140 / 0x8299E0 entries while a job runs (must be 0)
    LONG g11Plans;                // +0xCD4, sim-thread plans whose free window was closed by a routing touch
    LONG g11PlanUs;               // +0xCD8, their summed length (Route_PlanLocked on the sim thread, wait included in Mode 1)
    LONG g11MovableUs;            // +0xCDC, sum of min(plan length, gap to the sim thread's next routing touch)
    LONG g11GapUs;                // +0xCE0, sum of those gaps, each capped at 1 s
    LONG g11End[6];               // +0xCE4, what closed the window: +0x918 acquire, gate 1-12, gate 13-15, destroy, quadtree write, next plan
    LONG tgReady;                 // +0xCFC, 1 = PhysShape_Release 0x682760 and the [HeightCache] stubs carry the graveyard
    volatile LONG tgMode;         // +0xD00, switch: 0 = the game's release, 1 = park the swap's old shape until no ray runs
    LONG vfRtGraveParked;         // +0xD04, old cell shapes parked instead of released
    LONG vfRtGraveFreed;          // +0xD08, parked shapes released later (no ray in flight)
    LONG vfRtGraveSpins;          // +0xD0C, ring full: waits for the rays in flight to end before the drain
    LONG vfRtGraveRaces;          // +0xD10, swap releases that freed the shape while a ray was in flight (switch off)
    LONG vfRtGraveHeld;           // +0xD14, shapes parked now
    // v26 (0.11.0): H6 S3b + M1 (docs/60 §2.3/§2.4): gate enforced while an async job runs, closures, START limits
    LONG rgEnforce;               // +0xD18, 1 = [RouteGate] Enforce=1 and every closure site hooked: async START allowed
    LONG rgGateWaits;             // +0xD1C, outermost gate entries during an async job that took +0x918 (waited for the job)
    LONG rgGateWaitUs;            // +0xD20, their summed wait for the lock
    LONG rgGateWaitMaxUs;         // +0xD24
    LONG rgBorrows;               // +0xD28, +0x918 acquires and gate entries by the owner inside a callback it serves for the job
    LONG rgDropLocked;            // +0xD2C, gate frames left without returning that still held +0x918 (released late)
    LONG asyncRefusedM1[6];       // +0xD30, START refused by M1: no lock/world, save busy, world closing, pending lot updates,
                                  //         dirty lot index, gate frame or +0x918 hold open
    LONG vfRtDestroyInInvoke;     // +0xD48, destroys inside a callback served for the job (cannot wait; run as is)
    LONG vfRtClosingDrains;       // +0xD4C, travel / quit transition ICs (0xEC1180, 0xEC11B0) that set the closing flag
    LONG rtBusy;                  // +0xD50, save / BuildRoutingData / lot save calls open now (START refused while > 0)
    LONG rtClosing;               // +0xD54, 1 = a world transition was asked for, until the next world init
    // v27 (0.11.2): G11 closer attribution (vfG11CloseBy, docs/80-82): per closing key, closes and the movable time lost.
    // Key: a +0x918 acquire inside RoutingWorld_ObstacleQuery 0x82EDB0 / GoalFootprintGather 0x82F0B0 = the return
    // address of the call into them (the steering call site); any other +0x918 acquire = Lock_Acquire's return address;
    // 0x01000000 | gate site; 0x02000000 destroy; 0x03000000 | rtsites index (quadtree write); 0x04000000 next plan.
    LONG g11CloseKey[32];         // +0xD58
    LONG g11CloseN[32];           // +0xDD8
    LONG g11CloseLostUs[32];      // +0xE58, sum of (plan length - movable) for the closes with this key
    LONG g11CloseOther;           // +0xED8, closes whose key did not fit the 32 slots
    // v28 (0.11.3): async job queue (8 per-route slots, FIFO)
    LONG asyncSkipped;            // +0xEDC, queued jobs not run: the world changed since START (M1 limits, closing, save, new world)
    LONG asyncBackoffs;           // +0xEE0, job starts taken back because a guarded routing call was open elsewhere
    LONG asyncQueueFull;          // +0xEE4, STARTs refused: all 8 slots busy
    LONG asyncQueueMax;           // +0xEE8, most jobs queued or running at once
    // v29 (0.11.4): what the owner (sim) thread waits on while async jobs run (probe P0, docs/83). Key as g11CloseKey:
    // steering caller (outermost of RouteFollow_EditPath 0x85F5A0 / 0x82EDB0 / 0x82F0B0), else the lock wait's
    // Lock_Acquire return address; 0x01000000 | gate site; 0x02000000 | rtsites kind (destroy, save drain);
    // 0x05000000 a sync plan waiting for the running job (RunPlan).
    LONG blockKey[32];             // +0xEEC
    LONG blockN[32];               // +0xF6C
    LONG blockUs[32];             // +0xFEC
    LONG blockOther;              // +0x106C
    // v30 (0.12.0): design D, discourage footprints deferred while a job runs (discourage.cpp, docs/84 §2.4)
    LONG discReady;               // +0x1070, 1 = both detours in place
    volatile LONG discMode;       // +0x1074, switch: 0 = the game's calls, 1 = defer while a job runs
    LONG discDeferredAdd;         // +0x1078
    LONG discDeferredRemove;      // +0x107C
    LONG discApplied;             // +0x1080, records applied later in order
    LONG discDropped;             // +0x1084, records whose object was gone or whose world changed
    LONG discPendingMax;          // +0x1088
    LONG discDrainWaitUs;         // +0x108C, waits for a running job before a drain
    LONG discAgeMaxMs;            // +0x1090, oldest record at its drain
    LONG discForced;              // +0x1094, drains that waited for a job (record too old, queue full, save, transition)
    LONG discNested;              // +0x1098, records made during a drain (event handlers)
    LONG discOverflow;            // +0x109C, calls not deferred because the queue was full
    // v31 (0.13.0): design S2, read-only Locomotion_ObstacleCheck while a job runs (steerro.cpp, docs/84 §3.6)
    LONG steerReady;              // +0x10A0, 1 = 0x85FD10 and the 12 worker writer detours in place
    volatile LONG steerMode;      // +0x10A4, switch: 1 = answer from the read-only clone while a job runs
    volatile LONG steerShadow;    // +0x10A8, switch: 1 = with no job running, game call then clone, differences counted
    LONG steerCalls;              // +0x10AC, owner-thread calls with a flagged node while a switch is on
    LONG steerTaken;              // +0x10B0, read-only answers given during a job
    LONG steerFb[8];              // +0x10B4, gave up: state (lock owner, job callback, gate frame), QL busy, dirty lot index,
                                  //          lot not in table, deferred quadtree flag, polygon cache, shape (slot/overflow/null), tripwire
    LONG steerUs;                 // +0x10D4, time of the read-only answers
    LONG steerMaxUs;              // +0x10D8
    LONG steerShadowRuns;         // +0x10DC
    LONG steerShadowMismatch;     // +0x10E0, appended ids differ (must stay 0)
    LONG steerShadowOrder;        // +0x10E4, same ids in another order
    LONG steerShadowQueryMismatch; // +0x10E8, 0x82EDB0's footprints differ from the clone's point query (must stay 0)
    LONG steerQlHolds;            // +0x10EC, worker QL exclusive holds (outermost)
    LONG steerQlWaitUs;           // +0x10F0, worker waits for QL (a clone was reading)
    LONG steerSimTreeWrite;       // +0x10F4, sim-thread W1-W5 entries during a job (tree / lot table writes; existing race)
    LONG steerSimCacheWrite;      // +0x10F8, sim-thread W6 entries during a job (polygon cache rebuilds; existing race)
    LONG steerSimWriteInRo;       // +0x10FC, of those while the clone ran (answer dropped; must stay 0)
    LONG steerPinMoved;           // +0x1100, Q+0x48 / Q+0x4C moved during a clone walk (answer dropped)
    LONG steerSeqSkew;            // +0x1104, an exclusive QL hold during a shared one (must stay 0)
    LONG steerUntracked;          // +0x1108, worker W frames without a leave slot (clone switched off for good)
    // v31 too: fix C-A (docs/84 §4.4), sim-thread polygon level rebuilds take +0x918 first while a job runs (steerro.cpp)
    LONG caReady;                 // +0x110C, 1 = the W6a stub on 0x80E5B0 is in place with [FpRebuildLock] Enabled=1
    volatile LONG caMode;         // +0x1110, switch: 0 = count only (T-L5), 1 = take +0x918 before the rebuild
    LONG caSimInJob[4];           // +0x1114, T-L5: sim rebuilds of an invalid level during a job, by class 0xFEB848,
                                  //          0xFEB970, 0xFEBD10, 0xFEBE38 (the classes whose rebuild reaches the lot table)
    LONG caWaits;                 // +0x1124, of those that took +0x918 (waited for the job)
    LONG caWaitUs;                // +0x1128
    LONG caQlHeld;                // +0x112C, would have waited while this thread held QL (not taken; must stay 0)
    // v32: design L1 (read-only LotRouting_FrontDoorPortal 0x631800) and probe P1b (docs/84 §4.6), lotfd.cpp
    LONG ltReady;                 // +0x1130, 1 = the L1 detour on 0x631800 is in place ([LotFrontDoorRO] Enabled=1)
    volatile LONG ltMode;         // +0x1134, switch: 1 = while a job runs, answer from the read-only clone
    volatile LONG ltShadow;       // +0x1138, switch: 1 = with no job running, the game's call then the clone, compared
    LONG ltCalls;                 // +0x113C, owner-thread entries with a switch on
    LONG ltTaken;                 // +0x1140, read-only answers during a job (no +0x918 wait)
    LONG ltFb[6];                 // +0x1144, fall-backs to the game's call: state, nested QL, no world manager / active lot,
                                  //          QL busy, shape (table, portal vtable or count), QL frames lost (broken)
    LONG ltUs;                    // +0x115C
    LONG ltMaxUs;                 // +0x1160
    LONG ltShadowRuns;            // +0x1164
    LONG ltShadowMismatch;        // +0x1168, T-L4: EDX:EAX or *out differ from the game's (must stay 0)
    LONG ltQlHeldBlock;           // +0x116C, T-L2: Lock_Acquire, host or managed call while this thread holds QL shared (= 0)
    LONG portalRef[8];            // +0x1170, T-L3: portal AddRef (0..3) / Release (4..7) through vtable 0xFED048 during a job,
                                  //          by thread: sim, worker inside a QL writer stub, worker outside, other
    LONG p1bReady;                // +0x1190, 1 = [ProbeP1b] Enabled=1
    LONG p1bKey[32];              // +0x1194, gate waits by second frame: (first-key index 1-5) << 24 | caller's return address
                                  //          (0xFFFFFF = outside the exe); 1 0x62F782, 2 0x62FB74, 3 0x631814, 4 0x631F40 (gate
                                  //          12), 5 0x62EF7A (gate 0)
    LONG p1bN[32];                // +0x1214
    LONG p1bUs[32];               // +0x1294
    LONG p1bOther;                // +0x1314, table full
    LONG p1bNoFrame;              // +0x1318, a known first key whose second frame could not be read (gate 0: bad ebp)
    // v33 (0.15.0): S2 tripwires T4 and T-S7 (steerro.cpp, docs/84 §3.6, §7.3); counted while a job runs or a clone is open
    LONG steerTwStubs;            // +0x131C, tripwire stubs in place (11 with [SteerRO] Enabled=1)
    LONG steerT4Bare;             // +0x1320, T4: node / map entry helper entries outside QL on a thread other than the sim (= 0)
    LONG steerS7Foreign;          // +0x1324, T-S7: fp+0x20 / lock-key vector writers or LotGraph_Reset on a thread other than
                                  //          the sim (= 0)
    LONG steerS7Sim;              // +0x1328, T-S7 writer entries on the sim thread during a job (by design, informational)
    LONG steerTripDiscard;        // +0x132C, clone answers dropped because such a writer ran beside it (also in steerFbTrip)
    LONG steerTwSite[11];         // +0x1330, those hits by site: 0x6653A0, 0x666010, 0x61ED80, 0x61ED10 (T4); 0x807110,
                                  //          0x8077A0, 0x62E420, 0x947FA0, 0x948050, 0x9479D0, 0x947620 (T-S7)
    // v34 (0.16.0): fix D-X of design D (discourage.cpp, docs/84 §7.2): destroy-gate apply, dead-id sweep, pointer check
    volatile LONG discDestroyMode;   // +0x135C, switch: 1 = a destroy first applies its object's queued Remove records
    volatile LONG discSweepMode;     // +0x1360, switch: 1 = a Remove record whose object is gone sweeps its id's footprints
    LONG discDropAdd;                // +0x1364, Add records dropped: object gone, or its destroy came first
    LONG discDropRemove;             // +0x1368, Remove records whose object was gone at apply (T-D5: = discDeadSweep)
    LONG discDestroyApplied;         // +0x136C, Remove records applied by the destroy gate
    LONG discDestroyInInvoke;        // +0x1370, destroys with records queued inside a job callback (cannot apply; skipped)
    LONG discDeadSweep;              // +0x1374, dead-id sweeps run (LotRouting_RemoveObjectFootprints 0x635700, all lots)
    LONG discDeadHit;                // +0x1378, sweeps that removed a quadtree footprint (a zombie vanilla would not leave)
    LONG discDeadKeyMap;             // +0x137C, dead ids still in the lock-key map world+0x8B8 (counted, not touched)
    LONG discIdMismatch;             // +0x1380, the id names another object than the recorded one (dropped, no sweep; = 0)
    // v35 (0.17.0): why Mode 1 route plans ran synchronously on the sim thread (IcPlanHook, docs/60 §2.3); observe only
    LONG rfbLogMs;                   // +0x1384, [RouteAsync] LogFallbackMs: log line per sync plan >= this (0 = all of v35 off)
    LONG rfbMs[3];                   // +0x1388, summed ms of sync plans >= 50 ms by reason group: refused (NoYield, M1*),
                                     //          queue (Running, QueueFull, InvokeHook, Unstarted), other (NoStart, Skipped,
                                     //          Uncollected)
    LONG rfbN[3];                    // +0x1394, their count
    LONG rfbMax;                     // +0x13A0, longest one: ms << 8 | reason code (RFB_* in ts3native.cpp)
    // v36 (0.18.0): S2 tripwire stubs armed only during jobs, per-call timers (steerro.cpp, docs/84 §3.6). Timers: `*Kt` =
    // rdtsc ticks / 1024 summed, `*MaxT` = largest single call in ticks; us = ticks / s2TscPerUs
    LONG s2TscPerUs;                 // +0x13A4, rdtsc ticks per microsecond (QPC-calibrated at job arms; 0 until 1 s after the first)
    LONG s2ArmJobs;                  // +0x13A8, async jobs that armed the tripwire stubs
    LONG s2JobEndDrop;               // +0x13AC, Mode 1 clone answers dropped: the stubs were disarmed (job ended) at or during the clone
    LONG s2EntryKt;                  // +0x13B0, our own time per S2 call (job: checks + clone, before the game's call on a fall-back;
    LONG s2EntryMaxT;                // +0x13B4  shadow: the compare after the game's call); calls = steerCalls
    LONG s2TwCalls;                  // +0x13B8, armed tripwire stub entries (TwEnter, all threads)
    LONG s2TwKt;                     // +0x13BC
    LONG s2TwMaxT;                   // +0x13C0
    LONG s2QlCalls;                  // +0x13C4, W1-W6 stub entries while armed (QlEnter, incl. the worker's QL wait)
    LONG s2QlKt;                     // +0x13C8
    LONG s2QlMaxT;                   // +0x13CC
    LONG s2ShadowKt;                 // +0x13D0, shadow compares (clone + obstacle query after the game's call); runs = steerShadowRuns
    LONG s2ShadowMaxT;               // +0x13D4
    LONG s2TryCalls;                 // +0x13D8, QL shared try-locks in the clone (busy = steerFbQlBusy)
    LONG s2TryKt;                    // +0x13DC
    LONG s2TryMaxT;                  // +0x13E0
    // v37 (0.19.0): design H2, no queued route job starts during the locomotion pass (locowin.cpp, docs/86 §5)
    LONG g0wReady;                   // +0x13E4, 1 = the Locomotion_Update call 0xD81C5C goes through LocoWinThunk
    LONG g0wMode;                    // +0x13E8, switch (--ab locowin): 0 = count only, 1 = hold job starts for the pass
    LONG g0wPasses;                  // +0x13EC, passes (outermost thunk calls)
    LONG g0wPassUs;                  // +0x13F0, their summed length
    LONG g0wPassMaxUs;               // +0x13F4
    LONG g0wRunningAtEntry;          // +0x13F8, passes that began while a job ran (that job may finish inside)
    LONG g0wJobStartInPass;          // +0x13FC, jobs that started while the sim was inside (Mode 1: only after a drop/watchdog)
    LONG g0wJobsHeld;                // +0x1400, job starts the window held back (worker)
    LONG g0wHoldUs;                  // +0x1404, worker start delay those holds caused (includes g_rgOffSimOpen time meanwhile)
    LONG g0wHoldMaxUs;               // +0x1408
    LONG g0wDrop[2];                 // +0x140C, windows dropped before a sim wait on a job: WaitJobDone, sync worker plan (RunPlan)
    LONG g0wWatchdog;                // +0x1414, holds the worker gave up after 250 ms (a wait site we missed; logged once)
    LONG g0wGateWaits;               // +0x1418, gate waits on the sim thread inside the pass (all keys)
    LONG g0wGate0Waits;              // +0x141C, of those keyed 0x62EF7A (gate 0, the transform wrapper)
    LONG g0wGate0Us;                 // +0x1420, their summed wait
    // v38 (0.20.0): probe P10 and design G10-C (p10.cpp, docs/87 §5-§6), observe only except G10-C. IC index: 0 Plan
    // 0x765160, 1 Replan 0x765490, 2 PlanFromPoint 0x765240, 3 ReplanFromPosition 0x765550 (sim thread only)
    LONG p10Ready;                   // +0x1424, bit 0 = P10 on the four route ICs, bit 1 = G10-C (gate site 10 on 0x7606E0)
    LONG p10Entries[4];              // +0x1428, IC entries (Plan: also the ones that collect an async result)
    LONG p10PlanUs[4];               // +0x1438, Route_PlanLocked time inside the IC
    LONG p10EarlyRet[4];             // +0x1448, entries that reached gate site 10 but called no Route_PlanLocked
    LONG p10CanYield[3];             // +0x1458, Replan / PlanFromPoint / ReplanFromPosition entries where CanYield() held
    // v39 (0.21.0): S2 SR_FB_SHAPE fallbacks per return site and the buffer sizes the clone needed (steerro.cpp, docs/84 §3.6)
    LONG s2Shape[11];                // +0x1464, per site (SRS_*): objects > kSrCap, NULL footprint, unknown slot, local > kSrLocal,
                                     // depth > kSrDepth, no active lot, empty lot table, ids > kSrIds, no planner / wm world,
                                     // nodes > kSrNodes, nodes > kSrNodes (first-node copy)
    LONG s2MaxObjs;                  // +0x1490, most footprints one gather needed (past kSrCap: not deduplicated, an upper bound)
    LONG s2MaxLocal;                 // +0x1494, most class-2 footprints one cell kept (with a type 0x1F entry)
    LONG s2MaxNodes;                 // +0x1498, most flagged path nodes one call needed (nodes[] entries incl. the first-node copy)
    LONG s2MaxIds;                   // +0x149C, most ids one EditPath node would append (past kSrIds: an upper bound)
    LONG s2MaxDepth;                 // +0x14A0, deepest CollectChildren recursion
    // v40 (0.22.0): design A10, the Replan-family route ICs as async jobs (a10.cpp, docs/87 §5). Arrays by job kind - 1:
    // Replan 0x765490, PlanFromPoint 0x765240, ReplanFromPosition 0x765550 (Plan, kind 0, stays in the v5 async* totals,
    // which now count every kind)
    LONG arReady;                    // +0x14A4, bit kind-1 = that IC's collect detour is in place ([RouteAsync] Kinds)
    volatile LONG arMode;            // +0x14A8, switch (--ab asyncreplan): 0 = shadow (checks run, START refused), 1 = async
    LONG arStarted[3];               // +0x14AC, jobs queued
    LONG arCollected[3];             // +0x14B8, results handed to the IC call
    LONG arRefused[3];               // +0x14C4, STARTs refused (any reason, incl. arActive; not the Mode 0 ones)
    LONG arDropped[3];               // +0x14D0, jobs thrown away (uncollected, cancelled, replaced, kind mismatch)
    LONG arShadow[3];                // +0x14DC, Mode 0: STARTs that passed every check (would-be jobs)
    LONG arActive[3];                // +0x14E8, refused because the route is being followed (route+0x60, Route_IsActive)
    LONG arJobUs[3];                 // +0x14F4, worker time of the collected jobs (sim-thread plan time saved)
    LONG arSyncUs[3];                // +0x1500, sim-thread time of the IC calls that planned there (not collected)
    LONG arSyncN[3];                 // +0x150C, their count
    LONG arMismatch;                 // +0x1518, an IC met its route's job of another kind or start point (dropped, planned sync)
    LONG arOrphan;                   // +0x151C, an IC armed by READY found no job (planned sync)
    LONG arReserved;                 // +0x1520, unused (v40 is 32 fields; v41 starts at 0x1524)
    // v41 (0.22.1): read-only IC_Route_IsPointObstructedBySim, the gate-0 fix (obstructro.cpp, docs/86 §10)
    LONG orReady;                    // +0x1524
    LONG orMode;                     // +0x1528, switch: 1 = while a job runs, answer from the read-only clone (--ab obstructedro)
    LONG orShadow;                   // +0x152C, switch: with no job running, the game's call, then the clone, compared
    LONG orCalls;                    // +0x1530, owner-thread entries with a switch on
    LONG orTaken;                    // +0x1534, read-only answers (Mode 1)
    LONG orFb[6];                    // +0x1538, fell back to the game's call: state, QL busy, lot (dirty index / not in the
                                     // table / deferred flush), polygon cache, shape (slot, buffer, planner), tripwire
    LONG orUs;                       // +0x1550, time of the read-only answers
    LONG orMaxUs;                    // +0x1554
    LONG orShadowRuns;               // +0x1558
    LONG orShadowMismatch;           // +0x155C, must stay 0
    LONG orHits;                     // +0x1560, read-only answers that named a Sim
    // v42 (0.22.2): design SH, QL-shared hold over the slot position helper 0x76F0A0 (slotro.cpp, docs/90)
    LONG slReady;                    // +0x1564, 1 = the detour on 0x76F0A0 and the interlocked portal refcounts are in place
    volatile LONG slMode;            // +0x1568, switch: 1 = while a job runs, run the call under QL shared and pass gates 12 / 0 hits
    volatile LONG slShadow;          // +0x156C, switch: 1 = with no job running, the game's call, then the call under a dry hold, compared
    LONG slCalls;                    // +0x1570, owner-thread entries with a switch on (age/gender flags and object set)
    LONG slTaken;                    // +0x1574, holds during a job that lasted to the call's return (no +0x918 wait in it)
    LONG slUs;                       // +0x1578, time in those calls
    LONG slMaxUs;                    // +0x157C
    LONG slPass12;                   // +0x1580, gate-12 entries passed inside a hold (LotTable_Contains under QL shared)
    LONG slPass0;                    // +0x1584, gate-0 entries passed inside a hold (GetOrInsert, a hit by our own find)
    LONG slDrop[6];                  // +0x1588, holds dropped (QL released, then today's wait): other gate site, gate-0 miss,
                                     // portal class / count, Lock_Acquire, managed or host call, lot table shape (job and dry)
    LONG slFb[4];                    // +0x15A0, no hold (the game's call): state, nested QL, lost writer frame, QL busy
    LONG slShadowRuns;               // +0x15B0
    LONG slShadowMismatch;           // +0x15B4, return value or point differ from the game's call (must stay 0)
    LONG slShadowDrop;               // +0x15B8, dry holds that dropped (reasons in slDrop)
    LONG slRefSwap;                  // +0x15BC, 1 = portal vtable 0xFED048 AddRef/Release are interlocked (ours, or P1b's thunks to ours)
    LONG slRelRetry;                 // +0x15C0, interlocked Release compare-exchange retries (another thread changed the count)
    // v43 (0.23.0): [ProbeSnap] indirect call/jmp target sampler over the routing closure (vcall.cpp, docs/94 §7)
    LONG vcReady;                    // +0x15C4, 1 = vectored handler in place and the sites armed (DelaySec after start-up)
    LONG vcSites;                    // +0x15C8, sites in the table that decoded
    LONG vcSkipped;                  // +0x15CC, sites rejected (decode, order) or dropped (bytes rewritten by another hook)
    LONG vcHits;                     // +0x15D0, int3 hits handled (each one is an exception dispatch: the probe's cost)
    LONG vcPairs;                    // +0x15D4, distinct (site, target) pairs seen
    LONG vcOutside;                  // +0x15D8, of those, targets outside TS3W.exe (other DLLs, mods, heap code)
    LONG vcOverflow;                 // +0x15DC, hits whose target did not fit (more than 8 per site)
    LONG vcFaults;                   // +0x15E0, unreadable or null targets (site disarmed for good, the original ran)
    LONG vcWindows;                  // +0x15E4, arming windows started
    LONG vcArmed;                    // +0x15E8, sites armed now
    // v44 (0.24.0): [ProbeValues] routing value probe, observe only (values.cpp, docs/108). Load-time values are float or raw
    // bits of the latest world (0x7FC00000 = object missing); everything else counts from start-up
    LONG pvReady;                    // +0x15EC, bit 0 = sites hooked (pvHooks), bit 1 = poll thread running
    LONG pvHooks;                    // +0x15F0, sites hooked (own stubs, or counted inside a route gate stub)
    LONG pvSkipped;                  // +0x15F4, sites not hooked (bytes differ, no owner stub)
    LONG pvWorlds;                   // +0x15F8, world snapshots (load, travel)
    LONG pvTune[13];                 // +0x15FC, W+0x18 +0x24 threshold, +0x28..+0x3C SHORT, +0x4C..+0x60 LONG (keep, trespass, sidewalk, riding, street, water-lot)
    LONG pvWm[4];                    // +0x1630, WM+0xB4 / +0xB8 world origin x, z; +0xBC / +0xC0 max x, z
    LONG pvSeaY;                     // +0x1640, [WM+0x60]+0x14 sea height
    LONG pvSeaChanges;               // +0x1644, polls that saw a new sea height in the same world
    LONG pvWmDirty;                  // +0x1648, WM+0x1B4 (latest)
    LONG pvWorld983;                 // +0x164C, routing world +0x983 byte (latest)
    LONG pvG[5];                     // +0x1650, G = world+0x8C: built byte, cells, water cells, bodies, blockedNodeMap size (max)
    LONG pvR[5];                     // +0x1664, R = world+0x54: checksum (flags & 0x7F), checksum (flags & 0x60), waypoints, blocked now, checksum changes
    LONG pvGuard[3];                 // +0x1678, dwords 0x11CF010, 0x11CF02C, 0x11CF030 (docs/101 §2.2 #12, #14)
    LONG pvGBuild[2];                // +0x1684, G builds 0x6734A0 by call site 0x82A098 (message path), 0x834A82 (BuildWorldRoutingData)
    LONG pvGBuildUs;                 // +0x168C
    LONG pvGBuildMaxUs;              // +0x1690
    LONG pvGBuildThr[3];             // +0x1694, builds on the main, sim, other thread
    LONG pvGMsg[3];                  // +0x16A0, 0x82A030 calls from 0x8352F1, 0x835390, 0x8358B5
    LONG pvRebuild[3];               // +0x16AC, 0x8349C0 calls from 0xC6BE53, 0xC6CD02, 0x835A27
    LONG pvDirtyPosts;               // +0x16B8, WorldManager_PostWaterGridDirty 0xC61FE0 entries
    LONG pvJobs;                     // +0x16BC, RoutingJob_Execute calls (plans that search)
    LONG pvTies;                     // +0x16C0, RNG goal ties (0x81BC65)
    LONG pvAStar[3];                 // +0x16C4, world A* entries by family: 0 no Route_PlanCommon frame, 1 Plan/Replan, 2 point ICs
    LONG pvAStarBit[3][10];          // +0x16D0, opts flag bits 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200 at entry
    LONG pvAStarRef14[3];            // +0x1748, opts+0x14 non-zero
    LONG pvAStarRef44[3];            // +0x1754, opts+0x44 non-zero
    LONG pvLotPlan[3];               // +0x1760, LotPlan_Inner entries by family
    LONG pvLotPlanBit[3][11];        // +0x176C, caller flag bits 1, 2, 4, 8, 0x10, 0x20, 0x100, 0x80000, 0x100000, 0x200000, 0x2000000
    LONG pvLotPlanObst[3];           // +0x17F0, obstacle mode (adds 0xF)
    LONG pvStatus[2][20];            // +0x17FC, mType at Route_PlanCommon return: Plan/Replan, point ICs; [19] = 19 or more
    LONG pvIpAlloc;                  // +0x189C, interest point record allocations (0x821B3D)
    LONG pvIpCalls;                  // +0x18A0, IC_Route_GetNumInterestPoints calls
    LONG pvIpNonZero;                // +0x18A4, of those, non-zero answers
    LONG pvPortal[5][5];             // +0x18A8, Portal_LockState state 0-3 / other by path: other, goal step, lot search, lot A*, in-room
    LONG pvQuery[2][5];              // +0x190C, World_FirePortalLockedQuery answer 0-3 / other by initialLocked 0, 1
    LONG pvQueryInPlan;              // +0x1934, queries (event 59) inside RoutingJob_Execute
    LONG pvH9[2];                    // +0x1938, H9 quadtree rewrites: remove 0x63257E, insert 0x632595
    LONG pvH9InPlan;                 // +0x1940, of those, inside RoutingJob_Execute
    LONG pvH9Plans;                  // +0x1944, plans with at least one rewrite
    LONG pvH9Max;                    // +0x1948, most rewrites in one plan
    LONG pvPart[2];                  // +0x194C, partition grid writers 0xC08C30, 0xC094E0
    LONG pvPartOffSim[2];            // +0x1954, of those, off the sim thread
    LONG pvLazy[8][3];               // +0x195C, calls / inside a plan / off the sim thread: LotTable_Insert, LotGraph_Build, RoomGraph_FirstUse, ApplyLotUpdate, FlushDeferred with work, Footprint_EnsureLevelPolygons, 0x670510, 0x670750
    LONG pvBlk[2];                   // +0x19BC, blockedNodeMap writers 0x66FB40, 0x66F6D0
    LONG pvHighCost;                 // +0x19C4, high-cost polygon stamps 0x6733C0
    LONG pvCanSee;                   // +0x19C8, World_CanSeeObject from radial goal validation (0x75AC22)
    LONG pvCanSeeRaise;              // +0x19CC, Lot_SetDisplayLevel calls inside it (0x789EB5, 0x78A218)
    LONG pvEv68;                     // +0x19D0, World event 68 fires from Lot_SetDisplayLevel (0xAC0763)
    LONG pvEv68InCanSee;             // +0x19D4, of those, inside World_CanSeeObject from 0x75AC22
    LONG pvFrameFull;                // +0x19D8, leave frames not taken (16 open on the thread)
    // v45 (0.24.1): [ProbeValues] fixes (docs/112, docs/108 §8). Thread classes here: sim, route worker (g_workerTid), other
    LONG pvRaiseIn[3];               // +0x19DC, Lot_SetDisplayLevel (0x789EB5, 0x78A218) inside an open CanSee frame from 0x75AC22
    LONG pvRoomBuild[3];             // +0x19E8, RoomGraph_FirstUse entries that build (lot graph +0x8C byte 0 or room record byte 0)
    LONG pvLevelBuild[3];            // +0x19F4, Footprint_EnsureLevelPolygons entries that rebuild (level valid byte 0)
    LONG pvRestore[3];               // +0x1A00, RoutingWorld_RestoreLotBlockedBits 0x82A190: calls / inside a plan / off the sim thread
    LONG pvRBlk[3];                  // +0x1A0C, R blocked-bit hash at the baseline, now; 60 s checks that saw it change
    LONG pvRRebase;                  // +0x1A18, R baselines retaken after the first G build of a world (docs/112 §3.1)
    // v46 (0.25.0): [RouteSnap] Count=1, snapshot change counters P1 (routesnap.cpp, docs/94 §5, §10). Thread classes: sim,
    // route worker, other. Leaf rows (SL_*): W1, W2, W3, W4, W5, W6 (8 sites), 0x8321F0, 0x831BA0, 0x82A0C0, 0x635700,
    // 0x829D50, 0x82B590, 0x82A190, 0x636BF0, 0x62D9F0, 0x6733C0, 0x670510, G build (5 call sites), blend reload 0xC62AC0.
    // Sources (SRC_*): none, IC frame, World_FireEvent frame, object event (head/tail nodes), Footprint_SetEnabled frame.
    LONG snReady;                    // +0x1A1C, bit 0 leaf sites, bit 1 World_FireEvent, bit 2 IC slots, bit 3 object nodes
    LONG snHooks;                    // +0x1A20, sites with our own stub
    LONG snShared;                   // +0x1A24, sites counted inside another stub of ours ([SteerRO], [RouteGate], [ProbeValues])
    LONG snSkipped;                  // +0x1A28, sites whose bytes differ (log: SKIP routesnap)
    LONG snIcSlots;                  // +0x1A2C, IC binding slots swapped (of 39)
    LONG snLeaf[19][3];              // +0x1A30, leaf hits by thread class (blend: reload pass only)
    LONG snSrc[19][5];               // +0x1B14, sim-thread leaf hits by source (G build and blend rows stay 0)
    LONG snUnattributed;             // +0x1C90, sim-thread leaf hits with no source open (docs/94 §4.2; G1)
    LONG snUaKey[16];                // +0x1C94, their callers (return address of the leaf)
    LONG snUaN[16];                  // +0x1CD4
    LONG snUaOther;                  // +0x1D14, callers past the 16 keys
    LONG snWev[96];                  // +0x1D18, World_FireEvent calls by event id (95 = 95 and above), any thread
    LONG snWevOffSim;                // +0x1E98, of those, off the sim thread
    LONG snObj[8][3];                // +0x1E9C, object events 4, 6, 9, 0x0A, 0x0B, 0x0E, 0x18, 0x21 by thread class (head node)
    LONG snObjNodes;                 // +0x1EFC, nodes registered (16 per object service)
    LONG snObjRegFails;              // +0x1F00, ObjectEvents_Register returned 0
    LONG snObjOpenAtIc;              // +0x1F04, IC entries that found an object chain still open (a handler stopped it)
    LONG snIc[39];                   // +0x1F08, IC calls per slot (kSnIcDef order; 35-38 = route STARTs)
    LONG snIcWrites[39];             // +0x1FA4, sim leaf hits attributed to that IC frame
    LONG snIcOffSim;                 // +0x2040, IC calls off the sim thread (not framed)
    LONG snGSite[5];                 // +0x2044, G builds per call site 0x82A098, 0x834A82, 0x8352F1, 0x835390, 0x8358B5
    LONG snStarts;                   // +0x2058, route STARTs (sim entries of the four route ICs)
    LONG snStartLots[8];             // +0x205C, distinct known lots marked since the previous START: 0, 1, 2, 3, 4, 5, 6-10, >10
    LONG snStartWrites[8];           // +0x207C, leaf hits since the previous START: 0, 1-2, 3-5, 6-10, 11-20, 21-50, 51-100, >100
    LONG snStartUnknown;             // +0x209C, STARTs whose window had a leaf hit with no lot argument
    LONG snSetFull;                  // +0x20A0, STARTs whose lot set overflowed (16)
    LONG snFpEn[3];                  // +0x20A4, Footprint_SetEnabled 0x947590 calls by thread class
    LONG snSeq;                      // +0x20B0, sequence of G builds and world events 7 (I-94-18)
    LONG snSeqGBuild;                // +0x20B4, sequence number of the last G build
    LONG snSeqEv7;                   // +0x20B8, sequence number of the last world event 7
    LONG snFrameFull;                // +0x20BC, source frames not taken (16 open on the thread)
    // v47 (0.26.0): [RouteSnap] Enabled=1 Mode=0, snapshot H images and audit, no search (snap.cpp, docs/94 P3). Inputs
    // (SP_*): R, Gcore, Gtiles, bnm, grid, blend, tune, lots.
    LONG spReady;                    // +0x20C0, bit 0 images on, bit 1 some P1 sites shared with another stub, bit 2 door channel
    volatile LONG spMode;            // +0x20C4, switch: 0 = audit only (P3)
    LONG spCuts;                     // +0x20C8, versions published (cuts at a route START under +0x918)
    LONG spCutSkip[3];               // +0x20CC, cuts not taken: async job running or queued, no world / planner, Lock_Acquire failed
    LONG spCutUs;                    // +0x20D8, sim time in cuts (microseconds, summed)
    LONG spCutMaxUs;                 // +0x20DC
    LONG spEpochs;                   // +0x20E0, world epochs (world events 5, 7, 9, 0x0A, or a new routing world)
    LONG spFullBuilds;               // +0x20E4, cuts that rebuilt every image
    LONG spVersionsLive;             // +0x20E8
    LONG spImgLive;                  // +0x20EC, images alive (shared between versions)
    LONG spKB;                       // +0x20F0, image memory now (KB)
    LONG spKBMax;                    // +0x20F4
    LONG spCopies[8];                // +0x20F8, images rebuilt per input (Gtiles: tiles, lots: lot images)
    LONG spAudits[8];                // +0x2118, images audited per input
    LONG spMis[8];                   // +0x2138, audit mismatches per input (gate G3: all 0)
    LONG spAuditUs;                  // +0x2158, sim time in audits (microseconds, summed)
    LONG spAuditMaxUs;               // +0x215C
    LONG spLots;                     // +0x2160, lot images in the current version
    LONG spTiles;                    // +0x2164, high-cost tiles in the current version
    LONG spDirtyOver;                // +0x2168, dirty-set overflows (that input then rebuilt whole)
    LONG spKeyMapFull;               // +0x216C, high-cost polygon keys not recorded (their remove marks every tile)
    LONG spPendingAtCut;             // +0x2170, cuts with pending lot updates P+0x10E set (applied at the cut from P4 on)
    LONG spDeferredAtCut;            // +0x2174, cuts with a deferred quadtree flush queued on some lot (E17)
    LONG spFaults;                   // +0x2178, image builds stopped by a fault or a shape cap (old image kept)
    LONG spCapOff;                   // +0x217C, 1 = switched off at CapMB
    LONG spDoor[6];                  // +0x2180, door channel: messages, lots filled, doors, invalidations, protocol errors, spare
    // v48 (0.27.0): [RouteSnap] Count=1 P1b (routesnap.cpp, docs/94 §11.6-§11.7, §15): attribution frames, design B records.
    // P1b sources (SRC_* 5..11): platform refresh 0xB57F20, user tool TryDrop 0xD442B0, tree init 0x948F30 (call 0x95EFF2),
    // lazy terrain 0x834B80, off-screen road plan 0xC7BF70, deferred flush 0x666A40, design D drain (discourage.cpp).
    // From v48 the v46 caller table snUaKey/snUaN/snUaOther stays 0 (replaced by snUa2*), and snUnattributed leaves out
    // the P1b sources and W6 cache fills. Records (design B, one per logical change, route worker excluded): W1, W2, W3,
    // W5, grid add, grid remove, high-cost add, high-cost remove, R bit 7, lock-key ICs 0-3, portal field ICs 12-15,
    // suppressed inside a rebase IC frame (portal 4-11, build/buy exit 22, world 29-33).
    LONG snSrc2[19][7];              // +0x2198, sim leaf hits by P1b source (row SL_*, column source - 5)
    LONG snCacheFill;                // +0x23AC, W6 sim hits with no source open (lazy polygon cache fills, not input changes)
    LONG snFrameCalls[6];            // +0x23B0, sim entries of the six P1b frame sites (shared sites count, frame only when ours)
    LONG snRec[12];                  // +0x23C8, design B records by kind (11 = suppressed inside a rebase IC frame)
    LONG snRecSec[8];                // +0x23F8, 1-s buckets by records: 0, 1-250, 251-500, 501-1000, 1001-2000, 2001-5000, 5001-10000, >10000
    LONG snRecMaxSec;                // +0x2418, most records in one second since start (loads included)
    LONG snRecUs;                    // +0x241C, time in the record bookkeeping (lock, bucket), microseconds summed
    LONG snCloneUs;                  // +0x2420, sim W1: footprint clone-equivalent copy (0x44 + level 0 cache + polygons), us summed
    LONG snCloneN;                   // +0x2424
    LONG snCloneBytesK;              // +0x2428, bytes copied / 1024
    LONG snCloneMaxBytes;            // +0x242C
    LONG snCloneFault;               // +0x2430
    LONG snLotCopyN;                 // +0x2434, timed canonical lot copies (lot of a sim LotGraph_Build, copied at the next START)
    LONG snLotCopyUs[8];             // +0x2438, by time: <=100, <=250, <=500, <=1000, <=2000, <=5000, <=10000, >10000 us
    LONG snLotCopyMaxUs;             // +0x2458
    LONG snLotCopyMaxKB;             // +0x245C
    LONG snLotCopyFault;             // +0x2460, faults or lot gone from the table
    LONG snUa2Key[64];               // +0x2464, unattributed callers: leaf << 24 | return address (0xFFFFFF outside the exe)
    LONG snUa2Next[64];              // +0x2564, next frame: first call-preceded exe address above the return slot (heuristic)
    LONG snUa2N[64];                 // +0x2664
    LONG snUa2Other;                 // +0x2764, keys past 64
    // v49 (0.28.0): [RouteSnap] Lots=replay, per-lot footprints and quadtrees by ordered leaf replay into a clone, audit
    // only (replay.cpp, docs/94 §11 design B, §16)
    LONG rpReady;                    // +0x2768, bit 0 replay on, bit 1 the consumer holds a clone, bit 2 consumer thread
    LONG rpEpochs;                   // +0x276C, clones built (world event 7, epoch, overflow, divergence, fault)
    LONG rpEpochFails;               // +0x2770, clone builds that failed (fault, unknown footprint class, shape)
    LONG rpEpochUs;                  // +0x2774, sim time in clone builds (microseconds, summed)
    LONG rpEpochMaxUs;               // +0x2778
    LONG rpLots;                     // +0x277C, lots in the consumer's clone
    LONG rpFps;                      // +0x2780, clone footprint blocks alive (records in flight included)
    LONG rpKB;                       // +0x2784, our replay memory now (clone, footprint copies, payloads; KB)
    LONG rpKBMax;                    // +0x2788
    LONG rpRec[8];                   // +0x278C, records appended by type (RP_*: 1 INS, 2 REM, 3 STALE, 4 LOTS, 5 EPOCH, 6 AUDIT, 7 SYNC)
    LONG rpDrop;                     // +0x27AC, records dropped on a full ring (clone rebuilt)
    LONG rpRingMax;                  // +0x27B0, ring depth at a cut, max
    LONG rpCopyUs;                   // +0x27B4, time in footprint copies (INS records and builds, all threads, summed)
    LONG rpCopyFail;                 // +0x27B8, footprint copies refused (unknown class, invalid level after the forced build, shape caps, fault)
    LONG rpUnknownVt;                // +0x27BC, last footprint vtable with no class row
    LONG rpApplied[8];               // +0x27C0, records applied by type
    LONG rpSkip[4];                  // +0x27E0, INS/REM not applied: Q not in the clone, lot stale (rebase pending), no footprint copy, no clone
    LONG rpApplyUs;                  // +0x27F0, consumer time applying under +0x918 (summed)
    LONG rpApplyMaxUs;               // +0x27F4, longest hold
    LONG rpLockWaitUs;               // +0x27F8, consumer time waiting for +0x918
    LONG rpLag;                      // +0x27FC, records behind the head after a drain, max
    LONG rpRebaseLots;               // +0x2800, lots rebased or removed at cuts
    LONG rpRebaseUs;                 // +0x2804, sim time in rebase copies (summed)
    LONG rpRebaseMaxUs;              // +0x2808
    LONG rpSyncs;                    // +0x280C, SYNC checks (map buckets, entries, lot count)
    LONG rpDiverge;                  // +0x2810, SYNC differences (clone rebuilt at the next cut)
    LONG rpFaults;                   // +0x2814, faults while building, applying or freeing
    LONG rpAudits;                   // +0x2818, lot audits (clone lot hash vs the live lot at the same record)
    LONG rpMis;                      // +0x281C, lot audit mismatches (gate: 0)
    LONG rpAuditUs;                  // +0x2820, sim time hashing live lots (summed)
    LONG rpAuditMaxUs;               // +0x2824
    LONG rpAuditWorkerUs;            // +0x2828, consumer time hashing clone lots (summed)
    LONG rpGcFreed;                  // +0x282C, clone footprint blocks freed when no clone FME held them
    LONG rpOrphanFme;                // +0x2830, clone FMEs met only in a tree leaf (not in the map; kept until the next epoch)
    LONG rpCapOff;                   // +0x2834, 1 = switched off at ReplayCapMB
    LONG rpPrebuilt;                 // +0x2838, Prebuild=1: lot graphs built at world event 7 (RoomGraph_FirstUse on each unbuilt lot)
    LONG rpPrebuildFail;             // +0x283C, lots whose forced first use returned 0 or faulted
    LONG rpPrebuildUs;               // +0x2840, sim time in the prebuild (summed over loads)
    LONG rpPrebuildKB;               // +0x2844, process private bytes added by the last prebuild (KB; the game's lot graphs)
    LONG rpExclLots;                 // +0x2848, lots the last clone build left out (a footprint the copy refuses; 0.29.0)
    LONG rpUnkClsVt[4];              // +0x284C, unknown footprint vtables met by a copy (first 4)
    LONG rpUnkClsN[4];               // +0x285C, copies refused per vtable above
    LONG rpSpare[63];                // +0x286C, v49 is 128 int32 (0x2768..0x2967, the size P4 reserved); v50 starts at 0x2968
    // v50 (0.28.0, [RouteSnap] Mode=1, P4 part A, p4.cpp; docs/94 §12, §17, §18)
    LONG snP4Ready;                  // +0x2968, bit 0 thunks T1-T4, 1 T1 in the [RouteGate] trampoline, 2 door stub chained behind
                                     //   [ProbeValues], 3 door stub on its own call site, 4 clone provider registered; 0 = off
    LONG snP4Mode;                   // +0x296C, [RouteSnap] Mode (1 = shadow)
    LONG snShadowArmed;              // +0x2970, async STARTs armed for a shadow (1 in ShadowEvery)
    LONG snShadowBusy;               // +0x2974, armed but skipped at the worker: another job was queued
    LONG snShadow;                   // +0x2978, shadows finished (one record each)
    LONG snM[6];                     // +0x297C, native class M0-M5 (docs/94 §13 rules)
    LONG snM1Tie;                    // +0x2994, M1 goal-tie (P4TieN, docs/94 §13.1; 0.30.3)
    LONG snAbort[16];                // +0x2998, refusal / abort reason (p4.cpp kP4Ab: clone, nover, epoch, cold, unsupported,
                                     //   capture, nrbusy, mat, door-miss, door-over, managed, event, livelock, lazy, fault, other)
    LONG snShadowUs;                 // +0x29D8, clone-run time summed
    LONG snShadowMaxUs;              // +0x29DC
    LONG snLiveUs;                   // +0x29E0, live Route_PlanLocked time of the shadowed jobs summed
    LONG snMatUs;                    // +0x29E4, clone acquire (materialise) time summed
    LONG snShadowFault;              // +0x29E8, exceptions in a clone run (P4 off for the session)
    LONG snDeadStamp;                // +0x29EC, T2' 0x61CC73 hits (dead code: must stay 0)
    LONG snStampWrap;                // +0x29F0, per-copy stamp wraps
    LONG snCloneManaged;             // +0x29F4, managed calls from a clone run (tripwire)
    LONG snCloneEvent;               // +0x29F8, World_FireEvent from a clone run (tripwire)
    LONG snCloneLazy;                // +0x29FC, lazy lot build / lot table insert in a clone run (D4)
    LONG snCloneLiveLock;            // +0x2A00, live +0x918 acquire from a clone run (tripwire)
    LONG snDoorRec;                  // +0x2A04, door answers recorded at 0x64F4D3 in live runs
    LONG snDoorReplay;               // +0x2A08, replayed in clone runs
    LONG snDoorMiss;                 // +0x2A0C, clone asked a door the live run did not
    LONG snDoorUnused;               // +0x2A10, recorded answers the clone did not use
    LONG snDoorOver;                 // +0x2A14, door table full in a live run
    LONG snWorkerDirty;              // +0x2A18, [RouteSnap] writer leaves hit inside a clone run
    LONG snDirtyOver;                // +0x2A1C, workerDirty lot set overflow (= every lot)
    LONG snNrWaits;                  // +0x2A20, D2: sim-side writers that waited for a clone run
    LONG snNrWaitUs;                 // +0x2A24
    LONG snNrArmWaitUs;              // +0x2A28, worker wait for g_snNrIn == 0 at arm
    LONG snNrBits[7];                // +0x2A2C, D5 families changed in a window the plan read: physics, terrain swap, destroy,
                                     //   lot index rebuild, world events 0x0D-0x10, blend reload, lock-key ICs
    LONG snJsonl;                    // +0x2A48, shadow records written
    LONG snJsonlFail;                // +0x2A4C
    LONG snPathsFreed;               // +0x2A50, private clone-run paths released
    // v51 (0.29.0, P4 part B: the replay clone provider, p4clone.cpp; docs/94 §21)
    LONG snPcReady;                  // +0x2A54, 1 = provider registered (Mode=1 with Lots=replay)
    LONG snPcCuts;                   // +0x2A58, armed cuts that gave a version
    LONG snPcCutUs;                  // +0x2A5C, sim time in those cuts (prepare + P3 cut + snapshot, summed)
    LONG snPcCutMaxUs;               // +0x2A60
    LONG snPcRefused[8];             // +0x2A64, 0 cut: busy / no world / lock; 1 cut: no version; 2 acquire: no clone; 3 an older
                                     //   cut or the consumer passed it; 4 wait timeout; 5 epoch or active lot moved; 6 copy failed;
                                     //   7 (0.33.1) clone stale: a lot the consumer forced after a failed map resync not rebased yet
    LONG snPcAcquired;               // +0x2A84, working copies handed out
    LONG snPcMatUs;                  // +0x2A88, worker time building them (summed)
    LONG snPcPending;                // +0x2A8C, ApplyPendingUpdates run at a START (§8.5 step 3)
    LONG snPcFlushed;                // +0x2A90, deferred quadtree flushes run at a START (step 4)
    LONG snPcFirstUse;               // +0x2A94, unbuilt lots first-used at a START (step 2)
    LONG snPcFaults;                 // +0x2A98, faults in the START steps or the working copy build
    LONG snPcOddQ;                   // +0x2A9C, clone Qs whose +0x28 is neither the live nor our A* object
    LONG snPcDirtyLots;              // +0x2AA0, lots a clone run wrote (rebased at the next cut; -1 counts 1)
    LONG snPcDiscard;                // +0x2AA4, faulted clone runs (clone rebuilt, A* object dropped)
    LONG snNrBitDl;                  // +0x2AA8, D5 family 8 "display level" changed in a window whose plan ran 0x6583E0 (M5)
    LONG snDlOnClone;                // +0x2AAC, display level writer called from a clone run (tripwire, expected 0)
    LONG arLosRefused;               // +0x2AB0, async STARTs refused "Los": ForcePhysicsBasedRaycast routes stay sync (§17.9)
    // v53 (private pools for the replay clone and P4 clone runs, pools.cpp; docs/94 §16.7). v52 is design G's (unmerged): when
    // it lands first, this block moves after it (docs/registry.md)
    LONG plReady;                    // +0x2AB4, bit 0 tree pool sites, 1 A*/goal/cell/quadtree cell sites, 2 region reserved
    LONG rpLivePool;                 // +0x2AB8, a clone thread (replay op or P4 job) popped / pushed a game pool (must stay 0)
    LONG plForeign;                  // +0x2ABC, a private node pushed by a thread that owns no clone work (must stay 0)
    LONG plOverlap;                  // +0x2AC0, PoolsEnterJob while another thread held a switch (must stay 0)
    LONG plEpochs;                   // +0x2AC4, game pool objects changed at a cut (PathPlanner_Reset): clone rebuilt
    LONG plKB;                       // +0x2AC8, private region committed (KB)
    LONG plFull;                     // +0x2ACC, private block requests refused (region full or commit failed)
    LONG plJobs;                     // +0x2AD0, PoolsEnterJob calls (P4 clone runs on private pools)
    // v54 (0.30.0, P4 portal references H20, docs/109 §8.9 P4-H20a-d, docs/94 §21.8)
    LONG snNrBitPortal;              // +0x2AD4, D5 family 9 "portal ICs" changed in a clone window (M5; snIc[4..14])
    LONG rpPortalRefs;               // +0x2AD8, portal references the replay clone holds now (H20b; gauge, 0 with no clone)
    LONG snPcPortalPend[2];          // +0x2ADC, H20c: 0 armed cuts refused for a live lot with pending portal adds +0x54 /
                                     //   removes +0x64; 1 acquires refused for a clone lot with them
    // v55 (0.31.0, lazy Gtiles, snap.cpp; docs/94 §21.9): dirty tiles are built only for a reader
    LONG spTileFill[2];              // +0x2AE4, tiles built after the cut left them out: 0 for a reader (armed P4 cut), 1 the audit
    LONG spTileFillUs[2];            // +0x2AEC, time in those builds (µs; [0] is inside spCutUs, [1] inside neither spCutUs
                                     //   nor spAuditUs)
    LONG spTileOut;                  // +0x2AF4, tiles the current version does not carry (gauge; 0 after a reader or audit)
    // v56 (0.31.3, H20 tripwire, p4.cpp; docs/94 §21.8.1 risk 1)
    LONG p4VarTrip;                  // +0x2AF8, AddPortalLocationVariation on a portal the clone holds, during a clone run
                                     //   (count only; expected 0, non-zero = put D2 on that IC slot)
    // v57 (0.31.4, bp-snap-p4 crash at 0x63576A; docs/94 §21.11)
    LONG lrPendFlush;                // +0x2AFC, lot-0 LotRouting_RemoveObjectFootprints 0x635700: Qs with a pending remove of the
                                     //   id flushed first (0x666A40), so the game's walk cannot skip past its range end
                                     //   ([RouteSnap] stub, or since 0.31.5 the [LotRemoveGuard] detour on every build)
    LONG lrPendBusy;                 // +0x2B00, such a Q left unflushed because it is being iterated (+0x48 > 0): the crash
                                     //   precondition stays (expected 0)
    LONG pcDeferStop;                // +0x2B04, a quadtree insert / remove deferral (0x666C50 / 0x666E30 with Q+0x48 > 0) in a
                                     //   P4 clone run, stopped before it wrote the live pending maps P+0x30 / P+0x3C
    // v59 (0.32.0, [LoadProf] world-load routing build share, loadprof.cpp; docs/39 §10). v58 is reserved by a parallel
    // branch: these fields move after its fields when it merges first (agent.js reads them in version order)
    LONG lpReady;                    // +0x2B08, 1 = the window has both ends hooked (LoadWorld call, world event 7 call)
    LONG lpHooks;                    // +0x2B0C, sites hooked
    LONG lpSkipped;                  // +0x2B10, sites skipped (bytes differ: another section owns them)
    LONG lpLoads;                    // +0x2B14, LoadWorld calls (each zeroes the fields from lpWindowMs on)
    LONG lpEv7;                      // +0x2B18, windows closed by world event 7
    LONG lpFrameFull;                // +0x2B1C, entries not timed (32 frames open on the thread)
    LONG lpWindowMs;                 // +0x2B20, LoadWorld entry -> world event 7 (ms)
    LONG lpRtUs[2];                  // +0x2B24, routing time, outermost routing frames only: [0] in the window, [1] after
    LONG lpRtThrUs[3];               // +0x2B2C, lpRtUs[0] by thread: main, sim, other
    LONG lpSite[28][2][3];           // +0x2B38, per site (kLp order) and row (0 window, 1 after): calls, us (inclusive), max us
    // v60 (0.32.10): [PlanRepeat] plan cache probe (planrepeat.cpp, docs/23 §18). Offsets without v58 (+v58's size when it
    // merges first). Class index: 0 Plan, 1 Replan, 2 PlanFromPoint, 3 ReplanFromPosition, 4 sim outside a route IC, 5 other
    // thread. Windows: 0 <= 1 s, 1 <= 5 s, 2 <= 30 s (cumulative).
    LONG prReady;                    // +0x2DD8, bit 0 counting, bit 1 world version from the [RouteSnap] counters
    LONG prRing;                     // +0x2DDC, ring entries (gauge, max 4096)
    LONG prEvictYoung;               // +0x2DE0, entries younger than 30 s overwritten (30 s column is a lower bound)
    LONG prFault;                    // +0x2DE4, plans whose key or result could not be read (timed, not keyed)
    LONG prUnwound;                  // +0x2DE8, an outer plan left without its end (managed exception)
    LONG prVersion;                  // +0x2DEC, world version after the last plan (gauge)
    LONG prPlans[6];                 // +0x2DF0, plans by class
    LONG prUs[6];                    // +0x2E08, their time (us)
    LONG prExact[6][3];              // +0x2E20, exact repeats (same key, same world version) by window
    LONG prExactUs[6][3];            // +0x2E68, time of those plans (the cache's savings bound)
    LONG prFpOnly[6];                // +0x2EB0, same key <= 30 s, only per-object footprint writers changed
    LONG prFpOnlyUs[6];              // +0x2EC8
    LONG prFpOnlyMatch[6];           // +0x2EE0, of those, the same result
    LONG prChanged[6];               // +0x2EF8, same key <= 30 s, other routing writers changed
    LONG prMatch[6];                 // +0x2F10, exact repeats <= 30 s with the same result
    LONG prMismatch[6];              // +0x2F28, exact repeats <= 30 s with another result (nondeterminism or unseen input)
    LONG prLoose[6];                 // +0x2F40, loose-key (0.5 m cells) repeats <= 30 s, same world version
    LONG prLooseUs[6];               // +0x2F58
    // v61 (0.33.0): [RouteSnap] Mode=2, P5: the game takes the clone run's answer for ReplanFromPosition jobs (p5.cpp, docs/94
    // §19, §22). Offsets without v58 (+v58's size when it merges first). Fallback reasons in P4_AB_* order (as snAbort).
    LONG m2Ready;                    // +0x2F70, bit 0 Mode=2 on (cleared when it turns off), 1 M2-1 sites snapshotted, 2 L2 read
    volatile LONG m2Take;            // +0x2F74, switch (`--ab routesnap`): 1 = Mode=2 jobs take the clone answer, 0 = plain A10 jobs
    LONG m2Off;                      // +0x2F78, why Mode=2 turned off for the session: 0 on, 1 M2-1 site, 2 M2-2 trips, 3 P4 fault
    LONG m2Sites;                    // +0x2F7C, code sites in the M2-1 snapshot
    LONG m2SiteAt;                   // +0x2F80, the first snapshotted site whose bytes changed (M2-1), 0 = none
    LONG m2Armed;                    // +0x2F84, Mode=2 STARTs armed (ReplanFromPosition, Kinds bit 8, m2Take on)
    LONG m2Plans;                    // +0x2F88, armed jobs that reached Route_PlanLocked
    LONG m2Taken;                    // +0x2F8C, the clone run's answer moved into the route (no game plan)
    LONG m2Fb[16];                   // +0x2F90, the game's own plan instead, by reason (P4_AB_* order)
    LONG m2FbList;                   // +0x2FD0, of m2Fb capture: the route's out list was not empty at entry
    LONG m2FbNr;                     // +0x2FD4, clean run, but a non-routing family the plan read changed (nr != 0: M5 skew)
    LONG m2Verify;                   // +0x2FD8, verify jobs (VerifyEvery): clone run and game plan, the game's kept, one record
    LONG m2VerM[6];                  // +0x2FDC, their class M0-M5 (native, as snM)
    LONG m2VerBad;                   // +0x2FF4, verify M2 / M3 (would have been taken wrong; logged)
    LONG m2Trips;                    // +0x2FF8, M2-2: managed call or world event in a clone run outside a door ask
    LONG m2DoorAsk;                  // +0x2FFC, door asks of Mode=2 clone runs (the game's chain, proxied to the sim thread)
    LONG m2DoorHist[5];              // +0x3000, clone runs by asks: 0, 1-2, 3-5, 6-10, 11+
    LONG m2DoorWaitUs;               // +0x3014, time inside those asks (us)
    LONG m2JobUs;                    // +0x3018, Mode=2 clone run time including the asks (us)
    LONG m2Stale;                    // +0x301C, runs whose cut had replay records after it (the world moved since START)
    LONG m2StaleRecs;                // +0x3020, sum of those records
    LONG m2Guard;                    // +0x3024, M2-3 log-only findings: bit 0 0xD7E560 detour, 1 0xE5CA90, 2 0xE59218, 3 0xE5637D,
                                     //   4 0xE648DC (MonoPatcher.asi, docs/107 §2.2)
    // v62 (0.34.0): [RouteSnap] ReleaseLock=1, P5b: the job's +0x918 lent to the sim's steering while a P5 clone run runs
    // (p5b.cpp, docs/94 §22.9). Offsets without v58 (+v58's size when it merges first).
    LONG p5bReady;                   // +0x3028, 1 = installed (ReleaseLock=1 with Mode=2, the gate's Lock_Release and steering frames)
    volatile LONG p5bRelease;        // +0x302C, switch (`--ab routesnaprel`): 1 = lend +0x918 out during clone runs, 0 = keep it (P5)
    LONG p5bWins;                    // +0x3030, windows opened (a clone run's start, again after each door ask)
    LONG p5bRefused[4];              // +0x3034, not opened: 0 switch off or verify job, 1 not the owner / counts differ, 2 an
                                     //   acquire waiting inside the game's Lock_Acquire, 3 not the job's lock or no job
    LONG p5bWinUs;                   // +0x3044, time the lock was lent out (us; compare m2JobUs)
    LONG p5bRetakeUs;                // +0x3048, worker time to get it back (us)
    LONG p5bRetakeMaxUs;             // +0x304C
    LONG p5bRetakeAsk;               // +0x3050, taken back for a door ask (lent out again after it)
    LONG p5bRetakeTrip;              // +0x3054, taken back for a tripwire (P4Abort: managed call, event, live lock, lazy stop)
    LONG p5bSimAcq;                  // +0x3058, outermost +0x918 acquires granted while lent out (the sim's steering)
    LONG p5bHeldOut;                 // +0x305C, other acquires while lent out: refused (tries) or held until the window closed
    LONG p5bWatchdog;                // +0x3060, retakes still waiting after 2 s (logged with the owner thread)
    LONG p5bRecMax;                  // +0x3064, highest recursion count lent out
    // v63 (0.34.2): P5b read-only gate leaves ([RouteSnap] ReleaseGates=1, p5b.cpp P5bGate; docs/94 §22.10)
    volatile LONG p5bGates;          // +0x3068, switch (`--ab routesnapgate`): 1 = gate-12 reads (F-B, F-C, F-G) and gate-0 hits
                                     //   also take the lent lock, 0 = steering only (0.34.0)
    LONG p5bGateAcq[2];              // +0x306C, outermost gate acquires granted while lent: 0 gate 12, 1 gate-0 hit
    LONG p5bGateMiss;                // +0x3074, gate-0 entries that got the lent lock on a miss: given back, waited as before
    // v65 (0.35.0; v64 was held for 0.34.3, which added no field): P5c, [RouteSnap] FreeLock=1 (p5c.cpp; docs/94 §22.11).
    // Offsets without v58 (+v58's size when it merges first).
    LONG p5cReady;                   // +0x3078, 1 = installed (FreeLock=1 with ReleaseLock=1)
    volatile LONG p5cFree;           // +0x307C, switch (`--ab routesnapfree`): 1 = a taken window admits every plain sim acquire,
                                     //   parks destroys and refuses stale answers; 0 = P5b admission only
    LONG p5cJobs;                    // +0x3080, jobs whose window opened with P5c admission
    LONG p5cAcq;                     // +0x3084, outermost plain sim acquires granted while lent (writers etc.; not steering or gates)
    LONG p5cStale[2];                // +0x3088, answers refused (the game's plan answered): a routing write after the cut, 0 all
                                     //   before the window, 1 at least one inside it
    LONG p5cParked;                  // +0x3090, destroys in a window that parked one reference and ran without waiting
    LONG p5cKeptWait[4];             // +0x3094, destroys in a window that waited as before: 0 D-X record, 1 graveyard full,
                                     //   2 off the sim or in a job callback, 3 object not found
    LONG p5cFlushed;                 // +0x30A4, parked references released
    LONG p5cFlushRefused;            // +0x30A8, flush points that found a job running (left for the next point)
    LONG p5cPendMax;                 // +0x30AC, most references parked at once (cap 512)
    LONG p5cAgeMaxUs;                // +0x30B0, longest park-to-release time (us)
    LONG p5cCountLow;                // +0x30B4, T-G1: a parked object's count below 1 at the flush (left unreleased; must be 0)
    LONG p5cPlanHere;                // +0x30B8, sync plans under the lent lock: planned on the sim without the job wait
    LONG p5cHandOver;                // +0x30BC, tripwire: a wait for the job or its clone run holding the lent lock (handed back)
    LONG p5cOff;                     // +0x30C0, 1 = FreeLock off for the session (after a hand-over)
    // v66 (0.34.10, merged after P5c v65 in 0.35.1): [HitchAttr] sim waits for the route job by class (hitchattr.cpp HA_*; docs/96 §9)
    LONG haReady;                    // +0x30C4, 1 = [HitchAttr] Enabled
    LONG haWaits[4];                 // +0x30C8, waits: 0 +0x918, 1 other lock during a job, 2 job wait, 3 plan waited for the job
    LONG haUs[4];                    // +0x30D8, their time (us)
    LONG haMaxUs;                    // +0x30E8, longest single wait
    // v67 (0.35.2, docs/94 §21.17): P4 Mode=1 cuts behind queued jobs, the longer shadow acquire wait, [OptsInit] Enabled=2
    LONG p4QCut;                     // +0x30EC, START cuts made while jobs were only queued (none running; try-lock, no sim wait)
    LONG p4QCutMiss;                 // +0x30F0, such cuts refused "busy": the routing lock was taken (the worker started a job)
    LONG p4WaitExt;                  // +0x30F4, shadow acquires that reached the cut after the old 250 ms limit
    LONG oiClone;                    // +0x30F8, options ctor calls zeroed in a clone run ([OptsInit] Enabled=2)
    LONG rpHard[4];                  // +0x30FC, replay map resync misses that rebuilt the clone: 0 no clone lot holds the missed
                                     // node's Q, 1 soft tries used up (kRpSoftMax), 2 entry counts differ or > 16 lots, 3 other
    // v68 (0.35.3, docs/94 §21.18): replay consumer misses on a clone a newer EPOCH already replaces (no rebuild asked)
    LONG rpOldAsk;                   // +0x310C, consumer rebuild asks skipped: a newer EPOCH is in the ring (the clone only marked bad)
    LONG p4AcqBad;                   // +0x3110, P4 acquires refused "noclone" on a clone marked bad with no rebuild ask pending
    // v69 (0.35.4, docs/94 §21.19)
    LONG p4Obst;                     // +0x3114, M2/M3 shadow records that carry the obstacle sets (clone and live footprints)
    LONG p4ObstBusy;                 // +0x3118, of those, live set not read: the one +0x918 try failed
    // v70 (0.35.5, docs/94 §21.20): replay soft resync by miss kind, rebase reasons
    LONG rpSoftReal;                 // +0x311C, resync misses on a lot this very batch rebased (kRpSoftMax tries, then rebuild)
    LONG rpSoftFresh;                // +0x3120, misses on a lot no pending soft force covers: forced (kRpSoftFreshMax batches)
    LONG rpSoftInFlight;             // +0x3124, misses on a lot already forced whose batch was cut before the force: not counted
    LONG rpRebaseWhy[4];             // +0x3128, lots rebased: 0 forced by a write (W3 / W4, refused copy), 1 soft resync force,
                                     // 2 lot signature changed, 3 lot new or gone
    LONG rpRebaseQSame;              // +0x3138, of [2]: the lot's Q list was unchanged (a lot shell refresh would have done)
    // v71 (0.35.6, docs/94 §21.21): P4 Mode=1 STARTs while a job holds +0x918 share the newest pending cut
    LONG p4Borrow;                   // +0x313C, START cuts shared with a pending cut (a job running; no sim wait, no new cut)
    LONG p4BorrowMiss;               // +0x3140, such STARTs with no pending cut to share: refused "nover"/"busy" as before
    LONG p4BorrowDirty;              // +0x3144, shadow acquires refused "mat"/"stale": a shadow on the same cut wrote clone lots
    // v72 (0.35.8, docs/94 §21.23): replay child table entries kept because a reused live child address belongs to another copy
    LONG rpKidsKept;                 // +0x3148, retire / GC of a block skipped a child entry another copy holds (I-94-440)
    // v73 (0.36.7, docs/134): sync fallback freezes, [RouteAsync] FallbackAsync / SyncCapMs / LogFallbackStack
    volatile LONG fbMode;            // +0x314C, runtime switch (--ab fallbackasync): 1 = foreign-task STARTs go async, M1Pending
                                     //          applies the pending lot updates first, the job queue is 32 deep instead of 8
    LONG fbForeign;                  // +0x3150, Route.Plan STARTs from a task that is not the route's Sim (ts3fixes 0.36.7+)
    LONG fbForeignStarted;           // +0x3154, of those, started on the worker (fbMode 1)
    LONG fbPendApplied;              // +0x3158, M1Pending STARTs: pending lot updates applied on the sim thread, START went on
    LONG fbPendBusy;                 // +0x315C, M1Pending STARTs whose one +0x918 try failed (refused M1Pending as before)
    LONG fbDeepQueued;               // +0x3160, STARTs that took a job slot past the first 8 (fbMode 1; QueueFull before)
    LONG fbCapUs;                    // +0x3164, [RouteAsync] SyncCapMs in us (0 = off): search cap of a sim-thread sync plan
    LONG fbCapped;                   // +0x3168, sync plans that cap ended (an ordinary no-path failure)
    LONG fbStacks;                   // +0x316C, fallback log lines followed by a script stack line (LogFallbackStack)
    LONG fbActiveTrip;               // +0x3170, tripwire: a foreign job's route was being followed at its collect (expected 0)
    // v74 (docs/89 §10): design U, UpdateSimFootprint 0x833560 deferred into design D's queue
    // ([Discourage] DeferFootprint=1, discourage.cpp)
    LONG usfReady;                   // +0x3174, 1 = detour in place
    volatile LONG usfMode;           // +0x3178, runtime switch (--ab deferfootprint): 1 = defer while a job runs
    LONG usfCalls;                   // +0x317C, sim-thread calls
    LONG usfInJob;                   // +0x3180, of those, while a job ran
    LONG usfPass;                    // +0x3184, passed through: nested under +0x918, job callback, drain, another world, closing
    LONG usfEarlyOut;                // +0x3188, deferral path: the early out 0x82A720 said no (nothing done, as the original)
    LONG usfDeferred;                // +0x318C, records queued
    LONG usfOrderDefer;              // +0x3190, of those, queued only because the object had queued records (no job or mode 0)
    LONG usfDup;                     // +0x3194, calls equal to the object's newest queued record: dropped
    LONG usfParkedPass;              // +0x3198, parked object (lock-key map), nothing queued: the original (waits)
    LONG usfFullDrain;               // +0x319C, queue full, no Add queued: drained (waits), then the original
    LONG usfFullPass;                // +0x31A0, queue full with an Add queued: the original (waits; may run out of order)
    LONG usfApplied;                 // +0x31A4, records applied by the original (force 1) at a drain or a destroy
    LONG usfCollapsed;               // +0x31A8, skipped at a drain: the object's next record is a U record (supersedes)
    LONG usfDestroyApplied;          // +0x31AC, of usfApplied, by the D-X destroy gate
    LONG usfGoneEnable0;             // +0x31B0, object gone at apply, disable record
    LONG usfSwept;                   // +0x31B4, of those, dead-id sweeps (T-U3: = usfGoneEnable0 with DeadSweep=1)
    LONG usfGoneEnable1;             // +0x31B8, object gone at apply, enable record: dropped
    LONG usfIdMismatch;              // +0x31BC, the id names another object at apply: dropped
    LONG usfAgeMaxMs;                // +0x31C0, oldest U record at its drain (T-U2: < 300)
    LONG usfApplyInJob;              // +0x31C4, tripwire T-U1: an apply while a job ran (expected 0)
    // v75 (docs/134 §2.6): M1Retry
    volatile LONG fbRetryMode;       // +0x31C8, runtime switch (--ab m1retry): 1 = an M1Pending START whose +0x918 try failed
                                     //          is parked; the task's READY polls retry once per tick (needs fbMode 1)
    LONG fbRetryParked;              // +0x31CC, STARTs parked (answered yes, no job queued yet)
    LONG fbRetryQueued;              // +0x31D0, parked STARTs queued on a later READY (pending updates applied or gone)
    LONG fbRetryGaveUp;              // +0x31D4, parked STARTs that planned synchronously (tick cap, other refusal, poll cap)
    LONG fbRetryTicksMax;            // +0x31D8, most READY ticks one parked START waited
    // v76 (docs/88 §10): design G standalone, [Discourage] DestroyPark=1 (p5c.cpp P5cPark / P5cFlush; the graveyard and its
    // p5c* counters shared)
    LONG dpReady;                    // +0x31DC, 1 = installed ([RouteGate] Enforce=1 and [Discourage] DestroyPark=1)
    volatile LONG dpMode;            // +0x31E0, runtime switch (--ab destroypark): 1 = a destroy during any async job parks
    LONG dpParked;                   // +0x31E4, parks outside a P5c window (also in p5cParked, the graveyard total)
    LONG dpKeptWait;                 // +0x31E8, destroys outside a P5c window that waited as before (also in p5cKeptWait[4])
    // v77 (docs/137): [AvoidRO] (built as v74 at +0x3174 on its branch, moved after v76 at the 0.36.12 merge), read-only RouteFollow_A 0x846B70 / RouteFollow_B 0x85B470 while a job runs (avoidro.cpp)
    LONG avReady;                    // +0x31EC, bit 0 A-entry, 1 A-site (call 0x846D1C), 2 B-entry, 3 T-AV stubs
    volatile LONG avMode;            // +0x31F0, runtime switch (--ab avoidro): 1 = while a job runs, answer from the clone
    volatile LONG avShadow;          // +0x31F4, runtime switch (--ab avoidshadow): no job, the game's call then the clone
    LONG avTakenHit;                 // +0x31F8, A answers used: a footprint id (the game's body did not run)
    LONG avTakenMiss;                // +0x31FC, A answers used: no footprint, the game's body ran with the site skipped
    LONG avTakenB;                   // +0x3200, B answers used
    LONG avSiteSkip;                 // +0x3204, A-site returns of 0 (must equal avTakenMiss)
    LONG avFb[10];                   // +0x3208, fallbacks: state, QL busy, dirty lot index, lot miss, deferred flag, polygon
                                     //          cache, shape/slot, tripwire, pin moved, job end
    LONG avUs;                       // +0x3230, clone time of the answers used (us)
    LONG avMaxUs;                    // +0x3234
    LONG avTripAv;                   // +0x3238, T-AV: followed-path option writers entered off the sim thread while armed
    LONG avTwSite[2];                // +0x323C, by site 0x81D090 RoutingOptions_Assign, 0x85FF30 EditPathForRoute
    LONG avShadowRuns;               // +0x3244, shadow compares (A and B)
    LONG avShadowMismatch;           // +0x3248, A: the game's id differs from the clone's (Sim scan included)
    LONG avShadowMismatchB;          // +0x324C, B: 0/1 differs
    LONG avShadowQueryMismatch;      // +0x3250, 0x82EDB0 (mode 1) footprint set differs from the clone's query
    LONG avShadowOrder;              // +0x3254, same footprints in another order
    LONG avShadowSimScan;            // +0x3258, A shadow cases where the clone found no footprint and the Sim scan answered
    LONG avSimWriteInRo;             // +0x325C, sim-thread W1-W6 / tripwire writer entries during an AvoidRO clone (= 0)
    // v78 (docs/142, [RouteEarlyOut] probe; after v77's fields): no-path pre-check at the world A*
    volatile LONG eoMode;            // +0x3260, runtime switch (--ab earlyout): 1 = on at the ini level (eoAct), 0 = pass-through
    LONG eoReady;                    // +0x3264, A* detour installed
    LONG eoReqs;                     // +0x3268, Route_PlanLocked requests seen with eoMode 1
    LONG eoAs;                       // +0x326C, outer world A* calls (the circle rerun counts inside its outer call)
    LONG eoAsFail;                   // +0x3270, of those, no path
    LONG eoAsMs;                     // +0x3274, time in them
    LONG eoAsMaxUs;                  // +0x3278, longest one
    LONG eoProv;                     // +0x327C, calls a proof said would fail (they still ran: Mode 1 changes nothing)
    LONG eoProvMs;                   // +0x3280, their time = what an acting early-out would save
    LONG eoProvWhy[3];               // +0x3284, by proof: car components, game colours, same-request memo
    LONG eoWrong;                    // +0x3290, proven calls that found a path (the correctness argument broke; expected 0)
    LONG eoReachMax;                 // +0x3294, largest closed set of a failed search (nodes reachable from the start)
    LONG eoLabelBuilds;              // +0x3298, car component builds (one per R)
    LONG eoLabelUs;                  // +0x329C, their time
    LONG eoSlow;                     // +0x32A0, requests >= LogMs
    LONG eoSlowMs;                   // +0x32A4, their time
    LONG eoSlowAsMs;                 // +0x32A8, world A* time inside them
    LONG eoSlowProvMs;               // +0x32AC, provable A* time inside them
    LONG eoSlowSaved;                // +0x32B0, slow requests the proofs alone would bring under LogMs
    // v79 (docs/143, unseen.cpp, [UnseenProbe]; built as v78 at +0x3174 on its branch, moved after v78
    // early-out at the 0.36.13 merge): sim-thread locomotion work by agent class. Classes: 0 visible, 1 unseen,
    // 2 outside an agent update / not classified. Kinds: 0 Sim, 1 other (cars, boats).
    LONG unReady;                    // +0x32B4, bit 0 agent call site, 1 footprint (own detour or via design U's stub), 2 plan split, 3 keyed +0x918 ([RouteGate])
    LONG unPasses;                   // +0x32B8, locomotion passes seen (camera read once per pass)
    LONG unCamFail;                  // +0x32BC, camera reads that failed (that pass's agents count as visible)
    LONG unSkipped;                  // +0x32C0, agent updates not classified (no owner / transform, fault): not split
    LONG unFlips;                    // +0x32C4, an agent's class changed since its last update
    LONG unAgents[2][2];             // +0x32C8, agent updates [class 0-1][kind]
    LONG unAgentUs[2][2];            // +0x32D8, their time (Locomotion_UpdateAgent 0x957210, whole call)
    LONG unAgentMaxUs[2];            // +0x32E8, longest single update by class
    LONG unAcq[3][3];                // +0x32F0, first +0x918 acquires on the sim [class 0-2][steering 0x85FE10, avoidance 0x846D21, other]
    LONG unWaitN[3][4];              // +0x3314, keyed sim waits for the route job [class][steering, avoidance, footprint 0x833593, other]
    LONG unWaitUs[3][4];             // +0x3344
    LONG unFp[3];                    // +0x3374, UpdateSimFootprint 0x833560 calls on the sim [class 0-2]
    LONG unFpUs[3];                  // +0x3380
    LONG unPlan[2][3];               // +0x338C, IC_Route_Plan calls [0 other route, 1 car-only route][follower class 0-2]
    LONG unPlanUs[2][3];             // +0x33A4, their sim-thread time
    LONG unReachX10;                 // +0x33BC, the last camera reach in decimetres
    LONG unEaOff[2];                 // +0x33C0, agent updates [class 0-1] after which EA's own on-screen byte agent+0xC8 was 0
                                     //          (OffScreenSimBrain / SimpleBody for walking Sims, docs/143 §2.2)
    // v79 continued (docs/143 §6, [UnseenThrottle] phase 1, unseen.cpp): steering obstacle list 0x85FFE0 at 0x9573B7 every
    // Nth tick for unseen walking Sims; a skipped call leaves the caller's vector empty (the game's "no obstacles")
    volatile LONG utMode;            // +0x33C8, runtime switch (--ab unseenthrottle): 1 = skip, 0 = every call runs
    LONG utReady;                    // +0x33CC, bit 0 call site 0x9573B7 patched, 1 viewer channel heard (ts3fixes push)
    LONG utEvery;                    // +0x33D0, N (Every)
    LONG utCalls;                    // +0x33D4, obstacle-list calls seen
    LONG utRun;                      // +0x33D8, of those, run (= utCalls - utSkipped)
    LONG utSkipped;                  // +0x33DC, skipped: vector left empty
    LONG utWhy[13];                  // +0x33E0, why a call ran: mode off, no agent context, visible, EA on-screen byte,
                                     //          state not 1, linked agent, near a viewer, no fresh viewer table, first tick
                                     //          after a hold / new agent, stagger turn, near goal, portal (sub-path end or
                                     //          start), path fault / no path
    LONG utPathUs;                   // +0x3414, time in the path tests (only on would-skip ticks)
    LONG utViewerPushes;             // +0x3418, viewer tables accepted (END with a matching count)
    LONG utViewers;                  // +0x341C, viewers in the current table
    LONG utViewerBad;                // +0x3420, viewer messages refused (out of sequence, count mismatch, table full)
    // v80 (docs/143 §8, [UnseenThrottle] phase 2, unseen.cpp): avoidance behaviour, footprint updates and whole agent
    // updates of unseen walking Sims every Every-th pass; each part its own runtime switch (1 = throttle, 0 = every call runs)
    volatile LONG utAvMode;          // +0x3424, --ab unseenavoid (ObstacleAvoidance slot 0x1026E10)
    volatile LONG utFpMode;          // +0x3428, --ab unseenfootprint (UpdateSimFootprint 0x833560)
    volatile LONG utRateMode;        // +0x342C, --ab unseenrate (Locomotion_UpdateAgent call 0x83CF52)
    LONG ut2Ready;                   // +0x3430, bit 0 avoidance slot, 1 footprint layer, 2 it sits below design U, 3 rate
    LONG utAvCalls;                  // +0x3434, avoidance behaviour updates seen
    LONG utAvSkipped;                // +0x3438, skipped (the record stays as the last run left it, no notice)
    LONG utAvWhy[18];                // +0x343C, why one ran: utWhy's 13 reasons, then not a Sim, short timer, new path,
                                     //          step cap, hash slot taken (the last five: rate only)
    LONG utFpCalls;                  // +0x3484, footprint calls the rule looked at (sim thread, mode on, not nested)
    LONG utFpSkipped;                // +0x3488, held back (a new pending update or folded into one)
    LONG utFpCoalesced;              // +0x348C, pending updates folded into or superseded by a later call
    LONG utFpFlushed;                // +0x3490, pending updates applied when due
    LONG utFpGone;                   // +0x3494, pending updates dropped at their due time (object gone, id reused, other world)
    LONG utFpWhy[12];                // +0x3498, why a call ran: off, pass-through (other thread, nested, own flush, no
                                     //          gate lock), not classified, visible, not a Sim, viewer, no viewer table,
                                     //          parked, disable / other arguments, due (FpMs since the last), table full
    LONG utFpFlushUs;                // +0x34C8, time in the flushes
    LONG utFpPendMax;                // +0x34CC, most pending updates at once (64 max)
    LONG utRateCalls;                // +0x34D0, agent updates the rate rule looked at
    LONG utRateSkipped;              // +0x34D4, skipped: the pass's step went into the agent's debt
    LONG utRateWhy[18];              // +0x34D8, why one ran (as utAvWhy)
    LONG utRateCatchUp;              // +0x3520, runs that carried debt (summed step)
    LONG utRateDebtMaxUs;            // +0x3524, largest summed step passed, us of game dt
    LONG utRateDropped;              // +0x3528, debts dropped (agent not called the pass before, slot taken by another)
    LONG utSnap[4];                  // +0x352C, snap-backs to full rate because the agent became visible after a skipped
                                     //          call of that part [0 list, 1 avoidance, 2 footprint, 3 rate]
    LONG utSnapMaxMs[4];             // +0x353C, the largest owed time at such a snap-back, ms (rate: the owed step)
    // v81 (docs/134 §10, 0.36.16): [RouteAsync] FbJobCapMs, a world A* cap for foreign (FallbackAsync) jobs on the worker
    volatile LONG fbjcMode;          // +0x354C, runtime switch (--ab fbjobcap): 1 = foreign jobs run under fbjcCapUs
    LONG fbjcCapUs;                  // +0x3550, [RouteAsync] FbJobCapMs in us (0 = off)
    LONG fbjcCapped;                 // +0x3554, foreign jobs the cap ended (an ordinary no-path failure)
    LONG fbjcMaxUs;                  // +0x3558, longest foreign job (fb = 1), worker time of the IC call
    LONG ownJobMaxUs;                // +0x355C, longest other job (own-task START, A10 kinds)
    LONG fbjcLong;                   // +0x3560, foreign jobs >= 200 ms (each logged, rate-limited)
    LONG ownJobLong;                 // +0x3564, other jobs >= 200 ms (each logged, rate-limited)
    // v83 (docs/143 §8.14, 0.36.18; moves after v82 when that merges first): [UnseenThrottle] diagnostics, observe only, on
    // with Enabled=1. Throttle parts: 0 obstacle list, 1 avoidance, 2 rate, 3 footprint (unseen.cpp UT_DG_*)
    LONG utDgReady;                  // +0x3568, bit 0 IC_Route_Replan 0x765490 detour, 1 IC_Route_ReplanFromPosition 0x765550
    LONG utDefer[3];                 // +0x356C, skips while the agent's last obstacle list was non-empty (its +0x60 notice
                                     //          likely deferred) [list, avoidance, rate]
    LONG utListNotices;              // +0x3578, obstacle-list runs in unseen agent updates that returned a non-empty list
    LONG utRpN[2][3];                // +0x357C, sim-thread IC calls [Replan, ReplanFromPosition][follower: a throttle part
                                     //          skipped it in the last 8 passes, other follower, no follower found]
    LONG utRpUs[2][3];               // +0x3594, their time (whole IC call, as stutter-profile's byEntry)
    LONG utRpLong[2][3];             // +0x35AC, of those >= 100 ms
    LONG utRpPart[2][4];             // +0x35C4, class-0 calls by the part that skipped the follower last [IC][part]
    LONG utRpPartMs[2][4];           // +0x35E4, summed ms from that skip to the IC call
    // v84 (docs/134 §11, 0.36.20): FbJobCapMs as a deadline from the job's start, checked every 16 cost calls; per-job split
    // of foreign jobs (fb = 1) and the sim thread's waits behind them. Buckets: <100, <300, <500, <1000, <2000, >=2000 ms
    LONG fbjcCapAtMaxUs;             // +0x3604, longest job start -> cap fire of a capped foreign job
    LONG fbjcCapLate;                // +0x3608, capped foreign jobs whose cap fired more than 20 ms after FbJobCapMs
    LONG fbjcTailMaxUs;              // +0x360C, longest cap fire -> job end of a capped foreign job (work the cap cannot stop)
    LONG fbjcPlanMaxUs;              // +0x3610, longest summed Route_PlanLocked time in one foreign job
    LONG fbjcHostMaxUs;              // +0x3614, longest summed managed-call wait (served by the sim thread) in one foreign job
    LONG fbjcSimWaitMaxUs;           // +0x3618, longest sim-thread wait behind a foreign job (+0x918 pump, RtWaitJob)
    LONG fbjcJobHist[6];             // +0x361C, foreign jobs by length
    LONG fbjcWaitHist[6];            // +0x3634, sim-thread waits behind a foreign job by length
    // v85 (docs/149, 0.36.21): design H2 ([RouteAsync] H2=1 or [LocoWindow], locowin.cpp); held starts and hold time are v37's
    // g0wJobsHeld / g0wHoldUs. The in-pass waits count in both modes (Mode 0 = the off side of --ab h2)
    LONG h2Released;                 // +0x364C, held starts that began once the pass had ended (not after a drop or watchdog)
    LONG h2PassLockN[2];             // +0x3650, sim-thread waits on +0x918 inside the pass [a job running at the wait's start,
                                     //          no job running: none queued, or queued / held]
    LONG h2PassLockUs[2];            // +0x3658, their summed wait
    LONG h2ReleaseMaxUs;             // +0x3660, longest gap from a pass's end to the start of a job it held
    // v86 (docs/142 §9, 0.36.22, [RouteEarlyOut] Mode=2): acting early-out. After v85 (H2) at +0x3664 (built at +0x364C
    // on its branch, moved at the merge). Kinds: [0] car components, [1] same-request memo
    LONG eoAct;                      // +0x3664, level from the ini: 0 probe (Mode 1), 1 shadow every proof (Mode 2 Act 0), 2 act
    LONG eoActed;                    // +0x3668, world A* calls answered without the search (same result, best, context, bits)
    LONG eoActedWhy[2];              // +0x366C, by proof
    LONG eoActUs;                    // +0x3674, time in the act path (fill, prologue, heuristic pass) of acted and shadowed calls
    LONG eoActMaxUs;                 // +0x3678, longest one
    LONG eoActSavedMs;               // +0x367C, estimated search time saved: nodes x the shadowed game us per node - act us
    LONG eoShadow;                   // +0x3680, proofs answered both ways and compared (the game's answer is returned)
    LONG eoShadowUs[2];              // +0x3684, the game's time on shadowed proofs, by proof (shortcut: us, wraps after 35 min)
    LONG eoShadowNodes[2];           // +0x368C, their reach (nodes in the closed set)
    LONG eoActDiff;                  // +0x3694, shadowed answers that differed (result, best, context +0x64..+0xBB, closed bits)
    LONG eoActOff;                   // +0x3698, 1 = acting latched off by a difference (proofs then run as in Mode 1)
    LONG eoStale[2];                 // +0x369C, proofs not acted because the data moved: car labels vs the live BFS, memo wall print
    LONG eoActRefused;               // +0x36A4, proofs not acted for another reason (restricted entry, factors, budget capped, colour)
    LONG eoActTie;                   // +0x36A8, proofs not acted because two nodes share the smallest heuristic (push order unknown)
    // v87 (0.36.23, docs/150 §7, routephases.cpp): [RouteJobPhases] per-phase self time of the route worker's jobs, observe only.
    // Kind k: 0 own START (Plan), 1 own A10 (Replan / PlanFromPoint / ReplanFromPosition), 2 foreign (fallback) job; phase p: RJP_*
    LONG rjpReady;                   // +0x36AC, 1 = switch on and at least one site timed
    LONG rjpHooks;                   // +0x36B0, sites timed (entry stubs plus interposed trampolines)
    LONG rjpSkipped;                 // +0x36B4, sites skipped (bytes differ, owner unknown)
    LONG rjpDropped;                 // +0x36B8, frames dropped (unwound without returning, or the 48-deep stack full)
    LONG rjpJobs[3];                 // +0x36BC, jobs by kind
    LONG rjpJobUs[3];                // +0x36C8, their time (the same clock as asyncUs; shortcut: us, wraps after 35 min)
    LONG rjpN[3][15];                // +0x36D4, phase entries by kind (nested entries of one phase count each)
    LONG rjpUs[3][15];               // +0x3788, phase self time by kind (nested phases subtracted; RJP_OTHER = job time outside all)
    LONG rjpMaxUs[15];               // +0x383C, longest single self time per phase
    // v89 (docs/150 §11, tailunlock.cpp; built at 0x3878, moves after v88 at its merge): [RouteAsync] TailUnlock, the worker's
    // +0x918 lent to the sim's steering while the post pass's tail passes 16-19 run
    LONG tuMode;                     // +0x3878, switch (--ab tailunlock): 1 = lend, 0 = keep the hold (call sites stay patched)
    LONG tuReady;                    // +0x387C, 1 = the 4 call sites patched
    LONG tuWins;                     // +0x3880, windows opened (the lock released before pass 16)
    LONG tuRefused[4];               // +0x3884, not opened: 0 switch off, 1 not a worker job holding it (clone run, P4, counts
                                     //   differ), 2 a waiter inside the game's Lock_Acquire, 3 a P5b window already open
    LONG tuReleasedUs;               // +0x3894, time the lock was lent out (sum; shortcut: us, wraps after 35 min)
    LONG tuRetakeUs;                 // +0x3898, the worker's wait to take it back after pass 19 (sum)
    LONG tuRetakeMaxUs;              // +0x389C, longest such wait
    LONG tuTrip[3];                  // +0x38A0, tripwires inside a window (retaken at once): 0 worker +0x918 acquire, 1 gate site,
                                     //   2 routing site (rtsites.cpp, not a lot-index or physics read)
    // v88 (docs/150 §12, cellmemo.cpp; appended after v89 because v89 already sits at 0x3878; version stays 89):
    // [CellMemoProbe] would-hit in-room legs of design M (docs/150 §8.3) within one worker plan, observe only
    LONG cmReady;                    // +0x38AC, 1 = Q reset + both wrapper entries hooked (needs [RouteJobPhases])
    LONG cmSkipped;                  // +0x38B0, sites not hooked
    LONG cmPlans;                    // +0x38B4, worker plans (Q reset to Q reset or job end) with at least one leg
    LONG cmLegs[2];                  // +0x38B8, leg requests: 0 InRoomPlan 0x632E40, 1 InRoomPlanToPoint 0x632630
    LONG cmRepeats[2];               // +0x38C0, legs whose M key was already seen in the same plan (would-hit)
    LONG cmCellUs;                   // +0x38C8, cell A* 0x61E500 inclusive time of all legs (us, sum)
    LONG cmHitCellUs;                // +0x38CC, the part in would-hit legs (us, sum; read as ms/s)
    LONG cmHitCellN;                 // +0x38D0, cell A* calls in would-hit legs
    LONG cmMaxRepeats;               // +0x38D4, most repeats in one plan
    LONG cmHist[8];                  // +0x38D8, plans by repeats: 0, 1, 2-3, 4-7, 8-15, 16-31, 32-63, 64+
    LONG cmFull;                     // +0x38F8, legs past the 256-key list of a plan (not learned)
    // v90 (docs/150 §13, replandedupe.cpp; built at 0x38AC right after v89, moved after v88 to 0x38FC at the merge): [RouteAsync]
    // ReplanDedupe, repeated Replan / ReplanFromPosition calls ([0] Replan, [1] ReplanFromPosition) and the failed-repeat cut
    LONG rdMode;                     // +0x38FC, switch (--ab replandedupe): 1 = serve a failed repeat within 500 ms, 0 = observe
    LONG rdReady;                    // +0x3900, bit 0 Replan, 1 ReplanFromPosition covered, 2 world version seen, 3 own detour
    LONG rdCalls[2];                 // +0x3904, IC calls (served ones included)
    LONG rdUs[2];                    // +0x390C, their planned time (served ones excluded; shortcut: us, wraps after 35 min)
    LONG rdNoKey;                    // +0x3914, calls without a key (no route, no follower, follower position failed, fault)
    LONG rdFails[2];                 // +0x3918, planned calls whose result type is not 1 Succeeded
    LONG rdRepN[3][3];               // +0x3920, repeats (same follower, IC, goal, start within 10 cm) by time since the previous
                                     //   plan's end [<= 250, <= 500, <= 1000 ms, cumulative] x [failure with the same answer,
                                     //   success with the same answer, another answer]
    LONG rdRepUs[3][3];              // +0x3944, their planned time
    LONG rdInval[4];                 // +0x3968, failed repeats within 500 ms not eligible: 0 route state changed, 1 footprint writes
                                     //   with BlockedByPeople / BlockedByDynamicFootprints, 2 other routing writes, 3 other (no
                                     //   world version, world changed or not quiet, result 18, P4)
    LONG rdWould[2];                 // +0x3978, eligible repeats that were planned (rdMode 0) [nothing changed, footprints only]
    LONG rdWouldSame[2];             // +0x3980, of those: the plan gave the cached answer (the live equivalence check)
    LONG rdWouldDiff[2];             // +0x3988, of those: another answer (must stay 0 for [0])
    LONG rdServed;                   // +0x3990, failed repeats answered from the cache (rdMode 1)
    LONG rdServedUs;                 // +0x3994, the cached plans' time (the time a served call would have planned, estimate)
    // v91 (docs/152 §G1, gate0ro.cpp; built at 0x38AC right after v89, moved after v90 to 0x3998 at the merge):
    // [Gate0RO], gate-0 G-a hits under QL shared while a job runs
    LONG g0Mode;                     // +0x3998, switch (--ab gate0ro): 1 = answer G-a hits without the +0x918 wait
    LONG g0Shadow;                   // +0x399C, switch (--ab gate0shadow): 1 = predict and compare G-a hits with no job running
    LONG g0Ready;                    // +0x39A0, 1 = installed (QL writer subset in place)
    LONG g0Calls;                    // +0x39A4, G-a gate-0 entries (second frame 0x846E0E/0x85B57E/0x85F76D/0x85F7A9), job running
    LONG g0Served;                   // +0x39A8, of those: run under QL shared, no wait
    LONG g0Fb[4];                    // +0x39AC, waited as today: 0 S2 or AvoidRO off, 1 QL nested/busy/broken, 2 lot-table miss
                                     //   or odd table, 3 not the owner thread or serving a job callback
    LONG g0Checks;                   // +0x39BC, predicted entries compared with GetOrInsert's return (served + shadow)
    LONG g0Mismatch;                 // +0x39C0, differences (an insert ran): must stay 0
    LONG g0Other;                    // +0x39C4, gate-0 entries from other second frames (G-b, script ICs), job running, mode on
    // v92 (docs/150 §14, routephases.cpp; built at 0x38AC right after v89, moved after v91 to 0x39C8 at the merge):
    // allocator call sites on the route worker inside a job, recorded with [RouteJobPhases] on (observe only)
    LONG raAllocN;                   // +0x39C8, EA_AllocTagged 0x4E3F90 calls timed on the worker inside a job
    LONG raFreeN;                    // +0x39CC, EA_Free 0x4E3F70 calls timed there
    LONG raFull;                     // +0x39D0, calls not recorded per site: the 64-row table was full
    LONG raJobHist[8];               // +0x39D4, jobs by allocator calls (alloc + free) per job: 0, 1-15, 16-63, 64-255,
                                     //   256-1023, 1024-4095, 4096-16383, >= 16384
    LONG raSizeLog2[16];             // +0x39F4, allocs by size: bucket i = [2^(i+2), 2^(i+3)) bytes (0: < 8, 15: >= 128 KB)
    LONG raInsCap[16];               // +0x3A34, PathNodes_Insert 0x629140 grows (alloc at 0x6291DE) by new capacity log2 (64-byte nodes)
    LONG raInsUs[16];                // +0x3A74, self time of those allocs per bucket
    LONG raSite[64];                 // +0x3AB4, row key 1: return address of the allocator call (the call site + 5)
    LONG raParent[64];               // +0x3BB4, row key 2: return address of the innermost open phase frame (the call into that
                                     //   phase function; an owner-interposed phase shows the owner's leave thunk, 0 = none)
    LONG raN[64];                    // +0x3CB4, calls per row
    LONG raUs[64];                   // +0x3DB4, allocator self time per row (us)
    LONG raBytes[64];                // +0x3EB4, bytes requested per row (allocs only)
    // v94 (docs/89 §12, discourage.cpp; built at 0x38AC right after v89, moved after v92 to 0x3FB4 at the merge; no v93): design U
    // phase 2, [Discourage] DeferFootprint2; [kind] 0 = ObjectFootprintFromTool 0x8334A0, 1 = UpdateObjectLot 0x82D280
    LONG fp2Mode;                    // +0x3FB4, switch (--ab deferfootprint2): 1 = defer while a job runs (order rule in any mode)
    LONG fp2Ready;                   // +0x3FB8, 1 = both detours and the two flush call sites installed
    LONG fp2Deferred[2];             // +0x3FBC, records queued
    LONG fp2Applied[2];              // +0x3FC4, records applied with the original (drains and D-X destroys)
    LONG fp2Dropped[2];              // +0x3FCC, records dropped at apply (object gone, id reused, another world)
    LONG fp2Dup;                     // +0x3FD4, calls dropped: the object's newest record is the same call
    LONG fp2Pass;                    // +0x3FD8, calls run now by rule (+0x918 owned, job callback, drain, world, closing, batch mode)
    LONG fp2Forced;                  // +0x3FDC, flushes forced by a reader of the object (private-footprint ICs) or a full queue
    LONG fp2FullPass;                // +0x3FE0, queue full with an Add queued: the call ran now, out of order (expected 0)
    LONG fp2AgeMaxMs;                // +0x3FE4, oldest record at its drain
    LONG fp2ApplyInJob;              // +0x3FE8, tripwire: an apply while a job runs (must stay 0)
    // v95 (docs/155 §8, p5b.cpp; 2026-10-22 integration merge): gate-12 F-G dropped from the lent-window admission set
    LONG p5bFgHeldOut;               // +0x3FEC, F-G (key 0x631F40) acquires held out by an open P5b / TailUnlock window (also in p5bHeldOut)
    // v93 (docs/140 §12, rtoro.cpp; built at 0x38AC right after v89, appended after v95 to 0x3FF0 at the 0325 merge; version field stays 95):
    // [RouteToObjRO], RoutingWorld_RouteToObjectCheck 0x831530 answered read-only while a route job holds +0x918
    LONG rtoReady;                   // +0x3FF0, bit 0 R-acq 0x831568, 1 R-call 0x831633, 2 R-rel 0x8316FF, 3 SH ([SlotEnabledRO]) in place
    volatile LONG rtoMode;           // +0x3FF4, switch (--ab routetoobjro): 1 = while a job runs, skip the lock and answer from the clone
    volatile LONG rtoShadow;         // +0x3FF8, switch (--ab routetoobjroshadow): 1 = with no job, the clone then the game's 0x830340, compared
    LONG rtoCalls;                   // +0x3FFC, R-acq skips (+0x918 not taken; a clone answers or the lock is taken late)
    LONG rtoTaken;                   // +0x4000, clone answers (served read-only)
    LONG rtoNoRoute;                 // +0x4004, skips whose call had no route part (no lock needed at all)
    LONG rtoWaitAvoided;             // +0x4008, answers while another thread owned +0x918 (the original would have waited)
    LONG rtoSlot;                    // +0x400C, phase S runs (the slot helper 0x76F0A0 through SH)
    LONG rtoFb[12];                  // +0x4010, fallbacks (+0x918 taken late, the game's 0x830340): 0 state, 1 QL busy, 2 dirty lot
                                     //   index, 3 lot not in table, 4 deferred quadtree / pin moved, 5 polygon cache, 6 shape or slot,
                                     //   7 tripwire, 8 physics tail (BlockerCheck2d option 4 with no hit), 9 option 0x100000, 10 SH
                                     //   off, 11 job end
    LONG rtoUs;                      // +0x4040, clone time of the answers (sum, us)
    LONG rtoMaxUs;                   // +0x4044
    LONG rtoCode[9];                 // +0x4048, answered result codes 0..8
    LONG rtoShadowRuns;              // +0x406C
    LONG rtoShadowMismatch;          // +0x4070, code or id differ from the game's 0x830340 (must stay 0)
    LONG rtoShadowQueryMismatch;     // +0x4074, step-2 obstacle query: footprint set differs from 0x82EDB0's (must stay 0)
    LONG rtoShadowOrder;             // +0x4078, same set, other order
    LONG rtoShadowFb;                // +0x407C, shadow runs where the clone gave up (reasons in rtoFb)
    LONG rtoSimWriteInRo;            // +0x4080, sim-thread W1-W6 entry during a clone (must stay 0)
    LONG rtoNested;                  // +0x4084, R-acq entries nested deeper than the state stack (taken as the game's path)
    // v96 (docs/159 §5 and §4 H2, hostcalls.cpp): [RouteJobPhases] HostSplit=1 (observe only) and [RouteAsync] DoorMemo.
    // Sites [6]: 0 lock pump +0x918, 1 lock pump other lock, 2 READY poll, 3 WaitAsyncDone, 4 WaitJobDone, 5 RunPlan
    LONG hcReady;                    // +0x4088, bit 0 HostSplit stamps on, 1 door stub at 0x64F4D3 installed, 2 limiter ring on
    volatile LONG dmMode;            // +0x408C, switch (--ab doormemo): 0 ask every time, 1 memo within one job, 2 shadow (ask, compare)
    LONG hcN[6];                     // +0x4090, proxied calls picked up per serve site (worker in a job)
    LONG hcQueueUs[6];               // +0x40A8, post -> pick-up per site (us)
    LONG hcQueueMaxUs;               // +0x40C0
    LONG hcServeUs;                  // +0x40C4, pick-up -> done on the sim thread, nested plan time taken out
    LONG hcServeMaxUs;               // +0x40C8
    LONG hcReturnUs;                 // +0x40CC, done -> worker awake
    LONG hcNestedUs;                 // +0x40D0, plans the call sent back to the worker (ServePlan inside Proxy)
    LONG hcQueueSleepUs;             // +0x40D4, queue time inside the frame-limiter window (sim pump return -> loop-back)
    LONG hcSleepN;                   // +0x40D8, limiter passes that ended with a call pending
    LONG hcOtherCalls;               // +0x40DC, proxied calls not made by the door stub's ask
    LONG hcDoorQ;                    // +0x40E0, door asks at 0x64F4D3 on the worker in a job (not nested)
    LONG hcDoorRepeatJob;            // +0x40E4, asks whose key was already asked in the same job
    LONG hcDoorRepeatJobWaitUs;      // +0x40E8, time those repeats spent in the ask (= H2's direct gain)
    LONG hcDoorRepeatDiff;           // +0x40EC, repeats answered differently from the first ask (must stay 0)
    LONG hcDoorRepeatXJob;           // +0x40F0, first asks of a job whose key another job asked within 1 s (sizes H4)
    LONG hcDoorRepeatXJobDiff;       // +0x40F4, of those, a different answer
    LONG hcDoorFull;                 // +0x40F8, first asks not tracked: the job's 64-row table was full
    LONG hcDoorWaitUs;               // +0x40FC, time all door asks spent in the ask
    LONG dmHits;                     // +0x4100, DoorMemo=1: repeats answered from the job's memo (no ask)
    LONG dmMisses;                   // +0x4104, first asks of a key in the job (asked)
    LONG dmFb[4];                    // +0x4108, passed through: 0 not the worker in a job, 1 plan nested in a managed call, 2 table
                                     //   full (asked, not kept), 3 clone run
    LONG dmShadow;                   // +0x4118, DoorMemo=2: repeats asked anyway and compared
    LONG dmMismatch;                 // +0x411C, of those, a different answer (must stay 0)
    // v97 (docs/164, syncphases.cpp): [RouteJobPhases] SyncPlans=1, observe only: the phase timers on sim-thread sync plans.
    // Phase index = RJP_* (routephases.cpp); loop index 0 goal-lot entry loop (world A* from 0x65F809), 1 open-ground goals
    // (0x65FB99 / 0x65FDA5), 2 circle pre-pass (0x6762B4, nested in its outer call), 3 other caller
    LONG sypMode;                     // +0x4120, 1 = sessions armed (Route_PlanLocked site timed)
    LONG sypPlans[3];                 // +0x4124, sync plans by result: succeeded, failed, no Route_PlanLocked ran
    LONG sypUs[3];                    // +0x4130, their summed time (us)
    LONG sypMaxUs;                    // +0x413C
    LONG sypEntry[2];                 // +0x4140, sessions opened at: 0 the route IC's sync path (SyncIc), 1 Route_PlanLocked
    LONG sypBusy;                     // +0x4148, sim plans not timed: another thread's session was open
    LONG sypStale;                    // +0x414C, sessions ended by a later entry (the opening frame was unwound)
    LONG sypPhaseUs[15];              // +0x4150, self time by phase, all sync plans (us)
    LONG sypLongN;                    // +0x418C, plans >= [RouteAsync] LogFallbackMs (one log line each, rate-limited)
    LONG sypLongPhaseUs[15];          // +0x4190, self time by phase of those plans
    LONG sypLoopN[4];                 // +0x41CC, world A* calls by caller (loop index above)
    LONG sypLoopUs[4];                // +0x41DC, their inclusive time (us)
    LONG sypLongLoopN[4];             // +0x41EC, the same for plans >= LogFallbackMs
    LONG sypLongLoopUs[4];            // +0x41FC
    LONG sypLines;                    // +0x420C, split lines logged
    LONG sypLinesDropped;             // +0x4210, split lines over the 20 per minute limit
    // v98 (0.36.25, docs/165 §8 gap #2): [HitchAttr] Holders=1, sim/main waits >= MinMs on any game Lock_Acquire (hitchattr.cpp)
    LONG hoReady;                     // +0x4214, 1 = Holders on
    LONG hoWaits[2];                  // +0x4218, waits >= MinMs: 0 sim thread, 1 main thread
    LONG hoUs[2];                     // +0x4220, their time (us)
    LONG hoMaxUs;                     // +0x4228, the longest
    LONG hoByRole[5];                 // +0x422C, waits by holder: 0 sim, 1 main, 2 route worker, 3 replay consumer, 4 other / unknown
    LONG hoNoSite;                    // +0x4240, waits whose holder had no acquire site in the side table (evicted from its 4-way set, held before on)
    // v99 (0.36.27, docs/164 §7, plandeadline.cpp): [RouteAsync] PlanDeadlineMs (D1-D4) and FailMemo (D5)
    LONG pdMode;                      // +0x4244, switch (native_set plandeadline): 1 = cut sync plans at their deadline, 0 = off
    LONG pdUs;                        // +0x4248, deadline of a no-own-task IC plan (NoStart, Foreign) and of a non-IC sim plan (us)
    LONG pdOwnUs;                     // +0x424C, deadline of an own-task IC plan (every other reason); 0 = never cut
    LONG pdReady;                     // +0x4250, bit 0 world A* cost stubs, 1 cell A* entry, 2 water entry
    LONG pdSessions[2];               // +0x4254, sync plans run with a deadline: 0 route IC sync path, 1 Route_PlanLocked (no IC)
    LONG pdHit;                       // +0x425C, of those, plans still running at their deadline
    LONG pdHitOk;                     // +0x4260, of those, plans that still succeeded (found before the cut, or no search needed)
    LONG pdHitUs;                     // +0x4264, the hit plans' whole time (us)
    LONG pdLateMaxUs;                 // +0x4268, longest end past a deadline (us): what the cut could not stop
    LONG pdSkipCell;                  // +0x426C, cell A* 0x61E500 entries answered "no path" past the deadline
    LONG pdSkipWater;                 // +0x4270, water 0x671930 entries answered "no path" past the deadline
    LONG pdWorldCapped;               // +0x4274, Route_PlanLocked calls in a deadline session whose world A* drained capped
    LONG pdOwnExempt;                 // +0x4278, own-task IC plans run without a deadline (pdOwnUs 0)
    LONG pdLines;                     // +0x427C, "route deadline:" lines logged (20 per minute)
    LONG fmMode;                      // +0x4280, switch (native_set failmemo): 1 = serve a failed repeat within 500 ms, 0 = observe
    LONG fmReady;                     // +0x4284, 1 = Plan IC sync path through the memo
    LONG fmCalls;                     // +0x4288, sync Plan ICs through the memo
    LONG fmNoKey;                     // +0x428C, no key (no route, no follower, no follower position)
    LONG fmFails;                     // +0x4290, of the planned ones, failures
    LONG fmRep;                       // +0x4294, repeats of a failed plan within 500 ms (same follower, goal, start within 10 cm)
    LONG fmInval[4];                  // +0x4298, repeats not eligible: route state, footprints with BlockedByPeople, routing writes, other
    LONG fmWould[2];                  // +0x42A8, eligible repeats that were planned (fmMode 0): [nothing changed, footprints only]
    LONG fmWouldSame[2];              // +0x42B0, of those, the same answer
    LONG fmWouldDiff[2];              // +0x42B8, of those, another answer (must stay 0 before fmMode 1 is trusted)
    LONG fmServed;                    // +0x42C0, failed repeats answered from the memo (fmMode 1)
    LONG fmServedUs;                  // +0x42C4, the served entries' own plan time (us): what the memo saved
    // v100 (0.36.28, docs/174, fgro.cpp): [PlacementRO] gate-12 F-G (FootprintsNearPoint 0x631F10 from 0x634F10) read-only during jobs
    LONG fgReady;                     // +0x42C8, 1 = call site 0x635013 retargeted
    volatile LONG fgMode;             // +0x42CC, switch (--ab placementro): 1 = while a job runs, answer from the clone
    volatile LONG fgShadow;           // +0x42D0, switch (--ab placementroshadow): 1 = with no job, the game's call then the clone, compared
    LONG fgCalls;                     // +0x42D4, sim calls with a switch on
    LONG fgTaken;                     // +0x42D8, answered read-only during a job (no +0x918 / gate wait)
    LONG fgHits;                      // +0x42DC, of those, answer 1 (a footprint near the point)
    LONG fgUs;                        // +0x42E0, their clone time (us)
    LONG fgMaxUs;                     // +0x42E4, the longest
    LONG fgFb[6];                     // +0x42E8, the game's call instead: state (owner, callback, gate frame, QL nested, mode 0), QL busy, lot/deferred flush, shape, tripwire, job ended
    LONG fgShadowRuns;                // +0x4300, shadow compares (no job)
    LONG fgShadowMismatch;            // +0x4304, of those, a different answer (must stay 0)
    // v101 (0.36.29, docs/176, a10.cpp): [RouteAsync] ForeignKinds, A10 jobs for Replan-family calls from a yielding task that
    // is not the route's Sim (vehicle/boat followers, other tasks); arrays by kind - 1 (Replan, PlanFromPoint, ReplanFromPosition)
    LONG afMode;                      // +0x4308, switch (native_set / --ab a10foreign): 1 = start foreign kind STARTs, 0 = refuse "Foreign"
    LONG afKinds;                     // +0x430C, ForeignKinds bits whose collect detour is in place
    LONG afSeen[3];                   // +0x4310, foreign kind STARTs received (any mode; the NoStart share ts3fixes now names)
    LONG afStarted[3];                // +0x431C, of those, queued on the worker
    LONG afActive[3];                 // +0x4328, refused: route being followed (route+0x60)
    LONG afNoYield[3];                // +0x4334, refused: nested native->managed call (CanYield false)
    LONG afCollected[3];              // +0x4340, worker results taken by the IC
    LONG afJobUs[3];                  // +0x434C, their worker time (us): plan time moved off the sim thread
    LONG afJobMaxUs;                  // +0x4358, the longest such job (us)
    LONG afActiveTrip;                // +0x435C, tripwire: the route started being followed during the job (expected 0)
    // v102 (0.36.30, docs/177 §5): probe P-G ([RouteJobPhases] GoalSplit=1, routephases.cpp), design D1 ([GoalGen] SameRing,
    // goalgen.cpp) and the H2 IC window ([RouteAsync] H2Ic, locowin.cpp). pg* arrays by phase: 0 goal generation 0x751FF0,
    // 1 goal step 0x75C2D0, 2 EditPathForRoute 0x85FF30, 3 GoalPoint_Validate 0x759B20 (worker jobs only; self time, so with
    // GoalSplit on v87's RJP_OTHER no longer holds these four: "rest of job" = rjpUs OTHER + pgUs)
    LONG pgReady;                     // +0x4360, bits of the P-G sites timed (1 goal gen, 2 goal step, 4 EditPathForRoute, 8 validate)
    LONG pgN[4];                      // +0x4364, frames by phase
    LONG pgUs[4];                     // +0x4374, self time by phase (us)
    LONG pgMaxUs[4];                  // +0x4384, longest self time of one frame (us)
    LONG pgValHist[6];                // +0x4394, validations per job: 0, 1-18, 19-35, 36-70, 71-200, > 200
    LONG pgValMax;                    // +0x43AC, most validations in one job
    LONG pgLongRest;                  // +0x43B0, jobs whose rest of job (OTHER + the four phases) was >= 50 ms (logged with the split)
    volatile LONG ggMode;             // +0x43B4, switch (native_set goalgen / --ab goalgen): 0 off, 1 skip the duplicate ring, 2 shadow
    LONG ggReady;                     // +0x43B8, 1 = call-site thunk at 0x75C246; 3 = also the ring count byte patch at 0x75C060
    LONG ggRings;                     // +0x43BC, ring builder calls through the site (any thread)
    LONG ggDup;                       // +0x43C0, of those, the duplicate ring (count 1, i 1, step 0): what D1 removes
    LONG ggSkipped;                   // +0x43C4, duplicates skipped (mode 1)
    LONG ggArgMismatch;               // +0x43C8, shadow: a duplicate whose ecx and 15 arguments differ from its ring 0 (expected 0)
    LONG ggValCmp;                    // +0x43CC, shadow: duplicate rings whose validation answers were compared (worker, GoalSplit)
    LONG ggValMismatch;               // +0x43D0, of those, an answer or the count differed (the sim moved something between the rings)
    LONG ggValDup;                    // +0x43D4, shadow: validations inside duplicate rings (what mode 1 saves; worker, GoalSplit)
    volatile LONG hxMode;             // +0x43D8, switch (native_set h2ic / --ab h2ic): 1 = no job starts while a listed script IC runs
    LONG hxReady;                     // +0x43DC, IC binding slots swapped (of 7)
    LONG hxCalls[7];                  // +0x43E0, sim-thread calls by IC: GetDestinationObstruction, GetObstructionAtPoint, IsPointObstructedBySim, FindObstaclesForSimAlongRoute, SetAGS, InsertRouteSubPathsAtIndex, InsertCustomPathAtIndex
    LONG hxOffSim;                    // +0x43FC, calls on another thread (passed through, no window)
    LONG hxHeld;                      // +0x4400, job starts held first while an IC window was open
    LONG hxRunningAtEntry;            // +0x4404, IC windows opened while a job ran (the IC's first +0x918 acquire waits for it)
    LONG hxStale;                     // +0x4408, IC frames dropped as unwound (an exception left the IC; window closed at the next entry)
    // v103 (0.36.31, docs/178 §6): T1 [RouteAsync] FbJobDeadline (plandeadline.cpp worker slot), T4 [RouteAsync] OwnJobWait,
    // T3 probe P-GS ([RouteJobPhases] GoalStepSplit, routephases.cpp). Buckets as v84: <100, <300, <500, <1000, <2000, >=2000 ms
    volatile LONG fbjdMode;           // +0x440C, switch (native_set fbjobdeadline / --ab fbjobdeadline): 1 = a foreign job's cell A* and
                                      //          water searches take the plan-deadline no-path exits once FbJobCapMs has passed
    LONG fbjdReady;                   // +0x4410, bit 0 cell A* stub, 1 water stub, 2 per-phase long-job line ([RouteJobPhases] Enabled=1)
    LONG fbjdSkip[2];                 // +0x4414, searches answered "no path" on the worker past a foreign job's deadline [cell, water]
    LONG fbjdCapped[3];               // +0x441C, foreign jobs cut at least once in [world A* (FbJobCapMs cost stubs), cell A*, water]
    LONG fbjdLateUs;                  // +0x4428, summed job end minus deadline of cut foreign jobs (work no exit could stop)
    LONG fbjdLateMaxUs;               // +0x442C, the longest of those
    LONG fbjdLong[2];                 // +0x4430, foreign jobs >= 300 / >= 1000 ms (either mode)
    LONG fbjdLines;                   // +0x4438, per-phase long-job lines logged (foreign jobs >= 300 ms)
    LONG owReady;                     // +0x443C, T4: 1 = sim waits behind own (non-foreign) jobs counted
    LONG owWaits;                     // +0x4440, sim-thread waits (+0x918 pump, RtWaitJob) with an own job running
    LONG owWaitUs;                    // +0x4444, their summed time
    LONG owWaitMaxUs;                 // +0x4448, the longest
    LONG owWaitHist[6];               // +0x444C, by length
    LONG owLines;                     // +0x4464, waits >= 300 ms logged
    LONG gsReady;                     // +0x4468, P-GS sites timed (bit i = site i: 0 lot planner 0x63F490, 1 room graph 0x63D2F0,
                                      //          2 LotPortalSearch 0x63EC40, 3 Q reset 0x61E320, 4 obstacle query 0x82EDB0,
                                      //          5 gather 0x82F0B0, 6 range query 0x83B3A0, 7 lot index rebuild 0xC68680)
    LONG gsN[8];                      // +0x446C, frames directly inside a goal step 0x75C2D0 (worker jobs), by site
    LONG gsUs[8];                     // +0x448C, their self time (us)
    LONG gsMaxUs[8];                  // +0x44AC, longest self time of one frame (us)
    LONG gsSteps;                     // +0x44CC, goal steps closed with P-GS on
    LONG gsLong;                      // +0x44D0, goal steps >= 50 ms (logged with the split)
    // v104 (0.36.32, docs/177 §7): [RouteAsync] RefcountGuard (rcguard.cpp). The sim keeps the +0x918 level that 0x82EDB0 /
    // 0x82F0B0 release at 0x82F098 / 0x82F45C until the query's caller returns, so the caller's plain [fp+4] writes never run
    // beside a job. rcByFn by caller: 0 RouteObstructionAtPoint 0x655880, 1 IsPointObstructedBySim 0x751480, 2 IsOkToIdle
    // 0x7556B0, 3 0x755EA0, 4 GoalPoint_Validate 0x759B20, 5 Route_GoalStep 0x75C2D0, 6 0x8243E0, 7 0x830340, 8 RouteFollow_A
    // 0x846B70, 9 RouteFollow_B 0x85B470, 10 RouteFollow_EditPath 0x85F5A0, 11 Locomotion_ObstacleCheck 0x85FD10
    volatile LONG rcMode;             // +0x44D4, switch (native_set refcountguard / --ab refcountguard): 1 = keep the level, 0 = count only
    LONG rcReady;                     // +0x44D8, 1 = both release call sites retargeted
    LONG rcDeferred;                  // +0x44DC, query releases kept on the sim thread (one per non-empty query of a listed caller)
    LONG rcByFn[12];                  // +0x44E0, of those, by caller
    LONG rcPass[5];                   // +0x4510, released as before: 0 no hits, 1 caller not listed / no steering key / other lock,
                                      //          2 serving a worker callback (worker waits), 3 frame stack full, 4 caller frame check failed
    LONG rcOffSim;                    // +0x4524, releases at the two sites on other threads (the worker: inside its job's hold)
    LONG rcFrames;                    // +0x4528, caller frames opened (return slot swapped)
    LONG rcLeaves;                    // +0x452C, closed at the caller's return (levels released there)
    LONG rcDropped[2];                // +0x4530, levels released early: 0 before a sim wait for the worker, 1 frame left by an unwind
    LONG rcHoldUs;                    // +0x4538, summed time from a frame's first kept level to its release (us): the extra hold
    LONG rcHoldMaxUs;                 // +0x453C, the longest
    LONG rcBusyAtOpen;                // +0x4540, frames opened while a job was queued, claimed or running (the guard mattered)
    LONG rcWould;                     // +0x4544, Mode 0: releases the guard would have kept
    // v105 (0.36.34, docs/182): [RouteSnap] RecsUnlocked (replay.cpp RpRecsUnlocked). The replay consumer applies INS / REM / NOQ /
    // STALE / SYNC / EPOCH records to the clone holding g_rpUse and the private pool set, not +0x918 (the clone is ours; the
    // game's ApplyInsert / ApplyRemove closure touches no lock-guarded live state, docs/182 §2)
    LONG ulMode;                      // +0x4548, 1 = on ([RouteSnap] RecsUnlocked)
    LONG ulRounds;                    // +0x454C, rounds applied outside +0x918
    LONG ulRecs;                      // +0x4550, records applied in them
    LONG ulUs;                        // +0x4554, summed round time (us): consumer time no longer under +0x918
    LONG ulMaxUs;                     // +0x4558, the longest round
    LONG ulOpBusy;                    // +0x455C, rounds refused because the pool set was busy (a P4 run or the sim's cut owns it)
    LONG ulLockTrip;                  // +0x4560, routing-lock acquires reached on the consumer inside a round (expected 0)
    LONG ulFaults;                    // +0x4564, faults in a round (clone dropped, rebuilt at the next cut)
    // v106 (0.36.35, docs/157 §4, docs/156 F1): P5 steps 2-4 (built without v105 on its branch; placed after v105's fields at
    // the 2026-10-23 merge). Lock-key writer cover (replay.cpp RpLockKeyEnter at 0x82B3A1, the
    // Footprint_SetLockKeyObject call site in RouteManager_FootprintLockKeyApply), replayed-insert answer check (RpInsCheck),
    // P5b retake watchdog escalation (p5b.cpp) and the Mode=2 refusal-storm breaker (p5.cpp)
    // (PlanDrain branch: the v106 layout of merge eaef8a1b kept as is so v107 sits at its final offset; nothing writes these
    // fields in a build without that merge, they read 0)
    LONG rpLockKeyForce;              // +0x4568, lock-key set swaps on an inserted footprint whose lots were forced to rebase
    LONG rpLockKeyNoLot;              // +0x456C, swaps on a footprint with no lot Q (Q-less nodes refreshed at the next cut)
    LONG rpInsDiff;                   // +0x4570, replayed inserts whose clone copy answered other bounds than live at the record
    LONG rpInsForced;                 // +0x4574, of those, lots forced to rebase (a difference over 0.01 or flat on one side)
    LONG p5bWdEscalate;               // +0x4578, retakes past EscalateMs: lending (P5b and P5c) off for the session
    LONG p5StormTrips;                // +0x457C, refusal storms: StormRefusals tried refusals in a row paused Mode=2 for StormPauseMs
    LONG p5StormSkips;                // +0x4580, Mode=2 plans answered by the game's plan with no acquire during a storm pause
    LONG p5StormRunMax;               // +0x4584, longest run of tried refusals in a row
    // v107 (0.36.36, docs/89 §13): [Discourage] PlanDrain (discourage.cpp DiscDrainForPlan). A sim-thread sync plan applies D's
    // deferred footprint records first (DiscDrain), so it sees the world DeferFootprint=0 would give it
    LONG sdReady;                     // +0x4588, 1 = installed (needs design U)
    volatile LONG sdMode;             // +0x458C, switch (native_set plandrain / --ab plandrain): 1 = drain before the plan
    LONG sdChecks;                    // +0x4590, owner-thread sync plans that reached Route_PlanLocked with records pending
    LONG sdDrains;                    // +0x4594, drains run before the plan
    LONG sdRecs;                      // +0x4598, records queued at those drains (summed)
    LONG sdJobRan;                    // +0x459C, drains that waited for a running job (the plan would have waited too)
    LONG sdUs;                        // +0x45A0, summed drain time including the job wait (us)
    LONG sdMaxUs;                     // +0x45A4, the longest
    LONG sdSkip[5];                   // +0x45A8, skipped: 0 Add queued, 1 job callback, 2 +0x918 already owned, 3 P5c lent, 4 draining
    LONG sdFail[2];                   // +0x45BC, plans that failed: 0 after a drain, 1 after a skip
    // v108 (0.36.37, docs/67 §13 crash K): [RouteGate] gate site 16 IC_World_FindPlacesOnShoreline 0x78C440 (routegate.cpp
    // index 15), counted as rgUnlocked / rgJob are for sites 0-14
    LONG rgUnlocked3;                 // +0x45C4, outermost entries without +0x918 at gate site 16
    LONG rgJob3;                      // +0x45C8, of those, entries while a route job ran
};
extern "C" __declspec(dllexport) Ts3nCounters ts3native_counters = {108,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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

static void PrLog();  // planrepeat.cpp
static void EoLogCounters();  // earlyout.cpp

static void MaybeLogCounters(ULONGLONG now) {
    LONG64 next = g_nextLog;
    if ((LONG64)now < next || InterlockedCompareExchange64(&g_nextLog, (LONG64)now + 60000, next) != next) return;
    const Ts3nCounters& c = ts3native_counters;
    if (c.prReady) PrLog();
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
    if (c.asyncReady && (c.fbMode || c.fbForeign || c.fbCapUs))  // docs/134
        Log("route fallback: mode %ld foreign %ld started %ld pending applied %ld busy %ld deep %ld capped %ld stacks %ld activeTrip %ld",
            c.fbMode, c.fbForeign, c.fbForeignStarted, c.fbPendApplied, c.fbPendBusy, c.fbDeepQueued, c.fbCapped, c.fbStacks, c.fbActiveTrip);
    if (c.usfReady)  // docs/89 design U
        Log("footprint defer: mode %ld calls %ld inJob %ld deferred %ld applied %ld collapsed %ld dup %ld ageMax %ld ms applyInJob %ld",
            c.usfMode, c.usfCalls, c.usfInJob, c.usfDeferred, c.usfApplied, c.usfCollapsed, c.usfDup, c.usfAgeMaxMs, c.usfApplyInJob);
    if (c.fp2Ready)  // docs/89 §12 design U phase 2
        Log("footprint defer 2: mode %ld deferred %ld/%ld applied %ld/%ld dropped %ld/%ld dup %ld pass %ld forced %ld fullPass %ld ageMax %ld ms applyInJob %ld",
            c.fp2Mode, c.fp2Deferred[0], c.fp2Deferred[1], c.fp2Applied[0], c.fp2Applied[1], c.fp2Dropped[0], c.fp2Dropped[1],
            c.fp2Dup, c.fp2Pass, c.fp2Forced, c.fp2FullPass, c.fp2AgeMaxMs, c.fp2ApplyInJob);
    if (c.asyncReady && (c.fbRetryMode || c.fbRetryParked))  // docs/134 §2.6
        Log("route fallback retry: mode %ld parked %ld queued %ld gave up %ld max ticks %ld", c.fbRetryMode, c.fbRetryParked,
            c.fbRetryQueued, c.fbRetryGaveUp, c.fbRetryTicksMax);
    if (c.asyncReady && c.asyncStarted)  // docs/134 §10
        Log("route jobs: longest foreign %ld us own %ld us, >= 200 ms foreign %ld own %ld; job cap mode %ld %ld ms capped %ld",
            c.fbjcMaxUs, c.ownJobMaxUs, c.fbjcLong, c.ownJobLong, c.fbjcMode, c.fbjcCapUs / 1000, c.fbjcCapped);
    if (c.asyncReady && c.fbForeignStarted)  // docs/134 §11: buckets <100, <300, <500, <1000, <2000, >=2000 ms
        Log("route jobs split: capAt max %ld ms late %ld tail max %ld ms, plan max %ld ms host max %ld ms, sim wait max %ld ms; "
            "foreign jobs %ld/%ld/%ld/%ld/%ld/%ld, sim waits behind them %ld/%ld/%ld/%ld/%ld/%ld", c.fbjcCapAtMaxUs / 1000, c.fbjcCapLate,
            c.fbjcTailMaxUs / 1000, c.fbjcPlanMaxUs / 1000, c.fbjcHostMaxUs / 1000, c.fbjcSimWaitMaxUs / 1000, c.fbjcJobHist[0],
            c.fbjcJobHist[1], c.fbjcJobHist[2], c.fbjcJobHist[3], c.fbjcJobHist[4], c.fbjcJobHist[5], c.fbjcWaitHist[0],
            c.fbjcWaitHist[1], c.fbjcWaitHist[2], c.fbjcWaitHist[3], c.fbjcWaitHist[4], c.fbjcWaitHist[5]);
    if (c.fbjdReady || c.owReady)  // docs/178 T1 / T4
        Log("route jobs T1/T4: deadline mode %ld ready 0x%lX, cut searches cell %ld water %ld, cut jobs world %ld cell %ld water %ld, "
            "late max %ld ms, foreign >= 300 ms %ld >= 1000 ms %ld; own-job sim waits %ld (%ld ms, max %ld ms) %ld/%ld/%ld/%ld/%ld/%ld",
            c.fbjdMode, c.fbjdReady, c.fbjdSkip[0], c.fbjdSkip[1], c.fbjdCapped[0], c.fbjdCapped[1], c.fbjdCapped[2],
            c.fbjdLateMaxUs / 1000, c.fbjdLong[0], c.fbjdLong[1], c.owWaits, c.owWaitUs / 1000, c.owWaitMaxUs / 1000, c.owWaitHist[0],
            c.owWaitHist[1], c.owWaitHist[2], c.owWaitHist[3], c.owWaitHist[4], c.owWaitHist[5]);
    if (c.avReady)  // docs/137
        Log("avoid RO: mode %ld shadow %ld hit %ld miss %ld (site %ld) B %ld fallbacks %ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld T-AV %ld "
            "shadow %ld (mismatch %ld B %ld query %ld order %ld simscan %ld) writeInRo %ld", c.avMode, c.avShadow, c.avTakenHit,
            c.avTakenMiss, c.avSiteSkip, c.avTakenB, c.avFb[0], c.avFb[1], c.avFb[2], c.avFb[3], c.avFb[4], c.avFb[5], c.avFb[6],
            c.avFb[7], c.avFb[8], c.avFb[9], c.avTripAv, c.avShadowRuns, c.avShadowMismatch, c.avShadowMismatchB,
            c.avShadowQueryMismatch, c.avShadowOrder, c.avShadowSimScan, c.avSimWriteInRo);
    if (c.eoReady) EoLogCounters();
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

// T-L2 (docs/84 §4.4): set while this thread holds QL shared (S2 and L1 clones); a lock, host or managed call then could
// wait for a job that needs QL exclusive (deadlock), so it must never happen
static __declspec(thread) LONG t_qlShared;
// Design SH (slotro.cpp): 1 while this thread holds QL shared over the game's 0x76F0A0, 2 once that hold was dropped. A lock,
// host or managed call inside the hold drops it (QL released first, then the call as today), so it never counts in T-L2.
static __declspec(thread) LONG t_slHold;
// [RouteSnap] Mode=1, P4 (p4.cpp, docs/94 §12, §17). A clone run = the route worker running the game's Route_PlanLocked on
// the working copy. g_snapRunTid (TEB thread id) and g_snapCtx select the copy in the T1-T4 thunks; written by the worker only.
// t_snapRun: the same test for C++ stubs (§12.4.3 pass-through rule). g_snNrIn: D2 sim-side non-routing writers in flight.
// t_p4Read: reader bits of the plan in the live run and the clone run (D5), bit 31 = collecting on this thread.
static volatile LONG g_snapRunTid;
static void* volatile g_snapCtx;
static __declspec(thread) LONG t_snapRun;
static __declspec(thread) LONG t_rpReplay;  // replay.cpp consumer applying records (its game calls are not writer records)
static __declspec(thread) LONG t_rpUl;      // replay.cpp consumer inside a RecsUnlocked round (no +0x918 held)
static volatile LONG g_snNrIn;
// 0.33.2 hang diagnostics (docs/94 §21.15): where the route worker and the replay consumer are, for the routing lock watchdog
// line in LockAcquireAsync. Worker: 0 idle, 1 live job, 2 shadow, 3 acquire: consumer wait, 4 acquire: g_rpUse / cut lock,
// 5 clone run, 6 release. Consumer: 0 idle, 1 g_rpUse held, 2 +0x918 held (applying), 3 record type in bits 8-15
static volatile LONG g_p4Phase, g_rpPhase;
static volatile DWORD g_rpTid;
static volatile LONG g_p4On;  // P4 installed: D2 handshakes, D3 physics lock, D5 counts
static volatile LONG g_nrN[8];  // D5 writer families counted in our stubs: [0] physics, [2] destroys, [3] lot index rebuilds, [7] display level
static __declspec(thread) LONG t_p4Read;
static __declspec(thread) LONG t_p4Cap;  // the worker captures the live job's Route_PlanLocked (args, answer, doors)
enum { P4R_PHYS = 1, P4R_TERRAIN = 2, P4R_LOTIDX = 4, P4R_VRAY = 8, P4R_ON = (LONG)0x80000000 };  // VRAY: Routing_PhysicsRayVertical 0x6583E0
static inline bool SnLive() { return g_snapRunTid != 0; }
static void P4Abort(int why);                                // p4.cpp
static void P4WorkerDirty(bool known, unsigned __int64 key);
static void P4Lazy();
static void P4LazyKeep();
static int RpCloneFpLots(const u8* fp, unsigned __int64* out, int cap);  // replay.cpp (W6b in a clone run, 0.35.21)
static void P4NrEnter();                                     // D2: returns with g_snNrIn held and no clone run live
enum { P4_AB_CLONE, P4_AB_NOVER, P4_AB_EPOCH, P4_AB_COLD, P4_AB_UNSUP, P4_AB_CAPTURE, P4_AB_NRBUSY, P4_AB_MAT, P4_AB_DOORMISS,
       P4_AB_DOOROVER, P4_AB_MANAGED, P4_AB_EVENT, P4_AB_LIVELOCK, P4_AB_LAZY, P4_AB_FAULT, P4_AB_OTHER, P4_AB_N };
enum { SLD_GATE, SLD_MISS, SLD_PORTAL, SLD_LOCK, SLD_MANAGED, SLD_TABLE };
static void SlDrop(int why);
static inline void QlSharedTrip(int slWhy) {
    if (t_slHold == 1) { SlDrop(slWhy); return; }
    if (t_qlShared) InterlockedIncrement(&ts3native_counters.ltQlHeldBlock);
}

// --- Route search budget (docs/11 Q3, docs/15 phase 4) ---
// World A* 0x676260 calls its cost function (cdecl, 8 args, float in ST0) through a pointer it stores at [esp+0x60]
// from one of five `mov [esp+0x60], imm32`. We swap the immediates for stubs `push orig; jmp CostGate`. Arg 5 is 1
// for an edge cost (checked against the 1e8 wall at 0x676683/0x6768A6/0x676B24) and 0 for the heuristic, which runs
// once per generated node right before it is allocated and pushed. Over budget, every edge cost returns the wall:
// nothing new is pushed, the open list drains (goal check on pop still runs), and the search leaves through its own
// "open list empty" exit 0x676BF7 (returns 0, no leak), i.e. an ordinary no-path failure.
// dl (docs/134 §11): absolute QPC deadline of a capped foreign job, 0 = none; calls = cost calls of either kind since then
struct Budget { LONG on; LONG nodes; LARGE_INTEGER t0; LONG capped; LONG maxUs; LONG maxNodes; LONGLONG dl; LONG calls; };
// Per thread: a plan on another thread (another world's lock, or the route worker) never sees or overwrites this one.
// Implicit TLS in a LoadLibrary'd DLL (the ASI loader) works on Vista+, existing threads included.
static __declspec(thread) Budget t_budget;
static LONG g_maxNodes, g_maxUs, g_onlyFlagged;
// docs/134 SyncCapMs: SyncIc sets the cap of the sim-thread sync fallback plan it runs; CallPlan notes a hit
static __declspec(thread) LONG t_fbCapUs, t_fbCapHit;
// docs/134 §11 (worker, one foreign job): FbJobCapMs as a deadline from the job's start (the sim waits for the whole job, not
// for its world A* nodes), when the cap fired, and the job's Route_PlanLocked / managed-call time. t_jobT0 0 = no foreign job
static __declspec(thread) LONGLONG t_fbDeadline, t_jobT0, t_jobCapAt;
static __declspec(thread) LONG t_jobPlanUs, t_jobHostUs;
// docs/178 T1 (plandeadline.cpp): this foreign job's cuts past t_fbDeadline [world A* cost stubs, cell A*, water]
static __declspec(thread) LONG t_fbjdCut[3];
static const float kWall = 1e9f;  // >= the planner's 1e8 blocked-edge constant at 0x00FEF678

static void BudgetCap(Budget& b) {
    b.capped = 1;
    InterlockedIncrement(&ts3native_counters.budgetCapped);
    InterlockedExchangeAdd(&ts3native_counters.budgetCapNodes, b.nodes);
    if (t_jobT0 && !t_jobCapAt) {
        LARGE_INTEGER n;
        QueryPerformanceCounter(&n);
        t_jobCapAt = n.QuadPart;
    }
}

static int __cdecl BudgetStep(DWORD costArg5) {
    Budget& b = t_budget;
    if (!b.on) return 0;
    if (b.capped) return costArg5 == 1;
    // docs/134 §11: a job deadline is read on every 4th cost call of either kind, the first one included, so neither work
    // before the first node nor slow expansions with few generated nodes (BP 2026-10-22: 32 nodes in 1,942 ms) delay it
    if (b.dl && (b.calls++ & 3) == 0) {
        LARGE_INTEGER n;
        QueryPerformanceCounter(&n);
        if (n.QuadPart >= b.dl) {
            BudgetCap(b);
            return costArg5 == 1;
        }
    }
    if (costArg5 != 0) return 0;
    // clock starts at the first node, after Route_PlanLocked took the routing lock: lock wait is not search time
    if (b.nodes++ == 0) QueryPerformanceCounter(&b.t0);
    if ((b.maxNodes && b.nodes > b.maxNodes) || (b.maxUs && (b.nodes & 15) == 0 && UsSince(b.t0) > b.maxUs)) BudgetCap(b);
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
// CallPlan (earlyout.cpp) wraps it in the [RouteEarlyOut] request scope.
static u8 CallPlan(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                   int* out1, int* out2, DWORD guidLo, DWORD guidHi);
// docs/164 §7 (plandeadline.cpp): a sim-thread Route_PlanLocked outside a deadline session opens its own (sets t_fbDeadline,
// read below); PdCapped counts a session's capped world A*
static volatile LONG g_pdOn;
// docs/178 T1: the worker's id while it runs a foreign job with a deadline (the cell / water entry stubs' second compare)
static volatile DWORD g_pdWkTid;
static bool PdPlanBegin();
static void PdPlanEnd(u8 r);
static void PdCapped();
static void ReadPlanDeadline(const wchar_t* ini);
static u8 CallPlanBudget0(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                          int* out1, int* out2, DWORD guidLo, DWORD guidHi);
static u8 CallPlanBudget(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                         int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    if (!g_pdOn || !PdPlanBegin()) return CallPlanBudget0(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    const u8 r = CallPlanBudget0(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    PdPlanEnd(r);
    return r;
}
static u8 CallPlanBudget0(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                          int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    // DebugPathPlan (RouteOption hi word bit 0x1) as the "budget this route" flag for a future C# caller (docs/11 Q3)
    const bool ready = ts3native_counters.budgetReady != 0;
    const bool global = ready && ts3native_counters.budgetMode == 1 && !(g_onlyFlagged && !(((const DWORD*)(opts + 0x2C))[0] & 1));
    const LONG cap = ready ? t_fbCapUs : 0;  // docs/134: a sim-thread sync fallback plan, also with budgetMode 0
    const LONGLONG dl = ready ? t_fbDeadline : 0;  // docs/134 §11: a capped foreign job's deadline replaces its node clock
    if (!global && !cap && !dl) {
        if (!t_jobT0) return g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
        LARGE_INTEGER p0;  // a foreign job with the cap off: only its Route_PlanLocked share is timed
        QueryPerformanceCounter(&p0);
        const u8 r0 = g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
        t_jobPlanUs += UsSince(p0);
        return r0;
    }
    // A plan nested on this thread (from a managed call inside the search) gets its own budget; the outer one resumes after.
    const Budget outer = t_budget;
    const LONG gUs = global ? g_maxUs : 0;
    const LONG nodeUs = cap && !dl && (!gUs || cap < gUs) ? cap : gUs;
    const Budget fresh = {1, 0, {}, 0, nodeUs, global ? g_maxNodes : 0, dl, 0};
    t_budget = fresh;
    LARGE_INTEGER t0;  // whole request, lock wait included: for the counters only
    QueryPerformanceCounter(&t0);
    u8 r = g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    LONG us = UsSince(t0), nodes = t_budget.nodes;
    if (t_jobT0) t_jobPlanUs += us;  // shortcut: a plan nested in this one counts twice; nesting is rare (docs/134 §4)
    if (t_budget.capped) InterlockedExchangeAdd(&ts3native_counters.budgetCapUs, us);
    if (t_budget.capped && cap) t_fbCapHit = 1;
    if (t_budget.capped && dl && g_pdOn) PdCapped();
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

// Async route plan (docs/18 "Async plan"). 0.11.3: a queue of per-route jobs (g_jobs, FIFO by seq) that the worker runs
// one after another; g_async is the job the worker runs NOW: state A_RUNNING exactly while the worker holds that job's
// routing lock (the "job live" test of every closure), A_IDLE between jobs.
enum { A_IDLE, A_RUNNING };
struct AsyncSlot {
    volatile LONG state;
    DWORD handle;  // script route handle (0x25xxxxxx) of the running job
    DWORD owner;   // the thread that starts jobs (the Mono sim thread); only it can run the jobs' managed calls
    void* lock;    // routing lock world+0x918 the worker holds for the whole running job (H6 S3b)
};
// J_PEND (docs/134 §2.6, M1Retry): a parked START, sim thread only; the worker never sees it
enum { J_FREE, J_QUEUED, J_RUNNING, J_DONE, J_SKIPPED, J_PEND };
struct AsyncJob {
    volatile LONG state;  // FREE -> QUEUED (sim) -> RUNNING (worker claims) -> DONE / SKIPPED (worker) -> FREE (sim)
    DWORD handle, seq, doneTick;
    unsigned short x87;
    unsigned int mxcsr;
    void* lock;        // world+0x918 when START was accepted; the job is skipped if the world changed since
    u8 result[0x48];   // RoutePlanResult as IC_Route_Plan writes it
    int kind;          // A10 (a10.cpp): 0 IC_Route_Plan, 1 Replan, 2 PlanFromPoint, 3 ReplanFromPosition
    float pt[3];       // kinds 2-3: the start point, copied at START (the IC's x, y, z by value)
    LONG us;           // worker time of the IC call
    LONG p4;           // P4 (p4.cpp): 1 = armed for a shadow at START; P5 (p5.cpp): 2 = take the clone answer, 3 = also verify
    DWORD p4Ver;       // P4: the clone version cut at START (0 = none)
    int p4Why;         // P4: PS_* why the cut was refused (p4.cpp, 0.33.1)
    LONG p4W;          // P4: snap.cpp's g_spWrites right after the cut (0.33.2)
    LONG fb;           // docs/134: 1 = a foreign-task START (fbMode 1), the caller's task is not the route's Sim
    LONG af;           // docs/176: 1 = an A10 kind START from a foreign task ([RouteAsync] ForeignKinds); no FbJobCapMs cap
    LONG pend;        // docs/134 §2.6: READY ticks this J_PEND slot has waited
};
// docs/134: 32 slots; with [RouteAsync] FallbackAsync off only the first kJobsBase are handed out (QueueFull as before)
static const int kJobs = 32, kJobsBase = 8;
static const DWORD kDoneKeepMs = 5000;  // a Sim collects within a tick or two; older results lost their caller (reset)
static AsyncSlot g_async;
static AsyncJob g_jobs[kJobs];
static DWORD g_jobSeq;  // sim thread only
// routegate.cpp / rtsites.cpp: guarded routing calls open now on threads other than the worker (gate leaves, quadtree
// mutators, destroys, saves). The worker does not start a job while > 0: it sets A_RUNNING, then reads this, and backs
// off if set; those callers add 1, then read A_RUNNING (Dekker order, both interlocked).
static volatile LONG g_rgOffSimOpen;
// [LocoWindow] (locowin.cpp, design H2): g_g0wIn = 1 while the sim thread is inside Locomotion_Update (both modes);
// g_g0Win = 1 meanwhile in Mode 1 only: the worker starts no queued job (ServeAsync, same Dekker order as g_rgOffSimOpen).
// Written by the sim thread only; every sim wait that needs a not yet started job clears g_g0Win first (G0wDrop).
static volatile LONG g_g0wIn, g_g0Win;
static volatile LONG g_hxOpen;  // locowin.cpp (docs/177 §4): 1 while a script IC holds g_g0Win (H2 IC window), for hxHeld
enum { G0W_JOBWAIT, G0W_SYNCPLAN };
static void G0wDrop(int site, const void* ret);  // locowin.cpp
static void RcDrop();  // rcguard.cpp: kept +0x918 levels released before a sim wait for the worker
static void G0wGateWait(DWORD key, LONG us);     // locowin.cpp: gate waits inside the pass
static bool H2InPass();                          // locowin.cpp: this thread is the sim inside the pass (v85 lock waits)
static void H2LockWait(bool running, LONG us);   // locowin.cpp
// [ProbeP10] (p10.cpp): t_p10 = the route IC this sim-thread code runs in (index + 1, 0 = none) | P10_* flags
static volatile LONG g_p10On;
static __declspec(thread) DWORD t_p10;
enum { P10_REACHED = 0x100, P10_PLANNED = 0x200 };
// [RouteGate] G10Common=1 (design G10-C): depth of this thread's open site-10 frame on Route_PlanCommon (RunPlan keeps
// its plan on this thread then: the worker may be waiting in ServeAsync for that frame to close)
static __declspec(thread) int t_g10cOpen;
// [RouteGate] (routegate.cpp): the routing lock world+0x918, learned in PlanLockedHook
static void* volatile g_rgLock;
static __declspec(thread) DWORD t_steerRet;  // rtsites.cpp: caller of 0x82EDB0 / 0x82F0B0 while one runs on this thread
static __declspec(thread) bool t_avSite;     // avoidro.cpp: AvSite calls 0x82EDB0 for the retargeted 0x846D1C (key 0x846D21)
static __declspec(thread) DWORD t_rtoKey;    // rtoro.cpp: the late +0x918 take of a given-up clone, keyed 0x83156D as the original
static __declspec(thread) DWORD t_waitKey;   // routegate.cpp: set around a gate's +0x918 wait (probe P0)
// P0: one owner-thread wait of `us` for `key` (only the owner thread waits for jobs, so no interlocked insert)
enum { HA_LOCK, HA_OTHERLOCK, HA_JOB, HA_PLANWAIT, HA_N };
static void HaNote(DWORD key, LONG us, int cls);  // hitchattr.cpp ([HitchAttr], per-frame sim waits)
// [UnseenProbe] (unseen.cpp, docs/143): keyed waits and +0x918 acquires split by the agent class of the sim thread
static bool g_unOn;
static void UnWait(DWORD key, LONG us);
static void UnAcq(DWORD key);
static void WaitNote(DWORD key, LONG us) {
    Ts3nCounters& k = ts3native_counters;
    if (g_unOn) UnWait(key, us);
    int i = 0;
    while (i < 32 && k.blockKey[i] && k.blockKey[i] != (LONG)key) i++;
    if (i == 32) { InterlockedIncrement(&k.blockOther); return; }
    k.blockKey[i] = (LONG)key;
    InterlockedIncrement(&k.blockN[i]);
    InterlockedExchangeAdd(&k.blockUs[i], us);
}
// The 32 slots fill first-come: a key first hit after they are full only counts in blockOther (no time). A switch that
// removes its key from the warm-up slice on (AvoidRO, SlotEnabledRO, DeferFootprint) so hid that key's off-half wait
// (2026-10-22 pairs, docs/51 §218). The A/B keys get their slots at start.
static void SeedWaitKeys() {
    static const DWORD kKeys[] = {0x0085FE10, 0x0062EF7A, 0x0062F782, 0x00833593, 0x02000003, 0x00846D21, 0x008334CC,
                                  0x00760BFF, 0x0062FB74, 0x00631814, 0x00631F40, 0x0083156D, 0x0082D296, 0x0085B50C};
    for (int i = 0; i < (int)(sizeof(kKeys) / sizeof(kKeys[0])); i++) ts3native_counters.blockKey[i] = (LONG)kKeys[i];
}
static HANDLE g_asyncReady, g_asyncDone;
// IC_Route_Plan 0x765160: cdecl, [esp+4] = hidden RoutePlanResult* (also returned in eax), [esp+8] = route handle.
typedef u8*(__cdecl* IcPlanFn)(u8* out, DWORD handle);
static IcPlanFn g_icPlanOrig;
// A10 (a10.cpp): IC_Route_Replan 0x765490 has IC_Route_Plan's signature; IC_Route_PlanFromPoint 0x765240 and
// IC_Route_ReplanFromPosition 0x765550 add the start point as three floats by value ([ebp+0x10..0x18]) [verified disassembly]
typedef u8*(__cdecl* IcPointFn)(u8* out, DWORD handle, float x, float y, float z);
static void* g_arOrig[4];  // [kind]: the trampolines of the three collect detours ([0] unused: g_icPlanOrig)
static u8* CallIcRaw(int kind, u8* out, DWORD handle, const float* pt) {
    if (!kind) return g_icPlanOrig(out, handle);
    if (kind == 1) return ((IcPlanFn)g_arOrig[1])(out, handle);
    return ((IcPointFn)g_arOrig[kind])(out, handle, pt[0], pt[1], pt[2]);
}
// [RouteAsync] ReplanDedupe (replandedupe.cpp, docs/150 §13): Replan / ReplanFromPosition through its repeat check
static volatile LONG g_rdOn;
static u8* RdRun(int kind, u8* out, DWORD handle, const float* pt, u8* (*inner)(int, u8*, DWORD, const float*));
// [RouteAsync] FailMemo (plandeadline.cpp, docs/164 §7 D5): the same check on the sim's sync Plan IC (SyncIc sets t_fmSync for
// its own call; a worker job's Plan never goes through it)
static volatile LONG g_fmOn;
static __declspec(thread) bool t_fmSync;
static u8* CallIc(int kind, u8* out, DWORD handle, const float* pt) {
    const bool fm = t_fmSync;
    t_fmSync = false;  // a Plan nested in a managed call of this one comes back through SyncIc, which sets it again
    if (g_rdOn && (kind == 1 || kind == 3)) return RdRun(kind, out, handle, pt, CallIcRaw);
    if (fm && !kind && g_fmOn) return RdRun(0, out, handle, pt, CallIcRaw);
    return CallIcRaw(kind, out, handle, pt);
}
static void WaitAsyncDone();
// P5c (p5c.cpp, [RouteSnap] FreeLock=1, docs/94 §22.11): the sim may own the job's +0x918 inside a window; it never waits for
// the job holding it (P5cHandBegin / P5cHandEnd around a wait, P5cPlanHere in RunPlan, P5cNrEnter at the D2 entries)
static bool P5cLent(DWORD tid);
static bool P5cPlanHere();
static LONG P5cHandBegin();
static void P5cHandEnd(LONG n);
static void P5cNrEnter();
static void P5cFlush();
// [RouteAsync] TailUnlock (tailunlock.cpp, docs/150 §11): installed (P5bLock then applies P5b's admission to its windows), and
// the worker inside its post-pass tail window with +0x918 lent out; TuTrip = a guarded touch there: count, take it back
static volatile LONG g_tuOn;
static __declspec(thread) bool t_tuOpen;
static void TuTrip(int where);
static void P4LiveBegin(AsyncJob* j);  // p4.cpp: the worker captures this job's Route_PlanLocked
static void P4LiveEnd(AsyncJob* j);    // copies what the shadow needs before J_DONE
static void P4Shadow();                // after J_DONE: the clone run, compare, record
static void P4ShadowIdle();            // nothing queued: a parked shadow (p4.cpp, 0.34.1)
static void P4OnStart(AsyncJob* j);    // sim, START: sampling and the clone cut
static void P5PreLock(const AsyncJob* j);  // p5.cpp: worker, before a Mode=2 job takes +0x918
static LONG __fastcall LockAcquireHook(void* lock, void* edx, const DWORD* deadline);
static LONG __fastcall LockReleaseHook(void* lock, void* edx);  // routegate.cpp
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
// prologue as read at load ([RouteAsync] start), before the Frida profiler can attach: its task attribution hooks 0xEA1090
// (`jmp [slot]`) and Interceptor follows the jump, rewriting 0xE5A600's first bytes, so a later byte check failed (4 runs
// 2026-10-22/23, every plan sync with reason InvokeHook). Calling 0xE5A600 through such a hook still runs the original.
static bool g_invokePrologueAtLoad;
static InvokeFn g_invokeOrig;
// self NULL: runtime_invoke(a[0..3] = method, obj, params, exc); else host delegate call 0xD75FA0(self; a[0..3] = out, args 2-4)
// tPost..nested: [RouteJobPhases] HostSplit stamps (QPC ticks; tPost 0 = not stamped), hostcalls.cpp
struct InvokeJob { void* a[4]; void* self; void* result; LONGLONG tPost, tPick, tDone, nested; };
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

// [RouteJobPhases] HostSplit (hostcalls.cpp, docs/159 §5): serve sites, stamps; t_hcJob = the job the worker runs (0 = none)
enum { HC_PUMP918, HC_PUMPOTHER, HC_READY, HC_ASYNCDONE, HC_JOBDONE, HC_RUNPLAN, HC_SITES };
static __declspec(thread) LONG t_hcJob;
static LONG g_hcJobSeq;  // worker only
static LONGLONG HcNow();
static void HcPost(InvokeJob& v);
static void HcPick(InvokeJob& v, int site);
static void HcWake(InvokeJob& v);

// Caller side: runs the worker's managed call on this (Mono-attached) thread. site = HC_* (who picked it up)
static void ServeInvoke(int site) {
    InvokeJob& v = *g_invJob;
    if (v.tPost) HcPick(v, site);
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
    if (v.tPost) v.tDone = HcNow();
    SetEvent(g_invDone);
}

// [RouteJobPhases] (routephases.cpp, docs/150 §7): job and host-wait frames on the worker
static void RjpJobBegin(int kind);
static void RjpJobEnd(LONG us);
static bool RjpHostBegin(DWORD* frame);
static void RjpHostEnd(DWORD* frame);
// A managed call on the worker for a waiting caller (sync plan) or for the job's owner (async plan)?
static bool ForCaller() { return GetCurrentThreadId() == g_workerTid && (g_callerTid || g_async.state == A_RUNNING); }

// Worker side: hands the call to the caller/owner thread and runs nested plans it sends back meanwhile.
static void* Proxy(InvokeJob& v) {
    InvokeJob* prev = g_invJob;
    g_invJob = &v;
    LARGE_INTEGER t0 = {};
    if (t_jobT0) QueryPerformanceCounter(&t0);
    const bool rjp = RjpHostBegin((DWORD*)&t0);  // [RouteJobPhases]: the wait is phase host, nested plans its children
    HcPost(v);                                   // HostSplit: post stamp (worker in a job only)
    SetEvent(g_invReady);
    HANDLE h[2] = {g_invDone, g_jobReady};
    while (WaitForMultipleObjects(2, h, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {  // a plan from inside the call
        const LONGLONG n0 = v.tPost ? HcNow() : 0;
        ServePlan();
        if (v.tPost) v.nested += HcNow() - n0;
    }
    if (t_jobT0) t_jobHostUs += UsSince(t0);  // docs/134 §11: the job waits for the sim thread to run its managed call
    if (v.tPost) HcWake(v);
    if (rjp) RjpHostEnd((DWORD*)&t0);
    g_invJob = prev;
    return v.result;
}

// In the runtime_invoke slot: every thread but the worker goes straight to the original.
static void* __cdecl InvokeHook(void* method, void* obj, void** params, void** exc) {
    QlSharedTrip(SLD_MANAGED);
    if (t_snapRun) P4Abort(P4_AB_MANAGED);  // P4 tripwire (§12.4.3): served as without us so the game does not hang
    if (!ForCaller()) return g_invokeOrig(method, obj, params, exc);
    InvokeJob v = {{method, obj, params, exc}, NULL, NULL};
    return Proxy(v);
}

// Detour of 0xD75FA0: the same for the whole host delegate call (boxing and EA's task-id guard included).
static void* __fastcall HostCallHook(void* self, void* edx, void* out, void* a2, void* a3, void* a4) {
    QlSharedTrip(SLD_MANAGED);
    if (t_snapRun) P4Abort(P4_AB_MANAGED);
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
    const bool bytesNow = Readable(code, sizeof(kInvokePrologue)) && memcmp(code, kInvokePrologue, sizeof(kInvokePrologue)) == 0;
    if (cur != g_invokeExpect || (g_invokeCheckBytes && !bytesNow && !g_invokePrologueAtLoad)) {
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
    if (g_invokeCheckBytes && !bytesNow) Log("route worker: runtime_invoke prologue changed after load (an inline hook, e.g. the Frida profiler); it matched at load, hooked anyway");
    ts3native_counters.invokeHook = 1;
    return true;
}

// Worker side of an async job: the whole original IC_Route_Plan (start points, Route_PlanCommon, Route_PlanLocked under
// the routing lock, result mapping) with the starter's FPU state. Its managed calls go to the owner thread (InvokeHook).
static bool RtQuietWorld(const void* lock);  // rtsites.cpp: M1 world conditions that do not need the sim thread
static void TwArm(bool on);  // steerro.cpp: S2 tripwire stubs armed for one job (0.18.0)
static bool LendBackstop();  // tailunlock.cpp: CE-4, a TailUnlock / P5b window still open on the worker taken back
// Worker side of [LocoWindow] (design H2): does the sim's window hold this job's start? The first hold notes the time; after
// kG0wMaxHoldUs the worker lets go (watchdog: a sim wait for a held job that no G0wDrop covers would otherwise deadlock).
struct G0wHold { bool held, expired; LARGE_INTEGER t0; };
static const LONG kG0wMaxHoldUs = 250000;
static bool G0wHolds(G0wHold& h) {
    if (!g_g0Win || h.expired) return false;
    if (!h.held) {
        h.held = true;
        QueryPerformanceCounter(&h.t0);
        InterlockedIncrement(&ts3native_counters.g0wJobsHeld);
        if (g_hxOpen) InterlockedIncrement(&ts3native_counters.hxHeld);
        return true;
    }
    if (UsSince(h.t0) < kG0wMaxHoldUs) return true;
    h.expired = true;
    if (InterlockedIncrement(&ts3native_counters.g0wWatchdog) == 1)
        Log("loco window: a job start was held %ld ms, started anyway (watchdog; a sim wait without G0wDrop?)", kG0wMaxHoldUs / 1000);
    return false;
}
// Worker: the job starts now (A_RUNNING published, no back-off)
// inPass = g_g0wIn read just before the start's last window check (v85, docs/149): the thunk raises g_g0Win before g_g0wIn
// and drops g_g0wIn first, so inPass with no hold means a start inside the pass (Mode 0, after a drop, or the watchdog), never
// a start that only raced the thunk's entry (that job is "running at entry")
static volatile LONG64 g_h2PassEnd;  // locowin.cpp: QPC when the last pass returned (64-bit atomics: 32-bit process)
static void G0wStarted(const G0wHold& h, bool inPass) {
    Ts3nCounters& k = ts3native_counters;
    if (h.held) {
        const LONG us = UsSince(h.t0);
        InterlockedExchangeAdd(&k.g0wHoldUs, us);
        Max(&k.g0wHoldMaxUs, us);
        if (!h.expired && !inPass) {  // the pass ended (a drop leaves g_g0wIn set): how long after its end did this start
            InterlockedIncrement(&k.h2Released);
            LARGE_INTEGER end;
            end.QuadPart = InterlockedCompareExchange64(&g_h2PassEnd, 0, 0);
            if (end.QuadPart) Max(&k.h2ReleaseMaxUs, UsSince(end));
        }
    }
    if (inPass) InterlockedIncrement(&k.g0wJobStartInPass);
}

// docs/134 §10: a job >= 200 ms (the sim thread waits for its rest at its next +0x918 acquire): counted, and logged
// (worker only, 20 lines a minute) so a run names the long jobs: foreign or own START, IC kind, capped or not
// docs/134 §11: split of a foreign job (capAt/tail -1 = the cap did not fire): plan = Route_PlanLocked, host = waits for the sim
// thread to run the job's managed calls, other = the rest of the IC (start points, goal step 0x75C2D0, result mapping)
struct JobSplit { LONG capAtUs, tailUs, planUs, hostUs; };
static void JobLongNote(const AsyncJob* j, LONG us, bool capped, const JobSplit* s = NULL) {
    static DWORD minute, lines;
    InterlockedIncrement(j->fb ? &ts3native_counters.fbjcLong : &ts3native_counters.ownJobLong);
    const DWORD now = GetTickCount();
    if (now - minute >= 60000) minute = now, lines = 0;
    if (lines++ >= 20) return;
    if (!s) {
        Log("route async: long job %ld ms, %s START, IC kind %d, route %08lX%s", us / 1000, j->fb ? "foreign" : j->af ? "A10 foreign" : "own", j->kind,
            j->handle, capped ? ", capped (FbJobCapMs)" : "");
        return;
    }
    Log("route async: long job %ld ms, foreign START, IC kind %d, route %08lX%s, capAt %ld ms tail %ld ms, plan %ld ms host %ld ms "
        "other %ld ms", us / 1000, j->kind, j->handle, capped ? ", capped (FbJobCapMs)" : "", s->capAtUs < 0 ? -1 : s->capAtUs / 1000,
        s->tailUs < 0 ? -1 : s->tailUs / 1000, s->planUs / 1000, s->hostUs / 1000, (us - s->planUs - s->hostUs) / 1000);
}

// docs/134 §11 buckets: <100, <300, <500, <1000, <2000, >=2000 ms
static void FbjcHist(LONG* h, LONG us) {
    static const LONG kEdgesMs[] = {100, 300, 500, 1000, 2000};
    int i = 0;
    while (i < 5 && us >= kEdgesMs[i] * 1000) i++;
    InterlockedIncrement(&h[i]);
}
// running job is a foreign one (worker publishes it with A_RUNNING)
static volatile LONG g_jobFb;
static volatile LONG g_jobOwn;  // docs/178 T4: the running job is an own one (neither foreign nor an A10 foreign kind)
// a sim-thread wait (LockAcquireAsync pump, RtWaitJob) that had a foreign job running at its start or end
static void FbjcSimWait(LONG us, bool fb) {
    if (!fb) return;
    Max(&ts3native_counters.fbjcSimWaitMaxUs, us);
    FbjcHist(ts3native_counters.fbjcWaitHist, us);
}
// docs/178 T4 ([RouteAsync] OwnJobWait=1, observe only): the same for a wait that had an own job running (handle = that job's
// route); waits >= 300 ms logged (sim thread only, 20 lines a minute)
static void OwSimWait(LONG us, bool own, DWORD handle) {
    Ts3nCounters& k = ts3native_counters;
    if (!own || !k.owReady) return;
    InterlockedIncrement(&k.owWaits);
    InterlockedExchangeAdd(&k.owWaitUs, us);
    Max(&k.owWaitMaxUs, us);
    FbjcHist(k.owWaitHist, us);
    if (us < 300000) return;
    static DWORD minute, lines;
    const DWORD now = GetTickCount();
    if (now - minute >= 60000) minute = now, lines = 0;
    if (lines++ >= 20) return;
    InterlockedIncrement(&k.owLines);
    Log("route jobs: sim waited %ld ms behind an own job (route %08lX)", us / 1000, handle);
}
static void FbjdJobEnd(LONG us, LONGLONG end, LONGLONG dl);  // plandeadline.cpp (docs/178 T1)

// Worker side: runs every queued job, oldest first. Each job: the whole original route IC of its kind (IC_Route_Plan, or
// with A10 Replan / PlanFromPoint / ReplanFromPosition: start points, Route_PlanCommon, Route_PlanLocked, result mapping)
// with the starter's FPU state, under +0x918 for the whole job (H6 S3b;
// recursive, so the planner's own acquires nest). Its managed calls go to the owner thread (InvokeHook).
static void ServeAsync() {
    Ts3nCounters& k = ts3native_counters;
    static const DWORD kInfinite = 0xFFFFFFFF;
    for (;;) {
        AsyncJob* j = NULL;
        for (int i = 0; i < kJobs; i++)
            if (g_jobs[i].state == J_QUEUED && (!j || (LONG)(g_jobs[i].seq - j->seq) < 0)) j = &g_jobs[i];
        if (!j) {
            g_p4Phase = 2;
            P4ShadowIdle();  // a shadow parked behind the job that just ran (0.34.1)
            g_p4Phase = 0;
            return;
        }
        if (InterlockedCompareExchange(&j->state, J_RUNNING, J_QUEUED) != J_QUEUED) continue;  // cancelled meanwhile
        // the world may have changed since START: wrong world, closing, saving, pending lot updates, dirty lot index
        if (j->lock != g_rgLock || k.rtClosing || k.rtBusy || !RtQuietWorld(j->lock)) {
            InterlockedIncrement(&k.asyncSkipped);
            j->doneTick = GetTickCount();
            InterlockedExchange(&j->state, J_SKIPPED);
            SetEvent(g_asyncDone);
            continue;
        }
        // start only when no guarded routing call is open elsewhere (g_rgOffSimOpen); else back off and retry. The S2 tripwire
        // stubs are armed before A_RUNNING is published and stay armed through back-offs until after A_IDLE below
        P5PreLock(j);  // P5: the replay consumer at this job's cut before the job holds +0x918 (0.35.19)
        TwArm(true);
        g_jobFb = j->fb;  // docs/134 §11: read with A_RUNNING by the sim thread's waits
        g_jobOwn = !j->fb && !j->af;
        G0wHold h = {};  // H2: the sim's locomotion window holds the start too (locowin.cpp)
        bool inPass = false, late = false;
        for (;;) {
            while (g_rgOffSimOpen || G0wHolds(h)) Sleep(0);
            LockAcquireHook(j->lock, NULL, &kInfinite);
            g_async.handle = j->handle;
            g_async.lock = j->lock;
            InterlockedExchange(&g_async.state, A_RUNNING);
            inPass = g_g0wIn != 0;  // before the window check (G0wStarted)
            // CE-1 (docs/154): travel / quit (RT_CLOSING) sets rtClosing, then reads A_RUNNING in RtWaitJob, with no
            // g_rgOffSimOpen frame; so the world is read again after A_RUNNING (Dekker), or a drain that ran in the gap above
            // (H2 hold, P5PreLock, the acquire) would let this job run beside the teardown. Saves keep their frame (above)
            late = j->lock != g_rgLock || k.rtClosing;
            if (late || (!g_rgOffSimOpen && !G0wHolds(h))) break;
            InterlockedExchange(&g_async.state, A_IDLE);
            LockReleaseHook(j->lock, NULL);
            SetEvent(g_asyncDone);  // an owner that saw A_RUNNING meanwhile waits in WaitAsyncDone: wake it now, not at its 2 s timeout
            InterlockedIncrement(&k.asyncBackoffs);
        }
        if (late) {  // backed out as the early refusal above: J_SKIPPED, the collect plans synchronously
            InterlockedExchange(&g_async.state, A_IDLE);
            LockReleaseHook(j->lock, NULL);
            g_jobFb = g_jobOwn = 0;
            TwArm(false);
            InterlockedIncrement(&k.asyncSkipped);
            j->doneTick = GetTickCount();
            InterlockedExchange(&j->state, J_SKIPPED);
            SetEvent(g_asyncDone);
            continue;
        }
        G0wStarted(h, inPass);
        const unsigned short cw = GetX87();
        const unsigned int csr = _mm_getcsr();
        SetX87(j->x87);
        _mm_setcsr(j->mxcsr);
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        g_p4Phase = 1;
        P4LiveBegin(j);
        // docs/134 §10 FbJobCapMs: the sim thread waits out a long job at its next +0x918 acquire (steering, destroy), so a
        // foreign job gets a world A* cap like a sync plan's SyncCapMs (nested plans the job's managed calls send back too).
        // §11: the cap is a deadline from t0 (the job's start), not a clock from its first world A* node
        t_fbCapUs = j->fb && k.fbjcMode == 1 ? k.fbjcCapUs : 0;
        t_fbDeadline = t_fbCapUs ? t0.QuadPart + (LONGLONG)t_fbCapUs * g_qpf.QuadPart / 1000000 : 0;
        t_jobT0 = j->fb ? t0.QuadPart : 0;
        t_jobCapAt = 0;
        t_jobPlanUs = t_jobHostUs = 0;
        t_fbCapHit = 0;
        t_fbjdCut[0] = t_fbjdCut[1] = t_fbjdCut[2] = 0;
        // docs/178 T1: the cell A* / water deadline exits (plandeadline.cpp) also answer this thread while the job has a
        // deadline; PdSkip reads fbjdMode per call (runtime switch)
        if (t_fbDeadline && (k.fbjdReady & 3)) InterlockedExchange((volatile LONG*)&g_pdWkTid, (LONG)GetCurrentThreadId());
        RjpJobBegin(j->fb ? 2 : j->kind ? 1 : 0);  // [RouteJobPhases] (routephases.cpp): own START, own A10, foreign
        t_hcJob = ++g_hcJobSeq;                     // hostcalls.cpp: door keys and the memo are per job
        CallIc(j->kind, j->result, j->handle, j->pt);
        t_hcJob = 0;
        LendBackstop();  // CE-4 (docs/154): a lend window an unwind left open is closed before the release below
        InterlockedExchange((volatile LONG*)&g_pdWkTid, 0);
        const LONGLONG dl = t_fbDeadline;
        t_fbCapUs = 0;
        t_fbDeadline = 0;
        t_fbjdCut[0] = t_fbCapHit;
        const bool capped = t_fbCapHit || t_fbjdCut[1] || t_fbjdCut[2];  // docs/178 T1: a cell / water cut counts as capped too
        t_fbCapHit = 0;
        const LONG us = UsSince(t0);
        if (j->fb) FbjdJobEnd(us, t0.QuadPart + (LONGLONG)us * g_qpf.QuadPart / 1000000, dl);
        RjpJobEnd(us);  // reads t_fbjdCut for the per-phase long-job line (docs/178 T1)
        JobSplit split = {-1, -1, t_jobPlanUs, t_jobHostUs};
        if (t_jobCapAt) {
            split.capAtUs = (LONG)((t_jobCapAt - t0.QuadPart) * 1000000 / g_qpf.QuadPart);
            split.tailUs = us - split.capAtUs;
        }
        t_jobT0 = t_jobCapAt = 0;
        j->us = us;
        P4LiveEnd(j);
        SetX87(cw);
        _mm_setcsr(csr);
        LockReleaseHook(j->lock, NULL);
        InterlockedExchange(&g_async.state, A_IDLE);
        g_jobFb = g_jobOwn = 0;
        TwArm(false);
        InterlockedExchangeAdd(&k.asyncUs, us);
        Max(&k.asyncMaxUs, us);
        if (capped) InterlockedIncrement(&k.fbjcCapped);
        Max(j->fb ? &k.fbjcMaxUs : &k.ownJobMaxUs, us);
        if (j->af) Max(&k.afJobMaxUs, us);
        if (j->fb) {  // docs/134 §11
            FbjcHist(k.fbjcJobHist, us);
            Max(&k.fbjcPlanMaxUs, split.planUs);
            Max(&k.fbjcHostMaxUs, split.hostUs);
            if (split.capAtUs >= 0) {
                Max(&k.fbjcCapAtMaxUs, split.capAtUs);
                Max(&k.fbjcTailMaxUs, split.tailUs);
                if (k.fbjcCapUs && split.capAtUs > k.fbjcCapUs + 20000) InterlockedIncrement(&k.fbjcCapLate);
            }
        }
        if (us >= 200000) JobLongNote(j, us, capped, j->fb ? &split : NULL);
        j->doneTick = GetTickCount();
        InterlockedExchange(&j->state, J_DONE);
        SetEvent(g_asyncDone);
        g_p4Phase = 2;
        P4Shadow();  // works on its own copies only: collect and slot reuse never wait for it (docs/94 §12.4.2)
        g_p4Phase = 0;
    }
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
        else if (P5cPlanHere()) viaWorker = false;  // P5c: this thread owns the lent lock; the job searches its clone
        else {
            LARGE_INTEGER w0;
            QueryPerformanceCounter(&w0);
            WaitAsyncDone();
            const LONG w = UsSince(w0);
            WaitNote(0x05000000, w);
            HaNote(0x05000000, w, HA_PLANWAIT);
        }
    }
    if (t_g10cOpen && !g_inInvoke) viaWorker = false;  // G10-C: no job can run now, and a worker in ServeAsync waits for us
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
    RcDrop();  // docs/177 §7: the worker may need +0x918 before it serves this plan
    G0wDrop(G0W_SYNCPLAN, _ReturnAddress());  // H2: a worker held inside ServeAsync serves g_jobReady only after its queue
    SetEvent(g_jobReady);
    // Serve the worker's managed calls until the plan is done. Watchdog: log deadlock evidence and keep waiting;
    // there is no safe way to take the search back.
    HANDLE h[2] = {g_jobDone, g_invReady};
    for (DWORD s = 0;;) {
        const DWORD w = WaitForMultipleObjects(2, h, FALSE, 2000);
        if (w == WAIT_OBJECT_0) break;
        if (w == WAIT_OBJECT_0 + 1) {
            ServeInvoke(HC_RUNPLAN);
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

// exact G11 window (docs/18 (3), S2b): what closed a sim-thread plan's free window (index into g11End)
enum { G11_LOCK, G11_GATE, G11_GATE13, G11_DESTROY, G11_QTWRITE, G11_PLAN };
static void G11Touch(int kind, DWORD key);
static bool G11PlanBegin();
static void G11PlanEnd(bool begun);

static u8 PlanLockedBody(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent,
                         DWORD doPlan, u8* opts, int* out1, int* out2, DWORD guidLo, DWORD guidHi);
static int DiscDrainForPlan();  // discourage.cpp (docs/89 §13 PlanDrain)
static DWORD P10PlanBegin(const DWORD* slot, LARGE_INTEGER* t0);  // p10.cpp
static void P10PlanEnd(DWORD tag, LARGE_INTEGER t0);
static void __cdecl P10Enter(int ic, DWORD* slot);
static u8 P4LivePlan(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                     int* out1, int* out2, DWORD guidLo, DWORD guidHi);  // p4.cpp: calls PlanLockedBody between its captures
struct PrTok { u64 key, loose; LARGE_INTEGER t0; DWORD ver, sver; int cls; bool keyed; };  // planrepeat.cpp
static bool PrBegin(PrTok& t, const void* world, const int* starts, const int* goals, DWORD agent, DWORD doPlan, const u8* opts,
                    DWORD guidLo, DWORD guidHi);
static void PrEnd(bool on, const PrTok& t, const int* outList, u8 r);

// noinline: P10 reads this frame's own return slot (selftest calls it directly)
static __declspec(noinline) u8 __fastcall PlanLockedHook(void* world, void* edx, int* outList, int* starts, int* goals,
                                                         DWORD agent, DWORD doPlan, u8* opts, int* out1, int* out2,
                                                         DWORD guidLo, DWORD guidHi) {
    if (t_p4Cap) return P4LivePlan(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    if (!t_p10) return PlanLockedBody(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    LARGE_INTEGER t0;  // P10: a sim-thread plan inside one of the four route ICs
    const DWORD tag = P10PlanBegin((const DWORD*)_AddressOfReturnAddress(), &t0);
    const u8 r = PlanLockedBody(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    P10PlanEnd(tag, t0);
    return r;
}

static u8 PlanLockedBody(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent,
                         DWORD doPlan, u8* opts, int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    if (ts3native_counters.rgReady == 7 && g_rgLock != (u8*)world + 0x918) {  // a new world (load, travel) has a new lock
        g_rgLock = (u8*)world + 0x918;
        ts3native_counters.rgLock = (LONG)(uintptr_t)g_rgLock;
        ts3native_counters.rgOwnerTid = 0;  // holds of the old lock are over; the new one is tracked from its next acquire
        Log("route gate: routing lock %p (thread %lu)", g_rgLock, GetCurrentThreadId());
    }
    InterlockedIncrement(&ts3native_counters.calls);
    ULONGLONG now = GetTickCount64();
    MaybeLogCounters(now);
    // the original's own early-outs (no plan requested, no start, no goal) stay with the original
    if ((u8)doPlan == 0 || starts[0] == starts[1] || goals[0] == goals[1])
        return g_planOrig(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    const Ts3nCounters& c = ts3native_counters;
    const bool on = (c.workerMode == 1 && c.workerReady) || (c.asyncMode == 1 && c.asyncReady);
    InterlockedIncrement(on ? &ts3native_counters.planOnCalls : &ts3native_counters.planOffCalls);
    const int sd = DiscDrainForPlan();  // docs/89 §13: deferred footprints applied first (1 drained, -1 skipped, 0 not asked)
    const bool g11 = G11PlanBegin();
    PrTok pr;
    const bool prOn = PrBegin(pr, world, starts, goals, agent, doPlan, opts, guidLo, guidHi);
    const u8 r = PlanCached(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi, now);
    PrEnd(prOn, pr, outList, r);
    G11PlanEnd(g11);
    if (!r) InterlockedIncrement(on ? &ts3native_counters.planOnFails : &ts3native_counters.planOffFails);
    if (!r && sd) InterlockedIncrement(&ts3native_counters.sdFail[sd > 0 ? 0 : 1]);
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
static bool RtM1Allows(void** lock, int* why = NULL);  // rtsites.cpp; *why = the refusal's asyncRefusedM1 index
static void DiscPoll();               // discourage.cpp
static DWORD g_collect;  // handle whose READY said "done": the owner's next IC_Route_Plan for it takes the stored result

// Owner side: runs the running job's managed calls until no job runs (2 s watchdog: log and keep waiting, as RunPlan does).
static void WaitAsyncDone() {
    RcDrop();  // docs/177 §7: no level kept by the refcount guard while waiting for the job
    const LONG hand = P5cHandBegin();  // P5c: never wait for the job holding the lock it lent out
    for (DWORD s = 0; g_async.state == A_RUNNING;) {
        HANDLE h[2] = {g_asyncDone, g_invReady};
        const DWORD w = WaitForMultipleObjects(2, h, FALSE, 2000);
        if (w == WAIT_OBJECT_0 + 1) ServeInvoke(HC_ASYNCDONE);
        else if (w == WAIT_TIMEOUT) {
            s += 2;
            InterlockedIncrement(&ts3native_counters.watchdogs);
            Log("route async: thread %lu waited %lu s for the job of route %08lX, still waiting", GetCurrentThreadId(), s, g_async.handle);
        }
    }
    P5cHandEnd(hand);
}

// Owner side: the same for one job (queued jobs ahead of it run first)
static void WaitJobDone(AsyncJob* j) {
    // H2: a queued or claimed job may be held by this thread's locomotion window; it must start for this wait to end
    if (j->state == J_QUEUED || j->state == J_RUNNING) G0wDrop(G0W_JOBWAIT, _ReturnAddress());
    RcDrop();  // docs/177 §7
    const LONG hand = P5cHandBegin();  // P5c, as WaitAsyncDone (a queued job waits for the running one too)
    for (DWORD s = 0; j->state == J_QUEUED || j->state == J_RUNNING;) {
        HANDLE h[2] = {g_asyncDone, g_invReady};
        const DWORD w = WaitForMultipleObjects(2, h, FALSE, 2000);
        if (w == WAIT_OBJECT_0 + 1) ServeInvoke(HC_JOBDONE);
        else if (w == WAIT_TIMEOUT) {
            s += 2;
            InterlockedIncrement(&ts3native_counters.watchdogs);
            Log("route async: thread %lu waited %lu s for the job of route %08lX, still waiting", GetCurrentThreadId(), s, j->handle);
        }
    }
    P5cHandEnd(hand);
}

static AsyncJob* JobFor(DWORD handle) {
    for (int i = 0; i < kJobs; i++)
        if (g_jobs[i].state != J_FREE && g_jobs[i].handle == handle) return &g_jobs[i];
    return NULL;
}

// a job's result thrown away (v5 total; A10 kinds also per kind)
static void NoteDrop(const AsyncJob* j) {
    InterlockedIncrement(&ts3native_counters.asyncDropped);
    if (j->kind) InterlockedIncrement(&ts3native_counters.arDropped[j->kind - 1]);
}

// Owner side: a queued job is taken back; a running one is waited for (its managed calls served); the result is thrown away.
static void DropJob(AsyncJob* j) {
    if (InterlockedCompareExchange(&j->state, J_FREE, J_PEND) == J_PEND) return;  // parked, never queued: nothing to drop
    if (InterlockedCompareExchange(&j->state, J_FREE, J_QUEUED) == J_QUEUED) { NoteDrop(j); return; }
    WaitJobDone(j);
    if (j->state == J_DONE) NoteDrop(j);
    InterlockedExchange(&j->state, J_FREE);
}

// Owner side, async switched off mid-run: no job may be left without a collector. No-op on other threads.
static void DropAllJobs() {
    if (GetCurrentThreadId() != g_async.owner) return;
    for (int i = 0; i < kJobs; i++)
        if (g_jobs[i].state != J_FREE) DropJob(&g_jobs[i]);
}

// v35 (docs/60 §2.3): why a Mode 1 plan ran synchronously on the sim thread. START and READY note the reason for their
// route; its IC_Route_Plan right after takes it. Codes 1-7 = group 0 (refused), 8-11 = group 1 (queue), else group 2.
// A10 (0.22.0) adds Shadow (Mode 0 refusal), KindMismatch (the job was another IC's) and Active (route being followed).
enum { RFB_NOSTART, RFB_NOYIELD, RFB_M1, RFB_RUNNING = RFB_M1 + 6, RFB_QFULL, RFB_INVOKE, RFB_UNSTARTED, RFB_SKIPPED, RFB_UNCOLLECTED,
       RFB_SHADOW, RFB_KIND, RFB_ACTIVE, RFB_LOS, RFB_FOREIGN };
// docs/134: NoStart = no START came for the route (the ts3fixes hook found no yielding task, or asyncroute is off);
// Foreign (0.36.7) = a START from a yielding task that is not the route's Sim, refused while fbMode is 0
static const char* const kRfbName[] = {"NoStart", "NoYield", "M1NoLock", "M1Busy", "M1Closing", "M1Pending", "M1DirtyIndex",
                                       "M1FrameOpen", "Running", "QueueFull", "InvokeHook", "Unstarted", "Skipped", "Uncollected",
                                       "Shadow", "KindMismatch", "Active", "Los", "Foreign"};

// docs/94 §17.9 (I-105-3, 0.29.0): a route with ForcePhysicsBasedRaycast (RouteOption bit 0x2000000, route+0x158+0x28: the
// test RoutingOptions_Test 0x81C880 makes at 0x75AB9C before World_CanSeeObject 0x75AC22) raises the lot display level
// (lot +0x39C, level group bit 0x10000) from inside the plan; on the worker that races the sim's level writers. Those plans
// stay on the sim thread (refusal "Los"). Route lookup as the route ICs: RouteHandle_Lookup 0x74B2C0 on [0x11D40C4].
typedef const u8*(__fastcall* AsRouteFn)(const void* rm, void* edx, DWORD handle);
static AsRouteFn g_asRoute = (AsRouteFn)(uintptr_t)0x0074B2C0;
static const u8* const* g_asRmSlot = (const u8* const*)(uintptr_t)0x011D40C4;
static bool AsyncLosRoute(DWORD handle) {
    __try {
        const u8* rm = *g_asRmSlot;
        const u8* route = rm ? g_asRoute(rm, NULL, handle) : NULL;
        return route && (*(const DWORD*)(route + 0x158 + 0x28) & 0x2000000) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static const char* const kArIcName[4] = {"Plan", "Replan", "PlanFromPoint", "ReplanFromPosition"};
static DWORD g_rfbHandle;  // sim thread only, like g_collect
static int g_rfbWhy;
static DWORD g_rfbMinute, g_rfbLines, g_rfbDropped;  // log rate limit
static const LONG kRfbCountMs = 50, kRfbLinesPerMin = 20;
static u8 RfbRefuse(DWORD handle, int why) {
    g_rfbHandle = handle;
    g_rfbWhy = why;
    return 0;
}
// docs/134 [RouteAsync] LogFallbackStack=N: after each fallback log line, the sim thread's script (interpreter) frames,
// innermost first, so a live run names the managed caller (the native caller is the same Mono IC wrapper for all of them).
// Walk = autledger.cpp AsLgFrames (TEB TLS slot [0x11F4160] -> context, +8 frame, frame[0] parent, frame[1] info, info +8
// MonoMethod*); names as tools/probe/agent.js: MonoMethod klass +8, name +0x18; MonoClass name +0x34, namespace +0x38.
// Every read under SEH; called on the sim thread while it is still inside the route IC, so the frames are the caller's.
static int AsLgFrames(DWORD* m, int n);  // autledger.cpp
static LONG g_fbStackFrames;             // LogFallbackStack (0 = off, max 16)
static int FbName(char* d, int room, DWORD method) {
    int n = 0;
    __try {
        const u8* meth = (const u8*)(uintptr_t)method;
        const u8* klass = meth ? *(const u8* const*)(meth + 8) : NULL;
        const char* parts[3] = {klass ? *(const char* const*)(klass + 0x38) : NULL, klass ? *(const char* const*)(klass + 0x34) : NULL,
                                meth ? *(const char* const*)(meth + 0x18) : NULL};
        for (int p = 0; p < 3; p++) {
            if (!parts[p] || !parts[p][0]) continue;
            if (n && n < room - 1) d[n++] = '.';
            for (int i = 0; i < 80 && parts[p][i] >= 0x20 && parts[p][i] < 0x7F && n < room - 1; i++) d[n++] = parts[p][i];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!n && room > 1) d[n++] = '?';
    d[n] = 0;
    return n;
}
// frames read (<= 0: none); d = "A.B.M < C.D.N < ..." innermost first
static int FbStackText(char* d, int room, int frames) {
    DWORD m[16] = {0};
    const int got = AsLgFrames(m, frames < 16 ? frames : 16);
    int n = 0;
    d[0] = 0;
    for (int i = 0; i < got && n < room - 8; i++) {
        if (i) { memcpy(d + n, " < ", 4); n += 3; }
        n += FbName(d + n, room - n, m[i]);
    }
    return got;
}
static void RfbStackLine(DWORD handle) {
    if (!g_fbStackFrames) return;
    char st[440];  // Log lines are 512 bytes
    if (FbStackText(st, sizeof(st), g_fbStackFrames) <= 0) { Log("route async: script stack of route %08lX: unreadable", handle); return; }
    InterlockedIncrement(&ts3native_counters.fbStacks);
    Log("route async: script stack of route %08lX (innermost first): %s", handle, st);
}

// kind 0 adds to the v35 counters; A10 kinds only log (v35 stays Route.Plan's)
static void RfbNote(int why, LONG ms, const char* entry, DWORD handle, const void* ret, int kind = 0) {
    Ts3nCounters& k = ts3native_counters;
    if (!kind) {
        const int g = why >= RFB_NOYIELD && why < RFB_RUNNING ? 0 : why >= RFB_RUNNING && why <= RFB_UNSTARTED ? 1 : 2;
        InterlockedExchangeAdd(&k.rfbMs[g], ms);
        InterlockedIncrement(&k.rfbN[g]);
        const LONG kMsCap = (1 << 23) - 1;  // ms in 23 bits: the packed value stays positive
        Max(&k.rfbMax, (ms < kMsCap ? ms : kMsCap) << 8 | why);
    }
    if (ms < k.rfbLogMs) return;
    const DWORD now = GetTickCount();
    if (now - g_rfbMinute >= 60000) { g_rfbMinute = now; g_rfbLines = 0; }
    if (g_rfbLines >= (DWORD)kRfbLinesPerMin) { g_rfbDropped++; return; }
    g_rfbLines++;
    Log("route async: sync plan %ld ms, IC %s, reason %s, entry %s, route %08lX, caller %p (%lu lines rate-limited before)%s", ms,
        kArIcName[kind], kRfbName[why], entry, handle, ret, g_rfbDropped, t_fbCapHit ? ", capped (SyncCapMs)" : "");
    g_rfbDropped = 0;
    RfbStackLine(handle);
}
// the route IC on this thread. Plan: timed only in Mode 1 with LogFallbackMs or SyncCapMs set (two QPC reads per sync plan);
// A10 kinds: always timed (arSyncUs), logged like Plan's. docs/134 SyncCapMs: the world A* of this sim-thread plan ends at
// the cap through the [RouteBudget] stubs (an ordinary no-path failure), whatever the reason it is not async
static bool SypIcWants();  // syncphases.cpp (docs/164): [RouteJobPhases] SyncPlans session around the whole sync IC
static u8* SypIcRun(int kind, u8* out, DWORD handle, const float* pt, int why, const char* entry, const void* ret);
// docs/164 §7 (plandeadline.cpp): the deadline of this IC by reason (0 = none) and its session around the IC
static LONG PdIcUs(int why);
static void PdIcBegin(LONG us, int kind, int why, const char* entry, DWORD handle);
static void PdIcEnd(const u8* out);
static u8* SyncIc(int kind, u8* out, DWORD handle, const float* pt, int why, const char* entry, const void* ret) {
    if (SypIcWants()) return SypIcRun(kind, out, handle, pt, why, entry, ret);
    Ts3nCounters& k = ts3native_counters;
    const LONG cap = k.asyncMode == 1 ? k.fbCapUs : 0;
    const LONG dlUs = g_pdOn ? PdIcUs(why) : 0;
    if (!kind && !cap && !dlUs && !g_fmOn && (!k.rfbLogMs || k.asyncMode != 1)) return g_icPlanOrig(out, handle);
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    const LONG outerCap = t_fbCapUs;  // shortcut: a sync plan nested in another keeps one cap flag; nesting is rare (docs/134 §4)
    t_fbCapUs = cap;
    t_fbCapHit = 0;
    if (dlUs) PdIcBegin(dlUs, kind, why, entry, handle);
    t_fmSync = true;
    u8* r = CallIc(kind, out, handle, pt);
    t_fmSync = false;
    if (dlUs) PdIcEnd(out);
    t_fbCapUs = outerCap;
    if (t_fbCapHit) InterlockedIncrement(&k.fbCapped);
    const LONG us = UsSince(t0), ms = us / 1000;
    if (kind) {
        InterlockedExchangeAdd(&k.arSyncUs[kind - 1], us);
        InterlockedIncrement(&k.arSyncN[kind - 1]);
    }
    // the v35 counts and lines stay as before: Mode 1 with LogFallbackMs set (RfbNote's line reads t_fbCapHit)
    if (k.rfbLogMs && k.asyncMode == 1 && ms >= kRfbCountMs) RfbNote(why, ms, entry, handle, ret, kind);
    t_fbCapHit = 0;
    return r;
}

// A10 tripwires: logged once each with the IC's caller (docs/87 §5)
static void ArTrip(LONG* n, const char* what, int kind, int jobKind, DWORD handle, const void* ret) {
    if (InterlockedIncrement(n) == 1)
        Log("route async: %s: IC %s met job kind %s, route %08lX, caller %p (logged once)", what, kArIcName[kind],
            jobKind >= 0 ? kArIcName[jobKind] : "none", handle, ret);
}

// A10: is the route being followed (Route_IsActive 0x74CFC0 reads route+0x60, set when following starts, cleared by
// Route_StopFollowingRoute 0x753230 [verified writers])? Locomotion reads such a route every tick, so it is never replanned
// on the worker [inferred race, never tested]. cdecl(handle), bool in AL; selftest swaps it. docs/134: foreign STARTs too.
typedef u8(__cdecl* RouteIsActiveFn)(DWORD handle);
static RouteIsActiveFn g_routeIsActive = (RouteIsActiveFn)(uintptr_t)0x0074CFC0;

// The collect side of every async route IC (IcPlanHook, a10.cpp hooks): hands the stored result over when READY armed it
// for this route and the job is this IC's (kind, start point), else plans here. Sim thread only.
static u8* IcCollect(int kind, u8* out, DWORD handle, const float* pt, const void* ret) {
    Ts3nCounters& k = ts3native_counters;
    if (k.asyncMode != 1) DropAllJobs();  // switched off mid-job: no job left without a collector
    // only the Mono sim thread calls internal calls (the worker calls the originals directly), so g_collect needs no lock
    const bool collect = g_collect == handle;
    g_collect = 0;
    const int why = g_rfbHandle == handle ? g_rfbWhy : RFB_NOSTART;
    if (g_rfbHandle == handle) g_rfbHandle = 0;
    AsyncJob* j = GetCurrentThreadId() == g_async.owner ? JobFor(handle) : NULL;
    // no job for this route: plan as asked (its Route_PlanLocked waits for a running job first: Lock_Acquire pump)
    if (!j) {
        if (collect && kind) ArTrip(&k.arOrphan, "armed collect without a job", kind, -1, handle, ret);
        return SyncIc(kind, out, handle, pt, why, "nojob", ret);
    }
    // the route's job is another IC's (or another start point): its result is not this call's answer
    if (j->kind != kind || (kind >= 2 && memcmp(j->pt, pt, sizeof(j->pt)) != 0)) {
        ArTrip(&k.arMismatch, "kind mismatch", kind, j->kind, handle, ret);
        DropJob(j);
        return SyncIc(kind, out, handle, pt, RFB_KIND, "mismatch", ret);
    }
    // docs/134 §2.6: still parked (the C# poll cap, or a Plan without READY): plan here, as M1Pending did before M1Retry
    if (InterlockedCompareExchange(&j->state, J_FREE, J_PEND) == J_PEND) {
        InterlockedIncrement(&k.fbRetryGaveUp);
        return SyncIc(kind, out, handle, pt, RFB_M1 + 3, "pending", ret);
    }
    // never started: take it back and plan it here
    if (InterlockedCompareExchange(&j->state, J_FREE, J_QUEUED) == J_QUEUED) {
        NoteDrop(j);
        return SyncIc(kind, out, handle, pt, RFB_UNSTARTED, "unstarted", ret);
    }
    WaitJobDone(j);  // normally already done (READY said so)
    const bool done = j->state == J_DONE;
    // docs/134 tripwire: the Sim whose route a foreign task planned started following it during the job (expected never)
    if (j->fb && done && g_routeIsActive(handle)) ArTrip(&k.fbActiveTrip, "foreign job's route being followed at collect", kind, j->kind, handle, ret);
    if (j->af && done && g_routeIsActive(handle)) ArTrip(&k.afActiveTrip, "A10 foreign job's route being followed at collect", kind, j->kind, handle, ret);
    if (done && collect) {
        memcpy(out, j->result, sizeof(j->result));
        InterlockedIncrement(&k.asyncCollected);
        if (kind) {
            InterlockedIncrement(&k.arCollected[kind - 1]);
            InterlockedExchangeAdd(&k.arJobUs[kind - 1], j->us);
            if (j->af) {
                InterlockedIncrement(&k.afCollected[kind - 1]);
                InterlockedExchangeAdd(&k.afJobUs[kind - 1], j->us);
            }
        }
    } else if (done) {
        NoteDrop(j);  // a plan the C# hook did not arm: plan it now, as asked
    }
    InterlockedExchange(&j->state, J_FREE);
    return done && collect ? out : SyncIc(kind, out, handle, pt, done ? RFB_UNCOLLECTED : RFB_SKIPPED, "job", ret);
}

static u8* UnPlan(u8* out, DWORD handle, const void* ret);  // unseen.cpp
// noinline: P10 swaps this frame's own return slot (selftest calls it directly)
static __declspec(noinline) u8* __cdecl IcPlanHook(u8* out, DWORD handle) {
    const void* ret = _ReturnAddress();
    if (g_p10On) P10Enter(0, (DWORD*)_AddressOfReturnAddress());  // P10: tags this IC until it returns (swaps the return slot)
    if (g_unOn) return UnPlan(out, handle, ret);                  // [UnseenProbe]: the same call, timed and classified
    return IcCollect(0, out, handle, NULL, ret);
}


static u8 AsyncRefuse(DWORD handle, int why, int kind, bool total = true) {
    if (total) InterlockedIncrement(&ts3native_counters.asyncRefused);
    if (kind) InterlockedIncrement(&ts3native_counters.arRefused[kind - 1]);
    return RfbRefuse(handle, why);
}

// docs/134 M1Pending (fbMode 1): Route_PlanLocked 0x82C6E0 takes +0x918 (Lock_Acquire call at 0x82C733) and then calls
// RoutingWorld_ApplyPendingUpdates 0x82C420 (thiscall world, no stack args, plain `ret` at 0x82C48E) [verified bytes]. A sync
// plan would do that first anyway; here the sim thread does only that, under one try of +0x918 (no wait, through the game's
// Lock_Acquire / Lock_Release and so our detours), and the job then finds a quiet world. Selftest swaps the three pointers.
typedef void(__fastcall* FbApplyFn)(void* world, void* edx);
typedef LONG(__fastcall* FbRelFn)(void* lock, void* edx);
static FbApplyFn g_fbApply = (FbApplyFn)(uintptr_t)0x0082C420;
static LockAcquireFn g_fbAcq = (LockAcquireFn)(uintptr_t)0x004E16F0;
static FbRelFn g_fbRel = (FbRelFn)(uintptr_t)0x004E17B0;
static bool FbApplyPending() {
    void* lock = g_rgLock;
    static const DWORD kTry = 0;
    if (!lock || g_fbAcq(lock, NULL, &kTry) <= 0) { InterlockedIncrement(&ts3native_counters.fbPendBusy); return false; }
    g_fbApply((u8*)lock - 0x918, NULL);
    g_fbRel(lock, NULL);
    InterlockedIncrement(&ts3native_counters.fbPendApplied);
    return true;
}

static bool JobQueue(AsyncJob* j, DWORD handle, int kind, const float* pt, bool foreign, void* lock);
// kind 0 = Route.Plan's START; kinds 1-3 (A10) also need their collect detour and a route nobody follows; pt = kinds 2-3.
// foreign (docs/134, fbMode 1): a kind-0 START from a yielding task that is not the route's Sim; also needs a route nobody follows.
// foreign with kind 1-3 (docs/176, afMode 1): the same for an A10 IC (job flag af, not fb: no FbJobCapMs cap, the result stays the IC's)
static u8 AsyncStart(DWORD handle, int kind = 0, const float* pt = NULL, bool foreign = false) {
    g_collect = 0;
    g_rfbHandle = 0;
    Ts3nCounters& k = ts3native_counters;
    if (k.asyncMode != 1 || !k.asyncReady) return 0;  // asyncMode is writable at runtime
    if (kind && !(k.arReady & (1 << (kind - 1)))) return AsyncRefuse(handle, RFB_NOSTART, kind, false);  // that IC not hooked
    if (AsyncLosRoute(handle)) { InterlockedIncrement(&k.arLosRefused); return AsyncRefuse(handle, RFB_LOS, kind); }
    // the caller could not sleep (nested native->managed call: its yield would abort the game)
    if (!CanYield()) {
        if (foreign && kind) InterlockedIncrement(&k.afNoYield[kind - 1]);
        return AsyncRefuse(handle, RFB_NOYIELD, kind);
    }
    // H6 M1 (docs/60 §2.4): only a quiet world, and only with the gate enforced (rtsites.cpp); the job's lock comes from there
    void* lock = NULL;
    int m1 = 0;
    bool park = false;  // docs/134 §2.6 M1Retry: the one +0x918 try met a running job; READY tries again each tick
    if (!RtM1Allows(&lock, &m1)) {
        if (m1 != 3 || k.fbMode != 1) return AsyncRefuse(handle, RFB_M1 + m1, kind);
        if (FbApplyPending()) {
            if (!RtM1Allows(&lock, &m1)) return AsyncRefuse(handle, RFB_M1 + m1, kind);
        } else if (k.fbRetryMode == 1) park = true;
        else return AsyncRefuse(handle, RFB_M1 + 3, kind);
    }
    if ((kind || foreign) && g_routeIsActive(handle)) {
        if (kind) InterlockedIncrement(&k.arActive[kind - 1]);
        if (kind && foreign) InterlockedIncrement(&k.afActive[kind - 1]);
        return AsyncRefuse(handle, RFB_ACTIVE, kind);
    }
    AsyncJob* j = JobFor(handle);
    // planned now; READY / plan collects it
    if (j && j->state == J_RUNNING) return AsyncRefuse(handle, RFB_RUNNING, kind);
    if (kind && k.arMode != 1) {  // A10 Mode 0: every check passed, no side effect
        InterlockedIncrement(&k.arShadow[kind - 1]);
        return RfbRefuse(handle, RFB_SHADOW);
    }
    if (j) DropJob(j);  // the same route again: the older request lost its caller
    const int slots = k.fbMode == 1 ? kJobs : kJobsBase;  // docs/134: a deeper queue instead of a QueueFull sync plan
    for (int i = 0; i < slots && !j; i++) if (g_jobs[i].state == J_FREE) j = &g_jobs[i];
    for (int i = 0; i < slots && !j; i++)  // full: reclaim a finished result its Sim never collected (reset)
        if ((g_jobs[i].state == J_DONE || g_jobs[i].state == J_SKIPPED || g_jobs[i].state == J_PEND) &&
            GetTickCount() - g_jobs[i].doneTick >= kDoneKeepMs) {  // a J_PEND this old lost its polling task
            if (g_jobs[i].state == J_DONE) NoteDrop(&g_jobs[i]);
            j = &g_jobs[i];
        }
    if (!j) { InterlockedIncrement(&k.asyncQueueFull); return AsyncRefuse(handle, RFB_QFULL, kind); }
    if (park) {  // the slot is this route's; READY (FbRetry) queues it once the pending updates are applied
        g_async.owner = GetCurrentThreadId();
        j->handle = handle;
        j->kind = kind;
        j->fb = foreign && !kind;
        j->af = foreign && kind;
        j->pend = 0;
        j->doneTick = GetTickCount();
        if (pt) memcpy(j->pt, pt, sizeof(j->pt));
        InterlockedIncrement(&k.fbRetryParked);
        InterlockedExchange(&j->state, J_PEND);
        return 1;
    }
    return JobQueue(j, handle, kind, pt, foreign, lock) ? 1 : AsyncRefuse(handle, RFB_INVOKE, kind, false);
}

// fills slot j and hands it to the worker (sim thread); false = the invoke hook is missing (the slot is left as it was)
static bool JobQueue(AsyncJob* j, DWORD handle, int kind, const float* pt, bool foreign, void* lock) {
    Ts3nCounters& k = ts3native_counters;
    EnterCriticalSection(&g_workerCs);
    const bool invokeOk = EnsureInvokeHook();  // the jobs' managed calls must come back to this thread
    LeaveCriticalSection(&g_workerCs);
    if (!invokeOk) return false;
    g_async.owner = GetCurrentThreadId();
    j->handle = handle;
    j->seq = ++g_jobSeq;
    j->x87 = GetX87();
    j->mxcsr = _mm_getcsr();
    j->lock = lock;
    j->kind = kind;
    j->fb = foreign && !kind;
    j->af = foreign && kind;
    if (j - g_jobs >= kJobsBase) InterlockedIncrement(&k.fbDeepQueued);
    if (j->fb) InterlockedIncrement(&k.fbForeignStarted);
    if (j->af) InterlockedIncrement(&k.afStarted[kind - 1]);
    if (pt) memcpy(j->pt, pt, sizeof(j->pt));
    P4OnStart(j);
    InterlockedExchange(&j->state, J_QUEUED);
    LONG queued = 0;
    for (int i = 0; i < kJobs; i++) queued += g_jobs[i].state == J_QUEUED || g_jobs[i].state == J_RUNNING;
    Max(&k.asyncQueueMax, queued);
    InterlockedIncrement(&k.asyncStarted);
    if (kind) InterlockedIncrement(&k.arStarted[kind - 1]);
    SetEvent(g_asyncReady);
    return true;
}

// docs/134 §2.6 M1Retry: one READY tick of a parked START (sim thread, the polling task's READY; DiscPoll ran just before).
// true = READY answers "not yet" (queued now, or wait another tick); false = given up, slot freed, the reason recorded for the
// plan that follows (synchronous, as M1Pending before M1Retry). The pending lot updates are applied only by FbApplyPending
// under a successful +0x918 try and only while the world's pending flag is set; a failed try applies nothing. D's deferred
// footprint records go first: with records still queued (a job ran at DiscPoll) the tick waits, so they never land after
// the pending updates, as in the sync plan (START's DiscPoll, then Route_PlanLocked's apply).
static const LONG kFbRetryTicks = 30;
static bool DiscPending();  // discourage.cpp
static bool FbRetry(AsyncJob* j) {
    Ts3nCounters& k = ts3native_counters;
    const DWORD h = j->handle;
    void* lock = NULL;
    int m1 = 0, why = -1;
    Max(&k.fbRetryTicksMax, ++j->pend);
    j->doneTick = GetTickCount();
    if (k.fbMode != 1 || k.fbRetryMode != 1) why = RFB_M1 + 3;  // switched off while parked
    else if (AsyncLosRoute(h)) why = RFB_LOS;
    else if (!RtM1Allows(&lock, &m1)) {
        if (m1 != 3) why = RFB_M1 + m1;
        else if (DiscPending() || !FbApplyPending()) {
            if (j->pend < kFbRetryTicks) return true;
            why = RFB_M1 + 3;
        } else if (!RtM1Allows(&lock, &m1)) why = RFB_M1 + m1;
    }
    if (why < 0 && (j->kind || j->fb) && g_routeIsActive(h)) why = RFB_ACTIVE;
    if (why < 0 && JobQueue(j, h, j->kind, NULL, j->fb != 0 || j->af != 0, lock)) {
        InterlockedIncrement(&k.fbRetryQueued);
        return true;
    }
    InterlockedIncrement(&k.fbRetryGaveUp);
    InterlockedExchange(&j->state, J_FREE);
    RfbRefuse(h, why < 0 ? RFB_INVOKE : why);
    return false;
}

// A10 handshake (ts3fixes RouteBackoff.cs), GetOption2 with bits 58 + 59 (no RouteOption2 names up there; the game answers
// false, older ts3native builds pass them to the game): kind in bits 56-57. Bit 60 set = POINT: start point component
// (bits 56-57: 0 x, 1 y, 2 z) as float bits in the low dword, sent before the START of kinds 2-3.
static const DWORD kArHi = 0x08000000, kArPtHi = 0x10000000, kArKindHi = 0x03000000;
// docs/176: bit 61 on a kind 1-3 START = foreign (the caller's task is not the route's Sim). Older builds fail the channel test
// and pass the call to the game, which answers false (unnamed bit): the plan stays synchronous. Bit 61 alone with bit 58 is
// the NoYield query (kNoYieldHi); the A10 channel also needs bit 59, so the two never meet.
static const DWORD kArFgnHi = 0x20000000;
static DWORD g_arPt[3], g_arPtSet;  // sim thread only
static int g_arOwnKinds = 7;        // [RouteAsync] Kinds: kinds whose own-task STARTs may start (InstallA10; selftest keeps 7)

static u8 SpDoorMsg(DWORD lo, DWORD hi);  // snap.cpp: door mirror channel (RouteOption bits 63 + 58)
static u8 UtViewerMsg(DWORD lo, DWORD hi);  // unseen.cpp: active-household viewer channel (RouteOption bits 63 + 57)
static u8 __cdecl GetOptionHook(DWORD handle, DWORD optLo, DWORD optHi, DWORD options2) {
    if ((u8)options2 && (optHi & 0xFF000000) == 0x84000000) return SpDoorMsg(optLo, optHi);
    if ((u8)options2 && (optHi & 0xFF000000) == 0x82000000) return UtViewerMsg(optLo, optHi);
    if ((u8)options2 && (optHi & ~(kArKindHi | kArPtHi | kArFgnHi)) == (kAsyncOptHi | kArHi) &&
        !((optHi & kArFgnHi) && ((optHi & kArPtHi) || !(optHi & kArKindHi)))) {  // foreign bit: kind 1-3 STARTs only
        const int idx = (int)(optHi >> 24) & 3;
        if (optHi & kArPtHi) {
            if (idx < 3) { g_arPt[idx] = optLo; g_arPtSet |= 1u << idx; }
            return 1;
        }
        if (ts3native_counters.asyncMode != 1) DropAllJobs();
        DiscPoll();  // a script-IC point, as Route.Plan's START (docs/84 §2.4)
        P5cFlush();  // P5c: parked destroy references, when no job runs (docs/88 §5.1 flush points)
        const bool ptOk = idx == 1 || g_arPtSet == 7;  // kinds 2-3: all three components since the last START
        g_arPtSet = 0;
        // docs/134 foreign START (ts3fixes 0.36.7+, kind 0 on this channel; older builds answered 0 here): Route.Plan from a
        // yielding task that is not the route's Sim. fbMode 0: refused as "Foreign" (the log then splits NoStart)
        if (!idx) {
            InterlockedIncrement(&ts3native_counters.fbForeign);
            if (ts3native_counters.fbMode != 1) { g_collect = 0; return RfbRefuse(handle, RFB_FOREIGN); }
            return AsyncStart(handle, 0, NULL, true);
        }
        if (!idx || !ptOk) { g_collect = 0; return idx ? AsyncRefuse(handle, RFB_NOSTART, idx, false) : 0; }
        // docs/176: foreign kind STARTs need ForeignKinds (afKinds) and afMode 1, else "Foreign" (the sync line names it);
        // own STARTs need the kind in Kinds (a kind hooked only for ForeignKinds keeps its own-task plans synchronous)
        const bool fgn = (optHi & kArFgnHi) != 0;
        if (fgn) {
            InterlockedIncrement(&ts3native_counters.afSeen[idx - 1]);
            if (ts3native_counters.afMode != 1 || !(ts3native_counters.afKinds >> (idx - 1) & 1)) {
                g_collect = 0;
                return RfbRefuse(handle, RFB_FOREIGN);
            }
        } else if (!(g_arOwnKinds >> (idx - 1) & 1)) {
            g_collect = 0;
            return AsyncRefuse(handle, RFB_NOSTART, idx, false);
        }
        float pt[3];
        memcpy(pt, g_arPt, sizeof(pt));
        return AsyncStart(handle, idx, idx >= 2 ? pt : NULL, fgn);
    }
    if (optLo != 0 || (optHi & ~(kAsyncCancelHi | kNoYieldHi)) != kAsyncOptHi) return g_getOptOrig(handle, optLo, optHi, options2);
    if (optHi & kNoYieldHi) return !CanYield();
    if (ts3native_counters.asyncMode != 1) DropAllJobs();  // switched off mid-job: READY then says "done", Route.Plan plans inline
    DiscPoll();  // a script-IC point: deferred discourage footprints may be applied here (docs/84 §2.4)
    P5cFlush();
    if (!(u8)options2) return AsyncStart(handle);
    AsyncJob* j = GetCurrentThreadId() == g_async.owner ? JobFor(handle) : NULL;
    if (optHi & kAsyncCancelHi) {  // the caller gives up (finally, Sim reset): its job must not outlive it
        g_collect = 0;
        if (j) DropJob(j);
        return 1;
    }
    if (j && j->state == J_PEND) {  // docs/134 §2.6: parked START, one more try this tick
        if (FbRetry(j)) return 0;
        j = NULL;  // given up: READY says done, the plan runs synchronously with the recorded reason
    }
    if (j && (j->state == J_QUEUED || j->state == J_RUNNING)) {
        if (WaitForSingleObject(g_invReady, 0) == WAIT_OBJECT_0) ServeInvoke(HC_READY);  // keep the running job moving
        return 0;
    }
    if (j && j->state == J_SKIPPED) {  // not planned (world changed): the plan runs synchronously
        InterlockedExchange(&j->state, J_FREE);
        j = NULL;
        RfbRefuse(handle, RFB_SKIPPED);
    }
    g_collect = j ? handle : 0;
    return 1;
}

// Every Lock_Acquire in the game passes through here; only the job owner's waits during a job are changed (INFINITE and
// finite deadlines, an absolute GetTickCount): try-lock (deadline 0 = one TryEnter/0 ms wait in the original) in a loop,
// running the job's managed calls in between. A finite deadline that passes ends with one original call with it (one
// last try; the original's own result, -2 = timed out).
static LONG LockAcquireAsync(void* lock, void* edx, const DWORD* deadline, DWORD key);
// [RouteGate] (routegate.cpp) owner tracking for world+0x918: success returns the recursion count (> 0), -1/-2 = failed
static __declspec(thread) LONG t_borrow;  // routegate.cpp releases these without the lock
static bool SypLockBegin(DWORD* slot);  // syncphases.cpp (docs/164)
static void SypLockEnd(DWORD* slot);
static volatile DWORD g_rjpSyncTid;  // routephases.cpp (docs/164): the sim thread's id while a sync plan session is open, else 0
static LONG LockAcquireFull(void* lock, void* edx, const DWORD* deadline, DWORD ret);
// docs/165 F1: a lock other than +0x918 from a thread that is neither the job owner nor the open sync plan session, with no
// QL-shared / SlotEnabledRO hold, ends in g_lockOrig unchanged on the full path: go there with no frame, key, TEB or SEH work
static __forceinline LONG LockAcquirePlain(void* lock, void* edx, const DWORD* deadline, DWORD ret) {
    const DWORD tid = __readfsdword(0x24);  // TEB ClientId.UniqueThread = GetCurrentThreadId()
    if (lock != g_rgLock && tid != g_async.owner && tid != g_rjpSyncTid && !(t_slHold | t_qlShared))
        return g_lockOrig(lock, edx, deadline);
    return LockAcquireFull(lock, edx, deadline, ret);
}
static volatile LONG g_hoOn;  // [HitchAttr] Holders=1 (hitchattr.cpp, docs/165 §8 gap #2): who holds the lock a sim/main wait is on
static LONG HoAcquire(void* lock, void* edx, const DWORD* deadline, DWORD ret);
static __declspec(noinline) LONG __fastcall LockAcquireHook(void* lock, void* edx, const DWORD* deadline) {
    const DWORD ret = (DWORD)(uintptr_t)_ReturnAddress();
    if (t_rpUl && lock == g_rgLock) InterlockedIncrement(&ts3native_counters.ulLockTrip);  // docs/182 §2: never expected
    if (g_hoOn) return HoAcquire(lock, edx, deadline, ret);
    return LockAcquirePlain(lock, edx, deadline, ret);
}
static __declspec(noinline) LONG LockAcquireFull(void* lock, void* edx, const DWORD* deadline, DWORD ret) {
    QlSharedTrip(SLD_LOCK);
    if (t_tuOpen && lock == g_rgLock) TuTrip(0);  // TailUnlock: the worker's own +0x918 back first (a recursive acquire then)
    if (t_snapRun) {  // P4 clone run: the copy's own +0x918 passes straight through; the live lock is a design breach (§12.6)
        if (lock == g_rgLock) P4Abort(P4_AB_LIVELOCK);
        return g_lockOrig(lock, edx, deadline);
    }
    // H6 borrow rule: the owner runs a callback for the job while the worker, holding the job's lock, waits in it; an acquire
    // of that lock here is nested in the job, as it was when the plan ran on this thread. Waiting would deadlock.
    if (lock == g_async.lock && g_inInvoke && g_async.state == A_RUNNING && GetCurrentThreadId() == g_async.owner) {
        t_borrow++;
        InterlockedIncrement(&ts3native_counters.rgBorrows);
        return 2;
    }
    const DWORD key = t_waitKey ? t_waitKey : t_steerRet ? t_steerRet : t_rtoKey ? t_rtoKey : ret;
    volatile DWORD spSlot = 0;  // docs/164: the whole wait (pump included) as a lock frame of an open sync plan session
    const bool sp = SypLockBegin((DWORD*)&spSlot);
    const LONG r = LockAcquireAsync(lock, edx, deadline, key);
    if (sp) SypLockEnd((DWORD*)&spSlot);
    if (g_unOn && lock == g_rgLock && r == 1) UnAcq(key);
    if (lock == g_rgLock && r > 0) {
        if (r == 1) {
            InterlockedIncrement(&ts3native_counters.rgAcquires);
            G11Touch(G11_LOCK, t_steerRet ? t_steerRet : ret);
        }
        ts3native_counters.rgOwnerTid = (LONG)GetCurrentThreadId();
    }
    return r;
}

// A job runs or waits in the queue (only the owner thread queues jobs, so on the owner this cannot turn true meanwhile).
static bool AsyncBusy() {
    if (g_async.state == A_RUNNING) return true;
    for (int i = 0; i < kJobs; i++)
        if (g_jobs[i].state == J_QUEUED || g_jobs[i].state == J_RUNNING) return true;
    return false;
}

// P5b (p5b.cpp, docs/94 §22.9): every game Lock_Acquire below goes through P5bLock (the job's +0x918 lent to steering)
static LONG P5bLock(void* lock, void* edx, const DWORD* deadline);
static void P5bAsk(bool begin);  // p4.cpp P4DoorEnter
static void P5bAbort();          // p4.cpp P4Abort
// docs/165 F1: the watchdog's guarded read in its own frame, so LockAcquireAsync has no SEH prologue
static __declspec(noinline) DWORD LockOwnerTid(void* lock) {
    DWORD owner = 0;
    __try { owner = (DWORD)(uintptr_t)((const CRITICAL_SECTION*)lock)->OwningThread; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return owner;
}
static LONG LockAcquireAsync(void* lock, void* edx, const DWORD* deadline, DWORD key) {
    // with jobs queued the worker may take any lock between jobs and then need this thread for a managed call: pump
    static const DWORD kTry = 0;
    if (*deadline == 0 || GetCurrentThreadId() != g_async.owner || !AsyncBusy()) {
        // v85: a +0x918 wait inside the locomotion pass with no job at all (who else holds it: EA threads, sync worker plans)
        if (*deadline == 0 || lock != g_rgLock || !H2InPass()) return P5bLock(lock, edx, deadline);
        LONG r = P5bLock(lock, edx, &kTry);
        if (r != -2) return r;
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        r = P5bLock(lock, edx, deadline);
        H2LockWait(false, UsSince(t0));
        return r;
    }
    LONG r = P5bLock(lock, edx, &kTry);
    if (r != -2) return r;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    const bool h2Running = g_async.state == A_RUNNING;  // v85 split: a running job vs one queued / held by the window
    InterlockedIncrement(&ts3native_counters.asyncPumpWaits);
    // waiting for the routing lock: the worker starts no further job until this thread has had it (g_rgOffSimOpen)
    const bool hold = lock == g_rgLock;
    if (hold) InterlockedIncrement(&g_rgOffSimOpen);
    bool fb = false, own = false;  // docs/134 §11: a foreign job ran during this wait; docs/178 T4: an own one
    DWORD ownHandle = 0;
    for (DWORD start = GetTickCount(), next = 2000;;) {
        fb = fb || (g_async.state == A_RUNNING && g_jobFb);
        if (!own && g_async.state == A_RUNNING && g_jobOwn) own = true, ownHandle = g_async.handle;
        if (!AsyncBusy()) { r = P5bLock(lock, edx, deadline); break; }  // no job left: wait as the game would
        HANDLE h[2] = {g_invReady, g_asyncDone};
        if (WaitForMultipleObjects(2, h, FALSE, 1) == WAIT_OBJECT_0) ServeInvoke(hold ? HC_PUMP918 : HC_PUMPOTHER);
        if ((r = P5bLock(lock, edx, &kTry)) != -2) break;
        if (*deadline != 0xFFFFFFFF && (LONG)(GetTickCount() - *deadline) >= 0) { r = P5bLock(lock, edx, deadline); break; }
        if (GetTickCount() - start >= next) {
            next += 2000;
            InterlockedIncrement(&ts3native_counters.watchdogs);
            // 0.33.2: who holds it (the lock is a CRITICAL_SECTION at +0) and where the P4 worker / replay consumer are
            const DWORD owner = LockOwnerTid(lock);
            Log("route async: lock %p busy for %lu s during the job of route %08lX, still waiting (owner thread %lu: worker %lu, "
                "replay consumer %lu, last gate acquirer %ld; async state %ld, P4 phase %ld, replay phase 0x%lX)", lock, next / 1000 - 2,
                g_async.handle, owner,
                g_workerTid, g_rpTid, ts3native_counters.rgOwnerTid, g_async.state, g_p4Phase, g_rpPhase);
        }
    }
    if (hold) InterlockedDecrement(&g_rgOffSimOpen);
    const LONG waited = UsSince(t0);
    InterlockedExchangeAdd(&ts3native_counters.asyncPumpUs, waited);
    WaitNote(key, waited);
    HaNote(key, waited, hold ? HA_LOCK : HA_OTHERLOCK);
    FbjcSimWait(waited, fb);
    OwSimWait(waited, own, ownHandle);
    if (hold && H2InPass()) H2LockWait(h2Running, waited);
    return r;
}

static const DWORD IC_ROUTE_PLAN = 0x00765160, ROUTE_GET_OPTION = 0x0074D040, LOCK_ACQUIRE = 0x004E16F0;
static const u8 kIcPlanPrologue[] = {0x0F, 0x57, 0xC0, 0x8B, 0x0D, 0xC4, 0x40, 0x1D, 0x01};     // xorps xmm0,xmm0; mov ecx,[0x11D40C4]
static const u8 kGetOptionPrologue[] = {0x8B, 0x0D, 0xC4, 0x40, 0x1D, 0x01};                     // mov ecx,[0x11D40C4]
static const u8 kLockAcquirePrologue[] = {0x53, 0x55, 0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x24, 0x00};  // push ebx/ebp/esi; mov esi,ecx; cmp byte [esi+24h],0

// [RouteAsync]: returns the start value of the switch, or -1 when off.
static int ReadRouteAsync(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"RouteAsync", L"Enabled", 0, ini)) { Log("route async disabled (Enabled=0)"); return -1; }
    g_invokePrologueAtLoad = Readable((const void*)(uintptr_t)g_invokeExpect, sizeof(kInvokePrologue)) &&
        memcmp((const void*)(uintptr_t)g_invokeExpect, kInvokePrologue, sizeof(kInvokePrologue)) == 0;
    ts3native_counters.rfbLogMs = Clamp(GetPrivateProfileIntW(L"RouteAsync", L"LogFallbackMs", 200, ini), 0, 600000);
    Log("route async: sync fallback log %ld ms (LogFallbackMs, 0 = off)", ts3native_counters.rfbLogMs);
    // docs/134: sync fallback freezes
    ts3native_counters.fbMode = Clamp(GetPrivateProfileIntW(L"RouteAsync", L"FallbackAsync", 0, ini), 0, 1);
    const int capMs = GetPrivateProfileIntW(L"RouteAsync", L"SyncCapMs", 0, ini);
    ts3native_counters.fbCapUs = capMs > 0 ? Clamp(capMs, 50, 60000) * 1000 : 0;
    g_fbStackFrames = Clamp(GetPrivateProfileIntW(L"RouteAsync", L"LogFallbackStack", 0, ini), 0, 16);
    ts3native_counters.fbRetryMode = Clamp(GetPrivateProfileIntW(L"RouteAsync", L"M1Retry", 0, ini), 0, 1);
    const int jobCapMs = GetPrivateProfileIntW(L"RouteAsync", L"FbJobCapMs", 0, ini);  // docs/134 §10
    ts3native_counters.fbjcCapUs = jobCapMs > 0 ? Clamp(jobCapMs, 50, 60000) * 1000 : 0;
    ts3native_counters.fbjcMode = ts3native_counters.fbjcCapUs ? 1 : 0;
    Log("route async: FallbackAsync=%ld SyncCapMs=%ld LogFallbackStack=%ld M1Retry=%ld FbJobCapMs=%ld", ts3native_counters.fbMode,
        ts3native_counters.fbCapUs / 1000, g_fbStackFrames, ts3native_counters.fbRetryMode, ts3native_counters.fbjcCapUs / 1000);
    ReadPlanDeadline(ini);  // docs/164 §7 (plandeadline.cpp): PlanDeadlineMs / PlanDeadlineOwnMs / FailMemo
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
// [RouteGate] needs it only to learn the routing lock (all three off: a plain pass-through).
// Returns true when installed (the log must then stay open for the periodic counters and the watchdog).
static bool InstallPlanDetour(bool cache, bool worker, bool budget, bool gate) {
    if (!cache && !worker && !budget && !gate) return false;
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

#include "apex.cpp"
#include "savewrite.cpp"
#include "decompress.cpp"
#include "alloc.cpp"
#include "waterjps.cpp"
#include "savecompress.cpp"
#include "footprint.cpp"
#include "heightcache.cpp"
#include "gcthreads.cpp"
#include "threadstack.cpp"
#include "iclock.cpp"
#include "routegate.cpp"
#include "rtsites.cpp"
#include "discourage.cpp"
#include "steerro.cpp"
#include "lotfd.cpp"
#include "obstructro.cpp"
#include "avoidro.cpp"
#include "slotro.cpp"
#include "gate0ro.cpp"
#include "locowin.cpp"
#include "goalgen.cpp"  // docs/177 §3: [GoalGen] SameRing (D1)
#include "rcguard.cpp"  // docs/177 §7: [RouteAsync] RefcountGuard
#include "p10.cpp"
#include "a10.cpp"
#include "simloop.cpp"
#include "monothread.cpp"
#include "lotstream.cpp"
#include "jobqueue.cpp"
#include "lotsincos.cpp"
#include "idcache.cpp"
#include "verify.cpp"
#include "vcall.cpp"
#include "values.cpp"
#include "routesnap.cpp"
#include "autshadow.cpp"
#include "snap.cpp"
#include "pools.cpp"
#include "replay.cpp"
#include "p4.cpp"
#include "p4clone.cpp"
#include "p5.cpp"
#include "p5b.cpp"
#include "p5c.cpp"
#include "tailunlock.cpp"
#include "rtoro.cpp"
#include "fgro.cpp"  // docs/174: [PlacementRO] (after rtoro.cpp: S2's QL and SrQuery, AvMayRun, PatchCalls)
#include "loadprof.cpp"
#include "planrepeat.cpp"
#include "replandedupe.cpp"
#include "optsinit.cpp"
#include "hitchattr.cpp"
#include "unseen.cpp"
#include "worldimage.cpp"
#include "crashreport.cpp"
#include "earlyout.cpp"
#include "plandeadline.cpp"  // docs/164 §7: [RouteAsync] PlanDeadlineMs / FailMemo (routephases.cpp takes its trampolines)
#include "routephases.cpp"
#include "syncphases.cpp"  // docs/164: [RouteJobPhases] SyncPlans (needs routephases.cpp)
#include "cellmemo.cpp"
#include "hostcalls.cpp"
#include "fgrender.cpp"  // docs/162 §7: [Harness] ForegroundRender (TEST ONLY)

// [D3DMultithreaded] (PLAN #4, docs/37 C5, docs/43 §1): Device_Create 0x611080 passes `esi` as the CreateDevice behaviour
// flags: 0x40 (software VP) at 0x6112A4, replaced by 0x20 (hardware VP) at 0x6112B9 when the caps allow [verified]. Adding
// D3DCREATE_MULTITHREADED (4) makes the device lock every call, so the ThumbnailManager ICs that create and fill a texture
// on the sim thread (0xD5D910) no longer race the main thread's rendering. The deferral to the device thread is not built:
// the IC callers wait for the result, and a sim-to-main wait can deadlock against the frame lock (docs/41 S1) [inferred].
static const Site kD3dFlags[] = {
    {0x006112A4, {0xBE, 0x40, 0x00, 0x00, 0x00}, 5, 1},  // mov esi,40h (D3DCREATE_SOFTWARE_VERTEXPROCESSING)
    {0x006112B9, {0xBE, 0x20, 0x00, 0x00, 0x00}, 5, 1},  // mov esi,20h (D3DCREATE_HARDWARE_VERTEXPROCESSING)
};
static bool ApplyD3dMultithreaded(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"D3DMultithreaded", L"Enabled", 0, ini)) { Log("d3d multithreaded disabled (Enabled=0)"); return false; }
    for (const Site& s : kD3dFlags)  // both or neither (PatchImm32 checks one group)
        if (!Matches((const void*)(uintptr_t)s.addr, s.expect, s.len)) { Log("SKIP d3d multithreaded: bytes at 0x%08X differ", s.addr); return false; }
    const bool ok = PatchImm32("D3DCREATE_MULTITHREADED (software VP)", &kD3dFlags[0], 1, 0x44) &&
                    PatchImm32("D3DCREATE_MULTITHREADED (hardware VP)", &kD3dFlags[1], 1, 0x24);
    ts3native_counters.d3dMtReady = ok ? 1 : 0;
    return false;  // start-up patch only: no periodic log needed
}

static void Run(HMODULE self) {
    wchar_t path[MAX_PATH], ini[MAX_PATH], log[MAX_PATH], exe[MAX_PATH];
    bool keepLog = false;
    g_mainTid = GetCurrentThreadId();  // DllMain of the ASI: the exe's main thread
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wchar_t* dot = wcsrchr(path, L'.');
    if (dot) *dot = 0;
    swprintf_s(ini, L"%s.ini", path);
    swprintf_s(log, L"%s.log", path);
    CrPreLog(ini, log);  // [CrashReport]: the previous session's last lines, before the log is truncated
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
        SeedWaitKeys();
        InstallXcptLog(ini);
        ApexDetect(ini);  // docs/148 §7: before every install (stand-downs, crash report line)
        InstallCrashReport(ini, log);
        ApplyGc(ini);
        bool cache = ApplyRouteCache(ini);
        bool worker = ApplyRouteWorker(ini);
        const int asyncMode = ReadRouteAsync(ini);
        if (asyncMode >= 0 && !worker && !(worker = StartWorker())) Log("FAIL route async: worker thread start (err %lu)", GetLastError());
        const bool gate = ReadRouteGate(ini);  // checks every prologue first: nothing is hooked if one differs
        // [PlanRepeat] (planrepeat.cpp) counts inside this detour
        // docs/134 SyncCapMs needs the cost-function stubs even with [RouteBudget] off (budgetMode then stays 0: no global cap)
        // docs/164 §7: PlanDeadlineMs cuts world A* through the same stubs
        const bool budget = ApplyRouteBudget(ini) || (asyncMode >= 0 && (ts3native_counters.fbCapUs > 0 || ts3native_counters.fbjcCapUs > 0 ||
                                                                         ts3native_counters.pdUs > 0 || ts3native_counters.pdOwnUs > 0));
        const bool earlyOut = ReadEarlyOut(ini);  // docs/142: its request scope lives in this detour
        keepLog = InstallPlanDetour(cache, worker, budget, gate || earlyOut || GetPrivateProfileIntW(L"PlanRepeat", L"Enabled", 0, ini));
        if (earlyOut) InstallEarlyOut(keepLog);
        if ((ts3native_counters.fbCapUs || ts3native_counters.fbjcCapUs) && !ts3native_counters.budgetReady)
            Log("FAIL route async: SyncCapMs / FbJobCapMs need the cost-function stubs, no cap");
        if (asyncMode >= 0 && worker && keepLog && InstallAsync(asyncMode)) InstallA10(ini);
        InstallRtSites(ini, gate && InstallRouteGate(g_planOrig != NULL));
        InstallDiscourage(ini);
        InstallSteerRO(ini);
        InstallLotFd(ini);
        InstallObstructedRO(ini);
        InstallAvoidRO(ini);
        InstallSlotRo(ini);
        InstallGate0Ro(ini);  // docs/152 §G1: after SteerRO and AvoidRO (reads their ready state for the log)
        InstallLocoWindow(ini);
        InstallH2Ic(ini);     // docs/177 §4: the same start hold around the script ICs that write footprint refcounts
        InstallGoalGen(ini);  // docs/177 §3: D1, the duplicate ring of a radius route
        InstallRefcountGuard(ini);  // docs/177 §7: after the steering frames (InstallRtSites) and AvoidRO's A-site
        InstallTailUnlock(ini);  // docs/150 §11: after the steering frames (InstallRtSites)
        InstallRouteToObjRO(ini);  // docs/140 §12: after [SteerRO] (QL, W1-W6) and [SlotEnabledRO] (SH)
        InstallPlacementRO(ini);   // docs/174: S2's writer set, like [RouteToObjRO]
        InstallP10(ini);
        InstallReplanDedupe(ini);  // docs/150 §13: after A10 (its collect detours) and P10 (probe stubs keep their ICs)
        InstallPlanDeadline();     // docs/164 §7: after the plan detour, the cost stubs and InstallAsync (SyncIc); before [RouteJobPhases]
        keepLog = ApplySaveWrite(ini) || keepLog;
        ApplySaveFlush(ini);
        // the NOYIELD query guards ts3fixes' other yields too, so it is answered with async off (START then says no)
        if (!g_getOptOrig)
            g_getOptOrig = (GetOptionFn)InstallDetour("Route_GetOption", ROUTE_GET_OPTION, kGetOptionPrologue, sizeof(kGetOptionPrologue), (void*)GetOptionHook);
        keepLog = ApplyDecompress(ini) || keepLog;
        keepLog = ApplyAlloc(ini) || keepLog;
        keepLog = ApplyWaterJps(ini) || keepLog;
        keepLog = ApplySaveCompress(ini) || keepLog;
        keepLog = ApplyFootprintIndex(ini) || keepLog;
        keepLog = ApplyHeightCache(ini) || keepLog;
        keepLog = ApplyGcThreadCheck(ini) || keepLog;
        keepLog = ApplyThreadStack(ini) || keepLog;  // docs/66 §6: exe IAT CreateThread/_beginthreadex (default off)
        keepLog = ApplyIcLockCheck(ini) || keepLog;
        keepLog = ApplyMonoThread(ini) || keepLog;
        keepLog = ApplyWorldImage(ini) || keepLog;  // after [MonoThread]: second callback on the Simulate loop-back hook
        keepLog = ApplyLotStream(ini) || keepLog;
        keepLog = ApplyJobQueue(ini) || keepLog;
        ApplyD3dMultithreaded(ini);
        ApplyForegroundRender(ini);  // TEST ONLY (docs/162 §7): unfocused harness runs pace like focused ones
        keepLog = ApplyLotSinCos(ini) || keepLog;
        keepLog = ApplyIdCache(ini) || keepLog;
        keepLog = ApplyProbeValues(ini) || keepLog;  // after every behaviour section: it counts inside their stubs or skips
        keepLog = ApplyRouteSnap(ini) || keepLog;  // after [ProbeValues]: counts inside its, [SteerRO]'s and [RouteGate]'s stubs
        InstallLotRemoveGuard(ini);                 // after [RouteSnap]: its stub at 0x635700 runs the same guard
        keepLog = ApplySnapImages(ini) || keepLog;  // P3 images on P1's sites
        keepLog = ApplyP4(ini) || keepLog;          // P4 part A: thunks, door stub, shadow (after [ProbeValues] and P3)
        PcRegister();                               // P4 part B: the replay clone as P4's clone provider
        keepLog = ApplyProbeSnap(ini) || keepLog;
        keepLog = ApplyLoadProf(ini) || keepLog;  // measurement: skips every site another section already hooked
        keepLog = ApplyPlanRepeat(ini) || keepLog;  // after [RouteSnap] (its world version) and the plan detour
        keepLog = ApplyOptsInit(ini) || keepLog;    // I-94-322: the planner options' hub references start at 0 (default off)
        keepLog = ApplyHitchAttr(ini) || keepLog;   // docs/96 §9: per-frame sim waits for the route job (observe only)
        keepLog = ApplyUnseenProbe(ini) || keepLog;  // docs/143: locomotion work split visible / unseen (observe only)
        keepLog = ApplyRouteJobPhases(ini) || keepLog;  // docs/150 §7: after every owner of a phase site (observe only)
        keepLog = ApplyCellMemoProbe(ini) || keepLog;   // docs/150 §12: after [RouteJobPhases] (its job window and cell A* frame)
        keepLog = ApplyHostCalls(ini) || keepLog;       // docs/159: after [RouteJobPhases] (rjpReady) and P4 / [ProbeValues] (door chain)
        keepLog = ApplyVerify(ini) || keepLog;  // last: probes skip whatever another section already hooked
        P5Snapshot();                           // P5 M2-1: the code bytes of every site the clone run passes, after every install
        keepLog = ApplyAutShadow(ini) || keepLog;  // docs/122 part 1: guard thunks after every other table owner (routesnap)
        ApexStartLate();  // zzApexRadiance.asi loads after us (its lines reach the log only while it stays open)
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
    } else if (reason == DLL_PROCESS_DETACH) {
        CrCleanExit();
        FgRevert();  // [Harness] ForegroundRender: original bytes back
    }
    return TRUE;
}
#endif
