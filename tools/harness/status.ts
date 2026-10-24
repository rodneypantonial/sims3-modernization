// What runs now and what is left, without reading logs by hand. Read-only.
//   node tools/harness/status.ts                                  the running queue (node ... queue.ts <file> in the process list,
//                                                                  its root = main checkout or worktree), else the newest queue run
//   node tools/harness/status.ts --queue <name> [--date YYYY-MM-DD] that queue's newest (or given) run
//   node tools/harness/status.ts --chain probe-out/queue/<chain>.log  every queue the chain started ('== <stage>' lines and the
//                                                                  queue lines after them) plus the queues its .sh/.ps1 still runs;
//                                                                  a stage line may name its queue anywhere ('== run v015-validate.json')
//                                                                  and job logs in a worktree's probe-out/queue count for that worktree
//   node tools/harness/status.ts --all                            every queue run today (UTC date) in the main checkout and
//                                                                  .claude/worktrees/*
//   [--root <dir>]...  also search <dir>/probe-out/queue (a worktree's runs); default root = TS3_REPO or this checkout
//   node tools/harness/status.ts --self-check
// Per queue: finished jobs with PASS / FAIL / VOID (run-verdict.ts classify; queue rows only, no profile files), the running job
// with its start and expected end (profile minutes, per repro window, + the overhead measured on this run's finished profile jobs,
// default 1.7 min; other jobs: timeoutMin, default 60), its machine-load verdict (machine-load.ts summarize on its -machine.jsonl),
// jobs left, and 'done ~HH:MM PHT' (PHT = UTC+8; harness logs are UTC). A queue whose job log and machine file have not changed for
// 5 min counts as not running (no ETA). Job list from tools/harness/queues/<name>.json; a queue file kept elsewhere shows only
// what ran.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, readlinkSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, isAbsolute, join, resolve } from 'node:path';
import { summarize } from './machine-load.ts';
import { classify, jsonlRows } from './run-verdict.ts';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..');
const DEFS = join(import.meta.dirname, 'queues');
const OVERHEAD_MIN = 1.7, STALE_MS = 5 * 60000;
const RUN_LOG = /^(.+)-(\d{4}-\d\d-\d\d)-(.+)\.log$/;

export const pht = (ms: number) => new Date(ms + 8 * 3600000).toISOString().slice(11, 16);
const mtime = (f: string) => existsSync(f) ? statSync(f).mtimeMs : 0;
type Def = { id: string; timeoutMin?: number; run?: { profile?: { minutes?: number; repros?: (string | { id: string; minutes?: number })[] } } };
const defJobs = (name: string, defs: string): Def[] | null => { const f = join(defs, `${name}.json`); if (!existsSync(f)) return null; const q = JSON.parse(readFileSync(f, 'utf8')); return q.jobs ?? q; };
const profMin = (j: Def) => { const p = j.run?.profile; if (!p?.minutes) return null; return p.repros?.length ? p.repros.reduce((s, r) => s + ((typeof r === 'object' && r.minutes) || p.minutes!), 0) : p.minutes; };
export const estMin = (j: Def, overhead: number) => { const p = profMin(j); return p !== null ? p + overhead : j.timeoutMin ?? 60; };

export type JobState = { id: string; state: 'PASS' | 'FAIL' | 'VOID' | 'running' | 'stopped' | 'left'; start?: number; end?: number; min?: number; why?: string; machine?: string };
export type QStatus = { name: string; date: string; dir: string; jobs: JobState[]; alive: boolean; last: number; eta: number | null; overhead: number };

