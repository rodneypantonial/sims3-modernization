"""Struct/field displacements our native code uses, checked against EA app 1.69.47 (docs/238).

    python tools/struct-offsets.py [--csv F] [--md F] [--changes F] [--depth 2]
    python tools/struct-offsets.py --self-check

1. Offsets: every `+ 0xNN` (0x4..0x3FFF) in comment-stripped code of native/ts3native (tests, fakes, selftest and the
   generated tables excluded) plus native/worldimage/wimage.h, and `[reg+N]` inside inline asm, grouped by (file, value).
2. Function pairs: every code pair of ts3addr-ea169.h (Steam 1.67 -> 1.69), then the `call rel32` targets at aligned
   positions of each pair, to --depth levels. Per function, the instructions reachable from the entry (jcc/jmp followed,
   calls not, 3000 instructions max), in address order, in both images.
3. Alignment: difflib on instruction shapes (mnemonic, operand kinds, registers; displacements, immediates and branch
   targets left out), so a moved field still aligns. Per aligned pair with a memory operand on a base register other
   than esp/ebp: (Steam disp, EA disp). `add/sub reg, imm` immediates are compared the same way.
4. Verdict per (file, value): `same` = the file's own sites (hook-sites.ts) or their aligned callees use it and every
   aligned EA instruction has the same value, and no aligned instruction anywhere moves it; `differs` = some aligned
   instruction moves it (all moves listed for review); `same-global` = only other functions use it, all unchanged;
   `unverifiable` = no aligned Steam instruction uses it.
"""
import argparse
import difflib
import os
import re
import struct
import subprocess
import sys

from iced_x86 import Decoder, FlowControl, Mnemonic, OpKind, Register

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
MAIN = os.path.normpath(os.path.join(REPO, '..', 'sims-3-modernization'))


def find(rel):
    for r in (REPO, MAIN):
        p = os.path.join(r, rel)
        if os.path.exists(p):
            return p
    return os.path.join(REPO, rel)


class Image:
    def __init__(self, path=None, raw=None, base=None):
        if raw is not None:
            self.mem, self.base, self.text = raw, base, (base, base + len(raw))
            return
        b = open(path, 'rb').read()
        pe = struct.unpack_from('<I', b, 0x3C)[0]
        nsec, optsz = struct.unpack_from('<H', b, pe + 6)[0], struct.unpack_from('<H', b, pe + 20)[0]
        self.base, size = struct.unpack_from('<I', b, pe + 0x34)[0], struct.unpack_from('<I', b, pe + 0x50)[0]
        secs = [struct.unpack_from('<8sIIII', b, pe + 24 + optsz + 40 * i) for i in range(nsec)]
        if len(b) == size:
            self.mem = b
        else:
            m = bytearray(size)
            m[:0x1000] = b[:0x1000]
            for _, vsz, rva, rsz, roff in secs:
                n = min(rsz, vsz or rsz)
                m[rva:rva + n] = b[roff:roff + n]
            self.mem = bytes(m)
        t = [s for s in secs if s[0].rstrip(b'\0') == b'.text'][0]
        self.text = (self.base + t[2], self.base + t[2] + t[1])

    def in_text(self, a):
        return self.text[0] <= a < self.text[1]

    def ins(self, a):
        o = a - self.base
        return next(iter(Decoder(32, self.mem[o:o + 16], ip=a)))

    def func(self, a, budget=3000):
        """Instructions reachable from a (jumps followed, calls not), address order."""
        seen, todo = {}, [a]
        while todo and len(seen) < budget:
            p = todo.pop()
            while p not in seen and self.in_text(p) and len(seen) < budget:
                x = self.ins(p)
                if x.mnemonic == Mnemonic.INVALID:
                    break
                seen[p] = x
                fc = x.flow_control
                if fc in (FlowControl.CONDITIONAL_BRANCH, FlowControl.UNCONDITIONAL_BRANCH) and x.op0_kind == OpKind.NEAR_BRANCH32:
                    t = x.near_branch_target
                    if abs(t - a) < 0x8000:
                        todo.append(t)
                if fc in (FlowControl.RETURN, FlowControl.UNCONDITIONAL_BRANCH, FlowControl.INDIRECT_BRANCH,
                          FlowControl.INTERRUPT, FlowControl.EXCEPTION):
                    break
                p = x.next_ip
        return [seen[k] for k in sorted(seen)]


