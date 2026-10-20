// Fake routing world for snap.cpp (docs/94 P3) and the K1, K2, K5 checks of docs/94 §4.4. Included by selftest.cpp (C
// stand-in for the high-cost rasteriser) and steerro-test.cpp (the game's own WaterGrid_MarkB 0x670750 on the mapped image).
// Every write goes through P1's SnEnter with the argument layout of the real site, so the dirty marks come from the same
// code the game's stubs run; the checks are the snapshot's own audit (invariant S) plus a full rebuild compared image by image.
// shortcut: the writers here are stand-ins with the game's data layout, except the rasteriser in steerro-test; running the
// game's footprint writers (0x8321F0 and below) needs a full object world and is left to the live audit (P3 queue).

// FNV-1a: an image's bytes did not change while it lived (snap.cpp hashed every image until 0.35.15; now tests only)
static DWORD SpHash(const u8* p, DWORD n) { DWORD h = 2166136261u; for (DWORD i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }

struct SfWorld {
    u8* world; u8* wm; u8* p; u8* W; u8* blendMap; u8* layer;
    DWORD gw, gh;              // G cells
    u8* nodes[24]; int nLots;  // lot table nodes (key = 0x100 + index)
    u8* lotObj[24];            // Lot objects (key at +0x48/+0x4C) for the lot-pointer leaves
    u8* polys[64]; DWORD polyKey[64][4]; int nPolys;
    DWORD nextKey;  // high-cost polygons stamped and not removed yet
};
static SfWorld g_sf;
static DWORD g_sfSeed = 1;
static DWORD SfRnd(DWORD n) { g_sfSeed = g_sfSeed * 1103515245 + 12345; return ((g_sfSeed >> 8) & 0xFFFFFF) % (n ? n : 1); }
static void* SfA(size_t n) { void* p = _aligned_malloc(n, 16); memset(p, 0, n); return p; }  // shortcut: never freed (test process)
static u8* g_sfPages;  // steerro-test K8: the world (+0) and P (+0x1000) on these two zeroed pages, so they can be guarded

typedef void(__fastcall* SfRasterFn)(u8* g, void* edx, float x0, float z0, float x1, float z1, DWORD value, DWORD cls, void* rec);
static void __fastcall SfRasterC(u8* g, void*, float x0, float z0, float x1, float z1, DWORD value, DWORD cls, void*) {
    // stand-in for 0x670750: 1 m steps along the segment, cell trunc(v) >> 2, inside the end points' box
    const DWORD w = *(DWORD*)(g + 0x7A0), h = *(DWORD*)(g + 0x7A4);
    u8* map = *(u8**)(*(u8**)(g + 0x7C0) + cls * 16);
    const int steps = (int)(fabsf(x1 - x0) + fabsf(z1 - z0)) + 1;
    for (int i = 0; i <= steps; i++) {
        const float t = (float)i / steps, x = x0 + (x1 - x0) * t, z = z0 + (z1 - z0) * t;
        const DWORD cx = (DWORD)(int)x >> 2, cz = (DWORD)(int)z >> 2;
        if (cx < w && cz < h) map[cz * w + cx] = (u8)value;
    }
}
static SfRasterFn g_sfRaster = SfRasterC;

static DWORD g_sfStk[64];
static DWORD* SfSlot() { memset(g_sfStk, 0, sizeof(g_sfStk)); return &g_sfStk[16]; }  // slot[-2] = ecx, slot[1..] = args
static void SfSignal(int site, DWORD* slot) { SnEnter(site, slot); }

static u8* SfVec(u8* at, DWORD bytes) {  // EASTL-style vector at `at`: begin, end, cap
    u8* b = (u8*)SfA(bytes ? bytes : 4);
    *(u8**)at = b, *(u8**)(at + 4) = b + bytes, *(u8**)(at + 8) = b + bytes;
    return b;
}
static u8* SfLeaf() {
    u8* n = (u8*)SfA(0x20);
    const DWORD cnt = SfRnd(4);
    u8* list = (u8*)SfA(12 * 8);  // room to grow to 8
    for (DWORD i = 0; i < cnt; i++) {
        u8* fp = (u8*)SfA(0x40);
        *(DWORD*)(fp + 0x14) = 0x2000 + SfRnd(500);
        u8* fme = (u8*)SfA(0x14);
        *(u8**)(fme + 8) = fp;
        *(u8**)(list + i * 12) = fme;
        *(DWORD*)(list + i * 12 + 4) = SfRnd(0xFFFFFF);
    }
    *(DWORD*)(n + 8) = (DWORD)(uintptr_t)list | 1;
    *(DWORD*)(n + 0xC) = cnt;
    return n;
}
static u8* SfTree(int depth) {
    if (depth >= 2 || SfRnd(3) == 0) return SfLeaf();
    u8* n = (u8*)SfA(0x20);
    n[4] = 1;
    for (int i = 0; i < 4; i++) *(u8**)(n + 0xC + 4 * i) = SfRnd(4) ? SfTree(depth + 1) : NULL;
    return n;
}
static u8* SfNewLotNode(DWORD key) {
    u8* node = (u8*)SfA(0x200);
    *(DWORD*)node = key;
    u8* g = node + 0x10;
    *(DWORD*)g = key;
    *(LONG*)(g + 0x40) = (LONG)SfRnd(2);
    u8* pv = SfVec(g + 0x44, 4 * 8);
    for (int i = 0; i < 3; i++) ((DWORD*)pv)[i] = 0x7000 + SfRnd(100);
    *(u8**)(g + 0x48) = pv + 12;
    SfVec(g + 0x54, 0), SfVec(g + 0x64, 0);
    const DWORD nq = 1 + SfRnd(2);
    u8** qv = (u8**)SfVec(g + 0x74, 4 * nq);
    for (DWORD i = 0; i < nq; i++) {
        u8* q = (u8*)SfA(0x60);
        *(u8**)(q + 0x24) = SfTree(0);
        *(LONG*)(q + 0x44) = 4;
        qv[i] = q;
    }
    g[0x8C] = (u8)SfRnd(2);
    for (int i = 0; i < 32; i++) ((float*)(g + 0xA0))[i] = (float)SfRnd(100);
    return node;
}
static void SfRelink() {  // lot table P+0x48: 7 buckets, chain +0x190, sentinel at buckets[7]
    DWORD* bk = *(DWORD**)(g_sf.p + 0x4C);
    memset(bk, 0, 7 * 4);
    for (int i = 0; i < g_sf.nLots; i++) {
        u8* nd = g_sf.nodes[i];
        const DWORD b = *(DWORD*)nd % 7;
        *(DWORD*)(nd + 0x190) = bk[b];
        bk[b] = (DWORD)(uintptr_t)nd;
    }
}
static void SfGridAddRec(DWORD lotKey, DWORD cell, DWORD key) {
    u8* c = *(u8**)(g_sf.p + 0xDC) + cell * 32;
    DWORD* bk = *(DWORD**)(c + 4);
    u8* r = (u8*)SfA(0x50);
    *(DWORD*)r = key;
    for (int i = 0; i < 4; i++) *(float*)(r + 0x18 + 4 * i) = (float)SfRnd(256);
    *(DWORD*)(r + 0x38) = 0xFFFFFFFF, *(DWORD*)(r + 0x3C) = 0xFFFFFFFF;
    *(DWORD*)(r + 0x40) = lotKey;
    const DWORD b = key % 3;
    *(DWORD*)(r + 0x48) = bk[b];
    bk[b] = (DWORD)(uintptr_t)r;
}

static void SfBuild(DWORD seed) {
    g_sfSeed = seed;
    memset(&g_sf, 0, sizeof(g_sf));
    u8* world = g_sf.world = g_sfPages ? g_sfPages : (u8*)SfA(0xA00);
    // R: 4 x 4 chunks, 512 bytes of records
    *(DWORD*)(world + 0x54) = 4, *(DWORD*)(world + 0x58) = 4;
    DWORD* tbl = (DWORD*)SfA(16 * 4);
    for (int i = 0; i < 16; i++) tbl[i] = i * 32;
    *(DWORD**)(world + 0x5C) = tbl;
    u8* raw = (u8*)SfA(512);
    for (int i = 0; i < 512; i++) raw[i] = (u8)SfRnd(256);
    *(u8**)(world + 0x60) = raw, *(DWORD*)(world + 0x64) = 512;
    // G: 150 x 130 cells (3 x 3 tiles, partial edges), 5 passability and 10 high-cost maps
    u8* g = world + 0x8C;
    g_sf.gw = 150, g_sf.gh = 130;
    const DWORD cells = g_sf.gw * g_sf.gh;
    u8* cv = SfVec(g + 0x1C, cells * 2);
    for (DWORD i = 0; i < cells * 2; i++) cv[i] = (u8)SfRnd(3);
    *(DWORD*)(g + 0x0C) = cells;
    *(DWORD*)(g + 0x7A0) = g_sf.gw, *(DWORD*)(g + 0x7A4) = g_sf.gh, *(DWORD*)(g + 0x7A8) = 5, *(DWORD*)(g + 0x7AC) = 10;
    u8* pm = SfVec(g + 0x7B0, 5 * 16);
    for (int m = 0; m < 5; m++) { u8* b = SfVec(pm + m * 16, cells); for (DWORD i = 0; i < cells; i++) b[i] = (u8)(SfRnd(4) != 0); }
    u8* hm = SfVec(g + 0x7C0, 10 * 16);
    for (int m = 0; m < 10; m++) SfVec(hm + m * 16, cells);
    DWORD* bnm = (DWORD*)SfA(5 * 4);
    *(DWORD**)(g + 0x360) = bnm, *(DWORD*)(g + 0x364) = 5;
    // P: lot table, footprint grid (16 x 16 cells of 32 bytes, 3 buckets each), pending flag
    u8* p = g_sf.p = g_sfPages ? g_sfPages + 0x1000 : (u8*)SfA(0x200);
    *(u8**)(world + 0x95C) = p;
    DWORD* lbk = (DWORD*)SfA(8 * 4);
    lbk[7] = 0x05E117E1;
    *(DWORD**)(p + 0x4C) = lbk, *(DWORD*)(p + 0x50) = 7;
    u8* gc = SfVec(p + 0xDC, 256 * 32);
    for (int c = 0; c < 256; c++) { *(DWORD**)(gc + c * 32 + 4) = (DWORD*)SfA(3 * 4); *(DWORD*)(gc + c * 32 + 8) = 3; }
    g_sf.nLots = 6;
    for (int i = 0; i < 24; i++) {
        g_sf.lotObj[i] = (u8*)SfA(0x60);
        *(DWORD*)(g_sf.lotObj[i] + 0x48) = 0x100 + i;
    }
    for (int i = 0; i < g_sf.nLots; i++) g_sf.nodes[i] = SfNewLotNode(0x100 + i);
    SfRelink();
    for (int i = 0; i < 20; i++) SfGridAddRec(0x100 + SfRnd(6), SfRnd(256), 0x3000 + i);
    // W tuning, WM with the blend map (64 x 48 m, 4 x 3 blocks)
    g_sf.W = (u8*)SfA(0x200);
    *(u8**)(world + 0x960) = g_sf.W;
    for (int i = 0; i < 0x48; i++) g_sf.W[0x18 + i] = (u8)SfRnd(256);
    g_sf.wm = (u8*)SfA(0x200);
    u8* t = (u8*)SfA(0x20);
    *(u8**)(g_sf.wm + 0x54) = t;
    g_sf.blendMap = (u8*)SfA(0x88);
    *(u8**)(t + 0x0C) = g_sf.blendMap;
    *(DWORD*)(g_sf.blendMap + 8) = 64, *(DWORD*)(g_sf.blendMap + 0x0C) = 48, *(DWORD*)(g_sf.blendMap + 0x10) = 4;
    u8* lv = SfVec(g_sf.blendMap + 0x2C, 4);
    g_sf.layer = (u8*)SfA(0x10);
    *(u8**)lv = g_sf.layer;
    DWORD* blocks = (DWORD*)SfA(12 * 4);
    for (int i = 0; i < 12; i++) {
        if (SfRnd(2)) blocks[i] = SfRnd(2) ? 255 : 0;
        else { u8* b = (u8*)SfA(256); for (int j = 0; j < 256; j++) b[j] = (u8)SfRnd(2) * 255; blocks[i] = (DWORD)(uintptr_t)b; }
    }
    *(DWORD**)g_sf.layer = blocks, *(DWORD*)(g_sf.layer + 4) = 12;
}

// --- writer steps: change, then the signal the real site gives; returns the input it touched (SP_*) ---
static u8* SfQ(int lot) { return *(u8**)*(u8***)(g_sf.nodes[lot] + 0x10 + 0x74); }
static u8* SfAnyLeaf(u8* n) { while (n && (n[4] & 1)) { u8* c = NULL; for (int i = 0; i < 4 && !c; i++) c = *(u8**)(n + 0xC + 4 * i); if (!c) return NULL; n = c; } return n; }
static void SfPolyStamp(DWORD* slot, u8* vec) {  // as 0x6733C0: every edge into the selected maps (all 10 here)
    const float* b = *(float**)vec;
    const float* e = *(float**)(vec + 4);
    const int n = (int)(e - b) / 2;
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        for (DWORD m = 0; m < 10; m++) g_sfRaster(g_sf.world + 0x8C, NULL, b[2 * i], b[2 * i + 1], b[2 * j], b[2 * j + 1], 1, m, NULL);
    }
    (void)slot;
}
static int SfStep() {
    DWORD* s = SfSlot();
    const int lot = (int)SfRnd(g_sf.nLots);
    switch (SfRnd(12)) {
        case 0: {  // W1/W2 quadtree apply: an entry added or removed in a leaf of the lot's level-0 Q
            u8* q = SfQ(lot);
            u8* leaf = SfAnyLeaf(*(u8**)(q + 0x24));
            if (!leaf) return SP_LOTS;
            DWORD& cnt = *(DWORD*)(leaf + 0xC);
            u8* list = (u8*)(uintptr_t)(*(DWORD*)(leaf + 8) & ~1u);
            if (cnt && SfRnd(2)) cnt--;
            else if (cnt < 8) { u8* fme = (u8*)SfA(0x14); *(u8**)(fme + 8) = (u8*)SfA(0x40); *(u8**)(list + cnt * 12) = fme; cnt++; }
            s[-2] = (DWORD)(uintptr_t)q;
            SfSignal(SfRnd(2) ? 0 : 1, s);
            return SP_LOTS;
        }
        case 1: {  // W4 LotGraph_Build: built byte and a portal
            u8* g = g_sf.nodes[lot] + 0x10;
            g[0x8C] = 1;
            DWORD* pe = *(DWORD**)(g + 0x48);
            if (pe - *(DWORD**)(g + 0x44) < 8) { *pe = 0x7100 + SfRnd(100); *(DWORD**)(g + 0x48) = pe + 1; }
            s[1] = (DWORD)(uintptr_t)g;
            SfSignal(12, s);
            return SP_LOTS;
        }
        case 2: {  // R blocked bit via ApplyLotUpdate (lot pointer arg 1) or RestoreLotBlockedBits
            (*(u8**)(g_sf.world + 0x60))[SfRnd(512)] ^= 0x80;
            s[1] = (DWORD)(uintptr_t)g_sf.lotObj[lot];
            SfSignal(SfRnd(2) ? 18 : 19, s);
            return SP_R;
        }
        case 3: {  // grid add (lot pointer arg 2), also a blockedNodeMap count
            SfGridAddRec(0x100 + lot, SfRnd(256), 0x4000 + SfRnd(1000));
            u8* g = g_sf.world + 0x8C;
            DWORD* bk = *(DWORD**)(g + 0x360);
            u8* nd = (u8*)SfA(0x10);
            *(DWORD*)nd = SfRnd(5000), nd[4] = 1;
            const DWORD b = *(DWORD*)nd % 5;
            *(DWORD*)(nd + 8) = bk[b];
            bk[b] = (DWORD)(uintptr_t)nd;
            s[1] = 0, s[2] = (DWORD)(uintptr_t)g_sf.lotObj[lot];
            SfSignal(20, s);
            return SP_GRID;
        }
        case 4: {  // grid remove (lot id args 1-2): drop the lot's first record found
            u8* gc = *(u8**)(g_sf.p + 0xDC);
            for (int c = 0; c < 256; c++) {
                DWORD* bk = *(DWORD**)(gc + c * 32 + 4);
                for (int b = 0; b < 3; b++)
                    for (DWORD* at = &bk[b]; *at; at = (DWORD*)(uintptr_t)(*at + 0x48))
                        if (*(DWORD*)(uintptr_t)(*at + 0x40) == 0x100u + lot) {
                            *at = *(DWORD*)(uintptr_t)(*at + 0x48);
                            s[1] = 0x100 + lot, s[2] = 0;
                            SfSignal(21, s);
                            return SP_GRID;
                        }
            }
            return SP_GRID;
        }
        case 5: {  // high-cost polygon stamp (0x6733C0: key args 1-4, arg 5 the point vector)
            if (g_sf.nPolys == 64) return SP_GHC;
            const int n = 3 + SfRnd(3);
            const float cx = (float)SfRnd(g_sf.gw * 4), cz = (float)SfRnd(g_sf.gh * 4), r = 1.0f + (float)SfRnd(40);
            u8* vec = (u8*)SfA(12);
            float* pts = (float*)SfVec(vec, n * 8);
            for (int i = 0; i < n; i++) {
                const float a = 6.2831853f * i / n;
                pts[2 * i] = cx + r * cosf(a) - (SfRnd(5) == 0 ? cx + 3.0f : 0.0f);  // sometimes off the map's low edge
                pts[2 * i + 1] = cz + r * sinf(a);
            }
            DWORD key[4] = {0x5000 + (DWORD)g_sf.nPolys, 0, 0x6000 + SfRnd(50), 0};
            memcpy(s + 1, key, 16);
            s[5] = (DWORD)(uintptr_t)vec;
            SfSignal(22, s);  // the stub runs before the body
            SfPolyStamp(s, vec);
            memcpy(g_sf.polyKey[g_sf.nPolys], key, 16);
            g_sf.polys[g_sf.nPolys++] = vec;
            return SP_GHC;
        }
        case 6: {  // high-cost remove (0x670510: key args 1-4): the game clears the cells it recorded
            if (!g_sf.nPolys) return SP_GHC;
            const int i = (int)SfRnd(g_sf.nPolys);
            memcpy(s + 1, g_sf.polyKey[i], 16);
            SfSignal(23, s);
            u8* vec = g_sf.polys[i];
            const float* b = *(float**)vec;
            const int n = (int)(*(float**)(vec + 4) - b) / 2;
            for (int e = 0; e < n; e++) {
                const int j = (e + 1) % n;
                for (DWORD m = 0; m < 10; m++) g_sfRaster(g_sf.world + 0x8C, NULL, b[2 * e], b[2 * e + 1], b[2 * j], b[2 * j + 1], 0, m, NULL);
            }
            g_sf.polys[i] = g_sf.polys[--g_sf.nPolys];
            memcpy(g_sf.polyKey[i], g_sf.polyKey[g_sf.nPolys], 16);
            return SP_GHC;
        }
        case 7: {  // lot removed (RemoveLot: lot id args 1-2) or a new lot inserted (W5: no signal; the table walk sees it)
            if (SfRnd(2) && g_sf.nLots > 2) {
                s[1] = *(DWORD*)g_sf.nodes[lot], s[2] = 0;
                SfSignal(17, s);
                g_sf.nodes[lot] = g_sf.nodes[--g_sf.nLots];
            } else if (g_sf.nLots < 24) {
                g_sf.nodes[g_sf.nLots++] = SfNewLotNode(0x200 + g_sf.nextKey++);  // keys are never reused
            }
            SfRelink();
            return SP_LOTS;
        }
        case 8: {  // G build: cells, a passability map and R type bits rewritten; the call-site stub runs first
            SfSignal(24 + SfRnd(5), s);
            // the build frees the game's high-cost records, so a later remove of a polygon stamped before writes nothing and
            // its cells stay (docs/100 §12.1). Before 0.31.0 the fake still cleared them: with a reused key stamped after the
            // build, the snapshot's record (that key's new box only) missed those cells (LAZY seeds 1-2, steps 2122 / 2201)
            g_sf.nPolys = 0;
            u8* g = g_sf.world + 0x8C;
            u8* cv = *(u8**)(g + 0x1C);
            for (int i = 0; i < 50; i++) cv[SfRnd(g_sf.gw * g_sf.gh * 2)] = (u8)SfRnd(256);
            (*(u8**)*(u8**)(g + 0x7B0))[SfRnd(g_sf.gw * g_sf.gh)] ^= 1;
            (*(u8**)(g_sf.world + 0x60))[SfRnd(512)] ^= 0x13;
            return SP_GCORE;
        }
        case 9: {  // blend reload job (pass 4): a block changes (value copy)
            DWORD* blocks = *(DWORD**)g_sf.layer;
            blocks[SfRnd(12)] = SfRnd(2) ? 0 : 255;
            s[3] = 4;
            SfSignal(29, s);
            return SP_BLEND;
        }
        case 10: {  // a portal IC (E9-E11): lot change through an IC frame; every lot is re-imaged
            u8* g = g_sf.nodes[lot] + 0x10;
            (*(DWORD**)(g + 0x44))[0] ^= 1;
            SpIcNote(4 + SfRnd(12));
            return SP_LOTS;
        }
        default: {  // lazy state the snapshot does not image: Q pin counter / deferred flag, planner-cleared lock byte
            u8* q = SfQ(lot);
            (*(LONG*)(q + 0x48))++;
            q[0x4C] ^= 1;
            g_sf.nodes[lot][0x10 + 0x8F] ^= 1;
            g_sf.p[0x10E] ^= 1;
            return SP_IN;
        }
    }
}

