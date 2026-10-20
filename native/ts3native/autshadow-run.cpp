// [AutShadow] part 2 (docs/122 §1.2, §4, §14): request, cut, reference run, worker dispatch, window and stop, the
// AutShadowNative internal calls of ts3fixes, the rooted column and the array allocator. Included by autshadow.cpp
// (after the snapshot code, before the install). Rides [MonoThread]: the cut and the window use its Simulate slot stub
// (g_mtOnEnter / g_mtOnLeave, one slot owner), the worker is its M5 thread (MtSubmit, g_mtJobHook).
//
// One shadow search (docs/122 §1.2):
//   ts3fixes request hook (sim thread, B13 entry): Begin() -> AddRow(...) -> Post(id)
//   return of Simulate (AsSimLeave): cut (AsCut), then Mode 1/2: the reference method on the sim thread with the script
//     RNG (host +0xC30) saved and restored, then the work method on the M5 worker; Mode 0 or no idle worker: finish at once
//   top of the next Simulate (AsSimEnter): window closed; a worker still inside gets the stop flag (overrun, A5)
//   a later return of Simulate, once the job left the worker: the finish method on the sim thread (compare, counters)
// Managed methods are static, no arguments: request and results live in rooted ts3fixes statics.

enum { AS_RUN_IDLE, AS_RUN_WINDOW, AS_RUN_WAIT };
enum { AS_LAST_OK, AS_LAST_STOPPED, AS_LAST_EPOCH, AS_LAST_NOWORKER, AS_LAST_REFUSED, AS_LAST_EXC };
static int g_asMode = -1, g_asSample = 4, g_asWarmIn = 20, g_asDump;
static void* g_asMFinish;  // MonoMethod* of AutShadow's finish / reference / work (SetMethods)
static void* g_asMRef;
static void* g_asMWork;
static volatile LONG g_asReqId;         // posted request, 0 = none
static volatile LONG g_asRun;           // AS_RUN_*
static LONG g_asLast = AS_LAST_OK;      // what LastRun() tells finish
static LONG g_asRunEpoch, g_asTrack0, g_asWaitTicks;
static volatile LONG g_asJobExc;
static LARGE_INTEGER g_asWin0, g_asJob0;
static double g_asUsPerTick;
static HANDLE g_asJsonl = INVALID_HANDLE_VALUE;
static wchar_t g_asJsonlPath[MAX_PATH];
// game addresses; selftest and monohost point them at fakes or the host's copies
typedef void(__cdecl* AsAddIcFn)(const char* name, const void* fn);
typedef void(__cdecl* AsAddRootsFn)(void* lo, void* hi);
typedef void*(__cdecl* AsGcMallocFn)(size_t n);
static AsAddIcFn g_asAddIc = (AsAddIcFn)(uintptr_t)0x00E82550;          // mono_add_internal_call
static AsAddRootsFn g_asAddRoots = (AsAddRootsFn)(uintptr_t)0x00E4D9F0;  // GC_add_roots (P3 site 17)
static AsGcMallocFn g_asGcMalloc = (AsGcMallocFn)(uintptr_t)0x00E4BE30;  // GC_malloc (P3-locked)
static u8** g_asHostPtr = (u8**)(uintptr_t)0x011EE514;                  // MonoScriptHost; script RNG at +0xC30 (docs/27 §1.1)
static const DWORD AS_RNG_OFF = 0xC30, AS_RNG_LEN = 0x9C8;  // MT state (624 words + index) up to the LCG at +0x15F8 [inferred end]

static LONG AsUs(const LARGE_INTEGER& t0) {
    LARGE_INTEGER t1;
    QueryPerformanceCounter(&t1);
    return (LONG)((t1.QuadPart - t0.QuadPart) * g_asUsPerTick);
}
static void AsMax(LONG& m, LONG v) { if (v > m) m = v; }

// Arrays answered to the worker (docs/122 §3.2 a-lists, a-lot): a MonoArray {vtable, sync, max_length, bounds, data at +16}
// [verified in monohost: length at +8, as AsRefresh reads it] from GC_malloc (scanned, zeroed; P3 lock while the worker runs).
// Every array answer in the descriptor table has 4-byte elements (object references, int[], uint[]).
static void* __cdecl AsNewArrayGc(DWORD vtable, DWORD n) {
    if (n > (1u << 20)) return NULL;
    DWORD* a = (DWORD*)g_asGcMalloc(16 + n * 4);
    if (a) a[0] = vtable, a[2] = n;
    return a;
}

