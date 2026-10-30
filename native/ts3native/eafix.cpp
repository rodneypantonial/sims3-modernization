// [EaCrashFixes] (0.36.121, docs/67 §26.4 + §27.5, counters v141 ca* / tp*, ledger docs/51 §394): guards for two EA
// lifetime bugs that crashed a contributor's game (issues #98, #99). Both switches default OFF. Included by ts3native.cpp.
//
// ConformActor (#98, crash V; crash I had the same site). Animation controllers (base 0x565A80) keep their actor raw at
// +0x1C. For the route-follow body's ConformToRouteController (vtable 0xFC2068, ctor 0x56E4C0) the actor is an
// AnimationComponent's +8 interface (vtable 0x10433EC): the body's own one ([body+0x124], set from the agent step 0x956A2C:
// component bit [0x11DC484] + 8) or one of [body+0xB4]. Three walkers of the body's controller list (+0x1C) ask the actor
// `[[actor]+0x10](controller)` = 0x93E130 ("does my rig still run this controller's serial?"); false = the controller is
// done and the walker drops it (0x84EA00 and 0x853500 unlink and release it, 0x849570 skips its listener). The actor can
// be destroyed while a controller still names it; the call then goes through a freed vtable [verified disassembly].
// Fix: a set of live AnimationComponents (call-site hooks on its only constructor call 0x93F90D -> 0x93E5C0 and its only
// destructor call 0x942E73 -> 0x941CD0; the dtor leaves 0x10433EC at +8, so the vtable word alone cannot tell a freed
// component), and at the three ask sites `push eax; call CaGuard` in place of the virtual call:
//   actor - 8 in the set              -> the game's own call (unchanged behaviour)
//   actor NULL                         -> false (skip[0])
//   vtable 0x10433EC, not in the set   -> false: a destroyed AnimationComponent (skip[1])
//   vtable unreadable, outside .rdata, or slot +0x10 outside .text -> false: freed memory (skip[2])
//   any other in-image vtable          -> the game's own call (another actor type: caOther counts them)
// "false" is the game's own "controller done" answer, so the walker cleans the controller up the way EA does.
// No game state is written; the guard only reads the actor's first dword under SEH.
//
// ThumbPose (#99, crash W). The Sim thumbnail render 0xD59C10, branch (a) (0xD5B745-0xD5B786), looks up an object into a
// local RefPtr (esp+0x28), copies its raw +0xC (the source pose) into the pose context, then releases that RefPtr
// (`call 0x7460F0` at 0xD5B77A) before the pose is evaluated (`call 0x54C570` at 0xD5B86F). Fix: the release call is
// retargeted to TpDefer (keeps the reference, keyed by the render's frame and thread) and the instruction after the
// evaluation (0xD5B874 `mov ecx,[esp+0xAC]`) becomes a call to TpAfterStub, which releases it there (same thread, same
// release function). Branches (b) and (c) hold no reference at all and stay as they are [verified disassembly].
//
// GeomBoneIndex (0.36.143, docs/67 §28, crash X). The GEOM resource (vtable 0xFE32C0, "CAS/GeometryResource") keeps its
// bone hashes in a vector at +0x58/+0x5C; slot +0x54 (0x5C6E00) returns bones[i] with no bounds check, slot +0x50
// (0x5C6DF0) the count. The Sim builder's skin pass 0x5D1010 asks slot +0x54 for each of a vertex's four blend indices
// (0x5D1631) and looks the hash up in the rig's bone map; a miss takes EA's own path for a bone the rig lacks (0x5D165B).
// A mesh whose blend index is past its own bone list reads past the vector and crashed at 0x5C6E07 [verified disassembly].
// Fix: the vtable slot points to GbGetBone, which answers 0 for such an index, so the lookup misses the way an unknown
// bone does. Indices inside the list get the game's answer.