// --- the checks ---
static LONG __fastcall SfAcq(void* lock, void*, const DWORD*) { return ++*(LONG*)((u8*)lock + 0x20); }
static LONG __fastcall SfRel(void* lock, void*) { return --*(LONG*)((u8*)lock + 0x20); }
static const u8* g_sfWorldSlot;
static const u8* g_sfWmSlot;

static LONG SfMisSum() { LONG s = 0; for (int i = 0; i < SP_IN; i++) s += ts3native_counters.spMis[i]; return s; }
static bool SfSameImg(const SpImg* a, const SpImg* b) { return a && b && a->size == b->size && !memcmp(a->data, b->data, a->size); }
// incremental version == a full rebuild, image by image (invariant S without the audit's own code path)
static bool SfEqualsFull(const SpVer* v) {
    SpDirty none;
    memset(&none, 0, sizeof(none));
    SpVer* f = SpBuildVersion(g_sf.world, NULL, none, true);
    bool eq = f && f->tw == v->tw && f->th == v->th && f->nLots == v->nLots;
    for (int i = 0; eq && i < SP_IN; i++) if (i != SP_GHC && i != SP_LOTS) eq = SfSameImg(f->w[i], v->w[i]);
    for (int i = 0; eq && i < v->tw * v->th; i++) eq = SfSameImg(f->tiles[i], v->tiles[i]);
    for (int i = 0; eq && i < v->nLots; i++) eq = SfSameImg(f->lots[i], v->lots[i]);
    SpVerRel(f);
    return eq;
}

