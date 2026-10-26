// docs/192 (docs/191 step 1): static script-leak scan of the EA script DLLs and every installed mod assembly.
// Finds, from the IL only (no game run):
//   static-collection   static BCL collection with add sites and no remove/clear/reset site
//   singleton-cache     the same on an instance field of a singleton type (a static field of its own type)
//   dead-remove         adds, but every remove/clear/reset sits in a method nothing references
//   world-ref-static    static field holding world objects (GameObject, Sim, SimDescription, Household, Lot, ...)
//                       never cleared on world quit or load: pins the old world across travel / world switch
//   delegate            event / delegate-field subscription (+=, add_X, Delegate.Combine) with no matching -= for the
//                       same handler and no reset of the field (static fields, and instance fields re-added on load)
//   listener            EventTracker.AddListener in a type that never calls RemoveListener
//   alarm               repeating alarm whose handle is discarded (pop): nothing can remove it before owner/world end
// Each candidate gets a trigger (per load / per sim-object / per interaction / per event / per alarm / once per app,
// from forward reach of root methods, depth 4), a growth estimate and a confidence.
// Usage:
//   node tools/script-leak-scan.ts [--game "<Steam>\The Sims 3"] [--user "<Documents>\Electronic Arts\The Sims 3"]
//        [--asm game-copy/assemblies] [--csv docs/data/script-leak-candidates.csv] [--top 30] [--no-mods]
//   node tools/script-leak-scan.ts --self-check      (synthetic fixture: no game files needed)
// Packages are read read-only (header + index + S3SA entries, extract-assemblies.ts s3saImages); extracted mod assemblies
// go to a temp folder deleted at exit. A mod's own copy of a game assembly (same module name) is skipped: the game's is scanned.
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdtempSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { join, relative } from 'node:path';

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };

// ---------- analysis input (pure; the self-check builds one by hand) ----------
export type MInfo = { id: string; asm: string; name: string; virt: boolean; refs: number; ctor: 'cctor' | 'ctor' | ''; guard?: boolean }; // guard: reads and writes one static bool (once-flag)
export type FInfo = { id: string; asm: string; isStatic: boolean; coll: boolean; singleton: boolean; worldRef: boolean };
export type Fact =
  | { k: 'coll'; f: string; op: 'add' | 'key' | 'inner' | 'rem' | 'probe'; m: string; at: number }
  | { k: 'esc'; f: string; m: string; at: number }
  | { k: 'store'; f: string; v: 'null' | 'new' | 'other'; m: string; at: number }
  | { k: 'dlg'; f: string; op: 'add' | 'rem'; h: string; m: string; at: number }
  | { k: 'listen'; op: 'add' | 'rem'; fam: string; m: string; at: number }
  | { k: 'alarm'; rep: boolean; kept: boolean; m: string; at: number };
export type Input = { methods: Map<string, MInfo>; fields: Map<string, FInfo>; facts: Fact[]; edges: Map<string, string[]>; roots: Record<string, string[]>; pkgOf: Map<string, string> };
export type Cand = { asm: string; pkg: string; type: string; member: string; kind: string; adds: number; removes: number; resets: number; trigger: string; growth: string; confidence: 'high' | 'medium' | 'low'; score: number; evidence: string };

// Most frequent first; 'quit' only marks where removes/resets run.
const TRIG = ['interaction', 'event', 'alarm', 'object', 'load', 'app'] as const;
const TRIG_W: Record<string, number> = { interaction: 5, event: 4, alarm: 4, object: 3, load: 3, app: 1, unknown: 1.5 };
const TRIG_TXT: Record<string, string> = { interaction: 'per interaction run', event: 'per event fired', alarm: 'per alarm fire', object: 'per object/Sim created or loaded', load: 'per world load', app: 'once per app start', unknown: 'unknown' };
const CONF_W = { high: 3, medium: 2, low: 1 };
const DEPTH = 4;

