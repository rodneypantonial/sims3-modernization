// [MonoJit] (docs/527 step 1, docs/207 §7-§14, counters v908 jit*): the method compiler of native/monohost/monohost-jit.cpp
// in the game, without lazy frames, the inliner and count mode. Default OFF. Included by ts3native.cpp after autshadow.cpp
// (the store templates call Satb's log); ilpatch.cpp's detour of the transform entry calls JitPostPass after EA's transform.
//
// How it works (the host's, docs/207 §7): a listed method whose IL hash is known gets compiled from the mint code EA's
// transform just built. Every instruction start's opcode word becomes JIT_OP (0x1A9, EA's "Unimplemented" slot; leave.s
// keeps its word, JitWord); the jump-table entry of JIT_OP is JitEnter, which maps edi (ip) to native code through JitFind
// and jumps there on the sim thread. Native code runs inside the interpreter's own C frame, every local, argument and
// stack slot stays in arena memory, and anything without a template leaves through "exit to handler": edi = ip,
// [ebp-4] = ip, eax = edx = original opcode, jmp to the LIVE jump-table entry, so CallPath, Satb's swaps and MintDivGuard
// stay in effect. Exits leave no native frame behind (Direct=0: every call and return runs EA's or CallPath's handler).
//
// Any other thread (the [AutShadow] worker, the finalizer) and the sim thread before its first loop iteration take the
// original opcode's live handler from JitEnter: no compiled code ever runs there (docs/207 §14).
//
// ForceJIT (MonoPatcher, 0xE5637D jne -> 90 90) re-transforms a method on every call into new mint code: the post-pass
// refuses while the byte is not 0x75, and old patched words are simply not reached, so unlike a lazy call site nothing
// here needs the per-call test of mintcall.cpp (compiled code is only entered through words in the code EA dispatches).

#include "ts3addr.h"  // TS3A(x): TS3W.exe addresses by their Steam 1.67 value

static const DWORD JIT_OP = 0x1A9, JIT_EA_UNIMPLEMENTED = TS3A(0x00E596C2);
static const DWORD JIT_PE_STAMP = 0x52DEC247, JIT_PE_SIZE = TS3A(0x00E5A000);  // the exe gate: Steam 1.67 (docs/527 §4.1)
// game addresses the selftest points elsewhere
static DWORD* g_jitTableP = (DWORD*)(uintptr_t)TS3A(0x00E59880);                  // the mint jump table
static const BYTE* g_jitOpLen = (const BYTE*)(uintptr_t)TS3A(0x01195088);        // instruction length in words, per opcode
static const BYTE* g_jitSuper = (const BYTE*)(uintptr_t)TS3A(0x0108FCD8);        // superinstruction descriptors (0x48 bytes each)
static const char* const* g_jitOpNames = (const char* const*)(uintptr_t)TS3A(0x011949E0);
static const BYTE* g_jitForceJit = (const BYTE*)(uintptr_t)TS3A(0x00E5637D);     // MonoPatcher's jne; 0x75 = stock
static DWORD g_jitMemcpy = TS3A(0x00F7E8A6), g_jitFmod = TS3A(0x00F7EFB0);        // EA's CRT memcpy thunk, _CIfmod
static const DWORD* g_jitMemmove = (const DWORD*)(uintptr_t)TS3A(0x00F95684);    // the memmove import
static void** g_jitObjectClassP = (void**)(uintptr_t)(TS3A(0x01220E40) + 4);     // mono_defaults.object_class
struct JitRow { const char* key; DWORD size; BYTE sha[32]; };
#include "monojit-list.h"  // kJitRows, kJitRowN: the built-in list, name + size + SHA-256 of each known IL body (tools/jit-hot.ts --emit-native)
static const JitRow* g_jitRows = kJitRows;
static int g_jitRowN = kJitRowN;
static char g_jitUserBuf[512];       // [MonoJit] List=: narrows the built-in list (a name not in it stays refused: no known hash)
static const char* g_jitUser[32];
static int g_jitUserN;
static int g_jitGroups = 255;        // [MonoJit] Groups: template mask, as MONOHOST_JITC2
static int g_jitMode;                // 0 off, 1 compiled
static volatile DWORD g_jitSimTid;   // set by the first JitTick; 0 = nothing compiled runs yet
static bool g_jitDead;               // a fault inside the compiler: no more compiles this session
static LONG g_jitLogN;
static CRITICAL_SECTION g_jitCs;
static void* g_jitDomain;            // the transform context's domain, per compile (rm is per domain)
static void* g_jitObjectClass;       // an object[] takes any reference without the class check

static bool JitEndsWith(const char* full, const char* key) {  // "ns.Class.Method" ends in "Class.Method" at a dot
    const size_t fl = strlen(full), l = strlen(key);
    return fl >= l && !strcmp(full + fl - l, key) && (fl == l || full[fl - l - 1] == '.');
}

static const char* JitMethodName(const u8* rm, char* buf, int n) {  // "ns.Class.Method" (nested types: "Class.Method")
    const u8* mm = *(const u8* const*)(rm + 8);
    const u8* k = mm ? *(const u8* const*)(mm + 8) : NULL;
    const char* ns = k ? *(const char* const*)(k + 0x38) : NULL;
    const char* cn = k ? *(const char* const*)(k + 0x34) : NULL;
    const char* mn = mm ? *(const char* const*)(mm + 0x18) : NULL;
    sprintf_s(buf, n, "%s%s%s.%s", ns && *ns ? ns : "", ns && *ns ? "." : "", cn ? cn : "?", mn ? mn : "?");
    return buf;
}
static bool JitListed(const char* full) {
    bool hit = false;
    for (int i = 0; i < g_jitRowN && !hit; i++) hit = JitEndsWith(full, g_jitRows[i].key);
    if (!hit || !g_jitUserN) return hit;
    for (int i = 0; i < g_jitUserN; i++) if (JitEndsWith(full, g_jitUser[i])) return true;
    return false;
}

// instruction length in words at p (superops: the summed base lengths); 0 = cannot walk (switch handled by the caller)
static int JitLen(const unsigned short* p) {
    unsigned op = *p;
    if (op == JIT_OP) return -1;
    if (op <= 0x1A8) return g_jitOpLen[op];
    if (op >= 0x1AB && op <= 0x1C0) {
        const u8* e = g_jitSuper + 0x48 * (op - 0x1AA);
        int m = *(const unsigned short*)(e + 4), len = 0;
        for (int k = 0; k < m; k++) len += g_jitOpLen[((const unsigned short*)(e + 6))[k]];
        return len;
    }
    return -1;
}
static int JitSwitchLen(const unsigned short* p) { return 3 + 2 * (int)*(const DWORD*)(p + 1); }  // op, u32 n, n x i32 offsets

// width code of a MonoType (type byte at +6) as EA's stackval_to_data / stackval_from_data move it; 0 = not compiled (value
// types, generic instances, anything else: the opcode compiles as an exit)
static int JitTypeWidth(const u8* type) {
    switch (type[6]) {
    case 0x04: return 1;                        // I1
    case 0x02: case 0x05: return 2;             // BOOLEAN, U1
    case 0x06: return 3;                        // I2
    case 0x03: case 0x07: return 4;             // CHAR, U2
    case 0x08: case 0x09: case 0x0E: case 0x0F: case 0x12: case 0x14: case 0x18: case 0x19: case 0x1B: case 0x1C: case 0x1D:
        return 5;                               // I4, U4, STRING, PTR, CLASS, ARRAY, I, U, FNPTR, OBJECT, SZARRAY
    case 0x0A: case 0x0B: case 0x0D: return 6;  // I8, U8, R8
    case 0x0C: return 7;                        // R4 (x87 widen on a load, round on a store, as EA's handlers do)
    default: return 0;
    }
}
// Thread / context statics live in the domain's special-static table (domain+0x88, g_hash_table_lookup 0xE45F00) and EA's
// stsfld / ldsfld / ldsflda take the per-thread address for them; such a field compiles as an exit.
static bool JitSpecialStatic(const u8* field) {
    if (!g_jitDomain) return true;
    void* hash = *(void**)((u8*)g_jitDomain + 0x88);
    return hash && ((void*(__cdecl*)(void*, const void*))(uintptr_t)TS3A(0x00E45F00))(hash, field) != NULL;
}
// mono_class_vtable(domain, field->parent) as EA's stsfld and ldsflda call it every execution (0xEA2AD0); one vtable per
// domain and class, so the compiled code carries it as a constant. field+0x14 = parent, field+8 = offset [verified: the
// handlers at 0xE56F52 / 0xE56D20 / 0xE56E1E].
static void* JitStaticVt(const u8* field) {
    if (!g_jitDomain) return NULL;
    return ((void*(__cdecl*)(void*, const void*))(uintptr_t)TS3A(0x00EA2AD0))(g_jitDomain, *(const void* const*)(field + 0x14));
}

