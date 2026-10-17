// H6 design S2 (docs/84 §3.4, implementation §3.6): a read-only Locomotion_ObstacleCheck while an async route job runs, with
// the docs/83 worker writer detours W1-W6 under the reader/writer lock QL. Included by ts3native.cpp after discourage.cpp.
// Installed with [SteerRO] Enabled=1 (default 0; needs [RouteGate] Enforce=1 and the async path). Runtime switches (counters
// v31): steerMode (+0x10A4) 1 = while a job runs, answer from the read-only clone; steerShadow (+0x10A8) 1 = with no job
// running, run the game's call, then the clone, and count differences.
//
// Locomotion_ObstacleCheck 0x85FD10 (thiscall agent: ids, ign1, n1, ign2, n2; ret 0x14; its caller 0x860129 ignores eax)
// queries 0x82EDB0 twice for the first flagged path node and calls RouteFollow_EditPath 0x85F5A0 (-> 0x82F0B0) per flagged
// node; both queries take +0x918, so while a job runs the sim thread waits for it (P0: 22% of its waits). The clone answers
// the same ids from the same structures without +0x918 and without any write outside our stack and the caller's id vector:
//   phase 1 (no lock): 0x85AE60 per flagged node and the World_LotLookup calls (both already run concurrently with jobs);
//   phase 2 (QL shared, try only): lot table find (0x627E00, no insert), cell graph, lot transform (SSE copy of 0x62ED40 /
//   0x62EF30), the game's point / segment walks 0x663E20 / 0x663B20 with our visitor SrVisit (0x669CD0 rules, no refcounts,
//   no set), the EditPath consumer with the game's footprint slots, 0x85AAA0 and the level-0 test 0x80E690 (cache checked);
//   phase 3: the ids are appended with IdVec_PushBack 0x572400.
// Any doubt (QL busy, dirty lot index, lot not in the table, deferred quadtree flag, invalid polygon cache, unknown slot target,
// overflow, the pin counter Q+0x48 or the flag Q+0x4C changing during a walk, a sim write while the clone runs) gives up and
// calls the original (today's behaviour: it waits for the job).
// Worker side: W1-W6 (12 detours) and the gate stub of LotGraph_Build 0x63CB90 (routegate.cpp site 3) hold QL exclusive on
// the worker thread for the whole call; the frame stack and leave thunk of routegate.cpp release it (RG_REL_QL).
// Fix C-A ([FpRebuildLock], docs/84 §4.4) is the sim-thread branch of the W6a stub (CaEnter below); it installs that stub
// alone when [SteerRO] is off.

