// [RouteEarlyOut] (docs/142, counters v78): probe of a no-path pre-check at the world A* WorldAStar_Search 0x676260.
// Included by ts3native.cpp. Mode 1 never changes a result: every A* call still runs; the probe only asks, before the call,
// whether the search can be proven to fail, and checks the answer against the real result afterwards (eoWrong, expected 0).
//
// The A* (thiscall on the PathContainer W+0x18, 7 stack args, `ret 1Ch`, result in EAX: non-zero = path) expands every
// node it can reach before it reports no path (docs/11 Q1, docs/99 §2.7). Three proofs that the goal is not reachable:
//   car    mode 2 (car only) admits only edge byte 3 and has no other walls that could open an edge: the goal must lie in
//          the start's byte-3 component of R (our own union-find over R's records, built once per R) [docs/99 §10.1]
//   colour modes 1-3 without water/boat context flags (0x80/0x100): the game's own flood colours (R+0x14/+0x18/+0x28,
//          docs/99 §1.8) differ; the planner checks this only for goals in a lot (0x65F781), not for open-ground goals
//   memo   an earlier unrestricted search of the same request with the same start and the same wall context failed, and
//          the goal is not in its closed set (the closed bits after a failed search = every node reachable from the start)
// Per request (one Route_PlanLocked, CallPlan below) it also measures how much of the plan is world A* at all.

typedef DWORD(__fastcall* EoAsFn)(u8* c, void* e, const u8* R, const u8* G, const DWORD* s, const DWORD* g, DWORD m, u8* o, DWORD* b);
static EoAsFn g_eoAsOrig;
static const DWORD WORLD_ASTAR = 0x00676260;
static const u8 kWorldAStarPrologue[] = {0x83, 0xEC, 0x3C, 0x53, 0x8B, 0x5C, 0x24, 0x58};  // sub esp,3Ch; push ebx; mov ebx,[esp+58h]
static LONG g_eoLogUs = 200000;
static DWORD g_eoMinute, g_eoLines, g_eoDropped;

// wall context of one search: everything the edge costs read except the goal waypoint and the circle (docs/142 §3.3)
enum { EO_KEY = 5 + 25 + 2 + 3 };
struct EoReq {
    LONG active, depth;
    LARGE_INTEGER t0;
    LONG as, asFail, asUs, prov, provUs, why[3], wrong;
    bool memo;
    DWORD key[EO_KEY];
    u8* bits;     // closed bits of the failed search (per thread, kept across requests)
    DWORD bitsCap, bitsLen;
};
static __declspec(thread) EoReq t_eo;

static void EoKey(DWORD* k, const u8* c, const u8* R, const u8* G, const DWORD* s, DWORD m, const u8* o) {
    k[0] = (DWORD)(uintptr_t)c, k[1] = (DWORD)(uintptr_t)R, k[2] = (DWORD)(uintptr_t)G, k[3] = m, k[4] = *s;
    memcpy(k + 5, o, 25 * 4);           // +0x00..+0x63: flags, hubs, start/goal lot points, edge-group sets
    memcpy(k + 30, o + 0x88, 2 * 4);    // +0x88 W, +0x8C job block (polygon set)
    memcpy(k + 32, o + 0xA8, 3 * 4);    // +0xA8 start point (+0xB4 goal ref and +0xB8 goal point stay out)
}

// --- car-only components of R (union-find over byte-3 edges) ---
struct EoCar { const u8* rec; DWORD size, w, h, n; bool ok; DWORD* base; int* lab; };
static EoCar g_eoCar;
static CRITICAL_SECTION g_eoCs;