// ---- opcode templates ----
enum JitKind : u8 {
    K_EXIT, K_RET, K_LDARG, K_LDTHIS, K_LDARGA, K_LDLOC, K_LDLOCA, K_STLOC, K_STLOCNP, K_STINARG, K_LDFLD, K_STFLD, K_LDC,
    K_LDCS, K_LDCI4, K_DUP, K_POP, K_BRS, K_BR, K_BTS, K_BT, K_BFS, K_BF, K_BCS, K_BC, K_LDLEN, K_LDELEM, K_LDIND, K_STIND,
    K_BIN, K_BIN8, K_MUL, K_INC, K_DEC, K_SHIFT, K_UN, K_CMP, K_CEQ0, K_CONV,
    K_LDSFLD, K_LDSFLDA, K_STSFLD, K_STELEM, K_STELEMREF, K_STOBJ, K_LDFLDA, K_CKNULL, K_STRLEN, K_GETCHR,  // round 10 C2
    K_DIVUN, K_REMR8, K_SWITCH,  // round 10 C2, the throw-only handlers, rem.r8 and switch
    K_LDVT, K_STVT, K_STINARGVT, K_DUPVT, K_VTRESULT, K_CEQ8, K_LDFLDVT, K_LDSFLDVT  // round 10 C1, the value-type moves and ceq.i8 (docs/207 §15)
};
// width: 1 = i1, 2 = u1, 3 = i2, 4 = u2, 5 = 32-bit, 6 = 64-bit, 7 = r4 (x87), 8 = typed by the field or class at compile
// time (JitTypeWidth); x = kind-specific byte (cc, alu op, imm)
struct JitOpInfo { u8 kind, width; int x; };
static JitOpInfo g_opInfo[0x1C1];

// compiled stores log through Satb while a span is on (docs/507 B3); JitEnter keeps the worker out of compiled code
static void __cdecl JitSatbStore(DWORD addr, DWORD n) {
    if (SatbSimSide()) SatbSimRange(addr, n);
}

static const struct { const char* name; u8 kind, width; int x; } kJitTmpl[] = {
    {"ret", K_RET, 0, 0}, {"ret.void", K_RET, 0, 0}, {"ret.vt", K_RET, 0, 0},
    {"ldarg.i1", K_LDARG, 1, 0}, {"ldarg.u1", K_LDARG, 2, 0}, {"ldarg.i2", K_LDARG, 3, 0}, {"ldarg.u2", K_LDARG, 4, 0},
    {"ldarg.i4", K_LDARG, 5, 0}, {"ldarg.o", K_LDARG, 5, 0}, {"ldarg.p", K_LDARG, 5, 0}, {"ldarg.i8", K_LDARG, 6, 0},
    {"ldthis", K_LDTHIS, 0, 0}, {"ldarga", K_LDARGA, 0, 0},
    {"ldloc.i1", K_LDLOC, 1, 0}, {"ldloc.u1", K_LDLOC, 2, 0}, {"ldloc.i2", K_LDLOC, 3, 0}, {"ldloc.u2", K_LDLOC, 4, 0},
    {"ldloc.i4", K_LDLOC, 5, 0}, {"ldloc.o", K_LDLOC, 5, 0}, {"ldloc.p", K_LDLOC, 5, 0}, {"ldloc.i8", K_LDLOC, 6, 0},
    {"ldloca.s", K_LDLOCA, 0, 0},
    {"stloc.i1", K_STLOC, 1, 0}, {"stloc.u1", K_STLOC, 2, 0}, {"stloc.i2", K_STLOC, 3, 0}, {"stloc.u2", K_STLOC, 4, 0},
    {"stloc.i4", K_STLOC, 5, 0}, {"stloc.o", K_STLOC, 5, 0}, {"stloc.p", K_STLOC, 5, 0}, {"stloc.i8", K_STLOC, 6, 0},
    {"stloc.np.i4", K_STLOCNP, 5, 0}, {"stloc.np.o", K_STLOCNP, 5, 0},
    {"stinarg.i1", K_STINARG, 1, 0}, {"stinarg.u1", K_STINARG, 2, 0}, {"stinarg.i2", K_STINARG, 3, 0}, {"stinarg.u2", K_STINARG, 4, 0},
    {"stinarg.i4", K_STINARG, 5, 0}, {"stinarg.o", K_STINARG, 5, 0}, {"stinarg.p", K_STINARG, 5, 0}, {"stinarg.i8", K_STINARG, 6, 0},
    // round 10 C1: the value-type moves; x = 0 an argument (ebx), 1 a local (edi)
    {"ldarg.vt", K_LDVT, 0, 0}, {"ldloc.vt", K_LDVT, 0, 1}, {"starg.vt", K_STVT, 0, 0}, {"stloc.vt", K_STVT, 0, 1},
    {"stinarg.vt", K_STINARGVT, 0, 0}, {"dup.vt", K_DUPVT, 0, 0}, {"vtresult", K_VTRESULT, 0, 0}, {"ldfld.vt", K_LDFLDVT, 0, 0}, {"ldsfld.vt", K_LDSFLDVT, 9, 0},
    {"ldfld.i1", K_LDFLD, 1, 0}, {"ldfld.u1", K_LDFLD, 2, 0}, {"ldfld.i2", K_LDFLD, 3, 0}, {"ldfld.u2", K_LDFLD, 4, 0},
    {"ldfld.i4", K_LDFLD, 5, 0}, {"ldfld.o", K_LDFLD, 5, 0}, {"ldfld.p", K_LDFLD, 5, 0}, {"ldfld.i8", K_LDFLD, 6, 0},
    {"stfld.i1", K_STFLD, 1, 0}, {"stfld.u1", K_STFLD, 2, 0}, {"stfld.i2", K_STFLD, 3, 0}, {"stfld.u2", K_STFLD, 4, 0},
    {"stfld.i4", K_STFLD, 5, 0}, {"stfld.o", K_STFLD, 5, 0}, {"stfld.p", K_STFLD, 5, 0}, {"stfld.i8", K_STFLD, 6, 0},
    {"ldnull", K_LDC, 0, 0}, {"ldc.i4.m1", K_LDC, 0, -1}, {"ldc.i4.0", K_LDC, 0, 0}, {"ldc.i4.1", K_LDC, 0, 1},
    {"ldc.i4.2", K_LDC, 0, 2}, {"ldc.i4.3", K_LDC, 0, 3}, {"ldc.i4.4", K_LDC, 0, 4}, {"ldc.i4.5", K_LDC, 0, 5},
    {"ldc.i4.6", K_LDC, 0, 6}, {"ldc.i4.7", K_LDC, 0, 7}, {"ldc.i4.8", K_LDC, 0, 8}, {"ldc.i4.s", K_LDCS, 0, 0}, {"ldc.i4", K_LDCI4, 0, 0},
    {"dup", K_DUP, 0, 0}, {"pop", K_POP, 0, 0}, {"br.s", K_BRS, 0, 0}, {"br", K_BR, 0, 0},
    {"brtrue.i4.s", K_BTS, 0, 0}, {"brtrue.i4", K_BT, 0, 0}, {"brfalse.i4.s", K_BFS, 0, 0}, {"brfalse.i4", K_BF, 0, 0},
    // jcc second byte (0F 8x): je 84, jne 85, jl 8C, jle 8E, jg 8F, jge 8D, jb 82, jbe 86, ja 87, jae 83
    {"beq.i4.s", K_BCS, 0, 0x84}, {"bne.un.i4.s", K_BCS, 0, 0x85}, {"blt.i4.s", K_BCS, 0, 0x8C}, {"ble.i4.s", K_BCS, 0, 0x8E},
    {"bgt.i4.s", K_BCS, 0, 0x8F}, {"bge.i4.s", K_BCS, 0, 0x8D}, {"blt.un.i4.s", K_BCS, 0, 0x82}, {"ble.un.i4.s", K_BCS, 0, 0x86},
    {"bgt.un.i4.s", K_BCS, 0, 0x87}, {"bge.un.i4.s", K_BCS, 0, 0x83},
    {"beq.i4", K_BC, 0, 0x84}, {"bne.un.i4", K_BC, 0, 0x85}, {"blt.i4", K_BC, 0, 0x8C}, {"ble.i4", K_BC, 0, 0x8E},
    {"bgt.i4", K_BC, 0, 0x8F}, {"bge.i4", K_BC, 0, 0x8D}, {"blt.un.i4", K_BC, 0, 0x82}, {"ble.un.i4", K_BC, 0, 0x86},
    {"bgt.un.i4", K_BC, 0, 0x87}, {"bge.un.i4", K_BC, 0, 0x83},
    {"ldlen", K_LDLEN, 0, 0},
    {"ldelem.i1", K_LDELEM, 1, 0}, {"ldelem.u1", K_LDELEM, 2, 0}, {"ldelem.i2", K_LDELEM, 3, 0}, {"ldelem.u2", K_LDELEM, 4, 0},
    {"ldelem.i4", K_LDELEM, 5, 0}, {"ldelem.u4", K_LDELEM, 5, 0}, {"ldelem.i", K_LDELEM, 5, 0}, {"ldelem.ref", K_LDELEM, 5, 0},
    {"ldelem.i8", K_LDELEM, 6, 0},
    {"ldind.i1", K_LDIND, 1, 0}, {"ldind.u1", K_LDIND, 2, 0}, {"ldind.i2", K_LDIND, 3, 0}, {"ldind.u2", K_LDIND, 4, 0},
    {"ldind.i4", K_LDIND, 5, 0}, {"ldind.u4", K_LDIND, 5, 0}, {"ldind.i", K_LDIND, 5, 0}, {"ldind.ref", K_LDIND, 5, 0},
    {"ldind.i8", K_LDIND, 6, 0},
    {"stind.i1", K_STIND, 1, 0}, {"stind.i2", K_STIND, 3, 0}, {"stind.i4", K_STIND, 5, 0}, {"stind.i", K_STIND, 5, 0},
    {"stind.ref", K_STIND, 5, 0}, {"stind.i8", K_STIND, 6, 0},
    // round 10 C2 (docs/207 §13): float fields, field addresses, static fields, stobj, the array and string stores
    {"ldfld.r4", K_LDFLD, 7, 0}, {"ldfld.r8", K_LDFLD, 6, 0}, {"stfld.r4", K_STFLD, 7, 0}, {"stfld.r8", K_STFLD, 6, 0}, {"ldflda", K_LDFLDA, 0, 0},
    {"ldsfld", K_LDSFLD, 8, 0}, {"ldsfld.i4", K_LDSFLD, 5, 0}, {"ldsfld.o", K_LDSFLD, 5, 0}, {"ldsflda", K_LDSFLDA, 0, 0}, {"stsfld", K_STSFLD, 8, 0},
    {"ldelem.r4", K_LDELEM, 7, 0}, {"ldelem.r8", K_LDELEM, 6, 0},
    {"stelem.i", K_STELEM, 5, 0}, {"stelem.i1", K_STELEM, 1, 0}, {"stelem.i2", K_STELEM, 3, 0}, {"stelem.i4", K_STELEM, 5, 0},
    {"stelem.i8", K_STELEM, 6, 0}, {"stelem.r4", K_STELEM, 7, 0}, {"stelem.r8", K_STELEM, 6, 0}, {"stelem.ref", K_STELEMREF, 5, 0},
    {"stobj", K_STOBJ, 8, 0}, {"cknull", K_CKNULL, 0, 0}, {"strlen", K_STRLEN, 0, 0}, {"getchr", K_GETCHR, 0, 0},
    {"div.un.i4", K_DIVUN, 0, 0}, {"rem.un.i4", K_DIVUN, 0, 1}, {"rem.r8", K_REMR8, 0, 0}, {"switch", K_SWITCH, 0, 0},
    // ALU r/m32, r32 opcodes: add 01, or 09, and 21, sub 29, xor 31 (i8: low op, high op adc 11 / sbb 19)
    {"add.i4", K_BIN, 0, 0x01}, {"sub.i4", K_BIN, 0, 0x29}, {"and.i4", K_BIN, 0, 0x21}, {"or.i4", K_BIN, 0, 0x09}, {"xor.i4", K_BIN, 0, 0x31},
    {"add.i8", K_BIN8, 0, 0x1101}, {"sub.i8", K_BIN8, 0, 0x1929}, {"and.i8", K_BIN8, 0, 0x2121}, {"or.i8", K_BIN8, 0, 0x0909},
    {"xor.i8", K_BIN8, 0, 0x3131}, {"mul.i4", K_MUL, 0, 0}, {"add1.i4", K_INC, 0, 0}, {"sub1.i4", K_DEC, 0, 0},
    {"shl.i4", K_SHIFT, 0, 4}, {"shr.i4", K_SHIFT, 0, 7}, {"shr.un.i4", K_SHIFT, 0, 5}, {"neg.i4", K_UN, 0, 3}, {"not.i4", K_UN, 0, 2},
    // setcc second byte (0F 9x): sete 94, setg 9F, setl 9C, seta 97, setb 92
    {"ceq.i4", K_CMP, 0, 0x94}, {"ceq.i8", K_CEQ8, 0, 0}, {"cgt.i4", K_CMP, 0, 0x9F}, {"clt.i4", K_CMP, 0, 0x9C}, {"cgt.un.i4", K_CMP, 0, 0x97},
    {"clt.un.i4", K_CMP, 0, 0x92}, {"ceq0.i4", K_CEQ0, 0, 0},
    {"conv.i8.i4", K_CONV, 0, 1}, {"conv.u8.i4", K_CONV, 0, 2}, {"conv.i4.i8", K_CONV, 0, 3}, {"conv.i1.i4", K_CONV, 0, 4},
    {"conv.u1.i4", K_CONV, 0, 5}, {"conv.i2.i4", K_CONV, 0, 6}, {"conv.u2.i4", K_CONV, 0, 7},
};

