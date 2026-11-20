// [FrameDiag] (docs/300, default 0; a tester diagnostic, read-only). Included by ts3native.cpp after pccheck.cpp and
// modtime.cpp (needs simloop.cpp SimLoopAdd, apex.cpp UserSims3Dir, pccheck.cpp facts, modtime.cpp mod shares).
//
// "Where does the frame time go, seen from the game side?" With the RTX Remix bridge the frame is drawn in another process
// (NvRemixBridge.exe); TS3 only sees its own frame loop and how long each d3d9 Present call takes to come back.
//  - Present: IDirect3DDevice9::Present (vtable slot 17, the call `Device_Present 0x611680` makes, docs/538 §4.2) is
//    wrapped on the device's vtable, found two ways: (a) the game's own device, [GraphicsDevice 0x11CECC8] +0x0C (the
//    pointer Device_Present reads; same code on Steam 1.67 and EA 1.69 [verified, decrypted EA image]), polled by the
//    minute thread until it exists, so a device made before the asi starts is timed too; (b) the exe's d3d9!Direct3DCreate9
//    import slot (Steam: the import table; EA: its import table is the DRM stub, so the slot is found by value in .rdata /
//    .data once the loader filled it, retried every second) -> IDirect3D9::CreateDevice (slot 16) for later devices.
//    The vtable must sit in a loaded module and slots 0-2, 16, 17 must point at executable code, else a logged stand-down.
//    The wrapper only times the call (QPC before / after) and passes everything through: frame time = Present-to-Present
//    interval, present wait = time inside Present (bridge round trip + whatever the bridge client blocks on).
//  - Main thread: a 50 Hz sampler stops the main thread for a few microseconds, reads eip and the top 16 stack words, and
//    counts which module it is in (TS3, d3d9 labelled from its file: system, DXVK, bridge client or other; system / wait,
//    Apex, S3PP, other asi). "wait in d3d9" =
//    eip in a system dll with a d3d9 return address in those 16 words [inferred heuristic, docs/51 §545].
//  - Sim thread: one simloop.cpp callback per Simulate iteration (QPC only): iterations/s and iteration ms; its CPU time
//    and the main thread's from GetThreadTimes; process file I/O from GetProcessIoCounters (no per-file wait time).
//  - GPU selection (once, on the minute thread): DXGI adapters (name, VRAM, LUID, outputs), the Windows per-app GPU
//    preference (HKCU\Software\Microsoft\DirectX\UserGpuPreferences) for this exe, Bin\TS3W.exe, Bin\TS3.exe (the EA
//    laptop harness runs TS3.exe under another name) and Bin\.trex\NvRemixBridge.exe.
// Per minute: one `framediag:` log line, a framediag.csv row, and fps-summary.txt (verdicts: bottleneck, GPU, CPU/power
// from [PcCheck], disk / OneDrive, top script mods from [Diag] ModTime) next to ts3native.log. Nothing here changes a game
// value, a thread priority or a setting.
#include <dxgi.h>
#undef small  // rpcndr.h (via dxgi.h) defines small as char; later files and tests use it as a name

enum { FD_BINS = 501 };  // 0.5 ms bins, the last one >= 250 ms
static int FdBin(double ms) { const int b = (int)(ms * 2); return b < 0 ? 0 : b >= FD_BINS - 1 ? FD_BINS - 1 : b; }
// the q-quantile (0..1) of a histogram in ms (bin middle; the overflow bin reads 250); 0 when empty
static double FdPct(const LONG* h, double q) {
    LONGLONG n = 0;
    for (int i = 0; i < FD_BINS; i++) n += h[i];
    if (!n) return 0;
    const LONGLONG want = (LONGLONG)ceil(q * (double)n);
    LONGLONG run = 0;
    for (int i = 0; i < FD_BINS; i++) {
        run += h[i];
        if (run >= want && h[i]) return i == FD_BINS - 1 ? 250.0 : i * 0.5 + 0.25;
    }
    return 250.0;
}

// ---- verdicts (pure, selftest fixtures) ----
enum { FDV_NODATA, FDV_FINE, FDV_BRIDGE, FDV_MAIN, FDV_SIM, FDV_WAIT, FDV_MIXED };
struct FdFacts {
    double minutes, fps, frameP50, frameP99;
    double presentMsS;    // ms per real second inside Present
    double mainCpuMsS;    // main thread CPU ms per second
    double simCpuMsS;     // sim thread CPU ms per second (0 = unknown)
    double mainD3dPct;    // main-thread samples in d3d9 or waiting under d3d9 (-1 = not sampled)
    double simIterS, simP50;
};
// thresholds [inferred, docs/51 §545]: a thread at >= 75-80% of a core is the limit; >= 35% of the frame in Present means the
// game waits on the bridge / GPU more than on itself
static int FdVerdict(const FdFacts& f, char* out, size_t cap) {
    out[0] = 0;
    if (f.minutes <= 0 || f.fps <= 0) { _snprintf_s(out, cap, _TRUNCATE, "Bottleneck: no frames seen yet (Present not timed, or not in a world)"); return FDV_NODATA; }
    const double waitOther = 1000 - f.presentMsS - f.mainCpuMsS;
    char d3d[64] = "";
    if (f.mainD3dPct >= 0) _snprintf_s(d3d, _TRUNCATE, ", %.0f%% of main-thread samples in d3d9", f.mainD3dPct);
    if (f.fps >= 50) {
        _snprintf_s(out, cap, _TRUNCATE, "Bottleneck: none, %.0f fps (frame p50 %.1f ms, p99 %.1f ms)", f.fps, f.frameP50, f.frameP99);
        return FDV_FINE;
    }
    int v;
    if (f.presentMsS >= 350 || f.mainD3dPct >= 50) {
        v = FDV_BRIDGE;
        _snprintf_s(out, cap, _TRUNCATE, "Bottleneck: BRIDGE / GPU side. %.0f fps; TS3's main thread waits %.0f ms of every second inside Present%s: "
                    "the bridge server (NvRemixBridge.exe) or the GPU sets the pace, not the game's CPU work", f.fps, f.presentMsS, d3d);
    } else if (f.mainCpuMsS >= 750) {
        v = FDV_MAIN;
        _snprintf_s(out, cap, _TRUNCATE, "Bottleneck: CPU, main (render) thread. %.0f fps; main thread busy %.0f ms/s (draw preparation and the plugins on that "
                    "thread: Apex, S3PP, Turbo), Present only %.0f ms/s%s", f.fps, f.mainCpuMsS, f.presentMsS, d3d);
    } else if (f.simCpuMsS >= 800) {
        v = FDV_SIM;
        _snprintf_s(out, cap, _TRUNCATE, "Bottleneck: CPU, sim thread (scripts and simulation). %.0f fps; sim thread busy %.0f ms/s, %.1f iterations/s (p50 %.0f ms); "
                    "main thread waits %.0f ms/s outside Present: see the top script mods below", f.fps, f.simCpuMsS, f.simIterS, f.simP50, waitOther > 0 ? waitOther : 0);
    } else if (waitOther >= 400) {
        v = FDV_WAIT;
        _snprintf_s(out, cap, _TRUNCATE, "Bottleneck: main thread WAITS outside Present %.0f ms/s (sim sync, job waits, file I/O, a lock); %.0f fps, main CPU %.0f ms/s, "
                    "sim CPU %.0f ms/s, Present %.0f ms/s%s", waitOther, f.fps, f.mainCpuMsS, f.simCpuMsS, f.presentMsS, d3d);
    } else {
        v = FDV_MIXED;
        _snprintf_s(out, cap, _TRUNCATE, "Bottleneck: mixed, no single limit. %.0f fps; main CPU %.0f ms/s, Present %.0f ms/s, sim CPU %.0f ms/s%s",
                    f.fps, f.mainCpuMsS, f.presentMsS, f.simCpuMsS, d3d);
    }
    return v;
}

