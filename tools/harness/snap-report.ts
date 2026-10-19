// Report of a [ProbeSnap] run (docs/94 §7): runtime targets of the routing closure's indirect call/jmp sites, per site, with
// the targets static analysis did not have.
//   node tools/harness/snap-report.ts probe-out/queue/<queue>-<date>-<job>-<HHMM>-ts3native.log [...] [--all] [--functions <csv>]
//   node tools/harness/snap-report.ts --self-check
// Reads the "vcall SITE -> TARGET n=N [jmp] [out=module]" lines vcall.cpp logs every window (cumulative counts: the last
// line per pair wins; several logs are merged by adding their last counts). A target is NEW when
// tools/ghidra/symbols/ts3w-1.67.symbols.csv has no row at that address (inside TS3W.exe); OUT when it lies outside TS3W.exe (module named,
// "anon" = heap code: Mono, Frida, a hook trampoline). Function names come from the symbols CSV and, when present,
// game-copy/analysis/functions.csv (Ghidra entries; local only). Prints only sites with a NEW or OUT target unless --all.
// [ProbeSnap] Threads=1 lines end in " thr=job:N,worker:N,sim:N,other:N" (docs/109 §8.8); the split is printed per pair.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
type Pair = { site: number; target: number; n: number; jmp: boolean; out: string; thr?: number[] };  // thr: job, worker, sim, other
type Log = { pairs: Map<string, Pair>; overflow: Map<number, number>; window: string };

export function parseLog(text: string): Log {
  const pairs = new Map<string, Pair>(), overflow = new Map<number, number>();
  let window = '';
  for (const l of text.split(/\r?\n/)) {
    let m = l.match(/vcall 0x([0-9A-F]+) -> 0x([0-9A-F]+) n=(\d+)( jmp)?(?: out=(\S+))?(?: thr=job:(\d+),worker:(\d+),sim:(\d+),other:(\d+))?/i);
    if (m) {
      const site = parseInt(m[1], 16), target = parseInt(m[2], 16);
      pairs.set(`${site}:${target}`, { site, target, n: Number(m[3]), jmp: !!m[4], out: m[5] ?? '', ...(m[6] ? { thr: m.slice(6, 10).map(Number) } : {}) });
      continue;
    }
    if ((m = l.match(/vcall 0x([0-9A-F]+) overflow n=(\d+)/i))) overflow.set(parseInt(m[1], 16), Number(m[2]));
    else if (/vcall window \d+:/.test(l)) window = l.slice(l.indexOf('vcall window'));
  }
  return { pairs, overflow, window };
}

export function merge(logs: Log[]): { pairs: Pair[]; overflow: Map<number, number> } {
  const all = new Map<string, Pair>(), overflow = new Map<number, number>();
  for (const g of logs) {
    for (const [k, p] of g.pairs) {
      const q = all.get(k);
      const thr = q?.thr && p.thr ? q.thr.map((v, i) => v + p.thr![i]) : q ? q.thr ?? p.thr : p.thr;
      all.set(k, q ? { ...q, n: q.n + p.n, thr } : { ...p });
    }
    for (const [s, n] of g.overflow) overflow.set(s, (overflow.get(s) ?? 0) + n);
  }
  return { pairs: [...all.values()].sort((a, b) => a.site - b.site || b.n - a.n), overflow };
}

// sorted function entries (+ names) for "containing function" lookups
export function namer(symbols: Map<number, string>, entries: [number, string][]) {
  const have = new Set(entries.map(([e]) => e));
  const fns = [...entries, ...[...symbols].filter(([a]) => !have.has(a))].sort((a, b) => a[0] - b[0]);
  return (a: number) => {
    let lo = 0, hi = fns.length - 1, at = -1;
    while (lo <= hi) { const m = (lo + hi) >> 1; if (fns[m][0] <= a) { at = m; lo = m + 1; } else hi = m - 1; }
    if (at < 0) return '?';
    const [e, n] = fns[at];
    return (symbols.get(e) ?? n) + (a === e ? '' : `+0x${(a - e).toString(16)}`);
  };
}

const hex = (a: number) => '0x' + a.toString(16).toUpperCase().padStart(8, '0');

// every row's address (known), and the function rows (names; labels and data are not containing functions)
function readSymbols(): { known: Set<number>; functions: Map<number, string> } {
  const known = new Set<number>(), functions = new Map<number, string>();
  for (const l of readFileSync(join(ROOT, 'tools', 'ghidra', 'symbols', 'ts3w-1.67.symbols.csv'), 'utf8').split(/\r?\n/).slice(1)) {
    const c = l.split(',');
    if (!c[0]?.startsWith('0x')) continue;
    known.add(parseInt(c[0], 16));
    if (c[2] === 'function' || c[2] === 'icall') functions.set(parseInt(c[0], 16), c[1]);
  }
  return { known, functions };
}