// ---- emitter: every memory operand is [base + disp32] (or [eax + ecx*scale + disp32]); registers by x86 number ----
enum { EAX, ECX, EDX, EBX, ESP_, EBP, ESI, EDI };
struct Em {
    u8* p;
    void b(int v) { *p++ = (u8)v; }
    void d(DWORD v) { memcpy(p, &v, 4); p += 4; }
    void m(int reg, int base, int disp) { b(0x80 | (reg << 3) | base); d((DWORD)disp); }  // mod=10
    void op(int o, int reg, int base, int disp) { b(o); m(reg, base, disp); }
    void op2(int o, int reg, int base, int disp) { b(0x0F); b(o); m(reg, base, disp); }
    void opEsp(int o, int reg, int disp) { b(o); b(0x84 | (reg << 3)); b(0x24); d((DWORD)disp); }  // o reg, [esp+disp32]
    void rr(int o, int dst, int src) { b(o); b(0xC0 | (src << 3) | dst); }  // o r/m, r with mod=11 (r/m = dst)
    void movRI(int r, DWORD v) { b(0xB8 + r); d(v); }
    void addEsi(int v) { b(0x83); b(v >= 0 ? 0xC6 : 0xEE); b(v >= 0 ? v : -v); }
    // load a value of width w from [base+disp] into r (zero or sign extended to 32 bits)
    void load(int w, int r, int base, int disp) {
        if (w == 1) op2(0xBE, r, base, disp); else if (w == 2) op2(0xB6, r, base, disp);
        else if (w == 3) op2(0xBF, r, base, disp); else if (w == 4) op2(0xB7, r, base, disp); else op(0x8B, r, base, disp);
    }
    void store(int w, int r, int base, int disp) {  // store the low 8/16/32 bits of r (r = eax/ecx/edx/ebx for 8-bit)
        if (w <= 2) op(0x88, r, base, disp); else if (w <= 4) { b(0x66); op(0x89, r, base, disp); } else op(0x89, r, base, disp);
    }
    // x87 for the r4 forms: EA widens a float with fld dword / fstp qword and rounds a double to a float with fld qword / fstp dword
    void fld4(int base, int disp) { op(0xD9, 0, base, disp); }
    void fld8(int base, int disp) { op(0xDD, 0, base, disp); }
    void fstp4(int base, int disp) { op(0xD9, 3, base, disp); }
    void fstp8(int base, int disp) { op(0xDD, 3, base, disp); }
    // [vt+0x18] bit 1: the class is initialised; clear = EA's handler runs the class constructor (the exit re-runs the instruction)
    u8* classInitExit(DWORD vt) { b(0xF6); b(0x05); d(vt + 0x18); b(2); return jcc(0x84); }
    u8* jcc(int cc) { b(0x0F); b(cc); d(0); return p - 4; }
    u8* jmp() { b(0xE9); d(0); return p - 4; }
    void jmpTo(const void* t) { b(0xE9); d((DWORD)(uintptr_t)t - ((DWORD)(uintptr_t)p + 4)); }
    void incAbs(volatile LONG* c) { b(0xFF); b(0x05); d((DWORD)(uintptr_t)c); }
    // Satb barrier before a heap store of width w at [base+disp] (docs/507 B3): outside a span one cmp and a taken jump;
    // inside, JitSatbStore(address, bytes). base survives (push/pop); eax, ecx, edx are scratch at every emitting site.
    void satb(int w, int base, int disp) {
        b(0x83); b(0x3D); d((DWORD)(uintptr_t)&g_satbOn); b(0);  // cmp dword ptr [g_satbOn], 0
        b(0x74); u8* skip = p; b(0);                                 // je skip
        b(0x50 + base); op(0x8D, EDX, base, disp);                   // push base; lea edx, [base+disp]
        b(0x6A); b(w <= 2 ? 1 : w <= 4 ? 2 : w == 5 || w == 7 ? 4 : 8); b(0x52);  // push bytes; push edx
        b(0xE8); d((DWORD)(uintptr_t)JitSatbStore - ((DWORD)(uintptr_t)p + 4));  // call JitSatbStore
        b(0x83); b(0xC4); b(8); b(0x58 + base);                      // add esp, 8; pop base
        *skip = (u8)(p - (skip + 1));
    }
};
static void Fix(u8* rel, const u8* target) { *(int*)rel = (int)(target - (rel + 4)); }

