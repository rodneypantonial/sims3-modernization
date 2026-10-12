// Self-check for the patch helpers on local memory (no game needed). Built and run by build.cmd.
#define TS3NATIVE_NO_DLLMAIN
#include "ts3native.cpp"
#include <assert.h>

static int __cdecl Hook() { return 7; }

int main() {
    // PatchImm32: all sites match -> written; one differs -> nothing written.
    static u8 buf[16] = {0x3D, 0x00, 0x08, 0x00, 0x00, 0x76, 0x2C, 0xB8, 0x00, 0x08, 0x00, 0x00, 0x50};
    Site ok[] = {{(DWORD)(uintptr_t)buf, {0x3D, 0x00, 0x08, 0x00, 0x00, 0x76, 0x2C}, 7, 1},
                 {(DWORD)(uintptr_t)(buf + 7), {0xB8, 0x00, 0x08, 0x00, 0x00, 0x50}, 6, 1}};
    Site bad[] = {ok[0], {(DWORD)(uintptr_t)(buf + 7), {0xB8, 0x00, 0x09, 0x00, 0x00, 0x50}, 6, 1}};
    assert(!PatchImm32("bad", bad, 2, 0x2000));
    assert(buf[2] == 0x08 && buf[9] == 0x08);
    assert(PatchImm32("ok", ok, 2, 0x2000));
    assert(buf[2] == 0x20 && buf[9] == 0x20 && buf[5] == 0x76);

    // InstallDetour on a tiny function: mov eax,42 ; ret
    u8* fn = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    const u8 code[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    memcpy(fn, code, sizeof(code));
    assert(!InstallDetour("bad", (DWORD)(uintptr_t)fn, (const u8*)"\xB8\x2B\x00\x00\x00", 5, (void*)Hook));
    void* tramp = InstallDetour("ok", (DWORD)(uintptr_t)fn, code, 5, (void*)Hook);
    assert(tramp);
    assert(((int(__cdecl*)())fn)() == 7);
    assert(((int(__cdecl*)())tramp)() == 42);
    puts("selftest ok");
    return 0;
}
