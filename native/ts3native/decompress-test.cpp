// Offline check for phase 6 (docs/15): decodes real RefPack resources from the installed game's .package files
// (read-only) with the port of the game's decoder (RefpackRef) and with RefpackFast, asserts identical results and
// bytes, and reports MB/s for each. Built by build.cmd, not run there (needs the game, takes a minute):
//   out\decompress-test.exe ["<game dir>"] [MB of compressed input, default 768]
// Optional third decoder for comparison (not in the repo): cl ... /DEXTRA_DECODER="\"file.h\"" with a file that
// defines `static int __cdecl ExtraDecode(u8* dst, DWORD cap, const u8* src, DWORD len)` and EXTRA_NAME.
#define TS3NATIVE_NO_DLLMAIN
#include "ts3native.cpp"
#ifdef EXTRA_DECODER
#include EXTRA_DECODER
#endif

struct Blob { const u8* src; DWORD len, size; };
static u8* g_pool;
static size_t g_poolUsed, g_poolCap;
static Blob* g_blobs;
static size_t g_nBlobs, g_blobCap;
static DWORD g_maxSize;
static double g_total, g_ratio, g_credit;  // compressed bytes in all packages; sampled share; carry

// collect = false: only add the package's RefPack bytes to g_total. true: read a byte-proportional sample (g_ratio).
static void AddPackage(const wchar_t* path, bool collect) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    u8 hd[96];
    DWORD got;
    if (!ReadFile(h, hd, 96, &got, NULL) || got != 96 || memcmp(hd, "DBPF", 4) != 0) { CloseHandle(h); return; }
    const DWORD count = *(DWORD*)(hd + 36), isz = *(DWORD*)(hd + 44), ipos = *(DWORD*)(hd + 64);
    u8* ix = (u8*)malloc(isz);
    LARGE_INTEGER at;
    at.QuadPart = ipos;
    SetFilePointerEx(h, at, NULL, FILE_BEGIN);
    if (!ix || !ReadFile(h, ix, isz, &got, NULL) || got != isz) { free(ix); CloseHandle(h); return; }
    const u8* p = ix;
    const DWORD flags = *(DWORD*)p;
    p += 4;
    for (int b = 0; b < 3; b++) if (flags & (1u << b)) p += 4;
    for (DWORD i = 0; i < count; i++) {
        for (int b = 0; b < 3; b++) if (!(flags & (1u << b))) p += 4;
        const DWORD off = *(DWORD*)(p + 4), fsize = *(DWORD*)(p + 8) & 0x7fffffff;
        const WORD comp = *(WORD*)(p + 16);
        p += 20;
        if (comp != 0xFFFF || fsize < 5) continue;
        if (!collect) { g_total += fsize; continue; }
        g_credit += fsize * g_ratio;
        if (g_credit < fsize || g_poolUsed + fsize > g_poolCap) continue;
        g_credit -= fsize;
        u8* dst = g_pool + g_poolUsed;
        at.QuadPart = off;
        SetFilePointerEx(h, at, NULL, FILE_BEGIN);
        if (!ReadFile(h, dst, fsize, &got, NULL) || got != fsize) continue;
        // as 0x4EC010: signature check, header size is the buffer the game allocates (memSize)
        if (((dst[0] << 8 | dst[1]) & 0x1FFF) != 0x10FB) continue;
        const DWORD sb = (dst[0] & 0x80) ? 4 : 3;
        DWORD size = 0;
        for (DWORD k = 0; k < sb; k++) size = size << 8 | dst[2 + k + ((dst[0] & 1) ? sb : 0)];
        if (g_nBlobs == g_blobCap) g_blobs = (Blob*)realloc(g_blobs, (g_blobCap = g_blobCap * 2 + 1024) * sizeof(Blob));
        g_blobs[g_nBlobs++] = {dst, fsize, size};
        if (size > g_maxSize) g_maxSize = size;
        g_poolUsed += fsize;
    }
    free(ix);
    CloseHandle(h);
}

