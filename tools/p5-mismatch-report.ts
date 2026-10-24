// P5 audit mismatch classes (docs/157 §7): reads the replay audit lines of ts3native logs (replay.cpp RpAudit /
// RpAuditDiff, snap.cpp SpLogMis) and sorts every mismatch into a class. Reads only.
//   node tools/p5-mismatch-report.ts [--filter <regex on log name>] [log ...]   default: probe-out/queue/*2026-10-23*{p5,redirect}*-ts3native.log
//   node tools/p5-mismatch-report.ts --self-check
// Classes (per mismatched leaf entry when the diff lists it, else per audit):
//   owner-gone     only live, on an FME no live map node holds: the game's own leftover leaf (I-157-6; 0.36.35+ diff line)
//   live-only      only live, the owner is still in the live map (0.36.35+ diff line says it is held)
//   copy-only      only in the clone (our replay kept or added it)
//   value-differs  same footprint id on both sides with other fields, or the lot hash differs at equal entry counts, or an image byte
//   unknown        only live, owner not logged (diff line before 0.36.35), or counts differ and no diff names the entries
//                  (the diff runs for the first 4 per session, and a forced rebase before it leaves nothing to list)
// Set TS3_REPO=<main checkout> when running from a git worktree.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { basename, join } from 'node:path';
import assert from 'node:assert/strict';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..');
const QUEUE = join(ROOT, 'probe-out', 'queue');
export const CLASSES = ['owner-gone', 'live-only', 'copy-only', 'value-differs', 'unknown'] as const;
type Cls = (typeof CLASSES)[number];
type Ent = { side: 'live' | 'clone'; id: string; cls: string; fme: string; w4: string; w8: string };
export type Audit = {
  log: string; lot: string; q: number; liveQ: number; cloneQ: number; live: number; clone: number; what: string;
  ring: string; ents: Ent[]; onlyLive: number; onlyClone: number; gone: number | null; diffed: boolean; counts: Record<Cls, number>;
  cls: Cls; empty: boolean;  // the audit's class (most entries); empty = the diff found the clone equal again (rebased before it ran)
};
export type Img = { log: string; table: string; key: string; size: [number, number]; off: string; object: string; cls: Cls };
export type Run = { log: string; version: string; audits: Audit[]; images: Img[]; lotSet: number };

const H = '0x[0-9A-Fa-f]+';
export function parse(text: string, log: string): Run {
  const run: Run = { log, version: text.match(/ts3native (\d+\.\d+\.\d+)/)?.[1] ?? '?', audits: [], images: [], lotSet: 0 };
  const last = new Map<string, Audit>();  // lot -> its latest audit (the diff follows at the next hold)
  const fresh = (lot: string): Audit => {
    const a: Audit = { log, lot, q: -1, liveQ: 0, cloneQ: 0, live: 0, clone: 0, what: '', ring: '', ents: [], onlyLive: 0, onlyClone: 0,
      gone: null, diffed: false, cls: 'unknown', empty: false, counts: { 'owner-gone': 0, 'live-only': 0, 'copy-only': 0, 'value-differs': 0, unknown: 0 } };
    run.audits.push(a), last.set(lot, a);
    return a;
  };
  let pend: Audit | null = null;  // a Q line waits for its "hash differs" line
  for (const l of text.split(/\r?\n/)) {
    let m = l.match(new RegExp(`snap replay audit MISMATCH lot (${H}): Q (\\d+) \\(level index\\) entries live (\\d+) clone (\\d+)`));
    if (m) { pend = fresh(m[1]); pend.q = +m[2], pend.liveQ = +m[3], pend.cloneQ = +m[4]; continue; }
    m = l.match(new RegExp(`snap replay audit MISMATCH lot (${H}): (hash differs|not in the clone), entries live (\\d+) clone (\\d+)(.*)`));
    if (m) {
      const a = pend && pend.lot === m[1] ? pend : fresh(m[1]);
      a.what = m[2], a.live = +m[3], a.clone = +m[4], a.ring = m[5].match(/ring (\d+)/)?.[1] ?? '';
      pend = null;
      continue;
    }
    if (/snap replay audit MISMATCH: clone has \d+ lots/.test(l)) { run.lotSet++; continue; }
    m = l.match(new RegExp(`snap replay audit diff lot (${H}) Q \\d+: only (live|in the clone): footprint (${H}) class (${H}), FME \\+0x10 (${H}), entry \\+4 (${H}) \\+8 (${H})`));
    if (m) {
      const a = last.get(m[1]);
      if (a) a.ents.push({ side: m[2] === 'live' ? 'live' : 'clone', id: m[3], cls: m[4], fme: m[5], w4: m[6], w8: m[7] });
      continue;
    }
    m = l.match(new RegExp(`snap replay audit diff lot (${H}) Q \\d+: live \\d+ entries, clone \\d+; only live (\\d+), only in the clone (\\d+)`));
    if (m) { const a = last.get(m[1]); if (a) a.diffed = true, a.onlyLive = +m[2], a.onlyClone = +m[3]; continue; }
    m = l.match(new RegExp(`snap replay audit diff lot (${H}) Q \\d+: (\\d+) of the (\\d+) only-live entries are on an FME no live map node holds`));
    if (m) { const a = last.get(m[1]); if (a) a.gone = +m[2]; continue; }
    m = l.match(new RegExp(`snap audit MISMATCH (\\S+) key (${H}): image (\\d+) bytes, live (\\d+), first difference at \\+(${H}).*object ([0-9A-Fa-f]+)`));
    if (m) {
      const [im, lv] = [+m[3], +m[4]];
      run.images.push({ log, table: m[1], key: m[2], size: [im, lv], off: m[5], object: m[6], cls: im < lv ? 'live-only' : im > lv ? 'copy-only' : 'value-differs' });
    }
  }
  for (const a of run.audits) classify(a);
  return run;
}

