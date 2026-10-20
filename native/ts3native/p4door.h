// [RouteSnap] DoorDump=1 (docs/94 §22.27, I-94-464 / I-94-499): the door inputs of a P4 shadow, live next to the clone, so the
// recurring BP M2 (game door ...2D9533C0, copy door ...7D22F4D0 on lot 0x6C110033) names the input the two sides differ on.
// Included by p4.cpp after the obstacle sets. Log-only, default off, at most DoorDumpMax records (default 32).
//
// What the door answer reads (Portal_LockState 0x64F2C0, verified disassembly Steam 1.67): the portal's cost +0x2C (>= 1e6 =
// locked), its AGS mask +0x78/+0x7C and variation masks (+0x5C vector, 0x38-byte entries, mask +0x30), the route's
// forbidden list, then, ONLY if the portal owner's lot graph (LotTable_GetIfPresent 0x62FB60 on the search's lot table) has
// byte +0x8F set, the script ask World_FirePortalLockedQuery 0x791C00 (P4DoorStub: recorded live, replayed by key in the
// clone run). An answer of 0 or > 3 clears that +0x8F (0x64F4EE); RouteManager_SetPortalLockStatus 0x74B490 sets it.
// The clone's lot graphs are byte copies made at the lot's last rebase (RpLotShell), and the replay signature (RpSig) and
// the P3 audit both leave +0x8F out, so a clone lot can carry an old +0x8F: with clone 0 / live 1 the clone never asks, keeps
// the native answer (unlocked) and walks a door the live Sim was refused (an M2, never a door-miss) [inferred; this dump].
// 0.35.24 (docs/94 §22.28): RpSig now includes +0x8F, so a flip refreshes the lot shell at the next signature scan.
//
// Per dumped record ("doors" after "copy"/"obst"):
//   ids     the path owner ids the two answers do not share (all of them when they share every one; at most 8)
//   asks    the live run's door asks (P4Door: lot, portal owner, rooms, initialLocked, answer, used by the clone run)
//   f8f     every lot whose +0x8F differs: clone (at the dump) vs live at the plan (the worker holds +0x918 there) vs live now
//   clone / live   lots holding a portal of `ids`: +0x8F, built byte +0x8C, and each such portal (pointer, cost, rooms, AGS
//           mask, variation count). The clone holds live portal objects (H20b), so a different pointer = a replaced portal
//   mirror  ts3fixes' door mirror record of each id (lock type / owner / one-way, docs/105 §10.4.5), when it has one
// Box mode: [RouteSnap] DoorBox=x0,z0,x1,z1 also dumps every compared plan whose goal (x, z) lies in the box, matched or not.
static bool g_p4DdOn;              // [RouteSnap] DoorDump (default 0)
static bool g_p4DdBoxOn;           // [RouteSnap] DoorBox set
static float g_p4DdBox[4];         // x0, z0, x1, z1
static volatile LONG g_p4DdLeft;   // [RouteSnap] DoorDumpMax: dumps left
static const int kDdIds = 8, kDdLotRecs = 8, kDdPorts = 4, kDdDiff = 16, kDdAsks = 96;
struct P4DdPort { unsigned __int64 owner; DWORD ptr, mLo, mHi; float cost; WORD r0, r1; int nVar; };
struct P4DdLotRec { unsigned __int64 key; u8 f8f, f8c; int n; P4DdPort p[kDdPorts]; };
struct P4DdSide { int nAll, nRec, state; P4DdF all[kP4DdLots]; P4DdLotRec rec[kDdLotRecs]; };  // state 1 ok, 2 busy, 3 none, -1 fault
static P4DdSide g_p4DdC, g_p4DdL;
static unsigned __int64 g_p4DdIds[kDdIds];
static int g_p4DdNIds;
static const char* g_p4DdWhy;
static bool g_p4DdRec;             // the next P4Record carries "doors"

