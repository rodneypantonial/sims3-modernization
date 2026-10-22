// [RouteAsync] PlanDeadlineMs / PlanDeadlineOwnMs (docs/164 §4 D1-D4) and FailMemo (D5), docs/164 §7, counters v99. Included by
// ts3native.cpp after earlyout.cpp and replandedupe.cpp (RdRun), before routephases.cpp (RjpOwner takes our trampolines).
//
// D1-D4: one wall deadline per sim-thread sync plan (a "session"), opened at the outermost of
//   - SyncIc, the route IC's sync path (the whole IC: goal step, every nested Route_PlanLocked, result mapping). Deadline by
//     reason: NoStart / Foreign (no own task: docs/141 classes b and c, ThreadedObjects, alarms, resets, other Sims' tasks) =
//     pdUs; any other reason (the route Sim's own task could not go async: NoYield, QueueFull, M1*, Unstarted, ...) = pdOwnUs,
//     0 by default = never cut (a route the Sim must take is not failed for time);
//   - CallPlanBudget on the sim thread with no session open (Replan / PlanFromPoint / ReplanFromPosition with A10 off, any other
//     non-IC sim plan) = pdUs. The goal step of such a plan runs before Route_PlanLocked, outside the session.
// Past the deadline every search of the plan answers the way the game's own no-path exit does, so the planner's own failure
// handling runs and the plan ends as an ordinary failed plan (the script callers already handle that):
//   world A* 0x676260 (D1; D2 = every later candidate of the goal-lot entry loop 0x65F809 and open-ground loop 0x65FB99 /
//     0x65FDA5): the built FbJobCapMs path. The session sets t_fbDeadline; CallPlanBudget gives each Route_PlanLocked a Budget
//     with it; BudgetStep reads the clock on every 4th cost call (the first included) and from then on every edge costs the
//     wall, the open list drains through the A*'s "open list empty" exit 0x676BF7 (b.capped stays set for that Route_PlanLocked)
//   cell A* 0x61E500 (D3; lot legs D4 through it: LotPlan_Inner takes its own code-5 path, LotPlan 0x6410B0 its own retry per
//     goal room, each retry cut again): the A*'s empty-start-set exit (0x61E53C je 0x61E62F): [this+0x24] = 0, AL = 0,
//     ret 0x18, nothing else written [verified disassembly, Steam 1.67]
//   water 0x671930 (D4): its "not the same water body" exit (0x671948 ja 0x671B98): out vector (arg 9) end = begin, AL = 0,
//     ret 0x24 [verified disassembly]
// Not cut (no exact no-write exit, or not a search; docs/164 §7.2): portal A* 0x63E340 (writes its context fields before any
// exit), LotPlan 0x6410B0 itself (PathPlanner_PlanHierarchical hands its out indices to 0x650950 whatever the code), one cell
// A* already running (bounded by the game's 64,000-node cap), LotGraph_Build, pending lot updates, post pass / insert (success
// path only), the IC's goal step and script callbacks.
// A plan that finishes before its deadline runs exactly as before: the stubs pass (one thread compare), the cost stubs read
// the clock every 4th call. With a deadline, SyncCapMs's node clock is not used inside that plan (CallPlanBudget: dl wins).
//
// D5 FailMemo: replandedupe.cpp's RdRun with kind 0 on SyncIc's Plan call (CallIc, t_fmSync): a repeat of a FAILED plan within
// 500 ms with the same follower, goal, start within 10 cm, route state as that plan left it and no routing write since is
// answered with that plan's result bytes.

static void* g_pdOrig[2];          // trampolines: [0] cell A*, [1] water ([RouteJobPhases] puts its stubs here, RjpOwner)
static volatile DWORD g_pdTid;     // the sim's id while its session has a deadline (the entry stubs' thread compare), else 0
static int g_fmIni;                // [RouteAsync] FailMemo as read
struct PdSess {
    bool open;
    int entry;                     // 0 IC, 1 Route_PlanLocked
    LONGLONG t0, dl, savedDl;
    LONG skip[2], capped;
    char ent[48];
    DWORD handle;
};
static __declspec(thread) PdSess t_pd;
static DWORD g_pdMinute, g_pdLinesMin;

