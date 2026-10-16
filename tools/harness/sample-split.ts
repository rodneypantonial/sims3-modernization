// Split existing sim-thread samples by native function (docs/29 G9) and per-stage timers by world (docs/28 G12). Reads only.
//   node tools/harness/sample-split.ts [--top 15] [--frames 12] [--all] [files...]   Mono runtime helpers from *simstack*.jsonl
//   node tools/harness/sample-split.ts --stages [files...]                         stage ms/s from *-seconds.csv (stutter-profile.js)
//   node tools/harness/sample-split.ts --main [--thread main|<id>|<name>] [files...]  same split of *mainstack*.jsonl (every family)
//   node tools/harness/sample-split.ts --self-check
// Defaults: every non-empty probe-out/profile/*simstack*.jsonl (or *-seconds.csv with stage columns); function ranges from
// game-copy/analysis/functions.csv (+ ghidra-user-symbols.tsv names). World = file tag prefix bp/sv/ip (else "other").
// Self = leaf-most TS3W frame (frames above it in ntdll/MSVCR80 are counted as "via OS/CRT"). Inclusive = function on any of the
// first --frames TS3W frames (stacks are partly scanned, so inclusive is approximate, docs/20). Caller = next TS3W frame in another
// function. Mono runtime = docs/20 ranges 0xE20000-0xEF0000 and 0xD82000-0xDC0000 minus interpreter, Boehm GC block, finalizers.
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(import.meta.dirname, '..', '..');
const PROFILE = join(ROOT, 'probe-out', 'profile');
const INTERP = 0xe53eb0;
const FINALIZERS = 0xe697f0;
// shortcut: Boehm GC block taken as 0xE40000-0xE51400 (GC_stopped_mark 0xE49980 .. GC_start_world 0xE51290, plus its CRT neighbours; allocation included); per-function GC list if this matters
const isGc = (a: number) => a >= 0xe40000 && a < 0xe51400;
const inMonoRange = (a: number) => (a >= 0xe20000 && a < 0xef0000) || (a >= 0xd82000 && a < 0xdc0000);
export const family = (entry: number) => entry === INTERP ? 'interp' : entry === FINALIZERS ? 'finalizers' : isGc(entry) ? 'gc'
  : inMonoRange(entry) ? 'runtime' : 'other';

type Fn = { entry: number; end: number; name: string };
export function loadFunctions(csv: string, symbols = ''): Fn[] {
  const names = new Map<number, string>();
  for (const l of symbols.split('\n')) { const c = l.trim().split('\t'); if (c[0] === 'SYM' && c[2] === 'Function') names.set(parseInt(c[1], 16), c[3]); }
  return csv.split('\n').slice(1).filter(Boolean).map(l => {
    const [e, end, name] = l.trim().split(',');
    const entry = parseInt(e, 16);
    return { entry, end: parseInt(end, 16), name: names.get(entry) ?? name };
  }).sort((a, b) => a.entry - b.entry);
}
export function lookup(fns: Fn[], a: number): Fn | undefined {
  let lo = 0, hi = fns.length - 1;
  while (lo <= hi) { const m = (lo + hi) >> 1; if (fns[m].entry <= a) lo = m + 1; else hi = m - 1; }
  const f = fns[hi];
  return f && a <= f.end ? f : undefined;
}
export const worldOf = (file: string) => (/^(?:simstack-)?(bp|sv|ip)\b/.exec(basename(file).replace(/^simstack-/, '')) ?? [])[1] ?? 'other';

type Row = { self: number; selfOs: number; incl: number; callers: Map<number, number>; inclCallers: Map<number, number> };
export type Split = { samples: number; rows: Map<number, Row> };
const bump = (m: Map<number, number>, k: number) => m.set(k, (m.get(k) ?? 0) + 1);

