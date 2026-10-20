// ---- Mono M3.1 synthetic capture and M3.2 restore (docs/123 §2.5, §7, §8). Included by monohost.cpp (one TU). ----
// imagecap <dir> [helper]: after the M1.1 path (Mono up, ScriptCore loaded, Route method run) this process asks the
//   worldimage helper to capture itself (the same request ts3native [WorldImage] makes in the game). A second thread holds
//   the Mono loader lock at the cut, so the restore has to clear a lock owned by a thread that does not exist there (W1).
// image <dir>: the parent creates the child suspended, re-reserves and writes every stored region at its address (the
//   sim stack is moved when it collides with the child's own initial stack), resumes it; the child maps the clean
//   TS3W.exe, overwrites the writable sections (with the pointer revert pass) and the hybrid .text, installs W1-W3 in
//   TS3W's import table, rewrites the 7 TLS keys, pushes the restored stack as a GC root (collections off), and runs
//   checks on the restored world: statics equal to capture, a managed object on the restored heap, class and method
//   lookups through the restored loader, one managed call equal to the reference. Exit code = failed checks.
#include "../worldimage/wimage.h"
#include <intrin.h>

static const DWORD MH_LOADER_LOCK = 0x011F43A8, MH_GC_DONT_GC = 0x011F30A8, MH_PUSH_HOOK = 0x011949CC, MH_PUSH_STACK = 0x00E4CEE0;
static const DWORD MH_GC_NO = 0x011F2F90, MH_HEAPSIZE = 0x012225A0, MH_CORLIB = 0x01220E40, MH_DOMAIN_TLS = 0x011959DC;

// ======== capture (process A) ========
static HANDLE g_holdEv;
static DWORD WINAPI LoaderLockHolder(void*) {
    EnterCriticalSection((CRITICAL_SECTION*)(uintptr_t)MH_LOADER_LOCK);
    SetEvent(g_holdEv);
    Sleep(4000);  // released after the watchdog would fire, so a reference that needs the lock still finishes
    LeaveCriticalSection((CRITICAL_SECTION*)(uintptr_t)MH_LOADER_LOCK);
    Sleep(INFINITE);
    return 0;
}
static void* g_capMethod;
static DWORD InvokeRoute(void* method) {
    float a = 100.0f, b = 1.0f;
    void* args[2] = {&a, &b};
    void* exc = NULL;
    void* r = ((RuntimeInvoke)0xEA1090)(method, NULL, args, &exc);
    return r && !exc ? ((DWORD*)r)[1] : 0xFFFFFFFF;
}
static void CapRef(void*, const char* dir) {  // reference: the same call the restored child makes
    DWORD v = InvokeRoute(g_capMethod);
    char path[MAX_PATH + 32];
    sprintf_s(path, "%s\\image.ref", dir);
    FILE* f = NULL;
    if (!fopen_s(&f, path, "w") && f) { fprintf(f, "invoke %lu\n", v); fclose(f); }
}
static int ImageCapture(const char* dir, const char* helper) {
    void* scImg = Served("ScriptCore.dll");
    void* klass = Cls(scImg, "ScriptCore", "Route");
    g_capMethod = Meth(klass, "GetNumPointsForZeroToDiameterApprox", 2);
    if (!g_capMethod) { Log("imagecap: Route method not found\n"); return 70; }
    float a = 100.0f, b = 1.0f;
    void* args[2] = {&a, &b};
    void* exc = NULL;
    DWORD* box = (DWORD*)((RuntimeInvoke)0xEA1090)(g_capMethod, NULL, args, &exc);
    if (!box || exc) { Log("imagecap: invoke failed\n"); return 71; }
    // hold the loader lock from a second thread when 0x11F43A8 is the free CS that mono_loader_lock 0xE85020 enters
    const CRITICAL_SECTION* ll = (const CRITICAL_SECTION*)(uintptr_t)MH_LOADER_LOCK;
    bool isCs = ll->LockCount == -1 && !ll->OwningThread && !memcmp((u8*)(uintptr_t)0xE85020 + 1, &MH_LOADER_LOCK, 4);
    DWORD holder = 0;
    if (isCs) {
        g_holdEv = CreateEventA(NULL, TRUE, FALSE, NULL);
        CloseHandle(CreateThread(NULL, 0, LoaderLockHolder, NULL, 0, &holder));
        WaitForSingleObject(g_holdEv, 5000);
    } else Log("imagecap: 0x%08lX is not a free CS entered by 0xE85020 (LockCount %ld): no lock holder\n", MH_LOADER_LOCK, ll->LockCount);
    char user[1024];
    sprintf_s(user, "corlib %08lX\ngcno %08lX\nheapsize %08lX\ndomain %08lX\nscimg %p\nklass %p\nmethod %p\nbox %p\nboxvt %08lX\nboxval %08lX\nholder %lu\n",
              *(DWORD*)(uintptr_t)MH_CORLIB, *(DWORD*)(uintptr_t)MH_GC_NO, *(DWORD*)(uintptr_t)MH_HEAPSIZE,
              (DWORD)(uintptr_t)TlsGetValue(*(DWORD*)(uintptr_t)MH_DOMAIN_TLS), scImg, klass, g_capMethod, box, box[0], box[1], holder);
    char err[256];
    DWORD t0 = GetTickCount();
    int rc = WiRequestCapture(helper, dir, IMG_LO, kWiTs3wTls, 7, kWiTs3wLocks, 16, user, CapRef, NULL, err, sizeof(err));
    Log("imagecap: capture into %s -> %d %s (%lu ms; loader lock holder thread %lu)\n", dir, rc, err, GetTickCount() - t0, holder);
    return rc ? 72 : 0;
}

