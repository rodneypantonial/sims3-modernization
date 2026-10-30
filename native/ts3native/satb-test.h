// TestSatbLog: [AutShadow] Satb log (satb.cpp, docs/206 §7): first-write values, partial-word merge, overflow, epoch wraps,
// and one writer (log, then store) racing one reader (load, then merge): the reader must always get the cut value.
static DWORD g_sbArr[4096];
static DWORD g_sbCut[4096];
static volatile LONG g_sbPause = 1, g_sbIdle = 1, g_sbQuit, g_sbBad, g_sbReads;
static DWORD WINAPI SatbTestReader(void*) {
    unsigned x = 12345;
    while (!g_sbQuit) {
        InterlockedExchange(&g_sbIdle, 0);  // full fence before reading g_sbPause
        if (g_sbPause) { g_sbIdle = 1; YieldProcessor(); continue; }
        x = x * 1103515245u + 12345u;
        const DWORD i = (x >> 8) & 4095;
        u8 b[8];
        const DWORD at = (DWORD)(uintptr_t)&g_sbArr[i];
        const DWORD n = i < 4095 && (x & 1) ? 8 : 4;  // 8-byte loads too (two words)
        memcpy(b, (const void*)(uintptr_t)at, n);
        _ReadWriteBarrier();
        SatbMerge(at, n, b);
        if (memcmp(b, &g_sbCut[i], n)) InterlockedIncrement(&g_sbBad);
        g_sbReads++;
    }
    return 0;
}

static void TestSatbLog() {
    assert(SatbAlloc());
    // single thread: the first write's value is kept, bytes of a partly covered word are merged
    for (int i = 0; i < 4096; i++) g_sbArr[i] = 0x1000 + i;
    SatbReset();
    const DWORD a0 = (DWORD)(uintptr_t)&g_sbArr[10];
    assert(SatbLogWord(a0) && g_satbN == 1);
    g_sbArr[10] = 7;
    assert(SatbLogWord(a0) && g_satbN == 1);  // second write: already logged
    g_sbArr[10] = 8;
    DWORD v = 0;
    assert(SatbFind(a0, &v) && v == 0x1000 + 10);
    u8 b[6];
    memcpy(b, (const u8*)&g_sbArr[10] + 2, 6);  // bytes 2..7: half of word 10 (logged), half of word 11 (not)
    assert(SatbMerge(a0 + 2, 6, b) == 1);
    const DWORD w10 = 0x1000 + 10, w11 = 0x1000 + 11;
    assert(!memcmp(b, (const u8*)&w10 + 2, 2) && !memcmp(b + 2, &w11, 4));
    // new job: the old entry is gone without a clear
    SatbReset();
    assert(!SatbFind(a0, &v) && g_satbN == 0);
    // overflow: SATB_CAP entries, then refused
    static DWORD big[SATB_CAP + 8];
    for (DWORD i = 0; i < SATB_CAP; i++) assert(SatbLogWord((DWORD)(uintptr_t)&big[i]));
    assert(!SatbLogWord((DWORD)(uintptr_t)&big[SATB_CAP]));
    DWORD vv;
    assert(SatbFind((DWORD)(uintptr_t)&big[SATB_CAP - 1], &vv) && !SatbFind((DWORD)(uintptr_t)&big[SATB_CAP], &vv));
    // epoch wraps: 8-bit after 255 jobs, 16-bit after 65535; an entry of an old job is never found
    for (int j = 0; j < 70000; j++) {
        SatbReset();
        if (j % 997 == 0 || g_satbE8 == 1 || g_satbE16 == 1) {
            assert(!SatbFind(a0, &v));
            assert(SatbLogWord(a0) && SatbFind(a0, &v));
        }
    }
    // writer + reader: 200 jobs, the writer logs then stores random values; the reader always sees the cut value
    g_sbBad = 0, g_sbReads = 0, g_sbQuit = 0;
    HANDLE th = CreateThread(NULL, 0, SatbTestReader, NULL, 0, NULL);
    unsigned x = 777;
    LONG stores = 0;
    for (int job = 0; job < 200; job++) {
        InterlockedExchange(&g_sbPause, 1);  // no reader during a reset, as in the game (the worker is idle at a cut)
        while (!g_sbIdle) YieldProcessor();
        SatbReset();
        memcpy(g_sbCut, g_sbArr, sizeof(g_sbArr));
        InterlockedExchange(&g_sbPause, 0);
        for (int k = 0; k < 20000; k++) {
            x = x * 1664525u + 1013904223u;
            const DWORD i = (x >> 10) & 4095;
            if (!SatbLogWord((DWORD)(uintptr_t)&g_sbArr[i])) break;
            _ReadWriteBarrier();
            g_sbArr[i] = x;
            stores++;
        }
    }
    g_sbQuit = 1;
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    printf("satb log: first values kept, partial words merged, overflow at %lu, epoch wraps clean; writer/reader %ld stores, %ld reads, %ld wrong\n",
           SATB_CAP, stores, g_sbReads, g_sbBad);
    assert(g_sbBad == 0 && g_sbReads > 1000);
}
