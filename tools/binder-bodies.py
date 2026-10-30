"""docs/217 (contributor research round 2, task 2): what each of EA's 1,085 binder native bodies writes.

    python tools/binder-bodies.py --hashes <out.txt> [--exe game-copy/bin/TS3W.exe]
    node tools/binder-bodies.ts --asm game-copy/assemblies --index game-copy/analysis/callgraph/index.json \
        --hashes <out.txt> --out <facts.json>
    python tools/binder-bodies.py --facts <facts.json> [--csv docs/data/binder-bodies.csv] [--json <raw.json>]
    python tools/binder-bodies.py --self-check

Static only (capstone 5 over a copy of the Steam exe; TS3W_EXE overrides the default path). Steps:
1. Registrations: the binder table builder 0xE3E570 calls 0xD9A860(hash, adapter, body, init) once per body (docs/130 D17);
   index = call order (the hashes ascend). --hashes writes them for binder-bodies.ts, which adds the managed facts (bound
   method, parameter kinds, Mono field layout, IL field writes, [AutShadow] worker reach).
2. Stores: each adapter is analysed once over symbolic inputs (stack args A0..A11, entry registers) and every callee's
   summary (stores, final registers, stack pop) is substituted at its call sites, so a store deep in a callee is expressed
   in the adapter's inputs. The dispatcher calls adapter(MonoInvocation frame, ThreadContext) (docs/130 call path), so the
   roots are F (frame: +0x0C this, +0x14 args in 8-byte slots, +0x08 return slot, +0x20 pending exception) and X.
   Stops: Mono allocation entries return a fresh object (N); managed re-entry (ScriptHost_NativeToManaged 0xD8A8E0,
   interpreter and runtime_invoke entries, class init 0xEA3370) is recorded, not followed; the exception builders
   0xE77000-0xE775FF and mono_raise_exception 0xEA18F0 end the path (docs/122 I-122-26); the EA static-field helpers
   0xD89CD0 / 0xD89BF0 / 0xD89C60 are modelled (get = writes its out argument, set = static data). Imports have fixed
   effects (CRT copies write their first argument, Interlocked* their target, others are opaque handoffs).
3. Classes, from the stores and the managed facts: reader (no managed store outside the interpreter frame), writes-args
   (byref / out / array arguments or a struct `this`, with the size), writes-other (fields of `this` or of objects reached
   from arguments, static fields), native-only (EA native globals only), unknown (a store the analysis could not resolve).
   Stores inside Mono, the GC, the EA heap and the binder's own records are runtime bookkeeping and listed apart.
Limits: indirect calls through unresolved pointers are opaque (listed as handoffs, confidence medium); pointer values are
tracked 4 loads deep; analysis budget 200,000 instructions per glue function (truncation is flagged). Field offsets come
from binder-bodies.ts's layout model (gcsort), checked against 859 native `this` stores (docs/217 §2).
"""
import argparse
import ast
import collections
import csv
import json
import os
import struct
import sys
import time
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_REG, CS_OP_IMM, CS_OP_MEM
from capstone import x86_const as X

sys.setrecursionlimit(20000)
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def sec_of(va):
    for s in SECS:
        if s[1] <= va < s[1] + max(s[2], s[4]):
            return s[0]
    return None


def read(va, n):
    s = SEC[sec_of(va)]
    off = s[3] + (va - s[1])
    return b[off:off + n]


def u32(va):
    return struct.unpack_from('<I', read(va, 4))[0]


def rva2off(rva):
    for n, va, vsz, raw, rsz in SECS:
        if va - IB <= rva < va - IB + max(vsz, rsz):
            return raw + rva - (va - IB)


b, IB, SECS, SEC, IMP = b'', 0, [], {}, {}


def load_exe(path):
    global b, IB, SECS, SEC
    b = open(path, 'rb').read()
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    nsec = struct.unpack_from('<H', b, pe + 6)[0]
    opt = struct.unpack_from('<H', b, pe + 20)[0]
    IB = struct.unpack_from('<I', b, pe + 24 + 28)[0]
    SECS = []
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        vsz, va, rsz, raw = struct.unpack_from('<IIII', b, o + 8)
        SECS.append((b[o:o + 8].rstrip(b'\0').decode(), IB + va, vsz, raw, rsz))
    SEC = {s[0]: s for s in SECS}
    dd = pe + 24 + 96
    p = rva2off(struct.unpack_from('<I', b, dd + 8)[0])
    while True:  # import directory: IAT slot -> dll!name
        oft, _, _, name_rva, ft = struct.unpack_from('<IIIII', b, p)
        if not name_rva:
            break
        dll = b[rva2off(name_rva):].split(b'\0')[0].decode()
        q, k = rva2off(oft or ft), 0
        while True:
            e = struct.unpack_from('<I', b, q + 4 * k)[0]
            if not e:
                break
            nm = ('#%d' % (e & 0xffff)) if e & 0x80000000 else b[rva2off(e) + 2:].split(b'\0')[0].decode()
            IMP[IB + ft + 4 * k] = dll.lower() + '!' + nm
            k += 1
        p += 20


md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True
_dec = {}


class LMem:
    __slots__ = ('base', 'index', 'scale', 'disp', 'segment')


class LOp:
    __slots__ = ('type', 'reg', 'imm', 'mem', 'size', 'access')


class LIns:
    __slots__ = ('address', 'size', 'mnemonic', 'operands', 'wr', 'rd')


def light(ins):
    li = LIns()
    li.address, li.size, li.mnemonic = ins.address, ins.size, ins.mnemonic
    ops = []
    for o in ins.operands:
        lo = LOp()
        lo.type, lo.size, lo.access = o.type, o.size, o.access
        lo.reg = o.reg if o.type == CS_OP_REG else 0
        lo.imm = o.imm if o.type == CS_OP_IMM else 0
        if o.type == CS_OP_MEM:
            m = LMem()
            m.base, m.index, m.scale, m.disp, m.segment = o.mem.base, o.mem.index, o.mem.scale, o.mem.disp, o.mem.segment
            lo.mem = m
        else:
            lo.mem = None
        ops.append(lo)
    li.operands = ops
    try:
        ra = ins.regs_access()
        li.rd, li.wr = list(ra[0]), list(ra[1])
    except Exception:
        li.rd, li.wr = [], []
    return li


def dec(a):
    if a not in _dec:
        try:
            _dec[a] = light(next(md.disasm(read(a, 16), a, 1)))
        except (StopIteration, KeyError, TypeError):
            _dec[a] = None
    return _dec[a]


PARENT = {}
for full, parts in {'eax': 'al ah ax eax', 'ebx': 'bl bh bx ebx', 'ecx': 'cl ch cx ecx', 'edx': 'dl dh dx edx',
                    'esi': 'si esi', 'edi': 'di edi', 'ebp': 'bp ebp', 'esp': 'sp esp'}.items():
    for pt in parts.split():
        PARENT[getattr(X, 'X86_REG_' + pt.upper())] = (full, pt == full)
GPR = ['eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp']
REGP = {'eax': 'Ra', 'ebx': 'Rb', 'ecx': 'Rc', 'edx': 'Rd', 'esi': 'Rs', 'edi': 'Ri', 'ebp': 'Rp'}
NARGS = 12
PARAMS = set(REGP.values()) | {'A%d' % i for i in range(NARGS)}

U = ('U', (), None)
UV = frozenset([U])
CHAIN_MAX, SET_MAX, BUDGET = 4, 8, 200000


def C(n):
    return ('G', (), n & 0xffffffff)


def cap(atoms):
    s = frozenset(atoms)
    if len(s) <= SET_MAX:
        return s
    s = frozenset((r, c, None) if r != 'U' else U for r, c, d in s)
    if len(s) <= SET_MAX:
        return s
    s = frozenset((r, c[:1], None) if r != 'U' else U for r, c, d in s)
    if len(s) <= SET_MAX:
        return s
    keep = sorted(s, key=lambda a: (a[0] in ('U', 'G'), str(a)))[:SET_MAX - 1]
    return frozenset(keep + [U])