struct FdGpu { char name[96]; UINT vendor; LONG vramMB, sharedMB; DWORD luidLo; LONG luidHi; int outputs; bool software; };
static bool FdIntegrated(const FdGpu& g) { return !g.software && (g.vramMB < 1024 || (g.vendor == 0x8086 && g.vramMB < 2048)); }  // [inferred] docs/51 §545
// "GpuPreference=2;" -> 2; -1 = no GpuPreference in the text
static int FdPrefOf(const wchar_t* s) {
    const wchar_t* p = s ? wcsstr(s, L"GpuPreference=") : NULL;
    return p && p[14] >= L'0' && p[14] <= L'9' ? p[14] - L'0' : -1;
}
static const char* FdPrefName(int p) { return p == 2 ? "High performance" : p == 1 ? "Power saving" : p == 0 ? "Let Windows decide" : "not set"; }
// GPU verdict lines ("\n" between them); bridgeExe NULL = no bridge server found (plain d3d9 / DXVK)
static void FdGpuVerdict(const FdGpu* a, int n, const char* bridgeExe, int bridgePref, const char* gameExe, int gamePref, char* out, size_t cap) {
    out[0] = 0;
    int disc = -1, integ = -1, real = 0;
    for (int i = 0; i < n; i++) {
        if (a[i].software) continue;
        real++;
        if (FdIntegrated(a[i])) { if (integ < 0) integ = i; }
        else if (disc < 0 || a[i].vramMB > a[disc].vramMB) disc = i;
    }
    const char* who = bridgeExe ? bridgeExe : gameExe;
    const int pref = bridgeExe ? bridgePref : gamePref;
    if (!real) { PcCat(out, cap, "GPU: adapter list unavailable\n"); return; }
    if (disc < 0) PcCat(out, cap, "GPU: only an integrated GPU (%s, %ld MB): %s renders on it\n", a[integ].name, a[integ].vramMB, bridgeExe ? "the bridge" : "the game");
    else if (integ < 0) PcCat(out, cap, "GPU: %s renders on %s (%ld MB VRAM, the only GPU)\n", bridgeExe ? "bridge" : "game", a[disc].name, a[disc].vramMB);
    else if (pref == 2) PcCat(out, cap, "GPU: %s renders on %s (High performance set for %s)\n", bridgeExe ? "bridge" : "game", a[disc].name, who);
    else if (pref == 1) PcCat(out, cap, "GPU: %s runs on the integrated GPU %s (Power saving set for %s): set High performance for %s\n", bridgeExe ? "bridge" : "game", a[integ].name, who, who);
    else PcCat(out, cap, "GPU: %s may run on the integrated GPU %s (two GPUs, no Windows graphics setting for %s): set High performance for %s\n",
               bridgeExe ? "bridge" : "game", a[integ].name, who, who);
    if (disc >= 0 && integ >= 0) {
        PcCat(out, cap, "GPU: two GPUs (%s + %s)%s; %s: %s, %s: %s\n", a[integ].name, a[disc].name, a[disc].vendor == 0x10DE ? ", NVIDIA Optimus-style hybrid" : "",
              gameExe, FdPrefName(gamePref), bridgeExe ? bridgeExe : "bridge server", bridgeExe ? FdPrefName(bridgePref) : "not found");
        if (a[disc].outputs == 0 && a[integ].outputs > 0)
            PcCat(out, cap, "GPU: the display is connected to %s, not to %s: every frame is copied across (normal on laptops; on a desktop the monitor belongs on the graphics card)\n",
                  a[integ].name, a[disc].name);
    }
}

// ---- live part ----
struct FdAcc {
    LONG frames, fh[FD_BINS], ph[FD_BINS], sh[FD_BINS], hitch100, hitch250, simIters;
    double frameMsSum, presentMsSum, simMsSum, frameMax;
    LONG smpN, smpCls[8];  // main-thread samples per class
};
enum { FDC_GAME, FDC_D3D, FDC_D3DWAIT, FDC_SYS, FDC_APEX, FDC_S3PP, FDC_OURS, FDC_OTHER };
static const char* kFdCls[8] = {"TS3", "d3d9", "wait under d3d9", "system/wait", "Apex", "S3PP", "ts3native", "other"};  // [FDC_D3D]: FdD3dKind

// which d3d9.dll is loaded, from its file contents: the RTX Remix bridge client names its server and config file, DXVK
// its config file and DXVK_ variables (the bridge client carries dxvk.conf too, so it is tested first), else by folder
enum { FDK_NONE, FDK_SYSTEM, FDK_DXVK, FDK_BRIDGE, FDK_OTHER };
static const char* const kFdD3dName[5] = {"d3d9", "d3d9 (system)", "d3d9 (DXVK)", "d3d9 (bridge client)", "d3d9 (other)"};
static bool FdHas(const u8* b, size_t n, const char* s) {
    const size_t k = strlen(s);
    for (size_t i = 0; i + k <= n; i++) if (b[i] == (u8)s[0] && !memcmp(b + i, s, k)) return true;
    return false;
}
static int FdD3dKindOf(const u8* b, size_t n, bool systemDir) {
    if (FdHas(b, n, "NvRemixBridge") || FdHas(b, n, "bridge.conf")) return FDK_BRIDGE;
    if (FdHas(b, n, "dxvk.conf") || FdHas(b, n, "DXVK_")) return FDK_DXVK;
    return systemDir ? FDK_SYSTEM : FDK_OTHER;
}
static bool FdInWorld(bool worldLines, LONG worlds, LONG quits, LONG simIters, LONG frames) {
    return worldLines ? worlds > quits : simIters >= 60 && frames > 0;
}
// the first candidate with a GPU preference set; -1 = none set
static int FdPickPref(const int* pref, int n) {
    for (int i = 0; i < n; i++) if (pref[i] >= 0) return i;
    return -1;
}
static FdAcc g_fdMin, g_fdAll;
static CRITICAL_SECTION g_fdCs;
static LONG g_fdOn;
static LONGLONG g_fdQf, g_fdLastPresent, g_fdLastSim;
static volatile DWORD g_fdSimTid;
static wchar_t g_fdSum[MAX_PATH], g_fdCsv[MAX_PATH];