static void SfSetup(DWORD seed) {
    SfBuild(seed);
    g_sfWorldSlot = g_sf.world, g_sfWmSlot = g_sf.wm;
    g_spWorldSlot = &g_sfWorldSlot, g_spWmSlot = &g_sfWmSlot;
    g_spAcq = SfAcq, g_spRel = SfRel;
    g_spLot = &kSpLotCopyRef;  // the reference lot strategy exercises the interface and the audit (docs/94 §11)
    g_spOn = 1;
    g_spCutMs = 0;
    g_spAuditEvery = 0;  // the checks call SpAudit themselves
    g_spLogMis = 4;
    SpKeysClear();
}
static void SfTeardown() {
    SpVerRel(g_spCur);
    g_spCur = NULL;
    g_spWorld = NULL;
    memset(&g_spD, 0, sizeof(g_spD));
    g_spOn = 0;
    g_spLot = &kSpLotNone;
}

// K1: random writer sequences with cuts after random steps; every cut: audit (version vs live) 0 mismatches and the
// incremental version equal to a full rebuild. Then a negative control: an unsignalled write must be found.
static void SnapK1(DWORD seed, int steps, long* cuts, long* writes) {
    SfSetup(seed);
    Ts3nCounters& k = ts3native_counters;
    assert(SpCut(g_sf.world) && k.spFullBuilds >= 1);
    for (int i = 0; i < steps; i++) {
        SfStep();
        (*writes)++;
        if (SfRnd(4) == 0) {
            const LONG m0 = SfMisSum();
            assert(SpCut(g_sf.world));
            SpAudit(g_spCur, g_sf.world);
            if (SfMisSum() != m0) { printf("K1 seed %lu step %d: audit mismatch\n", seed, i); assert(0); }
            assert(SfEqualsFull(g_spCur));
            (*cuts)++;
        }
    }
    // negative control: a transform float, a grid record and a high-cost byte changed with no signal
    assert(SpCut(g_sf.world, true));  // a reader: every tile carried, so the next cut (nothing dirty) shares every image
    const LONG m0 = SfMisSum();
    ((float*)(g_sf.nodes[0] + 0x10 + 0xA0))[3] += 1.0f;
    (*(u8**)*(u8**)(g_sf.world + 0x8C + 0x7C0))[5] ^= 1;
    assert(SpCut(g_sf.world));
    SpAudit(g_spCur, g_sf.world);
    assert(k.spMis[SP_LOTS] > 0 && k.spMis[SP_GHC] > 0 && SfMisSum() >= m0 + 2);
    SfTeardown();
}

