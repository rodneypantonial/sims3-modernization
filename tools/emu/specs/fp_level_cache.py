"""Footprint_EnsureLevelPolygons 0x80E5B0 with its clear helper 0x61EE20 followed (docs/94 §15, I-94-120..).

Fake footprint: vtable +0xF8 (build) and +0xFC (bounds) are stubs that record the level and the entry's valid byte
at call time; 0x4E3F70 (free) is a cdecl stub that records its pointer. Entry = [fp+0x38] + level*0x24.
"""
import struct
from unicorn import x86_const as ux

NAME = 'Footprint_EnsureLevelPolygons'
TARGET = 0x80E5B0
FOLLOW = [0x61EE20]
CONV = 'thiscall'
CASES = 300
BUILD, BOUNDS, FREE = 0x401000, 0x401010, 0x4E3F70


def setup(emu):
    def rec(tag):
        def f(e, a):
            ecx = e.uc.reg_read(ux.UC_X86_REG_ECX)
            lvl = a(0)
            ent = e.u32(ecx + 0x38) + lvl * 0x24
            e.tg.append((tag, lvl, e.read(ent + 0x20, 1)[0], e.u32(ent + 4) - e.u32(ent)))
            return 0
        return f
    emu.tg = []
    emu.stub(BUILD, rec('build'), pop=4, name='vt+0xF8 build (stub)')
    emu.stub(BOUNDS, rec('bounds'), pop=4, name='vt+0xFC bounds (stub)')
    emu.stub(FREE, lambda e, a: e.tg.append(('free', a(0))) or 0, pop=0, name='free 0x4E3F70 (stub)')


def cases(rng, n):
    out = []
    for i in range(n):
        out.append({'label': f'c{i}', 'level': rng.randint(0, 2), 'valid': rng.choice([0, 1, 0]),
                    'polys': [rng.choice([0, 0x5000000 + 16 * rng.randint(1, 999)]) for _ in range(rng.randint(0, 4))]})
    return out


def run(emu, c):
    emu.tg = []
    vt = emu.alloc(0x110, 16)
    emu.w32(vt + 0xF8, BUILD)
    emu.w32(vt + 0xFC, BOUNDS)
    arr = emu.alloc(3 * 0x24, 16)
    emu.write(arr, b'\0' * (3 * 0x24))
    fp = emu.alloc(0x48, 16)
    emu.write(fp, b'\0' * 0x48)
    emu.w32(fp, vt)
    emu.w32(fp + 0x38, arr)
    emu.w32(fp + 0x3C, 3)
    ent = arr + c['level'] * 0x24
    vec = emu.alloc(16 * max(1, len(c['polys'])), 16)
    for k, p in enumerate(c['polys']):
        emu.write(vec + 16 * k, struct.pack('<IIII', p, p + 8 * 4 if p else 0, p + 8 * 4 if p else 0, 0x1020498))
    emu.write(ent, struct.pack('<III', vec, vec + 16 * len(c['polys']), vec + 16 * len(c['polys'])))
    for o in (0x10, 0x14, 0x18, 0x1C):
        emu.wf32(ent + o, 7.5)
    emu.write(ent + 0x20, bytes([c['valid']]))
    emu.call(TARGET, [c['level']], CONV, this=fp, follow=FOLLOW)
    elems_kept = [emu.u32(vec + 16 * k) for k in range(len(c['polys']))]
    return (emu.read(ent + 0x20, 1)[0], emu.u32(ent + 4) - emu.u32(ent), [emu.rf32(ent + o) for o in (0x10, 0x14, 0x18, 0x1C)],
            elems_kept, list(emu.tg))


def reference(c):
    n = len(c['polys'])
    if c['valid']:
        return 1, 16 * n, [7.5] * 4, list(c['polys']), []
    frees = [('free', p) for p in c['polys'] if p]
    # valid byte is set to 1 before the build call; the vector is empty (end = begin) when build runs; element +0 kept
    return 1, 0, [0.0] * 4, list(c['polys']), frees + [('build', c['level'], 1, 0), ('bounds', c['level'], 1, 0)]
