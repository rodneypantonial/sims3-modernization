"""docs/120 §10 (I-120-8): static audit of the 17 P3 gated-lock entries (native/ts3native/monothread-m5.cpp kMtSites).

Per entry, on the Steam TS3W.exe: recursive-descent disassembly of the entry function (Ghidra bounds from
game-copy/analysis/functions.csv), its exits (ret / ret N / tail jmp / indirect jmp), reads of its own return slot
([esp+off] at the tracked stack offset, [ebp+4] after push ebp/mov ebp,esp, or a copy of esp), SEH frames (fs:[0]),
and the direct-call closure (depth 6): SEH installs, unwind/raise/longjmp imports (RtlUnwind, RaiseException,
longjmp, ...), process-exit imports, indirect calls (unknown callees) and the pointer slots they go through, with the GC_oom_fn slot named. Prints one block per entry. Static only.

Usage: python -I tools/p3-entry-audit.py <repo>/game-copy/bin/TS3W.exe <repo>/game-copy/analysis/functions.csv
"""
import bisect, csv, struct, sys
import capstone
from capstone import x86_const as X

ENTRIES = [  # monothread-m5.cpp kMtSites order
    (0xE4BE30, 'GC_malloc'), (0xE4BDC0, 'GC_malloc_atomic'), (0xE4BF90, 'GC_malloc_uncollectable'),
    (0xE50DE0, 'GC_malloc_explicitly_typed'), (0xE4BCE0, 'GC_generic_malloc'), (0xE4A050, 'GC_try_to_collect'),
    (0xE4A0B0, 'GC_gcollect'), (0xE4E450, 'GC_enable_incremental'), (0xE4AAC0, 'register disappearing link'),
    (0xE4ABE0, 'unregister disappearing link'), (0xE4AE30, 'register finalizer'), (0xE4A3F0, 'GC_apply_to_each_object'),
    (0xE4BEB0, 'GC_free'), (0xD71900, 'EA frame arena grow'), (0xD71A20, 'EA frame arena pop'),
    (0xD71B70, 'EA frame arena release all'), (0xE4D9F0, 'GC_add_roots'),
]
UNWIND = ('RtlUnwind', 'RaiseException', 'longjmp', '_longjmp', 'setjmp', '_setjmp', '_setjmp3', '_CxxThrowException',
          'SetUnhandledExceptionFilter', 'AddVectoredExceptionHandler', 'RtlUnwindEx', 'RtlRaiseException')
EXIT = ('abort', 'exit', '_exit', 'ExitProcess', 'TerminateProcess', 'FatalAppExitA', 'ExitThread')

exe, fcsv = sys.argv[1], sys.argv[2]
buf = open(exe, 'rb').read()
pe = struct.unpack_from('<I', buf, 0x3C)[0]
nsec, optsz = struct.unpack_from('<HH', buf, pe + 6)[0], struct.unpack_from('<H', buf, pe + 20)[0]
opt = pe + 24
base = struct.unpack_from('<I', buf, opt + 28)[0]
secs = []
for i in range(nsec):
    s = opt + optsz + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', buf, s + 8)
    secs.append((base + va, max(vs, rs), ro, rs))

def rd(va, n):
    for sva, sz, ro, rs in secs:
        if sva <= va < sva + sz:
            off = va - sva
            return buf[ro + off: ro + min(off + n, rs)].ljust(n, b'\0')
    return None

# imports: IAT slot VA -> name
imp = {}
idir = struct.unpack_from('<I', buf, opt + 96 + 8)[0]
p = idir
while True:
    d = rd(base + p, 20)
    oft, _, _, name_rva, ft = struct.unpack('<IIIII', d)
    if not name_rva:
        break
    k = 0
    while True:
        thunk = struct.unpack('<I', rd(base + (oft or ft) + 4 * k, 4))[0]
        if not thunk:
            break
        if not thunk & 0x80000000:
            nm = rd(base + thunk + 2, 64).split(b'\0')[0].decode()
            imp[base + ft + 4 * k] = nm
        k += 1
    p += 20

starts, ends, names = [], {}, {}
for r in csv.DictReader(open(fcsv)):
    a = int(r['entry'], 16); starts.append(a); ends[a] = int(r['end'], 16); names[a] = r['name']
