// TestStiltRemove ([StiltRemove], docs/858): a fake key function with EA's formula, the import and load key paths on fake
// frames, and StPatch on buffers holding the expected bytes; PlaceKeep's defer / end stubs on a fake place frame and
// PartClamp's index math (lots 10-64) and stubs; StPlacePatch / StPartPatch on buffers.

static DWORD __cdecl StFakeKey(const StPoint* p) {
    return ((DWORD)floor(p->z * 8.0 + 0.5) << 12 | (p->level & 0xFF) << 24) | (DWORD)floor(p->x * 8.0 + 0.5);
}

static uintptr_t StCallTarget(const u8* at) { return at[0] == 0xE8 ? (uintptr_t)at + 5 + *(const LONG*)(at + 1) : 0; }

// PlaceKeep fakes: 0xCA7E00 and 0xCA76B0 record their arguments
static struct { int n; DWORD lot, diag, recheck, undo, arg7; StCell p0, p1; } s_stRm;
static char __cdecl StFakeRemove(DWORD lot, const StCell* p0, const StCell* p1, DWORD diag, DWORD recheck, DWORD undo, DWORD arg7) {
    s_stRm.n++, s_stRm.lot = lot, s_stRm.p0 = *p0, s_stRm.p1 = *p1, s_stRm.diag = diag, s_stRm.recheck = recheck;
    s_stRm.undo = undo, s_stRm.arg7 = arg7;
    return 1;
}
static int s_stEndN;
static DWORD s_stEndArgs[2];
static void __cdecl StFakeEnd(DWORD lot, DWORD a) { s_stEndN++, s_stEndArgs[0] = lot, s_stEndArgs[1] = a; }

// 0xCAECA0 around the hooks: the 0x40-byte body frame with the cell write's result at +12h, the 7-argument call at
// 0xCAEDA9 (when defer), then `push 0; push lot; call` at 0xCAEF5F
static void StRunPlace(int ok, int defer, DWORD lot, const StCell* p0, const StCell* p1, DWORD diag, DWORD undo, DWORD arg7) {
    const DWORD d = (DWORD)(uintptr_t)&StPlaceDefer, e = (DWORD)(uintptr_t)&StPlaceEndStub;
    __asm {
        sub esp, 40h
        mov eax, ok
        mov byte ptr [esp + 12h], al
        cmp defer, 0
        je skip
        push arg7
        push undo
        push 0
        push diag
        push p1
        push p0
        push lot
        call d
        add esp, 1Ch
    skip:
        push 0
        push lot
        call e
        add esp, 8
        add esp, 40h
    }
}

// a partition stub called as 0xC1CF80 / 0xC1C120 call it: [esp+10h] = the floor value, ecx = edx = the manager
struct StRegs { DWORD a, c, d, s; };
static StRegs StRunPartStub(void (*stub)(), int v, const void* mgr) {
    StRegs r = {};
    __asm {
        push esi
        mov eax, v
        sub esp, 20h
        mov [esp + 10h], eax
        mov ecx, mgr
        mov edx, ecx
        mov esi, 5E5E5E5Eh
        mov eax, 0A0A0A0A0h
        call stub
        add esp, 20h
        mov r.a, eax
        mov r.c, ecx
        mov r.d, edx
        mov r.s, esi
        pop esi
    }
    return r;
}

