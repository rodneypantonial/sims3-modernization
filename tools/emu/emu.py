"""Offline x86-32 harness: run single functions of TS3W.exe (Steam 1.67) under Unicorn.

Maps the PE sections at their image base (the exe has no relocations), a stack, a
scratch heap, a TEB page for fs:, and a trap page for imports. Calls one function
with a calling convention, stops at a sentinel return address and reports EAX/EDX/ST0,
basic-block coverage of the target (and of callees the spec lets it run into),
which callees were stubbed, and which image globals were read or written.

Default policy is strict: an unstubbed import, a jump into code outside the target and
its allowed callees, an unmapped access, a write into .text/.rdata, a stack imbalance or
a changed callee-saved register fails the call with the address. See docs/63.
"""
import bisect
import csv
import math
import os
import struct
from dataclasses import dataclass, field

import capstone
from capstone import x86_const as cx
from unicorn import (UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_INVALID,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_PROT_ALL, Uc, UcError)
from unicorn import x86_const as ux

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_EXE = os.environ.get('TS3W_EXE') or os.path.join(REPO, 'game-copy', 'bin', 'TS3W.exe')
SYMBOLS_CSV = os.path.join(REPO, 'tools', 'ghidra', 'symbols', 'ts3w-1.67.symbols.csv')

TRAP_BASE, TRAP_SIZE = 0x0F000000, 0x10000      # one 4-byte slot per import
HEAP_BASE, HEAP_SIZE = 0x20000000, 0x4000000    # 64 MB scratch heap, bump allocator
STACK_BASE, STACK_SIZE = 0x30000000, 0x100000   # 1 MB stack
TEB_BASE = 0x30200000                           # fs: points here; fs:[0] = -1 (end of SEH chain)
SENTINEL = 0x30300000                           # return address of the outer call
CALLEE_SAVED = (('ebx', ux.UC_X86_REG_EBX), ('esi', ux.UC_X86_REG_ESI),
                ('edi', ux.UC_X86_REG_EDI), ('ebp', ux.UC_X86_REG_EBP))
CONVENTIONS = ('cdecl', 'stdcall', 'thiscall', 'fastcall')


class EmuError(Exception):
    pass


