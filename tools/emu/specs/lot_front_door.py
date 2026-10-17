"""LotRouting_FrontDoorPortal 0x631800 vs the read-only clone of ts3native design L1 (lotfd.cpp LtFrontDoor, docs/84 §4.6).

thiscall(P)(lotLo, lotHi, type, Portal** out), ret 0x10, result EDX:EAX. LotTable_Contains 0x628A90 (no: 0), then
LotTable_GetOrInsert 0x62EAB0 (a hit after a true Contains: entry = node+0x10), n = (entry+0x48 - entry+0x44) >> 2 (sar),
the door index by type (0: +0x1C when not -1 and below n, else +0x18; 1: +0x1C; 2: +0x18; other: 0), compared unsigned
with n; the portal pointer at that index (NULL: 0); AddRef (vtable slot 0), *out = portal when out is set, the id
+8/+0xC read, Release (slot +4), return the id. The clone skips AddRef/Release: the spec checks that the original calls
each exactly once per door found, on the portal, so their net change is 0 for a count >= 1 (the clone refuses a count < 1).
Coverage 1.0 for 0x631800; 0x628A90 runs all its branches; 0x62EAB0 only its hit blocks (a miss cannot follow a true
Contains: the insert path is not reached and not followed).
"""
import importlib.util
import os
import struct

from unicorn import x86_const as ux

_spec = importlib.util.spec_from_file_location('lot_table_contains', os.path.join(os.path.dirname(__file__), 'lot_table_contains.py'))
ltc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ltc)

NAME = 'LotRouting_FrontDoorPortal'
TARGET = 0x631800
CONV = 'thiscall'
FOLLOW = [0x628A90, 0x627E00, 0x62EAB0, 0xC88FD0]
CASES = 400
ADDREF, RELEASE = 0x401000, 0x401010  # stand for 0x64E8E0 / 0x64E8F0 (plain add / sub on +0x70, read in the disassembly)
OUT_MARK = 0x0BAD0BAD
NOTES = ['AddRef/Release are stubs that count calls and change +0x70 like the originals (counts stay >= 1, no delete)',
         'model = lotfd.cpp LtFrontDoor: Contains rules, find, door index by type, portal id; no refcount']


def setup(emu):
    def ref(d):
        def fn(e, _arg):
            ecx = e.uc.reg_read(ux.UC_X86_REG_ECX)
            e.fd_calls.append((d, ecx))
            e.w32(ecx + 0x70, e.u32(ecx + 0x70) + d)
            return e.u32(ecx + 0x70)
        return fn
    emu.fd_calls = []
    emu.stub(ADDREF, ref(1), name='portal AddRef 0x64E8E0 (stub)')
    emu.stub(RELEASE, ref(-1), name='portal Release 0x64E8F0 (stub)')


def door(case):
    """(id, portal index in the entry) the original returns, or (0, None)."""
    i = ltc.model_find(case)
    if i is None:
        return 0, None
    lot = case['lots'][i]
    ports = lot.get('portals', [])
    n = (-2 if lot.get('neg') else len(ports)) & 0xFFFFFFFF  # neg: end = begin - 8, n = -2 (huge unsigned)
    t = case['type']
    if t == 0:
        k = lot['i1C'] if lot['i1C'] != 0xFFFFFFFF and lot['i1C'] < n else lot['i18']
    elif t in (1, 2):
        k = lot['i1C'] if t == 1 else lot['i18']
    else:
        return 0, None
    if k == 0xFFFFFFFF or k >= n or k >= len(ports) or ports[k] is None:
        return 0, None
    lo, hi = ports[k]
    return hi << 32 | lo, k