// ---- the region and its tables (one allocation at install, RW; code pages flip to RX per method) ----
struct JitMeth;
struct JitEnt { DWORD ip; u8* native; DWORD orig; JitMeth* m; };
static_assert(sizeof(JitEnt) == 16 && offsetof(JitEnt, native) == 4 && offsetof(JitEnt, orig) == 8, "JitEnter reads these offsets");
static const DWORD JIT_TAB_N = 1 << 15;  // JitEnter's mask
static const int JIT_METH_N = 2048, JIT_POS_N = 24576, JIT_MAX_POS = 4000, JIT_MAX_WORDS = 16384, JIT_CODE_BYTES = 2 << 20;
struct JitMeth { u8* rm; unsigned short* code; int words; int pos0, posN; char name[96]; int nativeOps, exitOps, bytes; u8* native0; };
struct JitFix { u8* rel; int targetW; bool abs; };  // abs: a switch table slot takes the label itself
struct JitScratch { int posW[JIT_MAX_POS + 64]; unsigned short posOp[JIT_MAX_POS + 64]; u8* label[JIT_MAX_POS + 64]; JitFix fix[JIT_MAX_POS + 64]; int wordToPos[JIT_MAX_WORDS]; };
static JitEnt* g_jitTab;                 // JIT_TAB_N entries + a 4096-entry overflow tail (no wrap in the probe loop)
static JitMeth* g_jitMeth;
static int g_jitMethN;
static JitScratch* g_js;                 // the compiler's work area (compiles run under g_jitCs)
static unsigned short* g_jitPos;         // positions of all compiled methods (word offsets), [pos0, posN) per method
static unsigned short* g_jitOrigW;
static int g_jitPosN;
static u8* g_jitCode;
static u8* g_jitCodeEnd;
static size_t g_jitCodeUsed;
static bool g_jitPatched;

static JitEnt* JitAdd(DWORD ip) {
    DWORD i = (ip >> 1) & (JIT_TAB_N - 1);
    while (g_jitTab[i].ip && g_jitTab[i].ip != ip) i++;
    if (i >= JIT_TAB_N + 4000) return NULL;
    g_jitTab[i].ip = ip;
    return &g_jitTab[i];
}
static void JitProt(u8* p, size_t n, DWORD prot) {
    DWORD old;
    VirtualProtect(p, n, prot, &old);
}

// exit to the live handler of base opcode `op` at ip (state is exactly the interpreter's: esi, frame memory)
static void EmitExit(Em& e, const unsigned short* ip, DWORD op) {
    e.incAbs(&ts3native_counters.jitExits);
    e.b(0xBF); e.d((DWORD)(uintptr_t)ip);   // mov edi, ip
    e.op(0x89, EDI, EBP, -4);                // mov [ebp-4], edi
    e.movRI(EAX, op);
    e.rr(0x8B, EAX, EDX);                    // mov edx, eax (rr: r/m operand first, reg operand second)
    e.b(0xFF); e.b(0x25); e.d((DWORD)(uintptr_t)(g_jitTableP + op));  // jmp dword ptr [table + op*4]: the live entry (Satb's stub in a span)
}
// the value-type moves' pieces: the vt cursor [ebp-0xC], a memcpy through EA's CRT thunk (cdecl: eax, ecx, edx go)
static void VtLoad(Em& e, int r) { e.op(0x8B, r, EBP, -0xC); }
static void VtStore(Em& e, int r) { e.op(0x89, r, EBP, -0xC); }
static void VtAdd(Em& e, int n) {  // add dword [cursor], n
    if (!n) return;
    e.b(0x81); e.m(0, EBP, -0xC);
    e.d((DWORD)n);
}
static void VtCopy(Em& e, int dst, int src, DWORD n) {  // memcpy(dst, src, n): push n; push src; push dst; call thunk; add esp, 12
    e.b(0x68); e.d(n); e.b(0x50 + src); e.b(0x50 + dst);
    e.b(0xE8); e.d(g_jitMemcpy - ((DWORD)(uintptr_t)e.p + 4));
    e.b(0x83); e.b(0xC4); e.b(12);
}
static void JitPatchMeth(const JitMeth& m, bool on);

