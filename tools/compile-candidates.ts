// docs/207: how much sim-thread interpreter time sits in managed methods that never yield (compile candidates for an
// ahead-of-time / at-load compiler of non-yielding methods, docs/29 option d).
// Usage (cwd = repo root with game-copy/ and probe-out/; static, reads only):
//   node tools/compile-candidates.ts --run bp=<a-managed.jsonl>[+<b.jsonl>] --run ip=... --mono bp=334.3,ip=335.7,sv=358.0
//        [--csv docs/data/compile-candidates.csv] [--index game-copy/analysis/callgraph/index.json] [--asm game-copy/assemblies]
//   node tools/compile-candidates.ts --self-check
// Classes per method (managed-callgraph.ts index): Y = can yield (own YIELD tag, or any callee incl. every game override of a
// virt/iface/cons edge), D = no yield except through a delegate Invoke (target unknown), N = never yields, IC = native binding,
// ? = frame not in the index (mod or unmatched). A frame name maps to every overload; the worst class wins.
// Self time = leaf frame of a managed sample. Region root = outermost frame of the leaf's unbroken N/D run (the compile entry).
import { readFileSync, writeFileSync } from 'node:fs';
import { closure, icVerdicts, methodIl, ownTags, virtualTargets, type Index } from './managed-callgraph.ts';

type Cls = 'Y' | 'D' | 'N' | 'IC' | '?';
const RANK: Cls[] = ['?', 'IC', 'Y', 'D', 'N']; // worst first; an IC frame is native time, not interpreter
export const worst = (a: Cls, b: Cls): Cls => (RANK.indexOf(a) <= RANK.indexOf(b) ? a : b);

// Reverse propagation: callers of a yielding method yield; callers of a delegate invoker are D unless already Y.
// edges[i] = callee ids (virtual edges already expanded); seedY/seedD = own yield / own delegate Invoke.
export function classify(n: number, edges: number[][], seedY: Set<number>, seedD: Set<number>, ic: Set<number>): Cls[] {
  const rev: number[][] = Array.from({ length: n }, () => []);
  edges.forEach((cs, i) => cs.forEach((c) => rev[c].push(i)));
  const flood = (seed: Set<number>, skip: Set<number>) => {
    const out = new Set<number>(); const q = [...seed].filter((x) => !skip.has(x));
    q.forEach((x) => out.add(x));
    while (q.length) for (const p of rev[q.pop()!]) if (!out.has(p) && !skip.has(p)) { out.add(p); q.push(p); }
    return out;
  };
  const Y = flood(seedY, new Set()); const D = flood(seedD, Y);
  return Array.from({ length: n }, (_, i) => (ic.has(i) ? 'IC' : Y.has(i) ? 'Y' : D.has(i) ? 'D' : 'N'));
}