starts.sort()
def func_of(a):
    i = bisect.bisect_right(starts, a) - 1
    return starts[i] if i >= 0 and a < ends[starts[i]] else None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True

def mem_ops(ins):
    return [o for o in ins.operands if o.type == X.X86_OP_MEM]

def scan(entry, track_ret):
    """Recursive descent over one function. Returns facts."""
    lo, hi = entry, ends.get(entry, entry + 0x400)
    seen = {}; work = [(entry, 0, False)]  # addr, esp offset from entry (bytes pushed since entry), ebp=frame
    f = dict(calls=set(), icalls=[], exits=[], tails=[], ijmps=[], retreads=[], seh=[], imports=set(), unknown_off=False)
    while work:
        a, off, frame = work.pop()
        while True:
            if a in seen:
                break
            seen[a] = off
            code = rd(a, 16)
            if code is None:
                break
            try:
                ins = next(md.disasm(code, a))
            except StopIteration:
                break
            m, ops = ins.mnemonic, ins.operands
            # SEH: any fs:[0] reference
            for o in mem_ops(ins):
                if o.mem.segment == X.X86_REG_FS and o.mem.disp == 0 and o.mem.base == 0:
                    f['seh'].append(f'{a:#x} {m} {ins.op_str}')
                if track_ret and off is not None:
                    if o.mem.base == X.X86_REG_ESP and o.mem.disp == off and m not in ('call',):
                        f['retreads'].append(f'{a:#x} {m} {ins.op_str} (return slot at esp+{off:#x})')
                    if frame and o.mem.base == X.X86_REG_EBP and o.mem.disp == 4:
                        f['retreads'].append(f'{a:#x} {m} {ins.op_str} ([ebp+4])')
            if track_ret and m == 'mov' and len(ops) == 2 and ops[1].type == X.X86_OP_REG and ops[1].reg == X.X86_REG_ESP \
                    and not (ops[0].type == X.X86_OP_REG and ops[0].reg == X.X86_REG_EBP):
                f['retreads'].append(f'{a:#x} {m} {ins.op_str} (copy of esp: check)')
            # stack tracking (only for the entry itself)
            nxt_off = off
            if off is not None:
                if m == 'push':
                    nxt_off = off + 4
                elif m == 'pop':
                    nxt_off = off - 4
                    if ops[0].type == X.X86_OP_REG and ops[0].reg == X.X86_REG_EBP:
                        frame = False
                elif m in ('sub', 'add') and ops[0].type == X.X86_OP_REG and ops[0].reg == X.X86_REG_ESP:
                    nxt_off = off + ops[1].imm if (m == 'sub' and ops[1].type == X.X86_OP_IMM) else off - ops[1].imm if ops[1].type == X.X86_OP_IMM else None
                elif m == 'mov' and ops[0].type == X.X86_OP_REG and ops[0].reg == X.X86_REG_EBP and ops[1].type == X.X86_OP_REG and ops[1].reg == X.X86_REG_ESP:
                    frame = True
                elif m in ('mov', 'lea') and ops[0].type == X.X86_OP_REG and ops[0].reg == X.X86_REG_ESP:
                    nxt_off = None  # esp rebuilt (leave/mov esp,ebp): offset known again only at ret
                elif m == 'leave':
                    nxt_off = None; frame = False
            if m == 'call':
                if ops[0].type == X.X86_OP_IMM:
                    f['calls'].add(ops[0].imm)
                elif ops[0].type == X.X86_OP_MEM and ops[0].mem.base == 0 and ops[0].mem.index == 0 and ops[0].mem.disp in imp:
                    f['imports'].add(imp[ops[0].mem.disp])
                else:
                    f['icalls'].append(f'{a:#x} call {ins.op_str}')
            if m in ('ret', 'retn'):
                f['exits'].append(f'{a:#x} {m} {ins.op_str}'.strip()); break
            if m == 'jmp':
                if ops[0].type == X.X86_OP_IMM:
                    t = ops[0].imm
                    if lo <= t < hi:
                        a, off = t, nxt_off; continue
                    f['tails'].append(f'{a:#x} jmp {t:#x} {names.get(func_of(t) or t, "?")}'); f['calls'].add(t); break
                if ops[0].type == X.X86_OP_MEM and ops[0].mem.base == 0 and ops[0].mem.index == 0 and ops[0].mem.disp in imp:
                    f['imports'].add(imp[ops[0].mem.disp]); f['tails'].append(f'{a:#x} jmp [{imp[ops[0].mem.disp]}]'); break
                f['ijmps'].append(f'{a:#x} jmp {ins.op_str}'); break
            if m.startswith('j') or m.startswith('loop'):
                if ops[0].type == X.X86_OP_IMM and lo <= ops[0].imm < hi:
                    work.append((ops[0].imm, nxt_off, frame))
            if m in ('int3', 'hlt', 'ud2'):
                break
            a, off = a + ins.size, nxt_off
    return f

