"""Design S2 (docs/84 §3.6): the read-only footprint visitor against the game's visitors, under the game's own walks.

Target: LotQuadTree_QueryPoint 0x667480 (thiscall(Q; ctx, x, y, radius, set, mode), ret 0x18), which runs the point walk
0x663E20 with visitor 0x662DB0 (mode 0) or 0x662DD0 (mode 1) -> LotQuadTree_FootprintVisitor 0x669CD0 -> CollectMasked
0x669230 / CollectLeaf 0x669850 / CollectChildren 0x669B90, refcounts through slots +0x90/+0x94 and FootprintSet_Insert
0x628340 into a red-black tree. Each case also runs LotQuadTree_QuerySegment 0x667400 (the gather's segment walk 0x663B20,
NULL context, mode 1) on the same tree with a zero-length segment, as RouteFollow_EditPath does for locomotion.

Model: the same walks run again with a Python visitor in place of 0x662DB0 / 0x662DD0, implementing the rules of ts3native's
SrVisit / SrCollect (steerro.cpp): no refcount, no set, no allocation, the slot targets of the nine footprint classes
modelled in Python for RoutingWorld_Get = 0. Pass = the game's set (read back in order through 0x4E0B20) equals the model's
set, the game's references are balanced afterwards, and the model walk leaves the scratch heap (tree, entries, footprints)
byte for byte unchanged: the walks themselves write only their stack.

The model runs inside run() (it needs the emulator for the walk), so run() returns (game, model) and compare() checks them
against each other; reference() only passes the case through.
"""
import struct

NAME = 'RO footprint visitor vs LotQuadTree_QueryPoint / QuerySegment'
TARGET = 0x667480
CONV = 'thiscall'
CASES = 150
COVERAGE_TARGET = 0.75  # the flush call (Q+0x48 < 1 with the deferred flag) is not taken: the clone gives up there
FOLLOW = [0x667400, 0x663E20, 0x663B20, 0x669BE0, 0x669C30, 0x669CB0, 0x66A040, 0x669C20, 0x66A8E0, 0x66A760, 0x66A3B0,
          0x61B7C0, 0x662DB0, 0x662DD0, 0x669CD0, 0x669230, 0x669850, 0x669B90, 0x669B30, 0x6691F0, 0x620CA0,
          0x8292D0, 0x836200, 0x8294E0, 0x9C1B00, 0x620D60, 0x620DD0, 0x80F840, 0x80C300, 0x807090, 0x807B70, 0x8066C0,
          0x6119F0, 0x624450, 0x70AE20, 0x620CC0, 0x806FE0, 0x915880, 0x915890, 0x628340, 0x626B70, 0x4E0B20,
          0x4E3F90, 0x4E3F70, 0x4E0AF0, 0x4E0C70, 0x628F80, 0x4E0C40, 0x4E0B60]
TALLY = {'point cases with footprints': 0, 'point footprints': 0, 'segment cases with footprints': 0, 'segment footprints': 0}


class _Notes(list):
    def __iter__(self):  # printed after the run: the tally shows the cases were not empty
        return iter(list(super().__iter__()) + [f'tally: {TALLY}'])


NOTES = _Notes(['RoutingWorld_Get reads the global 0x11D9128, 0 in the image: no options object, as for the clone (record +0x28 = 0)',
         'leaf and children branches of the visitor are only reachable with a non-masked iterator; the walks hand the '
         'visitor masked sub-cells only (0x669CB0: visit when the node is NULL or the iterator byte >= 0x15)',
         'run() returns (game set, model set); reference() is the case label only'])

CLASSES = [0xFEBBC0, 0xFEB848, 0xFEAE08, 0xFEBA98, 0xFEBE38, 0xFEB5F8, 0xFEB720, 0xFEBD10, 0xFEB970]
SUB_TYPE, SUB_CONST = 0xFEB5D4, 0xFEADE0  # sub-object vtables: type from +0x35 (0x624ED0) / constant 0xA (0x620E50)
VISIT = 0x00401000  # a stub address for the model visitor (never executed: the stub returns first)
TYPES = [0x14, 0x1F, 9, 0x11, 0xC, 0xD, 0x17, 0x13, 0x18, 0x1E, 2, 5, 0xA, 3, 3, 3, 4, 4, 0x1A, 0x1B]


def setup(emu):
    emu.stub_allocator()
    emu.stub(VISIT, fn=lambda e, a: _MODEL.visit(e, a(0), a(1)), ret=1, pop=12, name='model visitor')


def cases(rng, n):
    out = []
    for i in range(n):
        out.append({'label': f'tree{i}', 'seed': rng.getrandbits(32), 'mode': rng.randint(0, 1),
                    'x': rng.uniform(-4, 68), 'y': rng.uniform(-4, 68), 'r': rng.choice([0.0, 0.3, 1.5, 6.0, 20.0, -1.0])})
    return out


def reference(case):
    return case['label']