static LONGLONG FdQpc() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static double FdMs(LONGLONG d) { return (double)d * 1000.0 / (double)g_fdQf; }

typedef HRESULT(__stdcall* FdPresentFn)(void*, const RECT*, const RECT*, HWND, const void*);
typedef HRESULT(__stdcall* FdCreateDevFn)(void*, UINT, DWORD, HWND, DWORD, void*, void**);
typedef void*(__stdcall* FdCreate9Fn)(UINT);
static FdPresentFn g_fdPresent;
static FdCreateDevFn g_fdCreateDev;
static FdCreate9Fn g_fdCreate9;
static void** g_fdDevVt;

static HRESULT __stdcall FdPresentHook(void* dev, const RECT* a, const RECT* b, HWND w, const void* r) {
    const LONGLONG t0 = FdQpc();
    const HRESULT hr = g_fdPresent(dev, a, b, w, r);
    const LONGLONG t1 = FdQpc();
    const double pm = FdMs(t1 - t0), fm = g_fdLastPresent ? FdMs(t1 - g_fdLastPresent) : 0;
    g_fdLastPresent = t1;
    if (fm > 0 && fm < 60000) {
        EnterCriticalSection(&g_fdCs);
        FdAcc* acc[2] = {&g_fdMin, &g_fdAll};
        for (FdAcc* x : acc) {
            x->frames++, x->fh[FdBin(fm)]++, x->ph[FdBin(pm)]++, x->frameMsSum += fm, x->presentMsSum += pm;
            if (fm > x->frameMax) x->frameMax = fm;
            if (fm >= 100) x->hitch100++;
            if (fm >= 250) x->hitch250++;
        }
        LeaveCriticalSection(&g_fdCs);
    }
    return hr;
}

static bool FdExec(const void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    return p && VirtualQuery(p, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
           (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
// a COM vtable we may patch: inside a loaded module, slots 0-2 (IUnknown) and `hi` executable (16 = CreateDevice on
// IDirect3D9, 17 = Present on the device); NULL = ok, else why not
static const char* FdVtCheck(void** vt, int hi) {
    HMODULE m = NULL;
    if (!vt || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void*)vt, &m))
        return "vtable not inside a loaded module";
    void* s[4] = {};
    __try { s[0] = vt[0], s[1] = vt[1], s[2] = vt[2], s[3] = vt[hi]; } __except (EXCEPTION_EXECUTE_HANDLER) { return "vtable unreadable"; }
    for (void* f : s) if (!FdExec(f)) return "a vtable slot does not point at executable code";
    return NULL;
}
static void FdModName(const void* a, char* out, int cap) {
    HMODULE m = NULL;
    wchar_t p[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &m)) GetModuleFileNameW(m, p, MAX_PATH);
    const wchar_t* n = wcsrchr(p, L'\\');
    WideCharToMultiByte(CP_UTF8, 0, n ? n + 1 : p, -1, out, cap, NULL, NULL);
}

// one vtable slot -> hook; *orig gets what it held (another plugin's wrapper is chained, not replaced)
static bool FdVtSlot(void** vt, int slot, void* hook, void** orig) {
    if (vt[slot] == hook) return true;
    *orig = vt[slot];  // before the write: a call through the slot right after it finds its target
    return Write(&vt[slot], &hook, sizeof(hook));
}

// device vtable -> Present timed, once. The main thread's CreateDevice wrapper and the minute thread's poll both call it;
// a refused or second vtable is logged once per vtable.
static volatile LONG g_fdHookBusy;
static void** g_fdBadVt;
static bool FdHookDevVt(void** vt, const char* via) {
    if (vt == g_fdDevVt) return true;
    if (InterlockedCompareExchange(&g_fdHookBusy, 1, 0)) return false;  // the other caller is at it; the poll retries
    bool ok = false;
    if (g_fdDevVt) {
        if (vt != g_fdBadVt) Log("framediag: another device vtable 0x%08X (%s) not timed", (DWORD)(uintptr_t)vt, via), g_fdBadVt = vt;
    } else if (const char* why = FdVtCheck(vt, 17)) {
        if (vt != g_fdBadVt) Log("framediag: Present NOT timed, device vtable 0x%08X (%s) refused: %s", (DWORD)(uintptr_t)vt, via, why), g_fdBadVt = vt;
    } else if (!FdVtSlot(vt, 17, (void*)FdPresentHook, (void**)&g_fdPresent)) {
        if (vt != g_fdBadVt) Log("framediag: Present NOT timed, device vtable 0x%08X (%s): slot 17 not writable", (DWORD)(uintptr_t)vt, via), g_fdBadVt = vt;
    } else {
        char vm[64], pm[64];
        FdModName(vt, vm, sizeof(vm)), FdModName((const void*)g_fdPresent, pm, sizeof(pm));
        g_fdDevVt = vt, ok = true;
        Log("framediag: Present timed via %s (device vtable 0x%08X in %s, slot 17 was 0x%08X in %s)", via, (DWORD)(uintptr_t)vt, vm, (DWORD)(uintptr_t)g_fdPresent, pm);
    }
    g_fdHookBusy = 0;
    return ok;
}

static HRESULT __stdcall FdCreateDevHook(void* d3d, UINT ad, DWORD type, HWND w, DWORD fl, void* pp, void** out) {
    const HRESULT hr = g_fdCreateDev(d3d, ad, type, w, fl, pp, out);
    if (SUCCEEDED(hr) && out && *out) FdHookDevVt(*(void***)*out, "IDirect3D9::CreateDevice");
    return hr;
}