static int EoFind(int* p, int x) {
    while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
    return x;
}
// R: +0 w (the A* reads it as u16), +4 h, +8 chunk offset table, +0xC records, +0x10 size. Chunk: u16 count, u16 recOff[count]
// from the chunk start; record: n, flags (0x40 = edge bytes), surface, bx, bz, u16 ref[n], u8 edge[n]; ref = wp | (dx+3)<<10 |
// (dz+3)<<13, target chunk c + dz*w + dx (docs/99 §1.2, §1.3). A reference the A* would follow outside the table makes the
// labels unusable (ok = false): the proof must cover every edge the search can take.
static bool EoCarBuild(EoCar& e, const u8* R) {
    const DWORD w = *(const unsigned short*)R, h = *(const DWORD*)(R + 4), chunks = w * h;
    const DWORD* table = *(const DWORD* const*)(R + 8);
    const u8* rec = *(const u8* const*)(R + 0xC);
    const DWORD size = *(const DWORD*)(R + 0x10);
    HeapFree(GetProcessHeap(), 0, e.base);
    HeapFree(GetProcessHeap(), 0, e.lab);
    e = {rec, size, w, h, 0, false, NULL, NULL};
    if (!rec || !table || !chunks || chunks > 0x10000) return false;
    e.base = (DWORD*)HeapAlloc(GetProcessHeap(), 0, (chunks + 1) * 4);
    if (!e.base) return false;
    DWORD n = 0;
    for (DWORD c = 0; c < chunks; c++) {
        e.base[c] = n;
        if (table[c] + 2 > size) return false;
        n += *(const unsigned short*)(rec + table[c]);
    }
    e.base[chunks] = n;
    e.n = n;
    e.lab = (int*)HeapAlloc(GetProcessHeap(), 0, (n ? n : 1) * 4);
    if (!e.lab) return false;
    for (DWORD i = 0; i < n; i++) e.lab[i] = (int)i;
    for (DWORD c = 0; c < chunks; c++) {
        const u8* ch = rec + table[c];
        const DWORD cnt = e.base[c + 1] - e.base[c];
        for (DWORD i = 0; i < cnt; i++) {
            const u8* r = ch + ((const unsigned short*)(ch + 2))[i];
            if (r + 5 > rec + size) return false;
            const DWORD nb = r[0];
            if (!(r[1] & 0x40)) continue;  // no edge bytes: every edge is byte 0, a wall in mode 2
            if (r + 5 + 3 * nb > rec + size) return false;
            for (DWORD j = 0; j < nb; j++) {
                if (r[5 + 2 * nb + j] != 3) continue;
                const DWORD ref = ((const unsigned short*)(r + 5))[j];
                const int dx = (int)((ref >> 10) & 7) - 3, dz = (int)((ref >> 13) & 7) - 3;
                const int tc = (int)c + dz * (int)w + dx;
                if (tc < 0 || (DWORD)tc >= chunks || (ref & 0x3FF) >= e.base[tc + 1] - e.base[tc]) return false;
                const int a = EoFind(e.lab, (int)(e.base[c] + i)), b = EoFind(e.lab, (int)(e.base[tc] + (ref & 0x3FF)));
                if (a != b) e.lab[a] = b;
            }
        }
    }
    for (DWORD i = 0; i < n; i++) e.lab[i] = EoFind(e.lab, (int)i);
    e.ok = true;
    return true;
}

// 1 = proven unreachable, 0 = not proven (or no usable labels)
static int EoCarApart(const u8* R, DWORD s, DWORD g) {
    int r = 0;
    EnterCriticalSection(&g_eoCs);
    EoCar& e = g_eoCar;
    const u8* rec = *(const u8* const*)(R + 0xC);
    if (e.rec != rec || e.size != *(const DWORD*)(R + 0x10) || e.w != *(const unsigned short*)R || e.h != *(const DWORD*)(R + 4)) {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        EoCarBuild(e, R);
        InterlockedIncrement(&ts3native_counters.eoLabelBuilds);
        InterlockedExchangeAdd(&ts3native_counters.eoLabelUs, UsSince(t0));
    }
    const DWORD cs = s & 0xFFFF, cg = g & 0xFFFF, ws = s >> 16, wg = g >> 16;
    if (e.ok && cs < e.w * e.h && cg < e.w * e.h && ws < e.base[cs + 1] - e.base[cs] && wg < e.base[cg + 1] - e.base[cg])
        r = e.lab[e.base[cs] + ws] != e.lab[e.base[cg] + wg];
    LeaveCriticalSection(&g_eoCs);
    return r;
}

