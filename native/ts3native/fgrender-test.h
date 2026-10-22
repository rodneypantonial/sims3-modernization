// TestForegroundRender ([Harness] ForegroundRender, fgrender.cpp, docs/162 §7). Included by selftest.cpp.
// Dummies hold the two sites' original bytes; a third holds S3SS-style foreign bytes and must stay untouched.
static void TestForegroundRender() {
    static u8 gate[8] = {0x74, 0x09, 0x84, 0xDB, 0x75, 0x05, 0xCC, 0xCC};      // je +9; test bl,bl; jne +5
    static u8 sleep[8] = {0xE8, 0x4A, 0x73, 0x61, 0xFF, 0x83, 0xC4, 0x04};     // call 0x4E1320; add esp,4
    static const u8 gatePatched[8] = {0xEB, 0x09, 0x84, 0xDB, 0x75, 0x05, 0xCC, 0xCC};
    static const u8 sleepPatched[8] = {0x0F, 0x1F, 0x44, 0x00, 0x00, 0x83, 0xC4, 0x04};
    u8 gateOrig[8], sleepOrig[8];
    memcpy(gateOrig, gate, 8), memcpy(sleepOrig, sleep, 8);

    u8* const at[2] = {gate, sleep};
    assert(FgPatch(at) == 3);
    assert(!memcmp(gate, gatePatched, 8) && !memcmp(sleep, sleepPatched, 8));
    assert(FgPatch(at) == 0);  // second apply: bytes are ours now, nothing written
    FgRevert();
    assert(!memcmp(gate, gateOrig, 8) && !memcmp(sleep, sleepOrig, 8));

    static u8 foreign[8] = {0xEB, 0x09, 0x84, 0xDB, 0x75, 0x05, 0xCC, 0xCC};  // someone else already owns the gate
    u8* const at2[2] = {foreign, sleep};
    assert(FgPatch(at2) == 2);
    FgRevert();
    assert(foreign[0] == 0xEB && !memcmp(sleep, sleepOrig, 8));
    printf("ForegroundRender: both sleep sites patched and reverted on dummies, foreign bytes left alone: ok\n");
}