def shape(x):
    s = [x.mnemonic]
    for i in range(x.op_count):
        k = x.op_kind(i)
        s.append(k)
        if k == OpKind.REGISTER:
            s.append(x.op_register(i))
        elif k == OpKind.MEMORY:
            s += [x.memory_base, x.memory_index, x.memory_index_scale, x.memory_base == Register.NONE]
    return tuple(s)


def field(x):
    """(kind, value) of a struct displacement or add/sub immediate, else None."""
    for i in range(x.op_count):
        if x.op_kind(i) == OpKind.MEMORY and x.memory_base not in (Register.NONE, Register.ESP, Register.EBP):
            d = x.memory_displacement & 0xFFFFFFFF
            return ('disp', d) if d < 0x10000 else None
    if x.mnemonic in (Mnemonic.ADD, Mnemonic.SUB) and x.op_count == 2 and x.op0_kind == OpKind.REGISTER \
            and x.op0_register not in (Register.ESP, Register.EBP) and x.op1_kind in (OpKind.IMMEDIATE8TO32, OpKind.IMMEDIATE32):
        v = x.immediate32
        return ('imm', v) if v < 0x10000 else None
    return None


def align(fs, fe):
    """Aligned (steam ins, ea ins) pairs of two instruction lists."""
    sm = difflib.SequenceMatcher(None, [shape(x) for x in fs], [shape(x) for x in fe], autojunk=False)
    for b in sm.get_matching_blocks():
        for k in range(b.size):
            yield fs[b.a + k], fe[b.b + k]


def corpus(steam, ea, seeds, depth):
    """{steam fn: ea fn}, evidence {(kind, value): [(steam ip, ea ip, ea value)]}, callees {steam fn: set}."""
    pairs, ev, callees, frontier = {}, {}, {}, dict(seeds)
    for _ in range(depth + 1):
        nxt = {}
        for s, e in frontier.items():
            if s in pairs or not (steam.in_text(s) and ea.in_text(e)):
                continue
            pairs[s] = e
            cs = callees.setdefault(s, set())
            for xs, xe in align(steam.func(s), ea.func(e)):
                f = field(xs)
                if f:
                    g = field(xe)
                    ev.setdefault(f, []).append((xs.ip, xe.ip, g[1] if g and g[0] == f[0] else None))
                if xs.mnemonic == Mnemonic.CALL and xs.op0_kind == OpKind.NEAR_BRANCH32 and xe.op0_kind == OpKind.NEAR_BRANCH32:
                    cs.add(xs.near_branch_target)
                    if xs.near_branch_target not in pairs:
                        nxt[xs.near_branch_target] = xe.near_branch_target
        frontier = nxt
    return pairs, ev, callees


SKIP = re.compile(r'(-test|fake|selftest|vcallsites|ts3addr|ts3bytes|replay-fake)', re.I)
HEX = re.compile(r'(?<=[\w\)\]])\s*\+\s*(0x[0-9A-Fa-f]{1,4})\b')
LIST = re.compile(r'static const (?:WORD|DWORD|int|u16|u32)\s+\w+\[\d*\]\s*=\s*\{([^}]*)\}')
ASM = re.compile(r'\[\s*e[a-ds][xip]\s*\+\s*(0x[0-9A-Fa-f]+|\d+)h?\s*\]', re.I)


def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), t, flags=re.S)
    t = re.sub(r'"(\\.|[^"\\\n])*"', '""', t)
    return re.sub(r'//[^\n]*', '', t)


def offsets(text, name):
    """{value: [line]} for one source file."""
    out = {}
    code = strip_comments(text)
    for n, line in enumerate(code.split('\n'), 1):
        vals = {int(v, 16) for v in HEX.findall(line)} | {int(v, 0) for v in ASM.findall(line) if '__asm' in text}
        for v in vals:
            if 4 <= v < 0x4000:
                out.setdefault(v, []).append(n)
    for m in LIST.finditer(code):
        n = code.count('\n', 0, m.start()) + 1
        for v in re.findall(r'\b0x([0-9A-Fa-f]{1,4})\b', m.group(1)):
            if 4 <= int(v, 16) < 0x4000:
                out.setdefault(int(v, 16), []).append(n)
    return out


