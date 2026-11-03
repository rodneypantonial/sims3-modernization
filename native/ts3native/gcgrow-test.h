// TestGcGrowLog ([GcGrowLog], gcgrow.cpp, docs/232 Run 8l). Included by selftest.cpp.
// The thunk runs for real from a fake GC_collect_or_expand frame (esi = needed, one pushed argument, add esp,4 after the
// call): the fake expand grows a fake heap; counters, failure, growth outside our calls and the line limit are checked.

static DWORD g_gwFakeHeap, g_gwFakeAsk;
static int __cdecl GwFakeExpand(DWORD blocks) {
    g_gwFakeAsk = blocks;
    if (blocks == 3) return 0;  // "out of address space"
    g_gwFakeHeap += blocks << 12;
    return 1;
}
static __declspec(naked) int __stdcall GwRunSite(DWORD /*blocks*/, DWORD /*needed*/) {
    __asm {
        push esi
        mov esi, [esp + 12]
        push dword ptr [esp + 8]
        call GwThunk
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
    static_assert(offsetof(Ts3nCounters, gwMode) == 0x5B04 && offsetof(Ts3nCounters, gwUnseenKB) == 0x5B1C &&
                      sizeof(Ts3nCounters) == 5835 * 4,
                  "counters v161 layout (docs/registry.md)");
    Ts3nCounters& k = ts3native_counters;
    const MxAddrs saved = g_mx;
    DWORD gcNo = 7, words = 0x100, largeFree = 0;
    g_mx.heapSize = &g_gwFakeHeap, g_mx.gcNo = &gcNo, g_mx.wordsAllocd = &words;
    g_gwLargeFree = &largeFree;
    g_gwExpand = GwFakeExpand;
    g_gwFakeHeap = 352u << 20, g_gwLastHeap = 0;
    memset(&k.gwMode, 0, sizeof(LONG) * 10);
    k.gwMode = 1;
    assert(GwRunSite(0x2000, 0x10) == 1 && g_gwFakeAsk == 0x2000 && g_gwFakeHeap == 384u << 20);
    assert(k.gwExpands == 1 && k.gwKB == 32 * 1024 && k.gwMaxAskKB == 32 * 1024 && k.gwLines == 1 && k.gwFails == 0);
    assert(GwRunSite(3, 3) == 0 && k.gwFails == 1 && k.gwKB == 32 * 1024);
    g_gwFakeHeap += 1u << 20;  // growth not through our calls
    assert(GwRunSite(0x40, 0x40) == 1 && k.gwUnseenKB == 1024);
    for (int i = 0; i < 60; i++) GwRunSite(0x40, 1);
    assert(k.gwExpands == 63 && k.gwLines == 50);   // first 50 lines only
    assert(GwRunSite(0x4000, 0x4000) == 1 && k.gwLines == 51 && k.gwMaxAskKB == 64 * 1024);  // >= 64 MB always logged
    k.gwMode = 0;
    assert(GwRunSite(0x4000, 0x4000) == 1 && k.gwLines == 51 && k.gwExpands == 65);  // switch off: count only
    g_mx = saved, g_gwLargeFree = GW_LARGE_FREE, g_gwExpand = (GwExpandFn)(uintptr_t)GW_EXPAND, g_gwLastHeap = 0;
    memset(&k.gwMode, 0, sizeof(LONG) * 10);
    printf("GcGrowLog: thunk passes blocks and needed (esi kept), heap growth, failures, unseen growth, 50-line limit, "
           ">= 64 MB lines, gwMode 0 counts only: ok\n");
}