static const DWORD STEER_LOCO = 0x0085FD10;
static const u8 kLocoPro[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-16 (no relative operand)
typedef void(__fastcall* LocoFn)(u8* agent, void* edx, u8* ids, const void* ign1, DWORD n1, const void* ign2, DWORD n2);
static LocoFn g_locoOrig;

// Game functions the clone calls (conventions read in the disassembly 2026-10-17; tests point them elsewhere)
typedef u8*(__cdecl* SrWorldFn)();                                                         // RoutingWorld_Get 0x8292D0
typedef void(__cdecl* SrConvFn)(const void* nodePt, void* out);                             // 0x85AE60: movaps both
typedef u8*(__fastcall* SrLotLookupFn)(void* wm, void* edx, float x, float z);              // World_LotLookup 0xC68990, ret 8
typedef void(__fastcall* SrLotFindFn)(const void* tbl, void* edx, DWORD* out, const DWORD* key);  // LotTable_Find 0x627E00, ret 8
typedef const u8*(__cdecl* SrActiveLotFn)();                                                // 0xC88FD0
typedef void(__fastcall* SrWalkPtFn)(const void* q, void* edx, void* ctx, float x, float y, float r, void* visit, void* list);  // 0x663E20, ret 0x18
typedef void(__fastcall* SrWalkSegFn)(const void* q, void* edx, void* ctx, float sx, float sy, float gx, float gy, float w,
                                      void* visit, DWORD cb, void* list);                   // 0x663B20, ret 0x24
typedef u8(__cdecl* SrIdListFn)(DWORD lo, DWORD hi, const void* list, DWORD n);              // IdList_Contains 0x85AAA0
typedef void(__fastcall* SrIdPushFn)(void* vec, void* edx, const DWORD* id);                // IdVec_PushBack 0x572400, ret 4
typedef u8(__fastcall* SrQueryFn)(u8* world, void* edx, const void* pt, float r, void* vec, DWORD mode, DWORD opt);  // 0x82EDB0, ret 0x14
typedef void(__fastcall* SrVecRelFn)(void* vec, void* edx, void* b, void* e);               // FootprintRefVec_ReleaseRange 0xAAC090, ret 8
typedef void(__cdecl* SrFreeFn)(void* p);                                                   // EA_Free 0x4E3F70
static SrWorldFn g_srWorldGet = (SrWorldFn)(uintptr_t)0x008292D0;
static SrConvFn g_srConv = (SrConvFn)(uintptr_t)0x0085AE60;
static SrLotLookupFn g_srLotLookup = (SrLotLookupFn)(uintptr_t)0x00C68990;
static SrLotFindFn g_srLotFind = (SrLotFindFn)(uintptr_t)0x00627E00;
static SrActiveLotFn g_srActiveLot = (SrActiveLotFn)(uintptr_t)0x00C88FD0;
static SrWalkPtFn g_srWalkPt = (SrWalkPtFn)(uintptr_t)0x00663E20;
static SrWalkSegFn g_srWalkSeg = (SrWalkSegFn)(uintptr_t)0x00663B20;
static SrIdListFn g_srIdList = (SrIdListFn)(uintptr_t)0x0085AAA0;
static SrIdPushFn g_srIdPush = (SrIdPushFn)(uintptr_t)0x00572400;
static SrQueryFn g_srQuery = (SrQueryFn)(uintptr_t)0x0082EDB0;
static SrVecRelFn g_srVecRel = (SrVecRelFn)(uintptr_t)0x00AAC090;
static SrFreeFn g_srFree = (SrFreeFn)(uintptr_t)0x004E3F70;
static u8* const* g_srWmSlot = (u8* const*)(uintptr_t)0x011ECBC4;     // the world manager Locomotion passes to EditPath
static const float* g_srOne = (const float*)(uintptr_t)0x0107A538;    // 1.0f, the w of every transformed point
static const DWORD* g_srMaskTab = (const DWORD*)(uintptr_t)0x00FEDE48;  // CollectMasked: iterator byte -> mask bit
static const DWORD kSrVecTag = 0x01026E1C;                            // 4th word of Locomotion's footprint vector
static bool g_srCheckSlots = true;  // selftest fakes turn the slot whitelist off

// Footprint vtable slots the clone calls (thiscall); every target was read (docs/84 §3.6) and is checked per object
typedef u8(__fastcall* SlotBoolArgFn)(const u8* o, void* edx, DWORD a);                     // +0x60
typedef u8(__fastcall* SlotBoolFn)(const u8* o, void* edx);                                 // +0x28, +0xC0
typedef int(__fastcall* SlotClassFn)(const u8* o, void* edx, const void* rec, DWORD b);     // +0x8, ret 8
typedef int(__fastcall* SlotIntFn)(const u8* o, void* edx);                                 // +0xC, sub +8
typedef unsigned __int64(__fastcall* SlotIdFn)(const u8* o, void* edx);                     // +0x84 (-> +0x7C), sub +4
typedef u8(__fastcall* SlotSegFn)(const u8* o, void* edx, DWORD level, const float* a, const float* b);  // +0xE8, ret 0xC

static SRWLOCK g_ql = SRWLOCK_INIT;   // worker exclusive in W1-W6, sim shared (try only) in the clone
static volatile LONG g_qlSeq;         // exclusive holds; T2 checks it does not move under a shared hold
static volatile LONG g_steerBroken;   // sticky: a worker W frame could not be tracked, so QL did not cover that call
static __declspec(thread) LONG t_qlDepth;
static __declspec(thread) int t_srFail;   // set by QlEnter when the sim thread writes while the clone runs (t_inRo)
static __declspec(thread) bool t_inRo;

enum { SR_OK, SR_FB_STATE, SR_FB_TRY, SR_FB_DIRTY, SR_FB_LOTMISS, SR_FB_DEFERRED, SR_FB_CACHE, SR_FB_SHAPE, SR_FB_TRIP };
static const char* const kSrFbName[] = {"", "state", "QL busy", "dirty lot index", "lot not in table", "deferred quadtree flag",
                                        "polygon cache", "unknown slot/overflow/null", "tripwire"};
static const int kSrCap = 512, kSrLocal = 64, kSrNodes = 16, kSrIds = 256;

static void QlAcquireEx() {
    if (t_qlDepth++) return;
    if (!TryAcquireSRWLockExclusive(&g_ql)) {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        AcquireSRWLockExclusive(&g_ql);
        InterlockedExchangeAdd(&ts3native_counters.steerQlWaitUs, UsSince(t0));
    }
    InterlockedIncrement(&g_qlSeq);
    InterlockedIncrement(&ts3native_counters.steerQlHolds);
}
static void QlReleaseEx() {
    if (--t_qlDepth == 0) ReleaseSRWLockExclusive(&g_ql);
}

// W4 (routegate.cpp RgEnter, gate site 3) and the W stubs: the worker takes QL for the call; false = no frame to release it
// at the leave, so it is released now and the clone is switched off for good (the call runs outside QL)
static bool QlPushWorker(RgThread& t, DWORD* slot, LONG reader) {
    QlAcquireEx();
    if (RgPush(t, slot, reader, RG_REL_QL)) return true;
    InterlockedExchange(&g_steerBroken, 1);
    InterlockedIncrement(&ts3native_counters.steerUntracked);
    QlReleaseEx();
    return false;
}

// W1-W6 (docs/83 §6): all bytes read from the exe (2026-10-17), whole instructions, no relative operand
struct QlSite { DWORD addr; u8 len; u8 pro[8]; u8 cache; const char* name; };
static const int QL_N = 12;
static const QlSite kQlSites[QL_N] = {
    {0x00665C50, 6, {0x81, 0xEC, 0x80, 0x00, 0x00, 0x00}, 0, "LotQuadTree_ApplyInsert (W1)"},          // ret 8
    {0x00666750, 5, {0x83, 0xEC, 0x60, 0x53, 0x55}, 0, "LotQuadTree_ApplyRemove (W2)"},                // ret 8
    {0x0062F8A0, 7, {0x8B, 0x44, 0x24, 0x08, 0x83, 0xEC, 0x14}, 0, "LotRouting_QuadTreeBuild (W3)"},   // ret 0x10
    {0x0062E680, 8, {0x83, 0xEC, 0x08, 0x53, 0x8B, 0x5C, 0x24, 0x14}, 0, "LotTable_Insert (W5)"},      // ret 0xC
    {0x0080E5B0, 5, {0x53, 0x8B, 0x5C, 0x24, 0x08}, 1, "Footprint_EnsureLevelPolygons (W6a)"},         // ret 4
    {0x0080E610, 8, {0x51, 0x57, 0x8B, 0xF9, 0x83, 0x7F, 0x38, 0x00}, 1, "Footprint_EnsureAllPolygons (W6b)"},  // ret
    {0x0080E980, 8, {0x83, 0xEC, 0x2C, 0x53, 0x8B, 0x5C, 0x24, 0x34}, 1, "footprint +0x78 rebuild (W6c)"},       // ret 0xC
    {0x0080EC30, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}, 1, "footprint +0xF0 transform (W6d)"},       // ret 4
    {0x0080E0B0, 7, {0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x38, 0x00}, 1, "Footprint_InvalidatePolygons (W6e)"},  // ret
    {0x0080F6A0, 5, {0x8B, 0x54, 0x24, 0x08, 0x53}, 1, "Footprint_SetLevelPolygons (W6f)"},            // ret 0xC
    {0x0080CB40, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}, 1, "footprint +0x100 class 0xFEB848 (W6g)"},  // ret 8
    {0x00806770, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}, 1, "footprint +0x100 class 0xFEB970 (W6g)"},  // ret 8
};
static void* g_qlTramp[QL_N];

