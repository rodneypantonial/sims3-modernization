// TestUfCam: docs/202 §2.1 camera queries (unseenfb.cpp UfCamMsg) on unseen-test.h's fake camera and frustum (on screen = |x| < 10).
static void TestUfCam() {
    const UnFrustumFn frSave = g_unFrustum;
    const UnInFrustumFn inSave = g_unInFrustum;
    const UnCamFn cpSave = g_unCamPos, ctSave = g_unCamTgt;
    g_unFrustum = UnFakeFrustum, g_unInFrustum = UnFakeIn, g_unCamPos = UnFakeCamPos, g_unCamTgt = UnFakeCamTgt;
    auto F = [](float f) { DWORD d; memcpy(&d, &f, 4); return d; };
    auto Ask = [&](float x, float y, float z, float r) {
        GetOptionHook(0, F(x), 0x83030000, 1), GetOptionHook(0, F(y), 0x83040000, 1), GetOptionHook(0, F(z), 0x83050000, 1);
        return GetOptionHook(0, F(r), 0x83060000, 1);
    };
    // camera 30 m above its target at the origin: reach 80 m
    g_unFakeCam[0][0] = 0, g_unFakeCam[0][1] = 30, g_unFakeCam[0][2] = 0;
    g_unFakeCam[1][0] = 0, g_unFakeCam[1][1] = 0, g_unFakeCam[1][2] = 0;
    g_unCam.ok = false;
    assert(Ask(200, 0, 0, 3) == 1);       // off screen, far: unseen
    assert(Ask(5, 0, 300, 3) == 0);       // on screen (|x| < 10) though far: seen
    assert(Ask(60, 0, 0, 3) == 0);        // off screen but inside the 80 m reach: seen
    assert(Ask(80, 0, 0, 3) == 0);        // the sphere's nearest point is inside the reach: seen
    assert(Ask(200, 0, 0, 150) == 0);     // a lot's bounding circle reaching into the camera's reach: seen
    GetOptionHook(0, F(200), 0x83030000, 1);
    assert(GetOptionHook(0, F(3), 0x83060000, 1) == 0);  // centre incomplete: seen (unknown)
    assert(Ask(200, 0, 0, -1) == 0);      // bad radius: seen
    // zoomed out: camera 130 m above the target, reach 260 m >= the 240 m wide limit: everything seen
    g_unFakeCam[0][1] = 130;
    g_unCam.ok = false;
    assert(Ask(2000, 0, 0, 3) == 0);
    // no camera: seen
    g_unFakeCam[0][1] = 30;
    g_unFrustum = []() -> void* { return NULL; };
    g_unCam.ok = false;
    assert(Ask(2000, 0, 0, 3) == 0);
    g_unFrustum = frSave, g_unInFrustum = inSave, g_unCamPos = cpSave, g_unCamTgt = ctSave;
    g_unCam.ok = false;
    printf("unseen fallback camera queries (docs/202 §2.1): frustum, reach with radius, wide camera, no camera, partial query ok\n");
}

// TestUfColour: docs/202 §3.5 (1) colour queries (unseenfb.cpp UfColourAt / UfColourMsg) on a fake routing world: R at
// world+0x54 with 2 x 1 chunks (32 m), origin (100, 200) at [world+0x964]+0xB4/+0xB8, waypoints with flood colours.
static void TestUfColour() {
    static u8 world[0x970], wm[0xC0], rec[64];
    static DWORD off[2], first[2];
    static signed char col[5];
    static u8* slot;
    u8* const* const slotSave = g_ufWorldSlot;
    memset(world, 0, sizeof world), memset(wm, 0, sizeof wm), memset(rec, 0, sizeof rec);
    // chunk 0 (x 100-132): waypoints at (+4, +4) colour 0, (+14, +14) colour 1, (+30, +2) type 0xE colour -1
    // chunk 1 (x 132-164): waypoint at (+8, +4) colour 0
    auto Rec = [](u8* r, u8 type, float bx, float bz) { r[0] = 0, r[1] = type, r[3] = (u8)(bx * 8), r[4] = (u8)(bz * 8); };
    u8* c0 = rec;
    *(unsigned short*)c0 = 3;
    ((unsigned short*)(c0 + 2))[0] = 8, ((unsigned short*)(c0 + 2))[1] = 13, ((unsigned short*)(c0 + 2))[2] = 18;
    Rec(c0 + 8, 1, 4, 4), Rec(c0 + 13, 1, 14, 14), Rec(c0 + 18, 0xE, 30, 2);
    u8* c1 = rec + 32;
    *(unsigned short*)c1 = 1;
    ((unsigned short*)(c1 + 2))[0] = 4;
    Rec(c1 + 4, 1, 8, 4);
    off[0] = 0, off[1] = 32, first[0] = 0, first[1] = 3;
    col[0] = 0, col[1] = 1, col[2] = -1, col[3] = 0;
    u8* R = world + 0x54;
    *(DWORD*)R = 2, *(DWORD*)(R + 4) = 1, *(DWORD**)(R + 8) = off, *(u8**)(R + 0xC) = rec, *(DWORD*)(R + 0x10) = sizeof rec;
    R[0x14] = 1;
    *(DWORD**)(R + 0x18) = first, *(DWORD**)(R + 0x1C) = first + 2;
    *(signed char**)(R + 0x28) = col, *(signed char**)(R + 0x2C) = col + 4;
    *(float*)(wm + 0xB4) = 100, *(float*)(wm + 0xB8) = 200;
    *(u8**)(world + 0x964) = wm;
    slot = world;
    g_ufWorldSlot = &slot;
    auto F = [](float f) { DWORD d; memcpy(&d, &f, 4); return d; };
    auto Ask = [&](int code, float x, float z) {
        GetOptionHook(0, F(x), 0x83030000, 1), GetOptionHook(0, F(z), 0x83050000, 1);
        return GetOptionHook(0, 0, 0x83000000 | (DWORD)code << 16, 1);
    };
    assert(UfColourAt(103, 203) == 0 && UfColourAt(115, 215) == 1);
    assert(UfColourAt(109, 209) == -3);     // 7.1 m from both colours: ambiguous
    assert(UfColourAt(129, 202) == 0);      // the type-0xE waypoint 1 m away is skipped; (140, 204) 11.2 m away counts
    assert(UfColourAt(300, 300) == -2);     // outside the chunks
    assert(Ask(11, 104, 204) == 1);          // A = colour 0
    assert(Ask(12, 141, 205) == 1 && Ask(13, 141, 205) == 0);   // same colour across the chunk edge
    assert(Ask(12, 114, 214) == 0 && Ask(13, 114, 214) == 1);   // proven different
    assert(Ask(12, 109, 209) == 0 && Ask(13, 109, 209) == 0);   // ambiguous: unknown
    assert(Ask(12, 400, 400) == 0 && Ask(13, 400, 400) == 0);   // no waypoint: unknown
    GetOptionHook(0, F(141), 0x83030000, 1);
    assert(GetOptionHook(0, 0, 0x83080000, 1) == 0);           // z missing: unknown
    assert(Ask(11, 400, 400) == 0 && Ask(12, 104, 204) == 0 && Ask(13, 114, 214) == 0);  // no A: unknown
    R[0x14] = 0;
    assert(UfColourAt(103, 203) == -2);     // colours not built
    R[0x14] = 1;
    slot = NULL;
    assert(Ask(11, 104, 204) == 0);          // no routing world
    g_ufWorldSlot = slotSave;
    printf("unseen fallback colour queries (docs/202 §3.5): same / different / ambiguous / none, chunk edge, no R ok\n");
}
