// Offline check for phase 10 (docs/15): the game's own RefPack compressor (0x4EB340..0x4EC282, copied read-only from TS3W.exe
// to its own addresses here, with memcpy/memset behind its import slots) on the records of a real save package:
//  1. determinism: the same output with scratch tables full of junk or of zeros, and the size-only pass gives the same length;
//  2. the stored bytes: how many compressed records of the save the game's code reproduces exactly (mode from the header);
//  3. the pipeline: a fake commit of all records through the codec slot, mode 0 (the game's slot, serial) vs mode 1 (workers),
//     every record's output byte-identical; times both.
// Built by build.cmd, not run there (needs the exe and a save):
//   out\savecompress-test.exe "<path>\TS3W.exe" "<Saves>\<name>.sims3\<world>.nhd" [threads] [MaxMB] [compression +0x20]
#define TS3NATIVE_NO_DLLMAIN
#include "ts3native.cpp"
#include "savecompress-fake.h"
#include <assert.h>
extern "C" void __cdecl _alloca_probe_16();  // CRT helper with the game's contract (eax = size, 16-byte aligned)

static bool Reserve(DWORD lo, DWORD hi) {
    const DWORD res = lo & ~0xFFFFu;
    u8* r = (u8*)VirtualAlloc((void*)(uintptr_t)res, hi - res, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (r == (u8*)(uintptr_t)res) return true;
    printf("cannot map 0x%08lX (err %lu)\n", lo, GetLastError());
    return false;
}
static bool MapCode(const u8* img, DWORD lo, DWORD hi) {
    const DWORD pe = *(const DWORD*)(img + 0x3C), base = *(const DWORD*)(img + pe + 0x34);
    const WORD ns = *(const WORD*)(img + pe + 6), oh = *(const WORD*)(img + pe + 20);
    for (int i = 0; i < ns; i++) {
        const u8* s = img + pe + 24 + oh + i * 40;
        const DWORD va = base + *(const DWORD*)(s + 12), vs = *(const DWORD*)(s + 8), raw = *(const DWORD*)(s + 20);
        if (lo >= va && hi <= va + vs) { memcpy((void*)(uintptr_t)lo, img + raw + (lo - va), hi - lo); return true; }
    }
    printf("0x%08lX not in one section\n", lo);
    return false;
}
static u8* ReadAll(const char* path, DWORD* size) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { printf("cannot open %s\n", path); return NULL; }
    *size = GetFileSize(f, NULL);
    u8* b = (u8*)malloc(*size);
    DWORD got;
    if (!b || !ReadFile(f, b, *size, &got, NULL) || got != *size) { printf("cannot read %s\n", path); return NULL; }
    CloseHandle(f);
    return b;
}

// A game-style allocator (ICoreAllocator slots +8 alloc, +0xC free) whose blocks start full of junk or of zeros.
static DWORD g_seed = 99;
static bool g_junk = true;
static void* __fastcall TaMalloc(void*, void*, DWORD n, const char*, DWORD) {
    u8* p = (u8*)malloc(n);
    g_seed = g_seed * 1103515245 + 12345;
    memset(p, g_junk ? (int)(0x80 | (g_seed >> 16)) & 0xFF : 0, n);  // a different junk byte per block
    return p;
}
static void __fastcall TaFree(void*, void*, void* p, DWORD) { free(p); }
static void* g_taVt[4] = {NULL, NULL, (void*)TaMalloc, (void*)TaFree};
static void* g_ta = g_taVt;

struct Rec { u8* data; DWORD len; const u8* stored; DWORD storedLen; WORD hdr; };

