// Logs Claude plan usage to docs/ai-usage.md and prints budget status.
// Source: ~/.claude/usage-latest.json, written by the user's Claude Code status line from its `rate_limits` input.
// Usage: node tools/ai-usage.ts [lane] [item...]   (no args: print only, nothing logged)
import { readFileSync, appendFileSync, readdirSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';

const BUDGET: Record<string, number> = { G: 30, M: 30, H: 50 }; // % per window; G and M share 30; 20% kept outside the project
const STOP_AT = 80;

let u: any;
try {
  u = JSON.parse(readFileSync(join(homedir(), '.claude', 'usage-latest.json'), 'utf8'));
} catch {
  console.error('No ~/.claude/usage-latest.json yet: the status line writes it after the first reply of a session.');
  process.exit(1);
}

const pht = (d: Date) => new Date(d.getTime() + 8 * 3600e3).toISOString().slice(0, 16).replace('T', ' ');
const win = (k: string) => {
  const w = u.rate_limits?.[k];
  if (w?.used_percentage == null) return '-';
  return `${Math.round(w.used_percentage)}%` + (w.resets_at ? ` (reset ${pht(new Date(w.resets_at * 1000)).slice(11)} PHT)` : '');
};
const worst = Math.max(...['five_hour', 'seven_day'].map(k => u.rate_limits?.[k]?.used_percentage ?? 0)); // the weekly window usually binds
const ageMin = Math.round((Date.now() - Date.parse(u.at)) / 60000);

console.log(`5h ${win('five_hour')} | 7d ${win('seven_day')} | read ${ageMin} min ago`);

// Share per project folder in the 7d window, from the transcripts' token counts.
// shortcut: token weights are Anthropic's usual price ratios (cache read 0.1, 5m write 1.25, 1h write 2, output 5);
// model differences (subagents on cheaper models) are ignored, so it is a rough share; upgrade if plans weigh models differently.
const resets = u.rate_limits?.seven_day?.resets_at;
const since = resets ? resets * 1000 - 7 * 86400e3 : Date.now() - 7 * 86400e3;
const root = join(homedir(), '.claude', 'projects');
const perProject = new Map<string, number>();
const seen = new Set<string>();
const scan = (dir: string, project: string) => {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, e.name);
    if (e.isDirectory()) { scan(p, project); continue; }
    if (!e.name.endsWith('.jsonl') || statSync(p).mtimeMs < since) continue;
    for (const line of readFileSync(p, 'utf8').split('\n')) {
      if (!line.includes('"usage"')) continue;
      let j: any;
      try { j = JSON.parse(line); } catch { continue; }
      const m = j.message, us = m?.usage;
      if (!us || Date.parse(j.timestamp) < since) continue;
      const key = `${m.id}:${j.requestId}`; // streamed chunks repeat the same usage
      if (seen.has(key)) continue;
      seen.add(key);
      const c1h = us.cache_creation?.ephemeral_1h_input_tokens ?? 0;
      const w = (us.input_tokens ?? 0) + 0.1 * (us.cache_read_input_tokens ?? 0)
        + 1.25 * ((us.cache_creation_input_tokens ?? 0) - c1h) + 2 * c1h + 5 * (us.output_tokens ?? 0);
      perProject.set(project, (perProject.get(project) ?? 0) + w);
    }
  }
};
for (const e of readdirSync(root, { withFileTypes: true })) if (e.isDirectory()) scan(join(root, e.name), e.name);
const total = [...perProject.values()].reduce((a, b) => a + b, 0);
const week = u.rate_limits?.seven_day?.used_percentage;
if (total) {
  console.log(`7d share by project (estimate${week != null ? `, of ${Math.round(week)}% used` : ''}):`);
  for (const [k, v] of [...perProject].sort((a, b) => b[1] - a[1]).slice(0, 8)) {
    const share = v / total;
    console.log(`  ${(share * 100).toFixed(0).padStart(3)}%${week != null ? ` ~${(share * week).toFixed(1)} pts` : ''}  ${k}`);
  }
}
if (worst >= STOP_AT) console.log(`STOP: ${Math.round(worst)}% >= ${STOP_AT}% (project share used)`);

const [lane, ...item] = process.argv.slice(2);
if (lane) {
  const L = lane.toUpperCase();
  if (!(L in BUDGET)) { console.error(`lane must be G, M or H`); process.exit(1); }
  const [date, time] = pht(new Date(u.at)).split(' ');
  appendFileSync('docs/ai-usage.md', `| ${date} | ${time} | 5h ${win('five_hour').split(' ')[0]}, 7d ${win('seven_day').split(' ')[0]} | ${L} | ${item.join(' ')} | |\n`);
  console.log(`logged: lane ${L} (budget ${BUDGET[L]}%), ${item.join(' ')}`);
}
