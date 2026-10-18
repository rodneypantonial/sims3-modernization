// Offline checks for the open items U1-U5 of docs/98 (in-room quadtree A*), results in docs/98 §10. Research tool, not part
// of ts3native: it maps TS3W.exe (Steam 1.67) at its own addresses in this process, like steerro-test, and calls the game's
// own functions on constructed inputs. Replaced game code: the GeneralAllocator entry points and the four small pools of the
// routing code (CRT heap), and two capture jumps inside InRoomPlan 0x632E40 (U2). Nothing else is patched.
//   U1  QuadCell_Neighbours 0x66B9C0 on every final cell of random trees: true edge neighbours vs the returned list (wrap at the
//       root border, order, missing cells); LotQuadTree_SetBounds 0x6646F0 for root size vs bounds.
//   U2  the nudge radius code of InRoomPlan (0x632F18..0x63306A) run for real: agent-class flag and radius per ring.
//   U3  footprint class / multiplier slots and the allow/deny gate 0x946E70 / 0x946E90 / 0x81CEF0 on constructed agents.
//   U4  sub-cell masks: shape masks from 0x667740, the eighth-mask writer 0x662FF0, the leaf final-bit update 0x667BA0.
//   U5  footprint rectangle tests: polygon slots 0x80F0D0 / 0x80F200 and box slots 0x80B4F0 / 0x80B1C0 vs exact geometry.
// Build (x86 developer prompt, in native\ts3native; the output folder is your choice, not out\):
//   cl /nologo /O2 /MT /W4 /WX /wd4505 /Fo<dir>\ /Fe<dir>\inroom-test.exe inroom-test.cpp kernel32.lib /link /BASE:0x20000000 /FIXED /DYNAMICBASE:NO
// Run:  <dir>\inroom-test.exe ["<path>\TS3W.exe"] [trees, default 300] [seed]      exit 0 = every check passed
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <stdint.h>

typedef unsigned char u8;
extern "C" void __cdecl _CIpow(void);

static DWORD g_seed = 12345;
static DWORD Rnd(DWORD n) { g_seed = g_seed * 1103515245 + 12345; return ((g_seed >> 8) & 0xFFFFFF) % n; }
static float Rf(float lo, float hi) { return lo + (hi - lo) * (float)Rnd(1 << 20) / (float)(1 << 20); }
static int g_fail;
static int g_trees = 300;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const DWORD IMG_BASE = 0x00400000, IMG_HI = 0x0125A000;  // SizeOfImage 0xE5A000 (Steam 1.67)

// as steerro-test: start ourselves suspended and reserve the image range before Windows maps anything at 0x400000
static int RunChild() {
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    SetEnvironmentVariableA("IRT_CHILD", "1");
    if (!CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) return 2;
    if (VirtualAllocEx(pi.hProcess, (void*)(uintptr_t)IMG_BASE, IMG_HI - IMG_BASE, MEM_RESERVE, PAGE_EXECUTE_READWRITE) != (void*)(uintptr_t)IMG_BASE) {
        printf("cannot reserve the image range in the child (err %lu)\n", GetLastError());
        TerminateProcess(pi.hProcess, 2);
        return 2;
    }
    ResumeThread(pi.hThread);
    DWORD rc = 2;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &rc);
    return (int)rc;
}

static void __cdecl TrapReport(DWORD from) {
    printf("unexpected import call from 0x%08lX\n", from);
    ExitProcess(3);
}
static void __declspec(naked) ImportTrap() {
    __asm {
        push dword ptr [esp]
        call TrapReport
    }
}

static char g_cipowName[16];
static bool MapImage(const u8* img) {
    const DWORD pe = *(const DWORD*)(img + 0x3C), base = *(const DWORD*)(img + pe + 0x34);
    const WORD ns = *(const WORD*)(img + pe + 6), oh = *(const WORD*)(img + pe + 20);
    if (base != IMG_BASE || *(const DWORD*)(img + pe + 0x50) != IMG_HI - IMG_BASE) { printf("not the Steam 1.67 exe\n"); return false; }
    if (VirtualAlloc((void*)(uintptr_t)IMG_BASE, IMG_HI - IMG_BASE, MEM_COMMIT, PAGE_EXECUTE_READWRITE) != (void*)(uintptr_t)IMG_BASE) {
        printf("cannot map the image at 0x%08lX (err %lu)\n", IMG_BASE, GetLastError());
        return false;
    }
    for (int i = 0; i < ns; i++) {
        const u8* s = img + pe + 24 + oh + i * 40;
        const DWORD rs = *(const DWORD*)(s + 16), vs = *(const DWORD*)(s + 8), n = rs < vs ? rs : vs;
        memcpy((void*)(uintptr_t)(base + *(const DWORD*)(s + 12)), img + *(const DWORD*)(s + 20), n);
    }
    const DWORD imp = base + *(const DWORD*)(img + pe + 0x80);
    for (const DWORD* d = (const DWORD*)(uintptr_t)imp; d[3]; d += 5) {
        DWORD* iat = (DWORD*)(uintptr_t)(base + d[4]);
        const DWORD* names = (const DWORD*)(uintptr_t)(base + (d[0] ? d[0] : d[4]));
        for (int j = 0; names[j]; j++) {
            const char* nm = (names[j] & 0x80000000) ? "" : (const char*)(uintptr_t)(base + names[j] + 2);
            void* f = (void*)ImportTrap;
            if (!strcmp(nm, "memmove")) f = (void*)memmove;
            else if (!strcmp(nm, "memcpy")) f = (void*)memcpy;
            else if (!strcmp(nm, "memset")) f = (void*)memset;
            else if (!strcmp(nm, "memcmp")) f = (void*)memcmp;
            else if (!strcmp(nm, "_CIpow")) f = (void*)_CIpow;  // U2: the same CRT helper (pow of ST(1), ST(0))
            if ((DWORD)(uintptr_t)(iat + j) == 0x00F956AC) strncpy_s(g_cipowName, nm, _TRUNCATE);
            iat[j] = (DWORD)(uintptr_t)f;
        }
    }
    return true;
}

static void Jmp(DWORD at, const void* to) {
    u8* p = (u8*)(uintptr_t)at;
    p[0] = 0xE9;
    *(LONG*)(p + 1) = (LONG)((const u8*)to - (p + 5));
}
template <class T> static T& F(const void* p, DWORD off) { return *(T*)((u8*)p + off); }
static void* A(size_t n) { void* p = _aligned_malloc(n ? n : 1, 16); memset(p, 0, n); return p; }

// --- fakes ---
static void* __fastcall FkMalloc(void*, void*, DWORD size, DWORD) { return _aligned_malloc(size ? size : 1, 16); }
static void* __fastcall FkMemalign(void*, void*, DWORD size, DWORD align, DWORD, DWORD) { return _aligned_malloc(size ? size : 1, align > 16 ? align : 16); }
static void __fastcall FkFree(void*, void*, void* p) { _aligned_free(p); }
static void* __cdecl FkPool(DWORD size) { return A(size); }  // the four routing pools are cdecl(size, ...): the caller pops

