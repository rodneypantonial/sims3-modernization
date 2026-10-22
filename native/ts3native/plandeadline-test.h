// --- [RouteAsync] PlanDeadlineMs / FailMemo (plandeadline.cpp, docs/164 §7): layout; the entry stubs on dummy cell A* and water
// functions with the game's prologue bytes (pass through outside a session, before the deadline, on another thread, with the
// switch off; past it the game's no-path exit: fields, AL, stack); sessions by reason (own task exempt), the non-IC session and
// its world A* cap through CallPlanBudget and the cost stub; the IC path through SyncIc; FailMemo on the sync Plan IC.
// Included by selftest.cpp after replandedupe-test.h (its route fakes); runs after TestRouteBudget (g_cost = a patched stub).
static volatile LONG g_pdtRuns[2];
static u8* g_pdtCell;
// calls a dummy (thiscall, n stack args, callee cleanup); returns AL, *bad = esp did not come back
static u8 PdtCall(const void* fn, void* self, int n, const DWORD* args, bool* bad) {
    DWORD before = 0, after = 0, res = 0;
    __asm {
        mov before, esp
        mov ecx, n
        mov edx, args
    next:
        test ecx, ecx
        jz go
        push dword ptr [edx + ecx * 4 - 4]
        dec ecx
        jmp next
    go:
        mov ecx, self
        call fn
        mov res, eax
        mov after, esp
        mov esp, before  // a wrong ret N must not wreck this frame
    }
    *bad = before != after;
    return (u8)res;
}
// a fake route IC: waits past the deadline, then one cell A* call through the stub; result type 16 (failed) unless it searched
static u8* __cdecl PdtIc(u8* out, DWORD) {
    Sleep(3);
    u8 self[0x40] = {};
    const DWORD a[6] = {};
    bool bad;
    const u8 r = PdtCall(g_pdtCell, self, 6, a, &bad);
    assert(!bad);
    memset(out, 0, 0x48);
    *(DWORD*)out = r ? 1 : 16;
    return out;
}

