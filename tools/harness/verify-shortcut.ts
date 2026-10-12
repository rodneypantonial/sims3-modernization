// Quick check of a trip shortcut on a loaded world (much shorter than a profile run):
//   node tools/harness/verify-shortcut.ts --name rabbithole [--tries 20] [--minutes 5]
// Runs at x3 with popups cleared and checks every 30 s: shortcut counters (patches command: <name>_shortcuts,
// <name>_no_spot) and stuck Sims as watchdog.ts judges them, marking Sims a shortcut recently placed. Passes once the
// shortcut has fired --tries times with no stuck teleported Sim and at least 2 min elapsed; fails on a stuck teleported
// Sim or when --minutes run out. Exit code 0 = pass. Needs the probe server and a world in live mode.
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: { name: { type: 'string' }, tries: { type: 'string', default: '20' }, minutes: { type: 'string', default: '5' } } });
if (!values.name) throw new Error('--name required (gohome, rabbithole, ...)');
const name = values.name, tries = Number(values.tries), limit = Number(values.minutes) * 60000;
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const ag = async (cmd: string, args?: object): Promise<any> => {
  const q = new URLSearchParams({ cmd, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  try { return await (await fetch(`http://127.0.0.1:47891/agent?${q}`, { signal: AbortSignal.timeout(20000) })).json(); } catch (e) { return { ok: false, error: String(e) }; }
};

await ag('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
const start = (await ag('patches')).result ?? {};
const base = { shortcuts: start[`${name}_shortcuts`] ?? 0, noSpot: start[`${name}_no_spot`] ?? 0 };
const seen = new Map<string, { key: string; checks: number; reported: boolean }>();
const stuck: string[] = [];
const t0 = Date.now();
let verdict = 'FAIL: not enough shortcut tries within the time limit';
while (Date.now() - t0 < limit) {
  await sleep(30000);
  const p = (await ag('patches')).result ?? {};
  const recent: string[] = p.recent ?? [];
  const sims = (await ag('sims_activity')).result ?? [];
  for (const s of sims) {
    const key = `${s.x},${s.z},${s.interaction},${s.routing}`;
    const prev = seen.get(s.id);
    const e = prev && prev.key === key ? { key, checks: prev.checks + 1, reported: prev.reported } : { key, checks: 1, reported: false };
    seen.set(s.id, e);
    if (!e.reported && s.interaction && e.checks >= (s.routing ? 3 : 6)) {
      e.reported = true;
      const teleported = recent.some(l => l.startsWith(s.name + ' -> ') || l.startsWith(s.name + ' => '));
      stuck.push(`${s.name}${teleported ? ' [teleported]' : ''} on ${s.interaction}`);
    }
  }
  const n = (p[`${name}_shortcuts`] ?? 0) - base.shortcuts, noSpot = (p[`${name}_no_spot`] ?? 0) - base.noSpot;
  console.log(`${stamp()} ${name}: ${n} shortcuts, ${noSpot} without a safe spot; stuck: ${stuck.length ? stuck.join('; ') : 'none'}`);
  if (stuck.some(s => s.includes('[teleported]'))) { verdict = 'FAIL: a teleported Sim is stuck'; break; }
  if (n >= tries && Date.now() - t0 >= 120000) { verdict = 'PASS'; break; }
}
console.log(`${stamp()} ${verdict}; recent: ${((await ag('patches')).result?.recent ?? []).slice(-6).join(' | ')}`);
process.exit(verdict === 'PASS' ? 0 : 1);