static void Walk(const wchar_t* dir, bool collect, int* nPkgs) {
    wchar_t pat[MAX_PATH];
    swprintf_s(pat, L"%s\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        wchar_t sub[MAX_PATH];
        swprintf_s(sub, L"%s\\%s", dir, fd.cFileName);
        const size_t n = wcslen(fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Walk(sub, collect, nPkgs);
        else if (n > 8 && _wcsicmp(fd.cFileName + n - 8, L".package") == 0) {
            (*nPkgs)++;
            AddPackage(sub, collect);
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

typedef int(__cdecl* DecodeFn)(u8*, DWORD, const u8*, DWORD);

// One pass over every blob; returns seconds. Small resources (< 16 KB) timed separately.
static double Pass(DecodeFn fn, u8* out, double* smallSec, double* smallMB) {
    LARGE_INTEGER f, t0, t1, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    LONG64 small = 0;
    double sb = 0;
    for (size_t i = 0; i < g_nBlobs; i++) {
        const Blob& x = g_blobs[i];
        if (x.size < 16384) {
            QueryPerformanceCounter(&a);
            fn(out, x.size, x.src, x.len);
            QueryPerformanceCounter(&b);
            small += b.QuadPart - a.QuadPart;
            sb += x.size;
        } else {
            fn(out, x.size, x.src, x.len);
        }
    }
    QueryPerformanceCounter(&t1);
    *smallSec = (double)small / f.QuadPart;
    *smallMB = sb / 1048576;
    return (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* dir = argc > 1 ? argv[1] : L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3";
    const size_t mb = argc > 2 ? (size_t)_wtoi(argv[2]) : 768;
    int nPkgs = 0;
    Walk(dir, false, &nPkgs);
    if (!nPkgs) { printf("no .package files under %ls\n", dir); return 2; }
    g_poolCap = mb << 20;
    g_ratio = g_poolCap * 0.98 / g_total;  // the same share of every package's RefPack bytes
    g_pool = (u8*)VirtualAlloc(NULL, g_poolCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_pool) { printf("no memory for %zu MB\n", mb); return 2; }
    nPkgs = 0;
    Walk(dir, true, &nPkgs);
    printf("%d packages, %.0f MB RefPack in all; sample: %zu resources, %.0f MB compressed, largest %lu bytes\n", nPkgs,
           g_total / 1048576, g_nBlobs, g_poolUsed / 1048576.0, g_maxSize);

    // Identity: result and bytes, plus guard bytes past the buffer (nothing written beyond cap).
    const DWORD guard = 64;
    u8* a = (u8*)malloc(g_maxSize + guard);
    u8* b = (u8*)malloc(g_maxSize + guard);
    double outMB = 0;
    size_t bad = 0, refFails = 0, shortOut = 0;
    for (size_t i = 0; i < g_nBlobs; i++) {
        const Blob& x = g_blobs[i];
        memset(b + x.size, 0xAA, guard);
        if (x.size) a[x.size - 1] = 0xAA;
        const int ra = RefpackRef(a, x.size, x.src, x.len), rb = RefpackFast(b, x.size, x.src, x.len);
        if (!ra) refFails++;
        // output is sequential: the last byte written under both fills means the stream filled its whole header size
        if (ra && x.size && a[x.size - 1] == 0xAA) {
            a[x.size - 1] = 0x55;
            RefpackRef(a, x.size, x.src, x.len);
            if (a[x.size - 1] == 0x55) shortOut++;
        }
        bool ok = ra == rb && (ra <= 0 || memcmp(a, b, x.size) == 0);
        for (DWORD g = 0; g < guard; g++) ok = ok && b[x.size + g] == 0xAA;
#ifdef EXTRA_DECODER
        memset(b, 0, x.size);
        const int rc = ExtraDecode(b, x.size, x.src, x.len);
        ok = ok && rc == ra && (ra <= 0 || memcmp(a, b, x.size) == 0);
#endif
        if (!ok && ++bad <= 5) printf("MISMATCH resource %zu: len %lu size %lu ref %d fast %d\n", i, x.len, x.size, ra, rb);
        outMB += x.size / 1048576.0;
    }
    printf("identity: %zu resources, %.0f MB out, %zu mismatches, %zu the game's decoder rejects, %zu end short of their header size\n",
           g_nBlobs, outMB, bad, refFails, shortOut);

    double best[3] = {1e9, 1e9, 1e9}, bestSmall[3] = {1e9, 1e9, 1e9}, smallMB = 0;
    DecodeFn fns[3] = {RefpackRef, RefpackFast, NULL};
    const char* names[3] = {"game (port of 0x4EB3B0)", "RefpackFast", NULL};
#ifdef EXTRA_DECODER
    fns[2] = ExtraDecode;
    names[2] = EXTRA_NAME;
#endif
    for (int rep = 0; rep < 3; rep++)
        for (int k = 0; k < 3; k++) {
            if (!fns[k]) continue;
            double ss;
            const double s = Pass(fns[k], a, &ss, &smallMB);
            if (s < best[k]) best[k] = s;
            if (ss < bestSmall[k]) bestSmall[k] = ss;
        }
    for (int k = 0; k < 3; k++)
        if (fns[k])
            printf("%-26s %7.0f MB/s all (%.2f s for %.0f MB)   %7.0f MB/s on resources < 16 KB (%.0f MB)\n", names[k], outMB / best[k], best[k],
                   outMB, smallMB / bestSmall[k], smallMB);
    printf("speedup RefpackFast vs game: %.2fx\n", best[0] / best[1]);
    if (fns[2]) printf("speedup RefpackFast vs %s: %.2fx\n", names[2], best[2] / best[1]);
    return bad ? 1 : 0;
}
