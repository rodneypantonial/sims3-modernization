// TestGcRoots: docs/67 §22.4, the Crash S task-rooting walkers (gcroots.cpp) on fake structures: EA's task container
// (101 buckets, {next, state} nodes), task states with arena chains (chunk +0 top, +4 end, +8 previous), libgc block
// headers (+0x10 map, +0x14 kind, +0x1C mark bits, +0x9C free bits) and an interpreter context with three frames.
static DWORD g_grtCtx[4];
static const u8* __cdecl GrtCtx() { return (const u8*)g_grtCtx; }

static void GrtBit(DWORD* hdr, DWORD addr, DWORD off) {  // set addr's word bit in the mark (0x1C) or free (0x9C) bits
    const DWORD w = (addr & 0xFFF) >> 2;
    hdr[(off + (w >> 5) * 4) / 4] |= 1u << (w & 31);
}

static void TestGcRoots() {
    u8* heap = (u8*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);  // 64 KB aligned: one 4 MB slot
    const DWORD H = (DWORD)(uintptr_t)heap;
    static DWORD top[1024], bot[1024], hdr[3][0x48], tbl[0x1A0 / 4], slot, curVar, hostVar, invalid = 0xBAD, dummy;
    static u8 host[0xC00], cls[0x40], meth[0x20], gmap[0x1000];  // map all 0: every word is an object start
    memset(top, 0, sizeof(top)), memset(bot, 0, sizeof(bot)), memset(hdr, 0, sizeof(hdr)), memset(tbl, 0, sizeof(tbl));
    top[H >> 22] = (DWORD)(uintptr_t)bot;
    for (int p = 0; p < 3; p++) {
        bot[((H + p * 0x1000) >> 12) & 0x3FF] = (DWORD)(uintptr_t)hdr[p];
        hdr[p][0] = 512, hdr[p][4] = (DWORD)(uintptr_t)gmap, ((u8*)hdr[p])[0x14] = 1;  // 2 KB objects, map != invalid, kind 1
    }
    // page 0: states S1-S4, nodes, receiver; page 1: S1's chunks A (current) and B (previous); page 2: C (S2), D (S3)
    DWORD* S[4];
    for (int i = 0; i < 4; i++) S[i] = (DWORD*)(heap + i * 0x200);
    DWORD *A = (DWORD*)(heap + 0x1000), *B = (DWORD*)(heap + 0x1800), *C = (DWORD*)(heap + 0x2000), *D = (DWORD*)(heap + 0x2800);
    const DWORD a = (DWORD)(uintptr_t)A, b = (DWORD)(uintptr_t)B, c = (DWORD)(uintptr_t)C, d = (DWORD)(uintptr_t)D;
    A[0] = a + 0x100, A[1] = a + 0x7FC, A[2] = b;
    B[0] = b + 0x200, B[1] = b + 0x7FC, B[2] = 0;
    C[0] = c + 0x900, C[1] = c + 0x7FC, C[2] = 0;  // top past its end: corrupt chain
    D[0] = d + 0x40, D[1] = d + 0x7FC, D[2] = 0;
    GrtBit(hdr[2], d, 0x9C);  // D on a GC free list
    S[0][0x13C / 4] = a, S[1][0x13C / 4] = c, S[2][0x13C / 4] = d, S[3][0x13C / 4] = 0;
    *(const char**)(cls + 0x34) = "WorkInRabbitHole", *(const char**)(cls + 0x38) = "Sims3.Gameplay.Careers";
    *(u8**)(meth + 8) = cls, *(const char**)(meth + 0x18) = "InRabbitHole";
    S[1][0] = (DWORD)(uintptr_t)meth;
    for (int i = 0; i < 3; i++) {  // S1-S3 in the container (as 0xD727F0: head insert into bucket state % 101)
        DWORD* nd = (DWORD*)(heap + 0x800 + i * 8);
        const DWORD s = (DWORD)(uintptr_t)S[i];
        nd[1] = s, nd[0] = tbl[s % 101], tbl[s % 101] = (DWORD)(uintptr_t)nd;
    }
    tbl[0x198 / 4] = 3;
    const DWORD recv = H + 0x900;
    GrtBit(hdr[0], recv, 0x9C);  // the freed delegate
    *(DWORD*)(host + 0xB84) = (DWORD)(uintptr_t)tbl, *(DWORD**)(host + 0xBC8) = &slot, *(DWORD*)(host + 0xBD0) = 7;
    slot = curVar = (DWORD)(uintptr_t)S[0], hostVar = (DWORD)(uintptr_t)host;
    // frames: f0 in A below its top, f1 in B below its top, f2 in A above its top (not scanned)
    static DWORD f[3][10];
    f[0][0] = (DWORD)(uintptr_t)f[1], f[1][0] = (DWORD)(uintptr_t)f[2], f[2][0] = 0;
    f[0][4] = a + 0x20, f[1][4] = b + 0x20, f[2][4] = a + 0x120;
    g_grtCtx[2] = (DWORD)(uintptr_t)f[0];

    const MxAddrs mxSave = g_mx;
    DWORD* hostSave = g_hostPtr;
    DWORD* curSave = g_curState;
    const u8* (__cdecl* ctxSave)() = g_lgCtx;
    g_mx = {top, &dummy, &invalid, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy};
    g_hostPtr = &hostVar, g_curState = &curVar, g_lgCtx = GrtCtx;

    int k;
    assert(GrBlock(recv, &k) == 'F' && k == 1 && GrBlock(a, &k) == '-' && GrBlock(0x7F000000, &k) == '?');
    GrtBit(hdr[0], (DWORD)(uintptr_t)S[0], 0x1C);
    assert(GrBlock((DWORD)(uintptr_t)S[0], &k) == 'M');
    hdr[2][4] = invalid;
    assert(GrBlock(c, &k) == 'B');
    hdr[2][4] = (DWORD)(uintptr_t)gmap;
    assert(GrInTable(tbl, (DWORD)(uintptr_t)S[0]) == 1 && GrInTable(tbl, (DWORD)(uintptr_t)S[3]) == 0 && GrInTable(NULL, 1) == -1);
    DWORD t = 0;
    assert(GrChunkOf((DWORD)(uintptr_t)S[0], b + 0x20, &t) == 1 && t == b + 0x200 && GrChunkOf((DWORD)(uintptr_t)S[0], c + 0x20, &t) == -1);
    int nc;
    DWORD bad;
    assert(!GrChainBad((DWORD)(uintptr_t)S[0], &nc, &bad) && nc == 2);
    assert(GrChainBad((DWORD)(uintptr_t)S[1], &nc, &bad) && bad == c);
    assert(GrChainBad((DWORD)(uintptr_t)S[2], &nc, &bad) && bad == d);

    // [GcRootCheck] at a stopped mark: S2's top, S3's freed chunk, the running task's frame f2 above its top
    g_grLines = 0;
    assert(GrOnGc() == 3 && g_grGcs >= 1 && g_grGcsInTask >= 1 && g_grStatesMax >= 3 && g_grChunksMax >= 4);
    slot = (DWORD)(uintptr_t)S[3];  // running task's root not in the container (+ f2 still above top)
    assert(GrOnGc() == 4);
    slot = (DWORD)(uintptr_t)S[0];
    D[0] = d + 0x40, memset((u8*)hdr[2] + 0x9C, 0, 0x80), C[0] = c + 0x100, f[2][4] = a + 0x40;  // healthy world: nothing to report
    assert(GrOnGc() == 0);

    // docs/67 §22.5 frame audit after the mark: chunks marked and ending in the descriptor, every frame word's object
    // marked or free-listed = nothing; then one unmarked object, one overwritten descriptor, one unreached chunk
    static DWORD fakeDesc = 0x00000D5C;
    const DWORD* descSave = g_grDesc;
    g_grDesc = &fakeDesc;
    for (DWORD* ck : {A, B, C, D}) ck[0x7FC / 4] = fakeDesc;
    GrtBit(hdr[1], a, 0x1C), GrtBit(hdr[1], b, 0x1C), GrtBit(hdr[2], c, 0x1C), GrtBit(hdr[2], d, 0x1C);
    A[4] = recv;  // free-listed: the marker skips it too, not a miss
    const LONG faults0 = g_grAuditFaults;
    assert(GrAudit() == 0 && g_grWords > 0);
    A[5] = H + 0xA00;  // a frame word to an object nobody marked: swept while held
    hdr[0][0x18 / 4] = hdr[1][0x18 / 4] = hdr[2][0x18 / 4] = 5;  // blocks stamped before this cycle (GC_gc_no = dummy = 0)
    const LONG um0 = g_grUnmarked, win0 = g_grWindow;
    assert(GrAudit() == 1 && g_grUnmarked == um0 + 1);
    hdr[0][0x18 / 4] = dummy;  // docs/67 §22.6: block stamped this cycle (allocated after the mark): EA keeps it, not a miss
    assert(GrAudit() == 0 && g_grUnmarked == um0 + 1 && g_grWindow == win0 + 1);
    GrtBit(hdr[0], H + 0xA00, 0x1C);
    assert(GrAudit() == 0);
    D[0x7FC / 4] = 0x00000101;  // a frame value over D's descriptor
    assert(GrAudit() == 1 && g_grDescBad >= 1);
    D[0x7FC / 4] = fakeDesc;
    hdr[1][0x1C / 4 + ((b & 0xFFF) >> 7)] &= ~(1u << (((b & 0xFFF) >> 2) & 31));  // B not reached from S1's A
    assert(GrAudit() == 2 && g_grChunkUnmarked >= 1);  // + A's previous-chunk link (A+8) to the unmarked B
    GrtBit(hdr[1], b, 0x1C);
    assert(GrAudit() == 0 && g_grAuditFaults == faults0);  // every walk ran to its end

    // docs/67 §22.6 descriptor watch: S1-S3 chains sound (A, B, C, D), C also in EA's pool; A and C lose their descriptor
    static DWORD sent[3], pnode[3], cell[9];
    static u8 rm[0x40];
    DWORD* poolSave = g_grPool;
    const DWORD sentVar = (DWORD)(uintptr_t)sent;
    g_grPool = (DWORD*)&sentVar;
    sent[0] = (DWORD)(uintptr_t)pnode, pnode[0] = sentVar, pnode[2] = c;
    *(WORD*)(rm + 0x38) = 0x40, *(u8**)(rm + 8) = meth;
    cell[0] = (DWORD)(uintptr_t)rm, cell[8] = a + 0x7FC - 0x40;  // S1's saved frame ending at A's end
    S[0][0x3C / 4] = (DWORD)(uintptr_t)cell;
    assert(GrAudit() == 0);
    const LONG chk0 = g_grDescChecks, bad0 = g_grDescBadEv1;
    assert(GrOnGc(true) == 0);
    assert(g_grDescChecks == chk0 + 5 && g_grDescBadEv1 == bad0);  // A, B, C, D + C in the pool
    A[0x7FC / 4] = 0, C[0x7FC / 4] = 0;
    g_grRestoreDesc = 0;
    assert(GrOnGc(true) == 0);
    assert(g_grDescBadEv1 == bad0 + 3 && g_grDescPool >= 1 && A[0x7FC / 4] == 0);  // A (S1), C (S2), C (pool)
    char cells[200], wantc[120];
    GrCellsText(cells, sizeof(cells), (DWORD)(uintptr_t)S[0], a, a + 0x7FC);
    sprintf_s(wantc, "0:Sims3.Gameplay.Careers.WorkInRabbitHole.InRabbitHole %08lX+40*", a + 0x7FC - 0x40);
    assert(strstr(cells, wantc));
    GrCellsText(cells, sizeof(cells), (DWORD)(uintptr_t)S[0], b, b + 0x7FC);
    assert(!strcmp(cells, "none"));
    g_grRestoreDesc = 1;
    const LONG fix0 = g_grDescFixed;
    assert(GrOnGc(true) == 0);
    assert(g_grDescFixed == fix0 + 2 && A[0x7FC / 4] == fakeDesc && C[0x7FC / 4] == fakeDesc);
    const LONG bad1 = g_grDescBadEv1;
    assert(GrOnGc(true) == 0);
    assert(g_grDescBadEv1 == bad1 && g_grDescFixed == fix0 + 2);
    // docs/67 §22.6.1 bounds (event 1 runs inside EA's ~1 ms stopped mark): a pool cycle that misses the sentinel stops at
    // 4096 nodes, a container bucket cycle at GR_STATES_MAX states, an unreadable chunk is reported without VirtualQuery
    pnode[0] = (DWORD)(uintptr_t)pnode;
    const LONG chk1 = g_grDescChecks, cap0 = g_grDescPoolCapped;
    assert(GrOnGc(true) == 0 && g_grDescChecks == chk1 + 4 + 4096 && g_grDescPoolCapped == cap0 + 1);
    pnode[0] = sentVar;
    DWORD b0 = 0;
    while (tbl[b0]) b0++;  // an empty bucket (3 states in 101)
    static DWORD loopNd[2];
    loopNd[0] = (DWORD)(uintptr_t)loopNd, loopNd[1] = (DWORD)(uintptr_t)S[0], tbl[b0] = (DWORD)(uintptr_t)loopNd;
    const LONG chk2 = g_grDescChecks;
    assert(GrOnGc(true) == 1 && g_grStatesMax == GR_STATES_MAX && g_grDescChecks - chk2 <= 2 * GR_STATES_MAX + 1);
    assert(GrAudit() == 0);  // its walk stops at the cap too
    tbl[b0] = 0;
    S[3][0x13C / 4] = 0x10;
    assert(!strcmp(GrChainBad((DWORD)(uintptr_t)S[3], &nc, &bad), "chunk unreadable") && bad == 0x10);
    S[3][0x13C / 4] = 0;
    g_grRestoreDesc = 0, g_grPool = poolSave, S[0][0x3C / 4] = 0;
    hdr[0][0x18 / 4] = 41;  // block stamp for the fault line

    C[0] = c + 0x900, GrtBit(hdr[2], d, 0x9C), f[2][4] = a + 0x120;

    // the fault line: current S1, outer S4 ([ebp-0x60], not in the container), root S1; frames; receiver on a free list
    static DWORD frame[32];
    CONTEXT ctx = {};
    ctx.Eip = 0x00E5201F, ctx.Ebp = (DWORD)(uintptr_t)(frame + 28);
    frame[28 - 0x60 / 4] = (DWORD)(uintptr_t)S[3], frame[28 - 0x28 / 4] = recv;
    char line[1500], want[160];
    assert(GrFaultText(&ctx, line, sizeof(line)));
    sprintf_s(want, "current %08lX (container 1, gc M/k1; chain %08lX top %08lX end %08lX < %08lX", (DWORD)(uintptr_t)S[0], a, a + 0x100, a + 0x7FC, b);
    assert(strstr(line, "mono fault task: container ") && strstr(line, "(3 states), task 0000000000000007") && strstr(line, want));
    sprintf_s(want, "; outer %08lX (container 0, gc -/k1; chain empty)", (DWORD)(uintptr_t)S[3]);
    assert(strstr(line, want));
    sprintf_s(want, "%08lX:c0 %08lX:c1 %08lX:c0!; 1 above top; receiver %08lX gc F/k1", a + 0x20, b + 0x20, a + 0x120, recv);
    assert(strstr(line, want));
    assert(strstr(line, " (mark bit 0, block stamp 41, gc ") && strstr(line, "; current chain descriptors ok ok"));
    ctx.Eip = 0x00E54AEC, ctx.Ebp = 0x10;  // outside the callvirt helper, unreadable frame: still a line, no receiver
    assert(GrFaultText(&ctx, line, sizeof(line)) && strstr(line, "receiver 00000000 gc ?/k-1"));
    ctx.Eip = 0x00401000;
    assert(!GrFaultText(&ctx, line, sizeof(line)));
    char small[64];  // truncates, never aborts
    ctx.Eip = 0x00E5201F, ctx.Ebp = (DWORD)(uintptr_t)(frame + 28);
    assert(GrFaultText(&ctx, small, sizeof(small)) && strlen(small) == sizeof(small) - 1);

    g_mx = mxSave, g_hostPtr = hostSave, g_curState = curSave, g_lgCtx = ctxSave, g_grDesc = descSave;
    g_grLines = 0, g_grGcs = g_grGcsInTask = g_grMismatch = g_grStatesMax = g_grChunksMax = 0;
    g_grAuditLines = 0, g_grAudits = g_grWords = g_grUnmarked = g_grChunkUnmarked = g_grDescBad = g_grAuditFaults = 0;
    g_grWindow = g_grDescChecks = g_grDescBadEv1 = g_grDescPool = g_grDescFixed = g_grDescLines = g_grDescPoolCapped = 0;
    VirtualFree(heap, 0, MEM_RELEASE);
    printf("gc roots: container membership, chain checks, frames above top, frame-word mark audit (stamp split), descriptor watch/restore and the fault line on fake structures\n");
}