// the game's own device: [GraphicsDevice] +0x0C, the pointer Device_Present 0x611680 calls through (docs/37 §1). EA 1.69:
// Device_Present 0x612760 has the same bytes and GraphicsDevice_Get returns this row's [0x01228D78] [verified, decrypted
// EA image]. NULL = no device yet.
static u8* const* g_fdGfxDev = (u8* const*)(uintptr_t)TS3A(0x011CECC8);
static void** FdGameDevVt() {
    __try {
        const u8* w = *g_fdGfxDev;
        void* dev = w ? *(void* const*)(w + 0x0C) : NULL;
        return dev ? *(void***)dev : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

static void* __stdcall FdCreate9Hook(UINT sdk) {
    void* d3d = g_fdCreate9(sdk);
    if (d3d && !g_fdCreateDev) {
        void** vt = *(void***)d3d;
        if (const char* why = FdVtCheck(vt, 16)) Log("framediag: IDirect3D9 vtable refused (%s), CreateDevice not wrapped", why);
        else if (!FdVtSlot(vt, 16, (void*)FdCreateDevHook, (void**)&g_fdCreateDev)) Log("framediag: CreateDevice slot not writable, not wrapped");
    }
    return d3d;
}

// the exe's IAT slot for d3d9.dll!Direct3DCreate9 (found by name in the import name table, whatever it holds now)
static void** FdIatSlot() {
    u8* b = (u8*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(b + ((const IMAGE_DOS_HEADER*)b)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!d.VirtualAddress) return NULL;
    for (const IMAGE_IMPORT_DESCRIPTOR* i = (const IMAGE_IMPORT_DESCRIPTOR*)(b + d.VirtualAddress); i->Name; i++) {
        if (_stricmp((const char*)(b + i->Name), "d3d9.dll") || !i->OriginalFirstThunk) continue;
        const IMAGE_THUNK_DATA* nameT = (const IMAGE_THUNK_DATA*)(b + i->OriginalFirstThunk);
        IMAGE_THUNK_DATA* iat = (IMAGE_THUNK_DATA*)(b + i->FirstThunk);
        for (; nameT->u1.AddressOfData; nameT++, iat++)
            if (!(nameT->u1.Ordinal & IMAGE_ORDINAL_FLAG) && !strcmp((const char*)((const IMAGE_IMPORT_BY_NAME*)(b + nameT->u1.AddressOfData))->Name, "Direct3DCreate9"))
                return (void**)&iat->u1.Function;
    }
    return NULL;
}

// EA 1.69: the import directory is the DRM stub (Core/Activation.dll only); the real d3d9 IAT slot sits in .rdata (RVA
// 0xBDFB00 [verified, decrypted EA image]) and is filled at run time. Found by value: a 4-byte aligned word in an
// initialized non-code section that holds d3d9!Direct3DCreate9, followed by 0 or another d3d9 address (an IAT run).
static DWORD* FdFindSlot(DWORD* p, DWORD n, DWORD want, DWORD lo, DWORD hi) {
    for (DWORD k = 0; k + 1 < n; k++)
        if (p[k] == want && (p[k + 1] == 0 || (p[k + 1] >= lo && p[k + 1] < hi))) return &p[k];
    return NULL;
}
static void** FdIatByValue() {
    HMODULE d3d = GetModuleHandleW(L"d3d9.dll");
    const DWORD want = d3d ? (DWORD)(uintptr_t)GetProcAddress(d3d, "Direct3DCreate9") : 0;
    MODULEINFO mi = {};
    if (!want || !K32GetModuleInformation(GetCurrentProcess(), d3d, &mi, sizeof(mi))) return NULL;
    const DWORD lo = (DWORD)(uintptr_t)mi.lpBaseOfDll, hi = lo + mi.SizeOfImage;
    u8* b = (u8*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(b + ((const IMAGE_DOS_HEADER*)b)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) || !(sec->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)) continue;
        __try {
            if (DWORD* s = FdFindSlot((DWORD*)(b + sec->VirtualAddress), sec->Misc.VirtualSize / 4, want, lo, hi)) return (void**)s;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return NULL;
}
// slot -> FdCreate9Hook (it held d3d9's export, or another plugin's wrapper of it: chained)
static bool FdHookCreate9(void** slot) {
    void* hook = (void*)FdCreate9Hook;
    if (!slot || !*slot || *slot == hook) return false;
    g_fdCreate9 = (FdCreate9Fn)*slot;
    if (Write(slot, &hook, sizeof(hook))) return true;
    g_fdCreate9 = NULL;
    return false;
}

static void __cdecl FdSimIter() {
    const LONGLONG t = FdQpc();
    if (!g_fdSimTid) g_fdSimTid = GetCurrentThreadId();
    const double ms = g_fdLastSim ? FdMs(t - g_fdLastSim) : 0;
    g_fdLastSim = t;
    if (ms <= 0 || ms > 60000) return;
    EnterCriticalSection(&g_fdCs);
    g_fdMin.simIters++, g_fdMin.sh[FdBin(ms)]++, g_fdMin.simMsSum += ms;
    g_fdAll.simIters++, g_fdAll.sh[FdBin(ms)]++, g_fdAll.simMsSum += ms;
    LeaveCriticalSection(&g_fdCs);
}

// module ranges for the main-thread classes
struct FdRange { DWORD lo, hi; };
static FdRange g_fdMods[8][3];
static void FdModRange(int cls, int k, const wchar_t* name) {
    HMODULE m = name ? GetModuleHandleW(name) : GetModuleHandleW(NULL);
    MODULEINFO mi = {};
    if (m && K32GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof(mi))) g_fdMods[cls][k] = {(DWORD)(uintptr_t)mi.lpBaseOfDll, (DWORD)(uintptr_t)mi.lpBaseOfDll + mi.SizeOfImage};
}
static HMODULE g_fdD3dMod;
static void FdD3dKind() {  // once per loaded d3d9.dll: read its file and name it
    HMODULE m = GetModuleHandleW(L"d3d9.dll");
    if (!m || m == g_fdD3dMod) return;
    g_fdD3dMod = m;
    wchar_t p[MAX_PATH], sys[MAX_PATH] = L"", wow[MAX_PATH] = L"";
    if (!GetModuleFileNameW(m, p, MAX_PATH)) return;
    GetSystemDirectoryW(sys, MAX_PATH), GetSystemWow64DirectoryW(wow, MAX_PATH);
    const wchar_t* slash = wcsrchr(p, L'\\');
    const size_t dn = slash ? (size_t)(slash - p) : 0;
    const bool sysDir = dn && ((wcslen(sys) == dn && !_wcsnicmp(p, sys, dn)) || (wcslen(wow) == dn && !_wcsnicmp(p, wow, dn)));
    int kind = sysDir ? FDK_SYSTEM : FDK_OTHER;
    LARGE_INTEGER sz = {};
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        u8* buf = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (64 << 20) ? (u8*)malloc((size_t)sz.QuadPart) : NULL;
        DWORD got = 0;
        if (buf && ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) && got == sz.QuadPart) kind = FdD3dKindOf(buf, got, sysDir);
        free(buf);
        CloseHandle(h);
    }
    kFdCls[FDC_D3D] = kFdD3dName[kind];
    Log("framediag: d3d9.dll from %s, %lld KB: %s", sysDir ? "the Windows system folder" : "outside the system folder", sz.QuadPart >> 10, kFdD3dName[kind]);
}
static void FdModsInit() {
    memset(g_fdMods, 0, sizeof(g_fdMods));
    FdModRange(FDC_GAME, 0, NULL);
    FdModRange(FDC_D3D, 0, L"d3d9.dll");
    FdD3dKind();
    FdModRange(FDC_SYS, 0, L"ntdll.dll"), FdModRange(FDC_SYS, 1, L"win32u.dll"), FdModRange(FDC_SYS, 2, L"KernelBase.dll");
    FdModRange(FDC_APEX, 0, L"ApexRadiance.asi");
    FdModRange(FDC_S3PP, 0, L"Sims3Performance.asi");
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void*)&FdModsInit, &self);
    MODULEINFO mi = {};
    if (self && K32GetModuleInformation(GetCurrentProcess(), self, &mi, sizeof(mi))) g_fdMods[FDC_OURS][0] = {(DWORD)(uintptr_t)mi.lpBaseOfDll, (DWORD)(uintptr_t)mi.lpBaseOfDll + mi.SizeOfImage};
}
static int FdIn(DWORD a, int cls) {
    for (int k = 0; k < 3; k++) if (a >= g_fdMods[cls][k].lo && a < g_fdMods[cls][k].hi) return 1;
    return 0;
}
static int FdClassify(DWORD eip, const DWORD* stk, int n) {
    static const int order[] = {FDC_D3D, FDC_GAME, FDC_APEX, FDC_S3PP, FDC_OURS};
    if (FdIn(eip, FDC_SYS)) {
        for (int i = 0; i < n; i++) if (FdIn(stk[i], FDC_D3D)) return FDC_D3DWAIT;
        return FDC_SYS;
    }
    for (int c : order) if (FdIn(eip, c)) return c;
    return FDC_OTHER;
}

