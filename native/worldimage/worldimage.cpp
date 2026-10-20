// worldimage: M3.1 capture helper for the Mono world image (docs/123 §2.5, §7, §8). 32-bit, static CRT.
//   worldimage capture <pid> <simtid> <dir> <exebase hex> <dumped event> <refdone event> tls=<g,..> locks=<a,..>
//       started by WiRequestCapture (wimage.h) inside the target: suspends every thread of <pid>, writes <dir>\header.txt
//       and mem.bin, resumes the requesting (sim) thread for its reference with the others still suspended; 2 s watchdog
//       (then the others are resumed and the image is marked loose), then resumes everything.
//   worldimage selftest <out folder>   fake-process selftest: captures two small test processes (strict and watchdog)
//       and checks the image contents; exit code = failed checks.
//   worldimage target <dir> <0|1>      (selftest only) the test process.
// Images hold the whole private memory of the target: game images contain personal data, keep them in game-copy/.
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include "wimage.h"

typedef LONG(NTAPI* NtQit)(HANDLE, int, PVOID, ULONG, PULONG);
typedef LONG(NTAPI* NtQip)(HANDLE, int, PVOID, ULONG, PULONG);
struct ThreadBasic { LONG exitStatus; PVOID teb; PVOID pid, tid; ULONG_PTR affinity; LONG prio, basePrio; };
struct ProcBasic { LONG exitStatus; PVOID peb; ULONG_PTR affinity; LONG basePrio; ULONG_PTR pid, parent; };

static FILE* g_log;
static void Log(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
    fflush(stdout);
}
static DWORD Rd(HANDLE p, DWORD va) {
    DWORD v = 0;
    ReadProcessMemory(p, (void*)(uintptr_t)va, &v, 4, NULL);
    return v;
}
static int ParseList(const char* arg, const char* key, DWORD* out, int max) {
    size_t kl = strlen(key);
    if (strncmp(arg, key, kl)) return 0;
    int n = 0;
    for (const char* s = arg + kl; *s && n < max;) {
        char* e;
        out[n++] = strtoul(s, &e, 16);
        s = *e == ',' ? e + 1 : e;
        if (e == s && *s) break;
    }
    return n;
}

