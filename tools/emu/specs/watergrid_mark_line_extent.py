"""docs/94 §9 (I-94-20): cells the high-cost line rasteriser writes, against the segment's end cells.

Target: WaterGrid_MarkB 0x670750 (thiscall(G; x0, z0, x1, z1, value byte, class, record vector), ret 0x1C), the only
writer WaterGrid_MarkFootprintPolygon 0x6733C0 uses: one call per polygon edge and selected class. Record vector 0 (no
record list), value 1, all maps zero before the call. floor (import thunk 0xF7EDF0) is replaced by a small x87 floor in
the scratch heap; ftol 0xF7EC40 runs as the game's code (x87 path).

Claim checked: every written cell lies inside the box of the two end cells (trunc(v) >> 2) widened by one cell, only the
selected class map is written, and the start cell is written. Output per case: (all cells within box+1, start cell
written, other maps untouched, cells outside the strict end-cell box). Reference: (True, True, True, any).
"""
import struct

NAME = 'WaterGrid line rasteriser extent (I-94-20)'
TARGET = 0x670750
CONV = 'thiscall'
CASES = 2000
FOLLOW = [0xF7EC40]
COVERAGE_TARGET = 0.6  # the record-list branch (record vector != 0) only appends cells; the map write does not depend on it
W = H = 64
# floor(double [esp+4]) -> ST0, rounding mode down for one frndint
FLOOR = bytes.fromhex('83EC08D93C24668B04246625FFF3660D00046689442404DD44240CD96C2404D9FCD92C2483C408C3')
TALLY = {'cases with a cell outside the strict end-cell box': 0}


class _Notes(list):
    def __iter__(self):
        return iter(list(super().__iter__()) + [f'tally (must be > 0, the +1 margin is needed): {TALLY}'])


NOTES = _Notes(['stub: floor import thunk 0xF7EDF0 patched to an x87 floor in the scratch heap'])


def cell(v):
    return (int(v) & 0xFFFFFFFF) >> 2


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def cases(rng, n):
    out = []
    for i in range(n):
        near = rng.random() < 0.3  # end points on or next to 4 m lines, where rounding decides the cell

        def coord():
            if near:
                return rng.randrange(1, 63) * 4 + rng.choice([0.0, -1e-5, 1e-5, -2.4e-7, 3.9999998, 0.5])
            return rng.uniform(0.5, 255.5)
        x0, z0, x1, z1 = (f32(coord()) for _ in range(4))
        r = rng.random()
        if r < 0.15:
            x1 = x0
        elif r < 0.3:
            z1 = z0
        out.append(dict(label=f'c{i}', p=(x0, z0, x1, z1), k=rng.randrange(10)))
    return out


def run(emu, c):
    fl = emu.alloc(64, 16, FLOOR)
    emu.write(0xF7EDF0, b'\xE9' + struct.pack('<i', fl - (0xF7EDF0 + 5)))
    g = emu.alloc(0x800)
    emu.write(g + 0x7A0, struct.pack('<IIII', W, H, 5, 10))
    arr, maps = emu.alloc(160), []
    for k in range(10):
        d = emu.alloc(W * H, 4, bytes(W * H))
        maps.append(d)
        emu.write(arr + 16 * k, struct.pack('<III', d, d + W * H, d + W * H))
    emu.write(g + 0x7C0, struct.pack('<III', arr, arr + 160, arr + 160))
    x0, z0, x1, z1 = c['p']
    emu.call(TARGET, [x0, z0, x1, z1, 1, c['k'], 0], CONV, this=g, follow=FOLLOW + [0xF7EDF0, fl])
    m = emu.read(maps[c['k']], W * H)
    cells = [(j % W, j // W) for j in range(W * H) if m[j]]
    cx0, cx1 = sorted((cell(x0), cell(x1)))
    cz0, cz1 = sorted((cell(z0), cell(z1)))
    in_box1 = all(cx0 - 1 <= x <= cx1 + 1 and cz0 - 1 <= z <= cz1 + 1 for x, z in cells)
    strict_out = sum(1 for x, z in cells if not (cx0 <= x <= cx1 and cz0 <= z <= cz1))
    TALLY['cases with a cell outside the strict end-cell box'] += strict_out > 0
    others = all(not any(emu.read(maps[k], W * H)) for k in range(10) if k != c['k'])
    return in_box1, (cell(x0), cell(z0)) in cells, others


def reference(c):
    return True, True, True
