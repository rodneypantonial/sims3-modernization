// TestWorldAStarRefs ([EaCrashFixes] WorldAStarRefs, wastar.cpp, docs/67 §30). Included by selftest.cpp.
// A synthetic routing graph R (2 x 2 chunks, one chunk record on a reserved page) and a fake A* frame; the stub runs for
// real, with its two exits pointed at recorders.

static DWORD g_waOut[7];  // exit (1 back, 2 skip), edx, ecx, eax, ebx, esi, edi at the exit
static __declspec(naked) void WaTestBack() {
    __asm {
        mov g_waOut[0], 1
        mov g_waOut[4], edx
        mov g_waOut[8], ecx
        mov g_waOut[12], eax
        mov g_waOut[16], ebx
        mov g_waOut[20], esi
        mov g_waOut[24], edi
        ret
    }
}
static __declspec(naked) void WaTestSkip() {
    __asm {
        mov g_waOut[0], 2
        mov g_waOut[12], eax
        mov g_waOut[16], ebx
        mov g_waOut[20], esi
        mov g_waOut[24], edi
        ret
    }
}

// The stub's esp is the frame below this call's return address: fields at [esp + off - 4] here are at [sp + off] there.
static void WaRunStub(const u8* r, DWORD chunk, DWORD wp, const u8* rec, const WORD* cur) {
    memset(g_waOut, 0, sizeof(g_waOut));
    __asm {
        push ebx
        push esi
        push edi
        sub esp, 0x100
        mov eax, r
        mov [esp + 0x50 - 4], eax
        mov eax, chunk
        mov word ptr [esp + 0x1C - 4], ax
        mov eax, wp
        mov word ptr [esp + 0x1E - 4], ax
        mov eax, rec
        mov [esp + 0x2C - 4], eax
        mov byte ptr [esp + 0x58 - 4], 1
        mov esi, cur
        mov ebx, 0x11111111
        mov edi, 0x33333333
        mov eax, wp
        or eax, 0xABCD0000  // the game's eax holds more than the waypoint; movzx ecx,ax must take the low word
        call WaStub
        add esp, 0x100
        pop edi
        pop esi
        pop ebx
    }
}

static void TestWorldAStarRefs() {
    static_assert(offsetof(Ts3nCounters, mdOn) == 0x5520 && offsetof(Ts3nCounters, waOn) == 0x5530 &&
                      offsetof(Ts3nCounters, waSkip) == 0x5534 && sizeof(Ts3nCounters) == 5456 * 4,
                  "counters v145 layout after v144 (docs/registry.md)");
    Ts3nCounters& k = ts3native_counters;

    // patch: foreign bytes refused and untouched; the game's bytes become jmp WaStub + 2-byte nop; ours are not patched again
    alignas(4) static u8 site[8];
    memcpy(site, kWaSite, 7), site[2] = 0x25;
    u8 orig[8];
    memcpy(orig, site, 8);
    assert(!WaPatch(site) && !memcmp(site, orig, 8));
    site[2] = 0x24;
    assert(WaPatch(site));
    assert(site[0] == 0xE9 && (uintptr_t)site + 5 + *(LONG*)(site + 1) == (uintptr_t)&WaStub && site[5] == 0x66 && site[6] == 0x90);
    assert(!WaPatch(site));

    // R: w 2, h 2; chunk counts 3, 1, 0; chunk 3's record on a reserved page (unreadable)
    alignas(4) static u8 area[64];
    alignas(4) static DWORD offs[4];
    alignas(4) static u8 r[0x14];
    u8* hole = (u8*)VirtualAlloc(NULL, 4096, MEM_RESERVE, PAGE_NOACCESS);
    assert(hole);
    *(WORD*)(area + 0) = 3, *(WORD*)(area + 16) = 1, *(WORD*)(area + 32) = 0;
    offs[0] = 0, offs[1] = 16, offs[2] = 32, offs[3] = (DWORD)((uintptr_t)hole - (uintptr_t)area);
    *(DWORD*)(r + 0) = 2, *(DWORD*)(r + 4) = 2, *(DWORD**)(r + 8) = offs, *(u8**)(r + 0xC) = area;
    alignas(4) static u8 rec[16] = {2, 0x10, 0, 0, 0, 0x07, 0x4E, 0x34, 0x12};  // n 2, flags, refs 0x4E07 0x1234
    static const WORD cur[2] = {21, 5};

    alignas(4) static u8 sp[0x60];
    *(u8**)(sp + 0x50) = r, *(u8**)(sp + 0x2C) = rec, sp[0x58] = 1;
    struct Case { DWORD chunk, wp; LONG ok, why; } cases[] = {
        {0, 2, 1, 0}, {0, 0, 1, 0}, {4, 0, 0, 1}, {0xFFD5, 0x207, 0, 1}, {1, 1, 0, 2}, {2, 0, 0, 2}, {3, 0, 0, 3}};
    k.waSkip[0] = k.waSkip[1] = k.waSkip[2] = 0;
    LONG want[3] = {0, 0, 0};
    for (const Case& c : cases) {
        *(WORD*)(sp + 0x1C) = (WORD)c.chunk, *(WORD*)(sp + 0x1E) = (WORD)c.wp;
        assert(WaRefOk(sp, cur) == c.ok);
        if (c.why) want[c.why - 1]++;
        assert(k.waSkip[0] == want[0] && k.waSkip[1] == want[1] && k.waSkip[2] == want[2]);
    }

    // the stub: in-range pair -> the moved instructions run and it continues at WA_BACK; out of R -> WA_SKIP; callee-saved kept
    g_waBack = (DWORD)(uintptr_t)&WaTestBack, g_waSkip = (DWORD)(uintptr_t)&WaTestSkip;
    WaRunStub(r, 0, 2, rec, cur);
    assert(g_waOut[0] == 1 && g_waOut[1] == (0u | (2u << 16)) && g_waOut[2] == 2 && g_waOut[3] == (0xABCD0000u | 2));
    assert(g_waOut[4] == 0x11111111 && g_waOut[5] == (DWORD)(uintptr_t)cur && g_waOut[6] == 0x33333333);
    WaRunStub(r, 1, 0, rec, cur);
    assert(g_waOut[0] == 1 && g_waOut[1] == (1u | (0u << 16)) && g_waOut[2] == 0);
    WaRunStub(r, 0xFFD5, 0x207, rec, cur);
    assert(g_waOut[0] == 2 && g_waOut[3] == (0xABCD0000u | 0x207) && g_waOut[4] == 0x11111111 && g_waOut[6] == 0x33333333);
    WaRunStub(r, 1, 1, rec, cur);
    assert(g_waOut[0] == 2);
    assert(k.waSkip[0] == want[0] + 1 && k.waSkip[1] == want[1] + 1);

    g_waBack = WA_BACK, g_waSkip = WA_SKIP;
    k.waSkip[0] = k.waSkip[1] = k.waSkip[2] = 0;
    VirtualFree(hole, 0, MEM_RELEASE);
    printf("WorldAStarRefs: patch bytes, in-range pairs kept (moved instructions run), chunk outside the map / waypoint past "
           "the chunk count / unreadable record skipped to the closed path: ok\n");
}