def comment(raw, n):
    """The // comment of a source line (or the line above), for the field column."""
    for k in (n - 1, n - 2):
        if 0 <= k < len(raw) and '//' in raw[k]:
            return raw[k].split('//', 1)[1].strip()
    return ''


# game fields named in tasks that our code reaches only through game functions: (value, field, Steam instruction)
EXTRA = [(0x174, 'Lot size x in metres (steerro fake world writes it; game reads it at 0x654F9D)', 0x654F9D),
         (0x178, 'Lot size y in metres (game read 0x654FB8)', 0x654FB8),
         (0x290, 'Lot per-level floor-height grid (docs/101, read 0x654EBB)', 0x654EBB)]


def sources():
    root = os.path.join(REPO, 'native')
    files = [os.path.join(root, 'worldimage', 'wimage.h')]
    d = os.path.join(root, 'ts3native')
    files += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(('.cpp', '.h')) and not SKIP.search(f)]
    return files


def ea_table():
    t = open(os.path.join(REPO, 'native', 'ts3native', 'ts3addr-ea169.h')).read()
    return {int(a, 16): int(b, 16) for a, b in re.findall(r'\{0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+)\}', t)}


def sites():
    out = subprocess.run(['node', os.path.join(REPO, 'tools', 'hook-sites.ts')], capture_output=True, text=True,
                         timeout=120, cwd=REPO).stdout
    s = {}
    for line in out.splitlines():
        m = re.match(r'0x([0-9A-F]{8})\s+(\S+)\s+(\S+)', line)
        if m:
            s.setdefault(m.group(3).split(':')[0], set()).add(int(m.group(1), 16))
    return s


def closure(fns, callees, depth):
    out, fr = set(fns), set(fns)
    for _ in range(depth):
        fr = {c for f in fr for c in callees.get(f, ())} - out
        out |= fr
    return out


def verdicts(srcs, site_map, pairs, ev, callees, depth, census):
    """rows: (file, value, lines, verdict, steam evidence, ea evidence, note)."""
    rows = []
    for path, offs in srcs:
        name = os.path.basename(path)
        own = closure({a for a in site_map.get(name, ()) if a in pairs}, callees, depth)
        for v, lines in sorted(offs.items()):
            evs = ev.get(('disp', v), []) + ev.get(('imm', v), [])
            if name in EA_ONLY:
                rows.append((name, v, lines, 'n/a', '-', '-', EA_ONLY[name]))
                continue
            if not evs:
                cs, ce = census(v)
                if cs and len(cs) == len(ce):
                    rows.append((name, v, lines, 'same-census', ' '.join('%08X' % a for a in cs[:2]),
                                 ' '.join('%08X' % a for a in ce[:2]), 'not in aligned code; %d [reg+disp] uses in each .text' % len(cs)))
                else:
                    rows.append((name, v, lines, 'unverifiable', '-', '-', 'no aligned Steam instruction uses it; .text uses '
                                 'Steam %d, 1.69 %d' % (len(cs), len(ce))))
                continue
            local = [e for e in evs if e[3] in own]
            mv = [e for e in evs if e[2] != v]
            mv_own = [e for e in mv if e[3] in own]
            verdict = 'differs' if mv_own else 'same' if local else 'same-global'
            show = (mv_own or local or evs)[:2]
            note = '%d aligned uses (%d in own functions), %d moved (%d in own)' % (len(evs), len(local), len(mv), len(mv_own))
            if mv:
                note += ': ' + ' '.join('0x%X@%08X->%s@%08X' % (v, s, '?' if n is None else '0x%X' % n, e) for s, e, n, _ in mv[:3])
            rows.append((name, v, lines, verdict, ' '.join('%08X' % e[0] for e in show), ' '.join('%08X' % e[1] for e in show), note))
    return rows


# files whose offsets were read from the 1.69 image itself (no Steam counterpart)
EA_ONLY = {'eadiag.cpp': 'EA-only file (#ifdef TS3A_TABLE), raw 1.69 offsets verified in the 1.69 image (docs/234)'}


