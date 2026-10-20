// TestAutShadow (autshadow.cpp, docs/122 §6.1 T1, T2, T4; gate G-A1). Included by selftest.cpp. A fake provider table
// in this image stands for the exe's: fake IC bodies with the real names and the cdecl layouts of docs/122 §13.

struct AsFakeObj { DWORD vt, pad; AsU64 id; float local[3]; AsU64 parent; DWORD hidden; DWORD lot; };  // id at +8
static AsFakeObj g_afo[65];        // id = 5:i, i = 1..64; a parent always has a lower index
static float g_afSunrise = 6.0f, g_afSea = 1.25f;
static DWORD g_afArena[1 << 16];   // fake MonoArrays made by the bodies (ring)
static DWORD g_afArenaAt;
static int g_afRefreshProbe;

static AsFakeObj* AfFind(DWORD lo, DWORD hi) { return hi == 5 && lo >= 1 && lo <= 64 ? &g_afo[lo] : NULL; }
static void AfWorld(const AsFakeObj* o, float* p) {
    p[0] = p[1] = p[2] = 0;
    for (; o; o = o->parent ? AfFind((DWORD)o->parent, (DWORD)(o->parent >> 32)) : NULL)
        for (int i = 0; i < 3; i++) p[i] += o->local[i];
}
static float* __cdecl AfPos(float* ret, DWORD lo, DWORD hi) { AfWorld(AfFind(lo, hi), ret); return ret; }
static AsU64 __cdecl AfParent(DWORD lo, DWORD hi) { const AsFakeObj* o = AfFind(lo, hi); return o ? o->parent : 0; }
static DWORD __cdecl AfHidden(DWORD lo, DWORD hi) { const AsFakeObj* o = AfFind(lo, hi); return o ? o->hidden : 0; }
static short __cdecl AfTomb(DWORD lo, DWORD) { g_afRefreshProbe++; return (short)-(int)(lo & 7); }  // negative: the mask matters
static float __cdecl AfSunrise() { return g_afSunrise; }
static float __cdecl AfSea() { return g_afSea; }
static DWORD __cdecl AfCount(DWORD type, DWORD lotLo, DWORD, DWORD flags) {
    DWORD n = 0;
    for (int i = 1; i <= 64; i++) n += g_afo[i].lot == lotLo && (!flags || i % 2 == 0);
    return n * 16 + (type & 15);
}
static DWORD* __cdecl AfOnLot(DWORD type, DWORD lotLo, DWORD, DWORD flags) {
    if (lotLo == 0) return NULL;
    if (g_afArenaAt + 80 > (1 << 16)) g_afArenaAt = 0;
    DWORD* a = g_afArena + g_afArenaAt;
    DWORD n = 0;
    for (int i = 1; i <= 64; i++) if (g_afo[i].lot == lotLo && (!flags || i % 2 == 0)) a[4 + n++] = i;
    a[0] = 0xA0000 + type, a[1] = 0, a[2] = n, a[3] = 0;
    g_afArenaAt += 4 + n;
    return a;
}
static AsU64 __cdecl AfLotLoc(float x, float, float z, DWORD* out, DWORD room) {
    out[0] = (DWORD)(int)x, out[1] = (DWORD)(int)z, out[2] = room;
    return 0x700000000ull + (DWORD)(int)x / 10;
}
struct AsFakeStr { DWORD vt, sync; LONG len; wchar_t c[16]; };
// like 0x741230: the out is written only when the option exists (odd length here)
static char __cdecl AfOption(const AsFakeStr* s, DWORD* out) { if (s->len & 1) *out = (DWORD)s->len * 3 + s->c[0]; return (char)(s->len & 1); }
// fake MT (stands for 0x4FA990 / 0x4FAA70: same this-relative thiscall contract, a simpler generator)
static void __fastcall AfMtInit(void* mt, void*, DWORD seed) {
    DWORD* w = (DWORD*)mt;
    w[0] = seed | 1, w[0x270] = (DWORD)(uintptr_t)mt, w[0x271] = 0x270;
}
static double __fastcall AfMtNext(void* mt, void*) {
    DWORD* w = (DWORD*)mt;
    w[0] = w[0] * 1664525u + 1013904223u;
    return (w[0] >> 8) / 16777216.0;
}
static u8 g_afDet;
static int g_afRandMt;  // T5: the fake body draws from the host MT as the game's does
static double __cdecl AfRand() { return g_afRandMt ? AfMtNext(*g_asHostPtr + 0xC30, NULL) : 0.25; }
// docs/125: slot position (handle, slot hash, age/gender/species, exit) and Query_GetObjects(Type), no change events
static float g_afSlotBias;
static float* __cdecl AfSlotPos(float* ret, DWORD lo, DWORD hi, DWORD slot, DWORD ags, DWORD exitSlot) {
    AfWorld(AfFind(lo, hi), ret);
    ret[1] += (float)(slot & 15) + (float)ags * 0.5f + g_afSlotBias + (exitSlot ? 100.0f : 0.0f);
    return ret;
}
static DWORD* __cdecl AfQuery(DWORD type) {  // type 9: a world-wide type, 300 objects (over the keep cap)
    const DWORD n = type == 9 ? 300 : 0;
    if (g_afArenaAt + 4 + 64 + n > (1 << 16)) g_afArenaAt = 0;
    DWORD* a = g_afArena + g_afArenaAt;
    DWORD m = 0;
    if (n) for (; m < n; m++) a[4 + m] = 0x900 + m;
    else for (int i = 1; i <= 64; i++) if (g_afo[i].hidden == (type & 1)) a[4 + m++] = i;
    a[0] = 0xB0000 + type, a[1] = 0, a[2] = m, a[3] = 0;
    g_afArenaAt += 4 + m;
    return a;
}
static float __cdecl AfTerrain(float x, float) { return x + 0.5f; }  // unlisted 'f'
static float* __cdecl AfCamPos(float* ret) { ret[0] = ret[1] = ret[2] = 9; return ret; }  // unlisted 's'
static DWORD __cdecl AfUnlisted(DWORD a) { return a + 1; }  // unlisted int
static void* __cdecl AfMoney(DWORD, DWORD) { return (void*)(uintptr_t)0x1234; }  // a-stub IC

