// World image (docs/123 §2.5, §7, §8): on-disk format, parser and the in-process capture request.
// Shared by native/worldimage (helper + fake-process selftest), ts3native [WorldImage] and native/monohost (synthetic
// capture `imagecap` and restore `image`). Header only; include after <windows.h> and <stdio.h>.
//
// An image is a folder:
//   header.txt  one record per line (below), written by the helper; the last line `loose 0|1` is added after the reference
//   mem.bin     the bytes of every stored region, back to back in table order (64-bit offsets: game images are ~2 GB)
//   user.txt    `key value` lines from the requester (what it wants compared after a restore)
//   image.ref   the requester's reference result, written by its callback while the other threads stay suspended
// header.txt records (numbers hex without 0x unless noted):
//   worldimage 1
//   target <pid dec> <simtid dec> <exebase> <exesize> <path>
//   thread <tid dec> <teb> <stackbase> <stacklimit> <eip> <esp> <ebp> <eax> <ebx> <ecx> <edx> <esi> <edi> <eflags>
//   tls <global> <index> <allocated 0|1> <sim thread value>
//   lock <addr> <lockcount> <recursion> <owner tid dec>
//   module <base> <size> <stamp> <path>
//   region <base> <size> <allocbase> <state> <protect> <type> <class> <fileoff dec, -1 = no bytes>
//   stats <regions dec> <stored bytes dec> <unreadable pages dec> <suspend+dump ms dec>
//   loose <0|1>
// Region classes [design, docs/123 §7.1]: exe = the exe-of-interest image (R1-R3: stored, the restore picks per section),
// priv = private committed (R6-R11, R13: stored), noread = private committed but unreadable (guard pages: committed
// zero on restore), reserve = private reserved-only (R14: re-reserved), system = TEB/PEB/shared pages (R12: not stored,
// listed so a restore can name collisions). Other MEM_IMAGE and MEM_MAPPED regions (R4, R5) are not listed.
#pragma once
#include <stdlib.h>
#include <string.h>

// TS3W.exe Steam 1.67 (docs/123 §7.2): the 7 TlsAlloc index globals; the Mono critical sections of §7.3 (I-123-17 logs
// their owners at the cut) plus Boehm GC_write_cs and EA's global CS.
static const DWORD kWiTs3wTls[7] = {0x0114C908, 0x011F4160, 0x011959DC, 0x01220F38, 0x011F43E0, 0x011F43C4, 0x01195B7C};
static const DWORD kWiTs3wLocks[16] = {0x011F43A8, 0x011F4130, 0x011F4174, 0x011F41F8, 0x011F4290, 0x011F4380, 0x011F43E4, 0x011F4520,
                                       0x011F4574, 0x011F45A0, 0x011F4698, 0x011F476C, 0x011F4788, 0x011F47B0, 0x01222580, 0x011CE588};

struct WiRegion { DWORD base, size, allocBase, state, protect, type; char cls[8]; long long off; };
struct WiThread { DWORD tid, teb, stackBase, stackLimit, eip, esp, ebp, eax, ebx, ecx, edx, esi, edi, eflags; };
struct WiTls { DWORD global, index, allocated, value; };
struct WiLock { DWORD addr, lockCount, recursion, owner; };
struct WiModule { DWORD base, size, stamp; char path[MAX_PATH]; };
struct WiImage {
    DWORD pid, simTid, exeBase, exeSize;
    char exe[MAX_PATH], dir[MAX_PATH];
    int loose, statsRegions;
    long long storedBytes;
    WiRegion* r; int nr, cap;
    WiThread t[512]; int nt;
    WiTls tls[16]; int ntls;
    WiLock lk[32]; int nlk;
    WiModule m[256]; int nm;
};

static inline void WiFree(WiImage* im) { free(im->r); im->r = NULL; im->nr = im->cap = 0; }