def add1(a, n):
    r, c, d = a
    if r == 'U':
        return U
    if d is None or n is None:
        return (r, c, None)
    if r == 'G' and not c:
        return C(d + n)
    return (r, c, d + n)


def addc(v, n):
    return cap(add1(a, n) for a in v)


def is_const(v):
    return all(r == 'G' and not c and d is not None for r, c, d in v)


def addv(v, w, scale=1):
    if is_const(w):
        out = set()
        for _, _, n in w:
            out |= addc(v, n * scale)
        return cap(out)
    if is_const(v) and scale == 1:
        return addv(w, v)
    if is_const(v):  # const table base + scaled index
        return cap(('GT', (), d) for _, _, d in v)
    return addc(v, None)


def load1(a, size, stack):
    """value(s) loaded from address atom a"""
    r, c, d = a
    if r == 'S' and d is not None:
        return stack.get(d, UV) if size == 4 else UV
    if r in ('U', 'S'):
        return UV
    if size != 4:
        return UV
    if r == 'G' and not c and d is not None:
        if d in IMP:
            return frozenset([('I', (), d)])
        if sec_of(d) in ('.rdata', '.text'):
            return frozenset([C(u32(d))])
        return frozenset([('G', (d,), 0)])
    if len(c) >= CHAIN_MAX:
        return frozenset([(r, c, None)])
    return frozenset([(r, c + (d,), 0)])


CUR = [None]


