// [LotSinCos] (PLAN #11, docs/38 C2): sin/cos cache for the lot world-to-local transform. Included by ts3native.cpp.
//
// Lot_WorldToLocal2D 0xAB8540 (thiscall, ecx = lot, float x, float z, float* outX, float* outZ; ret 0x10) [verified asm,
// tools/emu spec lot_world_to_local.py: 3,038 of 3,038 cases bit-exact]: s = fsin, c = fcos of the float angle at lot+0x1A0
// (x87, each stored as float); dx = x - [lot+0x190], dz = z - [lot+0x198] (SSE); ns = -0.0 - s (constant 0xF988F0);
// outX = ns*dz + c*dx, outZ = s*dx + c*dz (packed SSE, the s/ns products are the first operands). It reads no globals.
// The two x87 transcendentals are most of its cost (0.60% sim self, docs/38 §7), and lots keep their angle, so the
// cache keys on the angle bits alone. The replacement uses the same instructions in the same operand order (x87 for
// sin/cos, scalar SSE for the rest), so its output is bit-identical on any FPU and MXCSR state, NaN payloads included.
//
// Entries carry a check word written last (as [HeightCache]): a reader on another thread that sees a half-written entry
// counts a miss. Verify=1: every hit also runs the game's function and compares the bits (first 10 mismatches logged; a
// mismatch returns the game's answer). Runtime switch: ts3native_counters.lscMode (+0xC28; 0 = the game's code, 1 = cache).
//
// Registers: the original writes only eax, ecx, xmm0-xmm3, the x87 stack (balanced) and EFLAGS; it keeps edx, ebx, esi, edi,
// ebp and xmm4-xmm7 (verified asm, Steam 0xAB8540 and 1.69 0xAB71B0 are byte-identical apart from the -0.0 constant). Callers
// rely on that: 1.69 0xAB7C80 (return 0xAB7CAC) and Steam 0xAB9037 / 0xAC0C0F / 0xAC586B / 0xAC996D keep the lot in edx
// across the call. A C++ hook may change edx and xmm4-xmm7, so the detour enters LscStub, which saves them around LscHook
// (the crash at 0x00AD04EC after a long-distance move, docs/232 runs 7h / 7j2: a miss returned edx = &g_lsc[slot]).

static const DWORD LSC_FN = TS3A(0x00AB8540);
static const u8 kLscPrologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-16

typedef void(__fastcall* LscFn)(void* lot, void* edx, float x, float z, float* outX, float* outZ);  // thiscall
static LscFn g_lscOrig;
static bool g_lscVerify;
static volatile float g_lscNegZero = -0.0f;  // volatile: the compiler must not fold -0.0 - s into a sign flip
static unsigned __int64 g_lscCalls, g_lscHits;  // shortcut: plain ++ from any thread, a lost count only skews the stats

struct LscEntry { volatile DWORD ang; volatile float s, c; volatile DWORD chk; };
static LscEntry g_lsc[256];
static volatile LONG g_lscEdxClobber;  // stub exits whose edx differs from the entry value; must stay 0 (logged every 60 s)
static volatile LONG g_lscLastHit;     // last call 1 = hit, 0 = miss (for the clobber log line only; racy across threads)

static inline DWORD LscBits(float f) { return *(const DWORD*)&f; }
static inline DWORD LscMix(DWORD ang, float s, float c) { return (ang * 0x9E3779B1u ^ LscBits(s) * 0x85EBCA6Bu ^ LscBits(c) * 0xC2B2AE35u) | 1; }
// top 8 bits of a Fibonacci product: round floats have zero low mantissa bits, and +0.0/-0.0 must not share a slot
static inline LscEntry& LscSlot(DWORD ang) { return g_lsc[(ang * 0x9E3779B1u) >> 24]; }

// sin and cos of *ang as 0xAB8540 computes them: fld; fld st0; fsin; fstp (float); fcos on the first copy; fstp (float)
static void LscSinCos(const float* ang, float* s, float* c) {
    __asm {
        mov eax, ang
        fld dword ptr [eax]
        fld st(0)
        fsin
        mov ecx, s
        fstp dword ptr [ecx]
        fcos
        mov ecx, c
        fstp dword ptr [ecx]
    }
}

// The rotation part, scalar SSE in the game's operand order (lane 0 and lane 1 of its packed ops).
static void LscRotate(const u8* lot, float x, float z, float s, float c, float* outX, float* outZ) {
    float rx, rz;
    __asm {
        mov eax, lot
        movss xmm0, x
        subss xmm0, dword ptr [eax + 0x190]  // dx
        movss xmm1, z
        subss xmm1, dword ptr [eax + 0x198]  // dz
        movss xmm2, g_lscNegZero
        subss xmm2, s                        // ns
        movss xmm3, s
        mulss xmm2, xmm1                     // ns*dz
        mulss xmm3, xmm0                     // s*dx
        movss xmm4, c
        mulss xmm4, xmm0                     // c*dx
        movss xmm5, c
        mulss xmm5, xmm1                     // c*dz
        addss xmm2, xmm4
        addss xmm3, xmm5
        movss rx, xmm2
        movss rz, xmm3
    }
    *outX = rx;
    *outZ = rz;
}

