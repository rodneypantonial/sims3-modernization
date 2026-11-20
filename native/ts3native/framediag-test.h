// TestFrameDiag ([FrameDiag], framediag.cpp, docs/300). Included by selftest.cpp. Host checks of the histogram quantiles, the
// bottleneck verdict, the GPU-preference parse and the GPU verdict with fixture adapters, the d3d9 IAT search by value,
// and the device-vtable check + Present wrapper on fake vtables.
static LONG g_fdTestCalls;
static HRESULT __stdcall FdTestPresent(void*, const RECT*, const RECT*, HWND, const void*) { g_fdTestCalls++; return 0; }
static void* g_fdTestVt[18], *g_fdTestBadVt[18];  // in this image's .data: "inside a loaded module"
static void TestFrameDiagHooks() {
    // EA IAT run: Direct3DCreate9 followed by 0 or another d3d9 address; a lone match with a foreign neighbour is skipped
    const DWORD lo = 0x6AA00000, hi = 0x6AC00000, want = 0x6AAF1610;
    DWORD a[] = {1, want, 0x12345678, want, 0}, b2[] = {want, 0x6AA01000}, c[] = {5, want}, d[] = {0, 0, 0};
    assert(FdFindSlot(a, 5, want, lo, hi) == &a[3] && FdFindSlot(b2, 2, want, lo, hi) == &b2[0]);
    assert(FdFindSlot(c, 2, want, lo, hi) == NULL && FdFindSlot(d, 3, want, lo, hi) == NULL);

    if (!g_fdQf) { LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_fdQf = fq.QuadPart; InitializeCriticalSection(&g_fdCs); }
    for (int i = 0; i < 18; i++) g_fdTestVt[i] = g_fdTestBadVt[i] = (void*)FdTestPresent;
    g_fdTestBadVt[17] = (void*)&g_fdTestVt;  // data, not code
    void** heapVt = (void**)malloc(18 * sizeof(void*));
    memcpy(heapVt, g_fdTestVt, 18 * sizeof(void*));
    assert(FdVtCheck(g_fdTestVt, 17) == NULL && FdVtCheck(g_fdTestVt, 16) == NULL);
    assert(FdVtCheck(heapVt, 17) && strstr(FdVtCheck(heapVt, 17), "not inside a loaded module") && FdVtCheck(NULL, 17));
    assert(strstr(FdVtCheck(g_fdTestBadVt, 17), "executable") && FdVtCheck(g_fdTestBadVt, 16) == NULL);
    free(heapVt);
    // refused vtable: nothing written, remembered so it is logged once
    assert(!FdHookDevVt(g_fdTestBadVt, "selftest") && !g_fdDevVt && g_fdBadVt == g_fdTestBadVt && g_fdTestBadVt[17] == (void*)&g_fdTestVt);
    // good vtable: slot 17 wrapped, the original still called, frames counted
    assert(FdHookDevVt(g_fdTestVt, "selftest") && g_fdDevVt == g_fdTestVt && g_fdTestVt[17] == (void*)FdPresentHook && g_fdPresent == FdTestPresent);
    assert(FdHookDevVt(g_fdTestVt, "selftest again"));  // same vtable: already timed
    const FdPresentFn call = (FdPresentFn)g_fdTestVt[17];
    call(NULL, NULL, NULL, NULL, NULL), Sleep(2), call(NULL, NULL, NULL, NULL, NULL);
    assert(g_fdTestCalls == 2 && g_fdMin.frames == 1 && g_fdAll.frames == 1 && g_fdMin.frameMsSum > 0);
    assert(!FdHookDevVt(g_fdTestBadVt, "second device"));  // a second vtable is not timed
    g_fdDevVt = NULL, g_fdBadVt = NULL, g_fdPresent = NULL, g_fdLastPresent = 0, g_fdMin = {}, g_fdAll = {};
}
static void TestFrameDiag() {
    TestFrameDiagHooks();
    static LONG h[FD_BINS];
    memset(h, 0, sizeof(h));
    assert(FdPct(h, 0.5) == 0);
    h[FdBin(10)] = 98, h[FdBin(40)] = 1, h[FdBin(999)] = 1;  // 98 frames of 10 ms, one 40 ms, one >= 250 ms
    assert(FdPct(h, 0.5) == 10.25 && FdPct(h, 0.99) == 40.25 && FdPct(h, 1.0) == 250.0);
    assert(FdBin(-1) == 0 && FdBin(250) == FD_BINS - 1);

    char v[640];
    FdFacts f = {};
    assert(FdVerdict(f, v, sizeof(v)) == FDV_NODATA);
    f.minutes = 15, f.fps = 18, f.frameP50 = 55, f.frameP99 = 75, f.mainD3dPct = -1, f.simIterS = 18, f.simP50 = 55;
    FdFacts b = f;  // tester-like: main thread mostly inside Present
    b.presentMsS = 600, b.mainCpuMsS = 300, b.simCpuMsS = 400;
    assert(FdVerdict(b, v, sizeof(v)) == FDV_BRIDGE && strstr(v, "BRIDGE / GPU") && strstr(v, "600 ms"));
    b.presentMsS = 100, b.mainD3dPct = 60;  // the wait shows in other d3d9 calls, not Present
    assert(FdVerdict(b, v, sizeof(v)) == FDV_BRIDGE && strstr(v, "60% of main-thread samples in d3d9"));
    FdFacts m = f;
    m.presentMsS = 100, m.mainCpuMsS = 850, m.simCpuMsS = 500;
    assert(FdVerdict(m, v, sizeof(v)) == FDV_MAIN && strstr(v, "main (render) thread"));
    FdFacts s = f;
    s.presentMsS = 50, s.mainCpuMsS = 300, s.simCpuMsS = 950;
    assert(FdVerdict(s, v, sizeof(v)) == FDV_SIM && strstr(v, "sim thread") && strstr(v, "waits 650 ms/s"));
    s.simCpuMsS = 500;  // nobody busy, main waits outside Present
    assert(FdVerdict(s, v, sizeof(v)) == FDV_WAIT && strstr(v, "WAITS outside Present 650"));
    s.mainCpuMsS = 700;
    assert(FdVerdict(s, v, sizeof(v)) == FDV_MIXED);
    s.fps = 58;
    assert(FdVerdict(s, v, sizeof(v)) == FDV_FINE && strstr(v, "none, 58 fps"));

    assert(FdPrefOf(L"GpuPreference=2;") == 2 && FdPrefOf(L"SwapEffectUpgradeEnable=1;GpuPreference=1;") == 1);
    assert(FdPrefOf(L"SwapEffectUpgradeEnable=1;") == -1 && FdPrefOf(NULL) == -1 && FdPrefOf(L"GpuPreference=") == -1);

    // which d3d9.dll: file-content markers first (the bridge client also has dxvk.conf), then the folder
    static const u8 bridgeDll[] = "MZ..dxvk.conf..NvRemixBridge.exe..", bridgeDll2[] = "MZ..DXVK_HUD..bridge.conf..", dxvkDll[] = "MZ..DXVK_CONFIG_FILE..dxvk.conf",
                    sysDll[] = "MZ..Direct3DCreate9..d3d9.pdb";
    assert(FdD3dKindOf(bridgeDll, sizeof(bridgeDll), false) == FDK_BRIDGE && FdD3dKindOf(bridgeDll2, sizeof(bridgeDll2), false) == FDK_BRIDGE);
    assert(FdD3dKindOf(dxvkDll, sizeof(dxvkDll), false) == FDK_DXVK && FdD3dKindOf(dxvkDll, sizeof(dxvkDll), true) == FDK_DXVK);
    assert(FdD3dKindOf(sysDll, sizeof(sysDll), true) == FDK_SYSTEM && FdD3dKindOf(sysDll, sizeof(sysDll), false) == FDK_OTHER);
    assert(FdD3dKindOf(dxvkDll, 8, false) == FDK_OTHER && !strcmp(kFdD3dName[FDK_DXVK], "d3d9 (DXVK)"));  // marker past the end
    // in world: the userdiag world count when [UserDiag] is on (frames do not matter), else sim iterations + frames
    assert(FdInWorld(true, 1, 0, 0, 0) && !FdInWorld(true, 1, 1, 1800, 900) && FdInWorld(true, 2, 1, 0, 0) && !FdInWorld(true, 0, 0, 1800, 900));
    assert(FdInWorld(false, 0, 0, 60, 1) && !FdInWorld(false, 5, 0, 60, 0) && !FdInWorld(false, 0, 0, 59, 900));
    // GPU preference: the first candidate (this exe, Bin\TS3W.exe, Bin\TS3.exe) with one set
    const int noPref[3] = {-1, -1, -1}, onTs3[3] = {-1, 2, -1}, onHost[3] = {0, 2, 1};
    assert(FdPickPref(noPref, 3) == -1 && FdPickPref(onTs3, 3) == 1 && FdPickPref(onHost, 3) == 0 && FdPickPref(onTs3, 1) == -1);

    char g[1024];
    FdGpu one[1] = {{"NVIDIA GeForce RTX 4080", 0x10DE, 16048, 16000, 1, 0, 1, false}};
    FdGpuVerdict(one, 1, "NvRemixBridge.exe", -1, "TS3.exe", -1, g, sizeof(g));
    assert(strstr(g, "GPU: bridge renders on NVIDIA GeForce RTX 4080 (16048 MB VRAM, the only GPU)") && !strstr(g, "two GPUs"));
    FdGpu two[3] = {{"Intel(R) UHD Graphics 770", 0x8086, 128, 16000, 2, 0, 1, false},
                    {"NVIDIA GeForce RTX 4080", 0x10DE, 16048, 16000, 1, 0, 0, false},
                    {"Microsoft Basic Render Driver", 0x1414, 0, 16000, 3, 0, 0, true}};
    assert(FdIntegrated(two[0]) && !FdIntegrated(two[1]) && !FdIntegrated(two[2]));
    FdGpuVerdict(two, 3, "NvRemixBridge.exe", -1, "TS3.exe", 2, g, sizeof(g));
    assert(strstr(g, "bridge may run on the integrated GPU Intel(R) UHD Graphics 770") && strstr(g, "set High performance for NvRemixBridge.exe"));
    assert(strstr(g, "Optimus-style") && strstr(g, "TS3.exe: High performance") && strstr(g, "NvRemixBridge.exe: not set"));
    assert(strstr(g, "display is connected to Intel(R) UHD Graphics 770"));
    FdGpuVerdict(two, 3, "NvRemixBridge.exe", 2, "TS3.exe", -1, g, sizeof(g));
    assert(strstr(g, "GPU: bridge renders on NVIDIA GeForce RTX 4080 (High performance set for NvRemixBridge.exe)"));
    FdGpuVerdict(two, 3, "NvRemixBridge.exe", 1, "TS3.exe", -1, g, sizeof(g));
    assert(strstr(g, "bridge runs on the integrated GPU") && strstr(g, "Power saving"));
    FdGpuVerdict(two, 3, NULL, 2, "TS3.exe", 2, g, sizeof(g));  // no bridge: the game's own setting counts
    assert(strstr(g, "GPU: game renders on NVIDIA GeForce RTX 4080 (High performance set for TS3.exe)") && strstr(g, "bridge server: not found"));
    FdGpu igpu[1] = {{"Intel(R) Iris(R) Xe Graphics", 0x8086, 128, 8000, 1, 0, 1, false}};
    FdGpuVerdict(igpu, 1, "NvRemixBridge.exe", -1, "TS3.exe", -1, g, sizeof(g));
    assert(strstr(g, "only an integrated GPU"));
    FdGpuVerdict(two + 2, 1, "NvRemixBridge.exe", -1, "TS3.exe", -1, g, sizeof(g));
    assert(strstr(g, "adapter list unavailable"));

    // main-thread classes: eip in a system dll with a d3d9 return address near the top of the stack = wait under d3d9
    memset(g_fdMods, 0, sizeof(g_fdMods));
    g_fdMods[FDC_SYS][0] = {0x77000000, 0x77100000}, g_fdMods[FDC_D3D][0] = {0x10000000, 0x10200000}, g_fdMods[FDC_GAME][0] = {0x400000, 0x1400000};
    const DWORD stk[3] = {0x12345, 0x10001234, 0x401000};
    assert(FdClassify(0x77000010, stk, 3) == FDC_D3DWAIT && FdClassify(0x77000010, stk, 1) == FDC_SYS);
    assert(FdClassify(0x10000010, stk, 3) == FDC_D3D && FdClassify(0x500000, stk, 3) == FDC_GAME && FdClassify(0x60000000, stk, 3) == FDC_OTHER);
    memset(g_fdMods, 0, sizeof(g_fdMods));
    printf("TestFrameDiag ok\n");
}
