// [RouteSnap] M2LotDump=1 (docs/94 §24.8.4, I-94-381 / I-94-392 / I-94-521): the start lot's quadtree leaf entries near an
// M2/M3 route, clone and live side by side, so the recurring BP in-room pair (plan 2337 / 5849: live 2.4599 / 19 nodes, copy
// 2.5912 / 17) names the input the two sides differ on: a lock-key set (fp+0x20, docs/156 U1), a missing entry (Q leaf) or a
// cell split. Included by p4.cpp after p4door.h. Log-only, default off, at most M2LotDumpMax records (default 16).
//
// Lot: the one whose level-0 tree holds the start point deepest (P4LotAt). Box: both answers' nodes + 1 m (as "obst"), taken
// into the lot's frame by its world -> lot rows. Every Q of the lot (g+0x74, at most 8) is walked from its root (+0x24; bounds
// +4..+0x10, root side +0x38; inner node byte +4 bit 0, children +0xC + 4k with dx = k & 1, dz = k >> 1, docs/98 §3.2) into
// the cells that touch the box; each leaf entry (12 bytes: FME, masks +4 / +8) gives FME +0x10, its footprint's id, class,
// +0x34, lock-key set (pointer, origin of our copy, key counts and hash), the point hash of each valid level and the cell.
// Entries pair by (Q, footprint id, class, FME +0x10, cell); a pair differs by bit: 1 lock keys (NULL vs set, counts, ids),
// 2 key origin (the clone's copy stands for another live set than the live footprint holds now: U1), 4 masks, 8 points,
// 16 +0x34. Unpaired entries are "only live" / "only clone" (a missing entry, or a cell split when both sides list it).
// Box mode: [RouteSnap] M2Box=x0,z0,x1,z1 also dumps every compared plan whose start or goal (x, z) lies in the box, matched
// or not (for plan 5849: 1250.0,1731.3,1251.0,1734.0).
static bool g_p4LdOn;              // [RouteSnap] M2LotDump (default 0)
static bool g_p4LdBoxOn;           // [RouteSnap] M2Box set
static float g_p4LdBox[4];         // x0, z0, x1, z1
static volatile LONG g_p4LdLeft;   // [RouteSnap] M2LotDumpMax: dumps left
static const int kLdEnts = 128, kLdQs = 8, kLdLog = 12;
struct P4LdEnt { unsigned __int64 id; P4Fp pt; P4Key k; DWORD vt, f34, fl, w4, w8, fme, lv; float cell[4]; int q; };
struct P4LdSide { int state, nQ, n, more; unsigned __int64 lot; float lb[4], qb[kLdQs][5]; int nq[kLdQs]; P4LdEnt e[kLdEnts]; P4LaWhy why; };  // state 1 ok, 2 busy, 3 none, 4 no lot (why), -1 fault
static P4LdSide g_p4LdC, g_p4LdL;
static const char* g_p4LdWhy;
static float g_p4LdPt[2];          // the world (x, z) the lot was picked by
static bool g_p4LdRec;             // the next P4Record carries "lotdump"
struct P4LdDiff { int bits, c, l; };  // c / l: entry index on each side, -1 none
static P4LdDiff g_p4LdD[2 * kLdEnts];
static int g_p4LdND;

