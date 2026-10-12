// Puts the game into a known state, in two halves so the probe server can run as its own long-lived process:
//   node tools/harness/cycle.ts --restart        detach probe, close game, install our packages, clear script cache
//   python tools/probe/ts3probe.py serve --launch   (start separately; it must outlive this script)
//   node tools/harness/cycle.ts --save X.sims3   wait for the agent, load the save, end in live mode
// Windows-only by nature (TS3W.exe, taskkill).
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { focusGame } from './focus-game.ts';

const { values } = parseArgs({ options: { save: { type: 'string' }, restart: { type: 'boolean' }, port: { type: 'string', default: '47891' } } });
const ROOT = resolve(import.meta.dirname, '..', '..');
const USER_DIR = process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3');
const base = `http://127.0.0.1:${values.port}`;
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const log = (m: string) => console.log(`${stamp()} ${m}`);

async function get(path: string, timeoutMs = 30000): Promise<any> {
  const r = await fetch(`${base}/${path}`, { signal: AbortSignal.timeout(timeoutMs) });
  return r.json();
}
async function agent(cmd: string, args?: object): Promise<any> {
  const q = new URLSearchParams({ cmd, timeout: '5000' });
  if (args) q.set('args', JSON.stringify(args));
  return get(`agent?${q}`);
}
async function until(what: string, limitS: number, check: () => Promise<boolean>, everyMs = 1000): Promise<void> {
  const t0 = Date.now();
  while (!(await check().catch(() => false))) {
    if (Date.now() - t0 > limitS * 1000) throw new Error(`${what}: not reached within ${limitS} s`);
    await sleep(everyMs);
  }
  log(`${what} after ${Math.round((Date.now() - t0) / 1000)} s`);
}
const gameRunning = () => execFileSync('tasklist', ['/NH', '/FI', 'IMAGENAME eq TS3W.exe']).toString().includes('TS3W.exe');

if (values.restart) {
  // stop cleanly: detaching first matters, killing a host mid-call can take the game down with it
  await get('detach', 10000).catch(() => {});
  if (gameRunning()) {
    try { execFileSync('taskkill', ['/F', '/IM', 'TS3W.exe'], { stdio: 'ignore' }); } catch { /* already gone */ }
    await until('game closed', 60, async () => !gameRunning());
  }
  // scriptCache holds compiled mods, so a stale one would run the old agent
  const pkgs = join(USER_DIR, 'Mods', 'Packages');
  mkdirSync(pkgs, { recursive: true });
  // rebuild first: a plugin change that was only hot-reloaded would otherwise be lost on relaunch
  execFileSync(process.execPath, [join(ROOT, 'tools', 'harness', 'build-agent.ts')], { stdio: 'ignore' });
  // the player mod with the fixes (mods/ts3fixes); the agent only reads and switches it
  execFileSync(process.execPath, [join(ROOT, 'tools', 'setup', 'build-fixes.ts')], { stdio: 'ignore' });
  // the game does not notice changed script mods, but rebuilding the cache costs startup time: clear it only when the
  // agent source or the Mods folder changed (the old csc builds differ byte-wise every time, so hash the inputs)
  const stampFile = join(ROOT, 'probe-out', 'scriptcache.stamp');
  const listing = (dir: string): string[] => readdirSync(dir, { recursive: true }).map(String).sort()
    .map(n => { const st = statSync(join(dir, n)); return st.isFile() ? `${n}:${st.size}:${st.mtimeMs}` : ''; });
  const stamp = createHash('sha256').update(listing(join(ROOT, 'mods', 'ts3agent', 'src')).join('|'))
    .update(listing(join(ROOT, 'mods', 'ts3fixes', 'src')).join('|'))
    .update(listing(join(USER_DIR, 'Mods')).filter(n => !/^Packages[\\/](ts3agent|ts3-nointro|ts3fixes)\.package/.test(n)).join('|')).digest('hex');
  const unchanged = existsSync(stampFile) && readFileSync(stampFile, 'utf8') === stamp && existsSync(join(pkgs, 'ts3agent.package'));
  if (!unchanged) {
    for (const p of ['ts3agent.package', 'ts3-nointro.package', 'ts3fixes.package']) copyFileSync(join(ROOT, 'build', p), join(pkgs, p));
    rmSync(join(USER_DIR, 'scriptCache.package'), { force: true });
    mkdirSync(dirname(stampFile), { recursive: true });
    writeFileSync(stampFile, stamp);
  }
  log(`game closed, ${unchanged ? 'agent and mods unchanged, script cache kept' : 'packages installed, script cache cleared'}; now start: python tools/probe/ts3probe.py serve --launch`);
}