// Fix C-A (docs/84 §4.4, §7 #6): Footprint_EnsureLevelPolygons 0x80E5B0 (W6a, thiscall(fp)(level), ret 4) marks an invalid
// level valid and frees its polygons before it rebuilds them through slots +0xF8/+0xFC; for these four classes the rebuild
// reaches the lot table (gate 12), so on the sim thread during a job it waits there with the level valid and empty for the
// rest of the job, which the job may read. C-A takes +0x918 in the stub, before the original body runs: the same wait, moved
// before the level is touched. Only with no QL held on this thread (lock order, §5); counted either way (T-L5).
static volatile LONG g_caOn;
static DWORD g_caClasses[4] = {0x00FEB848, 0x00FEB970, 0x00FEBD10, 0x00FEBE38};  // steerro-test points one at its fake vtable
static const int QL_W6A = 4;  // kQlSites index of 0x80E5B0

static void CaEnter(DWORD tid, DWORD* slot) {
    Ts3nCounters& k = ts3native_counters;
    const u8* fp = (const u8*)(uintptr_t)slot[-2];  // ecx in the stub's pushad frame
    const u8* cache = fp ? *(const u8* const*)(fp + 0x38) : NULL;
    if (!cache || cache[slot[1] * 0x24 + 0x20]) return;  // level valid: the original rebuilds nothing (its own test 0x80E5B9)
    int cls = 0;
    while (cls < 4 && g_caClasses[cls] != *(const DWORD*)fp) cls++;
    if (cls == 4) return;
    InterlockedIncrement(&k.caSimInJob[cls]);
    if (!k.caMode) return;
    RgThread& t = t_rg;
    RgDrop(t, slot);
    if (t_qlDepth) { InterlockedIncrement(&k.caQlHeld); return; }     // never wait holding QL
    if (t.n != t.readers || k.rgOwnerTid == (LONG)tid) return;          // inside a gate frame or the +0x918 owner: no wait
    void* lock = g_async.lock;
    if (!lock) return;
    if (g_inInvoke && tid == g_async.owner) { InterlockedIncrement(&k.rgBorrows); return; }  // nested in the job: borrowed
    InterlockedIncrement(&g_rgOffSimOpen);  // as a gate entry: published before the job state is read again
    LONG rel = RG_REL_OFFSIM;
    if (g_async.state == A_RUNNING) {
        static const DWORD kInfinite = 0xFFFFFFFF;
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        t_waitKey = 0x0080E5B0;  // probe P0: this wait keyed by the rebuild
        LockAcquireHook(lock, NULL, &kInfinite);
        t_waitKey = 0;
        rel |= RG_REL_LOCK;
        InterlockedIncrement(&k.caWaits);
        InterlockedExchangeAdd(&k.caWaitUs, UsSince(t0));
    }
    if (!RgPush(t, slot, 0, rel, lock)) RgUndo(RgFrame{NULL, 0, 0, rel, lock});
}

static void __cdecl QlEnter(int i, DWORD* slot) {
    const DWORD tid = GetCurrentThreadId();
    if (RgIsWorker(tid)) {
        RgThread& t = t_rg;
        RgDrop(t, slot);
        QlPushWorker(t, slot, 1);
        return;
    }
    if (i == QL_W6A && g_caOn && g_async.state == A_RUNNING && RgIsSim(tid)) CaEnter(tid, slot);
    // tripwires: the sim thread rewriting the tree, the lot table or a polygon cache while a job runs (an existing race with
    // the job's readers; inside the clone it would be ours: that answer is dropped)
    if (g_async.state == A_RUNNING && RgIsSim(tid)) {
        Ts3nCounters& k = ts3native_counters;
        InterlockedIncrement(kQlSites[i].cache ? &k.steerSimCacheWrite : &k.steerSimTreeWrite);
        if (t_inRo) { InterlockedIncrement(&k.steerSimWriteInRo); t_srFail = SR_FB_TRIP; }
    }
}

#define QL_STUB(i) static __declspec(naked) void QlStub##i() { __asm pushad __asm pushfd __asm lea eax, [esp + 36] \
    __asm push eax __asm push i __asm call QlEnter __asm add esp, 8 __asm popfd __asm popad __asm jmp dword ptr [g_qlTramp + i * 4] }
QL_STUB(0) QL_STUB(1) QL_STUB(2) QL_STUB(3) QL_STUB(4) QL_STUB(5) QL_STUB(6) QL_STUB(7) QL_STUB(8) QL_STUB(9) QL_STUB(10) QL_STUB(11)
static void (*const kQlStubs[QL_N])() = {QlStub0, QlStub1, QlStub2, QlStub3, QlStub4, QlStub5, QlStub6, QlStub7, QlStub8, QlStub9,
                                         QlStub10, QlStub11};

// --- the clone ---
// 0x85AE60's output (0x34 bytes, 16-aligned, as Locomotion's and EditPath's stack copies): xyz w, a zero vector, 1.0, level
struct alignas(16) SrPt { float v[4]; float zero[4]; float one; LONG level; DWORD tail[6]; };
struct SrCtx { const u8* rec; DWORD flag; int n; int fail; const u8* objs[kSrCap]; };

static bool SrIn(DWORD v, const DWORD* s, int n) {
    for (int i = 0; i < n; i++) if (s[i] == v) return true;
    return false;
}

