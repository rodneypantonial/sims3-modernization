// [RouteJobPhases] (docs/150 §7, counters v87), observe only: where the route worker's job time goes. Per job kind (own START,
// own A10 kind, foreign/fallback START) and phase: entries and SELF time (a phase nested in another is subtracted from the outer
// one, so the phases plus RJP_OTHER sum to the job time exactly; nothing is counted twice). Included by ts3native.cpp after
// loadprof.cpp, installed after every other section except [Verify].
//
// Timing runs only on the worker inside a job: ServeAsync sets g_rjpTid to the worker's id around CallIc, and every stub first
// compares the TEB thread id (fs:[0x24]) with it, so other threads (the sim thread's allocator calls and plans included) pay a
// compare and a jump. On the worker the stub is loadprof.cpp's pattern: pushad/pushfd, RjpEnter(phase, return slot), the slot
// swapped for RjpLeaveThunk, the real return kept on a per-thread stack of 48 (frames an exception unwound are dropped at the
// next entry). The phase code runs with the same registers, flags and stack.
// Sites: an entry whose prologue still matches gets our detour. An entry another section owns is timed one step later instead:
// our stub is put into that section's trampoline pointer (Route_PlanLocked g_planOrig, Lock_Acquire g_lockOrig, world A*
// g_eoAsOrig, LotGraph_Build g_rgTramp[3] or g_lpJmp[LP_LOTGRAPH], ApplyPendingUpdates g_rtTramp[RT_PENDING]), so the owner's
// stub sees the real caller first and its own leave thunk is what we save and restore (LIFO). Host-call waits are a frame
// pushed by Proxy itself (RJP_HOST): plans the sim sends back during the wait are its children.
// Phases (docs/150 §2): lock = Lock_Acquire on the worker (contended wait plus the uncontended call; +0x918 is taken before the
// job clock starts); alloc = EA_AllocTagged + EA_Free (their callees are the allocator); candidate = PathPlanner_PlanHierarchical
// own code + LotSearch_Entry; lot leg = LotPlan own code incl. LotPlan_Inner and InRoomPlan own code; plan = Route_PlanLocked
// own code (RoutingJob_Execute, PathPlan_WithRetries bookkeeping, sub-path split).
// shortcut: each timed call costs two QPC reads on the worker (allocator calls dominate); the job time grows by that, so read
// shares, not absolute ms, and compare rjpJobUs with asyncUs. Upgrade to rdtsc if the overhead shows above ~5%.
// v92 (docs/150 §14): allocator frames also go to a per-call-site table (row = allocator return address + the return address of
// the innermost open phase frame), a size histogram, PathNodes_Insert's grow capacities and allocator calls per job.
// Self time is carried per phase in ticks (t.rem) so frames shorter than 1 us are not truncated to 0 (v87 dropped up to ~1 us
// per allocator call).
// Prologues checked by bytes (whole instructions, no relative operand, Steam 1.67, docs/150 §7). The branch-into scan of the
// stolen bytes (I-150-2) was done for the fix: no intra-function branch lands inside any of the 15 prologues; steerro-test
// STEERRO_PHASES=1 runs K8's planner with the stubs on.

enum { RJP_PLAN, RJP_CELL, RJP_POST, RJP_INSERT, RJP_WORLD, RJP_LOTLEG, RJP_PORTAL, RJP_CAND, RJP_LOTGRAPH, RJP_PENDING, RJP_WATER,
       RJP_ALLOC, RJP_LOCK, RJP_HOST, RJP_OTHER, RJP_N };
static_assert(RJP_N == 15, "rjpN/rjpUs/rjpMaxUs are [15] (counters v87)");
static void CmCell(LONGLONG incl);  // cellmemo.cpp ([CellMemoProbe], docs/150 §12): a closed cell A* frame
static void CmJob(bool begin);      // cellmemo.cpp: job start / end
// syncphases.cpp ([RouteJobPhases] SyncPlans=1, docs/164, counters v97): the same stubs on sim-thread sync plans
enum { RJP_KSYNC = 3 };  // t_rjp.kind of a sync plan session (not a v87 row: its frames go to SypFrame)
struct RjpFrame;
static bool SypEnter(int site, DWORD* slot);
static void SypFrame(const RjpFrame& f, LONG self, LONGLONG incl);
static void SypAfterLeave(DWORD eax);
static LONG SypLoopOf(DWORD ret, const DWORD* slot);