static const DWORD CA_ASK_SITES[3] = {0x0084EABD, 0x00849588, 0x00853521};
// mov ecx,[eax+1Ch]; mov edx,[ecx]; push eax; mov eax,[edx+10h]; call eax   (eax = controller)
static const u8 kCaAsk[11] = {0x8B, 0x48, 0x1C, 0x8B, 0x11, 0x50, 0x8B, 0x42, 0x10, 0xFF, 0xD0};
static const DWORD CA_CTOR_SITE = 0x0093F90D, CA_CTOR = 0x0093E5C0;  // AnimationComponent ctor (thiscall, owner; ret 4)
static const DWORD CA_DTOR_SITE = 0x00942E73, CA_DTOR = 0x00941CD0;  // AnimationComponent dtor (thiscall, no args)
static const DWORD CA_ANIM_VT8 = 0x010433EC;                          // AnimationComponent +8 interface vtable
static DWORD g_ecaRdLo = 0x00F95000, g_ecaRdHi = 0x01146000;            // .rdata (vtables); selftest: its own range
static DWORD g_ecaTxLo = 0x00401000, g_ecaTxHi = 0x00F95000;            // .text

typedef void*(__fastcall* CaCtorFn)(u8* comp, void* edx, void* owner);
typedef void(__fastcall* CaDtorFn)(u8* comp);
typedef bool(__fastcall* CaAskFn)(u8* actor, void* edx, u8* ctl);
static CaCtorFn g_ecaCtor = (CaCtorFn)(uintptr_t)CA_CTOR;
static CaDtorFn g_ecaDtor = (CaDtorFn)(uintptr_t)CA_DTOR;
static volatile LONG g_ecaLogged;

// Live set: open addressing, linear probe, 1 = tombstone. Full (more than half live) = tracking stops for the session and
// the guard calls the game for every actor it cannot prove dead by its vtable word (caOverflow).
static const DWORD CA_CAP = 1u << 17, CA_TOMB = 1;
static DWORD* g_ecaSet;
static DWORD g_ecaUsed, g_ecaLive;  // used = live + tombstones
static SRWLOCK g_ecaLock = SRWLOCK_INIT;
static volatile bool g_ecaFull;

static inline DWORD CaSlot(DWORD p) { return ((p >> 3) * 0x9E3779B1u) >> 15; }  // 17 bits

static bool CaHas(DWORD p) {
    AcquireSRWLockShared(&g_ecaLock);
    bool hit = false;
    for (DWORD i = CaSlot(p), n = 0; n < CA_CAP; i = (i + 1) & (CA_CAP - 1), n++) {
        const DWORD k = g_ecaSet[i];
        if (k == p) { hit = true; break; }
        if (!k) break;
    }
    ReleaseSRWLockShared(&g_ecaLock);
    return hit;
}

static void CaInsertLocked(DWORD* set, DWORD p) {
    DWORD i = CaSlot(p);
    while (set[i] > CA_TOMB) i = (i + 1) & (CA_CAP - 1);
    if (!set[i]) g_ecaUsed++;
    set[i] = p;
}

