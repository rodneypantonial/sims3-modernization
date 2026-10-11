// Puts the game into a known state, in two halves so the probe server can run as its own long-lived process:
//   node tools/harness/cycle.ts --restart        detach probe, close game, install our packages, clear script cache
//   python tools/probe/ts3probe.py serve --launch   (start separately; it must outlive this script)
//   node tools/harness/cycle.ts --save X.sims3   wait for the agent, load the save, end in live mode
// Windows-only by nature (TS3W.exe, taskkill).
import { execFileSync } from 'node:child_process';
import { copyFileSync, mkdirSync, rmSync } from 'node:fs';
import { homedir } from 'node:os';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';

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
async function until(what: string, limitS: number, check: () => Promise<boolean>): Promise<void> {
  const t0 = Date.now();
  while (!(await check().catch(() => false))) {
    if (Date.now() - t0 > limitS * 1000) throw new Error(`${what}: not reached within ${limitS} s`);
    await sleep(3000);
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
  for (const p of ['ts3agent.package', 'ts3-nointro.package']) copyFileSync(join(ROOT, 'build', p), join(pkgs, p));
  rmSync(join(USER_DIR, 'scriptCache.package'), { force: true });
  log('game closed, packages installed, script cache cleared; now start: python tools/probe/ts3probe.py serve --launch');
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
  const r = await agent('load_save', { file: values.save });
  if (!r.ok) throw new Error(`load_save: ${r.error}`);
  // Loading can stop on a modal (e.g. "Mod Scripts Found"), so clear dialogs while waiting.
  await until('world loaded', 600, async () => {
    const d = await agent('auto_dialog');
    if (d.ok && d.result.open) log(`dialog "${d.result.title}" -> ${d.result.clicked}`);
    return (await agent('game_state')).result?.in_world === true;
  });
  // Settle: dialogs and notifications keep appearing for a few seconds after a load (new lots, lots in the
  // world, tutorials). Poll every second until 5 s pass without one (at most 30 s), then clear the corner.
  {
    const t0 = Date.now();
    let quietSince = Date.now();
    while (Date.now() - quietSince < 5000 && Date.now() - t0 < 30000) {
      const d = await agent('auto_dialog');
      if (d.ok && d.result.open) { log(`dialog "${String(d.result.title).slice(0, 70)}" -> ${d.result.clicked}${d.result.rule ? ' (rule)' : ''}`); quietSince = Date.now(); }
      await sleep(1000);
    }
    await agent('quiet_ui', { memories_off: true });
    await agent('decline_requests');
  }
  const s = await agent('game_state');
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
  log(`ready: ${JSON.stringify((await agent('game_state')).result)}`);
  // record the change set this run uses, so a later crash can be tied to it (crash-ledger.ts triage)
  log(execFileSync(process.execPath, [join(ROOT, 'tools', 'harness', 'crash-ledger.ts'), 'snapshot']).toString().trim());
}