// Sample frame name ("Ns.Type.Method", nested types as "Outer.Inner.Method" or only "Inner.Method") from an index id.
export function frameKeys(id: string): string[] {
  const head = id.replace(/:[^:(]*$/, '').split('(')[0].replace(/`\d+$/, '');
  const [type, meth] = head.split('::');
  const full = `${type.replace(/\+/g, '.')}.${meth}`;
  const last = type.split(/[+]/).pop()!.split('.').pop()!;
  return [full, `${last}.${meth}`];
}

export type Agg = { self: Map<string, number>; root: Map<string, number>; cls: Record<string, number>; mono: number; total: number };
export function attribute(stacks: string[][], clsOf: (f: string) => Cls): Agg {
  const a: Agg = { self: new Map(), root: new Map(), cls: {}, mono: 0, total: stacks.length };
  for (const s0 of stacks) {
    const s = s0.filter((f, i) => f !== s0[i - 1]); // sampler repeats the IC stub frame
    if (!s.length || /^ScriptCore\.Route\./.test(s[0])) continue;
    const c = clsOf(s[0]);
    if (c === 'IC') continue;
    a.mono++; a.cls[c] = (a.cls[c] ?? 0) + 1;
    a.self.set(s[0], (a.self.get(s[0]) ?? 0) + 1);
    if (c === 'N' || c === 'D') {
      let j = 0; while (j + 1 < s.length && ['N', 'D'].includes(clsOf(s[j + 1]))) j++;
      a.root.set(s[j], (a.root.get(s[j]) ?? 0) + 1);
    }
  }
  return a;
}

// IL features of one body (op codes, ECMA-335 III).
export function features(ops: number[]): string {
  const n = (f: (o: number) => boolean) => ops.filter(f).length;
  const f: [string, number][] = [['call', n((o) => o === 0x28)], ['callvirt', n((o) => o === 0x6f)], ['newobj', n((o) => o === 0x73)],
    ['newarr', n((o) => o === 0x8d)], ['box', n((o) => o === 0x8c)], ['unbox', n((o) => o === 0x79 || o === 0xa5)],
    ['cast', n((o) => o === 0x74 || o === 0x75)], ['try', n((o) => o === 0xdc || o === 0xdd || o === 0xde)], ['throw', n((o) => o === 0x7a)],
    ['ldftn', n((o) => o === 0xfe06 || o === 0xfe07)], ['switch', n((o) => o === 0x45)], ['ldsfld', n((o) => o === 0x7e)],
    ['float', n((o) => (o >= 0x6b && o <= 0x6c) || o === 0x22 || o === 0x23)]];
  return f.filter(([, v]) => v).map(([k, v]) => `${k}${v}`).join(' ');
}

function selfCheck(): void {
  // 0 -> 1 -> 2(yield); 3 -> 4(dinv); 5 -> 6(ic); 7 alone
  const c = classify(8, [[1], [2], [], [4], [], [6], [], []], new Set([2]), new Set([4]), new Set([6]));
  console.assert(c.join(',') === 'Y,Y,Y,D,D,N,IC,N', 'classify');
  console.assert(frameKeys('System.Collections.Generic.List`1+Enumerator::get_Current()').join('|') === 'System.Collections.Generic.List`1.Enumerator.get_Current|Enumerator.get_Current', 'frameKeys nested');
  console.assert(frameKeys('A.B::M`1(int):int')[0] === 'A.B.M', 'frameKeys arity/ret');
  const cl: Record<string, Cls> = { a: 'N', b: 'N', c: 'Y', i: 'IC' };
  const g = attribute([['a', 'b', 'c'], ['i', 'i', 'a'], ['c'], ['ScriptCore.Route.X'], [], ['z']], (f) => cl[f] ?? '?');
  console.assert(g.mono === 3 && g.cls.N === 1 && g.cls.Y === 1 && g.cls['?'] === 1 && g.root.get('b') === 1, 'attribute');
  console.assert(features([0x28, 0x28, 0x8c, 0xdd]) === 'call2 box1 try1', 'features');
  console.log('compile-candidates self-check ok');
}

function main(): void {
  const args = process.argv.slice(2); const val = (k: string) => args.flatMap((a, i) => (a === k ? [args[i + 1]] : []));
  const runs = val('--run').map((r) => r.split('=')) as [string, string][];
  const mono = Object.fromEntries((val('--mono')[0] ?? '').split(',').filter(Boolean).map((p) => { const [k, v] = p.split('='); return [k, Number(v)]; }));
  const ix: Index = JSON.parse(readFileSync(val('--index')[0] ?? 'game-copy/analysis/callgraph/index.json', 'utf8'));
  const ics = icVerdicts(); const N = ix.methods.length;
  const seedY = new Set<number>(), seedD = new Set<number>(), ic = new Set<number>(); const edges: number[][] = [];
  for (let i = 0; i < N; i++) {
    const r = ix.methods[i]; if (r.ic) ic.add(i);
    if (ownTags(ix, i, ics).some(([t]) => t === 'YIELD')) seedY.add(i);
    const out: number[] = [];
    for (let k = 0; k < r.c.length; k += 2) {
      const t = r.c[k], kind = ix.kinds[r.c[k + 1]];
      if (kind === 'dlg') continue;
      if (kind === 'dinv') seedD.add(i);
      out.push(t);
      if (kind === 'virt' || kind === 'iface' || kind === 'cons') out.push(...virtualTargets(t).ea);
    }
    edges.push(out);
  }
  const cls = classify(N, edges, seedY, seedD, ic);
  const byKey = new Map<string, number[]>();
  ix.methods.forEach((r, i) => { if (r.a >= 0) for (const k of frameKeys(r.k)) byKey.set(k, [...(byKey.get(k) ?? []), i]); });
  const ids = (f: string) => byKey.get(f) ?? [];
  const clsOf = (f: string): Cls => { const g = ids(f); return g.length ? g.map((i) => cls[i]).reduce(worst) : '?'; };
  const why = (f: string): string => { // first yield path for a Y frame (for the report)
    const g = ids(f).filter((i) => cls[i] === 'Y'); if (!g.length) return '';
    const h = closure(ix, [g[0]], 12, ics, false, [], 12).tags.find((t) => t.tag === 'YIELD');
    return h ? `${h.path.length - 1} hops to ${h.detail.replace(/^.*::/, '')}` : 'via override';
  };
  const ilOf = new Map<string, { bytes: number; feat: string; asm: string; ov: number }>();
  const il = (f: string) => {
    if (ilOf.has(f)) return ilOf.get(f)!;
    const g = ids(f); let bytes = 0; const ops: number[] = [];
    for (const i of g) { const b = methodIl(i); if (b) { bytes += b.ins.length ? b.ins[b.ins.length - 1].next : 0; ops.push(...b.ins.map((x) => x.op)); } }
    const generic = g.some((i) => /`|!/.test(ix.methods[i].k)) ? ' generic' : '';
    const v = { bytes, feat: features(ops) + generic, asm: [...new Set(g.map((i) => ix.asms[ix.methods[i].a].name))].join('+'), ov: g.length };
    ilOf.set(f, v); return v;
  };
  const csv = ['world,kind,frame,class,samples,share_of_mono,ms_s,assembly,overloads,il_bytes,il_features,yield_path'];
  for (const [w, files] of runs) {
    const stacks = files.split('+').flatMap((p) => readFileSync(p, 'utf8').split('\n').filter(Boolean).map((l) => JSON.parse(l) as string[]));
    const a = attribute(stacks, clsOf); const ms = mono[w] ?? NaN;
    const sh = (c: string) => (a.cls[c] ?? 0) / a.mono;
    const nd = sh('N') + sh('D');
    console.log(`${w}: samples ${a.total}, managed-leaf ${a.mono}; self share N ${(100 * sh('N')).toFixed(1)}% D ${(100 * sh('D')).toFixed(1)}% Y ${(100 * sh('Y')).toFixed(1)}% ? ${(100 * sh('?')).toFixed(1)}%` +
      `; Mono ${ms} ms/s -> N+D ${(nd * ms).toFixed(1)} ms/s, gain 2x ${(nd * ms / 2).toFixed(1)} 3x ${(nd * ms * 2 / 3).toFixed(1)} (N only 2x ${(sh('N') * ms / 2).toFixed(1)} 3x ${(sh('N') * ms * 2 / 3).toFixed(1)})`);
    const rows = (kind: string, m: Map<string, number>, keep: (c: Cls) => boolean, top: number) =>
      [...m].filter(([f]) => keep(clsOf(f))).sort((x, y) => y[1] - x[1]).slice(0, top).forEach(([f, n]) => {
        const c = clsOf(f); const d = il(f);
        csv.push([w, kind, f, c, n, (n / a.mono).toFixed(4), ((n / a.mono) * ms).toFixed(1), d.asm, d.ov, d.bytes, d.feat, c === 'Y' ? why(f) : ''].map((x) => JSON.stringify(String(x))).join(','));
      });
    rows('self', a.self, (c) => c === 'N' || c === 'D', 40);
    rows('self', a.self, (c) => c === 'Y', 20);
    rows('self', a.self, (c) => c === '?', 10);
    rows('root', a.root, () => true, 20);
  }
  const out = val('--csv')[0];
  if (out) { writeFileSync(out, csv.join('\n') + '\n'); console.log(`wrote ${out} (${csv.length - 1} rows)`); }
}

if (process.argv.includes('--self-check')) selfCheck(); else main();