struct RjpSite { DWORD at; u8 len; u8 pro[10]; u8 phase; const char* name; };
enum { RJS_PLAN, RJS_CELL, RJS_POST, RJS_INSERT, RJS_WORLD, RJS_LOTLEG, RJS_PORTAL, RJS_CAND, RJS_LOTSEARCH, RJS_LOTGRAPH, RJS_PENDING,
       RJS_WATER, RJS_ALLOC, RJS_FREE, RJS_LOCK, RJS_N };
static const RjpSite kRjpSites[RJS_N] = {
    {0x0082C6E0, 6, {0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00}, RJP_PLAN, "Route_PlanLocked"},
    {0x0061E500, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}, RJP_CELL, "QuadTreeCellAStar"},
    {0x0064E380, 7, {0x80, 0x3D, 0x4C, 0x82, 0x15, 0x01, 0x00}, RJP_POST, "path-node post pass"},  // absolute operand
    {0x00629140, 5, {0x83, 0xEC, 0x08, 0x53, 0x55}, RJP_INSERT, "PathNodes_Insert"},
    {0x00676260, 8, {0x83, 0xEC, 0x3C, 0x53, 0x8B, 0x5C, 0x24, 0x58}, RJP_WORLD, "WorldAStar_Search"},
    {0x006410B0, 8, {0x8B, 0x44, 0x24, 0x34, 0xD9, 0x44, 0x24, 0x10}, RJP_LOTLEG, "LotPlan"},
    {0x0063E340, 6, {0x81, 0xEC, 0x14, 0x01, 0x00, 0x00}, RJP_PORTAL, "LotPortalAStar"},
    {0x0065E0D0, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}, RJP_CAND, "PathPlanner_PlanHierarchical"},
    {0x00640E00, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}, RJP_CAND, "LotSearch_Entry"},
    {0x0063CB90, 8, {0x83, 0xEC, 0x5C, 0x53, 0x8B, 0x5C, 0x24, 0x64}, RJP_LOTGRAPH, "LotGraph_Build"},
    {0x0082C420, 9, {0x57, 0x8B, 0xF9, 0x8B, 0x87, 0x5C, 0x09, 0x00, 0x00}, RJP_PENDING, "RoutingWorld_ApplyPendingUpdates"},
    {0x00671930, 7, {0x8B, 0x44, 0x24, 0x24, 0x83, 0xEC, 0x50}, RJP_WATER, "WaterPath_Plan"},
    {0x004E3F90, 8, {0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x4C, 0x24, 0x04}, RJP_ALLOC, "EA_AllocTagged"},
    {0x004E3F70, 6, {0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0}, RJP_ALLOC, "EA_Free"},
    {0x004E16F0, 5, {0x53, 0x55, 0x56, 0x8B, 0xF1}, RJP_LOCK, "Lock_Acquire"},
};

static void* g_rjpJmp[RJS_N];       // where each stub goes on: our trampoline, or the owner's trampoline we took the place of
static volatile DWORD g_rjpTid;     // the worker's id while it runs a job, else 0 (stubs pass straight through)
// g_rjpSyncTid (docs/164): defined in ts3native.cpp before LockAcquireHook (its fast path reads it)
static volatile LONG g_rjpAny[RJS_N];  // docs/164: 1 = every thread enters RjpEnter at this site (SyncPlans: Route_PlanLocked)

struct RjpFrame { DWORD* slot; DWORD ret, parent, arg; int phase, site; LONGLONG t0, child; };
// child = time of the job's outermost frames; rem = sub-us ticks per phase not yet counted; rem100 = the same for v92's 100 ns rows
struct RjpThread { int n, kind; LONG allocs; LONGLONG child, rem[RJP_N], rem100; RjpFrame f[48]; };
static __declspec(thread) RjpThread t_rjp;

static void __cdecl RjpLeaveThunk();
static LONGLONG RjpNow() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static LONG RjpUs(LONGLONG ticks) { return (LONG)(ticks * 1000000 / g_qpf.QuadPart); }

static int RaLog2(DWORD v) { int i = 0; while (v > 1 && i < 31) v >>= 1, i++; return i; }

