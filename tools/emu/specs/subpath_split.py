"""RoutingJob_SplitSubPaths 0x81A340: which subpaths and PathType values come out of a 64-byte node array (docs/99 §11, F-3).

thiscall(job)(nodeOwner, outVector) ret 8; nodes at nodeOwner+8/+0xC (begin, end), subpath records of 0x40 bytes pushed
into outVector (begin, end, capacity) by 0x81A0E0 (PathType at +0x10, first node pointer at +0x00, object id +0x18/+0x1C);
route options at job+0x40 (public word +0x28/+0x2C, internal +0x30/+0x34), tested by 0x81C880.
Each case is a plain pedestrian route (start type 6, goal type 7, inner nodes type 0) with one inner run changed: one
flag bit of +0x0E, a node type, a kind hash or an option bit. The model (reference) is the table docs/99 §11.1 reports.
Stubbed: 0x8292D0 (non-null), the object lookups 0x938B50 / 0x939560 / 0xB01770 (portal object id -> 0 = not found).
"""
import struct

NAME = 'RoutingJob_SplitSubPaths'
TARGET = 0x81A340
CONV = 'thiscall'
FOLLOW = [0x81C880, 0x81A0E0, 0x81A180, 0x81A1E0, 0x7AA5C0, 0x819640, 0x819580, 0x819B20, 0x8195E0]
CASES = 300
# 182 of 240 blocks: not reached are the portal object lookups (stubbed to 'not found'), vector growth (capacity preset),
# portal exit (type 3) pairing with real portal objects and the begin/end-as-stroller paths
COVERAGE_TARGET = 0.75
NOTES = ['model = observed table (docs/99 §11.1); random cases add +0x0E bits the split never reads and any +0x38',
         'BeginAsCar / BeginAsBoat erase the nodes before the first car/boat node (0x7AA5C0), so indices restart']

PT = {v: k for k, v in dict(  # SimIFace PathType enum values
    Sim=3328183143, Vehicle=4027457938, Portal=1348865362, PortalTraversal=1144997326, HorseLane=1584002891,
    Stroller=3780947909, UFO=2324053554, Ocean=2583738316, Wade=3919275477, FrozenPond=2309617367, Boat=2370016522,
    ElevatedTrain=1858382698).items()}


def setup(emu):
    emu.stub(0x8292D0, ret=1, name='0x8292D0(stub non-null)')
    emu.stub(0x938B50, ret=0, name='0x938B50(stub)')
    emu.stub(0x939560, ret=0, name='0x939560(stub)')
    emu.stub(0xB01770, ret=0, name='0xB01770(stub)')


def route(n=8, run=(3, 5), **chg):
    """Plain pedestrian route: start type 6, inner type 0, goal type 7; nodes run[0]..run[1]-1 get chg."""
    nodes = []
    for i in range(n):
        nd = {'type': 6 if i == 0 else 7 if i == n - 1 else 0, 'flags': 0, 'kind': 0xB828, 'cls': 0}
        if run[0] <= i < run[1]:
            nd.update(chg)
        nodes.append(nd)
    return nodes