struct Thr { DWORD tid; HANDLE h; bool suspended; };
static Thr g_thr[512];
static int g_nthr;
// Suspend every thread of pid; repeat until a snapshot finds no new one (threads started meanwhile).
static void SuspendAll(DWORD pid) {
    for (int pass = 0; pass < 8; pass++) {
        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te = {sizeof(te)};
        int added = 0;
        for (BOOL ok = Thread32First(s, &te); ok; ok = Thread32Next(s, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            bool known = false;
            for (int i = 0; i < g_nthr && !known; i++) known = g_thr[i].tid == te.th32ThreadID;
            if (known || g_nthr == 512) continue;
            HANDLE h = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
            if (!h) continue;  // exited meanwhile
            g_thr[g_nthr++] = {te.th32ThreadID, h, SuspendThread(h) != (DWORD)-1};
            added++;
        }
        CloseHandle(s);
        if (!added) return;
    }
}
static void ResumeAll(DWORD except) {
    for (int i = 0; i < g_nthr; i++)
        if (g_thr[i].suspended && g_thr[i].tid != except) { ResumeThread(g_thr[i].h); g_thr[i].suspended = false; }
}

static int Capture(int argc, char** argv) {
    DWORD pid = strtoul(argv[2], NULL, 10), simTid = strtoul(argv[3], NULL, 10), exeBase = strtoul(argv[5], NULL, 16);
    const char* dir = argv[4];
    DWORD tls[16], locks[32];
    int ntls = 0, nlocks = 0;
    for (int i = 8; i < argc; i++) {
        ntls += ParseList(argv[i], "tls=", tls + ntls, 16 - ntls);
        nlocks += ParseList(argv[i], "locks=", locks + nlocks, 32 - nlocks);
    }
    char path[MAX_PATH + 32];
    sprintf_s(path, "%s\\capture.log", dir);
    fopen_s(&g_log, path, "w");
    HANDLE evD = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[6]), evR = OpenEventA(SYNCHRONIZE, FALSE, argv[7]);
    HANDLE p = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!p || !evD || !evR) { Log("cannot open process %lu or events (err %lu)\n", pid, GetLastError()); return 2; }
    NtQit qit = (NtQit)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    NtQip qip = (NtQip)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");
    DWORD t0 = GetTickCount();
    SuspendAll(pid);
    sprintf_s(path, "%s\\mem.bin", dir);
    FILE* mem = NULL;
    char hdrPath[MAX_PATH + 32];
    sprintf_s(hdrPath, "%s\\header.tmp", dir);
    FILE* hdr = NULL;
    if (fopen_s(&mem, path, "wb") || fopen_s(&hdr, hdrPath, "w") || !mem || !hdr) { Log("cannot write the image files in %s\n", dir); ResumeAll(0); return 3; }
    setvbuf(mem, NULL, _IOFBF, 1 << 20);
    char exePath[MAX_PATH] = "?";
    DWORD exeSize = 0;
    // modules (the exe of interest is the one at exeBase)
    static WiModule mods[256];
    int nm = 0;
    HANDLE ms = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32 me = {sizeof(me)};
    for (BOOL ok = Module32First(ms, &me); ok && nm < 256; ok = Module32Next(ms, &me)) {
        WiModule& m = mods[nm++];
        m.base = (DWORD)(uintptr_t)me.modBaseAddr;
        m.size = me.modBaseSize;
        m.stamp = Rd(p, m.base + Rd(p, m.base + 0x3C) + 8);
        strcpy_s(m.path, me.szExePath);
        if (m.base == exeBase) { strcpy_s(exePath, m.path); exeSize = m.size; }
    }
    CloseHandle(ms);
    if (!exeSize && exeBase) {  // an exe mapped with LoadLibraryExW (monohost) is not in the module list: size from its header
        exeSize = Rd(p, exeBase + Rd(p, exeBase + 0x3C) + 0x50);
        strcpy_s(exePath, "(mapped image)");
    }
    fprintf(hdr, "worldimage 1\ntarget %lu %lu %lx %lx %s\n", pid, simTid, exeBase, exeSize, exePath);
    // threads: context, TEB, stack bounds
    DWORD sysAb[1100], simTeb = 0;  // allocation bases of TEBs (32- and 64-bit) and the PEB
    int nsys = 0;
    for (int i = 0; i < g_nthr; i++) {
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_FULL;
        GetThreadContext(g_thr[i].h, &c);  // also completes the asynchronous suspend
        ThreadBasic tb = {};
        qit(g_thr[i].h, 0, &tb, sizeof(tb), NULL);
        DWORD teb = (DWORD)(uintptr_t)tb.teb;
        for (DWORD a = teb - 0x2000; a <= teb; a += 0x2000) {
            MEMORY_BASIC_INFORMATION q;
            if (nsys < 1098 && VirtualQueryEx(p, (void*)(uintptr_t)a, &q, sizeof(q))) sysAb[nsys++] = (DWORD)(uintptr_t)q.AllocationBase;
        }
        if (g_thr[i].tid == simTid) simTeb = teb;
        fprintf(hdr, "thread %lu %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx\n", g_thr[i].tid, teb, Rd(p, teb + 4), Rd(p, teb + 8), c.Eip, c.Esp,
                c.Ebp, c.Eax, c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi, c.EFlags);
    }
    ProcBasic pb = {};
    qip(p, 0, &pb, sizeof(pb), NULL);
    DWORD peb = (DWORD)(uintptr_t)pb.peb;
    MEMORY_BASIC_INFORMATION q;
    if (VirtualQueryEx(p, (void*)(uintptr_t)peb, &q, sizeof(q))) sysAb[nsys++] = (DWORD)(uintptr_t)q.AllocationBase;
    // TLS keys: index from the global, allocated bit from the PEB bitmaps, value from the sim thread's TEB
    for (int i = 0; i < ntls; i++) {
        DWORD idx = Rd(p, tls[i]), alloc = 0, val = 0;
        if (idx < 64) { alloc = (Rd(p, peb + 0x44 + (idx / 32) * 4) >> (idx % 32)) & 1; val = Rd(p, simTeb + 0xE10 + idx * 4); }
        else if (idx < 64 + 1024) {
            alloc = (Rd(p, peb + 0x154 + ((idx - 64) / 32) * 4) >> ((idx - 64) % 32)) & 1;
            DWORD ex = Rd(p, simTeb + 0xF94);
            val = ex ? Rd(p, ex + (idx - 64) * 4) : 0;
        }
        fprintf(hdr, "tls %lx %lx %lu %lx\n", tls[i], idx, alloc, val);
    }
    for (int i = 0; i < nlocks; i++)  // CRITICAL_SECTION: +4 LockCount, +8 RecursionCount, +0xC OwningThread (I-123-17)
        fprintf(hdr, "lock %lx %lx %lx %lu\n", locks[i], Rd(p, locks[i] + 4), Rd(p, locks[i] + 8), Rd(p, locks[i] + 0xC));
    for (int i = 0; i < nm; i++) fprintf(hdr, "module %lx %lx %lx %s\n", mods[i].base, mods[i].size, mods[i].stamp, mods[i].path);
    // regions
    static u_char buf[1 << 20];
    long long off = 0, unread = 0;
    int nr = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (DWORD va = 0x10000; va < 0xFFFE0000 && VirtualQueryEx(p, (void*)(uintptr_t)va, &mbi, sizeof(mbi)); va = (DWORD)(uintptr_t)mbi.BaseAddress + (DWORD)mbi.RegionSize) {
        DWORD base = (DWORD)(uintptr_t)mbi.BaseAddress, size = (DWORD)mbi.RegionSize, ab = (DWORD)(uintptr_t)mbi.AllocationBase;
        if (!size) break;
        if (mbi.State == MEM_FREE) continue;
        const char* cls = NULL;
        bool exe = mbi.Type == MEM_IMAGE && exeBase && base >= exeBase && base - exeBase < exeSize;
        if (exe) cls = mbi.State == MEM_COMMIT ? "exe" : "reserve";
        else if (mbi.Type == MEM_PRIVATE) {
            bool sys = base >= 0x7FFE0000 && base < 0x7FFF0000;  // KUSER_SHARED_DATA
            for (int i = 0; i < nsys && !sys; i++) sys = sysAb[i] == ab;
            cls = sys ? "system" : mbi.State == MEM_RESERVE ? "reserve" : "priv";
        }
        if (!cls) continue;  // other images, mapped files
        long long at = -1;
        if (!strcmp(cls, "priv") || !strcmp(cls, "exe")) {
            if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) cls = "noread";
            else {
                at = off;
                for (DWORD o = 0; o < size; o += sizeof(buf)) {
                    DWORD n = min((DWORD)sizeof(buf), size - o);
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(p, (void*)(uintptr_t)(base + o), buf, n, &got) || got != n)
                        for (DWORD pg = 0; pg < n; pg += 0x1000)  // page by page; unreadable pages stay zero
                            if (!ReadProcessMemory(p, (void*)(uintptr_t)(base + o + pg), buf + pg, 0x1000, NULL)) { memset(buf + pg, 0, 0x1000); unread++; }
                    fwrite(buf, 1, n, mem);
                }
                off += size;
            }
        }
        fprintf(hdr, "region %lx %lx %lx %lx %lx %lx %s %lld\n", base, size, ab, mbi.State, mbi.Protect, mbi.Type, cls, at);
        nr++;
    }
    fclose(mem);
    DWORD dumpMs = GetTickCount() - t0;
    fprintf(hdr, "stats %d %lld %lld %lu\n", nr, off, unread, dumpMs);
    Log("dumped pid %lu: %d threads, %d regions, %lld MB stored, %lld unreadable pages, %lu ms\n", pid, g_nthr, nr, off >> 20, unread, dumpMs);
    // reference: the sim thread runs with the others suspended; watchdog 2 s
    for (int i = 0; i < g_nthr; i++)
        if (g_thr[i].tid == simTid && g_thr[i].suspended) { ResumeThread(g_thr[i].h); g_thr[i].suspended = false; }
    SetEvent(evD);
    int loose = 0;
    DWORD t1 = GetTickCount();
    if (WaitForSingleObject(evR, 2000) == WAIT_TIMEOUT) {
        loose = 1;
        Log("watchdog: reference still running after 2 s, resuming the other threads (image marked loose)\n");
        ResumeAll(0);
        if (WaitForSingleObject(evR, 120000) == WAIT_TIMEOUT) Log("reference did not finish in 120 s\n");
    }
    ResumeAll(0);
    Log("reference %lu ms, loose %d, stall %lu ms\n", GetTickCount() - t1, loose, GetTickCount() - t0);
    fprintf(hdr, "loose %d\n", loose);
    fclose(hdr);
    sprintf_s(path, "%s\\header.txt", dir);
    MoveFileExA(hdrPath, path, MOVEFILE_REPLACE_EXISTING);
    return 0;
}