// one main-thread sample; false = could not read the thread
static bool FdSampleMain(HANDLE h) {
    CONTEXT c = {};
    c.ContextFlags = CONTEXT_CONTROL;
    DWORD stk[16] = {};
    int n = 0;
    if (SuspendThread(h) == (DWORD)-1) return false;
    const bool ok = GetThreadContext(h, &c) != 0;
    if (ok) {  // no lock, heap or log while it is stopped
        __try { memcpy(stk, (const void*)(uintptr_t)c.Esp, sizeof(stk)); n = 16; } __except (EXCEPTION_EXECUTE_HANDLER) { n = 0; }
    }
    ResumeThread(h);
    if (!ok) return false;
    const int cls = FdClassify(c.Eip, stk, n);
    EnterCriticalSection(&g_fdCs);
    g_fdMin.smpN++, g_fdMin.smpCls[cls]++, g_fdAll.smpN++, g_fdAll.smpCls[cls]++;
    LeaveCriticalSection(&g_fdCs);
    return true;
}

// GPU facts, once
static FdGpu g_fdGpu[8];
static int g_fdGpuN;
static char g_fdGpuText[2048], g_fdGpuVerdict[1024];
static void FdGpuCheck() {
    typedef HRESULT(WINAPI * Cf1)(REFIID, void**);
    static const GUID kIidF1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};  // IDXGIFactory1
    HMODULE dx = LoadLibraryW(L"dxgi.dll");
    Cf1 cf = dx ? (Cf1)(void*)GetProcAddress(dx, "CreateDXGIFactory1") : NULL;
    IDXGIFactory1* f = NULL;
    if (cf && SUCCEEDED(cf(kIidF1, (void**)&f)) && f) {
        IDXGIAdapter1* a = NULL;
        for (UINT i = 0; g_fdGpuN < 8 && f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d = {};
            if (SUCCEEDED(a->GetDesc1(&d))) {
                FdGpu& g = g_fdGpu[g_fdGpuN++];
                WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, g.name, sizeof(g.name), NULL, NULL);
                g.vendor = d.VendorId, g.vramMB = (LONG)(d.DedicatedVideoMemory >> 20), g.sharedMB = (LONG)(d.SharedSystemMemory >> 20);
                g.luidLo = d.AdapterLuid.LowPart, g.luidHi = d.AdapterLuid.HighPart, g.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
                IDXGIOutput* o = NULL;
                for (UINT k = 0; a->EnumOutputs(k, &o) != DXGI_ERROR_NOT_FOUND; k++) { g.outputs++; o->Release(); }
            }
            a->Release();
        }
        f->Release();
    }
    if (dx) FreeLibrary(dx);
    // Windows per-app GPU preference: value name = full exe path. Candidates: this exe, then Bin\TS3W.exe and Bin\TS3.exe
    // (the EA laptop harness runs TS3.exe as Sims3Launcher.exe; a player sets the preference on the real name)
    wchar_t cand[3][MAX_PATH] = {}, bridge[MAX_PATH] = L"";
    GetModuleFileNameW(NULL, cand[0], MAX_PATH);
    int nc = 1;
    for (const wchar_t* n : {L"TS3W.exe", L"TS3.exe"}) {
        wcscpy_s(cand[nc], cand[0]);
        wchar_t* s = wcsrchr(cand[nc], L'\\');
        if (!s) continue;
        wcscpy_s(s + 1, MAX_PATH - (s + 1 - cand[nc]), n);
        if (_wcsicmp(cand[nc], cand[0]) && GetFileAttributesW(cand[nc]) != INVALID_FILE_ATTRIBUTES) nc++;
    }
    wcscpy_s(bridge, cand[0]);
    if (wchar_t* s = wcsrchr(bridge, L'\\')) wcscpy_s(s + 1, MAX_PATH - (s + 1 - bridge), L".trex\\NvRemixBridge.exe");
    const bool haveBridge = GetFileAttributesW(bridge) != INVALID_FILE_ATTRIBUTES;
    int pref[3] = {-1, -1, -1}, bridgePref = -1, bridgeElsewhere = -1;
    char global[96] = "";
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t name[1024], data[256];
        for (DWORD i = 0;; i++) {
            DWORD nl = 1024, dl = sizeof(data) - sizeof(wchar_t), type = 0;
            const LONG r = RegEnumValueW(k, i, name, &nl, NULL, &type, (BYTE*)data, &dl);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS || type != REG_SZ) continue;
            data[dl / sizeof(wchar_t)] = 0;
            const int p = FdPrefOf(data);
            int c = 0;
            while (c < nc && _wcsicmp(name, cand[c])) c++;
            if (c < nc) pref[c] = p;
            else if (!_wcsicmp(name, bridge)) bridgePref = p;
            else if (wcslen(name) >= 18 && !_wcsicmp(name + wcslen(name) - 18, L"\\NvRemixBridge.exe")) bridgeElsewhere = p;  // another install's bridge
            else if (!_wcsicmp(name, L"DirectXUserGlobalSettings")) WideCharToMultiByte(CP_UTF8, 0, data, -1, global, sizeof(global), NULL, NULL);
        }
        RegCloseKey(k);
    }
    // the game named in the verdict: the first candidate with a preference, else this exe when it is TS3(W).exe, else Bin's
    char names[3][64], prefs[256] = "", bridgeExe[] = "NvRemixBridge.exe";
    for (int c = 0; c < nc; c++) {
        const wchar_t* en = wcsrchr(cand[c], L'\\');
        WideCharToMultiByte(CP_UTF8, 0, en ? en + 1 : cand[c], -1, names[c], sizeof(names[c]), NULL, NULL);
        PcCat(prefs, sizeof(prefs), "%s%s %s", c ? ", " : "", names[c], FdPrefName(pref[c]));
    }
    int pick = FdPickPref(pref, nc);
    if (pick < 0) pick = !_strnicmp(names[0], "TS3", 3) || nc == 1 ? 0 : 1;
    const char* gameExe = names[pick];
    const int gamePref = pref[pick];
    FdGpuVerdict(g_fdGpu, g_fdGpuN, haveBridge ? bridgeExe : NULL, bridgePref, gameExe, gamePref, g_fdGpuVerdict, sizeof(g_fdGpuVerdict));
    char* t = g_fdGpuText;
    t[0] = 0;
    for (int i = 0; i < g_fdGpuN; i++) {
        const FdGpu& g = g_fdGpu[i];
        PcCat(t, sizeof(g_fdGpuText), "adapter %d: %s, vendor 0x%04X, %ld MB dedicated, %ld MB shared, LUID %08lX:%08lX, %d output(s)%s\n", i, g.name, g.vendor, g.vramMB,
              g.sharedMB, g.luidHi, g.luidLo, g.outputs, g.software ? ", software" : FdIntegrated(g) ? ", integrated" : ", discrete");
    }
    PcCat(t, sizeof(g_fdGpuText), "Windows graphics preference: %s; bridge server %s%s%s%s; global settings \"%s\"\n", prefs,
          haveBridge ? "Bin\\.trex\\NvRemixBridge.exe " : "not found in Bin\\.trex", haveBridge ? FdPrefName(bridgePref) : "",
          bridgeElsewhere >= 0 ? "; another NvRemixBridge.exe path has " : "", bridgeElsewhere >= 0 ? FdPrefName(bridgeElsewhere) : "", global);
    Log("framediag: %d adapter(s); %s", g_fdGpuN, g_fdGpuVerdict);
    for (char* p = g_fdGpuText; *p;) { char* nl = strchr(p, '\n'); if (nl) *nl = 0; Log("framediag: %s", p); if (!nl) break; *nl = '\n'; p = nl + 1; }
}

