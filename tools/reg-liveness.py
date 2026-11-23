"""Register liveness across our detours: callers that keep a volatile register live across a call to an EA function
that never writes it (MSVC whole-program optimisation), where our ABI-following C++ replacement would clobber it (docs/237).

    python tools/reg-liveness.py [--edition steam|ea169|both] [--addr 0xAB8540 ...] [--all] [--modules] [--rc-ini F] [--csv F]
    python tools/reg-liveness.py --self-check

Sites: every TS3W.exe code literal in native/ts3native (node tools/hook-sites.ts), Steam 1.67 values; the 1.69 address
comes from ts3addr-ea169.h (main repo, else the ../sims-3-ea169-port worktree). Per site, in each image:
  - `E8` at the site = patched call site: the replaced function is its target, the only caller is the site.
  - a function with direct `E8` xrefs = entry detour candidate: callers are those xrefs.
  - else (mid-function or reached only through pointers) = listed as not analysed: inline stubs own every register,
    pointer callers (vtables, Mono internal calls, function tables) cannot rely on more than the ABI.
Preserved set P(f): volatile registers (eax ecx edx xmm0-7 flags) that f never writes, following jumps, tail calls,
switch tables and direct calls transitively. Indirect calls and import thunks count as writing all of them. Bounded:
BUDGET instructions per function and DEPTH call levels; a hit marks the result `bound` (P may then be too big).
Live: from the return address, the first access to R on any path is a read (before a write), within LIVE_STEPS
instructions (a hit prints `live search bound`), also through direct calls that read R on entry. Scalar SSE merges
(cvtsi2ss, movss xmm,xmm) count as writes. Weak evidence (ecx at an indirect call, which would be `this`) is marked.
Not followed: callers' callers (a caller that itself never
writes R passes the reliance up; eax at the caller's `ret` is not counted either), tail-jump xrefs.
Hook side: conservative. Unless HOOKS below says a site's hook is a register-saving naked stub or not a hook at all,
an ABI C++ hook is assumed (writes eax ecx edx xmm0-7 flags). Every risky triple is printed with the hook's Enabled= in
the shipped ini (native/ts3native/ts3native.ini) and the rc ini (--rc-ini, default game-copy/ts3native.ini.rel025b).
"""
import argparse
import os
import re
import struct
import subprocess
import sys

from iced_x86 import Decoder, FlowControl, InstructionInfoFactory, Mnemonic, OpAccess, OpKind, Register

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
STEAM_EXE = os.path.join(REPO, 'game-copy', 'bin', 'TS3W.exe')
EA_IMG = os.path.join(REPO, 'probe-out', 'ts3-ea169.bin')
EA_TABLES = [os.path.join(REPO, 'native', 'ts3native', 'ts3addr-ea169.h'),
             os.path.join(REPO, '..', 'sims-3-ea169-port', 'native', 'ts3native', 'ts3addr-ea169.h')]
VOL = ['eax', 'ecx', 'edx'] + ['xmm%d' % i for i in range(8)] + ['flags']
ALL = frozenset(VOL)
BUDGET, DEPTH, LIVE_STEPS = 40000, 30, 4000

