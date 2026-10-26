// Memory measuring tool, report side (docs/191 module 1, docs/190 phase 1): reads the ts3native [MemDiag] lines
//   diag: mem #N <tag> [ev=<ev>] key=value[?|!] ... t=<s>     ('?' unvalidated, '!' failed the session's self-test, '-' unreadable)
//   diag: mem selftest PASS | FAIL: <fields>
//   diag: memsus #N <tag> [ev=<ev>] key=count[?|!] ... lag=<ms> t=<s>   (module 2, 0.36.74: ts3fixes' managed leak-suspect
//     counts for diag: mem sample #N, merged into that sample; diag: memsus selftest PASS | FAIL: <keys>)
// from any ts3native.log (player report, soak, queue job copy) into one JSON database keyed by run (the log's first line
// "ts3native X.Y.Z <date> <time>"), and prints growth per field over time and the deltas around events (load, travel,
// world switch, save, marks). Copies of one run (session reuse) merge by sample number, so merging twice changes nothing.
//   node tools/mem-report.ts merge <ts3native.log> [--job <id>] [--db <file>]   (default probe-out/memdiag/db.json)
//   node tools/mem-report.ts report [--db <file>] [--run <id substring>] [--log <ts3native.log>] [--min-mb 1]
//     (sections: growth, events, repeated marks = each tag + ev against its previous occurrence, forced GC)
//   node tools/mem-report.ts --self-check
// Numbers only (the lines carry no names or paths), so player logs merge without scrubbing.
import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';
import { runIdOf } from './callsite-db.ts';  // same run key as the call-site database

const ROOT = resolve(import.meta.dirname, '..');
export const DB_DEFAULT = join(ROOT, 'probe-out', 'memdiag', 'db.json');
export type Sample = { n: number; tag: string; ev?: string; t?: number; v: Record<string, number | null>; unval: string[]; failed: string[]; lag?: number };
export type Run = { job?: string; mergedAt: string; selftest?: 'PASS' | 'FAIL'; selftestFailed?: string[]; susSelftest?: 'PASS' | 'FAIL'; susFailed?: string[]; samples: Record<string, Sample> };
export type Db = { version: 1; runs: Record<string, Run> };
export const emptyDb = (): Db => ({ version: 1, runs: {} });

