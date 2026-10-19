// [PlanRepeat] Enabled=1 (docs/23 §18, 0.32.10, counters v60), observe only: how often a route plan is asked again with
// identical inputs while nothing it depends on changed, i.e. the upper bound of what an on-thread plan cache could save.
// Included by ts3native.cpp after loadprof.cpp (it reads routesnap.cpp's frames and counters).
//
// Where: inside the Route_PlanLocked 0x82C6E0 detour (PlanLockedBody), around the plan itself, after the original's own
// early-outs. That is the level a cache would sit at: its arguments are the whole request after the IC built the start
// points and drew the goals (docs/92 §6), and its time is the plan time docs/87 measured (P10). The IC is known from
// [RouteSnap]'s route IC frames (kSnIcDef rows 35-38) on the sim thread: class 0-3 = Plan, Replan, PlanFromPoint,
// ReplanFromPosition, 4 = sim thread outside a route IC, 5 = another thread. A plan nested in a plan (a managed call inside
// the search) is part of the outer one's time and is not counted on its own.
// Key (strict, 64-bit FNV): routing world, agent word, doPlan byte, follower ObjectGuid, options O+0x08 (lock keys),
// RouteOption and RouteOption2 (O+0x28..+0x34), the ignore list O+0x38 and forbidden portal list O+0x68 (u64 ids), the
// preferred portal O+0x78, and every start and goal record raw (64 bytes each: position, level, flags, priority and radius
// words alike). Loose key: RouteKey (agent, the four option words, guid, points in 0.5 m cells), the cache's key.
// World version: [RouteSnap] Count's change counters, which this section turns on (observe only, docs/94 §10, §15): every
// writer leaf row except W6 (lazy polygon cache fills) on any thread, plus the 35 routing writer IC slots. "Static" leaves
// out the per-object footprint family (W1, W2, UpdateObjectFootprintCore, private footprints, footprint removes, lot
// footprint removes), which moving Sims write all the time (docs/89). A ring entry holds the version at the END of its
// plan, so the plan's own lazy writes do not count as a change for the next one.
// Result: r (nonzero = success), then per path in the out list its status +0x94, node count and node positions
// (RoutingPathImp +0x98/+0x9C, 48-byte nodes; docs/93 §4.2), so "same result" means the same path to the bit.
// shortcut: the ring keeps 4096 plans; at more than ~136 plans/s the 30 s column loses its oldest entries (prEvictYoung
// counts them, so the column is then a lower bound); grow PR_RING if a run shows it.

static const LONG PR_RING = 4096;  // power of two
static const ULONGLONG kPrWin[3] = {1000, 5000, 30000};
struct PrEntry { u64 key, loose, res; ULONGLONG ms; DWORD ver, sver; };
static PrEntry* g_prRing;
static LONG g_prHead, g_prN;
static SRWLOCK g_prLock = SRWLOCK_INIT;
static volatile LONG g_prOn;
static __declspec(thread) uintptr_t t_prOuter;  // the outer plan's token (a stack address); 0 = no plan of ours open

static bool PrIsFootprint(int leaf) {
    return leaf == SL_W1 || leaf == SL_W2 || leaf == SL_FPCORE || leaf == SL_PRIVATE || leaf == SL_FPREMOVE || leaf == SL_LOTREMOVEFP;
}

static void PrVersions(DWORD* all, DWORD* stat) {
    const Ts3nCounters& k = ts3native_counters;
    DWORD fp = 0, other = 0;
    for (int l = 0; l < SL_N; l++) {
        if (l == SL_W6) continue;  // readers filling lazy polygon caches, not input changes
        const DWORD s = (DWORD)k.snLeaf[l][0] + (DWORD)k.snLeaf[l][1] + (DWORD)k.snLeaf[l][2];
        if (PrIsFootprint(l)) fp += s; else other += s;
    }
    for (int i = 0; i < SN_IC_START; i++) other += (DWORD)k.snIc[i];
    *stat = other;
    *all = other + fp;
}

// u64 or record vector {begin, end} at o+off: count, then the elements' words (at most 256 elements)
static u64 PrVec(u64 h, const u8* o, DWORD off, DWORD elem) {
    const u8* b = *(const u8* const*)(o + off);
    const u8* e = *(const u8* const*)(o + off + 4);
    DWORD n = b && e > b ? (DWORD)(e - b) / elem : 0;
    h = Fnv(h, n);
    if (n > 256) n = 256;
    for (DWORD i = 0; i < n * elem / 4; i++) h = Fnv(h, ((const DWORD*)b)[i]);
    return h;
}

