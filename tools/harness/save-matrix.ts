// Save-safety test matrix: proves the save-path fixes write the same save as the game, in one load, before a release.
//   node tools/harness/save-matrix.ts [--cases control,identical,...] [--travel] [--to france|university|future] [--case-timeout 900]
//   node tools/harness/save-matrix.ts --list            cases and what each checks
//   node tools/harness/save-matrix.ts --reload-check    second half of case `reload`, after loading its ON save (printed commands)
//   node tools/harness/save-matrix.ts --reload-check --off   same for its OFF save in a second load; the later check compares both
//   node tools/harness/save-matrix.ts --self-check      log parsing and diff filtering, no game
// Needs the probe server and a world in live mode (cycle.ts --save <save>). Never loads, launches or closes the game.
// Fixes under test, switched at runtime: ts3fixes `fastreftable` and `cachedreflection` (agent patches), ts3native footprint index
// (export ts3native_footprintMode), save compress (ts3native_counters +0x150, Verify +0x154) and save flush (+0x180).
// Every save is a new Matrix-<world>-<label>-<n> (Baseline-* refused); overwrites only hit the current Matrix-* save.
// Output: probe-out/save-matrix/<date>.jsonl (one row per case) and docs/data/results/<date>-save-matrix.md (one table per run).
// Windows-only by nature (game paths); times UTC.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { appendFileSync, existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync, openSync, readSync, closeSync } from 'node:fs';
import { homedir } from 'node:os';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { readIndex, refpack } from '../extract-assemblies.ts';

const { values } = parseArgs({ options: {
  cases: { type: 'string' }, list: { type: 'boolean' }, 'self-check': { type: 'boolean' }, travel: { type: 'boolean' },
  to: { type: 'string', default: 'france' }, 'reload-check': { type: 'boolean' }, off: { type: 'boolean' }, 'case-timeout': { type: 'string', default: '900' },
  port: { type: 'string', default: '47891' },
} });
const ROOT = resolve(import.meta.dirname, '..', '..');
const USER_DIR = process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3');
const SAVES = join(USER_DIR, 'Saves');
const BIN = process.env.TS3_BIN ?? 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3\\Game\\Bin';
const LOG = join(BIN, 'ts3native.log');
const OUT = join(ROOT, 'probe-out', 'save-matrix');
const RES = join(ROOT, 'docs', 'data', 'results');
const PENDING = join(OUT, 'reload-pending.json');
const date = new Date().toISOString().slice(0, 10);
const base = `http://127.0.0.1:${values.port}`;
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const log = (m: string) => console.log(`${stamp()} ${m}`);

// ---------------------------------------------------------------- pure parts (covered by --self-check)

type Check = { name: string; status: 'pass' | 'fail' | 'info' | 'skip'; why: string };
export type SaveEv = { result: number; travel: string; totalMs: number; backup: string; deferred: number;
  footprint?: { verify: string; mismatches: number; mode: number; fallbacks: number }; compressMismatches?: number;
  mismatchLines: number; flush?: { calls: number; files: number; fails: number; ms: number } };