static void P4LdTree(P4LdSide& s, int q, const u8* n, float x0, float z0, float side, int depth) {
    if (!n || depth > 40) return;
    if (x0 > s.lb[2] || x0 + side < s.lb[0] || z0 > s.lb[3] || z0 + side < s.lb[1]) return;  // the cell misses the box
    if (n[4] & 1) {
        const float h = side * 0.5f;
        for (int k = 0; k < 4; k++) P4LdTree(s, q, RpP(n + 0xC + 4 * k), x0 + (k & 1) * h, z0 + (k >> 1) * h, h, depth + 1);
        return;
    }
    const u8* a = n + 8;
    DWORD cnt = 1;
    if (n[8] & 1) cnt = RpD(n + 0xC), a = (const u8*)(uintptr_t)(RpD(n + 8) & ~1u);
    for (DWORD j = 0; j < cnt && j < 65536; j++) {
        const u8* e = a + j * 12;
        const u8* f = RpP(e);
        if (!f) continue;
        if (s.n == kLdEnts) { s.more++; continue; }
        P4LdEnt& d = s.e[s.n++];
        memset(&d, 0, sizeof(d));
        d.q = q, d.fme = (DWORD)(uintptr_t)f, d.w4 = RpD(e + 4), d.w8 = RpD(e + 8), d.fl = RpD(f + 0x10);
        d.cell[0] = x0, d.cell[1] = z0, d.cell[2] = x0 + side, d.cell[3] = z0 + side;
        d.k.na = true;
        const u8* fp = RpP(f + 8);
        if (!fp) continue;
        d.vt = RpD(fp), d.id = RpD(fp + 0x14) | (unsigned __int64)RpD(fp + 0x18) << 32;
        const RpCls* c = RpClass(d.vt);
        if (!c) continue;
        d.f34 = c->size > 0x34 ? RpD(fp + 0x34) : 0;
        P4KeyOf(fp, c, d.k);
        const DWORD lv = c->cache ? RpLevels(fp) : 0;
        if (lv && lv <= 8 && RpP(fp + 0x38)) d.lv = lv, P4FpPoints(fp, lv, d.pt);
    }
}

static void P4LdWalk(const u8* world, P4LdSide& s) {
    s.n = s.more = s.nQ = 0, s.lot = 0, s.why.lots = 0;
    __try {
        const u8* g = P4LotAt(world, g_p4LdPt[0], g_p4LdPt[1], &s.lot);
        s.why = g_p4LaWhy;
        if (!g) { s.state = 4; return; }
        P4BoxToLot(g, g_p4ObBox, s.lb);
        const u8* qv = RpP(g + 0x74);
        const u8* qe = RpP(g + 0x78);
        for (int i = 0; qv && qv + 4 * (i + 1) <= qe && i < kLdQs; i++) {
            const u8* Q = RpP(qv + 4 * i);
            s.nQ = i + 1, s.nq[i] = 0;
            if (!Q) continue;
            for (int j = 0; j < 4; j++) s.qb[i][j] = *(const float*)(Q + 4 + 4 * j);
            s.qb[i][4] = *(const float*)(Q + 0x38);
            const int n0 = s.n;
            P4LdTree(s, i, RpP(Q + 0x24), s.qb[i][0], s.qb[i][1], s.qb[i][4], 0);
            s.nq[i] = s.n - n0;
        }
        s.state = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s.state = -1; }
}

static bool P4LdPair(const P4LdEnt& a, const P4LdEnt& b) {
    return a.q == b.q && a.id == b.id && a.vt == b.vt && a.fl == b.fl && !memcmp(a.cell, b.cell, sizeof(a.cell));
}
static int P4LdBits(const P4LdEnt& c, const P4LdEnt& l) {
    int b = 0;
    if (P4KeyDiff(c.k, l.k)) b |= 1;
    if (!c.k.na && !l.k.na && c.k.set && c.k.origin != l.k.set) b |= 2;  // the clone's set stands for another live set than live holds now
    if (c.w4 != l.w4 || c.w8 != l.w8) b |= 4;
    if (c.lv != l.lv || c.pt.h != l.pt.h || c.pt.inv != l.pt.inv) b |= 8;
    if (c.f34 != l.f34) b |= 16;
    return b;
}
static void P4LdDiffAll() {
    g_p4LdND = 0;
    if (g_p4LdC.state != 1 || g_p4LdL.state != 1) return;
    bool used[kLdEnts] = {};
    for (int i = 0; i < g_p4LdC.n; i++) {
        int j = 0;
        while (j < g_p4LdL.n && (used[j] || !P4LdPair(g_p4LdC.e[i], g_p4LdL.e[j]))) j++;
        if (j == g_p4LdL.n) { g_p4LdD[g_p4LdND++] = {0, i, -1}; continue; }
        used[j] = true;
        if (const int b = P4LdBits(g_p4LdC.e[i], g_p4LdL.e[j])) g_p4LdD[g_p4LdND++] = {b, i, j};
    }
    for (int j = 0; j < g_p4LdL.n; j++) if (!used[j]) g_p4LdD[g_p4LdND++] = {0, -1, j};
}

