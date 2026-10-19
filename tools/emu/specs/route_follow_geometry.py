"""Route-follow geometry reads of a RoutingPathImp (docs/99 §12, F-4b): are they 3D, and what does y = -100 do to them.

Target: path slot +0x118 `RoutingPath_ProjectPoint 0x821820` (thiscall(path)(point*) -> distance along the path in ST0),
which runs `0x821120` (local search from cursor +0x108, then a global split search when the local best is further than
1 cm) over `RoutingPath_SegmentClosest 0x81D800`. Same case: path slot +0x11C `RoutingPath_PointAtDistance 0x81D9F0`
(thiscall(path)(outPos*, dist, outDir*, outIdx*)), at the projected distance plus 0.5 m (a follower look-ahead).
48-byte nodes as CopyNodes writes them (docs/99 §11.2): {x, y, z, x}, unit direction over x, y, z, length +0x20,
running length +0x24, flags +0x2C; path total +0x8C.

Model (written from the disassembly as text): the distance is running length + t of the segment with the smallest
**3D** distance to the point (t clamped to [0, length]); the point at distance d is node_i + (d - run_i) * dir_i.
Three node layouts per case: 'real' (every node at terrain height h), 'flat' (every node at -100, the lot-path writer
0x62F140 and world records, docs/99 §11.2) and 'prepended' (flat nodes behind one node at the Sim's height h, the
shape path slot +0x100 0x821E90 builds when the follower prepends the agent position at route start). The Sim stands
on the path's xz track at height h, `q` metres along it.
Stubbed: free 0x4E3F70 and aligned alloc 0x4E3FB0 (the global search's work list). Not reached by design: the end-point shortcut (point within
1 mm of the end with a linked path +0x184), the cached-hit branch (+0x144 set), vector growth.
"""
import math
import struct

NAME = 'RoutingPath_ProjectPoint + PointAtDistance (3D follow reads)'
TARGET = 0x821820
CONV = 'thiscall'
FOLLOW = [0x821120, 0x81D800, 0x81D8D0, 0x8210C0, 0x81D9F0, 0x820170, 0x81F7C0, 0x81E930, 0x820330, 0x81D750]
CASES = 300
COVERAGE_TARGET = 1.0
NOTES = ['all three layouts follow the same 3D model; flat -100 nodes give 2D answers (constant y offset), a prepended '
         'node at the Sim height gives a steep first segment whose projected progress is q * L2 / L3',
         'not reached by design in 0x821120: end-point shortcut, cached-hit branch, vector growth']
ALLOC_MARK = 0x4E3F70


def setup(emu):
    emu.stub(0x4E3F70, ret=0, name='free 0x4E3F70 (stub)')
    emu.stub(0x4E3FB0, lambda e, a: e.alloc(a(0), max(16, a(1))), name='aligned alloc 0x4E3FB0 (stub)')


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def track(rng, n):
    pts, x, z = [], rng.uniform(0, 500), rng.uniform(0, 500)
    for _ in range(n):
        pts.append((x, z))
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(1.0, 12.0)
        x, z = x + d * math.cos(a), z + d * math.sin(a)
    return pts


def build_nodes(xyz):
    """CopyNodes in double precision, rounded to float32 per field."""
    nodes, run = [], 0.0
    for i, (x, y, z) in enumerate(xyz):
        if i + 1 < len(xyz):
            nx, ny, nz = xyz[i + 1]
            d = (nx - x, ny - y, nz - z)
            ln = math.sqrt(sum(c * c for c in d))
            dr = (d[0] / ln, d[1] / ln, d[2] / ln)
        else:
            dr, ln = (1.0, 0.0, 0.0), 0.0
        nodes.append(tuple(f32(v) for v in (x, y, z)) + tuple(f32(v) for v in dr) + (f32(ln), f32(run)))
        run = f32(run + f32(ln))
    return nodes, run


def cases(rng, n):
    out = []
    for i in range(n):
        layout = ['real', 'flat', 'prepended'][i % 3]
        k = rng.randrange(2, 9)
        pts = track(rng, k)
        h = rng.uniform(0.0, 40.0)
        if layout == 'real':
            xyz = [(x, h, z) for x, z in pts]
        elif layout == 'flat':
            xyz = [(x, -100.0, z) for x, z in pts]
        else:
            xyz = [(pts[0][0], h, pts[0][1])] + [(x, -100.0, z) for x, z in pts[1:]]
        seg = 0
        L2 = math.dist(pts[0], pts[1])
        q = rng.uniform(0.05, 0.95) * L2
        ux_, uz = (pts[1][0] - pts[0][0]) / L2, (pts[1][1] - pts[0][1]) / L2
        sim = (pts[0][0] + q * ux_, h, pts[0][1] + q * uz)
        out.append({'label': f'{layout}-{k}', 'layout': layout, 'xyz': xyz, 'sim': sim, 'q': q, 'seg': seg})
    return out


def run(emu, case):
    nodes, total = build_nodes(case['xyz'])
    arr = emu.alloc(0x30 * len(nodes), 16)
    for i, nd in enumerate(nodes):
        x, y, z, dx, dy, dz, ln, rn = nd
        emu.write(arr + 0x30 * i, struct.pack('<4f4f2f2I', x, y, z, x, dx, dy, dz, dx, ln, rn, 0, 0))
    path = emu.alloc(0x200, 16)
    end = arr + 0x30 * len(nodes)
    emu.write(path + 0x98, struct.pack('<III', arr, end, end))
    emu.wf32(path + 0x8C, total)
    sx, sy, sz = case['sim']
    pt = emu.alloc(16, 16, struct.pack('<4f', sx, sy, sz, sx))
    emu.call(TARGET, [pt], CONV, this=path, follow=FOLLOW)
    s = emu.st0()
    look = f32(s + 0.5)
    out, dr, idx = emu.alloc(16, 16), emu.alloc(16, 16), emu.alloc(4, 4)
    emu.call(0x81D9F0, [out, look, dr, idx], CONV, this=path, follow=[])
    pos = struct.unpack('<3f', emu.read(out, 12))
    return s, pos, total


def reference(case):
    nodes, total = build_nodes(case['xyz'])
    p = case['sim']
    best = None
    for i in range(len(nodes) - 1):
        a, d, ln, rn = nodes[i][0:3], nodes[i][3:6], nodes[i][6], nodes[i][7]
        t = sum((p[k] - a[k]) * d[k] for k in range(3))
        t = min(max(t, 0.0), ln)
        c = [a[k] + t * d[k] for k in range(3)]
        dist = sum((p[k] - c[k]) ** 2 for k in range(3))
        if best is None or dist < best[0]:
            best = (dist, rn + t)
    s = best[1]
    look = s + 0.5
    pos = None
    for i in range(len(nodes) - 1):
        a, d, ln, rn = nodes[i][0:3], nodes[i][3:6], nodes[i][6], nodes[i][7]
        if rn <= look < rn + ln:
            pos = tuple(a[k] + (look - rn) * d[k] for k in range(3))
            break
    if pos is None:
        last = nodes[-1]
        pos = tuple(last[k] + last[6] * last[3 + k] for k in range(3))
    return s, pos, total


def compare(got, want):
    tol = lambda a, b: abs(a - b) <= 2e-3 * max(1.0, abs(b))
    if got[0] == 'EmuError':
        return got[1]
    (gs, gp, gt), (ws, wp, wt) = got, want
    if not tol(gs, ws):
        return f'distance {gs} want {ws}'
    if not all(tol(a, b) for a, b in zip(gp, wp)):
        return f'look-ahead point {gp} want {wp}'
    return None
