// Task-name pass rules for queue jobs (profile.hitchTasks -> profile-world.ts --hitch-tasks, docs/169): each rule
// "<regex>[@<ms>]>=N" needs at least N hitch lines (<tag>-<secs>-hitches.jsonl, docs/166 `tasks`) with a task run whose
// name matches the regex (and that took >= ms, when given), so a repro job proves its firings hitched in the expected
// script task and not only that some run of that task was slow. "<regex>[@<ms>]<N" is the cap form: fewer than N such
// lines (e.g. "OwChainTheirs@100<1": no iteration with a >= 100 ms run of the chain's task, docs/145 §11).
//   node tools/harness/hitch-tasks.ts    self-check
import assert from 'node:assert/strict';

export function parseHitchTasks(rules: string[]): { re: RegExp; ms: number; min: number; below: boolean; text: string }[] {
  if (!Array.isArray(rules)) throw new Error('hitch-tasks: a JSON list of "<regex>[@<ms>]>=N" or "<regex>[@<ms>]<N" rules');
  return rules.map(text => {
    const m = /^(.+?)(?:@(\d+))?(>=|<)(\d+)$/.exec(text);
    if (!m) throw new Error(`hitch-tasks: "${text}" is not <regex>[@<ms>]>=N or <regex>[@<ms>]<N`);
    return { re: new RegExp(m[1]), ms: Number(m[2] ?? 0), min: Number(m[4]), below: m[3] === '<', text };
  });
}

// Failed rules as text ([] = all hold) and the count per rule.
export function checkHitchTasks(rules: string[], hitches: { tasks?: { name?: string; ms?: number }[] }[]): { fails: string[]; counts: number[] } {
  const parsed = parseHitchTasks(rules);
  const counts = parsed.map(r => hitches.filter(h => (h.tasks ?? []).some(t => t.name && r.re.test(t.name) && (t.ms ?? 0) >= r.ms)).length);
  return { counts, fails: parsed.flatMap((r, i) => (r.below ? counts[i] < r.min : counts[i] >= r.min) ? [] : [`${r.text}: ${counts[i]} hitch lines`]) };
}

if (import.meta.url === `file:///${process.argv[1]?.replace(/\\/g, '/').replace(/^\//, '')}`) {
  const h = [{ tasks: [{ name: 'ScriptProxy>Sims3.Gameplay.Core.WorldLot', ms: 40 }] }, { tasks: [{ name: 'ScriptProxy>Sims3.Gameplay.Core.WorldLot', ms: 350 }] },
    { tasks: [{ name: 'OneShotFunctionTask>Ts3Fixes.Fixes.StuckCheckDrain', ms: 5 }] }, {}];
  assert.deepEqual(checkHitchTasks(['Core\\.(World)?Lot$>=2', 'StuckCheckDrain>=1'], h), { fails: [], counts: [2, 1] });
  assert.deepEqual(checkHitchTasks(['Core\\.(World)?Lot$@100>=1'], h).counts, [1]); // only the 350 ms run counts
  assert.deepEqual(checkHitchTasks(['StuckCheckDrain>=2'], h).fails, ['StuckCheckDrain>=2: 1 hitch lines']);
  assert.deepEqual(checkHitchTasks(['a>b>=0'], h).counts, [0]); // a '>' inside the regex is fine
  assert.throws(() => parseHitchTasks(['x>1']), /not <regex>/);
  assert.deepEqual(checkHitchTasks(['Core\\.(World)?Lot$@100<1'], h).fails, ['Core\\.(World)?Lot$@100<1: 1 hitch lines']); // cap form: the 350 ms run breaks it
  assert.deepEqual(checkHitchTasks(['StuckCheckDrain@100<1', 'Core\\.(World)?Lot$<3'], h).fails, []);
  assert.throws(() => parseHitchTasks('x>=1' as any), /JSON list/);
  console.log('hitch-tasks selftest: PASS');
}
