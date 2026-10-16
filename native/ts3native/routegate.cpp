// Routing gate measurement (docs/18 H6 step S2a). Included by ts3native.cpp (one translation unit). Measurement only:
// no locking, no behaviour change.
//
// Owner of the routing lock world+0x918 (learned in PlanLockedHook): Lock_Acquire 0x4E16F0 (shared LockAcquireHook) sets
// rgOwnerTid on every successful acquire of that lock (it returns the recursion count [lock+0x20], > 0); Lock_Release
// 0x4E17B0 clears it before the outermost release (count 1), so a thread that takes the lock right after is not clobbered.
// Gate sites: the 12 S0 sites (docs/18 "Gate-site list") get pass-through stubs. An entry counts in rgUnlocked[site] when
// it is outermost on its thread (no other gate frame open) and the thread does not own +0x918; while a route-worker job
// (g_callerTid set) or an async job runs it also counts in rgJob[site]. Outermost needs the leave: the stub swaps the
// return address for RgLeaveThunk and keeps the real one on a per-thread stack (Frida's onLeave does the same). A frame left
// without returning (exception unwind, longjmp) is dropped at the thread's next entry: its return slot is at or below the
// new entry's, so it cannot still be live. The game imports no fiber API, so one stack per thread holds.
// shortcut: entries are counted, not P/Q reads that skip the leaves, and not the 0x681E00 quadtree check (S2b).

static const int RG_SITES = 12;
struct RgSite { DWORD addr; u8 len; u8 pro[8]; const char* name; };
static const RgSite kRgSites[RG_SITES] = {  // prologues verified on the Steam 1.67 exe (docs/18 S0 table)
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
};
static const DWORD LOCK_RELEASE = 0x004E17B0;
static const u8 kLockReleasePrologue[] = {0x83, 0x41, 0x20, 0xFF, 0x80, 0x79, 0x24, 0x00};  // add [ecx+20h],-1; cmp byte [ecx+24h],0

typedef LONG(__fastcall* LockReleaseFn)(void* lock, void* edx);  // 0x4E17B0 thiscall, no args, returns the new count
static LockReleaseFn g_rgRelOrig;
static void* g_rgTramp[RG_SITES];

struct RgFrame { DWORD* slot; DWORD ret; };
struct RgThread { int n; RgFrame f[16]; };
static __declspec(thread) RgThread t_rg;

static CRITICAL_SECTION g_rgCs;  // guards the caller list only
static volatile LONG g_rgNoted;
static DWORD g_rgCallers[32];    // distinct (site << 24 | return address & 0xFFFFFF) of counted entries, logged once each

static LONG __fastcall LockReleaseHook(void* lock, void* edx) {
    if (lock == g_rgLock && *(const LONG*)((const u8*)lock + 0x20) == 1)
        InterlockedCompareExchange(&ts3native_counters.rgOwnerTid, 0, (LONG)GetCurrentThreadId());
    return g_rgRelOrig(lock, edx);
}

static void RgNote(int site, DWORD ret, LONG tid) {
    if (g_rgNoted >= 32) return;
    const DWORD key = (DWORD)site << 24 | (ret & 0xFFFFFF);
    EnterCriticalSection(&g_rgCs);
    bool seen = false;
    for (LONG i = 0; i < g_rgNoted && !seen; i++) seen = g_rgCallers[i] == key;
    if (!seen && g_rgNoted < 32) {
        g_rgCallers[g_rgNoted++] = key;
        Log("route gate: unlocked entry %s 0x%08X from 0x%08X thread %ld (main %lu, worker %lu)%s", kRgSites[site].name,
            kRgSites[site].addr, ret, tid, g_mainTid, g_workerTid, g_callerTid || g_async.state == A_RUNNING ? " during a job" : "");
    }
    LeaveCriticalSection(&g_rgCs);
}

static void __cdecl RgLeaveThunk();

// Stub side: slot = the caller's return-address slot ([esp] at the site's entry).
static void __cdecl RgEnter(int site, DWORD* slot) {
    Ts3nCounters& k = ts3native_counters;
    if (k.rgReady != 7 || !g_rgLock) return;  // before the lock is known nobody's hold can be told apart
    InterlockedIncrement(&k.rgEntries);
    RgThread& t = t_rg;
    while (t.n && t.f[t.n - 1].slot <= slot) t.n--;  // frames left without returning (see the file comment)
    const LONG me = (LONG)GetCurrentThreadId();
    if (t.n == 0 && k.rgOwnerTid != me) {
        InterlockedIncrement(&k.rgUnlocked[site]);
        if (g_callerTid || g_async.state == A_RUNNING) InterlockedIncrement(&k.rgJob[site]);
        RgNote(site, *slot, me);
    }
    if (t.n == 16) { InterlockedIncrement(&k.rgUntracked); return; }
    t.f[t.n].slot = slot;
    t.f[t.n].ret = *slot;
    t.n++;
    *slot = (DWORD)(uintptr_t)RgLeaveThunk;
}

static DWORD __cdecl RgLeave() { return t_rg.f[--t_rg.n].ret; }

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
static void (*const kRgStubs[RG_SITES])() = {RgStub0, RgStub1, RgStub2, RgStub3, RgStub4, RgStub5, RgStub6, RgStub7, RgStub8,
                                             RgStub9, RgStub10, RgStub11};

// [RouteGate] Enabled=1: true when every byte the gate will patch matches (12 sites, Lock_Acquire, Lock_Release); the
// Route_PlanLocked prologue is checked by its own InstallDetour. One mismatch: nothing of the gate is installed.
static bool ReadRouteGate(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"RouteGate", L"Enabled", 0, ini)) { Log("route gate disabled (Enabled=0)"); return false; }
    char hex[64];
    for (int i = 0; i < RG_SITES; i++) {
        const u8* p = (const u8*)(uintptr_t)kRgSites[i].addr;
        if (Matches(p, kRgSites[i].pro, kRgSites[i].len)) continue;
        if (Readable(p, 8)) Hex(p, 8, hex, sizeof(hex)); else strcpy_s(hex, "<unreadable>");
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
