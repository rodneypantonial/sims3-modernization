// docs/119 (M1): internal calls reachable from the docs/75 top autonomy entries, joined with docs/data/ic-safety.csv
// and classified for an offline Mono host (pure / reads-world / writes-world / engine-only / managed-reentry).
// Usage: node tools/mono-host-ics.ts [--index game-copy/analysis/callgraph/index.json] [--depth 12] [--top 20] > docs/data/mono-host-ics.csv
//        node tools/mono-host-ics.ts --summary ...   (counts to stderr only)
// Input: index.json from `node tools/managed-callgraph.ts build` (git-ignored), docs/data/autonomy-entry-list.csv, docs/data/ic-safety.csv.
import { readFileSync } from 'node:fs';
import { defRoots } from './managed-callgraph.ts';

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const INDEX = arg('--index', 'game-copy/analysis/callgraph/index.json');
const DEPTH = Number(arg('--depth', '12'));
const TOP = Number(arg('--top', '20'));

type M = { k: string; c: number[]; ic?: string };
const ix: { methods: M[]; kinds: string[] } = JSON.parse(readFileSync(INDEX, 'utf8'));

// CSV with quoted fields (no embedded newlines in these files).
const parseCsv = (s: string) => s.trim().split(/\r?\n/).map((l) => {
  const out: string[] = []; let cur = '', q = false;
  for (let i = 0; i < l.length; i++) {
    const ch = l[i];
    if (q) { if (ch === '"' && l[i + 1] === '"') { cur += '"'; i++; } else if (ch === '"') q = false; else cur += ch; }
    else if (ch === '"') q = true; else if (ch === ',') { out.push(cur); cur = ''; } else cur += ch;
  }
  out.push(cur); return out;
});

