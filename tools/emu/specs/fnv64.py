"""HashStringFNV64_W 0x7CFE40: FNV-1 64 over a NUL-terminated UTF-16 string, lowercased per code unit.

Runs the wrapper (wcslen) plus ResourceKey_HashNameFNV64 0x4AEB90 and the CRT 64-bit multiply
0xF7EB40 (__allmul shape). Only msvcr80 towlower is stubbed, with the "C" locale rule (A-Z only).
Reference: Python FNV-1 model for every case, plus tools/dbpf.ts fnv64 for the ASCII cases.
"""
from emu import ts_ref

NAME = 'HashStringFNV64_W'
TARGET = 0x7CFE40
CONV = 'cdecl'
FOLLOW = [0x4AEB90, 0xF7EB40]   # ResourceKey_HashNameFNV64, CRT 64-bit multiply
CASES = 300
NOTES = ['towlower stubbed with the "C" locale rule (A-Z only) [inferred: the game never calls setlocale '
         'for LC_CTYPE; not checked]; model and stub share it, so the case-folding of non-ASCII is not tested']


def towlower(c):
    return c + 32 if 0x41 <= c <= 0x5A else c


def setup(emu):
    emu.stub('msvcr80.dll!towlower', lambda e, a: towlower(a(0) & 0xFFFF))


def cases(rng, n):
    edge = ['', 'a', 'A', 'Z', '@[`{', 'Sims3', 'ResourceKey', 'x' * 1000, 'Äåö', '￿\u0001',
            '😀', 'MiXeD_cAsE-123']
    out = [{'label': f'edge{i}', 's': s} for i, s in enumerate(edge)]
    for i in range(n):
        k = rng.choice([1, 2, 8, 33, 200])
        if rng.random() < 0.6:
            s = ''.join(chr(rng.randint(0x20, 0x7E)) for _ in range(rng.randint(0, k)))
        else:
            s = ''.join(chr(rng.randint(1, 0xFFFF)) for _ in range(rng.randint(0, k)))
        out.append({'label': f'rand{i}', 's': s})
    return out


def units(s):
    b = s.encode('utf-16-le', 'surrogatepass')
    return [b[i] | b[i + 1] << 8 for i in range(0, len(b), 2)]


def run(emu, case):
    p = emu.alloc(2 * len(units(case['s'])) + 2, 4, case['s'].encode('utf-16-le', 'surrogatepass') + b'\0\0')
    r = emu.call(TARGET, [p], CONV, follow=FOLLOW)
    return r.edx << 32 | r.eax


def fnv1_64(us):
    h = 0xCBF29CE484222325
    for u in us:
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        h ^= towlower(u)
    return h


def reference_batch(cases):
    want = [fnv1_64(units(c['s'])) for c in cases]
    ascii_ix = [i for i, c in enumerate(cases) if all(0x20 <= ord(ch) <= 0x7E for ch in c['s'])]
    ts = ts_ref([{'fn': 'fnv64', 'text': cases[i]['s']} for i in ascii_ix])
    if ts is None:
        NOTES.append('node not found: tools/dbpf.ts fnv64 cross-check skipped')
        return want
    for i, r in zip(ascii_ix, ts):
        if int(r['hex'], 16) != want[i]:
            want[i] = ('Python model and tools/dbpf.ts disagree', want[i], r['hex'])
    NOTES.append(f'tools/dbpf.ts fnv64 cross-checked on {len(ascii_ix)} ASCII cases (a disagreement fails the case)')
    return want
