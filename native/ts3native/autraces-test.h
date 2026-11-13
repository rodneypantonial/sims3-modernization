
// TestAutRacesR2c: [AutShadow] Races bit 2 (R2c worker drain at world load / travel / quit) on the real run state: g_asRun,
// g_mtBusy (a helper thread stands in for the worker leaving), the stop flag, LastRun, the cut hold and the late leave.
// Release backport (rc1111g): no R1b pre-warm / R2b witness in this build; ArTestApply checks bits 0/1 are masked off and
// that world event 7 still reopens the gate without them.
static void ArTestApply() {
    wchar_t ini[MAX_PATH];
    GetTempPathW(MAX_PATH, ini);
    swprintf_s(ini + wcslen(ini), MAX_PATH - wcslen(ini), L"ts3n-races-test-%lu.ini", GetCurrentProcessId());
    Ts3nCounters& c = ts3native_counters;
    // Races=3 (R1b + R2b only, absent here): nothing in force, gate open, events do nothing
    WritePrivateProfileStringW(L"AutShadow", L"Races", L"3", ini);
    WritePrivateProfileStringW(L"AutShadow", L"DrainMs", L"99999", ini);
    ApplyAutRaces(ini);
    assert(g_arOn == 0 && !g_arGate && c.r2cOn == 0 && c.r2cDrainMs == 5000);
    ArWorldEvent(3);
    assert(!g_arGate && c.r2cCloses == 0);
    // Races=7: only bit 2 in force; closed until the first event 7, which reopens it with no R1 pre-warm in the build
    WritePrivateProfileStringW(L"AutShadow", L"Races", L"7", ini);
    WritePrivateProfileStringW(L"AutShadow", L"DrainMs", NULL, ini);  // default 250
    ApplyAutRaces(ini);
    assert(g_arOn == 4 && g_arGate == 1 && c.r2cOn == 1 && c.r2cDrainMs == 250 && g_arDrainMs == 250);
    ArWorldEvent(7);
    assert(!g_arGate && c.r2cReopens == 1);
    // release ini value Races=4
    WritePrivateProfileStringW(L"AutShadow", L"Races", L"4", ini);
    ApplyAutRaces(ini);
    assert(g_arOn == 4 && g_arGate == 1);
    ArWorldEvent(7);
    assert(!g_arGate && c.r2cReopens == 2);
    DeleteFileW(ini);
    c.r2cReopens = 0;
}
static DWORD WINAPI ArTestLeave(void* ms) {
    Sleep((DWORD)(uintptr_t)ms);
    InterlockedExchange(&g_mtBusy, 0);
    return 0;
}
static void TestAutRacesR2c() {
    static_assert(offsetof(Ts3nCounters, r2cOn) == 0x5FD0 && offsetof(Ts3nCounters, r2cReopens) == 0x6004 && sizeof(Ts3nCounters) == 0x6008,
                  "counters v187 layout (docs/registry.md)");
    static Ts3nCounters save;
    save = ts3native_counters;
    const Ts3nAutShadow ksave = ts3native_autshadow;
    const int on0 = g_arOn, ms0 = g_arDrainMs, satbMode0 = g_satbMode;
    void* const fin0 = g_asMFinish;
    const LONG run0 = g_asRun, busy0 = g_mtBusy, stop0 = g_asStop, last0 = g_asLast, req0 = g_asReqId;
    g_asMFinish = NULL;  // AsFinish: no managed call
    Ts3nCounters& c = ts3native_counters;
    c.r2cCloses = c.r2cIdle = c.r2cDrainReq = c.r2cDrained = c.r2cAborted = c.r2cSatb = c.r2cWaitUs = c.r2cWaitMaxUs = 0;
    c.r2cLate = c.r2cGateAborts = c.r2cHeld = c.r2cReopens = 0;
    g_arLatePending = false;
    ArTestApply();  // ini read: bits 0/1 masked off, event 7 reopens without R1b
    // off (bit 2 clear): going-away events leave the gate alone
    g_arOn = 2, g_arGate = 0;
    ArWorldEvent(3);
    assert(!g_arGate && c.r2cCloses == 0);
    // on: closed until the first event 7 (as ApplyAutRaces leaves it), which opens it once
    g_arOn = 4, g_arGate = 1, g_arDrainMs = 2000;
    ArWorldEvent(5);
    assert(g_arGate && c.r2cCloses == 0 && c.r2cIdle == 1);  // already closed: no second close
    ArWorldEvent(7);
    assert(!g_arGate && c.r2cReopens == 1);
    ArWorldEvent(7);
    assert(!g_arGate && c.r2cReopens == 1);
    // world quit with no job: close, idle; 0x11 and 9 are not going-away events
    g_asRun = AS_RUN_IDLE, g_mtBusy = 0;
    ArWorldEvent(0x11), ArWorldEvent(9);
    assert(!g_arGate && c.r2cCloses == 0);
    ArWorldEvent(3);
    assert(g_arGate && c.r2cCloses == 1 && c.r2cIdle == 2 && c.r2cDrainReq == 0);
    // a request posted while closed: no cut, finished as an epoch drop
    g_asReqId = 77, g_asBatchN = 1, g_asLast = AS_LAST_OK;
    const LONG cuts0 = ts3native_autshadow.cutReq;
    AsSimLeave();
    assert(c.r2cHeld == 1 && g_asLast == AS_LAST_EPOCH && !g_asReqId && g_asRun == AS_RUN_IDLE && ts3native_autshadow.cutReq == cuts0);
    ArWorldEvent(7);
    assert(!g_arGate && c.r2cReopens == 2);
    // travel out (event 3) with a job inside: stop, epoch drop, the job leaves after ~20 ms = drained
    g_asRun = AS_RUN_WAIT, g_mtBusy = 1, g_asStop = 0, g_asLast = AS_LAST_STOPPED;  // an overrun's kept part is dropped too
    HANDLE th = CreateThread(NULL, 0, ArTestLeave, (void*)(uintptr_t)20, 0, NULL);
    assert(th);
    ArWorldEvent(3);
    assert(g_arGate && g_asStop == 1 && g_asLast == AS_LAST_EPOCH && !g_mtBusy);
    assert(c.r2cDrainReq == 1 && c.r2cDrained == 1 && c.r2cAborted == 0 && c.r2cSatb == 0 && !g_arLatePending);
    assert(c.r2cWaitUs >= 10000 && c.r2cWaitUs < 2000000 && c.r2cWaitMaxUs == c.r2cWaitUs);
    WaitForSingleObject(th, INFINITE), CloseHandle(th);
    // the job's hook at phase 0 while the gate is closed: the stop flag goes on even with no epoch move
    void* const work0 = g_asMWork;
    const LONG ep0 = g_asRunEpoch;
    const unsigned short simCw0 = g_asSimCw;
    g_asMWork = (void*)(uintptr_t)0x4321, g_asSimCw = 0, g_asRunEpoch = g_asEpoch, g_asStop = 0;
    AsJobHook(0, g_asMWork);
    assert(g_asStop == 1);
    AsJobHook(1, g_asMWork);
    g_asMWork = work0, g_asSimCw = simCw0, g_asRunEpoch = ep0;
    // the finish of the drained job, then load started (5) / finished (7): the return trip is the same sequence
    g_asRun = AS_RUN_IDLE, g_asReqId = 0;
    ArWorldEvent(5);
    assert(g_arGate && c.r2cCloses == 2 && c.r2cIdle == 3);
    ArWorldEvent(7);
    assert(!g_arGate && c.r2cReopens == 3);
    // return trip under Satb act with a job that never leaves within DrainMs: abort fallback, then it leaves later
    g_satbMode = SATB_ACT, g_satbLive = 1;
    g_arDrainMs = 1;
    g_asRun = AS_RUN_WAIT, g_mtBusy = 1, g_asStop = 0, g_asLast = AS_LAST_OK;
    ArWorldEvent(3);
    assert(g_arGate && g_asStop == 1 && g_asLast == AS_LAST_EPOCH && g_mtBusy == 1);
    assert(c.r2cDrainReq == 2 && c.r2cDrained == 1 && c.r2cAborted == 1 && c.r2cSatb == 1 && g_arLatePending);
    g_satbLive = 0, g_satbMode = satbMode0;
    ArWorldEvent(5);  // still inside: a second bounded wait, a second abort; the late leave is counted once
    assert(c.r2cDrainReq == 3 && c.r2cAborted == 2 && c.r2cCloses == 3);
    g_mtBusy = 0, g_asJobExc = 0, g_asTrack0 = ts3native_mtprobe.workerTrackSkips;
    AsSimLeave();  // the WAIT branch sees the job out: late leave, finish as an epoch drop
    assert(c.r2cLate == 1 && !g_arLatePending && g_asRun == AS_RUN_IDLE && g_asLast == AS_LAST_EPOCH);
    ArWorldEvent(7);
    assert(!g_arGate && c.r2cReopens == 4);
    // quit app (4) and load failed (8) close it too and nothing reopens it without event 7
    ArWorldEvent(4);
    assert(g_arGate && c.r2cCloses == 4);
    ArWorldEvent(7);
    ArWorldEvent(8);
    assert(g_arGate && c.r2cCloses == 5 && c.r2cReopens == 5);
    g_arGate = 0, g_arOn = on0, g_arDrainMs = ms0, g_arLatePending = false;
    g_asMFinish = fin0, g_asRun = run0, g_mtBusy = busy0, g_asStop = stop0, g_asLast = last0, g_asReqId = req0, g_asBatchN = 0;
    ts3native_counters = save;
    ts3native_autshadow = ksave;
    printf("autraces: R2c drain, abort fallback, cut hold, late leave, reopen ok\n");
}
