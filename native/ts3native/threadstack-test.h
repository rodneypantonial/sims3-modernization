// [ThreadStack] (threadstack.cpp, docs/66 §6): selftest. Included by selftest.cpp. Real threads of this (32-bit) process
// check the TEB readout and that a reservation passed through CreateThread and the CRT's _beginthreadex really shrinks.
#include <process.h>
static DWORD WINAPI TsTestProc(LPVOID) { return 0; }
static unsigned __stdcall TsTestProcEx(void*) { return 0; }

static void TestThreadStack() {
    // decision rule
    g_tsReserve = 512 << 10; g_tsDefault = 1 << 20; g_tsSkip[0] = 0x76B470; g_tsSkipN = 1;
    DWORD s, f;
    assert(TsDecide(0, 0, 0x401000, &s, &f) && s == (512u << 10) && f == STACK_SIZE_PARAM_IS_A_RESERVATION);
    assert(TsDecide(64 << 10, CREATE_SUSPENDED, 0x401000, &s, &f) && s == (512u << 10) && f == (CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION));
    assert(TsDecide(768 << 10, 0, 0x401000, &s, &f) && s == (768u << 10));                // bigger ask kept, as a reservation
    assert(!TsDecide(1 << 20, 0, 0x401000, &s, &f) && s == (1u << 20) && f == 0);         // not below the default: untouched
    assert(!TsDecide(0, STACK_SIZE_PARAM_IS_A_RESERVATION, 0x401000, &s, &f) && s == 0);  // caller's own reservation
    assert(!TsDecide(0, 0, 0x76B470, &s, &f) && s == 0 && f == 0);                        // skipped (sim thread)
    g_tsReserve = 0;
    assert(!TsDecide(0, 0, 0x401000, &s, &f));                                            // observe only
    // user function behind the EAThread entries
    DWORD block[8] = {}, vt[2] = {0, 0x76B470}, obj = (DWORD)(uintptr_t)vt;
    block[4] = 0x76B470;
    assert(TsUserFn(TS_EATHREAD_ENTRY, block) == 0x76B470 && TsUserFn(0x401000, block) == 0x401000 && TsUserFn(TS_EATHREAD_ENTRY, NULL) == TS_EATHREAD_ENTRY);
    block[4] = (DWORD)(uintptr_t)&obj;
    assert(TsUserFn(TS_EATHREAD_ENTRY_RUNNABLE, block) == 0x76B470);
    // skip list parse
    TsParseSkip(L"0x76B470, 4DB220,,zz 0x0");
    assert(g_tsSkipN == 2 && g_tsSkip[0] == 0x76B470 && g_tsSkip[1] == 0x4DB220);
    // IAT swap: only while the slot holds the export, never twice
    DWORD slot = (DWORD)(uintptr_t)(void*)&CreateThread;
    void* orig = NULL;
    assert(TsSwap(&slot, (void*)&CreateThread, (void*)TsCreateThread, &orig, "test") && slot == (DWORD)(uintptr_t)(void*)TsCreateThread);
    assert(orig == (void*)&CreateThread && !TsSwap(&slot, (void*)&CreateThread, (void*)TsCreateThread, &orig, "test"));
    // real threads: cap on and off, through both wrappers
    g_tsCreateOrig = &CreateThread;
    g_tsBeginOrig = &_beginthreadex;
    g_tsSkipN = 0;
    g_tsLogMax = 0;
    const long def = TsReserveKB(GetCurrentThread());
    for (int cap = 0; cap < 2; cap++) {
        g_tsReserve = cap ? 256u << 10 : 0;
        HANDLE a = TsCreateThread(NULL, 0, TsTestProc, NULL, CREATE_SUSPENDED, NULL);
        HANDLE b = (HANDLE)TsBeginThreadEx(NULL, 0, TsTestProcEx, NULL, CREATE_SUSPENDED, NULL);
        const long ra = TsReserveKB(a), rb = TsReserveKB(b);
        printf("thread stack: %s: CreateThread %ld KB, _beginthreadex %ld KB (main thread %ld KB)\n", cap ? "cap 256" : "observe", ra, rb, def);
        assert(a && b && ra > 0 && rb > 0);
        if (cap) assert(ra == 256 && rb == 256);
        else assert(ra >= 1024 && rb >= 1024);
        ResumeThread(a); ResumeThread(b);
        WaitForSingleObject(a, 5000); WaitForSingleObject(b, 5000);
        CloseHandle(a); CloseHandle(b);
    }
    assert(g_tsCapped == 2 && g_tsSavedKB == 2 * (1024 - 256));
    g_tsReserve = 0;
}
