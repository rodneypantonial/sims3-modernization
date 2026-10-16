// Terrain height cache (docs/20 §4b, #7). Included by ts3native.cpp (one translation unit).
//
// 0xC2A570 (thiscall, ecx = world cell grid, float x, float z, ret 8, height in ST0) casts a ray down through the
// cell's terrain kd-tree (0x67EDE0 -> 0x67E2B0) for the terrain-only height. Its inputs are (x, z), the grid and the
// ray start [grid+0x34]; route search (A* edge test 0x627780, path-node pass 0x64C7C0) asks the same points again and
// again within one search. The only code that installs a new cell shape is 0xC2A840 (cell rebuild, from terrain update
// and world setup) [verified, H5].
//
// Per-cell invalidation [verified asm]: 0xC2A570 reads only one cell, (floor(x) >> 8, floor(z) >> 8) checked against
// [grid+0xCC] x [grid+0xD0] (cells at [grid+0xBC]); floor is FIST + compare of x - n with 0x80000001 + ADC -1. The
// rebuild 0xC2A840 (thiscall, int x, int z, shape, shape2, float* bounds; ret 0x14) touches only cell (x >> 8, z >> 8)
// of the same grid. So a rebuild bumps the generation of that cell's slot in g_hcCellGen (cells hashed on (cx & 63,
// cz & 63), so slot sharing only drops extra entries). A rebuild whose cell is out of range bumps the global g_hcGen.
// Points outside the grid are not cached (the game returns a constant there without a raycast), so a grid resize at
// world load cannot leave a stale entry.
//
// 2-way set-associative table keyed on (grid, bits(x), bits(z), [grid+0x34], global and cell generation), LRU by a
// per-set MRU word. Entries carry a check word written last, so a reader on another thread (route worker) that sees a
// half-written entry counts a miss, never a wrong height. Verify=1: every hit also runs the original and compares the
// bits (first 10 mismatches logged; a mismatch returns the original's answer). Runtime switch:
// ts3native_counters.heightMode (+0x1B4; 0 = the game's raycast, 1 = cache).

static const DWORD HC_FN = 0x00C2A570, HC_REBUILD = 0x00C2A840;
static const u8 kHcPrologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-16 (both functions)

typedef double(__fastcall* HcFn)(void* grid, void* edx, float x, float z);  // thiscall via fastcall + dummy edx
typedef DWORD(__fastcall* HcRebuildFn)(void* grid, void* edx, int x, int z, void* shape, void* shape2, void* bounds);
static HcFn g_hcOrig;
static HcRebuildFn g_hcRebuildOrig;
static volatile LONG g_hcGen = 1;
static volatile LONG g_hcCellGen[4096];
static bool g_hcVerify;

// mru: only meaningful in way 0 of a set (the way hit or written last)
struct HcEntry { volatile DWORD grid, xb, zb, top, gen, cgen; volatile double h; volatile DWORD chk, mru; };
static HcEntry* g_hc;      // sets of 2 ways
static DWORD g_hcMask;     // set count - 1

static DWORD HcMix(DWORD grid, DWORD xb, DWORD zb, DWORD top, DWORD gen, DWORD cgen, const double& h) {
    const DWORD* hw = (const DWORD*)&h;
    return ((grid * 0x9E3779B1u) ^ (xb * 0x85EBCA6Bu) ^ (zb * 0xC2B2AE35u) ^ top ^ (gen * 0x27D4EB2Fu) ^ (cgen * 0x165667B1u) ^
            hw[0] ^ (hw[1] * 31)) | 1;
}

// Round floats have zero low mantissa bits, so x * K keeps only its top bits; the old index (bits 7+ of the plain
// product) put e.g. every whole-number point on one set. Fold the high bits down first.
static inline HcEntry* HcSet(DWORD grid, DWORD xb, DWORD zb) {
    DWORD h = (xb * 0x9E3779B1u) ^ (zb * 0x85EBCA6Bu) ^ (grid >> 4);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return &g_hc[(h & g_hcMask) * 2];
}

static inline DWORD HcSlot(int cx, int cz) { return (DWORD)(cx & 63) | (DWORD)(cz & 63) << 6; }

// floor(v) as 0xC2A570 computes it, for |v| < 2^31; outside that (and NaN) the cell is out of range either way
static inline int HcFloor(float v) { const int n = (int)v; return n - ((float)n > v ? 1 : 0); }

