// TestEarlyOut ([RouteEarlyOut] probe, docs/142): included by selftest.cpp. A fake R (two chunks), a fake A* container and
// a fake search that floods R like the game's A* (forward refs, mode-2 byte-3 rule, a wall set), sets the closed bits and
// calls back into the hook for its circle rerun.

static u8 g_eoR[0x30], g_eoRec[256], g_eoC[0x80], g_eoBits[40];
static DWORD g_eoTable[2], g_eoCTable[3], g_eoFirst[2];
static signed char g_eoCol[5];
static DWORD g_eoWalls;       // bit per waypoint (chunk 0: 0..2, chunk 1: 3..4): edges into it are walls (modes 1, 3, 4)
static int g_eoFakeCalls, g_eoFakeNested;
static bool g_eoCircle;       // the fake runs a circle pass first when r2 is 0 at entry

static int EoGi(DWORD ref) { return (int)((ref & 0xFFFF) * 3 + (ref >> 16)); }

// record: n, flags (0x40 | type), surface, bx, bz, refs, edge bytes; refs here stay in the row (dz 0)
static u8* EoRec(u8* p, DWORD n, const DWORD* wp, const int* dx, const u8* eb) {
    p[0] = (u8)n, p[1] = 0x40 | 0x10, p[2] = p[3] = p[4] = 0;
    for (DWORD j = 0; j < n; j++) ((unsigned short*)(p + 5))[j] = (unsigned short)(wp[j] | (DWORD)(dx[j] + 3) << 10 | 3u << 13);
    memcpy(p + 5 + 2 * n, eb, n);
    return p + 5 + 3 * n;
}

// chunk 0: w0 -3- w1 (byte 3 both ways), w1 -7- c1w0, w2 alone; chunk 1: w0 -3- w1, w0 -7- c0w1
static void EoFakeWorld() {
    memset(g_eoRec, 0, sizeof(g_eoRec));
    u8* p = g_eoRec;
    g_eoTable[0] = 0;
    ((unsigned short*)p)[0] = 3;
    u8* r = p + 8;
    const DWORD a0[] = {1}; const int d0[] = {0}; const u8 e0[] = {3};
    ((unsigned short*)p)[1] = (unsigned short)(r - p); r = EoRec(r, 1, a0, d0, e0);
    const DWORD a1[] = {0, 0}; const int d1[] = {0, 1}; const u8 e1[] = {3, 7};
    ((unsigned short*)p)[2] = (unsigned short)(r - p); r = EoRec(r, 2, a1, d1, e1);
    ((unsigned short*)p)[3] = (unsigned short)(r - p); r = EoRec(r, 0, NULL, NULL, NULL);
    u8* q = r + (4 - ((r - g_eoRec) & 3)) % 4;
    g_eoTable[1] = (DWORD)(q - g_eoRec);
    ((unsigned short*)q)[0] = 2;
    r = q + 6;
    const DWORD b0[] = {1, 1}; const int f0[] = {0, -1}; const u8 h0[] = {3, 7};
    ((unsigned short*)q)[1] = (unsigned short)(r - q); r = EoRec(r, 2, b0, f0, h0);
    const DWORD b1[] = {0}; const int f1[] = {0}; const u8 h1[] = {3};
    ((unsigned short*)q)[2] = (unsigned short)(r - q); r = EoRec(r, 1, b1, f1, h1);
    memset(g_eoR, 0, sizeof(g_eoR));
    *(DWORD*)g_eoR = 2, *(DWORD*)(g_eoR + 4) = 1;
    *(DWORD**)(g_eoR + 8) = g_eoTable, *(u8**)(g_eoR + 0xC) = g_eoRec, *(DWORD*)(g_eoR + 0x10) = (DWORD)(r - g_eoRec);
    // the game's flood colours: {c0w0, c0w1, c1w0, c1w1} = 0, c0w2 = 1 (chunk 1's first index 3)
    g_eoFirst[0] = 0, g_eoFirst[1] = 3;
    const signed char col[] = {0, 0, 1, 0, 0};
    memcpy(g_eoCol, col, 5);
    g_eoR[0x14] = 1;
    *(DWORD**)(g_eoR + 0x18) = g_eoFirst, *(DWORD**)(g_eoR + 0x1C) = g_eoFirst + 2;
    *(signed char**)(g_eoR + 0x28) = g_eoCol, *(signed char**)(g_eoR + 0x2C) = g_eoCol + 5;
    // container: closed bits 32 bytes of virtual nodes, then 1 byte per chunk; cost sets all 1.0
    memset(g_eoC, 0, sizeof(g_eoC));
    g_eoCTable[0] = 0, g_eoCTable[1] = 32, g_eoCTable[2] = 33;
    *(DWORD*)(g_eoC + 8) = 34, *(DWORD**)(g_eoC + 0xC) = g_eoCTable, *(u8**)(g_eoC + 0x10) = g_eoBits;
    for (int i = 0; i < 18; i++) ((float*)(g_eoC + 0x28))[i] = 1.0f;
}

