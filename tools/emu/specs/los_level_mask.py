"""I-105-2 (docs/105 §1, §6.3, §9): does the display-level raise of World_CanSeeObject change the line-of-sight answer?

Target: the physics leaf visitor 0x680F80 (stdcall(ray, mask, leaf, outDist, result), ret 0x14) that
PhysicsWorld_RayCastSegment 0x686FD0 reaches through 0x681E50 -> 0x681820 -> 0x6814B0. Before the visitor runs, every
case drives the game's own Lot_SetDisplayLevel 0xAC0570 (thiscall(lot; level, forced, b, quiet), ret 0x10, called as
World_CanSeeObject calls it: forced 1, b 1, quiet 1) on a constructed lot whose per-level table (deque at lot+0x378/+0x37C,
block map +0x384) holds one physics group per level, through PhysicsWorld_Get 0x686A90 -> 0x686B30 -> 0x681F20 (the bit
0x10000 writer of group+0x2C). Sequence per case: set display level D0 (the camera's floor), optionally raise to the
target level L (what World_CanSeeObject does), run the visitor over wall shapes parented to the level groups, then restore
D0 (and check the group words come back).

Mask: 0x686B40(type 0x10) = 0x8FFFF; 0x686FD0 ORs 0x10000 when its 5th argument is set (World_CanSeeObject passes 1).
The visitor skips a shape unless (word & mask) > (mask & 0xFFFF0000), unsigned, for every group in its +0x50 / +0x10
parent chain and for its own word +0x34; then a bounding-sphere test (squared distance to the ray < shape+0x30), then the
shape's vtable slot +0x10 (stubbed: returns the hit and the distance we planted), nearest hit wins.

Model: groups for levels above the effective display level have the bit clear, so with the line-of-sight mask their walls
are invisible to the ray; with the plain mask (0x8FFFF) the bit does not matter. Output per case: index of the blocking
shape (-1 = visible), its distance, camera-path calls during the raise and during the restore, and whether the restore
put every group word back.
"""
import struct
from unicorn import x86_const as ux

NAME = 'LoS level mask: Lot_SetDisplayLevel + physics leaf visitor'
TARGET = 0x680F80
CONV = 'stdcall'
CASES = 300
FOLLOW = []
SETLEVEL = 0xAC0570
SETLEVEL_FOLLOW = [0x686A90, 0x686B30, 0x681F20]
PW_GLOBAL = 0x11CF490
MASK_PLAIN, MASK_LOS = 0x8FFFF, 0x9FFFF
SLOT = 0x0F000000 + 0x10000 - 4  # a free trap slot (imports use the first ones): the shapes' vtable +0x10
BEST0 = 1e30
THR = [0.0]  # value at 0xF98964, read in setup
TALLY = {'cases whose answer depends on the level bit': 0, 'of them with the raise': 0}


class _Notes(list):
    def __iter__(self):  # printed after the run: a model without the level bit would fail this many cases
        return iter(list(super().__iter__()) + [f'tally (self-check, must be > 0): {TALLY}'])


NOTES = _Notes(['stubs: shape vtable +0x10 (ray vs shape: returns the planted hit flag and distance), 0x96B390 and '
                '0xF20C20 (camera path of Lot_SetDisplayLevel when the level goes down; counted, return 0); lot+0x2AC = 0 '
                'so the per-level lock test 0xB95DF0 is not reached (clamp to max + 1)'])