function report(files: string[], all: boolean, functionsCsv: string) {
  const { known, functions: symbols } = readSymbols();
  const entries: [number, string][] = existsSync(functionsCsv)
    ? readFileSync(functionsCsv, 'utf8').split(/\r?\n/).slice(1).filter(Boolean).map(l => { const c = l.split(','); return [parseInt(c[0], 16), c[2]] as [number, string]; })
    : [];
  const fn = namer(symbols, entries);
  const logs = files.map(f => parseLog(readFileSync(f, 'utf8')));
  for (const [i, g] of logs.entries()) console.log(`${files[i]}: ${g.pairs.size} pairs; last ${g.window || '(no window line)'}`);
  const { pairs, overflow } = merge(logs);
  const sites = new Map<number, Pair[]>();
  for (const p of pairs) sites.set(p.site, [...(sites.get(p.site) ?? []), p]);
  let newT = 0, outT = 0, shown = 0;
  for (const [site, ps] of sites) {
    const flags = ps.map(p => (p.out ? `OUT=${p.out}` : known.has(p.target) ? '' : 'NEW'));
    newT += flags.filter(f => f === 'NEW').length;
    outT += ps.filter(p => p.out).length;
    if (!all && !flags.some(Boolean) && !overflow.has(site)) continue;
    shown++;
    console.log(`\n${hex(site)} in ${fn(site)}${ps[0].jmp ? ' (jmp)' : ''}: ${ps.length} targets${overflow.has(site) ? `, ${overflow.get(site)} hits past 8 targets` : ''}`);
    const thr = (p: Pair) => (p.thr ? ` job ${p.thr[0]}, worker ${p.thr[1]}, sim ${p.thr[2]}, other ${p.thr[3]}` : '');
    ps.forEach((p, i) => console.log(`  ${hex(p.target)} n=${String(p.n).padEnd(6)} ${p.out ? '(outside TS3W.exe)' : fn(p.target).padEnd(40)} ${flags[i]}${thr(p)}`));
  }
  console.log(`\n${sites.size} sites hit, ${pairs.length} pairs, ${newT} targets not in the symbols CSV, ${outT} outside TS3W.exe, ` +
    `${overflow.size} sites past 8 targets; ${shown} sites listed${all ? '' : ' (NEW/OUT/overflow only; --all for every site)'}`);
}

function selfCheck() {
  const a = parseLog('x\nvcall 0x0082C800 -> 0x0082D000 n=3\nvcall 0x0082C800 -> 0x0082D000 n=7\nvcall 0x0082C900 -> 0x10001000 n=2 out=mod.dll\n' +
    'vcall 0x0082CA00 -> 0x0067620A n=5 jmp\nvcall 0x0082C900 overflow n=4 (more than 8 targets)\nvcall window 3: hits 9, pairs 3');
  assert.equal(a.pairs.size, 3);
  assert.equal(a.pairs.get(`${0x82c800}:${0x82d000}`)!.n, 7);  // the last cumulative count wins
  assert.equal(a.pairs.get(`${0x82c900}:${0x10001000}`)!.out, 'mod.dll');
  assert.ok(a.pairs.get(`${0x82ca00}:${0x67620a}`)!.jmp);
  assert.equal(a.overflow.get(0x82c900), 4);
  assert.match(a.window, /^vcall window 3/);
  const m = merge([a, parseLog('vcall 0x0082C800 -> 0x0082D000 n=1')]);
  assert.equal(m.pairs.find(p => p.site === 0x82c800)!.n, 8);
  const t = merge([parseLog('vcall 0x009158AB -> 0x00626320 n=5 thr=job:3,worker:0,sim:2,other:0'),
    parseLog('vcall 0x009158AB -> 0x00626320 n=1 out=x.dll thr=job:1,worker:0,sim:0,other:0')]).pairs[0];
  assert.deepEqual([t.n, t.thr], [6, [4, 0, 2, 0]]);
  const f = namer(new Map([[0x82c6e0, 'Route_PlanLocked']]), [[0x82c6e0, 'FUN_0082c6e0'], [0x82d000, 'FUN_0082d000']]);
  assert.equal(f(0x82c6e0), 'Route_PlanLocked');
  assert.equal(f(0x82c800), 'Route_PlanLocked+0x120');
  assert.equal(f(0x82d010), 'FUN_0082d000+0x10');
  assert.equal(f(0x100), '?');
  console.log('snap-report self-check ok');
}

const { values, positionals } = parseArgs({ allowPositionals: true, options: {
  'self-check': { type: 'boolean' }, all: { type: 'boolean' },
  functions: { type: 'string', default: join(ROOT, 'game-copy', 'analysis', 'functions.csv') } } });
if (values['self-check']) selfCheck();
else if (!positionals.length) { console.error('usage: node tools/harness/snap-report.ts <ts3native.log> [...] [--all] [--functions <csv>]'); process.exitCode = 2; }
else report(positionals, !!values.all, values.functions!);
