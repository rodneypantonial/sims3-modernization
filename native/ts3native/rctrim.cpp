// [ResourceCache] TrimAfterCleanup (docs/198 §4, counters v136 rt*): EXPERIMENT, off by default, never a release default.
// ResourceCache is Apex Radiance's area (docs/191 Ownership): nothing here goes to Apex unless it shows a proven gain.
// Included by ts3native.cpp after memdiag.cpp (uses userdiag.cpp's g_udWorlds / UdMemWalk / UdPrivateUsage, memdiag.cpp's
// g_mdWorld) and apex.cpp (g_apexFound / g_apexStandDown).
//
// What it does: calls the game's own shrink of both ResourceSystem caches, ResourceManager slot 0x733E70 (vtable 0xFFE250
// +0xF0): for the caches at manager +0x1E0 (ResourceCache) and +0x1E4 (AnimationCache), trim 0x732E90(budget, 0, 1) =
// target budget - budget = 0 with force 1, i.e. drop every entry on the flush (idle, unreferenced) list; referenced entries
// are never on that list [verified disassembly, docs/198 §4.1]. The same function Apex MemoryGuard calls under low address
// space (docs/191 prior art).
// Where: on the main thread, right after the ResourceSystem service's own update 0x7377F0 (service vtable 0xFFEB10 slot
// +0x1C, data 0xFFEB2C) returns. That update is ServiceList_Update's main slot (docs/36 §1, docs/167 §7.1) and itself calls
// 0x737560 = trim(cache, 0, 0, 0) on both caches every frame, so our call runs the same trim on the same thread in the same
// place with a lower target [verified]. The trim takes g_ResourceCacheLock 0xFFDD34 itself and releases the dropped entries
// (0x731DD0) after unlocking, as in the per-frame call.
// Triggers: a world load (the ts3fixes "world:" line, [UserDiag] on; travel arrival = a load with a new world name), after
// a settle delay; every TrimEveryMin minutes of play. The Overwatch nightly trigger is not built: ts3fixes sends no
// Overwatch signal to native (docs/198 §4.2).
// Apex Radiance loaded and [Compat] ApexStandDown=1: no trim, one log line (late Apex load included: checked per trim).
static const DWORD RT_SVC_SLOT = 0x00FFEB2C, RT_SVC_UPDATE = 0x007377F0;  // ResourceSystem service +0x1C (main thread)
static const DWORD RT_MGR_VT = 0x00FFE250, RT_CACHE_VT = 0x00FFDD94;
typedef void(__thiscall* RtSvcFn)(void* svc, DWORD realDt, DWORD gameDt);
typedef void(__thiscall* RtShrinkFn)(void* mgr);
static RtSvcFn g_rtOrig;
static RtShrinkFn g_rtShrink = (RtShrinkFn)(uintptr_t)0x00733E70;  // selftest: a fake
static bool g_rtOn;
static DWORD g_rtEveryMs;                 // 0 = no periodic trim
static DWORD g_rtSettleMs = 2000;         // world load -> trim
static LONG g_rtWorlds;                   // g_udWorlds already handled
static DWORD g_rtWorldTick, g_rtLastTick; // world load seen at, last trim
static bool g_rtWorldDue;                 // a world load waits for its trim
static char g_rtLastWorld[64];
static bool g_rtStoodLogged, g_rtWalk = true;  // selftest: no address-space walk