// first cut (sim thread, outside any GC): the rooted column, pools and the two kept objects become GC roots
static void AsRootOnce() {
    Ts3nAutShadow& k = ts3native_autshadow;
    if (k.rooted & 1) return;
    if (g_asAddRoots) {
        for (int v = 0; v < 3; v++) {
            AsTab& t = v ? g_as->v[v - 1] : g_as->m;
            g_asAddRoots(t.refs, t.pool + AS_POOL);
        }
        g_asAddRoots(g_as->poolTmp, g_as->keep + 4);
        g_asRefsRooted = true;
        k.rooted |= 1;
    }
    if (!g_asNewArray && g_asGcMalloc) g_asNewArray = AsNewArrayGc;
    if (g_asNewArray) k.rooted |= 2;
}

// ---- managed calls (0xE5A600 on this thread, monothread-m5.cpp g_mtInvoke) ----
static bool AsInvoke(void* m, LONG* us) {
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    void* exc = NULL;
    g_mtInvoke(m, NULL, NULL, &exc);
    if (us) *us = AsUs(t0);
    return exc == NULL;
}

static void AsFinish(LONG status) {
    Ts3nAutShadow& k = ts3native_autshadow;
    g_asLast = status;
    k.finishRuns++;
    if (g_asMFinish && !AsInvoke(g_asMFinish, NULL)) k.finishExc++;
    InterlockedExchange(&g_asRun, AS_RUN_IDLE);
    InterlockedExchange(&g_asReqId, 0);
}

// the reference run on the sim thread: same driver, same cut, script RNG put back afterwards (docs/122 §3.4)
static void AsReference() {
    Ts3nAutShadow& k = ts3native_autshadow;
    static u8 saved[AS_RNG_LEN];
    u8* host = g_asHostPtr ? *g_asHostPtr : NULL;
    u8* rng = host ? host + AS_RNG_OFF : NULL;
    if (rng) memcpy(saved, rng, AS_RNG_LEN);
    LONG us = 0;
    if (!AsInvoke(g_asMRef, &us)) k.refExc++;
    if (rng && memcmp(saved, rng, AS_RNG_LEN)) { k.rngDraws++; memcpy(rng, saved, AS_RNG_LEN); }
    k.refRuns++, k.refUs += us;
    AsMax(k.refMaxUs, us);
}

// worker, around each M5 job (only ours count): hold the published version, time the run
static void __cdecl AsJobHook(int phase, void* method) {
    if (method != g_asMWork) return;
    Ts3nAutShadow& k = ts3native_autshadow;
    if (phase == 0) {
        InterlockedExchange(&g_asHeld, g_asPub);  // AsWorkerBegin without clearing stop: the sim may have set it already
        if (g_asRunEpoch != g_asEpoch) InterlockedExchange(&g_asStop, 1);  // the world moved between cut and start
        QueryPerformanceCounter(&g_asJob0);
        return;
    }
    const LONG us = AsUs(g_asJob0);
    InterlockedExchangeAdd(&k.workUs, us);
    if (us > k.workMaxUs) InterlockedExchange(&k.workMaxUs, us);
    if (phase == 2) InterlockedIncrement(&k.workExc), InterlockedExchange(&g_asJobExc, 1);
    AsWorkerEnd();
}

static bool AsWorkerIdle() { return g_mtWorker && ts3native_mtprobe.workerState == 2 && !g_mtBusy; }

// return of MonoScriptHost_Simulate (sim thread): finish a run whose job ended, else cut for a posted request
static void __cdecl AsSimLeave() {
    Ts3nAutShadow& k = ts3native_autshadow;
    if (g_asRun == AS_RUN_WAIT) {
        if (g_mtBusy) { k.waitTicks++; return; }  // the job (or a skip) has not left the worker yet
        LONG st = g_asLast;
        if (g_asJobExc) st = AS_LAST_EXC;
        else if (ts3native_mtprobe.workerTrackSkips != g_asTrack0) st = AS_LAST_REFUSED, k.workRefused++;
        AsFinish(st);
        return;
    }
    if (g_asRun != AS_RUN_IDLE || !g_asReqId) return;
    AsRootOnce();
    AsCut();
    k.cutReq++;
    g_asRunEpoch = g_asEpoch;
    if (g_asMode == 0 || !g_asMRef || !g_asMWork) { k.noWorker++; AsFinish(AS_LAST_NOWORKER); return; }
    AsReference();
    if (!AsWorkerIdle()) { k.noWorker++; AsFinish(AS_LAST_NOWORKER); return; }
    g_asWorkerTid = g_mtWorkerTid;
    g_asLast = AS_LAST_OK;
    g_asJobExc = 0;
    g_asTrack0 = ts3native_mtprobe.workerTrackSkips;
    InterlockedExchange(&g_asStop, 0);
    InterlockedExchange(&g_asRun, AS_RUN_WINDOW);
    QueryPerformanceCounter(&g_asWin0);
    if (!MtSubmit(g_asMWork, NULL, NULL)) { k.workRefused++; AsFinish(AS_LAST_REFUSED); return; }
    k.workRuns++;
}