static void* g_asFakeTab[] = {
    (void*)AfPos, (void*)"ScriptCore.Objects::Objects_GetPosition",
    (void*)AfParent, (void*)"ScriptCore.Slots::Slots_GetParentIdImpl",
    (void*)AfHidden, (void*)"ScriptCore.World::World_ObjectGetHiddenFlags",
    (void*)AfTomb, (void*)"ScriptCore.World::World_GetObjectTombRoomIdImpl",
    (void*)AfSunrise, (void*)"ScriptCore.World::World_GetSunriseTime",
    (void*)AfSea, (void*)"ScriptCore.World::World_GetSeaLevelImpl",
    (void*)AfCount, (void*)"ScriptCore.Queries::Query_CountObjectsOnLot",
    (void*)AfOnLot, (void*)"ScriptCore.Queries::Query_GetObjectsOnLot",
    (void*)AfLotLoc, (void*)"ScriptCore.World::World_GetLotLocationImpl",
    (void*)AfOption, (void*)"ScriptCore.DeviceConfig::DeviceConfig_GetOption",
    (void*)AfRand, (void*)"ScriptCore.Random::Random_NextDoubleImpl",
    (void*)AfTerrain, (void*)"ScriptCore.World::World_GetTerrainHeightImpl",
    (void*)AfCamPos, (void*)"ScriptCore.CameraController::Camera_GetPosition",
    (void*)AfUnlisted, (void*)"ScriptCore.Fake::Fake_UnlistedImpl",
    (void*)AfMoney, (void*)"ScriptCore.EAText::EAText_GetMoneyString",
    (void*)0x12345678, (void*)"ScriptCore.Fake::Fake_NotCode",  // fn outside .text: not a table entry
    (void*)AfSlotPos, (void*)"ScriptCore.Slots::Slots_GetPositionOfSlotImpl",
    (void*)AfQuery, (void*)"ScriptCore.Queries::Query_GetObjects",
    NULL, NULL};
enum { AF_POS, AF_PARENT, AF_HIDDEN, AF_TOMB, AF_SUNRISE, AF_SEA, AF_COUNT, AF_ONLOT, AF_LOTLOC, AF_OPTION, AF_RAND,
       AF_TERRAIN, AF_CAMPOS, AF_UNLISTED, AF_MONEY, AF_NOTCODE, AF_SLOTPOS, AF_QUERY };
typedef float*(__cdecl* AfSlotPosFn)(float*, DWORD, DWORD, DWORD, DWORD, DWORD);
typedef DWORD*(__cdecl* AfQueryFn)(DWORD);
static void* AfSlot(int i) { return g_asFakeTab[i * 2]; }

static DWORD g_afNewArena[1 << 14];
static DWORD g_afNewAt;
static void* __cdecl AfNewArray(DWORD vtable, DWORD n) {  // stands for part 2's allocator
    if (g_afNewAt + 4 + n > (1 << 14)) g_afNewAt = 0;
    DWORD* a = g_afNewArena + g_afNewAt;
    a[0] = vtable, a[2] = n;
    g_afNewAt += 4 + n;
    return a;
}

static int AsFpTop() {
    unsigned short sw;
    __asm { fnstsw sw }
    return (sw >> 11) & 7;
}

static DWORD g_afObjHeads[64];  // fake object-event lists: node {priority, fn, next}
static DWORD g_afNodes[64][3];
static int g_afNodeN;
static char __cdecl AfObjReg(DWORD id, void* fn, LONG prio) {
    DWORD* n = g_afNodes[g_afNodeN++];
    n[0] = (DWORD)prio, n[1] = (DWORD)(uintptr_t)fn, n[2] = g_afObjHeads[id];
    g_afObjHeads[id] = (DWORD)(uintptr_t)n;
    return 1;
}

static AsFakeStr AfStr(const wchar_t* s) {
    AsFakeStr r = {0x51, 0, (LONG)wcslen(s), {0}};
    wcscpy_s(r.c, s);
    return r;
}

static u8 g_afFmt[16] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x8B, 0x45, 0x0C, 0xC7, 0x00, 0x77, 0x00, 0x00, 0x00, 0x8B};  // + E5 5D C3
static u8* g_afFmtFn;

// the published version's answers equal fresh body calls (the A/B copy is complete)
static void AfCheckPublished() {
    const AsTab& t = g_as->v[g_asPub];
    LONG xn = 0;
    for (int i = 0; i < t.n; i++) {
        const AsDesc& d = *g_asDescOf[t.e[i].ic];
        if (d.owner == AO_MEMO || d.kind == AK_REF) continue;
        AsEnt x = t.e[i];
        AsRefresh(x.ic, x, g_as->poolTmp, &xn);
        assert(AsSame(d, t.e[i], t.pool, x, g_as->poolTmp));
        xn = 0;
    }
}

static AsU64 AfId(int i) { return 0x500000000ull + (DWORD)i; }

