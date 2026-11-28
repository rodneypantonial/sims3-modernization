// Queue job key atlasSet (Boundless Atlas generated test sets, docs/026 §9.5 there): the setup a set needs before a job loads it.
//   atlasSet: { world: "game-copy/worlds/p0/<Block>.world", sha256: "<64 hex>", set: "<Set>", sea?: <m>, stage: "game-copy/worlds/p0/sets/<Set>" }
// stage: Boundless Atlas `world-stitch.ts split <world> --set <Set> --root <stage> --staging` (a root the game never reads, so it
//   runs while a game job runs) plus `strip-pack.ts pack`, both offline; the stage has the Documents layout
//   (InstalledWorlds/<Set>*.world, ts3native/atlas/sets/<Set>/<Set>.atlas.json + .strip.package). Skipped when the stage exists.
// install (queue.ts, game closed, before the launch): copies the stage into the user folder with mtimes kept (the manifest
//   stamps size + mtime). A file already there with the same bytes is skipped; a different one refuses (an older set of that
//   name: remove it by hand). Installed files stay, as the AtlasStripSet ones do.
// Boundless Atlas checkout: ATLAS_ROOT, else the first boundless-atlas next to this checkout or one of its parents.
// Usage: node tools/harness/atlas-set.ts stage <queue.json>       builds every job's stage that is missing (offline)
//        node tools/harness/atlas-set.ts plan <queue.json>        what install would copy into the user folder
//        node tools/harness/atlas-set.ts --self-check
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync, utimesSync, writeFileSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { dirname, join, relative, resolve } from 'node:path';

export type AtlasSet = { world: string; sha256: string; set: string; sea?: number; stage: string };
const ROOT = resolve(import.meta.dirname, '..', '..');
const SET_RE = /^[A-Za-z0-9][A-Za-z0-9-]{0,47}$/, REL_RE = /^game-copy\/[A-Za-z0-9_./-]+$/;

export function checkAtlasSet(a: unknown, at: string): AtlasSet {
  const o = a as AtlasSet;
  if (!o || typeof o !== 'object') throw new Error(`${at}: atlasSet must be an object`);
  for (const k of Object.keys(o)) if (!['world', 'sha256', 'set', 'sea', 'stage'].includes(k)) throw new Error(`${at}: atlasSet key ${k} unknown`);
  if (typeof o.world !== 'string' || !REL_RE.test(o.world) || !/\.world$/i.test(o.world) || o.world.includes('..')) throw new Error(`${at}: atlasSet.world must be game-copy/.../<name>.world`);
  if (typeof o.stage !== 'string' || !REL_RE.test(o.stage) || o.stage.includes('..')) throw new Error(`${at}: atlasSet.stage must be game-copy/...`);
  if (typeof o.set !== 'string' || !SET_RE.test(o.set)) throw new Error(`${at}: atlasSet.set must match ${SET_RE}`);
  if (typeof o.sha256 !== 'string' || !/^[0-9a-f]{64}$/.test(o.sha256)) throw new Error(`${at}: atlasSet.sha256 must be 64 lower-case hex`);
  if (o.sea !== undefined && !(typeof o.sea === 'number' && Number.isFinite(o.sea))) throw new Error(`${at}: atlasSet.sea must be a number`);
  return o;
}
export const setWorld = (a: AtlasSet) => `${a.set}.world`;
const stageDir = (a: AtlasSet, root = ROOT) => join(root, a.stage);
export const manifestOf = (a: AtlasSet, root = ROOT) => join(stageDir(a, root), 'ts3native', 'atlas', 'sets', a.set, `${a.set}.atlas.json`);
export const stageReady = (a: AtlasSet, root = ROOT) => existsSync(manifestOf(a, root)) && existsSync(join(dirname(manifestOf(a, root)), `${a.set}.strip.package`)) && existsSync(join(stageDir(a, root), 'InstalledWorlds', setWorld(a)));
// the staged main world, for the dry run's world check (a real file once staged)
export const stagedWorldPath = (a: AtlasSet, root = ROOT) => join(stageDir(a, root), 'InstalledWorlds', setWorld(a));