// ======== restore, parent side ========
struct WiRange { DWORD lo, hi, delta; };
static int ImageParent(const char* dir) {
    static WiImage im;
    if (!WiLoad(dir, &im)) { printf("image: %s\\header.txt missing, unfinished or malformed\n", dir); return 2; }
    const WiThread* sim = WiSim(&im);
    char path[MAX_PATH + 32];
    sprintf_s(path, "%s\\mem.bin", dir);
    HANDLE mem = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    sprintf_s(path, "%s\\restore.txt", dir);
    FILE* out = NULL;
    fopen_s(&out, path, "w");
    if (mem == INVALID_HANDLE_VALUE || !out) { printf("image: cannot open mem.bin or write restore.txt\n"); return 2; }
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    SetEnvironmentVariableA("MONOHOST_CHILD", "1");
    if (!CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) return 2;
    HANDLE c = pi.hProcess;
    if (VirtualAllocEx(c, (void*)(uintptr_t)IMG_LO, IMG_HI - IMG_LO, MEM_RESERVE, PAGE_NOACCESS) != (void*)(uintptr_t)IMG_LO) {
        printf("image: cannot reserve the exe range in the child\n");
        TerminateProcess(c, 2);
        return 2;
    }
    // what the child has at creation (ntdll, the exe, PEB/TEB, initial stack, shared pages): the only possible collisions
    static WiRange occ[4096];
    int nocc = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (DWORD va = 0x10000; va < 0xFFFE0000 && VirtualQueryEx(c, (void*)(uintptr_t)va, &mbi, sizeof(mbi)) && mbi.RegionSize;
         va = (DWORD)(uintptr_t)mbi.BaseAddress + (DWORD)mbi.RegionSize)
        if (mbi.State != MEM_FREE && nocc < 4096) occ[nocc++] = {(DWORD)(uintptr_t)mbi.BaseAddress, (DWORD)(uintptr_t)mbi.BaseAddress + (DWORD)mbi.RegionSize, 0};
    static u8 buf[1 << 20];
    int groups = 0, moved = 0, skipped = 0, failed = 0;
    long long bytes = 0;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < im.nr;) {
        int j = i + 1;
        while (j < im.nr && im.r[j].allocBase == im.r[i].allocBase) j++;
        const WiRegion& f = im.r[i];
        DWORD lo = f.allocBase, hi = im.r[j - 1].base + im.r[j - 1].size;
        bool restore = strcmp(f.cls, "exe") && strcmp(f.cls, "system") && !(lo < IMG_HI && hi > IMG_LO);
        if (!restore) { i = j; continue; }
        bool hit = false;
        for (int k = 0; k < nocc && !hit; k++) hit = occ[k].lo < hi && occ[k].hi > lo;
        DWORD delta = 0;
        if (hit && sim && sim->stackLimit >= lo && sim->stackLimit < hi) {  // the sim stack: data only, a copy elsewhere works
            DWORD nb = (DWORD)(uintptr_t)VirtualAllocEx(c, NULL, hi - lo, MEM_RESERVE, PAGE_NOACCESS);
            delta = nb - lo;
            fprintf(out, "reloc %lx %lx %lx sim-stack\n", lo, nb, hi - lo);
            moved++;
        } else if (hit || VirtualAllocEx(c, (void*)(uintptr_t)lo, hi - lo, MEM_RESERVE, PAGE_NOACCESS) != (void*)(uintptr_t)lo) {
            fprintf(out, "collision %lx %lx %s\n", lo, hi - lo, f.cls);
            printf("image: region %08lX-%08lX (%s) collides with the child, skipped\n", lo, hi, f.cls);
            skipped++;  // a reserve that fails without a visible owner: WOW64's own 64-bit allocations, not shown to a 32-bit query
            i = j;
            continue;
        }
        fprintf(out, "restored %lx %lx %lx\n", lo, hi, delta);
        groups++;
        for (int k = i; k < j; k++) {
            const WiRegion& r = im.r[k];
            if (r.state != MEM_COMMIT) continue;
            void* at = (void*)(uintptr_t)(r.base + delta);
            if (!VirtualAllocEx(c, at, r.size, MEM_COMMIT, PAGE_READWRITE)) { failed++; continue; }
            if (r.off >= 0) {
                LARGE_INTEGER o;
                o.QuadPart = r.off;
                SetFilePointerEx(mem, o, NULL, FILE_BEGIN);
                for (DWORD done = 0; done < r.size;) {
                    DWORD n = min((DWORD)sizeof(buf), r.size - done), got = 0;
                    if (!ReadFile(mem, buf, n, &got, NULL) || got != n || !WriteProcessMemory(c, (u8*)at + done, buf, n, NULL)) { failed++; break; }
                    done += n;
                }
                bytes += r.size;
            }
            DWORD old;
            if (r.protect && r.protect != PAGE_READWRITE) VirtualProtectEx(c, at, r.size, r.protect, &old);
        }
        i = j;
    }
    fclose(out);
    CloseHandle(mem);
    printf("image: %d allocations restored (%lld MB), %d moved, %d skipped (collision), %d failed, %lu ms\n", groups, bytes >> 20, moved, skipped, failed,
           GetTickCount() - t0);
    ResumeThread(pi.hThread);
    DWORD rc = 2;
    if (WaitForSingleObject(c, 300000) == WAIT_TIMEOUT) { printf("image: child timed out\n"); TerminateProcess(c, 4); }
    GetExitCodeProcess(c, &rc);
    return (int)rc + (failed ? 100 : 0);
}

// ======== restore, child side ========
static WiImage g_im;
static WiRange g_rest[8192];
static int g_nrest;
static DWORD g_simDelta;
static bool Restored(const void* p) {
    DWORD a = (DWORD)(uintptr_t)p;
    for (int i = 0; i < g_nrest; i++) if (a >= g_rest[i].lo + g_rest[i].delta && a < g_rest[i].hi + g_rest[i].delta) return true;
    return false;
}
static DWORD RestoredEnd(const void* p) {
    DWORD a = (DWORD)(uintptr_t)p;
    for (int i = 0; i < g_nrest; i++) if (a >= g_rest[i].lo + g_rest[i].delta && a < g_rest[i].hi + g_rest[i].delta) return g_rest[i].hi + g_rest[i].delta;
    return a;
}

