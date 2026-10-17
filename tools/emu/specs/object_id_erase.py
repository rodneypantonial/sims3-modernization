"""ObjectIdHash_EraseNode 0x938D00: unlinks one node from the object id hash; ts3native [IdCache] hooks it to drop the
node's cache entry first (docs/45 §4, PLAN #22).

thiscall(ecx = bucket array, out iterator, node, bucket slot), ret 0xC. The model is the contract [IdCache] relies on:
arg2 is the node (its id at +0x08/+0x0C is intact on entry), arg3 the slot of its bucket; afterwards the node is out of
the chain, every other node keeps its order, and the count at ecx+0x1028 drops by one. The out iterator (the next node)
is not used by the hook and not compared.
"""
NAME = 'ObjectIdHash_EraseNode'
TARGET = 0x938D00
CONV = 'thiscall'
CASES = 200
NOTES = ['the returned iterator (next node, used by DestroyAll-style walks) is not checked']


def cases(rng, n):
    out = [{'label': 'only', 'len': 1, 'pos': 0}, {'label': 'head', 'len': 5, 'pos': 0},
           {'label': 'tail', 'len': 5, 'pos': 4}, {'label': 'middle', 'len': 5, 'pos': 2}]
    for i in range(n):
        length = rng.randint(1, 16)
        out.append({'label': f'rand{i}', 'len': length, 'pos': rng.randrange(length)})
    return out


def run(emu, case):
    tbl = emu.alloc(0x1040, 16)
    emu.write(tbl, b'\0' * 0x1040)
    emu.w32(tbl + 0x1024, 0x5E117E1)
    b = 77
    nodes = [emu.alloc(0x20, 16) for _ in range(case['len'])]
    for i, n in enumerate(nodes):
        emu.w32(n + 8, 77 + 0x409 * (i + 1))
        emu.w32(n + 0xC, i)
        emu.w32(n + 0x10, nodes[i + 1] if i + 1 < len(nodes) else 0)
    emu.w32(tbl + b * 4, nodes[0])
    # later buckets non-empty so a tail erase can step to the next bucket for its iterator
    emu.w32(tbl + (b + 1) * 4, emu.alloc(0x20, 16))
    emu.w32(tbl + 0x1028, 100)
    out = emu.alloc(8, 8)
    victim = nodes[case['pos']]
    emu.call(TARGET, [out, victim, tbl + b * 4], CONV, this=tbl)
    chain, n = [], emu.u32(tbl + b * 4)
    while n and len(chain) < 40:
        chain.append(nodes.index(n) if n in nodes else hex(n))
        n = emu.u32(n + 0x10)
    return chain, emu.u32(tbl + 0x1028)


def reference(case):
    return [i for i in range(case['len']) if i != case['pos']], 99
