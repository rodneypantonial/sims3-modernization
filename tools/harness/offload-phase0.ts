// Offload phase 0 (docs/08 §7): how much address space the engine's ResourceCache takes, with and without CC
// (store-*: CC plus the Store Updates repacks, a heavy store-content load, test-only), at CacheBudget 200, 50 and 0 MB. Per variant: close the game, switch CC and the budget with the installer,
// clear caches, launch under the probe, load the save, run N minutes at speed 3 (in-game autopilot holds the speed
// and clears popups), and sample /rescache + /memory every 10 s into probe-out/phase0/<stamp>/<variant>.csv.
// Each variant is recorded in the crash ledger. At the end CC and the budget are uninstalled and the game closed.
//   node tools/harness/offload-phase0.ts [--save Baseline-SV.sims3] [--minutes 15] [--only nocc-200,cc-50] [--dry-run]
// Windows-only by nature (the game, taskkill via cycle.ts).
import { execFileSync, spawn } from 'node:child_process';
import { appendFileSync, mkdirSync, openSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import assert from 'node:assert';

const { values } = parseArgs({ options: {
  save: { type: 'string', default: 'Baseline-SV.sims3' }, minutes: { type: 'string', default: '15' },
  only: { type: 'string' }, port: { type: 'string', default: '47891' }, 'dry-run': { type: 'boolean' },
} });
const ROOT = resolve(import.meta.dirname, '..', '..');
const base = `http://127.0.0.1:${values.port}`;
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const log = (m: string) => console.log(`${stamp()} ${m}`);

const ALL = ['nocc-200', 'nocc-50', 'nocc-0', 'cc-200', 'cc-50', 'cc-0', 'store-200', 'store-50', 'store-0'];
const variants = values.only ? values.only.split(',').map(s => s.trim()) : ALL;
for (const v of variants) assert(ALL.includes(v), `unknown variant '${v}' (one of ${ALL.join(', ')})`);
const parse = (v: string) => ({ cc: !v.startsWith('nocc-'), store: v.startsWith('store-'), mb: Number(v.split('-')[1]) });
assert.deepEqual(parse('cc-50'), { cc: true, store: false, mb: 50 });
assert.deepEqual(parse('nocc-0'), { cc: false, store: false, mb: 0 });
assert.deepEqual(parse('store-200'), { cc: true, store: true, mb: 200 });

const node = (script: string, ...args: string[]) => {
  const cmd = [join(ROOT, script), ...args];
  if (values['dry-run']) { log(`would run: node ${cmd.join(' ')}`); return ''; }
  return execFileSync(process.execPath, cmd, { encoding: 'utf8' }).trim();
};
async function get(path: string, timeoutMs = 30000): Promise<any> {
  const r = await fetch(`${base}/${path}`, { signal: AbortSignal.timeout(timeoutMs) });
  return r.json();
}
async function agent(cmd: string, args?: object): Promise<any> {
  const q = new URLSearchParams({ cmd, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  return get(`agent?${q}`).catch(e => ({ ok: false, error: String(e) }));
}

const out = join(ROOT, 'probe-out', 'phase0', new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19));
if (!values['dry-run']) mkdirSync(out, { recursive: true });
const MB = (b: number | null | undefined) => (b == null ? '' : (b / 1048576).toFixed(1));
const cols = ['time', 'elapsedS', 'simMinutes', 'speed', 'commitMB', 'largestFreeMB', 'privateMB',
  'cacheBudgetMB', 'cacheBytesMB', 'cacheFlushMB', 'cacheEntries', 'animBytesMB', 'error'];

for (const v of variants) {
  const { cc, store, mb } = parse(v);
  log(`=== ${v}: CC ${cc ? 'on' : 'off'}, store repacks ${store ? 'on' : 'off'}, CacheBudget ${mb} MB`);
  node('tools/harness/cycle.ts', '--restart');
  node('tools/setup/install-fixes.ts', cc ? 'install' : 'uninstall', '--only', 'user-cc');
  node('tools/setup/install-fixes.ts', store ? 'install' : 'uninstall', '--only', 'store-repacks-test');
  node('tools/setup/install-fixes.ts', 'install', '--only', 'cache-budget', '--cache-budget-mb', String(mb));
  node('tools/setup/install-fixes.ts', 'install', '--only', 'cache-clear'); // CAS/compositor caches must match the CC set
  if (values['dry-run']) { log(`would launch the probe, load ${values.save}, sample ${values.minutes} min`); continue; }

  // the probe server must outlive this step, as with cycle.ts: its own detached process
  const probeLog = openSync(join(out, `${v}-probe.log`), 'a');
  spawn('python', [join(ROOT, 'tools', 'probe', 'ts3probe.py'), 'serve', '--launch', '--port', values.port],
    { detached: true, stdio: ['ignore', probeLog, probeLog], cwd: ROOT }).unref();
  const tLoad = Date.now();
  try {
    log(node('tools/harness/cycle.ts', '--save', values.save).split('\n').at(-1)!);
    log(`${v}: launch to live mode ${Math.round((Date.now() - tLoad) / 1000)} s`);
  }
  catch (e) { log(`load failed, next variant: ${(e as Error).message.split('\n')[0]}`); continue; }
  node('tools/harness/crash-ledger.ts', 'snapshot', '--note', `offload phase0 ${v}`);

  const csv = join(out, `${v}.csv`);
  appendFileSync(csv, cols.join(',') + '\n');
  await agent('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
  const t0 = Date.now();
  let failures = 0, peak = 0;
  for (let i = 0; Date.now() - t0 < Number(values.minutes) * 60000; i++) {
    const row: Record<string, string | number> = { time: stamp(), elapsedS: Math.round((Date.now() - t0) / 1000) };
    try {
      const [m, rc, t] = await Promise.all([get('memory'), get('rescache'), agent('time')]);
      Object.assign(row, { commitMB: m.commitMB, largestFreeMB: m.largestFreeMB, privateMB: m.privateMB });
      if (t.ok) Object.assign(row, { simMinutes: t.result.minutes_total, speed: t.result.speed });
      if (rc.ok) {
        // priority 0 = the generic ResourceCache, 1000 = AnimationCache (0x738670)
        const gen = rc.caches.find((c: any) => c.known && c.priority === 0), anim = rc.caches.find((c: any) => c.known && c.priority === 1000);
        Object.assign(row, { cacheBudgetMB: MB(gen?.budget), cacheBytesMB: MB(gen?.bytes), cacheFlushMB: MB(gen?.flushBytes),
          cacheEntries: gen?.entries ?? '', animBytesMB: MB(anim?.bytes) });
        peak = Math.max(peak, gen?.bytes ?? 0);
        // a variant whose budget the game did not pick up measures nothing new (2026-10-11: Default.ini edit ignored)
        if (i === 0 && gen && gen.budget !== mb * 1048576) { log(`${v}: game reports budget ${MB(gen.budget)} MB, not ${mb}; skipping variant`); break; }
      } else row.error = rc.error;
      failures = 0;
    } catch (e) {
      row.error = String(e).replace(/,/g, ';');
      if (++failures >= 6) { log('probe/game not answering for a minute; triage and next variant'); log(node('tools/harness/crash-ledger.ts', 'triage')); break; }
    }
    appendFileSync(csv, cols.map(c => row[c] ?? '').join(',') + '\n');
    if (i % 3 === 0) { await agent('keep_alive', { threshold: -50 }); await agent('decline_requests'); }
    await sleep(10000);
  }
  await agent('autopilot', { enabled: false });
  await agent('set_speed', { speed: 'pause' });
  log(`${v}: peak ResourceCache ${MB(peak)} MB; samples in ${csv}`);
}

log('restoring: closing the game, removing CC and the budget change');
node('tools/harness/cycle.ts', '--restart');
node('tools/setup/install-fixes.ts', 'uninstall', '--only', 'user-cc,store-repacks-test,cache-budget');
node('tools/setup/install-fixes.ts', 'install', '--only', 'cache-clear');
log(`done${values['dry-run'] ? ' (dry run)' : `; results in ${out}`}`);