// one run of a queue: <dir>/<name>-<date>.jsonl rows + <name>-<date>-<job>.log files
export function queueStatus(name: string, date: string, dir: string, now = Date.now(), defs = DEFS): QStatus {
  const pre = `${name}-${date}-`, rows = jsonlRows(join(dir, `${name}-${date}.jsonl`));
  const logged = readdirSync(dir).filter(f => f.startsWith(pre) && f.endsWith('.log') && !/-(probe|ts3native)\.log$/.test(f)).map(f => f.slice(pre.length, -4));
  const def = defJobs(name, defs), ids = def ? def.map(j => j.id) : [...new Set([...rows.map(r => r.job), ...logged])];
  const byId = new Map(def?.map(j => [j.id, j] as const) ?? []);
  // overhead measured on this run: queue minutes minus profile minutes of finished plain profile jobs
  const ov = rows.flatMap(r => { const j = byId.get(r.job), p = j && !j.run?.profile?.repros ? profMin(j) : null; return p !== null && typeof r.minutes === 'number' && r.minutes >= p ? [r.minutes - p] : []; });
  const overhead = ov.length ? ov.reduce((a, b) => a + b, 0) / ov.length : OVERHEAD_MIN;
  const machineOf = (id: string) => readdirSync(dir).filter(f => f.startsWith(`${pre}${id}-`) && /-\d{4}-machine\.jsonl$/.test(f) && f.slice(pre.length + id.length + 1).match(/^\d{4}-machine/)).sort().at(-1);
  let last = 0;
  const jobs: JobState[] = ids.map(id => {
    const row = rows.filter(r => r.job === id).at(-1), log = join(dir, `${pre}${id}.log`), mf = machineOf(id);
    last = Math.max(last, mtime(log), mf ? mtime(join(dir, mf)) : 0);
    if (row) { const v = classify(row); return { id, state: v.v, start: Date.parse(row.time), end: row.end ? Date.parse(row.end) : undefined, min: row.minutes, why: v.why }; }
    if (!logged.includes(id)) return { id, state: 'left' };
    const iso = /\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(\.\d+)?Z/.exec(readFileSync(log, 'utf8'))?.[0];
    const start = iso ? Date.parse(iso) : statSync(log).birthtimeMs, j = byId.get(id);
    const s = mf ? summarize(join(dir, mf)) : null;
    return { id, state: 'running', start, end: j ? start + estMin(j, overhead) * 60000 : undefined, machine: s ? `${s.ok ? 'ok' : 'NOT comparable'}; ${s.short}` : 'no machine samples yet' };
  });
  const alive = now - last < STALE_MS && jobs.some(j => j.state === 'running');
  for (const j of jobs) if (j.state === 'running' && !alive) j.state = 'stopped';
  let eta: number | null = null;
  if (alive) {
    eta = Math.max(now, ...jobs.filter(j => j.state === 'running').map(j => j.end ?? now));
    for (const j of jobs) if (j.state === 'left') { const d = byId.get(j.id); j.start = eta; eta += (d ? estMin(d, overhead) : 60) * 60000; j.end = eta; }
  }
  return { name, date, dir, jobs, alive, last, eta, overhead };
}

const cell = (s = '') => s.replace(/\|/g, '\\|').replace(/\n/g, ' ').slice(0, 160);
export function render(q: QStatus): string {
  const n = (s: string) => q.jobs.filter(j => j.state === s).length;
  const out = [`### ${q.name} ${q.date}: ${n('PASS')} PASS, ${n('FAIL')} FAIL, ${n('VOID')} VOID, ${n('running')} running, ${n('left')} left${n('stopped') ? `, ${n('stopped')} stopped` : ''} (overhead ${q.overhead.toFixed(1)} min/job)`, '',
    '| Job | State | Start PHT | End PHT | min | Reason / machine |', '|---|---|---|---|---|---|'];
  for (const j of q.jobs) {
    const end = j.end === undefined ? '' : j.state === 'PASS' || j.state === 'FAIL' || j.state === 'VOID' ? pht(j.end) : `~${pht(j.end)}`;
    out.push(`| ${j.id} | ${j.state === 'PASS' || j.state === 'left' ? j.state : `**${j.state}**`} | ${j.start ? pht(j.start) : ''} | ${end} | ${j.min ?? ''} | ${cell(j.machine ?? j.why)} |`);
  }
  out.push('', q.alive ? `done ~${pht(q.eta!)} PHT` : `not running (last activity ${q.last ? `${pht(q.last)} PHT` : 'none'})`);
  return out.join('\n');
}

