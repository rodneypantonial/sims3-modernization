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
//   2. Native: [ProbeSnap] pairs (ts3native.log "vcall SITE -> TARGET n=N") at the sites of the armed table (default
//      native/ts3native/vcallsites5.h, docs/133 §6; pass vcallsites4.h for the glue-only job), classed as
//      tools/indirect-live-vs-static.ts does, per group: glue (docs/data/mono-glue-sites.csv, docs/131) and routing
//      (docs/data/routing-indirect-sites.csv, docs/102/106/107). Per group: armed/fired, static % resolved and % with live
//      answers; I-131-12 = no 'new' pair at a "receiver typed (caller)" site; I-107-12 = none at a docs/107 row.
//      --sites-out: one TSV row per armed or fired site (static status, fired / not fired, live targets with their class).
//      --db [file]: the cumulative CallSites coverage DB (tools/callsite-db.ts, default probe-out/callsites/db.json, docs/133 §7)
//      as the native input (with or without --log): the same per-group % resolved, plus runs, trap cost per run and the
//      armed sites that never fired in any run.
// Usage: node tools/virtual-resolution-report.ts --vres <tag>-vres.json --edges <edges.json> [--log <ts3native.log> ...]
//          [--sites-csv docs/data/mono-glue-sites.csv] [--routing-csv docs/data/routing-indirect-sites.csv]
//          [--table native/ts3native/vcallsites5.h] [--sites-out <file.tsv>] [--list 20] [--db [probe-out/callsites/db.json]]
//        node tools/virtual-resolution-report.ts --self-check
// Edges: node tools/autonomy-virtual-closure.ts --asm <repo>/game-copy/assemblies --edges <edges.json> > <scratch>.csv
// Live keys are "Namespace.Outer+Inner::Name" (no parameter list), so overloads of one name share a key [limit].
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';
import { parseProbe, parseCsv, classify } from './indirect-live-vs-static.ts';
import { DB_DEFAULT, dbToLog, loadDb, type Db } from './callsite-db.ts';

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

