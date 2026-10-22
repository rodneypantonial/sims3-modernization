// Route job host-call wait (docs/159), counters v96. Included by ts3native.cpp after cellmemo.cpp, installed after
// [RouteJobPhases] (needs rjpReady) and after P4 / [ProbeValues] (door call chain).
//
// 1. [RouteJobPhases] HostSplit=1 (docs/159 §5, observe only): every managed call the route worker proxies to the sim thread
//    while it runs a job (Proxy -> ServeInvoke) is split into queue (post -> pick-up, per serve site), service (pick-up -> done,
//    nested plans taken out), return (done -> worker awake) and nested plans; queue time that fell inside the frame-limiter
//    window (sim pump return -> Simulate loop-back, simloop.cpp) is summed apart. The door stub below counts asks per key:
//    repeats within a job, their wait, answer changes, and the same key from another job within 1 s.
// 2. [RouteAsync] DoorMemo (docs/159 §4 H2): within one job, a door ask at 0x64F4D3 whose key (P4DoorKey, p4.cpp: lot, Sim and
//    portal owner ids, room pair low words, initialLocked low byte) was already asked returns the first answer; no proxy round
//    trip. Exact-or-fall-through: only the worker inside a job and not inside a plan nested in a managed call; a clone run, a full
//    table or any other thread asks as today. The caller's code after the call (the lot+0x8F clear for 0 / > 3) still runs with the
//    same value. Mode 2 (shadow) asks anyway and counts answer changes (dmMismatch, must stay 0). Switch dmMode (+0x408C, --ab doormemo).
// The door stub sits in the chain like P4's (docs/68 rule 3): behind P4DoorStub when P4 owns the call (its jump slot
// g_p4DoorJmp now names ours), else in [ProbeValues]' jump slot, else on the call's own rel32.

static volatile LONG g_hcOn;       // HostSplit stamps
static volatile LONG g_hcPending;  // a stamped call posted and not picked up yet (read at the limiter's end)
static __declspec(thread) LONG t_hcDoor;  // worker: inside the door stub's own ask (Proxy: not an "other" call)

static LONGLONG HcNow() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static LONG HcUs(LONGLONG ticks) { return (LONG)(ticks * 1000000 / g_qpf.QuadPart); }

// --- limiter window ring: written and read on the sim thread only ---
struct HcSleep { LONGLONG in, out; };
static HcSleep g_hcRing[4];
static int g_hcRingI;
static DWORD g_hcRingTid;
static volatile LONG g_hcRingOn;
static void __cdecl HcLimiterIn() {  // SimPumpAdd: right before IdleSimulationCycle
    if (!g_hcRingOn) return;
    g_hcRingTid = GetCurrentThreadId();
    g_hcRing[g_hcRingI].in = HcNow();
    g_hcRing[g_hcRingI].out = 0;
}
static void __cdecl HcLimiterOut() {  // SimLoopAdd: the loop-back after the limiter
    if (!g_hcRingOn) return;
    HcSleep& s = g_hcRing[g_hcRingI];
    if (!s.in || s.out) return;  // no pump callback this iteration (no sim job record)
    s.out = HcNow();
    g_hcRingI = (g_hcRingI + 1) & 3;
    if (g_hcPending) InterlockedIncrement(&ts3native_counters.hcSleepN);
}
// shortcut: 4 entries = ~90 ms at the measured 43-53 iterations/s; a queue older than that loses its early sleeps (max seen 96 ms)
static LONGLONG HcSleepOverlap(LONGLONG post, LONGLONG pick) {
    if (GetCurrentThreadId() != g_hcRingTid) return 0;
    LONGLONG sum = 0;
    for (int i = 0; i < 4; i++) {
        const HcSleep& s = g_hcRing[i];
        if (!s.in) continue;
        const LONGLONG a = s.in > post ? s.in : post, e0 = s.out ? s.out : pick, e = e0 < pick ? e0 : pick;
        if (e > a) sum += e - a;
    }
    return sum;
}

// --- stamps (Proxy / ServeInvoke in ts3native.cpp) ---
static void HcPost(InvokeJob& v) {
    if (!g_hcOn || !t_hcJob) return;
    v.tPost = HcNow();
    g_hcPending = 1;
}
static void HcPick(InvokeJob& v, int site) {
    Ts3nCounters& k = ts3native_counters;
    v.tPick = HcNow();
    g_hcPending = 0;
    const LONG q = HcUs(v.tPick - v.tPost);
    InterlockedIncrement(&k.hcN[site]);
    InterlockedExchangeAdd(&k.hcQueueUs[site], q);
    Max(&k.hcQueueMaxUs, q);
    const LONGLONG sl = HcSleepOverlap(v.tPost, v.tPick);
    if (sl) InterlockedExchangeAdd(&k.hcQueueSleepUs, HcUs(sl));
}
static void HcWake(InvokeJob& v) {
    Ts3nCounters& k = ts3native_counters;
    if (!v.tPick) return;  // not picked up by a stamped serve (cannot happen: every ServeInvoke stamps)
    const LONGLONG now = HcNow();
    const LONG serve = HcUs(v.tDone - v.tPick - v.nested);
    InterlockedExchangeAdd(&k.hcServeUs, serve);
    Max(&k.hcServeMaxUs, serve);
    InterlockedExchangeAdd(&k.hcReturnUs, HcUs(now - v.tDone));
    InterlockedExchangeAdd(&k.hcNestedUs, HcUs(v.nested));
    if (!t_hcDoor) InterlockedIncrement(&k.hcOtherCalls);
}

