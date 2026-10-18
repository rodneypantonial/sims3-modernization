// [ProbeSnap] (docs/94 §7, 0.23.0, counters v43), observe only: runtime targets of the indirect call/jmp sites in the routing
// closure (Route_PlanLocked, Route_PlanCommon, the route ICs, the writer leaves, portal and lock-key ICs; site list generated
// by tools/snap-vcall-sites.py into vcallsites.h). Static analysis cannot see these targets (vtables of EP/mod objects,
// per-search callbacks, function pointers); this sampler records them, on every thread. Included by ts3native.cpp.
//
// Mechanism: int3 (0xCC) on the first byte of each site; a vectored handler (first in the list) decodes the site's own
// instruction from the table (FF /2 call or FF /4 jmp, r/m32 near forms), computes the target from the CONTEXT, records
// (site, target) with a count, and emulates the instruction (push the return address for a call, Eip = target), so the
// original byte is never needed to run the code. Sampling bounds the cost: a site disarms itself (original byte back) after
// PerWindow hits; every WindowSec all sites are re-armed and the pairs whose count changed are logged
// ("vcall SITE -> TARGET n=N [out=module]"). Nothing is armed until DelaySec after start-up, so S3SS and the other
// start-up patchers see the original bytes. Before each arming the site's bytes are compared with the table: a site another
// hook has rewritten is dropped for good (vcSkipped). A target that cannot be read (bad pointer, the game's own crash
// path) disarms the site for good and lets the original instruction run (vcFaults). Up to 8 targets per site; more are
// counted in vcOverflow and logged per site.
struct VcSiteDef { DWORD addr; u8 len; u8 bytes[7]; };
#include "vcallsites.h"

static const int VC_PAIRS = 8;
struct VcPair { volatile LONG tgt, n; LONG logged; };
struct VcSite {
    DWORD addr;
    u8 len, call, mod, base, index, scale, orig, dead;  // base/index 8 = none
    LONG disp;
    const u8* bytes;
    volatile LONG hits, armed, over;
    LONG overLogged;
    VcPair p[VC_PAIRS];
};
static VcSite* g_vc;
static int g_vcN;
static LONG g_vcPer = 16;
static DWORD g_vcImgLo, g_vcImgHi;
static SRWLOCK g_vcPoke = SRWLOCK_INIT;
static PVOID g_vcVeh;

// FF /2 (call) or FF /4 (jmp) r/m32, no prefix; false for anything else or a length that differs from the table's
static bool VcDecode(const u8* b, u8 len, VcSite& s) {
    if (len < 2 || b[0] != 0xFF) return false;
    const u8 reg = b[1] >> 3 & 7, rm = b[1] & 7;
    if (reg != 2 && reg != 4) return false;
    s.call = reg == 2;
    s.mod = b[1] >> 6;
    s.base = rm;
    s.index = 8;
    s.scale = 0;
    s.disp = 0;
    u8 n = 2;
    if (s.mod != 3) {
        if (rm == 4) {
            if (len < 3) return false;
            const u8 sib = b[2];
            n = 3;
            s.scale = sib >> 6;
            s.index = (sib >> 3 & 7) == 4 ? 8 : (sib >> 3 & 7);
            s.base = sib & 7;
            if (s.mod == 0 && s.base == 5) {
                if (len < 7) return false;
                s.base = 8;
                s.disp = *(const LONG*)(b + 3);
                n = 7;
            }
        } else if (s.mod == 0 && rm == 5) {
            return false;  // [disp32]: import thunk, not in the table
        }
        if (s.mod == 1) { if (len < n + 1) return false; s.disp = (signed char)b[n]; n += 1; }
        else if (s.mod == 2) { if (len < n + 4) return false; s.disp = *(const LONG*)(b + n); n += 4; }
    }
    return n == len;
}

static DWORD VcReg(const CONTEXT& c, int r) {
    switch (r) {
        case 0: return c.Eax; case 1: return c.Ecx; case 2: return c.Edx; case 3: return c.Ebx;
        case 4: return c.Esp; case 5: return c.Ebp; case 6: return c.Esi; default: return c.Edi;
    }
}