// Parses <dir>\header.txt. false when missing, malformed or without the final `loose` line (capture not finished).
static inline bool WiLoad(const char* dir, WiImage* im) {
    memset(im, 0, sizeof(*im));
    im->loose = -1;
    strcpy_s(im->dir, dir);
    char path[MAX_PATH + 32], line[1024];
    sprintf_s(path, "%s\\header.txt", dir);
    FILE* f = NULL;
    if (fopen_s(&f, path, "r") || !f) return false;
    bool magic = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char kind[16] = "";
        int pos = 0;
        if (sscanf_s(line, "%15s %n", kind, (unsigned)sizeof(kind), &pos) < 1) continue;
        const char* a = line + pos;
        if (!strcmp(kind, "worldimage")) magic = atoi(a) == 1;
        else if (!strcmp(kind, "target")) {
            int p2 = 0;
            sscanf_s(a, "%lu %lu %lx %lx %n", &im->pid, &im->simTid, &im->exeBase, &im->exeSize, &p2);
            strcpy_s(im->exe, a + p2);
        } else if (!strcmp(kind, "thread") && im->nt < 512) {
            WiThread& t = im->t[im->nt];
            if (sscanf_s(a, "%lu %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx", &t.tid, &t.teb, &t.stackBase, &t.stackLimit, &t.eip, &t.esp,
                         &t.ebp, &t.eax, &t.ebx, &t.ecx, &t.edx, &t.esi, &t.edi, &t.eflags) == 14) im->nt++;
        } else if (!strcmp(kind, "tls") && im->ntls < 16) {
            WiTls& t = im->tls[im->ntls];
            if (sscanf_s(a, "%lx %lx %lu %lx", &t.global, &t.index, &t.allocated, &t.value) == 4) im->ntls++;
        } else if (!strcmp(kind, "lock") && im->nlk < 32) {
            WiLock& k = im->lk[im->nlk];
            if (sscanf_s(a, "%lx %lx %lx %lu", &k.addr, &k.lockCount, &k.recursion, &k.owner) == 4) im->nlk++;
        } else if (!strcmp(kind, "module") && im->nm < 256) {
            WiModule& m = im->m[im->nm];
            int p2 = 0;
            if (sscanf_s(a, "%lx %lx %lx %n", &m.base, &m.size, &m.stamp, &p2) >= 3) { strcpy_s(m.path, a + p2); im->nm++; }
        } else if (!strcmp(kind, "region")) {
            if (im->nr == im->cap) {
                im->cap = im->cap ? im->cap * 2 : 1024;
                WiRegion* n = (WiRegion*)realloc(im->r, im->cap * sizeof(WiRegion));
                if (!n) { fclose(f); return false; }
                im->r = n;
            }
            WiRegion& r = im->r[im->nr];
            if (sscanf_s(a, "%lx %lx %lx %lx %lx %lx %7s %lld", &r.base, &r.size, &r.allocBase, &r.state, &r.protect, &r.type, r.cls,
                         (unsigned)sizeof(r.cls), &r.off) == 8) im->nr++;
        } else if (!strcmp(kind, "stats")) sscanf_s(a, "%d %lld", &im->statsRegions, &im->storedBytes);
        else if (!strcmp(kind, "loose")) im->loose = atoi(a);
    }
    fclose(f);
    return magic && im->nr > 0 && im->loose >= 0;
}

static inline const WiThread* WiSim(const WiImage* im) {
    for (int i = 0; i < im->nt; i++) if (im->t[i].tid == im->simTid) return &im->t[i];
    return NULL;
}
static inline const WiRegion* WiFind(const WiImage* im, DWORD va) {  // regions are written in address order
    int lo = 0, hi = im->nr - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const WiRegion& r = im->r[mid];
        if (va < r.base) hi = mid - 1;
        else if (va - r.base >= r.size) lo = mid + 1;
        else return &r;
    }
    return NULL;
}
// Stored bytes of [va, va+n) (may span regions); false if any byte is not stored.
static inline bool WiRead(const WiImage* im, FILE* mem, DWORD va, void* out, DWORD n) {
    while (n) {
        const WiRegion* r = WiFind(im, va);
        if (!r || r->off < 0) return false;
        DWORD take = min(n, r->base + r->size - va);
        if (_fseeki64(mem, r->off + (va - r->base), SEEK_SET) || fread(out, 1, take, mem) != take) return false;
        va += take; out = (char*)out + take; n -= take;
    }
    return true;
}
// user.txt value for key (first match), "" if none
static inline bool WiUser(const char* dir, const char* key, char* out, int n) {
    char path[MAX_PATH + 32], line[1024];
    sprintf_s(path, "%s\\user.txt", dir);
    out[0] = 0;
    FILE* f = NULL;
    if (fopen_s(&f, path, "r") || !f) return false;
    size_t kl = strlen(key);
    bool found = false;
    while (!found && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, key, kl) && line[kl] == ' ') { strcpy_s(out, n, line + kl + 1); found = true; }
    }
    fclose(f);
    return found;
}
static inline DWORD WiUserHex(const char* dir, const char* key) {
    char v[64];
    return WiUser(dir, key, v, sizeof(v)) ? strtoul(v, NULL, 16) : 0;
}