def setup(emu):
    THR[0] = struct.unpack('<f', emu.read(0xF98964, 4))[0]
    # the ray's mask for type 0x10 (0x686B40: switch on type - 0xB, table 0x686B80), before 0x686FD0 ORs 0x10000
    case = emu.u32(0x686B80 + 4 * (0x10 - 0xB))
    assert emu.read(case, 6) == b'\xB8' + struct.pack('<I', MASK_PLAIN) + b'\xC3', f'0x686B40 case 0x10 at {case:#x}'
    assert emu.read(0x6870AB, 5) == b'\x0D\x00\x00\x01\x00', '0x686FD0: or eax, 0x10000 expected at 0x6870AB'

    def intersect(e, arg):
        shape = e.uc.reg_read(ux.UC_X86_REG_ECX)
        out = arg(1)
        e.write(out + 0x20, struct.pack('<fI', e.rf32(shape + 0x70), shape))
        return e.u32(shape + 0x74)
    emu.stub(SLOT, intersect, pop=8, name='shape vtable +0x10 (ray test)')
    emu.stub(0x96B390, ret=0, name='camera 0x96B390')
    emu.stub(0xF20C20, ret=0, name='camera 0xF20C20')


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def edge_cases():
    wall = lambda lv, t, **k: dict(dict(kind='shape', parent=lv, t=t, hit=1, own=0x80001, off=0.0), **k)
    base = dict(n=2, mn=0, d0=0, L=1, los=True, raise_=False, root=0x90001, gflags=[0x80001] * 4,
                shapes=[wall(1, 2.0)])
    E = [('upper-wall-no-raise', {}), ('upper-wall-raise', dict(raise_=True)),
         ('upper-wall-plain-mask', dict(los=False)),
         ('ground-wall-no-raise', dict(shapes=[wall(0, 2.0)])),
         ('basement', dict(n=3, mn=-1, d0=-1, L=1, raise_=True, shapes=[wall(2, 3.0), wall(1, 2.0)])),
         ('clamp-above-max', dict(L=5, raise_=True, shapes=[wall(1, 2.0)])),
         ('d0-above-max', dict(d0=3, shapes=[wall(1, 2.0)])),
         ('nearest-wins', dict(raise_=True, shapes=[wall(0, 4.0), wall(1, 2.0)])),
         ('farther-second', dict(raise_=True, shapes=[wall(1, 2.0), wall(0, 4.0)])),
         ('null-shape', dict(shapes=[dict(kind='null'), wall(0, 2.0)])),
         ('no-parent', dict(shapes=[wall(None, 2.0)])),
         ('root-parent', dict(shapes=[wall('root', 2.0)])),
         ('root-lacks-bit', dict(root=0x80001, shapes=[wall('root', 2.0)])),
         ('own-word-fails', dict(raise_=True, shapes=[wall(1, 2.0, own=0x10001)])),
         ('own-low-bits-zero', dict(raise_=True, shapes=[wall(1, 2.0, own=0x90000)])),
         ('sphere-miss', dict(raise_=True, shapes=[wall(1, 2.0, off=2.0)])),
         ('ray-test-miss', dict(raise_=True, shapes=[wall(1, 2.0, hit=0)])),
         ('t-not-positive', dict(raise_=True, shapes=[wall(1, 0.0)])),
         ('empty-leaf', dict(shapes=[])),
         ('group-word-no-low-bits', dict(raise_=True, gflags=[0x80000] * 4)),
         ('target-below-display', dict(d0=1, L=0, raise_=True))]
    return [dict(base, label=l, **o) for l, o in E]


def cases(rng, n):
    out = edge_cases()
    words = [0x80001, 0x80001, 0x90001, 0x80000, 0x1, 0x10001, 0x8F00F]
    for i in range(n):
        lv = rng.randrange(1, 5)
        mn = rng.choice([0, 0, -1])
        shapes = []
        for _ in range(rng.randrange(0, 5)):
            if rng.random() < 0.08:
                shapes.append(dict(kind='null'))
                continue
            p = rng.random()
            parent = None if p < 0.05 else 'root' if p < 0.12 else rng.randrange(lv)
            shapes.append(dict(kind='shape', parent=parent, t=f32(rng.choice([0.0, -1.0]) if rng.random() < 0.05
                                                                   else rng.uniform(0.1, 20.0)),
                               hit=0 if rng.random() < 0.1 else 1, own=rng.choice(words[:3]) if rng.random() < 0.9
                               else rng.choice(words), off=rng.choice([0.0, 0.0, 0.0, 0.5, 2.0])))
        out.append(dict(label=f'rand{i}', n=lv, mn=mn, d0=rng.randrange(mn, mn + lv + 1),
                        L=rng.randrange(mn, mn + lv + 2), los=rng.random() < 0.8, raise_=rng.random() < 0.6,
                        root=rng.choice([0x90001, 0x90001, 0x80001]),
                        gflags=[rng.choice(words[:3]) if rng.random() < 0.85 else rng.choice(words) for _ in range(4)],
                        shapes=shapes))
    return out