// K2: lazy state at the cut (pending lot updates, deferred quadtree flush, pin counter, planner lock byte) is counted and
// not imaged: toggling it with no signal gives no mismatch; P3 does not force it (P4 adds ApplyPendingUpdates and the flush)
static void SnapK2(DWORD seed) {
    SfSetup(seed);
    Ts3nCounters& k = ts3native_counters;
    assert(SpCut(g_sf.world));
    const LONG p0 = k.spPendingAtCut, d0 = k.spDeferredAtCut, m0 = SfMisSum();
    g_sf.p[0x10E] = 1;
    SfQ(0)[0x4C] = 1;
    (*(LONG*)(SfQ(0) + 0x48)) += 3;
    g_sf.nodes[0][0x10 + 0x8F] ^= 1;
    assert(SpCut(g_sf.world));
    SpAudit(g_spCur, g_sf.world);
    assert(k.spPendingAtCut == p0 + 1 && k.spDeferredAtCut == d0 + 1 && SfMisSum() == m0);
    g_sf.p[0x10E] = 0;
    SfQ(0)[0x4C] = 0;
    SfTeardown();
}

// K5: a version held across cuts (a job, P4) keeps its images; a removed lot's image lives until the last holder lets go;
// every reference back to 0 at the end
static void SnapK5(DWORD seed) {
    SfSetup(seed);
    Ts3nCounters& k = ts3native_counters;
    const LONG img0 = k.spImgLive, ver0 = k.spVersionsLive;
    assert(SpCut(g_sf.world));
    SpVer* held = SpVerRef(g_spCur);
    const u64 gone = held->lots[0]->key;
    SpImg* goneImg = held->lots[0];
    const DWORD h0 = SpHash(goneImg->data, goneImg->size);
    for (int i = 0; i < g_sf.nLots; i++)
        if (*(DWORD*)g_sf.nodes[i] == (DWORD)gone) { DWORD* s = SfSlot(); s[1] = (DWORD)gone; SfSignal(17, s); g_sf.nodes[i] = g_sf.nodes[--g_sf.nLots]; break; }
    SfRelink();
    for (int i = 0; i < 40; i++) { SfStep(); if (i % 5 == 0) assert(SpCut(g_sf.world)); }
    assert(SpCut(g_sf.world) && !SpOldLot(g_spCur, gone));
    assert(goneImg->refs >= 1 && SpHash(goneImg->data, goneImg->size) == h0 && k.spVersionsLive == ver0 + 2);
    SpVerRel(held);
    assert(k.spVersionsLive == ver0 + 1);
    SfTeardown();
    assert(k.spVersionsLive == ver0 && k.spImgLive == img0);
}