// ======================================================================================== U2
static DWORD g_esp;
static float g_rad;
static BYTE g_flag;
// cdecl(start, esi, edi, byte [esp+0xE]): enters InRoomPlan's body at `start` on a fresh 16-aligned frame
static void __declspec(naked) EnterBody() {
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov eax, [esp + 0x14]
        mov esi, [esp + 0x18]
        mov edi, [esp + 0x1C]
        mov ecx, [esp + 0x20]
        mov g_esp, esp
        sub esp, 0x400
        and esp, 0xFFFFFFF0
        mov byte ptr [esp + 0xE], cl
        jmp eax
    }
}
static void __declspec(naked) CapRadius() {  // at 0x633067 (both branches meet; eax -> 4 x radius)
    __asm {
        movss xmm0, dword ptr [eax]
        movss g_rad, xmm0
        mov esp, g_esp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}
static void __declspec(naked) CapFlag() {  // at 0x632F50 (the class test has just written [esp+0xE])
    __asm {
        mov al, byte ptr [esp + 0xE]
        mov g_flag, al
        mov esp, g_esp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}
typedef void(__cdecl* EnterFn)(DWORD, DWORD, DWORD, DWORD);

static void U2() {
    printf("\n== U2: nudge radius (InRoomPlan 0x632F18..0x63306A run for real)\n");
    printf("IAT 0x00F956AC (thunk 0x00F7EDF6) imports '%s'\n", g_cipowName);
    CHECK(!strcmp(g_cipowName, "_CIpow"), "IAT 0xF956AC is %s", g_cipowName);
    Jmp(0x00632F50, (void*)CapFlag);
    Jmp(0x00633067, (void*)CapRadius);
    static const DWORD kClass[] = {0x4100, 0x4200, 0x4300, 0x7100, 0x4000, 0x4400, 0x4500, 0x0100, 0x0300, 0x0800, 0x0B00, 0xC100, 0};
    printf("agent class word -> big-nudge flag:");
    for (int i = 0; i < (int)(sizeof(kClass) / 4); i++) {
        u8* x = (u8*)A(0x60);
        DWORD* agent = (DWORD*)A(0x40);
        agent[0] = kClass[i];
        F<DWORD*>(x, 0x40) = kClass[i] ? agent : NULL;
        g_flag = 0xEE;
        ((EnterFn)(void*)EnterBody)(0x00632F18, 0, (DWORD)(uintptr_t)x, 0xEE);
        const DWORD m = kClass[i] & 0xCF00;
        const BYTE want = kClass[i] && (m == 0x4100 || m == 0x4200 || m == 0x4300);
        printf(" %s%04lX=%u", kClass[i] ? "" : "no agent:", kClass[i], g_flag);
        CHECK(g_flag == want, "class %04lX flag %u", kClass[i], g_flag);
    }
    printf("\nring  i     radius (flag 0)  radius (flag 1)\n");
    for (DWORD i = 0; i < 64; i++) {
        float r[2];
        for (int fl = 0; fl < 2; fl++) {
            g_rad = -1;
            ((EnterFn)(void*)EnterBody)(0x00632F91, i, 0, (DWORD)fl);
            r[fl] = g_rad;
        }
        const DWORD ring = i / 8 + 1;
        CHECK(r[0] == 0.5f * (float)ring && r[1] == 0.5f * (float)(1u << ring), "i %lu: %g %g", i, r[0], r[1]);
        if (i % 8 == 0) printf("%4lu  %2lu-%2lu %10g m %15g m\n", ring, i, i + 7, r[0], r[1]);
    }
}

// ======================================================================================== U1
static const float* g_off;   // 0xFED500: per shape (x, z) offset in node units
static const float* g_size;  // 0xFED7A8: per shape side in node units
static const int* g_child;   // 0xFEE518: 4 children per shape
struct Rect { float x0, z0, x1, z1; };
static Rect PosRect(const u8* pos) {
    const u8* t = F<u8*>(pos, 4);
    const DWORD p = F<DWORD>(pos, 0x18), s = pos[0x1C];
    const float sc = F<float>(pos, 0x14);
    Rect r;
    r.x0 = ((float)(p & 0xFFF) + g_off[2 * s]) * sc + F<float>(t, 4);
    r.z0 = ((float)((p >> 16) & 0xFFF) + g_off[2 * s + 1]) * sc + F<float>(t, 8);
    r.x1 = r.x0 + g_size[s] * sc;
    r.z1 = r.z0 + g_size[s] * sc;
    return r;
}
static DWORD AllBits(int s) {
    if (s >= 21) return 0;
    DWORD b = 1u << s;
    for (int k = 0; k < 4; k++) b |= AllBits(g_child[4 * s + k]);
    return b;
}
static DWORD LeafBits(int s, int pFinal) {
    if (s >= 21) return 0;
    if ((int)Rnd(100) < pFinal) return AllBits(s);
    DWORD b = 0;
    for (int k = 0; k < 4; k++) b |= LeafBits(g_child[4 * s + k], pFinal);
    return b;
}
static u8* NewLeaf(DWORD bits) {
    u8* n = (u8*)A(0x14);
    F<DWORD>(n, 0) = 0x00FEE0B4;
    F<DWORD>(n, 4) = bits << 3;
    n[7] = 100;
    F<DWORD>(n, 8) = 1;  // empty heap list
    return n;
}
static u8* NewNode(int depth, int maxDepth, u8* parent, int pFinal) {
    if (depth == maxDepth || (int)Rnd(100) < (depth == 0 ? 10 : 30)) return NewLeaf(LeafBits(0, pFinal));
    u8* n = (u8*)A(0x1C);
    F<DWORD>(n, 0) = 0x00FEC0C0;
    F<DWORD>(n, 4) = 1;
    n[7] = 100;
    F<u8*>(n, 8) = parent;
    for (int k = 0; k < 4; k++) F<u8*>(n, 0xC + 4 * k) = Rnd(100) < 15 ? NULL : NewNode(depth + 1, maxDepth, n, pFinal);
    return n;
}
static u8* NewTree(u8* root, float side) {
    u8* t = (u8*)A(0x60);
    F<float>(t, 0xC) = side, F<float>(t, 0x10) = side;
    F<u8*>(t, 0x24) = root;
    F<DWORD>(t, 0x2C) = 12;
    F<float>(t, 0x30) = 1.0f;
    F<float>(t, 0x38) = side;
    F<DWORD>(t, 0x50) = 1;
    return t;
}
struct Vec { u8* b; u8* e; u8* c; DWORD al; };
static Vec* NewVec(DWORD bytes) {
    Vec* v = (Vec*)A(sizeof(Vec));
    v->b = v->e = (u8*)A(bytes);
    v->c = v->b + bytes;
    return v;
}
static const int MAXC = 4096;
struct Cells { int n; u8 pos[MAXC][0x24]; Rect r[MAXC]; };
static Cells g_cells;
static void Enumerate(u8* tree, int grid) {  // every final cell, by point lookups through the game's LotQuadTree_CellAtPoint
    g_cells.n = 0;
    const float side = F<float>(tree, 0x38), step = side / (float)grid;
    for (int j = 0; j < grid; j++)
        for (int i = 0; i < grid; i++) {
            u8 pos[0x24];
            ((void(__fastcall*)(u8*, void*))(uintptr_t)0x00669BE0)(pos, NULL);
            const u8 ok = ((u8(__fastcall*)(u8*, void*, u8*, float, float, DWORD))(uintptr_t)0x00663900)(tree, NULL, pos, (i + 0.5f) * step, (j + 0.5f) * step, 1);
            if (!ok) { CHECK(false, "CellAtPoint failed"); continue; }
            int k = 0;
            for (; k < g_cells.n; k++)
                if (F<DWORD>(g_cells.pos[k], 0x18) == F<DWORD>(pos, 0x18) && g_cells.pos[k][0x1C] == pos[0x1C]) break;
            if (k < g_cells.n) continue;
            if (g_cells.n == MAXC) { CHECK(false, "too many cells"); return; }
            memcpy(g_cells.pos[g_cells.n], pos, 0x24);
            g_cells.r[g_cells.n] = PosRect(pos);
            g_cells.n++;
        }
}
static float Ov(float a0, float a1, float b0, float b1) { return (a1 < b1 ? a1 : b1) - (a0 > b0 ? a0 : b0); }
// side of b relative to a: 0 -z, 1 +x, 2 +z, 3 -x, -1 not edge-adjacent
static int SideOf(const Rect& a, const Rect& b) {
    if (b.z1 == a.z0 && Ov(a.x0, a.x1, b.x0, b.x1) > 0) return 0;
    if (b.x0 == a.x1 && Ov(a.z0, a.z1, b.z0, b.z1) > 0) return 1;
    if (b.z0 == a.z1 && Ov(a.x0, a.x1, b.x0, b.x1) > 0) return 2;
    if (b.x1 == a.x0 && Ov(a.z0, a.z1, b.z0, b.z1) > 0) return 3;
    return -1;
}
// the wrap on side d inside box B (the root square; for a sub-cell of a root leaf, its parent shape): a on B's border of
// side d, b on B's opposite border, spans overlapping
static bool OnBorder(const Rect& a, int d, const Rect& B) { return d == 0 ? a.z0 == B.z0 : d == 1 ? a.x1 == B.x1 : d == 2 ? a.z1 == B.z1 : a.x0 == B.x0; }
static bool WrapD(const Rect& a, const Rect& b, const Rect& B, int d) {
    const bool inB = b.x0 >= B.x0 && b.x1 <= B.x1 && b.z0 >= B.z0 && b.z1 <= B.z1;
    const bool span = (d == 0 || d == 2) ? Ov(a.x0, a.x1, b.x0, b.x1) > 0 : Ov(a.z0, a.z1, b.z0, b.z1) > 0;
    return inB && span && OnBorder(a, d, B) && OnBorder(b, d ^ 2, B);
}
struct U1Stats { long trees, cells, calls, listed, mismatch, selfOnly, rootLeafSub, wrap[4], wrapAlsoTrue, borderCells, borderWrapped; };

static void Neighbours(u8* q, u8* pos, Vec* out, Vec* path, Vec* stack) {
    ((void(__fastcall*)(u8*, void*, u8*, Vec*, Vec*, Vec*))(uintptr_t)0x0066B9C0)(pos, NULL, q, out, path, stack);
}

// model of the list (docs/98 §6.2 plus the wrap): per side -z, +x, +z, -x the final cells on that edge, high end first; for
// a cell on the border of side d of the box (see WrapD), instead the cells on the box's opposite border whose span overlaps
// its own; a cell with no parent node and shape 0 (the root as one whole leaf) has none: 0x66B9C0 returns at once
static int Model(int a, const Rect& box, bool none, int* out, int* wrapSide) {
    const Rect& ra = g_cells.r[a];
    int n = 0;
    if (none) return 0;
    for (int d = 0; d < 4; d++) {
        const int first = n;
        const bool wrap = OnBorder(ra, d, box);
        for (int b = 0; b < g_cells.n; b++)
            if (wrap ? WrapD(ra, g_cells.r[b], box, d) : SideOf(ra, g_cells.r[b]) == d) wrapSide[n] = wrap ? d : -1, out[n++] = b;
        for (int i = first; i < n; i++)  // high end first: x descending on -z/+z, z descending on +x/-x
            for (int j = i + 1; j < n; j++) {
                const Rect &p = g_cells.r[out[i]], &c = g_cells.r[out[j]];
                if ((d == 0 || d == 2) ? c.x0 > p.x0 : c.z0 > p.z0) { const int t = out[i]; out[i] = out[j]; out[j] = t; }
            }
    }
    return n;
}

static void U1Tree(u8* tree, int grid, U1Stats* st, bool print) {
    const float S = F<float>(tree, 0x38);
    const u8* root = F<u8*>(tree, 0x24);
    Enumerate(tree, grid);
    const bool rootIsLeaf = root && (root[4] & 3) == 0;  // sub-cells of a root leaf: the climb stops at the parent shape
    const Rect sq = {0, 0, S, S};
    u8* q = (u8*)A(0x9C90);
    Vec* out = NewVec(4 * 8192);
    Vec* path = NewVec(4 * 256);
    Vec* stack = NewVec(0x24 * 4096);
    static int want[4 * MAXC], wside[4 * MAXC];
    st->trees++;
    st->cells += g_cells.n;
    for (int a = 0; a < g_cells.n; a++) {
        const Rect& ra = g_cells.r[a];
        Neighbours(q, g_cells.pos[a], out, path, stack);
        st->calls++;
        const u8 shape = g_cells.pos[a][0x1C];
        const bool none = F<u8*>(g_cells.pos[a], 8) == NULL && shape == 0;
        Rect box = sq;
        if (rootIsLeaf && shape) {
            u8 pp[0x24];
            memcpy(pp, g_cells.pos[a], 0x24);
            pp[0x1C] = (u8)F<DWORD>((void*)(uintptr_t)0x00FEEA68, 4 * shape);  // parentShape
            box = PosRect(pp);
        }
        const int n = (int)((out->e - out->b) / 4), m = Model(a, box, none, want, wside);
        st->listed += n;
        bool same = n == m, wrapped = false;
        if (print) printf("  [%g,%g]x[%g,%g]:", ra.x0, ra.x1, ra.z0, ra.z1);
        for (int k = 0; k < n; k++) {
            const u8* c = F<u8*>(out->b, 4 * k);
            int b = 0;
            for (; b < g_cells.n; b++)
                if (F<DWORD>(g_cells.pos[b], 0x18) == F<DWORD>(c, 0x18) && g_cells.pos[b][0x1C] == c[0x1C]) break;
            CHECK(b < g_cells.n, "returned cell is not a final cell of the tree");
            if (b == g_cells.n) { same = false; continue; }
            if (k >= m || want[k] != b) same = false;
            const Rect& rb = g_cells.r[b];
            if (print) printf(" [%g,%g]x[%g,%g]%s", rb.x0, rb.x1, rb.z0, rb.z1, b == a ? "(self)" : k < m && wside[k] >= 0 ? "(wrap)" : "");
        }
        if (print) printf("\n");
        if (none) st->selfOnly++;
        if (rootIsLeaf && shape) st->rootLeafSub++;
        for (int k = 0; k < m; k++)
            if (wside[k] >= 0) {
                st->wrap[wside[k]]++, wrapped = true;
                if (SideOf(ra, g_cells.r[want[k]]) >= 0) st->wrapAlsoTrue++;
            }
        if (!same && ++st->mismatch <= 3) {
            printf("  mismatch: cell [%g,%g]x[%g,%g] shape %u: game", ra.x0, ra.x1, ra.z0, ra.z1, g_cells.pos[a][0x1C]);
            for (int k = 0; k < n; k++) { const Rect rb = PosRect(F<u8*>(out->b, 4 * k)); printf(" [%g,%g]x[%g,%g]", rb.x0, rb.x1, rb.z0, rb.z1); }
            printf("\n    model");
            for (int k = 0; k < m; k++) { const Rect& rb = g_cells.r[want[k]]; printf(" [%g,%g]x[%g,%g]%s", rb.x0, rb.x1, rb.z0, rb.z1, wside[k] >= 0 ? "(wrap)" : ""); }
            printf("\n");
        }
        if (!rootIsLeaf && (ra.x0 == 0 || ra.z0 == 0 || ra.x1 == S || ra.z1 == S)) { st->borderCells++; if (wrapped) st->borderWrapped++; }
    }
}

static void U1() {
    printf("\n== U1: QuadCell_Neighbours 0x66B9C0 on every final cell\n");
    g_off = (const float*)(uintptr_t)0x00FED500;
    g_size = (const float*)(uintptr_t)0x00FED7A8;
    g_child = (const int*)(uintptr_t)0x00FEE518;
    CHECK(g_child[0] == 1 && g_child[3] == 4 && g_child[4] == 5 && g_child[7] == 10 && g_size[0] == 1.0f && g_size[21] == 0.125f, "shape tables");
    // the smallest case: root internal, four whole leaves
    {
        u8* r = (u8*)A(0x1C);
        F<DWORD>(r, 0) = 0x00FEC0C0, F<DWORD>(r, 4) = 1, r[7] = 100;
        for (int k = 0; k < 4; k++) F<u8*>(r, 0xC + 4 * k) = NewLeaf(AllBits(0));
        U1Stats st = {};
        printf("2 x 2 tree, side 64 (wrap entries marked):\n");
        U1Tree(NewTree(r, 64), 8, &st, true);
        CHECK(st.wrap[0] + st.wrap[1] + st.wrap[2] + st.wrap[3] == 8 && st.mismatch == 0, "2x2 case");
        // root is a single whole leaf: no parent node and shape 0 -> 0x66B9C0 returns an empty list
        U1Stats s1 = {};
        printf("single whole leaf as root:\n");
        U1Tree(NewTree(NewLeaf(AllBits(0)), 64), 4, &s1, true);
        CHECK(s1.selfOnly == 1 && s1.listed == 0 && s1.mismatch == 0, "root leaf: no neighbours");
    }
    U1Stats st = {};
    const int trees = g_trees;
    for (int t = 0; t < trees; t++) {
        const int maxDepth = 1 + (int)Rnd(2);
        u8* tree = NewTree(NewNode(0, maxDepth, NULL, 20 + (int)Rnd(70)), 64);
        U1Tree(tree, 8 << maxDepth, &st, false);
    }
    printf("random trees %ld (depth 1-2, leaves split down to eighths), final cells %ld, calls %ld, entries listed %ld\n", st.trees, st.cells, st.calls, st.listed);
    printf("  calls whose list differs from the model (order included): %ld\n", st.mismatch);
    printf("  wrap entries by side -z/+x/+z/-x: %ld %ld %ld %ld (%ld of them are also a true neighbour on the other side)\n",
           st.wrap[0], st.wrap[1], st.wrap[2], st.wrap[3], st.wrapAlsoTrue);
    printf("  root-border cells %ld, all with wrap entries: %s; root as one whole leaf (no neighbours) %ld; sub-cells of a root leaf (wrap inside the parent shape) %ld\n",
           st.borderCells, st.borderWrapped == st.borderCells ? "yes" : "no", st.selfOnly, st.rootLeafSub);
    CHECK(st.mismatch == 0 && st.borderWrapped == st.borderCells, "neighbour model");

    // root size vs bounds: LotQuadTree_SetBounds 0x6646F0 as LotRouting_QuadTreeBuild calls it (rect grown by c, cell size c)
    printf("LotQuadTree_SetBounds 0x6646F0 (lot rect grown by the cell size c, as the build call passes it):\n");
    printf("  lot w x d   c     bounds minX..maxX, minZ..maxZ     root side  +0x40   +x / +z root border inside the bounds\n");
    static const float kLot[][2] = {{20, 30}, {30, 30}, {40, 40}, {50, 50}, {60, 60}, {64, 64}, {30, 62}, {62, 62}};
    static const float kCell[] = {0.5f, 1.0f};
    for (int c = 0; c < 2; c++)
        for (int l = 0; l < 8; l++) {
            u8* t = (u8*)A(0x60);
            const float cs = kCell[c], w = kLot[l][0], d = kLot[l][1];
            ((void(__fastcall*)(u8*, void*, void*, float, float, float, float, float, DWORD, DWORD, DWORD))(uintptr_t)0x006646F0)(
                t, NULL, NULL, -cs, -cs, w + cs, d + cs, cs, 0, 0, 0);
            const float x0 = F<float>(t, 4), z0 = F<float>(t, 8), x1 = F<float>(t, 0xC), z1 = F<float>(t, 0x10), side = F<float>(t, 0x38);
            printf("  %3g x %3g  %4g  %6g..%-6g %6g..%-6g  %9g  %5ld   %s / %s\n", w, d, cs, x0, x1, z0, z1, side, F<LONG>(t, 0x40),
                   x0 + side <= x1 ? "yes" : "no", z0 + side <= z1 ? "yes" : "no");
            CHECK(side >= x1 - x0 && side >= z1 - z0 && F<DWORD>(t, 0x2C) == 12, "root covers the bounds, default class 12");
        }
}

// ======================================================================================== U3
static u8* g_world;
static const DWORD RO_LO = 0x28, RO_HI = 0x2C, RO2_LO = 0x30;
static void SetVec(u8* o, DWORD off, const DWORD (*ids)[2], int n, bool sorted) {
    DWORD* b = (DWORD*)A(8 * (n ? n : 1));
    for (int i = 0; i < n; i++) b[2 * i] = ids[i][0], b[2 * i + 1] = ids[i][1];
    if (sorted)  // the agent's id set is a sorted vector (0x626AA0: lower bound by hi, then lo)
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                if (b[2 * j + 1] < b[2 * i + 1] || (b[2 * j + 1] == b[2 * i + 1] && b[2 * j] < b[2 * i])) {
                    DWORD t0 = b[2 * i], t1 = b[2 * i + 1];
                    b[2 * i] = b[2 * j], b[2 * i + 1] = b[2 * j + 1], b[2 * j] = t0, b[2 * j + 1] = t1;
                }
    F<DWORD*>(o, off) = b;
    F<DWORD*>(o, off + 4) = b + 2 * n;
}
struct Agent { u8* a; u8* o; };
static Agent NewAgent(DWORD cls, DWORD idLo, const DWORD (*held)[2], int nHeld, const DWORD (*ign)[2], int nIgn, DWORD roLo, DWORD roHi, DWORD ro2Lo) {
    Agent g;
    g.a = (u8*)A(0x40);
    g.o = (u8*)A(0x60);
    F<DWORD>(g.a, 0) = cls;
    F<DWORD>(g.a, 8) = idLo;  // the agent's own object id (+0x08/+0x0C)
    F<u8*>(g.a, 0x28) = g.o;
    F<DWORD>(g.o, 0x20) = idLo;  // options +0x20/+0x24: the agent's id again (0x81CEF0 matches it first)
    SetVec(g.o, 0x08, held, nHeld, true);
    SetVec(g.o, 0x38, ign, nIgn, false);
    F<DWORD>(g.o, RO_LO) = roLo, F<DWORD>(g.o, RO_HI) = roHi, F<DWORD>(g.o, RO2_LO) = ro2Lo;
    return g;
}
static u8* NewFp(DWORD vt, u8 cls, u8 type, DWORD idLo, float mult) {
    u8* f = (u8*)A(0x80);
    F<DWORD>(f, 0) = vt;
    F<DWORD>(f, 0x14) = idLo;
    F<float>(f, 0x1C) = mult;
    f[0x34] = cls;
    f[0x35] = type;
    F<u8*>(f, 0x24) = f + 0x24;  // empty list
    return f;
}
typedef LONG(__fastcall* ClassFn)(u8*, void*, u8*, DWORD);
typedef float(__fastcall* MultFn)(u8*, void*, u8*, DWORD);
static LONG Cls(DWORD fn, u8* fp, u8* ag, DWORD sub) { return ((ClassFn)(uintptr_t)fn)(fp, NULL, ag, sub); }
static float Mul(DWORD fn, u8* fp, u8* ag, DWORD sub) { return ((MultFn)(uintptr_t)fn)(fp, NULL, ag, sub); }

static void U3() {
    printf("\n== U3: footprint class / multiplier slots and the allow/deny gate\n");
    g_world = (u8*)A(0x40);
    *(u8**)(uintptr_t)0x011D9128 = g_world;  // RoutingWorld_Get 0x8292D0 -> 0x836200 reads it; options come from agent +0x28
    // gate (fp+0x20): +0x08..+0x0C list A, +0x1C..+0x20 list B, 8-byte ids
    struct G { const char* name; const DWORD (*a)[2]; int na; const DWORD (*b)[2]; int nb; };
    const DWORD XY[2][2] = {{0x111, 0}, {0x222, 0}};
    const DWORD XX[1][2] = {{0x111, 0}}, YY[1][2] = {{0x222, 0}}, ZZ[1][2] = {{0, 0}};
    const G gates[] = {{"none", NULL, 0, NULL, 0}, {"A={X}", XX, 1, NULL, 0}, {"A={X,Y}", XY, 2, NULL, 0}, {"B={X}", NULL, 0, XX, 1},
                       {"A={X} B={Y}", XX, 1, YY, 1}, {"A={X} B={X}", XX, 1, XX, 1}, {"A={0}", ZZ, 1, NULL, 0}, {"B={0}", NULL, 0, ZZ, 1}};
    struct Ag { const char* name; int kind; };  // 0 null agent, 1 options NULL, 2 holds nothing, 3 holds X, 4 holds X and Y, 5 own id = X
    const Ag ags[] = {{"no agent", 0}, {"agent, no options", 1}, {"holds {}", 2}, {"holds {X}", 3}, {"holds {X,Y}", 4}, {"own id X", 5}};
    printf("0x946E90 result (1 = excluded) / class of base slot 0x807090 (byte +0x34 = 3):\n  %-12s", "gate");
    for (const Ag& a : ags) printf(" | %-17s", a.name);
    printf("\n");
    for (const G& g : gates) {
        u8* gate = (u8*)A(0x40);
        SetVec(gate, 0x08, g.a, g.na, false);
        SetVec(gate, 0x1C, g.b, g.nb, false);
        u8* fp = NewFp(0x00FEBE38, 3, 3, 0x900, 2.0f);
        F<u8*>(fp, 0x20) = gate;
        const u8 has = ((u8(__fastcall*)(u8*, void*))(uintptr_t)0x00946E70)(gate, NULL);
        CHECK(has == (g.na || g.nb), "0x946E70");
        printf("  %-12s", g.name);
        for (const Ag& a : ags) {
            u8* agent = NULL;
            int nh = a.kind == 3 ? 1 : a.kind == 4 ? 2 : 0;
            if (a.kind >= 1) {
                Agent x = NewAgent(0x100, a.kind == 5 ? 0x111 : 0x500, XY, nh, NULL, 0, 0, 0, 0);
                if (a.kind == 1) F<u8*>(x.a, 0x28) = NULL;
                agent = x.a;
            }
            const u8 ex = ((u8(__fastcall*)(u8*, void*, u8*))(uintptr_t)0x00946E90)(gate, NULL, agent);
            const LONG c = Cls(0x00807090, fp, agent, 0);
            // model: A non-empty -> excluded if no options or any A id not held (id 0 is never held; own id counts);
            //        B non-empty -> excluded if options and any B id held; class 6 if excluded, else 0; no agent: byte
            auto held = [&](const DWORD* id) { if (!id[0] && !id[1]) return false; if (a.kind == 5 && id[0] == 0x111) return true;
                                              for (int i = 0; i < nh; i++) if (XY[i][0] == id[0] && XY[i][1] == id[1]) return true; return false; };
            bool want = false;
            const bool opts = a.kind >= 2;
            if (g.na) { if (!opts) want = true; else for (int i = 0; i < g.na; i++) if (!held(g.a[i])) want = true; }
            if (!want && g.nb && opts) for (int i = 0; i < g.nb; i++) if (held(g.b[i])) want = true;
            if (!g.na && !g.nb) want = false;
            const LONG wc = !agent || !has ? 3 : want ? 6 : 0;
            CHECK(ex == (u8)want && c == wc, "gate %s agent %s: %u/%ld want %u/%ld", g.name, a.name, ex, c, want, wc);
            printf(" | %u / class %-7ld", ex, c);
        }
        printf("\n");
    }
    // ignore list first: holds the footprint's object id -> class 0 even with a gate
    {
        const DWORD id[1][2] = {{0x900, 0}};
        Agent x = NewAgent(0x100, 0x500, NULL, 0, id, 1, 0, 0, 0);
        u8* fp = NewFp(0x00FEBE38, 3, 3, 0x900, 2.0f);
        const LONG c = Cls(0x00807090, fp, x.a, 0);
        CHECK(c == 0, "ignore list");
        printf("ignore list holds the footprint's id: class %ld (byte 3); multiplier slot 0x806F70: own object %g, other %g, no agent %g (fp+0x1C = 2)\n", c,
               Mul(0x00806F70, NewFp(0x00FEBE38, 3, 3, 0x500, 2.0f), x.a, 0), Mul(0x00806F70, fp, x.a, 0), Mul(0x00806F70, fp, NULL, 0));
    }
    // object footprints 0xFEBBC0: class 0x80F840 / multiplier 0x80F7D0, fp+0x40/+0x44 = the one agent they apply to
    printf("object footprint 0xFEBBC0 (byte 5, fp+0x1C = 4): agent id vs fp+0x40, RouteOption IgnorePrivateFootprints (hi 0x01000000) -> class, mult\n");
    for (int m = 0; m < 2; m++)
        for (int p = 0; p < 2; p++) {
            Agent x = NewAgent(0x100, 0x500, NULL, 0, NULL, 0, 0, p ? 0x01000000 : 0, 0);
            u8* fp = NewFp(0x00FEBBC0, 5, 3, 0x900, 4.0f);
            F<DWORD>(fp, 0x40) = m ? 0x500 : 0x777;
            const LONG c = Cls(0x0080F840, fp, x.a, 0);
            const float mu = Mul(0x0080F7D0, fp, x.a, 0);
            const bool on = m && !p;
            CHECK(c == (on ? 5 : 0) && mu == (on ? 4.0f : 1.0f), "object fp");
            printf("  agent %s fp+0x40, IgnorePrivateFootprints %s: class %ld, mult %g\n", m ? "==" : "!=", p ? "set" : "clear", c, mu);
        }
    {
        u8* fp = NewFp(0x00FEBBC0, 5, 3, 0x900, 4.0f);
        printf("  no agent: class %ld, mult %g\n", Cls(0x0080F840, fp, NULL, 0), Mul(0x0080F7D0, fp, NULL, 0));
        CHECK(Cls(0x0080F840, fp, NULL, 0) == 0 && Mul(0x0080F7D0, fp, NULL, 0) == 1.0f, "object fp no agent");
    }
    // 0xFEB848 / 0xFEB970: 0x80C300 class, 0x80C3D0 multiplier; fp+0x41 bits; agent class word; PreventCatsRoutingThroughPlants
    // (hi 0x10000), RouteAsFlier (hi 0x20000)
    printf("0xFEB848 class 0x80C300 / mult 0x80C3D0 (byte 5, fp+0x1C = 4), sub-index 0..3:\n");
    static const u8 kBits[] = {0, 1, 2, 4, 8, 0xC};
    static const DWORD kCls[] = {0x300, 0x100};
    static const DWORD kOpt[] = {0, 0x10000, 0x20000};
    static const char* kOptN[] = {"-", "PreventCats", "Flier"};
    for (u8 bits : kBits)
        for (DWORD cl : kCls)
            for (int o = 0; o < 3; o++) {
                Agent x = NewAgent(cl, 0x500, NULL, 0, NULL, 0, 0, kOpt[o], 0);
                u8* fp = NewFp(0x00FEB848, 5, 3, 0x900, 4.0f);
                fp[0x41] = bits;
                LONG c[4];
                float mu[4];
                for (DWORD s = 0; s < 4; s++) c[s] = Cls(0x0080C300, fp, x.a, s), mu[s] = Mul(0x0080C3D0, fp, x.a, s);
                const bool pass = ((bits & 1) && cl == 0x300 && o != 1) || ((bits & 2) && o == 2) || (bits & 0xC) == 0xC;
                for (DWORD s = 0; s < 4; s++) {
                    const LONG wc = pass ? 0 : s == 0 || s == 2 ? 8 : s == 1 ? 5 : 0;
                    const float wm = ((bits & 2) && o == 2) || s == 0 || s == 2 ? 1.0f : 4.0f;
                    CHECK(c[s] == wc && mu[s] == wm, "0x80C300 bits %x cls %lx opt %d sub %lu: %ld %g", bits, cl, o, s, c[s], mu[s]);
                }
                if (cl == 0x300 || bits & 2)
                    printf("  +0x41=%-3X agent %03lX %-11s class %ld %ld %ld %ld  mult %g %g %g %g\n", bits, cl, kOptN[o], c[0], c[1], c[2], c[3], mu[0], mu[1], mu[2], mu[3]);
            }
    {
        Agent x = NewAgent(0x100, 0x500, NULL, 0, NULL, 0, 0, 0, 0);
        const LONG own = Cls(0x008066C0, NewFp(0x00FEB970, 5, 3, 0x500, 4.0f), x.a, 0), other = Cls(0x008066C0, NewFp(0x00FEB970, 5, 3, 0x501, 4.0f), x.a, 0);
        CHECK(own == 0 && other == 5, "0x8066C0");
        printf("0xFEB970 class 0x8066C0: own object %ld, other %ld\n", own, other);
    }
    // 0xFEBD10: 0x807B70 / 0x807C20 by type; options EnablePondPlanning (hi 0x20000000), EnableWaterPlanning (hi 0x10000000),
    // RouteOption2 EnablePlanningAsBoat (2)
    printf("0xFEBD10 class 0x807B70 / mult 0x807C20 (byte 3, fp+0x1C = 4):\n");
    static const u8 kTypes[] = {0x1A, 0x1B, 0x1E, 0x03};
    struct O { const char* n; DWORD hi, o2; };
    static const O kO[] = {{"-", 0, 0}, {"Pond", 0x20000000, 0}, {"Water", 0x10000000, 0}, {"Boat(RO2)", 0, 2}};
    for (u8 ty : kTypes) {
        printf("  type %02X:", ty);
        for (const O& o : kO) {
            Agent x = NewAgent(0x100, 0x500, NULL, 0, NULL, 0, 0, o.hi, o.o2);
            u8* fp = NewFp(0x00FEBD10, 3, ty, 0x900, 4.0f);
            const LONG c = Cls(0x00807B70, fp, x.a, 0);
            const float mu = Mul(0x00807C20, fp, x.a, 0);
            const bool water = o.hi == 0x10000000 || o.o2 == 2;
            const LONG wc = (ty == 0x1A || ty == 0x1B) ? (o.hi == 0x20000000 ? 0 : 3) : ty == 0x1E ? (water ? 3 : 10) : 3;
            const float wm = ty == 0x1E && !water ? 3.4028235e38f : 4.0f;
            CHECK(c == wc && mu == wm, "0x807B70 type %x opt %s: %ld %g", ty, o.n, c, mu);
            printf("  %s: %ld, %g", o.n, c, mu);
        }
        printf("\n");
    }
    // 0x81CEF0 on its own: id 0 never held, the agent's own id always
    {
        const DWORD h[2][2] = {{0x300, 1}, {0x111, 0}};
        Agent x = NewAgent(0x100, 0x500, h, 2, NULL, 0, 0, 0, 0);
        typedef u8(__fastcall * HasFn)(u8*, void*, DWORD, DWORD);
        const HasFn has = (HasFn)(uintptr_t)0x0081CEF0;
        const u8 r[5] = {has(x.o, NULL, 0, 0), has(x.o, NULL, 0x500, 0), has(x.o, NULL, 0x300, 1), has(x.o, NULL, 0x300, 0), has(x.o, NULL, 0x111, 0)};
        CHECK(!r[0] && r[1] && r[2] && !r[3] && r[4], "0x81CEF0");
        printf("0x81CEF0: id 0 -> %u, own id -> %u, held {0x300,1} -> %u, {0x300,0} -> %u, held {0x111,0} -> %u\n", r[0], r[1], r[2], r[3], r[4]);
    }
}

// ======================================================================================== U5 helpers (also used by U4)
struct Poly { int n; float v[16][2]; };
// Sutherland-Hodgman: polygon clipped to the rectangle; |area| of the result (exact enough for convex and L-shapes)
static double ClipArea(const Poly& p, const Rect& r) {
    double a[64][2], b[64][2];
    int n = p.n;
    for (int i = 0; i < n; i++) a[i][0] = p.v[i][0], a[i][1] = p.v[i][1];
    for (int e = 0; e < 4 && n; e++) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            const double* s = a[(i + n - 1) % n];
            const double* t = a[i];
            auto in = [&](const double* q) { return e == 0 ? q[0] >= r.x0 : e == 1 ? q[0] <= r.x1 : e == 2 ? q[1] >= r.z0 : q[1] <= r.z1; };
            auto cut = [&](double* o) {
                const double k = e < 2 ? ((e == 0 ? r.x0 : r.x1) - s[0]) / (t[0] - s[0]) : ((e == 2 ? r.z0 : r.z1) - s[1]) / (t[1] - s[1]);
                o[0] = s[0] + k * (t[0] - s[0]), o[1] = s[1] + k * (t[1] - s[1]);
            };
            if (in(t)) {
                if (!in(s)) cut(b[m++]);
                b[m][0] = t[0], b[m][1] = t[1], m++;
            } else if (in(s)) cut(b[m++]);
        }
        n = m;
        memcpy(a, b, sizeof(double) * 2 * m);
    }
    double ar = 0;
    for (int i = 0; i < n; i++) ar += a[i][0] * a[(i + 1) % n][1] - a[(i + 1) % n][0] * a[i][1];
    return fabs(ar) * 0.5;
}
static Poly RandPoly(int kind) {  // 0 convex CCW, 1 convex CW, 2 L-shape, 3 axis box
    Poly p = {};
    const float cx = Rf(2, 14), cz = Rf(2, 14);
    if (kind == 3) {
        const float w = Rf(0.5f, 8), h = Rf(0.5f, 8);
        const float v[4][2] = {{cx - w / 2, cz - h / 2}, {cx + w / 2, cz - h / 2}, {cx + w / 2, cz + h / 2}, {cx - w / 2, cz + h / 2}};
        p.n = 4;
        memcpy(p.v, v, sizeof(v));
        return p;
    }
    if (kind == 2) {
        const float w = Rf(2, 8), h = Rf(2, 8), t = Rf(0.5f, 1.5f);
        const float v[6][2] = {{cx, cz}, {cx + w, cz}, {cx + w, cz + t}, {cx + t, cz + t}, {cx + t, cz + h}, {cx, cz + h}};
        p.n = 6;
        memcpy(p.v, v, sizeof(v));
        return p;
    }
    p.n = 3 + (int)Rnd(5);
    float ang[8];
    for (int i = 0; i < p.n; i++) ang[i] = Rf(0, 6.2831853f);
    for (int i = 0; i < p.n; i++)
        for (int j = i + 1; j < p.n; j++) if (ang[j] < ang[i]) { float t = ang[i]; ang[i] = ang[j]; ang[j] = t; }
    const float rx = Rf(0.5f, 6), rz = Rf(0.5f, 6), rot = Rf(0, 3.14159f);
    for (int i = 0; i < p.n; i++) {
        const float x = rx * cosf(ang[i]), z = rz * sinf(ang[i]);
        p.v[i][0] = cx + x * cosf(rot) - z * sinf(rot);
        p.v[i][1] = cz + x * sinf(rot) + z * cosf(rot);
    }
    if (kind == 1)
        for (int i = 0; i < p.n / 2; i++) for (int k = 0; k < 2; k++) { float t = p.v[i][k]; p.v[i][k] = p.v[p.n - 1 - i][k]; p.v[p.n - 1 - i][k] = t; }
    return p;
}
// polygon footprint: cache entry 0 (fp+0x38): polygon records {verts, verts end, -, -}, bbox +0x10..+0x1C, valid +0x20
static u8* PolyFp(const Poly* ps, int np, DWORD vt) {
    u8* f = NewFp(vt, 3, 3, 0x900, 1.0f);
    u8* cache = (u8*)A(0x24 * 3);
    u8* recs = (u8*)A(16 * np);
    float bx0 = 1e30f, bz0 = 1e30f, bx1 = -1e30f, bz1 = -1e30f;
    for (int i = 0; i < np; i++) {
        float* v = (float*)A(8 * ps[i].n);
        for (int k = 0; k < ps[i].n; k++) {
            v[2 * k] = ps[i].v[k][0], v[2 * k + 1] = ps[i].v[k][1];
            bx0 = min(bx0, v[2 * k]), bx1 = max(bx1, v[2 * k]), bz0 = min(bz0, v[2 * k + 1]), bz1 = max(bz1, v[2 * k + 1]);
        }
        F<float*>(recs, 16 * i) = v;
        F<float*>(recs, 16 * i + 4) = v + 2 * ps[i].n;
    }
    F<u8*>(cache, 0) = recs;
    F<u8*>(cache, 4) = recs + 16 * np;
    F<float>(cache, 0x10) = bx0, F<float>(cache, 0x14) = bz0, F<float>(cache, 0x18) = bx1, F<float>(cache, 0x1C) = bz1;
    cache[0x20] = 1;
    F<u8*>(f, 0x38) = cache;
    return f;
}
typedef u8(__fastcall* RectFn)(u8*, void*, DWORD, float, float, float, float, float);
static u8 RectTest(DWORD fn, u8* fp, const Rect& r, float grow) { return ((RectFn)(uintptr_t)fn)(fp, NULL, 0, r.x0, r.z0, r.x1, r.z1, grow); }

