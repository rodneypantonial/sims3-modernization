// TestStiltRemove ([StiltRemove], docs/858): a fake key function with EA's formula, the import and load key paths on fake
// frames, and StPatch on buffers holding the expected bytes.

static DWORD __cdecl StFakeKey(const StPoint* p) {
    return ((DWORD)floor(p->z * 8.0 + 0.5) << 12 | (p->level & 0xFF) << 24) | (DWORD)floor(p->x * 8.0 + 0.5);
}

static void TestStiltRemove() {
    static_assert(offsetof(Ts3nCounters, srMode) == 0x6B48 && offsetof(Ts3nCounters, srImportFixed) == 0x6B54 && sizeof(Ts3nCounters) == 6870 * 4,
                  "counters v923 layout after v222 (docs/registry.md)");
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
