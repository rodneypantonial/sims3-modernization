// World-wide stuck-Sim scanner, run alongside repros: the game simulates the whole world while a repro uses one corner,
// so every other stuck Sim is a new case to reproduce and fix next.
//   node tools/harness/stuck-scan.ts [--minutes 30]
// Every 30 s it checks all Sims (same rule as watchdog.ts: same spot and interaction for 6 checks, or routing without
// moving for 3), skips Sims an active repro is using (probe-out/repro-active.json, written by repro.ts), prints one
// line per new stuck Sim (for a Monitor) and appends a draft repro entry to probe-out/repro-candidates.json:
// place_sim at its spot plus the trigger action that matches what it was doing (sim_go_home, sim_visit_lot, ...).
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { mayBeStuck } from './stuck-rule.ts';
import { watchGame } from './game-alive.ts';

const { values } = parseArgs({ options: { minutes: { type: 'string', default: '30' } } });
const ROOT = resolve(import.meta.dirname, '..', '..');
const OUT = join(ROOT, 'probe-out');
mkdirSync(OUT, { recursive: true });
const ACTIVE = join(OUT, 'repro-active.json'), CANDIDATES = join(OUT, 'repro-candidates.json');
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const ag = async (cmd: string, args?: object): Promise<any> => {
  const q = new URLSearchParams({ cmd, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  try { return await (await fetch(`http://127.0.0.1:47891/agent?${q}`, { signal: AbortSignal.timeout(20000) })).json(); } catch (e) { return { ok: false, error: String(e) }; }
};

// the trigger action that recreates what the Sim was doing (extend with every new repro action)
function trigger(s: any): object | null {
  const i = String(s.interaction ?? '');
  if (/^Go Home$/i.test(i)) return { cmd: 'sim_go_home', args: { sim: s.name } };
  const visit = /^Visit (?:the )?(.+?)(?: household)?$/i.exec(i);
  if (visit) return { cmd: 'sim_visit_lot', args: { sim: s.name, lot: visit[1] }, optional: true };
  return null;
}

const seen = new Map<string, { key: string; checks: number; reported: boolean }>();
const t0 = Date.now();
let world = '';
console.log(`${stamp()} stuck-scan started`);
watchGame();
while (Date.now() - t0 < Number(values.minutes) * 60000) {
  await sleep(30000);
  const active = new Set<string>(existsSync(ACTIVE) ? JSON.parse(readFileSync(ACTIVE, 'utf8')).sims ?? [] : []);
  const r = await ag('sims_activity');
  if (!r.ok) continue;
  world ||= (await ag('game_state')).result?.save_name ?? '';
  const moved = (await ag('patches')).result?.moved ?? {};
  for (const s of r.result) {
    if (active.has(s.name)) continue;
    const key = `${s.x},${s.z},${s.interaction},${s.routing}`;
    const p = seen.get(s.id);
    const e = p && p.key === key ? { key, checks: p.checks + 1, reported: p.reported } : { key, checks: 1, reported: false };
    seen.set(s.id, e);
    if (e.reported || !mayBeStuck(s.interaction, !!s.routing) || e.checks < (s.routing ? 3 : 6)) continue;
    e.reported = true;
    console.log(`${stamp()} NEW STUCK ${s.name} on "${s.interaction}" at ${s.lot ?? 'street'} (${s.x}, ${s.z})${s.routing ? ' routing' : ''}${moved[s.name] ? ` [moved: ${moved[s.name]}]` : ''}`);
    const list = existsSync(CANDIDATES) ? JSON.parse(readFileSync(CANDIDATES, 'utf8')) : [];
    const t = trigger(s);
    list.push({
      id: `cand-${s.name.toLowerCase().replace(/\W+/g, '-')}-${stamp().replace(/:/g, '')}`,
      bug: `${s.name} stuck on "${s.interaction}" at ${s.lot ?? 'street'}${s.routing ? ' (routing)' : ''}${moved[s.name] ? `; moved by a shortcut: ${moved[s.name]}` : ''}`,
      fix: 'to investigate', save: world.replace(/\.sims3$/, '') || 'Baseline-BP', area: [s.x, s.z],
      steps: [{ cmd: 'place_sim', args: { sim: s.name, x: s.x, z: s.z } }, ...(t ? [t] : [])],
      watch: { kind: 'stuck', sims: [s.name], minutes: 5 }, expect: 'the Sim finishes or gives up (not stuck)',
    });
    writeFileSync(CANDIDATES, JSON.stringify(list, null, 2));
  }
}
console.log(`${stamp()} stuck-scan done`);