def compare(got, want):
    if isinstance(got, tuple) and got and got[0] == 'EmuError':
        return got[1]
    for kind, (g, m) in got.items():
        if g != m and isinstance(g, bool):
            return f'{kind}: false'
        if g != m:
            return f'{kind}: game {sorted(map(hex, g))} model {sorted(map(hex, m))}'
    return None


def s8(b):
    return b - 256 if b >= 128 else b


class Model:
    """ts3native SrVisit / SrCollect in Python, with the slot targets of the nine classes for RoutingWorld_Get = 0."""

    def __init__(self):
        self.rec, self.flag, self.objs = 0, 0, []

    def visit(self, e, ctx, it):
        node = e.u32(it + 0xC)
        if not node:
            return 1
        b = e.read(it + 0x1C, 1)[0]
        if b:
            self.collect(e, node, e.u32(0xFEDE48 + b * 4))
        elif not (e.read(node + 4, 1)[0] & 3):
            self.collect(e, node, None)
        else:
            self.children(e, node)
        return 1

    def children(self, e, node):
        for i in range(4):
            ch = e.u32(node + 0xC + 4 * i)
            if ch:
                if not (e.read(ch + 4, 1)[0] & 3):
                    self.collect(e, ch, None)
                else:
                    self.children(e, ch)

    def cls(self, e, o, byte):
        vt, rec = e.u32(o), self.rec
        s8at = lambda off: s8(e.read(o + off, 1)[0])
        if vt == 0xFEBBC0:  # 0x80F840
            return s8at(0x34) if rec and e.read(rec + 8, 8) == e.read(o + 0x40, 8) else 0
        if vt == 0xFEB848:  # 0x80C300 with no routing world
            fl = e.read(o + 0x41, 1)[0]
            if rec and fl & 8 and fl & 4:
                return 0
            return {0: 8, 1: s8at(0x34), 2: 8}.get(byte, 0)
        if vt == 0xFEB970:  # 0x8066C0
            return 0 if rec and e.read(rec + 8, 8) == e.read(o + 0x14, 8) else s8at(0x34)
        if vt == 0xFEAE08:  # 0x6119F0
            return 0
        return s8at(0x34)  # 0x807090 / 0x807B70 with no world and no lock-key object

    def collect(self, e, node, bit):
        l0 = e.u32(node + 8)
        heap = l0 & 1
        cnt = e.u32(node + 0xC) if heap else 1
        if heap and not cnt:
            return
        p = (l0 & ~1) if heap else node + 8
        local, seen = [], False
        ow = e.u32(self.rec) if self.rec else 0
        for i in range(cnt):
            ent, lo, hi = struct.unpack('<3I', e.read(p + 12 * i, 12))
            if not ent:
                continue
            if bit is not None and not ((lo >> (bit & 31)) & 1 if bit < 32 else (hi >> ((bit - 32) & 31)) & 1):
                continue
            o = e.u32(ent + 8)
            vt = e.u32(o)
            # +0x60 (0x9C1B00) is true; +0x28: 0x620DD0 (byte +0x48) for 0xFEAE08, else false
            if vt == 0xFEAE08 and e.read(o + 0x48, 1)[0]:
                continue
            c = self.cls(e, o, e.read(ent + 0x10, 1)[0])
            if self.rec and e.read(o + 0x14, 8) == e.read(self.rec + 8, 8):
                continue
            t = 0xA if vt == 0xFEAE08 else s8(e.read(o + 0x35, 1)[0])
            if t == 0x1F:
                seen = True
                continue
            if t == 0x14:
                c = 1
            if self.flag:
                if c == 2:
                    local.append(o)
                    continue
                if (c & 0xFFFFFFFF) < 2 or c == 0xD:
                    continue
            if o not in self.objs:
                self.objs.append(o)
        if self.flag and seen:
            for o in local:
                if o not in self.objs:
                    self.objs.append(o)


_MODEL = Model()


class Rng:
    def __init__(self, seed):
        self.s = seed or 1

    def __call__(self, n):
        self.s = (self.s * 1103515245 + 12345) & 0xFFFFFFFF
        return ((self.s >> 8) & 0xFFFFFF) % n