static void __fastcall LscHook(void* lotp, void* edx, float x, float z, float* outX, float* outZ) {
    Ts3nCounters& k = ts3native_counters;
    if (k.lscMode != 1) { g_lscOrig(lotp, edx, x, z, outX, outZ); return; }
    const u8* lot = (const u8*)lotp;
    const DWORD ang = *(const DWORD*)(lot + 0x1A0);
    if ((++g_lscCalls & 1023) == 0) { k.lscCallsK = (LONG)(g_lscCalls / 1000); k.lscHitsK = (LONG)(g_lscHits / 1000); }
    LscEntry& e = LscSlot(ang);
    const DWORD chk = e.chk;
    float s, c;
    if (chk && e.ang == ang) {
        s = e.s; c = e.c;
        if (chk == LscMix(ang, s, c) && e.chk == chk) {
            g_lscHits++;
            g_lscLastHit = 1;
            LscRotate(lot, x, z, s, c, outX, outZ);
            if (!g_lscVerify) return;
            float gx, gz;
            g_lscOrig(lotp, edx, x, z, &gx, &gz);
            if (LscBits(gx) == LscBits(*outX) && LscBits(gz) == LscBits(*outZ)) return;
            if (InterlockedIncrement(&k.lscMismatches) <= 10)
                Log("lotsincos MISMATCH angle %08X x %.3f z %.3f ours %08X %08X game %08X %08X", ang, x, z, LscBits(*outX), LscBits(*outZ),
                    LscBits(gx), LscBits(gz));
            *outX = gx;
            *outZ = gz;
            return;
        }
    }
    g_lscLastHit = 0;
    LscSinCos((const float*)(lot + 0x1A0), &s, &c);
    e.chk = 0;
    e.ang = ang; e.s = s; e.c = c;
    e.chk = LscMix(ang, s, c);
    LscRotate(lot, x, z, s, c, outX, outZ);
}

static void __stdcall LscEdxClobbered(DWORD ret, DWORD inEdx, DWORD outEdx) {
    if (InterlockedIncrement(&g_lscEdxClobber) <= 10)
        Log("lotsincos EDX CLOBBER caller ret %08X edx in %08X out %08X (%s)", ret, inEdx, outEdx, g_lscLastHit ? "hit" : "miss");
}

// The detour target: the game's thiscall convention (ecx = lot, 4 stack args, ret 10h) with edx and xmm4-xmm7 kept as the
// original keeps them. ebx/esi/edi/ebp are callee-saved in LscHook already. The exit check compares edx with a second copy
// of the entry value (always on, 2 instructions; a nonzero count means this frame is broken).
static __declspec(naked) void LscStub() {
    __asm {
        push edx                              ; [esp] entry edx, checked at exit
        push edx                              ; entry edx, restored at exit
        sub esp, 64
        movups xmmword ptr [esp], xmm4
        movups xmmword ptr [esp + 16], xmm5
        movups xmmword ptr [esp + 32], xmm6
        movups xmmword ptr [esp + 48], xmm7
        push dword ptr [esp + 88]             ; outZ (xmm 64, edx 2 x 4, ret 4, x z outX outZ)
        push dword ptr [esp + 88]             ; outX
        push dword ptr [esp + 88]             ; z
        push dword ptr [esp + 88]             ; x
        call LscHook                          ; __fastcall: ecx = lot, edx = caller's edx; pops 16
        movups xmm4, xmmword ptr [esp]
        movups xmm5, xmmword ptr [esp + 16]
        movups xmm6, xmmword ptr [esp + 32]
        movups xmm7, xmmword ptr [esp + 48]
        add esp, 64
        pop edx
        cmp edx, [esp]
        jne clobbered
        add esp, 4
        ret 10h
    clobbered:
        sub esp, 64
        movups xmmword ptr [esp], xmm4
        movups xmmword ptr [esp + 16], xmm5
        movups xmmword ptr [esp + 32], xmm6
        movups xmmword ptr [esp + 48], xmm7
        push edx                              ; outEdx
        push dword ptr [esp + 68]             ; inEdx (the checked copy)
        push dword ptr [esp + 76]             ; caller's return address
        call LscEdxClobbered                  ; __stdcall, pops 12
        movups xmm4, xmmword ptr [esp]
        movups xmm5, xmmword ptr [esp + 16]
        movups xmm6, xmmword ptr [esp + 32]
        movups xmm7, xmmword ptr [esp + 48]
        add esp, 64
        pop edx                               ; the entry value
        ret 10h
    }
}

static void LscLogCounters() {
    const Ts3nCounters& k = ts3native_counters;
    Log("lot sin/cos: mode %ld callsK %ld hitsK %ld mismatches %ld edxClobber %ld", k.lscMode, k.lscCallsK, k.lscHitsK, k.lscMismatches,
        g_lscEdxClobber);
}

// [LotSinCos] Enabled=1 hooks; Mode = start value of ts3native_counters.lscMode; Verify=1 checks every hit.
static bool ApplyLotSinCos(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"LotSinCos", L"Enabled", 0, ini)) { Log("lot sin/cos cache disabled (Enabled=0)"); return false; }
    g_lscVerify = GetPrivateProfileIntW(L"LotSinCos", L"Verify", 0, ini) != 0;
    ts3native_counters.lscMode = GetPrivateProfileIntW(L"LotSinCos", L"Mode", 1, ini) == 0 ? 0 : 1;
    g_lscOrig = (LscFn)InstallDetour("lot world-to-local (sin/cos cache)", LSC_FN, kLscPrologue, sizeof(kLscPrologue), (void*)LscStub, &g_lscOrig);
    if (!g_lscOrig) return false;
    ts3native_counters.lscReady = 1;
    Log("OK   lot sin/cos cache: 0x%08X (Mode=%ld Verify=%d, 256 angles)", LSC_FN, ts3native_counters.lscMode, g_lscVerify ? 1 : 0);
    return true;
}
