// TestEaCrashFixes ([EaCrashFixes] ConformActor + ThumbPose, eafix.cpp, docs/67 §26.4 / §27.5). Included by selftest.cpp.
// Fake AnimationComponents, controllers and refcounted owners; the patched ask sequence and the after-evaluation stub run
// for real on copies of the game's bytes.

static DWORD g_eaVts[3][8];  // [0] live AnimationComponent +8 stand-in, [1] another actor type, [2] slot +0x10 outside .text
static LONG g_eaAsks;
static u8* g_eaAskActor;
static bool __fastcall EaFakeAsk(u8* actor, void*, u8* ctl) {
    g_eaAsks++, g_eaAskActor = actor;
    return ctl[0x24] == 0;  // any answer the caller can see
}
static void* __fastcall EaFakeCtor(u8* comp, void*, void* owner) {
    *(void**)(comp + 4) = owner, *(DWORD*)(comp + 8) = (DWORD)(uintptr_t)g_eaVts[0];
    return comp;
}
static void __fastcall EaFakeDtor(u8* comp) { *(DWORD*)(comp + 8) = CA_ANIM_VT8; }  // EA's dtor leaves this vtable word

static LONG g_eaReleases, g_eaFreed;
static void __fastcall EaFakeRelease(u8** ref) {
    g_eaReleases++;
    if (*ref && --*(LONG*)(*ref + 4) == 0) g_eaFreed++;
}

static u8* g_eaStubObj;
static DWORD g_eaStubOut[5];  // ecx, ebx, esi, edi, eax after the stub
static void EaRunTpStub() {
    __asm {
        push ebx
        push esi
        push edi
        sub esp, 0x100
        mov eax, g_eaStubObj
        mov [esp + 0x28], eax
        lea ecx, [esp + 0x28]
        call TpDefer
        mov dword ptr [esp + 0xAC], 0x5A5A5A5A
        mov ebx, 0x11111111
        mov esi, 0x22222222
        mov edi, 0x33333333
        mov eax, 0x44444444
        call TpAfterStub
        mov g_eaStubOut[0], ecx
        mov g_eaStubOut[4], ebx
        mov g_eaStubOut[8], esi
        mov g_eaStubOut[12], edi
        mov g_eaStubOut[16], eax
        add esp, 0x100
        pop edi
        pop esi
        pop ebx
    }
}