// top of MonoScriptHost_Simulate (sim thread): the window closes; a worker still inside is told to stop (no wait)
static void AsRegisterIcs();
static void __cdecl AsSimEnter() {
    Ts3nAutShadow& k = ts3native_autshadow;
    if (!k.icRegistered) AsRegisterIcs();
    if (g_asRun != AS_RUN_IDLE && g_asRunEpoch != g_asEpoch && g_asLast == AS_LAST_OK) {
        InterlockedExchange(&g_asStop, 1);
        k.epochDrops++;
        g_asLast = AS_LAST_EPOCH;
    }
    if (g_asRun == AS_RUN_WINDOW) {
        const LONG us = AsUs(g_asWin0);
        k.windowUs += us;
        AsMax(k.windowMaxUs, us);
        if (g_mtBusy) {
            InterlockedExchange(&g_asStop, 1);
            k.overrun++;
            if (g_asLast == AS_LAST_OK) g_asLast = AS_LAST_STOPPED;
        }
        InterlockedExchange(&g_asRun, AS_RUN_WAIT);
    }
}

// ---- AutShadowNative internal calls (ts3fixes, docs/122 §3.5; registered at the first Simulate tick) ----
static int __cdecl AsIcVersion() { return 2; }
static int __cdecl AsIcConfig(int key) {
    switch (key) {
        case 0: return g_asMode;
        case 1: return g_asSample;
        case 2: return g_asWarmIn;
        case 3: return g_asDump;
        case 4: return AsWorkerIdle() ? 1 : 0;
        default: return 0;
    }
}
static int __cdecl AsIcIsWorker() { return g_asWorkerTid && GetCurrentThreadId() == g_asWorkerTid; }
static void __cdecl AsIcAbort(int reason) { AsManagedAbort(reason); }
// the abort word and the stop flag belong to the worker's run: on any other thread they read as clear
static int __cdecl AsIcAborted() { return AsIcIsWorker() ? AsAborted() : -1; }
static void __cdecl AsIcResetAbort() { if (AsIcIsWorker()) AsResetAbort(); }
static int __cdecl AsIcStopped() { return AsIcIsWorker() && g_asStop != 0; }
static int __cdecl AsIcBegin() {
    if (g_asMode < 0 || !g_as || !g_asMFinish || AsIcIsWorker()) return 0;
    if (g_asRun != AS_RUN_IDLE || g_asReqId) { ts3native_autshadow.busy++; return 0; }
    memset(&g_as->always, 0, sizeof(AsSet));  // reachable Sims are per request
    return 1;
}
static void __cdecl AsIcAddRow(DWORD lo, DWORD hi, int kind) {
    if (!AsIcIsWorker() && g_asRun == AS_RUN_IDLE) AsAddRowKind(lo | (AsU64)hi << 32, kind);
}
static void __cdecl AsIcPost(int id) {
    if (AsIcIsWorker() || g_asRun != AS_RUN_IDLE || g_asMode < 0) return;
    InterlockedExchange(&g_asReqId, id ? id : 1);
    ts3native_autshadow.req++;
}
static int __cdecl AsIcLastRun() { return g_asLast; }
static void __cdecl AsIcSetMethods(void* finish, void* reference, void* work) {
    if (AsIcIsWorker() || g_asRun != AS_RUN_IDLE) return;
    g_asMFinish = finish, g_asMRef = reference, g_asMWork = work;
}
static void __cdecl AsIcSetRefs(void* empty, void* fmtExc) {
    if (!g_as || AsIcIsWorker()) return;
    g_as->keep[0] = (DWORD)(uintptr_t)empty, g_as->keep[1] = (DWORD)(uintptr_t)fmtExc;  // rooted (AsRootOnce)
    g_asEmptyStr = empty, g_asFmtExc = fmtExc;
    if (empty) ts3native_autshadow.rooted |= 4;
    if (fmtExc) ts3native_autshadow.rooted |= 8;
}
// MonoString {vtable, sync, length, UTF-16} [inferred Mono 1.x layout, as AsStrKey]: one line to autshadow.jsonl
static void __cdecl AsIcLog(const DWORD* s) {
    Ts3nAutShadow& k = ts3native_autshadow;
    if (!s || k.logLines >= 100000) return;
    if (g_asJsonl == INVALID_HANDLE_VALUE) {
        if (!g_asJsonlPath[0]) return;
        g_asJsonl = CreateFileW(g_asJsonlPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_asJsonl == INVALID_HANDLE_VALUE) { g_asJsonlPath[0] = 0; return; }
    }
    const LONG len = (LONG)s[2];
    char buf[4096];
    int n = len > 0 && len < 1300 ? WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)(s + 3), len, buf, sizeof(buf) - 2, NULL, NULL) : 0;
    if (n <= 0) return;
    buf[n++] = '\n';
    DWORD w;
    WriteFile(g_asJsonl, buf, (DWORD)n, &w, NULL);
    k.logLines++;
}