// Slot targets of the 9 footprint classes and the middle table (docs/83 §8, re-read 2026-10-17); anything else is a doubt
static bool SrSlotsOk(const u8* obj) {
    if (!g_srCheckSlots) return true;
    static const DWORD k8[] = {0x0080F840, 0x0080C300, 0x00807090, 0x00807B70, 0x008066C0, 0x006119F0};
    const DWORD* vt = *(const DWORD* const*)obj;
    const DWORD* sv = *(const DWORD* const*)(obj + 4);
    if (!vt || !sv) return false;
    return vt[0x60 / 4] == 0x009C1B00 && (vt[0x28 / 4] == 0x00620D60 || vt[0x28 / 4] == 0x00620DD0) && SrIn(vt[2], k8, 6) &&
           (vt[3] == 0x00624450 || vt[3] == 0x0070AE20) && vt[0x84 / 4] == 0x00620CC0 && vt[0x7C / 4] == 0x00806FE0 &&
           (vt[0xC0 / 4] == 0x00620D60 ||
            (vt[0xC0 / 4] == 0x007EAEA0 && vt[0xE8 / 4] == 0x0080E690 && (vt[0] == 0x00619EF0 || vt[0] == 0x0083F5F0))) &&
           sv[1] == 0x00620E60 && (sv[2] == 0x00624ED0 || sv[2] == 0x00620E50);
}

static void SrAdd(SrCtx* c, const u8* obj) {
    for (int i = 0; i < c->n; i++) if (c->objs[i] == obj) return;
    if (c->n == kSrCap) { c->fail = SR_FB_SHAPE; return; }
    c->objs[c->n++] = obj;
}

// CollectLeaf 0x669850 (masked = 0, its args 4/5 are 1/0 from the visitor) and CollectMasked 0x669230 for this context:
// options object 0 (0x8294E0 returns record+0x28 = 0, or 0 for no record), so the id list and RoutingOptions_Test are not
// reached; ctx+0x50 = 0, so the type 0x13/0x18 and 0x1E branches are not either
static void SrCollect(SrCtx* c, const u8* node, bool masked, DWORD bit) {
    const u8* l = node + 8;
    const bool heap = (l[0] & 1) != 0;
    const DWORD cnt = heap ? *(const DWORD*)(l + 4) : 1;
    if (heap && !cnt) return;
    const u8* e = heap ? (const u8*)(uintptr_t)(*(const DWORD*)l & ~1u) : l;
    const u8* end = e + cnt * 12;
    const u8* local[kSrLocal];
    int nl = 0;
    bool seen = false;
    const DWORD ow = c->rec ? *(const DWORD*)c->rec : 0;
    for (; e != end; e += 12) {
        const u8* ent = *(const u8* const*)e;
        if (!ent) continue;
        if (masked && !(bit < 32 ? *(const DWORD*)(e + 4) & (1u << (bit & 31)) : *(const DWORD*)(e + 8) & (1u << ((bit - 32) & 31))))
            continue;
        const u8* obj = *(const u8* const*)(ent + 8);
        if (!obj || !SrSlotsOk(obj)) { c->fail = SR_FB_SHAPE; return; }  // the original dereferences a NULL footprint
        const void* const* vt = *(const void* const* const*)obj;
        if (!((SlotBoolArgFn)vt[0x60 / 4])(obj, NULL, ow)) continue;
        if (((SlotBoolFn)vt[0x28 / 4])(obj, NULL)) continue;
        int cls = ((SlotClassFn)vt[2])(obj, NULL, c->rec, ent[0x10]);
        if (c->rec && ((SlotIdFn)vt[0x84 / 4])(obj, NULL) == *(const unsigned __int64*)(c->rec + 8)) continue;
        const int t = ((SlotIntFn)vt[3])(obj, NULL);
        if (t == 0x1F) { seen = true; continue; }
        if (t == 0x14) cls = 1;
        if (c->flag) {
            if (cls == 2) {
                if (nl == kSrLocal) { c->fail = SR_FB_SHAPE; return; }
                local[nl++] = obj;
                continue;
            }
            if ((DWORD)cls < 2 || cls == 0xD) continue;
        }
        SrAdd(c, obj);
    }
    if (c->flag && seen)
        for (int i = 0; i < nl; i++) SrAdd(c, local[i]);
}

static void SrChildren(SrCtx* c, const u8* node, int depth) {  // CollectChildren 0x669B90 / CollectSubtree 0x669B30
    if (depth > 32) { c->fail = SR_FB_SHAPE; return; }
    for (int i = 0; i < 4 && !c->fail; i++) {
        const u8* ch = *(const u8* const*)(node + 0xC + i * 4);
        if (!ch) continue;
        if (!(ch[4] & 3)) SrCollect(c, ch, false, 0);
        else SrChildren(c, ch, depth + 1);
    }
}

// The walks' visitor (in place of 0x662DB0 / 0x662DD0 -> LotQuadTree_FootprintVisitor 0x669CD0): stdcall (ctx, cell iterator,
// list), ecx = Q unused, returns 1 to go on. A failed clone stops the walk (its answer is dropped).
static long g_srVisitKind[3];  // masked, leaf, children (owner thread; steerro-test reports its coverage)
static u8 __stdcall SrVisit(SrCtx* c, const u8* iter, void*) {
    if (c->fail) return 0;
    const u8* node = *(const u8* const*)(iter + 0xC);
    if (!node) return 1;
    const DWORD b = iter[0x1C];
    const int kind = b ? 0 : !(node[4] & 3) ? 1 : 2;
    g_srVisitKind[kind]++;
    if (kind == 0) SrCollect(c, node, true, g_srMaskTab[b]);
    else if (kind == 1) SrCollect(c, node, false, 0);
    else SrChildren(c, node, 0);
    return c->fail ? 0 : 1;
}

