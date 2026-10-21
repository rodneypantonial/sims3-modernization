// Send a ts3native crash report (docs/135). ts3native [CrashReport] writes scrubbed text reports to
// <Documents>\Electronic Arts\The Sims 3\ts3native-crashes\. This lists the new ones, shows exactly what would be sent,
// and only after you say yes either uses your own logged-in GitHub CLI (`gh`: find an issue with the same signature and
// comment there, else open a new one) or opens a prefilled GitHub "new issue" page you submit yourself.
// Nothing is sent without your yes, and no token is stored anywhere.
//   node tools/report-crash.ts                 go through the new reports one by one
//   node tools/report-crash.ts --list          list them, send nothing
//   node tools/report-crash.ts --browser       never use gh, always the prefilled page
//   node tools/report-crash.ts --dir <folder> --repo <owner/name>
//   node tools/report-crash.ts --self-check    offline checks (CI)
import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { homedir, hostname, tmpdir, userInfo } from 'node:os';
import { join, resolve } from 'node:path';
import { createInterface } from 'node:readline/promises';
import { parseArgs } from 'node:util';

const REPO = 'rodneypantonial/sims3-modernization';
const BODY_MAX = 60000;  // GitHub allows 65536 characters per issue or comment
const URL_MAX = 7500;    // browsers and GitHub cut long URLs (~8 KB)
const SIG_RE = /crashsig-[0-9a-f]{8}/;

type Report = { file: string; text: string; fields: Record<string, string> };