// ---- fake target (selftest) ----
static DWORD g_tlsA = TLS_OUT_OF_INDEXES, g_tlsB = TLS_OUT_OF_INDEXES;  // two index globals, like TS3W's
static CRITICAL_SECTION g_cs;
static volatile DWORD* g_spin;
static HANDLE g_held;
static DWORD WINAPI Worker(void*) {
    EnterCriticalSection(&g_cs);  // owned by this thread at the cut
    TlsSetValue(g_tlsB, (void*)0x5555);
    SetEvent(g_held);
    for (;;) (*g_spin)++;
}
static int g_case;
static void TargetRef(void*, const char* dir) {
    char path[MAX_PATH + 32];
    sprintf_s(path, "%s\\image.ref", dir);
    FILE* f = NULL;
    fopen_s(&f, path, "w");
    DWORD a = *g_spin, t0 = GetTickCount();
    if (g_case == 0) {  // strict: the worker is suspended, the counter stands still
        Sleep(100);
        if (f) fprintf(f, "spin-stable %d\n", *g_spin == a);
    } else {  // blocks until the worker runs again, like a lock owned by a suspended thread
        while (*g_spin == a && GetTickCount() - t0 < 10000) Sleep(10);
        if (f) fprintf(f, "spin-changed %d after %lu ms\n", *g_spin != a, GetTickCount() - t0);
    }
    if (f) fclose(f);
}
static int Target(const char* dir, int cs) {
    g_case = cs;
    const DWORD size = 1 << 20;
    DWORD* pat = (DWORD*)VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (DWORD i = 0; i < size / 4; i++) pat[i] = i * 2654435761u;
    void* res = VirtualAlloc(NULL, 4 << 20, MEM_RESERVE, PAGE_NOACCESS);
    u_char* guard = (u_char*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD old;
    VirtualProtect(guard + 0x1000, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old);
    g_spin = (volatile DWORD*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_tlsA = TlsAlloc();
    g_tlsB = TlsAlloc();
    TlsSetValue(g_tlsA, (void*)0x1234ABCD);
    InitializeCriticalSection(&g_cs);
    g_held = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD wtid = 0;
    CreateThread(NULL, 0, Worker, NULL, 0, &wtid);
    WaitForSingleObject(g_held, 5000);
    char user[512], exe[MAX_PATH], err[256];
    sprintf_s(user, "pattern %p\npatternsize %lx\nreserve %p\nguard %p\nworker %lu\ncs %p\n", pat, size, res, guard + 0x1000, wtid, &g_cs);
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    DWORD tls[2] = {(DWORD)(uintptr_t)&g_tlsA, (DWORD)(uintptr_t)&g_tlsB}, locks[1] = {(DWORD)(uintptr_t)&g_cs};
    int rc = WiRequestCapture(exe, dir, (DWORD)(uintptr_t)GetModuleHandleA(NULL), tls, 2, locks, 1, user, TargetRef, NULL, err, sizeof(err));
    if (rc) printf("capture failed: %d %s\n", rc, err);
    return rc;
}

// ---- selftest: two captures of the fake target, then checks on the files ----
static int g_pass, g_fail;
static bool Check(const char* id, bool ok, const char* fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    _vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a);
    va_end(a);
    Log("%s %s %s\n", ok ? "PASS" : "FAIL", id, b);
    (ok ? g_pass : g_fail)++;
    return ok;
}
static int RunTarget(const char* dir, int cs) {
    char exe[MAX_PATH], cmd[MAX_PATH * 3];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    sprintf_s(cmd, "\"%s\" target \"%s\" %d", exe, dir, cs);
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return -1;
    if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 99);
    DWORD rc = 99;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)rc;
}
static int SelfTest(const char* root) {
    CreateDirectoryA(root, NULL);
    char dirs[2][MAX_PATH];
    for (int cs = 0; cs < 2; cs++) {
        sprintf_s(dirs[cs], "%s\\%s", root, cs ? "loose" : "strict");
        CreateDirectoryA(dirs[cs], NULL);
        int rc = RunTarget(dirs[cs], cs);
        Check(cs ? "WI-run-loose" : "WI-run-strict", rc == 0, "fake target %s capture exit %d", cs ? "watchdog" : "strict", rc);
    }
    static WiImage im;
    for (int cs = 0; cs < 2; cs++) {
        const char* d = dirs[cs];
        bool ok = WiLoad(d, &im);
        if (!Check(cs ? "WI-header-loose" : "WI-header", ok, "%s header.txt parses (%d regions, %d threads)", cs ? "loose" : "strict", im.nr, im.nt)) continue;
        Check(cs ? "WI-loose" : "WI-strict", im.loose == cs, "loose = %d (watchdog fired: %s)", im.loose, cs ? "expected" : "not expected");
        char ref[128] = "";
        char path[MAX_PATH + 32];
        sprintf_s(path, "%s\\image.ref", d);
        FILE* f = NULL;
        if (!fopen_s(&f, path, "r") && f) { fgets(ref, sizeof(ref), f); fclose(f); }
        ref[strcspn(ref, "\r\n")] = 0;
        Check(cs ? "WI-ref-loose" : "WI-ref-strict", cs ? strncmp(ref, "spin-changed 1", 14) == 0 : strcmp(ref, "spin-stable 1") == 0,
              "reference '%s' (%s)", ref, cs ? "ran on after the watchdog resumed the worker" : "other thread suspended during the reference");
        if (cs) { WiFree(&im); continue; }
        DWORD pat = WiUserHex(d, "pattern"), psz = WiUserHex(d, "patternsize"), res = WiUserHex(d, "reserve"), guard = WiUserHex(d, "guard"),
              csAddr = WiUserHex(d, "cs");
        char wv[32];
        WiUser(d, "worker", wv, sizeof(wv));
        DWORD worker = strtoul(wv, NULL, 10);
        const WiThread* sim = WiSim(&im);
        Check("WI-threads", im.nt >= 2 && sim && sim->esp >= sim->stackLimit && sim->esp < sim->stackBase,
              "%d threads; sim thread %lu ESP %08lX inside its stack %08lX-%08lX", im.nt, im.simTid, sim ? sim->esp : 0, sim ? sim->stackLimit : 0, sim ? sim->stackBase : 0);
        sprintf_s(path, "%s\\mem.bin", d);
        FILE* mem = NULL;
        fopen_s(&mem, path, "rb");
        DWORD* buf = (DWORD*)malloc(psz);
        bool same = mem && buf && WiRead(&im, mem, pat, buf, psz);
        for (DWORD i = 0; same && i < psz / 4; i++) same = buf[i] == i * 2654435761u;
        const WiRegion* pr = WiFind(&im, pat);
        Check("WI-private", same && pr && !strcmp(pr->cls, "priv"), "1 MB pattern region at %08lX stored (class %s), bytes equal", pat, pr ? pr->cls : "none");
        free(buf);
        const WiRegion* rr = WiFind(&im, res);
        Check("WI-reserve", rr && !strcmp(rr->cls, "reserve") && rr->off < 0 && rr->size == 4u << 20, "reserved-only 4 MB at %08lX: class %s, no bytes", res,
              rr ? rr->cls : "none");
        const WiRegion* gr = WiFind(&im, guard);
        Check("WI-guard", gr && !strcmp(gr->cls, "noread") && gr->off < 0, "guard page at %08lX: class %s (committed, not read)", guard, gr ? gr->cls : "none");
        bool tlsOk = im.ntls == 2 && im.tls[0].allocated && im.tls[0].value == 0x1234ABCD && im.tls[1].allocated && im.tls[1].value == 0;
        Check("WI-tls", tlsOk, "2 TLS keys allocated; sim values %08lX (want 1234ABCD), %08lX (worker-only key, want 0)", im.ntls ? im.tls[0].value : 0,
              im.ntls > 1 ? im.tls[1].value : 0);
        Check("WI-lock", im.nlk == 1 && im.lk[0].addr == csAddr && im.lk[0].owner == worker, "CS %08lX owner %lu = worker %lu", csAddr, im.nlk ? im.lk[0].owner : 0, worker);
        int exeRegions = 0, sysRegions = 0;
        bool tebStored = false;
        for (int i = 0; i < im.nr; i++) {
            exeRegions += !strcmp(im.r[i].cls, "exe");
            sysRegions += !strcmp(im.r[i].cls, "system");
            for (int t = 0; t < im.nt; t++) tebStored |= im.t[t].teb >= im.r[i].base && im.t[t].teb - im.r[i].base < im.r[i].size && im.r[i].off >= 0;
        }
        Check("WI-exe", exeRegions > 0 && im.exeSize > 0 && strstr(im.exe, "worldimage"), "%d exe regions of %s (size %lX)", exeRegions, im.exe, im.exeSize);
        Check("WI-system", sysRegions > 0 && !tebStored, "%d system regions (TEB/PEB), no TEB stored", sysRegions);
        if (mem) fclose(mem);
        WiFree(&im);
    }
    Log("SELFTEST %s: %d passed, %d failed\n", g_fail ? "FAILED" : "OK", g_pass, g_fail);
    return g_fail;
}

int main(int argc, char** argv) {
    if (argc >= 8 && !strcmp(argv[1], "capture")) return Capture(argc, argv);
    if (argc >= 4 && !strcmp(argv[1], "target")) return Target(argv[2], atoi(argv[3]));
    if (argc >= 3 && !strcmp(argv[1], "selftest")) {
        char path[MAX_PATH + 32];
        sprintf_s(path, "%s.log", argv[2]);
        CreateDirectoryA(argv[2], NULL);
        fopen_s(&g_log, path, "w");
        return SelfTest(argv[2]);
    }
    printf("usage: worldimage capture <pid> <simtid> <dir> <exebase> <dumped> <refdone> tls=.. locks=.. | selftest <out folder>\n");
    return 1;
}