static const u8* EoRecOf(DWORD ref) {
    const u8* ch = g_eoRec + g_eoTable[ref & 0xFFFF];
    return ch + ((const unsigned short*)(ch + 2))[ref >> 16];
}

static DWORD __fastcall EoFakeAs(u8* c, void* e, const u8* R, const u8* G, const DWORD* s, const DWORD* g, DWORD m, u8* o, DWORD* b) {
    g_eoFakeCalls++;
    float& r2 = *(float*)(o + 0xA0);
    if (r2 == 0.0f && g_eoCircle) {  // circle pass through the detoured entry, as 0x6762B4 does
        r2 = 1.0f;
        g_eoFakeNested++;
        if (EoAsHook(c, e, R, G, s, g, m, o, b)) return 1;
        r2 = 0.0f;
    }
    const bool restricted = r2 != 0.0f;
    memset(g_eoBits, 0, sizeof(g_eoBits));
    DWORD stack[16], n = 0;
    stack[n++] = *s;
    while (n) {
        const DWORD at = stack[--n];
        const DWORD* tab = *(const DWORD* const*)(c + 0xC);
        u8& byte = g_eoBits[tab[(at & 0xFFFF) + 1] + ((at >> 16) >> 3)];
        if (byte & (1 << ((at >> 16) & 7))) continue;
        byte |= (u8)(1 << ((at >> 16) & 7));
        if (at == *g) return 1;
        if (restricted) continue;  // the fake circle reaches only the start
        const u8* rec = EoRecOf(at);
        for (DWORD j = 0; j < rec[0]; j++) {
            const DWORD ref = ((const unsigned short*)(rec + 5))[j];
            const DWORD to = (DWORD)((int)(at & 0xFFFF) + (int)((ref >> 10) & 7) - 3) | (ref & 0x3FF) << 16;
            const u8 eb = rec[5 + 2 * rec[0] + j];
            if (m == 2 ? eb != 3 : (g_eoWalls >> EoGi(to)) & 1) continue;
            stack[n++] = to;
        }
    }
    return 0;
}

static DWORD EoCall(DWORD s, DWORD g, DWORD m, u8* o) {
    DWORD best;
    return EoAsHook(g_eoC, NULL, g_eoR, NULL, &s, &g, m, o, &best);
}
static DWORD EoRef(DWORD chunk, DWORD wp) { return chunk | wp << 16; }