static void TestPlanDeadline() {
    static_assert(offsetof(Ts3nCounters, pdMode) == 0x4244 && offsetof(Ts3nCounters, pdSessions) == 0x4254 &&
                      offsetof(Ts3nCounters, pdLines) == 0x427C && offsetof(Ts3nCounters, fmMode) == 0x4280 &&
                      offsetof(Ts3nCounters, fmInval) == 0x4298 && offsetof(Ts3nCounters, fmServedUs) == 0x42C4 &&
                      sizeof(Ts3nCounters) == 4274 * 4,
                  "counters v99 layout after v98 (docs/registry.md)");
    Ts3nCounters& k = ts3native_counters;
    assert(k.version == 99);
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    memset(&k.pdMode, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, pdMode));
    u8* code = (u8*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(code);
    // cell A*: the game's prologue (push ebp; mov ebp,esp; and esp,-16), inc [runs0], mov esp,ebp; pop ebp; mov al,1; ret 18h
    u8* cell = code;
    static const u8 kC[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0xFF, 0x05, 0, 0, 0, 0, 0x8B, 0xE5, 0x5D, 0xB0, 0x01, 0xC2, 0x18, 0x00};
    memcpy(cell, kC, sizeof(kC));
    *(volatile LONG**)(cell + 8) = &g_pdtRuns[0];
    // water: the game's prologue (mov eax,[esp+24h]; sub esp,50h), inc [runs1], add esp,50h; mov al,1; ret 24h
    u8* water = code + 64;
    static const u8 kW[] = {0x8B, 0x44, 0x24, 0x24, 0x83, 0xEC, 0x50, 0xFF, 0x05, 0, 0, 0, 0, 0x83, 0xC4, 0x50, 0xB0, 0x01, 0xC2, 0x24, 0x00};
    memcpy(water, kW, sizeof(kW));
    *(volatile LONG**)(water + 9) = &g_pdtRuns[1];
    g_pdtCell = cell;
    const LONG ready0 = k.budgetReady;
    k.budgetReady = 1;
    PdInstallAt((DWORD)(uintptr_t)cell, (DWORD)(uintptr_t)water);
    assert(g_pdOn && g_pdOrig[0] && g_pdOrig[1] && k.pdReady == 7);
    DWORD simTid = GetCurrentThreadId();
    const DWORD* savedSim = g_rtSimTidSlot;
    g_rtSimTidSlot = &simTid;
    u8 self[0x40];
    DWORD vec[3] = {0x1000, 0x1040, 0x1080};  // begin, end, capacity
    DWORD ca[6] = {}, wa[9] = {};
    wa[8] = (DWORD)(uintptr_t)vec;
    bool bad;
    auto both = [&](u8 want, LONG runs) {
        memset(self, 0x77, sizeof(self));
        vec[1] = 0x1040;
        const LONG r0 = g_pdtRuns[0], r1 = g_pdtRuns[1];
        assert(PdtCall(cell, self, 6, ca, &bad) == want && !bad && g_pdtRuns[0] == r0 + runs);
        assert(PdtCall(water, self, 9, wa, &bad) == want && !bad && g_pdtRuns[1] == r1 + runs);
    };
    // 1. no session: the dummies run
    both(1, 1);
    // 2. a non-IC session on the sim thread (pdUs 1 s): before its deadline they run; t_fbDeadline is the session's
    k.pdUs = 1000000, k.pdMode = 1;
    assert(PdPlanBegin() && t_pd.open && t_pd.entry == 1 && t_fbDeadline == t_pd.dl && g_pdTid == simTid && k.pdSessions[1] == 1);
    assert(!PdPlanBegin());  // nested: one session
    both(1, 1);
    // 3. past it: the game's no-path exits, nothing searched; cell [this+0x24] = 0, water out end = begin, AL 0, stack right
    t_pd.dl = 1;
    both(0, 0);
    assert(*(DWORD*)(self + 0x24) == 0 && *(DWORD*)(self + 0x28) == 0x77777777 && vec[1] == vec[0] && vec[2] == 0x1080);
    assert(k.pdSkipCell == 1 && k.pdSkipWater == 1 && t_pd.skip[0] == 1 && t_pd.skip[1] == 1);
    // 4. switch off mid-session, then another thread's session: they run
    k.pdMode = 0;
    both(1, 1);
    k.pdMode = 1;
    g_pdTid = 1;
    both(1, 1);
    g_pdTid = simTid;
    // 5. the end: counted as a hit (failed), t_fbDeadline back, the stubs pass again
    PdPlanEnd(0);
    assert(!t_pd.open && t_fbDeadline == 0 && !g_pdTid && k.pdHit == 1 && k.pdHitOk == 0 && k.pdLines == 1);
    both(1, 1);
    // 6. not the sim thread: no session; pdUs 0: no non-IC session
    simTid = 1;
    assert(!PdPlanBegin());
    simTid = GetCurrentThreadId();
    k.pdUs = 0;
    assert(!PdPlanBegin());
    // 7. IC deadlines by reason: NoStart / Foreign = pdUs, own task = pdOwnUs (0 = exempt, counted)
    k.pdUs = 200000;
    assert(PdIcUs(RFB_NOSTART) == 200000 && PdIcUs(RFB_FOREIGN) == 200000 && k.pdOwnExempt == 0);
    assert(PdIcUs(RFB_QFULL) == 0 && PdIcUs(RFB_NOYIELD) == 0 && k.pdOwnExempt == 2);
    k.pdOwnUs = 3000000;
    assert(PdIcUs(RFB_QFULL) == 3000000 && k.pdOwnExempt == 2);
    k.pdMode = 0;
    assert(PdIcUs(RFB_NOSTART) == 0);
    k.pdMode = 1, k.pdOwnUs = 0;
    // 8. D1 through CallPlanBudget: a sim-thread plan opens its session; its search sleeps past 2 ms, the next cost call
    //    with the clock read caps it (every edge a wall), the plan fails through its own exit; counted, deadline cleared
    const PlanLockedFn plan0 = g_planOrig;
    const LONG bmode0 = k.budgetMode;
    k.budgetMode = 0;
    k.pdUs = 2000;
    g_planOrig = [](void*, void*, int*, int*, int*, DWORD, DWORD, u8*, int*, int*, DWORD, DWORD) -> u8 {
        assert(t_pd.open && t_fbDeadline && t_budget.on && t_budget.dl == t_fbDeadline);
        g_cost(0, 0, 0, 0, 0, 0, 0, 0);
        for (;;) {
            if (g_cost(0, 0, 0, 0, 1, 0, 0, 0) >= 1e8f) return 0;
            g_cost(0, 0, 0, 0, 0, 0, 0, 0);
            Sleep(1);
        }
    };
    u8 opts[0x40] = {};
    int list[4] = {};
    assert(CallPlanBudget(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 0);
    assert(k.pdSessions[1] == 2 && k.pdWorldCapped == 1 && k.pdHit == 2 && !t_pd.open && !t_fbDeadline && !t_budget.on);
    //    a plan under its deadline: unchanged, no hit
    g_planOrig = [](void*, void*, int*, int*, int*, DWORD, DWORD, u8*, int*, int*, DWORD, DWORD) -> u8 {
        for (int i = 0; i < 8; i++) assert(g_cost(0, 0, 0, 0, i & 1, 0, 0, 0) < 1e8f);
        return 1;
    };
    k.pdUs = 1000000;
    assert(CallPlanBudget(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 1 && k.pdSessions[1] == 3 && k.pdHit == 2);
    //    pdMode 0: no session at all
    k.pdMode = 0;
    assert(CallPlanBudget(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 1 && k.pdSessions[1] == 3);
    k.pdMode = 1;
    // 9. the IC path: SyncIc opens an IC session (reason NoStart, 2 ms); the IC's cell call after 3 ms is skipped, the plan
    //    fails; nested Route_PlanLocked would not open its own; own-task reasons run without a deadline (exempt)
    const IcPlanFn ic0 = g_icPlanOrig;
    g_icPlanOrig = PdtIc;
    const LONG am0 = k.asyncMode, log0 = k.rfbLogMs;
    k.asyncMode = 0, k.rfbLogMs = 0, k.pdUs = 2000;
    u8 out[0x48];
    const LONG c0 = g_pdtRuns[0];
    assert(SyncIc(0, out, 0x1234, NULL, RFB_NOSTART, "nojob", NULL) == out && *(DWORD*)out == 16 && g_pdtRuns[0] == c0);
    assert(k.pdSessions[0] == 1 && k.pdHit == 3 && k.pdSkipCell == 2 && !t_pd.open && !g_pdTid && !t_fbDeadline);
    assert(SyncIc(0, out, 0x1234, NULL, RFB_QFULL, "nojob", NULL) == out && *(DWORD*)out == 1 && g_pdtRuns[0] == c0 + 1);
    assert(k.pdSessions[0] == 1 && k.pdOwnExempt == 3);
    // 10. FailMemo on the sync Plan IC (ReplanDedupe's fakes): observe, then serve a failed repeat; a worker-side CallIc (no
    //     t_fmSync) never goes through it
    u8* const* rmSlot = g_orRmSlot;
    const OrRouteFn route0 = g_orRoute;
    const RdFollowerPosFn pos0 = g_rdFollowerPos;
    static u8* rmPtr = g_rdtRm;
    g_orRmSlot = &rmPtr, g_orRoute = RdtRoute, g_rdFollowerPos = RdtFollowerPos;
    static u8 world[0xA00], planner[0x200], wm[0x200];
    *(u8**)(world + 0x95C) = planner;
    *(u8**)(world + 0x964) = wm;
    void* const lock0 = g_rgLock;
    const LONG snReady0 = k.snReady;
    g_rgLock = world + 0x918, k.snReady = 5;
    *(DWORD*)(g_rdtRoute + 0x40) = 7;
    memset(g_rdTab, 0, sizeof(g_rdTab));
    k.pdMode = 0;
    const LONG rdc0 = k.rdCalls[0], rdc1 = k.rdCalls[1], rds0 = k.rdServed;
    g_fmOn = 1, k.fmMode = 0;
    g_icPlanOrig = [](u8* o, DWORD h) -> u8* { return RdtIc(0, o, h, NULL); };
    g_rdtType = 16;
    const DWORD p0 = g_rdtPlans, cnt = *(DWORD*)(g_rdtRoute + 0x1F4);
    SyncIc(0, out, 0x25000001, NULL, RFB_NOSTART, "nojob", NULL);
    SyncIc(0, out, 0x25000001, NULL, RFB_NOSTART, "nojob", NULL);
    assert(g_rdtPlans == p0 + 2 && k.fmCalls == 2 && k.fmFails == 2 && k.fmRep == 1 && k.fmWould[0] == 1 && k.fmWouldSame[0] == 1);
    k.fmMode = 1;
    memset(out, 0xCC, sizeof(out));
    SyncIc(0, out, 0x25000001, NULL, RFB_NOSTART, "nojob", NULL);
    assert(g_rdtPlans == p0 + 2 && k.fmServed == 1 && *(DWORD*)out == 16 && *(DWORD*)(g_rdtRoute + 0x1F4) == cnt);  // no +0x1F4 bump
    assert(k.rdCalls[0] == rdc0 && k.rdCalls[1] == rdc1 && k.rdServed == rds0);  // ReplanDedupe's own rows untouched
    CallIc(0, out, 0x25000001, NULL);  // not SyncIc's: planned
    assert(g_rdtPlans == p0 + 3 && k.fmCalls == 3);
    for (RdEntry& e : g_rdTab) e.ms = e.ms ? e.ms - 2000 : 0;  // out of every window
    g_rdtType = 1;  // a success is never served
    SyncIc(0, out, 0x25000001, NULL, RFB_NOSTART, "nojob", NULL);
    SyncIc(0, out, 0x25000001, NULL, RFB_NOSTART, "nojob", NULL);
    assert(g_rdtPlans == p0 + 5 && k.fmServed == 1);
    g_rdtType = 16;
    // restore
    g_fmOn = 0;
    g_orRmSlot = rmSlot, g_orRoute = route0, g_rdFollowerPos = pos0;
    g_rgLock = lock0, k.snReady = snReady0;
    g_icPlanOrig = ic0, g_planOrig = plan0;
    k.asyncMode = am0, k.rfbLogMs = log0, k.budgetMode = bmode0, k.budgetReady = ready0;
    g_rtSimTidSlot = savedSim;
    g_pdOn = 0;
    VirtualFree(code, 0, MEM_RELEASE);
    memset(&k.pdMode, 0, sizeof(Ts3nCounters) - offsetof(Ts3nCounters, pdMode));
    printf("PlanDeadline / FailMemo (docs/164 §7): counters v99; cell / water entry stubs (pass outside a session, before the "
           "deadline, switch off, other thread; past it the game's no-path exits, stack right), non-IC session + world A* cap "
           "through the cost stub, IC sessions by reason (own task exempt), FailMemo observe / serve / not on worker calls ok\n");
}