static DWORD WINAPI AfWorker(void*) {
    Ts3nAutShadow& k = ts3native_autshadow;
    g_asWorkerTid = GetCurrentThreadId();
    AsWorkerBegin();
    assert(g_asHeld == g_asPub && g_asHeld >= 0);
    // T1: snapshot answers through the guards, one per return kind
    float pos[3] = {-1, -1, -1}, want[3];
    AfWorld(&g_afo[3], want);
    float* rp = ((float*(__cdecl*)(float*, DWORD, DWORD))AfSlot(AF_POS))(pos, 3, 5);
    assert(rp == pos && !memcmp(pos, want, 12));
    assert(((AsU64(__cdecl*)(DWORD, DWORD))AfSlot(AF_PARENT))(3, 5) == g_afo[3].parent);
    assert(((DWORD(__cdecl*)(DWORD, DWORD))AfSlot(AF_HIDDEN))(3, 5) == g_afo[3].hidden);
    assert(((short(__cdecl*)(DWORD, DWORD))AfSlot(AF_TOMB))(5, 5) == -5);
    const int top = AsFpTop();
    for (int i = 0; i < 20; i++) {
        volatile float s = ((float(__cdecl*)())AfSlot(AF_SUNRISE))();
        assert(s == 6.0f);
        volatile float u = ((float(__cdecl*)(float, float))AfSlot(AF_TERRAIN))(1, 2);  // unlisted 'f': 0.0, abort ic
        assert(u == 0.0f);
        volatile double r = ((double(__cdecl*)())AfSlot(AF_RAND))();  // abort rng: 0.0
        assert(r == 0.0);
    }
    assert(AsFpTop() == top);  // x87 stack balanced on every path
    assert(AsAborted() == AS_AB_IC && k.abort[AS_AB_IC] >= 20 && k.abort[AS_AB_RNG] == 20);
    AsResetAbort();
    DWORD loc[3] = {0, 0, 0};
    assert(((AsU64(__cdecl*)(float, float, float, DWORD*, DWORD))AfSlot(AF_LOTLOC))(25.0f, 0, 7.0f, loc, 2) == 0x700000002ull);
    assert(loc[0] == 25 && loc[1] == 7 && loc[2] == 2);
    const DWORD* arr = ((DWORD*(__cdecl*)(DWORD, DWORD, DWORD, DWORD))AfSlot(AF_ONLOT))(3, 1, 0, 0);
    const DWORD* fresh = AfOnLot(3, 1, 0, 0);
    assert(arr && arr[0] == fresh[0] && arr[2] == fresh[2] && !memcmp(arr + 4, fresh + 4, fresh[2] * 4));
    assert(arr >= g_afNewArena && arr < g_afNewArena + (1 << 14));  // the worker's own array, not the sim's
    assert(((DWORD(__cdecl*)(DWORD, DWORD, DWORD, DWORD))AfSlot(AF_COUNT))(3, 1, 0, 0) == AfCount(3, 1, 0, 0));
    assert(AsAborted() == -1);
    // misses: unknown row (struct: memory untouched, eax = the result pointer), unknown list key -> learned
    float p2[3] = {7, 7, 7};
    assert(((float*(__cdecl*)(float*, DWORD, DWORD))AfSlot(AF_POS))(p2, 40, 5) == p2 && p2[0] == 7);
    assert(AsAborted() == AS_AB_ROW);
    assert(((DWORD(__cdecl*)(DWORD, DWORD, DWORD, DWORD))AfSlot(AF_COUNT))(3, 2, 0, 1) == 0);
    assert(AsAborted() == AS_AB_ROW);  // first reason wins
    AsResetAbort();
    float c[3] = {1, 1, 1};
    assert(((float*(__cdecl*)(float*))AfSlot(AF_CAMPOS))(c) == c && c[0] == 1);  // unlisted 's'
    assert(((DWORD(__cdecl*)(DWORD))AfSlot(AF_UNLISTED))(5) == 0 && AsAborted() == AS_AB_IC);
    AsResetAbort();
    // memo: same content, other string object -> hit; other content -> abort memo (never learned)
    AsFakeStr s2 = AfStr(L"Shadows"), s3 = AfStr(L"Bloom");
    DWORD out = 0;
    assert(((char(__cdecl*)(const AsFakeStr*, DWORD*))AfSlot(AF_OPTION))(&s2, &out) == 1 && out == 7 * 3 + L'S');
    out = 99;
    assert(((char(__cdecl*)(const AsFakeStr*, DWORD*))AfSlot(AF_OPTION))(&s3, &out) == 0 && out == 99);
    assert(AsAborted() == AS_AB_MEMO);
    AsResetAbort();
    // memo with an unwritten out: same in-value -> the caller's own value stays; other in-value -> abort memo, untouched
    AsFakeStr s4 = AfStr(L"Glow");
    out = 42;
    assert(((char(__cdecl*)(const AsFakeStr*, DWORD*))AfSlot(AF_OPTION))(&s4, &out) == 0 && out == 42);
    assert(AsAborted() == -1);
    out = 5;
    assert(((char(__cdecl*)(const AsFakeStr*, DWORD*))AfSlot(AF_OPTION))(&s4, &out) == 0 && out == 5);
    assert(AsAborted() == AS_AB_MEMO);
    AsResetAbort();
    // a-stub: the shared empty string
    assert(((void*(__cdecl*)(DWORD, DWORD))AfSlot(AF_MONEY))(1, 0) == g_asEmptyStr);
    // docs/125: slot position and Query_GetObjects are learned keys: the first call misses (struct untouched, null array)
    float sp[3] = {3, 3, 3};
    assert(((AfSlotPosFn)AfSlot(AF_SLOTPOS))(sp, 3, 5, 2, 1, 0) == sp && sp[0] == 3 && AsAborted() == AS_AB_SLOTPOS);
    AsResetAbort();
    assert(!((AfQueryFn)AfSlot(AF_QUERY))(1) && AsAborted() == AS_AB_QUERY);
    AsResetAbort();
    assert(!((AfQueryFn)AfSlot(AF_QUERY))(9) && AsAborted() == AS_AB_QUERY);
    AsResetAbort();
    // T4: F2 at the fake ParseFormatSpecifier: abort fmt, exception out, body not run
    void* exc = NULL;
    ((void(__cdecl*)(DWORD, void**))g_afFmtFn)(0, &exc);
    assert(exc == g_asFmtExc && AsAborted() == AS_AB_FMT);
    AsResetAbort();
    // T4: stop from the sim: the next guard call abandons the run
    InterlockedExchange(&g_asStop, 1);
    assert(((AsU64(__cdecl*)(DWORD, DWORD))AfSlot(AF_PARENT))(3, 5) == 0 && AsAborted() == AS_AB_STOP);
    AsResetAbort();
    AsWorkerEnd();
    assert(g_asHeld == -1);
    return 0;
}

// docs/125: learned slot and query keys answer as the body does at the cut; a world-wide type over the cap aborts
static DWORD WINAPI AfWorker2(void*) {
    g_asWorkerTid = GetCurrentThreadId();
    AsWorkerBegin();
    float sp[3] = {0, 0, 0}, want[3];
    assert(((AfSlotPosFn)AfSlot(AF_SLOTPOS))(sp, 3, 5, 2, 1, 0) == sp && !memcmp(sp, AfSlotPos(want, 3, 5, 2, 1, 0), 12));
    const DWORD* q = ((AfQueryFn)AfSlot(AF_QUERY))(1);
    const DWORD* f = AfQuery(1);
    assert(q && q != f && q[0] == f[0] && q[2] == f[2] && f[2] > 0 && !memcmp(q + 4, f + 4, f[2] * 4));
    assert(AsAborted() == -1);
    assert(!((AfQueryFn)AfSlot(AF_QUERY))(9) && AsAborted() == AS_AB_QUERY);
    AsResetAbort();
    AsWorkerEnd();
    return 0;
}