// LAZY (docs/94 §21.9): a cut after every write, a reader (armed P4 cut) every `readEvery` cuts. Every cut: each tile the
// version carries equals live (invariant S for what can be read) and a cut without a reader builds no tile. Reader cuts:
// every tile carried, audit 0 mismatches, version == full rebuild. eager = tiles an eager cut would have rebuilt (dirty marks)
static void SnapLazy(DWORD seed, int steps, int readEvery, long* reads, long* built, long* eager) {
    SfSetup(seed);
    Ts3nCounters& k = ts3native_counters;
    assert(SpCut(g_sf.world, true));
    const LONG m0 = SfMisSum();
    for (int i = 1; i <= steps; i++) {
        SfStep();
        const int nt = g_spCur->tw * g_spCur->th;
        long dirty = 0;
        for (int t = 0; t < nt; t++) {
            const int b = (t / g_spCur->tw) * kSpTileMax + t % g_spCur->tw;
            dirty += (g_spD.flags & (SPD_G | SPD_TILES | SPD_EPOCH)) || (g_spD.tiles[b >> 5] >> (b & 31) & 1);
        }
        *eager += dirty;
        const bool read = i % readEvery == 0;
        const LONG c0 = k.spCopies[SP_GHC];
        assert(SpCut(g_sf.world, read));
        if (!read) assert(k.spCopies[SP_GHC] == c0);  // no reader: no tile built
        *built += k.spCopies[SP_GHC] - c0;
        for (int t = 0; t < nt; t++) {
            const SpImg* ti = g_spCur->tiles[t];
            if (!ti) continue;
            SpImg* live = SpBuildTile(g_sf.world + 0x8C, t % g_spCur->tw, t / g_spCur->tw);
            if (!SfSameImg(ti, live)) {
                DWORD off = 0;
                while (off < ti->size && ti->data[off] == live->data[off]) off++;
                printf("LAZY seed %lu step %d: carried tile %d differs from live at +%lu\n", seed, i, t, off);
                assert(0);
            }
            SpImgRel(live);
        }
        if (read) {
            assert(SpTilesAll(g_spCur) && k.spTileOut == 0);
            SpAudit(g_spCur, g_sf.world);
            assert(SfMisSum() == m0 && SfEqualsFull(g_spCur));
            (*reads)++;
        }
    }
    SfTeardown();
}