// ---- G3-1 managed statics (docs/123 §4.2, §8.6): ts3native reads them on the sim thread at the cut into user.txt as
// `g31.<key> <hex>` and `g31k.<key> <MonoClass* hex>`; monohost image reads them again in the restored child.
// Mono 1.2.3 in TS3W Steam 1.67 [verified: disassembly of 0xE6F720, 0xEA1350, 0xEA2AD0, 0xE9DD40]: MonoClassField stride
// 0x1C, type +0 (attrs byte +4, 0x10 = static), offset +8, name +0xC; class->runtime_info +0xB8 = {u16 max_domain id;
// MonoVTable* per domain id from +4}, domain id = MonoDomain +0x58; static data = vtable +0xC; objects have no sync word,
// so instance offsets count the 4-byte vtable pointer. Pure reads: no vtable creation, no class init, no allocation.
struct WiStatic { const char* key; const char* ns; const char* cls; const char* field; const char* inner; int size; };
static const WiStatic kWiStatics[] = {
    {"simtickcnt", "Sims3.Gameplay.Utilities", "SimClock", "sSimTickCnt", NULL, 8},
    {"starttime", "Sims3.Gameplay.Utilities", "SimClock", "sStartTime", NULL, 8},  // DateAndTime { long Ticks }
    {"ticksadv", "Sims3.Gameplay.Utilities", "SimClock", "sTicksAdvanced", NULL, 8},
    {"lots", "Sims3.Gameplay.Core", "LotManager", "sLots", "used_slots", 4},                // Dictionary<ulong, Lot>.Count (game corlib)
    {"metaobjects", "Sims3.Gameplay.Autonomy", "Autonomy", "sPublicMetaObjects", "_size", 4},  // List<IMetaObject>.Count
    {"autlevel", "Sims3.Gameplay.Autonomy", "AutonomyRestrictions", "sLevel", NULL, 4},
};
static const int kWiNStatics = sizeof(kWiStatics) / sizeof(kWiStatics[0]);
typedef void* (__cdecl* WiFieldFromName)(void* klass, const char* name);         // mono_class_get_field_from_name 0xE6F720
typedef void* (__cdecl* WiDomainGet)();                                          // mono_domain_get 0xE75260 (TLS)
typedef void* (__fastcall* WiFindClassFn)(void* host, void* edx, const char* ns, const char* name);  // ScriptHost_FindClass 0xD79CA0
static const DWORD kWiHostPtr = 0x011EE514;                                      // MonoScriptHost*