static double Ms(LARGE_INTEGER a, LARGE_INTEGER b) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!Reserve(0x004EB000, 0x004ED000)) {
        // a randomized heap or stack sometimes sits there: start a fresh copy of this process (up to 20 tries)
        char t[8] = "0";
        GetEnvironmentVariableA("SCT_TRY", t, sizeof(t));
        const int tries = atoi(t) + 1;
        if (tries > 20) return 2;
        sprintf_s(t, "%d", tries);
        SetEnvironmentVariableA("SCT_TRY", t);
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        if (!CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 2;
        DWORD rc = 2;
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &rc);
        return (int)rc;
    }
    if (argc < 3) { printf("usage: savecompress-test <TS3W.exe> <save .nhd> [threads] [MaxMB] [compression +0x20]\n"); return 2; }
    DWORD exeSize, pkgSize;
    u8* img = ReadAll(argv[1], &exeSize);
    u8* pkg = ReadAll(argv[2], &pkgSize);
    if (!img || !pkg || !MapCode(img, 0x004EB000, 0x004ED000)) return 2;
    free(img);
    printf("compressor code hash 0x%08lX (expected 0x%08lX)\n", ScCodeHash(), SC_CODE_FNV);
    if (ScCodeHash() != SC_CODE_FNV) return 1;
    // The code's only calls out of its pages go to the CRT: memcpy (thunk 0xF7E8A6), memset (0xF7E8B8) and the alloca probe
    // (0xF7E8D0). Something of Windows' own sits at 0xF70000 in this process, so the copied calls are pointed at this
    // process's CRT instead (after the hash above).
    int redirected = 0;
    for (DWORD at = 0x004EB340; at < 0x004EC282; at++) {
        u8* c = (u8*)(uintptr_t)at;
        if (*c != 0xE8) continue;
        const DWORD t = at + 5 + *(LONG*)(c + 1);
        void* to = t == 0x00F7E8A6 ? (void*)memcpy : t == 0x00F7E8B8 ? (void*)memset : t == 0x00F7E8D0 ? (void*)_alloca_probe_16 : NULL;
        if (!to) continue;
        *(LONG*)(c + 1) = (LONG)((u8*)to - (c + 5));
        redirected++;
    }
    printf("%d CRT calls redirected\n", redirected);
    QueryPerformanceFrequency(&g_qpf);

    // DBPF 2.0 index; compressed records decoded with RefpackRef
    const DWORD n = *(const DWORD*)(pkg + 0x24), ipos = *(const DWORD*)(pkg + 0x40), flags = *(const DWORD*)(pkg + ipos);
    DWORD o = ipos + 4, cf[8] = {0};
    for (int i = 0; i < 8; i++) if (flags & (1u << i)) { cf[i] = *(const DWORD*)(pkg + o); o += 4; }
    Rec* recs = (Rec*)calloc(n, sizeof(Rec));
    int nr = 0, ncomp = 0;
    LONG64 total = 0;
    for (DWORD r = 0; r < n; r++) {
        DWORD f[8];
        for (int i = 0; i < 8; i++) if (flags & (1u << i)) f[i] = cf[i]; else { f[i] = *(const DWORD*)(pkg + o); o += 4; }
        const DWORD off = f[4], fsize = f[5] & 0x7FFFFFFF, mem = f[6], comp = f[7] & 0xFFFF;
        if (mem < 0x32 || mem > kScMaxLen) continue;
        Rec& x = recs[nr];
        x.len = mem;
        x.data = (u8*)malloc(mem + 64);
        if (comp) {
            if (RefpackRef(x.data, mem, pkg + off, fsize) != (int)mem) { printf("record %lu does not decode\n", r); return 1; }
            x.stored = pkg + off, x.storedLen = fsize, x.hdr = (WORD)(pkg[off] << 8 | pkg[off + 1]);
            ncomp++;
        } else {
            memcpy(x.data, pkg + off, mem);
        }
        total += mem;
        nr++;
    }
    printf("%d records (%d stored compressed), %.1f MB\n", nr, ncomp, total / 1048576.0);

    // 1 + 2: determinism and the stored bytes
    u8* a = (u8*)malloc(ScOutCap(kScMaxLen));
    u8* b = (u8*)malloc(ScOutCap(kScMaxLen));
    int same = 0, exact = 0, exactMode[3] = {0, 0, 0}, checked = 0;
    for (int i = 0; i < nr; i++) {
        Rec& x = recs[i];
        const DWORD modes[2] = {x.hdr == 0x10FB ? 1u : 0u, x.hdr == 0x10FB ? 1u : 2u};
        bool hit = false;
        for (int m = 0; m < 2 && !hit; m++) {
            const DWORD mode = modes[m];
            if (m && mode == modes[0]) break;
            g_junk = true;
            const int la = g_scCompress(a, ScOutCap(x.len), x.data, x.len, &g_ta, mode);
            if (m == 0 && (i % 7 == 0 || x.len > 1000000)) {  // a sample: zeroed scratch and the size-only pass
                g_junk = false;
                const int lb = g_scCompress(b, ScOutCap(x.len), x.data, x.len, &g_ta, mode);
                const int lc = g_scCompress(NULL, 0, x.data, x.len, &g_ta, mode);
                if (la != lb || la != lc || memcmp(a, b, (size_t)la)) { printf("NOT DETERMINISTIC record %d len %lu mode %lu: %d %d %d\n", i, x.len, mode, la, lb, lc); return 1; }
                same++;
            }
            if (RefpackRef(b, x.len, a, (DWORD)la) != (int)x.len || memcmp(b, x.data, x.len)) { printf("record %d does not round-trip\n", i); return 1; }
            if (x.stored && (DWORD)la == x.storedLen && !memcmp(a, x.stored, (size_t)la)) { hit = true; exact++; exactMode[mode]++; }
        }
        if (x.stored) checked++;
    }
    printf("determinism: %d records identical with junk and zeroed scratch and the size-only pass\n", same);
    printf("stored bytes: %d of %d compressed records reproduced exactly (mode 0: %d, 1: %d, 2: %d)\n", exact, checked, exactMode[0], exactMode[1], exactMode[2]);

    // 3: the pipeline, serial (mode 0) vs workers (mode 1), whole package as one commit
    g_scLock = FkLock;
    g_scUnlock = FkUnlock;
    g_scCommitOrig = FkCommit;
    g_scThreads = argc > 3 ? Clamp(atoi(argv[3]), 1, 8) : 6;
    g_sb.maxBytes = (LONG64)(argc > 4 ? Clamp(atoi(argv[4]), 4, 256) : 32) << 20;
    g_fkCodecVt[1] = (void*)ScSlotHook;
    g_fkCodec.vt = g_fkCodecVt;
    g_fkCodec.alloc = &g_ta;
    g_junk = true;
    const u8** data = (const u8**)malloc(nr * sizeof(u8*));
    DWORD* lens = (DWORD*)malloc(nr * sizeof(DWORD));
    for (int i = 0; i < nr; i++) data[i] = recs[i].data, lens[i] = recs[i].len;
    u8** out[2];
    int* outLen[2];
    double best[2] = {1e30, 1e30};
    Ts3nCounters& k = ts3native_counters;
    const int compression = argc > 5 ? atoi(argv[5]) : 1;  // package +0x20: 1 = flags 0x10001 (mode 2), as most records of this save
    for (int pass = 0; pass < 3; pass++)
        for (int mode = 0; mode < 2; mode++) {
            FkPkg p;
            FkBuild(&p, data, lens, nr, compression);
            if (pass) for (int i = 0; i < nr; i++) free(out[mode][i]);
            if (!pass) out[mode] = (u8**)calloc(nr, sizeof(u8*)), outLen[mode] = (int*)calloc(nr, sizeof(int));
            g_fkOut = out[mode];
            g_fkOutLen = outLen[mode];
            k.compressMode = mode;
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
            ScCommitHook(&p, NULL);
            QueryPerformanceCounter(&t1);
            const double ms = Ms(t0, t1);
            if (ms < best[mode]) best[mode] = ms;
            FkFree(&p);
        }
    for (int i = 0; i < nr; i++)
        if (outLen[0][i] != outLen[1][i] || memcmp(out[0][i], out[1][i], (size_t)outLen[0][i])) { printf("PIPELINE MISMATCH record %d\n", i); return 1; }
    int matchStored = 0;
    for (int i = 0; i < nr; i++)
        if (recs[i].stored && (DWORD)outLen[1][i] == recs[i].storedLen && !memcmp(out[1][i], recs[i].stored, recs[i].storedLen)) matchStored++;
    printf("pipeline: %d records identical serial vs %d workers (MaxMB %lld); batches %ld jobs %ld hits %ld misses %ld peak %ld MB\n", nr,
           g_scThreads, g_sb.maxBytes >> 20, k.compressBatches, k.compressJobs, k.compressHits, k.compressMisses, k.compressPeakMB);
    printf("pipeline output equal to the stored bytes for %d of %d compressed records\n", matchStored, ncomp);
    printf("commit time (best of 3): serial %.0f ms, workers %.0f ms, %.1fx\n", best[0], best[1], best[0] / best[1]);
    return 0;
}