// PlaceKeep (docs/858 §5) and PartClamp (§6)
static void TestStiltPlaceClamp() {
    Ts3nCounters& k = ts3native_counters;

    // (d) PartClamp math: EA's partitions per side ((side-1)>>3)+1 (0xC1F7D5); every grid point 0..side lands in one the
    // array has, and only the far edge of a side that is a multiple of 8 is clamped
    k.srClamped = 0;
    LONG want = 0;
    const int sides[] = {10, 15, 30, 40, 45, 63, 64};
    for (int s : sides) {
        const DWORD n = ((DWORD)(s - 1) >> 3) + 1;
        for (int v = 0; v <= s; v++) {
            const DWORD ea = (DWORD)v >> 3, i = StPartIdx(v, n, 1);
            assert(i < n && i == (ea < n ? ea : n - 1));
            want += ea >= n;
        }
    }
    assert(want == 2 && k.srClamped == 2);  // x = 40 on 40 and x = 64 on 64
    assert(StPartIdx(40, 5, 0) == 4 && StPartIdx(39, 5, 0) == 4 && StPartIdx(64, 8, 2) == 7 && StPartIdx(-1, 5, 3) == 0);
    assert(StPartIdx(7, 0, 0) == 0 && k.srClamped == 5);  // no partitions: EA's index, not counted
    for (int z = 0; z <= 64; z++)  // a 40 x 64 lot: every grid point inside the rows * cols array
        for (int x = 0; x <= 40; x++) assert(StPartIdx(z, 8, 0) * 5 + StPartIdx(x, 5, 1) < 5 * 8);
    assert(k.srClamped == 5 + 41 + 65);  // row 8 for the 41 points at z = 64, column 5 for the 65 at x = 40
    k.srClamped = 0;

    // (e) the four stubs: output register, live registers kept
    alignas(4) static u8 mgr[0x80];
    *(DWORD*)(mgr + 0x6C) = 5, *(DWORD*)(mgr + 0x70) = 8;  // 40 wide, 64 deep
    const DWORD m = (DWORD)(uintptr_t)mgr;
    StRegs r = StRunPartStub(StPartInsRow, 64, mgr);
    assert(r.c == 7 && r.d == m && r.s == 0x5E5E5E5E);
    r = StRunPartStub(StPartInsRow, 63, mgr);
    assert(r.c == 7 && r.d == m);
    r = StRunPartStub(StPartInsCol, 40, mgr);
    assert(r.a == 4 && r.c == m && r.d == m && r.s == 0x5E5E5E5E);
    r = StRunPartStub(StPartRemRow, 64, mgr);
    assert(r.s == 7 && r.c == m && r.d == m);
    r = StRunPartStub(StPartRemCol, 39, mgr);
    assert(r.a == 4 && r.c == m && r.d == m && r.s == 0x5E5E5E5E);
    assert(k.srClamped == 3);

    // (f) StPartPatch: both functions checked before anything is written; then four calls, the rest of the bytes as EA's
    static u8 ins[sizeof(kStPartIns)], rem[sizeof(kStPartRem)];
    memcpy(ins, kStPartIns, sizeof(ins));
    memcpy(rem, kStPartRem, sizeof(rem));
    rem[0x0E] = 0x70;  // imul esi,[ecx+70h]: another layout
    assert(!StPartPatch(ins, rem) && !memcmp(ins, kStPartIns, sizeof(ins)) && rem[0] == 0x8B);
    rem[0x0E] = 0x6C;
    assert(StPartPatch(ins, rem));
    assert(StCallTarget(ins) == (uintptr_t)&StPartInsRow && StCallTarget(ins + ST_PART_COL) == (uintptr_t)&StPartInsCol);
    assert(StCallTarget(rem) == (uintptr_t)&StPartRemRow && StCallTarget(rem + ST_PART_COL) == (uintptr_t)&StPartRemCol);
    assert(ins[5] == 0x90 && ins[6] == 0x90 && rem[ST_PART_COL + 6] == 0x90 && !memcmp(ins + 7, kStPartIns + 7, ST_PART_COL - 7) &&
           !memcmp(rem + ST_PART_COL + 7, kStPartRem + ST_PART_COL + 7, sizeof(kStPartRem) - ST_PART_COL - 7));
    assert(!StPartPatch(ins, rem));  // ours now

    // (g) PlaceKeep on a fake place frame
    const StRemoveFn sRm = g_stRemove;
    const DWORD sEnd = g_stEnd;
    g_stRemove = StFakeRemove, g_stEnd = (DWORD)(uintptr_t)&StFakeEnd;
    k.srPlaceRecheck = 0, s_stEndN = 0, g_stPlace.frame = 0;
    memset(&s_stRm, 0, sizeof(s_stRm));
    const StCell c0 = {10, 4, 0, 0x77}, c1 = {14, 9, 0, 0x77};
    StRunPlace(1, 1, 0x1111, &c0, &c1, 0x2201, 0x3333, 0x4444);  // written: one removal, check on, EA's arguments, then EA's end
    assert(s_stRm.n == 1 && s_stRm.lot == 0x1111 && s_stRm.recheck == 1 && s_stRm.diag == 0x2201 && s_stRm.undo == 0x3333 &&
           s_stRm.arg7 == 0x4444 && s_stRm.p0.x == 10 && s_stRm.p0.z == 4 && s_stRm.p1.x == 14 && s_stRm.p1.z == 9 && !s_stRm.p0.pad);
    assert(s_stEndN == 1 && s_stEndArgs[0] == 0x1111 && s_stEndArgs[1] == 0 && !g_stPlace.frame && k.srPlaceRecheck == 1);
    StRunPlace(0, 1, 0x1111, &c0, &c1, 0, 0, 0);  // the cell write failed: nothing removed
    assert(s_stRm.n == 1 && s_stEndN == 2 && !g_stPlace.frame && k.srPlaceRecheck == 1);
    StRunPlace(1, 0, 0x1111, &c0, &c1, 0, 0, 0);  // nothing deferred (stilted kind, other level): EA's end only
    assert(s_stRm.n == 1 && s_stEndN == 3);
    StPlaceDeferAt(0x12345678, 0x1111, &c0, &c1, 0, 0, 0);  // a record another call left: dropped, never run
    StRunPlace(1, 0, 0x1111, &c0, &c1, 0, 0, 0);
    assert(s_stRm.n == 1 && s_stEndN == 4 && !g_stPlace.frame);
    alignas(4) static u8 frame[0x40];
    frame[0x12] = 1;
    const DWORD f = (DWORD)(uintptr_t)frame;
    StCell m0 = c0;
    StPlaceDeferAt(f, 0x1111, &m0, &c1, 0, 0, 0);
    StPlaceFlush(f, 0x2222);  // another lot: dropped
    assert(s_stRm.n == 1 && !g_stPlace.frame);
    StPlaceDeferAt(f, 0x1111, &m0, &c1, 0, 0, 0);
    m0.x = 99;  // the deck branch's frame cells change before the end: the copies are used
    StPlaceFlush(f, 0x1111);
    assert(s_stRm.n == 2 && s_stRm.p0.x == 10 && k.srPlaceRecheck == 2);

    // (h) StPlacePatch: the three sites checked before anything is written; then the two calls are ours
    static u8 defer[0x64], write[sizeof(kStWrite)], end[0x28];
    const DWORD fr = (DWORD)(uintptr_t)&StFakeRemove, fe = (DWORD)(uintptr_t)&StFakeEnd;
    memcpy(defer, kStDefer, sizeof(kStDefer));
    defer[ST_DEFER_CALL] = 0xE8;
    *(LONG*)(defer + ST_DEFER_CALL + 1) = (LONG)(fr - ((uintptr_t)defer + ST_DEFER_CALL + 5));
    memcpy(defer + ST_DEFER_CALL + 5, kStDeferPost, sizeof(kStDeferPost));
    memcpy(write, kStWrite, sizeof(write));
    memcpy(end, kStEndPre, sizeof(kStEndPre));
    end[ST_END_CALL] = 0xE8;
    *(LONG*)(end + ST_END_CALL + 1) = (LONG)(fe - ((uintptr_t)end + ST_END_CALL + 5));
    memcpy(end + ST_END_CALL + 5, kStEndPost, sizeof(kStEndPost));
    g_stRemove = nullptr, g_stEnd = 0;
    write[8] = 0x13;  // mov [esp+13h],al: another frame
    assert(!StPlacePatch(defer, write, end, fr, fe) && StCallTarget(defer + ST_DEFER_CALL) == fr && StCallTarget(end + ST_END_CALL) == fe &&
           !g_stRemove);
    write[8] = 0x12;
    assert(StPlacePatch(defer, write, end, fr, fe));
    assert(StCallTarget(defer + ST_DEFER_CALL) == (uintptr_t)&StPlaceDefer && StCallTarget(end + ST_END_CALL) == (uintptr_t)&StPlaceEndStub);
    assert(g_stRemove == StFakeRemove && g_stEnd == fe && !memcmp(defer + ST_DEFER_CALL + 5, kStDeferPost, sizeof(kStDeferPost)));
    assert(!StPlacePatch(defer, write, end, fr, fe));  // ours now

    g_stRemove = sRm, g_stEnd = sEnd, g_stPlace.frame = 0;
    k.srClamped = k.srPlaceRecheck = 0;
    printf("selftest stiltremove placekeep / partclamp ok (removal after the cell write with the check on, clamp at 40 / 64 only, sites checked)\n");
}