struct RtCache { DWORD held, cold; bool ok; };
static RtCache RtRead(const u8* c) {
    RtCache r = {};
    __try {
        if (c && *(const DWORD*)c == RT_CACHE_VT) r.held = *(const volatile DWORD*)(c + 0x94), r.cold = *(const volatile DWORD*)(c + 0x98), r.ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

// The trim itself (main thread). mgr = ResourceManager (service + 0x18, vtable 0xFFE250 [verified ctor 0x739120 -> 0x736DB0]).
static void RtTrim(u8* mgr, const char* why) {
    if (g_apexFound && g_apexStandDown) {
        InterlockedIncrement(&ts3native_counters.rtStood);
        if (!g_rtStoodLogged) g_rtStoodLogged = true, Log("rctrim: stood down: Apex Radiance loaded (ResourceCache is its area, MemoryGuard trims it)");
        return;
    }
    const u8* c0 = NULL;
    const u8* c1 = NULL;
    __try {
        if (*(const DWORD*)mgr == RT_MGR_VT) c0 = *(u8* const*)(mgr + 0x1E0), c1 = *(u8* const*)(mgr + 0x1E4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    const RtCache a0 = RtRead(c0), a1 = RtRead(c1);
    if (!a0.ok) {
        if (InterlockedIncrement(&ts3native_counters.rtBad) <= 3) Log("rctrim: skipped %s: manager or ResourceCache vtable not as expected", why);
        return;
    }
    UdMemStat s0 = {}, s1 = {};
    u64 top = 0;
    LONG walkUs = 0;
    if (g_rtWalk) walkUs = UdMemWalk(s0, &top);
    const u64 p0 = UdPrivateUsage();
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    g_rtShrink(mgr);
    const LONG us = UsSince(t0);
    const RtCache b0 = RtRead(c0), b1 = RtRead(c1);
    const u64 p1 = UdPrivateUsage();
    if (g_rtWalk) walkUs += UdMemWalk(s1, &top);
    // other threads add and touch entries between the reads (the trim locks only itself): count drops only
    const LONG d0 = (LONG)(a0.cold - b0.cold), d1 = a1.ok && b1.ok ? (LONG)(a1.cold - b1.cold) : 0;
    const DWORD freed = (DWORD)(d0 > 0 ? d0 : 0) + (DWORD)(d1 > 0 ? d1 : 0);
    const LONG n = InterlockedIncrement(&ts3native_counters.rtTrims);
    InterlockedExchangeAdd(&ts3native_counters.rtFreedKB, (LONG)(freed >> 10));
    ts3native_counters.rtLastFreedKB = (LONG)(freed >> 10);
    if (us > ts3native_counters.rtMaxUs) ts3native_counters.rtMaxUs = us;
    const double MB = 1.0 / (1 << 20);
    Log("rctrim: #%ld %s world=%s rc=%.1f/%.1f -> %.1f/%.1f anim=%.1f/%.1f -> %.1f/%.1f freed=%.1f priv=%.0f -> %.0f "
        "commit=%.0f -> %.0f big=%.0f -> %.0f ms=%.1f walkMs=%.1f (MB; rc/anim = held/idle)",
        n, why, g_mdWorld[0] ? g_mdWorld : "-", a0.held * MB, a0.cold * MB, b0.held * MB, b0.cold * MB, a1.held * MB, a1.cold * MB,
        b1.held * MB, b1.cold * MB, freed * MB, p0 * MB, p1 * MB, s0.commit * MB, s1.commit * MB, s0.big1 * MB, s1.big1 * MB,
        us / 1000.0, walkUs / 1000.0);
}

// Trigger check, once per main-thread service pass: NULL = nothing due. `worlds` = g_udWorlds.
static const char* RtDue(DWORD now, LONG worlds) {
    if (worlds != g_rtWorlds) g_rtWorlds = worlds, g_rtWorldTick = now, g_rtWorldDue = true;
    if (g_rtWorldDue && now - g_rtWorldTick >= g_rtSettleMs) {
        g_rtWorldDue = false, g_rtLastTick = now;
        const char* why = !g_rtLastWorld[0] ? "load" : strcmp(g_rtLastWorld, g_mdWorld) ? "world-switch" : "reload";
        strcpy_s(g_rtLastWorld, g_mdWorld);
        return why;
    }
    if (g_rtEveryMs && worlds > 0 && !g_rtWorldDue && now - g_rtLastTick >= g_rtEveryMs) return g_rtLastTick = now, "periodic";
    return NULL;
}

static void __fastcall RtSvcHook(u8* svc, void*, DWORD realDt, DWORD gameDt) {
    g_rtOrig(svc, realDt, gameDt);
    if (const char* why = RtDue(GetTickCount(), g_udWorlds)) RtTrim(svc + 0x18, why);
}

static void ApplyRcTrim(const wchar_t* ini) {
    g_rtOn = GetPrivateProfileIntW(L"ResourceCache", L"TrimAfterCleanup", 0, ini) > 0;
    const int every = GetPrivateProfileIntW(L"ResourceCache", L"TrimEveryMin", 0, ini);
    g_rtEveryMs = every > 0 ? (DWORD)(every < 600 ? every : 600) * 60000 : 0;
    if (!g_rtOn) return;
    DWORD* slot = (DWORD*)(uintptr_t)RT_SVC_SLOT;
    if (*slot != RT_SVC_UPDATE) { Log("FAIL rctrim: ResourceSystem service slot 0x%08X holds 0x%08lX, not 0x%08X: not installed", RT_SVC_SLOT, *slot, RT_SVC_UPDATE); return; }
    g_rtOrig = (RtSvcFn)(uintptr_t)RT_SVC_UPDATE;
    const DWORD hook = (DWORD)(uintptr_t)&RtSvcHook;
    if (!Write(slot, &hook, 4)) { Log("FAIL rctrim: cannot write the service slot"); return; }
    ts3native_counters.rtOn = 1;
    Log("rctrim: on (EXPERIMENT, docs/198 §4): game shrink-both 0x733E70 after the ResourceSystem main-thread update; "
        "triggers: world load (+%lu ms, needs [UserDiag]), every %d min%s; Overwatch nightly not built",
        g_rtSettleMs, every > 0 ? every : 0, every > 0 ? "" : " (off)");
}
