// [LocoWindow] design H2 (docs/86 §5): no queued async route job starts while the sim thread runs the locomotion pass.
// Included by ts3native.cpp after lotfd.cpp.
//
// Site [verified disassembly, Steam 1.67]: MonoScriptHost_Simulate 0xD81840 calls Locomotion_Update 0x83CED0 once, at
// 0xD81C5C: `mov ecx,[0x11D9140]; test ecx,ecx; jz 0xD81C61; fld [esp+58h]; push ecx; fstp [esp]; call 0x83CED0`. The
// callee is thiscall (ecx = locomotion system, one float on the stack), reads only ecx and [ebp+8] at entry (eax is zeroed,
// edx is written before any read), has one `ret 4` (0x83D383), and the caller uses no register, flag or x87 value it
// returns (next: `mov ecx,[0x11CD800]`). So a __fastcall(self, edx, float) thunk is a drop-in for the rel32.
//
// The thunk publishes g_g0wIn (both modes) and, in Mode 1, g_g0Win before it reads the job state; ServeAsync publishes
// A_RUNNING before it reads g_g0Win (Dekker order, both interlocked). So either the pass sees the job (g0wRunningAtEntry:
// it may finish inside the pass) or the worker sees the window and holds the start until the pass returns.
//
// Sim waits on the worker, reviewed for a held start (docs/86 §9 "H2 as built"):
//   WaitJobDone (IcPlanHook, DropJob <- AsyncStart / CANCEL / DropAllJobs): waits for a job that may be queued or claimed
//     and held -> G0wDrop(G0W_JOBWAIT) first.
//   RunPlan with [RouteWorker] Mode 1: the worker serves g_jobReady only after ServeAsync returns -> G0wDrop(G0W_SYNCPLAN).
//   WaitAsyncDone (RunPlan, RtWaitJob, DiscDrain), gate waits (RgEnter), LockAcquireAsync pump, QL / C-A waits (steerro.cpp):
//     they wait only for a job in A_RUNNING (or a lock it holds), which a held start never is -> no drop needed.
// A missed site cannot deadlock: the worker lets go after kG0wMaxHoldUs (G0wHolds, g0wWatchdog).
static const DWORD LOCO_CALL = 0x00D81C5C, LOCO_UPDATE = 0x0083CED0;
static const u8 kLocoCall[] = {0xD9, 0x44, 0x24, 0x58, 0x51, 0xD9, 0x1C, 0x24, 0xE8, 0x6F, 0xB2, 0xAB, 0xFF};  // at 0xD81C54
typedef void(__fastcall* LocoUpdateFn)(void* self, void* edx, float dt);
static LocoUpdateFn g_g0wOrig = (LocoUpdateFn)(uintptr_t)LOCO_UPDATE;  // selftest / steerro-test point it at a fake pass
static DWORD g_g0wTid;            // the thread inside the pass (the sim)
static __declspec(thread) int t_g0wDepth;
static LONG g_g0wDropLogged[2];

static void G0wDrop(int site, const void* ret) {
    if (!g_g0Win || GetCurrentThreadId() != g_g0wTid) return;
    InterlockedExchange(&g_g0Win, 0);  // for the rest of this pass; g_g0wIn stays (later starts count as in-pass)
    InterlockedIncrement(&ts3native_counters.g0wDrop[site]);
    if (!g_g0wDropLogged[site]++) Log("loco window: dropped before a sim wait (%s), caller %p", site ? "sync worker plan" : "WaitJobDone", ret);
}

static void __fastcall LocoWinThunk(void* self, void* edx, float dt) {
    if (t_g0wDepth) { g_g0wOrig(self, edx, dt); return; }  // nested: the outer pass owns the window
    Ts3nCounters& k = ts3native_counters;
    t_g0wDepth = 1;
    g_g0wTid = GetCurrentThreadId();
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    InterlockedExchange(&g_g0wIn, 1);
    if (k.g0wMode == 1) InterlockedExchange(&g_g0Win, 1);
    if (g_async.state == A_RUNNING) InterlockedIncrement(&k.g0wRunningAtEntry);
    __try {
        g_g0wOrig(self, edx, dt);
    } __finally {  // also when an exception leaves the pass: never a window left open
        InterlockedExchange(&g_g0Win, 0);
        InterlockedExchange(&g_g0wIn, 0);
        t_g0wDepth = 0;
        const LONG us = UsSince(t0);
        InterlockedIncrement(&k.g0wPasses);
        InterlockedExchangeAdd(&k.g0wPassUs, us);
        Max(&k.g0wPassMaxUs, us);
    }
}

// gate wait inside the pass (routegate.cpp RgEnter, after the wait)
static void G0wGateWait(DWORD key, LONG us) {
    if (!g_g0wIn || GetCurrentThreadId() != g_g0wTid) return;
    Ts3nCounters& k = ts3native_counters;
    InterlockedIncrement(&k.g0wGateWaits);
    if (key == 0x0062EF7A) { InterlockedIncrement(&k.g0wGate0Waits); InterlockedExchangeAdd(&k.g0wGate0Us, us); }
}

// [LocoWindow] Enabled=1: the call site bytes are checked first (refused and logged when they differ)
static void InstallLocoWindow(const wchar_t* ini) {
    Ts3nCounters& k = ts3native_counters;
    if (!GetPrivateProfileIntW(L"LocoWindow", L"Enabled", 0, ini)) { Log("loco window off (Enabled=0)"); return; }
    if (!k.rgEnforce || !k.asyncReady) { Log("SKIP loco window: needs [RouteGate] Enforce=1 with the async path"); return; }
    if (!Matches((const void*)(uintptr_t)(LOCO_CALL - 8), kLocoCall, sizeof(kLocoCall))) {
        Log("SKIP loco window: bytes at 0x%08X differ (not the Locomotion_Update call of Steam 1.67), nothing written", LOCO_CALL - 8);
        return;
    }
    k.g0wMode = Clamp(GetPrivateProfileIntW(L"LocoWindow", L"Mode", 1, ini), 0, 1);
    const CallSite site = {LOCO_CALL, LOCO_UPDATE, (void*)LocoWinThunk};
    if (!PatchCalls("loco window", &site, 1)) return;
    k.g0wReady = 1;
    Log("OK   loco window: call 0x%08X -> LocoWinThunk (Mode=%ld: %s)", LOCO_CALL, k.g0wMode,
        k.g0wMode ? "no job starts during the pass" : "count only");
}