// v92: one allocator frame into the site table (only the worker writes; the probe reads), the size and grow histograms
static void RaRecord(RjpThread& t, const RjpFrame& f, LONGLONG selfTicks) {
    Ts3nCounters& k = ts3native_counters;
    const bool alloc = f.site == RJS_ALLOC;
    t.allocs++;
    InterlockedIncrement(alloc ? &k.raAllocN : &k.raFreeN);
    const LONGLONG h = selfTicks * 10000000 + t.rem100;  // 100 ns units, remainder carried
    const LONG hns = (LONG)(h / g_qpf.QuadPart);
    t.rem100 = h % g_qpf.QuadPart;
    if (alloc) {
        const int b = RaLog2(f.arg) - 2;
        InterlockedIncrement(&k.raSizeLog2[b < 0 ? 0 : b > 15 ? 15 : b]);
        if (f.ret == 0x006291E3) {  // PathNodes_Insert's grow: size = new capacity x 64
            const int c = RaLog2(f.arg >> 6);
            InterlockedIncrement(&k.raInsCap[c > 15 ? 15 : c]);
            InterlockedExchangeAdd(&k.raInsUs[c > 15 ? 15 : c], hns);
        }
    }
    const DWORD hash = (f.ret * 0x9E3779B1u) ^ (f.parent * 0x85EBCA6Bu);
    for (int i = 0; i < 64; i++) {
        const int r = (hash + i) & 63;
        if (!k.raN[r]) { k.raSite[r] = (LONG)f.ret; k.raParent[r] = (LONG)f.parent; }  // a fresh row: keys first, then the count
        else if (k.raSite[r] != (LONG)f.ret || k.raParent[r] != (LONG)f.parent) continue;
        InterlockedIncrement(&k.raN[r]);
        InterlockedExchangeAdd(&k.raUs[r], hns);
        if (alloc) InterlockedExchangeAdd(&k.raBytes[r], (LONG)f.arg);
        return;
    }
    InterlockedIncrement(&k.raFull);
}

// adds one finished frame: its self time to its phase, its whole time to the frame below (or the job)
static void RjpClose(RjpThread& t, const RjpFrame& f, LONGLONG now) {
    Ts3nCounters& k = ts3native_counters;
    const LONGLONG incl = now - f.t0;
    const LONGLONG selfTicks = incl - f.child + t.rem[f.phase];
    const LONG self = RjpUs(selfTicks);
    t.rem[f.phase] = selfTicks - (LONGLONG)self * g_qpf.QuadPart / 1000000;
    if (t.kind == RJP_KSYNC) SypFrame(f, self, incl);  // docs/164: per-plan sums; the v87/v92 rows and the cell memo stay the worker's
    else {
        if (f.phase == RJP_ALLOC) RaRecord(t, f, incl - f.child);
        InterlockedIncrement(&k.rjpN[t.kind][f.phase]);
        InterlockedExchangeAdd(&k.rjpUs[t.kind][f.phase], self);
        Max(&k.rjpMaxUs[f.phase], self);
        if (f.phase == RJP_CELL) CmCell(incl);
    }
    if (t.n) t.f[t.n - 1].child += incl; else t.child += incl;
}

// frames whose return slot is at or below `slot` were left without returning (unwound): dropped, their time stays in the parent
static void RjpDrop(RjpThread& t, DWORD* slot) {
    while (t.n && t.f[t.n - 1].slot <= slot) { t.n--; InterlockedIncrement(&ts3native_counters.rjpDropped); }
}

// slot = the return-address slot (stubs) or any address in the pushing function's frame (manual frames, swap = false)
static bool RjpPush(int phase, DWORD* slot, bool swap) {
    RjpThread& t = t_rjp;
    RjpDrop(t, slot);
    if (t.n == 48) { InterlockedIncrement(&ts3native_counters.rjpDropped); return false; }
    RjpFrame& f = t.f[t.n++];
    f.slot = slot;
    f.ret = swap ? *slot : 0;
    f.arg = swap ? slot[1] : 0;  // the first stack argument (EA_AllocTagged: size)
    f.parent = t.n > 1 ? t.f[t.n - 2].ret : 0;
    f.phase = phase;
    f.site = -1;
    f.child = 0;
    f.t0 = RjpNow();
    if (swap) *slot = (DWORD)(uintptr_t)RjpLeaveThunk;
    return true;
}