static void ReadPlanDeadline(const wchar_t* ini) {
    Ts3nCounters& k = ts3native_counters;
    const int ms = GetPrivateProfileIntW(L"RouteAsync", L"PlanDeadlineMs", 0, ini);
    const int own = GetPrivateProfileIntW(L"RouteAsync", L"PlanDeadlineOwnMs", 0, ini);
    k.pdUs = ms > 0 ? Clamp(ms, 50, 60000) * 1000 : 0;
    k.pdOwnUs = own > 0 ? Clamp(own, 50, 60000) * 1000 : 0;
    k.pdMode = k.pdUs || k.pdOwnUs ? 1 : 0;
    g_fmIni = Clamp(GetPrivateProfileIntW(L"RouteAsync", L"FailMemo", 0, ini), 0, 2);
    Log("route async: PlanDeadlineMs=%ld PlanDeadlineOwnMs=%ld FailMemo=%d", k.pdUs / 1000, k.pdOwnUs / 1000, g_fmIni);
}

static LONGLONG PdNow() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }

static void PdOpen(int entry, LONG us) {
    PdSess& s = t_pd;
    memset(&s, 0, sizeof(s));
    s.open = true;
    s.entry = entry;
    s.t0 = PdNow();
    s.dl = s.t0 + (LONGLONG)us * g_qpf.QuadPart / 1000000;
    s.savedDl = t_fbDeadline;
    t_fbDeadline = s.dl;  // read by CallPlanBudget for every Route_PlanLocked of the session (D1, D2)
    strcpy_s(s.ent, "Route_PlanLocked (no IC)");
    InterlockedExchange((volatile LONG*)&g_pdTid, (LONG)GetCurrentThreadId());
    InterlockedIncrement(&ts3native_counters.pdSessions[entry]);
}

// ok = the plan's own result: succeeded
static void PdClose(bool ok) {
    Ts3nCounters& k = ts3native_counters;
    PdSess& s = t_pd;
    const LONGLONG now = PdNow();
    t_fbDeadline = s.savedDl;
    InterlockedExchange((volatile LONG*)&g_pdTid, 0);
    s.open = false;
    if (now < s.dl) return;
    const LONGLONG lateUs = (now - s.dl) * 1000000 / g_qpf.QuadPart;  // a test sets dl = 1 ("already past")
    const LONG us = (LONG)((now - s.t0) * 1000000 / g_qpf.QuadPart), late = lateUs > 0x7FFFFFFF ? 0x7FFFFFFF : (LONG)lateUs;
    InterlockedIncrement(&k.pdHit);
    if (ok) InterlockedIncrement(&k.pdHitOk);
    InterlockedExchangeAdd(&k.pdHitUs, us);
    Max(&k.pdLateMaxUs, late);
    const DWORD tick = GetTickCount();
    if (tick - g_pdMinute >= 60000) g_pdMinute = tick, g_pdLinesMin = 0;
    if (g_pdLinesMin >= 20) return;
    g_pdLinesMin++;
    InterlockedIncrement(&k.pdLines);
    Log("route deadline: sync plan %ld ms %s, %ld ms past its deadline, entry %s, route %08lX; world A* capped %ld, cell skipped %ld, "
        "water skipped %ld", us / 1000, ok ? "succeeded" : "failed", late / 1000, s.ent, s.handle, s.capped, s.skip[0], s.skip[1]);
}

static LONG PdIcUs(int why) {
    Ts3nCounters& k = ts3native_counters;
    if (k.pdMode != 1 || t_pd.open) return 0;
    if (why == RFB_NOSTART || why == RFB_FOREIGN) return k.pdUs;
    if (!k.pdOwnUs && k.pdUs) InterlockedIncrement(&k.pdOwnExempt);
    return k.pdOwnUs;
}
static void PdIcBegin(LONG us, int kind, int why, const char* entry, DWORD handle) {
    PdOpen(0, us);
    sprintf_s(t_pd.ent, "IC %s/%s/%s", kArIcName[kind & 3], kRfbName[why], entry);
    t_pd.handle = handle;
}
static void PdIcEnd(const u8* out) { if (t_pd.open && t_pd.entry == 0) PdClose(*(const DWORD*)out == 1); }  // type 1 = succeeded

static bool PdPlanBegin() {
    const Ts3nCounters& k = ts3native_counters;
    if (k.pdMode != 1 || !k.pdUs || t_pd.open || !RgIsSim(GetCurrentThreadId())) return false;
    PdOpen(1, k.pdUs);
    return true;
}
static void PdPlanEnd(u8 r) { if (t_pd.open && t_pd.entry == 1) PdClose(r != 0); }

static void PdCapped() {
    if (!t_pd.open) return;
    t_pd.capped++;
    InterlockedIncrement(&ts3native_counters.pdWorldCapped);
}

