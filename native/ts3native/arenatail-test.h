// TestArenaTail: docs/67 §22.7, [ArenaGuard] TailPad and [GcRootCheck] TailWatch (arenatail.cpp) without the game.
// (1) the six pad patches on a fake copy of the 0xD71000 page: all applied, none when one site differs; (2) the rewritten
// own-chunk tail (0xD719EA, 15 bytes) and the original run on a fake chunk: same top, end 0x40 lower, same stack; (3) the
// grant arithmetic of the patched constants leaves >= 64 bytes before the descriptor for every frame size; (4) AtBump on
// fake chunks: fits / fits near the end (arms) / does not fit; (5) the debug-register watch on this thread: a write at
// end+4 is caught with its eip, a write below end is not, the same chunk does not re-arm, disarm stops it.
static u8 g_attPage[0x1000];
static u8* AttVa(DWORD va) { return g_attPage + (va - 0x00D71000); }
static __declspec(naked) void AttBack() { __asm mov eax, 1 __asm ret }
static __declspec(naked) void AttSlow() { __asm mov eax, 2 __asm ret }
#pragma warning(push)
#pragma warning(disable : 4100)  // naked: the arguments are read from the stack
static __declspec(naked) int __cdecl AttCallBump(DWORD chunk, DWORD rm) {
    __asm {
        push edi
        mov eax, dword ptr [esp + 8]
        mov edi, dword ptr [esp + 12]
        call AtBump
        pop edi
        ret
    }
}
#pragma warning(pop)
static __declspec(noinline) void AttStore(volatile DWORD* p, DWORD v) { *p = v; }

