// Joins a [ProbeSnap] run (live indirect-call targets, docs/94 §7) with the static map docs/data/routing-indirect-sites.csv
// (docs/102) and classes every live (site, target) pair (docs/106):
//   match      site resolved statically, target in its targets column
//   family     site partly resolved, target in its targets column
//   new        site resolved or partly resolved, target NOT predicted (truly new)
//   resolves   site unresolved statically; the live target is its first real answer ("hint" when the closure-built hint list had it)
//   no-row     site not in the CSV (in the probe closure, tools/snap-vcall-sites.py, but not in the docs/102 closure)
// Targets outside TS3W.exe (out=MODULE) match an import row that names a function of that module.
// Also lists CSV sites that never fired; with --sites (native/ts3native/vcallsites.h) it splits them into armed / not armed.
//   node tools/indirect-live-vs-static.ts <report.txt | ts3native.log> [...] [--csv <csv>] [--sites <vcallsites.h>]
//                                         [--pairs] [--unfired] [--class new,resolves]
//   node tools/indirect-live-vs-static.ts --self-check
// Inputs: tools/harness/snap-report.ts output (as docs/data/snap-probe-*.txt; it lists only sites with NEW/OUT targets
// unless run with --all) or the raw "vcall SITE -> TARGET n=N" log lines (complete; several logs are merged).
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
type Pair = { site: number; target: number; n: number; out: string };
type Row = { site: number; caller: string; kind: string; slot: string; targets: Set<number>; modules: Set<string>; status: string; note: string };
type Cls = 'match' | 'family' | 'new' | 'resolves' | 'no-row';

const hex = (a: number) => '0x' + a.toString(16).toUpperCase().padStart(6, '0');
const mod = (s: string) => s.toUpperCase().replace(/\.DLL$/, '');

// raw log lines (last cumulative count per pair wins inside one file) or snap-report blocks; files are merged by adding counts
export function parseProbe(text: string): Pair[] {
  const pairs = new Map<string, Pair>();
  let site = -1;
  for (const l of text.split(/\r?\n/)) {
    let m = l.match(/vcall 0x([0-9A-F]+) -> 0x([0-9A-F]+) n=(\d+)(?: jmp)?(?: out=(\S+))?/i);
    if (m) { const p = { site: parseInt(m[1], 16), target: parseInt(m[2], 16), n: Number(m[3]), out: m[4] ?? '' }; pairs.set(`${p.site}:${p.target}`, p); continue; }
    if ((m = l.match(/^0x([0-9A-F]+) in \S+.*: \d+ targets/i))) { site = parseInt(m[1], 16); continue; }
    if (site >= 0 && (m = l.match(/^\s+0x([0-9A-F]+) n=(\d+)\s.*?(?:OUT=(\S+))?\s*$/i))) {
      const p = { site, target: parseInt(m[1], 16), n: Number(m[2]), out: m[3] ?? '' };
      pairs.set(`${p.site}:${p.target}`, p);
    } else if (!/^\s+0x/.test(l)) site = -1;
  }
  return [...pairs.values()];
}

function csvSplit(line: string): string[] {
  const out: string[] = []; let cur = '', q = false;
  for (const ch of line) {
    if (ch === '"') q = !q; else if (ch === ',' && !q) { out.push(cur); cur = ''; } else cur += ch;
  }
  out.push(cur);
  return out;
}

export function parseCsv(text: string): Map<number, Row> {
  const rows = new Map<number, Row>();
  for (const l of text.split(/\r?\n/).slice(1)) {
    if (!l.startsWith('0x')) continue;
    const [site, caller, kind, slot, targets, status, note] = csvSplit(l);
    const t = new Set<number>(), m = new Set<string>();
    for (const tok of targets.split(';').map(s => s.trim().split(/\s+/)[0]).filter(Boolean)) {
      if (tok.startsWith('0x')) t.add(parseInt(tok, 16)); else if (tok.includes('!')) m.add(mod(tok.split('!')[0]));
    }
    rows.set(parseInt(site, 16), { site: parseInt(site, 16), caller, kind, slot, targets: t, modules: m, status, note });
  }
  return rows;
}

export function classify(p: Pair, r: Row | undefined): Cls | 'hint' {
  if (!r) return 'no-row';
  const hit = p.out ? r.modules.has(mod(p.out)) : r.targets.has(p.target);
  if (r.status === 'unresolved') return hit ? 'hint' : 'resolves';
  if (!hit) return 'new';
  return r.status === 'resolved' ? 'match' : 'family';
}