// captured modules the child does not have at the same base with the same size: pointers into them are reverted
static int g_moved[256], g_nmoved;
static bool SameInChild(const WiModule& m) {
    const char* base = strrchr(m.path, '\\') ? strrchr(m.path, '\\') + 1 : m.path;
    HMODULE h = GetModuleHandleA(base);
    if ((DWORD)(uintptr_t)h != m.base) return false;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((u8*)h + ((IMAGE_DOS_HEADER*)h)->e_lfanew);
    return nt->OptionalHeader.SizeOfImage == m.size;
}
static bool IntoMoved(DWORD p) {
    for (int i = 0; i < g_nmoved; i++) if (p - g_im.m[g_moved[i]].base < g_im.m[g_moved[i]].size) return true;
    return false;
}
// a detour target outside TS3W that no same-base module of the child holds
static bool Foreign(DWORD t) {
    if (t >= IMG_LO && t < IMG_HI) return false;
    for (int i = 0; i < g_im.nm; i++)
        if (t - g_im.m[i].base < g_im.m[i].size) return IntoMoved(t);
    return true;  // into no captured module: allocated code (trampolines) of some mod
}

// Section pass (docs/123 §7.4): writable sections = dumped bytes with the pointer revert; executable = hybrid .text.
static int g_textRuns, g_textReverted, g_dataReverted;
static bool RestoreSections(FILE* mem) {
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((u8*)(uintptr_t)IMG_LO + ((IMAGE_DOS_HEADER*)(uintptr_t)IMG_LO)->e_lfanew);
    IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        DWORD va = IMG_LO + s->VirtualAddress, n = (s->Misc.VirtualSize + 0xFFF) & ~0xFFFu;
        bool exec = (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0, wr = (s->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        if (!exec && !wr) { Log("  section %.8s %08lX: clean (rebuilt)\n", s->Name, va); continue; }
        u8* d = (u8*)malloc(n);
        const u8* clean = (const u8*)(uintptr_t)va;
        if (!d || !WiRead(&g_im, mem, va, d, n)) { Log("  section %.8s %08lX: dumped bytes missing\n", s->Name, va); free(d); return false; }
        int diffs = 0;
        if (exec) {
            for (DWORD k = 0; k < n;) {
                if (d[k] == clean[k]) { k++; continue; }
                DWORD start = k, last = k;
                for (DWORD m = k + 1; m < n && m - last <= 8; m++) if (d[m] != clean[m]) last = m;
                g_textRuns++;
                DWORD tgt = 0;
                bool detour = false;
                for (DWORD m = start >= 4 ? start - 4 : 0; m <= last && m + 5 <= n && !detour; m++) {
                    if ((d[m] == 0xE8 || d[m] == 0xE9) && m + 4 >= start) { tgt = va + m + 5 + *(DWORD*)(d + m + 1); detour = Foreign(tgt); }
                    else if (d[m] == 0x68 && m + 5 < n && d[m + 5] == 0xC3) { tgt = *(DWORD*)(d + m + 1); detour = Foreign(tgt); }
                }
                if (detour) {
                    memcpy(d + start, clean + start, last - start + 1);
                    if (++g_textReverted <= 20) Log("  .text revert %08lX-%08lX (detour to %08lX)\n", va + start, va + last, tgt);
                }
                k = last + 1;
            }
            diffs = g_textRuns;
        } else {
            for (DWORD k = 0; k + 4 <= n; k += 4) {
                DWORD dv = *(DWORD*)(d + k), cv = *(DWORD*)(clean + k);
                if (dv == cv) continue;
                diffs++;
                // CRITICAL_SECTION.SpinCount (0x02000000 dynamic-spin flag | count, LockCount 0x10 before it) is no pointer,
                // even when it falls inside a moved module (synthetic run: 0x020007D0 inside d3dx9_31 at 0x1FC0000)
                const LONG lockCount = k >= 0x10 ? *(LONG*)(d + k - 0x10) : 1;
                if ((dv & 0xFFFF0000) == 0x02000000 && lockCount <= 0 && lockCount > -0x10000) continue;
                if (IntoMoved(dv)) {
                    *(DWORD*)(d + k) = cv;
                    if (++g_dataReverted <= 20) Log("  .data revert [%08lX] %08lX -> %08lX (pointer into a module the child does not map)\n", va + k, dv, cv);
                }
            }
        }
        DWORD old;
        VirtualProtect((void*)(uintptr_t)va, n, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void*)(uintptr_t)va, d, n);
        VirtualProtect((void*)(uintptr_t)va, n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)(uintptr_t)va, n);
        free(d);
        Log("  section %.8s %08lX +%lX: %s, %d %s differ from clean\n", s->Name, va, n, exec ? "hybrid" : "dumped", diffs, exec ? "runs" : "dwords");
    }
    return true;
}

// ---- W1-W3 (docs/123 §7.3): wrappers in TS3W's import table ----
static void** IatSlot(const char* dll, const char* fn) {
    u8* b = (u8*)(uintptr_t)IMG_LO;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR* id = (IMAGE_IMPORT_DESCRIPTOR*)(b + dd.VirtualAddress); id->Name; id++) {
        if (_stricmp((const char*)(b + id->Name), dll)) continue;
        DWORD* names = (DWORD*)(b + (id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk));
        for (int i = 0; names[i]; i++)
            if (!(names[i] & IMAGE_ORDINAL_FLAG32) && !strcmp((const char*)(b + names[i] + 2), fn)) return (void**)(b + id->FirstThunk) + i;
    }
    return NULL;
}
static int g_iatHooked, g_iatMissing;
template <class F> static void Hook(const char* dll, const char* fn, F w, F* orig) {
    void** slot = IatSlot(dll, fn);
    if (!slot) { g_iatMissing++; return; }
    *orig = (F)*slot;
    DWORD old;
    VirtualProtect(slot, 4, PAGE_READWRITE, &old);
    *slot = (void*)w;
    VirtualProtect(slot, 4, old, &old);
    g_iatHooked++;
}

