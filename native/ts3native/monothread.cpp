// Second managed thread, step 1 (docs/30 §1 P1, §2 P2). Included by ts3native.cpp (one translation unit).
//
// Boehm's thread table 0x1221000 (256 entries of 0x14: +0 in use, +4 thread id, +8 handle, +0xC stack base,
// +0x10 suspended; highest index [0x11F4124]) gets entries only from GC_new_thread 0xE51070, which registers the
// calling thread. GC_stop_world 0xE511A0 sets [0x11F4120] = 1 and suspends every entry with a stack base and another
// thread id; but the root scan only pushes the collector's own registers and stack: GC_push_roots 0xE4D910 ends with
// `jmp [0x11949CC]`, which holds 0xE4EF90 (`jmp 0xE51350`, `jmp 0xD71820`: EA's push of the current stack) [verified].
//
// P2: [0x11949CC] -> MtPushOtherRoots: the old hook first, then, only while the world is stopped, every other
// suspended entry's registers (GC_push_one 0xE4D520) and stack [ESP, base) (GC_push_all_stack 0xE4CEE0, hi exclusive).
// With only the collector registered, the loop finds nothing and the scan is the game's.
// P1: at the first MonoScriptHost_Simulate tick (vtable slot 0x108B6B0) the sim thread logs the table and registers
// itself with 0xE51070 if it is missing. No worker exists yet (Mode 0 only).

static const DWORD MT_PUSH_HOOK = 0x011949CC, MT_PUSH_THUNK = 0x00E4EF90, MT_PUSH_THUNK2 = 0x00E51350, MT_PUSH_EA = 0x00D71820;
static const DWORD MT_NEW_THREAD = 0x00E51070, MT_PUSH_ONE = 0x00E4D520, MT_PUSH_STACK = 0x00E4CEE0;
static const DWORD MT_SIM_SLOT = 0x0108B6B0, MT_SIMULATE = 0x00D81840;
static const u8 kMtNewThread[16] = {0x56, 0x57, 0x8B, 0x3D, 0x34, 0x52, 0xF9, 0x00, 0x6A, 0x01, 0x68, 0x00, 0x10, 0x22, 0x01, 0x33};
static const u8 kMtPushOne[10] = {0x8B, 0x44, 0x24, 0x04, 0x3B, 0x05, 0x24, 0x48, 0x19, 0x01};  // mov eax,[esp+4]; cmp eax,[0x1194824]
static const u8 kMtPushStack[7] = {0x83, 0x3D, 0xB4, 0x49, 0x19, 0x01, 0x00};                // cmp [0x11949B4],0
static const u8 kMtPushEa[6] = {0x83, 0xEC, 0x20, 0x56, 0x6A, 0x1C};                         // sub esp,0x20; push esi; push 0x1C
static const u8 kMtThunk[5] = {0xE9, 0xBB, 0x23, 0x00, 0x00};   // 0xE4EF90: jmp 0xE51350
static const u8 kMtThunk2[5] = {0xE9, 0xCB, 0x04, 0xF2, 0xFF};  // 0xE51350: jmp 0xD71820
static const u8 kMtPushTail[11] = {0xA1, 0xCC, 0x49, 0x19, 0x01, 0x85, 0xC0, 0x74, 0x02, 0xFF, 0xE0};  // 0xE4D9DC

typedef void(__cdecl* MtVoidFn)();
typedef void(__cdecl* MtPushOneFn)(DWORD p);
typedef void(__cdecl* MtPushStackFn)(DWORD lo, DWORD hi);
typedef void*(__cdecl* MtNewThreadFn)();
// game addresses; the selftest points them at fakes
static MtVoidFn g_mtOrigPush;
static MtPushOneFn g_mtPushOne = (MtPushOneFn)(uintptr_t)MT_PUSH_ONE;
static MtPushStackFn g_mtPushStack = (MtPushStackFn)(uintptr_t)MT_PUSH_STACK;
static MtNewThreadFn g_mtNewThread = (MtNewThreadFn)(uintptr_t)MT_NEW_THREAD;
static DWORD* g_mtTable = (DWORD*)(uintptr_t)0x01221000;               // 5 DWORDs per entry
static volatile LONG* g_mtMaxIdx = (volatile LONG*)(uintptr_t)0x011F4124;
static volatile LONG* g_mtStopped = (volatile LONG*)(uintptr_t)0x011F4120;
static __declspec(align(16)) CONTEXT g_mtCtx;  // only used with the world stopped, by the one collecting thread
static void* g_mtSimOrig;
static volatile LONG g_mtTicked;
static DWORD g_mtSimTid;