// ts3native.log lines of the saves since an offset. footprint and compress lines come during the save, before its
// `save:` line; the flush worker logs after it.
export function parseLog(text: string): SaveEv[] {
  const evs: SaveEv[] = [];
  let fp: SaveEv['footprint'], cm: number | undefined, mm = 0, m: RegExpMatchArray | null;
  for (const line of text.split(/\r?\n/)) {
    if ((m = line.match(/^footprint: .* verify (off|ok|mismatches (\d+)) \(mode (\d+) fallbacks (\d+)/)))
      fp = { verify: m[1].split(' ')[0], mismatches: Number(m[2] ?? 0), mode: Number(m[3]), fallbacks: Number(m[4]) };
    else if (/^save compress MISMATCH/.test(line)) mm++;
    else if ((m = line.match(/^save compress: \d+ records.* mismatches (\d+)/))) cm = Number(m[1]);
    else if ((m = line.match(/^save: result (\d+) travel (\d+\/\d+) total (\d+) ms: .*backup \d+ \(([^,]+),(?:.*flushes deferred (\d+))?/))) {
      evs.push({ result: Number(m[1]), travel: m[2], totalMs: Number(m[3]), backup: m[4], deferred: Number(m[5] ?? 0), footprint: fp, compressMismatches: cm, mismatchLines: mm });
      fp = undefined; cm = undefined; mm = 0;
    } else if ((m = line.match(/^save flush: (\d+) calls, (\d+) files flushed on the worker, (\d+) fails, (\d+) ms/)) && evs.length)
      evs.at(-1)!.flush = { calls: Number(m[1]), files: Number(m[2]), fails: Number(m[3]), ms: Number(m[4]) };
  }
  return evs;
}

// file -> resource key (type:group:instance, or 'raw' for a non-package file) -> sha1 of the uncompressed bytes
export type Snap = Map<string, Map<string, string>>;
// resource types (or raw:<file>) with any changed, missing or extra resource
export function differingTypes(a: Snap, b: Snap): Map<string, string> {
  const out = new Map<string, string>();
  for (const f of new Set([...a.keys(), ...b.keys()])) {
    const x = a.get(f) ?? new Map(), y = b.get(f) ?? new Map();
    for (const k of new Set([...x.keys(), ...y.keys()])) {
      const t = k === 'raw' ? `raw:${f}` : k.split(':')[0];
      const why = !x.has(k) ? 'only in B' : !y.has(k) ? 'only in A' : x.get(k) !== y.get(k) ? 'changed' : '';
      if (why) out.set(t, `${out.has(t) ? out.get(t)! + ', ' : ''}${f.slice(-24)} ${k} ${why}`.slice(0, 300));
    }
  }
  return out;
}
export function judgeDiff(diff: Map<string, string>, unstable: Set<string>) {
  const bad = [...diff.keys()].filter(t => !unstable.has(t)), ignored = [...diff.keys()].filter(t => unstable.has(t));
  return { ok: bad.length === 0, bad: bad.map(t => `${t} (${diff.get(t)})`), ignored };
}
export function assertSafeName(n: string) {
  if (/^baseline-/i.test(n)) throw new Error(`refusing to write save ${n}: Baseline-* saves are read-only`);
  if (!n.startsWith('Matrix-')) throw new Error(`refusing to write save ${n}: matrix saves start with Matrix-`);
}

// ScriptError_*.xml -> signature: the error text before the assembly list, numbers/addresses masked (ids differ per load)
export function errSig(xml: string): string {
  const body = (xml.match(/<Content>([\s\S]*?)(?:Loaded Assemblies:|<\/Content>)/)?.[1] ?? xml).replace(/0x[0-9a-f]+/gi, '#').replace(/\d+/g, '#');
  return body.split(/\r?\n/).map(l => l.trim()).filter(Boolean).slice(0, 8).join(' | ');
}
// reload verdict: an error that also appears after loading the OFF save is the game's, not ours (2026-10-14 ReadBook)
export type ReloadSide = { save: string; households: string[]; files: string[]; sigs: string[] };
export function compareReload(on: ReloadSide, off: ReloadSide): Check[] {
  const offSigs = new Set(off.sigs), fresh = [...new Set(on.sigs.filter(x => !offSigs.has(x)))];
  const same = JSON.stringify(on.households) === JSON.stringify(off.households);
  return [
    { name: 'ON and OFF households', status: same ? 'pass' : 'fail', why: same ? `${on.households.length} households in both` : `ON only ${on.households.filter(h => !off.households.includes(h)).join(', ') || '-'}; OFF only ${off.households.filter(h => !on.households.includes(h)).join(', ') || '-'}` },
    { name: 'ScriptError files per save', status: 'info', why: `ON ${on.files.length}, OFF ${off.files.length}` },
    { name: 'new errors with fixes on', status: fresh.length ? 'fail' : 'pass', why: fresh.length ? `${fresh.length} not seen with OFF: ${fresh.map(x => x.slice(0, 150)).join('; ')}` : `0 (ON ${new Set(on.sigs).size} distinct, all also with OFF)` },
  ];
}

if (values['self-check']) {
  const assert = (await import('node:assert')).strict;
  const text = [
    'OK   footprint index: 0x00666488, 0x0082C940, 0x00AAC320 (Mode=1 Verify=0)',
    'footprint: levels 120 entries 5000 matched 5000 calls 40 ms 12 verify ok (mode 1 fallbacks 0 worldSave ms 900)',
    'save compress MISMATCH: len 10 flags 0x0 game 1 ours 2',
    'save compress: 900 records, 40 jobs (12 MB), hits 880, misses 20, run here 0, mismatches 1, commit 300 ms, wait 2 ms, peak 8 MB',
    'save: result 0 travel 0/0 total 4100 ms: info 10, write 3000, backup 200 (worker copy, worker 150, wait 0), copy 5, final 2 ms, flushes deferred 2',
    'save flush: 2 calls, 31 files flushed on the worker, 0 fails, 410 ms',
    'save: result 5 travel 1/1 total 9000 ms: info 10, write 3000, backup 0 (game, worker 0, wait 0), copy 5, final 2 ms, flushes deferred 0',
  ].join('\r\n');
  const e = parseLog(text);
  assert.equal(e.length, 2);
  assert.deepEqual(e[0].footprint, { verify: 'ok', mismatches: 0, mode: 1, fallbacks: 0 });
  assert.equal(e[0].compressMismatches, 1); assert.equal(e[0].mismatchLines, 1); assert.equal(e[0].backup, 'worker copy');
  assert.deepEqual(e[0].flush, { calls: 2, files: 31, fails: 0, ms: 410 });
  assert.equal(e[1].result, 5); assert.equal(e[1].travel, '1/1'); assert.equal(e[1].footprint, undefined); assert.equal(e[1].flush, undefined);
  assert.equal(parseLog('footprint: levels 1 entries 1 matched 0 calls 1 ms 0 verify mismatches 3 (mode 1 fallbacks 2 worldSave ms 1)\nsave: result 0 travel 0/0 total 1 ms: info 0, write 0, backup 0 (game, worker 0, wait 0), copy 0, final 0 ms, flushes deferred 0')[0].footprint!.mismatches, 3);
  const snap = (o: Record<string, Record<string, string>>): Snap => new Map(Object.entries(o).map(([f, r]) => [f, new Map(Object.entries(r))]));
  const A = snap({ 'w.nhd': { 'd84e7fc6:0:1': 'a', '0166038c:0:1': 'x', '0166038c:0:2': 'y' }, 'thumb.jpg': { raw: 'p' } });
  const B = snap({ 'w.nhd': { 'd84e7fc6:0:1': 'b', '0166038c:0:1': 'x', '025ed6f4:0:9': 'z' }, 'thumb.jpg': { raw: 'q' } });
  const d = differingTypes(A, B);
  assert.deepEqual([...d.keys()].sort(), ['0166038c', '025ed6f4', 'd84e7fc6', 'raw:thumb.jpg']);
  const j = judgeDiff(d, new Set(['d84e7fc6', 'raw:thumb.jpg']));
  assert.equal(j.ok, false); assert.deepEqual(j.ignored.sort(), ['d84e7fc6', 'raw:thumb.jpg']); assert.equal(j.bad.length, 2);
  assert.equal(judgeDiff(differingTypes(A, A), new Set()).ok, true);
  const xe = (t: string) => `<NRaas.ErrorTrap>\n<Content>\n\n${t}\n\nLoaded Assemblies:\n Ts3Fixes\n</Content>`;
  assert.equal(errSig(xe('Destroyed Object Found: Glass 0x1A2B id 42')), errSig(xe('Destroyed Object Found: Glass 0xFFFF id 7')));
  const side = (sigs: string[], hh = ['A:2']) => ({ save: 's', households: hh, files: sigs.map((_, i) => `f${i}`), sigs });
  const book = errSig(xe('ReadBook: no carry state machine')), ours = errSig(xe('NullReferenceException at Ts3Fixes.X'));
  const v = (c: Check[], n: string) => c.find(x => x.name === n)!.status;
  assert.equal(v(compareReload(side([book]), side([book, book])), 'new errors with fixes on'), 'pass');
  assert.equal(v(compareReload(side([book, ours]), side([book])), 'new errors with fixes on'), 'fail');
  assert.equal(v(compareReload(side([]), side([], ['B:1'])), 'ON and OFF households'), 'fail');
  assert.throws(() => assertSafeName('Baseline-BP')); assert.throws(() => assertSafeName('MySave')); assertSafeName('Matrix-BP-off-1');
  console.log('self-check ok');
  process.exit(0);
}

// ---------------------------------------------------------------- game access

let deadline = Infinity; // per case; every wait checks it
async function get(path: string, timeoutMs = 30000): Promise<any> {
  return (await fetch(`${base}/${path}`, { signal: AbortSignal.timeout(timeoutMs) })).json();
}
async function ag(cmd: string, args?: object): Promise<any> {
  const q = new URLSearchParams({ cmd, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  try { return await get(`agent?${q}`, 20000); } catch (e) { return { ok: false, error: String(e) }; }
}
async function until(what: string, limitS: number, check: () => Promise<boolean>, everyMs = 500): Promise<number> {
  const t0 = Date.now();
  while (!(await check().catch(() => false))) {
    if (Date.now() - t0 > limitS * 1000) throw new Error(`${what}: not reached within ${limitS} s`);
    if (Date.now() > deadline) throw new Error(`case timeout while waiting for ${what}`);
    await sleep(everyMs);
  }
  return Date.now() - t0;
}
const logSize = () => existsSync(LOG) ? statSync(LOG).size : 0;
function logFrom(off: number): string {
  if (!existsSync(LOG)) return '';
  const fd = openSync(LOG, 'r'), n = statSync(LOG).size - off;
  try { if (n <= 0) return ''; const b = Buffer.alloc(n); readSync(fd, b, 0, n, off); return b.toString('latin1'); } finally { closeSync(fd); }
}
const NATIVE_KEYS = ['saveCount', 'saveFallbacks', 'compressReady', 'compressMode', 'compressVerify', 'compressJobs', 'compressHits',
  'compressMisses', 'compressMismatches', 'flushReady', 'flushMode', 'flushJobs', 'flushWaits', 'flushFails'];
async function counters() {
  const n = await get('native').catch(() => null);
  const p = (await ag('patches')).result ?? {};
  const fp = await get('nativeexport?name=ts3native_footprintMode').catch(() => null);
  return { native: n ? Object.fromEntries(NATIVE_KEYS.map(k => [k, n[k]])) : null, footprintMode: fp?.value ?? null,
    fastreftable: p.fastreftable ?? null, reftable_converted: p.reftable_converted ?? null, reftable_max_entries: p.reftable_max_entries ?? null,
    reftable_grows: p.reftable_grows ?? null, cachedreflection: p.cachedreflection ?? null, typecache_started: p.typecache_started ?? null,
    typecache_hits: p.typecache_hits ?? null, typecache_stored: p.typecache_stored ?? null };
}
type Counters = Awaited<ReturnType<typeof counters>>;
const delta = (a: Counters, b: Counters, k: string) => (b.native?.[k] ?? 0) - (a.native?.[k] ?? 0);

// ---------------------------------------------------------------- fixes

const FIXES = ['fastreftable', 'cachedreflection', 'footprint', 'compress', 'flush'] as const;
type Fix = typeof FIXES[number];
const ready: Record<Fix, boolean> = { fastreftable: false, cachedreflection: false, footprint: false, compress: false, flush: false };
async function probeReady() {
  const p = await ag('patches');
  ready.fastreftable = p.ok && typeof p.result?.fastreftable === 'boolean' && !!p.result?.applied;
  // the mod's startup probe (byte-identical test serialization with the cache off and on) must have passed
  ready.cachedreflection = p.ok && typeof p.result?.cachedreflection === 'boolean'
    && (p.result?.applied ?? []).some((s: unknown) => /^CachedReflection probe .*: ok$/.test(String(s)));
  // the export exists whenever ts3native loads; only the OK line says the hooks are in (log is rewritten per game start)
  ready.footprint = (await get('nativeexport?name=ts3native_footprintMode').catch(() => null)) != null && /^OK +footprint index/m.test(logFrom(0));
  const n = await get('native').catch(() => null);
  ready.compress = n?.compressReady === 1;
  ready.flush = n?.flushReady === 1;
}
const nx = (q: string) => get(`nativeexport?${q}`).catch(() => null);
async function setFix(f: Fix, on: boolean): Promise<boolean | null> {
  if (!ready[f]) return null;
  const v = on ? 1 : 0;
  if (f === 'fastreftable') return (await ag('patches', { fastreftable: on })).result?.fastreftable ?? null;
  if (f === 'cachedreflection') return (await ag('patches', { cachedreflection: on })).result?.cachedreflection ?? null;
  const r = f === 'footprint' ? await nx(`name=ts3native_footprintMode&value=${v}`)
    : await nx(`name=ts3native_counters&offset=${f === 'compress' ? '0x150' : '0x180'}&value=${v}`);
  return r ? r.value === 1 : null;
}
async function setFixes(on: Partial<Record<Fix, boolean>>) {
  const got: Record<string, boolean | null> = {};
  for (const f of FIXES) got[f] = await setFix(f, on[f] ?? false);
  return got;
}
const allOn = Object.fromEntries(FIXES.map(f => [f, true])) as Record<Fix, boolean>;

// ---------------------------------------------------------------- saves

type SaveRec = { name: string; ok: boolean; error?: string; wallMs: number; ev?: SaveEv; before: Counters; after: Counters; off: number };
let world = '';
function nextName(label: string): string {
  let n = 1;
  while (existsSync(join(SAVES, `Matrix-${world}-${label}-${n}.sims3`))) n++;
  const name = `Matrix-${world}-${label}-${n}`;
  assertSafeName(name);
  return name;
}
async function dialogs() { const d = await ag('auto_dialog'); if (d.ok && d.result?.open) log(`dialog "${String(d.result.title ?? '').slice(0, 60)}" -> ${d.result.clicked}`); }
async function pause() { await ag('set_speed', { speed: 'pause' }); }

// label: new save Matrix-<world>-<label>-<n>; overwrite: that existing Matrix-* name again. Always by name: a nameless
// save goes to the game's current file, which may still be the save you loaded.
async function save(label: string, o: { overwrite?: string; keepSpeed?: boolean; flushWait?: boolean } = {}): Promise<SaveRec> {
  const name = o.overwrite ?? nextName(label);
  assertSafeName(name);
  if (!o.keepSpeed) await pause();
  await dialogs();
  const before = await counters(), off = logSize(), t0 = Date.now();
  let r: any;
  try {
    await until('save accepted', 30, async () => {
      r = await ag('save_game', { name });
      return r.ok || !/in progress|cannot save|dialog/.test(String(r.error)); // previous save still finishing: retry
    }, 250);
  } catch { /* still refused after 30 s: reported below with the game's last answer */ }
  if (!r.ok) { log(`save ${name}: refused: ${r.error}`); return { name, ok: false, error: String(r.error), wallMs: 0, before, after: before, off }; }
  let ev: SaveEv | undefined;
  try {
    await until(`save ${name}`, 600, async () => {
      ev = parseLog(logFrom(off))[0];
      return !!ev && (await ag('game_state')).result?.save_in_progress === false;
    });
    if (ev!.deferred > 0 && o.flushWait !== false)
      await until(`save flush ${name}`, 60, async () => !!(ev = parseLog(logFrom(off))[0])?.flush).catch(() => {});
  } catch (e) { log(`save ${name}: ${e}`); }
  const wallMs = Date.now() - t0, after = await counters();
  log(`saved ${name}: ${ev ? `result ${ev.result} travel ${ev.travel} total ${ev.totalMs} ms, backup ${ev.backup}, flush ${ev.flush ? `${ev.flush.fails} fails` : ev.deferred ? 'pending' : 'none'}` : 'NO save line in ts3native.log'} (${(wallMs / 1000).toFixed(1)} s)`);
  return { name, ok: !!ev && ev.result === 0, ev, wallMs, before, after, off };
}

// checks every save gets; strict also fails fallbacks (backup copied by the game, footprint index not used)
function saveChecks(s: SaveRec, strict = false): Check[] {
  const c: Check[] = [];
  const ev = s.ev;
  c.push({ name: `${s.name} result 0`, status: s.ok ? 'pass' : 'fail', why: s.error ?? (ev ? `result ${ev.result}` : 'no save line in ts3native.log') });
  const cm = delta(s.before, s.after, 'compressMismatches') + (ev?.compressMismatches ?? 0) + (ev?.mismatchLines ?? 0);
  c.push({ name: `${s.name} compress mismatches`, status: cm ? 'fail' : 'pass', why: `counter +${delta(s.before, s.after, 'compressMismatches')}, log ${ev?.compressMismatches ?? '-'} / ${ev?.mismatchLines ?? 0} lines` });
  const ff = delta(s.before, s.after, 'flushFails') + (ev?.flush?.fails ?? 0);
  c.push({ name: `${s.name} flush fails`, status: ff ? 'fail' : ev?.deferred && !ev.flush ? 'fail' : 'pass',
    why: ev?.deferred && !ev.flush ? `${ev.deferred} flushes deferred, no flush line within 60 s` : `counter +${delta(s.before, s.after, 'flushFails')}, log ${ev?.flush?.fails ?? '-'}` });
  if (ev?.footprint) c.push({ name: `${s.name} footprint verify`, status: ev.footprint.verify === 'mismatches' ? 'fail' : 'pass', why: `verify ${ev.footprint.verify}${ev.footprint.mismatches ? ' ' + ev.footprint.mismatches : ''}` });
  const fb = (ev?.footprint?.fallbacks ?? 0), sf = delta(s.before, s.after, 'saveFallbacks');
  c.push({ name: `${s.name} fallbacks`, status: fb || sf || ev?.backup === 'fallback' ? (strict ? 'fail' : 'info') : 'pass', why: `footprint ${fb}, save backup ${ev?.backup ?? '-'} (counter +${sf})` });
  return c;
}

const dirOf = (n: string) => join(SAVES, `${n}.sims3`);
function snapshot(dir: string): Snap {
  const out: Snap = new Map();
  for (const f of readdirSync(dir)) {
    const pkg = readFileSync(join(dir, f)), m = new Map<string, string>();
    if (pkg.toString('latin1', 0, 4) !== 'DBPF') m.set('raw', createHash('sha1').update(pkg).digest('hex'));
    else for (const e of readIndex(pkg)) { // same resource keys and decompression as save-diff.ts
      const raw = pkg.subarray(e.offset, e.offset + e.fileSize);
      const hex = (n: number) => n.toString(16).padStart(8, '0');
      m.set(`${hex(e.type)}:${hex(e.group)}:${e.inst.toString(16).padStart(16, '0')}`, createHash('sha1').update(e.compressed === 0xffff ? refpack(raw) : raw).digest('hex'));
    }
    out.set(f, m);
  }
  return out;
}
const fileHashes = (dir: string) => existsSync(dir) ? Object.fromEntries(readdirSync(dir).sort().map(f => [f, createHash('sha1').update(readFileSync(join(dir, f))).digest('hex')])) : null;

let unstable: Set<string> | null = null;
const snaps = new Map<string, Snap>(); // per-fix compares one off save four times
const snap = (n: string) => snaps.get(n) ?? snaps.set(n, snapshot(dirOf(n))).get(n)!;
function diffCheck(a: SaveRec, b: SaveRec, what: string): Check {
  if (!a.ok || !b.ok) return { name: `${what} identical`, status: 'fail', why: 'a save failed, nothing to compare' };
  const j = judgeDiff(differingTypes(snap(a.name), snap(b.name)), unstable ?? new Set());
  return { name: `${what} identical`, status: j.ok ? 'pass' : 'fail',
    why: `${j.ok ? 'all stable resources identical' : 'differs: ' + j.bad.join('; ')}${j.ignored.length ? `; ignored (unstable in control): ${j.ignored.join(', ')}` : ''}` };
}
function missingCheck(fixes: Record<string, boolean | null>, want: Fix[]): Check[] {
  const miss = want.filter(f => fixes[f] !== true);
  return miss.length ? [{ name: 'fixes switched on', status: 'fail', why: `not available or did not switch: ${miss.join(', ')} (check ts3native.ini Enabled=1 and ts3fixes)` }] : [];
}

// ---------------------------------------------------------------- cases

type Result = { fixes: Record<string, unknown>; saves: SaveRec[]; checks: Check[]; pending?: boolean };
type Case = { id: string; desc: string; optIn?: boolean; timeoutS?: number; run: () => Promise<Result> };

// one line a minute while a trip is pending, so a stalled trip shows where it stopped (2026-10-16 bp-future-travel-2:
// 20 min with no arrival and nothing in the log to tell "never left" from "never arrived")
let lastTrip = 0;
async function tripStatus(): Promise<void> {
  if (Date.now() - lastTrip < 60000) return;
  lastTrip = Date.now();
  const st = await get('agentstatus', 5000).catch(() => null);
  const ts = await ag('travel_state').catch(() => null), t = ts?.ok ? ts.result : {};
  const q = await ag('queues').catch(() => null);
  const queue = q?.ok ? q.result.map((i: any) => `${i.sim}:${i.name ?? i.type}`).join(', ') || 'empty' : q?.error ?? '?';
  log(`trip status: loaded ${st?.worldLoaded} in_travel ${t.in_travel} world ${t.world} dest ${t.destination} away ${t.away}; queue ${queue}`);
}

// every shown need of every household Sim to max, then check the six core needs (occult/pet motives have other ranges)
const CORE_NEEDS = ['Hunger', 'Bladder', 'Energy', 'Social', 'Hygiene', 'Fun'];
async function maxNeeds(): Promise<void> {
  const mo = await ag('set_motive', { sim: 'household' });
  if (!mo.ok) throw new Error(`set_motive household: ${mo.error}`);
  const low = Object.entries(mo.result as Record<string, Record<string, number>>)
    .flatMap(([sim, m]) => Object.entries(m).filter(([k, v]) => CORE_NEEDS.includes(k) && v < 90).map(([k, v]) => `${sim} ${k} ${v}`));
  if (low.length) throw new Error(`needs not at max before the trip: ${low.join(', ')}`);
  log(`needs at max: ${Object.keys(mo.result).join(', ')}`);
}

async function enterWorld(cmd: string, args: object, from: string): Promise<{ world: string; travelEvs: SaveEv[] }> {
  const off = logSize();
  // A tired Sim drops a queued trip for "Sleep Until Fully Rested" (2026-10-16 university and future runs): every
  // household need at max before the trip, checked. Test-only cheat; the trip's travel saves are what is under test.
  await maxNeeds();
  const r = await ag(cmd, args);
  if (!r.ok) throw new Error(`${cmd}: ${r.error}`);
  log(`${cmd} ${JSON.stringify(r.result)}`);
  // the agent answers queued:false when the trip cannot start (e.g. time portal not active): fail now, not after 20 min
  if (r.result?.queued === false) throw new Error(`${cmd}: trip not queued (${JSON.stringify(r.result)})`);
  // the trip is a queued interaction: it never starts while the clock is paused (save() pauses; 2026-10-15 IP run)
  await ag('set_speed', { speed: 'fast' });
  // Fail fast (Rodney 2026-10-16): every 30 s check the trip is still alive; two misses in a row (~1 min) end the case
  // instead of the 15/20 min waits. `until` swallows errors from its check, so a dead trip is flagged and thrown after.
  let n = 0, w = '', dead = '', misses = 0;
  const tripQueued = async (): Promise<boolean | null> => {
    const q = await ag('queues');
    return q.ok ? q.result.some((i: any) => /travel|moving ?van|portal/i.test(`${i.type} ${i.name}`)) : null;
  };
  await until(`left ${from}`, 900, async () => {
    if (++n % 3 === 0) await dialogs();
    if (n % 60 === 0) await maxNeeds().catch(e => log(String(e))); // keep needs full while the Sim walks to the trip
    await tripStatus();
    if ((await get('agentstatus', 5000)).worldLoaded === false) return true;
    const ts = await ag('travel_state');
    if (ts.ok && (ts.result.in_travel === true || ts.result.world !== from)) return true;
    if (n % 30 === 0) {
      misses = (await tripQueued()) === false ? misses + 1 : 0;
      if (misses >= 2) { dead = `trip dropped: no travel interaction in the household queue for ~1 min (still in ${from})`; return true; }
    }
    return false;
  }, 1000);
  if (dead) throw new Error(dead);
  misses = 0;
  await until('arrived in live mode', 1200, async () => {
    await tripStatus();
    if ((await get('agentstatus', 5000)).worldLoaded !== true) { misses = 0; return false; }
    if (++n % 10 === 0) await dialogs();
    const ts = await ag('travel_state');
    if (!ts.ok || ts.result.in_travel) { misses = 0; return false; }
    if (ts.result.world === from) { // loaded, not travelling, still the start world: the switch was called off
      if (n % 30 === 0 && ++misses >= 2) { dead = `trip aborted: back in live mode in ${from} for ~1 min without travelling`; return true; }
      return false;
    }
    w = ts.result.world;
    return (await ag('game_state')).result?.sub_state === 'LiveModeState';
  }, 1000);
  if (dead) throw new Error(dead);
  log(`arrived in ${w}`);
  return { world: w, travelEvs: parseLog(logFrom(off)).filter(e => e.travel.startsWith('1/')) };
}
function travelSaveChecks(what: string, evs: SaveEv[]): Check[] {
  if (!evs.length) return [{ name: `${what} travel save`, status: 'fail', why: 'no `save: ... travel 1/1` line during the trip' }];
  return evs.map((e, i) => ({ name: `${what} travel save ${i + 1}`, status: e.result === 0 && !(e.flush?.fails) && !e.compressMismatches && !e.mismatchLines && e.footprint?.verify !== 'mismatches' ? 'pass' : 'fail',
    why: `result ${e.result} travel ${e.travel} total ${e.totalMs} ms, flush fails ${e.flush?.fails ?? '-'}, compress mismatches ${e.compressMismatches ?? '-'}` }));
}
async function householdsNow() {
  const h = (await ag('list_households')).result ?? [];
  return { households: h.map((x: any) => `${x.name}:${x.members}`).sort(), sims_in_world: (await ag('travel_state')).result?.sims_in_world ?? null };
}

function printReload(name: string, flag: string) {
  console.log(`\nreload: run these yourself (restart closes the game):\n  node tools/harness/cycle.ts --restart\n  python tools/probe/ts3probe.py serve --launch\n  node tools/harness/cycle.ts --save ${name}.sims3\n  node tools/harness/save-matrix.ts --reload-check${flag}\n`);
}

const CASES: Case[] = [
  { id: 'control', desc: 'two saves with all fixes off; the resource types that differ are naturally unstable in this world (identical/per-fix ignore exactly those)', run: async () => {
    const fixes = await setFixes({});
    const a = await save('off'), b = await save('off');
    const checks = [...saveChecks(a), ...saveChecks(b)];
    if (a.ok && b.ok) {
      const d = differingTypes(snapshot(dirOf(a.name)), snapshot(dirOf(b.name)));
      unstable = new Set(d.keys());
      checks.push({ name: 'unstable types', status: 'info', why: unstable.size ? [...d].map(([t, w]) => `${t} (${w.slice(0, 120)})`).join('; ') : 'none' });
      if (!unstable.has('d84e7fc6')) checks.push({ name: 'known per-save type d84e7fc6', status: 'info', why: 'did not differ in this world' });
    } else checks.push({ name: 'unstable types', status: 'fail', why: 'control saves failed; identical/per-fix cannot run' });
    return { fixes, saves: [a, b], checks };
  } },
  { id: 'identical', desc: 'paused: save all fixes off, then all on; every stable resource byte-identical after decompression', run: async () => {
    await setFixes({});
    const a = await save('off');
    const fixes = await setFixes(allOn);
    const b = await save('on');
    return { fixes, saves: [a, b], checks: [...missingCheck(fixes, [...FIXES]), ...saveChecks(a), ...saveChecks(b), diffCheck(a, b, 'off vs on')] };
  } },
  { id: 'per-fix', desc: 'each fix alone on vs all off (one off save), identical check; names the fix that changes a save', run: async () => {
    await setFixes({});
    const a = await save('pf-off');
    const saves = [a], checks = saveChecks(a), fixes: Record<string, unknown> = {};
    for (const f of FIXES) {
      const got = await setFixes({ [f]: true });
      fixes[f] = got[f];
      if (got[f] !== true) { checks.push({ name: `${f} alone`, status: 'skip', why: 'fix not available in this game (ini/mod)' }); continue; }
      const b = await save(`pf-${f}`);
      saves.push(b); checks.push(...saveChecks(b), diffCheck(a, b, `${f} alone vs off`));
    }
    return { fixes, saves, checks };
  } },
  { id: 'overwrite', desc: 'fixes on: new save, then save over it twice; result 0, .backup equals the previous save byte for byte, no mismatches/fails/fallbacks', run: async () => {
    const fixes = await setFixes(allOn);
    const s1 = await save('ow');
    const saves = [s1], checks = [...missingCheck(fixes, [...FIXES]), ...saveChecks(s1, true)];
    let prev = fileHashes(dirOf(s1.name));
    for (let i = 2; i <= 3; i++) {
      const s = await save('ow', { overwrite: s1.name });
      saves.push(s); checks.push(...saveChecks(s, true));
      const bak = fileHashes(`${dirOf(s.name)}.backup`);
      const same = !!bak && !!prev && JSON.stringify(bak) === JSON.stringify(prev);
      const why = !bak ? `${s.name}.sims3.backup missing` : same ? `${Object.keys(bak).length} files equal the previous save`
        : `differs: ${Object.keys({ ...bak, ...prev }).filter(f => bak[f] !== prev?.[f]).join(', ')}`;
      checks.push({ name: `overwrite ${i - 1} backup = previous save`, status: same ? 'pass' : 'fail', why });
      prev = fileHashes(dirOf(s.name));
    }
    return { fixes, saves, checks };
  } },
  { id: 'back-to-back', desc: 'fixes on: second save right after the first returns (flush worker wait path); result 0 both, no fails', run: async () => {
    const fixes = await setFixes(allOn);
    const a = await save('b2b', { flushWait: false }), b = await save('b2b');
    a.ev = parseLog(logFrom(a.off))[0] ?? a.ev; // the first save's flush line came while the second waited
    const fw = delta(a.before, b.after, 'flushWaits');
    return { fixes, saves: [a, b], checks: [...missingCheck(fixes, [...FIXES]), ...saveChecks(a), ...saveChecks(b),
      { name: 'flush waits', status: 'info', why: `+${fw} (0 means the worker had finished before the second save)` }] };
  } },
  { id: 'unpaused', desc: 'fixes on: save while the game runs at speed 3 (normal player behaviour)', run: async () => {
    const fixes = await setFixes(allOn);
    await ag('set_speed', { speed: 'ultra' });
    await sleep(5000);
    const sp = (await ag('time')).result?.speed;
    const s = await save('run', { keepSpeed: true });
    await pause();
    return { fixes, saves: [s], checks: [...missingCheck(fixes, [...FIXES]), { name: 'running at save', status: sp === 'Triple' ? 'pass' : 'fail', why: `speed ${sp}` }, ...saveChecks(s)] };
  } },
  { id: 'build-mode', desc: 'fixes on: save in build mode (agent build_mode)', run: async () => {
    const fixes = await setFixes(allOn);
    await pause();
    const r = await ag('build_mode', { on: true });
    if (!r.ok) return { fixes, saves: [], checks: [{ name: 'enter build mode', status: 'fail', why: String(r.error) }] };
    let st = '';
    await until('build mode', 120, async () => /Build/.test(st = (await ag('game_state')).result?.sub_state ?? ''));
    let s: SaveRec;
    try { s = await save('build', { keepSpeed: true }); }
    finally {
      await ag('build_mode', { on: false });
      await until('live mode', 120, async () => { await dialogs(); return (await ag('game_state')).result?.sub_state === 'LiveModeState'; });
    }
    // the game itself refuses saves in build mode (CanSaveGame false): nothing of ours to test there
    if (!s.ok && /cannot save/.test(s.error ?? ''))
      return { fixes, saves: [s], checks: [{ name: 'save in build mode', status: 'skip', why: `game refuses: ${s.error}` }] };
    return { fixes, saves: [s], checks: [...missingCheck(fixes, [...FIXES]), { name: 'in build mode at save', status: 'pass', why: st }, ...saveChecks(s)] };
  } },
  { id: 'verify-counters', desc: 'fixes on with compress Verify (+0x154, runtime) on: game compressor runs too, mismatches 0; footprint verify if the ini has it', run: async () => {
    const fixes = await setFixes(allOn);
    const checks = missingCheck(fixes, [...FIXES]);
    const prev = ready.compress ? (await nx('name=ts3native_counters&offset=0x154'))?.value : null;
    if (ready.compress) await nx('name=ts3native_counters&offset=0x154&value=1');
    const s = await save('verify');
    if (prev === 0 || prev === 1) await nx(`name=ts3native_counters&offset=0x154&value=${prev}`);
    checks.push(...saveChecks(s));
    // 0.10.1: parallel compression has no batch path in a real save (records compress on close, docs/15 Phase 10), so the
    // compress line only measures; verify has nothing to compare unless a batch ran
    checks.push(ready.compress && s.ev?.compressMismatches !== undefined ? { name: 'compress verify ran', status: s.ev.compressMismatches === 0 && !s.ev.mismatchLines ? 'pass' : 'fail', why: `log mismatches ${s.ev.compressMismatches}` }
      : { name: 'compress verify ran', status: 'skip', why: ready.compress ? 'no batched compression in a save (measurement only)' : 'save compress not loaded' });
    const fv = s.ev?.footprint?.verify;
    checks.push(fv === 'ok' ? { name: 'footprint verify ran', status: 'pass', why: 'verify ok' }
      : fv === 'mismatches' ? { name: 'footprint verify ran', status: 'fail', why: `${s.ev!.footprint!.mismatches} mismatches` }
      : { name: 'footprint verify ran', status: 'skip', why: 'verify off: set [FootprintIndex] Verify=1 in ts3native.ini (read at game start only)' });
    return { fixes, saves: [s], checks };
  } },
  { id: 'reload', desc: 'paused: save all off, then all on; load each (two restarts): households match, 60 s at speed 3, game alive; FAIL only on ScriptErrors with ON that OFF does not have', run: async () => {
    await setFixes({});
    const a = await save('reload-off');
    const fixes = await setFixes(allOn);
    const s = await save('reload-on');
    const exp = await householdsNow();
    writeFileSync(PENDING, JSON.stringify({ world, save: s.name, t: Date.now(), expected: exp, on: { save: s.name }, off: { save: a.name } }, null, 1));
    printReload(s.name, '');
    return { fixes, saves: [a, s], checks: [...missingCheck(fixes, [...FIXES]), ...saveChecks(a), ...saveChecks(s)], pending: true };
  } },
  { id: 'travel', optIn: true, timeoutS: 3600, desc: 'fixes on: vacation (travel save), save in the vacation world, travel home (travel save), save at home', run: async () => {
    const fixes = await setFixes(allOn);
    // the travel saves go to the game's current file; the loaded save may still be it, so never travel from a Baseline-*
    const start = String(st.result.save_name ?? '');
    if (/^baseline-/i.test(start)) return { fixes, saves: [], checks: [{ name: 'start save', status: 'fail', why: `${start} is a Baseline-* save: the travel saves could overwrite it; load a copy (e.g. a Matrix-* save) first` }] };
    const startDir = join(SAVES, `${start.replace(/.sims3$/, '')}.sims3`), startM = existsSync(startDir) ? statSync(startDir).mtimeMs : 0;
    const s0 = await save('travel-start'); // save-as: makes a Matrix-* save the current one
    const checks = [...missingCheck(fixes, [...FIXES]), ...saveChecks(s0)];
    if (!s0.ok) return { fixes, saves: [s0], checks };
    const home = (await ag('travel_state')).result?.world;
    await ag('autopilot', { enabled: true, speed: 'none', dialogs: true });
    // --to university | future use their own trip actions (same save function, different data); anything else is a vacation world
    const trip = values.to === 'university' ? ['travel_university', { days: 2 }] as const
      : values.to === 'future' ? ['travel_future', { prepare: true }] as const : ['travel_vacation', { world: values.to, days: 2 }] as const;
    const out = await enterWorld(trip[0], trip[1], home);
    checks.push(...travelSaveChecks('trip out', out.travelEvs));
    const s1 = await save('travel-away'); checks.push(...saveChecks(s1));
    const back = await enterWorld('travel_home', {}, out.world);
    checks.push(...travelSaveChecks('trip back', back.travelEvs), { name: 'back home', status: back.world === home ? 'pass' : 'fail', why: `${back.world} (home ${home})` });
    const s2 = await save('travel-home'); checks.push(...saveChecks(s2));
    const nowM = existsSync(startDir) ? statSync(startDir).mtimeMs : 0;
    checks.push({ name: 'travel saves target', status: 'info', why: nowM === startM ? `${start} untouched (travel saves went to the Matrix save)` : `${start} was rewritten by the travel saves (its .backup has the previous version)` });
    return { fixes, saves: [s0, s1, s2], checks };
  } },
];

if (values.list) {
  for (const c of CASES) console.log(`${c.id.padEnd(16)}${c.optIn ? '(--travel) ' : ''}${c.desc}`);
  process.exit(0);
}

// ---------------------------------------------------------------- run

mkdirSync(OUT, { recursive: true }); mkdirSync(RES, { recursive: true });
const jsonl = join(OUT, `${date}.jsonl`), md = join(RES, `${date}-save-matrix.md`);
let build = '';
try { build = execFileSync('git', ['-C', ROOT, 'rev-parse', '--short', 'HEAD']).toString().trim() + (execFileSync('git', ['-C', ROOT, 'status', '--porcelain', '-uno']).toString().trim() ? '+dirty' : ''); } catch { build = '?'; }
const rows: { id: string; pass: boolean; pending?: boolean; failed: string[] }[] = [];
const mdRows: string[] = [];
function record(c: { id: string; desc: string }, r: Result | null, err: string | null, before: Counters | null, after: Counters | null) {
  const checks = r?.checks ?? [];
  if (err) checks.push({ name: 'case ran', status: 'fail', why: err });
  const failed = checks.filter(x => x.status === 'fail').map(x => `${x.name}: ${x.why}`);
  const pass = !failed.length && !r?.pending;
  rows.push({ id: c.id, pass, pending: r?.pending, failed });
  appendFileSync(jsonl, JSON.stringify({ time: new Date().toISOString(), world, build, case: c.id, desc: c.desc, pass, pending: !!r?.pending, fixes: r?.fixes ?? null,
    saves: (r?.saves ?? []).map(s => ({ name: s.name, ok: s.ok, error: s.error, wallMs: s.wallMs, ev: s.ev })), counters: { before, after }, checks }) + '\n');
  const saves = r?.saves ?? [];
  mdRows.push(`| ${stamp()} | ${c.id} | ${r?.pending ? 'pending' : pass ? 'PASS' : '**FAIL**'} | ${saves.map(s => s.name).join(', ')} | ${saves.map(s => s.ev?.totalMs ?? '-').join(', ')} | ${(failed.length ? failed : checks.filter(x => x.status !== 'pass').map(x => `${x.status} ${x.name}: ${x.why}`)).join('<br>').replace(/\|/g, '/').slice(0, 900) || 'all checks pass'} |`);
  log(`${c.id}: ${r?.pending ? 'PENDING (reload commands above)' : pass ? 'PASS' : 'FAIL'}${failed.length ? '\n    ' + failed.join('\n    ') : ''}`);
}
function writeMd(title: string) {
  const head = existsSync(md) ? '' : `# Save matrix ${date}\n\nTool: \`tools/harness/save-matrix.ts\` (docs/TESTING.md). Times UTC. Save ms = ts3native \`save:\` total per save.\n`;
  const n = rows.filter(r => !r.pending).length, p = rows.filter(r => r.pass).length, f = rows.filter(r => !r.pass && !r.pending);
  const summary = `${n} cases, ${p} passed, ${f.length} failed${f.length ? ` (${f.map(r => r.id).join(', ')})` : ''}${rows.some(r => r.pending) ? `, pending: ${rows.filter(r => r.pending).map(r => r.id).join(', ')}` : ''}`;
  appendFileSync(md, `${head}\n## ${title}\n\n| time | case | result | saves | save ms | failed checks / notes |\n|---|---|---|---|---|---|\n${mdRows.join('\n')}\n\n${summary}\n`);
  log(`summary: ${summary}`);
  log(`results: ${jsonl}, ${md}`);
  return f.length;
}

const { watchGame } = await import('./game-alive.ts');
const st = await ag('game_state');
if (!st.ok || !st.result?.in_world) { console.error(`not in a world: ${JSON.stringify(st)}`); process.exit(2); }
world = String((await ag('travel_state')).result?.world ?? 'World').replace(/[^A-Za-z0-9]/g, '');
if (!existsSync(LOG)) { console.error(`no ts3native.log at ${LOG} (set TS3_BIN)`); process.exit(2); }

if (values['reload-check']) {
  const p = existsSync(PENDING) ? JSON.parse(readFileSync(PENDING, 'utf8')) : null;
  if (!p) { console.error(`no ${PENDING}: run case reload first`); process.exit(2); }
  const alive = watchGame(() => { record({ id: 'reload', desc: 'reload check' }, null, 'game died', null, null); writeMd(`${new Date().toISOString().slice(0, 16)} ${world} reload check, build ${build}`); });
  const side = values.off ? 'off' : 'on', other = values.off ? 'on' : 'off';
  p.on ??= { save: p.save }; // pending file from before the OFF save existed
  const target: string = p[side]?.save;
  if (!target) { console.error(`no ${side} save in ${PENDING}: run case reload again`); process.exit(2); }
  const checks: Check[] = [];
  // save_name stays null after load_save until the first save; load_file is the loaded path
  const name = st.result.save_name ?? String(st.result.load_file ?? '').split(/[\\/]/).pop()?.replace(/\.sims3$/, '');
  checks.push({ name: 'loaded the reload save', status: name === target ? 'pass' : 'fail', why: `loaded ${name}, expected ${target}` });
  const now = await householdsNow();
  const miss = p.expected.households.filter((h: string) => !now.households.includes(h)), extra = now.households.filter((h: string) => !p.expected.households.includes(h));
  checks.push({ name: 'households and members', status: miss.length || extra.length ? 'fail' : 'pass', why: miss.length || extra.length ? `missing ${miss.join(', ') || '-'}; new ${extra.join(', ') || '-'}` : `${now.households.length} households match` });
  checks.push({ name: 'sims in world', status: 'info', why: `${p.expected.sims_in_world} before save, ${now.sims_in_world} after load (NPCs come and go)` });
  const before = await counters();
  log('running 60 s at speed 3');
  await ag('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
  await sleep(60000);
  const t = (await ag('time')).result;
  await ag('autopilot', { enabled: false }); await pause();
  checks.push({ name: 'sim clock runs', status: t?.minutes_total ? 'pass' : 'fail', why: t ? `day ${t.day_number} ${t.clock}` : 'time: no answer' });
  // files since the save, minus those the other load already counted (both loads come after the save)
  const errs = readdirSync(USER_DIR).filter(f => /^ScriptError_.*\.xml$/i.test(f) && statSync(join(USER_DIR, f)).mtimeMs > p.t && !(p[other]?.files ?? []).includes(f));
  checks.push({ name: `ScriptError files (${side.toUpperCase()})`, status: 'info', why: errs.length ? errs.join(', ') : `none since the save at ${new Date(p.t).toISOString().slice(11, 19)}` });
  await alive.check();
  checks.push({ name: 'game alive', status: (await ag('game_state')).ok ? 'pass' : 'fail', why: 'agent answers' });
  p[side] = { save: target, households: now.households, files: errs, sigs: errs.map(f => errSig(readFileSync(join(USER_DIR, f), 'utf8'))) };
  writeFileSync(PENDING, JSON.stringify(p, null, 1));
  const done = !!p[other]?.sigs;
  if (done) checks.push(...compareReload(p.on, p.off));
  else if (p[other]?.save) printReload(p[other].save, values.off ? '' : ' --off');
  record({ id: 'reload', desc: `reload check of ${target}` }, { fixes: {}, saves: [], checks, pending: !done }, null, before, await counters());
  process.exit(writeMd(`${new Date().toISOString().slice(0, 16)} ${world} reload check of ${target} (${side.toUpperCase()}), build ${build}`) ? 1 : 0);
}

let want = values.cases ? values.cases.split(',').map(s => s.trim()) : CASES.filter(c => !c.optIn || values.travel).map(c => c.id);
if (values.travel && !want.includes('travel')) want.push('travel');
const unknown = want.filter(id => !CASES.some(c => c.id === id));
if (unknown.length) { console.error(`unknown cases: ${unknown.join(', ')} (--list)`); process.exit(2); }
if (want.some(id => id === 'identical' || id === 'per-fix') && !want.includes('control')) want = ['control', ...want]; // learns the unstable types
want = CASES.map(c => c.id).filter(id => want.includes(id)); // fixed order: control first, travel last

await probeReady();
const ap0 = (await ag('autopilot')).result, speed0 = (await ag('time')).result?.speed, fix0 = await counters();
log(`${world}, save ${st.result.save_name}, build ${build}; fixes available: ${FIXES.map(f => `${f} ${ready[f] ? 'yes' : 'NO'}`).join(', ')}; speed ${speed0}`);
log(`cases: ${want.join(', ')}`);
// autopilot clears dialogs (a modal blocks save_game) but must not hold a speed
await ag('autopilot', { enabled: true, speed: 'none', dialogs: true });
const alive = watchGame(() => { record({ id: 'run', desc: 'game alive' }, null, 'game died', null, null); writeMd(`${new Date().toISOString().slice(0, 16)} ${world}, build ${build} (game died)`); });
const started = new Date().toISOString().slice(0, 16);

for (const id of want) {
  const c = CASES.find(x => x.id === id)!;
  if ((id === 'identical' || id === 'per-fix') && !unstable) { record(c, null, 'control did not produce a baseline', null, null); continue; }
  log(`== ${c.id}: ${c.desc}`);
  deadline = Date.now() + (c.timeoutS ?? Number(values['case-timeout'])) * 1000;
  const before = await counters();
  let r: Result | null = null, err: string | null = null;
  try { r = await c.run(); } catch (e) { err = String(e); }
  deadline = Infinity;
  record(c, r, err, before, await counters());
  if (err && /case timeout/.test(err)) { log('case timed out: stopping, game state unknown'); break; }
}

// leave the game as found: fixes, build mode, speed, autopilot
try {
  if (/Build|Buy/.test((await ag('game_state')).result?.sub_state ?? '')) await ag('build_mode', { on: false });
  if (ready.fastreftable && typeof fix0.fastreftable === 'boolean') await ag('patches', { fastreftable: fix0.fastreftable });
  if (ready.cachedreflection && typeof fix0.cachedreflection === 'boolean') await ag('patches', { cachedreflection: fix0.cachedreflection });
  if (ready.footprint && fix0.footprintMode != null) await nx(`name=ts3native_footprintMode&value=${fix0.footprintMode}`);
  if (ready.compress && fix0.native) await nx(`name=ts3native_counters&offset=0x150&value=${fix0.native.compressMode}`);
  if (ready.flush && fix0.native) await nx(`name=ts3native_counters&offset=0x180&value=${fix0.native.flushMode}`);
  if (ap0) await ag('autopilot', { enabled: ap0.enabled, speed: ap0.speed, dialogs: ap0.dialogs });
  const sp: Record<string, string> = { Normal: 'normal', Double: 'fast', Triple: 'ultra' };
  await ag('set_speed', { speed: sp[speed0] ?? 'pause' });
} catch (e) { log(`restore: ${e}`); }
await alive.check();
process.exit(writeMd(`${started} UTC ${world}, start save ${st.result.save_name}, build ${build}`) ? 1 : 0);