export function classify(a: Audit): void {
  count(a);
  a.empty = a.diffed && !a.onlyLive && !a.onlyClone;
  a.cls = CLASSES.reduce((b, k) => (a.counts[k] > a.counts[b] ? k : b), 'unknown' as Cls);
}
function count(a: Audit): void {
  const c = a.counts;
  if (a.diffed && (a.onlyLive || a.onlyClone)) {
    // same footprint id on both sides of the listed entries = a value difference, not a missing entry
    const ids = new Map<string, [number, number]>();
    for (const e of a.ents) { const v = ids.get(e.id) ?? [0, 0]; v[e.side === 'live' ? 0 : 1]++; ids.set(e.id, v); }
    let pairs = 0;
    for (const [lv, cl] of ids.values()) pairs += Math.min(lv, cl);
    c['value-differs'] = pairs;
    const live = a.onlyLive - pairs;
    c['copy-only'] = a.onlyClone - pairs;
    if (a.gone === null) c.unknown = live;
    else c['owner-gone'] = Math.min(a.gone, live), c['live-only'] = live - Math.min(a.gone, live);
    return;
  }
  const d = a.q >= 0 ? a.liveQ - a.cloneQ : a.live - a.clone;
  if (a.what === 'hash differs' && d === 0) c['value-differs'] = 1;  // equal counts, other content
  else if (a.what === 'not in the clone') c['live-only'] = a.live || 1;  // a whole lot the clone lacks
  else c.unknown = Math.abs(d) || 1;
}

function pad(s: string | number, n: number): string { return String(s).padEnd(n); }