// LotTable_Find 0x627E00 on P+0x48. contains = LotTable_Contains 0x628A90 rules (key 0 / -1 / active lot: no entry, key
// re-checked at node+0x10); else LotTable_GetOrInsert 0x62EAB0 (a miss would insert: a doubt)
static int SrEntry(const u8* p, DWORD lo, DWORD hi, bool contains, const u8** entry) {
    *entry = NULL;
    if (contains) {
        if (!(lo | hi) || (lo & hi) == 0xFFFFFFFF) return SR_OK;
        const u8* act = g_srActiveLot();
        if (!act) return SR_FB_SHAPE;  // the original reads [0 + 0x48]
        if (lo == *(const DWORD*)(act + 0x48) && hi == *(const DWORD*)(act + 0x4C)) return SR_OK;
    }
    const u8* tbl = p + 0x48;
    const DWORD cnt = *(const DWORD*)(tbl + 8);
    const DWORD* bk = *(const DWORD* const*)(tbl + 4);
    if (!cnt || !bk) return SR_FB_SHAPE;
    DWORD out[2];
    const DWORD key[2] = {lo, hi};
    g_srLotFind(tbl, NULL, out, key);
    if (out[0] == bk[cnt]) return contains ? SR_OK : SR_FB_LOTMISS;
    const u8* node = (const u8*)(uintptr_t)out[0];
    if (contains && (*(const DWORD*)(node + 0x10) != lo || *(const DWORD*)(node + 0x14) != hi)) return SR_OK;
    *entry = node + 0x10;
    return SR_OK;
}

static const u8* SrCell(const u8* entry, LONG level) {  // LotTable_GetCellGraph 0x62FB10 after the hit
    const LONG i = level - *(const LONG*)(entry + 0x40);
    if (i < 0) return NULL;
    const DWORD* b = *(const DWORD* const*)(entry + 0x74);
    const DWORD* e = *(const DWORD* const*)(entry + 0x78);
    if (i >= (LONG)(e - b)) return NULL;
    return (const u8*)(uintptr_t)b[i];
}

// 0x62ED40 / 0x62EF30 after the hit: {x, 0, z, 1} times the rows at entry+0xA0/+0xB0/+0xC0/+0xD0, same SSE operations in the
// same order (bit-exact, steerro-test); u = x', v = z'
static void SrXform(const u8* entry, float x, float z, float* u, float* v) {
    const __m128 p = _mm_set_ps(*g_srOne, z, 0.0f, x);
    __m128 r = _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(p, p, 0x55), _mm_load_ps((const float*)(entry + 0xB0))),
                          _mm_mul_ps(_mm_shuffle_ps(p, p, 0x00), _mm_load_ps((const float*)(entry + 0xA0))));
    r = _mm_add_ps(r, _mm_mul_ps(_mm_shuffle_ps(p, p, 0xAA), _mm_load_ps((const float*)(entry + 0xC0))));
    r = _mm_add_ps(r, _mm_mul_ps(_mm_shuffle_ps(p, p, 0xFF), _mm_load_ps((const float*)(entry + 0xD0))));
    __declspec(align(16)) float o[4];
    _mm_store_ps(o, r);
    *u = o[0];
    *v = o[2];
}

// One walk under QL. The original flushes first when Q+0x48 < 1 and the deferred flag is set (a write): a doubt. Pin counter
// and flag must not move while we walk (tripwire: a job query or pin on this tree in between).
static int SrWalk(SrCtx* c, const u8* q, bool point, float x, float y, float r) {
    const LONG q48 = *(volatile const LONG*)(q + 0x48);
    const u8 q4c = *(volatile const u8*)(q + 0x4C);
    if (q4c && q48 < 1) return SR_FB_DEFERRED;
    if (point) g_srWalkPt(q, NULL, c, x, y, r, (void*)SrVisit, NULL);
    else g_srWalkSeg(q, NULL, c, x, y, x, y, r, (void*)SrVisit, 0, NULL);  // locomotion: start == goal, w = 0
    if (*(volatile const LONG*)(q + 0x48) != q48 || *(volatile const u8*)(q + 0x4C) != q4c) {
        InterlockedIncrement(&ts3native_counters.steerPinMoved);
        return SR_FB_TRIP;
    }
    return c->fail;
}

static int SrCmpPtr(const void* a, const void* b) {
    const uintptr_t x = *(const uintptr_t*)a, y = *(const uintptr_t*)b;
    return x < y ? -1 : x > y;
}

// Footprints of lot (lo, hi) near pt as 0x631F10 / 0x631E20 collect them (point: radius r, record rec, mode 0; segment:
// NULL context, mode 1), sorted by sub-object pointer like 0x82EDB0 / 0x82F0B0 copy them out
static int SrQuery(SrCtx* c, const u8* p, DWORD lo, DWORD hi, const SrPt& pt, bool point, float r, const u8* rec) {
    c->n = 0;
    c->fail = SR_OK;
    c->rec = point ? rec : NULL;
    c->flag = point ? 0 : 1;
    const u8* entry;
    int why = SrEntry(p, lo, hi, true, &entry);
    if (why || !entry) return why;
    const u8* q = SrCell(entry, pt.level);
    if (!q) return SR_OK;
    float u, v;
    SrXform(entry, pt.v[0], pt.v[2], &u, &v);
    if ((why = SrWalk(c, q, point, u, v, r)) != SR_OK) return why;
    qsort(c->objs, c->n, sizeof(c->objs[0]), SrCmpPtr);  // obj+4 order = obj order
    return SR_OK;
}

struct SrNode { SrPt conv; const u8* raw; bool gLot, eLot; DWORD gLo, gHi, eLo, eHi, pad[2]; };  // 16-byte multiple
struct SrOut { int nIds; DWORD ids[kSrIds][2]; int nSubs; const u8* subs[kSrCap]; };

