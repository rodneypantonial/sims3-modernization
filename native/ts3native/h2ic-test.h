// --- docs/177 §4 H2 IC window (locowin.cpp, counters v102 hx*): on the same async setup as TestLocoWindow (real worker, gate
// enforced): a job queued inside a windowed IC is held until the IC returns; nesting; a sim wait for the held job drops the
// window; a callee-cleanup IC; an exception leaves a frame that the next entry drops; Mode 0 counts only. Included by
// selftest.cpp after TestH2Threaded.
static int g_hxCase;
static DWORD g_hxSlots[7];
typedef DWORD(__cdecl* HxFn)(DWORD, DWORD);
static DWORD __cdecl FakeIcBody(DWORD a, DWORD b) {
    Ts3nCounters& k = ts3native_counters;
    const int which = g_hxCase;
    g_hxCase = 0;
    switch (which) {
        case 1: {  // a job queued inside is held, not started, until the IC returns
            assert(g_g0Win == 1 && g_hxOpen == 1 && t_hx.n == 1);
            const LONG held0 = k.hxHeld;
            assert(Start(kH1));
            Sleep(60);
            assert(g_async.state != A_RUNNING && JobFor(kH1)->state != J_DONE && k.hxHeld == held0 + 1);
            break;
        }
        case 2:  // nested IC: the window stays up after the inner one returns
            assert(((HxFn)(uintptr_t)g_hxSlots[1])(1, 1) == 2 && t_hx.n == 1 && g_g0Win == 1 && g_hxOpen == 1);
            break;
        case 3: {  // a sim wait for the held job drops the window first (no 250 ms watchdog)
            u8 out[0x48];
            assert(Start(kH1));
            while (JobFor(kH1)->state != J_RUNNING) Sleep(0);
            Sleep(10);
            assert(g_async.state != A_RUNNING);
            const DWORD t0 = GetTickCount();
            assert(IcPlanHook(out, kH1) == out && *(DWORD*)(out + 0x40) == GetCurrentThreadId());
            assert(g_g0Win == 0);
            TIMING_ASSERT("H2Ic job-wait plan < 200 ms", GetTickCount() - t0 < 200);
            break;
        }
        case 4:
            RaiseException(0xE0000002, 0, 0, NULL);
    }
    return a + b;
}
static DWORD __stdcall FakeIcStd(DWORD a, DWORD b) { return a * b; }  // callee cleanup (ret 8)

static void TestH2Ic() {
    Ts3nCounters& k = ts3native_counters;
    memset((void*)&k.hxMode, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, hxMode));
    const LONG wd0 = k.g0wWatchdog, drop0 = k.g0wDrop[G0W_JOBWAIT];
    DWORD me = GetCurrentThreadId();
    const DWORD* savedSim = g_rtSimTidSlot;
    g_rtSimTidSlot = &me;
    for (int i = 0; i < 7; i++) g_hxSlots[i] = kHxIc[i].fn;
    g_hxSlots[3] = 0x12345678;  // another owner: left alone
    for (int i = 0; i < 7; i++) if (HxSwap(i, &g_hxSlots[i])) k.hxReady++;
    assert(k.hxReady == 6 && g_hxSlots[3] == 0x12345678 && g_hxSlots[0] == (DWORD)(uintptr_t)kHxShims[0]);
    g_hxOrig[0] = g_hxOrig[1] = (void*)FakeIcBody;
    g_hxOrig[4] = (void*)FakeIcStd;
    const HxFn ic0 = (HxFn)(uintptr_t)g_hxSlots[0];
    typedef DWORD(__stdcall * HxStd)(DWORD, DWORD);
    const HxStd ic4 = (HxStd)(uintptr_t)g_hxSlots[4];
    k.hxMode = 1;
    // 1. hold
    g_hxCase = 1;
    assert(ic0(2, 3) == 5 && g_g0Win == 0 && g_hxOpen == 0 && t_hx.n == 0);
    Collect(kH1);
    assert(k.hxCalls[0] == 1 && k.g0wWatchdog == wd0);
    // 2. nested, and a callee-cleanup IC (ret 8) through the leave thunk
    g_hxCase = 2;
    assert(ic0(2, 3) == 5 && k.hxCalls[1] == 1 && g_g0Win == 0 && t_hx.n == 0);
    assert(ic4(6, 7) == 42 && k.hxCalls[4] == 1 && g_g0Win == 0 && t_hx.n == 0);
    // 3. drop before WaitJobDone
    g_hxCase = 3;
    assert(ic0(1, 2) == 3 && k.g0wDrop[G0W_JOBWAIT] == drop0 + 1 && k.g0wWatchdog == wd0 && g_hxOpen == 0);
    // 4. an exception through the IC leaves its frame and the window; the next entry at the same depth drops both
    g_hxCase = 4;
    __try { ic0(1, 1); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    assert(t_hx.n == 1 && g_g0Win == 1 && g_hxOpen == 1);
    assert(ic0(1, 1) == 2 && k.hxStale == 1 && t_hx.n == 0 && g_g0Win == 0 && g_hxOpen == 0);
    // 5. a job running at entry is counted (the game's IC would wait for it at its +0x918 acquire)
    g_icSleepMs = 40;
    assert(Start(kH1));
    while (g_async.state != A_RUNNING) Sleep(0);
    assert(ic0(1, 1) == 2 && k.hxRunningAtEntry == 1);
    Collect(kH1);
    g_icSleepMs = 0;
    // 6. Mode 0: counted, no window, no frame
    k.hxMode = 0;
    assert(ic0(1, 1) == 2 && t_hx.n == 0 && g_g0Win == 0 && k.hxCalls[0] == 7);
    // 7. another thread: passed through
    HANDLE th = CreateThread(NULL, 0, [](LPVOID p) -> DWORD { return ((HxFn)p)(4, 4); }, (LPVOID)ic0, 0, NULL);
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    assert(k.hxOffSim == 1 && k.hxCalls[0] == 7);
    assert(!AsyncBusy() && g_rgOffSimOpen == 0 && g_g0Win == 0);
    g_rtSimTidSlot = savedSim;
    memset((void*)&k.hxMode, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, hxMode));
    printf("H2 IC window: slot swap (other owner left), start held inside the IC, nesting, ret-N leave, drop before a job wait, "
           "unwound frame dropped, running at entry, Mode 0, off-sim ok\n");
}