// the game's flood colour of a waypoint ref (chunk | wp << 16), as the planner reads it at 0x65F781; -2 = none
static int EoColour(const u8* R, DWORD ref) {
    if (!R[0x14]) return -2;
    const DWORD* first = *(const DWORD* const*)(R + 0x18);
    const DWORD* firstEnd = *(const DWORD* const*)(R + 0x1C);
    const signed char* col = *(const signed char* const*)(R + 0x28);
    const signed char* colEnd = *(const signed char* const*)(R + 0x2C);
    const DWORD c = ref & 0xFFFF, i = (first && c < (DWORD)(firstEnd - first)) ? first[c] + (ref >> 16) : 0xFFFFFFFF;
    return col && i < (DWORD)(colEnd - col) ? col[i] : -2;
}

// SHORT (+0x28) and LONG (+0x4C) cost sets of the container: the A* picks one per search from the goal's distance. With every
// factor finite and positive both give the same walls, so a memo from one goal holds for another (docs/142 §3.3)
static bool EoCostSetsPlain(const u8* c) {
    for (int i = 0; i < 18; i++) {
        const float f = ((const float*)(c + 0x28))[i];
        if (!(f > 0.0f && f < 1e6f)) return false;
    }
    return true;
}

// which proof holds before the search: 0 none, 1 car, 2 colour, 3 memo
static int EoProve(EoReq& q, const u8* c, const u8* R, const u8* G, const DWORD* s, const DWORD* g, DWORD m, const u8* o) {
    if ((*s & 0xFFFF) == 0xFFFF || (*g & 0xFFFF) == 0xFFFF || *s == *g || !R) return 0;  // virtual (water) ends: no proof
    if (m == 2 && EoCarApart(R, *s, *g)) return 1;
    const DWORD flags = *(const DWORD*)o;
    if (m >= 1 && m <= 3 && !(flags & 0x180)) {
        const int a = EoColour(R, *s), b = EoColour(R, *g);
        if (a >= 0 && b >= 0 && a != b) return 2;
    }
    if (q.memo) {
        DWORD k[EO_KEY];
        EoKey(k, c, R, G, s, m, o);
        if (!memcmp(k, q.key, sizeof(k))) {
            const DWORD* table = *(const DWORD* const*)(c + 0xC);
            const DWORD i = table[((*g & 0xFFFF) + 1) & 0xFFFF] + ((*g >> 16) >> 3);
            if (i < q.bitsLen && !(q.bits[i] & (1 << ((*g >> 16) & 7)))) return 3;
        }
    }
    return 0;
}

// after a failed unrestricted search: its closed bits are the start's reachable set under this wall context
static void EoMemoStore(EoReq& q, const u8* c, const u8* R, const u8* G, const DWORD* s, DWORD m, const u8* o) {
    q.memo = false;
    const DWORD len = *(const DWORD*)(c + 8);
    const u8* bits = *(const u8* const*)(c + 0x10);
    if (!bits || !len || len > (1u << 20) || !EoCostSetsPlain(c)) return;
    if (q.bitsCap < len) {
        HeapFree(GetProcessHeap(), 0, q.bits);
        q.bits = (u8*)HeapAlloc(GetProcessHeap(), 0, len);
        q.bitsCap = q.bits ? len : 0;
        if (!q.bits) return;
    }
    memcpy(q.bits, bits, len);
    q.bitsLen = len;
    EoKey(q.key, c, R, G, s, m, o);
    q.memo = true;
    LONG nodes = 0;
    for (DWORD i = 0; i < len; i++)
        for (u8 x = bits[i]; x; x &= x - 1) nodes++;
    Max(&ts3native_counters.eoReachMax, nodes);
}