def build(emu, c):
    pw = emu.alloc(0x20)
    cont = emu.alloc(0x20)
    root = emu.alloc(0x40)
    emu.w32(root + 0x2C, c['root'])
    emu.w32(cont, root)
    emu.w32(pw + 8, cont)
    emu.w32(PW_GLOBAL, pw)
    groups = []
    for i in range(c['n']):
        g = emu.alloc(0x40)
        emu.w32(g + 0x2C, c['gflags'][i])
        emu.w32(g + 0x10, root)
        groups.append(g)
    block = emu.alloc(64 * 4)
    for i, g in enumerate(groups):
        emu.w32(block + 4 * i, g)
    bmap = emu.alloc(4)
    emu.w32(bmap, block)
    lot = emu.alloc(0x500)
    mx = c['mn'] + c['n'] - 1
    emu.write(lot + 0x17C, struct.pack('<iii', c['mn'], mx, c['n'] - 1))
    emu.w32(lot + 0x2AC, 0)
    emu.w32(lot + 0x39C, 0)  # a lot never viewed sits at display level 0 (docs/107 §5.2)
    emu.w32(lot + 0x378, block)
    emu.w32(lot + 0x37C, block)
    emu.w32(lot + 0x384, bmap)
    vt = emu.alloc(0x20)
    emu.w32(vt + 0x10, SLOT)
    nodes, shapes = [], []
    for s in c['shapes']:
        node = emu.alloc(0x10)
        sh = 0
        if s['kind'] == 'shape':
            sh = emu.alloc(0x80)
            emu.w32(sh, vt)
            emu.write(sh + 0x10, struct.pack('<4f', 3.0, s['off'], 0.0, 0.0))
            emu.write(sh + 0x20, struct.pack('<4f', 0.0, 0.0, 0.0, 1.0))
            emu.write(sh + 0x30, struct.pack('<fI', 1.0, s['own']))
            par = s['parent']
            emu.w32(sh + 0x50, 0 if par is None else root if par == 'root' else groups[par])
            emu.write(sh + 0x70, struct.pack('<fI', s['t'], s['hit']))
        emu.w32(node + 8, sh)
        nodes.append(node)
        shapes.append(sh)
    for a, b in zip(nodes, nodes[1:]):
        emu.w32(a, b)
    leaf = emu.alloc(0x20)
    emu.w32(leaf + 0x14, nodes[0] if nodes else 0)
    emu.w32(leaf + 0x18, len(nodes))
    ray = emu.alloc(0x20)
    emu.write(ray, struct.pack('<8f', 0, 0, 0, 0, 1, 0, 0, 0))
    res = emu.alloc(0x30)
    emu.wf32(res + 0x20, BEST0)
    dist = emu.alloc(4)
    return lot, groups, root, shapes, leaf, ray, res, dist


def set_level(emu, lot, level):
    r = emu.call(SETLEVEL, [level & 0xFFFFFFFF, 1, 1, 1], 'thiscall', this=lot, follow=SETLEVEL_FOLLOW)
    return r.stubbed.get('camera 0xF20C20', 0)


def run(emu, c):
    lot, groups, root, shapes, leaf, ray, res, dist = build(emu, c)
    set_level(emu, lot, c['d0'])
    words = lambda: [emu.u32(g + 0x2C) for g in groups + [root]]
    before = words()
    cam_up = set_level(emu, lot, c['L']) if c['raise_'] else 0
    emu.call(TARGET, [ray, MASK_LOS if c['los'] else MASK_PLAIN, leaf, dist, res], CONV)
    best = emu.u32(res + 0x24)
    hit = shapes.index(best) if best else -1
    t = round(emu.rf32(res + 0x20), 4) if best else None
    cam_down = set_level(emu, lot, c['d0']) if c['raise_'] else 0
    return hit, t, cam_up, cam_down, words() == before


def reference(c):
    want = model(c)
    if c['los'] and model(dict(c, los=False))[:2] != want[:2]:
        TALLY['cases whose answer depends on the level bit'] += 1
        TALLY['of them with the raise'] += c['raise_']
    return want


def model(c):
    mn, mx = c['mn'], c['mn'] + c['n'] - 1
    eff = lambda lv: max(min(lv, mx + 1), mn)
    cur = eff(c['d0'])
    gw = [(c['gflags'][i] | 0x10000) if i <= cur - mn else (c['gflags'][i] & ~0x10000) for i in range(c['n'])]
    cam_up = cam_down = 0
    disp = cur
    if c['raise_']:
        disp = eff(c['L'])
        cam_up = int(disp < cur)
        gw = [(w | 0x10000) if i <= disp - mn else (w & ~0x10000) for i, w in enumerate(gw)]
        cam_down = int(cur < disp)
    m = MASK_LOS if c['los'] else MASK_PLAIN
    hi = m & 0xFFFF0000
    ok = lambda w: (w & m) > hi
    best, bi = BEST0, -1
    for i, s in enumerate(c['shapes']):
        if s['kind'] != 'shape':
            continue
        chain = [] if s['parent'] is None else [c['root']] if s['parent'] == 'root' else [gw[s['parent']], c['root']]
        if not all(ok(w) for w in chain) or not ok(s['own']) or not s['off'] ** 2 < 1.0 or not s['hit']:
            continue
        if s['t'] < best and s['t'] > THR[0]:
            best, bi = s['t'], i
    return bi, (round(f32(best), 4) if bi >= 0 else None), cam_up, cam_down, True
