// Unattended game-test queue: runs a list of jobs back to back, one game start per job, and records one result row each.
//   node tools/harness/queue.ts tools/harness/queues/<name>.json [--from <job id>] [--only id,id] [--dry-run]
//   node tools/harness/queue.ts --self-check          queue parsing, ini merge, verdict and usage parsing, content stash round trip, no game
//   node tools/harness/queue.ts --restore-content     put CC/Store packages back from the stash manifest of an unfinished run
// Queue file: a JSON list of jobs (or { "jobs": [...] }):
//   { id, note?, asi: "tested" | "<path from repo root>", ini?: { "Section.Key": value }, fixes?: { install?: [ids], uninstall?: [ids] },
//     save?: "<name>[.sims3]" | "menu", content?: "as-is" | "no-cc" | "no-store", run: {repro: id} | {matrix: [args]} | {soak: {minutes, speed?}} | {menuMemory: {waitS?, draws?}}
//     | {profile: {minutes, seed?, repro?, mainstack?}} | {routelock: {minutes, speed?}} | {age: {days, checkpoint, keepAlive?, save?}}, gameArgs?: ["-disablestoryprogression"], defaultIni?: { "Config.SimulatorRandomSeed": n },
//     s3ppIni?: { "Patches.MonoTypeEHashProbe": 0 }, s3ssToml?: { "patches.LotStreamingOptimizations.enabled": false }, minCounters?: 20, timeoutMin? }
// minCounters: the job refuses (error row, nothing measured) when /native after the load reports an older counters version
// than this, i.e. the asi lacks the module the ini switches on (verify.json: [Verify] needs v20). A missing asi refuses
// before the game is touched. The full /native before and after goes to probe-out/queue/<queue>-<date>-<job>-native.json
// (verify-verdicts.ts reads it with the profile files to fill docs/51). With ini Verify.Enabled the dump after the load also
// zeroes the [Verify] maxima (/native?vfresetmax=1, read first), so `after` - `before` and the after maxima cover play only
// (the dump says maxReset: true).
// profile: profile-world.ts --tag <queue>-<date>-<job> (--seed: seed_rng at the window start, docs/27); rng-compare.ts reads the rows.
// routelock: autopilot (default ultra) + the probe's /routelock watch for `minutes` (watchdog.ts runs alongside), then stop;
// the summary (G11 movable ms/s, unlocked gate entries, lock hold/wait) goes in the row as `routelock`, the full report with
// per-second series to probe-out/queue/<queue>-<date>-<job>-routelock.json (docs/18 S1).
// age: age-save.ts on the loaded save, saved under a NEW name (save, default <job id>-<date>; never Baseline-*, never an existing
// save), checkpoints <name>-dayN; per checkpoint (and the loaded save first): folder MB, bytes per resource type (save-stats.ts),
// the ts3native.log "save: result" total ms, /memory, sim day -> row.checkpoints and probe-out/queue/<queue>-<date>-<job>-age.csv
// (age-report.ts prints the same summary as the md row).
// menuMemory.draws: n RandomGen.NextDouble() draws at the menu into the row (rngDraws), for the docs/27 Default.ini seed check.
// gameArgs go to TS3W.exe (ts3probe.py serve --launch -- <args>); defaultIni is merged into Bin/Default.ini for the job, then put back;
// s3ppIni the same for Bin/Sims3Performance.ini (S3PP switches, e.g. its Mono caches on/off); s3ssToml the same for
// <user>/S3SS/S3SS.toml ("<table>.<key>", e.g. patches.LotStreamingOptimizations.enabled; S3SS reads it at game start).
// profile.repro: profile-world.ts --repro <id> runs that repros.json entry's steps at the window start (no stack samplers).
// profile.ab: "name,name" -> profile-world.ts --ab (runtime switches alternate on/off in one load), profile.slice: minutes per side.
// profile.mainstack: main | all | <thread id> -> profile-world.ts --mainstack (main-thread stacks beside the sim sampler, docs/20 §7).
// Per job: cycle.ts --restart; copy the job's asi to Bin; write Bin/ts3native.ini = repo native/ts3native/ts3native.ini + job.ini
// (fresh each job, so settings never leak); install-fixes.ts changes; start `ts3probe.py serve --launch` as a child; cycle.ts --save
// (or wait for the menu); run; read /memory and /native before and after; close the game; undo the installer changes.
// content (default "as-is"): with the game closed, move the CC sets offload-phase0.ts switches (fixes.json user-cc and
// store-repacks-test include/exclude, applied to the live Mods folder; no-cc = both, no-store = the Store repacks only) to
// CONTENT_STASH outside Mods, manifest written first; moved back after the job; caches cleared after each move (install-fixes
// cache-clear). ts3agent/ts3fixes/ts3-nointro and files other fixes installed (install-fixes state.json) are never moved. The
// queue refuses to start while a manifest exists (--restore-content). cycle.ts --restart sees the Mods change, clears scriptCache.
// Reuses the other harness tools as child processes (cycle.ts, repro.ts, save-matrix.ts, watchdog.ts, crash-ledger.ts, ai-usage.ts).
// Crash (TS3W.exe gone or "game died"): crash-ledger triage goes in the row, the queue goes on; it stops when the same job has
// crashed twice (today's rows of this queue count, so a resume remembers) or two jobs crash in a row.
// After each job: `ai-usage.ts G <job id>`; at >= 80% in either window the queue pauses and prints the --from line to resume.
// Output: probe-out/queue/<queue>-<date>.jsonl (one row per job, times UTC), docs/data/results/<date>-queue-<queue>.md (table, UTC + PHT).
// After each job the game's Bin/ts3native.log is copied to probe-out/queue/<queue>-<date>-<job>-<HHMM>-ts3native.log (row.nativeLog).
// At the end the tested build (native/ts3native/out/ts3native.asi) and the Bin ts3native.ini found at the start are put back
// (copies of what was in Bin at the start: probe-out/queue/bin-before/). Windows-only by nature (TS3W.exe, taskkill).
import { execFileSync, spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { appendFileSync, copyFileSync, createWriteStream, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { basename, dirname, join, normalize, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { CSV_HEADER, csvLine, summarize, type AgeRow } from './age-report.ts';
import { saveStats } from './save-stats.ts';
import { GUARD_MS } from './game-alive.ts';
import { saveForeground } from './focus-game.ts';

const { values, positionals } = parseArgs({ allowPositionals: true, options: {
  'dry-run': { type: 'boolean' }, from: { type: 'string' }, only: { type: 'string' }, 'self-check': { type: 'boolean' }, 'restore-content': { type: 'boolean' }, port: { type: 'string', default: '47891' },
} });
const ROOT = resolve(import.meta.dirname, '..', '..');
const BIN = process.env.TS3_BIN ?? 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3\\Game\\Bin';
const H = (f: string) => join(ROOT, 'tools', 'harness', f);
const TESTED_ASI = join(ROOT, 'native', 'ts3native', 'out', 'ts3native.asi');
const REPO_INI = join(ROOT, 'native', 'ts3native', 'ts3native.ini');
const OUT = join(ROOT, 'probe-out', 'queue');
const USER = process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3'); // as cycle.ts / install-fixes.ts
// next to the user folder (same volume, so moves are renames), outside Mods so the game never mounts it
const CONTENT_STASH = process.env.TS3_CONTENT_STASH ?? join(dirname(USER), 'The Sims 3 content-stash');
const FIXES_STATE = join(process.env.LOCALAPPDATA ?? join(homedir(), '.local', 'share'), 'ts3-fixes', 'state.json');
const base = `http://127.0.0.1:${values.port}`;
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const log = (m: string) => console.log(`${stamp()} queue: ${m}`);

// ---------------------------------------------------------------- pure parts (covered by --self-check)
type Job = { id: string; note?: string; asi: string; ini?: Record<string, string | number | boolean>; fixes?: { install?: string[]; uninstall?: string[] };
  save?: string; content?: string; run: { repro?: string; matrix?: string[]; soak?: { minutes: number; speed?: string }; menuMemory?: { waitS?: number; draws?: number };
    profile?: { minutes: number; seed?: number; repro?: string; mainstack?: string; ab?: string; slice?: number }; routelock?: { minutes: number; speed?: string }; age?: { days: number; checkpoint: number; keepAlive?: number | 'off'; save?: string } }; gameArgs?: string[]; defaultIni?: Record<string, string | number | boolean>; s3ppIni?: Record<string, string | number | boolean>; s3ssToml?: Record<string, string | number | boolean>; minCounters?: number; timeoutMin?: number };

export function parseQueue(json: unknown): Job[] {
  const jobs = Array.isArray(json) ? json : (json as any)?.jobs;
  if (!Array.isArray(jobs) || !jobs.length) throw new Error('queue: expected a non-empty list of jobs (or { "jobs": [...] })');
  const seen = new Set<string>();
  for (const [i, j] of jobs.entries()) {
    const at = `job ${i + 1}${j?.id ? ` (${j.id})` : ''}`;
    if (typeof j?.id !== 'string' || !/^[\w.-]+$/.test(j.id)) throw new Error(`${at}: id must be letters, digits, . _ -`);
    if (seen.has(j.id)) throw new Error(`${at}: duplicate id`);
    seen.add(j.id);
    if (typeof j.asi !== 'string' || !j.asi) throw new Error(`${at}: asi must be "tested" or a path`);
    for (const k of Object.keys(j.ini ?? {})) if (!/^\w+\.\w+$/.test(k)) throw new Error(`${at}: ini key "${k}" must be Section.Key`);
    for (const k of Object.keys(j.defaultIni ?? {})) if (!/^\w+\.\w+$/.test(k)) throw new Error(`${at}: defaultIni key "${k}" must be Section.Key`);
    for (const k of Object.keys(j.s3ppIni ?? {})) if (!/^\w+\.\w+$/.test(k)) throw new Error(`${at}: s3ppIni key "${k}" must be Section.Key`);
    for (const k of Object.keys(j.s3ssToml ?? {})) if (!/^\w+(\.\w+)+$/.test(k)) throw new Error(`${at}: s3ssToml key "${k}" must be table.key (e.g. patches.LotStreamingOptimizations.enabled)`);
    if (j.gameArgs && !(Array.isArray(j.gameArgs) && j.gameArgs.every((x: unknown) => typeof x === 'string' && x.startsWith('-')))) throw new Error(`${at}: gameArgs must be a list of -switches`);
    for (const k of ['install', 'uninstall']) if (j.fixes?.[k] && !Array.isArray(j.fixes[k])) throw new Error(`${at}: fixes.${k} must be a list of ids`);
    const kinds = Object.keys(j.run ?? {}).filter(k => ['repro', 'matrix', 'soak', 'menuMemory', 'profile', 'routelock', 'age'].includes(k));
    if (kinds.length !== 1 || Object.keys(j.run).length !== 1) throw new Error(`${at}: run needs exactly one of repro, matrix, soak, menuMemory, profile, routelock, age`);
    if (j.run.age) {
      const a = j.run.age;
      if (!(Number.isInteger(a.days) && a.days > 0)) throw new Error(`${at}: run.age.days must be a positive integer`);
      if (!(Number.isInteger(a.checkpoint) && a.checkpoint >= 0)) throw new Error(`${at}: run.age.checkpoint must be an integer >= 0 (0 = none)`);
      if (a.keepAlive !== undefined && a.keepAlive !== 'off' && typeof a.keepAlive !== 'number') throw new Error(`${at}: run.age.keepAlive must be a number or "off"`);
      if (a.save !== undefined && !(typeof a.save === 'string' && /^[\w.-]+$/.test(a.save))) throw new Error(`${at}: run.age.save must be letters, digits, . _ -`);
      // age-save.ts saves under this name: it must never be a baseline (the loaded save is only read)
      if (/^baseline-/i.test(a.save ?? j.id)) throw new Error(`${at}: run.age.save (default the job id) must not start with Baseline-`);
      if (!j.save || j.save === 'menu') throw new Error(`${at}: age needs a save to load`);
    }
    if (j.run.repro !== undefined && typeof j.run.repro !== 'string') throw new Error(`${at}: run.repro must be a repro id`);
    if (j.run.matrix !== undefined && !Array.isArray(j.run.matrix)) throw new Error(`${at}: run.matrix must be a list of save-matrix.ts args`);
    if (j.run.soak && !(j.run.soak.minutes > 0)) throw new Error(`${at}: run.soak.minutes must be > 0`);
    if (j.run.profile && !(j.run.profile.minutes > 0)) throw new Error(`${at}: run.profile.minutes must be > 0`);
    if (j.run.profile?.seed !== undefined && !Number.isInteger(j.run.profile.seed)) throw new Error(`${at}: run.profile.seed must be an integer`);
    if (j.run.profile?.repro !== undefined && typeof j.run.profile.repro !== 'string') throw new Error(`${at}: run.profile.repro must be a repro id`);
    if (j.run.profile?.ab !== undefined && !/^[a-z0-9]+(,[a-z0-9]+)*$/.test(String(j.run.profile.ab))) throw new Error(`${at}: run.profile.ab must be comma-separated switch names`);
    if (j.run.profile?.slice !== undefined && !(Number(j.run.profile.slice) > 0)) throw new Error(`${at}: run.profile.slice must be > 0 minutes`);
    if (j.run.profile?.mainstack !== undefined && !/^(main|all|\d+)$/.test(String(j.run.profile.mainstack))) throw new Error(`${at}: run.profile.mainstack must be main, all or a thread id`);
    if (j.run.menuMemory?.draws !== undefined && !(Number.isInteger(j.run.menuMemory.draws) && j.run.menuMemory.draws > 0)) throw new Error(`${at}: run.menuMemory.draws must be a positive integer`);
    if (j.run.soak?.speed && !['normal', 'fast', 'ultra'].includes(j.run.soak.speed)) throw new Error(`${at}: run.soak.speed must be normal, fast or ultra`);
    if (j.run.routelock && !(j.run.routelock.minutes > 0)) throw new Error(`${at}: run.routelock.minutes must be > 0`);
    if (j.run.routelock?.speed && !['normal', 'fast', 'ultra'].includes(j.run.routelock.speed)) throw new Error(`${at}: run.routelock.speed must be normal, fast or ultra`);
    if (j.run.menuMemory && j.save && j.save !== 'menu') throw new Error(`${at}: menuMemory runs at the menu, set save "menu" or leave it out`);
    if (!j.save && !j.run.repro && !j.run.menuMemory) throw new Error(`${at}: save is required (a save name or "menu")`);
    if (j.timeoutMin !== undefined && !(j.timeoutMin > 0)) throw new Error(`${at}: timeoutMin must be > 0`);
    if (j.minCounters !== undefined && !(Number.isInteger(j.minCounters) && j.minCounters > 0)) throw new Error(`${at}: minCounters must be a positive integer (a counters version)`);
    if (j.content !== undefined && !Object.hasOwn(CONTENT, j.content)) throw new Error(`${at}: content must be one of ${Object.keys(CONTENT).join(', ')}`);
  }
  return jobs;
}

// content variants: fixes.json sets moved out of Mods (offload-phase0.ts: nocc-* = neither set, cc-* = no Store repacks)
const CONTENT: Record<string, string[]> = { 'as-is': [], 'no-cc': ['user-cc', 'store-repacks-test'], 'no-store': ['store-repacks-test'] };
const NEVER_MOVE = /(^|\/)(ts3agent|ts3fixes|ts3-nointro)\.package$/i;
type ContentSet = { include: string[]; exclude: string[] };
const contentSets = (variant: string): ContentSet[] => {
  const fixes = JSON.parse(readFileSync(join(ROOT, 'tools', 'setup', 'fixes.json'), 'utf8')) as { id: string; config?: ContentSet }[];
  return CONTENT[variant].map(id => fixes.find(f => f.id === id)!.config!);
};
// files other fixes installed (absolute paths in install-fixes state.json): never moved
const fixFiles = (statePath: string): Set<string> => {
  if (!existsSync(statePath)) return new Set();
  const st = JSON.parse(readFileSync(statePath, 'utf8')) as Record<string, { files: { path: string }[] }>;
  return new Set(Object.entries(st).filter(([id]) => !['user-cc', 'store-repacks-test'].includes(id))
    .flatMap(([, r]) => r.files.map(f => normalize(f.path).toLowerCase())));
};
// the sets' .package files in the live Mods folder (Mods-relative, '/'), selected like install-fixes.ts user-cc does from the backup
export function contentFiles(mods: string, sets: ContentSet[], keep: Set<string> = new Set()): string[] {
  const out = new Set<string>();
  for (const c of sets) for (const inc of c.include) {
    if (!existsSync(join(mods, inc))) continue;
    for (const n of readdirSync(join(mods, inc), { recursive: true }).map(String)) {
      const rel = `${inc}/${n.replace(/\\/g, '/')}`;
      if (!rel.toLowerCase().endsWith('.package') || c.exclude.some(x => rel.startsWith(x)) || NEVER_MOVE.test(rel)) continue;
      if (!keep.has(normalize(join(mods, rel)).toLowerCase())) out.add(rel);
    }
  }
  return [...out].sort();
}
const move = (from: string, to: string) => {
  mkdirSync(dirname(to), { recursive: true });
  try { renameSync(from, to); } catch (e: any) { if (e.code !== 'EXDEV') throw e; copyFileSync(from, to); rmSync(from); }
};
const manifestOf = (stash: string) => join(stash, 'manifest.json');
// manifest first, so an interrupted move can be undone from it
export function stashContent(mods: string, stash: string, files: string[], meta: object = {}): void {
  if (existsSync(manifestOf(stash))) throw new Error(`content stash manifest exists: ${manifestOf(stash)} (run --restore-content)`);
  mkdirSync(stash, { recursive: true });
  writeFileSync(manifestOf(stash), JSON.stringify({ ...meta, time: new Date().toISOString(), mods, files }, null, 1));
  for (const rel of files) move(join(mods, rel), join(stash, rel));
}
// moves back what is in the stash and never overwrites a file in Mods; manifest and stash go only when nothing is left behind
export function restoreContent(stash: string): { restored: number; problems: string[] } {
  const m = JSON.parse(readFileSync(manifestOf(stash), 'utf8')) as { mods: string; files: string[] };
  let restored = 0;
  const problems: string[] = [];
  for (const rel of m.files) {
    const from = join(stash, rel), to = join(m.mods, rel);
    if (!existsSync(from)) continue; // not moved yet when the run stopped, or already back
    if (existsSync(to)) { problems.push(`in Mods and in the stash, stash copy kept: ${rel}`); continue; }
    try { move(from, to); restored++; } catch (e) { problems.push(`${rel}: ${String(e).split('\n')[0]}`); }
  }
  const left = readdirSync(stash, { recursive: true, withFileTypes: true })
    .filter(e => e.isFile() && !(e.name === 'manifest.json' && normalize(e.parentPath) === normalize(stash)));
  if (!problems.length && left.length) problems.push(`${left.length} files in ${stash} not in the manifest; stash kept`);
  if (!problems.length) rmSync(stash, { recursive: true, force: true });
  return { restored, problems };
}

// repo ini text + "Section.Key": value overrides; keeps comments and line endings, adds missing keys and sections
export function mergeIni(text: string, set: Record<string, string | number | boolean> = {}): string {
  const nl = text.includes('\r\n') ? '\r\n' : '\n';
  const lines = text.split(/\r?\n/);
  for (const [k, raw] of Object.entries(set)) {
    const dot = k.lastIndexOf('.'), sec = k.slice(0, dot), key = k.slice(dot + 1); // the section may hold dots (TOML tables)
    const v = typeof raw === 'boolean' ? (raw ? '1' : '0') : String(raw);
    const s = lines.findIndex(l => l.trim().toLowerCase() === `[${sec.toLowerCase()}]`);
    if (s < 0) { if (lines.at(-1) === '') lines.pop(); lines.push('', `[${sec}]`, `${key}=${v}`, ''); continue; }
    let e = lines.findIndex((l, i) => i > s && l.trim().startsWith('['));
    if (e < 0) e = lines.length;
    const at = lines.findIndex((l, i) => i > s && i < e && new RegExp(`^\\s*${key}\\s*=`, 'i').test(l));
    if (at >= 0) { lines[at] = `${key}=${v}`; continue; }
    let last = e - 1;
    while (last > s && !lines[last].trim()) last--;
    lines.splice(last + 1, 0, `${key}=${v}`);
  }
  return lines.join(nl);
}

// TOML wants true/false, not mergeIni's 1/0
export const tomlSet = (set: Record<string, string | number | boolean>) => Object.fromEntries(Object.entries(set).map(([k, v]) => [k, typeof v === 'boolean' ? String(v) : v]));

// ts3native counter fields per ini section (names from tools/probe/agent.js nativeCounters)
const COUNTERS: Record<string, RegExp> = {
  RouteCache: /^(enabled|calls|hits|misses|stored|otherFails)$/, RouteWorker: /^(worker\w*|waitUs|fallbacks|watchdogs|invoke\w*|hostCalls|planO\w*)$/,
  RouteAsync: /^async\w*$/, RouteBudget: /^budget\w*$/, SaveWrite: /^save\w*$/, Decompress: /^decompress\w*$/, Alloc: /^alloc\w*$/,
  WaterJps: /^waterJps\w*$/, SaveFlush: /^flush\w*$/, SaveCompress: /^compress\w*$/, HeightCache: /^height\w*$/, GcThreadCheck: /^gc\w*$/,
  LotStream: /^(ls[A-Z]\w*|lot[A-Z]\w*|pump[A-Z]\w*|sync[A-Z]\w*|objScene\w*)$/, Verify: /^vf\w*$/,
};
// minCounters gate on the /native answer after the load: '' = go, else why the job refuses
export function counterGate(c: Record<string, unknown> | null, min?: number): string {
  if (min === undefined) return '';
  if (!c) return `minCounters ${min}: /native did not answer`;
  const v = Number(c.version);
  return Number.isFinite(v) && v >= min ? '' : `minCounters ${min}: the asi reports counters v${c.version ?? '?'} (module for this ini missing; build it first)`;
}
export function pickCounters(c: Record<string, unknown> | null, sections: string[]): Record<string, unknown> | null {
  if (!c) return null;
  const res = sections.map(s => COUNTERS[s]).filter(Boolean);
  return Object.fromEntries(Object.entries(c).filter(([k]) => k === 'version' || k === 'error' || res.some(r => r.test(k))));
}
// last `save: result` line (savewrite.cpp) in a ts3native.log slice
export function lastSave(text: string): { result: number; totalMs: number } | null {
  const m = [...text.matchAll(/^save: result (\d+) travel \S+ total (\d+) ms/gm)].at(-1);
  return m ? { result: Number(m[1]), totalMs: Number(m[2]) } : null;
}
// age-save.ts "HH:MM:SS saved <name>" -> name (checkpoints <name>-dayN, then the final <name>)
export const savedName = (line: string) => line.match(/^\d\d:\d\d:\d\d saved (\S+)\s*$/)?.[1] ?? null;
const delta = (a: any, b: any) => (a && b ? Object.fromEntries(Object.keys(b).filter(k => typeof b[k] === 'number' && typeof a[k] === 'number' && b[k] !== a[k]).map(k => [k, b[k] - a[k]])) : null);

// verdict lines of a child's output: repro.ts "<id>: PASS (...)", "FAIL: ...", save-matrix.ts "<case>: PASS", "summary: ..."
export function verdicts(out: string): string[] {
  return out.split(/\r?\n/).map(l => l.replace(/^\d\d:\d\d:\d\d /, '').trim())
    .filter(l => /(^|[\s:])(PASS|FAIL|PENDING)\b|^summary:/.test(l) && !/^(fix under test|step )/.test(l)).slice(-8);
}

// first line of ai-usage.ts: "5h 45% (reset 12:00 PHT) | 7d 62% (...) | read 3 min ago"
export function parseUsage(line: string): { h5: number | null; d7: number | null } {
  const n = (re: RegExp) => { const m = line.match(re); return m ? Number(m[1]) : null; };
  return { h5: n(/5h (\d+)%/), d7: n(/7d (\d+)%/) };
}

// docs/18 G11 gate on the /routelock estimate: go to async only at >= 30 ms/s movable in IP, >= 15 in BP (world from the save name)
export function g11Gate(save: string, movableMsPerSec: number | null | undefined): string {
  const w = save.match(/(?:^|[-_ ])(BP|IP)(?=$|[-_ .])/i)?.[1]?.toUpperCase(), need = w === 'IP' ? 30 : w === 'BP' ? 15 : null;
  if (typeof movableMsPerSec !== 'number') return 'G11 no estimate';
  return need === null ? `G11 no threshold for this world` : `G11 ${movableMsPerSec >= need ? 'go' : 'stop'} (${w} needs >= ${need} ms/s)`;
}
// row summary of a /routelock report: the per-second series stay in the side file
export function routelockSummary(r: any): any {
  if (!r || typeof r !== 'object') return null;
  const { series, ...g11 } = r.g11 ?? {};
  return { ...r, g11: r.g11 ? g11 : null };
}

// stop rule after a job: same job crashed twice (earlier rows count), or this and the previous job both crashed
export function crashStop(crashed: boolean, earlierCrashesOfJob: number, prevCrashed: boolean): string {
  if (!crashed) return '';
  if (earlierCrashesOfJob >= 1) return 'same job crashed twice';
  if (prevCrashed) return 'two jobs in a row crashed';
  return '';
}

if (values['self-check']) {
  const assert = (await import('node:assert')).strict;
  const ok = parseQueue([{ id: 'a', asi: 'tested', save: 'Baseline-BP', run: { soak: { minutes: 5 } } }, { id: 'b', asi: 'x', run: { repro: 'r1' } },
    { id: 'c', asi: 'tested', save: 'menu', run: { menuMemory: { waitS: 30 } } }]);
  assert.equal(ok.length, 3);
  assert.equal(parseQueue({ jobs: [{ id: 'm', asi: 'tested', save: 'S', run: { matrix: ['--cases', 'control'] } }] })[0].id, 'm');
  const bad: [unknown, RegExp][] = [
    [[], /non-empty/], [[{ id: 'a b', asi: 'tested', run: { repro: 'x' } }], /id must/], [[{ id: 'a', asi: 'tested', run: { repro: 'x' } }, { id: 'a', asi: 'tested', run: { repro: 'x' } }], /duplicate/],
    [[{ id: 'a', run: { repro: 'x' } }], /asi/], [[{ id: 'a', asi: 'tested', run: { repro: 'x', soak: { minutes: 1 } } }], /exactly one/],
    [[{ id: 'a', asi: 'tested', run: {} }], /exactly one/], [[{ id: 'a', asi: 'tested', save: 'S', run: { soak: { minutes: 0 } } }], /minutes/],
    [[{ id: 'a', asi: 'tested', ini: { Alloc: 1 }, save: 'S', run: { soak: { minutes: 1 } } }], /Section\.Key/],
    [[{ id: 'a', asi: 'tested', run: { soak: { minutes: 1 } } }], /save is required/], [[{ id: 'a', asi: 'tested', save: 'S', run: { menuMemory: {} } }], /menu/],
    [[{ id: 'a', asi: 'tested', save: 'S', run: { soak: { minutes: 1, speed: 'turbo' } } }], /speed/],
    [[{ id: 'a', asi: 'tested', save: 'menu', content: 'cc-full', run: { menuMemory: {} } }], /content must be/],
    [[{ id: 'a', asi: 'tested', save: 'menu', content: 'toString', run: { menuMemory: {} } }], /content must be/],
    [[{ id: 'a', asi: 'tested', save: 'S', run: { profile: { minutes: 0 } } }], /profile.minutes/],
    [[{ id: 'a', asi: 'tested', save: 'S', run: { profile: { minutes: 5, seed: 1.5 } } }], /seed/],
    [[{ id: 'a', asi: 'tested', save: 'S', gameArgs: ['disablestoryprogression'], run: { profile: { minutes: 5 } } }], /gameArgs/],
    [[{ id: 'a', asi: 'tested', save: 'menu', defaultIni: { Seed: 1 }, run: { menuMemory: {} } }], /defaultIni/],
    [[{ id: 'a', asi: 'tested', save: 'menu', s3ppIni: { MonoIsinstCache: 0 }, run: { menuMemory: {} } }], /s3ppIni/],
    [[{ id: 'a', asi: 'tested', save: 'menu', s3ssToml: { enabled: false }, run: { menuMemory: {} } }], /s3ssToml/],
    [[{ id: 'a', asi: 'tested', save: 'S', run: { profile: { minutes: 5, repro: 7 } } }], /profile.repro/],
    [[{ id: 'a', asi: 'tested', save: 'S', run: { profile: { minutes: 5, mainstack: 'render' } } }], /profile.mainstack/],
    [[{ id: 'a', asi: 'tested', save: 'menu', run: { menuMemory: { draws: 0 } } }], /draws/],
  ];
  assert.equal(parseQueue([{ id: 'a', asi: 'tested', save: 'menu', content: 'no-cc', run: { menuMemory: {} } }])[0].content, 'no-cc');
  assert.equal(contentSets('no-cc').length, 2);
  assert.ok(contentSets('no-store')[0].include.includes('Packages/Sims 3 Store'));
  assert.equal(parseQueue([{ id: 'p', asi: 'tested', save: 'S', gameArgs: ['-disablestoryprogression'], run: { profile: { minutes: 10, seed: 7 } } },
    { id: 'i', asi: 'tested', save: 'menu', defaultIni: { 'Config.SimulatorRandomSeed': 7 }, run: { menuMemory: { draws: 3 } } }]).length, 2);
  for (const [q, re] of bad) assert.throws(() => parseQueue(q), re);
  const age = (a: object, extra: object = {}) => [{ id: 'age-bp', asi: 'tested', save: 'Baseline-BP', run: { age: { days: 14, checkpoint: 2, ...a } }, ...extra }];
  assert.equal(parseQueue(age({ keepAlive: 'off', save: 'Age-BP' }))[0].run.age!.days, 14);
  assert.equal(parseQueue(age({ keepAlive: -50 })).length, 1);
  for (const [q, re] of [[age({ days: 0 }), /days/], [age({ checkpoint: -1 }), /checkpoint/], [age({ keepAlive: 'on' }), /keepAlive/],
    [age({ save: 'Baseline-BP' }), /Baseline-/], [age({ save: 'baseline-bp-old' }), /Baseline-/], [age({}, { id: 'Baseline-age' }), /Baseline-/],
    [age({ save: 'a b' }), /run.age.save/], [age({}, { save: 'menu' }), /age needs a save/]] as [unknown, RegExp][]) assert.throws(() => parseQueue(q), re);
  const slog = 'save: result 0 travel 0/0 total 4100 ms: info 10\r\nsave flush: 1 calls\r\nsave: result 5 travel 0/0 total 9000 ms: info 1\r\n';
  assert.deepEqual(lastSave(slog), { result: 5, totalMs: 9000 });
  assert.equal(lastSave('footprint: x'), null);
  assert.equal(savedName('12:00:01 saved Age-BP-day5'), 'Age-BP-day5');
  assert.equal(savedName('12:00:01 save Age-BP-day5 not started: busy'), null);
  assert.equal(savedName('12:00:01 day 5 08:00 ultra; saved 2 sim days'), null);

  const ini = '; head\r\n[GC]\r\nEnabled=1\r\n\r\n[Alloc]\r\n; why\r\nEnabled=0\r\nMode=1\r\n\r\n[HeightCache]\r\nEnabled=0\r\n';
  const m = mergeIni(ini, { 'Alloc.Enabled': 1, 'alloc.Extra': true, 'HeightCache.Enabled': false, 'New.Key': 'x' });
  assert.ok(m.includes('\r\n') && !/[^\r]\n/.test(m), 'CRLF kept');
  assert.match(m, /\[Alloc\]\r\n; why\r\nEnabled=1\r\nMode=1\r\nExtra=1\r\n\r\n\[HeightCache\]/);
  assert.match(m, /\[HeightCache\]\r\nEnabled=0\r\n/);
  assert.match(m, /\r\n\r\n\[New\]\r\nKey=x\r\n$/);
  assert.match(m, /^; head\r\n\[GC\]\r\nEnabled=1\r\n/);
  assert.equal(mergeIni(ini, {}), ini);
  assert.equal(mergeIni('[A]\nk=1', { 'A.k': 2 }), '[A]\nk=2'); // no override from an earlier job: every job starts from the repo text
  // S3SS.toml: dotted tables, true/false values
  const toml = '[patches.Mimalloc]\nenabled = true\n\n[patches.LotStreamingOptimizations]\nenabled = true\n';
  assert.equal(mergeIni(toml, tomlSet({ 'patches.LotStreamingOptimizations.enabled': false })), '[patches.Mimalloc]\nenabled = true\n\n[patches.LotStreamingOptimizations]\nenabled=false\n');
  assert.equal(parseQueue([{ id: 'l', asi: 'x', save: 'S', s3ssToml: { 'patches.LotStreamingOptimizations.enabled': false }, run: { profile: { minutes: 6, repro: 'lotstream-pan' } } }])[0].run.profile!.repro, 'lotstream-pan');
  assert.equal(parseQueue([{ id: 'm', asi: 'x', save: 'S', run: { profile: { minutes: 6, mainstack: 'main' } } }])[0].run.profile!.mainstack, 'main');

  const c = { version: 14, enabled: 0, calls: 5, allocHitsK: 10, allocMode: 1, heightHits: 3, flushJobs: 2, workerCalls: 9, lotAdds: 4, pumpMaxUs: 9, lsMode: 1, lotsX: 1 };
  assert.deepEqual(pickCounters(c, ['LotStream']), { version: 14, lotAdds: 4, pumpMaxUs: 9, lsMode: 1 });
  assert.deepEqual(pickCounters(c, ['Alloc']), { version: 14, allocHitsK: 10, allocMode: 1 });
  assert.deepEqual(pickCounters(c, ['HeightCache', 'SaveFlush', 'GC']), { version: 14, heightHits: 3, flushJobs: 2 });
  assert.equal(pickCounters(null, ['Alloc']), null);
  // Verify group (docs/51): every vf* key, nothing else
  const v = { version: 20, vfEnabled: 1, vfThreadsOffMain: 3, vfRaceCellSwap: 0, verbose: 1, lotAdds: 2 };
  assert.deepEqual(pickCounters(v, ['Verify']), { version: 20, vfEnabled: 1, vfThreadsOffMain: 3, vfRaceCellSwap: 0 });
  assert.equal(counterGate(v, 20), '');
  assert.equal(counterGate(v, undefined), '');
  assert.match(counterGate({ version: 19, lotAdds: 1 }, 20), /counters v19/);
  assert.match(counterGate(null, 20), /did not answer/);
  assert.match(counterGate({ error: 'no native' }, 20), /counters v\?/);
  assert.equal(parseQueue([{ id: 'v', asi: 'game-copy/x.asi', save: 'S', ini: { 'Verify.Enabled': 1 }, minCounters: 20, run: { profile: { minutes: 7, repro: 'verify-sweep', mainstack: 'main' } } }])[0].minCounters, 20);
  assert.throws(() => parseQueue([{ id: 'v', asi: 'x', save: 'S', minCounters: 19.5, run: { soak: { minutes: 1 } } }]), /minCounters/);
  assert.throws(() => parseQueue([{ id: 'v', asi: 'x', save: 'S', minCounters: '20', run: { soak: { minutes: 1 } } }]), /minCounters/);
  // the shipped verify queue parses and points at existing repros
  const vq = parseQueue(JSON.parse(readFileSync(H('queues/verify.json'), 'utf8')));
  const rids = new Set((JSON.parse(readFileSync(H('repros.json'), 'utf8')) as { id: string }[]).map(r => r.id));
  assert.ok(vq.every(j => j.minCounters === 22 && j.ini?.['Verify.Enabled'] === 1 && j.run.profile?.mainstack === 'main' && rids.has(j.run.profile!.repro!) && (j.timeoutMin ?? 60) <= 25));
  assert.deepEqual(vq.map(j => j.save), ['Baseline-BP', 'Baseline-IP']);
  assert.deepEqual(delta({ a: 1, b: 2, s: 'x' }, { a: 4, b: 2, s: 'y' }), { a: 3 });

  const repro = '12:00:01 bp-route-async: route\n  fix under test: async plan\n12:00:02 step patches {}: ok\n12:10:00 stuck: none\n12:10:01 bp-route-async: PASS (expected: no stuck)';
  assert.deepEqual(verdicts(repro), ['bp-route-async: PASS (expected: no stuck)']);
  assert.deepEqual(verdicts('12:00:00 FAIL: game died at 11:59:00 (TS3W.exe process gone)\nlast crash report'), ['FAIL: game died at 11:59:00 (TS3W.exe process gone)']);
  const matrix = '12:00:00 == control: x\n12:01:00 control: PASS\n12:02:00 identical: FAIL\n    type 0x1: differs\n12:03:00 summary: 2 cases, 1 passed, 1 failed (identical)\n12:03:00 results: a, b';
  assert.deepEqual(verdicts(matrix), ['control: PASS', 'identical: FAIL', 'summary: 2 cases, 1 passed, 1 failed (identical)']);

  assert.deepEqual(parseUsage('5h 45% (reset 12:00 PHT) | 7d 81% (reset 09:00 PHT) | read 3 min ago'), { h5: 45, d7: 81 });
  assert.deepEqual(parseUsage('5h - | 7d 12% | read 1 min ago'), { h5: null, d7: 12 });

  assert.equal(parseQueue([{ id: 'r', asi: 'tested', save: 'Baseline-IP', run: { routelock: { minutes: 10, speed: 'fast' } } }])[0].run.routelock!.minutes, 10);
  assert.throws(() => parseQueue([{ id: 'r', asi: 'tested', save: 'S', run: { routelock: { minutes: 0 } } }]), /routelock.minutes/);
  assert.throws(() => parseQueue([{ id: 'r', asi: 'tested', save: 'S', run: { routelock: { minutes: 1, speed: 'x3' } } }]), /routelock.speed/);
  assert.equal(g11Gate('Baseline-IP', 31.2), 'G11 go (IP needs >= 30 ms/s)');
  assert.equal(g11Gate('Baseline-IP.sims3', 29), 'G11 stop (IP needs >= 30 ms/s)');
  assert.equal(g11Gate('Baseline-BP', 15), 'G11 go (BP needs >= 15 ms/s)');
  assert.equal(g11Gate('Baseline-SV', 50), 'G11 no threshold for this world');
  assert.equal(g11Gate('Baseline-BP', null), 'G11 no estimate');
  assert.deepEqual(routelockSummary({ since: 600, g11: { movableMsPerSec: 3, series: { movableMs: [1] } } }), { since: 600, g11: { movableMsPerSec: 3 } });
  assert.deepEqual(routelockSummary({ since: 1, g11: null }), { since: 1, g11: null });
  assert.equal(routelockSummary(null), null);

  assert.equal(crashStop(false, 3, true), '');
  assert.equal(crashStop(true, 0, false), '');
  assert.equal(crashStop(true, 1, false), 'same job crashed twice');
  assert.equal(crashStop(true, 0, true), 'two jobs in a row crashed');

  // content stash round trip on a temp Mods tree
  const tmp = mkdtempSync(join(tmpdir(), 'queue-content-')), mods = join(tmp, 'Mods'), stash = join(tmp, 'stash');
  const sets = [{ include: ['Packages/CC', 'Overrides/Body'], exclude: ['Packages/CC/Scripts'] }, { include: ['Packages/Store'], exclude: [] }];
  const mk = (rel: string) => { mkdirSync(dirname(join(mods, rel)), { recursive: true }); writeFileSync(join(mods, rel), rel); };
  const all = ['Packages/CC/a.package', 'Packages/CC/sub/b.package', 'Packages/CC/readme.txt', 'Packages/CC/Scripts/s.package', 'Packages/CC/ts3agent.package',
    'Packages/CC/fix.package', 'Overrides/Body/c.package', 'Packages/Store/d.package', 'Packages/ts3fixes.package', 'Packages/root.package'];
  all.forEach(mk);
  const moved = ['Overrides/Body/c.package', 'Packages/CC/a.package', 'Packages/CC/sub/b.package', 'Packages/Store/d.package'];
  const files = contentFiles(mods, sets, new Set([normalize(join(mods, 'Packages/CC/fix.package')).toLowerCase()]));
  assert.deepEqual(files, moved);
  assert.deepEqual(contentFiles(mods, []), []);
  stashContent(mods, stash, files, { job: 't' });
  assert.ok(moved.every(r => !existsSync(join(mods, r)) && existsSync(join(stash, r))), 'moved to the stash');
  assert.ok(all.filter(r => !moved.includes(r)).every(r => existsSync(join(mods, r))), 'the rest stays');
  assert.deepEqual(JSON.parse(readFileSync(manifestOf(stash), 'utf8')).files, moved);
  assert.throws(() => stashContent(mods, stash, []), /manifest exists/);
  assert.deepEqual(restoreContent(stash), { restored: 4, problems: [] });
  assert.ok(all.every(r => readFileSync(join(mods, r), 'utf8') === r) && !existsSync(stash), 'all back, stash gone');
  // interrupted: manifest written, only one file moved; a file present in both places is never overwritten
  mkdirSync(stash, { recursive: true });
  writeFileSync(manifestOf(stash), JSON.stringify({ mods, files: moved }));
  move(join(mods, moved[0]), join(stash, moved[0]));
  mkdirSync(dirname(join(stash, moved[1])), { recursive: true });
  writeFileSync(join(stash, moved[1]), 'other');
  const r2 = restoreContent(stash);
  assert.equal(r2.restored, 1);
  assert.match(r2.problems.join(), /stash copy kept: Packages\/CC\/a\.package/);
  assert.ok(existsSync(join(mods, moved[0])) && readFileSync(join(mods, moved[1]), 'utf8') === moved[1] && existsSync(manifestOf(stash)), 'kept for a human');
  rmSync(tmp, { recursive: true, force: true });
  console.log('self-check ok');
  process.exit(0);
}

const gameRunning = () => { try { return execFileSync('tasklist', ['/NH', '/FI', 'IMAGENAME eq TS3W.exe']).toString().includes('TS3W.exe'); } catch { return true; } };
const fixesTool = join(ROOT, 'tools', 'setup', 'install-fixes.ts');
// the Steam TS3W.exe exits on its own ("application-requested") when Steam is not running: start it silently, wait up to 180 s
// for its web helper (2026-10-17: Steam had closed, so one job's game quit mid-run and the next two quit at launch)
const steamUp = () => { try { return execFileSync('tasklist', ['/NH', '/FI', 'IMAGENAME eq steamwebhelper.exe']).toString().includes('steamwebhelper'); } catch { return false; } };
async function ensureSteam() {
  if (steamUp()) return;
  const exe = join(process.env['ProgramFiles(x86)'] ?? 'C:\\Program Files (x86)', 'Steam', 'steam.exe');
  log(`Steam not running: starting ${exe} -silent`);
  spawn(exe, ['-silent'], { detached: true, stdio: 'ignore' }).unref();
  for (let i = 0; i < 180 && !steamUp(); i++) await sleep(1000);
  if (!steamUp()) throw new Error('Steam is not running and did not start in 180 s (the Steam game quits without it)');
  await sleep(15000); // login
}

if (values['restore-content']) {
  if (!existsSync(manifestOf(CONTENT_STASH))) { console.log(`no content stash manifest in ${CONTENT_STASH}: nothing to restore`); process.exit(0); }
  if (gameRunning()) { console.error('TS3W.exe is running: close the game first'); process.exit(2); }
  const r = restoreContent(CONTENT_STASH);
  console.log(`restored ${r.restored} packages to ${join(USER, 'Mods')}`);
  for (const p of r.problems) console.error(`  ${p}`);
  if (r.problems.length) { console.error(`manifest kept: ${manifestOf(CONTENT_STASH)}; fix by hand, then rerun --restore-content`); process.exit(1); }
  execFileSync(process.execPath, [fixesTool, 'install', '--only', 'cache-clear'], { stdio: 'inherit' });
  process.exit(0);
}

// ---------------------------------------------------------------- queue run
const file = positionals[0];
if (!file) { console.error('usage: node tools/harness/queue.ts <queue.json> [--from id] [--only id,id] [--dry-run] | --self-check | --restore-content'); process.exit(2); }
// an unfinished run's packages are still in the stash: put them back before measuring anything
const stale = existsSync(manifestOf(CONTENT_STASH)) ? `content stash manifest from an unfinished run: ${manifestOf(CONTENT_STASH)}; run node tools/harness/queue.ts --restore-content` : '';
if (stale && !values['dry-run']) { console.error(`refusing to start: ${stale}`); process.exit(2); }
const queueName = basename(file).replace(/\.json$/i, '');
const all = parseQueue(JSON.parse(readFileSync(resolve(file), 'utf8')));
let jobs = all;
if (values.from) {
  const i = all.findIndex(j => j.id === values.from);
  if (i < 0) { console.error(`--from: no job ${values.from}`); process.exit(2); }
  jobs = all.slice(i);
}
if (values.only) {
  const ids = values.only.split(',').map(s => s.trim());
  const miss = ids.filter(id => !all.some(j => j.id === id));
  if (miss.length) { console.error(`--only: no job ${miss.join(', ')}`); process.exit(2); }
  jobs = jobs.filter(j => ids.includes(j.id));
}
const REPROS = JSON.parse(readFileSync(H('repros.json'), 'utf8')) as { id: string; save: string }[];
const asiPath = (j: Job) => (j.asi === 'tested' ? TESTED_ASI : resolve(ROOT, j.asi));
const saveOf = (j: Job) => j.save ?? (j.run.menuMemory ? 'menu' : REPROS.find(r => r.id === j.run.repro)?.save);
const saveFile = (s: string) => (/\.sims3$/i.test(s) ? s : `${s}.sims3`);
const ageName = (j: Job) => j.run.age?.save ?? `${j.id}-${new Date().toISOString().slice(0, 10)}`;
const profileTag = (j: Job) => `${queueName}-${new Date().toISOString().slice(0, 10)}-${j.id}`;
const runArgs = (j: Job): string[] | null =>
  j.run.repro ? [H('repro.ts'), 'run', j.run.repro, '--no-load'] : j.run.matrix ? [H('save-matrix.ts'), ...j.run.matrix]
    : j.run.profile ? [H('profile-world.ts'), '--tag', profileTag(j), '--minutes', String(j.run.profile.minutes), ...(j.run.profile.seed !== undefined ? ['--seed', String(j.run.profile.seed)] : []),
      ...(j.run.profile.repro ? ['--repro', j.run.profile.repro] : []), ...(j.run.profile.mainstack ? ['--mainstack', String(j.run.profile.mainstack)] : []),
      ...(j.run.profile.ab ? ['--ab', j.run.profile.ab, '--slice', String(j.run.profile.slice ?? 2)] : [])]
    : j.run.age ? [H('age-save.ts'), '--days', String(j.run.age.days), '--checkpoint', String(j.run.age.checkpoint), '--save', ageName(j), '--port', values.port!,
      '--keep-alive', String(j.run.age.keepAlive ?? -50)] : null;
const SAVES = join(USER, 'Saves');
const LOG = join(BIN, 'ts3native.log');
// saves an age job would overwrite: <name>.sims3 and <name>-day*.sims3
const ageClash = (j: Job) => { const n = ageName(j).toLowerCase(); return existsSync(SAVES) ? readdirSync(SAVES).filter(f => f.toLowerCase() === `${n}.sims3` || f.toLowerCase().startsWith(`${n}-day`)) : []; };
const DEFAULT_INI = join(BIN, 'Default.ini'), S3PP_INI = join(BIN, 'Sims3Performance.ini'), S3SS_TOML = join(USER, 'S3SS', 'S3SS.toml');
for (const j of jobs) {
  if (j.run.repro && !REPROS.some(r => r.id === j.run.repro)) { console.error(`${j.id}: no repro ${j.run.repro} in tools/harness/repros.json`); process.exit(2); }
  if (j.run.profile?.repro && !REPROS.some(r => r.id === j.run.profile!.repro)) { console.error(`${j.id}: no repro ${j.run.profile.repro} in tools/harness/repros.json`); process.exit(2); }
  if (!saveOf(j)) { console.error(`${j.id}: no save`); process.exit(2); }
}

if (values['dry-run']) {
  console.log(`queue ${queueName}: ${jobs.length} of ${all.length} jobs (dry run, nothing touched)`);
  if (stale) console.log(`** a real run would refuse to start: ${stale}`);
  for (const j of jobs) {
    const a = asiPath(j), s = saveOf(j)!, r = runArgs(j);
    console.log(`\n${j.id}${j.note ? ` - ${j.note}` : ''} (timeout ${j.timeoutMin ?? 60} min)`);
    if (CONTENT[j.content ?? 'as-is'].length) {
      const n = contentFiles(join(USER, 'Mods'), contentSets(j.content!), fixFiles(FIXES_STATE)).length;
      console.log(`  0 content ${j.content}: game closed, manifest, move ${n} packages now in Mods (${CONTENT[j.content!].join(' + ')} sets) to ${CONTENT_STASH}; cache-clear; back + cache-clear after the job`);
    }
    console.log(`  1 node tools/harness/cycle.ts --restart`);
    console.log(`  2 asi ${j.asi === 'tested' ? 'tested build' : j.asi}${existsSync(a) ? '' : '  ** MISSING: the job refuses before touching the game **'} -> Bin/ts3native.asi`);
    console.log(`  3 Bin/ts3native.ini = repo ini${Object.keys(j.ini ?? {}).length ? ` + ${Object.entries(j.ini!).map(([k, v]) => `${k}=${v}`).join(', ')}` : ' (unchanged)'}`);
    if (j.defaultIni) console.log(`  3 Bin/Default.ini + ${Object.entries(j.defaultIni).map(([k, v]) => `${k}=${v}`).join(', ')}   (put back after the job)`);
    if (j.s3ppIni) console.log(`  3 Bin/Sims3Performance.ini + ${Object.entries(j.s3ppIni).map(([k, v]) => `${k}=${v}`).join(', ')}   (put back after the job)`);
    if (j.s3ssToml) console.log(`  3 S3SS/S3SS.toml + ${Object.entries(j.s3ssToml).map(([k, v]) => `${k}=${v}`).join(', ')}${existsSync(S3SS_TOML) ? '' : '  ** MISSING **'}   (put back after the job)`);
    if (j.fixes?.install?.length) console.log(`  4 node tools/setup/install-fixes.ts install --only ${j.fixes.install.join(',')}   (uninstalled after the job)`);
    if (j.fixes?.uninstall?.length) console.log(`  4 node tools/setup/install-fixes.ts uninstall --only ${j.fixes.uninstall.join(',')}   (reinstalled after the job)`);
    console.log(`  5 python tools/probe/ts3probe.py serve --launch${j.gameArgs?.length ? ` -- ${j.gameArgs.join(' ')}` : ''}   (child, killed at job end)`);
    console.log(`  6 ${s === 'menu' ? 'wait for the main menu (agent ping)' : `node tools/harness/cycle.ts --save ${saveFile(s)}`}`);
    if (j.run.age) { const c = ageClash(j); console.log(`  7 saves ${ageName(j)}-dayN every ${j.run.age.checkpoint} sim days, final ${ageName(j)}; per checkpoint size/types/save ms/memory -> ${queueName}-<date>-${j.id}-age.csv${c.length ? `  ** REFUSED: would overwrite ${c.join(', ')} **` : ''}`); }
    console.log(`  7 ${r ? `node ${r.map(x => x.replace(ROOT + '\\', '').replace(/\\/g, '/')).join(' ')}` : j.run.soak ? `soak ${j.run.soak.minutes} min at ${j.run.soak.speed ?? 'ultra'} (autopilot + watchdog.ts)`
      : j.run.routelock ? `routelock ${j.run.routelock.minutes} min at ${j.run.routelock.speed ?? 'ultra'}: autopilot, /routelock?start=1, watchdog.ts, /routelock?stop=1 -> row.routelock` : `menu memory after ${j.run.menuMemory?.waitS ?? 60} s${j.run.menuMemory?.draws ? `, ${j.run.menuMemory.draws} RandomGen draws` : ''}`}`);
    if (j.minCounters !== undefined) console.log(`  7 refuse unless /native after the load reports counters >= v${j.minCounters}; full /native -> ${queueName}-<date>-${j.id}-native.json${j.ini?.['Verify.Enabled'] ? ' ([Verify] maxima zeroed after that read: live window)' : ''}`);
    console.log(`  8 /memory + /native (${['version', ...Object.keys(j.ini ?? {}).map(k => k.split('.')[0]).filter((v, i, x) => x.indexOf(v) === i)].join(', ')}) before/after; close game; ai-usage.ts G ${j.id}`);
  }
  process.exit(0);
}

// ---- helpers that touch the machine
async function get(path: string, timeoutMs = 20000): Promise<any> {
  try { return await (await fetch(`${base}/${path}`, { signal: AbortSignal.timeout(timeoutMs) })).json(); } catch { return null; }
}
const agent = (cmd: string, args?: object) => { const q = new URLSearchParams({ cmd, timeout: '8000' }); if (args) q.set('args', JSON.stringify(args)); return get(`agent?${q}`); };
const killTree = (pid?: number) => { if (pid) try { execFileSync('taskkill', ['/T', '/F', '/PID', String(pid)], { stdio: 'ignore' }); } catch { /* gone */ } };

// set while a job's game runs: '' when the game and the probe server are up, else why not; stall = the guard's verdict
let jobGuard: (() => string) | null = null, stall = '';
// child process with output to the console and the job log; killed (whole tree) at the deadline
function run(cmd: string, args: string[], logFile: string, deadline: number, onLine?: (l: string) => void): Promise<{ code: number | null; out: string; timedOut: boolean }> {
  return new Promise(res => {
    const p = spawn(cmd, args, { cwd: ROOT }), f = createWriteStream(logFile, { flags: 'a' });
    let out = '', timedOut = false, part = '';
    f.write(`\n${new Date().toISOString()} $ ${cmd} ${args.join(' ')}\n`);
    const on = (d: Buffer) => {
      const s = d.toString(); out += s; process.stdout.write(s); f.write(s);
      if (onLine) { const ls = (part + s).split(/\r?\n/); part = ls.pop()!; ls.forEach(onLine); }
    };
    p.stdout.on('data', on); p.stderr.on('data', on);
    const t = setTimeout(() => { timedOut = true; killTree(p.pid); }, Math.max(1000, deadline - Date.now()));
    // fail fast (Rodney 2026-10-16): once the game is launched, every 30 s the game and the probe server must still be up;
    // two misses in a row end the child instead of waiting for the job deadline (default 60 min)
    let misses = 0;
    const g = jobGuard && setInterval(() => {
      const why = jobGuard!();
      misses = why ? misses + 1 : 0;
      if (misses >= 2) { stall ||= `${why} for ~${2 * GUARD_MS / 1000} s (fail-fast guard)`; on(Buffer.from(`FAIL: ${stall}; child stopped\n`)); killTree(p.pid); }
    }, GUARD_MS);
    p.on('error', e => on(Buffer.from(`spawn failed: ${e}\n`)));
    p.on('close', code => { clearTimeout(t); if (g) clearInterval(g); f.end(); res({ code, out, timedOut }); });
  });
}
const node = (args: string[], logFile: string, deadline: number, onLine?: (l: string) => void) => run(process.execPath, args, logFile, deadline, onLine);

async function closeGame(probePid?: number) {
  await get('detach', 10000); // detach first: killing a host mid-call can take the game down
  killTree(probePid);
  if (gameRunning()) try { execFileSync('taskkill', ['/F', '/IM', 'TS3W.exe'], { stdio: 'ignore' }); } catch { /* gone */ }
  for (let i = 0; i < 60 && gameRunning(); i++) await sleep(1000);
}
const triage = () => {
  try { return execFileSync(process.execPath, [H('crash-ledger.ts'), 'triage']).toString().trim().split(/\n\s*\n/).at(-1) ?? ''; }
  catch (e) { return `crash-ledger triage failed: ${String(e).split('\n')[0]}`; }
};

const date = new Date().toISOString().slice(0, 10);
mkdirSync(OUT, { recursive: true });
mkdirSync(join(ROOT, 'docs', 'data', 'results'), { recursive: true });
const jsonl = join(OUT, `${queueName}-${date}.jsonl`), md = join(ROOT, 'docs', 'data', 'results', `${date}-queue-${queueName}.md`);
const earlier: any[] = existsSync(jsonl) ? readFileSync(jsonl, 'utf8').split('\n').filter(Boolean).map(l => JSON.parse(l)) : [];
const pht = (iso: string) => new Date(Date.parse(iso) + 8 * 3600e3).toISOString().slice(11, 16);
let build = 'unknown';
try { build = execFileSync('git', ['-C', ROOT, 'rev-parse', '--short', 'HEAD']).toString().trim(); } catch { /* not a repo */ }
if (!existsSync(md)) writeFileSync(md, `# Queue ${queueName} ${date}\n\nTool: \`tools/harness/queue.ts\` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: \`probe-out/queue/${queueName}-${date}.jsonl\`.\n`);
appendFileSync(md, `\n## ${new Date().toISOString().slice(0, 16)} UTC, build ${build}, jobs ${jobs.map(j => j.id).join(', ')}\n\n| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |\n|---|---|---|---|---|---|---|---|---|\n`);

// keep what Bin had, to restore the ini and fall back for the asi at the end
const BEFORE = join(OUT, 'bin-before');
// one queue at a time (2026-10-17: a queue whose parent shell was stopped kept running, and a second one fought it for the
// game: each restart killed the other's TS3W.exe)
if (!values['dry-run']) {
  const others = execFileSync('powershell', ['-NoProfile', '-NonInteractive', '-Command',
    `Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.ProcessId -ne ${process.pid} -and $_.CommandLine -match 'harness[\\/]queue\.ts' -and $_.CommandLine -notmatch '--dry-run|--self-check' } | ForEach-Object { $_.ProcessId }`]).toString().trim();
  if (others) { console.error(`another queue is running (node PID ${others.split(/\s+/).join(', ')}): stop it first (taskkill /PID <pid> /T /F)`); process.exit(3); }
}
if (!values['dry-run']) saveForeground(); // the window to give focus back to once the game takes it (focus-game.ts)
mkdirSync(BEFORE, { recursive: true });
for (const f of ['ts3native.asi', 'ts3native.ini', 'Default.ini', 'Sims3Performance.ini']) if (existsSync(join(BIN, f))) copyFileSync(join(BIN, f), join(BEFORE, f));
if (existsSync(S3SS_TOML)) copyFileSync(S3SS_TOML, join(BEFORE, 'S3SS.toml'));
const repoIni = readFileSync(REPO_INI, 'utf8');
const sha = (p: string) => (existsSync(p) ? createHash('sha256').update(readFileSync(p)).digest('hex').slice(0, 12) : 'missing');
const memLine = (m: any) => (m ? `${m.commitMB}/${m.privateMB}/${m.largestFreeMB}` : '?');

let prevCrashed = !!earlier.at(-1)?.crashed, stopWhy = '', exitCode = 0;
for (const [n, j] of jobs.entries()) {
  const t0 = new Date().toISOString(), deadline = Date.now() + (j.timeoutMin ?? 60) * 60000;
  const logFile = join(OUT, `${queueName}-${date}-${j.id}.log`);
  const sections = [...new Set(Object.keys(j.ini ?? {}).map(k => k.split('.')[0]))];
  const s = saveOf(j)!;
  log(`== ${j.id} (${n + 1}/${jobs.length})${j.note ? `: ${j.note}` : ''}; log ${logFile}`);
  const row: any = { time: t0, queue: queueName, build, job: j.id, note: j.note ?? null, asi: j.asi, asiSha: sha(asiPath(j)), ini: j.ini ?? {}, fixes: j.fixes ?? null,
    save: s, content: j.content ?? 'as-is', contentMoved: 0, run: j.run, gameArgs: j.gameArgs ?? [], defaultIni: j.defaultIni ?? null, s3ppIni: j.s3ppIni ?? null, s3ssToml: j.s3ssToml ?? null, profileTag: j.run.profile ? profileTag(j) : null, exitCode: null, verdict: [], pass: false, crashed: false, crash: null, timedOut: false, memory: {}, counters: {}, error: null };
  let probe: ReturnType<typeof spawn> | undefined, applied = { install: [] as string[], uninstall: [] as string[] }, launched = false, defaultIniOrig: string | null = null, s3ppIniOrig: string | null = null, s3ssTomlOrig: string | null = null, stashed = false;
  try {
    if (!existsSync(asiPath(j))) throw new Error(`asi missing: ${j.asi} (refused before touching the game)`);
    if (!values['dry-run']) await ensureSteam();
    if (CONTENT[row.content].length) {
      await closeGame(); // files must not be open when they move
      if (gameRunning()) throw new Error('content: TS3W.exe still running, nothing moved');
      const files = contentFiles(join(USER, 'Mods'), contentSets(row.content), fixFiles(FIXES_STATE));
      stashed = true; // from here on the manifest exists, even if a move below throws
      stashContent(join(USER, 'Mods'), CONTENT_STASH, files, { queue: queueName, job: j.id, content: row.content });
      row.contentMoved = files.length;
      log(`${j.id}: content ${row.content}: ${files.length} packages moved to ${CONTENT_STASH}`);
      const rc = await node([fixesTool, 'install', '--only', 'cache-clear'], logFile, deadline);
      if (rc.code !== 0) throw new Error(`install-fixes cache-clear exit ${rc.code}`);
    }
    const r0 = await node([H('cycle.ts'), '--restart'], logFile, deadline);
    if (r0.code !== 0) throw new Error(`cycle.ts --restart exit ${r0.code}`);
    copyFileSync(asiPath(j), join(BIN, 'ts3native.asi'));
    writeFileSync(join(BIN, 'ts3native.ini'), mergeIni(repoIni, j.ini));
    if (j.defaultIni) { defaultIniOrig = readFileSync(DEFAULT_INI, 'utf8'); writeFileSync(DEFAULT_INI, mergeIni(defaultIniOrig, j.defaultIni)); }
    if (j.s3ppIni) {
      if (!existsSync(S3PP_INI)) throw new Error('s3ppIni: Bin/Sims3Performance.ini missing (s3pp not installed)');
      s3ppIniOrig = readFileSync(S3PP_INI, 'utf8'); writeFileSync(S3PP_INI, mergeIni(s3ppIniOrig, j.s3ppIni));
    }
    if (j.s3ssToml) {
      if (!existsSync(S3SS_TOML)) throw new Error('s3ssToml: S3SS/S3SS.toml missing (s3ss-config not installed)');
      s3ssTomlOrig = readFileSync(S3SS_TOML, 'utf8'); writeFileSync(S3SS_TOML, mergeIni(s3ssTomlOrig, tomlSet(j.s3ssToml)));
    }
    for (const [k, ids] of [['install', j.fixes?.install], ['uninstall', j.fixes?.uninstall]] as const) {
      if (!ids?.length) continue;
      const r = await node([join(ROOT, 'tools', 'setup', 'install-fixes.ts'), k, '--only', ids.join(',')], logFile, deadline);
      if (r.code !== 0) throw new Error(`install-fixes ${k} exit ${r.code}`);
      applied[k] = [...ids];
    }
    const pf = createWriteStream(join(OUT, `${queueName}-${date}-${j.id}-probe.log`), { flags: 'a' });
    probe = spawn('python', [join(ROOT, 'tools', 'probe', 'ts3probe.py'), 'serve', '--launch', '--port', values.port!, ...(j.gameArgs?.length ? ['--', ...j.gameArgs] : [])], { cwd: ROOT });
    probe.stdout?.pipe(pf); probe.stderr?.pipe(pf); launched = true;
    const pr = probe;
    jobGuard = () => pr.exitCode !== null || pr.signalCode !== null ? `probe server exited (${pr.exitCode ?? pr.signalCode})` : !gameRunning() ? 'TS3W.exe process gone' : '';
    if (s === 'menu') {
      let up = false, misses = 0, nextCheck = Date.now() + GUARD_MS;
      const end = Math.min(deadline, Date.now() + 300000);
      while (!up && Date.now() < end) {
        up = (await agent('ping'))?.ok === true;
        if (!up && Date.now() >= nextCheck) { // same fail-fast rule as run(): two misses ~30 s apart
          nextCheck = Date.now() + GUARD_MS;
          const why = jobGuard();
          misses = why ? misses + 1 : 0;
          if (misses >= 2) { stall = `${why} for ~${2 * GUARD_MS / 1000} s (fail-fast guard)`; throw new Error(`waiting for the menu: ${stall}`); }
        }
        if (!up) await sleep(2000);
      }
      if (!up) throw new Error('agent not answering at the menu within 300 s');
    } else {
      const r = await node([H('cycle.ts'), '--save', saveFile(s)], logFile, deadline);
      if (r.code !== 0) throw new Error(`cycle.ts --save exit ${r.code}${r.timedOut ? ' (timeout)' : ''}`);
    }
    if (j.run.menuMemory) await sleep((j.run.menuMemory.waitS ?? 60) * 1000);
    if (j.run.menuMemory?.draws) { // 0.0 means gRandomGen not cached yet (docs/27 §1.1)
      row.rngDraws = [];
      for (let i = 0; i < j.run.menuMemory.draws; i++) row.rngDraws.push((await agent('reflect_call', { path: 'Sims3.SimIFace.RandomGen.NextDouble' }))?.result ?? null);
    }
    row.memory.before = await get('memory');
    const resetMax = !!j.ini?.['Verify.Enabled'];
    const c0 = await get(resetMax ? 'native?vfresetmax=1' : 'native'); // read first, then the [Verify] maxima start again
    const gate = counterGate(c0, j.minCounters);
    if (gate) throw new Error(`${gate}; refused, nothing run`);
    const args = runArgs(j);
    if (j.run.age) {
      const clash = ageClash(j);
      if (clash.length) throw new Error(`age: would overwrite ${clash.join(', ')}; set run.age.save to a new name`);
      const csv = join(OUT, `${queueName}-${date}-${j.id}-age.csv`);
      if (!existsSync(csv)) writeFileSync(csv, CSV_HEADER + '\n');
      let logOff = existsSync(LOG) ? readFileSync(LOG).length : 0;
      row.ageSave = ageName(j); row.ageCsv = csv; row.checkpoints = [];
      const checkpoint = async (save: string, saved: boolean) => {
        if (saved) await sleep(3000); // let the flush worker and the folder settle
        let st: ReturnType<typeof saveStats> | null = null;
        try { st = saveStats(join(SAVES, saveFile(save))); } catch (e) { log(`${j.id}: save-stats ${save}: ${String(e).split('\n')[0]}`); }
        let sv: ReturnType<typeof lastSave> = null;
        if (saved && existsSync(LOG)) { const b = readFileSync(LOG); sv = lastSave(b.subarray(Math.min(logOff, b.length)).toString('latin1')); logOff = b.length; }
        const mem = await get('memory'), t = await agent('time');
        const day = Number(save.match(/-day(\d+)$/)?.[1] ?? t?.result?.day_number ?? NaN);
        const cp: AgeRow = { time: new Date().toISOString(), save, day: Number.isFinite(day) ? day : null, folderMB: st ? Math.round(st.total / 10485.76) / 100 : null,
          saveMs: sv?.totalMs ?? null, saveResult: sv?.result ?? null, commitMB: mem?.commitMB ?? null, privateMB: mem?.privateMB ?? null, largestFreeMB: mem?.largestFreeMB ?? null,
          types: st ? [...st.stats].sort((a, b) => b[1].bytes - a[1].bytes).map(([k, v]) => `${k}=${v.bytes}/${v.count}`).join(';') : '' };
        row.checkpoints.push(cp);
        appendFileSync(csv, csvLine(cp) + '\n');
        log(`${j.id}: checkpoint ${save} day ${cp.day ?? '?'}: ${cp.folderMB ?? '?'} MB, save ${cp.saveMs ?? '?'} ms, memory ${memLine(mem)}`);
      };
      await checkpoint(s.replace(/\.sims3$/i, ''), false); // the loaded save: the starting point of the growth fit
      let chain = Promise.resolve();
      const r = await node(args!, logFile, deadline, l => { const n = savedName(l); if (n) chain = chain.then(() => checkpoint(n, true)).catch(e => log(`${j.id}: checkpoint ${n}: ${e}`)); });
      await chain;
      const final = row.checkpoints.some((c: AgeRow) => c.save === row.ageSave);
      Object.assign(row, { exitCode: r.code, timedOut: r.timedOut });
      row.verdict = [`${r.code === 0 && final ? 'PASS' : 'FAIL'}: age ${j.run.age.days} days${final ? '' : ', no final save'}: ${summarize(row.checkpoints)}`];
    } else if (args) {
      const r = await node(args, logFile, deadline);
      Object.assign(row, { exitCode: r.code, verdict: verdicts(r.out), timedOut: r.timedOut });
      if (j.run.profile) row.verdict = [`${r.code === 0 ? 'PASS' : 'FAIL'}: profile ${j.run.profile.minutes} min, ${j.run.profile.seed !== undefined ? `seed ${j.run.profile.seed}` : 'unseeded'} (rng-compare.ts)`];
    } else if (j.run.soak) {
      await agent('autopilot', { enabled: true, speed: j.run.soak.speed ?? 'ultra', dialogs: true });
      const wd = await node([H('watchdog.ts'), '--every', '1', '--port', values.port!], logFile, Math.min(deadline, Date.now() + j.run.soak.minutes * 60000));
      const ranOut = wd.timedOut; // the watchdog only exits on its own with a diagnosis
      row.exitCode = ranOut ? 0 : 1;
      row.verdict = [ranOut ? `PASS: soak ${j.run.soak.minutes} min, watchdog quiet` : `FAIL: watchdog: ${wd.out.trim().split(/\r?\n/).at(-1) ?? `exit ${wd.code}`}`];
    } else if (j.run.routelock) {
      await agent('autopilot', { enabled: true, speed: j.run.routelock.speed ?? 'ultra', dialogs: true });
      const st = await get('routelock?start=1', 60000);
      if (!st?.hooked) throw new Error(`routelock start failed: ${JSON.stringify(st)?.slice(0, 300)}`); // byte check refused, or no probe
      const wd = await node([H('watchdog.ts'), '--every', '1', '--port', values.port!], logFile, Math.min(deadline, Date.now() + j.run.routelock.minutes * 60000));
      const rep = await get('routelock?stop=1', 60000); // detach the hooks before anything else
      writeFileSync(join(OUT, `${queueName}-${date}-${j.id}-routelock.json`), JSON.stringify(rep, null, 1));
      row.routelock = routelockSummary(rep);
      const g = rep?.g11;
      const lost = rep?.ring?.lost ?? 0; // events the probe's ring dropped: the g11 accounting is incomplete
      row.exitCode = wd.timedOut && g && !lost ? 0 : 1;
      row.verdict = [!wd.timedOut ? `FAIL: watchdog: ${wd.out.trim().split(/\r?\n/).at(-1) ?? `exit ${wd.code}`}`
        : !g ? `FAIL: /routelock report without g11: ${JSON.stringify(rep)?.slice(0, 200)}`
        : `${lost ? `FAIL: ring lost ${lost} of ${rep.ring.events + lost} events, numbers incomplete:` : 'PASS:'} routelock ${j.run.routelock.minutes} min: movable ${g.movableMsPerSec} ms/s of ${g.planMsPerSec} plan ms/s (${g.plans} plans), ` +
          `free ${g.freeShare}, unlocked gate entries ${g.unlockedGatePerSec}/s, hooks ${rep.acquireHooksPerSec}+${rep.gateHooksPerSec}/s; ${g11Gate(s, g.movableMsPerSec)}`];
    } else {
      row.exitCode = row.memory.before ? 0 : 1;
      row.verdict = [row.memory.before ? `menu memory after ${j.run.menuMemory?.waitS ?? 60} s${row.rngDraws ? ` (draws ${row.rngDraws.map((d: unknown) => typeof d === 'number' ? d.toFixed(6) : '?').join(' ')})` : ''}` : 'FAIL: /memory did not answer'];
    }
    row.memory.after = await get('memory');
    const c1 = await get('native');
    if (j.minCounters !== undefined) { row.nativeDump = join(OUT, `${queueName}-${date}-${j.id}-native.json`); writeFileSync(row.nativeDump, JSON.stringify({ before: c0, after: c1, maxReset: resetMax && Number(c0?.vfMaxReset) > 0 }, null, 1)); }
    row.counters ={ before: pickCounters(c0, sections), after: pickCounters(c1, sections) };
    row.counters.delta = delta(row.counters.before, row.counters.after);
  } catch (e) { row.error = String((e as Error).message ?? e); log(`${j.id}: ${row.error}`); }
  jobGuard = null;
  if (stall && !row.error?.includes(stall)) row.error = [row.error, stall].filter(Boolean).join('; ');
  stall = '';
  row.crashed = launched && !gameRunning() || row.verdict.some((v: string) => /game died/.test(v));
  if (row.crashed) { row.crash = triage(); log(`${j.id}: game crashed\n${row.crash}`); }
  row.pass = row.exitCode === 0 && !row.crashed && !row.error && !row.timedOut;
  await closeGame(probe?.pid);
  // the game rewrites Bin/ts3native.log at every start: keep this job's copy (travel save lines, hook OK lines)
  if (launched && existsSync(LOG)) try { copyFileSync(LOG, row.nativeLog = join(OUT, `${queueName}-${date}-${j.id}-${t0.slice(11, 16).replace(':', '')}-ts3native.log`)); } catch (e) { log(`${j.id}: copy ts3native.log: ${e}`); }
  if (defaultIniOrig !== null) try { writeFileSync(DEFAULT_INI, defaultIniOrig); } catch (e) { stopWhy ||= `could not put back Bin/Default.ini (${e}; copy in probe-out/queue/bin-before)`; }
  if (s3ppIniOrig !== null) try { writeFileSync(S3PP_INI, s3ppIniOrig); } catch (e) { stopWhy ||= `could not put back Bin/Sims3Performance.ini (${e}; copy in probe-out/queue/bin-before)`; }
  if (s3ssTomlOrig !== null) try { writeFileSync(S3SS_TOML, s3ssTomlOrig); } catch (e) { stopWhy ||= `could not put back S3SS/S3SS.toml (${e}; copy in probe-out/queue/bin-before)`; }
  // undo installer changes (game closed, so files are not locked); state unknown when this fails, so stop
  for (const [k, undo] of [['install', 'uninstall'], ['uninstall', 'install']] as const) {
    if (!applied[k].length) continue;
    const r = await node([join(ROOT, 'tools', 'setup', 'install-fixes.ts'), undo, '--only', applied[k].join(',')], logFile, Date.now() + 600000);
    if (r.code !== 0) { stopWhy ||= `could not undo install-fixes ${k} ${applied[k].join(',')} (exit ${r.code})`; row.error = [row.error, stopWhy].filter(Boolean).join('; '); }
  }
  // content back (game closed above); the next cycle.ts --restart sees the Mods change and clears scriptCache
  if (stashed) {
    let why = '';
    try {
      const rs = gameRunning() ? { restored: 0, problems: ['TS3W.exe still running'] } : restoreContent(CONTENT_STASH);
      log(`${j.id}: content: ${rs.restored} packages moved back${rs.problems.length ? `; ${rs.problems.length} problems: ${rs.problems.slice(0, 3).join('; ')}` : ''}`);
      if (rs.problems.length) why = `content restore incomplete (${rs.problems.length} problems), manifest kept: ${manifestOf(CONTENT_STASH)}`;
      else if ((await node([fixesTool, 'install', '--only', 'cache-clear'], logFile, Date.now() + 600000)).code !== 0) why = 'install-fixes cache-clear failed after the content restore';
    } catch (e) { why = `content restore failed: ${String(e).split('\n')[0]}; run --restore-content`; }
    if (why) { stopWhy ||= why; row.error = [row.error, why].filter(Boolean).join('; '); }
  }
  row.end = new Date().toISOString();
  row.minutes = Math.round((Date.parse(row.end) - Date.parse(t0)) / 6000) / 10;
  appendFileSync(jsonl, JSON.stringify(row) + '\n');
  const cd = row.counters.delta ? Object.entries(row.counters.delta).map(([k, v]) => `${k} ${(v as number) > 0 ? '+' : ''}${v}`).join(', ') : '-';
  const cell = (x: string) => x.replace(/\|/g, '/').replace(/\r?\n/g, '<br>').slice(0, 400);
  appendFileSync(md, `| ${j.id} | ${t0.slice(11, 16)} (${pht(t0)}) | ${row.minutes} | ${row.pass ? 'PASS' : row.crashed ? '**CRASH**' : '**FAIL**'} | ${cell(row.verdict.join('<br>') || row.error || '-')} | ${row.exitCode ?? '-'} | ${memLine(row.memory.before)} -> ${memLine(row.memory.after)} | ${cell(cd || '-')} | ${cell([j.note, row.content !== 'as-is' ? `content ${row.content} (${row.contentMoved} moved)` : '', row.crashed ? row.crash.split('\n')[1]?.trim() : '', row.error && row.verdict.length ? row.error : ''].filter(Boolean).join('; ') || '')} |\n`);
  log(`${j.id}: ${row.pass ? 'PASS' : row.crashed ? 'CRASH' : 'FAIL'} in ${row.minutes} min`);
  if (!row.pass) exitCode = 1;

  const earlierCrashes = earlier.filter(r => r.job === j.id && r.crashed).length;
  stopWhy ||= crashStop(row.crashed, earlierCrashes, prevCrashed);
  prevCrashed = row.crashed;
  earlier.push(row);
  const next = jobs[n + 1];
  if (stopWhy) { log(`STOP: ${stopWhy}${next ? `; resume: node tools/harness/queue.ts ${file} --from ${next.id}` : ''}`); exitCode = 1; break; }
  // AI budget (best effort): pause at >= 80% in either window
  try {
    const first = execFileSync(process.execPath, [join(ROOT, 'tools', 'ai-usage.ts'), 'G', j.id], { cwd: ROOT }).toString().split(/\r?\n/)[0];
    const u = parseUsage(first);
    log(`ai usage: ${first}`);
    if (next && Math.max(u.h5 ?? 0, u.d7 ?? 0) >= 80) {
      log(`PAUSE: AI usage ${first}; resume: node tools/harness/queue.ts ${file} --from ${next.id}`);
      appendFileSync(md, `\nPaused after ${j.id} (AI usage ${first}); resume with \`--from ${next.id}\`.\n`);
      break;
    }
  } catch (e) { log(`ai-usage.ts failed (ignored): ${String(e).split('\n')[0]}`); }
}

// put back the tested build and the ini found at the start (game is closed after every job)
try {
  copyFileSync(existsSync(TESTED_ASI) ? TESTED_ASI : join(BEFORE, 'ts3native.asi'), join(BIN, 'ts3native.asi'));
  if (existsSync(join(BEFORE, 'ts3native.ini'))) copyFileSync(join(BEFORE, 'ts3native.ini'), join(BIN, 'ts3native.ini'));
  log(`restored: Bin/ts3native.asi = ${existsSync(TESTED_ASI) ? 'tested build' : 'copy from the start'}, ini from the start`);
} catch (e) { log(`restore failed: ${e} (copies in ${BEFORE})`); exitCode = 1; }
if (stopWhy) appendFileSync(md, `\nStopped: ${stopWhy}.\n`);
log(`results: ${jsonl}, ${md}`);
process.exit(exitCode);