export function report(runs: Run[]): string {
  const out: string[] = [];
  const tot: Record<Cls, number> = { 'owner-gone': 0, 'live-only': 0, 'copy-only': 0, 'value-differs': 0, unknown: 0 };
  out.push('## Per run (mismatched audits by class: a lot audit by its most entries, an image audit one each; entries = only live / only in the clone, as the diffs counted them)', '');
  out.push(`| run | ts3native | audits | diffed | diff empty | ${CLASSES.join(' | ')} | lot-set | entries live / clone |`, `|${'---|'.repeat(CLASSES.length + 7)}`);
  const sum = { audits: 0, diffed: 0, empty: 0, lotSet: 0, el: 0, ec: 0 };
  for (const r of runs) {
    const s: Record<Cls, number> = { 'owner-gone': 0, 'live-only': 0, 'copy-only': 0, 'value-differs': 0, unknown: 0 };
    for (const a of r.audits) s[a.cls]++;
    for (const i of r.images) s[i.cls]++;
    for (const k of CLASSES) tot[k] += s[k];
    const n = r.audits.length + r.images.length, dif = r.audits.filter(a => a.diffed).length, emp = r.audits.filter(a => a.empty).length;
    const el = r.audits.reduce((x, a) => x + a.onlyLive, 0), ec = r.audits.reduce((x, a) => x + a.onlyClone, 0);
    sum.audits += n, sum.diffed += dif, sum.empty += emp, sum.lotSet += r.lotSet, sum.el += el, sum.ec += ec;
    out.push(`| ${r.log.replace(/-ts3native\.log$/, '')} | ${r.version} | ${n} | ${dif} | ${emp} | ${CLASSES.map(k => s[k]).join(' | ')} | ${r.lotSet} | ${el} / ${ec} |`);
  }
  out.push(`| **total** | | ${sum.audits} | ${sum.diffed} | ${sum.empty} | ${CLASSES.map(k => tot[k]).join(' | ')} | ${sum.lotSet} | ${sum.el} / ${sum.ec} |`, '');

  out.push('## By lot and Q (live - clone entries per audit; "repeats" = the same counts after a forced rebase)', '');
  out.push('| lot | Q | audits | runs | deltas (live-clone) | repeats | diffed | class |', '|---|---|---|---|---|---|---|---|');
  const byLot = new Map<string, Audit[]>();
  for (const r of runs) for (const a of r.audits) { const k = `${a.lot} ${a.q}`; byLot.set(k, [...(byLot.get(k) ?? []), a]); }
  for (const [k, v] of [...byLot].sort((x, y) => y[1].length - x[1].length)) {
    const d = v.map(a => (a.q >= 0 ? a.liveQ - a.cloneQ : a.live - a.clone));
    const sig = v.map(a => `${a.log}|${a.liveQ}|${a.cloneQ}`);
    const rep = sig.length - new Set(sig).size;
    const emp = v.filter(a => a.empty).length;
    const cls = CLASSES.filter(c => v.some(a => a.cls === c)).join(', ') + (emp ? ` (${emp} diff empty)` : '');
    const [lot, q] = k.split(' ');
    out.push(`| ${lot} | ${q === '-1' ? '-' : q} | ${v.length} | ${new Set(v.map(a => a.log)).size} | ${[...new Set(d)].join(', ')} | ${rep} | ${v.filter(a => a.diffed).length} | ${cls} |`);
  }
  out.push('');

  out.push('## Diff entries by footprint class and field (only the first 12 per diff are listed in the log)', '');
  out.push('| side | footprint class | footprint id | FME +0x10 | entry +4 / +8 | listed | lots |', '|---|---|---|---|---|---|---|');
  const byEnt = new Map<string, { n: number; lots: Set<string> }>();
  for (const r of runs) for (const a of r.audits) for (const e of a.ents) {
    const k = [e.side, e.cls, e.id, e.fme, `${e.w4} / ${e.w8}`].join('\t');
    const v = byEnt.get(k) ?? { n: 0, lots: new Set<string>() };
    v.n++, v.lots.add(a.lot), byEnt.set(k, v);
  }
  for (const [k, v] of [...byEnt].sort((x, y) => y[1].n - x[1].n)) out.push(`| ${k.split('\t').join(' | ')} | ${v.n} | ${v.lots.size} |`);
  if (!byEnt.size) out.push('| (none) | | | | | | |');
  out.push('');

  const imgs = runs.flatMap(r => r.images);
  if (imgs.length) {
    out.push('## Image audits (snap.cpp, P3 images: table = input, field = first differing offset)', '');
    out.push('| run | table | key | image / live bytes | offset | object | class |', '|---|---|---|---|---|---|---|');
    for (const i of imgs) out.push(`| ${i.log.replace(/-ts3native\.log$/, '')} | ${i.table} | ${i.key} | ${i.size.join(' / ')} | ${i.off} | ${i.object} | ${i.cls} |`);
    out.push('');
  }
  return out.join('\n');
}

