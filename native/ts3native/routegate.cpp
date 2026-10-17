// Routing gate measurement (docs/18 H6 steps S2a and S2b, docs/60 §2.3). Included by ts3native.cpp (one translation unit).
// Measurement only unless [RouteGate] Enforce=1 (H6 S3b, rgEnforce): then, while an async job runs (the worker holds +0x918
// for the whole job), an outermost gate entry by another thread takes +0x918 for the call (the owner's acquire pumps the
// job's managed calls, LockAcquireAsync), and the owner inside a callback it serves for the job borrows the job's hold
// (the worker is blocked in that callback): its gate entries and +0x918 acquires/releases skip the real lock (rgBorrows).
//
// Owner of the routing lock world+0x918 (learned in PlanLockedHook): Lock_Acquire 0x4E16F0 (shared LockAcquireHook) sets
// rgOwnerTid on every successful acquire of that lock (it returns the recursion count [lock+0x20], > 0); Lock_Release
// 0x4E17B0 clears it before the outermost release (count 1), so a thread that takes the lock right after is not clobbered.
// Gate sites: the 12 S0 sites (docs/18 "Gate-site list") plus sites 13-15 of docs/60 §2.2 (#3 LotTable_Contains, #7 the
// two coarse gates) get pass-through stubs. An entry counts in rgUnlocked[site] when it is outermost on its thread (no
// other gate frame open) and the thread does not own +0x918; while a route-worker job (g_callerTid set) or an async job
// runs it also counts in rgJob[site]. Outermost needs the leave: the stub swaps the return address for RgLeaveThunk and
// keeps the real one on a per-thread stack (Frida's onLeave does the same). A frame left without returning (exception
// unwind, longjmp) is dropped at the thread's next entry: its return slot is at or below the new entry's, so it cannot
// still be live. The game imports no fiber API, so one stack per thread holds. rtsites.cpp (S2b) pushes reader frames
// (the worker's quadtree readers) on the same stack; they do not count as gate frames.
// G11 (docs/18 (3)): a sim-thread plan's free window runs from its return to the sim thread's next routing touch (an
// outermost +0x918 acquire, an unlocked gate entry, an object destroy, a quadtree write, the next plan); g11MovableUs sums
// min(plan length, window). Plan = the Route_PlanLocked detour on the sim thread (shortcut: the rest of IC_Route_Plan
// around it is not timed, so the plan length is a lower bound of what an async job would move).
// Verify probes on an address a stub here owns are counted from inside the stub (docs/68 §6 rule 3): VfEnter runs first,
// so this module reads the real caller through VfRealRet.
// shortcut: entries are counted, not P/Q reads that skip the leaves.

static const int RG_SITES = 15;
struct RgSite { DWORD addr; u8 len; u8 pro[10]; const char* name; };
static const RgSite kRgSites[RG_SITES] = {  // prologues verified on the Steam 1.67 exe (docs/18 S0 table, docs/60 §2.2)
    {0x0062EAB0, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}, "lot table find-or-insert"},
    {0x0063F490, 7, {0x83, 0xEC, 0x54, 0x8B, 0x44, 0x24, 0x5C}, "goal-step lot planner"},
    {0x0063D2F0, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}, "lazy room connectivity"},
    {0x0063CB90, 8, {0x83, 0xEC, 0x5C, 0x53, 0x8B, 0x5C, 0x24, 0x64}, "lazy lot build"},
    {0x0063D7B0, 5, {0x8B, 0x44, 0x24, 0x08, 0x55}, "lot rebuild"},
    {0x0063D860, 8, {0x8B, 0x44, 0x24, 0x08, 0x8B, 0x54, 0x24, 0x04}, "room graph refresh"},
    {0x0061E320, 5, {0x53, 0x8B, 0x5C, 0x24, 0x08}, "Q reset"},
    {0x0061E420, 7, {0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x18}, "Q reset (clear path)"},
    {0x0061E360, 8, {0x66, 0x83, 0x05, 0xC4, 0x80, 0x15, 0x01, 0x01}, "search generation stamp"},  // absolute operand
    {0x0061C020, 5, {0xB8, 0x70, 0x9C, 0x00, 0x00}, "Q node table clear"},  // the stack-probe call at +5 stays in place
    {0x0075C2D0, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}, "goal step (coarse)"},
    {0x007929F0, 6, {0x83, 0xEC, 0x14, 0x53, 0x33, 0xDB}, "IC_World_GetConnectedRoomsImpl (coarse)"},
    // docs/60 §2.2 #3 and #7 (S2b): stolen bytes end before the jz rel32 at +10 of the two coarse gates
    {0x00628A90, 8, {0x83, 0xEC, 0x08, 0x53, 0x8B, 0x5C, 0x24, 0x14}, "LotTable_Contains"},
    {0x00835240, 10, {0x56, 0x8B, 0xF1, 0x80, 0xBE, 0x7B, 0x09, 0x00, 0x00, 0x00}, "RoutingSystem_OnExitBuildBuy (coarse)"},
    {0x00835110, 10, {0x56, 0x8B, 0xF1, 0x80, 0xBE, 0x74, 0x09, 0x00, 0x00, 0x00}, "RoutingSystem_RebuildLotRouting (coarse)"},
};
static const DWORD LOCK_RELEASE = 0x004E17B0;
static const u8 kLockReleasePrologue[] = {0x83, 0x41, 0x20, 0xFF, 0x80, 0x79, 0x24, 0x00};  // add [ecx+20h],-1; cmp byte [ecx+24h],0
static const DWORD* g_rtSimTidSlot = (const DWORD*)(uintptr_t)0x011D8A8C;  // game global: the sim thread (selftest: its own)

