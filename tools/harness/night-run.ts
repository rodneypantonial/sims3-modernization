// Night run on a clean machine (Rodney, 2026-10-17): close the everyday apps that cost CPU (browsers, Slack, IntelliJ,
// Docker, ...), then start a test queue, so the runs measure the game and nothing else.
//
//   node tools/harness/night-run.ts                       report only: what runs, what would close, lock and idle state
//   node tools/harness/night-run.ts --close [--wait] [--queue clean-night.json] [--only a,b] [--force]
//
// Safety: nothing closes without --close (Claude passes it only when Rodney asks), and --close refuses unless the screen
// is locked (LogonUI.exe running) AND there was no keyboard/mouse input for --idle-min minutes (default 5). Apps get a
// normal close first (WM_CLOSE, like clicking X: browsers restore their tabs next start); --force kills what is still
// open after 30 s. Never touched: the game, Steam, Claude, terminals, shells, node/python (the harness itself).
// Windows only, like the rest of the harness. Self-check: --self-check.
import { execFileSync, spawnSync } from 'node:child_process';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';

const ROOT = resolve(import.meta.dirname, '..', '..');
const { values } = parseArgs({ options: {
  close: { type: 'boolean' }, force: { type: 'boolean' }, wait: { type: 'boolean' }, queue: { type: 'string' }, only: { type: 'string' }, from: { type: 'string' },
  'idle-min': { type: 'string', default: '5' }, 'self-check': { type: 'boolean' } } });

// app -> process names (without .exe). Edge covers Disney+ and Netflix in the browser; the Netflix store app runs as WWAHost.
export const APPS: Record<string, string[]> = {
  chrome: ['chrome'], edge: ['msedge'], firefox: ['firefox'], slack: ['slack'], teams: ['ms-teams', 'Teams'],
  discord: ['Discord'], spotify: ['Spotify'], netflix: ['WWAHost'], intellij: ['idea64'], vscode: ['Code'],
  docker: ['Docker Desktop', 'com.docker.backend', 'com.docker.build'], onedrive: ['OneDrive'],
  logi: ['logioptionsplus_agent', 'lghub_agent'],
};
// never closed, even if a name above ever matched one of them
export const PROTECTED = new Set(['TS3W', 'steam', 'steamwebhelper', 'claude', 'node', 'python', 'WindowsTerminal', 'OpenConsole',
  'powershell', 'pwsh', 'cmd', 'bash', 'conhost', 'explorer', 'dwm', 'LogonUI', 'frida-server']);

export const toClose = (running: string[]) => Object.entries(APPS)
  .map(([app, names]) => ({ app, procs: names.filter(n => running.includes(n) && !PROTECTED.has(n)) }))
  .filter(a => a.procs.length);

const ps = (script: string) => execFileSync('powershell', ['-NoProfile', '-NonInteractive', '-Command', script], { timeout: 30000 }).toString().trim();
const running = (): { name: string; cpu: number }[] => JSON.parse(ps(
  "Get-Process | Group-Object ProcessName | ForEach-Object { [pscustomobject]@{ name = $_.Name; cpu = [int](($_.Group | Measure-Object CPU -Sum).Sum) } } | ConvertTo-Json -Compress") || '[]');
const locked = () => ps("[bool](Get-Process LogonUI -ErrorAction SilentlyContinue)") === 'True';
const idleMin = () => Number(ps("Add-Type 'using System; using System.Runtime.InteropServices; public static class Idle { [StructLayout(LayoutKind.Sequential)] public struct L { public uint cb; public uint t; } [DllImport(\"user32.dll\")] static extern bool GetLastInputInfo(ref L l); public static uint Ms() { L l = new L(); l.cb = 8; GetLastInputInfo(ref l); return (uint)Environment.TickCount - l.t; } }'; [Idle]::Ms()")) / 60000;

if (values['self-check']) {
  const t = toClose(['chrome', 'msedge', 'claude', 'node', 'idea64', 'TS3W', 'Docker Desktop']);
  assert.ok(JSON.stringify(t.map(a => a.app)) === JSON.stringify(['chrome', 'edge', 'intellij', 'docker']), 'apps found');
  assert.ok(!t.some(a => a.procs.some(p => PROTECTED.has(p))), 'protected never closed');
  for (const n of Object.values(APPS).flat()) assert.ok(!PROTECTED.has(n), `listed app is protected: ${n}`);
  console.log('self-check ok');
  process.exit(0);
}

const procs = running(), names = procs.map(p => p.name);
const only = values.only?.split(',').map(s => s.trim());
const targets = toClose(names).filter(a => !only || only.includes(a.app));
const cpuOf = (n: string) => procs.find(p => p.name === n)?.cpu ?? 0;
const isLocked = locked(), idle = idleMin(), needIdle = Number(values['idle-min']);
console.log(`screen ${isLocked ? 'locked' : 'NOT locked'}, idle ${idle.toFixed(1)} min (need ${needIdle})`);
console.log(targets.length ? 'apps to close:' : 'no listed apps running');
for (const a of targets) console.log(`  ${a.app}: ${a.procs.map(p => `${p} (${cpuOf(p)} CPU s so far)`).join(', ')}`);
const java = procs.filter(p => p.name === 'java');
if (java.length) console.log(`note: java is running (${java.map(p => p.cpu).join(', ')} CPU s): IntelliJ, or builds from other sessions; not closed here`);

if (!values.close) { console.log('report only: add --close to close them (only when Rodney asks)'); process.exit(0); }
// --wait: poll every 30 s for up to 30 min until locked and idle long enough, instead of refusing at once
for (let t = 0; values.wait && t < 60 && !(locked() && idleMin() >= needIdle); t++) {
  if (t % 4 === 0) console.log(`waiting: screen ${locked() ? 'locked' : 'not locked'}, idle ${idleMin().toFixed(1)} min`);
  await new Promise(r => setTimeout(r, 30000));
}
if (!locked()) { console.log('refused: the screen is not locked'); process.exit(2); }
const idleNow = idleMin();
if (idleNow < needIdle) { console.log(`refused: input ${idleNow.toFixed(1)} min ago (someone may be using the laptop)`); process.exit(2); }

for (const a of targets) {
  if (a.app === 'docker') spawnSync('docker', ['desktop', 'stop'], { timeout: 60000, stdio: 'ignore' }); // its own clean stop
  for (const p of a.procs) spawnSync('taskkill', ['/IM', `${p}.exe`, '/T'], { stdio: 'ignore' }); // WM_CLOSE, like clicking X
}
await new Promise(r => setTimeout(r, 30000));
const left = toClose(running().map(p => p.name)).filter(a => targets.some(t => t.app === a.app));
if (left.length && values.force) for (const a of left) for (const p of a.procs) spawnSync('taskkill', ['/IM', `${p}.exe`, '/T', '/F'], { stdio: 'ignore' });
console.log(left.length ? `still open after 30 s: ${left.map(a => a.app).join(', ')}${values.force ? ' (killed with --force)' : ' (left alone; --force kills them)'}` : 'all closed');

if (values.queue) {
  const q = values.queue.includes('/') || values.queue.includes('\\') ? values.queue : join(ROOT, 'tools', 'harness', 'queues', values.queue);
  console.log(`starting queue ${q}`);
  const r = spawnSync(process.execPath, [join(ROOT, 'tools', 'harness', 'queue.ts'), q, ...(values.from ? ['--from', values.from] : [])], { stdio: 'inherit', env: { ...process.env, TS3_NIGHT: '1' } }); // night rule: a failed reused session restarts fresh (docs/170 §2)
  process.exit(r.status ?? 1);
}