// one "diag: mem #N ..." line; null when it is not one
export function parseLine(line: string, kind: 'mem' | 'memsus' = 'mem'): Sample | null {
  const m = (kind === 'mem' ? /^diag: mem #(\d+) (\S+)(.*)$/ : /^diag: memsus #(\d+) (\S+)(.*)$/).exec(line.trimEnd());
  if (!m) return null;
  const s: Sample = { n: Number(m[1]), tag: m[2], v: {}, unval: [], failed: [] };
  for (const [, k, val, flag] of m[3].matchAll(/ (\w+)=(-|[-\d.]+|[\w,-]+?)([?!]?)(?= |$)/g)) {
    if (k === 'ev') { s.ev = val; continue; }
    if (k === 't') { s.t = Number(val); continue; }
    if (k === 'lag') { s.lag = Number(val); continue; }
    s.v[k] = val === '-' ? null : Number(val);
    if (flag === '?') s.unval.push(k);
    if (flag === '!') s.failed.push(k);
  }
  return s;
}

export function parseLog(text: string) {
  const samples: Sample[] = [], sus: Sample[] = [];
  let selftest: Run['selftest'], failed: string[] | undefined, susSelftest: Run['susSelftest'], susFailed: string[] | undefined;
  for (const line of text.split(/\r?\n/)) {
    const s = parseLine(line);
    if (s) { samples.push(s); continue; }
    const u = parseLine(line, 'memsus');
    if (u) { sus.push(u); continue; }
    const st = /^diag: mem selftest (PASS|FAIL)(?:: (.*))?/.exec(line);
    if (st) { selftest = st[1] as 'PASS' | 'FAIL'; failed = st[2]?.trim().split(/\s+/); }
    const su = /^diag: memsus selftest (PASS|FAIL)(?:: (.*))?/.exec(line);
    if (su) { susSelftest = su[1] as 'PASS' | 'FAIL'; susFailed = su[1] === 'FAIL' ? su[2]?.trim().split(/\s+/) : undefined; }
  }
  // the managed counts belong to the diag: mem sample they answer (same number); one without its sample is dropped
  for (const u of sus) {
    const s = samples.find((x) => x.n === u.n);
    if (!s) continue;
    Object.assign(s.v, u.v);
    s.unval.push(...u.unval), s.failed.push(...u.failed), s.lag = u.lag;
  }
  return { run: runIdOf(text), samples, selftest, selftestFailed: failed, susSelftest, susFailed };
}

// merges one log copy; null when the text has no run header or no diag: mem line
export function mergeLog(db: Db, text: string, job?: string, now = new Date().toISOString()) {
  const p = parseLog(text);
  if (!p.run || !p.samples.length) return null;
  const r: Run = (db.runs[p.run] ??= { mergedAt: now, samples: {} });
  let added = 0;
  for (const s of p.samples) { if (!r.samples[s.n]) added++; r.samples[s.n] = s; }
  if (p.selftest) { r.selftest = p.selftest; r.selftestFailed = p.selftestFailed; }
  if (p.susSelftest) { r.susSelftest = p.susSelftest; r.susFailed = p.susFailed; }
  if (job) r.job = r.job ? (r.job.split(',').includes(job) ? r.job : `${r.job},${job}`) : job;
  r.mergedAt = now;
  return { run: p.run, added, samples: Object.keys(r.samples).length };
}

export function loadDb(file = DB_DEFAULT): Db {
  if (!existsSync(file)) return emptyDb();
  const d = JSON.parse(readFileSync(file, 'utf8'));
  if (d.version !== 1) throw new Error(`${file}: unknown version ${d.version}`);
  return d;
}
export function saveDb(db: Db, file = DB_DEFAULT) {
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(`${file}.tmp`, JSON.stringify(db));
  renameSync(`${file}.tmp`, file);  // never a half-written DB
}
export function mergeFile(log: string, job?: string, file = DB_DEFAULT): string {
  const db = loadDb(file), r = mergeLog(db, readFileSync(log, 'utf8'), job);
  if (!r) return 'memdiag: no diag: mem lines in the log ([MemDiag] off, or ts3native older than 0.36.69)';
  saveDb(db, file);
  return `memdiag: run ${r.run} merged (+${r.added} samples, ${r.samples} in the run); DB ${Object.keys(db.runs).length} runs`;
}

const ordered = (r: Run) => Object.values(r.samples).sort((a, b) => a.n - b.n);
const fmt = (x: number) => (Math.abs(x) >= 100 ? x.toFixed(0) : x.toFixed(1));
// counts (a delta of 1 matters): native counts plus the module 2 suspect counters; rdMs is a cost, never an event delta
export const SUSPECTS = ['cc', 'ccIcon', 'ppc', 'etRef', 'gsc', 'mapv', 'wq', 'wpl', 'ogs', 'smh', 'gcCnt'];
const COUNTS = new Set(['eaDirN', 'gcN', 'rcN', ...SUSPECTS]);
const NO_DELTA = new Set(['rdMs']);
const diff = (a: Sample, b: Sample, minMb: number) => {
  const d: Record<string, number> = {};
  for (const [k, v] of Object.entries(b.v)) {
    const p = a.v[k];
    if (typeof v !== 'number' || typeof p !== 'number' || NO_DELTA.has(k)) continue;
    if (Math.abs(v - p) >= (COUNTS.has(k) ? 1 : minMb)) d[k] = v - p;
  }
  return d;
};

// per field over the run: first, last, min, max and slope (MB per hour between the first and last timed sample)
export function growth(r: Run) {
  const ss = ordered(r), out: { key: string; first: number; last: number; min: number; max: number; perHour: number | null; unval: boolean }[] = [];
  const keys = [...new Set(ss.flatMap((s) => Object.keys(s.v)))];
  for (const key of keys) {
    const pts = ss.filter((s) => typeof s.v[key] === 'number');
    if (!pts.length) continue;
    const vals = pts.map((s) => s.v[key] as number), a = pts[0], b = pts[pts.length - 1];
    const dt = a.t !== undefined && b.t !== undefined ? (b.t - a.t) / 3600 : 0;
    out.push({ key, first: vals[0], last: vals[vals.length - 1], min: Math.min(...vals), max: Math.max(...vals),
      perHour: dt > 0 ? (vals[vals.length - 1] - vals[0]) / dt : null, unval: pts.some((s) => s.unval.includes(key)) });
  }
  return out;
}

// every sample that marks an event (load, save, exit, mark, selftest, or any ev=) against the sample before it; fields that
// moved by at least minMb (counts: by at least 1)
export function eventDeltas(r: Run, minMb = 1) {
  const ss = ordered(r), out: { n: number; tag: string; ev?: string; t?: number; d: Record<string, number> }[] = [];
  for (let i = 1; i < ss.length; i++) {
    const a = ss[i - 1], b = ss[i];
    if (b.tag === 'periodic' && !b.ev) continue;
    out.push({ n: b.n, tag: b.tag, ev: b.ev, t: b.t, d: diff(a, b, minMb) });
  }
  return out;
}

// mem_gc probes: each gc-after against the latest gc-before (both marks, sampled inline around the collections). A drop is
// memory that was collectable but not collected yet (finalizer-freed natives, Boehm free blocks); a suspect count that
// stays is held by a root. gcCheck: native gcN (0x11F2F90) moved exactly like managed GC.CollectionCount (gcCnt).
export function gcDeltas(r: Run, minMb = 0.1) {
  const ss = ordered(r), out: { before: number; after: number; d: Record<string, number>; gcCheck: 'ok' | 'mismatch' | 'n/a' }[] = [];
  for (let i = 0; i < ss.length; i++) {
    if (ss[i].ev !== 'gc-after') continue;
    const a = ss.slice(0, i).reverse().find((s) => s.ev === 'gc-before'), b = ss[i];
    if (!a) continue;
    const g = [a.v.gcN, b.v.gcN, a.v.gcCnt, b.v.gcCnt];
    const gcCheck = g.every((x) => typeof x === 'number') ? (g[1]! - g[0]! === g[3]! - g[2]! ? 'ok' : 'mismatch') : 'n/a';
    out.push({ before: a.n, after: b.n, d: diff(a, b, minMb), gcCheck });
  }
  return out;
}

// repeated marks (mem-travel-x3: away / home / gc-after once per trip): each occurrence of a tag + ev against its previous
// occurrence, so growth that repeats every round (leak lead) stands apart from growth only in the first (cache fill)
export function sameTagDeltas(r: Run, minMb = 1) {
  const last = new Map<string, Sample>(), out: { key: string; k: number; from: number; to: number; d: Record<string, number> }[] = [];
  const count = new Map<string, number>();
  for (const s of ordered(r)) {
    if (!s.ev) continue;
    const key = `${s.tag} ev=${s.ev}`, p = last.get(key), k = (count.get(key) ?? 0) + 1;
    count.set(key, k), last.set(key, s);
    if (p) out.push({ key, k, from: p.n, to: s.n, d: diff(p, s, minMb) });
  }
  return out;
}

export function report(db: Db, runFilter?: string, minMb = 1): string {
  const lines: string[] = [];
  for (const [id, r] of Object.entries(db.runs)) {
    if (runFilter && !id.includes(runFilter)) continue;
    const ss = ordered(r);
    lines.push(`run ${id}${r.job ? ` (${r.job})` : ''}: ${ss.length} samples, self-test ${r.selftest ?? 'not run'}${r.selftestFailed?.length ? ` (failed ${r.selftestFailed.join(' ')})` : ''}, suspect self-test ${r.susSelftest ?? 'not run'}${r.susFailed?.length ? ` (failed ${r.susFailed.join(' ')})` : ''}`);
    lines.push('  growth (MB unless a count; slope per hour between the first and last sample; ? = unvalidated field):');
    for (const g of growth(r))
      lines.push(`    ${(g.key + (g.unval ? '?' : '')).padEnd(10)} ${fmt(g.first).padStart(7)} -> ${fmt(g.last).padStart(7)}  min ${fmt(g.min)} max ${fmt(g.max)}${g.perHour === null ? '' : `  ${g.perHour >= 0 ? '+' : ''}${fmt(g.perHour)}/h`}`);
    lines.push(`  events (change since the sample before, |d| >= ${minMb} MB):`);
    for (const e of eventDeltas(r, minMb)) {
      const d = Object.entries(e.d).map(([k, v]) => `${k} ${v >= 0 ? '+' : ''}${fmt(v)}`).join(', ');
      lines.push(`    #${e.n} ${e.tag}${e.ev ? ` ev=${e.ev}` : ''}${e.t !== undefined ? ` t=${Math.round(e.t / 60)} min` : ''}: ${d || 'no change'}`);
    }
    const rep = sameTagDeltas(r, minMb);
    if (rep.length) lines.push(`  repeated marks (each against the same tag's previous sample; the same growth every round = leak lead, only in #2 (the first round after the baseline) = cache fill; |d| >= ${minMb} MB):`);
    for (const e of rep) {
      const d = Object.entries(e.d).map(([k, v]) => `${k} ${v >= 0 ? '+' : ''}${fmt(v)}`).join(', ');
      lines.push(`    ${e.key} #${e.k} (#${e.from} -> #${e.to}): ${d || 'no change'}`);
    }
    const gcs = gcDeltas(r);
    if (gcs.length) lines.push('  forced GC (mem_gc, gc-after vs gc-before; a drop = collectable but uncollected, a suspect count that stays = rooted):');
    for (const g of gcs) {
      const d = Object.entries(g.d).map(([k, v]) => `${k} ${v >= 0 ? '+' : ''}${fmt(v)}`).join(', ');
      lines.push(`    #${g.before} -> #${g.after}: ${d || 'no change'}; gcN vs managed GC.CollectionCount: ${g.gcCheck}`);
    }
  }
  return lines.length ? lines.join('\n') : 'no runs';
}

function selfCheck() {
  const head = 'ts3native 0.36.69 2026-10-25 09:00:00\n';
  const L = (n: number, tag: string, ev: string, priv: number, eaDir: string, rest: number, t: number) =>
    `diag: mem #${n} ${tag}${ev ? ` ev=${ev}` : ''} priv=${priv} wPriv=${priv - 20} wCom=2167 free=1646 big=1525? img=310? map=120 stkRes=140 stkCom=9.0? miRes=320 miCom=210 eaDir=${eaDir} eaDirN=3? rp=20.0 pl=- sp=- gcHeap=360? gcFree=151? gcN=812? rc=388? rcCold=14.0? rcBudget=200? rcN=41000? anim=4.0? rest=${rest}? t=${t}`;
  const log1 = head + [
    'diag: mem fields (docs/190 phase 1; ...): validated priv wPriv; unvalidated big',
    L(1, 'load', '', 1800, '22.0?', 400, 120),
    L(2, 'selftest', 'selftest-leak-before', 1810, '22.0', 400, 160),
    L(3, 'selftest', 'selftest-leak-after', 1818, '30.0', 400, 161),
    'diag: mem selftest PASS (every checked field moved by the injected size)',
    L(4, 'mark', 'build-before', 1820, '30.0', 401, 300),
    L(5, 'mark', 'build-after', 1900, '30.0', 480, 360),
    L(6, 'periodic', '', 1950, '30.0!', 530, 3720),
  ].join('\r\n') + '\r\n';
  const s1 = parseLine(L(1, 'load', 'travel,world-switch', 1800, '-', 400, 120))!;
  assert.equal(s1.ev, 'travel,world-switch'); assert.equal(s1.v.eaDir, null); assert.equal(s1.v.priv, 1800); assert.equal(s1.t, 120);
  assert.ok(s1.unval.includes('gcHeap') && !s1.unval.includes('priv') && !s1.unval.includes('eaDir'));
  assert.equal(parseLine('diag: memory at 10 min: private 1851 MB'), null);
  assert.deepEqual(parseLine('diag: mem #9 periodic priv=12.5 map=0.0! t=5')!.failed, ['map']);

  const db = emptyDb();
  assert.equal(mergeLog(db, 'ts3native 0.36.68 2026-10-25 08:00:00\nno mem lines\n'), null);
  assert.deepEqual(mergeLog(db, log1, 'j1', 't'), { run: '0.36.69 2026-10-25 09:00:00', added: 6, samples: 6 });
  assert.deepEqual(mergeLog(db, log1, 'j2', 't'), { run: '0.36.69 2026-10-25 09:00:00', added: 0, samples: 6 }); // idempotent
  const r = db.runs['0.36.69 2026-10-25 09:00:00'];
  assert.equal(r.selftest, 'PASS'); assert.equal(r.job, 'j1,j2');
  // the synthetic leak: +8 MB private and EA direct between the tagged pair
  const ev = eventDeltas(r);
  const leak = ev.find((e) => e.ev === 'selftest-leak-after')!;
  assert.equal(leak.d.priv, 8); assert.equal(leak.d.eaDir, 8); assert.equal(leak.d.wCom, undefined);
  const build = ev.find((e) => e.ev === 'build-after')!;
  assert.deepEqual([build.d.priv, build.d.rest], [80, 79]);
  assert.ok(!ev.some((e) => e.tag === 'periodic')); // a plain periodic sample is growth, not an event
  // growth: priv 1800 -> 1950 over 3600 s = +150/h; eaDir mixed flags; '-' values skipped
  const g = Object.fromEntries(growth(r).map((x) => [x.key, x]));
  assert.equal(g.priv.perHour, 150); assert.equal(g.priv.unval, false); assert.equal(g.rest.unval, true);
  assert.equal(g.eaDir.first, 22); assert.equal(g.eaDir.max, 30); assert.equal(g.pl, undefined);
  assert.equal(r.samples[6].failed[0], 'eaDir');
  const fail = mergeLog(db, 'ts3native 0.36.69 2026-10-25 10:00:00\n' + L(1, 'load', '', 1, '-', 1, 1) + '\ndiag: mem selftest FAIL: eaDir eaDirN\n', 'j3', 't')!;
  assert.equal(db.runs[fail.run].selftest, 'FAIL'); assert.deepEqual(db.runs[fail.run].selftestFailed, ['eaDir', 'eaDirN']);
  const text = report(db, '09:00');
  assert.match(text, /self-test PASS/); assert.match(text, /priv\s+1800 ->    1950 .* \+150\/h/); assert.match(text, /#3 selftest ev=selftest-leak-after t=3 min: .*priv \+8\.0/);
  assert.ok(!text.includes('10:00:00'));

  // module 2: memsus lines merge into their diag: mem sample (by number, whatever the line order); a memsus line without
  // its sample is dropped; the forced-GC pair: priv / gcHeap drop, cc stays, gcN moved like gcCnt; rdMs never a delta
  const S = (n: number, tag: string, ev: string, cc: number, gcCnt: number, rd: string) =>
    `diag: memsus #${n} ${tag}${ev ? ` ev=${ev}` : ''} cc=${cc} ccIcon=${cc - 100}? ppc=2 etRef=- gsc=12 mapv=5 wq=40 wpl=12 ogs=3 smh=9 gcCnt=${gcCnt} rdMs=${rd} lag=3 t=1`;
  const G = (n: number, ev: string, priv: number, gcHeap: number, gcN: number) =>
    `diag: mem #${n} mark ev=${ev} priv=${priv} wPriv=${priv - 20} gcHeap=${gcHeap}? gcFree=10? gcN=${gcN}? t=${n * 10}`;
  const log2 = 'ts3native 0.36.74 2026-10-25 11:00:00\n' + [
    G(1, 'cat-r1', 1900, 300, 50), S(1, 'mark', 'cat-r1', 1200, 50, '0.40'),
    S(2, 'mark', 'cat-r2', 1500, 51, '0.90'), G(2, 'cat-r2', 1950, 330, 51),  // answer logged before its sample: still merged
    G(3, 'gc-before', 1960, 340, 51), S(3, 'mark', 'gc-before', 1500, 51, '0.50'),
    G(4, 'gc-after', 1890, 290, 53), S(4, 'mark', 'gc-after', 1500, 53, '2.00'),
    S(9, 'periodic', '', 1, 1, '0.1'),  // no sample #9: dropped
    'diag: memsus selftest cc 1200 -> 1201 -> 1200 (want +1, back) ok',
    'diag: memsus selftest FAIL: ppc',
  ].join('\n') + '\n';
  const u = parseLine(S(1, 'mark', 'cat-r1', 1200, 50, '0.40'), 'memsus')!;
  assert.deepEqual([u.n, u.ev, u.v.cc, u.v.etRef, u.lag, u.unval], [1, 'cat-r1', 1200, null, 3, ['ccIcon']]);
  assert.equal(parseLine(S(1, 'mark', '', 1, 1, '0'), 'mem'), null);
  const db2 = emptyDb();
  assert.deepEqual(mergeLog(db2, log2, 'j4', 't'), { run: '0.36.74 2026-10-25 11:00:00', added: 4, samples: 4 });
  const r2 = db2.runs['0.36.74 2026-10-25 11:00:00'];
  assert.equal(r2.susSelftest, 'FAIL'); assert.deepEqual(r2.susFailed, ['ppc']);
  assert.equal(r2.samples[2].v.cc, 1500); assert.ok(r2.samples[2].unval.includes('ccIcon') && r2.samples[2].unval.includes('gcN'));
  const cat = eventDeltas(r2).find((e) => e.ev === 'cat-r2')!;
  assert.deepEqual([cat.d.cc, cat.d.ccIcon, cat.d.priv, cat.d.gsc, cat.d.rdMs], [300, 300, 50, undefined, undefined]);
  const [gc] = gcDeltas(r2);
  assert.deepEqual([gc.before, gc.after, gc.d.priv, gc.d.gcHeap, gc.d.cc, gc.d.gcN, gc.d.gcCnt, gc.gcCheck], [3, 4, -70, -50, undefined, 2, 2, 'ok']);
  r2.samples[4].v.gcCnt = 54;
  assert.equal(gcDeltas(r2)[0].gcCheck, 'mismatch');
  r2.samples[4].v.gcCnt = null;
  assert.equal(gcDeltas(r2)[0].gcCheck, 'n/a');
  r2.samples[4].v.gcCnt = 53;
  const text2 = report(db2, '11:00');
  assert.match(text2, /suspect self-test FAIL \(failed ppc\)/); assert.match(text2, /#3 -> #4: .*priv -70.*; gcN vs managed GC.CollectionCount: ok/);
  assert.match(text2, /cc\s+1200 ->    1500/);
  assert.equal(sameTagDeltas(r2).length, 0); // every ev once: no repeated marks

  // repeated marks: gc-after per trip; trip 1 fills a cache (rc +200), every trip leaks 5 MB priv and 1 cc
  const T = (n: number, ev: string, priv: number, rc: number) => `diag: mem #${n} mark ev=${ev} priv=${priv} rc=${rc}? t=${n * 60}`;
  const C = (n: number, ev: string, cc: number) => `diag: memsus #${n} mark ev=${ev} cc=${cc} lag=1 t=1`;
  const log3 = 'ts3native 0.36.76 2026-10-25 12:00:00\n' + [
    T(1, 'gc-after', 1800, 170), C(1, 'gc-after', 10), T(2, 'away', 1900, 300),
    T(3, 'gc-after', 2005, 370), C(3, 'gc-after', 11), T(4, 'away', 2010, 380),
    T(5, 'gc-after', 2010, 370), C(5, 'gc-after', 12), T(6, 'gc-after', 2015, 370.5), C(6, 'gc-after', 13),
  ].join('\n') + '\n';
  const db3 = emptyDb(); mergeLog(db3, log3, 'j5', 't');
  const rp = sameTagDeltas(db3.runs['0.36.76 2026-10-25 12:00:00']);
  assert.deepEqual(rp.map((e) => [e.key, e.k, e.from, e.to, e.d.priv, e.d.rc, e.d.cc]), [
    ['mark ev=gc-after', 2, 1, 3, 205, 200, 1], ['mark ev=away', 2, 2, 4, 110, 80, undefined],
    ['mark ev=gc-after', 3, 3, 5, 5, undefined, 1], ['mark ev=gc-after', 4, 5, 6, 5, undefined, 1]]);
  assert.match(report(db3), /repeated marks .*\n    mark ev=gc-after #2 \(#1 -> #3\): priv \+205, rc \+200, cc \+1/);
  console.log('mem-report self-check ok');
}

if (import.meta.filename === resolve(process.argv[1] ?? '')) {
  const { values, positionals } = parseArgs({ allowPositionals: true, options: { 'self-check': { type: 'boolean' }, job: { type: 'string' }, db: { type: 'string' }, run: { type: 'string' }, log: { type: 'string' }, 'min-mb': { type: 'string' } } });
  const minMb = Number(values['min-mb'] ?? 1);
  if (values['self-check']) selfCheck();
  else if (positionals[0] === 'merge' && positionals[1]) console.log(mergeFile(positionals[1], values.job, values.db ?? DB_DEFAULT));
  else if (positionals[0] === 'report') {
    let db = loadDb(values.db ?? DB_DEFAULT);
    if (values.log) { db = emptyDb(); if (!mergeLog(db, readFileSync(values.log, 'utf8'))) { console.log('no diag: mem lines in the log'); process.exit(0); } }
    console.log(report(db, values.run, minMb));
  } else { console.error('usage: node tools/mem-report.ts merge <ts3native.log> [--job id] [--db file] | report [--db file] [--run id] [--log file] [--min-mb 1] | --self-check'); process.exitCode = 2; }
}
