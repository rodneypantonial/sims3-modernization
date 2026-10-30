// docs/132: which installed script assemblies can change what the [AutShadow] worker runs (docs/122 v0 + docs/126 tier 2).
// Reads every .package / .dbc READ-ONLY (Steam install, Documents\...\Mods, DCCache, DCBackup), pulls S3SA assemblies,
// and lists per package: assemblies not on the known-MVID list (the game's own, from the call-graph index), and every
// type that derives from or implements an allow-listed definition / implementor / meta interface, or overrides a virtual
// (or implements an interface method) that the allow-listed closures call. Effect column: fallback (skip) or worker-affecting.
// Usage:
//   node tools/offload-override-scan.ts [--game "<Steam>\The Sims 3"] [--user "<Documents>\Electronic Arts\The Sims 3"]
//        [--asm game-copy/assemblies] [--index game-copy/analysis/callgraph/index.json] [--csv out.csv] [--summary]
//   node tools/offload-override-scan.ts --self-check [--asm ...]
// Env TS3_GAME / TS3_USER override the default folders. Extracted assemblies go to a temp folder that is deleted at exit.
import { copyFileSync, existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { join, relative } from 'node:path';
import { moduleName, mvidOf, s3saImages, type Img } from './extract-assemblies.ts';
import { TYPE_S3SA, fnv64, s3saEncode, writePackage } from './dbpf.ts';
import { defRoots, typeModel, type Index, type Method, type TypeDef } from './managed-callgraph.ts';

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const GAME = arg('--game', process.env.TS3_GAME ?? 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3');
const USER = arg('--user', process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3'));
const ASM = arg('--asm', 'game-copy/assemblies');
const INDEX = arg('--index', 'game-copy/analysis/callgraph/index.json');
const DEPTH = 12;
const BCL = new Set(['mscorlib', 'System', 'System.Xml']);
// AutShadow's session MVID guard covers these two (AutShadow.cs MvidCheck); other replaced game modules are unchecked.
const MVID_GUARDED = new Set(['Sims3GameplaySystems', 'Sims3GameplayObjects']);
const IMPL = ['Sims3.Gameplay.Core.Lot', 'Sims3.Gameplay.Abstracts.RabbitHole', 'Sims3.Gameplay.Autonomy.HomeMetaObject',
  'Sims3.Gameplay.Autonomy.WorkMetaObject', 'Sims3.Gameplay.Autonomy.GardeningMetaObject', 'Sims3.Gameplay.Pools.WorldPondHelper',
  'Sims3.Gameplay.Objects.Miscellaneous.BakeSaleTable'];
const META_IFACES = ['Sims3.Gameplay.Interfaces.IMetaObject', 'Sims3.Gameplay.Interactions.IMetaInteractionDefinition',
  'Sims3.Gameplay.Interactions.IOverridesCalculateScore'];
// Same cuts as tools/autonomy-tier2.ts (docs/121 §1, pilot worker hooks, docs/126 §3 H7-H10): the worker answers there.
const CUT = ['Sims3.Gameplay.Seasons.SeasonsManager::LazyAllocateManager(', 'Sims3.Gameplay.Socializing.Relationship::Get(',
  'Sims3.Gameplay.Actors.Sim::IsAllowedInRoom(', 'Sims3.Gameplay.Actors.Sim::get_NumSimsDoingGardening(',
  'Sims3.Gameplay.Utilities.Localization::LocalizeString(', 'Sims3.Gameplay.Utilities.OnlineDatingManager::DidSimsMeetOnline(',
  'Sims3.Gameplay.Autonomy.MetaAutonomyTuning::GetSimCountForVenueWithCustomDesireFunction(',
  'Sims3.Gameplay.ChildAndTeenUpdates.Punishment::GetPunishmentManagerFromSim(',
  'Sims3.Gameplay.UI.LiveDragHelperModel::get_CachedTopDraggedObject(', 'Sims3.Gameplay.UI.LiveDragHelperModel::get_TopDraggedObject(',
  'Sims3.Gameplay.Autonomy.Autonomy::UpdateCacheIfNeeded(', 'Sims3.Gameplay.Autonomy.InteractionObjectPair::AddTuning(',
  'Sims3.Gameplay.Autonomy.Autonomy::get_IsSufficientlyOnScreenForHighLODSimulation('];
const EFFECT = {
  core: 'worker off for the session (MVID guard)',
  unchecked: 'replaces a game module the MVID guard does not check: L3 per method only (risk)',
  def: 'fallback: skip ad (exact runtime definition type not allow-listed; game B14 on the sim thread)',
  impl: 'fallback: skip object (implementor not one of the 7 EA types; game B14 on the sim thread)',
  meta: 'fallback: skip ad (meta definition not allow-listed)',
  vcall: 'worker runs mod code when it meets this type: EA ICs without a shim abort (ic), managed effects unguarded (Mode R A2/A3, go-live risk)',
} as const;
type Kind = keyof typeof EFFECT;
const AFFECTS: Kind[] = ['core', 'unchecked', 'vcall'];

const walk = (dir: string, ext: RegExp): string[] => !existsSync(dir) ? [] : readdirSync(dir, { withFileTypes: true })
  .flatMap((e) => e.isDirectory() ? walk(join(dir, e.name), ext) : ext.test(e.name) ? [join(dir, e.name)] : []);

// ---------- self-check ----------
if (process.argv.includes('--self-check')) {
  const tmp = mkdtempSync(join(tmpdir(), 'oscan-'));
  try {
    const dll = readFileSync(join(ASM, 'ScriptCore.dll'));
    const pkg = join(tmp, 'a.package');
    writeFileSync(pkg, writePackage([{ type: 0x0333406c, group: 0, instance: 1n, data: Buffer.from('x') },
      { type: TYPE_S3SA, group: 0, instance: fnv64('ScriptCore'), data: s3saEncode(dll) }]));
    const got = s3saImages(pkg);
    if (got.length !== 1 || got[0].inst !== fnv64('ScriptCore') || !got[0].img || moduleName(got[0].img) !== 'ScriptCore.dll') throw new Error('S3SA round trip');
    const ix: Index = JSON.parse(readFileSync(INDEX, 'utf8'));
    if (!ix.asms.some((a) => a.name === 'ScriptCore' && a.mvid === mvidOf(got[0].img!))) throw new Error('MVID of ScriptCore not the index MVID');
    console.log('offload-override-scan self-check: PASS');
  } finally { rmSync(tmp, { recursive: true, force: true }); }
  process.exit(0);
}

// ---------- scan ----------
const ix: Index = JSON.parse(readFileSync(INDEX, 'utf8'));
const known = new Map(ix.asms.map((a) => [a.mvid, a.name]));
const gameNames = new Set(ix.asms.map((a) => a.name));
const roots = [['game', GAME], ['mods', join(USER, 'Mods')], ['dccache', join(USER, 'DCCache')], ['dcbackup', join(USER, 'DCBackup')]] as const;
const pkgs = roots.flatMap(([tag, dir]) => walk(dir, tag === 'dccache' ? /\.(dbc|package)$/i : /\.package$/i).map((f) => ({ tag, dir, f })));
const label = (p: { tag: string; dir: string; f: string }) => `${p.tag}:${relative(p.dir, p.f).split('\\').join('/')}`;

type Row = { pkg: string; asm: string; type: string; what: string; kind: Kind };
const rows: Row[] = [];
const asmInfo: { pkg: string; name: string; mvid: string; known: boolean }[] = [];
const tmp = mkdtempSync(join(tmpdir(), 'oscan-'));
const fileOwner = new Map<string, { pkg: string; name: string }>();
let unreadable = 0;
try {
  for (const p of pkgs) {
    let imgs: Img[];
    try { imgs = s3saImages(p.f); } catch { unreadable++; continue; }
    for (const { img, inst } of imgs) {
      if (!img) { asmInfo.push({ pkg: label(p), name: `${inst.toString(16)} (no PE)`, mvid: '', known: false }); continue; }
      const name = (moduleName(img) ?? `${inst.toString(16)}.dll`).replace(/\.dll$/i, ''), mvid = mvidOf(img);
      const isKnown = known.has(mvid);
      asmInfo.push({ pkg: label(p), name, mvid, known: isKnown });
      if (isKnown) continue;
      if (gameNames.has(name)) { rows.push({ pkg: label(p), asm: name, type: '(assembly)', what: `replaces ${name}`, kind: MVID_GUARDED.has(name) ? 'core' : 'unchecked' }); continue; }
      const file = `zz${String(fileOwner.size).padStart(4, '0')}_${name}.dll`; // sorts after every game assembly
      writeFileSync(join(tmp, file), img);
      fileOwner.set(file.replace(/\.dll$/, ''), { pkg: label(p), name });
    }
  }
  for (const f of readdirSync(ASM).filter((n) => n.endsWith('.dll'))) copyFileSync(join(ASM, f), join(tmp, f));
  const skipped: string[] = [];
  const M = typeModel(tmp, skipped);
  const isMod = (t: TypeDef) => fileOwner.has(t.asm.name);
  const type = (n: string) => { const t = M.typeByFull.get(n); if (!t) throw new Error(`no type ${n}`); return t; };

  // Allow-listed definitions: docs/122 v0 ('in') + docs/126 tier 2 ('ready'/'new-rows' rows that name a type).
  const csv = (f: string) => readFileSync(f, 'utf8').trim().split(/\r?\n/).slice(1).map((l) => l.split(','));
  const defs = new Set([...csv('docs/data/autonomy-allow.csv').filter((r) => r[5] === 'in').map((r) => r[0]),
    ...csv('docs/data/autonomy-tier2.csv').filter((r) => /^(ready|new-rows)$/.test(r[13]) && !/\s/.test(r[2])).map((r) => r[2])]);
  const defTypes = new Set([...defs].map(type)), implTypes = new Set(IMPL.map(type)), metaIfaces = new Set(META_IFACES.map(type));

  // Virtuals and interface methods the allow-listed closures call (declared targets of virt/iface/cons edges, depth 12).
  // shortcut: ifaceN edges (pre-resolved to 1-4 game classes) and BCL-declared virtuals (ToString/Equals/GetHashCode) are not
  // counted; add them if a Mode R mismatch ever traces to one.
  const virt = new Set<string>(), seen = new Set<number>();
  let q = [...defs].flatMap((d) => defRoots(ix, d)).filter((i) => i !== undefined);
  const dyn = new Set(['virt', 'iface', 'cons']), dlg = ix.kinds.indexOf('dlg');
  for (let d = 0; q.length && d <= DEPTH; d++) {
    const next: number[] = [];
    for (const i of q) {
      if (seen.has(i)) continue; seen.add(i);
      const c = ix.methods[i].c;
      for (let k = 0; k < c.length; k += 2) {
        if (c[k + 1] === dlg || CUT.some((x) => ix.methods[c[k]].k.startsWith(x))) continue;
        if (dyn.has(ix.kinds[c[k + 1]]) && !BCL.has(ix.asms[ix.methods[c[k]].a]?.name)) virt.add(ix.methods[c[k]].k);
        next.push(c[k]);
      }
    }
    q = next;
  }

  const idef = type('Sims3.Gameplay.Interactions.InteractionDefinition');
  let selfCovered = 0;
  const chain = (t: TypeDef) => { const o: TypeDef[] = []; for (let b = M.baseOf(t); b; b = M.baseOf(b)) o.push(b); return o; };
  const ifsOf = (t: TypeDef) => { const s = new Set<TypeDef>(); for (const c of [t, ...chain(t)]) for (const i of M.ifaceClosure(c)) s.add(i); return s; };
  for (const t of M.asms.flatMap((a) => a.types)) {
    if (!isMod(t) || M.isIface(t) || t.name === '<Module>') continue;
    const own = fileOwner.get(t.asm.name)!, add = (what: string, kind: Kind) => rows.push({ pkg: own.pkg, asm: own.name, type: t.full, what, kind });
    // A definition (InteractionDefinition is an interface) is reached by the worker only as the scored pair's definition,
    // and the worker scores exact allow-listed types only, so a mod definition's virtuals never run there.
    // shortcut: assumes no closure method calls another definition's virtuals or interfaces; upgrade if one does.
    const bases = chain(t), ifs = ifsOf(t), isDef = ifs.has(idef);
    const d = bases.find((b) => defTypes.has(b)), im = bases.find((b) => implTypes.has(b));
    if (d) add(`derives ${d.full}`, 'def');
    if (im) add(`derives ${im.full}`, 'impl');
    else if (ifs.has(type(META_IFACES[0]))) add('implements IMetaObject', 'impl');
    if (!d && [...ifs].some((i) => metaIfaces.has(i) && i !== type(META_IFACES[0]))) add('meta definition interface', 'meta');
    for (const x of t.methods) {
      if (!(x.flags & M.VIRT) || x.flags & M.NEWSLOT) continue;
      for (const b of bases) {
        const y: Method | undefined = b.methods.find((m) => m.flags & M.VIRT && M.sigEq(m, x));
        if (!y) continue;
        if (!isMod(b) && virt.has(y.id)) { if (isDef) selfCovered++; else add(`overrides ${y.id}`, 'vcall'); }
        if (!isMod(b) || y.flags & M.NEWSLOT) break;
      }
    }
    // Interface methods count only where mod code (this type or a mod base) supplies the body, not a game base class.
    const modChain = [t, ...bases].filter(isMod);
    for (const i of ifs) if (!isMod(i)) for (const m of i.methods) {
      if (!virt.has(m.id)) continue;
      const body = modChain.some((c) => c.methods.some((x) => x.flags & M.VIRT && (M.sigEq(m, x) || x.name === `${i.full.replace(/\+/g, '.')}.${m.name}`)));
      if (body && isDef) selfCovered++;
      else if (body) add(`implements ${m.id}`, 'vcall');
    }
  }

  // ---------- output ----------
  const types = (k?: Kind[]) => new Set(rows.filter((r) => r.type !== '(assembly)' && (!k || k.includes(r.kind))).map((r) => `${r.pkg}|${r.type}`)).size;
  const unknownAsm = asmInfo.filter((a) => !a.known);
  const summary = {
    packages: pkgs.length, unreadablePackages: unreadable, packagesWithAssemblies: new Set(asmInfo.map((a) => a.pkg)).size,
    assemblies: asmInfo.length, knownMvid: asmInfo.length - unknownAsm.length, unknownMvid: unknownAsm.length,
    unparsedAssemblies: skipped.length + asmInfo.filter((a) => !a.mvid).length,
    allowListedDefinitions: defs.size, closureVirtuals: virt.size, closureMethods: seen.size, definitionSelfOverridesCovered: selfCovered,
    matchingTypes: types(), fallbackTypes: types(['def', 'impl', 'meta']), workerAffectingTypes: types(AFFECTS),
    replacedGameModules: rows.filter((r) => r.type === '(assembly)').length,
    byKind: Object.fromEntries(Object.keys(EFFECT).map((k) => [k, rows.filter((r) => r.kind === k).length])),
  };
  if (!process.argv.includes('--summary')) {
    for (const pkg of [...new Set([...unknownAsm.map((a) => a.pkg), ...rows.map((r) => r.pkg)])].sort()) {
      console.log(`\n### ${pkg}\nassemblies not on the known-MVID list: ${unknownAsm.filter((a) => a.pkg === pkg).map((a) => a.name).join(', ') || '-'}`);
      const rs = rows.filter((r) => r.pkg === pkg);
      if (!rs.length) { console.log('no matching types: no effect on the worker'); continue; }
      console.log('| type | what it overrides | effect on the worker |\n|---|---|---|');
      for (const r of rs) console.log(`| ${r.type} | ${r.what} | ${EFFECT[r.kind]} |`);
    }
    if (skipped.length) console.log(`\nunparsed: ${skipped.join('; ')}`);
  }
  const out = arg('--csv', '');
  if (out) writeFileSync(out, 'package,assembly,type,what,kind,effect\n' + rows.map((r) => [r.pkg, r.asm, r.type, r.what, r.kind, EFFECT[r.kind]].map((v) => JSON.stringify(v)).join(',')).join('\n') + '\n');
  console.log('\n' + JSON.stringify(summary, null, 1));
} finally { rmSync(tmp, { recursive: true, force: true }); }
