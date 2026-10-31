// [EaCrashFixes] WorldAStarRefs (0.36.150, docs/67 §30, counters v145 wa*, issue #132): range check for the world A*'s
// neighbour references. Default OFF. Included by ts3native.cpp after eafix.cpp.
//
// The world A* 0x676260 decodes each neighbour reference of the current waypoint (docs/99 §1.3: bits 0-9 waypoint, bits
// 10-12 dx + 3, bits 13-15 dz + 3) into a target chunk `current + dz * w + dx` in 16 bits and stores the pair at its
// [esp+1Ch] (chunk) / [esp+1Eh] (waypoint), 0x67661D / 0x676622. Nothing checks the pair: it indexes the closed-bitset row
// table (chunk + 1, 0x67663F / 0x67664F) and then the cost function's chunk offset table and the chunk's record offsets
// (0x6742B5; the parent's pair at 0x6743FE). A pair outside R reads past those tables: Baseline-IP crashed 5 of 5 at the
// four sites, run 168 with chunk 0xFFD5 (-43) and waypoint 0x207 on a 64 x 64 world whose shipped graph has every reference
// in range (docs/67 §30). Fix: `mov edx,[esp+1Ch]; movzx ecx,ax` at 0x676627 becomes a jump to WaStub, which sends a pair
// outside R (chunk >= w * h, or waypoint >= the chunk's count) to the game's own "already closed" path 0x67673F (next
// neighbour; it reloads its state from the stack) and runs the two moved instructions for every other pair. In-range pairs
// cost one call; game state is never written. One "ea fix: WorldAStarRefs" line for each of the first 8 skips.

static const DWORD WA_SITE = 0x00676627, WA_BACK = 0x0067662E, WA_SKIP = 0x0067673F;
static const u8 kWaSite[7] = {0x8B, 0x54, 0x24, 0x1C, 0x0F, 0xB7, 0xC8};  // mov edx,[esp+1Ch]; movzx ecx,ax
static DWORD g_waBack = WA_BACK, g_waSkip = WA_SKIP;
static volatile LONG g_waLogged;

// sp = the A*'s esp at the patch site; cur = esi there (the current search node's chunk / waypoint pair).
// Returns 1 for a pair inside R, else 0 (counted in waSkip[reason - 1]).
static LONG __stdcall WaRefOk(const u8* sp, const WORD* cur) {
    const u8* r = *(const u8* const*)(sp + 0x50);  // R: +0 w, +4 h, +8 chunk offsets, +0xC record area (docs/99 §1.4)
    const DWORD chunk = *(const WORD*)(sp + 0x1C), wp = *(const WORD*)(sp + 0x1E);
    const DWORD w = *(const DWORD*)r, h = *(const DWORD*)(r + 4);
    DWORD count = 0;
    int why = 0;
    if (chunk >= w * h) why = 1;
    else {
        __try { count = *(const WORD*)(*(const u8* const*)(r + 0xC) + (*(const DWORD* const*)(r + 8))[chunk]); }
        __except (EXCEPTION_EXECUTE_HANDLER) { why = 3; }
        if (!why && wp >= count) why = 2;
    }
    if (!why) return 1;
    InterlockedIncrement(&ts3native_counters.waSkip[why - 1]);
    if (InterlockedIncrement(&g_waLogged) <= 8) {
        static const char* const kWhy[3] = {"chunk outside the map", "waypoint past its chunk's count", "chunk record unreadable"};
        const u8* rec = *(const u8* const*)(sp + 0x2C);  // the current waypoint's record: +0 n, +1 flags, +5 WORD refs[n]
        const DWORD k = sp[0x58];
        DWORD ref = 0, n = 0, fl = 0, cc = 0xFFFF, cw = 0xFFFF;
        __try { n = rec[0], fl = rec[1], ref = *(const WORD*)(rec + 5 + 2 * k), cc = cur[0], cw = cur[1]; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("ea fix: WorldAStarRefs: neighbour %lu of %lu of waypoint (chunk %lu, %lu), flags 0x%02lX, ref 0x%04lX -> chunk %lu "
            "waypoint %lu (map %lu x %lu chunks, chunk count %lu) skipped: %s", k, n, cc, cw, fl, ref, chunk, wp, w, h, count, kWhy[why - 1]);
    }
    return 0;
}

// Replaces the 7 bytes at 0x676627. Registers and flags at the patch site are the game's (flags are dead: `add edx,1` follows).
static __declspec(naked) void WaStub() {
    __asm {
        push eax
        push ecx
        push edx
        push esi
        lea ecx, [esp + 16]  // the A*'s esp
        push ecx
        call WaRefOk
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jz skip
        mov edx, [esp + 0x1C]
        movzx ecx, ax
        jmp dword ptr [g_waBack]
    skip:
        jmp dword ptr [g_waSkip]
    }
}

static bool WaPatch(u8* at) {
    if (!Matches(at, kWaSite, sizeof(kWaSite))) return false;
    u8 b[7] = {0xE9, 0, 0, 0, 0, 0x66, 0x90};
    *(LONG*)(b + 1) = (LONG)((uintptr_t)&WaStub - ((uintptr_t)at + 5));
    return Write(at, b, sizeof(b));
}

static bool ApplyWorldAStarRefs(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"EaCrashFixes", L"WorldAStarRefs", 0, ini)) return false;
    if (!WaPatch((u8*)(uintptr_t)WA_SITE)) {
        Log("SKIP ea fix: WorldAStarRefs: bytes at 0x%08X differ (another mod or a probe build owns it): not installed", WA_SITE);
        return false;
    }
    ts3native_counters.waOn = 1;
    Log("OK   ea fix: WorldAStarRefs (docs/67 §30): world A* neighbours outside the routing graph are skipped (0x%08X)", WA_SITE);
    return true;  // keep the log open for the skip lines
}