// the address the instruction would transfer to; false when its memory operand is unreadable
static bool VcTarget(const VcSite& s, const CONTEXT& c, DWORD* t) {
    if (s.mod == 3) { *t = VcReg(c, s.base); return true; }
    const DWORD ea = (DWORD)s.disp + (s.base < 8 ? VcReg(c, s.base) : 0) + (s.index < 8 ? VcReg(c, s.index) << s.scale : 0);
    __try { *t = *(volatile const DWORD*)(uintptr_t)ea; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

static VcSite* VcFind(DWORD a) {
    int lo = 0, hi = g_vcN - 1;
    while (lo <= hi) {
        const int m = (lo + hi) / 2;
        if (g_vc[m].addr == a) return &g_vc[m];
        if (g_vc[m].addr < a) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

// caller holds g_vcPoke
static void VcPokeLocked(VcSite& s, bool arm) {
    if ((s.armed != 0) == arm) return;
    const u8 v = arm ? 0xCC : s.orig;
    if (!Write((void*)(uintptr_t)s.addr, &v, 1)) return;
    s.armed = arm;
    InterlockedExchangeAdd(&ts3native_counters.vcArmed, arm ? 1 : -1);
}

static void VcRecord(VcSite& s, DWORD t) {
    Ts3nCounters& k = ts3native_counters;
    for (int i = 0; i < VC_PAIRS; i++) {
        LONG v = s.p[i].tgt;
        if (!v) {
            v = InterlockedCompareExchange(&s.p[i].tgt, (LONG)t, 0);
            if (!v) {
                v = (LONG)t;
                InterlockedIncrement(&k.vcPairs);
                if (t < g_vcImgLo || t >= g_vcImgHi) InterlockedIncrement(&k.vcOutside);
            }
        }
        if ((DWORD)v == t) { InterlockedIncrement(&s.p[i].n); return; }
    }
    InterlockedIncrement(&s.over);
    InterlockedIncrement(&k.vcOverflow);
}

static LONG CALLBACK VcVeh(EXCEPTION_POINTERS* e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_BREAKPOINT && code != 0x4000001F) return EXCEPTION_CONTINUE_SEARCH;  // STATUS_WX86_BREAKPOINT
    const DWORD at = (DWORD)(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    VcSite* s = VcFind(at);
    if (!s) return EXCEPTION_CONTINUE_SEARCH;
    Ts3nCounters& k = ts3native_counters;
    CONTEXT* c = e->ContextRecord;
    InterlockedIncrement(&k.vcHits);
    DWORD t = 0;
    if (!VcTarget(*s, *c, &t) || !t) {  // the original instruction faults by itself: put it back and let it run
        InterlockedIncrement(&k.vcFaults);
        AcquireSRWLockExclusive(&g_vcPoke);
        s->dead = 1;
        VcPokeLocked(*s, false);
        ReleaseSRWLockExclusive(&g_vcPoke);
        c->Eip = at;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    VcRecord(*s, t);
    if (InterlockedIncrement(&s->hits) >= g_vcPer && s->armed && TryAcquireSRWLockExclusive(&g_vcPoke)) {
        VcPokeLocked(*s, false);  // busy lock: the next hit tries again (never block in here)
        ReleaseSRWLockExclusive(&g_vcPoke);
    }
    if (s->call) {
        c->Esp -= 4;
        *(DWORD*)(uintptr_t)c->Esp = at + s->len;
    }
    c->Eip = t;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// builds the run-time table (defs sorted by address); returns the sites kept
static int VcSetup(const VcSiteDef* defs, int n) {
    Ts3nCounters& k = ts3native_counters;
    g_vc = (VcSite*)calloc((size_t)n, sizeof(VcSite));
    if (!g_vc) return 0;
    g_vcN = 0;
    for (int i = 0; i < n; i++) {
        VcSite& s = g_vc[g_vcN];
        if (!VcDecode(defs[i].bytes, defs[i].len, s) || (g_vcN && g_vc[g_vcN - 1].addr >= defs[i].addr)) {
            k.vcSkipped++;
            memset(&s, 0, sizeof(s));
            continue;
        }
        s.addr = defs[i].addr;
        s.len = defs[i].len;
        s.orig = defs[i].bytes[0];
        s.bytes = defs[i].bytes;
        g_vcN++;
    }
    const u8* img = (const u8*)GetModuleHandleW(NULL);
    g_vcImgLo = (DWORD)(uintptr_t)img;
    g_vcImgHi = g_vcImgLo + ((const IMAGE_NT_HEADERS*)(img + ((const IMAGE_DOS_HEADER*)img)->e_lfanew))->OptionalHeader.SizeOfImage;
    k.vcSites = g_vcN;
    return g_vcN;
}

// one window start: hit budgets reset, every live site armed again (bytes checked first)
static void VcArmAll() {
    Ts3nCounters& k = ts3native_counters;
    AcquireSRWLockExclusive(&g_vcPoke);
    for (int i = 0; i < g_vcN; i++) {
        VcSite& s = g_vc[i];
        if (s.dead || s.armed) continue;
        if (!Matches((const void*)(uintptr_t)s.addr, s.bytes, s.len)) {  // another hook rewrote it: never touch it again
            s.dead = 1;
            k.vcSkipped++;
            Log("vcall skip 0x%08lX: bytes differ from the table", s.addr);
            continue;
        }
        s.hits = 0;
        VcPokeLocked(s, true);
    }
    ReleaseSRWLockExclusive(&g_vcPoke);
    k.vcWindows++;
}

// pairs whose count changed since the last call (cumulative counts), with the module of targets outside the exe
static int VcLogPairs() {
    Ts3nCounters& k = ts3native_counters;
    int lines = 0;
    for (int i = 0; i < g_vcN; i++) {
        VcSite& s = g_vc[i];
        for (int j = 0; j < VC_PAIRS && s.p[j].tgt; j++) {
            const LONG n = s.p[j].n;
            if (n == s.p[j].logged) continue;
            s.p[j].logged = n;
            const DWORD t = (DWORD)s.p[j].tgt;
            char mod[MAX_PATH] = "";
            if (t < g_vcImgLo || t >= g_vcImgHi) {
                HMODULE h = NULL;
                char path[MAX_PATH];
                const char* b = "anon";  // no module: heap code (Mono, Frida, a hook's trampoline)
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCWSTR)(uintptr_t)t, &h) && GetModuleFileNameA(h, path, MAX_PATH)) {
                    b = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
                }
                sprintf_s(mod, " out=%s", b);
            }
            Log("vcall 0x%08lX -> 0x%08lX n=%ld%s%s", s.addr, t, n, s.call ? "" : " jmp", mod);
            lines++;
        }
        if (s.over != s.overLogged) {
            s.overLogged = s.over;
            Log("vcall 0x%08lX overflow n=%ld (more than %d targets)", s.addr, s.overLogged, VC_PAIRS);
            lines++;
        }
    }
    Log("vcall window %ld: hits %ld, pairs %ld, outside %ld, overflow %ld, faults %ld, skipped %ld, armed %ld", k.vcWindows,
        k.vcHits, k.vcPairs, k.vcOutside, k.vcOverflow, k.vcFaults, k.vcSkipped, k.vcArmed);
    return lines;
}

static DWORD g_vcDelayMs = 90000, g_vcWindowMs = 30000;
static DWORD WINAPI VcMain(void*) {
    Sleep(g_vcDelayMs);
    if (!(g_vcVeh = AddVectoredExceptionHandler(1, VcVeh))) { Log("FAIL probe snap: AddVectoredExceptionHandler"); return 0; }
    ts3native_counters.vcReady = 1;
    Log("probe snap: %d sites armed, %ld hits per site per %lu s window", g_vcN, g_vcPer, g_vcWindowMs / 1000);
    for (;;) {
        VcArmAll();
        Sleep(g_vcWindowMs);
        VcLogPairs();
    }
}

// [ProbeSnap] Enabled=1: table built now, armed DelaySec later on its own thread
static bool ApplyProbeSnap(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"ProbeSnap", L"Enabled", 0, ini)) { Log("probe snap off (Enabled=0)"); return false; }
    g_vcPer = Clamp(GetPrivateProfileIntW(L"ProbeSnap", L"PerWindow", 16, ini), 1, 100000);
    g_vcWindowMs = (DWORD)Clamp(GetPrivateProfileIntW(L"ProbeSnap", L"WindowSec", 30, ini), 5, 3600) * 1000;
    g_vcDelayMs = (DWORD)Clamp(GetPrivateProfileIntW(L"ProbeSnap", L"DelaySec", 90, ini), 0, 3600) * 1000;
    if (!VcSetup(kVcSites, (int)(sizeof(kVcSites) / sizeof(kVcSites[0])))) { Log("FAIL probe snap: no site table"); return false; }
    HANDLE th = CreateThread(NULL, 64 * 1024, VcMain, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!th) { Log("FAIL probe snap: thread start (err %lu)", GetLastError()); return false; }
    CloseHandle(th);
    Log("OK   probe snap: %d indirect call/jmp sites of the routing closure (%ld rejected), armed in %lu s", g_vcN,
        ts3native_counters.vcSkipped, g_vcDelayMs / 1000);
    return true;
}