// top script mods from [Diag] ModTime ("-" when it is off)
static void FdTopMods(char* out, size_t cap) {
    out[0] = 0;
    if (!g_mdtOn) { strcpy_s(out, cap, "Top script mods: [Diag] ModTime off"); return; }
    static double w[512];
    static MdtMod mods[512];
    EnterCriticalSection(&g_mdtCs);
    MdtMatchPackages();
    const int nm = g_mdtModN;
    memcpy(mods, g_mdtMod, sizeof(MdtMod) * nm);
    const double total = g_mdtTotalW;
    LeaveCriticalSection(&g_mdtCs);
    if (total <= 0) { strcpy_s(out, cap, "Top script mods: no samples yet"); return; }
    double game = 0, ours = 0, eng = 0, mod = 0;
    for (int i = 0; i < nm; i++) {
        w[i] = mods[i].kind == MDT_MOD ? mods[i].w : 0;
        (mods[i].kind == MDT_MOD ? mod : mods[i].kind == MDT_OURS ? ours : mods[i].kind == MDT_ENGINE ? eng : game) += mods[i].w;
    }
    PcCat(out, cap, "Sim-thread CPU: script mods %.0f%%, game scripts %.0f%%, ts3fixes %.0f%%, engine (no script) %.0f%%\nTop script mods:",
          mod * 100 / total, game * 100 / total, ours * 100 / total, eng * 100 / total);
    int idx[5], got;
    MdtOrder(w, nm, idx, 5, &got);
    for (int r = 0; r < got; r++) PcCat(out, cap, "%s %s %.1f%% (%s)", r ? ";" : "", mods[idx[r]].name, w[idx[r]] * 100 / total, mods[idx[r]].pkg[0] ? mods[idx[r]].pkg : "?");
    if (!got) PcCat(out, cap, " none seen");
}

struct FdRow { char t[8]; double fps, p50, p99, maxMs, presentMsS, mainCpu, simCpu, simIterS, d3dPct, rdMB, wrMB, ioOps; LONG h100, h250; };
static FdRow g_fdRows[60];
static int g_fdRowN;
static double g_fdAllMin, g_fdAllWall, g_fdAllMainCpu, g_fdAllSimCpu, g_fdAllRd, g_fdAllWr, g_fdAllOps;  // in-world minutes only

static FdFacts FdFactsOf(const FdAcc& a, double wallS, double mainCpuMs, double simCpuMs, double minutes) {
    FdFacts f = {};
    f.minutes = minutes;
    f.fps = wallS > 0 ? a.frames / wallS : 0;
    f.frameP50 = FdPct(a.fh, 0.5), f.frameP99 = FdPct(a.fh, 0.99);
    f.presentMsS = wallS > 0 ? a.presentMsSum / wallS : 0;
    f.mainCpuMsS = wallS > 0 ? mainCpuMs / wallS : 0;
    f.simCpuMsS = wallS > 0 ? simCpuMs / wallS : 0;
    f.mainD3dPct = a.smpN ? (a.smpCls[FDC_D3D] + a.smpCls[FDC_D3DWAIT]) * 100.0 / a.smpN : -1;
    f.simIterS = wallS > 0 ? a.simIters / wallS : 0;
    f.simP50 = FdPct(a.sh, 0.5);
    return f;
}

