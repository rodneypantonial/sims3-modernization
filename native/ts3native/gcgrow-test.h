// TestGcGrowLog ([GcGrowLog], gcgrow.cpp, docs/232 Run 8l). Included by selftest.cpp.
// The thunk runs for real from a fake GC_collect_or_expand frame (esi = needed, one pushed argument, add esp,4 after the
// call): the fake expand grows a fake heap; counters, failure, growth outside our calls and the line limit are checked.

static DWORD g_ggFakeHeap, g_ggFakeAsk;
static int __cdecl GgFakeExpand(DWORD blocks) {
    g_ggFakeAsk = blocks;
    if (blocks == 3) return 0;  // "out of address space"
    g_ggFakeHeap += blocks << 12;
    return 1;
}
static __declspec(naked) int __stdcall GgRunSite(DWORD /*blocks*/, DWORD /*needed*/) {
    __asm {
        push esi
        mov esi, [esp + 12]
        push dword ptr [esp + 8]
        call GgThunk
        add esp, 4
        cmp esi, [esp + 12]  // esi kept
        je kept
        mov eax, -1
    kept:
        pop esi
        ret 8
    }
}

static void TestGcGrowLog() {
    static_assert(offsetof(Ts3nCounters, ggMode) == 0x5B04 && offsetof(Ts3nCounters, ggUnseenKB) == 0x5B1C &&
                      sizeof(Ts3nCounters) == 5835 * 4,
                  "counters v161 layout (docs/registry.md)");
    Ts3nCounters& k = ts3native_counters;
    const MxAddrs saved = g_mx;
    DWORD gcNo = 7, words = 0x100, largeFree = 0;
    g_mx.heapSize = &g_ggFakeHeap, g_mx.gcNo = &gcNo, g_mx.wordsAllocd = &words;
    g_ggLargeFree = &largeFree;
    g_ggExpand = GgFakeExpand;
    g_ggFakeHeap = 352u << 20, g_ggLastHeap = 0;
    memset(&k.ggMode, 0, sizeof(LONG) * 10);
    k.ggMode = 1;
    assert(GgRunSite(0x2000, 0x10) == 1 && g_ggFakeAsk == 0x2000 && g_ggFakeHeap == 384u << 20);
    assert(k.ggExpands == 1 && k.ggKB == 32 * 1024 && k.ggMaxAskKB == 32 * 1024 && k.ggLines == 1 && k.ggFails == 0);
    assert(GgRunSite(3, 3) == 0 && k.ggFails == 1 && k.ggKB == 32 * 1024);
    g_ggFakeHeap += 1u << 20;  // growth not through our calls
    assert(GgRunSite(0x40, 0x40) == 1 && k.ggUnseenKB == 1024);
    for (int i = 0; i < 60; i++) GgRunSite(0x40, 1);
    assert(k.ggExpands == 63 && k.ggLines == 50);   // first 50 lines only
    assert(GgRunSite(0x4000, 0x4000) == 1 && k.ggLines == 51 && k.ggMaxAskKB == 64 * 1024);  // >= 64 MB always logged
    k.ggMode = 0;
    assert(GgRunSite(0x4000, 0x4000) == 1 && k.ggLines == 51 && k.ggExpands == 65);  // switch off: count only
    g_mx = saved, g_ggLargeFree = GG_LARGE_FREE, g_ggExpand = (GgExpandFn)(uintptr_t)GG_EXPAND, g_ggLastHeap = 0;
    memset(&k.ggMode, 0, sizeof(LONG) * 10);
    printf("GcGrowLog: thunk passes blocks and needed (esi kept), heap growth, failures, unseen growth, 50-line limit, "
           ">= 64 MB lines, ggMode 0 counts only: ok\n");
}