typedef LONG(__fastcall* LockReleaseFn)(void* lock, void* edx);  // 0x4E17B0 thiscall, no args, returns the new count
static LockReleaseFn g_rgRelOrig;
static void* g_rgTramp[RG_SITES];
static volatile LONG g_rgVf[RG_SITES];  // Verify probe index + 1 counted from inside the stub, 0 = none

static void __cdecl VfEnter(int id, DWORD* slot);  // verify.cpp
static DWORD VfRealRet(const DWORD* slot);          // verify.cpp: the caller behind a Verify leave thunk in *slot
static volatile LONG g_p1bOn;                                     // lotfd.cpp: probe P1b
static void P1bNote(int site, DWORD key, const DWORD* slot, LONG us);  // lotfd.cpp

// rel: what the leave undoes (S3b): RG_REL_LOCK releases `lock`, RG_REL_QT leaves g_qtCs, RG_REL_BUSY drops rtBusy,
// RG_REL_OFFSIM drops g_rgOffSimOpen
// RG_REL_STEER puts back the previous t_steerRet (kept in `lock`); RG_REL_QL releases the worker's QL hold (steerro.cpp)
enum { RG_REL_LOCK = 1, RG_REL_QT = 2, RG_REL_BUSY = 4, RG_REL_OFFSIM = 8, RG_REL_STEER = 16, RG_REL_QL = 32 };
struct RgFrame { DWORD* slot; DWORD ret; LONG reader; LONG rel; void* lock; };
struct RgThread { int n; LONG readers; RgFrame f[16]; };
static __declspec(thread) RgThread t_rg;
static CRITICAL_SECTION g_qtCs;            // H6 S3b #1: physics quadtree, worker readers vs mutators during a job (rtsites.cpp)
static volatile LONG g_qlOn;               // [SteerRO] (steerro.cpp): the LotGraph_Build gate stub (site 3) is W4
static void QlReleaseEx();                                        // steerro.cpp
static bool QlPushWorker(RgThread& t, DWORD* slot, LONG reader);  // steerro.cpp

static void RgUndo(const RgFrame& f) {
    if (f.rel & RG_REL_QL) QlReleaseEx();
    if (f.rel & RG_REL_LOCK) LockReleaseHook(f.lock, NULL);
    if (f.rel & RG_REL_QT) LeaveCriticalSection(&g_qtCs);
    if (f.rel & RG_REL_BUSY) InterlockedDecrement(&ts3native_counters.rtBusy);
    if (f.rel & RG_REL_OFFSIM) InterlockedDecrement(&g_rgOffSimOpen);
    if (f.rel & RG_REL_STEER) t_steerRet = (DWORD)(uintptr_t)f.lock;
}

static inline volatile LONG* RgUnl(int s) { return s < 12 ? &ts3native_counters.rgUnlocked[s] : &ts3native_counters.rgUnlocked2[s - 12]; }
static inline volatile LONG* RgJob(int s) { return s < 12 ? &ts3native_counters.rgJob[s] : &ts3native_counters.rgJob2[s - 12]; }
static inline bool RgJobLive() { return g_callerTid || g_async.state == A_RUNNING; }
static inline bool RgIsSim(DWORD tid) { return *g_rtSimTidSlot && tid == *g_rtSimTidSlot; }
static inline bool RgIsWorker(DWORD tid) { return g_workerTid && tid == g_workerTid; }