// the word a compiled position gets: JIT_OP, except leave.s. EA's leave / leave.s handler 0xE59315 re-reads [edi] to pick
// the operand size (0xE5933A cmp word ptr [edi], 0x9C): with JIT_OP there it reads leave.s's 16-bit offset plus the next
// word as a 32-bit offset and jumps to a wild ip. leave.s always exits to that handler, so its own word can stay.
static unsigned short JitWord(unsigned short orig) { return orig == 0x9C ? orig : (unsigned short)JIT_OP; }
// compile one method: returns why not (and leaves the code alone) if anything does not fit, else NULL
static const char* JitCompile(u8* rm) {
    if (g_jitMethN >= JIT_METH_N) return "method table full";
    if (rm[0x41]) return "exception clauses";
    unsigned short* code = *(unsigned short**)rm;
    int words = *(unsigned short*)(rm + 0x3A);
    if (words >= JIT_MAX_WORDS) return "too many mint words";
    // 1. positions: every instruction start, superops expanded into their base instructions
    int* const posW = g_js->posW;
    unsigned short* const posOp = g_js->posOp;
    int n = 0;
    for (int i = 0; i < words;) {
        unsigned op = code[i];
        if (n >= JIT_MAX_POS) return "too many positions";
        if (op == 0xE8) {  // switch: compiled as an exit (variable length)
            posW[n] = i, posOp[n++] = 0xE8;
            i += JitSwitchLen(code + i);
            continue;
        }
        int len = JitLen(code + i);
        if (len <= 0) return "cannot walk the mint code";
        if (op >= 0x1AB) {
            const u8* d = g_jitSuper + 0x48 * (op - 0x1AA);
            int m = *(const unsigned short*)(d + 4), at = i;
            for (int k = 0; k < m; k++) {
                unsigned short b = ((const unsigned short*)(d + 6))[k];
                if (k && code[at] != b) return "a superop's inner word differs";
                posW[n] = at, posOp[n++] = b;
                at += g_jitOpLen[b];
            }
        } else
            posW[n] = i, posOp[n++] = (unsigned short)op;
        i += len;
    }
    // 2. emit (bound: 256 bytes per position, as the host's pool check; a switch's table is 2 bytes per mint word it takes)
    const size_t need = 256 * (size_t)n + 2 * (size_t)words + 512;
    if ((size_t)(g_jitCodeEnd - (g_jitCode + g_jitCodeUsed)) < need) return "code region full";
    u8* const start = g_jitCode + g_jitCodeUsed;
    JitProt(start, need, PAGE_READWRITE);  // from the end of the last method on: no finished code lives here (its last page flips below)
    u8** const label = g_js->label;
    JitFix* const fix = g_js->fix;
    int* const wordToPos = g_js->wordToPos;
    for (int k = 0; k < words; k++) wordToPos[k] = -1;
    for (int k = 0; k < n; k++) wordToPos[posW[k]] = k;
    int nf = 0, nat = 0, ex = 0;
    Em e = {start};
    const char* why = NULL;
    const DWORD* argOff = *(const DWORD* const*)(rm + 0x1C);
    const DWORD* items = *(const DWORD* const*)(rm + 0x14);
    for (int k = 0; k < n; k++) {
        label[k] = e.p;
        const unsigned short* ip = code + posW[k];
        DWORD op = posOp[k];
        JitOpInfo t = op < 0x1C1 ? g_opInfo[op] : JitOpInfo{K_EXIT, 0, 0};
        int w = t.width, o16 = (short)ip[1];
        unsigned u16 = ip[1];
        u8* exitRel[3] = {NULL, NULL, NULL};
        // round 10 C2: the static-field and stobj kinds resolve their item now (field, vtable, width); anything they
        // cannot carry as a constant (no domain yet, a thread static, a value type, an unknown type) compiles as an exit
        DWORD sVt = 0; int sOff = 0;
        if (t.kind == K_LDSFLD || t.kind == K_LDSFLDA || t.kind == K_STSFLD || t.kind == K_STOBJ || t.kind == K_LDSFLDVT) {
            const u8* item = (const u8*)(uintptr_t)items[u16];
            if (t.kind == K_STOBJ) w = item ? JitTypeWidth(item + 0xA0) : 0;  // MonoClass+0xA0 = byval_arg (docs/213)
            else if (item) {
                if (w == 8) w = JitTypeWidth(*(const u8* const*)item);
                sOff = *(const int*)(item + 8);
                const bool fast = op == 0x067 || op == 0x068;  // ldsfld.i4 / ldsfld.o carry the vtable and skip the special-static lookup
                sVt = op == 0x066 || fast ? items[ip[2]] : (DWORD)(uintptr_t)JitStaticVt(item);
                if (!sVt || sOff < 0 || (!fast && JitSpecialStatic(item))) w = 0;
            } else w = 0;
            if (!w) t.kind = K_EXIT;
        }
        switch (t.kind) {
        case K_LDARG:  // [esi] = arena[u16] (i8: two words)
            e.load(w == 6 ? 5 : w, EAX, EBX, u16); e.op(0x89, EAX, ESI, 0);
            if (w == 6) { e.op(0x8B, ECX, EBX, u16 + 4); e.op(0x89, ECX, ESI, 4); }
            e.addEsi(8); break;
        case K_LDTHIS:  // MonoInvocation +0xC
            e.op(0x8B, EAX, EBP, -8); e.op(0x8B, EAX, EAX, 0xC);
            e.op(0x89, EAX, ESI, 0); e.addEsi(8); break;
        case K_LDARGA: e.op(0x8D, EAX, EBX, u16); e.op(0x89, EAX, ESI, 0); e.addEsi(8); break;
        case K_LDLOC:
            e.load(w == 6 ? 5 : w, EAX, EDI, u16); e.op(0x89, EAX, ESI, 0);
            if (w == 6) { e.op(0x8B, ECX, EDI, u16 + 4); e.op(0x89, ECX, ESI, 4); }
            e.addEsi(8); break;
        case K_LDLOCA: e.op(0x8D, EAX, EDI, u16); e.op(0x89, EAX, ESI, 0); e.addEsi(8); break;
        case K_STLOC: case K_STLOCNP:
            e.op(0x8B, EAX, ESI, -8);
            if (w == 6) { e.op(0x8B, ECX, ESI, -4); e.op(0x89, ECX, EDI, u16 + 4); }
            e.store(w, EAX, EDI, u16);
            if (t.kind == K_STLOC) e.addEsi(-8);
            break;
        case K_STINARG: {  // arena[argOff[i]] = stackargs[i] (offset constant per method)
            int off = (int)argOff[u16];
            e.op(0x8B, ECX, EBP, -8); e.op(0x8B, ECX, ECX, 0x14);
            e.op(0x8B, EAX, ECX, u16 * 8); e.store(w, EAX, EBX, off);
            if (w == 6) { e.op(0x8B, EDX, ECX, u16 * 8 + 4); e.op(0x89, EDX, EBX, off + 4); }
            break;
        }
        case K_LDFLD:
            e.op(0x8B, EAX, ESI, -8); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            if (w == 7) { e.fld4(EAX, u16); e.fstp8(ESI, -8); break; }
            if (w == 6) { e.op(0x8B, ECX, EAX, u16 + 4); e.op(0x89, ECX, ESI, -4); }
            e.load(w == 6 ? 5 : w, EAX, EAX, u16); e.op(0x89, EAX, ESI, -8); break;
        case K_STFLD:
            e.op(0x8B, EAX, ESI, -16); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            e.satb(w, EAX, u16);
            if (w == 7) { e.fld8(ESI, -8); e.fstp4(EAX, u16); e.addEsi(-16); break; }
            e.op(0x8B, ECX, ESI, -8); e.store(w, ECX, EAX, u16);
            if (w == 6) { e.op(0x8B, EDX, ESI, -4); e.op(0x89, EDX, EAX, u16 + 4); }
            e.addEsi(-16); break;
        case K_LDFLDA:
            e.op(0x8B, EAX, ESI, -8); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            e.op(0x8D, EAX, EAX, u16); e.op(0x89, EAX, ESI, -8); break;
        case K_LDSFLD:  // value at [vt+0xC] (the class's static data) + the field offset
            exitRel[0] = e.classInitExit(sVt);
            e.b(0xA1); e.d(sVt + 0xC);  // mov eax, [vt+0xC]
            if (w == 7) { e.fld4(EAX, sOff); e.fstp8(ESI, 0); }
            else {
                if (w == 6) { e.op(0x8B, ECX, EAX, sOff + 4); e.op(0x89, ECX, ESI, 4); }
                e.load(w == 6 ? 5 : w, EAX, EAX, sOff); e.op(0x89, EAX, ESI, 0);
            }
            e.addEsi(8); break;
        case K_LDSFLDA:
            exitRel[0] = e.classInitExit(sVt);
            e.b(0xA1); e.d(sVt + 0xC); e.op(0x8D, EAX, EAX, sOff); e.op(0x89, EAX, ESI, 0); e.addEsi(8); break;
        case K_STSFLD:
            exitRel[0] = e.classInitExit(sVt);
            e.b(0xA1); e.d(sVt + 0xC); e.satb(w, EAX, sOff);
            if (w == 7) { e.fld8(ESI, -8); e.fstp4(EAX, sOff); }
            else {
                e.op(0x8B, ECX, ESI, -8); e.store(w, ECX, EAX, sOff);
                if (w == 6) { e.op(0x8B, ECX, ESI, -4); e.op(0x89, ECX, EAX, sOff + 4); }
            }
            e.addEsi(-8); break;
        case K_STOBJ:  // address [esi-16], value [esi-8]; EA has no null check (the handler faults), a null exits so the fault stays EA's
            e.op(0x8B, EAX, ESI, -16); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            e.satb(w, EAX, 0);
            if (w == 7) { e.fld8(ESI, -8); e.fstp4(EAX, 0); }
            else {
                e.op(0x8B, ECX, ESI, -8); e.store(w, ECX, EAX, 0);
                if (w == 6) { e.op(0x8B, ECX, ESI, -4); e.op(0x89, ECX, EAX, 4); }
            }
            e.addEsi(-16); break;
        case K_STELEM: case K_STELEMREF: {  // array [esi-24], index [esi-16], value [esi-8]; null and bounds exit before any change
            int sz = w <= 2 ? 1 : w <= 4 ? 2 : w == 5 || w == 7 ? 4 : 8, sc = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3;
            e.op(0x8B, EAX, ESI, -24); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            e.op(0x8B, ECX, ESI, -16); e.op(0x3B, ECX, EAX, 8); exitRel[1] = e.jcc(0x83);
            if (t.kind == K_STELEMREF) {  // a null, a value of exactly the element class (array klass+8), or any value into an
                u8* ok[2];                // object[] stores; the rest (EA: mono_object_isinst, ArrayTypeMismatchException) exits
                e.op(0x8B, EDX, ESI, -8); e.rr(0x85, EDX, EDX); ok[0] = e.jcc(0x84);
                e.op(0x8B, EDX, EDX, 0); e.op(0x8B, EDX, EDX, 0);  // the value's class
                e.op(0x8B, ECX, EAX, 0); e.op(0x8B, ECX, ECX, 0);  // the array's class
                e.op(0x3B, EDX, ECX, 8); ok[1] = e.jcc(0x84);
                e.op(0x81, 7, ECX, 8); e.d((DWORD)(uintptr_t)g_jitObjectClass); exitRel[2] = e.jcc(0x85);  // cmp [klass+8], object_class
                Fix(ok[0], e.p); Fix(ok[1], e.p);
                e.op(0x8B, ECX, ESI, -16);
            }
            e.b(0x8D); e.b(0x84); e.b((sc << 6) | (ECX << 3) | EAX); e.d(0x10);  // lea eax, [eax + ecx*sz + 0x10]
            e.satb(w, EAX, 0);
            if (w == 7) { e.fld8(ESI, -8); e.fstp4(EAX, 0); }
            else {
                e.op(0x8B, EDX, ESI, -8); e.store(w, EDX, EAX, 0);
                if (w == 6) { e.op(0x8B, EDX, ESI, -4); e.op(0x89, EDX, EAX, 4); }
            }
            e.addEsi(-24); break;
        }
        case K_CKNULL: e.b(0x83); e.m(7, ESI, -8); e.b(0); exitRel[0] = e.jcc(0x84); break;  // cmp dword [esi-8], 0
        case K_STRLEN:  // EA strings: length at +4, chars at +8 (4-byte object header, docs/130 §2)
            e.op(0x8B, EAX, ESI, -8); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            e.op(0x8B, EAX, EAX, 4); e.op(0x89, EAX, ESI, -8); break;
        case K_GETCHR:  // string [esi-16], index [esi-8]; one unsigned compare refuses what EA's signed pair refuses
            e.op(0x8B, EAX, ESI, -16); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            e.op(0x8B, ECX, ESI, -8); e.op(0x3B, ECX, EAX, 4); exitRel[1] = e.jcc(0x83);
            e.b(0x0F); e.b(0xB7); e.b(0x84); e.b((1 << 6) | (ECX << 3) | EAX); e.d(8);  // movzx eax, word [eax + ecx*2 + 8]
            e.op(0x89, EAX, ESI, -16); e.addEsi(-8); break;
        case K_LDC: e.b(0xC7); e.m(0, ESI, 0); e.d((DWORD)t.x); e.addEsi(8); break;
        case K_LDCS: e.b(0xC7); e.m(0, ESI, 0); e.d((DWORD)o16); e.addEsi(8); break;
        case K_LDCI4: e.b(0xC7); e.m(0, ESI, 0); e.d(*(const DWORD*)(ip + 1)); e.addEsi(8); break;
        case K_DUP: e.op(0x8B, ECX, ESI, -8); e.op(0x8B, EDX, ESI, -4); e.op(0x89, ECX, ESI, 0); e.op(0x89, EDX, ESI, 4); e.addEsi(8); break;
        case K_POP: e.addEsi(-8); break;
        case K_BRS: case K_BR: {
            int tw = posW[k] + (t.kind == K_BRS ? o16 : *(const int*)(ip + 1));
            fix[nf].rel = e.jmp(), fix[nf].abs = false, fix[nf++].targetW = tw; break;
        }
        case K_BTS: case K_BT: case K_BFS: case K_BF: {
            int tw = posW[k] + (t.kind == K_BTS || t.kind == K_BFS ? o16 : *(const int*)(ip + 1));
            e.addEsi(-8); e.b(0x83); e.m(7, ESI, 0); e.b(0);  // cmp dword [esi], 0
            fix[nf].rel = e.jcc(t.kind == K_BTS || t.kind == K_BT ? 0x85 : 0x84), fix[nf].abs = false, fix[nf++].targetW = tw; break;
        }
        case K_BCS: case K_BC: {
            int tw = posW[k] + (t.kind == K_BCS ? o16 : *(const int*)(ip + 1));
            e.op(0x8B, EAX, ESI, -16); e.addEsi(-16); e.op(0x3B, EAX, ESI, 8);
            fix[nf].rel = e.jcc(t.x), fix[nf].abs = false, fix[nf++].targetW = tw; break;
        }
        case K_LDLEN: e.op(0x8B, EAX, ESI, -8); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84); e.op(0x8B, EAX, EAX, 8); e.op(0x89, EAX, ESI, -8); break;
        case K_LDELEM: {  // null and bounds checks exit before any change; element at data + 0x10
            int sz = w <= 2 ? 1 : w <= 4 ? 2 : w == 5 || w == 7 ? 4 : 8, sc = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3;
            e.op(0x8B, EAX, ESI, -16); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            e.op(0x8B, ECX, ESI, -8); e.op(0x3B, ECX, EAX, 8); exitRel[1] = e.jcc(0x83);
            // lea eax, [eax + ecx*sz + 0x10]
            e.b(0x8D); e.b(0x84); e.b((sc << 6) | (ECX << 3) | EAX); e.d(0x10);
            if (w == 7) { e.fld4(EAX, 0); e.fstp8(ESI, -16); e.addEsi(-8); break; }
            if (w == 6) { e.op(0x8B, EDX, EAX, 4); e.op(0x89, EDX, ESI, -12); }
            e.load(w == 6 ? 5 : w, EDX, EAX, 0); e.op(0x89, EDX, ESI, -16); e.addEsi(-8); break;
        }
        case K_LDIND:  // EA does not check; a null exits so the fault, if any, stays in EA's handler
            e.op(0x8B, EAX, ESI, -8); e.rr(0x85, EAX, EAX); exitRel[0] = e.jcc(0x84);
            if (w == 6) { e.op(0x8B, ECX, EAX, 4); e.op(0x89, ECX, ESI, -4); }
            e.load(w == 6 ? 5 : w, EAX, EAX, 0); e.op(0x89, EAX, ESI, -8); break;
        case K_STIND:
            e.op(0x8B, ECX, ESI, -16); e.rr(0x85, ECX, ECX); exitRel[0] = e.jcc(0x84);
            e.satb(w, ECX, 0);
            e.op(0x8B, EDX, ESI, -8); e.store(w, EDX, ECX, 0);
            if (w == 6) { e.op(0x8B, EDX, ESI, -4); e.op(0x89, EDX, ECX, 4); }
            e.addEsi(-16); break;
        case K_BIN: e.op(0x8B, ECX, ESI, -8); e.op(t.x, ECX, ESI, -16); e.addEsi(-8); break;
        case K_BIN8:
            e.op(0x8B, EAX, ESI, -8); e.op(0x8B, ECX, ESI, -4);
            e.op(t.x & 0xFF, EAX, ESI, -16); e.op(t.x >> 8, ECX, ESI, -12); e.addEsi(-8); break;
        case K_MUL: e.op(0x8B, ECX, ESI, -16); e.op2(0xAF, ECX, ESI, -8); e.op(0x89, ECX, ESI, -16); e.addEsi(-8); break;
        case K_INC: e.b(0x83); e.m(0, ESI, -8); e.b(1); break;
        case K_DEC: e.b(0x83); e.m(5, ESI, -8); e.b(1); break;
        case K_SHIFT: e.op(0x8B, ECX, ESI, -8); e.op(0xD3, t.x, ESI, -16); e.addEsi(-8); break;
        case K_UN: e.op(0xF7, t.x, ESI, -8); break;
        case K_DIVUN:  // dividend [esi-16], divisor [esi-8]; a zero divisor exits before any change (EA's handler throws there)
            e.op(0x8B, ECX, ESI, -8); e.rr(0x85, ECX, ECX); exitRel[0] = e.jcc(0x84);
            e.op(0x8B, EAX, ESI, -16); e.rr(0x31, EDX, EDX); e.b(0xF7); e.b(0xF1);  // xor edx, edx; div ecx
            e.op(0x89, t.x ? EDX : EAX, ESI, -16); e.addEsi(-8); break;
        case K_REMR8:  // x [esi-16], y [esi-8]: fld x; fld y; call the CRT _CIfmod as EA's handler at 0xE55C65 does; the result replaces x
            e.fld8(ESI, -16); e.fld8(ESI, -8); e.b(0xE8); e.d(g_jitFmod - ((DWORD)(uintptr_t)e.p + 4)); e.fstp8(ESI, -16); e.addEsi(-8); break;
        case K_SWITCH: {  // value [esi-8]; u32 n at ip+1, then n i32 word offsets from the end of the instruction (EA's 0xE558B1);
            const int cnt = *(const int*)(ip + 1);  // out of range falls through
            if (cnt < 0 || cnt > 1024 || nf + cnt > 4000) { EmitExit(e, ip, op); ex++; continue; }
            e.op(0x8B, EAX, ESI, -8); e.addEsi(-8);
            e.b(0x3D); e.d((DWORD)cnt);  // cmp eax, n
            u8* over = e.jcc(0x83);  // jae: next instruction
            e.b(0xFF); e.b(0x24); e.b(0x85); u8* tabRef = e.p; e.d(0);  // jmp dword [eax*4 + table]
            const u8* table = e.p; memcpy(tabRef, &table, 4);
            const int endW = posW[k] + 3 + 2 * cnt;
            for (int c = 0; c < cnt; c++) { fix[nf].rel = e.p, fix[nf].abs = true, fix[nf++].targetW = endW + ((const int*)(ip + 3))[c]; e.d(0); }
            Fix(over, e.p); break;
        }
        // round 10 C1: the value-type moves, EA's handlers ported (docs/213 §3, docs/207 §15). The vt stack is the frame's own
        // area; a slot that holds a value type holds a pointer into it; sizes round up to 8 on the cursor, not in the copy.
        case K_LDVT: {  // ldarg.vt / ldloc.vt: [esi] = cursor; copy size (words 2-3) bytes there; cursor += align8
            const DWORD sz = *(const DWORD*)(ip + 2);
            VtLoad(e, EAX); e.op(0x89, EAX, ESI, 0); e.op(0x8D, ECX, t.x ? EDI : EBX, u16);
            VtCopy(e, EAX, ECX, sz); VtAdd(e, (int)((sz + 7) & ~7u)); e.addEsi(8); break;
        }
        case K_STVT: {  // starg.vt / stloc.vt: the pointed-at bytes go to the argument / local; cursor -= align8 (EA's tail 0xE56C21)
            const DWORD sz = *(const DWORD*)(ip + 2);
            e.op(0x8B, ECX, ESI, -8); e.op(0x8D, EDX, t.x ? EDI : EBX, u16);
            VtCopy(e, EDX, ECX, sz); VtAdd(e, -(int)((sz + 7) & ~7u)); e.addEsi(-8); break;
        }
        case K_STINARGVT: {  // arena[argOff[i]] = *(stackargs[i].p), size bytes; the caller's copy stays on its vt stack
            const DWORD sz = *(const DWORD*)(ip + 2);
            e.op(0x8B, ECX, EBP, -8); e.op(0x8B, ECX, ECX, 0x14);
            e.op(0x8B, ECX, ECX, u16 * 8); e.op(0x8D, EDX, EBX, (int)argOff[u16]);
            VtCopy(e, EDX, ECX, sz); break;
        }
        case K_DUPVT: {  // size at words 1-2: a second copy at the cursor, [esi] points at it
            const DWORD sz = *(const DWORD*)(ip + 1);
            e.op(0x8B, ECX, ESI, -8); VtLoad(e, EAX); e.op(0x89, EAX, ESI, 0);
            VtCopy(e, EAX, ECX, sz); VtAdd(e, (int)((sz + 7) & ~7u)); e.addEsi(8); break;
        }
        case K_VTRESULT: {  // after a call that returned a value type: release the callee's vt bytes (words 2-3, aligned 8); a
            // result of w1 (word 1) bytes moves down to the new cursor (memmove through EA's import, skipped when it is there
            // already) and [esi-8] points at it; then its aligned size is kept on the cursor. EA's 0xE545B0.
            const DWORD a2 = (*(const DWORD*)(ip + 2) + 7) & ~7u, a1 = (u16 + 7) & ~7u;
            VtLoad(e, ECX); e.rr(0x8B, ECX, EAX);                                   // eax = old cursor
            if (a2) { e.b(0x81); e.b(0xE9); e.d(a2); }                               // sub ecx, a2
            VtStore(e, ECX);
            if (u16) {
                e.rr(0x3B, EAX, ECX); e.b(0x74); u8* same = e.p; e.b(0);              // cmp ecx, eax; je same
                e.b(0x68); e.d(u16); e.b(0x50 + EAX); e.b(0x50 + ECX); e.op(0x89, ECX, ESI, -8);
                e.b(0xFF); e.b(0x15); e.d((DWORD)(uintptr_t)g_jitMemmove); e.b(0x83); e.b(0xC4); e.b(12);   // call dword ptr [memmove]; add esp, 12
                *same = (u8)(e.p - (same + 1));
                VtAdd(e, (int)a1);
            }
            break;
        }
        case K_CMP:
            e.op(0x8B, EDX, ESI, -16); e.rr(0x33, EAX, EAX); e.op(0x3B, EDX, ESI, -8);
            e.b(0x0F); e.b(t.x); e.b(0xC0);  // setcc al
            e.op(0x89, EAX, ESI, -16); e.addEsi(-8); break;
        case K_LDFLDVT: {  // ldfld.vt (EA's 0xE567B3): null check; the slot becomes the cursor; memcpy(cursor, obj + off, size); cursor += align8
            const DWORD sz = *(const DWORD*)(ip + 2);
            e.op(0x8B, ECX, ESI, -8); e.rr(0x85, ECX, ECX); exitRel[0] = e.jcc(0x84);
            VtLoad(e, EAX); e.op(0x89, EAX, ESI, -8); e.op(0x8D, ECX, ECX, u16);
            VtCopy(e, EAX, ECX, sz); VtAdd(e, (int)((sz + 7) & ~7u)); break;
        }
        case K_LDSFLDVT: {  // ldsfld.vt (0xE56EAE): class-init check; [esi] = cursor; memcpy(cursor, statics + off, size); cursor += align8
            const DWORD sz = *(const DWORD*)(ip + 2);
            exitRel[0] = e.classInitExit(sVt);
            e.b(0xA1); e.d(sVt + 0xC); e.op(0x8D, ECX, EAX, sOff);
            VtLoad(e, EAX); e.op(0x89, EAX, ESI, 0); VtCopy(e, EAX, ECX, sz); VtAdd(e, (int)((sz + 7) & ~7u)); e.addEsi(8); break;
        }
        case K_CEQ8:  // (a == b) over two 8-byte slots: xor the halves, or them, sete (EA's 0xE5803C); the low dword gets the result
            e.op(0x8B, EAX, ESI, -16); e.op(0x33, EAX, ESI, -8); e.op(0x8B, ECX, ESI, -12); e.op(0x33, ECX, ESI, -4);
            e.rr(0x0B, ECX, EAX); e.b(0x0F); e.b(0x94); e.b(0xC0); e.b(0x0F); e.b(0xB6); e.b(0xC0);  // or eax, ecx; sete al; movzx eax, al
            e.op(0x89, EAX, ESI, -16); e.addEsi(-8); break;
        case K_CEQ0: e.rr(0x33, EAX, EAX); e.b(0x83); e.m(7, ESI, -8); e.b(0); e.b(0x0F); e.b(0x94); e.b(0xC0); e.op(0x89, EAX, ESI, -8); break;
        case K_CONV:
            if (t.x == 1) { e.op(0x8B, EAX, ESI, -8); e.b(0x99); e.op(0x89, EDX, ESI, -4); }
            else if (t.x == 2) { e.b(0xC7); e.m(0, ESI, -4); e.d(0); }
            else if (t.x >= 4) { e.load(t.x == 4 ? 1 : t.x == 5 ? 2 : t.x == 6 ? 3 : 4, EAX, ESI, -8); e.op(0x89, EAX, ESI, -8); }
            break;  // conv.i4.i8: the low word already is the result
        default:
            EmitExit(e, ip, op);
            ex++;
            continue;
        }
        nat++;
        if (exitRel[0] || exitRel[1] || exitRel[2]) {  // out-of-line slow path: back to EA's handler with nothing changed
            u8* skip = e.jmp();
            if (exitRel[0]) Fix(exitRel[0], e.p);
            if (exitRel[1]) Fix(exitRel[1], e.p);
            if (exitRel[2]) Fix(exitRel[2], e.p);
            EmitExit(e, ip, op);
            Fix(skip, e.p);
        }
    }
    label[n] = e.p;  // falling off the end cannot happen (mint ends in ret/throw/br); trap it
    e.b(0xCC);
    for (int f = 0; f < nf; f++) {
        int tw = fix[f].targetW, tp = tw >= 0 && tw < words ? wordToPos[tw] : -1;
        if (tp < 0) { why = "a branch target is not an instruction"; break; }
        if (fix[f].abs) memcpy(fix[f].rel, &label[tp], 4); else Fix(fix[f].rel, label[tp]);
    }
    const int bytes = (int)(e.p - start);
    JitProt(start, need, PAGE_EXECUTE_READ);
    FlushInstructionCache(GetCurrentProcess(), start, bytes);
    if (why) return why;
    // 3. publish: table entries for every position, then patch the opcode words (positions of EA instruction starts and
    // superop inner positions alike; inner words are never read by the superop handlers)
    if (g_jitPosN + n > JIT_POS_N) return "position table full";
    JitMeth& M = g_jitMeth[g_jitMethN];
    M.rm = rm, M.code = code, M.words = words, M.pos0 = g_jitPosN, M.nativeOps = nat, M.exitOps = ex, M.native0 = start, M.bytes = bytes;
    JitMethodName(rm, M.name, sizeof(M.name));
    for (int k = 0; k < n; k++) {
        JitEnt* en = JitAdd((DWORD)(uintptr_t)(code + posW[k]));
        if (!en) return "ip table full";
        en->native = label[k], en->orig = code[posW[k]], en->m = &M;
        g_jitPos[g_jitPosN] = (unsigned short)posW[k];
        g_jitOrigW[g_jitPosN++] = code[posW[k]];
    }
    M.posN = g_jitPosN;
    g_jitMethN++;
    g_jitCodeUsed = (g_jitCodeUsed + bytes + 1 + 15) & ~(size_t)15;
    if (g_jitPatched) JitPatchMeth(M, true);
    ts3native_counters.jitMethods = g_jitMethN;
    ts3native_counters.jitCodeKB = (LONG)(g_jitCodeUsed >> 10);
    if (g_jitLogN++ < 256) Log("jit: compiled %s: %d positions, %d native, %d exits, %d bytes", M.name, n, nat, ex, bytes);
    return NULL;
}