// GC root hook. Runs inside the mark with other threads suspended (one may hold the CRT heap or a game lock):
// no allocation, no Log, no lock, no CRT call.
static void __cdecl MtPushOtherRoots() {
    g_mtOrigPush();
    if (*g_mtStopped != 1) return;  // incremental step with the world running: the stopped mark rescans (docs/30 §12.6)
    Ts3nCounters& k = ts3native_counters;
    k.mtPushCalls++;
    const DWORD me = GetCurrentThreadId();
    LONG n = *g_mtMaxIdx;
    if (n > 255) n = 255;
    for (LONG i = 0; i <= n; i++) {
        const DWORD* e = g_mtTable + i * 5;
        const DWORD base = e[3];
        if (!base || e[4] != 1 || e[1] == me) continue;  // same test as GC_stop_world/GC_start_world, plus "suspended"
        DWORD lo = 0;
        g_mtCtx.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL;  // also completes the asynchronous SuspendThread
        // an ESP outside (base - 64 MB, base) is not on this stack (no game thread reserves more): treat as a failure
        if (GetThreadContext((HANDLE)(uintptr_t)e[2], &g_mtCtx) && g_mtCtx.Esp < base && base - g_mtCtx.Esp < (64u << 20)) {
            g_mtPushOne(g_mtCtx.Eax); g_mtPushOne(g_mtCtx.Ebx); g_mtPushOne(g_mtCtx.Ecx); g_mtPushOne(g_mtCtx.Edx);
            g_mtPushOne(g_mtCtx.Esi); g_mtPushOne(g_mtCtx.Edi); g_mtPushOne(g_mtCtx.Ebp);
            lo = g_mtCtx.Esp;
        } else {
            // an unscanned stack frees live objects: push the whole committed stack instead (conservative, slower)
            k.mtCtxFails++;
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery((const void*)(uintptr_t)(base - 4), &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) lo = (DWORD)(uintptr_t)mbi.BaseAddress;
        }
        if (lo && lo < base) { g_mtPushStack(lo, base); k.mtStacksPushed++; }
    }
}

static int MtFind(DWORD tid) {
    LONG n = *g_mtMaxIdx;
    if (n > 255) n = 255;
    for (LONG i = 0; i <= n; i++)
        if (g_mtTable[i * 5 + 3] && g_mtTable[i * 5 + 1] == tid) return (int)i;
    return -1;
}

static void MtLogTable(const char* when) {
    LONG n = *g_mtMaxIdx, used = 0;
    if (n > 255) n = 255;
    for (LONG i = 0; i <= n; i++) {
        const DWORD* e = g_mtTable + i * 5;
        if (!e[0] && !e[3]) continue;
        used++;
        Log("mono thread: GC table %s [%ld] in use %lu tid %lu%s%s base %08X suspended %lu", when, i, e[0], e[1],
            e[1] == g_mtSimTid ? " (sim)" : "", e[1] == g_mainTid ? " (main)" : "", e[3], e[4]);
    }
    ts3native_counters.mtThreads = used;
}

// P1: register the calling thread in Boehm's table (GC_new_thread 0xE51070 registers its caller).
// shortcut: no GC lock yet (P3, step 2), so only the sim thread may register, and only at a Simulate tick boundary (it
// is then outside every GC function and no other thread collects). Worker threads need P3 before they call this.
static bool MtRegisterCurrentThread() {
    const DWORD tid = GetCurrentThreadId();
    if (tid != g_mtSimTid) return false;
    if (MtFind(tid) >= 0) return true;
    g_mtNewThread();
    return MtFind(tid) >= 0;
}