const sha = (f: string) => createHash('sha256').update(readFileSync(f)).digest('hex');
export function atlasRoot(): string {
  if (process.env.ATLAS_ROOT) return resolve(process.env.ATLAS_ROOT);
  for (let d = ROOT; ; d = dirname(d)) {
    const c = join(dirname(d), 'boundless-atlas');
    if (existsSync(join(c, 'tools', 'world-stitch.ts'))) return c;
    if (dirname(d) === d) throw new Error('no boundless-atlas checkout found (set ATLAS_ROOT)');
  }
}
// offline: split + strip pack into the stage; returns the log lines. run = the node runner (self-check fakes it)
export function stage(a: AtlasSet, o: { root?: string; atlas?: string; run?: (args: string[], cwd: string) => string } = {}): string[] {
  const root = o.root ?? ROOT, w = join(root, a.world), log: string[] = [];
  if (stageReady(a, root)) return [`atlasSet ${a.set}: stage ready at ${a.stage}`];
  if (!existsSync(w)) throw new Error(`atlasSet ${a.set}: block world ${a.world} missing`);
  const got = sha(w); if (got !== a.sha256) throw new Error(`atlasSet ${a.set}: ${a.world} sha256 ${got.slice(0, 12)}, pinned ${a.sha256.slice(0, 12)}`);
  if (existsSync(stageDir(a, root))) throw new Error(`atlasSet ${a.set}: ${a.stage} exists but is not a complete stage (remove it, then stage again)`);
  const atlas = o.atlas ?? atlasRoot(), run = o.run ?? ((args, cwd) => execFileSync(process.execPath, ['--max-old-space-size=8192', ...args], { cwd, encoding: 'utf8', timeout: 900000 }));
  log.push(run([join(atlas, 'tools', 'world-stitch.ts'), 'split', w, '--set', a.set, '--root', stageDir(a, root), '--sea', String(a.sea ?? 0), '--staging'], atlas).trim().split(/\r?\n/).slice(-1)[0]);
  log.push(run([join(atlas, 'tools', 'strip-pack.ts'), 'pack', manifestOf(a, root)], atlas).trim().split(/\r?\n/).slice(-1)[0]);
  if (!stageReady(a, root)) throw new Error(`atlasSet ${a.set}: split and pack ran, stage still incomplete`);
  return log.map(l => `atlasSet ${a.set}: ${l}`);
}
const walk = (d: string): string[] => readdirSync(d, { withFileTypes: true }).flatMap(e => e.isDirectory() ? walk(join(d, e.name)) : [join(d, e.name)]);
// files of the set only: InstalledWorlds/<Set>.world, <Set>_rXX_ZZ.world and the set folder
export function stagedFiles(a: AtlasSet, root = ROOT): string[] {
  const s = stageDir(a, root), re = new RegExp(`^${a.set}(_r\\d{2,}_\\d{2,})?\\.world$`, 'i');
  const worlds = readdirSync(join(s, 'InstalledWorlds')).filter(f => re.test(f)).map(f => join('InstalledWorlds', f));
  return [...worlds, ...walk(join(s, 'ts3native', 'atlas', 'sets', a.set)).map(f => relative(s, f))].sort();
}
export type Plan = { copy: string[]; same: string[]; differ: string[] };
export function installPlan(a: AtlasSet, user: string, root = ROOT): Plan {
  const p: Plan = { copy: [], same: [], differ: [] }, s = stageDir(a, root);
  for (const rel of stagedFiles(a, root)) {
    const from = join(s, rel), to = join(user, rel);
    if (!existsSync(to)) p.copy.push(rel);
    else if (statSync(to).size === statSync(from).size && sha(to) === sha(from)) p.same.push(rel);
    else p.differ.push(rel);
  }
  return p;
}
// game closed (the caller checks): copy what is missing, keep mtimes; a differing installed file refuses before any copy
export function install(a: AtlasSet, user: string, root = ROOT): Plan {
  if (!stageReady(a, root)) throw new Error(`atlasSet ${a.set}: no stage at ${a.stage} (node tools/harness/atlas-set.ts stage <queue>)`);
  const p = installPlan(a, user, root), s = stageDir(a, root);
  if (p.differ.length) throw new Error(`atlasSet ${a.set}: installed files differ from the stage: ${p.differ.slice(0, 3).join(', ')} (an older set of that name; remove it by hand); nothing copied`);
  for (const rel of p.copy) {
    const from = join(s, rel), to = join(user, rel), st = statSync(from);
    mkdirSync(dirname(to), { recursive: true }); copyFileSync(from, to); utimesSync(to, st.atime, st.mtime);
  }
  return p;
}
export const planLine = (a: AtlasSet, user: string, root = ROOT) => {
  if (!stageReady(a, root)) return `atlasSet ${a.set}: stage missing at ${a.stage}: the job stages it (split + strip pack from ${a.world}) before the launch`;
  const p = installPlan(a, user, root);
  return `atlasSet ${a.set}: stage ready; game closed, install ${p.copy.length} files, ${p.same.length} already installed${p.differ.length ? `, ** ${p.differ.length} DIFFER: the job refuses (${p.differ.slice(0, 2).join(', ')}) **` : ''}`;
};

