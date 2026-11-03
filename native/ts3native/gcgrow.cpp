// [GcGrowLog] (counters v161 gg*, docs/232 Run 8l): one "gcgrow:" line per Boehm heap expansion. Log only, default OFF.
// Included by ts3native.cpp after memexp.cpp (g_mx) and savewrite.cpp (PatchCalls).
//
// Run 8k: the GC heap grew from 352 MB to ~2.3 GB within 10 s at the main menu (EA 1.69, GC_large_free_bytes ~2.1 GB of
// it, no single allocation >= 64 MB), and the next image decode crashed on a null buffer. Every growth after GC_init goes
// through the two calls of GC_expand_hp_inner 0xE49C80 in GC_collect_or_expand 0xE4A120 [verified: the only E8 callers
// besides GC_init on both builds]: 0xE4A20C (the step, max(heap / 5 + needed + slop, GrowthStep)) and 0xE4A219 (retry
// with exactly the needed blocks after the step failed); esi holds the needed blocks at both. Both calls go to GgThunk.
// Line: site, blocks asked, needed blocks, heap before / after, GC_gc_no, bytes allocated since the last GC, large free
// bytes, thread, the return address and the first 3 TS3W code addresses above it on the stack (heuristic: stale values
// possible), and the thread's script frames (interpreter, FbStackText). The first 50 expansions, then every one asking
// >= 64 MB, 500 lines at most. Growth not through these calls (between two of ours) is counted as ggUnseenKB.
// ts3native_counters.ggMode (+0x5B04, --ab gcgrowlog) 0 stops the lines; the counters keep counting.

typedef int(__cdecl* GgExpandFn)(DWORD blocks);
static const DWORD GG_EXPAND = TS3A(0x00E49C80);
static const DWORD GG_SITE[2] = {TS3A(0x00E4A20C), TS3A(0x00E4A219)};
static const DWORD GG_TEXT_LO = TS3A(0x00401000), GG_TEXT_HI = TS3A(0x00F95000);
static const DWORD* const GG_LARGE_FREE = (const DWORD*)(uintptr_t)TS3A(0x012225B4);  // GC_large_free_bytes
static GgExpandFn g_ggExpand = (GgExpandFn)(uintptr_t)GG_EXPAND;
static const DWORD* g_ggLargeFree = GG_LARGE_FREE;  // the selftest points it at a fake
static DWORD g_ggLastHeap;  // heap bytes after our previous call, 0 before the first

static DWORD GgRead(const DWORD* p) {
    __try { return *p; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// sp = esp at the thunk's entry: sp[0] = return address into GC_collect_or_expand, sp[1] = blocks
static int __cdecl GgExpand(DWORD blocks, DWORD needed, const DWORD* sp) {
    Ts3nCounters& k = ts3native_counters;
    const DWORD before = GgRead(g_mx.heapSize), gcNo = GgRead(g_mx.gcNo), words = GgRead(g_mx.wordsAllocd);
    const int r = g_ggExpand(blocks);
    const DWORD after = GgRead(g_mx.heapSize);
    if (g_ggLastHeap && before > g_ggLastHeap) k.ggUnseenKB += (LONG)((before - g_ggLastHeap) >> 10);
    g_ggLastHeap = after;
    const LONG n = ++k.ggExpands;  // under the GC allocation lock
    if (!r) k.ggFails++;
    k.ggKB += (LONG)((after - before) >> 10);
    const LONG askKB = (LONG)(blocks << 2);
    if (askKB > k.ggMaxAskKB) k.ggMaxAskKB = askKB;
    if (!k.ggMode || (n > 50 && blocks < 0x4000) || k.ggLines >= 500) return r;
    k.ggLines++;
    const DWORD ret = sp[0];
    const int site = ret - 5 == GG_SITE[0] ? 1 : ret - 5 == GG_SITE[1] ? 2 : 0;
    DWORD chain[3] = {0, 0, 0};
    __try {
        for (int i = 2, m = 0; i < 2 + 96 && m < 3; i++)
            if (sp[i] >= GG_TEXT_LO && sp[i] < GG_TEXT_HI) chain[m++] = sp[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    char st[700];
    if (FbStackText(st, sizeof(st), 8) <= 0) strcpy_s(st, "none");
    const DWORD tid = GetCurrentThreadId();
    Log("gcgrow: #%ld site %d (%s) ask %lu blocks (%lu KB) needed %lu blocks -> %s; heap %lu -> %lu KB; gc_no %lu, allocated since "
        "the last GC %lu KB, large free %lu KB; thread %lu%s; ret 0x%08lX, stack code refs 0x%08lX 0x%08lX 0x%08lX; script: %s",
        n, site, site == 1 ? "step" : site == 2 ? "retry needed" : "?", blocks, blocks << 2, needed, r ? "ok" : "FAILED", before >> 10,
        after >> 10, gcNo, (words << 2) >> 10, GgRead(g_ggLargeFree) >> 10, tid, tid == g_mainTid ? " (main)" : "", ret, chain[0],
        chain[1], chain[2], st);
    return r;
}

// Replaces GC_expand_hp_inner at both calls (cdecl, 1 argument): GgExpand(blocks, esi = needed, entry esp)
static __declspec(naked) void GgThunk() {
    __asm {
        mov eax, esp
        push eax
        push esi
        push dword ptr [eax + 4]
        call GgExpand
        add esp, 12
        ret
    }
}

static bool ApplyGcGrowLog(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"GcGrowLog", L"Enabled", 0, ini)) { Log("gc grow log off (Enabled=0)"); return false; }
    const CallSite s[2] = {{GG_SITE[0], GG_EXPAND, (void*)GgThunk}, {GG_SITE[1], GG_EXPAND, (void*)GgThunk}};
    if (!PatchCalls("gc grow log", s, 2)) return false;
    ts3native_counters.ggReady = 1;
    ts3native_counters.ggMode = 1;
    Log("OK   gc grow log: GC_expand_hp_inner 0x%08X calls 0x%08X / 0x%08X -> GgThunk (first 50 expansions, then >= 64 MB)", GG_EXPAND,
        GG_SITE[0], GG_SITE[1]);
    return true;  // keep the log open for the gcgrow lines
}
