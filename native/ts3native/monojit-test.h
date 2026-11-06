// TestMonoJit ([MonoJit], monojit.cpp, docs/527 step 1). Included by selftest.cpp. No EA interpreter here: a fake opcode table
// whose entries stop the run (JitTestStop) stands in for EA's handlers, so compiled templates really execute: what a run must
// leave is the state an exit hands the interpreter (edi = ip, eax = op, esi, arena).
// Not covered: EA's real opcode numbers and handlers (the monohost corpus has those).

enum { JT_LDC2 = 1, JT_LDC3, JT_ADD, JT_STLOC, JT_LDLOC, JT_RET, JT_BRS, JT_BRTRUE, JT_LDFLD, JT_STFLD, JT_LDELEM, JT_LDFLDR4, JT_LDLOCVT, JT_LDCS, JT_MYSTERY, JT_LEAVES = 0x9C, JT_SWITCH = 0xE8 };
static const char* g_jtNames[0x1A9];
static BYTE g_jtLen[0x1C1];
static DWORD g_jtTable[0x1C1];
static DWORD g_jtEsp, g_jtEdi, g_jtEsi, g_jtEax, g_jtEbpIn, g_jtSpIn, g_jtIpIn;
static DWORD g_jtFr[64], g_jtArena[64], g_jtLocals[16], g_jtVt[8];
static BYTE g_jtInv[0x20], g_jtState[0x150];
static DWORD* const g_jtEbp = g_jtFr + 32;

