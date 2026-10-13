// Creates the project's GitHub labels, milestones and issues from labels.json, milestones.json and issues.json (this folder).
//   node tools/github/create-issues.ts --repo owner/name [--branch main]   create what is missing (uses the gh CLI)
//   node tools/github/create-issues.ts --dry-run [--repo owner/name]       validate the JSON and print the plan; never calls gh
// Idempotent: existing labels and milestones (by name/title) are kept as they are, and an issue whose title already exists
// (open or closed) is skipped. Issue bodies may link docs as {{blob}}/docs/x.md; {{blob}} becomes
// https://github.com/<repo>/blob/<branch>. Needs Node >= 22.18 (runs .ts directly) and, without --dry-run, an authenticated gh.
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { parseArgs } from 'node:util';

type Label = { name: string; color: string; description: string };
type Milestone = { title: string; description: string; order: number };
type Issue = { key: string; title: string; labels: string[]; milestone: string; body: string };

const { values } = parseArgs({ options: { repo: { type: 'string' }, branch: { type: 'string', default: 'main' }, 'dry-run': { type: 'boolean' } } });
const dry = !!values['dry-run'];
if (!dry && !/^[\w.-]+\/[\w.-]+$/.test(values.repo ?? '')) throw new Error('--repo owner/name required (or --dry-run)');
const repo = values.repo ?? 'OWNER/REPO';
const read = <T>(f: string): T => JSON.parse(readFileSync(join(import.meta.dirname, f), 'utf8'));
const labels = read<Label[]>('labels.json');
const milestones = read<Milestone[]>('milestones.json').sort((a, b) => a.order - b.order);
const issues = read<Issue[]>('issues.json');

// ---- validation (also the --dry-run self-check) ----
// Personal data must never reach a public tracker: absolute user paths, e-mail addresses, backup drives.
const PERSONAL = [/[A-Za-z]:\\Users\\/i, /(^|[\s(`'"~])\/(Users|home)\/\w/m, /[\w.+-]+@[\w-]+\.[\w.]+/, /Sims3-backup/i];
const errors: string[] = [];
const dup = (xs: string[]) => xs.filter((x, i) => xs.indexOf(x) !== i);
for (const d of dup(labels.map(l => l.name))) errors.push(`duplicate label ${d}`);
for (const l of labels) if (!/^[0-9a-f]{6}$/i.test(l.color)) errors.push(`label ${l.name}: color must be 6 hex digits`);
for (const d of dup(milestones.map(m => m.title))) errors.push(`duplicate milestone ${d}`);
for (const d of dup(issues.map(i => i.key))) errors.push(`duplicate issue key ${d}`);
for (const d of dup(issues.map(i => i.title))) errors.push(`duplicate issue title ${d}`);
const labelNames = new Set(labels.map(l => l.name)), msTitles = new Set(milestones.map(m => m.title));
for (const i of issues) {
  const where = `issue ${i.key ?? '?'}`;
  if (!/^[a-z0-9-]+$/.test(i.key ?? '')) errors.push(`${where}: key must be a lowercase slug`);
  if (!i.title || i.title.length > 256) errors.push(`${where}: title missing or longer than 256`);
  if (!i.body || i.body.length > 65000) errors.push(`${where}: body missing or over GitHub's 65536 limit`);
  for (const l of i.labels ?? []) if (!labelNames.has(l)) errors.push(`${where}: unknown label ${l}`);
  if (!msTitles.has(i.milestone)) errors.push(`${where}: unknown milestone ${i.milestone}`);
  for (const p of (i.body ?? '').match(/\{\{\w+\}\}/g) ?? []) if (p !== '{{blob}}') errors.push(`${where}: unknown placeholder ${p}`);
  for (const re of PERSONAL) if (re.test(`${i.title}\n${i.body}`)) errors.push(`${where}: looks like personal data (${re})`);
}
if (errors.length) { console.error(errors.join('\n')); process.exit(1); }

const blob = `https://github.com/${repo}/blob/${values.branch}`;
const bodyOf = (i: Issue) => i.body.replaceAll('{{blob}}', blob);
const perMs = milestones.map(m => `${m.title}: ${issues.filter(i => i.milestone === m.title).length}`);
console.log(`${labels.length} labels, ${milestones.length} milestones, ${issues.length} issues (${perMs.join('; ')})`);

// ---- gh ----
const gh = (args: string[], input?: string) => execFileSync('gh', args, { encoding: 'utf8', input, stdio: ['pipe', 'pipe', 'inherit'] });
const pause = (ms: number) => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms); // GitHub throttles bursts of creates

const haveLabels = new Set<string>(dry ? [] : JSON.parse(gh(['label', 'list', '--repo', repo, '--limit', '1000', '--json', 'name'])).map((l: any) => l.name));
const haveMs = new Set<string>(dry ? [] : gh(['api', '--paginate', `repos/${repo}/milestones?state=all&per_page=100`, '--jq', '.[].title']).split('\n').filter(Boolean));
const haveTitles = new Set<string>(dry ? [] : JSON.parse(gh(['issue', 'list', '--repo', repo, '--state', 'all', '--limit', '5000', '--json', 'title'])).map((i: any) => i.title));

let made = 0, skipped = 0;
for (const l of labels) {
  if (haveLabels.has(l.name)) { skipped++; continue; }
  console.log(`${dry ? '[dry-run] ' : ''}label ${l.name} #${l.color}`);
  if (!dry) gh(['label', 'create', l.name, '--repo', repo, '--color', l.color, '--description', l.description]);
  made++;
}
for (const m of milestones) {
  if (haveMs.has(m.title)) { skipped++; continue; }
  console.log(`${dry ? '[dry-run] ' : ''}milestone ${m.title}`);
  if (!dry) gh(['api', `repos/${repo}/milestones`, '-f', `title=${m.title}`, '-f', `description=${m.description}`, '--silent']);
  made++;
}
for (const m of milestones) for (const i of issues.filter(x => x.milestone === m.title)) {
  if (haveTitles.has(i.title)) { console.log(`skip (exists): ${i.title}`); skipped++; continue; }
  console.log(`${dry ? '[dry-run] ' : ''}issue [${i.milestone}] ${i.title}  {${i.labels.join(', ')}}`);
  if (!dry) {
    const url = gh(['issue', 'create', '--repo', repo, '--title', i.title, '--body-file', '-', '--milestone', i.milestone, '--label', i.labels.join(',')], bodyOf(i));
    console.log(`  ${url.trim()}`);
    pause(1500);
  } else if (!bodyOf(i).includes(blob)) console.log('  (no doc links)');
  made++;
}
console.log(`${dry ? 'would create' : 'created'} ${made}, skipped ${skipped}`);