// pops the frame whose slot is `at` or just below it (and any left above it); returns its saved return address.
// Searched from the outermost frame like loadprof's LpLeave: 12 of the 15 sites return with `ret N` (callee-cleanup), so the
// leave thunk sees at = slot + N. The old top-down `slot < at` walk took our own frame for unwound and popped the parent's,
// returning into the parent's caller with the wrong esp: TS3W.exe vanished ~10 s into every phases job (docs/150 §7 fix,
// docs/67, ledger §NEW-T). An outer frame's slot is above at: the callee's N argument bytes lie below it.
static DWORD RjpPop(DWORD* at) {
    RjpThread& t = t_rjp;
    const LONGLONG now = RjpNow();
    int i = 0;
    while (i < t.n && t.f[i].slot > at) i++;  // frames above the one found were unwound without a leave
    if (i == t.n) return 0;
    while (t.n - 1 > i) { t.n--; InterlockedIncrement(&ts3native_counters.rjpDropped); }
    const RjpFrame f = t.f[i];
    t.n = i;
    RjpClose(t, f, now);
    return f.ret;
}

static void __cdecl RjpEnter(int site, DWORD* slot) {
    if (g_rjpAny[site] && !SypEnter(site, slot)) return;  // docs/164: a thread outside a job or session (not the sim's plan)
    if (!RjpPush(kRjpSites[site].phase, slot, true)) return;
    RjpFrame& f = t_rjp.f[t_rjp.n - 1];
    f.site = site;
    if (site == RJS_WORLD && t_rjp.kind == RJP_KSYNC) f.arg = SypLoopOf(f.ret, slot);  // which planner loop called the world A*
}
// eax = the leaving function's return value (docs/164: Route_PlanLocked's result in AL)
static DWORD __cdecl RjpLeave(DWORD* at, DWORD eax) {
    const DWORD r = RjpPop(at);
    if (t_rjp.kind == RJP_KSYNC) SypAfterLeave(eax);
    return r;
}

static __declspec(naked) void __cdecl RjpLeaveThunk() {
    __asm {
        push eax  // becomes the return address
        pushad
        pushfd
        push eax  // the function's return value (pushad/pushfd leave eax as it was)
        lea eax, [esp + 40]
        push eax
        call RjpLeave
        add esp, 8
        mov [esp + 36], eax
        popfd
        popad
        ret
    }
}

// thread check first (TEB +0x24 = thread id): only the worker inside a job, the sim inside a sync plan session (docs/164) and,
// at a g_rjpAny site, any thread reach pushad
#define RJP_STUB(i) static __declspec(naked) void RjpStub##i() { __asm push eax __asm mov eax, dword ptr fs:[0x24] \
    __asm cmp eax, g_rjpTid __asm je on##i __asm cmp eax, g_rjpSyncTid __asm je on##i \
    __asm cmp dword ptr [g_rjpAny + i * 4], 0 __asm je off##i \
    __asm on##i: __asm pop eax __asm pushad __asm pushfd __asm lea eax, [esp + 36] __asm push eax \
    __asm push i __asm call RjpEnter __asm add esp, 8 __asm popfd __asm popad __asm jmp pass##i \
    __asm off##i: __asm pop eax __asm pass##i: __asm jmp dword ptr [g_rjpJmp + i * 4] }
RJP_STUB(0) RJP_STUB(1) RJP_STUB(2) RJP_STUB(3) RJP_STUB(4) RJP_STUB(5) RJP_STUB(6) RJP_STUB(7) RJP_STUB(8) RJP_STUB(9) RJP_STUB(10)
RJP_STUB(11) RJP_STUB(12) RJP_STUB(13) RJP_STUB(14)
static void (*const kRjpStubs[RJS_N])() = {RjpStub0, RjpStub1, RjpStub2, RjpStub3, RjpStub4, RjpStub5, RjpStub6, RjpStub7,
                                           RjpStub8, RjpStub9, RjpStub10, RjpStub11, RjpStub12, RjpStub13, RjpStub14};

