// [StiltRemove] (next version, docs/858, counters v923 sr*): the stilts of a lot placed from the Library or the town bin can
// never be removed. Default OFF (save data). Included by ts3native.cpp after audiodup.cpp; selftest: stiltremove-test.h.
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

// [StiltRemove] Enabled=1: imported stilts keyed by their grid point, broken keys repaired at load
static bool ApplyStiltRemove(const wchar_t* ini) {
    if (GetPrivateProfileIntW(L"StiltRemove", L"Enabled", 0, ini) <= 0) return false;
    if (!StPatch((u8*)(uintptr_t)ST_LOAD_SITE, (u8*)(uintptr_t)ST_IMPORT_SITE, (const u8*)(uintptr_t)ST_PAIR_SITE, ST_KEY)) {
        ts3native_counters.srMode = -1;
        Log("SKIP StiltRemove: bytes at 0x%08lX / 0x%08lX / 0x%08lX differ (or no address on this build), nothing written",
            ST_LOAD_SITE, ST_IMPORT_SITE, ST_PAIR_SITE);
        return false;
    }
    ts3native_counters.srMode = 1;
    Log("OK   StiltRemove (docs/858): imported stilts keyed by their grid point (0xC1E2FB), stilt keys checked at load (0xC1DD49)");
    return true;
}