static void TestEarlyOut() {
    static_assert(offsetof(Ts3nCounters, eoMode) == 0x3260 && offsetof(Ts3nCounters, eoProvWhy) == 0x3284 &&
                  offsetof(Ts3nCounters, eoSlowSaved) == 0x32B0, "counters v78 layout (docs/registry.md)");
    Ts3nCounters& k = ts3native_counters;
    QueryPerformanceFrequency(&g_qpf);
    InitializeCriticalSection(&g_eoCs);
    EoFakeWorld();
    g_eoAsOrig = EoFakeAs;
    k.eoMode = 1;
    u8 o[0xC8] = {0};
    EoReq saved;

    // outside a request: pass-through, nothing counted
    g_eoCircle = true;
    assert(EoCall(EoRef(0, 0), EoRef(0, 1), 1, o) == 1 && k.eoProv == 0 && t_eo.as == 0);

    // car proof: byte-3 components {c0w0, c0w1}, {c1w0, c1w1}, {c0w2}; the circle rerun inside is not counted twice
    EoBegin(saved);
    g_eoFakeCalls = g_eoFakeNested = 0;
    assert(EoCall(EoRef(0, 0), EoRef(1, 0), 2, o) == 0);
    assert(t_eo.as == 1 && t_eo.asFail == 1 && t_eo.prov == 1 && t_eo.why[0] == 1 && g_eoFakeCalls == 2 && g_eoFakeNested == 1);
    assert(k.eoLabelBuilds == 1 && g_eoCar.ok);
    assert(EoCall(EoRef(0, 0), EoRef(0, 1), 2, o) == 1 && t_eo.prov == 1);  // same component: no proof, path
    assert(EoCall(EoRef(1, 1), EoRef(1, 0), 2, o) == 1 && t_eo.prov == 1 && k.eoLabelBuilds == 1);  // labels kept per R
    // colour proof (pedestrian, no water flags); with the water flag the colours prove nothing
    assert(EoCall(EoRef(0, 0), EoRef(0, 2), 1, o) == 0 && t_eo.why[1] == 1);
    *(DWORD*)o = 0x80;
    assert(EoCall(EoRef(0, 0), EoRef(0, 2), 1, o) == 0 && t_eo.why[1] == 1);
    *(DWORD*)o = 0;
    EoEnd(saved, 0);
    assert(!t_eo.active && k.eoReqs == 1 && k.eoAs == 5 && k.eoProv == 2 && k.eoProvWhy[0] == 1 && k.eoProvWhy[1] == 1 && k.eoWrong == 0);

    // memo: c1w0 walled; from c0w0 the search reaches {c0w0, c0w1}: goal c1w1 fails (no proof yet: same colour), its closed
    // set is kept; c1w0 is then proven, c0w1 is not (inside the set)
    g_eoWalls = 1u << 3;
    EoBegin(saved);
    assert(EoCall(EoRef(0, 0), EoRef(1, 1), 1, o) == 0 && t_eo.prov == 0 && t_eo.memo);
    assert(EoCall(EoRef(0, 0), EoRef(1, 0), 1, o) == 0 && t_eo.prov == 1 && t_eo.why[2] == 1);
    assert(EoCall(EoRef(0, 0), EoRef(0, 1), 1, o) == 1 && t_eo.prov == 1);
    // the success kept the closed bits of a partial search, but the memo is the copy of the failed one
    assert(EoCall(EoRef(0, 0), EoRef(1, 1), 1, o) == 0 && t_eo.prov == 2);
    // another wall context (an opts dword), another start, another mode: no proof
    o[0x34] = 1;
    assert(EoCall(EoRef(0, 0), EoRef(1, 0), 1, o) == 0 && t_eo.prov == 2);  // and this failure replaces the memo
    o[0x34] = 0;
    assert(EoCall(EoRef(0, 0), EoRef(1, 0), 1, o) == 0 && t_eo.prov == 2);
    assert(EoCall(EoRef(0, 1), EoRef(1, 0), 1, o) == 0 && t_eo.prov == 2);
    assert(EoCall(EoRef(0, 1), EoRef(1, 1), 3, o) == 0 && t_eo.prov == 2);
    // the goal point (+0xB8) and goal ref (+0xB4) are not part of the context
    *(DWORD*)(o + 0xB8) = 7;
    assert(EoCall(EoRef(0, 1), EoRef(1, 1), 3, o) == 0 && t_eo.prov == 3);
    // a wall that opens behind the memo's back: the proof is wrong and counted (the live check of docs/142 §3.3)
    g_eoWalls = 0;
    assert(EoCall(EoRef(0, 1), EoRef(1, 0), 3, o) == 1 && t_eo.wrong == 1 && k.eoWrong == 1);
    EoEnd(saved, 1);
    assert(!t_eo.memo && k.eoWrong == 1);

    // a new request starts without a memo; restricted searches (circle set at entry) and capped ones store none
    g_eoWalls = 1u << 3;
    g_eoCircle = false;
    EoBegin(saved);
    assert(EoCall(EoRef(0, 0), EoRef(1, 0), 1, o) == 0 && t_eo.prov == 0);
    EoEnd(saved, 0);
    EoBegin(saved);
    *(float*)(o + 0xA0) = 4.0f;
    assert(EoCall(EoRef(0, 0), EoRef(1, 1), 1, o) == 0 && !t_eo.memo);
    *(float*)(o + 0xA0) = 0.0f;
    t_budget.capped = 1;
    assert(EoCall(EoRef(0, 0), EoRef(1, 1), 1, o) == 0 && !t_eo.memo);
    t_budget.capped = 0;
    assert(EoCall(EoRef(0, 0), EoRef(1, 1), 1, o) == 0 && t_eo.memo);
    // a restricted search with a memo from an unrestricted one: still proven (it reaches less)
    *(float*)(o + 0xA0) = 4.0f;
    assert(EoCall(EoRef(0, 0), EoRef(1, 0), 1, o) == 0 && t_eo.why[2] == 1);
    *(float*)(o + 0xA0) = 0.0f;
    // a cost set with a zero factor: no memo (SHORT and LONG could give other walls)
    ((float*)(g_eoC + 0x28))[3] = 0.0f;
    assert(EoCall(EoRef(0, 1), EoRef(1, 1), 1, o) == 0 && !t_eo.memo);
    ((float*)(g_eoC + 0x28))[3] = 1.0f;
    // a nested request (a plan inside a managed call) resumes the outer counts and drops its memo
    assert(EoCall(EoRef(0, 0), EoRef(1, 1), 1, o) == 0 && t_eo.memo);
    const LONG asOuter = t_eo.as;
    EoReq inner;
    EoBegin(inner);
    assert(t_eo.as == 0 && !t_eo.memo && t_eo.active);
    assert(EoCall(EoRef(1, 1), EoRef(0, 0), 2, o) == 0 && t_eo.why[0] == 1);
    EoEnd(inner, 0);
    assert(t_eo.active && t_eo.as == asOuter && !t_eo.memo);
    // slow request: logged and counted; the proofs' share decides "saved"
    const LONG logUs = g_eoLogUs, slow0 = k.eoSlow;
    g_eoLogUs = 1;
    EoEnd(saved, 0);
    assert(k.eoSlow == slow0 + 1 && !t_eo.active);
    g_eoLogUs = logUs;

    // mode 0: pass-through inside a request too
    k.eoMode = 0;
    EoBegin(saved);
    assert(EoCall(EoRef(0, 0), EoRef(1, 0), 2, o) == 0 && t_eo.as == 0);
    EoEnd(saved, 0);
    k.eoMode = 1;

    // CallPlan opens the scope only with the detour ready and Mode 1; the plan's searches count inside it
    const LONG reqs0 = k.eoReqs;
    g_planOrig = [](void*, void*, int*, int*, int*, DWORD, DWORD, u8*, int*, int*, DWORD, DWORD) -> u8 {
        u8 oo[0xC8] = {0};
        return (u8)(EoCall(EoRef(0, 0), EoRef(1, 0), 2, oo) | EoCall(EoRef(0, 0), EoRef(0, 1), 2, oo));
    };
    u8 opts[0x38] = {0};
    int list[2];
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 1 && k.eoReqs == reqs0);  // eoReady 0
    k.eoReady = 1;
    const LONG prov0 = k.eoProv, as0 = k.eoAs;
    assert(CallPlan(NULL, NULL, list, NULL, NULL, 0, 1, opts, NULL, NULL, 0, 0) == 1);
    assert(k.eoReqs == reqs0 + 1 && k.eoAs == as0 + 2 && k.eoProv == prov0 + 1 && !t_eo.active);
    k.eoReady = 0;

    // labels refuse an R with a reference outside the chunk table (the proof must cover every edge the search can take)
    u8* farRec = (u8*)EoRecOf(EoRef(0, 0));
    const unsigned short keep = *(unsigned short*)(farRec + 5);
    *(unsigned short*)(farRec + 5) = (unsigned short)(1 | 6u << 10 | 3u << 13);  // dx +3: chunk 3 of 2
    *(DWORD*)(g_eoR + 0x10) += 1;  // another size: rebuild
    assert(EoCarApart(g_eoR, EoRef(0, 0), EoRef(1, 0)) == 0 && !g_eoCar.ok && k.eoLabelBuilds == 2);
    *(unsigned short*)(farRec + 5) = keep;
    *(DWORD*)(g_eoR + 0x10) -= 1;
    assert(EoCarApart(g_eoR, EoRef(0, 0), EoRef(1, 0)) == 1 && g_eoCar.ok && k.eoLabelBuilds == 3);

    k.eoMode = 0;
    k.eoReqs = k.eoAs = k.eoAsFail = k.eoAsMs = k.eoAsMaxUs = k.eoProv = k.eoProvMs = k.eoWrong = k.eoReachMax = 0;
    k.eoProvWhy[0] = k.eoProvWhy[1] = k.eoProvWhy[2] = k.eoLabelBuilds = k.eoLabelUs = 0;
    k.eoSlow = k.eoSlowMs = k.eoSlowAsMs = k.eoSlowProvMs = k.eoSlowSaved = 0;
    g_eoAsOrig = NULL;
    g_planOrig = NULL;
}
