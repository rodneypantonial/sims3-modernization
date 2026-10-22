// Pass rule for the main-thread hitch repros (docs/169 §main thread): count presented frames > ms whose longest main-slot
// service (frame-times.js svc1, docs/167 §7) is the target, and optionally the [LotStream] lot adds (seconds.csv lotAdds).
// Unlike freeze-report.ts the repro step windows COUNT here (the repro is the cause we want); only the first minute and
// the profiler edge (worst-hitches.ts baseWins) are dropped.
//   node tools/harness/frame-lead.ts <tag> '{"service":"World/Service","ms":50,"min":20,"lotAdds":20}'
//   node tools/harness/frame-lead.ts --self-check
// profile-world.ts --frame-lead <same JSON> runs it at the end of the window (queue profile.frameLead) and prints PASS/FAIL.
// Set TS3_REPO=<main checkout> when running from a git worktree.
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { afterLoad, baseWins } from './worst-hitches.ts';

export type LeadRule = { service: string; ms?: number; min: number; lotAdds?: number };
type R = Record<string, string>;

export function parseRule(s: string): LeadRule {
  const r = JSON.parse(s);
  if (typeof r?.service !== 'string' || !r.service || !(Number(r.min) >= 1)) throw new Error(`frame lead rule needs {service, min >= 1}: ${s}`);
  for (const k of ['ms', 'lotAdds']) if (r[k] !== undefined && !(Number(r[k]) >= 0)) throw new Error(`frame lead rule: ${k} must be a number >= 0`);
  return r;
}

const csv = (text: string): R[] => {
  const [head, ...ls] = text.trim().split(/\r?\n/), c = head.split(',');
  return ls.filter(Boolean).map(l => Object.fromEntries(l.split(',').map((v, i) => [c[i], v])));
};

// seconds: stutter-profile.js rows (t = QPC ms); frames: frame-times.js rows (qpcMs, frameMs, svc1)
export function check(seconds: R[], frames: R[], rule: LeadRule, load = true) {
  const ms = rule.ms ?? 50, wins = baseWins(seconds.map(x => ({ t: Number(x.t) })), load);
  const real = (q: number) => !wins.some(w => w.a < q && w.b > q);
  const svc = frames.filter(f => f.svc1 !== undefined && f.svc1 !== 'na');
  const led = svc.filter(f => Number(f.frameMs) > ms && f.svc1 === rule.service && real(Number(f.qpcMs))).length;
  const hasLot = seconds.length > 0 && 'lotAdds' in seconds[0];
  const lotAdds = hasLot ? seconds.filter(x => real(Number(x.t))).reduce((t, x) => t + (Number(x.lotAdds) || 0), 0) : NaN;
  const fails: string[] = [];
  if (!svc.length) fails.push('no service columns in frames.csv (services split refused or frames off)');
  if (led < rule.min) fails.push(`${rule.service} led ${led} frames > ${ms} ms (need ${rule.min})`);
  if (rule.lotAdds !== undefined && !(lotAdds >= rule.lotAdds)) fails.push(hasLot ? `lot adds ${lotAdds} (need ${rule.lotAdds})` : 'no lotAdds column ([LotStream] Enabled=0?)');
  return { led, lotAdds, fails, line: `${rule.service} led ${led} frames > ${ms} ms${hasLot ? `, lot adds ${lotAdds}` : ''}` };
}

export function checkTag(dir: string, tag: string, rule: LeadRule) {
  const f = readdirSync(dir).find(n => n.startsWith(`${tag}-`) && /^\d+-frames\.csv$/.test(n.slice(tag.length + 1)));
  if (!f) return { led: 0, lotAdds: NaN, fails: [`no ${tag}-<secs>-frames.csv`], line: '' };
  const base = join(dir, f.replace(/-frames\.csv$/, '')), sf = `${base}-seconds.csv`;
  return check(existsSync(sf) ? csv(readFileSync(sf, 'utf8')) : [], csv(readFileSync(join(dir, f), 'utf8')), rule, afterLoad(tag, dir)); // docs/170: no load before -> start edge only
}

function selfCheck() {
  // 120 s of seconds rows from QPC 1000; first minute ends at row 59 (t = 60000), edge = last row - EDGE_MS
  const sec = 'lotAdds,t\n' + Array.from({ length: 120 }, (_, i) => `${i < 59 ? 9 : 1},${1000 + i * 1000}`).join('\n');
  const fr = 'epochMs,qpcMs,frameMs,svc1,svc1Ms\n' + ['0,30000,200,World/Service,150', '0,70000,80,World/Service,60', '0,71000,40,World/Service,30',
    '0,72000,90,CAS/SimService,70', '0,73000,120,World/Service,100'].join('\n');
  const r = check(csv(sec), csv(fr), { service: 'World/Service', min: 2, lotAdds: 20 });
  assert.equal(r.led, 2); // first-minute frame and the 40 ms frame dropped, CAS frame not counted
  assert.ok(r.lotAdds >= 50 && r.lotAdds < 120 && r.fails.length === 0);
  assert.deepEqual(check(csv(sec), csv(fr), { service: 'World/Service', min: 3 }).fails, ['World/Service led 2 frames > 50 ms (need 3)']);
  assert.match(check(csv('t\n1000'), csv(fr), { service: 'X', min: 1, lotAdds: 1 }).fails.join(';'), /no lotAdds column/);
  assert.throws(() => parseRule('{"service":"A"}'));
  console.log('frame-lead self-check ok');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  if (process.argv[2] === '--self-check') selfCheck();
  else {
    const [tag, rule] = process.argv.slice(2);
    if (!tag || !rule) { console.error("usage: node tools/harness/frame-lead.ts <tag> '<rule JSON>' | --self-check"); process.exit(2); }
    const root = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..');
    const r = checkTag(join(root, 'probe-out', 'profile'), tag, parseRule(rule));
    console.log(r.fails.length ? `FAIL: frame lead: ${r.fails.join('; ')}` : `PASS: frame lead: ${r.line}`);
    process.exit(r.fails.length ? 1 : 0);
  }
}
