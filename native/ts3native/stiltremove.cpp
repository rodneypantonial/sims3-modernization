// [StiltRemove] (next version, docs/858, counters v923 sr*): the stilts of a lot placed from the Library or the town bin can
// never be removed (Enabled), a foundation or deck placed next to a stilted foundation deletes the shared edge's stilts
// (PlaceKeep), stilt partitions at a lot's far edge get no index clamp (PartClamp). Each key default OFF. Included by
// ts3native.cpp after audiodup.cpp; selftest: stiltremove-test.h.
//
// EA code [verified disassembly, Steam TS3W 1.67]: the lot's stilted foundation manager ([lot+0x2B8], ctor 0xC1D860) keys
// every stilt by its grid point: key = 0xC1AB00({x, z, level 0}) = floor(z*8+0.5) << 12 | floor(x*8+0.5). A stilt record
// holds a render position (+0x00) and the grid point (+0x14 / +0x18); 0xC1ABE0 insets the render position 0.17 or 0.35
// tile from the grid point at foundation edges and corners. Building (0xC1E4D0), the save load's upgrade of data version
// <= 2 (0xC1DBB0) and both lookups of the foundation tools (collect 0xC1E9E0, remove 0xC1E8F0) key by the grid point; the
// save load of version 3 takes the key stored in the save. The lot import (DownloadContent.ImportLotContents 0x997540 ->
// 0xAD5000 -> lot 0xACE4A0 -> stilts 0xC1EDD0 -> per group 0xC1DF40) rotates both points into the new lot (0xCB32B0) but
// keys the stilt by its rotated render position (struct from [esp+0x1C] / [esp+0x40], call at 0xC1E2FB). An inset stilt
// then has a key no lookup computes: delete and sledgehammer collect nothing, and every save keeps the bad key.
//
// Enabled=1:
//   import: the key call at 0xC1E2FB goes to StImportKey, which keys by the rotated grid point ([esp+0x64] / [esp+0x68],
//     the values the record stores) as building does. srImport counts imported stilts, srImportFixed those whose
//     render-position key differs (the stilts EA made unremovable).
//   load: the version test at 0xC1DD49 jumps to StLoadStub, which keys every loaded stilt by its grid point, EA's own
//     version <= 2 path. srLoadFixed counts version-3 keys that differed: stilts an earlier import broke, repaired in
//     memory and written right by the next save. A stilt with a good key gets the same key back.
// EA 1.69 / retail 1.67: no address, SKIP. Household rule: not applicable (lot build data, no Sim decision).