function selfCheck(): void {
  const log = [
    'ts3native 0.36.35 2026-10-23',
    // 1: classic pre-0.36.35 shape: 3 only-live children, owner not logged -> unknown
    'snap replay audit MISMATCH lot 0x00000000000000A1: Q 1 (level index) entries live 10 clone 7',
    'snap replay audit MISMATCH lot 0x00000000000000A1: hash differs, entries live 20 clone 17 (clone 5 lots, map 9 / live 9)',
    ...Array(3).fill('snap replay audit diff lot 0x00000000000000A1 Q 1: only live: footprint 0xFFFFFFFF00000000 class 0x00FEBF90, FME +0x10 0x00000600, entry +4 0xFFFFFFFF +8 0xFFFFFFFF'),
    'snap replay audit diff lot 0x00000000000000A1 Q 1: live 10 entries, clone 7; only live 3, only in the clone 0 (0 of them on an FME no clone map node holds: I-94-510 shape; 2 removes covered so far)',
    // 2: 0.36.35 line: 2 of 3 owner gone, one pair of the same id on both sides = value differs
    'snap replay audit MISMATCH lot 0x00000000000000B2: Q 1 (level index) entries live 10 clone 8 (ring 77, cut 5, last equal ring 40)',
    'snap replay audit MISMATCH lot 0x00000000000000B2: hash differs, entries live 30 clone 28 (clone 5 lots, map 9 / live 9) (ring 77, cut 5, last equal ring 40)',
    'snap replay audit diff lot 0x00000000000000B2 Q 1: only live: footprint 0x0000000000E91605 class 0x00FEBA98, FME +0x10 0x00000200, entry +4 0xFFFFFFFF +8 0xFFFFFFFF',
    'snap replay audit diff lot 0x00000000000000B2 Q 1: only in the clone: footprint 0x0000000000E91605 class 0x00FEBA98, FME +0x10 0x00000200, entry +4 0x0000FFFF +8 0xFFFFFFFF',
    'snap replay audit diff lot 0x00000000000000B2 Q 1: live 10 entries, clone 8; only live 3, only in the clone 1 (0 of them ...)',
    'snap replay audit diff lot 0x00000000000000B2 Q 1: 2 of the 2 only-live entries are on an FME no live map node holds (the game\'s own leftover leaves)',
    // 3: equal counts, other hash -> value differs; 4: counts differ, no diff -> unknown
    'snap replay audit MISMATCH lot 0x00000000000000C3: hash differs, entries live 5 clone 5 (clone 5 lots, map 9 / live 9)',
    'snap replay audit MISMATCH lot 0x00000000000000D4: Q 2 (level index) entries live 9 clone 4',
    'snap replay audit MISMATCH lot 0x00000000000000D4: hash differs, entries live 9 clone 4 (clone 5 lots, map 9 / live 9)',
    'snap replay audit MISMATCH: clone has 4 lots, live 5',
    'snap audit MISMATCH grid key 0x0000000000000001: image 400 bytes, live 400, first difference at +0x2C (image 01, live 02), object 0C0FFEE0, version 3 epoch 1',
  ].join('\r\n');
  const r = parse(log, 'x-ts3native.log');
  assert.equal(r.version, '0.36.35');
  assert.equal(r.audits.length, 4);
  const [a, b, c, d] = r.audits;
  assert.deepEqual([a.counts.unknown, a.counts['owner-gone'], a.ents.length], [3, 0, 3]);
  assert.deepEqual([b.counts['value-differs'], b.counts['owner-gone'], b.counts['live-only'], b.counts['copy-only'], b.ring], [1, 2, 0, 0, '77']);
  assert.equal(c.counts['value-differs'], 1);
  assert.equal(d.counts.unknown, 5);
  assert.equal(r.lotSet, 1);
  assert.equal(r.images[0].cls, 'value-differs');
  const t = report([r]);
  assert.deepEqual([a.cls, b.cls, c.cls, d.cls], ['unknown', 'owner-gone', 'value-differs', 'unknown']);
  assert.match(t, /\| \*\*total\*\* \| \| 5 \| 2 \| 0 \| 1 \| 0 \| 0 \| 2 \| 2 \| 1 \| 6 \/ 1 \|/);
  console.log('p5-mismatch-report self-check ok');
}

if (process.argv.includes('--self-check')) selfCheck();
else {
  const args = process.argv.slice(2);
  const fi = args.indexOf('--filter');
  const filter = fi >= 0 ? new RegExp(args.splice(fi, 2)[1]) : /2026-10-23.*(p5|redirect).*-ts3native\.log$/;
  const files = args.length ? args : existsSync(QUEUE) ? readdirSync(QUEUE).filter(f => filter.test(f)).sort().map(f => join(QUEUE, f)) : [];
  if (!files.length) { console.error(`no logs (looked in ${QUEUE}; set TS3_REPO in a worktree)`); process.exit(1); }
  console.log(report(files.map(f => parse(readFileSync(f, 'utf8'), basename(f)))));
}