// ---- T5 (part 2, docs/122 §14): request -> cut -> reference (RNG restored) -> worker -> finish on the M5 worker of
// TestMonoThreadM5 (its fakes: GC thread table, domain, tracking flag), plus overrun/stop, epoch drop, Mode 0, simstale,
// the rooted ranges, the IC registration and the JSONL log. Methods are fake ids handled by AfT5Invoke.
static float g_t5RefPos[3], g_t5WorkPos[3];
static volatile LONG g_t5Block, g_t5WorkRuns, g_t5WorkStale, g_t5Finish, g_t5Last = -9, g_t5SawStop;
static int g_t5Ics, g_t5Roots;
static u8 g_t5Host[0x1800];
static u8* g_t5HostPtr = g_t5Host;
static double g_t5R[4], g_t5W[3];  // docs/125: draws per unit in the reference and on the worker
static volatile LONG g_t5WNoUnit;
static double AfT5Draw() { return ((double(__cdecl*)())AfSlot(AF_RAND))(); }
static void __cdecl AfT5AddIc(const char* name, const void*) { g_t5Ics += !strncmp(name, "Ts3Fixes.AutShadowNative::", 26); }
static void __cdecl AfT5AddRoots(void* lo, void* hi) { g_t5Roots++; assert((u8*)hi - (u8*)lo >= (int)sizeof(DWORD) * AS_POOL); }
static void* __cdecl AfT5Malloc(size_t n) { return calloc(1, n); }
static void* __cdecl AfT5Invoke(void* method, void*, void**, void** exc) {
    *exc = NULL;
    float* pos = (uintptr_t)method == 2 ? g_t5RefPos : g_t5WorkPos;
    switch ((uintptr_t)method) {
        case 1: g_t5Last = AsIcLastRun(); InterlockedIncrement(&g_t5Finish); break;
        case 2: g_t5Host[0xC30 + 7] ^= 0x5A;  // the reference draws: the native side restores the MT state
        // fall through
        case 3:
            if ((uintptr_t)method == 3) {
                assert(AsIcIsWorker());
                for (int i = 0; i < 4000 && g_t5Block && !AsIcStopped(); i++) Sleep(1);
                if (AsIcStopped()) { InterlockedIncrement(&g_t5SawStop); break; }
            } else {
                assert(!AsIcIsWorker());
            }
            ((float*(__cdecl*)(float*, DWORD, DWORD))AfSlot(AF_POS))(pos, 5, 5);
            if (g_afRandMt && (uintptr_t)method == 2) {  // units 3 then 1; unit 1 draws once more (a tooltip-only draw)
                AsIcRngUnit(3);
                AsIcRngPause(1), AfT5Draw(), AsIcRngPause(0);  // addendum §4.1: a hooked call's draw before the unit's first
                g_t5R[0] = AfT5Draw(), g_t5R[1] = AfT5Draw();
                AsIcRngUnit(1);
                g_t5R[2] = AfT5Draw();
                AsIcRngPause(1), AsIcRngPause(1), AfT5Draw(), AsIcRngPause(0), AfT5Draw(), AsIcRngPause(0);  // nested, mid-unit
                g_t5R[3] = AfT5Draw();
            } else if (g_afRandMt) {  // worker: a draw outside any unit aborts; then units in the other order
                AsIcResetAbort();
                AfT5Draw();
                if (AsIcAborted() == AS_AB_RNG) InterlockedIncrement(&g_t5WNoUnit);
                AsIcResetAbort();
                AsIcRngUnit(1);
                g_t5W[2] = AfT5Draw();
                AsIcRngUnit(3);
                g_t5W[0] = AfT5Draw(), g_t5W[1] = AfT5Draw();
                assert(AsIcAborted() == -1);
            }
            if ((uintptr_t)method == 3) {
                AsIcResetAbort();
                float p7[3];
                ((float*(__cdecl*)(float*, DWORD, DWORD))AfSlot(AF_POS))(p7, 7, 5);  // listed as an unreachable Sim
                if (AsIcAborted() == AS_AB_SIMSTALE) InterlockedIncrement(&g_t5WorkStale);
                AsIcResetAbort();
                InterlockedIncrement(&g_t5WorkRuns);
            }
            break;
    }
    return NULL;
}
static void AfT5Request(int id) {
    assert(AsIcBegin());
    AsIcAddRow(5, 5, 1);
    AsIcAddRow(7, 5, 2);
    AsIcPost(id);
    assert(!AsIcBegin());  // a request is pending: busy
}
static void TestAutShadowRun() {
    Ts3nAutShadow& k = ts3native_autshadow;
    Ts3nMtProbe& m = ts3native_mtprobe;
    g_asAddIc = AfT5AddIc, g_asAddRoots = AfT5AddRoots, g_asGcMalloc = AfT5Malloc, g_asHostPtr = &g_t5HostPtr;
    g_asMtInit = AfMtInit, g_asMtNext = AfMtNext, g_asDetFlag = &g_afDet;
    AsInstallRun(NULL, true);
    assert(g_mtOnEnter == AsSimEnter && g_mtOnLeave == AsSimLeave && g_mtJobHook == AsJobHook && g_asMode == 0);
    // ICs register at the first Simulate entry; Begin refuses until the methods are set
    AsSimEnter();
    assert(g_t5Ics == 16 && k.icRegistered == 16 && AsIcVersion() == 4 && AsIcConfig(1) == 4 && AsIcConfig(2) == 20);
    assert(!AsIcBegin());
    AsIcSetMethods((void*)1, (void*)2, (void*)3);
    const DWORD emptyStr[4] = {0x51, 0, 0, 0};
    AsIcSetRefs((void*)emptyStr, (void*)0xE0E0);
    assert(g_as->keep[0] == (DWORD)(uintptr_t)emptyStr && (k.rooted & 0xC) == 0xC);
    for (int i = 0; i < 0xC30 + AS_RNG_LEN; i++) g_t5Host[i] = (u8)(i * 7);

    // Mode 0: cut and finish at once, no reference, no worker
    g_mtInvoke = AfT5Invoke;
    AfT5Request(40);
    AsSimLeave();
    assert(g_t5Ics == 16 && g_t5Roots == 4 && (k.rooted & 3) == 3 && g_asRefsRooted);
    assert(k.cutReq == 1 && k.refRuns == 0 && k.noWorker == 1 && g_t5Finish == 1 && g_t5Last == AS_LAST_NOWORKER && g_asRun == AS_RUN_IDLE);

    // Mode 1 on the M5 worker
    m.p3Ready = 7, m.p4Ready = 0xF;
    ts3native_counters.mtMode = 1;
    MtM5Tick();
    MtWaitFor(&m.workerState, 2);
    g_asMode = 1;
    assert(AsIcConfig(4) == 1);
    g_afRandMt = 1;
    AfT5Request(41);
    const u8 rng7 = g_t5Host[0xC30 + 7];
    AsSimLeave();  // cut, reference (sim thread), worker submitted
    assert(k.refRuns == 1 && k.rngDraws == 1 && g_t5Host[0xC30 + 7] == rng7 && k.workRuns == 1 && g_asRun == AS_RUN_WINDOW);
    assert(g_asOrig[AF_RAND] == (void*)AfRand && !g_asInRef && g_asRngSeedV[g_asPub]);
    MtWaitFor(&g_mtBusy, 0);
    assert(g_t5WorkRuns == 1 && g_t5WorkStale == 1 && !memcmp(g_t5RefPos, g_t5WorkPos, 12) && g_asHeld == -1);
    // docs/125: per-unit draws equal in the reference and on the worker, in any unit order, with an extra draw in one unit
    assert(g_t5WNoUnit == 1 && !memcmp(g_t5R, g_t5W, sizeof(g_t5W)) && g_t5R[0] != g_t5R[1] && g_t5R[0] != g_t5R[2]);
    assert(k.rngPause == 2 && k.rngPauseDraws == 3 && g_asRngPause == 0);
    AsIcRngPause(1);  // outside the reference: nothing
    assert(k.rngPause == 2 && g_asRngPause == 0);
    {  // v4 abort reasons from the managed hooks: H11 rain, null posture list; past the end = other
        const LONG other = k.abort[AS_AB_OTHER];
        AsManagedAbort(AS_AB_RAIN);
        assert(AsAborted() == AS_AB_RAIN && k.abort2[0] == 1);
        AsResetAbort();
        AsManagedAbort(AS_AB_POSTURE);
        AsManagedAbort(AS_AB_N);
        assert(AsAborted() == AS_AB_POSTURE && k.abort2[1] == 1 && k.abort[AS_AB_OTHER] == other + 1);
        AsResetAbort();
    }
    {
        AsRet rr = {0, 0, 0};
        g_asHeld = g_asPub, g_asRngUnitW = 0, g_asRngLiveW = false, g_afDet = 1;  // `Deterministic` on: 0.5, no MT use
        assert(AsRngWorker(AF_RAND, &rr) == 1 && rr.f == 0.5 && !g_asRngLiveW);
        g_asHeld = -1, g_afDet = 0;
    }
    AsSimEnter();  // window closes, the job had ended: no overrun
    assert(k.overrun == 0 && g_asRun == AS_RUN_WAIT && k.windowUs >= 0);
    AsSimLeave();  // finish
    assert(g_t5Finish == 2 && g_t5Last == AS_LAST_OK && g_asRun == AS_RUN_IDLE && g_asReqId == 0);
    // the stale Sim the worker read is refreshed from the next cut on: a second run reads it
    AfT5Request(42);
    AsSimLeave();
    MtWaitFor(&g_mtBusy, 0);
    assert(g_t5WorkRuns == 2 && g_t5WorkStale == 1);
    AsSimEnter();
    AsSimLeave();
    assert(g_t5Finish == 3 && g_t5Last == AS_LAST_OK);

    // overrun: the worker is still inside at the next Simulate entry -> stop, A5 status
    g_t5Block = 1;
    AfT5Request(43);
    AsSimLeave();
    AsSimEnter();
    assert(k.overrun == 1 && g_asStop == 1);
    MtWaitFor(&g_mtBusy, 0);
    assert(g_t5SawStop == 1);
    AsSimLeave();
    assert(g_t5Finish == 4 && g_t5Last == AS_LAST_STOPPED);
    // epoch between cut and the next Simulate entry: dropped (A5), stop set
    AfT5Request(44);
    AsSimLeave();
    AsEpochBump();
    AsSimEnter();
    assert(k.epochDrops == 1 && g_asStop == 1);
    MtWaitFor(&g_mtBusy, 0);
    AsSimLeave();
    assert(g_t5Finish == 5 && g_t5Last == AS_LAST_EPOCH);
    g_t5Block = 0;
    // the worker refuses the job while EA memory tracking is on
    g_mtFakeTrack = 1;
    AfT5Request(45);
    AsSimLeave();
    MtWaitFor(&g_mtBusy, 0);
    AsSimEnter();
    AsSimLeave();
    assert(g_t5Finish == 6 && g_t5Last == AS_LAST_REFUSED && k.workRefused == 1);
    g_mtFakeTrack = 0;
    ts3native_counters.mtMode = 0;
    MtM5Tick();
    assert(!g_mtWorker && m.workerStopFails == 0);
    // no idle worker in Mode 1: reference only, then finish
    AfT5Request(46);
    AsSimLeave();
    assert(g_t5Finish == 7 && g_t5Last == AS_LAST_NOWORKER && k.refRuns == 6 && k.rngDraws == 6);

    // AutShadowNative.Log -> autshadow.jsonl
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf_s(g_asJsonlPath, L"%sts3native-selftest-autshadow.jsonl", tmp);
    DeleteFileW(g_asJsonlPath);
    AsFakeStr line = AfStr(L"{\"a\":1}");
    AsIcLog((const DWORD*)&line);
    AsIcLog((const DWORD*)&line);
    CloseHandle(g_asJsonl);
    g_asJsonl = INVALID_HANDLE_VALUE;
    char buf[64] = "";
    FILE* f = NULL;
    assert(!_wfopen_s(&f, g_asJsonlPath, L"rb") && f);
    const size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    DeleteFileW(g_asJsonlPath);
    assert(got == 16 && !strcmp(buf, "{\"a\":1}\n{\"a\":1}\n") && k.logLines == 2);
    // the GC_malloc array allocator: MonoArray header (vtable, max_length), zeroed elements
    DWORD* arr = (DWORD*)AsNewArrayGc(0xA0001, 3);
    assert(arr && arr[0] == 0xA0001 && arr[2] == 3 && arr[4] == 0 && arr[6] == 0);
    free(arr);
    g_mtOnEnter = NULL, g_mtOnLeave = NULL, g_mtJobHook = NULL, g_asMode = -1, g_asWorkerTid = 0, g_afRandMt = 0;
    printf("autshadow run: %ld requests, %ld cuts for them, %ld reference runs (%ld RNG restores), %ld worker runs, %ld overrun, %ld epoch drop\n",
           k.req, k.cutReq, k.refRuns, k.rngDraws, k.workRuns, k.overrun, k.epochDrops);
}