export function split(stacks: string[][], fns: Fn[], frames = 12): Split {
  const rows = new Map<number, Row>();
  const row = (e: number) => rows.get(e) ?? rows.set(e, { self: 0, selfOs: 0, incl: 0, callers: new Map(), inclCallers: new Map() }).get(e)!;
  for (const stack of stacks) {
    const ts3: Fn[] = [];
    let osLeaf = false;
    for (const fr of stack) {
      if (!/ TS3W\.exe[!+]/.test(fr)) { if (!ts3.length) osLeaf = true; continue; }
      const f = lookup(fns, parseInt(fr, 16));
      if (f) ts3.push(f);
      if (ts3.length >= frames) break;
    }
    if (!ts3.length) continue;
    const callerAfter = (i: number) => ts3.slice(i + 1).find(f => f.entry !== ts3[i].entry)?.entry ?? -1;
    const leaf = row(ts3[0].entry);
    leaf.self++; if (osLeaf) leaf.selfOs++;
    bump(leaf.callers, callerAfter(0));
    const seen = new Set<number>();
    ts3.forEach((f, i) => { if (seen.has(f.entry)) return; seen.add(f.entry); const r = row(f.entry); r.incl++; bump(r.inclCallers, callerAfter(i)); });
  }
  return { samples: stacks.length, rows };
}

const hex = (a: number) => `0x${a.toString(16).toUpperCase()}`;
const pct = (n: number, d: number) => d ? `${(100 * n / d).toFixed(2)}%` : '-';
const top = (m: Map<number, number>) => [...m].sort((a, b) => b[1] - a[1])[0] ?? [-1, 0];

// thread: keep only lines whose thread id, role ("main") or thread name matches (mainstack files tag every line; simstack lines have none)
export function parseStacks(text: string, thread?: string): string[][] {
  return text.split('\n').filter(Boolean).flatMap(l => {
    try {
      const o = JSON.parse(l);
      return !thread || [String(o.thread), o.role, o.name].includes(thread) ? [o.stack ?? []] : [];
    } catch { return []; }
  });
}
const readStacks = (file: string, thread?: string) => parseStacks(readFileSync(file, 'utf8'), thread);
const defaults = (re: RegExp, keep: (f: string) => boolean) => existsSync(PROFILE)
  ? readdirSync(PROFILE).filter(f => re.test(f)).map(f => join(PROFILE, f)).filter(f => statSync(f).size > 0 && keep(f)) : [];

function mono(files: string[], topN: number, frames: number, all: boolean, thread?: string) {
  const an = join(ROOT, 'game-copy', 'analysis');
  const sym = join(an, 'ghidra-user-symbols.tsv');
  const fns = loadFunctions(readFileSync(join(an, 'functions.csv'), 'utf8'), existsSync(sym) ? readFileSync(sym, 'utf8') : '');
  const name = (e: number) => e < 0 ? '(none)' : `${hex(e)} ${lookup(fns, e)?.name ?? ''}`.trim();
  const byWorld = new Map<string, string[][]>();
  for (const f of files) { const w = worldOf(f); byWorld.set(w, [...(byWorld.get(w) ?? []), ...readStacks(f, thread)]); }
  const worlds = ['bp', 'sv', 'ip'].filter(w => byWorld.has(w));
  const res = new Map([['all', split([...byWorld.values()].flat(), fns, frames)], ...worlds.map(w => [w, split(byWorld.get(w)!, fns, frames)] as const)]);
  console.log(`inputs: ${files.length} files; samples ${[...res].map(([w, s]) => `${w} ${s.samples}`).join(', ')}`);
  for (const [w, s] of res) {
    const fam = new Map<string, number>();
    for (const [e, r] of s.rows) fam.set(family(e), (fam.get(family(e)) ?? 0) + r.self);
    console.log(`${w} self by family: ${[...fam].sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k} ${pct(v, s.samples)}`).join(', ')}`);
  }
  const want = (e: number) => all || family(e) === 'runtime';
  for (const kind of ['self', 'incl'] as const) {
    const p = res.get('all')!;
    const list = [...p.rows].filter(([e]) => want(e)).sort((a, b) => b[1][kind] - a[1][kind]).slice(0, topN);
    console.log(`\n| # | Function | ${kind === 'self' ? 'Self' : 'Inclusive'} all | ${worlds.map(w => w.toUpperCase()).join(' | ')} |${kind === 'self' ? ' via OS/CRT |' : ''} Top caller (share of its samples) |`);
    console.log(`|---|---|---|${worlds.map(() => '---|').join('')}${kind === 'self' ? '---|' : ''}---|`);
    list.forEach(([e, r], i) => {
      const [c, n] = top(kind === 'self' ? r.callers : r.inclCallers);
      const per = worlds.map(w => pct(res.get(w)!.rows.get(e)?.[kind] ?? 0, res.get(w)!.samples)).join(' | ');
      console.log(`| ${i + 1} | ${name(e)} | ${pct(r[kind], p.samples)} | ${per} |${kind === 'self' ? ` ${pct(r.selfOs, r.self)} |` : ''} ${name(c)} (${pct(n, r[kind])}) |`);
    });
  }
}