static __declspec(naked) void JitTestStop() {
    __asm {
        mov g_jtEdi, edi
        mov g_jtEsi, esi
        mov g_jtEax, eax
        mov esp, g_jtEsp
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static __declspec(naked) void JitTestRun() {  // the interpreter's dispatch of the first word
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov g_jtEsp, esp
        mov ebp, g_jtEbpIn
        mov esi, g_jtSpIn
        mov edi, g_jtIpIn
        movzx eax, word ptr [edi]
        mov edx, eax
        mov ecx, g_jitTableP
        jmp dword ptr [ecx + eax * 4]
    }
}
static void __cdecl JtMemcpy(void* d, const void* s, size_t n) { memmove(d, s, n); }

static void JtFrame() {
    memset(g_jtFr, 0, sizeof(g_jtFr)), memset(g_jtArena, 0, sizeof(g_jtArena)), memset(g_jtLocals, 0, sizeof(g_jtLocals)), memset(g_jtVt, 0, sizeof(g_jtVt));
    memset(g_jtInv, 0, sizeof(g_jtInv)), memset(g_jtState, 0, sizeof(g_jtState));
    *(DWORD**)(g_jtInv + 0x10) = g_jtArena;
    g_jtEbp[-2] = (DWORD)(uintptr_t)g_jtInv;     // [ebp-8] MonoInvocation
    g_jtEbp[-3] = (DWORD)(uintptr_t)g_jtVt;      // [ebp-0xC] the vt cursor
    g_jtEbp[-5] = (DWORD)(uintptr_t)g_jtLocals;  // [ebp-0x14] locals
    g_jtEbp[2] = (DWORD)(uintptr_t)g_jtState;    // [ebp+8] the thread's interpreter state
}
static void JtGo(const unsigned short* ip, DWORD spWords = 0) {  // spWords: arena dwords already on the eval stack
    g_jtEbpIn = (DWORD)(uintptr_t)g_jtEbp, g_jtSpIn = (DWORD)(uintptr_t)(g_jtArena + spWords), g_jtIpIn = (DWORD)(uintptr_t)ip;
    JitTestRun();
}
static DWORD WINAPI JtThread(void*) { JitTestRun(); return 0; }

struct JtMeth { BYTE rm[0x48], mm[0x20], klass[0x40]; };
static void JtInit(JtMeth& m, unsigned short* code, int words, const char* name = "Meth") {
    memset(&m, 0, sizeof(m));
    *(unsigned short**)m.rm = code;
    *(BYTE**)(m.rm + 8) = m.mm;
    *(WORD*)(m.rm + 0x34) = 64;   // with +0x3E: where the eval stack ends (the verifier's dead area)
    *(WORD*)(m.rm + 0x38) = sizeof(g_jtArena);
    *(WORD*)(m.rm + 0x3A) = (WORD)words;
    *(WORD*)(m.rm + 0x3E) = 32;
    *(BYTE**)(m.mm + 8) = m.klass;
    *(const char**)(m.mm + 0x18) = name;
    *(const char**)(m.klass + 0x34) = "Cls";
    *(const char**)(m.klass + 0x38) = "Ns";
}
static void JtCompile(JtMeth& m, unsigned short* code, int words) {
    JtInit(m, code, words);
    assert(JitCompile(m.rm) == NULL);
}

static void TestMonoJit() {
    Ts3nCounters& C = ts3native_counters;
    // default: nothing installed without the switch, and the switch alone does nothing on another exe
    assert(!ApplyMonoJit(L"Z:\\no-such-dir\\none.ini") && !C.jitOn && !g_ilpPost);
    wchar_t ini[MAX_PATH + 32];
    GetTempPathW(MAX_PATH, ini);
    wcscat_s(ini, L"monojit-test.ini");
    WritePrivateProfileStringW(L"MonoJit", L"Enabled", L"1", ini);
    assert(!ApplyMonoJit(ini) && !C.jitOn && !g_ilpPost);  // the selftest exe is not Steam 1.67
    WritePrivateProfileStringW(L"MonoJit", L"Mode", L"3", ini);
    assert(!ApplyMonoJit(ini));
    DeleteFileW(ini);

    for (DWORD i = 0; i < 0x1C1; i++) g_jtTable[i] = (DWORD)(uintptr_t)JitTestStop, g_jtLen[i] = 1;
    g_jtTable[JIT_OP] = (DWORD)(uintptr_t)JitEnter;
    static const struct { int op; const char* name; int len; } kOps[] = {{JT_LDC2, "ldc.i4.2", 1}, {JT_LDC3, "ldc.i4.3", 1}, {JT_ADD, "add.i4", 1}, {JT_STLOC, "stloc.i4", 2},
        {JT_LDLOC, "ldloc.i4", 2}, {JT_RET, "ret", 1}, {JT_BRS, "br.s", 2}, {JT_BRTRUE, "brtrue.i4.s", 2}, {JT_LDFLD, "ldfld.i4", 2}, {JT_STFLD, "stfld.i4", 2},
        {JT_LDELEM, "ldelem.i4", 1}, {JT_LDFLDR4, "ldfld.r4", 2}, {JT_LDLOCVT, "ldloc.vt", 4}, {JT_LDCS, "ldc.i4.s", 2}, {JT_MYSTERY, "mystery", 1}, {JT_LEAVES, "leave.s", 2}, {JT_SWITCH, "switch", 3}};
    for (const auto& o : kOps) g_jtNames[o.op] = o.name, g_jtLen[o.op] = (BYTE)o.len;
    g_jitTableP = g_jtTable, g_jitOpNames = g_jtNames, g_jitOpLen = g_jtLen, g_jitMemcpy = (DWORD)(uintptr_t)JtMemcpy;
    assert(JitTemplates(255) == 15);  // every row above but "mystery" and leave.s
    memset(g_opInfo, 0, sizeof(g_opInfo));
    assert(JitTemplates(255 & ~1) == 14 && g_opInfo[JT_LDFLDR4].kind == K_EXIT);  // Groups: group 1 left out
    JitTemplates(255);
    assert(g_opInfo[JT_RET].kind == K_RET && g_opInfo[JT_MYSTERY].kind == K_EXIT);
    assert(JitAlloc());
    InitializeCriticalSection(&g_jitCs);
    g_jitPatched = true, g_jitMode = 1, g_jitSimTid = GetCurrentThreadId(), C.jitSwitch = 1;

    // arithmetic, a local, the exit at ret: edi = ip, [ebp-4] = ip, eax = the original opcode through the live table
    static unsigned short a[] = {JT_LDC2, JT_LDC3, JT_ADD, JT_STLOC, 8, JT_LDLOC, 8, JT_RET};
    const unsigned short a0[8] = {JT_LDC2, JT_LDC3, JT_ADD, JT_STLOC, 8, JT_LDLOC, 8, JT_RET};
    static JtMeth mA;
    JtCompile(mA, a, 8);
    assert(C.jitMethods == 1 && g_jitMeth[0].nativeOps == 5 && g_jitMeth[0].exitOps == 1);
    assert(a[0] == JIT_OP && a[3] == JIT_OP && a[4] == 8 && a[7] == JIT_OP);  // operands stay
    JtFrame();
    JtGo(a);
    assert(g_jtEax == JT_RET && g_jtEdi == (DWORD)(uintptr_t)(a + 7) && g_jtEbp[-1] == g_jtEdi && g_jtEsi == (DWORD)(uintptr_t)(g_jtArena + 2));
    assert(g_jtArena[0] == 5 && g_jtLocals[2] == 5 && C.jitCalls == 1 && C.jitExits == 1);

    // another thread, and the sim thread before its first tick: the original opcode's live handler, no compiled code
    JtFrame();
    g_jtEbpIn = (DWORD)(uintptr_t)g_jtEbp, g_jtSpIn = (DWORD)(uintptr_t)g_jtArena, g_jtIpIn = (DWORD)(uintptr_t)a;
    HANDLE th = CreateThread(NULL, 0, JtThread, NULL, 0, NULL);
    assert(th && WaitForSingleObject(th, 10000) == WAIT_OBJECT_0);
    CloseHandle(th);
    assert(g_jtEax == JT_LDC2 && g_jtEdi == (DWORD)(uintptr_t)a && g_jtEbp[-1] == g_jtEdi && C.jitOtherTid == 1 && C.jitCalls == 1 && !g_jtArena[0]);
    const DWORD simTid = g_jitSimTid;
    g_jitSimTid = 0;
    JtGo(a);
    assert(g_jtEax == JT_LDC2 && C.jitOtherTid == 2 && C.jitCalls == 1);
    g_jitSimTid = simTid;

    // a word without a template is an exit; leave.s keeps its word (EA's handler re-reads it), the rest of the method is compiled
    static unsigned short b[] = {JT_LDC2, JT_LEAVES, 2, JT_MYSTERY, JT_RET};
    static JtMeth mB;
    JtCompile(mB, b, 5);
    assert(b[0] == JIT_OP && b[1] == JT_LEAVES && b[3] == JIT_OP);
    JtFrame();
    JtGo(b);
    assert(g_jtEax == JT_LEAVES && g_jtEdi == (DWORD)(uintptr_t)(b + 1) && g_jtArena[0] == 2);
    JtGo(b + 3);
    assert(g_jtEax == JT_MYSTERY && g_jtEdi == (DWORD)(uintptr_t)(b + 3));

    // branches: taken and not
    static unsigned short c0[] = {JT_LDCS, 0, JT_BRTRUE, 4, JT_LDC2, JT_RET, JT_LDC3, JT_RET}, c1[] = {JT_LDCS, 7, JT_BRTRUE, 4, JT_LDC2, JT_RET, JT_LDC3, JT_RET};
    static JtMeth mC0, mC1;
    JtCompile(mC0, c0, 8), JtCompile(mC1, c1, 8);
    JtFrame();
    JtGo(c0);
    assert(g_jtEdi == (DWORD)(uintptr_t)(c0 + 5) && g_jtArena[0] == 2);
    JtFrame();
    JtGo(c1);
    assert(g_jtEdi == (DWORD)(uintptr_t)(c1 + 7) && g_jtArena[0] == 3);

    // switch: a case, and out of range falling through
    static unsigned short s1[] = {JT_LDCS, 1, JT_SWITCH, 2, 0, 0, 0, 2, 0, JT_LDC2, JT_RET, JT_LDC3, JT_RET}, s5[] = {JT_LDCS, 5, JT_SWITCH, 2, 0, 0, 0, 2, 0, JT_LDC2, JT_RET, JT_LDC3, JT_RET};
    static JtMeth mS1, mS5;
    JtCompile(mS1, s1, 13), JtCompile(mS5, s5, 13);
    JtFrame();
    JtGo(s1);
    assert(g_jtEdi == (DWORD)(uintptr_t)(s1 + 12) && g_jtArena[0] == 3);
    JtFrame();
    JtGo(s5);
    assert(g_jtEdi == (DWORD)(uintptr_t)(s5 + 10) && g_jtArena[0] == 2);

    // fields: a read, a write, a null leaves the slot untouched at the instruction, r4 widens
    DWORD obj[3] = {0, 77, 0};
    static unsigned short f[] = {JT_LDFLD, 4, JT_RET}, w[] = {JT_STFLD, 8, JT_RET}, r[] = {JT_LDFLDR4, 0, JT_RET};
    static JtMeth mF, mW, mR;
    JtCompile(mF, f, 3), JtCompile(mW, w, 3), JtCompile(mR, r, 3);
    JtFrame();
    g_jtArena[0] = (DWORD)(uintptr_t)obj;
    JtGo(f, 2);
    assert(g_jtEax == JT_RET && g_jtArena[0] == 77);
    JtFrame();
    JtGo(f, 2);
    assert(g_jtEax == JT_LDFLD && g_jtEdi == (DWORD)(uintptr_t)f && g_jtEsi == (DWORD)(uintptr_t)(g_jtArena + 2) && !g_jtArena[0]);
    JtFrame();
    g_jtArena[0] = (DWORD)(uintptr_t)obj, g_jtArena[2] = 99;
    JtGo(w, 4);
    assert(g_jtEax == JT_RET && obj[2] == 99 && g_jtEsi == (DWORD)(uintptr_t)g_jtArena);
    const float half = 1.5f;
    JtFrame();
    g_jtArena[0] = (DWORD)(uintptr_t)&half;
    JtGo(r, 2);
    assert(g_jtEax == JT_RET && *(const double*)g_jtArena == 1.5);

    // ldelem: in bounds, out of bounds at the instruction (length at +8, data at +0x10)
    DWORD arr[6] = {0, 0, 2, 0, 10, 20};
    static unsigned short el[] = {JT_LDELEM, JT_RET};
    static JtMeth mE;
    JtCompile(mE, el, 2);
    JtFrame();
    g_jtArena[0] = (DWORD)(uintptr_t)arr, g_jtArena[2] = 1;
    JtGo(el, 4);
    assert(g_jtEax == JT_RET && g_jtArena[0] == 20 && g_jtEsi == (DWORD)(uintptr_t)(g_jtArena + 2));
    JtFrame();
    g_jtArena[0] = (DWORD)(uintptr_t)arr, g_jtArena[2] = 2;
    JtGo(el, 4);
    assert(g_jtEax == JT_LDELEM && g_jtEdi == (DWORD)(uintptr_t)el && g_jtEsi == (DWORD)(uintptr_t)(g_jtArena + 4));

    // ldloc.vt: the slot gets the cursor, the bytes go there through the memcpy thunk, the cursor moves by the aligned size
    static unsigned short v[] = {JT_LDLOCVT, 4, 12, 0, JT_RET};
    static JtMeth mV;
    JtCompile(mV, v, 5);
    JtFrame();
    g_jtLocals[1] = 11, g_jtLocals[2] = 22, g_jtLocals[3] = 33;
    JtGo(v);
    assert(g_jtEax == JT_RET && g_jtVt[0] == 11 && g_jtVt[2] == 33 && !g_jtVt[3] && g_jtArena[0] == (DWORD)(uintptr_t)g_jtVt);
    assert(g_jtEbp[-3] == (DWORD)(uintptr_t)(g_jtVt + 4));

    // the runtime byte: applied at the iteration hook, a full round trip of the words, the native code stays
    C.jitSwitch = 0;
    assert(a[0] == JIT_OP);  // only the tick acts
    JitTick();
    assert(!memcmp(a, a0, sizeof(a)) && b[0] == JT_LDC2 && b[1] == JT_LEAVES && b[3] == JT_MYSTERY && !g_jitPatched);
    const LONG calls0 = C.jitCalls;
    JtFrame();
    JtGo(a);
    assert(g_jtEax == JT_LDC2 && C.jitCalls == calls0);  // EA's opcode runs
    C.jitSwitch = 1;
    JitTick();
    assert(a[0] == JIT_OP && a[7] == JIT_OP && a[4] == 8 && b[1] == JT_LEAVES && g_jitPatched);

    // compiling with the arm off leaves the words alone until the arm comes on
    static unsigned short o[] = {JT_LDC2, JT_RET};
    static JtMeth mO;
    C.jitSwitch = 0;
    JitTick();
    JtCompile(mO, o, 2);
    assert(o[0] == JT_LDC2);
    C.jitSwitch = 1;
    JitTick();
    assert(o[0] == JIT_OP);

    // the refusals of the compiler
    static JtMeth mX;
    static unsigned short x[] = {JT_LDC2, JT_RET};
    JtInit(mX, x, 2);
    mX.rm[0x41] = 1;
    assert(JitCompile(mX.rm) != NULL && x[0] == JT_LDC2);  // clauses
    mX.rm[0x41] = 0;
    *(WORD*)(mX.rm + 0x3A) = JIT_MAX_WORDS;
    assert(JitCompile(mX.rm) != NULL);
    *(WORD*)(mX.rm + 0x3A) = 3;  // a branch target inside no instruction
    static unsigned short y[] = {JT_BRS, 1, JT_RET};
    *(unsigned short**)mX.rm = y;
    const int methN = g_jitMethN;
    assert(JitCompile(mX.rm) != NULL && g_jitMethN == methN && y[0] == JT_BRS);

    // the post-pass: the list, the hash, the gates; the header holds the body the game loaded
    BYTE il[8] = {1, 2, 3, 4, 5, 6, 7, 8}, sha[32];
    DWORD hdr[2] = {8, (DWORD)(uintptr_t)il};
    static JitRow row = {"Cls.Meth", 8, {0}};
    assert(IlpShaInit() && IlpSha256(il, 8, sha));
    memcpy(row.sha, sha, 32);
    g_jitRows = &row, g_jitRowN = 1;
    IlpHeaderFn header0 = g_ilpHeader;
    g_ilpHeader = [](BYTE*) -> BYTE* { return NULL; };
    BYTE force = 0x75;
    g_jitForceJit = &force;
    void* objectClass = (void*)0x1234, *domain = NULL, **ocP0 = g_jitObjectClassP;
    g_jitObjectClassP = &objectClass;
    static unsigned short p1[] = {JT_LDC2, JT_RET};
    static JtMeth mP, mN;
    JtInit(mP, p1, 2);
    JtInit(mN, p1, 2, "NotListed");
    static DWORD s_hdr[2];
    s_hdr[0] = hdr[0], s_hdr[1] = hdr[1];
    LONG refused = C.jitRefused;
    int compiled = g_jitMethN;
    JitPostPass(mN.rm, &domain);  // not listed: silent
    assert(C.jitRefused == refused && g_jitMethN == compiled && p1[0] == JT_LDC2);
    JitPostPass(mP.rm, &domain);  // no header
    assert(C.jitRefused == ++refused && p1[0] == JT_LDC2);
    g_ilpHeader = [](BYTE*) -> BYTE* { return (BYTE*)s_hdr; };
    row.sha[0] ^= 1;
    JitPostPass(mP.rm, &domain);  // hash miss
    assert(C.jitRefused == ++refused && g_jitMethN == compiled && p1[0] == JT_LDC2);
    row.sha[0] ^= 1;
    force = 0x90;
    JitPostPass(mP.rm, &domain);  // ForceJIT
    assert(C.jitRefused == ++refused && p1[0] == JT_LDC2);
    force = 0x75;
    g_jitSimTid = simTid + 1;
    JitPostPass(mP.rm, &domain);  // not the sim thread
    assert(C.jitRefused == ++refused && p1[0] == JT_LDC2);
    g_jitSimTid = simTid;
    mP.rm[0x41] = 1;
    JitPostPass(mP.rm, &domain);  // clauses
    assert(C.jitRefused == ++refused && p1[0] == JT_LDC2);
    mP.rm[0x41] = 0;
    JitPostPass(mP.rm, &domain);  // known
    assert(C.jitRefused == refused && g_jitMethN == compiled + 1 && p1[0] == JIT_OP && g_jitObjectClass == objectClass);
    JitPostPass(mP.rm, &domain);  // a repeat transform: compiled once
    assert(C.jitRefused == refused && g_jitMethN == compiled + 1);
    g_jitUserN = 1, g_jitUser[0] = "Other.Thing";  // List= narrows
    assert(!JitListed("Ns.Cls.Meth") && JitListed("Ns.Other.Thing") == false);
    g_jitUser[0] = "Cls.Meth";
    assert(JitListed("Ns.Cls.Meth") && !JitListed("NsCls.Meth"));
    g_jitUserN = 0;
    // an [IlPatch] spec's patched form counts as known, under that method's name only
    assert(JitHashKnown("X.Y.Z", kIlpSpecs[0].name, 1, kIlpSpecs[0].builds[0].patched) && !JitHashKnown("X.Y.Z", "NotThatName", 1, kIlpSpecs[0].builds[0].patched));
    *(BYTE**)(mP.rm + 8) = (BYTE*)16;  // a fault in the compiler: counted, no more compiles, the session goes on
    JitPostPass(mP.rm, &domain);
    assert(g_jitDead && C.jitFaults == 1);
    g_jitDead = false, C.jitFaults = 0;
    g_ilpHeader = header0, g_jitObjectClassP = ocP0, g_jitRows = kJitRows, g_jitRowN = kJitRowN;

    // the crash report's name for an eip in compiled code
    char nm[160];
    assert(JitFaultName((DWORD)(uintptr_t)g_jitMeth[0].native0 + 1, nm, sizeof(nm)) && strstr(nm, "Ns.Cls.Meth +0x1") && C.jitFaults == 1);
    assert(!JitFaultName(1, nm, sizeof(nm)));
    C.jitFaults = 0;

    // the region: code pages are not writable once a method is done
    MEMORY_BASIC_INFORMATION mbi;
    assert(VirtualQuery(g_jitMeth[0].native0, &mbi, sizeof(mbi)) && mbi.Protect == PAGE_EXECUTE_READ);

    // leave nothing behind for the other tests
    JitSetPatched(false);
    DeleteCriticalSection(&g_jitCs);
    g_jitMode = 0, g_jitSimTid = 0, g_jitPatched = false, g_jitMethN = 0;
    memset(&C.jitOn, 0, offsetof(Ts3nCounters, jitCodeKB) + 4 - offsetof(Ts3nCounters, jitOn));
    printf("monojit: ok\n");
}