// Documents folder as Windows has it (OneDrive may move it), else ~/Documents
function documents(): string {
  if (process.platform === 'win32') {
    try {
      const out = execFileSync('reg', ['query', 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders', '/v', 'Personal'],
        { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'], timeout: 10000 });
      const m = out.match(/Personal\s+REG_(?:EXPAND_)?SZ\s+(.+)/);
      if (m) return m[1].trim().replace(/%([^%]+)%/g, (all, v) => process.env[v] ?? all);
    } catch { /* fall through */ }
  }
  return join(homedir(), 'Documents');
}

function parse(text: string): Record<string, string> {
  const fields: Record<string, string> = {};
  for (const m of text.split('--- ')[0].matchAll(/^([a-z0-9 ]+): (.*?)\r?$/gm)) fields[m[1]] ??= m[2];
  return fields;
}

// Second guard on top of the mod's scrubber: this PC's user name, machine name and home folder, and any \Users\<x>.
function scrub(text: string, names = { user: userInfo().username, host: hostname(), home: homedir() }): string {
  const esc = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  let t = text;
  const pairs: [string, string][] = [[names.home, '<home>'], [names.user, '<user>'], [names.host, '<machine>']];
  for (const [v, to] of pairs.sort((a, b) => b[0].length - a[0].length))  // longest first: a host 'box-zed' before a user 'zed'
    if (v && v.length >= 3) t = t.replace(new RegExp(esc(v), 'gi'), to);
  return t.replace(/([\\/])Users\1[^\\/\r\n"' ]+/gi, (_m, s) => `${s}Users${s}<user>`);
}

function title(r: Report): string {
  return r.fields.title ?? `[crash] ${r.fields.signature ?? 'unknown'} (ts3native ${r.fields.ts3native ?? '?'})`;
}

function body(r: Report): string {
  const head = [
    `### Signature id\n\n${r.fields['signature id'] ?? '?'}`,
    `### Signature\n\n\`${r.fields.signature ?? '?'}\``,
    `### ts3native version\n\n${r.fields.ts3native ?? '?'}`,
    `### Crash report\n\n`,
  ].join('\n\n');
  let rep = r.text.replace(/\r\n/g, '\n');
  const room = BODY_MAX - head.length - 200;
  if (rep.length > room) rep = rep.slice(0, room) + '\n[cut here: the full file is on the reporter\'s PC]\n';
  return `${head}\`\`\`text\n${rep}\`\`\`\n\n_Sent with tools/report-crash.ts._\n`;
}

// Prefilled issue-form page: the crash.yml field ids; the report is cut to fit the URL (the player can attach the file)
function issueUrl(r: Report, repo = REPO): string {
  const base = `https://github.com/${repo}/issues/new?` + new URLSearchParams({
    template: 'crash.yml', title: title(r), signature: r.fields['signature id'] ?? '', version: r.fields.ts3native ?? '',
  }).toString();
  const lines = r.text.replace(/\r\n/g, '\n').split('\n');
  for (let n = lines.length; n > 0; n = Math.floor(n * 0.8)) {
    const rep = lines.slice(0, n).join('\n') + (n < lines.length ? '\n[cut: please attach the full report file]' : '');
    const url = `${base}&${new URLSearchParams({ report: rep })}`;
    if (url.length <= URL_MAX) return url;
  }
  return base;
}

function gh(args: string[]): string {
  return execFileSync('gh', args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout: 60000 }).trim();
}

function ghReady(): boolean {
  try { gh(['auth', 'status']); return true; } catch { return false; }
}

function openUrl(url: string) {
  const [cmd, args] = process.platform === 'win32' ? ['rundll32', ['url.dll,FileProtocolHandler', url]]
    : process.platform === 'darwin' ? ['open', [url]] : ['xdg-open', [url]];
  try { execFileSync(cmd, args, { stdio: 'ignore', timeout: 15000 }); } catch { /* the URL is printed anyway */ }
}

function sendWithGh(r: Report, repo: string): string {
  const id = r.fields['signature id'];
  const tmp = mkdtempSync(join(tmpdir(), 'ts3crash-'));
  const file = join(tmp, 'body.md');
  writeFileSync(file, body(r));
  try {
    const found = id && SIG_RE.test(id)
      ? JSON.parse(gh(['issue', 'list', '--repo', repo, '--state', 'all', '--search', `${id} in:body`, '--json', 'number,url', '--limit', '1']))
      : [];
    if (found.length) {
      console.log(`Same crash already reported: ${found[0].url}. Adding your report as a comment.`);
      return gh(['issue', 'comment', String(found[0].number), '--repo', repo, '--body-file', file]);
    }
    return gh(['issue', 'create', '--repo', repo, '--title', title(r), '--body-file', file]);
  } finally {
    rmSync(tmp, { recursive: true, force: true });
  }
}

function selfCheck() {
  const sample = [
    'ts3native crash report 1', 'kind: exception', 'time: 2026-10-21T01:02:03Z',
    'title: [crash] TS3W+0x23456 (ts3native 0.36.7)', 'signature id: crashsig-0a1b2c3d',
    'signature: TS3W+0x23456 TS3W+0x1000 d3d9+0x42 TS3W+0x2000', 'ts3native: 0.36.7',
    'fault: TS3W+0x23456', 'stack (return addresses found on the stack, top first):', '  TS3W+0x1000',
    'exe sha256: abc', '--- package files (names only) ---', ...Array.from({ length: 900 }, (_, i) => `  cc-${i}.package`),
    'packages: 900', '--- ts3native.log (last 50 lines) ---', 'host C:\\Users\\Zed\\Steam\\TS3W.exe on box-zed', '--- end ---',
  ].join('\r\n');
  const r: Report = { file: 'crash-x.txt', text: scrub(sample, { user: 'zed', host: 'box-zed', home: 'C:\\Users\\Zed' }), fields: parse(sample) };
  const ok = (c: boolean, what: string) => { if (!c) { console.error(`self-check FAILED: ${what}`); process.exit(1); } };
  ok(r.fields.title === '[crash] TS3W+0x23456 (ts3native 0.36.7)' && r.fields['signature id'] === 'crashsig-0a1b2c3d', 'parse header');
  ok(!/zed/i.test(r.text) && r.text.includes('host <home>\\Steam\\TS3W.exe on <machine>'), 'scrub');
  ok(scrub('x /Users/bob/y E:\\USERS\\carol\\z', { user: '', host: '', home: '' }) === 'x /Users/<user>/y E:\\Users\\<user>\\z', 'scrub \\Users\\ rule');
  ok(title(r) === r.fields.title && SIG_RE.test(body(r)) && body(r).length <= BODY_MAX, 'title and body');
  const big: Report = { ...r, text: r.text + 'x'.repeat(100000) };
  ok(body(big).length <= BODY_MAX && body(big).includes('[cut here'), 'body cut');
  const url = issueUrl(r);
  ok(url.length <= URL_MAX && url.includes('template=crash.yml') && url.includes('signature=crashsig-0a1b2c3d') && url.includes('cut%3A+please+attach'), 'url');
  const yml = readFileSync(resolve(import.meta.dirname, '..', '.github', 'ISSUE_TEMPLATE', 'crash.yml'), 'utf8');
  for (const id of ['signature', 'version', 'report']) ok(new RegExp(`^\\s+id: ${id}$`, 'm').test(yml), `crash.yml field ${id}`);
  console.log('report-crash self-check ok');
}

async function main() {
  const { values } = parseArgs({ options: {
    dir: { type: 'string' }, repo: { type: 'string', default: REPO }, browser: { type: 'boolean' }, list: { type: 'boolean' }, 'self-check': { type: 'boolean' },
  } });
  if (values['self-check']) return selfCheck();
  const dir = values.dir ?? join(documents(), 'Electronic Arts', 'The Sims 3', 'ts3native-crashes');
  if (!existsSync(dir)) {
    console.log(`No crash folder at ${dir}. If your Documents folder has another name, the reports are in Game\\Bin\\ts3native-crashes: pass --dir.`);
    return;
  }
  const stateFile = join(dir, 'reported.json');
  const state: Record<string, string> = existsSync(stateFile) ? JSON.parse(readFileSync(stateFile, 'utf8')) : {};
  const fresh = readdirSync(dir).filter(n => /^crash-.*\.txt$/.test(n) && !state[n]).sort();
  if (!fresh.length) { console.log(`No new crash reports in ${dir}.`); return; }
  console.log(`${fresh.length} new crash report(s) in ${dir}:`);
  const reports = fresh.map(file => {
    const raw = readFileSync(join(dir, file), 'utf8');
    return { file, text: scrub(raw), fields: parse(raw) } as Report;
  });
  for (const r of reports) console.log(`  ${r.file}  ${title(r)}`);
  if (values.list) return;
  const useGh = !values.browser && ghReady();
  const rl = createInterface({ input: process.stdin, output: process.stdout });
  try {
    for (const r of reports) {
      console.log(`\n================ ${r.file}: this is exactly what would be sent ================`);
      console.log(`Where: ${useGh ? `a GitHub issue or comment in ${values.repo}, posted as your own gh account` : `a GitHub page in your browser, prefilled; you press Submit`}`);
      console.log(`Title: ${title(r)}\n`);
      console.log(useGh ? body(r) : decodeURIComponent(new URL(issueUrl(r, values.repo)).searchParams.get('report') ?? ''));
      console.log('================ end of what would be sent ================');
      const a = (await rl.question('Send it? y = yes, n = not now, s = never ask again for this report: ')).trim().toLowerCase();
      if (a === 's') { state[r.file] = 'skipped'; writeFileSync(stateFile, JSON.stringify(state, null, 2)); continue; }
      if (a !== 'y') continue;
      if (useGh) {
        const url = sendWithGh(r, values.repo);
        console.log(`Sent: ${url}`);
        state[r.file] = url;
      } else {
        const url = issueUrl(r, values.repo);
        console.log(`Opening the prefilled page. Check it, press "Submit new issue", and attach the full file if it was cut:\n  ${join(dir, r.file)}\n${url}`);
        openUrl(url);
        state[r.file] = 'opened in browser';
      }
      writeFileSync(stateFile, JSON.stringify(state, null, 2));
    }
  } finally {
    rl.close();
  }
}

main().catch(e => { console.error(e instanceof Error ? e.message : e); process.exit(1); });