static DWORD __fastcall EoAsHook(u8* c, void* e, const u8* R, const u8* G, const DWORD* s, const DWORD* g, DWORD m, u8* o, DWORD* b) {
    EoReq& q = t_eo;
    // nested: the A*'s own circle rerun (0x6762B4) or a plan inside a managed call; the outer call measures the whole
    if (!q.active || q.depth || ts3native_counters.eoMode != 1) return g_eoAsOrig(c, e, R, G, s, g, m, o, b);
    const bool restricted = *(const float*)(o + 0xA0) != 0.0f;  // circle set at entry: one restricted search, no rerun
    const int why = EoProve(q, c, R, G, s, g, m, o);
    q.depth++;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    const DWORD r = g_eoAsOrig(c, e, R, G, s, g, m, o, b);
    const LONG us = UsSince(t0);
    q.depth--;
    q.as++, q.asUs += us;
    Max(&ts3native_counters.eoAsMaxUs, us);
    if (!r) q.asFail++;
    if (why) {
        q.prov++, q.provUs += us, q.why[why - 1]++;
        if (r) {  // the proof was wrong: the design's correctness argument failed live
            q.wrong++;
            if (InterlockedIncrement(&ts3native_counters.eoWrong) <= 4)
                Log("route early-out: WRONG proof %d: mode %lu start %08lX goal %08lX flags %08lX found a path", why, m, *s, *g, *(const DWORD*)o);
        }
    }
    // a capped search (budget or SyncCapMs) stopped early: its closed set is not the reachable set
    if (!r && !restricted && *(const float*)(o + 0xA0) == 0.0f && !t_budget.capped) EoMemoStore(q, c, R, G, s, m, o);
    return r;
}

// --- request scope (CallPlan): counts per Route_PlanLocked, one log line for a slow one ---
static void EoBegin(EoReq& saved) {
    saved = t_eo;
    EoReq& q = t_eo;
    q.active = 1, q.depth = 0;
    q.as = q.asFail = q.asUs = q.prov = q.provUs = q.wrong = 0;
    q.why[0] = q.why[1] = q.why[2] = 0;
    q.memo = false;
    QueryPerformanceCounter(&q.t0);
}

static void EoEnd(const EoReq& saved, u8 result) {
    EoReq& q = t_eo;
    Ts3nCounters& k = ts3native_counters;
    const LONG us = UsSince(q.t0);
    InterlockedIncrement(&k.eoReqs);
    InterlockedExchangeAdd(&k.eoAs, q.as);
    InterlockedExchangeAdd(&k.eoAsFail, q.asFail);
    InterlockedExchangeAdd(&k.eoAsMs, (q.asUs + 500) / 1000);
    InterlockedExchangeAdd(&k.eoProv, q.prov);
    InterlockedExchangeAdd(&k.eoProvMs, (q.provUs + 500) / 1000);
    for (int i = 0; i < 3; i++) InterlockedExchangeAdd(&k.eoProvWhy[i], q.why[i]);
    if (us >= g_eoLogUs) {
        InterlockedIncrement(&k.eoSlow);
        InterlockedExchangeAdd(&k.eoSlowMs, us / 1000);
        InterlockedExchangeAdd(&k.eoSlowAsMs, q.asUs / 1000);
        InterlockedExchangeAdd(&k.eoSlowProvMs, q.provUs / 1000);
        if (us - q.provUs < g_eoLogUs) InterlockedIncrement(&k.eoSlowSaved);  // the proofs alone would bring it under LogMs
        const DWORD now = GetTickCount();
        if (now - g_eoMinute >= 60000) g_eoMinute = now, g_eoLines = 0;
        if (g_eoLines < 20) {
            g_eoLines++;
            Log("route early-out: plan %ld ms (%s, %s): world A* %ld calls %ld fails %ld ms, provable %ld (car %ld colour %ld memo %ld) "
                "%ld ms, wrong %ld (%lu lines rate-limited before)", us / 1000, GetCurrentThreadId() == g_workerTid ? "worker" : "caller",
                result ? "path" : "no path", q.as, q.asFail, q.asUs / 1000, q.prov, q.why[0], q.why[1], q.why[2], q.provUs / 1000,
                q.wrong, g_eoDropped);
            g_eoDropped = 0;
        } else {
            g_eoDropped++;
        }
    }
    // the outer request (a plan nested in a managed call of another plan on this thread) resumes; its memo is dropped because
    // the nested search reused the closed bits and the buffer
    u8* bits = q.bits;
    const DWORD cap = q.bitsCap;
    q = saved;
    q.bits = bits, q.bitsCap = cap, q.memo = false;
}