// --- door stub at 0x64F4D3 ---
static void* g_hcDoorJmp;            // the next in the chain: 0x791C00, or what P4's / [ProbeValues]' slot held
static volatile DWORD g_hcDoorAns;   // the answer the stub returns (worker only)
struct HcDoorRow { DWORD key[9]; DWORD ans; };
static struct { LONG job; int n; HcDoorRow r[64]; } g_hcTab;  // the running job's first asks (worker only)
struct HcXRow { DWORD key[9]; DWORD ans; LONG job; LONGLONG t; };
static HcXRow g_hcX[256];            // first asks of recent jobs (HostSplit, worker only)
static int g_hcXi;
static volatile LONG g_hcMismatchLog;

static void HcCross(const DWORD* key, DWORD ans, LONGLONG now) {
    Ts3nCounters& k = ts3native_counters;
    const HcXRow* best = NULL;
    for (int i = 0; i < 256; i++) {
        const HcXRow& x = g_hcX[i];
        if (x.job && x.job != t_hcJob && now - x.t <= g_qpf.QuadPart && !memcmp(x.key, key, sizeof(x.key)) && (!best || x.t > best->t))
            best = &x;
    }
    if (best) {
        InterlockedIncrement(&k.hcDoorRepeatXJob);
        if (best->ans != ans) InterlockedIncrement(&k.hcDoorRepeatXJobDiff);
    }
    HcXRow& w = g_hcX[g_hcXi];
    g_hcXi = (g_hcXi + 1) & 255;
    memcpy(w.key, key, sizeof(w.key));
    w.ans = ans, w.job = t_hcJob, w.t = now;
}

// slot = the call's return slot: slot[1..9] = the arguments. 0 = pass through to the next, 1 = return g_hcDoorAns
static int __cdecl HcDoorEnter(DWORD* slot) {
    Ts3nCounters& k = ts3native_counters;
    const LONG mode = k.dmMode, split = g_hcOn;
    if (!mode && !split) return 0;
    if (t_snapRun) { if (mode) InterlockedIncrement(&k.dmFb[3]); return 0; }
    if (!t_hcJob || GetCurrentThreadId() != g_workerTid) { if (mode) InterlockedIncrement(&k.dmFb[0]); return 0; }
    if (g_invJob) { if (mode) InterlockedIncrement(&k.dmFb[1]); return 0; }  // a plan the sim sent back inside a managed call
    DWORD key[9];
    P4DoorKey(slot + 1, key);
    if (g_hcTab.job != t_hcJob) g_hcTab.job = t_hcJob, g_hcTab.n = 0;
    const HcDoorRow* hit = NULL;
    for (int i = 0; i < g_hcTab.n && !hit; i++)
        if (!memcmp(g_hcTab.r[i].key, key, sizeof(key))) hit = &g_hcTab.r[i];
    if (split) {
        InterlockedIncrement(&k.hcDoorQ);
        if (hit) InterlockedIncrement(&k.hcDoorRepeatJob);
    }
    if (hit && mode == 1) {
        InterlockedIncrement(&k.dmHits);
        g_hcDoorAns = hit->ans;
        return 1;
    }
    const LONGLONG t0 = HcNow();
    t_hcDoor = 1;
    const DWORD r = ((P4DoorFn)g_hcDoorJmp)(slot[1], slot[2], slot[3], slot[4], slot[5], slot[6], slot[7], slot[8], slot[9]);
    t_hcDoor = 0;
    const LONGLONG now = HcNow();
    const LONG us = HcUs(now - t0);
    if (split) InterlockedExchangeAdd(&k.hcDoorWaitUs, us);
    if (hit) {
        if (split) {
            InterlockedExchangeAdd(&k.hcDoorRepeatJobWaitUs, us);
            if (r != hit->ans) InterlockedIncrement(&k.hcDoorRepeatDiff);
        }
        if (mode == 2) {
            InterlockedIncrement(&k.dmShadow);
            if (r != hit->ans) {
                InterlockedIncrement(&k.dmMismatch);
                if (InterlockedIncrement(&g_hcMismatchLog) <= 20)
                    Log("door memo shadow: answer %lu, first ask %lu, key %08lX %08lX %08lX %08lX %08lX %08lX %04lX %04lX %02lX (job %ld)",
                        r, hit->ans, key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7], key[8], t_hcJob);
            }
        }
    } else {
        if (mode) InterlockedIncrement(&k.dmMisses);
        if (g_hcTab.n < 64) {
            HcDoorRow& w = g_hcTab.r[g_hcTab.n++];
            memcpy(w.key, key, sizeof(key));
            w.ans = r;
        } else {
            if (split) InterlockedIncrement(&k.hcDoorFull);
            if (mode) InterlockedIncrement(&k.dmFb[2]);
        }
        if (split) HcCross(key, r, now);
    }
    g_hcDoorAns = r;
    return 1;
}