// queue + date of a per-job log path, when <name> is a known queue (job ids hold dashes too). A path under <root>/probe-out/queue
// also gives that run's dir and its root's queue files (a worktree's own queue), else the dir is looked up by the caller.
export type Run = { name: string; date: string; dir?: string; defs?: string };
export function runOfLog(file: string, defs = DEFS): Run | null {
  const m = RUN_LOG.exec(basename(file)), f = file.replace(/\\/g, '/'), r = /^(.*)\/probe-out\/queue\/[^/]+$/.exec(f)?.[1];
  if (!m) return null;
  const own = r ? join(r, 'tools', 'harness', 'queues') : null;
  if (own && existsSync(join(own, `${m[1]}.json`))) return { name: m[1], date: m[2], dir: dirname(f), defs: own };
  return existsSync(join(defs, `${m[1]}.json`)) ? { name: m[1], date: m[2], ...(r && existsSync(dirname(f)) ? { dir: dirname(f) } : {}) } : null;
}
const qDirs = (roots: string[]) => roots.map(r => join(r, 'probe-out', 'queue')).filter(existsSync);
function newestRun(dirs: string[], name?: string, date?: string): { name: string; date: string; dir: string; t: number } | null {
  let best: { name: string; date: string; dir: string; t: number } | null = null;
  for (const dir of dirs) for (const f of readdirSync(dir)) {
    const r = /^(.+)-(\d{4}-\d\d-\d\d)\.jsonl$/.exec(f) ? { name: f.slice(0, -17), date: f.slice(-16, -6) } : runOfLog(f);
    if (!r || (name && r.name !== name) || (date && r.date !== date)) continue;
    const t = mtime(join(dir, f)); if (!best || t > best.t) best = { ...r, dir, t };
  }
  return best;
}

// chain log: '== <stage>' lines, queue lines 'queue: == <job> (n/N)...; log <path>' (which queue run) and 'queue: results:' (done);
// the chain's own .sh/.ps1 beside the log names the queue files still to run
export function chainRuns(text: string, script: string | null, defs = DEFS) {
  const stages: { label: string; runs: Run[] }[] = [];
  let open = false; // inside a queue run its jobs print '== ' lines of their own (save matrix cases): not chain stages
  for (const l of text.split(/\r?\n/)) {
    const s = /^\d\d:\d\d:\d\d == (.+)$/.exec(l), q = /queue: == .*; log (.+\.log)\s*$/.exec(l);
    if (/queue: results: |another queue is running/.test(l)) open = false;
    if (q) open = true;
    if (s && !open) stages.push({ label: s[1], runs: [] });
    else if (q && stages.length) { const r = runOfLog(q[1].replace(/\\/g, '/'), defs); if (r && !stages.at(-1)!.runs.some(x => x.name === r.name)) stages.at(-1)!.runs.push(r); }
  }
  // a stage that names a queue file that has not logged a job yet (and was not run by an earlier stage): that queue, newest run.
  // The name may be the whole label or anywhere in it ('== v0.1.5 package validation: queues/v015-validate.json')
  const named = (label: string) => [label, ...[...label.matchAll(/([\w.-]+)\.json\b/g)].map(m => m[1]), ...label.split(/[\s(),:;]+/)]
    .find(n => n && existsSync(join(defs, `${n}.json`)));
  const seen = new Set<string>();
  for (const st of stages) { const n = st.runs.length ? null : named(st.label); if (n && !seen.has(n)) st.runs.push({ name: n, date: '' }); st.runs.forEach(r => seen.add(r.name)); }
  const pending = script ? [...script.matchAll(/queues[\\/]([\w.-]+)\.json/g)].map(m => m[1]).filter(n => !seen.has(n) && (seen.add(n), true)) : [];
  return { stages, pending };
}