// EA's class lookup (cache map at host+0xB0C, then the loaded images); NULL on a miss or a fault
static inline void* WiFindClass(const char* ns, const char* name) {
    __try {
        void* host = *(void**)(uintptr_t)kWiHostPtr;
        return host ? ((WiFindClassFn)0xD79CA0)(host, NULL, ns, name) : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}
// value of one static (or of one int/long field of the object it holds); false when a step is missing or faults
static inline bool WiReadStatic(void* klass, const WiStatic& s, unsigned long long* out) {
    *out = 0;
    __try {
        unsigned char* f = klass ? (unsigned char*)((WiFieldFromName)0xE6F720)(klass, s.field) : NULL;
        unsigned char* dom = (unsigned char*)((WiDomainGet)0xE75260)();
        if (!f || !dom || !(*(unsigned char*)(*(DWORD*)f + 4) & 0x10) || *(int*)(f + 8) < 0) return false;  // not static, or thread-static
        const unsigned char* ri = *(unsigned char**)((unsigned char*)klass + 0xB8);
        const DWORD id = *(DWORD*)(dom + 0x58);
        if (!ri || id > *(const unsigned short*)ri) return false;
        const unsigned char* vt = *(unsigned char* const*)(ri + 4 + id * 4);
        unsigned char* p = vt ? *(unsigned char* const*)(vt + 0xC) : NULL;
        if (!p) return false;
        p += *(int*)(f + 8);
        if (s.inner) {
            unsigned char* obj = *(unsigned char**)p;
            if (!obj) return false;
            void* k = **(void***)obj;  // obj->vtable->klass
            unsigned char* g = (unsigned char*)((WiFieldFromName)0xE6F720)(k, s.inner);
            if (!g || *(int*)(g + 8) <= 0) return false;
            p = obj + *(int*)(g + 8);
        }
        memcpy(out, p, s.size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- capture request, run on the thread whose state is the cut (the sim thread in the game) ----
// The helper suspends every thread of this process (this one too, while it waits), writes the image, resumes only this
// thread and signals `dumped`; this thread runs ref(ctx, dir) while the others stay suspended, then signals `refdone`.
// If ref takes more than 2 s (a lock owned by a suspended thread) the helper resumes the others and marks the image loose.
// Returns 0 on success, else an error code with text in err.
typedef void (*WiRefFn)(void* ctx, const char* dir);
static inline int WiRequestCapture(const char* helper, const char* dir, DWORD exeBase, const DWORD* tls, int ntls, const DWORD* locks, int nlocks,
                            const char* user, WiRefFn ref, void* ctx, char* err, int errLen) {
    static LONG seq;
    err[0] = 0;
    CreateDirectoryA(dir, NULL);
    char path[MAX_PATH + 32];
    sprintf_s(path, "%s\\user.txt", dir);
    FILE* f = NULL;
    if (fopen_s(&f, path, "w") || !f) { sprintf_s(err, errLen, "cannot write %s", path); return 1; }
    fputs(user ? user : "", f);
    fclose(f);
    char evD[64], evR[64];
    LONG n = InterlockedIncrement(&seq);
    sprintf_s(evD, "Local\\wimage-%lu-%ld-dumped", GetCurrentProcessId(), n);
    sprintf_s(evR, "Local\\wimage-%lu-%ld-refdone", GetCurrentProcessId(), n);
    HANDLE hD = CreateEventA(NULL, TRUE, FALSE, evD), hR = CreateEventA(NULL, TRUE, FALSE, evR);
    if (!hD || !hR) { sprintf_s(err, errLen, "CreateEvent err %lu", GetLastError()); return 2; }
    static char cmd[4096];  // static: the sim thread's stack stays small and nothing else runs this
    int c = sprintf_s(cmd, "\"%s\" capture %lu %lu \"%s\" %lx %s %s", helper, GetCurrentProcessId(), GetCurrentThreadId(), dir, exeBase, evD, evR);
    c += sprintf_s(cmd + c, sizeof(cmd) - c, " tls=");
    for (int i = 0; i < ntls; i++) c += sprintf_s(cmd + c, sizeof(cmd) - c, "%s%lx", i ? "," : "", tls[i]);
    c += sprintf_s(cmd + c, sizeof(cmd) - c, " locks=");
    for (int i = 0; i < nlocks; i++) c += sprintf_s(cmd + c, sizeof(cmd) - c, "%s%lx", i ? "," : "", locks[i]);
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        sprintf_s(err, errLen, "cannot start helper %s (err %lu)", helper, GetLastError());
        CloseHandle(hD); CloseHandle(hR);
        return 3;
    }
    HANDLE w[2] = {hD, pi.hProcess};
    DWORD r = WaitForMultipleObjects(2, w, FALSE, 600000);  // fail-safe; a 2 GB dump takes seconds
    int rc = 0;
    if (r == WAIT_OBJECT_0) {
        if (ref) ref(ctx, dir);
        SetEvent(hR);
        if (WaitForSingleObject(pi.hProcess, 600000) == WAIT_TIMEOUT) { sprintf_s(err, errLen, "helper did not finish"); rc = 4; }
    } else {
        sprintf_s(err, errLen, "helper ended before the dump (wait %lu)", r);
        rc = 5;
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (!rc && code) { sprintf_s(err, errLen, "helper exit code %lu", code); rc = 6; }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hD); CloseHandle(hR);
    return rc;
}