def run(emu, case):
    emu.fd_calls = []
    p, nodes = ltc.build_table(emu, case)
    vt = emu.alloc(8, 16)
    emu.w32(vt, ADDREF)
    emu.w32(vt + 4, RELEASE)
    portals = {}
    for li, (lot, node) in enumerate(zip(case['lots'], nodes)):
        e = node + 0x10
        emu.w32(e + 0x18, lot.get('i18', 0xFFFFFFFF))
        emu.w32(e + 0x1C, lot.get('i1C', 0xFFFFFFFF))
        ports = lot.get('portals', [])
        if not ports:
            continue
        v = emu.alloc(4 * len(ports), 16)
        for k, pid in enumerate(ports):
            if pid is None:
                continue
            pt = emu.alloc(0x80, 16)
            emu.w32(pt, vt)
            emu.write(pt + 8, struct.pack('<II', *pid))
            emu.w32(pt + 0x70, lot.get('count', 1))
            emu.w32(v + 4 * k, pt)
            portals[(li, k)] = pt
        emu.w32(e + 0x44, v)
        emu.w32(e + 0x48, v - 8 if lot.get('neg') else v + 4 * len(ports))
    out = emu.alloc(4, 4)
    emu.w32(out, OUT_MARK)
    counts = {key: emu.u32(pt + 0x70) for key, pt in portals.items()}
    lo, hi = case['key']
    r = emu.call(TARGET, [lo, hi, case['type'], out if case['out'] else 0], CONV, this=p, follow=FOLLOW)
    unchanged = all(emu.u32(pt + 0x70) == counts[key] for key, pt in portals.items())
    adds = [c for d, c in emu.fd_calls if d == 1]
    rels = [c for d, c in emu.fd_calls if d == -1]
    li = ltc.model_find(case)
    _, k = door(case)
    target = portals.get((li, k)) if k is not None else None
    on_portal = all(c == target for c in adds + rels)
    o = emu.u32(out)
    return (r.eax, r.edx, 'portal' if target and o == target else o, len(adds), len(rels), unchanged, on_portal,
            sorted(r.global_writes))


def reference(case):
    pid, k = door(case)
    found = k is not None
    o = 'portal' if found and case['out'] else OUT_MARK
    return pid & 0xFFFFFFFF, pid >> 32, o, int(found), int(found), True, True, []


def lot_with(rng, lo, hi, n, i18, i1C, nulls=(), neg=False):
    ports = [None if k in nulls else (rng.randrange(1, 1 << 32), rng.randrange(0, 1 << 16)) for k in range(n)]
    return {'lo': lo, 'hi': hi, 'elo': lo, 'ehi': hi, 'portals': ports, 'i18': i18, 'i1C': i1C, 'neg': neg,
            'count': rng.choice([1, 2, 5])}


def cases(rng, n):
    M1 = 0xFFFFFFFF
    out = []
    for c in ltc.edge_cases():  # the Contains edges, each with a door at index 0
        c = dict(c, type=0, out=True)
        c['lots'] = [dict(l, portals=[(0x1000 + j, j)], i18=0, i1C=M1) for j, l in enumerate(c['lots'])]
        out.append(c)
    base = {'cnt': 4, 'act': (0x99, 0), 'key': (5, 0)}

    def one(label, lot, **kw):
        out.append(dict(base, label=label, lots=[lot], type=kw.get('type', 0), out=kw.get('out', True)))
    one('t0-1C-valid', lot_with(rng, 5, 0, 3, 0, 2))
    one('t0-1C-minus1', lot_with(rng, 5, 0, 3, 1, M1))
    one('t0-1C-ge-n', lot_with(rng, 5, 0, 3, 1, 3))
    one('t0-both-bad', lot_with(rng, 5, 0, 3, 7, 3))
    one('t0-18-minus1', lot_with(rng, 5, 0, 3, M1, M1))
    one('t1', lot_with(rng, 5, 0, 3, 0, 1), type=1)
    one('t1-minus1', lot_with(rng, 5, 0, 3, 0, M1), type=1)
    one('t2', lot_with(rng, 5, 0, 3, 2, 1), type=2)
    one('t2-ge-n', lot_with(rng, 5, 0, 3, 3, 1), type=2)
    one('t3', lot_with(rng, 5, 0, 3, 0, 1), type=3)
    one('t-big', lot_with(rng, 5, 0, 3, 0, 1), type=0x80000000)
    one('null-portal', lot_with(rng, 5, 0, 3, 1, M1, nulls=(1,)))
    one('no-portals', lot_with(rng, 5, 0, 0, 0, 0))
    one('out-null', lot_with(rng, 5, 0, 3, 0, 2), out=False)
    one('neg-n', lot_with(rng, 5, 0, 3, 1, M1, neg=True))  # end before begin: n = -2, unsigned huge
    for i in range(n):
        cnt = rng.randrange(1, 17)
        lots = ltc.random_lots(rng, rng.randrange(1, 65), cnt)
        for l in lots:
            k = rng.randrange(0, 5)
            nulls = {j for j in range(k) if rng.random() < 0.15}
            pick = lambda: rng.choice([M1, rng.randrange(0, k + 2)])  # noqa: E731
            l.update(lot_with(rng, l['lo'], l['hi'], k, pick(), pick(), nulls), elo=l['elo'], ehi=l['ehi'])
        c = {'label': f'rand{i}', 'cnt': cnt, 'lots': lots, 'act': (0x77777, 0), 'type': rng.choice([0, 0, 1, 2, 3]),
             'out': rng.random() < 0.5}
        c['key'] = ltc.random_key(rng, c)
        out.append(c)
    return out
