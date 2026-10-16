"""Lot_WorldToLocal2D 0xAB8540: world (x, z) into lot space (docs/38).

thiscall(lot, float x, float z, float* outX, float* outZ), ret 0x10. Reads the lot origin
(+0x190 x, +0x198 z) and angle (+0x1A0); s = fsin(angle), c = fcos(angle) rounded to float;
dx = x - ox, dz = z - oz;  outX = c*dx + (-s)*dz,  outZ = s*dx + c*dz  (single-precision SSE).
The -s comes from the constant at 0xF988F0, which is -0.0 (so -0.0 - s) [verified exe bytes].
"""
import math
import struct

from emu import f32 as _f32

NAME = 'Lot_WorldToLocal2D'
TARGET = 0xAB8540
CONV = 'thiscall'
CASES = 400
MAX_ULP = 1   # fsin/fcos: QEMU computes them through the host libm, the model through Python's
NOTES = ['x87 fsin/fcos come from the emulator (QEMU uses host sin/cos on the 80-bit value), not from an '
         'Intel FPU; real hardware can differ in the last bit of the float result [inferred]',
         'emulator quirk: fsin/fcos of +-inf leave the operand unchanged in Unicorn (case angle12 expects '
         'that); an Intel FPU returns NaN [verified here for Unicorn 2.1.4; hardware per the Intel SDM]']


def f32(x):
    if math.isnan(x):
        return x
    try:
        return _f32(x)
    except OverflowError:
        return math.copysign(math.inf, x)


def cases(rng, n):
    pi = math.pi
    angles = [0.0, -0.0, pi / 2, pi, -pi, 3 * pi / 2, 2 * pi, 1e-30, 1e5, 2.0 ** 62, 2.0 ** 64, -2.0 ** 70,
              math.inf, math.nan]
    coords = [0.0, -0.0, 1.0, -1.0, 1e-40, 3.0e38, -3.0e38, 1e30, math.inf, math.nan, 2048.0, 123.456]
    out = []
    for i, a in enumerate(angles):
        out.append({'label': f'angle{i}', 'lot': (100.0, 200.0, a), 'p': (110.0, 205.0)})
    for i, c in enumerate(coords):
        out.append({'label': f'coord{i}', 'lot': (c, -c, 0.7), 'p': (c, 1.0)})
        out.append({'label': f'coordp{i}', 'lot': (0.0, 0.0, 2.1), 'p': (c, c)})
    for i in range(n):
        lot = (rng.uniform(0, 2048), rng.uniform(0, 2048), rng.uniform(-2 * pi, 2 * pi))
        out.append({'label': f'rand{i}', 'lot': lot, 'p': (lot[0] + rng.uniform(-64, 64), lot[1] + rng.uniform(-64, 64))})
    return out


def run(emu, case):
    ox, oz, ang = case['lot']
    lot = emu.alloc(0x400, 16)
    emu.wf32(lot + 0x190, ox)
    emu.wf32(lot + 0x198, oz)
    emu.wf32(lot + 0x1A0, ang)
    outs = emu.alloc(16, 16)
    x, z = case['p']
    emu.call(TARGET, [f32(x), f32(z), outs, outs + 4], CONV, this=lot)
    return emu.rf32(outs), emu.rf32(outs + 4)


def x87(fn, a):
    if math.isnan(a):
        return math.nan
    if abs(a) >= 2.0 ** 63:
        # out of range: C2 set, ST(0) unchanged. Unicorn/QEMU does this for +-inf too; an Intel FPU
        # raises invalid there and returns the default NaN (see NOTES).
        return a
    return fn(a)


def reference(case):
    ox, oz, ang = (f32(v) for v in case['lot'])
    x, z = (f32(v) for v in case['p'])
    s, c = f32(x87(math.sin, ang)), f32(x87(math.cos, ang))
    dx, dz = f32(x - ox), f32(z - oz)
    ns = f32(-0.0 - s)
    return f32(f32(ns * dz) + f32(c * dx)), f32(f32(s * dx) + f32(c * dz))


def ulps(a, b):
    if math.isnan(a) or math.isnan(b):
        return 0 if math.isnan(a) and math.isnan(b) else 1 << 32
    ia, ib = (struct.unpack('<i', struct.pack('<f', v))[0] for v in (a, b))
    ia, ib = (v if v >= 0 else -(v & 0x7FFFFFFF) for v in (ia, ib))  # ordered integers (-0.0 == +0.0)
    return abs(ia - ib)


def compare(got, want):
    if isinstance(got, tuple) and got and got[0] == 'EmuError':
        return got[1]
    d = max(ulps(g, w) for g, w in zip(got, want))
    if d and d <= MAX_ULP:
        NOTES.append(f'{got} vs model {want}: {d} ulp (within tolerance)')
    return None if d <= MAX_ULP else f'got {got}, want {want} ({d} ulp)'