// Roots: base paths name a method; definition / family rows give a type whose Test and CalculateScore bodies are roots.
const BASE: Record<string, string[]> = {
  'Autonomy.CheckAvailability': ['Sims3.Gameplay.Autonomy.Autonomy::CheckAvailability'],
  'InteractionDefinitionUtilities.CommonTests': ['Sims3.Gameplay.Interactions.InteractionDefinitionUtilities::CommonTests'],
  'InteractionDefinitionUtilities.SpecialCaseTests': ['Sims3.Gameplay.Interactions.InteractionDefinitionUtilities::SpecialCaseTests'],
  'Autonomy.CalculateScoreForObjectInteraction': ['Sims3.Gameplay.Autonomy.Autonomy::CalculateScoreForObjectInteraction'],
  'GameObject.get_Parent': ['Sims3.Gameplay.Abstracts.GameObject::get_Parent'],
  'C2 self': ['Sims3.Gameplay.Autonomy.Autonomy::CalculateScoreAndAddToCandidates'],
  'MetaInteractionDefinitionUtilities.SpecialCaseTests': ['Sims3.Gameplay.Interactions.MetaInteractionDefinitionUtilities::SpecialCaseTests'],
  'Autonomy.UpdateCacheIfNeeded': ['Sims3.Gameplay.Autonomy.Autonomy::UpdateCacheIfNeeded'],
  'LiveDragHelperModel.get_CachedTopDraggedObject': ['Sims3.Gameplay.UI.LiveDragHelperModel::get_CachedTopDraggedObject'],
  'Autonomy.CheckAvailabilityTooltip': ['Sims3.Gameplay.Autonomy.Autonomy::CheckAvailabilityTooltip'],
};
// "scoring function Type.Method" rows: the type's namespace is not in the row, so match on "Type::Method(".
const SCORING = (s: string) => { const m = s.match(/^scoring function (\w+)\.(\w+)$/); return m ? ix.methods.flatMap((x, i) => (x.k.includes(`.${m[1]}::${m[2]}(`) ? [i] : [])) : []; };
const starts = (pat: string) => ix.methods.flatMap((m, i) => (m.k === pat || m.k.startsWith(pat + '(') || m.k.startsWith(pat + '`') ? [i] : []));
const rows = parseCsv(readFileSync('docs/data/autonomy-entry-list.csv', 'utf8'));
const h = rows[0]; const col = (n: string) => h.indexOf(n);
const roots: number[] = []; const used: string[] = [];
for (const r of rows.slice(1, TOP + 1)) {
  const def = r[col('definition_type')];
  const base = def.match(/^base path: (.*)$/);
  let pats: string[] = []; let defs: number[] = [];
  if (base) pats = BASE[base[1]] ?? [];
  else if (def.startsWith('scoring function ')) defs = SCORING(def);
  else if (!/unresolved/.test(def)) {
    const t = def.replace(/ \(target .*\)$/, '');
    const callees = r[col('top_callees')].split(';').map((x) => x.trim().replace(/ \d+$/, '')).filter(Boolean);
    // Test and CalculateScore up the base chain (docs/121 §8.5): many definitions inherit both.
    try { defs = defRoots(ix as never, t); } catch { pats = [`${t}::Test`, `${t}::CalculateScore`]; }
    // Family rows: the hot callees (e.g. Lot.HowMuchItWantsSimToCome) are roots too; resolve by type suffix.
    for (const c of callees) {
      const [ty, me] = [c.slice(0, c.lastIndexOf('.')), c.slice(c.lastIndexOf('.') + 1)];
      const hit = ix.methods.find((m) => m.k.includes(`.${ty}::${me}(`) || m.k.startsWith(`${ty}::${me}(`));
      if (hit) pats.push(hit.k.replace(/\(.*$/s, ''));
    }
  }
  const found = [...new Set([...defs, ...pats.flatMap(starts)])];
  if (found.length) { roots.push(...found); used.push(`${r[col('rank')]}:${def.slice(0, 60)}=${found.length}`); }
}

// Breadth-first closure over resolved and declared edges (same edge set as managed-callgraph.ts closure; delegates not followed).
const FOLLOW = new Set(['call', 'cv', 'virt', 'ifaceN', 'newobj']);
const dist = new Map<number, number>(); const from = new Map<string, Set<number>>();
let q = [...new Set(roots)]; q.forEach((r) => dist.set(r, 0));
for (let d = 0; q.length && d <= DEPTH; d++) {
  const next: number[] = [];
  for (const n of q) {
    const m = ix.methods[n];
    if (m.ic) { if (!from.has(m.ic)) from.set(m.ic, new Set()); from.get(m.ic)!.add(d); }
    for (let k = 0; k < m.c.length; k += 2) {
      const t = m.c[k];
      if (!FOLLOW.has(ix.kinds[m.c[k + 1]])) continue;
      if (d < DEPTH && !dist.has(t)) { dist.set(t, d + 1); next.push(t); }
    }
  }
  q = next;
}

// Join with ic-safety.csv and classify.
const ics = parseCsv(readFileSync('docs/data/ic-safety.csv', 'utf8'));
const ih = ics[0]; const ic = (r: string[], n: string) => r[ih.indexOf(n)] ?? '';
const byName = new Map(ics.slice(1).map((r) => [ic(r, 'ic'), r]));
// Classes for an offline host. ic-safety.csv's `writes` column also lists allocator and string-buffer globals, so
// "writes world" is decided by the method verb, not by that column. shortcut: name rules; a hand check per IC is M2's job.
const ENGINE = /::(Audio|Camera|UIManager|Window|Responder|Effect|VisualEffect|Light|Sound|Music|Video|Graphics|Render|Thumbnail|ScreenCapture|Fade|Tutorial|Hud)_|\.(VisualEffect|ThumbnailManager|VideoRecorder|DebugDraw|CameraController|UIManager|UTFConnectorUIManager|Audio)::/;
const STATIC = /::(EAText|LocalizedStringService|GameUtils|UserToolUtils|OnlineFeaturesHost|ScriptProxy_GetResourceKey|Simulator_LoadXmlAsString|CASUtils|DeviceConfig_Get|DeviceConfig_Is)/;
const WRITE_VERB = /::[A-Za-z]+_(Set|Add|Remove|Start|Stop|Save|Create|Destroy|Load|Play|Attach|Detach|Fire|Post|Push|Apply)/;
function classify(name: string, r?: string[]): [string, string] {
  if (/^(System|Mono)\./.test(name)) return ['pure', 'Mono runtime IC in mscorlib: implemented by the runtime itself'];
  if (/::MonoScriptHost_GetEnumSimple/.test(name)) return ['pure', 'registered by MonoScriptHost_Init 0xD7E560 itself (not in the binding table): enum name or hash from metadata'];
  if (!r) return ['unknown', 'no ic-safety.csv row'];
  const v = ic(r, 'verdict'), cm = ic(r, 'callsManaged'), proxy = ic(r, 'proxy');
  if (ENGINE.test(name) || v === 'MAIN') return ['engine-only', `ic-safety ${v}; device, UI or audio: no-op or fixed value`];
  if (cm === 'yes' || v === 'UNSAFE') return ['managed-reentry', `ic-safety ${v}; yields, reads the running task or calls back into managed code: scheduler shim`];
  if (/::DeviceConfig_(Set|Save)/.test(name)) return ['engine-only', 'user option write: no-op'];
  if (STATIC.test(name)) return ['static-data', `ic-safety ${v}/${proxy}; install or session data (strings, world name, options, products): record once per session`];
  if (WRITE_VERB.test(name) || proxy === 'queued') return ['writes-world', `ic-safety ${v}/${proxy}; changes sim or engine state: stub plus record (M3)`];
  if (/::Random_/.test(name)) return ['reads-world', 'native RNG state: record the stream or seed it (docs/27)'];
  if (v === 'SAFE') return ['pure', 'ic-safety SAFE: init-time config or no sim state'];
  return ['reads-world', `ic-safety ${v}/${proxy}; reads sim or world state: replay from a recording (M3)`];
}
const out = ['ic,address,class,min_depth,ic_verdict,ic_confidence,reason'];
const counts: Record<string, number> = {};
for (const [name, ds] of [...from.entries()].sort()) {
  // UTFConnector* and Sims3.UI.* declare the same native binding under another type: match on the method name.
  const meth = name.slice(name.indexOf('::'));
  const r = byName.get(name) ?? ics.slice(1).find((x) => ic(x, 'ic').endsWith(meth));
  const [cls, why] = classify(name, r);
  counts[cls] = (counts[cls] ?? 0) + 1;
  const esc = (s: string) => (/[",]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s);
  out.push([name, r ? ic(r, 'address') : '', cls, String(Math.min(...ds)), r ? ic(r, 'verdict') : '', r ? ic(r, 'confidence') : '', why].map(esc).join(','));
}
if (process.argv.includes('--all')) {
  // Whole binding table: offline class per IC class (subsystem), to stderr.
  const per = new Map<string, Record<string, number>>(); const tot: Record<string, number> = {};
  for (const r of ics.slice(1)) {
    const [cls] = classify(ic(r, 'ic'), r); const sub = ic(r, 'class');
    const p = per.get(sub) ?? {}; p[cls] = (p[cls] ?? 0) + 1; per.set(sub, p); tot[cls] = (tot[cls] ?? 0) + 1;
  }
  const n = (p: Record<string, number>) => Object.values(p).reduce((a, b) => a + b, 0);
  console.error(`all ${ics.length - 1} EA ICs: ${JSON.stringify(tot)}`);
  for (const [s, p] of [...per.entries()].sort((a, b) => n(b[1]) - n(a[1]))) console.error(`${s}\t${n(p)}\t${JSON.stringify(p)}`);
}
if (process.argv.includes('--cctors')) {
  // Static constructors that reach a native IC within 6 calls: they would run (or fail) on first type use in a bare host.
  const tally: Record<string, number> = {}; const lines: string[] = []; let all = 0;
  ix.methods.forEach((m, i) => {
    if (!m.k.includes('::.cctor(') || /^(System|Mono)\./.test(m.k)) return;
    all++;
    const seen = new Set([i]); let fr = [i]; const hit = new Map<string, string>();
    for (let d = 0; d <= 6 && fr.length; d++) {
      const nx: number[] = [];
      for (const n of fr) {
        const x = ix.methods[n];
        if (x.ic && !/^(System|Mono)\./.test(x.ic)) { const r = byName.get(x.ic) ?? ics.slice(1).find((y) => ic(y, 'ic').endsWith(x.ic!.slice(x.ic!.indexOf('::')))); hit.set(x.ic, classify(x.ic, r)[0]); }
        for (let k = 0; k < x.c.length; k += 2) if (FOLLOW.has(ix.kinds[x.c[k + 1]]) && !seen.has(x.c[k])) { seen.add(x.c[k]); nx.push(x.c[k]); }
      }
      fr = nx;
    }
    if (!hit.size) return;
    const cls = [...new Set(hit.values())].sort();
    cls.forEach((c) => (tally[c] = (tally[c] ?? 0) + 1));
    lines.push(`${m.k.replace(/::\.cctor\(.*$/, '')}\t${hit.size}\t${cls.join('+')}\t${[...hit.keys()].slice(0, 4).map((s) => s.replace(/^.*::/, '')).join(' ')}`);
  });
  console.error(`cctors ${all}; reaching a native IC within 6 calls: ${lines.length}; by class (a cctor counts once per class): ${JSON.stringify(tally)}`);
  lines.sort((a, b) => Number(b.split('\t')[1]) - Number(a.split('\t')[1])).forEach((l) => console.error(l));
}
console.error(`roots ${new Set(roots).size} from ${used.length} rows: ${used.join(' | ')}`);
console.error(`closure ${dist.size} methods (depth ${DEPTH}); ICs ${from.size}: ${JSON.stringify(counts)}`);
if (!process.argv.includes('--summary')) console.log(out.join('\n'));
