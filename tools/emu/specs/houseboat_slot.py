"""docs/100 L7 (offline half): where WorldManager_FindHouseboatLotSlot 0xC6ACB0 puts a new houseboat lot.

thiscall(WM; a0, a1, out), ret 0xC, result in AL; out receives the slot centre as a 16-byte vector (x, y, z, x). Inputs from
the world manager: sea object WM+0x60 (height at +0x14, default -100 without one), world bounds minX +0xB4, maxX +0xBC,
maxZ +0xC0, the houseboat zone depth +0xC4 (float) and an integer extent +0xCC (columns = (extent - 8) / 72, signed).
Slots: x = minX + 8 + 72 col, z = maxZ + 8 + 72 row, y = sea + 3; a slot is free when World_LotLookup 0xC68990 (stubbed:
our occupancy set) finds no lot at (x + 0.5, z + 0.5). After the last column the next row is tried only while
x0 >= minX, z + 72 > maxZ, maxX > x0 and z + 72 < maxZ + depth; at most 256 lookups.

Model: the same walk in Python. The L7 point checked on every case: a returned slot has z >= maxZ + 8, so the houseboat
lot lies outside the water grid G (G spans minZ..maxZ, docs/100 §2) and its routing frame is the off-map slot.
"""
import struct

NAME = 'WorldManager_FindHouseboatLotSlot (L7)'
TARGET = 0xC6ACB0
CONV = 'thiscall'
CASES = 300
FOLLOW = []
OCC = set()
LOOKUPS = []
TALLY = {'slots returned': 0, 'returned slots with z <= maxZ': 0}


class _Notes(list):
    def __iter__(self):
        return iter(list(super().__iter__()) + [f'tally (second count must be 0): {TALLY}'])


NOTES = _Notes(['stub: World_LotLookup 0xC68990 answers from the case occupancy set (lot found = 1)'])
f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]


def setup(emu):
    def lookup(e, arg):
        x, z = struct.unpack('<ff', struct.pack('<II', arg(0), arg(1)))
        LOOKUPS.append((x, z))
        return 1 if (x, z) in OCC else 0
    emu.stub(0xC68990, lookup, pop=8, name='World_LotLookup')


def cases(rng, n):
    out = []
    E = [('no-sea', dict(sea=None)), ('first-free', dict(occ=0.0)), ('cols-zero', dict(extent=40)),
         ('zone-exhausted', dict(occ=1.0, depth=300.0)), ('tries-256', dict(occ=1.0, depth=1e6, extent=8 + 72 * 30)),
         ('negative-extent', dict(extent=-100)), ('min-x-positive', dict(minx=500.0, maxx=900.0))]
    for i in range(len(E) + n):
        lab, o = E[i] if i < len(E) else (f'rand{i}', {})
        c = dict(label=lab, sea=rng.choice([None, f32(rng.uniform(-5, 20))]), minx=f32(rng.choice([0.0, 0.0, 128.0])),
                 maxx=f32(rng.choice([1024.0, 2048.0, 300.0])), maxz=f32(rng.choice([1024.0, 2048.0, 512.0])),
                 depth=f32(rng.choice([360.0, 1000.0, 3000.0, 100.0])), extent=rng.choice([2048, 1024, 300, 8 + 72 * 3]),
                 occ=rng.choice([0.0, 0.5, 0.9, 1.0]), seed=rng.getrandbits(32))
        c.update(o)
        out.append(c)
    return out


def walk(c, occupied):
    """Python model of the slot walk: (found, (x, y, z), lookups)."""
    y = f32((c['sea'] if c['sea'] is not None else -100.0) + 3.0)
    x0, z0 = f32(c['minx'] + 8.0), f32(c['maxz'] + 8.0)
    q = c['extent'] - 8
    cols = abs(q) // 72 * (1 if q >= 0 else -1)
    col, row, n = 0, 0, 0
    for _ in range(256):
        x, z = f32(72 * col + x0), f32(72 * row + z0)
        n += 1
        if not occupied(f32(x + 0.5), f32(z + 0.5)):
            return 1, (x, y, z), n
        col += 1
        if col >= cols:
            row += 1
            col = 0
            zf = f32(72 * row + 72 + z0)
            if not (x0 >= c['minx'] and zf > c['maxz'] and c['maxx'] > x0 and f32(c['maxz'] + c['depth']) > zf):
                return 0, None, n
    return 0, None, n


def occupancy(c):
    import random
    r = random.Random(c['seed'])
    occ = set()
    for row in range(64):
        for col in range(40):
            if r.random() < c['occ']:
                occ.add((f32(f32(72 * col + f32(c['minx'] + 8.0)) + 0.5), f32(f32(72 * row + f32(c['maxz'] + 8.0)) + 0.5)))
    return occ


def run(emu, c):
    wm = emu.alloc(0x200)
    if c['sea'] is not None:
        sea = emu.alloc(0x20)
        emu.wf32(sea + 0x14, c['sea'])
        emu.w32(wm + 0x60, sea)
    emu.write(wm + 0xB4, struct.pack('<5f', c['minx'], 0.0, c['maxx'], c['maxz'], c['depth']))
    emu.write(wm + 0xCC, struct.pack('<i', c['extent']))
    OCC.clear()
    OCC.update(occupancy(c))
    LOOKUPS.clear()
    out = emu.alloc(16)
    r = emu.call(TARGET, [0, 0, out], CONV, this=wm)
    v = struct.unpack('<3f', emu.read(out, 12)) if r.eax & 0xFF else None
    return r.eax & 0xFF, v, len(LOOKUPS)


def reference(c):
    occ = occupancy(c)
    found, v, n = walk(c, lambda x, z: (x, z) in occ)
    if found:
        TALLY['slots returned'] += 1
        TALLY['returned slots with z <= maxZ'] += v[2] <= c['maxz']
    return found, v, n