def build(emu, rnd):
    fps = []
    for _ in range(12 + rnd(30)):
        o = emu.alloc(0x80)
        vt = CLASSES[rnd(9)]
        emu.w32(o, vt)
        emu.w32(o + 4, SUB_CONST if vt == 0xFEAE08 else SUB_TYPE)
        emu.w32(o + 8, 100)
        emu.w32(o + 0x14, 0 if rnd(4) == 0 else 0x1000 + rnd(40))
        emu.write(o + 0x34, bytes([0xD if rnd(5) == 0 else 3 if rnd(2) else rnd(4), TYPES[rnd(len(TYPES))]]))
        emu.w32(o + 0x40, 0 if rnd(4) == 0 else rnd(1000))
        emu.write(o + 0x41, bytes([rnd(16)]))
        emu.write(o + 0x48, bytes([rnd(2)]))
        fps.append(o)

    def entry():
        e = emu.alloc(0x20)
        emu.w32(e, 0xFEAC7C)
        emu.w32(e + 8, fps[rnd(len(fps))])
        emu.write(e + 0x10, bytes([rnd(4)]))
        return e

    def leaf():
        n = emu.alloc(0x20)
        emu.write(n + 4, bytes([2 if rnd(4) == 0 else 0]))
        cnt = 1 if rnd(5) == 0 else rnd(9)
        if cnt == 1 and rnd(2):
            emu.w32(n + 8, entry())
            emu.w32(n + 0xC, rnd(0xFFFFFF) * 251)
            emu.w32(n + 0x10, rnd(0xFFFFFF) * 251)
            return n
        lst = emu.alloc(12 * max(cnt, 1), 4)
        for i in range(cnt):
            emu.w32(lst + 12 * i, entry() if rnd(12) else 0)
            emu.w32(lst + 12 * i + 4, rnd(0xFFFFFF) * 251 if rnd(2) else 0xFFFFFFFF)
            emu.w32(lst + 12 * i + 8, rnd(0xFFFFFF) * 251 if rnd(2) else 0xFFFFFFFF)
        emu.w32(n + 8, lst | 1)
        emu.w32(n + 0xC, cnt)
        return n

    def tree(depth):
        if depth >= 2 or rnd(4) == 0:
            return leaf()
        n = emu.alloc(0x20)
        emu.write(n + 4, b'\x01')
        for i in range(4):
            emu.w32(n + 0xC + 4 * i, tree(depth + 1) if rnd(5) else 0)
        return n

    q = emu.alloc(0x60)
    emu.w32(q + 0x24, tree(0))
    emu.wf32(q + 0x38, 64.0)
    emu.w32(q + 0x44, rnd(3))
    emu.w32(q + 0x48, 1)  # no flush (the clone gives up when the original would flush)
    return q, fps


def new_set(emu):
    s = emu.alloc(0x20)
    emu.w32(s + 4, s + 4)  # header: parent slot, leftmost, rightmost (0x82EDB0 builds it this way on its stack)
    emu.w32(s + 8, s + 4)
    emu.w32(s + 0x18, 0xFEC82C)
    return s


def read_set(emu, s):
    out, node = [], emu.u32(s + 8)
    while node != s + 4:
        out.append(emu.u32(node + 0x10))
        node = emu.call(0x4E0B20, [node], 'cdecl', follow=[0x4E0AF0, 0x4E0C70]).eax
    return out


def run(emu, case):
    rnd = Rng(case['seed'])
    emu.w32(0x11D9128, 0)
    q, fps = build(emu, rnd)
    rec = emu.alloc(0x30)
    emu.w32(rec, rnd(0x10000))
    emu.write(rec + 0x24, b'\x01')
    ctx = emu.alloc(0x60)
    emu.w32(ctx + 0x40, rec)
    res = {}
    # point query with the option record (0x82EDB0's context), the case's mode
    s = new_set(emu)
    emu.call(TARGET, [ctx, case['x'], case['y'], case['r'], s, case['mode']], 'thiscall', this=q, follow=FOLLOW)
    game = set(read_set(emu, s))
    snap = emu.read(0x20000000, emu._heap - 0x20000000)
    _MODEL.rec, _MODEL.flag, _MODEL.objs = rec, case['mode'], []
    emu.call(0x663E20, [ctx, case['x'], case['y'], case['r'], VISIT, s], 'thiscall', this=q, follow=FOLLOW)
    res['point'] = (game, set(_MODEL.objs))
    TALLY['point cases with footprints'] += bool(game)
    TALLY['point footprints'] += len(game)
    res['point-heap-unchanged'] = (True, emu.read(0x20000000, len(snap)) == snap)
    # the gather's segment walk: NULL context, mode 1, zero-length segment, width 0
    s = new_set(emu)
    emu.call(0x667400, [0, case['x'], case['y'], case['x'], case['y'], 0.0, s, 1], 'thiscall', this=q, follow=FOLLOW)
    game = set(read_set(emu, s))
    snap = emu.read(0x20000000, emu._heap - 0x20000000)
    _MODEL.rec, _MODEL.flag, _MODEL.objs = 0, 1, []
    emu.call(0x663B20, [0, case['x'], case['y'], case['x'], case['y'], 0.0, VISIT, 0, s], 'thiscall', this=q, follow=FOLLOW)
    res['segment'] = (game, set(_MODEL.objs))
    TALLY['segment cases with footprints'] += bool(game)
    TALLY['segment footprints'] += len(game)
    res['segment-heap-unchanged'] = (True, emu.read(0x20000000, len(snap)) == snap)
    return res  # the sets keep their references (FootprintSet_FreeTree is not run): balance is checked in steerro-test