static void TestStiltRemove() {
    static_assert(offsetof(Ts3nCounters, srMode) == 0x6B48 && offsetof(Ts3nCounters, srImportFixed) == 0x6B54 &&
                      offsetof(Ts3nCounters, srPlaceMode) == 0x6B58 && offsetof(Ts3nCounters, srClamped) == 0x6B64 && sizeof(Ts3nCounters) == 6874 * 4,
                  "counters v923 layout after v222 (docs/registry.md)");
    TestStiltPlaceClamp();
    const StKeyFn sKey = g_stKey;
    const DWORD sCont = g_stLoadCont;
    Ts3nCounters& k = ts3native_counters;
    g_stKey = StFakeKey;
    k.srImport = k.srImportFixed = k.srLoadFixed = 0;

    // (a) load: a good version-3 key stays, a bad one is repaired and counted, version 2 is EA's recompute (not counted)
    const float grid[2] = {10.5f, 4.0f};
    const StPoint gp = {10.5f, 4.0f, 0};
    const DWORD good = StFakeKey(&gp);
    DWORD key = good;
    StLoadKey(grid, &key, 3);
    assert(key == good && k.srLoadFixed == 0);
    key = good + 3;
    StLoadKey(grid, &key, 3);
    assert(key == good && k.srLoadFixed == 1);
    key = 0;
    StLoadKey(grid, &key, 2);
    assert(key == good && k.srLoadFixed == 1);

    // (b) import: the key comes from the grid point 0x14 bytes below the struct, an inset render position is counted
    alignas(16) static u8 frame[0x90];
    memset(frame, 0, sizeof(frame));
    memcpy(frame + 0x64, grid, 8);
    StPoint* p = (StPoint*)(frame + 0x78);
    *p = {10.5f - 0.3465f, 4.0f + 0.3465f, 0};
    assert(StImportKey(p) == good && k.srImport == 1 && k.srImportFixed == 1);
    *p = {10.5f, 4.0f, 0};
    assert(StImportKey(p) == good && k.srImport == 2 && k.srImportFixed == 1);

    // (c) StPatch: all three sites checked before anything is written; then the load jump and the import call are ours
    static u8 load[0x40], imp[0x30], pair[sizeof(kStPair)];
    const DWORD fakeKeyFn = (DWORD)(uintptr_t)&StFakeKey;
    memcpy(load, kStLoad, sizeof(kStLoad));
    memcpy(imp, kStImport, sizeof(kStImport));
    imp[ST_IMPORT_CALL] = 0xE8;
    *(LONG*)(imp + ST_IMPORT_CALL + 1) = (LONG)(fakeKeyFn - ((uintptr_t)imp + ST_IMPORT_CALL + 5));
    memcpy(pair, kStPair, sizeof(kStPair));
    pair[15] = 0x60;  // another frame offset: refused, nothing written
    assert(!StPatch(load, imp, pair, fakeKeyFn) && !memcmp(load, kStLoad, sizeof(kStLoad)) && imp[ST_IMPORT_CALL] == 0xE8 &&
           (uintptr_t)imp + ST_IMPORT_CALL + 5 + *(LONG*)(imp + ST_IMPORT_CALL + 1) == fakeKeyFn);
    pair[15] = 0x64;
    assert(StPatch(load, imp, pair, fakeKeyFn));
    assert(load[0] == 0xE9 && (uintptr_t)load + 5 + *(LONG*)(load + 1) == (uintptr_t)&StLoadStub && load[5] == 0x90);
    assert(imp[ST_IMPORT_CALL] == 0xE8 && (uintptr_t)imp + ST_IMPORT_CALL + 5 + *(LONG*)(imp + ST_IMPORT_CALL + 1) == (uintptr_t)&StImportKey);
    assert(g_stLoadCont == (DWORD)(uintptr_t)(load + ST_LOAD_CONT) && g_stKey == StFakeKey);
    assert(!StPatch(load, imp, pair, fakeKeyFn));  // ours now

    g_stKey = sKey, g_stLoadCont = sCont;
    k.srImport = k.srImportFixed = k.srLoadFixed = 0;
    printf("selftest stiltremove ok (grid-point keys at import and load, bad keys counted, sites checked before writing)\n");
}