static void CaAdd(DWORD p) {
    if (g_ecaFull) return;
    AcquireSRWLockExclusive(&g_ecaLock);
    for (DWORD i = CaSlot(p), n = 0; n < CA_CAP; i = (i + 1) & (CA_CAP - 1), n++) {  // a reused address that missed its dtor
        if (g_ecaSet[i] == p) { ReleaseSRWLockExclusive(&g_ecaLock); return; }
        if (!g_ecaSet[i]) break;
    }
    if (g_ecaLive + 1 > CA_CAP / 2) {
        g_ecaFull = true, ts3native_counters.caOverflow = 1;
        ReleaseSRWLockExclusive(&g_ecaLock);
        Log("ea fix: ConformActor: more than %lu live AnimationComponents, tracking stopped (unknown actors get the game's call)", CA_CAP / 2);
        return;
    }
    if (g_ecaUsed + 1 > CA_CAP / 4 * 3) {  // rebuild without tombstones (rare: only after many destroys)
        DWORD* fresh = (DWORD*)VirtualAlloc(NULL, CA_CAP * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (fresh) {
            DWORD* old = g_ecaSet;
            g_ecaSet = fresh, g_ecaUsed = 0;
            for (DWORD i = 0; i < CA_CAP; i++)
                if (old[i] > CA_TOMB) CaInsertLocked(fresh, old[i]);
            VirtualFree(old, 0, MEM_RELEASE);
        }
    }
    CaInsertLocked(g_ecaSet, p);
    const LONG live = (LONG)++g_ecaLive;
    ts3native_counters.caLive = live;
    if (live > ts3native_counters.caMaxLive) ts3native_counters.caMaxLive = live;
    ReleaseSRWLockExclusive(&g_ecaLock);
}

static void CaDel(DWORD p) {
    if (g_ecaFull) return;
    AcquireSRWLockExclusive(&g_ecaLock);
    for (DWORD i = CaSlot(p), n = 0; n < CA_CAP; i = (i + 1) & (CA_CAP - 1), n++) {
        if (g_ecaSet[i] == p) { g_ecaSet[i] = CA_TOMB, ts3native_counters.caLive = (LONG)--g_ecaLive; break; }
        if (!g_ecaSet[i]) break;
    }
    ReleaseSRWLockExclusive(&g_ecaLock);
}

static void* __fastcall CaCtorHook(u8* comp, void*, void* owner) {
    void* r = g_ecaCtor(comp, NULL, owner);
    CaAdd((DWORD)(uintptr_t)comp);
    return r;
}

static void __fastcall CaDtorHook(u8* comp) {
    CaDel((DWORD)(uintptr_t)comp);  // before the body: from here on the component counts as gone
    g_ecaDtor(comp);
}

// 0 = call the game (live, or a type we cannot judge); 1-3 = skip reason (index + 1 into caSkip).
static int CaDead(u8* a, DWORD* vtOut) {
    *vtOut = 0;
    if (!a) return 1;
    if (!g_ecaFull && CaHas((DWORD)(uintptr_t)a - 8)) return 0;
    DWORD vt = 0, fn = 0;
    __try { vt = *(const volatile DWORD*)a; } __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
    *vtOut = vt;
    if (vt == CA_ANIM_VT8) return g_ecaFull ? 0 : 2;
    if (vt < g_ecaRdLo || vt >= g_ecaRdHi || (vt & 3)) return 3;
    __try { fn = ((const volatile DWORD*)(uintptr_t)vt)[4]; } __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
    if (fn < g_ecaTxLo || fn >= g_ecaTxHi) return 3;
    ts3native_counters.caOther++;  // sim thread; a lost count under a race is fine
    return 0;
}

// Called by the patched ask sites in place of `[[ctl+1Ch]]+10h`(ctl); stdcall pops ctl like the thiscall did.
static bool __stdcall CaGuard(u8* ctl) {
    u8* a = *(u8* const*)(ctl + 0x1C);  // the controller itself is alive: the walker's list holds a reference
    DWORD vt;
    const int why = CaDead(a, &vt);
    if (!why) return ((CaAskFn)(uintptr_t)(*(const DWORD* const*)a)[4])(a, NULL, ctl);  // the game's own call
    InterlockedIncrement(&ts3native_counters.caSkip[why - 1]);
    if (InterlockedIncrement(&g_ecaLogged) <= 8) {
        static const char* const kWhy[3] = {"null actor", "AnimationComponent destroyed", "vtable unreadable or outside the exe"};
        DWORD cvt = 0, serial = 0;
        __try { cvt = *(const DWORD*)ctl, serial = *(const DWORD*)(ctl + 0x20); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("ea fix: ConformActor: controller %p (vtable 0x%08lX, serial %lu) skipped: actor %p (vtable word 0x%08lX): %s",
            ctl, cvt, serial, a, vt, kWhy[why - 1]);
    }
    return false;
}

// Ask site i at `at`: 11 original bytes -> push eax; call CaGuard; 5-byte nop. Returns false (nothing written) on other bytes.
static bool CaPatchAsk(u8* at) {
    if (!Matches(at, kCaAsk, sizeof(kCaAsk))) return false;
    u8 b[11] = {0x50, 0xE8, 0, 0, 0, 0, 0x0F, 0x1F, 0x44, 0x00, 0x00};
    *(LONG*)(b + 2) = (LONG)((uintptr_t)&CaGuard - ((uintptr_t)at + 6));
    return Write(at, b, sizeof(b));
}

static bool ApplyConformActor(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"EaCrashFixes", L"ConformActor", 0, ini)) return false;
    // all or nothing: the guard without the live set would call every live AnimationComponent dead
    bool ok = FpCallOk(CA_CTOR_SITE, CA_CTOR) && FpCallOk(CA_DTOR_SITE, CA_DTOR);
    for (DWORD s : CA_ASK_SITES) ok = ok && Matches((const void*)(uintptr_t)s, kCaAsk, sizeof(kCaAsk));
    if (!ok) { Log("SKIP ea fix: ConformActor: a site's bytes differ (another mod or a probe build owns it): not installed"); return false; }
    g_ecaSet = (DWORD*)VirtualAlloc(NULL, CA_CAP * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_ecaSet) { Log("FAIL ea fix: ConformActor: no memory for the live set"); return false; }
    // the set hooks first: no AnimationComponent exists yet (we run before the exe's entry point)
    if (!FpRedirect(CA_CTOR_SITE, (const void*)&CaCtorHook) || !FpRedirect(CA_DTOR_SITE, (const void*)&CaDtorHook)) {
        Log("FAIL ea fix: ConformActor: cannot write the constructor / destructor call sites");
        return false;
    }
    int n = 0;
    for (DWORD s : CA_ASK_SITES) n += CaPatchAsk((u8*)(uintptr_t)s);
    ts3native_counters.caOn = n;
    Log("OK   ea fix: ConformActor (docs/67 §26.4): live AnimationComponent set + %d of 3 controller ask sites guarded", n);
    return true;  // keep the log open for the skip lines
}