export function selfCheck() {
  let n = 0; const ok = (c: boolean, m: string) => { assert.ok(c, m); n++; };
  const t = mkdtempSync(join(tmpdir(), 'atlas-set-'));
  try {
    const user = join(t, 'user'), w = join(t, 'game-copy', 'worlds', 'B.world'); mkdirSync(dirname(w), { recursive: true }); writeFileSync(w, 'block');
    const a = checkAtlasSet({ world: 'game-copy/worlds/B.world', sha256: sha(w), set: 'Syn', sea: 5, stage: 'game-copy/worlds/sets/Syn' }, 't');
    for (const bad of [{ ...a, world: '../x.world' }, { ...a, set: 'a_b' }, { ...a, sha256: 'AB' }, { ...a, extra: 1 }, { ...a, stage: 'C:/x' }]) { assert.throws(() => checkAtlasSet(bad, 't')); n++; }
    const calls: string[][] = [];
    const fake = (args: string[]) => { calls.push(args); const s = join(t, a.stage);
      if (args.includes('split')) { ok(args.includes('--staging') && args[args.indexOf('--sea') + 1] === '5', 'split runs with --staging and the sea level');
        mkdirSync(join(s, 'InstalledWorlds'), { recursive: true }); for (const f of ['Syn.world', 'Syn_r01_00.world', 'Other.world']) writeFileSync(join(s, 'InstalledWorlds', f), f);
        mkdirSync(dirname(manifestOf(a, t)), { recursive: true }); writeFileSync(manifestOf(a, t), '{}'); return 'manifest Syn.atlas.json: 2 regions'; }
      writeFileSync(join(dirname(manifestOf(a, t)), 'Syn.strip.package'), 'pack'); return 'written Syn.strip.package'; };
    assert.throws(() => stage({ ...a, sha256: '0'.repeat(64) }, { root: t, atlas: t, run: fake }), /pinned/); n++;
    const log = stage(a, { root: t, atlas: t, run: fake });
    ok(calls.length === 2 && log.length === 2 && /strip\.package/.test(log[1]), 'stage: split then pack');
    ok(stage(a, { root: t, atlas: t, run: fake })[0].includes('stage ready') && calls.length === 2, 'stage again: nothing run');
    ok(stagedFiles(a, t).join() === ['InstalledWorlds/Syn.world', 'InstalledWorlds/Syn_r01_00.world', 'ts3native/atlas/sets/Syn/Syn.atlas.json', 'ts3native/atlas/sets/Syn/Syn.strip.package'].map(p => p.replace(/\//g, process.platform === 'win32' ? '\\' : '/')).join(), 'staged files: the set only (Other.world left out)');
    ok(/install 4 files, 0 already/.test(planLine(a, user, t)), 'plan: 4 to copy');
    const old = new Date('2026-01-02T03:04:05Z'); utimesSync(join(t, a.stage, 'InstalledWorlds', 'Syn.world'), old, old);
    const p = install(a, user, t);
    ok(p.copy.length === 4 && readFileSync(join(user, 'InstalledWorlds', 'Syn.world'), 'utf8') === 'Syn.world', 'install copies');
    ok(statSync(join(user, 'InstalledWorlds', 'Syn.world')).mtimeMs === old.getTime(), 'install keeps the mtime the manifest stamped');
    ok(install(a, user, t).same.length === 4 && install(a, user, t).copy.length === 0, 'install again: all the same, nothing copied');
    writeFileSync(join(user, 'ts3native', 'atlas', 'sets', 'Syn', 'Syn.strip.package'), 'older');
    rmSync(join(user, 'InstalledWorlds', 'Syn_r01_00.world'));
    assert.throws(() => install(a, user, t), /differ from the stage: .*Syn\.strip\.package.*nothing copied/); n++;
    ok(!existsSync(join(user, 'InstalledWorlds', 'Syn_r01_00.world')), 'a refused install copies nothing');
    ok(/DIFFER: the job refuses/.test(planLine(a, user, t)), 'plan names the differing file');
    rmSync(join(t, a.stage), { recursive: true }); mkdirSync(join(t, a.stage));
    assert.throws(() => stage(a, { root: t, atlas: t, run: fake }), /not a complete stage/); n++;
    ok(/stage missing/.test(planLine(a, user, t)), 'plan: stage missing');
  } finally { rmSync(t, { recursive: true, force: true }); }
  console.log(`atlas-set self-check OK (${n} checks)`);
}

if (process.argv[1]?.endsWith('atlas-set.ts')) {
  const [cmd, q] = process.argv.slice(2);
  if (cmd === '--self-check' || !cmd) selfCheck();
  else if ((cmd === 'stage' || cmd === 'plan') && q) {
    const json = JSON.parse(readFileSync(q, 'utf8')), jobs = (Array.isArray(json) ? json : json.jobs) as { id: string; atlasSet?: unknown }[];
    const user = process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3');
    for (const j of jobs.filter(j => j.atlasSet)) {
      const a = checkAtlasSet(j.atlasSet, j.id);
      for (const l of cmd === 'stage' ? stage(a) : [planLine(a, user)]) console.log(`${j.id}: ${l}`);
    }
  } else { console.log('usage: node tools/harness/atlas-set.ts stage|plan <queue.json> | --self-check'); process.exit(2); }
}