static void TestEaCrashFixes() {
    static_assert(offsetof(Ts3nCounters, caOn) == 0x5480 && offsetof(Ts3nCounters, caSkip) == 0x5484 &&
                      offsetof(Ts3nCounters, caOther) == 0x5490 && offsetof(Ts3nCounters, caOverflow) == 0x549C &&
                      offsetof(Ts3nCounters, tpOn) == 0x54A0 && offsetof(Ts3nCounters, tpNoSlot) == 0x54AC &&
                      sizeof(Ts3nCounters) == 5460 * 4,
                  "counters v141 layout after v140 (docs/registry.md)");
    Ts3nCounters& k = ts3native_counters;
    assert(k.version == 145);

    // ---- ConformActor ----
    const DWORD rd0 = g_ecaRdLo, rd1 = g_ecaRdHi, tx0 = g_ecaTxLo, tx1 = g_ecaTxHi;
    g_ecaRdLo = (DWORD)(uintptr_t)g_eaVts, g_ecaRdHi = g_ecaRdLo + sizeof(g_eaVts);
    g_ecaTxLo = 0x10000, g_ecaTxHi = 0xFFFF0000;
    g_eaVts[0][4] = g_eaVts[1][4] = (DWORD)(uintptr_t)&EaFakeAsk;
    g_eaVts[2][4] = 0x10;
    const CaCtorFn ctor0 = g_ecaCtor;
    const CaDtorFn dtor0 = g_ecaDtor;
    g_ecaCtor = &EaFakeCtor, g_ecaDtor = &EaFakeDtor;
    if (!g_ecaSet) g_ecaSet = (DWORD*)VirtualAlloc(NULL, CA_CAP * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(g_ecaSet && !g_ecaLive);
    memset(k.caSkip, 0, sizeof(k.caSkip)), k.caOther = 0;

    alignas(8) static u8 comp[0x40], other[0x10], badSlot[0x10], ctl[0x48];
    static u8 owner[0x40];
    assert(CaCtorHook(comp, NULL, owner) == comp && CaHas((DWORD)(uintptr_t)comp) && k.caLive == 1);
    *(u8**)(ctl + 0x1C) = comp + 8, *(DWORD*)(ctl + 0x20) = 77;

    // live: the game's call, unchanged
    g_eaAsks = 0;
    assert(CaGuard(ctl) && g_eaAsks == 1 && g_eaAskActor == comp + 8);
    ctl[0x24] = 1;
    assert(!CaGuard(ctl) && g_eaAsks == 2);  // the actor's own "done" answer passes through
    ctl[0x24] = 0;
    // destroyed: answered "done", no call through it
    CaDtorHook(comp);
    assert(!CaHas((DWORD)(uintptr_t)comp) && k.caLive == 0);
    assert(!CaGuard(ctl) && g_eaAsks == 2 && k.caSkip[1] == 1);
    // the same address constructed again: live again
    CaCtorHook(comp, NULL, owner);
    assert(CaGuard(ctl) && g_eaAsks == 3);
    CaDtorHook(comp);
    // null actor, released page, clobbered vtable words (0x002B002B in #98, 0 in crash I)
    *(u8**)(ctl + 0x1C) = NULL;
    assert(!CaGuard(ctl) && k.caSkip[0] == 1);
    u8* page = (u8*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    VirtualFree(page, 0, MEM_RELEASE);
    *(u8**)(ctl + 0x1C) = page + 8;
    assert(!CaGuard(ctl) && k.caSkip[2] == 1);
    *(DWORD*)(comp + 8) = 0x002B002B, *(u8**)(ctl + 0x1C) = comp + 8;
    assert(!CaGuard(ctl) && k.caSkip[2] == 2);
    *(DWORD*)(comp + 8) = 0;
    assert(!CaGuard(ctl) && k.caSkip[2] == 3);
    // another actor type (vtable in .rdata, slot in .text): the game's call; slot outside .text: skipped
    *(DWORD*)other = (DWORD)(uintptr_t)g_eaVts[1], *(u8**)(ctl + 0x1C) = other;
    assert(CaGuard(ctl) && g_eaAsks == 4 && g_eaAskActor == other && k.caOther == 1);
    *(DWORD*)badSlot = (DWORD)(uintptr_t)g_eaVts[2], *(u8**)(ctl + 0x1C) = badSlot;
    assert(!CaGuard(ctl) && k.caSkip[2] == 4 && g_eaAsks == 4);

    // the patched ask sequence, run: mov eax,[esp+4]; <11 site bytes>; ret 4
    u8* code = (u8*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    static const u8 pre[4] = {0x8B, 0x44, 0x24, 0x04}, post[3] = {0xC2, 0x04, 0x00};
    memcpy(code, pre, 4), memcpy(code + 4, kCaAsk, 11), memcpy(code + 15, post, 3);
    typedef bool(__stdcall * AskSeq)(u8* ctl);
    const AskSeq run = (AskSeq)(void*)code;
    CaCtorHook(comp, NULL, owner);
    *(u8**)(ctl + 0x1C) = comp + 8;
    assert(run(ctl) && g_eaAsks == 5);  // the game's bytes
    assert(CaPatchAsk(code + 4) && code[4] == 0x50 && code[5] == 0xE8 && code[10] == 0x0F);
    assert(run(ctl) && g_eaAsks == 6);  // patched, live
    CaDtorHook(comp);
    assert(!run(ctl) && g_eaAsks == 6 && k.caSkip[1] == 2);  // patched, destroyed
    assert(!CaPatchAsk(code + 4));                            // ours now: not patched twice
    VirtualFree(code, 0, MEM_RELEASE);

    // set churn: tombstones force rebuilds; membership stays exact
    for (DWORD i = 0; i < 200000; i++) CaAdd(0x10000000 + i * 8), CaDel(0x10000000 + i * 8);
    for (DWORD i = 0; i < 1000; i++) CaAdd(0x20000000 + i * 0x40);
    for (DWORD i = 0; i < 1000; i += 2) CaDel(0x20000000 + i * 0x40);
    for (DWORD i = 0; i < 1000; i++) assert(CaHas(0x20000000 + i * 0x40) == ((i & 1) == 1));
    assert(!CaHas(0x10000008) && k.caLive == 500 && k.caMaxLive >= 500 && !g_ecaFull && g_ecaUsed < CA_CAP / 4 * 3);
    memset(g_ecaSet, 0, CA_CAP * 4), g_ecaUsed = g_ecaLive = 0, k.caLive = k.caMaxLive = 0;
    memset(k.caSkip, 0, sizeof(k.caSkip)), k.caOther = 0;
    g_ecaCtor = ctor0, g_ecaDtor = dtor0, g_ecaRdLo = rd0, g_ecaRdHi = rd1, g_ecaTxLo = tx0, g_ecaTxHi = tx1;

    // ---- ThumbPose ----
    const TpReleaseFn rel0 = g_tpRelease;
    g_tpRelease = &EaFakeRelease;
    k.tpHeld = k.tpLastRef = k.tpNoSlot = 0;
    alignas(8) static u8 obj[0x20];
    u8* frame[0x40] = {};  // stands in for the render's stack: the RefPtr local sits at frame+0x28
    u8** local = (u8**)((u8*)frame + 0x28);
    const DWORD f = (DWORD)(uintptr_t)frame;
    // last reference: kept across "the evaluation", released after it
    *(LONG*)(obj + 4) = 1, *local = obj, g_eaReleases = g_eaFreed = 0;
    TpDefer(local);
    assert(g_eaReleases == 0 && *(LONG*)(obj + 4) == 1 && k.tpHeld == 1);
    TpAfter(f + 0x10);  // another frame: nothing
    assert(g_eaReleases == 0);
    TpAfter(f);
    assert(g_eaReleases == 1 && g_eaFreed == 1 && k.tpLastRef == 1);
    TpAfter(f);  // branch (b) / (c) next time: nothing kept, nothing released
    assert(g_eaReleases == 1);
    // shared owner: released after, not counted as last
    *(LONG*)(obj + 4) = 2;
    TpDefer(local), TpAfter(f);
    assert(g_eaReleases == 2 && *(LONG*)(obj + 4) == 1 && k.tpLastRef == 1 && k.tpHeld == 2);
    // empty RefPtr: nothing to keep
    *local = NULL;
    TpDefer(local);
    assert(k.tpHeld == 2 && g_eaReleases == 2);
    // every slot taken by other threads: released at once (the game's order)
    TpSlot saved[8];
    memcpy(saved, g_tp, sizeof(g_tp));
    for (int i = 0; i < 8; i++) g_tp[i].tid = 0x7FFF0000 + i, g_tp[i].frame = 0, g_tp[i].obj = obj;
    *(LONG*)(obj + 4) = 2, *local = obj;
    TpDefer(local);
    assert(k.tpNoSlot == 1 && g_eaReleases == 3 && *(LONG*)(obj + 4) == 1);
    memcpy(g_tp, saved, sizeof(g_tp));
    // the naked stub on a real stack: releases the frame's reference, keeps every register, runs the moved instruction
    *(LONG*)(obj + 4) = 1, g_eaStubObj = obj;
    EaRunTpStub();
    assert(g_eaReleases == 4 && g_eaFreed == 2 && k.tpLastRef == 2);
    assert(g_eaStubOut[0] == 0x5A5A5A5A && g_eaStubOut[1] == 0x11111111 && g_eaStubOut[2] == 0x22222222 &&
           g_eaStubOut[3] == 0x33333333 && g_eaStubOut[4] == 0x44444444);
    for (const TpSlot& s : g_tp) assert(!s.obj);
    // patch on dummies laid out like the two sites
    alignas(4) static u8 defer[8], after[8];
    defer[0] = 0xE8, *(LONG*)(defer + 1) = (LONG)(TP_RELEASE - ((uintptr_t)defer + 5));
    memcpy(after, kTpAfter, 7);
    u8 dOrig[8], aOrig[8];
    memcpy(dOrig, defer, 8), memcpy(aOrig, after, 8);
    after[3] = 0xA8;  // foreign bytes: nothing written anywhere
    assert(!TpPatch(defer, after) && !memcmp(defer, dOrig, 8));
    after[3] = 0xAC;
    assert(TpPatch(defer, after));
    assert(defer[0] == 0xE8 && (uintptr_t)defer + 5 + *(LONG*)(defer + 1) == (uintptr_t)&TpDefer);
    assert(after[0] == 0xE8 && (uintptr_t)after + 5 + *(LONG*)(after + 1) == (uintptr_t)&TpAfterStub && after[5] == 0x66 && after[6] == 0x90);
    assert(!TpPatch(defer, after));  // ours now
    g_tpRelease = rel0;
    k.tpHeld = k.tpLastRef = k.tpNoSlot = 0;
    printf("EaCrashFixes: ConformActor live call unchanged, destroyed / null / freed / clobbered actors skipped, other types "
           "called, patched sequence run; ThumbPose reference kept across the evaluation and released by the stub: ok\n");
}