# Reviewed hook side per Steam site: [(ini section, verdict, registers the hook keeps, note)]. verdict: 'abi' = compiled
# C++ reached before the caller gets control back (writes eax ecx edx xmm0-7 flags), 'saves' = naked stub that keeps the
# listed registers around every C++ call, 'nohook' = we only call or read it (a pointer-slot swap counts: pointer callers
# follow the ABI). An unlisted site with a live caller prints as UNREVIEWED and counts as 'abi'.
GPRF = 'eax ecx edx flags'
HOOKS = {
    0x00AB8540: [('LotSinCos', 'abi', '', 'LscHook: __fastcall C++ detour, no stub'),
                 ('Verify', 'saves', GPRF, 'VF_STUB + VfLeaveThunk: pushad/pushfd around VfEnter/VfLeave (C++), xmm not saved; '
                                          'skipped while [LotSinCos] owns the prologue')],
    0x00C0FD60: [('MemDiag', 'nohook', '', 'GC notify pointer slot swapped (memexp.cpp); direct callers keep the original')],
    0x00580420: [('EaCrashFixes', 'nohook', '', 'GeomNoMaterial getter, called')],
    0x006C93D0: [('CrashForensics', 'nohook', '', 'resource getter, called')],
    0x007EAEA0: [('SteerRo', 'nohook', '', 'vtable slot compared')],
    0x0081C880: [('SteerRo', 'nohook', '', 'option test, called')],
    0x008292D0: [('SteerRo', 'nohook', '', 'world getter, called')],
    0x00935770: [('AutShadow/RtoRo/Unseen', 'nohook', '', 'component getter, called')],
    0x00938B50: [('Discourage', 'nohook', '', 'object service getter, called')],
    0x00953690: [('AvoidRo', 'nohook', '', 'posture getter, called')],
    0x00E52160: [('Satb', 'nohook', '', 'value-type test, called')],
}
SHIPPED_INI = os.path.join(REPO, 'native', 'ts3native', 'ts3native.ini')
RC_INI = os.path.join(REPO, 'game-copy', 'ts3native.ini.rel025b')


def at_risk(verdict, keeps, reg):
    return verdict != 'nohook' and not (verdict == 'saves' and reg in keeps.split())


def ini_enabled(text, section):
    """Enabled= of [section] ('-' when the section or key is missing). Sections with '/' are not single ini sections."""
    cur = None
    for line in text.splitlines():
        m = re.match(r'\s*\[([^\]]+)\]', line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r'\s*Enabled\s*=\s*(\S+)', line)
        if m and cur == section:
            return m.group(1)
    return '-'

_NAMES = {getattr(Register, n): n.lower() for n in dir(Register) if not n.startswith('_')}
_GPR = {'eax': 'eax', 'ax': 'eax', 'al': 'eax', 'ah': 'eax', 'ecx': 'ecx', 'cx': 'ecx', 'cl': 'ecx', 'ch': 'ecx',
        'edx': 'edx', 'dx': 'edx', 'dl': 'edx', 'dh': 'edx'}
READS = {OpAccess.READ, OpAccess.COND_READ, OpAccess.READ_WRITE, OpAccess.READ_COND_WRITE}
WRITES = {OpAccess.WRITE, OpAccess.COND_WRITE, OpAccess.READ_WRITE, OpAccess.READ_COND_WRITE}
_INFO = InstructionInfoFactory()
# Scalar SSE ops that keep the destination's upper lanes: iced reports the destination as read, MSVC treats it as written.
MERGE = {getattr(Mnemonic, n) for n in ('CVTSI2SS', 'CVTSI2SD', 'CVTSS2SD', 'CVTSD2SS', 'SQRTSS', 'SQRTSD', 'RCPSS',
                                        'RSQRTSS', 'ROUNDSS', 'ROUNDSD', 'MOVSS', 'MOVSD')}


def vol(reg):
    n = _NAMES.get(reg, '')
    if n in _GPR:
        return _GPR[n]
    m = re.fullmatch(r'[xyz]mm([0-7])', n)
    return 'xmm' + m.group(1) if m else None


