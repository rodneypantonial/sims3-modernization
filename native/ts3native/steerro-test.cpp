// Offline check for design S2 (docs/84 §3.6). Maps TS3W.exe at its own addresses in this process (all sections; the game's
// code runs read-only on fake data), with only these replaced: the GeneralAllocator entry points (CRT heap), Lock_Acquire /
// Lock_Release (no-ops), 0x85AE60 (copies the node point, level from its w) and World_LotLookup (rectangles). It builds random
// lots, lot tables, quadtrees and footprints with the game's own vtables, then:
//   A. compares the game's Locomotion_ObstacleCheck 0x85FD10 with the read-only clone SrRun (appended ids, in order) and the
//      game's RoutingWorld_ObstacleQuery 0x82EDB0 with the clone's point query (footprint sub-objects, in order);
//   A2. buffer limits (0.21.0): worlds past the 0.13.0 limits must compare equal, past the 0.21.0 ones must give up at the site;
//   B. installs the S2 detour on the mapped 0x85FD10 and checks the hook (job running: clone; trampoline otherwise), shadow mode
//      and every fallback reason;
//   C. installs the 12 worker writer stubs on the mapped W1-W6 prologues (the bytes must match) and runs the game's
//      Footprint_EnsureLevelPolygons 0x80E5B0 through its stub on a "worker" thread: QL is held exclusive inside, released
//      after; while it is held the hook falls back (QL busy) and still answers the same ids;
//   D. checks the lot transforms 0x62ED40 / 0x62EF30 against SrXform bit for bit;
//   E. design L1 (lotfd.cpp, docs/84 §4.6): the game's LotRouting_FrontDoorPortal 0x631800 vs LtFrontDoor on random lot
//      tables (EDX:EAX, *out, portal counts unchanged; AddRef == Release through the T-L3 thunks in vtable 0xFED048), then the
//      L1 detour on the mapped 0x631800 (job: clone, else the game's code), shadow mode, and QL busy -> the game's code;
//   F. tripwires T4 / T-S7 (0.15.0): the 11 stubs on the game's prologues; the game's 0x807110 (another thread) and 0x61ED10
//      (worker outside QL) run through them and block inside: the hook drops the clone, the game's call gives the same ids.
//   F2. arm / disarm races (0.18.0): disarmed site not counted; writer gone before the arm; writer entering after the arm
//      but before A_RUNNING; the job ending (and the next arming) inside the clone with a writer in the disarmed site.
//   G. fix D-X; H. design H2 (see the sections);
//   J. [ObstructedRO] (0.22.1): the game's IC_Route_IsPointObstructedBySim 0x751480 vs OrRun (fakes for its phase-1 callees
//      0x74B2C0, 0xADD860, 0xB015B0, 0x74A8F0 and the route's vtable pair), then its detour: job, shadow, QL busy.
// Built by build.cmd, not run there (needs the exe):  out\steerro-test.exe ["<path>\TS3W.exe"] [cases, default 400] [seed]
#define TS3NATIVE_NO_DLLMAIN
#include "ts3native.cpp"
#include <assert.h>

static DWORD g_seed = 12345;
static DWORD Rnd(DWORD n) { g_seed = g_seed * 1103515245 + 12345; return ((g_seed >> 8) & 0xFFFFFF) % n; }
static float Rf(float lo, float hi) { return lo + (hi - lo) * (float)Rnd(1 << 20) / (float)(1 << 20); }

static const DWORD IMG_BASE = 0x00400000, IMG_HI = 0x0125A000;  // SizeOfImage 0xE5A000 (Steam 1.67)
static const DWORD IMG_LO = IMG_BASE;

// Windows maps small regions (NLS, apiset) at the lowest free addresses during process start, i.e. at 0x400000 when the exe
// sits elsewhere. So the parent starts this program suspended, reserves the image range in the child first, then resumes it.
static int RunChild() {
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    SetEnvironmentVariableA("SRT_CHILD", "1");
    if (!CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) return 2;
    if (VirtualAllocEx(pi.hProcess, (void*)(uintptr_t)IMG_LO, IMG_HI - IMG_LO, MEM_RESERVE, PAGE_EXECUTE_READWRITE) != (void*)(uintptr_t)IMG_LO) {
        printf("cannot reserve 0x%08lX-0x%08lX in the child (err %lu)\n", IMG_LO, IMG_HI, GetLastError());
        TerminateProcess(pi.hProcess, 2);
        return 2;
    }
    ResumeThread(pi.hThread);
    DWORD rc = 2;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &rc);
    return (int)rc;
}

