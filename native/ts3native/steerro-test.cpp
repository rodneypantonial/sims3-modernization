// Offline check for design S2 (docs/84 §3.6). Maps TS3W.exe at its own addresses in this process (all sections; the game's
// code runs read-only on fake data), with only these replaced: the GeneralAllocator entry points (CRT heap), Lock_Acquire /
// Lock_Release (no-ops), 0x85AE60 (copies the node point, level from its w) and World_LotLookup (rectangles). It builds random
// lots, lot tables, quadtrees and footprints with the game's own vtables, then:
//   A. compares the game's Locomotion_ObstacleCheck 0x85FD10 with the read-only clone SrRun (appended ids, in order) and the
//      game's RoutingWorld_ObstacleQuery 0x82EDB0 with the clone's point query (footprint sub-objects, in order);
//   B. installs the S2 detour on the mapped 0x85FD10 and checks the hook (job running: clone; trampoline otherwise), shadow mode
//      and every fallback reason;
//   C. installs the 12 worker writer stubs on the mapped W1-W6 prologues (the bytes must match) and runs the game's
//      Footprint_EnsureLevelPolygons 0x80E5B0 through its stub on a "worker" thread: QL is held exclusive inside, released
//      after; while it is held the hook falls back (QL busy) and still answers the same ids;
//   D. checks the lot transforms 0x62ED40 / 0x62EF30 against SrXform bit for bit;
//   E. design L1 (lotfd.cpp, docs/84 §4.6): the game's LotRouting_FrontDoorPortal 0x631800 vs LtFrontDoor on random lot
//      tables (EDX:EAX, *out, portal counts unchanged; AddRef == Release through the T-L3 thunks in vtable 0xFED048), then the
//      L1 detour on the mapped 0x631800 (job: clone, else the game's code), shadow mode, and QL busy -> the game's code.
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
    u8* fps[96];
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
        W.lots[i] = (u8*)A(0x60);
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

// local (u, v) in lot i -> world (x, z): the inverse of the rows (rotation by ang, translation)
static void WorldPoint(int lot, float u, float v, float* x, float* z) {
    const float* r = (const float*)(W.entries[lot] + 0xA0);
    const float c = r[0], s = r[2], tx = r[12], tz = r[14];
    // u = x c - z s + tx, v = x s + z c + tz
    const float du = u - tx, dv = v - tz;
    *x = c * du + s * dv;
    *z = -s * du + c * dv;
}

struct Agent { u8* a; u8* nodes[6]; int nn; };
static Agent NewAgent(bool otherLot) {
    Agent ag = {};
    ag.a = (u8*)A(0x100);
    u8* head = ag.a + 8;
    ag.nn = 1 + Rnd(5);
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
        n[0x30] = (u8)(Rnd(3) != 0);
    }
    *(u8**)prev = head;
    *(float*)(ag.a + 0x40) = Rnd(4) ? Rf(0.2f, 6) : -1.0f;  // -1: the obstacle query finds nothing, so nothing is excluded
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
static int OneCase() {
    BuildWorld();
    Agent ag = NewAgent(Rnd(4) == 0);
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
        g_async.state = A_RUNNING;
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &a, NULL, 0, NULL, 0);
        g_async.state = A_IDLE;
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
        g_async.state = A_RUNNING;
        for (int i = 0; i < ag.nn; i++) { float* p = (float*)(ag.nodes[i] + 0x10); p[0] = -0.5f; p[2] = 0; }
        k.rgOwnerTid = (LONG)me;
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &v, NULL, 0, NULL, 0);
        k.rgOwnerTid = 0;
        g_inInvoke = 1;
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &v, NULL, 0, NULL, 0);
        g_inInvoke = 0;
        g_async.state = A_IDLE;
        assert(k.steerFb[SR_FB_STATE - 1] == st0 + 2);
        _aligned_free(v.b);
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
        g_async.state = A_RUNNING;
        ((GameLocoFn)(uintptr_t)STEER_LOCO)(ag.a, NULL, &a, NULL, 0, NULL, 0);
        assert(k.steerFb[SR_FB_TRY - 1] == busy0 + 1);
        // a sim-thread cache rebuild during a job is counted (here on a valid cache: no rebuild, the stub still counts)
        const LONG sc0 = k.steerSimCacheWrite;
        u8* fp = W.fps[0];
        ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0080E5B0)(fp, NULL, 0);
        assert(k.steerSimCacheWrite == sc0 + 1);
        g_async.state = A_IDLE;
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
        g_async.state = A_RUNNING;
        ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0080E5B0)(g_wFp, NULL, 0);
        g_async.state = A_IDLE;
        assert(g_caAcq == 1 && g_caRebuilds == 2 && cache[0x20] == 1 && *(LONG*)(g_caLock + 0x20) == 0 && t_rg.n == 0);
        assert(k.caSimInJob[0] == 1 && k.caWaits == 1 && k.caQlHeld == 0);
        // a valid level: no lock, nothing counted
        g_async.state = A_RUNNING;
        ((void(__fastcall*)(u8*, void*, DWORD))(uintptr_t)0x0080E5B0)(g_wFp, NULL, 0);
        g_async.state = A_IDLE;
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
                g_async.state = A_RUNNING;  // T-L3 counts only during a job
                const u64 r1 = ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, type, withOut ? &out1 : NULL);
                g_async.state = A_IDLE;
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
            g_async.state = A_RUNNING;
            const u64 a = ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, type, NULL);
            g_async.state = A_IDLE;
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
        g_async.state = A_RUNNING;
        const u64 a = ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, 0, NULL);
        g_async.state = A_IDLE;
        SetEvent(g_wGo);
        WaitForSingleObject(qh, INFINITE);
        CloseHandle(qh);
        assert(k.ltFb[LT_FB_TRY - 1] == 1 && a == ((GameFdFn)(uintptr_t)LT_FD)(p, NULL, lo, hi, 0, NULL));
        printf("E: L1 hook %ld read-only answers equal to the game's; shadow %ld runs, 0 mismatches; QL busy -> game's call\n", ltTaken,
               k.ltShadowRuns);
        k.ltMode = k.ltShadow = 0;
    }
    printf("visitor branches: masked %ld, leaf %ld, children %ld\n", g_srVisitKind[0], g_srVisitKind[1], g_srVisitKind[2]);
    printf("steerro-test ok\n");
    return 0;
}