// ======================================================================================== U4
static void U4() {
    printf("\n== U4: sub-cell masks\n");
    const int* bitOf = (const int*)(uintptr_t)0x00FEDE48;
    ((void(__cdecl*)())(uintptr_t)0x00667740)();
    const DWORD* sm = (const DWORD*)(uintptr_t)0x011CF180;  // per shape 0..20: the 64-bit set of eighths it covers
    printf("shape: eighths covered (bit = 8*row(z) + col(x)), bitOfShape 0xFEDE48\n");
    for (int s = 0; s < 85; s++) {
        unsigned long long m = s < 21 ? ((unsigned long long)sm[2 * s + 1] << 32 | sm[2 * s]) : 1ull << (s - 21);
        // model: the shape's square in eighths, from the shape tables
        const int c0 = (int)(g_off[2 * s] * 8), r0 = (int)(g_off[2 * s + 1] * 8), w = (int)(g_size[s] * 8);
        unsigned long long want = 0;
        for (int r = r0; r < r0 + w; r++) for (int c = c0; c < c0 + w; c++) want |= 1ull << (8 * r + c);
        int lo = 0;
        while (lo < 64 && !(m >> lo & 1)) lo++;
        CHECK(m == want && bitOf[s] == lo, "shape %d mask %016llX want %016llX bitOf %d", s, m, want, bitOf[s]);
        if (s < 21 || s == 21 || s == 84) printf("  %2d: %016llX  bitOfShape %2d (lowest eighth)\n", s, m, bitOf[s]);
    }
    // the eighth-mask writer 0x662FF0(cell, entry, mask*): one slot +0x34 test per eighth of the cell
    u8* tree = NewTree(NULL, 64);
    long cases = 0, bitsSet = 0, edgeOnly = 0;
    for (int t = 0; t < 400; t++) {
        const Poly p = RandPoly((int)Rnd(4));
        u8* fp = PolyFp(&p, 1, 0x00FEBE38);
        u8* entry = (u8*)A(0x20);
        F<DWORD>(entry, 0) = 0x00FEAC7C;
        F<u8*>(entry, 8) = fp;
        u8 pos[0x24];
        ((void(__fastcall*)(u8*, void*))(uintptr_t)0x00669BE0)(pos, NULL);
        F<u8*>(pos, 4) = tree;
        F<float>(pos, 0x14) = 16.0f;  // a 16 m leaf at (0, 0): eighths of 2 m
        DWORD mask[2] = {0xDEAD, 0xBEEF};
        ((void(__cdecl*)(u8*, u8*, DWORD*))(uintptr_t)0x00662FF0)(pos, entry, mask);
        for (int i = 0; i < 64; i++) {
            const Rect e = {(float)(i % 8) * 2, (float)(i / 8) * 2, (float)(i % 8) * 2 + 2, (float)(i / 8) * 2 + 2};
            const bool bit = (mask[i / 32] >> (i % 32)) & 1, slot = RectTest(0x0080F0D0, fp, e, 0) != 0;
            const double ar = ClipArea(p, e);
            CHECK(bit == slot, "mask bit %d vs slot", i);
            if (bit) bitsSet++;
            if (bit && ar <= 1e-9) edgeOnly++;
            CHECK(!(ar > 1e-6 && !bit), "eighth %d overlaps by %g but bit clear", i, ar);
        }
        cases++;
    }
    printf("0x662FF0 on %ld random polygons over a 16 m leaf: bit i == slot +0x34 on eighth i (col i%%8 in x, row i/8 in z) in every case; %ld bits set, %ld of them for eighths the polygon only touches\n",
           cases, bitsSet, edgeOnly);
    // the leaf final-bit update 0x667BA0(leaf)(entry, mask*): shape s stays final only if every entry covers s fully or not at all
    long adds = 0, cleared = 0;
    for (int t = 0; t < 2000; t++) {
        u8* leaf = NewLeaf(AllBits(0));
        DWORD model = AllBits(0);
        const int n = 1 + (int)Rnd(4);
        unsigned long long masks[4];
        for (int k = 0; k < n; k++) {
            unsigned long long m = 0;
            const int kind = (int)Rnd(3);
            if (kind == 0) m = ~0ull;
            else if (kind == 1) { const int s = (int)Rnd(21); m = (unsigned long long)sm[2 * s + 1] << 32 | sm[2 * s]; }
            else m = ((unsigned long long)Rnd(0x10000) << 48) | ((unsigned long long)Rnd(0x10000) << 32) | ((unsigned long long)Rnd(0x10000) << 16) | Rnd(0x10000);
            masks[k] = m;
            u8* entry = (u8*)A(0x20);
            F<DWORD>(entry, 0) = 0x00FEAC7C;
            DWORD mm[2] = {(DWORD)m, (DWORD)(m >> 32)};
            ((void(__fastcall*)(u8*, void*, u8*, DWORD*))(uintptr_t)0x00667BA0)(leaf, NULL, entry, mm);
            for (int s = 0; s < 21; s++) {
                const unsigned long long sh = (unsigned long long)sm[2 * s + 1] << 32 | sm[2 * s];
                if ((m & sh) && (m & sh) != sh) model &= ~(1u << s);
            }
            adds++;
        }
        const DWORD got = (F<DWORD>(leaf, 4) >> 3) & 0x1FFFFF;
        CHECK(got == model, "0x667BA0 bits %06lX want %06lX", got, model);
        CHECK(F<DWORD>(leaf, 0xC) == (DWORD)n || n == 1, "leaf count");
        for (DWORD x = AllBits(0) & ~got; x; x &= x - 1) cleared++;
        // the masked class rule (0x667DB0 tests bit bitOfShape[s] only) is exact for every shape still final
        for (int s = 0; s < 21; s++)
            if (got >> s & 1)
                for (int k = 0; k < n; k++) {
                    const unsigned long long sh = (unsigned long long)sm[2 * s + 1] << 32 | sm[2 * s];
                    CHECK(((masks[k] >> bitOf[s]) & 1) == ((masks[k] & sh) != 0), "corner bit decides shape %d", s);
                }
    }
    printf("0x667BA0: %ld entry adds on fresh leaves (final bits 0xFFFFF8): the final bit of shape s is cleared exactly when an entry covers s partly (%ld bits cleared); for every shape left final the bit bitOfShape[s] decides membership\n",
           adds, cleared);
}

