"""RefPack_DecompressCore 0x4EB3B0: cdecl(dst, dstCap, src, srcLen) -> size from the header, or 0 on error.

Round trip: a small Python RefPack encoder (below, all four copy forms, the literal run, the stop
op, 3- and 4-byte size headers, with and without the compressed-size field) builds a stream from
generated data; the exe decodes it; the output must equal the original data AND what the repo's
own decoder (refpack() in tools/extract-assemblies.ts, via ts-ref.ts) returns. Guard bytes after
dstCap must stay untouched. Error cases (NULL src, short src, truncated stream, small dstCap,
copy before the buffer start, no stop op) must return 0. An empty result also returns 0, so a
caller cannot tell an empty resource from an error by the return value [verified below: rand cases of size 0].
"""
from emu import ts_ref

NAME = 'RefPack_DecompressCore'
TARGET = 0x4EB3B0
CONV = 'cdecl'
CASES = 200
GUARD = b'\xA5' * 64
NOTES = []


def encode(data, rng, big_header=False, csize_field=False):
    """Greedy RefPack encoder; rng picks among the valid opcode forms so every form gets used."""
    n = len(data)
    flags = (0x80 if big_header else 0) | (0x01 if csize_field else 0) | 0x10
    sb = 4 if big_header else 3
    head = bytes([flags, 0xFB]) + (b'\0' * sb if csize_field else b'') + n.to_bytes(sb, 'big')
    out = bytearray()
    lit = bytearray()
    last = {}
    i = 0

    def flush_runs():
        while len(lit) >= 4:
            k = min(112, len(lit) & ~3)
            out.append(0xE0 + (k - 4) // 4)
            out.extend(lit[:k])
            del lit[:k]

    while i < n:
        best_len, best_off = 0, 0
        key = data[i:i + 3]
        for j in last.get(key, [])[-8:]:
            off = i - j
            if off > 131072:
                continue
            m = 0
            while i + m < n and m < 1028 and data[j + m] == data[i + m]:
                m += 1
            if m > best_len:
                best_len, best_off = m, off
        if best_len >= 3:
            forms = []
            if best_len <= 10 and best_off <= 1024:
                forms.append(2)
            if 4 <= best_len <= 67 and best_off <= 16384:
                forms.append(3)
            if best_len >= 5 and best_off <= 131072:
                forms.append(4)
            if not forms:  # 3-byte match too far for the short form: shrink is not possible, emit literal
                best_len = 0
        if best_len >= 3:
            form = rng.choice(forms)
            ln = min(best_len, {2: 10, 3: 67, 4: 1028}[form])
            flush_runs()
            p = len(lit)  # 0..3 literals ride along with the copy
            o = best_off - 1
            if form == 2:
                out += bytes([((o >> 3) & 0x60) | ((ln - 3) << 2) | p, o & 0xFF])
            elif form == 3:
                out += bytes([0x80 | (ln - 4), (p << 6) | (o >> 8), o & 0xFF])
            else:
                out += bytes([0xC0 | ((o >> 12) & 0x10) | (((ln - 5) >> 6) & 0x0C) | p, (o >> 8) & 0xFF, o & 0xFF,
                              (ln - 5) & 0xFF])
            out += lit
            lit.clear()
            for k in range(i, i + ln):
                last.setdefault(data[k:k + 3], []).append(k)
            i += ln
        else:
            last.setdefault(key, []).append(i)
            lit.append(data[i])
            i += 1
            if len(lit) >= 112:
                flush_runs()
    flush_runs()
    out.append(0xFC + len(lit))
    out += lit
    return head + bytes(out)


def gen_data(rng, n):
    kind = rng.choice(['random', 'runs', 'text', 'mixed', 'far'])
    if kind == 'random':
        return bytes(rng.getrandbits(8) for _ in range(n))
    if kind == 'runs':
        return b''.join(bytes([rng.getrandbits(8)]) * rng.randint(1, 300) for _ in range(n // 50 + 1))[:n]
    if kind == 'text':
        words = [bytes(rng.getrandbits(8) % 26 + 97 for _ in range(rng.randint(2, 9))) for _ in range(30)]
        return b' '.join(rng.choice(words) for _ in range(n // 5 + 1))[:n]
    if kind == 'far':  # repeats at distances 16K..128K so the 4-byte form with large offsets is used
        block = bytes(rng.getrandbits(8) for _ in range(rng.randint(17000, 40000)))
        return (block + block[:5000] + bytes(rng.getrandbits(8) for _ in range(3)) + block[100:3000])
    parts = []
    while sum(map(len, parts)) < n:
        parts.append(bytes(rng.getrandbits(8) for _ in range(rng.randint(0, 40))))
        if parts and rng.random() < 0.7:
            src = b''.join(parts)
            a = rng.randrange(len(src)) if src else 0
            parts.append(src[a:a + rng.randint(3, 1100)])
    return b''.join(parts)[:n]


def cases(rng, n):
    out = []
    for i in range(n):
        data = gen_data(rng, rng.choice([0, 1, 3, 7, 50, 500, 3000, 9000]))
        s = encode(data, rng, rng.random() < 0.3, rng.random() < 0.3)
        out.append({'label': f'rand{i}', 'data': data, 'src': s, 'cap': len(data)})
    d = bytes(range(256)) * 8
    good = encode(d, rng)
    errs = [('null_src', None, 2048, 0), ('len0', good[:0], 2048, 0), ('len1', good[:1], 2048, 0),
            ('hdr_short', good[:4], 2048, 0), ('trunc_mid', good[:len(good) // 2], 2048, 0),
            ('no_stop', good[:-1], 2048, 0), ('cap_small', good, len(d) - 1, 0),
            ('copy_before_start', bytes([0x10, 0xFB, 0, 0, 8, 0x00, 0x05, 0xFC]), 8, 0),
            ('big_hdr_short', bytes([0x90, 0xFB, 0, 0]), 8, 0), ('csize_short', bytes([0x11, 0xFB, 0, 0]), 8, 0),
            ('big_csize_short', bytes([0x91, 0xFB, 0, 0, 0, 0]), 8, 0)]
    for label, src, cap, _ in errs:
        out.append({'label': label, 'src': src, 'cap': cap, 'expect_error': True})
    # header claims more than the stream produces: the exe returns the header size anyway
    short = bytes([0x10, 0xFB, 0, 0, 9, 0xFD, 0x41])
    out.append({'label': 'header_size_larger', 'src': short, 'cap': 16, 'data': b'A', 'claimed': 9})
    return out


def run(emu, case):
    cap = case['cap']
    dst = emu.alloc(cap + len(GUARD), 16, b'\0' * cap + GUARD)
    src = 0 if case['src'] is None else emu.alloc(len(case['src']), 4, case['src'])
    srclen = 0 if case['src'] is None else len(case['src'])
    r = emu.call(TARGET, [dst, cap, src, srclen], CONV, budget=50_000_000, timeout_ms=60_000)
    body = emu.read(dst, cap + len(GUARD))
    if body[cap:] != GUARD:
        return ('guard bytes overwritten', r.eax)
    return (r.eax, body[:len(case.get('data', b''))])


def reference_batch(cases):
    ok = [i for i, c in enumerate(cases) if not c.get('expect_error') and 'claimed' not in c]
    ts = ts_ref([{'fn': 'refpack', 'hex': cases[i]['src'].hex()} for i in ok])
    want = [(0, b'') if c.get('expect_error') else (c.get('claimed', len(c['data'])), c['data']) for c in cases]
    if ts is None:
        NOTES.append('node not found: the tools/extract-assemblies.ts decoder was not run')
        return want
    for i, r in zip(ok, ts):
        if r.get('hex') != cases[i]['data'].hex():
            want[i] = ('encoder and tools/extract-assemblies.ts refpack() disagree', r.get('error'))
    NOTES.append(f'tools/extract-assemblies.ts refpack() decoded all {len(ok)} round-trip streams too '
                 '(a disagreement fails the case)')
    return want