// W1: first touch of a restored CRITICAL_SECTION
static void* g_csSeen[16384];
static int g_w1Touched, g_w1Rewritten, g_w1Reset;
static bool CsMark(void* cs) {  // true on the first sighting
    unsigned h = ((DWORD)(uintptr_t)cs >> 3) & 16383;
    while (g_csSeen[h] && g_csSeen[h] != cs) h = (h + 1) & 16383;
    if (g_csSeen[h]) return false;
    g_csSeen[h] = cs;
    return true;
}
static void CsTouch(CRITICAL_SECTION* cs, DWORD ret) {
    // restored heap or TS3W's dumped .data (the Mono lock globals live there)
    const bool inExe = (DWORD)(uintptr_t)cs >= IMG_LO && (DWORD)(uintptr_t)cs < IMG_HI;
    if (!(inExe || Restored(cs)) || !CsMark(cs)) return;
    g_w1Touched++;
    DWORD owner = (DWORD)(uintptr_t)cs->OwningThread;
    if (owner && owner == g_im.simTid) {
        cs->OwningThread = (HANDLE)(uintptr_t)GetCurrentThreadId();
        cs->LockCount = -2;  // held, no waiters (the waiters were game threads)
        g_w1Rewritten++;
        Log("W1: lock %p held by the sim thread at the cut -> this thread (recursion %ld)\n", cs, cs->RecursionCount);
    } else if (owner) {
        Log("W1: lock %p owned by game thread %lu at the cut: reset (caller %08lX)\n", cs, owner, ret);
        cs->LockCount = -1;
        cs->RecursionCount = 0;
        cs->OwningThread = NULL;
        if (ret >= 0x4E16F0 && ret < 0x4E1800) *(DWORD*)((u8*)cs + 0x20) = 0;  // EA Lock: its recursion count at +0x20
        g_w1Reset++;
    }
    cs->LockSemaphore = NULL;                            // the game's event handle is dead here
    cs->DebugInfo = (PRTL_CRITICAL_SECTION_DEBUG)(LONG_PTR)-1;  // "no debug info": never walk the game's debug list
}
static void(WINAPI* oEnterCS)(LPCRITICAL_SECTION);
static BOOL(WINAPI* oTryEnterCS)(LPCRITICAL_SECTION);
static void(WINAPI* oLeaveCS)(LPCRITICAL_SECTION);
static void(WINAPI* oDeleteCS)(LPCRITICAL_SECTION);
static void(WINAPI* oInitCS)(LPCRITICAL_SECTION);
static BOOL(WINAPI* oInitCSSpin)(LPCRITICAL_SECTION, DWORD);
static void WINAPI WEnterCS(LPCRITICAL_SECTION cs) { CsTouch(cs, (DWORD)(uintptr_t)_ReturnAddress()); oEnterCS(cs); }
static BOOL WINAPI WTryEnterCS(LPCRITICAL_SECTION cs) { CsTouch(cs, (DWORD)(uintptr_t)_ReturnAddress()); return oTryEnterCS(cs); }
static void WINAPI WLeaveCS(LPCRITICAL_SECTION cs) { CsTouch(cs, (DWORD)(uintptr_t)_ReturnAddress()); oLeaveCS(cs); }
static void WINAPI WDeleteCS(LPCRITICAL_SECTION cs) {
    if (!Restored(cs) && !((DWORD)(uintptr_t)cs >= IMG_LO && (DWORD)(uintptr_t)cs < IMG_HI && CsMark(cs))) oDeleteCS(cs);
}
static void WINAPI WInitCS(LPCRITICAL_SECTION cs) { CsMark(cs); oInitCS(cs); }  // a fresh CS in restored memory is the child's own
static BOOL WINAPI WInitCSSpin(LPCRITICAL_SECTION cs, DWORD n) { CsMark(cs); return oInitCSSpin(cs, n); }

// W2: CRT frees of restored blocks
static int g_w2Frees, g_w2Reallocs;
static void(__cdecl* oFree)(void*);
static void*(__cdecl* oRealloc)(void*, size_t);
static void(__cdecl* oAlignedFree)(void*);
static void*(__cdecl* g_crtMalloc)(size_t);
static void __cdecl WFree(void* p) { if (p && Restored(p)) { g_w2Frees++; return; } oFree(p); }
static void __cdecl WAlignedFree(void* p) { if (p && Restored(p)) { g_w2Frees++; return; } oAlignedFree(p); }
static void* __cdecl WRealloc(void* p, size_t n) {
    if (!p || !Restored(p)) return oRealloc(p, n);
    g_w2Reallocs++;
    void* q = g_crtMalloc(n);
    if (q) memcpy(q, p, min(n, (size_t)(RestoredEnd(p) - (DWORD)(uintptr_t)p)));
    return q;
}
// the same rule for monohost's own allocator redirects (eglib thunks and GeneralAllocator methods of the M1.1 patch list)
static void __cdecl WGFree(void* p) { if (p && Restored(p)) { g_w2Frees++; return; } free(p); }
static void* __cdecl WGRealloc(void* p, size_t n) {
    if (!p || !Restored(p)) return realloc(p, n);
    g_w2Reallocs++;
    void* q = malloc(n ? n : 1);
    if (q) memcpy(q, p, min(n, (size_t)(RestoredEnd(p) - (DWORD)(uintptr_t)p)));
    return q;
}
static void __fastcall WEaFree(void*, void*, void* p) { WGFree(p); }
static void* __fastcall WEaRealloc(void*, void*, void* p, size_t n, int) { return WGRealloc(p, n); }