// frames left without returning (see the file comment)
static inline void RgDrop(RgThread& t, const DWORD* slot) {
    while (t.n && t.f[t.n - 1].slot <= slot) {
        const RgFrame& f = t.f[--t.n];
        t.readers -= f.reader;
        if (f.rel & RG_REL_LOCK) InterlockedIncrement(&ts3native_counters.rgDropLocked);
        if (f.rel) RgUndo(f);  // late, but a hold left behind would deadlock the next job
    }
}

static void __cdecl RgLeaveThunk();

// false: no frame left; the caller must undo `rel` itself (the call then runs without its closure's leave)
static bool RgPush(RgThread& t, DWORD* slot, LONG reader, LONG rel = 0, void* lock = NULL) {
    if (t.n == 16) { InterlockedIncrement(&ts3native_counters.rgUntracked); return false; }
    t.f[t.n].slot = slot;
    t.f[t.n].ret = *slot;
    t.f[t.n].reader = reader;
    t.f[t.n].rel = rel;
    t.f[t.n].lock = lock;
    t.n++;
    t.readers += reader;
    *slot = (DWORD)(uintptr_t)RgLeaveThunk;
    return true;
}

static CRITICAL_SECTION g_rgCs;  // guards the caller list only
static volatile LONG g_rgNoted;
static DWORD g_rgCallers[48];    // distinct (site << 24 | return address & 0xFFFFFF) of counted entries, logged once each

static LONG __fastcall LockReleaseHook(void* lock, void* edx) {
    if (t_borrow && lock == g_async.lock) { t_borrow--; return 1; }  // a borrowed acquire (LockAcquireHook)
    if (lock == g_rgLock && *(const LONG*)((const u8*)lock + 0x20) == 1)
        InterlockedCompareExchange(&ts3native_counters.rgOwnerTid, 0, (LONG)GetCurrentThreadId());
    return g_rgRelOrig(lock, edx);
}

// first 48 distinct site/caller pairs (gate sites 0-14, rtsites.cpp sites 32+) with a line in ts3native.log
static void RgNoteAs(int site, const char* name, DWORD at, const char* what, DWORD ret, LONG tid) {
    if (g_rgNoted >= 48) return;
    const DWORD key = (DWORD)site << 24 | (ret & 0xFFFFFF);
    EnterCriticalSection(&g_rgCs);
    bool seen = false;
    for (LONG i = 0; i < g_rgNoted && !seen; i++) seen = g_rgCallers[i] == key;
    if (!seen && g_rgNoted < 48) {
        g_rgCallers[g_rgNoted++] = key;
        Log("route gate: %s %s 0x%08X from 0x%08X thread %ld (main %lu, sim %lu, worker %lu)%s", what, name, at, ret, tid, g_mainTid,
            *g_rtSimTidSlot, g_workerTid, RgJobLive() ? " during a job" : "");
    }
    LeaveCriticalSection(&g_rgCs);
}

// --- G11: per-thread plan state; only the sim thread opens windows ---
struct G11State { int depth; LARGE_INTEGER t0, end; LONG planUs; };
static __declspec(thread) G11State t_g11;

static bool G11PlanBegin() {
    if (ts3native_counters.rgReady != 7 || !RgIsSim(GetCurrentThreadId())) return false;
    G11State& g = t_g11;
    if (g.depth == 0) {
        G11Touch(G11_PLAN, 0x04000000);  // a plan right after a plan closes the window too
        QueryPerformanceCounter(&g.t0);
    }
    g.depth++;
    return true;
}

static void G11PlanEnd(bool begun) {
    if (!begun) return;
    G11State& g = t_g11;
    if (--g.depth) return;
    QueryPerformanceCounter(&g.end);
    g.planUs = (LONG)((g.end.QuadPart - g.t0.QuadPart) * 1000000 / g_qpf.QuadPart);
}