// Ledger probe (autledger.cpp, docs/51 mono ledger): the four watched ICs on their own fake table, scope by task id,
// class and frame records, the Simulate tick read, the Sleep probe marks; every answer passes through unchanged.
static double __cdecl AfLgRand() { return 0.5; }
static float* __cdecl AfLgSlot(float* ret, DWORD, DWORD, DWORD, DWORD, DWORD) { ret[0] = 1, ret[1] = 2, ret[2] = 3; return ret; }
static DWORD __cdecl AfLgObjs(DWORD type) { return type + 1; }
static DWORD __cdecl AfLgRange(DWORD type, float, float, float, float r) { return type + (DWORD)r; }
static void* g_lgFakeTab[] = {
    (void*)AfLgRand, (void*)"ScriptCore.Random::Random_NextDoubleImpl",
    (void*)AfLgSlot, (void*)"ScriptCore.Slots::Slots_GetPositionOfSlotImpl",
    (void*)AfLgObjs, (void*)"ScriptCore.Queries::Query_GetObjects",
    (void*)AfLgRange, (void*)"ScriptCore.Queries::Query_GetObjectsInRange",
    NULL, NULL};
static DWORD g_lgInfo0[3] = {0, 0, 0x111}, g_lgInfo1[3] = {0, 0, 0x222};
static DWORD g_lgFrame1[2] = {0, (DWORD)(uintptr_t)g_lgInfo1};
static DWORD g_lgFrame0[2] = {(DWORD)(uintptr_t)g_lgFrame1, (DWORD)(uintptr_t)g_lgInfo0};
static DWORD g_lgCtxOk[3] = {0, 0, (DWORD)(uintptr_t)g_lgFrame0}, g_lgCtxBad[3] = {0, 0, 0x10};
static const DWORD* g_lgCtxNow = g_lgCtxOk;
static const u8* __cdecl AfLgCtx() { return (const u8*)g_lgCtxNow; }

