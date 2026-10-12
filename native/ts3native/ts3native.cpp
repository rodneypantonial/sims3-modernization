// ts3native.asi: native patches for TS3W.exe (Steam 1.67, SHA-256 10b2423e..., base 0x400000, no ASLR).
// Loaded by Ultimate ASI Loader. Every patch site is checked against the exact original bytes first;
// a mismatch skips that patch and logs it. See docs/15-native-patches.md.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define TS3NATIVE_VERSION "0.1.0"

typedef unsigned char u8;

static HANDLE g_log = INVALID_HANDLE_VALUE;

static void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf_s(buf, sizeof(buf) - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)strlen(buf);
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD w;
    if (g_log != INVALID_HANDLE_VALUE) WriteFile(g_log, buf, (DWORD)n, &w, NULL);
}

static void Hex(const u8* p, size_t n, char* out, size_t cap) {
    out[0] = 0;
    for (size_t i = 0; i < n && (i * 3 + 3) < cap; i++) sprintf_s(out + i * 3, cap - i * 3, "%02X ", p[i]);
}

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (const u8*)p + n <= (const u8*)mbi.BaseAddress + mbi.RegionSize;
}

static bool Matches(const void* addr, const u8* expect, size_t n) {
    return Readable(addr, n) && memcmp(addr, expect, n) == 0;
}