static void G11Touch(int kind, DWORD key) {
    G11State& g = t_g11;
    if (g.depth || !g.end.QuadPart || !RgIsSim(GetCurrentThreadId())) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG gap = (now.QuadPart - g.end.QuadPart) * 1000000 / g_qpf.QuadPart;
    if (gap > 1000000) gap = 1000000;
    g.end.QuadPart = 0;
    Ts3nCounters& k = ts3native_counters;
    const LONG movable = gap < g.planUs ? (LONG)gap : g.planUs;
    InterlockedIncrement(&k.g11Plans);
    InterlockedExchangeAdd(&k.g11PlanUs, g.planUs);
    InterlockedExchangeAdd(&k.g11MovableUs, movable);
    InterlockedExchangeAdd(&k.g11GapUs, (LONG)gap);
    InterlockedIncrement(&k.g11End[kind]);
    // vfG11CloseBy: only the sim thread gets here, so the table needs no interlocked insert
    int i = 0;
    while (i < 32 && k.g11CloseKey[i] && k.g11CloseKey[i] != (LONG)key) i++;
    if (i == 32) { InterlockedIncrement(&k.g11CloseOther); return; }
    k.g11CloseKey[i] = (LONG)key;
    InterlockedIncrement(&k.g11CloseN[i]);
    InterlockedExchangeAdd(&k.g11CloseLostUs[i], g.planUs - movable);
}

// Stub side: slot = the caller's return-address slot ([esp] at the site's entry).
static void __cdecl RgEnter(int site, DWORD* slot) {
    if (g_rgVf[site]) VfEnter(g_rgVf[site] - 1, slot);
    Ts3nCounters& k = ts3native_counters;
    if (k.rgReady != 7 || !g_rgLock) return;  // before the lock is known nobody's hold can be told apart
    InterlockedIncrement(&k.rgEntries);
    RgThread& t = t_rg;
    RgDrop(t, slot);
    const DWORD tid = GetCurrentThreadId();
    const LONG me = (LONG)tid;
    if (t.n == t.readers && k.rgOwnerTid != me) {  // no gate frame open on this thread
        InterlockedIncrement(RgUnl(site));
        if (RgJobLive()) InterlockedIncrement(RgJob(site));
        const bool sim = RgIsSim(tid), worker = RgIsWorker(tid);
        if (sim) G11Touch(site < 12 ? G11_GATE : G11_GATE13, 0x01000000 | site);
        else if (!worker) InterlockedIncrement(&k.vfRtGateOffSim);
        RgNoteAs(site, kRgSites[site].name, kRgSites[site].addr, "unlocked entry", VfRealRet(slot), me);
        if (k.rgEnforce && !worker) {
            // every non-worker frame is published before the job state is read; the worker sets RUNNING before it reads
            // them (ServeAsync), so a leaf entry that saw no job and a job start never overlap
            InterlockedIncrement(&g_rgOffSimOpen);
            LONG rel = RG_REL_OFFSIM;
            void* lock = g_async.state == A_RUNNING ? g_async.lock : NULL;
            if (lock && tid == g_async.owner && g_inInvoke) {
                InterlockedIncrement(&k.rgBorrows);  // the worker waits in this callback: nested in the job, as without us
            } else if (lock) {
                static const DWORD kInfinite = 0xFFFFFFFF;
                LARGE_INTEGER t0;
                QueryPerformanceCounter(&t0);
                const DWORD key = VfRealRet(slot);
                t_waitKey = key;  // P1 (docs/84 §4): gate waits keyed by the leaf's caller, not the site
                LockAcquireHook(lock, NULL, &kInfinite);  // the owner pumps the job's managed calls while it waits
                t_waitKey = 0;
                const LONG us = UsSince(t0);
                if (g_p1bOn) P1bNote(site, key, slot, us);  // P1b: and by the caller's caller
                InterlockedIncrement(&k.rgGateWaits);
                InterlockedExchangeAdd(&k.rgGateWaitUs, us);
                Max(&k.rgGateWaitMaxUs, us);
                rel |= RG_REL_LOCK;
            }
            if (!RgPush(t, slot, 0, rel, lock)) RgUndo(RgFrame{NULL, 0, 0, rel, lock});
            return;
        }
    }
    // W4 (docs/83 §6): LotGraph_Build on the worker holds QL exclusive for the call (level vector, transform, nested W1/W3/W6)
    if (site == 3 && g_qlOn && RgIsWorker(GetCurrentThreadId())) { QlPushWorker(t, slot, 0); return; }
    RgPush(t, slot, 0);
}

static DWORD __cdecl RgLeave() {
    RgThread& t = t_rg;
    const RgFrame f = t.f[--t.n];
    t.readers -= f.reader;
    if (f.rel) RgUndo(f);
    return f.ret;
}