static bool P4LdPoint(const DWORD* vec, int idx, float* x, float* z) {
    const u8* b = (const u8*)(uintptr_t)vec[0];
    const u8* e = (const u8*)(uintptr_t)vec[1];
    if (idx < 0 || !b || b + (idx + 1) * 64 > e) return false;
    *x = ((const float*)(b + idx * 64))[0], *z = ((const float*)(b + idx * 64))[2];
    return true;
}
static bool P4LdIn(float x, float z) {
    return x >= g_p4LdBox[0] && z >= g_p4LdBox[1] && x <= g_p4LdBox[2] && z <= g_p4LdBox[3];
}
static void P4LdLog(const P4Job& c);

// worker, P4ShadowRun / the P5 verify while the clone is still held; cls = the compare class (2 / 3 = M2 / M3)
static void P4LdCapture(const P4Job& c, const void* cloneWorld, int cls) {
    const bool m = cls == 2 || cls == 3;
    float sx = 0, sz = 0, gx = 0, gz = 0;
    bool hs = false, hg = false;
    __try {
        hs = P4LdPoint(c.starts, 0, &sx, &sz);
        hg = P4LdPoint(c.goals, g_p4Live.out2 < 0 ? 0 : g_p4Live.out2, &gx, &gz);
    } __except (EXCEPTION_EXECUTE_HANDLER) { hs = hg = false; }
    const bool box = g_p4LdBoxOn && ((hs && P4LdIn(sx, sz)) || (hg && P4LdIn(gx, gz)));
    if (!g_p4LdOn || (!m && !box) || InterlockedDecrement(&g_p4LdLeft) < 0) return;
    Ts3nCounters& k = ts3native_counters;
    g_p4LdWhy = m ? "m2" : "box";
    if (!m) InterlockedIncrement(&k.ldBox);
    bool any = false;  // the "obst" box: both answers' nodes + 1 m (the same box P4ObCapture uses)
    P4ObBoxOf(g_p4Live, &any);
    P4ObBoxOf(g_p4Copy, &any);
    if (!any && hs) g_p4ObBox[0] = g_p4ObBox[2] = sx, g_p4ObBox[1] = g_p4ObBox[3] = sz, any = true;
    if (any) g_p4ObBox[0] -= 1.0f, g_p4ObBox[1] -= 1.0f, g_p4ObBox[2] += 1.0f, g_p4ObBox[3] += 1.0f;
    g_p4LdPt[0] = hs ? sx : (g_p4ObBox[0] + g_p4ObBox[2]) * 0.5f;
    g_p4LdPt[1] = hs ? sz : (g_p4ObBox[1] + g_p4ObBox[3]) * 0.5f;
    P4LdWalk((const u8*)cloneWorld, g_p4LdC);
    g_p4LdL.state = 3, g_p4LdL.n = g_p4LdL.nQ = 0;
    const u8* world = g_spWorldSlot ? *g_spWorldSlot : NULL;
    if (world) {
        static const DWORD kTry = 0;
        void* lk = (void*)(world + 0x918);
        g_p4LdL.state = 2;
        if (g_spAcq(lk, NULL, &kTry) > 0) {  // one try, as the obstacle sets and the door dump: a sim holder keeps its lock
            P4LdWalk(world, g_p4LdL);
            g_spRel(lk, NULL);
        } else InterlockedIncrement(&k.ldBusy);
    }
    if (g_p4LdC.state == 4 || g_p4LdL.state == 4) InterlockedIncrement(&k.ldNoLot);
    P4LdDiffAll();
    InterlockedIncrement(&k.ldDumps);
    InterlockedExchangeAdd(&k.ldEnts, g_p4LdC.n + (g_p4LdL.state == 1 ? g_p4LdL.n : 0));
    InterlockedExchangeAdd(&k.ldDiff, g_p4LdND);
    for (int i = 0; i < g_p4LdND; i++) {
        if (g_p4LdD[i].bits & 1) InterlockedIncrement(&k.ldKeyDiff);
        if (g_p4LdD[i].bits & 2) InterlockedIncrement(&k.ldKeyOrigin);
    }
    g_p4LdRec = true;
    P4LdLog(c);
}