// Reads [RouteEarlyOut]; true = install the A* detour (needs the Route_PlanLocked detour for the request scope)
static bool ReadEarlyOut(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"RouteEarlyOut", L"Enabled", 0, ini)) { Log("route early-out disabled (Enabled=0)"); return false; }
    QueryPerformanceFrequency(&g_qpf);
    InitializeCriticalSection(&g_eoCs);
    ts3native_counters.eoMode = Clamp(GetPrivateProfileIntW(L"RouteEarlyOut", L"Mode", 1, ini), 0, 1);
    g_eoLogUs = Clamp(GetPrivateProfileIntW(L"RouteEarlyOut", L"LogMs", 200, ini), 1, 60000) * 1000;
    Log("route early-out settings: Mode=%ld LogMs=%ld", ts3native_counters.eoMode, g_eoLogUs / 1000);
    return true;
}

static void InstallEarlyOut(bool planDetour) {
    if (!planDetour) { Log("FAIL route early-out: no Route_PlanLocked detour, nothing installed"); return; }
    g_eoAsOrig = (EoAsFn)InstallDetour("WorldAStar_Search (early-out probe)", WORLD_ASTAR, kWorldAStarPrologue,
                                       sizeof(kWorldAStarPrologue), (void*)EoAsHook);
    ts3native_counters.eoReady = g_eoAsOrig != NULL;
}

static void EoLogCounters() {
    const Ts3nCounters& c = ts3native_counters;
    Log("route early-out: mode %ld plans %ld A* %ld fails %ld %ld ms max %ld us, provable %ld (car %ld colour %ld memo %ld) %ld ms, "
        "wrong %ld, slow %ld (%ld ms, A* %ld ms, provable %ld ms, saved %ld), reach max %ld, labels %ld (%ld us)", c.eoMode, c.eoReqs,
        c.eoAs, c.eoAsFail, c.eoAsMs, c.eoAsMaxUs, c.eoProv, c.eoProvWhy[0], c.eoProvWhy[1], c.eoProvWhy[2], c.eoProvMs, c.eoWrong,
        c.eoSlow, c.eoSlowMs, c.eoSlowAsMs, c.eoSlowProvMs, c.eoSlowSaved, c.eoReachMax, c.eoLabelBuilds, c.eoLabelUs);
}

// Route_PlanLocked on this thread (RunPlan, the worker): the request scope around the budget wrapper
static u8 CallPlan(void* world, void* edx, int* outList, int* starts, int* goals, DWORD agent, DWORD doPlan, u8* opts,
                   int* out1, int* out2, DWORD guidLo, DWORD guidHi) {
    if (!ts3native_counters.eoReady || ts3native_counters.eoMode != 1)
        return CallPlanBudget(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    EoReq saved;
    EoBegin(saved);
    const u8 r = CallPlanBudget(world, edx, outList, starts, goals, agent, doPlan, opts, out1, out2, guidLo, guidHi);
    EoEnd(saved, r);
    return r;
}