// running queues from the process list: node ... harness/queue.ts <file>.json. Root = the checkout the queue file (or queue.ts)
// lives in when a path is absolute, else the process cwd when known (Linux /proc), else the candidate root that has the file and
// the newest files of that queue under probe-out/queue (how: 'path' | 'cwd' | 'inferred')
export type Proc = { pid: number; cmd: string; cwd?: string };
export type LiveQueue = { pid: number; name: string; file: string; root: string | null; defs: string; how: string };
const argv = (cmd: string) => [...cmd.matchAll(/"([^"]*)"|(\S+)/g)].map(m => m[1] ?? m[2]);
const rootOfPath = (p: string) => /^(.*)[\\/]tools[\\/]harness[\\/](?:queues[\\/][^\\/]+|queue\.ts)$/.exec(p)?.[1] ?? null;
export function liveQueues(procs: Proc[], roots: string[]): LiveQueue[] {
  const out: LiveQueue[] = [];
  for (const p of procs) {
    const a = argv(p.cmd), i = a.findIndex(x => /harness[\\/]queue\.ts$/.test(x));
    if (i < 0 || a.includes('--dry-run') || a.includes('--self-check')) continue;
    const file = a.slice(i + 1).find(x => /\.json$/i.test(x)); if (!file) continue;
    const name = basename(file).replace(/\.json$/i, '');
    let root: string | null = null, how = 'path';
    if (isAbsolute(file)) root = rootOfPath(file) ?? (isAbsolute(a[i]) ? rootOfPath(a[i]) : null);
    else if (isAbsolute(a[i])) root = rootOfPath(a[i]);
    else if (p.cwd) { root = p.cwd; how = 'cwd'; }
    else {
      how = 'inferred';
      const t = (r: string) => existsSync(join(r, file)) ? newestRun(qDirs([r]), name)?.t ?? 0 : -1;
      root = roots.map(r => [r, t(r)] as const).filter(([, v]) => v >= 0).sort((x, y) => y[1] - x[1])[0]?.[0] ?? null;
    }
    const abs = isAbsolute(file) ? file : root ? join(root, file) : file;
    out.push({ pid: p.pid, name, file: abs, root, defs: dirname(abs), how });
  }
  return out;
}
function processList(): Proc[] {
  try {
    if (process.platform === 'win32') {
      const j = execFileSync('powershell', ['-NoProfile', '-NonInteractive', '-Command',
        `Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -match 'harness[\\/]queue\.ts' } | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress`],
        { timeout: 20000 }).toString().trim();
      if (!j) return [];
      return [JSON.parse(j)].flat().map((r: { ProcessId: number; CommandLine: string }) => ({ pid: r.ProcessId, cmd: r.CommandLine ?? '' }));
    }
    return execFileSync('ps', ['-eo', 'pid=,args='], { timeout: 20000 }).toString().split('\n').flatMap(l => {
      const m = /^\s*(\d+)\s+(.*)$/.exec(l); if (!m || !/harness[\\/]queue\.ts/.test(m[2])) return [];
      let cwd: string | undefined; try { cwd = readlinkSync(`/proc/${m[1]}/cwd`); } catch { /* not Linux: no cwd */ }
      return [{ pid: Number(m[1]), cmd: m[2], cwd }];
    });
  } catch { return []; }
}

// the main checkout and every .claude/worktrees/* that has a probe-out/queue
export function allRoots(root = ROOT): string[] {
  const main = /^(.*?)[\\/]\.claude[\\/]worktrees[\\/][^\\/]+[\\/]?$/.exec(root)?.[1] ?? root, wt = join(main, '.claude', 'worktrees');
  const more = existsSync(wt) ? readdirSync(wt).map(d => join(wt, d)).filter(d => existsSync(join(d, 'probe-out', 'queue'))) : [];
  return [main, ...more];
}
// every queue run of one date (rows file or job logs) under the roots: name, dir, the root's own queue files when it has them
export function runsOn(roots: string[], date: string): Required<Run>[] {
  const out: Required<Run>[] = [];
  for (const r of roots) {
    const dir = join(r, 'probe-out', 'queue'), own = join(r, 'tools', 'harness', 'queues'); if (!existsSync(dir)) continue;
    const names = new Set<string>();
    for (const f of readdirSync(dir)) {
      const n = f.endsWith(`-${date}.jsonl`) ? f.slice(0, -(date.length + 7)) : runOfLog(join(dir, f), existsSync(own) ? own : DEFS)?.date === date ? RUN_LOG.exec(f)![1] : null;
      if (n) names.add(n);
    }
    for (const name of names) out.push({ name, date, dir, defs: existsSync(join(own, `${name}.json`)) ? own : DEFS });
  }
  return out;
}

