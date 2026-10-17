"""ObjectId_Lookup 0x939100: the object id hash lookup that ts3native [IdCache] answers from its cache (docs/45 §4, PLAN #22).

thiscall(ecx = bucket array, out iterator, pointer to the 64-bit id), ret 8, eax = out. The model is the rule [IdCache]
reimplements: bucket = id_lo % 0x409, walk node+0x10 comparing node+0x08/+0x0C; hit -> out = {node, &bucket};
miss -> out = {[ecx+0x1024], ecx+0x1024}. A case builds one bucket array with chains of 0-20 nodes (some sharing id_lo
or id_hi with the key) and looks up a key that is present at a random chain position or absent.
"""
import struct

NAME = 'ObjectId_Lookup'
TARGET = 0x939100
CONV = 'thiscall'
CASES = 300
NOTES = ['key low halves stay below 2^31 so chain ids share the bucket without wrapping; the high bit of id_lo is not exercised']

SENTINEL_VALUE = 0x5E117E1


def cases(rng, n):
    out = []
    edge = [('empty-miss', 0, None), ('one-hit', 1, 0), ('one-miss', 1, None), ('long-last', 20, 19),
            ('long-first', 20, 0), ('long-miss', 20, None), ('same-lo-miss', 8, 'lo'), ('same-hi-miss', 8, 'hi')]
    for label, length, where in edge:
        out.append(make(rng, label, length, where))
    for i in range(n):
        length = rng.randint(0, 16)
        where = rng.choice([None, 'lo', 'hi'] + list(range(length)))
        out.append(make(rng, f'rand{i}', length, where))
    return out


def make(rng, label, length, where):
    key = (rng.getrandbits(31), rng.getrandbits(32))  # lo < 2^31: lo + k * 0x409 stays in the same bucket (no wrap)
    chain = []
    for i in range(length):
        lo = key[0] + 0x409 * rng.randint(1, 1000) & 0xFFFFFFFF  # same bucket, different id
        chain.append((lo, rng.getrandbits(32)))
    if isinstance(where, int):
        chain[where] = key
    elif where == 'lo' and chain:
        chain[0] = (key[0], key[1] ^ 1)   # same low half, other high half: must not match
    elif where == 'hi' and chain:
        chain[0] = ((key[0] + 0x409) & 0xFFFFFFFF, key[1])
    return {'label': label, 'key': key, 'chain': chain, 'other': rng.getrandbits(32)}


def run(emu, case):
    tbl = emu.alloc(0x1040, 16)
    emu.write(tbl, b'\0' * 0x1040)
    emu.w32(tbl + 0x1024, SENTINEL_VALUE)
    lo, hi = case['key']
    b = lo % 0x409
    nodes = [emu.alloc(0x20, 16) for _ in case['chain']]
    for i, (n, (nlo, nhi)) in enumerate(zip(nodes, case['chain'])):
        emu.w32(n + 8, nlo)
        emu.w32(n + 0xC, nhi)
        emu.w32(n + 0x10, nodes[i + 1] if i + 1 < len(nodes) else 0)
    emu.w32(tbl + b * 4, nodes[0] if nodes else 0)
    # a node in another bucket with the same id halves swapped: never reached
    other = emu.alloc(0x20, 16)
    emu.w32(other + 8, hi)
    emu.w32(other + 0xC, lo)
    emu.w32(tbl + ((b + 1) % 0x409) * 4, other)
    out = emu.alloc(8, 8)
    idp = emu.alloc(8, 8)
    emu.write(idp, struct.pack('<II', lo, hi))
    r = emu.call(TARGET, [out, idp], CONV, this=tbl)
    got = (r.eax == out, emu.u32(out), emu.u32(out + 4))
    # translate addresses to model terms
    idx = nodes.index(got[1]) if got[1] in nodes else ('sentinel' if got[1] == SENTINEL_VALUE else hex(got[1]))
    slot = 'bucket' if got[2] == tbl + b * 4 else ('end' if got[2] == tbl + 0x1024 else hex(got[2]))
    return got[0], idx, slot


def reference(case):
    for i, (nlo, nhi) in enumerate(case['chain']):
        if (nlo, nhi) == case['key']:
            return True, i, 'bucket'
    return True, 'sentinel', 'end'
