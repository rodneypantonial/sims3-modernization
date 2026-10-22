// Release feature switchboard (docs/RELEASE.md "v0.1.5"): one step from "the A/Bs decided these features" to the release
// defaults. It knows every v0.1.5 candidate and where its default lives: ts3native.ini keys (native/ts3native/ts3native.ini,
// which build.cmd copies to out/) and ts3fixes tuning (tools/setup/build-fixes.ts TUNING_XML). Unchosen candidates are set
// back to off, so the result depends only on the list.
//   node tools/release-features.ts --list                      candidates, their edits, what they need
//   node tools/release-features.ts --status                    which candidates the current ini/tuning have on
//   node tools/release-features.ts slotenabledro,fallbackasync dry run: print every edit, change nothing
//   node tools/release-features.ts slotenabledro,... --write   write ini + tuning
//   node tools/release-features.ts slotenabledro,... --write --docs   also resolve the feature blocks in docs/PLAYERS.md
//                                                             and CHANGELOG.md (one way: the markers are removed)
//   node tools/release-features.ts none --write                every candidate off (= the v0.1.4 defaults)
//   node tools/release-features.ts --self-check                offline checks (CI)
// Doc blocks: lines between "<!-- feature:<id> on -->" and "<!-- /feature -->" stay only when <id> is chosen, between
// "<!-- feature:<id> off -->" and "<!-- /feature -->" only when it is not. Until --docs runs, PLAYERS.md names every
// candidate, so the CI PLAYERS.md check passes for any choice.
import { readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..');
const INI = join(ROOT, 'native', 'ts3native', 'ts3native.ini');
const TUNING = join(ROOT, 'tools', 'setup', 'build-fixes.ts');
const DOCS = [join(ROOT, 'docs', 'PLAYERS.md'), join(ROOT, 'CHANGELOG.md')];
const CI = join(ROOT, '.github', 'workflows', 'build.yml');

type Feature = { ini?: Record<string, [on: string, off: string]>; tuning?: string[]; needs?: string[]; doc: string };
// ini key -> [release value when chosen, value when not]; tuning = ts3fixes tunables set True when chosen, False when not
export const FEATURES: Record<string, Feature> = {
  fallbackasync: { ini: { 'RouteAsync.FallbackAsync': ['1', '0'], 'RouteAsync.SyncCapMs': ['1000', '0'] }, doc: 'docs/134 (SyncCapMs=1000 decided 2026-10-21, ships with FallbackAsync)' },
  m1retry: { ini: { 'RouteAsync.M1Retry': ['1', '0'] }, needs: ['fallbackasync'], doc: 'docs/134 §2.6' },
  fbjobcap: { ini: { 'RouteAsync.FbJobCapMs': ['300', '0'] }, needs: ['fallbackasync'], doc: 'docs/134 §10 (ts3native 0.36.16)' },
  slotenabledro: { ini: { 'SlotEnabledRO.Enabled': ['1', '0'], 'SlotEnabledRO.Mode': ['1', '1'], 'SlotEnabledRO.Shadow': ['0', '0'] }, doc: 'docs/90, docs/136 (decided ship)' },
  deferfootprint: { ini: { 'Discourage.DeferFootprint': ['1', '0'] }, doc: 'docs/89 design U' },
  destroypark: { ini: { 'Discourage.DestroyPark': ['1', '0'] }, doc: 'docs/88 §10 design G' },
  avoidro: { ini: { 'AvoidRO.Enabled': ['1', '0'], 'AvoidRO.Mode': ['1', '1'], 'AvoidRO.Shadow': ['0', '0'] }, doc: 'docs/137' },
  unseenthrottle: { ini: { 'UnseenThrottle.Enabled': ['1', '0'] }, doc: 'docs/143 (needs ts3fixes from the same release: household positions)' },
  stuckcheckasync: { tuning: ['kStuckCheckAsync'], doc: 'docs/134 §8' },
  replandefer: { tuning: ['kReplanDefer'], doc: 'docs/141 §6' },
  replanspread: { tuning: ['kReplanSpread'], doc: 'docs/141 §6' },
  safespotcap: { tuning: ['kSafeSpotCap'], doc: 'docs/141 §7' },
  farthestsorted: { tuning: ['kFarthestLotSorted'], doc: 'docs/141 §7' },
  indoorcap: { tuning: ['kIndoorPositionCap'], doc: 'docs/141 §7' },
  overwatchspread: { tuning: ['kOverwatchSpread'], doc: 'docs/145' },
  easpreadlots: { tuning: ['kEaSpreadLots'], doc: 'docs/147 §2' },
  easpreadpets: { tuning: ['kEaSpreadPets'], doc: 'docs/147 §3' },
  easpreaddecay: { tuning: ['kEaSpreadDecay'], doc: 'docs/147 §4' },
  errortrapspread: { tuning: ['kErrorTrapSpread'], doc: 'docs/146' },
  unseenenv: { tuning: ['kUnseenEnvThrottle'], doc: 'docs/144' },
  unseenmotives: { tuning: ['kUnseenMotiveThrottle'], doc: 'docs/144' },
  unseenbroadcast: { tuning: ['kUnseenBroadcastThrottle'], doc: 'docs/144' },
};

export function parseChosen(arg: string): Set<string> {
  const ids = arg === 'none' ? [] : arg.split(',').map(s => s.trim()).filter(Boolean);
  for (const id of ids) if (!FEATURES[id]) throw new Error(`unknown feature "${id}" (--list shows the candidates)`);
  for (const id of ids) for (const n of FEATURES[id].needs ?? []) if (!ids.includes(n)) throw new Error(`${id} needs ${n}`);
  return new Set(ids);
}

// set Section.Key=value on its non-comment line; throws when the key is missing (the ini layout changed)
export function setIni(text: string, key: string, value: string, log: string[]): string {
  const [sec, k] = key.split('.');
  const lines = text.split(/\r?\n/);
  const eol = text.includes('\r\n') ? '\r\n' : '\n';
  let cur = '', hit = -1;
  for (let i = 0; i < lines.length; i++) {
    const t = lines[i].trim();
    if (t.startsWith('[')) cur = t.slice(1, t.indexOf(']'));
    else if (cur === sec && !t.startsWith(';') && t.split('=')[0].trim() === k) { hit = i; break; }
  }
  if (hit < 0) throw new Error(`ts3native.ini has no ${key}`);
  const old = lines[hit].split('=').slice(1).join('=').trim();
  if (old !== value) { log.push(`ini  [${sec}] ${k}: ${old} -> ${value}`); lines[hit] = `${k}=${value}`; }
  return lines.join(eol);
}

export function setTuning(text: string, name: string, on: boolean, log: string[]): string {
  const re = new RegExp(`<${name} value="(True|False)"`);
  const m = text.match(re);
  if (!m) throw new Error(`build-fixes.ts tuning has no ${name}`);
  const v = on ? 'True' : 'False';
  if (m[1] !== v) log.push(`tuning ${name}: ${m[1]} -> ${v}`);
  return text.replace(re, `<${name} value="${v}"`);
}

export function resolveBlocks(text: string, chosen: Set<string>, log: string[], file: string): string {
  const out: string[] = [];
  let keep = true, open = '';
  const eol = text.includes('\r\n') ? '\r\n' : '\n';
  for (const line of text.split(/\r?\n/)) {
    const s = line.match(/^<!-- feature:([a-z0-9]+) (on|off) -->$/);
    if (s) {
      if (open) throw new Error(`${file}: feature block ${s[1]} opens inside ${open}`);
      if (!FEATURES[s[1]]) throw new Error(`${file}: unknown feature block ${s[1]}`);
      open = s[1]; keep = (s[2] === 'on') === chosen.has(s[1]);
      log.push(`doc  ${file}: ${s[1]} ${s[2]} block ${keep ? 'kept' : 'removed'}`);
      continue;
    }
    if (line === '<!-- /feature -->') { if (!open) throw new Error(`${file}: stray <!-- /feature -->`); open = ''; keep = true; continue; }
    if (keep) out.push(line);
  }
  if (open) throw new Error(`${file}: feature block ${open} not closed`);
  return out.join(eol);
}

export function apply(chosen: Set<string>, ini: string, tuning: string, log: string[]): { ini: string; tuning: string } {
  for (const [id, f] of Object.entries(FEATURES)) {
    for (const [key, [on, off]] of Object.entries(f.ini ?? {})) ini = setIni(ini, key, chosen.has(id) ? on : off, log);
    for (const t of f.tuning ?? []) tuning = setTuning(tuning, t, chosen.has(id), log);
  }
  return { ini, tuning };
}

// which candidates the given ini/tuning have on (every key at its "on" value)
export function status(ini: string, tuning: string): string[] {
  return Object.entries(FEATURES).filter(([, f]) => {
    const log: string[] = [];
    for (const [key, [on]] of Object.entries(f.ini ?? {})) setIni(ini, key, on, log);
    for (const t of f.tuning ?? []) setTuning(tuning, t, true, log);
    return log.length === 0;  // already at every "on" value
  }).map(([id]) => id);
}

// sections the CI release step accepts as Enabled=1 (build.yml "Not stable" check)
function ciAllowed(): string[] {
  const m = readFileSync(CI, 'utf8').match(/extra=\$\(awk[^\n]*/);
  return m ? [...m[0].matchAll(/s!="\[(\w+)\]"/g)].map(x => x[1]) : [];
}

function selfCheck(): void {
  const ok = (c: boolean, what: string) => { if (!c) { console.error(`self-check FAILED: ${what}`); process.exit(1); } };
  const ini = '[RouteAsync]\n; FallbackAsync=9 comment\nEnabled=1\nFallbackAsync=0\nM1Retry=0\nSyncCapMs=0\nFbJobCapMs=0\n[SlotEnabledRO]\nEnabled=0\nMode=1\nShadow=0\n' +
    '[Discourage]\nDeferFootprint=0\nDestroyPark=0\n[AvoidRO]\nEnabled=0\nMode=1\nShadow=0\n[UnseenThrottle]\nEnabled=0\n';
  const tun = Object.values(FEATURES).flatMap(f => f.tuning ?? []).map(t => `<${t} value="False" />`).join('\n') + '\n<kGoHomeShortcut value="True" />';
  const log: string[] = [];
  const a = apply(parseChosen('fallbackasync,m1retry,stuckcheckasync'), ini, tun, log);
  ok(a.ini.includes('\nFallbackAsync=1\n') && a.ini.includes('\nSyncCapMs=1000\n') && a.ini.includes('\nM1Retry=1\n'), 'fallbackasync + m1retry set');
  ok(a.ini.includes('; FallbackAsync=9 comment'), 'comment lines untouched');
  ok(a.tuning.includes('<kStuckCheckAsync value="True"') && a.tuning.includes('<kReplanDefer value="False"') && a.tuning.includes('<kGoHomeShortcut value="True"'), 'tuning set');
  ok(log.length === 4, `4 edits logged (got ${log.length})`);
  ok(status(a.ini, a.tuning).join() === 'fallbackasync,m1retry,stuckcheckasync', `status reads back (${status(a.ini, a.tuning).join()})`);
  const b = apply(parseChosen('none'), a.ini, a.tuning, []);
  ok(b.ini === ini && b.tuning === tun, 'none = back to all off');
  let threw = ''; try { parseChosen('m1retry'); } catch (e: any) { threw = e.message; } ok(/needs fallbackasync/.test(threw), 'needs checked');
  threw = ''; try { parseChosen('nope'); } catch (e: any) { threw = e.message; } ok(/unknown/.test(threw), 'unknown id refused');
  threw = ''; try { setIni(ini, 'AvoidRO.Missing', '1', []); } catch (e: any) { threw = e.message; } ok(/no AvoidRO.Missing/.test(threw), 'missing key refused');
  const doc = 'a\n<!-- feature:avoidro on -->\nAV on\n<!-- /feature -->\n<!-- feature:avoidro off -->\nAV off\n<!-- /feature -->\nb';
  ok(resolveBlocks(doc, new Set(['avoidro']), [], 'x') === 'a\nAV on\nb', 'on block kept');
  ok(resolveBlocks(doc, new Set(), [], 'x') === 'a\nAV off\nb', 'off block kept');
  threw = ''; try { resolveBlocks('<!-- feature:avoidro on -->\nx', new Set(), [], 'x'); } catch (e: any) { threw = e.message; } ok(/not closed/.test(threw), 'open block refused');
  // the real files: every key and tunable exists, every doc block parses, the repo state reads back
  const realIni = readFileSync(INI, 'utf8'), realTun = readFileSync(TUNING, 'utf8');
  apply(new Set(Object.keys(FEATURES)), realIni, realTun, []);
  for (const f of DOCS) resolveBlocks(readFileSync(f, 'utf8'), new Set(), [], f);
  console.log('release-features self-check ok');
}

const args = process.argv.slice(2);
if (args.includes('--self-check')) selfCheck();
else if (args.includes('--list')) {
  for (const [id, f] of Object.entries(FEATURES))
    console.log(`${id.padEnd(16)} ${[...Object.entries(f.ini ?? {}).map(([k, [on]]) => `${k}=${on}`), ...(f.tuning ?? []).map(t => `${t}=True`)].join(', ')}${f.needs ? `  (needs ${f.needs.join(', ')})` : ''}  [${f.doc}]`);
} else if (args.includes('--status')) {
  console.log(`on in the repo defaults: ${status(readFileSync(INI, 'utf8'), readFileSync(TUNING, 'utf8')).join(', ') || 'none'}`);
} else {
  const list = args.find(a => !a.startsWith('--'));
  if (!list) { console.error('usage: node tools/release-features.ts <id,id,...|none> [--write [--docs]] | --list | --status | --self-check'); process.exit(2); }
  const chosen = parseChosen(list);
  const log: string[] = [];
  const r = apply(chosen, readFileSync(INI, 'utf8'), readFileSync(TUNING, 'utf8'), log);
  const docs = args.includes('--docs') ? DOCS.map(f => [f, resolveBlocks(readFileSync(f, 'utf8'), chosen, log, f.slice(ROOT.length + 1))]) : [];
  console.log(log.join('\n') || 'no changes');
  // the CI release step refuses an Enabled=1 section it does not list
  const allowed = new Set(ciAllowed());
  const blocked = [...chosen].flatMap(id => Object.keys(FEATURES[id].ini ?? {}).filter(k => k.endsWith('.Enabled')).map(k => k.split('.')[0])).filter(s => !allowed.has(s));
  if (blocked.length) console.log(`WARNING: .github/workflows/build.yml does not allow ${blocked.join(', ')} as Enabled=1: add them to the "Not stable" check`);
  if (args.includes('--write')) {
    writeFileSync(INI, r.ini); writeFileSync(TUNING, r.tuning);
    for (const [f, t] of docs) writeFileSync(f, t);
    console.log(`written: ts3native.ini, build-fixes.ts${docs.length ? ', PLAYERS.md, CHANGELOG.md' : ''}. Next: native/ts3native/build.cmd, node tools/setup/build-fixes.ts, hashes into docs/RELEASE.md`);
  } else console.log('(dry run: add --write to change the files)');
}