class Image:
    """Mapped 32-bit image: a PE file (sections laid out) or an already mapped dump (size = SizeOfImage)."""

    def __init__(self, path=None, raw=None, base=None, text=None):
        if raw is not None:  # self-check: bare code bytes
            self.mem, self.base, self.text = raw, base, text
            self._dec = {}
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
        self._dec = {}

    def in_text(self, a):
        return self.text[0] <= a < self.text[1]

    def u32(self, a):
        o = a - self.base
        return struct.unpack_from('<I', self.mem, o)[0] if 0 <= o <= len(self.mem) - 4 else None

    def ins(self, a):
        x = self._dec.get(a)
        if x is None:
            o = a - self.base
            x = next(iter(Decoder(32, self.mem[o:o + 16], ip=a)))
            self._dec[a] = x
        return x

    def xrefs(self):
        """target -> [call site] for every E8 rel32 whose target is in .text (byte scan)."""
        out, m, lo, hi = {}, self.mem, self.text[0] - self.base, self.text[1] - self.base
        i = m.find(b'\xE8', lo, hi)
        while i >= 0:
            t = (self.base + i + 5 + struct.unpack_from('<i', m, i + 1)[0]) & 0xFFFFFFFF
            if self.in_text(t):
                out.setdefault(t, []).append(self.base + i)
            i = m.find(b'\xE8', i + 1, hi)
        return out

    def table(self, x):
        """Targets of `jmp [idx*4+table]`, or None for other indirect jumps."""
        if x.memory_base != Register.NONE or x.memory_index == Register.NONE or x.memory_index_scale != 4:
            return None
        out, a = [], x.memory_displacement
        while len(out) < 512:
            t = self.u32(a)
            if t is None or not self.in_text(t) or abs(t - x.ip) > 0x10000:
                break
            out.append(t)
            a += 4
        return out


def accesses(x):
    """(read set, write set) of volatile registers for one instruction (flags included)."""
    r, w = set(), set()
    for u in _INFO.info(x).used_registers():
        v = vol(u.register)
        if v:
            if u.access in READS:
                r.add(v)
            if u.access in WRITES:
                w.add(v)
    if x.mnemonic in MERGE and x.op_count > 1 and x.op0_kind == OpKind.REGISTER:
        d = vol(x.op0_register)
        if d and d not in {vol(x.op_register(i)) for i in range(1, x.op_count) if x.op_kind(i) == OpKind.REGISTER}:
            r.discard(d)
            w.add(d)
    if x.rflags_read:
        r.add('flags')
    if x.rflags_modified:
        w.add('flags')
    return r, w


class Analyzer:
    def __init__(self, img):
        self.img, self.memo, self.busy = img, {}, set()

    def writes(self, f, depth=0):
        """(set of volatile registers f may write, bound hit) following tail calls and direct calls."""
        if f in self.memo:
            return self.memo[f]
        if f in self.busy or depth > DEPTH or not self.img.in_text(f):
            return (set(), True) if f in self.busy or depth > DEPTH else (set(ALL), False)
        self.busy.add(f)
        w, bound, calls, seen, work = set(), False, [], set(), [f]
        while work and w != ALL:
            a = work.pop()
            if a in seen or not self.img.in_text(a):
                continue
            if len(seen) >= BUDGET:
                bound = True
                break
            seen.add(a)
            x = self.img.ins(a)
            w |= accesses(x)[1]
            fc = x.flow_control
            if fc == FlowControl.NEXT:
                work.append(x.next_ip)
            elif fc == FlowControl.CONDITIONAL_BRANCH:
                work += [x.next_ip, x.near_branch_target]
            elif fc == FlowControl.UNCONDITIONAL_BRANCH:
                work.append(x.near_branch_target)
            elif fc == FlowControl.CALL:
                calls.append(x.near_branch_target)
                work.append(x.next_ip)
            elif fc == FlowControl.INDIRECT_CALL:
                w |= ALL
            elif fc == FlowControl.INDIRECT_BRANCH:
                t = self.img.table(x)
                if t is None:
                    w |= ALL  # import thunk or computed jump: unknown callee
                else:
                    work += t
            elif fc in (FlowControl.RETURN, FlowControl.INTERRUPT, FlowControl.EXCEPTION):
                pass
            else:
                work.append(x.next_ip)
        for c in calls:
            if w == ALL:
                break
            cw, cb = self.writes(c, depth + 1)
            w |= cw
            bound |= cb
        self.busy.discard(f)
        r = (w, bound)
        if not (bound and depth):  # partial results inside a cycle are not memoised
            self.memo[f] = r
        return r

    def live(self, start, reg, top=True, depth=0):
        """First access to reg from start: (evidence address, kind) if a read comes before a write, else None."""
        seen, work, steps = set(), [start], 0
        while work:
            a = work.pop()
            if a in seen or not self.img.in_text(a):
                continue
            steps += 1
            if steps > LIVE_STEPS:
                return (a, 'BOUND') if top else None
            seen.add(a)
            x = self.img.ins(a)
            r, w = accesses(x)
            if reg in r:
                return (a, 'read')
            fc = x.flow_control
            if reg in w and fc != FlowControl.CALL:
                continue  # killed on this path
            if fc == FlowControl.NEXT:
                work.append(x.next_ip)
            elif fc == FlowControl.CONDITIONAL_BRANCH:
                work += [x.next_ip, x.near_branch_target]
            elif fc == FlowControl.UNCONDITIONAL_BRANCH:
                work.append(x.near_branch_target)
            elif fc == FlowControl.CALL:
                g = x.near_branch_target
                if depth < 2:
                    ev = self.live(g, reg, False, depth + 1)
                    if ev and ev[1] != 'BOUND':
                        return (a, 'arg to 0x%X (read at 0x%X)' % (g, ev[0]))
                if reg not in self.writes(g)[0]:
                    work.append(x.next_ip)
            elif fc == FlowControl.INDIRECT_CALL:
                if reg == 'ecx' and top:
                    return (a, 'weak: ecx at indirect call')
            elif fc == FlowControl.INDIRECT_BRANCH:
                work += self.img.table(x) or []
            elif fc not in (FlowControl.RETURN, FlowControl.INTERRUPT, FlowControl.EXCEPTION):
                work.append(x.next_ip)
        return None


