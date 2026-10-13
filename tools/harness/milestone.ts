// One-command milestone run (PLAN.md "Next, in order", Milestone; docs/05 results table): per world save
//   node tools/harness/milestone.ts [--worlds sv,bp,ip] [--runs 1] [--minutes 10] [--label milestone] [--dry-run]
//   node tools/harness/milestone.ts --report [--worlds ..] [--runs N] [--label X]   table only, from existing probe-out data
//   node tools/harness/milestone.ts --self-check [--probe-out <dir>]                 statistics + a table of bp-nobudget vs bp
// Steps: cycle.ts --restart once (rebuilds and installs the mods), probe server (ts3probe.py serve --launch, left running),
// then per world: cycle.ts --save Baseline-<W>.sims3 (no restart between worlds), focus-game, tests-be.ts --tag <w>-<label>
// --only B (frame times), profile-world.ts --tag <w>-<label>-<n> --minutes M (x --runs). tests-be and profile-world run the
// game-alive watchdog themselves (FAIL line + crash-ledger triage, exit 1); a failing step stops the batch here.
// At the end one table, milestone vs the no-mod baselines (profile tags sv, bp, ip; frame times from tests/<w>-B.json when
// present), also written to docs/data/milestone-<label>-<date>.md with profile-compare.mjs's detail below it.
import { execFileSync, spawn } from 'node:child_process';
import { existsSync, mkdirSync, openSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { focusGame } from './focus-game.ts';

const { values } = parseArgs({ options: {
  worlds: { type: 'string', default: 'sv,bp,ip' }, runs: { type: 'string', default: '1' }, minutes: { type: 'string', default: '10' },
  label: { type: 'string', default: 'milestone' }, 'dry-run': { type: 'boolean' }, report: { type: 'boolean' },
  'self-check': { type: 'boolean' }, 'probe-out': { type: 'string' } } });
const ROOT = resolve(import.meta.dirname, '..', '..');
const PROBE = values['probe-out'] ? resolve(values['probe-out']) : join(ROOT, 'probe-out');
const SAVES: Record<string, string> = { sv: 'Baseline-SV.sims3', bp: 'Baseline-BP.sims3', ip: 'Baseline-IP.sims3' };
const worlds = values.worlds!.split(',').map(w => w.trim().toLowerCase());
for (const w of worlds) if (!SAVES[w]) throw new Error(`unknown world ${w}; known: ${Object.keys(SAVES).join(',')}`);
const runs = Number(values.runs), minutes = Number(values.minutes), label = values.label!;
const stamp = () => new Date().toISOString().slice(11, 19);
const log = (m: string) => console.log(`${stamp()} ${m}`);
const HITCH_MS = 50; // profile-world.ts runs stutter-profile.js with hitchMs 50: shorter iterations are only counted

// ---- statistics (covered by --self-check) ----
type Sec = Record<string, number>;
type Hitch = { wallMs: number };
// p-th percentile iteration time by nearest rank over all iterations; only iterations >= HITCH_MS are known one by one,
// so a percentile that falls below them reads as '<50'
export function iterPct(totalIters: number, hitchMs: number[], p: number): number | string {
  if (!totalIters) return '-';
  const fromTop = totalIters - Math.ceil(p * totalIters) + 1;
  const desc = [...hitchMs].sort((a, b) => b - a);
  return fromTop <= desc.length ? desc[fromTop - 1] : `<${HITCH_MS}`;
}
export function profileStats(secs: Sec[], hitches: Hitch[], stuck: number | null) {
  if (!secs.length) return null;
  const n = secs.length, iters = secs.reduce((a, r) => a + (r.iters || 0), 0), ms = hitches.map(h => h.wallMs);
  return { seconds: n, itersPerSec: iters / n, hitchesPerSec: hitches.length / n, p99: iterPct(iters, ms, 0.99), p999: iterPct(iters, ms, 0.999),
    freezesPerMin: ms.filter(x => x >= 250).length / (n / 60), worst: Math.max(0, ...secs.map(r => r.maxIterMs || 0), ...ms), stuck };
}

// ---- reading probe-out ----
const lines = (f: string) => existsSync(f) ? readFileSync(f, 'utf8').split('\n').filter(Boolean) : [];
function readTag(tag: string) {
  const dir = join(PROBE, 'profile');
  const csv = existsSync(dir) ? readdirSync(dir).find(f => f.startsWith(`${tag}-`) && /^\d+-seconds\.csv$/.test(f.slice(tag.length + 1))) : undefined;
  if (!csv) return null;
  const [head, ...rows] = lines(join(dir, csv)), cols = head.split(',');
  const secs = rows.map(l => Object.fromEntries(l.split(',').map((v, i) => [cols[i], Number(v)])));
  const hitches = lines(join(dir, csv.replace('-seconds.csv', '-hitches.jsonl'))).map(l => JSON.parse(l));
  const st = join(dir, `${tag}-stuck.json`);
  return { secs, hitches, stuck: existsSync(st) ? JSON.parse(readFileSync(st, 'utf8')).length as number : null };
}
// several runs of one world count as one long run
function statsFor(tags: string[]) {
  const got = tags.map(readTag).filter(x => x !== null);
  if (!got.length) return null;
  const stuck = got.every(g => g.stuck === null) ? null : got.reduce((a, g) => a + (g.stuck ?? 0), 0);
  return profileStats(got.flatMap(g => g.secs), got.flatMap(g => g.hitches), stuck);
}
function frames(tag: string) {
  const f = join(PROBE, 'tests', `${tag}-B.json`);
  return existsSync(f) ? JSON.parse(readFileSync(f, 'utf8')).stats as { fps: number; p99Ms: number; p999Ms: number } | null : null;
}

// ---- table ----
type Col = { name: string; profile: string[]; frames: string };
const METRICS: [string, (p: any, f: any) => number | string | null | undefined, number][] = [ // label, value, decimals
  ['iterations/s', p => p?.itersPerSec, 1], ['hitches/s (>=50 ms)', p => p?.hitchesPerSec, 2], ['p99 iteration ms', p => p?.p99, 0],
  ['p99.9 iteration ms', p => p?.p999, 0], ['freezes >=250 ms/min', p => p?.freezesPerMin, 2], ['worst iteration ms', p => p?.worst, 0],
  ['FPS', (_, f) => f?.fps, 1], ['frame p99 ms', (_, f) => f?.p99Ms, 1], ['frame p99.9 ms', (_, f) => f?.p999Ms, 1], ['stuck Sims', p => p?.stuck, 0]];
const fmt = (v: unknown, d: number) => typeof v === 'number' && Number.isFinite(v) ? v.toFixed(d) : v == null ? '-' : String(v);
const change = (a: unknown, b: unknown) => typeof a === 'number' && typeof b === 'number' && b ? `${a >= b ? '+' : ''}${(100 * (a - b) / b).toFixed(0)}%` : '-';
// pairs = [milestone column, baseline column]; one markdown table, three columns per pair
export function table(pairs: [Col, Col][]): string {
  const data = pairs.map(pr => pr.map(c => ({ p: statsFor(c.profile), f: frames(c.frames) })));
  const head = ['', ...pairs.flatMap(([a, b]) => [a.name, b.name, 'change'])];
  const body = METRICS.map(([name, get, d]) => [name, ...data.flatMap(([a, b]) => {
    const va = get(a.p, a.f), vb = get(b.p, b.f);
    return [fmt(va, d), fmt(vb, d), change(va, vb)];
  })]);
  const secs = ['seconds profiled', ...data.flatMap(([a, b]) => [fmt(a.p?.seconds, 0), fmt(b.p?.seconds, 0), ''])];
  return [head, head.map(() => '---'), ...body, secs].map(r => `| ${r.join(' | ')} |`).join('\n');
}
const pairsFor = (ws: string[]): [Col, Col][] => ws.map(w => [
  { name: `${w} ${label}`, profile: Array.from({ length: runs }, (_, i) => `${w}-${label}-${i + 1}`), frames: `${w}-${label}` },
  { name: `${w} no-mod`, profile: [w], frames: w }]);

function report(ws: string[]) {
  const t = table(pairsFor(ws));
  console.log(`\n${t}\n`);
  let detail = '';
  try { // profile-compare.mjs reads <repo>/probe-out only
    detail = execFileSync(process.execPath, [join(ROOT, 'tools', 'probe', 'profile-compare.mjs'),
      ...ws.flatMap(w => [...Array.from({ length: runs }, (_, i) => `${w}-${label}-${i + 1}`), w])]).toString();
  } catch (e) { detail = `profile-compare.mjs failed: ${String(e).split('\n')[0]}`; }
  const date = new Date().toISOString().slice(0, 10), out = join(ROOT, 'docs', 'data', `milestone-${label}-${date}.md`);
  mkdirSync(join(ROOT, 'docs', 'data'), { recursive: true });
  writeFileSync(out, `# Milestone ${label}, ${date}\n\nWorlds ${ws.join(', ')}, ${runs} x ${minutes} min profile-world.ts per world plus tests-be.ts B, ` +
    `against the no-mod baselines (profile tags ${ws.join(', ')}). Made by \`tools/harness/milestone.ts\`. ` +
    `p99/p99.9 iteration ms come from seconds.csv (iterations) and hitches.jsonl (iterations >= ${HITCH_MS} ms); \`<${HITCH_MS}\` = below the hitch threshold.\n\n` +
    `${t}\n\n## Detail (profile-compare.mjs)\n\n\`\`\`\n${detail.trimEnd()}\n\`\`\`\n`);
  log(`table written to ${out}`);
}

if (values['self-check']) {
  const assert = (await import('node:assert')).strict;
  assert.equal(iterPct(1000, [60, 300, 80, 55, 70, 90, 51, 52, 53, 54, 400], 0.99), 51); // 11th largest of 11
  assert.equal(iterPct(1000, [300], 0.99), '<50');
  assert.equal(iterPct(1000, [300, 200], 0.999), 200);
  const s = profileStats([{ iters: 30, maxIterMs: 120 }, { iters: 30, maxIterMs: 40 }], [{ wallMs: 120 }, { wallMs: 260 }], 2)!;
  assert.equal(s.itersPerSec, 30); assert.equal(s.hitchesPerSec, 1); assert.equal(s.freezesPerMin, 30); assert.equal(s.worst, 260);
  assert.equal(change(110, 100), '+10%'); assert.equal(change(90, 100), '-10%'); assert.equal(change('<50', 60), '-');
  console.log('self-check ok');
  if (existsSync(join(PROBE, 'profile'))) console.log(table([[{ name: 'bp-nobudget (frames bp-cur)', profile: ['bp-nobudget'], frames: 'bp-cur' }, { name: 'bp', profile: ['bp'], frames: 'bp' }]]));
  process.exit(0);
}
if (values.report) { report(worlds); process.exit(0); }

// ---- the run ----
const node = (script: string, args: string[], timeoutS: number) => {
  const cmd = [join(ROOT, 'tools', 'harness', script), ...args];
  log(`node tools/harness/${script} ${args.join(' ')}`);
  if (values['dry-run']) return;
  try { execFileSync(process.execPath, cmd, { stdio: 'inherit', timeout: timeoutS * 1000 }); } catch (e: any) {
    log(`FAIL: ${script} ${e.signal ? `timed out after ${timeoutS} s` : `exited ${e.status}`}; batch stopped (see the FAIL / crash-ledger triage lines above)`);
    process.exit(1);
  }
};
const probeUp = async () => { try { await fetch('http://127.0.0.1:47891/agentstatus', { signal: AbortSignal.timeout(3000) }); return true; } catch { return false; } };

if (values['dry-run']) console.log(`dry run: worlds ${worlds.join(',')}, ${runs} run(s) x ${minutes} min, label ${label}; nothing touches the game. Would:`);
node('cycle.ts', ['--restart'], 180);
if (values['dry-run']) log('python tools/probe/ts3probe.py serve --launch  (detached, log probe-out/serve-milestone.log; skipped if the probe already answers)');
else if (await probeUp()) log('probe server already answering; not starting another');
else {
  mkdirSync(PROBE, { recursive: true });
  const fd = openSync(join(PROBE, 'serve-milestone.log'), 'w');
  // detached: the probe server must outlive this script (cycle.ts header)
  spawn('python', [join(ROOT, 'tools', 'probe', 'ts3probe.py'), 'serve', '--launch'], { cwd: ROOT, detached: true, stdio: ['ignore', fd, fd] }).unref();
  log('probe server started (python tools/probe/ts3probe.py serve --launch, log probe-out/serve-milestone.log)');
}
for (const w of worlds) {
  node('cycle.ts', ['--save', SAVES[w]], 600);
  if (values['dry-run']) log('focus game window'); else log(`game window ${focusGame() ? 'focused' : 'NOT focused'}`);
  node('tests-be.ts', ['--tag', `${w}-${label}`, '--only', 'B'], 300);
  for (let i = 1; i <= runs; i++) node('profile-world.ts', ['--tag', `${w}-${label}-${i}`, '--minutes', String(minutes)], minutes * 60 + 300);
}
if (values['dry-run']) log(`print the table and write docs/data/milestone-${label}-<date>.md`);
else report(worlds);