// stutter-profile.js per-second rows: stage columns are ms inside that row; t is a Frida ms clock, so ms/s = sum / sum(dt) * 1000
// shortcut: files from before a column existed count 0 for it (animSacs..publish: 2026-10-15); pass only newer files for those
const STAGES = ['script', 'taskObjects', 'oneShot', 'threaded', 'route', 'routeSearch', 'gcMark', 'gcForcedMs', 'fin', 'animSacs', 'animPose', 'locomotion', 'publish'];
export function stageTotals(csv: string) {
  const [head, ...lines] = csv.trim().split(/\r?\n/);
  const cols = head.split(',');
  const ix = (c: string) => cols.indexOf(c);
  const sum: Record<string, number> = Object.fromEntries(STAGES.map(s => [s, 0]));
  let ms = 0, rows = 0, iters = 0, prev = NaN;
  for (const l of lines) {
    const v = l.split(',').map(Number);
    const t = v[ix('t')], dt = t - prev;
    prev = t;
    if (!(dt > 0 && dt < 5000) || !v[ix('iters')]) continue; // first row, gaps, paused seconds
    ms += dt; rows++; iters += v[ix('iters')];
    for (const s of STAGES) sum[s] += v[ix(s)] || 0;
  }
  return { rows, ms, iters, sum };
}
function stages(files: string[]) {
  const by = new Map<string, { rows: number; ms: number; iters: number; sum: Record<string, number>; files: string[] }>();
  for (const f of files) {
    const t = stageTotals(readFileSync(f, 'utf8'));
    if (!t.rows) continue;
    const w = worldOf(f), acc = by.get(w) ?? { rows: 0, ms: 0, iters: 0, sum: Object.fromEntries(STAGES.map(s => [s, 0])), files: [] };
    acc.rows += t.rows; acc.ms += t.ms; acc.iters += t.iters; acc.files.push(basename(f).replace(/-\d+-seconds\.csv$/, ''));
    for (const s of STAGES) acc.sum[s] += t.sum[s];
    by.set(w, acc);
  }
  console.log(`| World | Seconds | it/s | ${STAGES.join(' | ')} |\n|---|---|---|${STAGES.map(() => '---|').join('')}`);
  for (const [w, a] of by) console.log(`| ${w.toUpperCase()} | ${a.rows} | ${(a.iters * 1000 / a.ms).toFixed(1)} | ${STAGES.map(s => (a.sum[s] * 1000 / a.ms).toFixed(1)).join(' | ')} |`);
  for (const [w, a] of by) console.log(`${w}: ${a.files.join(', ')}`);
}

