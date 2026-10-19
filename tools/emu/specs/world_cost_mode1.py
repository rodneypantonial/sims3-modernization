"""RouteCost_Mode1 0x674E40 (world A* pedestrian cost, docs/99 §10.1) on real edges of Sunset Valley's R (docs/99 §11, F-13).

cdecl(R, G, fromRef*, toRef*, isEdge, edgeByte, opts*, parent*) -> float in ST0. R is the loader's in-memory form built by
`node tools/rtdata-decode.ts <world> --mem-image` (world: env TS3_WORLD, else <exe dir>/../worlds/Sunset Valley.world).
The model is docs/99 §10.1 mode 1 as written (real ends only; virtual water ends are not tested, they need G).
Stubbed (not run): LotEdgeGroup 0x653200 (W+0xA0 hash) -> per-case group table; EASTL set find 0x526490 -> per-case
start/goal group sets; terrain point test 0xC6BAB0 and RouteCost_Dispatch 0x627780 (polygon set) -> per-case answers.
"""
import math
import os
import random
import struct
import subprocess
import tempfile

from emu import f32
from unicorn import x86_const as ux

NAME = 'RouteCost_Mode1 (real SV edges)'
TARGET = 0x674E40
CONV = 'cdecl'
FOLLOW = [0x64FCE0]  # WaypointRef_Equal
CASES = 600
WM_SLOT = 0x11ECBC4
WALL, HUB = f32(1e8), f32(5e6)
# 74 of 110 blocks: the 32 blocks from 0x675417 on are the virtual-node (water) branch (chunk 0xFFFF ends, needs G), and
# 0x67519F, 0x6751CF, 0x675311, 0x67533F add 2^32 after fild of a negative chunk coordinate (unsigned fix-up, unreachable).
COVERAGE_TARGET = 0.67
NOTES = ['not reached: the virtual (water) branch from 0x675417 (needs G) and four unsigned fild fix-ups (unreachable)',
         'lot-edge groups, group sets, the terrain point test and the polygon test are per-case stubs (runtime state)']

R = {}  # filled by setup(): w, h, offsets, raw, recs {(c, i): rec}, by_byte {byte: [(c, i, k)]}


def world_path(emu):
    return os.environ.get('TS3_WORLD') or os.path.join(os.path.dirname(os.path.dirname(emu.exe_path)), 'worlds',
                                                       'Sunset Valley.world')


def load_image(emu):
    world = world_path(emu)
    if not os.path.exists(world):
        raise FileNotFoundError(world)
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, 'r.bin')
        subprocess.run(['node', os.path.join(repo, 'tools', 'rtdata-decode.ts'), world, '--mem-image', out],
                       check=True, capture_output=True, timeout=300)
        blob = open(out, 'rb').read()
    w, h = struct.unpack_from('<II', blob, 0)
    offsets = blob[8:8 + 4 * w * h]
    raw = blob[8 + 4 * w * h:]
    recs, by_byte = {}, {}
    for c in range(w * h):
        base = struct.unpack_from('<I', offsets, 4 * c)[0]
        cnt = struct.unpack_from('<H', raw, base)[0]
        for i in range(cnt):
            p = base + struct.unpack_from('<H', raw, base + 2 + 2 * i)[0]
            n, fl, _, bx, bz = raw[p:p + 5]
            refs = list(struct.unpack_from(f'<{n}H', raw, p + 5))
            eb = list(raw[p + 5 + 2 * n:p + 5 + 3 * n]) if fl & 0x40 else [0] * n
            recs[(c, i)] = {'p': p, 'type': fl & 0x1F, 'flags': fl, 'bx': bx, 'bz': bz, 'refs': refs, 'eb': eb}
            for k, b in enumerate(eb):
                by_byte.setdefault(b, []).append((c, i, k))
    R.update(w=w, h=h, offsets=offsets, raw=raw, recs=recs, by_byte=by_byte)
    NOTES.append(f'world {os.path.basename(world)}: {w}x{h} chunks, {len(recs)} waypoints, edges per byte '
                 + ' '.join(f'{b}:{len(v)}' for b, v in sorted(by_byte.items())))