static double __fastcall HcHook(void* gridp, void* edx, float x, float z) {
    Ts3nCounters& s = ts3native_counters;
    if (s.heightMode != 1) return g_hcOrig(gridp, edx, x, z);
    s.heightCalls++;  // shortcut: plain ++, a lost count from the route worker only skews the stats
    const u8* g = (const u8*)gridp;
    const int cx = HcFloor(x) >> 8, cz = HcFloor(z) >> 8;
    if ((DWORD)cx >= *(const DWORD*)(g + 0xCC) || (DWORD)cz >= *(const DWORD*)(g + 0xD0)) {
        s.heightMisses++;
        return g_hcOrig(gridp, edx, x, z);  // no cell: the game returns its constant without a raycast
    }
    const DWORD grid = (DWORD)(uintptr_t)gridp, xb = *(const DWORD*)&x, zb = *(const DWORD*)&z;
    const DWORD top = *(const DWORD*)(g + 0x34), gen = (DWORD)g_hcGen, cgen = (DWORD)g_hcCellGen[HcSlot(cx, cz)];
    HcEntry* set = HcSet(grid, xb, zb);
    for (DWORD w = 0; w < 2; w++) {
        HcEntry& e = set[w];
        const DWORD chk = e.chk;
        if (chk && e.grid == grid && e.xb == xb && e.zb == zb && e.top == top && e.gen == gen && e.cgen == cgen) {
            const double h = e.h;
            if (chk == HcMix(grid, xb, zb, top, gen, cgen, h) && e.chk == chk) {
                s.heightHits++;
                if (set[0].mru != w) set[0].mru = w;
                if (!g_hcVerify) return h;
                const double real = g_hcOrig(gridp, edx, x, z);
                if (memcmp(&real, &h, sizeof(h)) == 0) return h;
                if (InterlockedIncrement(&s.heightMismatches) <= 10)
                    Log("heightcache MISMATCH grid %08X x %.3f z %.3f cached %.6f real %.6f", grid, x, z, h, real);
                return real;
            }
        }
    }
    s.heightMisses++;
    const double h = g_hcOrig(gridp, edx, x, z);
    const DWORD w = set[0].mru ^ 1;  // replace the way not used last
    HcEntry& e = set[w];
    e.chk = 0;
    e.grid = grid; e.xb = xb; e.zb = zb; e.top = top; e.gen = gen; e.cgen = cgen; e.h = h;
    e.chk = HcMix(grid, xb, zb, top, gen, cgen, h);
    set[0].mru = w;
    return h;
}

// 0xC2A840 (cell rebuild): the game's function first, then the new generation, so an entry another thread computed
// from the old shape during the rebuild carries the old generation.
static DWORD __fastcall HcRebuildHook(void* gridp, void* edx, int x, int z, void* shape, void* shape2, void* bounds) {
    const DWORD r = g_hcRebuildOrig(gridp, edx, x, z, shape, shape2, bounds);
    const u8* g = (const u8*)gridp;
    if ((DWORD)(x >> 8) < *(const DWORD*)(g + 0xCC) && (DWORD)(z >> 8) < *(const DWORD*)(g + 0xD0))
        InterlockedIncrement(&g_hcCellGen[HcSlot(x >> 8, z >> 8)]);
    else
        InterlockedIncrement(&g_hcGen);  // cell not identified: drop everything
    InterlockedIncrement(&ts3native_counters.heightRebuilds);
    return r;
}

// [HeightCache] Enabled=1 hooks; Mode = start value of ts3native_counters.heightMode; Verify=1 checks every hit;
// Entries = table size (power of two, 1024..1M, 40 B each, 2 ways per set).
static bool ApplyHeightCache(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"HeightCache", L"Enabled", 0, ini)) { Log("height cache disabled (Enabled=0)"); return false; }
    DWORD n = GetPrivateProfileIntW(L"HeightCache", L"Entries", 65536, ini);
    if (n < 1024) n = 1024;
    if (n > (1u << 20)) n = 1u << 20;
    while (n & (n - 1)) n &= n - 1;
    g_hc = (HcEntry*)calloc(n, sizeof(HcEntry));
    if (!g_hc) { Log("FAIL height cache: no memory for %lu entries", n); return false; }
    g_hcMask = n / 2 - 1;
    g_hcVerify = GetPrivateProfileIntW(L"HeightCache", L"Verify", 0, ini) != 0;
    ts3native_counters.heightMode = GetPrivateProfileIntW(L"HeightCache", L"Mode", 1, ini) == 0 ? 0 : 1;
    // the invalidation first: the cache never runs without it
    g_hcRebuildOrig = (HcRebuildFn)InstallDetour("cell rebuild (height cache)", HC_REBUILD, kHcPrologue, sizeof(kHcPrologue), (void*)HcRebuildHook);
    if (!g_hcRebuildOrig) { free(g_hc); g_hc = NULL; return false; }
    g_hcOrig = (HcFn)InstallDetour("terrain height (height cache)", HC_FN, kHcPrologue, sizeof(kHcPrologue), (void*)HcHook);
    if (!g_hcOrig) return false;  // the rebuild detour stays: it only bumps counters after the game's code
    ts3native_counters.heightReady = 1;
    Log("OK   height cache: 0x%08X, 0x%08X (Mode=%ld Verify=%d Entries=%lu, 2-way, per-cell invalidation)", HC_FN, HC_REBUILD,
        ts3native_counters.heightMode, g_hcVerify ? 1 : 0, n);
    return true;
}