// W3: kernel handles the child did not create fail loud
static HANDLE g_handles[4096];
static int g_nhandles, g_w3Checked;
static HANDLE Own(HANDLE h) {
    if (h && h != INVALID_HANDLE_VALUE && g_nhandles < 4096) g_handles[g_nhandles++] = h;
    return h;
}
static void W3(HANDLE h, const char* fn, DWORD ret) {
    g_w3Checked++;
    if (!h || (DWORD)(uintptr_t)h >= 0xFFFFFFF0) return;  // NULL, pseudo handles
    for (int i = 0; i < g_nhandles; i++) if (g_handles[i] == h) return;
    Log("W3 FAIL: %s on handle %p that the child did not create (a game handle from the image), caller %08lX\n", fn, h, ret);
    TerminateProcess(GetCurrentProcess(), 93);
}
#define RA ((DWORD)(uintptr_t)_ReturnAddress())
static DWORD(WINAPI* oWait)(HANDLE, DWORD);
static DWORD(WINAPI* oWaitEx)(HANDLE, DWORD, BOOL);
static DWORD(WINAPI* oWaitMulti)(DWORD, const HANDLE*, BOOL, DWORD);
static BOOL(WINAPI* oSetEvent)(HANDLE);
static BOOL(WINAPI* oResetEvent)(HANDLE);
static BOOL(WINAPI* oReleaseMutex)(HANDLE);
static BOOL(WINAPI* oReleaseSem)(HANDLE, LONG, LPLONG);
static BOOL(WINAPI* oCloseHandle)(HANDLE);
static BOOL(WINAPI* oReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL(WINAPI* oWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static HANDLE(WINAPI* oCreateEventA)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
static HANDLE(WINAPI* oCreateEventW)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
static HANDLE(WINAPI* oCreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
static HANDLE(WINAPI* oCreateMutexW)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
static HANDLE(WINAPI* oCreateSemA)(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR);
static HANDLE(WINAPI* oCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE(WINAPI* oCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE(WINAPI* oCreateThread)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
static DWORD WINAPI WWait(HANDLE h, DWORD ms) { W3(h, "WaitForSingleObject", RA); return oWait(h, ms); }
static DWORD WINAPI WWaitEx(HANDLE h, DWORD ms, BOOL al) { W3(h, "WaitForSingleObjectEx", RA); return oWaitEx(h, ms, al); }
static DWORD WINAPI WWaitMulti(DWORD n, const HANDLE* hs, BOOL all, DWORD ms) {
    for (DWORD i = 0; i < n; i++) W3(hs[i], "WaitForMultipleObjects", RA);
    return oWaitMulti(n, hs, all, ms);
}
static BOOL WINAPI WSetEvent(HANDLE h) { W3(h, "SetEvent", RA); return oSetEvent(h); }
static BOOL WINAPI WResetEvent(HANDLE h) { W3(h, "ResetEvent", RA); return oResetEvent(h); }
static BOOL WINAPI WReleaseMutex(HANDLE h) { W3(h, "ReleaseMutex", RA); return oReleaseMutex(h); }
static BOOL WINAPI WReleaseSem(HANDLE h, LONG n, LPLONG p) { W3(h, "ReleaseSemaphore", RA); return oReleaseSem(h, n, p); }
static BOOL WINAPI WCloseHandle(HANDLE h) { W3(h, "CloseHandle", RA); return oCloseHandle(h); }
static BOOL WINAPI WReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD r, LPOVERLAPPED o) { W3(h, "ReadFile", RA); return oReadFile(h, b, n, r, o); }
static BOOL WINAPI WWriteFile(HANDLE h, LPCVOID b, DWORD n, LPDWORD r, LPOVERLAPPED o) { W3(h, "WriteFile", RA); return oWriteFile(h, b, n, r, o); }
static HANDLE WINAPI WCreateEventA(LPSECURITY_ATTRIBUTES s, BOOL m, BOOL i, LPCSTR n) { return Own(oCreateEventA(s, m, i, n)); }
static HANDLE WINAPI WCreateEventW(LPSECURITY_ATTRIBUTES s, BOOL m, BOOL i, LPCWSTR n) { return Own(oCreateEventW(s, m, i, n)); }
static HANDLE WINAPI WCreateMutexA(LPSECURITY_ATTRIBUTES s, BOOL o, LPCSTR n) { return Own(oCreateMutexA(s, o, n)); }
static HANDLE WINAPI WCreateMutexW(LPSECURITY_ATTRIBUTES s, BOOL o, LPCWSTR n) { return Own(oCreateMutexW(s, o, n)); }
static HANDLE WINAPI WCreateSemA(LPSECURITY_ATTRIBUTES s, LONG i, LONG m, LPCSTR n) { return Own(oCreateSemA(s, i, m, n)); }
static HANDLE WINAPI WCreateFileA(LPCSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t) { return Own(oCreateFileA(f, a, s, sa, c, fl, t)); }
static HANDLE WINAPI WCreateFileW(LPCWSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t) { return Own(oCreateFileW(f, a, s, sa, c, fl, t)); }
static HANDLE WINAPI WCreateThread(LPSECURITY_ATTRIBUTES s, SIZE_T n, LPTHREAD_START_ROUTINE f, LPVOID p, DWORD fl, LPDWORD id) { return Own(oCreateThread(s, n, f, p, fl, id)); }

static void InstallWrappers() {
    const char* k = "KERNEL32.dll";
    Hook(k, "EnterCriticalSection", WEnterCS, &oEnterCS);
    Hook(k, "TryEnterCriticalSection", WTryEnterCS, &oTryEnterCS);
    Hook(k, "LeaveCriticalSection", WLeaveCS, &oLeaveCS);
    Hook(k, "DeleteCriticalSection", WDeleteCS, &oDeleteCS);
    Hook(k, "InitializeCriticalSection", WInitCS, &oInitCS);
    Hook(k, "InitializeCriticalSectionAndSpinCount", WInitCSSpin, &oInitCSSpin);
    void** m = IatSlot("MSVCR80.dll", "malloc");
    g_crtMalloc = m ? (void*(__cdecl*)(size_t))*m : malloc;
    Hook("MSVCR80.dll", "free", WFree, &oFree);
    Hook("MSVCR80.dll", "realloc", WRealloc, &oRealloc);
    Hook("MSVCR80.dll", "_aligned_free", WAlignedFree, &oAlignedFree);
    Hook(k, "WaitForSingleObject", WWait, &oWait);
    Hook(k, "WaitForSingleObjectEx", WWaitEx, &oWaitEx);
    Hook(k, "WaitForMultipleObjects", WWaitMulti, &oWaitMulti);
    Hook(k, "SetEvent", WSetEvent, &oSetEvent);
    Hook(k, "ResetEvent", WResetEvent, &oResetEvent);
    Hook(k, "ReleaseMutex", WReleaseMutex, &oReleaseMutex);
    Hook(k, "ReleaseSemaphore", WReleaseSem, &oReleaseSem);
    Hook(k, "CloseHandle", WCloseHandle, &oCloseHandle);
    Hook(k, "ReadFile", WReadFile, &oReadFile);
    Hook(k, "WriteFile", WWriteFile, &oWriteFile);
    Hook(k, "CreateEventA", WCreateEventA, &oCreateEventA);
    Hook(k, "CreateEventW", WCreateEventW, &oCreateEventW);
    Hook(k, "CreateMutexA", WCreateMutexA, &oCreateMutexA);
    Hook(k, "CreateMutexW", WCreateMutexW, &oCreateMutexW);
    Hook(k, "CreateSemaphoreA", WCreateSemA, &oCreateSemA);
    Hook(k, "CreateFileA", WCreateFileA, &oCreateFileA);
    Hook(k, "CreateFileW", WCreateFileW, &oCreateFileW);
    Hook(k, "CreateThread", WCreateThread, &oCreateThread);
}

// GC: the restored sim stack (moved copy if it collided) is pushed as a root after the collector's own stack
typedef void(__cdecl* PushStackFn)(void* lo, void* hi);
static void(__cdecl* g_wiPushOrig)();
static DWORD g_wiStackLo, g_wiStackHi;
static void __cdecl WiPushRoots() {
    g_wiPushOrig();
    if (g_wiStackLo) ((PushStackFn)(uintptr_t)MH_PUSH_STACK)((void*)(uintptr_t)g_wiStackLo, (void*)(uintptr_t)g_wiStackHi);
}

// Child watchdog (docs/123 §5 risk 2): a wait on something W1/W3 missed ends the run in 60 s with the main thread's
// EIP, the exe return addresses on its stack and the stack words that point into TS3W .data or restored memory (lock candidates).
static HANDLE g_mainThread;
static DWORD WINAPI ChildWatchdog(void*) {
    Sleep(60000);
    SuspendThread(g_mainThread);
    CONTEXT c = {};
    c.ContextFlags = CONTEXT_FULL;
    GetThreadContext(g_mainThread, &c);
    Log("WATCHDOG: main thread stuck after 60 s at EIP %08lX ESP %08lX\n  stack:", c.Eip, c.Esp);
    for (int i = 0, n = 0; i < 4096 && n < 32; i++) {
        DWORD v = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)(uintptr_t)(c.Esp + i * 4), &v, 4, NULL)) break;
        bool code = v > IMG_LO + 0x1000 && v < 0x00F95000, data = (v >= 0x01146000 && v < 0x01235000) || Restored((void*)(uintptr_t)v);
        if (code || data) { Log(" %s%08lX", code ? "ret:" : "obj:", v); n++; }
    }
    Log("\n");
    TerminateProcess(GetCurrentProcess(), 94);
    return 0;
}

// G3-1 on a game image (docs/123 §4.2, §8.6): the managed statics ts3native read on the sim thread at the cut (user.txt
// g31.*) read again in the child through the restored loader and domain, the MT index at [host+0x15F0] (I-123-23), one
// managed call (SimClock.get_CurrentTicks) on the restored interpreter, and EA's class lookup on the restored host. Pure
// reads first; the call and the host lookup last, so a fault or hang there (60 s watchdog) still leaves the earlier lines.
static int GameStaticsG31(const char* dir) {
    char v[64], key[64];
    if (!WiUser(dir, "g31.simtickcnt", v, sizeof(v))) {
        Log("image: no G3-1 values in user.txt (ts3native without docs/123 §8.6): restore checks only\n");
        return g_fail;
    }
    for (int i = 0; i < kWiNStatics; i++) {
        const WiStatic& s = kWiStatics[i];
        sprintf_s(key, "g31.%s", s.key);
        const bool had = WiUser(dir, key, v, sizeof(v)) && strcmp(v, "none");
        const unsigned long long want = had ? _strtoui64(v, NULL, 16) : 0;
        sprintf_s(key, "g31k.%s", s.key);
        void* k = (void*)(uintptr_t)WiUserHex(dir, key);
        unsigned long long got = 0;
        const bool ok = WiReadStatic(k, s, &got);
        sprintf_s(key, "G3-1-%s", s.key);
        Check(key, had && ok && got == want, "%s.%s%s%s = %llu in the child (%s), %llu at the cut (%s), class %p", s.cls, s.field, s.inner ? "." : "",
              s.inner ? s.inner : "", got, ok ? "read" : "not read", want, had ? "read" : "not read", k);
    }
    const DWORD host = *(DWORD*)(uintptr_t)kWiHostPtr;
    DWORD mt = 0xFFFFFFFF;
    __try { if (host) mt = *(DWORD*)(uintptr_t)(host + 0x15F0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    const DWORD mtCut = WiUser(dir, "mtindex", v, sizeof(v)) ? strtoul(v, NULL, 10) : 0xFFFFFFFE;
    Check("G3-1-mtindex", mt == mtCut, "MT index [host %08lX +0x15F0] = %lu in the child, %lu at the cut", host, mt, mtCut);
    void* sc = (void*)(uintptr_t)WiUserHex(dir, "g31k.simtickcnt");
    void* m = Meth(sc, "get_CurrentTicks", 0);
    void* exc = NULL;
    DWORD code = 0;
    void* r = m ? SafeInvoke(m, NULL, NULL, &exc, &code) : NULL;
    const unsigned long long ticks = r && !exc ? *(unsigned long long*)((u8*)r + 4) : 0;  // boxed long: no sync word
    const bool hadSum = WiUser(dir, "g31.currentticks", v, sizeof(v));
    const unsigned long long want = hadSum ? _strtoui64(v, NULL, 16) : 0;
    Check("G3-1-invoke", hadSum && r && !exc && ticks == want, "SimClock.CurrentTicks on the restored interpreter = %llu, cut %llu (method %p, exception %p, fault %08lX)",
          ticks, want, m, exc, code);
    void* fc = WiFindClass(kWiStatics[0].ns, kWiStatics[0].cls);
    Check("G3-1-findclass", fc && fc == sc, "ScriptHost_FindClass(%s.%s) through the restored host = %p, cut %p", kWiStatics[0].ns, kWiStatics[0].cls, fc, sc);
    Log("image: W2 %d frees / %d reallocs of restored blocks absorbed, W3 %d handle checks, %d handles created\n", g_w2Frees, g_w2Reallocs, g_w3Checked, g_nhandles);
    Log("IMAGE %s: %d passed, %d failed (G3-1 game image)\n", g_fail ? "FAILED" : "OK", g_pass, g_fail);
    return g_fail;
}

static int ImageChild(const wchar_t* exe, const char* dir) {
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    CloseHandle(CreateThread(NULL, 0, ChildWatchdog, NULL, 0, NULL));
    if (!WiLoad(dir, &g_im)) { Log("image: header missing\n"); return 80; }
    const WiThread* sim = WiSim(&g_im);
    char path[MAX_PATH + 32], line[256];
    sprintf_s(path, "%s\\restore.txt", dir);
    FILE* rf = NULL;
    int collisions = 0;
    if (!fopen_s(&rf, path, "r") && rf) {
        while (fgets(line, sizeof(line), rf)) {
            DWORD a = 0, b = 0, c = 0;
            if (sscanf_s(line, "restored %lx %lx %lx", &a, &b, &c) == 3 && g_nrest < 8192) g_rest[g_nrest++] = {a, b, c};
            if (sim && sscanf_s(line, "reloc %lx %lx", &a, &b) == 2 && sim->stackLimit >= a) g_simDelta = b - a;
            collisions += !strncmp(line, "collision", 9);
        }
        fclose(rf);
    }
    Log("image %s: pid %lu sim thread %lu, %d regions, %d threads, %d modules, loose %d; child: %d allocations restored, %d collisions, sim stack %s\n", dir, g_im.pid,
        g_im.simTid, g_im.nr, g_im.nt, g_im.nm, g_im.loose, g_nrest, collisions, g_simDelta ? "moved" : "in place");
    // map the clean exe as M1.1 does, then the section pass
    ACTCTXW ac = {sizeof(ac)};
    ac.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    ac.lpSource = exe;
    ac.lpResourceName = MAKEINTRESOURCEW(1);
    HANDLE actx = CreateActCtxW(&ac);
    ULONG_PTR cookie = 0;
    if (actx != INVALID_HANDLE_VALUE) ActivateActCtx(actx, &cookie);
    VirtualFree((void*)(uintptr_t)IMG_LO, 0, MEM_RELEASE);
    HMODULE h = LoadLibraryExW(exe, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h || !CheckImage(h) || !ResolveImports(h)) { Log("image: TS3W.exe map failed\n"); return 81; }
    for (int i = 0; i < g_im.nm; i++)
        if (g_im.m[i].base != IMG_LO && !SameInChild(g_im.m[i]) && g_nmoved < 256) g_moved[g_nmoved++] = i;
    Log("image: %d of %d captured modules are not mapped at the same base in the child (pointers into them revert)\n", g_nmoved, g_im.nm);
    sprintf_s(path, "%s\\mem.bin", dir);
    FILE* mem = NULL;
    if (fopen_s(&mem, path, "rb") || !mem || !RestoreSections(mem)) { Log("image: section pass failed\n"); return 82; }
    Check("M3-text", true, "hybrid .text: %d differing runs, %d detours reverted; .data: %d pointers reverted", g_textRuns, g_textReverted, g_dataReverted);
    InstallWrappers();
    Log("image: W1-W3 wrappers in %d TS3W import slots (%d names not imported)\n", g_iatHooked, g_iatMissing);
    static const struct { DWORD at; const void* to; } patches[] = {
        {0xD70A10, (const void*)GMalloc}, {0xD70A30, (const void*)GMalloc0}, {0xD70A60, (const void*)WGFree}, {0xD70A80, (const void*)GStrdup},
        {0xD71860, (const void*)WGRealloc}, {0x4E69D0, (const void*)EaAlloc}, {0x4E5920, (const void*)WEaFree}, {0x4E6A70, (const void*)WEaRealloc},
        {0xD6FCB0, (const void*)LoadImageByName}, {0xD6FC40, (const void*)ConfigPath},
    };
    // a synthetic (monohost) image allocated through these redirects; a game image keeps EA's allocator and its restored cores
    char v[64];
    const bool synthetic = WiUser(dir, "method", v, sizeof(v));
    for (auto& p : patches)
        if (synthetic && !Jmp(p.at, p.to)) { Log("image: patch 0x%08lX failed\n", p.at); return 83; }
    // TLS: new indices in the child, the sim thread's values on this thread (docs/123 §7.2 rule)
    int keys = 0;
    for (int i = 0; i < g_im.ntls; i++) {
        const WiTls& t = g_im.tls[i];
        if (!t.allocated) { Log("  tls [%08lX] = %lX was not allocated at the cut: left\n", t.global, t.index); continue; }
        DWORD idx = TlsAlloc();
        *(DWORD*)(uintptr_t)t.global = idx;
        TlsSetValue(idx, (void*)(uintptr_t)t.value);
        keys++;
        Log("  tls [%08lX] %lX -> %lX, value %08lX\n", t.global, t.index, idx, t.value);
    }
    // GC: collections off for this one-shot run (M4 turns them on); the restored stack becomes a root
    (*(volatile LONG*)(uintptr_t)MH_GC_DONT_GC)++;
    if (sim) { g_wiStackLo = sim->esp + g_simDelta; g_wiStackHi = sim->stackBase + g_simDelta; }
    g_wiPushOrig = *(void(__cdecl**)())(uintptr_t)MH_PUSH_HOOK;
    *(void**)(uintptr_t)MH_PUSH_HOOK = (void*)WiPushRoots;

    // ---- checks on the restored world (G3-1 style, synthetic image) ----
    Check("M3-tls", keys == g_im.ntls || keys >= 4, "%d of %d TLS keys rewritten", keys, g_im.ntls);
    u8 a[256] = {}, b2[256] = {};
    bool stackOk = sim && WiRead(&g_im, mem, sim->esp, a, 256) && ReadProcessMemory(GetCurrentProcess(), (void*)(uintptr_t)g_wiStackLo, b2, 256, NULL) && !memcmp(a, b2, 256);
    Check("M3-stack", stackOk && *(void**)(uintptr_t)MH_PUSH_HOOK == (void*)WiPushRoots, "sim stack %08lX-%08lX restored%s, pushed from GC hook [0x%08lX]", g_wiStackLo,
          g_wiStackHi, g_simDelta ? " (moved copy)" : "", MH_PUSH_HOOK);
    fclose(mem);
    if (!synthetic) return GameStaticsG31(dir);
    Check("M3-statics", *(DWORD*)(uintptr_t)MH_CORLIB == WiUserHex(dir, "corlib") && *(DWORD*)(uintptr_t)MH_GC_NO == WiUserHex(dir, "gcno") &&
                            *(DWORD*)(uintptr_t)MH_HEAPSIZE == WiUserHex(dir, "heapsize"),
          "mono_defaults.corlib %08lX, GC_gc_no %lu, GC_heapsize %lu equal to capture", *(DWORD*)(uintptr_t)MH_CORLIB, *(DWORD*)(uintptr_t)MH_GC_NO,
          *(DWORD*)(uintptr_t)MH_HEAPSIZE);
    DWORD dom = (DWORD)(uintptr_t)TlsGetValue(*(DWORD*)(uintptr_t)MH_DOMAIN_TLS);
    Check("M3-domain", dom && dom == WiUserHex(dir, "domain"), "current domain through the rewritten TLS key %08lX (capture %08lX)", dom, WiUserHex(dir, "domain"));
    const DWORD* box = (const DWORD*)(uintptr_t)WiUserHex(dir, "box");
    Check("M3-heap-object", box && Restored(box) && box[0] == WiUserHex(dir, "boxvt") && box[1] == 7920, "boxed result of the capture-side call at %p: vtable %08lX, value %lu",
          box, box ? box[0] : 0, box ? box[1] : 0);
    void* scImg = (void*)(uintptr_t)WiUserHex(dir, "scimg");
    void* klass = Cls(scImg, "ScriptCore", "Route");
    void* method = Meth(klass, "GetNumPointsForZeroToDiameterApprox", 2);
    Check("M3-lookup", klass == (void*)(uintptr_t)WiUserHex(dir, "klass") && method == (void*)(uintptr_t)WiUserHex(dir, "method"),
          "class and method lookup through the restored loader: %p %p (loader lock: W1 touched %d, reset %d, rewritten %d)", klass, method, g_w1Touched, g_w1Reset,
          g_w1Rewritten);
    if (WiUserHex(dir, "holder")) Check("M3-W1", g_w1Reset >= 1, "the loader lock a vanished thread held at the cut was reset (%d resets)", g_w1Reset);
    DWORD got = method ? InvokeRoute(method) : 0;
    char ref[64] = "";
    sprintf_s(path, "%s\\image.ref", dir);
    FILE* f = NULL;
    if (!fopen_s(&f, path, "r") && f) { fgets(ref, sizeof(ref), f); fclose(f); }
    DWORD want = 0;
    sscanf_s(ref, "invoke %lu", &want);
    Check("M3-invoke", got == 7920 && got == want, "Route.GetNumPointsForZeroToDiameterApprox(100, 1) on the restored heap = %lu, reference %lu", got, want);
    Log("image: W2 %d frees / %d reallocs of restored blocks absorbed, W3 %d handle checks, %d handles created\n", g_w2Frees, g_w2Reallocs, g_w3Checked, g_nhandles);
    Log("IMAGE %s: %d passed, %d failed\n", g_fail ? "FAILED" : "OK", g_pass, g_fail);
    return g_fail;
}