static bool Reserve() {
    u8* r = (u8*)VirtualAlloc((void*)(uintptr_t)IMG_LO, IMG_HI - IMG_LO, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (r == (u8*)(uintptr_t)IMG_LO) return true;
    MEMORY_BASIC_INFORMATION m = {};
    VirtualQuery((void*)(uintptr_t)IMG_LO, &m, sizeof(m));
    printf("cannot map the image at 0x%08lX (err %lu): region at %p size 0x%lX\n", IMG_LO, GetLastError(), m.AllocationBase, (DWORD)m.RegionSize);
    return false;
}

static void __cdecl TrapReport(DWORD from) {
    printf("unexpected import call from 0x%08lX\n", from);
    fflush(stdout);
    ExitProcess(3);
}
static void __declspec(naked) ImportTrap() {
    __asm {
        push dword ptr [esp]
        call TrapReport
    }
}

static bool MapImage(const u8* img) {
    const DWORD pe = *(const DWORD*)(img + 0x3C), base = *(const DWORD*)(img + pe + 0x34);
    const WORD ns = *(const WORD*)(img + pe + 6), oh = *(const WORD*)(img + pe + 20);
    if (base != IMG_BASE || *(const DWORD*)(img + pe + 0x50) != IMG_HI - IMG_BASE) { printf("not the Steam 1.67 exe\n"); return false; }
    for (int i = 0; i < ns; i++) {
        const u8* s = img + pe + 24 + oh + i * 40;
        const DWORD rs = *(const DWORD*)(s + 16), vs = *(const DWORD*)(s + 8);
        DWORD va = base + *(const DWORD*)(s + 12), raw = *(const DWORD*)(s + 20), n = rs < vs ? rs : vs;
        if (va < IMG_LO) { const DWORD cut = IMG_LO - va < n ? IMG_LO - va : n; va += cut, raw += cut, n -= cut; }
        memcpy((void*)(uintptr_t)va, img + raw, n);
    }
    // imports: memmove/memcpy/memset/memcmp from the CRT, anything else stops the test with its caller
    const DWORD imp = base + *(const DWORD*)(img + pe + 0x80);
    for (const DWORD* d = (const DWORD*)(uintptr_t)imp; d[3]; d += 5) {
        DWORD* iat = (DWORD*)(uintptr_t)(base + d[4]);
        const DWORD* names = (const DWORD*)(uintptr_t)(base + (d[0] ? d[0] : d[4]));
        for (int j = 0; names[j]; j++) {
            const char* nm = (names[j] & 0x80000000) ? "" : (const char*)(uintptr_t)(base + names[j] + 2);
            void* f = (void*)ImportTrap;
            if (!strcmp(nm, "memmove")) f = (void*)memmove;
            else if (!strcmp(nm, "memcpy")) f = (void*)memcpy;
            else if (!strcmp(nm, "memset")) f = (void*)memset;
            else if (!strcmp(nm, "memcmp")) f = (void*)memcmp;
            else if (!strcmp(nm, "ceil")) f = (void*)(double(__cdecl*)(double))ceil;  // hash table growth
            else if (!strcmp(nm, "floor")) f = (void*)(double(__cdecl*)(double))floor;
            iat[j] = (DWORD)(uintptr_t)f;
        }
    }
    return true;
}

static void Jmp(DWORD at, const void* to) {
    u8* p = (u8*)(uintptr_t)at;
    p[0] = 0xE9;
    *(LONG*)(p + 1) = (LONG)((const u8*)to - (p + 5));
}

// --- fakes for the replaced game functions ---
static void* __fastcall FkMalloc(void*, void*, DWORD size, DWORD) { return _aligned_malloc(size ? size : 1, 16); }
static void* __fastcall FkMemalign(void*, void*, DWORD size, DWORD align, DWORD, DWORD) { return _aligned_malloc(size ? size : 1, align > 16 ? align : 16); }
static void __fastcall FkFree(void*, void*, void* p) { _aligned_free(p); }
static LONG __fastcall FkLock(void*, void*, const DWORD*) { return 1; }
static LONG __fastcall FkUnlock(void*, void*) { return 0; }
static int g_convCalls;
static void __cdecl FkConv(const float* src, u8* out) {
    g_convCalls++;
    float* o = (float*)out;
    o[0] = src[0], o[1] = src[1], o[2] = src[2], o[3] = 1.0f;
    o[4] = o[5] = o[6] = o[7] = 0;
    o[8] = 1.0f;
    *(LONG*)(out + 0x24) = (LONG)src[3];
    *(DWORD*)(out + 0x28) = *(DWORD*)(out + 0x2C) = *(DWORD*)(out + 0x30) = 0;
}
struct LotRect { u8* lot; float x0, z0, x1, z1; };
static LotRect g_rects[4];
static int g_nRects;
static u8* __fastcall FkLotLookup(void*, void*, float x, float z) {
    for (int i = 0; i < g_nRects; i++)
        if (x >= g_rects[i].x0 && x < g_rects[i].x1 && z >= g_rects[i].z0 && z < g_rects[i].z1) return g_rects[i].lot;
    return NULL;
}

static long g_twAfter;  // F: read-only answers once the tripwire writer had left
static void* A(size_t n) { void* p = _aligned_malloc(n, 16); memset(p, 0, n); return p; }

// --- the fake world ---
static const DWORD kClasses[9] = {0xFEBBC0, 0xFEB848, 0xFEAE08, 0xFEBA98, 0xFEBE38, 0xFEB5F8, 0xFEB720, 0xFEBD10, 0xFEB970};
static DWORD g_subVt[2];  // sub-object vtables: [0] type via +0x35 (0x624ED0), [1] constant 0xA (0x620E50)
static const DWORD kSentinel = 0x05E117E1;

struct World {
    u8* world; u8* wm; u8* p; u8* act;
    u8* lots[3];       // [0], [1] in the table, [2] not
    u8* entries[2];
    u8* qs[2][2];      // per lot entry, two levels
    u8* fps[2400];  // A2 adds its big leaf's footprints after the random ones
    int nfp;
};
static World W;

static u8* NewFootprint(u8* cachePolys, int nPolys) {
    u8* f = (u8*)A(0x80);
    const DWORD vt = kClasses[Rnd(9)];
    *(DWORD*)f = vt;
    const bool constType = *(const DWORD*)(uintptr_t)(vt + 0xC) == 0x0070AE20;
    *(DWORD*)(f + 4) = g_subVt[constType ? 1 : 0];
    *(LONG*)(f + 8) = 100;  // references: never reach 0 (the deleting destructor is not ours to run)
    *(DWORD*)(f + 0x14) = 0x1000 + Rnd(40);  // ids collide on purpose (ids, ignore and exclude lists)
    *(DWORD*)(f + 0x18) = Rnd(3) ? 0 : 7;
    if (Rnd(4) == 0) *(DWORD*)(f + 0x14) = *(DWORD*)(f + 0x18) = 0;  // id 0: the obstacle query skips it (record id), EditPath not
    static const u8 kTypes[] = {0x14, 0x1F, 9, 0x11, 0xC, 0xD, 0x17, 0x13, 0x18, 0x1E, 2, 5, 0xA, 3, 3, 3, 4, 4};
    f[0x34] = (u8)(Rnd(5) == 0 ? 0xD : Rnd(2) ? 3 : Rnd(4));  // class byte (3 passes EditPath's class filter)
    f[0x35] = kTypes[Rnd(sizeof(kTypes))];
    *(DWORD*)(f + 0x40) = Rnd(4) == 0 ? 0 : Rnd(1000);  // 0x80F840 compares +0x40/+0x44 with the record's id (0)
    *(DWORD*)(f + 0x44) = 0;
    f[0x48] = (u8)Rnd(2);  // 0x620DD0
    u8* cache = (u8*)A(0x24 * 3);
    *(u8**)(cache + 0) = cachePolys;
    *(u8**)(cache + 4) = cachePolys + nPolys * 16;
    cache[0x20] = 1;
    *(u8**)(f + 0x38) = cache;
    return f;
}

static u8* Polys(int* n) {  // 1-2 quads somewhere in the 64 x 64 lot
    *n = 1 + Rnd(2);
    u8* p = (u8*)A(16 * *n);
    for (int i = 0; i < *n; i++) {
        float* v = (float*)A(4 * 8);
        const bool big = Rnd(2) == 0;  // half of them cover most of the lot, so the level-0 test passes often
        const float x = big ? Rf(-8, 8) : Rf(0, 60), y = big ? Rf(-8, 8) : Rf(0, 60), w = big ? Rf(50, 80) : Rf(1, 30), h = big ? Rf(50, 80) : Rf(1, 30);
        v[0] = x, v[1] = y, v[2] = x + w, v[3] = y, v[4] = x + w, v[5] = y + h, v[6] = x, v[7] = y + h;
        *(float**)(p + i * 16) = v;
        *(float**)(p + i * 16 + 4) = v + 8;
    }
    return p;
}

static u8* NewEntry(u8* fp) {
    u8* e = (u8*)A(0x20);
    *(DWORD*)e = 0x00FEAC7C;
    *(u8**)(e + 8) = fp;
    e[0x10] = (u8)Rnd(4);
    return e;
}

static u8* NewLeaf() {
    u8* n = (u8*)A(0x20);
    n[4] = (u8)(Rnd(4) == 0 ? 2 : 0);  // kind 0 leaf, sometimes 2
    const int cnt = Rnd(5) == 0 ? 1 : Rnd(9);
    if (cnt == 1 && Rnd(2)) {  // inline entry (bit 0 clear)
        u8* fp = W.fps[Rnd(W.nfp)];
        *(u8**)(n + 8) = NewEntry(fp);
        *(DWORD*)(n + 0xC) = Rnd(0xFFFFFFFF) | Rnd(0xFFFF) << 16;
        *(DWORD*)(n + 0x10) = Rnd(0xFFFFFF) << 8;
        return n;
    }
    u8* list = (u8*)A(12 * (cnt ? cnt : 1));
    for (int i = 0; i < cnt; i++) {
        *(u8**)(list + i * 12) = Rnd(12) ? NewEntry(W.fps[Rnd(W.nfp)]) : NULL;
        *(DWORD*)(list + i * 12 + 4) = Rnd(2) ? Rnd(0xFFFFFF) * 251 : 0xFFFFFFFF;
        *(DWORD*)(list + i * 12 + 8) = Rnd(2) ? Rnd(0xFFFFFF) * 251 : 0xFFFFFFFF;
    }
    *(DWORD*)(n + 8) = (DWORD)(uintptr_t)list | 1;
    *(DWORD*)(n + 0xC) = cnt;
    return n;
}

static u8* NewTree(int depth) {
    if (depth >= 2 || Rnd(4) == 0) return NewLeaf();
    u8* n = (u8*)A(0x20);
    n[4] = 1;
    for (int i = 0; i < 4; i++) *(u8**)(n + 0xC + i * 4) = Rnd(5) ? NewTree(depth + 1) : NULL;
    return n;
}

static u8* NewQ() {
    u8* q = (u8*)A(0x60);
    *(float*)(q + 4) = 0, *(float*)(q + 8) = 0;
    *(u8**)(q + 0x24) = NewTree(0);
    *(float*)(q + 0x38) = 64.0f;
    *(LONG*)(q + 0x44) = (LONG)Rnd(3);
    *(LONG*)(q + 0x48) = (LONG)Rnd(3);  // 0: QueryPoint calls the flush, which returns at once (flag 0)
    return q;
}

// a lot table node: key at +0, entry at +0x10 (key again), level base +0x40, Q vector +0x74/+0x78, rows +0xA0..+0xD0
static u8* NewLotNode(DWORD lo, DWORD hi, u8* q0, u8* q1, float ang, float tx, float tz) {
    u8* node = (u8*)A(0x200);
    *(DWORD*)node = lo, *(DWORD*)(node + 4) = hi;
    u8* e = node + 0x10;
    *(DWORD*)e = lo, *(DWORD*)(e + 4) = hi;
    *(LONG*)(e + 0x40) = 0;
    u8** qv = (u8**)A(8);
    qv[0] = q0, qv[1] = q1;
    *(u8***)(e + 0x74) = qv;
    *(u8***)(e + 0x78) = qv + 2;
    float* r = (float*)(e + 0xA0);
    const float c = cosf(ang), s = sinf(ang);
    r[0] = c, r[1] = 0, r[2] = s, r[3] = 0;
    r[4] = 0, r[5] = 1, r[6] = 0, r[7] = 0;
    r[8] = -s, r[9] = 0, r[10] = c, r[11] = 0;
    r[12] = tx, r[13] = 0, r[14] = tz, r[15] = 1;
    return node;
}

static void BuildWorld() {
    memset(&W, 0, sizeof(W));
    W.world = (u8*)A(0xA00);
    W.wm = (u8*)A(0x200);
    W.p = (u8*)A(0x200);
    W.act = (u8*)A(0x60);
    W.world[0x974] = 1;
    *(u8**)(W.world + 0x95C) = W.p;
    *(u8**)(W.world + 0x964) = W.wm;
    *(u8**)(W.wm + 0xB0) = W.act;
    *(DWORD*)(W.act + 0x48) = 0x99, *(DWORD*)(W.act + 0x4C) = 0;
    *(u8**)(uintptr_t)0x011D9128 = W.world;  // RoutingWorld_Get 0x8292D0 -> 0x836200 reads it
    *(u8**)(uintptr_t)0x011ECBC4 = W.wm;
    W.nfp = 24 + Rnd(60);
    for (int i = 0; i < W.nfp; i++) {
        int np;
        u8* polys = Polys(&np);
        W.fps[i] = NewFootprint(polys, np);
    }
    // two lots in the table (one of them sometimes the active lot), one that is not
    for (int i = 0; i < 3; i++) {
        W.lots[i] = (u8*)A(0x420);  // the IC 0x751480 reads lot +0x408/+0x40C (section I)
        *(DWORD*)(W.lots[i] + 0x48) = 0x10 + i;
        *(DWORD*)(W.lots[i] + 0x4C) = 0;
    }
    if (Rnd(6) == 0) *(DWORD*)(W.act + 0x48) = 0x11;  // lot 1 is the active lot: Contains says no
    const DWORD cnt = 1 + Rnd(3);
    DWORD* bk = (DWORD*)A((cnt + 1) * 4);
    bk[cnt] = kSentinel;
    for (int i = 0; i < 2; i++) {
        W.qs[i][0] = NewQ();
        W.qs[i][1] = Rnd(3) ? NewQ() : NULL;
        u8* node = NewLotNode(0x10 + i, 0, W.qs[i][0], W.qs[i][1], Rf(-3.1f, 3.1f), Rf(-5, 5), Rf(-5, 5));
        W.entries[i] = node + 0x10;
        const DWORD b = (0x10 + i) % cnt;  // chain: insert at the head
        *(DWORD*)(node + 0x190) = bk[b];
        bk[b] = (DWORD)(uintptr_t)node;
    }
    *(DWORD**)(W.p + 0x4C) = bk;
    *(DWORD*)(W.p + 0x50) = cnt;
    g_nRects = 3;
    g_rects[0] = {W.lots[0], -100, -100, 0, 100};
    g_rects[1] = {W.lots[1], 0, -100, 100, 100};
    g_rects[2] = {W.lots[2], 100, -100, 200, 100};
}

// A2 (0.21.0): buffer limits. One leaf with g_big.objs footprints (unique ids, a covering quad) replaces level 0 of both lots;
// g_big.cls2 of them have class byte 2, and one has type 0x1F (so the segment gather keeps the class-2 ones); agents get
// g_big.nodes flagged nodes in one lot
struct Big { int objs, cls2, nodes; bool noRadius; };
static Big g_big;
static void BigLeaf() {
    const int cnt = g_big.objs;
    u8* list = (u8*)A(12 * cnt);
    for (int i = 0; i < cnt; i++) {
        u8* p = (u8*)A(16);
        float* v = (float*)A(4 * 8);
        v[0] = -8, v[1] = -8, v[2] = 72, v[3] = -8, v[4] = 72, v[5] = 72, v[6] = -8, v[7] = 72;
        *(float**)p = v;
        *(float**)(p + 4) = v + 8;
        u8* f = NewFootprint(p, 1);
        *(DWORD*)(f + 0x14) = 0x100000 + i;  // unique: every footprint can add an id
        *(DWORD*)(f + 0x18) = 0;
        f[0x34] = i < g_big.cls2 ? 2 : 3;
        f[0x35] = i == cnt - 1 ? 0x1F : 3;
        f[0x48] = 0;
        W.fps[W.nfp++] = f;
        *(u8**)(list + i * 12) = NewEntry(f);
        *(DWORD*)(list + i * 12 + 4) = *(DWORD*)(list + i * 12 + 8) = 0xFFFFFFFF;
    }
    u8* n = (u8*)A(0x20);
    *(DWORD*)(n + 8) = (DWORD)(uintptr_t)list | 1;
    *(DWORD*)(n + 0xC) = cnt;
    for (int i = 0; i < 2; i++) *(u8**)(W.qs[i][0] + 0x24) = n;
}

// local (u, v) in lot i -> world (x, z): the inverse of the rows (rotation by ang, translation)
static void WorldPoint(int lot, float u, float v, float* x, float* z) {
    const float* r = (const float*)(W.entries[lot] + 0xA0);
    const float c = r[0], s = r[2], tx = r[12], tz = r[14];
    // u = x c - z s + tx, v = x s + z c + tz
    const float du = u - tx, dv = v - tz;
    *x = c * du + s * dv;
    *z = -s * du + c * dv;
}

struct Agent { u8* a; u8* nodes[80]; int nn; };
static Agent NewAgent(bool otherLot) {
    Agent ag = {};
    ag.a = (u8*)A(0x100);
    u8* head = ag.a + 8;
    ag.nn = g_big.nodes ? g_big.nodes : 1 + Rnd(5);
    u8* prev = head;
    const int lot = Rnd(2);
    for (int i = 0; i < ag.nn; i++) {
        u8* n = (u8*)A(0x40);
        ag.nodes[i] = n;
        *(u8**)prev = n;
        prev = n;
        float x, z;
        WorldPoint(lot, Rf(-4, 68), Rf(-4, 68), &x, &z);
        if (lot == 0 && x >= 0) x = -0.5f;  // keep it in lot 0's rectangle
        if (lot == 1 && x < 0) x = 0.5f;
        if (otherLot && Rnd(2)) x += lot == 0 ? 50 : 150;  // lot 2 (not in the table) or lot 1 seen from lot 0's frame
        float* p = (float*)(n + 0x10);
        p[0] = x, p[1] = Rf(0, 2), p[2] = z, p[3] = (float)(Rnd(5) == 0 ? 1 : 0);
        n[0x30] = (u8)(g_big.nodes || Rnd(3) != 0);
    }
    *(u8**)prev = head;
    *(float*)(ag.a + 0x40) = Rnd(4) && !g_big.noRadius ? Rf(0.2f, 6) : -1.0f;  // -1: the obstacle query finds nothing, so nothing is excluded
    *(DWORD*)(ag.a + 0xB8) = Rnd(0x10000);
    return ag;
}

struct IdVec { DWORD* b; DWORD* e; DWORD* c; };
static void NewIds(IdVec* v, int n) {
    v->b = (DWORD*)_aligned_malloc(8 * (n + 1), 16);  // growth goes through the game's allocator (our fake)
    v->e = v->b;
    v->c = v->b + 2 * (n + 1);
    for (int i = 0; i < n; i++) { *v->e++ = 0x1000 + Rnd(40); *v->e++ = 0; }
}

typedef void(__fastcall* GameLocoFn)(u8* agent, void* edx, IdVec* ids, const DWORD* ign1, DWORD n1, const DWORD* ign2, DWORD n2);

static int g_dbgOnce;
static void Dump(const char* what, const DWORD* a, int na, const DWORD* b, int nb) {
    if (g_dbgOnce++ > 3) return;
    printf("%s\n  game :", what);
    for (int i = 0; i < na; i++) printf(" %lX", a[i * 2]);
    printf("\n  clone:");
    for (int i = 0; i < nb; i++) printf(" %lX", b[i * 2]);
    printf("\n");
}

// one case: the game's 0x85FD10 vs SrRun; returns the clone's fallback reason (0 = compared)
static long g_cases, g_withIds, g_idsTotal, g_withExcl, g_fbCases[9], g_gatherObjs;
static int RunCase(const Agent& ag);
static int OneCase() {
    BuildWorld();
    return RunCase(NewAgent(Rnd(4) == 0));
}
static int RunCase(const Agent& ag) {
    DWORD ign1[8], ign2[8];
    const DWORD n1 = Rnd(3), n2 = Rnd(3);
    for (DWORD i = 0; i < 8; i++) { ign1[i] = 0x1000 + Rnd(40); ign2[i] = 0x1000 + Rnd(40); }
    for (DWORD i = 0; i < 8; i += 2) ign1[i + 1] = ign2[i + 1] = 0;
    DWORD ign1v[16], ign2v[16];
    for (int i = 0; i < 8; i++) { ign1v[i * 2] = ign1[i]; ign1v[i * 2 + 1] = 0; ign2v[i * 2] = ign2[i]; ign2v[i * 2 + 1] = 0; }
    IdVec ids;
    NewIds(&ids, Rnd(4));
    const int n0 = (int)(ids.e - ids.b) / 2;
    static DWORD before[512];
    memcpy(before, ids.b, n0 * 8);
    // the clone first (it must not change anything the game's call reads)
    static SrOut out;
    static SrCtx ctx;
    int why = SrRun(ag.a, before, n0, ign1v, n1, ign2v, n2, &out, &ctx);
    g_fbCases[why]++;
    if (!why) g_gatherObjs += ctx.n;  // the last gather's footprints
    // the game's call
    ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &ids, ign1v, n1, ign2v, n2);
    const int added = (int)(ids.e - ids.b) / 2 - n0;
    for (int i = 0; i < W.nfp; i++) assert(*(LONG*)(W.fps[i] + 8) == 100);  // the game's references are balanced
    g_cases++;
    if (why) return why;
    if (added != out.nIds || memcmp(ids.b + n0 * 2, out.ids, (size_t)added * 8) != 0) {
        Dump("MISMATCH appended ids", ids.b + n0 * 2, added, &out.ids[0][0], out.nIds);
        printf("case %ld, n0 %d, ign %lu/%lu, radius %g, excl subs %d\n", g_cases, n0, n1, n2, *(const float*)(ag.a + 0x40), out.nSubs);
        {
            u8* first = SrFirstFlagged(ag.a);
            __declspec(align(16)) u8 pt[0x40] = {};
            FkConv((const float*)(first + 0x10), pt);
            DWORD vec[4] = {0, 0, 0, kSrVecTag};
            ((SrQueryFn)(uintptr_t)0x0082EDB0)(W.world, NULL, pt, *(const float*)(ag.a + 0x40), vec, 0, *(const DWORD*)(ag.a + 0xB8));
            printf(" game obstacle query:");
            for (DWORD* s = (DWORD*)(uintptr_t)vec[0]; s != (DWORD*)(uintptr_t)vec[1]; s++) printf(" %p", (u8*)(uintptr_t)*s - 4);
            printf("\n");
        }
        for (int i = 0; i < ag.nn; i++) {  // per flagged node: the game's gather 0x82F0B0 vs the clone's
            if (!ag.nodes[i][0x30]) continue;
            __declspec(align(16)) u8 pt[0x40] = {};
            FkConv((const float*)(ag.nodes[i] + 0x10), pt);
            DWORD vec[4] = {0, 0, 0, kSrVecTag};
            ((u8(__fastcall*)(u8*, void*, const void*, const void*, void*, DWORD, DWORD, float))(uintptr_t)0x0082F0B0)(
                W.world, NULL, pt, pt, vec, 1, *(const DWORD*)(ag.a + 0xB8), 0.0f);
            const u8* lot = FkLotLookup(NULL, NULL, ((float*)pt)[0], ((float*)pt)[2]);
            SrQuery(&ctx, W.p, lot ? *(DWORD*)(lot + 0x48) : 0, 0, *(SrPt*)pt, false, 0.0f, NULL);
            printf(" node %d (%.2f, %.2f) lot %p: game gather", i, ((float*)pt)[0], ((float*)pt)[2], lot);
            for (DWORD* s = (DWORD*)(uintptr_t)vec[0]; s != (DWORD*)(uintptr_t)vec[1]; s++) {
                const u8* o = (const u8*)(uintptr_t)*s - 4;
                printf(" %p[id %lX t %X c %X]", o, *(DWORD*)(o + 0x14), o[0x35], o[0x34]);
            }
            printf("\n   clone gather");
            for (int j = 0; j < ctx.n; j++) printf(" %p", ctx.objs[j]);
            printf("\n");
            const float* raw = (const float*)(ag.nodes[i] + 0x10);
            const u8* lotE = FkLotLookup(NULL, NULL, raw[0], raw[2]);
            float u = 0, v = 0;
            if (lotE) ((void(__fastcall*)(u8*, void*, DWORD, DWORD, float, float, float*, float*))(uintptr_t)0x0062EF30)(W.p, NULL, *(DWORD*)(lotE + 0x48), 0, raw[0], raw[2], &u, &v);
            for (int j = 0; j < ctx.n; j++) {
                const u8* o = ctx.objs[j];
                const void* const* vt = *(const void* const* const*)o;
                __declspec(align(16)) float a[4] = {u, v, u, u};
                const u8 c0 = ((SlotBoolFn)vt[0xC0 / 4])(o, NULL);
                const u8 e8 = c0 ? ((SlotSegFn)vt[0xE8 / 4])(o, NULL, 0, a, a) : 9;
                printf("   obj %p id %lX:%lX vt %08lX c0 %d e8 %d lotE %p uv %g %g ign2[0..1] %lX %lX\n", o, *(DWORD*)(o + 0x14), *(DWORD*)(o + 0x18),
                       *(DWORD*)o, c0, e8, lotE, u, v, ign2v[0], ign2v[2]);
            }
        }
        exit(1);
    }
    g_withIds += added > 0;
    g_idsTotal += added;
    g_withExcl += out.nSubs > 0;
    // the obstacle query itself vs the clone's point query
    u8* first = SrFirstFlagged(ag.a);
    __declspec(align(16)) u8 pt[0x40] = {};
    FkConv((const float*)(first + 0x10), pt);
    DWORD vec[4] = {0, 0, 0, kSrVecTag};
    ((SrQueryFn)(uintptr_t)0x0082EDB0)(W.world, NULL, pt, *(const float*)(ag.a + 0x40), vec, 0, *(const DWORD*)(ag.a + 0xB8));
    const int n = (int)((vec[1] - vec[0]) / 4);
    if (n != out.nSubs || (n && memcmp((const void*)(uintptr_t)vec[0], out.subs, (size_t)n * 4) != 0)) {
        printf("MISMATCH obstacle query: game %d footprints, clone %d (case %ld)\n", n, out.nSubs, g_cases);
        exit(1);
    }
    ((SrVecRelFn)(uintptr_t)0x00AAC090)(vec, NULL, (void*)(uintptr_t)vec[0], (void*)(uintptr_t)vec[1]);
    if (vec[0]) _aligned_free((void*)(uintptr_t)vec[0]);
    _aligned_free(ids.b);
    return 0;
}

