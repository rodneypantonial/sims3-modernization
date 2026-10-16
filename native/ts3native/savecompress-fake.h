// Test-only fake of a package commit for phase 10 (selftest.cpp, savecompress-test.cpp). Include after ts3native.cpp.
// Lays out what FUN_004A7910 / FUN_004A8910 / FUN_004A7030 read: package +0x14 flags, +0x20 compression, +0x270 lock,
// +0x2EC pending map anchor (right, left = leftmost at +0x2F0, parent = root), +0x2FC count, +0x390 codec plugins;
// map node +0 right, +4 left, +8 parent, +0x20 record; record +8 class, +0x28 stream (vt +0x1C size, +0x28 seek, +0x30 read).
#include <stddef.h>

struct FkStream { void** vt; const u8* data; DWORD len, pos; };
struct FkRec { DWORD idx, pad1; DWORD cls; DWORD pad2[7]; FkStream s; };
static_assert(offsetof(FkRec, cls) == 8 && offsetof(FkRec, s) == 0x28, "record layout");
struct FkNode { FkNode* right; FkNode* left; FkNode* parent; DWORD color; DWORD key[4]; FkRec* rec; };
static_assert(offsetof(FkNode, rec) == 0x20, "node layout");

static DWORD __fastcall FkSize(FkStream* s, void*) { return s->len; }
static DWORD __fastcall FkSeek(FkStream* s, void*, DWORD pos, DWORD) { s->pos = pos; return 1; }
static DWORD __fastcall FkRead(FkStream* s, void*, void* buf, DWORD n) {
    const DWORD k = n < s->len - s->pos ? n : s->len - s->pos;
    memcpy(buf, s->data + s->pos, k);
    s->pos += k;
    return k;
}
static void* g_fkStreamVt[13] = {0, 0, 0, 0, 0, 0, 0, (void*)FkSize, 0, 0, (void*)FkSeek, 0, (void*)FkRead};

static LONG __fastcall FkLock(void*, void*, const DWORD*) { return 1; }
static LONG __fastcall FkUnlock(void*, void*) { return 0; }

struct FkPkg {
    u8 b[0x400];
    FkNode* nodes;
    FkRec* recs;
    int count;
};
static FkNode* FkAnchor(FkPkg* p) { return (FkNode*)(p->b + 0x2EC); }

static FkNode* FkBuildRange(FkPkg* p, int lo, int hi, FkNode* parent) {  // balanced tree, in-order = index order
    if (lo > hi) return NULL;
    const int mid = (lo + hi) / 2;
    FkNode* n = &p->nodes[mid];
    n->parent = parent;
    n->left = FkBuildRange(p, lo, mid - 1, n);
    n->right = FkBuildRange(p, mid + 1, hi, n);
    return n;
}

// Records i = 0..count-1 with the given payloads, pending in index order.
static void FkBuild(FkPkg* p, const u8* const* data, const DWORD* lens, int count, int compression) {
    memset(p->b, 0, sizeof(p->b));
    p->count = count;
    p->nodes = (FkNode*)calloc(count, sizeof(FkNode));
    p->recs = (FkRec*)calloc(count, sizeof(FkRec));
    for (int i = 0; i < count; i++) {
        FkRec& r = p->recs[i];
        r.idx = (DWORD)i;
        r.cls = kScRecClass;
        r.s.vt = g_fkStreamVt;
        r.s.data = data[i];
        r.s.len = lens[i];
        p->nodes[i].rec = &r;
        p->nodes[i].key[0] = (DWORD)i;
    }
    FkNode* a = FkAnchor(p);
    FkNode* root = FkBuildRange(p, 0, count - 1, a);
    a->parent = root;
    a->left = count ? &p->nodes[0] : a;
    a->right = count ? &p->nodes[count - 1] : a;
    *(DWORD*)(p->b + 0x14) = 3;
    *(int*)(p->b + 0x20) = compression;
    *(int*)(p->b + 0x2FC) = count;
}
static void FkFree(FkPkg* p) { free(p->nodes); free(p->recs); }

// The codec object FUN_004A7030 builds (0x4EBFD0): vtable, allocator at +4. Slot 1 is what the vtable patch points to.
static void* g_fkCodecVt[5];
struct FkCodec { void** vt; void* alloc; };

// What FUN_004A7910 + FUN_004A8910 + FUN_004A7030 (default codec path) do per pending record, leftmost first.
static u8** g_fkOut;       // per record index: compressed bytes
static int* g_fkOutLen;
static FkCodec g_fkCodec;
static DWORD g_fkFlags;    // 0 = the game's choice from +0x20 and the length
static bool g_fkSizeFirst; // also a size-only call (dst 0, flags 2) first, as the codec plugin path does
static DWORD __fastcall FkCommit(void* pkg, void*) {
    u8* p = (u8*)pkg;
    while (*(int*)(p + 0x2FC)) {
        FkNode* n = *(FkNode**)(p + 0x2F0);
        FkRec* r = n->rec;
        void* s = &r->s;
        const DWORD len = FkSize((FkStream*)s, NULL);
        u8* buf = (u8*)malloc(len + 16);
        FkSeek((FkStream*)s, NULL, 0, 0);
        FkRead((FkStream*)s, NULL, buf, len);
        ScSlotFn slot = (ScSlotFn)g_fkCodec.vt[1];
        const int c = *(int*)(p + 0x20);
        DWORD flags = g_fkFlags;
        if (!flags) flags = c == 1 ? 0x10001 : c == 3 ? 2 : len > 250000 ? 1 : 2;
        if (g_fkSizeFirst) slot(&g_fkCodec, NULL, buf, len, NULL, 0, 2);
        const int bound = slot(&g_fkCodec, NULL, buf, len, NULL, 0, 1);
        u8* out = (u8*)malloc((size_t)bound + 1024);
        g_fkOutLen[r->idx] = slot(&g_fkCodec, NULL, buf, len, out, (DWORD)bound, flags);
        g_fkOut[r->idx] = out;
        free(buf);
        *(FkNode**)(p + 0x2F0) = (FkNode*)ScNext((u8*)n);  // FUN_004A8E00 erases the committed node
        (*(int*)(p + 0x2FC))--;
    }
    return 1;
}