type Group = { name: string; csvText: string };
type Verdict = { armed: number; fired: number; newPairs: number };
const pct = (a: number, b: number) => (b ? (100 * a / b).toFixed(1) : '0.0') + '%';
const hx = (a: number) => `0x${a.toString(16).toUpperCase()}`;
// Per group (glue = docs/131 sites, routing = docs/102/106/107 sites; a site in both CSVs counts in both): every non-import
// row is the denominator; static % resolved from the CSV status, then the unresolved rows that fired (first live answer) and
// the partial rows that fired (with or without a target outside the static set).
export function compareNative(logs: string[], groups: Group[], tableText: string) {
  const armed = new Set([...tableText.matchAll(/\{0x([0-9A-F]+), \d+, \{/gi)].map((m) => parseInt(m[1], 16)));
  const merged = new Map<string, ReturnType<typeof parseProbe>[number]>();
  for (const t of logs) for (const p of parseProbe(t)) { const k = `${p.site}:${p.target}`, q = merged.get(k); merged.set(k, q ? { ...q, n: q.n + p.n } : p); }
  const bySite = new Map<number, ReturnType<typeof parseProbe>>();
  for (const p of merged.values()) bySite.set(p.site, [...(bySite.get(p.site) ?? []), p]);
  const sites: string[][] = [], news: string[] = [];
  const out = groups.map(({ name, csvText }) => {
    const rows = [...parseCsv(csvText).values()].filter((r) => r.kind !== 'import');
    const g = { name, rows: rows.length, armed: 0, armedFired: 0, staticResolved: 0, partial: 0, unresolved: 0, unresolvedFired: 0, partialFired: 0, partialNew: 0,
      pairs: new Map<string, number>(), callerTyped: { armed: 0, fired: 0, newPairs: 0 } as Verdict, d107: { armed: 0, fired: 0, newPairs: 0 } as Verdict };
    for (const r of rows) {
      const ps = [...(bySite.get(r.site) ?? [])].sort((a, b) => b.n - a.n), cls = ps.map((p) => classify(p, r)), on = armed.has(r.site);
      const nNew = cls.filter((c) => c === 'new').length, ct = r.note.startsWith('[receiver typed (caller)]'), d107 = r.note.includes('docs/107');
      const partial = r.status !== 'resolved' && r.status !== 'unresolved';
      if (r.status === 'resolved') g.staticResolved++; else if (partial) g.partial++; else g.unresolved++;
      if (ps.length && !partial && r.status === 'unresolved') g.unresolvedFired++;
      if (ps.length && partial) { g.partialFired++; if (nNew) g.partialNew++; }
      cls.forEach((c) => bump(g.pairs, `${c} / ${r.status}`));
      for (const [v, flag] of [[g.callerTyped, ct], [g.d107, d107]] as const) if (flag && on) { v.armed++; if (ps.length) v.fired++; v.newPairs += nNew; }
      if (on) { g.armed++; if (ps.length) g.armedFired++; }
      ps.forEach((p, i) => { if (cls[i] === 'new') news.push(`${name}\t${hx(p.site)}\t${r.caller}\t-> ${hx(p.target)} n=${p.n}${ct ? '\tcaller-typed' : ''}${d107 ? '\tdocs/107' : ''}\t${r.note}`); });
      if (on || ps.length) sites.push([name, hx(r.site), r.caller, r.status, String(r.targets.size + r.modules.size), ps.length ? 'fired' : 'not fired',
        ps.map((p, i) => `${hx(p.target)}${p.out ? ` out=${p.out}` : ''} n=${p.n} ${cls[i]}`).join('; ')]);
    }
    return g;
  });
  return { armed: armed.size, armedFired: [...armed].filter((s) => bySite.has(s)).length, firedSites: bySite.size, groups: out, news, sites };
}

// --db: runs and trap cost, then per group the armed sites no run has fired (docs/133 §7)
export function dbLines(db: Db, n: ReturnType<typeof compareNative>, list: number): string[] {
  const runs = Object.entries(db.runs), out = [`CALLSITE DB: ${runs.length} runs, ${Object.keys(db.sites).length} sites seen`];
  for (const [id, r] of runs.slice(-list)) out.push(`  run ${id}${r.job ? ` (${r.job})` : ''}: ${r.pairs} pairs${r.armed !== undefined ? `, ${r.armed} armed, ${r.trapNs} ns per trap` : ''}${r.trapMs !== undefined ? `, trap ${r.trapMs} ms in ${r.minutes} min` : ''}`);
  for (const g of new Set(n.sites.map((r) => r[0]))) {
    const never = n.sites.filter((r) => r[0] === g && r[5] === 'not fired').map((r) => r[1]);
    out.push(`  ${g}: ${never.length} armed sites never fired${never.length ? `: ${never.slice(0, list).join(' ')}${never.length > list ? ` +${never.length - list}` : ''}` : ''}`);
  }
  return out;
}

// one TSV row per armed or fired site and group: the per-site answer (docs/133 §6)
export function sitesTsv(n: ReturnType<typeof compareNative>) {
  return ['group\tsite\tcaller\tstatic status\tstatic targets\tfired\tlive targets (target n=N class)', ...n.sites.map((r) => r.join('\t'))].join('\n') + '\n';
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
    console.log(`NATIVE ([ProbeSnap]): ${n.armed} armed, ${n.armedFired} fired; ${n.firedSites} sites with a pair`);
    const verdict = (v: Verdict, fail: string) => `${v.armed} armed, ${v.fired} fired, ${v.newPairs} live targets outside the static set -> ${v.fired ? (v.newPairs ? `FAIL (${fail})` : 'pass') : 'not answered (none fired)'}`;
    for (const g of n.groups) {
      console.log(`  ${g.name}: ${g.rows} indirect rows (imports left out); armed ${g.armed}, fired ${g.armedFired} (${pct(g.armedFired, g.armed)} of armed)`);
      console.log(`    static: resolved ${g.staticResolved} (${pct(g.staticResolved, g.rows)}), partial ${g.partial}, unresolved ${g.unresolved}; live: unresolved that fired ${g.unresolvedFired}, partial that fired ${g.partialFired} (${g.partialNew} with a new target)`);
      console.log(`    % resolved: static ${pct(g.staticResolved, g.rows)} -> with live answers ${pct(g.staticResolved + g.unresolvedFired, g.rows)}; some target known ${pct(g.staticResolved + g.partial + g.unresolvedFired, g.rows)}; still unresolved ${pct(g.unresolved - g.unresolvedFired, g.rows)}`);
      for (const [k, c] of [...g.pairs].sort()) console.log(`    pairs ${k}: ${c}`);
      if (g.callerTyped.armed) console.log(`    I-131-12 caller-typed sites: ${verdict(g.callerTyped, 'demote those sites to partial')}`);
      if (g.d107.armed) console.log(`    I-107-12 docs/107 rows: ${verdict(g.d107, 'RoutingPathImp is not the only receiver there')}`);
    }
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
  const rcsv = 'site,caller,kind,slot offset,targets,status,note\n' +
    '0x300,0x2F0 C,reg,,,unresolved,[register] | hop 4; groups b\n' +
    '0x400,0x3F0 D,vcall,+0x8,0x800,partly resolved,[family] x\n' +
    '0x500,0x4F0 E,vcall,+0xC,0xA00,resolved,[receiver class (register trace)] | docs/107 §12\n' +
    '0x600,0x5F0 F,import,,MSVCR80!memmove,resolved,[import]\n' +
    '0x700,0x6F0 G,vcall,+0x4,,unresolved,[x]\n';
  const table2 = table.replace('};', '    {0x000400, 3, {0xFF, 0x50, 0x08}},\n    {0x000500, 3, {0xFF, 0x50, 0x0C}},\n    {0x000700, 3, {0xFF, 0x50, 0x04}},\n};');
  const log = 'vcall 0x00000100 -> 0x00000500 n=4\nvcall 0x00000100 -> 0x00000501 n=1\nvcall 0x00000300 -> 0x00000900 n=2 thr=job:2,worker:0,sim:0,other:0\n' +
    'vcall 0x00000400 -> 0x00000800 n=3\nvcall 0x00000500 -> 0x00000A00 n=7\nvcall 0x00000500 -> 0x00000A00 n=9';
  const n = compareNative([log], [{ name: 'glue', csvText: csv }, { name: 'routing', csvText: rcsv }], table2);
  assert.equal(n.armed, 6); assert.equal(n.armedFired, 4);
  const [gl, ro] = n.groups;
  assert.deepEqual(gl.callerTyped, { armed: 1, fired: 1, newPairs: 1 });
  assert.deepEqual(Object.fromEntries(gl.pairs), { 'match / resolved': 1, 'new / resolved': 1, 'resolves / unresolved': 1 });
  assert.equal(gl.rows, 3); assert.equal(gl.armedFired, 2); assert.equal(gl.staticResolved, 1); assert.equal(gl.unresolvedFired, 1);
  assert.equal(ro.rows, 4); assert.equal(ro.armed, 4); assert.equal(ro.armedFired, 3); // import row left out; 0x300 counts in both groups
  assert.equal(ro.unresolved, 2); assert.equal(ro.unresolvedFired, 1); assert.equal(ro.partialFired, 1); assert.equal(ro.partialNew, 0);
  assert.deepEqual(ro.d107, { armed: 1, fired: 1, newPairs: 0 });
  const tsv = sitesTsv(n).split('\n').slice(0, -1);
  assert.equal(tsv.length, 1 + 3 + 4);
  assert.ok(tsv.includes('routing\t0x700\t0x6F0 G\tunresolved\t0\tnot fired\t'));
  assert.ok(tsv.includes('routing\t0x500\t0x4F0 E\tresolved\t1\tfired\t0xA00 n=9 match')); // last cumulative count of one log wins
  const db: Db = { version: 1, runs: { r1: { mergedAt: 't', pairs: 1, armed: 6, trapNs: 4000, minutes: 3, trapMs: 2 } },
    sites: { '0x100': { runs: 1, lastRun: 'r1', hits: 4, targets: { '0x500': { n: 4, lastRun: 'r1', lastN: 4 } } } } };
  const nd = compareNative([dbToLog(db)], [{ name: 'glue', csvText: csv }, { name: 'routing', csvText: rcsv }], table2);
  assert.equal(nd.armedFired, 1);
  assert.deepEqual(dbLines(db, nd, 2), ['CALLSITE DB: 1 runs, 1 sites seen', '  run r1: 1 pairs, 6 armed, 4000 ns per trap, trap 2 ms in 3 min',
    '  glue: 2 armed sites never fired: 0x200 0x300', '  routing: 4 armed sites never fired: 0x300 0x400 +2']);
  console.log('virtual-resolution-report self-check ok');
}

// --db with no file: the default DB (parseArgs wants a value, so a bare --db is rewritten first)
const argv = process.argv.slice(2).flatMap((a, i, all) => (a === '--db' && (i + 1 >= all.length || all[i + 1].startsWith('--')) ? ['--db', DB_DEFAULT] : [a]));
const { values } = parseArgs({ args: argv, options: {
  'self-check': { type: 'boolean' }, vres: { type: 'string' }, edges: { type: 'string' }, log: { type: 'string', multiple: true },
  'sites-csv': { type: 'string', default: join(ROOT, 'docs', 'data', 'mono-glue-sites.csv') },
  'routing-csv': { type: 'string', default: join(ROOT, 'docs', 'data', 'routing-indirect-sites.csv') },
  table: { type: 'string', default: join(ROOT, 'native', 'ts3native', 'vcallsites5.h') }, list: { type: 'string', default: '20' },
  'sites-out': { type: 'string' }, db: { type: 'string' } } });
if (values['self-check']) selfCheck();
else if (!(values.vres && values.edges) && !values.log?.length && !values.db) { console.error('usage: node tools/virtual-resolution-report.ts --vres <tag>-vres.json --edges <edges.json> [--log <ts3native.log> ...] [--sites-csv f] [--routing-csv f] [--table f] [--sites-out f.tsv] [--list N] | --self-check'); process.exitCode = 2; }
else {
  const read = (f: string) => { if (!existsSync(f)) throw new Error(`missing ${f}`); return readFileSync(f, 'utf8'); };
  const m = values.vres && values.edges ? compareManaged(JSON.parse(read(values.vres)), JSON.parse(read(values.edges))) : null;
  const groups = [{ name: 'glue', csvText: read(values['sites-csv']!) }, { name: 'routing', csvText: read(values['routing-csv']!) }];
  const db = values.db ? loadDb(values.db) : null;
  const logs = [...(values.log ?? []).map(read), ...(db ? [dbToLog(db)] : [])];
  const n = logs.length ? compareNative(logs, groups, read(values.table!)) : null;
  print(m, n, Number(values.list));
  if (db && n) for (const l of dbLines(db, n, Number(values.list))) console.log(l);
  if (n && values['sites-out']) { writeFileSync(values['sites-out'], sitesTsv(n)); console.log(`wrote ${values['sites-out']} (${n.sites.length} rows)`); }
}