// runs seq (len bytes) with eax = chunk, esi = request, two dummy argument words on the stack (as after 0xD719E5's call)
static void AttRunTail(const u8* seq, int len, DWORD chunk, DWORD request) {
    u8* x = (u8*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    int n = 0;
    const u8 pre[] = {0x56, 0x57, 0x6A, 0x00, 0x6A, 0x00};  // push esi; push edi; push 0; push 0
    memcpy(x + n, pre, sizeof(pre)), n += sizeof(pre);
    x[n++] = 0xB8, memcpy(x + n, &chunk, 4), n += 4;    // mov eax, chunk
    x[n++] = 0xBE, memcpy(x + n, &request, 4), n += 4;  // mov esi, request
    memcpy(x + n, seq, len), n += len;
    x[n++] = 0x5F, x[n++] = 0x5E, x[n++] = 0xC3;  // pop edi; pop esi; ret (the sequence dropped the two words)
    ((void(__cdecl*)())(void*)x)();
    VirtualFree(x, 0, MEM_RELEASE);
}

// (6) [ArenaGuard] InitobjFix (docs/67 §22.9) with EA's layout rule on fake classes: value size = field offset (header 4
// rounded up to the field's alignment) + size - 4, storage = mono_type_size. Reproduces the overrun: List<ulong>.RemoveAt's
// `initobj` on an 8-byte last local of a frame that ends at the chunk's descriptor zeroes it with EA's size (12), not with ours.
struct AttKlass { u8 pad[0xA6]; u8 type; u8 pad2; int vtSize; };
static int __cdecl AttTypeSize(void* t, int* align) {
    const AttKlass* k = (const AttKlass*)((u8*)t - 0xA0);
    switch (k->type) {
    case 0x0A: case 0x0B: case 0x0D: return *align = 8, 8;  // I8 U8 R8 (0xE9C473 / 0xE9C483)
    case 0x08: case 0x09: case 0x0C: return *align = 4, 4;  // I4 U4 R4
    case 0x04: case 0x05: return *align = 1, 1;             // I1 U1
    default: return *align = 4, k->vtSize;                  // VALUETYPE: its own value size
    }
}
static int __cdecl AttValueSize(void* klass, int* align) {
    const AttKlass* k = (const AttKlass*)klass;
    if (k->type == 0x11) return k->vtSize;
    int a = 0;
    const int s = AttTypeSize((u8*)klass + 0xA0, &a);
    if (align) *align = a;
    return ((4 + a - 1) & ~(a - 1)) + s - 4;  // instance_size - sizeof(MonoObject) (4 in EA's Mono, 0xE6F5BB)
}
static void TestInitobjSize() {
    const AtSizeFn vs0 = g_atValueSize, ts0 = g_atTypeSize;
    g_atValueSize = AttValueSize, g_atTypeSize = AttTypeSize;
    AttKlass u8k = {}, i4k = {}, u1k = {}, vt = {}, var = {};
    u8k.type = 0x0B, i4k.type = 0x08, u1k.type = 0x04, vt.type = 0x11, vt.vtSize = 12, var.type = 0x13;
    assert(AttValueSize(&u8k, NULL) == 12);  // EA's size for UInt64: the bug
    const LONG cl0 = g_atInitobjClamped;
    assert(AtInitobjSize(&u8k, NULL) == 8 && AtInitobjSize(&i4k, NULL) == 4 && AtInitobjSize(&u1k, NULL) == 1);
    assert(AtInitobjSize(&vt, NULL) == 12 && AtInitobjSize(&var, NULL) == AttValueSize(&var, NULL));  // value types / open generics: EA's size
    assert(g_atInitobjClamped == cl0 + 1);
    // the crash: frame ends at c[1] = descriptor, its last local (8 bytes) right below
    static DWORD ck[0x200];
    const int end = 0x7FC / 4;
    for (int pass = 0; pass < 2; pass++) {
        ck[end] = 0x2A, ck[end - 1] = ck[end - 2] = 0xEEEEEEEE;
        memset(&ck[end - 2], 0, pass ? (size_t)AtInitobjSize(&u8k, NULL) : (size_t)AttValueSize(&u8k, NULL));  // MINT_INITOBJ 0xE589F7
        assert(ck[end - 1] == 0 && ck[end - 2] == 0 && ck[end] == (pass ? 0x2Au : 0u));
    }
    // the call-site patch on an executable copy: push 0; push klass; call valuesize; add esp,8; ret
    u8* x = (u8*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    x[0] = 0x6A, x[1] = 0, x[2] = 0x68, *(DWORD*)(x + 3) = (DWORD)(uintptr_t)&u8k, x[7] = 0xE8;
    *(DWORD*)(x + 8) = (DWORD)(uintptr_t)AttValueSize - ((DWORD)(uintptr_t)x + 12);
    x[12] = 0x83, x[13] = 0xC4, x[14] = 8, x[15] = 0xC3;
    typedef int(__cdecl * AttRun)();
    assert(((AttRun)(void*)x)() == 12);
    assert(!AtApplyInitobj(x + 7, (const void*)AttTypeSize) && ((AttRun)(void*)x)() == 12);  // other target: nothing written
    assert(AtApplyInitobj(x + 7, (const void*)AttValueSize) && ((AttRun)(void*)x)() == 8);
    assert(!AtApplyInitobj(x + 7, (const void*)AttValueSize));  // second apply finds our call
    VirtualFree(x, 0, MEM_RELEASE);
    g_atValueSize = vs0, g_atTypeSize = ts0;
    printf("initobj size: UInt64 12 -> 8, I4/U1/value types/open generics unchanged, chunk-end descriptor kept, call-site patch exact ok\n");
}

static void TestArenaTail() {
    // (1) pad patches
    memset(g_attPage, 0xCC, sizeof(g_attPage));
    for (const AtPatch& p : kAtPatches) memcpy(AttVa(p.at), p.old, p.len);
    g_attPage[0x1000 - 1] = 0;
    AttVa(0x00D71AEA)[1] = 0xFD;  // one site differs: nothing written
    assert(!AtApplyPad(AttVa) && AttVa(0x00D718CC)[2] == 0xFC);
    AttVa(0x00D71AEA)[1] = 0xFC;
    assert(AtApplyPad(AttVa));
    for (const AtPatch& p : kAtPatches) assert(memcmp(AttVa(p.at), p.neu, p.len) == 0);
    assert(!AtApplyPad(AttVa));  // a second apply finds the new bytes: no double patch
    // (2) own-chunk tail: request = size + 0xC (+ pad after the 0xD71946 patch)
    static DWORD ck[0x300];
    const DWORD c = (DWORD)(uintptr_t)ck, size = 0x900;
    memset(ck, 0, sizeof(ck));
    AttRunTail(kAtPatches[2].old, 15, c, size + 0xC);
    assert(ck[0] == c + 0xC && ck[1] == c + size + 0xC);
    memset(ck, 0, sizeof(ck));
    AttRunTail(kAtPatches[2].neu, 15, c, size + 0xC + AT_PAD);
    assert(ck[0] == c + 0xC && ck[1] == c + size + 0xC);  // frame end = c[1]; the block (size + 0x4C, +4 descriptor) ends 0x40+ beyond
    // (3) grant arithmetic with the patched constants: pool chunk when size + 0xC + 0x40 <= 0x7FC (0xD71946 / 0xD71961),
    // end 0x7BC, descriptor at 0x7FC; own chunk of size + 0x4C (descriptor at >= its end) with end = size + 0xC
    for (DWORD s = 0; s <= 0x1000; s += 4) {
        const bool pool = s + 0xC + AT_PAD <= 0x7FC;
        const DWORD end = pool ? 0x7BCu : s + 0xC, desc = pool ? 0x7FC : ((s + 0xC + AT_PAD + 4 + 7) & ~7u) - 4;
        assert(0xC + s <= end && desc >= end + AT_PAD);
        assert(pool || s + 0xC != 0x7BC);  // release (0xD71A4E / 0xD71AEA / 0xD71B88): own chunks are never mistaken for pool chunks
    }
    // (4) AtBump: chunk {top, end}, rm +0x38 = frame size; back -> 1, grow -> 2
    const DWORD back0 = g_atBack, slow0 = g_atSlow;
    g_atBack = (DWORD)(uintptr_t)AttBack, g_atSlow = (DWORD)(uintptr_t)AttSlow;
    g_atVeh = AddVectoredExceptionHandler(1, AtVeh);
    static DWORD k1[0x200], k2[0x200];
    static u8 rm[0x40];
    const DWORD a1 = (DWORD)(uintptr_t)k1, a2 = (DWORD)(uintptr_t)k2;
    k1[0] = a1 + 0x100, k1[1] = a1 + 0x7BC;
    k2[0] = a2 + 0x790, k2[1] = a2 + 0x7BC;
    *(WORD*)(rm + 0x38) = 0x28;
    const LONG near0 = g_atNear, arms0 = g_atArms, hits0 = g_atHits;
    g_atHitLines = AT_DIST;  // count only: the fake rm names nothing
    assert(AttCallBump(a1, (DWORD)(uintptr_t)rm) == 1 && g_atNear == near0);  // plenty left: no arm
    assert(AttCallBump(a2, (DWORD)(uintptr_t)rm) == 1 && g_atNear == near0 + 1 && g_atArms == arms0 + 1);  // 0x2C - 0x28 = 4 left
    assert(g_atArmChunk == a2 && g_atArmAddr == a2 + 0x7BC && g_atArmBase == a2 + 0x790 && g_atArmRm == (DWORD)(uintptr_t)rm);
    *(WORD*)(rm + 0x38) = 0x30;
    assert(AttCallBump(a2, (DWORD)(uintptr_t)rm) == 2 && g_atNear == near0 + 1);  // does not fit: EA's grow path
    // (5) the watch on this thread
    AttStore(&k2[0x7BC / 4 - 1], 7);  // below end: a frame's own word
    assert(g_atHits == hits0);
    AttStore(&k2[0x7BC / 4 + 1], 0);  // end + 4: past every frame
    assert(g_atHits == hits0 + 1 && g_atLastAddr == a2 + 0x7C0 && g_atLastEip > (DWORD)(uintptr_t)AttStore &&
           g_atLastEip < (DWORD)(uintptr_t)AttStore + 0x20);
    const LONG dist0 = g_atDistN;
    AttStore(&k2[0x7BC / 4], 0);  // end + 0: the descriptor, same eip and method = same distinct entry
    assert(g_atHits == hits0 + 2 && g_atLastAddr == a2 + 0x7BC && g_atDistN == dist0 && g_atDist[dist0 - 1].n == 2);
    AttStore(&k2[0x7BC / 4 + 2], 0);  // end + 8: no longer watched (only end and end + 4)
    assert(g_atHits == hits0 + 2);
    // EA's next-chunk header writes (trap eip = one past the write) are filtered at end + 4 only
    assert(AtHeaderWriter(0x00E563D3, 4) && AtHeaderWriter(0x00E54453, 4) && AtHeaderWriter(0x00D71A14, 4) && AtHeaderWriter(0x00D71BFF, 4));
    assert(!AtHeaderWriter(0x00E563D3, 0) && !AtHeaderWriter(0x00D71A14, 0) && !AtHeaderWriter(0x00D71C00, 4) && !AtHeaderWriter(0x00E563D1, 4));
    *(WORD*)(rm + 0x38) = 0x28;
    assert(AttCallBump(a2, (DWORD)(uintptr_t)rm) == 1 && g_atArms == arms0 + 1);  // same chunk: not re-armed
    AtArm(0);
    AttStore(&k2[0x7BC / 4 + 1], 0);
    assert(g_atHits == hits0 + 2);
    EXCEPTION_RECORD er = {AT_ARM_CODE};
    CONTEXT cx = {};
    EXCEPTION_POINTERS ep = {&er, &cx};
    assert(AtOwnsXcpt(&ep));  // XcptLog skips our arm requests
    RemoveVectoredExceptionHandler(g_atVeh), g_atVeh = NULL;
    g_atBack = back0, g_atSlow = slow0, g_atArmChunk = g_atArmAddr = 0, g_atHitLines = 0, g_atDistN = 0;
    printf("arena tail: pad patches all-or-none, own-chunk tail end-0x40, grants keep 64 B before the descriptor, bump near-end arm, DR watch hit "
           "at end/end+4 only, distinct (eip, method) table, EA next-chunk header writers filtered ok\n");
    TestInitobjSize();
}