static int g_pdSkipMask = 3;  // bit 0 cell, bit 1 water (steerro-test cuts one phase at a time; always 3 in game)
// the entry stubs' question (sim thread inside a session only): 1 = answer "no path" without searching
static int __cdecl PdSkip(int which) {
    PdSess& s = t_pd;
    if (!s.open || ts3native_counters.pdMode != 1 || !(g_pdSkipMask >> which & 1) || PdNow() < s.dl) return 0;
    s.skip[which]++;
    InterlockedIncrement(which ? &ts3native_counters.pdSkipWater : &ts3native_counters.pdSkipCell);
    return 1;
}

// cell A* 0x61E500 (thiscall, 6 stack args): past the deadline its empty-start-set exit, else on to the trampoline
static __declspec(naked) void PdCellStub() {
    __asm {
        push eax
        mov eax, dword ptr fs:[0x24]
        cmp eax, g_pdTid
        jne off
        pushad
        pushfd
        push 0
        call PdSkip
        add esp, 4
        test eax, eax
        jnz skip
        popfd
        popad
    off:
        pop eax
        jmp dword ptr [g_pdOrig]
    skip:
        popfd
        popad
        pop eax
        mov dword ptr [ecx + 0x24], 0  // 0x61E539: the inserted-node counter
        mov eax, dword ptr [esp + 8]   // eax as the game's exit leaves it (argument 2), AL = 0
        xor al, al
        ret 0x18
    }
}

// water 0x671930 (thiscall, 9 stack args): past the deadline its "not the same water body" exit
static __declspec(naked) void PdWaterStub() {
    __asm {
        push eax
        mov eax, dword ptr fs:[0x24]
        cmp eax, g_pdTid
        jne off
        pushad
        pushfd
        push 1
        call PdSkip
        add esp, 4
        test eax, eax
        jnz skip
        popfd
        popad
    off:
        pop eax
        jmp dword ptr [g_pdOrig + 4]
    skip:
        popfd
        popad
        pop eax
        mov eax, dword ptr [esp + 0x24]  // 0x671930..0x67193C: the out vector's end = its begin
        mov ecx, dword ptr [eax]
        mov dword ptr [eax + 4], ecx
        xor al, al                       // 0x671B98
        ret 0x24
    }
}

static const u8 kPdCellPro[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};         // push ebp; mov ebp,esp; and esp,-16
static const u8 kPdWaterPro[] = {0x8B, 0x44, 0x24, 0x24, 0x83, 0xEC, 0x50};  // mov eax,[esp+24h]; sub esp,50h

// parameters let the selftest put the stubs on dummies
static void PdInstallAt(DWORD cellAt, DWORD waterAt) {
    Ts3nCounters& k = ts3native_counters;
    g_pdOrig[0] = InstallDetour("QuadTreeCellAStar (plan deadline)", cellAt, kPdCellPro, sizeof(kPdCellPro), (void*)PdCellStub);
    g_pdOrig[1] = InstallDetour("WaterPath_Plan (plan deadline)", waterAt, kPdWaterPro, sizeof(kPdWaterPro), (void*)PdWaterStub);
    k.pdReady = (k.budgetReady ? 1 : 0) | (g_pdOrig[0] ? 2 : 0) | (g_pdOrig[1] ? 4 : 0);
    g_pdOn = 1;
}

// after the plan detour, the cost stubs, InstallAsync (SyncIc) and ReplanDedupe; before [RouteJobPhases]
static void InstallPlanDeadline() {
    Ts3nCounters& k = ts3native_counters;
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    if (k.pdUs || k.pdOwnUs) {
        PdInstallAt(0x0061E500, 0x00671930);
        Log("%s route deadline: PlanDeadlineMs %ld, own task %ld (0 = never); ready 0x%lX (bit 0 world A* cost stubs, 1 cell A*, "
            "2 water), Route_PlanLocked detour %s, route IC sync path %s", k.pdReady == 7 && g_planOrig && k.asyncReady ? "OK  " : "PART",
            k.pdUs / 1000, k.pdOwnUs / 1000, k.pdReady, g_planOrig ? "yes" : "NO (non-IC plans not cut)",
            k.asyncReady ? "yes" : "NO (IC plans not cut)");
    } else {
        Log("route deadline off (PlanDeadlineMs=0, PlanDeadlineOwnMs=0)");
    }
    if (!g_fmIni) { Log("fail memo off (FailMemo=0)"); return; }
    if (!k.asyncReady) { Log("FAIL fail memo: the route IC's sync path is not hooked ([RouteAsync] detours), nothing served"); return; }
    k.fmMode = g_fmIni == 1 ? 1 : 0;
    k.fmReady = 1;
    g_fmOn = 1;
    Log("OK   fail memo: sync Plan ICs through ReplanDedupe's repeat check, fmMode %ld (1 serve, 0 observe); world version from "
        "[RouteSnap] counters (turned on for this section)", k.fmMode);
}