def owner(xr_sorted, a):
    """Nearest direct-call target at or before a: the caller's function start, as a label (no function table on 1.69)."""
    import bisect
    i = bisect.bisect_right(xr_sorted, a) - 1
    return xr_sorted[i] if i >= 0 else 0


def analyse(an, xr, site):
    """One site: dict(kind, fn, preserved, bound, risky=[(caller, reg, evidence)])."""
    img = an.img
    if not img.in_text(site):
        return {'kind': 'outside .text'}
    if img.mem[site - img.base] == 0xE8 and img.ins(site).flow_control == FlowControl.CALL:
        fn, callers, kind = img.ins(site).near_branch_target, [site], 'call site'
    elif site in xr:
        fn, callers, kind = site, xr[site], 'entry'
    else:
        return {'kind': 'mid or pointer-only'}
    w, bound = an.writes(fn)
    keep = [r for r in VOL if r not in w]
    risky, lbound = [], []
    for c in callers:
        for r in keep:
            ev = an.live(img.ins(c).next_ip, r)
            if ev:
                (lbound if ev[1] == 'BOUND' else risky).append((c, r, ev))
    return {'kind': kind, 'fn': fn, 'preserved': keep, 'bound': bound, 'callers': len(callers), 'risky': risky,
            'lbound': lbound}


def ea_table():
    for p in EA_TABLES:
        if os.path.exists(p):
            return {int(a, 16): int(b, 16) for a, b in re.findall(r'\{0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+)\}', open(p).read())}, p
    return {}, None


def sites():
    """Steam address -> [(module, file:line, label)] from tools/hook-sites.ts."""
    out = subprocess.run(['node', os.path.join(REPO, 'tools', 'hook-sites.ts')], capture_output=True, text=True,
                         timeout=120, cwd=REPO).stdout
    s = {}
    for line in out.splitlines():
        m = re.match(r'0x([0-9A-F]{8})\s+(\S+)\s+(\S+)\s*(.*)$', line)
        if m:
            s.setdefault(int(m.group(1), 16), []).append((m.group(2), m.group(3), m.group(4).strip()))
    return s