static bool SrHasId(const DWORD* b, int n, DWORD lo, DWORD hi) {
    for (int i = 0; i < n; i++) if (b[i * 2] == lo && b[i * 2 + 1] == hi) return true;
    return false;
}

// RouteFollow_EditPath 0x85F5A0 after its gather, for start == goal (docs/83 §5): ids of the gathered footprints that pass
// its type, ignore-list, ids and exclude tests, and its level-0 segment test 0x80E690 (cache valid, unchanged)
static int SrConsume(SrCtx* c, const u8* p, const SrNode& nd, const DWORD* ids0, int n0, SrOut* out, const DWORD* excl, int nExcl,
                     const void* ign1, DWORD n1, const void* ign2, DWORD n2) {
    __declspec(align(16)) float a[4], b[4];
    const u8* entryE = NULL;
    if (nd.eLot) {
        const int why = SrEntry(p, nd.eLo, nd.eHi, false, &entryE);
        if (why) return why;
        float u, v;
        SrXform(entryE, ((const float*)nd.raw)[0], ((const float*)nd.raw)[2], &u, &v);
        a[0] = a[2] = a[3] = u;  // {u, v, u, u} for start and goal (the same point here)
        a[1] = v;
        memcpy(b, a, sizeof(b));
    }
    for (int i = 0; i < c->n; i++) {
        const u8* obj = c->objs[i];
        const u8* sub = obj + 4;
        const void* const* svt = *(const void* const* const*)sub;
        const unsigned __int64 id = ((SlotIdFn)svt[1])(sub, NULL);
        const DWORD lo = (DWORD)id, hi = (DWORD)(id >> 32);
        const int t = ((SlotIntFn)svt[2])(sub, NULL);
        if (t == 0x14 || t == 9 || t == 0x11 || t == 0xC || t == 0xD || t == 0x17) continue;
        if (g_srIdList(lo, hi, ign1, n1) || g_srIdList(lo, hi, ign2, n2)) continue;
        if (SrHasId(ids0, n0, lo, hi) || SrHasId(&out->ids[0][0], out->nIds, lo, hi)) continue;  // already in the caller's ids
        if (SrHasId(excl, nExcl, lo, hi)) continue;                                             // an obstacle-query footprint
        const void* const* vt = *(const void* const* const*)obj;
        if (((SlotBoolFn)vt[0xC0 / 4])(obj, NULL) && entryE) {
            const u8* cache = *(const u8* const*)(obj + 0x38);
            if (!cache || !cache[0x20]) return SR_FB_CACHE;  // the original would rebuild it (0x80E5B0)
            u8 before[0x24];
            memcpy(before, cache, sizeof(before));
            const u8 hit = ((SlotSegFn)vt[0xE8 / 4])(obj, NULL, 0, a, b);
            if (memcmp(before, cache, sizeof(before)) != 0) return SR_FB_CACHE;  // T3: a writer outside W1-W6
            if (!hit) continue;
        }
        if (out->nIds == kSrIds) return SR_FB_SHAPE;
        out->ids[out->nIds][0] = lo;
        out->ids[out->nIds][1] = hi;
        out->nIds++;
    }
    return SR_OK;
}

static u8* SrFirstFlagged(u8* agent) {
    u8* head = agent + 8;
    for (u8* n = *(u8**)head; n != head; n = *(u8**)n)
        if (n[0x30]) return n;
    return NULL;
}

// The clone: ids that Locomotion_ObstacleCheck would append, in order, into out (ids0/n0 = the caller's ids before the call).
// Caller: owner thread, preconditions checked. QL is taken here (try only) for phase 2.
static int SrRun(u8* agent, const DWORD* ids0, int n0, const void* ign1, DWORD n1, const void* ign2, DWORD n2, SrOut* out, SrCtx* c) {
    out->nIds = out->nSubs = 0;
    u8* world = g_srWorldGet();
    u8* wm = *g_srWmSlot;
    if (!world || !wm) return SR_FB_STATE;
    const u8* p = world[0x974] ? *(const u8* const*)(world + 0x95C) : NULL;  // RoutingWorld_GetLotPlanner 0x829BC0
    u8* wmw = *(u8* const*)(world + 0x964);
    if (!p || !wmw) return SR_FB_SHAPE;
    if (wmw[0x1CC] || wm[0x1CC]) return SR_FB_DIRTY;  // World_LotLookup would rebuild the lot index (wm+0x228)
    // phase 1, no lock: 0x85AE60 and the lot lookups, as the originals run them before their lock. nodes[0] = the first
    // flagged node (both 0x82EDB0 calls); nodes[1..] = the flagged nodes from the second node on (the EditPath loop starts
    // at [[agent+8]], 0x85FE81: the path's first node is never edited)
    static SrNode nodes[kSrNodes + 1];  // owner thread only
    int nn = 0;
    u8* head = agent + 8;
    for (u8* n = *(u8**)head; n != head; n = *(u8**)n) {
        if (!n[0x30]) continue;
        if (nn == kSrNodes + 1) return SR_FB_SHAPE;
        SrNode& nd = nodes[nn++];
        nd.raw = n + 0x10;
        memset(&nd.conv, 0, sizeof(nd.conv));
        nd.conv.one = *g_srOne;
        g_srConv(nd.raw, &nd.conv);
        const u8* lot = g_srLotLookup(wmw, NULL, nd.conv.v[0], nd.conv.v[2]);  // 0x82EDB0 / 0x82F0B0 (goal == start)
        nd.gLot = lot != NULL;
        nd.gLo = lot ? *(const DWORD*)(lot + 0x48) : 0;
        nd.gHi = lot ? *(const DWORD*)(lot + 0x4C) : 0;
        const float* raw = (const float*)nd.raw;
        lot = g_srLotLookup(wm, NULL, raw[0], raw[2]);  // EditPath's own lookup on the raw node point
        nd.eLot = lot != NULL;
        nd.eLo = lot ? *(const DWORD*)(lot + 0x48) : 0;
        nd.eHi = lot ? *(const DWORD*)(lot + 0x4C) : 0;
        if (nn == 1 && n != *(u8**)head) {  // the first flagged node is also an EditPath node: second entry, same point
            if (nn == kSrNodes + 1) return SR_FB_SHAPE;
            nodes[nn++] = nd;
        }
    }
    if (!nn) return SR_FB_STATE;
    // phase 2, QL shared
    if (!TryAcquireSRWLockShared(&g_ql)) return SR_FB_TRY;
    if (g_steerBroken) { ReleaseSRWLockShared(&g_ql); return SR_FB_STATE; }
    const LONG seq = g_qlSeq;
    t_inRo = true;
    t_qlShared = 1;  // T-L2
    t_srFail = SR_OK;
    // the obstacle query (both of Locomotion's 0x82EDB0 calls: identical inputs): option record as 0x82EDB0 builds it
    __declspec(align(16)) u8 rec[0x30] = {};
    *(DWORD*)rec = *(const DWORD*)(agent + 0xB8);
    rec[0x24] = 1;
    static DWORD excl[kSrCap][2];
    int nExcl = 0;
    int why = SR_OK;
    if (nodes[0].gLot) why = SrQuery(c, p, nodes[0].gLo, nodes[0].gHi, nodes[0].conv, true, *(const float*)(agent + 0x40), rec);
    else c->n = 0;
    for (int i = 0; !why && i < c->n; i++) {
        const u8* sub = c->objs[i] + 4;
        const unsigned __int64 id = ((SlotIdFn)(*(const void* const* const*)sub)[1])(sub, NULL);
        excl[nExcl][0] = (DWORD)id;
        excl[nExcl][1] = (DWORD)(id >> 32);
        nExcl++;
        out->subs[out->nSubs++] = sub;
    }
    for (int i = 1; !why && i < nn; i++) {
        if (!nodes[i].gLot) continue;  // 0x82F0B0 returned 0: EditPath appends nothing
        if ((why = SrQuery(c, p, nodes[i].gLo, nodes[i].gHi, nodes[i].conv, false, 0.0f, NULL)) != SR_OK) break;
        why = SrConsume(c, p, nodes[i], ids0, n0, out, &excl[0][0], nExcl, ign1, n1, ign2, n2);
    }
    if (!why && t_srFail) why = t_srFail;
    if (!why && g_qlSeq != seq) { InterlockedIncrement(&ts3native_counters.steerSeqSkew); why = SR_FB_TRIP; }
    t_inRo = false;
    t_qlShared = 0;
    ReleaseSRWLockShared(&g_ql);
    return why;
}