static void FdWriteSummary(const FdAcc& all) {
    const FdFacts f = FdFactsOf(all, g_fdAllWall, g_fdAllMainCpu, g_fdAllSimCpu, g_fdAllMin);
    static char bott[640], pc[2048], mods[1536], disk[512];
    FdVerdict(f, bott, sizeof(bott));
    pc[0] = 0;
    double oneDrivePct = -1;
    if (g_pcOn) {
        EnterCriticalSection(&g_pcCs);
        const PcFacts pf = PcFactsOf(g_pcAll);
        for (int i = 0; i < g_pcAll.topN; i++)
            if (!_stricmp(g_pcAll.top[i].name, "OneDrive.exe") && g_pcAll.wallS > 0) oneDrivePct = g_pcAll.top[i].ms / 10 / (g_pcAll.wallS * (pf.logical > 0 ? pf.logical : 1));
        LeaveCriticalSection(&g_pcCs);
        PcVerdict(pf, pc, sizeof(pc));
    } else strcpy_s(pc, "[PcCheck] off\n");
    FdTopMods(mods, sizeof(mods));
    wchar_t ud[MAX_PATH];
    UserSims3Dir(ud);
    _wcslwr_s(ud);
    const bool oneDrive = wcsstr(ud, L"onedrive") != NULL;
    const double m = g_fdAllMin > 0 ? g_fdAllMin : 1;
    _snprintf_s(disk, _TRUNCATE, "Disk: the game read %.1f MB/min and wrote %.1f MB/min (%.0f file ops/min, all files; file wait time not measured)%s", g_fdAllRd / m, g_fdAllWr / m, g_fdAllOps / m,
                oneDrive ? "; the user folder (saves, logs, screenshots) is in OneDrive" : "; the user folder is not in OneDrive");
    if (oneDrivePct >= 0) PcCat(disk, sizeof(disk), "; OneDrive.exe used %.1f%% CPU", oneDrivePct);
    HANDLE h = CreateFileW(g_fdSum, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    static char b[16384];
    b[0] = 0;
    SYSTEMTIME st;
    GetLocalTime(&st);
    PcCat(b, sizeof(b), "ts3native " TS3NATIVE_VERSION " fps summary ([FrameDiag], docs/300; read-only, nothing in the game was changed), written %02d:%02d\n\n", st.wHour, st.wMinute);
    PcCat(b, sizeof(b), "%s\n%s%s\n%s\n\n", bott, g_fdGpuVerdict, disk, mods);
    PcCat(b, sizeof(b), "CPU / power ([PcCheck]):\n%s\n", pc);
    PcCat(b, sizeof(b), "In world: %.0f min; %.1f fps, frame p50 %.1f ms, p99 %.1f ms, max %.0f ms, %ld frames >= 100 ms, %ld >= 250 ms\n", g_fdAllMin, f.fps, f.frameP50,
          f.frameP99, all.frameMax, all.hitch100, all.hitch250);
    PcCat(b, sizeof(b), "Present: %.0f ms/s, per frame p50 %.2f ms, p99 %.1f ms; main thread CPU %.0f ms/s; sim thread CPU %.0f ms/s, %.1f iterations/s (p50 %.1f ms, p99 %.1f ms)\n",
          f.presentMsS, FdPct(all.ph, 0.5), FdPct(all.ph, 0.99), f.mainCpuMsS, f.simCpuMsS, f.simIterS, f.simP50, FdPct(all.sh, 0.99));
    PcCat(b, sizeof(b), "Main-thread samples (%ld):", all.smpN);
    for (int c = 0; c < 8; c++) if (all.smpCls[c]) PcCat(b, sizeof(b), " %s %.1f%%", kFdCls[c], all.smpCls[c] * 100.0 / (all.smpN ? all.smpN : 1));
    PcCat(b, sizeof(b), "\n\n%s\nPer minute (last %d): time fps p50 p99 max | present ms/s | main cpu | sim cpu | sim it/s | d3d9%% | MB read/write | >=100/>=250 ms\n", g_fdGpuText,
          g_fdRowN < 60 ? g_fdRowN : 60);
    for (int i = g_fdRowN > 60 ? g_fdRowN - 60 : 0; i < g_fdRowN; i++) {
        const FdRow& r = g_fdRows[i % 60];
        PcCat(b, sizeof(b), "%s %5.1f %5.1f %5.1f %4.0f | %4.0f | %4.0f | %4.0f | %4.1f | %3.0f | %.1f/%.1f | %ld/%ld\n", r.t, r.fps, r.p50, r.p99, r.maxMs, r.presentMsS,
              r.mainCpu, r.simCpu, r.simIterS, r.d3dPct, r.rdMB, r.wrMB, r.h100, r.h250);
    }
    DWORD w;
    for (const char* p = b; *p; p++) { if (*p == '\n') WriteFile(h, "\r", 1, &w, NULL); WriteFile(h, p, 1, &w, NULL); }
    CloseHandle(h);
}

static LONGLONG FdThreadCpu(HANDLE h) {
    FILETIME c, e, k, u;
    return h && GetThreadTimes(h, &c, &e, &k, &u) ? PcFt(k) + PcFt(u) : -1;
}

static DWORD WINAPI FdThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);  // samples on time; it runs a few microseconds per sample
    FdGpuCheck();
    FdModsInit();
    HANDLE mainH = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_mainTid);
    HANDLE simH = NULL;
    LONGLONG lastMainCpu = FdThreadCpu(mainH), lastSimCpu = -1, lastQ = FdQpc();
    IO_COUNTERS io0 = {};
    GetProcessIoCounters(GetCurrentProcess(), &io0);
    DWORD next = GetTickCount() + 60000, tick = 0;
    bool iatDone = g_fdCreate9 != NULL, told = false;
    for (;;) {
        Sleep(20);  // 50 Hz main-thread samples
        if (mainH) FdSampleMain(mainH);
        tick++;
        if (!g_fdDevVt) {  // the game's device, also one made before the asi started (each refused vtable is logged once)
            void** vt = FdGameDevVt();
            if (vt && vt != g_fdBadVt) FdHookDevVt(vt, "the game device");
        }
        if (!iatDone && tick % 50 == 0) {  // EA: the d3d9 IAT slot, filled at run time; once a second for 5 min
            void** slot = FdIatByValue();
            if (FdHookCreate9(slot)) {
                iatDone = true;
                Log("framediag: d3d9!Direct3DCreate9 slot found by value at exe+0x%X, CreateDevice wrapped for later devices",
                    (DWORD)((uintptr_t)slot - (uintptr_t)GetModuleHandleW(NULL)));
            } else if (tick >= 50 * 300) {
                iatDone = true;
                Log("framediag: no d3d9!Direct3DCreate9 slot found by value in 300 s (d3d9 %s); only the game device path is left", GetModuleHandleW(L"d3d9.dll") ? "loaded" : "not loaded");
            }
        }
        if (!told && !g_fdDevVt && tick == 50 * 120) {
            told = true;
            Log("framediag: Present still NOT timed after 120 s: game device %s, Direct3DCreate9 import %s; fps reads 0 until it is",
                FdGameDevVt() ? "refused (line above)" : "not created yet", g_fdCreate9 ? "wrapped" : "not found yet");
        }
        if ((LONG)(GetTickCount() - next) < 0) continue;
        next += 60000;
        FdModsInit();  // d3d9 / Apex may load after the thread started
        if (g_fdDevVt) {  // a new device with another vtable (logged once, not timed)
            void** vt = FdGameDevVt();
            if (vt && vt != g_fdDevVt && vt != g_fdBadVt) FdHookDevVt(vt, "the game device");
        }
        if (!simH && g_fdSimTid) { simH = OpenThread(THREAD_QUERY_INFORMATION, FALSE, g_fdSimTid); lastSimCpu = FdThreadCpu(simH); }
        const LONGLONG q = FdQpc(), mc = FdThreadCpu(mainH), sc = FdThreadCpu(simH);
        const double wallS = FdMs(q - lastQ) / 1000, mainMs = mc >= 0 && lastMainCpu >= 0 ? (mc - lastMainCpu) / 1e4 : 0, simMs = sc >= 0 && lastSimCpu >= 0 ? (sc - lastSimCpu) / 1e4 : 0;
        lastQ = q, lastMainCpu = mc, lastSimCpu = sc;
        IO_COUNTERS io = {};
        GetProcessIoCounters(GetCurrentProcess(), &io);
        const double rd = (io.ReadTransferCount - io0.ReadTransferCount) / 1048576.0, wr = (io.WriteTransferCount - io0.WriteTransferCount) / 1048576.0;
        const double ops = (double)(io.ReadOperationCount - io0.ReadOperationCount + io.WriteOperationCount - io0.WriteOperationCount);
        io0 = io;
        static FdAcc mn, all;
        EnterCriticalSection(&g_fdCs);
        mn = g_fdMin, g_fdMin = {};
        LeaveCriticalSection(&g_fdCs);
        // in world = ts3fixes "world:" lines outnumber its "world quit" lines (userdiag.cpp, as ModTime); frames are no test
        // (EA smoke 1: Present untimed, every minute "not in world"). Without [UserDiag]: the sim loop ran once a second + frames.
        const bool inWorld = FdInWorld(g_udOn != 0, g_udWorlds, g_udQuits, mn.simIters, mn.frames);
        if (inWorld) g_fdAllMin += wallS / 60, g_fdAllWall += wallS, g_fdAllMainCpu += mainMs, g_fdAllSimCpu += simMs, g_fdAllRd += rd, g_fdAllWr += wr, g_fdAllOps += ops;
        const FdFacts f = FdFactsOf(mn, wallS, mainMs, simMs, wallS / 60);
        char v[640];
        const int code = FdVerdict(f, v, sizeof(v));
        SYSTEMTIME st;
        GetLocalTime(&st);
        FdRow& r = g_fdRows[g_fdRowN++ % 60];
        _snprintf_s(r.t, _TRUNCATE, "%02d:%02d", st.wHour, st.wMinute);
        r.fps = f.fps, r.p50 = f.frameP50, r.p99 = f.frameP99, r.maxMs = mn.frameMax, r.presentMsS = f.presentMsS, r.mainCpu = f.mainCpuMsS, r.simCpu = f.simCpuMsS;
        r.simIterS = f.simIterS, r.d3dPct = f.mainD3dPct < 0 ? 0 : f.mainD3dPct, r.rdMB = rd, r.wrMB = wr, r.ioOps = ops, r.h100 = mn.hitch100, r.h250 = mn.hitch250;
        Log("framediag: %s fps %.1f, frame p50 %.1f p99 %.1f max %.0f ms, present %.0f ms/s (p99 %.1f ms), main cpu %.0f ms/s, sim cpu %.0f ms/s, sim %.1f it/s (p50 %.0f ms), "
            "main in d3d9 %.0f%%, io %.1f/%.1f MB %.0f ops, hitches %ld/%ld%s; verdict %d",
            r.t, f.fps, f.frameP50, f.frameP99, mn.frameMax, f.presentMsS, FdPct(mn.ph, 0.99), f.mainCpuMsS, f.simCpuMsS, f.simIterS, f.simP50, r.d3dPct, rd, wr, ops,
            mn.hitch100, mn.hitch250, inWorld ? "" : " (not in world)", code);
        HANDLE c = CreateFileW(g_fdCsv, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (c != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz = {};
            GetFileSizeEx(c, &sz);
            char line[512];
            int n = 0;
            if (!sz.QuadPart) n = _snprintf_s(line, _TRUNCATE, "time,in_world,fps,frame_p50_ms,frame_p99_ms,frame_max_ms,present_ms_s,present_p99_ms,main_cpu_ms_s,sim_cpu_ms_s,sim_iter_s,sim_p50_ms,main_d3d9_pct,read_mb,write_mb,io_ops,hitch100,hitch250,verdict\r\n");
            _snprintf_s(line + (n > 0 ? n : 0), sizeof(line) - (n > 0 ? n : 0), _TRUNCATE, "%s,%d,%.2f,%.2f,%.2f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,%.0f,%ld,%ld,%d\r\n",
                        r.t, inWorld ? 1 : 0, f.fps, f.frameP50, f.frameP99, mn.frameMax, f.presentMsS, FdPct(mn.ph, 0.99), f.mainCpuMsS, f.simCpuMsS, f.simIterS, f.simP50,
                        r.d3dPct, rd, wr, ops, mn.hitch100, mn.hitch250, code);
            DWORD w;
            WriteFile(c, line, (DWORD)strlen(line), &w, NULL);
            CloseHandle(c);
        }
        EnterCriticalSection(&g_fdCs);
        all = g_fdAll;
        LeaveCriticalSection(&g_fdCs);
        FdWriteSummary(all);
    }
}