def disp_census(img, v):
    """Every instruction in .text with a [reg+v] disp32 operand (base not esp/ebp), by byte scan + decode."""
    pat, out, m = struct.pack('<I', v), [], img.mem
    lo, hi = img.text[0] - img.base, img.text[1] - img.base
    i = m.find(pat, lo, hi)
    while i >= 0:
        for back in (2, 3):
            x = img.ins(img.base + i - back)
            if x.len >= back + 4 and x.memory_displacement == v and x.memory_base not in (Register.NONE, Register.ESP, Register.EBP):
                out.append(x.ip)
        i = m.find(pat, i + 1, hi)
    return out


MONO = (0xE40000, 0xEB2000)  # embedded Mono 1.2.3 + Boehm GC in TS3W.exe (docs/29: Boehm 0xE40000.., runtime ..0xEB1E60)


def consts(x, img):
    """Displacements and immediates of x that are not image addresses (fields, sizes, constants)."""
    out = []
    lo, hi = img.base, img.base + len(img.mem)
    if any(x.op_kind(i) == OpKind.MEMORY for i in range(x.op_count)):
        d = x.memory_displacement & 0xFFFFFFFF
        out.append(None if lo <= d < hi else d)
    for i in range(x.op_count):
        if x.op_kind(i) in (OpKind.IMMEDIATE8, OpKind.IMMEDIATE16, OpKind.IMMEDIATE32, OpKind.IMMEDIATE8TO16,
                            OpKind.IMMEDIATE8TO32, OpKind.IMMEDIATE8_2ND):
            v = x.immediate(i) & 0xFFFFFFFF
            out.append(None if lo <= v < hi else v)
    return out


def mono_compare(steam, ea, pairs):
    """(same, differ list) over the function pairs inside MONO: same = equal instruction shapes and equal non-address
    displacements/immediates, i.e. identical code up to relocated addresses."""
    same, diff = 0, []
    for s, e in sorted(pairs.items()):
        if not MONO[0] <= s < MONO[1]:
            continue
        fs, fe = steam.func(s), ea.func(e)
        ok = len(fs) == len(fe) and all(shape(a) == shape(b) and consts(a, steam) == consts(b, ea) for a, b in zip(fs, fe))
        if ok:
            same += 1
        else:
            diff.append((s, e, len(fs), len(fe)))
    return same, diff


