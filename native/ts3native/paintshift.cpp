// [PaintShift] (next version, docs/859, counters v924 ps*): a lot placed from the Library or the town bin onto a bigger lot
// gets its terrain paint half a tile off from the house. Default OFF. Included by ts3native.cpp after audiodup.cpp;
// selftest: paintshift-test.h.
//
// EA code [verified disassembly, Steam TS3W 1.67]: the lot import (0xACE4A0) picks a rotation and a position (the
// player's LotPosition: front-left .. back-right) and builds two grid transforms in its context with 0xCB3120
// (thiscall: dst width, dst depth, src width, src depth, rotation, position): cells at ctx+0x54 and grid points at
// ctx+0x64 (tiles + 1, 0xAC4F02). Objects, stilts and heights use ctx+0x64. A centred axis gets the offset
// (dst - src) >> 1 (0xCB3080), so a lot 5 tiles smaller sits 2 tiles in. The terrain paint import (0xAF34F0) builds
// its own transform for the paint alpha maps, which have 4 samples per tile (tiles*4 + 1, LotTerrain init 0xAEB730):
// the call at 0xAF36ED passes (4*dst + 1, 4*src + 1, ...), so a centred axis gets 2 * (dst - src) samples, (dst - src)/2
// tiles without the floor: 2.5 tiles for the lot above. With an odd size difference on a centred axis the paint lands
// half a tile (2 samples) past the house, walls and fences. Same-size lots and the corner / edge positions match.
// The export (0xAF2970) writes the alpha maps as they are: bin files are fine, only the placed copy shifts.
//
// Enabled=1: the call at 0xAF36ED goes to PsXform, which runs EA's 0xCB3120 as before and then sets the paint offsets
// to 4 x the offsets of the grid-point transform for the same sizes, rotation and position (EA's own 0xCB3120 on
// tiles + 1, as 0xAC4F02 builds ctx+0x64). The rotation terms agree already ((4*src + 1) - 1 = 4 * src), so only the
// odd centred case changes. psImports counts paint transforms, psFixed those whose offset changed.
// EA 1.69 / retail 1.67: no address, SKIP. Household rule: not applicable (lot build data, no Sim decision).

static const DWORD PS_SITE = TS3A_PICK(0x00AF36AD, 0, 0);    // in 0xAF34F0: args for the paint transform, call at +0x40
static const DWORD PS_XFORM = TS3A_PICK(0x00CB3120, 0, 0);   // grid transform ctor
// mov eax,[esp+0C4h]; mov esi,[eax+68h]; mov ecx,[eax+1Ch]; mov edx,[eax+20h]; mov eax,[eax+64h]; push esi;
// mov esi,[esp+14h]; push eax; lea edx,[edx*4+1]; lea ecx,[ecx*4+1]; push edx; push ecx; mov [esp+50h],ecx;
// mov ecx,[esi+20h]; mov [esp+3Ch],edx; mov edx,[esi+1Ch]; push ecx; push edx; lea ecx,[esp+0C4h]; then the call
static const u8 kPsSite[0x40] = {
    0x8B, 0x84, 0x24, 0xC4, 0x00, 0x00, 0x00, 0x8B, 0x70, 0x68, 0x8B, 0x48, 0x1C, 0x8B, 0x50, 0x20, 0x8B, 0x40, 0x64, 0x56, 0x8B, 0x74,
    0x24, 0x14, 0x50, 0x8D, 0x14, 0x95, 0x01, 0x00, 0x00, 0x00, 0x8D, 0x0C, 0x8D, 0x01, 0x00, 0x00, 0x00, 0x52, 0x51, 0x89, 0x4C, 0x24,
    0x50, 0x8B, 0x4E, 0x20, 0x89, 0x54, 0x24, 0x3C, 0x8B, 0x56, 0x1C, 0x51, 0x52, 0x8D, 0x8C, 0x24, 0xC4, 0x00, 0x00, 0x00};
static const DWORD PS_CALL = 0x40;  // 0xAF36ED

struct PsXf { LONG rot, pos, offX, offZ; };  // EA's grid transform: rotation 0-3, LotPosition 0-8, offsets in samples
typedef void(__fastcall* PsXfFn)(PsXf* self, void* edx, LONG dstW, LONG dstD, LONG srcW, LONG srcD, LONG rot, LONG pos);
static PsXfFn g_psXform;  // EA's 0xCB3120; selftest points it at a port

static void __fastcall PsXform(PsXf* self, void* edx, LONG dstW, LONG dstD, LONG srcW, LONG srcD, LONG rot, LONG pos) {
    g_psXform(self, edx, dstW, dstD, srcW, srcD, rot, pos);
    InterlockedIncrement(&ts3native_counters.psImports);
    if ((DWORD)rot > 3 || (DWORD)pos > 8) return;  // EA writes no offsets for these
    PsXf pts;  // the grid-point transform objects get, on tiles + 1 (sizes here are tiles * 4 + 1)
    g_psXform(&pts, NULL, (dstW - 1) / 4 + 1, (dstD - 1) / 4 + 1, (srcW - 1) / 4 + 1, (srcD - 1) / 4 + 1, rot, pos);
    const LONG x = pts.offX * 4, z = pts.offZ * 4;
    if (x == self->offX && z == self->offZ) return;
    const LONG n = InterlockedIncrement(&ts3native_counters.psFixed);
    if (n <= 8 || !(n & (n - 1)))
        Log("paint shift: lot %ldx%ld on %ldx%ld, rotation %ld, position %ld: paint offset (%ld, %ld) -> (%ld, %ld) samples (%ld so far)",
            (srcW - 1) / 4, (srcD - 1) / 4, (dstW - 1) / 4, (dstD - 1) / 4, rot, pos, self->offX, self->offZ, x, z, n);
    self->offX = x, self->offZ = z;
}

// Pointers so the selftest can use a buffer: the argument block and EA's call checked, then the call goes to PsXform.
static bool PsPatch(u8* site, DWORD xform) {
    u8 call[5] = {0xE8};
    *(LONG*)(call + 1) = (LONG)(xform - ((uintptr_t)site + PS_CALL + 5));
    if (!Matches(site, kPsSite, sizeof(kPsSite)) || !Matches(site + PS_CALL, call, 5)) return false;
    g_psXform = (PsXfFn)(uintptr_t)xform;
    *(LONG*)(call + 1) = (LONG)((uintptr_t)&PsXform - ((uintptr_t)site + PS_CALL + 5));
    return Write(site + PS_CALL, call, 5);
}

// [PaintShift] Enabled=1: imported terrain paint placed with the same offsets as the lot's objects
static bool ApplyPaintShift(const wchar_t* ini) {
    if (GetPrivateProfileIntW(L"PaintShift", L"Enabled", 0, ini) <= 0) return false;
    if (!PsPatch((u8*)(uintptr_t)PS_SITE, PS_XFORM)) {
        ts3native_counters.psMode = -1;
        Log("SKIP PaintShift: bytes at 0x%08lX differ (or no address on this build), nothing written", PS_SITE);
        return false;
    }
    ts3native_counters.psMode = 1;
    Log("OK   PaintShift (docs/859): imported terrain paint offset = 4 x the object offset (0xAF36ED)");
    return true;
}