export function analyze(I: Input): { cands: Cand[]; stats: Record<string, number> } {
  // Forward reach per trigger label (direct call edges only, DEPTH hops): which roots can run a method.
  const labels = new Map<string, Set<string>>();
  for (const [lab, ids] of Object.entries(I.roots)) {
    let q = ids.filter((i) => I.methods.has(i)); const seen = new Set<string>();
    for (let d = 0; q.length && d <= DEPTH; d++) {
      const next: string[] = [];
      for (const i of q) { if (seen.has(i)) continue; seen.add(i); (labels.get(i) ?? labels.set(i, new Set()).get(i)!).add(lab); next.push(...(I.edges.get(i) ?? [])); }
      q = next;
    }
  }
  const labOf = (m: string) => labels.get(m) ?? new Set<string>();
  const topTrig = (ms: string[]) => { const all = new Set(ms.flatMap((m) => [...labOf(m)])); return TRIG.find((t) => all.has(t)) ?? 'unknown'; };
  const initOnly = (ms: string[]) => ms.every((m) => { const x = I.methods.get(m); const l = labOf(m); return x?.ctor === 'cctor' || (l.size > 0 && [...l].every((t) => t === 'app' || t === 'quit')); });
  const dead = (m: string) => { const x = I.methods.get(m); return !!x && !x.refs && !x.virt && !x.ctor && !labOf(m).size; };
  const quitOrLoad = (m: string) => labOf(m).has('quit') || labOf(m).has('load');
  const split = (id: string) => { const i = id.lastIndexOf('::'); return i < 0 ? ['', id] : [id.slice(0, i), id.slice(i + 2)]; };
  const ev = (fs: { m: string; at: number }[]) => [...new Set(fs.map((f) => `${f.m} IL_${f.at.toString(16).padStart(4, '0')}`))].slice(0, 3).join(' | ');
  const cands: Cand[] = [];
  const stats: Record<string, number> = { facts: I.facts.length, initOnlySkipped: 0, removedOk: 0 };
  const push = (c: Omit<Cand, 'score' | 'pkg'>, worldRef = false, isStatic = true) => {
    const score = +(CONF_W[c.confidence] * TRIG_W[c.trigger] * (isStatic ? 1.2 : 1) * (worldRef ? 1.3 : 1)).toFixed(2);
    cands.push({ ...c, trigger: TRIG_TXT[c.trigger] ?? c.trigger, pkg: I.pkgOf.get(c.asm) ?? 'game', score });
  };
  const by = <T extends Fact>(k: T['k']) => { const o = new Map<string, T[]>(); for (const f of I.facts) if (f.k === k) { const key = 'f' in f ? f.f : 'fam' in f ? f.fam : f.m; (o.get(key) ?? o.set(key, []).get(key)!).push(f as T); } return o; };

  // Collections (static, and instance fields of singletons) + world references in statics.
  const coll = by<Extract<Fact, { k: 'coll' }>>('coll'), esc = by<Extract<Fact, { k: 'esc' }>>('esc'), store = by<Extract<Fact, { k: 'store' }>>('store');
  for (const [fid, fs] of coll) {
    const F = I.fields.get(fid); if (!F || !(F.isStatic || F.singleton)) continue;
    const probes = new Set(fs.filter((f) => f.op === 'probe').map((f) => f.m));
    const adds = fs.filter((f) => f.op !== 'rem' && f.op !== 'probe'), rems = fs.filter((f) => f.op === 'rem');
    if (!adds.length) continue;
    const addMs = adds.map((f) => f.m);
    if (initOnly(addMs) || adds.every((f) => I.methods.get(f.m)?.ctor === (F.isStatic ? 'cctor' : 'ctor'))) { stats.initOnlySkipped++; continue; }
    // A store of null/new outside the initialiser and outside the adding methods (lazy init) resets the collection.
    const resets = (store.get(fid) ?? []).filter((s) => s.v !== 'other' && !I.methods.get(s.m)?.ctor && !addMs.includes(s.m));
    const [type, member] = split(fid), trig = topTrig(addMs), memo = adds.every((f) => f.op === 'key' || probes.has(f.m)), keyed = memo, nEsc = (esc.get(fid) ?? []).length;
    const clears = [...rems, ...resets];
    let kind = F.isStatic ? 'static-collection' : 'singleton-cache', conf: Cand['confidence'];
    if (clears.length) {
      if (clears.every((c) => dead(c.m))) { kind = 'dead-remove'; conf = 'medium'; }
      else if (F.isStatic && F.worldRef && !clears.some((c) => quitOrLoad(c.m))) { kind = 'world-ref-static'; conf = 'low'; }
      else { stats.removedOk++; continue; }
    } else conf = !keyed && !nEsc && trig !== 'unknown' ? 'high' : 'medium';
    // Every add right after a `new` of the field in the same method (fill-once lazy init or rebuild), or behind a static
    // bool once-flag: bounded unless that method also accumulates on later calls.
    const sts = store.get(fid) ?? [];
    const fill = adds.every((a) => sts.some((s) => s.m === a.m && s.v === 'new' && s.at < a.at)), guarded = addMs.every((m) => I.methods.get(m)?.guard);
    if ((fill || guarded) && kind !== 'world-ref-static') conf = 'low';
    const growth = kind === 'world-ref-static' ? 'entries removed in play, but not on world quit: last world\'s objects stay pinned'
      : fill ? 'likely bounded: filled right after a new collection in the same method (fill-once or rebuild)'
      : guarded ? 'likely bounded: adding method behind a static bool once-flag'
      : keyed ?'bounded by distinct keys (memo: indexer set, or add after a ContainsKey/TryGetValue/Contains miss)' : `+1 entry ${TRIG_TXT[trig]} per add site (${adds.length} sites)`;
    push({ asm: F.asm, type, member, kind, adds: adds.length, removes: rems.length, resets: resets.length, trigger: trig, growth, confidence: conf,
      evidence: `add: ${ev(adds)}${clears.length ? `; clear: ${ev(clears)}` : ''}${nEsc ? `; ${nEsc} reads escape (passed on or aliased)` : ''}` }, F.worldRef, F.isStatic);
  }
  for (const [fid, F] of I.fields) {
    if (!F.isStatic || !F.worldRef || F.coll || coll.has(fid)) continue;
    const ss = store.get(fid) ?? [], sets = ss.filter((s) => s.v === 'other' && I.methods.get(s.m)?.ctor !== 'cctor');
    if (!sets.length || ss.some((s) => s.v !== 'other' && quitOrLoad(s.m)) || sets.some((s) => quitOrLoad(s.m))) continue;
    const [type, member] = split(fid);
    push({ asm: F.asm, type, member, kind: 'world-ref-static', adds: sets.length, removes: 0, resets: ss.length - sets.length, trigger: topTrig(sets.map((s) => s.m)),
      growth: 'one object graph (bounded), kept across world quit/travel', confidence: 'low', evidence: `set: ${ev(sets)}` }, true);
  }

  // Delegates: per field (event backing field or delegate field) and handler.
  for (const [fid, fs] of by<Extract<Fact, { k: 'dlg' }>>('dlg')) {
    const F = I.fields.get(fid);
    const isStatic = F ? F.isStatic : false; // every delegate-typed field and event accessor is in fields; a miss is an instance field of a BCL type
    const resets = (store.get(fid) ?? []).filter((s) => s.v === 'null');
    if (resets.some((r) => !dead(r.m))) { stats.removedOk++; continue; }
    const remH = new Set(fs.filter((f) => f.op === 'rem').map((f) => f.h));
    const hs = new Map<string, typeof fs>();
    for (const f of fs) if (f.op === 'add' && !remH.has(f.h) && !remH.has('?')) (hs.get(f.h) ?? hs.set(f.h, []).get(f.h)!).push(f);
    if (!hs.size && fs.some((f) => f.op === 'add')) stats.removedOk++;
    for (const [h, adds] of hs) {
      const addMs = adds.map((f) => f.m);
      if (initOnly(addMs) || (!isStatic && adds.every((f) => I.methods.get(f.m)?.ctor))) { stats.initOnlySkipped++; continue; }
      const trig = topTrig(addMs);
      if (!isStatic && trig !== 'load' && !addMs.some((m) => labOf(m).has('load'))) continue; // instance delegate dies with its object unless re-added on load
      const [type, member] = split(fid);
      const owner = F?.asm ?? I.methods.get(addMs[0])?.asm ?? '';
      push({ asm: I.methods.get(addMs[0])?.asm ?? owner, type, member, kind: 'delegate', adds: adds.length, removes: fs.filter((f) => f.op === 'rem').length, resets: resets.length, trigger: labOf(addMs[0]).has('load') && !isStatic ? 'load' : trig,
        growth: addMs.every((m) => I.methods.get(m)?.guard) ? `likely once: adding method behind a static bool once-flag (handler ${h})`
          : isStatic ? `+1 handler ${TRIG_TXT[trig]} (handler ${h}); each fire runs every copy` : `+1 handler per world load on each saved object (handler ${h}); duplicates persist in the save`,
        confidence: addMs.every((m) => I.methods.get(m)?.guard) ? 'low' : h !== '?' && trig !== 'unknown' ? (isStatic ? 'high' : 'medium') : 'medium', evidence: `add: ${ev(adds)}` }, false, isStatic);
    }
  }

  // EventTracker listeners per type family; repeating alarms whose handle is dropped.
  for (const [fam, fs] of by<Extract<Fact, { k: 'listen' }>>('listen')) {
    const adds = fs.filter((f) => f.op === 'add'); if (!adds.length || fs.some((f) => f.op === 'rem')) continue;
    const addMs = adds.map((f) => f.m); if (initOnly(addMs)) { stats.initOnlySkipped++; continue; }
    push({ asm: I.methods.get(addMs[0])?.asm ?? '', type: fam, member: 'EventTracker.AddListener', kind: 'listener', adds: adds.length, removes: 0, resets: 0, trigger: topTrig(addMs),
      growth: `+1 listener ${TRIG_TXT[topTrig(addMs)]} until its Sim/object is destroyed or the world quits`, confidence: 'low', evidence: `add: ${ev(adds)}` }, false, false);
  }
  for (const [m, fs] of by<Extract<Fact, { k: 'alarm' }>>('alarm')) {
    const lost = fs.filter((f) => f.rep && !f.kept); if (!lost.length || initOnly([m])) continue;
    const [type, member] = split(m.replace(/\(.*$/, ''));
    push({ asm: I.methods.get(m)?.asm ?? '', type, member, kind: 'alarm', adds: lost.length, removes: 0, resets: 0, trigger: topTrig([m]),
      growth: `+1 repeating alarm ${TRIG_TXT[topTrig([m])]}, handle dropped: ends only with its owner or the world`, confidence: topTrig([m]) === 'unknown' ? 'low' : 'medium', evidence: `add: ${ev(lost)}` }, false, false);
  }
  cands.sort((a, b) => b.score - a.score || a.type.localeCompare(b.type));
  for (const c of cands) stats[c.kind] = (stats[c.kind] ?? 0) + 1;
  return { cands, stats };
}

export function toCsv(cs: Cand[]): string {
  const cols = ['assembly', 'package', 'type', 'member', 'kind', 'add_sites', 'remove_sites', 'reset_sites', 'trigger', 'growth', 'confidence', 'score', 'evidence'];
  return cols.join(',') + '\n' + cs.map((c) => [c.asm, c.pkg, c.type, c.member, c.kind, c.adds, c.removes, c.resets, c.trigger, c.growth, c.confidence, c.score, c.evidence]
    .map((v) => typeof v === 'number' ? String(v) : JSON.stringify(v)).join(',')).join('\n') + '\n';
}

// ---------- self-check: synthetic fixture ----------
function selfCheck(): void {
  const M = (id: string, o: Partial<MInfo> = {}): [string, MInfo] => [id, { id, asm: 'Fx', name: id.replace(/^.*::|\(.*$/g, ''), virt: false, refs: 1, ctor: '', ...o }];
  const F = (id: string, o: Partial<FInfo> = {}): [string, FInfo] => [id, { id, asm: 'Fx', isStatic: true, coll: true, singleton: false, worldRef: false, ...o }];
  const I: Input = {
    methods: new Map([M('A::OnWorldLoadFinished()'), M('A::Track(Sim)'), M('A::.cctor()', { ctor: 'cctor' }), M('B::Run()'), M('B::Forget()', { refs: 0 }),
      M('C::OnLoad()'), M('C::OnFire(object,EventArgs)'), M('D::Cache(int)'), M('D::OnWorldQuit()'), M('E::Arm()'), M('G::Set(Sim)'), M('G::Reset()'), M('G::Hook()', { guard: true })]),
    fields: new Map([F('A::sSeen'), F('B::sParse'), F('B::sFill'), F('A::sInit'), F('B::sLog'), F('D::sMemo'), F('G::sSims', { worldRef: true }), F('G::sLast', { coll: false, worldRef: true }),
      F('C::mOnFire', { isStatic: false, coll: false }), F('World::sOnQuit', { coll: false })]),
    facts: [
      { k: 'coll', f: 'A::sSeen', op: 'add', m: 'A::Track(Sim)', at: 4 },                 // per load via edge, never removed: high
      { k: 'coll', f: 'A::sInit', op: 'add', m: 'A::.cctor()', at: 2 },                   // init only: skipped
      { k: 'coll', f: 'B::sLog', op: 'add', m: 'B::Run()', at: 8 },                       // per interaction, remove only in dead method
      { k: 'coll', f: 'B::sLog', op: 'rem', m: 'B::Forget()', at: 1 },
      { k: 'coll', f: 'D::sMemo', op: 'key', m: 'D::Cache(int)', at: 3 },                 // cleared on quit: not a candidate
      { k: 'coll', f: 'B::sParse', op: 'probe', m: 'B::Run()', at: 1 },                  // add after a miss: memo, medium
      { k: 'coll', f: 'B::sParse', op: 'add', m: 'B::Run()', at: 5 },
      { k: 'store', f: 'B::sFill', v: 'new', m: 'B::Run()', at: 10 },                    // add after a new in the same method: low
      { k: 'coll', f: 'B::sFill', op: 'add', m: 'B::Run()', at: 12 },
      { k: 'dlg', f: 'World::sOnQuit', op: 'add', h: 'G::Once', m: 'G::Hook()', at: 2 },   // per load but once-flag guarded: low
      { k: 'store', f: 'D::sMemo', v: 'new', m: 'D::OnWorldQuit()', at: 0 },
      { k: 'coll', f: 'G::sSims', op: 'add', m: 'G::Set(Sim)', at: 5 },                   // world refs removed in play only
      { k: 'coll', f: 'G::sSims', op: 'rem', m: 'G::Reset()', at: 6 },
      { k: 'store', f: 'G::sLast', v: 'other', m: 'G::Set(Sim)', at: 9 },                 // world ref static
      { k: 'dlg', f: 'World::sOnQuit', op: 'add', h: 'A::OnQuit', m: 'A::OnWorldLoadFinished()', at: 7 }, // per-load +=: high
      { k: 'dlg', f: 'C::mOnFire', op: 'add', h: 'C::OnFire', m: 'C::OnLoad()', at: 2 },  // instance re-add on load: medium
      { k: 'dlg', f: 'World::sOnQuit', op: 'add', h: 'A::Once', m: 'A::.cctor()', at: 1 }, // once: skipped
      { k: 'alarm', rep: true, kept: false, m: 'E::Arm()', at: 3 },
      { k: 'alarm', rep: true, kept: true, m: 'B::Run()', at: 9 },
    ],
    edges: new Map([['A::OnWorldLoadFinished()', ['A::Track(Sim)', 'G::Hook()']], ['B::Run()', ['E::Arm()', 'G::Set(Sim)']], ['G::Reset()', []]]),
    roots: { load: ['A::OnWorldLoadFinished()', 'C::OnLoad()'], interaction: ['B::Run()'], quit: ['D::OnWorldQuit()'], app: ['A::.cctor()'], event: ['G::Reset()'] },
    pkgOf: new Map(),
  };
  const { cands, stats } = analyze(I);
  const get = (m: string) => cands.find((c) => c.member === m);
  const want = (m: string, kind: string, conf: string, trig: string) => { const c = get(m); console.assert(c?.kind === kind && c.confidence === conf && c.trigger === TRIG_TXT[trig], `${m}: ${JSON.stringify(c)}`); if (!(c?.kind === kind && c.confidence === conf)) process.exitCode = 1; };
  want('sSeen', 'static-collection', 'high', 'load');
  want('sLog', 'dead-remove', 'medium', 'interaction');
  want('sParse', 'static-collection', 'medium', 'interaction');
  console.assert(get('sParse')?.growth.startsWith('bounded'), 'sParse memo growth');
  want('sSims', 'world-ref-static', 'low', 'interaction');
  want('sLast', 'world-ref-static', 'low', 'interaction');
  want('mOnFire', 'delegate', 'medium', 'load');
  want('Arm', 'alarm', 'medium', 'interaction');
  want('sFill', 'static-collection', 'low', 'interaction');
  const quit = cands.filter((c) => c.member === 'sOnQuit');
  const qh = quit.find((c) => c.growth.includes('A::OnQuit')), qg = quit.find((c) => c.growth.includes('G::Once'));
  if (quit.length !== 2 || qh?.confidence !== 'high' || qg?.confidence !== 'low' || !qg.growth.startsWith('likely once')) { console.error('sOnQuit', quit); process.exitCode = 1; }
  for (const m of ['sInit', 'sMemo']) if (get(m)) { console.error(`${m} must not be a candidate`); process.exitCode = 1; }
  if (cands[0].score < cands[cands.length - 1].score) process.exitCode = 1;
  const csv = toCsv(cands);
  if (csv.split('\n')[0] !== 'assembly,package,type,member,kind,add_sites,remove_sites,reset_sites,trigger,growth,confidence,score,evidence' || csv.trim().split('\n').length !== cands.length + 1) process.exitCode = 1;
  console.log(`script-leak-scan self-check: ${process.exitCode ? 'FAIL' : 'PASS'} (${cands.length} candidates, ${stats.initOnlySkipped} init-only skipped)`);
}

// ---------- extraction from the assemblies ----------
async function scan(): Promise<void> {
  const { s3saImages, moduleName } = await import('./extract-assemblies.ts');
  const CG = await import('./managed-callgraph.ts');
  const GAME = arg('--game', process.env.TS3_GAME ?? 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3');
  const USER = arg('--user', process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3'));
  const ASM = arg('--asm', 'game-copy/assemblies');
  const walk = (dir: string, ext: RegExp): string[] => !existsSync(dir) ? [] : readdirSync(dir, { withFileTypes: true })
    .flatMap((e) => e.isDirectory() ? walk(join(dir, e.name), ext) : ext.test(e.name) ? [join(dir, e.name)] : []);
  const tmp = mkdtempSync(join(tmpdir(), 'leakscan-'));
  const pkgOf = new Map<string, string>(); const replaced: string[] = []; let pkgs = 0, unreadable = 0, dup = 0;
  try {
    const gameNames = new Set(readdirSync(ASM).filter((n) => n.endsWith('.dll')).map((n) => n.replace(/\.dll$/, '')));
    for (const n of gameNames) copyFileSync(join(ASM, `${n}.dll`), join(tmp, `${n}.dll`));
    if (!process.argv.includes('--no-mods')) {
      const roots = [['mods', join(USER, 'Mods')], ['dccache', join(USER, 'DCCache')], ['dcbackup', join(USER, 'DCBackup')], ['game', GAME]] as const;
      const seen = new Set<string>();
      for (const [tag, dir] of roots) for (const f of walk(dir, tag === 'dccache' ? /\.(dbc|package)$/i : /\.package$/i)) {
        pkgs++;
        let imgs; try { imgs = s3saImages(f); } catch { unreadable++; continue; }
        for (const { img } of imgs) {
          if (!img) continue;
          const name = (moduleName(img) ?? 'unknown.dll').replace(/\.dll$/i, ''), h = createHash('sha1').update(img).digest('hex');
          if (seen.has(h)) { dup++; continue; } seen.add(h);
          const label = `${tag}:${relative(dir, f).split('\\').join('/')}`;
          if (gameNames.has(name)) { if (tag !== 'game') replaced.push(`${label} (${name})`); continue; }
          const file = `zz${String(pkgOf.size).padStart(4, '0')}_${name}`;
          writeFileSync(join(tmp, `${file}.dll`), img); pkgOf.set(file, label);
        }
      }
    }
    const skipped: string[] = [];
    const T = CG.typeModel(tmp, skipped);
    const asmName = (a: { name: string }) => a.name.replace(/^zz\d{4}_/, '');
    const pkgByName = new Map([...pkgOf].map(([f, l]) => [f.replace(/^zz\d{4}_/, ''), l]));
    const isA = (t: CG.TypeDef | undefined, base: string, ifaces = false) => { for (let c = t, n = 0; c && n < 60; c = T.baseOf(c), n++) if (c.full === base || (ifaces && T.ifaceClosure(c).has(T.typeByFull.get(base)!))) return true; return false; };
    const WORLD = ['Sims3.Gameplay.Abstracts.GameObject', 'Sims3.Gameplay.Interfaces.IGameObject', 'Sims3.Gameplay.CAS.SimDescription', 'Sims3.Gameplay.CAS.Household',
      'Sims3.Gameplay.Interactions.InteractionInstance', 'Sims3.Gameplay.Socializing.Relationship', 'Sims3.Gameplay.Core.Lot'].filter((n) => T.typeByFull.has(n));
    const COLL = /^System\.Collections\.(Generic\.)?(List|Dictionary|HashSet|Queue|Stack|SortedList|SortedDictionary|LinkedList|ArrayList|Hashtable)\b/;
    const worldCache = new Map<string, boolean>();
    const worldRef = (ty: string) => { let v = worldCache.get(ty); if (v === undefined) { v = (ty.match(/Sims3\.[\w.+]+/g) ?? []).some((n) => { const t = T.typeByFull.get(n); return !!t && WORLD.some((w) => isA(t, w, true)); }); worldCache.set(ty, v); } return v; };

    const isDlg = (ty: string) => { const t = T.typeByFull.get(ty.replace(/<.*$/s, '')); return !!t && T.baseOf(t)?.full === 'System.MulticastDelegate'; };
    const methods = new Map<string, MInfo>(), fields = new Map<string, FInfo>(), facts: Fact[] = [], edges = new Map<string, string[]>();
    const roots: Record<string, string[]> = { load: [], app: [], quit: [], interaction: [], event: [], alarm: [], object: [] };
    const all = T.asms.filter((a) => !['mscorlib', 'System', 'System.Xml'].includes(a.name));
    // Fields: statics, collections, singletons (a static field of the type's own type in the type).
    for (const a of all) for (const t of a.types) for (const f of t.fields) {
      const ty = T.fieldType(f), isStatic = !!(f.flags & 0x10), coll = COLL.test(ty);
      if (f.flags & 0x40) continue; // literal const
      if (!isStatic && !coll && !isDlg(ty)) continue;
      fields.set(f.id, { id: f.id, asm: asmName(a), isStatic, coll, singleton: false, worldRef: isStatic && worldRef(ty) });
    }
    for (const a of all) for (const t of a.types) if (t.fields.some((f) => f.flags & 0x10 && T.fieldType(f) === t.full && /instance|singleton/i.test(f.name))) for (const f of t.fields) { const F = fields.get(f.id); if (F && !F.isStatic) F.singleton = true; }
    const WORLD_EVT = /^Sims3\.SimIFace\.World::add_On(WorldLoadFinished|WorldLoadStarted|FinishedLoadingWorldFile|FinishedLoadingSaveGame)EventHandler\(/;
    const accessorField = new Map<string, string>(); // add_X / remove_X method id -> backing field
    const pendingEvt: { tgt: string; op: 'add' | 'rem'; h: string; m: string; at: number; st: boolean; asm: string }[] = [];
    const refs = new Map<string, number>();
    const ROLE = (m: CG.Method) => {
      const n = m.name;
      if (n === '.cctor') return 'app';
      if (/^(OnWorldLoadFinished|OnWorldLoadStarted|OnPreLoad|OnPostLoad|OnLoad|PostLoad|OnLoadFixup|OnFinishedLoading\w*|OnWorldLoad\w*)$/.test(n)) return 'load';
      if (/^(OnWorldQuit|OnQuitApp|OnWorldQuitEventHandler|Cleanup|OnCleanUp|OnShutdown)$/.test(n)) return 'quit';
      if (/^OnStartupApp/.test(n)) return 'app';
      if (/^(Run|RunFromInventory|Perform)$/.test(n) && isA(m.type, 'Sims3.Gameplay.Interactions.InteractionInstance')) return 'interaction';
      if (/^(OnStartup|OnCreation|.ctor)$/.test(n) && (isA(m.type, 'Sims3.Gameplay.Abstracts.GameObject') || isA(m.type, 'Sims3.Gameplay.CAS.SimDescription'))) return 'object';
      return '';
    };
    const handlerBefore = (ins: CG.FullIns[], j: number, asm: CG.Asm, back: number) => {
      for (let i = j - 1; i >= Math.max(0, j - back); i--) if (ins[i].op === 0xfe06 || ins[i].op === 0xfe07) return CG.resolveMethodToken(asm, ins[i].tok).m?.id.replace(/\(.*$/, '') ?? '?';
      return '?';
    };
    // A class shipped twice (game Sims3StoreObjects and a store-content repack) gives equal method ids: qualify those.
    const idCount = new Map<string, number>(); for (const a of all) for (const m of a.methods) if (m) idCount.set(m.id, (idCount.get(m.id) ?? 0) + 1);
    const K = (m: CG.Method) => (idCount.get(m.id) ?? 0) > 1 ? `${m.id}@${asmName(m.asm)}` : m.id;
    for (const a of all) for (const m of a.methods) {
      if (!m) continue;
      const bR = new Set<string>(), bW = new Set<string>(); // static bools read and written here: a once-guard
      methods.set(K(m), { id: K(m), asm: asmName(a), name: m.name, virt: !!(m.flags & 0x40), refs: 0, ctor: m.name === '.cctor' ? 'cctor' : m.name === '.ctor' ? 'ctor' : '' });
      const role = ROLE(m); if (role) roots[role].push(K(m));
      const il = CG.methodIl(m.gi); if (!il) continue;
      const { ins, sites } = il; const out = new Set<string>();
      const recvSrc = new Set(sites.map((s) => s.src));
      const siteAt = new Map(sites.map((s) => [s.j, s]));
      const fieldAt = (i: number) => i >= 0 && (ins[i].op === 0x7e || ins[i].op === 0x7b) ? CG.resolveFieldToken(a, ins[i].tok).id : undefined;
      const accessor = /^(add|remove)_/.test(m.name);
      // Event on a long-lived receiver: a static field (Responder.Instance style) or a static Instance/Singleton getter.
      const staticRecv = (s?: CG.CallSite) => { if (!s || s.src < 0) return false; const p = ins[s.src];
        if (p.op === 0x7e) return true;
        const g = p.op === 0x28 ? CG.resolveMethodToken(a, p.tok).m : undefined; return !!g && !!(g.flags & 0x10) && /^get_\w*(Instance|Singleton|Global|Manager)$/.test(g.name); };
      for (let j = 0; j < ins.length; j++) {
        const x = ins[j], op = x.op;
        if (op === 0x7e || op === 0x80) { const r = CG.resolveFieldToken(a, x.tok); if (r.type === 'bool') (op === 0x7e ? bR : bW).add(r.id); }
        if (op === 0x28 || op === 0x6f || op === 0x73 || op === 0xfe06 || op === 0xfe07) {
          const t = CG.resolveMethodToken(a, x.tok).m; if (!t) continue;
          if (op === 0xfe06 || op === 0xfe07) { refs.set(K(t), (refs.get(K(t)) ?? 0) + 1); continue; }
          out.add(K(t));
          const tf = t.type.full, n = t.name;
          if (COLL.test(tf) && op !== 0x73) {
            const s = siteAt.get(j); if (!s) continue;
            const add = /^(Add|AddRange|Insert|InsertRange|Push|Enqueue|AddFirst|AddLast|AddBefore|AddAfter|UnionWith)$/.test(n), key = n === 'set_Item';
            const rem = /^(Remove|RemoveAt|RemoveAll|RemoveRange|RemoveWhere|RemoveFirst|RemoveLast|Clear|Pop|Dequeue|ExceptWith|IntersectWith)$/.test(n);
            const probe = /^(ContainsKey|TryGetValue|Contains|get_Count)$/.test(n); // get_Count: "fill once when empty" guard, also any count read (shortcut: lowers confidence only)
            if (!add && !key && !rem && !probe) continue;
            let f = fieldAt(s.src), inner = false;
            // sDict[k].Add(x) / sDict.get_Item(k).Add: growth of a value collection inside the field's collection.
            if (!f && s.src >= 0 && siteAt.get(s.src) && COLL.test(siteAt.get(s.src)!.target.type.full)) { f = fieldAt(siteAt.get(s.src)!.src); inner = true; }
            // inner remove (sDict[k].Remove) leaves the outer key: not a clear of the field.
            if (f && !((rem || probe) && inner)) facts.push({ k: 'coll', f, op: probe ? 'probe' : rem ? 'rem' : inner ? 'inner' : key ? 'key' : 'add', m: K(m), at: x.at });
          } else if (tf === 'System.Delegate' && (n === 'Combine' || n === 'Remove')) {
            let st = -1; for (let i = j + 1; i <= Math.min(ins.length - 1, j + 2); i++) if (ins[i].op === 0x80 || ins[i].op === 0x7d) { st = i; break; }
            if (st < 0) continue;
            const f = CG.resolveFieldToken(a, ins[st].tok).id;
            if (!fields.has(f)) fields.set(f, { id: f, asm: asmName(a), isStatic: ins[st].op === 0x80, coll: false, singleton: false, worldRef: false }); // field of a type outside the model (replaced module)
            if (accessor) accessorField.set(K(m), f);
            else facts.push({ k: 'dlg', f, op: n === 'Combine' ? 'add' : 'rem', h: handlerBefore(ins, j, a, 6), m: K(m), at: x.at });
          } else if (/^(add|remove)_/.test(n) && !accessor) {
            const h = handlerBefore(ins, j, a, 4);
            pendingEvt.push({ tgt: K(t), op: n.startsWith('add_') ? 'add' : 'rem', h, m: K(m), at: x.at, st: !!(t.flags & 0x10) || staticRecv(siteAt.get(j)), asm: asmName(t.asm) });
            if (n.startsWith('add_') && h !== '?') {
              const hm = [...a.methods].find((y) => y && y.id.startsWith(h + '(')); // handler in the same assembly (the usual case)
              const hid = (hm && K(hm)) ?? h;
              if (WORLD_EVT.test(K(t))) roots.load.push(hid); else if (/World::add_On(StartupApp)/.test(K(t))) roots.app.push(hid); else if (/World::add_On(WorldQuit|QuitApp)/.test(K(t))) roots.quit.push(hid);
            }
          } else if (tf === 'Sims3.Gameplay.EventSystem.EventTracker' && /^(AddListener|RemoveListener)$/.test(n)) {
            const fam = m.type.full.split('+')[0];
            facts.push({ k: 'listen', op: n === 'AddListener' ? 'add' : 'rem', fam, m: K(m), at: x.at });
            const h = handlerBefore(ins, j, a, 6); if (n === 'AddListener' && h !== '?') { const hm = a.methods.find((y) => y && y.id.startsWith(h + '(')); if (hm) roots.event.push(K(hm)); }
          } else if (tf === 'Sims3.Gameplay.Utilities.AlarmManager' && /^AddAlarm/.test(n)) {
            facts.push({ k: 'alarm', rep: /Repeating|^AddAlarmDay$/.test(n), kept: ins[j + 1]?.op !== 0x26, m: K(m), at: x.at });
            const h = handlerBefore(ins, j, a, 8); if (h !== '?') { const hm = a.methods.find((y) => y && y.id.startsWith(h + '(')); if (hm) roots.alarm.push(K(hm)); }
          }
        } else if (op === 0x80 || op === 0x7d) {
          const f = CG.resolveFieldToken(a, x.tok).id;
          if (!fields.has(f)) continue;
          const p = ins[j - 1]?.op;
          facts.push({ k: 'store', f, v: p === 0x14 ? 'null' : p === 0x73 ? 'new' : 'other', m: K(m), at: x.at });
        } else if (op === 0x7e && !recvSrc.has(j)) {
          const f = CG.resolveFieldToken(a, x.tok).id, nx = ins[j + 1]?.op;
          // passed on (call arg) or aliased (stloc/stfld/ret): a clear elsewhere is invisible to this scan
          if (fields.get(f)?.coll && nx !== undefined && (nx === 0x28 || nx === 0x6f || nx === 0x73 || (nx >= 0x0a && nx <= 0x0d) || nx === 0x13 || nx === 0x7d || nx === 0x2a)) facts.push({ k: 'esc', f, m: K(m), at: x.at });
        } else if (op === 0x7f) { // ldsflda F; call Array.Resize = grown by copy
          const nx = ins.slice(j + 1, j + 4).find((y) => y.op === 0x28);
          const t = nx && CG.resolveMethodToken(a, nx.tok).m;
          if (t?.type.full === 'System.Array' && t.name === 'Resize') { const f = CG.resolveFieldToken(a, x.tok).id; const F = fields.get(f); if (F) { F.coll = true; facts.push({ k: 'coll', f, op: 'add', m: K(m), at: x.at }); } }
        }
      }
      edges.set(K(m), [...out]);
      if ([...bW].some((f) => bR.has(f))) methods.get(K(m))!.guard = true;
      for (const o of out) refs.set(o, (refs.get(o) ?? 0) + 1);
    }
    for (const [id, n] of refs) { const x = methods.get(id); if (x) x.refs = n; }
    // Event add_/remove_ calls -> the accessor's backing field (event id when the accessor has no Combine body).
    for (const e of pendingEvt) {
      let f = accessorField.get(e.tgt.replace(/::remove_/, '::add_')) ?? accessorField.get(e.tgt);
      if (!f) { f = e.tgt.replace(/\(.*$/, '').replace(/::(add|remove)_/, '::'); if (!fields.has(f)) fields.set(f, { id: f, asm: e.asm, isStatic: e.st, coll: false, singleton: false, worldRef: false }); }
      facts.push({ k: 'dlg', f, op: e.op, h: e.h, m: e.m, at: e.at });
    }
    const pkgMap = new Map<string, string>([...pkgByName]);
    const { cands, stats } = analyze({ methods, fields, facts, edges, roots, pkgOf: pkgMap });
    const out = arg('--csv', 'docs/data/script-leak-candidates.csv');
    writeFileSync(out, toCsv(cands));
    const top = Number(arg('--top', '30'));
    for (const c of cands.slice(0, top)) console.log(`${c.score}\t${c.confidence}\t${c.kind}\t${c.asm}\t${c.type}::${c.member}\t${c.trigger}\t${c.evidence.slice(0, 160)}`);
    const byAsm: Record<string, number> = {}; for (const c of cands) byAsm[c.asm] = (byAsm[c.asm] ?? 0) + 1;
    const byConf: Record<string, number> = {}; for (const c of cands) byConf[c.confidence] = (byConf[c.confidence] ?? 0) + 1;
    console.log(JSON.stringify({ packages: pkgs, unreadable, modAssemblies: pkgOf.size, duplicateImages: dup, replacedGameModules: replaced, unparsed: skipped,
      methods: methods.size, fieldsTracked: fields.size, roots: Object.fromEntries(Object.entries(roots).map(([k, v]) => [k, v.length])), candidates: cands.length, byConf, ...stats, byAsm }, null, 1));
  } finally { rmSync(tmp, { recursive: true, force: true }); }
}

if (process.argv.includes('--self-check')) selfCheck();
else await scan();