static void TestAutLedger() {
    Ts3nAutShadow& k = ts3native_autshadow;
    assert(offsetof(Ts3nAutShadow, lgOn) == 0x13C && offsetof(Ts3nAutShadow, lgIn) == 0x158 && offsetof(Ts3nAutShadow, lgSleep) == 0x17C &&
           offsetof(Ts3nAutShadow, lgCls) == 0x1A0 && offsetof(Ts3nAutShadow, lgCall) == 0x260 && offsetof(Ts3nAutShadow, abort2) == 0x760 && offsetof(Ts3nAutShadow, rngPauseDraws) == 0x76C && sizeof(Ts3nAutShadow) == 0x770);  // registry v3, v4
    assert(AsIcConfig(5) == 0 && AsIcConfig(7) == 0 && AsLgWatchOf("ScriptCore.Queries::Query_GetObjects") < 0);  // Ledger=0: inert
    g_asLedger = true;
    const u8* base = (const u8*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    DWORD textLo = 0, textHi = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections && !textLo; i++)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) textLo = (DWORD)(uintptr_t)base + sec[i].VirtualAddress, textHi = textLo + sec[i].Misc.VirtualSize;
    const DWORD imgLo = (DWORD)(uintptr_t)base, imgHi = imgLo + nt->OptionalHeader.SizeOfImage;
    const int n0 = g_asIcN, nTab = (int)(sizeof(g_lgFakeTab) / sizeof(g_lgFakeTab[0]));
    AsInstallGuards((DWORD*)g_lgFakeTab, (DWORD*)g_lgFakeTab + nTab, imgLo, imgHi, textLo, textHi, false);
    assert(g_asIcN == n0 + 4);
    for (int i = 0; i < 4; i++) {
        const u8* t = g_asThunks + (n0 + i) * 10;
        assert(g_asLgWatch[n0 + i] == i + 1 && g_lgFakeTab[i * 2] == (void*)t);
        assert((DWORD)(uintptr_t)t + 10 + *(const LONG*)(t + 6) == (DWORD)(uintptr_t)AsLedgerEntry);
    }
    double(__cdecl* rnd)() = (double(__cdecl*)())g_lgFakeTab[0];
    float*(__cdecl* slot)(float*, DWORD, DWORD, DWORD, DWORD, DWORD) = (float*(__cdecl*)(float*, DWORD, DWORD, DWORD, DWORD, DWORD))g_lgFakeTab[2];
    DWORD(__cdecl* objs)(DWORD) = (DWORD(__cdecl*)(DWORD))g_lgFakeTab[4];
    DWORD(__cdecl* range)(DWORD, float, float, float, float) = (DWORD(__cdecl*)(DWORD, float, float, float, float))g_lgFakeTab[6];
    const u8*(__cdecl* ctx0)() = g_lgCtx;
    g_lgCtx = AfLgCtx;
    AsU64& task = *(AsU64*)(g_t5Host + 0xBD0);
    task = 7;
    const int top = AsFpTop();
    // outside every scope: counted, answered by the body
    assert(rnd() == 0.5 && k.lgOut[0] == 1 && k.lgIn[0] == 0 && AsFpTop() == top);
    assert(AsIcConfig(7) == 1 && AsIcConfig(5) == 1 && k.lgScopes == 1 && (k.lgOn & 4));
    assert(rnd() == 0.5 && k.lgIn[0] == 1 && k.lgCall[0][0] == 0 && k.lgCall[0][1] == 1 && k.lgCall[0][2] == 0x111 &&
           k.lgCall[0][3] == 0x222 && k.lgCall[0][4] == 0 && (k.lgOn & 8) && (k.lgOn & 16));
    float out[3] = {0, 0, 0};
    assert(slot(out, 9, 5, 1, 2, 0) == out && out[0] == 1 && out[2] == 3);
    slot(out, 10, 5, 1, 2, 0);
    slot(out, 9, 5, 1, 2, 0);
    assert(k.lgIn[1] == 3 && k.lgSlotIds == 2);
    static DWORD klass[16];
    static DWORD type[2] = {(DWORD)(uintptr_t)klass, 0x12};
    static DWORD refl[3] = {0x51, 0, (DWORD)(uintptr_t)type};
    const DWORD r = (DWORD)(uintptr_t)refl;
    assert(objs(r) == r + 1 && objs(r) == r + 1 && range(r, 1, 2, 3, 4) == r + 4);
    assert(k.lgCls[0][0] == 2 && (DWORD)k.lgCls[0][1] == (DWORD)(uintptr_t)klass && k.lgCls[0][2] == 2);
    assert(k.lgCls[1][0] == 3 && k.lgCls[1][2] == 1 && k.lgIn[2] == 2 && k.lgIn[3] == 1);
    assert(k.lgCall[1][0] == 1 && k.lgCall[1][1] == 3 && k.lgCall[2][0] == 2 && k.lgCall[3][0] == 3);
    // another task runs while B13 sleeps: outside
    task = 8;
    rnd();
    assert(k.lgOut[0] == 2 && k.lgIn[0] == 1);
    // nested open and close on the scope's task
    task = 7;
    assert(AsIcConfig(5) == 1 && k.lgScopes == 1);
    AsIcConfig(6);
    rnd();
    assert(k.lgIn[0] == 2 && k.lgCall[0][1] == 2);
    // a bad frame chain drops the record, never the call
    g_lgCtxNow = g_lgCtxBad;
    assert(rnd() == 0.5 && k.lgIn[0] == 3 && k.lgDropped == 1);
    g_lgCtxNow = g_lgCtxOk;
    AsIcConfig(6);
    rnd();
    assert(k.lgOut[0] == 3 && k.lgScopeUs >= 0 && k.lgScopeFull == 0);
    AsIcConfig(6);
    assert(k.lgScopeFull == 1);
    // Simulate ticks: the EA memory tracking word
    volatile LONG track = 0;
    volatile LONG* track0 = g_lgTrack;
    g_lgTrack = &track;
    AsSimEnter();
    track = 3;
    AsLgTick();
    track = 0;
    AsLgTick();
    assert(k.lgTicks == 3 && k.lgTrackTicks == 1 && k.lgTrackLast == 3 && (k.lgOn & 2) && AsIcConfig(8) == 3);
    // Sleep probe: ticks between two marks of a series
    assert(AsIcConfig(9) == -1);
    AsLgTick();
    assert(AsIcConfig(9) == 1 && AsIcConfig(9) == 0);
    assert(AsIcConfig(10) == -1);
    AsLgTick(), AsLgTick(), AsLgTick(), AsLgTick();
    assert(AsIcConfig(10) == 4);
    assert(k.lgSleep[0] == 1 && k.lgSleep[1] == 1 && k.lgSleep[2] == 0 && k.lgSleep[7] == 1);
    g_lgTrack = track0, g_lgCtx = ctx0, g_asLedger = false;
    task = 0;
    printf("autshadow ledger: in %ld/%ld/%ld/%ld, out %ld, dropped %ld, ticks %ld, sleep(0) %ld/%ld, sleep(1) 3+ %ld\n", k.lgIn[0],
           k.lgIn[1], k.lgIn[2], k.lgIn[3], k.lgOut[0], k.lgDropped, k.lgTicks, k.lgSleep[0], k.lgSleep[1], k.lgSleep[7]);
}