// Top of every MonoScriptHost_Simulate call, on the sim thread. Only the first one does work.
static void __cdecl MtTick() {
    if (g_mtTicked) return;
    g_mtTicked = 1;
    Ts3nCounters& k = ts3native_counters;
    g_mtSimTid = GetCurrentThreadId();
    k.mtSimTid = (LONG)g_mtSimTid;
    MtLogTable("at first tick");
    if (MtFind(g_mtSimTid) < 0) {
        const bool ok = MtRegisterCurrentThread();
        k.mtSimRegistered = ok ? 1 : -1;
        Log("mono thread: sim thread %lu was not registered (main %lu); GC_new_thread %s", g_mtSimTid, g_mainTid, ok ? "added it" : "FAILED");
        MtLogTable("after register");
    } else {
        Log("mono thread: sim thread %lu already registered (main %lu)", g_mtSimTid, g_mainTid);
    }
    k.mtReady |= 4;
}

static __declspec(naked) void MtSimulateStub() {
    __asm {
        pushad
        pushfd
        call MtTick
        popfd
        popad
        jmp dword ptr [g_mtSimOrig]
    }
}

// [MonoThread] Enabled=1: P2 at load (data write before any GC runs), P1 at the first Simulate tick. Mode 0 only.
static bool ApplyMonoThread(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"MonoThread", L"Enabled", 0, ini)) { Log("mono thread disabled (Enabled=0)"); return false; }
    Ts3nCounters& k = ts3native_counters;
    k.mtMode = GetPrivateProfileIntW(L"MonoThread", L"Mode", 0, ini);
    if (k.mtMode != 0) { Log("mono thread: Mode=%ld not built yet (step 1), running Mode 0", k.mtMode); k.mtMode = 0; }
    DWORD* hook = (DWORD*)(uintptr_t)MT_PUSH_HOOK;
    DWORD* slot = (DWORD*)(uintptr_t)MT_SIM_SLOT;
    if (!Readable(hook, 4) || !Readable(slot, 4) || !Readable(g_mtTable, 256 * 0x14)) { Log("SKIP mono thread: GC globals unreadable"); return false; }
    const bool viaThunk = *hook == MT_PUSH_THUNK && Matches((void*)(uintptr_t)MT_PUSH_THUNK, kMtThunk, 5) &&
                          Matches((void*)(uintptr_t)MT_PUSH_THUNK2, kMtThunk2, 5);
    if (!(viaThunk || *hook == MT_PUSH_EA) || !Matches((void*)(uintptr_t)MT_PUSH_EA, kMtPushEa, sizeof(kMtPushEa)) ||
        !Matches((void*)(uintptr_t)0x00E4D9DC, kMtPushTail, sizeof(kMtPushTail)) ||
        !Matches((void*)(uintptr_t)MT_PUSH_ONE, kMtPushOne, sizeof(kMtPushOne)) ||
        !Matches((void*)(uintptr_t)MT_PUSH_STACK, kMtPushStack, sizeof(kMtPushStack)) ||
        !Matches((void*)(uintptr_t)MT_NEW_THREAD, kMtNewThread, sizeof(kMtNewThread)) || *slot != MT_SIMULATE) {
        Log("SKIP mono thread: [0x%08X]=0x%08X or [0x%08X]=0x%08X or GC code bytes differ, nothing written", MT_PUSH_HOOK, *hook,
            MT_SIM_SLOT, *slot);
        return false;
    }
    g_mtOrigPush = (MtVoidFn)(uintptr_t)*hook;
    g_mtSimOrig = (void*)(uintptr_t)*slot;
    const DWORD push = (DWORD)(uintptr_t)MtPushOtherRoots, stub = (DWORD)(uintptr_t)MtSimulateStub;
    if (!Write(hook, &push, 4)) { Log("FAIL mono thread: write [0x%08X] (err %lu)", MT_PUSH_HOOK, GetLastError()); return false; }
    k.mtReady = 1;
    if (Write(slot, &stub, 4)) k.mtReady |= 2;
    else Log("FAIL mono thread: Simulate vtable slot (err %lu); P2 stays, no sim registration", GetLastError());
    Log("OK   mono thread: [0x%08X] 0x%08X -> MtPushOtherRoots, Simulate slot 0x%08X %s (Mode=0, no worker)", MT_PUSH_HOOK,
        (DWORD)(uintptr_t)g_mtOrigPush, MT_SIM_SLOT, (k.mtReady & 2) ? "hooked" : "not hooked");
    return true;
}
