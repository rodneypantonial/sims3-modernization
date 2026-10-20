// docs/133: live check of the static virtual-call resolution, two comparisons from one probe job
// (tools/harness/queues/virtual-resolution.json).
//   1. Managed: the mint callvirt inline caches read by the probe agent (/vcache, <tag>-vres.json: per call site the targets
//      seen over the snapshots) against the typed closures of docs/127 (allow-list v0 + tier 2; edges exported by
//      tools/autonomy-virtual-closure.ts --edges). Per live (caller, declared) pair in a closure method, each live target is
//        match        in the typed closure's targets for that call
//        typed-miss   not typed, but a class-hierarchy override (CHA): the receiver typing dropped a real target (soundness gap)
//        cha-miss     not even in CHA (mod override, generic or BCL target the index does not expand): hierarchy gap
//        bcl-decl     declared on a BCL type (CHA does not expand those, docs/127 §1): reported apart
//        beyond-hops  the caller sits >= hops calls from a root, where the closure keeps the declared body only (by design)
//        no-edge      no static callvirt edge for (caller, declared) in the closure (naming or devirtualisation mismatch)
//      and per static edge within the hop bound with > 1 typed target: the targets never seen live (over-approximation).
//   2. Native: [ProbeSnap] pairs (ts3native.log "vcall SITE -> TARGET n=N") at the docs/131 glue sites that are unresolved,
//      partial or caller-typed (docs/data/mono-glue-sites.csv, table native/ts3native/vcallsites4.h), classed as
//      tools/indirect-live-vs-static.ts does; I-131-12 = no 'new' pair at a "receiver typed (caller)" site.
// Usage: node tools/virtual-resolution-report.ts --vres <tag>-vres.json --edges <edges.json> [--log <ts3native.log> ...]
//          [--sites-csv docs/data/mono-glue-sites.csv] [--table native/ts3native/vcallsites4.h] [--list 20]
//        node tools/virtual-resolution-report.ts --self-check
// Edges: node tools/autonomy-virtual-closure.ts --asm <repo>/game-copy/assemblies --edges <edges.json> > <scratch>.csv
// Live keys are "Namespace.Outer+Inner::Name" (no parameter list), so overloads of one name share a key [limit].
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';
import { parseProbe, parseCsv, classify } from './indirect-live-vs-static.ts';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
type Vres = { snaps: number; sites: { caller: string; off: number; decl: string; snaps: number; targets: Record<string, number>; classes: Record<string, number> }[]; stats?: Record<string, number> };
type EdgeFile = { hops: number; methods: { k: string; groups: string[] }[]; edges: { caller: string; decl: string; kind: string; d: number; typed: string[]; cha: string[] }[] };
// index key -> live key: no parameter list, no generic-method arity (the index writes GetObjects`1, metadata has GetObjects)
const strip = (k: string) => k.replace(/\(.*$/s, '').replace(/`\d+$/, '');
const bump = (m: Map<string, number>, k: string, n = 1) => m.set(k, (m.get(k) ?? 0) + n);

export function compareManaged(v: Vres, e: EdgeFile) {
  const closure = new Map<string, Set<string>>();
  for (const m of e.methods) { const k = strip(m.k); const g = closure.get(k) ?? new Set(); m.groups.forEach((x) => g.add(x)); closure.set(k, g); }
  const edges = new Map<string, { d: number; typed: Set<string>; cha: Set<string> }>();
  for (const x of e.edges) {
    const k = `${strip(x.caller)}|${strip(x.decl)}`, o = edges.get(k) ?? { d: x.d, typed: new Set(), cha: new Set() };
    o.d = Math.min(o.d, x.d); x.typed.forEach((t) => o.typed.add(strip(t))); x.cha.forEach((t) => o.cha.add(strip(t))); edges.set(k, o);
  }
  const live = new Map<string, Map<string, number>>(); // caller|decl -> target -> snapshots
  let liveSites = 0, inClosure = 0;
  for (const s of v.sites) {
    liveSites++;
    if (!closure.has(s.caller)) continue;
    inClosure++;
    const k = `${s.caller}|${s.decl}`, m = live.get(k) ?? new Map();
    for (const [t, n] of Object.entries(s.targets)) bump(m, t, n);
    live.set(k, m);
  }
  const pairs = new Map<string, number>(), rows: { cls: string; caller: string; decl: string; target: string; n: number; d: number | null }[] = [];
  for (const [k, m] of live) {
    const [caller, decl] = k.split('|'), ed = edges.get(k);
    for (const [t, n] of m) {
      const cls = !ed ? 'no-edge' : ed.typed.has(t) ? 'match' : ed.d >= e.hops ? 'beyond-hops' : ed.cha.has(t) ? 'typed-miss' : /^(System|Mono)\./.test(decl) ? 'bcl-decl' : 'cha-miss';
      bump(pairs, cls);
      if (cls !== 'match') rows.push({ cls, caller, decl, target: t, n, d: ed ? ed.d : null });
    }
  }
  const over: { caller: string; decl: string; typed: number; seen: number; unseen: string[] }[] = [];
  let edgesIn = 0, edgesSeen = 0, typedSum = 0, seenSum = 0;
  for (const [k, ed] of edges) {
    if (ed.d >= e.hops || ed.typed.size < 2) continue;
    edgesIn++;
    const m = live.get(k);
    if (!m) continue;
    edgesSeen++;
    const unseen = [...ed.typed].filter((t) => !m.has(t));
    typedSum += ed.typed.size; seenSum += ed.typed.size - unseen.length;
    const [caller, decl] = k.split('|');
    if (unseen.length) over.push({ caller, decl, typed: ed.typed.size, seen: ed.typed.size - unseen.length, unseen });
  }
  over.sort((a, b) => b.unseen.length - a.unseen.length);
  return { snaps: v.snaps, liveSites, inClosure, liveCalls: live.size, closureMethods: closure.size, staticEdges: edges.size, pairs, rows, edgesIn, edgesSeen, typedSum, seenSum, over };
}

export function compareNative(logs: string[], csvText: string, tableText: string) {
  const rows = parseCsv(csvText);
  const armed = new Set([...tableText.matchAll(/\{0x([0-9A-F]+), \d+, \{/gi)].map((m) => parseInt(m[1], 16)));
  const merged = new Map<string, ReturnType<typeof parseProbe>[number]>();
  for (const t of logs) for (const p of parseProbe(t)) { const k = `${p.site}:${p.target}`, q = merged.get(k); merged.set(k, q ? { ...q, n: q.n + p.n } : p); }
  const pairs = new Map<string, number>(), fired = new Set<number>(), news: string[] = [], callerTyped = { armed: 0, fired: 0, newPairs: 0 };
  const isCallerTyped = (s: number) => rows.get(s)?.note.startsWith('[receiver typed (caller)]') ?? false;
  for (const p of merged.values()) {
    const r = rows.get(p.site), c = classify(p, r);
    fired.add(p.site);
    bump(pairs, `${c}${r ? ' / ' + r.status : ''}`);
    if (c === 'new') news.push(`0x${p.site.toString(16).toUpperCase()}\t${r?.caller ?? ''}\t-> 0x${p.target.toString(16).toUpperCase()} n=${p.n}${isCallerTyped(p.site) ? '\tcaller-typed' : ''}\t${r?.note ?? ''}`);
    if (c === 'new' && isCallerTyped(p.site)) callerTyped.newPairs++;
  }
  for (const s of armed) if (isCallerTyped(s)) { callerTyped.armed++; if (fired.has(s)) callerTyped.fired++; }
  const armedFired = [...armed].filter((s) => fired.has(s)).length;
  return { armed: armed.size, armedFired, firedSites: fired.size, pairs, news, callerTyped };
}

function print(m: ReturnType<typeof compareManaged> | null, n: ReturnType<typeof compareNative> | null, list: number) {
  if (m) {
    console.log(`MANAGED (callvirt inline caches, ${m.snaps} snapshots): ${m.liveSites} live sites, ${m.inClosure} in ${m.closureMethods} closure methods, ${m.liveCalls} (caller, declared) pairs; ${m.staticEdges} static callvirt edges`);
    for (const [k, c] of [...m.pairs].sort()) console.log(`  live targets ${k}: ${c}`);
    const gaps = m.rows.filter((r) => r.cls === 'typed-miss' || r.cls === 'cha-miss');
    console.log(`  soundness gaps (typed-miss + cha-miss): ${gaps.length}${gaps.length ? '' : ' (none)'}`);
    for (const cls of ['typed-miss', 'cha-miss', 'bcl-decl', 'no-edge', 'beyond-hops']) {
      const rs = m.rows.filter((r) => r.cls === cls).sort((a, b) => b.n - a.n);
      if (rs.length) console.log(`  ${cls} (${rs.length}${rs.length > list ? `, first ${list}` : ''}):`);
      for (const r of rs.slice(0, list)) console.log(`    ${r.caller} -> ${r.decl} => ${r.target} (seen ${r.n}${r.d === null ? '' : `, depth ${r.d}`})`);
    }
    console.log(`  over-approximation: ${m.edgesSeen} of ${m.edgesIn} multi-target edges within the hop bound were live; their typed targets ${m.typedSum}, seen ${m.seenSum} (${m.typedSum ? Math.round(100 * m.seenSum / m.typedSum) : 0}%); ${m.over.length} edges with unseen targets${m.over.length > list ? `, first ${list}` : ''}:`);
    for (const o of m.over.slice(0, list)) console.log(`    ${o.caller} -> ${o.decl}: ${o.seen}/${o.typed} seen; unseen ${o.unseen.slice(0, 4).join(', ')}${o.unseen.length > 4 ? `, +${o.unseen.length - 4}` : ''}`);
  }
  if (n) {
    console.log(`NATIVE ([ProbeSnap] glue sites): ${n.armed} armed, ${n.armedFired} fired; ${n.firedSites} sites with a pair`);
    for (const [k, c] of [...n.pairs].sort()) console.log(`  pairs ${k}: ${c}`);
    console.log(`  I-131-12 caller-typed sites: ${n.callerTyped.armed} armed, ${n.callerTyped.fired} fired, ${n.callerTyped.newPairs} live targets outside the static set -> ${n.callerTyped.fired ? (n.callerTyped.newPairs ? 'FAIL (demote those sites to partial)' : 'pass') : 'not answered (no caller-typed site fired)'}`);
    if (n.news.length) console.log(`  new pairs (resolved or partial site, target not predicted)${n.news.length > list ? `, first ${list}` : ''}:`);
    for (const l of n.news.slice(0, list)) console.log(`    ${l}`);
  }
}

function selfCheck() {
  const e: EdgeFile = { hops: 5, methods: [{ k: 'A::Root()', groups: ['v0'] }, { k: 'A::Deep()', groups: ['tier2'] }, { k: 'A::Bcl()', groups: ['v0'] }], edges: [
    { caller: 'A::Root()', decl: 'I::M(int)', kind: 'iface', d: 0, typed: ['I::M(int)', 'X::M(int)', 'Y::M(int)'], cha: ['X::M(int)', 'Y::M(int)', 'Z::M(int)'] },
    { caller: 'A::Deep()', decl: 'B::N()', kind: 'virt', d: 7, typed: ['B::N()'], cha: ['C::N()'] },
    { caller: 'A::Bcl()', decl: 'System.Object::ToString()', kind: 'virt', d: 1, typed: ['System.Object::ToString()'], cha: [] },
    { caller: 'A::Root()', decl: 'L`1::Get`1(int)', kind: 'cv', d: 0, typed: ['L`1::Get`1(int)'], cha: [] },
  ] };
  const v: Vres = { snaps: 3, sites: [
    { caller: 'A::Root', off: 10, decl: 'I::M', snaps: 3, targets: { 'X::M': 3, 'Z::M': 1 }, classes: {} },
    { caller: 'A::Root', off: 40, decl: 'I::M', snaps: 1, targets: { 'W::M': 1 }, classes: {} },
    { caller: 'A::Deep', off: 4, decl: 'B::N', snaps: 2, targets: { 'C::N': 2 }, classes: {} },
    { caller: 'A::Bcl', off: 2, decl: 'System.Object::ToString', snaps: 1, targets: { 'G::ToString': 1 }, classes: {} },
    { caller: 'A::Root', off: 50, decl: 'Q::R', snaps: 1, targets: { 'Q::R': 1 }, classes: {} },
    { caller: 'Other::F', off: 0, decl: 'I::M', snaps: 1, targets: { 'X::M': 1 }, classes: {} },
    { caller: 'A::Root', off: 60, decl: 'L`1::Get', snaps: 1, targets: { 'L`1::Get': 1 }, classes: {} },
  ] };
  const m = compareManaged(v, e);
  assert.equal(m.inClosure, 6);
  assert.deepEqual(Object.fromEntries(m.pairs), { match: 2, 'typed-miss': 1, 'cha-miss': 1, 'beyond-hops': 1, 'bcl-decl': 1, 'no-edge': 1 });
  assert.equal(m.rows.find((r) => r.cls === 'typed-miss')!.target, 'Z::M');
  assert.equal(m.rows.find((r) => r.cls === 'cha-miss')!.target, 'W::M');
  assert.equal(m.edgesIn, 1); assert.equal(m.edgesSeen, 1);
  assert.deepEqual(m.over[0].unseen, ['I::M', 'Y::M']);
  const csv = 'site,caller,kind,slot offset,targets,status,note\n' +
    '0x100,0x0F0 A,slot,,0x500 X,resolved,[receiver typed (caller)] | hop 2; groups a\n' +
    '0x200,0x1F0 B,slot,,0x700,partial,"[receiver typed, some sources untyped (caller)] | hop 3; groups a"\n' +
    '0x300,0x2F0 C,reg,,,unresolved,[register] | hop 4; groups b\n';
  const table = 'static const VcSiteDef kVcSites[] = {\n    {0x000100, 3, {0xFF, 0x50, 0x08}},\n    {0x000200, 2, {0xFF, 0xD0}},\n    {0x000300, 2, {0xFF, 0xD1}},\n};\n';
  const n = compareNative(['vcall 0x00000100 -> 0x00000500 n=4\nvcall 0x00000100 -> 0x00000501 n=1\nvcall 0x00000300 -> 0x00000900 n=2'], csv, table);
  assert.equal(n.armed, 3); assert.equal(n.armedFired, 2);
  assert.deepEqual(n.callerTyped, { armed: 1, fired: 1, newPairs: 1 });
  assert.deepEqual(Object.fromEntries(n.pairs), { 'match / resolved': 1, 'new / resolved': 1, 'resolves / unresolved': 1 });
  console.log('virtual-resolution-report self-check ok');
}

const { values } = parseArgs({ options: {
  'self-check': { type: 'boolean' }, vres: { type: 'string' }, edges: { type: 'string' }, log: { type: 'string', multiple: true },
  'sites-csv': { type: 'string', default: join(ROOT, 'docs', 'data', 'mono-glue-sites.csv') },
  table: { type: 'string', default: join(ROOT, 'native', 'ts3native', 'vcallsites4.h') }, list: { type: 'string', default: '20' } } });
if (values['self-check']) selfCheck();
else if (!(values.vres && values.edges) && !values.log?.length) { console.error('usage: node tools/virtual-resolution-report.ts --vres <tag>-vres.json --edges <edges.json> [--log <ts3native.log> ...] [--sites-csv f] [--table f] [--list N] | --self-check'); process.exitCode = 2; }
else {
  const read = (f: string) => { if (!existsSync(f)) throw new Error(`missing ${f}`); return readFileSync(f, 'utf8'); };
  const m = values.vres && values.edges ? compareManaged(JSON.parse(read(values.vres)), JSON.parse(read(values.edges))) : null;
  const n = values.log?.length ? compareNative(values.log.map(read), read(values['sites-csv']!), read(values.table!)) : null;
  print(m, n, Number(values.list));
}
