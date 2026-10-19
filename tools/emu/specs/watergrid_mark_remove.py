"""docs/100 L5: stale high-cost marks after a G rebuild, run with the game's own code on a constructed water grid G.

Target: WaterGrid_MarkA 0x670510 (the footprint mark remover; thiscall(G; ownerLo, ownerHi, fpLo, fpHi, maskLo, maskHi),
ret 0x18). It maps the footprint's 64-bit agent mask to map classes with WaterGrid_FootprintMarkClasses 0x66D5E0, finds the
(owner, footprint) record in the highCostFootprints tree G+0x7D0 (0x66F390 lower bound + 0x66E120 compare), and if found
writes 0 to every recorded cell (16-byte entries, x at +0 and z at +4, cell = trunc(v) >> 2, bounds G+0x7A0 / +0x7A4 /
class < G+0x7AC) in each selected high-cost map (G+0x7C0), then unlinks and frees the record (stubbed: 0x4E0B20,
0x4E0E20, 0x4E3F70).

"rebuild" cases first run the two G-build steps that touch these maps and records, also the game's code: PassMaps_Resize
0x61F5C0 (thiscall(G+0x7A0; w, h, 5, 10), same size as before, as at 0x672AC2) and the record tree free 0xC5E990
(thiscall(G+0x7D0; root), as at 0x672AD6), followed by the header reset the build does inline at 0x672ADB-0x672AED (anchor
linked to itself, root 0, size 0; done here by the spec). Then the same remove runs.

Model: present record and no rebuild -> exactly the recorded cells of the selected classes are cleared; after a rebuild the
map bytes are unchanged by the resize and the remove finds nothing, so the marks it set stay (L5: stale 1 bytes). Output:
high-cost map bytes, passability map bytes, record count.
"""
import struct

NAME = 'WaterGrid mark remove after a G rebuild (L5)'
TARGET = 0x670510
CONV = 'thiscall'
CASES = 250
FOLLOW = [0x66D5E0, 0x66F390, 0x66E120]
REBUILD_FOLLOW = [0x61F5C0, 0x61F550, 0x61F330, 0x61F280, 0x61F070]
TABLE = []  # 9 (lo, hi) agent-mask entries at 0x1158610, read in setup
TALLY = {'rebuild cases that keep marks a remove would have cleared': 0}


class _Notes(list):
    def __iter__(self):
        return iter(list(super().__iter__()) + [f'tally (self-check, must be > 0): {TALLY}'])


NOTES = _Notes(['stubs: 0x4E0B20, 0x4E0E20 (tree unlink), 0x4E3F70 (free), memmove (Python); the header reset after '
                '0xC5E990 mirrors 0x672ADB-0x672AED'])


def setup(emu):
    TABLE[:] = [struct.unpack('<II', emu.read(0x1158610 + 8 * k, 8)) for k in range(9)]
    for a in (0x4E0B20, 0x4E0E20, 0x4E3F70):
        emu.stub(a, ret=0)

    def memmove(e, arg):
        d, s, n = arg(0), arg(1), arg(2)
        e.write(d, e.read(s, n)) if n else None
        return d
    emu.stub('msvcr80.dll!memmove', memmove)


def classes(lo, hi):
    if lo & hi == 0xFFFFFFFF:
        return 1
    m = 0
    for k, (tlo, thi) in enumerate(TABLE):
        if (lo & tlo) | (hi & thi):
            m |= 2 << k
    return m


def rand_mask(rng):
    r = rng.random()
    if r < 0.15:
        return 0xFFFFFFFF, 0xFFFFFFFF
    if r < 0.2:
        return 0, 0
    lo, hi = TABLE[rng.randrange(9)]
    return (lo | rng.getrandbits(32) * (rng.random() < 0.3)) & 0xFFFFFFFF, hi


def cases(rng, n):
    out = []
    for i in range(n + 12):
        w, h = rng.randrange(1, 20), rng.randrange(1, 20)
        nh = 10 if rng.random() < 0.85 else rng.randrange(1, 10)
        keys = set()
        while len(keys) < rng.randrange(0, 9):
            keys.add((rng.randrange(1, 6), rng.choice([0, 1]), rng.randrange(0, 6), rng.choice([0, 0xFFFFFFFF])))
        recs = []
        for k in sorted(keys, key=lambda x: rng.random()):
            cells = []
            for _ in range(rng.randrange(0, 6)):
                if rng.random() < 0.1:  # out of range: negative or past the edge
                    cells.append((rng.choice([-3.0, w * 4 + 1.0]), rng.uniform(0, h * 4)))
                else:
                    cells.append((rng.randrange(w) * 4 + rng.choice([0.0, 1.5, 3.99]),
                                  rng.randrange(h) * 4 + rng.choice([0.0, 2.25, 3.5])))
            recs.append(dict(key=k, cells=cells))
        present = recs and rng.random() < 0.75
        key = rng.choice(recs)['key'] if present else (rng.randrange(1, 7), rng.choice([0, 1]), rng.randrange(0, 7), 0)
        lab = ['remove', 'rebuild-then-remove'][i % 2] if i < 12 else f'rand{i}'
        out.append(dict(label=lab, w=w, h=h, nh=nh, recs=recs, key=key, mask=rand_mask(rng),
                        rebuild=(i % 2 == 1) if i < 12 else rng.random() < 0.4,
                        bytes=[[rng.random() < 0.15 for _ in range(w * h)] for _ in range(15)]))
    return out