def target(c, ref):
    dx, dz = ((ref >> 10) & 7) - 3, (ref >> 13) - 3
    return (c + dz * R['w'] + dx, ref & 0x3FF)


def pos(chunk, rec):
    return f32((chunk % R['w']) * 32 + rec['bx'] * 0.125), f32((chunk // R['w']) * 32 + rec['bz'] * 0.125)


# ---- stubs ----
def setup(emu):
    load_image(emu)

    def group(e, a):
        v = e.u32(a(0))
        return CUR['groups'].get((v & 0xFFFF, v >> 16), -1) & 0xFFFFFFFF

    def set_find(e, a):
        s = e.uc.reg_read(ux.UC_X86_REG_ECX)
        key = struct.unpack('<i', e.read(a(1), 4))[0]
        members = CUR['start_set'] if s == CUR['opts'] + 0x18 else CUR['goal_set']
        e.w32(a(0), CUR['node'] if key in members else s + 4)  # end() = the header at set+4
        return a(0)

    emu.stub(0x653200, group, pop=4, name='LotEdgeGroup(stub)')
    emu.stub(0x526490, set_find, pop=8, name='IntSet_Find(stub)')
    emu.stub(0xC6BAB0, lambda e, a: int(CUR['pt_ok']), pop=8, name='WM_PointRoutable(stub)')
    emu.stub(0x627780, lambda e, a: int(CUR['poly_hit']), pop=16, name='RouteCost_Dispatch(stub)')


CUR = {}  # the case being run (the stubs read it)


# ---- cases ----
def ref_u32(cw):
    return cw[0] | (cw[1] << 16)


def make_case(rng, label, kind):
    c = {'label': label, 'heur': kind == 'heur'}
    if kind == 'heur':
        fc, fi = rng.choice(list(R['recs']))
        rec = R['recs'][(fc, fi)]
        to = target(fc, rng.choice(rec['refs'])) if rec['refs'] and rng.random() < 0.7 else rng.choice(list(R['recs']))
        c.update(frm=(fc, fi), to=to, eb=0, parent=None)
    else:
        fc, fi, k = rng.choice(R['by_byte'][kind])
        c.update(frm=(fc, fi), to=target(fc, R['recs'][(fc, fi)]['refs'][k]), eb=kind, parent=None)
        frm = R['recs'][c['frm']]
        if kind == 1 and frm['type'] == 1 and rng.random() < 0.6:  # out of a hub: the expanded node's parent
            c['parent'] = target(fc, rng.choice(frm['refs']))
    hubs = [x for x in (c['frm'], c['to']) if R['recs'][x]['type'] == 1] or [rng.choice(list(R['recs']))]
    pick = lambda: rng.choice(hubs) if rng.random() < 0.6 else rng.choice(list(R['recs']))
    c['start_hub'], c['goal_hub'] = pick(), pick()
    c['ref14'] = c['to'] if rng.random() < 0.3 else (0x7777, 1)
    c['ref44'] = c['frm'] if rng.random() < 0.3 else (0x7777, 2)
    c['flags'] = sum(b for b in (1, 2, 4, 8, 0x10, 0x20, 0x80, 0x100, 0x200) if rng.random() < 0.25)
    refs = [c['frm'], c['to']] + ([c['parent']] if c['parent'] else [])
    c['groups'] = {x: rng.randrange(-1, 3) for x in refs if rng.random() < 0.9}
    c['start_set'] = {g for g in range(-1, 3) if rng.random() < 0.6}
    c['goal_set'] = {g for g in range(-1, 3) if rng.random() < 0.6}
    c['blocked'] = rng.random() < 0.12
    c['to_type'] = 0x15 if R['recs'][c['to']]['type'] == 2 and rng.random() < 0.08 else None
    c['wm'] = rng.random() < 0.9
    c['pt_ok'] = rng.random() < 0.85
    c['poly_hit'] = rng.random() < 0.15
    c['keep'] = f32(rng.choice([1.3, 1.3, 1.0, 2.0]))  # the tuning slots are float32
    c['trespass'] = 0.0 if rng.random() < 0.05 else f32(rng.choice([1.3, 2.0, 1.0]))
    c['sidewalk'], c['riding'], c['street'] = map(f32, rng.choice([(1.5, 2.0, 3.0), (1.7, 10.0, 6.0), (2.0, 2.0, 2.0)]))
    tx, tz = pos(c['to'][0], R['recs'][c['to']])
    if rng.random() < 0.25:
        c['circle'] = (f32(tx + rng.uniform(-60, 60)), f32(tz + rng.uniform(-60, 60)), f32(rng.uniform(100, 4000)))
    else:
        c['circle'] = (0.0, 0.0, 0.0)
    c['start_pt'] = (f32(rng.uniform(0, 2048)), f32(rng.uniform(0, 2048)))
    c['goal_pt'] = (f32(rng.uniform(0, 2048)), f32(rng.uniform(0, 2048)))
    return c


def cases(rng, n):
    out = []
    # edge cases: every jump-table byte and each hub sub-branch at least once
    for b in (1, 1, 1, 1, 2, 2, 3, 5, 6, 7):
        out.append(make_case(random.Random(b * 7919 + len(out)), f'edge-byte{b}-{len(out)}', b))
    for i in range(n):
        r = rng.random()
        kind = 'heur' if r < 0.1 else 1 if r < 0.5 else 2 if r < 0.8 else rng.choice([3, 5, 6, 7])
        out.append(make_case(rng, f'rand{i}-{kind}', kind))
    return out


# ---- run ----
def run(emu, case):
    CUR.clear()
    CUR.update(case)
    raw = bytearray(R['raw'])
    trec = R['recs'][case['to']]
    if case['blocked']:
        raw[trec['p'] + 1] |= 0x80
    if case['to_type'] is not None:
        raw[trec['p'] + 1] = (raw[trec['p'] + 1] & 0xE0) | case['to_type']
    offs = emu.alloc(len(R['offsets']), 16, R['offsets'])
    rawp = emu.alloc(len(raw), 16, bytes(raw))
    r = emu.alloc(0x40, 16)
    emu.write(r, struct.pack('<IIII', R['w'], R['h'], offs, rawp))
    w = emu.alloc(0x200, 16)
    emu.w32(w + 8, emu.alloc(0x40, 16))  # P (only passed to the stubbed polygon test)
    o = emu.alloc(0x100, 16)
    CUR['opts'], CUR['node'] = o, emu.alloc(0x20, 16)
    emu.w32(o, case['flags'])
    emu.w32(o + 0x04, ref_u32(case['start_hub']))
    emu.write(o + 0x08, struct.pack('<ff', *case['start_pt']))
    emu.w32(o + 0x14, ref_u32(case['ref14']))
    emu.w32(o + 0x34, ref_u32(case['goal_hub']))
    emu.write(o + 0x38, struct.pack('<ff', *case['goal_pt']))
    emu.w32(o + 0x44, ref_u32(case['ref44']))
    sw, rd, st = case['sidewalk'], case['riding'], case['street']
    emu.write(o + 0x64, struct.pack('<9f', case['keep'], case['trespass'], sw, rd, st, 2.0, f32(1 / sw), f32(1 / rd), f32(1 / st)))
    emu.w32(o + 0x88, w)
    emu.w32(o + 0x8C, emu.alloc(0x10, 16))
    cx_, cz_, r2 = case['circle']
    emu.write(o + 0x90, struct.pack('<ffff', cx_, cz_, 0.0, 0.0))
    emu.wf32(o + 0xA0, r2)
    fr = emu.alloc(4, 4, struct.pack('<HH', *case['frm']))
    to = emu.alloc(4, 4, struct.pack('<HH', *case['to']))
    par = 0
    if case['parent']:
        par = emu.alloc(0x14, 16)
        emu.write(par + 8, struct.pack('<HH', *case['parent']))
    emu.w32(WM_SLOT, emu.alloc(0x10, 16) if case['wm'] else 0)
    res = emu.call(TARGET, [r, 0, fr, to, 0 if case['heur'] else 1, case['eb'], o, par], CONV, follow=FOLLOW)
    return f32(res.st0)


# ---- model: docs/99 §10.1 mode 1, real ends ----
def reference(c):
    recs = dict(R['recs'])
    T = dict(recs[c['to']])
    if c['blocked']:
        T['flags'] |= 0x80
    if c['to_type'] is not None:
        T['type'] = c['to_type']
    F = recs[c['frm']]
    fl, eb = c['flags'], c['eb']
    grp = lambda x: c['groups'].get(x, -1)
    src, k = F, 10.0
    if c['heur']:
        k = 1.0
    else:
        if fl & 0x200 and T['type'] == 2:
            return WALL
        if T['type'] == 0x15 and not fl & 0x180:
            return WALL
        if T['flags'] & 0x80 and not (F['type'] in (2, 0x15) and T['type'] in (2, 0x15) and F['refs'][0] == T['refs'][0]):
            return WALL
        if eb == 0:
            return WALL
        if eb == 1:
            if c['to'] == c['start_hub'] and fl & 0x10:
                return HUB
            if c['to'] == c['goal_hub']:
                if fl & 0x20:
                    return HUB
                if grp(c['frm']) not in c['goal_set']:
                    return WALL
                k = 10.0 if fl & 8 or c['frm'] == c['ref44'] else 30.0
            elif T['type'] == 1:
                k = 10.0
            elif c['parent']:
                src = recs[c['parent']]
                k = f32(c['trespass'] * (1000.0 if grp(c['to']) != grp(c['parent']) else 10.0))
            else:
                if grp(c['to']) not in c['start_set']:
                    return WALL
                k = 10.0 if fl & 4 or c['to'] == c['ref14'] else 30.0
        elif eb == 2:
            k = f32(c['trespass'] * (1000.0 if grp(c['frm']) != grp(c['to']) else 10.0))
        elif eb in (3, 4, 5):
            k = f32(f32(1 / c['sidewalk']) * 10.0)
        if src['type'] != T['type']:
            k = f32(c['keep'] * k)
        if k <= 0:
            return 0.0
    if fl & 2 and T['type'] == 1 and c['to'] == c['goal_hub']:
        tp = c['goal_pt']
        k = f32(c['keep'] * k)
    else:
        tp = pos(c['to'][0], T)
        if T['type'] in (2, 0x15) and c['wm'] and not c['pt_ok']:
            return WALL
    cx_, cz_, r2 = c['circle']
    if r2 > 0 and eb != 3:
        if f32(f32((tp[0] - cx_) ** 2) + f32((tp[1] - cz_) ** 2)) > r2:
            return WALL
    if fl & 1 and src['type'] == 1 and c['frm'] == c['start_hub']:
        fp = c['start_pt']
        k = f32(c['keep'] * k)
    else:
        fp = pos(c['frm'][0], src)  # the parent's in-chunk offsets with the from chunk (docs/99 §10.1 4d)
    if not c['heur'] and eb in (2, 7) and c['poly_hit']:
        return WALL
    dx, dz = f32(fp[0] - tp[0]), f32(fp[1] - tp[1])
    return f32(f32(math.sqrt(f32(f32(dx * dx) + f32(dz * dz)))) * k)