// --- C: worker writer stubs ---
static HANDLE g_wIn, g_wGo;
static int g_cbSeen;
static void __fastcall CbRebuild(u8*, void*, DWORD) {
    // inside Footprint_EnsureLevelPolygons on the worker: QL is ours, exclusive
    assert(t_qlDepth == 1 && !TryAcquireSRWLockShared(&g_ql));
    g_cbSeen++;
    if (g_wIn) { SetEvent(g_wIn); WaitForSingleObject(g_wGo, 5000); }
}
static u8* g_wFp;
// C-A: the fake +0x918 for the sim-thread rebuild; the acquire must come before 0x80E5B0 marks the level valid
static u8 g_caLock[0x28];
static int g_caAcq, g_caRebuilds;
static LONG __fastcall CaFakeAcquire(void* lock, void*, const DWORD*) {
    assert((*(u8**)(g_wFp + 0x38))[0x20] == 0);  // level still invalid, polygons not freed yet
    g_caAcq++;
    return ++*(LONG*)((u8*)lock + 0x20);
}
static LONG __fastcall CaFakeRelease(void* lock, void*) { return --*(LONG*)((u8*)lock + 0x20); }
static void __fastcall CbRebuildSim(u8*, void*, DWORD) {
    assert(*(LONG*)(g_caLock + 0x20) == 1 && t_qlDepth == 0);  // inside the rebuild: +0x918 held, no QL
    g_caRebuilds++;
}
// F: the callees that block inside a tripwire writer (thiscall: the lock-key deleting destructor, ret 4; a footprint release)
static void __fastcall TwKeyDtor(u8*, void*, DWORD flag) {
    assert(flag == 1);
    SetEvent(g_wIn);
    WaitForSingleObject(g_wGo, 5000);
}
static void __fastcall TwFpRelease(u8*, void*) {
    SetEvent(g_wIn);
    WaitForSingleObject(g_wGo, 5000);
}
// a "job" as ServeAsync runs it (0.18.0): the tripwire stubs are armed before A_RUNNING and disarmed after A_IDLE
static void Job(bool on) {
    if (on) { TwArm(true); g_async.state = A_RUNNING; }
    else { g_async.state = A_IDLE; TwArm(false); }
}
// F2: the game's 0x807110 on a plain thread (T-S7 site 4), blocking in TwKeyDtor until g_wGo
static u8 g_kOld[0x10], g_kNew[0x10], g_kFp[0x80];
static DWORD g_kVt[4];
static void KeyReset() {
    g_kVt[0] = (DWORD)(uintptr_t)TwKeyDtor;
    *(DWORD**)g_kOld = g_kVt, *(LONG*)(g_kOld + 4) = 1;
    *(DWORD**)g_kNew = g_kVt, *(LONG*)(g_kNew + 4) = 1;
    *(u8**)(g_kFp + 0x20) = g_kOld;
}
static DWORD WINAPI KeyWriter(void*) {
    ((void(__fastcall*)(u8*, void*, u8*))(uintptr_t)0x00807110)(g_kFp, NULL, g_kNew);
    assert(*(u8**)(g_kFp + 0x20) == g_kNew && t_rg.n == 0 && t_twDepth == 0);
    return 0;
}
// F2: inside the clone (its active-lot lookup in phase 2): the job ends (disarm), a writer enters the now disarmed site and
// blocks; mode 2 also arms the next job before the clone goes on
static int g_f2Mode, g_f2Runs;
static HANDLE g_f2Writer;
static const u8* __cdecl F2ActiveLot() {
    if (g_f2Mode && !g_f2Writer) {
        TwArm(false);
        KeyReset();
        g_f2Writer = CreateThread(NULL, 0, KeyWriter, NULL, 0, NULL);
        WaitForSingleObject(g_wIn, INFINITE);
        if (g_f2Mode == 2) TwArm(true);
        g_f2Runs++;
    }
    return ((SrActiveLotFn)(uintptr_t)0x00C88FD0)();
}

// I: fakes for the IC's phase-1 callees (RouteHandle_Lookup, the route's vtable pair, 0xADD860, 0xB015B0, 0x74A8F0)
struct I {
    static u8* __fastcall Route(const void*, void*, DWORD h) { return h == 0x25000001 ? route : NULL; }
    static void __fastcall Ref(u8*, void*) { refIn++; }
    static void __fastcall Unref(u8*, void*) { refOut++; }
    static void __cdecl Level(const u8*, const float* pt, float* io) {  // the point's level: 0 mostly, 1 or -1 sometimes
        const DWORD h = (DWORD)(LONG)(pt[0] * 7.0f + pt[2] * 13.0f);
        io[0] = io[1] = 0.5f;
        *(LONG*)&io[2] = h % 7 == 0 ? 1 : h % 11 == 0 ? -1 : 0;
    }
    static u8 __cdecl Inside(const u8*, const void*) { return 0; }
    static float __cdecl Height(float x, float z, LONG, DWORD, DWORD) { return x * 0.5f - z; }
    static u8* route;
    static int refIn, refOut;
};
u8* I::route;
int I::refIn, I::refOut;

