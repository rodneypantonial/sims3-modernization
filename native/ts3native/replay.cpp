// [RouteSnap] Lots=replay (docs/94 §11 design B, §16 status; 0.28.0, counters v49): per-lot footprints and quadtrees by
// ordered leaf replay into a private clone, audit only (no search reads the clone; that is P4). Included by ts3native.cpp
// after snap.cpp, plugged into its SpLotStrategy. Off unless [RouteSnap] Enabled=1 and Lots=replay.
//
// Clone (game layout, docs/94 §11.4 step 1 and §12.8): planner P (0x110 bytes) with its own footprint map M = [P+0x24]
// (EASTL hash map: buckets +4, count +8, size +0xC, rehash policy +0x10, node pool +0x1C; 0x18-byte nodes key +0, FME +8,
// next +0x10), its own lot table (P+0x4C buckets, P+0x50 count, 0x194-byte nodes chained at +0x190, lot graph at +0x10),
// per lot the quadtrees Q (0x58 bytes: P +0x20, root +0x24, refcount +0x34), tree nodes (inner 0x1C from pool 0x667990,
// leaf 0x14 from pool 0x667920; flags +4, refcount byte +7; leaf entry list +8: one inline 12-byte entry or a heap array
// | 1 with count +0xC), FootprintMapEntry FME (0x14: vtable 0xFEAC7C, ref +4, footprint +8, Q +0xC, level/flags +0x10;
// pool 0x61ED80; the map node holds its only reference) and footprints (9 classes, 0x40-0x4C bytes; per-level polygon
// cache [fp+0x38] of 0x24-byte levels with 16-byte polygon records of 8-byte points; children list +0x24). Insertion
// order is kept (bucket chains, leaf lists); caches are copied valid (EnsureAllPolygons +0x70 on the live footprint
// first); node stamps do not exist at this level. Objects the game's code may free come from the game's allocators: tree
// nodes and map nodes from PRIVATE pools (pools.cpp, docs/94 §16.7: the game's pools are unlocked, §16.6), leaf arrays,
// buckets and FMEs from the locked EA heap / FME pool; the rest is ours (P, M, lot nodes, Q, footprints).
// Clone Qs and footprints carry RP_BIAS in their refcount so the game's release never deletes them; we free them.
// Pointers left live (opaque, never followed by the replay): P fields other than +0x24/+0x48..+0x50, lot graph fields
// other than the portal / add / remove / Q vectors, footprint +0x20 lock-key set and owner fields (P4 copies those).
//
// Recorder (§11.4 step 2): routesnap.cpp's P1 stubs call SpNote; before taking the dirty lock it calls RpPre, which turns
// W1 apply insert (Q, footprint, flag) into an INS record carrying a fresh footprint copy made right there on the writing
// thread (caches forced first), W2 apply remove (Q, key) into a REM record, and W3 QuadTreeBuild / W4 LotGraph_Build
// into a STALE mark for their lot (rebase at the next cut). W1's own child recursion (return 0x665F87) is not recorded:
// the replayed parent inserts its children. W5, lot removal, portal and lock ICs, and lot graph builds are found by the
// cut's lot signature compare and rebased (step 3). W6 cache fills are not recorded (§11.6).
// Ring: one record per logical change, appended under a spin lock by any writing thread (the sim and the game's own route
// job, both under +0x918), read by one consumer. Full ring = records dropped, clone rebuilt at the next cut.
// Cut (§11.4 step 4, sim at a route START under +0x918, from SpBuildVersion): lot signatures compared with the sim's
// mirror of what the clone was built from; changed / new / removed lots go out as one LOTS rebase (whole-lot copies made
// here); then a SYNC record (live map size and lot count) and, every AuditEvery cuts, an AUDIT record with a hash of every
// live lot; then the ring position is published. §8.5 steps 2-4 (forced first use, ApplyPendingUpdates, deferred flushes)
// are not forced: records are taken at the apply leaves, so live state and clone stay equal at the published position
// without them; P4 adds them for the search.
// Consumer (§11.4 step 5): the "snap replay" thread applies records up to the published position with the game's own
// ApplyInsert 0x665C50 / ApplyRemove 0x666750 on the clone, holding +0x918 through the game's Lock_Acquire (it serialises
// the consumer with the sim's cut and builder, which also write the clone; NOT the game's pools: those are unlocked and the
// clone uses private ones, pools.cpp, docs/94 §16.6-§16.7; the thread is the replay op thread inside each hold), at most ~1 ms per hold. AUDIT: clone lots hashed the same way and compared (rpAudits / rpMis, first LogMismatch logged). SYNC: map
// size or lot count different = divergence, rebuilt at the next cut. Footprint identity (§11.4 step 6): live pointer ->
// clone block; an INS reuses the clone footprint while clone FMEs still hold it, else takes the record's copy; blocks no
// FME holds are freed after each batch.
// Epoch (§11.4 step 1): at world event 7 (sim, +0x918 taken here) the whole clone is built; world events 5, 9, 0x0A, a new
// routing world, a ring overflow, a divergence or a fault rebuild it at the next cut.

static const LONG RP_BIAS = 0x20000000;
enum { RP_INS = 1, RP_REM, RP_STALE, RP_LOTS, RP_EPOCH, RP_AUDIT, RP_SYNC, RP_NTYPES };
enum { RPS_NOQ, RPS_STALE, RPS_NOCOPY, RPS_NOCLONE };  // RPS_STALE counts records applied to a lot waiting for its rebase
static const DWORD kRpNodeSz = 0x194, kRpQSz = 0x58, kRpPSz = 0x110, kRpMSz = 0x40, kRpRet665F87 = 0x00665F87;

// game entry points (thiscall as fastcall with a dummy edx; the tests point them at fakes)
typedef char(__fastcall* RpInsFn)(u8* q, void*, u8* fp, DWORD flag);                        // 0x665C50, ret 8
typedef char(__fastcall* RpRemFn)(u8* q, void*, DWORD lo, DWORD hi);                        // 0x666750, ret 8
typedef void*(__fastcall* RpMapAllocFn)(void* alloc, void*, DWORD size, DWORD flags);       // 0x624A10 (M+0x1C)
typedef void(__fastcall* RpMapFreeFn)(void* alloc, void*, void* p, DWORD size);             // 0x624AB0 (M+0x1C)
typedef void(__fastcall* RpMapNodeFreeFn)(u8* m, void*, u8* node);                          // 0x6281A0: FME release + pool
typedef void*(__cdecl* RpFmePoolFn)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);               // 0x61ED80 (locked pool)
typedef void*(__cdecl* RpNewFn)(DWORD size, DWORD flags, DWORD, DWORD, DWORD name, DWORD line);  // 0x4E3F90
typedef DWORD(__fastcall* RpVoidFn)(const u8* self, void*);                                 // footprint slots +0, +0x70
typedef void(__fastcall* RpDtorFn)(u8* self, void*, DWORD flags);                           // deleting destructors
static RpInsFn g_rpIns = (RpInsFn)(uintptr_t)0x00665C50;
static RpRemFn g_rpRem = (RpRemFn)(uintptr_t)0x00666750;
static RpMapAllocFn g_rpMapAlloc = (RpMapAllocFn)(uintptr_t)0x00624A10;
static RpMapFreeFn g_rpMapFree = (RpMapFreeFn)(uintptr_t)0x00624AB0;
static RpMapNodeFreeFn g_rpMapNodeFree = (RpMapNodeFreeFn)(uintptr_t)0x006281A0;
static RpFmePoolFn g_rpFmePool = (RpFmePoolFn)(uintptr_t)0x0061ED80;
static RpNewFn g_rpNew = (RpNewFn)(uintptr_t)0x004E3F90;
typedef void(__cdecl* RpDelFn)(void* p);                                                    // EA_Free 0x4E3F70
static RpDelFn g_rpDel = (RpDelFn)(uintptr_t)0x004E3F70;
static const DWORD* g_rpNewFlags = (const DWORD*)(uintptr_t)0x01158060;  // the leaf array resize 0x663766 passes it

// footprint classes (docs/82 §3.1; sizes from the factory 0x62CD40): cache = [fp+0x38] is a polygon cache. child = a child
// sub-footprint that lives only in a parent's child list (+0x24 nodes): 0xFEBF90, 0x28 bytes from the pool 0x1165B80, made
// by 0x807400 on the parent (+0xC = the parent, a back pointer; +8 refcount as the others, slots +0x90/+0x94; +0x10 float,
// +0x18/+0x1C key, +0x20 byte; slot 0 forwards to the parent's; no child list of its own: +0x24 is the object's last word)
// [verified disassembly, Steam 1.67; P3-B live run 2026-10-18: every clone build failed on it]. Copied flat inside the
// parent's block with +0xC re-pointed at the parent's copy; never copied on its own.
// middle = 0x1020AE8, objects of the middle constructor 0x80EED0 itself: "Routing/LotEdgeFootprint" (0x63A1C0, every lot)
// and "Routing/PolygonFootprint" (factory 0x62CD40), both 0x40 bytes from 0x4E3F90, levels 1 (slot 0 = 0x619EF0), cache
// at +0x38 / count +0x3C, slot +0x70 = 0x80E610 [verified disassembly, Steam 1.67; P3-B live run 2026-10-18 on 0.29.0:
// left out of the table, every clone build failed on it].
struct RpCls { DWORD vt, size, cache, child; };
static const RpCls kRpClsGame[] = {{0xFEBBC0, 0x48, 1, 0}, {0xFEBA98, 0x44, 1, 0}, {0xFEB848, 0x44, 1, 0}, {0xFEB970, 0x44, 1, 0},
                                   {0xFEBE38, 0x48, 1, 0}, {0xFEBD10, 0x44, 1, 0}, {0xFEB5F8, 0x40, 1, 0}, {0xFEB720, 0x40, 1, 0},
                                   {0x1020AE8, 0x40, 1, 0}, {0xFEAE08, 0x4C, 0, 0}, {0xFEBF90, 0x28, 0, 1}};
static const int kRpNClsGame = sizeof(kRpClsGame) / sizeof(kRpClsGame[0]);
static const RpCls* g_rpCls = kRpClsGame;
static int g_rpNCls = kRpNClsGame;

// --- our memory (16-aligned: the lot transforms are read with movaps) ---
static HANDLE g_rpHeap;
static volatile LONG g_rpBytes;
static LONG g_rpCapBytes = 64 << 20;
static void* RpAlloc(DWORD n) {
    if (!g_rpHeap) g_rpHeap = HeapCreate(0, 0, 0);
    u8* raw = (u8*)HeapAlloc(g_rpHeap, HEAP_ZERO_MEMORY, n + 24);
    if (!raw) return NULL;
    u8* a = (u8*)(((uintptr_t)raw + 8 + 15) & ~(uintptr_t)15);
    ((void**)a)[-1] = raw;
    ((DWORD*)a)[-2] = n;
    Ts3nCounters& k = ts3native_counters;
    k.rpKB = (InterlockedExchangeAdd(&g_rpBytes, (LONG)n) + (LONG)n) >> 10;
    Max(&k.rpKBMax, k.rpKB);
    return a;
}
static void RpFree(void* a) {
    if (!a) return;
    ts3native_counters.rpKB = (InterlockedExchangeAdd(&g_rpBytes, -(LONG)((DWORD*)a)[-2]) - (LONG)((DWORD*)a)[-2]) >> 10;
    HeapFree(g_rpHeap, 0, ((void**)a)[-1]);
}
static inline DWORD A16(DWORD n) { return (n + 15) & ~15u; }
static inline DWORD RpD(const u8* p) { return *(volatile const DWORD*)p; }
static inline u8* RpP(const u8* p) { return (u8*)(uintptr_t)*(volatile const DWORD*)p; }

// pointer -> pointer table (open addressing; key 1 = erased)
struct RpTab { DWORD* k; DWORD* v; DWORD cap, used; };
static DWORD RpTabH(DWORD key, DWORD cap) { return (key * 0x9E3779B1u >> 4) & (cap - 1); }
static void RpTabFree(RpTab& t) { RpFree(t.k); RpFree(t.v); t.k = t.v = NULL; t.cap = t.used = 0; }
static bool RpTabPut(RpTab& t, DWORD key, DWORD v);
static bool RpTabGrow(RpTab& t) {
    RpTab n = {NULL, NULL, t.cap ? t.cap * 2 : 1024, 0};
    n.k = (DWORD*)RpAlloc(n.cap * 4), n.v = (DWORD*)RpAlloc(n.cap * 4);
    if (!n.k || !n.v) { RpFree(n.k); RpFree(n.v); return false; }
    for (DWORD i = 0; i < t.cap; i++) if (t.k[i] > 1) RpTabPut(n, t.k[i], t.v[i]);
    RpTabFree(t);
    t = n;
    return true;
}
// keys 0 (empty) and 1 (erased) are never stored: 0.29.2 looked up key 0 (an FME without a Q, "lot 0"), matched an empty
// slot and read its value 0 as "no copy" (BP 2026-10-19: 6 builds failed)
static bool RpTabPut(RpTab& t, DWORD key, DWORD v) {
    if (key <= 1) return false;
    if ((t.used + 1) * 2 > t.cap && !RpTabGrow(t)) return false;
    DWORD h = RpTabH(key, t.cap), at = 0xFFFFFFFF;
    for (;; h = (h + 1) & (t.cap - 1)) {
        if (t.k[h] == key) { t.v[h] = v; return true; }
        if (t.k[h] == 1 && at == 0xFFFFFFFF) at = h;
        if (!t.k[h]) break;
    }
    if (at == 0xFFFFFFFF) { at = h; t.used++; }
    t.k[at] = key, t.v[at] = v;
    return true;
}
static DWORD* RpTabFind(const RpTab& t, DWORD key) {
    if (!t.cap || key <= 1) return NULL;
    for (DWORD h = RpTabH(key, t.cap);; h = (h + 1) & (t.cap - 1)) {
        if (t.k[h] == key) return &t.v[h];
        if (!t.k[h]) return NULL;
    }
}
static void RpTabDel(RpTab& t, DWORD key) {
    DWORD* v = RpTabFind(t, key);
    if (v) t.k[v - t.v] = 1;
}

// --- footprint copies: one block per footprint and its children, header 16 bytes before the footprint ---
struct RpFpHdr { u8* live; DWORD bytes, nKids, pad; };
static const RpCls* RpClass(DWORD vt) {
    for (int i = 0; i < g_rpNCls; i++) if (g_rpCls[i].vt == vt) return &g_rpCls[i];
    return NULL;
}
static bool RpIsChild(const u8* f) { const RpCls* c = RpClass(RpD(f)); return c && c->child; }
static bool RpFpHasCache(DWORD vt) { const RpCls* c = RpClass(vt); return c && c->cache; }
static inline bool RpHasList(const u8* f) { return !RpIsChild(f); }  // the child class has no child list at +0x24
// unknown footprint classes met by a copy: first 4 vtables with counts (rpUnkClsVt / rpUnkClsN), each logged once
static void RpUnknownClass(DWORD vt) {
    Ts3nCounters& k = ts3native_counters;
    k.rpUnknownVt = (LONG)vt;
    for (int i = 0; i < 4; i++) {
        if ((DWORD)k.rpUnkClsVt[i] == vt) { InterlockedIncrement(&k.rpUnkClsN[i]); return; }
        if (!k.rpUnkClsVt[i] && !InterlockedCompareExchange(&k.rpUnkClsVt[i], (LONG)vt, 0)) {
            InterlockedIncrement(&k.rpUnkClsN[i]);
            Log("snap replay: footprint class 0x%08lX unknown: lots holding it are left out of the clone (rpExclLots)", vt);
            return;
        }
    }
}
static DWORD RpLevels(const u8* fp) { return ((RpVoidFn)(uintptr_t)RpD(RpP(fp)))(fp, NULL); }  // slot +0 (1 or 3)

// why a clone build failed or left a lot out: the first cause, logged by RpEpochBody (0.29.2; the BP run on 0.29.1 failed
// 6 builds with no fault, no refused copy and no unknown class, and the old log line could not say which check fired)
enum { RW_NONE, RW_FAULT, RW_ALLOC, RW_MAPPOOL, RW_LOTTABLE, RW_NQ, RW_ROOMS, RW_MAP1, RW_EXCL64, RW_LEAFONLY, RW_DEPTH,
       RW_LEAFCNT, RW_FMEQ, RW_UNKNOWN, RW_LEVELS, RW_INVALID, RW_POLYS, RW_VERTS, RW_KIDS, RW_CHILDALONE, RW_NOPARENT, RW_N };
static const char* const kRwName[RW_N] = {
    "none", "fault (a = exception code)", "allocation", "no private map pool", "lot table walk (a = lots, -1 = over 1024 or fault)",
    "lot with more than 16 Qs (a = Qs)", "room vectors not copied", "one-bucket map with entries (a = entries)",
    "more than 64 lots left out", "refused copy met only in a tree leaf (a = Qs added)", "tree deeper than 40",
    "leaf list over 65536 (a = count)", "FME of a rebase leaf on another lot's Q", "unknown footprint class",
    "level count or cache pointer (a = levels, b = cache)", "level not valid after the forced build (a = level)",
    "polygon vector shape (a = level, b = bytes)", "vertex vector shape (a = level, b = bytes)",
    "child list over 64 or deeper than 3 (a = children)", "child footprint copied on its own", "child whose +0xC parent is a child (b = its class)"};