// patch (true) or restore (false) one method's opcode words; the native code stays
static void JitPatchMeth(const JitMeth& m, bool on) {
    for (int k = m.pos0; k < m.posN; k++) m.code[g_jitPos[k]] = on ? JitWord(g_jitOrigW[k]) : g_jitOrigW[k];
}
static void JitSetPatched(bool on) {
    for (int m = 0; m < g_jitMethN; m++) JitPatchMeth(g_jitMeth[m], on);
    g_jitPatched = on;
}

// ---- JitEnter: the jump-table entry of JIT_OP (eax = edx = JIT_OP, edi = ip); jitCalls counts entries: calls and returns into a method ----
static __declspec(naked) void JitEnter() {
    __asm {
        mov eax, edi
        shr eax, 1
        and eax, 0x7FFF            // JIT_TAB_N - 1
        shl eax, 4
        add eax, g_jitTab
    probe:
        cmp [eax], edi
        je found
        add eax, 16
        cmp dword ptr [eax], 0
        jne probe
        int 3                      // a patched word without an entry: cannot happen
    found:
        mov ecx, dword ptr fs:[24h]
        cmp ecx, g_jitSimTid
        jne other
        inc ts3native_counters.jitCalls
        mov ebx, [ebp - 8]
        mov ebx, [ebx + 0x10]      // arena base (arguments)
        mov edi, [ebp - 0x14]      // locals base
        jmp dword ptr [eax + 4]
    other:                         // not the sim thread: the original opcode's live handler (the worker's redirect stub in a span)
        lock inc ts3native_counters.jitOtherTid
        mov edx, [eax + 8]         // JitEnt.orig
        mov eax, edx
        mov [ebp - 4], edi
        mov ecx, g_jitTableP
        jmp dword ptr [ecx + edx * 4]
    }
}