def f32(x):
    """Round a Python float to IEEE single precision (what an SSE/x87 store to float does)."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def ts_ref(reqs):
    """Run tools/emu/ts-ref.ts (the repo's TypeScript reference code) on a batch; None when node is missing."""
    import json
    import shutil
    import subprocess
    node = shutil.which('node')
    if not node:
        return None
    out = subprocess.run([node, os.path.join(HERE, 'ts-ref.ts')], input=json.dumps(reqs), capture_output=True,
                         text=True, timeout=300, check=True)
    return json.loads(out.stdout)


def load_symbols(path=SYMBOLS_CSV):
    names = {}
    if os.path.exists(path):
        with open(path, newline='', encoding='utf-8') as fh:
            for row in csv.DictReader(fh):
                if row.get('type') == 'function':
                    names[int(row['address'], 16)] = row['name']
    return names


@dataclass
class Stub:
    name: str
    fn: object = None       # fn(emu, args) -> int | (eax, edx) | None; args(i) reads the i-th stack dword
    ret: int = 0            # return value when fn is None or returns None
    pop: int = 0            # bytes the callee pops (stdcall/thiscall arguments)
    calls: int = 0


@dataclass
class Result:
    eax: int
    edx: int
    st0: float | None
    blocks_run: int
    stubbed: dict           # name -> call count in this run
    global_reads: set
    global_writes: set


@dataclass
class Function:
    entry: int
    name: str
    blocks: list            # sorted block start addresses
    insns: set              # every instruction address
    callees: set            # direct call targets
    indirect: list = field(default_factory=list)  # addresses of indirect jumps (CFG may be incomplete)


class Emu:
    def __init__(self, exe_path=None):
        self.exe_path = exe_path or DEFAULT_EXE
        if not os.path.exists(self.exe_path):
            raise FileNotFoundError(self.exe_path)
        data = open(self.exe_path, 'rb').read()
        self.names = load_symbols()
        self.addr_by_name = {v: k for k, v in self.names.items()}
        self._parse_pe(data)
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self._map(data)
        self.stubs = {}         # address -> Stub (internal functions and import trap slots)
        self.functions = {}     # entry -> Function (CFG cache)
        self.coverage = {}      # entry -> set of reached block starts (accumulates over calls)
        self.allowed = set()    # instruction addresses the current call may execute
        self._fault = None
        self._heap = HEAP_BASE
        self.globals_read, self.globals_written = set(), set()  # .data addresses, accumulated over calls
        self._hooks()

    # ---- PE loading ----
    def _parse_pe(self, d):
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        if d[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', d, pe + 4)[0] != 0x14C:
            raise EmuError('not a 32-bit PE')
        nsec, = struct.unpack_from('<H', d, pe + 6)
        optsz, = struct.unpack_from('<H', d, pe + 20)
        opt = pe + 24
        self.base, = struct.unpack_from('<I', d, opt + 28)
        self.image_size, = struct.unpack_from('<I', d, opt + 56)
        self.import_rva, = struct.unpack_from('<I', d, opt + 96 + 8)
        self.sections = []
        o = opt + optsz
        for _ in range(nsec):
            name = d[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, va, rsize, rptr = struct.unpack_from('<IIII', d, o + 8)
            self.sections.append((name, self.base + va, max(vsize, rsize), d[rptr:rptr + min(rsize, vsize or rsize)]))
            o += 40

    def section(self, name):
        for n, va, size, _ in self.sections:
            if n == name:
                return va, va + size
        raise KeyError(name)

    def _map(self, d):
        uc = self.uc
        uc.mem_map(self.base, (self.image_size + 0xFFF) & ~0xFFF, UC_PROT_ALL)
        for _, va, _, raw in self.sections:
            uc.mem_write(va, raw)
        for base, size in ((TRAP_BASE, TRAP_SIZE), (HEAP_BASE, HEAP_SIZE), (STACK_BASE, STACK_SIZE),
                           (TEB_BASE, 0x1000), (SENTINEL, 0x1000)):
            uc.mem_map(base, size, UC_PROT_ALL)
        uc.mem_write(TRAP_BASE, b'\xCC' * TRAP_SIZE)
        uc.mem_write(SENTINEL, b'\xF4')  # hlt; never executed (emu_start stops at SENTINEL)
        uc.mem_write(TEB_BASE, struct.pack('<I', 0xFFFFFFFF))
        uc.mem_write(TEB_BASE + 0x18, struct.pack('<I', TEB_BASE))
        # fs: selector through a one-entry GDT (FS_BASE is a no-op in 32-bit mode): SEH prologues read fs:[0].
        # Entry 1: flat 4 GB data (SS/DS/ES must be reloaded too, or the stack turns 16-bit); entry 2: TEB.
        def seg(base, lim, flags):  # access 0x93 = present, ring 0, data RW; flags 0xC = 4K pages, 32-bit
            return (lim & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x93 << 40) | (((lim >> 16) & 0xF) << 48) \
                | (flags << 52) | ((base >> 24) << 56)
        gdt = TEB_BASE + 0x800
        uc.mem_write(gdt + 8, struct.pack('<QQ', seg(0, 0xFFFFF, 0xC), seg(TEB_BASE, 0xFFF, 0x4)))
        uc.reg_write(ux.UC_X86_REG_GDTR, (0, gdt, 0x18, 0))
        for reg in (ux.UC_X86_REG_SS, ux.UC_X86_REG_DS, ux.UC_X86_REG_ES):
            uc.reg_write(reg, 1 << 3)
        uc.reg_write(ux.UC_X86_REG_FS, 2 << 3)
        # Import table: point every IAT slot at its own trap address.
        self.imports = {}       # trap address -> 'dll!name'
        self.import_by_name = {}
        rd = lambda a, n: bytes(uc.mem_read(a, n))
        u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
        desc = self.base + self.import_rva
        slot = TRAP_BASE
        while self.import_rva and u32(desc + 12):
            dll = rd(self.base + u32(desc + 12), 64).split(b'\0')[0].decode().lower()
            oft, ft = u32(desc), u32(desc + 16)
            i = 0
            while True:
                ent = u32(self.base + (oft or ft) + 4 * i)
                if not ent:
                    break
                nm = f'#{ent & 0xFFFF}' if ent & 0x80000000 else rd(self.base + ent + 2, 128).split(b'\0')[0].decode()
                full = f'{dll}!{nm}'
                self.imports[slot] = full
                self.import_by_name[full] = slot
                uc.mem_write(self.base + ft + 4 * i, struct.pack('<I', slot))
                slot += 4
                i += 1
            desc += 20
        self.text = self.section('.text')
        self.rdata = self.section('.rdata')
        self.data = self.section('.data')

    # ---- memory helpers ----
    def read(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def write(self, addr, b):
        self.uc.mem_write(addr, bytes(b))

    def u32(self, addr):
        return struct.unpack('<I', self.read(addr, 4))[0]

    def w32(self, addr, v):
        self.write(addr, struct.pack('<I', v & 0xFFFFFFFF))

    def rf32(self, addr):
        return struct.unpack('<f', self.read(addr, 4))[0]

    def wf32(self, addr, v):
        self.write(addr, struct.pack('<f', v))

    def alloc(self, size, align=16, data=None):
        """Scratch heap (reset by reset_heap, which call() does not do: allocate inputs first)."""
        p = (self._heap + align - 1) & ~(align - 1)
        if p + size > HEAP_BASE + HEAP_SIZE:
            raise EmuError('scratch heap exhausted')
        self._heap = p + max(size, 1)
        self.write(p, data if data is not None else bytes(size))
        return p

    def reset_heap(self):
        used = getattr(self, '_heap', HEAP_BASE + HEAP_SIZE) - HEAP_BASE
        self.write(HEAP_BASE, bytes(min(used, HEAP_SIZE)))
        self._heap = HEAP_BASE

    def wstr(self, s):
        return self.alloc(2 * len(s) + 2, 4, s.encode('utf-16-le') + b'\0\0')

    # ---- stubs ----
    def resolve(self, target):
        """Address of an int, a symbol name from the CSV, or an import 'dll!name' (trap slot)."""
        if isinstance(target, int):
            return target
        if '!' in target:
            if target.lower() not in {k.lower() for k in self.import_by_name}:
                raise EmuError(f'no import {target}')
            return next(v for k, v in self.import_by_name.items() if k.lower() == target.lower())
        if target in self.addr_by_name:
            return self.addr_by_name[target]
        raise EmuError(f'unknown symbol {target}')

    def stub(self, target, fn=None, ret=0, pop=0, name=None):
        addr = self.resolve(target)
        self.stubs[addr] = Stub(name or self.imports.get(addr) or self.names.get(addr) or hex(addr), fn, ret, pop)
        if not (TRAP_BASE <= addr < TRAP_BASE + TRAP_SIZE):
            self.uc.hook_add(UC_HOOK_CODE, self._stub_hit, begin=addr, end=addr)

    def stub_allocator(self):
        """EA GeneralAllocator entry points (thiscall, signatures from the symbols CSV) on the scratch heap."""
        self.stub('GeneralAllocator_Malloc', lambda e, a: e.alloc(a(0), 16), pop=8)
        self.stub('GeneralAllocator_Memalign', lambda e, a: e.alloc(a(0), max(a(1), 16)), pop=16)
        self.stub('GeneralAllocator_Free', ret=0, pop=4)

    def _run_stub(self, uc, addr):
        st = self.stubs[addr]
        st.calls += 1
        esp = uc.reg_read(ux.UC_X86_REG_ESP)
        ret_addr = self.u32(esp)
        r = st.fn(self, lambda i: self.u32(esp + 4 + 4 * i)) if st.fn else None
        r = st.ret if r is None else r
        eax, edx = r if isinstance(r, tuple) else (r, 0)
        uc.reg_write(ux.UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(ux.UC_X86_REG_EDX, edx & 0xFFFFFFFF)
        uc.reg_write(ux.UC_X86_REG_ESP, esp + 4 + st.pop)
        uc.reg_write(ux.UC_X86_REG_EIP, ret_addr)

    def _stub_hit(self, uc, addr, size, _):
        if addr in self.stubs and self._fault is None:
            self._run_stub(uc, addr)

    # ---- CFG (recursive descent from the entry; calls are not followed) ----
    def cfg(self, entry):
        if entry in self.functions:
            return self.functions[entry]
        leaders, insns, callees, indirect, ends = {entry}, {}, set(), [], set()
        work, seen = [entry], set()
        while work:
            a = work.pop()
            if a in seen:
                continue
            bound = None  # imm of the last 'cmp reg, imm' on this walk: the size of a following jump table
            while True:
                if a in seen:
                    break
                seen.add(a)
                code = self.read(a, 16)
                ins = next(self.md.disasm(code, a, 1), None)
                if ins is None:
                    raise EmuError(f'cannot decode {a:#x} in function {entry:#x}')
                insns[a] = ins.size
                nxt = a + ins.size
                grp = set(ins.groups)
                op = ins.operands[0] if ins.operands else None
                imm = op.imm if op is not None and op.type == cx.X86_OP_IMM else None
                if cx.X86_GRP_CALL in grp:
                    if imm is not None:
                        callees.add(imm & 0xFFFFFFFF)
                    a = nxt
                    continue
                if ins.mnemonic in ('ret', 'retf', 'int3', 'hlt', 'ud2'):
                    ends.add(nxt)
                    break
                if ins.mnemonic == 'cmp' and len(ins.operands) == 2 and ins.operands[1].type == cx.X86_OP_IMM:
                    bound = ins.operands[1].imm
                if cx.X86_GRP_JUMP in grp:
                    tail = imm is not None and imm != entry and imm in self.names and ins.mnemonic == 'jmp'
                    m = op.mem if op is not None and op.type == cx.X86_OP_MEM else None
                    if (imm is None and m is not None and ins.mnemonic == 'jmp' and m.base == 0 and m.scale == 4
                            and bound is not None and 0 <= bound < 256 and self.text[0] <= m.disp < self.rdata[1]):
                        # MSVC switch: 'cmp idx, n; ja default; jmp [idx*4 + table]' -> table has n + 1 entries
                        for t in struct.unpack(f'<{bound + 1}I', self.read(m.disp, 4 * (bound + 1))):
                            leaders.add(t)
                            work.append(t)
                    elif imm is None:
                        indirect.append(a)
                    elif tail:
                        callees.add(imm)  # tail call into another named function
                    else:
                        leaders.add(imm)
                        work.append(imm)
                    if ins.mnemonic == 'jmp':
                        ends.add(nxt)
                        break
                    leaders.add(nxt)
                a = nxt
        blocks = sorted(l for l in leaders if l in insns)
        f = Function(entry, self.names.get(entry, f'sub_{entry:x}'), blocks, set(insns), callees, indirect)
        self.functions[entry] = f
        self.coverage.setdefault(entry, set())
        return f

    # ---- hooks ----
    def _hooks(self):
        uc = self.uc
        uc.hook_add(UC_HOOK_BLOCK, self._on_block)
        uc.hook_add(UC_HOOK_CODE, self._on_trap, begin=TRAP_BASE, end=TRAP_BASE + TRAP_SIZE - 1)
        uc.hook_add(UC_HOOK_MEM_INVALID, self._on_bad_mem)
        uc.hook_add(UC_HOOK_MEM_WRITE, self._on_image_write, begin=self.base, end=self.base + self.image_size - 1)
        uc.hook_add(UC_HOOK_MEM_READ, self._on_global_read, begin=self.data[0], end=self.data[1] - 1)

    def _fail(self, msg):
        if self._fault is None:
            self._fault = msg
        self.uc.emu_stop()

    def _on_block(self, uc, addr, size, _):
        self._blocks_run += 1
        if TRAP_BASE <= addr < TRAP_BASE + TRAP_SIZE or addr == SENTINEL or addr in self.stubs:
            return
        if addr not in self.allowed:
            who = self.names.get(addr, '')
            self._fail(f'unstubbed call or jump to {addr:#x} {who} (last block {self._last_block:#x}); '
                       'stub it or add it to follow')
            return
        self._last_block = addr
        for f in self._cover:
            i = bisect.bisect_left(f.blocks, addr)
            cov = self.coverage[f.entry]
            while i < len(f.blocks) and f.blocks[i] < addr + size:
                cov.add(f.blocks[i])
                i += 1

    def _on_trap(self, uc, addr, size, _):
        if addr in self.stubs:
            self._run_stub(uc, addr)
        else:
            self._fail(f'unstubbed import {self.imports.get(addr, hex(addr))} called from block {self._last_block:#x}')

    def _on_bad_mem(self, uc, access, addr, size, value, _):
        eip = uc.reg_read(ux.UC_X86_REG_EIP)
        self._fail(f'unmapped or invalid memory access (type {access}) at {addr:#x} size {size} by {eip:#x}')
        return False

    def _on_image_write(self, uc, access, addr, size, value, _):
        if self.text[0] <= addr < self.rdata[1]:
            self._fail(f'write into .text/.rdata at {addr:#x} by {uc.reg_read(ux.UC_X86_REG_EIP):#x}')
        else:
            self._gw.add(addr)

    def _on_global_read(self, uc, access, addr, size, value, _):
        self._gr.add(addr)

    # ---- call ----
    def st0(self):
        """ST(0) as a Python float (None when the x87 stack is empty)."""
        fpsw = self.uc.reg_read(ux.UC_X86_REG_FPSW)
        fptag = self.uc.reg_read(ux.UC_X86_REG_FPTAG)
        top = (fpsw >> 11) & 7
        if (fptag >> (2 * top)) & 3 == 3:
            return None
        mant, exp = self.uc.reg_read(ux.UC_X86_REG_FP0 + top)
        sign, e = exp >> 15, exp & 0x7FFF
        if e == 0x7FFF:
            v = math.inf if mant & ((1 << 63) - 1) == 0 else math.nan
        else:
            v = math.ldexp(mant, e - 16383 - 63) if (mant or e) else 0.0
        return -v if sign else v

    def call(self, target, args=(), conv='cdecl', this=None, follow=(), budget=2_000_000, timeout_ms=5000):
        """Call target with args (ints, Python floats as float32, ('f64', x) as double).

        follow: functions the target may run into (their code executes and gets coverage).
        Raises EmuError with the address on any policy violation.
        """
        if conv not in CONVENTIONS:
            raise EmuError(f'unknown convention {conv}')
        uc = self.uc
        entry = self.resolve(target)
        funcs = [self.cfg(entry)] + [self.cfg(self.resolve(f)) for f in follow]
        self._cover = funcs
        self.allowed = set().union(*(f.insns for f in funcs))
        stack = []
        for a in args:
            if isinstance(a, float):
                stack.append(struct.pack('<f', a))
            elif isinstance(a, tuple) and a[0] == 'f64':
                stack.append(struct.pack('<d', a[1]))
            else:
                stack.append(struct.pack('<I', a & 0xFFFFFFFF))
        regs = {}
        if conv == 'thiscall':
            regs[ux.UC_X86_REG_ECX] = this or 0
        elif this is not None:
            raise EmuError('this is only for thiscall')
        if conv == 'fastcall':
            for reg in (ux.UC_X86_REG_ECX, ux.UC_X86_REG_EDX):
                if stack and len(stack[0]) == 4:
                    regs[reg] = struct.unpack('<I', stack.pop(0))[0]
        blob = b''.join(stack)
        top = STACK_BASE + STACK_SIZE - 0x100
        esp = top - len(blob) - 4
        self.write(esp, struct.pack('<I', SENTINEL) + blob)
        expected_esp = esp + 4 + (len(blob) if conv in ('stdcall', 'thiscall', 'fastcall') else 0)
        uc.reg_write(ux.UC_X86_REG_FPCW, 0x027F)  # MSVC default: 53-bit precision, all exceptions masked
        uc.reg_write(ux.UC_X86_REG_MXCSR, 0x1F80)
        uc.reg_write(ux.UC_X86_REG_FPTAG, 0xFFFF)  # empty x87 stack
        uc.reg_write(ux.UC_X86_REG_FPSW, 0)
        canaries = {}
        for i, (name, reg) in enumerate(CALLEE_SAVED):
            canaries[name] = 0x5EED0000 + i
            uc.reg_write(reg, canaries[name])
        for reg in (ux.UC_X86_REG_EAX, ux.UC_X86_REG_ECX, ux.UC_X86_REG_EDX):
            uc.reg_write(reg, 0xBAD0BAD0)
        for reg, v in regs.items():
            uc.reg_write(reg, v)
        uc.reg_write(ux.UC_X86_REG_ESP, esp)
        uc.reg_write(ux.UC_X86_REG_EFLAGS, 0x202)
        for s in self.stubs.values():
            s.calls = 0
        self._fault, self._blocks_run, self._last_block = None, 0, entry
        self._gr, self._gw = set(), set()
        try:
            uc.emu_start(entry, SENTINEL, timeout=timeout_ms * 1000, count=budget)
        except UcError as e:
            if self._fault is None:
                self._fault = f'{e} at {uc.reg_read(ux.UC_X86_REG_EIP):#x}'
        self.globals_read |= self._gr
        self.globals_written |= self._gw
        if self._fault:
            raise EmuError(self._fault)
        eip = uc.reg_read(ux.UC_X86_REG_EIP)
        if eip != SENTINEL:
            raise EmuError(f'instruction budget ({budget}) or timeout ({timeout_ms} ms) hit at {eip:#x}')
        if uc.reg_read(ux.UC_X86_REG_ESP) != expected_esp:
            raise EmuError(f'stack imbalance after return: esp {uc.reg_read(ux.UC_X86_REG_ESP):#x}, '
                           f'expected {expected_esp:#x} for {conv}')
        for name, reg in CALLEE_SAVED:
            if uc.reg_read(reg) != canaries[name]:
                raise EmuError(f'callee-saved {name} not restored')
        return Result(uc.reg_read(ux.UC_X86_REG_EAX), uc.reg_read(ux.UC_X86_REG_EDX), self.st0(),
                      self._blocks_run, {s.name: s.calls for s in self.stubs.values() if s.calls},
                      set(self._gr), set(self._gw))

    def coverage_report(self, entries):
        """[(Function, reached block set)] for the given entries (accumulated over every call)."""
        return [(self.functions[e], self.coverage[e]) for e in entries]
