"""PhysShape_Release 0x682760: the reference drop that ts3native [TerrainGrave] (H6 S3a, docs/60 §2.2 #2) parks when the
terrain cell rebuild 0xC2A840 calls it from 0xC2A896.

thiscall(ecx = shape), no stack args, plain ret. The model is the contract the graveyard relies on: the count at
shape+0x4C drops by one (plain add, no lock), the return value is the new count, and only when it reaches 0 the
deleting destructor (vtable slot 0) runs with ecx = shape and the argument 1. So skipping the call leaves the shape
alive with its count untouched, and calling the original later does exactly the deferred drop. That the swap ignores the
return value (eax is rewritten after 0xC2A896 before any read) is a disassembly fact, not tested here.
"""
from unicorn import x86_const as ux

NAME = 'PhysShape_Release'
TARGET = 0x682760
CONV = 'thiscall'
CASES = 200
DTOR = 0x401000  # any address outside the target: the stub stands for the shape's deleting destructor
NOTES = ['the destructor is a stub (records ecx and its argument, pops 4 like a thiscall with one argument)']


def setup(emu):
    def dtor(e, arg):
        e.tg_calls.append((e.uc.reg_read(ux.UC_X86_REG_ECX), arg(0)))
        return 0
    emu.tg_calls = []
    emu.stub(DTOR, dtor, pop=4, name='deleting destructor (stub)')


def cases(rng, n):
    out = [{'label': f'count{c}', 'count': c} for c in (1, 2, 3, 0x7FFFFFFF, 0)]
    for i in range(n):
        out.append({'label': f'rand{i}', 'count': rng.choice([1, 2, rng.randint(1, 1000)])})
    return out


def run(emu, case):
    emu.tg_calls = []
    vtbl = emu.alloc(16, 16)
    emu.w32(vtbl, DTOR)
    shape = emu.alloc(0x50, 16)
    emu.write(shape, b'\0' * 0x50)
    emu.w32(shape, vtbl)
    emu.w32(shape + 0x4C, case['count'])
    r = emu.call(TARGET, [], CONV, this=shape)
    eax = r.eax
    calls = [(c[0] == shape, c[1]) for c in emu.tg_calls]
    return eax & 0xFFFFFFFF, emu.u32(shape + 0x4C), calls


def reference(case):
    n = (case['count'] - 1) & 0xFFFFFFFF
    return n, n, [(True, 1)] if n == 0 else []