def run(args):
    steam = Image(find('game-copy/bin/TS3W.exe'))
    ea = Image(find('probe-out/ts3-ea169.bin'))
    table = ea_table()
    site_map = sites()
    used = set().union(*site_map.values())
    seeds = {s: e for s, e in table.items() if s in used and steam.in_text(s) and ea.in_text(e)}
    pairs, ev, callees = corpus(steam, ea, seeds, args.depth)
    # tag evidence with the Steam function it came from
    owner = {}
    for s in pairs:
        for x in steam.func(s):
            owner.setdefault(x.ip, s)
    ev = {f: [(a, b, c, owner.get(a)) for a, b, c in v] for f, v in ev.items()}
    srcs = []
    for p in sources():
        o = offsets(open(p, encoding='utf-8', errors='replace').read(), os.path.basename(p))
        if o:
            srcs.append((p, o))
    rows = verdicts(srcs, site_map, pairs, ev, callees, args.depth, lambda v: (disp_census(steam, v), disp_census(ea, v)))
    raws = {os.path.basename(p): open(p, encoding='utf-8', errors='replace').read().split('\n') for p, _ in srcs}
    for v, what, ip in EXTRA:
        hit = [e for e in ev.get(('disp', v), []) if e[0] == ip]
        vd = 'unverifiable' if not hit else 'same' if hit[0][2] == v else 'differs'
        rows.append(('(game) ' + what, v, [], vd, '%08X' % ip, '%08X' % hit[0][1] if hit else '-',
                     'EA value %s' % (('0x%X' % hit[0][2]) if hit and hit[0][2] is not None else '?')))
    print('struct-offsets: %d function pairs (%d seeds, depth %d), %d (file, offset) rows' % (len(pairs), len(seeds), args.depth, len(rows)))
    cnt = {}
    for r in rows:
        cnt[r[3]] = cnt.get(r[3], 0) + 1
    print('verdicts: ' + ', '.join('%s %d' % kv for kv in sorted(cnt.items())))
    ms, md = mono_compare(steam, ea, pairs)
    print('mono range %X-%X: %d function pairs identical up to addresses, %d differ%s' % (MONO[0], MONO[1], ms, len(md),
          ': ' + ' '.join('%X->%X (%d/%d ins)' % d for d in md[:20]) if md else ''))
    if args.csv:
        with open(args.csv, 'w') as f:
            f.write('file,offset,lines,verdict,steam,ea,note\n')
            for n, v, lines, vd, s, e, note in rows:
                f.write('%s,0x%X,%s,%s,%s,%s,"%s"\n' % (n, v, ' '.join(map(str, lines[:6])), vd, s, e, note))
    if args.md:
        rank = {'differs': 0, 'unverifiable': 1, 'same-census': 2, 'same-global': 3, 'same': 4, 'n/a': 5}
        by = {}
        for r in rows:
            by.setdefault(r[1], []).append(r)
        with open(args.md, 'w', encoding='utf-8') as f:
            f.write('| offset | struct/field (source comment) | users in native/ | Steam evidence | 1.69 evidence | verdict |\n')
            f.write('|---|---|---|---|---|---|\n')
            for v in sorted(by):
                rs = sorted(by[v], key=lambda r: rank[r[3]])
                worst = rs[0]
                ev_row = next((r for r in rs if r[4] != '-'), worst)
                users = ', '.join(r[0] if not r[2] else '%s:%s' % (r[0], r[2][0]) for r in rs[:6])
                users += ' (+%d files)' % (len(rs) - 6) if len(rs) > 6 else ''
                fld = next((comment(raws[r[0]], r[2][0]) for r in rs if r[2] and r[0] in raws and comment(raws[r[0]], r[2][0])), '')
                fld = (fld[:90] + '...') if len(fld) > 90 else fld
                vds = sorted({r[3] for r in rs}, key=lambda x: rank[x])
                f.write('| 0x%X | %s | %s | %s | %s | %s |\n' % (v, fld.replace('|', '/'), users, ev_row[4], ev_row[5], ' / '.join(vds)))
    if args.changes:
        with open(args.changes, 'w') as f:
            f.write('kind,steam_value,ea_value,steam_ip,ea_ip,steam_fn\n')
            for (k, v), lst in sorted(ev.items()):
                for s, e, n, o in lst:
                    if n != v:
                        f.write('%s,0x%X,%s,%08X,%08X,%s\n' % (k, v, '?' if n is None else '0x%X' % n, s, e, '%08X' % o if o else '?'))
    return 0


def self_check():
    # Steam: mov eax,[ecx+0x174]; add edx,0x10; ret   EA: mov eax,[ecx+0x178]; add edx,0x10; ret
    s = Image(raw=bytes.fromhex('8B8174010000 83C210 C3'.replace(' ', '')), base=0x1000)
    e = Image(raw=bytes.fromhex('8B8178010000 83C210 C3'.replace(' ', '')), base=0x2000)
    pairs, ev, _ = corpus(s, e, {0x1000: 0x2000}, 0)
    assert pairs == {0x1000: 0x2000}, pairs
    assert ev[('disp', 0x174)] == [(0x1000, 0x2000, 0x178)], ev
    assert ev[('imm', 0x10)] == [(0x1006, 0x2006, 0x10)], ev
    o = offsets('x = *(DWORD*)(p + 0x174); // + 0x999\ny = q + 0x2;\n__asm { mov eax, [ecx+0x24] }\n'
                'static const WORD kZ[] = {0x128,\n 0x12C};\n', 'a.cpp')
    assert o == {0x174: [1], 0x24: [3], 0x128: [4], 0x12C: [4]}, o
    print('struct-offsets self-check ok')
    return 0


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--self-check', action='store_true')
    ap.add_argument('--csv')
    ap.add_argument('--changes')
    ap.add_argument('--md')
    ap.add_argument('--depth', type=int, default=2)
    a = ap.parse_args()
    sys.exit(self_check() if a.self_check else run(a))