def run(emu, case):
    nodes = case['nodes']
    arr = emu.alloc(0x40 * len(nodes) + 0x40, 16)
    for i, nd in enumerate(nodes):
        p = arr + 0x40 * i
        emu.write(p, struct.pack('<fff', float(3 * i), 0.0, 0.0))
        emu.write(p + 0x0E, struct.pack('<H', nd['flags']))
        emu.write(p + 0x14, struct.pack('<If', 0xFFFFFC19, 1.0))
        emu.w32(p + 0x1C, nd['type'])
        emu.w32(p + 0x2C, nd['kind'])
        emu.w32(p + 0x38, nd['cls'])
    owner = emu.alloc(0x20, 16)
    emu.write(owner + 8, struct.pack('<II', arr, arr + 0x40 * len(nodes)))
    out = emu.alloc(0x40 * 64, 16)
    vec = emu.alloc(0x10, 16, struct.pack('<III', out, out, out + 0x40 * 64))
    job = emu.alloc(0x200, 16)
    emu.write(job + 0x40 + 0x28, struct.pack('<QQ', case.get('opt', 0), case.get('opt2', 0)))
    emu.call(TARGET, [owner, vec], CONV, this=job, follow=FOLLOW)
    # per subpath: PathType and the index of its last node (record +0x00 points at it)
    return [(PT.get(emu.u32(p + 0x10), hex(emu.u32(p + 0x10))), (emu.u32(p) - arr) // 0x40)
            for p in range(out, emu.u32(vec + 4), 0x40)]


# RouteOption / RouteOption2 bits (SimIFace Route.cs) that the splitter tests through 0x81C880
CAR, NOPED, BEGINCAR, STROLLER, BEGINSTROLLER, ENDSTROLLER = 0x10, 0x20, 0x40, 1 << 39, 1 << 41, 1 << 42
MOUNTED, UFO, WATER, POND = 1 << 46, 1 << 59, 1 << 60, 1 << 61
BOAT2, NOLAND2, BEGINBOAT2, SHARK2 = 0x2, 0x4, 0x8, 0x20
TABLE = [  # (label, route changes, opt, opt2, expected); the docs/99 §11.1 table
    ('plain', {}, 0, 0, [('Sim', 7)]),
    ('car type1 0x3B83', dict(type=1, kind=0x3B83), CAR, 0, [('Sim', 3), ('Vehicle', 5), ('Sim', 7)]),
    ('car type1 0x347C', dict(type=1, kind=0x347C), CAR, 0, [('Sim', 3), ('Vehicle', 5), ('Sim', 7)]),
    ('car type1 0xB828', dict(type=1), CAR, 0, [('Sim', 7)]),
    ('car type1, no option', dict(type=1, kind=0x3B83), 0, 0, [('Sim', 7)]),
    ('car only', dict(type=1, kind=0x3B83), NOPED, 0, [('Vehicle', 2)]),
    ('begin as car', dict(type=1, kind=0x3B83), BEGINCAR, 0, [('Vehicle', 2), ('Sim', 4)]),  # nodes 0-2 erased
    ('ufo 0x1000', dict(type=1, kind=0x3B83, flags=0x1000), CAR | UFO, 0, [('Sim', 3), ('UFO', 5), ('Sim', 7)]),
    ('ufo 0x2000', dict(type=1, kind=0x3B83, flags=0x2000), CAR | UFO, 0, [('Sim', 3), ('UFO', 5), ('Sim', 7)]),
    ('ufo flag, no ufo option', dict(type=1, kind=0x3B83, flags=0x1000), CAR, 0, [('Sim', 3), ('Vehicle', 5), ('Sim', 7)]),
    ('wade 0x80 water', dict(flags=0x80), WATER, 0, [('Sim', 3), ('Wade', 5), ('Sim', 7)]),
    ('wade 0x80 boat', dict(flags=0x80), 0, BOAT2, [('Sim', 3), ('Wade', 5), ('Sim', 7)]),
    ('wade 0x80, no option', dict(flags=0x80), 0, 0, [('Sim', 7)]),
    ('ocean 0x100 water', dict(flags=0x100), WATER, 0, [('Sim', 3), ('Ocean', 5), ('Sim', 7)]),
    ('ocean 0x100 boat only', dict(flags=0x100), 0, BOAT2, [('Sim', 7)]),
    ('pond 0x200', dict(flags=0x200), POND, 0, [('Sim', 3), ('FrozenPond', 5), ('Sim', 7)]),
    ('pond 0x200, no option', dict(flags=0x200), 0, 0, [('Sim', 7)]),
    ('boat 0x800 0xA582', dict(flags=0x800, kind=0xA582), 0, BOAT2, [('Sim', 3), ('Boat', 5), ('Sim', 7)]),
    ('boat 0x800 other kind', dict(flags=0x800), 0, BOAT2, [('Sim', 7)]),
    ('boat 0x800 water option', dict(flags=0x800, kind=0xA582), WATER, 0, [('Sim', 7)]),
    ('begin as boat', dict(flags=0x800, kind=0xA582), 0, BEGINBOAT2, [('Boat', 2), ('Sim', 4)]),  # nodes 0-2 erased
    ('horse type 11', dict(type=11), MOUNTED, 0, [('Sim', 3), ('HorseLane', 5), ('Sim', 7)]),
    ('horse type 11, no option', dict(type=11), 0, 0, [('Sim', 7)]),
    ('portal type 2', dict(type=2), 0, 0, [('Portal', 3), ('Portal', 4), ('PortalTraversal', 7)]),
    ('portal type 2 0xB0C2', dict(type=2, kind=0xB0C2), 0, 0, [('Portal', 3), ('Portal', 4), ('PortalTraversal', 7)]),
    ('type 2 car kind', dict(type=2, kind=0x3B83), 0, 0, [('Sim', 7)]),
    ('stroller', {}, STROLLER, 0, [('Sim', 1), ('Stroller', 7)]),
    ('stroller 0x40', dict(flags=0x40), STROLLER, 0, [('Sim', 1), ('Stroller', 3), ('Sim', 5), ('Stroller', 7)]),
    ('stroller type 9', dict(type=9), STROLLER, 0, [('Sim', 1), ('Stroller', 3), ('Sim', 5), ('Stroller', 7)]),
    ('stroller type 10', dict(type=10), STROLLER, 0, [('Sim', 1), ('Stroller', 7)]),
    ('0x40 without stroller', dict(flags=0x40), 0, 0, [('Sim', 7)]),
]
INERT_FLAGS = (0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x400, 0x4000, 0x8000)


def cases(rng, n):
    out = [{'label': l, 'nodes': route(**chg), 'opt': o, 'opt2': o2, 'want': w} for l, chg, o, o2, w in TABLE]
    for i in range(n):  # a table route plus bits the split never reads: +0x0E inert bits on any node, any +0x38
        l, chg, o, o2, w = rng.choice(TABLE)
        nodes = route(**chg)
        for nd in nodes:
            nd['flags'] |= sum(b for b in INERT_FLAGS if rng.random() < 0.3)
            nd['cls'] = rng.choice([0, 0x0D, 0x10, 0x13, 0x14, 0x15, 9, 13, 16, 20])
        out.append({'label': f'rand{i} {l}', 'nodes': nodes, 'opt': o, 'opt2': o2, 'want': w})
    return out


def reference(case):
    return case['want']