// ---- ThumbPose ----
static const DWORD TP_DEFER_SITE = 0x00D5B77A, TP_RELEASE = 0x007460F0;  // call RefPtr_Release (ecx = &RefPtr)
static const DWORD TP_AFTER_SITE = 0x00D5B874;                           // mov ecx,[esp+0ACh] after the evaluation
static const u8 kTpAfter[7] = {0x8B, 0x8C, 0x24, 0xAC, 0x00, 0x00, 0x00};
typedef void(__fastcall* TpReleaseFn)(u8** ref);
static TpReleaseFn g_tpRelease = (TpReleaseFn)(uintptr_t)TP_RELEASE;

// One deferred reference per render frame in flight: {thread, frame esp, object}. The render does not nest between the
// two sites (only pose/pool calls run there), so one slot per thread is enough; 8 cover the main and sim threads.
struct TpSlot { volatile LONG tid; DWORD frame; u8* obj; };
static TpSlot g_tp[8];

static void TpReleaseNow(u8* obj) {
    __try { if (*(volatile LONG*)(obj + 4) == 1) InterlockedIncrement(&ts3native_counters.tpLastRef); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    u8* tmp = obj;
    g_tpRelease(&tmp);
}

// Replaces `call 0x7460F0` at 0xD5B77A: ecx = &RefPtr local at frame+0x28. Keeps the reference instead of releasing it.
static void __fastcall TpDefer(u8** ref) {
    u8* obj = *ref;
    if (!obj) return;  // release of an empty RefPtr does nothing either
    const LONG tid = (LONG)GetCurrentThreadId();
    const DWORD frame = (DWORD)(uintptr_t)ref - 0x28;
    for (TpSlot& s : g_tp)
        if (s.tid == tid && s.frame == frame && s.obj) TpReleaseNow(s.obj), s.obj = NULL;  // stale (never seen: same frame again)
    for (TpSlot& s : g_tp) {
        if (s.tid == tid && !s.obj) { s.frame = frame, s.obj = obj; InterlockedIncrement(&ts3native_counters.tpHeld); return; }
    }
    for (TpSlot& s : g_tp) {
        if (InterlockedCompareExchange(&s.tid, tid, 0) == 0) { s.frame = frame, s.obj = obj; InterlockedIncrement(&ts3native_counters.tpHeld); return; }
    }
    InterlockedIncrement(&ts3native_counters.tpNoSlot);
    g_tpRelease(ref);  // vanilla order
}

// After the evaluation: release this frame's kept reference (none when branch (b) or (c) ran).
static void __stdcall TpAfter(DWORD frame) {
    const LONG tid = (LONG)GetCurrentThreadId();
    for (TpSlot& s : g_tp)
        if (s.tid == tid && s.frame == frame && s.obj) {
            u8* obj = s.obj;
            s.obj = NULL;
            TpReleaseNow(obj);
            return;
        }
}

// Replaces `mov ecx,[esp+0ACh]` (7 bytes) at 0xD5B874 with `call TpAfterStub; 2-byte nop`. Every register is kept and
// the moved instruction runs last; flags are dead there (push edi; call 0x573330 follow).
static __declspec(naked) void TpAfterStub() {
    __asm {
        pushad
        lea eax, [esp + 36]  // the render's esp: 32 bytes of pushad + our return address
        push eax
        call TpAfter
        popad
        mov ecx, [esp + 4 + 0xAC]
        ret
    }
}

static bool TpPatch(u8* defer, u8* after) {
    u8 call[5] = {0xE8};
    *(LONG*)(call + 1) = (LONG)(TP_RELEASE - ((uintptr_t)defer + 5));
    if (!Matches(defer, call, 5) || !Matches(after, kTpAfter, sizeof(kTpAfter))) return false;
    u8 b[7] = {0xE8, 0, 0, 0, 0, 0x66, 0x90};
    *(LONG*)(b + 1) = (LONG)((uintptr_t)&TpAfterStub - ((uintptr_t)after + 5));
    if (!Write(after, b, sizeof(b))) return false;  // release point first: a deferral is never left without it
    *(LONG*)(call + 1) = (LONG)((uintptr_t)&TpDefer - ((uintptr_t)defer + 5));
    return Write(defer, call, 5);
}

static bool ApplyThumbPose(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"EaCrashFixes", L"ThumbPose", 0, ini)) return false;
    if (!TpPatch((u8*)(uintptr_t)TP_DEFER_SITE, (u8*)(uintptr_t)TP_AFTER_SITE)) {
        Log("SKIP ea fix: ThumbPose: bytes at 0x%08X / 0x%08X differ: not installed", TP_DEFER_SITE, TP_AFTER_SITE);
        return false;
    }
    ts3native_counters.tpOn = 1;
    Log("OK   ea fix: ThumbPose (docs/67 §27.5): thumbnail branch (a) keeps its object reference until the source pose is evaluated");
    return false;
}