static bool PrKey(const void* world, const int* starts, const int* goals, DWORD agent, DWORD doPlan, const u8* opts,
                  DWORD guidLo, DWORD guidHi, u64* key, u64* loose) {
    __try {
        u64 h = Fnv(0xCBF29CE484222325ull, (DWORD)(uintptr_t)world);
        h = Fnv(Fnv(Fnv(Fnv(h, agent), doPlan & 0xFF), guidLo), guidHi);
        h = Fnv(h, *(const DWORD*)(opts + 0x08));
        for (DWORD off = 0x28; off < 0x38; off += 4) h = Fnv(h, *(const DWORD*)(opts + off));
        h = PrVec(PrVec(h, opts, 0x38, 8), opts, 0x68, 8);
        h = Fnv(Fnv(h, *(const DWORD*)(opts + 0x78)), *(const DWORD*)(opts + 0x7C));
        h = PrVec(PrVec(h, (const u8*)starts, 0, 64), (const u8*)goals, 0, 64);
        *key = h ? h : 1;
        *loose = RouteKey(starts, goals, agent, opts, guidLo, guidHi);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

static bool PrResult(const int* outList, u8 r, u64* out) {
    u64 h = Fnv(0xCBF29CE484222325ull, r);
    __try {
        DWORD n = 0;
        for (const int* node = (const int*)(uintptr_t)outList[0]; node != outList && n < 16; node = (const int*)(uintptr_t)node[0], n++) {
            const u8* path = (const u8*)(uintptr_t)node[2];
            if (!path) { h = Fnv(h, 0); continue; }
            const u8* b = *(const u8* const*)(path + 0x98);
            const u8* e = *(const u8* const*)(path + 0x9C);
            const DWORD cnt = b && e > b ? (DWORD)(e - b) / 48 : 0;
            h = Fnv(Fnv(h, *(const DWORD*)(path + 0x94)), cnt);
            for (DWORD i = 0; i < cnt && i < 1024; i++)
                for (int j = 0; j < 3; j++) h = Fnv(h, ((const DWORD*)(b + i * 48))[j]);
        }
        h = Fnv(h, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    *out = h;
    return true;
}

// sim: the innermost live route IC frame of [RouteSnap] (frames at or below `here` are dead)
static int PrClass(const void* here) {
    if (!RgIsSim(GetCurrentThreadId())) return 5;
    const SnThread& t = t_sn;
    for (int i = t.n - 1; i >= 0; i--)
        if ((const void*)t.f[i].slot > here && t.f[i].src == SRC_IC && t.f[i].idx >= SN_IC_START) return t.f[i].idx - SN_IC_START;
    return 4;
}

// one finished plan against the ring: ver0/sver0 = versions at its start, ver1/sver1 at its end (stored)
static void PrNote(int cls, u64 key, u64 loose, DWORD ver0, DWORD sver0, DWORD ver1, DWORD sver1, ULONGLONG now, u64 res, LONG us) {
    Ts3nCounters& k = ts3native_counters;
    AcquireSRWLockExclusive(&g_prLock);
    const PrEntry *same = NULL, *sameLoose = NULL;
    for (LONG i = 0; i < g_prN && (!same || !sameLoose); i++) {
        const PrEntry& e = g_prRing[(g_prHead - 1 - i) & (PR_RING - 1)];
        if (now - e.ms > kPrWin[2]) break;
        if (!same && e.key == key) same = &e;
        if (!sameLoose && e.loose == loose) sameLoose = &e;
    }
    if (same) {
        const ULONGLONG age = now - same->ms;
        if (same->ver == ver0) {
            for (int w = 0; w < 3; w++)
                if (age <= kPrWin[w]) { k.prExact[cls][w]++; k.prExactUs[cls][w] += us; }
            (same->res == res ? k.prMatch : k.prMismatch)[cls]++;
        } else if (same->sver == sver0) {
            k.prFpOnly[cls]++;
            k.prFpOnlyUs[cls] += us;
            if (same->res == res) k.prFpOnlyMatch[cls]++;
        } else {
            k.prChanged[cls]++;
        }
    }
    if (sameLoose && sameLoose->ver == ver0) { k.prLoose[cls]++; k.prLooseUs[cls] += us; }
    PrEntry& n = g_prRing[g_prHead & (PR_RING - 1)];
    if (g_prN == PR_RING && now - n.ms <= kPrWin[2]) k.prEvictYoung++;
    n.key = key;
    n.loose = loose;
    n.res = res;
    n.ms = now;
    n.ver = ver1;
    n.sver = sver1;
    g_prHead++;
    if (g_prN < PR_RING) g_prN++;
    k.prRing = g_prN;
    k.prVersion = (LONG)ver1;
    ReleaseSRWLockExclusive(&g_prLock);
}

// PlanLockedBody, before the plan: false = not counted (off, a clone or replay run, or nested in a plan of ours)
static bool PrBegin(PrTok& t, const void* world, const int* starts, const int* goals, DWORD agent, DWORD doPlan, const u8* opts,
                    DWORD guidLo, DWORD guidHi) {
    if (!g_prOn || t_snapRun || t_rpReplay) return false;
    const uintptr_t me = (uintptr_t)&t;
    if (t_prOuter) {
        if (me < t_prOuter) return false;  // nested: inside the outer plan's time
        InterlockedIncrement(&ts3native_counters.prUnwound);  // the outer plan left without PrEnd (managed exception)
    }
    t_prOuter = me;
    t.cls = PrClass(&t);
    t.keyed = PrKey(world, starts, goals, agent, doPlan, opts, guidLo, guidHi, &t.key, &t.loose);
    PrVersions(&t.ver, &t.sver);
    QueryPerformanceCounter(&t.t0);
    return true;
}

static void PrEnd(bool on, const PrTok& t, const int* outList, u8 r) {
    if (!on) return;
    t_prOuter = 0;
    const LONG us = UsSince(t.t0);
    Ts3nCounters& k = ts3native_counters;
    InterlockedIncrement(&k.prPlans[t.cls]);
    InterlockedExchangeAdd(&k.prUs[t.cls], us);
    u64 res;
    if (!t.keyed || !PrResult(outList, r, &res)) { InterlockedIncrement(&k.prFault); return; }
    DWORD ver, sver;
    PrVersions(&ver, &sver);
    PrNote(t.cls, t.key, t.loose, t.ver, t.sver, ver, sver, GetTickCount64(), res, us);
}

static void PrLog() {
    const Ts3nCounters& c = ts3native_counters;
    LONG plans = 0, ms = 0, ex = 0, exMs = 0, fp = 0, chg = 0, mis = 0;
    for (int i = 0; i < 6; i++) {
        plans += c.prPlans[i];
        ms += c.prUs[i] / 1000;
        ex += c.prExact[i][2];
        exMs += c.prExactUs[i][2] / 1000;
        fp += c.prFpOnly[i];
        chg += c.prChanged[i];
        mis += c.prMismatch[i];
    }
    Log("plan repeat: plans %ld (%ld ms) exact<=30s %ld (%ld ms) footprints-only %ld world changed %ld mismatches %ld ring %ld evicted<30s %ld",
        plans, ms, ex, exMs, fp, chg, mis, c.prRing, c.prEvictYoung);
}

// [PlanRepeat] Enabled=1: needs the Route_PlanLocked detour (DllMain installs it for this section) and reads [RouteSnap]
// Count's counters (ApplyRouteSnap turns them on for this section); installed after both
static bool ApplyPlanRepeat(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"PlanRepeat", L"Enabled", 0, ini)) { Log("plan repeat probe off (Enabled=0)"); return false; }
    Ts3nCounters& k = ts3native_counters;
    if (!g_planOrig) { Log("SKIP plan repeat probe: Route_PlanLocked detour missing"); return false; }
    if (!(g_prRing = (PrEntry*)calloc(PR_RING, sizeof(PrEntry)))) { Log("FAIL plan repeat probe: ring allocation"); return false; }
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    k.prReady = 1 | ((k.snReady & 5) == 5 ? 2 : 0);
    g_prOn = 1;
    Log("%s plan repeat probe: Route_PlanLocked counted, world version %s (%ld change sites skipped)", k.prReady == 3 ? "OK  " : "PART",
        k.prReady & 2 ? "from the [RouteSnap] counters" : "MISSING (the repeat columns are not valid)", k.snSkipped);
    return true;
}
