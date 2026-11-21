// TestPaintShift ([PaintShift], docs/859): PsXform against a port of EA's grid transform (0xCB3120 with the position
// offsets of 0xCB3080), and PsPatch on a buffer holding the expected bytes.

static void __fastcall PsFakeXform(PsXf* t, void*, LONG dW, LONG dD, LONG sW, LONG sD, LONG rot, LONG pos) {
    static const u8 kX[9] = {0, 1, 2, 0, 1, 2, 0, 1, 2}, kZ[9] = {0, 0, 0, 1, 1, 1, 2, 2, 2};  // 0 near / far, 1 centred
    t->rot = rot, t->pos = pos;
    if ((DWORD)rot > 3 || (DWORD)pos > 8) return;
    const LONG w = rot & 1 ? sD : sW, d = rot & 1 ? sW : sD;  // the source turned by the rotation
    t->offX = kX[pos] == 0 ? 0 : kX[pos] == 1 ? (dW - w) >> 1 : dW - w;
    t->offZ = kZ[pos] == 0 ? dD - d : kZ[pos] == 1 ? (dD - d) >> 1 : 0;
    if (rot == 1) t->offZ += sW - 1;
    else if (rot == 2) t->offX += sW - 1, t->offZ += sD - 1;
    else if (rot == 3) t->offX += sD - 1;
}

// the paint transform for a lot of sW x sD tiles on dW x dD, as 0xAF36ED builds it (samples: tiles * 4 + 1)
static PsXf PsPaint(LONG dW, LONG dD, LONG sW, LONG sD, LONG rot, LONG pos) {
    PsXf t = {-1, -1, -1, -1};
    PsXform(&t, NULL, dW * 4 + 1, dD * 4 + 1, sW * 4 + 1, sD * 4 + 1, rot, pos);
    return t;
}

static void TestPaintShift() {
    static_assert(offsetof(Ts3nCounters, srClamped) == 0x6B64 && offsetof(Ts3nCounters, psMode) == 0x6B68 &&
                  offsetof(Ts3nCounters, psSpare) == 0x6B74 && sizeof(Ts3nCounters) == 6878 * 4,
                  "counters v924 layout after v923 (docs/registry.md)");
    const PsXfFn sXf = g_psXform;
    Ts3nCounters& k = ts3native_counters;
    g_psXform = PsFakeXform;
    k.psImports = k.psFixed = 0;

    // (a) same size, every rotation, centred: EA's paint offsets already match, nothing changes
    for (LONG rot = 0; rot < 4; rot++) {
        const PsXf t = PsPaint(20, 20, 20, 20, rot, 4);
        assert(t.offX == (rot == 2 || rot == 3 ? 80 : 0) && t.offZ == (rot == 1 || rot == 2 ? 80 : 0));
    }
    assert(k.psImports == 4 && k.psFixed == 0);

    // (b) 20x30 on 25x30, centred: EA puts the paint 10 samples (2.5 tiles) in, the house 2 tiles: 8 samples
    PsXf t = PsPaint(25, 30, 20, 30, 0, 4);
    assert(t.offX == 8 && t.offZ == 0 && k.psFixed == 1);
    // (c) turned a quarter on 35x20: x is the odd centred axis (35 - 30), z keeps the rotation term 4 * 20
    t = PsPaint(35, 20, 20, 30, 1, 4);
    assert(t.offX == 8 && t.offZ == 80 && k.psFixed == 2);
    // (d) front-left corner and an even difference: EA's offsets stand
    t = PsPaint(25, 30, 20, 30, 0, 0);
    assert(t.offX == 0 && t.offZ == 0);
    t = PsPaint(30, 30, 20, 30, 2, 4);
    assert(t.offX == 20 + 80 && t.offZ == 120 && k.psFixed == 2);
    // (e) a rotation EA does not handle: untouched
    t = PsPaint(25, 30, 20, 30, 7, 4);
    assert(t.offX == -1 && t.offZ == -1 && k.psImports == 9 && k.psFixed == 2);

    // (f) PsPatch: argument block and EA's call checked before writing, then the call is ours
    static u8 site[PS_CALL + 5];
    const DWORD fake = (DWORD)(uintptr_t)&PsFakeXform;
    memcpy(site, kPsSite, sizeof(kPsSite));
    site[PS_CALL] = 0xE8;
    *(LONG*)(site + PS_CALL + 1) = (LONG)(fake - ((uintptr_t)site + PS_CALL + 5));
    site[0x1C] = 0x02;  // `lea edx,[edx*4+2]`: refused, nothing written
    assert(!PsPatch(site, fake) && (uintptr_t)site + PS_CALL + 5 + *(LONG*)(site + PS_CALL + 1) == fake);
    site[0x1C] = 0x01;
    assert(PsPatch(site, fake) && g_psXform == PsFakeXform);
    assert(site[PS_CALL] == 0xE8 && (uintptr_t)site + PS_CALL + 5 + *(LONG*)(site + PS_CALL + 1) == (uintptr_t)&PsXform);
    assert(!PsPatch(site, fake));  // ours now

    g_psXform = sXf;
    k.psImports = k.psFixed = 0;
    printf("selftest paintshift ok (paint offsets = 4 x object offsets: odd centred case fixed, others unchanged)\n");
}