static volatile LONG g_srNoted;
static void SrNote(const char* what, const u8* agent, int a, int b) {
    if (InterlockedIncrement(&g_srNoted) <= 16) Log("steer RO: %s (agent %p, %d / %d)", what, agent, a, b);
}

static void SrFallback(int why) {
    if (why > 0 && why <= 8) InterlockedIncrement(&ts3native_counters.steerFb[why - 1]);
}

// Shadow (no job running, job starts held off): the game's call, then the clone on the same world; then the obstacle query
// itself (0x82EDB0 into our own vector) against the clone's point query
static void SrShadow(u8* agent, u8* ids, const void* ign1, DWORD n1, const void* ign2, DWORD n2, SrOut* out, SrCtx* c) {
    Ts3nCounters& k = ts3native_counters;
    const int n0 = (int)((*(DWORD**)(ids + 4) - *(DWORD**)ids) / 2);
    g_locoOrig(agent, NULL, ids, ign1, n1, ign2, n2);
    const int why = SrRun(agent, *(const DWORD* const*)ids, n0, ign1, n1, ign2, n2, out, c);
    InterlockedIncrement(&k.steerShadowRuns);
    if (why) { SrFallback(why); return; }
    const DWORD* got = *(const DWORD* const*)ids + n0 * 2;
    const int added = (int)((*(DWORD**)(ids + 4) - *(DWORD**)ids) / 2) - n0;
    if (added != out->nIds || memcmp(got, out->ids, (size_t)added * 8) != 0) {
        bool same = added == out->nIds;  // same ids in another order?
        for (int i = 0; same && i < added; i++) same = SrHasId(&out->ids[0][0], out->nIds, got[i * 2], got[i * 2 + 1]);
        InterlockedIncrement(same ? &k.steerShadowOrder : &k.steerShadowMismatch);
        SrNote(same ? "shadow: same ids, other order" : "shadow: appended ids differ", agent, added, out->nIds);
    }
    // second level: 0x82EDB0 itself, as Locomotion calls it (same point, radius, mode 0, option word), vs the clone's list
    u8* first = SrFirstFlagged(agent);
    u8* world = g_srWorldGet();
    if (!first || !world) return;
    SrPt pt;
    memset(&pt, 0, sizeof(pt));
    pt.one = *g_srOne;
    g_srConv(first + 0x10, &pt);
    DWORD vec[4] = {0, 0, 0, kSrVecTag};
    g_srQuery(world, NULL, &pt, *(const float*)(agent + 0x40), vec, 0, *(const DWORD*)(agent + 0xB8));
    const u8* const* b = (const u8* const*)(uintptr_t)vec[0];
    const int n = (int)((vec[1] - vec[0]) / 4);
    if (n != out->nSubs || (n && memcmp(b, out->subs, (size_t)n * 4) != 0)) {
        InterlockedIncrement(&k.steerShadowQueryMismatch);
        SrNote("shadow: obstacle query differs", agent, n, out->nSubs);
    }
    g_srVecRel(vec, NULL, (void*)(uintptr_t)vec[0], (void*)(uintptr_t)vec[1]);
    if (vec[0]) g_srFree((void*)(uintptr_t)vec[0]);
}