static bool ApplyFrameDiag(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"FrameDiag", L"Enabled", 0, ini)) return false;
    LARGE_INTEGER fq;
    QueryPerformanceFrequency(&fq);
    g_fdQf = fq.QuadPart;
    wcscpy_s(g_fdSum, ini);
    wchar_t* slash = wcsrchr(g_fdSum, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    wcscpy_s(g_fdCsv, g_fdSum);
    wcscat_s(g_fdSum, L"fps-summary.txt");
    wcscat_s(g_fdCsv, L"framediag.csv");
    InitializeCriticalSection(&g_fdCs);
    void** slot = FdIatSlot();  // Steam: the import table; EA: the minute thread finds it by value
    const bool imp = FdHookCreate9(slot);
    const bool iter = SimLoopAdd(FdSimIter, "framediag: sim iteration times");
    HANDLE t = CreateThread(NULL, 64 * 1024, FdThread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (t) CloseHandle(t);
    g_fdOn = t ? 1 : 0;
    Log("%s framediag ([FrameDiag] Enabled=1, docs/300): Present through the game device's vtable (polled) and %s, sim iterations %s, main-thread sampler 50 Hz, "
        "fps-summary.txt + framediag.csv next to this log", t ? "OK  " : "FAIL",
        imp ? "the d3d9!Direct3DCreate9 import" : slot ? "no import wrapper (the import slot is empty or wrapped already)" : "the d3d9!Direct3DCreate9 slot searched by value (no import entry)",
        iter ? "timed" : "not timed");
    return g_fdOn != 0;
}
