// Routing fail-fast (Rodney 2026-10-26, docs/202 hitch guard): a hitch-guard / routing test job stops at once with FAIL when
// routing goes above the limit, instead of finishing a run nobody needs to wait for. queue.ts calls the guard every GUARD_MS
// (the 30 s fail-fast loop) from the profile start; a hit kills the child the way the early noise check does (normal stop path,
// Bin/content restore as usual) and the row reads `FAIL: routing fail-fast: <what> <ms> ms at <t>`.
// Job field: routingFailFast: { ms: 300, scope: "unseen" | "all", allow?: [{ match, reason, ref }] }
//   unseen: ts3native minute lines only (the follower class is known only there):
//     `unseen fallback: mode N ...; longest N ms, >= 300 ms N`  hit = in an on window (mode >= 1) the >= 300 ms count rose
//       (limit <= 300) or `longest` rose above the limit
//     `unseen fallback sites: mode N ...; longest wait N ms`     hit = longest wait rose above the limit
//   all: the above plus `route async: sync plan N ms` and `route jobs: sim waited N ms` lines >= limit, and hitch records
//     (<tag>-<secs>-hitches.jsonl) >= limit led by route or route waits (sim-budget-report.ts split). Hitch records and these
//     lines carry no follower class, so they are judged only with scope all; queue.ts refuses scope all on --ab / loop jobs
//     (their off arms would trip it).
// Ignored like hitch-ab.ts: the first 60 s after the profile start (load settle; counter lines there only set the baseline) and
// save windows (`save: start` .. `save: end` in ts3native.log; hitch records read in a poll that saw a save are skipped).
// Allow rules: a hit whose text matches `match` (regex) is not a failure. Triaged false positives go into
// tools/harness/routing-failfast-allow.json (all jobs) or the job's own allow list; each rule needs a reason and a doc ref.
//   node tools/harness/routing-failfast.ts --self-check
import assert from 'node:assert/strict';
import { closeSync, existsSync, openSync, readdirSync, readFileSync, readSync, statSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { split } from './sim-budget-report.ts';

export type Allow = { match: string; reason: string; ref: string };
export type FailFast = { ms: number; scope: 'unseen' | 'all'; allow?: Allow[] };
export const ALLOW_FILE = join(import.meta.dirname, 'routing-failfast-allow.json');
export const SKIP_MS = 60000;

export function checkAllow(a: unknown, at: string): Allow[] {
  if (!Array.isArray(a)) throw new Error(`${at}: allow must be a list`);
  for (const r of a) {
    if (!r || typeof r.match !== 'string' || !r.reason?.trim?.() || !r.ref?.trim?.()) throw new Error(`${at}: allow rule needs match, reason and ref (doc reference): ${JSON.stringify(r)}`);
    new RegExp(r.match);
  }
  return a as Allow[];
}
export function checkCfg(c: any, at: string): void {
  if (!c || !(Number.isInteger(c.ms) && c.ms > 0)) throw new Error(`${at}: routingFailFast.ms must be a positive integer`);
  if (c.scope !== 'unseen' && c.scope !== 'all') throw new Error(`${at}: routingFailFast.scope must be "unseen" or "all"`);
  if (c.allow !== undefined) checkAllow(c.allow, `${at}: routingFailFast`);
}
export const loadAllow = (file = ALLOW_FILE): Allow[] => existsSync(file) ? checkAllow(JSON.parse(readFileSync(file, 'utf8')), basename(file)) : [];
export const describe = (c: FailFast, fileRules = loadAllow()) =>
  `routing fail-fast: > ${c.ms} ms, scope ${c.scope} (${c.scope === 'unseen' ? 'unseen fallback minute + site lines' : 'unseen lines + sync plans, own-job waits, route-led hitches'}); first 60 s and saves ignored; allow rules ${(c.allow?.length ?? 0) + fileRules.length}`;

export type State = { start: number; save: boolean; sawSave: boolean; uf: { long: number; max: number } | null; ufc: number | null; hits: string[]; allowed: string[] };
export const newState = (start: number): State => ({ start, save: false, sawSave: false, uf: null, ufc: null, hits: [], allowed: [] });

const hit = (st: State, c: FailFast, allow: Allow[], what: string, line: string) => {
  const a = [...(c.allow ?? []), ...allow].find(r => new RegExp(r.match).test(line));
  (a ? st.allowed : st.hits).push(a ? `${what} (allowed: ${a.reason}, ${a.ref})` : `${what}; line: ${line.slice(0, 300)}`);
};

// new ts3native.log lines read at `now` (pure); hits go to st.hits
export function scanLines(st: State, lines: string[], now: number, c: FailFast, allow: Allow[]) {
  const warm = now - st.start < SKIP_MS;
  for (const l of lines) {
    if (/^save: start/.test(l)) { st.save = st.sawSave = true; continue; }
    if (/^save: end/.test(l)) { st.save = false; continue; }
    const quiet = warm || st.save;
    let m = /^unseen fallback: mode (\d+) .*longest (\d+) ms, >= 300 ms (\d+)/.exec(l);
    if (m) {
      const [mode, max, long] = [+m[1], +m[2], +m[3]], p = st.uf;
      st.uf = { long, max };
      if (!quiet && p && mode >= 1 && ((c.ms <= 300 && long > p.long) || (max > c.ms && max > p.max)))
        hit(st, c, allow, `unseen marked route ${max} ms (>= 300 ms count ${p.long} -> ${long})`, l);
      continue;
    }
    m = /^unseen fallback sites: mode (\d+) .*longest wait (\d+) ms/.exec(l);
    if (m) {
      const max = +m[2], p = st.ufc;
      st.ufc = max;
      if (!quiet && p !== null && +m[1] >= 1 && max > c.ms && max > p) hit(st, c, allow, `unseen site wait ${max} ms`, l);
      continue;
    }
    if (c.scope !== 'all' || quiet) continue;
    m = /^route async: sync plan (\d+) ms/.exec(l) ?? /^route jobs: sim waited (\d+) ms/.exec(l);
    if (m && +m[1] >= c.ms) hit(st, c, allow, `${l.startsWith('route async') ? 'sync plan' : 'sim wait on a route job'} ${m[1]} ms`, l);
  }
}

// hitch records (stutter-profile.js), t0 = the profiler's first seconds row t (pure); scope all only
export function scanHitches(st: State, recs: any[], t0: number, c: FailFast, allow: Allow[]) {
  if (c.scope !== 'all') return;
  for (const h of recs) {
    if (h.wallMs < c.ms || h.t - t0 < SKIP_MS) continue;
    const p = split(h, h.wallMs, 0), lead = (Object.entries(p) as [string, number][]).sort((a, b) => b[1] - a[1])[0][0];
    if (lead !== 'route' && lead !== 'waits') continue;
    const plans = Object.entries(h.byEntry ?? {}).map(([k, v]: any) => `${k} ${v.n}/${Math.round(v.ms)}`).join(', ');
    hit(st, c, allow, `route-led hitch ${Math.round(h.wallMs)} ms`, `hitch ${Math.round(h.wallMs)} ms lead ${lead} route ${Math.round(p.route)} waits ${Math.round(p.waits)} key ${h.rtMaxKey ?? '-'} plans ${plans || '-'}`);
  }
}

// live guard over the files (impure): '' or the FAIL reason; log = Bin/ts3native.log, hitchPrefix = probe-out/profile/<tag>
export function routingGuard(c: FailFast, log: string, hitchPrefix: string, allow = loadAllow()) {
  const st = newState(Date.now());
  const size = (f: string) => { try { return statSync(f).size; } catch { return 0; } };
  let off = size(log), carry = '', hf = '', hOff = 0, hCarry = '', t0: number | null = null;
  const tail = (f: string, from: number): [string, number] => {
    const n = size(f);
    if (n <= from) return ['', n < from ? 0 : from]; // shorter: a new log after a reload
    const fd = openSync(f, 'r'), b = Buffer.alloc(n - from);
    try { readSync(fd, b, 0, b.length, from); } finally { closeSync(fd); }
    return [b.toString('latin1'), n];
  };
  return (): string => {
    const now = Date.now();
    st.sawSave = st.save;
    let s; [s, off] = tail(log, off);
    const ls = (carry + s).split(/\r?\n/); carry = ls.pop()!;
    scanLines(st, ls, now, c, allow);
    if (c.scope === 'all') try {
      const dir = dirname(hitchPrefix), pre = basename(hitchPrefix) + '-';
      hf ||= readdirSync(dir).filter(f => f.startsWith(pre) && f.endsWith('-hitches.jsonl') && statSync(join(dir, f)).mtimeMs >= st.start).map(f => join(dir, f))[0] ?? '';
      if (hf && t0 === null) { const r = readFileSync(hf.replace(/-hitches\.jsonl$/, '-seconds.csv'), 'utf8').split(/\r?\n/)[1]; if (r) t0 = Number(r.split(',').at(-1)); }
      if (hf && t0 !== null && Number.isFinite(t0)) {
        let h; [h, hOff] = tail(hf, hOff);
        const hl = (hCarry + h).split(/\r?\n/); hCarry = hl.pop()!;
        // shortcut: hitch t is profiler time, not wall time, so a poll that saw a save skips all its hitch records; map t to wall time if saves hide real hits
        if (!st.sawSave) scanHitches(st, hl.filter(Boolean).map(x => JSON.parse(x)), t0, c, allow);
      }
    } catch { /* profiler files not there yet */ }
    if (!st.hits.length) return '';
    const at = new Date(now).toISOString().slice(11, 19);
    return `routing fail-fast: ${st.hits[0].replace(/; line: /, ` at ${at} UTC; line: `)}${st.hits.length > 1 ? ` (+${st.hits.length - 1} more: ${st.hits.slice(1, 3).join(' | ')})` : ''}`;
  };
}

if (process.argv[1]?.endsWith('routing-failfast.ts') && process.argv.includes('--self-check')) {
  const c: FailFast = { ms: 300, scope: 'unseen' };
  const uf = (mode: number, max: number, long: number) => `unseen fallback: mode ${mode} deadline 300 ms; marks 5 (refused 0), sync 1 cut 0, jobs 4 cut 1, cut answers 1, longest ${max} ms, >= 300 ms ${long}`;
  const ufc = (max: number) => `unseen fallback sites: mode 1 wait 100 ms list 3 (pushes 9 bad 0 stale 0); try/got/timeout/fake/recover/seen C1 1/1/0/0/0/0 C2 0/0/0/0/0/0 C3 0/0/0/0/0/0 (no route 0) C4 0/0/0/0/0/0; longest wait ${max} ms`;
  const T = 1_000_000;
  // first minute: counter rises only set the baseline
  let st = newState(T);
  scanLines(st, [uf(1, 0, 0), uf(1, 900, 2)], T + 30000, c, []);
  assert.equal(st.hits.length, 0);
  scanLines(st, [uf(1, 900, 2)], T + 90000, c, []); assert.equal(st.hits.length, 0); // no rise
  scanLines(st, [uf(1, 950, 3)], T + 150000, c, []);
  assert.match(st.hits[0], /^unseen marked route 950 ms \(>= 300 ms count 2 -> 3\); line: unseen fallback: mode 1/);
  // off window (mode 0) and save window do not count
  st = newState(T);
  scanLines(st, [uf(0, 0, 0)], T + 70000, c, []); scanLines(st, [uf(0, 400, 1)], T + 130000, c, []); assert.equal(st.hits.length, 0);
  scanLines(st, ['save: start #1, x', uf(1, 500, 2), 'save: end #1 result 0 after 9000 ms', uf(1, 500, 2)], T + 190000, c, []); assert.equal(st.hits.length, 0);
  // limit above 300: the >= 300 count alone is not a hit, longest is
  st = newState(T);
  scanLines(st, [uf(1, 0, 0)], T + 70000, { ms: 400, scope: 'unseen' }, []); scanLines(st, [uf(1, 350, 1)], T + 130000, { ms: 400, scope: 'unseen' }, []); assert.equal(st.hits.length, 0);
  scanLines(st, [uf(1, 420, 2)], T + 190000, { ms: 400, scope: 'unseen' }, []); assert.equal(st.hits.length, 1);
  // site wait
  st = newState(T); scanLines(st, [ufc(90), ufc(320)], T + 70000, c, []); assert.match(st.hits[0], /^unseen site wait 320 ms/);
  // sync plans / own-job waits: scope all only
  const sp = 'route async: sync plan 731 ms, IC Plan, reason Foreign, entry nojob, route 2519BFE1, caller 3F7D2227 (0 lines rate-limited before)';
  const sw = 'route jobs: sim waited 412 ms behind an own job (route 0251AB00, no stuck-check mark)';
  st = newState(T); scanLines(st, [sp, sw], T + 70000, c, []); assert.equal(st.hits.length, 0);
  st = newState(T); scanLines(st, [sp, sw, 'route async: sync plan 120 ms, IC Plan'], T + 70000, { ms: 300, scope: 'all' }, []);
  assert.deepEqual(st.hits.map(h => h.split(';')[0]), ['sync plan 731 ms', 'sim wait on a route job 412 ms']);
  st = newState(T); scanLines(st, [sp], T + 10000, { ms: 300, scope: 'all' }, []); assert.equal(st.hits.length, 0); // first minute
  // allow rules: file rules and job rules; reason and ref required
  const rule: Allow = { match: 'reason Foreign', reason: 'foreign plan on a visible Sim, triaged', ref: 'docs/202 §9' };
  st = newState(T); scanLines(st, [sp, sw], T + 70000, { ms: 300, scope: 'all' }, [rule]);
  assert.equal(st.hits.length, 1); assert.match(st.allowed[0], /^sync plan 731 ms \(allowed: foreign plan.*docs\/202 §9\)/);
  st = newState(T); scanLines(st, [sp], T + 70000, { ms: 300, scope: 'all', allow: [rule] }, []); assert.equal(st.hits.length, 0);
  assert.throws(() => checkAllow([{ match: 'x', reason: 'r' }], 'f'), /needs match, reason and ref/);
  assert.throws(() => checkAllow([{ match: '(', reason: 'r', ref: 'd' }], 'f'));
  assert.throws(() => checkCfg({ ms: 300, scope: 'some' }, 'j'), /scope/);
  assert.throws(() => checkCfg({ ms: 0, scope: 'all' }, 'j'), /ms/);
  checkCfg({ ms: 300, scope: 'unseen', allow: [rule] }, 'j');
  assert.deepEqual(loadAllow(), checkAllow(loadAllow(), 'file')); // the shipped file parses
  // hitch records: route-led >= limit after the first minute; script-led and early ones are not hits
  const h = (t: number, wallMs: number, x: object) => ({ t, wallMs, ...x });
  st = newState(T);
  scanHitches(st, [h(5000, 900, { route: 800, byEntry: { Route_Plan: { n: 1, ms: 800 } } }), h(70000, 350, { script: 340 }), h(70000, 450, { rtPlanWait: 400, route: 400, rtMaxKey: '0x0062ef7a' }), h(80000, 200, { route: 190 })], 0, { ms: 300, scope: 'all' }, []);
  assert.deepEqual(st.hits.map(x => x.split(';')[0]), ['route-led hitch 450 ms']);
  st = newState(T); scanHitches(st, [h(70000, 900, { route: 800 })], 0, c, []); assert.equal(st.hits.length, 0); // unseen scope: no hitch records
  console.log('self-check ok');
}