// The site returned here (ret or ret N): back to its real caller with eax, edx, flags and the x87 stack as the site left them.
static __declspec(naked) void __cdecl RgLeaveThunk() {
    __asm {
        push eax  // becomes the return address
        pushad
        pushfd
        call RgLeave
        mov [esp + 36], eax
        popfd
        popad
        ret
    }
}

// Site entry: count, then the site's stolen prologue (trampoline) with the same registers, flags and stack.
#define RG_STUB(i) static __declspec(naked) void RgStub##i() { __asm pushad __asm pushfd __asm lea eax, [esp + 36] \
    __asm push eax __asm push i __asm call RgEnter __asm add esp, 8 __asm popfd __asm popad __asm jmp dword ptr [g_rgTramp + i * 4] }
RG_STUB(0) RG_STUB(1) RG_STUB(2) RG_STUB(3) RG_STUB(4) RG_STUB(5) RG_STUB(6) RG_STUB(7) RG_STUB(8) RG_STUB(9) RG_STUB(10) RG_STUB(11)
RG_STUB(12) RG_STUB(13) RG_STUB(14)
static void (*const kRgStubs[RG_SITES])() = {RgStub0, RgStub1, RgStub2, RgStub3, RgStub4, RgStub5, RgStub6, RgStub7, RgStub8,
                                             RgStub9, RgStub10, RgStub11, RgStub12, RgStub13, RgStub14};

// [RouteGate] Enabled=1: true when every byte the gate will patch matches (15 sites, Lock_Acquire, Lock_Release); the
// Route_PlanLocked prologue is checked by its own InstallDetour. One mismatch: nothing of the gate is installed.
static bool ReadRouteGate(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"RouteGate", L"Enabled", 0, ini)) { Log("route gate disabled (Enabled=0)"); return false; }
    char hex[64];
    for (int i = 0; i < RG_SITES; i++) {
        const u8* p = (const u8*)(uintptr_t)kRgSites[i].addr;
        if (Matches(p, kRgSites[i].pro, kRgSites[i].len)) continue;
        if (Readable(p, 10)) Hex(p, 10, hex, sizeof(hex)); else strcpy_s(hex, "<unreadable>");
        Log("SKIP route gate: %s 0x%08X bytes %s differ, nothing hooked", kRgSites[i].name, kRgSites[i].addr, hex);
        return false;
    }
    // async (installed in between) may own Lock_Acquire by then; InstallRouteGate reuses that detour
    if (!Matches((const void*)(uintptr_t)LOCK_ACQUIRE, kLockAcquirePrologue, sizeof(kLockAcquirePrologue)) ||
        !Matches((const void*)(uintptr_t)LOCK_RELEASE, kLockReleasePrologue, sizeof(kLockReleasePrologue))) {
        Log("SKIP route gate: Lock_Acquire/Lock_Release bytes differ, nothing hooked");
        return false;
    }
    return true;
}

static bool InstallRouteGate(bool planHooked) {
    if (!planHooked) { Log("FAIL route gate: Route_PlanLocked detour missing (the lock is learned there), nothing hooked"); return false; }
    InitializeCriticalSection(&g_rgCs);
    Ts3nCounters& k = ts3native_counters;
    if (!g_lockOrig)
        g_lockOrig = (LockAcquireFn)InstallDetour("Lock_Acquire (route gate)", LOCK_ACQUIRE, kLockAcquirePrologue, sizeof(kLockAcquirePrologue), (void*)LockAcquireHook);
    if (!g_lockOrig) return false;
    g_rgRelOrig = (LockReleaseFn)InstallDetour("Lock_Release (route gate)", LOCK_RELEASE, kLockReleasePrologue, sizeof(kLockReleasePrologue), (void*)LockReleaseHook);
    if (!g_rgRelOrig) { k.rgReady = 1; return false; }
    int n = 0;
    for (int i = 0; i < RG_SITES; i++)
        n += (g_rgTramp[i] = InstallDetour(kRgSites[i].name, kRgSites[i].addr, kRgSites[i].pro, kRgSites[i].len, (void*)kRgStubs[i])) != NULL;
    k.rgReady = 3 | (n == RG_SITES ? 4 : 0);  // counting needs all three (7); a partial set stays pass-through
    Log("%s route gate: %d of %d gate sites, owner tracking on Lock_Acquire/Lock_Release (measurement only)",
        n == RG_SITES ? "OK  " : "FAIL", n, RG_SITES);
    return n == RG_SITES;
}