static void AsRegisterIcs() {
    static const struct { const char* name; const void* fn; } kIcs[] = {
        {"Ts3Fixes.AutShadowNative::Version", (const void*)AsIcVersion},
        {"Ts3Fixes.AutShadowNative::Config", (const void*)AsIcConfig},
        {"Ts3Fixes.AutShadowNative::IsWorker", (const void*)AsIcIsWorker},
        {"Ts3Fixes.AutShadowNative::Abort", (const void*)AsIcAbort},
        {"Ts3Fixes.AutShadowNative::Aborted", (const void*)AsIcAborted},
        {"Ts3Fixes.AutShadowNative::ResetAbort", (const void*)AsIcResetAbort},
        {"Ts3Fixes.AutShadowNative::Stopped", (const void*)AsIcStopped},
        {"Ts3Fixes.AutShadowNative::Begin", (const void*)AsIcBegin},
        {"Ts3Fixes.AutShadowNative::AddRow", (const void*)AsIcAddRow},
        {"Ts3Fixes.AutShadowNative::Post", (const void*)AsIcPost},
        {"Ts3Fixes.AutShadowNative::LastRun", (const void*)AsIcLastRun},
        {"Ts3Fixes.AutShadowNative::SetMethods", (const void*)AsIcSetMethods},
        {"Ts3Fixes.AutShadowNative::SetRefs", (const void*)AsIcSetRefs},
        {"Ts3Fixes.AutShadowNative::Log", (const void*)AsIcLog},
    };
    if (!g_asAddIc) return;
    for (const auto& ic : kIcs) g_asAddIc(ic.name, ic.fn);
    ts3native_autshadow.icRegistered = (LONG)(sizeof(kIcs) / sizeof(kIcs[0]));
}

// [AutShadow] Mode/Sample/WarmIn/DumpOnMismatch; the cut needs [MonoThread]'s Simulate slot stub (Enabled=1, any Mode)
static void AsInstallRun(const wchar_t* ini, bool slotStub) {
    Ts3nAutShadow& k = ts3native_autshadow;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_asUsPerTick = 1e6 / (double)f.QuadPart;
    const int mode = ini ? GetPrivateProfileIntW(L"AutShadow", L"Mode", 0, ini) : 0;
    g_asSample = ini ? GetPrivateProfileIntW(L"AutShadow", L"Sample", 4, ini) : 4;
    g_asWarmIn = ini ? GetPrivateProfileIntW(L"AutShadow", L"WarmIn", 20, ini) : 20;
    g_asDump = ini ? GetPrivateProfileIntW(L"AutShadow", L"DumpOnMismatch", 0, ini) : 0;
    if (ini) {
        wcscpy_s(g_asJsonlPath, ini);
        wchar_t* slash = wcsrchr(g_asJsonlPath, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - g_asJsonlPath), L"autshadow.jsonl");
        else g_asJsonlPath[0] = 0;
    }
    g_asMode = slotStub ? (mode < 0 || mode > 2 ? 0 : mode) : -1;
    k.mode = g_asMode;
    if (!slotStub) return;
    g_mtOnEnter = AsSimEnter;
    g_mtOnLeave = AsSimLeave;
    g_mtJobHook = AsJobHook;
}