static const DWORD ST_KEY = TS3A_PICK(0x00C1AB00, 0, 0);          // stilt key, cdecl(const StPoint*)
static const DWORD ST_LOAD_SITE = TS3A_PICK(0x00C1DD49, 0, 0);    // in 0xC1DBB0: cmp dword [ebp+10h],2; ja 0xC1DD86
static const DWORD ST_IMPORT_SITE = TS3A_PICK(0x00C1E2D0, 0, 0);  // in 0xC1DF40: key struct from the render position
static const DWORD ST_PAIR_SITE = TS3A_PICK(0x00C1E192, 0, 0);    // in 0xC1DF40: the rotated grid point to [esp+64h] / [esp+68h]
static const u8 kStLoad[6] = {0x83, 0x7D, 0x10, 0x02, 0x77, 0x37};
static const DWORD ST_LOAD_CONT = 0x3D;  // 0xC1DD86 - 0xC1DD49: the key is in [esp+18h], EA inserts the stilt
// movss xmm0,[esp+1Ch]; lea edx,[esp+78h]; movss [esp+78h],xmm0; movss xmm0,[esp+40h]; push edx; movss [esp+80h],xmm0;
// mov dword [esp+84h],0; then `call 0xC1AB00` at +0x2B
static const u8 kStImport[0x2B] = {0xF3, 0x0F, 0x10, 0x44, 0x24, 0x1C, 0x8D, 0x54, 0x24, 0x78, 0xF3, 0x0F, 0x11, 0x44, 0x24,
                                   0x78, 0xF3, 0x0F, 0x10, 0x44, 0x24, 0x40, 0x52, 0xF3, 0x0F, 0x11, 0x84, 0x24, 0x80, 0x00,
                                   0x00, 0x00, 0xC7, 0x84, 0x24, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const DWORD ST_IMPORT_CALL = 0x2B;
// movss xmm0,[esp+1Ch]; mov ecx,[esp+48h]; movss [esp+64h],xmm0; movss xmm0,[esp+40h]; lea edx,[esp+40h]; push edx;
// movss [esp+6Ch],xmm0 (= [esp+68h] before the push): the import's own frame offsets StImportKey reads
static const u8 kStPair[0x21] = {0xF3, 0x0F, 0x10, 0x44, 0x24, 0x1C, 0x8B, 0x4C, 0x24, 0x48, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x64, 0xF3,
                                 0x0F, 0x10, 0x44, 0x24, 0x40, 0x8D, 0x54, 0x24, 0x40, 0x52, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x6C};

struct StPoint { float x, z; DWORD level; };  // EA's key input; the level byte at +8 is 0 for every stilt
typedef DWORD(__cdecl* StKeyFn)(const StPoint* p);
static StKeyFn g_stKey;     // EA's 0xC1AB00; selftest points it at a fake
static DWORD g_stLoadCont;  // 0xC1DD86

static DWORD StGridKey(const float* grid) {
    const StPoint p = {grid[0], grid[1], 0};
    return g_stKey(&p);
}

static void StLogFixed(const char* what, LONG n, const float* grid, DWORD bad, DWORD good) {
    if (n <= 8 || !(n & (n - 1)))
        Log("stilt key %s: grid (%.2f, %.2f) key 0x%08lX -> 0x%08lX (%ld so far)", what, grid[0], grid[1], bad, good, n);
}

// import (replaces the call at 0xC1E2FB): p = the caller's [esp+78h]; the rotated grid point is at [esp+64h] / [esp+68h]
static DWORD __cdecl StImportKey(const StPoint* p) {
    const float* grid = (const float*)((const u8*)p - 0x14);
    const DWORD key = StGridKey(grid), eaKey = g_stKey(p);
    InterlockedIncrement(&ts3native_counters.srImport);
    if (key != eaKey) StLogFixed("fixed at import", InterlockedIncrement(&ts3native_counters.srImportFixed), grid, eaKey, key);
    return key;
}

// load: *key = the key read from the save; version = the stilt data version (3 since Island Paradise, EA writes 3)
static void __cdecl StLoadKey(const float* grid, DWORD* key, DWORD version) {
    const DWORD k = StGridKey(grid);
    if (version > 2 && k != *key) StLogFixed("repaired at load", InterlockedIncrement(&ts3native_counters.srLoadFixed), grid, *key, k);
    *key = k;
}

// Replaces `cmp dword [ebp+10h],2; ja 0xC1DD86` at 0xC1DD49 (jmp + nop). esp is the loader's 16-aligned frame: [esp+18h]
// the key read from the save, [esp+84h] / [esp+88h] the grid point (read from the save, or the position's x / z for
// version 1, as EA's own path uses). Every register is kept; flags and xmm are dead at 0xC1DD86 (EA reloads xmm0 there).
static __declspec(naked) void StLoadStub() {
    __asm {
        pushad
        push dword ptr [ebp + 10h]          // version
        lea eax, [esp + 4 + 20h + 18h]      // &key
        push eax
        lea eax, [esp + 8 + 20h + 84h]      // grid point
        push eax
        call StLoadKey
        add esp, 12
        popad
        jmp dword ptr [g_stLoadCont]
    }
}

// All three sites checked first, then the load jump and the import call written. Pointers so the selftest can use buffers.
static bool StPatch(u8* load, u8* imp, const u8* pair, DWORD keyFn) {
    u8 call[5] = {0xE8};
    *(LONG*)(call + 1) = (LONG)(keyFn - ((uintptr_t)imp + ST_IMPORT_CALL + 5));
    if (!Matches(load, kStLoad, sizeof(kStLoad)) || !Matches(imp, kStImport, sizeof(kStImport)) ||
        !Matches(imp + ST_IMPORT_CALL, call, 5) || !Matches(pair, kStPair, sizeof(kStPair)))
        return false;
    g_stKey = (StKeyFn)(uintptr_t)keyFn;
    g_stLoadCont = (DWORD)(uintptr_t)(load + ST_LOAD_CONT);
    u8 jmp[6] = {0xE9, 0, 0, 0, 0, 0x90};
    *(LONG*)(jmp + 1) = (LONG)((uintptr_t)&StLoadStub - ((uintptr_t)load + 5));
    *(LONG*)(call + 1) = (LONG)((uintptr_t)&StImportKey - ((uintptr_t)imp + ST_IMPORT_CALL + 5));
    return Write(load, jmp, sizeof(jmp)) && Write(imp + ST_IMPORT_CALL, call, 5);
}

// PlaceKeep=1 (docs/858 §5): the place path 0xCAECA0 of a foundation or deck that is not stilted removes the stilts in its
// block with RemoveAnyStiltedFoundationsInBlock 0xCA7E00 before the cell write 0xCAAC80 (0xCAEEA4) and with the neighbour
// check off (`push 0` at 0xCAED57 / 0xCAED8A), so collect 0xC1F810 -> 0xC1E9E0 drops every stilt in the block (0xC1EC36),
// also the edge stilts a stilted neighbour still stands on. EA's delete path 0xCAEF90 writes the cells first and calls
// 0xCA7E00 with the check on (0xCAF1BB): a stilt on the block edge goes only when no stilted cell still touches it, else it
// is moved (0xAFEBE0 -> 0xC1ABE0 on the written cells). PlaceKeep gives the place path that order: the call at 0xCAEDA9
// goes to StPlaceDefer, which keeps the arguments (cells copied: the deck branch builds them in the frame and 0xCAEDC2
// overwrites one); the call to 0xCA76B0(lot, 0) at 0xCAEF5F, which every path after the cell write reaches and which is
// also the delete path's last call, goes to StPlaceEndStub: when the cell write succeeded ([frame+12h], 0xCAEEAE) it runs
// 0xCA7E00 once with the kept arguments and the check on, then EA's 0xCA76B0. The undo branch of 0xCA7E00 (0xD1EA20)
// passes the check to the same collect (0xD1EB0E). The removal now comes after the write's undo records, as in delete.
static const DWORD ST_DEFER_SITE = TS3A_PICK(0x00CAED4D, 0, 0);    // in 0xCAECA0: both branches' 0xCA7E00 arguments
static const DWORD ST_WRITE_SITE = TS3A_PICK(0x00CAEEA9, 0, 0);    // after `call 0xCAAC80`: its result to [esp+12h]
static const DWORD ST_END_SITE = TS3A_PICK(0x00CAEF5C, 0, 0);      // push 0; push esi; call 0xCA76B0; ... the epilogue
static const DWORD ST_REMOVE_BLOCK = TS3A_PICK(0x00CA7E00, 0, 0);  // RemoveAnyStiltedFoundationsInBlock, cdecl, 7 args
static const DWORD ST_END_FN = TS3A_PICK(0x00CA76B0, 0, 0);        // cdecl (lot, 0 / 1), no register inputs
// mov eax,[esp+64h]; mov ecx,[esp+24h]; push eax; push ebx; push 0; push ecx; push edi; push ebp; jmp +4Ah (block branch);
// cmp byte [ebp+2],1; jne; ... the deck branch's cells at [esp+14h] / [esp+18h] (level 0 to level-1), push 0 ...; push esi;
// then `call 0xCA7E00` at +0x5C and `add esp,1Ch`
static const u8 kStDefer[0x5C] = {
    0x8B, 0x44, 0x24, 0x64, 0x8B, 0x4C, 0x24, 0x24, 0x50, 0x53, 0x6A, 0x00, 0x51, 0x57, 0x55, 0xEB, 0x4A, 0x80, 0x7D, 0x02,
    0x01, 0x75, 0x4D, 0x0F, 0xB6, 0x55, 0x00, 0x0F, 0xB6, 0x45, 0x01, 0x8A, 0x0F, 0x88, 0x44, 0x24, 0x19, 0x0F, 0xB6, 0x47,
    0x02, 0x88, 0x4C, 0x24, 0x14, 0x8B, 0x4C, 0x24, 0x64, 0x51, 0x88, 0x54, 0x24, 0x1C, 0x0F, 0xB6, 0x57, 0x01, 0x53, 0x2C,
    0x01, 0x6A, 0x00, 0x88, 0x54, 0x24, 0x21, 0x8B, 0x54, 0x24, 0x30, 0x88, 0x44, 0x24, 0x22, 0x52, 0x8D, 0x44, 0x24, 0x24,
    0x50, 0x8D, 0x4C, 0x24, 0x2C, 0xC6, 0x44, 0x24, 0x2E, 0x00, 0x51, 0x56};
static const DWORD ST_DEFER_CALL = 0x5C;
static const u8 kStDeferPost[3] = {0x83, 0xC4, 0x1C};
// add esp,54h; test al,al; mov [esp+12h],al; je 0xCAEF53: the frame (esp of the 0x40-byte body) keeps the write's result
static const u8 kStWrite[0xF] = {0x83, 0xC4, 0x54, 0x84, 0xC0, 0x88, 0x44, 0x24, 0x12, 0x0F, 0x84, 0x9B, 0x00, 0x00, 0x00};
static const u8 kStEndPre[3] = {0x6A, 0x00, 0x56};
static const DWORD ST_END_CALL = 3;
// mov eax,[esp+38h]; add esp,8; test eax,eax; pop ebp; pop ebx; je; push eax; call free; add esp,4; mov al,[esp+0Ah] (the
// frame's +12h: the function returns the write's result); pop esi; pop edi; add esp,30h; ret
static const u8 kStEndPost[0x20] = {0x8B, 0x44, 0x24, 0x38, 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x5D, 0x5B, 0x74, 0x09, 0x50, 0xE8, 0xF9,
                                    0x4F, 0x83, 0xFF, 0x83, 0xC4, 0x04, 0x8A, 0x44, 0x24, 0x0A, 0x5E, 0x5F, 0x83, 0xC4, 0x30, 0xC3};

struct StCell { u8 x, z, level, pad; };  // EA's block corner (3 bytes read)
typedef char(__cdecl* StRemoveFn)(DWORD lot, const StCell* p0, const StCell* p1, DWORD diag, DWORD recheck, DWORD undo, DWORD arg7);
static StRemoveFn g_stRemove;  // 0xCA7E00; selftest: a fake
static DWORD g_stEnd;          // 0xCA76B0
// the deferred removal; frame = the place function's esp below its 7 pushes, 0 = none (one place call at a time: main thread)
static struct { DWORD frame, lot, diag, undo, arg7; StCell p0, p1; } g_stPlace;

static void StPlaceDeferAt(DWORD frame, DWORD lot, const StCell* p0, const StCell* p1, DWORD diag, DWORD undo, DWORD arg7) {
    g_stPlace.lot = lot, g_stPlace.diag = diag, g_stPlace.undo = undo, g_stPlace.arg7 = arg7;
    g_stPlace.p0 = {p0->x, p0->z, p0->level, 0};
    g_stPlace.p1 = {p1->x, p1->z, p1->level, 0};
    g_stPlace.frame = frame;
}

// replaces `call 0xCA7E00` at 0xCAEDA9: same cdecl arguments (EA pushes recheck 0); 0xCA7E00's result is not read
static __declspec(noinline) char __cdecl StPlaceDefer(DWORD lot, const StCell* p0, const StCell* p1, DWORD diag, DWORD recheck,
                                                      DWORD undo, DWORD arg7) {
    (void)recheck;
    StPlaceDeferAt((DWORD)(uintptr_t)_AddressOfReturnAddress() + 4 + 0x1C, lot, p0, p1, diag, undo, arg7);
    return 1;
}

static void __cdecl StPlaceFlush(DWORD frame, DWORD lot) {
    if (!g_stPlace.frame) return;
    const bool mine = g_stPlace.frame == frame && g_stPlace.lot == lot;  // else left by a call that never got here
    g_stPlace.frame = 0;
    const StCell a = g_stPlace.p0, b = g_stPlace.p1;
    if (!mine || !*(const u8*)(uintptr_t)(frame + 0x12)) {  // cell write failed: nothing placed, nothing to remove
        static LONG dropped;
        const LONG n = InterlockedIncrement(&dropped);
        if (n <= 8 || !(n & (n - 1)))
            Log("stilt place: deferred removal in block (%u,%u)-(%u,%u) dropped (%s, %ld so far)", a.x, a.z, b.x, b.z,
                mine ? "cell write failed" : "not this call", n);
        return;
    }
    const LONG n = InterlockedIncrement(&ts3native_counters.srPlaceRecheck);
    if (n <= 8 || !(n & (n - 1)))
        Log("stilt place: removal in block (%u,%u)-(%u,%u) level %u-%u run after the cell write with the neighbour check (%ld so far)",
            a.x, a.z, b.x, b.z, a.level, b.level, n);
    g_stRemove(g_stPlace.lot, &g_stPlace.p0, &g_stPlace.p1, g_stPlace.diag, 1, g_stPlace.undo, g_stPlace.arg7);
}

// replaces `call 0xCA76B0` at 0xCAEF5F (after `push 0; push esi`): the place function's frame is esp + 0Ch at entry
static __declspec(naked) void StPlaceEndStub() {
    __asm {
        push dword ptr [esp + 4]  // lot
        lea eax, [esp + 10h]      // frame
        push eax
        call StPlaceFlush
        add esp, 8
        jmp dword ptr [g_stEnd]   // EA's 0xCA76B0(lot, 0), returns to 0xCAEF64
    }
}

// All three sites checked first; the end call is written before the defer call, so a failed second write leaves EA's order.
static bool StPlacePatch(u8* defer, const u8* write, u8* end, DWORD removeFn, DWORD endFn) {
    u8 cd[5] = {0xE8}, ce[5] = {0xE8};
    *(LONG*)(cd + 1) = (LONG)(removeFn - ((uintptr_t)defer + ST_DEFER_CALL + 5));
    *(LONG*)(ce + 1) = (LONG)(endFn - ((uintptr_t)end + ST_END_CALL + 5));
    if (!Matches(defer, kStDefer, sizeof(kStDefer)) || !Matches(defer + ST_DEFER_CALL, cd, 5) ||
        !Matches(defer + ST_DEFER_CALL + 5, kStDeferPost, sizeof(kStDeferPost)) || !Matches(write, kStWrite, sizeof(kStWrite)) ||
        !Matches(end, kStEndPre, sizeof(kStEndPre)) || !Matches(end + ST_END_CALL, ce, 5) ||
        !Matches(end + ST_END_CALL + 5, kStEndPost, sizeof(kStEndPost)))
        return false;
    g_stRemove = (StRemoveFn)(uintptr_t)removeFn;
    g_stEnd = endFn;
    *(LONG*)(cd + 1) = (LONG)((uintptr_t)&StPlaceDefer - ((uintptr_t)defer + ST_DEFER_CALL + 5));
    *(LONG*)(ce + 1) = (LONG)((uintptr_t)&StPlaceEndStub - ((uintptr_t)end + ST_END_CALL + 5));
    return Write(end + ST_END_CALL, ce, 5) && Write(defer + ST_DEFER_CALL, cd, 5);
}

// PartClamp=1 (docs/858 §6): the stilt manager files each stilt in an 8x8-tile partition by its grid point (insert 0xC1CF80
// from build 0xC1E530 and rebuild 0xC1F473, remove 0xC1C120 from 0xC1E945): idx = (floor(z)>>3) * cols + (floor(x)>>3)
// with cols = ((width-1)>>3)+1 ([lot+174h], 0xC1F7D5). A grid point runs to x = width, so on a lot whose side is a multiple
// of 8 a stilt on the far edge gets column cols (filed in the next row, which the query 0xC1F4D0, clamped to cols-1, never
// reads: the stilt cannot be collected) or row rows (past the array). The four `mov reg,[esp+10h]; shr reg,3` become calls
// that clamp to cols-1 / rows-1 (a negative value to 0), the same at insert and remove, so a filed stilt is found again.
static const DWORD ST_PART_INS = TS3A_PICK(0x00C1D03E, 0, 0);  // in 0xC1CF80: row from floor(z) (ecx), col at +2Dh (eax)
static const DWORD ST_PART_REM = TS3A_PICK(0x00C1C1E0, 0, 0);  // in 0xC1C120: row (esi), col at +2Dh (eax)
static const DWORD ST_PART_COL = 0x2D;
// mov ecx,[esp+10h]; shr ecx,3; fldcw; imul ecx,[edx+6Ch]; (floor(x) by fistp); mov eax,[esp+10h]; shr eax,3; add ecx,eax;
// fldcw; shl ecx,5; add ecx,[edx+5Ch]
static const u8 kStPartIns[0x40] = {
    0x8B, 0x4C, 0x24, 0x10, 0xC1, 0xE9, 0x03, 0xD9, 0x6C, 0x24, 0x0E, 0x0F, 0xAF, 0x4A, 0x6C, 0xD9, 0x44, 0x24, 0x20, 0xD9,
    0x7C, 0x24, 0x0E, 0x0F, 0xB7, 0x44, 0x24, 0x0E, 0x0D, 0x00, 0x0C, 0x00, 0x00, 0x89, 0x44, 0x24, 0x10, 0xD9, 0x6C, 0x24,
    0x10, 0xDF, 0x7C, 0x24, 0x10, 0x8B, 0x44, 0x24, 0x10, 0xC1, 0xE8, 0x03, 0x03, 0xC8, 0xD9, 0x6C, 0x24, 0x0E, 0xC1, 0xE1,
    0x05, 0x03, 0x4A, 0x5C};
// the same with esi for the row and ecx for the manager: ... imul esi,[ecx+6Ch] ... add esi,eax; shl esi,5; add esi,[ecx+5Ch]
static const u8 kStPartRem[0x3C] = {
    0x8B, 0x74, 0x24, 0x10, 0xC1, 0xEE, 0x03, 0xD9, 0x6C, 0x24, 0x0E, 0x0F, 0xAF, 0x71, 0x6C, 0xD9, 0x44, 0x24, 0x20, 0xD9,
    0x7C, 0x24, 0x0E, 0x0F, 0xB7, 0x44, 0x24, 0x0E, 0x0D, 0x00, 0x0C, 0x00, 0x00, 0x89, 0x44, 0x24, 0x10, 0xD9, 0x6C, 0x24,
    0x10, 0xDF, 0x7C, 0x24, 0x10, 0x8B, 0x44, 0x24, 0x10, 0xC1, 0xE8, 0x03, 0x03, 0xF0, 0xC1, 0xE6, 0x05, 0x03, 0x71, 0x5C};

// v = floor(z) or floor(x) as EA's fistp left it; n = rows ([mgr+70h]) or cols ([mgr+6Ch]); site bit 0 column, bit 1 remove
static DWORD __cdecl StPartIdx(int v, DWORD n, DWORD site) {
    const DWORD i = (DWORD)v >> 3;  // EA: shr reg,3
    if (i < n || !n) return i;
    const DWORD c = v < 0 ? 0 : n - 1;
    const LONG k = InterlockedIncrement(&ts3native_counters.srClamped);
    if (k <= 8 || !(k & (k - 1)))
        Log("stilt partition index clamped (%s %s): grid %d -> %lu of %lu (%ld so far)", site & 2 ? "remove" : "insert",
            site & 1 ? "column" : "row", v, c, n, k);
    return c;
}

// Each replaces a 7-byte `mov reg,[esp+10h]; shr reg,3` (call + 2 nops); the caller's [esp+10h] is [esp+14h] at entry.
// Flags are dead (fldcw / add follow, then imul / shl); eax is dead at the row sites (reloaded by movzx before its next use).
static __declspec(naked) void StPartInsRow() {  // 0xC1D03E: out ecx; edx = manager
    __asm {
        push edx
        push 0
        push dword ptr [edx + 70h]
        push dword ptr [esp + 20h]
        call StPartIdx
        add esp, 12
        pop edx
        mov ecx, eax
        ret
    }
}
static __declspec(naked) void StPartInsCol() {  // 0xC1D06B: out eax; ecx = row * cols, edx = manager
    __asm {
        push ecx
        push edx
        push 1
        push dword ptr [edx + 6Ch]
        push dword ptr [esp + 24h]
        call StPartIdx
        add esp, 12
        pop edx
        pop ecx
        ret
    }
}
static __declspec(naked) void StPartRemRow() {  // 0xC1C1E0: out esi; ecx = manager, edx = &[esp+10h] (pushed later)
    __asm {
        push ecx
        push edx
        push 2
        push dword ptr [ecx + 70h]
        push dword ptr [esp + 24h]
        call StPartIdx
        add esp, 12
        pop edx
        pop ecx
        mov esi, eax
        ret
    }
}
static __declspec(naked) void StPartRemCol() {  // 0xC1C20D: out eax; esi = row * cols, ecx = manager, edx as above
    __asm {
        push ecx
        push edx
        push 3
        push dword ptr [ecx + 6Ch]
        push dword ptr [esp + 24h]
        call StPartIdx
        add esp, 12
        pop edx
        pop ecx
        ret
    }
}

static bool StCall7(u8* at, void (*fn)()) {
    u8 b[7] = {0xE8, 0, 0, 0, 0, 0x90, 0x90};
    *(LONG*)(b + 1) = (LONG)((uintptr_t)fn - ((uintptr_t)at + 5));
    return Write(at, b, sizeof(b));
}

// Both functions checked before anything is written: insert and remove must clamp alike.
static bool StPartPatch(u8* ins, u8* rem) {
    if (!Matches(ins, kStPartIns, sizeof(kStPartIns)) || !Matches(rem, kStPartRem, sizeof(kStPartRem))) return false;
    return StCall7(rem, StPartRemRow) && StCall7(rem + ST_PART_COL, StPartRemCol) && StCall7(ins, StPartInsRow) &&
           StCall7(ins + ST_PART_COL, StPartInsCol);
}

// [StiltRemove] Enabled=1: imported stilts keyed by their grid point, broken keys repaired at load; PlaceKeep=1, PartClamp=1
static bool ApplyStiltRemove(const wchar_t* ini) {
    bool on = false;
    Ts3nCounters& k = ts3native_counters;
    if (GetPrivateProfileIntW(L"StiltRemove", L"Enabled", 0, ini) > 0) {
        if (!StPatch((u8*)(uintptr_t)ST_LOAD_SITE, (u8*)(uintptr_t)ST_IMPORT_SITE, (const u8*)(uintptr_t)ST_PAIR_SITE, ST_KEY)) {
            k.srMode = -1;
            Log("SKIP StiltRemove: bytes at 0x%08lX / 0x%08lX / 0x%08lX differ (or no address on this build), nothing written",
                ST_LOAD_SITE, ST_IMPORT_SITE, ST_PAIR_SITE);
        } else {
            k.srMode = 1, on = true;
            Log("OK   StiltRemove (docs/858): imported stilts keyed by their grid point (0xC1E2FB), stilt keys checked at load (0xC1DD49)");
        }
    }
    if (GetPrivateProfileIntW(L"StiltRemove", L"PlaceKeep", 0, ini) > 0) {
        if (!StPlacePatch((u8*)(uintptr_t)ST_DEFER_SITE, (const u8*)(uintptr_t)ST_WRITE_SITE, (u8*)(uintptr_t)ST_END_SITE, ST_REMOVE_BLOCK,
                          ST_END_FN)) {
            k.srPlaceMode = -1;
            Log("SKIP StiltRemove PlaceKeep: bytes at 0x%08lX / 0x%08lX / 0x%08lX differ (or no address on this build), nothing written",
                ST_DEFER_SITE, ST_WRITE_SITE, ST_END_SITE);
        } else {
            k.srPlaceMode = 1, on = true;
            Log("OK   StiltRemove PlaceKeep (docs/858 §5): a placement's stilt removal runs after the cell write with the neighbour "
                "check (0xCAEDA9 -> 0xCAEF5F)");
        }
    }
    if (GetPrivateProfileIntW(L"StiltRemove", L"PartClamp", 0, ini) > 0) {
        if (!StPartPatch((u8*)(uintptr_t)ST_PART_INS, (u8*)(uintptr_t)ST_PART_REM)) {
            k.srClampMode = -1;
            Log("SKIP StiltRemove PartClamp: bytes at 0x%08lX / 0x%08lX differ (or no address on this build), nothing written",
                ST_PART_INS, ST_PART_REM);
        } else {
            k.srClampMode = 1, on = true;
            Log("OK   StiltRemove PartClamp (docs/858 §6): stilt partition indexes clamped to the last column / row (insert "
                "0xC1D03E, remove 0xC1C1E0)");
        }
    }
    return on;
}