def self_check():
    # f (0x1000): mov eax,[esp+4]; add eax,1; ret 4, never writes ecx/edx. Caller (0x100A) keeps edx (the lot) across it:
    #   push 5; call f; mov ecx,[edx]; ret  -> edx live (risky); ecx is written before any read (not live).
    # g (0x1020): xor edx,edx; xor eax,eax; ret. Its caller (0x1030) reads edx after the call, but g writes edx: nothing to rely on.
    base, code = 0x1000, bytearray(0x40)
    call = lambda at, to: bytes([0xE8]) + struct.pack('<i', to - (at + 5))
    code[0x00:0x0A] = bytes.fromhex('8B442404' '83C001' 'C20400')
    code[0x0A:0x14] = bytes.fromhex('6A05') + call(0x100C, 0x1000) + bytes.fromhex('8B0A' 'C3')
    code[0x20:0x25] = bytes.fromhex('31D2' '31C0' 'C3')
    code[0x30:0x38] = call(0x1030, 0x1020) + bytes.fromhex('8B0A' 'C3')
    img = Image(raw=bytes(code), base=base, text=(base, base + len(code)))
    an = Analyzer(img)
    xr = img.xrefs()
    assert xr.get(0x1000) == [0x100C] and xr.get(0x1020) == [0x1030], xr
    w, bound = an.writes(0x1000)
    assert w == {'eax', 'flags'} and not bound, w
    r = analyse(an, xr, 0x1000)
    assert r['kind'] == 'entry' and [(c, reg) for c, reg, _ in r['risky']] == [(0x100C, 'edx')], r
    r = analyse(an, xr, 0x100C)
    assert r['kind'] == 'call site' and r['fn'] == 0x1000 and len(r['risky']) == 1, r
    assert analyse(an, xr, 0x1020)['risky'] == []  # g writes edx: nothing preserved to rely on
    assert vol(Register.DL) == 'edx' and vol(Register.XMM3) == 'xmm3' and vol(Register.XMM9) is None
    assert at_risk('abi', '', 'edx') and not at_risk('saves', GPRF, 'edx') and at_risk('saves', GPRF, 'xmm4')
    assert not at_risk('nohook', '', 'edx')
    t = '[A]\nEnabled=1\n[LotSinCos]\nVerify=0\nEnabled=0\n[B]\nEnabled=1\n'
    assert ini_enabled(t, 'LotSinCos') == '0' and ini_enabled(t, 'C') == '-'
    assert ini_enabled('[C]\nX=1\n[D]\nEnabled=1', 'C') == '-'
    assert 0x00AB8540 in HOOKS
    # the validation case (docs/237), when the local images are there: [LotSinCos] Lot_WorldToLocal2D callers keep edx
    tab = ea_table()[0]
    for path, fn, want in ((STEAM_EXE, 0x00AB8540, {0x00AB9037, 0x00AC0C0F, 0x00AC586B, 0x00AC996D}),
                           (EA_IMG, tab.get(0x00AB8540), {0x00AB7CA7})):  # 0xAB7CA7 is the call in 0xAB7C80
        if not os.path.exists(path) or not fn:
            print('reg-liveness: %s missing, image check skipped' % path)
            continue
        img = Image(path)
        xr = img.xrefs()
        got = {c for c, reg, _ in analyse(Analyzer(img), xr, fn)['risky'] if reg == 'edx'}
        assert want <= got, (path, sorted(map(hex, got)))
        assert owner(sorted(xr), 0x00AB7CA7) == 0x00AB7C80 or path == STEAM_EXE
    print('reg-liveness self-check OK')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--self-check', action='store_true')
    ap.add_argument('--edition', default='both', choices=['steam', 'ea169', 'both'])
    ap.add_argument('--addr', action='append', default=[], help='Steam address(es) to analyse instead of all sites')
    ap.add_argument('--all', action='store_true', help='also print sites with no live caller')
    ap.add_argument('--modules', action='store_true', help='per source module: sites by kind and live callers')
    ap.add_argument('--rc-ini', default=RC_INI)
    ap.add_argument('--csv', help='write one row per (hook, register, caller)')
    a = ap.parse_args()
    if a.self_check:
        return self_check()
    src = {int(x, 16): [('cli', '', '')] for x in a.addr} if a.addr else sites()
    tab, tabpath = ea_table()
    ini = [open(p, encoding='utf-8', errors='replace').read() if os.path.exists(p) else '' for p in (SHIPPED_INI, a.rc_ini)]
    rows, risky = [], 0
    for ed in (['steam', 'ea169'] if a.edition == 'both' else [a.edition]):
        img = Image(STEAM_EXE if ed == 'steam' else EA_IMG)
        an, xr = Analyzer(img), img.xrefs()
        starts = sorted(xr)
        print('== %s: %d sites, .text 0x%X-0x%X%s' % (ed, len(src), *img.text, '' if ed == 'steam' else ', table ' + str(tabpath)))
        counts, mods = {}, {}
        for s in sorted(src):
            at = s if ed == 'steam' else tab.get(s)
            r = analyse(an, xr, at) if at is not None else {'kind': 'no ea169 address'}
            k = r['kind'] + (' (bound)' if r.get('bound') else '') + (' (live search bound)' if r.get('lbound') else '')
            for c, reg, _ in r.get('lbound', []):
                print('  live search bound: 0x%08X (steam 0x%08X) caller 0x%08X %s' % (at, s, c, reg))
            counts[k] = counts.get(k, 0) + 1
            for mod in {m for m, _, _ in src[s]}:
                mods.setdefault(mod, {}).setdefault(k, 0)
                mods[mod][k] += 1
                if r.get('risky'):
                    mods[mod]['live'] = mods[mod].get('live', 0) + 1
            if not (r.get('risky') or (a.all and 'fn' in r)):
                continue
            where = ' '.join(sorted({w for _, w, _ in src[s]}))[:80]
            print('0x%08X (steam 0x%08X) %s fn 0x%08X callers %d preserved %s%s  [%s]' % (
                at, s, r['kind'], r['fn'], r['callers'], ','.join(r['preserved']) or '-', ' BOUND' if r['bound'] else '', where))
            for sec, verdict, keeps, note in HOOKS.get(s, [('?', 'UNREVIEWED', '', 'assumed ABI C++')]):
                on = [ini_enabled(t, sec) for t in ini]
                hit = [(c, reg, ev) for c, reg, ev in r['risky'] if at_risk(verdict, keeps, reg)]
                risky += len(hit)
                print('  hook [%s] %s%s (shipped Enabled=%s, rc Enabled=%s): %s; %s' % (
                    sec, verdict, ' keeps ' + keeps if keeps else '', on[0], on[1], 'RISK %d' % len(hit) if hit else 'ok', note))
                for c, reg, ev in r['risky']:
                    rows.append((ed, sec, verdict, 'RISK' if (c, reg, ev) in hit else 'ok', s, at, r['kind'], r['fn'], reg, c,
                                 owner(starts, c), '0x%08X %s' % ev, on[0], on[1]))
            for c, reg, ev in r['risky']:
                print('    caller 0x%08X (in ~0x%08X) keeps %s live: 0x%08X %s' % (c, owner(starts, c), reg, *ev))
        print('   kinds: ' + ', '.join('%s %d' % kv for kv in sorted(counts.items())))
        if a.modules:
            for mod in sorted(mods):
                print('   %-14s %s' % (mod, ', '.join('%s %d' % kv for kv in sorted(mods[mod].items()))))
    if a.csv:
        with open(a.csv, 'w', newline='') as fh:
            fh.write('edition,hook,verdict,risk,steam_site,site,kind,fn,reg,caller,caller_fn,evidence,shipped,rc\n')
            for r in rows:
                fh.write('%s,%s,%s,%s,0x%08X,0x%08X,%s,0x%08X,%s,0x%08X,0x%08X,"%s",%s,%s\n' % r)
    print('risky (hook, register, caller) triples: %d' % risky)
    return 0


if __name__ == '__main__':
    sys.exit(main() or 0)