function selfCheck() {
  const fns = loadFunctions('entry,end,name\n0xea8720,0xea87ff,FUN_00ea8720\n0xe53eb0,0xe55000,FUN_00e53eb0\n0x700000,0x7000ff,FUN_00700000\n',
    'SYM\t00700000\tFunction\tIC_Test\n');
  const assert = (ok: boolean, msg: string) => { if (!ok) throw new Error(`self-check failed: ${msg}`); };
  assert(lookup(fns, 0xea8730)?.entry === 0xea8720 && !lookup(fns, 0xea8900) && lookup(fns, 0x700010)?.name === 'IC_Test', 'lookup');
  assert(family(0xea8720) === 'runtime' && family(INTERP) === 'interp' && family(0xe4a050) === 'gc' && family(0x700000) === 'other', 'family');
  const s = split([
    ['0x777a1719 ntdll.dll+0x1', '0xea8730 TS3W.exe!FUN_00ea8720+0x10', '0xe53f00 TS3W.exe!FUN_00e53eb0+0x50', '0x700010 TS3W.exe+0x300010'],
    ['0xea8740 TS3W.exe!FUN_00ea8720+0x20', '0xea8741 TS3W.exe+0xaa8741', '0x700010 TS3W.exe+0x300010', '0xe53f00 TS3W.exe+0xa53f00'],
    ['0xe53f00 TS3W.exe+0xa53f00', '0xe53f10 TS3W.exe+0xa53f10'],
    ['0x777a1719 ntdll.dll+0x1'],
  ], fns);
  const r = s.rows.get(0xea8720)!;
  assert(s.samples === 4 && r.self === 2 && r.selfOs === 1 && r.incl === 2, `type-check counts ${JSON.stringify({ ...r, callers: 0, inclCallers: 0 })}`);
  assert(r.callers.get(INTERP) === 1 && r.callers.get(0x700000) === 1, 'callers skip same-function frames');
  assert(s.rows.get(INTERP)!.self === 1 && s.rows.get(INTERP)!.incl === 3 && s.rows.get(0x700000)!.incl === 2, 'interp self/incl, dedupe');
  assert(worldOf('x/simstack-bp.jsonl') === 'bp' && worldOf('ip-all-simstack.jsonl') === 'ip' && worldOf('t14-rc2-simstack.jsonl') === 'other', 'worldOf');
  const ms = ['{"t":1,"thread":7,"role":"main","name":null,"stack":["0x700010 TS3W.exe+0x300010"]}', '{"t":1,"thread":9,"role":null,"name":"Audio","stack":["0x1 x"]}',
    'bad line', '{"t":2,"stack":["0x2 y"]}'].join('\n');
  assert(parseStacks(ms).length === 3 && parseStacks(ms, 'main')[0][0].startsWith('0x700010') && parseStacks(ms, '9').length === 1
    && parseStacks(ms, 'Audio')[0][0] === '0x1 x' && parseStacks(ms, '8').length === 0, 'parseStacks thread filter');
  const t = stageTotals('iters,script,taskObjects,threaded,t\r\n20,1,2,3,1000\r\n20,2,4,6,2000\n0,9,9,9,3000\n20,2,4,6,4000\n');
  assert(t.rows === 2 && t.ms === 2000 && t.sum.taskObjects === 8 && t.sum.oneShot === 0, `stageTotals ${JSON.stringify(t)}`);
  console.log('self-check ok');
}

const main = resolve(process.argv[1] ?? "") === fileURLToPath(import.meta.url); // run as CLI, not when imported
const args = main ? process.argv.slice(2) : [];
const opt = (k: string, d: number) => { const i = args.indexOf(k); return i < 0 ? d : Number(args.splice(i, 2)[1]); };
const flag = (k: string) => { const i = args.indexOf(k); if (i >= 0) args.splice(i, 1); return i >= 0; };
if (!main) { /* imported */ }
else if (flag("--self-check")) selfCheck();
else if (flag('--stages')) stages(args.length ? args : defaults(/-seconds\.csv$/, f => readFileSync(f, 'utf8').split('\n', 1)[0].includes('taskObjects')));
else {
  const ti = args.indexOf('--thread'), thread = ti < 0 ? undefined : args.splice(ti, 2)[1];
  const topN = opt('--top', 15), frames = opt('--frames', 12), mainFiles = flag('--main'), all = flag('--all') || mainFiles;
  mono(args.length ? args : defaults(mainFiles ? /mainstack.*\.jsonl$/ : /simstack.*\.jsonl$/, () => true), topN, frames, all, thread);
}