function selfCheck() {
  const tmp = mkdtempSync(join(tmpdir(), 'ts3-status-')), dir = join(tmp, 'q'), defs = join(tmp, 'defs');
  try {
    mkdirSync(dir); mkdirSync(defs);
    writeFileSync(join(defs, 'demo-q.json'), JSON.stringify({ jobs: [
      { id: 'bp-a', run: { profile: { minutes: 10 } } }, { id: 'ip-b', run: { profile: { minutes: 10, repros: ['r1', { id: 'r2', minutes: 5 }] } } },
      { id: 'sv-c', run: { matrix: {} }, timeoutMin: 30 }, { id: 'sv-d', run: { profile: { minutes: 45 } } }] }));
    const t0 = Date.parse('2026-10-24T04:00:00Z');
    writeFileSync(join(dir, 'demo-q-2026-10-24.jsonl'), JSON.stringify({ time: '2026-10-24T04:00:00.000Z', job: 'bp-a', pass: true, exitCode: 0, verdict: ['PASS: profile 10 min'], end: '2026-10-24T04:12:00.000Z', minutes: 12 }) + '\n');
    writeFileSync(join(dir, 'demo-q-2026-10-24-ip-b.log'), '\n2026-10-24T04:12:30.000Z $ node cycle.ts\n');
    writeFileSync(join(dir, 'demo-q-2026-10-24-ip-b-1212-machine.jsonl'), JSON.stringify({ t: t0, other: 2, ts3w: 11, perf: 180, top: ['x 1'], og: 1, oo: 9 }) + '\n');
    const now = mtime(join(dir, 'demo-q-2026-10-24-ip-b.log')) + 1000, q = queueStatus('demo-q', '2026-10-24', dir, now, defs);
    assert.equal(q.overhead, 2);
    assert.deepEqual(q.jobs.map(j => j.state), ['PASS', 'running', 'left', 'left']);
    assert.equal(q.jobs[1].end, Date.parse('2026-10-24T04:12:30Z') + (10 + 5 + 2) * 60000); // r1 10 + r2 5 + overhead 2
    assert.match(q.jobs[1].machine!, /^ok; noise: low/);
    assert.equal(q.eta, Math.max(now, q.jobs[1].end!) + (30 + 47) * 60000);
    assert.match(render(q), /done ~\d\d:\d\d PHT$/);
    assert.equal(queueStatus('demo-q', '2026-10-24', dir, now + STALE_MS + 1, defs).jobs[1].state, 'stopped');
    assert.deepEqual(runOfLog('C:/x/probe-out/queue/demo-q-2026-10-24-ip-b.log', defs), { name: 'demo-q', date: '2026-10-24' });
    const c = chainRuns('16:45:10 == stage one\n04:12:30 queue: == ip-b (2/4): note; log C:\\x\\demo-q-2026-10-24-ip-b.log\n04:13:00 == overwrite: a job line\n04:40:00 queue: results: x.jsonl\n16:50:00 == demo-q\n', 'node queue.ts tools/harness/queues/demo-q.json\nnode queue.ts tools/harness/queues/other.json\n', defs);
    assert.deepEqual(c.stages.map(s => s.runs.map(r => r.name)), [['demo-q'], []]); // stage 2 names demo-q, already run by stage 1
    assert.deepEqual(c.pending, ['other']);
    assert.equal(pht(Date.parse('2026-10-24T05:52:53Z')), '13:52');
    // worktree: its own queue file and probe-out/queue; chain stage in prose, queue named only by the following job lines
    const wt = join(tmp, 'main', '.claude', 'worktrees', 'v015'), wq = join(wt, 'probe-out', 'queue'), wd = join(wt, 'tools', 'harness', 'queues');
    mkdirSync(wq, { recursive: true }); mkdirSync(wd, { recursive: true }); mkdirSync(join(tmp, 'main', 'probe-out', 'queue'), { recursive: true });
    writeFileSync(join(wd, 'wt-q.json'), JSON.stringify({ jobs: [{ id: 'bp-x', run: { profile: { minutes: 6 } } }, { id: 'ip-x', run: { profile: { minutes: 6 } } }] }));
    writeFileSync(join(wq, 'wt-q-2026-10-24-bp-x.log'), '2026-10-24T08:55:56.000Z start\n');
    const wlog = join(wq, 'wt-q-2026-10-24-bp-x.log').replace(/\//g, '\\');
    const wr = runOfLog(wlog, defs)!;
    assert.deepEqual([wr.name, wr.date, resolve(wr.dir!), wr.defs], ['wt-q', '2026-10-24', resolve(wq), wd]);
    const c2 = chainRuns(`16:55:55 == v0.1.5 package validation (worktree at 85cf1978)\n08:55:56 queue: == bp-x (1/2): note; log ${wlog}\n09:20:00 queue: results: wt-q-2026-10-24.jsonl\n17:30:00 == then run demo-q (rerun)\n17:31:00 == part c: queues/demo-q.json\n`, null, defs);
    assert.deepEqual(c2.stages.map(s => s.runs.map(r => `${r.name}@${r.dir ? 'wt' : ''}`)), [['wt-q@wt'], ['demo-q@'], []]); // prose names demo-q; part c: already run
    const c3 = chainRuns('17:31:00 == part c: queues/demo-q.json\n', null, defs);
    assert.deepEqual(c3.stages.map(s => s.runs.map(r => r.name)), [['demo-q']]); // a queue file named inside the label
    const wst = queueStatus('wt-q', '2026-10-24', c2.stages[0].runs[0].dir!, mtime(join(wq, 'wt-q-2026-10-24-bp-x.log')) + 1000, c2.stages[0].runs[0].defs);
    assert.deepEqual(wst.jobs.map(j => j.state), ['running', 'left']);
    // process list: absolute queue file, absolute queue.ts + relative file, relative both (inferred from files), cwd, dry runs skipped
    const roots = allRoots(wt);
    assert.deepEqual(roots, [join(tmp, 'main'), wt]);
    const lq = liveQueues([
      { pid: 1, cmd: `node tools/harness/queue.ts "${join(wd, 'wt-q.json')}"` },
      { pid: 2, cmd: `"C:\\Program Files\\nodejs\\node.exe" ${join(wt, 'tools', 'harness', 'queue.ts')} tools/harness/queues/wt-q.json --reuse` },
      { pid: 3, cmd: 'node tools/harness/queue.ts tools/harness/queues/wt-q.json' },
      { pid: 4, cmd: 'node tools/harness/queue.ts probe-out/x.json', cwd: wt },
      { pid: 5, cmd: 'node tools/harness/queue.ts tools/harness/queues/wt-q.json --dry-run' }, { pid: 6, cmd: 'node other.ts' }], roots);
    assert.deepEqual(lq.map(q => [q.pid, q.name, q.root, q.how]), [[1, 'wt-q', wt, 'path'], [2, 'wt-q', wt, 'path'], [3, 'wt-q', wt, 'inferred'], [4, 'x', wt, 'cwd']]);
    assert.equal(lq[2].defs, wd);
    // --all: runs of the date in every root (rows file or job logs), none from other dates
    writeFileSync(join(tmp, 'main', 'probe-out', 'queue', 'demo-q-2026-10-24.jsonl'), '');
    writeFileSync(join(tmp, 'main', 'probe-out', 'queue', 'demo-q-2026-10-23.jsonl'), '');
    assert.deepEqual(runsOn(roots, '2026-10-24').map(r => `${r.name}:${basename(dirname(dirname(r.dir)))}:${r.defs === wd ? 'own' : 'main'}`), ['demo-q:main:main', 'wt-q:v015:own']);
    console.log('status self-check ok');
  } finally { rmSync(tmp, { recursive: true, force: true }); }
}

if (import.meta.main) {
  const args = process.argv.slice(2), opt = (k: string) => { const i = args.indexOf(k); return i < 0 ? undefined : args.splice(i, 2)[1]; };
  if (args.includes('--self-check')) { selfCheck(); process.exit(0); }
  const roots = [ROOT]; for (let r; (r = opt('--root')); ) roots.push(resolve(r));
  if (args.includes('--all')) {
    const date = opt('--date') ?? new Date().toISOString().slice(0, 10), runs = runsOn([...new Set([...allRoots(), ...roots])], date);
    console.log(runs.length ? runs.map(r => `${r.dir}\n${render(queueStatus(r.name, r.date, r.dir, Date.now(), r.defs))}`).join('\n\n') : `no queue run on ${date}`);
    process.exit(0);
  }
  const dirs = qDirs(roots), chain = opt('--chain'), name = opt('--queue'), date = opt('--date');
  // a queue run's dir: the first root that has its files
  const dirOf = (n: string, d: string) => dirs.find(x => existsSync(join(x, `${n}-${d}.jsonl`)) || readdirSync(x).some(f => f.startsWith(`${n}-${d}-`))) ?? null;
  const blocks: string[] = []; let eta: number | null = null, live: LiveQueue[] = [];
  if (chain) {
    const text = readFileSync(chain, 'utf8'), sf = ['.sh', '.ps1'].map(e => chain.replace(/\.log$/, e)).find(existsSync) ?? null;
    const { stages, pending } = chainRuns(text, sf ? readFileSync(sf, 'utf8') : null);
    let anyAlive = false;
    for (const st of stages) {
      if (!st.runs.length) { blocks.push(`### stage "${st.label}": no queue run started`); continue; }
      for (const r of st.runs) {
        const run = r.date ? (() => { const d = r.dir ?? dirOf(r.name, r.date); return d ? { ...r, dir: d } : null; })() : newestRun(dirs, r.name);
        if (!run) { blocks.push(`### stage "${st.label}": ${r.name} has no files yet`); continue; }
        const q = queueStatus(run.name, run.date, run.dir, Date.now(), r.defs); blocks.push(`stage "${st.label}"\n${render(q)}`);
        if (q.alive) { anyAlive = true; eta = q.eta; }
      }
    }
    if (pending.length) {
      let t = eta ?? Date.now();
      const rows = pending.map(n => { const js = defJobs(n, DEFS) ?? []; const m = js.reduce((s, j) => s + estMin(j, OVERHEAD_MIN), 0); t += m * 60000; return `| ${n} | ${js.length} | ${m.toFixed(0)} | ~${pht(t)} |`; });
      blocks.push(`### still to run (${basename(sf!)})\n\n| Queue | jobs | est min | done PHT |\n|---|---|---|---|\n${rows.join('\n')}`);
      if (anyAlive || eta === null) eta = t;
    }
    blocks.push(anyAlive || pending.length ? `chain done ~${pht(eta!)} PHT${anyAlive ? '' : ' (no queue running now: from now)'}` : 'chain: no queue running, nothing left in its script');
  } else if (!name && !date && (live = liveQueues(processList(), [...new Set([...allRoots(), ...roots])])).length) {
    for (const l of live) {
      const run = l.root ? newestRun(qDirs([l.root]), l.name) : null;
      blocks.push(`queue ${l.name} (node PID ${l.pid}, root ${l.root ?? 'unknown'}${l.how === 'path' ? '' : `, ${l.how}`})\n${run ? render(queueStatus(run.name, run.date, run.dir, Date.now(), l.defs)) : 'no job log yet'}`);
    }
  } else {
    const run = name && date ? (() => { const d = dirOf(name, date); return d ? { name, date, dir: d } : null; })() : newestRun(dirs, name, date);
    if (!run) { console.log(`no queue run found${name ? ` for ${name}` : ''} under ${dirs.join(', ')} (set TS3_REPO from a worktree)`); process.exit(1); }
    blocks.push(render(queueStatus(run.name, run.date, run.dir)));
  }
  console.log(blocks.join('\n\n'));
}
