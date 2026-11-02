// [EaDiag] (docs/234): log-only diagnostics for the EA app 1.69.47 product/entitlement gate, the code that decides which
// installed packs mount (player report: 19 packs installed, BaseGame only). EA build only (/DTS3A_TABLE): raw 1.69.47
// addresses, no Steam counterpart. Nothing in the game is changed: the only writes are the five detour jmps; every game
// read goes through Readable first; everything is bounded (strings 64 chars, lists 64/80 entries, lookup lines 400).
// Sites [verified in the 1.69.47 image, no branch lands inside a detoured range; see docs/234]:
//   0xA421F0 entitlement lookup, cdecl, one arg = offer id (char*), returns 2 = no service or answer not loaded (the
//            gate treats 2 as entitled), 1 = id found in the answer, 0 = not found. Callers 0x5969E1 / 0x596A21 (gate
//            product loop), 0x5949B7 / 0x5949F4 (0x594980, after the gate).
//   0x596999 gate 0x596990, right after the Origin service create + <=3000 ms wait (0xA42A30); ebx = this, pending
//            products = intrusive list at this+0x4BC (node+8 = product id).
//   0x5969C2 product loop head (one hit per pending product); edi = product node (+8 id, +0xC, +0x10 go to the mount
//            0x596810). The loop tries the rows of the 12-byte table 0x11A7B40 {productId, ?, char* offer} and the
//            vector at [0x1227E48] (same row layout) whose productId matches, one 0xA421F0 call each.
//   0x596A3D mount branch (a lookup returned non-zero); 0x596A50 = next product without a mount.
//   0x597E34 the gate's caller 0x597E0F, after the gate (0x597E1B) and the two follow-up calls 0x594980 / 0x594D30.
//   The answered entitlements: eastl rb-tree at service+0x78 (anchor; begin = [service+0x7C]), node +0 right, +4 left,
//   +8 parent (0x4E0CF0 = increment), node+0x48 = eastl string {begin, end} compared with the offer id by 0x5B8000.
// Included by ts3native.cpp.
#ifdef TS3A_TABLE
static void* g_edLookupJmp;  // 0xA421F0 trampoline
static void* g_edWaitJmp;    // 0x596999
static void* g_edLoopJmp;    // 0x5969C2
static void* g_edMountJmp;   // 0x596A3D
static void* g_edAfterJmp;   // 0x597E34
static DWORD g_edProduct;    // product id of the loop iteration in progress (0 = none)
static int g_edMounted;      // its verdict so far
static char g_edLastId[65];  // offer id of the last lookup
static LONG g_edLookups, g_edLoops, g_edAfterHits;

static const DWORD kEdService = 0x01244FB0, kEdFlags0 = 0x01227E50, kEdFlags1 = 0x01227E54;
static const DWORD kEdOfferVec = 0x01227E48, kEdOfferTable = 0x011A7B40;

static bool EdRd(DWORD addr, DWORD* v) {
    if (!Readable((const void*)(uintptr_t)addr, 4)) return false;
    *v = *(const DWORD*)(uintptr_t)addr;
    return true;
}
// at most cap-1 chars of a game C string, every byte checked, non-printable as '?'
static void EdStr(DWORD p, char* out, size_t cap) {
    size_t i = 0;
    if (!p) { strcpy_s(out, cap, "<null>"); return; }
    if (!Readable((const void*)(uintptr_t)p, 1)) { strcpy_s(out, cap, "<unreadable>"); return; }
    for (; i + 1 < cap; i++) {
        const u8* c = (const u8*)(uintptr_t)(p + i);
        if (!Readable(c, 1) || !*c) break;
        out[i] = (*c >= 0x20 && *c < 0x7F) ? (char)*c : '?';
    }
    out[i] = 0;
}
static void EdAppend(char* buf, size_t cap, size_t* len, const char* fmt, ...) {
    if (*len + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    const int k = _vsnprintf_s(buf + *len, cap - *len, _TRUNCATE, fmt, ap);
    va_end(ap);
    *len = k < 0 ? cap - 1 : *len + (size_t)k;
}

// (1) entitlement lookup: the id, the caller and the answer
static int __cdecl EdLookup(const char* id, DWORD from) {
    typedef int(__cdecl * Fn)(const char*);
    const int r = ((Fn)g_edLookupJmp)(id);
    EdStr((DWORD)(uintptr_t)id, g_edLastId, sizeof(g_edLastId));
    if (InterlockedIncrement(&g_edLookups) <= 400)
        Log("eadiag: lookup \"%s\" -> %d (%s) from 0x%08lX product %lu", g_edLastId, r,
            r == 2 ? "no service or answer not loaded, counts as entitled" : r == 1 ? "found in the answer" : r == 0 ? "not in the answer" : "?",
            from, g_edProduct);
    return r;
}
static __declspec(naked) void EdLookupStub() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 8]   // the caller's return address
        push dword ptr [esp + 16]  // the offer id ([esp+4] at entry)
        call EdLookup
        add esp, 8
        pop edx
        pop ecx
        ret                        // cdecl: the caller pops its argument
    }
}