def initial_maps(c):
    """Map bytes before the remove: random lot-outline 1s plus the record cells marked in their classes."""
    hc = [bytearray(1 if b else 0 for b in c['bytes'][5 + k]) for k in range(10)]
    m = classes(*c['mask'])
    for r in c['recs']:
        if r['key'] == c['key']:
            for x, z in r['cells']:
                col, row = cell(x), cell(z)
                for k in range(10):
                    if m >> k & 1 and col < c['w'] and row < c['h'] and k < c['nh']:
                        hc[k][row * c['w'] + col] = 1
    return hc


def cell(v):
    return (int(v) & 0xFFFFFFFF) >> 2  # fistp with truncation, then shr 2 (negative -> huge -> out of range)


def vec_of_maps(emu, maps):
    arr = emu.alloc(16 * len(maps))
    for i, m in enumerate(maps):
        d = emu.alloc(max(len(m), 1), 4, bytes(m))
        emu.write(arr + 16 * i, struct.pack('<III', d, d + len(m), d + len(m)))
    return arr


def build_tree(emu, tree, recs):
    """EASTL rbtree: anchor at tree+4 (right +0, left +4, parent +8), size tree+0x14; node key at +0x10, vector +0x20."""
    anchor = tree + 4
    root = None
    nodes = {}
    for r in recs:
        nd = emu.alloc(0x30)
        lo, hi, flo, fhi = r['key']
        emu.write(nd + 0x10, struct.pack('<IIII', lo, hi, flo, fhi))
        if r['cells']:
            v = emu.alloc(16 * len(r['cells']))
            for i, (x, z) in enumerate(r['cells']):
                emu.write(v + 16 * i, struct.pack('<4f', x, z, 0, 0))
            emu.write(nd + 0x20, struct.pack('<II', v, v + 16 * len(r['cells'])))
        nodes[nd] = ((hi << 32) | lo, (fhi << 32) | flo)
        if root is None:
            root = nd
            emu.w32(nd + 8, anchor)
            continue
        p = root
        while True:
            side = 0 if nodes[nd] > nodes[p] else 4  # right child at +0 holds larger keys
            ch = emu.u32(p + side)
            if not ch:
                emu.w32(p + side, nd)
                emu.w32(nd + 8, p)
                break
            p = ch
    emu.write(anchor, struct.pack('<III', anchor, anchor, root or 0))
    emu.w32(tree + 0x14, len(recs))
    return root or 0


def run(emu, c):
    g = emu.alloc(0x800)
    w, h = c['w'], c['h']
    emu.write(g + 0x7A0, struct.pack('<IIII', w, h, 5, c['nh']))
    pas = [bytearray(1 if b else 0 for b in c['bytes'][k]) for k in range(5)]
    hc = initial_maps(c)[:c['nh']]
    pv, hv = vec_of_maps(emu, pas), vec_of_maps(emu, hc)
    emu.write(g + 0x7B0, struct.pack('<III', pv, pv + 16 * 5, pv + 16 * 5))
    emu.write(g + 0x7C0, struct.pack('<III', hv, hv + 16 * len(hc), hv + 16 * len(hc)))
    root = build_tree(emu, g + 0x7D0, c['recs'])
    if c['rebuild']:
        emu.call(0x61F5C0, [w, h, 5, c['nh']], 'thiscall', this=g + 0x7A0, follow=REBUILD_FOLLOW)
        emu.call(0xC5E990, [root], 'thiscall', this=g + 0x7D0, follow=[0xC5E990])
        a = g + 0x7D4
        emu.write(a, struct.pack('<III', a, a, 0))
        emu.w32(g + 0x7E4, 0)
    lo, hi, flo, fhi = c['key']
    emu.call(TARGET, [lo, hi, flo, fhi, *c['mask']], CONV, this=g, follow=FOLLOW)
    rd = lambda vec, k: emu.read(emu.u32(emu.u32(vec) + 16 * k), w * h)
    return ([rd(g + 0x7C0, k) for k in range(c['nh'])], [rd(g + 0x7B0, k) for k in range(5)], emu.u32(g + 0x7E4))


def reference(c):
    hc = initial_maps(c)[:c['nh']]
    pas = [bytes(1 if b else 0 for b in c['bytes'][k]) for k in range(5)]
    rec = next((r for r in c['recs'] if r['key'] == c['key']), None)
    size = 0 if c['rebuild'] else len(c['recs'])
    if rec is not None:
        after = [bytearray(m) for m in hc]
        m = classes(*c['mask'])
        for x, z in rec['cells']:
            col, row = cell(x), cell(z)
            for k in range(c['nh']):
                if m >> k & 1 and col < c['w'] and row < c['h']:
                    after[k][row * c['w'] + col] = 0
        if c['rebuild']:
            TALLY['rebuild cases that keep marks a remove would have cleared'] += after != hc
        else:
            hc, size = after, size - 1
    return [bytes(m) for m in hc], pas, size
