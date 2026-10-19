"""RoutingPath_CopyNodes 0x819E10: 64-byte search nodes -> 48-byte route nodes (docs/99 §11, F-4: what happens to y = -100).

thiscall(job)(a0, begin, end, a3, lastDir*, lastLen, path, a7) ret 0x20 (a0, a3, a7 are not read on the paths tested).
Per node it writes {x, y, z, x} (copied as they are), the unit direction to the next node and the segment length, both
over **x, y and z**, the running length before the segment, 0, and route flags 0x10 / 0x20 from search-node flag bits 0 / 1;
the last node gets *lastDir and lastLen. Then path vtable slot +0x130(total length).
Model: the same in double precision, compared with a relative tolerance (the exe uses rsqrtps + one Newton step).
Stubbed: 0x8292D0 (returns non-null), the vtable slot +0x130 (records the total). Not reached by design: the same-point
helper 0x819680 (consecutive points within 2^-16 m on all axes) and the vector growth 0x6438D0 (capacity is preset).
"""
import math
import struct

NAME = 'RoutingPath_CopyNodes'
TARGET = 0x819E10
CONV = 'thiscall'
FOLLOW = [0x819B60]  # vector reserve: returns at once, the capacity is preset
CASES = 300
COVERAGE_TARGET = 0.80
NOTES = ['not reached by design: same-point helper 0x819680 and vector growth 0x6438D0',
         'y = -100 (world-leg nodes, docs/99 §3) is copied unchanged and enters direction and length']
TOTAL = {}


def setup(emu):
    emu.stub(0x8292D0, ret=1, name='0x8292D0(stub non-null)')
    emu.stub(0x23FFF000, lambda e, a: TOTAL.__setitem__('v', struct.unpack('<f', struct.pack('<I', a(0)))[0]),
             pop=4, name='path vtable +0x130 (stub)')


def path_case(rng, label, n, world_share):
    nodes, x, z = [], rng.uniform(0, 2000), rng.uniform(0, 2000)
    for i in range(n):
        x += rng.choice([-1, 1]) * rng.uniform(0.5, 12)
        z += rng.choice([-1, 1]) * rng.uniform(0.5, 12)
        world = 0 < i < n - 1 and rng.random() < world_share
        y = -100.0 if world else rng.uniform(0, 40)
        nodes.append((x, y, z, rng.choice([0, 0, 1, 2, 3, 0x40])))
    return {'label': label, 'nodes': nodes, 'lastDir': (1.0, 0.0, 0.0, 0.0), 'lastLen': 0.0}


def cases(rng, n):
    out = [path_case(rng, 'two-lot-nodes', 2, 0), path_case(rng, 'lot-world-lot', 6, 1.0),
           path_case(rng, 'all-lot', 8, 0)]
    out += [path_case(rng, f'rand{i}', rng.randrange(2, 24), rng.random()) for i in range(n)]
    return out


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def run(emu, case):
    nodes = case['nodes']
    arr = emu.alloc(0x40 * len(nodes), 16)
    for i, (x, y, z, fl) in enumerate(nodes):
        emu.write(arr + 0x40 * i, struct.pack('<fff', x, y, z))
        emu.write(arr + 0x40 * i + 0x0E, struct.pack('<H', fl))
    path = emu.alloc(0x200, 16)
    vt = emu.alloc(0x200, 16)
    emu.w32(vt + 0x130, 0x23FFF000)
    emu.w32(path, vt)
    out = emu.alloc(0x30 * len(nodes) + 0x30, 16)
    emu.write(path + 0x98, struct.pack('<III', out, out, out + 0x30 * len(nodes)))
    last = emu.alloc(16, 16, struct.pack('<4f', *case['lastDir']))
    job = emu.alloc(0x40, 16)
    TOTAL.clear()
    emu.call(TARGET, [0, arr, arr + 0x40 * len(nodes), 0, last, case['lastLen'], path, 0], CONV, this=job, follow=FOLLOW)
    end = emu.u32(path + 0x9C)
    rows = []
    for p in range(out, end, 0x30):
        v = struct.unpack('<10f2I', emu.read(p, 0x30))
        rows.append((v[0:4], v[4:8], v[8], v[9], v[10], v[11]))
    return rows, TOTAL.get('v')


def reference(case):
    nodes, rows, run_len = case['nodes'], [], 0.0
    for i, (x, y, z, fl) in enumerate(nodes):
        x, y, z = f32(x), f32(y), f32(z)
        flags = (0x10 if fl & 1 else 0) | (0x20 if fl & 2 else 0)
        if i + 1 < len(nodes):
            nx, ny, nz = (f32(v) for v in nodes[i + 1][:3])
            d = (nx - x, ny - y, nz - z)
            ln = math.sqrt(sum(c * c for c in d))
            dr = (d[0] / ln, d[1] / ln, d[2] / ln, d[0] / ln)
        else:
            dr, ln = case['lastDir'], case['lastLen']
        rows.append(((x, y, z, x), dr, ln, run_len, 0, flags))
        run_len += ln
    return rows, run_len


def compare(got, want):
    close = lambda a, b: abs(a - b) <= 1e-5 * max(1.0, abs(b))
    (gr, gt), (wr, wt) = got, want
    if len(gr) != len(wr):
        return f'{len(gr)} nodes, want {len(wr)}'
    for i, (g, w) in enumerate(zip(gr, wr)):
        if g[0] != w[0] or g[4:] != w[4:]:
            return f'node {i}: position/flags {g[0]} {g[4:]} want {w[0]} {w[4:]}'
        if not (all(map(close, g[1], w[1])) and close(g[2], w[2]) and close(g[3], w[3])):
            return f'node {i}: dir {g[1]} len {g[2]} run {g[3]}, want {w[1]} {w[2]} {w[3]}'
    return None if close(gt, wt) else f'total {gt} want {wt}'