// ---- the post-pass (ilpatch.cpp's detour, after EA's transform) ----
static bool JitHashKnown(const char* full, const char* mn, DWORD size, const BYTE sha[32]) {
    for (int i = 0; i < g_jitRowN; i++)
        if (g_jitRows[i].size == size && !memcmp(g_jitRows[i].sha, sha, 32) && JitEndsWith(full, g_jitRows[i].key)) return true;
    for (int i = 0; i < kIlpSpecN; i++) {  // a core that carries an [IlPatch] fix in the converter's form (docs/527 §1)
        if (strcmp(mn, kIlpSpecs[i].name)) continue;
        for (DWORD b = 0; b < kIlpSpecs[i].nBuilds; b++) if (!memcmp(kIlpSpecs[i].builds[b].patched, sha, 32)) return true;
    }
    return false;
}
// NULL = compiled or nothing to do; else why not (name and note filled)
static const char* JitOne(BYTE* rm, void* ctx, char* name, int nameN, char* note, int noteN) {
    if (!*(void**)rm) return NULL;
    JitMethodName(rm, name, nameN);
    if (!JitListed(name)) return NULL;
    if (g_jitSimTid && GetCurrentThreadId() != g_jitSimTid) return "another thread";
    if (*g_jitForceJit != 0x75) return "MonoPatcher ForceJIT is on";
    for (int i = 0; i < g_jitMethN; i++) if (g_jitMeth[i].rm == rm) return NULL;  // a repeat transform: compiled once
    BYTE* mm = *(BYTE**)(rm + 8);
    const BYTE* hdr = mm ? g_ilpHeader(mm) : NULL;
    const DWORD size = hdr ? *(const DWORD*)hdr : 0;
    const BYTE* code = hdr ? *(const BYTE* const*)(hdr + 4) : NULL;
    BYTE sha[32];
    if (!code || !size || size > (1u << 20) || !IlpSha256(code, size, sha)) return "no readable IL body";
    if (!JitHashKnown(name, *(const char* const*)(mm + 0x18), size, sha)) {
        char hex[17];
        sprintf_s(note, noteN, " (%lu bytes, sha256 %s...)", size, IlpHex(sha, 8, hex));
        return "the IL hash is not a known one";
    }
    g_jitDomain = ctx ? *(void**)ctx : NULL;
    g_jitObjectClass = *g_jitObjectClassP;
    return JitCompile(rm);
}
static void __cdecl JitPostPass(BYTE* rm, void* ctx) {
    if (g_jitDead) return;
    char name[256] = "?", note[64] = "";
    const char* why = NULL;
    EnterCriticalSection(&g_jitCs);
    __try {
        why = JitOne(rm, ctx, name, sizeof(name), note, sizeof(note));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_jitDead = true;
        ts3native_counters.jitFaults++;
        JitProt(g_jitCode, JIT_CODE_BYTES, PAGE_EXECUTE_READ);
        Log("jit: fault 0x%08lX while compiling %s: no more compiles this session", GetExceptionCode(), name);
    }
    LeaveCriticalSection(&g_jitCs);
    if (why) {
        ts3native_counters.jitRefused++;
        if (g_jitLogN++ < 256) Log("jit: %s refused: %s%s", name, why, note);
    }
}
// End of every Simulate iteration (sim thread): names the sim thread, applies jitSwitch
static void __cdecl JitTick() {
    if (!g_jitSimTid) {
        EnterCriticalSection(&g_jitCs);  // a compile on the loading thread finishes first: from here on only this thread compiles
        g_jitSimTid = GetCurrentThreadId();
        LeaveCriticalSection(&g_jitCs);
        Log("jit: sim thread %lu, %d methods compiled before the first tick", g_jitSimTid, g_jitMethN);
    }
    const bool on = ts3native_counters.jitSwitch != 0;
    if (on != g_jitPatched) JitSetPatched(on);
}
// for the crash report: a fault inside compiled code names its method
static bool JitFaultName(DWORD eip, char* out, int n) {
    for (int i = 0; i < g_jitMethN; i++) {
        const JitMeth& m = g_jitMeth[i];
        if (eip < (DWORD)(uintptr_t)m.native0 || eip >= (DWORD)(uintptr_t)m.native0 + (DWORD)m.bytes) continue;
        ts3native_counters.jitFaults++;
        sprintf_s(out, n, "%s +0x%lX", m.name, eip - (DWORD)(uintptr_t)m.native0);
        return true;
    }
    return false;
}
// the template table for this build's opcode numbers (names as the host; [MonoJit] Groups = MONOHOST_JITC2's bits: 1 float
// fields + ldflda, 2 static fields, 4 array loads / stores, 8 stobj, 16 cknull / strlen / getchr, 32 div.un.i4 / rem.un.i4 /
// rem.r8, 64 switch, 128 the value-type moves and ceq.i8); returns the rows that found their opcode
static int JitTemplates(unsigned groups) {
    int n = 0;
    for (auto& t : kJitTmpl) {
        const unsigned g = t.kind == K_LDFLDA || !strcmp(t.name, "ldfld.r4") || !strcmp(t.name, "ldfld.r8") || !strcmp(t.name, "stfld.r4") || !strcmp(t.name, "stfld.r8") ? 1
            : t.kind == K_LDSFLD || t.kind == K_LDSFLDA || t.kind == K_STSFLD ? 2
            : t.kind == K_STELEM || t.kind == K_STELEMREF || !strcmp(t.name, "ldelem.r4") || !strcmp(t.name, "ldelem.r8") ? 4
            : t.kind == K_STOBJ ? 8 : t.kind == K_CKNULL || t.kind == K_STRLEN || t.kind == K_GETCHR ? 16
            : t.kind == K_DIVUN || t.kind == K_REMR8 ? 32 : t.kind == K_SWITCH ? 64
            : t.kind == K_LDVT || t.kind == K_STVT || t.kind == K_STINARGVT || t.kind == K_DUPVT || t.kind == K_VTRESULT || t.kind == K_CEQ8 || t.kind == K_LDFLDVT || t.kind == K_LDSFLDVT ? 128 : 0;
        if (g & ~groups) continue;
        for (DWORD op = 0; op <= 0x1A8; op++)
            if (g_jitOpNames[op] && !strcmp(g_jitOpNames[op], t.name)) { g_opInfo[op] = {t.kind, t.width, t.x}; n++; break; }
    }
    return n;
}
// the region: the tables, the work area and the code, carved from one allocation
static bool JitAlloc() {
    const size_t tab = sizeof(JitEnt) * (JIT_TAB_N + 4096), meth = sizeof(JitMeth) * JIT_METH_N, pos = 2 * sizeof(unsigned short) * JIT_POS_N;
    const size_t data = (tab + meth + pos + sizeof(JitScratch) + 0xFFF) & ~(size_t)0xFFF;
    u8* p = (u8*)VirtualAlloc(NULL, data + JIT_CODE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) return false;
    g_jitTab = (JitEnt*)p;
    g_jitMeth = (JitMeth*)(p + tab);
    g_jitPos = (unsigned short*)(p + tab + meth);
    g_jitOrigW = g_jitPos + JIT_POS_N;
    g_js = (JitScratch*)(p + tab + meth + pos);
    g_jitCode = p + data, g_jitCodeEnd = g_jitCode + JIT_CODE_BYTES, g_jitCodeUsed = 0;
    return true;
}
static_assert(JIT_POS_N <= (int)(JIT_TAB_N * 3 / 4), "the ip table stays under 3/4 full");
static void JitReadList(const wchar_t* ini) {
    wchar_t w[512] = L"";
    GetPrivateProfileStringW(L"MonoJit", L"List", L"", w, 512, ini);
    if (!w[0] || !WideCharToMultiByte(CP_ACP, 0, w, -1, g_jitUserBuf, sizeof(g_jitUserBuf), NULL, NULL)) return;
    char* ctx = NULL;
    for (char* t = strtok_s(g_jitUserBuf, ",", &ctx); t && g_jitUserN < 32; t = strtok_s(NULL, ",", &ctx)) g_jitUser[g_jitUserN++] = t;
}

