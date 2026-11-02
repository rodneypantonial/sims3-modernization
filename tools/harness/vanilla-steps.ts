// Harness steps for the vanilla queue (docs/236, queues/vanilla-matrix.json, run.script); each needs the probe server and a loaded world.
//   node tools/harness/vanilla-steps.ts accept --title "<lowercase fragment>" [--minutes 30] [--after 10] [--speed ultra]
//       H-move: autopilot on, dialog_expect {title, accept:true, rules:false} armed (re-armed when a window expires) until the agent itself accepts
//       a matching dialog, then the game must stay up --after minutes. No match in --minutes: UNKNOWN (exit 1), nothing else accepted.
//   node tools/harness/vanilla-steps.ts state-cmd     H-fault #2: buy_open, buy_close, enter_live_mode; the queue's mode guard must stay quiet
//   node tools/harness/vanilla-steps.ts kill-game [--after 60]   H-fault #1: kills the game; the queue must FAIL the job (never PASS)
//   node tools/harness/vanilla-steps.ts --self-check
// Accept and expiry lines come from the queue's click log (TS3_CLICKS_LOG, vanilla), else ts3native.log, else this script's own
// `autopilot` poll (run by hand, nobody else reads `cleared`).
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { parseArgs } from 'node:util';
import { GUARD_MS, stallGuard } from './game-alive.ts';
import { BIN, GAME_EXE } from './game-exe.ts';

const { values, positionals } = parseArgs({ allowPositionals: true, options: {
  title: { type: 'string' }, minutes: { type: 'string', default: '30' }, after: { type: 'string' }, speed: { type: 'string', default: 'ultra' },
  port: { type: 'string', default: '47891' }, 'self-check': { type: 'boolean' },
} });
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const log = (m: string) => console.log(`${new Date().toISOString().slice(11, 19)} ${m}`);
async function agent(cmd: string, args?: object): Promise<any> {
  const q = new URLSearchParams({ cmd, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  try { return await (await fetch(`http://127.0.0.1:${values.port}/agent?${q}`, { signal: AbortSignal.timeout(20000) })).json(); } catch (e) { return { ok: false, error: String(e) }; }
}

// pure (self-check): accept and expiry entries in click lines, in either form (`cleared` text or the ts3native `ap click:` line)
export const acceptLines = (lines: string[]) => lines.filter(l => /dialog_expect accept: /.test(l));
export const expiryLines = (lines: string[]) => lines.filter(l => /dialog_expect expired \(no match\)/.test(l));

let readOff = 0;
async function newClicks(): Promise<string[]> {
  const file = process.env.TS3_CLICKS_LOG || (existsSync(join(BIN, 'ts3native.log')) ? join(BIN, 'ts3native.log') : '');
  if (!file) return ((await agent('autopilot')).result?.cleared ?? []).map(String);
  if (!existsSync(file)) return [];
  const b = readFileSync(file), out = b.subarray(Math.min(readOff, b.length)).toString('latin1').split(/\r?\n/).filter(Boolean);
  readOff = b.length;
  return out;
}

async function accept() {
  const title = values.title?.toLowerCase(), minutes = Number(values.minutes), after = Number(values.after ?? 10);
  if (!title) throw new Error('accept needs --title');
  await newClicks(); // skip what is already there
  await agent('autopilot', { enabled: true, speed: values.speed, dialogs: true });
  const end = Date.now() + minutes * 60000, guard = stallGuard('accept wait', { port: Number(values.port) });
  let windows = 0, armedUntil = 0, got = '';
  while (!got && Date.now() < end) {
    if (Date.now() >= armedUntil) {
      const sec = Math.min(900, Math.ceil((end - Date.now()) / 1000));
      const r = await agent('dialog_expect', { title, accept: true, rules: false, seconds: sec }); // rules off: the move rule declines this offer
      if (!r.ok || r.result?.accept !== true) throw new Error(`dialog_expect accept not supported by this ts3agent: ${JSON.stringify(r).slice(0, 200)}`);
      armedUntil = Date.now() + sec * 1000; windows++;
      log(`dialog_expect armed: "${title}", accept, ${sec} s (window ${windows})`);
    }
    await sleep(5000);
    const c = await newClicks();
    for (const e of expiryLines(c)) log(`agent: ${e.replace(/^.*?dialog_expect/, 'dialog_expect')}`);
    got = acceptLines(c).at(-1) ?? '';
    const why = await guard();
    if (why) { console.log(`FAIL: ${why}`); process.exit(1); }
  }
  if (!got) { console.log(`UNKNOWN: accept: no dialog matching "${title}" in ${minutes} min (${windows} windows); nothing accepted`); process.exit(1); }
  log(`accepted: ${got.replace(/^.*?dialog_expect accept: /, '')}`);
  const g2 = stallGuard('after the accept', { port: Number(values.port) });
  for (const stop = Date.now() + after * 60000; Date.now() < stop;) {
    await sleep(5000);
    const why = await g2();
    if (why) { console.log(`FAIL: after the accept: ${why}`); process.exit(1); }
  }
  console.log(`PASS: accept: "${title}" accepted by the agent (${got.replace(/^.*?dialog_expect accept: /, '').slice(0, 160)}), game up ${after} min after`);
}

async function stateCmd() {
  for (const [cmd, waitS] of [['buy_open', 20], ['buy_close', 10], ['enter_live_mode', 90]] as const) {
    const r = await agent(cmd);
    log(`${cmd}: ${r.ok ? 'ok' : `error ${r.error}`}`);
    if (!r.ok) { console.log(`FAIL: state-cmd: ${cmd}: ${r.error}`); process.exit(1); }
    await sleep(waitS * 1000);
  }
  console.log(`PASS: state-cmd: harness state commands ran; the mode guard must not have stopped this job (${3 * GUARD_MS / 1000} s of checks)`);
}

async function killGame() {
  await sleep(Number(values.after ?? 60) * 1000);
  log(`fault: taskkill /F /IM ${GAME_EXE}`);
  try { execFileSync('taskkill', ['/F', '/IM', GAME_EXE], { stdio: 'ignore' }); } catch { /* already gone */ }
  await sleep(150000); // the queue's jobGuard ends this child within ~60 s
  console.log('FAIL: kill-game: the queue did not stop this job within 150 s of the kill');
  process.exit(1);
}

if (values['self-check']) {
  const lines = ['12:00:01 dialog_expect accept: There are new towns available -> #2 yes (accept requested)', 'ap click: x -> dialog_expect accept: #2 yes (accept requested)',
    '12:00:02 dialog_expect expired (no match): new towns available [accept]', '12:00:03 Hot Dogs -> #3 cancel (safe default)'];
  console.assert(acceptLines(lines).length === 2 && expiryLines(lines).length === 1, 'accept/expiry line match');
  if (acceptLines(lines).length !== 2 || expiryLines(lines).length !== 1) process.exit(1);
  console.log('vanilla-steps.ts self-check OK');
} else if (positionals[0] === 'accept') await accept();
else if (positionals[0] === 'state-cmd') await stateCmd();
else if (positionals[0] === 'kill-game') await killGame();
else { console.error('usage: vanilla-steps.ts accept --title T [--minutes 30] [--after 10] | state-cmd | kill-game [--after 60] | --self-check'); process.exit(2); }