static __declspec(naked) void HcDoorStub() {
    __asm {
        pushad
        pushfd
        lea eax, [esp + 36]
        push eax
        call HcDoorEnter
        add esp, 4
        test eax, eax
        jnz answered
        popfd
        popad
        jmp dword ptr [g_hcDoorJmp]
    answered:
        popfd
        popad
        mov eax, g_hcDoorAns
        ret
    }
}

// 1 = behind P4's stub, 2 = in [ProbeValues]' jump slot, 3 = own call-site redirect, 0 = not installed (bytes differ)
static LONG HcInstallDoor(DWORD at, DWORD target) {
    u8 p4call[5] = {0xE8};
    *(LONG*)(p4call + 1) = (LONG)((DWORD)(uintptr_t)P4DoorStub - (at + 5));
    const bool pvHere = g_pvOn && kPv[PV_QUERY].at == at;
    if (g_p4DoorJmp && ((pvHere && g_pvJmp[PV_QUERY] == (void*)P4DoorStub) || Matches((const void*)(uintptr_t)at, p4call, 5))) {
        g_hcDoorJmp = g_p4DoorJmp;
        InterlockedExchangePointer(&g_p4DoorJmp, (void*)HcDoorStub);
        return 1;
    }
    if (pvHere && g_pvJmp[PV_QUERY] == (void*)(uintptr_t)target) {
        g_hcDoorJmp = g_pvJmp[PV_QUERY];
        InterlockedExchangePointer(&g_pvJmp[PV_QUERY], (void*)HcDoorStub);
        return 2;
    }
    u8 want[5] = {0xE8};
    *(LONG*)(want + 1) = (LONG)(target - (at + 5));
    if (!Matches((const void*)(uintptr_t)at, want, 5)) return 0;
    g_hcDoorJmp = (void*)(uintptr_t)target;
    const LONG rel = (LONG)((DWORD)(uintptr_t)HcDoorStub - (at + 5));
    return Write((u8*)(uintptr_t)at + 1, &rel, 4) ? 3 : 0;
}

static bool ApplyHostCalls(const wchar_t* ini) {
    Ts3nCounters& k = ts3native_counters;
    const bool split = k.rjpReady && GetPrivateProfileIntW(L"RouteJobPhases", L"HostSplit", 0, ini);
    LONG memo = (LONG)GetPrivateProfileIntW(L"RouteAsync", L"DoorMemo", 0, ini);
    if (memo < 0 || memo > 2) { Log("SKIP door memo: DoorMemo=%ld is not 0, 1 or 2", memo); memo = 0; }
    if (!split && !memo) { Log("host-call split and door memo off"); return false; }
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    static const char* kHow[4] = {"", "behind P4's stub", "in [ProbeValues]' slot", "on the call's rel32"};
    const LONG how = HcInstallDoor(P4_DOOR_AT, P4_DOOR_TARGET);
    if (how) k.hcReady |= 2, Log("OK   door stub 0x%08lX -> 0x%08lX %s", P4_DOOR_AT, P4_DOOR_TARGET, kHow[how]);
    else Log("SKIP door stub: 0x%08lX is not `call 0x%08lX` and no known owner; door counters and the memo stay off", P4_DOOR_AT, P4_DOOR_TARGET);
    if (memo && how) {
        k.dmMode = memo;
        Log("OK   door memo: DoorMemo=%ld (%s), worker jobs only (switch dmMode +0x408C)", memo, memo == 1 ? "answer repeats from the job's memo" : "shadow: ask and compare");
    }
    if (split) {
        g_hcOn = 1;
        k.hcReady |= 1;
        if (SimPumpAdd(HcLimiterIn, "host-call split: limiter entry") && SimLoopAdd(HcLimiterOut, "host-call split: limiter exit"))
            g_hcRingOn = 1, k.hcReady |= 4;
        Log("%s host-call split: serve sites, service, return, nested%s", k.hcReady & 4 ? "OK  " : "PART",
            k.hcReady & 4 ? ", limiter window" : " (no limiter window: a sim loop hook is missing)");
    }
    return true;
}
