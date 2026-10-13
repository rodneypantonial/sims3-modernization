// Offline check for phase 9 (docs/15): runs the game's own water-grid jump (0x61FE10 and the functions it calls, plus the
// route callback 0x66DC50) against WaterJump on random grids and asserts identical results and callback calls, then times both.
// The game's code is copied read-only from TS3W.exe to its own addresses in this process (it only calls itself; its switch
// tables are in the same pages). Built by build.cmd, not run there (needs the exe):
//   out\waterjps-test.exe ["<path>\TS3W.exe"] [grids, default 300]
#define TS3NATIVE_NO_DLLMAIN
#include "ts3native.cpp"
#include <assert.h>

static DWORD g_seed = 12345;
static DWORD Rnd(DWORD n) { g_seed = g_seed * 1103515245 + 12345; return ((g_seed >> 8) & 0xFFFFFF) % n; }

// Takes [lo, hi) at its own address (RWX), first thing in main, before any big allocation can land there.
static bool Reserve(DWORD lo, DWORD hi) {
    const DWORD res = lo & ~0xFFFFu;  // reservations start on 64 KB boundaries
    u8* r = (u8*)VirtualAlloc((void*)(uintptr_t)res, hi - res, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (r == (u8*)(uintptr_t)res) return true;
    MEMORY_BASIC_INFORMATION m = {};
    VirtualQuery((void*)(uintptr_t)lo, &m, sizeof(m));
    printf("cannot map 0x%08lX (err %lu): taken by a 0x%lX region at %p, type 0x%lX (randomized heap)\n", lo, GetLastError(), (DWORD)m.RegionSize,
           m.AllocationBase, m.Type);
    fflush(stdout);
    return false;
}

// Copies [lo, hi) of the exe's image to the same VAs (reserved above).
static bool MapCode(const u8* img, DWORD lo, DWORD hi) {
    const DWORD pe = *(const DWORD*)(img + 0x3C), base = *(const DWORD*)(img + pe + 0x34);
    const WORD ns = *(const WORD*)(img + pe + 6), oh = *(const WORD*)(img + pe + 20);
    u8* at = (u8*)(uintptr_t)lo;
    for (int i = 0; i < ns; i++) {
        const u8* s = img + pe + 24 + oh + i * 40;
        const DWORD va = base + *(const DWORD*)(s + 12), vs = *(const DWORD*)(s + 8), raw = *(const DWORD*)(s + 20);
        if (lo >= va && hi <= va + vs) { memcpy(at, img + raw + (lo - va), hi - lo); return true; }
    }
    printf("0x%08lX not in one section\n", lo);
    return false;
}

// A callback that is not 0x66DC50 (called through the vtable): pure in x,y, counts its calls.
static int g_cbCalls;
static DWORD g_cbSalt;
static u8 __fastcall OtherNear(void*, void*, const DWORD* xy) {
    g_cbCalls++;
    return ((xy[0] * 7 + xy[1] * 13 + g_cbSalt) % 29) == 0;
}

struct Cb { void** vt; DWORD pad[3]; int sx, sy, gx, gy; };

static void FillGrid(u8* g1, u8* g2, DWORD w, DWORD h) {
    const DWORD land = Rnd(4) == 0 ? 0 : 5 + Rnd(40);  // % of land cells, sometimes open sea
    const DWORD marks = Rnd(3) == 0 ? 0 : Rnd(8);     // % of g2 cells
    for (DWORD i = 0; i < w * h; i++) g1[i] = Rnd(100) >= land, g2[i] = Rnd(100) < marks;
    // blobs: islands and marked patches, so long runs and corners appear
    for (int b = Rnd(12); b > 0; b--) {
        const DWORD cx = Rnd(w), cy = Rnd(h), r = 1 + Rnd(12);
        const bool mark = Rnd(3) == 0;
        for (DWORD y = cy > r ? cy - r : 0; y < h && y <= cy + r; y++)
            for (DWORD x = cx > r ? cx - r : 0; x < w && x <= cx + r; x++)
                if (mark) g2[y * w + x] = 1; else g1[y * w + x] = 0;
    }
}

static int g_argc;
static char** g_argv;
static DWORD WINAPI Run(void*);

int main(int argc, char** argv) {
    if (!Reserve(0x0061F000, 0x00621000) || !Reserve(0x0066D000, 0x0066E000)) {
        // a randomized heap sometimes sits there: start a fresh copy of this process (up to 20 tries)
        char n[8] = "0";
        GetEnvironmentVariableA("WJT_TRY", n, sizeof(n));
        const int tries = atoi(n) + 1;
        if (tries > 20) return 2;
        sprintf_s(n, "%d", tries);
        SetEnvironmentVariableA("WJT_TRY", n);
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        if (!CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 2;
        DWORD rc = 2;
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &rc);
        return (int)rc;
    }
    g_argc = argc, g_argv = argv;
    // the game's jump recurses once per cell (a scan that wraps rows can go w*h deep): run on a 256 MB stack
    HANDLE t = CreateThread(NULL, 256u << 20, Run, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    DWORD rc = 2;
    if (!t || WaitForSingleObject(t, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeThread(t, &rc)) return 2;
    return (int)rc;
}

static DWORD WINAPI Run(void*) {
    const char* exe = g_argc > 1 ? g_argv[1] : "..\\..\\game-copy\\bin\\TS3W.exe";
    const int grids = g_argc > 2 ? atoi(g_argv[2]) : 300;
    HANDLE f = CreateFileA(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { printf("cannot open %s\n", exe); return 2; }
    const DWORD size = GetFileSize(f, NULL);
    u8* img = (u8*)malloc(size);
    DWORD got;
    if (!img || !ReadFile(f, img, size, &got, NULL) || got != size) { printf("cannot read %s\n", exe); return 2; }
    CloseHandle(f);
    if (!MapCode(img, 0x0061F000, 0x00621000) || !MapCode(img, 0x0066D000, 0x0066E000)) return 2;
    free(img);
    // the same checks ApplyWaterJps makes, then the hook's install on the mapped copy
    assert(WjFnv(WJ_CODE_LO, WJ_CODE_HI) == WJ_CODE_FNV && WjFnv(WJ_NEAR_CB, WJ_NEAR_CB + 0x60) == WJ_NEAR_FNV);
    g_wjNearOk = true;
    Ts3nCounters& k = ts3native_counters;
    k.waterJpsMode = 1;

    void* nearVt[3] = {NULL, (void*)(uintptr_t)WJ_NEAR_CB, NULL};
    void* otherVt[3] = {NULL, (void*)OtherNear, NULL};
    const DWORD kMax = 160;
    u8* g1 = (u8*)malloc(kMax * kMax);
    u8* g2 = (u8*)malloc(kMax * kMax);
    long long checks = 0, hits = 0;
    for (int gi = 0; gi < grids; gi++) {
        const DWORD w = 2 + Rnd(gi < 20 ? 6 : kMax - 1), h = 1 + Rnd(gi < 20 ? 6 : kMax);
        FillGrid(g1, g2, w, h);        Cb cb = {NULL, {0, 0, 0}, (int)Rnd(w), (int)Rnd(h), (int)Rnd(w), (int)Rnd(h)};
        const int kind = gi % 3;  // 0 = no callback, 1 = the game's 0x66DC50, 2 = another callback through the vtable
        cb.vt = kind == 1 ? nearVt : otherVt;
        void* cbp = kind == 0 ? NULL : &cb;
        g_cbSalt = Rnd(29);
        const DWORD cells = w * h, step = cells > 4000 ? 1 + Rnd(7) : 1;  // big grids: a sample of start cells
        for (DWORD cell = Rnd(step); cell < cells; cell += step) {
            const DWORD goal = Rnd(4) == 0 ? 0xFFFFFFFF : Rnd(cells);
            for (DWORD dir = 0; dir < 8; dir++)
                for (DWORD flag = 0; flag < 2; flag++) {
                    const DWORD junk = flag ? (Rnd(256) << 8) | 1 : Rnd(256) << 8;  // the game only reads the flag's low byte
                    k.waterJpsMode = 0;
                    g_cbCalls = 0;
                    const DWORD a = WaterJumpHook((const u8* const*)&g1, (const u8* const*)&g2, w, h, goal, cell, dir, junk, cbp);
                    const int ca = g_cbCalls;
                    k.waterJpsMode = 1;
                    g_cbCalls = 0;
                    const DWORD b = WaterJumpHook((const u8* const*)&g1, (const u8* const*)&g2, w, h, goal, cell, dir, junk, cbp);
                    if (a != b || ca != g_cbCalls) {
                        printf("MISMATCH grid %d w %lu h %lu cell %lu dir %lu flag %lu goal %ld cb %d: game %ld (cb %d) fast %ld (cb %d)\n", gi, w, h,
                               cell, dir, flag, (long)goal, kind, (long)a, ca, (long)b, g_cbCalls);
                        return 1;
                    }
                    checks++;
                    hits += a != 0xFFFFFFFF;
                }
        }
    }
    assert(k.waterJpsFallbacks == 0 && k.waterJpsMismatches == 0);
    printf("identical: %lld jumps on %d grids (%lld found a jump point)\n", checks, grids, hits);

    // Verify mode: a wrong fast result is replaced by the game's (forced by a fake 'game' jump that disagrees).
    k.waterJpsVerify = 1;
    g1[0] = g1[1] = 1; g2[0] = g2[1] = 0;
    assert(WaterJumpHook((const u8* const*)&g1, (const u8* const*)&g2, 2, 1, 1, 0, 2, 0, NULL) == 1 && k.waterJpsMismatches == 0);
    g_wjOrig = [](const u8* const*, const u8* const*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*) -> DWORD { return 0xFFFFFFFF; };
    assert(WaterJumpHook((const u8* const*)&g1, (const u8* const*)&g2, 2, 1, 1, 0, 2, 0, NULL) == 0xFFFFFFFF && k.waterJpsMismatches == 1);
    g_wjOrig = (WjJumpFn)(uintptr_t)WJ_JUMP;
    k.waterJpsVerify = 0;
    k.waterJpsMismatches = 0;

    // Timing: a 512 x 512 sea with islands and a sparse cost layer, the game's callback, the same queries for both.
    const DWORD W = 512, H = 512, Q = 400000;
    u8* s1 = (u8*)malloc(W * H);
    u8* s2 = (u8*)malloc(W * H);
    g_seed = 777;
    for (DWORD i = 0; i < W * H; i++) s1[i] = 1, s2[i] = Rnd(1000) == 0;
    for (int b = 0; b < 300; b++) {
        const DWORD cx = Rnd(W), cy = Rnd(H), r = 2 + Rnd(10);
        for (DWORD y = cy > r ? cy - r : 0; y < H && y <= cy + r; y++)
            for (DWORD x = cx > r ? cx - r : 0; x < W && x <= cx + r; x++) s1[y * W + x] = 0;
    }
    DWORD* qs = (DWORD*)malloc(Q * 2 * sizeof(DWORD));
    for (DWORD i = 0; i < Q; i++) {
        DWORD c;
        do c = Rnd(W * H); while (!s1[c]);
        qs[i * 2] = c, qs[i * 2 + 1] = Rnd(8);
    }
    g_wjCalls = g_wjCells = 0;
    Cb ncb = {nearVt, {0, 0, 0}, 100, 100, 400, 300};
    LARGE_INTEGER qpf, t0, t1;
    QueryPerformanceFrequency(&qpf);
    double ms[2] = {1e30, 1e30};
    DWORD sum[2] = {0, 0};
    for (int pass = 0; pass < 3; pass++)
        for (int mode = 0; mode < 2; mode++) {
            k.waterJpsMode = mode;
            DWORD s = 0;
            QueryPerformanceCounter(&t0);
            for (DWORD i = 0; i < Q; i++)
                s += WaterJumpHook((const u8* const*)&s1, (const u8* const*)&s2, W, H, 300 * W + 400, qs[i * 2], qs[i * 2 + 1], 0, &ncb);
            QueryPerformanceCounter(&t1);
            const double t = (t1.QuadPart - t0.QuadPart) * 1000.0 / qpf.QuadPart;
            if (t < ms[mode]) ms[mode] = t;
            sum[mode] = s;
        }
    assert(sum[0] == sum[1]);
    printf("timing (%lu jumps, best of 3): game %.1f ms, WaterJump %.1f ms, %.2fx; %.1f cells per jump\n", Q, ms[0], ms[1], ms[0] / ms[1],
           (double)g_wjCells / (double)g_wjCalls);
    return 0;
}