// ServeAsync around CallIc: kind 0 own START, 1 own A10, 2 foreign
static void RjpJobBegin(int kind) {
    if (!ts3native_counters.rjpReady) return;
    RjpThread& t = t_rjp;
    t.n = 0;
    t.kind = kind;
    t.child = 0;
    CmJob(true);
    t.allocs = 0;
    memset(t.rem, 0, sizeof(t.rem));
    InterlockedExchange((volatile LONG*)&g_rjpTid, (LONG)GetCurrentThreadId());
}
static void RjpJobEnd(LONG us) {
    Ts3nCounters& k = ts3native_counters;
    if (!g_rjpTid) return;
    InterlockedExchange((volatile LONG*)&g_rjpTid, 0);
    RjpThread& t = t_rjp;
    const LONGLONG now = RjpNow();
    while (t.n) { const RjpFrame f = t.f[--t.n]; RjpClose(t, f, now); }  // shortcut: a frame still open at the end (none expected)
    CmJob(false);
    LONG other = us - RjpUs(t.child);
    if (other < 0) other = 0;  // the job clock started a few QPC reads before ours
    InterlockedIncrement(&k.rjpJobs[t.kind]);
    static const LONG kJobEdge[7] = {1, 16, 64, 256, 1024, 4096, 16384};
    int b = 0;
    while (b < 7 && t.allocs >= kJobEdge[b]) b++;
    InterlockedIncrement(&k.raJobHist[b]);
    InterlockedExchangeAdd(&k.rjpJobUs[t.kind], us);
    InterlockedIncrement(&k.rjpN[t.kind][RJP_OTHER]);
    InterlockedExchangeAdd(&k.rjpUs[t.kind][RJP_OTHER], other);
    Max(&k.rjpMaxUs[RJP_OTHER], other);
}
// Proxy (host-call wait): a manual frame, no return slot swapped
static bool RjpHostBegin(DWORD* frame) { return g_rjpTid && GetCurrentThreadId() == g_rjpTid && RjpPush(RJP_HOST, frame, false); }
static void RjpHostEnd(DWORD* frame) { RjpPop(frame); }

// One site: our detour when the prologue matches; else the trampoline pointer of the section that owns it (owner != NULL and
// set); else skipped. Parameters let the selftest use local memory.
static bool RjpInstallAt(int i, DWORD at, void** owner) {
    const RjpSite& s = kRjpSites[i];
    if (Matches((const void*)(uintptr_t)at, s.pro, s.len)) {
        char name[80];
        sprintf_s(name, "%s (route phases)", s.name);
        return (g_rjpJmp[i] = InstallDetour(name, at, s.pro, s.len, (void*)kRjpStubs[i])) != NULL;
    }
    if (owner && *owner) {
        g_rjpJmp[i] = *owner;
        InterlockedExchangePointer(owner, (void*)kRjpStubs[i]);
        Log("OK   route phases %s: timed inside its owner's stub", s.name);
        return true;
    }
    Log("SKIP route phases %s: bytes at 0x%08lX differ and no known owner", s.name, at);
    return false;
}

static void** RjpOwner(int i) {
    switch (i) {
        case RJS_PLAN: return (void**)&g_planOrig;
        case RJS_LOCK: return (void**)&g_lockOrig;
        case RJS_WORLD: return (void**)&g_eoAsOrig;
        case RJS_LOTGRAPH: return g_rgTramp[3] ? &g_rgTramp[3] : &g_lpJmp[LP_LOTGRAPH];
        case RJS_PENDING: return &g_rtTramp[RT_PENDING];
        case RJS_CELL: return &g_pdOrig[0];  // docs/164 §7 [RouteAsync] PlanDeadlineMs entry stubs (plandeadline.cpp)
        case RJS_WATER: return &g_pdOrig[1];
        default: return NULL;
    }
}

static bool SypConfig(const wchar_t* ini);  // syncphases.cpp: [RouteJobPhases] SyncPlans
static void SypArm();

static bool ApplyRouteJobPhases(const wchar_t* ini) {
    const bool worker = GetPrivateProfileIntW(L"RouteJobPhases", L"Enabled", 0, ini) != 0, sync = SypConfig(ini);
    if (!worker && !sync) { Log("route job phases off (Enabled=0, SyncPlans=0)"); return false; }
    Ts3nCounters& k = ts3native_counters;
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    for (int i = 0; i < RJS_N; i++) {
        if (RjpInstallAt(i, kRjpSites[i].at, RjpOwner(i))) k.rjpHooks++;
        else k.rjpSkipped++;
    }
    k.rjpReady = worker && k.rjpHooks ? 1 : 0;  // SyncPlans alone: worker jobs stay untimed (cell memo and HostSplit stay off)
    Log("%s route job phases: %ld of %d sites timed (%ld skipped), %s", k.rjpSkipped ? "PART" : "OK  ", k.rjpHooks, RJS_N,
        k.rjpSkipped, worker ? (sync ? "worker jobs and sim sync plans" : "worker jobs only") : "sim sync plans only");
    if (sync) SypArm();
    return true;
}