function run(files: string[], csvPath: string, sitesPath: string | undefined, showPairs: boolean, showUnfired: boolean, only: Set<string> | null) {
  const rows = parseCsv(readFileSync(csvPath, 'utf8'));
  const merged = new Map<string, Pair>();
  for (const f of files) for (const p of parseProbe(readFileSync(f, 'utf8'))) {
    const k = `${p.site}:${p.target}`, q = merged.get(k);
    merged.set(k, q ? { ...q, n: q.n + p.n } : p);
  }
  const pairs = [...merged.values()].sort((a, b) => a.site - b.site || b.n - a.n);
  const count = new Map<string, number>(), siteCls = new Map<string, Set<number>>();
  for (const p of pairs) {
    const r = rows.get(p.site), c = classify(p, r);
    const key = `${c}${r ? ' / ' + r.status : ''}`;
    count.set(key, (count.get(key) ?? 0) + 1);
    if (!siteCls.has(c)) siteCls.set(c, new Set());
    siteCls.get(c)!.add(p.site);
    if (showPairs && (!only || only.has(c)))
      console.log([c, hex(p.site), r?.caller ?? '', r?.kind ?? '', r?.slot ?? '', p.out ? `${p.out}+${hex(p.target)}` : hex(p.target), p.n].join('\t'));
  }
  const fired = new Set(pairs.map(p => p.site));
  console.log(`${files.length} probe file(s): ${fired.size} sites, ${pairs.length} pairs`);
  for (const [k, n] of [...count].sort()) console.log(`  pairs ${k}: ${n}`);
  for (const [c, s] of [...siteCls].sort()) console.log(`  sites with a ${c} pair: ${s.size}`);
  const armed = sitesPath && existsSync(sitesPath)
    ? new Set([...readFileSync(sitesPath, 'utf8').matchAll(/\{0x([0-9A-F]+), \d+, \{/gi)].map(m => parseInt(m[1], 16))) : null;
  const unfired = [...rows.values()].filter(r => !fired.has(r.site));
  const by = new Map<string, number>();
  for (const r of unfired) { const k = `${armed ? (armed.has(r.site) ? 'armed' : 'not armed') + ' / ' : ''}${r.kind} / ${r.status}`; by.set(k, (by.get(k) ?? 0) + 1); }
  console.log(`CSV sites not fired: ${unfired.length} of ${rows.size}${armed ? ` (probe table ${armed.size} sites, ${[...rows.keys()].filter(s => armed.has(s)).length} of them in the CSV)` : ''}`);
  for (const [k, n] of [...by].sort()) console.log(`  ${k}: ${n}`);
  if (showUnfired) for (const r of unfired)
    console.log(['unfired', hex(r.site), r.caller, r.kind, r.slot, r.status, armed ? (armed.has(r.site) ? 'armed' : 'not-armed') : '', r.note.split('|').pop()!.trim()].join('\t'));
}

function selfCheck() {
  const csv = 'site,caller,kind,slot offset,targets,status,note\n' +
    '0x100,0x0F0 A,vcall,+0x4,0x500 X;0x600,resolved,[x] | g\n' +
    '0x200,0x1F0 B,vcall,+0x8,0x700,partly resolved,"[family] a, b | g"\n' +
    '0x300,0x2F0 C,vcall,+0xc,0x800,unresolved,hint | g\n' +
    '0x400,0x3F0 D,import,,MSVCR80!memmove,resolved,[import] | g\n' +
    '0x450,0x3F0 D,vcall,+0x0,,unresolved,never | g\n';
  const rows = parseCsv(csv);
  assert.equal(rows.size, 5);
  assert.equal(rows.get(0x200)!.note, '[family] a, b | g');
  const raw = parseProbe('vcall 0x00000100 -> 0x00000500 n=3\nvcall 0x00000100 -> 0x00000500 n=9\nvcall 0x00000400 -> 0x716ED25F n=2 out=MSVCR80.dll');
  assert.equal(raw.length, 2);
  assert.equal(raw.find(p => p.site === 0x100)!.n, 9);
  const rep = parseProbe('hdr: 821 pairs\n\n0x00000200 in FUN_000001f0+0x10: 2 targets\n  0x00000700 n=5      FUN_00000700   NEW\n' +
    '  0x00000900 n=1      FUN_00000900                             NEW\n\n0x00000300 in FUN_x+0x1 (jmp): 1 targets\n  0x00000801 n=4  FUN_x  NEW\n' +
    '\n0x00000400 in FUN_y+0x2: 1 targets\n  0x6EA8D25F n=368    (outside TS3W.exe) OUT=MSVCR80.dll\n\n607 sites hit, 906 pairs');
  assert.equal(rep.length, 4);
  const c = (s: number, t: number, out = '') => classify({ site: s, target: t, n: 1, out }, rows.get(s));
  assert.equal(c(0x100, 0x600), 'match');
  assert.equal(c(0x100, 0x601), 'new');
  assert.equal(c(0x200, 0x700), 'family');
  assert.equal(c(0x200, 0x900), 'new');
  assert.equal(c(0x300, 0x800), 'hint');
  assert.equal(c(0x300, 0x801), 'resolves');
  assert.equal(c(0x400, 0x716ED25F, 'MSVCR80.dll'), 'match');
  assert.equal(c(0x400, 0x716ED25F, 'KERNEL32.dll'), 'new');
  assert.equal(c(0x999, 0x1), 'no-row');
  console.log('indirect-live-vs-static self-check ok');
}

const { values, positionals } = parseArgs({ allowPositionals: true, options: {
  'self-check': { type: 'boolean' }, pairs: { type: 'boolean' }, unfired: { type: 'boolean' }, class: { type: 'string' },
  csv: { type: 'string', default: join(ROOT, 'docs', 'data', 'routing-indirect-sites.csv') },
  sites: { type: 'string', default: join(ROOT, 'native', 'ts3native', 'vcallsites.h') } } });
if (values['self-check']) selfCheck();
else if (!positionals.length) { console.error('usage: node tools/indirect-live-vs-static.ts <report.txt|ts3native.log> [...] [--csv f] [--sites f] [--pairs] [--unfired] [--class new,resolves]'); process.exitCode = 2; }
else run(positionals, values.csv!, values.sites, !!values.pairs, !!values.unfired, values.class ? new Set(values.class.split(',')) : null);
