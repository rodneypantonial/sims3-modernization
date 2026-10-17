"""LotTable_Contains 0x628A90: is a lot key in the planner's lot table (gate 12, docs/84 §4.2), and the read-only find model
that ts3native's design L1 (lotfd.cpp LtFind) reimplements.

thiscall(P)(keyLo, keyHi), ret 8, result in AL. Rejects key 0, a key whose two words AND to all-ones, and the active lot
(0xC88FD0 = [[0x11ECBC4]+0xB0], compared at +0x48/+0x4C); then LotTable_Find 0x627E00 on P+0x48 (bucket keyLo % [P+0x50]
of the array [P+0x4C], chain through node+0x190, key at node+0; no match returns the end slot's value [P+0x4C][count]),
compares with that end value and then the key again at node+0x10/+0x14. The spec also checks that the hit and miss paths
write nothing: the scratch heap (table, nodes, entries) is byte-identical after the call and no .data address is written.
A NULL world manager or active lot is not tested: the original then reads [0 + 0x48] (a crash), and L1 falls back there.
"""
import struct

NAME = 'LotTable_Contains'
TARGET = 0x628A90
CONV = 'thiscall'
FOLLOW = [0x627E00, 0xC88FD0]
CASES = 300
WM_SLOT = 0x11ECBC4
SENTINEL_END = 0x05E117E1
NOTES = ['model = lotfd.cpp LtFind (Contains rules + find + entry key); heap snapshot before/after = no writes']


def build_table(emu, case):
    """P with the table at +0x48, nodes of 0x1A0 bytes inserted at the bucket heads in list order; returns (P, nodes)."""
    cnt = case['cnt']
    p = emu.alloc(0x60, 16)
    bk = emu.alloc(4 * (cnt + 1), 16)
    emu.w32(bk + 4 * cnt, SENTINEL_END)
    emu.w32(p + 0x4C, bk)
    emu.w32(p + 0x50, cnt)
    nodes = []
    for lot in case['lots']:
        n = emu.alloc(0x1A0, 16)
        emu.write(n, struct.pack('<II', lot['lo'], lot['hi']))
        emu.write(n + 0x10, struct.pack('<II', lot['elo'], lot['ehi']))
        b = bk + 4 * (lot['lo'] % cnt)
        emu.w32(n + 0x190, emu.u32(b))
        emu.w32(b, n)
        nodes.append(n)
    wm = emu.alloc(0x100, 16)
    act = emu.alloc(0x60, 16)
    emu.w32(wm + 0xB0, act)
    emu.write(act + 0x48, struct.pack('<II', *case['act']))
    emu.w32(WM_SLOT, wm)
    return p, nodes


def model_find(case):
    """Index into case['lots'] of the lot whose entry the find returns, or None (not in the table)."""
    lo, hi = case['key']
    if not (lo | hi) or (lo & hi) == 0xFFFFFFFF or (lo, hi) == tuple(case['act']):
        return None
    chain = [i for i, l in enumerate(case['lots']) if l['lo'] % case['cnt'] == lo % case['cnt']][::-1]  # head first
    for i in chain:
        l = case['lots'][i]
        if (l['lo'], l['hi']) == (lo, hi):
            return i if (l['elo'], l['ehi']) == (lo, hi) else None
    return None


def random_lots(rng, n, cnt):
    lots, seen = [], set()
    while len(lots) < n:
        lo, hi = rng.randrange(1, cnt * 8 + 64), rng.choice([0, 0, 0, 1, 2])
        if (lo, hi) in seen:
            continue
        seen.add((lo, hi))
        bad = rng.random() < 0.03  # a node whose entry holds another key
        lots.append({'lo': lo, 'hi': hi, 'elo': lo + (1 if bad else 0), 'ehi': hi})
    return lots


def random_key(rng, case):
    r = rng.random()
    if r < 0.05:
        return (0, 0)
    if r < 0.08:
        return (0xFFFFFFFF, 0xFFFFFFFF)
    if r < 0.12:
        return tuple(case['act'])
    if r < 0.35:
        return (rng.randrange(1, 4096), rng.choice([0, 1]))
    l = rng.choice(case['lots'])
    return (l['lo'], l['hi'])


def edge_cases():
    lots = [{'lo': 5, 'hi': 0, 'elo': 5, 'ehi': 0}, {'lo': 9, 'hi': 0, 'elo': 9, 'ehi': 0},
            {'lo': 13, 'hi': 1, 'elo': 13, 'ehi': 1}, {'lo': 6, 'hi': 0, 'elo': 7, 'ehi': 0}]
    base = {'cnt': 4, 'lots': lots, 'act': (0x99, 0)}  # bucket 1: 5, 9, 13 (13 at the head); bucket 2: 6; buckets 0, 3 empty
    keys = {'key0': (0, 0), 'ones': (0xFFFFFFFF, 0xFFFFFFFF), 'lo-ones-only': (0xFFFFFFFF, 0), 'empty-bucket': (4, 0),
            'chain-miss': (17, 0), 'hi-miss': (5, 1), 'hit-tail': (5, 0), 'hit-middle': (9, 0), 'hit-head': (13, 1),
            'entry-key-differs': (6, 0)}
    out = [dict(base, label=k, key=v) for k, v in keys.items()]
    out.append(dict(base, label='active-lot-in-table', key=(9, 0), act=(9, 0)))
    return out


def cases(rng, n):
    out = edge_cases()
    for i in range(n):
        cnt = rng.randrange(1, 17)
        c = {'label': f'rand{i}', 'cnt': cnt, 'lots': random_lots(rng, rng.randrange(1, 65), cnt), 'act': (0x77777, 0)}
        if rng.random() < 0.2:
            c['act'] = tuple((lambda l: (l['lo'], l['hi']))(rng.choice(c['lots'])))
        c['key'] = random_key(rng, c)
        out.append(c)
    return out


def heap_snapshot(emu):
    return emu.read(0x20000000, emu._heap - 0x20000000)


def run(emu, case):
    p, _ = build_table(emu, case)
    snap = heap_snapshot(emu)
    r = emu.call(TARGET, list(case['key']), CONV, this=p, follow=FOLLOW)
    return r.eax & 0xFF, heap_snapshot(emu) == snap, sorted(r.global_writes)


def reference(case):
    return (1 if model_find(case) is not None else 0), True, []

