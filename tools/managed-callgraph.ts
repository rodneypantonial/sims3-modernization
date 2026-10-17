// PLAN gap (12) steps 12a + 12b: managed call graph of the script DLLs from .NET metadata and IL, plus leaf tags.
// Usage:
//   node tools/managed-callgraph.ts build [--asm game-copy/assemblies] [--out game-copy/analysis/callgraph]
//   node tools/managed-callgraph.ts query <root>... [--depth 12] [--delegates] [--index <out>/index.json] [--ic docs/data/ic-safety.csv]
//   node tools/managed-callgraph.ts def <DefinitionType> [--depth 12] ...   (roots: the type's Test overloads + nearest CalculateScore)
//   query/def: --cut <prefix,prefix> skips calls into hand-verified guarded methods; --delegates follows delegate creation
//   node tools/managed-callgraph.ts callers <method> [--index ...]
//   node tools/managed-callgraph.ts --self-check [--asm ...]
// A root or method is a method id ("Ns.Type+Nested::Name(params)") or a prefix of one ("Ns.Type::Name" = all overloads).
// Reads ECMA-335 metadata tables and IL bodies directly (no decompiler, no npm deps). Method ids carry the full
// parameter list, so overloads are distinct; generic methods carry `n arity; a duplicate id (conversion operators) gets ":ret".
// Edge kinds: call (direct), cv (callvirt to a non-virtual, sealed or final target), virt (unresolved virtual: declared
// target), iface (interface: declared target), ifaceN (interface with 1-4 implementing classes, all final: one edge per implementation), newobj,
// dlg (ldftn/ldvirtftn = delegate creation, not followed unless --delegates), dinv (delegate Invoke), cons (constrained. callvirt).
import { readFileSync, readdirSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { join } from 'node:path';

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const ASM_DIR = arg('--asm', 'game-copy/assemblies');
const OUT = arg('--out', 'game-copy/analysis/callgraph');
const INDEX = arg('--index', join(OUT, 'index.json'));
const IC_CSV = arg('--ic', 'docs/data/ic-safety.csv');
const DEPTH = Number(arg('--depth', '12'));
// Hand-verified guards (docs/73 §2.1 style): calls into methods with these id prefixes are not followed.
const CUT = arg('--cut', '').split(',').filter(Boolean);

// ---------- PE + metadata reader ----------
type Asm = {
  name: string; buf: Buffer; mvid: string; sections: { va: number; size: number; raw: number }[];
  strOff: number; blobOff: number; guidOff: number; rows: number[]; tables: number[][][];
  types: TypeDef[]; methods: Method[]; fields: Field[];
};
type TypeDef = { asm: Asm; row: number; ns: string; name: string; full: string; flags: number; extendsCi: number; methods: Method[]; fields: Field[]; enclosing?: TypeDef; ifaces: number[]; base?: TypeDef | null };
type Method = { gi: number; asm: Asm; row: number; type: TypeDef; name: string; flags: number; impl: number; rva: number; sigBlob: number; params: string; ret: string; arity: number; id: string };
type Field = { asm: Asm; row: number; type: TypeDef; name: string; flags: number; sigBlob: number; id: string };

// Column specs: number = fixed bytes; 's' string, 'g' guid, 'b' blob; 't<n>' simple index into table n; 'c<name>' coded index.
const CODED: Record<string, number[]> = {
  TypeDefOrRef: [2, 1, 0x1b], HasConstant: [4, 8, 0x17],
  HasCustomAttribute: [6, 4, 1, 2, 8, 9, 10, 0, 0x0e, 0x17, 0x14, 0x11, 0x1a, 0x1b, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2a, 0x2c, 0x2b],
  HasFieldMarshal: [4, 8], HasDeclSecurity: [2, 6, 0x20], MemberRefParent: [2, 1, 0x1a, 6, 0x1b], HasSemantics: [0x14, 0x17],
  MethodDefOrRef: [6, 0x0a], MemberForwarded: [4, 6], Implementation: [0x26, 0x23, 0x27], CustomAttributeType: [-1, -1, 6, 0x0a, -1],
  ResolutionScope: [0, 0x1a, 0x23, 1], TypeOrMethodDef: [2, 6],
};
const SCHEMA: (number | string)[][] = [];
const S = (t: number, ...c: (number | string)[]) => { SCHEMA[t] = c; };
S(0x00, 2, 's', 'g', 'g', 'g'); S(0x01, 'cResolutionScope', 's', 's'); S(0x02, 4, 's', 's', 'cTypeDefOrRef', 't4', 't6');
S(0x03, 't4'); S(0x04, 2, 's', 'b'); S(0x05, 't6'); S(0x06, 4, 2, 2, 's', 'b', 't8'); S(0x07, 't8'); S(0x08, 2, 2, 's');
S(0x09, 't2', 'cTypeDefOrRef'); S(0x0a, 'cMemberRefParent', 's', 'b'); S(0x0b, 2, 'cHasConstant', 'b');
S(0x0c, 'cHasCustomAttribute', 'cCustomAttributeType', 'b'); S(0x0d, 'cHasFieldMarshal', 'b'); S(0x0e, 2, 'cHasDeclSecurity', 'b');
S(0x0f, 2, 4, 't2'); S(0x10, 4, 't4'); S(0x11, 'b'); S(0x12, 't2', 't20'); S(0x13, 't20'); S(0x14, 2, 's', 'cTypeDefOrRef');
S(0x15, 't2', 't23'); S(0x16, 't23'); S(0x17, 2, 's', 'b'); S(0x18, 2, 't6', 'cHasSemantics'); S(0x19, 't2', 'cMethodDefOrRef', 'cMethodDefOrRef');
S(0x1a, 's'); S(0x1b, 'b'); S(0x1c, 2, 'cMemberForwarded', 's', 't26'); S(0x1d, 4, 't4'); S(0x1e, 4, 4); S(0x1f, 4);
S(0x20, 4, 2, 2, 2, 2, 4, 'b', 's', 's'); S(0x21, 4); S(0x22, 4, 4, 4); S(0x23, 2, 2, 2, 2, 4, 'b', 's', 's', 'b'); S(0x24, 4, 't35');
S(0x25, 4, 4, 4, 't35'); S(0x26, 4, 's', 'b'); S(0x27, 4, 4, 's', 's', 'cImplementation'); S(0x28, 4, 4, 's', 'cImplementation');
S(0x29, 't2', 't2'); S(0x2a, 2, 2, 'cTypeOrMethodDef', 's'); S(0x2b, 'cMethodDefOrRef', 'b'); S(0x2c, 't42', 'cTypeDefOrRef');

const tagBits = (n: number) => Math.ceil(Math.log2(n));
function decodeCi(kind: string, v: number): [number, number] {
  const tabs = CODED[kind]; const b = tagBits(tabs.length);
  return [tabs[v & ((1 << b) - 1)], v >>> b];
}

function loadAsm(path: string, name: string): Asm {
  const buf = readFileSync(path);
  const pe = buf.readUInt32LE(0x3c);
  if (buf.readUInt32LE(pe) !== 0x4550) throw new Error(`${name}: not PE`);
  const nsec = buf.readUInt16LE(pe + 6), optSize = buf.readUInt16LE(pe + 20), opt = pe + 24;
  const ddir = opt + (buf.readUInt16LE(opt) === 0x20b ? 112 : 96);
  const sections = [];
  for (let i = 0, s = opt + optSize; i < nsec; i++, s += 40) sections.push({ va: buf.readUInt32LE(s + 12), size: Math.max(buf.readUInt32LE(s + 8), buf.readUInt32LE(s + 16)), raw: buf.readUInt32LE(s + 20) });
  const asm = { name, buf, sections } as Asm;
  const cli = rva(asm, buf.readUInt32LE(ddir + 14 * 8));
  const md = rva(asm, buf.readUInt32LE(cli + 8));
  if (buf.readUInt32LE(md) !== 0x424a5342) throw new Error(`${name}: no metadata`);
  let p = md + 16 + buf.readUInt32LE(md + 12) + 2;
  const nstreams = buf.readUInt16LE(p); p += 2;
  const streams: Record<string, number> = {};
  for (let i = 0; i < nstreams; i++) {
    const off = buf.readUInt32LE(p); p += 8;
    let e = p; while (buf[e]) e++;
    streams[buf.toString('latin1', p, e)] = md + off;
    p = (e + 4) & ~3;
  }
  if (streams['#-'] !== undefined) throw new Error(`${name}: uncompressed (#-) metadata not supported`);
  asm.strOff = streams['#Strings']; asm.blobOff = streams['#Blob']; asm.guidOff = streams['#GUID'];
  const t = streams['#~'];
  const heap = buf[t + 6];
  const valid = buf.readBigUInt64LE(t + 8);
  const rows: number[] = new Array(64).fill(0);
  p = t + 24;
  for (let i = 0; i < 64; i++) if ((valid >> BigInt(i)) & 1n) { rows[i] = buf.readUInt32LE(p); p += 4; }
  asm.rows = rows;
  const size = (c: number | string): number => {
    if (typeof c === 'number') return c;
    if (c === 's') return heap & 1 ? 4 : 2;
    if (c === 'g') return heap & 2 ? 4 : 2;
    if (c === 'b') return heap & 4 ? 4 : 2;
    if (c[0] === 't') return rows[+c.slice(1)] < 0x10000 ? 2 : 4;
    const tabs = CODED[c.slice(1)];
    const max = Math.max(...tabs.map((x) => (x < 0 ? 0 : rows[x])));
    return max < 1 << (16 - tagBits(tabs.length)) ? 2 : 4;
  };
  asm.tables = [];
  for (let ti = 0; ti < 64; ti++) {
    if (!rows[ti]) continue;
    const spec = SCHEMA[ti];
    if (!spec) throw new Error(`${name}: unknown table 0x${ti.toString(16)}`);
    const sizes = spec.map(size);
    const out: number[][] = [];
    for (let r = 0; r < rows[ti]; r++) {
      const row: number[] = [];
      for (const s of sizes) { row.push(s === 1 ? buf[p] : s === 2 ? buf.readUInt16LE(p) : buf.readUInt32LE(p)); p += s; }
      out.push(row);
    }
    asm.tables[ti] = out;
  }
  const mod = asm.tables[0][0];
  asm.mvid = buf.subarray(asm.guidOff + (mod[2] - 1) * 16, asm.guidOff + mod[2] * 16).toString('hex');
  return asm;
}
function rva(asm: Asm, r: number): number {
  for (const s of asm.sections) if (r >= s.va && r < s.va + s.size) return r - s.va + s.raw;
  throw new Error(`${asm.name}: rva 0x${r.toString(16)} outside sections`);
}
const str = (a: Asm, i: number) => { const s = a.strOff + i; let e = s; while (a.buf[e]) e++; return a.buf.toString('utf8', s, e); };
const tbl = (a: Asm, t: number) => a.tables[t] ?? [];

// Compressed unsigned int (II.23.2). Returns [value, bytes used].
export function cuint(b: Buffer, p: number): [number, number] {
  const x = b[p];
  if ((x & 0x80) === 0) return [x, 1];
  if ((x & 0xc0) === 0x80) return [((x & 0x3f) << 8) | b[p + 1], 2];
  return [((x & 0x1f) * 0x1000000) + (b[p + 1] << 16) + (b[p + 2] << 8) + b[p + 3], 4];
}
const blob = (a: Asm, i: number): [number, number] => { const [len, n] = cuint(a.buf, a.blobOff + i); return [a.blobOff + i + n, len]; };

// ---------- global model ----------
const asms: Asm[] = [];
const typeByFull = new Map<string, TypeDef>();
const methods: Method[] = [];

function typeRefName(a: Asm, row: number): string {
  const r = tbl(a, 1)[row - 1];
  const [st, si] = decodeCi('ResolutionScope', r[0]);
  const nm = str(a, r[1]), ns = str(a, r[2]);
  if (st === 1 && si) return `${typeRefName(a, si)}+${nm}`;
  return ns ? `${ns}.${nm}` : nm;
}
function tdorName(a: Asm, t: number, i: number): string {
  if (t === 2) return a.types[i - 1].full;
  if (t === 1) return typeRefName(a, i);
  const [p] = blob(a, tbl(a, 0x1b)[i - 1][0]);
  return sigType(a, p)[0];
}
const PRIM: Record<number, string> = { 1: 'void', 2: 'bool', 3: 'char', 4: 'sbyte', 5: 'byte', 6: 'short', 7: 'ushort', 8: 'int', 9: 'uint', 0x0a: 'long', 0x0b: 'ulong', 0x0c: 'float', 0x0d: 'double', 0x0e: 'string', 0x16: 'typedref', 0x18: 'IntPtr', 0x19: 'UIntPtr', 0x1c: 'object' };
// Type in a signature blob to a name string. Returns [name, next offset].
function sigType(a: Asm, p: number): [string, number] {
  const b = a.buf; const e = b[p++];
  if (PRIM[e]) return [PRIM[e], p];
  switch (e) {
    case 0x0f: { const [t, q] = sigType(a, p); return [t + '*', q]; }
    case 0x10: { const [t, q] = sigType(a, p); return [t + '&', q]; }
    case 0x11: case 0x12: { const [v, n] = cuint(b, p); return [tdorName(a, [2, 1, 0x1b][v & 3], v >>> 2), p + n]; }
    case 0x13: case 0x1e: { const [v, n] = cuint(b, p); return [(e === 0x13 ? '!' : '!!') + v, p + n]; }
    case 0x14: {
      const [t, q0] = sigType(a, p); let q = q0;
      const [rank, n1] = cuint(b, q); q += n1;
      for (let k = 0; k < 2; k++) { const [cnt, n2] = cuint(b, q); q += n2; for (let i = 0; i < cnt; i++) q += cuint(b, q)[1]; }
      return [`${t}[${','.repeat(Math.max(0, rank - 1))}]`, q];
    }
    case 0x15: {
      const [g, q0] = sigType(a, p); let q = q0;
      const [cnt, n] = cuint(b, q); q += n;
      const args: string[] = [];
      for (let i = 0; i < cnt; i++) { const [t, q2] = sigType(a, q); args.push(t); q = q2; }
      return [`${g}<${args.join(',')}>`, q];
    }
    case 0x1b: { const r = methodSig(a, p); return [`fnptr(${r.params})`, r.end]; }
    case 0x1d: { const [t, q] = sigType(a, p); return [t + '[]', q]; }
    case 0x1f: case 0x20: { const n = cuint(b, p)[1]; return sigType(a, p + n); } // custom modifiers: dropped
    case 0x41: case 0x45: return sigType(a, p); // sentinel, pinned
  }
  throw new Error(`${a.name}: sig element 0x${e.toString(16)} at ${p - 1}`);
}
function methodSig(a: Asm, p: number): { params: string; ret: string; arity: number; end: number } {
  const b = a.buf; const cc = b[p++]; let arity = 0;
  if (cc & 0x10) { const [g, n] = cuint(b, p); arity = g; p += n; }
  const [cnt, n] = cuint(b, p); p += n;
  const [ret, q] = sigType(a, p); p = q;
  const ps: string[] = [];
  for (let i = 0; i < cnt; i++) { if (b[p] === 0x41) p++; const [t, q2] = sigType(a, p); ps.push(t); p = q2; }
  return { params: ps.join(','), ret, arity, end: p };
}
function fieldSigType(a: Asm, blobIdx: number): string { const [p] = blob(a, blobIdx); return sigType(a, p + 1)[0]; }

function loadAll(dir: string): void {
  for (const f of readdirSync(dir).filter((n) => n.endsWith('.dll')).sort()) asms.push(loadAsm(join(dir, f), f.replace(/\.dll$/, '')));
  for (const a of asms) {
    const tds = tbl(a, 2), mds = tbl(a, 6), fds = tbl(a, 4);
    a.types = tds.map((r, i) => ({ asm: a, row: i + 1, ns: str(a, r[2]), name: str(a, r[1]), full: '', flags: r[0], extendsCi: r[3], methods: [], fields: [], ifaces: [] }));
    for (const r of tbl(a, 0x29)) a.types[r[0] - 1].enclosing = a.types[r[1] - 1];
    const full = (t: TypeDef): string => t.full || (t.full = t.enclosing ? `${full(t.enclosing)}+${t.name}` : t.ns ? `${t.ns}.${t.name}` : t.name);
    a.types.forEach(full);
    for (const r of tbl(a, 9)) a.types[r[0] - 1].ifaces.push(r[1]);
    a.methods = []; a.fields = [];
    tds.forEach((r, i) => {
      const t = a.types[i];
      const mEnd = i + 1 < tds.length ? tds[i + 1][5] : mds.length + 1;
      for (let m = r[5]; m < mEnd; m++) {
        const md = mds[m - 1];
        const [bp] = blob(a, md[4]);
        const sig = methodSig(a, bp);
        const meth: Method = { gi: methods.length, asm: a, row: m, type: t, name: str(a, md[3]), flags: md[2], impl: md[1], rva: md[0], sigBlob: md[4], params: sig.params, ret: sig.ret, arity: sig.arity, id: '' };
        methods.push(meth); t.methods.push(meth); a.methods[m - 1] = meth;
      }
      const fEnd = i + 1 < tds.length ? tds[i + 1][4] : fds.length + 1;
      for (let f = r[4]; f < fEnd; f++) {
        const fd = fds[f - 1];
        const fld: Field = { asm: a, row: f, type: t, name: str(a, fd[1]), flags: fd[0], sigBlob: fd[2], id: `${t.full}::${str(a, fd[1])}` };
        t.fields.push(fld); a.fields[f - 1] = fld;
      }
    });
    for (const t of a.types) if (!typeByFull.has(t.full)) typeByFull.set(t.full, t);
  }
  for (const t of asms.flatMap((a) => a.types)) {
    const seen = new Map<string, Method[]>();
    for (const m of t.methods) {
      m.id = `${t.full}::${m.name}${m.arity ? '`' + m.arity : ''}(${m.params})`;
      seen.set(m.id, [...(seen.get(m.id) ?? []), m]);
    }
    for (const [, ms] of seen) if (ms.length > 1) for (const m of ms) m.id += `:${m.ret}`;
  }
}

// ---------- resolution ----------
// TypeDef/TypeRef/TypeSpec to its TypeDef; a generic instance resolves to its generic type (arrays, vars: none).
function resolveTdor(a: Asm, t: number, i: number): TypeDef | undefined {
  if (t === 2) return a.types[i - 1];
  if (t === 1) return typeByFull.get(typeRefName(a, i));
  const [p] = blob(a, tbl(a, 0x1b)[i - 1][0]);
  if (a.buf[p] !== 0x15) return undefined;
  const [v] = cuint(a.buf, p + 2);
  return resolveTdor(a, [2, 1, 0x1b][v & 3], v >>> 2);
}
function baseOf(t: TypeDef): TypeDef | undefined {
  if (t.base === undefined) {
    const [bt, bi] = decodeCi('TypeDefOrRef', t.extendsCi);
    t.base = bi ? resolveTdor(t.asm, bt, bi) ?? null : null;
  }
  return t.base ?? undefined;
}
const isIface = (t: TypeDef) => (t.flags & 0x20) !== 0;
const isSealed = (t: TypeDef) => (t.flags & 0x100) !== 0;
const isDelegate = (t: TypeDef) => baseOf(t)?.full === 'System.MulticastDelegate';
const VIRT = 0x40, FINAL = 0x20, ABSTRACT = 0x400, IC_FLAG = 0x1000, PINVOKE = 0x2000; // ScriptCore declares its ICs as extern PInvokeImpl

function findMethod(t: TypeDef | undefined, name: string, params: string, ret: string, arity: number): Method | undefined {
  for (let c = t, hops = 0; c && hops < 40; c = baseOf(c), hops++) {
    const m = c.methods.find((x) => x.name === name && x.params === params && x.ret === ret && x.arity === arity);
    if (m) return m;
  }
  return undefined;
}
const stubs = new Map<string, number>(); // unresolved external targets (array accessors, missing refs)
type Target = { m?: Method; stub?: string };
function resolveMethodToken(a: Asm, tok: number): Target {
  const tt = tok >>> 24, ti = tok & 0xffffff;
  if (tt === 0x06) return { m: a.methods[ti - 1] };
  if (tt === 0x2b) { const [mt, mi] = decodeCi('MethodDefOrRef', tbl(a, 0x2b)[ti - 1][0]); return resolveMethodToken(a, ((mt === 6 ? 0x06 : 0x0a) << 24) | mi); }
  if (tt !== 0x0a) return { stub: `?token 0x${tok.toString(16)}` };
  const r = tbl(a, 0x0a)[ti - 1];
  const [pt, pi] = decodeCi('MemberRefParent', r[0]);
  const name = str(a, r[1]);
  if (pt === 6) return { m: a.methods[pi - 1] };
  const [bp] = blob(a, r[2]);
  const sig = methodSig(a, bp);
  const owner = pt === 2 || pt === 1 || pt === 0x1b ? tdorName(a, pt, pi) : `module:${pi}`;
  const t = pt === 0x1a ? undefined : resolveTdor(a, pt, pi);
  const m = findMethod(t, name, sig.params, sig.ret, sig.arity);
  return m ? { m } : { stub: `${owner}::${name}${sig.arity ? '`' + sig.arity : ''}(${sig.params})` };
}
function resolveFieldToken(a: Asm, tok: number): { id: string; type: string } {
  const tt = tok >>> 24, ti = tok & 0xffffff;
  if (tt === 0x04) { const f = a.fields[ti - 1]; return { id: f.id, type: fieldSigType(a, f.sigBlob) }; }
  const r = tbl(a, 0x0a)[ti - 1];
  const [pt, pi] = decodeCi('MemberRefParent', r[0]);
  const t = resolveTdor(a, pt, pi);
  return { id: `${t?.full ?? tdorName(a, pt, pi)}::${str(a, r[1])}`, type: fieldSigType(a, r[2]) };
}

// Interface method -> its implementations when at most 4 non-abstract classes declare the interface and every
// implementation is final (no subclass can override it). Cheap class hierarchy analysis; 12d does the rest.
let implementors: Map<TypeDef, TypeDef[]> | undefined;
function ifaceImpls(m: Method): Method[] | undefined {
  if (!implementors) {
    implementors = new Map();
    for (const t of asms.flatMap((a) => a.types)) {
      if (isIface(t) || t.flags & 0x80) continue;
      for (const ci of t.ifaces) { const [it, ii] = decodeCi('TypeDefOrRef', ci); const i = resolveTdor(t.asm, it, ii); if (i) implementors.set(i, [...(implementors.get(i) ?? []), t]); }
    }
  }
  const im = implementors.get(m.type);
  if (!im || im.length > 4) return undefined;
  const out: Method[] = [];
  for (const c of im) {
    let impl: Method | undefined;
    for (const r of tbl(c.asm, 0x19)) if (r[0] === c.row) { // explicit MethodImpl
      const [dt, di] = decodeCi('MethodDefOrRef', r[2]);
      if (resolveMethodToken(c.asm, ((dt === 6 ? 0x06 : 0x0a) << 24) | di).m === m) { const [bt, bi] = decodeCi('MethodDefOrRef', r[1]); impl = resolveMethodToken(c.asm, ((bt === 6 ? 0x06 : 0x0a) << 24) | bi).m; }
    }
    impl ??= findMethod(c, m.name, m.params, m.ret, m.arity);
    if (!impl || (impl.flags & VIRT && !(impl.flags & FINAL) && !isSealed(c))) return undefined;
    out.push(impl);
  }
  return out;
}

// ---------- IL ----------
// Operand size per opcode; 'T' = 4-byte token, 'S' = switch. Unlisted single-byte opcodes take no operand.
const OP1: Record<number, number | string> = {};
for (const o of [0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x1f, 0xde]) OP1[o] = 1;
for (let o = 0x2b; o <= 0x37; o++) OP1[o] = 1;
for (let o = 0x38; o <= 0x44; o++) OP1[o] = 4;
OP1[0x20] = 4; OP1[0x21] = 8; OP1[0x22] = 4; OP1[0x23] = 8; OP1[0xdd] = 4; OP1[0x45] = 'S';
for (const o of [0x27, 0x28, 0x29, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x79, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x8c, 0x8d, 0x8f, 0xa3, 0xa4, 0xa5, 0xc2, 0xc6, 0xd0]) OP1[o] = 'T';
const OP2: Record<number, number | string> = { 0x06: 'T', 0x07: 'T', 0x09: 2, 0x0a: 2, 0x0b: 2, 0x0c: 2, 0x0d: 2, 0x0e: 2, 0x12: 1, 0x15: 'T', 0x16: 'T', 0x19: 1, 0x1c: 'T' };
export type Ins = { op: number; tok: number; at: number };
// Decode an IL byte range into instructions that carry a token (the only ones the graph needs); throws on a bad stream.
export function decodeIl(b: Buffer, start: number, len: number): Ins[] {
  const out: Ins[] = []; let p = start; const end = start + len;
  while (p < end) {
    const at = p; let op = b[p++]; let sz: number | string | undefined;
    if (op === 0xfe) { op = 0xfe00 | b[p++]; sz = OP2[op & 0xff]; } else sz = OP1[op];
    if (sz === 'T') { out.push({ op, tok: b.readUInt32LE(p), at }); p += 4; } else if (sz === 'S') { p += 4 + 4 * b.readUInt32LE(p); } else if (sz) p += sz as number;
  }
  if (p !== end) throw new Error(`IL overrun at ${p} (end ${end})`);
  return out;
}
function body(m: Method): Ins[] {
  if (!m.rva || m.impl & IC_FLAG || m.flags & (ABSTRACT | PINVOKE)) return [];
  const b = m.asm.buf; const p = rva(m.asm, m.rva);
  if ((b[p] & 3) === 2) return decodeIl(b, p + 1, b[p] >> 2);
  const hdr = (b.readUInt16LE(p) >> 12) * 4;
  return decodeIl(b, p + hdr, b.readUInt32LE(p + 4));
}

// ---------- build ----------
const KINDS = ['call', 'cv', 'virt', 'iface', 'ifaceN', 'newobj', 'dlg', 'dinv', 'cons'] as const;
const MUTATORS = /^(Add|AddRange|Remove|RemoveAt|RemoveAll|Clear|Insert|set_Item|Push|Pop|Enqueue|Dequeue|Sort|Reverse)$/;
type Rec = { k: string; a: number; f: number; ic?: string; c: number[]; fr: number[]; fw: number[]; sr: number[]; sw: number[]; sc?: number[] };
type Index = { version: 1; asms: { name: string; mvid: string }[]; kinds: string[]; methods: Rec[]; fields: string[]; stats: Record<string, number> };

function build(): Index {
  loadAll(ASM_DIR);
  const fieldIx = new Map<string, number>(); const fields: string[] = [];
  const fx = (id: string) => { let i = fieldIx.get(id); if (i === undefined) { i = fields.length; fields.push(id); fieldIx.set(id, i); } return i; };
  const recs: Rec[] = methods.map((m) => ({ k: m.id, a: asms.indexOf(m.asm), f: m.flags, c: [], fr: [], fw: [], sr: [], sw: [] }));
  const stubIx = (s: string) => { let i = stubs.get(s); if (i === undefined) { i = recs.length; recs.push({ k: s, a: -1, f: 0, c: [], fr: [], fw: [], sr: [], sw: [] }); stubs.set(s, i); } return i; };
  const st: Record<string, number> = { methods: methods.length, withBody: 0, ics: 0, ilErrors: 0, sites: 0, unresolvedSites: 0 };
  for (const k of KINDS) st[k] = 0;
  for (const m of methods) {
    const r = recs[m.gi];
    if (m.impl & IC_FLAG || m.flags & PINVOKE) { r.ic = `${m.type.full}::${m.name}`; st.ics++; continue; }
    let ins: Ins[];
    try { ins = body(m); } catch { st.ilErrors++; continue; }
    if (ins.length || m.rva) st.withBody++;
    const edges = new Set<string>();
    const collFields: string[] = []; let collMut = false;
    let constrained = false;
    for (const x of ins) {
      if (x.op === 0xfe16) { constrained = true; continue; }
      const op = x.op;
      if (op === 0x28 || op === 0x6f || op === 0x73 || op === 0xfe06 || op === 0xfe07) {
        const tg = resolveMethodToken(m.asm, x.tok);
        let kind: string; let tgt = tg.m;
        if (!tgt) kind = op === 0x73 ? 'newobj' : op >= 0xfe00 ? 'dlg' : op === 0x6f ? 'virt' : 'call';
        else if (op === 0x73) kind = 'newobj';
        else if (op >= 0xfe00) kind = 'dlg';
        else if (op === 0x28) kind = 'call';
        else if (constrained) kind = 'cons';
        else if (isDelegate(tgt.type) && tgt.name === 'Invoke') kind = 'dinv';
        else if (isIface(tgt.type)) kind = ifaceImpls(tgt) ? 'ifaceN' : 'iface';
        else if (!(tgt.flags & VIRT) || tgt.flags & FINAL || isSealed(tgt.type)) kind = 'cv';
        else kind = 'virt';
        constrained = false;
        if (op === 0x28 || op === 0x6f) { st.sites++; if (['virt', 'iface', 'cons', 'dinv'].includes(kind) || !tgt) st.unresolvedSites++; }
        if (tgt && MUTATORS.test(tgt.name) && tgt.type.full.startsWith('System.Collections')) collMut = true;
        for (const ti of kind === 'ifaceN' ? ifaceImpls(tgt!)!.map((x) => x.gi) : [tgt ? tgt.gi : stubIx(tg.stub!)]) {
          const key = `${ti}:${kind}`;
          if (!edges.has(key)) { edges.add(key); r.c.push(ti, KINDS.indexOf(kind as never)); st[kind]++; }
        }
        continue;
      }
      constrained = false;
      if (op >= 0x7b && op <= 0x80) {
        const f = resolveFieldToken(m.asm, x.tok); const i = fx(f.id);
        const list = op === 0x7d ? r.fw : op === 0x80 ? r.sw : op <= 0x7c ? r.fr : r.sr;
        if (!list.includes(i)) list.push(i);
        if ((op === 0x7e || op === 0x7f) && /^System\.Collections\./.test(f.type)) collFields.push(f.id);
      }
    }
    if (collMut && collFields.length) r.sc = [...new Set(collFields)].map(fx);
  }
  st.stubs = stubs.size; st.fields = fields.length;
  return { version: 1, asms: asms.map((a) => ({ name: a.name, mvid: a.mvid })), kinds: [...KINDS], methods: recs, fields, stats: st };
}

// ---------- 12b leaf rules + query ----------
const SEV = ['YIELD', 'MAIN', 'UNSAFE', 'IC?', 'EVENT', 'STATIC_WRITE', 'STATIC_COLL', 'DELEGATE', 'VIRTUAL', 'UNRESOLVED', 'PROXY', 'LAZY_CACHE', 'MONO_IC', 'SAFE'];
const YIELD_IC = /::(Simulator_SleepImpl|Simulator_SetYieldingDisabledImpl|SACS_AcquireDriverImpl|GameUtils_WaitFor\w*|GameUtils_StartupYield|OnlineFeaturesHost_WaitForDownload)$/;
const YIELD_M = /^(Sims3\.SimIFace\.Simulator::(Sleep|Yield\w*|set_YieldingDisabled)\(|Sims3\.SimIFace\.StateMachineClient::RequestState\()/;
// Compiler-generated lazy caches (cached anonymous delegates, string-switch maps): idempotent writes, own low tag.
const LAZY = /::<>9__CachedAnonymousMethodDelegate|^<PrivateImplementationDetails>/;
const EVENT_M = /\.EventTracker::(SendEvent|SendEvents|RaiseEvent)\w*\(/;

function parseCsv(text: string): string[][] {
  const rows: string[][] = []; let row: string[] = [], cur = '', q = false;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (q) { if (ch === '"') { if (text[i + 1] === '"') { cur += '"'; i++; } else q = false; } else cur += ch; continue; }
    if (ch === '"') q = true; else if (ch === ',') { row.push(cur); cur = ''; } else if (ch === '\n') { row.push(cur.replace(/\r$/, '')); rows.push(row); row = []; cur = ''; } else cur += ch;
  }
  if (cur || row.length) { row.push(cur); rows.push(row); }
  return rows;
}
function icVerdicts(): Map<string, string> {
  const rows = parseCsv(readFileSync(IC_CSV, 'utf8'));
  const h = rows[0]; const ci = h.indexOf('ic'), vi = h.indexOf('verdict');
  const m = new Map(rows.slice(1).filter((r) => r[ci]).map((r) => [r[ci], r[vi]]));
  // By method name too, for bindings the csv names under another class (UTFConnector* twins, Sims3.UI.LocalizedStringService).
  const byName = new Map<string, string>();
  for (const [k, v] of m) { const n = '*::' + k.split('::')[1]; byName.set(n, byName.has(n) && byName.get(n) !== v ? 'IC?' : v); }
  for (const [k, v] of byName) m.set(k, v);
  return m;
}
function icTag(ix: Index, r: Rec, ics: Map<string, string>): string {
  return ics.get(r.ic!) ?? (ix.asms[r.a].name === 'mscorlib' ? 'MONO_IC' : ics.get('*::' + r.ic!.split('::')[1]) ?? 'IC?');
}
// Own tags of one method (not its callees): [tag, detail][].
function ownTags(ix: Index, i: number, ics: Map<string, string>): [string, string][] {
  const r = ix.methods[i]; const out: [string, string][] = [];
  if (r.ic) { out.push([icTag(ix, r, ics), r.ic]); if (YIELD_IC.test(r.ic)) out.push(['YIELD', r.ic]); }
  if (YIELD_M.test(r.k)) out.push(['YIELD', r.k]);
  if (EVENT_M.test(r.k)) out.push(['EVENT', r.k]);
  if (r.a < 0) out.push(['UNRESOLVED', r.k]);
  for (const f of r.sw) out.push([LAZY.test(ix.fields[f]) ? 'LAZY_CACHE' : 'STATIC_WRITE', ix.fields[f]]);
  for (const f of r.sc ?? []) out.push([LAZY.test(ix.fields[f]) ? 'LAZY_CACHE' : 'STATIC_COLL', ix.fields[f]]);
  return out;
}
type Hit = { tag: string; detail: string; path: number[]; edge?: string };
export function closure(ix: Index, roots: number[], depth: number, ics: Map<string, string>, delegates = false, cut: string[] = []) {
  const parent = new Map<number, number>(); const dist = new Map<number, number>();
  const hits = new Map<string, Hit>(); const counts = new Map<string, number>();
  const note = (h: Hit) => { counts.set(h.tag, (counts.get(h.tag) ?? 0) + 1); if (!hits.has(h.tag)) hits.set(h.tag, h); };
  const pathTo = (n: number) => { const p = [n]; while (parent.has(p[0])) p.unshift(parent.get(p[0])!); return p; };
  let q = roots.filter((r, i, a) => a.indexOf(r) === i);
  q.forEach((r) => dist.set(r, 0));
  for (let d = 0; q.length && d <= depth; d++) {
    const next: number[] = [];
    for (const n of q) {
      for (const [tag, detail] of ownTags(ix, n, ics)) note({ tag, detail, path: pathTo(n) });
      const c = ix.methods[n].c;
      for (let k = 0; k < c.length; k += 2) {
        const t = c[k], kind = ix.kinds[c[k + 1]];
        if (kind === 'virt' || kind === 'iface' || kind === 'cons') note({ tag: 'VIRTUAL', detail: `${kind} ${ix.methods[t].k}`, path: pathTo(n), edge: kind });
        if (kind === 'dinv') note({ tag: 'DELEGATE', detail: ix.methods[t].k, path: pathTo(n), edge: kind });
        if (kind === 'dlg' && !delegates) continue;
        if (cut.some((c) => ix.methods[t].k.startsWith(c))) continue;
        if (d < depth && !dist.has(t)) { dist.set(t, d + 1); parent.set(t, n); next.push(t); }
      }
    }
    q = next;
  }
  const tags = [...hits.values()].sort((x, y) => SEV.indexOf(x.tag) - SEV.indexOf(y.tag));
  return { size: dist.size, reached: dist, tags, counts, worst: tags[0]?.tag ?? 'SAFE' };
}
const loadIndex = (): Index => JSON.parse(readFileSync(INDEX, 'utf8'));
function lookup(ix: Index, pat: string): number[] {
  const exact = ix.methods.findIndex((r) => r.k === pat);
  if (exact >= 0) return [exact];
  const out: number[] = [];
  ix.methods.forEach((r, i) => { if (r.k.startsWith(pat + '(') || r.k.startsWith(pat + '`')) out.push(i); });
  return out;
}
// Roots for an interaction definition: own Test overloads (else nearest base's) + nearest CalculateScore up the base chain.
function defRoots(ix: Index, type: string): number[] {
  if (!asms.length) loadAll(ASM_DIR);
  const t = typeByFull.get(type);
  if (!t) throw new Error(`no type ${type}`);
  const byId = new Map(ix.methods.map((r, i) => [r.k, i]));
  const roots: number[] = [];
  for (const name of ['Test', 'CalculateScore']) {
    for (let c: TypeDef | undefined = t; c; c = baseOf(c)) {
      const ms = c.methods.filter((m) => m.name === name && !(m.flags & ABSTRACT));
      if (ms.length) { roots.push(...ms.map((m) => byId.get(m.id)!)); break; }
    }
  }
  return roots;
}
function printClosure(ix: Index, roots: number[], label: string): string {
  const r = closure(ix, roots, DEPTH, icVerdicts(), process.argv.includes('--delegates'), CUT);
  const lines = [`${label}: worst ${r.worst}; closure ${r.size} methods (depth ${DEPTH}); roots ${roots.map((i) => ix.methods[i].k).join(' | ')}`];
  for (const h of r.tags) lines.push(`  ${h.tag} x${r.counts.get(h.tag)}: ${h.detail}\n    path: ${h.path.map((i) => ix.methods[i].k.replace(/\(.*$/s, '')).join(' > ')}${h.edge ? ` > [${h.edge}]` : ''}`);
  return lines.join('\n');
}

// ---------- self-check ----------
function selfCheck(): void {
  // Synthetic: compressed ints and an IL stream (ldarg.0; call tok; ldc.i4.s 5; switch(2); ldfld tok; ret).
  const c = Buffer.from([0x03, 0x80, 0x80, 0xc0, 0x00, 0x40, 0x00]);
  if (cuint(c, 0)[0] !== 3 || cuint(c, 1)[0] !== 0x80 || cuint(c, 3)[0] !== 0x4000) throw new Error('selfcheck: cuint');
  const il = Buffer.from([0x02, 0x28, 1, 0, 0, 6, 0x1f, 5, 0x45, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x7b, 2, 0, 0, 4, 0xfe, 0x06, 3, 0, 0, 6, 0x2a]);
  const ins = decodeIl(il, 0, il.length);
  if (ins.length !== 3 || ins[0].tok !== 0x06000001 || ins[1].op !== 0x7b || ins[2].op !== 0xfe06) throw new Error('selfcheck: IL decode');
  if (!existsSync(ASM_DIR)) { console.log('self-check: synthetic ok; no assemblies at', ASM_DIR, '(real-data checks skipped)'); return; }
  // Real data.
  const ix = build();
  const ids = new Map(ix.methods.map((r, i) => [r.k, i]));
  const fail = (m: string) => { throw new Error('selfcheck: ' + m); };
  if (ix.stats.ilErrors) fail(`${ix.stats.ilErrors} IL decode errors`);
  const ics = icVerdicts();
  const icBound = new Set(ix.methods.filter((r) => r.ic).map((r) => r.ic!));
  const rows = [...ics.keys()].filter((k) => !k.startsWith("*::"));
  const missing = rows.filter((k) => !icBound.has(k));
  if (missing.length > rows.length * 0.02) fail(`${missing.length} ic-safety rows not bound to an IC method, e.g. ${missing.slice(0, 3)}`);
  const cs = lookup(ix, 'Sims3.Gameplay.Autonomy.Autonomy::CalculateScore');
  if (!cs.length) fail('Autonomy.CalculateScore not found');
  const callers = ix.methods.flatMap((r, i) => { for (let k = 0; k < r.c.length; k += 2) if (cs.includes(r.c[k])) return [i]; return []; });
  if (!callers.some((i) => ix.methods[i].k === 'Sims3.Gameplay.Autonomy.Autonomy::CalculateScoreAndAddToCandidates(Sims3.Gameplay.Autonomy.InteractionObjectPair)')) fail(`CalculateScore callers miss C1 (docs/59): ${callers.map((i) => ix.methods[i].k).slice(0, 5)}`);
  const sends = lookup(ix, 'Sims3.Gameplay.EventSystem.EventTracker::SendEvent');
  if (sends.length < 4) fail(`EventTracker.SendEvent overloads ${sends.length} < 4 (overloads not split?)`);
  const sleep = lookup(ix, 'Sims3.SimIFace.Simulator::Sleep');
  if (!sleep.length) fail('Simulator.Sleep missing');
  const sc = closure(ix, sleep, 4, ics);
  if (![...sc.reached.keys()].some((i) => ix.methods[i].ic === 'ScriptCore.Simulator::Simulator_SleepImpl')) fail('Simulator.Sleep does not reach Simulator_SleepImpl (interface resolution)');
  if (!ids.size) fail('no ids');
  console.log(`self-check ok: ${ix.stats.methods} methods, ${ix.stats.ics} ICs (${rows.length - missing.length}/${rows.length} csv rows bound), CalculateScore callers ${callers.length}, SendEvent overloads ${sends.length}, Sleep reaches SleepImpl`);
  for (const i of callers.slice(0, 8)) console.log('  caller:', ix.methods[i].k);
}

// ---------- main ----------
const VALUE_FLAGS = new Set(['--asm', '--out', '--index', '--ic', '--depth', '--cut']);
function positional(): string[] {
  const out: string[] = []; const a = process.argv.slice(3);
  for (let i = 0; i < a.length; i++) { if (VALUE_FLAGS.has(a[i])) i++; else if (!a[i].startsWith('--')) out.push(a[i]); }
  return out;
}
const cmd = process.argv[2];
if (cmd === '--self-check' || process.argv.includes('--self-check')) selfCheck();
else if (cmd === 'build') {
  const t0 = Date.now(); const ix = build();
  mkdirSync(OUT, { recursive: true });
  writeFileSync(join(OUT, 'index.json'), JSON.stringify(ix));
  const csv = ['method,assembly,ic,calls,virt_unresolved,instance_reads,instance_writes,static_reads,static_writes'];
  for (const r of ix.methods) {
    if (r.a < 0) continue;
    let v = 0; for (let k = 1; k < r.c.length; k += 2) if (['virt', 'iface', 'cons', 'dinv'].includes(ix.kinds[r.c[k]])) v++;
    csv.push([JSON.stringify(r.k), ix.asms[r.a].name, r.ic ?? '', r.c.length / 2, v, r.fr.length, r.fw.length, r.sr.length, r.sw.length].join(','));
  }
  writeFileSync(join(OUT, 'methods.csv'), csv.join('\n') + '\n');
  console.log(JSON.stringify(ix.stats), `${((Date.now() - t0) / 1000).toFixed(1)}s`);
} else if (cmd === 'query' || cmd === 'def') {
  const ix = loadIndex();
  for (const a of positional()) {
    const roots = cmd === 'def' ? defRoots(ix, a) : lookup(ix, a);
    if (!roots.length) { console.log(`${a}: no method`); continue; }
    console.log(printClosure(ix, roots, a));
  }
} else if (cmd === 'callers') {
  const ix = loadIndex(); const ts = new Set(lookup(ix, process.argv[3]));
  ix.methods.forEach((r, i) => { for (let k = 0; k < r.c.length; k += 2) if (ts.has(r.c[k])) { console.log(`${ix.kinds[r.c[k + 1]]}\t${r.k}`); break; } });
} else console.log('usage: build | query <method>... | def <type>... | callers <method> | --self-check');