static bool Write(void* addr, const void* bytes, size_t n) {
    DWORD old;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(addr, bytes, n);
    VirtualProtect(addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
    return true;
}

struct Site {
    DWORD addr;      // VA of the instruction
    u8 expect[8];    // original instruction bytes
    size_t len;
    size_t immOff;   // offset of the 32-bit immediate inside the instruction
};

// Verify every site first, then write the new immediate into all of them (all or nothing).
static bool PatchImm32(const char* name, const Site* sites, int count, DWORD value) {
    char hex[64];
    for (int i = 0; i < count; i++) {
        const u8* p = (const u8*)(uintptr_t)sites[i].addr;
        if (!Matches(p, sites[i].expect, sites[i].len)) {
            if (Readable(p, sites[i].len)) Hex(p, sites[i].len, hex, sizeof(hex)); else strcpy_s(hex, "<unreadable>");
            Log("SKIP %s: bytes at 0x%08X differ (found %s), nothing written", name, sites[i].addr, hex);
            return false;
        }
    }
    for (int i = 0; i < count; i++) {
        u8* imm = (u8*)(uintptr_t)sites[i].addr + sites[i].immOff;
        DWORD oldv;
        memcpy(&oldv, imm, 4);
        if (!Write(imm, &value, 4)) {
            Log("FAIL %s: VirtualProtect at 0x%08X failed (err %lu)", name, sites[i].addr, GetLastError());
            return false;  // shortcut: earlier sites of this group stay patched; they are independent immediates
        }
        Log("OK   %s: 0x%08X imm 0x%X -> 0x%X", name, sites[i].addr, oldv, value);
    }
    return true;
}

// Phase 2 helper (unused so far): 5-byte jmp detour at `target`. `prologue` is the verified original
// code, `len` >= 5 and ending on an instruction boundary, with no relative operands (it is copied
// verbatim into the trampoline). Returns the trampoline that runs the original function, or NULL.
void* InstallDetour(const char* name, DWORD target, const u8* prologue, size_t len, void* hook) {
    u8* t = (u8*)(uintptr_t)target;
    if (len < 5 || len > 16 || !Matches(t, prologue, len)) {
        Log("SKIP detour %s at 0x%08X: prologue differs", name, target);
        return NULL;
    }
    u8* tramp = (u8*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;
    memcpy(tramp, prologue, len);
    tramp[len] = 0xE9;
    *(LONG*)(tramp + len + 1) = (LONG)((t + len) - (tramp + len + 5));
    u8 jmp[16];
    memset(jmp, 0x90, sizeof(jmp));
    jmp[0] = 0xE9;
    *(LONG*)(jmp + 1) = (LONG)((u8*)hook - (t + 5));
    if (!Write(t, jmp, len)) { VirtualFree(tramp, 0, MEM_RELEASE); return NULL; }
    Log("OK   detour %s at 0x%08X", name, target);
    return tramp;
}

// --- Phase 1: Boehm GC (docs/13-gc-tuning.md) ---
static const DWORD GC_IS_INITIALIZED = 0x011F30D0;  // set at the end of GC_init (0xE4E160)
static const DWORD BLOCKS_PER_MB = 256;              // GC heap blocks are 4 KB

static const Site kInitialHeap[] = {
    {0x00E4E168, {0xBD, 0x00, 0x28, 0x00, 0x00}, 5, 1},  // GC_init: mov ebp,0x2800 (40 MB default)
};
static const Site kGrowthStep[] = {
    {0x00E4A1D8, {0x3D, 0x00, 0x08, 0x00, 0x00, 0x76, 0x2C}, 7, 1},  // GC_collect_or_expand: cmp eax,0x800 ; jbe
    {0x00E4A1FF, {0x3D, 0x00, 0x08, 0x00, 0x00, 0x77, 0x05}, 7, 1},  // cmp eax,0x800 ; ja
    {0x00E4A206, {0xB8, 0x00, 0x08, 0x00, 0x00, 0x50}, 6, 1},        // mov eax,0x800 ; push eax
};
static const Site kBudgetCeiling[] = {
    {0x01192324, {0xDC, 0x05, 0x00, 0x00}, 4, 0},  // .data dword 1500, per-frame GC budget ceiling
};

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void ApplyGc(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"GC", L"Enabled", 1, ini)) { Log("GC patches disabled (Enabled=0)"); return; }
    int heapMB = GetPrivateProfileIntW(L"GC", L"InitialHeapMB", 256, ini);
    int stepMB = GetPrivateProfileIntW(L"GC", L"GrowthStepMB", 32, ini);
    int ceiling = GetPrivateProfileIntW(L"GC", L"BudgetCeiling", 0, ini);
    // Limits: an initial heap the 32-bit process cannot reserve in one block makes GC_init exit(1).
    int heapC = Clamp(heapMB, 40, 1024), stepC = Clamp(stepMB, 8, 256);
    if (heapC != heapMB || stepC != stepMB) Log("clamped InitialHeapMB %d->%d, GrowthStepMB %d->%d", heapMB, heapC, stepMB, stepC);
    Log("settings: InitialHeapMB=%d GrowthStepMB=%d BudgetCeiling=%d", heapC, stepC, ceiling);

    char env[32];
    if (GetEnvironmentVariableA("GC_INITIAL_HEAP_SIZE", env, sizeof(env)))
        Log("note: GC_INITIAL_HEAP_SIZE=%s is set and overrides InitialHeapMB", env);

    const DWORD* inited = (const DWORD*)(uintptr_t)GC_IS_INITIALIZED;
    if (!Readable(inited, 4)) { Log("SKIP GC: 0x%08X unreadable, not the expected exe", GC_IS_INITIALIZED); return; }
    if (*inited) Log("SKIP initial heap: GC_init already ran (GC_is_initialized=1), plugin loaded too late");
    else if (heapC != 40) PatchImm32("GC initial heap", kInitialHeap, 1, (DWORD)heapC * BLOCKS_PER_MB);

    if (stepC != 8) PatchImm32("GC growth step", kGrowthStep, 3, (DWORD)stepC * BLOCKS_PER_MB);

    if (ceiling == 0) Log("budget ceiling unchanged (BudgetCeiling=0)");
    else PatchImm32("GC budget ceiling", kBudgetCeiling, 1, (DWORD)Clamp(ceiling, 500, 20000));
}

static void Run(HMODULE self) {
    wchar_t path[MAX_PATH], ini[MAX_PATH], log[MAX_PATH], exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wchar_t* dot = wcsrchr(path, L'.');
    if (dot) *dot = 0;
    swprintf_s(ini, L"%s.ini", path);
    swprintf_s(log, L"%s.log", path);
    g_log = CreateFileW(log, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    SYSTEMTIME st;
    GetLocalTime(&st);
    Log("ts3native " TS3NATIVE_VERSION " %04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    HMODULE base = GetModuleHandleW(NULL);
    Log("host %ls base 0x%p", exe, base);
    const wchar_t* name = wcsrchr(exe, L'\\');
    if (base != (HMODULE)0x400000 || _wcsicmp(name ? name + 1 : exe, L"TS3W.exe") != 0) {
        Log("SKIP all: host is not TS3W.exe at 0x400000");
    } else {
        ApplyGc(ini);
    }
    Log("done");
    if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
}

#ifndef TS3NATIVE_NO_DLLMAIN
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // Patch now: the ASI loader runs before the exe's entry point, so before GC_init (lazy, first GC use).
        Run(inst);
    }
    return TRUE;
}
#endif