static DWORD WINAPI WorkerThread(void*) {
    g_workerTid = GetCurrentThreadId();
    ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0080E5B0)(g_wFp, NULL, 0);
    assert(t_qlDepth == 0 && t_rg.n == 0);
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* e) -> LONG {
        const DWORD code = e->ExceptionRecord->ExceptionCode;
        if (code == 0x406D1388 || code == 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
        printf("exception %08lX at %08lX esp %08lX; stack:", code, e->ContextRecord->Eip, e->ContextRecord->Esp);
        const DWORD* sp = (const DWORD*)(uintptr_t)e->ContextRecord->Esp;
        for (int i = 0, k = 0; i < 256 && k < 10; i++)
            if (sp[i] >= 0x00401000 && sp[i] < 0x00F95000) { printf(" %08lX", sp[i]); k++; }
        printf("\n");
        return EXCEPTION_CONTINUE_SEARCH;
    });
    char child[4] = "";
    if (!GetEnvironmentVariableA("SRT_CHILD", child, sizeof(child))) return RunChild();
    if (!Reserve()) return 2;
    QueryPerformanceFrequency(&g_qpf);  // StartWorker sets it in the game (the async path needs the worker)
    const char* exe = argc > 1 ? argv[1] : "..\\..\\..\\game-copy\\bin\\TS3W.exe";
    const int cases = argc > 2 ? atoi(argv[2]) : 400;
    if (argc > 3) g_seed = (DWORD)atoi(argv[3]);
    HANDLE f = CreateFileA(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { printf("cannot open %s\n", exe); return 2; }
    const DWORD size = GetFileSize(f, NULL);
    u8* img = (u8*)malloc(size);
    DWORD got;
    if (!img || !ReadFile(f, img, size, &got, NULL) || got != size) { printf("cannot read %s\n", exe); return 2; }
    CloseHandle(f);
    if (!MapImage(img)) return 2;
    free(img);
    // every address the clone calls, unchanged in the image (the same bytes the game runs)
    assert(Matches((const void*)(uintptr_t)STEER_LOCO, kLocoPro, sizeof(kLocoPro)));
    for (int i = 0; i < QL_N; i++) assert(Matches((const void*)(uintptr_t)kQlSites[i].addr, kQlSites[i].pro, kQlSites[i].len));
    for (int i = 0; i < TW_N; i++) assert(Matches((const void*)(uintptr_t)kTwSites[i].addr, kTwSites[i].pro, kTwSites[i].len));
    // P10 / G10-C (p10.cpp, docs/87): the three IC stubs' prologues, Route_PlanCommon has the goal step's prologue (site 10
    // moves there), and the call edges the P1b row and the G10-C keys rely on: 0x760BFA -> goal step, 0x764298 / 0x7643FB ->
    // Route_PlanCommon (returns 0x76429D / 0x764400)
    for (int i = 1; i < 4; i++) assert(Matches((const void*)(uintptr_t)kP10Ic[i], kP10Pro[i], sizeof(kP10Pro[i])));
    assert(Matches((const void*)(uintptr_t)ROUTE_PLAN_COMMON, kRgSites[10].pro, kRgSites[10].len) && kRgSites[10].addr == 0x0075C2D0);
    assert(Matches((const void*)(uintptr_t)kRgSites[10].addr, kRgSites[10].pro, kRgSites[10].len));
    auto callsTo = [](DWORD at, DWORD to) { const u8* p = (const u8*)(uintptr_t)at; return p[0] == 0xE8 && at + 5 + *(const DWORD*)(p + 1) == to; };
    assert(callsTo(0x00760BFA, 0x0075C2D0) && callsTo(0x00764298, ROUTE_PLAN_COMMON) && callsTo(0x007643FB, ROUTE_PLAN_COMMON));
    printf("P10 / G10-C: IC prologues, Route_PlanCommon prologue and the three call edges match\n");
    // [ProbeValues] (values.cpp, docs/108): every entry prologue and call edge of the table, on the image
    for (int i = 0; i < PV_N; i++) {
        u8 want[8];
        PvExpect(kPv[i], kPv[i].at, kPv[i].target, want);
        if (!Matches((const void*)(uintptr_t)kPv[i].at, want, kPv[i].len)) printf("values site %s 0x%08lX differs\n", kPv[i].name, kPv[i].at);
        assert(Matches((const void*)(uintptr_t)kPv[i].at, want, kPv[i].len));
    }
    assert(callsTo(PV_INROOM_RET - 5, 0x0064F550));  // the in-room path's Portal_IsLocked call (0x632554)
    printf("values: %d sites match\n", (int)PV_N);
    Jmp(0x004E69D0, (void*)FkMalloc);
    Jmp(0x004E6A20, (void*)FkMemalign);
    Jmp(0x004E5920, (void*)FkFree);
    Jmp(0x004E16F0, (void*)FkLock);
    Jmp(0x004E17B0, (void*)FkUnlock);
    Jmp(0x0085AE60, (void*)FkConv);
    Jmp(0x00C68990, (void*)FkLotLookup);
    // sub-object vtables (the scan for 0x620E60 at +4, docs/84 §3.6): one per type getter
    static const DWORD kSubs[] = {0xFEADE0, 0xFEB5D4, 0xFEB6F8, 0xFEB820, 0xFEB94C, 0xFEBA74, 0xFEBB98, 0xFEBCE8, 0xFEBE10};
    for (DWORD s : kSubs) {
        const DWORD* v = (const DWORD*)(uintptr_t)s;
        assert(v[1] == 0x00620E60 && (v[2] == 0x00624ED0 || v[2] == 0x00620E50));
        g_subVt[v[2] == 0x00620E50] = s;
    }
    assert(g_subVt[0] && g_subVt[1]);

    // D. lot transforms, bit for bit (lot table hit: no insert)
    for (int i = 0; i < 2000; i++) {
        if (i % 100 == 0) BuildWorld();
        const int lot = Rnd(2);
        const float x = Rf(-300, 300), z = Rf(-300, 300);
        __declspec(align(16)) float rec[4] = {x, z, 0, 0};
        float o3[3], ox, oy, u, v;
        ((void(__fastcall*)(u8*, void*, DWORD, DWORD, const float*, float*))(uintptr_t)0x0062ED40)(W.p, NULL, 0x10 + lot, 0, rec, o3);
        ((void(__fastcall*)(u8*, void*, DWORD, DWORD, float, float, float*, float*))(uintptr_t)0x0062EF30)(W.p, NULL, 0x10 + lot, 0, x, z, &ox, &oy);
        SrXform(W.entries[lot], x, z, &u, &v);
        assert(memcmp(&u, &o3[0], 4) == 0 && memcmp(&v, &o3[1], 4) == 0 && memcmp(&u, &ox, 4) == 0 && memcmp(&v, &oy, 4) == 0);
    }
    printf("transforms: 2000 x 2 bit-exact\n");

    // A. the game's call vs the clone
    for (int i = 0; i < cases; i++) OneCase();
    printf("A: %ld cases, %ld compared; with ids %ld (%ld ids), with obstacle footprints %ld; clone gave up: lot not in table %ld, "
           "deferred %ld, shape %ld, other %ld\n", g_cases, g_fbCases[0], g_withIds, g_idsTotal, g_withExcl, g_fbCases[SR_FB_LOTMISS],
           g_fbCases[SR_FB_DEFERRED], g_fbCases[SR_FB_SHAPE], g_cases - g_fbCases[0] - g_fbCases[SR_FB_LOTMISS] - g_fbCases[SR_FB_DEFERRED] - g_fbCases[SR_FB_SHAPE]);
    printf("   gathered footprints %ld; visitor branches so far: masked %ld, leaf %ld, children %ld\n", g_gatherObjs, g_srVisitKind[0],
           g_srVisitKind[1], g_srVisitKind[2]);
    assert(g_fbCases[0] > cases / 2 && g_withIds > cases / 20 && g_withExcl > cases / 10 && g_srVisitKind[0] > cases);

    // A2. buffer limits (0.21.0): past the 0.13.0 sizes the clone must answer as the game's call; past the 0.21.0 ones it must
    // give up with SR_FB_SHAPE at that limit's site (RunCase still runs the game's call on the same world)
    {
        Ts3nCounters& kc = ts3native_counters;
        struct Lim { const char* name; int old, now, site; LONG* seen; Big lo, hi; };
        Lim lims[] = {
            {"objects", 512, kSrCap, SRS_OBJCAP, &kc.s2MaxObjs, {700, 0, 0, false}, {1200, 0, 0, false}},
            {"local", 64, kSrLocal, SRS_LOCALCAP, &kc.s2MaxLocal, {300, 100, 3, true}, {300, 200, 3, true}},
            {"nodes", 17, kSrNodes + 1, SRS_NODECAP, &kc.s2MaxNodes, {30, 0, 26, false}, {30, 0, 40, false}},
            {"ids", 256, kSrIds, SRS_IDCAP, &kc.s2MaxIds, {450, 0, 3, true}, {900, 0, 3, true}},
        };
        const int per = cases / 40 > 10 ? cases / 40 : 10;
        for (Lim& l : lims) {
            int okOld = 0, fbNew = 0, overOld = 0, overNew = 0;
            LONG most = 0;
            for (int side = 0; side < 2; side++)
                for (int i = 0; i < per; i++) {
                    g_big = side ? l.hi : l.lo;
                    BuildWorld();
                    BigLeaf();
                    Agent ag = NewAgent(false);
                    *l.seen = 0;
                    t_srShape = -1;
                    const int why = RunCase(ag);  // exits on a mismatch
                    const LONG seen = *l.seen;
                    if (seen > most) most = seen;
                    if (seen > l.now) {  // past the new limit: never an answer, and the site is this limit's
                        overNew++;
                        assert(why != SR_OK);
                        if (why == SR_FB_SHAPE) { assert(t_srShape == l.site); fbNew++; }
                    } else if (seen > l.old) {
                        overOld++;
                        okOld += why == SR_OK;
                    }
                }
            printf("A2: %-7s old %d new %d: %d cases past the old limit (%d compared equal), %d past the new (%d gave up at its "
                   "site), most seen %ld\n", l.name, l.old, l.now, overOld, okOld, overNew, fbNew, most);
            assert(okOld > 0 && fbNew > 0);
        }
        g_big = Big{};
    }

    // B. the S2 hook on the mapped 0x85FD10
    Ts3nCounters& k = ts3native_counters;
    g_locoOrig = (LocoFn)InstallDetour("Locomotion_ObstacleCheck", STEER_LOCO, kLocoPro, sizeof(kLocoPro), (void*)LocoHook);
    assert(g_locoOrig);
    const DWORD me = GetCurrentThreadId();
    static DWORD simTid;
    simTid = me;
    g_rtSimTidSlot = &simTid;
    g_async.owner = me;
    k.steerMode = 1;
    long taken = 0, compared = 0;
    for (int i = 0; i < cases; i++) {
        // the same world twice: the hook during a "job", then the game's call
        const DWORD seed = g_seed;
        BuildWorld();
        Agent ag = NewAgent(false);
        IdVec a, b;
        NewIds(&a, 2);
        b = a;
        b.b = (DWORD*)_aligned_malloc(8 * 3, 16);
        memcpy(b.b, a.b, 16);
        b.e = b.b + 4, b.c = b.b + 6;
        const LONG t0 = k.steerTaken;
        Job(true);
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &a, NULL, 0, NULL, 0);
        Job(false);
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &b, NULL, 0, NULL, 0);  // no job: the trampoline
        taken += k.steerTaken - t0;
        assert(a.e - a.b == b.e - b.b && memcmp(a.b, b.b, (a.e - a.b) * 4) == 0);
        compared++;
        _aligned_free(a.b);
        _aligned_free(b.b);
        (void)seed;
    }
    printf("B: hook %ld calls, %ld read-only answers, all equal to the game's\n", compared, taken);
    assert(taken > cases / 2);
    // shadow: no job, the game's call and the clone compared inside the hook
    k.steerShadow = 1;
    const LONG sh0 = k.steerShadowRuns;
    for (int i = 0; i < cases; i++) {
        BuildWorld();
        Agent ag = NewAgent(false);
        IdVec a;
        NewIds(&a, 2);
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &a, NULL, 0, NULL, 0);
        _aligned_free(a.b);
    }
    printf("B: shadow %ld runs, mismatch %ld, order %ld, query mismatch %ld\n", k.steerShadowRuns - sh0, k.steerShadowMismatch,
           k.steerShadowOrder, k.steerShadowQueryMismatch);
    assert(k.steerShadowRuns - sh0 > cases / 2 && k.steerShadowMismatch == 0 && k.steerShadowOrder == 0 && k.steerShadowQueryMismatch == 0);
    k.steerShadow = 0;
    // fallbacks: each reason calls the game's code (here: its answer is still the reference)
    {
        BuildWorld();
        Agent ag = NewAgent(false);
        while (ag.nn < 2) ag = NewAgent(false);  // EditPath runs from the second node on
        for (int i = 0; i < ag.nn; i++) ag.nodes[i][0x30] = 1;
        static SrOut out;
        static SrCtx ctx;
        W.wm[0x1CC] = 1;
        assert(SrRun(ag.a, NULL, 0, NULL, 0, NULL, 0, &out, &ctx) == SR_FB_DIRTY);
        W.wm[0x1CC] = 0;
        // a lot that is not in the lot table: EditPath's GetOrInsert would insert it (the gather's Contains says no first)
        for (int i = 0; i < ag.nn; i++) { float* p = (float*)(ag.nodes[i] + 0x10); p[0] = 150; p[2] = 0; }
        assert(SrRun(ag.a, NULL, 0, NULL, 0, NULL, 0, &out, &ctx) == SR_FB_LOTMISS);
        // the hook's state checks: lock owner, inside a job callback
        IdVec v;
        NewIds(&v, 0);
        const LONG st0 = k.steerFb[SR_FB_STATE - 1];
        Job(true);
        for (int i = 0; i < ag.nn; i++) { float* p = (float*)(ag.nodes[i] + 0x10); p[0] = -0.5f; p[2] = 0; }
        k.rgOwnerTid = (LONG)me;
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &v, NULL, 0, NULL, 0);
        k.rgOwnerTid = 0;
        g_inInvoke = 1;
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &v, NULL, 0, NULL, 0);
        g_inInvoke = 0;
        Job(false);
        assert(k.steerFb[SR_FB_STATE - 1] == st0 + 2);
        _aligned_free(v.b);
    }
    {  // A2 through the hook during a job: past kSrCap the fallback is counted under its site and the game's call answers
        int atSite = 0, ids = 0;
        for (int i = 0; i < 20; i++) {
            g_big = Big{1200, 0, 3, true};
            BuildWorld();
            BigLeaf();
            Agent ag = NewAgent(false);
            g_big = Big{};
            IdVec a, b;
            NewIds(&a, 0);
            NewIds(&b, 0);
            const LONG s0 = k.s2Shape[SRS_OBJCAP], f0 = k.steerFb[SR_FB_SHAPE - 1], t0 = k.steerTaken;
            Job(true);
            ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &a, NULL, 0, NULL, 0);
            Job(false);
            ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &b, NULL, 0, NULL, 0);
            if (k.s2Shape[SRS_OBJCAP] != s0) {  // a gather past kSrCap: no answer, one shape fallback, the game's call
                assert(k.s2Shape[SRS_OBJCAP] == s0 + 1 && k.steerFb[SR_FB_SHAPE - 1] == f0 + 1 && k.steerTaken == t0);
                atSite++;
            }
            assert(a.e - a.b == b.e - b.b && memcmp(a.b, b.b, (a.e - a.b) * 4) == 0);
            ids += (int)(a.e - a.b) / 2;
            _aligned_free(a.b);
            _aligned_free(b.b);
        }
        printf("B: past kSrCap through the hook, 20 calls: %d shape fallbacks at its site (no answer taken), %d ids from the game's call\n",
               atSite, ids);
        assert(atSite > 0);
    }
    printf("B: fallbacks %ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld (state, QL busy, dirty, lot miss, deferred, cache, shape, tripwire)\n",
           k.steerFb[0], k.steerFb[1], k.steerFb[2], k.steerFb[3], k.steerFb[4], k.steerFb[5], k.steerFb[6], k.steerFb[7]);

    // C. the worker writer stubs on the game's prologues; W6a end to end on a worker thread
    for (int i = 0; i < QL_N; i++) {
        g_qlTramp[i] = InstallDetour(kQlSites[i].name, kQlSites[i].addr, kQlSites[i].pro, kQlSites[i].len, (void*)kQlStubs[i]);
        assert(g_qlTramp[i]);
    }
    g_qlOn = 1;
    static DWORD cbVt[0x50];
    memcpy(cbVt, (const void*)(uintptr_t)0x00FEBBC0, sizeof(cbVt));
    cbVt[0xF8 / 4] = cbVt[0xFC / 4] = (DWORD)(uintptr_t)CbRebuild;
    g_wFp = (u8*)A(0x80);
    *(DWORD**)g_wFp = cbVt;
    u8* cache = (u8*)A(0x24 * 3);  // level 0 invalid: 0x80E5B0 rebuilds through +0xF8 / +0xFC
    *(u8**)(g_wFp + 0x38) = cache;
    HANDLE th = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    assert(g_cbSeen == 2 && cache[0x20] == 1 && k.steerQlHolds == 1 && TryAcquireSRWLockShared(&g_ql));
    ReleaseSRWLockShared(&g_ql);
    // the worker holds QL (inside the rebuild) while a job runs: the hook falls back, the game's answer is the same
    cache[0x20] = 0;
    g_wIn = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_wGo = CreateEventW(NULL, FALSE, FALSE, NULL);
    th = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    WaitForSingleObject(g_wIn, INFINITE);
    {
        BuildWorld();
        Agent ag = NewAgent(false);
        ag.nodes[0][0x30] = 1;
        IdVec a, b;
        NewIds(&a, 0);
        NewIds(&b, 0);
        const LONG busy0 = k.steerFb[SR_FB_TRY - 1];
        Job(true);
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &a, NULL, 0, NULL, 0);
        assert(k.steerFb[SR_FB_TRY - 1] == busy0 + 1);
        // a sim-thread cache rebuild during a job is counted (here on a valid cache: no rebuild, the stub still counts)
        const LONG sc0 = k.steerSimCacheWrite;
        u8* fp = W.fps[0];
        ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0080E5B0)(fp, NULL, 0);
        assert(k.steerSimCacheWrite == sc0 + 1);
        Job(false);
        SetEvent(g_wGo);
        WaitForSingleObject(th, INFINITE);
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &b, NULL, 0, NULL, 0);
        assert(a.e - a.b == b.e - b.b && memcmp(a.b, b.b, (a.e - a.b) * 4) == 0);
    }
    CloseHandle(th);
    assert(k.steerQlHolds == 2 && k.steerQlWaitUs == 0 && k.steerUntracked == 0 && k.steerSeqSkew == 0 && k.steerSimWriteInRo == 0);
    printf("C: 12 worker writer stubs on the game's prologues; W6a holds QL exclusive on the worker; QL busy -> fallback, same ids\n");

    // C-A on the game's 0x80E5B0: sim thread, a job runs, invalid level: +0x918 is taken in the stub before the body
    {
        static DWORD simVt[0x50];
        memcpy(simVt, cbVt, sizeof(simVt));
        simVt[0xF8 / 4] = simVt[0xFC / 4] = (DWORD)(uintptr_t)CbRebuildSim;
        *(DWORD**)g_wFp = simVt;
        g_caClasses[0] = (DWORD)(uintptr_t)simVt;
        g_lockOrig = CaFakeAcquire;
        g_rgRelOrig = CaFakeRelease;
        g_rgLock = g_caLock;
        g_async.lock = g_caLock;
        g_async.owner = me;
        g_workerTid = me + 7;
        g_caOn = 1;
        k.caMode = 1;
        cache[0x20] = 0;
        Job(true);
        ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0080E5B0)(g_wFp, NULL, 0);
        Job(false);
        assert(g_caAcq == 1 && g_caRebuilds == 2 && cache[0x20] == 1 && *(LONG*)(g_caLock + 0x20) == 0 && t_rg.n == 0);
        assert(k.caSimInJob[0] == 1 && k.caWaits == 1 && k.caQlHeld == 0);
        // a valid level: no lock, nothing counted
        Job(true);
        ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0080E5B0)(g_wFp, NULL, 0);
        Job(false);
        assert(g_caAcq == 1 && k.caSimInJob[0] == 1);
        printf("C-A: the game's 0x80E5B0 on the sim thread during a job took +0x918 before marking the level valid, released on return\n");
    }
    // E. design L1 (lotfd.cpp): the game's 0x631800 vs LtFrontDoor on random lot tables, then the L1 detour on it, with the
    // T-L3 counting thunks in the game's portal vtable 0xFED048 (the game's own AddRef 0x64E8E0 / Release 0x64E8F0 behind them)
    {
        assert(Matches((const void*)(uintptr_t)LT_FD, kFdPro, sizeof(kFdPro)));
        assert(InstallPortalRefThunks((DWORD*)(uintptr_t)LT_PORTAL_VT));
        static u8 fwm[0x200], fact[0x60];
        *(u8**)(uintptr_t)0x011ECBC4 = fwm;
        *(u8**)(fwm + 0xB0) = fact;
        struct FdLot { DWORD lo, hi; };
        static FdLot lots[64];
        static u8* portals[64 * 5];
        static LONG counts[64 * 5];
        auto build = [&](int* nLots, int* nPortals) -> u8* {
            u8* p = (u8*)A(0x200);
            const DWORD cnt = 1 + Rnd(16);
            DWORD* bk = (DWORD*)A((cnt + 1) * 4);
            bk[cnt] = kSentinel;
            *(DWORD**)(p + 0x4C) = bk;
            *(DWORD*)(p + 0x50) = cnt;
            *nLots = 1 + Rnd(64);
            *nPortals = 0;
            for (int i = 0; i < *nLots; i++) {
                lots[i].lo = 1 + Rnd(cnt * 4) + (DWORD)i * 0x100;  // distinct, several per bucket
                lots[i].hi = Rnd(4) ? 0 : Rnd(3);
                u8* node = (u8*)A(0x200);
                *(DWORD*)node = lots[i].lo, *(DWORD*)(node + 4) = lots[i].hi;
                u8* e = node + 0x10;
                *(DWORD*)e = Rnd(40) ? lots[i].lo : lots[i].lo + 1;  // rarely a node whose entry holds another key
                *(DWORD*)(e + 4) = lots[i].hi;
                const int n = (int)Rnd(5);
                if (n) {
                    DWORD* v = (DWORD*)A(4 * n);
                    for (int j = 0; j < n; j++) {
                        if (Rnd(6) == 0) continue;  // a NULL slot
                        u8* pt = (u8*)A(0x80);
                        *(DWORD*)pt = LT_PORTAL_VT;
                        *(DWORD*)(pt + 8) = Rnd(0xFFFFFF) * 7, *(DWORD*)(pt + 0xC) = Rnd(0xFFFF);
                        *(LONG*)(pt + 0x70) = 1 + (LONG)Rnd(3);
                        portals[*nPortals] = pt;
                        counts[(*nPortals)++] = *(LONG*)(pt + 0x70);
                        v[j] = (DWORD)(uintptr_t)pt;
                    }
                    *(DWORD**)(e + 0x44) = v, *(DWORD**)(e + 0x48) = v + n;
                }
                *(DWORD*)(e + 0x18) = Rnd(4) ? Rnd(n + 2) : 0xFFFFFFFF;  // sometimes n or n + 1: out of range
                *(DWORD*)(e + 0x1C) = Rnd(4) ? Rnd(n + 2) : 0xFFFFFFFF;
                const DWORD b = lots[i].lo % cnt;
                *(DWORD*)(node + 0x190) = bk[b];
                bk[b] = (DWORD)(uintptr_t)node;
            }
            *(DWORD*)(fact + 0x48) = Rnd(8) ? 0x77777777 : lots[Rnd(*nLots)].lo;  // sometimes a table lot is the active lot
            *(DWORD*)(fact + 0x4C) = Rnd(8) ? 0 : lots[0].hi;
            return p;
        };
        auto pick = [&](int nLots, DWORD* lo, DWORD* hi) {
            const DWORD r = Rnd(20);
            if (r == 0) *lo = *hi = 0;
            else if (r == 1) *lo = *hi = 0xFFFFFFFF;
            else if (r == 2) { *lo = *(DWORD*)(fact + 0x48); *hi = *(DWORD*)(fact + 0x4C); }
            else if (r < 6) { *lo = Rnd(0x10000); *hi = Rnd(2); }  // mostly not in the table
            else { const FdLot& l = lots[Rnd(nLots)]; *lo = l.lo; *hi = l.hi; }
        };
        typedef u64(__fastcall * GameFdFn)(const u8*, void*, DWORD, DWORD, DWORD, const u8**);
        const u8* const kOutMark = (const u8*)(uintptr_t)0x0BAD0BAD;
        g_async.owner = me;
        long cmp = 0, withDoor = 0, perType[5] = {};
        for (int c = 0; c < cases * 5; c++) {
            int nLots, nPortals;
            const u8* p = build(&nLots, &nPortals);
            for (int q = 0; q < 8; q++) {
                DWORD lo, hi;
                pick(nLots, &lo, &hi);
                const DWORD type = Rnd(5);  // 3 and 4: no door
                const u8* out1 = kOutMark;
                const bool withOut = Rnd(2) != 0;  // the IC passes NULL; the out path is checked anyway
                Job(true);  // T-L3 counts only during a job
                const u64 r1 = ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, type, withOut ? &out1 : NULL);
                Job(false);
                for (int i = 0; i < nPortals; i++) assert(*(LONG*)(portals[i] + 0x70) == counts[i]);  // the game's net change is 0
                u64 r2;
                const u8* pt;
                assert(LtFrontDoor(p, lo, hi, type, &r2, &pt) == LT_OK);
                assert(r1 == r2 && out1 == (withOut && pt ? pt : kOutMark) && (pt || r2 == 0));
                cmp++;
                withDoor += pt != NULL;
                perType[type] += pt != NULL;
            }
        }
        printf("E: game 0x631800 vs clone: %ld calls, all equal; with a door %ld (type 0 %ld, 1 %ld, 2 %ld); portal AddRef %ld = Release %ld\n",
               cmp, withDoor, perType[0], perType[1], perType[2], k.portalRef[0], k.portalRef[4]);
        assert(withDoor > cmp / 10 && perType[3] == 0 && perType[4] == 0 && k.portalRef[0] == withDoor && k.portalRef[4] == withDoor);
        // the L1 detour on the mapped 0x631800: job -> clone (no AddRef/Release), no job -> the game's code; shadow; QL busy
        g_fdOrig = (FdFn)InstallDetour("LotRouting_FrontDoorPortal", LT_FD, kFdPro, sizeof(kFdPro), (void*)FdHook);
        assert(g_fdOrig);
        k.ltMode = 1;
        const LONG ref0 = k.portalRef[0];
        long ltTaken = 0;
        for (int c = 0; c < cases; c++) {
            int nLots, nPortals;
            const u8* p = build(&nLots, &nPortals);
            DWORD lo, hi;
            pick(nLots, &lo, &hi);
            const DWORD type = Rnd(3);
            const LONG t0 = k.ltTaken;
            Job(true);
            const u64 a = ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, type, NULL);
            Job(false);
            const u64 b = ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, type, NULL);
            assert(a == b && k.ltTaken == t0 + 1);
            ltTaken++;
        }
        assert(k.portalRef[0] == ref0);  // read-only answers took no reference
        k.ltShadow = 1;
        for (int c = 0; c < cases; c++) {
            int nLots, nPortals;
            const u8* p = build(&nLots, &nPortals);
            DWORD lo, hi;
            pick(nLots, &lo, &hi);
            const u8* o = kOutMark;
            ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, Rnd(4), &o);
        }
        assert(k.ltShadowRuns == cases && k.ltShadowMismatch == 0 && g_rgOffSimOpen == 0);
        // a worker writer holds QL: the hook falls back to the game's code, same answer
        g_wIn = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_wGo = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE qh = CreateThread(NULL, 0, [](void*) -> DWORD {
            AcquireSRWLockExclusive(&g_ql);
            SetEvent(g_wIn);
            WaitForSingleObject(g_wGo, 5000);
            ReleaseSRWLockExclusive(&g_ql);
            return 0;
        }, NULL, 0, NULL);
        WaitForSingleObject(g_wIn, INFINITE);
        int nLots, nPortals;
        const u8* p = build(&nLots, &nPortals);
        const DWORD lo = lots[0].lo, hi = lots[0].hi;
        Job(true);
        const u64 a = ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, 0, NULL);
        Job(false);
        SetEvent(g_wGo);
        WaitForSingleObject(qh, INFINITE);
        CloseHandle(qh);
        assert(k.ltFb[LT_FB_TRY - 1] == 1 && a == ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, 0, NULL));
        printf("E: L1 hook %ld read-only answers equal to the game's; shadow %ld runs, 0 mismatches; QL busy -> game's call\n", ltTaken,
               k.ltShadowRuns);
        k.ltMode = k.ltShadow = 0;
    }
    // F. tripwires T4 and T-S7 (0.15.0) on the game's prologues: the game's own writer runs on another thread through its stub
    // and blocks inside (a fake callee); meanwhile the S2 hook drops the clone and runs the game's call; same ids as with no job
    {
        assert(InstallTripwires() == TW_N && k.steerTwStubs == TW_N);
        g_async.owner = me;
        g_workerTid = me + 7;
        k.steerMode = 1;
        k.steerShadow = 0;
        g_wIn = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_wGo = CreateEventW(NULL, FALSE, FALSE, NULL);
        // a fake lock-key object: vtable slot 0 = deleting destructor (0x807110 tail-jumps to it on the last reference)
        static DWORD keyVt[4], relVt[0x30];
        keyVt[0] = (DWORD)(uintptr_t)TwKeyDtor;
        relVt[0x94 / 4] = (DWORD)(uintptr_t)TwFpRelease;  // footprint release from the entry dtor
        static u8 keyOld[0x10], keyNew[0x10], keyFp[0x80], entry[0x20], relObj[0x10];
        auto oneCase = [&](DWORD (WINAPI* writer)(void*), int site) {
            BuildWorld();
            Agent ag = NewAgent(false);
            ag.nodes[0][0x30] = 1;
            IdVec a, b;
            NewIds(&a, 1);
            b = a;
            b.b = (DWORD*)_aligned_malloc(8 * 2, 16);
            memcpy(b.b, a.b, 8);
            b.e = b.b + 2, b.c = b.b + 4;
            const LONG fb0 = k.steerFb[SR_FB_TRIP - 1], tk0 = k.steerTaken, d0 = k.steerTripDiscard, s0 = k.steerTwSite[site];
            Job(true);
            HANDLE th = CreateThread(NULL, 0, writer, NULL, 0, NULL);
            WaitForSingleObject(g_wIn, INFINITE);
            assert(g_twActive == 1 && k.steerTwSite[site] == s0 + 1);
            ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &a, NULL, 0, NULL, 0);  // job running, writer inside: game's call
            assert(k.steerFb[SR_FB_TRIP - 1] == fb0 + 1 && k.steerTripDiscard == d0 + 1 && k.steerTaken == tk0);
            SetEvent(g_wGo);
            WaitForSingleObject(th, INFINITE);
            CloseHandle(th);
            assert(g_twActive == 0);
            Job(false);
            ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &b, NULL, 0, NULL, 0);  // no job: the trampoline
            assert(a.e - a.b == b.e - b.b && memcmp(a.b, b.b, (a.e - a.b) * 4) == 0);
            // the same world with the writer gone: the clone answers again, and the same
            IdVec c = b;
            c.b = (DWORD*)_aligned_malloc(8 * 2, 16);
            memcpy(c.b, a.b, 8);
            c.e = c.b + 2, c.c = c.b + 4;
            Job(true);
            ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &c, NULL, 0, NULL, 0);
            Job(false);
            assert(c.e - c.b == b.e - b.b && memcmp(c.b, b.b, (c.e - c.b) * 4) == 0);
            g_twAfter += k.steerTaken - tk0;  // read-only again (or an ordinary fall-back such as a lot miss)
            const long added = (long)(a.e - a.b) / 2 - 1;
            _aligned_free(a.b);
            _aligned_free(b.b);
            _aligned_free(c.b);
            return added;
        };
        // T-S7: the game's Footprint_SetLockKeyObject 0x807110 on a plain thread replaces fp+0x20; the old key's last reference
        long idsS7 = 0, idsT4 = 0;
        for (int i = 0; i < 200; i++) {
            *(DWORD**)keyOld = keyVt, *(LONG*)(keyOld + 4) = 1;
            *(DWORD**)keyNew = keyVt, *(LONG*)(keyNew + 4) = 1;
            *(u8**)(keyFp + 0x20) = keyOld;
            idsS7 += oneCase([](void*) -> DWORD {
                ((void(__fastcall*)(u8*, void*, u8*))(uintptr_t)0x00807110)(keyFp, NULL, keyNew);
                assert(*(u8**)(keyFp + 0x20) == keyNew && *(LONG*)(keyNew + 4) == 2 && *(LONG*)(keyOld + 4) == 1);
                assert(t_rg.n == 0 && t_twDepth == 0);  // left through the leave thunk (the destructor's ret 4 came back to it)
                return 0;
            }, 4);
        }
        // T4: the game's FootprintMapEntry_DeletingDtor 0x61ED10 (flag 0: no pool write) on the worker outside QL; its footprint
        // release (+0x94) blocks. Inside a W frame (QL depth 1) the same call is not counted.
        for (int i = 0; i < 200; i++) {
            *(DWORD*)entry = 0x00FEAC7C;
            *(u8**)(entry + 8) = relObj;
            *(DWORD**)relObj = relVt;
            idsT4 += oneCase([](void*) -> DWORD {
                g_workerTid = GetCurrentThreadId();
                ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0061ED10)(entry, NULL, 0);
                assert(*(DWORD*)entry == 0x0107396C && t_rg.n == 0 && t_twDepth == 0);
                const LONG bare = ts3native_counters.steerT4Bare;
                t_qlDepth = 1;
                *(u8**)(entry + 8) = NULL;
                ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0061ED10)(entry, NULL, 0);
                assert(ts3native_counters.steerT4Bare == bare && t_rg.n == 0);
                t_qlDepth = 0;
                g_workerTid = 0x7FFFFFFF;
                return 0;
            }, 3);
        }
        // F2 (0.18.0): arm / disarm races, the writer is the game's 0x807110 on a plain thread. Every answer given while a
        // writer that entered at or after the arm still runs must be dropped; a disarmed site must cost nothing.
        {
            const LONG f0 = k.steerS7Foreign;
            long beforeArm = 0, armedEarly = 0, endDrop = 0, f2Ids = 0;
            for (int i = 0; i < 100; i++) {
                // (a) no job, disarmed: the site goes straight to the trampoline (nothing counted, held or timed)
                const LONG tw0 = k.s2TwCalls, site0 = k.steerTwSite[4];
                KeyReset();
                SetEvent(g_wGo);  // TwKeyDtor does not block this time
                HANDLE wt = CreateThread(NULL, 0, KeyWriter, NULL, 0, NULL);
                WaitForSingleObject(wt, INFINITE);
                CloseHandle(wt);
                ResetEvent(g_wIn);
                assert(g_twArmed == 0 && g_twActive == 0 && k.s2TwCalls == tw0 && k.steerTwSite[4] == site0);
                // (b) that writer ran and left before the arm: the clone answers, and the same as the game's call
                BuildWorld();
                Agent ag = NewAgent(false);
                ag.nodes[0][0x30] = 1;
                IdVec a, b;
                NewIds(&a, 1);
                b = a;
                b.b = (DWORD*)_aligned_malloc(8 * 2, 16);
                memcpy(b.b, a.b, 8);
                b.e = b.b + 2, b.c = b.b + 4;
                ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &b, NULL, 0, NULL, 0);  // the reference: no job
                IdVec c = a;
                c.b = (DWORD*)_aligned_malloc(8 * 2, 16);
                memcpy(c.b, a.b, 8);
                c.e = c.b + 2, c.c = c.b + 4;
                LONG tk0 = k.steerTaken;
                Job(true);
                ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &c, NULL, 0, NULL, 0);
                Job(false);
                beforeArm += k.steerTaken - tk0;
                assert(c.e - c.b == b.e - b.b && memcmp(c.b, b.b, (c.e - c.b) * 4) == 0);
                // (c) a writer that enters after the arm but before A_RUNNING (back-off window) is counted: the clone drops
                KeyReset();
                TwArm(true);
                wt = CreateThread(NULL, 0, KeyWriter, NULL, 0, NULL);
                WaitForSingleObject(g_wIn, INFINITE);
                assert(g_twActive == 1);
                g_async.state = A_RUNNING;
                tk0 = k.steerTaken;
                const LONG d0 = k.steerTripDiscard;
                memcpy(c.b, a.b, 8);
                c.e = c.b + 2;
                ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &c, NULL, 0, NULL, 0);
                assert(k.steerTaken == tk0 && k.steerTripDiscard == d0 + 1);
                assert(c.e - c.b == b.e - b.b && memcmp(c.b, b.b, (c.e - c.b) * 4) == 0);
                armedEarly++;
                SetEvent(g_wGo);
                WaitForSingleObject(wt, INFINITE);
                CloseHandle(wt);
                Job(false);
                // (d) the job ends during the clone and a writer enters the disarmed site (mode 1), or the next job arms
                // again before the clone goes on (mode 2): the generation moved, the answer is dropped
                for (g_f2Mode = 1; g_f2Mode <= 2; g_f2Mode++) {
                    memcpy(c.b, a.b, 8);
                    c.e = c.b + 2;
                    tk0 = k.steerTaken;
                    const LONG e0 = k.s2JobEndDrop, r0 = g_f2Runs;
                    g_srActiveLot = F2ActiveLot;
                    Job(true);
                    ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &c, NULL, 0, NULL, 0);
                    g_srActiveLot = (SrActiveLotFn)(uintptr_t)0x00C88FD0;
                    if (g_f2Runs != r0) {  // the clone reached its lot lookup with the writer inside: dropped, the game's call ran
                        assert(k.steerTaken == tk0 && k.s2JobEndDrop == e0 + 1);
                        endDrop++;
                        SetEvent(g_wGo);
                        WaitForSingleObject(g_f2Writer, INFINITE);
                        CloseHandle(g_f2Writer);
                        g_f2Writer = NULL;
                    }
                    Job(false);
                    assert(c.e - c.b == b.e - b.b && memcmp(c.b, b.b, (c.e - c.b) * 4) == 0);
                }
                g_f2Mode = 0;
                f2Ids += (long)(b.e - b.b) / 2 - 1;
                _aligned_free(a.b);
                _aligned_free(b.b);
                _aligned_free(c.b);
            }
            printf("F2: arm/disarm 100 x: disarmed site not counted; writer gone before the arm -> %ld read-only answers, same ids; "
                   "writer entering after the arm, before A_RUNNING -> %ld dropped; job ending inside the clone with a writer in "
                   "the disarmed site -> %ld dropped (s2JobEndDrop %ld); %ld ids; contaminated answers taken 0\n", beforeArm,
                   armedEarly, endDrop, k.s2JobEndDrop, f2Ids);
            assert(beforeArm > 30 && armedEarly == 100 && endDrop > 30 && k.steerS7Foreign == f0 + 100 && g_twActive == 0 &&
                   g_twArmed == 0 && f2Ids > 5);
        }
        // the sim thread's own lock-key write during a job: counted apart, nothing dropped
        *(LONG*)(keyOld + 4) = 5;
        *(u8**)(keyFp + 0x20) = keyOld;
        const LONG sim0 = k.steerS7Sim, f0 = k.steerS7Foreign;
        Job(true);
        ((void(__fastcall*)(u8*, void*, u8*))(uintptr_t)0x00807110)(keyFp, NULL, keyNew);
        Job(false);
        assert(k.steerS7Sim == sim0 + 1 && k.steerS7Foreign == f0 && *(u8**)(keyFp + 0x20) == keyNew && g_twActive == 0);
        printf("F: 11 tripwire stubs on the game's prologues; T-S7 (0x807110 on another thread) and T4 (0x61ED10 on the worker "
               "outside QL) each 200 x: clone dropped, the game's call ran, same ids as with no job (%ld / %ld ids); T-S7 %ld, T4 %ld, "
               "sim %ld, dropped %ld; read-only again after the writer left %ld / 400\n", idsS7, idsT4, k.steerS7Foreign, k.steerT4Bare,
               k.steerS7Sim, k.steerTripDiscard, g_twAfter);
        assert(k.steerS7Foreign == 300 && k.steerT4Bare == 200 && k.steerTripDiscard == 500 && k.steerUntracked == 0 && g_twAfter > 200 &&
               idsS7 + idsT4 > 10);
        k.steerMode = 0;
    }
    // G. fix D-X (0.16.0, discourage.cpp, docs/84 §7.2): our call sites into the game's own ObjectService_FindByIdAddRef
    //    0x9395A0 (empty and one-object id hash), object release 0x7460F0, RoutingWorld_RemoveDiscourageFootprint 0x834860 (an
    //    object without a footprint component: lock, component lookup, unlock, ret 4), the lock-key find 0x82AD60 and
    //    LotRouting_RemoveObjectFootprints 0x635700 with lot 0 on fake id maps (the dead id's routing record has lost its lot:
    //    the erase path; a quadtree hit needs the lot quadtrees and is left to the game run)
    {
        struct GxLock {
            static LONG __fastcall Acq(void* lock, void*, const DWORD*) { ++*(LONG*)((u8*)lock + 0x20); return 1; }
            static LONG __fastcall Rel(void* lock, void*) { return --*(LONG*)((u8*)lock + 0x20); }
        };
        g_lockOrig = GxLock::Acq;
        g_rgRelOrig = GxLock::Rel;
        *(u8**)(uintptr_t)0x011D9128 = NULL;  // RoutingWorld_Get: the lot mark 0x62D9F0 at the end of 0x635700 returns at once
        u8* svc = (u8*)A(0x1040);             // ObjectService: 0x409 buckets at +0x10, end node [+0x1034] = 0
        *(u8**)(uintptr_t)0x011DC350 = svc;   // ObjectService_Get 0x938B50 reads it
        u8* world = (u8*)A(0xA00);
        world[0x974] = 1;
        u8* p = (u8*)A(0x200);
        *(u8**)(world + 0x95C) = p;
        const DWORD dead = 0x5151, live = 0x6262;
        // P+0x3C and P+0x24: id maps (buckets +4, bucket count +8, size +0xC; node: key +0/+4, value +8, next +0x10)
        auto newMap = [](DWORD n, DWORD size) {
            u8* m = (u8*)A(0x20);
            DWORD* bk = (DWORD*)A((n + 1) * 4);
            bk[n] = kSentinel;
            *(DWORD**)(m + 4) = bk;
            *(DWORD*)(m + 8) = n;
            *(DWORD*)(m + 0xC) = size;
            return m;
        };
        u8* m3c = newMap(3, 0);
        u8* m24 = newMap(3, 1);
        *(u8**)(p + 0x3C) = m3c;
        *(u8**)(p + 0x24) = m24;
        u8* pool = (u8*)A(0x20);     // node pool of the map (0x6281A0 pushes freed nodes on its list +0x10)
        *(u8**)(m24 + 0x1C) = pool;
        u8* rec = (u8*)A(0x20);      // the id's per-lot routing record: owner +0xC gone
        *(LONG*)(rec + 4) = 100;     // references (never reach 0: its deleting destructor is not ours to run)
        u8* node = (u8*)A(0x20);
        *(DWORD*)node = dead;
        *(u8**)(node + 8) = rec;
        (*(DWORD**)(m24 + 4))[dead % 3] = (DWORD)(uintptr_t)node;
        // the lock-key map world+0x8B8 (buckets +0x8BC, count +0x8C0, size +0x8C4; node: key +0/+4, next +0x20): holds the dead id
        DWORD* kb = (DWORD*)A(3 * 4);
        u8* kn = (u8*)A(0x28);
        *(DWORD*)kn = dead;
        kb[dead % 2] = (DWORD)(uintptr_t)kn;
        kb[2] = kSentinel;
        *(DWORD**)(world + 0x8BC) = kb;
        *(DWORD*)(world + 0x8C0) = 2;
        *(DWORD*)(world + 0x8C4) = 1;
        world[0x994] = 1;
        // a live object without a footprint component, in the id hash
        u8* obj = (u8*)A(0x80);
        *(LONG*)(obj + 4) = 5;
        *(DWORD*)(obj + 8) = live;
        *(DWORD*)(svc + 0x10 + (live % 0x409) * 4) = (DWORD)(uintptr_t)obj;
        u8* deadObj = (u8*)A(0x80);  // the destroyed object: no longer in the hash
        *(DWORD*)(deadObj + 8) = dead;
        g_discRemoveOrig = (DiscRemoveFn)(uintptr_t)DISC_REMOVE;  // the game's code (no detour on the mapped image)
        g_rgLock = world + 0x918;
        g_async.owner = GetCurrentThreadId();
        Job(false);
        k.discMode = 1;
        k.discDestroyMode = 1;
        k.discSweepMode = 1;
        // the dead id's Remove at the drain: dropped, the sweep runs the game's 0x635700 (erase path, nothing removed)
        Job(true);
        assert(DiscRemoveHook(world, NULL, deadObj) == 1 && g_discCount == 1);
        Job(false);
        DiscPoll();
        assert(g_discCount == 0 && k.discDropRemove == 1 && k.discDeadSweep == 1 && k.discDeadHit == 0 && k.discDeadKeyMap == 1);
        assert((*(DWORD**)(m24 + 4))[dead % 3] == 0 && *(DWORD*)(m24 + 0xC) == 0 && world[0x994] == 0);
        assert(*(LONG*)(rec + 4) == 99 && *(u8**)(pool + 0x10) == node);  // the map's reference dropped, node back in the pool
        assert(*(LONG*)(world + 0x918 + 0x20) == 0);
        // the destroy gate on the live object: the game's Remove runs (returns 0 with no footprint component), reference balanced
        Job(true);
        DiscRemoveHook(world, NULL, obj);
        DiscRemoveHook(world, NULL, deadObj);
        Job(false);
        DiscOnDestroy(live, 0, true);
        assert(k.discDestroyApplied == 1 && k.discApplied == 1 && g_discCount == 1 && *(LONG*)(obj + 4) == 5);
        assert(*(LONG*)(world + 0x918 + 0x20) == 0);
        DiscPoll();  // the dead id again: the map no longer has it, the sweep finds nothing
        assert(g_discCount == 0 && k.discDeadSweep == 2 && k.discDeadHit == 0 && k.discDropRemove == k.discDeadSweep);
        printf("G: D-X on the game's code: dead-id sweep 0x635700 (lot 0, erase path) + lock-key find 0x82AD60 (%ld key-map "
               "hit); destroy gate ran the game's Remove 0x834860 through FindByIdAddRef / release, references balanced; T-D5 %ld = %ld\n",
               k.discDeadKeyMap, k.discDropRemove, k.discDeadSweep);
        g_async.owner = 0;
        g_rgLock = NULL;
        k.discMode = 0;
    }
    // K. design SH (slotro.cpp, docs/90): the gate-skip decision and the interlocked portal count pair on the game's own code.
    // SH runs the game's own 0x76F0A0 body under a QL-shared hold; the hold changes no computation, it only lets RgEnter skip
    // the +0x918 wait at gate 12 (LotTable_Contains) and gate-0 hits (GetOrInsert). So equal answers are structural; the real
    // correctness is (a) SlGate's decision matches the game's Contains / GetOrInsert on random tables, and (b) PortalAddRefX /
    // PortalReleaseX behave as the game's 0x64E8E0 / 0x64E8F0 under contention. Both run the mapped game functions. Seeds 1-4.
    {
        typedef u8(__fastcall * ContainsFn)(const u8*, void*, DWORD, DWORD);        // 0x628A90, ret 8
        typedef const u8*(__fastcall * GetOrInsertFn)(const u8*, void*, const DWORD*);  // 0x62EAB0 hit, ret 4
        typedef LONG(__fastcall * RefFn)(u8*, void*);
        assert(InstallPortalRefX());  // the interlocked pair into vtable 0xFED048
        assert(k.slRefSwap == 0);     // set only by InstallSlotRo; here we installed the pair directly
        const RefFn gAdd = (RefFn)(uintptr_t)0x0064E8E0, gRel = (RefFn)(uintptr_t)0x0064E8F0;
        static u8 fwm[0x200], fact[0x60];
        *(u8**)(uintptr_t)0x011ECBC4 = fwm;
        *(u8**)(fwm + 0xB0) = fact;
        long gateCases = 0, pass12 = 0, pass0 = 0, drop = 0, refCases = 0;
        for (int seed = 1; seed <= 4; seed++) {
            g_seed = (DWORD)seed;
            for (int c = 0; c < 3000; c++) {
                // a random table: cnt buckets, several lots, each a node whose entry (node+0x10) repeats the key
                const DWORD cnt = 1 + Rnd(8);
                u8* p = (u8*)A(0x200);
                DWORD* bk = (DWORD*)A((cnt + 1) * 4);
                bk[cnt] = kSentinel;
                *(DWORD**)(p + 0x4C) = bk;
                *(DWORD*)(p + 0x50) = cnt;
                const int nLots = 1 + (int)Rnd(8);
                DWORD lo[8], hi[8];
                for (int i = 0; i < nLots; i++) {
                    lo[i] = 1 + Rnd(cnt * 4) + (DWORD)i * 0x100;
                    hi[i] = Rnd(3) ? 0 : Rnd(2);
                    u8* node = (u8*)A(0x200);
                    *(DWORD*)node = lo[i], *(DWORD*)(node + 4) = hi[i];
                    u8* e = node + 0x10;
                    *(DWORD*)e = lo[i], *(DWORD*)(e + 4) = hi[i];
                    const int np = (int)Rnd(4);
                    if (np) {
                        DWORD* v = (DWORD*)A(4 * np);
                        for (int j = 0; j < np; j++) {
                            u8* pt = (u8*)A(0x80);
                            *(DWORD*)pt = LT_PORTAL_VT;
                            *(DWORD*)(pt + 8) = 0x5000 + Rnd(2), *(DWORD*)(pt + 0xC) = 7;
                            *(LONG*)(pt + 0x70) = Rnd(8) ? 1 + (LONG)Rnd(3) : 0;  // rarely a count the clone must not Release
                            v[j] = (DWORD)(uintptr_t)pt;
                        }
                        *(DWORD**)(e + 0x44) = v, *(DWORD**)(e + 0x48) = v + np;
                    }
                    const DWORD b = lo[i] % cnt;
                    *(DWORD*)(node + 0x190) = bk[b];
                    bk[b] = (DWORD)(uintptr_t)node;
                }
                *(DWORD*)(fact + 0x48) = Rnd(8) ? 0x77777777 : lo[Rnd(nLots)];
                *(DWORD*)(fact + 0x4C) = 0;
                // (a) gate 12: the game's Contains vs LtFind's hit/miss; a hit also feeds SlGate via a PortalsForObject frame
                DWORD qlo, qhi;
                const DWORD r = Rnd(10);
                if (r == 0) qlo = qhi = 0;
                else if (r == 1) { qlo = *(DWORD*)(fact + 0x48); qhi = *(DWORD*)(fact + 0x4C); }
                else if (r < 4) { qlo = Rnd(0x10000); qhi = Rnd(2); }
                else { const int i = (int)Rnd(nLots); qlo = lo[i]; qhi = hi[i]; }
                const bool gameHas = ((ContainsFn)(uintptr_t)0x00628A90)(p, NULL, qlo, qhi) != 0;
                const u8* e;
                const int why = LtFind(p, qlo, qhi, &e);
                assert(why == LT_OK && (e != NULL) == gameHas);  // the clone's find matches the game's Contains
                DWORD fr[16] = {};
                DWORD* slot = fr + 4;
                slot[-2] = (DWORD)(uintptr_t)p, slot[0] = SL_PFO_RET, slot[1] = qlo, slot[2] = qhi, slot[9] = 0x5000, slot[10] = 7;
                t_slHold = 1;  // SlGate drops the hold on a bad portal; set it each time
                t_qlShared = 1;
                AcquireSRWLockShared(&g_ql);
                const bool p12 = SlGate(12, slot);
                if (t_slHold == 1) { t_qlShared = 0; ReleaseSRWLockShared(&g_ql); t_slHold = 0; }
                // a pass is sound whenever the object's portals (if the lot is present) are the interlocked class with count >= 1
                bool badPortal = false;
                if (e)
                    for (const DWORD* it = *(const DWORD* const*)(e + 0x44); it && it != *(const DWORD* const*)(e + 0x48); it++) {
                        const u8* pt = (const u8*)(uintptr_t)*it;
                        if (pt && *(const DWORD*)(pt + 8) == slot[9] && *(const DWORD*)(pt + 0xC) == 7 &&
                            *(const LONG*)(pt + 0x70) < 1)
                            badPortal = true;
                    }
                assert(p12 == !badPortal);
                p12 ? pass12++ : drop++;
                gateCases++;
                // (b) gate 0: on a present key, the game's GetOrInsert hit returns node+0x10; SlGate must pass with the same entry
                if (e) {
                    DWORD key[2] = {qlo, qhi};
                    const u8* gi = ((GetOrInsertFn)(uintptr_t)0x0062EAB0)((const u8*)(p + 0x48), NULL, key);
                    assert(gi == e);  // the game's hit node+0x10 equals LtFind's entry
                    DWORD f0[16] = {};
                    DWORD* s0 = f0 + 4;
                    s0[-2] = (DWORD)(uintptr_t)(p + 0x48), s0[0] = 0x0062F7A2, s0[1] = (DWORD)(uintptr_t)key;
                    t_slHold = 1;
                    t_qlShared = 1;
                    AcquireSRWLockShared(&g_ql);
                    const bool g0ok = SlGate(0, s0);
                    if (t_slHold == 1) { t_qlShared = 0; ReleaseSRWLockShared(&g_ql); t_slHold = 0; }
                    assert(g0ok);
                    pass0++;
                }
                // (c) the interlocked count pair vs the game's, on a live portal
                u8* pt = (u8*)A(0x80);
                *(DWORD*)pt = LT_PORTAL_VT;
                *(LONG*)(pt + 0x70) = 1;
                const LONG a1 = gAdd(pt, NULL), a2 = PortalAddRefX(pt, NULL);  // 2 then 3
                assert(a1 == 2 && a2 == 3);
                const LONG r1 = PortalReleaseX(pt, NULL), r2 = gRel(pt, NULL);  // 2 then 1
                assert(r1 == 2 && r2 == 1 && *(LONG*)(pt + 0x70) == 1);
                refCases++;
            }
        }
        // restore the plain pair so later code (none here) sees the game's vtable
        DWORD* vt = (DWORD*)(uintptr_t)LT_PORTAL_VT;
        vt[0] = 0x0064E8E0, vt[1] = 0x0064E8F0;
        printf("K: SH gate decision on the game's Contains / GetOrInsert: %ld cases (gate-12 pass %ld, drop %ld; gate-0 pass %ld), "
               "interlocked portal pair vs the game's: %ld, all equal\n", gateCases, pass12, drop, pass0, refCases);
        assert(gateCases == 4 * 3000 && pass12 > gateCases / 4 && drop > 0 && pass0 > 0);
        g_seed = 12345;
    }
    // H. design H2 (locowin.cpp, docs/86 §6): InstallLocoWindow on the game's bytes at 0xD81C5C (refused when one differs);
    // the patched rel32 called the way MonoScriptHost_Simulate calls it (ecx, one float pushed, callee pops it); the mapped
    // 0x83CED0 replaced by a fake pass that runs the game's Locomotion_ObstacleCheck: answers equal to the call without the
    // window, the window up only inside (Mode 1) or never (Mode 0), nothing left open after
    {
        static const u8 kUpdatePro[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0x84, 0x00, 0x00, 0x00};
        assert(Matches((const void*)(uintptr_t)LOCO_UPDATE, kUpdatePro, sizeof(kUpdatePro)));
        assert(Matches((const void*)(uintptr_t)0x0083D383, (const u8*)"\xC2\x04\x00", 3));  // its one return: ret 4
        wchar_t ini[MAX_PATH];
        GetTempPathW(MAX_PATH, ini);
        wcscat_s(ini, L"steerro-test-h2.ini");
        WritePrivateProfileStringW(L"LocoWindow", L"Enabled", L"1", ini);
        WritePrivateProfileStringW(L"LocoWindow", L"Mode", L"1", ini);
        const LONG enf = k.rgEnforce, ready = k.asyncReady;
        k.rgEnforce = k.asyncReady = 1;
        u8* site = (u8*)(uintptr_t)LOCO_CALL;
        const LONG rel0 = *(LONG*)(site + 1);
        site[-4] = 0x90;  // `push ecx` changed: refused, nothing written
        InstallLocoWindow(ini);
        assert(k.g0wReady == 0 && *(LONG*)(site + 1) == rel0);
        site[-4] = 0x51;
        InstallLocoWindow(ini);
        DeleteFileW(ini);
        const DWORD target = LOCO_CALL + 5 + *(LONG*)(site + 1);
        assert(k.g0wReady == 1 && k.g0wMode == 1 && target == (DWORD)(uintptr_t)LocoWinThunk && g_g0wOrig == (LocoUpdateFn)(uintptr_t)LOCO_UPDATE);
        // the caller's sequence: mov ecx,self; fld [dt]; push ecx; fstp [esp]; call <the site's target>; ret
        static float hDt = 0.25f;
        static void* const kSelf = (void*)0x5A5A0001;
        u8* stub = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        u8* p = stub;
        *p++ = 0xB9, *(void**)p = kSelf, p += 4;
        *p++ = 0xD9, *p++ = 0x05, *(float**)p = &hDt, p += 4;
        *p++ = 0x51, *p++ = 0xD9, *p++ = 0x1C, *p++ = 0x24;
        *p++ = 0xE8, *(LONG*)p = (LONG)(target - (DWORD)(uintptr_t)(p + 4)), p += 4;
        *p++ = 0xC3;
        static u8* hAgent;
        static IdVec* hIds;
        static long hRuns;
        struct H {
            static void __fastcall Pass(void* self, void*, float dt) {
                const Ts3nCounters& c = ts3native_counters;
                assert(self == kSelf && dt == hDt && g_g0wIn == 1 && g_g0Win == (c.g0wMode == 1 ? 1 : 0) && t_g0wDepth == 1);
                ((GameLocoFn)(uintptr_t)STEER_LOCO)(hAgent, NULL, hIds, NULL, 0, NULL, 0);
                hRuns++;
            }
        };
        Jmp(LOCO_UPDATE, (void*)H::Pass);
        const int hCases = cases;
        long equal = 0, withIds = 0;
        for (int mode = 1; mode >= 0; mode--) {
            k.g0wMode = mode;
            for (int i = 0; i < hCases; i++) {
                BuildWorld();
                Agent ag = NewAgent(Rnd(4) == 0);
                IdVec a, b;
                NewIds(&a, 2);
                b = a;
                b.b = (DWORD*)_aligned_malloc(8 * 3, 16);
                memcpy(b.b, a.b, 16);
                b.e = b.b + 4, b.c = b.b + 6;
                hAgent = ag.a;
                hIds = &a;
                ((void(__cdecl*)())stub)();
                ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &b, NULL, 0, NULL, 0);  // the same call, no window
                assert(a.e - a.b == b.e - b.b && memcmp(a.b, b.b, (a.e - a.b) * 4) == 0);
                assert(g_g0Win == 0 && g_g0wIn == 0 && t_g0wDepth == 0);
                equal++;
                withIds += a.e - a.b > 4;
                _aligned_free(a.b);
                _aligned_free(b.b);
            }
        }
        assert(hRuns == 2 * hCases && k.g0wPasses == 2 * hCases && withIds > 0 && g_rgOffSimOpen == 0);
        assert(k.g0wJobsHeld == 0 && k.g0wJobStartInPass == 0 && k.g0wDrop[0] == 0 && k.g0wDrop[1] == 0 && k.g0wWatchdog == 0);
        printf("H: loco window on the game's call site 0xD81C5C (refused on a changed byte); %ld passes through the patched rel32 "
               "(Mode 1 and 0), the game's 0x85FD10 inside: %ld answers equal to the call without the window (%ld with new ids); "
               "window closed after each\n", hRuns, equal, withIds);
        k.rgEnforce = enf;
        k.asyncReady = ready;
        k.g0wReady = 0;
    }
    // I. design A10 (a10.cpp, docs/87 §6): the four route ICs' bytes, Route_IsActive reads route+0x60 and
    // Route_StopFollowingRoute's helper clears it; InstallA10 on the game's prologues (refused per IC when a byte differs);
    // each patched IC called the way Mono calls an internal call (cdecl, hidden result pointer, floats by value) reaches our
    // hook with its arguments and, with no job, the game's own IC through the trampoline (route manager 0x11D40C4 is NULL in
    // the mapped image: the IC's own "no manager" path, status 2), stack balanced, result pointer returned. The job queue
    // itself (kinds, collect rules, D, destroy, random order) runs in selftest TestA10 on fake originals.
    {
        assert(Matches((const void*)(uintptr_t)IC_ROUTE_PLAN, kIcPlanPrologue, sizeof(kIcPlanPrologue)));
        static const u8 kIsActive[] = {0x8B, 0x0D, 0xC4, 0x40, 0x1D, 0x01, 0x85, 0xC9, 0x56, 0x74, 0x2B};
        assert(Matches((const void*)(uintptr_t)0x0074CFC0, kIsActive, sizeof(kIsActive)));
        assert(Matches((const void*)(uintptr_t)0x0074CFEA, (const u8*)"\x8A\x5E\x60", 3));          // mov bl,[esi+60h]
        assert(Matches((const void*)(uintptr_t)0x00753294, (const u8*)"\xC6\x47\x60\x00", 4));      // mov byte [edi+60h],0
        assert(callsTo(0x0075766C, 0x00753230));  // in IC_Route_StopFollowingRoute 0x757640
        assert(callsTo(0x007654C8, 0x007641D0) && callsTo(0x007655C3, 0x007642C0) && callsTo(0x0076542C, 0x007642C0));
        assert(*(const DWORD*)(uintptr_t)0x011D40C4 == 0);
        wchar_t ini[MAX_PATH];
        GetTempPathW(MAX_PATH, ini);
        wcscat_s(ini, L"steerro-test-a10.ini");
        WritePrivateProfileStringW(L"RouteAsync", L"Kinds", L"7", ini);
        const LONG ready = k.asyncReady;
        const IcPlanFn savedIc = g_icPlanOrig;
        k.asyncReady = 1;
        g_icPlanOrig = (IcPlanFn)(uintptr_t)IC_ROUTE_PLAN;
        u8* p3 = (u8*)(uintptr_t)kP10Ic[3];
        p3[5] = 0xF8;  // ReplanFromPosition's `and esp,-16` changed: that IC alone is refused
        InstallA10(ini);
        assert(k.arReady == (kArBuilt & 3) && k.arMode == 1 && p3[0] == 0x55);
        p3[5] = 0xF0;
        WritePrivateProfileStringW(L"RouteAsync", L"Kinds", L"4", ini);
        InstallA10(ini);
        DeleteFileW(ini);
        assert(k.arReady == kArBuilt);
        for (int i = 1; i < 4; i++) {
            if (!(kArBuilt >> (i - 1) & 1)) continue;
            const u8* e = (const u8*)(uintptr_t)kP10Ic[i];
            assert(e[0] == 0xE9 && kP10Ic[i] + 5 + *(const LONG*)(e + 1) == (DWORD)(uintptr_t)kArHooks[i]);
            assert(memcmp(g_arOrig[i], kP10Pro[i], 6) == 0);
        }
        static u8 res[0x48];
        static DWORD iEsp0, iEsp1, iOut, iFn;
        static float fx = 1.5f, fy = -2.0f, fz = 3.25f;
        for (int i = 1; i < 4; i++) {
            if (!(kArBuilt >> (i - 1) & 1)) continue;
            memset(res, 0xCC, sizeof(res));
            iFn = kP10Ic[i];
            if (i == 1) {
                __asm {
                    mov iEsp0, esp
                    push 0x25000077
                    push offset res
                    call iFn
                    add esp, 8
                    mov iOut, eax
                    mov iEsp1, esp
                }
            } else {
                __asm {
                    mov iEsp0, esp
                    push fz
                    push fy
                    push fx
                    push 0x25000077
                    push offset res
                    call iFn
                    add esp, 20
                    mov iOut, eax
                    mov iEsp1, esp
                }
            }
            assert(iEsp0 == iEsp1 && iOut == (DWORD)(uintptr_t)res && *(const DWORD*)res == 2 && *(const DWORD*)(res + 4) == 0);
            assert(k.arSyncN[i - 1] == 1 && k.arStarted[i - 1] == 0);
        }
        printf("I: A10 detours on the game's three IC prologues (refused on a changed byte); IC calls through them reach the "
               "hook and, with no job, the game's IC via the trampoline (status 2, stack balanced); Route_IsActive / "
               "route+0x60 bytes and the start-point call edges match\n");
        k.asyncReady = ready;
        g_icPlanOrig = savedIc;
        memset(&k.arReady, 0, sizeof(LONG) * 32);
    }
    // J. [ObstructedRO] (obstructro.cpp, docs/86 §10): the game's IC_Route_IsPointObstructedBySim 0x751480 (its own transform
    // 0x62EF30, obstacle query 0x82EDB0, ignore list 0x81C970, point test 0x80F030 -> 0x67B130) vs OrRun on the section A
    // worlds; then the detour on the mapped 0x751480: job -> clone, no job -> the game's code, shadow, QL busy -> the game's code
    {
        assert(Matches((const void*)(uintptr_t)OR_IC, kOrPro, sizeof(kOrPro)));
        // the IC's call edges the clone mirrors (rel32 targets read from the image)
        assert(callsTo(0x007514A3, 0x0074B2C0) && callsTo(0x007514BF, 0x008292D0) && callsTo(0x007514CA, 0x00829BC0) &&
               callsTo(0x0075156E, 0x00C68990) && callsTo(0x007515A0, 0x00ADD860) && callsTo(0x0075167A, 0x0062EF30) &&
               callsTo(0x007516B3, 0x0082EDB0) && callsTo(0x00751739, 0x0081C970));
        Jmp(0x0074B2C0, (void*)I::Route);
        Jmp(0x00ADD860, (void*)I::Level);
        Jmp(0x00B015B0, (void*)I::Inside);
        Jmp(0x0074A8F0, (void*)I::Height);
        static u8 rmObj[0x10];
        *(u8**)(uintptr_t)0x011D40C4 = rmObj;
        static DWORD rvt[8];
        rvt[4] = (DWORD)(uintptr_t)I::Ref;
        rvt[5] = (DWORD)(uintptr_t)I::Unref;
        I::route = (u8*)A(0x200);
        *(DWORD**)I::route = rvt;
        static DWORD ign[8];
        auto newCase = [&](OrV3* at) {
            BuildWorld();
            for (int i = 0; i < W.nfp; i++) if (Rnd(2)) W.fps[i][0x35] = 0x14;  // more Sims (classes with the +0x35 type)
            const int lot = Rnd(2);
            float x, z;
            WorldPoint(lot, Rf(-4, 68), Rf(-4, 68), &x, &z);
            if (lot == 0 && x >= 0) x = -0.5f;
            if (lot == 1 && x < 0) x = 0.5f;
            if (Rnd(8) == 0) x += lot == 0 ? 50 : 150;  // another lot's frame, or lot 2 (not in the table)
            *at = OrV3{x, Rf(0, 2), z};
            u8* r = I::route;
            *(DWORD*)(r + 0x3C) = Rnd(0x10000);
            const u8* own = W.fps[Rnd(W.nfp)];  // the route's own Sim: one of the ids in play, sometimes
            *(DWORD*)(r + 0x40) = Rnd(3) ? *(const DWORD*)(own + 0x14) : 0x7777;
            *(DWORD*)(r + 0x44) = Rnd(3) ? *(const DWORD*)(own + 0x18) : 0;
            const int ni = (int)Rnd(4);
            for (int i = 0; i < ni; i++) { ign[i * 2] = 0x1000 + Rnd(40); ign[i * 2 + 1] = Rnd(3) ? 0 : 7; }
            *(DWORD**)(r + 0x158 + 0x38) = ign;
            *(DWORD**)(r + 0x158 + 0x3C) = ign + ni * 2;
        };
        g_async.owner = me;
        g_workerTid = me + 7;
        long cmp = 0, hits = 0, fb[8] = {};
        for (int c = 0; c < cases; c++) {
            OrV3 at;
            newCase(&at);
            u64 mine;
            const int i0 = I::refIn, o0 = I::refOut;
            const int why = OrRun(0x25000001, at, false, &mine);  // the clone first: it must not change what the game reads
            assert(I::refIn - i0 == 1 && I::refOut - o0 == 1);
            const u64 game = ((OrFn)(uintptr_t)OR_IC)(0x25000001, at);
            assert(I::refIn - i0 == 2 && I::refOut - o0 == 2);
            for (int i = 0; i < W.nfp; i++) assert(*(LONG*)(W.fps[i] + 8) == 100);  // the game's references are balanced
            fb[why < 8 ? why : 7]++;
            if (why) continue;
            if (game != mine) {
                printf("MISMATCH obstructed: case %d at (%.3f, %.3f): game %08lX:%08lX, clone %08lX:%08lX\n", c, at.x, at.z, (DWORD)(game >> 32),
                       (DWORD)game, (DWORD)(mine >> 32), (DWORD)mine);
                exit(1);
            }
            cmp++;
            hits += game != 0;
        }
        assert(((OrFn)(uintptr_t)OR_IC)(0x25000009, OrV3{0, 0, 0}) == 0);  // no route: 0, nothing else read
        printf("I: game 0x751480 vs clone: %ld cases, %ld compared equal (%ld name a Sim); clone gave up: lot %ld, deferred %ld, "
               "cache %ld, shape %ld, other %ld\n", (long)cases, cmp, hits, fb[SR_FB_LOTMISS], fb[SR_FB_DEFERRED], fb[SR_FB_CACHE],
               fb[SR_FB_SHAPE], cases - cmp - fb[SR_FB_LOTMISS] - fb[SR_FB_DEFERRED] - fb[SR_FB_CACHE] - fb[SR_FB_SHAPE]);
        assert(cmp > cases / 2 && hits > cases / 20);
        // the detour on the mapped 0x751480 (tripwire and W stubs in place since C and F): job -> clone, no job -> the game's code
        g_orOrig = (OrFn)InstallDetour("IC_Route_IsPointObstructedBySim", OR_IC, kOrPro, sizeof(kOrPro), (void*)OrHook);
        assert(g_orOrig);
        k.orMode = 1;
        long orN = 0;
        for (int c = 0; c < cases; c++) {
            OrV3 at;
            newCase(&at);
            const LONG t0 = k.orTaken;
            Job(true);
            const u64 a = ((OrFn)(uintptr_t)OR_IC)(0x25000001, at);
            Job(false);
            const u64 b = ((OrFn)(uintptr_t)OR_IC)(0x25000001, at);  // no job: the trampoline
            assert(a == b);
            orN += k.orTaken - t0;
        }
        assert(orN > cases / 2 && I::refIn == I::refOut);
        k.orShadow = 1;
        for (int c = 0; c < cases; c++) {
            OrV3 at;
            newCase(&at);
            ((OrFn)(uintptr_t)OR_IC)(0x25000001, at);
        }
        assert(k.orShadowRuns > cases / 2 && k.orShadowMismatch == 0 && g_rgOffSimOpen == 0);
        k.orShadow = 0;
        // a worker writer holds QL: the hook falls back to the game's code, same answer
        g_wIn = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_wGo = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE qh = CreateThread(NULL, 0, [](void*) -> DWORD {
            AcquireSRWLockExclusive(&g_ql);
            SetEvent(g_wIn);
            WaitForSingleObject(g_wGo, 5000);
            ReleaseSRWLockExclusive(&g_ql);
            return 0;
        }, NULL, 0, NULL);
        WaitForSingleObject(g_wIn, INFINITE);
        long busy = 0;
        for (int c = 0; c < 50; c++) {
            OrV3 at;
            newCase(&at);
            const LONG f0 = k.orFb[OR_FB_TRY - 1];
            Job(true);
            const u64 a = ((OrFn)(uintptr_t)OR_IC)(0x25000001, at);
            Job(false);
            busy += k.orFb[OR_FB_TRY - 1] - f0;
            assert(a == ((OrFn)(uintptr_t)OR_IC)(0x25000001, at));  // no job: the trampoline
        }
        SetEvent(g_wGo);
        WaitForSingleObject(qh, INFINITE);
        CloseHandle(qh);
        printf("I: hook %ld read-only answers equal to the game's; shadow %ld runs, 0 mismatches; QL busy %ld x -> game's call; route "
               "pair balanced (%d / %d)\n", orN, k.orShadowRuns, busy, I::refIn, I::refOut);
        assert(busy > 25);
        k.orMode = 0;
    }
    printf("visitor branches: masked %ld, leaf %ld, children %ld\n", g_srVisitKind[0], g_srVisitKind[1], g_srVisitKind[2]);
    printf("steerro-test ok\n");
    return 0;
}