for entry, label in ENTRIES:
    f = scan(entry, True)
    # direct-call closure, depth 6
    seen, front, depth = {entry}, set(f['calls']), 0
    clo = dict(seh=[], imports=set(), icalls=0, icall_sites=[], slots={})
    for x in f['icalls']:
        if 'ptr [0x' in x: clo['slots'].setdefault(x.split('[')[1].rstrip(']'), set()).add(names.get(entry, hex(entry)))
    while front and depth < 6:
        nxt = set()
        for t in front:
            s = func_of(t) or t
            if s in seen:
                continue
            seen.add(s)
            g = scan(s, False)
            if g['seh']:
                clo['seh'].append(f'{names.get(s, hex(s))}: {g["seh"][0]}')
            clo['imports'] |= g['imports']
            if g['icalls'] or g['ijmps']:
                clo['icalls'] += len(g['icalls']) + len(g['ijmps'])
                clo['icall_sites'].append(f'{names.get(s, hex(s))}({len(g["icalls"]) + len(g["ijmps"])})')
                for x in g['icalls'] + g['ijmps']:
                    if 'ptr [0x' in x: clo['slots'].setdefault(x.split('[')[1].rstrip(']'), set()).add(names.get(s, hex(s)))
            nxt |= g['calls']
        front, depth = nxt, depth + 1
    bad = sorted(i for i in clo['imports'] | f['imports'] if i in UNWIND)
    ex = sorted(i for i in clo['imports'] | f['imports'] if i in EXIT)
    print(f'== {entry:#x} {label} ({names.get(entry, "?")}, end {ends.get(entry, 0):#x})')
    print(f'  exits: {", ".join(f["exits"]) or "-"}')
    print(f'  tail jumps: {", ".join(f["tails"]) or "-"}; indirect jmps: {", ".join(f["ijmps"]) or "-"}')
    print(f'  own SEH: {", ".join(f["seh"]) or "-"}; return-slot reads: {", ".join(f["retreads"]) or "-"}')
    for t in sorted({int(x.split()[2], 16) for x in f['tails'] if x.split()[2].startswith('0x')}):
        g = scan(t, True)  # a tail-jump target returns to our thunk: it must not read that slot either
        print(f'  tail target {t:#x}: exits {", ".join(g["exits"]) or "-"}; own SEH {", ".join(g["seh"]) or "-"}; return-slot reads {", ".join(g["retreads"]) or "-"}; tails {", ".join(g["tails"]) or "-"}')
    print(f'  own indirect calls: {", ".join(f["icalls"]) or "-"}; own imports: {", ".join(sorted(f["imports"])) or "-"}')
    print(f'  closure: {len(seen)} functions (direct calls, depth 6); SEH installs: {"; ".join(clo["seh"]) or "-"}')
    print(f'  closure unwind/raise imports: {", ".join(bad) or "-"}; exit imports: {", ".join(ex) or "-"}')
    oom = clo['slots'].get('0x11949bc')
    print(f'  GC_oom_fn [0x11949bc] call (runtime value 0xE78610 -> mono_raise_exception 0xEA18F0 -> interp handler 0xE53740 -> longjmp): {", ".join(sorted(oom)) if oom else "not reached"}')
    print(f'  pointer slots called: {"; ".join(f"[{k}] in {chr(44).join(sorted(v))}" for k, v in sorted(clo["slots"].items())) or "-"}')
    print(f'  closure indirect calls: {clo["icalls"]} in {", ".join(clo["icall_sites"][:12]) or "-"}{" ..." if len(clo["icall_sites"]) > 12 else ""}')