// ======================================================================================== U5
static void U5() {
    printf("\n== U5: footprint rectangle tests\n");
    long n = 0, agree = 0, touchTrue = 0, touchFalse = 0, inNoArea = 0, cn = 0, cAgree = 0;
    for (int t = 0; t < 20000; t++) {
        const Poly p = RandPoly((int)Rnd(4));
        u8* fp = PolyFp(&p, 1, 0x00FEBE38);
        Rect r;
        if (Rnd(4) == 0) {  // exact touches: share an edge or a corner with the polygon's bbox
            const float* b = (const float*)(F<u8*>(fp, 0x38) + 0x10);
            const float w = Rf(0.2f, 3), h = Rf(0.2f, 3);
            switch (Rnd(4)) {
                case 0: r = {b[2], b[1], b[2] + w, b[1] + h}; break;
                case 1: r = {b[0] - w, b[1], b[0], b[1] + h}; break;
                case 2: r = {b[0], b[3], b[0] + w, b[3] + h}; break;
                default: r = {b[2], b[3], b[2] + w, b[3] + h}; break;
            }
        } else {
            const float x = Rf(0, 16), z = Rf(0, 16);
            r = {x, z, x + Rf(0.1f, 6), z + Rf(0.1f, 6)};
        }
        const u8 got = RectTest(0x0080F0D0, fp, r, 0);
        const double ar = ClipArea(p, r);
        const double rectA = (double)(r.x1 - r.x0) * (r.z1 - r.z0);
        n++;
        if (ar > 1e-5) { agree += got != 0; CHECK(got, "positive overlap %g reported false", ar); }
        else if (ar > 0) inNoArea++;
        else { if (got) touchTrue++; else { touchFalse++; agree++; } }
        const u8 in = RectTest(0x0080F200, fp, r, 0);
        cn++;
        const bool wantIn = ar >= rectA * (1 - 1e-5);
        if ((in != 0) == wantIn) cAgree++;
        else if (fabs(ar - rectA) > 1e-3 * rectA) CHECK(false, "contains: game %u, overlap area %g of %g", in, ar, rectA);
    }
    printf("polygon overlap slot +0x34 (0x80F0D0, bbox then polygon clip 0x67CBF0): %ld cases; positive-area overlaps all true; no area (apart, or edge/corner contact; 1 in 4 cases is built to touch the bbox): %ld true, %ld false; %ld near-zero cases not judged\n",
           n, touchTrue, touchFalse, inNoArea);
    printf("polygon contains slot +0x38 (0x80F200 -> 0x67B490): %ld of %ld agree with 'rectangle inside the polygon' (a disagreement within 0.1%% of the rectangle area is not counted as a failure)\n", cAgree, cn);
    // two polygons that tile a rectangle: overlap is any-polygon; contains needs one polygon to hold the whole rectangle
    {
        Poly two[2] = {};
        const float a[4][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}}, b[4][2] = {{4, 0}, {8, 0}, {8, 4}, {4, 4}};
        two[0].n = two[1].n = 4;
        memcpy(two[0].v, a, sizeof(a));
        memcpy(two[1].v, b, sizeof(b));
        u8* fp = PolyFp(two, 2, 0x00FEBE38);
        const Rect r = {3, 1, 5, 3};
        const u8 ov = RectTest(0x0080F0D0, fp, r, 0), in = RectTest(0x0080F200, fp, r, 0);
        printf("two squares [0,4] and [4,8] x [0,4], rectangle [3,5]x[1,3] across the seam: overlap %u, contains %u\n", ov, in);
    }
    // grow: used only with flag 0x2000000 at fp+0x34; then fp+0x30 if the list at fp+0x24 is empty, else the argument
    {
        Poly sq = {};
        const float a[4][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
        sq.n = 4;
        memcpy(sq.v, a, sizeof(a));
        const Rect r = {4.5f, 1, 5.5f, 2};  // 0.5 to the right of the square
        u8* fp = PolyFp(&sq, 1, 0x00FEBE38);
        F<float>(fp, 0x30) = 1.0f;
        const u8 g0 = RectTest(0x0080F0D0, fp, r, 1.0f);
        F<DWORD>(fp, 0x34) |= 0x2000000;
        const u8 g1 = RectTest(0x0080F0D0, fp, r, 0.0f);
        F<float>(fp, 0x30) = 0.25f;
        const u8 g1b = RectTest(0x0080F0D0, fp, r, 1.0f);
        static DWORD node[2];
        F<DWORD*>(fp, 0x24) = node;  // list not empty: the argument is used
        const u8 g2 = RectTest(0x0080F0D0, fp, r, 1.0f), g3 = RectTest(0x0080F0D0, fp, r, 0.25f);
        CHECK(!g0 && g1 && !g1b && g2 && !g3, "grow rule");
        printf("grow (gap 0.5): flag clear, arg 1 -> %u; flag set, list empty, fp+0x30 = 1 / 0.25 (arg ignored) -> %u / %u; flag set, list not empty, arg 1 / 0.25 -> %u / %u\n",
               g0, g1, g1b, g2, g3);
    }
    // the box class 0xFEAE08: overlap 0x80B4F0 (closed on the grown side), contains 0x80B1C0 (grown box)
    {
        u8* fp = NewFp(0x00FEAE08, 3, 3, 0x900, 1.0f);
        F<float>(fp, 0x38) = 0, F<float>(fp, 0x3C) = 0, F<float>(fp, 0x40) = 4, F<float>(fp, 0x44) = 4;
        struct C { Rect r; float g; } cs[] = {{{4, 1, 5, 2}, 0}, {{4.5f, 1, 5, 2}, 0.5f}, {{4.5f, 1, 5, 2}, 0.6f}, {{1, 1, 2, 2}, 0}, {{-0.5f, 1, 2, 2}, 0.5f}, {{-0.5f, 1, 2, 2}, 0.4f}};
        printf("box footprint 0xFEAE08 [0,4]^2:");
        long fails = 0;
        for (const C& c : cs) {
            const u8 ov = RectTest(0x0080B4F0, fp, c.r, c.g), in = RectTest(0x0080B1C0, fp, c.r, c.g);
            const float ox = min(4.0f, c.r.x1) - max(0.0f, c.r.x0), oz = min(4.0f, c.r.z1) - max(0.0f, c.r.z0);
            const bool wantOv = ox + c.g > 0 && oz + c.g > 0;
            const bool wantIn = c.r.x0 >= -c.g && c.r.x1 <= 4 + c.g && c.r.z0 >= -c.g && c.r.z1 <= 4 + c.g;
            fails += (ov != 0) != wantOv || (in != 0) != wantIn;
            printf(" [%g,%g]x[%g,%g] g %g: overlap %u contains %u;", c.r.x0, c.r.x1, c.r.z0, c.r.z1, c.g, ov, in);
        }
        printf("\n");
        CHECK(fails == 0, "box slots");
        long bn = 0, bAgree = 0;
        for (int t = 0; t < 20000; t++) {
            const float x = Rf(-3, 7), z = Rf(-3, 7), g = Rnd(2) ? 0 : Rf(0, 1);
            const Rect r = {x, z, x + Rf(0, 3), z + Rf(0, 3)};
            const float ox = min(4.0f, r.x1) - max(0.0f, r.x0), oz = min(4.0f, r.z1) - max(0.0f, r.z0);
            bn++;
            bAgree += (RectTest(0x0080B4F0, fp, r, g) != 0) == (ox + g > 0 && oz + g > 0);
        }
        CHECK(bAgree == bn, "box overlap model");
        printf("box overlap model 'per axis: min(max) - max(min) + grow > 0' (grow added once, not on both sides): %ld of %ld\n", bAgree, bn);
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* e) -> LONG {
        const DWORD code = e->ExceptionRecord->ExceptionCode;
        if (code == 0x406D1388 || code == 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
        printf("exception %08lX at %08lX; stack:", code, e->ContextRecord->Eip);
        const DWORD* sp = (const DWORD*)(uintptr_t)e->ContextRecord->Esp;
        for (int i = 0, k = 0; i < 256 && k < 10; i++)
            if (sp[i] >= 0x00401000 && sp[i] < 0x00F95000) { printf(" %08lX", sp[i]); k++; }
        printf("\n");
        return EXCEPTION_CONTINUE_SEARCH;
    });
    char child[4] = "";
    if (!GetEnvironmentVariableA("IRT_CHILD", child, sizeof(child))) return RunChild();
    const char* exe = argc > 1 ? argv[1] : "..\\..\\..\\game-copy\\bin\\TS3W.exe";
    if (argc > 2) g_trees = atoi(argv[2]);
    if (argc > 3) g_seed = (DWORD)atoi(argv[3]);
    HANDLE f = CreateFileA(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { printf("cannot open %s\n", exe); return 2; }
    const DWORD size = GetFileSize(f, NULL);
    u8* img = (u8*)malloc(size);
    DWORD got;
    if (!img || !ReadFile(f, img, size, &got, NULL) || got != size) { printf("cannot read %s\n", exe); return 2; }
    CloseHandle(f);
    if (!MapImage(img)) return 2;
    free(img);
    Jmp(0x004E69D0, (void*)FkMalloc);
    Jmp(0x004E6A20, (void*)FkMemalign);
    Jmp(0x004E5920, (void*)FkFree);
    Jmp(0x0066BB50, (void*)FkPool);  // cell objects
    Jmp(0x0061AE80, (void*)FkPool);  // A* graph nodes
    Jmp(0x00667920, (void*)FkPool);  // quadtree leaves
    Jmp(0x00667990, (void*)FkPool);  // quadtree internal nodes
    U2();
    U1();
    U3();
    U4();
    U5();
    printf("\n%s (%d failed checks)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