// load a save, ending in live mode (a save can come back in Edit Town, which pauses the clock)
if (values.save) {
  await until('agent answering', 300, async () => (await agent('ping')).ok === true);
  // "Mod Scripts Found" (native, script mods present) opens at the menu up to ~90 s after the agent answers.
  // Accepting it during a load leaves the game stuck on the loading screen, so clear it first and wait for quiet.
  let quietSince = Date.now(), sawWarning = false;
  const t0 = Date.now();
  while (Date.now() - t0 < 150000 && (!sawWarning ? Date.now() - t0 < 120000 : Date.now() - quietSince < 5000)) {
    const d = await agent('auto_dialog');
    if (d.ok && d.result.open) {
      log(`menu dialog "${d.result.title}" -> ${d.result.clicked}`);
      if (/mod scripts/i.test(d.result.title ?? '')) sawWarning = true;
      quietSince = Date.now();
    }
    await sleep(2000);
  }
  if (!sawWarning) log('no "Mod Scripts Found" warning within 120 s; loading anyway');
  // in-game autopilot clears popups within a frame once in a world (load, Edit Town, live); speed left alone
  await agent('autopilot', { enabled: true, speed: 'none', dialogs: true });
  const r = await agent('load_save', { file: values.save });
  if (!r.ok) throw new Error(`load_save: ${r.error}`);
  // The agent sets the mailbox's world-loaded flag in OnWorldLoadFinished; /agentstatus reads it without a round trip.
  // Dialogs once in the world are the autopilot's job; every 10 s also try a modal that blocks the load itself.
  let waits = 0;
  await until('world loaded', 600, async () => {
    if ((await get('agentstatus', 5000)).worldLoaded === true) return true;
    if (++waits % 20 === 0) { const d = await agent('auto_dialog'); if (d.ok && d.result.open) log(`dialog "${d.result.title}" -> ${d.result.clicked}`); }
    return false;
  }, 500);
  // Settle: dialogs and notifications keep appearing for a few seconds after a load (new lots, lots in the
  // world, tutorials). Poll every second until 2 s pass without one (at most 30 s), then clear the corner.
  {
    const t0 = Date.now();
    let quietSince = Date.now();
    while (Date.now() - quietSince < 2000 && Date.now() - t0 < 30000) { // the autopilot clears dialogs within a frame
      const ap = await agent('autopilot');
      for (const c of ap.ok ? ap.result.cleared : []) { log(`dialog ${String(c).slice(0, 80)}`); quietSince = Date.now(); }
      await sleep(1000);
    }
    await agent('quiet_ui', { memories_off: true });
    await agent('decline_requests');
  }
  // right after OnWorldLoadFinished game_state can still throw (world objects not set up); wait for a real answer
  let s: any;
  await until('world state', 60, async () => (s = await agent('game_state')).ok && /State$/.test(s.result?.sub_state ?? ''));
  if (s.result?.sub_state !== 'LiveModeState') {
    log(`in ${s.result?.sub_state}; entering live mode`);
    await agent('enter_live_mode');
    // leaving Edit Town asks "save your town before continuing?"; clear it or the switch never happens
    await until('live mode', 120, async () => {
      const d = await agent('auto_dialog');
      if (d.ok && d.result.open) log(`dialog "${d.result.title}" -> ${d.result.clicked}`);
      return (await agent('game_state')).result?.sub_state === 'LiveModeState';
    });
  }
  log(`ready: ${JSON.stringify((await agent('game_state')).result)}; window ${focusGame() ? 'focused' : 'NOT focused'}`);
  // record the change set this run uses, so a later crash can be tied to it (crash-ledger.ts triage)
  log(execFileSync(process.execPath, [join(ROOT, 'tools', 'harness', 'crash-ledger.ts'), 'snapshot']).toString().trim());
}