// ---- GeomBoneIndex ----
static const DWORD GB_SLOT = 0x00FE3314, GB_GET = 0x005C6E00, GB_COUNT = 0x005C6DF0;  // vtable 0xFE32C0 +0x54, +0x50
// mov eax,[ecx+58h]; mov ecx,[esp+4]; mov eax,[eax+ecx*4]; ret 4
static const u8 kGbGet[13] = {0x8B, 0x41, 0x58, 0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x04, 0x88, 0xC2, 0x04, 0x00};
// mov eax,[ecx+5Ch]; sub eax,[ecx+58h]; sar eax,2; ret
static const u8 kGbCount[10] = {0x8B, 0x41, 0x5C, 0x2B, 0x41, 0x58, 0xC1, 0xF8, 0x02, 0xC3};
static volatile LONG g_gbBad;

static DWORD __fastcall GbGetBone(u8* geom, void*, DWORD i) {
    const DWORD* b = *(const DWORD* const*)(geom + 0x58);
    const DWORD n = (DWORD)(*(const DWORD* const*)(geom + 0x5C) - b);
    if (i < n) return b[i];
    if (InterlockedIncrement(&g_gbBad) <= 8)
        Log("ea fix: GeomBoneIndex: GEOM %p asked for bone %lu of %lu (caller 0x%08X): answered 0, the rig's missing-bone path",
            geom, i, n, (DWORD)(uintptr_t)_ReturnAddress());
    return 0;
}

// slot = the vtable entry; checks the getter it names and the count getter (the vector layout) first.
static bool GbPatch(DWORD* slot, const u8* count) {
    if (!Readable(slot, 4) || !Matches((const void*)(uintptr_t)*slot, kGbGet, sizeof(kGbGet)) ||
        !Matches(count, kGbCount, sizeof(kGbCount)))
        return false;
    const DWORD hook = (DWORD)(uintptr_t)&GbGetBone;
    return Write(slot, &hook, 4);
}

static bool ApplyGeomBoneIndex(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"EaCrashFixes", L"GeomBoneIndex", 0, ini)) return false;
    DWORD* slot = (DWORD*)(uintptr_t)GB_SLOT;
    if (!Readable(slot, 4) || *slot != GB_GET || !GbPatch(slot, (const u8*)(uintptr_t)GB_COUNT)) {
        Log("SKIP ea fix: GeomBoneIndex: vtable slot 0x%08X or the getter bytes differ: not installed", GB_SLOT);
        return false;
    }
    Log("OK   ea fix: GeomBoneIndex (docs/67 §28): GEOM bone-by-index getter answers 0 past the mesh's bone list");
    return true;  // keep the log open for the skip lines
}

static bool ApplyEaCrashFixes(const wchar_t* ini) {
    bool keep = ApplyConformActor(ini);
    ApplyThumbPose(ini);
    keep = ApplyGeomBoneIndex(ini) || keep;
    return keep;
}
