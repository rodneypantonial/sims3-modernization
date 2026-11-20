// TestModTime ([Diag] ModTime, modtime.cpp, docs/298). Included by selftest.cpp. Host checks of the parts with logic: MVID
// from a metadata root, owner kinds and the frame pick, ordering, the hitch list.
static void TestModTime() {
    // a fake S3SA blob: junk, then a metadata root (version "v2.0.50727" padded to 12) with #~, #GUID streams
    static u8 b[512] = {};
    memset(b, 0xCC, 64);
    u8* r = b + 64;
    memcpy(r, "BSJB", 4);
    *(WORD*)(r + 4) = 1, *(WORD*)(r + 6) = 1;
    *(DWORD*)(r + 12) = 12;
    memcpy(r + 16, "v2.0.50727\0\0", 12);
    u8* p = r + 28;
    *(WORD*)(p + 2) = 2;  // streams
    p += 4;
    *(DWORD*)p = 0x100, *(DWORD*)(p + 4) = 8, memcpy(p + 8, "#~\0\0", 4), p += 12;
    *(DWORD*)p = 0x120, *(DWORD*)(p + 4) = 16, memcpy(p + 8, "#GUID\0\0\0", 8), p += 16;
    for (int i = 0; i < 16; i++) r[0x120 + i] = (u8)(0xA0 + i);
    u8 mv[16] = {};
    assert(MdtMvidOf(b, sizeof(b), mv) && mv[0] == 0xA0 && mv[15] == 0xAF);
    assert(!MdtMvidOf(b, 64, mv));                       // no root in range
    u8 none[64] = {};
    assert(!MdtMvidOf(none, sizeof(none), mv));
    static u8 cut[64 + 0x110];                           // root found, #GUID past the end: no read past n
    memcpy(cut, b, sizeof(cut));
    assert(!MdtMvidOf(cut, sizeof(cut), mv));

    // kinds and the pick: innermost mod, else innermost ours, else innermost (game); none = -1
    assert(MdtKindOf("Sims3GameplaySystems") == MDT_GAME && MdtKindOf("mscorlib") == MDT_GAME && MdtKindOf("ScriptCore") == MDT_GAME);
    assert(MdtKindOf("Ts3Fixes") == MDT_OURS && MdtKindOf("NRaasStoryProgression") == MDT_MOD && MdtKindOf("SystemTweaks") == MDT_MOD);
    const int k1[4] = {MDT_GAME, MDT_GAME, MDT_MOD, MDT_GAME};
    assert(MdtPick(k1, 4) == 2);
    const int k2[3] = {MDT_GAME, MDT_OURS, MDT_GAME};
    assert(MdtPick(k2, 3) == 1);
    const int k3[3] = {MDT_GAME, MDT_MOD, MDT_OURS};  // a mod outranks ours wherever it sits
    assert(MdtPick(k3, 3) == 1);
    const int k4[2] = {MDT_GAME, MDT_GAME};
    assert(MdtPick(k4, 2) == 0 && MdtPick(k4, 0) == -1);

    // ordering: largest first, zeros never listed
    const double w[6] = {1, 0, 5, 3, 5, 0.5};
    int idx[4], got;
    MdtOrder(w, 6, idx, 4, &got);
    assert(got == 4 && w[idx[0]] == 5 && w[idx[1]] == 5 && idx[0] != idx[1] && idx[2] == 3 && idx[3] == 0);
    const double z[2] = {0, 0};
    MdtOrder(z, 2, idx, 4, &got);
    assert(got == 0);

    // hitch list keeps the 20 longest, longest first
    g_mdtHitN = 0;
    for (LONG i = 1; i <= 50; i++) { MdtHitch t = {}; t.ms = (i * 37) % 51; MdtHitchAdd(t); }
    assert(g_mdtHitN == 20 && g_mdtHit[0].ms == 50 && g_mdtHit[19].ms == 31);
    g_mdtHitN = 0;

    // S3SA decrypt (s3pi scheme): encrypt a fake image with the inverse chain, one flagged (zero) block, then decrypt
    {
        const DWORD count = 4, tlen = count * 8;
        static u8 clear[4 * 512], body[1 + 4 + 64 + 2 + 4 * 8 + 4 * 512];
        for (DWORD i = 0; i < sizeof(clear); i++) clear[i] = (u8)(i * 7 + 3);
        memset(clear + 2 * 512, 0, 512);  // block 2 flagged: stays zero, takes no input
        DWORD bp = 0;
        body[bp++] = 1;  // version 1: no string
        bp += 4 + 64;
        *(WORD*)(body + bp) = (WORD)count, bp += 2;
        u8* table = body + bp;
        for (DWORD i = 0; i < tlen; i++) table[i] = (u8)(i * 29 + 10);
        table[0] &= ~1, table[8] &= ~1, table[16] |= 1, table[24] &= ~1;
        bp += tlen;
        unsigned long long sum = 0;
        for (DWORD i = 0; i < tlen; i += 8) sum += *(const unsigned long long*)(table + i);
        DWORD seed = (DWORD)((unsigned long long)(tlen - 1) & sum), bw = bp;
        for (DWORD blk = 0; blk < count; blk++) {
            if (table[blk * 8] & 1) continue;
            for (DWORD j = 0; j < 512; j++) { const u8 c = clear[blk * 512 + j] ^ table[seed]; body[bw++] = c; seed = (seed + c) % tlen; }
        }
        DWORD on = 0;
        u8* out = MdtS3saDecrypt(body, bw, &on);
        assert(out && on == count * 512 && !memcmp(out, clear, on));
        free(out);
        assert(!MdtS3saDecrypt(body, 40, &on));  // truncated header
        assert(!MdtS3saDecrypt(body, bw - 100, &on));  // truncated data
    }
    // real packages (not in the repo): TS3_S3SA_FIXTURES = package paths separated by ';', each must give >= 1 MVID
    // (2026-11-20: NRaas_ErrorTrap / NRaas_Overwatch from the test Mods folder; their S3SA bodies are encrypted)
    {
        wchar_t list[2048];
        if (GetEnvironmentVariableW(L"TS3_S3SA_FIXTURES", list, 2048) && (g_mdtPkg = (MdtPkg*)calloc(4096, sizeof(MdtPkg))) != NULL) {
            int files = 0;
            for (wchar_t* ctx = NULL, *t = wcstok_s(list, L";", &ctx); t; t = wcstok_s(NULL, L";", &ctx)) {
                const int before = g_mdtPkgN;
                MdtScanFile(t, t);
                printf("modtime fixture: %ls: %d MVID(s)\n", t, g_mdtPkgN - before);
                assert(g_mdtPkgN > before);
                files++;
            }
            printf("modtime: %d real S3SA fixture package(s) ok\n", files);
            g_mdtPkgN = g_mdtPkgFiles = g_mdtPkgS3sa = 0;
            free(g_mdtPkg), g_mdtPkg = NULL;
        }
    }
}