static void __fastcall LocoHook(u8* agent, void* edx, u8* ids, const void* ign1, DWORD n1, const void* ign2, DWORD n2) {
    Ts3nCounters& k = ts3native_counters;
    const DWORD tid = GetCurrentThreadId();
    // no switch, not the job owner, or no flagged node (then the original touches no lock): the game's call
    if ((!k.steerMode && !k.steerShadow) || tid != g_async.owner || !SrFirstFlagged(agent)) {
        g_locoOrig(agent, edx, ids, ign1, n1, ign2, n2);
        return;
    }
    static SrOut out;  // owner thread only
    static SrCtx ctx;
    InterlockedIncrement(&k.steerCalls);
    if (g_async.state != A_RUNNING) {
        if (!k.steerShadow) { g_locoOrig(agent, edx, ids, ign1, n1, ign2, n2); return; }
        InterlockedIncrement(&g_rgOffSimOpen);  // no job starts while the two answers are compared (worker Dekker order)
        if (g_async.state == A_RUNNING) g_locoOrig(agent, edx, ids, ign1, n1, ign2, n2);
        else SrShadow(agent, ids, ign1, n1, ign2, n2, &out, &ctx);
        InterlockedDecrement(&g_rgOffSimOpen);
        return;
    }
    // a job runs: read-only answer, unless this thread holds +0x918, serves the job's callback, has a gate frame open, or
    // the worker lost a QL frame
    int why = SR_FB_STATE;
    const RgThread& t = t_rg;
    if (k.steerMode && !g_inInvoke && k.rgOwnerTid != (LONG)tid && t.n == t.readers && !g_steerBroken) {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        why = SrRun(agent, *(const DWORD* const*)ids, (int)((*(DWORD**)(ids + 4) - *(DWORD**)ids) / 2), ign1, n1, ign2, n2, &out, &ctx);
        if (!why) {
            for (int i = 0; i < out.nIds; i++) g_srIdPush(ids, NULL, out.ids[i]);
            const LONG us = UsSince(t0);
            InterlockedIncrement(&k.steerTaken);
            InterlockedExchangeAdd(&k.steerUs, us);
            Max(&k.steerMaxUs, us);
            return;
        }
    }
    SrFallback(why);
    g_locoOrig(agent, edx, ids, ign1, n1, ign2, n2);
}

// [FpRebuildLock] Enabled=1 alone: only the W6a stub (C-A); with [SteerRO] it rides on the full set
static void InstallCa(const wchar_t* ini, bool needStub) {
    Ts3nCounters& k = ts3native_counters;
    if (needStub) {
        const QlSite& s = kQlSites[QL_W6A];
        if (!(g_qlTramp[QL_W6A] = InstallDetour(s.name, s.addr, s.pro, s.len, (void*)kQlStubs[QL_W6A]))) return;
    }
    if (!g_qlTramp[QL_W6A]) return;
    k.caMode = Clamp(GetPrivateProfileIntW(L"FpRebuildLock", L"Mode", 0, ini), 0, 1);
    g_caOn = 1;
    k.caReady = 1;
    Log("OK   footprint rebuild lock C-A (Mode=%ld: %s)", k.caMode, k.caMode ? "take +0x918 first" : "count only");
}

// [SteerRO] Enabled=1: every prologue is checked first; one differs and nothing is hooked
static void InstallSteerRO(const wchar_t* ini) {
    const bool ca = GetPrivateProfileIntW(L"FpRebuildLock", L"Enabled", 0, ini) != 0;
    Ts3nCounters& k = ts3native_counters;
    if (!GetPrivateProfileIntW(L"SteerRO", L"Enabled", 0, ini)) {
        Log("steer RO off (Enabled=0)");
        if (!ca) { Log("footprint rebuild lock off (Enabled=0)"); return; }
        if (!k.rgEnforce || !k.asyncReady) { Log("SKIP footprint rebuild lock: needs [RouteGate] Enforce=1 with the async path"); return; }
        InstallCa(ini, true);
        return;
    }
    if (!k.rgEnforce || !k.asyncReady) { Log("SKIP steer RO: needs [RouteGate] Enforce=1 with the async path"); return; }
    bool ok = Matches((const void*)(uintptr_t)STEER_LOCO, kLocoPro, sizeof(kLocoPro)) && g_rgTramp[3] != NULL;  // W4 = gate stub 3
    for (int i = 0; ok && i < QL_N; i++) ok = Matches((const void*)(uintptr_t)kQlSites[i].addr, kQlSites[i].pro, kQlSites[i].len);
    if (!ok) { Log("SKIP steer RO: a prologue differs (0x85FD10, W1-W6) or the LotGraph_Build gate stub is missing"); return; }
    int n = 0;
    for (int i = 0; i < QL_N; i++)
        n += (g_qlTramp[i] = InstallDetour(kQlSites[i].name, kQlSites[i].addr, kQlSites[i].pro, kQlSites[i].len, (void*)kQlStubs[i])) != NULL;
    g_qlOn = 1;
    if (ca) InstallCa(ini, false);
    if (n != QL_N) {  // a W site without QL: the clone could read under its writes; it stays off (the stubs that exist still lock)
        Log("FAIL steer RO: %d of %d worker writer detours, read-only steering stays off", n, QL_N);
        return;
    }
    g_locoOrig = (LocoFn)InstallDetour("Locomotion_ObstacleCheck", STEER_LOCO, kLocoPro, sizeof(kLocoPro), (void*)LocoHook);
    if (!g_locoOrig) return;
    k.steerMode = Clamp(GetPrivateProfileIntW(L"SteerRO", L"Mode", 1, ini), 0, 1);
    k.steerShadow = Clamp(GetPrivateProfileIntW(L"SteerRO", L"Shadow", 0, ini), 0, 1);
    k.steerReady = 1;
    Log("OK   steer RO: 0x85FD10 + %d worker writer detours + LotGraph_Build gate (Mode=%ld Shadow=%ld)", n, k.steerMode, k.steerShadow);
}
