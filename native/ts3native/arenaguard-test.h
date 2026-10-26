// TestArenaGuard: docs/67 §22.2, the Crash S pattern on a fake EA frame-arena pool ([ArenaGuard], monothread-m5.cpp).
// A task that ends by a managed exception is released twice (EA frees its state twice) with a chunk still on its chain; a
// yielded task then takes that chunk and keeps a delegate in its frame; another state's grow must not get the same chunk.
// Pool as in the game: sentinel +0 last, +4 first; node +0 prev, +4 next, +8 chunk; chunk +0 top, +8 previous chunk, data +0xC.
static DWORD g_agSent[2], g_agNodes[16][3], g_agCh[3][0x7FC / 4];
static DWORD* g_agPoolHead = g_agSent;
static int g_agNodeN;

static void AgPush(DWORD* c) {  // pool push at the head, chunk top back to its data start (0xD71A20 / 0xD71B70)
    DWORD* n = g_agNodes[g_agNodeN++];
    n[0] = (DWORD)(uintptr_t)g_agSent, n[1] = g_agSent[1], n[2] = (DWORD)(uintptr_t)c;
    ((DWORD*)(uintptr_t)g_agSent[1])[0] = (DWORD)(uintptr_t)n;
    g_agSent[1] = (DWORD)(uintptr_t)n;
    c[0] = (DWORD)(uintptr_t)c + 0xC;
}
static void __cdecl AgFakeRelAll(DWORD* st) {  // 0xD71B70: the whole chain back to the pool, +0x13C left as it was (EA's bug)
    for (DWORD* c = (DWORD*)(uintptr_t)st[0x13C / 4]; c;) {
        DWORD* prev = (DWORD*)(uintptr_t)c[2];
        AgPush(c);
        c = prev;
    }
}
static DWORD* __cdecl AgFakeGrow(DWORD* st, DWORD size) {  // 0xD71900: the pool head's chunk onto the state's chain
    DWORD* n = (DWORD*)(uintptr_t)g_agSent[1];
    if (n == g_agSent) return NULL;
    g_agSent[1] = n[1], ((DWORD*)(uintptr_t)n[1])[0] = (DWORD)(uintptr_t)g_agSent;
    DWORD* c = (DWORD*)(uintptr_t)n[2];
    c[2] = st[0x13C / 4], st[0x13C / 4] = (DWORD)(uintptr_t)c;
    c[0] = (DWORD)(uintptr_t)(c + 3) + size;
    return c + 3;
}

// one run: pool [B, D]; task S takes B and ends by exception (released twice); yielded task Y grows and keeps a delegate in
// its frame; state Z grows and writes its own frame. dupBefore: S's double release happened before the guard (pool B B D).
// Returns true when Y's delegate slot survived Z's grow.
static bool AgScenario(bool guard, bool dupBefore, DWORD** zFrame) {
    typedef DWORD*(__cdecl * GrowFn)(DWORD*, DWORD);
    typedef void(__cdecl * RelFn)(DWORD*);
    static DWORD S[0x150 / 4], Y[0x150 / 4], Z[0x150 / 4];
    memset(S, 0, sizeof(S)), memset(Y, 0, sizeof(Y)), memset(Z, 0, sizeof(Z)), memset(g_agCh, 0, sizeof(g_agCh));
    g_agSent[0] = g_agSent[1] = (DWORD)(uintptr_t)g_agSent, g_agNodeN = 0;
    DWORD* B = g_agCh[0];
    DWORD* D = g_agCh[1];
    AgPush(D), AgPush(B);
    const GrowFn grow = guard ? (GrowFn)(void*)MtArenaGrow : AgFakeGrow;
    const RelFn rel = guard && !dupBefore ? (RelFn)(void*)MtArenaReleaseAll : AgFakeRelAll;
    grow(S, 0x40);
    rel(S), rel(S);  // 0xE59FD0 -> 0xE52DD0, then ScriptHost_RunTask 0xD7F166 -> 0xE52DD0
    DWORD* yf = grow(Y, 0x40);
    yf[0] = 0xDE1E6A7E;  // the InsideLoopFunction delegate DoLoop keeps across its yields
    DWORD* zf = grow(Z, 0x40);
    if (zf) zf[0] = 0;  // Z's first local
    *zFrame = zf;
    return yf[0] == 0xDE1E6A7E;
}

static void TestArenaGuard() {
    Ts3nMtProbe& m = ts3native_mtprobe;
    DWORD** poolSave = g_mtArenaPool;
    void* growSave = g_mtGrowOrig;
    void* relSave = g_mtRelAllOrig;
    g_mtArenaPool = &g_agPoolHead, g_mtGrowOrig = (void*)AgFakeGrow, g_mtRelAllOrig = (void*)AgFakeRelAll;
    DWORD* zf;
    // negative control: the game's own arena reproduces the Crash S shape (Z overwrites Y's delegate)
    assert(!AgScenario(false, false, &zf) && zf == g_agCh[0] + 3);
    // guard on: release-all clears +0x13C, so the second release adds nothing; Z gets D
    const LONG rel0 = m.arenaRelNonEmpty, drop0 = m.arenaDropped;
    assert(AgScenario(true, false, &zf) && zf == g_agCh[1] + 3);
    assert(m.arenaRelNonEmpty - rel0 == 1 && m.arenaDropped - drop0 == 0);
    // guard on after the duplicate exists: the heal before Z's grow drops B's second node (in use by Y)
    assert(AgScenario(true, true, &zf) && zf == g_agCh[1] + 3);
    assert(m.arenaDropped - drop0 == 1);
    g_mtArenaPool = poolSave, g_mtGrowOrig = growSave, g_mtRelAllOrig = relSave;
    printf("arena guard: double release no longer pools a chunk twice; an in-use pooled chunk is dropped before a grow\n");
}