// lot table P+0x48 (buckets +0x4C, count +0x50, sentinel buckets[count], chain +0x190, key +0, lot graph node+0x10)
static int P4DdWalk(const u8* world, P4DdF* all, int cap, P4DdSide* s) {
    int n = 0;
    __try {
        const u8* P = world ? RpP(world + 0x95C) : NULL;
        const DWORD* bk = P ? (const DWORD*)(uintptr_t)RpD(P + 0x4C) : NULL;
        const DWORD nb = P ? RpD(P + 0x50) : 0;
        if (!bk || nb > (1u << 20)) return -1;
        const DWORD sentinel = bk[nb];
        for (DWORD i = 0; i < nb; i++)
            for (const u8* nd = (const u8*)(uintptr_t)bk[i]; nd && (DWORD)(uintptr_t)nd != sentinel && n < 4096; nd = RpP(nd + 0x190)) {
                const u8* g = nd + 0x10;
                const P4DdF f = {((unsigned __int64)RpD(nd + 4) << 32) | RpD(nd), g[0x8F], g[0x8C]};
                if (n < cap) all[n] = f;
                n++;
                if (!s) continue;
                P4DdLotRec* r = NULL;
                for (const u8* p = RpP(g + 0x44); p && p < RpP(g + 0x48) && p < RpP(g + 0x44) + 4 * 4096; p += 4) {
                    const u8* pt = RpP(p);
                    if (!pt) continue;
                    const unsigned __int64 own = ((unsigned __int64)RpD(pt + 0xC) << 32) | RpD(pt + 8);
                    bool hit = false;
                    for (int j = 0; j < g_p4DdNIds && !hit; j++) hit = g_p4DdIds[j] == own;
                    if (!hit) continue;
                    if (!r) {
                        if (s->nRec == kDdLotRecs) break;
                        r = &s->rec[s->nRec++];
                        r->key = f.key, r->f8f = f.f8f, r->f8c = f.f8c, r->n = 0;
                    }
                    if (r->n == kDdPorts) break;
                    P4DdPort& q = r->p[r->n++];
                    q.owner = own, q.ptr = (DWORD)(uintptr_t)pt, q.cost = *(const float*)(pt + 0x2C);
                    q.r0 = *(const WORD*)(pt + 0x48), q.r1 = *(const WORD*)(pt + 0x4A);
                    q.mLo = RpD(pt + 0x78), q.mHi = RpD(pt + 0x7C);
                    q.nVar = (int)((RpP(pt + 0x60) - RpP(pt + 0x5C)) / 0x38);
                }
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return n;
}

// worker, right after the live Route_PlanLocked (the job still holds +0x918): every lot's +0x8F as the live search saw it
static void P4DdPlan(P4Job& c, const void* world) {
    c.ddN = g_p4DdOn ? P4DdWalk((const u8*)world, c.dd, kP4DdLots, NULL) : 0;
}

static bool P4DdGoal(const P4Job& c, float* x, float* z) {
    const u8* b = (const u8*)(uintptr_t)c.goals[0];
    const u8* e = (const u8*)(uintptr_t)c.goals[1];
    const int i = g_p4Live.out2 < 0 ? 0 : g_p4Live.out2;
    if (!b || b + (i + 1) * 64 > e) return false;
    *x = ((const float*)(b + i * 64))[0], *z = ((const float*)(b + i * 64))[2];
    return true;
}
static bool P4DdInBox(const P4Job& c) {
    float x, z;
    return g_p4DdBoxOn && P4DdGoal(c, &x, &z) && x >= g_p4DdBox[0] && z >= g_p4DdBox[1] && x <= g_p4DdBox[2] && z <= g_p4DdBox[3];
}

static void P4DdAddId(unsigned __int64 id) {
    if (!id || g_p4DdNIds == kDdIds) return;
    for (int i = 0; i < g_p4DdNIds; i++) if (g_p4DdIds[i] == id) return;
    g_p4DdIds[g_p4DdNIds++] = id;
}
static bool P4DdHas(const P4Ans& a, unsigned __int64 id) {
    for (int i = 0; i < a.nPaths; i++) if (a.p[i].obj == id) return true;
    return false;
}
static void P4DdLog(const P4Job& c);

// worker, P4ShadowRun while the clone is still held; cls = the compare class (2 / 3 = M2 / M3)
static void P4DdCapture(const P4Job& c, const void* cloneWorld, int cls) {
    const bool m = cls == 2 || cls == 3;
    if (!g_p4DdOn || (!m && !P4DdInBox(c)) || InterlockedDecrement(&g_p4DdLeft) < 0) return;
    g_p4DdWhy = m ? "m2" : "box";
    g_p4DdNIds = 0;
    for (int s = 0; s < 2; s++) {  // owners on one side only: the doors the two answers disagree on
        const P4Ans &a = s ? g_p4Copy : g_p4Live, &b = s ? g_p4Live : g_p4Copy;
        for (int i = 0; i < a.nPaths; i++) if (!P4DdHas(b, a.p[i].obj)) P4DdAddId(a.p[i].obj);
    }
    if (!g_p4DdNIds)
        for (int i = 0; i < g_p4Live.nPaths; i++) P4DdAddId(g_p4Live.p[i].obj);
    g_p4DdC.nRec = g_p4DdL.nRec = 0;
    g_p4DdC.nAll = P4DdWalk((const u8*)cloneWorld, g_p4DdC.all, kP4DdLots, &g_p4DdC);
    g_p4DdC.state = g_p4DdC.nAll < 0 ? -1 : 1;
    g_p4DdL.nAll = 0, g_p4DdL.state = 3;
    const u8* world = g_spWorldSlot ? *g_spWorldSlot : NULL;
    if (world) {
        static const DWORD kTry = 0;
        void* lk = (void*)(world + 0x918);
        g_p4DdL.state = 2;
        if (g_spAcq(lk, NULL, &kTry) > 0) {  // one try, as the obstacle sets: a sim holder keeps its lock
            g_p4DdL.nAll = P4DdWalk(world, g_p4DdL.all, kP4DdLots, &g_p4DdL);
            g_spRel(lk, NULL);
            g_p4DdL.state = g_p4DdL.nAll < 0 ? -1 : 1;
        }
    }
    g_p4DdRec = true;
    P4DdLog(c);
}

static int P4DdFind(const P4DdF* a, int n, unsigned __int64 key) {
    for (int i = 0; i < n && i < kP4DdLots; i++) if (a[i].key == key) return a[i].f8f;
    return -1;
}
// the lots whose clone +0x8F differs from live at the plan or live now; returns how many, fills at most kDdDiff
struct P4DdDiff { unsigned __int64 key; int clone, plan, now; };
static int P4DdDiffs(const P4Job& c, P4DdDiff* out) {
    int n = 0;
    for (int i = 0; i < g_p4DdC.nAll && i < kP4DdLots; i++) {
        const P4DdF& f = g_p4DdC.all[i];
        const int plan = P4DdFind(c.dd, c.ddN, f.key), now = g_p4DdL.state == 1 ? P4DdFind(g_p4DdL.all, g_p4DdL.nAll, f.key) : -1;
        if ((plan < 0 || plan == f.f8f) && (now < 0 || now == f.f8f)) continue;
        if (n < kDdDiff) out[n] = {f.key, f.f8f, plan, now};
        n++;
    }
    return n;
}

static void P4DdLog(const P4Job& c) {
    P4DdDiff d[kDdDiff];
    const int nd = P4DdDiffs(c, d);
    int unused = 0;
    for (int i = 0; i < c.nDoors; i++) unused += !c.doors[i].used;
    Log("P4 door dump plan %lu (%s): %d ids, %d asks (%d unused by the clone), clone lots %d, live %s; lots with +0x8F clone != live: %d%s",
        c.seq, g_p4DdWhy, g_p4DdNIds, c.nDoors, unused, g_p4DdC.nAll, g_p4DdL.state == 1 ? "ok" : g_p4DdL.state == 2 ? "busy" : "none",
        nd, nd ? " (first below)" : "");
    for (int i = 0; i < nd && i < 4; i++)
        Log("P4 door dump plan %lu: lot 0x%016llX +0x8F clone %d, live at the plan %d, live now %d", c.seq, d[i].key, d[i].clone,
            d[i].plan, d[i].now);
}

static void P4PutDdSide(const P4DdSide& s) {
    P4Put("[");
    for (int i = 0; i < s.nRec; i++) {
        const P4DdLotRec& r = s.rec[i];
        P4Put("%s{\"lot\":\"0x%016llX\",\"f8f\":%d,\"f8c\":%d,\"portals\":[", i ? "," : "", r.key, r.f8f, r.f8c);
        for (int j = 0; j < r.n; j++) {
            const P4DdPort& q = r.p[j];
            P4Put("%s[\"0x%016llX\",\"0x%08lX\",", j ? "," : "", q.owner, q.ptr);
            P4PutF(q.cost);
            P4Put(",%u,%u,\"0x%08lX%08lX\",%d]", q.r0, q.r1, q.mHi, q.mLo, q.nVar);
        }
        P4Put("]}");
    }
    P4Put("]");
}
static void P4PutDd(const P4Job& c) {
    static const char* const kSt[4] = {"fault", "ok", "busy", "none"};
    P4Put(",\"doors\":{\"why\":\"%s\",\"cloneState\":\"%s\",\"liveState\":\"%s\",\"ids\":[", g_p4DdWhy,
          kSt[g_p4DdC.state < 0 ? 0 : g_p4DdC.state & 3], kSt[g_p4DdL.state < 0 ? 0 : g_p4DdL.state & 3]);
    for (int i = 0; i < g_p4DdNIds; i++) P4Put("%s\"0x%016llX\"", i ? "," : "", g_p4DdIds[i]);
    int unused = 0;
    for (int i = 0; i < c.nDoors; i++) unused += !c.doors[i].used;
    P4Put("],\"asksN\":%d,\"unused\":%d,\"asks\":[", c.nDoors, unused);  // key: lot (a0-1), router (a2-3), portal owner (a4-5)
    for (int i = 0; i < c.nDoors && i < kDdAsks; i++) {
        const P4Door& d = c.doors[i];
        P4Put("%s[\"0x%08lX%08lX\",\"0x%08lX%08lX\",%lu,%lu,%lu,%lu,%ld]", i ? "," : "", d.a[1], d.a[0], d.a[5], d.a[4], d.a[6], d.a[7],
              d.a[8], d.eax, d.used);
    }
    P4DdDiff d[kDdDiff];
    const int nd = P4DdDiffs(c, d);
    P4Put("],\"lots\":{\"plan\":%d,\"clone\":%d,\"live\":%d},\"f8fDiffN\":%d,\"f8fDiff\":[", c.ddN, g_p4DdC.nAll, g_p4DdL.nAll, nd);
    for (int i = 0; i < nd && i < kDdDiff; i++)
        P4Put("%s[\"0x%016llX\",%d,%d,%d]", i ? "," : "", d[i].key, d[i].clone, d[i].plan, d[i].now);
    P4Put("],\"clone\":");
    P4PutDdSide(g_p4DdC);
    P4Put(",\"live\":");
    P4PutDdSide(g_p4DdL);
    P4Put(",\"mirror\":[");  // sim-only table read from the worker: a torn record is possible (diagnostic only)
    int nm = 0;
    __try {
        for (int i = 0; i < g_p4DdNIds; i++)
            for (int l = 0; l < g_spDoorN; l++) {
                const SpDoorLot& dl = g_spDoorLots[l];
                for (int j = 0; dl.d && j < dl.n; j++) {
                    const SpDoor& r = dl.d[j];
                    if (r.obj != g_p4DdIds[i]) continue;
                    P4Put("%s[\"0x%016llX\",\"0x%016llX\",%ld,%d,%lu,%lu,\"0x%016llX\",\"0x%016llX\",%lu,%lu,%lu,%lu]", nm++ ? "," : "",
                          r.obj, dl.key, dl.valid, dl.epoch == g_spEpoch, r.kind, r.lock, r.owner, r.household, r.oneWay, r.roomA,
                          r.roomB, r.listN);
                }
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) { P4Put("%s\"fault\"", nm ? "," : ""); }
    P4Put("]}");
}

// [RouteSnap] DoorDump / DoorBox / DoorDumpMax (from ApplyP4)
static void ApplyP4Dd(const wchar_t* ini) {
    g_p4DdOn = GetPrivateProfileIntW(L"RouteSnap", L"DoorDump", 0, ini) != 0;
    g_p4DdLeft = Clamp(GetPrivateProfileIntW(L"RouteSnap", L"DoorDumpMax", 32, ini), 0, 10000);
    wchar_t box[96];
    GetPrivateProfileStringW(L"RouteSnap", L"DoorBox", L"", box, 96, ini);
    g_p4DdBoxOn = swscanf_s(box, L"%f,%f,%f,%f", &g_p4DdBox[0], &g_p4DdBox[1], &g_p4DdBox[2], &g_p4DdBox[3]) == 4;
    if (g_p4DdOn)
        Log("route snapshot P4 door dump on: up to %ld records (M2/M3%s), box %s", g_p4DdLeft, g_p4DdBoxOn ? " and goals in the box" : "",
            g_p4DdBoxOn ? "set" : "none");
}