static const char* P4LdSt(int s) { return s == 1 ? "ok" : s == 2 ? "busy" : s == 4 ? "nolot" : s < 0 ? "fault" : "none"; }
static void P4LdLog(const P4Job& c) {
    int only[2] = {}, bit[5] = {};
    for (int i = 0; i < g_p4LdND; i++) {
        const P4LdDiff& d = g_p4LdD[i];
        if (d.c < 0 || d.l < 0) only[d.c < 0 ? 0 : 1]++;
        for (int b = 0; b < 5; b++) bit[b] += (d.bits >> b) & 1;
    }
    Log("P4 lot dump plan %lu (%s): lot 0x%016llX, start (%.3f, %.3f), lot box [%.2f,%.2f,%.2f,%.2f]; clone %s %d entries in %d Qs%s, "
        "live %s %d; %d differ: only live %d, only clone %d, lock keys %d, key origin %d, masks %d, points %d, +0x34 %d",
        c.seq, g_p4LdWhy, g_p4LdC.lot, g_p4LdPt[0], g_p4LdPt[1], g_p4LdC.lb[0], g_p4LdC.lb[1], g_p4LdC.lb[2], g_p4LdC.lb[3],
        P4LdSt(g_p4LdC.state), g_p4LdC.n, g_p4LdC.nQ, g_p4LdC.more || g_p4LdL.more ? " (capped)" : "", P4LdSt(g_p4LdL.state),
        g_p4LdL.n, g_p4LdND, only[0], only[1], bit[0], bit[1], bit[2], bit[3], bit[4]);
    if (g_p4LdC.state == 4) g_p4LaWhy = g_p4LdC.why, P4LaLog(c.seq, "clone");
    if (g_p4LdL.state == 4) g_p4LaWhy = g_p4LdL.why, P4LaLog(c.seq, "live");
    static bool tabC, tabL;  // 0.36.54: the whole lot table once per side (docs/94 §24.12)
    if (!tabC && g_p4LdC.why.lots) tabC = true, P4LaTableLog(c.seq, "clone", g_p4LdC.why);
    if (!tabL && g_p4LdL.why.lots) tabL = true, P4LaTableLog(c.seq, "live", g_p4LdL.why);
    for (int i = 0; i < g_p4LdND && i < kLdLog; i++) {
        const P4LdDiff& d = g_p4LdD[i];
        const P4LdEnt& e = d.c >= 0 ? g_p4LdC.e[d.c] : g_p4LdL.e[d.l];
        const P4LdEnt* l = d.l >= 0 ? &g_p4LdL.e[d.l] : NULL;
        Log("P4 lot dump plan %lu: %s Q %d footprint 0x%016llX vt 0x%08lX FME+0x10 0x%08lX cell [%.3f,%.3f,%.3f,%.3f]: "
            "keys clone %s0x%08lX (origin 0x%08lX, %lu/%lu, 0x%016llX) live %s0x%08lX (%lu/%lu, 0x%016llX); masks clone 0x%08lX/0x%08lX "
            "live 0x%08lX/0x%08lX; points clone 0x%016llX live 0x%016llX",
            c.seq, d.c < 0 ? "only live:" : d.l < 0 ? "only clone:" : "differs (bits)", e.q, e.id, e.vt, e.fl, e.cell[0], e.cell[1],
            e.cell[2], e.cell[3], d.c >= 0 && e.k.na ? "n/a " : "", d.c >= 0 ? e.k.set : 0, d.c >= 0 ? e.k.origin : 0, d.c >= 0 ? e.k.nA : 0,
            d.c >= 0 ? e.k.nB : 0, d.c >= 0 ? e.k.h : 0, l && l->k.na ? "n/a " : "", l ? l->k.set : 0, l ? l->k.nA : 0, l ? l->k.nB : 0,
            l ? l->k.h : 0, d.c >= 0 ? e.w4 : 0, d.c >= 0 ? e.w8 : 0, l ? l->w4 : 0, l ? l->w8 : 0, d.c >= 0 ? e.pt.h : 0, l ? l->pt.h : 0);
        if (d.bits) Log("P4 lot dump plan %lu: entry %d bits 0x%X (1 lock keys, 2 key origin, 4 masks, 8 points, 16 +0x34)", c.seq, i, d.bits);
    }
}