// (4) the answered entitlement list: every string-like {begin, end} pair of each tree node (+0x48 is the compared one)
static DWORD EdTreeNext(DWORD node) {  // eastl RBTreeIncrement (0x4E0CF0), every step bounded
    DWORD r = 0, l = 0, p = 0, pr = 0;
    if (!EdRd(node, &r)) return 0;
    if (r) {
        node = r;
        for (int i = 0; i < 64 && EdRd(node + 4, &l) && l; i++) node = l;
        return node;
    }
    if (!EdRd(node + 8, &p)) return 0;
    for (int i = 0; i < 64 && EdRd(p, &pr) && pr == node; i++) { node = p; if (!EdRd(p + 8, &p)) return 0; }
    if (!EdRd(node, &r)) return 0;
    return r != p ? p : node;
}
static void EdDumpTree(DWORD svc) {
    const DWORD end = svc + 0x78;
    DWORD node = 0;
    int n = 0;
    if (!EdRd(svc + 0x7C, &node)) { Log("eadiag: entitlement list: anchor unreadable"); return; }
    for (; node && node != end && n < 80; n++) {
        char line[560];
        size_t len = 0;
        for (DWORD off = 0x0C; off <= 0x7C; off += 4) {
            DWORD b = 0, e = 0;
            if (!EdRd(node + off, &b) || !EdRd(node + off + 4, &e) || e <= b || e - b > 256 || !Readable((const void*)(uintptr_t)b, e - b)) continue;
            bool text = true;
            for (DWORD i = 0; i < e - b && text; i++) text = ((const u8*)(uintptr_t)b)[i] >= 0x20 && ((const u8*)(uintptr_t)b)[i] < 0x7F;
            if (!text) continue;
            EdAppend(line, sizeof(line), &len, " +0x%02lX=\"%.*s\"", off, (int)(e - b > 64 ? 64 : e - b), (const char*)(uintptr_t)b);
            off += 4;  // the end pointer
        }
        line[len] = 0;
        Log("eadiag: entitlement[%d] node 0x%08lX%s", n, node, line);
        node = EdTreeNext(node);
    }
    Log("eadiag: entitlement list: %d entries%s", n, n >= 80 ? " (cut at 80)" : node != end ? " (walk stopped: unreadable node)" : "");
}

// (3a) after the service create + wait, before the product loop
static void __cdecl EdAfterWait(DWORD self) {
    DWORD svc = 0, a28 = 0, a24 = 0;
    EdRd(kEdService, &svc);
    const bool loaded = svc && EdRd(svc + 0x28, &a28) && EdRd(svc + 0x24, &a24) && (a28 & 0xFF) && !a24;
    const DWORD head = self + 0x4BC;
    DWORD node = 0, id = 0;
    char ids[400];
    size_t len = 0;
    int n = 0;
    if (EdRd(head, &node))
        for (; node && node != head && n < 64 && EdRd(node + 8, &id); n++) { EdAppend(ids, sizeof(ids), &len, " %lu", id); if (!EdRd(node, &node)) break; }
    ids[len] = 0;
    Log("eadiag: after wait: service 0x%08lX +0x28 %lu +0x24 0x%08lX loaded %d; %d pending products:%s", svc, a28 & 0xFF, a24, loaded ? 1 : 0, n, ids);
    if (svc) EdDumpTree(svc);
}
static __declspec(naked) void EdWaitStub() {
    __asm {
        pushfd
        pushad
        push ebx
        call EdAfterWait
        add esp, 4
        popad
        popfd
        jmp dword ptr [g_edWaitJmp]
    }
}

