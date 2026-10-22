// ts3fixes counter pass rules for queue jobs (profile.fixesRequire -> profile-world.ts --fixes-require): a rule list such as
// "easpread_lots_passes>=1,easpread_lots_max_tick_us<5000" checked against the agent's `patches` answer at the end of the run,
// so a job proves the mechanism ran (and stayed under its cap) from counters, not from timing.
//   node tools/harness/fixes-require.ts    self-check
import assert from 'node:assert/strict';

export const RULE = /^([A-Za-z0-9_.]+)(>=|<=|==|>|<)(-?\d+)$/;

export function parseRequire(s: string): { name: string; op: string; value: number }[] {
  return s.split(',').map(r => {
    const m = RULE.exec(r.trim());
    if (!m) throw new Error(`fixes-require: "${r}" is not name<op>number (ops >= <= == > <)`);
    return { name: m[1], op: m[2], value: Number(m[3]) };
  });
}

// Failed rules as text; [] = all hold. A counter missing from the answer fails (old agent or wrong name).
export function checkRequire(s: string, answer: Record<string, unknown> | null | undefined): string[] {
  const fails: string[] = [];
  for (const { name, op, value } of parseRequire(s)) {
    // agent.js flattens counter arrays as name_i; rules may write name.i (repro-check-route sypPlans.1 read "missing", 2026-10-22)
    const v = answer?.[name] ?? answer?.[name.replace(/\.(\d+)$/, '_$1')];
    if (typeof v !== 'number') { fails.push(`${name} missing`); continue; }
    const ok = op === '>=' ? v >= value : op === '<=' ? v <= value : op === '==' ? v === value : op === '>' ? v > value : v < value;
    if (!ok) fails.push(`${name}=${v} (want ${op}${value})`);
  }
  return fails;
}

// The numeric fields of a `patches` answer (switch states and counters), for per-slice snapshots.
export function numericFields(answer: Record<string, unknown> | null | undefined): Record<string, number | boolean> {
  return Object.fromEntries(Object.entries(answer ?? {}).filter(([, v]) => typeof v === 'number' || typeof v === 'boolean')) as Record<string, number | boolean>;
}

if (import.meta.url === `file:///${process.argv[1]?.replace(/\\/g, '/').replace(/^\//, '')}`) {
  assert.deepEqual(checkRequire('a>=1,b<5000', { a: 2, b: 4999 }), []);
  assert.deepEqual(checkRequire('a>=1,b<5000', { a: 0, b: 5000 }), ['a=0 (want >=1)', 'b=5000 (want <5000)']);
  assert.deepEqual(checkRequire('c==0', {}), ['c missing']);
  assert.deepEqual(checkRequire('c==-1', { c: -1 }), []);
  assert.throws(() => parseRequire('a=>1'), /not name<op>number/);
  assert.deepEqual(numericFields({ a: 1, b: true, c: 'x', d: [1] }), { a: 1, b: true });
  console.log('fixes-require selftest: PASS');
}