static void P4PutLdEnt(const P4LdEnt& e) {
    P4Put("[%d,\"0x%08lX\",\"0x%016llX\",\"0x%08lX\",%lu,%lu,%lu,%lu,%lu,%lu,\"0x%016llX\",[", e.q, e.fme, e.id, e.vt, e.f34, e.fl,
          e.w4, e.w8, e.lv, e.pt.inv, e.pt.h);
    for (DWORD l = 0; l < e.lv && l < 8; l++) P4Put("%s\"0x%016llX\"", l ? "," : "", e.pt.lh[l]);
    P4Put("],[");
    for (int j = 0; j < 4; j++) { if (j) P4Put(","); P4PutF(e.cell[j]); }
    P4Put("],[");
    for (int j = 0; j < 4; j++) { if (j) P4Put(","); P4PutF(e.lv ? e.pt.b[j] : 0.0f); }
    P4Put("],");
    P4PutKey(e.k);
    P4Put("]");
}
static void P4PutLdSide(const P4LdSide& s) {
    P4Put("{\"state\":\"%s\",\"lot\":\"0x%016llX\",\"lbox\":[", P4LdSt(s.state), s.lot);
    for (int j = 0; j < 4; j++) { if (j) P4Put(","); P4PutF(s.state == 1 ? s.lb[j] : 0.0f); }
    P4Put("],\"qs\":[");
    for (int i = 0; s.state == 1 && i < s.nQ; i++) {
        P4Put(i ? ",[" : "[");
        for (int j = 0; j < 5; j++) { P4PutF(s.qb[i][j]); P4Put(","); }
        P4Put("%d]", s.nq[i]);
    }
    P4Put("],\"more\":%d,\"ents\":[", s.more);
    for (int i = 0; s.state == 1 && i < s.n; i++) { if (i) P4Put(","); P4PutLdEnt(s.e[i]); }
    P4Put("]}");
}
// "lotdump": {why, pt, box (world), clone/live side, diffs [[bits, clone index | -1, live index | -1]]}
//   side: {state, lot, lbox (lot frame), qs [[x0, z0, x1, z1, rootSide, entries]], more, ents [E]}
//   E = [q, "0xFME", "0xID", "0xVT", f34, fmeFlags, mask4, mask8, levels, invalidMask, "0xHASH", ["0xLEVELHASH"...],
//        cell [x0, z0, x1, z1], point bounds [x0, z0, x1, z1], key ["0xSET", "0xORIGIN", nA, nB, "0xKEYHASH"] | null (child class)]
static void P4PutLd() {
    P4Put(",\"lotdump\":{\"why\":\"%s\",\"pt\":[", g_p4LdWhy);
    P4PutF(g_p4LdPt[0]);
    P4Put(",");
    P4PutF(g_p4LdPt[1]);
    P4Put("],\"box\":[");
    for (int j = 0; j < 4; j++) { if (j) P4Put(","); P4PutF(g_p4ObBox[j]); }
    P4Put("],\"clone\":");
    P4PutLdSide(g_p4LdC);
    P4Put(",\"live\":");
    P4PutLdSide(g_p4LdL);
    P4Put(",\"diffs\":[");
    for (int i = 0; i < g_p4LdND; i++) P4Put("%s[%d,%d,%d]", i ? "," : "", g_p4LdD[i].bits, g_p4LdD[i].c, g_p4LdD[i].l);
    P4Put("]}");
}

// [RouteSnap] M2LotDump / M2Box / M2LotDumpMax (from ApplyP4)
static void ApplyP4Ld(const wchar_t* ini) {
    g_p4LdOn = GetPrivateProfileIntW(L"RouteSnap", L"M2LotDump", 0, ini) != 0;
    g_p4LdLeft = Clamp(GetPrivateProfileIntW(L"RouteSnap", L"M2LotDumpMax", 16, ini), 0, 10000);
    wchar_t box[96];
    GetPrivateProfileStringW(L"RouteSnap", L"M2Box", L"", box, 96, ini);
    g_p4LdBoxOn = swscanf_s(box, L"%f,%f,%f,%f", &g_p4LdBox[0], &g_p4LdBox[1], &g_p4LdBox[2], &g_p4LdBox[3]) == 4;
    if (g_p4LdOn)
        Log("route snapshot P4 M2 lot dump on: up to %ld records (M2/M3%s), box %s", g_p4LdLeft,
            g_p4LdBoxOn ? " and starts or goals in the box" : "", g_p4LdBoxOn ? "set" : "none");
}