// (2) one line per pending product with the offer ids the loop will try for it; the verdict comes from (1) and the mount site
static void EdCloseProduct() {
    if (g_edProduct && !g_edMounted) Log("eadiag: product %lu: NOT mounted", g_edProduct);
    g_edProduct = 0;
    g_edMounted = 0;
}
static void __cdecl EdProduct(DWORD node) {
    EdCloseProduct();
    DWORD id = 0, c = 0, d = 0, pid = 0, name = 0, vec = 0, b = 0, e = 0;
    EdRd(node + 8, &id);
    EdRd(node + 0xC, &c);
    EdRd(node + 0x10, &d);
    g_edProduct = id;
    InterlockedIncrement(&g_edLoops);
    char cands[600], s[65];
    size_t len = 0;
    int n = 0;
    for (DWORD row = kEdOfferTable; n < 64 && EdRd(row, &pid) && pid; row += 12) {
        if (pid != id) continue;
        EdRd(row + 8, &name);
        EdStr(name, s, sizeof(s));
        EdAppend(cands, sizeof(cands), &len, " \"%s\"", s);
        n++;
    }
    if (EdRd(kEdOfferVec, &vec) && vec && EdRd(vec, &b) && EdRd(vec + 4, &e))
        for (; b + 12 <= e && n < 64 && EdRd(b, &pid); b += 12) {
            if (pid != id) continue;
            EdRd(b + 8, &name);
            EdStr(name, s, sizeof(s));
            EdAppend(cands, sizeof(cands), &len, " [vec]\"%s\"", s);
            n++;
        }
    cands[len] = 0;
    Log("eadiag: product %lu (+0xC 0x%08lX +0x10 0x%08lX): %d offer ids:%s", id, c, d, n, cands);
}
static __declspec(naked) void EdLoopStub() {
    __asm {
        pushfd
        pushad
        push edi
        call EdProduct
        add esp, 4
        popad
        popfd
        jmp dword ptr [g_edLoopJmp]
    }
}
static void __cdecl EdMount(DWORD node) {
    DWORD id = 0;
    EdRd(node + 8, &id);
    g_edMounted = 1;
    Log("eadiag: product %lu: mounted (lookup \"%s\")", id, g_edLastId);
}
static __declspec(naked) void EdMountStub() {
    __asm {
        pushfd
        pushad
        push edi
        call EdMount
        add esp, 4
        popad
        popfd
        jmp dword ptr [g_edMountJmp]
    }
}

// (3b) after the gate and its two follow-up calls returned to 0x597E0F
static void __cdecl EdAfterGate() {
    if (InterlockedIncrement(&g_edAfterHits) > 4) return;
    EdCloseProduct();
    DWORD f0 = 0, f1 = 0, svc = 0, a28 = 0, a24 = 0;
    EdRd(kEdFlags0, &f0);
    EdRd(kEdFlags1, &f1);
    if (EdRd(kEdService, &svc) && svc) { EdRd(svc + 0x28, &a28); EdRd(svc + 0x24, &a24); }
    Log("eadiag: after gate: flags [0x1227E50] 0x%08lX [0x1227E54] 0x%08lX; service 0x%08lX +0x28 %lu +0x24 0x%08lX; %ld lookups, %ld products",
        f0, f1, svc, a28 & 0xFF, a24, g_edLookups, g_edLoops);
}
static __declspec(naked) void EdAfterStub() {
    __asm {
        pushfd
        pushad
        call EdAfterGate
        popad
        popfd
        jmp dword ptr [g_edAfterJmp]
    }
}

static bool ApplyEaDiag(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"EaDiag", L"ProductGate", 0, ini)) return false;
    static const u8 kLookup[] = {0x8B, 0x0D, 0xB0, 0x4F, 0x24, 0x01};            // mov ecx,[0x1244FB0]
    static const u8 kWait[] = {0x8D, 0xAB, 0xBC, 0x04, 0x00, 0x00};              // lea ebp,[ebx+0x4BC]
    static const u8 kLoop[] = {0x33, 0xF6, 0x39, 0x35, 0x40, 0x7B, 0x1A, 0x01};  // xor esi,esi; cmp [0x11A7B40],esi
    static const u8 kMount[] = {0x8B, 0x47, 0x10, 0x8B, 0x4F, 0x0C};             // mov eax,[edi+0x10]; mov ecx,[edi+0xC]
    static const u8 kAfter[] = {0x8B, 0x84, 0x24, 0xE4, 0x08, 0x00, 0x00};       // mov eax,[esp+0x8E4]
    g_edLookupJmp = InstallDetour("EaDiag entitlement lookup", 0x00A421F0, kLookup, sizeof(kLookup), (void*)EdLookupStub);
    g_edWaitJmp = InstallDetour("EaDiag gate after wait", 0x00596999, kWait, sizeof(kWait), (void*)EdWaitStub);
    g_edLoopJmp = InstallDetour("EaDiag gate product loop", 0x005969C2, kLoop, sizeof(kLoop), (void*)EdLoopStub);
    g_edMountJmp = InstallDetour("EaDiag gate mount", 0x00596A3D, kMount, sizeof(kMount), (void*)EdMountStub);
    g_edAfterJmp = InstallDetour("EaDiag after gate", 0x00597E34, kAfter, sizeof(kAfter), (void*)EdAfterStub);
    void* const tramps[] = {g_edLookupJmp, g_edWaitJmp, g_edLoopJmp, g_edMountJmp, g_edAfterJmp};
    int n = 0;
    for (void* t : tramps) n += t ? 1 : 0;
    Log("%s EaDiag: ProductGate log-only hooks, %d/5 installed (docs/234)", n == 5 ? "OK  " : "WARN", n);
    return n > 0;
}
#endif