static void TestAutShadow() {
    Ts3nAutShadow& k = ts3native_autshadow;
    assert(offsetof(Ts3nAutShadow, abort) == 0x7C && offsetof(Ts3nAutShadow, mode) == 0xCC && offsetof(Ts3nAutShadow, lgOn) == 0x13C);  // registry v1, v2 (v3: TestAutLedger)
    assert(AsKindOfName("ScriptCore.World::World_GetSunriseTime") == 'f' && AsKindOfName("ScriptCore.Random::Random_NextDoubleImpl") == 'd' &&
           AsKindOfName("ScriptCore.Objects::Objects_GetPosition") == 's' && AsKindOfName("ScriptCore.Fake::Fake_UnlistedImpl") == 0);
    for (int i = 1; i < (int)(sizeof(kAsKindNames) / sizeof(kAsKindNames[0])); i++) assert(strcmp(kAsKindNames[i - 1], kAsKindNames[i]) < 0);
    assert(AsAlloc());
    // install on the fake table (this image's .text and bounds)
    const u8* base = (const u8*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    DWORD textLo = 0, textHi = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections && !textLo; i++)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) textLo = (DWORD)(uintptr_t)base + sec[i].VirtualAddress, textHi = textLo + sec[i].Misc.VirtualSize;
    const DWORD imgLo = (DWORD)(uintptr_t)base, imgHi = imgLo + nt->OptionalHeader.SizeOfImage;
    const int nTab = (int)(sizeof(g_asFakeTab) / sizeof(g_asFakeTab[0]));
    assert(AsInstallGuards((DWORD*)g_asFakeTab, (DWORD*)g_asFakeTab + nTab, imgLo, imgHi, textLo, textHi, true) == 17);
    assert(k.descIcs == 14 && g_asIcParent == 1 && g_asIcRng == AF_RAND && (k.ready & 1) && g_asFakeTab[30] == (void*)0x12345678);
    assert(g_asOrig[AF_POS] == (void*)AfPos && g_asKind[AF_SUNRISE] == 'f' && g_asKind[AF_CAMPOS] == 's');
    g_afFmtFn = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(g_afFmtFn, g_afFmt, 16);
    g_afFmtFn[16] = 0xE5, g_afFmtFn[17] = 0x5D, g_afFmtFn[18] = 0xC3;
    assert(AsInstallFmt((DWORD)(uintptr_t)g_afFmtFn) && (k.ready & 2));
    void* exc = NULL;
    ((void(__cdecl*)(DWORD, void**))g_afFmtFn)(0, &exc);  // not the worker: the body runs
    assert(exc == (void*)0x77);
    // change tracking: world event 7 registers our object-event nodes once
    DWORD* heads = g_snObjHeads;
    SnObjRegFn reg = g_snObjReg;
    g_snObjHeads = g_afObjHeads, g_snObjReg = AfObjReg;
    AsWorldEvent(7);
    AsWorldEvent(7);
    assert(g_afNodeN == 8 && (k.ready & 0xC) == 0xC && g_asTracked);
    assert(SnObjListed(4, (void*)AsObjEvList) && SnObjListed(0x21, (void*)AsObjEvDirty) && !SnObjListed(0x21, (void*)AsObjEvList));
    g_snObjHeads = heads, g_snObjReg = reg;

    // fake world: 64 objects on 4 lots
    for (int i = 1; i <= 64; i++) {
        AsFakeObj& o = g_afo[i];
        o.id = AfId(i), o.local[0] = (float)i, o.local[1] = 0, o.local[2] = (float)(i % 5);
        o.parent = i > 8 && i % 3 == 0 ? AfId(i / 3) : 0, o.hidden = i % 7 == 0, o.lot = 1 + i % 4;
    }
    for (int i = 1; i <= 32; i++) AsAddRow(AfId(i), i == 1);
    const DWORD lk[4] = {3, 1, 0, 0}, lk2[4] = {3, 1, 0, 0};
    AsInsert(AF_ONLOT, lk, 4, 0);
    AsInsert(AF_COUNT, lk2, 4, 0);
    const float v[3] = {25.0f, 0, 7.0f};
    const DWORD ll[4] = {*(const DWORD*)&v[0], *(const DWORD*)&v[1], *(const DWORD*)&v[2], 2};
    AsInsert(AF_LOTLOC, ll, 4, 0);
    // the sim thread's own memo call is recorded on the pass-through
    AsFakeStr s1 = AfStr(L"Shadows");
    DWORD out = 0;
    assert(((char(__cdecl*)(const AsFakeStr*, DWORD*))AfSlot(AF_OPTION))(&s1, &out) == 1 && out == 7 * 3 + L'S' && k.memoRecords == 1);
    AsFakeStr s5 = AfStr(L"Glow");  // missing option: out left at the caller's 42, keyed on it
    out = 42;
    assert(((char(__cdecl*)(const AsFakeStr*, DWORD*))AfSlot(AF_OPTION))(&s5, &out) == 0 && out == 42 && k.memoRecords == 2);
    const LONG sim0 = g_asSimCalls;
    assert(((AsU64(__cdecl*)(DWORD, DWORD))AfSlot(AF_PARENT))(9, 5) == AfId(3) && g_asSimCalls == sim0 + 1);  // pass-through
    assert(((float(__cdecl*)())AfSlot(AF_SUNRISE))() == 6.0f && ((double(__cdecl*)())AfSlot(AF_RAND))() == 0.25);
    g_asAudit = 1;
    AsCut();
    assert(k.cuts == 1 && k.epochs == 1 && k.auditMis[0] == 0 && k.entries > 100);
    AfCheckPublished();

    // T1 + T4 on a second thread marked worker
    g_asRefsRooted = true, g_asNewArray = AfNewArray;
    static DWORD emptyStr[4] = {0x51, 0, 0, 0};
    g_asEmptyStr = emptyStr, g_asFmtExc = (void*)(uintptr_t)0xE0E0;
    HANDLE th = CreateThread(NULL, 0, AfWorker, NULL, 0, NULL);
    assert(th && WaitForSingleObject(th, 10000) == WAIT_OBJECT_0);
    CloseHandle(th);
    g_asWorkerTid = 0;
    // the worker's misses come back as learned keys at the next cut, then answer
    const LONG learned0 = k.learned;
    AsCut();
    assert(k.learned == learned0 + 5);  // row 5:40 (Objects_GetPosition), list (3, lot 2, flags 1), slot, query 1 and 9
    const DWORD rk[2] = {40, 5};
    assert(AsFind(g_as->v[g_asPub], AF_POS, rk, 2) >= 0 && AsFind(g_as->v[g_asPub], AF_PARENT, rk, 2) >= 0);  // + its parent entry
    // docs/125: the slot and query keys answer from this cut on and follow changes that fire no event (AO_CUT)
    for (int round = 0; round < 2; round++) {
        th = CreateThread(NULL, 0, AfWorker2, NULL, 0, NULL);
        assert(th && WaitForSingleObject(th, 10000) == WAIT_OBJECT_0);
        CloseHandle(th);
        g_asWorkerTid = 0;
        g_afSlotBias += 2.0f;  // no event
        g_afo[2].hidden ^= 1;
        AsObjEvDirty(&g_afo[2], NULL);
        AsCut();
        assert(k.auditMis[0] == 0);
    }

    // T2: incremental snapshot vs full rebuild on random event sequences (invariant S-A), seeds 1-4 x 3,000 steps
    for (DWORD seed = 1; seed <= 4; seed++) {
        DWORD x = seed * 2654435761u;
        auto rnd = [&x](DWORD n) { x = x * 1664525u + 1013904223u; return (x >> 8) % n; };
        for (int step = 0; step < 3000; step++) {
            const int i = 1 + (int)rnd(64);
            AsFakeObj& o = g_afo[i];
            switch (rnd(9)) {
                case 0: case 1: o.local[0] += 1.5f; AsObjEvDirty(&o, NULL); break;                    // 0x0E transform
                case 2: o.hidden ^= 1; AsObjEvDirty(&o, NULL); break;                                  // 0x18 visibility
                case 3: o.parent = i > 1 && rnd(2) ? AfId(1 + (int)rnd(i - 1)) : 0; AsObjEvDirty(&o, NULL); break;  // 0x21 parent
                case 4: o.lot = 1 + rnd(4); if (rnd(2)) AsObjEvList(&o, NULL); else { AsObjEvDirty(&o, NULL); AsWorldEvent(0x11); } break;
                case 5: g_afSunrise += 0.25f; break;                                             // per-cut constant: no event
                case 6: if (!rnd(8)) { g_afSea += 1; AsWorldEvent(5); } break;                   // world event: epoch
                case 7: { const DWORD key[2] = {(DWORD)(1 + rnd(64)), 5}; AsLearn(rnd(2) ? AF_POS : AF_TOMB, key, 2); } break;
                default: { const DWORD key[4] = {3 + rnd(2), 1 + rnd(4), 0, rnd(2)}; AsLearn(rnd(2) ? AF_ONLOT : AF_COUNT, key, 4); } break;
            }
            if (step % 7 == 6) {
                AsCut();
                assert(k.auditMis[0] == 0);
                if (step % 70 == 69) AfCheckPublished();
            }
        }
    }
    assert(k.full == 0 && k.dirtyOverflow == 0 && k.entries > 200);
    // the incremental cut really skips clean rows: with no parents, a clean cut refreshes only the Sim row's tomb entry
    for (int i = 1; i <= 64; i++) { g_afo[i].parent = 0; AsObjEvDirty(&g_afo[i], NULL); }
    AsCut();
    const int probe = g_afRefreshProbe;
    g_asAudit = 0;
    AsCut();
    assert(g_afRefreshProbe - probe == 1);
    // negative control: a change without its event is caught by the audit (S1) and nowhere else
    g_asAudit = 1;
    g_afo[30].hidden ^= 1;
    AsCut();
    assert(k.auditMis[0] >= 1);
    k.auditMis[0] = 0;
    AsObjEvDirty(&g_afo[30], NULL);
    AsCut();
    assert(k.auditMis[0] == 0);

    // A/B: a worker run that overruns keeps its version; two cuts later it is still intact
    g_asWorkerTid = GetCurrentThreadId();
    AsWorkerBegin();
    const LONG held = g_asHeld;
    const DWORD key5[2] = {5, 5};
    const int at = AsFind(g_as->v[held], AF_POS, key5, 2);
    assert(at >= 0);
    const AsEnt before = g_as->v[held].e[at];
    for (int c = 0; c < 2; c++) {
        g_afo[5].local[0] += 100;
        AsObjEvDirty(&g_afo[5], NULL);
        AsCut();
        assert(g_asPub != held);
    }
    assert(!memcmp(&g_as->v[held].e[at], &before, sizeof(AsEnt)));
    const int now = AsFind(g_as->v[g_asPub], AF_POS, key5, 2);
    assert(now >= 0 && memcmp(g_as->v[g_asPub].e[now].st, before.st, 12));
    AsWorkerEnd();
    g_asWorkerTid = 0;
    // a learned key nobody asks for again is dropped after 100 cuts
    const LONG dropped = k.learnDropped;
    for (int c = 0; c < 101; c++) AsCut();
    assert(k.learnDropped > dropped);
    printf("autshadow: %ld IC slots, %ld cuts (max %ld us), %ld entries, %ld audits, %ld aborts\n", k.guardSlots, k.cuts,
           k.cutMaxUs, k.entries, k.auditN, k.abortTotal);
    TestAutShadowRun();
    TestAutLedger();
}