struct RpWhy { int code; u64 lot; DWORD fp, vt, a, b; };
static RpWhy g_rpCopyWhy;  // the last refused footprint copy (any writing thread; diagnostics only)
static void RpCopyRefused(int code, const u8* f, DWORD a = 0, DWORD b = 0) {
    RpWhy w = {code, 0, (DWORD)(uintptr_t)f, 0, a, b};
    __try { w.vt = RpD(f); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_rpCopyWhy = w;
}
static u64 RpQKey(DWORD q) {  // a live Q's lot id (+0x18 / +0x1C); 0 when unreadable
    // a freed Q (crash G log: 8 first-chance AVs on the consumer) is checked first, so it uses no first-chance log slot
    if (q && !Readable((const u8*)(uintptr_t)q + 0x18, 8)) return 0;
    __try { return q ? ((u64)RpD((const u8*)(uintptr_t)q + 0x1C) << 32) | RpD((const u8*)(uintptr_t)q + 0x18) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void RpWhyLog(const char* what, const RpWhy& w) {
    Log("snap replay: %s: %s (lot 0x%08lX%08lX, footprint 0x%08lX class 0x%08lX, a %ld, b %ld)", what,
        kRwName[w.code >= 0 && w.code < RW_N ? w.code : 0], (DWORD)(w.lot >> 32), (DWORD)w.lot, w.fp, w.vt, (LONG)w.a, (LONG)w.b);
}
// bytes of the block for fp and its children (footprints, level arrays, child list nodes; polygon and vertex arrays come
// from the game's allocator, as the game's own cache clear 0x61EE20 frees them); 0 = not copyable (unknown class, a level
// not valid after the forced build, shape caps). Forces the caches first, on the writing thread (never on the consumer):
// slot +0x70 is EnsureAllPolygons 0x80E610 exactly for the classes with a cache (docs/94 §15.1); the box class 0xFEAE08 has
// box floats at +0x38..+0x44 and is copied flat.
static DWORD RpFpMeasure(const u8* f, int depth, DWORD* kids) {
    const RpCls* c = RpClass(RpD(f));
    if (!c) { RpUnknownClass(RpD(f)); RpCopyRefused(RW_UNKNOWN, f); return 0; }
    if (c->child) {  // only inside its parent's block (a standalone copy is refused: rebase)
        if (!depth) RpCopyRefused(RW_CHILDALONE, f);
        return depth ? A16(c->size) : 0;
    }
    DWORD n = A16(c->size);
    if (c->cache) {
        ((RpVoidFn)(uintptr_t)RpD(RpP(f) + 0x70))(f, NULL);  // EnsureAllPolygons 0x80E610 on the live footprint (a W6 fill)
        const DWORD lv = RpLevels(f);
        const u8* lc = RpP(f + 0x38);
        if (!lc || lv == 0 || lv > 8) { RpCopyRefused(RW_LEVELS, f, lv, (DWORD)(uintptr_t)lc); return 0; }
        n += A16(lv * 0x24);
        for (DWORD l = 0; l < lv; l++) {
            const u8* e = lc + l * 0x24;
            if (!e[0x20]) { RpCopyRefused(RW_INVALID, f, l); return 0; }  // still invalid: a clone run would rebuild it (docs/82)
            const u8* pb = RpP(e);
            const u8* pe = RpP(e + 4);
            if (pe < pb || pe - pb > 16 * 4096 || (pe - pb) % 16) { RpCopyRefused(RW_POLYS, f, l, (DWORD)(pe - pb)); return 0; }
            for (const u8* r = pb; r < pe; r += 16) {
                const u8* vb = RpP(r);
                const u8* ve = RpP(r + 4);
                if (ve < vb || ve - vb > (1 << 20) || (ve - vb) % 8) { RpCopyRefused(RW_VERTS, f, l, (DWORD)(ve - vb)); return 0; }
            }
        }
    }
    const u8* head = f + 0x24;
    int nk = 0;
    for (const u8* l = RpP(head); l != head; l = RpP(l)) {
        if (!l || ++nk > 64 || depth >= 3) { RpCopyRefused(RW_KIDS, f, nk); return 0; }
        const DWORD m = RpFpMeasure(RpP(l + 8), depth + 1, kids);
        if (!m) return 0;
        n += 16 + 16 + m;  // list node, child header slot, child
        (*kids)++;
    }
    return n;
}
static u8* RpGameCopy(const u8* src, DWORD n, bool* ok) {  // a polygon or vertex array in the game's allocator (+0xC words kept)
    if (!n) return NULL;
    u8* d = (u8*)g_rpNew(n, *g_rpNewFlags, 0, 0, 0xF98980, 0xE7);
    if (!d) { *ok = false; return NULL; }
    memcpy(d, src, n);
    InterlockedExchangeAdd(&g_rpBytes, (LONG)n);  // counted with ours (ReplayCapMB)
    return d;
}
static void RpGameFree(u8* b, u8* e) { if (b) { InterlockedExchangeAdd(&g_rpBytes, -(LONG)(e - b)); g_rpDel(b); } }
static void RpFpArrays(u8* g) {  // frees the game-allocated arrays of one copied footprint (not its children)
    const RpCls* c = RpClass(*(const DWORD*)g);
    u8* nc = c && c->cache ? *(u8**)(g + 0x38) : NULL;
    if (!nc) return;
    const DWORD lv = RpLevels(g);
    for (DWORD l = 0; l < lv; l++) {
        u8* e = nc + l * 0x24;
        for (u8* r = *(u8**)e; r && r < *(u8**)(e + 4); r += 16) RpGameFree(*(u8**)r, *(u8**)(r + 4));
        RpGameFree(*(u8**)e, *(u8**)(e + 4));
        *(u8**)e = *(u8**)(e + 4) = *(u8**)(e + 8) = NULL;
    }
}
static u8* RpFpCopyInto(const u8* f, u8*& at, bool* ok) {
    const RpCls* c = RpClass(RpD(f));
    u8* g = at;
    at += A16(c->size);
    memcpy(g, f, c->size);
    *(LONG*)(g + 8) = RP_BIAS;
    if (c->child) return g;  // flat; the parent's loop re-points +0xC
    if (c->cache) {
        const DWORD lv = RpLevels(f);
        const u8* lc = RpP(f + 0x38);
        u8* nc = at;
        at += A16(lv * 0x24);
        memcpy(nc, lc, lv * 0x24);
        for (DWORD l = 0; l < lv; l++) { u8* e = nc + l * 0x24; *(u8**)e = *(u8**)(e + 4) = *(u8**)(e + 8) = NULL; }
        *(u8**)(g + 0x38) = nc;
        for (DWORD l = 0; l < lv && *ok; l++) {
            u8* e = nc + l * 0x24;
            const u8* le = lc + l * 0x24;
            const u8* pb = RpP(le);
            const DWORD pn = (DWORD)(RpP(le + 4) - pb);
            u8* np = RpGameCopy(pb, pn, ok);
            if (!np) continue;
            for (DWORD r = 0; r < pn; r += 16) *(u8**)(np + r) = *(u8**)(np + r + 4) = *(u8**)(np + r + 8) = NULL;
            *(u8**)e = np, *(u8**)(e + 4) = np + pn, *(u8**)(e + 8) = np + pn;
            for (DWORD r = 0; r < pn && *ok; r += 16) {
                const u8* vb = RpP(pb + r);
                const DWORD vn = (DWORD)(RpP(pb + r + 4) - vb);
                u8* nv = RpGameCopy(vb, vn, ok);
                *(u8**)(np + r) = nv, *(u8**)(np + r + 4) = nv + vn, *(u8**)(np + r + 8) = nv + vn;
            }
        }
    }
    u8* head = g + 0x24;  // children list {next, prev}; nodes {next, prev, footprint} (0x8075F0)
    *(u8**)head = head, *(u8**)(head + 4) = head;
    for (const u8* l = RpP(f + 0x24); l != f + 0x24; l = RpP(l)) {
        u8* ln = at;
        at += 16;
        at += 16;  // keeps the child 16-aligned
        u8* ch = RpFpCopyInto(RpP(l + 8), at, ok);
        if (RpIsChild(ch) && RpP(ch + 0xC) == f) *(u8**)(ch + 0xC) = g;  // the back pointer: the parent's copy
        u8* prev = *(u8**)(head + 4);
        *(u8**)ln = head, *(u8**)(ln + 4) = prev, *(u8**)(ln + 8) = ch, *(u8**)(ln + 12) = RpP(l + 8);  // +12: the live child
        *(u8**)prev = ln, *(u8**)(head + 4) = ln;
    }
    return g;
}
static void RpFpArraysDeep(u8* g) {
    RpFpArrays(g);
    if (!RpHasList(g)) return;
    for (u8* l = *(u8**)(g + 0x24); l && l != g + 0x24; l = *(u8**)l) if (*(u8**)(l + 8)) RpFpArraysDeep(*(u8**)(l + 8));
}
static void RpFpFreeBlock(u8* fp) { if (fp) { RpFpArraysDeep(fp); RpFree(fp - 16); } }
// fresh clone of a live footprint (any thread; the writer's own thread at an INS); NULL = not copyable (counted)
static u8* RpFpCopy(u8* live) {
    Ts3nCounters& k = ts3native_counters;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    u8* fp = NULL;
    RpCopyRefused(RW_FAULT, live);  // replaced by RpFpMeasure's reason, or kept when the copy faults
    __try {
        DWORD kids = 0;
        const DWORD n = live ? RpFpMeasure(live, 0, &kids) : 0;
        RpFpHdr* h = n ? (RpFpHdr*)RpAlloc(16 + n) : NULL;
        if (h) {
            u8* at = (u8*)h + 16;
            bool ok = true;
            fp = RpFpCopyInto(live, at, &ok);
            h->live = live, h->bytes = n, h->nKids = kids;
            if (!ok) { RpFpFreeBlock(fp); fp = NULL; RpCopyRefused(RW_ALLOC, live, 1); }
        } else if (n) RpCopyRefused(RW_ALLOC, live, 0, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { fp = NULL; }  // shortcut: a fault inside the copy leaks that block (counted as a refused copy)
    if (!fp) InterlockedIncrement(&k.rpCopyFail); else InterlockedIncrement(&k.rpFps);
    InterlockedExchangeAdd(&k.rpCopyUs, UsSince(t0));
    return fp;
}
// A child footprint (0xFEBF90) that its parent's list does not hold gets a block of its own, +0xC = the parent's copy, which
// it pins (+8) so the GC keeps the parent while the child lives (hdr.pad = 1). Its slots forward to +0xC unchecked
// (0x626210 and 21 others [verified]), so the parent must stay. Mass frees (whole clone, whole batch) skip the unpin.
static __declspec(thread) bool g_rpMassFree;  // per thread: the consumer's GC and the sim's batch drop may overlap
static void RpFpFree(u8* fp) {
    if (!fp) return;
    if (((RpFpHdr*)(fp - 16))->pad == 1 && !g_rpMassFree)
        if (u8* par = *(u8**)(fp + 0xC)) (*(LONG*)(par + 8))--;
    RpFpFreeBlock(fp);
    InterlockedDecrement(&ts3native_counters.rpFps);
}
static u8* RpFpCopyChild(const u8* live, u8* parCopy) {
    const RpCls* c = RpClass(RpD(live));
    RpFpHdr* h = (RpFpHdr*)RpAlloc(16 + A16(c->size));
    if (!h) { RpCopyRefused(RW_ALLOC, live, 2); return NULL; }
    u8* g = (u8*)h + 16;
    memcpy(g, live, c->size);
    *(LONG*)(g + 8) = RP_BIAS;
    *(u8**)(g + 0xC) = parCopy;
    if (parCopy) (*(LONG*)(parCopy + 8))++;
    h->live = (u8*)live, h->bytes = A16(c->size), h->nKids = 0, h->pad = 1;
    InterlockedIncrement(&ts3native_counters.rpFps);
    return g;
}
// children of a copy (list node +12 holds the live child) in or out of a live -> clone table
static void RpKids(RpTab& kids, const u8* fp, bool put) {
    if (!RpHasList(fp)) return;
    for (const u8* l = *(u8* const*)(fp + 0x24); l != fp + 0x24; l = *(u8* const*)l) {
        u8* ch = *(u8* const*)(l + 8);
        if (put) RpTabPut(kids, *(const DWORD*)(l + 12), (DWORD)(uintptr_t)ch); else RpTabDel(kids, *(const DWORD*)(l + 12));
        RpKids(kids, ch, put);
    }
}
// references the clone holds on fp and its children (FMEs; the bias excluded)
static LONG RpFpRefs(const u8* fp) {
    LONG r = *(const LONG*)(fp + 8) - RP_BIAS;
    if (!RpHasList(fp)) return r;
    for (const u8* l = *(u8* const*)(fp + 0x24); l != fp + 0x24; l = *(u8* const*)l) r += RpFpRefs(*(u8* const*)(l + 8));
    return r;
}

// --- the clone ---
// prefs: the portal references this lot copy holds (H20b, 0.30.0): one per non-null element of the lot graph's portal list +0x44,
// its pending adds +0x54 and every room record's portal vectors +4 / +0x14, taken at the copy and released at RpLotFree
struct RpLot { u64 key; u8* node; int nQ; u8* q[16]; DWORD liveQ[16]; bool stale, rooms; u8** prefs; int nPrefs; };
struct RpClone {
    u8* p; u8* m; u8* dummyQ;
    DWORD* lotBk; DWORD lotN;
    RpLot* lots; int nLots, capLots;
    RpTab fps;   // live footprint -> clone footprint block (top level; a block holds its children)
    RpTab kids;  // live child footprint -> its clone inside the parent's block (children are inserted by their parent)
    RpTab old;   // replaced blocks some FMEs still hold (a dropped child's): freed by the GC at no reference, or with the clone
    RpTab oldLive;  // the live footprints those blocks copy (a fresh copy of one of them unifies the FMEs)
    u64 ex[64]; int nEx;  // lots left out at the build (a footprint the copy refuses, e.g. an unknown class): rpExclLots
    PlSeen seen;          // the game's pool objects at the build (pools.cpp): a change drops this clone at the drain
};

static u8* RpGraph(u8* node) { return node + 0x10; }

// H20 (docs/109 §8.9, docs/94 §21.8): the lot graph and room vectors hold counted pointers to LIVE portal objects (vtable
// 0xFED048: AddRef slot 0 = 0x64E8E0, Release slot +4 = 0x64E8F0, count +0x70, deleting slot +8 at a count <= 1; verified
// disassembly, Steam 1.67). With g_rpPortalRefs (set by P4 once the interlocked pair is in the vtable, H20a) each lot copy
// takes one reference per element it holds, so a sim-side Release (RemovePortals, a lot rebuild) never deletes a portal the
// clone points at. Without it (P3-B audit only: nothing follows the pointers) no reference is taken. The calls go through
// the element's own vtable, as the game's vector copy 0x9EC420 and release 0x5BF1E0 do.
static bool g_rpPortalRefs;
typedef LONG(__fastcall* RpRefFn)(u8* self, void*);
static void RpPortalRefsTake(RpLot* l, u8* g) {
    int n = 0;
    const DWORD vecs[2] = {0x44, 0x54};
    for (int v = 0; v < 2; v++) n += (int)((RpP(g + vecs[v] + 4) - RpP(g + vecs[v])) / 4);
    for (u8* r = RpP(g + 0x90); r && r + 0x44 <= RpP(g + 0x94); r += 0x44) n += (int)((RpP(r + 8) - RpP(r + 4)) / 4 + (RpP(r + 0x18) - RpP(r + 0x14)) / 4);
    l->prefs = n ? (u8**)RpAlloc(n * 4) : NULL;
    if (!l->prefs) return;
    auto take = [&](const u8* b, const u8* e) {
        for (const u8* p = b; p && p < e; p += 4)
            if (u8* pt = RpP(p)) {
                ((RpRefFn)(uintptr_t)RpD(RpP(pt)))(pt, NULL);
                l->prefs[l->nPrefs++] = pt;
                InterlockedIncrement(&ts3native_counters.rpPortalRefs);  // per reference: a fault mid-way leaves the gauge right
            }
    };
    for (int v = 0; v < 2; v++) take(RpP(g + vecs[v]), RpP(g + vecs[v] + 4));
    for (u8* r = RpP(g + 0x90); r && r + 0x44 <= RpP(g + 0x94); r += 0x44) take(RpP(r + 4), RpP(r + 8)), take(RpP(r + 0x14), RpP(r + 0x18));
}
static void RpPortalRefsDrop(RpLot* l) {
    __try {
        for (int i = 0; i < l->nPrefs; i++) {
            ((RpRefFn)(uintptr_t)RpD(RpP(l->prefs[i]) + 4))(l->prefs[i], NULL);
            InterlockedDecrement(&ts3native_counters.rpPortalRefs);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ts3native_counters.rpFaults++; }
    RpFree(l->prefs);
    l->prefs = NULL, l->nPrefs = 0;
}
static bool RpVecCopy(u8* g, DWORD off, DWORD elem) {  // vector begin/end/cap at g+off: our own copy of begin..end
    const u8* b = RpP(g + off);
    const u8* e = RpP(g + off + 4);
    const DWORD n = b && e > b && (DWORD)(e - b) <= elem * 65536 ? (DWORD)(e - b) : 0;
    u8* c = (u8*)RpAlloc(n ? n : 4);
    if (!c) return false;
    if (n) memcpy(c, b, n);
    *(u8**)(g + off) = c, *(u8**)(g + off + 4) = c + n, *(u8**)(g + off + 8) = c + n;
    return true;
}
// crash H (docs/67 §10, 0.32.6): a vector the game itself grows or clears in a clone run must be an EA block, as the game's
// vector grow allocates it (0x4E3F90 with the vector's own +0xC word, 0x5D1DCD); an RpAlloc block there reaches EA's free
// (0x4E3F70) with no EA chunk header. Empty: NULL (the game's grow and clear accept it, 0x5D1D83 / 0x61EE2A). Freed with g_rpDel
static bool RpVecCopyEA(u8* g, DWORD off, DWORD elem) {
    const u8* b = RpP(g + off);
    const u8* e = RpP(g + off + 4);
    const DWORD n = b && e > b && (DWORD)(e - b) <= elem * 65536 ? (DWORD)(e - b) : 0;
    u8* c = n ? (u8*)g_rpNew(n, RpD(g + off + 0xC), 0, 0, 0xF98980, 0xE7) : NULL;
    if (n && !c) return false;
    if (n) memcpy(c, b, n);
    *(u8**)(g + off) = c, *(u8**)(g + off + 4) = c + n, *(u8**)(g + off + 8) = c + n;
    return true;
}
// a room record's +0x24 (u16 portal index list) and +0x34 (one 16-byte vector of 8-byte entries per portal). RoomGraph_FirstUse
// 0x63D2F0 on an unbuilt room (never built, or reset by 0x630F70, which clears only the built byte) empties +0x24, EA-frees every
// +0x34 element's buffer (0x61EE20), resizes +0x34 (0x622A30) and pushes into both (0x5D1D60, 0x6202E0) [verified disassembly].
// So both levels are private EA blocks: the flat 0.29.0 copy left the inner buffers live (I-94-133: a clone run freed live
// blocks) and the outer ones in our heap (crash H)
static bool RpRoomListsCopy(u8* r) {
    if (!RpVecCopyEA(r, 0x24, 2) || !RpVecCopyEA(r, 0x34, 16)) return false;
    for (u8* e = RpP(r + 0x34); e && e < RpP(r + 0x38); e += 16)
        if (!RpVecCopyEA(e, 0, 8)) return false;  // the rest stay live: l->rooms false, the build fails, nothing is freed
    return true;
}
static void RpRoomListsFree(u8* r) {
    for (u8* e = RpP(r + 0x34); e && e < RpP(r + 0x38); e += 16) g_rpDel(RpP(e));
    g_rpDel(RpP(r + 0x34)), g_rpDel(RpP(r + 0x24));
}

// build context: an epoch build (whole clone) or a rebase batch (some lots against the clone's P)
struct RpCtx {
    u8* cp; u8* dummyQ;
    RpTab qMap;    // live Q -> clone Q
    RpTab fmeMap;  // live FME -> clone FME
    RpTab fps;     // live footprint -> clone footprint block (epoch: the clone's table; batch: its own)
    RpTab kids;    // live child -> clone child inside a block
    bool fail;
    bool excl;     // a refused footprint copy leaves its FME's lot out (exQ: live Qs) instead of failing (epoch build, soft refresh)
    RpTab exQ;
    RpTab bad;     // live footprint -> why its copy was refused (RW_*): refused once per build or batch, not once per FME / leaf
    RpWhy why;     // first cause of x.fail
    RpWhy exWhy;   // first cause of a lot left out (exQ)
    u64 lot;       // the lot being shelled or filled (for RpWhy)
    int nAlone;    // child footprints copied in a block of their own (not in their parent's list)
    int nNoQ;      // FMEs without a Q (world-level hanging footprints, 0x6377F0) copied
};
static void RpFailW(RpCtx& x, int code, u64 lot = 0, DWORD a = 0, DWORD b = 0) {
    if (!x.why.code) x.why = {code, lot, 0, 0, a, b};
    x.fail = true;
}
// a refused footprint (g_rpCopyWhy holds the reason) leaves the lot of live Q q out, or fails a build that does not exclude
static void RpRefused(RpCtx& x, DWORD q) {
    RpWhy w = g_rpCopyWhy;
    w.lot = RpQKey(q);
    if (x.excl && q) {  // an FME without a Q (world-level) has no lot to leave out: the build fails, named
        if (!x.exWhy.code) x.exWhy = w;
        RpTabPut(x.exQ, q, 1);
    } else {
        if (!x.why.code) x.why = w;
        x.fail = true;
    }
}
static u8* RpFpFor(RpCtx& x, u8* live, DWORD q) {
    if (!live) return NULL;  // key 0 would match an empty table slot
    if (DWORD* v = RpTabFind(x.kids, (DWORD)(uintptr_t)live)) return (u8*)(uintptr_t)*v;
    if (DWORD* v = RpTabFind(x.fps, (DWORD)(uintptr_t)live)) return (u8*)(uintptr_t)*v;
    if (RpIsChild(live)) {  // inside its parent's block when the parent's list holds it (+0xC is the parent)
        u8* par = RpP(live + 0xC);
        u8* pc = par && !RpIsChild(par) ? RpFpFor(x, par, q) : NULL;
        if (pc)
            if (DWORD* v = RpTabFind(x.kids, (DWORD)(uintptr_t)live)) return (u8*)(uintptr_t)*v;
        if (par && !pc && !RpIsChild(par)) return NULL;  // a refused parent has left q's lot out already
        if (par && !pc) { RpCopyRefused(RW_NOPARENT, live, 1, RpD(par)); RpRefused(x, q); return NULL; }  // parent is a child
        // not in its parent's list (BP 0.29.2: 209 lots left out; e.g. children linked by the load fix-up 0x806ED0, which
        // sets +0xC from an id lookup, or created alone by the factory, "Routing/ExtensionFootprint" 0x6261F0): own block
        u8* c = RpFpCopyChild(live, pc);
        if (!c) { RpRefused(x, q); return NULL; }
        if (!RpTabPut(x.fps, (DWORD)(uintptr_t)live, (DWORD)(uintptr_t)c)) { RpFpFree(c); RpFailW(x, RW_ALLOC); return NULL; }
        x.nAlone++;
        return c;
    }
    if (DWORD* b = RpTabFind(x.bad, (DWORD)(uintptr_t)live)) {  // refused before in this build: same reason, no second copy
        RpCopyRefused((int)*b, live);
        RpRefused(x, q);
        return NULL;
    }
    u8* c = RpFpCopy(live);
    if (!c) {
        RpTabPut(x.bad, (DWORD)(uintptr_t)live, (DWORD)g_rpCopyWhy.code);
        RpRefused(x, q);
        return NULL;
    }
    if (!RpTabPut(x.fps, (DWORD)(uintptr_t)live, (DWORD)(uintptr_t)c)) { RpFpFree(c); RpFailW(x, RW_ALLOC); return NULL; }
    RpKids(x.kids, c, true);
    return c;
}
// parents before their children: a child met first would get a block of its own besides the parent's copy
static void RpPreParent(RpCtx& x, const u8* liveFme) {
    u8* fp = liveFme ? RpP(liveFme + 8) : NULL;
    if (!fp) return;
    u8* top = RpIsChild(fp) ? RpP(fp + 0xC) : RpP(fp + 0x24) != fp + 0x24 ? fp : NULL;
    if (top) RpFpFor(x, top, RpD(liveFme + 0xC));
}
// clone FME of a live one (reference count 0; the map node adds its one). An FME without a footprint (+8 = 0) is copied as
// it is (0.29.2): before, the table lookup of key 0 hit an empty slot and returned "no copy", which left its lot out, and in
// a tree leaf only failed the whole build
static u8* RpFme(RpCtx& x, const u8* live) {
    if (DWORD* v = RpTabFind(x.fmeMap, (DWORD)(uintptr_t)live)) return (u8*)(uintptr_t)*v;
    u8* lfp = RpP(live + 8);
    u8* fp = lfp ? RpFpFor(x, lfp, RpD(live + 0xC)) : NULL;
    if (lfp && !fp) return NULL;
    u8* f = (u8*)g_rpFmePool(0x14, 0xFEAC80, 0, 0, 0, 0);
    if (!f) { RpFailW(x, RW_ALLOC, RpQKey(RpD(live + 0xC))); return NULL; }
    memcpy(f, live, 0x14);
    *(LONG*)(f + 4) = 0;
    *(u8**)(f + 8) = fp;
    if (fp) (*(LONG*)(fp + 8))++;  // as the game's +0x90 (0x915880)
    const DWORD lq = RpD(live + 0xC);  // 0: a world-level hanging footprint's FME (0x6377F0 clears it) [verified]: kept 0
    const DWORD* q = RpTabFind(x.qMap, lq);
    *(u8**)(f + 0xC) = !lq ? NULL : q ? (u8*)(uintptr_t)*q : x.dummyQ;
    if (!lq) x.nNoQ++;
    else if (!RpP(f + 0xC)) RpFailW(x, RW_FMEQ, RpQKey(RpD(live + 0xC)));  // a rebase leaf that holds another lot's FME
    if (!RpTabPut(x.fmeMap, (DWORD)(uintptr_t)live, (DWORD)(uintptr_t)f)) RpFailW(x, RW_ALLOC);
    return f;
}
// footprint copies for every FME in a live tree before the lots are filled, so a refusal met only in a leaf (an FME that no
// map node holds) leaves its lot out like one met in the map, instead of failing the whole build (0.29.2)
static void RpPreTree(RpCtx& x, const u8* n, int depth) {
    if (!n || x.fail || depth > 40) return;
    if (n[4] & 1) { for (int i = 0; i < 4; i++) RpPreTree(x, RpP(n + 0xC + 4 * i), depth + 1); return; }
    const bool heap = (n[8] & 1) != 0;
    const DWORD cnt = heap ? RpD(n + 0xC) : 1;
    const u8* a = heap ? (const u8*)(uintptr_t)(RpD(n + 8) & ~1u) : n + 8;
    for (DWORD j = 0; j < cnt && j <= 65536; j++) {
        const u8* f = RpP(a + j * 12);
        if (!f || !RpP(f + 8) || RpTabFind(x.fmeMap, (DWORD)(uintptr_t)f)) continue;
        RpPreParent(x, f);
        RpFpFor(x, RpP(f + 8), RpD(f + 0xC));
    }
}
static u8* RpTree(RpCtx& x, const u8* n, int depth, u8* parent = NULL) {
    if (!n || x.fail) return NULL;
    if (depth > 40) { RpFailW(x, RW_DEPTH, x.lot, depth); return NULL; }
    if (n[4] & 1) {  // inner (0x628B70): children +0xC..+0x18
        u8* m = (u8*)PlAlloc(PL_INNER);  // private pool (pools.cpp; the pop site 0x667990 would pick it on this thread too)
        if (!m) { RpFailW(x, RW_ALLOC, x.lot, 1); return NULL; }
        memcpy(m, n, 0x1C);
        *(u8**)(m + 8) = parent;  // the clone's parent, not the live one (inner +8; the root's is 0), I-94-193
        m[7] = 1;
        for (int i = 0; i < 4; i++) *(u8**)(m + 0xC + 4 * i) = RpTree(x, RpP(n + 0xC + 4 * i), depth + 1, m);
        return m;
    }
    u8* m = (u8*)PlAlloc(PL_LEAF);  // leaf (0x667B10), private pool
    if (!m) { RpFailW(x, RW_ALLOC, x.lot, 2); return NULL; }
    memcpy(m, n, 0x14);
    m[7] = 1;
    if (m[8] & 1) {  // heap list (ptr | 1, count +0xC), exact size as 0x6636B0 allocates it
        const DWORD cnt = RpD(n + 0xC);
        const u8* src = (const u8*)(uintptr_t)(RpD(n + 8) & ~1u);
        if (cnt) {
            if (cnt > 65536) { RpFailW(x, RW_LEAFCNT, x.lot, cnt); return m; }
            u8* arr = (u8*)g_rpNew(cnt * 12, *g_rpNewFlags, 0, 0, 0xF98980, 0xE7);
            if (!arr) { *(DWORD*)(m + 8) = 1, *(DWORD*)(m + 0xC) = 0; RpFailW(x, RW_ALLOC, x.lot, 3); return m; }
            for (DWORD j = 0; j < cnt; j++) {
                memcpy(arr + j * 12, src + j * 12, 12);
                const u8* f = RpP(src + j * 12);
                if (f) {
                    u8* cf = RpFme(x, f);
                    if (cf && !*(LONG*)(cf + 4)) { *(LONG*)(cf + 4) = 1; ts3native_counters.rpOrphanFme++; }  // not in the map yet
                    *(u8**)(arr + j * 12) = cf;
                }
            }
            *(DWORD*)(m + 8) = (DWORD)(uintptr_t)arr | 1;
        }
    } else if (const u8* f = RpP(n + 8)) {
        u8* cf = RpFme(x, f);
        if (cf && !*(LONG*)(cf + 4)) { *(LONG*)(cf + 4) = 1; ts3native_counters.rpOrphanFme++; }
        *(u8**)(m + 8) = cf;
    }
    return m;
}
// tree release as 0x664CE0: the root's reference, its deleting destructor frees the subtree into the pools
static void RpTreeRel(u8* q) {
    u8* r = *(u8**)(q + 0x24);
    *(u8**)(q + 0x24) = NULL;
    if (r && --r[7] == 0) { r[7] = 1; ((RpDtorFn)(uintptr_t)RpD(RpP(r)))(r, NULL, 1); }
}
// lot node copy with Q shells (trees filled after the map, so FMEs found in the map get their map reference first)
static u8* RpLotShell(RpCtx& x, const u8* live, RpLot* l) {
    x.lot = ((u64)RpD(live + 4) << 32) | RpD(live);
    u8* node = (u8*)RpAlloc(kRpNodeSz);
    if (!node) { RpFailW(x, RW_ALLOC, x.lot, 4); return NULL; }
    memcpy(node, live, kRpNodeSz);
    *(DWORD*)(node + 0x190) = 0;
    u8* g = RpGraph(node);
    // +0x64 holds 8-byte portal ids (pending removes, 0x639A80 walks it with sar 3), not pointers [verified disassembly]
    const bool pv = RpVecCopy(g, 0x44, 4) && RpVecCopy(g, 0x54, 4);
    if (!pv || !RpVecCopy(g, 0x64, 4) || !RpVecCopy(g, 0x74, 4)) RpFailW(x, RW_ALLOC, x.lot, 5);
    // rooms (P4, 0.29.0): the 0x44-byte room records +0x90 with their portal index list +0x24 and connectivity lists +0x34,
    // so a clone search reads no live room vector (docs/94 §16.5). +0x34 is a vector of vectors: deep-copied, both lists as EA
    // blocks since 0.32.6 (I-94-133 refuted, crash H: RpRoomListsCopy).
    // H20b (0.30.0): the portal vectors +4 and +0x14 (counted portal pointers; the room copy 0x6223C0 AddRefs each element
    // through 0x9EC420, the room destroy 0x621D20 releases them through 0x5BF1E0 [verified disassembly]) are copied too
    bool rooms = RpVecCopy(g, 0x90, 0x44);
    for (u8* r = rooms ? RpP(g + 0x90) : NULL; r && r + 0x44 <= RpP(g + 0x94); r += 0x44)
        rooms = rooms && RpVecCopy(r, 4, 4) && RpVecCopy(r, 0x14, 4) && RpRoomListsCopy(r);  // 0.32.6: +0x24 / +0x34 EA, deep
    l->rooms = rooms;  // false: some room vectors stay live (leaked copies, never a free of a live pointer)
    if (!rooms) RpFailW(x, RW_ROOMS, x.lot, (DWORD)((RpP(g + 0x94) - RpP(g + 0x90)) / 0x44));
    l->key = ((u64)RpD(live + 4) << 32) | RpD(live);
    l->node = node, l->stale = false;
    l->prefs = NULL, l->nPrefs = 0;
    if (g_rpPortalRefs && pv && rooms) RpPortalRefsTake(l, g);  // only private vectors: a live one is never ours to count
    u8** qv = *(u8***)(g + 0x74);
    l->nQ = (int)(*(u8***)(g + 0x78) - qv);
    if (l->nQ > 16) { RpFailW(x, RW_NQ, x.lot, l->nQ); l->nQ = 0; }
    for (int i = 0; i < l->nQ; i++) {
        const u8* lq = qv[i];
        l->liveQ[i] = (DWORD)(uintptr_t)lq;
        l->q[i] = NULL;
        if (!lq) continue;
        u8* q = (u8*)RpAlloc(kRpQSz);
        if (!q) { RpFailW(x, RW_ALLOC, x.lot, 6); continue; }
        memcpy(q, lq, kRpQSz);
        *(u8**)(q + 0x20) = x.cp, *(u8**)(q + 0x24) = NULL, *(LONG*)(q + 0x34) = RP_BIAS, *(LONG*)(q + 0x48) = 0, q[0x4C] = 0;
        qv[i] = q;
        l->q[i] = q;
        if (!RpTabPut(x.qMap, (DWORD)(uintptr_t)lq, (DWORD)(uintptr_t)q)) RpFailW(x, RW_ALLOC, x.lot, 7);
    }
    return node;
}
static void RpLotPre(RpCtx& x, const RpLot* l) {  // RpPreTree over the lot's live trees
    for (int i = 0; i < l->nQ; i++) if (l->liveQ[i]) RpPreTree(x, RpP((const u8*)(uintptr_t)l->liveQ[i] + 0x24), 0);
}
static void RpLotFill(RpCtx& x, RpLot* l) {
    x.lot = l->key;
    for (int i = 0; i < l->nQ; i++)
        if (l->q[i]) *(u8**)(l->q[i] + 0x24) = RpTree(x, RpP((const u8*)(uintptr_t)l->liveQ[i] + 0x24), 0);
}
static void RpLotFree(RpLot* l) {  // trees released first; the caller has dropped the map nodes that reference its Qs
    for (int i = 0; i < l->nQ; i++) if (l->q[i]) RpTreeRel(l->q[i]);
    for (int i = 0; i < l->nQ; i++) RpFree(l->q[i]);
    if (l->prefs) RpPortalRefsDrop(l);
    if (l->node) {
        u8* g = RpGraph(l->node);
        RpFree(RpP(g + 0x44)), RpFree(RpP(g + 0x54)), RpFree(RpP(g + 0x64)), RpFree(RpP(g + 0x74));
        if (l->rooms) {  // ours only when the shell copied them (RpVecCopy failures leave live pointers: never freed)
            for (u8* r = RpP(g + 0x90); r && r + 0x44 <= RpP(g + 0x94); r += 0x44)
                RpFree(RpP(r + 4)), RpFree(RpP(r + 0x14)), RpRoomListsFree(r);
            RpFree(RpP(g + 0x90));
        }
        RpFree(l->node);
    }
    l->node = NULL, l->nQ = 0;
}

// lot table P+0x48: buckets +4 (P+0x4C), count +8 (P+0x50), sentinel buckets[count], chain +0x190
struct RpLiveLot { u64 key; u8* node; };
static int RpLiveLots(const u8* p, RpLiveLot* out, int cap) {
    int n = 0;
    __try {
        const DWORD* bk = (const DWORD*)RpP(p + 0x4C);
        const DWORD nb = RpD(p + 0x50);
        if (!bk || nb > (1u << 20)) return 0;
        const DWORD sentinel = bk[nb];
        for (DWORD i = 0; i < nb; i++)
            for (u8* nd = (u8*)(uintptr_t)bk[i]; nd && (DWORD)(uintptr_t)nd != sentinel; nd = RpP(nd + 0x190)) {
                if (n == cap) return -1;
                out[n].key = ((u64)RpD(nd + 4) << 32) | RpD(nd), out[n].node = nd;
                n++;
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return n;
}
static const int kRpMaxLots = 1024;
static RpLiveLot g_rpLive[kRpMaxLots];  // sim thread (cuts)

static void RpTableLink(RpClone* c, RpLot* l) {
    const DWORD b = (DWORD)l->key % (c->lotN ? c->lotN : 1);
    *(DWORD*)(l->node + 0x190) = c->lotBk[b];
    c->lotBk[b] = (DWORD)(uintptr_t)l->node;
}
static void RpTableUnlink(RpClone* c, RpLot* l) {
    DWORD* at = &c->lotBk[(DWORD)l->key % (c->lotN ? c->lotN : 1)];
    while (*at && *at != (DWORD)(uintptr_t)l->node) at = (DWORD*)(uintptr_t)(*at + 0x190);
    if (*at) *at = *(DWORD*)(l->node + 0x190);
}
static RpLot* RpLotAdd(RpClone* c) {
    if (c->nLots == c->capLots) {
        const int cap = c->capLots ? c->capLots * 2 : 128;
        RpLot* n = (RpLot*)RpAlloc(cap * sizeof(RpLot));
        if (!n) return NULL;
        if (c->lots) { memcpy(n, c->lots, c->nLots * sizeof(RpLot)); RpFree(c->lots); }
        c->lots = n, c->capLots = cap;
    }
    RpLot* l = &c->lots[c->nLots++];
    memset(l, 0, sizeof(*l));
    return l;
}

// map M: our walk; nodes unlinked by us go through the game's node free (FME release, node pool)
static void RpMapDrop(RpClone* c, const RpTab& qs) {  // every node whose FME's Q is in qs
    u8* m = c->m;
    DWORD* bk = (DWORD*)RpP(m + 4);
    const DWORD nb = RpD(m + 8);
    for (DWORD i = 0; nb > 1 && i < nb; i++)
        for (DWORD* at = &bk[i]; *at;) {
            u8* nd = (u8*)(uintptr_t)*at;
            const u8* f = RpP(nd + 8);
            if (f && RpTabFind(qs, RpD(f + 0xC))) {
                *at = RpD(nd + 0x10);
                g_rpMapNodeFree(m, NULL, nd);
                (*(DWORD*)(m + 0xC))--;
            } else at = (DWORD*)(nd + 0x10);
        }
}
static bool RpMapAdd(RpClone* c, DWORD lo, DWORD hi, u8* f) {  // after the run of the same key, else at the bucket head
    u8* m = c->m;
    const DWORD nb = RpD(m + 8);
    if (nb <= 1) return false;  // the empty map's shared bucket: the next game insert rehashes; a rebase does not
    u8* nd = (u8*)g_rpMapAlloc(m + 0x1C, NULL, 0x18, 0);
    if (!nd) return false;
    *(DWORD*)nd = lo, *(DWORD*)(nd + 4) = hi, *(u8**)(nd + 8) = f, *(DWORD*)(nd + 0xC) = 0, *(DWORD*)(nd + 0x10) = 0;
    (*(LONG*)(f + 4))++;
    DWORD* at = &((DWORD*)RpP(m + 4))[lo % nb];
    DWORD* after = NULL;
    for (DWORD* p = at; *p; p = (DWORD*)(uintptr_t)(*p + 0x10)) {
        const u8* x = (const u8*)(uintptr_t)*p;
        if (RpD(x) == lo && RpD(x + 4) == hi) after = (DWORD*)(x + 0x10);
        else if (after) break;
    }
    DWORD* link = after ? after : at;
    *(DWORD*)(nd + 0x10) = *link;
    *link = (DWORD)(uintptr_t)nd;
    (*(DWORD*)(m + 0xC))++;
    return true;
}

static void RpFreeClone(RpClone* c) {  // caller holds +0x918 (the clone's writers); nodes go back to the private pools by address
    if (!c) return;
    __try {
        for (int i = 0; i < c->nLots; i++) for (int j = 0; j < c->lots[i].nQ; j++) if (c->lots[i].q[j]) RpTreeRel(c->lots[i].q[j]);
        if (u8* m = c->m) {
            DWORD* bk = (DWORD*)RpP(m + 4);
            const DWORD nb = RpD(m + 8);
            if (nb > 1) {
                for (DWORD i = 0; i < nb; i++)
                    for (u8* nd = (u8*)(uintptr_t)bk[i]; nd;) { u8* nx = RpP(nd + 0x10); g_rpMapNodeFree(m, NULL, nd); nd = nx; }
                g_rpMapFree(m + 0x1C, NULL, bk, nb * 4 + 4);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ts3native_counters.rpFaults++; }
    for (int i = 0; i < c->nLots; i++) RpLotFree(&c->lots[i]);
    g_rpMassFree = true;  // every block goes: no unpin into a parent freed before its child
    for (DWORD i = 0; i < c->fps.cap; i++) if (c->fps.k[i] > 1) RpFpFree((u8*)(uintptr_t)c->fps.v[i]);
    for (DWORD i = 0; i < c->old.cap; i++) if (c->old.k[i] > 1) RpFpFree((u8*)(uintptr_t)c->old.k[i]);
    g_rpMassFree = false;
    RpTabFree(c->fps);
    RpTabFree(c->kids);
    RpTabFree(c->old), RpTabFree(c->oldLive);
    RpFree(c->lots), RpFree(c->lotBk), RpFree(c->dummyQ), RpFree(c->m), RpFree(c->p), RpFree(c);
}
static void RpCtxDone(RpCtx& x) { RpTabFree(x.qMap); RpTabFree(x.fmeMap); RpTabFree(x.fps); RpTabFree(x.kids); RpTabFree(x.exQ); RpTabFree(x.bad); }

// lots of the build whose live Qs are in exQ: map nodes dropped, lot unlinked and freed (no trees yet); false = over 64
static bool RpExclude(RpClone* c, const RpTab& exQ) {
    RpTab qs = {};
    for (int i = 0; i < c->nLots; i++) {
        bool hit = false;
        for (int j = 0; j < c->lots[i].nQ && !hit; j++) hit = c->lots[i].liveQ[j] && RpTabFind(exQ, c->lots[i].liveQ[j]);
        if (!hit) continue;
        if (c->nEx == 64) { RpTabFree(qs); return false; }
        c->ex[c->nEx++] = c->lots[i].key;
        for (int j = 0; j < c->lots[i].nQ; j++) if (c->lots[i].q[j]) RpTabPut(qs, (DWORD)(uintptr_t)c->lots[i].q[j], 1);
    }
    if (c->nEx) RpMapDrop(c, qs);
    RpTabFree(qs);
    for (int e = 0; e < c->nEx; e++)
        for (int i = 0; i < c->nLots; i++)
            if (c->lots[i].key == c->ex[e]) { RpTableUnlink(c, &c->lots[i]); RpLotFree(&c->lots[i]); c->lots[i] = c->lots[--c->nLots]; break; }
    return true;
}

// --- the whole clone (sim, +0x918 held) ---
static RpWhy g_rpBuildWhy, g_rpBuildExWhy;
static int g_rpBuildAlone, g_rpBuildNoQ;  // the last build's child footprints in their own block, FMEs without a Q  // the last build's first failure / left-out cause (sim; RpEpochBody logs them)
static int RpFaultW(RpWhy& w, DWORD code) { w = {RW_FAULT, 0, 0, 0, code, 0}; return EXCEPTION_EXECUTE_HANDLER; }
static RpClone* RpBuild(const u8* world) {
    const u8* P = RpP(world + 0x95C);
    const u8* M = P ? RpP(P + 0x24) : NULL;
    if (!P || !M) return NULL;
    RpClone* c = (RpClone*)RpAlloc(sizeof(RpClone));
    RpCtx x;
    memset(&x, 0, sizeof(x));
    if (!c) return NULL;
    __try {
        c->p = (u8*)RpAlloc(kRpPSz), c->m = (u8*)RpAlloc(kRpMSz), c->dummyQ = (u8*)RpAlloc(kRpQSz);
        if (!c->p || !c->m || !c->dummyQ) RpFailW(x, RW_ALLOC, 0, 8);
        else {
            memcpy(c->p, P, kRpPSz);
            memcpy(c->m, M, kRpMSz);
            // the map's node pool: the allocator object is inline at M+0x1C with the pool at +0; ours, not the live planner's
            // [P+0x28] (docs/94 §16.6 step 1). +4 (bucket sub-allocator, the locked EA heap) stays as copied
            if (u8* pool = PlMapPool(RpP(M + 0x1C))) *(u8**)(c->m + 0x1C) = pool; else RpFailW(x, RW_MAPPOOL);
            c->seen = PlSeenNow(P);
            *(u8**)(c->p + 0x24) = c->m;
            x.cp = c->p, x.dummyQ = c->dummyQ;
            x.excl = true;  // a footprint the copy refuses leaves its lot out (rpExclLots) instead of failing the whole build
            // lots and Q shells, table in the live chain order
            const int n = RpLiveLots(P, g_rpLive, kRpMaxLots);
            const DWORD nb = RpD(P + 0x50);
            c->lotN = nb;
            c->lotBk = (DWORD*)RpAlloc((nb + 1) * 4);
            if (n < 0 || !c->lotBk) RpFailW(x, n < 0 ? RW_LOTTABLE : RW_ALLOC, 0, (DWORD)n, 9);
            else {
                c->lotBk[nb] = ((const DWORD*)RpP(P + 0x4C))[nb];
                *(DWORD**)(c->p + 0x4C) = c->lotBk;
                for (int i = 0; i < n && !x.fail; i++) {
                    RpLot* l = RpLotAdd(c);
                    if (!l || !RpLotShell(x, g_rpLive[i].node, l)) RpFailW(x, RW_ALLOC, g_rpLive[i].key, 10);
                }
                for (int i = n - 1; i >= 0 && !x.fail; i--) RpTableLink(c, &c->lots[i]);  // head inserts in reverse = live order within a bucket
                for (int i = 0; i < c->nLots; i++) if (c->lots[i].nQ && c->lots[i].q[0]) { memcpy(c->dummyQ, c->lots[i].q[0], kRpQSz); break; }
                *(u8**)(c->dummyQ + 0x24) = NULL, *(LONG*)(c->dummyQ + 0x34) = RP_BIAS;
            }
            // map M: same bucket count and chain order; FMEs and footprints cloned as met
            const DWORD mb = RpD(M + 8);
            if (!x.fail && mb > 1) {
                DWORD* bk = (DWORD*)g_rpMapAlloc(c->m + 0x1C, NULL, mb * 4 + 4, 0x400000);
                const DWORD* lb = (const DWORD*)RpP(M + 4);
                if (!bk) RpFailW(x, RW_ALLOC, 0, 11);
                else {
                    memset(bk, 0, mb * 4);
                    bk[mb] = lb[mb];
                    *(DWORD**)(c->m + 4) = bk;
                    DWORD cnt = 0;
                    for (DWORD i = 0; i < mb && !x.fail; i++)
                        for (const u8* nd = (const u8*)(uintptr_t)lb[i]; nd && !x.fail; nd = RpP(nd + 0x10)) RpPreParent(x, RpP(nd + 8));
                    for (DWORD i = 0; i < mb && !x.fail; i++) {
                        DWORD* tail = &bk[i];
                        for (const u8* nd = (const u8*)(uintptr_t)lb[i]; nd && !x.fail; nd = RpP(nd + 0x10)) {
                            u8* f = RpP(nd + 8) ? RpFme(x, RpP(nd + 8)) : NULL;
                            if (RpP(nd + 8) && !f) continue;  // its lot is left out (exQ): no node
                            u8* cn = (u8*)g_rpMapAlloc(c->m + 0x1C, NULL, 0x18, 0);
                            if (!cn) { RpFailW(x, RW_ALLOC, 0, 12); break; }
                            memcpy(cn, nd, 0x18);
                            *(u8**)(cn + 8) = f;
                            *(DWORD*)(cn + 0x10) = 0;
                            if (f) (*(LONG*)(f + 4))++;
                            *tail = (DWORD)(uintptr_t)cn;
                            tail = (DWORD*)(cn + 0x10);
                            cnt++;
                        }
                    }
                    *(DWORD*)(c->m + 0xC) = cnt;
                }
            } else if (!x.fail && RpD(M + 0xC)) RpFailW(x, RW_MAP1, 0, RpD(M + 0xC));  // a one-bucket map with entries: not a shape we know
            for (int i = 0; i < c->nLots && !x.fail; i++) RpLotPre(x, &c->lots[i]);  // leaf-only refusals: left out too
            if (!x.fail && x.exQ.used && !RpExclude(c, x.exQ)) RpFailW(x, RW_EXCL64, x.exWhy.lot, x.exQ.used);
            const DWORD exBefore = x.exQ.used;
            for (int i = 0; i < c->nLots && !x.fail; i++) RpLotFill(x, &c->lots[i]);
            if (x.exQ.used != exBefore) RpFailW(x, RW_LEAFONLY, x.lot, x.exQ.used - exBefore);  // RpLotPre should have met it
        }
    } __except (RpFaultW(x.why, GetExceptionCode())) { x.fail = true; ts3native_counters.rpFaults++; }
    g_rpBuildWhy = x.fail ? x.why : RpWhy{}, g_rpBuildExWhy = x.exWhy;
    g_rpBuildAlone = x.nAlone, g_rpBuildNoQ = x.nNoQ;
    c->fps = x.fps, c->kids = x.kids;
    x.fps.k = x.fps.v = NULL, x.kids.k = x.kids.v = NULL;
    RpCtxDone(x);
    if (x.fail) { ts3native_counters.rpEpochFails++; RpFreeClone(c); return NULL; }
    return c;
}

// --- ring ---
struct RpRec { DWORD type, a, b, c; void* p; DWORD pad; };
static const DWORD kRpRing = 1 << 16;
static RpRec* g_rpRing;
static volatile LONG g_rpRingLock;
static volatile DWORD g_rpEpochAt;  // ring position after the last EPOCH (sim)
static volatile DWORD g_rpHead, g_rpTail, g_rpPub;  // head: next position written; tail: next read; pub: published
static volatile LONG g_rpOn, g_rpNeedEpoch;
// what asked for the next clone rebuild (0.31.4, log only): the source line in replay.cpp, negative for p4clone.cpp
static volatile LONG g_rpEpochWhy;
#define RP_NEED_EPOCH() (g_rpEpochWhy = __LINE__, g_rpNeedEpoch = 1)
#define RP_REBUILD(w) (g_rpEpochWhy = __LINE__, RpEpoch(w))
static HANDLE g_rpEvent, g_rpThread;
static bool g_rpThreaded = true;  // selftest drains on its own thread
static void RpPayloadFree(const RpRec& r);
static void RpPush(DWORD type, DWORD a, DWORD b, DWORD c, void* p) {
    Ts3nCounters& k = ts3native_counters;
    while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
    const bool full = g_rpHead - g_rpTail >= kRpRing;
    if (!full) {
        RpRec& r = g_rpRing[g_rpHead & (kRpRing - 1)];
        r.type = type, r.a = a, r.b = b, r.c = c, r.p = p, r.pad = 0;
        g_rpHead++;
    }
    InterlockedExchange(&g_rpRingLock, 0);
    if (full) {
        RpRec r = {type, a, b, c, p, 0};
        RpPayloadFree(r);
        InterlockedIncrement(&k.rpDrop);
        RP_NEED_EPOCH();
        return;
    }
    InterlockedIncrement(&k.rpRec[type]);
}

// --- payloads ---
struct RpBatchLot { u64 key; bool removed; RpLot lot; };
struct RpBatch {
    int n; RpBatchLot* lots;
    DWORD* map; int nMap;  // (lo, hi, FME) triples in the live chain order
    RpTab fps, kids;  // its footprint blocks (live -> clone) and their children
    // the whole live map at the cut, chain by chain: (key lo, key hi, live Q, FME +0x10, live footprint) per node, the bucket
    // count and the rehash policy; the consumer re-chains its map to it after the rebase (equal-key order and rehash timing)
    DWORD* order; DWORD nOrder, mapN, policy[3];
    u64* lotKeys; int nLotKeys;  // the live lot table at the cut in chain order: the consumer re-links its table to it
};
struct RpAuditRec { int n; u64* keys; u64* hash; DWORD* ents; DWORD mapN, mapCount; int nAll; DWORD* perQ; };  // perQ: 16 per lot  // nAll: live lots (n may be a subset)
struct RpSyncRec { DWORD mapN, mapCount, lots; };
static void RpBatchFree(RpBatch* b, bool owned) {  // owned: lots / FMEs not taken by the clone (needs +0x918)
    if (!b) return;
    if (owned) {
        __try {
            for (int i = 0; i < b->nMap; i++) {  // FMEs: their only reference would have been the map node
                u8* f = (u8*)(uintptr_t)b->map[i * 3 + 2];
                if (f && --*(LONG*)(f + 4) <= 0) { *(LONG*)(f + 4) = 1; ((RpDtorFn)(uintptr_t)RpD(RpP(f)))(f, NULL, 1); }
            }
            for (int i = 0; i < b->n; i++) if (!b->lots[i].removed) RpLotFree(&b->lots[i].lot);
        } __except (EXCEPTION_EXECUTE_HANDLER) { ts3native_counters.rpFaults++; }
        g_rpMassFree = true;
        for (DWORD i = 0; i < b->fps.cap; i++) if (b->fps.k[i] > 1) RpFpFree((u8*)(uintptr_t)b->fps.v[i]);
        g_rpMassFree = false;
    }
    RpTabFree(b->fps), RpTabFree(b->kids);
    RpFree(b->lotKeys), RpFree(b->order), RpFree(b->map), RpFree(b->lots), RpFree(b);
}
static void RpAuditFree(RpAuditRec* a) { if (a) { RpFree(a->keys), RpFree(a->hash), RpFree(a->ents), RpFree(a->perQ), RpFree(a); } }
// FMEs without a Q (0.30.2): a footprint with no lot goes into map M straight from 0x6376D0 (FME at 0x637809, Q cleared,
// map insert 0x628220 at 0x63787E, after PathPlanner_AddHangingFootprint 0x636BF0 with lot pointer 0) and leaves it in
// LotRouting_RemoveObjectFootprints 0x635700 with lot id 0 [verified]. No W1/W2 runs, so 0.30.1 had no record: the clone's
// map lost count at the next SYNC and was rebuilt (BP: 872 divergences, 486 builds in 10 min). The writer notes the key; the
// cut sends the live Q-less nodes of each noted key (RP_NOQ), the consumer swaps its own for them.
static const DWORD RP_NOQ = 0;  // record type (rpRec[0] / rpApplied[0], unused before)
struct RpNoQEnt { DWORD lo, hi; DWORD fme[5]; u8* liveFp; u8* copy; };
struct RpNoQRec { u64 key; bool all; int n; RpNoQEnt e[1]; };
static void RpNoQFree(RpNoQRec* r) {
    if (!r) return;
    for (int i = 0; i < r->n; i++) RpFpFree(r->e[i].copy);
    RpFree(r);
}
static void RpPayloadFree(const RpRec& r) {
    switch (r.type) {
        case RP_NOQ: RpNoQFree((RpNoQRec*)r.p); break;
        case RP_INS: RpFpFree((u8*)r.p); break;
        case RP_LOTS: RpBatchFree((RpBatch*)r.p, true); break;  // dropped before the clone took it (the sim's cut, +0x918 held)
        case RP_EPOCH: RpFreeClone((RpClone*)r.p); break;
        case RP_AUDIT: RpAuditFree((RpAuditRec*)r.p); break;
        case RP_SYNC: RpFree(r.p); break;
        default: break;
    }
}

// --- lot hash (the audit; same walk on live and clone memory; no pointers) ---
static u64 RpMix(u64 h, DWORD v) { for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 0xFF; h *= 0x100000001B3ull; } return h; }
static u64 RpEntry(u64 h, const u8* e) {  // 12-byte leaf entry: FME, mask
    const u8* f = RpP(e);
    h = RpMix(RpMix(h, RpD(e + 4)), RpD(e + 8));
    if (!f) return RpMix(h, 0);
    const u8* fp = RpP(f + 8);
    h = RpMix(h, RpD(f + 0x10));
    if (fp) {
        const RpCls* c = RpClass(RpD(fp));
        const DWORD w34 = !c || c->size > 0x34 ? RpD(fp + 0x34) : 0;  // the 0x28-byte child class ends before +0x34
        h = RpMix(RpMix(RpMix(RpMix(h, RpD(fp)), RpD(fp + 0x14)), RpD(fp + 0x18)), w34);
    }
    return h;
}
static u64 RpTreeHash(u64 h, const u8* n, int depth, DWORD* ents) {
    if (!n) return RpMix(h, 0xEEEEEEEE);
    if (depth > 40) return RpMix(h, 0xDDDDDDDD);
    h = RpMix(h, RpD(n + 4) & 0x00FFFFFF);
    if (n[4] & 1) { for (int i = 0; i < 4; i++) h = RpTreeHash(h, RpP(n + 0xC + 4 * i), depth + 1, ents); return h; }
    if (n[8] & 1) {
        const DWORD cnt = RpD(n + 0xC);
        const u8* a = (const u8*)(uintptr_t)(RpD(n + 8) & ~1u);
        h = RpMix(h, cnt);
        for (DWORD j = 0; j < cnt && j < 65536; j++) h = RpEntry(h, a + j * 12);
        *ents += cnt;
        return h;
    }
    (*ents)++;
    return RpEntry(RpMix(h, 0xFFFFFFFF), n + 8);
}
static u64 RpLotHash(const u8* node, DWORD* ents, DWORD* perQ = NULL) {  // perQ: 16 entry counts, one per Q (0.32.8)
    u64 h = 0xCBF29CE484222325ull;
    *ents = 0;
    if (perQ) memset(perQ, 0, 16 * 4);
    __try {
        const u8* g = node + 0x10;
        h = RpMix(RpMix(RpMix(RpMix(h, RpD(node)), RpD(node + 4)), g[0x8C]), RpD(g + 0x40));
        for (DWORD o = 0xA0; o < 0x120; o += 4) h = RpMix(h, RpD(g + o));
        const u8* pb = RpP(g + 0x44);
        const u8* pe = RpP(g + 0x48);
        h = RpMix(h, (DWORD)(pe - pb));
        for (const u8* p = pb; p && p < pe && p < pb + 4 * 4096; p += 4) h = RpMix(h, RpD(p));
        h = RpMix(RpMix(h, RpD(g + 0x58) - RpD(g + 0x54)), RpD(g + 0x68) - RpD(g + 0x64));
        const u8* qb = RpP(g + 0x74);
        const u8* qe = RpP(g + 0x78);
        h = RpMix(h, (DWORD)(qe - qb));
        for (const u8* qp = qb; qp && qp < qe && qp < qb + 64; qp += 4) {
            const u8* q = RpP(qp);
            if (!q) { h = RpMix(h, 0); continue; }
            for (DWORD o = 0x18; o <= 0x1C; o += 4) h = RpMix(h, RpD(q + o));
            for (DWORD o = 0x38; o <= 0x44; o += 4) h = RpMix(h, RpD(q + o));
            const DWORD e0 = *ents;
            h = RpTreeHash(h, RpP(q + 0x24), 0, ents);
            if (perQ) perQ[(qp - qb) / 4] = *ents - e0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { h = 0; }
    return h;
}
// lot signature (sim mirror): what forces a rebase. Node, built byte, level base, Q pointers, transforms, portals, lists.
static u64 RpSig(const u8* node) {
    u64 h = RpMix(0xCBF29CE484222325ull, (DWORD)(uintptr_t)node);
    __try {
        const u8* g = node + 0x10;
        h = RpMix(RpMix(h, g[0x8C]), RpD(g + 0x40));
        for (DWORD o = 0xA0; o < 0x120; o += 4) h = RpMix(h, RpD(g + o));
        for (DWORD v = 0x44; v <= 0x74; v += 0x10) {
            const u8* b = RpP(g + v);
            const u8* e = RpP(g + v + 4);
            h = RpMix(h, (DWORD)(e - b));
            if (v == 0x44 || v == 0x74) for (const u8* p = b; p && p < e && p < b + 4 * 4096; p += 4) h = RpMix(h, RpD(p));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { h = 0; }
    return h;
}

// --- the sim's mirror of what the clone was built from (sim thread only) ---
struct RpMirror { u64 key, sig; };
static RpMirror g_rpMir[kRpMaxLots];
static int g_rpNMir;
static u8* g_rpSimP;                 // the clone's P (stable until the next epoch); rebase shells point their Qs at it
static const u8* g_rpWorld;
static bool g_rpHave;                // an epoch clone was pushed
static u64 g_rpForce[64];            // W3 / W4 lots: rebase at the next cut (any thread, under the ring lock)
static int g_rpNForce;
static LONG g_rpForceAll;
static DWORD g_rpBadQ[16];           // Qs that got an insert whose footprint copy was refused (under the ring lock)
static int g_rpNBadQ;
static DWORD g_rpAuditEvery = 4, g_rpLogMis = 32;
// lots hashed per audit cut, rotating (0 = all). 0.30.1 hashed all 82 BP lots on the sim at every 4th cut: ~97 ms each, 38 ms/s
static DWORD g_rpAuditLots = 2, g_rpAuditAt;
static LONG g_rpLogged;
// Qs the clone knows (sim mirror). A record on another Q is skipped by the consumer, so the footprints it touched (their
// insert counters, polygon transform state) are refreshed from live at the next cut: their keys and ~keys are kept here
// (any writing thread under the ring lock; the writers and the cut both hold +0x918, so the table does not move under them)
static RpTab g_rpKnownQ;
static u64 g_rpUnk[256];
static int g_rpNUnk;
static LONG g_rpUnkAll;

// lots the epoch build left out (sim): never rebased, not in the SYNC / audit / map order, their Qs unknown (records on them
// are skipped and the footprints they touch refreshed); the next epoch build tries them again
static u64 g_rpEx[64];
static int g_rpNEx;
static bool RpIsEx(u64 key) {
    for (int i = 0; i < g_rpNEx; i++) if (g_rpEx[i] == key) return true;
    return false;
}
// live Qs of the left-out lots in `live` (sim, at a cut)
static void RpExQs(const RpLiveLot* live, int n, RpTab& out) {
    for (int i = 0; i < n && g_rpNEx; i++) {
        if (!RpIsEx(live[i].key)) continue;
        __try {
            const u8* g = live[i].node + 0x10;
            for (const u8* qp = RpP(g + 0x74); qp && qp < RpP(g + 0x78) && qp < RpP(g + 0x74) + 64; qp += 4)
                if (RpD(qp)) RpTabPut(out, RpD(qp), 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}
static void RpMirrorSet(const RpLiveLot* live, int n) {
    g_rpNMir = 0;
    RpTabFree(g_rpKnownQ);
    for (int i = 0; i < n && g_rpNMir < kRpMaxLots; i++) {
        g_rpMir[g_rpNMir++] = {live[i].key, RpSig(live[i].node)};
        if (RpIsEx(live[i].key)) continue;
        __try {
            const u8* g = live[i].node + 0x10;
            for (const u8* qp = RpP(g + 0x74); qp && qp < RpP(g + 0x78) && qp < RpP(g + 0x74) + 64; qp += 4)
                if (RpD(qp)) RpTabPut(g_rpKnownQ, RpD(qp), 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}
static void RpUnkAdd(u64 key) {  // under the ring lock
    for (int i = 0; i < g_rpNUnk; i++) if (g_rpUnk[i] == key) return;
    if (g_rpNUnk < 256) g_rpUnk[g_rpNUnk++] = key; else g_rpUnkAll = 1;
}
static bool RpUnkHas(const u64* set, int n, u64 key) {
    for (int i = 0; i < n; i++) if (set[i] == key) return true;
    return false;
}
static RpMirror* RpMirrorFind(u64 key) {
    for (int i = 0; i < g_rpNMir; i++) if (g_rpMir[i].key == key) return &g_rpMir[i];
    return NULL;
}

// Q-less keys noted by the writers (under the ring lock); cleared by an epoch build
static u64 g_rpNoQ[256];
static int g_rpNNoQ;
static LONG g_rpNoQAll;
static void RpNoQNote(u64 key, bool all) {
    while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
    if (all) g_rpNoQAll = 1;
    else {
        int i = 0;
        while (i < g_rpNNoQ && g_rpNoQ[i] != key) i++;
        if (i == g_rpNNoQ) { if (g_rpNNoQ < 256) g_rpNoQ[g_rpNNoQ++] = key; else g_rpNoQAll = 1; }
    }
    InterlockedExchange(&g_rpRingLock, 0);
}
// a footprint's map key: its id slot +0x7C, 0x806FE0 (+0x14 / +0x18) or the child's 0x6262B0 (~ its parent's) [verified];
// false = another slot (the cut then resyncs every Q-less node)
static bool RpFpKey(const u8* fp, u64* key) {
    const DWORD s = RpD(RpP(fp) + 0x7C);
    if (s == 0x00806FE0) { *key = ((u64)RpD(fp + 0x18) << 32) | RpD(fp + 0x14); return true; }
    if (s == 0x006262B0 && RpP(fp + 0xC) && RpD(RpP(RpP(fp + 0xC)) + 0x7C) == 0x00806FE0) {
        const u8* p = RpP(fp + 0xC);
        *key = ~(((u64)RpD(p + 0x18) << 32) | RpD(p + 0x14));
        return true;
    }
    return false;
}
// LotRouting_QuadTreeBuild 0x62F8A0 (thiscall P; lot lo, lot hi, level, create) is get-or-create [verified 0x62F8B5..0x62F901]:
// an existing Q is returned as it is. Only a missing Q with create != 0 on a lot in the table builds anything. 0.30.1 took
// every call as a rebuild (0x6376D0 calls it for each footprint add): 11,712 STALE records and 919 rebases in 10 min on BP
static bool RpW3Builds(const DWORD* slot) {
    __try {
        const u8* world = *g_spWorldSlot;
        const u8* P = world ? RpP(world + 0x95C) : NULL;
        if (!P || !slot[4]) return false;
        const int level = (int)slot[3];
        if (level + 4 > 0x10 || level < -4) return false;  // 0x62F8C6: refused
        const DWORD* bk = (const DWORD*)RpP(P + 0x4C);
        const DWORD nb = RpD(P + 0x50);
        if (!bk || !nb) return false;
        const DWORD sentinel = bk[nb];
        for (const u8* nd = (const u8*)(uintptr_t)bk[slot[1] % nb]; nd && (DWORD)(uintptr_t)nd != sentinel; nd = RpP(nd + 0x190)) {
            if (RpD(nd) != slot[1] || RpD(nd + 4) != slot[2]) continue;
            const u8* g = nd + 0x10;
            const DWORD idx = (DWORD)(level - (int)RpD(g + 0x40));
            const u8* qv = RpP(g + 0x74);
            return !(qv && idx < (DWORD)((RpP(g + 0x78) - qv) / 4) && RpD(qv + idx * 4));
        }
        return false;  // not in the table: 0x628A90 refuses
    } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}
// back-off after a failed build (sim): no new build before g_rpRetryAt on the same world, except world event 7 (its own
// call). 0.28.0 counted cuts but never set the world it compared with, so a failing build ran at every cut (P3-B live run:
// 1,978 failed builds of up to 10.5 ms in 10 min). 0.29.1: the wait doubles per failure on the same world, 10 s to 160 s
// (0.29.0 BP: 60 failed builds of ~80 ms each, 8 of the 10.8 ms/s cut cost).
static ULONGLONG g_rpRetryAt;
static const u8* g_rpFailWorld;
static int g_rpFailRun;  // failed builds in a row on g_rpFailWorld
static int g_rpLastWhy;  // the cause last logged (a new cause is logged past the first 8 failures too)
static const DWORD kRpRetryMs = 10000;
static PlSeen g_rpSimSeen;  // the game's pool objects at the last clone build (sim)
static void RpEpochBody(const u8* world);
static void RpEpoch(const u8* world) {  // sim, +0x918 held; clone work on private pools (pools.cpp)
    const LONG op = RpOpEnter();
    RpEpochBody(world);
    RpOpLeave(op);
}
static void RpEpochBody(const u8* world) {
    Ts3nCounters& k = ts3native_counters;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    RpClone* c = RpBuild(world);
    const LONG us = UsSince(t0);
    k.rpEpochUs += us;
    Max(&k.rpEpochMaxUs, us);
    if (!c) {
        RP_NEED_EPOCH();
        g_rpFailRun = world == g_rpFailWorld ? g_rpFailRun + 1 : 0;
        g_rpRetryAt = GetTickCount64() + ((ULONGLONG)kRpRetryMs << (g_rpFailRun < 4 ? g_rpFailRun : 4));
        g_rpFailWorld = world;
        if (k.rpEpochFails <= 8 || g_rpBuildWhy.code != g_rpLastWhy) RpWhyLog("clone build failed", g_rpBuildWhy);
        if (g_rpBuildExWhy.code && (k.rpEpochFails <= 8 || g_rpBuildWhy.code != g_rpLastWhy)) RpWhyLog("clone build: first lot left out", g_rpBuildExWhy);
        g_rpLastWhy = g_rpBuildWhy.code;
        return;
    }
    k.rpEpochs++;
    g_rpRetryAt = 0, g_rpFailWorld = NULL, g_rpFailRun = 0;
    g_rpSimSeen = c->seen;
    g_rpNEx = c->nEx;
    memcpy(g_rpEx, c->ex, c->nEx * sizeof(u64));
    k.rpExclLots = c->nEx;
    if (c->nEx) {
        Log("snap replay: %d lot(s) left out of the clone (a footprint the copy refuses; first 0x%08lX%08lX)", c->nEx,
            (DWORD)(c->ex[0] >> 32), (DWORD)c->ex[0]);
        RpWhyLog("first lot left out", g_rpBuildExWhy);
    }
    g_rpSimP = c->p;
    g_rpWorld = world;
    g_rpHave = true;
    g_rpNeedEpoch = 0;
    const int n = RpLiveLots(RpP(world + 0x95C), g_rpLive, kRpMaxLots);
    RpMirrorSet(g_rpLive, n < 0 ? 0 : n);
    while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
    g_rpNForce = 0, g_rpForceAll = 0, g_rpNUnk = 0, g_rpUnkAll = 0, g_rpNNoQ = 0, g_rpNoQAll = 0;
    InterlockedExchange(&g_rpRingLock, 0);
    RpPush(RP_EPOCH, 0, 0, 0, c);
    g_rpEpochAt = g_rpHead;
    const LONG why = g_rpEpochWhy;
    g_rpEpochWhy = 0;
    Log("snap replay: clone built (%d lots, %lu map entries; %d child footprints outside their parent's list, %d FMEs without a Q) in %ld us; "
        "asked by %s:%ld", c->nLots, RpD(c->m + 0xC), g_rpBuildAlone, g_rpBuildNoQ, us, why < 0 ? "p4clone.cpp" : "replay.cpp", why < 0 ? -why : why);
}

// rebase of the lots in `set` (keys), removal of `gone`, and fresh copies of the live footprints whose key is in `unk`
// (touched by records the consumer skips) (sim, +0x918 held)
// the cut's Q-less records: the live map's Q-less nodes for each noted key (or all of them), in chain order, with a fresh
// footprint copy each (sim, +0x918 held); false = the clone must be rebuilt
static bool RpNoQOne(const u8* M, u64 key, bool all) {
    const DWORD mb = RpD(M + 8);
    const DWORD* lb = (const DWORD*)RpP(M + 4);
    if (mb <= 1 || !lb) return true;
    int n = 0;
    const DWORD b0 = all ? 0 : (DWORD)key % mb, b1 = all ? mb : b0 + 1;
    for (int pass = 0; pass < 2; pass++) {
        RpNoQRec* r = NULL;
        if (pass) {
            r = (RpNoQRec*)RpAlloc(sizeof(RpNoQRec) + (n ? n - 1 : 0) * sizeof(RpNoQEnt));
            if (!r) return false;
            r->key = key, r->all = all, r->n = 0;
        }
        for (DWORD i = b0; i < b1; i++)
            for (const u8* nd = (const u8*)(uintptr_t)lb[i]; nd; nd = RpP(nd + 0x10)) {
                const u8* f = RpP(nd + 8);
                if (!f || RpD(f + 0xC) || (!all && (RpD(nd) != (DWORD)key || RpD(nd + 4) != (DWORD)(key >> 32)))) continue;
                if (!pass) { n++; continue; }
                if (r->n == n) break;  // shortcut: nodes added between the passes cannot happen (+0x918 held)
                RpNoQEnt& e = r->e[r->n++];
                e.lo = RpD(nd), e.hi = RpD(nd + 4);
                memcpy(e.fme, f, 0x14);
                e.liveFp = RpP(f + 8);
                e.copy = e.liveFp ? RpFpCopy(e.liveFp) : NULL;
            }
        if (pass) RpPush(RP_NOQ, 0, 0, 0, r);
    }
    return true;
}
static bool RpNoQCut(const u8* P) {
    u64 keys[256];
    int nk;
    bool all;
    while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
    nk = g_rpNNoQ, all = g_rpNoQAll != 0;
    memcpy(keys, g_rpNoQ, nk * 8);
    g_rpNNoQ = 0, g_rpNoQAll = 0;
    InterlockedExchange(&g_rpRingLock, 0);
    if (!nk && !all) return true;
    __try {
        const u8* M = RpP(P + 0x24);
        if (all) return RpNoQOne(M, 0, true);
        for (int i = 0; i < nk; i++) if (!RpNoQOne(M, keys[i], false)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
static RpBatch* RpRebase(const u8* world, const RpLiveLot* set, int n, const u64* gone, int nGone, const u64* unk, int nUnk,
                         const RpTab& exQ) {
    RpBatch* b = (RpBatch*)RpAlloc(sizeof(RpBatch));
    if (!b) return NULL;
    b->lots = (RpBatchLot*)RpAlloc((n + nGone) * sizeof(RpBatchLot) + 4);
    if (!b->lots) { RpFree(b); return NULL; }
    RpCtx x;
    memset(&x, 0, sizeof(x));
    x.cp = g_rpSimP;
    x.dummyQ = NULL;  // FMEs of other lots are not copied (the map walk below takes only these lots' Qs)
    __try {
        for (int i = 0; i < nGone; i++) { b->lots[b->n].key = gone[i]; b->lots[b->n].removed = true; b->n++; }
        for (int i = 0; i < n && !x.fail; i++) {
            RpBatchLot& bl = b->lots[b->n++];
            bl.key = set[i].key, bl.removed = false;
            if (!RpLotShell(x, set[i].node, &bl.lot)) x.fail = true;
        }
        const u8* M = RpP(RpP(world + 0x95C) + 0x24);
        const DWORD mb = RpD(M + 8);
        const DWORD* lb = (const DWORD*)RpP(M + 4);
        int cap = 0;
        for (DWORD i = 0; mb > 1 && i < mb && !x.fail; i++)
            for (const u8* nd = (const u8*)(uintptr_t)lb[i]; nd && !x.fail; nd = RpP(nd + 0x10))
                if (RpP(nd + 8) && (RpTabFind(x.qMap, RpD(RpP(nd + 8) + 0xC)) || RpUnkHas(unk, nUnk, ((u64)RpD(nd + 4) << 32) | RpD(nd))))
                    RpPreParent(x, RpP(nd + 8));
        x.excl = true;  // a refused refresh (a left-out lot's footprint) is skipped, not a failed batch
        for (DWORD i = 0; mb > 1 && i < mb && !x.fail; i++)  // footprints touched on unknown Qs: fresh copies (swapped in on apply)
            for (const u8* nd = (const u8*)(uintptr_t)lb[i]; nd && !x.fail && nUnk; nd = RpP(nd + 0x10))
                if (RpP(nd + 8) && RpUnkHas(unk, nUnk, ((u64)RpD(nd + 4) << 32) | RpD(nd))) RpFpFor(x, RpP(RpP(nd + 8) + 8), RpD(RpP(nd + 8) + 0xC));
        x.excl = false;
        for (DWORD i = 0; mb > 1 && i < mb && !x.fail; i++)
            for (const u8* nd = (const u8*)(uintptr_t)lb[i]; nd && !x.fail; nd = RpP(nd + 0x10)) {
                const u8* f = RpP(nd + 8);
                if (!f || !RpTabFind(x.qMap, RpD(f + 0xC))) continue;
                u8* cf = RpFme(x, f);
                if (!cf) break;
                if (b->nMap == cap) {
                    cap = cap ? cap * 2 : 1024;
                    DWORD* m = (DWORD*)RpAlloc(cap * 12);
                    if (!m) { x.fail = true; break; }
                    if (b->map) { memcpy(m, b->map, b->nMap * 12); RpFree(b->map); }
                    b->map = m;
                }
                b->map[b->nMap * 3] = RpD(nd), b->map[b->nMap * 3 + 1] = RpD(nd + 4), b->map[b->nMap * 3 + 2] = (DWORD)(uintptr_t)cf;
                b->nMap++;
                (*(LONG*)(cf + 4))++;  // the batch holds it for its map node (taken over on apply); before the trees, so no orphan
            }
        for (int i = 0; i < b->n && !x.fail; i++) if (!b->lots[i].removed) RpLotFill(x, &b->lots[i].lot);
    } __except (EXCEPTION_EXECUTE_HANDLER) { x.fail = true; ts3native_counters.rpFaults++; }
    b->fps = x.fps, b->kids = x.kids;
    x.fps.k = x.fps.v = NULL, x.kids.k = x.kids.v = NULL;
    RpCtxDone(x);
    if (!x.fail) {
        __try {
            const u8* M = RpP(RpP(world + 0x95C) + 0x24);
            const DWORD mb = RpD(M + 8), cnt = RpD(M + 0xC);
            const DWORD* lb = (const DWORD*)RpP(M + 4);
            b->mapN = mb;
            memcpy(b->policy, M + 0x10, 12);
            b->order = (DWORD*)RpAlloc((cnt + 1) * 20);
            if (!b->order) x.fail = true;
            DWORD skipped = 0;
            for (DWORD i = 0; mb > 1 && i < mb && !x.fail; i++)
                for (const u8* nd = (const u8*)(uintptr_t)lb[i]; nd; nd = RpP(nd + 0x10)) {
                    if (b->nOrder > cnt) { x.fail = true; break; }
                    const u8* f = RpP(nd + 8);
                    if (f && exQ.used && RpTabFind(exQ, RpD(f + 0xC))) { skipped++; continue; }  // a left-out lot's node
                    DWORD* o = b->order + b->nOrder++ * 5;
                    o[0] = RpD(nd), o[1] = RpD(nd + 4);
                    o[2] = f && RpTabFind(g_rpKnownQ, RpD(f + 0xC)) ? RpD(f + 0xC) : 0, o[3] = f ? RpD(f + 0x10) : 0, o[4] = f ? RpD(f + 8) : 0;
                }
            if (b->nOrder + skipped != cnt) x.fail = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { x.fail = true; }
    }
    if (x.fail) { RpBatchFree(b, true); return NULL; }
    return b;
}

// --- recorder (any writing thread; from SpNote before its dirty lock) ---
#include <psapi.h>  // K32GetProcessMemoryInfo (kernel32 on Windows 7+)
// Prebuild=1 (Rodney 2026-10-18): every lot's graph built on the loading screen, so no lazy first use (LotGraph_Build and the
// clone's rebase copy of that lot) happens in play. The game's own path, as LotSearch_Entry 0x640E66 runs it: for each lot in
// the planner's table that LotTable_Contains 0x628A90 accepts and whose built byte +0x8C is 0, RoomGraph_FirstUse 0x63D2F0
// (thiscall P, entry, outside room word +0x16C) -> LotGraph_Build 0x63CB90 -> LotRouting_FinishBuild 0x63CAF0 ->
// RestoreLotBlockedBits 0x82A190. Sim thread, +0x918 held, before the clone build.
// shortcut: lots not yet in the planner's lot table at world event 7 are not built (the table walk is the only lot list
// used here); rpRebaseLots in play shows what is left.
typedef char(__fastcall* RpFirstUseFn)(const u8* p, void*, u8* entry, DWORD room);  // 0x63D2F0, ret 8
typedef char(__fastcall* RpContainsFn)(const u8* p, void*, DWORD lo, DWORD hi);     // 0x628A90, ret 8
static RpFirstUseFn g_rpFirstUse = (RpFirstUseFn)(uintptr_t)0x0063D2F0;
static RpContainsFn g_rpContains = (RpContainsFn)(uintptr_t)0x00628A90;
static bool g_rpPrebuild;
static LONG RpPrivateKB() {
    PROCESS_MEMORY_COUNTERS_EX m = {};
    m.cb = sizeof(m);
    return K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&m, sizeof(m)) ? (LONG)(m.PrivateUsage >> 10) : 0;
}
static void RpPrebuild(const u8* world) {
    Ts3nCounters& k = ts3native_counters;
    const u8* P = RpP(world + 0x95C);
    const int n = P ? RpLiveLots(P, g_rpLive, kRpMaxLots) : 0;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    const LONG kb0 = RpPrivateKB();
    int built = 0, failed = 0, already = 0;
    for (int i = 0; i < n; i++) {
        u8* g = g_rpLive[i].node + 0x10;
        if (g[0x8C]) { already++; continue; }
        __try {
            if (!g_rpContains(P, NULL, (DWORD)g_rpLive[i].key, (DWORD)(g_rpLive[i].key >> 32))) continue;  // the active lot: plans skip it too
            if (g_rpFirstUse(P, NULL, g, *(const WORD*)(g + 0x16C))) built++; else failed++;
        } __except (EXCEPTION_EXECUTE_HANDLER) { failed++; }
    }
    const LONG us = UsSince(t0), kb = RpPrivateKB() - kb0;
    k.rpPrebuilt += built, k.rpPrebuildFail += failed, k.rpPrebuildUs += us, k.rpPrebuildKB = kb;
    Log("snap replay: prebuilt %d lot graphs (%d already built, %d failed, %d lots in the table) in %ld ms; process private +%ld KB",
        built, already, failed, n, us / 1000, kb);
}
static void RpPreBody(int site, const DWORD* slot);
static void RpPre(int site, const DWORD* slot) {
    if (!g_rpOn) return;
    __try { RpPreBody(site, slot); } __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedIncrement(&ts3native_counters.rpFaults); RP_NEED_EPOCH(); }
}
static void RpPreBody(int site, const DWORD* slot) {
    const SnSite& s = kSn[site];
    if (site == SN_WEV) {
        const DWORD id = slot[2];
        if (id == 5 || id == 9 || id == 0x0A) RP_NEED_EPOCH();
        if (id == 7 && RgIsSim(GetCurrentThreadId())) {  // the load's G build has run (docs/94 §9.1): build the clone now
            const u8* world = *g_spWorldSlot;
            static const DWORD kInf = 0xFFFFFFFF;
            if (world && RpP(world + 0x95C) && g_spAcq((void*)(world + 0x918), NULL, &kInf) > 0) {
                if (g_rpPrebuild) RpPrebuild(world);
                RP_REBUILD(world);
                g_spRel((void*)(world + 0x918), NULL);
                g_rpPub = g_rpHead;
                if (g_rpThreaded) SetEvent(g_rpEvent);
            } else RP_NEED_EPOCH();
        }
        return;
    }
    if (!g_rpHave) return;
    switch (s.leaf) {
        case SL_W1: case SL_W2: {
            if (s.leaf == SL_W1 && slot[0] == kRpRet665F87) return;  // W1's own child recursion: the replayed parent inserts them
            u8* fp = s.leaf == SL_W1 ? (u8*)(uintptr_t)slot[1] : NULL;
            if (!RpTabFind(g_rpKnownQ, slot[-2])) {  // the consumer will skip it: refresh what it touches at the next cut
                u64 key = ((u64)slot[2] << 32) | slot[1];
                __try { if (fp) key = ((u64)RpD(fp + 0x18) << 32) | RpD(fp + 0x14); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
                RpUnkAdd(key), RpUnkAdd(~key);
                InterlockedExchange(&g_rpRingLock, 0);
            }
            if (s.leaf == SL_W1) {
                u8* cp = fp ? RpFpCopy(fp) : NULL;
                if (fp && !cp) {  // refused copy (unknown class, ...): the cut rebases that Q's lot before its SYNC and audit
                    while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
                    int i = 0;
                    while (i < g_rpNBadQ && g_rpBadQ[i] != slot[-2]) i++;
                    if (i == g_rpNBadQ) { if (g_rpNBadQ < 16) g_rpBadQ[g_rpNBadQ++] = slot[-2]; else g_rpForceAll = 1; }
                    InterlockedExchange(&g_rpRingLock, 0);
                }
                RpPush(RP_INS, slot[-2], slot[1], slot[2], cp);
            }
            else RpPush(RP_REM, slot[-2], slot[1], slot[2], NULL);
            break;
        }
        case SL_GRIDADD: case SL_LOTREMOVEFP: {  // Q-less map nodes may change for this key (lot 0 only)
            u64 key = 0;
            if (s.leaf == SL_LOTREMOVEFP) { if (slot[1] | slot[2]) break; key = ((u64)slot[4] << 32) | slot[3]; RpNoQNote(key, false); }
            else if (!slot[2] && slot[1]) { const bool ok = RpFpKey((const u8*)(uintptr_t)slot[1], &key); RpNoQNote(key, !ok); }
            break;
        }
        case SL_W3: case SL_W4: {  // a tree rebuilt or a lot graph built: rebase at the next cut, skip its records till then
            if (s.leaf == SL_W3 && !RpW3Builds(slot)) break;  // a get of an existing Q (0.30.2)
            const u64 key = s.leaf == SL_W3 ? ((u64)slot[2] << 32) | slot[1] : ((u64)RpD((const u8*)(uintptr_t)slot[1] + 4) << 32) | RpD((const u8*)(uintptr_t)slot[1]);
            while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
            int i = 0;
            while (i < g_rpNForce && g_rpForce[i] != key) i++;
            if (i == g_rpNForce) { if (g_rpNForce < 64) g_rpForce[g_rpNForce++] = key; else g_rpForceAll = 1; }
            InterlockedExchange(&g_rpRingLock, 0);
            RpPush(RP_STALE, (DWORD)key, (DWORD)(key >> 32), 0, NULL);
            break;
        }
        default: break;
    }
}

// --- cut (sim, +0x918 held; from SpBuildVersion through the strategy's begin) ---
static void RpCut(const u8* world) {
    Ts3nCounters& k = ts3native_counters;
    if (!g_rpOn) return;
    if (g_rpBytes > g_rpCapBytes) {
        g_rpOn = 0;
        k.rpCapOff = 1;
        k.rpReady &= ~1;
        Log("snap replay: %ld KB, above CapMB: switched off", g_rpBytes >> 10);
        if (g_rpThreaded) SetEvent(g_rpEvent);
        return;
    }
    if (g_rpNeedEpoch && world == g_rpFailWorld && GetTickCount64() < g_rpRetryAt) goto publish;  // backing off; no audit of a stale clone
    if (g_rpHave && !g_rpNeedEpoch && world == g_rpWorld && PlMoved(g_rpSimSeen, RpP(world + 0x95C))) {
        k.plEpochs++;  // PathPlanner_Reset recreated the game's pools: new epoch (docs/94 §16.6 step 3)
        RP_NEED_EPOCH();
    }
    // a rebuild waits while the last EPOCH is not applied yet: each clone is ~40 MB on BP, and 0.32.4 had up to three
    // alive (126 MB of 128) when the consumer lagged behind rebuilds (0.32.8)
    if (g_rpNeedEpoch && g_rpHave && world == g_rpWorld && (LONG)(g_rpEpochAt - g_rpTail) > 0) goto publish;
    if (!g_rpHave || g_rpNeedEpoch || world != g_rpWorld) { if (!g_rpNeedEpoch) g_rpEpochWhy = __LINE__; RpEpoch(world); goto publish; }
    {
        const u8* P = RpP(world + 0x95C);
        const int n = RpLiveLots(P, g_rpLive, kRpMaxLots);
        if (n < 0) { RP_NEED_EPOCH(); return; }
        u64 force[64];
        static u64 unk[256];
        int nForce, nUnk;
        bool all, unkAll;
        while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
        nForce = g_rpNForce, all = g_rpForceAll != 0, nUnk = g_rpNUnk, unkAll = g_rpUnkAll != 0;
        memcpy(force, g_rpForce, nForce * 8);
        memcpy(unk, g_rpUnk, nUnk * 8);
        DWORD badQ[16];
        const int nBad = g_rpNBadQ;
        memcpy(badQ, g_rpBadQ, nBad * 4);
        g_rpNForce = 0, g_rpForceAll = 0, g_rpNUnk = 0, g_rpUnkAll = 0, g_rpNBadQ = 0;
        InterlockedExchange(&g_rpRingLock, 0);
        if (unkAll) { RP_REBUILD(world); goto publish; }  // too many keys touched on unknown Qs: rebuild
        if (!RpNoQCut(P)) { RP_REBUILD(world); goto publish; }  // Q-less nodes first: a batch below re-chains the map to live order
        // an insert the copy refused on a lot the clone holds: rebuild now (that lot is left out), before this cut's SYNC/audit
        bool badIn = false;
        for (int i = 0; i < n && nBad && !badIn; i++) {
            if (g_rpNEx && RpIsEx(g_rpLive[i].key)) continue;
            __try {
                const u8* g = g_rpLive[i].node + 0x10;
                for (const u8* qp = RpP(g + 0x74); qp && qp < RpP(g + 0x78) && qp < RpP(g + 0x74) + 64 && !badIn; qp += 4)
                    for (int b = 0; b < nBad; b++) badIn |= RpD(qp) == badQ[b];
            } __except (EXCEPTION_EXECUTE_HANDLER) { badIn = true; }
        }
        if (badIn) { RP_REBUILD(world); goto publish; }
        static RpLiveLot set[kRpMaxLots];
        static u64 gone[kRpMaxLots];
        int ns = 0, ng = 0;
        RpTab exLive = {};  // live Qs of the left-out lots (they stay out until the next epoch build)
        RpExQs(g_rpLive, n, exLive);
        int nExLive = 0;
        for (int i = 0; i < n; i++) {
            if (g_rpNEx && RpIsEx(g_rpLive[i].key)) { nExLive++; continue; }
            const RpMirror* m = RpMirrorFind(g_rpLive[i].key);
            bool f = all || !m || m->sig != RpSig(g_rpLive[i].node);
            for (int j = 0; !f && j < nForce; j++) f = force[j] == g_rpLive[i].key;
            if (f) set[ns++] = g_rpLive[i];
        }
        for (int i = 0; i < g_rpNMir; i++) {
            int j = 0;
            while (j < n && g_rpLive[j].key != g_rpMir[i].key) j++;
            if (j == n) gone[ng++] = g_rpMir[i].key;
        }
        if (ns || ng || nUnk) {
            LARGE_INTEGER t0;
            QueryPerformanceCounter(&t0);
            RpMirrorSet(g_rpLive, n);  // first: the batch's map order names Qs of the lots as they are now
            const LONG op = RpOpEnter();  // private pools; the push too (a full ring frees the batch here)
            RpBatch* b = RpRebase(world, set, ns, gone, ng, unk, nUnk, exLive);
            const LONG us = UsSince(t0);
            k.rpRebaseUs += us;
            Max(&k.rpRebaseMaxUs, us);
            if (b && (b->lotKeys = (u64*)RpAlloc(n * 8 + 8)) != NULL) {
                for (int i = 0; i < n; i++) b->lotKeys[i] = g_rpLive[i].key;
                b->nLotKeys = n;
            }
            if (b) RpPush(RP_LOTS, 0, 0, 0, b);
            RpOpLeave(op);
            if (!b) { RpTabFree(exLive); RP_REBUILD(world); goto publish; }
            k.rpRebaseLots += ns + ng;
        }
        const u8* M = RpP(P + 0x24);
        DWORD exNodes = 0;  // map entries of the left-out lots (the clone does not hold them)
        if (exLive.used) {
            __try {
                const DWORD mb = RpD(M + 8);
                const DWORD* lb = (const DWORD*)RpP(M + 4);
                for (DWORD i = 0; mb > 1 && i < mb; i++)
                    for (const u8* nd = (const u8*)(uintptr_t)lb[i]; nd; nd = RpP(nd + 0x10))
                        if (RpP(nd + 8) && RpTabFind(exLive, RpD(RpP(nd + 8) + 0xC))) exNodes++;
            } __except (EXCEPTION_EXECUTE_HANDLER) { RP_NEED_EPOCH(); }
        }
        RpTabFree(exLive);
        RpSyncRec* sy = (RpSyncRec*)RpAlloc(sizeof(RpSyncRec));
        if (sy) { sy->mapN = RpD(M + 8), sy->mapCount = RpD(M + 0xC) - exNodes, sy->lots = (DWORD)(n - nExLive); RpPush(RP_SYNC, 0, 0, 0, sy); }
        if (g_rpAuditEvery && (k.spCuts + 1) % (LONG)g_rpAuditEvery == 0 && n > 0) {
            LARGE_INTEGER t0;
            QueryPerformanceCounter(&t0);
            RpAuditRec* a = (RpAuditRec*)RpAlloc(sizeof(RpAuditRec));
            if (a && (a->keys = (u64*)RpAlloc(n * 8)) && (a->hash = (u64*)RpAlloc(n * 8)) && (a->ents = (DWORD*)RpAlloc(n * 4))) {
                a->n = 0, a->nAll = n - nExLive;
                const DWORD from = g_rpAuditAt;
                if (g_rpAuditLots) g_rpAuditAt = (g_rpAuditAt + g_rpAuditLots) % (DWORD)n;
                for (int j = 0; j < n; j++) {
                    const int i = (int)((from + (DWORD)j) % (DWORD)n);
                    if (g_rpAuditLots && j >= (int)g_rpAuditLots) break;
                    if (g_rpNEx && RpIsEx(g_rpLive[i].key)) continue;
                    a->keys[a->n] = g_rpLive[i].key;
                    if (!a->perQ) a->perQ = (DWORD*)RpAlloc(n * 16 * 4);
                    a->hash[a->n] = RpLotHash(g_rpLive[i].node, &a->ents[a->n], a->perQ ? a->perQ + a->n * 16 : NULL);
                    a->n++;
                }
                a->mapN = RpD(M + 8), a->mapCount = RpD(M + 0xC) - exNodes;
                RpPush(RP_AUDIT, 0, 0, 0, a);
            } else RpAuditFree(a);
            const LONG us = UsSince(t0);
            k.rpAuditUs += us;
            Max(&k.rpAuditMaxUs, us);
        }
    }
publish:
    {
        const DWORD depth = g_rpHead - g_rpTail;
        Max(&k.rpRingMax, (LONG)depth);
    }
    g_rpPub = g_rpHead;
    if (g_rpThreaded) SetEvent(g_rpEvent);
}

// --- consumer (one thread; the clone and its footprint table are its own) ---
static RpClone* g_rpClone;
static RpLot* RpFindQ(RpClone* c, DWORD liveQ, u8** q) {
    for (int i = 0; i < c->nLots; i++) {
        RpLot& l = c->lots[i];
        for (int j = 0; j < l.nQ; j++) if (l.liveQ[j] == liveQ && l.q[j]) { *q = l.q[j]; return &l; }
    }
    return NULL;
}
static RpLot* RpFindLot(RpClone* c, u64 key) {
    for (int i = 0; i < c->nLots; i++) if (c->lots[i].key == key) return &c->lots[i];
    return NULL;
}
// the clone footprint for a live one: the one FMEs still hold, else the record's copy (registered)
static void RpRepin(RpClone* c, u8* old, u8* fresh);
static void RpRevOld(RpTab& rev, const u8* blk, const u8* live);
// live -> its copy, for blk and its children (list node +12 names the live child)
static void RpWant(RpTab& want, u8* blk, DWORD live) {
    RpTabPut(want, live, (DWORD)(uintptr_t)blk);
    if (!RpHasList(blk)) return;
    for (u8* l = *(u8**)(blk + 0x24); l != blk + 0x24; l = *(u8**)l) RpWant(want, *(u8**)(l + 8), *(DWORD*)(l + 12));
}
// replaced blocks (c->old) and the live footprints they copy (c->oldLive); freed once no FME holds them
static void RpOldLiveAdd(RpTab& t, const u8* blk, DWORD live) {
    RpTabPut(t, live, 1);
    if (!RpHasList(blk)) return;
    for (const u8* l = RpP(blk + 0x24); l != blk + 0x24; l = RpP(l)) RpOldLiveAdd(t, RpP(l + 8), RpD(l + 12));
}
static void RpOldGc(RpClone* c) {
    if (!c->old.used) return;
    bool freed = false;
    for (DWORD i = 0; i < c->old.cap; i++)
        if (c->old.k[i] > 1 && RpFpRefs((u8*)(uintptr_t)c->old.k[i]) <= 0) {
            RpFpFree((u8*)(uintptr_t)c->old.k[i]);
            c->old.k[i] = 1;
            freed = true;
            ts3native_counters.rpGcFreed++;
        }
    if (!freed) return;
    RpTabFree(c->oldLive);
    for (DWORD i = 0; i < c->old.cap; i++)
        if (c->old.k[i] > 1) RpOldLiveAdd(c->oldLive, (const u8*)(uintptr_t)c->old.k[i], (DWORD)(uintptr_t)((const RpFpHdr*)(uintptr_t)(c->old.k[i] - 16))->live);
}
// old leaves the tables (its own-block children move to fresh) and waits in c->old until no FME holds it
static void RpRetire(RpClone* c, u8* old, u8* fresh) {
    RpRepin(c, old, fresh);
    RpKids(c->kids, old, false);
    RpTabPut(c->old, (DWORD)(uintptr_t)old, 1);
    RpOldLiveAdd(c->oldLive, old, (DWORD)(uintptr_t)((const RpFpHdr*)(old - 16))->live);
}
// every clone FME on another copy of a live footprint in want moves to want's copy: one clone object per live footprint,
// as the game has one (0.32.4; 0.32.1 kept the old block for FMEs of other lots, so a later insert with a changed child list,
// or a child seen again on its own, gave the clone two copies whose insert counters then differed)
static void RpUnify(RpClone* c, const RpTab& want) {
    RpTab rev = {};
    for (DWORD i = 0; i < c->fps.cap; i++) if (c->fps.k[i] > 1) RpTabPut(rev, c->fps.v[i], c->fps.k[i]);
    for (DWORD i = 0; i < c->kids.cap; i++) if (c->kids.k[i] > 1) RpTabPut(rev, c->kids.v[i], c->kids.k[i]);
    for (DWORD i = 0; i < c->old.cap; i++)
        if (c->old.k[i] > 1) RpRevOld(rev, (const u8*)(uintptr_t)c->old.k[i], ((const RpFpHdr*)(uintptr_t)(c->old.k[i] - 16))->live);
    u8* m = c->m;
    DWORD* bk = (DWORD*)RpP(m + 4);
    const DWORD nb = RpD(m + 8);
    for (DWORD i = 0; nb > 1 && i < nb; i++)
        for (u8* nd = (u8*)(uintptr_t)bk[i]; nd; nd = RpP(nd + 0x10)) {
            u8* f = RpP(nd + 8);
            u8* x = f ? RpP(f + 8) : NULL;
            const DWORD* lv = x ? RpTabFind(rev, (DWORD)(uintptr_t)x) : NULL;
            const DWORD* to = lv ? RpTabFind(want, *lv) : NULL;
            if (!to || (u8*)(uintptr_t)*to == x) continue;
            (*(LONG*)(x + 8))--, (*(LONG*)((u8*)(uintptr_t)*to + 8))++;
            *(u8**)(f + 8) = (u8*)(uintptr_t)*to;
        }
    RpTabFree(rev);
}
// old takes fresh's state in place (same class, same live children, same level counts): every field but its reference
// count, child list head, level array pointer and parent back pointer; the level entries' polygon vectors are swapped, so
// freeing fresh frees the old arrays. FMEs keep pointing at old. false = shapes differ (nothing changed).
// 0.32.4 reused the old copy as it was: BP lot audits found fewer tree entries than live (the old bounds / counters), since
// 0.32.1 no longer refreshed copies through its own records (0.32.8)
static bool RpFpRefreshOk(const u8* o, const u8* f) {
    if (RpD(o) != RpD(f)) return false;
    const RpCls* c = RpClass(RpD(o));
    if (!c) return false;
    if (c->cache && (RpLevels(o) != RpLevels(f) || !RpP(o + 0x38) || !RpP(f + 0x38))) return false;
    if (!RpHasList(o)) return true;
    const u8* lo = RpP(o + 0x24);
    const u8* lf = RpP(f + 0x24);
    for (; lo != o + 0x24 && lf != f + 0x24; lo = RpP(lo), lf = RpP(lf))
        if (RpD(lo + 12) != RpD(lf + 12) || !RpFpRefreshOk(RpP(lo + 8), RpP(lf + 8))) return false;
    return lo == o + 0x24 && lf == f + 0x24;
}
static void RpFpRefreshIn(u8* o, u8* f) {
    const RpCls* c = RpClass(*(DWORD*)o);
    const LONG refs = *(LONG*)(o + 8);
    const DWORD par = *(DWORD*)(o + 0xC);
    u8 head[8];
    const bool list = RpHasList(o);
    if (list) memcpy(head, o + 0x24, 8);
    u8* lc = c->cache ? *(u8**)(o + 0x38) : NULL;
    memcpy(o, f, c->size);
    *(LONG*)(o + 8) = refs;
    if (c->child) *(DWORD*)(o + 0xC) = par;
    if (list) memcpy(o + 0x24, head, 8);
    if (c->cache) {
        *(u8**)(o + 0x38) = lc;
        u8* fc = *(u8**)(f + 0x38);
        const DWORD lv = RpLevels(o);
        for (DWORD l = 0; l < lv; l++) {
            u8 t[0x24];
            memcpy(t, lc + l * 0x24, 0x24), memcpy(lc + l * 0x24, fc + l * 0x24, 0x24), memcpy(fc + l * 0x24, t, 0x24);
        }
    }
    if (!list) return;
    for (u8 *lo = *(u8**)(o + 0x24), *lf = *(u8**)(f + 0x24); lo != o + 0x24; lo = *(u8**)lo, lf = *(u8**)lf) RpFpRefreshIn(*(u8**)(lo + 8), *(u8**)(lf + 8));
}
static u8* RpFpUse(RpClone* c, u8* live, u8* fresh) {
    DWORD* v = RpTabFind(c->fps, (DWORD)(uintptr_t)live);
    u8* old = v ? (u8*)(uintptr_t)*v : NULL;
    // other FMEs still hold the old copy: it takes the record's state in place (0.32.8), unless the live children changed
    // (0.32.4: BP lot audits found the replayed insert putting the old children in): then fresh replaces it below
    if (old && RpFpRefs(old) > 0 && (!fresh || RpFpRefreshOk(old, fresh))) {
        if (fresh) RpFpRefreshIn(old, fresh);
        RpFpFree(fresh);
        return old;
    }
    if (!fresh) return NULL;
    bool unify = old && RpFpRefs(old) > 0;
    if (old) RpRetire(c, old, fresh);
    if (!RpTabPut(c->fps, (DWORD)(uintptr_t)live, (DWORD)(uintptr_t)fresh)) { RpFpFree(fresh); return NULL; }
    RpKids(c->kids, fresh, true);
    RpTab want = {};
    RpWant(want, fresh, (DWORD)(uintptr_t)live);
    for (DWORD i = 0; !unify && c->oldLive.used && i < want.cap; i++) unify = want.k[i] > 1 && RpTabFind(c->oldLive, want.k[i]);
    if (unify) RpUnify(c, want);
    RpTabFree(want);
    RpOldGc(c);
    return fresh;
}
// own-block children (RpFpCopyChild) whose parent is old: re-pointed to fresh, the pin moved with them
static void RpRepin(RpClone* c, u8* old, u8* fresh) {
    for (DWORD i = 0; i < c->fps.cap; i++) {
        u8* ch = c->fps.k[i] > 1 ? (u8*)(uintptr_t)c->fps.v[i] : NULL;
        if (!ch || ((RpFpHdr*)(ch - 16))->pad != 1 || *(u8**)(ch + 0xC) != old) continue;
        (*(LONG*)(old + 8))--, (*(LONG*)(fresh + 8))++;
        *(u8**)(ch + 0xC) = fresh;
    }
}
static void RpGc(RpClone* c) {
    for (DWORD i = 0; i < c->fps.cap; i++)
        if (c->fps.k[i] > 1 && RpFpRefs((u8*)(uintptr_t)c->fps.v[i]) <= 0) {
            RpKids(c->kids, (u8*)(uintptr_t)c->fps.v[i], false);
            RpFpFree((u8*)(uintptr_t)c->fps.v[i]);
            c->fps.k[i] = 1;
            ts3native_counters.rpGcFreed++;
        }
    RpOldGc(c);
}
// re-chain the clone's map in the live order of the batch (bucket count, chains, policy); false = a node had no partner
struct RpOrd { DWORD t[5]; u8* node; int next; bool used; };
// a replaced block and its children in the clone -> live table (their FMEs still name the live objects)
static void RpRevOld(RpTab& rev, const u8* blk, const u8* live) {
    RpTabPut(rev, (DWORD)(uintptr_t)blk, (DWORD)(uintptr_t)live);
    if (!RpHasList(blk)) return;
    for (const u8* l = RpP(blk + 0x24); l != blk + 0x24; l = RpP(l)) RpRevOld(rev, RpP(l + 8), RpP(l + 12));
}
static LONG g_rpResyncLog;  // first 8 resync failures logged (0.32.1)
static bool RpMapResync(RpClone* c, const RpBatch* b) {
    u8* m = c->m;
    const DWORD nb = RpD(m + 8), cnt = RpD(m + 0xC);
    const bool same = cnt == b->nOrder;  // else matched dry, to name the first node without a partner (0.32.1)
    if (b->mapN <= 1 || (!same && g_rpResyncLog >= 8)) {
        if (!(same && cnt == 0) && InterlockedIncrement(&g_rpResyncLog) <= 8)
            Log("snap replay: map resync failed: clone %lu entries, live %lu at the cut (buckets %lu)", cnt, b->nOrder, b->mapN);
        return same && cnt == 0;
    }
    RpTab rev = {}, lq = {}, heads = {};
    for (DWORD i = 0; i < c->fps.cap; i++) if (c->fps.k[i] > 1) RpTabPut(rev, c->fps.v[i], c->fps.k[i]);
    for (DWORD i = 0; i < c->kids.cap; i++) if (c->kids.k[i] > 1) RpTabPut(rev, c->kids.v[i], c->kids.k[i]);
    for (DWORD i = 0; i < c->old.cap; i++) if (c->old.k[i] > 1) RpRevOld(rev, (const u8*)(uintptr_t)c->old.k[i], ((const RpFpHdr*)(uintptr_t)(c->old.k[i] - 16))->live);
    for (int i = 0; i < c->nLots; i++) for (int j = 0; j < c->lots[i].nQ; j++) if (c->lots[i].q[j]) RpTabPut(lq, (DWORD)(uintptr_t)c->lots[i].q[j], c->lots[i].liveQ[j]);
    RpOrd* o = (RpOrd*)RpAlloc((cnt + 1) * sizeof(RpOrd));
    bool ok = o != NULL;
    DWORD n = 0;
    DWORD* bk = (DWORD*)RpP(m + 4);
    for (DWORD i = 0; ok && i < nb; i++)
        for (u8* nd = (u8*)(uintptr_t)bk[i]; nd; nd = RpP(nd + 0x10)) {
            if (n >= cnt) { ok = false; break; }
            const u8* f = RpP(nd + 8);
            const DWORD* q = f ? RpTabFind(lq, RpD(f + 0xC)) : NULL;
            const DWORD* lf = f ? RpTabFind(rev, RpD(f + 8)) : NULL;
            RpOrd& r = o[n];
            r.t[0] = RpD(nd), r.t[1] = RpD(nd + 4), r.t[2] = q ? *q : 0, r.t[3] = f ? RpD(f + 0x10) : 0, r.t[4] = lf ? *lf : 0;
            r.node = nd, r.used = false;
            const DWORD h = (r.t[0] * 31 + r.t[2]) * 31 + r.t[3] * 7 + r.t[4] | 2;
            DWORD* hd = RpTabFind(heads, h);
            r.next = hd ? (int)*hd : -1;
            if (!RpTabPut(heads, h, n)) ok = false;
            n++;
        }
    ok = ok && n == cnt;
    DWORD* nbk = ok && same ? (DWORD*)g_rpMapAlloc(m + 0x1C, NULL, b->mapN * 4 + 4, 0x400000) : NULL;
    if (ok && same && !nbk) ok = false;
    if (ok) {
        if (nbk) memset(nbk, 0, b->mapN * 4), nbk[b->mapN] = 0xFFFFFFFF;
        DWORD tailBucket = 0xFFFFFFFF;
        u8* tail = NULL;
        for (DWORD i = 0; ok && i < b->nOrder; i++) {
            const DWORD* t = b->order + i * 5;
            const DWORD h = (t[0] * 31 + t[2]) * 31 + t[3] * 7 + t[4] | 2;
            const DWORD* hd = RpTabFind(heads, h);
            int k = hd ? (int)*hd : -1;
            while (k >= 0 && (o[k].used || memcmp(o[k].t, t, 20))) k = o[k].next;
            if (k < 0) {
                ok = false;
                if (InterlockedIncrement(&g_rpResyncLog) <= 8) {
                    DWORD vt = 0;  // the live footprint (read under the consumer's +0x918 hold)
                    __try { vt = t[4] ? RpD((const u8*)(uintptr_t)t[4]) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    const RpCls* lc = RpClass(vt);
                    const DWORD* cf = RpTabFind(c->fps, t[4]);
                    const DWORD* ck = RpTabFind(c->kids, t[4]);
                    Log("snap replay: map resync failed (clone %lu entries): live node %lu of %lu has no clone partner (key 0x%08lX%08lX, Q 0x%08lX, +0x10 0x%08lX, footprint 0x%08lX class 0x%08lX%s; clone copy: %s%s%s)",
                        cnt, i, b->nOrder, t[1], t[0], t[2], t[3], t[4], vt, lc && lc->child ? " child" : "",
                        cf ? (((const RpFpHdr*)(uintptr_t)(*cf - 16))->pad == 1 ? "own block" : "top level") : "", ck ? " in a parent's block" : "",
                        RpTabFind(c->oldLive, t[4]) ? " retired" : (!cf && !ck ? "none" : ""));
                }
                break;
            }
            o[k].used = true;
            if (!same) continue;
            u8* nd = o[k].node;
            const DWORD bi = t[0] % b->mapN;
            *(DWORD*)(nd + 0x10) = 0;
            if (bi == tailBucket) *(DWORD*)(tail + 0x10) = (DWORD)(uintptr_t)nd; else nbk[bi] = (DWORD)(uintptr_t)nd;
            tailBucket = bi, tail = nd;
        }
        if (ok && !same) {  // every live node matched: the clone has extra ones
            ok = false;
            for (DWORD j = 0; j < n; j++)
                if (!o[j].used) {
                    if (InterlockedIncrement(&g_rpResyncLog) <= 8)
                        Log("snap replay: map resync failed: clone %lu entries, live %lu; clone node without a live partner (key 0x%08lX%08lX, Q 0x%08lX, +0x10 0x%08lX, footprint 0x%08lX)",
                            cnt, b->nOrder, o[j].t[1], o[j].t[0], o[j].t[2], o[j].t[3], o[j].t[4]);
                    break;
                }
        } else if (ok) {
            if (nb > 1) g_rpMapFree(m + 0x1C, NULL, bk, nb * 4 + 4);
            *(DWORD**)(m + 4) = nbk, *(DWORD*)(m + 8) = b->mapN;
            memcpy(m + 0x10, b->policy, 12);
        } else if (same) {  // put the old chains back: they were not touched except +0x10 of nodes already moved, so rebuild them from o
            memset(bk, 0, nb * 4);
            for (DWORD i = n; i-- > 0;) { u8* nd = o[i].node; const DWORD bi = RpD(nd) % nb; *(DWORD*)(nd + 0x10) = bk[bi]; bk[bi] = (DWORD)(uintptr_t)nd; }
            g_rpMapFree(m + 0x1C, NULL, nbk, b->mapN * 4 + 4);
        }
    }
    RpFree(o);
    RpTabFree(rev), RpTabFree(lq), RpTabFree(heads);
    return ok;
}
static void RpApplyBatch(RpClone* c, RpBatch* b) {
    // every clone node on the rebased lots' old Qs goes; the batch brings the lots' nodes at the cut. 0.32.1 kept nodes on a
    // replaced Q (dummy Q): on BP the game had dropped them, so the clone held 6 more entries than live at each SYNC (0.32.4)
    RpTab qs = {};
    for (int i = 0; i < b->n; i++)
        if (RpLot* old = RpFindLot(c, b->lots[i].key))
            for (int j = 0; j < old->nQ; j++) if (old->q[j]) RpTabPut(qs, (DWORD)(uintptr_t)old->q[j], 1);
    RpMapDrop(c, qs);
    RpTabFree(qs);
    for (int i = 0; i < b->n; i++) {
        if (RpLot* old = RpFindLot(c, b->lots[i].key)) {
            RpTableUnlink(c, old);
            RpLotFree(old);
            *old = c->lots[--c->nLots];
        }
    }
    for (int i = 0; i < b->n; i++) {
        if (b->lots[i].removed) continue;
        RpLot* l = RpLotAdd(c);
        if (!l) { RP_NEED_EPOCH(); continue; }
        *l = b->lots[i].lot;
        b->lots[i].removed = true;  // taken
        RpTableLink(c, l);
    }
    // lot table chains in the live order (a rebased lot was linked at its bucket head; the live table may hold it later):
    // head inserts in reverse live order. shortcut: a live bucket count change is not followed (lotN stays the build's)
    if (b->lotKeys && c->lotN) {
        memset(c->lotBk, 0, c->lotN * 4);
        for (int i = 0; i < c->nLots; i++) {
            int j = 0;
            while (j < b->nLotKeys && b->lotKeys[j] != c->lots[i].key) j++;
            if (j == b->nLotKeys) RpTableLink(c, &c->lots[i]);
        }
        for (int j = b->nLotKeys - 1; j >= 0; j--) if (RpLot* l = RpFindLot(c, b->lotKeys[j])) RpTableLink(c, l);
    }
    for (int i = 0; i < b->nMap; i++) {
        u8* f = (u8*)(uintptr_t)b->map[i * 3 + 2];
        (*(LONG*)(f + 4))--;  // the batch's hold, the map node takes its own
        if (!RpMapAdd(c, b->map[i * 3], b->map[i * 3 + 1], f)) RP_NEED_EPOCH();
        b->map[i * 3 + 2] = 0;
    }
    b->nMap = 0;
    // children in a block of their own (RpFpCopyChild) whose parent the batch replaces: re-pointed to the fresh parent and the
    // pin moved with them, so the old parent can go below
    for (DWORD i = 0; i < c->fps.cap; i++) {
        u8* ch = c->fps.k[i] > 1 ? (u8*)(uintptr_t)c->fps.v[i] : NULL;
        u8* par = ch && ((RpFpHdr*)(ch - 16))->pad == 1 ? *(u8**)(ch + 0xC) : NULL;
        const DWORD* nv = par ? RpTabFind(b->fps, (DWORD)(uintptr_t)((RpFpHdr*)(par - 16))->live) : NULL;
        if (!nv || (u8*)(uintptr_t)*nv == par) continue;
        (*(LONG*)(par + 8))--, (*(LONG*)((u8*)(uintptr_t)*nv + 8))++;
        *(u8**)(ch + 0xC) = (u8*)(uintptr_t)*nv;
    }
    // footprints: the batch's copies (live at the cut) replace the clone's; FMEs of other lots move over to them
    RpTab want = {};
    for (DWORD i = 0; i < b->fps.cap; i++) {
        if (b->fps.k[i] <= 1) continue;
        const DWORD live = b->fps.k[i];
        u8* fresh = (u8*)(uintptr_t)b->fps.v[i];
        b->fps.k[i] = 1;
        DWORD* v = RpTabFind(c->fps, live);
        if (u8* old = v ? (u8*)(uintptr_t)*v : NULL) RpRetire(c, old, fresh);
        if (!RpTabPut(c->fps, live, (DWORD)(uintptr_t)fresh)) { RP_NEED_EPOCH(); continue; }
        RpKids(c->kids, fresh, true);
        RpWant(want, fresh, live);
    }
    if (want.used) RpUnify(c, want);  // FMEs of other lots move to the batch's copies (children matched by their live object)
    RpTabFree(want);
    RpOldGc(c);
    if (!RpMapResync(c, b)) { RP_NEED_EPOCH(); ts3native_counters.rpDiverge++; }  // the SYNC after it then reports it too
}
static void RpAudit(RpClone* c, const RpAuditRec* a) {
    Ts3nCounters& k = ts3native_counters;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    int seen = 0;
    for (int i = 0; i < a->n; i++) {
        const RpLot* l = RpFindLot(c, a->keys[i]);
        DWORD ents = 0;
        DWORD perQ[16];
        const u64 h = l ? RpLotHash(l->node, &ents, perQ) : 0;
        if (l) seen++;
        k.rpAudits++;
        if (l && h == a->hash[i]) continue;
        k.rpMis++;
        {
            int qd = -1;  // first Q whose tree entry count differs (0.32.8)
            for (int j = 0; l && a->perQ && j < 16 && qd < 0; j++) if (perQ[j] != a->perQ[i * 16 + j]) qd = j;
            if (InterlockedIncrement(&g_rpLogged) <= (LONG)g_rpLogMis && l && qd >= 0)
                Log("snap replay audit MISMATCH lot 0x%08lX%08lX: Q %d (level index) entries live %lu clone %lu", (DWORD)(a->keys[i] >> 32), (DWORD)a->keys[i],
                    qd, a->perQ[i * 16 + qd], perQ[qd]);
        }
        if (InterlockedIncrement(&g_rpLogged) <= (LONG)g_rpLogMis)
            Log("snap replay audit MISMATCH lot 0x%08lX%08lX: %s, entries live %lu clone %lu (clone %d lots, map %lu / live %lu)",
                (DWORD)(a->keys[i] >> 32), (DWORD)a->keys[i], l ? "hash differs" : "not in the clone", a->ents[i], ents, c->nLots,
                RpD(c->m + 0xC), a->mapCount);
    }
    if (a->nAll != c->nLots || seen != a->n) {
        k.rpAudits++, k.rpMis++;
        if (InterlockedIncrement(&g_rpLogged) <= (LONG)g_rpLogMis) Log("snap replay audit MISMATCH: clone has %d lots, live %d", c->nLots, a->n);
    }
    k.rpAuditWorkerUs += UsSince(t0);
}
static void RpSync(RpClone* c, const RpSyncRec* s) {
    Ts3nCounters& k = ts3native_counters;
    k.rpSyncs++;
    if (RpD(c->m + 8) == s->mapN && RpD(c->m + 0xC) == s->mapCount && (DWORD)c->nLots == s->lots) return;
    k.rpDiverge++;
    RP_NEED_EPOCH();
    if (InterlockedIncrement(&g_rpLogged) <= (LONG)g_rpLogMis)
        Log("snap replay: clone diverged: map buckets %lu / live %lu, entries %lu / %lu, lots %d / %lu (rebuilt at the next cut)",
            RpD(c->m + 8), s->mapN, RpD(c->m + 0xC), s->mapCount, c->nLots, s->lots);
}
// consumer: the clone's Q-less nodes of the record's key (or all) replaced by the record's, in its order
static void RpNoQApply(RpClone* c, RpNoQRec* r) {
    u8* m = c->m;
    const DWORD nb = RpD(m + 8);
    DWORD* bk = (DWORD*)RpP(m + 4);
    if (nb <= 1) { if (r->n) RP_NEED_EPOCH(); return; }
    const DWORD b0 = r->all ? 0 : (DWORD)r->key % nb, b1 = r->all ? nb : b0 + 1;
    for (DWORD i = b0; i < b1; i++)
        for (DWORD* at = &bk[i]; *at;) {
            u8* nd = (u8*)(uintptr_t)*at;
            const u8* f = RpP(nd + 8);
            if (f && !RpD(f + 0xC) && (r->all || (RpD(nd) == (DWORD)r->key && RpD(nd + 4) == (DWORD)(r->key >> 32)))) {
                *at = RpD(nd + 0x10);
                g_rpMapNodeFree(m, NULL, nd);
                (*(DWORD*)(m + 0xC))--;
            } else at = (DWORD*)(nd + 0x10);
        }
    for (int i = 0; i < r->n; i++) {
        RpNoQEnt& e = r->e[i];
        u8* fp = e.liveFp ? RpFpUse(c, e.liveFp, e.copy) : NULL;
        e.copy = NULL;  // RpFpUse took it (kept or freed)
        if (e.liveFp && !fp) { RP_NEED_EPOCH(); continue; }  // no copy of its footprint: rebuild
        u8* f = (u8*)g_rpFmePool(0x14, 0xFEAC80, 0, 0, 0, 0);
        if (!f) { RP_NEED_EPOCH(); continue; }
        memcpy(f, e.fme, 0x14);
        *(LONG*)(f + 4) = 0;
        *(u8**)(f + 8) = fp;
        if (fp) (*(LONG*)(fp + 8))++;
        *(u8**)(f + 0xC) = NULL;
        // shortcut: RpMapAdd puts it after the run of its key; the live insert 0x628220 may order equal keys otherwise (the
        // count, which the SYNC checks, is exact; order only matters to a search walking equal keys)
        if (!RpMapAdd(c, e.lo, e.hi, f)) { ((RpDtorFn)(uintptr_t)RpD(RpP(f)))(f, NULL, 1); RP_NEED_EPOCH(); }
    }
}
// one record; true = keep going in this hold, false = release the lock first (audit / epoch)
static bool RpApply(const RpRec& r) {
    Ts3nCounters& k = ts3native_counters;
    RpClone* c = g_rpClone;
    switch (r.type) {
        case RP_NOQ:
            if (c) RpNoQApply(c, (RpNoQRec*)r.p);
            RpNoQFree((RpNoQRec*)r.p);
            break;
        case RP_EPOCH:
            RpFreeClone(g_rpClone);
            g_rpClone = (RpClone*)r.p;
            k.rpLots = g_rpClone->nLots;
            k.rpReady |= 2;
            break;
        case RP_INS: case RP_REM: {
            u8* q = NULL;
            RpLot* l = c ? RpFindQ(c, r.a, &q) : NULL;
            const int skip = !c ? RPS_NOCLONE : !l ? RPS_NOQ : -1;
            if (skip >= 0) {
                k.rpSkip[skip]++;
                if (skip == RPS_NOQ && k.rpSkip[RPS_NOQ] <= 8) {  // which Q (0.30.2 diagnostics; read under the consumer's +0x918 hold)
                    const u64 key = RpQKey(r.a);
                    Log("snap replay: %s on a Q the clone does not have: Q 0x%08lX, lot 0x%08lX%08lX (clone has that lot: %s)",
                        r.type == RP_INS ? "insert" : "remove", r.a, (DWORD)(key >> 32), (DWORD)key, RpFindLot(c, key) ? "yes" : "no");
                }
                if (r.type == RP_INS) RpFpFree((u8*)r.p);
                return true;
            }
            if (l->stale) k.rpSkip[RPS_STALE]++;  // applied anyway: its footprints are shared with other lots (the rebase replaces the lot)
            if (r.type == RP_INS) {
                u8* fp = RpFpUse(c, (u8*)(uintptr_t)r.b, (u8*)r.p);
                if (!fp) {  // the writer's copy failed (counted in rpCopyFail): this lot is rebased at the next cut instead
                    k.rpSkip[RPS_NOCOPY]++;
                    l->stale = true;
                    while (InterlockedExchange(&g_rpRingLock, 1)) YieldProcessor();
                    int i = 0;
                    while (i < g_rpNForce && g_rpForce[i] != l->key) i++;
                    if (i == g_rpNForce) { if (g_rpNForce < 64) g_rpForce[g_rpNForce++] = l->key; else g_rpForceAll = 1; }
                    InterlockedExchange(&g_rpRingLock, 0);
                    return true;
                }
                g_rpIns(q, NULL, fp, r.c);
            } else g_rpRem(q, NULL, r.b, r.c);
            break;
        }
        case RP_STALE:
            if (c) if (RpLot* l = RpFindLot(c, ((u64)r.b << 32) | r.a)) l->stale = true;
            break;
        case RP_LOTS:
            if (c) RpApplyBatch(c, (RpBatch*)r.p);
            RpBatchFree((RpBatch*)r.p, true);
            if (c) k.rpLots = c->nLots;
            break;
        case RP_SYNC:
            if (c) RpSync(c, (const RpSyncRec*)r.p);
            RpFree(r.p);
            break;
        case RP_AUDIT: return false;  // compared outside the lock (the clone is ours)
        default: break;
    }
    k.rpApplied[r.type]++;
    return true;
}
// apply up to the published position; lock = take +0x918 (the worker); the tests call it directly
// P4 (p4clone.cpp): the consumer stops at g_rpHoldAt (the armed job's cut) until the clone run is done or the hold expires,
// and never drains while a clone run uses the clone (g_rpUse, exclusive)
static volatile DWORD g_rpHoldAt;
static volatile ULONGLONG g_rpHoldUntil;
static SRWLOCK g_rpUse = SRWLOCK_INIT;
static DWORD RpLimit() {
    const DWORD pub = g_rpPub, h = g_rpHoldAt;
    if (h && GetTickCount64() < g_rpHoldUntil && (DWORD)(h - g_rpTail) <= (DWORD)(pub - g_rpTail)) return h;
    return pub;
}
static void RpDrainBody(bool lock);
static void RpDrain(bool lock) {  // exclusive with a P4 clone run (g_rpUse)
    AcquireSRWLockExclusive(&g_rpUse);
    // the replay's own ApplyInsert / ApplyRemove on clone Qs enter P1's W1/W2 stubs too: not records (0.32.1; before, each
    // applied record came back as one on an unknown Q: BP 34,086 skips, a refresh batch at nearly every cut, resync failures)
    t_rpReplay++;
    RpDrainBody(lock);
    t_rpReplay--;
    ReleaseSRWLockExclusive(&g_rpUse);
}
static void RpDrainBody(bool lock) {
    Ts3nCounters& k = ts3native_counters;
    static const DWORD kInf = 0xFFFFFFFF;
    while (g_rpTail != RpLimit()) {
        const u8* world = *g_spWorldSlot;
        LARGE_INTEGER tw, t0;
        QueryPerformanceCounter(&tw);
        void* lk = world ? (void*)(world + 0x918) : NULL;
        if (lock && (!lk || g_spAcq(lk, NULL, &kInf) <= 0)) return;
        const LONG op = RpOpEnter();  // inside the hold: this thread's tree pool pops are private (pools.cpp)
        k.rpLockWaitUs += UsSince(tw);
        QueryPerformanceCounter(&t0);
        const RpRec* audit = NULL;
        __try {
            if (g_rpClone && world && PlMoved(g_rpClone->seen, RpP(world + 0x95C))) {  // the game's pools were recreated (§16.6)
                RpFreeClone(g_rpClone);  // private memory only: safe after PathPlanner_Reset; records skip until the next EPOCH
                g_rpClone = NULL;        // (the sim's next cut sees the change itself and builds that EPOCH)
                k.rpReady &= ~2;
            }
            while (g_rpTail != RpLimit()) {
                const RpRec& r = g_rpRing[g_rpTail & (kRpRing - 1)];
                if (!RpApply(r)) { audit = &r; break; }
                g_rpTail++;
                if (UsSince(t0) > 1000) break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            k.rpFaults++;
            RP_NEED_EPOCH();
            Log("snap replay: fault applying record %lu: clone dropped, rebuilt at the next cut", g_rpTail);
            g_rpTail++;  // shortcut: that record's payload and the clone may hold half-applied game state: dropped, not freed
            g_rpClone = NULL;
            k.rpReady &= ~2;
        }
        const LONG us = UsSince(t0);
        k.rpApplyUs += us;
        Max(&k.rpApplyMaxUs, us);
        RpOpLeave(op);
        if (lock) g_spRel(lk, NULL);
        if (audit) {
            if (g_rpClone) RpAudit(g_rpClone, (const RpAuditRec*)audit->p);
            RpAuditFree((RpAuditRec*)audit->p);
            k.rpApplied[RP_AUDIT]++;
            g_rpTail++;
        }
        if (g_rpClone) RpGc(g_rpClone);  // our heap only: no lock
        Max(&k.rpLag, (LONG)(g_rpHead - g_rpTail));
        if (!g_rpOn) break;
    }
    if (!g_rpOn && g_rpClone) {  // switched off: drop the clone under the lock
        const u8* world = *g_spWorldSlot;
        if (!lock || (world && g_spAcq((void*)(world + 0x918), NULL, &kInf) > 0)) {
            const LONG op = RpOpEnter();
            RpFreeClone(g_rpClone);
            RpOpLeave(op);
            g_rpClone = NULL;
            k.rpReady &= ~2;
            if (lock) g_spRel((void*)(world + 0x918), NULL);
        }
    }
}
static volatile LONG g_rpStop;  // tests only
static DWORD WINAPI RpThread(void*) {
    while (!g_rpStop) {
        WaitForSingleObject(g_rpEvent, 1000);
        RpDrain(true);
    }
    return 0;
}

// SpLotStrategy "replay": begin = the cut; no per-version lot images (the replay audit compares hashes itself)
static_assert(offsetof(Ts3nCounters, rpReady) == 0x2768 && offsetof(Ts3nCounters, rpMis) == 0x281C && offsetof(Ts3nCounters, snP4Ready) == 0x2968,
              "counters v49 layout (docs/registry.md)");
static void RpBegin(SpVer*, const SpVer*, const u8* world, const SpDirty&, const u64*, int) { RpCut(world); }
static const SpLotStrategy kSpLotReplay = {"replay", NULL, RpBegin, NULL, RpPre};

// [RouteSnap] Lots=replay (from ApplySnapImages)
static bool ApplyReplay(const wchar_t* ini) {
    Ts3nCounters& k = ts3native_counters;
    wchar_t lots[32];
    GetPrivateProfileStringW(L"RouteSnap", L"Lots", L"none", lots, 32, ini);
    if (_wcsicmp(lots, L"replay")) { Log("route snapshot lots: none (Lots=%ls)", lots); return false; }
    g_rpAuditEvery = (DWORD)Clamp(GetPrivateProfileIntW(L"RouteSnap", L"ReplayAuditEvery", 4, ini), 0, 100000);
    g_rpAuditLots = (DWORD)Clamp(GetPrivateProfileIntW(L"RouteSnap", L"ReplayAuditLots", 2, ini), 0, 1024);
    g_rpCapBytes = Clamp(GetPrivateProfileIntW(L"RouteSnap", L"ReplayCapMB", 128, ini), 16, 512) << 20;
    g_rpLogMis = g_spLogMis;
    g_rpPrebuild = GetPrivateProfileIntW(L"RouteSnap", L"Prebuild", 0, ini) != 0;
    g_rpRing = (RpRec*)VirtualAlloc(NULL, kRpRing * sizeof(RpRec), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_rpEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_rpRing || !g_rpEvent) { Log("SKIP route snapshot replay: no ring"); return false; }
    // private tree and map pools (docs/94 §16.6-§16.7): without them the clone would share unlocked free lists with the game
    if (!PlInstall()) { Log("SKIP route snapshot replay: no private pools"); return false; }
    g_rpHeap = g_rpHeap ? g_rpHeap : HeapCreate(0, 0, 0);
    g_rpThread = CreateThread(NULL, 1 << 20, RpThread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!g_rpThread) { Log("SKIP route snapshot replay: no thread"); return false; }
    g_spLot = &kSpLotReplay;
    g_rpOn = 1;
    k.rpReady = 1 | 4;
    Log("OK   route snapshot lots: replay (docs/94 §11 B, audit only): ring %lu records, ReplayAuditEvery=%lu, ReplayAuditLots=%lu, ReplayCapMB=%ld, Prebuild=%d",
        kRpRing, g_rpAuditEvery, g_rpAuditLots, g_rpCapBytes >> 20, g_rpPrebuild ? 1 : 0);
    return true;
}