def load(st, addr, size):
    out = set()
    for a in addr:
        if a[0] == 'S' and a[2] is not None and a[2] >= 4 and CUR[0] is not None:
            CUR[0].nargs = max(CUR[0].nargs, min((a[2] - 4) // 4 + 1, NARGS))
        out |= load1(a, size, st.s)
    return cap(out)


class St:
    __slots__ = ('r', 's')

    def __init__(self, r, s):
        self.r, self.s = r, s

    def copy(self):
        return St(dict(self.r), dict(self.s))


def merge(a, bst):
    ch = False
    for src, dst in ((bst.r, a.r), (bst.s, a.s)):
        for k, v in src.items():
            o = dst.get(k)
            if o is None:
                dst[k] = v
                ch = True
            elif not v <= o:
                n = cap(o | v)
                if n != o:
                    dst[k] = n
                    ch = True
    return ch


def esp_off(st):
    v = st.r['esp']
    if len(v) == 1:
        r, c, d = next(iter(v))
        if r == 'S' and d is not None:
            return d
    return None


class Sum:
    __slots__ = ('w', 'regs', 'pop', 'flags', 'steps', 'nat', 'ext', 'trunc', 'nargs', 'used', 'entry')

    def __init__(self):
        self.w = {}  # (atom, size) -> [site, valueset]
        self.regs = {}
        self.pop = None
        self.flags = {}
        self.steps = 0
        self.ext = {}
        self.trunc = False
        self.nargs = 0
        self.used = set()
        self.entry = None
        self.nat = {}  # native write targets: global address or root kind -> first site

    def flag(self, k, site):
        self.flags.setdefault(k, site)

    def write(self, a, size, site, val):
        e = self.w.get((a, size))
        if e is None:
            self.w[(a, size)] = [site, val]
        else:
            e[1] = cap(e[1] | val)


def store(st, addr, size, val, site, res):
    single = len(addr) == 1
    for a in addr:
        r, c, d = a
        if r == 'S':
            if d is None:
                continue
            if size == 4 and d % 4 == 0:
                st.s[d] = val if single else cap(st.s.get(d, frozenset()) | val)
            else:
                if size is None:
                    for k in [k for k in st.s if k >= d]:
                        st.s[k] = UV
                for k in range((d // 4) * 4, d + (size or 4), 4):
                    st.s[k] = UV
        elif r == 'G' and not c and d is not None and d < 0x10000:
            continue  # null-ish garbage from merged paths
        else:
            res.write(a, size, site, val)


WRITE0 = set('''mov movss movsd movaps movups movapd movupd movq movd movdqa movdqu movlps movhps movlpd movhpd movnti movntps
movntq movntdq add sub adc sbb and or xor inc dec neg not shl shr sar sal rol ror rcl rcr shld shrd xchg xadd cmpxchg cmpxchg8b
bts btr btc fst fstp fist fistp fisttp fbstp fnstsw fstsw fnstcw fstcw fnsave fsave fnstenv fstenv stmxcsr pop stosb stosw stosd
movsb movsw pextrw extractps'''.split())

MEMO, ACTIVE, STATS, PROV = {}, [], {'fns': 0, 'steps': 0}, {}
SPECIAL = {}
GLUE = (0xD70000, 0xE40000)  # EA script host glue and binder bodies; Mono + GC start at 0xE45000
PROBES = {0xF7EE60: 1, 0xF7E8D0: 16, 0xF7E8E6: 8}  # __chkstk, _alloca_probe_16, _alloca_probe_8


def ea(st, m):
    if m.segment == X.X86_REG_FS:
        return frozenset([('T', (), m.disp)])
    v = st.r.get(PARENT[m.base][0], UV) if m.base else frozenset([C(0)])
    if m.index:
        v = addv(v, st.r.get(PARENT[m.index][0], UV), m.scale)
    return addc(v, m.disp)


def opval(st, op):
    if op.type == CS_OP_IMM:
        return frozenset([C(op.imm)])
    if op.type == CS_OP_REG:
        p = PARENT.get(op.reg)
        return st.r.get(p[0], UV) if p and p[1] else UV
    return load(st, ea(st, op.mem), op.size)


def setreg(st, reg, val):
    p = PARENT.get(reg)
    if p:
        st.r[p[0]] = val if p[1] else UV


# ---------- substitution of a callee summary into the caller ----------
def sub_atom(a, env, stack):
    r, c, d = a
    if r not in env:
        return frozenset([a])
    cur = env[r]
    for off in c:
        nxt = set()
        for x in cur:
            nxt |= load1(add1(x, off), 4, stack)
        cur = cap(nxt)
    return addc(cur, d)


def sub_val(v, env, stack):
    out = set()
    for a in v:
        out |= sub_atom(a, env, stack)
    return cap(out)


def sub_key(k, env, stack):
    if isinstance(k, tuple):
        return tuple(sub_key(x, env, stack) if isinstance(x, tuple) and len(x) != 3 else
                     (tuple(sorted(map(repr, sub_atom(x, env, stack)))) if isinstance(x, tuple) and len(x) == 3 and isinstance(x[1], tuple) else x)
                     for x in k)
    return k


def call(st, ins, tv, res):
    E = esp_off(st)
    if E is None:
        res.flag('esp-lost', ins.address)
        return False
    env = {'A%d' % i: st.s.get(E + 4 * i, UV) for i in range(NARGS)}
    mk = [x[2] for x in st.r.get('_m', ()) if x[0] == 'S' and x[2] is not None]
    win = max(0, min(NARGS, (max(mk) - E) // 4)) if mk else 4
    nx = dec(ins.address + ins.size)
    if nx is not None and nx.mnemonic == 'add' and len(nx.operands) == 2 and nx.operands[0].type == CS_OP_REG and \
            nx.operands[0].reg == X.X86_REG_ESP and nx.operands[1].type == CS_OP_IMM:
        win = max(win, min(NARGS, nx.operands[1].imm // 4))
    for g, pn in REGP.items():
        env[pn] = st.r.get(g, UV)
    targets = []
    for r, c, d in tv:
        if r == 'G' and not c and d is not None and sec_of(d) == '.text':
            targets.append(d)
        elif r == 'I':
            targets.append(('imp', IMP[d]))
        else:
            targets.append(('ind', (r, c, d)))
    newregs, pops = {}, set()
    for t in targets:
        if isinstance(t, tuple):
            s = external(t)
            opaque = win
        elif t in SPECIAL:
            s = SPECIAL[t]
            opaque = win
        elif THROW[0] <= t < THROW[1] or t == 0xEA18F0:
            s = THROWS  # exception object built / raised: error path, the exception is fresh
            opaque = 0
        else:
            s = summary(t)
            opaque = None
        for pn in s.used & {'Rc', 'Rd', 'Ra'}:
            for x in env.get(pn, ()):
                if x[0] in REGP.values() and not x[1]:
                    res.used.add(x[0])
        base = E - 4
        for (a, size), (site, val) in s.w.items():
            v2 = sub_val(val, env, st.s)
            for a2 in sub_atom(a, env, st.s):
                if a2[0] == 'S':
                    if a2[2] is not None and size == 4:
                        st.s[a2[2]] = cap(st.s.get(a2[2], frozenset()) | v2)
                    continue
                if a2[0] == 'G' and not a2[1] and a2[2] is not None and a2[2] < 0x10000:
                    continue
                if (a2, size) not in res.w:
                    PROV.setdefault((res.entry, 'w', a2), (t, a, ins.address))
                res.write(a2, size, site, v2)
        for k, site in s.flags.items():
            if not (isinstance(k, tuple) and k[0] in ('budget', 'recursion', 'deep')):
                res.flag(k, site)
        if s.trunc:
            lab = 'trunc:%X' % t
            for pn in ['A%d' % i for i in range(s.nargs)] + sorted(s.used & {'Rc', 'Rd', 'Ra'}):
                for a2 in env[pn]:
                    if a2[0] in PARAMS or a2[0] in KEEPROOT:
                        PROV.setdefault((res.entry, lab, a2), (t, pn, ins.address))
                        res.ext.setdefault((lab, a2), ins.address)
        for (lab, a), site in s.ext.items():
            if lab == 'indirect-target':
                res.ext.setdefault((lab, a), site)
                continue
            if opaque is not None and not a[1] and (a[0] == 'Rd' or a[0][0] == 'A' and 0 < opaque <= int(a[0][1:])):
                continue  # beyond the pushed arguments of an opaque callee (A0 always kept: args stored with mov)
            for a2 in sub_atom(a, env, st.s):
                if a2[0] in PARAMS or a2[0] in KEEPROOT or lab in SFLAB:
                    if (lab, a2) not in res.ext:
                        PROV.setdefault((res.entry, lab, a2), (t, a, ins.address))
                    res.ext.setdefault((lab, a2), site)
        rt = isinstance(t, int) and (t >= 0xE40000 or 0x4E0000 <= t < 0x4F0000 or t in RUNTIME_EA)
        for k, site in s.nat.items():
            # writes reached through a Mono / GC / EA-heap callee are runtime bookkeeping, wherever the store sits
            natadd(res, ('runtime:' + k.split(':', 1)[1]) if rt else k, site)
        for g in GPR:
            v = s.regs.get(g)
            if v is not None:
                newregs[g] = cap(newregs.get(g, frozenset()) | sub_val(v, env, st.s))
        pops.add(s.pop or 0)
    for g in GPR:
        st.r[g] = newregs.get(g, UV)
    STATS['calls'] = STATS.get('calls', 0) + 1
    if len(pops) > 1:
        res.flag('pop-mismatch', ins.address)
    st.r['esp'] = frozenset([('S', (), E + max(pops))])
    setmark(st)
    return True


def subflag(k, env, stack):
    if isinstance(k, tuple) and len(k) == 3 and k[0] in ('arg-to-ext', 'managed-call') and isinstance(k[2], tuple):
        return (k[0], k[1], frozenset(x for x in sub_atom(k[2], env, stack)))
    if isinstance(k, tuple) and len(k) == 3 and k[0] in ('arg-to-ext', 'managed-call') and isinstance(k[2], frozenset):
        out = set()
        for a in k[2]:
            out |= sub_atom(a, env, stack)
        return (k[0], k[1], frozenset(out))
    return k


KEEPROOT = ('MV', 'F', 'X', 'N')


def keepflag(k):
    if isinstance(k, tuple) and len(k) == 3 and k[0] in ('arg-to-ext', 'managed-call'):
        return any(x[0] in PARAMS or x[0] in ('MV', 'F', 'X', 'N') for x in (k[2] if isinstance(k[2], frozenset) else [k[2]]))
    return True


def interesting(a):
    return a[0] not in ('G', 'U', 'S', 'I', 'RA', 'GT', 'T')


_EXT = {}
KEEP = {g: frozenset([(REGP[g], (), 0)]) for g in ('ebx', 'esi', 'edi', 'ebp')}
# stdcall arg counts of the Win32 imports the analysis reaches (all others: flagged, pop 0)
STDCALL = {'LeaveCriticalSection': 1, 'EnterCriticalSection': 1, 'TryEnterCriticalSection': 1, 'InitializeCriticalSection': 1,
           'DeleteCriticalSection': 1, 'TlsGetValue': 1, 'TlsSetValue': 2, 'TlsAlloc': 0, 'GetCurrentThreadId': 0, 'GetCurrentThread': 0,
           'GetCurrentProcess': 0, 'CloseHandle': 1, 'InterlockedIncrement': 1, 'InterlockedDecrement': 1, 'InterlockedExchange': 2,
           'InterlockedCompareExchange': 3, 'InterlockedExchangeAdd': 2, 'VirtualQuery': 3, 'VirtualAlloc': 4, 'VirtualFree': 3,
           '#11': 1, 'WaitForSingleObject': 2, 'WideCharToMultiByte': 8, 'MultiByteToWideChar': 6, 'DuplicateHandle': 7,
           'GetSystemInfo': 1, 'GetWriteWatch': 6, 'ResetWriteWatch': 2, 'GetTickCount': 0, 'ReleaseMutex': 1, 'WriteFile': 5,
           'ReadFile': 5, 'DebugBreak': 0, 'CreateFileA': 7, 'CreateFileW': 7, 'GetExitCodeThread': 2, 'ResumeThread': 1,
           'SuspendThread': 1, 'GetVersion': 0, 'Sleep': 1, 'GetLastError': 0, 'SetLastError': 1, 'QueryPerformanceCounter': 1,
           'QueryPerformanceFrequency': 1, 'GetProcAddress': 2, 'LoadLibraryA': 1, 'LoadLibraryW': 1, 'OutputDebugStringA': 1,
           'OutputDebugStringW': 1, 'SetEvent': 1, 'ResetEvent': 1, 'CreateEventA': 4, 'CreateEventW': 4, 'GetThreadContext': 2,
           'SetThreadContext': 2, 'GetModuleHandleA': 1, 'GetModuleHandleW': 1, 'HeapAlloc': 3, 'HeapFree': 3, 'GetProcessHeap': 0,
           'RaiseException': 4, 'FlushFileBuffers': 1, 'SetFilePointer': 4, 'GetFileSize': 2, 'timeGetTime': 0, 'GetSystemTimeAsFileTime': 1,
           'GetLocalTime': 1, 'GetSystemTime': 1, 'FileTimeToSystemTime': 2, 'SystemTimeToFileTime': 2, 'lstrlenA': 1, 'lstrlenW': 1}
# imports that write through an argument: name -> arg indexes
IMPW = {'InterlockedIncrement': (0,), 'InterlockedDecrement': (0,), 'InterlockedExchange': (0,), 'InterlockedCompareExchange': (0,),
        'InterlockedExchangeAdd': (0,), 'VirtualQuery': (1,), 'WideCharToMultiByte': (4,), 'MultiByteToWideChar': (4,),
        'DuplicateHandle': (3,), 'GetSystemInfo': (0,), 'GetWriteWatch': (3, 4), 'GetExitCodeThread': (1,), 'WriteFile': (3,),
        'ReadFile': (1, 3), 'QueryPerformanceCounter': (0,), 'QueryPerformanceFrequency': (0,), 'GetThreadContext': (1,),
        'GetSystemTimeAsFileTime': (0,), 'GetLocalTime': (0,), 'GetSystemTime': (0,), 'FileTimeToSystemTime': (1,),
        'SystemTimeToFileTime': (1,), 'InitializeCriticalSection': (0,), 'EnterCriticalSection': (0,), 'LeaveCriticalSection': (0,),
        'TryEnterCriticalSection': (0,), 'DeleteCriticalSection': (0,)}
CRT_W = {'memcpy', 'memmove', 'memset', 'strcpy', 'wcscpy', 'strncpy', 'wcsncpy', 'strcat', 'wcscat', 'sprintf', 'swprintf',
         '_snprintf', '_snwprintf', 'vsprintf', '_vsnprintf', 'wcsncat', 'memcpy_s', 'memmove_s', 'strcpy_s', 'wcscpy_s',
         'strncpy_s', 'wcsncpy_s', 'sprintf_s', 'swprintf_s', '_snprintf_s', '_vsnprintf_s', '_vswprintf_c_l', 'mbstowcs', 'wcstombs'}


def external(t):
    if t in _EXT:
        return _EXT[t]
    s = Sum()
    if t[0] == 'imp':
        nm = t[1].split('!')[1]
        s.regs = {'eax': frozenset([('R', (t[1],), 0)]), **KEEP}
        dll = t[1].split('!')[0]
        if dll.startswith('msvcr'):
            s.pop = 0
        elif dll.startswith('msvcp'):
            tail = nm.split('@@')[1] if '@@' in nm else ''
            if len(tail) > 1 and tail[1] in ('E', 'G'):  # thiscall/stdcall member: callee pops an unknown count
                s.flag(('import-pop-unknown', t[1]), 0)
            s.pop = 0
        elif nm in STDCALL:
            s.pop = 4 * STDCALL[nm]
        else:
            s.pop = 0
            s.flag(('import-pop-unknown', t[1]), 0)
        isite = IMPSITE.setdefault(nm, 0xFF000000 + len(IMPSITE))  # site of a store an import makes: names the import
        if nm in CRT_W:
            s.write(('A0', (), None), None, isite, UV)
        elif nm in IMPW:
            for i in IMPW[nm]:
                s.write(('A%d' % i, (), None), None, isite, UV)
        elif dll.startswith('msvc'):
            for i in range(4):
                s.ext[(t[1], ('A%d' % i, (), 0))] = 0
        else:
            for i in range(STDCALL.get(nm, 4)):
                s.ext[(t[1], ('A%d' % i, (), 0))] = 0
    else:
        s.regs = {'eax': UV, **KEEP}
        s.flag('indirect', 0)
        for i in range(6):
            s.ext[('indirect', ('A%d' % i, (), 0))] = 0
        s.ext[('indirect', ('Rc', (), 0))] = 0
        s.ext[('indirect-target', t[1])] = 0
    _EXT[t] = s
    return s


def summary(entry):
    if entry in MEMO:
        return MEMO[entry]
    if entry in ACTIVE or len(ACTIVE) > 60:
        s = Sum()
        s.flag(('recursion' if entry in ACTIVE else 'deep', entry), entry)
        s.trunc = True
        s.regs = {g: frozenset([(REGP[g], (), 0)]) for g in ('ebx', 'esi', 'edi', 'ebp')}
        return s
    ACTIVE.append(entry)
    s = analyze(entry)
    ACTIVE.pop()
    MEMO[entry] = s
    return s


JCC = {'je', 'jne', 'ja', 'jae', 'jb', 'jbe', 'jg', 'jge', 'jl', 'jle', 'js', 'jns', 'jo', 'jno', 'jp', 'jnp', 'jecxz', 'jcxz',
       'loop', 'loope', 'loopne'}


EXTCAP = 400


IMPSITE = {}
# EA runtime support inside the glue range: method binder 0xD9A7B0 (+ key builder 0xD9A530, registration 0xD9A860, record
# lookup 0xD9A890), native service lookup 0xD89D40, interface-offset layout 0xD790A0 (docs/130 D6), frame arena and
# context cells 0xD71900 / 0xD71A20 / 0xD71B70 / 0xD71800 (docs/118 D5)
RUNTIME_EA = {0xD9A7B0, 0xD9A530, 0xD9A860, 0xD9A890, 0xD89D40, 0xD790A0, 0xD71900, 0xD71A20, 0xD71B70, 0xD71800}
# their code ranges (entry to the int3 padding after it): 0xD9A530-0xD9A856 holds the key builder and the binder
RUNTIME_EA_RANGES = [(0xD790A0, 0xD7972C), (0xD9A530, 0xD9A856), (0xD9A860, 0xD9A88A), (0xD9A890, 0xD9A965),
                     (0xD71800, 0xD7180B), (0xD71900, 0xD71A17), (0xD71A20, 0xD71B66), (0xD71B70, 0xD71BD7), (0xD89D40, 0xD89D87)]


def region(site):
    if not site or site >= 0xFF000000:
        return 'import'
    if 0x4E0000 <= site < 0x4F0000:
        return 'alloc'  # EA heap allocator (0x4E5E40 family)
    if any(a <= site < b for a, b in RUNTIME_EA_RANGES):
        return 'runtime'  # store inside an EA runtime-support function, even when inlined into a caller's summary
    if GLUE[0] <= site < GLUE[1]:
        return 'glue'
    return 'runtime' if site >= 0xE40000 else 'engine'


def natadd(res, k, site):
    # native write samples, at most 12 per region so allocator writes cannot crowd out glue / engine writes
    rg = k.split(':', 1)[0] if ':' in k else region(site)
    k = k if ':' in k else rg + ':' + k
    if k not in res.nat and sum(1 for x in res.nat if x.startswith(rg + ':')) < 12:
        res.nat[k] = site


def analyze(entry):
    STATS['fns'] += 1
    res = Sum()
    saved = CUR[0]
    CUR[0] = res
    res.entry = entry
    r = {g: frozenset([(pn, (), 0)]) for g, pn in REGP.items()}
    r['esp'] = frozenset([('S', (), 0)])
    r['_m'] = frozenset([('S', (), 0)])
    s = {0: frozenset([('RA', (), 0)])}
    for i in range(NARGS):
        s[4 + 4 * i] = frozenset([('A%d' % i, (), 0)])
    states = {entry: St(r, s)}
    work = [entry]
    steps = 0
    rets = []
    while work:
        a = work.pop()
        st = states[a].copy()
        while True:
            steps += 1
            if steps > (BUDGET if GLUE[0] <= entry < GLUE[1] else BUDGET // 4):
                res.flag(('budget', entry), a)
                res.trunc = True
                work = []
                break
            ins = dec(a)
            if ins is None:
                res.flag('decode', a)
                break
            nxt = step(ins, st, res, entry, rets)
            if nxt is None:
                a = ins.address + ins.size
                if a in states:
                    if merge(states[a], st):
                        work.append(a)
                    break
                continue
            for t in nxt:
                if t not in states:
                    states[t] = st.copy()
                    work.append(t)
                elif merge(states[t], st):
                    work.append(t)
            break
    STATS['steps'] += steps
    res.steps = steps
    keep = {}
    for (a, size), e in res.w.items():
        if a[0] in PARAMS or a[0] in ('MV', 'U'):
            keep[(a, size)] = e
        else:
            k = ('G%X' % a[2]) if a[0] == 'G' and not a[1] and a[2] is not None else ('G%X*' % (a[1][0] or 0)) if a[0] == 'G' and a[1] else a[0] if isinstance(a[0], str) else 'R'
            natadd(res, k, e[0])
    res.w = keep
    ext = {}
    for (lab, a), site in res.ext.items():
        if lab == 'indirect-target':
            continue
        if a[0] in PARAMS or a[0] in KEEPROOT or lab in SFLAB:
            ext[(lab, a)] = site
    if len(ext) > EXTCAP:
        ext2 = {}  # coarser atoms keep 3 hops so a vtable hop (offset 0) stays visible to the classifier
        for (lab, a), site in ext.items():
            ext2.setdefault((lab if not lab.startswith('msvc') else 'crt', (a[0], a[1][:3], None if len(a[1]) > 3 else a[2])), site)
        ext = ext2
    res.ext = ext
    if len(res.w) > 60:
        w2 = {}
        for (a, size), e in res.w.items():
            k = ((a[0], a[1][:2], None), None)
            if k in w2:
                w2[k][1] = cap(w2[k][1] | e[1])
            else:
                w2[k] = [e[0], e[1]]
        res.w = w2
    regs = {}
    pops = set()
    for rg, n in rets:
        pops.add(n)
        for g in GPR:
            regs[g] = cap(regs.get(g, frozenset()) | rg.get(g, UV))
    res.regs = regs
    res.pop = max(pops) if pops else 0
    res.nargs = max(res.nargs, res.pop // 4)
    CUR[0] = saved
    if len(pops) > 1:
        res.flag(('ret-mismatch', entry), entry)
    return res


def setmark(st):
    st.r['_m'] = st.r['esp']


def step(ins, st, res, entry, rets):
    for reg in ins.rd:
        p = PARENT.get(reg)
        if p and p[0] in ('eax', 'ecx', 'edx'):
            pn = REGP[p[0]]
            if (pn, (), 0) in st.r.get(p[0], ()):
                res.used.add(pn)
    m = ins.mnemonic
    ops = ins.operands
    a = ins.address
    rep = m.startswith('rep')
    if rep and ' ' in m:
        m = m.split()[-1]
    if ops and ops[0].type == CS_OP_MEM and m != 'push' and m not in ('cmp', 'test', 'bt') and (m in WRITE0 or m.startswith('set') or ops[0].access & 2):
        size = None if rep else ops[0].size
        val = UV
        if m == 'mov' and len(ops) == 2:
            val = opval(st, ops[1])
        if m == 'pop':
            E = esp_off(st)
            val = st.s.get(E, UV) if E is not None else UV
            st.r['esp'] = addc(st.r['esp'], 4)
        store(st, ea(st, ops[0].mem), size, val, a, res)
        if m in ('xchg', 'xadd', 'cmpxchg') and len(ops) > 1 and ops[1].type == CS_OP_REG:
            setreg(st, ops[1].reg, UV)
        if m in ('stosb', 'stosw', 'stosd', 'movsb', 'movsw', 'movsd') and len(ops) >= 1 and ops[0].mem.base:
            st.r['edi'] = addc(st.r['edi'], None)
            if m.startswith('movs'):
                st.r['esi'] = addc(st.r['esi'], None)
            if rep:
                st.r['ecx'] = UV
        if m in ('cmpxchg', 'cmpxchg8b'):
            st.r['eax'] = UV
        if m == 'cmpxchg8b':
            st.r['edx'] = UV
        return None
    if ops and ops[0].type == CS_OP_REG and (m in ('mov', 'movzx', 'movsx', 'lea') or m.startswith('cmov')):
        if m == 'lea':
            v = ea(st, ops[1].mem)
        elif m == 'mov':
            v = opval(st, ops[1])
        elif m.startswith('cmov'):
            p = PARENT.get(ops[0].reg)
            v = cap(st.r.get(p[0], UV) | opval(st, ops[1])) if p and p[1] else UV
        else:
            v = UV
        setreg(st, ops[0].reg, v)
        if PARENT.get(ops[0].reg, ('',))[0] == 'esp':
            setmark(st)
        return None
    if m == 'push':
        v = opval(st, ops[0]) if ops else UV
        st.r['esp'] = addc(st.r['esp'], -4 if not ops or ops[0].size != 2 else -2)
        E = esp_off(st)
        if E is None:
            res.flag('esp-lost', a)
            return []
        st.s[E] = v
        return None
    if m == 'pop':
        E = esp_off(st)
        if E is None:
            res.flag('esp-lost', a)
            return []
        v = st.s.get(E, UV)
        st.r['esp'] = addc(st.r['esp'], 4)
        setreg(st, ops[0].reg, v)
        return None
    if m in ('pushal', 'pushad', 'pushfd', 'pushf'):
        st.r['esp'] = addc(st.r['esp'], -32 if m.startswith('pusha') else -4)
        return None
    if m in ('popal', 'popad', 'popfd', 'popf'):
        st.r['esp'] = addc(st.r['esp'], 32 if m.startswith('popa') else 4)
        if m.startswith('popa'):
            for g in GPR:
                st.r[g] = UV
        return None
    if m == 'leave':
        st.r['esp'] = st.r.get('ebp', UV)
        E = esp_off(st)
        if E is None:
            res.flag('esp-lost', a)
            return []
        st.r['ebp'] = st.s.get(E, UV)
        st.r['esp'] = addc(st.r['esp'], 4)
        setmark(st)
        return None
    if m in ('add', 'sub', 'inc', 'dec', 'and', 'xor', 'or') and ops[0].type == CS_OP_REG:
        p = PARENT.get(ops[0].reg)
        if not p or not p[1]:
            setreg(st, ops[0].reg, UV)
            return None
        cur = st.r.get(p[0], UV)
        if m in ('inc', 'dec'):
            v = addc(cur, 1 if m == 'inc' else -1)
        elif m == 'xor':
            v = frozenset([C(0)]) if ops[1].type == CS_OP_REG and ops[1].reg == ops[0].reg else UV
        elif m == 'and':
            if p[0] == 'esp' and ops[1].type == CS_OP_IMM:
                d = esp_off(st)
                v = frozenset([('S', (), d & ops[1].imm)]) if d is not None else UV
            elif is_const(cur) and ops[1].type == CS_OP_IMM:
                v = cap(C(d & ops[1].imm) for _, _, d in cur)
            elif ops[1].type == CS_OP_IMM and (ops[1].imm & 0xffffffff) >= 0xfffff000:
                v = addc(frozenset(x for x in cur if x[0] != 'G' or x[1]), None) or UV
            else:
                v = UV
        elif m == 'or':
            v = addc(frozenset(x for x in cur if x[0] != 'G' or x[1]), None) if ops[1].type == CS_OP_IMM and ops[1].imm < 0x1000 else UV
            v = v or UV
        else:
            w = opval(st, ops[1])
            if m == 'sub':
                v = cap(add1(x, -next(iter(w))[2]) for x in cur) if is_const(w) and len(w) == 1 else UV
            else:
                v = addv(cur, w)
        st.r[p[0]] = v
        if p[0] == 'esp':
            if esp_off(st) is None:
                res.flag('esp-lost', a)
                return []
            setmark(st)
        return None
    if m == 'xchg' and len(ops) == 2 and ops[0].type == CS_OP_REG and ops[1].type == CS_OP_REG:
        p0, p1 = PARENT.get(ops[0].reg), PARENT.get(ops[1].reg)
        if p0 and p1 and p0[1] and p1[1]:
            st.r[p0[0]], st.r[p1[0]] = st.r.get(p1[0], UV), st.r.get(p0[0], UV)
        else:
            setreg(st, ops[0].reg, UV)
            setreg(st, ops[1].reg, UV)
        return None
    if m == 'call' and ops[0].type == CS_OP_IMM and ops[0].imm in PROBES:
        ev = st.r.get('eax', UV)
        E = esp_off(st)
        if not is_const(ev) or len(ev) != 1 or E is None:
            res.flag('esp-lost', a)
            return []
        al = PROBES[ops[0].imm]
        n = (next(iter(ev))[2] + al - 1) // al * al
        st.r['esp'] = frozenset([('S', (), E - n)])
        st.r['eax'] = UV
        setmark(st)
        return None
    if m == 'call':
        tv = opval(st, ops[0])
        if not call(st, ins, tv, res):
            return []
        return None
    if m == 'ret':
        E = esp_off(st)
        if E != 0:
            res.flag(('esp-at-ret', E), a)
        rets.append((dict(st.r), ops[0].imm if ops else 0))
        return []
    if m == 'jmp':
        if ops[0].type == CS_OP_IMM:
            t = ops[0].imm
            if abs(t - entry) < 0x8000 and t != entry and not (t in MEMO or t in SPECIAL):
                return [t]
            tv = frozenset([C(t)])
        else:
            mm = ops[0].mem if ops[0].type == CS_OP_MEM else None
            if mm is not None and mm.index and not mm.base and sec_of(mm.disp) in ('.text', '.rdata'):
                out = []
                for k in range(512):
                    t = u32(mm.disp + 4 * k)
                    if sec_of(t) != '.text' or abs(t - a) > 0x10000:
                        break
                    out.append(t)
                if out:
                    return sorted(set(out))
            tv = opval(st, ops[0])
        # tail call: the callee returns to our caller
        st.r['esp'] = addc(st.r['esp'], 4)
        if not call(st, ins, tv, res):
            return []
        rets.append((dict(st.r), 0))
        return []
    if m in JCC:
        return [ins.address + ins.size, ops[0].imm] if ops and ops[0].type == CS_OP_IMM else [ins.address + ins.size]
    if m in ('int3', 'hlt', 'ud2', 'int'):
        return []
    for reg in ins.wr:
        p = PARENT.get(reg)
        if p and p[0] != 'esp':
            st.r[p[0]] = UV
        elif p and p[0] == 'esp':
            res.flag(('esp-generic', m), a)
    for o in ops:
        if o.type == CS_OP_MEM and o.access & 2:
            store(st, ea(st, o.mem), o.size, UV, a, res)
    return None


def fixed(ret=None, flag=None):
    s = Sum()
    s.regs = {g: frozenset([(REGP[g], (), 0)]) for g in ('ebx', 'esi', 'edi', 'ebp')}
    s.regs['eax'] = frozenset([ret]) if ret else UV
    if flag:
        s.flag(flag, 0)
        for i in range(8):
            s.ext[('managed:' + flag[1], ('A%d' % i, (), 0))] = 0
    return s


ALLOC = {0xE4BE30: 'GC_malloc', 0xE4BDC0: 'GC_malloc_atomic', 0xE4BF90: 'GC_malloc_uncollectable', 0xE50DE0: 'GC_malloc_explicitly_typed',
         0xE4BCE0: 'GC_generic_malloc', 0xEA36F0: 'mono_object_new', 0xEA40B0: 'mono_string_new', 0xEA30F0: 'mono_array_new',
         0xEA3140: 'mono_array_new_specific?'}
for addr in ALLOC:
    SPECIAL[addr] = fixed(('N', (), 0))
SPECIAL[0xEA2AD0] = fixed(('MV', (), 0))
MANAGED = {0xD8A8E0: 'ScriptHost_NativeToManaged', 0xE53EB0: 'ves_exec_method_with_context', 0xE59FD0: 'interp_mono_invoke_with_state',
           0xE5A600: 'interp_runtime_invoke', 0xEA1090: 'mono_runtime_invoke', 0xEA1400: 'mono_runtime_delegate_invoke',
           0xEA3370: 'mono_runtime_class_init'}  # class init runs the type's .cctor (managed code)
for addr, nm in MANAGED.items():
    SPECIAL[addr] = fixed(None, ('managed', nm))


# docs/122 I-122-26 markers: the mono_get_exception_* block and mono_raise_exception 0xEA18F0
THROW = (0xE77000, 0xE77600)
THROWS = fixed(('N', (), 0), ('throws', 'mono_exception'))
THROWS.ext = {}


# EA static-field helpers (docs/130 static-field helper row): get value 0xD89CD0(field, out) copies the static into *out
# through 0xE521A0; set value 0xD89BF0(field, value) stores through 0xE52350 at vtable->data + field->offset; get address
# 0xD89C60(field). Modelled exactly so a static read is not a write and a static write names its MonoClassField* global.
SFLAB = ('static-get', 'static-set', 'static-addr')


def sfield(kind):
    s = fixed(('MV', (12,), None) if kind == 'static-addr' else None)
    s.ext[(kind, ('A0', (), 0))] = 0
    s.ext[('managed:mono_runtime_class_init', ('A0', (), 0))] = 0  # first use runs the class's .cctor
    if kind == 'static-get':
        s.write(('A1', (), None), None, 0xD89D30, UV)
    if kind == 'static-set':
        s.write(('MV', (12,), None), None, 0xD89C50, UV)
    return s


SPECIAL[0xD89CD0] = sfield('static-get')
SPECIAL[0xD89BF0] = sfield('static-set')
SPECIAL[0xD89C60] = sfield('static-addr')


def regs():
    out, pushes = [], []
    for ins in md.disasm(read(0xE3E570, 0x10000), 0xE3E570):
        if ins.mnemonic == 'push':
            try:
                pushes.append(int(ins.op_str, 16))
            except ValueError:
                pushes.append(None)
        elif ins.mnemonic == 'call':
            if ins.op_str == '0xd9a860':
                out.append((pushes[-1], pushes[-2], pushes[-3], pushes[-4]))
            pushes = []
        elif ins.mnemonic == 'ret':
            break
    return out


def top(f1):
    s = summary(f1)
    env = {'A0': frozenset([('F', (), 0)]), 'A1': frozenset([('X', (), 0)])}
    for g, pn in REGP.items():
        env[pn] = UV
    for i in range(2, NARGS):
        env['A%d' % i] = UV
    w = {}
    for (a, size), (site, val) in s.w.items():
        for a2 in sub_atom(a, env, {}):
            w.setdefault((a2, size), site)
    ext = {}
    for (lab, a), site in s.ext.items():
        if lab == 'indirect-target':
            continue
        for a2 in sub_atom(a, env, {}):
            if a2[0] in KEEPROOT:
                ext.setdefault((lab, a2), site)
    return w, dict(s.flags), s.nat, ext


def store_path(fn, atom, depth=16):
    """functions a store passed through, from the adapter down to the one that made it (first path recorded)"""
    out = []
    for _ in range(depth):
        e = PROV.get((fn, 'w', atom))
        if e is None or not isinstance(e[0], int) or not isinstance(e[1], tuple):
            break
        out.append(e[0])
        fn, atom = e[0], e[1]
    return out


def analyse(pick=None):
    R = regs()
    out = []
    for i in (pick if pick is not None else range(len(R))):
        h, f1, f2, f3 = R[i]
        w, fl, nat, ext = top(f1)
        s = summary(f1)
        out.append({'i': i, 'hash': '%08x' % h, 'adapter': f1, 'body': f2, 'init': f3, 'nat': nat,
                    'ext': [[lab, repr(a), site] for (lab, a), site in ext.items()],
                    'w': [[repr(a), size, site] for (a, size), site in w.items()],
                    'wchain': sorted({'%X' % x for (a, size) in s.w if a[0] == 'U' for x in store_path(f1, a)}),
                    'flags': [[repr(k), site] for k, site in fl.items()]})
    return {'bodies': out, 'imports': {'%d' % v: k for k, v in IMPSITE.items()}}


# ---------- classification (managed facts from tools/binder-bodies.ts) ----------
TY = {}
BINDG = (0x11EE000, 0x11F3000)  # binder and init globals (docs/130: MonoClassField* globals 0x11EE650-0x11F2D60)
NAMED = {0x11F2D84: 'IC nesting counter (docs/122)', 0x11CB920: 'temp string refcount (docs/131)'}


def kind_of(t):
    if t.endswith('&'):
        return 'byref'
    if t.endswith('*') or t in ('System.IntPtr', 'System.UIntPtr', 'IntPtr', 'UIntPtr'):
        return 'native'
    if t.endswith(']'):
        return 'array'
    if t in ('string', 'System.String'):
        return 'string'
    if t in ('object', 'System.Object'):
        return 'object'
    lay = TY.get(t)
    return lay['kind'] if lay else 'unknown'


def field_at(t, off):
    lay = TY.get(t)
    if not lay or off is None:
        return None
    for f in lay['fields']:
        if f['off'] is not None and f['off'] <= off < f['off'] + (f['size'] or 4):
            return f
    return None


def short(t):
    return t.rsplit('.', 1)[-1]


def obj(t, chain, add, size, via):
    """Write target inside the object of declared type t (pointer = object start), reached by `chain` loads then +add."""
    k = kind_of(t)
    if chain:
        nxt, rest = chain[0], chain[1:]
        if k in ('class', 'string', 'array', 'object', 'unknown') and nxt == 0:
            return 'meta', 'vtable of %s' % via  # a load at offset 0 of an object is its vtable: runtime metadata
        if nxt is None:
            return 'other', '%s: object reached through an unknown field' % via
        if k == 'array':
            el = t[:t.rindex('[')]
            if nxt >= 0xC and kind_of(el) in ('class', 'string', 'array', 'object'):
                return obj(el, rest, add, size, via + '[]')
            return 'unknown', '%s: load from array header %d' % (via, nxt)
        f = field_at(t, nxt) if k in ('class', 'struct') else None
        if f is None:
            return 'unknown', '%s: load at +%d of %s' % (via, nxt, short(t))
        fk = f['kind']
        if fk == 'native':
            return 'native', '%s.%s (IntPtr target)' % (via, f['name'])
        if fk in ('class', 'string', 'array', 'object'):
            return obj(f['t'], rest, add, size, '%s.%s' % (via, f['name']))
        return 'unknown', '%s.%s: load of a %s field' % (via, f['name'], fk)
    if add is None:
        return 'other', '%s (%s, unknown offset)' % (via, short(t))
    if k == 'array':
        return ('other' if add >= 0xC else 'meta'), ('%s elements' % via if add >= 0xC else '%s header +%d' % (via, add))
    if k == 'string':
        return 'other', '%s chars in place' % via
    if add < 4 and k != 'struct':
        return 'meta', '%s header' % via
    f = field_at(t, add)
    return 'other', '%s.%s' % (via, f['name'] if f else '+%d' % add)


def param_target(p, via, chain, add, size):
    t, k = p['t'], p['kind']
    if k == 'byref':
        base = t[:-1]
        if not chain:
            return 'args', '%s:%s' % (via, size if size else '?')
        bk = kind_of(base)
        if bk in ('class', 'string', 'array', 'object'):
            return obj(base, chain[1:], add, size, '*' + via) if chain[0] == 0 else ('unknown', '%s: byref +%s' % (via, chain[0]))
        return 'unknown', '%s: load through a byref %s' % (via, short(base))
    if k == 'array':
        if not chain and add is not None and add >= 0xC:
            return 'args', '%s[]:%s' % (via, size if size else '?')
        return obj(t, chain, add, size, via)
    if k in ('class', 'string', 'object'):
        return obj(t, chain, add, size, via)
    if k == 'struct':
        if not chain:
            return 'interp', '%s copy' % via  # by-value struct: the stackval points at the interpreter's copy
        f = field_at(t, chain[0])
        if f and f['kind'] in ('class', 'string', 'array', 'object'):
            return obj(f['t'], chain[1:], add, size, '%s.%s' % (via, f['name']))
        if f and f['kind'] == 'native':
            return 'native', '%s.%s (IntPtr target)' % (via, f['name'])
        return 'unknown', '%s: load in a struct' % via
    if k == 'native':
        return 'native', '%s target' % via
    return 'unknown', '%s (%s) used as a pointer' % (via, k)


def target(a, size, m):
    r, c, add = a
    if r == 'F':
        if not c:
            return ('throws', 'pending exception') if add == 0x20 else ('interp', 'frame')
        if c[0] == 8:
            return 'interp', 'return slot'
        if c[0] == 12:
            s = m['self']
            if not s:
                return 'unknown', 'this slot of a static method'
            if s['kind'] == 'struct':
                if len(c) == 1:
                    return 'args', 'this:%s' % (size if size else '?')
                return param_target(s, 'this', c[1:], add, size)
            return obj(s['t'], c[1:], add, size, 'this')
        if c[0] == 20:
            if len(c) == 1:
                return 'interp', 'argument slot'
            if c[1] is None:
                return 'unknown', 'unknown argument'
            i = c[1] // 8
            if i >= len(m['params']):
                return 'unknown', 'argument %d beyond the signature' % i
            return param_target(m['params'][i], 'arg%d' % i, c[2:], add, size)
        return 'interp', 'frame +%s' % c[0]
    if r == 'X':
        return 'interp', 'ThreadContext'  # per script thread (docs/57): the worker has its own
    if r == 'MV':
        if c and c[0] == 12:
            return 'static', 'static field data'
        return 'meta', 'vtable'
    if r == 'N':
        return 'fresh', 'new object'
    if r == 'U':
        return 'unknown', 'unresolved pointer'
    return 'native', r


def hint(r, m, cls):
    """unknown rows: an unresolved store on the path of a body whose managed method builds a string it allocated [inferred]"""
    if cls != 'unknown':
        return ''
    if m.get('il_alloc_str'):
        return 'copy into the string this method allocates (IL: InternalAllocateStr) [inferred]'
    for x in r.get('wchain', ()):
        o = BODY.get(int(x, 16))
        if o and o != r['hash'] and M.get(o, {}).get('il_alloc_str'):
            return 'copy into the string %s allocates (callee body 0x%s) [inferred]' % (M[o]['id'], x)
    return ''


BODY, M = {}, {}


def classify(J, F):
    global TY, IMPS
    d, IMPS = J['bodies'], {int(k): v for k, v in J['imports'].items()}
    M.clear()
    M.update(F['methods'])
    TY = F['types']
    BODY.clear()
    BODY.update({r['body']: r['hash'] for r in d})
    ROWS = []
    for r in d:
        idx, h = r['i'], r['hash']
        m = M[h]
        cats = collections.defaultdict(set)
        for a, size, site in r['w']:
            a = ast.literal_eval(a)
            cat, det = target(a, size, m)
            if cat == 'unknown' and not (GLUE[0] <= site < GLUE[1]):
                cat = 'runtime'  # unresolved store inside Mono / GC / EA heap code: bookkeeping on runtime-owned memory
                det = 'unresolved store at 0x%X' % site
            cats[cat].add(det)
        for k, site in r['nat'].items():
            rg, key = k.split(':', 1)
            if rg in ('runtime', 'alloc'):
                cats['runtime'].add('heap' if rg == 'alloc' else 'runtime')
                continue
            if rg == 'import' and site in IMPS:
                rg = IMPS[site]
            if key == 'N':
                cats['fresh'].add('new native or managed block')
                continue
            g = key[1:].rstrip('*')
            ga = int(g, 16) if key[0] == 'G' and g and all(ch in '0123456789ABCDEF' for ch in g) else None
            if ga in NAMED and not key.endswith('*'):
                cats['native'].add(NAMED[ga])
            elif ga is not None and key.endswith('*') and BINDG[0] <= ga < BINDG[1]:
                # through a binder init global (MonoMethod*, MonoClassField*, call records: docs/130): runtime metadata
                cats['runtime'].add('binder record')
                continue
            elif ga is None or ga < 0x10000 or ga >= 0xFFFF0000:
                cats['unresolved'].add('%s %s' % (rg, key))  # pointer the analysis lost (small constant or unknown base)
                continue
            else:
                cats['native'].add('%s %s' % (rg, key))  # EA native global (G<addr>) or memory reached from it (G<addr>*)
        managed = sorted({lab.split(':', 1)[1] if ':' in lab else lab for lab, a, s in r['ext'] if lab.startswith('managed:')})
        hand = collections.Counter()
        for lab, a, s in r['ext']:
            if lab.startswith('managed:') or lab == 'indirect-target':
                continue
            cat, det = target(ast.literal_eval(a), None, m)
            if cat in ('other', 'args', 'static'):
                hand[lab.split('!')[-1] if '!' in lab else lab] += 1
        throws = any('throws' in f[0] for f in r['flags'])
        trunc = any('budget' in f[0] or 'recursion' in f[0] for f in r['flags'])
        if cats.get('unknown'):
            cls = 'unknown'
        elif cats.get('other') or cats.get('static'):
            cls = 'writes-other'
        elif cats.get('args'):
            cls = 'writes-args'
        elif cats.get('native'):
            cls = 'native-only'
        else:
            cls = 'reader'
        conf = 'high'
        if hand or trunc or cats.get('unresolved'):
            conf = 'medium'
        if cls == 'unknown':
            conf = 'low'
        ROWS.append({'index': idx, 'address': '0x%X' % r['body'], 'adapter': '0x%X' % r['adapter'], 'hash': h,
                     'managed_signature': m['id'], 'class': cls,
                     'writes_args': ';'.join(sorted(cats.get('args', ()))),
                     'writes_other': ';'.join(sorted(cats.get('other', set()) | cats.get('static', set()))),
                     'writes_native': ';'.join(sorted(cats.get('native', ()))),
                     'runtime_writes': ';'.join(sorted(cats.get('runtime', ()))),
                     'unknown': ';'.join(sorted(cats.get('unknown', ()))),
                     'unknown_hint': hint(r, m, cls),
                     'unresolved_native': ';'.join(sorted(cats.get('unresolved', ()))),
                     'calls_managed': ';'.join(managed), 'opaque_handoffs': ';'.join('%s:%d' % x for x in sorted(hand.items())),
                     'throws': 'yes' if throws else '', 'reachable_from_worker': m['reach'], 'il_writes': ';'.join(m['il_fw'] + m['il_sw'])[:300],
                     'confidence': conf})
    return ROWS


def self_check(exe):
    # classifier on a fixture: one row per class, the field layout and the unknown-row hint
    F = {'types': {'T': {'kind': 'class', 'size': 24, 'align': 4,
                         'fields': [{'name': 'm_x', 't': 'float', 'kind': 'scalar', 'off': 20, 'size': 4}]}},
         'methods': {}}

    def meth(h, id, self=None, params=(), alloc=False):
        F['methods'][h] = {'id': id, 'self': self, 'params': list(params), 'reach': 'v0', 'il_fw': [], 'il_sw': [],
                           'il_alloc_str': alloc}

    def body(h, i, w=(), nat=None, path=()):
        return {'i': i, 'hash': h, 'adapter': 0xE00000 + i, 'body': 0xE10000 + i, 'init': 0, 'nat': nat or {}, 'ext': [],
                'w': [list(x) for x in w] + [["('F', (8,), 0)", 4, 0xE10000]], 'wchain': list(path), 'flags': []}

    meth('00000001', 'T::Ref(float&)', params=[{'t': 'float&', 'kind': 'byref'}])
    meth('00000002', 'T::Fx(float)', self={'t': 'T', 'kind': 'class'}, params=[{'t': 'float', 'kind': 'scalar'}])
    meth('00000003', 'T::Get()')
    meth('00000004', 'T::Name()')
    meth('00000005', 'System.String::Concat(string,string)', alloc=True)
    meth('00000006', 'T::Tick()')
    J = {'imports': {}, 'bodies': [
        body('00000001', 0, [["('F', (20, 0), 0)", 4, 0xE10010]]),
        body('00000002', 1, [["('F', (12,), 20)", 4, 0xE10020]]),
        body('00000003', 2),
        body('00000004', 3, [["('U', (), None)", None, 0xDC0316]], path=['E10004']),
        body('00000005', 4),
        body('00000006', 5, nat={'glue:G1234567': 0xE10060, 'runtime:G4*': 0xE48552})]}
    got = {r['hash']: r for r in classify(J, F)}
    want = {'00000001': ('writes-args', 'arg0:4', ''), '00000002': ('writes-other', '', 'this.m_x'),
            '00000003': ('reader', '', ''), '00000004': ('unknown', '', ''), '00000005': ('reader', '', ''),
            '00000006': ('native-only', '', '')}
    for h, (cls, wa, wo) in want.items():
        r = got[h]
        assert (r['class'], r['writes_args'], r['writes_other']) == (cls, wa, wo), (h, r)
    assert 'String::Concat' in got['00000004']['unknown_hint'], got['00000004']
    assert got['00000006']['writes_native'] == 'glue G1234567' and got['00000006']['runtime_writes'] == 'runtime', got['00000006']
    assert got['00000004']['confidence'] == 'low' and got['00000003']['confidence'] == 'high'
    print('classifier fixture: 6 rows ok')
    if not os.path.exists(exe):
        print('exe not found (%s): analyzer checks skipped' % exe)
        return 0
    # analyzer on known bodies (docs/217 §4): ElapsedTimeInMinutes(ref float) writes the float, Curve.Fx writes this.m_x
    # (+0x14), PriorityQueue.Add writes this fields, SimClock.get_CurrentTicks only fills its return slot
    load_exe(exe)
    R = regs()
    at = {'%08x' % r[0]: i for i, r in enumerate(R)}
    expect = {'8cc00867': "('F', (20, 0), 0)", 'aee09772': "('F', (12,), 20)", 'd576215f': "('F', (12,), 12)"}
    picks = list(expect) + ['5a1c8308']
    assert len(R) == 1085 and all(h in at for h in picks), len(R)
    out = {x['hash']: x for x in analyse([at[h] for h in picks])['bodies']}
    for h, atom in expect.items():
        assert any(w[0] == atom for w in out[h]['w']), (h, out[h]['w'])
    assert all(w[0].startswith("('F', ()") or w[0].startswith("('F', (8,)") for w in out['5a1c8308']['w']), out['5a1c8308']['w']
    print('analyzer: 1085 registrations, 4 known bodies ok')
    return 0


def main():
    ap = argparse.ArgumentParser(description='docs/217: what EA binder native bodies write')
    ap.add_argument('--exe', default=os.environ.get('TS3W_EXE') or os.path.join(REPO, 'game-copy', 'bin', 'TS3W.exe'))
    ap.add_argument('--hashes', help='write the registrations (hash, adapter, body, init) for binder-bodies.ts and stop')
    ap.add_argument('--facts', help='managed facts from tools/binder-bodies.ts')
    ap.add_argument('--csv', default=os.path.join(REPO, 'docs', 'data', 'binder-bodies.csv'))
    ap.add_argument('--json', help='also write the raw store summaries')
    ap.add_argument('--self-check', action='store_true')
    a = ap.parse_args()
    if a.self_check:
        return self_check(a.exe)
    if not (a.hashes or a.facts):
        ap.error('give --hashes or --facts')
    load_exe(a.exe)
    if a.hashes:
        R = regs()
        with open(a.hashes, 'w', newline='\n') as f:
            for h, f1, f2, f3 in R:
                f.write('%08x\t0x%X\t0x%X\t0x%X\n' % (h, f1, f2, f3))
        print('%d registrations -> %s' % (len(R), a.hashes), file=sys.stderr)
        return 0
    t0 = time.time()
    J = analyse()
    print('%d bodies analysed in %.0f s (%d functions)' % (len(J['bodies']), time.time() - t0, STATS['fns']), file=sys.stderr)
    if a.json:
        json.dump(J, open(a.json, 'w'), indent=0)
    rows = classify(J, json.load(open(a.facts, encoding='utf-8')))
    with open(a.csv, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()), lineterminator='\n')
        w.writeheader()
        w.writerows(rows)
    print('classes', dict(collections.Counter(r['class'] for r in rows)), file=sys.stderr)
    print('confidence', dict(collections.Counter(r['confidence'] for r in rows)), file=sys.stderr)
    print('worker-reachable', dict(collections.Counter(r['class'] for r in rows if r['reachable_from_worker'] != 'no')),
          file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