// ts3native.ini [MonoJit] Enabled=1, Mode=1 compiled, Groups=<mask>, List=<names>. true = installed
// (keep the log). After every other code patch, before [AutShadow] (docs/527 §4.1), and only on the Steam 1.67 exe.
static bool ApplyMonoJit(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"MonoJit", L"Enabled", 0, ini)) return false;
    Ts3nCounters& C = ts3native_counters;
    const int mode = GetPrivateProfileIntW(L"MonoJit", L"Mode", 1, ini);
    if (mode != 1) { Log("SKIP mono jit: Mode=%d (1 compiled)", mode); return false; }
    const u8* base = (const u8*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != JIT_PE_STAMP || nt->OptionalHeader.SizeOfImage != JIT_PE_SIZE) { Log("SKIP mono jit: not the Steam 1.67 exe"); return false; }
    if (GetPrivateProfileIntW(L"MonoJit", L"Direct", 0, ini)) Log("mono jit: Direct=1 is not built (docs/527 step 1; docs/223 §8 gates lazy frames on the reader trap): Direct=0");
    for (const auto& c : kMcCode)  // [GcRootCheck] TailWatch and any other owner of the call path
        if (McFnv((const u8*)(uintptr_t)c.at, c.n) != c.fnv && (!c.fnv2 || McFnv((const u8*)(uintptr_t)c.at, c.n) != c.fnv2)) {
            Log("SKIP mono jit: EA code at 0x%08lX (%lu bytes) is not the stock Steam 1.67 bytes", c.at, c.n);
            return false;
        }
    if (g_jitTableP[JIT_OP] != JIT_EA_UNIMPLEMENTED) { Log("SKIP mono jit: jump-table entry 0x%03lX is 0x%08lX, not EA's (another owner; [MonoFast] CallPath=2 has them all)", JIT_OP, g_jitTableP[JIT_OP]); return false; }
    if (!IlpShaInit()) { Log("SKIP mono jit: SHA-256 (bcrypt.dll) unavailable"); return false; }
    g_jitGroups = GetPrivateProfileIntW(L"MonoJit", L"Groups", 255, ini);
    const int templates = JitTemplates((unsigned)g_jitGroups);
    if (!templates) { Log("SKIP mono jit: no template matches EA's opcode names"); return false; }
    if (!JitAlloc()) { Log("SKIP mono jit: no address space for the region"); return false; }
    InitializeCriticalSection(&g_jitCs);
    JitReadList(ini);
    if (!IlpHook()) { Log("SKIP mono jit: the transform prologue differs (another owner of 0xE643B0)"); return false; }
    if (!SimLoopAdd(JitTick, "mono jit: jitSwitch and the sim thread")) { Log("SKIP mono jit: no Simulate iteration hook"); return false; }
    const DWORD ent = (DWORD)(uintptr_t)JitEnter;
    if (!Write(g_jitTableP + JIT_OP, &ent, 4)) { Log("FAIL mono jit: jump-table entry (err %lu)", GetLastError()); return false; }
    g_jitMode = mode;
    g_jitPatched = true;
    C.jitSwitch = 1;
    C.jitOn = mode;
    g_ilpPost = JitPostPass;
    Log("OK   mono jit (docs/527): Mode=%d, %d templates (Groups=%d), %d list rows%s, code region %d KB", mode, templates, g_jitGroups, g_jitRowN, g_jitUserN ? " narrowed by List=" : "", JIT_CODE_BYTES >> 10);
    return true;
}
